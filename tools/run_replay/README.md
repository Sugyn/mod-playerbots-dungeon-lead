# Run replay

Turns schema v2 telemetry (`docs/telemetry-schema-v2.md`) into a reconstructed run, a report and
a verdict. Python 3, standard library only, no worldserver needed.

```text
DungeonLeadEvents.v2.jsonl ─┐
DungeonLeadCampaigns.jsonl ─┴─> reconstruct ─> run_model.json ─┬─> report  ─> timeline.md, map.svg, report.html
                                                                ├─> analyze ─> metrics.json, failures.json, verdict.json
                                                                └─> compare ─> comparison.json, comparison.md
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
  before it and the checkpoint it resumed from.
- Events that have no structured payload yet are read from their v1 `detail` text in `legacy.py`
  only, and marked `"source": "detail"`.
- Evidence is sampled by the session guard every ~2 s: a unit that joins and dies between two
  passes is not seen, and times are accurate to ~2 s.

## Verdict and findings

`verdict.json`: `result` FULL_ROUTE / PARTIAL / FAILED / UNKNOWN (record incomplete or still
running) with the reason, and `health` clean / warning / unhealthy with the conditions behind it.
`failures.json`: findings with `facts`, `classification`, `probable_cause`, `confidence` and
`evidence` (F = fight, W = wipe, I = interaction ids in the report). Without enough evidence the
class is OBSERVABILITY and the cause says so.

## Panel contract (panel.hungryvessel.com/dungeonlead)

The panel reads the generated artifacts, never the raw telemetry:

- `campaigns/<campaign_id>/index.json` - `campaign_id`, `build`, `module_version`,
  `validation_profile`, `parallelism`, `timeout_minutes`, `dungeons`, `finished`, and `runs[]`
  with `run_id`, `dungeon`, `lfg_id`, `scenario_id`, `leader`, `started_at`, `duration_ms`,
  `result`, `health`, `metrics`, `findings` (classes), `artifact` (run model path), `report`.
- `runs/<run_id>/derived/run_model.json` (`model_version` 1), `metrics.json`, `failures.json`,
  `verdict.json` (`analysis_version` 1); `runs/<run_id>/report/map.svg`, `timeline.md`,
  `report.html`.

Every file carries its version field; a consumer refuses versions it does not know.
