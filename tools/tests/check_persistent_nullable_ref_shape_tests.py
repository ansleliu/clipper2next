#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_persistent_nullable_ref_shape as scanner


class PersistentNullableRefShapeScannerTests(unittest.TestCase):
    def test_detects_nullable_ref_fields_and_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            internal = root / "include" / "clipper2next" / "internal"
            internal.mkdir(parents=True)
            (internal / "engine_types.h").write_text(
                "struct active { nullable_ref<active> next; };\nusing active_ref = nullable_ref<active>;\n",
                encoding="utf-8",
            )

            findings = scanner.find_persistent_nullable_ref_findings(root)

        categories = {finding.category for finding in findings}
        self.assertIn("persistent_nullable_ref", categories)
        self.assertIn("nullable_ref_alias", categories)

    def test_ignores_nullable_ref_definition_header(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            internal = root / "include" / "clipper2next" / "internal"
            internal.mkdir(parents=True)
            (internal / "engine_reference.h").write_text(
                "template <class T> class nullable_ref { T* ptr_; };\n",
                encoding="utf-8",
            )

            findings = scanner.find_persistent_nullable_ref_findings(root)

        self.assertEqual([], findings)

    def test_category_filter(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            internal = root / "include" / "clipper2next" / "internal"
            internal.mkdir(parents=True)
            (internal / "rectclip_graph.h").write_text(
                "struct node { nullable_ref<node> next; };\n",
                encoding="utf-8",
            )

            findings = scanner.find_persistent_nullable_ref_findings(root, category="persistent_nullable_ref")

        self.assertEqual(1, len(findings))
        self.assertEqual("persistent_nullable_ref", findings[0].category)


if __name__ == "__main__":
    unittest.main()
