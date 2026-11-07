#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.evidence_contract import load_contract


DEFAULT_EXTERNAL_TEST = Path("tests/oracle/external_geometry_corpus_tests.cpp")

PROFILE_LEGACY_TOKENS: dict[str, tuple[str, ...]] = {
    "overlay": ("Clipper2Lib::Clipper64", ".Execute("),
    "offset": ("Clipper2Lib::ClipperOffset", ".Execute("),
    "rectclip": ("Clipper2Lib::RectClip",),
    "rectclip-lines": ("Clipper2Lib::RectClipLines",),
    "open-path-overlay": ("Clipper2Lib::Clipper64", ".AddOpenSubject(", ".Execute("),
    "minkowski": ("Clipper2Lib::MinkowskiSum",),
    "triangulation": ("Clipper2Lib::Triangulate",),
    "bounds": ("Clipper2Lib::GetBounds",),
    "simplification": ("Clipper2Lib::SimplifyPaths",),
    "collinear-trimming": ("Clipper2Lib::TrimCollinear",),
    "point-in-polygon": ("Clipper2Lib::PointInPolygon",),
    "scaling": ("Clipper2Lib::ScalePaths",),
    "translation": ("Clipper2Lib::TranslatePaths",),
    "clip-tree": ("Clipper2Lib::Clipper64", ".Execute("),
    "polytree": ("Clipper2Lib::Clipper64", ".Execute("),
    "batch": ("Clipper2Lib::Clipper64", ".Execute("),
}

MARKER_RE = re.compile(r'CLIPPER2NEXT_CONSUMES_VERIFICATION_PROFILE\("([^"]+)"\)')
TEST_RE = re.compile(r"TEST\s*\(\s*ExternalGeometryCorpus\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\{")
HELPER_RE = re.compile(
    r"(?:\[\[nodiscard\]\]\s*)?auto\s+([A-Za-z_][A-Za-z0-9_]*)\s*"
    r"\([^;{}]*\)\s*(?:->\s*[^{}]+)?\{",
    re.MULTILINE,
)


@dataclass(frozen=True)
class NamespaceAlias:
    name: str
    target: str
    position: int
    scope: tuple[str, ...]


@dataclass(frozen=True)
class Block:
    name: str
    body: str
    start: int
    end: int
    source: str = ""
    scope: tuple[str, ...] = ()
    aliases: tuple[NamespaceAlias, ...] = ()
    lexical_scopes: tuple[tuple[int, int, str], ...] = ()
    body_start: int = 0


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


def code_only(text: str) -> str:
    # Preserve offsets for namespace/block containment; comments cannot prove calls.
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])\'',
                  lambda match: re.sub(r"[^\n]", " ", match.group()), text, flags=re.DOTALL)


def scope_at(scopes: tuple[tuple[int, int, str], ...], position: int) -> tuple[str, ...]:
    return tuple(part for start, end, name in scopes if start < position < end
                 for part in name.split("::"))


def lexical_scopes(code: str, source: str) -> tuple[tuple[int, int, str], ...]:
    namespaces = {match.end() - 1: match.group(1) or f"@{source}:{match.start()}"
                  for match in re.finditer(r"namespace(?:\s+([\w:]+))?\s*\{", code)}
    stack = []
    scopes = []
    for position, char in enumerate(code):
        if char == "{":
            stack.append(position)
        elif char == "}":
            if not stack:
                raise ValueError(f"unmatched closing brace at offset {position}")
            opening = stack.pop()
            # Local blocks cannot leak aliases into their siblings or parent.
            scopes.append((opening, position, namespaces.get(opening, f"${opening}")))
    if stack:
        raise ValueError(f"unmatched opening brace at offset {stack[-1]}")
    return tuple(sorted(scopes))


def extract_blocks(text: str, regex: re.Pattern[str], source: str = "") -> list[Block]:
    code = code_only(text)
    scopes = lexical_scopes(code, source)
    aliases = tuple(NamespaceAlias(match.group(1), match.group(2).strip(), match.start(),
                                   scope_at(scopes, match.start()))
                    for match in re.finditer(r"namespace\s+(\w+)\s*=\s*([^;{}]+);", code))
    blocks = []
    for match in regex.finditer(code):
        opening = match.end() - 1
        closing = find_matching_brace(code, opening)
        scope = scope_at(scopes, opening)
        if any(part.startswith("$") for part in scope):
            # Class members/local definitions are outside the free-helper subset.
            continue
        blocks.append(Block(match.group(1), text[opening + 1:closing], match.start(),
                            closing + 1, source, scope, aliases, scopes, opening + 1))
    return blocks


def visible_aliases(block: Block, name: str, scope: tuple[str, ...],
                    position: int) -> list[NamespaceAlias]:
    candidates = [alias for alias in block.aliases if alias.name == name
                  and alias.position < position and scope[:len(alias.scope)] == alias.scope]
    if not candidates:
        return []
    depth = max(len(alias.scope) for alias in candidates)
    return [alias for alias in candidates if len(alias.scope) == depth]


def canonical_code(block: Block) -> str:
    def qualify(match: re.Match[str]) -> str:
        name, rest = match.group().split("::", 1)
        position = block.body_start + match.start()
        aliases = visible_aliases(block, name, scope_at(block.lexical_scopes, position), position)
        if not aliases:
            return match.group()
        targets = {re.sub(r"\s*::\s*", "::", alias.target) for alias in aliases}
        target = next(iter(targets)) if len(targets) == 1 else ""
        alias = aliases[0]
        first = target.removeprefix("::").split("::", 1)[0]
        target_scope = () if target.startswith("::") else alias.scope
        # Only direct namespace targets are supported. An ambiguous declaration,
        # macro target or alias chain cannot fall back to an outer legacy alias.
        if (not re.fullmatch(r"(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*", target)
                or visible_aliases(block, first, target_scope, alias.position)):
            return "__unresolved_alias__::" + rest
        return target.removeprefix("::") + "::" + rest

    return re.sub(r"(?<![\w:])(?:[A-Za-z_]\w*::)+[A-Za-z_]\w*", qualify,
                  code_only(block.body))


def free_calls(body: str) -> Iterator[str]:
    for match in re.finditer(r"(?<![\w:])((?:\w+::)*\w+)\s*\(", body):
        prefix = body[:match.start()].rstrip()
        if prefix.endswith("template"):
            prefix = prefix[:-len("template")].rstrip()
        if prefix.endswith((".", "->", ".*", "->*")):
            continue
        yield match.group(1)


def resolve_helper(name: str, caller: Block, helpers: list[Block]) -> Block | None:
    scope = tuple(part for part in caller.scope if not part.startswith("@"))
    for depth in range(len(scope), -1, -1):
        wanted = "::".join((*scope[:depth], name))
        candidates = []
        for helper in helpers:
            if helper.source != caller.source and any(p.startswith("@") for p in helper.scope):
                continue
            qualified = "::".join((*[p for p in helper.scope if not p.startswith("@")],
                                   helper.name))
            if qualified == wanted:
                candidates.append(helper)
        if candidates:
            # Ambiguous definitions are not evidence of a particular live call.
            return candidates[0] if len(candidates) == 1 else None
    return None


def reachable_text(start: Block, helpers: list[Block]) -> str:
    reached = []
    pending = [start]
    seen: set[tuple[str, int]] = set()
    while pending:
        block = pending.pop()
        identity = (block.source, block.start)
        if identity in seen:
            continue
        seen.add(identity)
        body = canonical_code(block)
        reached.append(body)
        for name in free_calls(body):
            helper = resolve_helper(name, block, helpers)
            if helper is not None:
                pending.append(helper)
    return "\n".join(reached)


def linked_oracle_sources(root: Path) -> list[Path]:
    cmake = root / "tests/CMakeLists.txt"
    if not cmake.exists():
        return []
    text = re.sub(r"#[^\n]*", "", cmake.read_text(encoding="utf-8"))
    target = re.search(r"add_executable\(\s*clipper2next_oracle_tests\b([^)]*)\)", text)
    if not target:
        return []
    # Only explicitly linked translation units of this actual oracle target.
    sources = re.findall(r"(?<![\w/])[\w/-]+\.cpp\b", target.group(1))
    return [root / "tests" / source for source in sources]


def check_external_verification_legacy_oracles(
    path: Path, profiles: list[str], support_sources: list[Path] | None = None
) -> list[str]:
    text = path.read_text(encoding="utf-8")
    tests = extract_blocks(text, TEST_RE, str(path))
    helpers = extract_blocks(text, HELPER_RE, str(path))
    for source in support_sources or []:
        if source.exists():
            helpers.extend(extract_blocks(source.read_text(encoding="utf-8"), HELPER_RE, str(source)))
    profile_bodies = {}
    for test in tests:
        for profile in MARKER_RE.findall(test.body):
            profile_bodies[profile] = reachable_text(test, helpers)
    findings: list[str] = []
    for profile in profiles:
        body = profile_bodies.get(profile)
        if body is None:
            findings.append(f"{profile}: missing external verification profile consumer marker")
            continue
        missing = [token for token in PROFILE_LEGACY_TOKENS[profile] if token not in body]
        if missing:
            findings.append(f"{profile}: missing live legacy oracle token(s): {', '.join(missing)}")
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
    findings = check_external_verification_legacy_oracles(
        source, profiles, linked_oracle_sources(args.root))
    if findings:
        print("status=FAIL")
        for finding in findings:
            print(finding)
        return 1

    print(f"status=PASS profiles={len(profiles)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
