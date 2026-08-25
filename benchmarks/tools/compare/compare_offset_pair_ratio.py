#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


def load_records(path: Path) -> dict[str, dict[str, float | str]]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    records: dict[str, dict[str, float | str]] = {}
    means: dict[str, float] = {}
    cvs: dict[str, float] = {}
    for benchmark in data.get("benchmarks", []):
        name = benchmark["name"]
        run_type = benchmark.get("run_type", "iteration")
        cpu_time = float(benchmark["cpu_time"])
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


def require_cpu(records: dict[str, dict[str, float | str]], name: str) -> float:
    record = records.get(name)
    if record is None or "cpu_time" not in record:
        raise KeyError(name)
    return float(record["cpu_time"])


def optional_cv(records: dict[str, dict[str, float | str]], name: str) -> float | None:
    record = records.get(name)
    if record is None or "cv" not in record:
        return None
    return float(record["cv"])


def next_to_legacy_ratio(records: dict[str, dict[str, float | str]]) -> tuple[float, float | None, float | None]:
    legacy_cpu = require_cpu(records, "BM_legacy_offset")
    next_cpu = require_cpu(records, "BM_next_offset")
    if legacy_cpu <= 0.0:
        raise ZeroDivisionError("BM_legacy_offset")
    return next_cpu / legacy_cpu, optional_cv(records, "BM_legacy_offset"), optional_cv(records, "BM_next_offset")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--max-ratio-regression-percent", type=float, required=True)
    parser.add_argument("--max-cv-percent", type=float, default=15.0)
    args = parser.parse_args()

    baseline = load_records(Path(args.baseline))
    candidate = load_records(Path(args.candidate))

    try:
        baseline_ratio, _, _ = next_to_legacy_ratio(baseline)
        candidate_ratio, legacy_cv, next_cv = next_to_legacy_ratio(candidate)
    except KeyError as error:
        print(f"missing benchmark: {error.args[0]}", file=sys.stderr)
        return 1
    except ZeroDivisionError as error:
        print(f"zero legacy baseline: {error.args[0]}", file=sys.stderr)
        return 1

    regression = ((candidate_ratio - baseline_ratio) / baseline_ratio) * 100.0 if baseline_ratio else 0.0
    cv_text = ""
    if legacy_cv is not None:
        cv_text += f" legacy_cv={legacy_cv:.2f}%"
    if next_cv is not None:
        cv_text += f" next_cv={next_cv:.2f}%"
    print(
        "BM_next_offset_vs_legacy: "
        f"baseline_ratio={baseline_ratio:.6f} candidate_ratio={candidate_ratio:.6f} "
        f"regression={regression:.2f}%{cv_text}"
    )

    cv_values = [cv for cv in (legacy_cv, next_cv) if cv is not None]
    if regression > args.max_ratio_regression_percent:
        return 1
    if any(cv > args.max_cv_percent for cv in cv_values):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
