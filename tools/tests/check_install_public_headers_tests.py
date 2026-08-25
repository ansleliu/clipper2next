#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_install_public_headers as scanner


def write_installed_headers(root: Path) -> None:
    include_root = root / "include"
    for header in scanner.INSTALLED_HEADERS:
        path = include_root / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#pragma once\n", encoding="utf-8")


class InstallPublicHeadersTests(unittest.TestCase):
    def test_stable_and_support_header_sets_are_explicit_and_disjoint(self) -> None:
        self.assertFalse(
            set(scanner.STABLE_PUBLIC_HEADERS) & set(scanner.INSTALLED_SUPPORT_HEADERS)
        )
        self.assertEqual([], scanner.INSTALLED_SUPPORT_HEADERS)
        self.assertIn(
            "clipper2next/geotypes/coordinate.hpp",
            scanner.STABLE_PUBLIC_HEADERS,
        )

    def test_ignores_non_clipper2next_headers_in_shared_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_installed_headers(root)
            for header in ("gtest/gtest.h", "clipper2/clipper.h"):
                path = root / "include" / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n", encoding="utf-8")

            findings = scanner.find_install_public_header_findings(root)

        self.assertEqual([], findings)

    def test_detects_missing_stable_header(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_installed_headers(root)
            stable = root / "include" / "clipper2next" / "core" / "path_set.h"
            stable.unlink()

            findings = scanner.find_install_public_header_findings(root)

        self.assertTrue(any("missing required installed header" in item for item in findings))

    def test_detects_unexpected_clipper2next_public_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_installed_headers(root)
            extra = root / "include" / "clipper2next" / "internal" / "detail.h"
            extra.parent.mkdir(parents=True, exist_ok=True)
            extra.write_text("#pragma once\n", encoding="utf-8")

            findings = scanner.find_install_public_header_findings(root)

        self.assertTrue(any("unexpected installed public header" in item for item in findings))
        self.assertTrue(any("internal headers installed unexpectedly" in item for item in findings))


if __name__ == "__main__":
    unittest.main()
