#!/usr/bin/env python3
"""Repair colliding normalized-full IDs without dropping valid records."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class NormalizedIdRepairError(ValueError):
    """Raised when colliding records cannot be assigned unambiguous identities."""


@dataclass(frozen=True)
class IdRename:
    old_id: str
    new_id: str
    record_number: int
    upstream_id: str


@dataclass(frozen=True)
class RepairResult:
    records: list[dict[str, Any]]
    renames: tuple[IdRename, ...]


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number {value}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def _source_identity(record: dict[str, Any]) -> tuple[str, str]:
    source = record.get("source")
    if not isinstance(source, dict):
        return "unknown", "unknown"
    source_id = source.get("source_id")
    upstream_id = source.get("upstream_id")
    return (
        source_id if isinstance(source_id, str) and source_id else "unknown",
        upstream_id
        if isinstance(upstream_id, str) and upstream_id
        else "unknown",
    )


def _disambiguated_id(old_id: str, record: dict[str, Any]) -> str:
    source_id, upstream_id = _source_identity(record)
    payload_without_id = {key: value for key, value in record.items() if key != "id"}
    identity = {
        "source_id": source_id,
        "upstream_id": upstream_id,
        "payload": payload_without_id,
    }
    suffix = hashlib.sha256(_canonical_bytes(identity)).hexdigest()[:16]
    return f"{old_id}--source-{suffix}"


def repair_colliding_ids(records: list[dict[str, Any]]) -> RepairResult:
    """Rename every member of an ID collision using stable source identity."""

    repaired = copy.deepcopy(records)
    indices_by_id: dict[str, list[int]] = defaultdict(list)
    for index, record in enumerate(repaired):
        record_id = record.get("id")
        if not isinstance(record_id, str) or not record_id:
            raise NormalizedIdRepairError(
                f"record {index + 1} has no non-empty string id"
            )
        indices_by_id[record_id].append(index)

    existing_ids = set(indices_by_id)
    renames: list[IdRename] = []
    for old_id, indices in sorted(indices_by_id.items()):
        if len(indices) == 1:
            continue
        generated_ids: set[str] = set()
        for index in indices:
            record = repaired[index]
            new_id = _disambiguated_id(old_id, record)
            if new_id in generated_ids or (
                new_id in existing_ids and new_id != old_id
            ):
                raise NormalizedIdRepairError(
                    f"colliding id {old_id!r} records remain ambiguous after "
                    "source-identity suffixing"
                )
            generated_ids.add(new_id)
            _, upstream_id = _source_identity(record)
            record["id"] = new_id
            renames.append(
                IdRename(
                    old_id=old_id,
                    new_id=new_id,
                    record_number=index + 1,
                    upstream_id=upstream_id,
                )
            )
        existing_ids.update(generated_ids)

    final_ids = [record["id"] for record in repaired]
    if len(final_ids) != len(set(final_ids)):
        raise NormalizedIdRepairError("normalized IDs remain ambiguous after repair")
    return RepairResult(records=repaired, renames=tuple(renames))


def load_records(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise NormalizedIdRepairError(f"missing normalized JSONL file: {path}")
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(
                    line,
                    object_pairs_hook=_unique_object,
                    parse_constant=_reject_json_constant,
                )
            except (json.JSONDecodeError, ValueError) as error:
                raise NormalizedIdRepairError(
                    f"{path}:{line_number}: invalid JSON: {error}"
                ) from error
            if not isinstance(record, dict):
                raise NormalizedIdRepairError(
                    f"{path}:{line_number}: record must be an object"
                )
            records.append(record)
    if not records:
        raise NormalizedIdRepairError(f"normalized JSONL contains no records: {path}")
    return records


def _write_atomically(path: Path, records: list[dict[str, Any]]) -> None:
    handle = tempfile.NamedTemporaryFile(
        mode="wb",
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
        delete=False,
    )
    temporary = Path(handle.name)
    try:
        with handle:
            for record in records:
                handle.write(_canonical_bytes(record))
                handle.write(b"\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def repair_file(path: Path) -> RepairResult:
    result = repair_colliding_ids(load_records(path))
    if result.renames:
        _write_atomically(path, result.records)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Detect normalized-full ID collisions and, with --write, rename "
            "every colliding record using a stable source-identity suffix."
        )
    )
    parser.add_argument("jsonl_path", type=Path)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    try:
        if args.write:
            result = repair_file(args.jsonl_path)
        else:
            result = repair_colliding_ids(load_records(args.jsonl_path))
    except (NormalizedIdRepairError, OSError) as error:
        print("status=FAIL")
        print(error, file=sys.stderr)
        return 1

    if result.renames and not args.write:
        print("status=CHANGES_REQUIRED")
    else:
        print("status=PASS")
    print(f"records={len(result.records)}")
    print(f"renames={len(result.renames)}")
    for rename in result.renames:
        print(
            f"record={rename.record_number} old_id={rename.old_id} "
            f"new_id={rename.new_id} upstream_id={rename.upstream_id}"
        )
    return 1 if result.renames and not args.write else 0


if __name__ == "__main__":
    raise SystemExit(main())
