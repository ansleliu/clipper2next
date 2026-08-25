#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "checks" / "check_default_oracle_dependency.py"


class DefaultOracleDependencyTests(unittest.TestCase):
    def run_checker(self, files: dict[str, str]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            root.mkdir(parents=True, exist_ok=True)
            checker = root / "tools" / "checks" / CHECKER.name
            checker.parent.mkdir(parents=True)
            checker.write_text(CHECKER.read_text(encoding="utf-8"), encoding="utf-8")
            for relative, text in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
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

    def test_rejects_legacy_include_in_product_source(self) -> None:
        result = self.run_checker({"src/product.cpp": '#include "clipper2/clipper.h"\n'})

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("legacy_include", result.stdout)

    def test_ignores_vcpkg_build_artifacts(self) -> None:
        result = self.run_checker(
            {
                "src/product.cpp": "#include <vector>\n",
                "build/msvc-oracle/vcpkg_installed/vcpkg/blds/clipper2/src/foo.cpp":
                    '#include "clipper2/clipper.h"\n',
            }
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
