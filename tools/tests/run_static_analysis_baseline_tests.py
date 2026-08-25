#!/usr/bin/env python3
import json
import sys
import tempfile
import unittest
from pathlib import Path

from tools.runners import run_static_analysis_baseline as baseline


class StaticAnalysisBaselineTests(unittest.TestCase):
    def test_overall_status_blocks_when_tool_is_unavailable(self) -> None:
        results = [
            baseline.ToolResult("clang-tidy", "BLOCKED", "", "clang.log", "missing"),
            baseline.ToolResult("cppcheck", "PASS", "/usr/bin/cppcheck", "cpp.log", "exit_code=0"),
        ]

        self.assertEqual("BLOCKED", baseline.overall_status(results))

    def test_source_files_limits_selected_cpp_and_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "a.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            (root / "b.h").write_text("#pragma once\n", encoding="utf-8")
            (root / "ignored.txt").write_text("x\n", encoding="utf-8")

            files = baseline.source_files([str(root)], limit=1)

        self.assertEqual(1, len(files))
        self.assertIn(files[0].suffix, {".cpp", ".h"})

    def test_non_positive_file_limit_selects_all_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            first = root / "first.cpp"
            second = root / "second.cpp"
            first.write_text("int first;", encoding="utf-8")
            second.write_text("int second;", encoding="utf-8")

            self.assertEqual([first, second], baseline.source_files([str(root)], 0))

    def test_compile_database_files_returns_unique_existing_translation_units(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build = Path(temp_dir)
            source = build / "source.cpp"
            source.write_text("int value;", encoding="utf-8")
            (build / "compile_commands.json").write_text(
                json.dumps(
                    [
                        {
                            "directory": str(build),
                            "file": str(source),
                            "command": "c++ -c source.cpp",
                        },
                        {
                            "directory": str(build),
                            "file": str(source),
                            "command": "c++ -c source.cpp",
                        },
                    ]
                ),
                encoding="utf-8",
            )
            original_repo_root = baseline.repo_root
            try:
                baseline.repo_root = lambda: build
                self.assertEqual([source.resolve()], baseline.compile_database_files(build))
            finally:
                baseline.repo_root = original_repo_root

    def test_find_executable_accepts_explicit_existing_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            candidate = Path(temp_dir) / "tool.exe"
            candidate.write_text("", encoding="utf-8")

            executable = baseline.find_executable("definitely-not-on-path", [candidate])

        self.assertEqual(str(candidate), executable)

    def test_clang_tidy_standard_args_match_driver_family(self) -> None:
        self.assertEqual(["--extra-arg=/std:c++latest"], baseline.clang_tidy_standard_args("nt"))
        self.assertEqual(["--extra-arg=-std=c++23"], baseline.clang_tidy_standard_args("posix"))

    def test_clang_tidy_commands_use_one_translation_unit_per_process(self) -> None:
        files = [Path("first.cpp"), Path("second.cpp")]

        commands = baseline.clang_tidy_commands(
            "clang-tidy", files, Path("build"), os_name="posix"
        )

        self.assertEqual(2, len(commands))
        self.assertEqual(
            ["clang-tidy", "first.cpp", "--extra-arg=-std=c++23", "-p", "build"],
            commands[0],
        )
        self.assertEqual(
            ["clang-tidy", "second.cpp", "--extra-arg=-std=c++23", "-p", "build"],
            commands[1],
        )

    def test_run_commands_preserves_log_order_and_reports_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "commands.log"
            commands = [
                [sys.executable, "-c", "print('first-output')"],
                [sys.executable, "-c", "print('second-output'); raise SystemExit(3)"],
            ]

            status = baseline.run_commands(commands, log_path, jobs=2)
            content = log_path.read_text(encoding="utf-8")

        self.assertEqual(3, status)
        self.assertLess(content.index("first-output"), content.index("second-output"))
        self.assertIn("[1/2]", content)
        self.assertIn("[2/2]", content)

    def test_cppcheck_standard_args_use_widely_supported_cpp_dialect(self) -> None:
        self.assertEqual(
            [
                "--language=c++",
                "--std=c++20",
                "--error-exitcode=1",
                "--suppress=passedByValue",
            ],
            baseline.cppcheck_standard_args(),
        )

    def test_markdown_report_is_written(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "baseline.md"
            baseline.write_markdown_report(
                path,
                "PASS",
                [baseline.ToolResult("cppcheck", "PASS", "/bin/cppcheck", "cpp.log", "exit_code=0")],
            )

            content = path.read_text(encoding="utf-8")

        self.assertIn("Static Analysis Baseline", content)
        self.assertIn("cppcheck", content)

    def test_disable_clang_tidy_runs_cppcheck_only(self) -> None:
        calls: list[str] = []
        original_clang_tidy = baseline.check_clang_tidy
        original_cppcheck = baseline.check_cppcheck

        def fake_clang_tidy(
            files: list[Path], output_dir: Path, build_dir: Path | None
        ) -> baseline.ToolResult:
            calls.append("clang-tidy")
            return baseline.ToolResult("clang-tidy", "PASS", "/bin/clang-tidy", "clang.log", "exit_code=0")

        def fake_cppcheck(files: list[Path], output_dir: Path) -> baseline.ToolResult:
            calls.append("cppcheck")
            return baseline.ToolResult("cppcheck", "PASS", "/bin/cppcheck", "cpp.log", "exit_code=0")

        try:
            baseline.check_clang_tidy = fake_clang_tidy
            baseline.check_cppcheck = fake_cppcheck

            results = baseline.analysis_results([], Path("."), None, disable_clang_tidy=True)
        finally:
            baseline.check_clang_tidy = original_clang_tidy
            baseline.check_cppcheck = original_cppcheck

        self.assertEqual(["cppcheck"], calls)
        self.assertEqual(["cppcheck"], [result.name for result in results])


if __name__ == "__main__":
    unittest.main()
