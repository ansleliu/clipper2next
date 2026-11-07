import json
import unittest
from pathlib import Path


class PublicTriangulationTests(unittest.TestCase):
    def test_frozen_fixture_counts_areas_and_modes(self):
        root = Path(__file__).resolve().parents[3]
        fixture = root / "benchmarks/public_triangulation/fixtures.json"
        self.assertTrue(fixture.is_file(), "matched public fixture must be materialized before timing")
        from benchmarks.tools.common.public_triangulation import validate_fixtures, supplement_rows
        cases = validate_fixtures(json.loads(fixture.read_text(encoding="utf-8")))
        self.assertEqual(len(cases), 10)
        self.assertEqual([c["point_count"] for c in cases[:3]], [512, 2048, 4096])
        self.assertEqual(len(supplement_rows(cases)), 40)


if __name__ == "__main__":
    unittest.main()
