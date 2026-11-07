from __future__ import annotations

import hashlib
import ctypes
import json
import os
import shutil
import subprocess
import time
from pathlib import Path


def process_paths(pid: int) -> tuple[Path, list[Path]]:
    """Query the producing process, never infer its modules from neighboring files."""
    if os.name != "nt":
        root = Path("/proc") / str(pid)
        executable = (root / "exe").resolve(strict=True)
        paths = set()
        for line in (root / "maps").read_text(encoding="utf-8").splitlines():
            fields = line.split(None, 5)
            if len(fields) == 6 and fields[5].startswith("/"):
                paths.add(Path(fields[5]).resolve(strict=True))
        return executable, sorted(paths)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                                ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
    psapi.EnumProcessModulesEx.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p),
                                          ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.c_ulong]
    psapi.GetModuleFileNameExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_ulong]
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        raise OSError(ctypes.get_last_error(), "OpenProcess failed")
    try:
        buffer = ctypes.create_unicode_buffer(32768)
        length = ctypes.c_ulong(len(buffer))
        if not kernel.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(length)):
            raise OSError(ctypes.get_last_error(), "QueryFullProcessImageName failed")
        executable = Path(buffer.value).resolve(strict=True)
        modules = (ctypes.c_void_p * 1024)()
        needed = ctypes.c_ulong()
        if not psapi.EnumProcessModulesEx(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
            raise OSError(ctypes.get_last_error(), "EnumProcessModulesEx failed")
        if needed.value > ctypes.sizeof(modules):
            raise OSError("process module enumeration truncated")
        paths = []
        for module in modules[:needed.value // ctypes.sizeof(ctypes.c_void_p)]:
            if not psapi.GetModuleFileNameExW(handle, module, buffer, len(buffer)):
                raise OSError(ctypes.get_last_error(), "GetModuleFileNameEx failed")
            paths.append(Path(buffer.value).resolve(strict=True))
        return executable, paths
    finally:
        kernel.CloseHandle(handle)


def observe_process(pid: int, command: list[str], run_id: str) -> dict:
    executable, modules = process_paths(pid)
    clipper = [p for p in modules if p.name.lower() == "clipper2next.dll" or
               p.name.startswith("libclipper2next.so")]
    return {"pid": pid, "command": command, "run_id": run_id,
            "facility": "Windows QueryFullProcessImageName/EnumProcessModulesEx" if os.name == "nt" else "Linux /proc/exe/maps",
            "executable": {"path": str(executable), "sha256": sha256_file(executable)},
            "modules": [{"path": str(p), "sha256": sha256_file(p)} for p in sorted(set(clipper))]}


def validate_process_observation(observed, command, run_id, executable, module):
    if (not isinstance(run_id, str) or not run_id or not isinstance(observed, dict) or
            type(observed.get("pid")) is not int or
            observed["pid"] <= 0 or observed.get("command") != command or
            observed.get("run_id") != run_id or not observed.get("facility") or
            observed.get("executable") != executable or observed.get("modules") != [module]):
        raise ValueError("actual producing PID/executable/module observation does not match freeze")


def physical_core_sets() -> list[list[int]]:
    if os.name != "nt":
        groups = {}
        for path in Path("/sys/devices/system/cpu").glob("cpu[0-9]*"):
            topology = path / "topology"
            key = ((topology / "physical_package_id").read_text().strip(),
                   (topology / "core_id").read_text().strip())
            groups.setdefault(key, []).append(int(path.name[3:]))
        return sorted(sorted(v) for v in groups.values())
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    size = ctypes.c_ulong()
    kernel.GetLogicalProcessorInformationEx(0, None, ctypes.byref(size))
    buffer = ctypes.create_string_buffer(size.value)
    if not kernel.GetLogicalProcessorInformationEx(0, buffer, ctypes.byref(size)):
        raise OSError(ctypes.get_last_error(), "GetLogicalProcessorInformationEx failed")
    data = buffer.raw
    offset, groups = 0, []
    while offset < size.value:
        length = int.from_bytes(data[offset + 4:offset + 8], "little")
        group_count = int.from_bytes(data[offset + 30:offset + 32], "little")
        cpus = []
        for i in range(group_count):
            start = offset + 32 + i * 16
            mask = int.from_bytes(data[start:start + 8], "little")
            group = int.from_bytes(data[start + 8:start + 10], "little")
            cpus.extend(group * 64 + cpu for cpu in range(64) if mask & (1 << cpu))
        groups.append(cpus)
        offset += length
    return groups


def _windows_process_thread_ids(kernel, pid):
    # Capture only this process. Toolhelp's thread snapshot ignores its PID
    # argument and makes warmup observation depend on every thread in the OS.
    class FileTime(ctypes.Structure):
        _fields_ = [("low", ctypes.c_ulong), ("high", ctypes.c_ulong)]

    class ThreadEntry(ctypes.Structure):
        _fields_ = [("exit_status", ctypes.c_ulong), ("teb", ctypes.c_void_p),
                    ("pid", ctypes.c_ulong), ("tid", ctypes.c_ulong),
                    ("affinity", ctypes.c_size_t), ("priority", ctypes.c_int),
                    ("base_priority", ctypes.c_int), ("syscall_arg", ctypes.c_void_p),
                    ("syscall_number", ctypes.c_ushort), ("create_time", FileTime),
                    ("exit_time", FileTime), ("kernel_time", FileTime),
                    ("user_time", FileTime), ("start_address", ctypes.c_void_p),
                    ("capture_time", FileTime), ("flags", ctypes.c_int),
                    ("suspend_count", ctypes.c_ushort), ("context_size", ctypes.c_ushort),
                    ("context", ctypes.c_void_p)]

    kernel.PssCaptureSnapshot.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                        ctypes.POINTER(ctypes.c_void_p)]
    kernel.PssWalkMarkerCreate.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    kernel.PssWalkSnapshot.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                     ctypes.c_void_p, ctypes.c_ulong]
    kernel.PssWalkMarkerFree.argtypes = [ctypes.c_void_p]
    kernel.PssFreeSnapshot.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    process = kernel.OpenProcess(0x400, False, pid)
    if not process:
        raise OSError(ctypes.get_last_error(), "thread snapshot process open failed")
    snapshot, marker = ctypes.c_void_p(), ctypes.c_void_p()
    try:
        error = kernel.PssCaptureSnapshot(process, 0x80, 0, ctypes.byref(snapshot))  # PSS_CAPTURE_THREADS
        if error:
            raise OSError(error, "target thread snapshot failed")
        error = kernel.PssWalkMarkerCreate(None, ctypes.byref(marker))
        if error:
            raise OSError(error, "thread snapshot marker failed")
        threads, entry = [], ThreadEntry()
        while True:
            error = kernel.PssWalkSnapshot(snapshot, 3, marker, ctypes.byref(entry), ctypes.sizeof(entry))
            if error == 259:  # ERROR_NO_MORE_ITEMS
                return threads
            if error:
                raise OSError(error, "target thread enumeration failed")
            if entry.pid != pid:
                raise ValueError("target snapshot returned a different thread owner")
            threads.append(entry.tid)
    finally:
        if marker:
            kernel.PssWalkMarkerFree(marker)
        if snapshot:
            kernel.PssFreeSnapshot(kernel.GetCurrentProcess(), snapshot)
        kernel.CloseHandle(process)


def set_process_cpus(pid, cpus):
    if os.name != "nt":
        os.sched_setaffinity(pid, cpus)
        actual = sorted(os.sched_getaffinity(pid))
        if actual != sorted(cpus):
            raise ValueError("actual process affinity differs")
        return {"facility": "sched_setaffinity/sched_getaffinity", "logical_cpus": actual}
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.GetSystemCpuSetInformation.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                                 ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p, ctypes.c_ulong]
    kernel.SetProcessDefaultCpuSets.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.c_ulong]
    kernel.GetProcessDefaultCpuSets.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong),
                                               ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong)]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x3400, False, pid)
    if not handle:
        raise OSError(ctypes.get_last_error(), "OpenProcess affinity failed")
    try:
        size = ctypes.c_ulong()
        kernel.GetSystemCpuSetInformation(None, 0, ctypes.byref(size), handle, 0)
        buffer = ctypes.create_string_buffer(size.value)
        if not kernel.GetSystemCpuSetInformation(buffer, size, ctypes.byref(size), handle, 0):
            raise OSError(ctypes.get_last_error(), "GetSystemCpuSetInformation failed")
        data, offset, selected = buffer.raw, 0, []
        while offset < size.value:
            length = int.from_bytes(data[offset:offset + 4], "little")
            group = int.from_bytes(data[offset + 12:offset + 14], "little")
            number = data[offset + 14]
            if group * 64 + number in cpus:
                selected.append({"id": int.from_bytes(data[offset + 8:offset + 12], "little"),
                                 "group": group, "number": number, "core_index": data[offset + 15],
                                 "l3_index": data[offset + 16], "numa_index": data[offset + 17]})
            offset += length
        if len(selected) != len(cpus):
            raise ValueError("requested CPUs absent from OS CPU sets")
        ids = (ctypes.c_ulong * len(selected))(*(c["id"] for c in selected))
        if not kernel.SetProcessDefaultCpuSets(handle, ids, len(ids)):
            raise OSError(ctypes.get_last_error(), "SetProcessDefaultCpuSets failed")
        actual = (ctypes.c_ulong * len(ids))()
        required = ctypes.c_ulong()
        if (not kernel.GetProcessDefaultCpuSets(handle, actual, len(actual), ctypes.byref(required)) or
                required.value != len(ids) or set(actual) != set(ids)):
            raise ValueError("actual process CPU-set assignment differs")
        # CPU sets respect existing group affinity. Move every initial thread
        # into the selected group before the ready/go barrier releases it.
        groups = {c["group"] for c in selected}
        if len(groups) != 1:
            raise ValueError("this bounded protocol places a process in one processor group")
        class GroupAffinity(ctypes.Structure):
            _fields_ = [("mask", ctypes.c_size_t), ("group", ctypes.c_ushort),
                        ("reserved", ctypes.c_ushort * 3)]
        kernel.OpenThread.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenThread.restype = ctypes.c_void_p
        kernel.GetProcessIdOfThread.argtypes = [ctypes.c_void_p]
        kernel.GetProcessIdOfThread.restype = ctypes.c_ulong
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.WaitForSingleObject.restype = ctypes.c_ulong
        kernel.SetThreadGroupAffinity.argtypes = [ctypes.c_void_p, ctypes.POINTER(GroupAffinity), ctypes.c_void_p]
        kernel.GetThreadGroupAffinity.argtypes = [ctypes.c_void_p, ctypes.POINTER(GroupAffinity)]
        target = GroupAffinity(sum(1 << c["number"] for c in selected), next(iter(groups)))
        applied_threads = []
        departed_threads = []
        for tid in _windows_process_thread_ids(kernel, pid):
            thread = kernel.OpenThread(0x100060, False, tid)
            if not thread:
                error = ctypes.get_last_error()
                # A snapshot is not a lifetime lease. Only a fresh observation
                # that this thread has gone can excuse an unavailable handle.
                if error == 87 and tid not in _windows_process_thread_ids(kernel, pid):
                    departed_threads.append(tid)
                    continue
                raise OSError(error, "OpenThread placement failed")
            try:
                owner = kernel.GetProcessIdOfThread(thread)
                if not owner:
                    raise OSError(ctypes.get_last_error(), "thread owner query failed")
                if owner != pid:
                    departed_threads.append(tid)
                    continue
                actual_group = GroupAffinity()
                placed = (kernel.SetThreadGroupAffinity(thread, ctypes.byref(target), None) and
                          kernel.GetThreadGroupAffinity(thread, ctypes.byref(actual_group)))
                state = kernel.WaitForSingleObject(thread, 0)
                if state == 0:  # Thread completed during placement.
                    departed_threads.append(tid)
                    continue
                if state != 258:  # WAIT_TIMEOUT is the live-thread observation.
                    raise OSError(ctypes.get_last_error(), "thread lifetime query failed")
                if not placed or actual_group.group != target.group or actual_group.mask != target.mask:
                    raise ValueError("actual initial-thread processor group/affinity differs")
                applied_threads.append({"tid": tid, "group": actual_group.group,
                                        "mask": hex(actual_group.mask)})
            finally:
                kernel.CloseHandle(thread)
        if not applied_threads:
            raise ValueError("no actual process threads observed for placement")
        return {"facility": "GetSystemCpuSetInformation/SetProcessDefaultCpuSets/GetProcessDefaultCpuSets",
                "cpu_sets": selected, "logical_cpus": sorted(cpus), "initial_threads": applied_threads,
                "departed_threads": departed_threads}
    finally:
        kernel.CloseHandle(handle)


def run_observed_command(command, log, *, run_id, executable, module,
                         benchmark_cpus, observer_cpu, warmup_seconds=1.0,
                         ready_file=None, go_file=None, env=None):
    """One producer driver for immutable old and current public processes.

    Framework runs must finish observation inside their mandatory warmup.
    The public supplement waits on a ready/go handshake before touching geometry.
    """
    if (ready_file is None) != (go_file is None):
        raise ValueError("ready/go handshake requires both fresh paths")
    if ready_file and (Path(ready_file).exists() or Path(go_file).exists()):
        raise ValueError("ready/go handshake paths must be fresh")
    cores = physical_core_sets()
    observer_core = next((set(c) for c in cores if observer_cpu in c), None)
    if not observer_core or observer_core & set(benchmark_cpus):
        raise ValueError("observer must occupy a different physical core, including SMT siblings")
    for expected in (executable, module):
        if sha256_file(Path(expected["path"])) != expected["sha256"]:
            raise ValueError("frozen executable/module changed before launch")
    if Path(command[0]).resolve() != Path(executable["path"]).resolve():
        raise ValueError("command executable differs from freeze")
    set_process_cpus(os.getpid(), [observer_cpu])
    started = time.monotonic()
    log.parent.mkdir(parents=True, exist_ok=True)
    observation = None
    with log.open("w", encoding="utf-8") as handle:
        process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT, env=env,
                                   preexec_fn=(lambda: os.sched_setaffinity(0, benchmark_cpus)) if os.name != "nt" else None)
        try:
            applied = set_process_cpus(process.pid, benchmark_cpus)
            deadline = started + (30 if ready_file else warmup_seconds)
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    if ready_file is None or Path(ready_file).is_file():
                        observation = observe_process(process.pid, command, run_id)
                        validate_process_observation(observation, command, run_id, executable, module)
                        break
                except (OSError, ValueError):
                    observation = None
                time.sleep(0.001)
            if observation is None:
                raise ValueError("missing actual process observation before measurement")
            observation["observation_seconds"] = time.monotonic() - started
            observation["benchmark_cpus"] = benchmark_cpus
            observation["observer_cpu"] = observer_cpu
            observation["physical_cores"] = cores
            observation["applied_placement"] = applied
            if ready_file:
                Path(go_file).write_text(run_id, encoding="utf-8")
            elif observation["observation_seconds"] >= warmup_seconds:
                raise ValueError("module observation overlapped formal measurement")
            status = process.wait()
            for expected in (executable, module):
                if sha256_file(Path(expected["path"])) != expected["sha256"]:
                    raise ValueError("frozen executable/module changed after execution")
            observation["exit_code"] = status
            return observation
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()


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
    top_level = _git(root, "rev-parse", "--show-toplevel").stdout.decode("utf-8").strip()
    if Path(top_level).resolve() != root.resolve():
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


def candidate_source_identity_at(root: Path, reference: str) -> str:
    completed = _git(root, "ls-tree", "-r", "-z", reference)
    blobs: dict[str, str | None] = {}
    for record in completed.stdout.split(b"\0"):
        if not record:
            continue
        metadata, relative_bytes = record.split(b"\t", 1)
        _, kind, blob = metadata.split(b" ", 2)
        relative = relative_bytes.decode("utf-8")
        if kind == b"blob" and _is_candidate_relative_path(relative):
            blobs[relative] = blob.decode("ascii")
    return _aggregate_blob_map(blobs)


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
    if not all(path.is_file() for path in paths):
        return None
    digest = hashlib.sha256()
    for name, path in sorted(zip(names, paths, strict=True)):
        content = path.read_bytes().replace(b"\r\n", b"\n")
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(content).digest())
    return f"sha256:{digest.hexdigest()}"


def protocol_identity_at(root: Path, reference: str) -> str:
    names = (
        "benchmarks/tools/runners/run_calibrated_external_performance_gate.py",
        "benchmarks/tools/common/evidence_identity.py",
        "benchmarks/tools/common/external_core_measurement.py",
        "benchmarks/tools/common/release_gate_policy.py",
        "benchmarks/tools/gates/external_benchmark_variance_gate.py",
        "benchmarks/tools/gates/external_legacy_speedup_gate.py",
        "benchmarks/oracle/external_corpus_benchmark.cpp",
    )
    digest = hashlib.sha256()
    for name in sorted(names):
        content = _git(root, "show", f"{reference}:{name}").stdout.replace(
            b"\r\n", b"\n"
        )
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(content).digest())
    return f"sha256:{digest.hexdigest()}"


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
        "cxx_compiler": Path(compiler).name if compiler else "",
        "cxx_compiler_version": version,
        "cxx_flags": values.get("CMAKE_CXX_FLAGS", ""),
        "cxx_flags_release": values.get("CMAKE_CXX_FLAGS_RELEASE", ""),
    }


def _runtime_library(benchmark_executable: Path) -> tuple[str, Path] | None:
    if os.name == "nt":
        library = benchmark_executable.parent / "clipper2next.dll"
        if library.is_file():
            return library.name, library.resolve()
        return None

    try:
        completed = subprocess.run(
            ["ldd", str(benchmark_executable)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    for line in completed.stdout.splitlines():
        if "libclipper2next.so" not in line or "=>" not in line:
            continue
        path_text = line.split("=>", 1)[1].strip().split(" ", 1)[0]
        library = Path(path_text)
        if library.is_file():
            return line.split("=>", 1)[0].strip(), library.resolve()
    return None


def runtime_library_identity(
    benchmark_executable: Path,
    artifact_directory: Path | None = None,
) -> dict:
    resolved = _runtime_library(benchmark_executable)
    if resolved is None:
        return {"linkage": "standalone"}
    soname, library = resolved
    result = {
        "linkage": "shared",
        "soname": soname,
        "basename": library.name,
        "sha256": sha256_file(library),
    }
    if artifact_directory is not None:
        artifact_directory.mkdir(parents=True, exist_ok=True)
        destination = artifact_directory / library.name
        shutil.copy2(library, destination)
        if sha256_file(destination) != result["sha256"]:
            raise RuntimeError("archived runtime library identity changed during copy")
        result["artifact_id"] = destination.relative_to(
            artifact_directory.parent
        ).as_posix()
    return result


def release_identity(repository: dict) -> str:
    payload = {
        "head_commit": repository["head_commit"],
        "head_tree": repository["head_tree"],
        "canonical_source_identity": repository["canonical_source_identity"],
    }
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return f"sha256:{hashlib.sha256(encoded.encode('utf-8')).hexdigest()}"


def collect_evidence_identity(
    root: Path,
    benchmark_executable: Path,
    artifact_directory: Path | None = None,
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
    runtime = runtime_library_identity(benchmark_executable, artifact_directory)
    repository = git_repository_identity(root)
    benchmark_artifact_id = None
    if artifact_directory is not None and benchmark_executable.is_file():
        artifact_directory.mkdir(parents=True, exist_ok=True)
        archived_executable = artifact_directory / benchmark_executable.name
        shutil.copy2(benchmark_executable, archived_executable)
        if sha256_file(archived_executable) != executable_identity:
            raise RuntimeError("archived benchmark identity changed during copy")
        benchmark_artifact_id = archived_executable.relative_to(
            artifact_directory.parent
        ).as_posix()
    payload = {
        "benchmark_executable_identity": executable_identity,
        "benchmark_artifact_id": benchmark_artifact_id,
        "candidate_source_identity": source_identity,
        "compiler_identity": build_identity,
        "corpus_identity": data_identity,
        "protocol_identity": protocol,
        "runtime_library_identity": runtime,
        "git_repository_identity": repository,
        "release_identity": (
            release_identity(repository) if repository is not None else None
        ),
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
            "release_identity",
        )
    )
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return {
        **payload,
        "identity_complete": complete,
        "evidence_identity":
            f"sha256:{hashlib.sha256(encoded.encode('utf-8')).hexdigest()}",
    }
