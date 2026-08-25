#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_product_pascal_api_shape as scanner


class ProductPascalApiShapeScannerTests(unittest.TestCase):
    def test_detects_public_pascal_free_functions_members_and_parameters(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            root.mkdir(exist_ok=True)
            (root / "clipper.h").write_text(
                "auto Union(const Paths64& subjects, FillRule fill_rule) -> Paths64;\n",
                encoding="utf-8",
            )
            (root / "geometry_core.h").write_text(
                "auto CrossProduct(Point64 a, Point64 b) -> int64_t;\n",
                encoding="utf-8",
            )
            (root / "point.h").write_text(
                "struct Point64 { void SetZ(int64_t value); void Negate(); };\n",
                encoding="utf-8",
            )
            (root / "minkowski.h").write_text(
                "auto MinkowskiSum(const Path64& pattern, const Path64& path, bool isClosed) -> Paths64;\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_pascal_api_findings(root)

        categories = {finding.category for finding in findings}
        self.assertIn("pascal_free_function", categories)
        self.assertIn("predicate_names", categories)
        self.assertIn("pascal_member_function", categories)
        self.assertIn("pascal_parameter_name", categories)

    def test_ignores_internal_and_compat_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "internal").mkdir()
            (root / "compat").mkdir()
            (root / "internal" / "engine.h").write_text("auto Union() -> void;\n", encoding="utf-8")
            (root / "compat" / "clipper_legacy_api.h").write_text("auto Union() -> void;\n", encoding="utf-8")

            findings = scanner.find_product_pascal_api_findings(root)

        self.assertEqual([], findings)

    def test_category_filter(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "geometry_core.h").write_text(
                "auto PointInPolygon(Point64 point, const Path64& path) -> PointInPolygonResult;\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_pascal_api_findings(root, category="predicate_names")

        self.assertEqual(1, len(findings))
        self.assertEqual("predicate_names", findings[0].category)


if __name__ == "__main__":
    unittest.main()
