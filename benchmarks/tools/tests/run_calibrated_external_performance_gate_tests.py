#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"
SCRIPT = TOOLS_DIR / "runners" / "run_calibrated_external_performance_gate.py"

from benchmarks.tools.common import release_gate_policy as policy

CORPUS_PROFILES = (
    "overlay",
    "rectclip",
    "rectclip-lines",
    "open-path-overlay",
    "offset",
    "triangulation",
    "bounds",
    "minkowski",
    "polytree",
    "clip-tree",
    "batch",
)


def make_fake_benchmark(
    directory: Path,
    cv_value: float,
    unprepared_cpu: float = 40.0,
    prepared_cpu: float = 20.0,
    next_cpu: float = 50.0,
    batch_cpu: float = 25.0,
    prepared_batch_cpu: float = 25.0,
    omit_benchmark: str | None = None,
) -> Path:
    records = []
    for benchmark in policy.EXTERNAL_CORE_BENCHMARK_NAMES:
        if benchmark == omit_benchmark:
            continue
        if benchmark == "BM_external_legacy/geometry_corpus" or "_legacy/" in benchmark:
            measured_time = 100.0
        elif benchmark == "BM_external_next/geometry_corpus":
            measured_time = next_cpu
        elif "_next_unprepared/" in benchmark:
            measured_time = unprepared_cpu
        elif benchmark == "BM_external_next_prepared/geometry_corpus":
            measured_time = prepared_cpu
        elif benchmark == "BM_external_next_prepared_batch/geometry_corpus":
            measured_time = prepared_batch_cpu
        elif benchmark in {
            "BM_external_next_batch/geometry_corpus",
            "BM_external_batch_next_batch/geometry_corpus",
        }:
            measured_time = batch_cpu
        else:
            measured_time = unprepared_cpu
        records.extend([
            {
                "name": f"{benchmark}_mean",
                "run_type": "aggregate",
                "cpu_time": measured_time,
                "real_time": measured_time,
            },
            {
                "name": f"{benchmark}_cv",
                "run_type": "aggregate",
                "cpu_time": cv_value,
                "real_time": cv_value,
                "aggregate_unit": "percentage",
            },
        ])

    fake = directory / "fake_benchmark.py"
    fake.write_text(
        textwrap.dedent(
            f"""
            import json
            import re
            import sys
            from pathlib import Path

            output = None
            benchmark_filter = ".*"
            for arg in sys.argv[1:]:
                if arg.startswith("--benchmark_out="):
                    output = Path(arg.split("=", 1)[1])
                elif arg.startswith("--benchmark_filter="):
                    benchmark_filter = arg.split("=", 1)[1].strip('"')
            if output is None:
                raise SystemExit(7)
            output.parent.mkdir(parents=True, exist_ok=True)
            records = {records!r}
            selected = []
            for record in records:
                base_name = record["name"]
                for suffix in ("_mean", "_median", "_stddev", "_cv"):
                    if base_name.endswith(suffix):
                        base_name = base_name[:-len(suffix)]
                        break
                if re.search(benchmark_filter, base_name):
                    selected.append(record)
            invocation_log = Path(__file__).with_name("benchmark_invocations.log")
            with invocation_log.open("a", encoding="utf-8") as handle:
                handle.write(benchmark_filter + "\\n")
            output.write_text(json.dumps({{"context": {{"fake": True}}, "benchmarks": selected}}), encoding="utf-8")
            """
        ).lstrip(),
        encoding="utf-8",
    )

    if os.name == "nt":
        wrapper = directory / "fake_benchmark.cmd"
        wrapper.write_text(f'@echo off\r\n"{sys.executable}" "{fake}" %*\r\n', encoding="utf-8")
    else:
        wrapper = directory / "fake_benchmark.sh"
        wrapper.write_text(f'#!/bin/sh\n"{sys.executable}" "{fake}" "$@"\n', encoding="utf-8")
        wrapper.chmod(0o755)
    (directory / "CMakeCache.txt").write_text(
        "CMAKE_BUILD_TYPE:STRING=Release\n"
        "CMAKE_CXX_COMPILER:FILEPATH=/test/compiler\n"
        "CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG\n",
        encoding="utf-8",
    )
    return wrapper


class CalibratedExternalPerformanceGateTests(unittest.TestCase):
    def run_gate(
        self,
        benchmark_exe: Path,
        output_dir: Path,
        *extra: str,
        calibrated: bool,
    ) -> subprocess.CompletedProcess:
        directional = "--allow-uncalibrated" in extra
        env = os.environ.copy()
        if calibrated:
            env["CLIPPER2NEXT_CALIBRATED_RUNNER"] = "1"
            env["CLIPPER2NEXT_RUNNER_ID"] = "unit-test-runner"
        else:
            env.pop("CLIPPER2NEXT_CALIBRATED_RUNNER", None)
            env.pop("CLIPPER2NEXT_RUNNER_ID", None)
        corpus = benchmark_exe.parent / "corpus" / "normalized" / "benchmark"
        corpus.mkdir(parents=True, exist_ok=True)
        for profile in CORPUS_PROFILES:
            (corpus / f"{profile}.jsonl").write_text(
                f'{{"profile":"{profile}"}}\n', encoding="utf-8"
            )
        env["CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT"] = str(
            benchmark_exe.parent / "corpus"
        )
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--benchmark-exe",
                str(benchmark_exe),
                "--output-dir",
                str(output_dir),
                "--prefix",
                "unit",
                "--repetitions",
                str(
                    1
                    if directional
                    else policy.CALIBRATED_EXTERNAL_REPETITIONS
                ),
                "--min-time",
                str(
                    0.01
                    if directional
                    else policy.CALIBRATED_EXTERNAL_MIN_TIME_SECONDS
                ),
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
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(root, 0.02)
            output_dir = root / "results"

            completed = self.run_gate(benchmark_exe, output_dir, calibrated=False)

            self.assertEqual(completed.returncode, 2, completed.stdout + completed.stderr)
            self.assertIn("CLIPPER2NEXT_CALIBRATED_RUNNER", completed.stderr)
            self.assertFalse((output_dir / "unit_external_benchmark.json").exists())
            summary = json.loads((output_dir / "unit_calibrated_external_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "NOISY")
            self.assertFalse(summary["calibrated_runner"])
            self.assertEqual(summary["evidence_mode"], "release")

    def test_cpu_affinity_is_recorded_in_successful_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(root, 0.02)
            output_dir = root / "results"

            completed = self.run_gate(
                benchmark_exe,
                output_dir,
                "--cpu-affinity",
                "0",
                calibrated=True,
            )

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            summary = json.loads(
                (output_dir / "unit_calibrated_external_summary.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(summary["cpu_affinity"], 0)
            metadata = json.loads(
                (output_dir / "unit_runner_metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                metadata["runner_placement"]["processor_number"], 0
            )

    def test_allowed_uncalibrated_run_uses_directional_variance_threshold(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(root, 0.10)
            output_dir = root / "results"

            completed = self.run_gate(
                benchmark_exe,
                output_dir,
                "--allow-uncalibrated",
                calibrated=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            summary = json.loads(
                (output_dir / "unit_calibrated_external_summary.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(summary["status"], "PASS")
            self.assertFalse(summary["calibrated_runner"])
            self.assertEqual(summary["evidence_mode"], "directional")
            self.assertEqual(
                summary["max_cv_percent"],
                policy.DIRECTIONAL_EXTERNAL_MAX_CV_PERCENT,
            )

    def test_release_mode_rejects_cli_policy_weakening_before_benchmark(self) -> None:
        weak_overrides = (
            ("--repetitions", "6"),
            ("--min-time", "0.19"),
            ("--max-cv-percent", "5.01"),
            ("--min-pair-speedup", "1.19"),
            ("--min-geomean-speedup", "1.19"),
            ("--skip-speedup-gate",),
        )
        for index, override in enumerate(weak_overrides):
            with self.subTest(override=override), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                benchmark_exe = make_fake_benchmark(root, 0.02)
                output_dir = root / f"results-{index}"

                completed = self.run_gate(
                    benchmark_exe,
                    output_dir,
                    *override,
                    calibrated=True,
                )

                self.assertEqual(
                    completed.returncode,
                    2,
                    completed.stdout + completed.stderr,
                )
                self.assertIn("weaken", completed.stderr)
                self.assertFalse(
                    (output_dir / "unit_external_benchmark.json").exists()
                )

    def test_calibrated_run_writes_benchmark_variance_speedup_and_summary_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(root, 0.02)
            output_dir = root / "results"

            completed = self.run_gate(benchmark_exe, output_dir, calibrated=True)

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertTrue((output_dir / "unit_external_benchmark.json").exists())
            benchmark_log = (output_dir / "unit_external_benchmark.log").read_text(encoding="utf-8")
            self.assertIn("--benchmark_min_time=0.5s", benchmark_log)
            self.assertIn("--benchmark_min_warmup_time=1.0", benchmark_log)
            self.assertIn("--benchmark_enable_random_interleaving=true", benchmark_log)
            self.assertIn("--benchmark_report_aggregates_only=false", benchmark_log)
            self.assertIn("--benchmark_filter=", benchmark_log)
            self.assertNotIn("scanbeam_global_schedule_equivalence_probe", benchmark_log)
            self.assertTrue((output_dir / "unit_external_variance_gate.md").exists())
            self.assertTrue((output_dir / "unit_external_variance_gate.json").exists())
            self.assertTrue((output_dir / "unit_external_legacy_speedup_gate.md").exists())
            self.assertTrue((output_dir / "unit_external_legacy_speedup_gate.json").exists())
            self.assertTrue((output_dir / "unit_runner_metadata.json").exists())
            summary = json.loads((output_dir / "unit_calibrated_external_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "PASS")
            self.assertTrue(summary["calibrated_runner"])
            self.assertEqual(summary["evidence_mode"], "release")
            self.assertEqual(
                summary["contract_sha256"],
                policy.RELEASE_EVIDENCE_CONTRACT_SHA256,
            )
            self.assertEqual(summary["runner_id"], "unit-test-runner")
            self.assertEqual(summary["max_cv_percent"], policy.CALIBRATED_EXTERNAL_MAX_CV_PERCENT)
            self.assertEqual(summary["min_pair_speedup"], policy.CALIBRATED_EXTERNAL_MIN_PAIR_SPEEDUP)
            self.assertEqual(
                summary["min_geomean_speedup"],
                policy.CALIBRATED_EXTERNAL_MIN_GEOMEAN_SPEEDUP,
            )
            self.assertEqual(summary["speedup_mode"], policy.EXTERNAL_CORE_SPEEDUP_MODE)
            self.assertEqual(summary["measurement_isolation"], "pairwise-process")
            self.assertEqual(summary["min_warmup_time_seconds"], 1.0)
            metadata = json.loads(
                (output_dir / "unit_runner_metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertTrue(metadata["identity_complete"])
            self.assertEqual(
                metadata["process_priority"],
                "high" if os.name == "nt" else "default",
            )
            self.assertRegex(
                summary["runner_metadata_identity"], r"^sha256:[0-9a-f]{64}$"
            )
            for key in (
                "benchmark_executable_identity",
                "candidate_source_identity",
                "compiler_identity",
                "corpus_identity",
                "protocol_identity",
                "runtime_library_identity",
                "git_repository_identity",
            ):
                self.assertIn(key, metadata)
            repository_identity = metadata["git_repository_identity"]
            self.assertRegex(
                repository_identity["head_commit"], r"^[0-9a-f]{40}$"
            )
            self.assertRegex(
                repository_identity["head_tree"], r"^[0-9a-f]{40}$"
            )
            self.assertRegex(
                repository_identity["canonical_diff_identity"],
                r"^sha256:[0-9a-f]{64}$",
            )
            self.assertEqual(
                summary["evidence_identity"], metadata["evidence_identity"]
            )
            benchmark_payload = json.loads(
                (output_dir / "unit_external_benchmark.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                benchmark_payload["context"]["evidence_identity"],
                metadata["evidence_identity"],
            )
            for group in benchmark_payload["clipper2next_measurement"]["groups"]:
                self.assertNotIn("\\", group["json"])
                self.assertNotIn("\\", group["log"])
            for raw in (output_dir / "unit_external_benchmark_groups").glob(
                "*.json"
            ):
                payload = json.loads(raw.read_text(encoding="utf-8"))
                self.assertEqual(
                    payload["context"]["evidence_identity"],
                    metadata["evidence_identity"],
                )
            for derived_name in (
                "unit_external_variance_gate.json",
                "unit_external_legacy_speedup_gate.json",
            ):
                derived = json.loads(
                    (output_dir / derived_name).read_text(encoding="utf-8")
                )
                self.assertEqual(
                    derived["evidence_identity"],
                    metadata["evidence_identity"],
                )
            invocations = (root / "benchmark_invocations.log").read_text(
                encoding="utf-8"
            ).splitlines()
            self.assertEqual(len(invocations), len(policy.EXTERNAL_CORE_BENCHMARK_GROUPS))
            summary_markdown = (output_dir / "unit_calibrated_external_summary.md").read_text(
                encoding="utf-8"
            )
            self.assertIn("Max CV threshold: **5.0%**", summary_markdown)
            self.assertIn("Speedup mode: `default-unprepared`", summary_markdown)
            speedup = json.loads((output_dir / "unit_external_legacy_speedup_gate.json").read_text(encoding="utf-8"))
            self.assertEqual(speedup["mode"], "default-unprepared")
            pairs = {(row["next"], row["pairing"]) for row in speedup["rows"]}
            self.assertEqual(len(pairs), 14)
            self.assertIn(("BM_external_next/geometry_corpus", "default"), pairs)
            self.assertIn(
                ("BM_external_rectclip_next_unprepared/geometry_corpus", "unprepared"),
                pairs,
            )

    def test_noisy_variance_returns_noisy_and_writes_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(root, 0.18)
            output_dir = root / "results"

            completed = self.run_gate(benchmark_exe, output_dir, calibrated=True)

            self.assertEqual(completed.returncode, 2, completed.stdout + completed.stderr)
            summary = json.loads((output_dir / "unit_calibrated_external_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "NOISY")
            variance = json.loads((output_dir / "unit_external_variance_gate.json").read_text(encoding="utf-8"))
            self.assertEqual(variance["status"], "NOISY")

    def test_speedup_failure_returns_fail_and_writes_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(root, 0.02, unprepared_cpu=90.0, prepared_cpu=20.0)
            output_dir = root / "results"

            completed = self.run_gate(benchmark_exe, output_dir, calibrated=True)

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            summary = json.loads((output_dir / "unit_calibrated_external_summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "FAIL")
            speedup = json.loads((output_dir / "unit_external_legacy_speedup_gate.json").read_text(encoding="utf-8"))
            self.assertEqual(speedup["status"], "FAIL")

    def test_missing_registered_core_benchmark_fails_during_collection(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            benchmark_exe = make_fake_benchmark(
                root,
                0.02,
                omit_benchmark="BM_external_overlay_union_next/geometry_corpus",
            )
            output_dir = root / "results"

            completed = self.run_gate(benchmark_exe, output_dir, calibrated=True)

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            summary = json.loads(
                (output_dir / "unit_calibrated_external_summary.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(summary["status"], "FAIL")
            self.assertIn("coverage mismatch", summary["reason"])
            self.assertFalse((output_dir / "unit_external_benchmark.json").exists())


if __name__ == "__main__":
    unittest.main()
