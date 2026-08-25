#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
TOOLS_DIR = REPO_ROOT / "benchmarks" / "tools"

from benchmarks.tools.compare import compare_batch_pair_ratios as compare


class BatchPairRatioComparatorTests(unittest.TestCase):
    def test_ratio_regression_without_public_time_regression_is_not_release_blocking(self) -> None:
        self.assertFalse(
            compare.is_release_blocking_ratio_regression(
                ratio_regression_percent=40.0,
                public_time_regression_percent=-25.0,
                max_regression_percent=5.0,
            )
        )

    def test_ratio_and_public_time_regression_is_release_blocking(self) -> None:
        self.assertTrue(
            compare.is_release_blocking_ratio_regression(
                ratio_regression_percent=40.0,
                public_time_regression_percent=12.0,
                max_regression_percent=5.0,
            )
        )


if __name__ == "__main__":
    unittest.main()
