#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))

from benchmarks.tools.common.evidence_identity import (
    candidate_source_identity_at,
    release_identity,
    sha256_file,
)
from benchmarks.tools.evidence import archive_release_evidence as evidence

TEST_PROTOCOL_IDENTITY = "sha256:" + "1" * 64


def git(*arguments: str) -> str:
    return subprocess.run(
        ["git", "-C", str(REPO_ROOT), *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def release_repository() -> dict:
    return {
        "head_commit": git("rev-parse", "HEAD"),
        "head_tree": git("rev-parse", "HEAD^{tree}"),
        "dirty": False,
        "worktree_status_dirty": False,
        "canonical_source_identity": candidate_source_identity_at(
            REPO_ROOT, "HEAD"
        ),
        "canonical_diff_identity": "sha256:" + "0" * 64,
    }


def write_valid_test_manifest(root: Path) -> Path:
    artifact = root / "ci" / "artifacts" / "asan" / "clipper2next_tests"
    artifact.parent.mkdir(parents=True)
    artifact.write_bytes(b"tested-binary")
    log = root / "ci" / "ctest-linux-gcc-asan-ubsan.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(
        "100% tests passed, 0 tests failed out of 490\n",
        encoding="utf-8",
    )
    repository = release_repository()
    manifest = root / "ci" / "ctest-linux-gcc-asan-ubsan.json"
    manifest.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "name": "ctest-linux-gcc-asan-ubsan",
                "status": "PASS",
                "exit_code": 0,
                "tests_total": 490,
                "tests_failed": 0,
                "sanitizer_failures": 0,
                "log_artifact": "ctest-linux-gcc-asan-ubsan.log",
                "log_sha256": sha256_file(log),
                "candidate_source_identity": repository[
                    "canonical_source_identity"
                ],
                "compiler_identity": "GCC-13.1.0",
                "build_configuration": "ASan-UBSan",
                "cmake_cache_identity": "sha256:" + "2" * 64,
                "git_repository_identity": repository,
                "release_identity": release_identity(repository),
                "protocol_identity": TEST_PROTOCOL_IDENTITY,
                "artifacts": [
                    {
                        "artifact_id": "artifacts/asan/clipper2next_tests",
                        "sha256": sha256_file(artifact),
                    }
                ],
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return manifest


class ReleaseEvidenceArchiveTests(unittest.TestCase):
    def test_derived_comparison_allows_only_float_roundoff(self) -> None:
        reference = {"rows": [{"speedup": 1.6000000000001}], "status": "PASS"}
        self.assertTrue(
            evidence._equivalent_derived(
                reference,
                {"rows": [{"speedup": 1.6000000000002}], "status": "PASS"},
            )
        )
        self.assertFalse(
            evidence._equivalent_derived(
                reference,
                {"rows": [{"speedup": 1.6001}], "status": "PASS"},
            )
        )
        self.assertFalse(
            evidence._equivalent_derived(
                reference,
                {"rows": [{"speedup": 1.6000000000001}], "status": "NOISY"},
            )
        )

    def test_accepts_structured_zero_failure_test_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_valid_test_manifest(root)

            with mock.patch.object(
                evidence,
                "_aggregate_ref_paths",
                return_value=TEST_PROTOCOL_IDENTITY,
            ):
                row, artifacts = evidence.check_test_manifest(
                    root, manifest, release_ref="HEAD"
                )

            self.assertEqual("PASS", row.status)
            self.assertEqual(3, len(artifacts))
            self.assertRegex(row.release_identity or "", r"^sha256:[0-9a-f]{64}$")

    def test_rejects_nonempty_log_without_ctest_success_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_valid_test_manifest(root)
            log = root / "ci" / "ctest-linux-gcc-asan-ubsan.log"
            log.write_text("ok\n", encoding="utf-8")
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            payload["log_sha256"] = sha256_file(log)
            manifest.write_text(json.dumps(payload), encoding="utf-8")

            with mock.patch.object(
                evidence,
                "_aggregate_ref_paths",
                return_value=TEST_PROTOCOL_IDENTITY,
            ):
                row, _ = evidence.check_test_manifest(
                    root, manifest, release_ref="HEAD"
                )

            self.assertEqual("BLOCKED", row.status)
            self.assertIn("zero-failure summary", row.detail)

    def test_rejects_test_manifest_without_build_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_valid_test_manifest(root)
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            del payload["compiler_identity"]
            manifest.write_text(json.dumps(payload), encoding="utf-8")

            with mock.patch.object(
                evidence,
                "_aggregate_ref_paths",
                return_value=TEST_PROTOCOL_IDENTITY,
            ):
                row, _ = evidence.check_test_manifest(
                    root, manifest, release_ref="HEAD"
                )

            self.assertEqual("BLOCKED", row.status)
            self.assertIn("compiler identity", row.detail)

    def test_rejects_sanitizer_failure_even_with_green_ctest_footer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_valid_test_manifest(root)
            log = root / "ci" / "ctest-linux-gcc-asan-ubsan.log"
            log.write_text(
                "ERROR: AddressSanitizer\n"
                "100% tests passed, 0 tests failed out of 490\n",
                encoding="utf-8",
            )
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            payload["log_sha256"] = sha256_file(log)
            manifest.write_text(json.dumps(payload), encoding="utf-8")

            with mock.patch.object(
                evidence,
                "_aggregate_ref_paths",
                return_value=TEST_PROTOCOL_IDENTITY,
            ):
                row, _ = evidence.check_test_manifest(
                    root, manifest, release_ref="HEAD"
                )

            self.assertEqual("BLOCKED", row.status)
            self.assertIn("sanitizer failure", row.detail)

    def test_rejects_private_absolute_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = write_valid_test_manifest(root)
            log = root / "ci" / "ctest-linux-gcc-asan-ubsan.log"
            log.write_text(
                "D:\\private\\build\\tests.exe\n"
                "100% tests passed, 0 tests failed out of 490\n",
                encoding="utf-8",
            )
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            payload["log_sha256"] = sha256_file(log)
            manifest.write_text(json.dumps(payload), encoding="utf-8")

            with mock.patch.object(
                evidence,
                "_aggregate_ref_paths",
                return_value=TEST_PROTOCOL_IDENTITY,
            ):
                row, _ = evidence.check_test_manifest(
                    root, manifest, release_ref="HEAD"
                )

            self.assertEqual("BLOCKED", row.status)
            self.assertIn("private absolute path", row.detail)

    def test_rejects_handwritten_pass_summary_without_identity_chain(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            summary = root / "summary.json"
            summary.write_text(
                json.dumps(
                    {
                        "status": "PASS",
                        "evidence_mode": "release",
                        "calibrated_runner": True,
                        "runner_id": "manual",
                    }
                ),
                encoding="utf-8",
            )

            row, _ = evidence.check_performance_bundle(
                root, summary, directional=False, release_ref="HEAD"
            )

            self.assertEqual("BLOCKED", row.status)
            self.assertIn("expected", row.detail)


if __name__ == "__main__":
    unittest.main()
