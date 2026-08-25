#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from tools.checks import check_public_header_shape as scanner


class PublicHeaderShapeScannerTests(unittest.TestCase):
    def write_required_request_headers(self, root: Path) -> None:
        for relative in scanner.REQUIRED_PUBLIC_HEADERS:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("#pragma once\n", encoding="utf-8")

    def test_detects_cross_facade_public_includes_and_aggregate_request_ownership(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write_required_request_headers(root)
            (root / "api").mkdir(parents=True)
            (root / "offset").mkdir(parents=True)
            (root / "clip.h").write_text(
                '\n'.join(
                    [
                        '#include "clipper2next/offset/engine.h"',
                        '#include "clipper2next/rectclip/clip.h"',
                    ]
                ),
                encoding="utf-8",
            )
            (root / "offset" / "engine.h").write_text(
                '#include "clipper2next/api/requests.h"\n',
                encoding="utf-8",
            )
            (root / "api" / "requests.h").write_text(
                "struct clip_request64 final {};\nstruct offset_request64 final {};\n",
                encoding="utf-8",
            )

            findings = scanner.scan_public_header_shape(root)

        joined = "\n".join(findings)
        self.assertIn("clip facade must not include offset facade", joined)
        self.assertIn("clip facade must not include rectclip facade", joined)
        self.assertIn("offset/engine.h is obsolete", joined)
        self.assertIn("aggregate request header owns module request type", joined)

    def test_detects_obsolete_public_headers_and_missing_request_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for relative in scanner.FORBIDDEN_PUBLIC_HEADERS:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n", encoding="utf-8")

            findings = scanner.scan_public_header_shape(root)

        joined = "\n".join(findings)
        self.assertIn("missing required public header", joined)
        self.assertIn("obsolete public header", joined)
        self.assertIn("api/requests.h is an obsolete aggregate header", joined)
        self.assertIn("clip/engine.h is obsolete", joined)
        self.assertIn("core/rect_algorithms.h is obsolete", joined)
        self.assertIn("offset/engine.h is obsolete", joined)
        self.assertIn("triangulation/engine.h is obsolete", joined)

    def test_detects_direct_public_header_include_cycles(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write_required_request_headers(root)
            (root / "core").mkdir(parents=True)
            (root / "core" / "rect.h").write_text(
                '#include "clipper2next/core/rect_algorithms.h"\n',
                encoding="utf-8",
            )
            (root / "core" / "rect_algorithms.h").write_text(
                '#include "clipper2next/core/rect.h"\n',
                encoding="utf-8",
            )

            findings = scanner.scan_public_header_shape(root)

        joined = "\n".join(findings)
        self.assertIn("public header include cycle", joined)
        self.assertIn("core/rect.h <-> core/rect_algorithms.h", joined)

    def test_accepts_split_request_and_facade_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            self.write_required_request_headers(root)
            (root / "api").mkdir(parents=True)
            (root / "clip").mkdir(parents=True)
            (root / "offset").mkdir(parents=True)
            (root / "clip.h").write_text(
                '#include "clipper2next/clip/request.h"\n',
                encoding="utf-8",
            )
            (root / "clip" / "request.h").write_text("struct clip_request64 final {};\n", encoding="utf-8")
            (root / "offset" / "request.h").write_text("struct offset_request64 final {};\n", encoding="utf-8")

            findings = scanner.scan_public_header_shape(root)

        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()
