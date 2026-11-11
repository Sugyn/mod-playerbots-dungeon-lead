# Run reconstruction fixtures

Telemetry of real validation runs (bot-only test parties), one directory per case, for the replay
tools' tests: `sessions.csv` (DungeonLeadSessions.csv rows of the run) and `runs.csv` (its
DungeonLeadRuns.csv row), telemetry v1. Run and campaign ids are replaced by synthetic ones.

| Case | Run | Dungeon | Result |
|---|---|---|---|
| clean_full_route | 100005 | Razorfen Downs | complete, no wipe (optional rare not present) |
| wipe_and_recovery | 100003 | Ragefire Chasm | complete after 1 wipe |
| unexpected_add | 100001 | Shadowfang Keep | partial, 4 wipes; boss fights joined far from home, many adds |
| interaction | 100006 | Zul'Farrak | complete with the pyramid event (key, cage, talk, End Door) |
| objective_failure | 100007 | Shadowfang Keep | partial: Razorclaw aborted, Cell Door closed on the way |
| campaign_timeout | 100004 | Deadmines | still running at the 70 min cap |
| campaign_killed_run | 100002 | SM Armory | released by the campaign while still active |

Raw evidence: do not edit these files; add a new case instead.
