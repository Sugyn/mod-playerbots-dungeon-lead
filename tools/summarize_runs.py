#!/usr/bin/env python3
"""Summarize Dungeon Lead runs from the two telemetry files.

Usage: summarize_runs.py DungeonLeadSessions.csv DungeonLeadRuns.csv [--since "YYYY-MM-DD HH:MM"]

Prints one block per run: dungeon, outcome and terminal reason (from DungeonLeadRuns.csv),
bosses cleared, objectives skipped/retried/failed, wipes, recoveries, interactions, pulls and
the last state - enough to fill the validation matrix in docs/testing-status.md and to classify
a failure before touching code. ALERT lines flag what should never happen in a healthy run.
"""
import csv
import sys
from collections import Counter, OrderedDict


def read(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        return list(csv.reader(f))


def main():
    args = sys.argv[1:]
    since = ""
    if "--since" in args:
        i = args.index("--since")
        since = args[i + 1]
        del args[i:i + 2]
    sessions, runs = args

    events = OrderedDict()
    dropped = 0
    for row in read(sessions)[1:]:
        if len(row) < 9 or row[0] < since:
            continue
        if row[7] == "telemetry_dropped":
            dropped += 1
        run_id = row[1]
        if run_id == "0":
            continue
        events.setdefault(run_id, []).append(row)

    summaries = {}
    for row in read(runs)[1:]:
        if len(row) >= 13 and row[0] >= since:
            summaries[row[1]] = row

    for run_id, rows in events.items():
        kinds = Counter(r[7] for r in rows)
        dungeon = next((r[4] for r in rows if r[4] not in ("?", "")), "?")
        cleared = [r[8] for r in rows if r[7] == "already_dead"]
        failed = [r[8] for r in rows if r[7] in ("objective_failed", "objective_skipped", "objective_retry")]
        recoveries = [r[8] for r in rows if r[7] in ("recovery_start", "recovery_escalate", "recovery_failed")]
        interactions = [r[8] for r in rows if r[7] == "interaction_state"]
        last_state = rows[-1][14] if len(rows[-1]) > 14 else "?"
        s = summaries.get(run_id)
        print(f"run {run_id}  {dungeon}  {rows[0][0]} .. {rows[-1][0]}")
        if s:
            print(f"  result: {s[6]} {s[7]}/{s[8]}  skipped={s[9]} wipes={s[10]} "
                  f"duration={int(s[11]) // 60000}m terminal={s[12]}")
        else:
            print(f"  result: (no run row) last state {last_state}")
        print(f"  cleared ({len(cleared)}): {', '.join(cleared)}")
        if failed:
            print(f"  objectives: {' | '.join(failed)}")
        print(f"  pulls established={kinds['pull_established']} failed={kinds['pull_failed']} "
              f"wipes={kinds['wipe_detected']} leash_holds={kinds['leash_hold']}")
        if recoveries:
            print(f"  recovery: {' | '.join(recoveries[:6])}{' ...' if len(recoveries) > 6 else ''}")
        if interactions:
            print(f"  interaction: {' | '.join(interactions)}")
        for alert in alerts(kinds, rows):
            print(f"  ALERT {alert}")
        print()
    if dropped:
        print(f"ALERT telemetry_dropped rows: {dropped} (lines were lost)")


def alerts(kinds, rows):
    """Invariants found live in H7: each of these needed a fix when it appeared."""
    out = []
    if kinds["unexpected_teleport"]:
        out.append(f"unexpected_teleport x{kinds['unexpected_teleport']} (something tried to move a session bot out)")
    if kinds["instance_validity_restored"] > 5:
        out.append(f"instance_validity_restored x{kinds['instance_validity_restored']} (more than one per member)")
    if kinds["recovery_failed"]:
        out.append("recovery_failed (run stopped by the recovery controller)")
    if kinds["leader_unstuck"]:
        out.append(f"leader_unstuck x{kinds['leader_unstuck']} (leader was left where it could not path)")
    for r in rows:
        if r[7] == "objective_failed" and "requirement=boss" in r[8]:
            out.append(f"boss objective failed: {r[8]}")
    return out


if __name__ == "__main__":
    main()
