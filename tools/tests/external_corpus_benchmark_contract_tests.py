from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BENCHMARK_SOURCE = ROOT / "benchmarks" / "oracle" / "external_corpus_benchmark.cpp"
BENCHMARK_CMAKE = ROOT / "benchmarks" / "CMakeLists.txt"
ROOT_CMAKE = ROOT / "CMakeLists.txt"
EVIDENCE_CONTRACT = ROOT / "tools" / "release" / "release-evidence-contract.json"


class ExternalCorpusBenchmarkContractTests(unittest.TestCase):
    def test_release_benchmark_uses_only_stable_public_execution_paths(self) -> None:
        source = BENCHMARK_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("/private/", source)
        self.assertNotIn("namespace clipper2next::internal", source)
        for obsolete_diagnostic in (
            "BM_external_next_internal_validated/",
            "BM_external_next_stage_add_paths/",
            "BM_external_next_stage_scanbeam_no_build/",
            "BM_external_next_fast_path_only/",
            "BM_external_next_fast_path_probe/",
            "BM_external_next_scanbeam_breakdown/",
            "BM_external_next_scanbeam_global_schedule_equivalence_probe/",
            "BM_external_rectclip_next_no_cache/",
            "BM_external_rectclip_next_copy_only/",
            "BM_external_offset_next_internal_no_range_scan/",
            "BM_external_offset_next_raw_generation_only/",
            "BM_external_offset_next_raw_then_union/",
            "BM_external_offset_next_raw_then_full_union/",
            "BM_external_offset_direct_guard_probe/",
            "BM_external_offset_raw_equivalence_probe/",
            "BM_external_offset_union_cleanup_breakdown/",
            "BM_external_offset_union_global_schedule_equivalence_probe/",
        ):
            self.assertNotIn(obsolete_diagnostic, source)

    def test_benchmark_consumers_use_current_release_profile_names(self) -> None:
        source = BENCHMARK_SOURCE.read_text(encoding="utf-8")

        for profile in (
            "overlay",
            "rectclip",
            "rectclip-lines",
            "open-path-overlay",
            "offset",
            "triangulation",
            "bounds",
            "minkowski",
            "polytree",
            "clip-tree",
            "batch",
        ):
            self.assertIn(f'"{profile}"', source)

        self.assertNotIn('"open-line-clip"', source)
        self.assertNotIn('"geometry-algorithms"', source)

        for execution_path in (
            "load_open_path_overlay_cases",
            "execute_legacy_open_path_overlay",
            "execute_next_open_path_overlay",
            "load_bounds_cases",
            "execute_legacy_bounds",
            "execute_next_bounds",
        ):
            self.assertIn(execution_path, source)

    def test_batch_evidence_has_dedicated_profile_backed_lanes(self) -> None:
        source = BENCHMARK_SOURCE.read_text(encoding="utf-8")
        contract = json.loads(EVIDENCE_CONTRACT.read_text(encoding="utf-8"))
        algorithms = {algorithm["id"]: algorithm for algorithm in contract["algorithms"]}

        expected_lanes = {
            "BM_external_batch_scalar_legacy/geometry_corpus",
            "BM_external_batch_scalar_next_unprepared/geometry_corpus",
            "BM_external_batch_next_batch/geometry_corpus",
        }
        self.assertEqual(
            expected_lanes,
            set(algorithms["batch_clip"]["required_benchmarks"]),
        )
        self.assertIn('"batch_scalar"', source)
        self.assertIn('"batch_next_batch"', source)

        overlay_lanes = set(algorithms["clip_overlay"]["required_benchmarks"])
        self.assertTrue(expected_lanes.isdisjoint(overlay_lanes))

    def test_optimized_benchmark_exposes_full_contract_preflight(self) -> None:
        source = BENCHMARK_SOURCE.read_text(encoding="utf-8")
        cmake = BENCHMARK_CMAKE.read_text(encoding="utf-8")

        self.assertIn('"--clipper2next_verify_legacy"', source)
        self.assertIn("verify_all_benchmark_profiles_against_legacy", source)
        self.assertIn("assert_open_paths_exactly_equal", source)
        self.assertIn("assert_poly_tree_semantically_equal", source)
        self.assertIn("int main(int argc, char** argv)", source)

        external_target = cmake.split(
            "clipper2next_add_external_corpus_benchmark(", maxsplit=1
        )[1].split(")", maxsplit=1)[0]
        self.assertIn("clipper2next_bench_external_corpus", external_target)
        self.assertNotIn("benchmark_main", external_target)

    def test_pgo_training_discards_fixture_loading_and_trains_current_paths(self) -> None:
        source = BENCHMARK_SOURCE.read_text(encoding="utf-8")
        cmake = ROOT_CMAKE.read_text(encoding="utf-8")

        self.assertIn('"--clipper2next_train_pgo"', source)
        self.assertIn("PgoAutoSweep", source)
        self.assertIn("train_current_benchmark_profiles", source)
        self.assertIn("minimum_profile_training_time", source)
        self.assertIn("std::chrono::steady_clock", source)
        self.assertIn("CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED", source)
        self.assertIn("CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED", cmake)
        self.assertIn("pgobootrun", cmake)
        self.assertRegex(
            source,
            r"#if defined\(CLIPPER2NEXT_MSVC_PGO_INSTRUMENTED\)\s+"
            r"struct current_training_profiles",
        )


if __name__ == "__main__":
    unittest.main()
