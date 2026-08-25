#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path


VALID_SCOPES = ("all", "clipper_compat", "core", "engine_facade")

CLIPPER_COMPAT_HELPER_PATTERNS = (
    r"\bnamespace\s+details\b",
    r"\bPolyTreeToPaths64\b",
    r"\bPolyTreeToPathsD\b",
    r"\bCheckPolytreeFullyContainsChildren\b",
    r"\bMakePath(?:D|Z|ZD)?\b",
    r"\bTrimCollinear\b",
    r"\bEllipse\b",
    r"\bSimplifyPath(?:s)?\b",
    r"\bPath2ContainsPath1\b",
    r"\bRamerDouglasPeucker\b",
)

CORE_TEMPLATE_UTILITY_PATTERNS = (
    r"\btemplate\s*<[^>]+>\s*(?:inline\s+)?int\s+CrossProductSign\b",
    r"\btemplate\s*<[^>]+>\s*(?:inline\s+)?double\s+CrossProduct\b",
    r"\btemplate\s*<[^>]+>\s*(?:inline\s+)?double\s+PerpendicDistFromLineSqrd\b",
    r"\btemplate\s*<[^>]+>\s*(?:inline\s+)?PointInPolygonResult\s+PointInPolygon\b",
)

ENGINE_PUBLIC_FACADE_PATTERNS = (
    (r"^\s*protected\s*:", "public protected engine surface"),
    (r"\bfriend\s+class\b", "public friend-class engine access"),
    (r"\bvirtual\b", "public virtual engine/poly-tree surface"),
)


def has_non_template_minkowski_body(text: str) -> bool:
    return (
        "namespace detail" in text
        and re.search(r"\bPaths64\s+Minkowski\s*\(", text) is not None
        and re.search(r"\bfor\s*\(", text) is not None
    )


@dataclass(frozen=True)
class PublicWeightScanResult:
    findings: list[str]
    observations: list[str]


def has_default_clipper_compat_helpers(text: str) -> bool:
    return any(re.search(pattern, text, flags=re.MULTILINE | re.DOTALL) for pattern in CLIPPER_COMPAT_HELPER_PATTERNS)


def has_core_template_geometry_utilities(text: str) -> bool:
    return any(re.search(pattern, text, flags=re.MULTILINE | re.DOTALL) for pattern in CORE_TEMPLATE_UTILITY_PATTERNS)


def engine_public_facade_findings(header: Path, text: str) -> list[str]:
    findings: list[str] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for pattern, label in ENGINE_PUBLIC_FACADE_PATTERNS:
            if re.search(pattern, line):
                findings.append(f"{header}:{line_number}: {label}: {line.strip()}")
    return findings


def add_issue(
    *,
    strict: bool,
    findings: list[str],
    observations: list[str],
    message: str,
) -> None:
    if strict:
        findings.append(message)
    else:
        observations.append(message)


def scan_public_implementation_weight(
    root: Path,
    strict: bool = False,
    scope: str = "all",
    max_core_lines: int | None = None,
) -> PublicWeightScanResult:
    if scope not in VALID_SCOPES:
        raise ValueError(f"scope must be one of {', '.join(VALID_SCOPES)}")

    findings: list[str] = []
    observations: list[str] = []

    for header in sorted(root.rglob("*.h")):
        relative = header.relative_to(root).as_posix()
        if "/internal/" in f"/{relative}" or "/compat/" in f"/{relative}":
            continue
        text = header.read_text(encoding="utf-8")
        line_count = len(text.splitlines())
        name = header.name
        if name == "minkowski.h" and has_non_template_minkowski_body(text):
            findings.append(
                f"{header}:{line_count}: non-template Minkowski algorithm body remains in public header"
            )
            continue
        if name == "clipper.h" and scope in ("all", "clipper_compat") and has_default_clipper_compat_helpers(text):
            add_issue(
                strict=strict,
                findings=findings,
                observations=observations,
                message=f"{header}:{line_count}: observation: public inline compatibility helpers",
            )
        if name == "core.h" and scope in ("all", "core") and has_core_template_geometry_utilities(text):
            add_issue(
                strict=strict,
                findings=findings,
                observations=observations,
                message=f"{header}:{line_count}: observation: template-heavy public geometry utilities",
            )
        if name == "core.h" and scope in ("all", "core") and max_core_lines is not None:
            if line_count > max_core_lines:
                add_issue(
                    strict=strict,
                    findings=findings,
                    observations=observations,
                    message=(
                        f"{header}:{line_count}: exceeds core header line limit "
                        f"max_core_lines={max_core_lines}"
                    ),
                )
        if relative in ("engine.h", "clip/engine.h") and scope in ("all", "engine_facade"):
            for message in engine_public_facade_findings(header, text):
                add_issue(
                    strict=strict,
                    findings=findings,
                    observations=observations,
                    message=message,
                )

    return PublicWeightScanResult(findings=findings, observations=observations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Treat public implementation-weight observations as findings.",
    )
    parser.add_argument(
        "--scope",
        choices=VALID_SCOPES,
        default="all",
        help="Limit strict modernization gate to a planned cleanup scope.",
    )
    parser.add_argument("--max-core-lines", type=int)
    args = parser.parse_args()

    root = Path(args.root)
    result = scan_public_implementation_weight(
        root,
        strict=args.strict,
        scope=args.scope,
        max_core_lines=args.max_core_lines,
    )

    output_lines = []
    if result.findings:
        output_lines.append("[findings]")
        output_lines.extend(result.findings)
    if result.observations:
        output_lines.append("[observations]")
        output_lines.extend(result.observations)

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(output_lines) + ("\n" if output_lines else ""), encoding="utf-8")
    print(f"findings={len(result.findings)}")
    print(f"observations={len(result.observations)}")
    return 1 if result.findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
