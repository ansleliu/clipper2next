#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.checks import check_ctest_skips as check


SCRIPT = Path(check.__file__).resolve()


class CTestSkipCheckTests(unittest.TestCase):
    def test_parse_gtest_and_ctest_skip_lines(self) -> None:
        records = check.parse_skips(
            "\n".join(
                [
                    "[  SKIPPED ] ExternalGeometryCorpus.OverlayVerificationCorpusExistsWhenEnabled",
                    "42/80 Test #42: PendingThing ...................................***Skipped   0.01 sec",
                ]
            )
        )

        self.assertEqual(2, len(records))
        self.assertEqual(
            "ExternalGeometryCorpus.OverlayVerificationCorpusExistsWhenEnabled",
            records[0].name,
        )
        self.assertEqual("PendingThing", records[1].name)

    def test_parse_skips_ignores_gtest_summary_lines(self) -> None:
        records = check.parse_skips(
            "\n".join(
                [
                    "[  SKIPPED ] 1 test, listed below:",
                    "[  SKIPPED ] CurrentGTestFixture.SkippedCase",
                ]
            )
        )

        self.assertEqual(1, len(records))
        self.assertEqual("CurrentGTestFixture.SkippedCase", records[0].name)

    def test_required_corpus_skip_fails_even_when_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log = Path(temp_dir) / "ctest.log"
            log.write_text(
                "[  SKIPPED ] ExternalGeometryCorpus.OverlayVerificationCorpusExistsWhenEnabled\n",
                encoding="utf-8",
            )

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), str(log), "--allow", "ExternalGeometryCorpus."],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("status=FAIL", completed.stdout)
            self.assertIn("ExternalGeometryCorpus", completed.stdout)

    def test_allowed_pending_skip_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log = Path(temp_dir) / "ctest.log"
            log.write_text(
                "10/80 Test #10: PendingThing ...................................***Skipped   0.01 sec\n",
                encoding="utf-8",
            )

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), str(log), "--allow", "PendingThing"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("status=PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
