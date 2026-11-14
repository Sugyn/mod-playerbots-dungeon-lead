# Architecture

Dungeon Lead is an AzerothCore module that sits on top of mod-playerbots. A tank bot takes the
group lead in a 5-man dungeon and decides the party's objective: wait, walk, pull, fight, recover,
finish. Spell selection, rotations, threat and healing stay with mod-playerbots' class AI.

## Integration without patching mod-playerbots

mod-playerbots has no extension seam for new strategies, actions and triggers. The module reaches
the shared registries anyway, without editing a single mod-playerbots file:

- **Registry access** (`DungeonLeadAccess.h`): the base `AiObjectContext` registries are private
  static members; an explicit template instantiation may name a private member, which yields their
  addresses in well-defined C++. The per-class contexts (`WarriorAiObjectContext`, ...) have their
  own public registries and are reached directly. Every bot is built from its class context, so the
  module registers into all of them.
- **Overrides** (`DungeonLeadOverrides.h`): two stock entries are replaced by subclasses registered
  under the same key after mod-playerbots' own registration - the `formation` value learns
  `leader`, and the `unknown dungeon` trigger stays quiet while a bot leads a session.
- **Strategies on every bot**: `dungeon lead commands` is appended to mod-playerbots' default
  non-combat strategy strings at the first world tick, so every bot hears `startdungeon`.
- **Own config and commands**: `DungeonLeadConfig` reads `AiPlayerbot.DungeonLead.*` through
  `sConfigMgr`; GM commands live under the module's own `.dungeonlead` root.
- **Hooks**: a world script drives the session guard, telemetry flush, canary and test bot pool;
  a player script vetoes teleports of session bots; a map script drops per-instance kill memory
  when an instance is destroyed.

A registration probe checks, a few minutes after start, that one online bot of each class creates
the module's strategy and action and resolves both overrides; the result is logged per class.

## Session lifecycle

```text
startdungeon -> Starting -> (active states) -> Stopping -> gone
                  |  leadership requested        |  strategies restored,
                  |  until the group leader      |  until leadership is
                  |  is observed to be the tank  |  observed back
```

- Queueing a leader change is not leadership: `Starting` ends only when the group's leader is
  observed to be the tank. Requests are retried a bounded number of times; each transfer has a
  generation, and a queued request of an old generation is rejected when it finally runs.
- Before anything changes, every member's formation and strategy set is snapshotted; `Stop()`
  restores them exactly, by GUID, also for members that have left or a leader that disconnected.
- A guard pass every ~2 s (world thread) re-asserts the session's strategies on every bot, because
  a group change makes mod-playerbots reset bot AI asynchronously. Only components that actually
  differ are changed.
- Exactly one terminal result is written per run (`DungeonLeadRuns.csv`), whatever ends it.

## Components

| Component | Responsibility |
|---|---|
| `DungeonLeadBrain` | the session state machine; `TransitionTo()` is the only writer of the state |
| `DungeonPartyState` | one observation of the party per tick: alive, map, distance, mana, sitting, sight |
| `DungeonRouteMgr` | route table, steps, kinds, entrances, per-instance boss kill memory |
| `DungeonPack` | which live units form the enemy group a route step is about |
| `DungeonPullController` | approach, wait for party and patrols, mark, initiate, establish |
| `DungeonTargetManager` | stable target plan: skull (primary), cross (secondary), moon (crowd control) |
| `DungeonRecoveryController` | bounded recovery: act, escalate, abort |
| `DungeonInteractionController` | closed doors, levers and keys in the way |
| `DungeonValidationCampaign` | `.dungeonlead validate`: runs a list of dungeons with test parties |
| `DungeonTestBotPool` | deterministic tank/healer/dps test bots from the AddClass accounts |
| AutoBot Canary | optional: takes over bot-only LFG groups for telemetry (off by default) |

States of an active session: `WaitingReady`, `Travelling`, `PrePull`, `Pulling`, `Combat`,
`PostCombat`, `BossPrep`, `BossCombat`, `Recovery`, `WipeRecovery`, `Completing`. Every transition
is logged with its reason.

### Decisions are pure functions

Every decision lives in `src/DungeonLead/DungeonLeadKernels.h` as a dependency-free function of
plain facts (readiness, pull step, recovery step, leadership step, pack resolution, door handling,
target plan, wipe checkpoint). The components gather facts from the game and apply the result.
`tests/test_kernels.cpp` exercises the kernels with g++ and no worldserver.

### Readiness and pulls

The leader walks on only when the party is ready: master and healer alive and in reach, nobody
dead, eating, drinking or below the health threshold after a fight, the party within the soft and
hard ranges. A new pull additionally needs a healer in sight of the tank. A pull waits for patrols
to pass and for the party to stand still, each wait bounded. A fight that does not start, or a pack
that resets too often, is a failed attempt; when attempts run out the objective policy decides.

### Objectives and route progress

Each route row has a kind (`boss`, `required`, `optional`, `door`, `use`, `talk`, `event`, ...) and
from it a requirement:

- **Boss / Required**: retried for a number of rounds, then the run ends `Partial`. The route never
  advances past them silently.
- **Optional**: skipped after its first failed round, and recorded as skipped.

A pack step advances only when its pack is observed cleared, never because the tank reached a
coordinate. Path anchors (`entry = 1`, the GM-only waypoint template) are pure navigation.

### Combat anchor and leash

A fight is held around the spot where it started (a boss fight around the boss's home), within
`CombatLeashRadius` / `BossLeashRadius`. A fleeing mob is not chased into the next pack; a mob that
is beating a party member is.

### Recovery and wipes

Recovery reasons (fragmented party, lost member, dead member, lost leadership, unavailable master,
persistent waits) each have an action, a timeout, an escalation and an abort. Escalation only ever
moves bot members - a real player is never teleported. Dead bots come back like players: a
resurrecter of the party brings a bot back in place when it can (its release is held for that),
otherwise the bot releases, walks as a ghost from the graveyard to the dungeon's portal, steps in
and walks to the party; a straggler gets a walk order to the leader, who waits. A teleport is only
the fallback of a walk that failed within its bound, with a `member_teleport` event. The leader's
own death is `WipeRecovery`. The route resumes after the last checkpoint confirmed
safe, so nothing that was only passed over stays skipped because of a wipe.

### Doors and events

A closed door on the way is opened the way a player can - by hand, with a key a bot carries, or
with a lever on the near side - or waited for (`DoorWaitSeconds`). `use` and `talk` steps run the
events a route needs; their effect is confirmed in the world before the step counts as done, where
the effect is observable.

## Threads

Bot AI runs on the map update threads. The guard pass, the controllers that act on several bots and
all file I/O run on the world thread after the map updates have joined, so they never race a bot's
own AI tick. Telemetry producers only append to a bounded in-memory buffer; the world thread writes
it to the files every ~2 s. No database access happens in an AI tick: the route table is loaded
once and is read-only afterwards.

## Route data

`data/dungeon_routes.csv` is the source; `sql/playerbots_dungeon_route.sql` is the complete table
for the `acore_playerbots` database and must equal the CSV (`tools/check_route_sql.py`).
`tools/validate_routes.py` checks the rows against the LFG dungeon list; `tools/route_insert.py`
inserts steps and writes a numbered update with its rollback (`sql/updates/README.md`).

## Telemetry and replay

- `DungeonLeadSessions.csv` / `DungeonLeadRuns.csv`: one row per event / per finished run.
- `DungeonLeadEvents.v2.jsonl`: structured events with a per-run sequence and a run context
  (module, core and mod-playerbots commits, route, config and mmap digests). Schema:
  [telemetry-schema-v2.md](telemetry-schema-v2.md).
- `tools/run_replay` rebuilds a run into a timeline, a map and a verdict with findings, and compares
  runs. A dungeon counts as Verified after two full-route runs on the same run context
  ([dungeons.md](dungeons.md)).
