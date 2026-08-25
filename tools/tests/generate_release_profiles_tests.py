#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.checks import check_geometry_corpus_profiles as check  # noqa: E402
from tools.corpus import generate_release_profiles as generator  # noqa: E402
from tools.release.evidence_contract import (  # noqa: E402
    DEFAULT_CONTRACT_PATH,
    ProfileContract,
    ScenarioQuota,
    load_contract,
    release_gated_profile_ids,
)


def write_jsonl(path: Path, records: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(
            json.dumps(
                record,
                allow_nan=False,
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
            )
            + "\n"
            for record in records
        ),
        encoding="utf-8",
    )


def polygon_wkt(offset: int) -> str:
    return (
        "POLYGON (("
        f"{offset} 0, {offset + 20} 0, {offset + 20} 20, "
        f"{offset} 20, {offset} 0"
        "), ("
        f"{offset + 5} 5, {offset + 5} 15, {offset + 15} 15, "
        f"{offset + 15} 5, {offset + 5} 5"
        "))"
    )


def line_wkt(offset: int) -> str:
    return (
        f"LINESTRING ({offset} 0, {offset + 5} 15, "
        f"{offset + 10} -5, {offset + 20} 20)"
    )


def high_polygon_wkt(offset: int, point_count: int = 520) -> str:
    points = []
    for index in range(point_count):
        angle = (2.0 * math.pi * index) / point_count
        radius = 40_000 if index % 2 == 0 else 27_000
        points.append(
            (
                offset + int(round(radius * math.cos(angle))),
                int(round(radius * math.sin(angle))),
            )
        )
    points.append(points[0])
    return "POLYGON ((" + ", ".join(f"{x} {y}" for x, y in points) + "))"


def high_line_wkt(offset: int, point_count: int = 520) -> str:
    return "LINESTRING (" + ", ".join(
        f"{offset + index * 10} {1000 if index % 2 else -1000}"
        for index in range(point_count)
    ) + ")"


def overlay_eligibility(count: int = 320) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    operations = ("intersection", "union", "difference", "symmetric_difference")
    for index in range(count):
        offset = index * 100
        lhs = polygon_wkt(offset)
        rhs = polygon_wkt(offset + 10)
        records.append(
            {
                "id": f"overlay-{index:04d}",
                "record_type": "overlay-candidate",
                "profile": "full",
                "operation": operations[index % len(operations)],
                "inputs": {
                    "lhs": {
                        "geometry_type": "Polygon",
                        "point_count": 8,
                        "ring_count": 2,
                        "wkt": lhs,
                    },
                    "rhs": {
                        "geometry_type": "Polygon",
                        "point_count": 8,
                        "ring_count": 2,
                        "wkt": rhs,
                    },
                },
                "source": {
                    "source_id": f"source-{index % 7}",
                    "upstream_id": f"fixture/overlay/{index}",
                    "license_id": "Test",
                },
                "status": "normalized",
                "tags": ["fixture", "polygonal", "holes"],
            }
        )
    return records


def shape_eligibility(count_per_type: int = 320) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for index in range(count_per_type):
        offset = index * 100
        polygon = high_polygon_wkt(offset) if index < 40 else polygon_wkt(offset)
        line = high_line_wkt(offset) if index < 40 else line_wkt(offset)
        records.append(
            {
                "id": f"polygon-{index:04d}",
                "record_type": "geometry",
                "profile": "full",
                "geometry": {
                    "geometry_type": "Polygon",
                    "point_count": 520 if index < 40 else 8,
                    "ring_count": 1 if index < 40 else 2,
                    "wkt": polygon,
                },
                "source": {
                    "source_id": f"source-{index % 11}",
                    "upstream_id": f"fixture/polygon/{index}",
                    "license_id": "Test",
                },
                "status": "normalized",
                "tags": ["fixture", "polygonal", "holes"],
            }
        )
        records.append(
            {
                "id": f"line-{index:04d}",
                "record_type": "geometry",
                "profile": "full",
                "geometry": {
                    "geometry_type": "LineString",
                    "point_count": 520 if index < 40 else 4,
                    "ring_count": 0,
                    "wkt": line,
                },
                "source": {
                    "source_id": f"source-{index % 11}",
                    "upstream_id": f"fixture/line/{index}",
                    "license_id": "Test",
                },
                "status": "normalized",
                "tags": ["fixture", "linear"],
            }
        )
    return records


def populate_full_fixture(root: Path) -> None:
    full = root / "normalized" / "full"
    write_jsonl(full / "overlay-candidates.jsonl", overlay_eligibility())
    write_jsonl(full / "shape-inputs.jsonl", shape_eligibility())


def load_generated(path: Path, case_set: str) -> list[dict[str, object]]:
    records, errors = check.load_profile(path, case_set)
    if errors:
        raise AssertionError("\n".join(errors))
    return records


class ReleaseProfileGeneratorTests(unittest.TestCase):
    def test_partition_is_stable_and_hash_owned(self) -> None:
        self.assertEqual("release-holdout", generator.stable_partition("shape-0"))
        self.assertEqual("development", generator.stable_partition("shape-a"))
        self.assertEqual(
            generator.stable_partition("stable-case"),
            generator.stable_partition("stable-case"),
        )

    def test_record_tags_treats_missing_source_tags_as_empty(self) -> None:
        tags = generator._record_tags(
            {"id": "shape-without-tags"},
            "rectclip",
            "crossing",
            "polygonal",
        )

        self.assertEqual(
            ("crossing", "polygonal", "rectclip"),
            tags,
        )

    def test_duplicate_eligibility_id_fails_before_writing_profiles(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            full = root / "input" / "normalized" / "full"
            duplicate = overlay_eligibility(1)[0]
            write_jsonl(
                full / "overlay-candidates.jsonl",
                [duplicate, duplicate],
            )
            write_jsonl(full / "shape-inputs.jsonl", shape_eligibility())

            with self.assertRaisesRegex(
                generator.ProfileGenerationError,
                "duplicate eligibility id 'overlay-0000'",
            ):
                generator.generate_release_profiles(
                    root / "input",
                    root / "output",
                    load_contract(DEFAULT_CONTRACT_PATH),
                )

            self.assertFalse((root / "output" / "normalized").exists())

    def test_minkowski_eligibility_requires_a_nontrivial_executable_first_path(
        self,
    ) -> None:
        degenerate = shape_eligibility(1)[0]
        degenerate["geometry"] = {
            "geometry_type": "Polygon",
            "point_count": 7,
            "ring_count": 2,
            "wkt": (
                "POLYGON ((0 0, 0 0, 0 0, 0 0), "
                "(1 1, 5 1, 1 5, 1 1))"
            ),
        }
        profile = ProfileContract(
            id="minkowski",
            eligibility_file="shape-inputs.jsonl",
            geometry_types=("Polygon", "MultiPolygon"),
            required_case_sets=("verification", "benchmark"),
            scenario_quotas={
                "operation.sum": ScenarioQuota(min_count=1),
                "operation.difference": ScenarioQuota(min_count=1),
                "path.closed": ScenarioQuota(min_count=1),
                "path.open": ScenarioQuota(min_count=1),
                "identity": ScenarioQuota(max_count=0),
            },
            forbidden_scenarios=("identity",),
        )

        self.assertFalse(generator.is_minkowski_eligible(degenerate, profile))

    def test_official_contract_generation_is_deterministic_and_semantically_valid(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            input_root = root / "input"
            first_output = root / "first"
            second_output = root / "second"
            populate_full_fixture(input_root)
            contract = load_contract(DEFAULT_CONTRACT_PATH)

            first = generator.generate_release_profiles(
                input_root,
                first_output,
                contract,
            )
            second = generator.generate_release_profiles(
                input_root,
                second_output,
                contract,
            )

            self.assertEqual(first.profile_counts, second.profile_counts)
            first_files = sorted(
                path.relative_to(first_output)
                for path in first_output.rglob("*.jsonl")
            )
            second_files = sorted(
                path.relative_to(second_output)
                for path in second_output.rglob("*.jsonl")
            )
            self.assertEqual(first_files, second_files)
            for relative_path in first_files:
                self.assertEqual(
                    (first_output / relative_path).read_bytes(),
                    (second_output / relative_path).read_bytes(),
                    str(relative_path),
                )

            all_records: list[dict[str, object]] = []
            for profile_id in sorted(release_gated_profile_ids(contract)):
                profile = contract.profiles[profile_id]
                verification = load_generated(
                    first_output
                    / "normalized"
                    / "verification"
                    / f"{profile_id}.jsonl",
                    "verification",
                )
                benchmark = load_generated(
                    first_output
                    / "normalized"
                    / "benchmark"
                    / f"{profile_id}.jsonl",
                    "benchmark",
                )
                errors = check.validate_profile_semantics(
                    verification,
                    profile,
                    "verification",
                )
                errors.extend(
                    check.validate_profile_semantics(
                        benchmark,
                        profile,
                        "benchmark",
                    )
                )
                errors.extend(check.validate_twins(verification, benchmark))
                self.assertEqual([], errors, f"{profile_id}:\n" + "\n".join(errors))
                all_records.extend(verification)
                all_records.extend(benchmark)

            self.assertEqual(
                [],
                check.validate_partition_disjointness(all_records),
            )

            offset = load_generated(
                first_output / "normalized" / "verification" / "offset.jsonl",
                "verification",
            )
            self.assertNotIn(0.0, {record["inputs"]["delta"] for record in offset})
            self.assertEqual(
                {"miter", "round", "square"},
                {record["inputs"]["join_type"] for record in offset},
            )
            self.assertEqual(
                {"polygon", "joined", "butt", "square", "round"},
                {record["inputs"]["end_type"] for record in offset},
            )
            self.assertEqual(
                {False, True},
                {record["inputs"]["preserve_collinear"] for record in offset},
            )
            self.assertEqual(
                {False, True},
                {record["inputs"]["reverse_solution"] for record in offset},
            )

            minkowski = load_generated(
                first_output / "normalized" / "verification" / "minkowski.jsonl",
                "verification",
            )
            self.assertEqual(
                {"minkowski.sum", "minkowski.difference"},
                {record["operation"] for record in minkowski},
            )
            self.assertEqual(
                {False, True},
                {record["inputs"]["is_closed"] for record in minkowski},
            )

            batch = load_generated(
                first_output / "normalized" / "verification" / "batch.jsonl",
                "verification",
            )
            self.assertTrue(
                all(
                    record["inputs"]["comparison_modes"]
                    == ["legacy_scalar", "next_scalar", "next_batch"]
                    for record in batch
                )
            )
            self.assertTrue(
                all(len(record["inputs"]["requests"]) >= 4 for record in batch)
            )


if __name__ == "__main__":
    unittest.main()
