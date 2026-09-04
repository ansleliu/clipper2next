import tempfile
import unittest
from pathlib import Path

from conanfile import normalize_exported_text


class ConanRecipeTests(unittest.TestCase):
    def test_exported_text_is_canonical_lf_on_every_platform(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.cpp"
            notice = root / "NOTICE.md"
            binary = root / "fixture.bin"
            source.write_bytes(b"first\r\nsecond\r\n")
            notice.write_bytes(b"already\ncanonical\n")
            binary.write_bytes(b"\x00\r\n\xff")

            normalize_exported_text(str(root))

            self.assertEqual(b"first\nsecond\n", source.read_bytes())
            self.assertEqual(b"already\ncanonical\n", notice.read_bytes())
            self.assertEqual(b"\x00\r\n\xff", binary.read_bytes())


if __name__ == "__main__":
    unittest.main()
