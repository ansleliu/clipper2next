#!/usr/bin/env python3
import json
import math
import statistics
from collections.abc import Callable
from pathlib import Path

from benchmarks.tools.common.benchmark_command_args import benchmark_min_time_arg
from benchmarks.tools.common.release_gate_policy import EXTERNAL_CORE_BENCHMARK_GROUPS
from benchmarks.tools.common.release_gate_policy import coefficient_of_variation_percent


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def benchmark_base_name(record: dict) -> str | None:
    name = record.get("name")
    if not isinstance(name, str):
        return None
    for suffix in ("_mean", "_median", "_stddev", "_cv"):
        if name.endswith(suffix):
            return name.removesuffix(suffix)
    return name


def _cv_variance_roundoff_bound(repetitions: int, cv: float) -> float:
    """Bound second-moment rounding in dimensionless sample variance.

    The native producer subtracts mean(t)**2 from mean(t**2). Checking CV
    with a fixed relative tolerance amplifies that subtraction's error when
    variance is small. gamma(4*n+16) covers the positive sums/products,
    timer scaling and final division/sqrt; n/(n-1) is the sample correction.
    This bound checks aggregate consistency, not the release CV threshold.
    """
    unit_roundoff = math.ulp(1.0) / 2.0
    operations = 4 * repetitions + 16
    gamma = operations * unit_roundoff / (1.0 - operations * unit_roundoff)
    return gamma * (1.0 + cv * cv) * repetitions / (repetitions - 1)


def validate_benchmark_records(
    payload, expected_benchmarks, *, expected_case_count=None, expected_repetitions=None
):
    """Validate completion records at the shared benchmark evidence boundary."""
    if not isinstance(payload, dict) or not isinstance(payload.get("benchmarks"), list):
        raise ValueError("benchmark payload has no benchmark list")
    records = payload["benchmarks"]
    if any(not isinstance(record, dict) for record in records):
        raise ValueError("malformed benchmark record")
    errors = [
        (
            str(record.get("name", "<unnamed>")),
            str(record.get("error_message", "unspecified benchmark error")),
        )
        for record in records
        if record.get("error_occurred") or record.get("error_message")
    ]
    if errors:
        raise ValueError(
            "benchmark reported errors: "
            + "; ".join(f"{name}: {message}" for name, message in errors)
        )
    observed = {benchmark_base_name(record) for record in records}
    expected = set(expected_benchmarks)
    if observed != expected:
        raise ValueError(
            f"benchmark coverage mismatch: missing={sorted(expected - observed)} "
            f"unexpected={sorted(observed - expected, key=str)}"
        )
    if expected_case_count is not None:
        for name in expected:
            iterations = [
                record
                for record in records
                if benchmark_base_name(record) == name
                and record.get("run_type") == "iteration"
            ]
            if not iterations:
                raise ValueError(f"{name}: missing actual iteration records")
            repetitions = iterations[0].get("repetitions")
            if (
                type(repetitions) is not int
                or repetitions <= 0
                or (
                    expected_repetitions is not None
                    and repetitions != expected_repetitions
                )
            ):
                raise ValueError(f"{name}: invalid repetition count")
            indexes = [record.get("repetition_index") for record in iterations]
            if (
                any(type(index) is not int for index in indexes)
                or len(indexes) != repetitions
                or set(indexes) != set(range(repetitions))
            ):
                raise ValueError(f"{name}: incomplete/duplicate repetitions")
            for record in iterations:
                if (
                    record.get("repetitions") != repetitions
                    or type(record.get("iterations")) is not int
                    or record["iterations"] <= 0
                    or record.get("cases") != expected_case_count
                ):
                    raise ValueError(f"{name}: incomplete case execution")
    return records


def load_median_records(payload, expected_benchmarks=None, *, time_field,
                        expected_repetitions=None, iteration_counts="fixed"):
    """Admit timed evidence, then select the declared median (never a fallback).

    Capture-only mathematical producers use validate_benchmark_records instead.
    Their one-shot output is deliberately not performance evidence.
    Native benchmark aggregation requires equal iteration counts. Public timing
    producers normalize each independently calibrated repetition before aggregation.
    """
    if iteration_counts not in {"fixed", "adaptive"}:
        raise ValueError("unknown iteration count contract")
    if expected_benchmarks is None:
        if not isinstance(payload, dict) or not isinstance(payload.get("benchmarks"), list):
            raise ValueError("benchmark payload has no benchmark list")
        expected_benchmarks = {benchmark_base_name(r) for r in payload["benchmarks"]
                               if isinstance(r, dict)}
    records = validate_benchmark_records(payload, expected_benchmarks)
    if not records:
        raise ValueError("empty measurement")
    result = {}
    for name in expected_benchmarks:
        rows = [r for r in records if benchmark_base_name(r) == name]
        raw = [r for r in rows if r.get("run_type") == "iteration"]
        if not raw:
            raise ValueError(f"{name}: missing raw repetitions")
        repetitions = raw[0].get("repetitions")
        if (type(repetitions) is not int or repetitions < 7 or
                (expected_repetitions is not None and repetitions != expected_repetitions)):
            raise ValueError(f"{name}: expected at least seven complete repetitions")
        indexes = [r.get("repetition_index") for r in raw]
        if (any(type(i) is not int for i in indexes) or len(indexes) != repetitions or
                set(indexes) != set(range(repetitions))):
            raise ValueError(f"{name}: incomplete/duplicate repetitions")
        aggregates = {}
        unit = raw[0].get("time_unit")
        if unit not in {"ns", "us", "ms", "s"}:
            raise ValueError(f"{name}: invalid time unit")
        for row in rows:
            if row.get("run_name") != name or row.get("repetitions") != repetitions:
                raise ValueError(f"{name}: wrong run identity/repetition count")
            if row.get("time_unit") != unit:
                raise ValueError(f"{name}: inconsistent time units")
            value = row.get(time_field)
            if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                    not math.isfinite(value)):
                raise ValueError(f"{name}: invalid {time_field}")
            if row.get("run_type") == "iteration":
                if value <= 0 or type(row.get("iterations")) is not int or row["iterations"] <= 0:
                    raise ValueError(f"{name}: invalid raw time/iterations")
            elif row.get("run_type") == "aggregate":
                aggregate = row.get("aggregate_name")
                if (aggregate not in {"mean", "median", "stddev", "cv"} or
                        row.get("name") != f"{name}_{aggregate}" or aggregate in aggregates or
                        row.get("aggregate_unit") != ("percentage" if aggregate == "cv" else "time") or
                        value < 0 or (aggregate in {"mean", "median"} and value <= 0)):
                    raise ValueError(f"{name}: invalid/duplicate aggregate identity/value")
                aggregates[aggregate] = row
            else:
                raise ValueError(f"{name}: invalid run type")
        if not {"median", "cv"} <= aggregates.keys():
            raise ValueError(f"{name}: missing median/CV aggregate")
        if (iteration_counts == "fixed" and
                any(row["iterations"] != raw[0]["iterations"] for row in raw)):
            raise ValueError(f"{name}: inconsistent iteration counts")
        times = [r[time_field] for r in raw]
        median = statistics.median(times)
        cv = statistics.stdev(times) / statistics.mean(times)
        if not math.isclose(aggregates["median"][time_field], median,
                            rel_tol=1e-9, abs_tol=1e-12):
            raise ValueError(f"{name}: median disagrees with raw repetitions")
        reported_cv = aggregates["cv"][time_field]
        if abs(reported_cv * reported_cv - cv * cv) > _cv_variance_roundoff_bound(repetitions, cv):
            raise ValueError(f"{name}: cv disagrees with raw repetitions")
        result[name] = {"time": median, "source": f"{name}_median", "time_unit": unit,
                        "cv": max(cv * 100.0,
                                  coefficient_of_variation_percent(aggregates["cv"], time_field)),
                        "raw_times": times}
    return result


def collect_pairwise_external_core(
    *,
    benchmark_exe: Path,
    output_dir: Path,
    prefix: str,
    repetitions: int,
    min_time: float,
    min_warmup_time: float,
    benchmark_json: Path,
    benchmark_log: Path,
    benchmark_filter_arg: Callable[[str], str],
    run_command: Callable[[list[str], Path], int],
) -> tuple[int, str | None, Path]:
    group_dir = output_dir / f"{prefix}_external_benchmark_groups"
    group_dir.mkdir(parents=True, exist_ok=True)
    merged_benchmarks: list[dict] = []
    contexts: list[dict] = []
    group_evidence: list[dict] = []
    log_lines = [
        "measurement_isolation=pairwise-process",
        f"groups={len(EXTERNAL_CORE_BENCHMARK_GROUPS)}",
        "",
    ]

    for index, (group_name, expected_benchmarks) in enumerate(
        EXTERNAL_CORE_BENCHMARK_GROUPS,
        start=1,
    ):
        group_filter = "^(" + "|".join(expected_benchmarks) + ")$"
        stem = f"{index:02d}_{group_name}"
        group_json = group_dir / f"{stem}.json"
        group_log = group_dir / f"{stem}.log"
        command = [
            str(benchmark_exe),
            f"--benchmark_repetitions={repetitions}",
            "--benchmark_report_aggregates_only=false",
            "--benchmark_enable_random_interleaving=true",
            benchmark_min_time_arg(min_time),
            f"--benchmark_min_warmup_time={min_warmup_time}",
            benchmark_filter_arg(group_filter),
            f"--benchmark_out={group_json}",
            "--benchmark_out_format=json",
        ]
        status = run_command(command, group_log)
        log_lines.extend(
            [
                f"group={group_name}",
                "command=" + " ".join(command),
                f"status={status}",
                f"filter={group_filter}",
                f"json={group_json}",
                f"log={group_log}",
                "",
            ]
        )
        if status != 0:
            benchmark_log.write_text("\n".join(log_lines), encoding="utf-8")
            return (
                status,
                f"benchmark group {group_name} exited with {status}",
                group_dir,
            )

        try:
            payload = json.loads(group_json.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            benchmark_log.write_text("\n".join(log_lines), encoding="utf-8")
            return 1, f"invalid benchmark group {group_name}: {error}", group_dir
        try:
            load_median_records(payload, expected_benchmarks, time_field="real_time",
                                expected_repetitions=repetitions)
            records = payload["benchmarks"]
        except ValueError as error:
            reason = f"benchmark group {group_name}: {error}"
            benchmark_log.write_text(
                "\n".join([*log_lines, reason, ""]), encoding="utf-8"
            )
            return 1, reason, group_dir
        merged_benchmarks.extend(records)
        context = payload.get("context")
        if isinstance(context, dict):
            contexts.append(context)
        group_evidence.append(
            {
                "name": group_name,
                "benchmarks": list(expected_benchmarks),
                "filter": group_filter,
                "json": group_json.relative_to(output_dir).as_posix(),
                "log": group_log.relative_to(output_dir).as_posix(),
            }
        )

    merged_payload = {
        "context": contexts[0] if contexts else {},
        "benchmarks": merged_benchmarks,
        "clipper2next_measurement": {
            "isolation": "pairwise-process",
            "groups": group_evidence,
            "group_contexts": contexts,
        },
    }
    write_json(benchmark_json, merged_payload)
    benchmark_log.write_text("\n".join(log_lines), encoding="utf-8")
    return 0, None, group_dir
