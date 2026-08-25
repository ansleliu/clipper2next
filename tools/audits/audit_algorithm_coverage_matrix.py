#!/usr/bin/env python3
"""Audit algorithm release admission from executed, hash-bound evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Callable

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.collect_release_runtime_evidence import (  # noqa: E402
    collect_benchmark_names,
    collect_ctest_results,
    collect_git_state,
)
from tools.release.evidence_contract import (  # noqa: E402
    DEFAULT_CONTRACT_PATH,
    AlgorithmContract,
    ReleaseEvidenceContract,
    load_contract,
)


_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}(?:[0-9a-f]{24})?$")
_RUNTIME_FIELDS = {
    "schema_version",
    "contract_sha256",
    "profile_report_sha256",
    "git",
    "tests",
    "benchmarks",
    "inputs",
    "input_sha256",
}
_PROFILE_REPORT_FIELDS = {
    "schema_version",
    "status",
    "corpus_root",
    "contract_sha256",
    "profile_sha256",
    "profiles",
    "error_count",
    "errors_truncated",
    "error_categories",
    "errors",
}
_PROFILE_ENTRY_FIELDS = {
    "case_set",
    "path",
    "profile",
    "records",
    "sha256",
}
_INPUT_NAMES = {
    "contract",
    "profile_report",
    "ctest_junit",
    "benchmark_list",
}
_TEST_STATUSES = {"PASS", "FAIL", "SKIP"}
_PROFILE_STATUSES = {"PASS", "FAIL"}
_CASE_SETS = {"verification", "benchmark"}


@dataclass(frozen=True)
class Evidence:
    ok: bool
    status: str
    details: tuple[str, ...]


@dataclass(frozen=True)
class CoverageRow:
    id: str
    tests: Evidence
    profiles: Evidence
    benchmarks: Evidence
    status: str
    reasons: tuple[str, ...]


@dataclass(frozen=True)
class JsonArtifact:
    path: Path
    payload: dict[str, Any]
    sha256: str


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number {value}")


def _exact_fields(
    payload: dict[str, Any],
    expected: set[str],
    context: str,
) -> None:
    missing = sorted(expected - set(payload))
    unknown = sorted(set(payload) - expected)
    if missing:
        raise ValueError(f"{context} is missing fields: {missing}")
    if unknown:
        raise ValueError(f"{context} has unknown fields: {unknown}")


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return value


def _nonempty_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{context} must be a non-empty string")
    if value != value.strip():
        raise ValueError(f"{context} must not have surrounding whitespace")
    return value


def _sha256_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not _SHA256_RE.fullmatch(value):
        raise ValueError(f"{context} must be a lowercase SHA-256")
    return value


def _schema_version_one(value: Any, context: str) -> None:
    if isinstance(value, bool) or value != 1:
        raise ValueError(f"{context} must use schema_version 1")


def _integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{context} must be an integer")
    return value


def _validate_runtime_evidence(payload: dict[str, Any]) -> None:
    _exact_fields(payload, _RUNTIME_FIELDS, "runtime evidence")
    _schema_version_one(payload["schema_version"], "runtime evidence")
    contract_sha256 = _sha256_string(
        payload["contract_sha256"],
        "runtime evidence contract_sha256",
    )
    profile_report_sha256 = _sha256_string(
        payload["profile_report_sha256"],
        "runtime evidence profile_report_sha256",
    )

    git = _object(payload["git"], "runtime evidence git")
    _exact_fields(git, {"commit", "dirty"}, "runtime evidence git")
    commit = _nonempty_string(git["commit"], "runtime evidence git.commit")
    if not _GIT_COMMIT_RE.fullmatch(commit):
        raise ValueError("runtime evidence git.commit is not a full Git object ID")
    if not isinstance(git["dirty"], bool):
        raise ValueError("runtime evidence git.dirty must be boolean")

    tests = _object(payload["tests"], "runtime evidence tests")
    if not tests:
        raise ValueError("runtime evidence tests must not be empty")
    for name, status in tests.items():
        _nonempty_string(name, "runtime evidence test name")
        if status not in _TEST_STATUSES:
            raise ValueError(
                f"runtime evidence test {name!r} has invalid status {status!r}"
            )

    benchmarks = payload["benchmarks"]
    if not isinstance(benchmarks, list) or not benchmarks:
        raise ValueError("runtime evidence benchmarks must be a non-empty array")
    benchmark_names = [
        _nonempty_string(name, f"runtime evidence benchmarks[{index}]")
        for index, name in enumerate(benchmarks)
    ]
    if len(set(benchmark_names)) != len(benchmark_names):
        raise ValueError("runtime evidence benchmarks contains duplicate names")
    if any(not name.startswith("BM_") for name in benchmark_names):
        raise ValueError("runtime evidence benchmarks contains a non-benchmark name")

    inputs = _object(payload["inputs"], "runtime evidence inputs")
    input_sha256 = _object(
        payload["input_sha256"],
        "runtime evidence input_sha256",
    )
    _exact_fields(inputs, _INPUT_NAMES, "runtime evidence inputs")
    _exact_fields(input_sha256, _INPUT_NAMES, "runtime evidence input_sha256")
    for name in sorted(_INPUT_NAMES):
        _nonempty_string(inputs[name], f"runtime evidence inputs.{name}")
        _sha256_string(
            input_sha256[name],
            f"runtime evidence input_sha256.{name}",
        )
    link_errors: list[str] = []
    if input_sha256["contract"] != contract_sha256:
        link_errors.append(
            "runtime evidence contract SHA-256 does not match its contract input"
        )
    if input_sha256["profile_report"] != profile_report_sha256:
        link_errors.append(
            "runtime evidence profile report SHA-256 does not match its profile input"
        )
    if link_errors:
        raise ValueError("; ".join(link_errors))


def _combined_profile_sha256(entries: list[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for entry in sorted(
        entries,
        key=lambda item: (item["profile"], item["case_set"]),
    ):
        digest.update(
            (f"\0{entry['profile']}\0{entry['case_set']}\0{entry['sha256']}").encode(
                "utf-8"
            )
        )
    return digest.hexdigest()


def _validate_profile_report(payload: dict[str, Any]) -> None:
    _exact_fields(payload, _PROFILE_REPORT_FIELDS, "profile report")
    _schema_version_one(payload["schema_version"], "profile report")
    status = payload["status"]
    if status not in _PROFILE_STATUSES:
        raise ValueError(f"profile report has invalid status {status!r}")
    _nonempty_string(payload["corpus_root"], "profile report corpus_root")
    _sha256_string(
        payload["contract_sha256"],
        "profile report contract_sha256",
    )
    profile_sha256 = _sha256_string(
        payload["profile_sha256"],
        "profile report profile_sha256",
    )

    raw_entries = payload["profiles"]
    if not isinstance(raw_entries, list):
        raise ValueError("profile report profiles must be an array")
    if status == "PASS" and not raw_entries:
        raise ValueError("passing profile report profiles must not be empty")
    entries: list[dict[str, Any]] = []
    identities: set[tuple[str, str]] = set()
    for index, raw_entry in enumerate(raw_entries):
        entry = _object(raw_entry, f"profile report profiles[{index}]")
        _exact_fields(
            entry,
            _PROFILE_ENTRY_FIELDS,
            f"profile report profiles[{index}]",
        )
        case_set = _nonempty_string(
            entry["case_set"],
            f"profile report profiles[{index}].case_set",
        )
        if case_set not in _CASE_SETS:
            raise ValueError(f"profile report profiles[{index}].case_set is invalid")
        profile = _nonempty_string(
            entry["profile"],
            f"profile report profiles[{index}].profile",
        )
        _nonempty_string(
            entry["path"],
            f"profile report profiles[{index}].path",
        )
        records = _integer(
            entry["records"],
            f"profile report profiles[{index}].records",
        )
        if records <= 0:
            raise ValueError(
                f"profile report profiles[{index}].records must be positive"
            )
        _sha256_string(
            entry["sha256"],
            f"profile report profiles[{index}].sha256",
        )
        identity = (profile, case_set)
        if identity in identities:
            raise ValueError(
                f"profile report contains duplicate profile entry {identity!r}"
            )
        identities.add(identity)
        entries.append(entry)

    if _combined_profile_sha256(entries) != profile_sha256:
        raise ValueError(
            "profile report profile_sha256 does not match its profile entries"
        )

    error_count = _integer(payload["error_count"], "profile report error_count")
    if error_count < 0:
        raise ValueError("profile report error_count must be non-negative")
    if not isinstance(payload["errors_truncated"], bool):
        raise ValueError("profile report errors_truncated must be boolean")
    errors = payload["errors"]
    if not isinstance(errors, list) or any(
        not isinstance(error, str) or not error for error in errors
    ):
        raise ValueError("profile report errors must be an array of strings")
    if error_count < len(errors):
        raise ValueError("profile report error_count is below its error examples")
    categories = _object(
        payload["error_categories"],
        "profile report error_categories",
    )
    for category, count in categories.items():
        _nonempty_string(category, "profile report error category")
        if _integer(count, f"profile report category {category!r}") <= 0:
            raise ValueError("profile report category counts must be positive")
    if status == "PASS" and (
        error_count != 0 or errors or categories or payload["errors_truncated"]
    ):
        raise ValueError("passing profile report must not contain errors")


def _load_json_artifact(
    path: Path,
    label: str,
    validator: Callable[[dict[str, Any]], None],
) -> JsonArtifact:
    artifact_path = Path(path)
    try:
        raw = artifact_path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read {label} {artifact_path}: {error}") from error
    try:
        payload = json.loads(
            raw.decode("utf-8-sig"),
            object_pairs_hook=_unique_json_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {label} JSON in {artifact_path}: {error}") from error
    if not isinstance(payload, dict):
        raise ValueError(f"{label} {artifact_path} must be a JSON object")
    try:
        validator(payload)
    except ValueError as error:
        raise ValueError(f"invalid {label} {artifact_path}: {error}") from error
    return JsonArtifact(
        path=artifact_path.resolve(),
        payload=payload,
        sha256=hashlib.sha256(raw).hexdigest(),
    )


def _try_load_json_artifact(
    path: Path | None,
    label: str,
    validator: Callable[[dict[str, Any]], None],
) -> tuple[JsonArtifact | None, str | None]:
    if path is None:
        return None, f"{label} path was not provided"
    try:
        return _load_json_artifact(path, label, validator), None
    except ValueError as error:
        return None, str(error)


def _resolve_recorded_path(value: str, repository_root: Path) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = repository_root / path
    return path.resolve()


def _file_sha256(path: Path, label: str) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        raise ValueError(f"cannot read {label} {path}: {error}") from error


def _deduplicate(values: list[str]) -> tuple[str, ...]:
    return tuple(dict.fromkeys(values))


def _validate_recorded_inputs(
    runtime: JsonArtifact,
    repository_root: Path,
) -> list[str]:
    issues: list[str] = []
    inputs = runtime.payload["inputs"]
    expected_hashes = runtime.payload["input_sha256"]
    resolved: dict[str, Path] = {}
    for name in sorted(_INPUT_NAMES):
        path = _resolve_recorded_path(inputs[name], repository_root)
        resolved[name] = path
        try:
            actual_sha256 = _file_sha256(path, f"runtime input {name}")
        except ValueError as error:
            issues.append(str(error))
            continue
        if actual_sha256 != expected_hashes[name]:
            issues.append(f"runtime input {name} SHA-256 no longer matches evidence")

    junit_path = resolved.get("ctest_junit")
    if junit_path is not None and junit_path.is_file():
        try:
            actual_tests = collect_ctest_results(junit_path)
        except ValueError as error:
            issues.append(str(error))
        else:
            if actual_tests != runtime.payload["tests"]:
                issues.append(
                    "runtime test statuses do not match the recorded CTest JUnit input"
                )

    benchmark_path = resolved.get("benchmark_list")
    if benchmark_path is not None and benchmark_path.is_file():
        try:
            actual_benchmarks = collect_benchmark_names(benchmark_path)
        except ValueError as error:
            issues.append(str(error))
        else:
            if actual_benchmarks != set(runtime.payload["benchmarks"]):
                issues.append(
                    "runtime benchmark names do not match the recorded benchmark input"
                )
    return issues


def _validate_profile_files(
    profile: JsonArtifact,
    repository_root: Path,
) -> list[str]:
    issues: list[str] = []
    for entry in profile.payload["profiles"]:
        path = _resolve_recorded_path(entry["path"], repository_root)
        try:
            raw = path.read_bytes()
        except OSError as error:
            issues.append(f"cannot read profile artifact {path}: {error}")
            continue
        if hashlib.sha256(raw).hexdigest() != entry["sha256"]:
            issues.append(
                f"profile artifact {entry['profile']}/{entry['case_set']} "
                "SHA-256 no longer matches the report"
            )
            continue
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            issues.append(f"profile artifact {path} is not UTF-8")
            continue
        records = sum(1 for line in text.splitlines() if line.strip())
        if records != entry["records"]:
            issues.append(
                f"profile artifact {entry['profile']}/{entry['case_set']} "
                f"record count is {records}, expected {entry['records']}"
            )
    return issues


def validate_provenance(
    contract: ReleaseEvidenceContract,
    runtime: JsonArtifact | None,
    profile: JsonArtifact | None,
    repository_root: Path,
    *,
    input_errors: tuple[str, ...] = (),
) -> Evidence:
    issues = list(input_errors)
    if runtime is None and not any("runtime evidence" in item for item in issues):
        issues.append("runtime evidence is unavailable")
    if profile is None and not any("profile report" in item for item in issues):
        issues.append("profile report is unavailable")

    if runtime is not None:
        if runtime.payload["contract_sha256"] != contract.sha256:
            issues.append(
                "runtime evidence contract SHA-256 does not match the "
                "authoritative contract"
            )
        issues.extend(_validate_recorded_inputs(runtime, repository_root))
        if runtime.payload["git"]["dirty"]:
            issues.append("runtime evidence was collected from a dirty Git tree")

    if profile is not None:
        if profile.payload["contract_sha256"] != contract.sha256:
            issues.append(
                "profile report contract SHA-256 does not match the "
                "authoritative contract"
            )
        issues.extend(_validate_profile_files(profile, repository_root))

    if runtime is not None and profile is not None:
        if runtime.payload["profile_report_sha256"] != profile.sha256:
            issues.append(
                "runtime evidence profile report SHA-256 does not match the "
                "supplied profile report"
            )

    try:
        current_git = collect_git_state(repository_root)
    except ValueError as error:
        issues.append(str(error))
    else:
        if current_git["dirty"]:
            issues.append("current repository Git tree is dirty")
        if (
            runtime is not None
            and runtime.payload["git"]["commit"] != current_git["commit"]
        ):
            issues.append(
                "runtime evidence Git commit does not match the current repository"
            )

    details = _deduplicate(issues)
    if details:
        return Evidence(False, "BLOCKED", details)
    return Evidence(
        True,
        "PASS",
        ("contract, profile, raw runtime inputs, and Git provenance match",),
    )


def _tests_evidence(
    algorithm: AlgorithmContract,
    runtime: JsonArtifact | None,
) -> Evidence:
    if runtime is None:
        return Evidence(False, "BLOCKED", ("runtime evidence is unavailable",))
    statuses = runtime.payload["tests"]
    issues: list[str] = []
    for name in algorithm.required_tests:
        status = statuses.get(name)
        if status is None:
            issues.append(f"required test {name} is missing from runtime evidence")
        elif status != "PASS":
            issues.append(f"required test {name} is {status}, not PASS")
    if issues:
        return Evidence(False, "BLOCKED", tuple(issues))
    return Evidence(
        True,
        "PASS",
        (f"all {len(algorithm.required_tests)} required tests passed",),
    )


def _benchmark_evidence(
    algorithm: AlgorithmContract,
    runtime: JsonArtifact | None,
) -> Evidence:
    if runtime is None:
        return Evidence(False, "BLOCKED", ("runtime evidence is unavailable",))
    registered = set(runtime.payload["benchmarks"])
    missing = [name for name in algorithm.required_benchmarks if name not in registered]
    if missing:
        return Evidence(
            False,
            "BLOCKED",
            tuple(f"required benchmark {name} is not registered" for name in missing),
        )
    return Evidence(
        True,
        "PASS",
        (f"all {len(algorithm.required_benchmarks)} required benchmarks registered",),
    )


def _profile_evidence(
    algorithm: AlgorithmContract,
    profile: JsonArtifact | None,
) -> Evidence:
    if profile is None:
        return Evidence(False, "BLOCKED", ("profile report is unavailable",))
    issues: list[str] = []
    status = profile.payload["status"]
    if status != "PASS":
        issues.append(f"profile semantic validation status is {status}, not PASS")
    available = {
        (entry["profile"], entry["case_set"]) for entry in profile.payload["profiles"]
    }
    for profile_id in algorithm.required_profiles:
        for case_set in sorted(_CASE_SETS):
            if (profile_id, case_set) not in available:
                issues.append(
                    f"required profile {profile_id}/{case_set} is absent "
                    "from the semantic report"
                )
    if issues:
        return Evidence(False, "BLOCKED", tuple(issues))
    return Evidence(
        True,
        "PASS",
        (
            f"all {len(algorithm.required_profiles)} required profile pairs "
            "passed semantic validation",
        ),
    )


def build_row(
    algorithm: AlgorithmContract,
    runtime: JsonArtifact | None,
    profile: JsonArtifact | None,
    provenance: Evidence,
) -> CoverageRow:
    if not algorithm.release_gated:
        reason = (
            f"{algorithm.id} is deliberately not release-gated by the "
            "authoritative contract"
        )
        informational = Evidence(True, "NOT_GATED", (reason,))
        return CoverageRow(
            id=algorithm.id,
            tests=informational,
            profiles=informational,
            benchmarks=informational,
            status="PARTIAL",
            reasons=(reason,),
        )

    tests = _tests_evidence(algorithm, runtime)
    profiles = _profile_evidence(algorithm, profile)
    benchmarks = _benchmark_evidence(algorithm, runtime)
    reasons: list[str] = []
    for evidence in (provenance, tests, profiles, benchmarks):
        if not evidence.ok:
            reasons.extend(evidence.details)
    unique_reasons = _deduplicate(reasons)
    return CoverageRow(
        id=algorithm.id,
        tests=tests,
        profiles=profiles,
        benchmarks=benchmarks,
        status="READY" if not unique_reasons else "BLOCKED",
        reasons=unique_reasons,
    )


def status_counts(rows: list[CoverageRow]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        counts[row.status] = counts.get(row.status, 0) + 1
    return dict(sorted(counts.items()))


def _write_json(
    path: Path,
    contract: ReleaseEvidenceContract,
    runtime_path: Path | None,
    profile_path: Path | None,
    rows: list[CoverageRow],
    input_errors: tuple[str, ...],
) -> None:
    counts = status_counts(rows)
    payload = {
        "schema_version": 1,
        "overall_status": "BLOCKED" if counts.get("BLOCKED", 0) else "PASS",
        "contract_sha256": contract.sha256,
        "runtime_evidence": (
            str(runtime_path.resolve()) if runtime_path is not None else None
        ),
        "profile_report": (
            str(profile_path.resolve()) if profile_path is not None else None
        ),
        "input_errors": list(input_errors),
        "status_counts": counts,
        "rows": [asdict(row) for row in rows],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def _markdown_cell(evidence: Evidence) -> str:
    detail = "; ".join(evidence.details)
    return f"{evidence.status}: {detail}".replace("|", "\\|").replace("\n", " ")


def _write_markdown(
    path: Path,
    contract: ReleaseEvidenceContract,
    rows: list[CoverageRow],
) -> None:
    lines = [
        "# Clipper2Next Algorithm Release Admission",
        "",
        f"Contract SHA-256: `{contract.sha256}`",
        "",
        "| Algorithm | Executed tests | Semantic profiles | Registered benchmarks | Status | Reasons |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        reasons = "; ".join(row.reasons) if row.reasons else "none"
        lines.append(
            "| "
            + " | ".join(
                [
                    row.id,
                    _markdown_cell(row.tests),
                    _markdown_cell(row.profiles),
                    _markdown_cell(row.benchmarks),
                    row.status,
                    reasons.replace("|", "\\|").replace("\n", " "),
                ]
            )
            + " |"
        )
    counts = status_counts(rows)
    lines.extend(
        [
            "",
            "## Summary",
            "",
            f"- READY: {counts.get('READY', 0)}",
            f"- PARTIAL: {counts.get('PARTIAL', 0)}",
            f"- BLOCKED: {counts.get('BLOCKED', 0)}",
            "",
            "READY requires exact PASS runtime tests, registered benchmark names, "
            "passing semantic profiles, matching artifact SHA-256 values, and a "
            "clean matching Git commit. Source text is not admission evidence.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Audit algorithm release admission from runtime evidence."
    )
    parser.add_argument("--repository-root", type=Path, default=Path.cwd())
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT_PATH)
    parser.add_argument("--runtime-evidence", type=Path)
    parser.add_argument("--profile-report", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-md", type=Path)
    parser.add_argument("--fail-on-blocked", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    repository_root = args.repository_root.resolve()
    try:
        contract = load_contract(args.contract)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    runtime, runtime_error = _try_load_json_artifact(
        args.runtime_evidence,
        "runtime evidence",
        _validate_runtime_evidence,
    )
    profile, profile_error = _try_load_json_artifact(
        args.profile_report,
        "profile report",
        _validate_profile_report,
    )
    input_errors = tuple(
        error for error in (runtime_error, profile_error) if error is not None
    )
    provenance = validate_provenance(
        contract,
        runtime,
        profile,
        repository_root,
        input_errors=input_errors,
    )
    rows = [
        build_row(algorithm, runtime, profile, provenance)
        for algorithm in contract.algorithms.values()
    ]

    results_dir = repository_root / "benchmarks" / "results"
    output_json = (
        args.output_json.resolve()
        if args.output_json is not None
        else results_dir / "algorithm_coverage_matrix.json"
    )
    output_md = (
        args.output_md.resolve()
        if args.output_md is not None
        else results_dir / "algorithm_coverage_matrix.md"
    )
    try:
        _write_json(
            output_json,
            contract,
            args.runtime_evidence,
            args.profile_report,
            rows,
            input_errors,
        )
        _write_markdown(output_md, contract, rows)
    except OSError as error:
        print(f"error: cannot write coverage matrix: {error}", file=sys.stderr)
        return 2

    counts = status_counts(rows)
    blocked = counts.get("BLOCKED", 0)
    overall = "BLOCKED" if blocked else "PASS"
    print(
        f"status={overall} ready={counts.get('READY', 0)} "
        f"partial={counts.get('PARTIAL', 0)} blocked={blocked}"
    )
    print(f"matrix={output_md}")
    if args.fail_on_blocked and blocked:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
