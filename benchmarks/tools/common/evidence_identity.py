from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path


def sha256_file(path: Path) -> str:
    return f"sha256:{hashlib.sha256(path.read_bytes()).hexdigest()}"


def _aggregate(root: Path, paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        relative = path.relative_to(root).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return f"sha256:{digest.hexdigest()}"


def _candidate_source_paths(root: Path) -> list[Path]:
    paths: list[Path] = []
    for name in ("CMakeLists.txt", "conanfile.py", "vcpkg.json"):
        path = root / name
        if path.is_file():
            paths.append(path)
    for directory in (root / "include", root / "src", root / "benchmarks"):
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or "results" in path.parts or "__pycache__" in path.parts:
                continue
            if path.suffix in {".cpp", ".h", ".hpp", ".py", ".txt"}:
                paths.append(path)
    return sorted(paths)


def _git(
    root: Path,
    *arguments: str,
    check: bool = True,
) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=check,
        capture_output=True,
    )


def _is_candidate_relative_path(relative: str) -> bool:
    path = Path(relative)
    if relative in {"CMakeLists.txt", "conanfile.py", "vcpkg.json"}:
        return True
    if not path.parts or path.parts[0] not in {"include", "src", "benchmarks"}:
        return False
    return "results" not in path.parts and "__pycache__" not in path.parts and (
        path.suffix in {".cpp", ".h", ".hpp", ".py", ".txt"}
    )


def _head_blobs(root: Path) -> dict[str, str]:
    completed = _git(root, "ls-tree", "-r", "-z", "HEAD")
    result: dict[str, str] = {}
    for record in completed.stdout.split(b"\0"):
        if not record:
            continue
        metadata, relative = record.split(b"\t", 1)
        _, kind, blob = metadata.split(b" ", 2)
        if kind == b"blob":
            result[relative.decode("utf-8")] = blob.decode("ascii")
    return result


def _canonical_blobs(
    root: Path,
    relative_paths: set[str],
) -> dict[str, str | None]:
    existing = [
        relative
        for relative in sorted(relative_paths)
        if (root / relative).is_file()
    ]
    if any("\n" in relative or "\r" in relative for relative in existing):
        raise ValueError("source identity paths cannot contain line breaks")
    result = dict.fromkeys(sorted(relative_paths))
    if not existing:
        return result
    completed = subprocess.run(
        ["git", "-C", str(root), "hash-object", "--stdin-paths"],
        input=("\n".join(existing) + "\n").encode("utf-8"),
        check=True,
        capture_output=True,
    )
    blobs = completed.stdout.decode("ascii").splitlines()
    if len(blobs) != len(existing):
        raise RuntimeError("git hash-object returned an incomplete source identity")
    result.update(zip(existing, blobs, strict=True))
    return result


def _aggregate_blob_map(blobs: dict[str, str | None]) -> str:
    digest = hashlib.sha256()
    for relative, blob in sorted(blobs.items()):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update((blob or "deleted").encode("ascii"))
        digest.update(b"\0")
    return f"sha256:{digest.hexdigest()}"


def git_repository_identity(root: Path) -> dict | None:
    inside = _git(root, "rev-parse", "--is-inside-work-tree", check=False)
    if inside.returncode != 0 or inside.stdout.strip() != b"true":
        return None
    head_commit = _git(root, "rev-parse", "HEAD").stdout.decode("ascii").strip()
    head_tree = _git(root, "rev-parse", "HEAD^{tree}").stdout.decode("ascii").strip()
    head_blobs = _head_blobs(root)
    relative_paths = {
        path.relative_to(root).as_posix()
        for path in _candidate_source_paths(root)
    }
    relative_paths.update(
        relative
        for relative in head_blobs
        if _is_candidate_relative_path(relative)
    )
    current = _canonical_blobs(root, relative_paths)
    baseline = {
        relative: head_blobs.get(relative)
        for relative in sorted(relative_paths)
    }
    changed = {
        relative: f"{baseline[relative] or 'untracked'}->{current[relative] or 'deleted'}"
        for relative in sorted(relative_paths)
        if baseline[relative] != current[relative]
    }
    status = _git(
        root,
        "status",
        "--porcelain=v1",
        "-z",
        "--untracked-files=all",
    ).stdout
    return {
        "head_commit": head_commit,
        "head_tree": head_tree,
        "dirty": bool(changed),
        "worktree_status_dirty": bool(status),
        "canonical_source_identity": _aggregate_blob_map(current),
        "canonical_diff_identity": _aggregate_blob_map(changed),
    }


def candidate_source_identity(root: Path) -> str | None:
    repository = git_repository_identity(root)
    if repository is not None:
        return str(repository["canonical_source_identity"])
    paths = _candidate_source_paths(root)
    return _aggregate(root, paths) if paths else None


def corpus_identity() -> str | None:
    value = os.environ.get("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT", "").strip()
    if not value:
        return None
    root = Path(value).resolve()
    profile_root = root / "normalized" / "benchmark"
    paths = list(profile_root.glob("*.jsonl")) if profile_root.is_dir() else []
    return _aggregate(root, paths) if paths else None


def protocol_identity(root: Path) -> str | None:
    names = (
        "benchmarks/tools/runners/run_calibrated_external_performance_gate.py",
        "benchmarks/tools/common/evidence_identity.py",
        "benchmarks/tools/common/external_core_measurement.py",
        "benchmarks/tools/common/release_gate_policy.py",
        "benchmarks/tools/gates/external_benchmark_variance_gate.py",
        "benchmarks/tools/gates/external_legacy_speedup_gate.py",
        "benchmarks/oracle/external_corpus_benchmark.cpp",
    )
    paths = [root / name for name in names]
    return _aggregate(root, paths) if all(path.is_file() for path in paths) else None


def _cmake_cache(benchmark_executable: Path) -> Path | None:
    directory = benchmark_executable.parent
    for candidate in (directory, *directory.parents):
        cache = candidate / "CMakeCache.txt"
        if cache.is_file():
            return cache
        if candidate.parent == candidate:
            break
    return None


def compiler_identity(benchmark_executable: Path) -> dict | None:
    cache = _cmake_cache(benchmark_executable)
    if cache is None:
        return None
    values: dict[str, str] = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_type, value = line.split("=", 1)
        values[key_type.split(":", 1)[0]] = value
    compiler = values.get("CMAKE_CXX_COMPILER", "")
    version = ""
    if compiler:
        try:
            completed = subprocess.run(
                [compiler, "--version"],
                check=False,
                capture_output=True,
            )
            output = completed.stdout + completed.stderr
            version = output.decode("utf-8", errors="replace").strip()
        except OSError:
            version = ""
    return {
        "cmake_cache_identity": sha256_file(cache),
        "build_type": values.get("CMAKE_BUILD_TYPE", ""),
        "cxx_compiler": compiler,
        "cxx_compiler_version": version,
        "cxx_flags": values.get("CMAKE_CXX_FLAGS", ""),
        "cxx_flags_release": values.get("CMAKE_CXX_FLAGS_RELEASE", ""),
    }


def runtime_library_identity(benchmark_executable: Path) -> dict:
    if os.name == "nt":
        library = benchmark_executable.parent / "clipper2next.dll"
        if library.is_file():
            return {
                "linkage": "shared",
                "path": str(library.resolve()),
                "sha256": sha256_file(library),
            }
        return {"linkage": "standalone"}

    try:
        completed = subprocess.run(
            ["ldd", str(benchmark_executable)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return {"linkage": "standalone"}
    for line in completed.stdout.splitlines():
        if "libclipper2next.so" not in line or "=>" not in line:
            continue
        path_text = line.split("=>", 1)[1].strip().split(" ", 1)[0]
        library = Path(path_text)
        if library.is_file():
            return {
                "linkage": "shared",
                "path": str(library.resolve()),
                "sha256": sha256_file(library),
            }
    return {"linkage": "standalone"}


def collect_evidence_identity(
    root: Path,
    benchmark_executable: Path,
) -> dict:
    executable_identity = (
        sha256_file(benchmark_executable)
        if benchmark_executable.is_file()
        else None
    )
    source_identity = candidate_source_identity(root)
    build_identity = compiler_identity(benchmark_executable)
    data_identity = corpus_identity()
    protocol = protocol_identity(root)
    runtime = runtime_library_identity(benchmark_executable)
    repository = git_repository_identity(root)
    payload = {
        "benchmark_executable_identity": executable_identity,
        "candidate_source_identity": source_identity,
        "compiler_identity": build_identity,
        "corpus_identity": data_identity,
        "protocol_identity": protocol,
        "runtime_library_identity": runtime,
        "git_repository_identity": repository,
    }
    complete = all(
        payload[key] is not None
        for key in (
            "benchmark_executable_identity",
            "candidate_source_identity",
            "compiler_identity",
            "corpus_identity",
            "protocol_identity",
            "git_repository_identity",
        )
    )
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return {
        **payload,
        "identity_complete": complete,
        "evidence_identity":
            f"sha256:{hashlib.sha256(encoded.encode('utf-8')).hexdigest()}",
    }
