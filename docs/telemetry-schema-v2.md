# Telemetry schema v2

Structured run telemetry for replay, comparison and evidence-based verdicts. v2 is written next to
the v1 files, not instead of them: `DungeonLeadSessions.csv`, `DungeonLeadRuns.csv` and
`DungeonLeadDebug.log` stay unchanged for at least one release.

```text
worldserver (map threads) --RecordEvent--> bounded buffer --world thread, ~2 s--> files
  DungeonLeadSessions.csv      v1, one row per event (unchanged)
  DungeonLeadRuns.csv          v1, one row per finished run (unchanged)
  DungeonLeadEvents.v2.jsonl   v2, one JSON object per event   <- raw evidence
  DungeonLeadCampaigns.jsonl   v2, validation campaign records <- lineage
```

Producers only format a line and append it to the bounded buffer (`DungeonTelemetryBuffer.h`); no
file I/O, DB access or JSON DOM on the map threads. A full buffer drops lines and reports the count
(`telemetry_dropped` in both v1 and v2). Raw files are evidence: tools read them, never rewrite
them.

## 1. v1 inventory (what exists today)

Every v1 row carries timestamp (1 s), run_id, master, lfg_id, dungeon, tank, group members,
event, free-text `detail`, outcome / failure domain / failure reason, map, instance, state, step,
pack_id. Grouped by what they prove, with what a replay is missing:

| Area | v1 events | Proves | Missing for reconstruction |
|---|---|---|---|
| Lifecycle | `start`, `starting`, `leadership_request/confirmed/failed`, `stop`, `canary_stop`, `test_result`, `route_selected`, `no_route` | session start/end, leadership, route length | build/commit, campaign, scenario; run-relative time |
| State machine | `state_transition` | every brain transition with reason, `after_ms`, objective, combat anchor (text) | anchor as numbers; leader position |
| Movement | `pathing` (pos/target/dist in text), `reached`, `travel_reached`, `skip_stuck`, `leader_unstuck`, `leader_to_floor` | progress toward a step, stuck handling | continuous leader path (only sparse `pathing` text) |
| Pull | `pull_state`, `pull_start`, `pull_established`, `pull_failed`, `pack_state`, `pack_resolution`, `pack_reset`, `already_dead`, `not_found` | pull lifecycle, pack state, member/add **counts** | member and add **identities** (guid/entry/position), pull id, expected vs engaged sets |
| Targets | `target_plan`, `mark_moon`, `cc_landed`, `cc_released`, `cc_target_gone` | the plan by name | GUIDs/entries; plan per pull |
| Combat | `boss_prep`, `boss_combat`, `leash_hold`, `waiting` (group in combat) | boss anchor, leash refusals | boss engaged/killed times, mob deaths, party deaths, add joins |
| Readiness | `waiting`, `post_combat_decision` | why the leader waited | party spread / healer distance as numbers |
| Recovery | `recovery_start/complete/failed`, `recovery_escalate(_deferred)`, `wipe_detected`, `wipe_recovered`, `wipe_recovery_teleport`, `wipe_giveup`, `checkpoint_restore` | recovery episodes, wipes, checkpoint | who died, when, where, order; positions before/after |
| Interaction | `interaction_state`, `door_open`, `door_opened_by_us`, `door_not_found`, `use`, `use_waiting`, `use_failed`, `key_looted`, `talk`, `talk_waiting`, `talk_not_ours`, `event_assist`, `area_trigger` | what was opened/used/talked to and how | object position as numbers |
| Integrity | `unexpected_teleport`, `instance_validity_restored`, `strategy_restored`, `telemetry_dropped` | integrity events | - |
| Objectives | `objective_retry`, `objective_failed`, `objective_skipped`, `route_complete` | objective outcome with requirement and reason | - |

What v1 cannot answer at all: where the party walked between `pathing` lines, which mobs (not how
many) joined a pull, who died where, which build and campaign produced a run, the order of events
within the same second.

## 2. Envelope (every v2 line)

```json
{
  "schema_version": 2,
  "event_seq": 123,
  "event_type": "state_transition",
  "run_id": "1878312760639490",
  "campaign_id": "verify-20261006-1600-5f667c8",
  "scenario_id": "lfg8-lvl20-bot5-wtank-phealer-wmr-dps-lfg-target-level",
  "wall_time": "2026-10-06T15:11:22.481Z",
  "run_ms": 1240533,
  "build": {"commit_sha": "5f667c8", "module_version": "0.13.0-alpha"},
  "dungeon": {"lfg_id": 8, "name": "Shadowfang Keep", "map_id": 33, "instance_id": 4},
  "leader": {"guid": "1234", "name": "Aevar", "x": -190.12, "y": 2211.40, "z": 79.76, "o": 2.18, "alive": true},
  "state": "travelling",
  "step": 4,
  "pack_id": 5,
  "outcome": {"outcome": "running", "failure_domain": "none", "failure_reason": "none"},
  "payload": {"detail": "recovery->travelling reason=party_ready ..."}
}
```

- **Order**: `event_seq` is monotonic per run and is the canonical order; `run_ms` is milliseconds
  since the session started (monotonic clock). `wall_time` (UTC, ms) is for display and
  correlation only. Telemetry from different runs interleaves in the file - group by `run_id`.
- **leader**: the session's tank, read when the event is produced. Coordinates have 2 decimals.
- **payload**: `detail` is the v1 free text (kept so nothing is lost); events with structured data
  add their own members next to it (section 4). Tools must not parse `detail` when a structured
  member exists.
- `campaign_id` / `scenario_id` are empty for a run no validation campaign started (manual
  `startdungeon`, the automatic canary).
- `telemetry_dropped` is written by the flush itself and has only `schema_version`, `event_type`,
  `wall_time` and `payload.lines`.

Tools must reject a `schema_version` they do not know instead of guessing.

## 3. Lineage

| Field | Source |
|---|---|
| `build.commit_sha` | `tools/write_build_info.sh` writes `DungeonLeadBuildInfo.generated.h` (not in git) before a build; `<sha>-dirty` for uncommitted source changes, `unknown` without the file |
| `build.module_version` | `DungeonLeadBuildInfo.h` |
| `campaign_id` | `.dungeonlead validate`: `verify-<UTC yyyymmdd-hhmm>-<commit_sha>` |
| `scenario_id` | `lfg<id>-lvl<target level>-<validation profile>` for a campaign run |

`DungeonLeadCampaigns.jsonl` records, one object per line, each with `schema_version`, `record`
and `campaign_id`:

- `campaign_started`: `started_at`, `commit_sha`, `module_version`, `validation_profile`,
  `parallelism`, `timeout_minutes`, `dungeons` (LFG ids, in queue order).
- `run_result`: `lfg_id`, `tank`, `started`, `ended` (false = the campaign stopped a run that was
  still active - a campaign failure, not a dungeon one), `minutes`.
- `run_requeued`: `lfg_id`, `reason`.
- `campaign_finished`: `finished_at`, `runs`.

A run joins its campaign through `campaign_id` + `scenario_id` + tank name.

## 4. Event catalogue

v2 keeps the v1 event names (section 1) so both files describe the same events; the replay tools
map them onto the semantic catalogue below. New events and structured payloads are added in
increments, each with its own entry here.

| Semantic event | v2 event_type today | Payload |
|---|---|---|
| leader path | `position_sample` (v2 only) | `moving`, `in_combat` (+ envelope position) |
| run_started / run_ended | `start`, `stop`, `canary_stop`, `test_result` | `detail` |
| route_step_* | `route_selected`, `reached`, `objective_*`, `skip_stuck`, `route_complete` | `detail` |
| pull_* | `pull_state`, `pull_start`, `pull_established`, `pull_failed`, `pack_*` | `detail` (identities: planned) |
| combat_anchor_set | `state_transition` to combat (`anchor=` in `detail`) | planned: numbers |
| boss_* | `boss_prep`, `boss_combat`, `pack_state` of a boss pack | `detail` |
| wipe / recovery | `wipe_*`, `recovery_*`, `checkpoint_restore` | `detail` |
| interaction | `interaction_state`, `door_*`, `use*`, `talk*`, `key_looted`, `area_trigger`, `event_assist` | `detail` |
| integrity | `unexpected_teleport`, `instance_validity_restored`, `strategy_restored`, `telemetry_dropped` | `detail` |

Planned structured payloads (next increments, in this order): pull members and adds with
guid/entry/name/position (`pull_members_resolved`, `combat_add_joined`), party deaths
(`member_died`: who, role, position, pull), wipe summary, combat anchor as numbers, boss
engaged/killed. Only values the runtime has cheaply at the moment of the event; no world scans for
telemetry.

## 5. Position sampling

`position_sample` for the leader only, from the session guard (every ~2 s on the world thread, not
an AI tick). A sample is written when the leader moved >= 3 yd since the last one, after >= 3 s of
still moving, or whenever the state or route step changed; standing still writes nothing
(`DungeonLeadKernel::ShouldSamplePosition`). At run speed that is one sample per guard pass, about
30 per minute while walking.

## 6. Fixtures

`tests/fixtures/run_reconstruction/` holds sanitized v1 (and, once runs exist, v2) telemetry of
representative runs - clean full route, wipe and recovery, unexpected adds, interaction, objective
failure, campaign timeout - for the replay tools' tests. Only bot names and dungeon data; no
account or server details.

## 7. Performance

Watch per campaign: v2 bytes per run, events per minute, buffer size at flush, dropped lines. The
target is zero `telemetry_dropped` in validation campaigns.
