#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "large_source_file",
    "large_public_header",
    "large_function",
    "deep_branch_nesting",
    "mixed_facade_internal_include",
)

SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
CPP_SUFFIXES = {".cpp", ".cc", ".cxx"}
CONTROL_START_RE = re.compile(r"^\s*(?:if|for|while|switch|catch)\b")
FUNCTION_HINT_RE = re.compile(r"\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[^({;]+)?\s*\{")
BRANCH_RE = re.compile(r"\b(?:if|for|while|switch|catch)\b")
PUBLIC_FACADE_INCLUDE_RE = re.compile(r"#\s*include\s*[<\"]clipper2next/(?:clipper|engine|offset|rectclip|triangulation)\.h[>\"]")
PRIVATE_INCLUDE_RE = re.compile(
    r"#\s*include\s*[<\"](?:batch|clip/engine|clip|core|geometry|memory|minkowski|offset|rectclip|support|triangulation)/private/"
)


@dataclass(frozen=True)
class ComplexityFinding:
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


def relevant_roots(root: Path) -> list[Path]:
    candidates = [root / "include", root / "src"]
    existing = [candidate for candidate in candidates if candidate.exists()]
    return existing if existing else [root]


def is_source_file(path: Path) -> bool:
    return path.is_file() and path.suffix in SOURCE_SUFFIXES


def is_product_file(path: Path, root: Path) -> bool:
    relative = relative_to_root(path, root)
    normalized = f"/{relative}"
    if any(part in normalized for part in ("/compat/", "/tests/", "/benchmarks/", "/tools/", "/oracle/")):
        return False
    if "/generated/" in normalized or relative.endswith(".generated.h"):
        return False
    return True


def is_public_header(path: Path, root: Path) -> bool:
    relative = relative_to_root(path, root)
    normalized = f"/{relative}"
    return (
        path.suffix in HEADER_SUFFIXES
        and relative.startswith("include/clipper2next/")
        and "/compat/" not in normalized
    )


def add_finding(
    findings: list[ComplexityFinding],
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
        ComplexityFinding(
            finding_category,
            relative_to_root(path, root),
            line_number,
            label,
            snippet.strip(),
        )
    )


def logical_line_count(lines: list[str]) -> int:
    return sum(1 for line in lines if line.strip() and line.strip() not in {"{", "}", "};"})


def starts_function(line: str) -> bool:
    stripped = line.strip()
    if not stripped or CONTROL_START_RE.match(stripped):
        return False
    if stripped.startswith(("#", "namespace ", "class ", "struct ", "enum ")):
        return False
    return FUNCTION_HINT_RE.search(stripped) is not None


def scan_large_functions(
    path: Path,
    root: Path,
    category: str | None,
    function_threshold: int,
) -> list[ComplexityFinding]:
    if category not in (None, "large_function") or function_threshold <= 0:
        return []
    text_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    findings: list[ComplexityFinding] = []
    active_start = 0
    active_lines: list[str] = []
    brace_depth = 0
    for line_number, line in enumerate(text_lines, start=1):
        code_line = line.split("//", 1)[0]
        if brace_depth == 0 and starts_function(code_line):
            active_start = line_number
            active_lines = [line]
            brace_depth = code_line.count("{") - code_line.count("}")
            if brace_depth <= 0:
                count = logical_line_count(active_lines)
                if count > function_threshold:
                    add_finding(
                        findings,
                        category,
                        "large_function",
                        path,
                        root,
                        active_start,
                        "function exceeds logical-line threshold",
                        f"logical_lines={count}, threshold={function_threshold}",
                    )
                brace_depth = 0
            continue
        if brace_depth > 0:
            active_lines.append(line)
            brace_depth += code_line.count("{") - code_line.count("}")
            if brace_depth <= 0:
                count = logical_line_count(active_lines)
                if count > function_threshold:
                    add_finding(
                        findings,
                        category,
                        "large_function",
                        path,
                        root,
                        active_start,
                        "function exceeds logical-line threshold",
                        f"logical_lines={count}, threshold={function_threshold}",
                    )
                brace_depth = 0
                active_lines = []
    return findings


def scan_branch_depth(
    path: Path,
    root: Path,
    category: str | None,
    branch_threshold: int,
) -> list[ComplexityFinding]:
    if category not in (None, "deep_branch_nesting") or branch_threshold <= 0:
        return []
    findings: list[ComplexityFinding] = []
    text_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    branch_stack: list[int] = []
    max_branch_depth = 0
    max_line = 1
    for line_number, line in enumerate(text_lines, start=1):
        code_line = line.split("//", 1)[0]
        if BRANCH_RE.search(code_line):
            indent = len(code_line) - len(code_line.lstrip())
            while branch_stack and indent <= branch_stack[-1]:
                branch_stack.pop()
            max_branch_depth = max(max_branch_depth, len(branch_stack) + 1)
            max_line = line_number
            if "{" in code_line and "}" not in code_line:
                branch_stack.append(indent)
    if max_branch_depth > branch_threshold:
        add_finding(
            findings,
            category,
            "deep_branch_nesting",
            path,
            root,
            max_line,
            "module exceeds branch nesting threshold",
            f"branch_depth={max_branch_depth}, threshold={branch_threshold}",
        )
    return findings


def scan_file(
    path: Path,
    root: Path,
    category: str | None,
    source_threshold: int,
    header_threshold: int,
    function_threshold: int,
    branch_threshold: int,
) -> list[ComplexityFinding]:
    findings: list[ComplexityFinding] = []
    if not is_product_file(path, root):
        return findings
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    line_count = len(lines)
    if path.suffix in CPP_SUFFIXES and line_count > source_threshold:
        add_finding(
            findings,
            category,
            "large_source_file",
            path,
            root,
            line_count,
            "product source exceeds line-count threshold",
            f"lines={line_count}, threshold={source_threshold}",
        )
    if is_public_header(path, root) and line_count > header_threshold:
        add_finding(
            findings,
            category,
            "large_public_header",
            path,
            root,
            line_count,
            "product public header exceeds line-count threshold",
            f"lines={line_count}, threshold={header_threshold}",
        )
    if PUBLIC_FACADE_INCLUDE_RE.search(text) and PRIVATE_INCLUDE_RE.search(text):
        add_finding(
            findings,
            category,
            "mixed_facade_internal_include",
            path,
            root,
            1,
            "module includes both public facade and internal implementation headers",
            "public_facade_include + internal_include",
        )
    findings.extend(scan_large_functions(path, root, category, function_threshold))
    findings.extend(scan_branch_depth(path, root, category, branch_threshold))
    return findings


def find_complexity_shape_findings(
    root: Path,
    category: str | None = None,
    source_threshold: int = 250,
    header_threshold: int = 180,
    function_threshold: int = 80,
    branch_threshold: int = 5,
    strict_product: bool = False,
) -> list[ComplexityFinding]:
    del strict_product
    root = root.resolve()
    findings: list[ComplexityFinding] = []
    for search_root in relevant_roots(root):
        for path in sorted(search_root.rglob("*")):
            if not is_source_file(path):
                continue
            findings.extend(
                scan_file(
                    path,
                    root,
                    category,
                    source_threshold,
                    header_threshold,
                    function_threshold,
                    branch_threshold,
                )
            )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--category", choices=CATEGORIES)
    parser.add_argument("--source-threshold", type=int, default=250)
    parser.add_argument("--header-threshold", type=int, default=180)
    parser.add_argument("--function-threshold", type=int, default=80)
    parser.add_argument("--branch-threshold", type=int, default=5)
    parser.add_argument("--strict-product", action="store_true")
    args = parser.parse_args()

    findings = find_complexity_shape_findings(
        Path(args.root),
        category=args.category,
        source_threshold=args.source_threshold,
        header_threshold=args.header_threshold,
        function_threshold=args.function_threshold,
        branch_threshold=args.branch_threshold,
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
