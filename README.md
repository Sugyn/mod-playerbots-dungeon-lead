# mod-playerbots: Dungeon Lead

> **⚠️ Work in progress.** Under active development and testing on a live server. Behavior, chat
> commands and config keys can still change between commits. Known limitations are tracked below
> and in `CHANGELOG.md` — read those before reporting something already listed there.

**Autonomous dungeon module for [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
(AzerothCore 3.3.5a).** The tank bot takes the lead of a 5-man dungeon on its own: it becomes group leader, the
other bots follow *it* instead of the player, it walks the boss route, decides what to pull next, marks the kill
target (skull) and a CC target (moon), and paces the group by healer mana and group spread — while the real player
in the group simply rides along (or fights, without having to give any orders).

Works in every 5-man dungeon of Vanilla, The Burning Crusade (normal + heroic) and Wrath of the Lich King
(normal + heroic). Raids are explicitly excluded. Event/vehicle dungeons (Violet Hold, Culling of Stratholme,
Oculus after Drakos, Trial of the Champion, Halls of Reflection, Black Morass, Old Hillsbrad) have no route and fall
back to plain "grind what you see".

## Prerequisites

- **AzerothCore** with **mod-playerbots** already built and working (bots can be added to a group and fight).
  This patch was written and tested against mod-playerbots `master` @ `b949b50b` — API it relies on
  (`MovementAction`/`NewRpgBaseAction`, `FormationValue`, `RtiTargetValue`, `PlayerbotOperations::GroupSetLeaderOperation`,
  `Map::IsNonRaidDungeon`) is fairly stable but not guaranteed identical on a much older or newer checkout.
- **mmaps generated** for the continents/instances you want to use this in. Without mmaps, `PathGenerator`-based
  movement (which this entire feature is built on) silently fails or produces nonsense paths — if bots won't move
  or wander into walls, check mmaps before opening an issue here.
- A working **`acore_playerbots`** database connection (the module's own requirement; this patch adds one table to it).
- **Re-run CMake**, not just `make` — the module globs its source tree at configure time, so a fresh
  `src/Ai/Base/DungeonLead/` directory won't be picked up by an existing build cache.
- Route positions (`data/dungeon_routes.csv` → `sql/playerbots_dungeon_route.sql`) were resolved against a
  standard/Blizzlike-ish AzerothCore world database. A heavily customized world DB (moved spawns, different
  creature entries for the same boss) may need positions re-resolved — see `tools/resolve_routes.py`.
- No RBAC/GM permission is required for `startdung`/`stopdung`/etc. — any player commanding their own bot can use
  them, same as any other mod-playerbots chat command.

## In-game usage

Whisper (or /w) the tank bot inside the dungeon:

| command | effect |
|---|---|
| `startdung` | bot takes group leadership, sets every other bot to formation `leader` (follow the group leader), enables `cc` on all bots, marks itself with the star icon, and starts walking the route |
| `startdung pause` | stops walking/pulling in place; everything else (leadership, marks, combat) stays as-is |
| `startdung continue` | resumes after a pause |
| `startdung reset` | resets route progress back to the first stop, without redoing leadership/formations |
| `startdung debug` | toggles verbose per-wait diagnostics to a dedicated log file, for bug reports (see "Debugging" below) |
| `stopdung` | ends the run: leadership goes back to you, formations reset to `chaos`, star/marks cleared, route state cleared |

The bot reports what it does ("heading to Lady Anacondra", "reached Lady Anacondra", "can't reach X, skipping",
"route complete", "We're waiting for you!" when you fall behind). Leaving the instance switches the mode off
automatically.

Rules the leader follows before walking on or pulling:
- nobody in the group is in combat,
- no healer below `AiPlayerbot.DungeonLead.HealerManaPct` (default 20 %) and nobody sitting (eating/drinking),
- the real player is within `AiPlayerbot.DungeonLead.Leash` yards (default 60) — the bot stops in place and sends
  one "We're waiting for you!" ping rather than wandering off; bots within 1.5× that.
- a moon-marked CC target that nobody actually manages to crowd-control within `AiPlayerbot.DungeonLead.CcTimeoutSeconds`
  (default 10s) is released back into normal kill priority instead of being ignored forever.

## Debugging (for bug reports)

If something goes wrong on a run, whisper the leading bot `startdung debug` before reproducing it. This turns on a
verbose, per-wait log line (bot position, current step, distance to every group member) written to its own file,
`DungeonLeadDebug.log`, instead of drowning in the shared `Playerbots.log`. To enable the file (one-time, in
`worldserver.conf` next to the existing `Appender.Playerbots`/`Logger.playerbots` lines):

```
Appender.DungeonLeadDebug=2,5,0,DungeonLeadDebug.log,w
Logger.playerbots.dungeonlead=5,Console DungeonLeadDebug
```

Reproduce the issue, then whisper `startdung debug` again to turn it off, and attach `DungeonLeadDebug.log` (it's
small and self-contained — every line is prefixed `[DungeonLead]`) to your GitHub issue. This is far more useful
than a description of what it looked like on screen.

## Testing status

One dungeon has been played through live end-to-end; the rest have their route data generated and validated against
the world/travelnode databases but have **not** been run in-game yet. "Unhandled door/gate" means the route has at
least one `door`/`event` step (a key, a lever, a scripted encounter) that the leader does not yet wait for — it may
walk up to a locked door and get stuck/skip past it. See `data/routes.tsv` for exactly which step.

<details>
<summary>Per-dungeon status (64 base dungeons; heroics share their normal counterpart's route/status)</summary>

| Expansion | Dungeon | Status |
|---|---|---|
| Vanilla | Blackfathom Deeps | Data ready, untested — has an unhandled door/gate |
| Vanilla | Blackrock Depths - Prison | Data ready, untested — has an unhandled door/gate |
| Vanilla | Blackrock Depths - Upper City | Data ready, untested — has an unhandled door/gate |
| Vanilla | Coren Direbrew | Data ready, untested — has an unhandled door/gate |
| Vanilla | Deadmines | Data ready, untested — has an unhandled door/gate |
| Vanilla | Dire Maul - East | Data ready, untested — has an unhandled door/gate |
| Vanilla | Dire Maul - North | Data ready, untested — has an unhandled door/gate |
| Vanilla | Dire Maul - West | Data ready, untested — has an unhandled door/gate |
| Vanilla | Gnomeregan | Data ready, untested — has an unhandled door/gate |
| Vanilla | Lower Blackrock Spire | Data ready, untested |
| Vanilla | Maraudon - Orange Crystals | Data ready, untested |
| Vanilla | Maraudon - Pristine Waters | Data ready, untested |
| Vanilla | Maraudon - Purple Crystals | Data ready, untested |
| Vanilla | Ragefire Chasm | Data ready, untested |
| Vanilla | Razorfen Downs | Data ready, untested — has an unhandled door/gate |
| Vanilla | Razorfen Kraul | Data ready, untested |
| Vanilla | Scarlet Monastery - Armory | Data ready, untested |
| Vanilla | Scarlet Monastery - Cathedral | Data ready, untested — has an unhandled door/gate |
| Vanilla | Scarlet Monastery - Graveyard | Data ready, untested |
| Vanilla | Scarlet Monastery - Library | Data ready, untested |
| Vanilla | Scholomance | Data ready, untested — has an unhandled door/gate |
| Vanilla | Shadowfang Keep | Data ready, untested — has an unhandled door/gate |
| Vanilla | Stormwind Stockade | Data ready, untested |
| Vanilla | Stratholme - Main Gate | Data ready, untested — has an unhandled door/gate |
| Vanilla | Stratholme - Service Entrance | Data ready, untested — has an unhandled door/gate |
| Vanilla | Sunken Temple | Data ready, untested |
| Vanilla | The Crown Chemical Co. | Data ready, untested — has an unhandled door/gate |
| Vanilla | The Headless Horseman | Data ready, untested — has an unhandled door/gate |
| Vanilla | Uldaman | Data ready, untested — has an unhandled door/gate |
| Vanilla | Wailing Caverns | In testing (live) — see CHANGELOG |
| Vanilla | Zul'Farrak | Data ready, untested — has an unhandled door/gate |
| TBC | Auchenai Crypts | Data ready, untested |
| TBC | Blood Furnace | Data ready, untested — has an unhandled door/gate |
| TBC | Hellfire Ramparts | Data ready, untested |
| TBC | Magisters' Terrace | Data ready, untested |
| TBC | Mana-Tombs | Data ready, untested |
| TBC | Sethekk Halls | Data ready, untested |
| TBC | Shadow Labyrinth | Data ready, untested — has an unhandled door/gate |
| TBC | Shattered Halls | Data ready, untested — has an unhandled door/gate |
| TBC | Slave Pens | Data ready, untested |
| TBC | The Arcatraz | Data ready, untested — has an unhandled door/gate |
| TBC | The Black Morass | No route (event/vehicle dungeon) |
| TBC | The Botanica | Data ready, untested |
| TBC | The Escape From Durnholde | No route (event/vehicle dungeon) |
| TBC | The Frost Lord Ahune | Data ready, untested — has an unhandled door/gate |
| TBC | The Mechanar | Data ready, untested — has an unhandled door/gate |
| TBC | The Steamvault | Data ready, untested — has an unhandled door/gate |
| TBC | Underbog | Data ready, untested |
| WotLK | Ahn'kahet: The Old Kingdom | Data ready, untested |
| WotLK | Azjol-Nerub | Data ready, untested |
| WotLK | Drak'Tharon Keep | Data ready, untested — has an unhandled door/gate |
| WotLK | Gundrak | Data ready, untested — has an unhandled door/gate |
| WotLK | Halls of Lightning | Data ready, untested |
| WotLK | Halls of Reflection | No route (event/vehicle dungeon) |
| WotLK | Halls of Stone | Data ready, untested — has an unhandled door/gate |
| WotLK | Pit of Saron | Data ready, untested — has an unhandled door/gate |
| WotLK | The Culling of Stratholme | No route (event/vehicle dungeon) |
| WotLK | The Forge of Souls | Data ready, untested |
| WotLK | The Nexus | Data ready, untested |
| WotLK | The Oculus | Data ready, untested |
| WotLK | Trial of the Champion | No route (event/vehicle dungeon) |
| WotLK | Utgarde Keep | Data ready, untested |
| WotLK | Utgarde Pinnacle | Data ready, untested — has an unhandled door/gate |
| WotLK | Violet Hold | No route (event/vehicle dungeon) |

</details>

## What is in this repo

## What is in this repo

| path | content |
|------|---------|
| `mod-playerbots-dungeon-lead.patch` | full `git diff` against upstream `master` (`b949b50b`) — 8 new files + 11 touched |
| `src/DungeonLead/` | the new sources on their own (`src/Ai/Base/DungeonLead/` in the module) |
| `sql/playerbots_dungeon_route.sql` | route table for the `acore_playerbots` database (408 steps, 96 LFD entries incl. heroics) |
| `data/routes.tsv` | hand-authored boss order per LFD entry with source per dungeon (Classic-era wiki / Icy Veins Classic / Wowhead TBC) |
| `data/dungeon_routes.csv` / `.md` | resolved routes (creature entry + spawn position from the world DB) |
| `data/lfg_dungeons.tsv` | `LFGDungeons.dbc` dump (id, name, level range, map, difficulty, type, expansion) |
| `tools/resolve_routes.py` | resolves `routes.tsv` names against DB dumps and derives the heroic entries |
| `tools/routes_md.py` | renders the Markdown overview |

## How it works (short)

- `DungeonLeadStrategy` ("dungeon lead") is added to both engines by `startdung`. Non-combat triggers:
  `dungeon lead idle` → walk to the next route stop (relevance 3.5, i.e. *below* grind's "attack anything" 4.0 and
  food/drink), `dungeon lead boss near` → mark skull/moon, `dungeon lead left instance` → auto stop.
- `DungeonLeadMultiplier` zeroes "attack anything" / "pull my target" / walking while the healer is low or someone
  is drinking, and walking while anyone is in combat.
- `DungeonRouteMgr` loads `playerbots_dungeon_route`; the route is picked by the group's LFD id
  (`sLFGMgr->GetDungeon`), falling back to the nearest route on the map/difficulty when the group walked in.
- Movement mirrors `NewRpgBaseAction::MoveFarTo` (mmap path to the real destination, walk to the furthest reachable
  waypoint, cone-sample when blocked) but a stop that makes no progress for `StuckSeconds` is **skipped**, never
  teleported to.
- A new formation `leader` (`FollowFormation` targeting `"group leader"`) makes the other bots follow the bot leader.
- `UnknownDungeonTrigger` (the upstream "I don't know this dungeon, lead the way!" hand-back) is bypassed while
  `dungeon lead` or `grind` is active.

## Install

1. Apply `mod-playerbots-dungeon-lead.patch` in the module directory (`git apply`), or copy `src/DungeonLead/` to
   `src/Ai/Base/DungeonLead/` and re-do the registrations from the patch.
2. Re-run CMake (the module globs its sources at configure time), build, install.
3. `mysql acore_playerbots < sql/playerbots_dungeon_route.sql`
4. Optional config keys (defaults shown): `AiPlayerbot.DungeonLead.HealerManaPct = 20`, `.Leash = 60`,
   `.ArriveDistance = 8`, `.StuckSeconds = 45`, `.SkipOptional = 0`, `.MarkCc = 1`.

## Known limits / next steps

- Doors and gated bosses (e.g. Gnomeregan, Uldaman Ironaya, Nexus, Halls of Stone) are only annotated in the route
  data (`kind = door`), the leader does not yet wait for them.
- Multi-wing dungeons are identified by LFD id; walking in on foot picks the wing whose first stop is nearest.
- Boss order was verified against 3.3.x-era sources; the few judgement calls (Stockade order, Sunken Temple troll
  order, BRD Prison order) are documented in `data/routes.tsv`.

## License

GPL-2.0-or-later — same as mod-playerbots, of which this is a derivative work. See `LICENSE`.
