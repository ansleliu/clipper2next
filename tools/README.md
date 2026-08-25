# Repository Tools

- `checks/`: repository architecture and release-contract checks.
- `audits/`: reporting-only coverage and readiness audits.
- `corpus/`: external corpus fetch/profile utilities.
- `compare/`: general comparison utilities.
- `runners/`: orchestration scripts that call other tools.
- `tests/`: Python unit tests for the tools.

Keep executable scripts in the topic directory that owns the behavior. The
`tools/` root is only for package metadata and this README.
