#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


LITERAL_PATTERNS = (
    "clipper2next/internal",
    "/private/",
    "batch/private/",
    "clip/private/",
    "clip/engine/private/",
    "core/private/",
    "geometry/private/",
    "memory/private/",
    "minkowski/private/",
    "offset/private/",
    "rectclip/private/",
    "support/private/",
    "triangulation/private/",
)

REGEX_PATTERNS = (
    ("internal namespace qualifier", re.compile(r"\binternal\s*::")),
    ("internal namespace declaration", re.compile(r"^\s*namespace\s+internal\b")),
)

REQUIRED_PUBLIC_HEADERS = {
    "minkowski/request.h": "minkowski request API must live in minkowski/request.h",
    "rectclip/request.h": "rectclip request API must live in rectclip/request.h",
    "triangulation/request.h": "triangulation request API must live in triangulation/request.h",
}

FORBIDDEN_PUBLIC_HEADERS = {
    "api/requests.h": "api/requests.h is an obsolete aggregate header; include module request headers",
    "clip/engine.h": "clip/engine.h is obsolete; use clip/types.h or clip/request.h",
    "core/rect_algorithms.h": "core/rect_algorithms.h is obsolete; keep Rect value API in core/rect.h",
    "minkowski/engine.h": "minkowski/engine.h is obsolete; use minkowski/request.h",
    "offset/engine.h": "offset/engine.h is obsolete; use offset.h or offset/request.h",
    "rectclip/clip.h": "rectclip/clip.h is obsolete; use rectclip/request.h",
    "triangulation/engine.h": "triangulation/engine.h is obsolete; use triangulation/request.h",
}

FORBIDDEN_HEADER_INCLUDES = {
    "clip.h": (
        (
            'clipper2next/offset/engine.h',
            "clip facade must not include offset facade",
        ),
        (
            'clipper2next/rectclip/clip.h',
            "clip facade must not include rectclip facade",
        ),
    ),
}

FORBIDDEN_HEADER_PATTERNS = {
    "api/requests.h": (
        (
            "aggregate request header owns module request type",
            re.compile(r"\bstruct\s+(?:clip_request64|prepared_clip_request64|offset_request64)\b"),
        ),
    ),
}

PUBLIC_INCLUDE_PATTERN = re.compile(r'#\s*include\s+[<"]clipper2next/([^">]+)[">]')


def public_header_include_graph(root: Path) -> dict[str, set[str]]:
    graph: dict[str, set[str]] = {}
    for path in sorted(root.rglob("*.h")):
        relative = path.relative_to(root).as_posix()
        if "internal" in path.relative_to(root).parts:
            continue
        graph[relative] = set()
        text = path.read_text(encoding="utf-8")
        for match in PUBLIC_INCLUDE_PATTERN.finditer(text):
            included = match.group(1)
            if (root / included).exists():
                graph[relative].add(included)
    return graph


def direct_public_include_cycles(root: Path) -> list[tuple[str, str]]:
    graph = public_header_include_graph(root)
    cycles = []
    for source, targets in sorted(graph.items()):
        for target in sorted(targets):
            if source < target and source in graph.get(target, set()):
                cycles.append((source, target))
    return cycles


def scan_public_header_shape(root: Path) -> list[str]:
    root = root.resolve()
    findings = []
    for relative, label in sorted(REQUIRED_PUBLIC_HEADERS.items()):
        if not (root / relative).exists():
            findings.append(f"{root / relative}: missing required public header: {label}")
    for relative, label in sorted(FORBIDDEN_PUBLIC_HEADERS.items()):
        if (root / relative).exists():
            findings.append(f"{root / relative}: obsolete public header: {label}")
    for path in sorted(root.rglob("*.h")):
        relative = path.relative_to(root).as_posix()
        if "internal" in path.relative_to(root).parts:
            continue
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), start=1):
            for pattern in LITERAL_PATTERNS:
                if pattern in line:
                    findings.append(f"{path}:{line_number}: {pattern}: {line.strip()}")
            for label, pattern in REGEX_PATTERNS:
                if pattern.search(line):
                    findings.append(f"{path}:{line_number}: {label}: {line.strip()}")
            for include, label in FORBIDDEN_HEADER_INCLUDES.get(relative, ()):
                if include in line:
                    findings.append(f"{path}:{line_number}: {label}: {line.strip()}")
            for label, pattern in FORBIDDEN_HEADER_PATTERNS.get(relative, ()):
                if pattern.search(line):
                    findings.append(f"{path}:{line_number}: {label}: {line.strip()}")
    for source, target in direct_public_include_cycles(root):
        findings.append(
            f"{root / source}: public header include cycle: {source} <-> {target}"
        )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    root = Path(args.root)
    findings = scan_public_header_shape(root)

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(findings) + ("\n" if findings else ""), encoding="utf-8")
    print(f"findings={len(findings)}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
