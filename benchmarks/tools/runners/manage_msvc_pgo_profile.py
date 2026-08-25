#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def matching_profile_counts(profile_dir: Path, database: Path) -> list[Path]:
    pattern = f"{database.stem}!*.pgc"
    return sorted(
        path
        for path in profile_dir.glob(pattern)
        if path.is_file() and path.stat().st_size > 0
    )


def owned_profile_count_artifacts(profile_dir: Path, database: Path) -> list[Path]:
    patterns = (
        f"{database.stem}!*.pgc",
        f"{database.stem}-clipper2next_pgo_fixture_discard!*.pgc",
    )
    return sorted(
        {
            path
            for pattern in patterns
            for path in profile_dir.glob(pattern)
            if path.is_file()
        }
    )


def prepare_profile_counts(profile_dir: Path, database: Path) -> None:
    for path in owned_profile_count_artifacts(profile_dir, database):
        path.unlink()
    if database.exists():
        database.unlink()


def merge_profile_counts(
    profile_dir: Path,
    database: Path,
    *,
    pgomgr: str | None = None,
) -> None:
    profile_counts = matching_profile_counts(profile_dir, database)
    if not profile_counts:
        raise RuntimeError(
            f"no non-empty PGC files match {database.stem}!*.pgc in {profile_dir}"
        )
    if not database.is_file():
        raise RuntimeError(f"PGD does not exist after instrumented link: {database}")

    executable = pgomgr or shutil.which("pgomgr")
    if not executable:
        raise RuntimeError("pgomgr is not available in the MSVC environment")

    command = [executable, "/merge", *(str(path) for path in profile_counts), str(database)]
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"pgomgr /merge exited with {completed.returncode}")

    summary = subprocess.run([executable, "/summary", str(database)], check=False)
    if summary.returncode != 0:
        raise RuntimeError(f"pgomgr /summary exited with {summary.returncode}")

    for path in owned_profile_count_artifacts(profile_dir, database):
        path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("prepare", "merge"))
    parser.add_argument("--profile-dir", required=True)
    parser.add_argument("--database", required=True)
    args = parser.parse_args()

    profile_dir = Path(args.profile_dir).resolve()
    database = Path(args.database).resolve()
    try:
        if args.action == "prepare":
            prepare_profile_counts(profile_dir, database)
        else:
            merge_profile_counts(profile_dir, database)
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
