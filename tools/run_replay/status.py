"""Generated part of docs/testing-status.md: per dungeon, the latest campaign evidence and whether the
dungeon is Verified - from campaign artifacts (campaign.py), never by hand.

Verified (DLN-024): two distinct runs (by run id - one run listed in several campaigns counts once)
with FULL_ROUTE, health not unhealthy and a complete record, on the same run context (context_id:
module, core, mod-playerbots, route table, config, mmaps) and validation profile, the context
complete (nothing unknown, module not dirty). Runs recorded before the run context existed (no
context_id) can only reach "Verified (pre-context)": the earlier contract, two such runs on the same
module commit and profile.

  python3 -m tools.run_replay.status <validation_runs dir> [--write docs/testing-status.md]

Without --write the section is printed. With it, the text between the GENERATED markers is replaced.
"""
import glob
import json
import os
import sys

BEGIN = "<!-- GENERATED: tools/run_replay/status.py - do not edit by hand -->"
END = "<!-- END GENERATED -->"


def load(vr):
    runs = []
    for idx_path in glob.glob(os.path.join(vr, "campaigns", "*", "index.json")):
        idx = json.load(open(idx_path))
        for r in idx["runs"]:
            if not r.get("lfg_id"):
                continue
            runs.append(dict(r, campaign=idx["campaign_id"], build=idx.get("build"),
                             profile=idx.get("validation_profile")))
    return runs


def _qualifying(r):
    return r["result"] == "FULL_ROUTE" and r["health"] != "unhealthy" and r.get("complete_record", True) is not False


def verified(runs):
    """(build, [run ids], contract) for the newest qualifying pair, else None. contract is "context"
    (same complete run context) or "pre-context" (runs without a context, same commit)."""
    by = {}
    for r in runs:
        if not _qualifying(r):
            continue
        if r.get("context_id"):
            if r.get("context_incomplete"):
                continue  # unknown part or dirty module: evidence, not a qualification
            key = ("context", r["context_id"], r["profile"])
        else:
            key = ("pre-context", r["build"], r["profile"])
        by.setdefault(key, {})[r["run_id"]] = r  # distinct runs only
    best = None
    for (contract, _, _), rs in by.items():
        if len(rs) < 2:
            continue
        newest = max(x["started_at"] or "" for x in rs.values())
        rank = (contract == "context", newest)
        if not best or rank > best[0]:
            build = max(rs.values(), key=lambda x: x["started_at"] or "")["build"]
            best = (rank, build, sorted(rs), contract)
    return (best[1], best[2], best[3]) if best else None


def section(vr):
    runs = load(vr)
    by_dungeon = {}
    for r in runs:
        by_dungeon.setdefault(r["dungeon"], []).append(r)
    lines = [BEGIN, "",
             "| Dungeon | Level | Verified on | Latest build | Latest runs (newest first) |",
             "|---|---|---|---|---|"]
    for name in sorted(by_dungeon):
        rs = sorted(by_dungeon[name], key=lambda r: r["started_at"] or "", reverse=True)
        v = verified(rs)
        latest_build = rs[0]["build"]
        latest = [r for r in rs if r["build"] == latest_build][:4]
        cells = "; ".join(
            f"{r['result'].replace('_', ' ').lower()}" + (f" ({r['health']})" if r["health"] != "clean" else "") +
            (f" {', '.join(sorted(set(r['findings'])))}" if r["findings"] else "")
            for r in latest)
        if v:
            level = "**Verified**" if v[2] == "context" else "Verified (pre-context)"
        elif any(r["result"] == "FULL_ROUTE" for r in rs):
            level = "Full route"
        elif any(r["result"] == "PARTIAL" for r in rs):
            level = "Partial"
        else:
            level = "Smoke"
        lines.append(f"| {name} | {level} | {('`' + v[0] + '`') if v else '-'} | `{latest_build}` | {cells} |")
    camps = sorted({r["campaign"] for r in runs})
    lines += ["", f"From {len(runs)} runs in {len(camps)} telemetry v2 campaigns ({camps[0]} … {camps[-1]})."
              if camps else "No campaigns.", "", END]
    return "\n".join(lines)


def main():
    vr = sys.argv[1]
    text = section(vr)
    if "--write" in sys.argv:
        path = sys.argv[sys.argv.index("--write") + 1]
        doc = open(path).read()
        if BEGIN in doc and END in doc:
            a, b = doc.index(BEGIN), doc.index(END) + len(END)
            doc = doc[:a] + text + doc[b:]
        else:
            raise SystemExit(f"{path}: no GENERATED markers")
        open(path, "w").write(doc)
        print(f"{path}: generated section updated")
    else:
        print(text)


if __name__ == "__main__":
    main()
