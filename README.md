# mod-playerbots: Dungeon Lead

> **⚠️ Work in progress.** Under active development and testing on a live server. Behavior, chat
> commands and config keys can still change between commits.

AzerothCore module for [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
(3.3.5a). The tank bot leads a 5-man dungeon on its own: it becomes group leader, the other bots
follow *it* instead of the player, it walks the boss route, pulls, marks the kill target (skull)
and a CC target (moon), and paces the group by healer mana and spread — the real player in the
group just rides along (or fights, without giving orders).

Ships as a standalone module (drop it into `modules/`), not a patch against mod-playerbots'
source — no pinned commit to go stale, nothing to re-apply after a mod-playerbots update.

**What it covers:** every 5-man dungeon in Vanilla/TBC/WotLK (normal + heroic). Raids: no.
Event/vehicle dungeons (Violet Hold, Culling of Stratholme, Oculus past Drakos, Trial of the
Champion, Halls of Reflection, Black Morass, Old Hillsbrad): no route, falls back to plain "grind
what you see." Doors opened by an event or a boss are waited for; the leader doesn't use keys,
levers or clickable objects itself. Live runs so far cover a handful of Vanilla dungeons; the rest
has route data but no live run - [docs/testing-status.md](docs/testing-status.md) is the current
per-dungeon status.

## Install

1. Clone into `modules/mod-dungeon-lead/` inside your AzerothCore checkout, next to
   `modules/mod-playerbots`:
   `git clone https://github.com/Sugyn/mod-playerbots-dungeon-lead.git modules/mod-dungeon-lead`
2. Re-run CMake (`cmake ..`) — module directories are only picked up at configure time.
3. Build + install `worldserver` as usual.
4. Load the route data: `mysql acore_playerbots < sql/playerbots_dungeon_route.sql`
   (updating an existing install: apply the new files in `sql/updates/` instead)
5. Restart `worldserver`.

Needs mod-playerbots already built and working, mmaps generated for the maps you'll use this on
(bots won't path without them), and an `acore_playerbots` DB connection. Config is optional — see
`conf/mod-dungeon-lead.conf.dist` for every key and its default; nothing there is required to
install.

## Usage

Whisper (or `/w`) the tank bot inside the dungeon:

| command | effect |
|---|---|
| `startdungeon` | takes group leadership, sets the others to formation `leader`, enables `cc`, marks itself with the star icon, starts walking the route |
| `startdungeon pause` / `continue` | holds in place / resumes |
| `startdungeon reset` | resets route progress back to the first stop |
| `startdungeon debug` | toggles verbose diagnostics to a log file (see "Debugging / reporting a bug" below) |
| `startdungeon test` | same as plain `startdungeon`, reports one result line when the run ends |
| `startdungeon status` | reports the current route/step/outcome, read-only |
| `stopdungeon` | ends the run, restores everyone's leadership/formation/strategies |

The bot reports what it's doing in chat and, at the end, whether the route actually completed or
was partial — don't take "done" at face value without checking which one it said. Leaving the
instance stops the mode automatically.

How it runs:

- It only starts once the tank is actually group leader, and `stopdungeon` checks leadership
  really went back to you (both retried a few times, then given up on and reported).
- Between fights it waits until the party is ready: you alive and within `Leash` (60 yd), the
  healer alive and in the instance with mana, nobody dead, drinking or below half health, and
  the party together (`PartySoftRange`/`PartyHardRange`).
- It pulls one pack at a time: skull on the main target, cross on the second, moon on something
  to crowd-control, then opens the fight. A pull that doesn't take is retried; after that an
  optional stop is skipped and reported, while a boss is tried once more and otherwise the run
  stops as partial - a boss is never skipped silently. It doesn't chase runners away from the
  fight (`CombatLeashRadius`).
- A closed door in the way is waited for (`DoorWaitSeconds`) until its event or boss opens it.
- If something won't sort itself out (a straggler, a lost or dead member) it tries to fix it -
  walks back, waits for a resurrection, brings a *bot* member over - and stops the run after a
  few minutes rather than waiting forever. You are never teleported.
- After a tank death it resumes from the last cleared pack.
- Bots of a running session are kept inside the dungeon: other systems can't teleport them out
  mid-run (logged as `unexpected_teleport`).

`startdungeon status` shows the current state, objective, pack and pull.

## Debugging / reporting a bug

Two files next to `Playerbots.log`, no logger config needed:

- **`DungeonLeadSessions.csv`** — always on. One row per key event per run; this is what lets you
  see what actually happened without reproducing anything live.
- **`DungeonLeadDebug.log`** — opt-in. Whisper `startdungeon debug` before reproducing, whisper it
  again when done.

**To report a bug:** reproduce with `startdungeon debug` on, then attach both files to a GitHub
issue — the [bug report template](../../issues/new/choose) asks for exactly this and won't let you
submit without it. A description of what it looked like on screen isn't enough to act on.

Questions, comparisons with other mods, feature ideas → [Discussions](../../discussions). Issues
are for reproducible bugs only.

## Known limits / in progress

- Live coverage is small; see [docs/testing-status.md](docs/testing-status.md).
- Doors that need a key, a lever or a click (and NPC talks, elevators) aren't operated by the
  leader; such a step fails after `DoorWaitSeconds`.
- Route stops are bosses; trash in between is fought where the walk runs into it, pack by pack,
  so a full clear takes 25-60 minutes.
- GM diagnostic commands from the pre-module patch (`testbotpool`, `lfgstate`, `pathcheck`,
  `pathcheckfrom`, `tpbot`) aren't ported to this module's `.dungeonlead` command root yet — only
  `canarytest` is.
- Live runs use bot-only test parties; a run with a real player in the group hasn't been done
  since the leader became a state machine.
- Optional advanced features, off by default: **AutoBot Canary** (auto-starts on bot-only groups
  that queue on their own) and **DungeonTestBotPool** (deterministic tank/healer test bots on
  demand, not yet reachable via a command in this module).

## License

GPL-2.0-or-later — same as mod-playerbots, of which this is a derivative work. See `LICENSE`.
