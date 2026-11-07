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
        path.write_text("namespace legacy = Clipper2Lib;\n" + text, encoding="utf-8")

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

    def test_linked_support_requires_reachable_real_legacy_call(self) -> None:
        primary = """
            namespace oracle = clipper2next::tests::oracle;
            TEST(ExternalGeometryCorpus, Triangulation) {
                // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("triangulation")
                oracle::record_triangulation_case();
            }
        """
        support = """
            namespace clipper2next::tests::oracle {
                auto record_triangulation_case() -> void { Clipper2Lib::Triangulate(); }
            }
        """
        for scenario in ("linked", "removed-call", "disconnected", "unrelated", "unlinked"):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                text = primary if scenario != "disconnected" else primary.replace(
                    "oracle::record_triangulation_case();", "")
                self.write_external_tests(root, text)
                source = root / "tests/oracle/triangulation_contract.cpp"
                source.parent.mkdir(parents=True, exist_ok=True)
                body = support
                if scenario == "removed-call":
                    body = body.replace("Clipper2Lib::Triangulate();", "")
                if scenario == "unrelated":
                    body = body.replace("clipper2next::tests::oracle", "unrelated")
                source.write_text(body, encoding="utf-8")
                linked = "oracle/triangulation_contract.cpp" if scenario != "unlinked" else ""
                (root / "tests/CMakeLists.txt").write_text(
                    f"add_executable(clipper2next_oracle_tests {linked})", encoding="utf-8")
                result = self.run_checker(root, "--profiles", "triangulation")
                self.assertEqual(scenario == "linked", result.returncode == 0,
                                 result.stdout + result.stderr)

    def linked_triangulation_result(self, primary: str, support: str):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write_external_tests(root, primary)
            (root / "tests/oracle/triangulation_contract.cpp").write_text(support, encoding="utf-8")
            (root / "tests/CMakeLists.txt").write_text(
                "add_executable(clipper2next_oracle_tests oracle/triangulation_contract.cpp)",
                encoding="utf-8")
            return self.run_checker(root, "--profiles", "triangulation")

    def test_alias_identity_uses_visible_scope_and_declaration_position(self) -> None:
        primary = """
            namespace oracle = clipper2next::tests::oracle;
            TEST(ExternalGeometryCorpus, Triangulation) {
                // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("triangulation")
                oracle::record_triangulation_case();
            }
        """
        cases = {
            "sibling": """
                namespace clipper2next::tests::oracle {
                    namespace engine = clipper2next;
                    auto record_triangulation_case() -> void { engine::Triangulate(); }
                }
                namespace unrelated { namespace engine = Clipper2Lib; }
            """,
            "later-in-same-namespace": """
                namespace engine = clipper2next;
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void { engine::Triangulate(); }
                    namespace engine = Clipper2Lib;
                }
            """,
            "later-local": """
                namespace engine = clipper2next;
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void {
                        engine::Triangulate();
                        namespace engine = Clipper2Lib;
                    }
                }
            """,
            "inner-shadow-must-not-fall-back": """
                namespace engine = Clipper2Lib;
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void {
                        namespace product = clipper2next;
                        namespace engine = product;
                        engine::Triangulate();
                    }
                }
            """,
            "ambiguous-declarations": """
                namespace clipper2next::tests::oracle {
                    namespace engine = clipper2next;
                    namespace engine = Clipper2Lib;
                    auto record_triangulation_case() -> void { engine::Triangulate(); }
                }
            """,
            "nested-sibling-block": """
                namespace engine = clipper2next;
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void {
                        { namespace engine = Clipper2Lib; }
                        engine::Triangulate();
                    }
                }
            """,
        }
        for scenario, support in cases.items():
            with self.subTest(scenario=scenario):
                result = self.linked_triangulation_result(primary, support)
                self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)

    def test_visible_aliases_still_reach_the_linked_free_helper(self) -> None:
        primary = """
            namespace oracle = clipper2next::tests::oracle;
            TEST(ExternalGeometryCorpus, Triangulation) {
                // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("triangulation")
                oracle::record_triangulation_case();
            }
        """
        cases = (
            """
                namespace engine = Clipper2Lib;
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void { engine::Triangulate(); }
                }
                namespace unrelated { namespace engine = clipper2next; }
            """,
            """
                namespace clipper2next::tests::oracle { namespace engine = Clipper2Lib; }
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void { engine::Triangulate(); }
                }
            """,
            """
                namespace clipper2next::tests::oracle {
                    auto record_triangulation_case() -> void {
                        namespace engine = ::Clipper2Lib;
                        { namespace engine = clipper2next; engine::Triangulate(); }
                        engine::Triangulate();
                    }
                }
            """,
        )
        for support in cases:
            with self.subTest(support=support):
                result = self.linked_triangulation_result(primary, support)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_member_calls_do_not_reach_dead_free_helpers(self) -> None:
        for expression in ("recorder.record_triangulation_case()",
                           "recorder->record_triangulation_case()",
                           "recorder . /* comment */ record_triangulation_case()",
                           "recorder -> /* comment */ record_triangulation_case()"):
            with self.subTest(expression=expression):
                primary = """
                    struct Recorder { void record_triangulation_case() {} };
                    TEST(ExternalGeometryCorpus, Triangulation) {
                        // CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE("triangulation")
                        Recorder recorder;
                        CALL;
                    }
                """.replace("CALL", expression)
                support = "auto record_triangulation_case() -> void { Clipper2Lib::Triangulate(); }"
                result = self.linked_triangulation_result(primary, support)
                self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)

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
