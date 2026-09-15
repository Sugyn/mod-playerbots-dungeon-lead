#!/usr/bin/env python3
"""T0 fast validation for the resolved route data (data/dungeon_routes.csv), cross-checked against
data/lfg_dungeons.tsv. Server-independent, runs in well under a second - meant to be run after any
change to routes.tsv/dungeon_routes.csv, and by CI.

Checks (see the architecture roadmap's L1.2):
  - every lfg_id exists in lfg_dungeons.tsv
  - map/difficulty on each row matches that lfg_id's LFD entry
  - kind is one of the values DungeonRouteStep/IsWalkable() actually understands
  - no duplicate (lfg_id, step) rows
  - step numbers per lfg_id are contiguous from 1 (warning, not an error - not load-bearing)
  - a "boss" (mandatory - see DungeonRouteStep::IsMandatory()) row always has a resolved position;
    one without silently becomes invisible to the runtime (IsWalkable() == false, skipped with no
    outcome signal at all) rather than an obvious failure - this is the single most valuable check
    here
  - x/y/z parse as finite numbers
  - flags rows whose source is "script" (creature template exists but isn't spawned on that map -
    resolve_routes.py's own ambiguity marker) as a warning, not an error

Exit code is 0 only if there are zero ERRORs (warnings don't fail the run).
"""
import csv
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"

VALID_KINDS = {"boss", "optional", "heroic_only", "event", "door", "skip"}


def load_lfg_dungeons():
    lfg = {}
    with open(DATA / "lfg_dungeons.tsv") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            lfg[int(p[0])] = dict(name=p[1], map=int(p[4]), diff=int(p[5]))
    return lfg


def is_finite_float(s):
    try:
        return math.isfinite(float(s))
    except (ValueError, TypeError):
        return False


def parse_coord(s):
    """Parses a CSV coordinate cell, treating '', '0' and the literal sentinel 'NULL' (this
    project's convention for "no static position, script/quest-spawned - see resolve_routes.py's
    'source' column) the same way the runtime does: the SQL loader wraps every column in
    IFNULL(x, 0), so an actual SQL NULL becomes 0.0 by the time DungeonRouteStep sees it."""
    if s is None or s.strip().upper() in ("", "NULL"):
        return 0.0, True  # (value, was_unresolved)
    try:
        return float(s), False
    except ValueError:
        return float("nan"), False


def main():
    lfg = load_lfg_dungeons()
    rows = list(csv.DictReader(open(DATA / "dungeon_routes.csv")))

    errors = []
    warnings = []
    seen_steps = {}  # (lfg_id, step) -> row number, for duplicate detection
    steps_by_lfg = {}  # lfg_id -> set(step)

    for i, r in enumerate(rows, start=2):  # start=2: header is line 1
        lfg_id = int(r["lfg_id"])
        step = int(r["step"])
        kind = r["kind"]
        boss = r["boss"]
        entry = r["entry"]
        loc = f"line {i} (lfg_id={lfg_id} step={step} '{boss}')"

        if lfg_id not in lfg:
            errors.append(f"{loc}: lfg_id {lfg_id} not found in lfg_dungeons.tsv")
        else:
            d = lfg[lfg_id]
            if int(r["map"]) != d["map"]:
                errors.append(f"{loc}: map {r['map']} != lfg_dungeons.tsv map {d['map']} for lfg_id {lfg_id}")
            if int(r["difficulty"]) != d["diff"]:
                errors.append(
                    f"{loc}: difficulty {r['difficulty']} != lfg_dungeons.tsv diff {d['diff']} for lfg_id {lfg_id}"
                )

        if kind not in VALID_KINDS:
            errors.append(f"{loc}: unknown kind '{kind}' (valid: {sorted(VALID_KINDS)})")

        if not boss.strip():
            errors.append(f"{loc}: empty boss/step name")

        key = (lfg_id, step)
        if key in seen_steps:
            errors.append(f"{loc}: duplicate step {step} for lfg_id {lfg_id} (first seen at line {seen_steps[key]})")
        else:
            seen_steps[key] = i
        steps_by_lfg.setdefault(lfg_id, set()).add(step)

        # Mirrors DungeonRouteStep::HasPosition() exactly: entry != 0 && !(x==0 && y==0 && z==0).
        # The SQL loader wraps every coordinate column in IFNULL(x, 0), so an unresolved ('NULL')
        # coordinate becomes 0.0 by the time the runtime sees it - same as this parse_coord().
        try:
            entry_val = int(entry) if entry.strip() and entry.strip().upper() != "NULL" else 0
        except ValueError:
            entry_val = 0
            errors.append(f"{loc}: entry={entry!r} does not parse as an integer")
        xv, x_unresolved = parse_coord(r["x"])
        yv, y_unresolved = parse_coord(r["y"])
        zv, z_unresolved = parse_coord(r["z"])
        any_unresolved = x_unresolved or y_unresolved or z_unresolved
        has_position = entry_val != 0 and not (xv == 0 and yv == 0 and zv == 0)

        if kind == "boss" and not has_position:
            errors.append(
                f"{loc}: mandatory 'boss' step has no resolved position (HasPosition() would be "
                f"false at runtime) - IsWalkable() will silently skip it with no route-outcome "
                f"signal at all, not even a visible failure"
            )

        for name, v in (("x", xv), ("y", yv), ("z", zv)):
            if not math.isfinite(v):
                errors.append(f"{loc}: {name} does not parse as a finite number")

        if kind == "heroic_only" and not has_position:
            # 2026-09-15 (independent architecture review DL-021 - "heroic-only objectives are
            # accepted but permanently inert"): unlike a genuinely optional boss (fine to be
            # unresolved - nobody claimed it works), a heroic_only row exists specifically to
            # represent supported heroic content. resolve_routes.py only resolves kind in
            # (boss, optional, event) - heroic_only was never in that list - so every heroic_only
            # row is unresolved by construction today, and IsWalkable() silently treats it as "not
            # walkable" at runtime: it shows up in coverage/route-length reports but can never
            # actually be navigated to or verified. Surfaced as its own warning (not folded into
            # the general any_unresolved/has_position pass-through below) so this doesn't read as
            # the same "fine, expected" case as a real optional gap.
            warnings.append(
                f"{loc}: heroic_only step has no resolved position - resolve_routes.py doesn't "
                f"resolve this kind yet (see DL-021), so this row is silently inert at runtime "
                f"despite counting toward this dungeon's reported route coverage"
            )
        elif any_unresolved and not has_position:
            # legitimate and already covered by the boss-mandatory check above if it matters;
            # for optional/event this is just "not walkable", which is fine and expected (see
            # 'source=script' below) - not worth a second warning of its own
            pass
        elif has_position and (xv == 0 or yv == 0 or zv == 0) and r.get("source") == "spawn":
            # NOT the same as "no position" (HasPosition() only excludes all-three-zero), but a
            # single axis sitting at exactly 0.0 from a real DB spawn (not a script/NULL sentinel)
            # is unusual enough in practice to be worth a human glance
            warnings.append(f"{loc}: x={xv} y={yv} z={zv} - one axis is exactly 0, worth double-checking")

        if r.get("source") == "script":
            warnings.append(f"{loc}: source=script - creature template exists but has no static spawn on this map")

    for lfg_id, steps in steps_by_lfg.items():
        expected = set(range(1, len(steps) + 1))
        if steps != expected:
            warnings.append(
                f"lfg_id {lfg_id} ({lfg.get(lfg_id, {}).get('name', '?')}): step numbers {sorted(steps)} "
                f"are not contiguous from 1 (not load-bearing, but usually means a gap/typo)"
            )

    print(f"rows: {len(rows)}  lfg entries: {len(steps_by_lfg)}")
    print(f"ERRORS: {len(errors)}")
    for e in errors:
        print(f"  ERROR: {e}")
    print(f"WARNINGS: {len(warnings)}")
    for w in warnings:
        print(f"  WARN: {w}")

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
