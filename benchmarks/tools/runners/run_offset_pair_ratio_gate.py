#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


AGGREGATE_SUFFIXES = ("_mean", "_median", "_stddev", "_cv")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def next_root() -> Path:
    return repo_root()


def default_benchmark_dir() -> Path:
    return Path("build") / "msvc-oracle-benchmarks" / "benchmarks" / "Release"


def resolve_path(path: str) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate
    return repo_root() / candidate


def run_command(command: list[str], log_path: Path) -> int:
    with log_path.open("w", encoding="utf-8") as handle:
        handle.write(" ".join(command))
        handle.write("\n\n")
        completed = subprocess.run(
            command,
            cwd=repo_root(),
            stdout=handle,
            stderr=subprocess.STDOUT,
            check=False,
        )
    return completed.returncode


def overall_status_for_rows(rows: list[dict[str, object]]) -> str:
    if not rows:
        return "FAIL"
    pass_count = sum(1 for row in rows if row.get("status") == "PASS")
    required_passes = (len(rows) // 2) + 1
    return "PASS" if pass_count >= required_passes else "FAIL"


def benchmark_base_name(name: str) -> str:
    for suffix in AGGREGATE_SUFFIXES:
        if name.endswith(suffix):
            return name.removesuffix(suffix)
    return name


def unique_offset_benchmark_order(names: list[str]) -> list[str]:
    order: list[str] = []
    seen: set[str] = set()
    for name in names:
        base_name = benchmark_base_name(name)
        if base_name not in {"BM_legacy_offset", "BM_next_offset"}:
            continue
        if base_name in seen:
            continue
        seen.add(base_name)
        order.append(base_name)
    return order


def has_adjacent_offset_pair_order(names: list[str]) -> bool:
    return unique_offset_benchmark_order(names) == ["BM_legacy_offset", "BM_next_offset"]


def candidate_has_adjacent_offset_pair_order(candidate: Path) -> bool:
    with candidate.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    names = [benchmark["name"] for benchmark in data.get("benchmarks", [])]
    return has_adjacent_offset_pair_order(names)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, required=True)
    parser.add_argument("--baseline-prefix", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--min-time", type=float, default=1.0)
    parser.add_argument("--max-ratio-regression-percent", type=float, default=5.0)
    parser.add_argument("--max-cv-percent", type=float, default=15.0)
    parser.add_argument(
        "--benchmark-dir",
        default=str(default_benchmark_dir()),
    )
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if args.runs < 1:
        print("--runs must be >= 1", file=sys.stderr)
        return 2

    output_dir = resolve_path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    baseline = output_dir / f"{args.baseline_prefix}_offset.json"
    if not baseline.exists():
        print(f"missing baseline JSON: {baseline}", file=sys.stderr)
        return 1

    tools_dir = next_root() / "benchmarks" / "tools"
    runner = tools_dir / "runners" / "run_benchmark_gate.py"
    comparator = tools_dir / "compare" / "compare_offset_pair_ratio.py"
    rows: list[dict[str, object]] = []

    for run_number in range(1, args.runs + 1):
        run_prefix = f"{args.prefix}_run{run_number}"
        candidate = output_dir / f"{run_prefix}_offset.json"
        gate_log = output_dir / f"{run_prefix}_offset_gate.log"
        compare_log = output_dir / f"{run_prefix}_offset_pair_ratio_compare.log"

        gate_command = [
            sys.executable,
            str(runner),
            "--suite",
            "offset",
            "--output-dir",
            str(output_dir),
            "--prefix",
            run_prefix,
            "--repetitions",
            str(args.repetitions),
            "--min-time",
            str(args.min_time),
            "--benchmark-dir",
            str(resolve_path(args.benchmark_dir)),
        ]
        if args.skip_build:
            gate_command.append("--skip-build")
        gate_status = run_command(gate_command, gate_log)
        compare_status = 1
        if gate_status == 0 and candidate.exists():
            if candidate_has_adjacent_offset_pair_order(candidate):
                compare_command = [
                    sys.executable,
                    str(comparator),
                    "--baseline",
                    str(baseline),
                    "--candidate",
                    str(candidate),
                    "--max-ratio-regression-percent",
                    str(args.max_ratio_regression_percent),
                    "--max-cv-percent",
                    str(args.max_cv_percent),
                ]
                compare_status = run_command(compare_command, compare_log)
            else:
                compare_log.write_text(
                    "offset benchmark rows are not adjacent as legacy/next pair\n",
                    encoding="utf-8",
                )

        status = "PASS" if gate_status == 0 and compare_status == 0 else "FAIL"
        rows.append(
            {
                "run": run_number,
                "candidate": candidate.name,
                "gate_log": gate_log.name,
                "compare_log": compare_log.name,
                "status": status,
            }
        )

    overall_status = overall_status_for_rows(rows)
    report = output_dir / f"{args.prefix}_offset_pair_ratio_report.md"
    lines = [
        "# Offset Paired Ratio Gate",
        "",
        f"Runs: {args.runs}",
        f"Baseline: `{baseline.name}`",
        f"Max ratio regression: {args.max_ratio_regression_percent:.2f}%",
        f"Max CV: {args.max_cv_percent:.2f}%",
        f"Overall status: {overall_status}",
        "",
        "| Run | Candidate | Gate Log | Pair Ratio Log | Status |",
        "| --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| {row['run']} | `{row['candidate']}` | `{row['gate_log']}` | "
            f"`{row['compare_log']}` | {row['status']} |"
        )
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"report={report}")
    print(f"status={overall_status}")
    return 0 if overall_status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
