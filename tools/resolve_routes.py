#!/usr/bin/env python3
"""Resolve hand-authored dungeon routes (boss names) against DB dumps.

Input  : routes.tsv   lfg_id \t wing \t step \t kind \t boss_name \t note
DB     : lfg_dungeons.tsv, boss_positions.tsv, extra_bosses.tsv, instance_bosses.tsv
Output : dungeon_routes.csv (resolved) + report to stdout

NOTE: boss_positions.tsv and extra_bosses.tsv are raw exports from a world database
(creature/creature_template joined by name, roughly `map entry name rank x y z spawnMask` and
`entry name rank map spawns x y z` respectively) and are NOT checked into data/ - only the
already-resolved output (data/dungeon_routes.csv/.md) is. Re-running this script from a clean
checkout needs those two re-exported from a 3.3.5a world DB first; lfg_dungeons.tsv and routes.tsv
are the only inputs that ship with this repo.
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

with open(f"{data}/dungeon_routes.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader(); w.writerows(rows)

print(f"rows: {len(rows)}  lfg entries: {len({r['lfg_id'] for r in rows})}")
print("bosses without a static spawn on their map (script-spawned or name mismatch):")
print("\n".join(report) if report else "  none")
