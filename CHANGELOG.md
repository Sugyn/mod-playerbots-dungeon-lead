# Changelog

All notable changes to this patch, by version and date. Format is [Keep a Changelog](https://keepachangelog.com/).
Versioning is pre-1.0 (0.MINOR.PATCH) while this is under active development against a single live
test dungeon — see README "Testing status" for what's actually been run in-game.

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
- `stopdung` restores followers to formation `chaos` and clears the strategies dungeon-lead added,
  rather than recording and restoring each member's actual pre-`startdung` strategy/formation set.
  In practice this is a reasonable default, not a currently-known bug, but it isn't a byte-for-byte
  restore.
- `kind` (route step type) is a free-form string, not a validated enum - a typo in the DB silently
  becomes a non-walkable step instead of a load-time error.
- No automated route data validation (duplicate/missing steps, unknown `kind`, non-finite
  coordinates, ...) and no CI - see the project's issue tracker for interest in adding either.

## [0.5.0] - 2026-09-12

Fixes from an external code review of 0.4.0 (see the repo's issue tracker for the full writeup),
verified against the actual source before applying - not applied blind.

### Fixed
- **`debugMode`/`paused`/owned marks were silently wiped seconds after every single `startdung`.**
  `DungeonLeadNextAction::ResolveRoute()` called a full `DungeonLeadState::Reset()` the first time
  it ran after a fresh state - which is *every* `startdung`, since `lfgId` starts at 0 - undoing
  whatever `startdung` had just set (most importantly `DebugDefault`). Split state into
  route-progress fields (cleared by the new `ResetRouteProgress()`) and session fields (debug mode,
  pause, owned marks - survive a route reset, only a real `startdung`/`stopdung` touches them).
  `startdung reset` now uses `ResetRouteProgress()` for the same reason.
- **`stopdung` could leave a moon mark stuck forever.** `Stop()` called `ResetState()` (erasing
  `ccGuid`) *before* trying to clean up marks, so it had nothing left to identify which moon mark
  was its own; it also never tracked which creature it skull-marked, so it could only ever clear
  the star icon. Now captures owned skull/moon GUIDs before resetting state, and clears only marks
  it actually placed.
- **`RecordEvent("stop", ...)` was logged after `Stop()` had already erased the state it reads**
  (lfg_id, dungeon name), leaving every "stop" CSV row with an empty dungeon/lfg_id. Reordered to
  log before cleanup in both `stopdung` and the automatic "left the instance" stop.
- **A player who left the instance/logged elsewhere no longer counted as "too far".**
  `MasterTooFar()` compared map IDs and returned `false` (not too far) the instant the real player
  wasn't on the same map at all - the opposite of what a leash should do. Now treats "not on the
  same map" as too far.
- **The leash/group-spread check only blocked walking onward, not starting a new fight.**
  `DungeonLeadMultiplier` gated `"dungeon lead next"` on `MasterTooFar()`/`GroupTooSpread()` but not
  `"attack anything"`/`"pull my target"`/`"pull rti target"` - the tank could stand still "waiting
  for you" and still open a brand new pull the moment something wandered into range. Now gates both.
- **Any party member could hand their own bot group leadership via `startdung`**, regardless of who
  actually held it, because the leadership-takeover check only asked "is the bot already leader?",
  never "is the person asking currently the leader?". `startdung` now refuses unless the requester
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
  paths back to it again — not after `startdung reset`, not after a route re-resolution, and not if
  its corpse/entity later falls outside probe range.
- Always-on structured event log, `DungeonLeadSessions.csv` (one row per run start/stop, boss
  reached, boss already dead, mark placed/released, stuck-skip, waiting — keyed by player, date,
  dungeon, and which bots were in the group). Plain `fopen`/`fprintf` file I/O, independent of
  `worldserver.conf` logger config (the `Appender.*`/`Logger.*` mechanism from 0.2.0 never reliably
  wrote to a dedicated file — abandoned).
- `startdung debug`'s verbose log (`DungeonLeadDebug.log`) reimplemented on the same plain-file
  mechanism as the CSV above, replacing the broken logger-config version from 0.2.0.
- Config: `AiPlayerbot.DungeonLead.DebugDefault` (default `0`; only the maintainer's own live
  server overrides it to `1` in its local, uncommitted `playerbots.conf`).

## [0.3.0] - 2026-09-12

First live end-to-end run, on Wailing Caverns.

### Added
- `startdung pause` / `startdung continue` — freeze/resume walking and pulling without tearing down
  leadership or formations.
- `startdung reset` — reset route progress back to the first stop without redoing the leadership
  handoff (useful after a stuck run, without the "AI was reset to defaults" churn a full restart
  causes for every bot in the group).
- `startdung debug` — toggle for verbose per-wait diagnostics (position, distance to every group
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
  targeted independently per bot (fine most of the time, riskier in heroics). `startdung` now also
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
- Dungeon Lead feature: `startdung`/`stopdung` chat commands, `DungeonLeadStrategy`,
  route-following via `playerbots_dungeon_route`, boss/CC marking, formation `leader`.
- Hand-authored + DB-resolved boss routes for all 64 base LFD entries (96 incl. heroics), sourced
  from Classic-era wiki/Icy Veins/Wowhead pages — see `data/routes.tsv` for the source per dungeon.
- GPL-2.0 license (derivative of mod-playerbots, itself GPL-2.0-or-later).

### Fixed
- Whisper shortcut used the wrong slash command (`/p` instead of `/w`) for talking to the tank bot.
