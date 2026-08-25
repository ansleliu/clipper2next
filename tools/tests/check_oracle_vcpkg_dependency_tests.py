#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "checks" / "check_oracle_vcpkg_dependency.py"


class OracleVcpkgDependencyTests(unittest.TestCase):
    def run_checker(self, cmake_text: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            root.mkdir(parents=True, exist_ok=True)
            (root / "CMakeLists.txt").write_text(cmake_text, encoding="utf-8")
            checker = root / "tools" / "checks" / CHECKER.name
            checker.parent.mkdir(parents=True)
            checker.write_text(CHECKER.read_text(encoding="utf-8"), encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    str(checker),
                    "--root",
                    str(root),
                    "--output",
                    str(root / "scan.log"),
                ],
                text=True,
                capture_output=True,
                check=False,
            )

    def test_rejects_oracle_compiling_parent_clipper2lib(self) -> None:
        result = self.run_checker(
            """
            if(CLIPPER2NEXT_BUILD_ORACLE)
              add_library(Clipper2
                "${CLIPPER2NEXT_LEGACY_ROOT}/Clipper2Lib/src/clipper.engine.cpp")
              target_include_directories(Clipper2 PUBLIC
                "${CLIPPER2NEXT_LEGACY_ROOT}/Clipper2Lib/include")
            endif()
            """
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("local_legacy_source", result.stdout)

    def test_accepts_imported_vcpkg_clipper2_targets(self) -> None:
        result = self.run_checker(
            """
            if(CLIPPER2NEXT_BUILD_ORACLE)
              find_package(Clipper2 CONFIG REQUIRED)
              set(clipper2next_oracle_legacy_target Clipper2::Clipper2)
              set(clipper2next_oracle_legacy_z_target Clipper2::Clipper2Z)
            endif()
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
