"""Complete raw measurements for reader/runner contract tests."""
import math
import statistics


def complete_measurements(rows):
    if any(not math.isfinite(r.get(f, 0)) or r.get(f, 0) < 0
           for r in rows for f in ("cpu_time", "real_time")):
        return rows
    names = {r["name"].removesuffix("_mean").removesuffix("_cv") for r in rows}
    result = []
    for name in sorted(names):
        selected = [r for r in rows if r["name"] in (name + "_mean", name + "_cv")]
        mean = next((r for r in selected if r["name"].endswith("_mean")), {})
        cv_row = next((r for r in selected if r["name"].endswith("_cv")), {})
        fields = {field: mean.get(field, 100.0) for field in ("real_time", "cpu_time")
                  if field in mean or field in cv_row}
        common = {"run_name": name, "repetitions": 7, "time_unit": "ns"}
        raw = []
        for i in range(7):
            values = {f: v * (1 + ((i - 3) / math.sqrt(28 / 6)
                                  if cv_row.get(f, 0) <= 0.3 else
                                  (-1 if i < 6 else 6) / math.sqrt(7)) * cv_row.get(f, 0))
                      for f, v in fields.items()}
            raw.append(dict(common, name=name, run_type="iteration", repetition_index=i,
                            iterations=10, **values))
        result.extend(raw)
        for aggregate in ("mean", "median", "stddev", "cv"):
            values = {}
            for f in fields:
                times = [r[f] for r in raw]
                values[f] = {"mean": statistics.mean(times), "median": statistics.median(times),
                             "stddev": statistics.stdev(times),
                             "cv": statistics.stdev(times) / statistics.mean(times)}[aggregate]
            result.append(dict(common, name=f"{name}_{aggregate}", run_type="aggregate",
                               aggregate_name=aggregate,
                               aggregate_unit="percentage" if aggregate == "cv" else "time", **values))
    return result
