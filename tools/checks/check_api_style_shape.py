#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "legacy_product_type",
    "pascal_product_method",
    "mutable_public_state",
    "int_error_api",
    "compat_legacy_name",
)

HEADER_SUFFIXES = {".h", ".hpp", ".hh"}

LEGACY_PRODUCT_TYPES = (
    "Clipper64",
    "ClipperD",
    "ClipperBase",
    "ClipperOffset",
    "RectClip64",
    "RectClipLines64",
    "ReuseableDataContainer64",
)
PASCAL_PRODUCT_METHODS = (
    "AddPath",
    "AddPaths",
    "ArcTolerance",
    "CheckCallback",
    "Clear",
    "ErrorCode",
    "Execute",
    "MiterLimit",
    "MutableErrorCode",
    "PreserveCollinear",
    "ReverseSolution",
    "SetDefaultZ",
    "SetZCallback",
    "State",
    "ZCB",
)

LEGACY_TYPE_RE = re.compile(r"\b(" + "|".join(LEGACY_PRODUCT_TYPES) + r")\b")
PASCAL_METHOD_RE = re.compile(r"\b(" + "|".join(PASCAL_PRODUCT_METHODS) + r")\s*(?:\(|;|=)")
MUTABLE_STATE_RE = re.compile(r"\b(?:DefaultZ|error_code_|preserve_collinear_|reverse_solution_)\b")
INT_ERROR_RE = re.compile(r"\b(?:int|std::int32_t|int32_t)\s+ErrorCode\s*\(|\bErrorCode\s*\([^)]*\)\s*(?:const\s*)?->\s*(?:int|std::int32_t|int32_t)\b")


@dataclass(frozen=True)
class ApiStyleFinding:
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


def is_compat_header(path: Path, root: Path) -> bool:
    return "/compat/" in f"/{relative_to_root(path, root)}"


def is_default_public_header(path: Path, root: Path) -> bool:
    relative = relative_to_root(path, root)
    normalized = f"/{relative}"
    return "/internal/" not in normalized and "/compat/" not in normalized


def add_finding(
    findings: list[ApiStyleFinding],
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
        ApiStyleFinding(
            finding_category,
            relative_to_root(path, root),
            line_number,
            label,
            snippet.strip(),
        )
    )


def scan_header(
    path: Path,
    root: Path,
    category: str | None,
    include_compat: bool,
) -> list[ApiStyleFinding]:
    findings: list[ApiStyleFinding] = []
    default_public = is_default_public_header(path, root)
    compat_header = is_compat_header(path, root)
    if not default_public and not (include_compat and compat_header):
        return findings

    text = path.read_text(encoding="utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), start=1):
        code_line = line.split("//", 1)[0]
        if default_public:
            if LEGACY_TYPE_RE.search(code_line):
                add_finding(
                    findings,
                    category,
                    "legacy_product_type",
                    path,
                    root,
                    line_number,
                    "legacy product type name in default public header",
                    line,
                )
            if PASCAL_METHOD_RE.search(code_line):
                add_finding(
                    findings,
                    category,
                    "pascal_product_method",
                    path,
                    root,
                    line_number,
                    "PascalCase product API member",
                    line,
                )
            if MUTABLE_STATE_RE.search(code_line):
                add_finding(
                    findings,
                    category,
                    "mutable_public_state",
                    path,
                    root,
                    line_number,
                    "mutable public status or state field",
                    line,
                )
            if INT_ERROR_RE.search(code_line):
                add_finding(
                    findings,
                    category,
                    "int_error_api",
                    path,
                    root,
                    line_number,
                    "int-based public error accessor",
                    line,
                )
        elif include_compat and compat_header:
            if LEGACY_TYPE_RE.search(code_line):
                add_finding(
                    findings,
                    category,
                    "compat_legacy_name",
                    path,
                    root,
                    line_number,
                    "legacy type name in explicit compat scope",
                    line,
                )
            if PASCAL_METHOD_RE.search(code_line):
                add_finding(
                    findings,
                    category,
                    "compat_legacy_name",
                    path,
                    root,
                    line_number,
                    "legacy method name in explicit compat scope",
                    line,
                )
    return findings


def find_api_style_findings(
    root: Path,
    category: str | None = None,
    include_compat: bool = False,
    strict_product: bool = False,
) -> list[ApiStyleFinding]:
    del strict_product
    root = root.resolve()
    findings: list[ApiStyleFinding] = []
    for path in sorted(root.rglob("*")):
        if not is_header(path):
            continue
        findings.extend(scan_header(path, root, category, include_compat))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--category", choices=CATEGORIES)
    parser.add_argument("--include-compat", action="store_true")
    parser.add_argument("--strict-product", action="store_true")
    args = parser.parse_args()

    findings = find_api_style_findings(
        Path(args.root),
        category=args.category,
        include_compat=args.include_compat,
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
