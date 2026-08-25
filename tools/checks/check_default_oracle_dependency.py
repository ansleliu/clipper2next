#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
ORACLE_PATH_PARTS = ("tests/oracle/", "benchmarks/oracle/")
IGNORED_SOURCE_DIR_PARTS = {
    ".git",
    "__pycache__",
    "build",
    "out",
    "vcpkg_installed",
}


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    category: str
    detail: str

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.category}: {self.detail}"


def strip_oracle_blocks(text: str) -> str:
    lines = text.splitlines()
    result: list[str] = []
    oracle_depth = 0
    for line in lines:
        stripped = line.strip()
        starts_if = re.match(r"if\s*\(", stripped, re.IGNORECASE)
        ends_if = re.match(r"endif\s*\(", stripped, re.IGNORECASE) or re.match(r"endif\s*\s*$", stripped, re.IGNORECASE)

        if starts_if and "CLIPPER2NEXT_BUILD_ORACLE" in stripped:
            oracle_depth = 1
            result.append("")
            continue
        if oracle_depth:
            if starts_if:
                oracle_depth += 1
            elif ends_if:
                oracle_depth -= 1
            result.append("")
            continue
        result.append(line)
    return "\n".join(result)


def line_number(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def scan_cmake(path: Path, root: Path) -> list[Finding]:
    relative = path.relative_to(root).as_posix()
    original = path.read_text(encoding="utf-8", errors="replace")
    text = strip_oracle_blocks(original)
    patterns = (
        (
            "legacy_target",
            r"(?is)add_library\s*\(\s*Clipper2\b|target_link_libraries\s*\([^\)]*\bClipper2\b|list\s*\(\s*APPEND[^\)]*\bClipper2\b",
            "default CMake text references Clipper2 target"),
        ("legacy_source", r"Clipper2Lib[/\\]", "default CMake text references Clipper2Lib source/include path"),
        ("oracle_test_source", r"differential_[A-Za-z0-9_]+\.cpp", "default test sources include differential oracle test"),
        ("oracle_benchmark_source", r"legacy_vs_next_[A-Za-z0-9_]+\.cpp", "default benchmark sources include legacy-vs-next oracle benchmark"),
    )
    findings: list[Finding] = []
    for category, pattern, detail in patterns:
        for match in re.finditer(pattern, text):
            findings.append(Finding(relative, line_number(text, match.start()), category, detail))
    return findings


def scan_source(path: Path, root: Path) -> list[Finding]:
    relative = path.relative_to(root).as_posix()
    if any(part in relative for part in ORACLE_PATH_PARTS):
        return []
    text = path.read_text(encoding="utf-8", errors="replace")
    findings: list[Finding] = []
    for match in re.finditer(r'#\s*include\s+"clipper2/clipper\.h"', text):
        findings.append(
            Finding(
                relative,
                line_number(text, match.start()),
                "legacy_include",
                "non-oracle source includes clipper2/clipper.h"))
    return findings


def is_ignored_generated_path(path: Path, root: Path) -> bool:
    relative_parts = path.relative_to(root).parts
    return any(part in IGNORED_SOURCE_DIR_PARTS for part in relative_parts)


def find_default_oracle_dependencies(root: Path) -> list[Finding]:
    root = root.resolve()
    findings: list[Finding] = []
    for cmake_path in (root / "CMakeLists.txt", root / "tests" / "CMakeLists.txt", root / "benchmarks" / "CMakeLists.txt"):
        if cmake_path.exists():
            findings.extend(scan_cmake(cmake_path, root))
    for path in sorted(root.rglob("*")):
        if is_ignored_generated_path(path, root):
            continue
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            findings.extend(scan_source(path, root))
    return sorted(findings, key=lambda finding: (finding.path, finding.line, finding.category))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    findings = find_default_oracle_dependencies(Path(args.root))
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(finding.format() for finding in findings) + ("\n" if findings else ""), encoding="utf-8")
    counts: dict[str, int] = {}
    for finding in findings:
        counts[finding.category] = counts.get(finding.category, 0) + 1
    for category in sorted(counts):
        print(f"{category}={counts[category]}")
    print(f"findings={len(findings)}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
