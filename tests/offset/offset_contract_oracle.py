#!/usr/bin/env python3
"""Absolute Offset contracts. Only Python integer/Fraction/Decimal mathematics.

The input fixture is fixed before either engine executes. Records must contain
those same actual request values and each engine's output; this verifier never
runs a geometry binary and never uses one engine as the other engine's oracle.
"""

from __future__ import annotations
import argparse
import hashlib
import json
import sys
from decimal import Decimal, localcontext
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.release.mathematical_validation import require_normal_math_mode
require_normal_math_mode()
from tools.release.correction_authority import (
    authority_digest,
    canonical_digest,
    canonical_options,
    validate_authority,
    declared_rounding_mismatch,
)


def area2(path):
    return sum(a[0] * b[1] - a[1] * b[0] for a, b in zip(path, path[1:] + path[:1]))


def canonical(path):
    points = [tuple(p) for p in path]
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    if not points:
        return ()
    return min(tuple(points[i:] + points[:i]) for i in range(len(points)))


def boundaries(paths):
    return sorted(canonical(path) for path in paths)


def rectangle(x0, y0, x1, y1):
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]


def basis(inputs):
    for group in inputs["groups"]:
        if group["end_type"] == "polygon":
            for path in group["paths"]:
                if area2(path):
                    return area2(path) < 0
    return False


def thin_expected(inputs):
    (group,) = inputs["groups"]
    (path,) = group["paths"]
    y = min(p[1] for p in path)
    assert set(map(tuple, path)) == {(0, y), (1000003, y + 1), (1000002, y + 1)}
    assert group["join_type"] == "bevel" and group["end_type"] == "polygon"
    assert abs(area2(path)) == 1 and inputs["delta"] in (-2, 2)
    # Height=1 bounds the inradius <= 1/2, so inward distance 2 leaves no point.
    if inputs["delta"] < 0:
        return []
    positive = [[0, y], [1000003, y + 1], [1000002, y + 1]]
    endpoints = []
    with localcontext() as ctx:
        ctx.prec = 80
        for a, b in zip(positive, positive[1:] + positive[:1]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            length2 = dx * dx + dy * dy
            root = Decimal(length2).sqrt()
            for point in (a, b):
                out = [
                    Decimal(point[0]) + 2 * Decimal(dy) / root,
                    Decimal(point[1]) - 2 * Decimal(dx) / root,
                ]
                rounded = [int(v.to_integral_value()) for v in out]
                assert all(
                    abs(v - Decimal(q)) < Decimal("0.01") for v, q in zip(out, rounded)
                )
                endpoints.append(rounded)
            # Rational bounds independently establish noncritical quantization:
            # horizontal drift < .01 and vertical drift from two < .01.
            if dy:
                assert Fraction(4 * dy * dy, length2) < Fraction(1, 10000)
                assert Fraction(4 * dx * dx, length2) > Fraction(199, 100) ** 2
    expected = [
        [0, y + 2],
        [0, y - 2],
        [1000003, y - 1],
        [1000003, y + 3],
        [1000002, y + 3],
    ]
    assert set(map(tuple, endpoints)) == set(map(tuple, expected))
    assert area2(expected) == 8000025
    return [expected]


def absolute_expected(contract, inputs, legacy=False):
    assert inputs["callback"] in ("none", "constant:10", "constant:-10")
    assert inputs["miter_limit"] == 2 and not inputs["preserve_collinear"]
    if contract == "exact-area-thin-bevel":
        paths = thin_expected(inputs)
    elif contract == "cardinal-rounding":
        (group,) = inputs["groups"]
        assert group == {
            "paths": [[[0, 0]]],
            "join_type": "round",
            "end_type": "polygon",
        }
        assert inputs["delta"] == 2.5 and inputs["arc_segments_per_quadrant"] == 1
        radius = (
            2 if inputs["coordinate_rounding"] == "nearest_even" and not legacy else 3
        )
        paths = [[[radius, 0], [0, radius], [-radius, 0], [0, -radius]]]
    elif contract == "group-parameter-isolation":
        assert inputs["delta"] == -10
        assert sorted(len(p) for g in inputs["groups"] for p in g["paths"]) == [1, 4]
        assert all(
            g["join_type"] == "miter" and g["end_type"] == "polygon"
            for g in inputs["groups"]
        )
        assert any(g["paths"] == [rectangle(0, 0, 100, 100)] for g in inputs["groups"])
        assert any(g["paths"] == [[[300, 300]]] for g in inputs["groups"])
        # Axis-aligned support lines translate by exactly -delta.
        inset = Fraction(-inputs["delta"])
        assert inset == 10
        paths = [rectangle(int(inset), int(inset), int(100 - inset), int(100 - inset))]
    elif contract == "mixed-group-winding":
        assert inputs["delta"] == 10 and len(inputs["groups"]) == 2
        assert {g["end_type"] for g in inputs["groups"]} == {"polygon", "butt"}
        for g in inputs["groups"]:
            assert g["join_type"] == "miter"
            if g["end_type"] == "polygon":
                assert set(map(tuple, g["paths"][0])) == set(
                    map(tuple, rectangle(0, 0, 100, 100))
                )
            else:
                assert g["end_type"] == "butt" and g["paths"] == [[[200, 0], [300, 0]]]
        paths = [rectangle(-10, -10, 110, 110), rectangle(200, -10, 300, 10)]
    elif contract == "constant-callback-analytic":
        assert inputs["callback"] in ("constant:10", "constant:-10")
        (group,) = inputs["groups"]
        (path,) = group["paths"]
        assert group["join_type"] == "miter"
        if group["end_type"] == "butt":
            assert set(map(tuple, path)) == {(200, 0), (300, 0)}
            # Butt caps use |delta|; reversing the two-point path exchanges ends.
            paths = [rectangle(200, -10, 300, 10)]
        else:
            assert group["end_type"] == "joined"
            assert set(map(tuple, path)) == set(map(tuple, rectangle(200, 0, 300, 100)))
            # Positive distance yields an annulus between translated support
            # lines; a negative signed callback reverses this native winding,
            # so Positive fill admits no region, in either input orientation.
            paths = (
                []
                if inputs["callback"] == "constant:-10"
                else [
                    rectangle(190, -10, 310, 110),
                    list(reversed(rectangle(210, 10, 290, 90))),
                ]
            )
    else:
        raise AssertionError("unknown analytic Offset contract: " + contract)
    if basis(inputs) != inputs["reverse_solution"]:
        paths = [list(reversed(p)) for p in paths]
    return paths


def check_actual(case, actual, legacy=False):
    expected = absolute_expected(case["contract_id"], case["inputs"], legacy)
    if actual["outer_error"] != "ok":
        return False
    paths = actual["paths"]
    if any(
        not isinstance(v, int) or isinstance(v, bool)
        for p in paths
        for q in p
        for v in q
    ):
        return False
    # Full oriented boundaries imply exact region/role equality; area is an
    # independent check, never a substitute for checking the entire boundary.
    return boundaries(paths) == boundaries(expected) and sum(map(area2, paths)) == sum(
        map(area2, expected)
    )


def verify(fixture, records, expected_count):
    cases = [
        json.loads(line)
        for line in Path(fixture).read_text().splitlines()
        if line.strip()
    ]
    assert len(cases) == expected_count, "fixture count mismatch"
    seen = {}
    for line in Path(records).read_text(encoding="utf-8-sig").splitlines():
        prefix, _, payload = line.partition(" ")
        if prefix not in ("OFFSET_CONTRACT", "OFFSET_LEGACY"):
            continue
        value = json.loads(payload)
        index = value["case_index"]
        engine = "next" if prefix == "OFFSET_CONTRACT" else "legacy"
        assert (index, engine) not in seen, "duplicate actual case"
        assert 0 <= index < len(cases), "unexpected actual case"
        case = cases[index]
        assert not validate_authority(case), "invalid declared authority"
        assert value["contract_id"] == case["contract_id"], "contract mismatch"
        assert value["inputs"] == case["inputs"], (
            "actual request differs from declared input"
        )
        assert canonical_digest(value["inputs"]) == case["canonical_input_digest"], (
            "input digest mismatch"
        )
        assert (
            canonical_digest(canonical_options(value["inputs"]))
            == case["canonical_options_digest"]
        ), "options digest mismatch"
        assert authority_digest(case) == case["authority_digest"], (
            "authority digest mismatch"
        )
        if declared_rounding_mismatch(case):
            assert boundaries(
                case["expected"]["legacy_absolute"]["paths"]
            ) == boundaries(
                absolute_expected(case["contract_id"], case["inputs"], True)
            ), "declared legacy absolute differs from analytic authority"
        assert boundaries(case["expected"]["absolute"]["paths"]) == boundaries(
            absolute_expected(case["contract_id"], case["inputs"])
        ), "declared absolute differs from analytic authority"
        seen[index, engine] = value
    assert len(seen) == len(cases) * 2, "missing engine or case"
    results = []
    for index, case in enumerate(cases):
        actual, legacy = seen[index, "next"], seen[index, "legacy"]
        good = check_actual(case, actual)
        legacy_good = check_actual(case, legacy, declared_rounding_mismatch(case))
        assert good, f"next violates absolute Offset contract: {case['id']}"

        def representation(paths):
            return sorted(
                min(tuple(map(tuple, p[i:] + p[:i])) for i in range(len(p)))
                for p in paths
            )

        equal = representation(actual["paths"]) == representation(legacy["paths"])
        difference = (
            "none"
            if equal
            else "api-rounding-semantics-mismatch"
            if declared_rounding_mismatch(case) and legacy_good
            else "legacy-invalid-result"
            if not legacy_good
            else "unknown"
        )
        assert difference != "unknown", (
            f"unqualified Offset representation difference: {case['id']}"
        )
        verdict = lambda valid: {
            "executed": True,
            "valid": valid,
            "oracle": "python-int-fraction-analytic",
            "checks": {"absolute": valid},
        }
        results.append(
            {
                "id": case["id"],
                **{
                    key: case[key]
                    for key in (
                        "contract_id",
                        "comparison_domain",
                        "authority_digest",
                        "canonical_input_digest",
                        "canonical_options_digest",
                    )
                },
                "next": verdict(good),
                "legacy": verdict(legacy_good),
                "representation_equal": equal,
                "difference_class": difference,
            }
        )
    return {
        "status": "PASS",
        "authority": "offset-contract",
        "cases": len(cases),
        "records_sha256": hashlib.sha256(Path(records).read_bytes()).hexdigest(),
        "results": results,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--records", required=True, type=Path)
    parser.add_argument("--expected-count", required=True, type=int)
    parser.add_argument("--result", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.fixture, args.records, args.expected_count)
    except (AssertionError, KeyError, ValueError) as error:
        args.result.write_text(json.dumps({"status": "FAIL", "error": str(error)}))
        print("Offset contract FAIL:", error)
        return 1
    args.result.write_text(json.dumps(result, indent=2) + "\n")
    print(
        f"Offset contract PASS cases={result['cases']} legacy_differences={sum(r['difference_class'] != 'none' for r in result['results'])}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
