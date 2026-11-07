# Public triangulation benchmarks

The public supplement compares a frozen baseline with a candidate through
their respective public headers and shared libraries. [fixtures.json](fixtures.json)
defines the geometry inputs. [protocol.json](protocol.json) defines the matched
product, supplement, and core performance gates.

## Build and capture

Build one consumer for each side using the same supplement source. Set
`PUBLIC_HEADERS` to that side's include directory and `PUBLIC_LIBRARY` to its
Windows import library or Linux shared library. On Linux, set `PUBLIC_RUNTIME_DIR`
to the matching runtime directory. On Windows, configure from a developer
terminal with UTF-8 enabled (`chcp 65001`) and place the matching DLL directory
on `PATH` for each run.

```sh
cmake -S benchmarks/public_triangulation -B build/public-candidate -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPUBLIC_HEADERS=/path/to/include \
  -DPUBLIC_LIBRARY=/path/to/libclipper2next.so \
  -DPUBLIC_RUNTIME_DIR=/path/to/runtime
cmake --build build/public-candidate --verbose
ninja -C build/public-candidate -t deps
```

Use the fixture writer to generate the consumer's portable input:

```sh
python -c "from benchmarks.tools.common.public_triangulation import write_fixture_text; write_fixture_text('benchmarks/public_triangulation/fixtures.json', 'build/fixtures.txt')"
build/public-candidate/clipper2next_public_triangulation --fixtures build/fixtures.txt --mode records
```

Records mode captures geometry for the independent integer and rational oracle.
The oracle checks coverage, boundary constraints, holes, and local CDT legality
for the declared cases. A records capture is a prerequisite for matched timing.

## Matched runs

The matched manifest binds the protocol and fixtures, baseline authority, clean
candidate source, consumer builds, actual loaded modules, mathematical records,
worker placement, and cold-kernel captures. Kernel attribution checks the entry
used by each route, including graph execution, direct Sweep projection, and
certified cocircular CDT projection. `--ready-file` and `--go-file` let the
observer check a process before it executes geometry.

```sh
python benchmarks/tools/runners/run_release_performance_suite.py \
  --matched-manifest /path/to/platform-inputs.json \
  --output-dir build/release-evidence
```

The protocol requires seven measured samples per process and seven outer
rounds. Default supplement rows warm up before measurement. Cold rows release
the library's thread-local caches before each timed call. Request copying,
execution, and result destruction are timed together. The runner applies the
declared stability and performance limits to every required pair. Keep raw
samples, diagnostic output, and result summaries in the ignored output
directory. Diagnostic captures and report-only experiments have their own
evidence scope.
