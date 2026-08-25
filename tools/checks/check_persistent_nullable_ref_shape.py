#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


CATEGORIES = (
    "persistent_nullable_ref",
    "nullable_ref_alias",
)

SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx"}
TYPE_START_RE = re.compile(r"\b(?:class|struct)\s+[A-Za-z_][A-Za-z0-9_]*\b")
TYPE_ALIAS_RE = re.compile(r"\b(?:using|typedef)\b")
NULLABLE_REF_RE = re.compile(r"\bnullable_ref\s*<")
APPROVED_PARTS = (
    "engine_reference.h",
)


@dataclass(frozen=True)
class NullableRefFinding:
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


def is_product_topology_file(path: Path, root: Path) -> bool:
    relative = relative_to_root(path, root)
    normalized = f"/{relative}"
    if any(part in normalized for part in ("/compat/", "/tests/", "/benchmarks/", "/tools/")):
        return False
    return not any(relative.endswith(part) for part in APPROVED_PARTS)


def add_finding(
    findings: list[NullableRefFinding],
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
        NullableRefFinding(
            finding_category,
            relative_to_root(path, root),
            line_number,
            label,
            snippet.strip(),
        )
    )


def scan_file(path: Path, root: Path, category: str | None) -> list[NullableRefFinding]:
    findings: list[NullableRefFinding] = []
    if not is_product_topology_file(path, root):
        return findings
    text = path.read_text(encoding="utf-8", errors="replace")
    type_depth = 0
    for line_number, line in enumerate(text.splitlines(), start=1):
        code_line = line.split("//", 1)[0]
        starts_type = TYPE_START_RE.search(code_line) is not None
        in_type = type_depth > 0 or starts_type
        if TYPE_ALIAS_RE.search(code_line) and NULLABLE_REF_RE.search(code_line):
            add_finding(
                findings,
                category,
                "nullable_ref_alias",
                path,
                root,
                line_number,
                "nullable_ref alias persists in product topology",
                line,
            )
        elif in_type and NULLABLE_REF_RE.search(code_line):
            add_finding(
                findings,
                category,
                "persistent_nullable_ref",
                path,
                root,
                line_number,
                "nullable_ref field persists in product topology",
                line,
            )
        if starts_type or type_depth > 0:
            type_depth += code_line.count("{")
            type_depth -= code_line.count("}")
            if type_depth < 0:
                type_depth = 0
    return findings


def find_persistent_nullable_ref_findings(
    root: Path,
    category: str | None = None,
    strict_product: bool = False,
) -> list[NullableRefFinding]:
    del strict_product
    root = root.resolve()
    findings: list[NullableRefFinding] = []
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

    findings = find_persistent_nullable_ref_findings(
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
