# Per-dungeon testing status

Live runs use a bot-only test party (`DungeonTestBotPool`) at the dungeon's LFG target level,
25 minutes per dungeon, on the current build. Dungeons not listed in the matrix have route data
validated against the world/travelnode databases but no live run yet. "Has a door/gate" means the
route has a `door`/`event` step; the leader now waits at a closed door (up to `DoorWaitSeconds`)
but never opens one itself, so key/lever doors still end the objective. See `data/routes.tsv`
for the exact step.

## Live validation (2026-10-04)

Bosses = killed within the 25 min cap. Every column: OK = exercised and behaved correctly,
`-` = not exercised in that run.

| Dungeon | Bosses | Route | Pulls | Recovery | Wipe | Door | Result |
|---|---|---|---|---|---|---|---|
| Ragefire Chasm | 2/4 (Oggleflint, Taragaman) | OK | OK | OK (straggler escalated) | - | - | cap |
| Deadmines | 3 (Rhahk'Zor, Sneed's Shredder, Gilnid) | OK | OK | - | - | - | cap |
| Wailing Caverns | 2 (Lady Anacondra, Kresh) | OK | OK | - | - | - | cap |
| Shadowfang Keep | 4 (Rethilgore, Razorclaw, Silverlaine, Springvale) | OK | OK | OK (escalated) | OK (1 wipe recovered) | OK (Courtyard) | cap; a second run hit the wipe limit (4 wipes at the Moonwalker pack before Razorclaw) |
| Razorfen Kraul | 2 (Roogug, Aggem Thorncurse) | OK | OK | OK (escalated) | OK | - | cap |
| SM Graveyard | 2 (Vishas, Thalnos) | OK | OK | - | - | - | complete; rare spawns not present were skipped as optional |
| SM Library | 1 (Houndmaster Loksey) | path failure to Arcanist Doan (leader ended under the floor) | OK | OK | - | - | cap, Doan objective in retry |
| SM Armory | 0 | OK | OK | - | - | - | cap (369 yd of trash before Herod) |
| SM Cathedral | 0 | OK | OK | - | - | - | cap |
| Razorfen Downs | 2 (Mordresh Fire Eye, Glutton) | OK | OK | OK (escalated) | - | - | cap |
| Zul'Farrak | 0 | OK | OK | - | - | - | cap |

What limits these runs is pace: trash is cleared pack by pack (~30 s per pack incl. the
post-combat gate), so a full clear needs 40-60 minutes in most of these dungeons.

64 base dungeons; heroics share their normal counterpart's route/status.

| Expansion | Dungeon | Status |
|---|---|---|
| Vanilla | Blackfathom Deeps | Data ready, untested — has a door/gate |
| Vanilla | Blackrock Depths - Prison | Data ready, untested — has a door/gate |
| Vanilla | Blackrock Depths - Upper City | Data ready, untested — has a door/gate |
| Vanilla | Coren Direbrew | Data ready, untested — has a door/gate |
| Vanilla | Deadmines | Live-tested, partial (see matrix) — has a door/gate |
| Vanilla | Dire Maul - East | Data ready, untested — has a door/gate |
| Vanilla | Dire Maul - North | Data ready, untested — has a door/gate |
| Vanilla | Dire Maul - West | Data ready, untested — has a door/gate |
| Vanilla | Gnomeregan | Data ready, untested — has a door/gate |
| Vanilla | Lower Blackrock Spire | Data ready, untested |
| Vanilla | Maraudon - Orange Crystals | Data ready, untested |
| Vanilla | Maraudon - Pristine Waters | Data ready, untested |
| Vanilla | Maraudon - Purple Crystals | Data ready, untested |
| Vanilla | Ragefire Chasm | Live-tested, partial (see matrix) |
| Vanilla | Razorfen Downs | Live-tested, partial (see matrix) — has a door/gate |
| Vanilla | Razorfen Kraul | Live-tested, partial (see matrix) |
| Vanilla | Scarlet Monastery - Armory | Live-tested, partial (see matrix) |
| Vanilla | Scarlet Monastery - Cathedral | Live-tested, partial (see matrix) — has a door/gate |
| Vanilla | Scarlet Monastery - Graveyard | Live-tested, partial (see matrix) |
| Vanilla | Scarlet Monastery - Library | Live-tested, partial (see matrix) |
| Vanilla | Scholomance | Data ready, untested — has a door/gate |
| Vanilla | Shadowfang Keep | Live-tested, partial (see matrix) — has a door/gate |
| Vanilla | Stormwind Stockade | Data ready, untested |
| Vanilla | Stratholme - Main Gate | Data ready, untested — has a door/gate |
| Vanilla | Stratholme - Service Entrance | Data ready, untested — has a door/gate |
| Vanilla | Sunken Temple | Data ready, untested |
| Vanilla | The Crown Chemical Co. | Data ready, untested — has a door/gate |
| Vanilla | The Headless Horseman | Data ready, untested — has a door/gate |
| Vanilla | Uldaman | Data ready, untested — has a door/gate |
| Vanilla | Wailing Caverns | Live-tested, partial (see matrix) |
| Vanilla | Zul'Farrak | Live-tested, partial (see matrix) — has a door/gate |
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
