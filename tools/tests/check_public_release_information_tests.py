from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.checks import check_public_release_information as check


ROOT = Path(__file__).resolve().parents[2]


class PublicReleaseInformationTests(unittest.TestCase):
    def test_detects_private_network_identity_path_and_secret_shapes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private_url = "http://" + "10" + ".23.4.5/repository"
            internal_name = "lk" + "sense"
            private_path = "C:\\" + "Users\\private-user\\source"
            (root / "source.txt").write_text(
                f"{private_url}\n"
                f"internal brand: {internal_name}\n"
                f"{private_path}\n"
                "PRIVATE_TOKEN=not-a-real-token\n",
                encoding="utf-8",
            )

            categories = {
                finding.category for finding in check.find_findings(root)
            }

            self.assertEqual(
                {
                    "access_token",
                    "internal_identity",
                    "private_ipv4",
                    "private_local_path",
                },
                categories,
            )

    def test_public_repository_tree_contains_no_private_information(self) -> None:
        self.assertEqual([], check.find_findings(ROOT))


if __name__ == "__main__":
    unittest.main()
