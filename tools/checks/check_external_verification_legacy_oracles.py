#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.evidence_contract import load_contract


DEFAULT_EXTERNAL_TEST = Path("tests/oracle/external_geometry_corpus_tests.cpp")

PROFILE_LEGACY_TOKENS: dict[str, tuple[str, ...]] = {
    "overlay": ("legacy::Clipper64", ".Execute("),
    "offset": ("legacy::ClipperOffset", ".Execute("),
    "rectclip": ("legacy::RectClip",),
    "rectclip-lines": ("legacy::RectClipLines",),
    "open-path-overlay": ("legacy::Clipper64", ".AddOpenSubject(", ".Execute("),
    "minkowski": ("legacy::MinkowskiSum",),
    "triangulation": ("legacy::Triangulate",),
    "bounds": ("legacy::GetBounds",),
    "simplification": ("legacy::SimplifyPaths",),
    "collinear-trimming": ("legacy::TrimCollinear",),
    "point-in-polygon": ("legacy::PointInPolygon",),
    "scaling": ("legacy::ScalePaths",),
    "translation": ("legacy::TranslatePaths",),
    "clip-tree": ("legacy::Clipper64", ".Execute("),
    "polytree": ("legacy::Clipper64", ".Execute("),
    "batch": ("legacy::Clipper64", ".Execute("),
}

MARKER_RE = re.compile(r'CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE\("([^"]+)"\)')
TEST_RE = re.compile(r"TEST\s*\(\s*ExternalGeometryCorpus\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\{")
HELPER_RE = re.compile(
    r"(?:\[\[nodiscard\]\]\s*)?auto\s+([A-Za-z_][A-Za-z0-9_]*)\s*"
    r"\([^;{}]*\)\s*(?:->\s*[^{}]+)?\{",
    re.MULTILINE,
)


@dataclass(frozen=True)
class Block:
    name: str
    body: str
    start: int
    end: int


def find_matching_brace(text: str, opening_index: int) -> int:
    depth = 0
    for index in range(opening_index, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(f"unmatched brace at byte offset {opening_index}")


def extract_blocks(text: str, regex: re.Pattern[str]) -> list[Block]:
    blocks: list[Block] = []
    for match in regex.finditer(text):
        opening = match.end() - 1
        closing = find_matching_brace(text, opening)
        blocks.append(Block(match.group(1), text[opening + 1 : closing], match.start(), closing + 1))
    return blocks


def reachable_text(start_body: str, helpers: dict[str, str]) -> str:
    reached = start_body
    pending = [start_body]
    seen: set[str] = set()
    while pending:
        body = pending.pop()
        for name, helper_body in helpers.items():
            if name in seen:
                continue
            if re.search(rf"\b{re.escape(name)}\s*\(", body):
                seen.add(name)
                reached += "\n" + helper_body
                pending.append(helper_body)
    return reached


def profiles_from_text(text: str, tests: list[Block], helpers: dict[str, str]) -> dict[str, str]:
    found: dict[str, str] = {}
    for test in tests:
        markers = MARKER_RE.findall(test.body)
        if not markers:
            continue
        expanded = reachable_text(test.body, helpers)
        for profile in markers:
            found[profile] = expanded
    return found


def check_external_verification_legacy_oracles(path: Path, profiles: list[str]) -> list[str]:
    text = path.read_text(encoding="utf-8")
    tests = extract_blocks(text, TEST_RE)
    helpers = {block.name: block.body for block in extract_blocks(text, HELPER_RE)}
    profile_bodies = profiles_from_text(text, tests, helpers)

    findings: list[str] = []
    for profile in profiles:
        body = profile_bodies.get(profile)
        if body is None:
            findings.append(f"{profile}: missing external verification profile consumer marker")
            continue
        tokens = PROFILE_LEGACY_TOKENS[profile]
        missing = [token for token in tokens if token not in body]
        if missing:
            findings.append(
                f"{profile}: missing live legacy oracle token(s): {', '.join(missing)}"
            )
    return findings


def parse_profiles(value: str) -> list[str]:
    profiles = [item.strip() for item in value.split(",") if item.strip()]
    unknown = [profile for profile in profiles if profile not in PROFILE_LEGACY_TOKENS]
    if unknown:
        raise argparse.ArgumentTypeError("unknown profile(s): " + ", ".join(unknown))
    return profiles


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Require external verification corpus consumers to reach live legacy oracles."
    )
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--source", type=Path, default=DEFAULT_EXTERNAL_TEST)
    parser.add_argument(
        "--profiles",
        type=parse_profiles,
        default=None,
        help="Comma-separated verification profile names to check.",
    )
    args = parser.parse_args(argv)

    contract_profiles = list(load_contract().profiles)
    token_profiles = set(PROFILE_LEGACY_TOKENS)
    if token_profiles != set(contract_profiles):
        missing = sorted(set(contract_profiles) - token_profiles)
        obsolete = sorted(token_profiles - set(contract_profiles))
        print(
            "status=FAIL profile token mapping does not match evidence contract: "
            f"missing={missing} obsolete={obsolete}"
        )
        return 1
    profiles = contract_profiles if args.profiles is None else args.profiles

    source = args.source if args.source.is_absolute() else args.root / args.source
    findings = check_external_verification_legacy_oracles(source, profiles)
    if findings:
        print("status=FAIL")
        for finding in findings:
            print(finding)
        return 1

    print(f"status=PASS profiles={len(profiles)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
