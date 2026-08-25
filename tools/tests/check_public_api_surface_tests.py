#!/usr/bin/env python3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.checks import check_public_api_surface as scanner


def write_stable_headers(root: Path) -> None:
    include_root = root / "include"
    for header in scanner.STABLE_PUBLIC_HEADERS:
        path = include_root / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#pragma once\n", encoding="utf-8")


class PublicApiSurfaceTests(unittest.TestCase):
    def test_multiline_parameter_type_changes_surface(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_stable_headers(root)
            public_header = root / "include" / "clipper2next" / "clip.h"
            public_header.write_text(
                """
                #pragma once
                namespace clipper2next {
                [[nodiscard]] auto function(
                    const int& value,
                    long mode) -> int;
                }
                """,
                encoding="utf-8",
            )
            before = scanner.collect_public_api_surface(root)
            public_header.write_text(
                """
                #pragma once
                namespace clipper2next {
                [[nodiscard]] auto function(
                    const double& value,
                    long mode) -> int;
                }
                """,
                encoding="utf-8",
            )
            after = scanner.collect_public_api_surface(root)

        self.assertNotEqual(before, after)

    def test_crlf_normalization_is_platform_independent(self) -> None:
        lf = "#pragma once\nnamespace clipper2next {\n}\n"
        crlf = lf.replace("\n", "\r\n")

        self.assertEqual(scanner.hash_header_content(lf), scanner.hash_header_content(crlf))

    def test_geotypes_coordinate_contract_is_part_of_stable_surface(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_stable_headers(root)
            surface = scanner.collect_public_api_surface(root)

        self.assertTrue(any("geotypes/coordinate.hpp" in item for item in surface))

    def test_cli_fails_when_stable_header_differs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_stable_headers(root)
            baseline = root / "baseline.txt"
            baseline.write_text(
                scanner.baseline_text(scanner.collect_public_api_surface(root)),
                encoding="utf-8",
            )
            public_header = root / "include" / "clipper2next" / "clip.h"
            public_header.write_text(
                "#pragma once\nnamespace clipper2next { auto clip() -> int; }\n",
                encoding="utf-8",
            )

            completed = subprocess.run(
                [
                    sys.executable,
                    str(Path(scanner.__file__).resolve()),
                    "--root",
                    str(root),
                    "--baseline",
                    "baseline.txt",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

        self.assertEqual(1, completed.returncode)
        self.assertIn("-clipper2next/clip.h: sha256=", completed.stdout)
        self.assertIn("+clipper2next/clip.h: sha256=", completed.stdout)


if __name__ == "__main__":
    unittest.main()
