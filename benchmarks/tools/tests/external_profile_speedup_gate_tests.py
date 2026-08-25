import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"
SCRIPT = TOOLS_DIR / "gates" / "external_profile_speedup_gate.py"


def write_benchmark(path: Path, benchmarks: list[dict]) -> None:
    path.write_text(json.dumps({"benchmarks": benchmarks}), encoding="utf-8")


class ExternalProfileSpeedupGateTests(unittest.TestCase):
    def run_gate(self, path: Path, *extra: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--candidate", str(path), *extra],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_normalized_profile_pairs_compare_all_next_variants(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_legacy/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 100.0,
                },
                {
                    "name": "BM_external_next/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 50.0,
                },
                {
                    "name": "BM_external_next_prepared/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 40.0,
                },
                {
                    "name": "BM_external_next_batch/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 45.0,
                },
                {
                    "name": "BM_external_next_prepared_batch/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 35.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_legacy/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 10.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_next_unprepared/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 5.0,
                },
            ])

            completed = self.run_gate(candidate, "--output-json", str(report_json))

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "PASS")
            self.assertEqual(payload["mode"], "normalized-profile")
            self.assertEqual(payload["profile"], "geometry_corpus")
            self.assertEqual(len(payload["rows"]), 4)
            pairs = {(row["next"], row["pairing"]) for row in payload["rows"]}
            self.assertEqual(
                pairs,
                {
                    ("BM_external_next/geometry_corpus", "default"),
                    ("BM_external_next_prepared/geometry_corpus", "prepared"),
                    ("BM_external_next_batch/geometry_corpus", "batch"),
                    ("BM_external_next_prepared_batch/geometry_corpus", "prepared_batch"),
                },
            )
            self.assertEqual(payload["missing"], [])
            self.assertNotIn("BM_external_rectclip_polygon_legacy/tiger", completed.stdout)

    def test_old_source_pairs_are_not_accepted_as_external_profile_gate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_rectclip_polygon_legacy/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 100.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_next_unprepared/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 20.0,
                },
            ])

            completed = self.run_gate(candidate, "--output-json", str(report_json))

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "FAIL")
            self.assertEqual(payload["rows"], [])
            self.assertIn("normalized external benchmark profile", payload["reason"])
            self.assertIn("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT", payload["reason"])

    def test_missing_next_profile_variants_fail(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_legacy/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 100.0,
                },
                {
                    "name": "BM_external_next/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 50.0,
                },
            ])

            completed = self.run_gate(candidate, "--output-json", str(report_json))

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "FAIL")
            self.assertEqual(payload["rows"][0]["pairing"], "default")
            self.assertIn("BM_external_next_prepared/geometry_corpus", payload["missing"])
            self.assertIn("BM_external_next_batch/geometry_corpus", payload["missing"])
            self.assertIn("BM_external_next_prepared_batch/geometry_corpus", payload["missing"])

    def test_pair_floor_still_applies_to_normalized_profile_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_legacy/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 100.0,
                },
                {
                    "name": "BM_external_next/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 90.0,
                },
                {
                    "name": "BM_external_next_prepared/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 40.0,
                },
                {
                    "name": "BM_external_next_batch/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 45.0,
                },
                {
                    "name": "BM_external_next_prepared_batch/geometry_corpus_mean",
                    "run_type": "aggregate",
                    "real_time": 35.0,
                },
            ])

            completed = self.run_gate(candidate, "--output-json", str(report_json))

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertIn("below min pair speedup", payload["reason"])
            self.assertEqual(payload["rows"][0]["pairing"], "default")

    def test_old_mode_argument_is_removed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            write_benchmark(candidate, [])

            removed_mode = "-".join(("default", "unprepared"))
            completed = self.run_gate(candidate, "--mode", removed_mode)

            self.assertEqual(completed.returncode, 2)
            self.assertIn("unrecognized arguments: --mode", completed.stderr)

    def test_cpu_time_only_candidate_fails_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {"name": "BM_external_legacy/geometry_corpus_mean", "run_type": "aggregate", "cpu_time": 100.0},
                {"name": "BM_external_next/geometry_corpus_mean", "run_type": "aggregate", "cpu_time": 50.0},
            ])

            completed = self.run_gate(candidate, "--output-json", str(report_json))

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["time_field"], "real_time")
            self.assertIn("real_time", payload["reason"])


if __name__ == "__main__":
    unittest.main()
