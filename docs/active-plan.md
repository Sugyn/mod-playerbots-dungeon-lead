# Active phase batch
Phase / findings / goal: none active. Phases 0-7 (DL-001 through DL-010) all closed at commit 6e96f92. Next up: Phase 8 - validate the candidate with real players and update support evidence.
Status: not started (blocked on live play time, not a technical blocker)
Base / integration SHA: main @ 6e96f92
Dependencies: Phases 1-7 all implementation_ready_live_pending (accepted artifacts, fast-tested, real-build-verified) - Phase 8's own inputs are satisfied on the implementation side. What's missing is the live evidence itself.
Contracts: n/a for Phase 8 (it's evidence collection, not a code contract) - see DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md section 9.6 Phase 8 and section 9.9 for the exact runtime validation matrix to execute.

| Task | Mode/owner | Exclusive writes | Read inputs | Worktree/artifact | Status |
|---|---|---|---|---|---|
| (none dispatched - needs the user's live play time first) | | | | | |

Main-owned shared docs: CHANGELOG.md (Added entry for DL-011 once Phase 8 evidence exists), docs/testing-status.md, docs/project-state.md, docs/active-plan.md.
Acceptance: per DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md section 9.6 Phase 8 - two autonomous mixed-player runs each in RFC/Deadmines/SFK (one human + four bots, no tactical commands), one controlled interruption/recovery and clean stop in each, plus the eight-dungeon bot-only regression set (Deadmines, RFC, RFD, SM Armory/Cathedral/Library, SFK, ZF) on the final candidate SHA.
Validation: not started - this is real gameplay time, not a background/automated task.
Review: n/a.
Correction rounds used: 0.
Integrated artifacts: none for this phase.
Open gates/blocker: needs the user to actually play a character (or direct someone who will) alongside bots on acore-clean. Ask before scheduling/attempting this - it's live server time, not something to run unattended.
Next action: ask the user whether/when they want to run Phase 8's live validation. Until then, there is no further implementation work pending in this plan - all 11 findings are code-complete and real-build-verified.
