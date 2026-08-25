#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_public_value_object_shape as scanner


class PublicValueObjectShapeScannerTests(unittest.TestCase):
    def test_detects_value_object_legacy_shapes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "point.h").write_text("struct Point64 { void Init(); void SetZ(int64_t z); };\n", encoding="utf-8")
            (root / "rect.h").write_text("struct Rect64 { void Width(int64_t value); Path64 AsPath() const; };\n", encoding="utf-8")
            (root / "poly_tree.h").write_text(
                "class PolyPath { public: virtual PolyPath* AddChild(Path64 path); int Count() const; };\n",
                encoding="utf-8",
            )
            (root / "path.h").write_text("auto operator<<(Path64& path, Point64 point) -> Path64&;\n", encoding="utf-8")

            findings = scanner.find_public_value_object_shape_findings(root)

        categories = {finding.category for finding in findings}
        self.assertIn("point_mutating_member", categories)
        self.assertIn("rect_legacy_member", categories)
        self.assertIn("polytree_legacy_shape", categories)
        self.assertIn("path_append_operator", categories)

    def test_ignores_internal_and_compat_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "internal").mkdir()
            (root / "compat").mkdir()
            (root / "internal" / "point.h").write_text("void SetZ();\n", encoding="utf-8")
            (root / "compat" / "point.h").write_text("void SetZ();\n", encoding="utf-8")

            findings = scanner.find_public_value_object_shape_findings(root)

        self.assertEqual([], findings)

    def test_category_filter(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "rect.h").write_text("struct Rect64 { bool IsValid() const; };\n", encoding="utf-8")

            findings = scanner.find_public_value_object_shape_findings(root, category="rect_legacy_member")

        self.assertEqual(1, len(findings))
        self.assertEqual("rect_legacy_member", findings[0].category)


if __name__ == "__main__":
    unittest.main()
