#!/usr/bin/env python3
import argparse
from pathlib import Path


STABLE_PUBLIC_HEADERS = [
    "clipper2next/api/error.h",
    "clipper2next/api/execution.h",
    "clipper2next/api/export.h",
    "clipper2next/api/memory.h",
    "clipper2next/api/options.h",
    "clipper2next/api/result.h",
    "clipper2next/batch.h",
    "clipper2next/clip.h",
    "clipper2next/clip/borrowed_paths.h",
    "clipper2next/clip/path_source.h",
    "clipper2next/clip/request.h",
    "clipper2next/clip/topology.h",
    "clipper2next/clip/topology_writer.h",
    "clipper2next/clip/types.h",
    "clipper2next/clipper.h",
    "clipper2next/core.h",
    "clipper2next/core/fill_rule.h",
    "clipper2next/core/path.h",
    "clipper2next/core/path_set.h",
    "clipper2next/core/path_set_builder.h",
    "clipper2next/core/point.h",
    "clipper2next/core/rect.h",
    "clipper2next/geometry.h",
    "clipper2next/geometry/algorithms.h",
    "clipper2next/geometry/core.h",
    "clipper2next/geometry/line_intersections.h",
    "clipper2next/geometry/math.h",
    "clipper2next/geometry/path_transforms.h",
    "clipper2next/geometry/predicates.h",
    "clipper2next/geometry/scale.h",
    "clipper2next/geometry/scaling.h",
    "clipper2next/geometry/scaling_policy.h",
    "clipper2next/geometry/translate.h",
    "clipper2next/geotypes/geotypes.hpp",
    "clipper2next/geotypes/coordinate.hpp",
    "clipper2next/geotypes/coordinate_arithmetic.hpp",
    "clipper2next/geotypes/coordinate_cast.hpp",
    "clipper2next/geotypes/coordinate_midpoint.hpp",
    "clipper2next/geotypes/path.hpp",
    "clipper2next/geotypes/point.hpp",
    "clipper2next/geotypes/rect.hpp",
    "clipper2next/geotypes/topology.hpp",
    "clipper2next/minkowski.h",
    "clipper2next/minkowski/request.h",
    "clipper2next/offset.h",
    "clipper2next/offset/borrowed.h",
    "clipper2next/offset/builder.h",
    "clipper2next/offset/operations.h",
    "clipper2next/offset/request.h",
    "clipper2next/offset/types.h",
    "clipper2next/polygon/poly_tree.h",
    "clipper2next/polygon/tree.h",
    "clipper2next/rectclip.h",
    "clipper2next/rectclip/request.h",
    "clipper2next/triangulation.h",
    "clipper2next/triangulation/request.h",
    "clipper2next/version.h",
]

INSTALLED_SUPPORT_HEADERS = [
]

INSTALLED_HEADERS = [*STABLE_PUBLIC_HEADERS, *INSTALLED_SUPPORT_HEADERS]


def find_install_public_header_findings(root: Path) -> list[str]:
    include_root = root / "include"
    missing = [header for header in INSTALLED_HEADERS if not (include_root / header).exists()]
    internal_dir = include_root / "clipper2next" / "internal"
    compat_dir = include_root / "clipper2next" / "compat"
    allowed_headers = set(INSTALLED_HEADERS)

    findings: list[str] = []
    for header in missing:
        findings.append(f"missing required installed header: {header}")

    clipper2next_root = include_root / "clipper2next"
    if clipper2next_root.exists():
        for header_path in sorted(clipper2next_root.rglob("*")):
            if not header_path.is_file() or header_path.suffix not in {".h", ".hpp", ".hh"}:
                continue
            relative = header_path.relative_to(include_root).as_posix()
            if relative not in allowed_headers:
                findings.append(f"unexpected installed public header: {relative}")

    if internal_dir.exists():
        findings.append(f"internal headers installed unexpectedly: {internal_dir}")

    if compat_dir.exists():
        findings.append(f"compat headers installed unexpectedly: {compat_dir}")

    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-root", required=True)
    args = parser.parse_args()

    root = Path(args.install_root)
    findings = find_install_public_header_findings(root)
    for finding in findings:
        print(finding)
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
