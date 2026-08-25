#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "point_mutating_member",
    "rect_legacy_member",
    "polytree_legacy_shape",
    "path_append_operator",
)

HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
POINT_MUTATING_RE = re.compile(r"\b(?:Init|SetZ|Negate)\s*\(")
RECT_LEGACY_RE = re.compile(
    r"\b(?:InvalidRect|IsValid|Width|Height|MidPoint|AsPath|Contains|Scale|IsEmpty|Intersects|UnionBounds)\s*\("
)
POLYTREE_LEGACY_RE = re.compile(
    r"\b(?:class\s+PolyPath|struct\s+PolyPath|virtual\b|PolyPath\s*\*|AddChild|Count|Parent|IsHole|Child|Polygon|Area|SetScale|Scale)\b"
)
PATH_APPEND_RE = re.compile(r"\boperator\s*<<\s*\(")


@dataclass(frozen=True)
class ValueObjectFinding:
    category: str
    path: str
    line: int
    label: str
    snippet: str

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.category}: {self.label}: {self.snippet}"


def relative_to_root(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def is_header(path: Path) -> bool:
    return path.is_file() and path.suffix in HEADER_SUFFIXES


def is_default_public_header(path: Path, root: Path) -> bool:
    relative = relative_to_root(path, root)
    normalized = f"/{relative}"
    return "/compat/" not in normalized and "/internal/" not in normalized


def add_finding(
    findings: list[ValueObjectFinding],
    category: str | None,
    finding_category: str,
    path: Path,
    root: Path,
    line_number: int,
    label: str,
    snippet: str,
) -> None:
    if category not in (None, finding_category):
        return
    findings.append(
        ValueObjectFinding(
            finding_category,
            relative_to_root(path, root),
            line_number,
            label,
            snippet.strip(),
        )
    )


def scan_header(path: Path, root: Path, category: str | None) -> list[ValueObjectFinding]:
    findings: list[ValueObjectFinding] = []
    if not is_default_public_header(path, root):
        return findings
    text = path.read_text(encoding="utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), start=1):
        code_line = line.split("//", 1)[0]
        if path.name == "point.h" and POINT_MUTATING_RE.search(code_line):
            add_finding(
                findings,
                category,
                "point_mutating_member",
                path,
                root,
                line_number,
                "public point type exposes old mutating member",
                line,
            )
        if path.name == "rect.h" and RECT_LEGACY_RE.search(code_line):
            add_finding(
                findings,
                category,
                "rect_legacy_member",
                path,
                root,
                line_number,
                "public rect type exposes old PascalCase or mutating member",
                line,
            )
        if path.name == "poly_tree.h" and POLYTREE_LEGACY_RE.search(code_line):
            add_finding(
                findings,
                category,
                "polytree_legacy_shape",
                path,
                root,
                line_number,
                "public polygon tree exposes old virtual/raw-pointer shape",
                line,
            )
        if path.name == "path.h" and PATH_APPEND_RE.search(code_line):
            add_finding(
                findings,
                category,
                "path_append_operator",
                path,
                root,
                line_number,
                "public path header exposes operator<< append/stream overload shape",
                line,
            )
    return findings


def find_public_value_object_shape_findings(
    root: Path,
    category: str | None = None,
    strict_product: bool = False,
) -> list[ValueObjectFinding]:
    del strict_product
    root = root.resolve()
    findings: list[ValueObjectFinding] = []
    for path in sorted(root.rglob("*")):
        if not is_header(path):
            continue
        findings.extend(scan_header(path, root, category))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--category", choices=CATEGORIES)
    parser.add_argument("--strict-product", action="store_true")
    args = parser.parse_args()

    findings = find_public_value_object_shape_findings(
        Path(args.root),
        category=args.category,
        strict_product=args.strict_product,
    )
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "\n".join(finding.format() for finding in findings) + ("\n" if findings else ""),
        encoding="utf-8",
    )

    counts: dict[str, int] = {}
    for finding in findings:
        counts[finding.category] = counts.get(finding.category, 0) + 1
    for scanner_category in CATEGORIES:
        if args.category and scanner_category != args.category:
            continue
        print(f"{scanner_category}={counts.get(scanner_category, 0)}")
    print(f"findings={len(findings)}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
