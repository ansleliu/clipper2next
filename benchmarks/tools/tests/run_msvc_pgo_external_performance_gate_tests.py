#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"
SCRIPT = TOOLS_DIR / "runners" / "run_msvc_pgo_external_performance_gate.py"

from benchmarks.tools.runners import run_msvc_pgo_external_performance_gate as pgo_gate

RETIRED_CONFIGURE_ARGS = (
    "CLIPPER2_BUILD_LEGACY",
    "CLIPPER2_BUILD_LEGACY_TESTS",
    "CLIPPER2_BUILD_LEGACY_EXAMPLES",
    "CLIPPER2_BUILD_LEGACY_BENCHMARKS",
    "CLIPPER2_UTILS",
    "CLIPPER2_EXAMPLES",
    "CLIPPER2_TESTS",
    "USE_EXTERNAL_GTEST",
    "USE_EXTERNAL_GBENCHMARK",
    "CLIPPER2NEXT_BUILD=ON",
    "CLIPPER2NEXT_BUILD_COMPAT",
    "CLIPPER2NEXT_INSTALL_COMPAT",
    "CLIPPER2NEXT_COMPARE_WITH_LEGACY",
)


class MsvcPgoExternalPerformanceGateTests(unittest.TestCase):
    def test_benchmark_executable_uses_common_runtime_directory(self) -> None:
        suffix = ".exe" if os.name == "nt" else ""
        self.assertEqual(
            pgo_gate.benchmark_exe(Path("build")),
            Path("build") / "bin" / f"clipper2next_bench_external_corpus{suffix}",
        )

    def run_gate(
        self,
        output_dir: Path,
        *extra: str,
        calibrated: bool,
    ) -> subprocess.CompletedProcess:
        env = os.environ.copy()
        if calibrated:
            env["CLIPPER2NEXT_CALIBRATED_RUNNER"] = "1"
            env["CLIPPER2NEXT_RUNNER_ID"] = "pgo-unit-test"
        else:
            env.pop("CLIPPER2NEXT_CALIBRATED_RUNNER", None)
            env.pop("CLIPPER2NEXT_RUNNER_ID", None)
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--build-dir",
                str(output_dir / "build-pgo"),
                "--output-dir",
                str(output_dir),
                "--prefix",
                "unit",
                *extra,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            env=env,
        )

    def test_requires_calibrated_runner_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp)

            completed = self.run_gate(output_dir, calibrated=False)

            self.assertEqual(completed.returncode, 2, completed.stdout + completed.stderr)
            self.assertIn("CLIPPER2NEXT_CALIBRATED_RUNNER", completed.stderr)
            summary = json.loads((output_dir / "unit_msvc_pgo_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "NOISY")
            self.assertFalse(summary["calibrated_runner"])

    def test_loads_complete_calibrated_performance_contract_for_archive(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            summary_path = Path(tmp) / "calibrated-summary.json"
            expected = {
                "schema_version": 1,
                "evidence_mode": "release",
                "contract_sha256": "contract-hash",
                "repetitions": 7,
                "min_time_seconds": 0.5,
                "max_cv_percent": 5.0,
                "min_pair_speedup": 1.2,
                "min_geomean_speedup": 1.2,
                "speedup_mode": "default-unprepared",
                "variance_status": "PASS",
                "speedup_status": "PASS",
            }
            summary_path.write_text(json.dumps(expected), encoding="utf-8")

            actual = pgo_gate.load_calibrated_performance_contract(summary_path)

        self.assertEqual(expected, actual)

    def test_dry_run_writes_correctness_target_scoped_pgo_preflight_and_gate_plan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp)

            completed = self.run_gate(
                output_dir,
                "--dry-run",
                "--allow-uncalibrated",
                "--cpu-affinity",
                "10",
                calibrated=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            plan = json.loads((output_dir / "unit_msvc_pgo_plan.json").read_text(encoding="utf-8"))
            names = [command["name"] for command in plan["commands"]]
            commands = [" ".join(command["argv"]) for command in plan["commands"]]
            training_command = " ".join(
                next(command["argv"] for command in plan["commands"] if command["name"] == "train_profile")
            )
            calibrated_command = " ".join(
                next(
                    command["argv"]
                    for command in plan["commands"]
                    if command["name"] == "calibrated_external_gate"
                )
            )
            self.assertLess(names.index("run_canonical_correctness"), names.index("configure_instrumented"))
            self.assertLess(
                names.index("configure_instrumented"),
                names.index("prepare_profile_counts"),
            )
            self.assertLess(
                names.index("prepare_profile_counts"),
                names.index("build_instrumented"),
            )
            self.assertLess(names.index("train_profile"), names.index("merge_profile_counts"))
            self.assertLess(
                names.index("merge_profile_counts"),
                names.index("configure_optimized"),
            )
            self.assertLess(
                names.index("run_optimized_correctness"),
                names.index("verify_benchmark_contracts"),
            )
            self.assertLess(
                names.index("verify_benchmark_contracts"),
                names.index("calibrated_external_gate"),
            )
            optimized_build_names = [
                "build_optimized_benchmark",
                "build_optimized_product_tests",
                "build_optimized_oracle_tests",
            ]
            self.assertEqual(
                optimized_build_names,
                [name for name in names if name.startswith("build_optimized")],
            )
            for build_name, target in zip(
                optimized_build_names,
                (
                    "clipper2next_bench_external_corpus",
                    "clipper2next_tests",
                    "clipper2next_oracle_tests",
                ),
            ):
                command = next(
                    command["argv"] for command in plan["commands"] if command["name"] == build_name
                )
                self.assertEqual(target, command[-1])
            self.assertTrue(any("CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE=INSTRUMENT" in command for command in commands))
            self.assertTrue(any("CLIPPER2NEXT_MSVC_EXTERNAL_PGO_MODE=OPTIMIZE" in command for command in commands))
            self.assertTrue(any("CLIPPER2NEXT_MSVC_EXTERNAL_PGO_DATABASE=" in command for command in commands))
            profile_commands = [
                command
                for command in commands
                if "manage_msvc_pgo_profile.py" in command
            ]
            self.assertEqual(2, len(profile_commands))
            self.assertTrue(any(" prepare " in f" {command} " for command in profile_commands))
            self.assertTrue(any(" merge " in f" {command} " for command in profile_commands))
            self.assertFalse(any("CMAKE_EXE_LINKER_FLAGS_RELEASE" in command for command in commands))
            self.assertFalse(any("CMAKE_SHARED_LINKER_FLAGS_RELEASE" in command for command in commands))
            self.assertTrue(
                any("VCPKG_MANIFEST_FEATURES=tests;benchmarks;oracle" in command for command in commands)
            )
            self.assertTrue(any("CLIPPER2NEXT_FETCH_DEPS=OFF" in command for command in commands))
            configure_commands = [
                command["argv"]
                for command in plan["commands"]
                if command["name"].startswith("configure_")
            ]
            self.assertTrue(configure_commands)
            for command in configure_commands:
                generator_index = command.index("-G")
                self.assertEqual(command[generator_index + 1], "Ninja")
                self.assertIn("-DCMAKE_BUILD_TYPE=Release", command)
            for command in plan["commands"]:
                if command["name"].startswith("build_"):
                    self.assertNotIn("--config", command["argv"])
            for retired_arg in RETIRED_CONFIGURE_ARGS:
                self.assertFalse(
                    any(retired_arg in command for command in commands),
                    f"retired configure argument remains in PGO plan: {retired_arg}",
                )
            self.assertTrue(any("clipper2next_bench_external_corpus" in command for command in commands))
            self.assertTrue(any("clipper2next_tests" in command for command in commands))
            self.assertTrue(any("clipper2next_oracle_tests" in command for command in commands))
            self.assertEqual(2, sum("check_ctest_skips.py" in command for command in commands))
            self.assertEqual(
                2,
                sum("check_external_geometry_corpus_ctest_evidence.py" in command for command in commands),
            )
            self.assertTrue(training_command.endswith("--clipper2next_train_pgo"))
            self.assertIn("VCPROFILE_PATH=", training_command)
            self.assertNotIn("--benchmark_filter", training_command)
            self.assertNotIn("_legacy/", training_command)
            self.assertTrue(
                any("--clipper2next_verify_legacy" in command for command in commands)
            )
            self.assertTrue(any("run_calibrated_external_performance_gate.py" in command for command in commands))
            self.assertTrue(any("--repetitions 7" in command for command in commands))
            self.assertTrue(any("--min-time 0.5" in command for command in commands))
            self.assertIn("--cpu-affinity 10", calibrated_command)


if __name__ == "__main__":
    unittest.main()
