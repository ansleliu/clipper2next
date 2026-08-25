#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from tools.release.collect_release_runtime_evidence import (
    build_runtime_evidence,
    collect_benchmark_names,
    collect_ctest_results,
)
from tools.release.evidence_contract import DEFAULT_CONTRACT_PATH


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "release" / "collect_release_runtime_evidence.py"


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_junit(path: Path, outcomes: list[tuple[str, str]]) -> None:
    suite = ET.Element("testsuite")
    for name, outcome in outcomes:
        case = ET.SubElement(suite, "testcase", name=name)
        if outcome == "FAIL":
            ET.SubElement(case, "failure", message="failed")
        elif outcome == "ERROR":
            ET.SubElement(case, "error", message="errored")
        elif outcome == "SKIP":
            ET.SubElement(case, "skipped", message="skipped")
        elif outcome != "PASS":
            raise ValueError(f"unsupported test outcome {outcome}")
    ET.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


def write_profile_report(path: Path) -> None:
    path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "contract_sha256": "a" * 64,
                "profile_sha256": "b" * 64,
                "status": "PASS",
            }
        ),
        encoding="utf-8",
    )


class ReleaseRuntimeEvidenceCollectorTests(unittest.TestCase):
    def test_failed_error_and_skipped_ctest_results_remain_distinct(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            junit = Path(temp_dir) / "ctest.xml"
            write_junit(
                junit,
                [
                    (
                        "ExternalGeometryCorpus."
                        "OverlayVerificationCorpusExecutesAgainstLegacy"
                        "WithFrozenExpectedTargets",
                        "PASS",
                    ),
                    (
                        "Clipper2NextDifferentialRectClipTests."
                        "PolygonClippedToRectMatchesLegacy",
                        "FAIL",
                    ),
                    (
                        "Clipper2NextDifferentialOffsetTests."
                        "PolygonJoinTypeMatrixMatchesLegacy",
                        "SKIP",
                    ),
                    ("CompilerGate.BuildsWithoutWarnings", "ERROR"),
                ],
            )

            results = collect_ctest_results(junit)

        self.assertEqual(
            "FAIL",
            results[
                "Clipper2NextDifferentialRectClipTests."
                "PolygonClippedToRectMatchesLegacy"
            ],
        )
        self.assertEqual(
            "SKIP",
            results[
                "Clipper2NextDifferentialOffsetTests.PolygonJoinTypeMatrixMatchesLegacy"
            ],
        )
        self.assertEqual("FAIL", results["CompilerGate.BuildsWithoutWarnings"])

    def test_duplicate_ctest_name_is_rejected_instead_of_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            junit = Path(temp_dir) / "ctest.xml"
            write_junit(
                junit,
                [
                    ("Differential.RectClip", "PASS"),
                    ("Differential.RectClip", "FAIL"),
                ],
            )

            with self.assertRaisesRegex(ValueError, "duplicate CTest testcase name"):
                collect_ctest_results(junit)

    def test_empty_ctest_document_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            junit = Path(temp_dir) / "ctest.xml"
            write_junit(junit, [])

            with self.assertRaisesRegex(ValueError, "contains no testcases"):
                collect_ctest_results(junit)

    def test_benchmark_parser_accepts_only_registered_benchmark_names(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            benchmark_list = Path(temp_dir) / "benchmarks.txt"
            benchmark_list.write_text(
                "benchmark banner\n"
                "  BM_external_overlay/legacy  \n"
                "BM_external_overlay/next\n"
                "\n",
                encoding="utf-16",
            )

            names = collect_benchmark_names(benchmark_list)

        self.assertEqual(
            {
                "BM_external_overlay/legacy",
                "BM_external_overlay/next",
            },
            names,
        )

    def test_empty_benchmark_registration_list_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            benchmark_list = Path(temp_dir) / "benchmarks.txt"
            benchmark_list.write_text("benchmark banner only\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "no registered benchmarks"):
                collect_benchmark_names(benchmark_list)

    def test_runtime_evidence_binds_all_input_hashes_and_git_state(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            junit = root / "ctest.xml"
            benchmark_list = root / "benchmarks.txt"
            profile_report = root / "profile-report.json"
            write_junit(junit, [("Differential.RectClip", "PASS")])
            benchmark_list.write_text(
                "BM_external_rectclip/legacy\nBM_external_rectclip/next\n",
                encoding="utf-8",
            )
            write_profile_report(profile_report)
            expected_profile_report_sha256 = file_sha256(profile_report)
            expected_inputs = {
                "contract": DEFAULT_CONTRACT_PATH,
                "profile_report": profile_report,
                "ctest_junit": junit,
                "benchmark_list": benchmark_list,
            }
            expected_input_sha256 = {
                key: file_sha256(path) for key, path in expected_inputs.items()
            }

            evidence = build_runtime_evidence(
                contract_path=DEFAULT_CONTRACT_PATH,
                profile_report_path=profile_report,
                ctest_junit_path=junit,
                benchmark_list_path=benchmark_list,
                repository_root=REPO_ROOT,
            )

        self.assertEqual(1, evidence["schema_version"])
        self.assertEqual(
            file_sha256(DEFAULT_CONTRACT_PATH),
            evidence["contract_sha256"],
        )
        self.assertEqual(
            expected_profile_report_sha256,
            evidence["profile_report_sha256"],
        )
        self.assertEqual("PASS", evidence["tests"]["Differential.RectClip"])
        self.assertEqual(
            [
                "BM_external_rectclip/legacy",
                "BM_external_rectclip/next",
            ],
            evidence["benchmarks"],
        )
        self.assertRegex(evidence["git"]["commit"], r"^[0-9a-f]{40,64}$")
        self.assertIsInstance(evidence["git"]["dirty"], bool)
        for key, path in expected_inputs.items():
            self.assertEqual(str(path.resolve()), evidence["inputs"][key])
            self.assertEqual(
                expected_input_sha256[key],
                evidence["input_sha256"][key],
            )

    def test_boolean_profile_report_schema_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            junit = root / "ctest.xml"
            benchmark_list = root / "benchmarks.txt"
            profile_report = root / "profile-report.json"
            write_junit(junit, [("Differential.RectClip", "PASS")])
            benchmark_list.write_text(
                "BM_external_rectclip\n",
                encoding="utf-8",
            )
            profile_report.write_text(
                json.dumps(
                    {
                        "schema_version": True,
                        "contract_sha256": "a" * 64,
                        "profile_sha256": "b" * 64,
                        "status": "PASS",
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "schema_version 1"):
                build_runtime_evidence(
                    contract_path=DEFAULT_CONTRACT_PATH,
                    profile_report_path=profile_report,
                    ctest_junit_path=junit,
                    benchmark_list_path=benchmark_list,
                    repository_root=REPO_ROOT,
                )

    def test_cli_writes_schema_version_one_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            junit = root / "ctest.xml"
            benchmark_list = root / "benchmarks.txt"
            profile_report = root / "profile-report.json"
            output = root / "runtime-evidence.json"
            write_junit(junit, [("Differential.RectClip", "PASS")])
            benchmark_list.write_text(
                "BM_external_rectclip\n",
                encoding="utf-8",
            )
            write_profile_report(profile_report)

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--contract",
                    str(DEFAULT_CONTRACT_PATH),
                    "--ctest-junit",
                    str(junit),
                    "--benchmark-list",
                    str(benchmark_list),
                    "--profile-report",
                    str(profile_report),
                    "--repository-root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(0, result.returncode, result.stderr)
            payload = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(1, payload["schema_version"])
        self.assertEqual("PASS", payload["tests"]["Differential.RectClip"])
        self.assertIn("runtime_evidence=", result.stdout)


if __name__ == "__main__":
    unittest.main()
