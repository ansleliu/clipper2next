#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.release.source_inventory import (
    exported_inventory,
    validate_inventory,
    public_archive_inventory,
)


IGNORED_DIRECTORIES = {
    ".git",
    ".idea",
    ".vs",
    ".vscode",
    "__pycache__",
    "build",
    "install",
    "out",
    "vcpkg_installed",
}
IGNORED_FILES = {
    "tools/checks/check_public_release_information.py",
    "tools/tests/check_public_release_information_tests.py",
}
TEXT_SUFFIXES = {
    "",
    ".cmake",
    ".cpp",
    ".csv",
    ".h",
    ".hpp",
    ".in",
    ".json",
    ".jsonl",
    ".md",
    ".map",
    ".py",
    ".sh",
    ".txt",
    ".yml",
    ".yaml",
}


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    category: str
    value: str

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.category}: {self.value}"


PATTERNS = (
    (
        "private_ipv4",
        re.compile(
            r"(?<![0-9])(?:"
            r"10(?:[.][0-9]{1,3}){3}|"
            r"192[.]168(?:[.][0-9]{1,3}){2}|"
            r"172[.](?:1[6-9]|2[0-9]|3[01])(?:[.][0-9]{1,3}){2}"
            r")(?![0-9])"
        ),
    ),
    (
        "internal_identity",
        re.compile(
            r"(?i)(?<![A-Za-z0-9])(?:lk"
            r"sense[0-9]*|link"
            r"sense|ai"
            r"fi)(?![A-Za-z0-9])"
        ),
    ),
    (
        "private_local_path",
        re.compile(
            r"(?i)(?:"
            r"[A-Za-z]:[\\/](?:Users|User|TestData)[\\/]|"
            r"/(?:home|Users)/[^/\s]+/"
            r")"
        ),
    ),
    (
        "private_key",
        re.compile(r"-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----"),
    ),
    (
        "access_token",
        re.compile(
            r"(?:gh[pousr]_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|"
            r"(?i:private[-_ ]?token|api[-_ ]?key)[ \t]*[:=][ \t]*[^\s]+)"
        ),
    ),
    (
        "email_address",
        re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+[.][A-Za-z]{2,}"),
    ),
)


def candidate_files(root: Path) -> list[Path]:
    result: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if relative in IGNORED_FILES:
            continue
        parts = set(path.relative_to(root).parts)
        if parts & IGNORED_DIRECTORIES:
            continue
        if relative.startswith("benchmarks/results/"):
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES and path.name != "CMakeLists.txt":
            continue
        result.append(path)
    return sorted(result)


def find_findings(root: Path, files: list[Path] | None = None) -> list[Finding]:
    repository = root.resolve()
    findings: list[Finding] = []
    for path in candidate_files(repository) if files is None else files:
        relative = path.relative_to(repository).as_posix()
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
        ):
            for category, pattern in PATTERNS:
                for match in pattern.finditer(line):
                    value = match.group(0)
                    if category == "email_address" and value.casefold().endswith(
                        "@example.invalid"
                    ):
                        continue
                    findings.append(Finding(relative, line_number, category, value))
    return findings


def public_inputs(root: Path) -> list[Path]:
    root = root.resolve()
    inventory = public_archive_inventory(root)
    return [
        root / name
        for name in sorted(set(inventory["files"]) | set(inventory["archive_files"]))
        if name not in IGNORED_FILES
        and (
            (root / name).suffix.lower() in TEXT_SUFFIXES
            or Path(name).name == "CMakeLists.txt"
        )
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject private or sensitive information in the public release tree."
    )
    parser.add_argument("--root", default=".")
    parser.add_argument("--output")
    parser.add_argument("--inventory", type=Path)
    arguments = parser.parse_args()
    import json

    root = Path(arguments.root).resolve()
    inventory = (
        json.loads(arguments.inventory.read_text(encoding="utf-8"))
        if arguments.inventory
        else exported_inventory(root)
    )
    findings = find_findings(
        root,
        validate_inventory(root, inventory)
        if arguments.inventory
        else public_inputs(root),
    )
    rendered = "\n".join(finding.format() for finding in findings)
    if arguments.output:
        output = Path(arguments.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered + ("\n" if rendered else ""), encoding="utf-8")
    if findings:
        print(rendered, file=sys.stderr)
        print(f"status=FAIL findings={len(findings)}")
        return 1
    print("status=PASS findings=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
