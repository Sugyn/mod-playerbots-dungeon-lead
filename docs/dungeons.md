# Dungeons

Live results of bot-only test parties (warrior tank, priest healer, three dps, LFG target level),
judged by `tools/run_replay`. Heroics share their normal route. No run with a real player in the
party has been made yet. The results below were recorded on earlier AzerothCore and mod-playerbots
revisions (the latest campaigns on AzerothCore `f19a18799` and mod-playerbots `037c0141`); for the
revisions this release is pinned to, see the 0.15.0 smoke result at the end.

| Level | Meaning |
|---|---|
| Verified | two full-route runs on the same build, route table, config and maps (run context) |
| Verified (earlier contract) | two full-route runs on the same module build, recorded before the run context existed |
| Verified (modeled route) | Verified, but the route skips content it cannot execute |
| Full route | at least one run reached the route end with every boss and required objective done |
| Partial | real route progress and at least one boss killed, route end not reached |
| Route data only | route validated against the world database, no live run |
| No route | event or vehicle dungeon, not supported |

| Expansion | Dungeon | Live result | Notes |
|---|---|---|---|
| Classic | Blackfathom Deeps | Route data only |  |
| Classic | Blackrock Depths - Prison | Route data only |  |
| Classic | Blackrock Depths - Upper City | Route data only |  |
| Classic | Coren Direbrew | Route data only |  |
| Classic | Deadmines | Verified |  |
| Classic | Dire Maul - East | Route data only |  |
| Classic | Dire Maul - North | Route data only |  |
| Classic | Dire Maul - West | Route data only |  |
| Classic | Gnomeregan | Route data only |  |
| Classic | Lower Blackrock Spire | Route data only |  |
| Classic | Maraudon - Orange Crystals | Route data only |  |
| Classic | Maraudon - Pristine Waters | Route data only |  |
| Classic | Maraudon - Purple Crystals | Route data only |  |
| Classic | Ragefire Chasm | Verified |  |
| Classic | Razorfen Downs | Verified (earlier contract) |  |
| Classic | Razorfen Kraul | Partial |  |
| Classic | Scarlet Monastery - Armory | Verified (earlier contract) |  |
| Classic | Scarlet Monastery - Cathedral | Verified (earlier contract) |  |
| Classic | Scarlet Monastery - Graveyard | Full route |  |
| Classic | Scarlet Monastery - Library | Verified (earlier contract) |  |
| Classic | Scholomance | Route data only |  |
| Classic | Shadowfang Keep | Verified | latest run partial |
| Classic | Stormwind Stockade | Route data only |  |
| Classic | Stratholme - Main Gate | Route data only |  |
| Classic | Stratholme - Service Entrance | Route data only |  |
| Classic | Sunken Temple | Route data only |  |
| Classic | The Crown Chemical Co. | Route data only |  |
| Classic | The Headless Horseman | Route data only |  |
| Classic | Uldaman | Route data only |  |
| Classic | Wailing Caverns | Partial |  |
| Classic | Zul'Farrak | Verified (modeled route) | the route does not model every event of the dungeon |
| TBC | Auchenai Crypts | Route data only |  |
| TBC | Blood Furnace | Verified (earlier contract) |  |
| TBC | Hellfire Ramparts | Verified (earlier contract) |  |
| TBC | Magisters' Terrace | Route data only |  |
| TBC | Mana-Tombs | Full route | latest two runs partial (wipe limit) |
| TBC | Sethekk Halls | Route data only |  |
| TBC | Shadow Labyrinth | Route data only |  |
| TBC | Shattered Halls | Route data only |  |
| TBC | Slave Pens | Verified |  |
| TBC | The Arcatraz | Route data only |  |
| TBC | The Black Morass | No route |  |
| TBC | The Botanica | Route data only |  |
| TBC | The Escape From Durnholde | No route |  |
| TBC | The Frost Lord Ahune | Route data only |  |
| TBC | The Mechanar | Route data only |  |
| TBC | The Steamvault | Route data only |  |
| TBC | Underbog | Full route | latest run failed |
| WotLK | Ahn'kahet: The Old Kingdom | Route data only |  |
| WotLK | Azjol-Nerub | Route data only |  |
| WotLK | Drak'Tharon Keep | Route data only |  |
| WotLK | Gundrak | Route data only |  |
| WotLK | Halls of Lightning | Route data only |  |
| WotLK | Halls of Reflection | No route |  |
| WotLK | Halls of Stone | Route data only |  |
| WotLK | Pit of Saron | Route data only |  |
| WotLK | The Culling of Stratholme | No route |  |
| WotLK | The Forge of Souls | Route data only |  |
| WotLK | The Nexus | Route data only |  |
| WotLK | The Oculus | Route data only | route covers Drakos the Interrogator only (vehicle section after it) |
| WotLK | Trial of the Champion | No route |  |
| WotLK | Utgarde Keep | Route data only |  |
| WotLK | Utgarde Pinnacle | Route data only |  |
| WotLK | Violet Hold | No route |  |

## 0.15.0-alpha smoke

One run each on the pinned revisions (AzerothCore `e7e2e288e`, mod-playerbots `405f8119`), same
bot-only party, judged with `tools/run_replay`. One run is not enough for Verified.

| Dungeon | Result | Notes |
|---|---|---|
| Ragefire Chasm | Full route | 29 min, 2 wipes, other packs joined fights |
| Deadmines | Full route | 40 min, 1 wipe, other packs joined fights |

