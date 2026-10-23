# Active phase batch
Phase / tasks / goal: Phase 1 (DLN-004..DLN-010) - correctness and lifecycle boundary fixes
Status: implemented, live validation pending
Base / integration SHA: audit baseline `1e2cf6e`; fixes `fa2c41b`..`5f21c83` on `main`
Dependencies: DLN-002 (integration build) - build part done for every commit; registration probe and smoke test open

| Task | Exclusive writes | Status |
|---|---|---|
| DLN-005 | DungeonRouteTypes.h, skip loop in DungeonLeadActions.cpp, conf | implemented |
| DLN-006 | DungeonRouteMgr.{h,cpp} door spawn, WalkDoorStep, DecideMissingDoor | implemented |
| DLN-004 | IsBotActor, ChooseKeyUser, WalkUseStep, FindOpener | implemented |
| DLN-007 | pause command, isUseful, InteractionMayAct | implemented |
| DLN-010 | DungeonLeadOverrides.h | implemented |
| DLN-009 | DungeonLeadSetLeaderOperation, BeginLeadershipTransfer, DecideQueuedLeaderChange | implemented |
| DLN-008 | reset command, Execute guard | implemented |

Acceptance (phase gate): every task's automated and staging criteria in the plan; no human-side
action and no mandatory false success in the live scenarios.
Validation: kernel suite 394/0, Python 30/30, worldserver build clean per commit.
Open gates: install + smoke on acore-clean; live scenarios per task (project-state.md); mixed
human+bot scenarios need a player client.
Next action: deploy the Phase 1 build, smoke test, run the bot-only scenarios that need no human.
