#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_api_style_shape as scanner


class ApiStyleShapeScannerTests(unittest.TestCase):
    def test_detects_legacy_product_names_pascal_methods_and_mutable_status(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            header = root / "engine.h"
            header.write_text(
                "\n".join(
                    [
                        "class Clipper64 {",
                        "public:",
                        "  int64_t DefaultZ;",
                        "  int ErrorCode() const;",
                        "  void AddPath(const Path64& path);",
                        "  void PreserveCollinear(bool value);",
                        "};",
                    ]
                ),
                encoding="utf-8",
            )

            findings = scanner.find_api_style_findings(root)

        categories = {finding.category for finding in findings}
        self.assertIn("legacy_product_type", categories)
        self.assertIn("pascal_product_method", categories)
        self.assertIn("mutable_public_state", categories)
        self.assertIn("int_error_api", categories)

    def test_ignores_legacy_names_in_compat_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            compat = root / "compat" / "clipper_legacy_api.h"
            compat.parent.mkdir()
            compat.write_text("class Clipper64 { public: int ErrorCode() const; };\n", encoding="utf-8")

            findings = scanner.find_api_style_findings(root)

        self.assertEqual([], findings)

    def test_can_report_compat_legacy_names_when_requested(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            compat = root / "compat" / "clipper_legacy_api.h"
            compat.parent.mkdir()
            compat.write_text("class ClipperOffset { public: void Execute(); };\n", encoding="utf-8")

            findings = scanner.find_api_style_findings(root, include_compat=True)

        self.assertEqual(2, len(findings))
        self.assertTrue(all(finding.category == "compat_legacy_name" for finding in findings))


if __name__ == "__main__":
    unittest.main()
