#!/usr/bin/env python3
"""Require exact PASS JUnit evidence for every contracted corpus test."""

from __future__ import annotations

import argparse
import hashlib
import json
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
from tools.release.correction_authority import validate_qualification
from tools.release.corpus_authority_manifest import (
    validate_frozen_corpus_manifest, validate_offset_contract_source_manifest,
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
    parser.add_argument("--qualification", type=Path, help="same-run case execution and authority verdicts")
    parser.add_argument("--corpus-manifest", type=Path, help="frozen source and case mapping for the executed corpus")
    parser.add_argument("--source-corpus-root", type=Path)
    parser.add_argument("--corpus-root", type=Path, help="explicitly selected derived geometry parent root")
    parser.add_argument("--frozen-manifest-sha256", help="trusted digest retained before execution, outside submitted evidence")
    parser.add_argument("--offset-contract-manifest", type=Path)
    parser.add_argument("--offset-contract-source", type=Path)
    parser.add_argument("--offset-contract-cases", type=Path)
    parser.add_argument("--frozen-offset-manifest-sha256")
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
    if args.qualification is None:
        failures.append("missing case-level authority qualification; JUnit PASS alone is insufficient")
    else:
        try:
            payload = json.loads(args.qualification.read_text(encoding="utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("qualification must be an object")
            failures.extend(validate_qualification(payload, set(contract.profiles)))
            if payload.get("junit_sha256") != hashlib.sha256(args.junit.read_bytes()).hexdigest():
                failures.append("qualification JUnit hash does not match this run")
            if payload.get("contract_sha256") != contract.sha256:
                failures.append("qualification contract hash does not match")
            if args.corpus_manifest is None or args.source_corpus_root is None or args.corpus_root is None or not args.frozen_manifest_sha256:
                failures.append("qualification requires selected source/derived roots, corpus manifest and independently frozen manifest SHA256")
            else:
                observed_sha256, verification = validate_frozen_corpus_manifest(
                    args.corpus_manifest, args.source_corpus_root, args.corpus_root,
                    args.frozen_manifest_sha256, contract,
                )
                if payload.get("corpus_manifest_sha256") != observed_sha256:
                    failures.append("qualification corpus manifest hash does not match")
                if (args.offset_contract_manifest is None or args.offset_contract_source is None
                        or args.offset_contract_cases is None or not args.frozen_offset_manifest_sha256):
                    raise ValueError("qualification requires independently frozen Offset analytic source manifest")
                formal = validate_offset_contract_source_manifest(
                    args.offset_contract_manifest, args.offset_contract_source,
                    args.offset_contract_cases, args.frozen_offset_manifest_sha256,
                )
                if payload.get("offset_contract_manifest_sha256") != args.frozen_offset_manifest_sha256:
                    failures.append("qualification Offset analytic manifest hash does not match")
                if {case["id"] for case in formal} & {case["id"] for case in verification}:
                    raise ValueError("formal and external case identities overlap")
                # Analytic cases are additional mandatory cases, never replacements
                # for external profile quotas or source-bound execution records.
                verification = verification + formal
                cases = payload.get("cases")
                if not isinstance(cases, list):
                    raise ValueError("qualification requires case list")
                planned = {case["id"]: case for case in cases if isinstance(case, dict) and isinstance(case.get("id"), str)}
                if len(verification) != len(planned) or {case["id"] for case in verification} != set(planned):
                    failures.append("qualification must execute every frozen verification case exactly once")
                for case in verification:
                    actual = planned.get(case["id"], {})
                    for key in ("profile_id", "operation", "canonical_input_digest", "canonical_options_digest", "comparison_domain", "contract_id", "authority_digest"):
                        if case[key] != actual.get(key):
                            failures.append(f"qualification frozen case binding mismatch: {case['id']} {key}")
        except (OSError, ValueError) as error:
            failures.append(f"qualification error: {error}")
    if failures:
        passed = sum(results.get(test) == "PASS" for test in required_tests)
        print(f"status=FAIL required_tests={len(required_tests)} passed={passed}")
        for failure in failures:
            print(f"required external corpus test is not PASS: {failure}")
        return 1

    print(f"status=PASS required_tests={len(required_tests)} authority=domain-contracts cases={len(payload['cases'])} legacy_comparison=reported-separately")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
