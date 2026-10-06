"""Compare two run models (old vs new): comparison.json and comparison.md.

Runs from different contexts are compared but flagged NOT DIRECTLY COMPARABLE with the reasons
(commit, route data, profile, dungeon) - never silently.
"""
COMPARISON_VERSION = 1

_ROWS = [
    ("result", lambda m, v: v["result"] if v else m["result"]["outcome"]),
    ("duration_min", lambda m, v: round(m["metadata"]["duration_ms"] / 60000, 1)),
    ("wipes", lambda m, v: m["metrics"]["wipes"]),
    ("deaths", lambda m, v: m["metrics"]["deaths"]),
    ("fights", lambda m, v: m["metrics"]["fights"]),
    ("fights_en_route", lambda m, v: m["metrics"].get("fights_en_route")),
    ("units_engaged", lambda m, v: m["metrics"]["units_engaged"]),
    ("unexpected_adds", lambda m, v: m["metrics"]["unexpected_adds"]),
    ("bosses_killed", lambda m, v: m["metrics"]["bosses_killed"]),
    ("boss_attempts_failed", lambda m, v: m["metrics"]["boss_attempts_failed"]),
    ("recoveries", lambda m, v: m["metrics"]["recoveries"]),
    ("interactions_failed", lambda m, v: m["metrics"]["interactions_failed"]),
    ("path_length_yd", lambda m, v: m["metrics"]["path_length_yd"]),
    ("alerts", lambda m, v: m["metrics"]["alerts"]),
]


def comparable(old, new):
    reasons = []
    o, n = old["metadata"], new["metadata"]
    od, nd = o.get("dungeon") or {}, n.get("dungeon") or {}
    if od.get("lfg_id") != nd.get("lfg_id"):
        reasons.append(f"dungeon {od.get('name')} vs {nd.get('name')}")
    if o["build"].get("commit_sha") != n["build"].get("commit_sha"):
        reasons.append(f"commit {o['build'].get('commit_sha')} vs {n['build'].get('commit_sha')}")
    if (o.get("scenario_id") or "") != (n.get("scenario_id") or ""):
        reasons.append(f"scenario {o.get('scenario_id') or '-'} vs {n.get('scenario_id') or '-'}")
    if not (o.get("complete_record") and n.get("complete_record")):
        reasons.append("a record starts mid-run")
    return reasons


def compare(old, new, old_verdict=None, new_verdict=None):
    rows = [{"metric": name, "old": f(old, old_verdict), "new": f(new, new_verdict)} for name, f in _ROWS]
    reasons = comparable(old, new)
    # a different commit is the point of most comparisons - it is reported, but only a different
    # dungeon / scenario / partial record makes the numbers not comparable
    blocking = [r for r in reasons if not r.startswith("commit")]
    return {"comparison_version": COMPARISON_VERSION,
            "old_run": old["metadata"]["run_id"], "new_run": new["metadata"]["run_id"],
            "directly_comparable": not blocking, "differences": reasons, "rows": rows}


def comparison_md(c):
    out = [f"# Run {c['old_run']} (old) vs {c['new_run']} (new)", ""]
    if not c["directly_comparable"]:
        out += ["**NOT DIRECTLY COMPARABLE**", ""]
    if c["differences"]:
        out += ["Context differences: " + "; ".join(c["differences"]), ""]
    out += ["| Metric | Old | New |", "|---|---|---|"]
    out += [f"| {r['metric']} | {r['old'] if r['old'] is not None else 'unknown'} | "
            f"{r['new'] if r['new'] is not None else 'unknown'} |" for r in c["rows"]]
    return "\n".join(out) + "\n"
