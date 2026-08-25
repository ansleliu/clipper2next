#!/usr/bin/env python3
import re
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]

RETIRED_TOOL_PATHS = (
    "benchmarks/tools/external_legacy_speedup_gate.py",
    "tests/oracle/pending",
    "tools/checks/check_clone_similarity.py",
    "tools/checks/check_complete_refactor_shape.py",
    "tools/checks/check_cpp_style_shape.py",
    "tools/checks/check_engine_geometry_hot_inline_shape.py",
    "tools/checks/check_engine_private_shape.py",
    "tools/checks/check_installed_public_manifest.py",
    "tools/checks/check_legacy_style_shape.py",
    "tools/checks/check_offset_direct_simple_shape.py",
    "tools/checks/check_offset_move_union_shape.py",
    "tools/checks/check_root_next_ownership.py",
    "tools/checks/check_scanbeam_inversion_precheck_shape.py",
    "tools/checks/check_semantic_legacy_shape.py",
    "tools/checks/check_topology_pointer_shape.py",
    "tools/runners/run_modernization_gate.py",
)

RETIRED_CONFIGURATION_TOKENS = (
    "CLIPPER2NEXT_ENABLE_PMR",
    "CLIPPER2NEXT_USE_PMR",
    "CLIPPER2NEXT_ENABLE_OFFSET_PRESCAN_AVX2",
    "CLIPPER2NEXT_USE_OFFSET_PRESCAN_AVX2",
    "CLIPPER2NEXT_ENABLE_OFFSET_NORMAL_AVX2",
    "CLIPPER2NEXT_USE_OFFSET_NORMAL_AVX2",
    "CLIPPER2NEXT_COMPARE_WITH_LEGACY",
)

RETIRED_DEPENDENCY_RE = re.compile(
    r"mimalloc|mi_malloc|mi_free|CLIPPER2NEXT_USE_MIMALLOC|"
    r"CLIPPER2NEXT_ENABLE_MIMALLOC|oneTBB|CLIPPER2NEXT_USE_ONETBB|"
    r"\bTBB\b|tbb/|xsimd|range/v3|range-v3",
    re.IGNORECASE,
)

DEPENDENCY_AUDIT_ROOTS = (
    "benchmarks",
    "cmake",
    "include",
    "src",
    "tests",
    "tools",
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "vcpkg.json",
)

DEPENDENCY_AUDIT_SUFFIXES = {
    ".cmake",
    ".cpp",
    ".cxx",
    ".h",
    ".hpp",
    ".json",
    ".md",
    ".py",
    ".txt",
    ".yml",
}


def audited_files() -> list[Path]:
    result: list[Path] = []
    for relative_root in DEPENDENCY_AUDIT_ROOTS:
        root = REPO_ROOT / relative_root
        candidates = [root] if root.is_file() else sorted(root.rglob("*"))
        result.extend(
            path
            for path in candidates
            if path.is_file()
            and (
                path.suffix in DEPENDENCY_AUDIT_SUFFIXES
                or path.name == "CMakeLists.txt"
            )
            and "benchmarks/results" not in path.as_posix()
        )
    return result


class RepositoryHygieneTests(unittest.TestCase):
    def test_python_tool_roots_only_contain_package_markers(self) -> None:
        offenders = [
            path.relative_to(REPO_ROOT).as_posix()
            for root in (REPO_ROOT / "tools", REPO_ROOT / "benchmarks" / "tools")
            for path in sorted(root.glob("*.py"))
            if path.name != "__init__.py"
        ]
        self.assertEqual([], offenders)

    def test_local_generated_artifacts_are_ignored(self) -> None:
        root_gitignore = (REPO_ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("build*/", root_gitignore)
        self.assertIn("/install/", root_gitignore)
        self.assertIn("ctest-*.log", root_gitignore)
        self.assertIn("benchmarks/results/", root_gitignore)
        self.assertIn("__pycache__/", root_gitignore)

    def test_dependency_audit_roots_are_version_controlled(self) -> None:
        completed = subprocess.run(
            ["git", "ls-files"],
            cwd=REPO_ROOT,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        )
        tracked_paths = set(completed.stdout.splitlines())
        missing = [
            relative
            for relative in DEPENDENCY_AUDIT_ROOTS
            if relative not in tracked_paths
            and not any(
                path.startswith(f"{relative.rstrip('/')}/")
                for path in tracked_paths
            )
        ]
        self.assertEqual([], missing)

    def test_retired_migration_scaffolding_is_absent(self) -> None:
        self.assertEqual(
            [],
            [relative for relative in RETIRED_TOOL_PATHS if (REPO_ROOT / relative).exists()],
        )

    def test_retired_configuration_switches_are_absent(self) -> None:
        offenders: list[tuple[str, int, str]] = []
        for path in audited_files():
            relative = path.relative_to(REPO_ROOT).as_posix()
            if relative == "tools/tests/repository_hygiene_tests.py" or relative.endswith(
                "_tests.py"
            ):
                continue
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                for token in RETIRED_CONFIGURATION_TOKENS:
                    if token in line:
                        offenders.append((relative, line_number, token))
        self.assertEqual([], offenders)

    def test_release_required_artifacts_are_present(self) -> None:
        for relative in (
            "LICENSE_1_0.txt",
            "NOTICE.md",
            "tools/baselines/public_api_surface.txt",
        ):
            self.assertTrue((REPO_ROOT / relative).is_file(), relative)

    def test_cmake_installs_license_and_notice(self) -> None:
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('"${CMAKE_CURRENT_LIST_DIR}/LICENSE_1_0.txt"', cmake)
        self.assertIn('"${CMAKE_CURRENT_LIST_DIR}/NOTICE.md"', cmake)

    def test_retired_external_dependencies_are_absent(self) -> None:
        offenders: list[tuple[str, int, str]] = []
        for path in audited_files():
            relative = path.relative_to(REPO_ROOT).as_posix()
            if relative == "tools/tests/repository_hygiene_tests.py":
                continue
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                if match := RETIRED_DEPENDENCY_RE.search(line):
                    offenders.append((relative, line_number, match.group(0)))
        self.assertEqual([], offenders)

    def test_tests_do_not_reference_retired_repo_local_external_sources(self) -> None:
        offenders = [
            (path.relative_to(REPO_ROOT).as_posix(), line_number)
            for path in sorted((REPO_ROOT / "tests").rglob("*"))
            if path.suffix in {".cpp", ".h", ".hpp", ".cxx"}
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            )
            if "external_sources" in line
        ]
        self.assertEqual([], offenders)

    def test_oracle_tests_only_vendor_bounded_geometry_corpus(self) -> None:
        corpus_root = REPO_ROOT / "tests" / "oracle" / "corpus"
        vendored_files = sorted(
            path.relative_to(REPO_ROOT).as_posix()
            for path in corpus_root.rglob("*")
            if path.is_file()
        )
        self.assertTrue(vendored_files)
        self.assertTrue(
            all(path.startswith("tests/oracle/corpus/geometry/") for path in vendored_files)
        )
        self.assertTrue(
            (corpus_root / "geometry" / "normalized" / "verification" / "overlay.jsonl").is_file()
        )

    def test_oracle_tests_do_not_use_retired_corpus_contracts(self) -> None:
        retired_tokens = {
            "CLIPPER2NEXT_ORACLE_CORPUS_DIR",
            "CLIPPER2NEXT_LEGACY_TESTS_DIR",
            "jts_geos_overlay_smoke.xml",
            "real_world_wkt_smoke.tsv",
            "issue_regression_map.tsv",
            "tests/oracle/corpus/legacy",
            "../tests/oracle/corpus",
        }
        offenders: list[tuple[str, int, str]] = []
        roots = (REPO_ROOT / "tests", REPO_ROOT / "benchmarks", REPO_ROOT / "CMakeLists.txt")
        for root in roots:
            candidates = [root] if root.is_file() else sorted(root.rglob("*"))
            for path in candidates:
                if not path.is_file() or (
                    path.suffix not in {".cpp", ".h", ".hpp", ".cxx", ".cmake", ".txt"}
                    and path.name != "CMakeLists.txt"
                ):
                    continue
                relative = path.relative_to(REPO_ROOT).as_posix()
                for line_number, line in enumerate(
                    path.read_text(encoding="utf-8").splitlines(), 1
                ):
                    for token in retired_tokens:
                        if token in line:
                            offenders.append((relative, line_number, token))
        self.assertEqual([], offenders)

    def test_batch_determinism_tests_remain_registered(self) -> None:
        cmake = (REPO_ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("support/batch_determinism_tests.cpp", cmake)


if __name__ == "__main__":
    unittest.main()
