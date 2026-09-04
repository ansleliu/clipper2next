import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from benchmarks.tools.common.evidence_identity import (
    candidate_source_identity,
    git_repository_identity,
    protocol_identity,
    protocol_identity_at,
    runtime_library_identity,
)


def run_git(root: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


class EvidenceIdentityTests(unittest.TestCase):
    def test_protocol_identity_matches_the_committed_file_set(self) -> None:
        names = (
            "benchmarks/tools/runners/run_calibrated_external_performance_gate.py",
            "benchmarks/tools/common/evidence_identity.py",
            "benchmarks/tools/common/external_core_measurement.py",
            "benchmarks/tools/common/release_gate_policy.py",
            "benchmarks/tools/gates/external_benchmark_variance_gate.py",
            "benchmarks/tools/gates/external_legacy_speedup_gate.py",
            "benchmarks/oracle/external_corpus_benchmark.cpp",
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_git(root, "init")
            run_git(root, "config", "user.name", "Evidence Test")
            run_git(root, "config", "user.email", "evidence@example.invalid")
            for ordinal, name in enumerate(reversed(names)):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(f"protocol-{ordinal}\n", encoding="utf-8")
            run_git(root, "add", ".")
            run_git(root, "commit", "-m", "protocol")

            self.assertEqual(
                protocol_identity(root),
                protocol_identity_at(root, "HEAD"),
            )

    @unittest.skipUnless(os.name == "nt", "Windows DLL discovery contract")
    def test_runtime_identity_archives_binary_without_absolute_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "benchmark.exe"
            library = root / "clipper2next.dll"
            executable.write_bytes(b"benchmark")
            library.write_bytes(b"runtime")

            identity = runtime_library_identity(
                executable, root / "evidence" / "artifacts"
            )

            self.assertEqual("shared", identity["linkage"])
            self.assertEqual("clipper2next.dll", identity["soname"])
            self.assertEqual("clipper2next.dll", identity["basename"])
            self.assertEqual(
                "artifacts/clipper2next.dll", identity["artifact_id"]
            )
            self.assertFalse(any("path" in key.lower() for key in identity))

    def test_git_filtered_source_identity_is_independent_of_checkout_eol(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_git(root, "init")
            run_git(root, "config", "user.name", "Evidence Test")
            run_git(root, "config", "user.email", "evidence@example.invalid")
            (root / ".gitattributes").write_text("*.cpp text eol=lf\n", encoding="utf-8")
            (root / "CMakeLists.txt").write_text("project(test)\n", encoding="utf-8")
            (root / "src").mkdir()
            source = root / "src" / "sample.cpp"
            source.write_bytes(b"int value = 1;\n")
            run_git(root, "add", ".")
            run_git(root, "commit", "-m", "initial")

            lf_identity = candidate_source_identity(root)
            source.write_bytes(b"int value = 1;\r\n")
            crlf_identity = candidate_source_identity(root)
            repository = git_repository_identity(root)

            self.assertEqual(lf_identity, crlf_identity)
            self.assertFalse(repository["dirty"])
            self.assertRegex(repository["head_commit"], r"^[0-9a-f]{40}$")
            self.assertRegex(repository["head_tree"], r"^[0-9a-f]{40}$")
            self.assertRegex(
                repository["canonical_diff_identity"],
                r"^sha256:[0-9a-f]{64}$",
            )

    def test_dirty_canonical_blob_changes_source_and_diff_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_git(root, "init")
            run_git(root, "config", "user.name", "Evidence Test")
            run_git(root, "config", "user.email", "evidence@example.invalid")
            (root / "CMakeLists.txt").write_text("project(test)\n", encoding="utf-8")
            (root / "src").mkdir()
            source = root / "src" / "sample.cpp"
            source.write_text("int value = 1;\n", encoding="utf-8")
            run_git(root, "add", ".")
            run_git(root, "commit", "-m", "initial")
            clean_source = candidate_source_identity(root)
            clean_repository = git_repository_identity(root)

            source.write_text("int value = 2;\n", encoding="utf-8")
            dirty_source = candidate_source_identity(root)
            dirty_repository = git_repository_identity(root)

            self.assertNotEqual(clean_source, dirty_source)
            self.assertNotEqual(
                clean_repository["canonical_diff_identity"],
                dirty_repository["canonical_diff_identity"],
            )
            self.assertTrue(dirty_repository["dirty"])


if __name__ == "__main__":
    unittest.main()
