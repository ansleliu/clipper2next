#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "int_error_reference",
    "legacy_error_constant",
    "do_error_call",
    "legacy_error_accessor",
    "typed_error_bitmask_bridge",
)

SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx"}
LEGACY_ERROR_CONSTANTS = (
    "precision_error_i",
    "scale_error_i",
    "non_pair_error_i",
    "undefined_error_i",
    "range_error_i",
)

INT_ERROR_RE = re.compile(r"\b(?:int|std::int32_t|int32_t)\s*&\s*error_code\b")
ERROR_CONSTANT_RE = re.compile(r"\b(" + "|".join(LEGACY_ERROR_CONSTANTS) + r")\b")
DO_ERROR_RE = re.compile(r"\bDoError\s*\(")
ERROR_ACCESSOR_RE = re.compile(r"\b(?:ErrorCode|MutableErrorCode)\s*\(")
BITMASK_BRIDGE_RE = re.compile(
    r"\b(?:clipper_error|error_code|errors?)\b[^;\n]*"
    r"(?:(?<!\|)\|(?!\|)|&=|\|=|static_cast\s*<\s*int)"
)


@dataclass(frozen=True)
class ErrorShapeFinding:
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


def is_source_file(path: Path) -> bool:
    return path.is_file() and path.suffix in SOURCE_SUFFIXES


def relevant_roots(root: Path) -> list[Path]:
    candidates = [root / "include", root / "src"]
    existing = [candidate for candidate in candidates if candidate.exists()]
    return existing if existing else [root]


def is_product_file(path: Path, root: Path) -> bool:
    relative = relative_to_root(path, root)
    normalized = f"/{relative}"
    if any(part in normalized for part in ("/compat/", "/tests/", "/benchmarks/", "/tools/")):
        return False
    return True


def add_finding(
    findings: list[ErrorShapeFinding],
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
        ErrorShapeFinding(
            finding_category,
            relative_to_root(path, root),
            line_number,
            label,
            snippet.strip(),
        )
    )


def scan_file(path: Path, root: Path, category: str | None) -> list[ErrorShapeFinding]:
    findings: list[ErrorShapeFinding] = []
    if not is_product_file(path, root):
        return findings
    text = path.read_text(encoding="utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), start=1):
        code_line = line.split("//", 1)[0]
        if INT_ERROR_RE.search(code_line):
            add_finding(
                findings,
                category,
                "int_error_reference",
                path,
                root,
                line_number,
                "product API or implementation passes int error state by reference",
                line,
            )
        if ERROR_CONSTANT_RE.search(code_line):
            add_finding(
                findings,
                category,
                "legacy_error_constant",
                path,
                root,
                line_number,
                "legacy integer error constant in product file",
                line,
            )
        if DO_ERROR_RE.search(code_line):
            add_finding(
                findings,
                category,
                "do_error_call",
                path,
                root,
                line_number,
                "legacy DoError call in product file",
                line,
            )
        if ERROR_ACCESSOR_RE.search(code_line):
            add_finding(
                findings,
                category,
                "legacy_error_accessor",
                path,
                root,
                line_number,
                "legacy integer error accessor in product file",
                line,
            )
        if BITMASK_BRIDGE_RE.search(code_line):
            add_finding(
                findings,
                category,
                "typed_error_bitmask_bridge",
                path,
                root,
                line_number,
                "typed error appears to be converted through integer bitmask plumbing",
                line,
            )
    return findings


def find_product_error_shape_findings(
    root: Path,
    category: str | None = None,
    strict_product: bool = False,
) -> list[ErrorShapeFinding]:
    del strict_product
    root = root.resolve()
    findings: list[ErrorShapeFinding] = []
    for search_root in relevant_roots(root):
        for path in sorted(search_root.rglob("*")):
            if not is_source_file(path):
                continue
            findings.extend(scan_file(path, root, category))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--category", choices=CATEGORIES)
    parser.add_argument("--strict-product", action="store_true")
    args = parser.parse_args()

    findings = find_product_error_shape_findings(
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
