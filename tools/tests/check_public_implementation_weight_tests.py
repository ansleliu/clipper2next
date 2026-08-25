#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_public_implementation_weight as scanner


class PublicImplementationWeightScannerTests(unittest.TestCase):
    def test_default_mode_records_observations_without_findings(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "clipper.h").write_text(
                "namespace details {}\ninline Path64 MakePath(const int* values) { return {}; }\n",
                encoding="utf-8",
            )
            (root / "core.h").write_text(
                "template <typename T>\nPointInPolygonResult PointInPolygon(Point<T>, Path<T>) { return {}; }\n",
                encoding="utf-8",
            )

            result = scanner.scan_public_implementation_weight(root, strict=False)

        self.assertEqual([], result.findings)
        self.assertEqual(2, len(result.observations))

    def test_strict_mode_promotes_observations_to_findings(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "clipper.h").write_text(
                "namespace details {}\ninline Path64 MakePath(const int* values) { return {}; }\n",
                encoding="utf-8",
            )
            (root / "core.h").write_text(
                "template <typename T>\nPointInPolygonResult PointInPolygon(Point<T>, Path<T>) { return {}; }\n",
                encoding="utf-8",
            )

            result = scanner.scan_public_implementation_weight(root, strict=True)

        self.assertEqual(2, len(result.findings))
        self.assertEqual([], result.observations)

    def test_clipper_compat_scope_flags_only_default_clipper_helpers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "clipper.h").write_text(
                "namespace details {}\ninline Path64 MakePath(const int* values) { return {}; }\n",
                encoding="utf-8",
            )
            (root / "core.h").write_text(
                "template <typename T>\nPointInPolygonResult PointInPolygon(Point<T>, Path<T>) { return {}; }\n",
                encoding="utf-8",
            )

            result = scanner.scan_public_implementation_weight(root, strict=True, scope="clipper_compat")

        self.assertEqual(1, len(result.findings))
        self.assertIn("clipper.h", result.findings[0])

    def test_clipper_compat_scope_ignores_core_until_core_phase(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "clipper.h").write_text("Paths64 Intersect();\n", encoding="utf-8")
            (root / "core.h").write_text(
                "template <typename T>\nPointInPolygonResult PointInPolygon(Point<T>, Path<T>) { return {}; }\n",
                encoding="utf-8",
            )

            result = scanner.scan_public_implementation_weight(root, strict=True, scope="clipper_compat")

        self.assertEqual([], result.findings)
        self.assertEqual([], result.observations)

    def test_engine_facade_scope_flags_public_inheritance_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "engine.h").write_text(
                "\n".join(
                    [
                        "class ReuseableDataContainer64 { friend class ClipperBase; };",
                        "class ClipperBase {",
                        "protected:",
                        "  virtual ~ClipperBase();",
                        "};",
                    ]
                ),
                encoding="utf-8",
            )
            (root / "clipper.h").write_text(
                "namespace details {}\ninline Path64 MakePath(const int* values) { return {}; }\n",
                encoding="utf-8",
            )

            result = scanner.scan_public_implementation_weight(root, strict=True, scope="engine_facade")

        self.assertEqual(3, len(result.findings))
        self.assertTrue(all("engine.h" in finding for finding in result.findings))

    def test_strict_core_line_limit_flags_heavy_core_header(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "core.h").write_text("\n".join("// line" for _ in range(4)), encoding="utf-8")

            result = scanner.scan_public_implementation_weight(
                root,
                strict=True,
                scope="core",
                max_core_lines=3,
            )

        self.assertEqual(1, len(result.findings))
        self.assertIn("core.h", result.findings[0])
        self.assertIn("exceeds core header line limit", result.findings[0])


if __name__ == "__main__":
    unittest.main()
