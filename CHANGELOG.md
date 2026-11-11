# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are `MAJOR.MINOR.PATCH-alpha`
while the module is in alpha.

## [Unreleased]

### Fixed

- A route target that died away from its spot in an earlier fight (it ran for help, or joined a fight as an add) no longer keeps the leader waiting at the empty spot.

## [0.15.0-alpha] - 2026-11-10

### Added

- A registration probe logs a few minutes after start whether the module's strategy, action and overrides work for every class.
- New runs record their run context (module, AzerothCore and mod-playerbots commits, route, config and mmap digests) in the telemetry.
- `AiPlayerbot.DungeonLead.ValidationAutostart` starts a validation campaign automatically after a server start.
- Position samples list the followers that lag far behind the leader.
- Telemetry events carry structured payloads for objectives, checkpoints, wipes, recoveries and interactions.
- The campaign heartbeat reports how long the session guard passes take.
- `tools/check_route_sql.py` checks that the route CSV and the SQL table agree.
- `tools/route_insert.py` writes a numbered route update together with its rollback.
- CI runs the kernel tests, the replay tests and the route validation on every change.

### Changed

- Builds against mod-playerbots after its config member rename (tested with AzerothCore `e7e2e288e` and mod-playerbots `405f8119`).
- Trash is pulled from range and the tank holds the fight at the spot it pulled from.
- A route that does not model the whole dungeon says so when it completes.
- A dungeon counts as Verified only after two full-route runs on the same run context.
- The route table ships as one complete SQL file; updates follow a numbered file-plus-rollback convention.
- Route notes are short neutral descriptions.

### Fixed

- A new pull waits for a healer that can actually reach and see the tank.
- The moon goes only to a mob that a bot in the party can crowd-control, and a kept moon is no longer dropped while the caster is busy.
- The route walk follows the path it checked instead of a straight line, also for short hops.
- A route step can no longer be walked forever without progress.
- A pull waiting for the party or for a patrol holds the tank in place instead of walking it into the pack.
- A mob that flees for help is stopped before it brings the next pack.
- Each strategy a session owns is repaired on its own, and never one the bot's class does not have.
- A queued leadership change can no longer outlive the request that made it.
- `startdungeon pause` stops the leader where it is.
- A finished run is not reopened by `startdungeon reset`.
- A key held only by a real player is asked for instead of being used through that player's character.
- `SkipOptional=1` skips optional trash only, never required trash, doors or events.
- A required door that is missing from its spot no longer counts as passed unless the instance saved it open.
- Use and talk steps count as done only once their effect is seen in the world.
- A boss killed during another step's fight is remembered, and clearing one trash pack no longer skips other packs of the same creature.
- Waits for eating, drinking or healing no longer run through a fight and abort the run afterwards.
- Recovery for a missing, dead or far-away real player and for endless waits is bounded in manual runs.
- Offline group members and an offline master stay visible to the readiness checks.
- A door is opened with the lever closest to it.
- After a wipe on a long route the party walks back along the path anchors and resumes after the last safe checkpoint.
- A session whose leader stays out of the world, loses its group or disconnects is closed out and its followers restored.
- Every run writes exactly one terminal result, also when the leadership handback is still pending.
- Telemetry lines that never reach their file are counted and reported.
- Bots outside a Dungeon Lead session keep mod-playerbots' normal "unknown dungeon" behaviour.

### Removed

- Internal planning and history documents; the design is summarised in `docs/architecture.md` and live results in `docs/dungeons.md`.

## [0.14.0-alpha] - 2026-10-06

Summary of 0.1.0 (2026-09-12) to 0.14.0-alpha.

### Added

- `startdungeon` / `stopdungeon` with `pause`, `continue`, `reset`, `status`, `test` and `debug`, and hand-authored routes for every 5-man dungeon of Classic, TBC and WotLK.
- A leader state machine with readiness checks, pull lifecycle, target plan, combat leash, bounded recovery, wipe checkpoints and boss anchors.
- Door, lever, key, `use` and `talk` handling for the dungeon events a route needs.
- Exact restore of every member's formation and strategies when a run stops.
- AutoBot Canary (off by default), deterministic test bots and `.dungeonlead validate` for unattended validation campaigns.
- Always-on telemetry (`DungeonLeadSessions.csv`, `DungeonLeadRuns.csv`, schema v2 events) and `tools/run_replay` for timelines, maps and verdicts.

### Changed

- Converted from a patch against mod-playerbots to a standalone AzerothCore module (0.10.0).
- Chat commands renamed from `startdung` / `stopdung` to `startdungeon` / `stopdungeon` (0.5.1).

### Fixed

- Crowd-control marks that never landed are released, also during combat.
- Leadership changes are confirmed in the game state instead of assumed.
- Bots of a running session can no longer be teleported out of the dungeon by other systems.
