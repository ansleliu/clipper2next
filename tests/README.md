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
