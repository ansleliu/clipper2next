#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.corpus import repair_normalized_full_ids as repair  # noqa: E402


def record(
    record_id: str,
    upstream_id: str,
    *,
    point_count: int,
) -> dict[str, object]:
    return {
        "id": record_id,
        "geometry": {
            "geometry_type": "Polygon",
            "point_count": point_count,
            "wkt": (
                "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))"
                if point_count == 4
                else "POLYGON ((0 0, 5 0, 10 0, 10 5, 10 10, 5 10, 0 10, 0 0))"
            ),
        },
        "source": {
            "source_id": "geos",
            "upstream_id": upstream_id,
        },
        "status": "normalized",
    }


class NormalizedFullIdRepairTests(unittest.TestCase):
    def test_distinct_source_records_receive_stable_unambiguous_ids(self) -> None:
        records = [
            record(
                "geos_TestRelatePA_0004_b",
                "sources/geos/general/TestRelatePA.xml",
                point_count=4,
            ),
            record(
                "geos_TestRelatePA_0004_b",
                "sources/geos/validate/TestRelatePA.xml",
                point_count=8,
            ),
        ]

        first = repair.repair_colliding_ids(copy.deepcopy(records))
        second = repair.repair_colliding_ids(copy.deepcopy(records))

        self.assertEqual(first.records, second.records)
        self.assertEqual(2, len(first.renames))
        ids = [item["id"] for item in first.records]
        self.assertEqual(2, len(set(ids)))
        self.assertTrue(
            all(
                value.startswith("geos_TestRelatePA_0004_b--source-")
                for value in ids
            )
        )

    def test_exact_duplicate_cannot_be_repaired_by_identity_suffix(self) -> None:
        duplicate = record(
            "duplicate",
            "sources/geos/general/TestRelatePA.xml",
            point_count=4,
        )

        with self.assertRaisesRegex(
            repair.NormalizedIdRepairError,
            "remain ambiguous",
        ):
            repair.repair_colliding_ids([duplicate, copy.deepcopy(duplicate)])

    def test_cli_rewrites_atomically_and_second_run_is_a_noop(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "shape-inputs.jsonl"
            records = [
                record("collision", "source/a.xml", point_count=4),
                record("collision", "source/b.xml", point_count=8),
                record("unique", "source/c.xml", point_count=4),
            ]
            path.write_text(
                "".join(
                    json.dumps(item, separators=(",", ":"), sort_keys=True) + "\n"
                    for item in records
                ),
                encoding="utf-8",
            )

            first = repair.repair_file(path)
            first_bytes = path.read_bytes()
            second = repair.repair_file(path)

            self.assertEqual(2, len(first.renames))
            self.assertEqual(0, len(second.renames))
            self.assertEqual(first_bytes, path.read_bytes())
            loaded = [
                json.loads(line)
                for line in path.read_text(encoding="utf-8").splitlines()
            ]
            self.assertEqual(3, len({item["id"] for item in loaded}))


if __name__ == "__main__":
    unittest.main()
