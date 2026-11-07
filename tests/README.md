# Test Layout

Keep test files close to the behavior they cover. The repository root under
`tests/` should not contain `.cpp` files; use the directories below instead.

## Directories

- `api/`: public API behavior and examples. Do not include headers from
  `src/*/private` here.
- `clip/`: clip module internals that are not engine-specific, including closed
  clip fast paths and Minkowski behavior.
- `clip_engine/`: private clipping engine internals, split by engine topic.
- `core/`: geometry primitives, predicates, scaling, canonicalization, and
  deterministic core helpers.
- `fixture/`, `fuzz/`, `golden/`, `property/`: reusable fixture, fuzz, golden,
  and property-based coverage.
- `offset/`, `rectclip/`, `triangulation/`: module-specific behavior and
  private internals, split by topic instead of catch-all internal files.
- `oracle/`: differential and corpus tests that need the legacy Clipper oracle.
- `support/`: test-only helpers and support component tests. Shared helpers
  should live here before they are copied into more than one test file.

When moving tests, keep `tests/` on the include path and prefer includes rooted
at this directory, for example `#include "support/test_paths.h"`.

## Correction qualification

The authority policy is in `tools/release/release-evidence-contract.json` and
validated by `tools/release/correction_authority.py`. Triangulation records use
`expected.relation=triangulation-exact-contract`, with coverage, constraints,
nonoverlap and optional CDT checks by independent Python int/Fraction oracles.
Preidentified Offset correction records use `offset-correction-contract`, a
fixed approved `contract_id`, and independent analytic absolute expected
geometry. Metamorphic equality alone cannot authorize an Offset correction.
All other records use `strict-legacy-runtime` with zero coordinate/area tolerance.
An approved `cardinal-rounding` case can declare a rounding API difference before
execution: `expected.legacy_semantics_mismatch` fixes `parameter=coordinate_rounding`,
`legacy=nearest_away_from_zero`, `next=nearest_even`, with matching input rounding
and independent `legacy_absolute` as well as current `absolute`. Both engines can
then be valid under their own declared API; report `api-rounding-semantics-mismatch`
instead of inventing a legacy algorithm failure. The declaration and both expected
contracts are authority-digest-bound. Other or undeclared semantic differences fail.
Record schema metadata uses generator version 2; benchmark twins carry the same
authority, expected contract and input/options digests as verification cases.

The input corpus `<geometry-root>/normalized` is read-only. Consumers
accept its geometry parent root. Derive authority metadata without changing
inputs or tolerances with:

```powershell
python tools/corpus/generate_release_profiles.py $env:CLIPPER2NEXT_GEOMETRY_CORPUS_SOURCE_ROOT build/correction-T00/geometry --derive-authority
python tools/checks/check_geometry_corpus_profiles.py build/correction-T00/geometry
```

Oracle configurations also require `CLIPPER2NEXT_LEGACY_CORPUS_SOURCE_ROOT`
to name the geometry parent containing the original five text inputs under
`sources/clipper2-rust/Tests/data`. Configuration freezes a fresh derived vendored
root, copies those text inputs without changing their bytes, and binds both the
oracle and benchmark defaults to that root. Reconfiguration creates a new root;
previous frozen inputs remain intact. These vendored profiles provide no full
external eligibility.

`tools/release/run_correction_verification.py` supplies its separately frozen full
corpus receipt and SHA256 to CMake. The configured scope, root and manifest hash
are recorded in CMakeCache and checked by the runtime collector. A vendored
receipt cannot select full qualification. The collector now requires
`--producer-manifest`; arbitrary old JUnit/profile/list files are not release
evidence. Coverage audit uses the same producer validator.

The derived root includes all original full files byte-for-byte and an
`authority-manifest.json` binding all 36 source files, record counts and every
verification/benchmark case. Full records remain eligibility data and are not
counted as executed verification. Use one explicit corpus root for the run;
set `CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT` to the derived geometry parent root.
Existing Offset corpus records lack independent absolute expected and therefore
remain strict legacy; the profile is never relaxed wholesale.

`check_strict_oracle_equivalence_shape.py --root .` retains the source tolerance
gate; `--qualification` additionally validates controlled case/result evidence.
Qualification schema 1 requires `selection_phase=before-execution`,
`required_profiles`, `cases` and `results`. Results bind case IDs, domain,
contract ID, input/options digests and an `authority_digest` binding the expected
contract; both engines report execution, independent
validity/oracle/checks, representation equality and a classified difference.
`check_external_geometry_corpus_ctest_evidence.py` requires exact PASS JUnit plus
`--qualification`, `--corpus-manifest`, explicitly selected `--source-corpus-root`
and derived `--corpus-root`, and mandatory `--frozen-manifest-sha256` retained by
the caller before execution outside the submitted evidence. Calculate the digest
during the corpus freeze to establish the pre-execution commitment. The gate
verifies each selected source and derived JSONL file against the generator
manifest, including its hash, count, purpose, original inputs and tolerances,
and per-case authority mapping. The manifest's
`source_root` must match the selected source root. For a relocated runner,
derive metadata against that runner's source root and freeze it before execution.
Same-run JUnit/contract/manifest hashes
and complete frozen verification-case mappings are mandatory; skipped, empty,
missing, reclassified and unknown cases fail. Bare legacy JUnit and the current
legacy-only C++ benchmark preflight cannot qualify mathematical corrections.

Optional performance evidence keeps two fixed metric sets:
`required_product_pairs` binds all `product_pairs` (`baseline_ns`,
`candidate_ns`, `cv_percent`); `required_legacy_pairs` binds the existing required
default/unprepared `legacy_pairs` (`legacy_ns`, `candidate_ns`, `cv_percent`).
`max_regression_percent` must be zero for product pairs, candidate latency cannot
exceed its product baseline, and CV must meet the applicable limit in the
evidence contract. Legacy pair and geomean speedups are calculated from measured
times and must remain at least 1.2x. Prepared, borrowed, and transform product
rows use their product baseline.
NOISY, omitted pairs and relaxed thresholds fail;
absent performance evidence makes no performance qualification claim.
