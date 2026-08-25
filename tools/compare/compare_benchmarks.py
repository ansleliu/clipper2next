#!/usr/bin/env python3
import json
import sys


def load(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def benchmark_map(payload):
    return {
        item["name"]: item
        for item in payload.get("benchmarks", [])
        if item.get("run_type") == "iteration"
    }


def main(argv):
    if len(argv) != 3:
        print("usage: compare_benchmarks.py BASELINE CANDIDATE", file=sys.stderr)
        return 2

    baseline = benchmark_map(load(argv[1]))
    candidate = benchmark_map(load(argv[2]))

    for name, base_item in baseline.items():
        next_item = candidate.get(name)
        if next_item is None:
            continue
        base_time = float(base_item["real_time"])
        next_time = float(next_item["real_time"])
        if base_time == 0.0:
            continue
        ratio = next_time / base_time
        print(f"{name}: candidate/base real_time ratio {ratio:.3f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
