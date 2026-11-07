#!/usr/bin/env python3
"""Execute only the configured tracked vendored profiles; never grant release eligibility."""

from pathlib import Path
import sys

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.release.run_triangulation_contract_preflight import main

if __name__ == "__main__":
    raise SystemExit(main(vendored=True))
