#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import copy
import json
import hashlib
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Callable

from tools.release.evidence_contract import DEFAULT_CONTRACT_PATH, load_contract
from tools.tests.check_geometry_corpus_profiles_tests import verification_record, benchmark_twin, write_jsonl
from tools.corpus.generate_release_profiles import derive_authority_profiles, derive_offset_contract_source


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
    def test_bare_junit_pass_is_not_authority_qualification(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            junit = Path(tmp) / "ctest.xml"
            write_junit(junit, {name: "PASS" for name in required_external_tests()})
            result = subprocess.run([sys.executable, str(CHECKER), str(junit)], text=True, capture_output=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("qualification", result.stdout + result.stderr)

    def run_checker(
        self,
        statuses: dict[str, str],
        mutate: Callable | None = None,
        refreeze: bool = True,
        include_formal: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp_dir:
            junit = Path(temp_dir) / "ctest.xml"
            write_junit(junit, statuses)
            source, derived = Path(temp_dir) / "source", Path(temp_dir) / "derived"
            contract_payload = json.loads(DEFAULT_CONTRACT_PATH.read_text(encoding="utf-8"))
            # Tiny controlled corpus uses the same schema and all profiles/tests;
            # official release quotas are exercised by the external preflight.
            for profile in contract_payload["profiles"]:
                profile["scenario_quotas"] = {key: quota for key, quota in profile["scenario_quotas"].items() if quota.get("max_count") == 0}
            contract_path = Path(temp_dir) / "contract.json"
            contract_path.write_text(json.dumps(contract_payload), encoding="utf-8")
            contract = load_contract(contract_path)
            operations = {"rectclip": "rectclip.polygon", "rectclip-lines": "rectclip.lines", "offset": "offset.polygon",
                "minkowski": "minkowski.sum", "triangulation": "triangulation.delaunay", "batch": "batch.clip",
                "bounds": "geometry.bounds", "simplification": "geometry.simplify", "collinear-trimming": "geometry.trim_collinear",
                "point-in-polygon": "geometry.point_in_polygon", "scaling": "transform.scale", "translation": "transform.translate"}
            for profile in contract.profiles:
                records = []
                for index in range(2):
                    record = verification_record(profile, operation=operations.get(profile, "overlay.union"),
                        record_id=f"{profile}-{index}-verification", full_id=f"{profile}-{index}", scenario="controlled")
                    record["inputs"]["fill_rule"] = "non_zero"
                    if profile == "batch":
                        record["inputs"].update(comparison_modes=["legacy_scalar", "next_scalar", "next_batch"], requests=[{"operation": "overlay.union"}])
                    record["generator"]["version"] = 1
                    records.append(record)
                write_jsonl(source / "normalized" / "verification" / f"{profile}.jsonl", records)
                write_jsonl(source / "normalized" / "benchmark" / f"{profile}.jsonl", [benchmark_twin(record) for record in records])
            for filename in {profile.eligibility_file for profile in contract.profiles.values()}:
                write_jsonl(source / "normalized" / "full" / filename, [{"id": filename, "profile": "full"}])
            derive_authority_profiles(source, derived)
            payload = {"schema_version": 1, "selection_phase": "before-execution", "required_profiles": list(contract.profiles),
                       "cases": [], "results": []}
            for profile in contract.profiles:
                path = derived / "normalized" / "verification" / f"{profile}.jsonl"
                for line in path.read_text(encoding="utf-8").splitlines():
                    case = json.loads(line)
                    case["profile_id"] = profile
                    result = {key: case[key] for key in ("id", "comparison_domain", "contract_id", "canonical_input_digest", "canonical_options_digest", "authority_digest")}
                    verdict = {"executed": True, "valid": True, "oracle": "python-int-fraction",
                               "checks": {"coverage": True, "constraints": True, "nonoverlap": True, "cdt": True}}
                    result.update(legacy=copy.deepcopy(verdict), next=copy.deepcopy(verdict), representation_equal=True, difference_class="none")
                    payload["cases"].append(case)
                    payload["results"].append(result)
            formal_arguments = []
            if include_formal:
                formal_source = ROOT / "tests/offset/offset_contract_cases.jsonl"
                formal_root = Path(temp_dir) / "formal"
                derive_offset_contract_source(formal_source, formal_root)
                formal_manifest = formal_root / "offset-contract-manifest.json"
                formal_cases = formal_root / "offset-contract-cases.jsonl"
                formal_hash = hashlib.sha256(formal_manifest.read_bytes()).hexdigest()
                payload["offset_contract_manifest_sha256"] = formal_hash
                formal_arguments = ["--offset-contract-manifest", str(formal_manifest),
                    "--offset-contract-source", str(formal_source), "--offset-contract-cases", str(formal_cases),
                    "--frozen-offset-manifest-sha256", formal_hash]
                for line in formal_cases.read_text().splitlines():
                    case = json.loads(line)
                    case["profile_id"] = "offset"
                    result = {key: case[key] for key in ("id", "comparison_domain", "contract_id", "canonical_input_digest", "canonical_options_digest", "authority_digest")}
                    # Controlled evidence-schema fixture, not an oracle execution.
                    verdict = {"executed": True, "valid": True, "oracle": "python-int-fraction-analytic", "checks": {"absolute": True}}
                    result.update(legacy=copy.deepcopy(verdict), next=copy.deepcopy(verdict), representation_equal=True, difference_class="none")
                    payload["cases"].append(case)
                    payload["results"].append(result)
            payload["junit_sha256"] = hashlib.sha256(junit.read_bytes()).hexdigest()
            payload["contract_sha256"] = contract.sha256
            manifest = derived / "authority-manifest.json"
            frozen_digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
            if mutate:
                content = json.loads(manifest.read_text(encoding="utf-8"))
                mutate(content, payload, source, derived)
                manifest.write_text(json.dumps(content), encoding="utf-8")
                if refreeze:
                    frozen_digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
            payload["corpus_manifest_sha256"] = hashlib.sha256(manifest.read_bytes()).hexdigest()
            qualification = Path(temp_dir) / "qualification.json"
            qualification.write_text(json.dumps(payload), encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    str(CHECKER),
                    str(junit),
                    "--contract",
                    str(contract_path),
                    "--qualification", str(qualification),
                    "--corpus-manifest", str(manifest),
                    "--source-corpus-root", str(source), "--corpus-root", str(derived),
                    "--frozen-manifest-sha256", frozen_digest,
                    *formal_arguments,
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

    def test_rejects_external_profiles_without_declared_offset_analytic_source(self):
        completed = self.run_checker({name: "PASS" for name in required_external_tests()}, include_formal=False)
        self.assertNotEqual(0, completed.returncode, completed.stdout + completed.stderr)

    def test_accepts_all_contract_required_profile_tests_with_exact_pass(
        self,
    ) -> None:
        tests = required_external_tests()

        completed = self.run_checker({name: "PASS" for name in tests})

        self.assertEqual(16, len(tests))
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertIn("status=PASS required_tests=16", completed.stdout)

    def test_rejects_incomplete_or_rebound_corpus_manifests(self) -> None:
        def remove_case(manifest, payload, source, derived):
            removed = payload["cases"].pop(0)["id"]
            payload["results"] = [result for result in payload["results"] if result["id"] != removed]
            manifest["case_mappings"] = [case for case in manifest["case_mappings"] if case["id"] != removed]
        mutations = {
            "removed source": lambda m, p, s, d: m["source_files"].pop(),
            "removed case": remove_case,
            "count": lambda m, p, s, d: m["source_files"][0].update(record_count=999),
            "hash": lambda m, p, s, d: m["derived_files"][0].update(sha256="0" * 64),
            "source hash": lambda m, p, s, d: m["source_files"][0].update(sha256="0" * 64),
            "purpose": lambda m, p, s, d: m["source_files"][0].update(purpose="verification"),
            "profile-operation": lambda m, p, s, d: m["case_mappings"][0].update(operation="geometry.bounds"),
        }
        for name, mutation in mutations.items():
            with self.subTest(name=name):
                completed = self.run_checker({name: "PASS" for name in required_external_tests()}, mutation)
                self.assertNotEqual(0, completed.returncode, completed.stdout + completed.stderr)

    def test_rewritten_manifest_after_independent_freeze_fails(self) -> None:
        completed = self.run_checker({name: "PASS" for name in required_external_tests()},
            lambda m, p, s, d: m["source_files"].reverse(), refreeze=False)
        self.assertNotEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertIn("externally retained pre-execution freeze", completed.stdout)

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
