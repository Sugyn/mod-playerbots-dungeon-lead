#!/usr/bin/env python3
"""Render dungeon_routes.csv into a readable Markdown overview (normal entries only; heroics share the map)."""
import csv, os
here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(os.path.dirname(here), "data")  # inputs/outputs live in ../data, not next to the script
rows = list(csv.DictReader(open(f"{data}/dungeon_routes.csv")))

exp_name = {"0": "Vanilla", "1": "The Burning Crusade", "2": "Wrath of the Lich King"}
kind_mark = {"boss": "", "optional": " *(optional)*", "heroic_only": " *(heroic only)*",
             "event": " **[event]**", "door": " **[door]**", "skip": " **[UNSUPPORTED]**"}

# group normal entries by expansion, then lfg name; note heroic ids
by_map_heroic = {}
for r in rows:
    if r["difficulty"] == "1":
        by_map_heroic.setdefault(r["map"], r["lfg_id"])

out = ["# Dungeon routes (LFD 3.3.5a) — boss order per LFD entry", "",
       "Legend: `spawn` = static spawn in `creature` table (entry + x/y/z resolved), `script` = template exists but is "
       "spawned by instance script/summon, `unknown` = no template match. Heroic LFD ids share the normal route "
       "(same map); `heroic only` steps apply to the heroic id only. Sources per dungeon are in `routes.tsv` comments.", ""]

cur_exp, cur_lfg = None, None
for r in rows:
    if r["difficulty"] == "1" and r["kind"] != "heroic_only":
        continue                    # heroic rows are duplicates of normal ones
    if r["expansion"] != cur_exp:
        cur_exp = r["expansion"]; out += [f"## {exp_name[cur_exp]}", ""]
    if r["lfg_id"] != cur_lfg and not (r["difficulty"] == "1"):
        cur_lfg = r["lfg_id"]
        h = by_map_heroic.get(r["map"])
        htxt = f", heroic LFD {h}" if h else ""
        out += [f"### {r['lfg_name']} (LFD {r['lfg_id']}, map {r['map']}{htxt})", ""]
    if r["difficulty"] == "1" and r["kind"] == "heroic_only":
        # printed under the normal section as an extra line
        pass
    if r["entry"] and r["x"] not in ("", "NULL"):
        pos = f" — entry {r['entry']} @ ({r['x']}, {r['y']}, {r['z']})"
    elif r["entry"]:
        pos = f" — entry {r['entry']} (no static spawn)"
    else:
        pos = ""
    src = "" if r["source"] in ("spawn", "-") else f" `{r['source']}`"
    note = f" — {r['note']}" if r["note"] else ""
    wing = f"[{r['wing']}] " if r["wing"] != "-" else ""
    out.append(f"{r['step']}. {wing}**{r['boss']}**{kind_mark.get(r['kind'], '')}{src}{pos}{note}")
    if r["step"] == "1" and False:
        pass
out.append("")
open(f"{data}/dungeon_routes.md", "w").write("\n".join(out))
print(f"wrote dungeon_routes.md ({len(out)} lines)")
