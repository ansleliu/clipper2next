#!/usr/bin/env python3
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"

from benchmarks.tools.evidence import archive_release_evidence as evidence


def release_performance_summary() -> dict:
    contract = evidence.load_contract()
    return {
        "schema_version": 1,
        "status": "PASS",
        "evidence_mode": "release",
        "calibrated_runner": True,
        "runner_id": "unit-test-runner",
        "contract_sha256": contract.sha256,
        "repetitions": contract.performance["release_repetitions"],
        "min_time_seconds": contract.performance["min_time_seconds"],
        "max_cv_percent": contract.performance["release_max_cv_percent"],
        "min_pair_speedup": contract.performance["min_pair_speedup"],
        "min_geomean_speedup": contract.performance["min_geomean_speedup"],
        "speedup_mode": "default-unprepared",
        "variance_status": "PASS",
        "speedup_status": "PASS",
    }


def write_required_evidence(root: Path) -> None:
    (root / "ci").mkdir()
    (
        root / "release_calibrated_external_calibrated_external_summary.json"
    ).write_text(
        json.dumps(release_performance_summary()),
        encoding="utf-8",
    )
    (
        root / "release_linux_calibrated_external_calibrated_external_summary.json"
    ).write_text(
        json.dumps(release_performance_summary()),
        encoding="utf-8",
    )
    for log_name in (
        "ctest-linux-gcc-asan-ubsan.log",
        "ctest-linux-gcc-fuzz-smoke.log",
        "ctest-linux-gcc-tsan.log",
    ):
        (root / "ci" / log_name).write_text("ok\n", encoding="utf-8")


class ReleaseEvidenceArchiveTests(unittest.TestCase):
    def test_blocks_missing_release_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            rows = [
                evidence.check_summary(root / "missing.json", calibrated_required=True),
                evidence.check_log(root / "missing.log"),
            ]

        self.assertEqual("BLOCKED", evidence.overall_status(rows))
        self.assertEqual("BLOCKED", rows[0].status)
        self.assertEqual("BLOCKED", rows[1].status)

    def test_accepts_calibrated_summaries_and_nonempty_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            summary = root / "summary.json"
            summary.write_text(
                json.dumps(release_performance_summary()),
                encoding="utf-8",
            )
            log = root / "run.log"
            log.write_text("ok\n", encoding="utf-8")

            rows = [
                evidence.check_summary(
                    summary,
                    calibrated_required=True,
                    release_performance_required=True,
                ),
                evidence.check_log(log),
            ]

        self.assertEqual("PASS", evidence.overall_status(rows))
        self.assertTrue(all(row.status == "PASS" for row in rows))

    def test_rejects_uncalibrated_pass_summary_for_release_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            summary = root / "summary.json"
            summary.write_text(
                json.dumps({"status": "PASS", "calibrated_runner": False}),
                encoding="utf-8",
            )

            row = evidence.check_summary(summary, calibrated_required=True)

        self.assertEqual("BLOCKED", row.status)
        self.assertIn("not from a calibrated runner", row.detail)

    def test_rejects_directional_summary_for_release_archive(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            summary = Path(temp_dir) / "summary.json"
            payload = release_performance_summary()
            payload["evidence_mode"] = "directional"
            summary.write_text(json.dumps(payload), encoding="utf-8")

            row = evidence.check_summary(
                summary,
                calibrated_required=True,
                release_performance_required=True,
            )

        self.assertEqual("BLOCKED", row.status)
        self.assertIn("evidence_mode", row.detail)

    def test_rejects_release_summary_with_weakened_contract_threshold(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            summary = Path(temp_dir) / "summary.json"
            payload = release_performance_summary()
            payload["max_cv_percent"] = 15.0
            summary.write_text(json.dumps(payload), encoding="utf-8")

            row = evidence.check_summary(
                summary,
                calibrated_required=True,
                release_performance_required=True,
            )

        self.assertEqual("BLOCKED", row.status)
        self.assertIn("max_cv_percent", row.detail)

    def test_default_canonical_scope_does_not_require_pgo_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_required_evidence(root)

            with mock.patch.object(
                sys,
                "argv",
                ["archive_release_evidence.py", "--results-dir", str(root)],
            ):
                status = evidence.main()

            report = json.loads(
                (root / "release_release_evidence_archive.json").read_text(
                    encoding="utf-8"
                )
            )

        self.assertEqual(0, status)
        self.assertEqual("canonical", report["artifact_scope"])
        self.assertFalse(any("pgo" in row["name"] for row in report["rows"]))

    def test_canonical_scope_blocks_missing_linux_performance_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_required_evidence(root)
            (
                root / "release_linux_calibrated_external_calibrated_external_summary.json"
            ).unlink()

            with mock.patch.object(
                sys,
                "argv",
                ["archive_release_evidence.py", "--results-dir", str(root)],
            ):
                status = evidence.main()

        self.assertEqual(2, status)

    def test_pgo_artifact_scope_blocks_missing_pgo_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_required_evidence(root)

            with mock.patch.object(
                sys,
                "argv",
                [
                    "archive_release_evidence.py",
                    "--results-dir",
                    str(root),
                    "--require-pgo",
                ],
            ):
                status = evidence.main()

        self.assertEqual(2, status)

    def test_pgo_artifact_scope_requires_full_release_performance_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_required_evidence(root)
            (root / "release_msvc_pgo_msvc_pgo_summary.json").write_text(
                json.dumps({"status": "PASS", "calibrated_runner": True}),
                encoding="utf-8",
            )

            with mock.patch.object(
                sys,
                "argv",
                [
                    "archive_release_evidence.py",
                    "--results-dir",
                    str(root),
                    "--require-pgo",
                ],
            ):
                status = evidence.main()

        self.assertEqual(2, status)

    def test_pgo_artifact_scope_accepts_matching_release_performance_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_required_evidence(root)
            (root / "release_msvc_pgo_msvc_pgo_summary.json").write_text(
                json.dumps(release_performance_summary()),
                encoding="utf-8",
            )

            with mock.patch.object(
                sys,
                "argv",
                [
                    "archive_release_evidence.py",
                    "--results-dir",
                    str(root),
                    "--require-pgo",
                ],
            ):
                status = evidence.main()

            report = json.loads(
                (root / "release_release_evidence_archive.json").read_text(
                    encoding="utf-8"
                )
            )

        self.assertEqual(0, status)
        self.assertEqual("canonical+pgo", report["artifact_scope"])


if __name__ == "__main__":
    unittest.main()
