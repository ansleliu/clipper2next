from __future__ import annotations

import argparse
from pathlib import Path


FORBIDDEN_FILES = (
    "src/support/private/parallel_for.h",
    "src/offset/private/offset_execution_policy.h",
)

FORBIDDEN_TOKENS = (
    "CLIPPER2NEXT_ENABLE_BATCH_PARALLEL",
    "CLIPPER2NEXT_ENABLE_OFFSET_GROUP_PARALLEL",
    "CLIPPER2NEXT_USE_BATCH_PARALLEL",
    "CLIPPER2NEXT_USE_OFFSET_GROUP_PARALLEL",
    "shared_parallel_thread_pool",
    "parallel_thread_pool",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    remaining_files = [
        name for name in FORBIDDEN_FILES if (root / name).exists()
    ]
    violations: list[str] = []
    for path in (
        root / "CMakeLists.txt",
        root / "src",
        root / "include",
        root / "tests",
        root / "benchmarks",
    ):
        candidates = [path] if path.is_file() else path.rglob("*")
        for candidate in candidates:
            if not candidate.is_file() or candidate.suffix not in {
                ".cpp", ".h", ".hpp", ".cmake", ".txt"
            }:
                continue
            content = candidate.read_text(encoding="utf-8", errors="ignore")
            for token in FORBIDDEN_TOKENS:
                if token in content:
                    violations.append(
                        f"{candidate.relative_to(root).as_posix()}: {token}"
                    )

    if remaining_files or violations:
        details = remaining_files + violations
        raise SystemExit(
            "clipper2next must not own a background thread pool:\n"
            + "\n".join(details)
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
