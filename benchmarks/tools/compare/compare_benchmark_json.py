#!/usr/bin/env python3
import argparse
import json
import sys


def load_cpu_times(path):
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    records = {}
    means = {}
    cvs = {}
    for benchmark in data.get("benchmarks", []):
        name = benchmark["name"]
        cpu_time = float(benchmark["cpu_time"])
        run_type = benchmark.get("run_type", "iteration")
        if run_type == "iteration":
            records[name] = {"cpu_time": cpu_time, "source": name}
            continue
        if run_type != "aggregate":
            continue
        if name.endswith("_median"):
            records[name.removesuffix("_median")] = {"cpu_time": cpu_time, "source": name}
        elif name.endswith("_mean"):
            means[name.removesuffix("_mean")] = cpu_time
        elif name.endswith("_cv"):
            cvs[name.removesuffix("_cv")] = cpu_time

    for name, cpu_time in means.items():
        records.setdefault(name, {"cpu_time": cpu_time, "source": f"{name}_mean"})
    for name, cv in cvs.items():
        records.setdefault(name, {})["cv"] = cv
    return records


def find_candidate_record(candidate, name):
    for candidate_name in (f"{name}_median", f"{name}_mean", name):
        if candidate_name in candidate:
            record = candidate[candidate_name]
            if "cpu_time" in record:
                return candidate_name, record
    return None, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--max-regression-percent", type=float, required=True)
    args = parser.parse_args()

    baseline = load_cpu_times(args.baseline)
    candidate = load_cpu_times(args.candidate)
    failed = False

    for name, base_record in baseline.items():
        base_cpu = base_record["cpu_time"]
        candidate_name, candidate_record = find_candidate_record(candidate, name)
        if candidate_name is None:
            print(f"missing benchmark: {name}", file=sys.stderr)
            failed = True
            continue
        cand_cpu = candidate_record["cpu_time"]
        regression = ((cand_cpu - base_cpu) / base_cpu) * 100.0 if base_cpu else 0.0
        candidate_label = name if candidate_name == name else f"{name} ({candidate_name})"
        cv = candidate_record.get("cv")
        cv_text = f" cv={cv:.2f}%" if cv is not None else ""
        print(
            f"{candidate_label}: baseline={base_cpu:.3f} "
            f"candidate={cand_cpu:.3f} regression={regression:.2f}%{cv_text}"
        )
        if regression > args.max_regression_percent:
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
