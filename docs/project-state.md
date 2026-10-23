# Dungeon Lead current state
Plan edition: implementation contract from the 2026-10-23 audit (tasks DLN-001..DLN-026, audit baseline `1e2cf6e`)
Updated: 2026-10-23, Europe/Prague session
Authorized scope: implementation, build, install, restart and live runs on acore-clean; push to `main`
Module branch: `main`; audit baseline `1e2cf6e79ef8aa9a5409a3ec36f7b7da841d38bc`
Core / playerbots / compiler / build / SQL / config / maps: see docs/testing-status.md "Baseline manifest (audit 2026-10-23, DLN-001)"
Historical plans (DL-001..DL-011, 2026-10-07/08; Observability V2): history only, superseded by this ledger

Status vocabulary: TODO, IN_PROGRESS, IMPLEMENTED (code, docs and available tests done), VERIFIED
(every task-specific live/integration check executed), BLOCKED (named missing gate).

| Task | Phase | Priority | Status | Evidence / open gate |
|---|---|---|---|---|
| DLN-001 baseline manifest | 0 | P1 | IMPLEMENTED | manifest in testing-status; open: route-table hash and mmap identity as part of run context (DLN-022) |
| DLN-002 integration build | 0 | P1 | IN_PROGRESS | worldserver builds clean on acore-clean `build/` (f19a18799 / 037c0141) for every DLN commit; open: all-class registration probe, start/status/stop smoke, AddClass login classification check (ADR-003) |
| DLN-003 recovery delta coverage | 0 | P1 | IMPLEMENTED | kernels `RecoveryClosesWhenIneligible`, `WalkBackFromEntrance`, `ReopenPathAnchors` + boundary tests; replay: new events leave the reconstruction unchanged, heartbeat is not a campaign record; live: 72 eating/drinking waits none aborted (0740-41f4375); Slave Pens wipe -> `checkpoint_walk_back` 663 yd, route completed, twice (1421-3e3d6d2) |
| DLN-004 no actions through human characters | 1 | P1 | IMPLEMENTED | `180c93b`, kernel tests, build clean; open: live human-key scenarios |
| DLN-005 SkipOptional keeps required trash | 1 | P1 | IMPLEMENTED | `fa2c41b`, kernel tests, build clean; open: ZF with SkipOptional=0/1 |
| DLN-006 missing mandatory door | 1 | P1 | IMPLEMENTED | `57b20d0`, kernel tests, build clean; open: SFK/Deadmines door runs, invalid door entry |
| DLN-007 pause stops motion and door actions | 1 | P1 | IMPLEMENTED | `78cfb57`, kernel tests, build clean; open: pause mid-leg and at a closed door |
| DLN-008 finished run not reopened | 1 | P1 | IMPLEMENTED | `5f21c83`, build clean; owner checkpoint H2 taken as the plan's minimal option (refuse reset); open: held-complete reset/stop live |
| DLN-009 stale queued leadership | 1 | P1 | IMPLEMENTED | `d84caea`, kernel tests, build clean; live: 2 handbacks confirmed through the new operation (campaign 0553-b0993ee); open: delayed-queue/cancel/group-change rejection scenarios |
| DLN-010 unknown-dungeon override scoped | 1 | P2 | IMPLEMENTED | `285b6f8`, build clean; open: no-session grinding bot control |
| DLN-011 accepted path execution | 2 | P1 | IMPLEMENTED | `3e3d6d2`, build clean; evidence: ZF run 1879810038038528 path vs actual; live campaign 1421-3e3d6d2: 7/8 full routes, Greenskin reached, 11 direct_rejected all at one pre-existing bad anchor; open: missing-mmap-tile negative test |
| DLN-012 expired pull hold | 2 | P1 | IMPLEMENTED | `872fa7a`, kernel tests, build clean, not deployed yet; open: staged soft-range straggler and patrol live |
| DLN-015 strategy repair per component | 3 | P2 | IMPLEMENTED | `95f3352`, kernel tests, build clean, not deployed yet; open: injected partial resets live |
| DLN-018 route scope vs dungeon support | 5 | P1 | IMPLEMENTED | `8da1e09`; H5 taken as the plan's minimal option (metadata, no new outcome); 39 full / 41 partial_model / 16 no_mandatory routes; not deployed yet |
| DLN-019 route tooling and SQL equivalence | 5 | P2 | IMPLEMENTED | resolver refuses any non-reproduced row (110 today), `check_route_sql.py` in CI, fresh == upgrade rehearsed on disposable DBs (497 rows); open: heroic-only rows resolved individually (no trustworthy positions yet) |
| DLN-022 run context | 7 | P1 | IMPLEMENTED | `e6a79fb`; live: context `ee4a40e190227422` complete on `dbea4d4` (module, core f19a18799a35, playerbots 037c01418b5d, 497 route rows, 15 config keys, 3780 mmap files); open: a deliberate config/route change detected by compare |
| DLN-024 distinct comparable evidence | 7 | P1 | IMPLEMENTED | `fbca540`; status regenerated - every existing level is now "(pre-context)"; first context-qualified pairs come from campaign 1623-dbea4d4 onward |
| DLN-023 telemetry sink failures | 7 | P2 | IMPLEMENTED | `TelemetrySinkHealth` + kernel tests, heartbeat `lost_lines`, build clean; live: `lost_lines` 0 through campaign 0740-41f4375; open: unwritable/full log destination on staging |
| DLN-013 (known: Slave Pens anchor "path Mennu to Rokmar 13" unreachable), DLN-014, DLN-016, DLN-017, DLN-020, DLN-021, DLN-025, DLN-026 | 2-7 | - | TODO | DLN-021 (mixed human runs) and the human parts of DLN-004/007/008/017/020 have no player available |

Validation:
- Fast suite (`bash tools/run_tests.sh`) after DLN-010: 394 kernel checks, 30 Python tests, all passing.
- Worldserver build (`cmake --build build --target worldserver`, acore-clean, clang 18) after each
  DLN commit: clean, no install between them.
- Live: first bot-only campaign on the DLN build in progress (below); no mixed human+bot run yet.

Deployed 2026-10-23 on acore-clean: build `02fc932` installed (VM 107 snapshot
`pre-dln-phase1-20261023` taken first), `2026_10_10_00_unresolved_doors.sql` applied (route table
now equal to `data/dungeon_routes.csv`, previous table dumped to
`/root/playerbots_dungeon_route.pre-20261023.sql`), worldserver restarted. Campaign
`ValidationAutostart = "24,6,8,24,6,8"` (ZF, Deadmines, SFK twice, bot-only) running; first
observations: four sessions started, SFK cell door opened by the leader's lever, Ashcrombe talk,
courtyard door seen open.

Next action: replay the campaign (`tools/run_replay`), check DLN-004/006/009 evidence in it
(bot key use on Troll Cage and Defias Cannon, door steps, leader handback through
DungeonLeadSetLeaderOperation), then DLN-003.
