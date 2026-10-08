# Dungeon Lead current state
Plan edition: batch-oriented 2026-10-07 (DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md)
Updated: 2026-10-08, Europe/Prague session
Authorized scope: full hardening pass (Phases 0-8), user-authorized commit/build/test on acore-clean (not push, not install/restart production without separate confirmation)
Module branch / integration SHA / dirty files: main @ 8c497be, working tree clean
Core / playerbots / compiler / build / SQL / config / maps: acore-clean, /home/prgadm/azerothcore, build/ (CMakeCache 2026-10-02), g++13/clang - see docs/testing-status.md "Real module build" section for manifest detail. build-asan tree is stale (CMakeCache 2026-09-16, predates patch-to-module migration) - do not use it.
Audit baseline: e1c9f8a6fd92e798ed966fe1dfb00cfb2ebc3e23

| Phase | Status | Accepted artifact/evidence | Open required gates |
|---|---|---|---|
| 0 | implementation_ready_live_pending | fb3e734, 92ddf1b - CI expanded, fast baseline recorded, real build verified clean | start/stop smoke test on a real server not performed |
| 1 (DL-001) | implementation_ready_live_pending | 0094fa2 - kernel test, real build clean | Deadmines/SFK live pack-identity scenario |
| 2 (DL-002) | implementation_ready_live_pending | caf16e4 - real build clean, manual review (no kernel test possible - AzerothCore-dependent) | live disconnect/kick/disband/late-join scenarios |
| 3 (DL-003) | implementation_ready_live_pending | 842ef50 (merged 7d10fea) - kernel tests, real build clean | live master-death/70yd/persistent-wait scenarios |
| 4 (DL-004) | implementation_ready_live_pending | 06249de (same merge) - manual review, real build clean | live offline-healer/offline-master scenario |
| 5 (DL-008) | implementation_ready_live_pending | a1f4921 (merged 4e6fcea) - 17 new kernel+Python tests, real build clean, validator now 11 errors/29 warnings (honest, undispatched) | 11 unresolved door rows need DB-verified positions or explicit unsupported reclassification (separate from this fix) |
| 6 (DL-006) | implementation_ready_live_pending | 8c497be - real build clean; no new kernel test (adapter-only fix, documented why) | live refusal/delayed-effect scenario on the 6 confirmed rows (Gunpowder, Cannon, Troll Cage, Adamant, Ashcrombe, Bly); Weegli intentionally left at dispatch-confirm (documented, not a gap to close here) |
| 7 (DL-010) | not_started | - | depends on Phase 6 (now landed) |
| 8 | not_started | - | depends on 1-7; needs a live human+bot session on acore-clean |

Active phase: none (Phase 6 just closed). Next: Phase 7 (DL-010), recommended in a fresh conversation per section 7.

Current tasks: none in flight.

Blockers: none hard; all remaining work is live-runtime verification on acore-clean, which is available but not yet executed for any finding (bot-only/mixed-player campaigns, Phase 8), plus Phase 7 (DL-010) implementation.

Current decisions:
- Delegation via spawned background agents proved unreliable in this session: a DL-006 agent was dispatched, ran silently for many hours across a date rollover with no completion notification ever arriving, and turned out to have been stopped (by the environment/user) with zero committed or uncommitted work - "Agent a1b052... was stopped by the user and won't be resumed" on status check. I implemented DL-006 directly instead of redelegating. **Lesson for future phases in this project: verify a long-running background agent's actual worktree state directly (git status/log) rather than trusting the absence of a notification as "still working."**
- Used manually-created git worktrees (/home/code/wt-dl00X) instead of Agent tool's built-in isolation, because the session's primary cwd (/home/code/ops) is not a git repo - `isolation: "worktree"` fails there.
- Real-build verification done against acore-clean's `build/` (current, CMakeCache 2026-10-02, matches the running production binary's source lineage) rather than `build-asan` (stale, unrelated link failure against legacy mod-playerbots GM commands - not this module's fault). Four passes so far, all clean, no install/restart performed, running production binary never touched.
- DL-006: verified all 7 shipped use/talk rows against the real world DB/scripts on acore-clean (gameobject_template, event_scripts, instance_script source) rather than guessing from route note text. 6 of 7 got a real DB-grounded confirmation contract; Weegli Blastfuse (ZF) intentionally kept at dispatch-confirmation - his gossip starts a multi-step scripted escort sequence with no single observable flag, and the plan explicitly allows "confirm activation only" for that case.
- DL-008's validator hardening intentionally introduces 11 CI errors against the shipped CSV (previously-silent unresolved door rows). Not fixed with guessed coordinates, per plan explicit prohibition - left as honest open data gate, named in CHANGELOG.
- CHANGELOG entries kept in the existing `[Unreleased]` section/categories throughout, per user instruction - no new version header created.
- Commits: English only (code, commit messages, CHANGELOG). Chat/status replies to the user: Czech, per user instruction.

Validation:
- Fast suite (`bash tools/run_tests.sh`): 349 kernel checks, 28 Python tests, green at 8c497be.
- `python3 tools/validate_routes.py`: 438 rows/96 LFG/11 errors/29 warnings (11 errors are DL-008's intended, honest effect, unchanged by DL-006).
- Real module build: four passes against acore-clean `build/`, covering DL-001/002/005/007/009, DL-008, DL-003/004, and DL-006 - all `[100%] Built target worldserver`, zero errors, no install/restart.
- No live server/bot/human scenario executed yet for any finding.

Next action: Phase 7 (DL-010 - preserve run lineage and truthful terminal outcomes). Per section 7 of the batch-oriented plan edition, this is a good boundary to start a fresh conversation/context - this session has run far past the plan's one-phase-per-context target. Read this file and docs/active-plan.md (will be rewritten for Phase 7) first on resume; do not reconstruct state from old conversation history.

Completion: not complete. Phases 7/8 outstanding; no live runtime evidence collected for any phase yet (section 9.12 requires it).
