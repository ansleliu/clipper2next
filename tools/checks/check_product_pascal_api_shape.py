#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "pascal_free_function",
    "pascal_member_function",
    "pascal_parameter_name",
    "predicate_names",
)

HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
FREE_FUNCTIONS = (
    "BooleanOp",
    "Intersect",
    "Union",
    "Difference",
    "Xor",
    "InflatePaths",
    "TranslatePath",
    "TranslatePaths",
    "RectClip",
    "RectClipLines",
    "GetBounds",
    "Area",
    "IsPositive",
    "MinkowskiSum",
    "MinkowskiDiff",
    "Triangulate",
    "WritePath",
    "WritePaths",
    "EraseConsecutiveDuplicates",
    "StripDuplicates",
    "ScalePath",
    "ScalePaths",
    "CheckPrecisionRange",
)
PREDICATE_FUNCTIONS = (
    "Sqr",
    "DistanceSqr",
    "NearEqual",
    "TransformPath",
    "TransformPaths",
    "StripNearEqual",
    "TriSign",
    "MultiplyUInt64",
    "ProductsAreEqual",
    "CrossProductSign",
    "IsCollinear",
    "CrossProduct",
    "DotProduct",
    "PerpendicDistFromLineSqrd",
    "GetLineIntersectPt",
    "TranslatePoint",
    "ReflectPoint",
    "GetSign",
    "SegmentsIntersect",
    "GetClosestPointOnSegment",
    "PointInPolygon",
)
VALUE_MEMBER_FUNCTIONS = (
    "Init",
    "SetZ",
    "Negate",
    "InvalidRect",
    "IsValid",
    "Width",
    "Height",
    "MidPoint",
    "AsPath",
    "Contains",
    "Scale",
    "IsEmpty",
    "Intersects",
    "UnionBounds",
    "Level",
    "AddChild",
    "Count",
    "Parent",
    "IsHole",
    "Child",
    "Polygon",
    "Area",
    "SetScale",
)
CAMEL_PARAMETER_NAMES = (
    "isClosed",
    "decimalPlaces",
    "decPlaces",
    "useDelaunay",
    "preserveCollinear",
    "reverseSolution",
    "errorCode",
    "openPaths",
    "closedPaths",
)

FREE_FUNCTION_RE = re.compile(r"\b(" + "|".join(FREE_FUNCTIONS) + r")\s*\(")
PREDICATE_FUNCTION_RE = re.compile(r"\b(" + "|".join(PREDICATE_FUNCTIONS) + r")\s*\(")
VALUE_MEMBER_RE = re.compile(r"\b(" + "|".join(VALUE_MEMBER_FUNCTIONS) + r")\s*\(")
CAMEL_PARAMETER_RE = re.compile(r"\b(" + "|".join(CAMEL_PARAMETER_NAMES) + r")\b")
VALUE_HEADER_NAMES = {"point.h", "rect.h", "poly_tree.h", "path.h"}


@dataclass(frozen=True)
class PascalApiFinding:
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
    if "/compat/" in normalized or "/internal/" in normalized:
        return False
    return True


def add_finding(
    findings: list[PascalApiFinding],
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
        PascalApiFinding(
            finding_category,
            relative_to_root(path, root),
            line_number,
            label,
            snippet.strip(),
        )
    )


def strip_line_comment(line: str) -> str:
    return line.split("//", 1)[0]


def scan_header(path: Path, root: Path, category: str | None) -> list[PascalApiFinding]:
    findings: list[PascalApiFinding] = []
    if not is_default_public_header(path, root):
        return findings

    text = path.read_text(encoding="utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), start=1):
        code_line = strip_line_comment(line)
        for match in PREDICATE_FUNCTION_RE.finditer(code_line):
            add_finding(
                findings,
                category,
                "predicate_names",
                path,
                root,
                line_number,
                "PascalCase predicate or numeric helper in product header",
                match.group(0),
            )
        for match in FREE_FUNCTION_RE.finditer(code_line):
            add_finding(
                findings,
                category,
                "pascal_free_function",
                path,
                root,
                line_number,
                "PascalCase free function in product header",
                match.group(0),
            )
        if path.name in VALUE_HEADER_NAMES:
            for match in VALUE_MEMBER_RE.finditer(code_line):
                add_finding(
                    findings,
                    category,
                    "pascal_member_function",
                    path,
                    root,
                    line_number,
                    "PascalCase value-object member in product header",
                    match.group(0),
                )
        for match in CAMEL_PARAMETER_RE.finditer(code_line):
            add_finding(
                findings,
                category,
                "pascal_parameter_name",
                path,
                root,
                line_number,
                "camelCase parameter name remains in product header",
                match.group(0),
            )
    return findings


def find_product_pascal_api_findings(
    root: Path,
    category: str | None = None,
    strict_product: bool = False,
) -> list[PascalApiFinding]:
    del strict_product
    root = root.resolve()
    findings: list[PascalApiFinding] = []
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

    findings = find_product_pascal_api_findings(
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
