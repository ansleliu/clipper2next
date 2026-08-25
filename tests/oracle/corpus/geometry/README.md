# Geometry Corpus CI Fixture

This directory is the bounded release-gate fixture used by hosted CI.

It intentionally contains only:

- `normalized/verification/*.jsonl`
- `normalized/benchmark/*.jsonl`
- `normalized/full/overlay-candidates.jsonl`
- `normalized/full/source-inventory.jsonl`
- `manifests/*.csv|*.jsonl`
- the legacy Clipper2 text corpora required by oracle tests

The full local corpus may live outside the repository, for example under
`/data/clipper2next/geometry`, and can contain the much larger
`normalized/full/shape-inputs.jsonl`. Keep this fixture small enough for pull
request CI while preserving every release-gated profile.
