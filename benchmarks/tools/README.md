# Benchmark Tools

- `runners/`: benchmark execution, repeated gates, and release suites.
- `gates/`: single benchmark-result admission checks.
- `compare/`: benchmark JSON comparators.
- `evidence/`: release evidence summaries and archive checks.
- `common/`: shared Python helper modules.
- `tests/`: unit tests for benchmark tooling.

Keep script-to-script calls explicit about these subdirectories so moving one
tool family does not silently break another.

Release performance thresholds live in
`common/release_gate_policy.py`. Runners and tests import those constants; CI
workflow commands may still spell out the values so the archived logs show the
admission policy that was used for a run.
