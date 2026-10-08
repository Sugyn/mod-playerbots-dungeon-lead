# Dungeon Lead current state
Plan edition: batch-oriented 2026-10-07 (DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md)
Updated: 2026-10-08, Europe/Prague session
Authorized scope: full hardening pass (Phases 0-8), user-authorized commit/build/test on acore-clean (not push, not install/restart production without separate confirmation)
Module branch / integration SHA / dirty files: main @ 6e96f92, working tree clean
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
| 6 (DL-006) | implementation_ready_live_pending | 8c497be - real build clean; no new kernel test (adapter-only fix, documented why) | live refusal/delayed-effect scenario on the 6 confirmed rows; Weegli intentionally left at dispatch-confirm (documented, not a gap) |
| 7 (DL-010) | implementation_ready_live_pending | 6e96f92 - 2 new replay tests, real build clean; no new kernel test (adapter+replay fix, documented why) | live held-complete-then-stop, early-stop, disconnect-mid-handback, disband scenarios |
| 8 | not_started | - | needs a live human+bot session on acore-clean; depends on 1-7 (all now implementation_ready_live_pending) |

Active phase: none. All of Phases 0-7 (every technical finding DL-001 through DL-010, plus the Phase 0 CI/baseline work) are implemented, locally fast-tested, and real-build-verified against acore-clean. Only Phase 8 (live validation) remains, and it requires an actual human+bot session - genuinely live runtime work, not something to simulate or claim from this environment.

Current tasks: none in flight.

Blockers: Phase 8 needs a live human player plus bots on acore-clean (two autonomous mixed-player runs each in RFC/Deadmines/SFK, one controlled interruption/recovery and clean stop in each, plus the eight-dungeon bot-only regression set) - this is real gameplay time, not something I can execute unattended without the user actually playing a character. Flag this to the user before attempting Phase 8.

Current decisions:
- Delegation via spawned background agents proved unreliable in this session: a DL-006 agent was dispatched, ran silently for many hours across a date rollover with no completion notification ever arriving, and turned out to have been stopped (by the environment/user) with zero committed or uncommitted work. I implemented DL-006 and DL-010 directly instead of redelegating, which also turned out faster overall given each finding's small, well-understood scope once DB/source evidence was gathered. **Lesson for Phase 8 or any future project: verify a long-running background agent's actual state directly rather than trusting the absence of a notification as "still working."**
- Used manually-created git worktrees (/home/code/wt-dl00X, all now removed/merged) instead of Agent tool's built-in isolation, because the session's primary cwd (/home/code/ops) is not a git repo.
- Real-build verification done against acore-clean's `build/` (current, matches the running production binary's source lineage) rather than `build-asan` (stale, unrelated to this module). Five passes total, all clean, no install/restart performed, running production binary never touched.
- DL-010: removed the premature `ResetState()`+partial-rebuild inside `Stop()` entirely rather than trying to enumerate every field to carry over - `Stop()` now mutates the existing state in place for the Stopping wait, since `ReconcileLeadership()` already owns the real final erase. Added `RecordRunSummary()` idempotency and Running->Aborted finalization, and a replay-side duplicate/out-of-order lineage check (tools/run_replay/schema.py) as a defensive second layer.
- DL-006: verified all 7 shipped use/talk rows against the real world DB/scripts on acore-clean rather than guessing from route note text. 6 of 7 got a real DB-grounded confirmation contract; Weegli Blastfuse (ZF) intentionally kept at dispatch-confirmation (documented, plan-permitted).
- DL-008's validator hardening intentionally introduces 11 CI errors against the shipped CSV (previously-silent unresolved door rows). Not fixed with guessed coordinates - left as honest open data gate, named in CHANGELOG.
- CHANGELOG entries kept in the existing `[Unreleased]` section/categories throughout, per user instruction - no new version header created.
- Commits: English only (code, commit messages, CHANGELOG). Chat/status replies to the user: Czech, per user instruction.

Validation:
- Fast suite (`bash tools/run_tests.sh`): 349 kernel checks, 30 Python tests, green at 6e96f92.
- `python3 tools/validate_routes.py`: 438 rows/96 LFG/11 errors/29 warnings (11 errors are DL-008's intended, honest effect).
- Real module build: five passes against acore-clean `build/`, covering every commit from DL-001 through DL-010 - all `[100%] Built target worldserver`, zero errors, no install/restart.
- No live server/bot/human scenario executed yet for any finding - this is the one category of evidence genuinely missing across the whole plan.

Next action: Phase 8 - live validation. Requires the user (or someone) to actually play a character alongside bots on acore-clean; cannot be simulated. Before attempting: confirm with the user whether/when they want to do this, since it's real play time on their server, not a background task. If/when ready: follow DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md section 9.6's Phase 8 ("Execution batch P8") and section 9.9's full runtime validation matrix.

Completion: not complete. Phase 8 (live validation) is the sole remaining gate (section 9.12 requires it for every finding).
