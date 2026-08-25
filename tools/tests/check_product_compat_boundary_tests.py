#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_product_compat_boundary as scanner


class ProductCompatBoundaryScannerTests(unittest.TestCase):
    def test_detects_product_target_compat_source_and_calls(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                "\n".join(
                    [
                        "add_library(clipper2next)",
                        "target_sources(clipper2next PRIVATE",
                        "  src/clipper_facade.cpp",
                        "  src/offset_algorithm.cpp",
                        "  include/offset/private/offset_cleanup.h",
                        ")",
                    ]
                ),
                encoding="utf-8",
            )
            src = root / "src"
            include = root / "include" / "clipper2next" / "internal"
            src.mkdir(parents=True)
            include.mkdir(parents=True)
            (src / "clipper_facade.cpp").write_text(
                '#include "clipper2next/compat/clipper_legacy_api.h"\n'
                "void run() { Clipper64 clipper; clipper.AddSubject({}); clipper.Execute(type, fill, result); }\n",
                encoding="utf-8",
            )
            (src / "offset_algorithm.cpp").write_text(
                "void run() { ClipperOffset offset; offset.AddPaths(paths, join, end); }\n",
                encoding="utf-8",
            )
            (include / "offset_cleanup.h").write_text(
                "inline void clean(Clipper64& clipper) { clipper.ReverseSolution(true); }\n",
                encoding="utf-8",
            )

            findings = scanner.find_product_compat_boundary_findings(root, cmake)

        categories = {finding.category for finding in findings}
        self.assertIn("product_target_sources", categories)
        self.assertIn("compat_include_in_product_source", categories)
        self.assertIn("legacy_type_in_product_source", categories)
        self.assertIn("legacy_product_calls", categories)

    def test_detects_product_compat_target_overlap(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                "\n".join(
                    [
                        "add_library(clipper2next)",
                        "target_sources(clipper2next PRIVATE src/engine.cpp)",
                        "add_library(clipper2next_compat)",
                        "target_sources(clipper2next_compat PRIVATE src/engine.cpp)",
                    ]
                ),
                encoding="utf-8",
            )
            (root / "src").mkdir()
            (root / "src" / "engine.cpp").write_text("", encoding="utf-8")

            findings = scanner.find_product_compat_boundary_findings(root, cmake, category="target_overlap")

        self.assertEqual(1, len(findings))
        self.assertEqual("target_overlap", findings[0].category)

    def test_clean_product_sources_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                "add_library(clipper2next)\ntarget_sources(clipper2next PRIVATE src/requests.cpp)\n",
                encoding="utf-8",
            )
            (root / "src").mkdir()
            (root / "src" / "requests.cpp").write_text(
                '#include "clipper2next/clip/request.h"\nvoid run() {}\n',
                encoding="utf-8",
            )

            findings = scanner.find_product_compat_boundary_findings(root, cmake)

        self.assertEqual([], findings)

    def test_strict_product_rejects_in_tree_compat_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                "add_library(clipper2next)\ntarget_sources(clipper2next PRIVATE src/requests.cpp)\n",
                encoding="utf-8",
            )
            (root / "src").mkdir()
            (root / "src" / "requests.cpp").write_text("", encoding="utf-8")
            compat_header_dir = root / "include" / "clipper2next" / "compat"
            compat_source_dir = root / "src" / "compat"
            compat_header_dir.mkdir(parents=True)
            compat_source_dir.mkdir(parents=True)
            (compat_header_dir / "clipper_legacy_api.h").write_text("", encoding="utf-8")
            (compat_source_dir / "clipper_legacy_functions.cpp").write_text("", encoding="utf-8")

            findings = scanner.find_product_compat_boundary_findings(root, cmake, strict_product=True)

        categories = {finding.category for finding in findings}
        self.assertIn("compat_tree_in_product_root", categories)

    def test_strict_product_rejects_in_tree_compat_tests(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                "add_library(clipper2next)\ntarget_sources(clipper2next PRIVATE src/requests.cpp)\n",
                encoding="utf-8",
            )
            (root / "src").mkdir()
            (root / "src" / "requests.cpp").write_text("", encoding="utf-8")
            tests = root / "tests"
            smoke = tests / "compat_install_smoke_project"
            smoke.mkdir(parents=True)
            (tests / "compat_header_tests.cpp").write_text("", encoding="utf-8")
            (smoke / "main.cpp").write_text("", encoding="utf-8")

            findings = scanner.find_product_compat_boundary_findings(root, cmake, strict_product=True)

        categories = {finding.category for finding in findings}
        self.assertIn("compat_tests_in_product_root", categories)

    def test_strict_product_rejects_sibling_compat_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            product = root / "clipper2next"
            product.mkdir()
            cmake = product / "CMakeLists.txt"
            cmake.write_text(
                "add_library(clipper2next)\ntarget_sources(clipper2next PRIVATE src/requests.cpp)\n",
                encoding="utf-8",
            )
            (product / "src").mkdir()
            (product / "src" / "requests.cpp").write_text("", encoding="utf-8")
            sibling = root / "clipper2next_compat"
            sibling.mkdir()
            (sibling / "README.md").write_text("", encoding="utf-8")

            findings = scanner.find_product_compat_boundary_findings(root, cmake, strict_product=True)

        categories = {finding.category for finding in findings}
        self.assertIn("sibling_compat_tree", categories)


if __name__ == "__main__":
    unittest.main()
