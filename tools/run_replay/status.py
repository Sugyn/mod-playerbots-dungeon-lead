"""Generated part of docs/testing-status.md: per dungeon, the latest campaign evidence and whether the
dungeon is Verified (two FULL_ROUTE runs on the same commit and validation profile, health not
unhealthy) - from campaign artifacts (campaign.py), never by hand.

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


def verified(runs):
    """(commit, [run ids]) of the newest commit with two qualifying runs, else None."""
    by = {}
    for r in runs:
        if r["result"] == "FULL_ROUTE" and r["health"] != "unhealthy":
            by.setdefault((r["build"], r["profile"]), []).append(r)
    best = None
    for (build, _), rs in by.items():
        if len(rs) >= 2:
            newest = max(x["started_at"] or "" for x in rs)
            if not best or newest > best[0]:
                best = (newest, build, [x["run_id"] for x in rs])
    return (best[1], best[2]) if best else None


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
            level = "**Verified**"
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
