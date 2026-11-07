#!/usr/bin/env python3
"""Execute a declared Offset producer, verify its exact records, then allow timing.

This is Offset evidence only. T11 must also require the independent external
profile manifest and remaining algorithm qualifications; fixtures cannot replace
any external corpus quota. Each invocation freezes its source before execution.
"""

from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tests" / "offset"))
from tools.corpus.generate_release_profiles import derive_offset_contract_source
from tools.release.corpus_authority_manifest import (
    validate_offset_contract_source_manifest,
)
from offset_contract_oracle import verify
from tools.release.actual_contract_producer import capture


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args):
    producer = args.producer.resolve()
    source = args.fixture.resolve()
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "offset-contract-result.json").unlink(missing_ok=True)
    derive_offset_contract_source(source, directory)
    manifest = directory / "offset-contract-manifest.json"
    cases = directory / "offset-contract-cases.jsonl"
    frozen = sha(manifest)
    binary_digest = sha(producer)
    (directory / "pre-execution-freeze.json").write_text(
        json.dumps(
            {"manifest_sha256": frozen, "producer_sha256": binary_digest}, indent=2
        )
        + "\n"
    )
    mappings = validate_offset_contract_source_manifest(manifest, source, cases, frozen)
    records = directory / "actual-records.log"
    producer_identity = capture(producer, args.producer_argument, records, binary_digest)
    validate_offset_contract_source_manifest(manifest, source, cases, frozen)
    verdict = verify(cases, records, len(mappings))
    verdict["producer"] = producer_identity
    verdict["manifest_sha256"] = frozen
    verdict["scope"] = "Offset analytic source; external profiles additionally required"
    output = directory / "offset-contract-result.json"
    output.write_text(json.dumps(verdict, indent=2) + "\n")
    differences = sum(r["difference_class"] != "none" for r in verdict["results"])
    print(
        f"Offset contract PASS cases={len(mappings)} legacy_differences={differences} result={output}"
    )
    if args.timing_argument:
        # The same executable is timed only after its actual preflight records
        # pass. No timing process runs for missing, malformed or invalid output.
        if sha(producer) != binary_digest:
            raise ValueError("producer changed before timing")
        if any(sha(Path(path)) != digest for path, digest in producer_identity["product_libraries"].items()):
            raise ValueError("product library changed before timing")
        return subprocess.run([str(producer), *args.timing_argument]).returncode
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--producer", required=True, type=Path)
    parser.add_argument("--producer-argument", action="append", default=[])
    parser.add_argument("--timing-argument", action="append", default=[])
    parser.add_argument(
        "--fixture",
        type=Path,
        default=ROOT / "tests/offset/offset_contract_cases.jsonl",
    )
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        return run(args)
    except (AssertionError, ValueError, KeyError, OSError) as error:
        (args.output_dir / "offset-contract-result.json").write_text(
            json.dumps({"status": "FAIL", "error": str(error)}) + "\n"
        )
        print("Offset contract FAIL:", error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
