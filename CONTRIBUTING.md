# Contributing

Thanks for helping. This document describes how changes reach `main`.

## Pull requests

- Changes reach `main` only through a pull request. No direct pushes.
- A pull request is merged only with green CI, and always as a **squash merge**: one commit per
  change on `main`.
- The pull request title is a [Conventional Commit](https://www.conventionalcommits.org/) title,
  for example `fix(pull): hold the tank while the party is out of range` or
  `docs: describe the door handling`. Common types: `feat`, `fix`, `refactor`, `perf`, `test`,
  `docs`, `data` (route data), `ci`, `chore`.
- The description says **what** changes, **why**, and **how it was verified** (tests run, a build
  against the pinned revisions, a validation run and its verdict).
- Every user-facing change adds one sentence to `CHANGELOG.md` under `[Unreleased]`, in
  `Added`, `Changed`, `Fixed` or `Removed`.

## Before you open a pull request

```
bash tools/run_tests.sh            # kernel tests (g++) and replay tests
python3 tools/validate_routes.py   # route data
python3 tools/check_route_sql.py   # route CSV == route SQL
python3 tools/check_repo_hygiene.py
```

Behaviour changes need evidence: a run (or better, two) of an affected dungeon judged with
`tools/run_replay`, before and after. Route changes go through `tools/route_insert.py`, which keeps
the CSV, the TSV and the SQL in step and writes a numbered update with its rollback
(`sql/updates/README.md`).

## Code

- The module decides the party's objective; spell rotations and combat stay with mod-playerbots'
  class AI.
- New decisions go into `src/DungeonLead/DungeonLeadKernels.h` as pure functions with tests in
  `tests/test_kernels.cpp`.
- No blocking work, file I/O or database access in an AI tick; every wait and retry is bounded.
- Use only AzerothCore and mod-playerbots APIs that exist in the pinned revisions; do not patch
  mod-playerbots.
- Comments explain *why* in a few lines. Do not put dates, ticket or task ids, run ids or the
  history of a change into code comments - that belongs in the pull request.

## What must never be in the repository

`tools/check_repo_hygiene.py` (run in CI) rejects:

- operational data: host names, user names, server paths, IP addresses, domains, VM ids, backup
  locations, run ids and campaign ids of real runs (test fixtures use synthetic ids);
- internal task or audit ids and date-stamped comments in `src/`, `tests/` and `tools/`;
- tool attribution lines in files (for example "Generated with ..." or `Co-Authored-By:` for a
  tool);
- a `CHANGELOG.md` that leaves the structure above.

Never commit passwords, keys, tokens or database dumps.

## License

By contributing you agree that your contribution is licensed under GPL-2.0-or-later, like the rest
of the project.
