#!/usr/bin/env python3
"""Generate deterministic release verification and benchmark corpus profiles."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import re
import shutil
import sys
import tempfile
from collections import defaultdict, deque
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.checks import check_geometry_corpus_profiles as profile_check
from tools.release.evidence_contract import (
    DEFAULT_CONTRACT_PATH,
    ProfileContract,
    ReleaseEvidenceContract,
    load_contract,
)


GENERATOR_NAME = "generate_release_profiles"
GENERATOR_VERSION = 1
SELECTOR_METHOD = "sha256-stratified-v1"
LEGACY_ENGINE_VERSION = "2.0.1"
HOLDOUT_MODULUS = 5
HIGH_COMPLEXITY_POINT_COUNT = 512
DEFAULT_UNGATED_CASE_COUNT = 32

_NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
_POINT_RE = re.compile(rf"({_NUMBER})\s+({_NUMBER})")
_INNER_PATH_RE = re.compile(r"\(([^()]*)\)")
_SLUG_RE = re.compile(r"[^a-z0-9]+")
_OPTION_KEYS = {
    "fill_rule",
    "join_type",
    "end_type",
    "is_closed",
    "preserve_collinear",
    "reverse_solution",
}
_OVERLAY_OPERATIONS = ("intersection", "union", "difference", "xor")
_FILL_RULES = ("even_odd", "non_zero", "positive", "negative")
_JOIN_TYPES = ("miter", "round", "square")
_END_TYPES = ("polygon", "joined", "butt", "square", "round")
_RECT_SCENARIOS = ("contained", "disjoint", "crossing", "boundary")
_POLYGON_TYPES = {"Polygon", "MultiPolygon"}
_LINE_TYPES = {"LineString", "MultiLineString"}


class ProfileGenerationError(ValueError):
    """Raised when release profiles cannot be generated without weakening evidence."""


@dataclass(frozen=True)
class CaseSpec:
    profile_id: str
    operation: str
    scenario: str
    full_id: str
    inputs: dict[str, object]
    source: dict[str, object]
    tags: tuple[str, ...]
    complexity: dict[str, object]
    parameters: dict[str, object]


@dataclass(frozen=True)
class GenerationResult:
    profile_counts: Mapping[str, int]
    output_root: Path


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number {value}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def canonical_json_digest(value: object) -> str:
    encoded = json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _stable_digest(*parts: object) -> str:
    encoded = "\0".join(str(part) for part in parts).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def stable_partition(full_id: str) -> str:
    """Assign one base ID to a stable 20% release holdout partition."""

    digest = hashlib.sha256(f"partition\0{full_id}".encode("utf-8")).digest()
    bucket = int.from_bytes(digest[:4], "big") % HOLDOUT_MODULUS
    return "release-holdout" if bucket == 0 else "development"


def load_eligibility_records(path: Path) -> list[dict[str, Any]]:
    """Load strict normalized JSONL and reject ambiguous or duplicate IDs."""

    if not path.is_file():
        raise ProfileGenerationError(f"missing eligibility file: {path}")

    records: list[dict[str, Any]] = []
    ids: set[str] = set()
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(
                    line,
                    object_pairs_hook=_unique_object,
                    parse_constant=_reject_json_constant,
                )
            except (json.JSONDecodeError, ValueError) as error:
                raise ProfileGenerationError(
                    f"{path}:{line_number}: invalid eligibility JSON: {error}"
                ) from error
            if not isinstance(record, dict):
                raise ProfileGenerationError(
                    f"{path}:{line_number}: eligibility record must be an object"
                )
            record_id = record.get("id")
            if not isinstance(record_id, str) or not record_id:
                raise ProfileGenerationError(
                    f"{path}:{line_number}: eligibility record has no non-empty id"
                )
            if record_id in ids:
                raise ProfileGenerationError(
                    f"{path}:{line_number}: duplicate eligibility id {record_id!r}"
                )
            if record.get("status") != "normalized":
                raise ProfileGenerationError(
                    f"{path}:{line_number}: eligibility record {record_id!r} "
                    "is not normalized"
                )
            ids.add(record_id)
            records.append(record)

    if not records:
        raise ProfileGenerationError(f"eligibility file contains no records: {path}")
    return records


def _record_id(record: Mapping[str, Any]) -> str:
    value = record.get("id")
    if not isinstance(value, str) or not value:
        raise ProfileGenerationError("internal error: selected record has no id")
    return value


def _record_source(record: Mapping[str, Any], transformation: str) -> dict[str, object]:
    raw = record.get("source")
    source = copy.deepcopy(raw) if isinstance(raw, dict) else {}
    source["base_record_id"] = _record_id(record)
    source["transformation"] = transformation
    if not source.get("source_id"):
        source["source_id"] = "unknown"
    return source


def _record_tags(
    record: Mapping[str, Any],
    profile_id: str,
    scenario: str,
    *extra: str,
) -> tuple[str, ...]:
    raw = record.get("tags")
    tags = (
        {tag for tag in raw if isinstance(tag, str) and tag}
        if isinstance(raw, list)
        else set()
    )
    tags.update({profile_id, scenario, *extra})
    return tuple(sorted(tags))


def _shape_geometry(record: Mapping[str, Any]) -> dict[str, Any] | None:
    geometry = record.get("geometry")
    return geometry if isinstance(geometry, dict) else None


def _shape_geometry_type(record: Mapping[str, Any]) -> str:
    geometry = _shape_geometry(record)
    value = geometry.get("geometry_type") if geometry is not None else None
    return value if isinstance(value, str) else ""


def _shape_wkt(record: Mapping[str, Any]) -> str:
    geometry = _shape_geometry(record)
    value = geometry.get("wkt") if geometry is not None else None
    return value if isinstance(value, str) else ""


def _overlay_input(record: Mapping[str, Any], side: str) -> dict[str, Any] | None:
    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        return None
    value = inputs.get(side)
    return value if isinstance(value, dict) else None


def _overlay_wkt(record: Mapping[str, Any], side: str) -> str:
    geometry = _overlay_input(record, side)
    value = geometry.get("wkt") if geometry is not None else None
    return value if isinstance(value, str) else ""


def _overlay_geometry_type(record: Mapping[str, Any]) -> str:
    lhs = _overlay_input(record, "lhs")
    value = lhs.get("geometry_type") if lhs is not None else None
    return value if isinstance(value, str) else ""


def _wkt_points(wkt: object) -> list[tuple[Decimal, Decimal]]:
    if not isinstance(wkt, str):
        return []
    points: list[tuple[Decimal, Decimal]] = []
    for match in _POINT_RE.finditer(wkt):
        try:
            points.append((Decimal(match.group(1)), Decimal(match.group(2))))
        except InvalidOperation as error:
            raise ProfileGenerationError(f"invalid WKT coordinate in {wkt[:80]!r}") from error
    return points


@lru_cache(maxsize=None)
def _wkt_point_count(wkt: str) -> int:
    return sum(1 for _ in _POINT_RE.finditer(wkt))


@lru_cache(maxsize=None)
def _wkt_path_count(wkt: str) -> int:
    return sum(1 for match in _INNER_PATH_RE.finditer(wkt) if _wkt_points(match.group(1)))


@lru_cache(maxsize=None)
def _wkt_bbox(wkt: str) -> tuple[Decimal, Decimal, Decimal, Decimal]:
    points = _wkt_points(wkt)
    if not points:
        raise ProfileGenerationError(f"WKT contains no coordinates: {wkt[:80]!r}")
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return min(xs), min(ys), max(xs), max(ys)


def _format_decimal(value: Decimal) -> str:
    if value == 0:
        return "0"
    formatted = format(value, "f")
    if "." in formatted:
        formatted = formatted.rstrip("0").rstrip(".")
    return formatted


def _parse_path_text(text: str) -> list[tuple[Decimal, Decimal]]:
    points = _wkt_points(text)
    if not points:
        raise ProfileGenerationError(f"invalid or empty WKT path: {text[:80]!r}")
    return points


def _map_wkt_paths(
    wkt: str,
    transform: Callable[
        [list[tuple[Decimal, Decimal]], int],
        list[tuple[Decimal, Decimal]],
    ],
) -> str:
    path_index = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal path_index
        points = _parse_path_text(match.group(1))
        transformed = transform(points, path_index)
        path_index += 1
        if not transformed:
            raise ProfileGenerationError("WKT transformation produced an empty path")
        body = ", ".join(
            f"{_format_decimal(x)} {_format_decimal(y)}" for x, y in transformed
        )
        return f"({body})"

    transformed, count = _INNER_PATH_RE.subn(replace, wkt)
    if count == 0:
        raise ProfileGenerationError(f"unsupported WKT structure: {wkt[:80]!r}")
    return transformed


def _translate_wkt(wkt: str, dx: Decimal, dy: Decimal) -> str:
    return _map_wkt_paths(
        wkt,
        lambda points, _: [(x + dx, y + dy) for x, y in points],
    )


def _scale_wkt(wkt: str, factor: Decimal) -> str:
    return _map_wkt_paths(
        wkt,
        lambda points, _: [(x * factor, y * factor) for x, y in points],
    )


@lru_cache(maxsize=None)
def _has_holes(wkt: str) -> bool:
    upper = wkt.upper()
    if upper.startswith("POLYGON"):
        return len(_INNER_PATH_RE.findall(wkt)) > 1
    if upper.startswith("MULTIPOLYGON"):
        return "), (" in wkt or "),(" in wkt
    return False


def _complexity(*wkts: str) -> dict[str, object]:
    point_count = sum(_wkt_point_count(wkt) for wkt in wkts)
    return {
        "point_count": point_count,
        "path_count": sum(_wkt_path_count(wkt) for wkt in wkts),
        "complexity_class": "high" if point_count > HIGH_COMPLEXITY_POINT_COUNT else "standard",
    }


def _record_complexity_class(record: Mapping[str, Any], eligibility_file: str) -> str:
    if eligibility_file == "overlay-candidates.jsonl":
        count = _wkt_point_count(_overlay_wkt(record, "lhs"))
        count += _wkt_point_count(_overlay_wkt(record, "rhs"))
    else:
        count = _wkt_point_count(_shape_wkt(record))
    if count > HIGH_COMPLEXITY_POINT_COUNT:
        return "high"
    if count > 64:
        return "medium"
    return "small"


def _record_geometry_type(record: Mapping[str, Any], eligibility_file: str) -> str:
    if eligibility_file == "overlay-candidates.jsonl":
        return _overlay_geometry_type(record)
    return _shape_geometry_type(record)


def _record_source_id(record: Mapping[str, Any]) -> str:
    source = record.get("source")
    value = source.get("source_id") if isinstance(source, dict) else None
    return value if isinstance(value, str) and value else "unknown"


def _balanced_select(
    records: Iterable[dict[str, Any]],
    count: int,
    *,
    salt: str,
    eligibility_file: str,
    exclude_ids: set[str] | None = None,
) -> list[dict[str, Any]]:
    if count <= 0:
        return []
    excluded = exclude_ids or set()
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        if _record_id(record) in excluded:
            continue
        stratum = (
            _record_source_id(record),
            _record_geometry_type(record, eligibility_file),
            _record_complexity_class(record, eligibility_file),
        )
        groups[stratum].append(record)

    queues: list[tuple[tuple[str, str, str], deque[dict[str, Any]]]] = []
    for stratum, members in groups.items():
        members.sort(key=lambda item: _stable_digest(salt, _record_id(item)))
        queues.append((stratum, deque(members)))
    queues.sort(key=lambda item: _stable_digest(salt, *item[0]))

    selected: list[dict[str, Any]] = []
    while queues and len(selected) < count:
        remaining: list[tuple[tuple[str, str, str], deque[dict[str, Any]]]] = []
        for stratum, queue in queues:
            if queue and len(selected) < count:
                selected.append(queue.popleft())
            if queue:
                remaining.append((stratum, queue))
        queues = remaining

    if len(selected) != count:
        raise ProfileGenerationError(
            f"{salt}: requires {count} distinct eligible records, found {len(selected)}"
        )
    return selected


def _quota(profile: ProfileContract, dimension: str) -> int:
    quota = profile.scenario_quotas.get(dimension)
    return quota.min_count if quota is not None else 0


def _round_up(value: int, divisor: int) -> int:
    if divisor <= 0:
        raise ValueError("divisor must be positive")
    return ((value + divisor - 1) // divisor) * divisor


def _slug(value: str) -> str:
    slug = _SLUG_RE.sub("-", value.lower()).strip("-")
    return slug or "case"


def _make_case(
    profile_id: str,
    operation: str,
    scenario: str,
    record: Mapping[str, Any],
    inputs: dict[str, object],
    transformation: str,
    *extra_tags: str,
) -> CaseSpec:
    wkts = [
        value
        for key, value in _walk_mapping(inputs)
        if isinstance(value, str) and (key == "wkt" or key.endswith("_wkt"))
    ]
    return CaseSpec(
        profile_id=profile_id,
        operation=operation,
        scenario=scenario,
        full_id=_record_id(record),
        inputs=inputs,
        source=_record_source(record, transformation),
        tags=_record_tags(
            record,
            profile_id,
            scenario,
            GENERATOR_NAME,
            *extra_tags,
        ),
        complexity=_complexity(*wkts),
        parameters={"iterations_hint": max(1, 100_000 // max(1, sum(map(_wkt_point_count, wkts))))},
    )


def _walk_mapping(value: object) -> Iterable[tuple[str, object]]:
    if isinstance(value, dict):
        for key, child in value.items():
            yield key, child
            yield from _walk_mapping(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk_mapping(child)


def _overlay_records(
    records: list[dict[str, Any]],
    profile: ProfileContract,
) -> list[dict[str, Any]]:
    return [
        record
        for record in records
        if _overlay_geometry_type(record) in set(profile.geometry_types)
        and _overlay_wkt(record, "lhs")
        and _overlay_wkt(record, "rhs")
    ]


def _shape_records(
    records: list[dict[str, Any]],
    profile: ProfileContract,
) -> list[dict[str, Any]]:
    allowed = set(profile.geometry_types)
    return [
        record
        for record in records
        if _shape_geometry_type(record) in allowed and _shape_wkt(record)
    ]


def _boundary_pair(lhs_wkt: str, rhs_wkt: str) -> tuple[str, str]:
    lhs_left, lhs_top, lhs_right, _ = _wkt_bbox(lhs_wkt)
    rhs_left, rhs_top, _, _ = _wkt_bbox(rhs_wkt)
    del lhs_left
    return lhs_wkt, _translate_wkt(
        rhs_wkt,
        lhs_right - rhs_left,
        lhs_top - rhs_top,
    )


def _disjoint_pair(lhs_wkt: str, rhs_wkt: str) -> tuple[str, str]:
    lhs_left, lhs_top, lhs_right, _ = _wkt_bbox(lhs_wkt)
    rhs_left, rhs_top, rhs_right, _ = _wkt_bbox(rhs_wkt)
    gap = max(Decimal(1), lhs_right - lhs_left, rhs_right - rhs_left)
    return lhs_wkt, _translate_wkt(
        rhs_wkt,
        lhs_right + gap - rhs_left,
        lhs_top - rhs_top,
    )


def _generated_star_pair(record: Mapping[str, Any]) -> tuple[str, str]:
    """Construct a deterministic, genuinely non-collinear high-complexity pair."""

    seed = int(_stable_digest("high-overlay", _record_id(record))[:16], 16)
    center_x = (seed % 2_000_001) - 1_000_000
    center_y = ((seed >> 21) % 2_000_001) - 1_000_000
    outer_radius = 200_000 + seed % 50_000
    vertex_count = 520

    def star(cx: int, cy: int, phase: float) -> str:
        points: list[tuple[int, int]] = []
        for index in range(vertex_count):
            angle = phase + (2.0 * math.pi * index) / vertex_count
            radius = outer_radius if index % 2 == 0 else int(outer_radius * 0.61)
            points.append(
                (
                    cx + int(round(radius * math.cos(angle))),
                    cy + int(round(radius * math.sin(angle))),
                )
            )
        points.append(points[0])
        return "POLYGON ((" + ", ".join(f"{x} {y}" for x, y in points) + "))"

    lhs = star(center_x, center_y, 0.0)
    rhs = star(
        center_x + outer_radius // 3,
        center_y + outer_radius // 5,
        math.pi / vertex_count,
    )
    return lhs, rhs


def _build_overlay_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = _overlay_records(records, profile)
    operation_minimum = max(
        (_quota(profile, f"operation.{operation}") for operation in _OVERLAY_OPERATIONS),
        default=0,
    )
    fill_minimum = max(
        (_quota(profile, f"fill_rule.{fill_rule}") for fill_rule in _FILL_RULES),
        default=0,
    )
    combo_minimum = max(
        (
            _quota(profile, f"operation.{operation}.fill_rule.{fill_rule}")
            for operation in _OVERLAY_OPERATIONS
            for fill_rule in _FILL_RULES
        ),
        default=0,
    )
    feature_counts = {
        "feature.holes": _quota(profile, "feature.holes"),
        "feature.boundary_touching": _quota(profile, "feature.boundary_touching"),
        "feature.disjoint": _quota(profile, "feature.disjoint"),
        "feature.high_complexity": _quota(profile, "feature.high_complexity"),
    }
    total = max(
        16 * combo_minimum,
        4 * operation_minimum,
        4 * fill_minimum,
        sum(feature_counts.values()),
        16,
    )
    total = _round_up(total, 16)

    selected_by_scenario: list[tuple[str, str, list[dict[str, Any]]]] = []
    hole_candidates = [
        record
        for record in candidates
        if _has_holes(_overlay_wkt(record, "lhs"))
        or _has_holes(_overlay_wkt(record, "rhs"))
    ]
    selected_by_scenario.append(
        (
            "feature.holes",
            "source-holes",
            _balanced_select(
                hole_candidates,
                feature_counts["feature.holes"],
                salt=f"{profile.id}:feature.holes",
                eligibility_file=profile.eligibility_file,
            ),
        )
    )
    for scenario, transformation in (
        ("feature.boundary_touching", "generated-boundary-touch"),
        ("feature.disjoint", "generated-disjoint"),
        ("feature.high_complexity", "generated-concave-high-complexity"),
    ):
        selected_by_scenario.append(
            (
                scenario,
                transformation,
                _balanced_select(
                    candidates,
                    feature_counts[scenario],
                    salt=f"{profile.id}:{scenario}",
                    eligibility_file=profile.eligibility_file,
                ),
            )
        )

    assigned = sum(len(selected) for _, _, selected in selected_by_scenario)
    selected_by_scenario.append(
        (
            "balanced-overlay",
            "source-overlay",
            _balanced_select(
                candidates,
                total - assigned,
                salt=f"{profile.id}:balanced-overlay",
                eligibility_file=profile.eligibility_file,
            ),
        )
    )

    cases: list[CaseSpec] = []
    for scenario, transformation, selected in selected_by_scenario:
        for record in selected:
            index = len(cases)
            operation = _OVERLAY_OPERATIONS[(index % 16) // 4]
            fill_rule = _FILL_RULES[index % 4]
            lhs_wkt = _overlay_wkt(record, "lhs")
            rhs_wkt = _overlay_wkt(record, "rhs")
            if scenario == "feature.boundary_touching":
                lhs_wkt, rhs_wkt = _boundary_pair(lhs_wkt, rhs_wkt)
            elif scenario == "feature.disjoint":
                lhs_wkt, rhs_wkt = _disjoint_pair(lhs_wkt, rhs_wkt)
            elif scenario == "feature.high_complexity":
                lhs_wkt, rhs_wkt = _generated_star_pair(record)
            inputs: dict[str, object] = {
                "lhs_wkt": lhs_wkt,
                "rhs_wkt": rhs_wkt,
                "fill_rule": fill_rule,
                "preserve_collinear": bool(index & 1),
                "reverse_solution": bool(index & 2),
            }
            cases.append(
                _make_case(
                    profile.id,
                    f"overlay.{operation}",
                    scenario,
                    record,
                    inputs,
                    transformation,
                )
            )
    return cases


def _integer_bbox(wkt: str) -> tuple[int, int, int, int]:
    left, top, right, bottom = _wkt_bbox(wkt)
    return (
        math.floor(left),
        math.floor(top),
        math.ceil(right),
        math.ceil(bottom),
    )


def _scenario_geometry(
    wkt: str,
    scenario: str,
) -> tuple[str, dict[str, int]]:
    if scenario == "crossing":
        transformed = _scale_wkt(wkt, Decimal(32))
        points = _wkt_points(transformed)
        first = points[0]
        if not any(point != first for point in points[1:]):
            raise ProfileGenerationError("crossing scenario requires distinct vertices")
        x = int(first[0])
        y = int(first[1])
        return transformed, {
            "left": x - 1,
            "top": y - 1,
            "right": x + 1,
            "bottom": y + 1,
        }

    left, top, right, bottom = _integer_bbox(wkt)
    width = max(2, right - left)
    height = max(2, bottom - top)
    margin = max(1, width, height)
    if scenario == "contained":
        return wkt, {
            "left": left - margin,
            "top": top - margin,
            "right": right + margin,
            "bottom": bottom + margin,
        }
    if scenario == "disjoint":
        return wkt, {
            "left": right + margin,
            "top": top,
            "right": right + margin + width,
            "bottom": top + height,
        }
    if scenario == "boundary":
        return wkt, {
            "left": left,
            "top": top,
            "right": max(left + 1, right + margin),
            "bottom": max(top + 1, bottom + margin),
        }
    raise ProfileGenerationError(f"unsupported rectangle scenario {scenario!r}")


def _rect_polygon(rect: Mapping[str, int]) -> str:
    left = rect["left"]
    top = rect["top"]
    right = rect["right"]
    bottom = rect["bottom"]
    return (
        "POLYGON (("
        f"{left} {top}, {right} {top}, {right} {bottom}, "
        f"{left} {bottom}, {left} {top}"
        "))"
    )


def _select_scenario_shapes(
    profile: ProfileContract,
    candidates: list[dict[str, Any]],
    scenario: str,
    count: int,
    high_count: int = 0,
) -> list[tuple[dict[str, Any], bool]]:
    high_candidates = [
        record
        for record in candidates
        if _wkt_point_count(_shape_wkt(record)) > HIGH_COMPLEXITY_POINT_COUNT
    ]
    high = _balanced_select(
        high_candidates,
        high_count,
        salt=f"{profile.id}:{scenario}:high",
        eligibility_file=profile.eligibility_file,
    )
    high_ids = {_record_id(record) for record in high}
    standard = _balanced_select(
        candidates,
        count - high_count,
        salt=f"{profile.id}:{scenario}:standard",
        eligibility_file=profile.eligibility_file,
        exclude_ids=high_ids,
    )
    return [(record, True) for record in high] + [
        (record, False) for record in standard
    ]


def _build_rect_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
    *,
    lines: bool,
) -> list[CaseSpec]:
    candidates = _shape_records(records, profile)
    high_count = _quota(profile, "points_gt_512")
    cases: list[CaseSpec] = []
    for scenario in _RECT_SCENARIOS:
        count = _quota(profile, scenario)
        selected = _select_scenario_shapes(
            profile,
            candidates,
            scenario,
            count,
            high_count if scenario == "crossing" else 0,
        )
        for record, high in selected:
            wkt, rect = _scenario_geometry(_shape_wkt(record), scenario)
            inputs = {
                "lines_wkt" if lines else "paths_wkt": wkt,
                "rect": rect,
            }
            cases.append(
                _make_case(
                    profile.id,
                    "rectclip.lines" if lines else "rectclip.polygon",
                    scenario,
                    record,
                    inputs,
                    f"generated-{scenario}-rectangle",
                    "actual-high-complexity" if high else "standard-complexity",
                )
            )
    return cases


def _build_open_overlay_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = _shape_records(records, profile)
    scenario_counts = {scenario: _quota(profile, scenario) for scenario in _RECT_SCENARIOS}
    high_count = _quota(profile, "points_gt_512")
    operation_minimum = max(
        (_quota(profile, f"operation.{operation}") for operation in _OVERLAY_OPERATIONS),
        default=0,
    )
    fill_minimum = max(
        (_quota(profile, f"fill_rule.{fill_rule}") for fill_rule in _FILL_RULES),
        default=0,
    )
    combo_minimum = max(
        (
            _quota(profile, f"operation.{operation}.fill_rule.{fill_rule}")
            for operation in _OVERLAY_OPERATIONS
            for fill_rule in _FILL_RULES
        ),
        default=0,
    )
    total = _round_up(
        max(
            sum(scenario_counts.values()),
            4 * operation_minimum,
            4 * fill_minimum,
            16 * combo_minimum,
        ),
        16,
    )
    scenario_counts["crossing"] += total - sum(scenario_counts.values())

    cases: list[CaseSpec] = []
    for scenario in _RECT_SCENARIOS:
        selected = _select_scenario_shapes(
            profile,
            candidates,
            scenario,
            scenario_counts[scenario],
            high_count if scenario == "crossing" else 0,
        )
        for record, high in selected:
            index = len(cases)
            operation = _OVERLAY_OPERATIONS[(index % 16) // 4]
            fill_rule = _FILL_RULES[index % 4]
            line, rect = _scenario_geometry(_shape_wkt(record), scenario)
            inputs: dict[str, object] = {
                "lhs_wkt": line,
                "rhs_wkt": _rect_polygon(rect),
                "fill_rule": fill_rule,
                "preserve_collinear": bool(index & 1),
                "reverse_solution": bool(index & 2),
            }
            cases.append(
                _make_case(
                    profile.id,
                    f"overlay.{operation}",
                    scenario,
                    record,
                    inputs,
                    f"generated-{scenario}-open-overlay",
                    "actual-high-complexity" if high else "standard-complexity",
                )
            )
    return cases


def _offset_delta(wkt: str, positive: bool) -> float:
    left, top, right, bottom = _wkt_bbox(wkt)
    span = max(abs(right - left), abs(bottom - top), Decimal(1))
    magnitude = max(Decimal(1), min(Decimal(10_000), span / Decimal(100)))
    value = float(magnitude)
    return value if positive else -value


def _build_offset_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = _shape_records(records, profile)
    polygons = [
        record for record in candidates if _shape_geometry_type(record) in _POLYGON_TYPES
    ]
    lines = [
        record for record in candidates if _shape_geometry_type(record) in _LINE_TYPES
    ]
    positive_min = _quota(profile, "delta.positive")
    negative_min = _quota(profile, "delta.negative")
    join_min = max((_quota(profile, f"join.{join}") for join in _JOIN_TYPES), default=0)
    end_min = max((_quota(profile, f"end.{end}") for end in _END_TYPES), default=0)
    high_count = _quota(profile, "feature.high_complexity")
    hole_count = _quota(profile, "feature.holes")
    total = _round_up(
        max(
            2 * max(positive_min, negative_min),
            3 * join_min,
            5 * end_min,
            high_count,
            hole_count,
            30,
        ),
        30,
    )

    schedule = [
        {
            "positive": index % 2 == 0,
            "join": _JOIN_TYPES[index % len(_JOIN_TYPES)],
            "end": _END_TYPES[index % len(_END_TYPES)],
        }
        for index in range(total)
    ]
    hole_indices = [
        index for index, entry in enumerate(schedule) if entry["end"] == "polygon"
    ][:hole_count]
    high_indices = [
        index for index, entry in enumerate(schedule) if entry["end"] != "polygon"
    ][:high_count]
    if len(high_indices) < high_count:
        high_indices.extend(
            index
            for index in range(total)
            if index not in high_indices and index not in hole_indices
        )
        high_indices = high_indices[:high_count]
    if len(hole_indices) != hole_count or len(high_indices) != high_count:
        raise ProfileGenerationError(f"{profile.id}: cannot place feature quotas")

    high_set = set(high_indices)
    hole_set = set(hole_indices)
    grouped_indices: dict[tuple[str, str, bool, bool], list[int]] = defaultdict(list)
    for index, entry in enumerate(schedule):
        end_type = str(entry["end"])
        sign = "positive" if bool(entry["positive"]) else "negative"
        scenario = f"offset-{sign}-{entry['join']}-{end_type}"
        grouped_indices[(scenario, end_type, index in high_set, index in hole_set)].append(
            index
        )

    assignments: dict[int, dict[str, Any]] = {}
    used_by_scenario: dict[str, set[str]] = defaultdict(set)
    for (scenario, end_type, needs_high, needs_holes), indices in sorted(
        grouped_indices.items()
    ):
        pool = polygons if end_type == "polygon" else lines
        if needs_high:
            pool = [
                record
                for record in pool
                if _wkt_point_count(_shape_wkt(record))
                > HIGH_COMPLEXITY_POINT_COUNT
            ]
        if needs_holes:
            pool = [record for record in pool if _has_holes(_shape_wkt(record))]
        selected = _balanced_select(
            pool,
            len(indices),
            salt=(
                f"{profile.id}:{scenario}:high={needs_high}:holes={needs_holes}"
            ),
            eligibility_file=profile.eligibility_file,
            exclude_ids=used_by_scenario[scenario],
        )
        used_by_scenario[scenario].update(map(_record_id, selected))
        assignments.update(zip(indices, selected))

    cases: list[CaseSpec] = []
    for index, entry in enumerate(schedule):
        end_type = str(entry["end"])
        positive = bool(entry["positive"])
        sign = "positive" if positive else "negative"
        scenario = f"offset-{sign}-{entry['join']}-{end_type}"
        selected = assignments[index]
        wkt = _shape_wkt(selected)
        inputs: dict[str, object] = {
            "paths_wkt": wkt,
            "delta": _offset_delta(wkt, positive),
            "join_type": entry["join"],
            "end_type": end_type,
            "preserve_collinear": bool(index & 1),
            "reverse_solution": bool(index & 2),
        }
        cases.append(
            _make_case(
                profile.id,
                "offset.polygon" if end_type == "polygon" else "offset.open",
                scenario,
                selected,
                inputs,
                "source-offset-options",
                "actual-high-complexity" if index in high_set else "standard-complexity",
                "source-holes" if index in hole_set else "no-hole-quota",
            )
        )
    return cases


def _path_operand_wkt(wkt: str, *, max_points: int) -> str:
    matches = _INNER_PATH_RE.findall(wkt)
    if not matches:
        raise ProfileGenerationError("Minkowski operand has no WKT path")
    points = _parse_path_text(matches[0])
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    distinct: list[tuple[Decimal, Decimal]] = []
    seen: set[tuple[Decimal, Decimal]] = set()
    for point in points:
        if point not in seen:
            distinct.append(point)
            seen.add(point)
        if len(distinct) == max_points:
            break
    if len(distinct) < 3:
        raise ProfileGenerationError("Minkowski operand has fewer than three points")
    return "LINESTRING (" + ", ".join(
        f"{_format_decimal(x)} {_format_decimal(y)}" for x, y in distinct
    ) + ")"


def is_minkowski_eligible(
    record: Mapping[str, Any],
    profile: ProfileContract,
) -> bool:
    if _shape_geometry_type(record) not in set(profile.geometry_types):
        return False
    try:
        _path_operand_wkt(_shape_wkt(record), max_points=3)
    except ProfileGenerationError:
        return False
    return True


def _build_minkowski_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = [
        record
        for record in _shape_records(records, profile)
        if is_minkowski_eligible(record, profile)
    ]
    operation_min = max(
        _quota(profile, "operation.sum"),
        _quota(profile, "operation.difference"),
    )
    closure_min = max(
        _quota(profile, "path.closed"),
        _quota(profile, "path.open"),
    )
    total = _round_up(max(2 * operation_min, 2 * closure_min, 4), 4)
    schedule: list[tuple[str, bool, str]] = []
    grouped_indices: dict[str, list[int]] = defaultdict(list)
    for index in range(total):
        operation = "sum" if index % 2 == 0 else "difference"
        is_closed = (index // 2) % 2 == 0
        closure = "closed" if is_closed else "open"
        scenario = f"minkowski-{operation}-{closure}"
        schedule.append((operation, is_closed, scenario))
        grouped_indices[scenario].append(index)

    primary_by_index: dict[int, dict[str, Any]] = {}
    secondary_by_index: dict[int, dict[str, Any]] = {}
    for scenario, indices in sorted(grouped_indices.items()):
        primary = _balanced_select(
            candidates,
            len(indices),
            salt=f"{profile.id}:{scenario}:primary",
            eligibility_file=profile.eligibility_file,
        )
        secondary = _balanced_select(
            candidates,
            min(len(candidates), len(indices) + 1),
            salt=f"{profile.id}:{scenario}:secondary",
            eligibility_file=profile.eligibility_file,
        )
        for offset, (index, first) in enumerate(zip(indices, primary)):
            second = secondary[offset % len(secondary)]
            if _record_id(second) == _record_id(first):
                second = secondary[(offset + 1) % len(secondary)]
            if _record_id(second) == _record_id(first):
                raise ProfileGenerationError(
                    f"{profile.id}:{scenario}: cannot select distinct operands"
                )
            primary_by_index[index] = first
            secondary_by_index[index] = second

    cases: list[CaseSpec] = []
    for index, (operation, is_closed, scenario) in enumerate(schedule):
        primary = primary_by_index[index]
        secondary = secondary_by_index[index]
        pattern_wkt = _path_operand_wkt(_shape_wkt(primary), max_points=12)
        path_wkt = _path_operand_wkt(_shape_wkt(secondary), max_points=128)
        inputs: dict[str, object] = {
            "pattern_wkt": pattern_wkt,
            "path_wkt": path_wkt,
            "is_closed": is_closed,
        }
        source = copy.deepcopy(primary)
        source["source"] = {
            "source_id": _record_source_id(primary),
            "primary_id": _record_id(primary),
            "secondary_id": _record_id(secondary),
        }
        cases.append(
            _make_case(
                profile.id,
                f"minkowski.{operation}",
                scenario,
                source,
                inputs,
                "paired-nontrivial-operands",
            )
        )
    return cases


def _build_triangulation_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = _shape_records(records, profile)
    sweep_min = _quota(profile, "mode.sweep")
    delaunay_min = _quota(profile, "mode.delaunay")
    high_count = _quota(profile, "feature.high_complexity")
    total = max(sweep_min + delaunay_min, 2)
    modes = ["sweep"] * sweep_min + ["delaunay"] * delaunay_min
    while len(modes) < total:
        modes.append("sweep" if len(modes) % 2 == 0 else "delaunay")

    high_per_mode = {
        "sweep": high_count // 2,
        "delaunay": high_count - high_count // 2,
    }
    selected_by_mode: dict[str, list[tuple[dict[str, Any], bool]]] = {}
    for mode in ("sweep", "delaunay"):
        mode_count = modes.count(mode)
        high_candidates = [
            record
            for record in candidates
            if _wkt_point_count(_shape_wkt(record)) > HIGH_COMPLEXITY_POINT_COUNT
        ]
        high = _balanced_select(
            high_candidates,
            high_per_mode[mode],
            salt=f"{profile.id}:{mode}:high",
            eligibility_file=profile.eligibility_file,
        )
        high_ids = set(map(_record_id, high))
        standard = _balanced_select(
            candidates,
            mode_count - len(high),
            salt=f"{profile.id}:{mode}:standard",
            eligibility_file=profile.eligibility_file,
            exclude_ids=high_ids,
        )
        selected_by_mode[mode] = [(record, True) for record in high] + [
            (record, False) for record in standard
        ]

    cases: list[CaseSpec] = []
    consumed_by_mode = defaultdict(int)
    for index, mode in enumerate(modes):
        scenario = f"triangulation-{mode}"
        selected, needs_high = selected_by_mode[mode][consumed_by_mode[mode]]
        consumed_by_mode[mode] += 1
        cases.append(
            _make_case(
                profile.id,
                f"triangulation.{mode}",
                scenario,
                selected,
                {"polygon_wkt": _shape_wkt(selected)},
                "source-triangulation-input",
                "actual-high-complexity" if needs_high else "standard-complexity",
            )
        )
    return cases


def _batch_request(record: Mapping[str, Any], index: int) -> dict[str, object]:
    operation = _OVERLAY_OPERATIONS[(index // 4) % len(_OVERLAY_OPERATIONS)]
    return {
        "operation": f"overlay.{operation}",
        "lhs_wkt": _overlay_wkt(record, "lhs"),
        "rhs_wkt": _overlay_wkt(record, "rhs"),
        "fill_rule": _FILL_RULES[index % len(_FILL_RULES)],
        "preserve_collinear": bool(index & 1),
        "reverse_solution": bool(index & 2),
    }


def _build_batch_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = _overlay_records(records, profile)
    count = max(_quota(profile, "batch.scalar_next_legacy"), 1)
    selected = _balanced_select(
        candidates,
        count,
        salt=f"{profile.id}:batch.scalar_next_legacy",
        eligibility_file=profile.eligibility_file,
    )
    ranked = sorted(
        candidates,
        key=lambda record: _stable_digest(profile.id, "batch-members", _record_id(record)),
    )
    cases: list[CaseSpec] = []
    for index, primary in enumerate(selected):
        primary_position = next(
            position
            for position, record in enumerate(ranked)
            if _record_id(record) == _record_id(primary)
        )
        members = [
            ranked[(primary_position + offset) % len(ranked)] for offset in range(4)
        ]
        inputs: dict[str, object] = {
            "comparison_modes": [
                "legacy_scalar",
                "next_scalar",
                "next_batch",
            ],
            "requests": [
                _batch_request(record, index * 4 + request_index)
                for request_index, record in enumerate(members)
            ],
        }
        cases.append(
            _make_case(
                profile.id,
                "batch.clip",
                "batch.scalar_next_legacy",
                primary,
                inputs,
                "four-request-batch",
            )
        )
    return cases


def _build_ungated_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    candidates = _shape_records(records, profile)
    count = min(DEFAULT_UNGATED_CASE_COUNT, len(candidates))
    if count == 0:
        raise ProfileGenerationError(f"{profile.id}: no eligible geometry records")
    selected = _balanced_select(
        candidates,
        count,
        salt=f"{profile.id}:balanced",
        eligibility_file=profile.eligibility_file,
    )
    cases: list[CaseSpec] = []
    for index, record in enumerate(selected):
        wkt = _shape_wkt(record)
        geometry_type = _shape_geometry_type(record)
        if profile.id == "bounds":
            operation = "geometry.bounds"
            inputs: dict[str, object] = {"geometry_wkt": wkt}
        elif profile.id == "simplification":
            operation = "geometry.simplify" if index % 2 == 0 else "geometry.rdp"
            inputs = {"geometry_wkt": wkt, "epsilon": 1.0}
        elif profile.id == "collinear-trimming":
            operation = "geometry.trim_collinear"
            inputs = {
                "geometry_wkt": wkt,
                "is_closed": geometry_type in _POLYGON_TYPES,
            }
        elif profile.id == "point-in-polygon":
            left, top, right, bottom = _integer_bbox(wkt)
            operation = "geometry.point_in_polygon"
            inputs = {
                "polygon_wkt": wkt,
                "point": {
                    "x": (left + right) // 2,
                    "y": (top + bottom) // 2,
                },
            }
        elif profile.id == "scaling":
            operation = "transform.scale"
            inputs = {"geometry_wkt": wkt, "scale_factor": 2.0}
        elif profile.id == "translation":
            operation = "transform.translate"
            inputs = {"geometry_wkt": wkt, "delta_x": 17, "delta_y": -23}
        else:
            raise ProfileGenerationError(
                f"no profile builder for non-gated profile {profile.id!r}"
            )
        cases.append(
            _make_case(
                profile.id,
                operation,
                f"balanced-{profile.id}",
                record,
                inputs,
                "source-geometry",
            )
        )
    return cases


def _build_profile_cases(
    profile: ProfileContract,
    records: list[dict[str, Any]],
) -> list[CaseSpec]:
    if profile.id in {"overlay", "clip-tree", "polytree"}:
        return _build_overlay_cases(profile, records)
    if profile.id == "open-path-overlay":
        return _build_open_overlay_cases(profile, records)
    if profile.id == "rectclip":
        return _build_rect_cases(profile, records, lines=False)
    if profile.id == "rectclip-lines":
        return _build_rect_cases(profile, records, lines=True)
    if profile.id == "offset":
        return _build_offset_cases(profile, records)
    if profile.id == "minkowski":
        return _build_minkowski_cases(profile, records)
    if profile.id == "triangulation":
        return _build_triangulation_cases(profile, records)
    if profile.id == "batch":
        return _build_batch_cases(profile, records)
    return _build_ungated_cases(profile, records)


def _render_record(case: CaseSpec, case_set: str, index: int) -> dict[str, object]:
    input_digest = canonical_json_digest(case.inputs)
    options = {
        key: case.inputs[key] for key in sorted(_OPTION_KEYS & set(case.inputs))
    }
    identifier_digest = _stable_digest(
        case.profile_id,
        case.scenario,
        case.operation,
        case.full_id,
        input_digest,
    )[:12]
    record: dict[str, object] = {
        "id": (
            f"{_slug(case.profile_id)}-{_slug(case.scenario)}-"
            f"{index:04d}-{identifier_digest}-{case_set}"
        ),
        "profile": case_set,
        "operation": case.operation,
        "scenario": case.scenario,
        "partition": stable_partition(case.full_id),
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
        },
        "inputs": copy.deepcopy(case.inputs),
        "source": copy.deepcopy(case.source),
        "tags": list(case.tags),
        "selector": {
            "method": SELECTOR_METHOD,
            "full_id": case.full_id,
        },
        "tolerance": {
            "coordinate": 0.0,
            "area": 0.0,
        },
        "canonical_input_digest": input_digest,
        "canonical_options_digest": canonical_json_digest(options),
    }
    if case_set == "verification":
        record["expected"] = {"relation": "strict-legacy-runtime"}
        record["reference_engines"] = [
            {
                "engine": "clipper2-legacy",
                "version": LEGACY_ENGINE_VERSION,
            }
        ]
    elif case_set == "benchmark":
        record["complexity"] = copy.deepcopy(case.complexity)
        record["parameters"] = copy.deepcopy(case.parameters)
    else:
        raise ProfileGenerationError(f"unsupported case set {case_set!r}")
    return record


def _encode_jsonl(records: Iterable[dict[str, object]]) -> bytes:
    return "".join(
        json.dumps(
            record,
            allow_nan=False,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        )
        + "\n"
        for record in records
    ).encode("utf-8")


def _validate_generated(
    generated: Mapping[tuple[str, str], list[dict[str, Any]]],
    contract: ReleaseEvidenceContract,
) -> None:
    errors: list[str] = []
    all_records: list[dict[str, Any]] = []
    for profile in contract.profiles.values():
        verification = generated[(profile.id, "verification")]
        benchmark = generated[(profile.id, "benchmark")]
        errors.extend(
            profile_check.validate_profile_semantics(
                verification,
                profile,
                "verification",
            )
        )
        errors.extend(
            profile_check.validate_profile_semantics(
                benchmark,
                profile,
                "benchmark",
            )
        )
        errors.extend(profile_check.validate_twins(verification, benchmark))
        all_records.extend(verification)
        all_records.extend(benchmark)
    errors.extend(profile_check.validate_partition_disjointness(all_records))
    if errors:
        examples = "\n".join(sorted(set(errors))[:50])
        raise ProfileGenerationError(
            "generated profiles violate the release evidence contract:\n" + examples
        )


def _write_atomically(
    output_root: Path,
    payloads: Mapping[Path, bytes],
) -> None:
    output_root.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(
        tempfile.mkdtemp(
            prefix=".release-profiles-stage-",
            dir=output_root.parent,
        )
    )
    try:
        for relative_path, payload in payloads.items():
            staged_path = stage / relative_path
            staged_path.parent.mkdir(parents=True, exist_ok=True)
            staged_path.write_bytes(payload)

        for relative_path in sorted(payloads):
            destination = output_root / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            os.replace(stage / relative_path, destination)
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def generate_release_profiles(
    input_root: Path,
    output_root: Path,
    contract: ReleaseEvidenceContract,
) -> GenerationResult:
    """Generate all contract profiles after validating the complete in-memory set."""

    full_root = Path(input_root) / "normalized" / "full"
    eligibility_files = sorted(
        {profile.eligibility_file for profile in contract.profiles.values()}
    )
    loaded = {
        filename: load_eligibility_records(full_root / filename)
        for filename in eligibility_files
    }

    generated: dict[tuple[str, str], list[dict[str, Any]]] = {}
    profile_counts: dict[str, int] = {}
    for profile in contract.profiles.values():
        cases = _build_profile_cases(profile, loaded[profile.eligibility_file])
        if not cases:
            raise ProfileGenerationError(f"{profile.id}: generated no cases")
        for case_set in profile.required_case_sets:
            records = [
                _render_record(case, case_set, index)
                for index, case in enumerate(cases, 1)
            ]
            generated[(profile.id, case_set)] = records
            profile_counts[f"{profile.id}:{case_set}"] = len(records)

    _validate_generated(generated, contract)
    payloads = {
        Path("normalized") / case_set / f"{profile_id}.jsonl": _encode_jsonl(records)
        for (profile_id, case_set), records in generated.items()
    }
    _write_atomically(Path(output_root), payloads)
    return GenerationResult(
        profile_counts=dict(sorted(profile_counts.items())),
        output_root=Path(output_root),
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Generate deterministic strict-legacy verification and benchmark "
            "profiles from normalized full geometry records."
        )
    )
    parser.add_argument("input_root", type=Path)
    parser.add_argument("output_root", type=Path)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT_PATH)
    args = parser.parse_args()

    try:
        contract = load_contract(args.contract)
        result = generate_release_profiles(
            args.input_root,
            args.output_root,
            contract,
        )
    except (ProfileGenerationError, ValueError, OSError) as error:
        print("status=FAIL")
        print(error, file=sys.stderr)
        return 1

    print("status=PASS")
    print(f"output_root={result.output_root}")
    for key, count in result.profile_counts.items():
        print(f"{key}={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
