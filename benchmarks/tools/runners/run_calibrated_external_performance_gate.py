#!/usr/bin/env python3
import argparse
import ctypes
import json
import os
import platform
import socket
import subprocess
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.external_core_measurement import collect_pairwise_external_core
from benchmarks.tools.common.evidence_identity import (
    collect_evidence_identity,
    sha256_file,
)
from benchmarks.tools.common.release_gate_policy import (
    CALIBRATED_EXTERNAL_MAX_CV_PERCENT,
    CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP,
    CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP,
    CALIBRATED_EXTERNAL_MIN_TIME_SECONDS,
    CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS,
    CALIBRATED_EXTERNAL_REPETITIONS,
    DIRECTIONAL_EXTERNAL_MAX_CV_PERCENT,
    EXTERNAL_CORE_SPEEDUP_MODE,
    RELEASE_EVIDENCE_CONTRACT_SHA256,
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def benchmark_tools_dir() -> Path:
    return repo_root() / "benchmarks" / "tools"


def resolve_path(path: str | Path) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate
    return repo_root() / candidate


class _ProcessorNumber(ctypes.Structure):
    _fields_ = (
        ("group", ctypes.c_ushort),
        ("number", ctypes.c_ubyte),
        ("reserved", ctypes.c_ubyte),
    )


def constrain_runner_process(cpu_affinity: int | None) -> dict | None:
    if cpu_affinity is None:
        return None
    if os.name != "nt":
        if not hasattr(os, "sched_getaffinity"):
            raise OSError("CPU affinity is not supported on this platform")
        if cpu_affinity not in os.sched_getaffinity(0):
            raise OSError(f"CPU {cpu_affinity} is outside the runner CPU set")
        os.sched_setaffinity(0, {cpu_affinity})
        actual = sorted(os.sched_getaffinity(0))
        if actual != [cpu_affinity]:
            raise OSError(f"runner affinity is {actual}, expected [{cpu_affinity}]")
        return {
            "affinity_mask": hex(1 << cpu_affinity),
            "processor_group": 0,
            "processor_number": cpu_affinity,
        }

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetCurrentProcess.argtypes = []
    kernel32.GetCurrentProcess.restype = ctypes.c_void_p
    kernel32.GetActiveProcessorGroupCount.argtypes = []
    kernel32.GetActiveProcessorGroupCount.restype = ctypes.c_ushort
    kernel32.GetActiveProcessorCount.argtypes = [ctypes.c_ushort]
    kernel32.GetActiveProcessorCount.restype = ctypes.c_uint
    kernel32.GetProcessGroupAffinity.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_ushort),
        ctypes.POINTER(ctypes.c_ushort),
    ]
    kernel32.GetProcessGroupAffinity.restype = ctypes.c_int
    kernel32.GetCurrentProcessorNumberEx.argtypes = [
        ctypes.POINTER(_ProcessorNumber)
    ]
    kernel32.GetCurrentProcessorNumberEx.restype = None
    kernel32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    kernel32.SetProcessAffinityMask.restype = ctypes.c_int
    kernel32.Sleep.argtypes = [ctypes.c_uint]
    kernel32.Sleep.restype = None

    process = kernel32.GetCurrentProcess()
    affinity_mask = 1 << cpu_affinity
    if not kernel32.SetProcessAffinityMask(process, affinity_mask):
        raise OSError(ctypes.get_last_error(), "SetProcessAffinityMask failed")
    group_capacity = int(kernel32.GetActiveProcessorGroupCount())
    group_count = ctypes.c_ushort(group_capacity)
    groups = (ctypes.c_ushort * group_capacity)()
    if not kernel32.GetProcessGroupAffinity(
        process, ctypes.byref(group_count), groups
    ):
        raise OSError(ctypes.get_last_error(), "GetProcessGroupAffinity failed")
    if group_count.value != 1:
        raise OSError("calibrated runner must belong to one processor group")
    current = _ProcessorNumber()
    for _ in range(64):
        kernel32.GetCurrentProcessorNumberEx(ctypes.byref(current))
        if int(current.group) == int(groups[0]) and int(current.number) == cpu_affinity:
            break
        kernel32.Sleep(0)
    else:
        raise OSError("runner did not migrate to the requested processor")
    return {
        "affinity_mask": hex(affinity_mask),
        "processor_group": int(current.group),
        "processor_number": int(current.number),
        "processor_group_active_counts": [
            int(kernel32.GetActiveProcessorCount(group))
            for group in range(group_capacity)
        ],
    }


def runner_metadata(
    benchmark_executable: Path,
    runner_placement: dict | None,
    output_dir: Path,
) -> dict:
    return {
        "calibrated_runner": os.environ.get("CLIPPER2NEXT_CALIBRATED_RUNNER") == "1",
        "runner_id": os.environ.get("CLIPPER2NEXT_RUNNER_ID", ""),
        "hostname": socket.gethostname(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "python": sys.version.split()[0],
        "process_priority": "high" if os.name == "nt" else "default",
        "runner_placement": runner_placement,
        **collect_evidence_identity(
            repo_root(), benchmark_executable, output_dir / "artifacts"
        ),
    }


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_summary_markdown(path: Path, payload: dict) -> None:
    lines = [
        "# Calibrated External Performance Gate",
        "",
        f"Status: **{payload['status']}**",
        f"Evidence mode: **{payload.get('evidence_mode', '')}**",
        f"Calibrated runner: **{str(payload['calibrated_runner']).lower()}**",
        f"Runner id: `{payload.get('runner_id', '')}`",
        f"Contract SHA-256: `{payload.get('contract_sha256', '')}`",
        f"Repetitions: **{payload.get('repetitions', '')}**",
        f"Benchmark min time: **{payload.get('min_time_seconds', '')}s**",
        f"Benchmark min warmup time: **{payload.get('min_warmup_time_seconds', '')}s**",
        f"Max CV threshold: **{payload.get('max_cv_percent', '')}%**",
        f"Speedup mode: `{payload.get('speedup_mode', '')}`",
        f"Measurement isolation: `{payload.get('measurement_isolation', '')}`",
        f"Min pair speedup: **{payload.get('min_pair_speedup', '')}x**",
        f"Min geomean speedup: **{payload.get('min_geomean_speedup', '')}x**",
    ]
    if payload.get("reason"):
        lines.extend(["", f"Reason: {payload['reason']}"])
    lines.extend([
        "",
        "| Artifact | Path |",
        "| --- | --- |",
    ])
    for key in [
        "benchmark_json",
        "benchmark_log",
        "benchmark_group_dir",
        "variance_markdown",
        "variance_json",
        "variance_log",
        "speedup_markdown",
        "speedup_json",
        "speedup_log",
        "runner_metadata",
    ]:
        value = payload.get(key)
        if value:
            lines.append(f"| {key} | `{value}` |")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_summary(output_dir: Path, prefix: str, payload: dict) -> None:
    json_path = output_dir / f"{prefix}_calibrated_external_summary.json"
    markdown_path = output_dir / f"{prefix}_calibrated_external_summary.md"
    payload = {**payload, "summary_json": json_path.name, "summary_markdown": markdown_path.name}
    write_json(json_path, payload)
    write_summary_markdown(markdown_path, payload)


def set_process_affinity(process: subprocess.Popen, cpu_affinity: int) -> None:
    if os.name == "nt":
        import ctypes

        high_priority_class = 0x00000080
        if not ctypes.windll.kernel32.SetPriorityClass(
            int(process._handle), high_priority_class
        ):
            raise OSError(
                ctypes.get_last_error(), "SetPriorityClass failed"
            )
        affinity_mask = 1 << cpu_affinity
        if not ctypes.windll.kernel32.SetProcessAffinityMask(
            int(process._handle), affinity_mask
        ):
            raise OSError(ctypes.get_last_error(), "SetProcessAffinityMask failed")
        return
    if not hasattr(os, "sched_setaffinity"):
        raise OSError("CPU affinity is not supported on this platform")
    os.sched_setaffinity(process.pid, {cpu_affinity})


def run_command(
    command: list[str],
    log_path: Path,
    *,
    cpu_affinity: int | None = None,
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as handle:
        handle.write(" ".join(command))
        handle.write("\n\n")
        process = subprocess.Popen(
            command,
            cwd=repo_root(),
            stdout=handle,
            stderr=subprocess.STDOUT,
        )
        if cpu_affinity is not None:
            try:
                set_process_affinity(process, cpu_affinity)
            except OSError as error:
                handle.write(f"failed to set CPU affinity: {error}\n")
                process.terminate()
                process.wait()
                return 1
        return process.wait()


def benchmark_filter_arg(benchmark_exe: Path, benchmark_filter: str) -> str:
    if os.name == "nt" and benchmark_exe.suffix.lower() in {".bat", ".cmd"}:
        return f'--benchmark_filter="{benchmark_filter}"'
    return f"--benchmark_filter={benchmark_filter}"


def run_pairwise_benchmarks(
    *,
    benchmark_exe: Path,
    output_dir: Path,
    prefix: str,
    repetitions: int,
    min_time: float,
    cpu_affinity: int | None,
    benchmark_json: Path,
    benchmark_log: Path,
) -> tuple[int, str | None, Path]:
    return collect_pairwise_external_core(
        benchmark_exe=benchmark_exe,
        output_dir=output_dir,
        prefix=prefix,
        repetitions=repetitions,
        min_time=min_time,
        min_warmup_time=CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS,
        benchmark_json=benchmark_json,
        benchmark_log=benchmark_log,
        benchmark_filter_arg=lambda value: benchmark_filter_arg(benchmark_exe, value),
        run_command=lambda command, log: run_command(
            command,
            log,
            cpu_affinity=cpu_affinity,
        ),
    )


def load_status(path: Path) -> str:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "FAIL"
    status = payload.get("status")
    return status if isinstance(status, str) else "FAIL"


def bind_evidence_identity(path: Path, identity: str) -> None:
    payload = json.loads(path.read_text(encoding="utf-8"))
    context = payload.setdefault("context", {})
    if not isinstance(context, dict):
        raise ValueError(f"benchmark context is not an object: {path}")
    context["evidence_identity"] = identity
    if "executable" in context:
        context["executable"] = Path(str(context["executable"])).name
    write_json(path, payload)


def redact_local_paths(path: Path, replacements: list[tuple[str, str]]) -> None:
    if not path.is_file() or path.suffix not in {".json", ".log", ".md"}:
        return
    def redact(value):
        if isinstance(value, dict):
            return {key: redact(item) for key, item in value.items()}
        if isinstance(value, list):
            return [redact(item) for item in value]
        if not isinstance(value, str):
            return value
        for source, replacement in ordered:
            if source:
                value = value.replace(source, replacement)
                value = value.replace(source.replace("\\", "/"), replacement)
        return value

    ordered = sorted(replacements, key=lambda value: len(value[0]), reverse=True)
    if path.suffix == ".json":
        payload = json.loads(path.read_text(encoding="utf-8"))
        write_json(path, redact(payload))
        return
    content = path.read_text(encoding="utf-8", errors="replace")
    for source, replacement in ordered:
        if source:
            content = content.replace(source, replacement)
            content = content.replace(source.replace("\\", "/"), replacement)
    path.write_text(content, encoding="utf-8", newline="\n")


def bind_derived_identity(path: Path, identity: str) -> None:
    payload = json.loads(path.read_text(encoding="utf-8"))
    payload["evidence_identity"] = identity
    write_json(path, payload)


def release_policy_weakening_reasons(args: argparse.Namespace) -> list[str]:
    reasons: list[str] = []
    if args.repetitions < CALIBRATED_EXTERNAL_REPETITIONS:
        reasons.append(
            f"repetitions {args.repetitions} is below "
            f"{CALIBRATED_EXTERNAL_REPETITIONS}"
        )
    if args.min_time < CALIBRATED_EXTERNAL_MIN_TIME_SECONDS:
        reasons.append(
            f"min time {args.min_time} is below "
            f"{CALIBRATED_EXTERNAL_MIN_TIME_SECONDS}"
        )
    if args.max_cv_percent > CALIBRATED_EXTERNAL_MAX_CV_PERCENT:
        reasons.append(
            f"max CV {args.max_cv_percent} exceeds "
            f"{CALIBRATED_EXTERNAL_MAX_CV_PERCENT}"
        )
    if args.min_pair_speedup < CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP:
        reasons.append(
            f"pair speedup {args.min_pair_speedup} is below "
            f"{CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP}"
        )
    if args.min_geomean_speedup < CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP:
        reasons.append(
            f"geomean speedup {args.min_geomean_speedup} is below "
            f"{CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP}"
        )
    if args.skip_speedup_gate:
        reasons.append("speedup gate cannot be skipped")
    return reasons


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark-exe", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", default="calibrated_external")
    parser.add_argument("--repetitions", type=int, default=CALIBRATED_EXTERNAL_REPETITIONS)
    parser.add_argument("--min-time", type=float, default=CALIBRATED_EXTERNAL_MIN_TIME_SECONDS)
    parser.add_argument("--max-cv-percent", type=float)
    parser.add_argument("--min-pair-speedup", type=float, default=CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP)
    parser.add_argument(
        "--min-geomean-speedup",
        type=float,
        default=CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP,
    )
    parser.add_argument("--skip-speedup-gate", action="store_true")
    parser.add_argument("--allow-uncalibrated", action="store_true")
    parser.add_argument("--cpu-affinity", type=int)
    args = parser.parse_args()
    if args.cpu_affinity is not None and not 0 <= args.cpu_affinity < (os.cpu_count() or 1):
        parser.error(
            f"--cpu-affinity must be between 0 and {(os.cpu_count() or 1) - 1}"
        )
    if args.max_cv_percent is None:
        args.max_cv_percent = (
            DIRECTIONAL_EXTERNAL_MAX_CV_PERCENT
            if args.allow_uncalibrated
            else CALIBRATED_EXTERNAL_MAX_CV_PERCENT
        )
    evidence_mode = "directional" if args.allow_uncalibrated else "release"

    output_dir = resolve_path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    benchmark_exe = resolve_path(args.benchmark_exe)
    runner_placement = constrain_runner_process(args.cpu_affinity)
    metadata = runner_metadata(benchmark_exe, runner_placement, output_dir)
    metadata_path = output_dir / f"{args.prefix}_runner_metadata.json"
    write_json(metadata_path, metadata)
    metadata_identity = sha256_file(metadata_path)

    common_summary = {
        "schema_version": 1,
        "evidence_mode": evidence_mode,
        "contract_sha256": RELEASE_EVIDENCE_CONTRACT_SHA256,
        "calibrated_runner": metadata["calibrated_runner"],
        "runner_id": metadata["runner_id"],
        "runner_metadata": metadata_path.name,
        "runner_metadata_identity": metadata_identity,
        "evidence_identity": metadata["evidence_identity"],
        "release_identity": metadata["release_identity"],
        "repetitions": args.repetitions,
        "min_time_seconds": args.min_time,
        "min_warmup_time_seconds": CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS,
        "max_cv_percent": args.max_cv_percent,
        "min_pair_speedup": args.min_pair_speedup,
        "min_geomean_speedup": args.min_geomean_speedup,
        "speedup_mode": EXTERNAL_CORE_SPEEDUP_MODE,
        "measurement_isolation": "pairwise-process",
        "cpu_affinity": args.cpu_affinity,
    }

    if evidence_mode == "release":
        weakening_reasons = release_policy_weakening_reasons(args)
        if weakening_reasons:
            reason = "release policy weakening is forbidden: " + "; ".join(
                weakening_reasons
            )
            print(reason, file=sys.stderr)
            print("status=FAIL")
            write_summary(
                output_dir,
                args.prefix,
                {
                    **common_summary,
                    "status": "FAIL",
                    "reason": reason,
                },
            )
            return 2
        if not metadata["identity_complete"]:
            reason = "release evidence identity is incomplete"
            print(reason, file=sys.stderr)
            print("status=FAIL")
            write_summary(
                output_dir,
                args.prefix,
                {**common_summary, "status": "FAIL", "reason": reason},
            )
            return 2

    if not metadata["calibrated_runner"] and not args.allow_uncalibrated:
        reason = "CLIPPER2NEXT_CALIBRATED_RUNNER=1 is required for calibrated performance admission"
        print(reason, file=sys.stderr)
        print("status=NOISY")
        write_summary(
            output_dir,
            args.prefix,
            {
                **common_summary,
                "status": "NOISY",
                "reason": reason,
            },
        )
        return 2

    benchmark_json = output_dir / f"{args.prefix}_external_benchmark.json"
    benchmark_log = output_dir / f"{args.prefix}_external_benchmark.log"
    benchmark_status, benchmark_reason, benchmark_group_dir = run_pairwise_benchmarks(
        benchmark_exe=benchmark_exe,
        output_dir=output_dir,
        prefix=args.prefix,
        repetitions=args.repetitions,
        min_time=args.min_time,
        cpu_affinity=args.cpu_affinity,
        benchmark_json=benchmark_json,
        benchmark_log=benchmark_log,
    )
    if benchmark_status != 0:
        write_summary(
            output_dir,
            args.prefix,
            {
                **common_summary,
                "status": "FAIL",
                "reason": benchmark_reason or f"benchmark exited with {benchmark_status}",
                "benchmark_json": benchmark_json.name,
                "benchmark_log": benchmark_log.name,
                "benchmark_group_dir": benchmark_group_dir.name,
            },
        )
        print("status=FAIL")
        return 1

    for raw_path in benchmark_group_dir.glob("*.json"):
        bind_evidence_identity(
            raw_path, metadata["evidence_identity"])
    benchmark_payload = json.loads(benchmark_json.read_text(encoding="utf-8"))
    group_contexts = [
        json.loads(path.read_text(encoding="utf-8")).get("context", {})
        for path in sorted(benchmark_group_dir.glob("*.json"))
    ]
    benchmark_payload["context"] = group_contexts[0] if group_contexts else {}
    measurement = benchmark_payload.get("clipper2next_measurement")
    if not isinstance(measurement, dict):
        raise ValueError("combined benchmark measurement metadata is missing")
    measurement["group_contexts"] = group_contexts
    write_json(benchmark_json, benchmark_payload)

    variance_md = output_dir / f"{args.prefix}_external_variance_gate.md"
    variance_json = output_dir / f"{args.prefix}_external_variance_gate.json"
    variance_log = output_dir / f"{args.prefix}_external_variance_gate.log"
    variance_gate = benchmark_tools_dir() / "gates" / "external_benchmark_variance_gate.py"
    variance_command = [
        sys.executable,
        str(variance_gate),
        "--candidate",
        str(benchmark_json),
        "--max-cv-percent",
        str(args.max_cv_percent),
        "--require-core-benchmarks",
        "--output-md",
        str(variance_md),
        "--output-json",
        str(variance_json),
    ]
    if not args.allow_uncalibrated:
        variance_command.append("--require-calibrated-runner")
    variance_status_code = run_command(variance_command, variance_log)
    if variance_json.is_file():
        bind_derived_identity(
            variance_json, metadata["evidence_identity"])
    variance_status = load_status(variance_json)
    if variance_status_code not in (0, 2):
        variance_status = "FAIL"

    speedup_md = output_dir / f"{args.prefix}_external_legacy_speedup_gate.md"
    speedup_json = output_dir / f"{args.prefix}_external_legacy_speedup_gate.json"
    speedup_log = output_dir / f"{args.prefix}_external_legacy_speedup_gate.log"
    speedup_status = "PASS"
    speedup_artifacts: dict[str, str] = {}
    if not args.skip_speedup_gate:
        speedup_gate = benchmark_tools_dir() / "gates" / "external_legacy_speedup_gate.py"
        speedup_command = [
            sys.executable,
            str(speedup_gate),
            "--candidate",
            str(benchmark_json),
            "--mode",
            EXTERNAL_CORE_SPEEDUP_MODE,
            "--min-pair-speedup",
            str(args.min_pair_speedup),
            "--min-geomean-speedup",
            str(args.min_geomean_speedup),
            "--require-core-pairs",
            "--output-md",
            str(speedup_md),
            "--output-json",
            str(speedup_json),
        ]
        speedup_status_code = run_command(speedup_command, speedup_log)
        if speedup_json.is_file():
            bind_derived_identity(
                speedup_json, metadata["evidence_identity"])
        speedup_status = load_status(speedup_json)
        if speedup_status_code != 0:
            speedup_status = "FAIL"
        speedup_artifacts = {
            "speedup_markdown": speedup_md.name,
            "speedup_json": speedup_json.name,
            "speedup_log": speedup_log.name,
        }

    if variance_status == "FAIL" or speedup_status == "FAIL":
        status = "FAIL"
    elif variance_status == "NOISY" or speedup_status == "NOISY":
        status = "NOISY"
    else:
        status = "PASS"

    write_summary(
        output_dir,
        args.prefix,
        {
            **common_summary,
            "status": status,
            "variance_status": variance_status,
            "speedup_status": speedup_status,
            "benchmark_json": benchmark_json.name,
            "benchmark_log": benchmark_log.name,
            "benchmark_group_dir": benchmark_group_dir.name,
            "variance_markdown": variance_md.name,
            "variance_json": variance_json.name,
            "variance_log": variance_log.name,
            **speedup_artifacts,
        },
    )

    replacements = [
        (str(benchmark_exe.resolve()), "${BENCHMARK}"),
        (str(output_dir.resolve()), "${EVIDENCE}"),
        (str(repo_root().resolve()), "${REPOSITORY}"),
        (str(Path(sys.executable).resolve()), "${PYTHON}"),
    ]
    for artifact in output_dir.rglob("*"):
        redact_local_paths(artifact, replacements)

    print(f"summary={output_dir / f'{args.prefix}_calibrated_external_summary.md'}")
    print(f"status={status}")
    if status == "PASS":
        return 0
    if status == "NOISY":
        return 2
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
