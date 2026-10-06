#!/usr/bin/env python3
"""Insert route steps into one dungeon's route, before a given step, and renumber the rest.

  tools/route_insert.py <lfg_id> <before_step> <rows.tsv> <update name> "<why>"

rows.tsv: kind<TAB>name<TAB>entry<TAB>x<TAB>y<TAB>z<TAB>note, one row per new step, in walk order.
Updates data/dungeon_routes.csv, data/routes.tsv, sql/playerbots_dungeon_route.sql and writes
sql/updates/<update name>.sql (replaces the dungeon's route). Run tools/routes_md.py and
tools/validate_routes.py afterwards.
"""
import csv
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def q(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def num(v):
    return v if v not in ("", None) else "NULL"


def main():
    lfg, before, rows_path, update, why = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
    new = []
    for line in open(rows_path):
        if line.strip() and not line.startswith("#"):
            kind, name, entry, x, y, z, *note = line.rstrip("\n").split("\t")
            if "," in name or (note and "," in note[0]):
                sys.exit(f"no commas in names or notes: {line}")
            new.append({"kind": kind, "boss": name, "entry": entry, "x": x, "y": y, "z": z, "note": note[0] if note else ""})

    path = os.path.join(ROOT, "data", "dungeon_routes.csv")
    text = open(path).read().splitlines()
    head, body = text[0], text[1:]
    fields = head.split(",")
    out, route = [], []
    for line in body:
        r = dict(zip(fields, next(csv.reader([line]))))
        if r["lfg_id"] != lfg:
            out.append(line)
            continue
        step = int(r["step"])
        if step == before:
            tmpl = r
            for i, n in enumerate(new):
                row = dict(tmpl, step=str(before + i), kind=n["kind"], boss=n["boss"], entry=n["entry"], x=n["x"], y=n["y"],
                           z=n["z"], source="spawn", note=n["note"])
                out.append(",".join(row[f] for f in fields))
                route.append(row)
        if step >= before:
            r["step"] = str(step + len(new))
            line = ",".join(r[f] for f in fields)
        out.append(line)
        route.append(r)
    if not route:
        sys.exit(f"lfg {lfg}: no route")
    open(path, "w").write("\n".join([head] + out) + "\n")

    name = route[0]["lfg_name"]
    mapid, diff = route[0]["map"], route[0]["difficulty"]
    wing = route[0]["wing"]
    vals = [f"({lfg},{mapid},{diff},{q(name)},{q(r['wing'])},{r['step']},{q(r['kind'])},{q(r['boss'])},{num(r['entry'])},"
            f"{num(r['x'])},{num(r['y'])},{num(r['z'])},{q(r['note'])})" for r in sorted(route, key=lambda r: int(r["step"]))]
    ins = ("INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,"
           "`entry`,`x`,`y`,`z`,`note`) VALUES\n" + ",\n".join(vals) + ";\n")
    with open(os.path.join(ROOT, "sql", "updates", update + ".sql"), "w") as f:
        f.write("".join(f"-- {ln}\n" for ln in why.splitlines()) + f"-- Replaces the {name} route.\n"
                f"DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`={lfg};\n" + ins)

    sql = os.path.join(ROOT, "sql", "playerbots_dungeon_route.sql")
    lines = open(sql).read().split("\n")
    idx = [i for i, l in enumerate(lines) if l.startswith(f"({lfg},{mapid},")]
    end = lines[idx[-1]][-1]
    newl = [v + "," for v in vals]
    newl[-1] = newl[-1][:-1] + end
    lines[idx[0]:idx[-1] + 1] = newl
    open(sql, "w").write("\n".join(lines))

    tsv = os.path.join(ROOT, "data", "routes.tsv")
    t = open(tsv).read().split("\n")
    ti = [i for i, l in enumerate(t) if l.startswith(f"{lfg}\t")]
    old = {(l.split("\t")[3], l.split("\t")[4]): (l.split("\t")[5] if len(l.split("\t")) > 5 else "") for l in (t[i] for i in ti)}
    tl = []
    for r in sorted(route, key=lambda r: int(r["step"])):
        note = old.get((r["kind"], r["boss"]), r["note"])
        tl.append(f"{lfg}\t{r['wing']}\t{r['step']}\t{r['kind']}\t{r['boss']}" + (f"\t{note}" if note else ""))
    t[ti[0]:ti[-1] + 1] = tl
    open(tsv, "w").write("\n".join(t))
    print(f"lfg {lfg}: {len(new)} steps inserted before step {before}; route has {len(route)} steps")


if __name__ == "__main__":
    main()
