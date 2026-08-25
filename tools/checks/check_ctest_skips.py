#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


SKIP_PATTERNS = (
    re.compile(r"\[\s+SKIPPED\s+\]\s+(?P<name>\S+)"),
    re.compile(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(?P<name>.*?)\s+\.+\*\*\*Skipped"),
    re.compile(r"^\s*(?P<name>\S+)\s+\.+\s+\*\*\*Skipped"),
)

DEFAULT_REQUIRED_PATTERNS = (
    "ExternalGeometryCorpus.",
    "Clipper2NextLegacyCorpusTests.",
)


@dataclass(frozen=True)
class SkipRecord:
    name: str
    line: str


def parse_skips(text: str) -> list[SkipRecord]:
    records: list[SkipRecord] = []
    for line in text.splitlines():
        for pattern in SKIP_PATTERNS:
            match = pattern.search(line)
            if match is None:
                continue
            name = match.group("name")
            if name.isdigit():
                break
            records.append(SkipRecord(name=name, line=line.strip()))
            break
    return records


def unexpected_skips(skips: list[SkipRecord], allowed: tuple[str, ...]) -> list[SkipRecord]:
    result: list[SkipRecord] = []
    for record in skips:
        if not any(allowed_pattern in record.name for allowed_pattern in allowed):
            result.append(record)
    return result


def required_skip_failures(skips: list[SkipRecord], required: tuple[str, ...]) -> list[SkipRecord]:
    result: list[SkipRecord] = []
    for record in skips:
        if any(required_pattern in record.name for required_pattern in required):
            result.append(record)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail when ctest/gtest output contains disallowed skips.")
    parser.add_argument("log", type=Path)
    parser.add_argument("--allow", action="append", default=[])
    parser.add_argument("--required-pattern", action="append", default=list(DEFAULT_REQUIRED_PATTERNS))
    parser.add_argument("--forbid-all", action="store_true")
    args = parser.parse_args()

    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        print(f"failed to read ctest log: {error}", file=sys.stderr)
        return 2

    skips = parse_skips(text)
    required_failures = required_skip_failures(skips, tuple(args.required_pattern))
    unexpected = skips if args.forbid_all else unexpected_skips(skips, tuple(args.allow))

    failures = sorted({record.line for record in (*required_failures, *unexpected)})
    if failures:
        print("status=FAIL")
        print(f"skips={len(skips)}")
        for line in failures:
            print(line)
        return 1

    print("status=PASS")
    print(f"skips={len(skips)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
