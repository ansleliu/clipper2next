#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.benchmark_command_args import benchmark_min_time_arg


BENCHMARKS = {
    "clip": ("clip", "clipper2next_bench_legacy_vs_next_clip"),
    "offset": ("offset", "clipper2next_bench_legacy_vs_next_offset"),
    "rectclip": ("rectclip", "clipper2next_bench_legacy_vs_next_rectclip"),
    "batch": ("batch_parallel", "clipper2next_bench_batch_parallel"),
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def default_benchmark_dir() -> Path:
    return Path("build") / "msvc-oracle-benchmarks" / "bin"


def run_benchmark(
    executable: Path,
    output_json: Path,
    run_log: Path,
    repetitions: int,
    min_time: float,
) -> None:
    command = [
        str(executable),
        f"--benchmark_repetitions={repetitions}",
        "--benchmark_report_aggregates_only=true",
        "--benchmark_enable_random_interleaving=true",
        benchmark_min_time_arg(min_time),
        f"--benchmark_out={output_json}",
        "--benchmark_out_format=json",
    ]
    with run_log.open("w", encoding="utf-8") as handle:
        handle.write(" ".join(command))
        handle.write("\n\n")
        subprocess.run(
            command,
            cwd=repo_root(),
            stdout=handle,
            stderr=subprocess.STDOUT,
            check=True,
        )


def build_benchmark_target(build_preset: str, target_name: str) -> None:
    command = ["cmake", "--build", "--preset", build_preset, "--target", target_name]
    env = os.environ.copy()
    if os.name == "nt":
        vsdev = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat")
        if vsdev.exists():
            quoted = " ".join(command)
            subprocess.run(
                f'call "{vsdev}" -arch=x64 >nul && {quoted}',
                cwd=repo_root(),
                env=env,
                check=True,
                shell=True,
            )
            return
    subprocess.run(command, cwd=repo_root(), env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", default="phase17")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--min-time", type=float, default=0.2)
    parser.add_argument(
        "--suite",
        choices=("all", *BENCHMARKS.keys()),
        default="all",
        help="Benchmark suite to run; defaults to all suites.",
    )
    parser.add_argument(
        "--benchmark-dir",
        default=str(default_benchmark_dir()),
    )
    parser.add_argument("--build-preset", default="msvc-oracle-benchmarks")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = repo_root() / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    benchmark_dir = Path(args.benchmark_dir)
    if not benchmark_dir.is_absolute():
        benchmark_dir = repo_root() / benchmark_dir

    suites = BENCHMARKS.items() if args.suite == "all" else ((args.suite, BENCHMARKS[args.suite]),)
    executable_suffix = ".exe" if os.name == "nt" else ""
    for _, (stem, target_name) in suites:
        if not args.skip_build:
            build_benchmark_target(args.build_preset, target_name)

        executable = benchmark_dir / f"{target_name}{executable_suffix}"
        if not executable.exists():
            print(f"missing benchmark executable: {executable}", file=sys.stderr)
            return 1

        output_json = output_dir / f"{args.prefix}_{stem}.json"
        run_log = output_dir / f"{args.prefix}_{stem}_run.log"
        run_benchmark(executable, output_json, run_log, args.repetitions, args.min_time)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
