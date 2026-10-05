# Per-dungeon testing status

Live runs use a bot-only test party (`DungeonTestBotPool`) at the dungeon's LFG target level on
the current build. Dungeons not listed in the matrix have route data validated against the
world/travelnode databases but no live run yet. "Has a door/gate" means the route has a
`door`/`event` step; the leader waits at a closed door (up to `DoorWaitSeconds`) but never opens
one itself, so key/lever doors still end the objective. See `data/routes.tsv` for the exact step.

## Live validation (2026-10-04/05)

Best run per dungeon. Two passes: 25 min per dungeon (all 11), then up to 60 min for the Tier 1
dungeons plus RFK, SM Library and RFD. Test parties stop after 45 min (`canary_timeout`), which
ended some of the long runs. OK = exercised and behaved correctly, `-` = not exercised.

| Dungeon | Bosses | Pulls | Recovery | Wipe | Door | Result |
|---|---|---|---|---|---|---|
| Ragefire Chasm | 4/4 | OK | OK (escalated) | OK (1) | - | complete, 41 min |
| Deadmines | 6/7 (to Edwin VanCleef; Cookie optional) | OK | OK | - | OK (Ironclad Cove) | 45 min test cap after VanCleef |
| Wailing Caverns | 6 (Anacondra, Kresh, Cobrahn, Verdan, Serpentis, Pythas) | OK | OK | OK (1) | - | 45 min test cap; one `leader_unstuck` |
| Shadowfang Keep | 6 (to Fenrus) | OK | OK | OK (1) | OK (Courtyard) | 45 min test cap; one 25 min run hit the wipe limit at the Moonwalker pack |
| Razorfen Kraul | 4 (Roogug, Aggem, Ramtusk, Jargba) | OK | OK | OK (1) | - | 45 min test cap |
| SM Graveyard | 2 (Vishas, Thalnos) | OK | - | - | - | complete; rare spawns not present were skipped as optional |
| SM Library | 2/2 | OK | - | - | - | complete, 24 min |
| Razorfen Downs | 3 (Mordresh, Glutton, Amnennar) | OK | OK | - | - | complete, 39 min; Ragglesnout (rare) not present |
| SM Armory | 0 | OK | - | - | - | 25 min cap (369 yd of trash before Herod) |
| SM Cathedral | 0 | OK | - | - | - | 25 min cap |
| Zul'Farrak | 0 | OK | - | - | - | 25 min cap |

Pace: trash is cleared pack by pack (~30 s per pack incl. the post-combat gate), so a full clear
takes 25-60 minutes.

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
| Vanilla | Ragefire Chasm | Live-tested, full route completed (see matrix) |
| Vanilla | Razorfen Downs | Live-tested, full route completed (see matrix) — has a door/gate |
| Vanilla | Razorfen Kraul | Live-tested, partial (see matrix) |
| Vanilla | Scarlet Monastery - Armory | Live-tested, partial (see matrix) |
| Vanilla | Scarlet Monastery - Cathedral | Live-tested, partial (see matrix) — has a door/gate |
| Vanilla | Scarlet Monastery - Graveyard | Live-tested, full route completed (see matrix) |
| Vanilla | Scarlet Monastery - Library | Live-tested, full route completed (see matrix) |
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
