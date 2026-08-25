#!/usr/bin/env python3
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"

from benchmarks.tools.common import release_gate_policy as policy
from benchmarks.tools.common import external_core_measurement
from benchmarks.tools.runners import run_benchmark_gate
from benchmarks.tools.runners import run_release_performance_suite as suite


class ReleasePerformanceSuiteTests(unittest.TestCase):
    def test_oracle_environment_preflight_requires_calibration_and_complete_corpus(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            errors = suite.oracle_environment_errors()

        self.assertTrue(any("CLIPPER2NEXT_CALIBRATED_RUNNER" in error for error in errors))
        self.assertTrue(any("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT" in error for error in errors))

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            profile_dir = root / "normalized" / "benchmark"
            profile_dir.mkdir(parents=True)
            for profile in suite.ORACLE_CORPUS_REQUIRED_PROFILES:
                (profile_dir / f"{profile}.jsonl").write_text("{}\n", encoding="utf-8")
            with mock.patch.dict(
                os.environ,
                {
                    "CLIPPER2NEXT_CALIBRATED_RUNNER": "1",
                    "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT": str(root),
                },
                clear=True,
            ):
                errors = suite.oracle_environment_errors()

        self.assertEqual(errors, [])

    def test_benchmark_runner_always_randomly_interleaves_registered_pairs(self) -> None:
        captured: list[str] = []
        original_run = run_benchmark_gate.subprocess.run
        try:
            def capture(command: list[str], **_kwargs: object) -> object:
                captured.extend(command)
                return object()

            run_benchmark_gate.subprocess.run = capture
            with tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                run_benchmark_gate.run_benchmark(
                    root / "benchmark",
                    root / "result.json",
                    root / "run.log",
                    repetitions=2,
                    min_time=0.1,
                )
        finally:
            run_benchmark_gate.subprocess.run = original_run

        self.assertIn("--benchmark_enable_random_interleaving=true", captured)

    def test_external_measurement_rejects_google_benchmark_error_rows(self) -> None:
        def write_error_payload(command: list[str], _log_path: Path) -> int:
            output = next(
                Path(argument.removeprefix("--benchmark_out="))
                for argument in command
                if argument.startswith("--benchmark_out=")
            )
            expected = policy.EXTERNAL_CORE_BENCHMARK_GROUPS[0][1]
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(
                json.dumps(
                    {
                        "context": {},
                        "benchmarks": [
                            {
                                "name": name,
                                "run_type": "iteration",
                                "error_occurred": True,
                                "error_message": "missing corpus",
                            }
                            for name in expected
                        ],
                    }
                ),
                encoding="utf-8",
            )
            return 0

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            status, reason, _ = external_core_measurement.collect_pairwise_external_core(
                benchmark_exe=root / "benchmark",
                output_dir=root,
                prefix="candidate",
                repetitions=1,
                min_time=0.01,
                min_warmup_time=0.01,
                benchmark_json=root / "merged.json",
                benchmark_log=root / "merged.log",
                benchmark_filter_arg=lambda value: f"--benchmark_filter={value}",
                run_command=write_error_payload,
            )

        self.assertEqual(status, 1)
        self.assertIn("missing corpus", reason or "")

    def test_release_external_policy_constants_are_the_single_release_floor(self) -> None:
        self.assertEqual(policy.EXTERNAL_CORE_SPEEDUP_MODE, "default-unprepared")
        self.assertEqual(policy.CALIBRATED_EXTERNAL_REPETITIONS, 7)
        self.assertEqual(policy.CALIBRATED_EXTERNAL_MIN_TIME_SECONDS, 0.5)
        self.assertEqual(policy.CALIBRATED_EXTERNAL_MIN_WARMUP_TIME_SECONDS, 1.0)
        self.assertEqual(policy.CALIBRATED_EXTERNAL_MAX_CV_PERCENT, 5.0)
        self.assertEqual(policy.DIRECTIONAL_EXTERNAL_MAX_CV_PERCENT, 15.0)
        self.assertEqual(policy.CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP, 1.2)
        self.assertEqual(policy.CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP, 1.2)

    def test_release_external_core_filter_excludes_linear_bounds_scan_profile(self) -> None:
        self.assertNotIn("geometry_algorithms", policy.EXTERNAL_CORE_BENCHMARK_FILTER)
        self.assertNotIn("BM_external_bounds_", policy.EXTERNAL_CORE_BENCHMARK_FILTER)
        self.assertIn("open_path_overlay", policy.EXTERNAL_CORE_BENCHMARK_FILTER)
        self.assertIn("batch_scalar", policy.EXTERNAL_CORE_BENCHMARK_FILTER)
        self.assertIn("batch_next_batch", policy.EXTERNAL_CORE_BENCHMARK_FILTER)

    def test_external_core_groups_are_exact_disjoint_and_complete(self) -> None:
        grouped = [
            benchmark
            for _, benchmarks in policy.EXTERNAL_CORE_BENCHMARK_GROUPS
            for benchmark in benchmarks
        ]

        self.assertEqual(len(grouped), 32)
        self.assertEqual(len(grouped), len(set(grouped)))
        self.assertEqual(set(grouped), set(policy.EXTERNAL_CORE_BENCHMARK_NAMES))
        self.assertEqual(len(policy.EXTERNAL_CORE_SPEEDUP_PAIRS), 14)
        self.assertEqual(
            {
                benchmark
                for legacy, next_name in policy.EXTERNAL_CORE_SPEEDUP_PAIRS
                for benchmark in (legacy, next_name)
            },
            {
                benchmark
                for group_name, benchmarks in policy.EXTERNAL_CORE_BENCHMARK_GROUPS
                if group_name.endswith("_pair")
                for benchmark in benchmarks
            },
        )

    def test_product_suites_do_not_use_legacy_benchmark_targets(self) -> None:
        product_suites = suite.product_suites()

        self.assertIn("clip", product_suites)
        self.assertIn("offset", product_suites)
        self.assertIn("rectclip", product_suites)
        self.assertIn("batch", product_suites)
        self.assertIn("triangulation", product_suites)
        for benchmark in product_suites.values():
            joined = " ".join(
                [
                    benchmark.stem,
                    benchmark.target_name,
                    benchmark.executable_stem,
                ]
            )
            self.assertNotIn("legacy", joined)

    def test_find_benchmark_executable_uses_ninja_runtime_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            suffix = ".exe" if os.name == "nt" else ""
            executable = build_dir / "bin" / f"clipper2next_bench_product_clip{suffix}"
            executable.parent.mkdir(parents=True)
            executable.write_text("", encoding="utf-8")

            self.assertEqual(
                suite.find_benchmark_executable(build_dir, "clipper2next_bench_product_clip"),
                executable,
            )

    def test_product_baseline_names_use_product_prefix_and_suite_stem(self) -> None:
        benchmark = suite.product_suites()["batch"]

        self.assertEqual(
            suite.benchmark_json_path(Path("results"), "product_baseline", benchmark),
            Path("results") / "product_baseline_batch_parallel.json",
        )

    def test_release_runner_rejects_pgo_instrumented_build_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "CMakeCache.txt").write_text(
                "\n".join(
                    [
                        "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE:STRING=INSTRUMENT",
                        "CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=/LTCG:PGINSTRUMENT /GENPROFILE",
                        "CMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING=/LTCG:PGINSTRUMENT /GENPROFILE",
                    ]
                ),
                encoding="utf-8",
            )

            errors = suite.release_benchmark_build_errors(build_dir)

        self.assertEqual(
            errors,
            [
                "CMakeCache.txt selects target-scoped PGO instrumentation mode",
                "CMakeCache.txt contains PGO instrumentation flags in "
                "CMAKE_EXE_LINKER_FLAGS_RELEASE: /LTCG:PGINSTRUMENT /GENPROFILE",
                "CMakeCache.txt contains PGO instrumentation flags in "
                "CMAKE_SHARED_LINKER_FLAGS_RELEASE: /LTCG:PGINSTRUMENT /GENPROFILE",
            ],
        )

    def test_release_runner_accepts_pgo_optimized_build_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "CMakeCache.txt").write_text(
                "\n".join(
                    [
                        "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE:STRING=OPTIMIZE",
                        "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE:FILEPATH=C:/pgo/external.pgd",
                    ]
                ),
                encoding="utf-8",
            )

            errors = suite.release_benchmark_build_errors(build_dir)

        self.assertEqual(errors, [])

    def test_main_rejects_pgo_instrumented_build_before_running_benchmarks(self) -> None:
        original_argv = sys.argv
        original_run_product_suite = suite.run_product_suite
        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                build_dir = root / "build"
                output_dir = root / "results"
                build_dir.mkdir()
                (build_dir / "CMakeCache.txt").write_text(
                    "CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE:STRING=INSTRUMENT\n",
                    encoding="utf-8",
                )
                called = False

                def fail_if_called(*_args: object, **_kwargs: object) -> int:
                    nonlocal called
                    called = True
                    return 0

                suite.run_product_suite = fail_if_called
                sys.argv = [
                    "run_release_performance_suite.py",
                    "--build-dir",
                    str(build_dir),
                    "--output-dir",
                    str(output_dir),
                    "--prefix",
                    "candidate",
                ]
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    status = suite.main()
        finally:
            suite.run_product_suite = original_run_product_suite
            sys.argv = original_argv

        self.assertEqual(status, 2)
        self.assertFalse(called)
        self.assertIn("PGO instrumentation", stderr.getvalue())

    def test_product_compare_log_classifies_noise_band_without_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            compare_log = Path(temp_dir) / "compare.log"
            compare_log.write_text(
                "BM_product_offset_miter/64: baseline=100.000 "
                "candidate=116.000 regression=16.00% cv=0.03%\n",
                encoding="utf-8",
            )

            status, release_blocking, failures = suite.classify_product_compare_log(
                compare_log,
                max_regression_percent=10.0,
                noise_regression_percent=20.0,
            )

        self.assertEqual(status, "NOISY")
        self.assertFalse(release_blocking)
        self.assertEqual(failures, ["BM_product_offset_miter/64"])

    def test_product_compare_log_blocks_above_noise_band(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            compare_log = Path(temp_dir) / "compare.log"
            compare_log.write_text(
                "BM_product_rectclip_polygons/24: baseline=100.000 "
                "candidate=145.000 regression=45.00% cv=0.03%\n",
                encoding="utf-8",
            )

            status, release_blocking, failures = suite.classify_product_compare_log(
                compare_log,
                max_regression_percent=10.0,
                noise_regression_percent=20.0,
            )

        self.assertEqual(status, "FAIL")
        self.assertTrue(release_blocking)
        self.assertEqual(failures, ["BM_product_rectclip_polygons/24"])

    def test_external_core_speedup_gate_command_uses_legacy_default_unprepared_gate(self) -> None:
        command = suite.external_core_speedup_gate_command(
            Path("candidate.json"),
            Path("speedup.md"),
            Path("speedup.json"),
        )

        self.assertIn("external_legacy_speedup_gate.py", command[1])
        self.assertIn("--mode", command)
        self.assertEqual(command[command.index("--mode") + 1], "default-unprepared")

    def test_external_core_variance_gate_command_requires_calibrated_runner(self) -> None:
        command = suite.external_core_variance_gate_command(
            Path("candidate.json"),
            Path("variance.md"),
            Path("variance.json"),
            max_cv_percent=5.0,
        )

        self.assertIn("external_benchmark_variance_gate.py", command[1])
        self.assertIn("--require-calibrated-runner", command)
        self.assertIn("--max-cv-percent", command)
        self.assertEqual(command[command.index("--max-cv-percent") + 1], "5.0")

    def test_external_core_variance_gate_noisy_is_release_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            report_json = Path(temp_dir) / "variance.json"
            report_json.write_text(
                json.dumps(
                    {
                        "status": "NOISY",
                        "max_cv_percent": 15.0,
                        "rows": [
                            {
                                "name": "BM_external_next/geometry_corpus",
                                "cv_percent": 77.0,
                                "status": "NOISY",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            status, release_blocking, note = suite.classify_external_core_variance_gate(
                report_json,
                exit_code=2,
            )

        self.assertEqual(status, "NOISY")
        self.assertTrue(release_blocking)
        self.assertIn("max_cv=15.00%", note)
        self.assertIn("noisy_rows=1", note)

    def test_external_core_benchmark_command_uses_seconds_min_time_suffix(self) -> None:
        captured: dict = {}
        original_find = suite.find_benchmark_executable
        original_collect = suite.collect_pairwise_external_core
        try:
            suite.find_benchmark_executable = lambda build_dir, stem: Path("benchmark.exe")

            def capture(**kwargs):
                captured.update(kwargs)
                return 0, None, Path("groups")

            suite.collect_pairwise_external_core = capture
            status = suite.run_external_core_benchmark(
                Path("build"),
                Path("candidate.json"),
                Path("run.log"),
                repetitions=1,
                min_time=0.05,
            )
        finally:
            suite.find_benchmark_executable = original_find
            suite.collect_pairwise_external_core = original_collect

        self.assertEqual(status, 0)
        self.assertEqual(captured["min_time"], 0.05)
        self.assertEqual(captured["repetitions"], 1)

    def test_external_core_speedup_gate_failure_is_release_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            report_json = Path(temp_dir) / "speedup.json"
            report_json.write_text(
                json.dumps(
                    {
                        "status": "FAIL",
                        "mode": "default-unprepared",
                        "geomean_speedup": 2.9815,
                        "reason": "1 legacy pairs are below min pair speedup 1.200x",
                    }
                ),
                encoding="utf-8",
            )

            status, release_blocking, note = suite.classify_external_core_speedup_gate(
                report_json,
                exit_code=1,
            )

        self.assertEqual(status, "FAIL")
        self.assertTrue(release_blocking)
        self.assertIn("default-unprepared", note)
        self.assertIn("below min pair speedup", note)

    def test_oracle_mode_requires_explicit_baseline_prefix(self) -> None:
        original_argv = sys.argv
        try:
            sys.argv = [
                "run_release_performance_suite.py",
                "--mode",
                "oracle",
                "--build-dir",
                "build",
                "--output-dir",
                "benchmarks/results",
                "--prefix",
                "candidate",
            ]
            with contextlib.redirect_stderr(io.StringIO()):
                status = suite.main()
        finally:
            sys.argv = original_argv

        self.assertEqual(status, 2)

    def test_oracle_capture_baseline_runs_core_suites_with_explicit_prefix(self) -> None:
        captured: list[list[str]] = []
        original_argv = sys.argv
        original_run_command = suite.run_command
        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                build_dir = root / "build"
                output_dir = root / "results"
                build_dir.mkdir()

                def capture(command: list[str], log_path: Path) -> int:
                    captured.append(command)
                    log_path.parent.mkdir(parents=True, exist_ok=True)
                    log_path.write_text("stub\n", encoding="utf-8")
                    return 0

                suite.run_command = capture
                sys.argv = [
                    "run_release_performance_suite.py",
                    "--mode",
                    "oracle",
                    "--build-dir",
                    str(build_dir),
                    "--output-dir",
                    str(output_dir),
                    "--prefix",
                    "capture",
                    "--baseline-prefix",
                    "oracle_baseline",
                    "--capture-baseline",
                ]

                status = suite.main()
                report = output_dir / "capture_oracle_baseline_capture_report.md"
                report_exists = report.exists()
        finally:
            suite.run_command = original_run_command
            sys.argv = original_argv

        self.assertEqual(status, 0)
        self.assertEqual(
            [command[command.index("--suite") + 1] for command in captured],
            ["clip", "offset", "rectclip", "batch"],
        )
        self.assertTrue(report_exists)
        for command in captured:
            self.assertEqual(command[command.index("--prefix") + 1], "oracle_baseline")


if __name__ == "__main__":
    unittest.main()
