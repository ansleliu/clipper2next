import copy
import json
from pathlib import Path
import tempfile
import unittest

from offset_contract_oracle import absolute_expected, verify
from tools.release.correction_authority import authority_digest

SOURCE = Path(__file__).with_name("offset_contract_cases.jsonl")


class RecordGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.fixture = Path(self.temp.name) / "fixture.jsonl"
        self.records = Path(self.temp.name) / "records.log"
        self.cases = [json.loads(line) for line in SOURCE.read_text().splitlines()]
        self.rows = []
        for index, case in enumerate(self.cases):
            for prefix in ("OFFSET_CONTRACT", "OFFSET_LEGACY"):
                self.rows.append(
                    (
                        prefix,
                        {
                            "case_index": index,
                            "contract_id": case["contract_id"],
                            "inputs": copy.deepcopy(case["inputs"]),
                            "outer_error": "ok",
                            "paths": absolute_expected(
                                case["contract_id"],
                                case["inputs"],
                                prefix == "OFFSET_LEGACY",
                            ),
                        },
                    )
                )

    def run_gate(self):
        self.fixture.write_text("\n".join(map(json.dumps, self.cases)))
        self.records.write_text(
            "\n".join(p + " " + json.dumps(r) for p, r in self.rows)
        )
        return verify(self.fixture, self.records, len(self.cases))

    def test_complete_oriented_boundaries_pass(self):
        self.assertEqual(self.run_gate()["status"], "PASS")

    def test_missing_and_duplicate_engines_fail(self):
        row = self.rows.pop()
        with self.assertRaisesRegex(AssertionError, "missing"):
            self.run_gate()
        self.rows.extend([row, row])
        with self.assertRaisesRegex(AssertionError, "duplicate"):
            self.run_gate()

    def test_actual_input_changes_fail(self):
        self.rows[0][1]["inputs"]["delta"] = 99
        with self.assertRaisesRegex(AssertionError, "actual request"):
            self.run_gate()

    def test_options_digest_changes_fail(self):
        self.cases[0]["canonical_options_digest"] = "0" * 64
        self.cases[0]["authority_digest"] = authority_digest(self.cases[0])
        with self.assertRaisesRegex(AssertionError, "options digest"):
            self.run_gate()

    def test_same_area_wrong_boundary_fails(self):
        index = next(
            i
            for i, (_, r) in enumerate(self.rows)
            if r["contract_id"] == "group-parameter-isolation"
        )
        for point in self.rows[index][1]["paths"][0]:
            point[0] += 1
        with self.assertRaisesRegex(AssertionError, "next violates"):
            self.run_gate()

    def test_wrong_direction_fails(self):
        index = next(i for i, (_, r) in enumerate(self.rows) if r["paths"])
        self.rows[index][1]["paths"][0].reverse()
        with self.assertRaisesRegex(AssertionError, "next violates"):
            self.run_gate()

    def test_legacy_mismatch_expected_is_independent(self):
        case = next(c for c in self.cases if "legacy_absolute" in c["expected"])
        case["expected"]["legacy_absolute"]["paths"] = [[[0, 0], [1, 0], [0, 1]]]
        case["authority_digest"] = authority_digest(case)
        with self.assertRaisesRegex(AssertionError, "legacy absolute"):
            self.run_gate()


if __name__ == "__main__":
    unittest.main()
