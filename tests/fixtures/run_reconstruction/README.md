# Run reconstruction fixtures

Telemetry of real validation runs (2026-10-06, bot-only test parties), one directory per case,
for the replay tools' tests. v1 only so far: `sessions.csv` (DungeonLeadSessions.csv rows of the
run) and `runs.csv` (its DungeonLeadRuns.csv row). v2 (`events.v2.jsonl`) is added per case once
the run has been repeated on a v2 build.

| Case | Run | Dungeon | Result |
|---|---|---|---|
| clean_full_route | 1878285668581390 | Razorfen Downs | complete, no wipe (optional rare not present) |
| wipe_and_recovery | 1878285668581384 | Ragefire Chasm | complete after 1 wipe |
| unexpected_add | 1878285668581378 | Shadowfang Keep | partial, 4 wipes; boss fights joined far from home, many adds |
| interaction | 1878285668581391 | Zul'Farrak | complete with the pyramid event (key, cage, talk, End Door) |
| objective_failure | 1878312760639490 | Shadowfang Keep | partial: Razorclaw aborted, Cell Door closed on the way |
| campaign_timeout | 1878285668581385 | Deadmines | still running at the 70 min cap |
| campaign_killed_run | 1878285668581380 | SM Armory | released by the campaign while active (campaign bug, fixed) |

Raw evidence: never edit these files; add a new case instead.
