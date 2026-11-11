# Run reconstruction fixtures (telemetry v2)

Real runs, one directory per acceptance case of the replay tools, with run and campaign ids
replaced by synthetic ones. `events.jsonl.gz` is the run's raw v2 telemetry (evidence: do not edit
it - add a new case instead); `campaigns.jsonl` the records of
its campaign; `run_model.json.gz`, `timeline.md`, `verdict.json`, `failures.json` the expected
outputs (goldens). `tests/test_run_replay.py` rebuilds them and also checks that each case still
shows what it was chosen for. After a deliberate change to reconstruction or analysis:
`python3 -m tools.run_replay.golden --update`, and review the diff.

| Case | Run | Dungeon | Shows |
|---|---|---|---|
| A_clean_full_route | 100012 | SM Armory | full route, no wipe |
| B_wipe_and_checkpoint | 100009 | Ragefire Chasm | wipe on Bazzalan (12 adds), checkpoint restore, full route |
| C_unexpected_adds | 100010 | Deadmines | Gilnid pulled with 11 adds, wipe, objective failed on a path |
| D_interaction_event | 100013 | Zul'Farrak | the pyramid event: key, cage, talks, End Door |
| E_objective_failure | 100011 | Shadowfang Keep | Razorclaw aborted: Cell Door 18935 closed on the way |
| F_record_ends_mid_run | 100008 | Shadowfang Keep | record ends mid-run (server restarted): verdict UNKNOWN |

Campaigns: campaign-0002 (A-E), campaign-0001 (F).
