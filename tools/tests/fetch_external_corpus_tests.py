#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.corpus import fetch_external_corpus as fetcher


class FetchExternalCorpusTests(unittest.TestCase):
    def test_corpus_root_uses_external_geometry_root_by_default(self) -> None:
        with mock.patch.dict(
            "os.environ", {fetcher.GEOMETRY_CORPUS_ROOT_ENV: "/data/geometry"}
        ):
            self.assertEqual(
                Path("/data/geometry") / "legacy_external_sources",
                fetcher.corpus_root(),
            )

    def test_corpus_root_requires_external_root_or_explicit_root(self) -> None:
        with mock.patch.dict("os.environ", {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "external_sources has been retired"):
                fetcher.corpus_root()

            explicit_root = Path("custom") / "legacy_sources"
            self.assertEqual(explicit_root, fetcher.corpus_root(explicit_root))

    def test_extended_wkt_cases_include_operation_and_shape_mix(self) -> None:
        geometry = fetcher.Geometry(
            rings=[
                [
                    (0.0, 0.0),
                    (10.0, 0.0),
                    (10.0, 10.0),
                    (0.0, 10.0),
                    (0.0, 0.0),
                ]
            ]
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "extended.tsv"
            count = fetcher.write_extended_wkt_cases(
                output,
                "sample",
                "unit",
                [geometry],
                max_base_geometries=1,
                max_points=100,
            )
            rows = [
                line.strip().split("\t")
                for line in output.read_text(encoding="utf-8").splitlines()
                if line and not line.startswith("#")
            ]

        operations = {row[1] for row in rows}
        names = {row[0] for row in rows}
        self.assertEqual(count, len(rows))
        self.assertTrue({"intersection", "union", "difference", "xor"}.issubset(operations))
        self.assertTrue(any("partial_rectangle" in name for name in names))
        self.assertTrue(any("non_rectangle" in name for name in names))
        self.assertTrue(any("multipath" in name for name in names))

    def test_shp_record_count_counts_non_polygon_sources(self) -> None:
        header = bytes(100)
        record_header = (1).to_bytes(4, "big", signed=True) + (2).to_bytes(4, "big", signed=True)
        record_content = (3).to_bytes(4, "little", signed=True)

        self.assertEqual(1, fetcher.count_shp_records(header + record_header + record_content))

    def test_line_wkt_cases_include_scaled_rect_and_open_line(self) -> None:
        geometry = fetcher.LineGeometry(paths=[[(0.0, 0.0), (10.0, 10.0), (20.0, 0.0)]])

        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "line_wkt.tsv"
            count = fetcher.write_line_wkt_cases(
                output,
                "sample_lines",
                "unit",
                [geometry],
                max_cases=1,
                max_points=100,
            )
            rows = [
                line.strip().split("\t")
                for line in output.read_text(encoding="utf-8").splitlines()
                if line and not line.startswith("#")
            ]

        self.assertEqual(1, count)
        self.assertEqual(1, len(rows))
        self.assertEqual("sample_lines/unit/0", rows[0][0])
        self.assertEqual("1000000", rows[0][1])
        self.assertEqual("LINESTRING (0 0, 10 10, 20 0)", rows[0][6])


if __name__ == "__main__":
    unittest.main()
