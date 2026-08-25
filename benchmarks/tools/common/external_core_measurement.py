#!/usr/bin/env python3
import json
from collections.abc import Callable
from pathlib import Path

from benchmarks.tools.common.benchmark_command_args import benchmark_min_time_arg
from benchmarks.tools.common.release_gate_policy import EXTERNAL_CORE_BENCHMARK_GROUPS


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def benchmark_base_name(record: dict) -> str | None:
    name = record.get("name")
    if not isinstance(name, str):
        return None
    for suffix in ("_mean", "_median", "_stddev", "_cv"):
        if name.endswith(suffix):
            return name.removesuffix(suffix)
    return name


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
        log_lines.extend([
            f"group={group_name}",
            "command=" + " ".join(command),
            f"status={status}",
            f"filter={group_filter}",
            f"json={group_json}",
            f"log={group_log}",
            "",
        ])
        if status != 0:
            benchmark_log.write_text("\n".join(log_lines), encoding="utf-8")
            return status, f"benchmark group {group_name} exited with {status}", group_dir

        try:
            payload = json.loads(group_json.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            benchmark_log.write_text("\n".join(log_lines), encoding="utf-8")
            return 1, f"invalid benchmark group {group_name}: {error}", group_dir
        records = payload.get("benchmarks")
        if not isinstance(records, list):
            benchmark_log.write_text("\n".join(log_lines), encoding="utf-8")
            return 1, f"benchmark group {group_name} has no benchmark list", group_dir
        errors = [
            (
                str(record.get("name", "<unnamed>")),
                str(record.get("error_message", "unspecified benchmark error")),
            )
            for record in records
            if isinstance(record, dict) and record.get("error_occurred") is True
        ]
        if errors:
            detail = "; ".join(f"{name}: {message}" for name, message in errors)
            reason = f"benchmark group {group_name} reported errors: {detail}"
            benchmark_log.write_text("\n".join([*log_lines, reason, ""]), encoding="utf-8")
            return 1, reason, group_dir
        observed = {
            base_name
            for record in records
            if isinstance(record, dict)
            and (base_name := benchmark_base_name(record)) is not None
        }
        expected = set(expected_benchmarks)
        if observed != expected:
            missing = sorted(expected - observed)
            unexpected = sorted(observed - expected)
            reason = (
                f"benchmark group {group_name} coverage mismatch: "
                f"missing={missing} unexpected={unexpected}"
            )
            benchmark_log.write_text("\n".join([*log_lines, reason, ""]), encoding="utf-8")
            return 1, reason, group_dir
        merged_benchmarks.extend(records)
        context = payload.get("context")
        if isinstance(context, dict):
            contexts.append(context)
        group_evidence.append({
            "name": group_name,
            "benchmarks": list(expected_benchmarks),
            "filter": group_filter,
            "json": str(group_json.relative_to(output_dir)),
            "log": str(group_log.relative_to(output_dir)),
        })

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
