# Active phase batch
Phase / findings / goal: Phase 6, DL-006 - advance/checkpoint a routed use/talk objective only after its declared effect is observed, not on action dispatch alone.
Status: working (worker dispatched, result not yet collected)
Base / integration SHA: 7d10fea (main, after Phase 0-5 merged)
Dependencies: Phase 2 (DL-002 cleanup, merged), Phase 4 (DL-005/007 waits, merged), Phase 5 (DL-008 typed requirement contract, merged) - all accepted inputs for this worker.
Contracts: reuse `FailObjective`/`DecideObjectiveFailure`/`ObjectiveFailureAction` (DL-008's machinery) for bounded retry-then-partial; reuse the door controller's `GetGoState() != GO_STATE_READY` pattern for any use/talk objective whose real postcondition is a different door's state; do not duplicate it.

| Task | Mode/owner | Exclusive writes | Read inputs | Worktree/artifact | Status |
|---|---|---|---|---|---|
| P6-A | implementation, single worker | src/DungeonLead/DungeonLeadActions.cpp, DungeonLeadKernels.h, tests/test_kernels.cpp, CHANGELOG.md (Fixed entry only) | plan Phase 6 section, DL-006 finding text, current WalkUseStep/WalkTalkStep source, 7 use/talk CSV rows (Deadmines gunpowder/cannon, SFK Adamant/Ashcrombe, ZF cage/Weegli/Bly), real world DB on acore-clean for actual mechanic verification | /home/code/wt-dl006, branch work/dl006 | dispatched, awaiting result |

Main-owned shared docs: CHANGELOG.md (Fixed section), docs/testing-status.md, docs/project-state.md, docs/active-plan.md.
Acceptance: per original Phase 6 criteria - no issued-request-alone completion; every supported interaction has observable confirmation + bounded failure; unsupported confirmation contracts diagnosed honestly, not faked; no premature checkpoint.
Validation: worker runs `bash tools/run_tests.sh` + `python3 tools/validate_routes.py` locally; main re-verifies on collection and performs a real-build compile check on acore-clean `build/` (NOT build-asan, stale) before merging to main.
Review: pending (not yet collected).
Correction rounds used: 0.
Integrated artifacts: none yet for this phase.
Open gates/blocker: none - worker in flight, no blocker identified.
Next action: collect P6-A's result when its completion notification arrives (do not poll); review diff against the acceptance criteria above; integrate via `git merge --no-ff work/dl006` onto main; resolve any CHANGELOG conflict (expected, same pattern as Phase 3/5 merges); run fast suite + real-build check; remove the worktree; update docs/project-state.md; then checkpoint and recommend the user open a fresh conversation for Phase 7 (DL-010) and Phase 8 (live validation) per section 7 of the batch-oriented plan edition - this context has run well past the plan's target size for a single phase, let alone three more.
