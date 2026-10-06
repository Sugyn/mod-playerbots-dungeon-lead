# Hardening H1-H7 (2026-10-04/05)

Historical record (2026-10-06). It describes the state when each round was finished; current
validation is in `docs/testing-status.md`, releases in `CHANGELOG.md`.

# HARDENING PROGRESS

Plan: internal hardening plan (2026-10-04, not in the repository). Its release numbers (v0.6.0-beta.1 ...)
collide with existing tags (v0.6.0-v0.10.0-alpha). Agreed with the user 2026-10-04: continue the
existing numbering, and keep `-alpha` until the CHANGELOG's own `-beta` bar (most non-event
dungeons live-verified) is met. First hardening release: v0.11.0-alpha (H1-H3).

## H1 — Recovery timer on reason change

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): reset recovery timeout on reason change`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 194 checks (10 new: fragmented 20 s -> lost restarts from 0, dead -> fragmented
  independent window, same reason keeps its clock, flapping bounded by the episode cap, clocks
  cleared when the problem is gone)
- Live: Wailing Caverns test party 12 min, no regression (Anacondra, Kresh cleared); no
  recovery occurred in that run.

Notes:
- `DungeonLeadKernel::RecoveryTimers` {reason, step, reasonSince, episodeSince} replaces the
  three loose session fields; `ObserveRecovery` restarts the reason clock and step on a reason
  change, `DecideRecovery(reason, msInReason, msInEpisode)` keeps per-reason act/escalate/abort
  and adds an episode cap (2 x (act + escalate)) so flapping between reasons stays bounded -
  the property the old shared clock gave.
- The plan's example reasons PullFailed/RouteLost/HealerDead don't exist as recovery reasons
  here (pull failure is the pull controller's, a dead healer is member_dead); tests use the
  existing reasons.
- recovery_start now records the previous reason (`after=`).

## H2 — Combat target selection on the combat anchor

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): anchor combat target selection`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 199 checks (5 new IsFightCandidate cases: tank at anchor, neighbour near the
  moved tank but 50 yd from the anchor rejected, pack member inside the radius kept, add
  attacking the party kept wherever it stands, idle mob near the anchor rejected)
- Live: Wailing Caverns test party 12 min: plans built from 1-3 candidates, pulls/CC as before,
  no skip or recovery failure.

Notes:
- DungeonTargetManager in combat measures from the session's combat anchor (`anchorX/Y/Z`, or
  the boss spot in BossCombat) and falls back to the tank position only without an anchor.
- `DungeonLeadKernel::IsFightCandidate`: in combat AND (within 40 yd of the anchor OR attacking
  a party member). Same boundary as the leash; pack identity hardening follows in H4.

## H3 — Mandatory objective failure policy

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): enforce mandatory objective failure policy`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 211 checks (12 new: requirement classification, optional skipped after 1-3
  rounds, required/boss retried then aborted, a boss/required objective never skipped)
- Live: Wailing Caverns test party with two forced failures on Lady Anacondra (harness, not
  committed): round 1 -> objective_retry, session kept; round 2 -> objective_failed, session
  stopped, run row `partial / pull_planning / boss_evade / objective_failed`.

Notes:
- `DungeonObjectiveRequirement` {Optional, Required, Boss} in DungeonRouteTypes.h, derived from
  existing data (`DungeonRouteStep::Requirement()`): a `boss` row is Boss, everything else
  Optional; Required exists for future route data (no row marks trash as required yet).
- `DungeonLeadKernel::DecideObjectiveFailure` -> Skip (optional only) / Retry / Abort, rounds =
  ObjectiveRetryRounds (2). `DungeonLead::FailObjective` is the single entry for every
  "can't complete this step" path: pull attempts exhausted, pack reset too often, arrival
  give-up (stuck_alive/not_found), route walk stuck (path). Retry = `ResetStepState` (fresh
  observation, same step); Abort = run Partial + objective_failed + run summary + Stop. The
  route never advances past an unresolved boss.
- objective_skipped replaces pack_skipped.

## H4 — Pack identity

Status: DONE (2026-10-04) - dense-room live validation pending (H7 Tier 2)

Commit:
`refactor(dungeon-lead): strengthen runtime pack identity`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 220 checks (9 new ResolvePack cases: two same-entry packs 15 yd apart not
  merged, lock on engage, add joins, neighbour that joins counts but its idle friends don't,
  locked members dead -> cleared, a boss's trash is an add not the boss, patrol lead, empty)
- Live: Wailing Caverns test party 14 min (debug on): Lady Anacondra resolved as 1 member with
  the trash around her as adds (up to 7), locked on engage, cleared; Kresh the same; no
  neighbouring group merged, no objective failure.

Notes:
- `DungeonLeadKernel::ResolvePack`: core members = the nearest expected unit (lead) + expected
  units whose spawn (home) position is within 12 yd of the pack spot + expected units attacking
  the party; once engaged the core is locked (`DungeonLeadState::packLocked`); any other unit
  attacking a party member is an add. The pack's observation (found/alive/engaged -> state) uses
  the core only - fighting a boss's trash must not look like fighting the boss.
- `DungeonPacks::Observe` gathers the expected entries (grid search) plus all creatures attacking
  party members (`Unit::getAttackers`); `DungeonLead::TrackPack` = observe + state + lock + debug
  `pack_resolution` (members, adds, rejected, reason) and is shared by the route walk and the
  pull controller.
- Dense-room behaviour (two same-entry groups) is kernel-tested; WC routes are single named
  units, so the live run exercised the add/lock path, not the rejection path - Tier 2 dungeons
  in H7 will.

## H5 — Interaction controller (doors)

Status: DONE (2026-10-04) - door wait not yet triggered live (see Notes)

Commit:
`feat(dungeon-lead): add interaction controller`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - 231 checks (11 new DecideInteraction cases: door already open, closed -> wait,
  opens after the event -> complete, never opens -> failed, target missing/vanished,
  act -> wait for confirmation, world-confirmed completion)
- Live: Shadowfang Keep test party (level 22, 20 min): Rethilgore and Razorclaw cleared, the
  Courtyard door (opened by its event after Rethilgore) passed; the door wait itself did not
  trigger because the event had already opened it when the leader got there.

Notes:
- `src/DungeonLead/DungeonInteractionController.{h,cpp}`, decision
  `DungeonLeadKernel::DecideInteraction` {None, Resolving, WaitingPrerequisite, Interacting,
  WaitingConfirmation, Complete, Failed}; only type so far: Door.
- Data-free: route `door` rows have no entry/position, so the door is found where the walk gets
  stuck - when MoveRouteTo is about to give up, a closed (GO_STATE_READY) GAMEOBJECT_TYPE_DOOR
  within 15 yd of the leader and closer to the destination starts a door interaction instead of
  failing the step. The leader holds; success only when the world shows the door open; after
  DoorWaitSeconds (120) it fails through `FailObjective` ("door_closed"/"door_gone").
- The leader never operates doors (dungeon doors are event/key/boss gated). Elevator / NPC /
  game-object interactions are not implemented - no route data needs them yet (plan: only what
  routes require).
- Events: interaction_state; `startdungeon status` shows "waiting at door".

## H6 — Combined failure paths

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): harden recovery objective reconciliation`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 233 checks (2 new: recovery during a door wait doesn't use the door's budget;
  back at the door its own time still runs out)
- Live: covered by the H7 runs on this build.

Review of the plan's scenarios against the code:
- pull fails -> retry: FailObjective Retry (fresh round, step re-observed) - live in H3.
- mandatory fails permanently -> abort/partial: FailObjective Abort - live in H3.
- party fragments during an interaction: FIXED - the door's clock (`interactionActiveMs`,
  `AdvanceInteractionClock`) now only runs while the leader is holding at it (Travelling), so a
  recovery elsewhere no longer times the door out; the interaction itself is preserved.
- wipe near an interaction: RestoreCheckpoint -> ResetStepState clears it; it is found again
  from the world (closed door) when the walk gets stuck there.
- boss evade: Cleared only when locked core members are dead; a reset counts toward the reset
  bound and ends in FailObjective (retry, then abort) - no false completion.
- leadership lost during recovery: Observe() checks leadership first, so leadership_lost takes
  over the recovery (fresh window, then abort without handback).

Stale state: AdvanceStep now also clears the target plan; pack/lock, pull, objective count,
interaction, anchor (on Travelling) were already cleared per step, and ResetStepState covers a
retry or a checkpoint restore.

## H7 — Live validation campaign

Status: DONE for the first validation set (2026-10-05); more dungeons remain untested

Harness: was a server-only `DungeonLeadSelfTest.inc` during H7; now in the module as
`.dungeonlead validate <lfgId...>` (`DungeonValidationCampaign`), dungeon sets in
`tools/live_validation/validation_set.tsv`, results via `tools/summarize_runs.py` (with ALERT lines
for H7's invariants). Matrix: `docs/testing-status.md`.

Passes: 11 dungeons x 25 min, then 7 dungeons x 60 min (test parties stop at 45 min).
Completed routes: Ragefire Chasm, SM Graveyard, SM Library, Razorfen Downs. Deadmines to VanCleef,
Shadowfang Keep to Fenrus, Wailing Caverns to Pythas, Razorfen Kraul to Jargba within 45 min.
No objective/recovery failure in the final 60-min pass.

Failure classes found and fixed (all pushed):
- session bots leaving the dungeon (upstream "new rpg" teleports, other teleports) ->
  `fix(dungeon-lead): keep session bots inside the dungeon` (PlayerScript teleport guard)
- per-tick homebind teleport after the test characters' old group disbanded (AC marks the instance
  invalid) -> `fix(dungeon-lead): keep party members' instance valid`
- a straggler stuck on terrain re-opening the same recovery with fresh clocks forever ->
  `fix(dungeon-lead): treat a relapsing recovery as one episode`
- escalation brought only the farthest straggler, the next one ran the recovery out ->
  `fix(dungeon-lead): escalate every straggler, not only the farthest`
- marking timeouts not counted as pull attempts (unbounded), boss out of sight counted as in
  range, marking while still in combat -> `fix(dungeon-lead): bound marking failures and pull only
  in sight`
- tank inside the rock / under the floor (SM Library, Deadmines): the regroup walk-back was a
  straight MovePoint -> `fix(dungeon-lead): regroup walk-back only along a real path`, plus a
  bounded move back to the last good spot (`leader_unstuck`, seen once in WC, worked)

Open:
- SM Armory, Cathedral, Zul'Farrak: only 25-min runs (no boss reached); the rest of the route
  table has no live run.
- Pace: ~30 s per trash pack.
- Trash between bosses is fought where the walk meets it (route rows are bosses).
