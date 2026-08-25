#!/usr/bin/env python3
"""Collect machine-readable evidence from executed release gates."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.release.evidence_contract import (  # noqa: E402
    DEFAULT_CONTRACT_PATH,
    load_contract,
)


_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}(?:[0-9a-f]{24})?$")
_PROFILE_REPORT_STATUSES = {"PASS", "FAIL"}


def _read_bytes(path: Path, artifact: str) -> bytes:
    artifact_path = Path(path)
    try:
        return artifact_path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read {artifact} {artifact_path}: {error}") from error


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _parse_ctest_results(data: bytes, source: Path) -> dict[str, str]:
    try:
        root = ET.fromstring(data)
    except ET.ParseError as error:
        raise ValueError(f"invalid CTest JUnit XML in {source}: {error}") from error

    results: dict[str, str] = {}
    for testcase in root.iter():
        if _local_name(testcase.tag) != "testcase":
            continue

        name = testcase.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError(f"CTest JUnit testcase in {source} has no name")
        if name in results:
            raise ValueError(f"duplicate CTest testcase name {name!r} in {source}")

        outcome_elements = {_local_name(child.tag) for child in testcase}
        if outcome_elements & {"failure", "error"}:
            status = "FAIL"
        elif "skipped" in outcome_elements:
            status = "SKIP"
        else:
            status = "PASS"
        results[name] = status

    if not results:
        raise ValueError(f"CTest JUnit XML {source} contains no testcases")
    return dict(sorted(results.items()))


def collect_ctest_results(path: Path) -> dict[str, str]:
    """Return exact CTest testcase names mapped to PASS, FAIL, or SKIP."""

    junit_path = Path(path)
    return _parse_ctest_results(
        _read_bytes(junit_path, "CTest JUnit XML"),
        junit_path,
    )


def _decode_benchmark_list(data: bytes, source: Path) -> str:
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        encoding = "utf-16"
    elif data.startswith(b"\xef\xbb\xbf"):
        encoding = "utf-8-sig"
    else:
        encoding = "utf-8"
    try:
        return data.decode(encoding)
    except UnicodeDecodeError as error:
        raise ValueError(
            f"benchmark registration list {source} is not UTF-8 or BOM-marked UTF-16"
        ) from error


def _parse_benchmark_names(data: bytes, source: Path) -> set[str]:
    text = _decode_benchmark_list(data, source)
    names: set[str] = set()
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line.startswith("BM_"):
            continue
        if line in names:
            raise ValueError(
                f"duplicate registered benchmark name {line!r} in {source}"
            )
        names.add(line)
    if not names:
        raise ValueError(
            f"benchmark registration list {source} contains no registered benchmarks"
        )
    return names


def collect_benchmark_names(path: Path) -> set[str]:
    """Return registered Google Benchmark names from list-tests output."""

    benchmark_path = Path(path)
    return _parse_benchmark_names(
        _read_bytes(benchmark_path, "benchmark registration list"),
        benchmark_path,
    )


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"profile report contains duplicate JSON key {key!r}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"profile report contains non-finite JSON number {value}")


def _validate_profile_report(data: bytes, source: Path) -> dict[str, Any]:
    try:
        payload = json.loads(
            data.decode("utf-8-sig"),
            object_pairs_hook=_unique_json_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid profile report JSON in {source}: {error}") from error
    if not isinstance(payload, dict):
        raise ValueError(f"profile report {source} must be a JSON object")
    schema_version = payload.get("schema_version")
    if isinstance(schema_version, bool) or schema_version != 1:
        raise ValueError(f"profile report {source} must use schema_version 1")
    for field in ("contract_sha256", "profile_sha256"):
        value = payload.get(field)
        if not isinstance(value, str) or not _SHA256_RE.fullmatch(value):
            raise ValueError(f"profile report {source} has missing or invalid {field}")
    status = payload.get("status")
    if status not in _PROFILE_REPORT_STATUSES:
        raise ValueError(f"profile report {source} has invalid status {status!r}")
    return payload


def _run_git(repository_root: Path, *arguments: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(repository_root), *arguments],
            text=True,
            encoding="utf-8",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as error:
        raise ValueError(f"cannot execute git: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "unknown error"
        raise ValueError(
            f"git {' '.join(arguments)} failed in {repository_root}: {detail}"
        )
    return result.stdout


def collect_git_state(repository_root: Path) -> dict[str, str | bool]:
    """Return the exact candidate commit and whether its work tree is dirty."""

    root = Path(repository_root).resolve()
    if not root.is_dir():
        raise ValueError(f"repository root is not a directory: {root}")
    commit = _run_git(root, "rev-parse", "--verify", "HEAD").strip()
    if not _GIT_COMMIT_RE.fullmatch(commit):
        raise ValueError(f"git returned an invalid commit ID {commit!r}")
    porcelain = _run_git(
        root,
        "status",
        "--porcelain=v1",
        "--untracked-files=normal",
    )
    return {"commit": commit, "dirty": bool(porcelain)}


def build_runtime_evidence(
    *,
    contract_path: Path,
    profile_report_path: Path,
    ctest_junit_path: Path,
    benchmark_list_path: Path,
    repository_root: Path,
) -> dict[str, object]:
    """Build evidence bound to the exact artifacts that were parsed."""

    contract_file = Path(contract_path)
    profile_report_file = Path(profile_report_path)
    junit_file = Path(ctest_junit_path)
    benchmark_file = Path(benchmark_list_path)

    contract = load_contract(contract_file)
    profile_report_bytes = _read_bytes(profile_report_file, "profile report")
    _validate_profile_report(profile_report_bytes, profile_report_file)
    junit_bytes = _read_bytes(junit_file, "CTest JUnit XML")
    tests = _parse_ctest_results(junit_bytes, junit_file)
    benchmark_bytes = _read_bytes(
        benchmark_file,
        "benchmark registration list",
    )
    benchmarks = _parse_benchmark_names(benchmark_bytes, benchmark_file)
    git = collect_git_state(repository_root)

    input_paths = {
        "contract": str(contract_file.resolve()),
        "profile_report": str(profile_report_file.resolve()),
        "ctest_junit": str(junit_file.resolve()),
        "benchmark_list": str(benchmark_file.resolve()),
    }
    input_sha256 = {
        "contract": contract.sha256,
        "profile_report": _sha256(profile_report_bytes),
        "ctest_junit": _sha256(junit_bytes),
        "benchmark_list": _sha256(benchmark_bytes),
    }
    return {
        "schema_version": 1,
        "contract_sha256": contract.sha256,
        "profile_report_sha256": input_sha256["profile_report"],
        "git": git,
        "tests": tests,
        "benchmarks": sorted(benchmarks),
        "inputs": input_paths,
        "input_sha256": input_sha256,
    }


def _write_json(path: Path, payload: dict[str, object]) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            prefix=f".{output.name}.",
            suffix=".tmp",
            dir=output.parent,
            delete=False,
        ) as temporary:
            json.dump(
                payload,
                temporary,
                indent=2,
                sort_keys=True,
                allow_nan=False,
            )
            temporary.write("\n")
            temporary_path = Path(temporary.name)
        temporary_path.replace(output)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Collect machine-readable release runtime evidence."
    )
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT_PATH)
    parser.add_argument("--ctest-junit", type=Path, required=True)
    parser.add_argument("--benchmark-list", type=Path, required=True)
    parser.add_argument("--profile-report", type=Path, required=True)
    parser.add_argument("--repository-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        evidence = build_runtime_evidence(
            contract_path=args.contract,
            profile_report_path=args.profile_report,
            ctest_junit_path=args.ctest_junit,
            benchmark_list_path=args.benchmark_list,
            repository_root=args.repository_root,
        )
        _write_json(args.output, evidence)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print(f"runtime_evidence={args.output.resolve()}")
    print(f"tests={len(evidence['tests'])}")
    print(f"benchmarks={len(evidence['benchmarks'])}")
    print(f"git_dirty={str(evidence['git']['dirty']).lower()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
