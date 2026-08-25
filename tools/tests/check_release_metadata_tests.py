#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.checks import check_release_metadata as check


SCRIPT = Path(check.__file__).resolve()


def write_minimal_project(
    root: Path,
    *,
    cmake_version: str = "2.0.1",
    conan_version: str = "2.0.1",
) -> None:
    (root / "include" / "clipper2next").mkdir(parents=True)
    (root / "include" / "clipper2next" / "version.h").write_text(
        '#pragma once\nconstexpr auto CLIPPER2NEXT_VERSION = "2.0.1";\n',
        encoding="utf-8",
    )
    (root / "CMakeLists.txt").write_text(
        f"project(clipper2next VERSION {cmake_version} LANGUAGES CXX)\n",
        encoding="utf-8",
    )
    (root / "vcpkg.json").write_text(
        '{"name":"clipper2next","version-string":"2.0.1"}\n',
        encoding="utf-8",
    )
    (root / "conanfile.py").write_text(
        "from conan import ConanFile\n\n"
        "class Recipe(ConanFile):\n"
        "    name = \"clipper2next\"\n"
        f"    version = \"{conan_version}\"\n",
        encoding="utf-8",
    )


class ReleaseMetadataCheckTests(unittest.TestCase):
    def test_matching_versions_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_minimal_project(root)

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), "--root", str(root)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("status=PASS", completed.stdout)

    def test_version_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_minimal_project(root, cmake_version="0.1.0")

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), "--root", str(root)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("version mismatch", completed.stderr)

    def test_conan_recipe_version_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_minimal_project(root, conan_version="9.9.9")

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), "--root", str(root)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("conanfile.py", completed.stderr)

    def test_local_release_notes_do_not_affect_version_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_minimal_project(root)
            (root / "RELEASE.md").write_text("[Missing](benchmarks/results/missing.md)\n", encoding="utf-8")

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), "--root", str(root)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("status=PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
