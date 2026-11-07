"""Derive, freeze, validate and consume one selected immutable corpus authority."""

from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import uuid
import shutil

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.corpus.generate_release_profiles import derive_authority_profiles
from tools.release.corpus_authority_manifest import (
    validate_frozen_corpus_manifest,
    validate_vendored_profile_manifest,
)
from tools.release.evidence_contract import load_contract, DEFAULT_CONTRACT_PATH
from tools.checks.check_geometry_corpus_profiles import file_sha256
from tools.release.source_inventory import file_record

ROOT = Path(__file__).resolve().parents[2]
LEGACY_FILES = (
    "Polygons.txt",
    "Lines.txt",
    "Offsets.txt",
    "PolytreeHoleOwner.txt",
    "PolytreeHoleOwner2.txt",
)


def stage_legacy_inputs(source: Path, destination: Path) -> list[dict]:
    records = []
    for name in LEGACY_FILES:
        relative = Path("sources/clipper2-rust/Tests/data") / name
        original = file_record(source / relative)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / relative, target)
        records.append({"source": original, "derived": file_record(target)})
    return records


def validate_legacy_inputs(records: list[dict]) -> None:
    if len(records) != len(LEGACY_FILES) or {
        Path(r["source"]["path"]).name for r in records
    } != set(LEGACY_FILES):
        raise ValueError("the five original legacy text inputs must be frozen")
    for pair in records:
        if (
            file_record(Path(pair["source"]["path"])) != pair["source"]
            or file_record(Path(pair["derived"]["path"])) != pair["derived"]
            or pair["source"]["sha256"] != pair["derived"]["sha256"]
        ):
            raise ValueError("legacy source or derived bytes changed")


def prepare(
    source: Path,
    destination: Path,
    *,
    release_eligibility: bool = True,
    legacy_source: Path | None = None,
) -> dict:
    if destination.exists():
        raise ValueError("corpus producer requires a fresh destination")
    derive_authority_profiles(source, destination)
    manifest = destination / "authority-manifest.json"
    frozen = file_sha256(manifest)
    receipt = {
        "manifest": str(manifest.resolve()),
        "sha256": frozen,
        "source_root": str(source.resolve()),
        "derived_root": str(destination.resolve()),
        "release_eligibility": release_eligibility,
    }
    if legacy_source is not None:
        receipt["legacy_inputs"] = stage_legacy_inputs(legacy_source, destination)
    with (destination.parent / (destination.name + "-freeze.json")).open(
        "x", encoding="utf-8"
    ) as stream:
        json.dump(receipt, stream, indent=2)
    validator = (
        validate_frozen_corpus_manifest
        if release_eligibility
        else validate_vendored_profile_manifest
    )
    validator(
        manifest, source, destination, frozen, load_contract(DEFAULT_CONTRACT_PATH)
    )
    return receipt


def configure_selection(
    output: Path,
    legacy_source: Path,
    full_receipt: Path | None,
    full_receipt_sha256: str | None,
) -> Path:
    if (full_receipt is None) != (full_receipt_sha256 is None):
        raise ValueError("full selection requires a receipt and its pre-execution hash")
    selected = None
    scope = "vendored-profile"
    if full_receipt is not None:
        if file_sha256(full_receipt) != full_receipt_sha256:
            raise ValueError("full receipt differs from explicit configuration binding")
        selected = json.loads(full_receipt.read_text(encoding="utf-8"))
        validate_frozen_corpus_manifest(
            Path(selected["manifest"]),
            Path(selected["source_root"]),
            Path(selected["derived_root"]),
            selected["sha256"],
            load_contract(),
        )
        validate_legacy_inputs(selected.get("legacy_inputs", []))
        scope = "full-external"
    target = output / str(uuid.uuid4())
    vendored = prepare(
        ROOT / "tests/oracle/corpus/geometry",
        target,
        release_eligibility=False,
        legacy_source=legacy_source,
    )
    if selected is None:
        selected = vendored
    selection = {
        "schema_version": 1,
        "scope": scope,
        "selected": selected,
        "vendored": vendored,
    }
    path = output / (target.name + "-selection.json")
    with path.open("x", encoding="utf-8") as stream:
        json.dump(selection, stream, indent=2)
    return path.resolve()


def check_selection(path: Path, digest: str) -> dict:
    if file_sha256(path) != digest:
        raise ValueError("configuration selection changed after its freeze")
    selection = json.loads(path.read_text(encoding="utf-8"))
    receipt = selection["vendored"]
    validate_vendored_profile_manifest(
        Path(receipt["manifest"]),
        ROOT / "tests/oracle/corpus/geometry",
        Path(receipt["derived_root"]),
        receipt["sha256"],
        load_contract(),
    )
    validate_legacy_inputs(receipt["legacy_inputs"])
    return selection


def main():
    import subprocess

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--configure", action="store_true")
    parser.add_argument("--legacy-source", type=Path)
    parser.add_argument("--full-receipt", type=Path)
    parser.add_argument("--full-receipt-sha256")
    parser.add_argument("--check-selection", type=Path)
    parser.add_argument("--selection-sha256")
    args = parser.parse_args()
    if args.configure:
        if not args.legacy_source or not args.output_dir:
            parser.error(
                "oracle configuration requires explicit legacy-source and output-dir"
            )
        print(
            configure_selection(
                args.output_dir,
                args.legacy_source,
                args.full_receipt,
                args.full_receipt_sha256,
            )
        )
        return 0
    if args.check_selection:
        selection = check_selection(args.check_selection, args.selection_sha256)
        target = Path(selection["vendored"]["derived_root"])
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/checks/check_geometry_corpus_profiles.py"),
                str(target),
            ],
            check=False,
        ).returncode
    if not args.source or not args.output_dir:
        parser.error("source and output-dir are required for a standalone profile gate")
    target = args.output_dir / str(uuid.uuid4())
    # Vendored profiles exercise authority semantics, not external full-corpus quotas.
    receipt = prepare(args.source, target, release_eligibility=False)
    result = subprocess.run(
        [
            sys.executable,
            str(
                Path(__file__).resolve().parents[1]
                / "checks/check_geometry_corpus_profiles.py"
            ),
            str(target),
        ],
        check=False,
    )
    validate_vendored_profile_manifest(
        Path(receipt["manifest"]),
        args.source,
        target,
        receipt["sha256"],
        load_contract(DEFAULT_CONTRACT_PATH),
    )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
