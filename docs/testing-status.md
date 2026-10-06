# Per-dungeon testing status

Live runs use a bot-only test party (`DungeonTestBotPool`) at the dungeon's LFG target level on
the current build, started with `.dungeonlead validate` (see `tools/live_validation/README.md`). Dungeons not listed in the matrix have route data validated against the
world/travelnode databases but no live run yet. "Has a door/gate" means the route has a
`door`/`event` step; the leader opens a door the way a player can (hand lock, key, lever on its
side) or waits for it (up to `DoorWaitSeconds`). See `data/routes.tsv` for the exact step.

## Live validation (2026-10-04/05)

Best run per dungeon (25 to 70 min runs, 2026-10-04/05). Event steps (use/talk) and door opening
are on since 2026-10-05; Deadmines, Shadowfang Keep and Zul'Farrak were run with them.
OK = exercised and behaved correctly, `-` = not exercised.
Levels (definitions in `tools/live_validation/README.md`): Smoke, Partial, Full route, Verified
(full route twice without a failure that needed a fix), Blocked. Verified so far: Razorfen Downs
(see below).

## Verified campaign (2026-10-06)

Each dungeon twice, four parties at a time (`ValidationParallel=4`), on build `ca3d077`:

| Dungeon | Run 1 | Run 2 | Note |
|---|---|---|---|
| Razorfen Downs | complete | complete | **Verified** - the only skip is Ragglesnout (rare spawn, absent) |
| SM Library | complete | killed by the campaign | campaign bug, fixed in `5380a13` |
| SM Armory | killed by the campaign | complete | same campaign bug |
| SM Cathedral | complete | partial, 4 wipes | not classified yet |
| Ragefire Chasm | partial (Bazzalan not found) | complete | Taragaman wipes on the cultist packs |
| Deadmines | complete (Greenskin, Cookie skipped: stuck) | 70 min cap | |
| Zul'Farrak | 70 min cap | complete | |
| Shadowfang Keep | partial, 4 wipes | partial (Cell Door 18935 on the way back) | GM-observed, see below |

Shadowfang Keep, GM-observed on the next build (2026-10-06): the walk from the Courtyard Door to
Razorclaw crossed the courtyard and pulled three packs at once; the tank stood still while fel
steeds beat the healer 55 yd from the combat anchor (leash); upstream's loot actions outrank the
route walk, so the leader looted every corpse first and once walked 135 yd back to old corpses at
the cells (then waited at Cell Door 18935). Fixed in the commit after `5380a13` (courtyard packs as
optional steps, leash lets the tank reach a mob on a party member, no looting during a session) -
not live-validated yet.

| Dungeon | Level | Bosses | Pulls | Recovery | Wipe | Door | Result |
|---|---|---|---|---|---|---|---|
| Ragefire Chasm | Full route | 4/4 | OK | OK (escalated) | OK (1) | - | complete, 41 min |
| Deadmines | Full route | 7/7 (to VanCleef and Cookie) | OK | OK | - | OK (Heavy Doors opened by the leader, gunpowder + cannon blow the Iron Clad Door) | complete, 38 min, no wipe |
| Wailing Caverns | Partial | 6 (Anacondra, Kresh, Cobrahn, Verdan, Serpentis, Pythas) | OK | OK | OK (1) | - | 45 min test cap; one `leader_unstuck` |
| Shadowfang Keep | Full route | 8/8 (to Archmage Arugal) | OK | OK | OK (3) | OK (prisoner talked to after his cell's lever, Courtyard Door, Sorcerer and Arugal doors) | complete, 46 min |
| Razorfen Kraul | Partial | 4 (Roogug, Aggem, Ramtusk, Jargba) | OK | OK | OK (1) | - | 45 min test cap |
| SM Graveyard | Full route | 2 (Vishas, Thalnos) | OK | - | - | - | complete; rare spawns not present were skipped as optional |
| SM Library | Full route | 2/2 | OK | - | - | - | complete, 24 min |
| Razorfen Downs | Full route | 3 (Mordresh, Glutton, Amnennar) | OK | OK | - | - | complete, 39 min; Ragglesnout (rare) not present |
| SM Armory | Full route | 1/1 (Herod) | OK | - | - | - | complete, 21 min |
| SM Cathedral | Full route | 3/3 (Fairbanks, Mograine, Whitemane) | OK | OK | - | - | complete, 26 min |
| Zul'Farrak | Full route | 7 (Theka, Antu'sul, Zum'rah, Executioner, Bly, Ukorz, Velratha) | OK | OK | - | OK (Zum'rah's area trigger, Executioner's Key looted, cage, stairs waves fought beside the crew, Weegli then Bly talked to, End Door) | complete, 55 min, no wipe; Gahz'rilla (Mallet summon) not routed |

Pace: trash is cleared pack by pack (~30 s per pack incl. the post-combat gate), so a full clear
takes 25-60 minutes.

64 base dungeons; heroics share their normal counterpart's route/status.

| Expansion | Dungeon | Status |
|---|---|---|
| Vanilla | Blackfathom Deeps | Data ready, untested — has a door/gate |
| Vanilla | Blackrock Depths - Prison | Data ready, untested — has a door/gate |
| Vanilla | Blackrock Depths - Upper City | Data ready, untested — has a door/gate |
| Vanilla | Coren Direbrew | Data ready, untested — has a door/gate |
| Vanilla | Deadmines | Live: Full route (see matrix) — has a door/gate |
| Vanilla | Dire Maul - East | Data ready, untested — has a door/gate |
| Vanilla | Dire Maul - North | Data ready, untested — has a door/gate |
| Vanilla | Dire Maul - West | Data ready, untested — has a door/gate |
| Vanilla | Gnomeregan | Data ready, untested — has a door/gate |
| Vanilla | Lower Blackrock Spire | Data ready, untested |
| Vanilla | Maraudon - Orange Crystals | Data ready, untested |
| Vanilla | Maraudon - Pristine Waters | Data ready, untested |
| Vanilla | Maraudon - Purple Crystals | Data ready, untested |
| Vanilla | Ragefire Chasm | Live: Full route (see matrix) |
| Vanilla | Razorfen Downs | Live: Full route (see matrix) — has a door/gate |
| Vanilla | Razorfen Kraul | Live: Partial (see matrix) |
| Vanilla | Scarlet Monastery - Armory | Live: Full route (see matrix) |
| Vanilla | Scarlet Monastery - Cathedral | Live: Full route (see matrix) — has a door/gate |
| Vanilla | Scarlet Monastery - Graveyard | Live: Full route (see matrix) |
| Vanilla | Scarlet Monastery - Library | Live: Full route (see matrix) |
| Vanilla | Scholomance | Data ready, untested — has a door/gate |
| Vanilla | Shadowfang Keep | Live: Full route (see matrix) — has a door/gate |
| Vanilla | Stormwind Stockade | Data ready, untested |
| Vanilla | Stratholme - Main Gate | Data ready, untested — has a door/gate |
| Vanilla | Stratholme - Service Entrance | Data ready, untested — has a door/gate |
| Vanilla | Sunken Temple | Data ready, untested |
| Vanilla | The Crown Chemical Co. | Data ready, untested — has a door/gate |
| Vanilla | The Headless Horseman | Data ready, untested — has a door/gate |
| Vanilla | Uldaman | Data ready, untested — has a door/gate |
| Vanilla | Wailing Caverns | Live: Partial (see matrix) |
| Vanilla | Zul'Farrak | Live: Full route (see matrix) — has a door/gate |
| TBC | Auchenai Crypts | Data ready, untested |
| TBC | Blood Furnace | Data ready, untested — has a door/gate |
| TBC | Hellfire Ramparts | Data ready, untested |
| TBC | Magisters' Terrace | Data ready, untested |
| TBC | Mana-Tombs | Data ready, untested |
| TBC | Sethekk Halls | Data ready, untested |
| TBC | Shadow Labyrinth | Data ready, untested — has a door/gate |
| TBC | Shattered Halls | Data ready, untested — has a door/gate |
| TBC | Slave Pens | Data ready, untested |
| TBC | The Arcatraz | Data ready, untested — has a door/gate |
| TBC | The Black Morass | No route (event/vehicle dungeon) |
| TBC | The Botanica | Data ready, untested |
| TBC | The Escape From Durnholde | No route (event/vehicle dungeon) |
| TBC | The Frost Lord Ahune | Data ready, untested — has a door/gate |
| TBC | The Mechanar | Data ready, untested — has a door/gate |
| TBC | The Steamvault | Data ready, untested — has a door/gate |
| TBC | Underbog | Data ready, untested |
| WotLK | Ahn'kahet: The Old Kingdom | Data ready, untested |
| WotLK | Azjol-Nerub | Data ready, untested |
| WotLK | Drak'Tharon Keep | Data ready, untested — has a door/gate |
| WotLK | Gundrak | Data ready, untested — has a door/gate |
| WotLK | Halls of Lightning | Data ready, untested |
| WotLK | Halls of Reflection | No route (event/vehicle dungeon) |
| WotLK | Halls of Stone | Data ready, untested — has a door/gate |
| WotLK | Pit of Saron | Data ready, untested — has a door/gate |
| WotLK | The Culling of Stratholme | No route (event/vehicle dungeon) |
| WotLK | The Forge of Souls | Data ready, untested |
| WotLK | The Nexus | Data ready, untested |
| WotLK | The Oculus | Partial route — Drakos the Interrogator only; unsupported after that (vehicle/drake section) |
| WotLK | Trial of the Champion | No route (event/vehicle dungeon) |
| WotLK | Utgarde Keep | Data ready, untested |
| WotLK | Utgarde Pinnacle | Data ready, untested — has a door/gate |
| WotLK | Violet Hold | No route (event/vehicle dungeon) |
