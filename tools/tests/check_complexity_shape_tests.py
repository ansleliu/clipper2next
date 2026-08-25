#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_complexity_shape as scanner


class ComplexityShapeScannerTests(unittest.TestCase):
    def test_detects_large_files_functions_branch_depth_and_mixed_includes(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            include = root / "include" / "clipper2next"
            src = root / "src"
            include.mkdir(parents=True)
            src.mkdir()
            (include / "geometry_core.h").write_text("\n".join(["int x;"] * 5), encoding="utf-8")
            (src / "engine.cpp").write_text(
                "\n".join(
                    [
                        '#include "clipper2next/clipper.h"',
                        '#include "clip/engine/private/engine_types.h"',
                        "auto large() -> int {",
                        "  int value = 0;",
                        "  if (value) {",
                        "    if (value) {",
                        "      if (value) {",
                        "        if (value) {",
                        "          if (value) {",
                        "            if (value) { value++; }",
                        "          }",
                        "        }",
                        "      }",
                        "    }",
                        "  }",
                        "  value++;",
                        "  value++;",
                        "  value++;",
                        "  return value;",
                        "}",
                    ]
                ),
                encoding="utf-8",
            )

            findings = scanner.find_complexity_shape_findings(
                root,
                source_threshold=10,
                header_threshold=3,
                function_threshold=5,
                branch_threshold=3,
            )

        categories = {finding.category for finding in findings}
        self.assertIn("large_source_file", categories)
        self.assertIn("large_public_header", categories)
        self.assertIn("large_function", categories)
        self.assertIn("deep_branch_nesting", categories)
        self.assertIn("mixed_facade_internal_include", categories)

    def test_ignores_compat_and_tests(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            compat = root / "src" / "compat"
            tests = root / "tests"
            compat.mkdir(parents=True)
            tests.mkdir()
            (compat / "legacy.cpp").write_text("\n".join(["int x;"] * 20), encoding="utf-8")
            (tests / "large_tests.cpp").write_text("\n".join(["int x;"] * 20), encoding="utf-8")

            findings = scanner.find_complexity_shape_findings(root, source_threshold=1)

        self.assertEqual([], findings)

    def test_category_filter(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            src = root / "src"
            src.mkdir()
            (src / "geometry.cpp").write_text("\n".join(["int x;"] * 20), encoding="utf-8")

            findings = scanner.find_complexity_shape_findings(
                root,
                category="large_source_file",
                source_threshold=1,
            )

        self.assertEqual(1, len(findings))
        self.assertEqual("large_source_file", findings[0].category)

    def test_sequential_branches_do_not_accumulate_as_nested_depth(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            src = root / "src"
            src.mkdir()
            (src / "sequential.cpp").write_text(
                "\n".join(
                    [
                        "auto f(int value) -> int {",
                        "  if (value == 1) { return 1; }",
                        "  if (value == 2) { return 2; }",
                        "  if (value == 3) { return 3; }",
                        "  if (value == 4) { return 4; }",
                        "  return 0;",
                        "}",
                    ]
                ),
                encoding="utf-8",
            )

            findings = scanner.find_complexity_shape_findings(
                root,
                category="deep_branch_nesting",
                branch_threshold=2,
            )

        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()
