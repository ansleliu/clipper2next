#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "checks" / "check_strict_oracle_equivalence_shape.py"


def qualification_fixture() -> dict:
    # Independent input and absolute expected: a 10-by-10 square has area 100.
    inputs = {"polygon_wkt": "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))"}
    def digest(value: object) -> str:
        return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    case = {
        "id": "square-cdt", "profile_id": "triangulation",
        "operation": "triangulation.delaunay", "inputs": inputs,
        "canonical_input_digest": digest(inputs), "canonical_options_digest": digest({}),
        "comparison_domain": "triangulation-exact-contract", "contract_id": "coverage-constraint-cdt",
        "expected": {"relation": "triangulation-exact-contract", "checks": ["coverage", "constraints", "nonoverlap", "cdt"]},
        "tolerance": {"coordinate": 0, "area": 0},
    }
    case["authority_digest"] = digest({key: case[key] for key in ("operation", "canonical_input_digest", "canonical_options_digest", "comparison_domain", "contract_id", "expected")})
    result = {key: case[key] for key in ("id", "canonical_input_digest", "canonical_options_digest", "comparison_domain", "contract_id", "authority_digest")}
    verdict = {"executed": True, "valid": True, "oracle": "python-int-fraction",
               "checks": {"coverage": True, "constraints": True, "nonoverlap": True, "cdt": True}}
    result.update({"legacy": copy.deepcopy(verdict), "next": copy.deepcopy(verdict),
                   "representation_equal": False, "difference_class": "legal-triangulation-difference"})
    return {"schema_version": 1, "selection_phase": "before-execution", "required_profiles": ["triangulation"],
            "cases": [case], "results": [result]}


class StrictOracleEquivalenceShapeTests(unittest.TestCase):
    def run_checker(self, source_text: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "clipper2next" / "tests" / "oracle" / "external_corpus_tests.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(source_text, encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(CHECKER), "--root", str(root / "clipper2next" / "tests" / "oracle")],
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_rejects_positional_nonzero_coordinate_tolerance(self) -> None:
        result = self.run_checker(
            "oracle::assert_paths_semantically_equal(expected, actual, oracle::path_equivalence_options{1});\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-zero oracle coordinate tolerance", result.stdout)

    def test_rejects_named_nonzero_coordinate_tolerance(self) -> None:
        result = self.run_checker(
            "auto options = oracle::path_equivalence_options{.coordinate_tolerance = 1};\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-zero oracle coordinate tolerance", result.stdout)

    def test_accepts_default_and_zero_tolerance(self) -> None:
        result = self.run_checker(
            "oracle::assert_paths_semantically_equal(expected, actual, oracle::path_equivalence_options{});\n"
            "auto options = oracle::path_equivalence_options{.coordinate_tolerance = 0};\n"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def run_qualification(self, payload: dict) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "qualification.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            return subprocess.run([sys.executable, str(CHECKER), "--root", tmp, "--qualification", str(path)],
                                  text=True, capture_output=True, check=False)

    def test_legal_different_triangulations_pass_independent_contract(self) -> None:
        result = self.run_qualification(qualification_fixture())
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_rejects_weakened_or_incomplete_qualification(self) -> None:
        mutations = {
            "non-zero": lambda p: p["cases"][0]["tolerance"].update(coordinate=1),
            "both engines": lambda p: p["results"][0]["legacy"].update(executed=False),
            "execution binding": lambda p: p["results"][0].update(contract_id="coverage-constraint"),
            "profile": lambda p: p["required_profiles"].append("offset"),
            "before-execution": lambda p: p.update(selection_phase="after-execution"),
            "product-regression": lambda p: p["results"][0]["next"].update(valid=False),
            "unknown": lambda p: p["results"][0].update(difference_class="unknown"),
            "independent": lambda p: p["results"][0]["next"].update(oracle="production-predicate"),
            "cdt": lambda p: p["results"][0]["next"]["checks"].update(cdt=False),
            "authority digest binding": lambda p: p["cases"][0].update(expected={"relation": "triangulation-exact-contract", "checks": ["coverage"]}),
        }
        for message, mutate in mutations.items():
            with self.subTest(message=message):
                payload = qualification_fixture()
                mutate(payload)
                result = self.run_qualification(payload)
                self.assertNotEqual(0, result.returncode)
                self.assertIn(message, result.stdout + result.stderr)

    def test_offset_metamorphic_equality_without_absolute_expected_fails(self) -> None:
        payload = qualification_fixture()
        case = payload["cases"][0]
        case.update(profile_id="offset", operation="offset.polygon", comparison_domain="offset-correction-contract",
                    contract_id="mixed-group-winding", expected={"relation": "offset-correction-contract", "metamorphic": "permutation"})
        payload["required_profiles"] = ["offset"]
        payload["results"][0].update(comparison_domain="offset-correction-contract", contract_id="mixed-group-winding")
        result = self.run_qualification(payload)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("absolute", result.stdout + result.stderr)

    def test_correction_performance_rejects_any_regression(self) -> None:
        payload = qualification_fixture()
        payload["performance"] = {"max_regression_percent": 0,
            "required_product_pairs": ["prepared"], "product_pairs": [
                {"id": "prepared", "baseline_ns": 100, "candidate_ns": 100, "cv_percent": 4.9}],
            "required_legacy_pairs": ["tri"], "legacy_pairs": [
                {"id": "tri", "legacy_ns": 150, "candidate_ns": 100, "cv_percent": 4.9}]}
        self.assertEqual(0, self.run_qualification(payload).returncode)
        for group, field, value, message in (("product_pairs", "candidate_ns", 100.01, "regression"),
                ("product_pairs", "cv_percent", 5.01, "CV"), ("legacy_pairs", "candidate_ns", 126, "speedup")):
            changed = copy.deepcopy(payload)
            changed["performance"][group][0][field] = value
            result = self.run_qualification(changed)
            self.assertNotEqual(0, result.returncode)
            self.assertIn(message, result.stdout + result.stderr)
        payload["performance"]["max_regression_percent"] = 15
        result = self.run_qualification(payload)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("zero", result.stdout + result.stderr)

    def test_performance_noisy_and_omitted_pairs_cannot_qualify(self) -> None:
        base = {"max_regression_percent": 0,
            "required_product_pairs": ["prepared"], "product_pairs": [
                {"id": "prepared", "baseline_ns": 100, "candidate_ns": 100, "cv_percent": 1}],
            "required_legacy_pairs": ["tri"], "legacy_pairs": [
                {"id": "tri", "legacy_ns": 150, "candidate_ns": 100, "cv_percent": 1}]}
        for mutation in (lambda p: p.update(status="NOISY"),
                         lambda p: p["required_product_pairs"].append("offset"),
                         lambda p: p["required_legacy_pairs"].append("offset")):
            performance = copy.deepcopy(base)
            mutation(performance)
            payload = qualification_fixture()
            payload["performance"] = performance
            result = self.run_qualification(payload)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("performance", result.stdout + result.stderr)

    def test_only_approved_absolute_offset_cases_can_depart_from_legacy(self) -> None:
        from tools.release.correction_authority import authority_digest
        payload = qualification_fixture()
        payload["required_profiles"] = ["offset"]
        case, result = payload["cases"][0], payload["results"][0]
        case.update(profile_id="offset", operation="offset.polygon", comparison_domain="offset-correction-contract",
                    contract_id="mixed-group-winding", expected={"relation": "offset-correction-contract",
                        "oracle": "python-int-fraction-analytic", "absolute": {"area": 16400}})
        result.update(comparison_domain="offset-correction-contract", contract_id="mixed-group-winding", difference_class="legacy-invalid-result")
        result["legacy"] = {"executed": True, "valid": False, "oracle": "python-int-fraction-analytic", "checks": {"absolute": False}}
        result["next"] = {"executed": True, "valid": True, "oracle": "python-int-fraction-analytic", "checks": {"absolute": True}}
        case["authority_digest"] = result["authority_digest"] = authority_digest(case)
        self.assertEqual(0, self.run_qualification(payload).returncode)
        case.update(comparison_domain="strict-legacy-runtime", contract_id="strict-legacy-v1", expected={"relation": "strict-legacy-runtime"})
        result.update(comparison_domain="strict-legacy-runtime", contract_id="strict-legacy-v1")
        self.assertNotEqual(0, self.run_qualification(payload).returncode)

    def test_identical_but_invalid_legacy_cannot_qualify(self) -> None:
        payload = qualification_fixture()
        payload["results"][0].update(representation_equal=True, difference_class="none")
        payload["results"][0]["legacy"].update(valid=False)
        payload["results"][0]["legacy"]["checks"].update(cdt=False)
        result = self.run_qualification(payload)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("invalid", result.stdout + result.stderr)

    def test_predeclared_cardinal_rounding_api_difference_can_qualify(self) -> None:
        from tools.release.correction_authority import authority_digest, canonical_digest
        payload = qualification_fixture()
        payload["required_profiles"] = ["offset"]
        case, result = payload["cases"][0], payload["results"][0]
        case.update(profile_id="offset", operation="offset.polygon", comparison_domain="offset-correction-contract",
            contract_id="cardinal-rounding", inputs={"delta": 2.5, "coordinate_rounding": "nearest_even"},
            expected={"relation": "offset-correction-contract", "oracle": "python-int-fraction-analytic",
                      "absolute": {"points": [[2, 0]]}, "legacy_absolute": {"points": [[3, 0]]},
                      "legacy_semantics_mismatch": {"parameter": "coordinate_rounding", "legacy": "nearest_away_from_zero", "next": "nearest_even"}})
        case["canonical_input_digest"] = case["canonical_options_digest"] = canonical_digest(case["inputs"])
        case["authority_digest"] = authority_digest(case)
        result.update({key: case[key] for key in ("comparison_domain", "contract_id", "canonical_input_digest", "canonical_options_digest", "authority_digest")})
        result.update(difference_class="api-rounding-semantics-mismatch")
        for engine in ("legacy", "next"):
            result[engine] = {"executed": True, "valid": True, "oracle": "python-int-fraction-analytic", "checks": {"absolute": True}}
        self.assertEqual(0, self.run_qualification(payload).returncode)
        for key in ("legacy_absolute", "legacy_semantics_mismatch"):
            changed = copy.deepcopy(payload)
            changed["cases"][0]["expected"].pop(key)
            changed["cases"][0]["authority_digest"] = changed["results"][0]["authority_digest"] = authority_digest(changed["cases"][0])
            self.assertNotEqual(0, self.run_qualification(changed).returncode)


if __name__ == "__main__":
    unittest.main()
