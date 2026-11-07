"""Shared actual-result capture boundary for mathematical preflights."""

from __future__ import annotations
import hashlib
import os
import uuid
import time
from pathlib import Path
import subprocess
from tools.release.mathematical_validation import (
    require_normal_math_mode,
    interpreter_identity,
)

require_normal_math_mode()


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(
    producer: Path, arguments: list[str], records: Path, binary_digest: str
) -> dict:
    if sha(producer) != binary_digest:
        raise ValueError("producer differs from pre-execution freeze")
    environment = dict(os.environ)
    run_id = environment.setdefault("CLIPPER2NEXT_CORRECTION_RUN_ID", str(uuid.uuid4()))
    environment["CLIPPER2NEXT_CAPTURE_RUNTIME"] = "1"
    expected = environment.get("CLIPPER2NEXT_EXPECTED_PRODUCT")
    expected_hash = sha(Path(expected)) if expected else None
    started_ns = time.time_ns()
    command = [str(producer), *arguments]
    records.parent.mkdir(parents=True, exist_ok=True)
    with records.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            command, stdout=log, stderr=subprocess.STDOUT, text=True, env=environment
        )
    if result.returncode:
        raise ValueError(
            f"actual record producer failed ({result.returncode}); see {records}"
        )
    if sha(producer) != binary_digest:
        raise ValueError("producer changed during execution")
    values = {}
    for line in records.read_text(encoding="utf-8").splitlines():
        if line.startswith("correction_") and "=" in line:
            key, value = line.split("=", 1)
            if key in values:
                raise ValueError("duplicate actual runtime identity")
            values[key] = value
    if values.get("correction_run_id") != run_id:
        raise ValueError("actual producer has missing/old run identity")
    if Path(values.get("correction_executable", "")).resolve() != producer.resolve():
        raise ValueError("actual producer executable differs")
    loaded = Path(values.get("correction_module_clipper", ""))
    if not loaded.is_absolute() or not loaded.is_file():
        raise ValueError("actual loaded product module identity missing")
    if loaded.stat().st_mtime_ns > started_ns:
        raise ValueError("actual loaded product changed during execution")
    library_hashes = {str(loaded.resolve()): sha(loaded)}
    if expected_hash and (
        sha(Path(expected)) != expected_hash or sha(loaded) != expected_hash
    ):
        raise ValueError("actual loaded product differs from pre-execution freeze")
    return {
        "run_id": run_id,
        "path": str(producer),
        "sha256": binary_digest,
        "command": command,
        "product_libraries": library_hashes,
        "stdout_sha256": sha(records),
        "interpreter": interpreter_identity(),
    }
