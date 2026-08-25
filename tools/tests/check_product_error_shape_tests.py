#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_product_error_shape as scanner


class ProductErrorShapeScannerTests(unittest.TestCase):
    def test_detects_legacy_error_shapes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            include = root / "include" / "clipper2next"
            src = root / "src"
            include.mkdir(parents=True)
            src.mkdir()
            (include / "error.h").write_text(
                "constexpr int precision_error_i = 1;\ninline void DoError(int code) {}\n",
                encoding="utf-8",
            )
            (include / "scale.h").write_text(
                "void CheckPrecisionRange(int precision, int& error_code);\n",
                encoding="utf-8",
            )
            (src / "engine_facade.cpp").write_text(
                "int Clipper64::ErrorCode() const { return base_.MutableErrorCode(); }\n",
                encoding="utf-8",
            )
            (src / "requests.cpp").write_text(
                "auto mask = static_cast<int>(clipper_error::precision) | code;\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_error_shape_findings(root)

        categories = {finding.category for finding in findings}
        self.assertIn("int_error_reference", categories)
        self.assertIn("legacy_error_constant", categories)
        self.assertIn("do_error_call", categories)
        self.assertIn("legacy_error_accessor", categories)
        self.assertIn("typed_error_bitmask_bridge", categories)

    def test_ignores_compat_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            compat = root / "include" / "clipper2next" / "compat"
            compat.mkdir(parents=True)
            (compat / "clipper_legacy_api.h").write_text(
                "int Clipper64::ErrorCode() const;\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_error_shape_findings(root)

        self.assertEqual([], findings)

    def test_category_filter(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            include = root / "include" / "clipper2next"
            include.mkdir(parents=True)
            (include / "scale.h").write_text(
                "void ScalePaths(int& error_code);\nconstexpr int range_error_i = 1;\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_error_shape_findings(root, category="legacy_error_constant")

        self.assertEqual(1, len(findings))
        self.assertEqual("legacy_error_constant", findings[0].category)

    def test_typed_error_comparisons_joined_by_logical_or_are_not_bitmask_bridges(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            src = root / "src"
            src.mkdir()
            (src / "request.cpp").write_text(
                "if (error != clipper_error_code::ok || other_error != clipper_error_code::ok) {}\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_error_shape_findings(root)

        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()
