#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.evidence_identity import (  # noqa: E402
    candidate_source_identity_at,
    protocol_identity_at,
    release_identity,
    sha256_file,
)
from benchmarks.tools.common.release_gate_policy import (  # noqa: E402
    EXTERNAL_CORE_BENCHMARK_NAMES,
    EXTERNAL_CORE_BENCHMARK_GROUPS,
)
from benchmarks.tools.gates import external_benchmark_variance_gate as variance_gate  # noqa: E402
from benchmarks.tools.gates import external_legacy_speedup_gate as speedup_gate  # noqa: E402
from tools.release.evidence_contract import load_contract  # noqa: E402


_IDENTITY_PATTERN = re.compile(r"^sha256:[0-9a-f]{64}$")
_PRIVATE_PATH_PATTERN = re.compile(
    r"(?i)(?:[a-z]:[\\/]|/(?:home|users|tmp|var/tmp)/)"
)
_CTEST_SUCCESS_PATTERN = re.compile(
    r"100% tests passed,\s*0 tests failed out of\s+(\d+)", re.IGNORECASE
)
_SANITIZER_FAILURE_PATTERNS = (
    "ERROR: AddressSanitizer",
    "ERROR: LeakSanitizer",
    "WARNING: ThreadSanitizer",
    "ThreadSanitizer: reported",
    "runtime error:",
)
_TEST_PROTOCOL_FILES = (
    "benchmarks/tools/evidence/run_ctest_evidence.py",
    "benchmarks/tools/evidence/archive_release_evidence.py",
)


@dataclass(frozen=True)
class EvidenceRow:
    name: str
    status: str
    artifact_id: str
    detail: str
    release_identity: str | None = None


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def default_results_dir() -> Path:
    return Path("benchmarks") / "results"


def resolve_path(path: str | Path) -> Path:
    candidate = Path(path)
    return candidate if candidate.is_absolute() else repo_root() / candidate


def _load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("JSON root must be an object")
    return value


def _artifact_path(root: Path, artifact_id: str) -> Path:
    if not isinstance(artifact_id, str) or not artifact_id:
        raise ValueError("artifact id must be a non-empty relative path")
    relative = Path(artifact_id)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"artifact id is not confined: {artifact_id!r}")
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError(f"artifact id escapes evidence root: {artifact_id!r}") from error
    return candidate


def _artifact_id(root: Path, path: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def _require_private_path_free(path: Path) -> None:
    if path.suffix.lower() not in {".json", ".jsonl", ".log", ".md", ".txt"}:
        return
    content = path.read_text(encoding="utf-8", errors="replace")
    match = _PRIVATE_PATH_PATTERN.search(content)
    if match is not None:
        raise ValueError(
            f"artifact contains a private absolute path near {match.group(0)!r}"
        )


def _safe_detail(error: Exception) -> str:
    return _PRIVATE_PATH_PATTERN.sub("<private-path>/", str(error))


def _require_identity(value: Any, field: str) -> str:
    if not isinstance(value, str) or _IDENTITY_PATTERN.fullmatch(value) is None:
        raise ValueError(f"{field} is not a SHA-256 identity")
    return value


def _git_output(*arguments: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repo_root()), *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def _aggregate_ref_paths(release_ref: str, names: tuple[str, ...]) -> str:
    digest = hashlib.sha256()
    for name in names:
        content = subprocess.run(
            ["git", "-C", str(repo_root()), "show", f"{release_ref}:{name}"],
            check=True,
            capture_output=True,
        ).stdout
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(content).digest())
    return f"sha256:{digest.hexdigest()}"


def _require_release_repository(
    metadata: dict[str, Any],
    release_ref: str,
    *,
    protocol: str | None = None,
) -> str:
    repository = metadata.get("git_repository_identity")
    if not isinstance(repository, dict):
        raise ValueError("runner metadata has no Git repository identity")
    if repository.get("dirty") is not False or repository.get("worktree_status_dirty") is not False:
        raise ValueError("release evidence was collected from a dirty repository")
    commit = _git_output("rev-parse", f"{release_ref}^{{commit}}")
    tree = _git_output("rev-parse", f"{release_ref}^{{tree}}")
    if repository.get("head_commit") != commit or repository.get("head_tree") != tree:
        raise ValueError("release evidence Git commit/tree differs from release ref")
    source = candidate_source_identity_at(repo_root(), release_ref)
    if repository.get("canonical_source_identity") != source:
        raise ValueError("release evidence source identity differs from release ref")
    if metadata.get("candidate_source_identity") != source:
        raise ValueError("runner source identity differs from its Git identity")
    expected_protocol = protocol or protocol_identity_at(repo_root(), release_ref)
    if metadata.get("protocol_identity") != expected_protocol:
        raise ValueError("runner protocol identity differs from release ref")
    expected_release = release_identity(repository)
    if metadata.get("release_identity") != expected_release:
        raise ValueError("runner release identity is not reproducible")
    return expected_release


def _measurement_identity(metadata: dict[str, Any]) -> str:
    fields = (
        "benchmark_executable_identity",
        "benchmark_artifact_id",
        "candidate_source_identity",
        "compiler_identity",
        "corpus_identity",
        "protocol_identity",
        "runtime_library_identity",
        "git_repository_identity",
        "release_identity",
    )
    payload = {field: metadata.get(field) for field in fields}
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return f"sha256:{hashlib.sha256(encoded.encode('utf-8')).hexdigest()}"


def _verify_archived_binary(
    root: Path, artifact_id: Any, expected_identity: Any, artifacts: set[Path]
) -> None:
    identity = _require_identity(expected_identity, "binary identity")
    path = _artifact_path(root, artifact_id)
    if not path.is_file():
        raise ValueError(f"missing archived binary {artifact_id!r}")
    if sha256_file(path) != identity:
        raise ValueError(f"archived binary hash changed: {artifact_id!r}")
    artifacts.add(path)


def _release_performance_errors(
    payload: dict[str, Any], *, require_variance_pass: bool
) -> list[str]:
    contract = load_contract()
    expected: dict[str, Any] = {
        "schema_version": 1,
        "evidence_mode": "release",
        "contract_sha256": contract.sha256,
        "speedup_mode": "default-unprepared",
        "speedup_status": "PASS",
        "calibrated_runner": True,
    }
    if require_variance_pass:
        expected["status"] = "PASS"
        expected["variance_status"] = "PASS"
    errors = [
        f"{field} is {payload.get(field)!r}, expected {value!r}"
        for field, value in expected.items()
        if payload.get(field) != value
    ]
    runner_id = payload.get("runner_id")
    if not isinstance(runner_id, str) or not runner_id.strip():
        errors.append("runner_id must be a non-empty string")
    minimums = {
        "repetitions": contract.performance["release_repetitions"],
        "min_time_seconds": contract.performance["min_time_seconds"],
        "min_warmup_time_seconds": contract.performance[
            "min_warmup_time_seconds"
        ],
        "min_pair_speedup": contract.performance["min_pair_speedup"],
        "min_geomean_speedup": contract.performance["min_geomean_speedup"],
    }
    for field, minimum in minimums.items():
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            errors.append(f"{field} must be numeric")
        elif value < minimum:
            errors.append(f"{field} is {value}, below {minimum}")
    maximum = payload.get("max_cv_percent")
    if isinstance(maximum, bool) or not isinstance(maximum, (int, float)):
        errors.append("max_cv_percent must be numeric")
    elif maximum > contract.performance["directional_max_cv_percent"]:
        errors.append("max_cv_percent exceeds the directional ceiling")
    elif require_variance_pass and maximum > contract.performance["release_max_cv_percent"]:
        errors.append("max_cv_percent exceeds the release ceiling")
    return errors


def _without_identity(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        key: value for key, value in payload.items() if key != "evidence_identity"
    }


def _equivalent_derived(first: Any, second: Any) -> bool:
    if isinstance(first, dict) and isinstance(second, dict):
        return first.keys() == second.keys() and all(
            _equivalent_derived(first[key], second[key]) for key in first
        )
    if isinstance(first, list) and isinstance(second, list):
        return len(first) == len(second) and all(
            _equivalent_derived(left, right)
            for left, right in zip(first, second, strict=True)
        )
    if (
        isinstance(first, (int, float))
        and not isinstance(first, bool)
        and isinstance(second, (int, float))
        and not isinstance(second, bool)
    ):
        return math.isclose(
            float(first), float(second), rel_tol=1e-12, abs_tol=1e-12
        )
    return type(first) is type(second) and first == second


def _verify_derived_results(
    benchmark: dict[str, Any],
    variance: dict[str, Any],
    speedup: dict[str, Any],
    summary: dict[str, Any],
) -> None:
    benchmark_rows = benchmark.get("benchmarks")
    if not isinstance(benchmark_rows, list):
        raise ValueError("combined benchmark has no row array")
    cv_rows = variance_gate.external_cv_rows(benchmark_rows)
    calculated_variance_rows = variance_gate.report_rows(
        cv_rows, float(summary["max_cv_percent"])
    )
    present = {name for name, _ in cv_rows}
    missing_variance = [
        name for name in EXTERNAL_CORE_BENCHMARK_NAMES if name not in present
    ]
    variance_status = (
        "FAIL"
        if missing_variance
        else "NOISY"
        if any(row["status"] == "NOISY" for row in calculated_variance_rows)
        else "PASS"
    )
    expected_variance = {
        "status": variance_status,
        "max_cv_percent": float(summary["max_cv_percent"]),
        "rows": calculated_variance_rows,
        "missing": missing_variance,
    }
    if not _equivalent_derived(
        _without_identity(variance), expected_variance
    ):
        raise ValueError("variance result is not reproducible from benchmark rows")

    records = speedup_gate.load_records_from_payload(
        benchmark, time_field="real_time"
    )
    rows, missing, skipped = speedup_gate.core_speedup_rows(records)
    geomean = speedup_gate.geomean_speedup(rows) if rows else None
    failed = [
        row
        for row in rows
        if float(row["speedup"]) < float(summary["min_pair_speedup"])
    ]
    status = "PASS"
    reason = None
    if missing:
        status = "FAIL"
        reason = f"missing next benchmark pairs for {len(missing)} legacy rows"
    elif failed:
        status = "FAIL"
        reason = (
            f"{len(failed)} legacy pairs are below min pair speedup "
            f"{float(summary['min_pair_speedup']):.3f}x"
        )
    elif geomean is None or geomean < float(summary["min_geomean_speedup"]):
        status = "FAIL"
        reason = (
            f"geomean speedup {geomean:.3f}x is below required "
            f"{float(summary['min_geomean_speedup']):.3f}x"
            if geomean is not None
            else "no external legacy benchmark pairs found with real_time"
        )
    expected_speedup: dict[str, Any] = {
        "status": status,
        "mode": "default-unprepared",
        "min_pair_speedup": float(summary["min_pair_speedup"]),
        "min_geomean_speedup": float(summary["min_geomean_speedup"]),
        "geomean_speedup": geomean,
        "require_pair_floor": True,
        "time_field": "real_time",
        "rows": rows,
        "missing": missing,
        "skipped": skipped,
    }
    if reason is not None:
        expected_speedup["reason"] = reason
    if not _equivalent_derived(
        _without_identity(speedup), expected_speedup
    ):
        raise ValueError("speedup result is not reproducible from benchmark rows")


def check_performance_bundle(
    root: Path,
    summary_path: Path,
    *,
    directional: bool,
    release_ref: str,
) -> tuple[EvidenceRow, set[Path]]:
    artifacts: set[Path] = set()
    try:
        if not summary_path.is_file():
            raise ValueError("missing performance summary")
        bundle_root = summary_path.parent
        summary = _load_json(summary_path)
        _require_private_path_free(summary_path)
        errors = _release_performance_errors(
            summary, require_variance_pass=not directional
        )
        if directional:
            status = summary.get("status")
            variance = summary.get("variance_status")
            if status not in {"PASS", "NOISY"}:
                errors.append("directional status must be PASS or NOISY")
            if status == "NOISY" and variance != "NOISY":
                errors.append("NOISY directional summary must identify variance")
        if errors:
            raise ValueError("; ".join(errors))

        metadata_path = _artifact_path(
            bundle_root, summary.get("runner_metadata")
        )
        metadata = _load_json(metadata_path)
        _require_private_path_free(metadata_path)
        if sha256_file(metadata_path) != summary.get("runner_metadata_identity"):
            raise ValueError("runner metadata hash differs from summary")
        release = _require_release_repository(metadata, release_ref)
        if summary.get("release_identity") != release:
            raise ValueError("summary release identity differs from runner")
        measurement = _require_identity(
            metadata.get("evidence_identity"), "runner evidence_identity"
        )
        if measurement != _measurement_identity(metadata):
            raise ValueError("runner evidence_identity is not reproducible")
        if summary.get("evidence_identity") != measurement:
            raise ValueError("summary evidence_identity differs from runner")
        if metadata.get("identity_complete") is not True:
            raise ValueError("runner identity is incomplete")

        _verify_archived_binary(
            bundle_root,
            metadata.get("benchmark_artifact_id"),
            metadata.get("benchmark_executable_identity"),
            artifacts,
        )
        runtime = metadata.get("runtime_library_identity")
        if not isinstance(runtime, dict) or runtime.get("linkage") != "shared":
            raise ValueError("release benchmark must bind the shared runtime")
        if any("path" in key.lower() for key in runtime):
            raise ValueError("runtime identity leaks a filesystem path")
        if not isinstance(runtime.get("soname"), str) or not runtime["soname"]:
            raise ValueError("runtime identity has no SONAME/basename")
        _verify_archived_binary(
            bundle_root,
            runtime.get("artifact_id"),
            runtime.get("sha256"),
            artifacts,
        )

        json_identity_locations = (
            (summary.get("benchmark_json"), "context"),
            (summary.get("variance_json"), None),
            (summary.get("speedup_json"), None),
        )
        identity_payloads: dict[str, dict[str, Any]] = {}
        for artifact_id, nested in json_identity_locations:
            path = _artifact_path(bundle_root, artifact_id)
            payload = _load_json(path)
            identity_payloads[str(artifact_id)] = payload
            identity_owner = payload.get(nested) if nested else payload
            if not isinstance(identity_owner, dict) or identity_owner.get(
                "evidence_identity"
            ) != measurement:
                raise ValueError(f"artifact identity differs: {artifact_id!r}")
            _require_private_path_free(path)
            artifacts.add(path)

        group_dir = _artifact_path(
            bundle_root, summary.get("benchmark_group_dir")
        )
        group_json = sorted(group_dir.glob("*.json"))
        if len(group_json) != len(EXTERNAL_CORE_BENCHMARK_GROUPS):
            raise ValueError("benchmark raw group cardinality changed")
        merged_rows: list[dict[str, Any]] = []
        group_contexts: list[dict[str, Any]] = []
        group_records: list[dict[str, Any]] = []
        for ordinal, (path, group_contract) in enumerate(
            zip(group_json, EXTERNAL_CORE_BENCHMARK_GROUPS, strict=True), start=1
        ):
            group_name, expected_benchmarks = group_contract
            if path.stem != f"{ordinal:02d}_{group_name}":
                raise ValueError("benchmark raw group ordering changed")
            payload = _load_json(path)
            context = payload.get("context")
            if not isinstance(context, dict) or context.get(
                "evidence_identity"
            ) != measurement:
                raise ValueError(f"raw benchmark identity differs: {path.name}")
            rows = payload.get("benchmarks")
            if not isinstance(rows, list):
                raise ValueError(f"raw benchmark rows are missing: {path.name}")
            observed = {
                name
                for row in rows
                if isinstance(row, dict)
                and isinstance((name := row.get("name")), str)
            }
            observed_bases = set()
            for name in observed:
                base = name
                for suffix in ("_mean", "_median", "_stddev", "_cv"):
                    if base.endswith(suffix):
                        base = base.removesuffix(suffix)
                        break
                observed_bases.add(base)
            if observed_bases != set(expected_benchmarks):
                raise ValueError(f"raw benchmark coverage changed: {path.name}")
            merged_rows.extend(rows)
            group_contexts.append(context)
            group_records.append(
                {
                    "name": group_name,
                    "benchmarks": list(expected_benchmarks),
                    "filter": "^(" + "|".join(expected_benchmarks) + ")$",
                    "json": f"{group_dir.name}/{path.name}",
                    "log": f"{group_dir.name}/{path.with_suffix('.log').name}",
                }
            )
            _require_private_path_free(path)
            artifacts.add(path)
            group_log = path.with_suffix(".log")
            if not group_log.is_file() or group_log.stat().st_size == 0:
                raise ValueError(f"missing raw benchmark log: {group_log.name}")
            _require_private_path_free(group_log)
            artifacts.add(group_log)

        benchmark_payload = identity_payloads[str(summary["benchmark_json"])]
        expected_combined = {
            "context": group_contexts[0] if group_contexts else {},
            "benchmarks": merged_rows,
            "clipper2next_measurement": {
                "isolation": "pairwise-process",
                "groups": group_records,
                "group_contexts": group_contexts,
            },
        }
        if benchmark_payload != expected_combined:
            raise ValueError("combined benchmark is not reproducible from raw groups")
        _verify_derived_results(
            benchmark_payload,
            identity_payloads[str(summary["variance_json"])],
            identity_payloads[str(summary["speedup_json"])],
            summary,
        )

        for field in (
            "benchmark_log",
            "variance_log",
            "variance_markdown",
            "speedup_log",
            "speedup_markdown",
            "summary_markdown",
        ):
            artifact_id = summary.get(field)
            if not artifact_id:
                raise ValueError(f"summary is missing {field}")
            path = _artifact_path(bundle_root, artifact_id)
            if not path.is_file() or path.stat().st_size == 0:
                raise ValueError(f"missing or empty artifact {artifact_id!r}")
            _require_private_path_free(path)
            artifacts.add(path)
        artifacts.update({summary_path, metadata_path})
        accepted = "DIRECTIONAL" if directional and summary["status"] == "NOISY" else "PASS"
        return (
            EvidenceRow(
                summary_path.stem,
                accepted,
                _artifact_id(root, summary_path),
                "complete performance identity chain verified",
                release,
            ),
            artifacts,
        )
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        return (
            EvidenceRow(
                summary_path.stem,
                "BLOCKED",
                summary_path.name,
                _safe_detail(error),
            ),
            artifacts,
        )


def check_test_manifest(
    root: Path, manifest_path: Path, *, release_ref: str
) -> tuple[EvidenceRow, set[Path]]:
    artifacts: set[Path] = set()
    try:
        bundle_root = manifest_path.parent
        manifest = _load_json(manifest_path)
        _require_private_path_free(manifest_path)
        required = {
            "schema_version": 1,
            "status": "PASS",
            "exit_code": 0,
            "tests_failed": 0,
            "sanitizer_failures": 0,
        }
        for field, expected in required.items():
            if manifest.get(field) != expected:
                raise ValueError(f"{field} is not {expected!r}")
        tests_total = manifest.get("tests_total")
        if not isinstance(tests_total, int) or tests_total <= 0:
            raise ValueError("tests_total must be positive")
        if not isinstance(manifest.get("compiler_identity"), str) or not manifest[
            "compiler_identity"
        ]:
            raise ValueError("test manifest has no compiler identity")
        if manifest.get("build_configuration") not in {
            "Release",
            "ASan-UBSan",
            "TSan",
            "Fuzz-UBSan",
        }:
            raise ValueError("test manifest has an invalid build configuration")
        expected_configurations = {
            "ctest-linux-gcc-asan-ubsan": "ASan-UBSan",
            "ctest-linux-gcc-fuzz-smoke": "Fuzz-UBSan",
            "ctest-linux-gcc-tsan": "TSan",
        }
        name = manifest.get("name")
        if expected_configurations.get(name) != manifest.get(
            "build_configuration"
        ):
            raise ValueError("test suite and build configuration differ")
        _require_identity(
            manifest.get("cmake_cache_identity"), "CMake cache identity"
        )
        repository = manifest.get("git_repository_identity")
        metadata = {
            "git_repository_identity": repository,
            "candidate_source_identity": manifest.get("candidate_source_identity"),
            "protocol_identity": manifest.get("protocol_identity"),
            "release_identity": manifest.get("release_identity"),
        }
        release = _require_release_repository(
            metadata,
            release_ref,
            protocol=_aggregate_ref_paths(release_ref, _TEST_PROTOCOL_FILES),
        )

        log_path = _artifact_path(bundle_root, manifest.get("log_artifact"))
        if sha256_file(log_path) != manifest.get("log_sha256"):
            raise ValueError("test log hash differs from manifest")
        log = log_path.read_text(encoding="utf-8", errors="replace")
        match = _CTEST_SUCCESS_PATTERN.search(log)
        if match is None or int(match.group(1)) != tests_total:
            raise ValueError("test log does not contain the declared zero-failure summary")
        if any(pattern.lower() in log.lower() for pattern in _SANITIZER_FAILURE_PATTERNS):
            raise ValueError("test log contains a sanitizer failure")
        _require_private_path_free(log_path)
        artifacts.update({manifest_path, log_path})
        for item in manifest.get("artifacts", []):
            if not isinstance(item, dict):
                raise ValueError("test artifact record is not an object")
            _verify_archived_binary(
                bundle_root,
                item.get("artifact_id"),
                item.get("sha256"),
                artifacts,
            )
        if not manifest.get("artifacts"):
            raise ValueError("test manifest does not bind any tested binary")
        return (
            EvidenceRow(
                manifest_path.stem,
                "PASS",
                _artifact_id(root, manifest_path),
                f"{tests_total}/{tests_total} and binary identities verified",
                release,
            ),
            artifacts,
        )
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        return (
            EvidenceRow(
                manifest_path.stem,
                "BLOCKED",
                manifest_path.name,
                _safe_detail(error),
            ),
            artifacts,
        )


def overall_status(rows: list[EvidenceRow]) -> str:
    return "PASS" if all(row.status in {"PASS", "DIRECTIONAL"} for row in rows) else "BLOCKED"


def _write_reports(
    json_path: Path,
    markdown_path: Path,
    status: str,
    scope: str,
    rows: list[EvidenceRow],
    root: Path,
    artifacts: set[Path],
) -> None:
    release_ids = sorted(
        {row.release_identity for row in rows if row.release_identity is not None}
    )
    if status == "PASS" and len(release_ids) != 1:
        status = "BLOCKED"
        rows.append(
            EvidenceRow(
                "release_identity",
                "BLOCKED",
                "release",
                "evidence does not share one release identity",
            )
        )
    artifact_hashes = {
        _artifact_id(root, path): sha256_file(path)
        for path in sorted(artifacts)
        if path.is_file()
    }
    payload = {
        "schema_version": 2,
        "artifact_scope": scope,
        "release_identity": release_ids[0] if len(release_ids) == 1 else None,
        "status": status,
        "rows": [row.__dict__ for row in rows],
        "artifacts": artifact_hashes,
    }
    json_path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    lines = [
        "# Release Evidence Archive Gate",
        "",
        f"Status: **{status}**",
        f"Artifact scope: **{scope}**",
        f"Release identity: `{payload['release_identity'] or ''}`",
        "",
        "| Evidence | Status | Artifact | Detail |",
        "| --- | --- | --- | --- |",
    ]
    lines.extend(
        f"| {row.name} | {row.status} | `{row.artifact_id}` | {row.detail} |"
        for row in rows
    )
    markdown_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify a complete, path-free release evidence archive."
    )
    parser.add_argument("--results-dir", default=str(default_results_dir()))
    parser.add_argument("--prefix", default="release")
    parser.add_argument("--release-ref", default="HEAD")
    parser.add_argument("--windows-calibrated-summary")
    parser.add_argument("--linux-calibrated-summary")
    parser.add_argument("--pgo-summary")
    parser.add_argument("--require-pgo", action="store_true")
    parser.add_argument("--linux-ubsan-manifest")
    parser.add_argument("--linux-fuzz-ubsan-manifest")
    parser.add_argument("--linux-tsan-manifest")
    parser.add_argument("--output-json")
    parser.add_argument("--output-md")
    args = parser.parse_args()
    if args.pgo_summary and not args.require_pgo:
        parser.error("--pgo-summary requires --require-pgo")

    root = resolve_path(args.results_dir).resolve()
    windows = resolve_path(args.windows_calibrated_summary) if args.windows_calibrated_summary else root / "release_calibrated_external_calibrated_external_summary.json"
    linux = resolve_path(args.linux_calibrated_summary) if args.linux_calibrated_summary else root / "release_linux_calibrated_external_calibrated_external_summary.json"
    rows: list[EvidenceRow] = []
    artifacts: set[Path] = set()
    if windows.is_file():
        row, used = check_performance_bundle(
            root, windows, directional=True, release_ref=args.release_ref
        )
        rows.append(row)
        artifacts.update(used)
    row, used = check_performance_bundle(
        root, linux, directional=False, release_ref=args.release_ref
    )
    rows.append(row)
    artifacts.update(used)

    manifest_arguments = (
        (args.linux_ubsan_manifest, "ctest-linux-gcc-asan-ubsan.json"),
        (args.linux_fuzz_ubsan_manifest, "ctest-linux-gcc-fuzz-smoke.json"),
        (args.linux_tsan_manifest, "ctest-linux-gcc-tsan.json"),
    )
    for configured, name in manifest_arguments:
        manifest = resolve_path(configured) if configured else root / "ci" / name
        row, used = check_test_manifest(
            root, manifest, release_ref=args.release_ref
        )
        rows.append(row)
        artifacts.update(used)

    scope = "linux-canonical+windows-directional"
    if args.require_pgo:
        scope = "canonical+pgo"
        pgo = resolve_path(args.pgo_summary) if args.pgo_summary else root / "release_msvc_pgo_msvc_pgo_summary.json"
        row, used = check_performance_bundle(
            root, pgo, directional=False, release_ref=args.release_ref
        )
        rows.insert(2, row)
        artifacts.update(used)

    status = overall_status(rows)
    output_json = resolve_path(args.output_json) if args.output_json else root / f"{args.prefix}_release_evidence_archive.json"
    output_md = resolve_path(args.output_md) if args.output_md else root / f"{args.prefix}_release_evidence_archive.md"
    _write_reports(output_json, output_md, status, scope, rows, root, artifacts)
    final_status = _load_json(output_json)["status"]
    print(f"status={final_status}")
    print(f"summary={output_md.name}")
    return 0 if final_status == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
