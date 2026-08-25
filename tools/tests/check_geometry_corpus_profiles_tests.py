#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.checks import check_geometry_corpus_profiles as check  # noqa: E402
from tools.release.evidence_contract import (  # noqa: E402
    ProfileContract,
    ScenarioQuota,
)


SCRIPT = Path(check.__file__).resolve()
OPTION_KEYS = {
    "fill_rule",
    "join_type",
    "end_type",
    "is_closed",
    "preserve_collinear",
    "reverse_solution",
}


def canonical_json_digest(value: object) -> str:
    encoded = json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def profile_contract(
    profile_id: str = "rectclip",
    *,
    scenario_quotas: dict[str, ScenarioQuota] | None = None,
    forbidden_scenarios: tuple[str, ...] = (),
) -> ProfileContract:
    return ProfileContract(
        id=profile_id,
        eligibility_file="shape-inputs.jsonl",
        geometry_types=("Polygon", "MultiPolygon"),
        required_case_sets=("verification", "benchmark"),
        scenario_quotas=scenario_quotas
        if scenario_quotas is not None
        else {"crossing": ScenarioQuota(min_count=1)},
        forbidden_scenarios=forbidden_scenarios,
    )


def verification_record(
    profile_id: str = "rectclip",
    *,
    record_id: str = "rectclip-crossing-verification-1",
    full_id: str = "shape-a",
    scenario: str = "crossing",
    partition: str = "release-holdout",
    operation: str | None = None,
) -> dict[str, object]:
    operation = operation or {
        "rectclip": "rectclip.polygon",
        "offset": "offset.polygon",
        "minkowski": "minkowski.sum",
    }.get(profile_id, "overlay.intersection")
    inputs: dict[str, object] = {
        "paths_wkt": "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))",
        "rect": {"left": 5, "top": -1, "right": 11, "bottom": 11},
    }
    if profile_id == "offset":
        inputs = {
            "paths_wkt": "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))",
            "delta": 2.0,
            "join_type": "miter",
            "end_type": "polygon",
        }
    elif profile_id == "minkowski":
        inputs = {
            "pattern_wkt": "POLYGON ((0 0, 2 0, 1 1, 0 0))",
            "path_wkt": "POLYGON ((0 0, 10 0, 10 10, 0 0))",
            "is_closed": True,
        }

    record: dict[str, object] = {
        "id": record_id,
        "profile": "verification",
        "operation": operation,
        "scenario": scenario,
        "partition": partition,
        "generator": {"name": "generate_release_profiles", "version": 1},
        "inputs": inputs,
        "expected": {"relation": "strict-legacy-runtime"},
        "reference_engines": [{"engine": "clipper2-legacy", "version": "2.0.1"}],
        "selector": {
            "method": "sha256-stratified-v1",
            "full_id": full_id,
        },
        "source": {"source_id": "test"},
        "tags": [profile_id, scenario],
        "tolerance": {"area": 0.0, "coordinate": 0.0},
    }
    options = {key: inputs[key] for key in sorted(OPTION_KEYS & set(inputs))}
    record["canonical_input_digest"] = canonical_json_digest(inputs)
    record["canonical_options_digest"] = canonical_json_digest(options)
    return record


def benchmark_twin(record: dict[str, object]) -> dict[str, object]:
    twin = copy.deepcopy(record)
    twin["id"] = str(record["id"]).replace("verification", "benchmark")
    twin["profile"] = "benchmark"
    twin.pop("expected")
    twin.pop("reference_engines")
    twin["complexity"] = {"point_count": 5}
    twin["parameters"] = {"iterations_hint": 1}
    return twin


def write_jsonl(path: Path, records: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(f"{json.dumps(record, sort_keys=True)}\n" for record in records),
        encoding="utf-8",
    )


def minimal_contract_payload() -> dict[str, object]:
    return {
        "schema_version": 1,
        "equivalence": {
            "coordinate_tolerance": 0,
            "normalize_closed_ring_start": True,
            "normalize_independent_path_order": True,
            "normalize_winding": False,
            "normalize_open_path_direction": False,
        },
        "profiles": [
            {
                "id": "rectclip",
                "eligibility_file": "shape-inputs.jsonl",
                "geometry_types": ["Polygon", "MultiPolygon"],
                "required_case_sets": ["verification", "benchmark"],
                "scenario_quotas": {"crossing": {"min_count": 1}},
                "forbidden_scenarios": [],
            }
        ],
        "algorithms": [
            {
                "id": "rect_clip_polygon",
                "release_gated": True,
                "required_tests": ["RectClip.StrictLegacy"],
                "required_benchmarks": ["BM_rectclip"],
                "required_profiles": ["rectclip"],
            }
        ],
        "performance": {
            "release_repetitions": 7,
            "min_time_seconds": 0.5,
            "min_warmup_time_seconds": 1.0,
            "release_max_cv_percent": 5.0,
            "directional_max_cv_percent": 15.0,
            "min_pair_speedup": 1.2,
            "min_geomean_speedup": 1.2,
            "time_field": "real_time",
        },
        "provenance": [
            "git_commit",
            "git_tree_state",
            "contract_sha256",
            "profile_sha256",
            "benchmark_executable_sha256",
            "compiler",
            "build_flags",
            "runner_id",
        ],
    }


class GeometryCorpusProfileCheckTests(unittest.TestCase):
    def test_error_summary_preserves_total_and_categories_when_examples_are_capped(
        self,
    ) -> None:
        summary = check.summarize_errors(
            [
                "verification:rectclip[1]: missing scenario",
                "verification:rectclip[2]: missing scenario",
                "benchmark record has no identical verification twin: case-a",
            ],
            max_examples=2,
        )

        self.assertEqual(3, summary["error_count"])
        self.assertTrue(summary["errors_truncated"])
        self.assertEqual(2, len(summary["errors"]))
        self.assertEqual(
            2,
            summary["error_categories"]["verification:rectclip[]: missing scenario"],
        )

    def test_valid_semantic_profile_and_benchmark_twin_pass(self) -> None:
        verification = verification_record()
        benchmark = benchmark_twin(verification)
        contract = profile_contract()

        errors = check.validate_profile_semantics(
            [verification], contract, "verification"
        )
        errors.extend(
            check.validate_profile_semantics([benchmark], contract, "benchmark")
        )
        errors.extend(check.validate_twins([verification], [benchmark]))

        self.assertEqual([], errors)

    def test_duplicate_record_ids_fail(self) -> None:
        first = verification_record()
        second = verification_record(full_id="shape-b")

        errors = check.validate_profile_semantics(
            [first, second],
            profile_contract(),
            "verification",
        )

        self.assertIn("duplicate record id", "\n".join(errors))

    def test_duplicate_selector_ids_within_one_scenario_fail(self) -> None:
        first = verification_record()
        second = verification_record(
            record_id="rectclip-crossing-verification-2",
        )

        errors = check.validate_profile_semantics(
            [first, second],
            profile_contract(),
            "verification",
        )

        self.assertIn("duplicate selector.full_id within scenario", "\n".join(errors))

    def test_missing_scenario_fails(self) -> None:
        record = verification_record()
        record.pop("scenario")

        errors = check.validate_profile_semantics(
            [record],
            profile_contract(),
            "verification",
        )

        self.assertIn("missing scenario", "\n".join(errors))

    def test_zero_delta_offset_is_forbidden(self) -> None:
        record = verification_record(
            "offset",
            scenario="zero-delta",
            operation="offset.polygon",
        )
        record["inputs"]["delta"] = 0.0
        contract = profile_contract(
            "offset",
            scenario_quotas={"zero-delta": ScenarioQuota(max_count=0)},
            forbidden_scenarios=("zero-delta",),
        )

        errors = check.validate_profile_semantics(
            [record],
            contract,
            "verification",
        )

        self.assertIn("zero-delta release case is forbidden", "\n".join(errors))

    def test_forged_tag_cannot_satisfy_option_quota(self) -> None:
        record = verification_record(
            "offset",
            scenario="negative-offset",
            operation="offset.polygon",
        )
        record["inputs"]["delta"] = -2.0
        record["tags"] = ["offset", "delta.positive"]
        record["canonical_input_digest"] = canonical_json_digest(record["inputs"])
        contract = profile_contract(
            "offset",
            scenario_quotas={"delta.positive": ScenarioQuota(min_count=1)},
        )

        errors = check.validate_profile_semantics(
            [record],
            contract,
            "verification",
        )

        self.assertIn(
            "quota delta.positive requires at least 1, found 0",
            "\n".join(errors),
        )

    def test_minkowski_single_point_operand_is_forbidden(self) -> None:
        record = verification_record(
            "minkowski",
            scenario="operation.sum",
            operation="minkowski.sum",
        )
        record["inputs"]["pattern_wkt"] = "POINT (0 0)"
        contract = profile_contract(
            "minkowski",
            scenario_quotas={"operation.sum": ScenarioQuota(min_count=1)},
        )

        errors = check.validate_profile_semantics(
            [record],
            contract,
            "verification",
        )

        self.assertIn(
            "both Minkowski operands must be non-trivial",
            "\n".join(errors),
        )

    def test_open_path_scenario_must_match_geometry_relation(self) -> None:
        record = verification_record(
            "open-path-overlay",
            scenario="contained",
            operation="overlay.intersection",
        )
        record["inputs"] = {
            "lhs_wkt": "LINESTRING (20 20, 30 30)",
            "rhs_wkt": "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))",
            "fill_rule": "non_zero",
        }
        record["canonical_input_digest"] = canonical_json_digest(record["inputs"])
        record["canonical_options_digest"] = canonical_json_digest(
            {"fill_rule": "non_zero"}
        )
        contract = profile_contract(
            "open-path-overlay",
            scenario_quotas={"contained": ScenarioQuota(min_count=1)},
        )

        errors = check.validate_profile_semantics(
            [record],
            contract,
            "verification",
        )

        self.assertIn(
            "contained open-path scenario has outside vertices",
            "\n".join(errors),
        )

    def test_open_path_bbox_classification_requires_rectangular_clip(self) -> None:
        record = verification_record(
            "open-path-overlay",
            scenario="contained",
            operation="overlay.intersection",
        )
        record["inputs"] = {
            "lhs_wkt": "LINESTRING (8 8, 9 9)",
            "rhs_wkt": "POLYGON ((0 0, 10 0, 0 10, 0 0))",
            "fill_rule": "non_zero",
        }
        record["canonical_input_digest"] = canonical_json_digest(record["inputs"])
        record["canonical_options_digest"] = canonical_json_digest(
            {"fill_rule": "non_zero"}
        )

        errors = check.validate_profile_semantics(
            [record],
            profile_contract(
                "open-path-overlay",
                scenario_quotas={"contained": ScenarioQuota(min_count=1)},
            ),
            "verification",
        )

        self.assertIn(
            "open-path scenario clip must be an axis-aligned rectangle",
            "\n".join(errors),
        )

    def test_batch_scenario_requires_legacy_scalar_next_scalar_and_batch_modes(
        self,
    ) -> None:
        record = verification_record(
            "batch",
            scenario="batch.scalar_next_legacy",
            operation="batch.clip",
        )
        contract = profile_contract(
            "batch",
            scenario_quotas={"batch.scalar_next_legacy": ScenarioQuota(min_count=1)},
        )

        errors = check.validate_profile_semantics(
            [record],
            contract,
            "verification",
        )

        self.assertIn(
            "batch comparison_modes must contain legacy_scalar, next_scalar, "
            "and next_batch",
            "\n".join(errors),
        )

    def test_scenario_quota_shortfall_fails(self) -> None:
        errors = check.validate_profile_semantics(
            [verification_record()],
            profile_contract(scenario_quotas={"crossing": ScenarioQuota(min_count=2)}),
            "verification",
        )

        self.assertIn("quota crossing requires at least 2", "\n".join(errors))

    def test_development_and_holdout_partition_overlap_fails(self) -> None:
        development = verification_record(partition="development")
        holdout = benchmark_twin(verification_record(partition="release-holdout"))

        errors = check.validate_partition_disjointness([development, holdout])

        self.assertIn(
            "appears in both development and release-holdout", "\n".join(errors)
        )

    def test_benchmark_without_identical_verification_twin_fails(self) -> None:
        verification = verification_record()
        benchmark = benchmark_twin(verification)
        benchmark["canonical_options_digest"] = "c" * 64

        errors = check.validate_twins([verification], [benchmark])

        self.assertIn(
            "benchmark record has no identical verification twin", "\n".join(errors)
        )

    def test_non_zero_coordinate_tolerance_fails(self) -> None:
        record = verification_record()
        record["tolerance"]["coordinate"] = 0.01

        errors = check.validate_profile_semantics(
            [record],
            profile_contract(),
            "verification",
        )

        self.assertIn("non-zero coordinate tolerance", "\n".join(errors))

    def test_missing_digest_reports_only_the_root_cause(self) -> None:
        record = verification_record()
        record.pop("canonical_input_digest")

        errors = check.validate_profile_semantics(
            [record],
            profile_contract(),
            "verification",
        )
        joined = "\n".join(errors)

        self.assertIn("missing or invalid canonical_input_digest", joined)
        self.assertNotIn("canonical_input_digest does not match inputs", joined)

    def test_cli_consumes_contract_and_emits_profile_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            verification = verification_record()
            benchmark = benchmark_twin(verification)
            write_jsonl(
                root / "normalized" / "verification" / "rectclip.jsonl",
                [verification],
            )
            write_jsonl(
                root / "normalized" / "benchmark" / "rectclip.jsonl",
                [benchmark],
            )
            contract_path = root / "contract.json"
            contract_path.write_text(
                json.dumps(minimal_contract_payload()),
                encoding="utf-8",
            )
            output_path = root / "profile-report.json"

            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    str(root),
                    "--contract",
                    str(contract_path),
                    "--output-json",
                    str(output_path),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(
                0,
                completed.returncode,
                completed.stdout + completed.stderr,
            )
            payload = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual("PASS", payload["status"])
            self.assertEqual(64, len(payload["contract_sha256"]))
            self.assertEqual(64, len(payload["profile_sha256"]))
            self.assertEqual(2, len(payload["profiles"]))


if __name__ == "__main__":
    unittest.main()
