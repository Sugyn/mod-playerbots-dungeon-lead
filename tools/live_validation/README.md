# Live validation

Runs a set of dungeons one after another with a fresh bot-only test party at each dungeon's LFG
target level and logs one result line per dungeon. Built into the module; nothing to install.

## Requirements

- mod-playerbots' AddClass bot pool (offline characters of each class; the test party is a
  Protection warrior, a Holy priest and a warrior, mage and rogue as dps).
- `AiPlayerbot.DungeonLead.CanaryMaxConcurrent` >= 1. A run ends when its session ends: route end,
  a failure, or `AiPlayerbot.DungeonLead.CanaryTimeoutMinutes` (45 by default - raise it for full
  clears of long dungeons).

## Run

From the worldserver console or SOAP (GM level):

```
.dungeonlead validate 4 6 1 8          # tier 1 from validation_set.tsv
.dungeonlead validate status
.dungeonlead validate stop             # ends the campaign and releases the test bots
```

`DungeonLeadDebug.log` gets `[DungeonLead][Validation]` lines; each dungeon ends with
`RESULT lfg=<id> started=<0|1> ended=<0|1> minutes=<n>` and the campaign with
`RESULT campaign finished`.

## Summarize

```
tools/summarize_runs.py DungeonLeadSessions.csv DungeonLeadRuns.csv --since "YYYY-MM-DD HH:MM"
```

prints one block per run (result, bosses cleared, objective failures, wipes, recoveries,
interactions) and an `ALERT` line for anything that should never happen in a healthy run. Copy the
results into the matrix in `docs/testing-status.md`.

## Maturity levels

Used in `docs/testing-status.md`:

| Level | Meaning |
|---|---|
| Untested | route data only |
| Smoke | ran >= 10 min, no crash or invariant alert |
| Partial | real route progress and at least one boss killed |
| Full route | route end reached, every boss and required objective done |
| Verified | full route at least twice with no failure that needed a fix |
| Blocked | a known blocker stops the route (named in the matrix) |
