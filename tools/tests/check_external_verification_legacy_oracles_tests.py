#!/usr/bin/env python3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "checks" / "check_external_verification_legacy_oracles.py"


class ExternalVerificationLegacyOracleCheckerTests(unittest.TestCase):
    def run_checker(self, root: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(CHECKER),
                "--root",
                str(root),
                "--profiles",
                "rectclip-lines,minkowski",
                *extra,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def write_external_tests(self, root: Path, text: str) -> None:
        path = root / "tests" / "oracle" / "external_geometry_corpus_tests.cpp"
        path.parent.mkdir(parents=True)
        path.write_text(text, encoding="utf-8")

    def test_rejects_profile_consumers_without_live_legacy_oracle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write_external_tests(
                root,
                """
                auto assert_rectclip_lines_profile() -> void {
                    const auto actual = next::rect_clip_lines(request).paths;
                    EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
                }
                auto assert_minkowski_profile() -> void {
                    const auto actual = execute_next_minkowski_sum_origin(patterns, origin, true);
                    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
                }
                TEST(ExternalGeometryCorpus, OpenLineClipVerificationCorpusExecutesDerivedIdentityOracle) {
                    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("rectclip-lines")
                    assert_rectclip_lines_profile();
                }
                TEST(ExternalGeometryCorpus, MinkowskiVerificationCorpusExecutesDerivedOriginOracle) {
                    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("minkowski")
                    assert_minkowski_profile();
                }
                """,
            )

            result = self.run_checker(root)

        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("rectclip-lines", result.stdout)
        self.assertIn("minkowski", result.stdout)

    def test_accepts_profile_consumers_with_reachable_live_legacy_oracle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write_external_tests(
                root,
                """
                auto execute_legacy_line_clip() -> legacy::Paths64 {
                    return legacy::RectClipLines(rect, oracle::to_legacy_paths(lines));
                }
                auto execute_legacy_minkowski_sum() -> legacy::Paths64 {
                    return legacy::MinkowskiSum(pattern, path, true);
                }
                auto assert_rectclip_lines_profile() -> void {
                    const auto expected = execute_legacy_line_clip();
                    const auto actual = next::rect_clip_lines(request).paths;
                    EXPECT_NO_THROW(oracle::assert_open_paths_exactly_equal(expected, actual));
                }
                auto assert_minkowski_profile() -> void {
                    const auto expected = execute_legacy_minkowski_sum();
                    const auto actual = execute_next_minkowski_sum_origin(patterns, origin, true);
                    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
                }
                TEST(ExternalGeometryCorpus, OpenLineClipVerificationCorpusExecutesAgainstLegacy) {
                    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("rectclip-lines")
                    assert_rectclip_lines_profile();
                }
                TEST(ExternalGeometryCorpus, MinkowskiVerificationCorpusExecutesAgainstLegacy) {
                    // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("minkowski")
                    assert_minkowski_profile();
                }
                """,
            )

            result = self.run_checker(root)

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("status=PASS", result.stdout)

    def test_repository_default_requires_every_contract_profile(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(CHECKER),
                "--root",
                str(ROOT),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("status=PASS profiles=16", result.stdout)


if __name__ == "__main__":
    unittest.main()
