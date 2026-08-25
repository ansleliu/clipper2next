import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"
SCRIPT = TOOLS_DIR / "gates" / "external_legacy_speedup_gate.py"


def write_benchmark(path: Path, benchmarks: list[dict]) -> None:
    path.write_text(json.dumps({"benchmarks": benchmarks}), encoding="utf-8")


class ExternalLegacySpeedupGateTests(unittest.TestCase):
    def run_gate(self, path: Path, *extra: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--candidate", str(path), *extra],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_default_mode_uses_unprepared_core_pairs_as_hard_gate(self) -> None:
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
                    "real_time": 90.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_next/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 20.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_next_immutable/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 10.0,
                },
            ])

            completed = self.run_gate(
                candidate,
                "--min-geomean-speedup",
                "1.0",
                "--output-json",
                str(report_json),
            )

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["mode"], "default-unprepared")
            self.assertEqual(
                payload["rows"][0]["next"],
                "BM_external_rectclip_polygon_next_unprepared/tiger",
            )
            self.assertEqual(payload["rows"][0]["pairing"], "unprepared")
            self.assertIn("below min pair speedup", payload["reason"])

    def test_default_unprepared_mode_falls_back_to_next_when_unprepared_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {
                    "name": "BM_external_triangulation_legacy/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 100.0,
                },
                {
                    "name": "BM_external_triangulation_next/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 25.0,
                },
            ])

            completed = self.run_gate(
                candidate,
                "--mode",
                "default-unprepared",
                "--output-json",
                str(report_json),
            )

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(
                payload["rows"][0]["next"],
                "BM_external_triangulation_next/tiger",
            )
            self.assertEqual(payload["rows"][0]["pairing"], "default")

    def test_explicit_reuse_reports_supported_reuse_pairs_separately(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {"name": "BM_external_legacy/tiger_mean", "run_type": "aggregate", "real_time": 100.0},
                {
                    "name": "BM_external_next_prepared/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 20.0,
                },
                {
                    "name": "BM_external_next_prepared_batch/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 25.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_legacy/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 90.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_next/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 30.0,
                },
                {
                    "name": "BM_external_rectclip_polygon_next_immutable/tiger_mean",
                    "run_type": "aggregate",
                    "real_time": 25.0,
                },
                {
                    "name": "BM_external_open_clip_lines_legacy/roads_mean",
                    "run_type": "aggregate",
                    "real_time": 80.0,
                },
                {
                    "name": "BM_external_open_clip_lines_next/roads_mean",
                    "run_type": "aggregate",
                    "real_time": 10.0,
                },
                {
                    "name": "BM_external_rectclip_lines_legacy/roads_mean",
                    "run_type": "aggregate",
                    "real_time": 60.0,
                },
                {
                    "name": "BM_external_rectclip_lines_next/roads_mean",
                    "run_type": "aggregate",
                    "real_time": 20.0,
                },
            ])

            completed = self.run_gate(
                candidate,
                "--mode",
                "explicit-reuse",
                "--output-json",
                str(report_json),
            )

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "PASS")
            self.assertEqual(payload["mode"], "explicit-reuse")
            self.assertEqual(len(payload["rows"]), 5)
            pairings = {(row["next"], row["pairing"]) for row in payload["rows"]}
            self.assertIn(("BM_external_next_prepared/tiger", "prepared"), pairings)
            self.assertIn(("BM_external_next_prepared_batch/tiger", "prepared_batch"), pairings)
            self.assertIn(("BM_external_rectclip_polygon_next/tiger", "prepared"), pairings)
            self.assertIn(
                ("BM_external_rectclip_polygon_next_immutable/tiger", "immutable"),
                pairings,
            )
            self.assertIn(("BM_external_open_clip_lines_next/roads", "prepared"), pairings)
            self.assertEqual(payload["skipped"], ["BM_external_rectclip_lines_legacy/roads"])

    def test_allow_slower_pairs_uses_geomean_only_for_observational_runs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            write_benchmark(candidate, [
                {"name": "BM_external_legacy/a_mean", "run_type": "aggregate", "real_time": 120.0},
                {
                    "name": "BM_external_next_unprepared/a_mean",
                    "run_type": "aggregate",
                    "real_time": 100.0,
                },
                {
                    "name": "BM_external_open_clip_lines_legacy/b_mean",
                    "run_type": "aggregate",
                    "real_time": 300.0,
                },
                {
                    "name": "BM_external_open_clip_lines_next/b_mean",
                    "run_type": "aggregate",
                    "real_time": 50.0,
                },
            ])

            completed = self.run_gate(candidate, "--allow-slower-pairs")

            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertIn("status=PASS", completed.stdout)

    def test_missing_next_pairs_fail_and_write_markdown(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            markdown = Path(tmp) / "speedup.md"
            write_benchmark(candidate, [
                {"name": "BM_external_legacy/tiger_mean", "run_type": "aggregate", "real_time": 100.0},
            ])

            completed = self.run_gate(candidate, "--output-md", str(markdown))

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            report = markdown.read_text(encoding="utf-8")
            self.assertIn("Status: **FAIL**", report)
            self.assertIn("Missing pairs", report)

    def test_cpu_time_only_candidate_fails_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            candidate = Path(tmp) / "candidate.json"
            report_json = Path(tmp) / "speedup.json"
            write_benchmark(candidate, [
                {"name": "BM_external_legacy/tiger_mean", "run_type": "aggregate", "cpu_time": 100.0},
                {
                    "name": "BM_external_next_unprepared/tiger_mean",
                    "run_type": "aggregate",
                    "cpu_time": 50.0,
                },
            ])

            completed = self.run_gate(candidate, "--output-json", str(report_json))

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["time_field"], "real_time")
            self.assertIn("real_time", payload["reason"])

    def test_required_core_pairs_fail_when_an_expected_pair_is_absent(self) -> None:
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

            completed = self.run_gate(
                candidate,
                "--require-core-pairs",
                "--output-json",
                str(report_json),
            )

            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            payload = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(payload["status"], "FAIL")
            self.assertEqual(len(payload["rows"]), 1)
            self.assertEqual(len(payload["missing"]), 13)


if __name__ == "__main__":
    unittest.main()
