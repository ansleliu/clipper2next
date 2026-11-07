#!/usr/bin/env python3
"""Freeze source and producer, capture actual records, independently qualify, then time."""

from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tests/triangulation"))
from tools.release.actual_contract_producer import capture, sha
from tools.release.corpus_authority_manifest import (
    validate_frozen_corpus_manifest,
    validate_vendored_profile_manifest,
)
from tools.release.evidence_contract import load_contract
from triangulation_contract_oracle import verify, bind_profile
from benchmarks.tools.common.external_core_measurement import validate_benchmark_records
from benchmarks.tools.common.release_gate_policy import EXTERNAL_CORE_BENCHMARK_GROUPS

TRIANGULATION_TIMING_ROWS = dict(EXTERNAL_CORE_BENCHMARK_GROUPS)["triangulation_pair"]


def run_timing(producer, arguments, directory, binary_digest, expected_count):
    result_path = directory / "timing-result.json"
    output = directory / "timing.json"
    log = directory / "timing.log"
    output.unlink(missing_ok=True)
    try:
        if any(argument.startswith("--benchmark_out") for argument in arguments):
            raise ValueError(
                "timing output paths/format are owned by the preflight runner"
            )
        repetitions = 1
        for index, argument in enumerate(arguments):
            if argument == "--benchmark_repetitions":
                if index + 1 == len(arguments):
                    raise ValueError("missing benchmark repetition count")
                repetitions = int(arguments[index + 1])
            elif argument.startswith("--benchmark_repetitions="):
                repetitions = int(argument.split("=", 1)[1])
        if repetitions <= 0:
            raise ValueError("benchmark repetitions must be positive")
        command_arguments = list(arguments)
        if not any(argument.startswith("--benchmark_filter") for argument in arguments):
            command_arguments.append(
                "--benchmark_filter=^(" + "|".join(TRIANGULATION_TIMING_ROWS) + ")$"
            )
        command_arguments += [
            f"--benchmark_out={output}",
            "--benchmark_out_format=json",
            "--benchmark_report_aggregates_only=false",
            f"--benchmark_repetitions={repetitions}",
        ]
        identity = capture(producer, command_arguments, log, binary_digest)
        payload = json.loads(output.read_text(encoding="utf-8"))
        validate_benchmark_records(
            payload,
            TRIANGULATION_TIMING_ROWS,
            expected_case_count=expected_count,
            expected_repetitions=repetitions,
        )
        result = {
            "status": "PASS",
            "scope": "timing completion only; performance unqualified",
            "producer": identity,
            "benchmark_json_sha256": sha(output),
            "required_rows": list(TRIANGULATION_TIMING_ROWS),
            "cases_per_iteration": expected_count,
            "repetitions": repetitions,
        }
    except (ValueError, KeyError, OSError) as error:
        result_path.write_text(
            json.dumps({"status": "FAIL", "error": str(error)}) + "\n", encoding="utf-8"
        )
        raise
    result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


def _run(args, validate_corpus, scope):
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "triangulation-contract-result.json").unlink(missing_ok=True)
    (directory / "timing-result.json").unlink(missing_ok=True)
    producer = args.producer.resolve()
    binary_digest = sha(producer)
    frozen = {"producer_sha256": binary_digest}
    profile = None
    if args.external:
        selected = os.environ.get("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT")
        if not selected:
            raise ValueError("external corpus root is required")
        args.corpus_root = Path(selected)
    if args.corpus_root:
        if args.original_root is None:
            raise ValueError(
                "a selected corpus requires its explicit original source root"
            )
        root = args.corpus_root.resolve()
        manifest = root / "authority-manifest.json"
        frozen["manifest_sha256"] = getattr(args, "frozen_corpus_sha256", None) or sha(
            manifest
        )
        original = args.original_root.resolve()
        validate_corpus(
            manifest, original, root, frozen["manifest_sha256"], load_contract()
        )
        profile = root / "normalized" / args.case_set / "triangulation.jsonl"
        frozen["profile_sha256"] = sha(profile)
        ambient = os.environ.get("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT")
        if ambient and Path(ambient).resolve() != root:
            raise ValueError(
                "ambient corpus differs from explicit frozen configuration"
            )
        os.environ["CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT"] = str(root)
    (directory / "pre-execution-freeze.json").write_text(
        json.dumps(frozen, indent=2) + "\n"
    )
    records = directory / "actual-records.log"
    identity = capture(producer, args.producer_argument, records, binary_digest)
    if profile:
        validate_corpus(
            manifest, original, root, frozen["manifest_sha256"], load_contract()
        )
        if sha(profile) != frozen["profile_sha256"]:
            raise ValueError("profile changed during execution")
        bind_profile(records, profile, args.expected_count)
    if args.capture_only:
        return 0
    verdict = verify(records, args.expected_count)
    if profile:
        bind_profile(records, profile, args.expected_count, verdict)
    verdict["producer"] = identity
    verdict["freeze"] = frozen
    verdict["scope"] = scope
    (directory / "triangulation-contract-result.json").write_text(
        json.dumps(verdict, indent=2) + "\n"
    )
    print(f"Triangulation contract PASS cases={args.expected_count}")
    if args.timing_argument:
        if sha(producer) != binary_digest:
            raise ValueError("producer changed before timing")
        if any(
            sha(Path(path)) != digest
            for path, digest in identity["product_libraries"].items()
        ):
            raise ValueError("product library changed before timing")
        return run_timing(
            producer,
            args.timing_argument,
            directory,
            binary_digest,
            args.expected_count,
        )
    return 0


def run(args):
    return _run(
        args,
        validate_frozen_corpus_manifest,
        "full-external" if args.external or args.corpus_root else "repository-analytic",
    )


def run_vendored(args):
    if (
        args.external
        or args.original_root is None
        or args.original_root.resolve() != ROOT / "tests/oracle/corpus/geometry"
    ):
        raise ValueError(
            "vendored preflight only accepts the original tracked profile source"
        )
    if not args.corpus_root or not args.frozen_corpus_sha256:
        raise ValueError(
            "vendored preflight requires its configured root and pre-execution freeze"
        )
    return _run(
        args,
        validate_vendored_profile_manifest,
        "vendored-profile-only; no external eligibility",
    )


def main(*, vendored=False):
    p = argparse.ArgumentParser()
    p.add_argument("--producer", required=True, type=Path)
    p.add_argument("--producer-argument", action="append", default=[])
    p.add_argument("--timing-argument", action="append", default=[])
    p.add_argument("--expected-count", required=True, type=int)
    p.add_argument("--output-dir", required=True, type=Path)
    p.add_argument("--corpus-root", type=Path)
    p.add_argument("--frozen-corpus-sha256")
    p.add_argument("--external", action="store_true")
    p.add_argument(
        "--original-root",
        type=Path,
        default=os.environ.get("CLIPPER2NEXT_GEOMETRY_CORPUS_SOURCE_ROOT"),
    )
    p.add_argument(
        "--case-set", choices=("verification", "benchmark"), default="verification"
    )
    p.add_argument("--capture-only", action="store_true")
    args = p.parse_args()
    try:
        return run_vendored(args) if vendored else run(args)
    except (AssertionError, ValueError, KeyError, OSError) as error:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        if not (args.output_dir / "timing-result.json").exists():
            (args.output_dir / "triangulation-contract-result.json").write_text(
                json.dumps({"status": "FAIL", "error": str(error)}) + "\n"
            )
        scope = (
            "timing"
            if (args.output_dir / "timing-result.json").exists()
            else "contract"
        )
        print(f"Triangulation {scope} FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
