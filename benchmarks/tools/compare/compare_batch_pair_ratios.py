#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


PAIR_ARGS = ("1", "8", "64")


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
            base_name = name.removesuffix("_median")
            records[base_name] = {"cpu_time": cpu_time, "source": name}
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


def ratio(records: dict[str, dict[str, float | str]], arg: str) -> tuple[float, float | None, float | None]:
    scalar_name = f"BM_next_batch_scalar/{arg}"
    public_name = f"BM_next_batch_public_clip/{arg}"
    scalar_cpu = require_cpu(records, scalar_name)
    public_cpu = require_cpu(records, public_name)
    if scalar_cpu <= 0.0:
        raise ZeroDivisionError(scalar_name)
    return public_cpu / scalar_cpu, optional_cv(records, scalar_name), optional_cv(records, public_name)


def public_clip_time(records: dict[str, dict[str, float | str]], arg: str) -> float:
    return require_cpu(records, f"BM_next_batch_public_clip/{arg}")


def is_release_blocking_ratio_regression(
    ratio_regression_percent: float,
    public_time_regression_percent: float,
    max_regression_percent: float,
) -> bool:
    return (
        ratio_regression_percent > max_regression_percent
        and public_time_regression_percent > max_regression_percent
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--max-ratio-regression-percent", type=float, required=True)
    parser.add_argument("--max-cv-percent", type=float, default=15.0)
    parser.add_argument("--args", nargs="+", default=list(PAIR_ARGS))
    args = parser.parse_args()

    baseline = load_records(Path(args.baseline))
    candidate = load_records(Path(args.candidate))
    failed = False

    for arg in args.args:
        try:
            baseline_ratio, _, _ = ratio(baseline, arg)
            candidate_ratio, scalar_cv, public_cv = ratio(candidate, arg)
            baseline_public_time = public_clip_time(baseline, arg)
            candidate_public_time = public_clip_time(candidate, arg)
        except KeyError as error:
            print(f"missing benchmark: {error.args[0]}", file=sys.stderr)
            failed = True
            continue
        except ZeroDivisionError as error:
            print(f"zero scalar baseline: {error.args[0]}", file=sys.stderr)
            failed = True
            continue

        regression = ((candidate_ratio - baseline_ratio) / baseline_ratio) * 100.0 if baseline_ratio else 0.0
        public_regression = (
            ((candidate_public_time - baseline_public_time) / baseline_public_time) * 100.0
            if baseline_public_time
            else 0.0
        )
        cv_values = [cv for cv in (scalar_cv, public_cv) if cv is not None]
        cv_text = ""
        if scalar_cv is not None:
            cv_text += f" scalar_cv={scalar_cv:.2f}%"
        if public_cv is not None:
            cv_text += f" public_cv={public_cv:.2f}%"
        print(
            f"BM_next_batch_public_clip/{arg}_vs_scalar: "
            f"baseline_ratio={baseline_ratio:.6f} candidate_ratio={candidate_ratio:.6f} "
            f"regression={regression:.2f}% "
            f"public_time_regression={public_regression:.2f}%{cv_text}"
        )
        if is_release_blocking_ratio_regression(
            regression,
            public_regression,
            args.max_ratio_regression_percent,
        ):
            failed = True
        if any(cv > args.max_cv_percent for cv in cv_values):
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
