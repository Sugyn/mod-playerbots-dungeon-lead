# Changelog

All notable changes to this patch. Format is loosely [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

Work in progress — see README "Testing status".

### Added
- Initial Dungeon Lead feature: `startdung`/`stopdung` chat commands, `DungeonLeadStrategy`,
  route-following via `playerbots_dungeon_route`, boss/CC marking, formation `leader`.
- Hand-authored + DB-resolved boss routes for all 64 base LFD entries (96 incl. heroics), sourced
  from Classic-era wiki/Icy Veins/Wowhead pages — see `data/routes.tsv` for the source per dungeon.
- `startdung pause` / `startdung continue` — freeze/resume walking and pulling without tearing down
  leadership or formations.
- `startdung reset` — reset route progress back to the first stop without redoing the leadership
  handoff (useful after a stuck run, without the "AI was reset to defaults" churn a full restart
  causes for every bot in the group).
- `startdung debug` — toggles verbose per-wait diagnostics (position, distance to every group
  member, current step) to a dedicated `DungeonLeadDebug.log`, so a bug report can ship a self-
  contained log instead of a description of what it looked like on screen.
- The leading bot marks itself with the star icon on start, and clears it on stop.
- A "heading to X" chat line each time the leader commits to a new route stop.
- A one-shot "We're waiting for you!" ping (not repeated every tick) when the real player falls
  behind the leash distance; the bot stops in place instead of continuing on alone.
- Config: `AiPlayerbot.DungeonLead.{HealerManaPct, Leash, ArriveDistance, StuckSeconds,
  CcTimeoutSeconds, SkipOptional, MarkCc, DebugDefault}`.
- Always-on structured event log, `DungeonLeadSessions.csv` (one row per run start/stop, boss
  reached, boss already dead, mark placed/released, stuck-skip, waiting — keyed by player, date,
  dungeon and which bots were in the group), so an admin can see across many players' runs which
  dungeons/steps actually cause trouble without asking anyone to write anything up. Plain file I/O,
  independent of `worldserver.conf` logger config — see README "Debugging".
- Per-instance-ID kill memory: once a routed boss is confirmed dead, that instance never paths back
  to it again (e.g. after `startdung reset`, or a route mismatch triggering re-resolution), even if
  its corpse/entity later falls outside probe range.

### Fixed (found during live testing on Wailing Caverns)
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
  actual CC landed, releasing it back to normal DPS targeting.
- **A moon-marked CC target could stay ignored forever even with the timeout above.** The release
  check (`DungeonLead::CheckCcMark`) was only ever called from `DungeonLeadNextAction::isUseful()`,
  which bails out immediately if the bot is in combat — but a marked target that nobody can CC stays
  in combat indefinitely (a shaman's autonomous Searing Totem alone is enough to keep the whole
  party combat-tagged through the shared threat table), which is exactly the situation the timeout
  exists for. The mob just sat there excluded from DPS targeting while everyone milled around it.
  Moved the check to its own action (`DungeonLeadCcWatchAction`) wired to the base module's generic
  `"often"` trigger, which fires regardless of combat state, instead of the combat-gated one.
- **Unstick sampling was widened, then that made things worse.** Originally forward-cone-only,
  2 attempts, 0.5-1.0x pathfinder distance (mirroring upstream `NewRpgBaseAction::MoveFarTo`) — a
  column or wall corner directly on the line to a distant destination boxed the bot in on every
  sample. Widened to a full 360° circle, 8 attempts, 0.3-1.0x distance to fix that, but this let the
  bot find "technically pathable" routes in arbitrary directions with no bias toward the intended
  corridor, worsening reports of bots wandering off-route/off-map/through walls. Dialed back to a
  forward-biased ±90° arc, 5 attempts, a shorter 0.2-0.5x distance — more retries than upstream, but
  each one a short, cautious probe instead of a long blind reach.
- Trash packs never got a shared kill-priority mark unless a boss was nearby — `DpsTargetValue`
  only forces group-wide single-target focus when a skull mark exists, so ordinary trash was
  targeted independently per bot (fine most of the time, riskier in heroics). `startdung` now also
  enables the base game's generic `mark rti` strategy (skull on the lowest-HP attacker) in combat,
  with the boss-specific mark kept at higher relevance so a real boss never loses the skull to a
  low-HP add standing next to it.

### Known limitations
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
