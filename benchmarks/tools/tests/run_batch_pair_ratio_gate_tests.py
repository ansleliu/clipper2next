#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"

from benchmarks.tools.runners import run_batch_pair_ratio_gate as gate


class BatchPairRatioGateTests(unittest.TestCase):
    def test_majority_passing_runs_pass_overall_gate(self) -> None:
        rows = [
            {"status": "PASS"},
            {"status": "FAIL"},
            {"status": "PASS"},
        ]

        self.assertEqual(gate.overall_status_for_rows(rows), "PASS")

    def test_without_majority_passing_runs_fails_overall_gate(self) -> None:
        rows = [
            {"status": "PASS"},
            {"status": "FAIL"},
            {"status": "FAIL"},
        ]

        self.assertEqual(gate.overall_status_for_rows(rows), "FAIL")

    def test_required_pair_families_accept_random_execution_order(self) -> None:
        names = [
            "BM_next_batch_public_clip/8_mean",
            "BM_next_batch_scalar/1_mean",
            "BM_next_batch_scalar/64_mean",
            "BM_next_batch_public_clip/1_mean",
            "BM_next_batch_scalar/8_mean",
            "BM_next_batch_public_clip/64_mean",
        ]

        self.assertTrue(gate.has_required_pair_families(names))

    def test_required_pair_families_reject_missing_counterpart(self) -> None:
        names = [
            "BM_next_batch_scalar/1_mean",
            "BM_next_batch_scalar/8_mean",
            "BM_next_batch_scalar/64_mean",
            "BM_next_batch_public_clip/1_mean",
            "BM_next_batch_public_clip/8_mean",
        ]

        self.assertFalse(gate.has_required_pair_families(names))


if __name__ == "__main__":
    unittest.main()
