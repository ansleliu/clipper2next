#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "product_target_sources",
    "target_overlap",
    "compat_include_in_product_source",
    "legacy_type_in_product_source",
    "legacy_product_calls",
    "compat_tree_in_product_root",
    "compat_tests_in_product_root",
    "sibling_compat_tree",
)

SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx"}
PRODUCT_TARGETS = ("clipper2next",)
COMPAT_TARGETS = ("clipper2next_compat", "compat")
COMPAT_IMPLEMENTATION_SOURCES = {
    "src/clipper_facade.cpp",
    "src/engine_base.cpp",
    "src/engine.cpp",
    "src/engine_facade.cpp",
    "src/offset.cpp",
    "src/rectclip.cpp",
}
LEGACY_TYPES = (
    "Clipper64",
    "ClipperD",
    "ClipperBase",
    "ClipperOffset",
    "RectClip64",
    "RectClipLines64",
)
LEGACY_METHODS = (
    "AddSubject",
    "AddOpenSubject",
    "AddClip",
    "AddPath",
    "AddPaths",
    "Execute",
    "PreserveCollinear",
    "ReverseSolution",
    "MutableErrorCode",
)

TARGET_CALL_RE = re.compile(r"\b(?:add_library|target_sources)\s*\(\s*([A-Za-z0-9_:.-]+)")
SOURCE_TOKEN_RE = re.compile(
    r"(?:(?:\$\{CMAKE_CURRENT_LIST_DIR\}/)?)(?P<path>(?:src|include)/[A-Za-z0-9_./+-]+\.(?:h|hpp|hh|cpp|cc|cxx))"
)
COMPAT_INCLUDE_RE = re.compile(r"#\s*include\s*[<\"]clipper2next/compat/")
LEGACY_TYPE_RE = re.compile(r"\b(" + "|".join(LEGACY_TYPES) + r")\b")
LEGACY_QUALIFIED_DEFINITION_RE = re.compile(r"\b(" + "|".join(LEGACY_TYPES) + r")::[A-Za-z_][A-Za-z0-9_]*\b")
LEGACY_METHOD_RE = re.compile(r"(?:\.|->)\s*(" + "|".join(LEGACY_METHODS) + r")\s*\(")


@dataclass(frozen=True)
class ProductCompatFinding:
    category: str
    path: str
    line: int
    label: str
    snippet: str

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.category}: {self.label}: {self.snippet}"


def normalize_cmake_path(value: str) -> str:
    return value.replace("\\", "/").lstrip("./")


def relative_to_root(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def strip_cmake_comments(text: str) -> str:
    lines = []
    for line in text.splitlines():
        lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def extract_cmake_call(text: str, start: int) -> tuple[str, int]:
    depth = 0
    end = start
    for index in range(start, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                end = index + 1
                break
    return text[start:end], end


def extract_target_sources(cmake: Path, targets: tuple[str, ...]) -> set[str]:
    if not cmake.exists():
        return set()
    text = strip_cmake_comments(cmake.read_text(encoding="utf-8", errors="replace"))
    sources: set[str] = set()
    for match in TARGET_CALL_RE.finditer(text):
        target = match.group(1)
        if target not in targets:
            continue
        block, _ = extract_cmake_call(text, match.start())
        for source_match in SOURCE_TOKEN_RE.finditer(block):
            sources.add(normalize_cmake_path(source_match.group("path")))
    return sources


def is_source_file(path: Path) -> bool:
    return path.is_file() and path.suffix in SOURCE_SUFFIXES


def add_finding(
    findings: list[ProductCompatFinding],
    category: str | None,
    finding_category: str,
    path: str,
    line_number: int,
    label: str,
    snippet: str,
) -> None:
    if category not in (None, finding_category):
        return
    findings.append(
        ProductCompatFinding(
            finding_category,
            path,
            line_number,
            label,
            snippet.strip(),
        )
    )


def scan_product_file(
    path: Path,
    root: Path,
    category: str | None,
) -> list[ProductCompatFinding]:
    findings: list[ProductCompatFinding] = []
    if not is_source_file(path):
        return findings
    relative = relative_to_root(path, root)
    if "/compat/" in f"/{relative}":
        return findings

    text = path.read_text(encoding="utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), start=1):
        code_line = line.split("//", 1)[0]
        if COMPAT_INCLUDE_RE.search(code_line):
            add_finding(
                findings,
                category,
                "compat_include_in_product_source",
                relative,
                line_number,
                "product implementation includes compat header",
                line,
            )
        if LEGACY_QUALIFIED_DEFINITION_RE.search(code_line):
            add_finding(
                findings,
                category,
                "legacy_type_in_product_source",
                relative,
                line_number,
                "product implementation defines legacy class member",
                line,
            )
        elif LEGACY_TYPE_RE.search(code_line):
            add_finding(
                findings,
                category,
                "legacy_type_in_product_source",
                relative,
                line_number,
                "product implementation references legacy class type",
                line,
            )
        for match in LEGACY_METHOD_RE.finditer(code_line):
            add_finding(
                findings,
                category,
                "legacy_product_calls",
                relative,
                line_number,
                "product implementation calls legacy stateful method",
                match.group(0),
            )
    return findings


def find_product_compat_boundary_findings(
    root: Path,
    cmake: Path,
    category: str | None = None,
    strict_product: bool = False,
) -> list[ProductCompatFinding]:
    root = root.resolve()
    cmake = cmake.resolve()
    product_root = cmake.parent
    product_sources = extract_target_sources(cmake, PRODUCT_TARGETS)
    compat_sources = extract_target_sources(cmake, COMPAT_TARGETS)
    findings: list[ProductCompatFinding] = []

    for source in sorted(product_sources):
        if source in COMPAT_IMPLEMENTATION_SOURCES:
            add_finding(
                findings,
                category,
                "product_target_sources",
                "CMakeLists.txt",
                1,
                "compat implementation source is compiled into product target",
                source,
            )

    for source in sorted(product_sources.intersection(compat_sources)):
        add_finding(
            findings,
            category,
            "target_overlap",
            "CMakeLists.txt",
            1,
            "source is listed in both product and compat targets",
            source,
        )

    if category in (None, "compat_include_in_product_source", "legacy_type_in_product_source", "legacy_product_calls"):
        for source in sorted(product_sources):
            path = product_root / source
            findings.extend(scan_product_file(path, root, category))

    if strict_product and category in (None, "compat_tree_in_product_root"):
        for relative_dir in ("include/clipper2next/compat", "src/compat"):
            path = product_root / relative_dir
            if path.exists():
                add_finding(
                    findings,
                    category,
                    "compat_tree_in_product_root",
                    relative_to_root(path, root),
                    1,
                    "compatibility code lives inside the product source tree",
                    relative_to_root(path, root),
                )

    if strict_product and category in (None, "compat_tests_in_product_root"):
        tests_root = product_root / "tests"
        if tests_root.exists():
            for path in tests_root.rglob("*"):
                if not path.is_file():
                    continue
                relative = relative_to_root(path, root)
                if path.name.startswith("compat_") or "compat_install_smoke_project" in path.parts:
                    add_finding(
                        findings,
                        category,
                        "compat_tests_in_product_root",
                        relative,
                        1,
                        "compatibility test source lives inside the product test tree",
                        relative,
                    )

    if strict_product and category in (None, "sibling_compat_tree"):
        sibling_compat = product_root.parent / "clipper2next_compat"
        if sibling_compat.exists():
            relative = relative_to_root(sibling_compat, root)
            add_finding(
                findings,
                category,
                "sibling_compat_tree",
                relative,
                1,
                "legacy compatibility package remains next to product source tree",
                relative,
            )

    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--category", choices=CATEGORIES)
    parser.add_argument("--strict-product", action="store_true")
    args = parser.parse_args()

    findings = find_product_compat_boundary_findings(
        Path(args.root),
        Path(args.cmake),
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
