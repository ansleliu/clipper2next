#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

from tools.audits import audit_external_corpus_coverage as audit  # noqa: E402
from tools.release.evidence_contract import (  # noqa: E402
    AlgorithmContract,
    ProfileContract,
    ReleaseEvidenceContract,
)


def write_sources_manifest(root: Path, raw_features: int = 3) -> None:
    manifests_dir = root / "manifests"
    manifests_dir.mkdir(parents=True, exist_ok=True)
    (manifests_dir / "sources.csv").write_text(
        "source_id,kind,url,local_path,revision_or_version,sha256,license_id,"
        "raw_features,status\n"
        "sample,file,https://example.test/a.geojson,sources/sample/a.geojson,,"
        f"pending,Test,{raw_features},required\n",
        encoding="utf-8",
    )


def write_jsonl(path: Path, records: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(f"{json.dumps(record, sort_keys=True)}\n" for record in records),
        encoding="utf-8",
    )


def profile_contract(
    profile_id: str,
    *,
    eligibility_file: str = "shape-inputs.jsonl",
    geometry_types: tuple[str, ...] = ("Polygon", "MultiPolygon"),
) -> ProfileContract:
    return ProfileContract(
        id=profile_id,
        eligibility_file=eligibility_file,
        geometry_types=geometry_types,
        required_case_sets=("verification", "benchmark"),
        scenario_quotas={},
        forbidden_scenarios=(),
    )


def coverage_contract(*profiles: ProfileContract) -> ReleaseEvidenceContract:
    profile_ids = tuple(profile.id for profile in profiles)
    return ReleaseEvidenceContract(
        schema_version=1,
        equivalence={},
        profiles={profile.id: profile for profile in profiles},
        algorithms={
            "test_release": AlgorithmContract(
                id="test_release",
                release_gated=True,
                required_tests=("test",),
                required_benchmarks=("benchmark",),
                required_profiles=profile_ids,
            )
        },
        performance={},
        provenance=(),
        sha256="0" * 64,
    )


def shape_record(base_id: str, geometry_type: str = "Polygon") -> dict[str, object]:
    return {
        "id": base_id,
        "geometry": {
            "geometry_type": geometry_type,
            "wkt": "POLYGON EMPTY",
        },
    }


def selected_record(
    base_id: str,
    record_id: str | None = None,
    scenario: str | None = None,
) -> dict[str, object]:
    record: dict[str, object] = {
        "id": record_id or f"selected-{base_id}",
        "selector": {"full_id": base_id},
    }
    if scenario is not None:
        record["scenario"] = scenario
    return record


class ExternalCorpusCoverageAuditTests(unittest.TestCase):
    def test_unique_coverage_percent_is_bounded_by_unique_ids(self) -> None:
        self.assertEqual(
            100.0,
            audit.unique_coverage_percent({"a", "b"}, {"a", "b"}),
        )

        with self.assertRaisesRegex(ValueError, "unknown selector full_id"):
            audit.unique_coverage_percent({"a", "b", "missing"}, {"a", "b"})

    def test_repeated_base_ids_across_profiles_count_once_overall(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            write_jsonl(
                root / "normalized" / "full" / "shape-inputs.jsonl",
                [shape_record("a"), shape_record("b"), shape_record("c")],
            )
            for case_set in ("verification", "benchmark"):
                for profile_id in ("rectclip", "offset"):
                    write_jsonl(
                        root / "normalized" / case_set / f"{profile_id}.jsonl",
                        [selected_record("a"), selected_record("b")],
                    )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(
                    profile_contract("rectclip"),
                    profile_contract("offset"),
                ),
            )

        self.assertEqual(66.67, report.unique_verification_coverage_percent)
        self.assertEqual(66.67, report.unique_benchmark_coverage_percent)
        self.assertLessEqual(report.unique_verification_coverage_percent, 100.0)
        self.assertEqual(
            2,
            report.profile_coverage["rectclip"].verification_selected_unique_ids,
        )
        self.assertTrue(report.coverage_admissible)
        self.assertEqual([], report.contract_errors)

    def test_profile_eligibility_filters_shape_records_by_geometry_type(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            write_jsonl(
                root / "normalized" / "full" / "shape-inputs.jsonl",
                [
                    shape_record("polygon", "Polygon"),
                    shape_record("line", "LineString"),
                ],
            )
            for case_set in ("verification", "benchmark"):
                write_jsonl(
                    root / "normalized" / case_set / "rectclip.jsonl",
                    [selected_record("polygon")],
                )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(profile_contract("rectclip")),
            )

        profile = report.profile_coverage["rectclip"]
        self.assertEqual(1, profile.eligible_unique_ids)
        self.assertEqual(100.0, profile.verification_coverage_percent)
        self.assertEqual([], report.contract_errors)

    def test_unknown_selector_id_is_a_contract_error(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            write_jsonl(
                root / "normalized" / "full" / "shape-inputs.jsonl",
                [shape_record("known")],
            )
            for case_set in ("verification", "benchmark"):
                write_jsonl(
                    root / "normalized" / case_set / "rectclip.jsonl",
                    [selected_record("missing")],
                )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(profile_contract("rectclip")),
            )

        self.assertIn("unknown selector full_id", "\n".join(report.contract_errors))
        self.assertLessEqual(report.unique_verification_coverage_percent, 100.0)
        self.assertFalse(report.coverage_admissible)

    def test_same_base_id_in_distinct_scenarios_counts_once(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            write_jsonl(
                root / "normalized" / "full" / "shape-inputs.jsonl",
                [shape_record("shared")],
            )
            for case_set in ("verification", "benchmark"):
                write_jsonl(
                    root / "normalized" / case_set / "rectclip.jsonl",
                    [
                        selected_record(
                            "shared",
                            f"{case_set}-crossing",
                            "crossing",
                        ),
                        selected_record(
                            "shared",
                            f"{case_set}-boundary",
                            "boundary",
                        ),
                    ],
                )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(profile_contract("rectclip")),
            )

        self.assertEqual(1, report.profile_coverage["rectclip"].eligible_unique_ids)
        self.assertEqual(
            1,
            report.profile_coverage["rectclip"].verification_selected_unique_ids,
        )
        self.assertEqual([], report.contract_errors)

    def test_duplicate_full_or_record_ids_are_contract_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            write_jsonl(
                root / "normalized" / "full" / "shape-inputs.jsonl",
                [shape_record("duplicate"), shape_record("duplicate")],
            )
            for case_set in ("verification", "benchmark"):
                write_jsonl(
                    root / "normalized" / case_set / "rectclip.jsonl",
                    [
                        selected_record("duplicate", "same-record"),
                        selected_record("duplicate", "same-record"),
                    ],
                )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(profile_contract("rectclip")),
            )

        errors = "\n".join(report.contract_errors)
        self.assertIn("duplicate eligible full ID", errors)
        self.assertIn("duplicate record id", errors)

    def test_groups_manifest_rows_and_flags_under_sampled_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "manifest.tsv").write_text(
                "\n".join(
                    [
                        "source\tkind\turl\tlocal_path\tsha256\traw_features\twkt_cases",
                        f"natural_earth\tshapefile_zip\tu\t{root.as_posix()}\th\t100\t100",
                        f"tiger\tshapefile_zip\tu\t{root.as_posix()}\th\t1000\t50",
                        f"geos_xml\tgit_sparse_xml\tu\t{root.as_posix()}\tdirectory\t174\t0",
                    ]
                ),
                encoding="utf-8",
            )

            report = audit.audit_coverage(root)

        self.assertEqual(1100, report.total_raw_features)
        self.assertEqual(150, report.total_wkt_cases)
        self.assertEqual(
            100.0,
            report.sources["natural_earth"].inventory_sampling_percent,
        )
        self.assertEqual(5.0, report.sources["tiger"].inventory_sampling_percent)
        self.assertNotIn("tiger", report.expand_default_candidates)
        self.assertIn("tiger", report.expand_extended_candidates)

    def test_recommends_extended_gate_when_operations_are_rectangle_intersections(
        self,
    ) -> None:
        source = audit.SourceCoverage(
            source="tiger",
            raw_features=3235,
            wkt_cases=160,
            inventory_sampling_percent=4.95,
        )
        report = audit.CoverageReport(
            total_raw_features=3235,
            total_wkt_cases=160,
            geos_xml_files=174,
            geos_xml_executed_sample=32,
            sources={"tiger": source},
            default_gate_recommendation="keep-bounded",
            extended_gate_recommendation="required",
            expand_default_candidates=[],
            expand_extended_candidates=["tiger"],
            missing_dimensions=["operation_mix"],
        )

        markdown = audit.format_markdown(report)

        self.assertIn("keep bounded", markdown)
        self.assertIn("Extended/nightly", markdown)
        self.assertIn("operation_mix", markdown)

    def test_supports_custom_manifest_and_wkt_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            wkt_dir = root / "wkt_extended"
            wkt_dir.mkdir()
            (root / "manifest_extended.tsv").write_text(
                "\n".join(
                    [
                        "source\tkind\turl\tlocal_path\tsha256\traw_features\twkt_cases",
                        f"tiger\textended_shapefile_zip\tu\t{root.as_posix()}\th\t3235\t8000",
                    ]
                ),
                encoding="utf-8",
            )
            (wkt_dir / "tiger_extended_wkt.tsv").write_text(
                "\n".join(
                    [
                        "# name\toperation\tscale\tsubject_wkt\tclip_wkt",
                        "tiger/a\tintersection\t1\tPOLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))\tPOLYGON ((0 0, 2 0, 2 2, 0 2, 0 0))",
                        "tiger/a/non_rectangular\tunion\t1\tPOLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))\tPOLYGON ((0 0, 2 0, 1 2, 0 2, 0 0))",
                        "tiger/a/hole_heavy\tdifference\t1\tPOLYGON ((0 0, 3 0, 3 3, 0 3, 0 0))\tPOLYGON ((1 1, 2 1, 2 2, 1 2, 1 1))",
                        "tiger/a/partial_overlap\txor\t1\tPOLYGON ((0 0, 2 0, 2 2, 0 2, 0 0))\tPOLYGON ((1 1, 3 1, 3 3, 1 3, 1 1))",
                    ]
                ),
                encoding="utf-8",
            )

            report = audit.audit_coverage(
                root, manifest_name="manifest_extended.tsv", wkt_dir_name="wkt_extended"
            )

        self.assertEqual(4, report.total_wkt_cases)
        self.assertEqual(8000, report.sources["tiger"].wkt_cases)
        self.assertLessEqual(
            report.sources["tiger"].inventory_sampling_percent,
            100.0,
        )
        self.assertEqual(1, report.operation_counts["xor"])
        self.assertNotIn("operation_mix", report.missing_dimensions)
        self.assertNotIn("non_rectangle_clip_shapes", report.missing_dimensions)

    def test_counts_normalized_jsonl_verification_and_benchmark_cases(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            manifests_dir = root / "manifests"
            manifests_dir.mkdir()
            (manifests_dir / "sources.csv").write_text(
                "source_id,kind,url,local_path,revision_or_version,sha256,license_id,raw_features,status\n"
                "sample,file,https://example.test/a.geojson,sources/sample/a.geojson,,pending,Test,3,required\n"
                "deferred,file,https://example.test/deferred.geojson,sources/deferred.geojson,,pending,Test,0,deferred\n",
                encoding="utf-8",
            )
            verification_dir = root / "normalized" / "verification"
            benchmark_dir = root / "normalized" / "benchmark"
            verification_dir.mkdir(parents=True)
            benchmark_dir.mkdir(parents=True)
            (verification_dir / "overlay.jsonl").write_text(
                '{"id":"verification-overlay-1"}\n', encoding="utf-8"
            )
            (benchmark_dir / "overlay.jsonl").write_text(
                '{"id":"benchmark-overlay-1"}\n', encoding="utf-8"
            )

            report = audit.audit_coverage(root)

        self.assertEqual(1, report.total_verification_cases)
        self.assertEqual(1, report.total_benchmark_cases)
        self.assertEqual(3, report.total_raw_features)
        self.assertEqual(0, report.total_wkt_cases)
        self.assertNotIn("deferred", report.expand_default_candidates)
        self.assertNotIn("deferred", report.expand_extended_candidates)

    def test_counts_generated_wktcase_and_wktshape_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            manifests_dir = root / "manifests"
            manifests_dir.mkdir()
            (manifests_dir / "sources.csv").write_text(
                "source_id,kind,url,local_path,revision_or_version,sha256,license_id,raw_features,status\n"
                "sample,file,https://example.test/a.geojson,sources/sample/a.geojson,,pending,Test,3,required\n",
                encoding="utf-8",
            )
            generated_dir = root / "normalized" / "generated"
            generated_dir.mkdir(parents=True)
            (generated_dir / "overlay.wktcase").write_text(
                "\n".join(
                    [
                        "[case]",
                        "id=union_case",
                        "tags=generated,sample,hole_heavy",
                        "operation=union",
                        "lhs=POLYGON EMPTY",
                        "rhs=POLYGON EMPTY",
                        "expected=POLYGON EMPTY",
                        "",
                        "[case]",
                        "id=difference_case",
                        "tags=generated,sample,non_rectangular",
                        "operation=difference",
                        "lhs=POLYGON EMPTY",
                        "rhs=POLYGON EMPTY",
                        "expected=POLYGON EMPTY",
                        "",
                        "[case]",
                        "id=xor_case",
                        "tags=generated,sample",
                        "operation=xor",
                        "lhs=POLYGON EMPTY",
                        "rhs=POLYGON EMPTY",
                        "expected=POLYGON EMPTY",
                    ]
                ),
                encoding="ascii",
            )
            (generated_dir / "shape_inputs.wktshape").write_text(
                "\n".join(
                    [
                        "[case]",
                        "id=shape_case",
                        "tags=generated,sample,polygonal",
                        "wkt=POLYGON EMPTY",
                    ]
                ),
                encoding="ascii",
            )

            report = audit.audit_coverage(root)

        self.assertEqual(4, report.total_wkt_cases)
        self.assertEqual(1, report.operation_counts["union"])
        self.assertEqual(1, report.operation_counts["difference"])
        self.assertEqual(1, report.operation_counts["xor"])
        self.assertEqual(1, report.wkt_tags["hole_heavy"])
        self.assertNotIn("operation_mix", report.missing_dimensions)
        self.assertNotIn("non_rectangle_clip_shapes", report.missing_dimensions)
        self.assertNotIn(
            "shapefile_multipolygon_hole_diversity", report.missing_dimensions
        )

    def test_reports_full_profiles_quarantine_and_fetch_failures_separately(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            manifests_dir = root / "manifests"
            (manifests_dir / "fetch-report.jsonl").write_text(
                '{"source_id":"sample","status":"fetched"}\n',
                encoding="utf-8",
            )
            quarantine_dir = root / "quarantine"
            quarantine_dir.mkdir()
            (quarantine_dir / "fetch-failed.jsonl").write_text(
                '{"source_id":"missing","status":"fetch-failed"}\n',
                encoding="utf-8",
            )
            (quarantine_dir / "unsupported.jsonl").write_text(
                '{"id":"unsupported-1"}\n{"id":"unsupported-2"}\n',
                encoding="utf-8",
            )
            (quarantine_dir / "normalize-failed.jsonl").write_text(
                '{"id":"failed-1"}\n',
                encoding="utf-8",
            )
            full_dir = root / "normalized" / "full"
            verification_dir = root / "normalized" / "verification"
            benchmark_dir = root / "normalized" / "benchmark"
            full_dir.mkdir(parents=True)
            verification_dir.mkdir(parents=True)
            benchmark_dir.mkdir(parents=True)
            write_jsonl(
                full_dir / "shape-inputs.jsonl",
                [shape_record("shape-1"), shape_record("shape-2")],
            )
            write_jsonl(
                full_dir / "overlay-candidates.jsonl",
                [{"id": "overlay-1"}, {"id": "overlay-2"}],
            )
            write_jsonl(
                verification_dir / "overlay.jsonl",
                [selected_record("overlay-1", "verification-1")],
            )
            write_jsonl(
                benchmark_dir / "overlay.jsonl",
                [selected_record("overlay-1", "benchmark-1")],
            )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(
                    profile_contract(
                        "overlay",
                        eligibility_file="overlay-candidates.jsonl",
                    )
                ),
            )

        self.assertEqual(4, report.total_full_records)
        self.assertEqual(2, report.total_full_overlay_candidates)
        self.assertEqual(3, report.total_quarantine_records)
        self.assertEqual(1, report.fetch_failed_sources)
        self.assertEqual(50.0, report.unique_verification_coverage_percent)
        self.assertEqual(50.0, report.unique_benchmark_coverage_percent)

    def test_reports_missing_required_verification_and_benchmark_profiles(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            write_sources_manifest(root)
            write_jsonl(
                root / "normalized" / "full" / "shape-inputs.jsonl",
                [shape_record("shape-1")],
            )
            verification_dir = root / "normalized" / "verification"
            benchmark_dir = root / "normalized" / "benchmark"
            verification_dir.mkdir(parents=True)
            benchmark_dir.mkdir(parents=True)
            write_jsonl(
                verification_dir / "overlay.jsonl",
                [selected_record("shape-1", "verification-overlay-1")],
            )
            write_jsonl(
                benchmark_dir / "overlay.jsonl",
                [selected_record("shape-1", "benchmark-overlay-1")],
            )

            report = audit.audit_coverage(
                root,
                contract=coverage_contract(
                    profile_contract("overlay"),
                    profile_contract("rectclip"),
                ),
            )

        self.assertEqual(report.required_profile_counts["overlay"]["verification"], 1)
        self.assertEqual(report.required_profile_counts["overlay"]["benchmark"], 1)
        self.assertIn("verification:rectclip", report.missing_required_profiles)
        self.assertIn("benchmark:rectclip", report.missing_required_profiles)

    def test_fail_on_contract_gaps_returns_nonzero(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            manifests_dir = root / "manifests"
            manifests_dir.mkdir()
            (manifests_dir / "sources.csv").write_text(
                "source_id,kind,url,local_path,revision_or_version,sha256,license_id,raw_features,status\n",
                encoding="utf-8",
            )
            output_json = root / "coverage.json"

            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(audit.__file__).resolve()),
                    str(root),
                    "--output-json",
                    str(output_json),
                    "--fail-on-contract-gaps",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            payload = output_json.read_text(encoding="utf-8")
            self.assertIn('"coverage_admissible": false', payload)
            self.assertIn("contract_errors", payload)
            self.assertIn("missing_required_profiles", payload)
            self.assertIn("verification:overlay", payload)


if __name__ == "__main__":
    unittest.main()
