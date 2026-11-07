"""Mutations against the actual-result protocol and mesh authority."""

import os
import subprocess
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from triangulation_contract_oracle import qualify, validate_record, verify

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.release.run_triangulation_contract_preflight import run


def square():
    return {
        "version": 1,
        "case_index": 0,
        "use_delaunay": False,
        "input": [[(0, 0), (10, 0), (10, 10), (0, 10)]],
        "outer_error": 0,
        "domain_status": 0,
        "triangles": [[(0, 0), (10, 0), (10, 10)], [(0, 0), (10, 10), (0, 10)]],
    }


class Contracts(unittest.TestCase):
    def test_accepts_actual_square(self):
        self.assertEqual(qualify(square())["area2"], 200)

    def test_accepts_omitted_redundant_collinear_source_point(self):
        r = square()
        r["input"] = [[(0, 0), (5, 0), (10, 0), (10, 10), (0, 10)]]
        self.assertEqual(qualify(r)["area2"], 200)

    def test_rejects_omitted_t_contact_even_when_area_matches(self):
        r = square()
        r["input"].append([(5, 10), (7, 12), (3, 12)])
        r["triangles"].append([(5, 10), (7, 12), (3, 12)])
        with self.assertRaisesRegex(AssertionError, "unsplit"):
            qualify(r)

    def test_rejects_seam_point_omitted_on_both_sides(self):
        r = square()
        r["input"] = [
            [(0, 0), (5, 0), (5, 5), (5, 10), (0, 10)],
            [(5, 0), (10, 0), (10, 10), (5, 10), (5, 5)],
        ]
        r["triangles"] = [
            [(0, 0), (5, 0), (5, 10)],
            [(0, 0), (5, 10), (0, 10)],
            [(5, 0), (10, 0), (10, 10)],
            [(5, 0), (10, 10), (5, 10)],
        ]
        with self.assertRaisesRegex(AssertionError, "mandatory contact/seam vertex"):
            qualify(r)

    def test_rejects_area_equal_overlap_and_gap(self):
        r = square()
        r["triangles"][1] = r["triangles"][0]
        with self.assertRaises(AssertionError):
            qualify(r)

    def test_rejects_moved_boundary_even_with_equal_area(self):
        r = square()
        r["triangles"] = [[(x + 1, y) for x, y in t] for t in r["triangles"]]
        with self.assertRaisesRegex(AssertionError, "vertex"):
            qualify(r)

    def test_rejects_added_steiner_vertex(self):
        r = square()
        r["triangles"] = [
            [(0, 0), (10, 0), (5, 5)],
            [(10, 0), (10, 10), (5, 5)],
            [(10, 10), (0, 10), (5, 5)],
            [(0, 10), (0, 0), (5, 5)],
        ]
        with self.assertRaisesRegex(AssertionError, "vertex"):
            qualify(r)

    def test_rejects_seam_erasure_with_equal_region_area(self):
        r = square()
        r["input"] = [
            [(0, 0), (5, 0), (5, 10), (0, 10)],
            [(5, 0), (10, 0), (10, 10), (5, 10)],
        ]
        with self.assertRaises(AssertionError):
            qualify(r)

    def test_hole_cannot_disappear(self):
        r = square()
        r["input"].append([(2, 2), (2, 3), (3, 2)])
        with self.assertRaises(AssertionError):
            qualify(r)

    def test_invalid_domain_requires_typed_rejection(self):
        r = square()
        r["input"].append([(2, 2), (8, 2), (8, 8), (2, 8)])
        with self.assertRaisesRegex(AssertionError, "explicitly"):
            qualify(r)
        r.update(outer_error=12, domain_status=1, triangles=[])
        self.assertEqual(qualify(r)["domain"], "invalid")

    def test_crossing_requires_domain_status(self):
        r = square()
        r["input"] = [[(0, 0), (10, 10), (0, 10), (10, 0)]]
        with self.assertRaisesRegex(AssertionError, "domain"):
            qualify(r)
        r.update(domain_status=3, triangles=[])
        self.assertEqual(qualify(r)["domain"], "crossing")

    def test_missing_duplicate_unknown_or_wrong_count_rejects(self):
        a = "TRI_CONTRACT " + json.dumps(square()) + "\n"
        b = "TRI_LEGACY " + json.dumps(square()) + "\n"
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "records.log"
            for text, count in [
                (a, 1),
                (a + a + b, 1),
                (a + b, 2),
                (a + b + "TRI_BAD {}\n", 1),
            ]:
                p.write_text(text)
                with self.assertRaises(AssertionError):
                    verify(p, count)
            p.write_text(a + b)
            self.assertEqual(verify(p, 1)["status"], "PASS")

    def test_unknown_status_rejects(self):
        for key in ("outer_error", "domain_status"):
            r = square()
            r[key] = 99
            with self.assertRaises(AssertionError):
                validate_record(json.loads(json.dumps(r)))

    def test_timing_rejects_exit_zero_google_benchmark_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "error_benchmark.py"
            fake.write_text(
                "import sys,json,os\nfrom pathlib import Path\n"
                "print('correction_run_id='+os.environ['CLIPPER2NEXT_CORRECTION_RUN_ID'])\n"
                "print('correction_executable='+str(Path(sys.executable).resolve()))\n"
                "print('correction_module_clipper='+str(Path(sys.executable).resolve()))\n"
                "out=next((x.split('=',1)[1] for x in sys.argv if x.startswith('--benchmark_out=')),None)\n"
                "if out:\n"
                " Path(out).write_text(json.dumps({'benchmarks':[{'name':'BM_external_triangulation_next_unprepared/geometry_corpus','error_occurred':True,'error_message':'Invalid request argument'}]}))\n"
                " print('ERROR OCCURRED: Invalid request argument')\n"
                "else:\n"
                f" print('TRI_CONTRACT '+{json.dumps(square())!r})\n"
                f" print('TRI_LEGACY '+{json.dumps(square())!r})\n",
                encoding="utf-8",
            )
            output = root / "evidence"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/release/run_triangulation_contract_preflight.py"),
                    "--producer",
                    sys.executable,
                    f"--producer-argument={fake}",
                    f"--timing-argument={fake}",
                    "--expected-count",
                    "1",
                    "--output-dir",
                    str(output),
                ],
                capture_output=True,
                text=True,
                # This protocol fixture exercises timing-error admission, not a product DLL.
                env={key: value for key, value in os.environ.items()
                     if key != "CLIPPER2NEXT_EXPECTED_PRODUCT"},
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("Invalid request argument", result.stderr)
            self.assertTrue((output / "timing.json").exists())
            self.assertIn(
                "Invalid request argument", (output / "timing.log").read_text()
            )
            self.assertEqual(
                json.loads((output / "timing-result.json").read_text())["status"],
                "FAIL",
            )
            self.assertEqual(
                json.loads((output / "triangulation-contract-result.json").read_text())[
                    "status"
                ],
                "PASS",
            )

    def test_timing_requires_complete_pair_and_cases(self):
        from tools.release.run_triangulation_contract_preflight import (
            TRIANGULATION_TIMING_ROWS,
        )
        from benchmarks.tools.common.external_core_measurement import (
            validate_benchmark_records,
        )

        rows = [
            {
                "name": name,
                "run_type": "iteration",
                "iterations": 1,
                "repetitions": 1,
                "repetition_index": 0,
                "cases": 240,
            }
            for name in TRIANGULATION_TIMING_ROWS
        ]
        validate_benchmark_records(
            {"benchmarks": rows}, TRIANGULATION_TIMING_ROWS, expected_case_count=240
        )
        with self.assertRaises(ValueError):
            validate_benchmark_records(
                {"benchmarks": rows},
                TRIANGULATION_TIMING_ROWS,
                expected_case_count=240,
                expected_repetitions=2,
            )
        invalid = [
            rows[:1],
            [rows[0], rows[0], rows[1]],
            [dict(r, cases=239) for r in rows],
            [dict(r, run_type="aggregate") for r in rows],
            [dict(r, repetitions=2) for r in rows],
            [dict(r, error_message="failed") for r in rows],
        ]
        for mutated in invalid:
            with self.assertRaises(ValueError):
                validate_benchmark_records(
                    {"benchmarks": mutated},
                    TRIANGULATION_TIMING_ROWS,
                    expected_case_count=240,
                )

    def test_optimized_interpreters_cannot_qualify(self):
        modules = [
            ("tests/triangulation", "triangulation_contract_oracle"),
            ("tests/triangulation", "exact_predicate_oracle"),
            ("tests/offset", "offset_contract_oracle"),
            (".", "tools.release.actual_contract_producer"),
        ]
        for directory, module in modules:
            code = f"import sys;sys.path.insert(0,{str(ROOT / directory)!r});__import__({module!r})"
            for option in ("-O", "environment"):
                env = dict(os.environ)
                flags = ["-O"] if option == "-O" else []
                if option == "environment":
                    env["PYTHONOPTIMIZE"] = "1"
                result = subprocess.run(
                    [sys.executable, *flags, "-c", code],
                    capture_output=True,
                    text=True,
                    env=env,
                )
                self.assertNotEqual(
                    result.returncode, 0, (module, option, result.stdout)
                )
                self.assertIn("optimization must be disabled", result.stderr)

    def test_capture_only_removes_stale_pass(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            binary = root / "producer"
            binary.write_text("fixture")
            verdict = root / "triangulation-contract-result.json"
            verdict.write_text('{"status":"PASS"}')
            args = SimpleNamespace(
                output_dir=root,
                producer=binary,
                external=False,
                corpus_root=None,
                producer_argument=[],
                capture_only=True,
                expected_count=1,
            )
            with patch(
                "tools.release.run_triangulation_contract_preflight.capture",
                return_value={},
            ):
                self.assertEqual(run(args), 0)
            self.assertFalse(verdict.exists())


if __name__ == "__main__":
    unittest.main()
