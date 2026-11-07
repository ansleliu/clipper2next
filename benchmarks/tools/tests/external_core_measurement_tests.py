import copy
import statistics
import unittest

from benchmarks.tools.common import external_core_measurement


def load_median_records(*args, **kwargs):
    owner = getattr(external_core_measurement, "load_median_records", None)
    assert callable(owner), "shared measurement owner must admit complete median records"
    return owner(*args, **kwargs)


def measured_payload(name="BM_case", value=100.0, repetitions=7):
    common = {"run_name": name, "repetitions": repetitions, "time_unit": "ns"}
    rows = [dict(common, name=name, run_type="iteration", repetition_index=i,
                 iterations=10, cpu_time=value, real_time=value) for i in range(repetitions)]
    for aggregate, measured in (("mean", value), ("median", value), ("stddev", 0.0), ("cv", 0.0)):
        rows.append(dict(common, name=f"{name}_{aggregate}", run_type="aggregate",
                         aggregate_name=aggregate, aggregate_unit="percentage" if aggregate == "cv" else "time",
                         cpu_time=measured, real_time=measured))
    return {"benchmarks": rows}


def low_variance_native_payload():
    """Recorded benchmark 1.9.5 output; raw times and aggregates are unchanged."""
    cpu = [3214.2829587146484, 3214.8521262575928, 3214.0071793623765,
           3214.777360786492, 3214.9004692873914, 3214.186812656985,
           3214.8459451640897]
    real = [3214.3699779642698, 3214.9194466084573, 3214.087464695073,
            3214.8549534064696, 3214.9688418534365, 3214.2709621029467,
            3214.9407856844346]
    payload = measured_payload()
    for row, cpu_time, real_time in zip(payload["benchmarks"][:7], cpu, real):
        row.update(cpu_time=cpu_time, real_time=real_time, iterations=2177770)
    aggregates = [(3214.5504074613677, 3214.630347473584),
                  (3214.7773607864915, 3214.8549534064696),
                  (0.37666400951442236, 0.37341851300526896),
                  (0.00011717470929687048, 0.00011616219367142572)]
    for row, (cpu_time, real_time) in zip(payload["benchmarks"][7:], aggregates):
        row.update(cpu_time=cpu_time, real_time=real_time)
    return payload


class MeasurementAdmissionTests(unittest.TestCase):
    def test_accepts_native_low_variance_second_moment_rounding(self):
        payload = low_variance_native_payload()
        for field in ("cpu_time", "real_time"):
            with self.subTest(field=field):
                record = load_median_records(payload, ["BM_case"], time_field=field)["BM_case"]
                times = [row[field] for row in payload["benchmarks"][:7]]
                self.assertEqual(record["time"], statistics.median(times))
                self.assertGreaterEqual(record["cv"],
                                        statistics.stdev(times) / statistics.mean(times) * 100.0)
                self.assertGreaterEqual(record["cv"], payload["benchmarks"][-1][field] * 100.0)

    def test_rejects_cv_changes_outside_native_rounding(self):
        payload = low_variance_native_payload()
        payload["benchmarks"][-1]["cpu_time"] += 1e-8
        with self.assertRaises(ValueError):
            load_median_records(payload, ["BM_case"], time_field="cpu_time")

    def test_requires_identical_iteration_counts_across_repetitions(self):
        payload = measured_payload()
        payload["benchmarks"][0]["iterations"] += 1
        with self.assertRaises(ValueError):
            load_median_records(payload, ["BM_case"], time_field="cpu_time")

    def test_public_adaptive_iterations_preserve_normalized_median_and_cv(self):
        payload = measured_payload()
        for index, row in enumerate(payload["benchmarks"][:7]):
            row["iterations"] = 100 + index
        record = load_median_records(
            payload, ["BM_case"], time_field="real_time", iteration_counts="adaptive"
        )["BM_case"]
        self.assertEqual(record["time"], 100.0)
        self.assertEqual(record["cv"], 0.0)
        self.assertEqual(record["raw_times"], [100.0] * 7)

    def test_adaptive_iterations_do_not_relax_raw_or_aggregate_admission(self):
        for mutation in (
            lambda p: p["benchmarks"][0].update(iterations=0),
            lambda p: p["benchmarks"][8].update(real_time=101.0),
            lambda p: p["benchmarks"][-1].update(real_time=0.01),
        ):
            with self.subTest(mutation=mutation):
                payload = measured_payload()
                for index, row in enumerate(payload["benchmarks"][:7]):
                    row["iterations"] = 100 + index
                mutation(payload)
                with self.assertRaises(ValueError):
                    load_median_records(
                        payload, ["BM_case"], time_field="real_time",
                        iteration_counts="adaptive",
                    )

    def test_unknown_iteration_count_contract_is_rejected(self):
        with self.assertRaises(ValueError):
            load_median_records(
                measured_payload(), ["BM_case"], time_field="real_time",
                iteration_counts="automatic",
            )

    def test_selects_median_independent_of_record_order(self):
        payload = measured_payload()
        payload["benchmarks"].reverse()
        record = load_median_records(payload, ["BM_case"], time_field="cpu_time")["BM_case"]
        self.assertEqual(record["time"], 100.0)
        self.assertEqual(record["source"], "BM_case_median")
        self.assertEqual(record["cv"], 0.0)

    def test_rejects_incomplete_or_corrupt_measurements(self):
        def absent_median(p): p["benchmarks"] = [r for r in p["benchmarks"] if r.get("aggregate_name") != "median"]
        mutations = {
            "missing raw": lambda p: p["benchmarks"].pop(0),
            "duplicate rep": lambda p: p["benchmarks"][0].update(repetition_index=1),
            "wrong rep count": lambda p: p["benchmarks"][0].update(repetitions=8),
            "missing median": absent_median,
            "duplicate median": lambda p: p["benchmarks"].append(copy.deepcopy(p["benchmarks"][8])),
            "error": lambda p: p["benchmarks"][0].update(error_occurred=True),
            "zero": lambda p: p["benchmarks"][0].update(cpu_time=0),
            "negative": lambda p: p["benchmarks"][0].update(cpu_time=-1),
            "nan": lambda p: p["benchmarks"][0].update(cpu_time=float("nan")),
            "infinity": lambda p: p["benchmarks"][0].update(cpu_time=float("inf")),
            "bool": lambda p: p["benchmarks"][0].update(cpu_time=True),
            "wrong unit": lambda p: p["benchmarks"][0].update(time_unit="ms"),
            "wrong aggregate": lambda p: p["benchmarks"][8].update(aggregate_name="mean"),
            "wrong run identity": lambda p: p["benchmarks"][8].update(run_name="BM_foreign"),
            "forged median": lambda p: p["benchmarks"][8].update(cpu_time=1),
            "missing cv": lambda p: p["benchmarks"].pop(),
            "forged cv": lambda p: p["benchmarks"][-1].update(cpu_time=0.04),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label):
                payload = measured_payload()
                mutate(payload)
                with self.assertRaises(ValueError):
                    load_median_records(payload, ["BM_case"], time_field="cpu_time")

    def test_rejects_missing_unexpected_rows_and_less_than_seven_reps(self):
        for payload, expected in ((measured_payload(), ["BM_missing"]),
                                  (measured_payload(repetitions=6), ["BM_case"])):
            with self.assertRaises(ValueError):
                load_median_records(payload, expected, time_field="real_time")


if __name__ == "__main__":
    unittest.main()
