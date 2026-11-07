"""Independent integer contracts for the actual build's triangulation probe."""

import argparse
from collections import defaultdict
from itertools import permutations
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from tools.release.mathematical_validation import require_normal_math_mode
require_normal_math_mode()
import random
import subprocess

M = (1 << 61) - 1
SEED = 20261003
PERMUTATIONS = [
    (p, (-1) ** sum(p[i] > p[j] for i in range(4) for j in range(i + 1, 4)))
    for p in permutations(range(4))
]


def sign(x):
    return (x > 0) - (x < 0)


def determinant(points):
    rows = [[x, y, x * x + y * y, 1] for x, y in points]
    return sum(
        s * rows[0][p[0]] * rows[1][p[1]] * rows[2][p[2]] * rows[3][p[3]]
        for p, s in PERMUTATIONS
    )


def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def distance(points):
    a, b = points
    return sum((a[i] - b[i]) ** 2 for i in range(2))


def words(value, count):
    return [(value >> (64 * i)) & ((1 << 64) - 1) for i in range(count)]


def intersection(points):
    a, b, c, d = points
    if a == b or c == d:
        return 0
    o = [orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)]
    if all(x == 0 for x in o):
        axis = 0 if a[0] != b[0] else 1
        return int(
            max(min(a[axis], b[axis]), min(c[axis], d[axis]))
            < min(max(a[axis], b[axis]), max(c[axis], d[axis]))
        )
    return 2 if o[0] * o[1] < 0 and o[2] * o[3] < 0 else 0


def line(op, points):
    return op + " " + " ".join(str(x) for p in points for x in p)


def mesh_check(response, paths, delaunay):
    fields = response.split()
    assert fields[:2] == ["OK", "0"], response
    count = int(fields[2])
    data = list(map(int, fields[3:]))
    assert len(data) == count * 6
    triangles = [
        [tuple(data[i + j : i + j + 2]) for j in (0, 2, 4)]
        for i in range(0, len(data), 6)
    ]
    vertices = {p for path in paths for p in path}
    boundary = {
        tuple(sorted((a, b)))
        for path in paths
        for a, b in zip(path, path[1:] + path[:1])
    }
    edge_opposites = defaultdict(list)
    total_area = 0
    for triangle in triangles:
        a, b, c = triangle
        assert set(triangle) <= vertices
        assert orient(a, b, c) != 0
        total_area += abs(orient(a, b, c))
        for x, y, opposite in ((a, b, c), (b, c, a), (c, a, b)):
            edge_opposites[tuple(sorted((x, y)))].append(opposite)
    area = lambda path: abs(
        sum(a[0] * b[1] - a[1] * b[0] for a, b in zip(path, path[1:] + path[:1]))
    )
    assert total_area == area(paths[0]) - sum(area(path) for path in paths[1:])
    assert {edge for edge, opp in edge_opposites.items() if len(opp) == 1} == boundary
    for (a, b), opposite in edge_opposites.items():
        assert len(opposite) in (1, 2)
        if len(opposite) != 2:
            continue
        c, d = opposite
        assert orient(a, b, c) * orient(a, b, d) < 0
        if delaunay and orient(c, d, a) * orient(c, d, b) < 0:
            assert determinant([a, b, c, d]) * orient(a, b, c) <= 0, (
                "illegal diagonal",
                a,
                b,
                c,
                d,
            )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    args = parser.parse_args()
    probe = args.probe.resolve(strict=True)
    rng = random.Random(SEED)
    cases = []

    def add(op, points, expected):
        assert all(-M <= x <= M for p in points for x in p)
        cases.append((line(op, points), expected))

    n = 1 << 27
    quad = [(0, -n), (-n, 0), (0, n), (n, 1)]
    assert determinant(quad) == 2 * n * n
    incircles = [
        quad,
        [(-M, -M), (M, -M), (M, M), (-M, M)],
        [(-M, -M), (M, -M), (M, M), (-M, M - 1)],
        [(0, 0), (1, 0), (2, 0), (0, 1)],
        [(1, 1)] * 4,
    ]
    for limit in (1, 16383, 16384, 1 << 30, M):
        for _ in range(250):
            incircles.append(
                [
                    (rng.randint(-limit, limit), rng.randint(-limit, limit))
                    for _ in range(4)
                ]
            )
    # Scaling crosses the exact precision boundary without changing the sign.
    for _ in range(100):
        points = [(rng.randint(-100, 100), rng.randint(-100, 100)) for _ in range(4)]
        incircles += [points, [(x * (1 << 40), y * (1 << 40)) for x, y in points]]
        incircles += [
            [(x + shift, y + shift) for x, y in points] for shift in (M - 100, -M + 100)
        ]
    for edge in (16383, 16384, -16383, -16384, (1 << 30)-1, 1 << 30, (1 << 30)+1, -(1 << 30)-1, -(1 << 30), -(1 << 30)+1):
        incircles += [
            [(edge, 0), (0, edge), (-edge, 0), (0, 0)],
            [(edge, edge), (0, edge), (edge, 0), (0, 0)],
        ]
    for points in incircles:
        add("I", points, f"{sign(determinant(points))} {sign(orient(*points[:3]))}")
        reversed_points = [points[1], points[0], *points[2:]]
        add("I", reversed_points, f"{sign(determinant(reversed_points))} {sign(orient(*reversed_points[:3]))}")

    # M/R operands are complete int64 scalars/vectors, not admitted public points.
    low, high = -(1 << 63), (1 << 63) - 1
    products = [(low, low, high, high), (low, high, high, low),
                (0, low, 0, high), (high, high - 1, high, high),
                (low, 0, 0, low), (low, low, low, high)]
    products_rng = random.Random(SEED ^ 0x5349474E)
    for _ in range(1000):
        products.append(tuple(products_rng.randint(low, high) for _ in range(4)))
    for a, b, c, d in products:
        cases.append((f"M {a} {b} {c} {d}", str(sign(a * b - c * d))))
        cases.append((f"R {a} {b} {c} {d}", str(sign(a * d - b * c))))

    segments = []
    for limit in (1 << 30, M):
        segments.append([(0, 0), (limit, limit - 1), (1, 1), (limit - 1, limit - 2)])
    segments += [
        [(0, 0), (10, 0), (5, 0), (15, 0)],
        [(0, 0), (10, 0), (11, 0), (15, 0)],
        [(0, 0), (10, 0), (0, 1), (10, 1)],
        [(0, 0), (10, 0), (5, 0), (5, 5)],
        [(0, 0), (0, 0), (-1, 0), (1, 0)],
        [(0, 0), (10, 0), (10, 0), (10, 10)],
    ]
    for _ in range(500):
        segments.append([(rng.randint(-M, M), rng.randint(-M, M)) for _ in range(4)])
    for a, b, c, d in segments:
        for points in ([a, b, c, d], [b, a, c, d], [a, b, d, c], [c, d, a, b]):
            add("S", points, str(intersection(points)))

    # Exact admitted-domain point ranking, including full-span 125-bit squares
    # and one-unit distinctions lost by double, with translation near each limit.
    distances = [[(0, 0), (3, 4)], [(M, M), (-M, -M)], [(0, 0), (0, 0)]]
    for shift in (0, M - 100, -M):
        distances += [[(shift, shift), (shift + 3, shift + 4)],
                      [(shift, shift), (shift + 4, shift + 4)]]
    for pair in ([[(-M, -M), (M, M)], [(-M, -M), (M, M - 1)]],
                 [[(0, 0), (M, 0)], [(0, 0), (M, 1)]]):
        for x, y in ((0, 1), (1, 0), (0, 0)):
            add("C", pair[x] + pair[y], str(sign(distance(pair[x]) - distance(pair[y]))))
        distances += pair
    for limit in (16383, 16384, 1 << 30, M):
        for _ in range(600):
            distances.append([(rng.randint(-limit, limit), rng.randint(-limit, limit))
                              for _ in range(2)])
    for points in distances:
        expected = " ".join(map(str, words(distance(points), 2)))
        add("P", points, expected)
        add("P", points[::-1], expected)
    for first, second in zip(distances, distances[1:]):
        add("C", first + second, str(sign(distance(first) - distance(second))))

    meshes = []
    for points in (
        quad,
        [(0, 0), (100, 0), (100, 100), (0, 100)],
        [(-M, -M), (M, -M), (M, M), (-M, M)],
        [(M - 100, M - 100), (M, M - 100), (M, M), (M - 100, M)],
    ):
        for reflect in (1, -1):
            for reverse in (False, True):
                transformed = [(reflect * x, y) for x, y in points]
                if reverse:
                    transformed.reverse()
                for mode in (0, 1):
                    meshes.append(([transformed], mode))
    outer = [(0, 0), (1000, 0), (1000, 1000), (0, 1000)]
    holes = [
        [(100, 100), (100, 300), (300, 300), (300, 100)],
        [(600, 600), (600, 800), (800, 800), (800, 600)],
    ]
    meshes += [([outer, *holes], mode) for mode in (0, 1)]
    for paths, mode in meshes:
        request = f"T {mode} {len(paths)} " + " ".join(
            str(len(path)) + " " + line("", path).strip() for path in paths
        )
        cases.append((request, (paths, mode)))
    cases += [(f"T 1 1 3 {M + 1} 0 0 10 10 0", "ERR 4")]
    result = subprocess.run(
        [str(probe)],
        input="\n".join(c[0] for c in cases) + "\n",
        text=True,
        capture_output=True,
        check=True,
    )
    actual = result.stdout.splitlines()
    assert len(actual) == len(cases), (len(actual), len(cases), result.stderr)
    for index, ((request, expected), response) in enumerate(zip(cases, actual)):
        if isinstance(expected, tuple):
            mesh_check(response, *expected)
        else:
            assert response == expected, (index, request, expected, response)
    for invalid in (
        "I 0 0",
        "Q 0",
        "M 0 0",
        "R 9223372036854775808 0 0 0",
        f"P {M + 1} 0 0 0",
        "I 0 0 0 0 0 0 0 0 trailing",
        "T 2 0",
    ):
        rejected = subprocess.run(
            [str(probe)], input=invalid + "\n", text=True, capture_output=True
        )
        assert rejected.returncode != 0 and not rejected.stdout
    print(
        f"PASS: {len(cases)} exact contracts, {len(meshes)} final meshes, seed={SEED}, probe={probe}"
    )


if __name__ == "__main__":
    main()
