#!/usr/bin/env python3
"""Independent integer/Fraction directed-region and actual mesh qualification."""

from __future__ import annotations
import argparse
import hashlib
from collections import defaultdict
from fractions import Fraction
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.release.mathematical_validation import require_normal_math_mode

require_normal_math_mode()
import sys


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def area2(path):
    return sum(a[0] * b[1] - a[1] * b[0] for a, b in edges(path))


def edges(path):
    return list(zip(path, path[1:] + path[:1]))


def on(p, a, b):
    return (
        min(a[0], b[0]) <= p[0] <= max(a[0], b[0])
        and min(a[1], b[1]) <= p[1] <= max(a[1], b[1])
        and cross(a, b, p) == 0
    )


def proper(a, b, c, d):
    return cross(a, b, c) * cross(a, b, d) < 0 and cross(c, d, a) * cross(c, d, b) < 0


def candidates(segments):
    """Conservative bounding-box sweep; every intersecting pair is retained."""
    boxes = [
        (min(a[0], b[0]), max(a[0], b[0]), min(a[1], b[1]), max(a[1], b[1]), i)
        for i, (a, b) in enumerate(segments)
    ]
    active = []
    for box in sorted(boxes):
        active = [other for other in active if other[1] >= box[0]]
        for other in active:
            if other[2] <= box[3] and box[2] <= other[3]:
                yield other[4], box[4]
        active.append(box)


def winding(path, p):
    # Normalize the rational witness once; every edge comparison is integer.
    from math import lcm

    x, y = map(Fraction, p)
    denominator = lcm(x.denominator, y.denominator)
    px = x.numerator * (denominator // x.denominator)
    py = y.numerator * (denominator // y.denominator)
    w = 0
    for a, b in edges(path):
        ay = a[1] * denominator
        by = b[1] * denominator
        if (ay <= py < by) or (by <= py < ay):
            c = (b[0] - a[0]) * (py - ay) - (b[1] - a[1]) * (px - a[0] * denominator)
            if ay <= py < by and c > 0:
                w += 1
            if by <= py < ay and c < 0:
                w -= 1
    return w


def interior_probe(a, b, sign, extent):
    d = (-(b[1] - a[1]) * sign, (b[0] - a[0]) * sign)
    # Every nonzero midpoint orientation has magnitude >=1/2. The exact
    # displacement changes every orientation and height by strictly <1/4.
    bound = max(abs(d[0]), abs(d[1]), extent[0] * abs(d[1]) + extent[1] * abs(d[0]))
    epsilon = Fraction(1, 4 * (bound + 1))
    return (
        Fraction(a[0] + b[0], 2) + epsilon * d[0],
        Fraction(a[1] + b[1], 2) + epsilon * d[1],
    )


def clean(paths):
    result = []
    for path in paths:
        ring = []
        for p in path:
            if not ring or p != ring[-1]:
                ring.append(p)
        if len(ring) > 1 and ring[0] == ring[-1]:
            ring.pop()
        if len(ring) >= 3 and any(cross(ring[0], ring[1], p) for p in ring[2:]):
            result.append(ring)
    return result


def domain(paths):
    rings = clean(paths)
    if not rings:
        return "empty", [], {}
    segments = [edge for ring in rings for edge in edges(ring)]
    points = [p for edge in segments for p in edge]
    extent = (
        max(p[0] for p in points) - min(p[0] for p in points),
        max(p[1] for p in points) - min(p[1] for p in points),
    )
    splits = [set(edge) for edge in segments]
    for i, j in candidates(segments):
        a, b = segments[i]
        c, d = segments[j]
        if proper(a, b, c, d):
            return "crossing", rings, {}
        for p in (a, b):
            if on(p, c, d):
                splits[j].add(p)
        for p in (c, d):
            if on(p, a, b):
                splits[i].add(p)
    signs = [(area2(r) > 0) - (area2(r) < 0) for r in rings]
    if 0 in signs:
        return "invalid", rings, {}
    atomic = []
    offset = 0
    for ring in rings:
        pieces = []
        for _ in ring:
            a, b = segments[offset]
            points = sorted(
                splits[offset],
                key=lambda p: (
                    (p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])
                ),
            )
            pieces.extend(zip(points, points[1:]))
            offset += 1
        atomic.append(pieces)
    contains = [[False] * len(rings) for _ in rings]
    for i, pieces in enumerate(atomic):
        for j, ring in enumerate(rings):
            if i == j:
                continue
            values = {
                winding(ring, interior_probe(a, b, signs[i], extent)) != 0
                for a, b in pieces
            }
            if len(values) != 1:
                return "invalid", rings, {}
            contains[i][j] = values.pop()
    roots = []
    for i in range(len(rings)):
        parents = [j for j in range(len(rings)) if contains[i][j]]
        if any(contains[j][i] for j in parents):
            return "invalid", rings, {}
        if any(
            not contains[j][k] and not contains[k][j]
            for j in parents
            for k in parents
            if j != k
        ):
            return "invalid", rings, {}
        roots.append(signs[i] * (-1) ** len(parents))
    if len(set(roots)) != 1:
        return "invalid", rings, {}
    direction = roots[0]
    normalized = [r if direction > 0 else r[::-1] for r in rings]
    constraints = defaultdict(list)
    for pieces in atomic:
        for a, b in pieces:
            if direction < 0:
                a, b = b, a
            constraints[tuple(sorted((a, b)))].append((a, b))
            left = interior_probe(a, b, 1, extent)
            right = interior_probe(a, b, -1, extent)
            if sum(winding(r, left) for r in normalized) != 1 or sum(
                winding(r, right) for r in normalized
            ) not in (0, 1):
                return "invalid", rings, {}
    for uses in constraints.values():
        if len(uses) > 2 or len(set(uses)) != len(uses):
            return "invalid", rings, {}
    return "region", normalized, dict(constraints)


def incircle(a, b, c, d):
    x, y = a[0] - d[0], a[1] - d[1]
    u, v = b[0] - d[0], b[1] - d[1]
    p, q = c[0] - d[0], c[1] - d[1]
    return (
        (x * x + y * y) * (u * q - v * p)
        - (u * u + v * v) * (x * q - y * p)
        + (p * p + q * q) * (x * v - y * u)
    )


def qualify(record, prepared_domain=None):
    kind, rings, constraints = (
        prepared_domain if prepared_domain is not None else domain(record["input"])
    )
    error, status, triangles = (
        record["outer_error"],
        record["domain_status"],
        record["triangles"],
    )
    if kind == "invalid":
        assert error == 12 and status == 1 and not triangles, (
            "invalid directed structure not explicitly rejected"
        )
        return {"domain": kind}
    assert error == 0, "unexpected outer error"
    if kind != "region":
        assert status == ({"empty": 2, "crossing": 3}[kind]) and not triangles, (
            "wrong domain status"
        )
        return {"domain": kind}
    assert status == 0 and triangles, "nondegenerate region did not produce a mesh"
    source_vertices = {p for r in rings for p in r}
    incidence = defaultdict(list)
    total = 0
    for t in triangles:
        assert len(t) == 3 and len(set(t)) == 3, "invalid triangle vertices"
        assert all(p in source_vertices for p in t), (
            "mesh moved or introduced a source vertex"
        )
        value = cross(*t)
        assert value, "zero area triangle"
        if value < 0:
            t = t[::-1]
        total += abs(value)
        center = (Fraction(sum(p[0] for p in t), 3), Fraction(sum(p[1] for p in t), 3))
        assert sum(winding(r, center) for r in rings) == 1, (
            "triangle outside filled region"
        )
        for a, b in edges(t):
            opposite = next(p for p in t if p not in (a, b))
            incidence[tuple(sorted((a, b)))].append((a, b, opposite))
    mesh = list(incidence)
    for i, j in candidates(mesh):
        a, b = mesh[i]
        c, d = mesh[j]
        assert not proper(a, b, c, d), "proper mesh edge crossing"
        assert not any(p not in (c, d) and on(p, c, d) for p in (a, b)), (
            "unsplit mesh contact/overlap"
        )
        assert not any(p not in (a, b) and on(p, a, b) for p in (c, d)), (
            "unsplit mesh contact/overlap"
        )
    # Contacts and seams carry topology, unlike a redundant degree-two
    # collinear source point. Their vertices must survive in the actual mesh.
    neighbors = defaultdict(set)
    source_degree = defaultdict(int)
    for (a, b), uses in constraints.items():
        neighbors[a].add(b)
        neighbors[b].add(a)
        source_degree[a] += len(uses)
        source_degree[b] += len(uses)
    required = {p for p in neighbors if len(neighbors[p]) > 2 or source_degree[p] > 2}
    mesh_vertices = {p for t in triangles for p in t}
    assert required <= mesh_vertices, "missing mandatory contact/seam vertex"
    for p in required:
        assert not any(p not in e and on(p, *e) for e in mesh), (
            "unsplit mandatory contact/seam"
        )
    # Exact projection intersections permit removal of ordinary collinear
    # degree-two points, while retaining the entire oriented source coverage.
    assigned = set()
    for (a, b), uses in constraints.items():
        axis = 0 if a[0] != b[0] else 1
        source_low, source_high = sorted((a[axis], b[axis]))
        intervals = []
        candidates_mesh = [(a, b)] if (a, b) in incidence else mesh
        for e in candidates_mesh:
            c, d = e
            if cross(a, b, c) or cross(a, b, d):
                continue
            low = max(source_low, min(c[axis], d[axis]))
            high = min(source_high, max(c[axis], d[axis]))
            if high <= low:
                continue
            intervals.append((low, high))
            entries = incidence[e]
            assert len(entries) == len(uses), "wrong boundary/seam incidence"
            expected = sorted((v[axis] > u[axis]) for u, v in uses)
            assert sorted(v[axis] > u[axis] for u, v, _ in entries) == expected, (
                "wrong boundary orientation"
            )
            assigned.add(e)
        assert intervals, "missing constraint"
        cursor = source_low
        for low, high in sorted(intervals):
            assert low == cursor and high > low, "constraint coverage gap/overlap"
            cursor = high
        assert cursor == source_high, "incomplete constraint coverage"
    for e, entries in incidence.items():
        if e not in assigned:
            assert len(entries) == 2, "uncovered mesh boundary"
        assert len(entries) <= 2, "mesh nonmanifold edge"
        if len(entries) == 2:
            a, b, c = entries[0]
            u, v, d = entries[1]
            assert (a, b) == (v, u), "overlapping triangle sides"
            if (
                record["use_delaunay"]
                and e not in assigned
                and cross(c, d, a) * cross(c, d, b) < 0
            ):
                assert incircle(a, b, c, d) <= 0, "illegal flippable Delaunay edge"
    assert total == sum(area2(r) for r in rings), "exact area mismatch"
    return {
        "domain": kind,
        "area2": total,
        "triangles": len(triangles),
        "constraints": len(constraints),
    }


def strict_json(text):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            assert key not in result, "duplicate JSON field"
            result[key] = value
        return result

    return json.loads(text, object_pairs_hook=unique)


def validate_record(value):
    assert isinstance(value, dict) and set(value) == {
        "version",
        "case_index",
        "use_delaunay",
        "input",
        "outer_error",
        "domain_status",
        "triangles",
    }, "unknown record fields"
    assert (
        type(value["version"]) is int
        and value["version"] == 1
        and type(value["case_index"]) is int
        and value["case_index"] >= 0
    ), "bad record identity"
    assert type(value["use_delaunay"]) is bool, "bad mode"
    assert type(value["outer_error"]) is int and value["outer_error"] in (0, 12), (
        "unknown outer error"
    )
    assert type(value["domain_status"]) is int and value["domain_status"] in range(4), (
        "unknown status"
    )
    for key in ("input", "triangles"):
        assert isinstance(value[key], list), "bad paths"
        for path in value[key]:
            assert isinstance(path, list), "bad path"
            for point in path:
                assert (
                    isinstance(point, list)
                    and len(point) == 2
                    and all(type(c) is int and abs(c) < 2**61 for c in point)
                ), "bad point"
        value[key] = [[tuple(p) for p in path] for path in value[key]]
    return value


def verify(records, expected_count):
    pools = {"TRI_CONTRACT": {}, "TRI_LEGACY": {}}
    for line in Path(records).read_text(encoding="utf-8-sig").splitlines():
        prefix = line.split(" ", 1)[0]
        if prefix not in pools:
            assert not line.startswith("TRI_"), "unknown triangulation protocol"
            continue
        value = validate_record(strict_json(line[len(prefix) + 1 :]))
        index = value["case_index"]
        assert index not in pools[prefix], "duplicate record"
        pools[prefix][index] = value
    expected = set(range(expected_count))
    assert expected_count > 0 and all(
        set(pool) == expected for pool in pools.values()
    ), "missing/extra record"
    results = []
    for i in range(expected_count):
        actual, legacy = pools["TRI_CONTRACT"][i], pools["TRI_LEGACY"][i]
        assert (
            actual["input"] == legacy["input"]
            and actual["use_delaunay"] == legacy["use_delaunay"]
        ), "engine inputs differ"
        prepared = domain(actual["input"])
        try:
            product = qualify(actual, prepared)
        except AssertionError as error:
            raise AssertionError(f"case {i}: {error}") from error
        try:
            qualify(legacy, prepared)
            legacy_error = None
        except AssertionError as error:
            legacy_error = str(error)

        def canonical(ts):
            return sorted(tuple(sorted(t)) for t in ts)

        same = (
            actual["outer_error"] == legacy["outer_error"]
            and actual["domain_status"] == legacy["domain_status"]
            and canonical(actual["triangles"]) == canonical(legacy["triangles"])
        )
        results.append(
            {
                "case_index": i,
                **product,
                "legacy_valid": legacy_error is None,
                "legacy_error": legacy_error,
                "difference_class": "none"
                if same
                else ("legacy-invalid" if legacy_error else "both-valid-alternative"),
            }
        )
    return {
        "status": "PASS",
        "cases": expected_count,
        "records_sha256": hashlib.sha256(Path(records).read_bytes()).hexdigest(),
        "results": results,
    }


def bind_profile(records, profile, expected_count, verdict=None):
    import re
    from decimal import Decimal

    source = [
        json.loads(line)
        for line in Path(profile).read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    assert len(source) == expected_count, "profile count mismatch"
    actual = {}
    for line in Path(records).read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("TRI_CONTRACT "):
            record = json.loads(line[len("TRI_CONTRACT ") :])
            assert record["case_index"] not in actual, "duplicate source binding"
            actual[record["case_index"]] = record
    assert set(actual) == set(range(expected_count)), "missing source binding"
    for index, row in enumerate(source):
        assert row["comparison_domain"] == "triangulation-exact-contract", (
            "undeclared comparison domain"
        )
        paths = []
        for ring in re.findall(r"\(([^()]*)\)", row["inputs"]["polygon_wkt"]):
            points = []
            for point in ring.split(","):
                coordinates = point.strip().split()
                assert len(coordinates) == 2, "unsupported WKT dimensions"
                values = [Decimal(c) for c in coordinates]
                # This frozen integer corpus is exactly representable in both
                # MSVC binary64 and extended long double. Other WKT numeric
                # domains require a separate parser-semantics qualification.
                assert all(v == int(v) and abs(v) <= 2**53 for v in values), (
                    "unproved WKT numeric binding"
                )
                points.append([int(v) for v in values])
            if len(points) > 1 and points[0] == points[-1]:
                points.pop()
            if len(points) >= 3:
                paths.append(points)
        assert actual[index]["input"] == paths, (
            "executed input differs from frozen profile"
        )
        assert actual[index]["use_delaunay"] == (
            row["operation"] == "triangulation.delaunay"
        ), "executed options differ from profile"
        if verdict is not None:
            verdict["results"][index]["id"] = row["id"]
            verdict["results"][index]["canonical_input_digest"] = row[
                "canonical_input_digest"
            ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--expected-count", type=int, required=True)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--profile", type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.records, args.expected_count)
        if args.profile:
            bind_profile(args.records, args.profile, args.expected_count, result)
    except (AssertionError, ValueError, KeyError, OSError) as error:
        result = {"status": "FAIL", "error": str(error)}
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "results"}))
    return int(result["status"] != "PASS")


if __name__ == "__main__":
    sys.exit(main())
