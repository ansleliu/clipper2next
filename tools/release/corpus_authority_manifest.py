"""Validate a frozen derivation against explicitly selected corpus files."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from tools.checks.check_geometry_corpus_profiles import (
    canonical_json_digest,
    file_sha256,
    load_profile,
    validate_profile_semantics,
)
from tools.release.evidence_contract import ReleaseEvidenceContract


_GENERATOR = {"name": "generate_release_profiles", "version": 2}
_MAPPING_FIELDS = (
    "id",
    "case_set",
    "profile_id",
    "operation",
    "source",
    "comparison_domain",
    "contract_id",
    "canonical_input_digest",
    "canonical_options_digest",
    "authority_digest",
)


def _file_entries(
    entries: Any, root: Path, fields: set[str]
) -> dict[str, dict[str, Any]]:
    if not isinstance(entries, list) or not entries:
        raise ValueError("corpus manifest requires nonempty file bindings")
    indexed: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != fields:
            raise ValueError(
                "corpus manifest file binding fields are incomplete/unknown"
            )
        relative = entry.get("path")
        if not isinstance(relative, str) or relative in indexed:
            raise ValueError("corpus manifest has invalid/duplicate file paths")
        path = (root / relative).resolve()
        if (
            not path.is_relative_to(root)
            or not relative.startswith("normalized/")
            or path.suffix != ".jsonl"
        ):
            raise ValueError("corpus manifest path leaves selected normalized corpus")
        if not path.is_file() or entry.get("sha256") != file_sha256(path):
            raise ValueError(f"corpus manifest actual file hash mismatch: {relative}")
        indexed[relative] = entry
    actual = {
        path.relative_to(root).as_posix()
        for path in (root / "normalized").rglob("*.jsonl")
    }
    if set(indexed) != actual:
        raise ValueError("corpus manifest file bindings omit/add selected files")
    return indexed


def _validate_frozen_corpus_manifest(
    path: Path,
    source_root: Path,
    derived_root: Path,
    frozen_sha256: str,
    contract: ReleaseEvidenceContract,
    *,
    require_release_eligibility: bool = True,
) -> tuple[str, list[dict[str, Any]]]:
    """The caller supplies a digest retained before execution, outside evidence.

    Reading a digest from the submitted manifest or qualification would not
    establish that commitment. T11 owns the actual pre-execution producer.
    """
    source_root, derived_root = source_root.resolve(), derived_root.resolve()
    if not re.fullmatch(r"[0-9a-f]{64}", frozen_sha256 or ""):
        raise ValueError("corpus manifest requires externally retained frozen SHA256")
    observed_sha256 = file_sha256(path)
    if observed_sha256 != frozen_sha256:
        raise ValueError(
            "corpus manifest differs from externally retained pre-execution freeze"
        )
    manifest = json.loads(path.read_text(encoding="utf-8"))
    fields = {
        "schema_version",
        "generator",
        "source_root",
        "selection_phase",
        "source_files",
        "derived_files",
        "case_mappings",
    }
    if not isinstance(manifest, dict) or set(manifest) != fields:
        raise ValueError("corpus manifest schema fields are incomplete/unknown")
    if (
        manifest["schema_version"] != 2
        or manifest["generator"] != _GENERATOR
        or manifest["selection_phase"] != "before-execution"
    ):
        raise ValueError(
            "corpus manifest requires current generator/schema and pre-execution selection"
        )
    if (
        not isinstance(manifest["source_root"], str)
        or Path(manifest["source_root"]).resolve() != source_root
    ):
        raise ValueError(
            "corpus manifest source_root differs from explicitly selected source corpus"
        )
    if (
        source_root == derived_root
        or source_root in derived_root.parents
        or derived_root in source_root.parents
    ):
        raise ValueError("source and derived corpus roots must be disjoint")
    source_files = _file_entries(
        manifest["source_files"],
        source_root,
        {"path", "sha256", "record_count", "purpose"},
    )
    derived_files = _file_entries(
        manifest["derived_files"], derived_root, {"path", "sha256"}
    )
    required = {
        f"normalized/{case_set}/{profile.id}.jsonl"
        for profile in contract.profiles.values()
        for case_set in profile.required_case_sets
    }
    if require_release_eligibility:
        required.update(
            f"normalized/full/{profile.eligibility_file}"
            for profile in contract.profiles.values()
        )
    if not required <= set(source_files) or set(derived_files) != set(source_files):
        raise ValueError(
            "corpus manifest omits required profiles/eligibility files or changes derived file set"
        )
    expected_mappings: list[dict[str, Any]] = []
    for relative, entry in sorted(source_files.items()):
        case_set = Path(relative).parts[1]
        purpose = "eligibility-only" if case_set == "full" else case_set
        if (
            case_set not in {"full", "verification", "benchmark"}
            or entry["purpose"] != purpose
        ):
            raise ValueError(f"corpus manifest source purpose mismatch: {relative}")
        original, errors = load_profile(source_root / relative, case_set)
        if (
            errors
            or type(entry["record_count"]) is not int
            or entry["record_count"] != len(original)
        ):
            raise ValueError(
                f"corpus manifest source record count/load mismatch: {relative}"
            )
        derived_path = derived_root / relative
        if case_set == "full":
            if derived_files[relative]["sha256"] != entry["sha256"]:
                raise ValueError(
                    f"corpus manifest full eligibility bytes changed: {relative}"
                )
            continue
        profile_id = Path(relative).stem
        if profile_id not in contract.profiles:
            raise ValueError(f"corpus manifest unknown profile: {profile_id}")
        records, errors = load_profile(derived_path, case_set)
        errors.extend(
            validate_profile_semantics(records, contract.profiles[profile_id], case_set)
        )
        if errors or len(records) != len(original):
            raise ValueError(
                f"corpus manifest derived profile/count invalid: {relative}: {'; '.join(errors[:3])}"
            )
        for ordinal, (before, record) in enumerate(zip(original, records), 1):
            if any(
                record.get(key) != before.get(key)
                for key in ("id", "profile", "operation", "inputs", "tolerance")
            ):
                raise ValueError(
                    f"corpus manifest derived source case changed: {relative}:{ordinal}"
                )
            source = {"path": relative, "sha256": entry["sha256"], "record": ordinal}
            if record.get("authority_source") != source:
                raise ValueError(
                    f"corpus manifest case-to-source binding mismatch: {relative}:{ordinal}"
                )
            expected_mappings.append(
                {
                    "id": record["id"],
                    "case_set": case_set,
                    "profile_id": profile_id,
                    "operation": record["operation"],
                    "source": source,
                    **{key: record[key] for key in _MAPPING_FIELDS[5:]},
                }
            )
    mappings = manifest["case_mappings"]
    if not isinstance(mappings, list) or not all(
        isinstance(case, dict) and set(case) == set(_MAPPING_FIELDS)
        for case in mappings
    ):
        raise ValueError(
            "corpus manifest requires complete case/operation/source mappings"
        )
    if sorted(map(canonical_json_digest, mappings)) != sorted(
        map(canonical_json_digest, expected_mappings)
    ):
        raise ValueError(
            "corpus manifest case/source/profile-operation mappings differ from selected files"
        )
    return observed_sha256, [
        case for case in expected_mappings if case["case_set"] == "verification"
    ]


def validate_frozen_corpus_manifest(
    path: Path,
    source_root: Path,
    derived_root: Path,
    frozen_sha256: str,
    contract: ReleaseEvidenceContract,
):
    """Full external release admission always requires all eligibility inputs."""
    return _validate_frozen_corpus_manifest(
        path, source_root, derived_root, frozen_sha256, contract
    )


def validate_vendored_profile_manifest(
    path: Path,
    source_root: Path,
    derived_root: Path,
    frozen_sha256: str,
    contract: ReleaseEvidenceContract,
):
    """Vendored authority examples confer no full-corpus release eligibility."""
    return _validate_frozen_corpus_manifest(
        path,
        source_root,
        derived_root,
        frozen_sha256,
        contract,
        require_release_eligibility=False,
    )


def offset_contract_source_manifest(
    source_path: Path, cases_path: Path
) -> dict[str, Any]:
    """Bind the separate repository analytic source; it never fills external quotas."""
    from tools.release.correction_authority import (
        canonical_digest,
        canonical_options,
        validate_authority,
    )

    source_path, cases_path = source_path.resolve(), cases_path.resolve()
    if source_path == cases_path:
        raise ValueError("formal Offset source and derived cases must be separate")
    cases = [
        json.loads(line)
        for line in source_path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if not cases or len({case.get("id") for case in cases}) != len(cases):
        raise ValueError("formal Offset source has empty or duplicate cases")
    mappings = []
    for ordinal, case in enumerate(cases, 1):
        errors = validate_authority(case)
        if case.get("comparison_domain") != "offset-correction-contract":
            errors.append("formal Offset source has wrong domain")
        if case.get("tolerance") != {"coordinate": 0, "area": 0}:
            errors.append("formal Offset source tolerance must be zero")
        if case.get("canonical_input_digest") != canonical_digest(case.get("inputs")):
            errors.append("formal Offset input digest mismatch")
        if case.get("canonical_options_digest") != canonical_digest(
            canonical_options(case.get("inputs", {}))
        ):
            errors.append("formal Offset options digest mismatch")
        if errors:
            raise ValueError("; ".join(errors))
        mappings.append(
            {
                "id": case["id"],
                "source_record": ordinal,
                "profile_id": "offset",
                "comparison_domain": case["comparison_domain"],
                "operation": case["operation"],
                "contract_id": case["contract_id"],
                "canonical_input_digest": case["canonical_input_digest"],
                "canonical_options_digest": case["canonical_options_digest"],
                "authority_digest": case["authority_digest"],
            }
        )
    return {
        "schema_version": 1,
        "kind": "repository-offset-analytic-source",
        "selection_phase": "before-execution",
        "external_profile_credit": False,
        "source": {
            "path": source_path.as_posix(),
            "sha256": file_sha256(source_path),
            "record_count": len(cases),
        },
        "derived": {"path": cases_path.as_posix(), "sha256": file_sha256(source_path)},
        "case_mappings": mappings,
    }


def validate_offset_contract_source_manifest(
    path: Path, source_path: Path, cases_path: Path, frozen_sha256: str
) -> list[dict[str, Any]]:
    """T11 must require this AND the external manifest, with separately frozen hashes."""
    if (
        not re.fullmatch(r"[0-9a-f]{64}", frozen_sha256 or "")
        or file_sha256(path) != frozen_sha256
    ):
        raise ValueError(
            "formal Offset manifest differs from external pre-execution freeze"
        )
    expected = offset_contract_source_manifest(source_path, cases_path)
    actual = json.loads(path.read_text(encoding="utf-8"))
    if actual != expected:
        raise ValueError("formal Offset manifest source/hash/ordinal mapping mismatch")
    if source_path.read_bytes() != cases_path.read_bytes():
        raise ValueError("formal Offset derived source bytes differ")
    return actual["case_mappings"]
