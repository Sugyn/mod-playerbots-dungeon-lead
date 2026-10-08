# Per-dungeon testing status

Live runs use a bot-only test party (`DungeonTestBotPool`) at the dungeon's LFG target level on
the current build, started with `.dungeonlead validate` (see `tools/live_validation/README.md`). Dungeons not listed in the matrix have route data validated against the
world/travelnode databases but no live run yet. "Has a door/gate" means the route has a
`door`/`event` step; the leader opens a door the way a player can (hand lock, key, lever on its
side) or waits for it (up to `DoorWaitSeconds`). See `data/routes.tsv` for the exact step.

## Fast-test baseline (hardening audit Phase 0, 2026-10-07)

Recorded at module SHA `e1c9f8a` (then HEAD of `main`), the SHA the 2026-10-07 hardening audit
analyzed (`DUNGEON_LEAD_IMPLEMENTATION_PLAN.md`). No AzerothCore/mod-playerbots checkout, live DB
or mmaps were available in this environment, so only the worldserver-independent suites below
were run; the real module build against pinned core/playerbots revisions is a separate,
**requires runtime verification** gate, not covered here.

- `bash tools/run_tests.sh`: 304 C++17 kernel checks, 0 failures; 17 Python replay tests, all
  passing (includes six real telemetry-v2 golden runs).
- `python3 tools/validate_routes.py`: 438 rows, 96 LFG entries, 0 errors, 29 warnings - 16
  script-spawn rows (creature template exists, no static spawn on the map), 6 unresolved
  `heroic_only` rows (no resolved position - `resolve_routes.py` doesn't resolve this kind yet),
  2 zero-axis spawn warnings (worth double-checking, not confirmed wrong), 5 noncontiguous step
  sequences (not load-bearing - route execution uses vector order, not step number).
- CI (`.github/workflows/validate-routes.yml`) now runs both of the above on every change to
  `src/`, `tests/`, `data/` or the tooling itself, not only route-data changes (audit DL-011:
  these suites previously passed locally but were never executed in CI).

## Real module build (hardening fixes, 2026-10-07)

The DL-001/DL-002/DL-005/DL-007/DL-009 fixes (commits `0094fa2`, `caf16e4`, `30704f2`, `4baaec1`
on `main`) were compiled against the real, current AzerothCore + mod-playerbots source tree on
`acore-clean` (`/home/prgadm/azerothcore`, build directory `build/`, CMakeCache dated 2026-10-02 -
the tree the currently-running production `worldserver` was itself built from, last installed
2026-10-07 05:08). Changed files (`DungeonLeadActions.cpp`, `DungeonPack.cpp`,
`DungeonRouteTypes.h`, `DungeonLeadKernels.h`) were copied into
`modules/mod-dungeon-lead/src/DungeonLead/` (checksummed before/after to confirm exactly these
four files changed and nothing else drifted), then `cmake --build build --target worldserver -j12`
run. Result: clean build, zero errors - `[100%] Built target worldserver` - no `make install`, no
restart; the running production binary was not touched. An earlier attempt against the separate
ASAN build tree (`build-asan`) failed to link, but that tree's CMakeCache dates to 2026-09-16,
predating the 2026-10-02/03 patch-to-module migration - its own undefined-reference errors
(`DungeonLead::AcquireTestBot` and others, a known signature mismatch between the new module and
still-unported legacy GM commands in `modules/mod-playerbots/src/Script/PlayerbotCommandScript.cpp`,
unrelated to this module) confirm it is simply stale, not a regression from this work.

Two further passes against the same `build/` tree, same method (checksum-verified copy, compile
only, no install/restart): DL-008 (`a1f4921`/`4e6fcea` - `DungeonLeadActions.cpp`,
`DungeonRouteMgr.{h,cpp}`) - clean. DL-003/DL-004 (`06249de`/`842ef50`/`7d10fea` -
`DungeonPartyState.cpp`, `DungeonRecoveryController.{cpp,h}`, `DungeonLeadKernels.h`,
`DungeonRouteMgr.h`) - clean. DL-006 (`8c497be` - `DungeonLeadActions.cpp`, `DungeonRouteMgr.h`) -
clean. DL-010 (`6e96f92` - `DungeonLeadActions.cpp`, `DungeonRouteMgr.h`) - clean. All eleven
hardening findings (DL-001 through DL-010, plus the Phase 0 CI/baseline work tracked as DL-011)
are now implemented and build cleanly against the real source tree, five separate compile passes
deep with zero errors throughout; none has had a live start/stop smoke test or in-dungeon run yet
- see docs/project-state.md for the current open-gate ledger. Phase 8 (live validation with a real
human player plus bots) is the only remaining work in DUNGEON_LEAD_IMPLEMENTATION_PLAN_1.md.

## Live validation (2026-10-04/05)

Best run per dungeon (25 to 70 min runs, 2026-10-04/05). Event steps (use/talk) and door opening
are on since 2026-10-05; Deadmines, Shadowfang Keep and Zul'Farrak were run with them.
OK = exercised and behaved correctly, `-` = not exercised.
Levels (definitions in `tools/live_validation/README.md`): Smoke, Partial, Full route, Verified
(full route twice without a failure that needed a fix), Blocked. Verified on `d1f1ddf`: Ragefire
Chasm, SM Library, SM Armory, SM Cathedral, Razorfen Downs, Zul'Farrak (see below).

## Current status (generated from campaign artifacts)

Regenerate: `python3 -m tools.run_replay.status <validation_runs> --write docs/testing-status.md`
(the campaign artifacts come from `tools/run_replay/cli.py campaign`). Verified = two FULL_ROUTE
runs on the same commit and validation profile, health not unhealthy.

<!-- GENERATED: tools/run_replay/status.py - do not edit by hand -->

| Dungeon | Level | Verified on | Latest build | Latest runs (newest first) |
|---|---|---|---|---|
| Deadmines | **Verified** | `bc537b9` | `bc537b9` | full route; full route (warning) PACK_IDENTITY, PULL |
| Ragefire Chasm | **Verified** | `bc537b9` | `bc537b9` | full route; full route |
| Razorfen Downs | **Verified** | `5f3d706` | `bc537b9` | full route; partial (unhealthy) PULL |
| Scarlet Monastery - Armory | **Verified** | `bc537b9` | `bc537b9` | full route; full route |
| Scarlet Monastery - Cathedral | **Verified** | `bc537b9` | `bc537b9` | full route; full route (warning) PACK_IDENTITY |
| Scarlet Monastery - Library | **Verified** | `bc537b9` | `bc537b9` | full route; full route |
| Shadowfang Keep | **Verified** | `5f3d706` | `bc537b9` | full route (warning) PULL; partial (warning) PACK_IDENTITY, WIPE |
| Zul'Farrak | **Verified** | `bc537b9` | `bc537b9` | full route; full route |

From 100 runs in 9 telemetry v2 campaigns (verify-20261006-1608-b73632e … verify-20261007-0327-bc537b9).

<!-- END GENERATED -->

## Verified campaign `verify-20261006-1626-d1f1ddf` (telemetry v2)

The first campaign recorded with telemetry v2 and judged by `tools/run_replay` (verdicts and
findings, not by hand). Verified = two FULL_ROUTE runs on the same commit and profile, health not
unhealthy.

| Dungeon | Run 1 | Run 2 | Level | Findings |
|---|---|---|---|---|
| Ragefire Chasm | FULL_ROUTE, 1 wipe | FULL_ROUTE, 1 wipe | **Verified** | PACK_IDENTITY: Bazzalan pulled with 12 cultists |
| SM Library | FULL_ROUTE, 1 wipe | FULL_ROUTE | **Verified** | |
| SM Armory | FULL_ROUTE | FULL_ROUTE | **Verified** | |
| SM Cathedral | FULL_ROUTE, 2 wipes | FULL_ROUTE | **Verified** | ROUTE: the walk to Fairbanks ran into 13 / 6+12 units |
| Razorfen Downs | FULL_ROUTE | FULL_ROUTE, 1 wipe | **Verified** | |
| Zul'Farrak | FULL_ROUTE | FULL_ROUTE | **Verified** | |
| Deadmines | PARTIAL (Gilnid: path) | PARTIAL | Full route earlier | PACK_IDENTITY: Gilnid with 11 goblins and a golem |
| Shadowfang Keep | PARTIAL (Razorclaw: door) | PARTIAL | Partial | INTERACTION: the walk to the fel steeds turned back to the cells (Cell Door 18935) |

Most wipes have one cause: packs on the way to a boss have no pull steps, so the walk or the boss
pull takes several packs at once. Next: `path_decision` evidence for SFK (deployed `5f3d706`),
then pull steps from the world DB's packs.

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
