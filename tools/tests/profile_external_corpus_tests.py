#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.corpus import profile_external_corpus as profiler


class ExternalCorpusProfilerTests(unittest.TestCase):
    def test_classifies_small_contained_and_strict_rejected_cases(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            corpus = Path(temp_dir)
            (corpus / "sample.tsv").write_text(
                "\n".join(
                    [
                        "# name\toperation\tscale\tsubject_wkt\tclip_wkt",
                        "sample/a\tintersection\t1\t"
                        "POLYGON ((1 1, 9 1, 9 9, 1 9, 1 1))\t"
                        "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))",
                        "sample/b\tintersection\t1\t"
                        "POLYGON ((1 1, 5 1, 9 1, 9 9, 1 9, 1 1))\t"
                        "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))",
                    ]
                ),
                encoding="utf-8",
            )

            profile = profiler.profile_corpus(corpus)

        self.assertEqual(2, profile.total_cases)
        source = profile.sources["sample"]
        self.assertEqual(2, source.case_count)
        self.assertEqual(1, source.fast_path_eligible)
        self.assertEqual(1, source.strict_small_path_rejected)
        self.assertEqual(2, source.contained_rectangle_cases)

    def test_large_collinear_spike_case_is_profiled_as_large_cleaned_fast_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            corpus = Path(temp_dir)
            points = [f"{index * 10} 0" for index in range(300)]
            points.extend(["2980 0", "2990 0", "2990 1000", "0 1000", "0 0"])
            (corpus / "large.tsv").write_text(
                "\n".join(
                    [
                        "# name\toperation\tscale\tsubject_wkt\tclip_wkt",
                        "large/a\tintersection\t1\t"
                        f"POLYGON (({', '.join(points)}))\t"
                        "POLYGON ((-10 -10, 3000 -10, 3000 1010, -10 1010, -10 -10))",
                    ]
                ),
                encoding="utf-8",
            )

            profile = profiler.profile_corpus(corpus)

        source = profile.sources["large"]
        self.assertEqual(1, source.fast_path_eligible)
        self.assertEqual(1, source.large_relaxed_fast_path_eligible)
        self.assertEqual(1, source.cleaned_path_cases)
        self.assertGreater(source.removable_point_count, 0)


if __name__ == "__main__":
    unittest.main()
