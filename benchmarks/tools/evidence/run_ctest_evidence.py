#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from benchmarks.tools.common.evidence_identity import (  # noqa: E402
    candidate_source_identity,
    git_repository_identity,
    release_identity,
    sha256_file,
)


_SUMMARY = re.compile(
    r"100% tests passed,\s*0 tests failed out of\s+(\d+)", re.IGNORECASE
)
_FAILURES = (
    "ERROR: AddressSanitizer",
    "ERROR: LeakSanitizer",
    "WARNING: ThreadSanitizer",
    "ThreadSanitizer: reported",
    "runtime error:",
)
_PROTOCOL_FILES = (
    "benchmarks/tools/evidence/run_ctest_evidence.py",
    "benchmarks/tools/evidence/archive_release_evidence.py",
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _write_json(path: Path, payload: dict) -> None:
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def _protocol_identity() -> str:
    digest = hashlib.sha256()
    for name in _PROTOCOL_FILES:
        content = (repo_root() / name).read_bytes()
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(content).digest())
    return f"sha256:{digest.hexdigest()}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run CTest and bind its zero-failure result to release artifacts."
    )
    parser.add_argument("--name", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected-tests", type=int, required=True)
    parser.add_argument("--compiler-identity", required=True)
    parser.add_argument(
        "--build-configuration",
        choices=("Release", "ASan-UBSan", "TSan", "Fuzz-UBSan"),
        required=True,
    )
    parser.add_argument("--cmake-cache", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, action="append", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.expected_tests <= 0:
        parser.error("--expected-tests must be positive")
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not command:
        parser.error("a CTest command is required after --")
    cmake_cache = args.cmake_cache.resolve()
    if not cmake_cache.is_file():
        parser.error("--cmake-cache must name the tested build cache")

    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    artifact_root = output / "artifacts" / args.name
    artifact_root.mkdir(parents=True, exist_ok=True)
    log_path = output / f"{args.name}.log"
    completed = subprocess.run(
        command,
        cwd=repo_root(),
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    log = completed.stdout + completed.stderr
    for source, replacement in sorted(
        (
            (str(repo_root().resolve()), "${REPOSITORY}"),
            (str(output), "${EVIDENCE}"),
        ),
        key=lambda value: len(value[0]),
        reverse=True,
    ):
        log = log.replace(source, replacement)
        log = log.replace(source.replace("\\", "/"), replacement)
    log_path.write_text(log, encoding="utf-8", newline="\n")

    match = _SUMMARY.search(log)
    observed = int(match.group(1)) if match else 0
    sanitizer_failures = sum(
        log.lower().count(pattern.lower()) for pattern in _FAILURES
    )
    repository = git_repository_identity(repo_root())
    if repository is None:
        raise RuntimeError("release test evidence requires a Git repository")
    archived_artifacts: list[dict[str, str]] = []
    for source in args.artifact:
        resolved = source.resolve()
        if not resolved.is_file():
            raise FileNotFoundError(resolved)
        destination = artifact_root / resolved.name
        shutil.copy2(resolved, destination)
        archived_artifacts.append(
            {
                "artifact_id": destination.relative_to(output).as_posix(),
                "sha256": sha256_file(destination),
            }
        )
    status = (
        "PASS"
        if completed.returncode == 0
        and observed == args.expected_tests
        and sanitizer_failures == 0
        and repository["dirty"] is False
        and repository["worktree_status_dirty"] is False
        else "FAIL"
    )
    manifest = {
        "schema_version": 1,
        "name": args.name,
        "status": status,
        "exit_code": completed.returncode,
        "tests_total": observed,
        "tests_failed": 0 if match else None,
        "sanitizer_failures": sanitizer_failures,
        "log_artifact": log_path.relative_to(output).as_posix(),
        "log_sha256": sha256_file(log_path),
        "candidate_source_identity": candidate_source_identity(repo_root()),
        "compiler_identity": args.compiler_identity,
        "build_configuration": args.build_configuration,
        "cmake_cache_identity": sha256_file(cmake_cache),
        "git_repository_identity": repository,
        "release_identity": release_identity(repository),
        "protocol_identity": _protocol_identity(),
        "artifacts": archived_artifacts,
    }
    manifest_path = output / f"{args.name}.json"
    _write_json(manifest_path, manifest)
    print(f"status={status}")
    print(f"manifest={manifest_path.name}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
