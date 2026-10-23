#!/usr/bin/env python3
"""Check that the route data agrees across its three places (DLN-019).

  tools/check_route_sql.py                      CSV == sql/playerbots_dungeon_route.sql (offline)
  tools/check_route_sql.py --upgrade-from REV --mysql "CMD"
                                                 also: fresh install == the base SQL of git REV
                                                 plus every sql/updates/*.sql in name order,
                                                 rehearsed in two disposable databases

CMD runs the mysql client with SQL on stdin (e.g. `ssh acore-clean-root mysql`); the databases
dln_route_check_fresh / dln_route_check_upgrade are created, compared and dropped - nothing else is
touched. Rows are compared on lfg_id, map_id, difficulty, step, kind, boss, entry and x/y/z
(rounded to 0.01); notes and names are not compared. Exit 1 on any difference.
"""
import csv
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(ROOT, "data", "dungeon_routes.csv")
BASE_SQL = os.path.join(ROOT, "sql", "playerbots_dungeon_route.sql")
UPDATES = os.path.join(ROOT, "sql", "updates")
COLUMNS = ("lfg_id", "map_id", "difficulty", "name", "wing", "step", "kind", "boss", "entry", "x", "y", "z", "note")


def _num(v):
    return "" if v in ("", None, "NULL") else f"{float(v):.2f}"


def _int(v):
    return "" if v in ("", None, "NULL") else str(int(float(v)))


def row_key(lfg_id, map_id, difficulty, step, kind, boss, entry, x, y, z):
    return (int(lfg_id), int(map_id), int(difficulty), int(step), kind, boss, _int(entry), _num(x), _num(y), _num(z))


def csv_rows(path=CSV):
    with open(path, newline="") as f:
        return {row_key(r["lfg_id"], r["map"], r["difficulty"], r["step"], r["kind"], r["boss"], r["entry"],
                        r["x"], r["y"], r["z"]) for r in csv.DictReader(f)}


def parse_values(sql):
    """The value tuples of the INSERT statements in `sql`, as lists of strings (None for NULL)."""
    out, i, n = [], 0, len(sql)
    while True:
        i = sql.find("VALUES", i)
        if i < 0:
            return out
        i += len("VALUES")
        while i < n:
            while i < n and sql[i] in " \t\r\n,":
                i += 1
            if i >= n or sql[i] != "(":
                break
            i += 1
            vals, cur, quoted = [], "", False
            while i < n:
                c = sql[i]
                if quoted:
                    if c == "\\" and i + 1 < n:
                        cur += sql[i + 1]
                        i += 2
                        continue
                    if c == "'":
                        quoted = False
                    else:
                        cur += c
                elif c == "'":
                    quoted = True
                elif c in ",)":
                    vals.append(None if cur.strip() == "NULL" else cur.strip())
                    cur = ""
                    if c == ")":
                        i += 1
                        break
                else:
                    cur += c
                i += 1
            out.append(vals)


def sql_rows(path=BASE_SQL):
    rows = set()
    for v in parse_values(open(path, encoding="utf-8").read()):
        d = dict(zip(COLUMNS, v))
        rows.add(row_key(d["lfg_id"], d["map_id"], d["difficulty"], d["step"], d["kind"], d["boss"], d["entry"],
                         d["x"], d["y"], d["z"]))
    return rows


def report(name_a, a, name_b, b):
    only_a, only_b = sorted(a - b), sorted(b - a)
    print(f"{name_a}: {len(a)} rows, {name_b}: {len(b)} rows, only in {name_a}: {len(only_a)}, "
          f"only in {name_b}: {len(only_b)}")
    for r in only_a[:20]:
        print(f"  only {name_a}: {r}")
    for r in only_b[:20]:
        print(f"  only {name_b}: {r}")
    return not only_a and not only_b


def mysql(cmd, sql):
    p = subprocess.run(cmd, shell=True, input=sql, capture_output=True, text=True)
    if p.returncode:
        raise SystemExit(f"mysql failed: {p.stderr.strip()}")
    return p.stdout


def db_rows(cmd, db):
    out = mysql(cmd, f"SELECT lfg_id,map_id,difficulty,step,kind,boss,IFNULL(entry,'NULL'),IFNULL(x,'NULL'),"
                     f"IFNULL(y,'NULL'),IFNULL(z,'NULL') FROM `{db}`.`playerbots_dungeon_route`;\n")
    rows = set()
    for line in out.splitlines()[1:]:
        p = line.split("\t")
        rows.add(row_key(*p))
    return rows


def upgrade_check(rev, cmd):
    old_base = subprocess.run(["git", "-C", ROOT, "show", f"{rev}:sql/playerbots_dungeon_route.sql"],
                              capture_output=True, text=True, check=True).stdout
    updates = sorted(f for f in os.listdir(UPDATES) if f.endswith(".sql"))
    fresh, upgrade = "dln_route_check_fresh", "dln_route_check_upgrade"
    try:
        mysql(cmd, f"DROP DATABASE IF EXISTS `{fresh}`; DROP DATABASE IF EXISTS `{upgrade}`; "
                   f"CREATE DATABASE `{fresh}`; CREATE DATABASE `{upgrade}`;\n")
        mysql(cmd, f"USE `{fresh}`;\n" + open(BASE_SQL, encoding="utf-8").read())
        mysql(cmd, f"USE `{upgrade}`;\n" + old_base)
        for u in updates:
            mysql(cmd, f"USE `{upgrade}`;\n" + open(os.path.join(UPDATES, u), encoding="utf-8").read())
        print(f"upgrade path: base SQL of {rev} + {len(updates)} update(s)")
        return report("fresh", db_rows(cmd, fresh), "upgrade", db_rows(cmd, upgrade))
    finally:
        mysql(cmd, f"DROP DATABASE IF EXISTS `{fresh}`; DROP DATABASE IF EXISTS `{upgrade}`;\n")


def main():
    ok = report("csv", csv_rows(), "base_sql", sql_rows())
    if "--upgrade-from" in sys.argv:
        rev = sys.argv[sys.argv.index("--upgrade-from") + 1]
        cmd = sys.argv[sys.argv.index("--mysql") + 1]
        ok = upgrade_check(rev, cmd) and ok
    print("OK" if ok else "MISMATCH")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
