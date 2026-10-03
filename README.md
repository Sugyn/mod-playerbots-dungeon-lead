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
what you see." Doors and gated bosses: route data marks them, the leader doesn't wait for them yet.
Only **one dungeon (Wailing Caverns) has actually been run live end-to-end** — everything else has
route data but is untested; see [docs/testing-status.md](docs/testing-status.md) for the full
per-dungeon list.

## Install

1. Clone into `modules/mod-dungeon-lead/` inside your AzerothCore checkout, next to
   `modules/mod-playerbots`:
   `git clone https://github.com/Sugyn/mod-playerbots-dungeon-lead.git modules/mod-dungeon-lead`
2. Re-run CMake (`cmake ..`) — module directories are only picked up at configure time.
3. Build + install `worldserver` as usual.
4. Load the route data: `mysql acore_playerbots < sql/playerbots_dungeon_route.sql`
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

It only starts once the tank is actually group leader (it retries the leader change a few times
and gives up if it doesn't happen), and `stopdungeon` likewise checks that leadership really went
back to you.

It holds off pulling/walking on while: the real player is dead/disconnected/out of the group, the
group is in combat, the healer is dead, out of the instance, low on mana or drinking, another party
member is dead, or the player has fallen more than `AiPlayerbot.DungeonLead.Leash` yards behind
(config, default 60).

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

- Only Wailing Caverns is live-verified; see [docs/testing-status.md](docs/testing-status.md).
- Doors/gated bosses aren't waited for yet (Gnomeregan, Uldaman Ironaya, Nexus, Halls of Stone, ...).
- GM diagnostic commands from the pre-module patch (`testbotpool`, `lfgstate`, `pathcheck`,
  `pathcheckfrom`, `tpbot`) aren't ported to this module's `.dungeonlead` command root yet — only
  `canarytest` is.
- A full live run through the module itself (not just that it registers correctly) isn't
  confirmed yet.
- Optional advanced features, off by default: **AutoBot Canary** (auto-starts on bot-only groups
  that queue on their own) and **DungeonTestBotPool** (deterministic tank/healer test bots on
  demand, not yet reachable via a command in this module).

## License

GPL-2.0-or-later — same as mod-playerbots, of which this is a derivative work. See `LICENSE`.
