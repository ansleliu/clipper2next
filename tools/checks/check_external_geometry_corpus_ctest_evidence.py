#!/usr/bin/env python3
"""Require exact PASS JUnit evidence for every contracted corpus test."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.collect_release_runtime_evidence import (  # noqa: E402
    collect_ctest_results,
)
from tools.release.evidence_contract import (  # noqa: E402
    DEFAULT_CONTRACT_PATH,
    ReleaseEvidenceContract,
    load_contract,
)


_EXTERNAL_TEST_PREFIX = "ExternalGeometryCorpus."


def required_external_profile_tests(
    contract: ReleaseEvidenceContract,
) -> tuple[str, ...]:
    """Return unique external-corpus tests required by all public algorithms."""

    required = {
        test_name
        for algorithm in contract.algorithms.values()
        for test_name in algorithm.required_tests
        if test_name.startswith(_EXTERNAL_TEST_PREFIX)
    }
    if not required:
        raise ValueError(
            "release contract declares no required ExternalGeometryCorpus tests"
        )
    return tuple(sorted(required))


def failed_requirements(
    results: dict[str, str],
    required_tests: tuple[str, ...],
) -> list[str]:
    failures: list[str] = []
    for test_name in required_tests:
        status = results.get(test_name)
        if status != "PASS":
            failures.append(f"{test_name}: {status or 'MISSING'}")
    return failures


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Verify exact PASS CTest JUnit evidence for every contracted "
            "ExternalGeometryCorpus tests."
        )
    )
    parser.add_argument("junit", type=Path)
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT_PATH)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        contract = load_contract(args.contract)
        required_tests = required_external_profile_tests(contract)
        results = collect_ctest_results(args.junit)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    failures = failed_requirements(results, required_tests)
    if failures:
        passed = len(required_tests) - len(failures)
        print(f"status=FAIL required_tests={len(required_tests)} passed={passed}")
        for failure in failures:
            print(f"required external corpus test is not PASS: {failure}")
        return 1

    print(f"status=PASS required_tests={len(required_tests)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
