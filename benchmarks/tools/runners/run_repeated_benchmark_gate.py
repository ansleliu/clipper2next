#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
from pathlib import Path


BENCHMARK_STEMS = {
    "clip": "clip",
    "offset": "offset",
    "rectclip": "rectclip",
    "batch": "batch_parallel",
}


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


def failing_rows(compare_log: Path, max_regression_percent: float) -> list[str]:
    if not compare_log.exists():
        return []
    failures = []
    pattern = re.compile(r"^(?P<name>[^:]+): .* regression=(?P<regression>-?\d+(?:\.\d+)?)%")
    for line in compare_log.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        if float(match.group("regression")) > max_regression_percent:
            failures.append(match.group("name"))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=BENCHMARK_STEMS.keys(), required=True)
    parser.add_argument("--runs", type=int, required=True)
    parser.add_argument("--baseline-prefix", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--min-time", type=float, default=0.2)
    parser.add_argument("--max-regression-percent", type=float, default=3.0)
    parser.add_argument(
        "--benchmark-dir",
        default=str(default_benchmark_dir()),
    )
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if args.runs < 1:
      print("--runs must be >= 1", file=sys.stderr)
      return 2

    root = repo_root()
    output_dir = resolve_path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = BENCHMARK_STEMS[args.suite]
    baseline = output_dir / f"{args.baseline_prefix}_{stem}.json"
    if not baseline.exists():
        print(f"missing baseline JSON: {baseline}", file=sys.stderr)
        return 1

    tools_dir = next_root() / "benchmarks" / "tools"
    runner = tools_dir / "runners" / "run_benchmark_gate.py"
    comparator = tools_dir / "compare" / "compare_benchmark_json.py"
    rows = []
    infrastructure_failure = False

    for run_number in range(1, args.runs + 1):
        run_prefix = f"{args.prefix}_run{run_number}"
        gate_log = output_dir / f"{run_prefix}_{stem}_gate.log"
        candidate = output_dir / f"{run_prefix}_{stem}.json"
        compare_log = output_dir / f"{run_prefix}_{stem}_compare.log"

        gate_command = [
            sys.executable,
            str(runner),
            "--suite",
            args.suite,
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
        compare_status = None
        if gate_status == 0 and candidate.exists():
            compare_command = [
                sys.executable,
                str(comparator),
                "--baseline",
                str(baseline),
                "--candidate",
                str(candidate),
                "--max-regression-percent",
                str(args.max_regression_percent),
            ]
            compare_status = run_command(compare_command, compare_log)
        else:
            infrastructure_failure = True

        failures = failing_rows(compare_log, args.max_regression_percent) if compare_status is not None else []

        status = "PASS" if gate_status == 0 and compare_status == 0 else "FAIL"
        rows.append({
            "run": run_number,
            "candidate": candidate.name,
            "gate_log": gate_log.name,
            "compare_log": compare_log.name if compare_status is not None else "",
            "status": status,
            "failures": failures,
        })

    if all(row["status"] == "PASS" for row in rows):
        overall_status = "PASS"
    elif infrastructure_failure:
        overall_status = "FAIL"
    else:
        failure_sets = {tuple(row["failures"]) for row in rows if row["status"] != "PASS"}
        pass_count = sum(1 for row in rows if row["status"] == "PASS")
        overall_status = "NOISY" if pass_count or len(failure_sets) > 1 else "FAIL"

    report = output_dir / f"{args.prefix}_repeated_report.md"
    lines = [
        f"# Repeated Benchmark Gate: {args.suite}",
        "",
        f"Runs: {args.runs}",
        f"Baseline: `{baseline.name}`",
        f"Max regression: {args.max_regression_percent:.2f}%",
        f"Overall status: {overall_status}",
        "",
        "| Run | Candidate | Gate Log | Compare Log | Status | Failing Rows |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        failures = ", ".join(f"`{name}`" for name in row["failures"]) if row["failures"] else ""
        lines.append(
            f"| {row['run']} | `{row['candidate']}` | `{row['gate_log']}` | "
            f"`{row['compare_log']}` | {row['status']} | {failures} |"
        )
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"report={report}")
    print(f"status={overall_status}")
    if overall_status == "PASS":
        return 0
    if overall_status == "NOISY":
        return 2
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
