"""Pre-execution comparison domains and qualification evidence invariants.

This module validates evidence; geometry record producers and independent
mathematical oracles belong to their algorithm tasks, not this policy gate.
"""
from __future__ import annotations

import hashlib
import json
import math
from typing import Any

OPTION_KEYS = frozenset({
    "fill_rule", "join_type", "end_type", "is_closed", "preserve_collinear",
    "reverse_solution", "delta", "miter_limit", "arc_tolerance",
    "coordinate_rounding", "precision", "scale", "use_delaunay", "callback",
    "groups", "requests", "comparison_modes", "arc_segments_per_quadrant",
})
OFFSET_CONTRACTS = (
    "exact-area-thin-bevel", "mixed-group-winding", "group-parameter-isolation",
    "cardinal-rounding", "constant-callback-analytic",
)
CORRECTION_CV_HOST_OVERRIDES = {
    "Windows": {"host_name": "DESKTOP-S2N5RLE", "max_cv_percent": 30},
}
AUTHORITY_POLICY = {
    "engines": ["legacy", "next"],
    "selection_phase": "before-execution",
    "default_domain": "strict-legacy-runtime",
    "triangulation_domain": "triangulation-exact-contract",
    "triangulation_contracts": ["coverage-constraint", "coverage-constraint-cdt"],
    "offset_domain": "offset-correction-contract",
    "offset_contracts": list(OFFSET_CONTRACTS),
    "offset_requires_absolute_expected": True,
    "unknown_difference": "FAIL",
    "correction_max_regression_percent": 0,
    "correction_max_cv_percent": 5,
    "correction_cv_host_overrides": CORRECTION_CV_HOST_OVERRIDES,
    "correction_min_legacy_speedup": 1.2,
    "difference_classes": ["api-rounding-semantics-mismatch", "legacy-invalid-result",
                           "legal-triangulation-difference", "product-regression"],
}


def correction_max_cv_percent(platform_name: str | None, host_name: str | None) -> float:
    """Apply the authorized old Windows host exception; all others retain 5%."""
    override = CORRECTION_CV_HOST_OVERRIDES.get(platform_name) if isinstance(platform_name, str) else None
    if override and host_name == override["host_name"]:
        return float(override["max_cv_percent"])
    return float(AUTHORITY_POLICY["correction_max_cv_percent"])


def canonical_digest(value: object) -> str:
    encoded = json.dumps(value, allow_nan=False, ensure_ascii=False,
                         sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def canonical_options(inputs: dict[str, Any]) -> dict[str, Any]:
    return {key: inputs[key] for key in sorted(OPTION_KEYS & set(inputs))}


def authority_digest(record: dict[str, Any]) -> str:
    return canonical_digest({key: record.get(key) for key in (
        "operation", "canonical_input_digest", "canonical_options_digest",
        "comparison_domain", "contract_id", "expected",
    )})


def default_authority(operation: str) -> tuple[str, str, dict[str, Any]]:
    if operation.startswith("triangulation."):
        cdt = operation == "triangulation.delaunay"
        domain = "triangulation-exact-contract"
        return domain, "coverage-constraint-cdt" if cdt else "coverage-constraint", {
            "relation": domain, "checks": ["coverage", "constraints", "nonoverlap"] + (["cdt"] if cdt else []),
        }
    return "strict-legacy-runtime", "strict-legacy-v1", {"relation": "strict-legacy-runtime"}


def validate_authority(record: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if record.get("authority_digest") != authority_digest(record):
        errors.append("authority digest binding mismatch")
    domain, contract_id, expected = (record.get(key) for key in ("comparison_domain", "contract_id", "expected"))
    operation = record.get("operation", "")
    if not isinstance(operation, str) or not isinstance(expected, dict):
        return ["authority requires operation and expected"]
    default_domain, default_id, default_expected = default_authority(operation)
    if domain == "offset-correction-contract":
        if not operation.startswith("offset.") or contract_id not in OFFSET_CONTRACTS:
            errors.append("authority has unknown Offset contract/domain")
        absolute = expected.get("absolute")
        if not isinstance(absolute, dict) or not absolute or not set(absolute) & {"paths", "area", "direction", "points"}:
            errors.append("Offset authority requires independent absolute expected; metamorphic equality is insufficient")
        if expected.get("oracle") != "python-int-fraction-analytic":
            errors.append("Offset authority requires independent analytic oracle")
        if "legacy_semantics_mismatch" in expected and not declared_rounding_mismatch(record):
            errors.append("Offset authority semantic mismatch requires fixed cardinal rounding and independent legacy absolute expected")
    elif (domain, contract_id, expected) != (default_domain, default_id, default_expected):
        errors.append("authority must match fixed operation domain/contract/expected")
    if expected.get("relation") != domain:
        errors.append("authority expected.relation does not match comparison_domain")
    return errors


def declared_rounding_mismatch(record: dict[str, Any]) -> bool:
    expected, inputs = record.get("expected"), record.get("inputs")
    if not isinstance(expected, dict) or not isinstance(inputs, dict):
        return False
    legacy_absolute = expected.get("legacy_absolute")
    return (
        record.get("comparison_domain") == "offset-correction-contract"
        and record.get("contract_id") == "cardinal-rounding"
        and inputs.get("coordinate_rounding") == "nearest_even"
        and expected.get("legacy_semantics_mismatch") == {
            "parameter": "coordinate_rounding", "legacy": "nearest_away_from_zero", "next": "nearest_even",
        }
        and isinstance(legacy_absolute, dict) and bool(legacy_absolute)
        and bool(set(legacy_absolute) & {"paths", "area", "direction", "points"})
    )


def validate_qualification(payload: dict[str, Any], required_profiles: set[str] | None = None) -> list[str]:
    errors: list[str] = []
    if payload.get("schema_version") != 1 or payload.get("selection_phase") != "before-execution":
        errors.append("qualification requires schema 1 and before-execution selection")
    declared = payload.get("required_profiles")
    if not isinstance(declared, list) or not declared or not all(isinstance(p, str) for p in declared):
        return errors + ["qualification requires nonempty required profiles"]
    required = required_profiles if required_profiles is not None else set(declared)
    if set(declared) != required:
        errors.append("qualification required profile set differs from release contract")
    cases, results = payload.get("cases"), payload.get("results")
    if not isinstance(cases, list) or not cases or not isinstance(results, list):
        return errors + ["qualification requires nonempty cases and results"]
    result_by_id: dict[str, dict[str, Any]] = {}
    for result in results:
        if not isinstance(result, dict) or not isinstance(result.get("id"), str) or result["id"] in result_by_id:
            errors.append("qualification duplicate or invalid result")
            continue
        result_by_id[result["id"]] = result
    seen: set[str] = set()
    profiles: set[str] = set()
    for case in cases:
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or case["id"] in seen:
            errors.append("qualification duplicate or invalid case")
            continue
        identifier = case["id"]
        seen.add(identifier)
        profile_id = case.get("profile_id")
        if not isinstance(profile_id, str) or not profile_id:
            errors.append(f"{identifier}: invalid profile")
        else:
            profiles.add(profile_id)
        errors.extend(f"{identifier}: {error}" for error in validate_authority(case))
        tolerance = case.get("tolerance")
        if tolerance != {"coordinate": 0, "area": 0} or any(isinstance(value, bool) for value in tolerance.values()):
            errors.append(f"{identifier}: non-zero or missing tolerance")
        inputs = case.get("inputs")
        if not isinstance(inputs, dict) or case.get("canonical_input_digest") != canonical_digest(inputs) or case.get("canonical_options_digest") != canonical_digest(canonical_options(inputs)):
            errors.append(f"{identifier}: input/options digest binding mismatch")
        result = result_by_id.get(identifier)
        if result is None:
            errors.append(f"{identifier}: missing execution result")
            continue
        for key in ("canonical_input_digest", "canonical_options_digest", "comparison_domain", "contract_id", "authority_digest"):
            if result.get(key) != case.get(key):
                errors.append(f"{identifier}: execution binding mismatch: {key}")
        legacy, next_result = result.get("legacy"), result.get("next")
        if not isinstance(legacy, dict) or not isinstance(next_result, dict) or legacy.get("executed") is not True or next_result.get("executed") is not True:
            errors.append(f"{identifier}: both engines must execute; skip forbidden")
            continue
        if not isinstance(legacy.get("valid"), bool) or next_result.get("valid") is not True:
            errors.append(f"{identifier}: product-regression or missing independent validity")
        equal = result.get("representation_equal")
        difference = result.get("difference_class")
        domain = case.get("comparison_domain")
        if domain != "strict-legacy-runtime":
            operation = case.get("operation")
            _, _, tri_expected = default_authority(operation if isinstance(operation, str) else "")
            required_checks = ["absolute"] if domain == "offset-correction-contract" else tri_expected.get("checks", [])
            oracle = "python-int-fraction-analytic" if domain == "offset-correction-contract" else "python-int-fraction"
            for engine_name, verdict in (("legacy", legacy), ("next", next_result)):
                checks = verdict.get("checks")
                if verdict.get("oracle") != oracle or not isinstance(checks, dict) or any(not isinstance(checks.get(check), bool) for check in required_checks):
                    errors.append(f"{identifier}: {engine_name} requires independent oracle and every contract check")
                elif verdict.get("valid") != all(checks[check] for check in required_checks):
                    errors.append(f"{identifier}: {engine_name} independent verdict contradicts contract checks {required_checks}")
        if equal is True:
            if legacy.get("valid") is not True:
                errors.append(f"{identifier}: identical invalid legacy geometry cannot qualify")
            if difference != "none":
                errors.append(f"{identifier}: equal representation must use difference_class=none")
        elif equal is False:
            if declared_rounding_mismatch(case) and legacy.get("valid") is True:
                allowed = "api-rounding-semantics-mismatch"
            else:
                allowed = "legacy-invalid-result" if legacy.get("valid") is False else "legal-triangulation-difference"
            if domain == "strict-legacy-runtime" or difference != allowed or (allowed == "legal-triangulation-difference" and domain != "triangulation-exact-contract"):
                errors.append(f"{identifier}: unknown/unqualified difference: {difference}")
        else:
            errors.append(f"{identifier}: representation comparison required")
    if profiles != required:
        errors.append("qualification profile set missing/unknown/empty profile")
    if set(result_by_id) != seen:
        errors.append("qualification results must account for every case exactly once")
    if "performance" in payload:
        errors.extend(validate_correction_performance(payload["performance"]))
    return errors


def validate_correction_performance(evidence: Any) -> list[str]:
    """Accept measured baseline/candidate pairs only at zero allowed regression.

    The existing legacy speedup requirement remains separate from candidate
    regression and CV stability. No speedup claim follows from absent evidence.
    """
    if not isinstance(evidence, dict) or isinstance(evidence.get("max_regression_percent"), bool) or evidence.get("max_regression_percent") != 0:
        return ["correction performance requires zero allowed regression"]
    if evidence.get("status", "PASS") != "PASS":
        return ["correction performance NOISY/inconclusive/failed evidence cannot qualify"]
    errors: list[str] = []
    speedups: list[float] = []
    max_cv = correction_max_cv_percent(evidence.get("platform"), evidence.get("host_name"))
    for domain, reference_field in (("product", "baseline_ns"), ("legacy", "legacy_ns")):
        required = evidence.get(f"required_{domain}_pairs")
        pairs = evidence.get(f"{domain}_pairs")
        if not isinstance(required, list) or not required or not all(isinstance(pair, str) for pair in required) or len(set(required)) != len(required):
            errors.append(f"correction performance requires fixed nonempty {domain} pairs")
            continue
        if not isinstance(pairs, list) or not pairs:
            errors.append(f"correction performance requires measured {domain} pairs")
            continue
        seen: set[str] = set()
        for pair in pairs:
            if not isinstance(pair, dict) or not isinstance(pair.get("id"), str) or pair["id"] in seen:
                errors.append(f"correction performance duplicate/invalid {domain} pair")
                continue
            seen.add(pair["id"])
            values = [pair.get(key) for key in (reference_field, "candidate_ns", "cv_percent")]
            if any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) for value in values):
                errors.append(f"{pair['id']}: performance requires finite measured times/CV")
                continue
            reference, candidate, cv = values
            if reference <= 0 or candidate <= 0:
                errors.append(f"{pair['id']}: performance requires positive measured times")
            elif domain == "product":
                if candidate > reference:
                    errors.append(f"{pair['id']}: correction performance regression")
            else:
                speedup = reference / candidate
                speedups.append(speedup)
                if speedup < 1.2:
                    errors.append(f"{pair['id']}: legacy pair speedup below 1.2x")
            if cv < 0 or cv > max_cv:
                errors.append(f"{pair['id']}: correction performance CV exceeds {max_cv:g}%")
        if seen != set(required):
            errors.append(f"correction performance must account for every required {domain} pair; exclusions forbidden")
    if speedups and math.exp(sum(map(math.log, speedups)) / len(speedups)) < 1.2:
        errors.append("legacy geomean speedup below 1.2x")
    return errors
