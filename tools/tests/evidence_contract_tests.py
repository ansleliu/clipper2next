#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from tools.release.evidence_contract import (
    load_contract,
    release_gated_profile_ids,
)


def minimal_contract() -> dict:
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
                "id": "overlay",
                "eligibility_file": "overlay-candidates.jsonl",
                "geometry_types": ["Polygon", "MultiPolygon"],
                "required_case_sets": ["verification", "benchmark"],
                "scenario_quotas": {
                    "overlap": {"min_count": 1},
                    "identity": {"max_count": 0},
                },
                "forbidden_scenarios": ["identity"],
            }
        ],
        "algorithms": [
            {
                "id": "clip_overlay",
                "release_gated": True,
                "required_tests": ["DifferentialClip.StrictLegacy"],
                "required_benchmarks": ["BM_external_overlay"],
                "required_profiles": ["overlay"],
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


@contextmanager
def temporary_contract(payload: dict) -> Iterator[Path]:
    with tempfile.TemporaryDirectory() as temp_dir:
        path = Path(temp_dir) / "contract.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        yield path


class EvidenceContractTests(unittest.TestCase):
    def test_repository_contract_enforces_strict_equivalence_and_release_thresholds(
        self,
    ) -> None:
        contract = load_contract()

        self.assertEqual(0, contract.equivalence["coordinate_tolerance"])
        self.assertTrue(contract.equivalence["normalize_closed_ring_start"])
        self.assertTrue(contract.equivalence["normalize_independent_path_order"])
        self.assertFalse(contract.equivalence["normalize_winding"])
        self.assertFalse(contract.equivalence["normalize_open_path_direction"])
        self.assertEqual(0.5, contract.performance["min_time_seconds"])
        self.assertEqual(1.0, contract.performance["min_warmup_time_seconds"])
        self.assertEqual(5.0, contract.performance["release_max_cv_percent"])
        self.assertEqual(1.2, contract.performance["min_pair_speedup"])
        self.assertEqual(1.2, contract.performance["min_geomean_speedup"])
        self.assertEqual("real_time", contract.performance["time_field"])
        self.assertEqual(64, len(contract.sha256))

    def test_repository_contract_keeps_every_public_family_explicit(self) -> None:
        contract = load_contract()

        self.assertEqual(
            {
                "batch_clip",
                "clip_overlay",
                "clip_tree_polytree",
                "geometry_algorithms",
                "minkowski",
                "offset_polygon",
                "open_path_clip",
                "rect_clip_lines",
                "rect_clip_polygon",
                "scaling_and_transforms",
                "triangulation",
            },
            set(contract.algorithms),
        )
        self.assertFalse(contract.algorithms["geometry_algorithms"].release_gated)
        self.assertFalse(contract.algorithms["scaling_and_transforms"].release_gated)
        gated_profiles = release_gated_profile_ids(contract)
        self.assertIn("overlay", gated_profiles)
        self.assertIn("rectclip-lines", gated_profiles)
        self.assertNotIn("bounds", gated_profiles)
        self.assertNotIn("scaling", gated_profiles)

    def test_repository_contract_covers_open_offset_and_option_combinations(
        self,
    ) -> None:
        contract = load_contract()

        self.assertEqual(
            {
                "Polygon",
                "MultiPolygon",
                "LineString",
                "MultiLineString",
            },
            set(contract.profiles["offset"].geometry_types),
        )
        for profile_id in ("overlay", "open-path-overlay", "clip-tree", "polytree"):
            quotas = contract.profiles[profile_id].scenario_quotas
            self.assertEqual(
                8,
                quotas["operation.intersection.fill_rule.even_odd"].min_count,
            )
            self.assertEqual(
                8,
                quotas["operation.xor.fill_rule.negative"].min_count,
            )
        self.assertGreaterEqual(
            contract.profiles["batch"]
            .scenario_quotas["batch.scalar_next_legacy"]
            .min_count,
            128,
        )

    def test_rejects_contract_that_normalizes_winding(self) -> None:
        payload = minimal_contract()
        payload["equivalence"]["normalize_winding"] = True

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(ValueError, "normalize_winding must be false"):
                load_contract(path)

    def test_rejects_contract_that_normalizes_open_path_direction(self) -> None:
        payload = minimal_contract()
        payload["equivalence"]["normalize_open_path_direction"] = True

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(
                ValueError, "normalize_open_path_direction must be false"
            ):
                load_contract(path)

    def test_rejects_nonzero_coordinate_tolerance(self) -> None:
        payload = minimal_contract()
        payload["equivalence"]["coordinate_tolerance"] = 1

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(
                ValueError, "coordinate_tolerance must be zero"
            ):
                load_contract(path)

    def test_rejects_release_variance_above_five_percent(self) -> None:
        payload = minimal_contract()
        payload["performance"]["release_max_cv_percent"] = 5.01

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(ValueError, "release_max_cv_percent"):
                load_contract(path)

    def test_rejects_release_timing_interval_below_half_a_second(self) -> None:
        payload = minimal_contract()
        payload["performance"]["min_time_seconds"] = 0.49

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(ValueError, "min_time_seconds"):
                load_contract(path)

    def test_rejects_release_warmup_below_one_second(self) -> None:
        payload = minimal_contract()
        payload["performance"]["min_warmup_time_seconds"] = 0.99

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(ValueError, "min_warmup_time_seconds"):
                load_contract(path)

    def test_rejects_pair_or_geomean_floor_below_one_point_two(self) -> None:
        for key in ("min_pair_speedup", "min_geomean_speedup"):
            payload = minimal_contract()
            payload["performance"][key] = 1.19

            with self.subTest(key=key), temporary_contract(payload) as path:
                with self.assertRaisesRegex(ValueError, key):
                    load_contract(path)

    def test_rejects_algorithm_profile_reference_not_owned_by_contract(self) -> None:
        payload = minimal_contract()
        payload["algorithms"][0]["required_profiles"] = ["missing-profile"]

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(ValueError, "unknown profile"):
                load_contract(path)

    def test_rejects_quota_whose_maximum_is_below_its_minimum(self) -> None:
        payload = copy.deepcopy(minimal_contract())
        payload["profiles"][0]["scenario_quotas"]["overlap"] = {
            "min_count": 2,
            "max_count": 1,
        }

        with temporary_contract(payload) as path:
            with self.assertRaisesRegex(ValueError, "max_count"):
                load_contract(path)


if __name__ == "__main__":
    unittest.main()
