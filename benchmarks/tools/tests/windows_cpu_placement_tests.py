import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import unittest
from unittest.mock import patch

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
from benchmarks.tools.common import evidence_identity


def run_probe(scenario):
    """Use real Windows threads, pausing only at the relevant lifetime boundary."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenThread.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenThread.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.GetThreadId.argtypes = [ctypes.c_void_p]
    kernel.GetThreadId.restype = ctypes.c_ulong
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.WaitForSingleObject.restype = ctypes.c_ulong
    release = threading.Event()
    worker = threading.Thread(target=release.wait)
    worker.start()
    worker_id = worker.native_id
    worker_handle = kernel.OpenThread(0x100040, False, worker_id)
    assert worker_handle
    intercepted = False
    foreign = None
    foreign_id = None
    foreign_sets = 0

    def finish_worker():
        nonlocal worker_handle
        release.set()
        worker.join()
        assert kernel.WaitForSingleObject(worker_handle, 10000) == 0
        kernel.CloseHandle(worker_handle)
        worker_handle = None
    if scenario == "different_owner":
        foreign = subprocess.Popen([sys.executable, "-c",
            "import threading,sys; print(threading.get_native_id(),flush=True); sys.stdin.read(1)"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        foreign_id = int(foreign.stdout.readline())

    def open_thread(access, inherit, tid):
        nonlocal intercepted
        if tid == worker_id:
            intercepted = True
            if scenario == "exit_before_open":
                finish_worker()
            elif scenario == "live_open_failure":
                ctypes.set_last_error(87)
                return None
            elif scenario == "different_owner":
                return kernel.OpenThread(access, inherit, foreign_id)
        return kernel.OpenThread(access, inherit, tid)

    def set_group(handle, target, previous):
        nonlocal intercepted, foreign_sets
        tid = kernel.GetThreadId(handle)
        if scenario == "exit_before_set" and tid == worker_id:
            intercepted = True
            finish_worker()
        if tid == foreign_id:
            foreign_sets += 1
        return kernel.SetThreadGroupAffinity(handle, target, previous)

    class KernelProxy:
        OpenThread = staticmethod(open_thread)
        SetThreadGroupAffinity = staticmethod(set_group)

        def __getattr__(self, name):
            return getattr(kernel, name)

    original_loader = ctypes.WinDLL

    def load(name, **kwargs):
        return KernelProxy() if name == "kernel32" else original_loader(name, **kwargs)

    try:
        with patch.object(evidence_identity.ctypes, "WinDLL", side_effect=load):
            try:
                result = evidence_identity.set_process_cpus(os.getpid(), [0])
                error = None
            except OSError as failure:
                result, error = None, failure.errno
        assert intercepted, "the real worker did not reach the selected boundary"
        if scenario == "live_open_failure":
            assert error == 87 and worker.is_alive(), (error, worker.is_alive())
        else:
            assert error is None, error
            assert result["logical_cpus"] == [0]
            assert result["initial_threads"]
            assert all(t["group"] == 0 and t["mask"] == "0x1" for t in result["initial_threads"])
            assert foreign_sets == 0, "a snapshot entry was applied to a different process"
            assert worker_id not in [t["tid"] for t in result["initial_threads"]]
        print(json.dumps({"scenario": scenario, "passed": True, "error": error}))
    finally:
        release.set()
        worker.join()
        if worker_handle:
            kernel.CloseHandle(worker_handle)
        if foreign:
            foreign.communicate("x", timeout=10)


@unittest.skipUnless(os.name == "nt", "Windows thread placement contract")
class WindowsCpuPlacementTests(unittest.TestCase):
    def test_thread_snapshot_is_limited_to_the_target_process(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]

        class TargetOnlyKernel:
            def __getattr__(self, name):
                if name == "CreateToolhelp32Snapshot":
                    raise AssertionError("placement must not enumerate the whole machine")
                return getattr(kernel, name)

        release = threading.Event()
        workers = [threading.Thread(target=release.wait) for _ in range(8)]
        for worker in workers:
            worker.start()
        try:
            observed = evidence_identity._windows_process_thread_ids(TargetOnlyKernel(), os.getpid())
            self.assertEqual(len(observed), len(set(observed)))
            self.assertTrue({threading.get_native_id(), *(worker.native_id for worker in workers)}
                            <= set(observed))
        finally:
            release.set()
            for worker in workers:
                worker.join()

    def test_thread_lifetime_does_not_weaken_live_thread_placement(self):
        for scenario in ["exit_before_open", "exit_before_set", "live_open_failure", "different_owner"]:
            with self.subTest(scenario=scenario):
                result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--probe", scenario],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertTrue(json.loads(result.stdout)["passed"])


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--probe":
        run_probe(sys.argv[2])
    else:
        unittest.main()
