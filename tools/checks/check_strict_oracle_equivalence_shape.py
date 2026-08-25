#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ORACLE_ROOT = ROOT / "clipper2next" / "tests" / "oracle"
SOURCE_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}

POSITIONAL_OPTIONS_RE = re.compile(r"path_equivalence_options\s*\{\s*([^,}\n]*)")
NAMED_TOLERANCE_RE = re.compile(r"coordinate_tolerance\s*(?:=|\{)\s*([^,};\n]+)")


def is_zero_literal(token: str) -> bool:
    normalized = token.strip().lower()
    return normalized in {"0", "+0", "0l", "0ll", "0u", "0ul", "0ull"}


def source_files(root: Path) -> list[Path]:
    if root.is_file():
        return [root]
    return sorted(path for path in root.rglob("*") if path.suffix.lower() in SOURCE_SUFFIXES)


def relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def line_number(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def find_strict_oracle_findings(root: Path) -> list[str]:
    findings: list[str] = []
    for path in source_files(root):
        text = path.read_text(encoding="utf-8")
        for match in POSITIONAL_OPTIONS_RE.finditer(text):
            token = match.group(1).strip()
            if not token or token.startswith(".") or is_zero_literal(token):
                continue
            findings.append(
                f"{relative(path, root)}:{line_number(text, match.start())}: "
                "non-zero oracle coordinate tolerance is forbidden in strict oracle gates"
            )
        for match in NAMED_TOLERANCE_RE.finditer(text):
            token = match.group(1).strip()
            if is_zero_literal(token):
                continue
            findings.append(
                f"{relative(path, root)}:{line_number(text, match.start())}: "
                "non-zero oracle coordinate tolerance is forbidden in strict oracle gates"
            )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject non-zero coordinate tolerance in oracle path-equivalence gates."
    )
    parser.add_argument("--root", default=str(DEFAULT_ORACLE_ROOT))
    parser.add_argument("--output")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    findings = find_strict_oracle_findings(root)
    output = "\n".join(findings)
    if output:
        output += "\n"
    if args.output:
        output_path = Path(args.output).resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(output, encoding="utf-8")
    print(output, end="")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
