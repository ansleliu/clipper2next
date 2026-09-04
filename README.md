# clipper2next

clipper2next is a C++23 library for robust integer polygon geometry. It provides
polygon clipping, topology construction, offsetting, rectangle clipping,
triangulation, Minkowski operations, geometric predicates, and path transforms
through a compact CMake package.

The production library has no required third-party dependencies. Public owners
use `std::vector`, checked entry points use `std::expected`, and high-throughput
workloads can use shared GeoTypes views, prepared requests, borrowed input
ranges, flat path sets, and streaming topology output.

## Requirements

- CMake 3.24 or newer
- Ninja
- A C++23 compiler and standard library with `std::expected`

The project is tested on Windows with MSVC and on Linux with GCC 13.

Version 5.0.0 intentionally starts a new ABI generation for the complete
offset-stage statistics contract. The shared library therefore uses SONAME 5;
4.x consumers must rebuild, and no dual ABI or compatibility shim is shipped.

## Build

A production build does not require vcpkg or any geometry backend:

```sh
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCLIPPER2NEXT_TESTS=OFF \
  -DCLIPPER2NEXT_BENCHMARKS=OFF
cmake --build build/release
cmake --install build/release --prefix <install-prefix>
```

On Windows, run these commands from a developer environment for the selected
MSVC toolchain. Windows and Linux builds use Ninja as the supported generator.

The repository also provides CMake presets for product, oracle, sanitizer,
fuzzing, and benchmark configurations. Oracle targets use the original
Clipper2 package only as a test-time differential reference; it is never a
production dependency.

## Conan

Create a local package with:

```sh
conan create . -s build_type=Release --build=missing
```

The package exports `clipper2next::clipper2next` and the header-only shared type
contract `clipper2next::geotypes`.

## CMake integration

```cmake
find_package(clipper2next CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE clipper2next::clipper2next)
target_compile_features(my_target PRIVATE cxx_std_23)
```

Include the complete public surface with:

```cpp
#include <clipper2next/clipper.h>
```

Narrower module headers such as `<clipper2next/clip.h>` and
`<clipper2next/offset.h>` are available when preferred.

## Quick start

```cpp
#include <clipper2next/clip.h>

namespace next = clipper2next;

int main() {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::EvenOdd;
    request.subjects = {{{0, 0}, {80, 0}, {80, 80}, {0, 80}}};
    request.clips = {{{20, 20}, {60, 20}, {60, 60}, {20, 60}}};

    const auto result = next::clip_checked(request);
    if (!result) {
        return 1;
    }

    return result->closed.empty() ? 1 : 0;
}
```

`Path64` and `Paths64` store signed 64-bit integer coordinates. Floating-point
paths are available where the operation has an explicit scaling contract,
including triangulation, Minkowski operations, and geometry utilities.

## API overview

- Boolean clipping: intersection, union, difference, and XOR for closed and
  open paths, with EvenOdd, NonZero, Positive, and Negative fill rules.
- Topology: polygon-tree results and a streaming shell/hole sink.
- Offsetting: miter, square, bevel, and round joins; polygon and open-path end
  types; constant or callback-driven delta; legacy-compatible `arc_tolerance`.
- Rectangle clipping: polygons and polylines, including prepared immutable
  inputs for repeated queries.
- Triangulation: integer and scaled floating-point input, with optional
  Delaunay legalization.
- Minkowski sum and difference.
- Geometry utilities: area, bounds, orientation, point-in-polygon and related
  predicates, transforms, translation, and scaling.
- Batch and prepared execution for repeated workloads.

Most result-producing operations also provide an `*_into` form so callers can
reuse result storage.

## Checked operations

Checked APIs return `clipper_result<T>`, an alias of
`std::expected<T, clipper_error_code>`. They distinguish valid empty geometry
from invalid coordinates, invalid precision or scale, resource-limit failures,
allocation failures, and borrowed-input or output-sink failures.

Checked entry points cover clipping, topology output, offsetting, rectangle
clipping, Minkowski operations, and triangulation. Unchecked entry points are
available when the caller has already established the input contract.

## Memory and zero-copy integration

Public owning path and result containers use `std::vector`; allocator ownership
does not cross the public ABI. The `clipper2next::geotypes` component defines the
shared point, descriptor, path-view, and topology-view contract used for direct
interoperation with downstream geometry owners.

`borrow_paths64` accepts an lvalue random-access outer range whose inner ranges
are sized and expose integral `x` and `y` members. It borrows the source
collection without first converting it to `Paths64`. The collection must remain
alive and unchanged until execution returns.

For clipping, `clip_topology_checked` writes polygon, shell, and hole records
directly to caller-provided final spans. For offsetting, `offset_stage_checked`
avoids an owning input-collection conversion and returns a `path_set64` backed
by one point pool plus descriptors. Both APIs accept explicit path, point,
output, and staging-workspace limits and report write/allocation statistics.

## Performance configuration

`Point64` is always the 16-byte GeoTypes XY value. Per-point payloads belong in
parallel application-owned storage rather than in the geometry-kernel ABI,
which keeps point traffic compact and enables vectorized containment paths on
supported CPUs.

Parallel batch and large-offset execution accept an explicit synchronous,
non-owning `sync_bulk_executor_ref`. The application retains ownership of
threads, arenas, affinity, NUMA placement, and the global concurrency budget;
clipper2next retains ownership of admission, grain, deterministic result order,
and failure cleanup. Callers should provide parallel execution only after
measuring the target workload.

Some operations retain bounded thread-local scratch capacity. Long-lived worker
threads may call `clipper2next::release_thread_caches()` after a geometry burst
to release that calling thread's cached working storage.

## Performance evidence

Performance results are accepted only after exact differential comparison with
legacy Clipper2. The final external-corpus preflight covered 11 normalized
profiles and 2,426 cases. Integer coordinates, vertex counts, path direction,
winding, and topology were compared without coordinate tolerance.

The 5.0.0 release gates below compare the current public API with legacy
Clipper2 on the same external benchmark profiles. Windows used MSVC 19.44 with
`/O2 /Ob2 /DNDEBUG`; Linux used GCC 13.1 with `-O3 -DNDEBUG`. Both used Google
Benchmark 1.9.5, seven repetitions of at least 0.5 s, pairwise process
isolation, randomized pair order, a 1 s warmup, and a 5% CV ceiling. Every
required default/unprepared pair must exceed 1.2x; passing the geometric mean
alone is not sufficient. Linux is the canonical E3 result: all variance and
speedup gates passed, with a **1.615x** 14-pair geometric mean. Two independent
Windows runs also passed every speedup gate, but unrelated benchmarks moved
above the CV ceiling between runs; its **1.758x** result is directional E2,
not a variance-qualified E3 claim.

| Algorithm family | Windows vs legacy | Linux vs legacy |
| --- | ---: | ---: |
| Generic closed overlay | **1.636x** | **1.746x** |
| Intersection | **1.487x** | **1.584x** |
| Union | **1.545x** | **1.669x** |
| Difference | **1.522x** | **1.694x** |
| XOR | **1.630x** | **1.715x** |
| RectClip, unprepared | **1.383x** | **1.247x** |
| Open-line RectClip, unprepared | **4.287x** | **1.790x** |
| Open-path overlay, unprepared | **1.688x** | **1.527x** |
| Offset, unprepared | **1.563x** | **1.770x** |
| Triangulation, unprepared | **2.878x** | **2.340x** |
| Minkowski, unprepared | **1.465x** | **1.454x** |
| PolyTree, unprepared | **1.532x** | **1.434x** |
| ClipTree, unprepared | **1.415x** | **1.456x** |
| Batch-profile scalar calls, unprepared | **2.016x** | **1.434x** |

Large offset groups use the injected executor only when the caller can supply
16 workers and the input has at least 512 paths and 524,288 normalized points.
On the Linux release runner, a 512-path/524,288-point end-to-end offset improved
by **1.315x** at that admission point. Executor limits 2/4/8 stayed on the
serial path and measured between 1.003x and 1.008x, avoiding unprofitable nested
parallelism.

These are canonical non-PGO release results. PGO is a separate artifact scope:
it is required only when a PGO artifact is published, and that artifact must
independently preserve the same per-pair and variance gates. PGO results never
replace the canonical release gate.

The following Windows measurements compare the final Release product with the
pre-refactor clipper2next binary built from the same source baseline. Both used
MSVC `/O2 /Ob2 /DNDEBUG`, Google Benchmark 1.9.5, one benchmark thread, warmup,
fixed processor affinity, and alternating baseline/candidate order. The
topology row is a predeclared four-affinity stratified result over 20 pairs;
offset rows use six paired runs. Latencies are marginal medians, while the
conclusion uses the paired geometric-mean ratio to resist frequency drift.

| Algorithm and workload | Pre-refactor median | Current median | Paired result | Outcome |
| --- | ---: | ---: | ---: | ---: |
| Borrowed topology union, 256 subject groups | 1.358 ms | 1.278 ms | -3.71% latency; 15/20 pairs faster | **1.039x faster** |
| Polygon offset, miter join, 64 paths | 246.021 us | 243.521 us | +0.08% latency | parity |
| Polygon offset, round join, 64 paths | 701.653 us | 373.114 us | -46.43% latency; 6/6 pairs faster | **1.867x faster** |

The direct topology writer also removes an intermediate point-copy pass. For
the 256-group workload, the pre-refactor sink wrote 2,048 points to ring
scratch and then 2,048 points to final storage. The current writer performs the
same 2,048 final writes with zero ring-scratch point writes, eliminating 32 KiB
of intermediate point-write traffic per operation because `Point64` is 16
bytes. Both variants recorded zero input-collection writes and zero steady-state
staging reallocations.

| Topology write counter, 256 groups | Pre-refactor | Current |
| --- | ---: | ---: |
| Input-collection point writes | 0 | 0 |
| Engine-input point writes | 2,048 | 2,048 |
| Intermediate ring-scratch point writes | 2,048 | 0 |
| Final point writes | 2,048 | 2,048 |
| Steady-state staging reallocations | 0 | 0 |

These are workload-specific controlled measurements, not universal latency or
RSS claims. Standalone process RSS was not isolated for this clipper2next
comparison, so no memory-capacity number is invented. Raw JSON remains outside
Git by policy.

## Testing

The product test suite uses GoogleTest. Differential oracle tests compare
observable results with Clipper2 and remain isolated from the installed
production target. Google Benchmark, sanitizer, ThreadSanitizer, fuzz-smoke, and
install-consumer configurations are provided by the repository presets.

Oracle tests use the bounded normalized corpus stored in the repository by
default. A larger normalized corpus can override it through
`CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT`:

```sh
CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT=/path/to/geometry \
  ctest --preset linux-gcc-oracle --output-on-failure
```

Corpus tests are therefore exercised rather than skipped in a default source
build. Release evidence additionally rejects any required corpus test reported
as skipped.

## License

clipper2next is distributed under the Boost Software License 1.0. See
[`LICENSE_1_0.txt`](LICENSE_1_0.txt) and [`NOTICE.md`](NOTICE.md).
