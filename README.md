# clipper2next

clipper2next is a C++23 library for robust integer polygon geometry. It provides
polygon clipping, topology construction, offsetting, rectangle clipping,
triangulation, Minkowski operations, geometric predicates, and path transforms
through a compact CMake package.

The production library has no required third-party dependencies. Public owners
use `std::vector`, checked entry points use `std::expected`, and high-throughput
workloads can use shared GeoTypes views, prepared requests, borrowed input
ranges, flat path sets, and streaming topology output.

## Version and compatibility

This source snapshot is the **7.0.1 candidate**. Full release qualification
remains pending.

The shared library uses ABI generation 7 (SONAME 7 on Linux). The borrowed-path
measurement callback changed since 6.x. Rebuild consumers using the matching 7.x
headers and library. Prepared requests retain an owned snapshot of their inputs.

The geometry implementation uses exact integer predicates for triangulation.
Certified cocircular quadrilaterals preserve the selected diagonal and ordered
triangle output. Checked Offset validates input, generated coordinates, and
workspace limits. Rectangle clipping accepts owning requests and reusable
prepared inputs.

## Requirements

- CMake 3.24 or newer
- Ninja
- A C++23 compiler and standard library with `std::expected`

Use MSVC on Windows or GCC 13 or newer on Linux.

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

Public owning path and result containers use `std::vector`. Use a compatible
C++ runtime across the library and application. The `clipper2next::geotypes`
component defines the shared point, descriptor, path-view, and topology-view
contract for interoperation with downstream geometry owners.

`borrow_paths64` accepts an lvalue random-access outer range whose inner ranges
are sized and expose integral `x` and `y` members. It borrows the source
collection without first converting it to `Paths64`. The collection must remain
alive and unchanged until execution returns.

For clipping, `clip_topology_checked` writes polygon, shell, and hole records
directly to caller-provided final spans. For offsetting, `offset_stage_checked`
avoids an owning input-collection conversion and returns a `path_set64` backed
by one point pool plus descriptors. Both APIs accept explicit path, point,
output, and staging-workspace limits and report write/allocation statistics.
Workspace limits account for live library-owned storage, including buffers that
overlap during cleanup and publication. They measure logical usage rather than
process RSS.

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

## Tests and release verification

The product tests use GoogleTest. Oracle configurations compare geometry with
Clipper2 and use the checked-in normalized profiles together with the original
legacy text corpus. Point CMake at the legacy geometry parent directory at
configure time:

```sh
cmake --preset linux-gcc-oracle \
  -DCLIPPER2NEXT_LEGACY_CORPUS_SOURCE_ROOT=/path/to/geometry
cmake --build --preset linux-gcc-oracle
ctest --preset linux-gcc-oracle --output-on-failure
```

Triangulation is verified for exact coverage, constraints, non-overlap, and
local Delaunay legality where requested. Predeclared Offset corrections use
independent expected geometry. Other comparison domains use strict legacy
equivalence. See [the test guide](tests/README.md) and the
[public triangulation benchmark guide](benchmarks/public_triangulation/README.md).

The repository presets cover product, oracle, sanitizer, fuzz-smoke, and
benchmark configurations. Release qualification binds checks and measurements
to the selected source and binaries. Keep generated reports and raw results
in an ignored output directory such as `build/release-evidence`.

## License

clipper2next is distributed under the Boost Software License 1.0. See
[`LICENSE_1_0.txt`](LICENSE_1_0.txt) and [`NOTICE.md`](NOTICE.md).
