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
SCRIPT = TOOLS_DIR / "gates" / "external_benchmark_variance_gate.py"


def write_benchmark(path: Path, benchmarks: list[dict]) -> None:
    path.write_text(json.dumps({"benchmarks": benchmarks}), encoding="utf-8")


class ExternalBenchmarkVarianceGateTests(unittest.TestCase):
    def run_gate(self, path: Path, *extra: str, calibrated: bool = True) -> subprocess.CompletedProcess:
        env = os.environ.copy()
        if calibrated:
            env["CLIPPER2NEXT_CALIBRATED_RUNNER"] = "1"
        else:
            env.pop("CLIPPER2NEXT_CALIBRATED_RUNNER", None)
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--candidate", str(path), *extra],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            env=env,
        )

    def test_passes_when_all_external_cv_values_are_below_threshold(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            write_benchmark(candidate, [
                {"name": "BM_external_next/tiger_mean", "cpu_time": 10.0},
                {
                    "name": "BM_external_next/tiger_cv",
                    "cpu_time": 0.025,
                    "aggregate_unit": "percentage",
                },
                {
                    "name": "BM_external_next_batch/tiger_cv",
                    "cpu_time": 0.04,
                    "aggregate_unit": "percentage",
                },
            ])

            completed = self.run_gate(candidate, "--max-cv-percent", "5.0")

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertIn("status=PASS", completed.stdout)

    def test_reports_noisy_when_any_external_cv_exceeds_threshold(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_next/tiger_cv",
                    "cpu_time": 0.02,
                    "aggregate_unit": "percentage",
                },
                {
                    "name": "BM_external_next_batch/tiger_cv",
                    "cpu_time": 0.18,
                    "aggregate_unit": "percentage",
                },
            ])

            completed = self.run_gate(candidate, "--max-cv-percent", "5.0")

            self.assertEqual(completed.returncode, 2, completed.stdout + completed.stderr)
            self.assertIn("status=NOISY", completed.stdout)
            self.assertIn("BM_external_next_batch/tiger", completed.stdout)

    def test_writes_archivable_markdown_and_json_reports(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            markdown = Path(tmp) / "variance.md"
            report_json = Path(tmp) / "variance.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_next/tiger_cv",
                    "cpu_time": 0.02,
                    "aggregate_unit": "percentage",
                },
                {
                    "name": "BM_external_next_batch/tiger_cv",
                    "cpu_time": 0.18,
                    "aggregate_unit": "percentage",
                },
            ])

            completed = self.run_gate(
                candidate,
                "--max-cv-percent",
                "5.0",
                "--output-md",
                str(markdown),
                "--output-json",
                str(report_json),
            )

            self.assertEqual(completed.returncode, 2, completed.stdout + completed.stderr)
            self.assertIn("Status: **NOISY**", markdown.read_text(encoding="utf-8"))
            self.assertIn("| BM_external_next_batch/tiger | 18.00% | NOISY |", markdown.read_text(encoding="utf-8"))

            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "NOISY")
            self.assertEqual(payload["max_cv_percent"], 5.0)
            self.assertEqual(payload["rows"][0]["name"], "BM_external_next/tiger")
            self.assertEqual(payload["rows"][1]["status"], "NOISY")

    def test_requires_calibrated_runner_when_requested(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_next/tiger_cv",
                    "cpu_time": 0.02,
                    "aggregate_unit": "percentage",
                },
            ])

            completed = self.run_gate(candidate, "--require-calibrated-runner", calibrated=False)

            self.assertEqual(completed.returncode, 2, completed.stdout + completed.stderr)
            self.assertIn("CLIPPER2NEXT_CALIBRATED_RUNNER", completed.stderr)

    def test_required_core_benchmarks_fail_when_cv_rows_are_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "variance.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_next/geometry_corpus_cv",
                    "cpu_time": 0.02,
                    "aggregate_unit": "percentage",
                },
            ])

            completed = self.run_gate(
                candidate,
                "--require-core-benchmarks",
                "--output-json",
                str(report_json),
            )

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "FAIL")
            self.assertEqual(len(payload["missing"]), 31)


if __name__ == "__main__":
    unittest.main()
