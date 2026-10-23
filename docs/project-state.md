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
| DLN-003 recovery delta coverage | 0 | P1 | TODO | |
| DLN-004 no actions through human characters | 1 | P1 | IMPLEMENTED | `180c93b`, kernel tests, build clean; open: live human-key scenarios |
| DLN-005 SkipOptional keeps required trash | 1 | P1 | IMPLEMENTED | `fa2c41b`, kernel tests, build clean; open: ZF with SkipOptional=0/1 |
| DLN-006 missing mandatory door | 1 | P1 | IMPLEMENTED | `57b20d0`, kernel tests, build clean; open: SFK/Deadmines door runs, invalid door entry |
| DLN-007 pause stops motion and door actions | 1 | P1 | IMPLEMENTED | `78cfb57`, kernel tests, build clean; open: pause mid-leg and at a closed door |
| DLN-008 finished run not reopened | 1 | P1 | IMPLEMENTED | `5f21c83`, build clean; owner checkpoint H2 taken as the plan's minimal option (refuse reset); open: held-complete reset/stop live |
| DLN-009 stale queued leadership | 1 | P1 | IMPLEMENTED | `d84caea`, kernel tests, build clean; open: delayed-queue/cancel/group-change scenarios |
| DLN-010 unknown-dungeon override scoped | 1 | P2 | IMPLEMENTED | `285b6f8`, build clean; open: no-session grinding bot control |
| DLN-011 .. DLN-026 | 2-7 | - | TODO | |

Validation:
- Fast suite (`bash tools/run_tests.sh`) after DLN-010: 394 kernel checks, 30 Python tests, all passing.
- Worldserver build (`cmake --build build --target worldserver`, acore-clean, clang 18) after each
  DLN commit: clean, no install between them.
- No live run has been made on a DLN build yet.

Next action: install the DLN build on acore-clean (snapshot VM 107 first, apply
`2026_10_10_00_unresolved_doors.sql`), smoke start/status/stop, then the Phase 1 live scenarios and
DLN-003.
