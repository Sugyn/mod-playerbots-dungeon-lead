# Dungeon Lead current state
Plan edition: batch-oriented 2026-10-07 (DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md)
Updated: 2026-10-07T19:30Z, Europe/Prague session
Authorized scope: full hardening pass (Phases 0-8), user-authorized commit/build/test on acore-clean (not push, not install/restart production without separate confirmation)
Module branch / integration SHA / dirty files: main @ 7d10fea, working tree clean
Core / playerbots / compiler / build / SQL / config / maps: acore-clean, /home/prgadm/azerothcore, build/ (CMakeCache 2026-10-02), g++13/clang - see docs/testing-status.md "Real module build" section for manifest detail. build-asan tree is stale (CMakeCache 2026-09-16, predates patch-to-module migration) - do not use it.
Audit baseline: e1c9f8a6fd92e798ed966fe1dfb00cfb2ebc3e23

| Phase | Status | Accepted artifact/evidence | Open required gates |
|---|---|---|---|
| 0 | implementation_ready_live_pending | fb3e734, 92ddf1b - CI expanded, fast baseline recorded, real build verified clean (DL-001/002/005/007/009 commit) | start/stop smoke test on a real server not performed |
| 1 (DL-001) | implementation_ready_live_pending | 0094fa2 - kernel test, real build clean | Deadmines/SFK live pack-identity scenario |
| 2 (DL-002) | implementation_ready_live_pending | caf16e4 - real build clean, manual review (no kernel test possible - AzerothCore-dependent) | live disconnect/kick/disband/late-join scenarios |
| 3 (DL-003) | implementation_ready_live_pending | 842ef50 (worktree work/dl003-dl004, merged 7d10fea) - kernel tests, real build in progress | live master-death/70yd/persistent-wait scenarios |
| 4 (DL-004) | implementation_ready_live_pending | 06249de (same merge) - manual review, real build in progress | live offline-healer/offline-master scenario |
| 5 (DL-008) | implementation_ready_live_pending | a1f4921 (worktree work/dl008, merged 4e6fcea) - 17 new kernel+Python tests, real build clean, validator now 11 errors/29 warnings (honest, undispatched) | 11 unresolved door rows need DB-verified positions or explicit unsupported reclassification (separate from this fix) |
| 6 (DL-006) | active | worker dispatched (work/dl006 worktree), not yet collected | awaiting worker result |
| 7 (DL-010) | not_started | - | depends on Phase 6 landing |
| 8 | not_started | - | depends on 1-7; needs a live human+bot session on acore-clean |

Active phase: 6 (collecting worker result), then checkpoint and recommend a fresh context for Phase 7/8 per section 7.

Current tasks:
- P6-A: DL-006 (use/talk effect confirmation), worktree /home/code/wt-dl006, branch work/dl006, dispatched, awaiting completion notification.
- Background: real-build verification of DL-003/004/008 combined on acore-clean build/ tree (ssh job running, log /tmp/prod_build3.log).

Blockers: none hard; all remaining work is live-runtime verification on acore-clean, which is available but not yet executed for most findings (bot-only/mixed-player campaigns, Phase 8).

Current decisions:
- Used manually-created git worktrees (/home/code/wt-dl00X) instead of Agent tool's built-in isolation, because the session's primary cwd (/home/code/ops) is not a git repo - `isolation: "worktree"` fails there. This satisfies section 3's "separate worktrees" requirement by other means.
- Real-build verification done against acore-clean's `build/` (current, CMakeCache 2026-10-02, matches the running production binary's source lineage) rather than `build-asan` (stale, unrelated link failure against legacy mod-playerbots GM commands - not this module's fault).
- DL-008's validator hardening intentionally introduces 11 CI errors against the shipped CSV (previously-silent unresolved door rows). Not fixed with guessed coordinates, per plan explicit prohibition - left as honest open data gate, named in CHANGELOG.
- CHANGELOG entries kept in the existing `[Unreleased]` section/categories throughout, per user instruction - no new version header created.
- Commits: English only (code, commit messages, CHANGELOG). Chat/status replies to the user: Czech, per user instruction.

Validation:
- Fast suite (`bash tools/run_tests.sh`): 349 kernel checks, 28 Python tests, green at 7d10fea.
- `python3 tools/validate_routes.py`: 438 rows/96 LFG/11 errors/29 warnings (11 errors are DL-008's intended, honest effect - see above).
- Real module build: DL-001/002/005/007/009 confirmed clean on acore-clean `build/` (first pass). DL-008 confirmed clean (second pass). DL-003/004 confirmed clean (third pass, `[100%] Built target worldserver`). All three passes: zero errors, no install/restart performed, running production binary untouched throughout.
- No live server/bot/human scenario executed yet for any finding.

Next action: collect DL-006 worker result (work/dl006), review, integrate, real-build-verify, then checkpoint this file and docs/active-plan.md and recommend the user start a fresh conversation/context for Phase 7 (DL-010) and Phase 8 (live validation), per section 7's "fresh context per phase" guidance - this session has already run far longer than the plan's target.

Completion: not complete. Phases 6/7/8 outstanding; no live runtime evidence collected for any phase yet (section 9.12 requires it).
