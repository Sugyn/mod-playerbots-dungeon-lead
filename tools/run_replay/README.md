# Run replay

Turns schema v2 telemetry (`docs/telemetry-schema-v2.md`) into a reconstructed run, a report and
a verdict. Python 3, standard library only, no worldserver needed.

```text
DungeonLeadEvents.v2.jsonl ─┐
DungeonLeadCampaigns.jsonl ─┴─> reconstruct ─> run_model.json ─┬─> report  ─> timeline.md, map.svg, report.html
                                                                ├─> analyze ─> metrics.json, failures.json, verdict.json
                                                                └─> compare ─> comparison.json, comparison.md
campaign directories ─> summary ─> summary.json, summary.md
campaign A, campaign B ─> compare-campaigns ─> campaign_comparison.json, campaign_comparison.md
```

Layers never mix: raw files are evidence (read only), `run_model.json` is the reconstruction
(facts), `failures.json` / `verdict.json` are interpretation, the reports are presentation.

## Commands

Run from the repository root:

```sh
# runs in an events file
python3 -m tools.run_replay.cli runs DungeonLeadEvents.v2.jsonl
# one run
python3 -m tools.run_replay.cli reconstruct DungeonLeadEvents.v2.jsonl <run_id> -o run_model.json
python3 -m tools.run_replay.cli report run_model.json -d report/
python3 -m tools.run_replay.cli analyze run_model.json -d derived/ --campaigns DungeonLeadCampaigns.jsonl
# two runs of the same dungeon
python3 -m tools.run_replay.cli compare old/run_model.json new/run_model.json -d cmp/
# a whole validation campaign -> validation_runs/campaigns/<campaign_id>/ (not in git)
python3 -m tools.run_replay.cli campaign DungeonLeadEvents.v2.jsonl DungeonLeadCampaigns.jsonl <campaign_id>
# per dungeon and run context, over campaigns and/or single runs
python3 -m tools.run_replay.cli summary validation_runs/campaigns/<id> [more campaigns or runs/<run_id> dirs] -d sum/
# is campaign B better, worse or the same as A (campaign dirs or summary.json files)
python3 -m tools.run_replay.cli compare-campaigns validation_runs/campaigns/<a> validation_runs/campaigns/<b> -d cmp/
```

The server files are in `env/dist/bin` on the game server; copy them first. A campaign can be
rebuilt while it runs - a run's raw copy only ever grows.

## What the model says

- **Fights** come from the party's attackers (`fight_started` .. `fight_ended`). A fight is
  `planned` when a member of the current objective's pack fought in it - then `expected_count` is
  the pack at engage and every other unit is an **add**. Otherwise it is `en_route` (trash met on
  the way to the objective): no expected set, no adds.
- **Bosses**: attempts from the boss pack's state (`engaged` -> `cleared` = killed; back to
  `available` = reset, or `wipe` when a wipe happened meanwhile; `found_dead` = cleared without
  being fought by this party).
- **Deaths / wipes**: `member_died` with role and position; a wipe lists the deaths of the minute
  before it, the checkpoint it resumed from and when the party travelled again (`resumed_ms`).
- **Teleports**, **CC marks**, **dead-target waits** and **time spans**: see the run metrics below.
- Events that have no structured payload yet are read from their v1 `detail` text in `legacy.py`
  only, and marked `"source": "detail"`.
- Evidence is sampled by the session guard every ~2 s: a unit that joins and dies between two
  passes is not seen, and times are accurate to ~2 s.

## Run metrics

`metrics.json` (= `run_model.json` `metrics`) holds the counts (fights, wipes, deaths, ...) and,
since `metrics_version` 2, six structured metrics. Each has a `source` (the events it comes from)
and is `null` when the run has no such evidence - a run recorded before an event existed, or one
without state transitions. A `0` is measured, never a stand-in for "unknown".

| Metric | What it answers | Derived from |
|---|---|---|
| `dead_target_waits` | `count` of route steps whose target was already dead or not there and that held the leader for at least 1 s (`found_dead` / `not_found` split, same rule), `total_s` and `max_s` the leader spent on them (all episodes, also the shorter ones - those are just the next guard pass moving on) | found dead: the step's pack goes to `cleared` (`pack_state`) without this party engaging it on this visit of the step, until the step is left (`already_dead`, `objective_skipped` / `_failed`, `skip_stuck`, `step_walk_limit`, a new step); not found: from `reached` to `not_found`. Only time in walking / pulling states counts - a fight or a readiness wait meanwhile has its own reason (`elapsed_ms` per episode in the model keeps the whole span) |
| `teleports` | players moved, per kind: `leader_unstuck`, `leader_to_floor`, `member_to_leader`, `wipe_recovery`, `member_fallback`, `unexpected`; `leader` (the first two), `member` (`member_to_leader`, `wipe_recovery`, `member_fallback`), `total`, `fallback_reasons` (`member_teleport` reasons) | `leader_unstuck`, `leader_to_floor`, `recovery_escalate` with `action=brought_to_leader` (the member and every `+name` after it), `wipe_recovery_teleport` (a dead member released outside the instance, sent back to the entrance - also outside a wipe), `member_teleport` (every party bot moved, with its reason; a run that records it is not counted again from its `recovery_escalate` lines), `unexpected_teleport` (a teleport out of the dungeon the module cancelled) |
| `run_back` | `held`, `resurrected`, `released`, `walked_to_portal`, `walk_orders`, `rejoined_walking`, `fallbacks`, `back_ms_max`; `null` for a run recorded before the run back | `run_back` events |
| `cc` | `moons` placed and how each ended: `landed`, `released_without_cc` (of those `released_last_enemy`), `target_gone`, `replaced` (a new moon came before any outcome), `unresolved` (still marked when the record ends); `landed_ratio` = landed / moons placed; `landed_by` (caster, spell, spell id and moons each); `party_cc_classes`, `moons_without_cc_class` | `mark_moon`, `cc_landed`, `cc_released`, `cc_target_gone`; `landed_by` from `cc_outcome` (caster and spell of the control aura on a landed moon) - `null` for runs without `cc_outcome`, a landed moon whose aura was not seen has caster and spell `null`; classes from `party_roster`: hunter, mage, warlock, druid - the classes of the spells the module plans crowd control with (`kCcSpells` in `DungeonTargetManager.cpp`: Polymorph, Banish, Fear, Hibernate, Entangling Roots, Cyclone, Freezing Trap, Scare Beast); by class, not spell rank, talents or creature type |
| `wipe_to_resume_s` | per wipe, seconds from `wipe_detected` to the next switch to `travelling` (`null` if the record ends or the next wipe comes first); `median_s`, `max_s`, `unresolved` | `wipe_detected`, `state_transition` |
| `time_split` | seconds in `walking`, `pulling`, `combat`, `boss_prep`, `waiting`, `recovery`, `other`; `waiting_by_reason_s` (`healer_mana`, `party_range`, `eating_drinking`, `dead_member`, `patrol`, `low_health`, `post_combat_check`, `dead_target`, `other`, `unattributed`) and `recovery_by_reason_s` | the leader state from `state_transition`; waiting reasons from `waiting` lines, `post_combat_decision` (`ready` = the post-combat check itself), a pull held for the party or a patrol (`pull_state` `waiting_party` / `waiting_patrol`) and the dead-target waits; recovery reasons from `recovery_start` - of the episode still open when the recovery state begins or started at most 5 s before it, else `unattributed` - and `wipe` for the wipe-recovery state |
| `backtrack_ratio` | share of the walk (travelling, out of combat) that took the leader farther from its step's destination: `walked_yd`, `away_yd`, `ratio`, `unmeasured_yd` | `position_sample` and the navmesh path of the step's last `path_decision`: the way left from a position is its distance to the nearest point of that path plus the path from there plus the end to the destination - so the bends of a ramp or a cave are not counted as backwards. A sample pair counts when the way left grew by more than 1 yd |

The `waiting` line is throttled to one per 10 s, so its reason is also given to the time before
it in the same wait; a wait with no reason recorded at all is `unattributed`. `time_spans` in the
model cover the run exactly once (`start_ms`, `end_ms`, `state`, `category`, `reason`).

## Verdict and findings

`verdict.json`: `result` FULL_ROUTE / PARTIAL / FAILED / UNKNOWN (record incomplete or still
running) with the reason, and `health` clean / warning / unhealthy with the conditions behind it.
`failures.json`: findings with `facts`, `classification`, `probable_cause`, `confidence` and
`evidence` (F = fight, W = wipe, I = interaction ids in the report). Without enough evidence the
class is OBSERVABILITY and the cause says so.

Warnings from the run metrics (`analysis_version` 3; constants in `analyze_run.py`):

| Warning | When |
|---|---|
| `leader_teleports=N` | the leader was teleported (unstuck or put back on the floor) |
| `dead_target_wait_s=S` | one wait on a dead or absent target took more than 10 s |
| `cc_landed_ratio=R` | at least 3 moons were placed and under half of them landed |

## Summary

`summary INPUT...` reads campaign directories, single run directories (`.../runs/<run_id>`) or a
`validation_runs` directory (every campaign in it); a run reached twice counts once. Runs are
rebuilt from their raw events when present, so older artifacts get today's metrics; otherwise
`derived/run_model.json` is read. Per dungeon and run context (`groups`) and over everything
(`overall`): runs, FULL_ROUTE count and rate, median / p90 minutes (all finished runs and
full-route runs; nearest-rank p90), wipes and deaths per run, dead-target waits per run and their
seconds, teleports per run (by kind, and runs with each), member teleports per run (mean and median)
and the fallback reasons, the run back totals (a table of its own in `summary.md`), CC landed ratio (pooled: landed / moons
placed) with `released_last_enemy`, median and
max wipe-to-resume, time and waiting shares of the pooled run time, backtrack ratio (pooled).
"Per run" is per finished run: UNKNOWN runs (a cut record) are counted and left out of the rest.
`summary.json` also lists every run (`runs`) with its context, so a comparison can regroup them.

## Campaign comparison

`compare-campaigns A B` compares B (new) with A (old) per dungeon: every metric old / new / delta
and a verdict per rule. A dungeon is **worse** if any rule says worse, **better** if one says
better and none worse, else the **same**; overall worse if any dungeon is worse, better if one is
better and none worse, the same if all compared dungeons are. The pooled section is for
orientation only.

| Rule | Threshold (`compare_campaigns.py`) |
|---|---|
| FULL_ROUTE rate (share of finished runs) | change of 0.2 or more (one run in five) |
| median duration of full-route runs | change of more than 10 % (shorter is better) |
| wipes per run | change of 0.5 or more |
| deaths per run | change of 1 or more |
| seconds per run on dead or absent targets | change of 10 s or more |
| teleports the module decides on (leader unstuck, leader to floor, member to leader, unexpected) | a kind none in A and some in B is worse, the reverse better; wipe-recovery teleports follow deaths and are left to the deaths rule |
| CC landed ratio | change of 0.1 or more, when both sides placed at least 3 moons |

Reported without a rule: teleports per run, wipe-to-resume median, backtrack ratio, all-runs
median / p90 minutes, time shares.

**Not comparable**: a dungeon without a finished run on one side, a different validation profile,
or a run-context part other than the module that differs (core, mod-playerbots, route table,
config, mmaps). `--accept PART` (repeatable: `core_sha`, `playerbots_sha`, `route_digest`,
`config_digest`, `mmap_digest`) declares such a difference intended, e.g. a route data change
under test. A module difference is the point of a comparison and never blocks it; runs without a
run context are reported and do not block. With fewer than 5 finished runs of a dungeon on a side
the verdict is marked `low_sample`: one run more or less already crosses the FULL_ROUTE rule. The
overall `low_sample` is true when every judged dungeon is a low sample, and `not_comparable` lists
the dungeons left out; both are in the headline of the report and on stdout. Dungeons are matched
by LFG id and name together, as in the summary.

## Panel contract

The artifacts a viewer reads. Changes are additive and versioned: new fields come with a bump of
the file's version field, and a published field keeps its meaning - what would change it becomes a
new field instead. One deliberate exception: `health` and `warnings` keep their levels and form
but are judged with more conditions as `analysis_version` grows - under version 3 a run that was
`clean` can be `warning` (the run-metric warnings above) without anything else in it changing.

| File | Version field | Contents |
|---|---|---|
| `run_model.json` | `model_version` (2) | the reconstruction; see below |
| `metrics.json` | `metrics_version` (2) | the `metrics` of the run model |
| `verdict.json`, `failures.json` | `analysis_version` (3) | result, health, warnings; findings |
| `index.json` (campaign) | `index_version` (4) | one entry per run: result, health, `metrics` (as `metrics.json`), artifact paths |
| `summary.json` | `summary_version` (1), `metrics_version` | `campaigns`, `groups` (per dungeon and run context), `overall`, `runs` |
| `campaign_comparison.json` | `comparison_version` (1) | `a`, `b`, `verdict`, `thresholds`, `accepted_differences`, `low_sample`, `not_comparable`, `dungeons` (`verdict`, `verdict_by_metrics`, `why`, `differences`, `better`, `worse`, `low_sample`, `metrics` rows `metric` / `old` / `new` / `delta` / `verdict`), `pooled` |

Added to `run_model.json` in version 2 (version 1 fields unchanged):

| Field | Entries |
|---|---|
| `teleports[]` | `run_ms`, `kind`, `who` (players moved, `null` if not recorded), `detail`, `position` |
| `cc_marks[]` | `placed_ms`, `target`, `step`, `pack_id`, `landed_ms`, `end_ms`, `end_event`, `release_reason`, `outcome` (landed / released / target_gone / replaced / unresolved), `cc_outcome` (its `outcome`, `null` without one), `by` (`caster`, `spell`, `spell_id` from a landed `cc_outcome`, else `null`) |
| `dead_target_waits[]` or `null` | `step`, `name`, `pack_id`, `kind` (found_dead / not_found), `start_ms`, `end_ms`, `elapsed_ms`, `duration_ms` (walking / pulling time only), `ended_by` |
| `time_spans[]` or `null` | `start_ms`, `end_ms`, `state`, `category`, `reason` |
| `wipes[].resumed_ms` | first switch to travelling after the wipe, `null` if none before the record ends or the next wipe |
| `metrics.metrics_version`, `metrics.dead_target_waits`, `.teleports`, `.run_back`, `.cc`, `.wipe_to_resume_s`, `.time_split`, `.backtrack_ratio` | as in [Run metrics](#run-metrics) |

Every version 1 field (`metadata`, `party`, `route`, `path`, `path_decisions`, `timeline`,
`pulls`, `bosses`, `deaths`, `wipes`, `recoveries`, `interactions`, `alerts`, `result` and the
count metrics) keeps its meaning. `report` and `analyze` read only the current `model_version`;
`summary` reads older run models too and leaves the metrics they lack `null`.
