# Dungeon Lead for mod-playerbots

An [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) module for
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) (WotLK 3.3.5a). A tank bot leads a
5-man dungeon on its own: it takes the group lead, walks the route boss by boss, pulls one pack at a
time, marks the kill and crowd-control targets, waits for the healer and the party, opens doors and
runs the events a route needs, and recovers from stragglers and wipes. The other bots follow the
tank; a real player in the group rides along or fights, without having to give orders.

The module only decides the party's objective. Spell rotations, threat, healing and positioning in
a fight stay with mod-playerbots' class AI. It is a standalone module: mod-playerbots itself is not
patched.

## Status

**Alpha.** Behaviour, chat commands and config keys can still change between releases.

- Live-tested with bot-only parties only; no run with a real player in the group has been made yet.
- Route data exists for every 5-man dungeon of Classic, TBC and WotLK (normal and heroic). Live
  results exist for 16 of them - see the table below.
- Event and vehicle dungeons (The Black Morass, Old Hillsbrad, The Culling of Stratholme, Violet
  Hold, Trial of the Champion, Halls of Reflection, The Oculus after Drakos) have no executable
  route.

Tested with:

| Component | Revision |
|---|---|
| AzerothCore ([mod-playerbots fork](https://github.com/mod-playerbots/azerothcore-wotlk), branch `Playerbot`) | `e7e2e288e` |
| mod-playerbots | `405f8119` |

mod-playerbots revisions before its config member rename (`MaxWaitForMove`,
`NonCombatStrategies`, ...) do not build with this version.

## Install

1. Clone into the `modules/` directory of your AzerothCore checkout, next to `mod-playerbots`:
   `git clone https://github.com/Sugyn/mod-playerbots-dungeon-lead.git modules/mod-dungeon-lead`
2. Re-run CMake: module directories are picked up at configure time.
3. Build and install `worldserver` as usual.
4. Load the route table into the playerbots database:
   `mysql acore_playerbots < modules/mod-dungeon-lead/sql/playerbots_dungeon_route.sql`
   (an existing install applies the new files in `sql/updates/`, see its README).
5. Restart `worldserver`. The route table is read at startup.

Requirements: a working mod-playerbots, generated mmaps for the dungeons you use (bots cannot path
without them) and the `acore_playerbots` database.

## Usage

Whisper the tank bot inside the dungeon:

| Command | Effect |
|---|---|
| `startdungeon` | takes the group lead, sets the others to follow it, marks itself with the star and starts the route |
| `startdungeon pause` / `continue` | holds in place / resumes |
| `startdungeon reset` | restarts the route from the first stop; refused once the run has ended |
| `startdungeon status` | reports route, step and outcome, read-only |
| `startdungeon test` | like `startdungeon`, and reports one result line when the run ends |
| `startdungeon debug` | toggles verbose diagnostics to `DungeonLeadDebug.log` |
| `stopdungeon` | ends the run and restores everyone's lead, formation and strategies |

Only the current group leader (or the tank itself) can hand the lead to the tank. At the end the
bot says whether the route was completed or only partially done. Leaving the instance stops the
run.

GM / console commands:

| Command | Effect |
|---|---|
| `.dungeonlead validate <lfgId> [lfgId...]` | runs the listed dungeons with fresh bot-only test parties, one result line each ([tools/live_validation](tools/live_validation/README.md)) |
| `.dungeonlead validate status` / `stop` | campaign status / stop and release the test bots |
| `.dungeonlead canarytest <lfgId> [groups]` | makes idle bots queue for a dungeon so the AutoBot Canary takes the groups over (needs `CanaryEnabled` and the dungeon in `CanaryAllowedLfgIds`) |

## How a run goes

- The run starts once the tank is observed as group leader, and `stopdungeon` waits until the lead
  is observed back; both are retried a few times, then given up and reported.
- Between fights the leader waits until the party is ready: the player within `Leash`, the healer
  alive, in reach and with mana, nobody dead, eating or drinking, the party within
  `PartySoftRange` / `PartyHardRange`.
- One pack at a time: skull on the main target, cross on the second, moon on a mob a party bot can
  crowd-control. The tank pulls from range and holds the fight at that spot, within
  `CombatLeashRadius` (`BossLeashRadius` at a boss). Patrols are let past first.
- A pull that does not take is retried. Optional content is then skipped and reported; a boss or
  required objective gets more rounds (`ObjectiveRetryRounds`) and otherwise the run ends partial.
  Nothing mandatory is skipped silently.
- Closed doors are opened the way a player can (by hand, with a key a bot carries, with a lever)
  or waited for (`DoorWaitSeconds`). `use` and `talk` route steps run dungeon events.
- Stragglers are walked back to and, if they stay stuck, bot members are brought to the leader.
  A real player is never teleported. After a wipe the run resumes from the last cleared pack, up to
  `MaxWipesPerRun`.

Design: [docs/architecture.md](docs/architecture.md).

## Configuration

All keys are optional; [conf/mod-dungeon-lead.conf.dist](conf/mod-dungeon-lead.conf.dist) lists
every key with its default and range. Put the keys you change into
`etc/modules/mod-dungeon-lead.conf` (copy of the `.dist` file) or any other config file the
worldserver loads. The keys keep the `AiPlayerbot.DungeonLead.` prefix.

Most often changed:

| Key | Default | Meaning |
|---|---|---|
| `AiPlayerbot.DungeonLead.HealerManaPct` | 20 | below this healer mana %, the leader holds instead of walking on or pulling |
| `AiPlayerbot.DungeonLead.Leash` | 60 | how far the real player may fall behind before the leader waits |
| `AiPlayerbot.DungeonLead.SkipOptional` | 0 | 1 = skip optional trash pulls (never bosses, required trash, doors or events) |
| `AiPlayerbot.DungeonLead.MarkCc` | 1 | place the moon on a crowd-control target |
| `AiPlayerbot.DungeonLead.MaxWipesPerRun` | 3 | wipes before the run is given up |
| `AiPlayerbot.DungeonLead.DebugDefault` | 0 | 1 = `startdungeon debug` on from the start |
| `AiPlayerbot.DungeonLead.CanaryEnabled` | 0 | AutoBot Canary: take over bot-only LFG groups of allowed dungeons |
| `AiPlayerbot.DungeonLead.ValidationSimPlayer` | 0 | test tool: 1/2 = a validation party's first dps / healer plays a simulated player (`ValidationSimPlayerSeed`) |
| `AiPlayerbot.DungeonLead.ValidationAutostart` | "" | LFG ids of a validation campaign started 2 min after server start |
| `AiPlayerbot.DungeonLead.ValidationRepeat` | 1 | passes over a validation campaign's dungeon list; 0 = until stopped |

## Dungeons with live results

Bot-only test parties (warrior tank, priest healer, three dps) at the dungeon's LFG level. These
results were recorded on earlier AzerothCore and mod-playerbots revisions; the full list and the
level definitions are in [docs/dungeons.md](docs/dungeons.md).

| Dungeon | Result |
|---|---|
| Deadmines | Verified |
| Ragefire Chasm | Verified |
| Shadowfang Keep | Verified (latest run partial) |
| Slave Pens | Verified |
| Zul'Farrak | Verified (modeled route) |
| Blood Furnace | Verified (earlier contract) |
| Hellfire Ramparts | Verified (earlier contract) |
| Razorfen Downs | Verified (earlier contract) |
| Scarlet Monastery - Armory | Verified (earlier contract) |
| Scarlet Monastery - Cathedral | Verified (earlier contract) |
| Scarlet Monastery - Library | Verified (earlier contract) |
| Scarlet Monastery - Graveyard | Full route |
| Mana-Tombs | Full route (latest runs partial) |
| Underbog | Full route (latest run failed) |
| Wailing Caverns | Partial |
| Razorfen Kraul | Partial |

## Telemetry and bug reports

Written next to `Playerbots.log`, no logger configuration needed:

- `DungeonLeadEvents.v2.jsonl` - every event of every run as structured JSON
  ([schema](docs/telemetry-schema-v2.md)); [tools/run_replay](tools/run_replay/README.md) turns a
  run into a timeline, a map and a verdict, and compares runs.
- `DungeonLeadSessions.csv` / `DungeonLeadRuns.csv` - one row per event / per finished run.
- `DungeonLeadDebug.log` - only while `startdungeon debug` is on.

To report a bug, reproduce it with `startdungeon debug` on and attach these files to an issue; the
[bug report form](../../issues/new/choose) asks for them. Questions and ideas go to
[Discussions](../../discussions).

## Known limits

- No live run with a real player in the party yet; all results come from bot-only parties.
- Elevators, escorts and vehicle sections are not executed; such dungeons end their route early.
- Trash between bosses is fought where the walk meets it, pack by pack: a full clear takes
  25-60 minutes.
- Where mmaps give no path, the leader falls back to the route's earlier stops and short probes;
  a step that still cannot be reached fails through the objective rules.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

GPL-2.0-or-later, like mod-playerbots, of which this is a derivative work. See [LICENSE](LICENSE).
