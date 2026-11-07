import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.corpus.generate_release_profiles import derive_offset_contract_source
from tools.release.corpus_authority_manifest import (
    validate_offset_contract_source_manifest,
)
from tools.checks.check_geometry_corpus_profiles import file_sha256


class FormalSourceBindingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source.jsonl"
        self.source.write_bytes(
            (ROOT / "tests/offset/offset_contract_cases.jsonl").read_bytes()
        )
        derive_offset_contract_source(self.source, self.root / "derived")
        self.manifest = self.root / "derived/offset-contract-manifest.json"
        self.cases = self.root / "derived/offset-contract-cases.jsonl"
        self.frozen = file_sha256(self.manifest)

    def validate(self):
        return validate_offset_contract_source_manifest(
            self.manifest, self.source, self.cases, self.frozen
        )

    def test_exact_source_case_set_passes_without_external_credit(self):
        self.assertEqual(len(self.validate()), 36)
        self.assertFalse(
            json.loads(self.manifest.read_text())["external_profile_credit"]
        )

    def test_source_edit_fails(self):
        self.source.write_text(
            self.source.read_text().replace('"delta":2', '"delta":3', 1)
        )
        with self.assertRaises(ValueError):
            self.validate()

    def test_derived_edit_fails(self):
        self.cases.write_text(
            self.cases.read_text() + self.cases.read_text().splitlines()[0] + "\n"
        )
        with self.assertRaisesRegex(ValueError, "derived source bytes"):
            self.validate()

    def test_missing_mapping_and_claiming_external_credit_fail(self):
        raw = json.loads(self.manifest.read_text())
        raw["case_mappings"].pop()
        raw["external_profile_credit"] = True
        self.manifest.write_text(json.dumps(raw))
        # Even if an attacker supplies a fresh digest, actual source ordinals and
        # non-substitution policy are recomputed, never trusted from that file.
        self.frozen = file_sha256(self.manifest)
        with self.assertRaisesRegex(ValueError, "mapping mismatch"):
            self.validate()

    def test_unfrozen_manifest_fails(self):
        self.frozen = "0" * 64
        with self.assertRaisesRegex(ValueError, "freeze"):
            self.validate()


if __name__ == "__main__":
    unittest.main()
