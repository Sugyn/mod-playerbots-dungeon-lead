# mod-playerbots: Dungeon Lead

Patch for [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) (AzerothCore 3.3.5a) that lets a bot —
normally the tank — **lead a 5-man dungeon while a real player is in the group**: it becomes group leader, the other
bots follow *it* instead of the player, and it walks a hand-authored boss route, pulling on the way, marking the boss
(skull) and a CC target (moon), and holding back while the healer is low on mana or the group is spread out.

Works in every 5-man dungeon of Vanilla, The Burning Crusade (normal + heroic) and Wrath of the Lich King
(normal + heroic). Raids are explicitly excluded. Event/vehicle dungeons (Violet Hold, Culling of Stratholme,
Oculus after Drakos, Trial of the Champion, Halls of Reflection, Black Morass, Old Hillsbrad) have no route and fall
back to plain "grind what you see".

## In-game usage

Whisper (or /p) the tank bot inside the dungeon:

| command     | effect |
|-------------|--------|
| `startdung` | bot takes group leadership, sets every other bot to formation `leader` (follow the group leader), enables `cc` on all bots, and starts walking the route |
| `stopdung`  | leadership goes back to you, formations reset to `chaos`, route state cleared |

The bot reports what it does ("route 'Wailing Caverns', 9 stops", "reached Lady Anacondra", "can't reach X, skipping",
"route complete"). Leaving the instance switches the mode off automatically.

Rules the leader follows before walking on or pulling:
- nobody in the group is in combat,
- no healer below `AiPlayerbot.DungeonLead.HealerManaPct` (default 20 %) and nobody sitting (eating/drinking),
- the real player is within `AiPlayerbot.DungeonLead.Leash` yards (default 60), bots within 1.5× that.

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
