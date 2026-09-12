#!/usr/bin/env python3
"""Reorder the freely-explorable boss/optional stops of each dungeon route so that consecutive
steps are always connected by a real path in the travelnode graph (nearest-neighbor over Dijkstra
shortest-path distance), instead of the hand-authored "clearing guide" order which can jump
between nodes with no direct edge at all - which is exactly what made bots get stuck/skip stops
in Wailing Caverns (Anacondra->Cobrahn, Kresh->Pythas, Skum->Serpentis all had NO edge).

Door/event steps are left in place as fixed anchors (they encode real game-logic ordering, e.g.
"open a gate", "kill triggers a summon") - only the freely-orderable boss/optional/heroic_only
runs *between* anchors get reordered, using the first step of each run as the fixed entry point.

NOTE: graph_nodes.tsv (node name/position + adjacency, one row per playerbots_travelnode /
playerbots_travelnode_link row) is not checked into data/ - it's a raw export of those two DB
tables and needs re-dumping from a live playerbots DB to rerun this. The output that was actually
applied is `routes.tsv` itself (via apply_reorder.py) - this script is kept for the record and for
redoing the exercise on a future route change, not for CI reproducibility of the current data.
"""
import csv, heapq, sys, os
here = os.path.dirname(os.path.abspath(__file__))

# --- load graph: node name (per map) -> id/pos, and Dijkstra-ready adjacency ---
nodes = {}       # (map_id, name) -> (id, x, y, z)
node_by_id = {}  # id -> (map_id, name)
adj = {}         # node_id -> [(neighbor_id, dist), ...]

with open(f"{here}/graph_nodes.tsv") as f:
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 6:
            continue
        nid, name, mapid, x, y, z = int(p[0]), p[1], int(p[2]), float(p[3]), float(p[4]), float(p[5])
        nodes[(mapid, name)] = (nid, x, y, z)
        node_by_id[nid] = (mapid, name)
        adj.setdefault(nid, [])

with open(f"{here}/graph_links.tsv") as f:
    for line in f:
        p = line.rstrip("\n").split("\t")
        if len(p) < 3 or p[2] == "NULL" or p[2] == "":
            continue
        a, b, d = int(p[0]), int(p[1]), float(p[2])
        adj.setdefault(a, []).append((b, d))

def dijkstra(src):
    dist = {src: 0.0}
    pq = [(0.0, src)]
    while pq:
        d, u = heapq.heappop(pq)
        if d > dist.get(u, float("inf")):
            continue
        for v, w in adj.get(u, []):
            nd = d + w
            if nd < dist.get(v, float("inf")):
                dist[v] = nd
                heapq.heappush(pq, (nd, v))
    return dist

_dijkstra_cache = {}
def graph_dist(nid_a, nid_b):
    if nid_a not in _dijkstra_cache:
        _dijkstra_cache[nid_a] = dijkstra(nid_a)
    return _dijkstra_cache[nid_a].get(nid_b)

def euclid(a, b):
    return ((a[1]-b[1])**2 + (a[2]-b[2])**2 + (a[3]-b[3])**2) ** 0.5

FREE_KINDS = {"boss", "optional", "heroic_only"}

def reorder_segment(mapid, seg):
    """seg: list of row dicts (already resolved, with node info attached). First stays fixed."""
    if len(seg) <= 2:
        return seg
    fixed, rest = seg[0], seg[1:]
    ordered = [fixed]
    cur = fixed["_node"]
    remaining = rest
    used_fallback = False
    while remaining:
        best_i, best_d, best_is_graph = None, None, False
        for i, cand in enumerate(remaining):
            cn = cand["_node"]
            d = None
            is_graph = False
            if cur and cn:
                gd = graph_dist(cur[0], cn[0])
                if gd is not None:
                    d, is_graph = gd, True
            if d is None and cur and cn:
                d = euclid(cur, cn)
                used_fallback = True
            if d is not None and (best_d is None or d < best_d):
                best_i, best_d, best_is_graph = i, d, is_graph
        if best_i is None:  # no positions at all among remaining (shouldn't happen) - keep order
            ordered.extend(remaining)
            break
        nxt = remaining.pop(best_i)
        nxt["_hop_dist"] = best_d
        nxt["_hop_graph"] = best_is_graph
        ordered.append(nxt)
        cur = nxt["_node"]
    return ordered

rows = list(csv.DictReader(open(f"{here}/dungeon_routes.csv")))
# work per lfg_id, difficulty==0 only (normal); heroics are re-derived by resolve_routes.py
by_lfg = {}
for r in rows:
    if r["difficulty"] != "0":
        continue
    by_lfg.setdefault(r["lfg_id"], []).append(r)

report = []
out_order = {}  # lfg_id -> ordered list of boss names (for routes.tsv rewrite)

for lfg_id, rs in by_lfg.items():
    rs.sort(key=lambda r: int(r["step"]))
    mapid = int(rs[0]["map"])
    # attach graph node (match by exact name on this map)
    for r in rs:
        key = (mapid, r["boss"])
        r["_node"] = nodes.get(key)

    # split into segments at door/event boundaries; door/event rows are anchors, kept in place
    segments = []   # list of (anchor_before_or_None, [free rows], anchor_after_or_None)
    cur_seg = []
    result = []
    for r in rs:
        if r["kind"] in FREE_KINDS and r["_node"]:
            cur_seg.append(r)
        else:
            if cur_seg:
                result.append(("SEG", cur_seg))
                cur_seg = []
            result.append(("ANCHOR", r))
    if cur_seg:
        result.append(("SEG", cur_seg))

    new_rs = []
    changed = False
    for kind, payload in result:
        if kind == "ANCHOR":
            new_rs.append(payload)
        else:
            reordered = reorder_segment(mapid, payload)
            if [x["boss"] for x in reordered] != [x["boss"] for x in payload]:
                changed = True
            new_rs.extend(reordered)

    if changed:
        old_order = [r["boss"] for r in rs]
        new_order = [r["boss"] for r in new_rs]
        report.append((lfg_id, rs[0]["lfg_name"], old_order, new_order))
    out_order[lfg_id] = [r["boss"] for r in new_rs]

print(f"Dungeons with a changed order: {len(report)} / {len(by_lfg)}")
for lfg_id, name, old, new in report:
    print(f"\n[{lfg_id}] {name}")
    print(f"  old: {' -> '.join(old)}")
    print(f"  new: {' -> '.join(new)}")

import json
json.dump(out_order, open(f"{here}/reordered_routes.json", "w"), indent=1)
print(f"\nwrote reordered_routes.json ({len(out_order)} dungeons)")
