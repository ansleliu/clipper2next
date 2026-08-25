#!/usr/bin/env python3
import argparse
import json
import os
import platform
import socket
import subprocess
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.release_gate_policy import (
    CALIBRATED_EXTERNAL_MAX_CV_PERCENT,
    CALIBRATED_EXTERNAL_MIN_TIME_SECONDS,
    CALIBRATED_EXTERNAL_REPETITIONS,
)


CALIBRATED_PERFORMANCE_CONTRACT_FIELDS = (
    "schema_version",
    "evidence_mode",
    "contract_sha256",
    "repetitions",
    "min_time_seconds",
    "max_cv_percent",
    "min_pair_speedup",
    "min_geomean_speedup",
    "speedup_mode",
    "variance_status",
    "speedup_status",
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def benchmark_tools_dir() -> Path:
    return repo_root() / "benchmarks" / "tools"


def benchmark_output_dir(build_dir: Path) -> Path:
    return build_dir / "benchmarks"


def runtime_output_dir(build_dir: Path) -> Path:
    return build_dir / "bin"


def resolve_path(path: str | Path) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate
    return repo_root() / candidate


def runner_metadata() -> dict:
    return {
        "calibrated_runner": os.environ.get("CLIPPER2NEXT_CALIBRATED_RUNNER") == "1",
        "runner_id": os.environ.get("CLIPPER2NEXT_RUNNER_ID", ""),
        "hostname": socket.gethostname(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "python": sys.version.split()[0],
    }


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_calibrated_performance_contract(path: Path) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read calibrated performance summary {path}: {error}") from error

    missing = [field for field in CALIBRATED_PERFORMANCE_CONTRACT_FIELDS if field not in payload]
    if missing:
        raise RuntimeError(
            "calibrated performance summary is missing contract fields: "
            + ", ".join(missing)
        )
    return {field: payload[field] for field in CALIBRATED_PERFORMANCE_CONTRACT_FIELDS}


def write_markdown(path: Path, payload: dict) -> None:
    lines = [
        "# MSVC PGO External Performance Gate",
        "",
        f"Status: **{payload['status']}**",
        f"Calibrated runner: **{str(payload['calibrated_runner']).lower()}**",
        f"Runner id: `{payload.get('runner_id', '')}`",
    ]
    if payload.get("reason"):
        lines.extend(["", f"Reason: {payload['reason']}"])
    lines.extend(["", "| Artifact | Path |", "| --- | --- |"])
    artifact_keys = ["plan_json", "runner_metadata"]
    artifact_keys.extend(sorted(key for key in payload if key.endswith("_log")))
    artifact_keys.append("calibrated_summary")
    for key in artifact_keys:
        value = payload.get(key)
        if value:
            lines.append(f"| {key} | `{value}` |")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_summary(output_dir: Path, prefix: str, payload: dict) -> None:
    json_path = output_dir / f"{prefix}_msvc_pgo_summary.json"
    markdown_path = output_dir / f"{prefix}_msvc_pgo_summary.md"
    payload = {**payload, "summary_json": json_path.name, "summary_markdown": markdown_path.name}
    write_json(json_path, payload)
    write_markdown(markdown_path, payload)


def run_command(command: list[str], log_path: Path) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
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


def common_configure_command(
    build_dir: Path,
    pgo_mode: str,
    pgo_database: Path,
) -> list[str]:
    cxx_flags = "/O2 /Ob3 /DNDEBUG /arch:AVX2"
    command = [
        "cmake",
        "-S",
        str(repo_root()),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DVCPKG_MANIFEST_FEATURES=tests;benchmarks;oracle",
        "-DCLIPPER2NEXT_TESTS=ON",
        "-DCLIPPER2NEXT_BENCHMARKS=ON",
        "-DCLIPPER2NEXT_FETCH_DEPS=OFF",
        "-DCLIPPER2NEXT_BUILD_ORACLE=ON",
        "-DCLIPPER2NEXT_CXX_STANDARD=23",
        f"-DCMAKE_CXX_FLAGS_RELEASE={cxx_flags}",
        f"-DCLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE={pgo_mode}",
        f"-DCLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE={pgo_database.as_posix()}",
    ]
    vcpkg_root = os.environ.get("VCPKG_ROOT")
    if vcpkg_root:
        command.append(f"-DCMAKE_TOOLCHAIN_FILE={Path(vcpkg_root) / 'scripts' / 'buildsystems' / 'vcpkg.cmake'}")
    return command


def build_command(
    build_dir: Path,
    *targets: str,
) -> list[str]:
    command = [
        "cmake",
        "--build",
        str(build_dir),
        "--target",
    ]
    command.extend(targets)
    return command


def benchmark_exe(build_dir: Path) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return runtime_output_dir(build_dir) / f"clipper2next_bench_external_corpus{suffix}"


def with_corpus_environment(command: list[str], corpus_root: Path) -> list[str]:
    return [
        "cmake",
        "-E",
        "env",
        f"CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT={corpus_root}",
        *command,
    ]


def with_training_environment(
    command: list[str], corpus_root: Path, profile_dir: Path
) -> list[str]:
    return [
        "cmake",
        "-E",
        "env",
        f"CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT={corpus_root}",
        f"VCPROFILE_PATH={profile_dir}",
        *command,
    ]


def correctness_command(
    build_dir: Path,
    junit_path: Path,
    corpus_root: Path,
) -> list[str]:
    return with_corpus_environment(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--output-on-failure",
            "--output-junit",
            str(junit_path),
            "-E",
            "^clipper2next_install_tree_smoke$",
        ],
        corpus_root,
    )


def skip_audit_command(ctest_log: Path) -> list[str]:
    return [
        sys.executable,
        str(repo_root() / "tools" / "checks" / "check_ctest_skips.py"),
        str(ctest_log),
        "--forbid-all",
    ]


def external_evidence_command(junit_path: Path) -> list[str]:
    return [
        sys.executable,
        str(repo_root() / "tools" / "checks" / "check_external_geometry_corpus_ctest_evidence.py"),
        str(junit_path),
    ]


def benchmark_contract_verification_command(executable: Path, corpus_root: Path) -> list[str]:
    return with_corpus_environment(
        [str(executable), "--clipper2next_verify_legacy"],
        corpus_root,
    )


def profile_manager_command(
    action: str,
    build_dir: Path,
    pgo_database: Path,
) -> list[str]:
    return [
        sys.executable,
        str(benchmark_tools_dir() / "runners" / "manage_msvc_pgo_profile.py"),
        action,
        "--profile-dir",
        str(benchmark_output_dir(build_dir)),
        "--database",
        str(pgo_database),
    ]


def training_command(executable: Path) -> list[str]:
    return [str(executable), "--clipper2next_train_pgo"]


def calibrated_gate_command(
  executable: Path,
  output_dir: Path,
  prefix: str,
  repetitions: int,
  min_time: float,
  max_cv_percent: float,
  allow_uncalibrated: bool,
  cpu_affinity: int | None) -> list[str]:
    command = [
        sys.executable,
        str(benchmark_tools_dir() / "runners" / "run_calibrated_external_performance_gate.py"),
        "--benchmark-exe",
        str(executable),
        "--output-dir",
        str(output_dir),
        "--prefix",
        f"{prefix}_pgo_optimized",
        "--repetitions",
        str(repetitions),
        "--min-time",
        str(min_time),
        "--max-cv-percent",
        str(max_cv_percent),
    ]
    if allow_uncalibrated:
        command.append("--allow-uncalibrated")
    if cpu_affinity is not None:
        command.extend(["--cpu-affinity", str(cpu_affinity)])
    return command


def command_plan(args: argparse.Namespace, output_dir: Path, build_dir: Path) -> list[dict]:
    executable = benchmark_exe(build_dir)
    corpus_root = resolve_path(args.corpus_root)
    pgo_database = output_dir / f"{args.prefix}_external_profile.pgd"
    canonical_junit = output_dir / f"{args.prefix}_canonical_correctness.xml"
    optimized_junit = output_dir / f"{args.prefix}_optimized_correctness.xml"
    canonical_ctest_log = output_dir / f"{args.prefix}_run_canonical_correctness.log"
    optimized_ctest_log = output_dir / f"{args.prefix}_run_optimized_correctness.log"
    return [
        {
            "name": "configure_canonical",
            "argv": common_configure_command(build_dir, "OFF", pgo_database),
        },
        {
            "name": "build_canonical_correctness",
            "argv": build_command(
                build_dir, "clipper2next_tests", "clipper2next_oracle_tests"
            ),
        },
        {
            "name": "run_canonical_correctness",
            "argv": correctness_command(
                build_dir, canonical_junit, corpus_root
            ),
        },
        {
            "name": "audit_canonical_skips",
            "argv": skip_audit_command(canonical_ctest_log),
        },
        {
            "name": "audit_canonical_external_evidence",
            "argv": external_evidence_command(canonical_junit),
        },
        {
            "name": "configure_instrumented",
            "argv": common_configure_command(build_dir, "INSTRUMENT", pgo_database),
        },
        {
            "name": "prepare_profile_counts",
            "argv": profile_manager_command(
                "prepare", build_dir, pgo_database
            ),
        },
        {
            "name": "build_instrumented",
            "argv": build_command(
                build_dir, "clipper2next_bench_external_corpus"
            ),
        },
        {
            "name": "train_profile",
            "argv": with_training_environment(
                training_command(executable),
                corpus_root,
                benchmark_output_dir(build_dir),
            ),
        },
        {
            "name": "merge_profile_counts",
            "argv": profile_manager_command(
                "merge", build_dir, pgo_database
            ),
        },
        {
            "name": "configure_optimized",
            "argv": common_configure_command(build_dir, "OPTIMIZE", pgo_database),
        },
        {
            "name": "build_optimized_benchmark",
            "argv": build_command(
                build_dir, "clipper2next_bench_external_corpus"
            ),
        },
        {
            "name": "build_optimized_product_tests",
            "argv": build_command(build_dir, "clipper2next_tests"),
        },
        {
            "name": "build_optimized_oracle_tests",
            "argv": build_command(build_dir, "clipper2next_oracle_tests"),
        },
        {
            "name": "run_optimized_correctness",
            "argv": correctness_command(
                build_dir, optimized_junit, corpus_root
            ),
        },
        {
            "name": "audit_optimized_skips",
            "argv": skip_audit_command(optimized_ctest_log),
        },
        {
            "name": "audit_optimized_external_evidence",
            "argv": external_evidence_command(optimized_junit),
        },
        {
            "name": "verify_benchmark_contracts",
            "argv": benchmark_contract_verification_command(executable, corpus_root),
        },
        {
            "name": "calibrated_external_gate",
            "argv": with_corpus_environment(
                calibrated_gate_command(
                    executable,
                    output_dir,
                    args.prefix,
                    args.repetitions,
                    args.min_time,
                    args.max_cv_percent,
                    args.allow_uncalibrated,
                    args.cpu_affinity,
                ),
                corpus_root,
            ),
        },
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", default="msvc_pgo")
    parser.add_argument(
        "--corpus-root",
        default=str(repo_root() / "tests" / "oracle" / "corpus" / "geometry"),
    )
    parser.add_argument("--repetitions", type=int, default=CALIBRATED_EXTERNAL_REPETITIONS)
    parser.add_argument("--min-time", type=float, default=CALIBRATED_EXTERNAL_MIN_TIME_SECONDS)
    parser.add_argument("--max-cv-percent", type=float, default=CALIBRATED_EXTERNAL_MAX_CV_PERCENT)
    parser.add_argument("--allow-uncalibrated", action="store_true")
    parser.add_argument("--cpu-affinity", type=int)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    output_dir = resolve_path(args.output_dir)
    build_dir = resolve_path(args.build_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    metadata = runner_metadata()
    metadata_path = output_dir / f"{args.prefix}_msvc_pgo_runner_metadata.json"
    write_json(metadata_path, metadata)
    plan = command_plan(args, output_dir, build_dir)
    plan_path = output_dir / f"{args.prefix}_msvc_pgo_plan.json"
    write_json(plan_path, {"commands": plan})

    common_summary = {
        "calibrated_runner": metadata["calibrated_runner"],
        "runner_id": metadata["runner_id"],
        "plan_json": plan_path.name,
        "runner_metadata": metadata_path.name,
        "cpu_affinity": args.cpu_affinity,
    }

    policy_errors = []
    if args.repetitions < CALIBRATED_EXTERNAL_REPETITIONS:
        policy_errors.append(
            f"repetitions {args.repetitions} is below {CALIBRATED_EXTERNAL_REPETITIONS}"
        )
    if args.min_time < CALIBRATED_EXTERNAL_MIN_TIME_SECONDS:
        policy_errors.append(
            f"min-time {args.min_time} is below {CALIBRATED_EXTERNAL_MIN_TIME_SECONDS}"
        )
    if args.max_cv_percent > CALIBRATED_EXTERNAL_MAX_CV_PERCENT:
        policy_errors.append(
            f"max-cv-percent {args.max_cv_percent} exceeds {CALIBRATED_EXTERNAL_MAX_CV_PERCENT}"
        )
    if policy_errors:
        reason = "; ".join(policy_errors)
        print(reason, file=sys.stderr)
        write_summary(
            output_dir,
            args.prefix,
            {**common_summary, "status": "FAIL", "reason": reason},
        )
        return 1

    if not metadata["calibrated_runner"] and not args.allow_uncalibrated:
        reason = "CLIPPER2NEXT_CALIBRATED_RUNNER=1 is required for MSVC PGO performance admission"
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

    if args.dry_run:
        print(f"plan={plan_path}")
        print("status=DRY_RUN")
        write_summary(
            output_dir,
            args.prefix,
            {
                **common_summary,
                "status": "DRY_RUN",
                "reason": "dry run; commands were planned but not executed",
            },
        )
        return 0

    summary = {**common_summary, "status": "PASS"}
    for step in plan:
        log_key = f"{step['name']}_log"
        if step["name"] == "calibrated_external_gate":
            status = run_command(step["argv"], output_dir / f"{args.prefix}_{step['name']}.log")
            summary["calibrated_summary"] = f"{args.prefix}_pgo_optimized_calibrated_external_summary.md"
            calibrated_summary_json = (
                output_dir / f"{args.prefix}_pgo_optimized_calibrated_external_summary.json"
            )
            summary["calibrated_summary_json"] = calibrated_summary_json.name
            contract_error = None
            try:
                summary.update(load_calibrated_performance_contract(calibrated_summary_json))
            except RuntimeError as error:
                contract_error = str(error)
            if status != 0 or contract_error:
                summary["status"] = "NOISY" if status == 2 else "FAIL"
                summary["reason"] = (
                    f"{step['name']} exited with {status}"
                    if status != 0
                    else contract_error
                )
                write_summary(output_dir, args.prefix, summary)
                print(f"status={summary['status']}")
                return status if status != 0 else 1
            continue

        log_path = output_dir / f"{args.prefix}_{step['name']}.log"
        status = run_command(step["argv"], log_path)
        summary[log_key] = log_path.name
        if status != 0:
            summary["status"] = "FAIL"
            summary["reason"] = f"{step['name']} exited with {status}"
            write_summary(output_dir, args.prefix, summary)
            print("status=FAIL")
            return 1

    write_summary(output_dir, args.prefix, summary)
    print(f"summary={output_dir / f'{args.prefix}_msvc_pgo_summary.md'}")
    print("status=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
