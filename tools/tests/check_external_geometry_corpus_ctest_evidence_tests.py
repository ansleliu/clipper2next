#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from tools.release.evidence_contract import DEFAULT_CONTRACT_PATH, load_contract


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "checks" / "check_external_geometry_corpus_ctest_evidence.py"


def required_external_tests() -> list[str]:
    contract = load_contract(DEFAULT_CONTRACT_PATH)
    return sorted(
        {
            test_name
            for algorithm in contract.algorithms.values()
            for test_name in algorithm.required_tests
            if test_name.startswith("ExternalGeometryCorpus.")
        }
    )


def write_junit(path: Path, statuses: dict[str, str]) -> None:
    suite = ET.Element("testsuite")
    for name, status in statuses.items():
        case = ET.SubElement(suite, "testcase", name=name)
        if status == "FAIL":
            ET.SubElement(case, "failure")
        elif status == "SKIP":
            ET.SubElement(case, "skipped")
        elif status != "PASS":
            raise ValueError(f"unsupported test status {status}")
    ET.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


class ExternalGeometryCorpusCtestEvidenceTests(unittest.TestCase):
    def run_checker(
        self,
        statuses: dict[str, str],
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp_dir:
            junit = Path(temp_dir) / "ctest.xml"
            write_junit(junit, statuses)
            return subprocess.run(
                [
                    sys.executable,
                    str(CHECKER),
                    str(junit),
                    "--contract",
                    str(DEFAULT_CONTRACT_PATH),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

    def test_accepts_all_contract_required_profile_tests_with_exact_pass(
        self,
    ) -> None:
        tests = required_external_tests()

        completed = self.run_checker({name: "PASS" for name in tests})

        self.assertEqual(16, len(tests))
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertIn("status=PASS required_tests=16", completed.stdout)

    def test_skipped_required_profile_test_is_not_pass(self) -> None:
        statuses = {name: "PASS" for name in required_external_tests()}
        skipped = next(iter(statuses))
        statuses[skipped] = "SKIP"

        completed = self.run_checker(statuses)

        self.assertEqual(1, completed.returncode)
        self.assertIn("status=FAIL", completed.stdout)
        self.assertIn(f"{skipped}: SKIP", completed.stdout)

    def test_failed_required_profile_test_is_not_pass(self) -> None:
        statuses = {name: "PASS" for name in required_external_tests()}
        failed = next(iter(statuses))
        statuses[failed] = "FAIL"

        completed = self.run_checker(statuses)

        self.assertEqual(1, completed.returncode)
        self.assertIn(f"{failed}: FAIL", completed.stdout)

    def test_missing_required_profile_test_is_rejected(self) -> None:
        statuses = {name: "PASS" for name in required_external_tests()}
        missing = next(iter(statuses))
        del statuses[missing]

        completed = self.run_checker(statuses)

        self.assertEqual(1, completed.returncode)
        self.assertIn(f"{missing}: MISSING", completed.stdout)

    def test_console_text_containing_passed_cannot_substitute_for_junit(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log = Path(temp_dir) / "ctest.log"
            log.write_text(
                "Test #1: ExternalGeometryCorpus.Fake ... Passed\n",
                encoding="utf-8",
            )

            completed = subprocess.run(
                [
                    sys.executable,
                    str(CHECKER),
                    str(log),
                    "--contract",
                    str(DEFAULT_CONTRACT_PATH),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

        self.assertEqual(2, completed.returncode)
        self.assertIn("invalid CTest JUnit XML", completed.stderr)


if __name__ == "__main__":
    unittest.main()
