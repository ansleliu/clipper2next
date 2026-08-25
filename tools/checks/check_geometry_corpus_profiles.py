#!/usr/bin/env python3
"""Validate release corpus profiles against their semantic evidence contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from collections import Counter
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.evidence_contract import (
    DEFAULT_CONTRACT_PATH,
    ProfileContract,
    ReleaseEvidenceContract,
    load_contract,
    release_gated_profile_ids,
)


_ALLOWED_PARTITIONS = {"development", "release-holdout"}
_DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")
_NUMBER_RE = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
_POINT_RE = re.compile(rf"({_NUMBER_RE})\s+({_NUMBER_RE})")
_OPTION_KEYS = {
    "fill_rule",
    "join_type",
    "end_type",
    "is_closed",
    "preserve_collinear",
    "reverse_solution",
}
_FILL_RULES = {"even_odd", "non_zero", "positive", "negative"}
_JOIN_TYPES = {"miter", "round", "square"}
_END_TYPES = {"polygon", "joined", "butt", "square", "round"}
_FILL_RULE_PROFILES = {
    "overlay",
    "open-path-overlay",
    "clip-tree",
    "polytree",
}
_DERIVED_SCENARIO_PREFIXES = (
    "operation.",
    "fill_rule.",
    "delta.",
    "join.",
    "end.",
    "path.",
    "mode.",
    "feature.",
)
_ALLOWED_OPERATIONS = {
    "overlay": {
        "overlay.intersection",
        "overlay.union",
        "overlay.difference",
        "overlay.xor",
    },
    "open-path-overlay": {
        "overlay.intersection",
        "overlay.union",
        "overlay.difference",
        "overlay.xor",
    },
    "rectclip": {"rectclip.polygon"},
    "rectclip-lines": {"rectclip.lines"},
    "offset": {"offset.polygon", "offset.open"},
    "minkowski": {"minkowski.sum", "minkowski.difference"},
    "triangulation": {"triangulation.sweep", "triangulation.delaunay"},
    "clip-tree": {
        "overlay.intersection",
        "overlay.union",
        "overlay.difference",
        "overlay.xor",
    },
    "polytree": {
        "overlay.intersection",
        "overlay.union",
        "overlay.difference",
        "overlay.xor",
    },
    "batch": {
        "overlay.intersection",
        "overlay.union",
        "overlay.difference",
        "overlay.xor",
        "batch.clip",
    },
    "bounds": {"geometry.bounds"},
    "simplification": {"geometry.simplify", "geometry.rdp"},
    "collinear-trimming": {"geometry.trim_collinear"},
    "point-in-polygon": {"geometry.point_in_polygon"},
    "scaling": {"transform.scale"},
    "translation": {"transform.translate"},
}


@dataclass(frozen=True)
class ProfileResult:
    case_set: str
    profile: str
    path: Path
    records: int
    sha256: str


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_digest(value: object) -> str:
    encoded = json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number {value}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_profile(
    path: Path,
    case_set: str,
) -> tuple[list[dict[str, Any]], list[str]]:
    """Load one exact profile file without silently accepting malformed rows."""

    if not path.is_file():
        return [], [f"missing profile: {path}"]

    records: list[dict[str, Any]] = []
    errors: list[str] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                payload = json.loads(
                    line,
                    object_pairs_hook=_unique_object,
                    parse_constant=_reject_json_constant,
                )
            except (json.JSONDecodeError, ValueError) as error:
                errors.append(f"{path}:{line_number}: invalid JSON: {error}")
                continue
            if not isinstance(payload, dict):
                errors.append(f"{path}:{line_number}: record must be an object")
                continue
            records.append(payload)

    if not records:
        errors.append(f"{path}: profile contains no valid records")
    return records, errors


def _require(
    condition: bool,
    message: str,
    errors: list[str],
) -> None:
    if not condition:
        errors.append(message)


def _nonempty_string(value: object) -> bool:
    return isinstance(value, str) and bool(value)


def _finite_number(value: object) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, (int, float))
        and math.isfinite(float(value))
    )


def _zero_number(value: object) -> bool:
    return _finite_number(value) and float(value) == 0.0


def _canonical_options(record: dict[str, Any]) -> dict[str, object]:
    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        return {}
    return {key: inputs[key] for key in sorted(_OPTION_KEYS & set(inputs))}


def _wkt_points(value: object) -> list[tuple[Decimal, Decimal]]:
    if not isinstance(value, str):
        return []
    points: list[tuple[Decimal, Decimal]] = []
    for match in _POINT_RE.finditer(value):
        try:
            points.append((Decimal(match.group(1)), Decimal(match.group(2))))
        except InvalidOperation:
            continue
    return points


def distinct_wkt_points(value: object) -> int:
    return len(set(_wkt_points(value)))


def _input_wkts(value: object) -> list[str]:
    if isinstance(value, dict):
        result: list[str] = []
        for key, child in value.items():
            if isinstance(child, str) and (key == "wkt" or key.endswith("_wkt")):
                result.append(child)
            else:
                result.extend(_input_wkts(child))
        return result
    if isinstance(value, list):
        result = []
        for child in value:
            result.extend(_input_wkts(child))
        return result
    return []


def _wkt_bbox(value: object) -> tuple[Decimal, Decimal, Decimal, Decimal] | None:
    points = _wkt_points(value)
    if not points:
        return None
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return min(xs), min(ys), max(xs), max(ys)


def _is_axis_aligned_rectangle_wkt(value: object) -> bool:
    points = _wkt_points(value)
    if len(points) < 4:
        return False
    unique_points = set(points)
    if len(unique_points) != 4:
        return False
    xs = {point[0] for point in unique_points}
    ys = {point[1] for point in unique_points}
    if len(xs) != 2 or len(ys) != 2:
        return False
    if unique_points != {(x, y) for x in xs for y in ys}:
        return False
    return all(
        first[0] == second[0] or first[1] == second[1]
        for first, second in zip(points, points[1:])
    )


def _intervals_overlap(
    first_min: Decimal,
    first_max: Decimal,
    second_min: Decimal,
    second_max: Decimal,
) -> bool:
    return max(first_min, second_min) <= min(first_max, second_max)


def _overlay_geometry_dimensions(inputs: dict[str, Any]) -> set[str]:
    lhs_wkt = inputs.get("lhs_wkt")
    rhs_wkt = inputs.get("rhs_wkt")
    lhs_bbox = _wkt_bbox(lhs_wkt)
    rhs_bbox = _wkt_bbox(rhs_wkt)
    if lhs_bbox is None or rhs_bbox is None:
        return set()

    lhs_left, lhs_top, lhs_right, lhs_bottom = lhs_bbox
    rhs_left, rhs_top, rhs_right, rhs_bottom = rhs_bbox
    dimensions: set[str] = set()
    if (
        lhs_right < rhs_left
        or rhs_right < lhs_left
        or lhs_bottom < rhs_top
        or rhs_bottom < lhs_top
    ):
        dimensions.add("feature.disjoint")
        return dimensions

    x_boundary_touch = (
        lhs_right == rhs_left or rhs_right == lhs_left
    ) and _intervals_overlap(lhs_top, lhs_bottom, rhs_top, rhs_bottom)
    y_boundary_touch = (
        lhs_bottom == rhs_top or rhs_bottom == lhs_top
    ) and _intervals_overlap(lhs_left, lhs_right, rhs_left, rhs_right)
    if x_boundary_touch or y_boundary_touch:
        dimensions.add("feature.boundary_touching")
    return dimensions


def _evidence_dimensions(record: dict[str, Any]) -> set[str]:
    dimensions: set[str] = set()
    scenario = record.get("scenario")
    if (
        isinstance(scenario, str)
        and scenario
        and scenario not in {"identity", "points_gt_512", "zero-delta"}
        and not scenario.startswith(_DERIVED_SCENARIO_PREFIXES)
    ):
        dimensions.add(scenario)

    operation = record.get("operation")
    if isinstance(operation, str) and operation:
        suffix = operation.rsplit(".", 1)[-1]
        if operation.startswith(("overlay.", "minkowski.")):
            dimensions.add(f"operation.{suffix}")
        if operation.startswith("triangulation."):
            dimensions.add(f"mode.{suffix}")

    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        return dimensions

    fill_rule = inputs.get("fill_rule")
    if isinstance(fill_rule, str) and fill_rule:
        dimensions.add(f"fill_rule.{fill_rule}")
        if isinstance(operation, str) and operation.startswith("overlay."):
            dimensions.add(
                f"operation.{operation.rsplit('.', 1)[-1]}.fill_rule.{fill_rule}"
            )

    delta = inputs.get("delta")
    if _finite_number(delta):
        if float(delta) > 0.0:
            dimensions.add("delta.positive")
        elif float(delta) < 0.0:
            dimensions.add("delta.negative")
        else:
            dimensions.add("zero-delta")

    join_type = inputs.get("join_type")
    if isinstance(join_type, str) and join_type:
        dimensions.add(f"join.{join_type}")
    end_type = inputs.get("end_type")
    if isinstance(end_type, str) and end_type:
        dimensions.add(f"end.{end_type}")
    is_closed = inputs.get("is_closed")
    if isinstance(is_closed, bool):
        dimensions.add("path.closed" if is_closed else "path.open")

    dimensions.update(_overlay_geometry_dimensions(inputs))

    point_count = sum(len(_wkt_points(wkt)) for wkt in _input_wkts(inputs))
    complexity = record.get("complexity")
    if isinstance(complexity, dict) and _finite_number(complexity.get("point_count")):
        point_count = max(point_count, int(complexity["point_count"]))
    if point_count > 512:
        dimensions.add("points_gt_512")
        dimensions.add("feature.high_complexity")

    if any("), (" in wkt or "),(" in wkt for wkt in _input_wkts(inputs)):
        dimensions.add("feature.holes")
    if min(
        distinct_wkt_points(inputs.get("pattern_wkt")),
        distinct_wkt_points(inputs.get("path_wkt")),
    ) < 3 and ("pattern_wkt" in inputs or "path_wkt" in inputs):
        dimensions.add("identity")
    return dimensions


def _rect_values(rect: object) -> tuple[float, float, float, float] | None:
    if not isinstance(rect, dict):
        return None
    values = [rect.get(key) for key in ("left", "top", "right", "bottom")]
    if not all(_finite_number(value) for value in values):
        return None
    left, top, right, bottom = (float(value) for value in values)
    if not left < right or not top < bottom:
        return None
    return left, top, right, bottom


def _point_inside_rect(
    point: tuple[Decimal, Decimal],
    rect: tuple[float, float, float, float],
) -> bool:
    x, y = (float(point[0]), float(point[1]))
    left, top, right, bottom = rect
    return left <= x <= right and top <= y <= bottom


def _point_on_rect_boundary(
    point: tuple[Decimal, Decimal],
    rect: tuple[float, float, float, float],
) -> bool:
    x, y = (float(point[0]), float(point[1]))
    left, top, right, bottom = rect
    return ((x == left or x == right) and top <= y <= bottom) or (
        (y == top or y == bottom) and left <= x <= right
    )


def _segment_overlaps_rect_boundary(
    first: tuple[Decimal, Decimal],
    second: tuple[Decimal, Decimal],
    rect: tuple[float, float, float, float],
) -> bool:
    x1, y1 = (float(first[0]), float(first[1]))
    x2, y2 = (float(second[0]), float(second[1]))
    left, top, right, bottom = rect
    if y1 == y2 and y1 in {top, bottom}:
        return max(min(x1, x2), left) <= min(max(x1, x2), right)
    if x1 == x2 and x1 in {left, right}:
        return max(min(y1, y2), top) <= min(max(y1, y2), bottom)
    return False


def _validate_rect_scenario(
    record: dict[str, Any],
    prefix: str,
    errors: list[str],
) -> None:
    scenario = record.get("scenario")
    if scenario not in {"contained", "disjoint", "crossing", "boundary"}:
        return
    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        return
    rect = _rect_values(inputs.get("rect"))
    _require(rect is not None, f"{prefix}: invalid rect bounds", errors)
    if rect is None:
        return

    wkt = inputs.get("paths_wkt", inputs.get("lines_wkt"))
    points = _wkt_points(wkt)
    _require(bool(points), f"{prefix}: rect scenario has no input points", errors)
    if not points:
        return

    inside = [_point_inside_rect(point, rect) for point in points]
    if scenario == "contained":
        _require(
            all(inside), f"{prefix}: contained scenario has outside vertices", errors
        )
    elif scenario == "disjoint":
        xs = [float(point[0]) for point in points]
        ys = [float(point[1]) for point in points]
        left, top, right, bottom = rect
        bbox_disjoint = (
            max(xs) < left or min(xs) > right or max(ys) < top or min(ys) > bottom
        )
        _require(
            bbox_disjoint,
            f"{prefix}: disjoint scenario bounding boxes overlap",
            errors,
        )
    elif scenario == "crossing":
        _require(
            any(inside) and not all(inside),
            f"{prefix}: crossing scenario must contain inside and outside vertices",
            errors,
        )
    else:
        segments = zip(points, points[1:])
        _require(
            any(_point_on_rect_boundary(point, rect) for point in points)
            or any(
                _segment_overlaps_rect_boundary(first, second, rect)
                for first, second in segments
            ),
            f"{prefix}: boundary scenario does not touch the rectangle boundary",
            errors,
        )


def _validate_open_path_scenario(
    record: dict[str, Any],
    prefix: str,
    errors: list[str],
) -> None:
    scenario = record.get("scenario")
    if scenario not in {"contained", "disjoint", "crossing", "boundary"}:
        return
    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        return
    rectangular_clip = _is_axis_aligned_rectangle_wkt(inputs.get("rhs_wkt"))
    _require(
        rectangular_clip,
        f"{prefix}: open-path scenario clip must be an axis-aligned rectangle",
        errors,
    )
    if not rectangular_clip:
        return
    rhs_bbox = _wkt_bbox(inputs.get("rhs_wkt"))
    lhs_points = _wkt_points(inputs.get("lhs_wkt"))
    _require(
        rhs_bbox is not None,
        f"{prefix}: open-path scenario has no clip geometry",
        errors,
    )
    _require(
        bool(lhs_points),
        f"{prefix}: open-path scenario has no subject vertices",
        errors,
    )
    if rhs_bbox is None or not lhs_points:
        return

    rect = tuple(float(value) for value in rhs_bbox)
    inside = [_point_inside_rect(point, rect) for point in lhs_points]
    if scenario == "contained":
        _require(
            all(inside),
            f"{prefix}: contained open-path scenario has outside vertices",
            errors,
        )
    elif scenario == "disjoint":
        lhs_bbox = _wkt_bbox(inputs.get("lhs_wkt"))
        assert lhs_bbox is not None
        lhs_left, lhs_top, lhs_right, lhs_bottom = lhs_bbox
        rhs_left, rhs_top, rhs_right, rhs_bottom = rhs_bbox
        bbox_disjoint = (
            lhs_right < rhs_left
            or rhs_right < lhs_left
            or lhs_bottom < rhs_top
            or rhs_bottom < lhs_top
        )
        _require(
            bbox_disjoint,
            f"{prefix}: disjoint open-path scenario bounding boxes overlap",
            errors,
        )
    elif scenario == "crossing":
        _require(
            any(inside) and not all(inside),
            f"{prefix}: crossing open-path scenario must contain inside and "
            "outside vertices",
            errors,
        )
    else:
        segments = zip(lhs_points, lhs_points[1:])
        _require(
            any(_point_on_rect_boundary(point, rect) for point in lhs_points)
            or any(
                _segment_overlaps_rect_boundary(first, second, rect)
                for first, second in segments
            ),
            f"{prefix}: boundary open-path scenario does not touch clip bounds",
            errors,
        )


def _validate_operation_and_options(
    record: dict[str, Any],
    profile: ProfileContract,
    prefix: str,
    errors: list[str],
) -> None:
    operation = record.get("operation")
    allowed_operations = _ALLOWED_OPERATIONS.get(profile.id)
    if allowed_operations is not None:
        _require(
            operation in allowed_operations,
            f"{prefix}: unsupported operation {operation!r} for profile {profile.id}",
            errors,
        )

    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        return
    if profile.id in _FILL_RULE_PROFILES:
        _require(
            inputs.get("fill_rule") in _FILL_RULES,
            f"{prefix}: unsupported or missing fill_rule",
            errors,
        )
    if profile.id == "offset":
        _require(
            _finite_number(inputs.get("delta")),
            f"{prefix}: offset delta must be finite",
            errors,
        )
        _require(
            inputs.get("join_type") in _JOIN_TYPES,
            f"{prefix}: unsupported or missing offset join_type",
            errors,
        )
        _require(
            inputs.get("end_type") in _END_TYPES,
            f"{prefix}: unsupported or missing offset end_type",
            errors,
        )
    if profile.id == "minkowski":
        _require(
            isinstance(inputs.get("is_closed"), bool),
            f"{prefix}: Minkowski is_closed must be boolean",
            errors,
        )
    if profile.id == "batch":
        modes = inputs.get("comparison_modes")
        _require(
            isinstance(modes, list)
            and len(modes) == 3
            and set(modes) == {"legacy_scalar", "next_scalar", "next_batch"},
            f"{prefix}: batch comparison_modes must contain legacy_scalar, "
            "next_scalar, and next_batch",
            errors,
        )
        _require(
            isinstance(inputs.get("requests"), list) and bool(inputs["requests"]),
            f"{prefix}: batch requests must be a non-empty array",
            errors,
        )


def _validate_record_structure(
    record: dict[str, Any],
    profile: ProfileContract,
    case_set: str,
    prefix: str,
    errors: list[str],
) -> None:
    _require(_nonempty_string(record.get("id")), f"{prefix}: missing id", errors)
    _require(
        record.get("profile") == case_set,
        f"{prefix}: profile must be {case_set}",
        errors,
    )
    _require(
        _nonempty_string(record.get("operation")),
        f"{prefix}: missing operation",
        errors,
    )
    _require(
        _nonempty_string(record.get("scenario")),
        f"{prefix}: missing scenario",
        errors,
    )
    _require(
        record.get("partition") in _ALLOWED_PARTITIONS,
        f"{prefix}: partition must be development or release-holdout",
        errors,
    )

    generator = record.get("generator")
    _require(isinstance(generator, dict), f"{prefix}: missing generator", errors)
    if isinstance(generator, dict):
        _require(
            generator.get("name") == "generate_release_profiles",
            f"{prefix}: unexpected generator name",
            errors,
        )
        _require(
            generator.get("version") == 1,
            f"{prefix}: generator version must be 1",
            errors,
        )

    inputs = record.get("inputs")
    _require(
        isinstance(inputs, dict) and bool(inputs),
        f"{prefix}: missing inputs",
        errors,
    )
    _require(
        isinstance(record.get("source"), dict) and bool(record["source"]),
        f"{prefix}: missing source",
        errors,
    )
    tags = record.get("tags")
    _require(
        isinstance(tags, list)
        and bool(tags)
        and all(_nonempty_string(tag) for tag in tags),
        f"{prefix}: tags must contain non-empty strings",
        errors,
    )

    selector = record.get("selector")
    _require(isinstance(selector, dict), f"{prefix}: missing selector", errors)
    if isinstance(selector, dict):
        _require(
            selector.get("method") == "sha256-stratified-v1",
            f"{prefix}: unexpected selector method",
            errors,
        )
        _require(
            _nonempty_string(selector.get("full_id")),
            f"{prefix}: missing selector.full_id",
            errors,
        )

    tolerance = record.get("tolerance")
    _require(isinstance(tolerance, dict), f"{prefix}: missing tolerance", errors)
    if isinstance(tolerance, dict):
        _require(
            _zero_number(tolerance.get("coordinate")),
            f"{prefix}: non-zero coordinate tolerance",
            errors,
        )
        _require(
            _zero_number(tolerance.get("area")),
            f"{prefix}: non-zero area tolerance",
            errors,
        )

    input_digest = record.get("canonical_input_digest")
    options_digest = record.get("canonical_options_digest")
    valid_input_digest = isinstance(input_digest, str) and bool(
        _DIGEST_RE.fullmatch(input_digest)
    )
    valid_options_digest = isinstance(options_digest, str) and bool(
        _DIGEST_RE.fullmatch(options_digest)
    )
    _require(
        valid_input_digest,
        f"{prefix}: missing or invalid canonical_input_digest",
        errors,
    )
    _require(
        valid_options_digest,
        f"{prefix}: missing or invalid canonical_options_digest",
        errors,
    )
    if isinstance(inputs, dict):
        if valid_input_digest:
            _require(
                input_digest == canonical_json_digest(inputs),
                f"{prefix}: canonical_input_digest does not match inputs",
                errors,
            )
        if valid_options_digest:
            _require(
                options_digest == canonical_json_digest(_canonical_options(record)),
                f"{prefix}: canonical_options_digest does not match options",
                errors,
            )

    if case_set == "verification":
        _require(
            isinstance(record.get("expected"), dict),
            f"{prefix}: missing expected",
            errors,
        )
        engines = record.get("reference_engines")
        _require(
            isinstance(engines, list) and bool(engines),
            f"{prefix}: missing reference_engines",
            errors,
        )
        if isinstance(engines, list):
            _require(
                any(
                    isinstance(engine, dict)
                    and engine.get("engine") == "clipper2-legacy"
                    and _nonempty_string(engine.get("version"))
                    for engine in engines
                ),
                f"{prefix}: clipper2-legacy reference engine is required",
                errors,
            )
    else:
        _require(
            isinstance(record.get("complexity"), dict),
            f"{prefix}: missing complexity",
            errors,
        )
        _require(
            isinstance(record.get("parameters"), dict),
            f"{prefix}: missing parameters",
            errors,
        )

    _validate_operation_and_options(record, profile, prefix, errors)


def validate_profile_semantics(
    records: list[dict[str, Any]],
    profile_contract: ProfileContract,
    case_set: str,
) -> list[str]:
    """Validate stable record contracts and profile-specific release semantics."""

    errors: list[str] = []
    record_ids: set[str] = set()
    selector_scenarios: set[tuple[str, str]] = set()
    dimension_counts: Counter[str] = Counter()

    for index, record in enumerate(records, 1):
        prefix = f"{case_set}:{profile_contract.id}[{index}]"
        _validate_record_structure(
            record,
            profile_contract,
            case_set,
            prefix,
            errors,
        )

        record_id = record.get("id")
        if isinstance(record_id, str) and record_id:
            if record_id in record_ids:
                errors.append(f"{prefix}: duplicate record id {record_id!r}")
            record_ids.add(record_id)

        selector = record.get("selector")
        scenario = record.get("scenario")
        if isinstance(selector, dict) and isinstance(scenario, str) and scenario:
            full_id = selector.get("full_id")
            if isinstance(full_id, str) and full_id:
                selector_scenario = (full_id, scenario)
                if selector_scenario in selector_scenarios:
                    errors.append(
                        f"{prefix}: duplicate selector.full_id within scenario "
                        f"{full_id!r}:{scenario!r}"
                    )
                selector_scenarios.add(selector_scenario)

        dimensions = _evidence_dimensions(record)
        dimension_counts.update(dimensions)
        for forbidden in profile_contract.forbidden_scenarios:
            if forbidden in dimensions or record.get("scenario") == forbidden:
                errors.append(f"{prefix}: forbidden scenario {forbidden!r}")

        inputs = record.get("inputs")
        if profile_contract.id == "offset" and isinstance(inputs, dict):
            if _zero_number(inputs.get("delta")):
                errors.append(f"{prefix}: zero-delta release case is forbidden")
        if profile_contract.id == "minkowski" and isinstance(inputs, dict):
            if (
                min(
                    distinct_wkt_points(inputs.get("pattern_wkt")),
                    distinct_wkt_points(inputs.get("path_wkt")),
                )
                < 3
            ):
                errors.append(f"{prefix}: both Minkowski operands must be non-trivial")
        if profile_contract.id in {"rectclip", "rectclip-lines"}:
            _validate_rect_scenario(record, prefix, errors)
        if profile_contract.id == "open-path-overlay":
            _validate_open_path_scenario(record, prefix, errors)

    for dimension, quota in profile_contract.scenario_quotas.items():
        count = dimension_counts[dimension]
        if count < quota.min_count:
            errors.append(
                f"{case_set}:{profile_contract.id}: quota {dimension} requires "
                f"at least {quota.min_count}, found {count}"
            )
        if quota.max_count is not None and count > quota.max_count:
            errors.append(
                f"{case_set}:{profile_contract.id}: quota {dimension} permits "
                f"at most {quota.max_count}, found {count}"
            )
    return errors


def _twin_key(record: dict[str, Any]) -> tuple[str, str, str, str, str] | None:
    selector = record.get("selector")
    if not isinstance(selector, dict):
        return None
    values = (
        selector.get("full_id"),
        record.get("scenario"),
        record.get("operation"),
        record.get("canonical_input_digest"),
        record.get("canonical_options_digest"),
    )
    if not all(isinstance(value, str) and bool(value) for value in values):
        return None
    return values  # type: ignore[return-value]


def validate_twins(
    verification_records: list[dict[str, Any]],
    benchmark_records: list[dict[str, Any]],
) -> list[str]:
    """Require every benchmark workload to have an identical verification twin."""

    verification_keys = {
        key for record in verification_records if (key := _twin_key(record)) is not None
    }
    errors: list[str] = []
    for index, record in enumerate(benchmark_records, 1):
        key = _twin_key(record)
        if key is None or key not in verification_keys:
            errors.append(
                "benchmark record has no identical verification twin: "
                f"{record.get('id', f'index-{index}')}"
            )
    return errors


def validate_partition_disjointness(
    records: list[dict[str, Any]],
) -> list[str]:
    partitions_by_base_id: dict[str, set[str]] = {}
    for record in records:
        selector = record.get("selector")
        partition = record.get("partition")
        if not isinstance(selector, dict) or partition not in _ALLOWED_PARTITIONS:
            continue
        full_id = selector.get("full_id")
        if not isinstance(full_id, str) or not full_id:
            continue
        partitions_by_base_id.setdefault(full_id, set()).add(partition)

    return [
        f"selector.full_id {full_id!r} appears in both development and release-holdout"
        for full_id, partitions in sorted(partitions_by_base_id.items())
        if partitions == _ALLOWED_PARTITIONS
    ]


def _combined_profile_sha256(
    results: list[ProfileResult],
) -> str:
    digest = hashlib.sha256()
    for result in sorted(
        results,
        key=lambda item: (item.profile, item.case_set),
    ):
        digest.update(
            (f"\0{result.profile}\0{result.case_set}\0{result.sha256}").encode("utf-8")
        )
    return digest.hexdigest()


def _error_category(error: str) -> str:
    category = re.sub(r"\[\d+\]", "[]", error)
    if category.startswith("benchmark record has no identical verification twin:"):
        return "benchmark record has no identical verification twin"
    return category


def summarize_errors(
    errors: list[str],
    *,
    max_examples: int = 500,
) -> dict[str, object]:
    if max_examples < 0:
        raise ValueError("max_examples must be non-negative")
    ordered_errors = sorted(errors)
    categories = Counter(_error_category(error) for error in ordered_errors)
    return {
        "error_count": len(ordered_errors),
        "errors_truncated": len(ordered_errors) > max_examples,
        "error_categories": dict(sorted(categories.items())),
        "errors": ordered_errors[:max_examples],
    }


def _result_payload(
    corpus_root: Path,
    contract: ReleaseEvidenceContract,
    results: list[ProfileResult],
    errors: list[str],
) -> dict[str, object]:
    payload = {
        "schema_version": 1,
        "status": "FAIL" if errors else "PASS",
        "corpus_root": str(corpus_root),
        "contract_sha256": contract.sha256,
        "profile_sha256": _combined_profile_sha256(results),
        "profiles": [
            {
                "case_set": result.case_set,
                "profile": result.profile,
                "records": result.records,
                "sha256": result.sha256,
                "path": str(result.path),
            }
            for result in sorted(
                results,
                key=lambda item: (item.profile, item.case_set),
            )
        ],
    }
    payload.update(summarize_errors(errors))
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate geometry corpus release profiles semantically."
    )
    parser.add_argument("corpus_root", type=Path)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT_PATH)
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()

    try:
        contract = load_contract(args.contract)
    except ValueError as error:
        print("status=FAIL")
        print(f"contract error: {error}", file=sys.stderr)
        return 1

    required_profile_ids = release_gated_profile_ids(contract)
    results: list[ProfileResult] = []
    errors: list[str] = []
    records_by_profile: dict[tuple[str, str], list[dict[str, Any]]] = {}
    all_records: list[dict[str, Any]] = []

    for profile in contract.profiles.values():
        if profile.id not in required_profile_ids:
            continue
        for case_set in profile.required_case_sets:
            path = args.corpus_root / "normalized" / case_set / f"{profile.id}.jsonl"
            records, load_errors = load_profile(path, case_set)
            records_by_profile[(profile.id, case_set)] = records
            all_records.extend(records)
            errors.extend(load_errors)
            errors.extend(validate_profile_semantics(records, profile, case_set))
            if path.is_file():
                results.append(
                    ProfileResult(
                        case_set=case_set,
                        profile=profile.id,
                        path=path,
                        records=len(records),
                        sha256=file_sha256(path),
                    )
                )

        errors.extend(
            validate_twins(
                records_by_profile.get((profile.id, "verification"), []),
                records_by_profile.get((profile.id, "benchmark"), []),
            )
        )

    errors.extend(validate_partition_disjointness(all_records))
    errors = sorted(set(errors))
    payload = _result_payload(args.corpus_root, contract, results, errors)
    if args.output_json is not None:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    print(f"status={payload['status']}")
    print(f"profiles={len(results)}")
    print(f"profile_sha256={payload['profile_sha256']}")
    reported_errors = payload["errors"]
    assert isinstance(reported_errors, list)
    for error in reported_errors[:100]:
        print(error, file=sys.stderr)
    if payload["error_count"] > len(reported_errors[:100]):
        print(
            "... "
            f"{payload['error_count'] - len(reported_errors[:100])} "
            "additional errors summarized in the JSON report",
            file=sys.stderr,
        )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
