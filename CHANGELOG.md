# Changelog

All notable changes to this project, by version and date. Format is [Keep a Changelog](https://keepachangelog.com/).

### Versioning

`vMAJOR.MINOR.PATCH[-stage]`. PATCH = a narrow fix to already-shipped behavior, no new
capability. MINOR = a new capability, a new config key, a dungeon gaining a working route it
didn't have, or several PATCHes cut together. MAJOR (`1.0.0`) is reserved for a specific,
checkable bar, not a round number: every non-event/vehicle LFD dungeon in README "Testing
status" reaching at least `Verified` (not just "data ready, untested").

The `-stage` suffix (`-alpha`/`-beta`) is independent of how high MINOR climbs, specifically so
a version number never implies "close to 1.0" on its own — `v0.47.0-alpha` is exactly as early
as `v0.1.0-alpha`. `-alpha` (current) = only a handful of dungeons live-verified. `-beta` = most
non-event dungeons live-verified, only minor gaps (e.g. door/gate handling) remain. No suffix =
the MAJOR bar above is met.

## [Unreleased]

### Added
- Telemetry v2: structured payloads for objectives, checkpoints, wipes, recovery and
  interactions; the world DB spawn id on every unit; the navmesh path on every path decision;
  `boss_killed`.
- `tools/run_replay/status.py`: the testing-status table is generated from campaign artifacts.

### Fixed
- The leader's walk back to a straggler stops when the recovery ends. It went on to the
  straggler's old spot - in Shadowfang Keep from the courtyard back into the cells, where the next
  walk met the closed Cell Door and the run aborted. (Earlier put down to looting alone.)
- A deferred recovery escalation (leader "off the ground") waits at most 30 s, then puts the
  leader back on its last good spot or escalates - it waited 52 minutes in Deadmines.
- A mob running for help (flee for assistance) takes the kill target and the tank may follow it
  past the leash - in SM Cathedral runners chained the nave and Mograine into one fight.
- A route boss killed while the party worked on another step (Zul'Farrak: Antu'sul joined the
  fight at Theka's) is done - its own step no longer aborts the run on "not found".

## [0.14.0-alpha] - 2026-10-06

Validation you can see: every run is recorded as structured evidence and reconstructed into a
timeline, a map and a verdict, so a failure points at what happened instead of at a log line.
First campaign judged this way: Ragefire Chasm, SM Library, SM Armory, SM Cathedral, Razorfen
Downs and Zul'Farrak Verified (two full routes each on the same build).

### Added
- Telemetry schema v2 (`DungeonLeadEvents.v2.jsonl`, `docs/telemetry-schema-v2.md`) next to the
  unchanged CSV: ordered events (`event_seq`, `run_ms`), the leader's position, build commit,
  validation campaign and scenario; the leader's path; fights with every unit that joined (world DB
  spawn id), mob and party deaths, the pack at engage, combat anchors, the walk's path decisions.
  Validation campaigns write `DungeonLeadCampaigns.jsonl` (manifest, run results, requeues).
  Measured: ~45 events and 32 KB a minute per party, no drops.
- `tools/run_replay`: run model, timeline and XY map, verdict (FULL_ROUTE / PARTIAL / FAILED /
  UNKNOWN, health) and findings that keep facts, classification, probable cause, confidence and
  evidence apart; run comparison; campaign artifacts for a panel. Golden tests on six real runs
  (`tests/fixtures/run_reconstruction_v2`).
- `tools/route_insert.py`: insert steps into a route and write the SQL update.

### Fixed
- A boss that joins a fight far from its home no longer moves the fight to it: the fight stays
  where it runs (RFC Taragaman and SFK Springvale dragged the tank through more packs).
- The combat leash no longer holds the tank back from a mob beating a party member - it only
  stops chasing a mob that is not attacking anyone.
- No looting during a session (leader and followers): upstream's loot actions outrank the route
  walk, so the leader looted every corpse first and once walked back to old corpses.
- Validation campaign: a run counts as ended only after 30 s without a session (a wipe teleport
  hid the tank and the campaign released the bots of a live run).

### Changed
- Shadowfang Keep: the courtyard packs between the Courtyard Door and Razorclaw are optional pull
  steps, taken one by one (`sql/updates/2026_10_06_00_sfk_courtyard_packs.sql`).
- SM Cathedral: the nave's packs are optional pull steps from the door to the altar
  (`sql/updates/2026_10_06_01_cathedral_nave_packs.sql`) - crossing the nave pulled 13 at once.

## [0.13.0-alpha] - 2026-10-06

Dungeon events and doors: the leader opens what a player opens, runs the events a route needs
(use/talk steps) and no longer walks through closed doors. Live: Deadmines, Shadowfang Keep and
Zul'Farrak now complete with their events; 9 of the 11 validated dungeons reach full route.

### Added
- `.dungeonlead validate <lfgId...>` (GM/console): runs a list of dungeons with fresh bot-only test
  parties, one result line per dungeon - the live validation campaign, previously a server-side
  patch. Dungeon sets and procedure in `tools/live_validation/`; `tools/summarize_runs.py` now
  prints ALERT lines for states that should never occur.
- Route kind `required`: trash that gates progress. Fought like an optional stop, but never
  skipped - retried, then the run ends partial.
- Door rows can name their game object (entry + position): the leader goes to that door, moves on
  once it is open and waits while it is closed. Deadmines, Shadowfang Keep and Scholomance have
  theirs (`sql/updates/2026_10_05_00_route_door_targets.sql` for existing installs).

- Route steps `use` and `talk`, both never skipped. `use`: go to a game object and use it the way
  a client does - a hand lock is opened with its Opening spell (a chest is then looted), a key lock
  by the party member holding the key, a key with its own spell casts it on the object (the
  Deadmines cannon); a key nobody has is looted from a corpse nearby. `talk`: go to the NPC and
  pick its gossip option once it offers it; while waiting, the party fights whatever attacks the
  NPC or its friends; an NPC hostile to the leader (the other faction's prisoner) is skipped.
- Dungeon events run end to end: Deadmines (gunpowder, cannon, Iron Clad Door), Shadowfang Keep
  (the prisoner opens the Courtyard Door), Zul'Farrak (Executioner's Key, cage, stairs waves,
  Weegli, Sergeant Bly). `sql/updates/2026_10_05_0[1-5]_*.sql` for existing installs.
- Scripted area triggers the leader stands in are fired (bots have no client to send them):
  Witch Doctor Zum'rah turns hostile.
- Config: `EventWaitSeconds` (600) - how long a use/talk step waits for its event.

### Fixed
- Bots no longer walk through closed doors (mmaps don't know doors): the leader stops at a closed
  door its path crosses and waits for it - and opens it once, as a player could (a hand lock, a
  key, a lever on its side of the door).
- Target plan: an enemy totem takes the skull first; a crowd-controlled mob is not a kill target
  while others are up; no new CC on a hurt mob, none on a mob it failed on, and the CC is released
  when its mob is the last enemy (watched live: the party waited for a polymorph to break).
- The recovery walk-back follows the computed path (it walked the tank into the rock) and the
  escalation waits while the leader itself is off the ground.
- Long paths (cut at the point limit) count as reachable; detours that first lead away are taken.
- Test parties are one faction (the other faction's bots killed an event NPC).

### Changed
- A closed door only counts as blocking the way when it is near the line to where the leader is
  going, not merely nearby.
- README and the testing status describe the current state; the 2026-10-03
  implementation record moved to `docs/history/`.

## [0.12.0-alpha] - 2026-10-05

Hardening rounds H4-H7: pack identity, closed doors, combined failure paths, and the first live
validation campaign across 11 dungeons, with the failures it found fixed. Still `-alpha`: most
dungeons in the route table have no live run yet.

### Fixed
- A bot of a running session can no longer be moved out of the dungeon by another system
  mid-run (seen live: the leader vanished from Wailing Caverns while the party was drinking;
  in Ragefire Chasm the whole party kept being sent out through the exit). Real players, the
  dead and a party whose player has already left are not affected; each case is logged as
  `unexpected_teleport`.
- Test bots are only prepared once mod-playerbots has attached their AI (a quick re-login after
  a release used to fail every profile).
- The leader and its party no longer wander off with mod-playerbots' RPG behaviour during a run.
  When it couldn't walk to its RPG destination it teleported there - out of the dungeon (seen in
  Shadowfang Keep while the party was drinking).
- Party members no longer get sent to their homebind every tick about a minute into a run.
  AzerothCore marks a player's instance invalid when they leave or disband a group inside it,
  which test characters did on joining; the session restores the flag for members standing in the
  party's own instance (`instance_validity_restored`).
- A straggler stuck on terrain no longer keeps the party walking back and forth until the run
  times out: the same recovery coming back within 90 s continues its clock, so the straggler is
  brought to the leader. The escalation brings every bot straggler, and someone new falling behind
  afterwards gets a recovery of its own instead of ending the run as `recovery_failed`.
- The walk back to a straggler follows a computed path; a straight move to an unreachable member
  could put the tank inside the rock (SM Library, Deadmines). If the leader still ends up where it
  can't path from, it is moved back to the last spot it made progress from, once per step
  (`leader_unstuck`).
- A pull no longer retries forever when its mark can't be placed: a marking timeout counts as a
  failed attempt, a boss out of sight (one floor down in Razorfen Kraul) is approached instead of
  marked, and no new pull is started while the tank is still fighting.

### Added
- Live validation in 11 dungeons with bot-only test parties: Ragefire Chasm, SM Graveyard,
  SM Library and Razorfen Downs completed; Deadmines to VanCleef, Shadowfang Keep to Fenrus,
  Wailing Caverns to Pythas within the 45 min test cap. Matrix in `docs/testing-status.md`.
- `tools/summarize_runs.py`: one summary per run (result, bosses cleared, objective failures,
  wipes, recoveries, interactions) from the two telemetry CSVs.
- Closed doors: when the way is blocked by a closed door or gate, the leader waits for it to open
  (its event, key or boss) instead of giving up on the step - up to `DoorWaitSeconds` (120), then
  the step fails with reason `door_closed`. Logged as `interaction_state`.
- Config: `DoorWaitSeconds` (120).

### Changed
- A pack is now the units spawned at its spot (plus its nearest member, so a patrol still counts)
  rather than every unit of that type within 150 yd; once engaged its membership is locked, and
  anything attacking the party is tracked as an add without being mistaken for the pack itself.
  With `startdungeon debug` on, `pack_resolution` records how it was resolved.

## [0.11.0-alpha] - 2026-10-04

The leader is now a state machine with explicit readiness, pull, recovery and wipe handling
(phase-by-phase record in `docs/history/`), plus the first hardening round (H1-H3).
Live-tested with a bot-only test party in Wailing Caverns only.

### Fixed (hardening)
- A recovery whose reason changes (a straggler that then counts as lost, ...) gets a fresh
  timeout instead of inheriting an almost-expired one; a recovery episode as a whole is still
  capped, so switching between reasons can't loop.
- During a fight, targets are picked around where the fight began, not around wherever the tank
  has moved to - a neighbouring pack no longer slides into the target plan; an add attacking the
  party still counts wherever it is.
- A boss is never skipped any more: if it can't be pulled or reached, it gets another round
  (`ObjectiveRetryRounds`, 2) and then the run stops as partial (`objective_failed`) instead of
  walking past it. Optional trash and events are still skipped (`objective_skipped`, replaces
  `pack_skipped`).

### Fixed
- A session no longer counts as started until the tank is seen as group leader. The leader
  change is retried and the start abandoned if it never lands, instead of running with the wrong
  leader. `stopdungeon` waits for leadership to actually return to the player, with the same
  retry limit, instead of assuming the queued change worked.
- A dead or out-of-instance healer, or a dead party member, now also stops the route walk with
  a logged wait reason (pulls were already held back).
- Test parties and stranded (released) party members are placed at the dungeon's real entrance
  (the map's entrance teleport target) instead of the first route step, which is usually the
  first boss - test parties used to arrive on top of Lady Anacondra with no line of sight to her.
- The "stuck on the way to a stop" give-up only counts walking time. It used to keep counting
  through fights and skipped Lady Anacondra after three minutes of clearing her trash.
- Test parties start their session only once every member is actually inside, in the same
  instance, gathered by the tank - not as soon as the teleports were requested. A party that
  never assembles within 3 minutes is reported and not started.
- Pure navigation waypoints (e.g. the Wailing Caverns bridges) are passed as soon as they are
  reached. They used to wait `StuckSeconds` (45 s) each and end up listed as skipped stops.

### Changed (telemetry)
- `DungeonLeadSessions.csv` rows get five more columns at the end: `map_id`, `instance_id`,
  `state`, `step`, `pack_id` (existing columns keep their positions; an existing file keeps its
  old header). New events: `leadership_request`, `leadership_confirmed`, `leadership_failed`.
- Telemetry is no longer written to disk from the bots' own AI threads: lines go into a bounded
  in-memory buffer and are written every ~2 s and at shutdown. If the buffer ever overflows,
  the number of lost lines is logged and written as a `telemetry_dropped` row.

### Added
- Config: `LeadershipAcquireTimeoutSeconds`, `LeadershipReturnTimeoutSeconds` (default 5),
  `LeadershipMaxAttempts` (default 3).
- Config: `PullRange` (30), `PullInitiateTimeoutSeconds` (10), `PullEstablishTimeoutSeconds` (8),
  `PullMaxAttempts` (2).
- Config: `CombatLeashRadius` (30).
- Config: `PartySoftRange` (40), `PartyHardRange` (90).
- Config: `PostCombatMinSeconds` (3), `PostCombatMinHealthPct` (50).
- Config: `RecoveryTimeoutSeconds` (60), `RecoveryEscalationSeconds` (60).
- Config: `BossLeashRadius` (25).
- Config: `ObjectiveRetryRounds` (2).
- `tools/run_tests.sh`: unit tests for the leadership and party-readiness decisions.

### Changed
- Party readiness (player, healer, deaths, combat, drinking, healer mana, leash, spread) is
  checked in one place for both pulling and walking, so the two can no longer disagree. The
  healer mana check reads the party directly instead of mod-playerbots' cached value.
- Each session now has one explicit state (starting, waiting, travelling, combat, post-combat,
  wipe recovery, completing, stopping). Every change is logged and written to
  `DungeonLeadSessions.csv` as `state_transition`; `startdungeon status` shows the current one.
- Route steps are classified by what the leader does there (travel, pull, boss, door,
  interaction, recovery, end) from the existing route data - no data change. The current
  objective appears in state transitions, `startdungeon status` and the "heading to" message.
- Each pull/boss/event stop now tracks its enemy pack (unknown, available, engaged, cleared,
  skipped), logged as `pack_state` and shown in `startdungeon status`. The route only moves past
  a pack once it is cleared or given up on.
- Pull controller: once the party is ready and the tank is within `PullRange` of the current
  pack, it marks the target with the skull and starts the attack (the class AI does the
  fighting). A pull that doesn't start or doesn't stick within its timeouts is retried, then
  the pack is skipped and reported (`pull_failed`, `pack_skipped`) - never silently passed.
  Fighting the pack's trash counts as progress; only a pack that itself resets three times is
  given up on (`pack_reset`).
- Party cohesion has levels: a member beyond `PartySoftRange` (40) holds the next pull, beyond
  `PartyHardRange` (90, the old spread limit) the leader stops walking too, and a living member
  on another map or offline counts as lost (stops both). The pull controller logs why it holds.
- Target plan: each fight gets one stable plan - skull on the primary (boss, then caster, then
  elite), cross on the secondary, moon on an elite to crowd-control when there are more than two
  enemies - logged as `target_plan`. Marks follow the plan; a mark placed by a player is never
  moved. This also replaces the old boss-only skull/moon marking.
- After every fight the leader pauses (`PostCombatMinSeconds`, 3) and re-checks the party before
  moving on, now including health: it waits while anyone is below `PostCombatMinHealthPct` (50).
  The decision and what it was based on are logged as `post_combat_decision`.
- Recovery with a time limit instead of waiting forever: a member too far behind, on another
  map/offline, dead, or someone else taking the party lead now starts a recovery (the leader
  walks back to a straggler, waits for a resurrection...). After `RecoveryTimeoutSeconds` it
  escalates (a bot member is brought to the leader - a real player never is), and after
  `RecoveryEscalationSeconds` more the run is stopped and reported as failed. Logged as
  `recovery_start` / `recovery_escalate` / `recovery_complete` / `recovery_failed`.
- Wipe checkpoints: the last cleared pack / reached waypoint is a checkpoint. After the tank
  dies and recovers, the route resumes right after it - anything only skipped since gets
  another try - and the current pack is looked at afresh (`checkpoint_restore`).
- Boss fights: the leader goes through boss_prep and boss_combat states. A boss fight is
  anchored at the boss's own spot with its own leash (`BossLeashRadius`, 25), and facing is left
  to mod-playerbots' `tank face` (added for the fight if the leader isn't tank-specced).
- Combat leash: where a fight begins becomes its anchor, and the leading tank doesn't chase a
  target that runs more than `CombatLeashRadius` yards from it (logged as `leash_hold`), so a
  fleeing mob can't drag the party into the next pack.

## [0.10.0-alpha] - 2026-10-03

### Changed — ⚠ Breaking: install method
- **Converted from a patch against mod-playerbots to a standalone AzerothCore module.** Install
  is now "clone into `modules/`, reconfigure, build" instead of `git apply` — see README "Install".
  No more pinned-commit drift, no more `upstream-compat.yml` (removed, nothing left for it to
  check). Business logic in `src/DungeonLead/` is unchanged byte-for-byte except its config reads,
  redirected from mod-playerbots' own `PlayerbotAIConfig` to this module's own `DungeonLeadConfig`
  (same keys, same defaults, read independently via `sConfigMgr`).
- The two points that changed *existing* mod-playerbots behavior (the "leader" formation, the
  dungeon hand-back bypass) are now a subclass + registry-overwrite (see `DungeonLeadOverrides.h`)
  instead of edited mod-playerbots source — see README "How this works without patching
  mod-playerbots" and [ADR-004](docs/architecture/adr-004-patch-to-module-migration.md).
- `mod-playerbots-dungeon-lead.patch` and the old `src/DungeonLead/` patch-era mirror are removed
  from the working tree — recoverable from this repo's git history if ever needed, and the
  pre-migration patch-based state remains intact on the server it was developed against.

### Also included: unpublished DungeonLead fixes from the live development branch
Since this migration took its source from the live branch's current state rather than the last
published patch, it also catches up several `DungeonLead/` fixes developed there after 2026-10-02
that were never separately regenerated/pushed before now — notably stranded-member wipe recovery
(`RecoverStrandedMembers`, wipe count now recorded in `DungeonLeadSessions.csv`/run summaries) and
a `DungeonTestBotPool` instance-binding fix (teleporting a party simultaneously could land each
member in a *different* copy of the same instance; the leader now enters first so the group's
instance binding exists before followers teleport in, plus a self-exit step for a bot still sitting
in a stale copy of the dungeon). Not itemized further here since they predate and are independent
of the patch-to-module work itself.

### Known gap in this release
- GM diagnostic commands from the patch era (`testbotpool`, `lfgstate`, `pathcheck`,
  `pathcheckfrom`, `tpbot`) are not yet ported to the module's own `.dungeonlead` command root.
- A full live end-to-end run through the module is not yet confirmed (registration is).

## [0.9.0-alpha] - 2026-10-02

### Fixed
- **Patch no longer applied to mod-playerbots master** (tracked in #2, caught by the weekly
  `upstream-compat.yml` check since 2026-09-21). Re-verified against mod-playerbots `master` @
  `037c014` (previously pinned `b949b50b`). Only one of the 25 touched/new files actually
  conflicted — `src/Script/Playerbots.cpp` — and only on context, not content: upstream removed
  the `DatabaseLoader.h` include this patch's `#include` block was anchored after, and inserted a
  new `OnShutdown()` method into the same `WorldScript` subclass right after the `OnUpdate()` this
  patch hooks into, so the hunk's trailing context (previously the class-closing `};`) no longer
  matched. Both hunks re-anchored to current, stable context; no functional change. README's pinned
  commit references bumped to `037c014` accordingly.

Wailing Caverns navigation/CC fixes, from root-causing the 0.8.0 scale-test findings against real
mod-playerbots source (not guessed) and new `.playerbots pathcheck`/`pathcheckfrom`/`tpbot`
diagnostics.

### Added
- `.playerbots pathcheck <botName> <x> <y> <z>` (SOAP-reachable): runs the production
  `PathGenerator` call directly and dumps path type/actual end position/waypoints.
- `.playerbots pathcheckfrom <botName> <mapId> <srcX> <srcY> <srcZ> <destX> <destY> <destZ>` and
  `.playerbots tpbot <botName> <mapId> <x> <y> <z>`: teleport a bot to an arbitrary position first,
  so a specific route hop can be verified independent of wherever the bot currently stands.
- `AiPlayerbot.DungeonLead.CcAbsoluteTimeoutSeconds` (default 45): absolute ceiling on how long a
  moon-marked CC candidate can go un-crowd-controlled before being released unconditionally.
- `TestBotPoolTick()` retries a party member's entrance teleport (bounded, 3 attempts) if it
  doesn't land on the expected map within a few seconds - defense-in-depth for a genuine transient
  teleport failure, kept even after the "left instance" root cause below turned out to be
  something else.

### Changed
- **Wailing Caverns route rewritten from scratch, all 15 steps individually `pathcheck`-verified**
  (supersedes the single "Ramp waypoint" step from the previous entry, which is now known to have
  never actually been reached - see Fixed below). Straight-line hop distance turned out to be no
  guide at all to whether a hop actually completes (a 133y hop failed, a 330y hop was perfectly
  fine); every hop in the new route is backed by a real `PathGenerator` result, either
  `PATHFIND_NORMAL` or `PATHFIND_INCOMPLETE` landing within <0.5y of the next step. Lord Cobrahn and
  Skum both turned out to be navmesh dead ends (no path onward to any other boss/waypoint) and the
  route now explicitly backtracks through the same bridge point rather than assuming a shortcut
  exists. Also confirmed `Disciple of Naralex` is gated behind all 4 "Fanglords" (Cobrahn, Verdan,
  Serpentis, Pythas) being dead, not just orderable for pathing convenience.

### Fixed
- **CC timeout counted down before the caster could possibly act.** The base mod-playerbots CC
  reaction (`RtiCcTrigger`) only ever fires once the marked creature is in the bot's own
  `"attackers"` list (confirmed by reading `TargetValue::FindTarget`); `DungeonLeadMarkAction`
  marks a candidate by proximity, routinely before it has aggroed onto anyone. `CheckCcMark()` now
  holds the grace window open until the marked creature is actually in combat, bounded by the new
  absolute ceiling above (a candidate that never aggros would otherwise hold the mark forever).
  Also stopped a real regression this introduced mid-fix (an earlier version gated on "any party
  member in combat", which is a different creature entirely and just as easy to satisfy without
  the mark ever having a chance).
- **The "creature already dead" branch of `CheckCcMark()` cleared silently.** No log line, no CSV
  event - indistinguishable from a genuine multi-minute stall when debugging live. Now logs and
  records a `cc_target_gone` event.
- **Route waypoints reusing a dead boss's creature `entry` as a harmless placeholder were silently
  skipped.** `MoveRouteTo()`'s first move is to search for a live/dead creature matching the
  step's `entry` within 150y of the bot; a waypoint placed right after killing that same boss finds
  its corpse immediately and takes the "already dead, next" branch without ever walking to the
  waypoint's actual coordinates. This is why the previous "Ramp waypoint" (entry reused from
  Kresh) was never actually verified live despite the earlier changelog entry claiming otherwise -
  it was being skipped every single time. All pure waypoint steps now use `entry=1` ("Waypoint
  (Only GM can see it)", a stock `creature_template` row confirmed to have zero spawns on this
  map), so the entry-search always comes back empty and the stored coordinates are used as
  intended.
- **Some party members were left outside the instance after `RunTestParty` ("left instance" /
  needing a manual `/summon`, reported very early in this project).** Root cause was not a
  Dungeon Lead bug: AzerothCore's standard `AccountInstancesPerHour` throttle (default 5) silently
  refuses entry once an account has opened that many distinct dungeon instances in an hour, and
  `RunTestParty` opens a fresh instance every call - reliably hit by running many test parties
  against the same small AddClass account pool in one session. No code fix needed once identified;
  documented here since it looks exactly like a pathing/teleport bug from the symptoms alone.

### Known limitation
- CC still lands only a small fraction of the time even with correct timing (mark/attack-list
  gating) - something else (range, line of sight, or GCD) is blocking the actual spell cast. Not
  yet investigated.
### Worldserver crashes - root-caused and fixed (was: a Known limitation here)

The repeated SIGSEGVs during mass concurrent test-party load turned out **not** to be a Dungeon
Lead defect, and not the follower-lifetime bug they appeared to be. AddressSanitizer identified a
heap-use-after-free between two unrelated third-party modules installed on the same test host:

- `mod-junk-to-gold` destroys a looted item (`Player::DestroyItem`) from inside its
  `OnPlayerLootItem` hook, while `ScriptMgr::OnPlayerLootItem` is still handing that same raw
  `Item*` to every remaining `PlayerScript` in turn.
- `mod-transmog`, called later in that same dispatch, dereferences the freed pointer
  (`typeid(*item)`), corrupting the heap. The worldserver died within minutes, every time.

Bots trigger this constantly because they loot grey junk constantly, which is why it looked like
load-dependent random corruption. Fixed in `mod-junk-to-gold` by deferring the destroy to a
`m_Events` callback that re-resolves the item from its GUID, so the loot-hook dispatch is finished
before anything is freed. Verified with ASAN: unfixed builds reported the use-after-free and died
in ~4-5 minutes under identical load; the fixed build ran cleanly for 52 minutes.

Two earlier conclusions recorded in this changelog were **wrong** and are corrected here:

- Cross-map-teleporting a bot with active followers was **not** a reliable crash trigger. The
  server was crashing on its own every few minutes at the time, so that teleport happened to land
  in a window where the heap was already corrupt. On a clean heap the exact same command - same
  bot, same coordinates - does nothing.
- The symbolized backtraces through `Unit::m_followingMe` / `AbstractFollower` were showing where
  the corruption *surfaced* (a SEGV at address `0x8` inside an `unordered_set` bucket lookup), not
  where it originated. A code audit of that lifetime chain found no defect, and ASAN never flagged
  it. DL-001/DL-007's follower-ownership concern is therefore neither confirmed nor refuted - it
  is simply not what was crashing this server.

The `force` guard on `tpbot`/`pathcheckfrom` is kept as ordinary caution, but its refusal message
still cites the superseded reasoning.

## Phase 0 (2026-09-15): safety and truth, from an independent architecture review

An independent architecture/code review of the whole project (not just Wailing Caverns) found the
harness itself untrustworthy in several ways that could produce false test results, and rated the
project Alpha/2-10 autonomy. Its "Immediate Next Tasks" became this project's Phase 0 roadmap -
safety and truth before any further route/party-orchestration work. Findings referenced below by
their review ID (DL-001 etc.) for traceability; full findings are not reproduced here.

### Added
- `DungeonLeadState::leaderSnapshot`/`hasLeaderSnapshot`: the session leader's own pre-
  `startdungeon` formation/strategy state, snapshotted the same way every follower already was.
- `DungeonRoute::HasAnyMandatory()`.
- `TestBotLease::accountId`, and `AcquireBot()` now prefers a candidate whose AddClass account
  isn't already represented among active leases (falls back to reuse rather than ever refusing).
- `.playerbots tpbot`/`pathcheckfrom` gained a `force` argument and, without it, refuse to
  cross-map teleport a bot still in a >1-member group.
- A real `dungeon-lead` git branch (committed on top of the pinned upstream base in the working
  checkout) replaces hand-diffing an uncommitted working tree to produce
  `mod-playerbots-dungeon-lead.patch` - a new upstream release is now `fetch` + `rebase` + resolve
  + regenerate, not a from-scratch manual diff. See README's new "Maintainer workflow" section.
- `tools/resolve_routes.py` refuses (exit 1) to overwrite `dungeon_routes.csv` if doing so would
  silently drop a `source=derived` navigation waypoint it has no way to re-derive; `--force`
  overrides.

### Fixed
- **DL-002 - `Stop()` didn't reliably turn Dungeon Lead off.** `IsOn()` could still read true after
  a reported stop (upstream `Reset()` doesn't guarantee stripping an active strategy, and the
  leader itself was never snapshotted the way followers were). `Stop()` now restores the leader
  from its own snapshot and explicitly verifies `IsOn()==false` afterward, logging if not.
  `StartSession()` also now refuses to start on a bot that's already active instead of silently
  re-snapshotting over a live session.
- **DL-003 - route completion could be a zero-work false positive.** A route with no `boss`-kind
  step at all (14 of 96 configured routes) reported Complete having done zero movement or combat.
  Downgraded to `Blocked`/`UnsupportedEvent` when a route has no mandatory objective to begin with.
- **DL-006 (partial) - telemetry couldn't yield an authoritative run result.** `run_id` restarted
  at 1 every worldserver restart while the CSV itself never truncates, so two different processes'
  run 1 were indistinguishable - now seeded from process start time. New `DungeonLeadRuns.csv`
  writes one row per run at its terminal outcome (route completion and canary timeout so far, not
  every exit path yet) instead of requiring event-stream reconstruction.
- **DL-001 (containment) - `tpbot`/`pathcheckfrom` refuse to cross-map teleport a bot that is
  still in a multi-member group** unless `force` is given. This was added believing that teleport
  reliably reproduced a live SIGSEGV; it did not - see "Worldserver crashes" above for what was
  actually crashing the server. The guard is kept as ordinary caution around an unproven concern.
- **DL-017 - the standalone `src/` mirror was uncompilable.** `DungeonRouteMgr.h` was missing
  `ccLandedTold`/`lastPathLogTs`, fields `DungeonLeadActions.cpp` already used. All 12
  `src/DungeonLead/` files are now confirmed byte-identical to the deployed checkout.
- **DL-014 - the route-data pipeline didn't round-trip.** `resolve_routes.py` would have silently
  regenerated `dungeon_routes.csv` without the Wailing Caverns bridge waypoints (see the fix above);
  `routes.tsv`'s boss order was also still the pre-rewrite sequence and has been reconciled.
- **DL-005 - test bot leases weren't account-aware.** `AccountInstancesPerHour` is throttled per
  AzerothCore account, not per character, and an AddClass account owns ~10 characters - acquiring
  leases without regard to account concentrated instance-entry load on a few accounts. Verified
  live: 5 sequential Mage acquisitions landed on 5 different accounts.
- **DL-004 (partial) - `RunTestParty` could silently form incomplete parties.** Party count was
  `min(tanks, healers)` with no floor on dps, and the per-party loop hands out up to 3 dps per
  party from one shared cursor - once dps ran out mid-loop, later "parties" formed with 2-4
  members instead of 5 and were started anyway. Added a `dps/3` floor so every party this function
  forms has exactly 1 tank + 1 healer + 3 dps, or it refuses with a clear count in the message.
  Verified live: 2 tanks/2 healers/4 dps leased, `run` formed exactly 1 full 5-member party and
  left 1 tank/1 healer/1 dps idle instead of starting a broken second party. Only fixes party
  *size* - role/level/gear qualification, shared-difficulty binding, and consistent-instance
  postconditions are still open (see below).

- **DL-008 - disabling canary creation also disabled safety supervision of already-active canary
  sessions.** `CanaryTick()` returned before its real-player-join/timeout safety pass ever ran
  whenever `CanaryEnabled` was false (including the documented default). Split into
  `CanarySupervisorTick()` (unconditional) and `MaybeStartCanary()` (still flag-gated). Verified
  by code review and build/deploy; a live attempt (short `CanaryTimeoutMinutes`, forced
  `canarytest`) didn't converge - the party sat queued >10 minutes without an LFG match under the
  current bot-pool load - so not live-triggered end to end.

- **DL-009 (partial) - a dropped leader-change enqueue was invisible to the caller.**
  `StartSession()`/`Stop()` both queued a `GroupSetLeaderOperation` onto the bounded world-thread
  processor and discarded its bool result - if the queue was full, the operation was silently
  dropped and the caller proceeded as if leadership had actually changed. `StartSession()` now
  refuses to start (and logs why) if the enqueue itself fails; `Stop()`'s handback now at least
  logs when it fails. Still no atomic group-keyed claim, operation token, or wait for
  `GetLeaderGUID()` to actually match before committing state - two tanks racing for the same
  group remains open.

- **DL-006 (extended) - `ReleaseTestBot()` was a third silent exit path.** Route completion and
  canary timeout already wrote a `DungeonLeadRuns.csv` row on Stop(); releasing a leased test-pool
  tank mid-run - the normal way an operator reclaims a bot - didn't. Found live, by accident
  (released a demo tank a moment before it could be watched, with no telemetry trail left behind).
  Fixed and live-verified: `DungeonLeadRuns.csv` didn't exist anywhere on the server before this -
  DL-006's original two paths were never live-triggered either - and this release wrote its
  first-ever row.

- **DL-004 (extended) - a dead party member was silently left behind.** Found live, watching a run
  with the user: a leased healer died en route to the dungeon entrance (open-world mobs) before LFG
  teleported the group in; LFG drops a dead character from the teleport without erroring, so the
  tank and three dps ran the whole instance a healer short with no signal anywhere that the group
  was incomplete. `RunTestParty()`'s candidate filter now excludes `!bot->IsAlive()` the same way it
  already excluded a grouped bot.
- **Live full-length run (2026-09-15, watched with the user, terminated by DL-008's 45-minute
  timeout, not a crash or hang)**: exposed a real navigation gap the pathcheck-only verification
  process didn't catch - the bot's live movement AI got stuck approaching/returning from Lord
  Cobrahn's dead-end bridge (`stuckAttempts` climbing, `skip_stuck` eventually firing) even though
  point-to-point `pathcheck` had confirmed the same segment walkable. `DungeonLeadRuns.csv` recorded
  it correctly: `outcome=partial, failure_domain=navigation, failure_reason=path_failed,
  skipped_steps=7`. Not yet fixed - pathcheck verifying a graph is walkable turns out not to
  guarantee the live pathing AI executes it reliably under real (combat-interrupted) conditions;
  needs its own investigation, tracked loosely under DL-011 (navigation identity/execution split).
  **First fix attempt tried and disproved live**: `MoveRouteTo()` was changed to advance along
  `PathGenerator`'s own computed waypoint list instead of jumping straight to the far endpoint via
  `MoveTo()` - re-tested on the identical Cobrahn segment and `skip_stuck` fired again with the same
  pattern (bestDist 53.8y, ending ~196y away). Likely cause: `MoveRouteTo()` recomputes
  `CalculatePath()` fresh on every call with no cached state, and the navmesh solver appears to
  return a slightly different winding route each time depending on the bot's exact current
  position - the bot bounces between route variants instead of committing to and following one.
  Reverted rather than escalate to a session-state path cache without time to test it broadly
  across every dungeon's routes (this function runs for every step of every route) - same
  low-regression-risk posture as DL-007. A real fix needs to cache the computed path once and walk
  it across ticks, invalidating on real deviation, not recompute-and-hope each call.

- **DL-006 (extended) - the last two of five known `Stop()` call sites now record a run summary
  too.** `DungeonLeadStopAction` (auto "left instance" - the single most common exit path watching
  live runs today) and `StopDungChatShortcutAction` (manual "stopdungeon" from a human master) both
  now call `RecordRunSummary()` before `Stop()` erases state, matching the three sites already fixed
  earlier today. `DungeonLeadRuns.csv` now gets a row from every known way a session ends.
- **DL-020 - the ~2s reconciliation loop mutated every follower's formation/strategy (and the
  leader's own) on every pass, whether or not anything had drifted.** `ApplyLeaderFollowerStrategies`
  already computed a per-follower wiped comparison to decide whether to *log* a restore, then called
  `ChangeStrategy()`/`FormationValue::Load()` unconditionally anyway - not free upstream (they remove
  and reinitialize an engine even on a no-op `+token`). Each mutation now only runs when its own
  wiped flag is true; added the same missing check for the leader's own two `ChangeStrategy` calls
  (mirrors `DungeonLead::IsOn()`). `startdungeon`'s own unconditional first application is untouched.
  Verified live: the new leader check fired exactly once at session start (the expected reset), then
  zero further restores over 7+ subsequent guard passes on a stable session.

- **DL-018 - a canary run that finished its route stayed active until an unrelated timeout
  mislabeled it.** `DungeonLeadNextAction::Execute`'s terminal-outcome block already recorded the
  right outcome and reported it, but never called `Stop()` - the session kept its
  `CanaryMaxConcurrent` slot, kept `GuardActiveSessions` reasserting its strategies, and kept its
  group/leases held until the canary timeout supervisor eventually force-stopped it and recorded
  `terminal_reason=canary_timeout` for a run that had already actually finished. Now calls
  `Stop(botAI, true)` whenever the session was `testMode` (every `AutoCanary` session, organic or
  targeted, always is) or its origin wasn't `Manual` - a genuine non-test `startdungeon` from a
  human player is left running, since they may still want the group held after the route's data
  runs out. Verified by code review and build/deploy; not live-triggered end to end - reaching a
  genuine full route completion (rather than the timeout or a `skip_stuck`) took longer than this
  session's testing window allowed (see the Cobrahn finding above).

- **DL-016 (minimal fix) - a stuck objective whose target stayed alive waited forever.** Confirmed
  live: watching tonight's full-length run reach Wailing Caverns' Disciple of Naralex (a friendly
  gossip NPC whose escort/Mutanus chain this module has no `INTERACT` executor for - the review's
  DL-016 finding exactly), "reached" fired and then nothing - the only give-up path required the
  target to be *not found*, and a friendly NPC that never dies is never not-found either. The
  objective silently occupied the session until the unrelated canary timeout eventually stopped it
  and mislabeled a stuck objective as a timed-out run. The give-up timeout (`StuckSeconds`, 45s
  default) now fires whether the target vanished or is simply still alive and not progressing -
  distinguished in the log/telemetry as `stuck_alive` vs `not_found`, both counting as a mandatory-
  objective failure. Not the review's full fix (no `INTERACT`/`ESCORT` executor exists - this still
  can't walk through the gossip chain, it just now gives up and reports Partial within seconds
  instead of up to 45 minutes). Can't fire mid-fight against a live hostile boss -
  `DungeonLeadNextAction::isUseful()` already returns false while `bot->IsInCombat()` - verified at
  the source level; not independently stress-tested against a real pre-pull delay near the 45s edge.

- **DL-011 (narrow fix) - `AiPlayerbot.DungeonLead.SkipOptional=1` could silently delete navigation
  anchors, not just optional content.** The review names this exact scenario: Wailing Caverns'
  six bridge waypoints (added earlier today to make its dead-end/backtrack segments walkable) are
  modeled as `kind=Optional` with a dummy `entry=1`, since no better-fitting kind existed - so
  flipping `SkipOptional` on (currently `0` on this server, so dormant, not live) would have skipped
  them along with genuine optional bosses, reintroducing the direct NOPATH hops they exist to
  prevent. Added `DungeonRouteStep::IsPathAnchor()` (`entry == 1` - verified against the live
  `creature_template` table that this is AzerothCore's universal "Waypoint (Only GM can see it)"
  template, not WC-specific data) and excluded it from the `SkipOptional` skip condition. This is
  the review's own assessed low-risk slice of DL-011 ("Regression risk: High for route behavior;
  low for the immediate WC type correction"), not the full Navigator rearchitecture.
  **Found live while testing this**: a fresh run with `SkipOptional=1` had Lady Anacondra alive and
  engaged (unlike every earlier attempt tonight) - the tank then died mid-fight and the session
  simply stalled (`DungeonLeadNextAction::isUseful()` requires `bot->IsAlive()`). A live instance of
  DL-013 ("wipe/death recovery is largely absent"), not touched by this change and not investigated
  further - noted here since it's exactly the failure mode that finding describes.

- **DL-021 (validation visibility) - unresolved `heroic_only` rows now warn explicitly.**
  `resolve_routes.py` only ever resolved `kind in (boss, optional, event)` - `heroic_only` was never
  in that list, so all six heroic-only rows (Yor, Anzu, Blood Guard Porung, Eck the Ferocious,
  Amanitar, Commander Kolurg) are unresolved by construction and silently inert at runtime
  (`IsWalkable()` requires a resolved position), while still counting toward reported route
  coverage. `validate_routes.py` used to fold this into the same "unresolved, that's fine" bucket as
  a genuinely optional gap; it now warns on each one by name. Pure tooling change, no server code
  touched - verified `python3 tools/validate_routes.py` emits exactly the six expected warnings,
  exit code still 0. Not the actual resolver fix (still needs real DB lookups per boss, the same
  kind of work the Wailing Caverns route rewrite did).

- **DL-006 (last gap closed) - a hard-disconnected tank's session is now closed out.** All five
  `Stop()` call sites already write a run summary; a tank that hard-disconnects (character object
  gone entirely) runs none of them and used to just silently drop out of `GuardActiveSessions()`
  forever - no `DungeonLeadRuns.csv` row, no cleanup. `GuardActiveSessions()` now distinguishes
  "gone entirely" (`!bot`) from "not currently in world" (`bot->IsInWorld()==false`, which a bot
  mid-teleport hits routinely and must not be torn down for - unchanged) and records a
  `hard_disconnect` run summary + resets state for the former. Needed a
  `DungeonLeadState::tankName` (cached at `StartSession()`) since `RecordRunSummary()` no longer has
  a live `Player*` to read a name from at this point; split into a GUID+name core with the existing
  `PlayerbotAI*` overload as a thin wrapper. Every currently-known way a session ends now writes
  exactly one `DungeonLeadRuns.csv` row. Verified live that the fix doesn't misfire on the
  legitimate transient `IsInWorld()==false` case (a normal party's cross-map teleport produced zero
  false-positive `hard_disconnect` rows over 7+ guard passes); an actual hard disconnect itself
  wasn't triggered live (playerbots have no real network connection to sever).

- **DL-013 (narrow slice) - a wiped tank no longer occupies its session forever.** Found live
  tonight, testing an unrelated fix: a tank died fighting Lady Anacondra and the session just sat
  there afterward, with no telemetry and no visible failure -
  `DungeonLeadNextAction::isUseful()` already refuses to run at all while `bot->IsAlive()==false`,
  but nothing else ever reacted to that. `GuardActiveSessions()` now records `wipe_detected` the
  first tick a session's tank is found dead, then - after `StuckSeconds` of still being dead - sets
  `outcome=Partial/Combat/PartyWipe`, records the run summary as `wipe`, and stops the session. A
  tank resurrected within that window is left running exactly as before. Not the review's actual
  recovery model (no corpse release, resurrection, regroup, or resume) - the same honest-failure-
  instead-of-silent-hang shape as DL-016/DL-018, applied to death instead of navigation or route
  completion. Verified by code review and build/deploy; a live re-test attempted to reproduce the
  original wipe (AzerothCore's `.die`/`.damage` GM commands require an in-game target selection and
  aren't usable over the SOAP console this session automates through) - the same tank fought Lady
  Anacondra again and survived this time, so no regression was introduced (no false `wipe_giveup`
  during a real, ongoing fight), but the actual give-up path wasn't re-triggered. The same run did
  confirm DL-016's `stuck_alive` fix firing correctly on Kresh.

- **DL-010 (narrow slice) - a dead or off-map healer read identically to a healer at full mana.**
  The review names this exact scenario: `HealerManaLow()`'s underlying "healer low mana"
  AiObjectContext lookup comes back null for a dead/off-map healer, and the function then returns
  false (not low on mana - don't block), the opposite of the truth. Added `HealerUnavailable()`:
  scans the group directly for a healer-spec member (`bySpec=true`) and blocks new pulls only if
  the composition has a healer role and none of them are currently alive and on the tank's map -
  mirrors `MasterUnavailable()`'s existing pattern. A composition with no healer role at all is
  unaffected (unchanged opportunistic behavior), matching the review's own accepted tradeoff for
  this finding. Wired into `DungeonLeadMultiplier::GetValue()` next to the existing
  `HealerManaLow()`/`GroupResting()` check. Not the review's full `PartyReadiness`/`PullPlan` model
  (no roster tracking, pull-target recording, or readiness deadline). Verified by code review and
  build/deploy; a live regression run with a real healer present completed its startup normally
  (no unexpected blocking) but ended via a natural "left instance" before reaching combat, so the
  actual healer-unavailable block itself wasn't re-triggered (killing an arbitrary target by name
  needs an in-game GM target selection, unreachable over this session's SOAP console).

- **DL-012 (narrow slice) - CC candidate search was anchored to the bot, not the pack it's
  fighting.** The review names this exact scenario: "a closer elite behind the group is Moon while
  the intended pack is ahead." Confirmed by reading upstream `PossibleTargetsValue` directly:
  `FindCcCandidate()`'s candidate list is unconstrained by pack/encounter (just
  `AnyUnfriendlyUnitInObjectRangeCheck` around the bot), and the function measured "nearest" from
  the bot's own position even though it already receives `boss` (the actual pack anchor) as a
  parameter. Now measures distance from the boss instead. Deliberately did not add an
  `IsInCombat()` requirement - CC's value is landing on an add *before* it joins the fight, so
  requiring combat first would defeat the feature rather than fix the pack-selection bug. Not the
  review's full fix (no provider/capability table, no `CCPlan`, no diminishing-returns/immunity
  awareness). Verified by code review (including reading the actual upstream value provider, not
  assumed) and build/deploy; not live-triggered - needs two simultaneously-nearby elite packs at
  different distances from a boss than from the bot, which this session's testing never produced.

- **DL-015 (narrow slice) - the kill-detection search was centered on the bot, not the encounter.**
  `GetCreatureListWithEntryInGrid()` runs every tick while still walking toward a step (not just on
  arrival), searching 150y around wherever the bot currently is - an unrelated creature sharing the
  step's entry ID somewhere else within that radius could be mistaken for the actual encounter,
  independent of the review's already-known duplicate-entry/relocated-boss concerns.
  AzerothCore's grid search only accepts a `WorldObject` anchor (no raw-position overload), so
  re-centering it on the step's own coordinates isn't a one-line change - filtered the already-
  fetched candidate list against `step.x/y/z` at the same probe range instead: cheap, and can only
  remove candidates the search would have wrongly included (the real encounter is at its own
  recorded coordinates by construction, so this can't exclude a true positive). Not the review's
  full fix (no selected-GUID/phase tracking, no missing/dead/despawned/evaded/reset distinction, no
  `InstanceScript` boss state). Verified by code review and build/deploy; not live-triggered - needs
  a second, unrelated same-entry creature within 150y of the bot's path but away from the step's own
  location, which this session's route data doesn't happen to contain.

- **DL-019 (minimal-fix slice) - a single `testbotpool run` call could burst-form every requested
  party in one same-tick spike.** The review's own minimal fix says "pace admission 1->3->10"; new
  `AiPlayerbot.DungeonLead.MaxPartiesPerRun` (default 5, in the shared `PlayerbotAIConfig.h/.cpp` -
  patch-only, not mirrored under `src/DungeonLead/`) caps how many parties one call forms, forcing
  an operator ramping past that to issue separate, naturally-spaced-out calls instead of one big
  burst. Not a real gradual-admission scheduler - just a hard per-call ceiling. **Live verification
  was inconclusive and is reported honestly as such**: a 6-set test formed exactly 5 parties
  (matching the cap), but a follow-up call revealed `testbotpool status` had been reporting stale
  "Ready" for several leases that were actually ineligible for unrelated reasons (probably stale
  group membership from an earlier, incompletely-released test today) - so the 6->5 result can't be
  confidently attributed to this cap rather than to that. That staleness is itself worth noting: the
  pool's tracked lease state can drift from actual candidate eligibility without `status` reflecting
  it, echoing DL-019's own broader complaint about the pool not modeling stable party identity - not
  fixed here.

- **DL-009 (extended) - `StartSession()` now refuses to start if another group member is already
  leading.** The review's named scenario ("two tank bots receive start... both install Dungeon Lead
  and record active state") isn't a C++ data race - this function runs single-threaded start to
  finish - but the existing self-check (`IsOn(botAI)`) only looked at the calling bot's own state,
  so nothing stopped a *second*, separate call for a different member of the same group from also
  succeeding. Added a scan of the group right after the self-check: any other member already
  `IsOn()` refuses the new session. A group has at most one active leader by construction now.
  Reuses the already-proven `IsOn()` helper and the same `GroupReference` iteration pattern already
  used elsewhere in this file. Not the review's full fix (no atomic group-keyed claim/generation
  token, no waiting for the async leader-set operation to actually land before committing state) -
  closes the specific "two simultaneous leaders" scenario named, not a stale/rejected handoff after
  a successful enqueue. Verified by code review and build/deploy; a normal single-tank start showed
  no regression. Not live-triggered - reproducing two near-simultaneous starts on the same group's
  two tank-capable members isn't something the existing test tooling (one tank per formed party)
  naturally exercises.

### Not yet done from Phase 0
DL-001/DL-007's follower-ownership concern (whether a follower or its target can go stale without
the other side's cleanup running) - note this is no longer believed to be what crashed the test
server, and neither a code audit nor ASAN found a defect there, but it is unproven either way
rather than closed, the rest of DL-006 (structured `RunRecord` with campaign/scenario/commit_sha
identity - every known exit path, hard disconnect included, now writes a row, but there's still no
richer identity per row), the rest of DL-004 (role/level/gear qualification per bot beyond
alive/dead, shared-difficulty binding, consistent-instance postconditions before `StartSession`),
the rest of DL-013 (a wiped session now fails cleanly after a timeout instead of hanging forever,
but there's still no actual recovery - corpse release, resurrection, regroup, or resume), the rest
of DL-009 (atomic group-keyed claim/generation token, waiting for the
world-thread leader change to actually land before committing session state), DL-008's long-term
explicit finish/abort/drain policy on disable-mid-run, and the live-observed Cobrahn navigation
failure above remain open.

## [0.8.0] - 2026-09-13

DungeonTestBotPool (see [ADR-003](docs/architecture/adr-003-dungeon-test-bot-pool.md)):
deterministic, on-demand test bot identities, independent of the general bot population and of any
human staying logged in - Phases 1-3.

### Added
- `DungeonLead::ActiveCanaryCount()` exposed (was file-local to `DungeonLeadCanary.cpp`) so
  `RunTestParty` respects the same `AiPlayerbot.DungeonLead.CanaryMaxConcurrent` budget
  `TriggerTargetedTest()` already did, instead of a second, possibly-divergent count.
- `.playerbots pathcheck <botName> <x> <y> <z>` (SOAP-reachable): dumps `PathGenerator`'s path
  type, actual end position, and a sample of waypoints for the exact call `MoveRouteTo()` makes in
  production.
- `.playerbots testbotpool acquire tank|healer [level]` / `status` / `release <name>` (SOAP-reachable
  GM console commands): reserves an offline character from the existing upstream AddClass bot pool
  (`account_type=2`, ~500 idle characters), logs it in via the masterless `AddPlayerBot(guid, 0)`
  path (no live player session needed - the same path a fully autonomous random bot uses), applies
  a deterministic Tank or Healer talent spec + matching gear, and verifies the role at runtime
  (`PlayerbotAI::IsTank`/`IsHeal`) before ever reporting it `Ready` - a wrong spec-number guess
  surfaces as `Failed`, never as a silently-wrong bot. Verified live end-to-end on the first attempt:
  acquire tank, acquire healer, both `Ready`, release, re-acquire, `Ready` again.
- Rejected two riskier designs first (documented in ADR-003 for anyone tempted to redo this work):
  manually constructing a `WorldSession` to create brand-new characters (too easy to get the
  13-argument constructor or its lifecycle ownership wrong and crash the whole worldserver), and
  adopting AddClass bots under a live player's own account (ties the bot's uptime to that player
  staying logged in - the opposite of "unattended").
- **A `Dps` role** (`.playerbots testbotpool acquire dps <class> [level]`,
  any of the ten playable classes) alongside Tank/Healer, enabling a full 5-bot party
  (Tank + Healer + 3 Dps) built entirely on demand. No talent spec is forced for Dps (any spec is
  valid DPS), but a requested class with a known CC spell is verified to actually know it before
  `Ready` - Mage/Polymorph (spell 118) is the first one implemented; other CC classes
  (Warlock/Druid/Rogue/Hunter) are talent- or ability-gated in ways not yet independently verified
  (see ADR-003). Verified live: a full 5-bot party (Warrior/Priest/Mage/Rogue/Hunter) acquired in
  one sequence, all five `Ready`.

### Changed
- `RunTestParty` builds each `Group` directly (`Group::Create()`+`GroupMgr::AddGroup()`), teleports
  every member to the route's first walkable step (`Player::TeleportTo()`), and calls
  `DungeonLead::StartSession()` itself, instead of queuing bots through LFG and waiting for
  `CanaryTick()` to notice the resulting match - see ADR-003 for why LFG was dropped.

### Fixed
- **`Ready` verification was flaky for Healer** - caught live, not assumed: the same character,
  acquired the same way twice, verified `Ready` once and `Failed` the next time. Root cause:
  `PlayerbotAI::IsTank`/`IsHeal` default to `bySpec=false`, which checks the bot's current AI
  *strategy* (`ContainsStrategy(STRATEGY_TYPE_HEAL)`) rather than the talent spec directly, and
  that strategy assignment lagged behind the talent change `PrepareProfile()` had just applied.
  Fixed by calling both with `bySpec=true`, which reads `AiFactory::GetPlayerSpecTab()` - built
  directly from `bot->GetTalentMap()`, no caching - confirming the exact state `InitTalentsBySpecNo()`
  just set. (The `specNo` guesses themselves - `WARRIOR_TAB_PROTECTION=2`, `PRIEST_TAB_HOLY=1` -
  were correct all along, confirmed against the enum values in `PlayerbotAI.h`; the bug was
  entirely in how readiness was checked, not in the spec applied.)
- **A masterless AddClass bot was silently misclassified as a real human player.**
  `RandomPlayerbotMgr::OnPlayerLogin` (upstream `mod-playerbots`, not this patch's own code) ends
  every login with `if (IsRandomBot(player)) {...} else { players.push_back(player); }` -
  `IsRandomBot()` only recognizes `account_type=1` accounts, so an `account_type=2` AddClass bot
  fell into the `else` branch, the same collection real human logins use elsewhere in that class
  for population/LFG-queue observation. Found by inspection while building DungeonTestBotPool
  (which depends on masterless AddClass logins being classified correctly), fixed with one
  additional branch using the already-existing `IsAddclassBot()` check.
- **CRITICAL: `startdungeon` silently did nothing whenever the tank wasn't already party leader.**
  Found live: taking leadership (needed on essentially every real-world `startdungeon`, since the
  tank usually isn't already leader) makes every bot in the group receive `SMSG_GROUP_LIST`, which
  the base module's `WorldPacketHandlerStrategy` unconditionally maps to `ResetAiAction` ("reset
  botAI" - wipes *all* strategies back to class/spec defaults). `GroupSetLeaderOperation` runs
  asynchronously on the world thread, and each bot processes that packet on its own AI tick,
  entirely decoupled from `startdungeon`'s own timing - so the reset landed a moment after
  `startdungeon` had already applied `+dungeon lead`, silently erasing it. The tank was left
  standing in place forever with no error, no log line - "Dungeon lead: ON" had already printed
  and was already a lie by the time anyone read it. A first attempt fixed this by delaying the
  strategy application by a fixed 2 seconds; correctly rejected in review as still just a smaller
  race condition, not a fix (no fixed delay is long enough under bad network/DB/queue conditions).
  Replaced with `DungeonLead::GuardActiveSessions()`: a small reconciliation loop, called every ~2s
  from `PlayerbotsWorldScript::OnUpdate` (independent of any bot's own Strategy/Engine state, since
  the wipe removes the "dungeon lead" strategy object itself - nothing it owns could ever detect or
  heal its own absence), that unconditionally re-asserts the desired strategy for every session
  `DungeonRouteMgr` considers active, for as long as it stays active. Self-heals regardless of how
  long the external reset takes to land, with no arbitrary timeout to get wrong.

## [0.7.0] - 2026-09-13

L1 (test & observability platform) work, per the architecture roadmap - starting with the parts
that don't need a running worldserver.

### Added
- `startdungeon status`: read-only, no effect on the run - reports current run id, dungeon, step,
  outcome (with domain/reason if `Partial`), paused/debug/test-mode flags, on demand. Part of L1.1/
  L1.3 ("Current Objective, Party Readiness, Run Status" from the roadmap's own eventual UI list -
  useful as a plain chat command well before there's any UI to put it in).
- `data/boss_positions.tsv` and `data/extra_bosses.tsv`: the previously-missing inputs
  `resolve_routes.py` needs, finally checked in - re-running the resolver from a clean checkout now
  reproduces `data/dungeon_routes.csv` byte-for-byte (verified). Deliberately derived FROM the
  already-shipped, already-live-tested `dungeon_routes.csv` rather than a fresh world DB query: an
  early attempt at a fresh per-name DB query picked a *different* spawn than the one already
  verified for names with more than one candidate in the world DB (Wailing Caverns' Lady Anacondra
  has several) - caught by diffing against the committed file before it was ever pushed, reverted,
  and redone the safe way.
- `tools/validate_routes.py` (L1.2, T0 fast validation): checks `data/dungeon_routes.csv` against
  `data/lfg_dungeons.tsv` in well under a second - unknown/invalid lfg_id, map/difficulty mismatch,
  invalid `kind`, duplicate steps, non-finite coordinates, and the single most valuable check: a
  mandatory `boss` step whose position doesn't actually resolve (mirrors `HasPosition()` exactly -
  `entry != 0 && !(x==0 && y==0 && z==0)`), which the runtime would otherwise skip silently with no
  visible failure at all. First real run found 0 errors and 23 informational warnings (all
  expected: `script`-spawned/quest-summon bosses with no static position, one exactly-zero Z axis
  worth a human glance, and a few non-contiguous step numbers that turned out to be `heroic_only`
  rows correctly stripped from the normal-mode data).
  - Caught a real bug in the validator itself before it shipped: the first version flagged 14
    legitimate `script`-spawned bosses' `NULL` coordinates as hard errors, because its own
    "has a position" check was *stricter* than the runtime's actual `HasPosition()` (it rejected
    any single axis equal to `"0"`, where the real check only excludes all three being zero
    together). Fixed to mirror the runtime exactly before trusting its output.
- Two GitHub Actions: `validate-routes.yml` runs the validator on every push/PR touching `data/`;
  `upstream-compat.yml` runs weekly (and on patch changes) and checks `git apply --check` against
  the current mod-playerbots `master`, opening/closing a tracking issue (`upstream-compat` label)
  as an early-warning signal if upstream moves in a way that breaks this patch - it does not keep
  the patch in sync automatically, a failure just means someone needs to look.
- **`startdungeon test` (L1.3, first live smoke-test command).** Same start as plain
  `startdungeon` - no automatic party provisioning, a valid group is still prepared by hand, per
  the roadmap's explicit "don't build that yet" - but reports one structured result line when the
  run reaches a terminal outcome: `DungeonLead Test #<run_id>: <dungeon> -> <outcome>
  [(domain/reason)] | duration Xm Ys | skipped N | manual interventions N`, and a matching
  `test_result` CSV event. `manual interventions` counts `pause`/`continue`/`reset` invoked mid-run
  (a hint that it wasn't a clean, hands-off pass). Deliberately does **not** report deaths/wipes -
  no death/wipe detection exists anywhere in dungeon-lead yet, and inventing one just to fill in a
  report field would be exactly the "new recovery behavior added only to support telemetry" the
  roadmap says not to do; the report says so explicitly rather than implying a false "0".

### Changed
- **`DungeonRouteStep::kind` is now a real `enum class DungeonRouteKind`, not a free-form
  `std::string`.** The DB column itself is untouched (still `VARCHAR`, parsed once at `Load()`);
  a typo there used to silently become a non-walkable, non-mandatory step with zero diagnostic -
  now it's `Unknown`, which is never walkable/mandatory *and* logs a `LOG_ERROR` naming the exact
  lfg_id/step/boss at load time. `tools/validate_routes.py` already catches this ahead of time in
  CI; this is the runtime-side backstop for anything that slips through anyway.

### Fixed
- **`GroupTooSpread` gave no indication of *which* bot it was waiting on.** Found during live
  testing: the leader correctly held position for several minutes because one bot was stuck on
  terrain far behind, but "group too spread" in the log/CSV never said which one, and chat said
  nothing at all (unlike `MasterTooFar`'s "We're waiting for you!"). Added `FindSpreadMember()` and
  a matching one-shot `We're waiting for <name> to catch up!` ping (re-sent if a different bot
  becomes the farthest-behind one), plus the name in the `waiting` CSV/log detail.

## [0.6.0] - 2026-09-13

AutoBot Canary, Stage 0/1 of the design reviewed in `docs/architecture/` - the first slice that
actually runs, deliberately narrow (see that doc for the full staged plan; later stages are not
implemented yet).

### Added
- `DungeonLeadSessionOrigin` (`Manual` / `AutoCanary`) on `DungeonLeadState`, so telemetry, logs,
  and the reconciliation loop can all tell a human-requested run apart from one the canary
  controller started on its own. `ToString()`'d into both the CSV and `LOG_INFO` lines.
- `DungeonLead::StartSession()`: the leadership-takeover/snapshot/strategy-application/state-reset
  tail of `startdungeon`, pulled out into a shared function so the canary controller doesn't
  duplicate it. `StartDungChatShortcutAction::Execute` now just does its own permission/precondition
  checks (group, 5-man, "only the current leader can start this") and calls it - no behavior change
  for the manual chat command, verified by diffing the refactor against the pre-refactor logic
  line-for-line before deploying.
- **AutoBot Canary controller** (`DungeonLeadCanary.h/.cpp`, `DungeonLead::CanaryTick()`, called
  from `PlayerbotsWorldScript::OnUpdate` right next to `GuardActiveSessions()`): watches for
  already-formed, all-bot 5-man groups that queued via the real LFG tool for a dungeon named in
  `AiPlayerbot.DungeonLead.CanaryAllowedLfgIds`, and auto-starts a (`testMode`) dungeon-lead session
  on one, up to `CanaryMaxConcurrent` at a time. Every canary session is force-stopped (leadership
  handed back, outcome recorded) the moment a real player is found in its group, checked every tick
  independent of whether a new session is about to start, and again on `CanaryTimeoutMinutes` if it
  never reaches a terminal outcome. Off by default (`CanaryEnabled=0`) and inert even when enabled
  until dungeons are explicitly allowlisted (`CanaryAllowedLfgIds` empty by default) - a fresh
  checkout of this patch auto-starts nothing.
  - **Deliberately does NOT** actively assemble a party or bypass LFG for a specific requested
    dungeon ("I want to test Wailing Caverns right now, don't make me wait for it to come up by
    chance") - that's a real, separate, larger feature (new bot-selection/grouping/summon code) and
    is intentionally left for a later stage, not rushed into this one. See `DungeonLeadCanary.h`'s
    own comment for the exact scope line.
  - Candidate selection is deterministic when more than one eligible group exists on the same tick
    (lowest `ObjectGuid` wins) rather than "whichever the bot map happens to iterate first" -
    reproducible, easy to reason about in a bug report.
- Updated `DungeonLead::GuardActiveSessions()`'s own comment to document, with the actual AC
  call-chain evidence (`World::Update()` → `sMapMgr->Update()` → `MapUpdater::wait()`, before
  `sScriptMgr->OnWorldUpdate()`), why it's safe for this world-thread function to mutate bot AI
  state that map-update threads also touch - raised as an open question in external review,
  verified by reading AC core rather than left as an assumption.

## [0.5.2] - 2026-09-12

L0 closeout, per the project's architecture roadmap: the remaining runtime-correctness gaps after
0.5.0/0.5.1, before any Level 1 (automated testing) or larger architecture work begins.

### Added
- **Exact session snapshot/restore.** `startdungeon` now records each follower's formation and
  full active strategy set (`PlayerbotAI::GetStrategies`) before changing anything; `stopdungeon`
  restores each follower to exactly that, computing a strategy delta rather than leaving whatever
  dungeon-lead's own +/- changes happened to leave behind. The leader itself gets a full `Reset()`
  on stop (mirroring the `Reset()` already done on start), rather than only stripping the two
  strategies dungeon-lead itself had added - a leader that picked up `+cc`/`+mark rti` at start (it
  does, on its own combat state) previously kept them forever after `stopdungeon`. A follower who
  joined mid-run with no snapshot falls back to the old `chaos`-formation default.
- **`RunOutcome`: a route can no longer silently report "complete" after skipping a mandatory
  boss.** Route steps skipped for being stuck or never found are now checked against a new
  `DungeonRouteStep::IsMandatory()` (currently `kind == "boss"`, kept as one named policy boundary
  rather than the literal comparison repeated at each call site, since a future mandatory door/event
  shouldn't need every caller updated); if any mandatory step was skipped, the run reports
  "route PARTIAL (N stop(s) skipped: ...)" instead of "route complete", and the CSV event is
  `route_partial` instead of `route_complete` so telemetry can tell the two apart without parsing
  chat text.
- **Master dead/disconnected/left-the-party now blocks new pulls and route progress**, not just
  "wait for them to catch up" the way merely-too-far-but-fine does. Previously a dead master was
  explicitly treated as *not* blocking (reasoning: a ghost running back is still "coming"), but the
  project's architecture roadmap frames the real player as part of the run contract - a dead/gone
  player should pause the run, not let it continue attacking things nobody is meaningfully leading.
  Wired into the same wait-gating `isUseful()` uses for other conditions, plus the pull multiplier.
- **Small L1 (test/observability) foundations, added now per the architecture roadmap rather than
  reopening this same code once Level 1 starts:**
  - `DungeonRunOutcome` (`Running`/`Complete`/`Partial`/`Blocked`/`Failed`/`Aborted`) as a real
    enum on `DungeonLeadState`, not just chat-line wording - `Blocked`/`Failed`/`Aborted` are
    reserved for later recovery-manager/test-harness work and not produced yet.
  - `DungeonFailureDomain` (`Navigation`/`PartyCoordination`/`PullPlanning`/`Combat`/`Encounter`/
    `Recovery`/`Infrastructure`) and `DungeonFailureReason` (`PathFailed`/`ObjectiveTimeout`/
    `BossEvade`/`PartyWipe`/`PlayerMissing`/`UnsupportedEvent`/`InternalInvariant`) - only the
    combinations the engine can actually produce today are populated (stuck-skip ->
    Navigation/PathFailed, not-found -> Navigation/ObjectiveTimeout); the rest exist as vocabulary
    for later, not simulated behavior.
  - `run_id`: a per-session correlation id (monotonic per worldserver process), now the second
    column in `DungeonLeadSessions.csv` and prefixed onto every `startdungeon debug` line, so one
    run's rows/lines can be pulled out of a file that interleaves every dungeon-lead bot on the
    server.

## [0.5.1] - 2026-09-12

### Changed
- **Breaking: chat commands renamed `startdung`/`stopdung` -> `startdungeon`/`stopdungeon`**
  (still plain whispers to the leading bot, same mechanism - only the word changed, not how it's
  invoked). Subcommands follow: `startdungeon pause`/`continue`/`reset`/`debug`. Update any macro
  or muscle memory from earlier versions.

## Known limitations (current, not tied to one version)
- Doors/keys/scripted gates (Shadowforge Key, Scarlet Key, Crescent Key, Viewing Room Key, Ring of
  Law, elevators, altars, ...) are annotated in the route data (`kind = door`/`event`) but the
  leader does not yet wait for them — see the README testing-status table for which dungeons this
  affects.
- Bots path via `PathGenerator`/mmaps, which doesn't distinguish terrain a *player* can walk from
  terrain any creature can path across (steep rock, deep water). A bot can end up somewhere the
  real player physically can't follow; the leash mechanism makes it stop and wait there rather than
  run off further, but doesn't relocate it to reachable ground. The real fix is walking the
  precomputed `playerbots_travelnode_path` waypoint polylines node-to-node instead of raw
  point-to-point `PathGenerator` calls between arbitrary boss positions — not done yet.
- Multi-wing dungeons are identified by LFD id when queued via the dungeon finder; walking in on
  foot picks whichever wing's first stop is nearest, which can guess wrong.
- 5 dungeons (Keristrasza, Amnennar the Coldbringer, Princess Theradras, and the two others sharing
  their maps) still use the original hand-authored step order rather than the graph-reordered one —
  see 0.3.0 below.
- `DungeonLeadState` is only ever read/written by its own bot's own AI update, so `DungeonRouteMgr`
  doesn't need per-field locking on top of protecting the `states` map's own structure - see the
  class's own comment. If dungeon-lead code ever reaches into another bot's state from a different
  thread, that assumption needs revisiting.
- `kind` (route step type) is a free-form string, not a validated enum - a typo in the DB silently
  becomes a non-walkable step instead of a load-time error.
- No automated route data validation (duplicate/missing steps, unknown `kind`, non-finite
  coordinates, ...) and no CI - see the project's issue tracker for interest in adding either.

## [0.5.0] - 2026-09-12

Fixes from an external code review of 0.4.0 (see the repo's issue tracker for the full writeup),
verified against the actual source before applying - not applied blind.

### Fixed
- **`debugMode`/`paused`/owned marks were silently wiped seconds after every single `startdungeon`.**
  `DungeonLeadNextAction::ResolveRoute()` called a full `DungeonLeadState::Reset()` the first time
  it ran after a fresh state - which is *every* `startdungeon`, since `lfgId` starts at 0 - undoing
  whatever `startdungeon` had just set (most importantly `DebugDefault`). Split state into
  route-progress fields (cleared by the new `ResetRouteProgress()`) and session fields (debug mode,
  pause, owned marks - survive a route reset, only a real `startdungeon`/`stopdungeon` touches them).
  `startdungeon reset` now uses `ResetRouteProgress()` for the same reason.
- **`stopdungeon` could leave a moon mark stuck forever.** `Stop()` called `ResetState()` (erasing
  `ccGuid`) *before* trying to clean up marks, so it had nothing left to identify which moon mark
  was its own; it also never tracked which creature it skull-marked, so it could only ever clear
  the star icon. Now captures owned skull/moon GUIDs before resetting state, and clears only marks
  it actually placed.
- **`RecordEvent("stop", ...)` was logged after `Stop()` had already erased the state it reads**
  (lfg_id, dungeon name), leaving every "stop" CSV row with an empty dungeon/lfg_id. Reordered to
  log before cleanup in both `stopdungeon` and the automatic "left the instance" stop.
- **A player who left the instance/logged elsewhere no longer counted as "too far".**
  `MasterTooFar()` compared map IDs and returned `false` (not too far) the instant the real player
  wasn't on the same map at all - the opposite of what a leash should do. Now treats "not on the
  same map" as too far.
- **The leash/group-spread check only blocked walking onward, not starting a new fight.**
  `DungeonLeadMultiplier` gated `"dungeon lead next"` on `MasterTooFar()`/`GroupTooSpread()` but not
  `"attack anything"`/`"pull my target"`/`"pull rti target"` - the tank could stand still "waiting
  for you" and still open a brand new pull the moment something wandered into range. Now gates both.
- **Any party member could hand their own bot group leadership via `startdungeon`**, regardless of who
  actually held it, because the leadership-takeover check only asked "is the bot already leader?",
  never "is the person asking currently the leader?". `startdungeon` now refuses unless the requester
  already is the party leader (or the bot already is).
- **CSV escaping wasn't real escaping.** Commas/quotes/newlines in a boss or player name were
  replaced with `;`, silently corrupting the field instead of preserving it. Every field is already
  quote-wrapped by the format string, so the actual fix is just doubling embedded quotes (RFC 4180)
  - commas and newlines inside a quoted field don't need touching at all.
  `DungeonLeadSessions.csv`/`DungeonLeadDebug.log` writes are now also serialized behind a mutex
  (multiple bots' AI updates can run on different map-update threads) and use `localtime_r` instead
  of the not-thread-safe `localtime()`.
- **`DungeonRouteMgr::EnsureLoaded()` read a plain `bool` without synchronization** while `Load()`
  wrote it under a lock - a real data race on concurrent first calls. Replaced with `std::call_once`.
- **Reaching a still-alive boss was treated as completing that route step.** The tank marked a stop
  visited and moved the route on the instant it got within `ArriveDistance`, even though the pull
  itself could still evade, wipe, or simply not happen. Now holds at the stop (one-shot "reached X"
  message) until a later tick's existing dead-creature check actually confirms the kill; if nothing
  at all shows up there after `StuckSeconds`, gives up and moves on rather than parking forever. Bad
  pulls that get skipped this way (here, and the pre-existing stuck-skip case) are now named in the
  "route complete" message instead of silently vanishing from the report.
- `tools/resolve_routes.py` and `tools/routes_md.py` read/wrote their input/output files next to
  the script itself (`tools/`) instead of `data/`, where the files documented in the README and
  "What is in this repo" actually live - the scripts as committed could never actually run.
- Five `member->GetMapId() != bot->GetMapId()` comparisons (group-in-combat/resting/spread checks,
  the debug dump) compared map *IDs*, which doesn't distinguish two different concurrent instances
  of the same dungeon. Switched to `Map*` pointer comparison.
- `tools/reorder_routes.py`/`apply_reorder.py` (the one-off graph-reordering scripts behind the
  0.3.0 route fixes) were referenced by this changelog but never actually committed. Added, with a
  note on the DB-export inputs they need that aren't checked in.
- Data/documentation fixes: Stormwind Stockade's route comment said "west wing first" while the
  listed order was east-then-west; Sunken Temple's Zolo/Mijan had their "N/6" labels swapped
  relative to the listed kill order (also fixed in the live DB, `data/dungeon_routes.csv/.md` and
  `sql/`); the README's opening claim ("works in every 5-man dungeon...") overstated what "route
  data exists" actually means given only one dungeon has been run end-to-end; the Oculus testing-
  status row said "data ready, untested" when the route data itself is Drakos-only.

## [0.4.0] - 2026-09-12

### Fixed
- **Critical: a moon-marked CC target could stay stuck forever, even with the 0.3.0 timeout.** The
  release check (`DungeonLead::CheckCcMark`) was only ever called from
  `DungeonLeadNextAction::isUseful()`, which bails out immediately if the bot is in combat — but a
  target nobody can CC keeps the whole group combat-tagged indefinitely (even a shaman's autonomous
  Searing Totem alone is enough, via the shared threat table), which is exactly the situation the
  timeout exists for. The mob just sat there excluded from DPS targeting while everyone milled
  around it. Moved the check to its own action (`DungeonLeadCcWatchAction`), wired to the base
  module's generic `"often"` trigger, which fires regardless of combat state.
- **Unstick sampling, dialed back.** 0.3.0 widened the fallback probe (see below) to a full 360°
  circle at 8 attempts to fix a pillar-snag case, but this let bots find "technically pathable"
  routes in arbitrary directions with no bias toward the intended corridor — worsening reports of
  bots wandering off-route, off-map, or through walls. Back to a forward-biased ±90° arc, 5
  attempts, a shorter 0.2–0.5x pathfinder distance per probe.
- Inaccurate header comment on all 8 source files: they claimed to be part of mod-playerbots itself
  with an `AUTHORS` file backing copyright, which is wrong for a standalone derivative patch that
  ships no `AUTHORS` file of its own. Replaced with an accurate header referencing this repo's own
  `LICENSE`.

### Added
- Per-instance-ID kill memory: once a routed boss is confirmed dead, that dungeon instance never
  paths back to it again — not after `startdungeon reset`, not after a route re-resolution, and not if
  its corpse/entity later falls outside probe range.
- Always-on structured event log, `DungeonLeadSessions.csv` (one row per run start/stop, boss
  reached, boss already dead, mark placed/released, stuck-skip, waiting — keyed by player, date,
  dungeon, and which bots were in the group). Plain `fopen`/`fprintf` file I/O, independent of
  `worldserver.conf` logger config (the `Appender.*`/`Logger.*` mechanism from 0.2.0 never reliably
  wrote to a dedicated file — abandoned).
- `startdungeon debug`'s verbose log (`DungeonLeadDebug.log`) reimplemented on the same plain-file
  mechanism as the CSV above, replacing the broken logger-config version from 0.2.0.
- Config: `AiPlayerbot.DungeonLead.DebugDefault` (default `0`; only the maintainer's own live
  server overrides it to `1` in its local, uncommitted `playerbots.conf`).

## [0.3.0] - 2026-09-12

First live end-to-end run, on Wailing Caverns.

### Added
- `startdungeon pause` / `startdungeon continue` — freeze/resume walking and pulling without tearing down
  leadership or formations.
- `startdungeon reset` — reset route progress back to the first stop without redoing the leadership
  handoff (useful after a stuck run, without the "AI was reset to defaults" churn a full restart
  causes for every bot in the group).
- `startdungeon debug` — toggle for verbose per-wait diagnostics (position, distance to every group
  member, current step); see 0.4.0 for the logging mechanism it actually shipped with.
- The leading bot marks itself with the star icon on start, and clears it on stop.
- A "heading to X" chat line each time the leader commits to a new route stop.
- A one-shot "We're waiting for you!" ping (not repeated every tick) when the real player falls
  behind the leash distance; the bot stops in place instead of continuing on alone.
- Config: `AiPlayerbot.DungeonLead.{Leash, ArriveDistance, StuckSeconds, CcTimeoutSeconds,
  SkipOptional, MarkCc}`.

### Fixed
- **Route order didn't match the actual travelnode graph.** The first hand-authored order followed
  a human "clearing guide" sequence; three consecutive steps had *no* direct edge in
  `playerbots_travelnode_link` at all, so the bot fell back to raw point-to-point pathing across a
  huge, winding cave and got stuck. Reordered 12 dungeons' free (non-gated) stops by real
  shortest-path distance over the travelnode graph (Dijkstra + nearest-neighbor). 5 candidate
  reorderings were rejected after cross-checking against documented dungeon logic — see
  `tools/reorder_routes.py` output — because they would have put a "must be last" boss
  (Keristrasza, Amnennar the Coldbringer, Princess Theradras) or a "must be after X" boss (Cookie
  after Edwin VanCleef) out of order; those five dungeons are unchanged pending a dependency-aware
  version of the reordering.
- **Gate checks only blocked *new* moves, not one already in flight.** A bot mid-spline toward a
  distant stop kept walking even after the healer sat down to drink or the player fell behind,
  because the gate is only consulted before issuing a new `MoveTo`. Now stops the current motion
  (`Unit::StopMoving()`) the instant a wait condition trips.
- **A moon-marked CC target with nobody able to actually CC it was ignored forever.** The base
  game's own DPS-target-selection logic (`DpsTargetValue`) permanently excludes whatever is
  moon-marked from normal kill priority — so if no CC-capable class is in the group, or the spell
  can't land on that creature type, or it's on cooldown, that mob just sat there alive while the
  rest of the room died around it, and the party moved on without killing it. The mark now expires
  after `CcTimeoutSeconds` if `Unit::HasBreakableByDamageCrowdControlAura()` never confirms an
  actual CC landed, releasing it back to normal DPS targeting. (Turned out to be incomplete — see
  the critical fix in 0.4.0.)
- **Unstick sampling was forward-cone-only and gave up after 2 tries.** A column or wall corner
  directly on the line to a distant destination boxed the bot in on every forward-biased sample.
  Widened to a full 360° circle around the bot, 8 attempts instead of 2, 0.3–1.0x pathfinder
  distance. (Reverted in 0.4.0 — the wide version traded one problem for a worse one.)
- Trash packs never got a shared kill-priority mark unless a boss was nearby — `DpsTargetValue`
  only forces group-wide single-target focus when a skull mark exists, so ordinary trash was
  targeted independently per bot (fine most of the time, riskier in heroics). `startdungeon` now also
  enables the base game's generic `mark rti` strategy (skull on the lowest-HP attacker) in combat,
  with the boss-specific mark kept at higher relevance so a real boss never loses the skull to a
  low-HP add standing next to it.

## [0.2.0] - 2026-09-12

### Added
- `[DungeonLead]`-prefixed progress logging.
- README reframed around what this actually is (an autonomous module, not a scripted macro); added
  as work-in-progress.
- README Prerequisites section, per-dungeon testing-status table, and an initial Debugging section
  (superseded by 0.4.0's plain-file mechanism).
- This CHANGELOG.

## [0.1.0] - 2026-09-12

Initial release.

### Added
- Dungeon Lead feature: `startdungeon`/`stopdungeon` chat commands, `DungeonLeadStrategy`,
  route-following via `playerbots_dungeon_route`, boss/CC marking, formation `leader`.
- Hand-authored + DB-resolved boss routes for all 64 base LFD entries (96 incl. heroics), sourced
  from Classic-era wiki/Icy Veins/Wowhead pages — see `data/routes.tsv` for the source per dungeon.
- GPL-2.0 license (derivative of mod-playerbots, itself GPL-2.0-or-later).

### Fixed
- Whisper shortcut used the wrong slash command (`/p` instead of `/w`) for talking to the tank bot.
