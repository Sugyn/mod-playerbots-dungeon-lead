#!/usr/bin/env python3
"""Resolve hand-authored dungeon routes (boss names) against DB dumps.

Input  : routes.tsv   lfg_id \t wing \t step \t kind \t boss_name \t note
DB     : lfg_dungeons.tsv, boss_positions.tsv, extra_bosses.tsv, instance_bosses.tsv
Output : dungeon_routes.csv (resolved) + report to stdout

NOTE: boss_positions.tsv (`map entry name rank x y z`) and extra_bosses.tsv
(`entry name rank map spawns x y z`) are checked into data/, but they're derived FROM the already-
resolved dungeon_routes.csv, not from a fresh world DB query - several boss names have more than
one candidate spawn in a real world DB (Wailing Caverns' Lady Anacondra, for one), and a fresh
per-name query has no way to know which one was actually verified/live-tested without just picking
one arbitrarily. Re-deriving them this way guarantees re-running this script reproduces the
committed dungeon_routes.csv byte-for-byte (verified: `diff` reports no changes). If you need to
resolve a genuinely NEW boss name that isn't in dungeon_routes.csv yet, look it up in your own
world DB and add a row by hand - don't regenerate these two files wholesale from a DB query.
"""
import csv, sys, os
here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(os.path.dirname(here), "data")  # inputs/outputs live in ../data, not next to the script

# --- LFD entries ----------------------------------------------------------
lfg = {}
with open(f"{data}/lfg_dungeons.tsv") as f:
    next(f)
    for line in f:
        p = line.rstrip("\n").split("\t")
        lfg[int(p[0])] = dict(name=p[1], minl=int(p[2]), maxl=int(p[3]), map=int(p[4]),
                              diff=int(p[5]), type=int(p[6]), exp=int(p[7]))

# --- creature spawns: name -> list of (entry, map, x, y, z, rank, spawns) --
spawns = {}
def add(name, entry, mapid, x, y, z, rank, n):
    spawns.setdefault(name, []).append(dict(entry=int(entry), map=mapid, x=x, y=y, z=z, rank=rank, spawns=n))

with open(f"{data}/boss_positions.tsv") as f:          # map entry name rank x y z spawnMask
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 7: continue
        add(p[2], p[1], int(p[0]), p[4], p[5], p[6], p[3], 1)
with open(f"{data}/extra_bosses.tsv") as f:            # entry name rank map spawns x y z
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 8: continue
        m = None if p[3] == "-" else int(p[3])
        add(p[1], p[0], m, p[5], p[6], p[7], p[2], int(p[4]))

def resolve(name, mapid):
    cands = [c for c in spawns.get(name, []) if c["map"] == mapid]
    if cands:
        return cands[0], "spawn"
    cands = spawns.get(name, [])
    if cands:
        return cands[0], "script"           # exists as template, not spawned on this map
    return None, "unknown"

rows, report = [], []
with open(f"{data}/routes.tsv") as f:
    for line in f:
        if not line.strip() or line.startswith("#"): continue
        lfg_id, wing, step, kind, boss, note = (line.rstrip("\n").split("\t") + [""] * 6)[:6]
        lfg_id, step = int(lfg_id), int(step)
        d = lfg[lfg_id]
        c, how = resolve(boss, d["map"]) if kind in ("boss", "optional", "event") else (None, "-")
        rows.append(dict(lfg_id=lfg_id, lfg_name=d["name"], map=d["map"], difficulty=d["diff"],
                         expansion=d["exp"], wing=wing, step=step, kind=kind, boss=boss,
                         entry=c["entry"] if c else "", x=c["x"] if c else "", y=c["y"] if c else "",
                         z=c["z"] if c else "", source=how, note=note))
        if kind in ("boss", "optional") and how != "spawn":
            report.append(f"  {d['name']:40s} step {step:2d} {boss:35s} -> {how}")

# --- derive heroic (type 5) entries from the normal entry on the same map ---
normal_by_id = {}
for r in rows:
    normal_by_id.setdefault(r["lfg_id"], []).append(r)
def base_rows_for_heroic(d):
    # same name first (Slave Pens <-> Slave Pens), then same map among non-holiday entries
    for nid, nd in lfg.items():
        if nd["type"] == 1 and nd["map"] == d["map"] and nd["name"] == d["name"] and nid in normal_by_id:
            return normal_by_id[nid]
    for nid, nd in lfg.items():
        if nd["type"] == 1 and nd["map"] == d["map"] and nd["exp"] == d["exp"] and nd["minl"] < 70 and nid in normal_by_id:
            return normal_by_id[nid]
    return []
derived = []
for hid, d in lfg.items():
    if d["type"] != 5: continue
    base = base_rows_for_heroic(d)
    for r in base:
        n = dict(r); n.update(lfg_id=hid, lfg_name=d["name"], difficulty=1)
        derived.append(n)
# normal entries drop heroic_only steps
rows = [r for r in rows if r["kind"] != "heroic_only"] + derived

# --- safety check: a regeneration must keep every row the current file has ---
# DLN-019: the current dungeon_routes.csv is authored in more places than routes.tsv can express -
# fixed path anchors and navigation waypoints (source=derived/spawn), game-object rows, tools/
# route_insert.py pull steps with their own numbering. The old guard compared (lfg_id, step, boss)
# for source=derived/gameobject only; a renumbered route slipped through it. Every existing row is
# now compared on lfg_id, difficulty, step, kind, boss, entry and x/y/z (0.01): anything the fresh
# resolve would drop or change is listed and nothing is written. --force overwrites anyway (a
# deliberate reconciliation, not a way to make a check pass); --dry-run only reports.
def _key(r):
    num = lambda v: "" if v in ("", None, "NULL") else f"{float(v):.2f}"
    return (str(r["lfg_id"]), str(r["difficulty"]), str(r["step"]), r["kind"], r["boss"], str(r["entry"]),
            num(r["x"]), num(r["y"]), num(r["z"]))

existing_path = f"{data}/dungeon_routes.csv"
lost = []
if os.path.exists(existing_path):
    with open(existing_path, newline="") as f:
        existing = list(csv.DictReader(f))
    fresh_keys = {_key(r) for r in rows}
    lost = [r for r in existing if _key(r) not in fresh_keys]
if lost:
    print(f"{len(lost)} row(s) of {existing_path} would be dropped or changed by this resolve:", file=sys.stderr)
    for r in lost:
        print(f"  lfg_id={r['lfg_id']} diff={r['difficulty']} step={r['step']} {r['kind']} '{r['boss']}' "
              f"entry={r['entry']} source={r['source']}", file=sys.stderr)
if "--dry-run" in sys.argv:
    print(f"dry run: {len(rows)} resolved rows, {len(lost)} existing row(s) not reproduced; nothing written")
    sys.exit(1 if lost else 0)
if lost and "--force" not in sys.argv:
    print("REFUSING TO OVERWRITE: routes.tsv does not reproduce the current file. dungeon_routes.csv and the "
          "SQL are the authority for authored rows (anchors, pull steps, game objects); edit them with "
          "tools/route_insert.py or by hand, and check with tools/check_route_sql.py.", file=sys.stderr)
    sys.exit(1)

with open(f"{data}/dungeon_routes.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader(); w.writerows(rows)

print(f"rows: {len(rows)}  lfg entries: {len({r['lfg_id'] for r in rows})}")
print("bosses without a static spawn on their map (script-spawned or name mismatch):")
print("\n".join(report) if report else "  none")
