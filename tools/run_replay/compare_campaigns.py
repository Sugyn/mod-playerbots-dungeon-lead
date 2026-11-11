"""Campaign comparison: is campaign B better, worse or the same as campaign A?

  python3 -m tools.run_replay.cli compare-campaigns A B [-d DIR] [--accept PART]...
      -> campaign_comparison.json, campaign_comparison.md; the verdicts on stdout

A and B are anything `summary` reads (a campaign directory, a validation_runs directory, a run
directory) or a summary.json it wrote. Per dungeon (both sides' finished runs of it) and overall,
each metric is old / new / delta, and the rules below give better / worse / same; a dungeon is
worse if any rule says worse, better if at least one says better and none worse, else the same.
Overall: worse if any dungeon is worse, better if one is better and none worse, the same if all
compared dungeons are the same. With fewer than LOW_SAMPLE_RUNS finished runs of a dungeon on a
side the verdict is marked as a low sample.

Not comparable: a dungeon with no finished run on one side, a different validation profile, or a
run-context part other than the module that differs (core, mod-playerbots, route table, config,
mmaps) - unless that part is named with --accept (e.g. a route data change under test). A module
difference is the point of a comparison and never blocks it. Runs without a run context are
reported and do not block.
"""
import json
import os

from . import run_metrics, summary

COMPARISON_VERSION = 1

# The rules. Small campaigns are noisy: these are the smallest changes reported as a change.
FULL_ROUTE_RATE_DELTA = 0.2      # FULL_ROUTE share of finished runs: one run in five
DURATION_REL_DELTA = 0.10        # median full-route duration: more than 10 %
WIPES_PER_RUN_DELTA = 0.5        # wipes per run
DEATHS_PER_RUN_DELTA = 1.0       # deaths per run
DEAD_TARGET_S_PER_RUN_DELTA = 10.0  # seconds per run spent on dead or absent targets
CC_RATIO_DELTA = 0.1             # CC landed ratio ...
CC_MIN_ENDED = 3                 # ... when both sides have at least this many ended moons
# Teleports the module decides on. A new kind (none in A, some in B) is worse, a kind gone is
# better. Wipe-recovery teleports follow deaths and are judged by the deaths rule.
DECIDED_TELEPORTS = ("leader_unstuck", "leader_to_floor", "member_to_leader", "unexpected")
# Fewer finished runs of a dungeon on either side than this: the verdict stands but is marked as
# a low sample - one run more or less moves the FULL_ROUTE rate past its threshold.
LOW_SAMPLE_RUNS = 5

BLOCKING_PARTS = ("core_sha", "playerbots_sha", "route_digest", "config_digest", "mmap_digest")


def load_side(path):
    if os.path.isfile(path) and path.endswith(".json"):
        with open(path, encoding="utf-8") as f:
            s = json.load(f)
        if s.get("summary_version") != summary.SUMMARY_VERSION:
            raise SystemExit(f"{path}: summary_version {s.get('summary_version')!r}, this tool reads "
                             f"{summary.SUMMARY_VERSION}")
        return s
    return summary.build([path])


def _delta(old, new, digits=3):
    return None if old is None or new is None else round(new - old, digits)


def _by_threshold(old, new, threshold, higher_is_better):
    if old is None or new is None:
        return "n/a"
    d = new - old
    if abs(d) < threshold - 1e-9:
        return "same"
    return "better" if (d > 0) == higher_is_better else "worse"


def _rules(a, b):
    """[(metric, old, new, delta, verdict)] for two aggregates (summary.aggregate)."""
    rows = []
    rows.append(("full_route_rate", a["full_route_rate"], b["full_route_rate"],
                 _delta(a["full_route_rate"], b["full_route_rate"]),
                 _by_threshold(a["full_route_rate"], b["full_route_rate"], FULL_ROUTE_RATE_DELTA, True)))
    ma, mb = a["full_route_duration_min"]["median"], b["full_route_duration_min"]["median"]
    if ma and mb is not None:
        rel = (mb - ma) / ma
        v = "same" if abs(rel) <= DURATION_REL_DELTA else ("better" if rel < 0 else "worse")
    else:
        v = "n/a"
    rows.append(("full_route_median_min", ma, mb, _delta(ma, mb, 1), v))
    for key, threshold in (("wipes_per_run", WIPES_PER_RUN_DELTA), ("deaths_per_run", DEATHS_PER_RUN_DELTA)):
        rows.append((key, a[key], b[key], _delta(a[key], b[key], 2), _by_threshold(a[key], b[key], threshold, False)))
    da = (a["dead_target_waits"] or {}).get("seconds_per_run")
    db = (b["dead_target_waits"] or {}).get("seconds_per_run")
    rows.append(("dead_target_s_per_run", da, db, _delta(da, db, 1),
                 _by_threshold(da, db, DEAD_TARGET_S_PER_RUN_DELTA, False)))
    ta, tb = a["teleports"], b["teleports"]
    for kind in DECIDED_TELEPORTS:
        ka = ((ta or {}).get("by_kind") or {}).get(kind)
        kb = ((tb or {}).get("by_kind") or {}).get(kind)
        oa, ob = (ka or {}).get("total"), (kb or {}).get("total")
        if oa is None or ob is None:
            v = "n/a"
        elif oa == 0 and ob > 0:
            v = "worse"
        elif oa > 0 and ob == 0:
            v = "better"
        else:
            v = "same"
        rows.append((f"teleports_{kind}", oa, ob, _delta(oa, ob, 0), v))
    ca, cb = a["cc"], b["cc"]
    ra, rb = (ca or {}).get("landed_ratio"), (cb or {}).get("landed_ratio")
    enough = ca and cb and ca["ended"] >= CC_MIN_ENDED and cb["ended"] >= CC_MIN_ENDED
    rows.append(("cc_landed_ratio", ra, rb, _delta(ra, rb),
                 _by_threshold(ra, rb, CC_RATIO_DELTA, True) if enough else "n/a"))
    # reported, no rule
    for key, get in (("teleports_per_run", lambda g: (g["teleports"] or {}).get("per_run")),
                     ("wipe_to_resume_median_s", lambda g: (g["wipe_to_resume_s"] or {}).get("median")),
                     ("backtrack_ratio", lambda g: (g["backtrack_ratio"] or {}).get("ratio")),
                     ("median_min", lambda g: g["duration_min"]["median"]),
                     ("p90_min", lambda g: g["duration_min"]["p90"])):
        rows.append((key, get(a), get(b), _delta(get(a), get(b), 3), None))
    for c in run_metrics.TIME_CATEGORIES:
        oa, ob = (a["time_share"] or {}).get(c), (b["time_share"] or {}).get(c)
        rows.append((f"time_share_{c}", oa, ob, _delta(oa, ob), None))
    return [{"metric": m, "old": o, "new": n, "delta": d, "verdict": v} for m, o, n, d, v in rows]


def _combine(verdicts):
    vs = [v for v in verdicts if v in ("better", "worse", "same")]
    if "worse" in vs:
        return "worse"
    if "better" in vs:
        return "better"
    return "same"


def _context_parts(rows):
    out = {}
    for r in rows:
        for part in summary.CONTEXT_PARTS:
            out.setdefault(part, set()).add((r["context"] or {}).get(part))
    return out


def _differences(rows_a, rows_b, accept):
    """(reported differences, blocking reasons) between two sides' runs of one dungeon."""
    reported, blocking = [], []
    pa, pb = {r["profile"] for r in rows_a}, {r["profile"] for r in rows_b}
    if pa != pb:
        blocking.append(f"validation profile {sorted(map(str, pa))} vs {sorted(map(str, pb))}")
    missing = [side for side, rows in (("A", rows_a), ("B", rows_b)) if any(r["context"] is None for r in rows)]
    if missing:
        reported.append("run context unknown for some runs of " + " and ".join(missing))
    ca, cb = _context_parts([r for r in rows_a if r["context"]]), _context_parts([r for r in rows_b if r["context"]])
    for part in summary.CONTEXT_PARTS:
        va, vb = ca.get(part), cb.get(part)
        if va is None or vb is None or va == vb:
            continue
        text = f"{part} {', '.join(sorted(map(str, va)))} vs {', '.join(sorted(map(str, vb)))}"
        reported.append(text)
        if part in BLOCKING_PARTS and part not in accept:
            blocking.append(text)
    if any((r["context"] or {}).get("incomplete") for r in rows_a + rows_b):
        reported.append("a run context is incomplete (unknown part or dirty module)")
    return reported, blocking


def compare(a, b, accept=()):
    rows_a, rows_b = a["runs"], b["runs"]
    keys = sorted({(r["lfg_id"], r["dungeon"]) for r in rows_a + rows_b}, key=lambda k: (k[0] or 0, k[1] or ""))
    dungeons, compared_a, compared_b = [], [], []
    for lfg, name in keys:
        ra = [r for r in rows_a if r["lfg_id"] == lfg]
        rb = [r for r in rows_b if r["lfg_id"] == lfg]
        entry = {"dungeon": name, "lfg_id": lfg, "runs_a": len(ra), "runs_b": len(rb)}
        if not summary.finished(ra) or not summary.finished(rb):
            lacking = [side for side, rs in (("A", ra), ("B", rb)) if not summary.finished(rs)]
            entry.update(verdict="not comparable", why=["no finished run in " + " and ".join(lacking)],
                         differences=[], metrics=[])
            dungeons.append(entry)
            continue
        reported, blocking = _differences(ra, rb, accept)
        metrics = _rules(summary.aggregate(ra), summary.aggregate(rb))
        by_rules = _combine(m["verdict"] for m in metrics)
        n = min(len(summary.finished(ra)), len(summary.finished(rb)))
        entry.update(differences=reported, metrics=metrics, verdict_by_metrics=by_rules,
                     verdict="not comparable" if blocking else by_rules, why=blocking,
                     better=[m["metric"] for m in metrics if m["verdict"] == "better"],
                     worse=[m["metric"] for m in metrics if m["verdict"] == "worse"],
                     # below this one run moves the FULL_ROUTE rate by more than its threshold
                     low_sample=n < LOW_SAMPLE_RUNS)
        if not blocking:
            compared_a += ra
            compared_b += rb
        dungeons.append(entry)
    judged = [d["verdict"] for d in dungeons if d["verdict"] != "not comparable"]
    if not judged:
        overall = "not comparable"
    elif "worse" in judged:
        overall = "worse"
    elif "better" in judged:
        overall = "better"
    else:
        overall = "same"
    pooled = _rules(summary.aggregate(compared_a), summary.aggregate(compared_b)) if compared_a and compared_b else []
    ids = lambda s: [c["campaign_id"] for c in s.get("campaigns", [])] or s.get("inputs", [])  # noqa: E731
    return {"comparison_version": COMPARISON_VERSION, "a": ids(a), "b": ids(b), "accepted_differences": sorted(accept),
            "thresholds": {"full_route_rate": FULL_ROUTE_RATE_DELTA, "full_route_median_duration_rel": DURATION_REL_DELTA,
                           "wipes_per_run": WIPES_PER_RUN_DELTA, "deaths_per_run": DEATHS_PER_RUN_DELTA,
                           "dead_target_s_per_run": DEAD_TARGET_S_PER_RUN_DELTA, "cc_landed_ratio": CC_RATIO_DELTA,
                           "cc_min_ended_moons": CC_MIN_ENDED, "new_teleport_kinds": list(DECIDED_TELEPORTS),
                           "low_sample_runs": LOW_SAMPLE_RUNS},
            "verdict": overall, "dungeons": dungeons,
            # the comparable dungeons pooled - for orientation; the verdict comes from the dungeons
            "pooled": pooled}


def _cell(v):
    if v is None:
        return "-"
    return f"{v:+g}" if isinstance(v, (int, float)) and not isinstance(v, bool) else str(v)


def comparison_md(c):
    out = [f"# Campaign {', '.join(c['b'])} (B) vs {', '.join(c['a'])} (A): **{c['verdict'].upper()}**", ""]
    if c["accepted_differences"]:
        out += ["Accepted context differences: " + ", ".join(c["accepted_differences"]), ""]
    out += ["| Dungeon | Runs A / B | Verdict | Better | Worse | Why / differences |", "|---|---|---|---|---|---|"]
    for d in c["dungeons"]:
        notes = "; ".join(d["why"] + [x for x in d["differences"] if x not in d["why"]]) or "-"
        verdict = d["verdict"] + (f" (metrics: {d['verdict_by_metrics']})"
                                  if d["verdict"] == "not comparable" and d.get("verdict_by_metrics") else "")
        if d.get("low_sample"):
            verdict += " (low sample)"
        out.append(f"| {d['dungeon']} | {d['runs_a']} / {d['runs_b']} | {verdict} | "
                   f"{', '.join(d.get('better', [])) or '-'} | {', '.join(d.get('worse', [])) or '-'} | {notes} |")
    for d in c["dungeons"] + ([{"dungeon": "Comparable dungeons pooled", "metrics": c["pooled"]}] if c["pooled"] else []):
        if not d["metrics"]:
            continue
        out += ["", f"## {d['dungeon']}", "", "| Metric | A | B | Delta | Verdict |", "|---|---|---|---|---|"]
        out += [f"| {m['metric']} | {'-' if m['old'] is None else m['old']} | {'-' if m['new'] is None else m['new']} | "
                f"{_cell(m['delta'])} | {m['verdict'] or ''} |" for m in d["metrics"]]
    return "\n".join(out) + "\n"
