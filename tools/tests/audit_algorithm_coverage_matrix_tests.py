#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AUDIT = ROOT / "tools" / "audits" / "audit_algorithm_coverage_matrix.py"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def contract_payload() -> dict[str, object]:
    return {
        "schema_version": 1,
        "equivalence": {
            "coordinate_tolerance": 0,
            "normalize_closed_ring_start": True,
            "normalize_independent_path_order": True,
            "normalize_winding": False,
            "normalize_open_path_direction": False,
        },
        "profiles": [
            {
                "id": "overlay",
                "eligibility_file": "overlay-candidates.jsonl",
                "geometry_types": ["Polygon", "MultiPolygon"],
                "required_case_sets": ["verification", "benchmark"],
                "scenario_quotas": {"overlap": {"min_count": 1}},
                "forbidden_scenarios": [],
            }
        ],
        "algorithms": [
            {
                "id": "clip_overlay",
                "release_gated": True,
                "required_tests": ["DifferentialClip.StrictLegacy"],
                "required_benchmarks": [
                    "BM_external_overlay_legacy",
                    "BM_external_overlay_next",
                ],
                "required_profiles": ["overlay"],
            },
            {
                "id": "geometry_algorithms",
                "release_gated": False,
                "required_tests": ["DifferentialGeometry.StrictLegacy"],
                "required_benchmarks": ["BM_external_geometry"],
                "required_profiles": ["overlay"],
            },
        ],
        "performance": {
            "release_repetitions": 7,
            "min_time_seconds": 0.5,
            "min_warmup_time_seconds": 1.0,
            "release_max_cv_percent": 5.0,
            "directional_max_cv_percent": 15.0,
            "min_pair_speedup": 1.2,
            "min_geomean_speedup": 1.2,
            "time_field": "real_time",
        },
        "provenance": [
            "git_commit",
            "git_tree_state",
            "contract_sha256",
            "profile_sha256",
            "benchmark_executable_sha256",
            "compiler",
            "build_flags",
            "runner_id",
        ],
    }


def run_git(repository: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        text=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr or result.stdout)
    return result.stdout.strip()


def initialize_clean_repository(path: Path) -> str:
    path.mkdir(parents=True)
    run_git(path, "init", "--quiet")
    run_git(path, "config", "user.email", "tests@example.invalid")
    run_git(path, "config", "user.name", "Release Evidence Tests")
    tracked = path / "tracked.txt"
    tracked.write_text("candidate\n", encoding="utf-8")
    run_git(path, "add", "tracked.txt")
    run_git(path, "commit", "--quiet", "-m", "candidate")
    return run_git(path, "rev-parse", "HEAD")


def write_junit(path: Path, statuses: dict[str, str]) -> None:
    suite = ET.Element("testsuite")
    for name, status in statuses.items():
        case = ET.SubElement(suite, "testcase", name=name)
        if status == "FAIL":
            ET.SubElement(case, "failure")
        elif status == "SKIP":
            ET.SubElement(case, "skipped")
        elif status != "PASS":
            raise ValueError(f"unsupported status {status}")
    ET.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


def combined_profile_sha256(entries: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for entry in sorted(entries, key=lambda item: (item["profile"], item["case_set"])):
        digest.update(
            (f"\0{entry['profile']}\0{entry['case_set']}\0{entry['sha256']}").encode(
                "utf-8"
            )
        )
    return digest.hexdigest()


def create_evidence_fixture(
    root: Path,
    *,
    test_status: str = "PASS",
    include_next_benchmark: bool = True,
    profile_status: str = "PASS",
    runtime_contract_sha256: str | None = None,
    runtime_profile_report_sha256: str | None = None,
    runtime_dirty: bool = False,
) -> dict[str, Path]:
    artifacts = root / "artifacts"
    artifacts.mkdir()
    repository = root / "repository"
    commit = initialize_clean_repository(repository)

    contract = artifacts / "contract.json"
    write_json(contract, contract_payload())
    contract_sha256 = sha256_file(contract)

    profile_entries: list[dict[str, object]] = []
    for case_set in ("verification", "benchmark"):
        profile_path = artifacts / "profiles" / case_set / "overlay.jsonl"
        profile_path.parent.mkdir(parents=True)
        profile_path.write_text(
            json.dumps({"id": f"overlay-{case_set}-1"}) + "\n",
            encoding="utf-8",
        )
        profile_entries.append(
            {
                "case_set": case_set,
                "path": str(profile_path.resolve()),
                "profile": "overlay",
                "records": 1,
                "sha256": sha256_file(profile_path),
            }
        )

    profile_report = artifacts / "profile-report.json"
    profile_errors = [] if profile_status == "PASS" else ["semantic failure"]
    write_json(
        profile_report,
        {
            "schema_version": 1,
            "status": profile_status,
            "corpus_root": str((artifacts / "profiles").resolve()),
            "contract_sha256": contract_sha256,
            "profile_sha256": combined_profile_sha256(profile_entries),
            "profiles": profile_entries,
            "error_count": len(profile_errors),
            "errors_truncated": False,
            "error_categories": ({} if not profile_errors else {"semantic failure": 1}),
            "errors": profile_errors,
        },
    )

    ctest_junit = artifacts / "ctest.xml"
    tests = {"DifferentialClip.StrictLegacy": test_status}
    write_junit(ctest_junit, tests)

    benchmark_list = artifacts / "benchmarks.txt"
    benchmarks = ["BM_external_overlay_legacy"]
    if include_next_benchmark:
        benchmarks.append("BM_external_overlay_next")
    benchmark_list.write_text("\n".join(benchmarks) + "\n", encoding="utf-8")

    input_paths = {
        "contract": contract,
        "profile_report": profile_report,
        "ctest_junit": ctest_junit,
        "benchmark_list": benchmark_list,
    }
    runtime_evidence = artifacts / "runtime-evidence.json"
    write_json(
        runtime_evidence,
        {
            "schema_version": 1,
            "contract_sha256": (
                runtime_contract_sha256
                if runtime_contract_sha256 is not None
                else contract_sha256
            ),
            "profile_report_sha256": (
                runtime_profile_report_sha256
                if runtime_profile_report_sha256 is not None
                else sha256_file(profile_report)
            ),
            "git": {"commit": commit, "dirty": runtime_dirty},
            "tests": tests,
            "benchmarks": benchmarks,
            "inputs": {key: str(path.resolve()) for key, path in input_paths.items()},
            "input_sha256": {
                key: sha256_file(path) for key, path in input_paths.items()
            },
        },
    )
    return {
        "repository": repository,
        "contract": contract,
        "profile_report": profile_report,
        "runtime_evidence": runtime_evidence,
        "ctest_junit": ctest_junit,
    }


class AlgorithmCoverageMatrixTests(unittest.TestCase):
    def run_audit(
        self,
        fixture: dict[str, Path],
        *extra_args: str,
        include_runtime: bool = True,
        include_profile: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        command = [
            sys.executable,
            str(AUDIT),
            "--repository-root",
            str(fixture["repository"]),
            "--contract",
            str(fixture["contract"]),
        ]
        if include_runtime:
            command.extend(["--runtime-evidence", str(fixture["runtime_evidence"])])
        if include_profile:
            command.extend(["--profile-report", str(fixture["profile_report"])])
        command.extend(extra_args)
        return subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_source_keywords_without_runtime_evidence_are_blocked(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(Path(temp_dir))
            deceptive_source = fixture["repository"] / "tests" / "all-the-keywords.cpp"
            deceptive_source.parent.mkdir()
            deceptive_source.write_text(
                "TEST(DifferentialClip, StrictLegacy) {}\n"
                "BENCHMARK(BM_external_overlay_legacy);\n"
                "BENCHMARK(BM_external_overlay_next);\n",
                encoding="utf-8",
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
                include_runtime=False,
            )
            payload = json.loads(output.read_text(encoding="utf-8"))
            rows = {row["id"]: row for row in payload["rows"]}

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertEqual("BLOCKED", rows["clip_overlay"]["status"])
        self.assertIn(
            "runtime evidence",
            " ".join(rows["clip_overlay"]["reasons"]),
        )
        self.assertEqual("PARTIAL", rows["geometry_algorithms"]["status"])

    def test_complete_passing_runtime_evidence_is_ready(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(Path(temp_dir))
            output = Path(temp_dir) / "matrix.json"
            markdown = Path(temp_dir) / "matrix.md"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--output-md",
                str(markdown),
                "--fail-on-blocked",
            )
            payload = json.loads(output.read_text(encoding="utf-8"))
            rows = {row["id"]: row for row in payload["rows"]}
            markdown_text = markdown.read_text(encoding="utf-8")

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual("READY", rows["clip_overlay"]["status"])
        self.assertEqual("PASS", rows["clip_overlay"]["tests"]["status"])
        self.assertEqual("PASS", rows["clip_overlay"]["profiles"]["status"])
        self.assertEqual("PASS", rows["clip_overlay"]["benchmarks"]["status"])
        self.assertEqual("PARTIAL", rows["geometry_algorithms"]["status"])
        self.assertEqual({"PARTIAL": 1, "READY": 1}, payload["status_counts"])
        self.assertIn("READY", markdown_text)

    def test_skipped_required_test_is_blocked_not_passed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(
                Path(temp_dir),
                test_status="SKIP",
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            rows = {
                row["id"]: row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
            }

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertEqual("BLOCKED", rows["clip_overlay"]["status"])
        self.assertIn("SKIP", " ".join(rows["clip_overlay"]["reasons"]))

    def test_wrong_contract_and_profile_report_digests_are_blocked(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(
                Path(temp_dir),
                runtime_contract_sha256="c" * 64,
                runtime_profile_report_sha256="d" * 64,
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            row = next(
                row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
                if row["id"] == "clip_overlay"
            )

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        reasons = " ".join(row["reasons"])
        self.assertIn("contract SHA-256", reasons)
        self.assertIn("profile report SHA-256", reasons)

    def test_missing_required_benchmark_is_blocked(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(
                Path(temp_dir),
                include_next_benchmark=False,
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            row = next(
                row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
                if row["id"] == "clip_overlay"
            )

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertIn(
            "BM_external_overlay_next",
            " ".join(row["reasons"]),
        )

    def test_failed_profile_report_blocks_every_release_gated_row(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(
                Path(temp_dir),
                profile_status="FAIL",
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            row = next(
                row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
                if row["id"] == "clip_overlay"
            )

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertIn("semantic validation status is FAIL", " ".join(row["reasons"]))

    def test_dirty_runtime_evidence_cannot_be_ready(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(
                Path(temp_dir),
                runtime_dirty=True,
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            row = next(
                row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
                if row["id"] == "clip_overlay"
            )

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertIn("dirty", " ".join(row["reasons"]))

    def test_repository_change_after_collection_is_blocked_as_stale(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(Path(temp_dir))
            (fixture["repository"] / "tracked.txt").write_text(
                "changed after evidence collection\n",
                encoding="utf-8",
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            row = next(
                row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
                if row["id"] == "clip_overlay"
            )

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertIn("current repository Git tree is dirty", row["reasons"])

    def test_tampered_raw_junit_is_blocked_even_if_runtime_claims_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(Path(temp_dir))
            write_junit(
                fixture["ctest_junit"],
                {"DifferentialClip.StrictLegacy": "FAIL"},
            )
            output = Path(temp_dir) / "matrix.json"

            result = self.run_audit(
                fixture,
                "--output-json",
                str(output),
                "--fail-on-blocked",
            )
            row = next(
                row
                for row in json.loads(output.read_text(encoding="utf-8"))["rows"]
                if row["id"] == "clip_overlay"
            )

        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        reasons = " ".join(row["reasons"])
        self.assertIn("ctest_junit SHA-256", reasons)
        self.assertIn("do not match the recorded CTest JUnit", reasons)

    def test_default_outputs_stay_under_repository_benchmark_results(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = create_evidence_fixture(Path(temp_dir))

            result = self.run_audit(fixture)
            output_json = (
                fixture["repository"]
                / "benchmarks"
                / "results"
                / "algorithm_coverage_matrix.json"
            )
            output_md = output_json.with_suffix(".md")

            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertTrue(output_json.exists())
            self.assertTrue(output_md.exists())


if __name__ == "__main__":
    unittest.main()
