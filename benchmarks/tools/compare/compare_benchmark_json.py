#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.external_core_measurement import load_median_records


def load_cpu_times(path):
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return {name: dict(record, cpu_time=record["time"])
            for name, record in load_median_records(data, time_field="cpu_time").items()}


def find_candidate_record(candidate, name):
    return (name, candidate[name]) if name in candidate else (None, None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--max-regression-percent", type=float, required=True)
    parser.add_argument("--expected-rows", type=Path, help="JSON list of mandatory row names")
    args = parser.parse_args()

    try:
        baseline = load_cpu_times(args.baseline)
        candidate = load_cpu_times(args.candidate)
        expected = set(json.loads(args.expected_rows.read_text(encoding="utf-8"))) if args.expected_rows else set(baseline)
        if set(baseline) != expected or set(candidate) != expected:
            raise ValueError("baseline/candidate mandatory row coverage differs")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    failed = False

    for name, base_record in baseline.items():
        base_cpu = base_record["cpu_time"]
        candidate_name, candidate_record = find_candidate_record(candidate, name)
        if candidate_name is None:
            print(f"missing benchmark: {name}", file=sys.stderr)
            failed = True
            continue
        cand_cpu = candidate_record["cpu_time"]
        if candidate_record["time_unit"] != base_record["time_unit"]:
            print(f"{name}: baseline/candidate time units differ", file=sys.stderr)
            failed = True
            continue
        regression = ((cand_cpu - base_cpu) / base_cpu) * 100.0 if base_cpu else 0.0
        candidate_label = name if candidate_name == name else f"{name} ({candidate_name})"
        cv = candidate_record.get("cv")
        cv_text = f" baseline_cv={base_record['cv']:.2f}% candidate_cv={cv:.2f}%"
        print(
            f"{candidate_label}: baseline={base_cpu:.3f} "
            f"candidate={cand_cpu:.3f} regression={regression:.2f}%{cv_text}"
        )
        if regression > args.max_regression_percent or base_record["cv"] > 5 or cv > 5:
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
