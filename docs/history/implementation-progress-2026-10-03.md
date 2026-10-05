# Implementation progress - phases 0-16 (2026-10-03)

Historical record moved out of `CLAUDE.md` on 2026-10-05. It describes the state on the day each
phase was finished; statements like "only Wailing Caverns exercised live" or "doors not waited
for" are no longer current - see `CLAUDE.md` (HARDENING PROGRESS, CURRENT STATE) and
`docs/testing-status.md` for that.

## Phase 0 — Baseline

Status: DONE

Commit:
`docs(dungeon-lead): add CLAUDE.md plan and baseline`

Validation:
- Build: PASS - clean rebuild of all module sources, 0 warnings, 0 errors (HEAD fb50d4a)
- Tests: no tests existed before Phase 1

Notes:
- Layout: `src/` (module entry `DungeonLeadModule.cpp`, loader `mod_dungeon_lead_loader.cpp`,
  `DungeonLeadCommandScript.cpp`, config singleton `DungeonLeadConfig.h`, registry access
  `DungeonLeadAccess.h`, overrides `DungeonLeadOverrides.h`), `src/DungeonLead/` (actions,
  strategy/multiplier, triggers, route manager + per-bot session state, canary, test bot pool),
  `conf/mod-dungeon-lead.conf.dist`, `sql/playerbots_dungeon_route.sql`, `data/` + `tools/`
  (route data and validators, CI `.github/workflows/validate-routes.yml`).
- Session ownership: `DungeonRouteMgr::states` (one entry per session), driven by
  `DungeonLead::GuardActiveSessions()` every 2 s from the module's WorldScript::OnUpdate.
- Runtime logs: `Playerbots.log` (`[DungeonLead]`), `Errors.log`, `DungeonLeadSessions.csv`
  (events), `DungeonLeadRuns.csv` (one row per run), `DungeonLeadDebug.log` (debug mode).

Remaining:
- none

---

## Phase 1 — Correctness and lifecycle

Status: DONE (2026-10-03)

Commit:
`fix(dungeon-lead): confirm leadership lifecycle`

Validation:
- Build: PASS (server, clang, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 24 checks (healer alive/dead/ghost/other map/low mana,
  transfer success, queued never confirms -> retry -> give up, handback enqueue failure,
  handback retry succeeds, target gone / third party -> abandon)
- Live: temporary server-only harness (not committed) on a 2-bot group: Starting with
  IsOn()=false until confirmed, acquire confirmed after 166 ms on attempt 1, handback
  confirmed after ~2 s on attempt 1. Harness removed and server redeployed clean.

Notes:
- `DungeonLeadLifecycle {Starting, Active, Stopping}` in `DungeonLeadState`; decisions in
  `DungeonLeadKernel::DecideLeadership` / `EvaluateHealer` (pure, unit-tested).
- Starting applies no strategies; give-up drops the session (run row
  `leadership_not_acquired`). Stopping keeps a minimal entry until handback is observed.
- `DungeonLead::HasSession()` (any stage) replaces `IsOn()` for "busy" checks.
- Config: `LeadershipAcquireTimeoutSeconds`, `LeadershipReturnTimeoutSeconds` (5, range 1-60),
  `LeadershipMaxAttempts` (3, range 1-10).
- Not exercisable live: queue-full and never-confirming transfers (covered by unit tests only).

Remaining:
- none

---

## Phase 2 — DungeonPartyState

Status: DONE (2026-10-03)

Commit:
`refactor(dungeon-lead): single party readiness snapshot`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 46 checks (22 new readiness cases: each status, walk vs
  pull, priority order, no master, no group, offender index)
- Live: temporary server-only harness (not committed): Evaluate() on a real 2-bot group read
  self/healer/alive/map/mana/distance correctly, 3650 yd member -> Fragmented. Removed after.

Notes:
- `src/DungeonLead/DungeonPartyState.{h,cpp}` gathers `DungeonLeadKernel::PartyFacts`;
  `DungeonLeadKernel::EvaluateReadiness` decides (`ReadyStatus`, offender index).
- `DungeonLeadNextAction::isUseful` (walk) and `DungeonLeadMultiplier` (pull + walk) both use
  it; the old per-check helpers (MasterUnavailable, HealerUnavailable, FollowerDead,
  GroupResting, HealerManaLow, MasterTooFar, FindSpreadMember, GroupTooSpread) are gone.
  `GroupInCombat` remains as a thin wrapper for the canary.
- Healer mana now read directly (lowest living healer-role member, GMs ignored, leader
  excluded) - same rule as mod-playerbots' "healer low mana" value, without its cache.
- Not modelled yet (no behavior existed for them): MemberMissing, DifferentMap for non-healers,
  Resurrection in progress.

Remaining:
- none

---

## Phase 3 — DungeonLeadBrain

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): explicit leader state machine`

Validation:
- Build: PASS (server, 0 warnings; full rebuild needed once - see Notes)
- Tests: PASS - `tools/run_tests.sh` 61 checks (15 new: every DecideActive transition,
  PostCombat hold, wipe in/out, Completing, pause, Starting/Stopping not active)
- Live: temporary server-only harness (not committed) on a real bot: none->waiting_ready
  (session_start), KillSelf -> waiting_ready->wipe_recovery (leader_died, wipe #1),
  ResurrectPlayer -> wipe_recovery->waiting_ready (leader_recovered). Removed after.

Notes:
- `DungeonLeadKernel::LeadState` {Starting, WaitingReady, Travelling, Combat, PostCombat,
  WipeRecovery, Completing, Stopping} in `DungeonLeadState::state` is the one authoritative
  state; it replaced Phase 1's `lifecycle` field and the old `tankDeathTs` timestamp.
- `DungeonLeadBrain::TransitionTo` is the only writer (logs + `state_transition` CSV event).
  `DungeonLeadBrain::Update` (from GuardActiveSessions every 2 s and from the route walk's
  isUseful) decides via `DungeonLeadKernel::DecideActive` and owns wipe give-up.
- The route walk only moves in Travelling. Pull gating stays on readiness (Phase 2) until the
  pull controller (Phase 6).
- Not modelled yet (spec names them; added when the behavior exists): MovingToPull/PrePull/
  Pulling (Phase 6), Recovery/RouteRecovery (Phase 11), BossPrep/BossCombat (Phase 14).
- Environment: on 2026-10-03 unattended Ubuntu upgrades touched libc headers, invalidating
  the server's precompiled headers ("file has been modified since the precompiled header").
  Fix: `find build -name '*.pch' -delete` then rebuild (~13 min full worldserver build).
- Harness pitfall: outside an instance the "dungeon lead left instance" trigger stops a
  session on the bot's next AI tick - open-world tests must act within the same tick.

Remaining:
- none

---

## Phase 4 — Typed route nodes

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): typed route objectives`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 71 checks (10 new: kind parsing, anchor-wins, every
  kind -> node type)
- Live: temporary server-only harness (not committed) over the live route table: 96 routes,
  353 walkable steps -> boss 260, pull 76, interaction 44, travel 20, door 14; 0 walkable
  non-anchor Travel, 0 unknown kinds; Wailing Caverns sequence reads correctly. Removed after.

Notes:
- `src/DungeonLead/DungeonRouteTypes.h` (dependency-free): `DungeonRouteKind` + `ParseRouteKind`
  (moved), `DungeonRouteNodeType` {Travel, Pull, TankPosition, SafeSpot, Boss, Door, Interaction,
  Recovery, End}, `ClassifyRouteStep`. Node type is derived (`DungeonRouteStep::NodeType()`),
  so route data/SQL/validator are unchanged.
- `DungeonLeadBrain::CurrentObjective(st)` = destination + intent, derived from state + route
  position (Recovery = `DungeonRoute::RecoveryPoint()` while WipeRecovery, End when Completing
  or past the last step). Shown in every state_transition, `startdungeon status`, and the
  "heading to" chat line.
- SkipOptional now keys on node type Pull (same behavior as the old anchor special case).
- TankPosition/SafeSpot have no route data yet; they are produced once data needs them.

Remaining:
- none

---

## Phase 5 — Pack model

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): pack model and cleared detection`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 82 checks (11 new pack-state cases incl. "reaching the
  spot alone does not clear a live pack", terminal Cleared/Skipped, kill memory)
- Live: temporary server-only harness (not committed): pack built from a real creature
  (Wrekt Warrior, entry 17142) observed found=1 alive=1 -> available; after KillSelf
  found=1 alive=0 -> cleared. Removed after.

Notes:
- `src/DungeonLead/DungeonPack.{h,cpp}`: `DungeonPack` from static route data only (entry +
  position + probe radius, boss/optional flags; id = step index + 1); `DungeonPacks::Observe`
  finds the live creatures each time. Decision: `DungeonLeadKernel::DecidePackState`
  {Unknown, Available, Engaged, Cleared, Skipped}.
- Session: `packId` / `packState` (route-progress fields), changes logged as `pack_state`;
  `startdungeon status` shows the current pack.
- Route walk: a pack node advances only on Cleared (or Skipped by the existing
  StuckSeconds-after-arrival give-up). Travel nodes have no pack and complete on arrival -
  fixes path anchors waiting 45 s and being reported `not_found`/skipped (18 occurrences in
  the run CSV before this change).
- One expected entry per pack (route data has one entry per row). Multi-entry packs need
  route data first.

Remaining:
- none

---

## Phase 6 — DungeonPullController

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): pull controller with bounded retries`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 103 checks (21 new: every DecidePull transition incl.
  marking timeout, initiate/establish timeouts, evade after Established, bounded retry,
  class-AI-opened fight, pack cleared; brain PrePull/Pulling)
- Live: temporary server-only harness (not committed): real bot (lvl 64) + skull on a hostile
  creature 26 yd away + `DoSpecificAction("attack rti target")` -> in combat after 4.0 s
  (initiating->establishing), pack engaged 1 s later (established). Removed after.

Notes:
- `src/DungeonLead/DungeonPullController.{h,cpp}`, decision `DungeonLeadKernel::DecidePull`
  {None, Approaching, WaitingParty, Marking, Initiating, Establishing, Established, Failed}.
  Driven from GuardActiveSessions (2 s) before the brain; session fields pullState /
  pullStateTs / pullAttempts (route progress, reset per step).
- Orchestration only: skull on the pack's first live creature (respecting someone else's live
  mark; owned skull tracked in skullGuid so Stop() clears only ours), then upstream
  "attack rti target". A fight the class AI opens itself (grind) is observed as Established.
- Failure: retried up to PullMaxAttempts, then the pack is Skipped via `DungeonLead::SkipStep`
  (PullPlanning / BossEvade for bosses, ObjectiveTimeout otherwise) with pull_failed +
  pack_skipped events and a chat line - never a silent advance.
- Brain: new LeadState PrePull (Marking) and Pulling (Initiating/Establishing); the route walk
  holds in both.
- Shared route helpers: `DungeonLead::AdvanceStep`, `SkipStep`, `SetPackState` (single writer
  of pack state) replace the walk action's private MarkVisited/SetPackState.
- PullInitiateTimeoutSeconds default 10 (not 5): the live test needed 4.0 s from 26 yd, plus
  the 2 s controller tick. Config: PullRange 30, PullInitiateTimeoutSeconds 10,
  PullEstablishTimeoutSeconds 8, PullMaxAttempts 2.
- In-dungeon live run (2026-10-03, prepared test bots via DungeonTestBotPool - Acquire*TestBot
  logs in offline characters, so no idle online healer is needed): Lord Cobrahn pulled by the
  controller, initiating->established in 2 s, cleared. Lady Anacondra: the order was refused
  (`dist=0 los=0`) because the test party is teleported onto her own route coordinates; follow-up
  fix `fix(dungeon-lead): fail a refused pull order at once` records the refusal facts in
  pull_start and fails the attempt immediately instead of waiting the 10 s timeout.

Remaining:
- none

---

## Phase 7 — Combat anchor and leash

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): combat anchor and chase leash`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 110 checks (5 new ChaseAllowed cases)
- Live: in-dungeon test party in Wailing Caverns (prepared test bots): every fight logged its
  anchor in the state_transition into combat; `leash_hold Kresh dist=55` when Kresh swam off -
  the tank stayed, Kresh was pulled and killed properly afterwards.

Notes:
- Anchor = leader position when the brain enters Combat (session fields anchorSet/anchorX/Y/Z,
  route progress; cleared when the session walks on). No route data has tank positions yet;
  the engagement position is the validated equivalent.
- Containment is compatible with class AI: the leader's DungeonLeadMultiplier (present in its
  combat engine) returns 0 for upstream "reach melee"/"reach spell" when the action's target is
  beyond CombatLeashRadius of the anchor. The tank keeps fighting whatever comes back to it.
- Route movement during unresolved combat was already suppressed by the brain (walk only in
  Travelling); with the leash a fleeing mob can no longer drag the tank into the next pack.
- No active "return to anchor" move: issuing movement from outside the class AI fights its own
  positioning; not chasing is the safe containment.
- Config: CombatLeashRadius 30 (10-80).

Remaining:
- none

---

## Phase 8 — Party cohesion

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): party cohesion levels`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 120 checks (10 new: soft/hard/lost levels, farthest
  offender, hard beats soft, offline = lost, dead elsewhere = dead not lost, master off-map =
  MasterTooFar, soft range holds pulls but not walking)
- Live: Wailing Caverns test party (25 min): a resurrected DPS left 133 yd behind -> walk held
  as "group too spread - Carineva" (HardStop) for the rest of the run, no pull started.

Notes:
- `DungeonLeadKernel::EvaluateCohesion` -> {Ok, SoftWarning, HardStop, LostMember} + offender;
  used by EvaluateReadiness: LostMember -> MemberLost (walk + pull), HardStop -> Fragmented
  (walk + pull), SoftWarning -> PartySpread (pull only).
- PartySoftRange 40, PartyHardRange 90 (= the old leash*1.5 spread limit, validated hard >
  soft). The real player is still held to Leash (60) via MasterTooFar.
- New member fact `online`; a living member on another map/offline is LostMember (the master
  stays MasterTooFar/MasterUnavailable).
- Pull controller's waiting_party transition records the readiness reason and member.
- In combat nothing here abandons the pack: cohesion only gates new pulls and the walk.
- Observed gap (Phase 11): a member who stays beyond follow range (resurrected at the entrance,
  133 yd back) is never regrouped - the leader holds until the session times out. Needs an
  active regroup with timeout/escalation in the recovery controller.

Remaining:
- none (regroup belongs to Phase 11)

---

## Phase 9 — Target manager

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): stable target plan and pull success on the plan`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 132 checks (12 new: priority boss > elite caster > caster >
  elite > normal, deterministic ties, stability while targets live, dead primary replaced,
  kept CC not promoted, empty; pull: primary engaged = established, fight over with pack
  standing = approach again)
- Live: Wailing Caverns test party, 3 runs. Run 1 found that pulls of the boss's trash
  (planned primary) were counted as failed boss pulls -> Lady Anacondra skipped; run 2 found
  that a fight cap counting trash fights skipped her again; run 3 (both fixed): 20 trash fights
  around her, then Lady Anacondra engaged and cleared, route advanced; Kresh pulled and cleared.

Notes:
- `src/DungeonLead/DungeonTargetManager.{h,cpp}` builds candidates (hostiles within 20 yd of the
  current pack's live creature before a pull, units in combat within 40 yd of the leader during
  one), decides with `DungeonLeadKernel::PickTargetPlan`, keeps the plan in the session
  (targetPrimary/Secondary/Cc) and logs `target_plan` changes.
- Marks mirror the plan: skull = primary, cross = secondary, moon = CC (existing CC lifecycle in
  CheckCcMark unchanged). Only icons that are free, on a dead unit, or ours are moved; owned
  cross tracked like skull/moon so Stop() clears only ours.
- Single writer for marks: the old boss-only marking in DungeonLeadMarkAction (and its
  FindCcCandidate) is replaced by a call to the manager; the pull controller no longer marks.
- Pull controller now pulls the plan's primary and counts a pull as established when the pack
  OR the primary is engaged; a fight that ends with the pack standing returns to Approaching
  (progress, not failure). Bounds: failed tries (PullMaxAttempts) and pack resets (the pack
  itself engaged and survived, 3) - trash fights are not capped.
- The route walk's 45 s arrival give-up no longer skips a live pull/boss pack (the controller's
  bounds apply instead); it still covers interactions and empty spots.
- "Healer-type NPC" priority not modelled (no reliable data); casters = mana users.

Remaining:
- none

---

## Phase 10 — Post-combat gate

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): deliberate post-combat gate`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 140 checks (8 new: LowHealth incl. leader, fighting member
  not gated, threshold 0 = off; PostCombat minimum pause, unexpected combat during it)
- Live: Wailing Caverns test party, 2 runs: every fight ended in a logged
  post_combat_decision (pause 4-12 s, lowest health and healer mana recorded). Run 1 found the
  route walk's stuck detector skipping Lady Anacondra ("stuck 196s") because its timer ran
  through 3 min of trash fights - fixed in this commit; run 2: Lady Anacondra and Kresh both
  engaged and cleared, no skips, party on its way to Lord Cobrahn at the cap.

Notes:
- Brain: PostCombat holds for PostCombatMinSeconds (3) before the readiness decision; unexpected
  combat during it goes straight back to Combat. Leaving PostCombat records
  `post_combat_decision` (next state, readiness, lowest health, healer mana, waited ms).
- Readiness: new ReadyStatus::LowHealth (any living same-map member, leader included, out of
  combat, below PostCombatMinHealthPct, default 50, 0 = off); blocks walking and pulls.
  Hostiles / unexpected combat (AnyInCombat), deaths, healer, mana, drinking were already in the
  readiness path; resurrection in progress = MemberDead (a ghost is dead).
- On every entry to Travelling the brain resets the walk's stuck baseline (bestDist/stuckTs/
  stuckAttempts) and the arrival flag, so StuckSeconds measures walking time only.
- Config: PostCombatMinSeconds 3 (0-30), PostCombatMinHealthPct 50 (0-100).

Remaining:
- none

---

## Phase 11 — Recovery controller

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): bounded recovery controller`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 158 checks (18 new: act/escalate/abort windows, never an
  endless loop, readiness -> recovery mapping incl. waits that are not recoveries, brain
  Recovery state, combat interrupts it)
- Live: Wailing Caverns test party, 20 min: the healer died and was returned to the entrance ->
  `recovery_start party_fragmented member=Thyleae`, leader walked back, `recovery_complete`
  after 32 s (the same situation stalled earlier runs for 16-20 min); Lady Anacondra and Kresh
  cleared, party moving on to Cobrahn at the cap. A first attempt was cut short by a test-pool
  start race (characters logged in inside the old instance) - Phase 13 territory.

Notes:
- `DungeonLeadKernel::DecideRecovery` / `RecoveryFor`, `src/DungeonLead/DungeonRecoveryController.{h,cpp}`.
  Reasons: leadership_lost, member_lost, party_fragmented, member_dead. Act ->
  RecoveryTimeoutSeconds (60) -> Escalate -> RecoveryEscalationSeconds (60) -> Abort (Stop, run
  recorded Failed, domain Recovery). The clock runs as long as any recovery problem persists
  (label changes don't reset it); escalation always runs once before an abort.
- Actions: party_fragmented = leader walks back to the straggler after an 8 s grace;
  member_lost/member_dead/leadership_lost = wait. Escalation brings a *bot* member (not real
  player, not selfbot) to the leader; nothing else is escalated. Abort for leadership_lost
  does not try to hand leadership back.
- Ordinary waits (drinking, mana, health, the real player's position/availability) are not
  recoveries and are never timed out here.
- Brain: new LeadState Recovery (while a recovery is open, out of combat); events
  recovery_start / recovery_escalate / recovery_complete / recovery_failed.
- Eligible only in WaitingReady/PostCombat/Recovery with the leader alive; the leader's own
  death stays WipeRecovery (Phase 12).

Remaining:
- none

---

## Phase 12 — Wipe and checkpoints

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): wipe checkpoints and route reconciliation`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 163 checks (5 new ResumeStepAfterWipe cases: no checkpoint,
  skipped steps after the checkpoint reopened, never ahead of the route)
- Live: Wailing Caverns test party with the leader killed 4 min in (harness, not committed):
  wipe_detected -> wipe_recovery, the healer resurrected the leader 14 s later ->
  wipe_recovered -> checkpoint_restore (from=0 to=0, no checkpoint before the first boss) ->
  waiting_ready, route continued at Lady Anacondra.

Notes:
- Checkpoint = `DungeonLeadState::checkpointStep`, set by `DungeonLead::AdvanceStep(st, true)` only
  for confirmed milestones (pack cleared - already_dead - and travel node reached); skips call
  `AdvanceStep(st, false)`. Route-progress field (reset with a new instance).
- `DungeonLead::RestoreCheckpoint` runs when the brain leaves WipeRecovery: resumes at
  `ResumeStepAfterWipe(checkpoint, current)`, reopens steps passed over since (visited cleared,
  removed from the skipped list, a Partial caused only by them is taken back), and resets the
  current step's pack/pull/target/anchor/arrival/stuck state - packs reset on a wipe, so the
  world is re-observed. Steps confirmed killed stay skipped via the instance kill memory.
- Wipe detection and its bounded give-up are the brain's WipeRecovery (Phase 3); regroup after
  the wipe is the recovery controller (Phase 11).
- Not live-exercised: a restore that actually rewinds (needs a skip after a checkpoint and then
  a wipe in the same run) - covered by the kernel tests.

Remaining:
- none

---

## Phase 13 — Test party synchronization

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): start test parties only once assembled`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 170 checks (7 new DecideAssembly cases: requested-not-landed,
  wrong instance, not gathered, dead member, timeout -> abort, never a partial/empty start)
- Live: the exact failure from the Phase 11 run (test characters logged in inside an old
  instance; the session started, the party left to re-enter, the session stopped as "left
  instance") reproduced on purpose: all five left, re-entered instance 1 together, gathered ->
  "party of Farancano assembled ... after 21551 ms - StartSession=true", then an ordinary 8 min
  run. A first attempt aborted at once because a tank in a cross-map teleport is briefly out of
  the world (FindPlayer = null); fixed with FindConnectedPlayer + "in transit = not there yet".

Notes:
- `DungeonLeadKernel::DecideAssembly` / `MemberAssembled`: online, alive, on the dungeon map,
  in the tank's instance, within 30 yd of the tank; Wait / Start / Abort (3 min timeout).
- `RunTestParty` registers a pending start instead of calling StartSession; `TestBotPoolTick`
  (3 s) evaluates and starts it. Releasing a bot drops its pending party. Pending parties count
  in `ActiveCanaryCount()` (via `PendingTestPartyCount()`), so the concurrency cap holds.
- The existing teleport machinery (leader first, followers after it lands, exit-then-enter,
  retries) is unchanged; this phase only stops the session from starting before it worked.

Remaining:
- none

---

## Phase 14 — Boss foundation

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): boss prep and boss combat with a boss anchor`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 176 checks (6 new: marking/pulling a boss -> BossPrep, its
  trash -> Combat, boss engaged -> BossCombat, holds while combat lasts, -> PostCombat)
- Live: Wailing Caverns test party: combat with Lady Anacondra's trash -> `combat->boss_combat
  reason=boss_engaged`, anchor moved to her home spot (boss_combat tank_pos), boss leash held an
  add at 28 yd (leash_hold), she was cleared 16 s later, adds finished, post_combat, route on.
  boss_prep not seen live (that fight began from trash, not a planned boss pull) - kernel-tested.

Notes:
- LeadState BossPrep (Marking/Initiating/Establishing on a boss pack) and BossCombat (in combat
  with the boss pack engaged; holds while combat lasts). TransitionReason boss_engaged.
- `DungeonBossStrategy` {bossEntry, tank position, leash} via `DungeonPacks::BossStrategyFor`:
  generic default = the boss's home position and BossLeashRadius (25). Route data has no
  per-boss positions yet; the struct is where they go when an encounter needs them.
- The pull controller publishes boss facts (bossPackCurrent, bossEngaged, boss tank position)
  each tick; the brain anchors BossCombat at the boss position with its own radius
  (`anchorRadius`, used by the leash multiplier instead of CombatLeashRadius).
- Facing: mod-playerbots' own "tank face" combat strategy (tank specs have it by default; added
  at BossPrep for a non-tank leader). No movement is issued from here - class AI positions.
- Events: boss_prep, boss_combat.

Remaining:
- none

---

## Phase 15 — Telemetry hardening

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): buffered structured telemetry`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 182 checks (6 new TelemetryBuffer cases: capacity, drop
  count, order/target file, drain resets, accepts again)
- Live: Wailing Caverns test party (6 min): every row carries map_id/instance_id/state/step/
  pack_id; 0 dropped lines; the run (start, transitions, fights, post-combat decisions, pulls,
  packs, leash, CC) reconstructs from DungeonLeadSessions.csv alone.

Notes:
- `src/DungeonLead/DungeonTelemetryBuffer.h` (dependency-free, tested): producers (bot AI on map
  threads) only push a formatted line under a short lock - no file I/O, no global lock around
  I/O any more. `DungeonLead::FlushTelemetry` (world thread, from the module's OnUpdate, ~2 s or
  when 1000 lines are queued; forced from WorldScript::OnShutdown) writes all three files with
  one fflush each. Capacity 20000 lines; overflow is logged and written as a telemetry_dropped
  row. Crash durability: up to ~2 s of lines can be lost (accepted trade-off).
- Sessions CSV: columns appended at the end (existing positions unchanged; an existing file
  keeps its old header). Events added: leadership_request / leadership_confirmed /
  leadership_failed (Phase 1's start_failed renamed to leadership_failed).
- Event coverage against the plan's list: session start/stop, leadership_*, state_transition,
  readiness (waiting / post_combat_decision), route (pathing, travel_reached, reached,
  already_dead, route_*), pull_*, pack_*, recovery_*, wipe_*, checkpoint_restore, boss_prep,
  boss_combat, run summary per run in DungeonLeadRuns.csv.

Remaining:
- none

---

## Phase 16 — Cleanup

Status: DONE (2026-10-03)

Commit:
`refactor(dungeon-lead): one leader decision engine`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 184 checks (StateAllowsNewPull)
- Live: Wailing Caverns test party (10 min) after the change: fights opened normally, Lady
  Anacondra and Kresh cleared, no skips, no failed recovery.

Notes:
- Removed the last duplicate decision: DungeonLeadMultiplier no longer gates the route walk
  (the brain decides walking - Travelling only). It now only gates the class AI's own moves:
  the chase leash, and grind's opportunistic pulls by the brain state
  (`DungeonLeadKernel::StateAllowsNewPull`) plus pull readiness.
- Earlier phases already removed: the per-check readiness helpers (Phase 2), the lifecycle
  flag and death timestamp (folded into the brain state, Phase 3), the walk action's private
  MarkVisited/SetPackState and pull marking (Phases 6/9), the boss-only mark logic (Phase 9),
  per-write file I/O under a global mutex (Phase 15). An audit of DungeonLeadState found no
  unused fields; stale names (GroupTooSpread, HealerManaLow, tankDeathTs, MarkVisited,
  FindCcCandidate, FollowerDead) no longer appear outside history notes.
- README "how it runs" rewritten for the current behavior (kept short).

Remaining:
- none

---

## Definition of done - status (2026-10-03)

Autonomous trash leadership (section 37):
1. leadership acquisition confirmed - yes (Phase 1, live)
2. leadership return confirmed or failed safely - yes (Phase 1, live)
3. party readiness centralized - yes (Phase 2)
4. healer unavailability blocks new pulls - yes (Phases 1/2)
5. state machine authoritative - yes (Phases 3/16)
6. route objectives typed - yes (Phase 4)
7. current pack explicit - yes (Phase 5)
8. pull lifecycle explicit - yes (Phase 6)
9. pull failure enters recovery - bounded retry, then the pack is skipped and recorded
   (pull_failed/pack_skipped, Partial for a boss); not routed through the party recovery
   controller (that one is for party problems) - judged sufficient: never silent, bounded
10. combat anchor exists - yes (Phase 7)
11. leash prevents unsafe chain pull - yes (Phase 7, live leash_hold)
12. route does not advance during unresolved combat - yes (walk only in Travelling)
13. pack completion confirmed - yes (Phase 5)
14. post-combat gate exists - yes (Phase 10)
15. dead members trigger recovery - yes (Phase 11)
16. wipe triggers checkpoint logic - yes (Phase 12, live)
17. checkpoint restoration works - yes for the live path (no rewind needed); rewind kernel-tested
18. test-party startup deterministic - yes (Phase 13, live)
19. telemetry explains state transitions - yes (Phase 15)
20. no competing module used - yes
21. no duplicate active leader engine - yes (Phase 16)

Project (section 38) - open items:
- Only Wailing Caverns exercised live (bot-only test party, up to Lord Cobrahn). Other
  dungeons have route data but no live run; a run with a real player in the group hasn't been
  done since these phases.
- Boss foundation is generic (boss home position + leash, upstream tank face); no per-boss data.
- Doors/gated bosses are still not waited for.
- Commits are local; not pushed (left for the user).

---
