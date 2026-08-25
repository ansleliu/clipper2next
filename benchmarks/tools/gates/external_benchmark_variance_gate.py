#!/usr/bin/env python3
import argparse
import json
import os
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.release_gate_policy import EXTERNAL_CORE_BENCHMARK_NAMES


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def load_benchmarks(path: Path) -> list[dict]:
    with path.open(encoding="utf-8") as handle:
        payload = json.load(handle)
    benchmarks = payload.get("benchmarks")
    if not isinstance(benchmarks, list):
        raise ValueError(f"benchmark JSON has no benchmark list: {path}")
    return benchmarks


def external_cv_rows(benchmarks: list[dict]) -> list[tuple[str, float]]:
    rows: list[tuple[str, float]] = []
    for benchmark in benchmarks:
        name = benchmark.get("name")
        if not isinstance(name, str):
            continue
        if not name.startswith("BM_external_") or not name.endswith("_cv"):
            continue
        cpu_time = benchmark.get("cpu_time")
        if not isinstance(cpu_time, int | float):
            continue
        cv_percent = float(cpu_time)
        if benchmark.get("aggregate_unit") == "percentage" and abs(cv_percent) <= 1.0:
            cv_percent *= 100.0
        rows.append((name.removesuffix("_cv"), cv_percent))
    return rows


def report_rows(rows: list[tuple[str, float]], max_cv_percent: float) -> list[dict]:
    return [
        {
            "name": name,
            "cv_percent": cv,
            "status": "NOISY" if cv > max_cv_percent else "PASS",
        }
        for name, cv in rows
    ]


def write_json_report(
    path: Path,
    *,
    status: str,
    max_cv_percent: float,
    rows: list[dict],
    missing: list[str] | None = None,
    reason: str | None = None,
) -> None:
    ensure_parent(path)
    payload: dict = {
        "status": status,
        "max_cv_percent": max_cv_percent,
        "rows": rows,
        "missing": missing or [],
    }
    if reason is not None:
        payload["reason"] = reason
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown_report(
    path: Path,
    *,
    status: str,
    max_cv_percent: float,
    rows: list[dict],
    missing: list[str] | None = None,
    reason: str | None = None,
) -> None:
    ensure_parent(path)
    lines = [
        "# External Benchmark Variance Gate",
        "",
        f"Status: **{status}**",
        f"Max CV threshold: **{max_cv_percent:.2f}%**",
    ]
    if reason is not None:
        lines.extend(["", f"Reason: {reason}"])
    lines.extend([
        "",
        "| Benchmark | CV | Status |",
        "| --- | ---: | --- |",
    ])
    for row in rows:
        lines.append(f"| {row['name']} | {row['cv_percent']:.2f}% | {row['status']} |")
    if missing:
        lines.extend(["", "## Missing benchmarks", ""])
        lines.extend(f"- {name}" for name in missing)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_reports(
    *,
    output_md: str | None,
    output_json: str | None,
    status: str,
    max_cv_percent: float,
    rows: list[dict],
    missing: list[str] | None = None,
    reason: str | None = None,
) -> None:
    if output_md:
        write_markdown_report(
            Path(output_md),
            status=status,
            max_cv_percent=max_cv_percent,
            rows=rows,
            missing=missing,
            reason=reason,
        )
    if output_json:
        write_json_report(
            Path(output_json),
            status=status,
            max_cv_percent=max_cv_percent,
            rows=rows,
            missing=missing,
            reason=reason,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--max-cv-percent", type=float, default=5.0)
    parser.add_argument("--require-calibrated-runner", action="store_true")
    parser.add_argument("--require-core-benchmarks", action="store_true")
    parser.add_argument("--output-md")
    parser.add_argument("--output-json")
    args = parser.parse_args()

    if args.require_calibrated_runner and os.environ.get("CLIPPER2NEXT_CALIBRATED_RUNNER") != "1":
        reason = "CLIPPER2NEXT_CALIBRATED_RUNNER=1 is required for a calibrated performance gate"
        print(
            reason,
            file=sys.stderr,
        )
        print("status=NOISY")
        write_reports(
            output_md=args.output_md,
            output_json=args.output_json,
            status="NOISY",
            max_cv_percent=args.max_cv_percent,
            rows=[],
            reason=reason,
        )
        return 2

    try:
        rows = external_cv_rows(load_benchmarks(Path(args.candidate)))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1

    if not rows and not args.require_core_benchmarks:
        print("no external benchmark cv rows found", file=sys.stderr)
        return 1

    output_rows = report_rows(rows, args.max_cv_percent)
    present = {name for name, _ in rows}
    missing = (
        [name for name in EXTERNAL_CORE_BENCHMARK_NAMES if name not in present]
        if args.require_core_benchmarks
        else []
    )
    noisy_rows = [row for row in output_rows if row["status"] == "NOISY"]
    status = "FAIL" if missing else "NOISY" if noisy_rows else "PASS"
    print(f"status={status}")
    print(f"max_cv_percent={args.max_cv_percent:.2f}")
    for row in output_rows:
        marker = " NOISY" if row["status"] == "NOISY" else ""
        print(f"{row['name']}: cv={row['cv_percent']:.2f}%{marker}")
    if missing:
        print(f"missing_benchmarks={len(missing)}")

    write_reports(
        output_md=args.output_md,
        output_json=args.output_json,
        status=status,
        max_cv_percent=args.max_cv_percent,
        rows=output_rows,
        missing=missing,
    )

    if missing:
        return 1
    return 2 if noisy_rows else 0


if __name__ == "__main__":
    raise SystemExit(main())
