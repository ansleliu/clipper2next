import copy
import ctypes
import os
import sys
import unittest
import tempfile
from pathlib import Path

from benchmarks.tools.common import evidence_identity as identity


class ProcessObservationTests(unittest.TestCase):
    def test_observer_rejects_smt_sibling_and_stale_handshake_before_launch(self):
        module = Path(__file__).resolve().parents[3] / "build/correction-baseline-release/bin/clipper2next.dll"
        if os.name != "nt":
            self.skipTest("Windows immutable-baseline integration")
        executable = {"path": str(Path(sys.executable).resolve()),
                      "sha256": identity.sha256_file(Path(sys.executable))}
        runtime = {"path": str(module.resolve()), "sha256": identity.sha256_file(module)}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ready, go = root / "ready", root / "go"
            go.write_text("stale")
            for cpus, ready_file, go_file, reason in (([1],None,None,"physical core"),
                    ([2],ready,go,"fresh")):
                with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                    identity.run_observed_command([executable["path"], "-c", "raise SystemExit(99)"],
                        root / "negative.log", run_id="negative", executable=executable, module=runtime,
                        benchmark_cpus=cpus, observer_cpu=0, ready_file=ready_file, go_file=go_file)

    def test_missing_and_same_name_foreign_module_are_rejected_on_actual_child_pid(self):
        if os.name != "nt":
            self.skipTest("Windows immutable-baseline integration")
        module = Path(__file__).resolve().parents[3] / "build/correction-baseline-release/bin/clipper2next.dll"
        executable = {"path": str(Path(sys.executable).resolve()),
                      "sha256": identity.sha256_file(Path(sys.executable))}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            foreign = root / "clipper2next.dll"
            foreign.write_bytes(module.read_bytes())
            expected = {"path": str(foreign.resolve()), "sha256": identity.sha256_file(foreign)}
            for code in ("import time; time.sleep(0.3)",
                         f"import ctypes,time; ctypes.CDLL({str(module)!r}); time.sleep(0.3)"):
                with self.subTest(code=code), self.assertRaisesRegex(ValueError, "missing actual process observation"):
                    identity.run_observed_command([executable["path"], "-c", code], root / "negative.log",
                        run_id="negative", executable=executable, module=expected,
                        benchmark_cpus=[2], observer_cpu=0)

    def test_actual_process_observation_and_admission_negatives(self):
        owner = getattr(identity, "observe_process", None)
        self.assertTrue(callable(owner), "identity owner needs actual PID observation")
        module = Path(__file__).resolve().parents[3] / "build/correction-baseline-release/bin/clipper2next.dll"
        if os.name == "nt":
            loaded = ctypes.CDLL(str(module))
            self.assertIsNotNone(loaded)
            expected_module = {"path": str(module.resolve()), "sha256": identity.sha256_file(module)}
        else:
            self.skipTest("real DLL integration runs on Windows; Linux observation has root replay")
        command = [str(Path(sys.executable).resolve())]
        observed = owner(os.getpid(), command, "test-run")
        expected_exe = {"path": command[0], "sha256": identity.sha256_file(Path(command[0]))}
        identity.validate_process_observation(observed, command, "test-run", expected_exe, expected_module)
        for label, mutate in {
            "old exe/current run": lambda p: p.update(run_id="old-run"),
            "foreign same name": lambda p: p["modules"][0].update(sha256="sha256:" + "0" * 64),
            "missing observation": lambda p: p.update(modules=[]),
            "wrong process exe": lambda p: p["executable"].update(path="foreign.exe"),
            "wrong command": lambda p: p.update(command=["another.exe"]),
        }.items():
            with self.subTest(label=label):
                bad = copy.deepcopy(observed)
                mutate(bad)
                with self.assertRaises(ValueError):
                    identity.validate_process_observation(bad, command, "test-run", expected_exe, expected_module)


if __name__ == "__main__":
    unittest.main()
