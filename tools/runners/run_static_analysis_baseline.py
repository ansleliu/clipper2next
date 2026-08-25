#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SOURCES = [
    "include",
    "src",
    "tests",
]

COMMON_CLANG_TIDY_CANDIDATES = [
    Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"),
    Path(r"C:\Program Files\LLVM\bin\clang-tidy.exe"),
]

COMMON_CPPCHECK_CANDIDATES = [
    Path(r"C:\Program Files\Cppcheck\cppcheck.exe"),
    Path(r"C:\Program Files (x86)\Cppcheck\cppcheck.exe"),
]


@dataclass(frozen=True)
class ToolResult:
    name: str
    status: str
    executable: str
    log: str
    detail: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(path: str | Path) -> Path:
    candidate = Path(path)
    if candidate.is_absolute():
        return candidate
    return repo_root() / candidate


def find_executable(name: str, candidates: list[Path]) -> str | None:
    executable = shutil.which(name)
    if executable is not None:
        return executable
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    return None


def run_command(command: list[str], log_path: Path) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as handle:
        handle.write(" ".join(command))
        handle.write("\n\n")
        completed = subprocess.run(
            command,
            cwd=repo_root(),
            stdout=handle,
            stderr=subprocess.STDOUT,
            check=False,
        )
    return completed.returncode


def run_commands(commands: list[list[str]], log_path: Path, jobs: int) -> int:
    if not commands:
        return 0

    def execute(command: list[str]) -> tuple[int, str]:
        try:
            completed = subprocess.run(
                command,
                cwd=repo_root(),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            return completed.returncode, completed.stdout
        except OSError as error:
            return 127, f"failed to execute command: {error}\n"

    worker_count = min(max(1, jobs), len(commands))
    with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as executor:
        results = list(executor.map(execute, commands))

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as handle:
        command_count = len(commands)
        for index, (command, (status, output)) in enumerate(zip(commands, results), start=1):
            handle.write(f"[{index}/{command_count}] {' '.join(command)}\n\n")
            handle.write(output)
            if output and not output.endswith("\n"):
                handle.write("\n")
            handle.write(f"exit_code={status}\n\n")

    return next((status for status, _ in results if status != 0), 0)


def clang_tidy_standard_args(os_name: str = os.name) -> list[str]:
    if os_name == "nt":
        return ["--extra-arg=/std:c++latest"]
    return ["--extra-arg=-std=c++23"]


def clang_tidy_commands(
    executable: str,
    files: list[Path],
    build_dir: Path,
    *,
    os_name: str = os.name,
) -> list[list[str]]:
    standard_args = clang_tidy_standard_args(os_name)
    return [
        [executable, str(path), *standard_args, "-p", str(build_dir)]
        for path in files
    ]


def cppcheck_standard_args() -> list[str]:
    return [
        "--language=c++",
        "--std=c++20",
        "--error-exitcode=1",
        "--suppress=passedByValue",
    ]


def source_files(paths: list[str], limit: int) -> list[Path]:
    files: list[Path] = []
    for source in paths:
        root = resolve_path(source)
        if root.is_file() and root.suffix in {".cpp", ".h", ".hpp"}:
            files.append(root)
        elif root.is_dir():
            files.extend(sorted(path for path in root.rglob("*") if path.suffix in {".cpp", ".h", ".hpp"}))
    return files if limit <= 0 else files[:limit]


def compile_database_files(build_dir: Path) -> list[Path]:
    database = build_dir / "compile_commands.json"
    payload = json.loads(database.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError(f"compile database must contain a list: {database}")

    root = repo_root().resolve()
    files: set[Path] = set()
    for entry in payload:
        if not isinstance(entry, dict) or "file" not in entry or "directory" not in entry:
            continue
        candidate = Path(entry["file"])
        if not candidate.is_absolute():
            candidate = Path(entry["directory"]) / candidate
        candidate = candidate.resolve()
        if (
            candidate.is_file()
            and candidate.suffix in {".cpp", ".cxx"}
            and candidate.is_relative_to(root)
        ):
            files.add(candidate)
    return sorted(files)


def check_clang_tidy(
    files: list[Path],
    output_dir: Path,
    build_dir: Path | None,
    jobs: int = 4,
) -> ToolResult:
    executable = find_executable("clang-tidy", COMMON_CLANG_TIDY_CANDIDATES)
    log_path = output_dir / "clang_tidy_baseline.log"
    if executable is None:
        return ToolResult(
            "clang-tidy", "BLOCKED", "", str(log_path), "clang-tidy executable was not found")
    if not files:
        return ToolResult("clang-tidy", "BLOCKED", executable, str(log_path), "no source files selected")

    if build_dir is None:
        return ToolResult(
            "clang-tidy", "BLOCKED", executable, str(log_path), "build directory was not provided")
    commands = clang_tidy_commands(executable, files, build_dir)
    status = run_commands(commands, log_path, jobs)
    return ToolResult(
        "clang-tidy",
        "PASS" if status == 0 else "FAIL",
        executable,
        str(log_path),
        f"exit_code={status}",
    )


def check_cppcheck(files: list[Path], output_dir: Path) -> ToolResult:
    executable = find_executable("cppcheck", COMMON_CPPCHECK_CANDIDATES)
    log_path = output_dir / "cppcheck_baseline.log"
    if executable is None:
        return ToolResult(
            "cppcheck", "BLOCKED", "", str(log_path), "cppcheck executable was not found")
    if not files:
        return ToolResult("cppcheck", "BLOCKED", executable, str(log_path), "no source files selected")

    command = [
        executable,
        "--enable=warning,performance,portability",
        "--inline-suppr",
        *cppcheck_standard_args(),
        *[str(path) for path in files],
    ]
    status = run_command(command, log_path)
    return ToolResult(
        "cppcheck",
        "PASS" if status == 0 else "FAIL",
        executable,
        str(log_path),
        f"exit_code={status}",
    )


def overall_status(results: list[ToolResult]) -> str:
    if any(result.status == "FAIL" for result in results):
        return "FAIL"
    if any(result.status == "BLOCKED" for result in results):
        return "BLOCKED"
    return "PASS"


def analysis_results(
    files: list[Path],
    output_dir: Path,
    build_dir: Path | None,
    *,
    disable_clang_tidy: bool = False,
    clang_tidy_jobs: int = 4,
) -> list[ToolResult]:
    results: list[ToolResult] = []
    if not disable_clang_tidy:
        log_path = output_dir / "clang_tidy_baseline.log"
        if build_dir is None:
            results.append(
                ToolResult(
                    "clang-tidy",
                    "BLOCKED",
                    "",
                    str(log_path),
                    "compile database build directory was not provided",
                )
            )
        else:
            try:
                clang_tidy_files = compile_database_files(build_dir)
            except (OSError, ValueError, json.JSONDecodeError) as error:
                results.append(
                    ToolResult(
                        "clang-tidy",
                        "BLOCKED",
                        "",
                        str(log_path),
                        f"compile database unavailable or invalid: {error}",
                    )
                )
            else:
                results.append(
                    check_clang_tidy(
                        clang_tidy_files, output_dir, build_dir, jobs=clang_tidy_jobs
                    )
                )
    results.append(check_cppcheck(files, output_dir))
    return results


def write_json_report(path: Path, status: str, results: list[ToolResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "status": status,
        "tools": [result.__dict__ for result in results],
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown_report(path: Path, status: str, results: list[ToolResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Static Analysis Baseline",
        "",
        f"Status: **{status}**",
        "",
        "| Tool | Status | Executable | Log | Detail |",
        "| --- | --- | --- | --- | --- |",
    ]
    for result in results:
        lines.append(
            f"| {result.name} | {result.status} | `{result.executable}` | `{result.log}` | {result.detail} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run or archive static-analysis baseline status.")
    parser.add_argument("--output-dir", default="benchmarks/results/static_analysis")
    parser.add_argument("--build-dir")
    parser.add_argument("--source", action="append", dest="sources")
    parser.add_argument("--file-limit", type=int, default=0)
    parser.add_argument("--output-json")
    parser.add_argument("--output-md")
    parser.add_argument("--disable-clang-tidy", action="store_true")
    parser.add_argument("--clang-tidy-jobs", type=int, default=4)
    args = parser.parse_args()
    if args.clang_tidy_jobs < 1:
        parser.error("--clang-tidy-jobs must be at least 1")

    output_dir = resolve_path(args.output_dir)
    build_dir = resolve_path(args.build_dir) if args.build_dir else None
    selected_files = source_files(args.sources or DEFAULT_SOURCES, args.file_limit)
    results = analysis_results(
        selected_files,
        output_dir,
        build_dir,
        disable_clang_tidy=args.disable_clang_tidy,
        clang_tidy_jobs=args.clang_tidy_jobs,
    )
    status = overall_status(results)

    output_json = resolve_path(args.output_json) if args.output_json else output_dir / "static_analysis_baseline.json"
    output_md = resolve_path(args.output_md) if args.output_md else output_dir / "static_analysis_baseline.md"
    write_json_report(output_json, status, results)
    write_markdown_report(output_md, status, results)
    print(f"status={status}")
    print(f"summary={output_md}")
    return 0 if status == "PASS" else (1 if status == "FAIL" else 2)


if __name__ == "__main__":
    raise SystemExit(main())
