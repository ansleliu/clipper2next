# Geometry Corpus CI Fixture

This directory is the bounded release-gate fixture used by hosted CI.

It intentionally contains only:

- `normalized/verification/*.jsonl`
- `normalized/benchmark/*.jsonl`
- `normalized/full/overlay-candidates.jsonl`
- `normalized/full/source-inventory.jsonl`
- source and license CSV manifests in `manifests/`

Configure oracle tests with the original legacy text inputs as described in the
[test guide](../../../README.md). A full local corpus may live outside the
repository and can contain the much larger `normalized/full/shape-inputs.jsonl`.
Keep this fixture bounded for CI and write fetch reports to the local corpus
or build output.
