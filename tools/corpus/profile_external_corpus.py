#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable


POINT_LIMIT = 4096
STRICT_SMALL_PATH_LIMIT = 256
csv.field_size_limit(sys.maxsize)


Point = tuple[int, int]
Path64 = list[Point]
Paths64 = list[Path64]


@dataclass
class SourceProfile:
    case_count: int = 0
    total_raw_points: int = 0
    max_raw_points: int = 0
    contained_rectangle_cases: int = 0
    fast_path_eligible: int = 0
    small_strict_fast_path_eligible: int = 0
    large_relaxed_fast_path_eligible: int = 0
    strict_small_path_rejected: int = 0
    over_cap_rejected: int = 0
    non_intersection_cases: int = 0
    non_rectangle_clip_cases: int = 0
    non_contained_cases: int = 0
    invalid_path_cases: int = 0
    multipath_ambiguous_cases: int = 0
    cleaned_path_cases: int = 0
    removable_point_count: int = 0
    near_point_cases: int = 0
    collinear_path_cases: int = 0


@dataclass
class CorpusProfile:
    total_cases: int = 0
    sources: dict[str, SourceProfile] = field(default_factory=dict)


@dataclass
class CleanResult:
    path: Path64
    removed_points: int


NUMBER_RE = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?")
RING_RE = re.compile(r"\(([-+0-9.,\sEe]+)\)")


def source_name(case_name: str) -> str:
    return case_name.split("/", 1)[0]


def parse_wkt_paths(wkt: str, scale: int) -> Paths64:
    paths: Paths64 = []
    for ring_match in RING_RE.finditer(wkt):
        values = [float(match.group(0)) for match in NUMBER_RE.finditer(ring_match.group(1))]
        if len(values) < 6 or len(values) % 2 != 0:
            continue
        path: Path64 = []
        for index in range(0, len(values), 2):
            path.append((round(values[index] * scale), round(values[index + 1] * scale)))
        if path:
            paths.append(path)
    return paths


def trimmed_size(path: Path64) -> int:
    size = len(path)
    while size > 1 and path[0] == path[size - 1]:
        size -= 1
    return size


def raw_point_count(paths: Paths64) -> int:
    return sum(len(path) for path in paths)


def cross(first: Point, second: Point, third: Point) -> int:
    return (second[0] - first[0]) * (third[1] - second[1]) - (
        second[1] - first[1]
    ) * (third[0] - second[0])


def dot(first: Point, second: Point, third: Point) -> int:
    return (second[0] - first[0]) * (third[0] - second[0]) + (
        second[1] - first[1]
    ) * (third[1] - second[1])


def is_collinear(first: Point, second: Point, third: Point) -> bool:
    return cross(first, second, third) == 0


def points_are_really_close(first: Point, second: Point) -> bool:
    return abs(first[0] - second[0]) < 2 and abs(first[1] - second[1]) < 2


def path_has_near_points(path: Path64, path_size: int) -> bool:
    return any(points_are_really_close(path[index], path[(index + 1) % path_size]) for index in range(path_size))


def path_has_collinear_triplet(path: Path64, path_size: int) -> bool:
    for index in range(path_size):
        previous = path[index - 1 if index > 0 else path_size - 1]
        if is_collinear(previous, path[index], path[(index + 1) % path_size]):
            return True
    return False


def path_is_safe_for_strict_passthrough(path: Path64, path_size: int) -> bool:
    for index in range(path_size):
        previous = path[index - 1 if index > 0 else path_size - 1]
        current = path[index]
        next_point = path[(index + 1) % path_size]
        if points_are_really_close(current, next_point) or is_collinear(previous, current, next_point):
            return False
    return True


def should_remove_passthrough_point(previous: Point, current: Point, next_point: Point) -> bool:
    return is_collinear(previous, current, next_point) and (
        current == previous or current == next_point or dot(previous, current, next_point) < 0
    )


def clean_contained_path_for_passthrough(source: Path64) -> CleanResult | None:
    path_size = trimmed_size(source)
    if path_size < 3:
        return None
    path = list(source[:path_size])
    removed = 0
    index = 0
    while len(path) >= 3 and index < len(path):
        previous_index = len(path) - 1 if index == 0 else index - 1
        next_index = (index + 1) % len(path)
        if not should_remove_passthrough_point(path[previous_index], path[index], path[next_index]):
            index += 1
            continue
        del path[index]
        removed += 1
        if index > 0:
            index -= 1
    if len(path) < 3:
        return None
    return CleanResult(path=path, removed_points=removed)


def path_as_axis_aligned_rect(path: Path64) -> tuple[int, int, int, int] | None:
    size = trimmed_size(path)
    if size != 4:
        return None
    points = path[:size]
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    left, right = min(xs), max(xs)
    top, bottom = min(ys), max(ys)
    if left == right or top == bottom:
        return None
    corners = {(left, top), (right, top), (right, bottom), (left, bottom)}
    if set(points) != corners:
        return None
    return (left, top, right, bottom)


def point_strictly_inside(rect: tuple[int, int, int, int], point: Point) -> bool:
    left, top, right, bottom = rect
    return left < point[0] < right and top < point[1] < bottom


def path_bounds(path: Path64) -> tuple[int, int, int, int]:
    xs = [point[0] for point in path]
    ys = [point[1] for point in path]
    return (min(xs), min(ys), max(xs), max(ys))


def bounds_intersect(first: tuple[int, int, int, int], second: tuple[int, int, int, int]) -> bool:
    return first[0] <= second[2] and first[2] >= second[0] and first[1] <= second[3] and first[3] >= second[1]


def bounds_contains(first: tuple[int, int, int, int], second: tuple[int, int, int, int]) -> bool:
    return first[0] <= second[0] and first[1] <= second[1] and first[2] >= second[2] and first[3] >= second[3]


def point_in_polygon(point: Point, path: Path64) -> bool:
    inside = False
    previous = path[-1]
    for current in path:
        if (current[1] > point[1]) != (previous[1] > point[1]):
            x_intersection = (previous[0] - current[0]) * (point[1] - current[1]) / (
                previous[1] - current[1]
            ) + current[0]
            if point[0] < x_intersection:
                inside = not inside
        previous = current
    return inside


def containment_hierarchy_is_unambiguous(paths: Paths64) -> bool:
    bounds = [path_bounds(path) for path in paths]
    for first in range(len(paths)):
        for second in range(first + 1, len(paths)):
            if not bounds_intersect(bounds[first], bounds[second]):
                continue
            first_contains_second = bounds_contains(bounds[first], bounds[second]) and point_in_polygon(
                paths[second][0], paths[first]
            )
            second_contains_first = bounds_contains(bounds[second], bounds[first]) and point_in_polygon(
                paths[first][0], paths[second]
            )
            if first_contains_second == second_contains_first:
                return False
    return True


def classify_case(operation: str, subjects: Paths64, clips: Paths64, stats: SourceProfile) -> None:
    stats.case_count += 1
    stats.total_raw_points += raw_point_count(subjects)
    stats.max_raw_points = max(stats.max_raw_points, raw_point_count(subjects))
    if operation != "intersection":
        stats.non_intersection_cases += 1
        return
    if len(clips) != 1:
        stats.non_rectangle_clip_cases += 1
        return
    rect = path_as_axis_aligned_rect(clips[0])
    if rect is None:
        stats.non_rectangle_clip_cases += 1
        return
    if not subjects:
        stats.invalid_path_cases += 1
        return
    total_points = raw_point_count(subjects)
    if total_points > POINT_LIMIT:
        stats.over_cap_rejected += 1
        return
    path_sizes: list[int] = []
    for path in subjects:
        path_size = trimmed_size(path)
        if path_size < 3:
            stats.invalid_path_cases += 1
            return
        if not all(point_strictly_inside(rect, path[index]) for index in range(path_size)):
            stats.non_contained_cases += 1
            return
        if path_has_near_points(path, path_size):
            stats.near_point_cases += 1
        if path_has_collinear_triplet(path, path_size):
            stats.collinear_path_cases += 1
        path_sizes.append(path_size)
    stats.contained_rectangle_cases += 1

    cleaned_paths: Paths64 = []
    saw_cleaned_path = False
    removable_points = 0
    uses_large_relaxed_path = False
    for path, path_size in zip(subjects, path_sizes):
        if path_size <= STRICT_SMALL_PATH_LIMIT and not path_is_safe_for_strict_passthrough(path, path_size):
            stats.strict_small_path_rejected += 1
            return
        if path_size > STRICT_SMALL_PATH_LIMIT:
            uses_large_relaxed_path = True
        cleaned = clean_contained_path_for_passthrough(path)
        if cleaned is None:
            stats.invalid_path_cases += 1
            return
        if cleaned.removed_points > 0:
            saw_cleaned_path = True
            removable_points += cleaned.removed_points
        cleaned_paths.append(cleaned.path)
    if not containment_hierarchy_is_unambiguous(cleaned_paths):
        stats.multipath_ambiguous_cases += 1
        return
    stats.fast_path_eligible += 1
    if uses_large_relaxed_path:
        stats.large_relaxed_fast_path_eligible += 1
    else:
        stats.small_strict_fast_path_eligible += 1
    if saw_cleaned_path:
        stats.cleaned_path_cases += 1
        stats.removable_point_count += removable_points


def iter_tsv_rows(corpus_dir: Path) -> Iterable[dict[str, str]]:
    for path in sorted(corpus_dir.rglob("*.tsv")):
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.reader((line for line in handle if not line.startswith("#")), delimiter="\t")
            for row in reader:
                if len(row) < 5:
                    continue
                yield {
                    "name": row[0],
                    "operation": row[1],
                    "scale": row[2],
                    "subject_wkt": row[3],
                    "clip_wkt": row[4],
                }


def profile_corpus(corpus_dir: Path) -> CorpusProfile:
    profile = CorpusProfile()
    for row in iter_tsv_rows(corpus_dir):
        scale = int(row["scale"])
        subjects = parse_wkt_paths(row["subject_wkt"], scale)
        clips = parse_wkt_paths(row["clip_wkt"], scale)
        name = source_name(row["name"])
        stats = profile.sources.setdefault(name, SourceProfile())
        classify_case(row["operation"], subjects, clips, stats)
        profile.total_cases += 1
    return profile


def profile_as_dict(profile: CorpusProfile) -> dict[str, object]:
    return {
        "total_cases": profile.total_cases,
        "sources": {name: asdict(stats) for name, stats in sorted(profile.sources.items())},
    }


def format_markdown(profile: CorpusProfile) -> str:
    lines = [
        "# External Corpus Fast Path Profile",
        "",
        f"Total cases: **{profile.total_cases}**",
        "",
        "| Source | Cases | Fast path | Small strict | Large cleaned | Strict rejects | Over cap | Non-contained | Max raw points | Removable points |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for name, stats in sorted(profile.sources.items()):
        lines.append(
            "| "
            + " | ".join(
                [
                    name,
                    str(stats.case_count),
                    str(stats.fast_path_eligible),
                    str(stats.small_strict_fast_path_eligible),
                    str(stats.large_relaxed_fast_path_eligible),
                    str(stats.strict_small_path_rejected),
                    str(stats.over_cap_rejected),
                    str(stats.non_contained_cases),
                    str(stats.max_raw_points),
                    str(stats.removable_point_count),
                ]
            )
            + " |"
        )
    lines.append("")
    lines.append("The profile models the current contained-rectangle fast-path admission rules.")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Profile external WKT corpus fast-path eligibility.")
    parser.add_argument("corpus_dir", type=Path)
    parser.add_argument("--output-md", type=Path)
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()

    profile = profile_corpus(args.corpus_dir)
    if args.output_json is not None:
        args.output_json.write_text(json.dumps(profile_as_dict(profile), indent=2), encoding="utf-8")
    markdown = format_markdown(profile)
    if args.output_md is not None:
        args.output_md.write_text(markdown, encoding="utf-8")
    else:
        print(markdown, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
