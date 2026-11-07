#!/usr/bin/env python3
"""Collect machine-readable evidence from executed release gates."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

_GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}(?:[0-9a-f]{24})?$")


def _read_bytes(path: Path, artifact: str) -> bytes:
    artifact_path = Path(path)
    try:
        return artifact_path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read {artifact} {artifact_path}: {error}") from error


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
        "--untracked-files=no",
    )
    return {"commit": commit, "dirty": bool(porcelain)}


def build_runtime_evidence(
    *, producer_manifest: Path, repository_root: Path
) -> dict[str, object]:
    """Validate only evidence sealed by the same-run producer; never re-sign logs."""
    from tools.release.correction_run import validate_producer
    from tools.release.source_inventory import file_record

    return {
        **validate_producer(producer_manifest, repository_root),
        "producer_manifest": file_record(producer_manifest),
    }


def runtime_projection(evidence: dict) -> dict:
    """Coverage rules consume this view only after producer validation."""
    records = {
        name: evidence[name]
        for name in ("contract", "profile_report", "ctest_junit", "benchmark_list")
    }
    return {
        "schema_version": 1,
        "contract_sha256": records["contract"]["sha256"],
        "profile_report_sha256": records["profile_report"]["sha256"],
        "git": {
            "commit": evidence["source"]["commit"],
            "dirty": evidence["source"]["tracked_dirty"],
        },
        "tests": evidence["tests"],
        "benchmarks": sorted(
            collect_benchmark_names(Path(records["benchmark_list"]["path"]))
        ),
        "inputs": {name: record["path"] for name, record in records.items()},
        "input_sha256": {name: record["sha256"] for name, record in records.items()},
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
    parser.add_argument("--producer-manifest", type=Path, required=True)
    parser.add_argument("--repository-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        evidence = build_runtime_evidence(
            producer_manifest=args.producer_manifest,
            repository_root=args.repository_root,
        )
        _write_json(args.output, evidence)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print(f"runtime_evidence={args.output.resolve()}")
    print(f"tests={len(evidence['tests'])}")
    print(f"scope={evidence['scope']}")
    print(f"run_id={evidence['run_id']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
