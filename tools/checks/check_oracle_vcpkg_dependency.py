#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    category: str
    detail: str

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.category}: {self.detail}"


def line_number(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def scan_cmake(path: Path, root: Path) -> list[Finding]:
    relative = path.relative_to(root).as_posix()
    text = path.read_text(encoding="utf-8", errors="replace")
    patterns = (
        (
            "local_legacy_root",
            r"\bCLIPPER2NEXT_LEGACY_ROOT\b",
            "oracle CMake must not depend on the parent legacy checkout",
        ),
        (
            "local_legacy_source",
            r"Clipper2Lib[/\\]",
            "oracle CMake must not compile or include local Clipper2Lib sources",
        ),
        (
            "local_legacy_target",
            r"(?is)add_library\s*\(\s*Clipper2Z?\b",
            "oracle CMake must use imported vcpkg Clipper2 targets",
        ),
        (
            "bare_legacy_link",
            r"(?is)target_link_libraries\s*\([^\)]*(?<!:)\bClipper2Z?\b(?!:)[^\)]*\)",
            "oracle CMake must link Clipper2::Clipper2 or Clipper2::Clipper2Z",
        ),
    )

    findings: list[Finding] = []
    for category, pattern, detail in patterns:
        for match in re.finditer(pattern, text):
            findings.append(Finding(relative, line_number(text, match.start()), category, detail))
    return findings


def find_oracle_vcpkg_dependency_findings(root: Path) -> list[Finding]:
    root = root.resolve()
    cmake_paths = [
        root / "CMakeLists.txt",
        root / "tests" / "CMakeLists.txt",
        root / "benchmarks" / "CMakeLists.txt",
    ]
    existing_paths = [path for path in cmake_paths if path.exists()]
    findings: list[Finding] = []
    for path in existing_paths:
        findings.extend(scan_cmake(path, root))

    combined = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in existing_paths)
    uses_imported_clipper2 = "Clipper2::Clipper2" in combined or "Clipper2::Clipper2Z" in combined
    has_find_package = re.search(r"find_package\s*\(\s*Clipper2\s+CONFIG\s+REQUIRED\s*\)", combined) is not None
    if uses_imported_clipper2 and not has_find_package:
        findings.append(
            Finding(
                "CMakeLists.txt",
                1,
                "missing_find_package",
                "imported Clipper2 targets require find_package(Clipper2 CONFIG REQUIRED)",
            )
        )

    return sorted(findings, key=lambda finding: (finding.path, finding.line, finding.category))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    findings = find_oracle_vcpkg_dependency_findings(Path(args.root))
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
