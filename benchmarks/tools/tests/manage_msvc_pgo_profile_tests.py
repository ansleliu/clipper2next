#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from benchmarks.tools.runners.manage_msvc_pgo_profile import (
    matching_profile_counts,
    merge_profile_counts,
    prepare_profile_counts,
)


class ManageMsvcPgoProfileTests(unittest.TestCase):
    def test_matching_profile_counts_are_scoped_to_database_stem(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            database = root / "release_profile.pgd"
            first = root / "release_profile!1.pgc"
            second = root / "release_profile!2.pgc"
            unrelated = root / "other_profile!1.pgc"
            for path in (first, second, unrelated):
                path.write_bytes(b"profile")

            self.assertEqual([first, second], matching_profile_counts(root, database))

    def test_prepare_removes_only_owned_profile_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            database = root / "release_profile.pgd"
            matching = root / "release_profile!1.pgc"
            discarded_fixture_profile = (
                root / "release_profile-clipper2next_pgo_fixture_discard!1.pgc"
            )
            unrelated = root / "other_profile!1.pgc"
            for path in (database, matching, discarded_fixture_profile, unrelated):
                path.write_bytes(b"profile")

            prepare_profile_counts(root, database)

            self.assertFalse(database.exists())
            self.assertFalse(matching.exists())
            self.assertFalse(discarded_fixture_profile.exists())
            self.assertTrue(unrelated.exists())

    def test_successful_merge_removes_consumed_and_discarded_profile_counts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            database = root / "release_profile.pgd"
            matching = root / "release_profile!1.pgc"
            discarded_fixture_profile = (
                root / "release_profile-clipper2next_pgo_fixture_discard!1.pgc"
            )
            unrelated = root / "other_profile!1.pgc"
            for path in (database, matching, discarded_fixture_profile, unrelated):
                path.write_bytes(b"profile")

            completed = mock.Mock(returncode=0)
            with mock.patch(
                "benchmarks.tools.runners.manage_msvc_pgo_profile.shutil.which",
                return_value="pgomgr",
            ), mock.patch(
                "benchmarks.tools.runners.manage_msvc_pgo_profile.subprocess.run",
                return_value=completed,
            ):
                merge_profile_counts(root, database)

            self.assertFalse(matching.exists())
            self.assertFalse(discarded_fixture_profile.exists())
            self.assertTrue(unrelated.exists())

    def test_merge_rejects_missing_profile_counts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            database = root / "release_profile.pgd"
            database.write_bytes(b"database")

            with self.assertRaisesRegex(RuntimeError, "no non-empty PGC files"):
                merge_profile_counts(root, database)


if __name__ == "__main__":
    unittest.main()
