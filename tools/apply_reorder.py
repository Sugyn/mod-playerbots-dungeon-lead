#!/usr/bin/env python3
"""Rewrite routes.tsv: for the accepted lfg_ids, reorder their rows to the new boss sequence
(from accepted_reorders.json) and renumber step, keeping each row's own kind/wing/note attached
to its boss name. Rows for lfg_ids not in the accepted set are left byte-for-byte untouched.

NOTE: accepted_reorders.json (the manually-reviewed subset of reorder_routes.py's suggested
orderings, with the 5 dependency-violating ones excluded - see CHANGELOG 0.3.0) isn't checked in;
it was a one-shot editorial decision already baked into the current data/routes.tsv. This script
is kept for the record, not for CI reproducibility."""
import json, os
here = os.path.dirname(os.path.abspath(__file__))
accepted = json.load(open(f"{here}/accepted_reorders.json"))

lines = open(f"{here}/routes.tsv").read().split("\n")
out = []
i = 0
touched = set()
while i < len(lines):
    line = lines[i]
    if not line.strip() or line.startswith("#"):
        out.append(line)
        i += 1
        continue
    lfg_id = line.split("\t")[0]
    if lfg_id not in accepted:
        out.append(line)
        i += 1
        continue
    # collect the contiguous block for this lfg_id
    block = []
    while i < len(lines) and lines[i].strip() and not lines[i].startswith("#") and lines[i].split("\t")[0] == lfg_id:
        block.append(lines[i])
        i += 1
    # heroic_only rows never took part in reordering (dropped from the normal-mode CSV the
    # reorder script read) - keep them pinned at their original relative slot in the block.
    by_boss = {}
    for row in block:
        p = row.split("\t")
        if p[3] != "heroic_only":
            by_boss[p[4]] = p
    new_order = accepted[lfg_id]
    assert set(new_order) == set(by_boss.keys()), (lfg_id, set(new_order) ^ set(by_boss.keys()))
    queue = list(new_order)
    result_block = []
    for row in block:
        p = row.split("\t")
        if p[3] == "heroic_only":
            result_block.append(list(p))
        else:
            result_block.append(list(by_boss[queue.pop(0)]))
    for step, p in enumerate(result_block, start=1):
        p[2] = str(step)
        out.append("\t".join(p))
    touched.add(lfg_id)

assert touched == set(accepted.keys()), touched ^ set(accepted.keys())
open(f"{here}/routes.tsv", "w").write("\n".join(out))
print(f"rewrote routes.tsv, reordered lfg_ids: {sorted(touched, key=int)}")
