#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


VERSION_H_RE = re.compile(r'CLIPPER2NEXT_VERSION\s*=\s*"(?P<version>[^"]+)"')
CMAKE_PROJECT_RE = re.compile(r"project\s*\(\s*clipper2next\s+VERSION\s+(?P<version>[0-9]+\.[0-9]+\.[0-9]+)")
CONAN_VERSION_RE = re.compile(
    r'^\s*version\s*=\s*"(?P<version>[^"]+)"\s*$', re.MULTILINE
)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def version_from_header(root: Path) -> str | None:
    match = VERSION_H_RE.search(read_text(root / "include" / "clipper2next" / "version.h"))
    return match.group("version") if match else None


def version_from_cmake(root: Path) -> str | None:
    match = CMAKE_PROJECT_RE.search(read_text(root / "CMakeLists.txt"))
    return match.group("version") if match else None


def version_from_vcpkg(root: Path) -> str | None:
    payload = json.loads(read_text(root / "vcpkg.json"))
    value = payload.get("version-string")
    return value if isinstance(value, str) else None


def version_from_conan(root: Path) -> str | None:
    match = CONAN_VERSION_RE.search(read_text(root / "conanfile.py"))
    return match.group("version") if match else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Check release metadata consistency.")
    parser.add_argument("--root", default=".")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    versions = {
        "version.h": version_from_header(root),
        "CMakeLists.txt": version_from_cmake(root),
        "vcpkg.json": version_from_vcpkg(root),
        "conanfile.py": version_from_conan(root),
    }
    errors: list[str] = []
    if len(set(versions.values())) != 1 or any(value is None for value in versions.values()):
        errors.append(f"version mismatch: {versions}")

    if errors:
        print("status=FAIL")
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("status=PASS")
    print(f"version={next(iter(versions.values()))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
