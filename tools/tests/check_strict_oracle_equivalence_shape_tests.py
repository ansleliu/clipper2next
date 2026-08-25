#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "checks" / "check_strict_oracle_equivalence_shape.py"


class StrictOracleEquivalenceShapeTests(unittest.TestCase):
    def run_checker(self, source_text: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "clipper2next" / "tests" / "oracle" / "external_corpus_tests.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(source_text, encoding="utf-8")
            checker = root / "tools" / "checks" / CHECKER.name
            checker.parent.mkdir(parents=True)
            checker.write_text(CHECKER.read_text(encoding="utf-8"), encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(checker), "--root", str(root / "clipper2next" / "tests" / "oracle")],
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_rejects_positional_nonzero_coordinate_tolerance(self) -> None:
        result = self.run_checker(
            "oracle::assert_paths_semantically_equal(expected, actual, oracle::path_equivalence_options{1});\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-zero oracle coordinate tolerance", result.stdout)

    def test_rejects_named_nonzero_coordinate_tolerance(self) -> None:
        result = self.run_checker(
            "auto options = oracle::path_equivalence_options{.coordinate_tolerance = 1};\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-zero oracle coordinate tolerance", result.stdout)

    def test_accepts_default_and_zero_tolerance(self) -> None:
        result = self.run_checker(
            "oracle::assert_paths_semantically_equal(expected, actual, oracle::path_equivalence_options{});\n"
            "auto options = oracle::path_equivalence_options{.coordinate_tolerance = 0};\n"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
