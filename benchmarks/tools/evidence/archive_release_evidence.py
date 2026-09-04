#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.release.evidence_contract import load_contract  # noqa: E402


@dataclass(frozen=True)
class EvidenceRow:
    name: str
    status: str
    path: str
    detail: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def default_results_dir() -> Path:
    return Path("benchmarks") / "results"


def resolve_path(path: str | Path) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate
    return repo_root() / candidate


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _release_performance_errors(
    payload: dict,
    *,
    require_variance_pass: bool = True,
) -> list[str]:
    contract = load_contract()
    performance = contract.performance
    expected = {
        "schema_version": 1,
        "evidence_mode": "release",
        "contract_sha256": contract.sha256,
        "speedup_mode": "default-unprepared",
        "speedup_status": "PASS",
    }
    if require_variance_pass:
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
        "repetitions": performance["release_repetitions"],
        "min_time_seconds": performance["min_time_seconds"],
        "min_pair_speedup": performance["min_pair_speedup"],
        "min_geomean_speedup": performance["min_geomean_speedup"],
    }
    for field, minimum in minimums.items():
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            errors.append(f"{field} must be numeric")
        elif value < minimum:
            errors.append(f"{field} is {value}, below {minimum}")

    max_cv = payload.get("max_cv_percent")
    if isinstance(max_cv, bool) or not isinstance(max_cv, (int, float)):
        errors.append("max_cv_percent must be numeric")
    elif max_cv > performance["release_max_cv_percent"]:
        errors.append(
            f"max_cv_percent is {max_cv}, above "
            f"{performance['release_max_cv_percent']}"
        )
    return errors


def check_summary(
    path: Path,
    *,
    calibrated_required: bool,
    release_performance_required: bool = False,
) -> EvidenceRow:
    if not path.exists():
        return EvidenceRow(path.stem, "BLOCKED", str(path), "missing summary artifact")
    try:
        payload = load_json(path)
    except (OSError, json.JSONDecodeError) as error:
        return EvidenceRow(path.stem, "FAIL", str(path), f"invalid json: {error}")

    status = payload.get("status")
    calibrated_runner = payload.get("calibrated_runner")
    if status != "PASS":
        return EvidenceRow(path.stem, "BLOCKED", str(path), f"status is {status!r}, expected 'PASS'")
    if calibrated_required and calibrated_runner is not True:
        return EvidenceRow(path.stem, "BLOCKED", str(path), "summary is not from a calibrated runner")
    if release_performance_required:
        try:
            errors = _release_performance_errors(payload)
        except ValueError as error:
            return EvidenceRow(
                path.stem,
                "FAIL",
                str(path),
                f"cannot validate release contract: {error}",
            )
        if errors:
            return EvidenceRow(
                path.stem,
                "BLOCKED",
                str(path),
                "; ".join(errors),
            )
    return EvidenceRow(path.stem, "PASS", str(path), "summary accepted")


def check_directional_summary(path: Path) -> EvidenceRow:
    if not path.exists():
        return EvidenceRow(
            path.stem,
            "OPTIONAL",
            str(path),
            "directional platform summary was not archived",
        )
    try:
        payload = load_json(path)
    except (OSError, json.JSONDecodeError) as error:
        return EvidenceRow(path.stem, "FAIL", str(path), f"invalid json: {error}")
    if payload.get("status") == "PASS":
        return check_summary(
            path,
            calibrated_required=True,
            release_performance_required=True,
        )
    if payload.get("status") != "NOISY" or payload.get("variance_status") != "NOISY":
        return EvidenceRow(
            path.stem,
            "BLOCKED",
            str(path),
            "directional summary must be PASS or variance-qualified NOISY",
        )
    if payload.get("calibrated_runner") is not True:
        return EvidenceRow(
            path.stem,
            "BLOCKED",
            str(path),
            "directional summary is not from a calibrated runner",
        )
    try:
        errors = _release_performance_errors(
            payload,
            require_variance_pass=False,
        )
    except ValueError as error:
        return EvidenceRow(
            path.stem,
            "FAIL",
            str(path),
            f"cannot validate release contract: {error}",
        )
    if errors:
        return EvidenceRow(
            path.stem,
            "BLOCKED",
            str(path),
            "; ".join(errors),
        )
    return EvidenceRow(
        path.stem,
        "DIRECTIONAL",
        str(path),
        "speedup gate passed; variance exceeded the E3 ceiling",
    )


def check_log(path: Path) -> EvidenceRow:
    if not path.exists():
        return EvidenceRow(path.stem, "BLOCKED", str(path), "missing log artifact")
    if path.stat().st_size == 0:
        return EvidenceRow(path.stem, "FAIL", str(path), "empty log artifact")
    return EvidenceRow(path.stem, "PASS", str(path), "log archived")


def overall_status(rows: list[EvidenceRow]) -> str:
    if any(row.status == "FAIL" for row in rows):
        return "FAIL"
    if any(row.status == "BLOCKED" for row in rows):
        return "BLOCKED"
    return "PASS"


def write_json_report(
    path: Path,
    status: str,
    artifact_scope: str,
    rows: list[EvidenceRow],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "artifact_scope": artifact_scope,
        "status": status,
        "rows": [row.__dict__ for row in rows],
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown_report(
    path: Path,
    status: str,
    artifact_scope: str,
    rows: list[EvidenceRow],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Release Evidence Archive Gate",
        "",
        f"Status: **{status}**",
        f"Artifact scope: **{artifact_scope}**",
        "",
        "| Evidence | Status | Path | Detail |",
        "| --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(f"| {row.name} | {row.status} | `{row.path}` | {row.detail} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def default_output_json(results_dir: Path, prefix: str) -> Path:
    return results_dir / f"{prefix}_release_evidence_archive.json"


def default_output_md(results_dir: Path, prefix: str) -> Path:
    return results_dir / f"{prefix}_release_evidence_archive.md"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify that release-blocking external evidence artifacts are archived.")
    parser.add_argument("--results-dir", default=str(default_results_dir()))
    parser.add_argument("--prefix", default="release")
    parser.add_argument("--windows-calibrated-summary")
    parser.add_argument("--linux-calibrated-summary")
    parser.add_argument("--pgo-summary")
    parser.add_argument(
        "--require-pgo",
        action="store_true",
        help="Require a PGO artifact to satisfy the complete release performance contract",
    )
    parser.add_argument("--linux-ubsan-log")
    parser.add_argument("--linux-fuzz-ubsan-log")
    parser.add_argument("--linux-tsan-log")
    parser.add_argument("--output-json")
    parser.add_argument("--output-md")
    args = parser.parse_args()
    if args.pgo_summary and not args.require_pgo:
        parser.error("--pgo-summary requires --require-pgo")

    results_dir = resolve_path(args.results_dir)
    windows_calibrated_summary = (
        resolve_path(args.windows_calibrated_summary)
        if args.windows_calibrated_summary
        else results_dir / "release_calibrated_external_calibrated_external_summary.json"
    )
    linux_calibrated_summary = (
        resolve_path(args.linux_calibrated_summary)
        if args.linux_calibrated_summary
        else results_dir
        / "release_linux_calibrated_external_calibrated_external_summary.json"
    )

    rows = [
        check_directional_summary(windows_calibrated_summary),
        check_summary(
            linux_calibrated_summary,
            calibrated_required=True,
            release_performance_required=True,
        ),
        check_log(resolve_path(args.linux_ubsan_log) if args.linux_ubsan_log else (
            results_dir / "ci" / "ctest-linux-gcc-asan-ubsan.log")),
        check_log(resolve_path(args.linux_fuzz_ubsan_log) if args.linux_fuzz_ubsan_log else (
            results_dir / "ci" / "ctest-linux-gcc-fuzz-smoke.log")),
        check_log(resolve_path(args.linux_tsan_log) if args.linux_tsan_log else (
            results_dir / "ci" / "ctest-linux-gcc-tsan.log")),
    ]
    artifact_scope = "linux-canonical+windows-directional"
    if args.require_pgo:
        artifact_scope = "canonical+pgo"
        pgo_summary = resolve_path(args.pgo_summary) if args.pgo_summary else (
            results_dir / "release_msvc_pgo_msvc_pgo_summary.json")
        rows.insert(
            2,
            check_summary(
                pgo_summary,
                calibrated_required=True,
                release_performance_required=True,
            ),
        )
    status = overall_status(rows)

    output_json = resolve_path(args.output_json) if args.output_json else default_output_json(results_dir, args.prefix)
    output_md = resolve_path(args.output_md) if args.output_md else default_output_md(results_dir, args.prefix)
    write_json_report(output_json, status, artifact_scope, rows)
    write_markdown_report(output_md, status, artifact_scope, rows)

    print(f"status={status}")
    print(f"summary={output_md}")
    return 0 if status == "PASS" else (1 if status == "FAIL" else 2)


if __name__ == "__main__":
    raise SystemExit(main())
