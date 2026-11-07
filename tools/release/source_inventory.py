"""The Conan recipe owns the exported source set; evidence binds its real bytes."""

from __future__ import annotations

import ast
import hashlib
import json
import subprocess
import tarfile
import re
from pathlib import Path


def digest(value) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def file_record(path: Path) -> dict:
    path = path.resolve(strict=True)
    return {
        "path": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "size": path.stat().st_size,
    }


def tree_record(root: Path) -> dict:
    root = root.resolve(strict=True)
    files = {
        p.relative_to(root).as_posix(): file_record(p)["sha256"]
        for p in sorted(root.rglob("*"))
        if p.is_file()
    }
    if not files:
        raise ValueError(f"empty artifact tree: {root}")
    return {"path": str(root), "files": files, "sha256": digest(files)}


def matching_public_headers(prefix: Path, source: Path) -> dict:
    from tools.checks.check_install_public_headers import (
        INSTALLED_HEADERS,
        find_install_public_header_findings,
    )

    errors = find_install_public_header_findings(prefix)
    if errors:
        raise ValueError("; ".join(errors))
    for relative in INSTALLED_HEADERS:
        actual = (prefix / "include" / relative).read_bytes().replace(b"\r\n", b"\n")
        expected = (source / "include" / relative).read_bytes().replace(b"\r\n", b"\n")
        if actual != expected:
            raise ValueError("installed headers do not match this source: " + relative)
    return tree_record(prefix / "include")


def exported_inventory(root: Path) -> dict:
    root = root.resolve()
    recipe = root / "conanfile.py"
    declarations = [
        node
        for node in ast.walk(ast.parse(recipe.read_text(encoding="utf-8")))
        if isinstance(node, ast.Assign)
        and any(
            isinstance(t, ast.Name) and t.id == "exports_sources" for t in node.targets
        )
    ]
    if len(declarations) != 1:
        raise ValueError("recipe must own exactly one exported input declaration")
    patterns = ast.literal_eval(declarations[0].value)
    paths = {recipe}
    for pattern in patterns:
        paths.update(p for p in root.glob(pattern) if p.is_file())
        for directory in (p for p in root.glob(pattern) if p.is_dir()):
            paths.update(p for p in directory.rglob("*") if p.is_file())
    source_files = {
        p.relative_to(root).as_posix(): file_record(p)["sha256"]
        for p in sorted(paths)
        if p != recipe
    }
    # These are precisely the staged, self-contained public test package sources.
    test_package = root / "packaging/test_package"
    paths.update(
        p
        for p in test_package.glob("*")
        if p.is_file() and p.suffix in {".py", ".cpp", ".h", ".txt"}
    )
    files = {
        p.relative_to(root).as_posix(): file_record(p)["sha256"] for p in sorted(paths)
    }
    return {
        "schema_version": 1,
        "root": str(root),
        "files": files,
        "source_files": source_files,
        "sha256": digest(files),
    }


def validate_inventory(root: Path, inventory: dict) -> list[Path]:
    root = root.resolve()
    if inventory != exported_inventory(root):
        raise ValueError("exported input inventory changed or source identity differs")
    return [root / relative for relative in inventory["files"]]


def public_archive_inventory(root: Path) -> dict:
    """Tracked files present in this checkout plus the recipe's current export set."""
    inventory = exported_inventory(root)
    names = (
        subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"])
        .decode()
        .split("\0")
    )
    inventory["archive_files"] = {
        p: file_record(root / p)["sha256"] for p in names if p and (root / p).is_file()
    }
    inventory["archive_sha256"] = digest(inventory["archive_files"])
    return inventory


def archive_record(archive: Path, source: Path) -> dict:
    """Verify actual producer output against the complete current published source selection."""
    expected = public_archive_inventory(source)["archive_files"]
    files = {}
    with tarfile.open(archive) as stream:
        for member in stream.getmembers():
            if member.isdir():
                continue
            if (
                not member.isfile()
                or member.name not in expected
                or member.name in files
            ):
                raise ValueError(
                    "archive contains unexpected/duplicate/non-file input: "
                    + member.name
                )
            data = stream.extractfile(member).read()
            current = (source / member.name).read_bytes()
            if data.replace(b"\r\n", b"\n") != current.replace(b"\r\n", b"\n"):
                raise ValueError("archive source bytes differ: " + member.name)
            files[member.name] = hashlib.sha256(data).hexdigest()
    if set(files) != set(expected):
        raise ValueError("archive input set differs from declared published source")
    return {"artifact": file_record(archive), "files": files, "sha256": digest(files)}


def source_state(root: Path) -> dict:
    def git(*args):
        return subprocess.check_output(["git", "-C", str(root), *args])

    paths = git("ls-files", "--recurse-submodules", "-z").decode().split("\0")
    files = {
        p: file_record(root / p)["sha256"] for p in paths if p and (root / p).is_file()
    }
    submodules = []
    for line in git("submodule", "status", "--recursive").decode().splitlines():
        match = re.fullmatch(r"(.)([0-9a-f]{40}) (.+?)(?: \(.+\))?", line)
        if not match:
            raise ValueError("unrecognized submodule source identity")
        marker, commit, relative = match.groups()
        subroot = (root / relative).resolve()
        if not subroot.is_relative_to(root.resolve()) or marker != " ":
            raise ValueError(
                "uninitialized or mismatched submodule gitlink: " + relative
            )
        actual = (
            subprocess.check_output(["git", "-C", str(subroot), "rev-parse", "HEAD"])
            .decode()
            .strip()
        )
        dirty = subprocess.check_output(
            [
                "git",
                "-C",
                str(subroot),
                "status",
                "--porcelain",
                "--untracked-files=all",
            ]
        )
        if actual != commit or dirty:
            raise ValueError(
                "submodule source is dirty or differs from gitlink: " + relative
            )
        submodules.append({"path": relative, "commit": commit})
    exported = exported_inventory(root) if (root / "conanfile.py").exists() else None
    untracked = (
        git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")
    )
    build_roots = {
        "src",
        "include",
        "cmake",
        "tools",
        "tests",
        "packaging",
        "benchmarks",
        "demo",
        "RGAL",
        "Tester",
    }
    untracked_inputs = [
        p
        for p in untracked
        if p
        and (Path(p).parts[0] in build_roots or exported and p in exported["files"])
    ]
    return {
        "root": str(root.resolve()),
        "commit": git("rev-parse", "HEAD").decode().strip(),
        "tracked_dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "untracked_inputs": untracked_inputs,
        "status_sha256": hashlib.sha256(
            git("status", "--porcelain", "--untracked-files=all")
        ).hexdigest(),
        "input_sha256": digest(files),
        "submodules": submodules,
        "exported": exported,
    }
