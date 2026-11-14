"""Campaign summary: what a set of runs did, per dungeon and run context.

  python3 -m tools.run_replay.cli summary INPUT... [-d DIR]   -> summary.json, summary.md

An INPUT is a campaign directory (validation_runs/campaigns/<id>, from the campaign command), one
run's artifact directory (.../runs/<run_id>), or a validation_runs directory (every campaign in
it). A run reached through several inputs counts once. A run is rebuilt from its raw events when
they are there, so artifacts written by an older version of these tools get today's metrics;
without raw events its derived/run_model.json is read, and metrics it does not have stay None.

"Per run" figures are over finished runs (result FULL_ROUTE, PARTIAL or FAILED): an UNKNOWN run
(record cut or started mid-run) is counted in `runs` / `unknown` and left out of everything else.
"""
import glob
import json
import math
import os
import statistics

from . import analyze_run, model, run_metrics, schema

SUMMARY_VERSION = 1

CONTEXT_PARTS = ("module_sha", "core_sha", "playerbots_sha", "route_digest", "config_digest", "mmap_digest")


# -- loading -------------------------------------------------------------------------------------

def _read_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _campaign_info(cdir):
    """Campaign records and index of a campaign directory ({} / [] when absent)."""
    records = _read_json(os.path.join(cdir, "campaign.json")) if os.path.exists(os.path.join(cdir, "campaign.json")) \
        else []
    index = _read_json(os.path.join(cdir, "index.json")) if os.path.exists(os.path.join(cdir, "index.json")) else {}
    return records, index


def _profile(scenario_id):
    # "lfg8-lvl20-bot5-wtank-..." -> "bot5-wtank-..."
    parts = (scenario_id or "").split("-", 2)
    return parts[2] if len(parts) == 3 and parts[0].startswith("lfg") and parts[1].startswith("lvl") else None


def _row(m, v, campaign_id, profile, source):
    md, mt = m["metadata"], m["metrics"]
    d = md.get("dungeon") or {}
    ctx = md.get("context")
    return {
        "run_id": md["run_id"], "campaign_id": campaign_id or md.get("campaign_id") or None,
        "dungeon": d.get("name"), "lfg_id": d.get("lfg_id"),
        "module_sha": (md.get("build") or {}).get("commit_sha"),
        "context_id": (ctx or {}).get("context_id"),
        "context": {k: ctx.get(k) for k in CONTEXT_PARTS + ("incomplete",)} if ctx else None,
        "profile": profile or _profile(md.get("scenario_id")),
        "started_at": md.get("started_at"), "complete_record": md.get("complete_record"),
        "result": v["result"], "health": v["health"], "warnings": v.get("warnings", []),
        "duration_ms": md["duration_ms"], "wipes": mt["wipes"], "deaths": mt["deaths"],
        # metrics v2: None where the run has no evidence or the artifact predates them
        "dead_target_waits": mt.get("dead_target_waits"), "teleports": mt.get("teleports"), "cc": mt.get("cc"),
        "wipe_to_resume_s": mt.get("wipe_to_resume_s"), "time_split": mt.get("time_split"),
        "backtrack_ratio": mt.get("backtrack_ratio"),
        "run_back": mt.get("run_back"),
        "evidence": source,
    }


def load_run(run_dir, records=(), campaign_id=None, profile=None):
    """One run's summary row, from raw events if present, else from its derived artifacts."""
    from .cli import campaign_result  # same matching rule as the CLI
    raw = os.path.join(run_dir, "raw", "events.jsonl")
    derived = os.path.join(run_dir, "derived", "run_model.json")
    if os.path.exists(raw):
        evs, dropped = schema.load_events(raw)
        if not evs:
            return None
        m, source = model.reconstruct(evs, dropped), "raw"
        v = analyze_run.verdict(m, campaign_result(m, records))
    elif os.path.exists(derived):
        m = _read_json(derived)
        if m.get("model_version") == model.MODEL_VERSION:
            source = "run_model"
            v = analyze_run.verdict(m, campaign_result(m, records))
        else:
            # an older model: its own verdict, and no v2 metrics
            source = f"run_model_v{m.get('model_version')}"
            vpath = os.path.join(run_dir, "derived", "verdict.json")
            v = _read_json(vpath) if os.path.exists(vpath) else {"result": "UNKNOWN", "health": None, "warnings": []}
    else:
        return None
    return _row(m, v, campaign_id, profile, source)


def _campaign_dirs(path):
    if os.path.exists(os.path.join(path, "index.json")) or os.path.isdir(os.path.join(path, "runs")):
        return [path]
    if os.path.isdir(os.path.join(path, "campaigns")):
        return sorted(p for p in glob.glob(os.path.join(path, "campaigns", "*")) if os.path.isdir(p))
    return []


def load(inputs):
    """(campaigns, rows) for the inputs; raises SystemExit for an input that is none of the kinds."""
    campaigns, rows, seen = [], [], set()
    for path in inputs:
        path = os.path.normpath(path)
        cdirs = _campaign_dirs(path)
        if cdirs:
            targets = []
            for cdir in cdirs:
                records, index = _campaign_info(cdir)
                started = next((r for r in records if r.get("record") == "campaign_started"), {})
                cid = index.get("campaign_id") or started.get("campaign_id") or os.path.basename(cdir)
                profile = index.get("validation_profile") or started.get("validation_profile")
                campaigns.append({"campaign_id": cid, "build": index.get("build") or started.get("commit_sha"),
                                  "module_version": index.get("module_version") or started.get("module_version"),
                                  "validation_profile": profile,
                                  "finished": any(r.get("record") == "campaign_finished" for r in records)
                                  if records else index.get("finished")})
                for rdir in sorted(glob.glob(os.path.join(cdir, "runs", "*"))):
                    targets.append((rdir, records, cid, profile))
        elif os.path.isdir(os.path.join(path, "raw")) or os.path.isdir(os.path.join(path, "derived")):
            cdir = os.path.dirname(os.path.dirname(path))  # .../campaigns/<id>/runs/<run_id>
            records, index = _campaign_info(cdir)
            targets = [(path, records, index.get("campaign_id"), index.get("validation_profile"))]
        else:
            raise SystemExit(f"{path}: not a campaign directory, a run directory or a validation_runs directory")
        for rdir, records, cid, profile in targets:
            row = load_run(rdir, records, cid, profile)
            if row is None or row["run_id"] in seen:
                continue
            seen.add(row["run_id"])
            rows.append(row)
    rows.sort(key=lambda r: (r["started_at"] or "", r["run_id"]))
    return campaigns, rows


# -- aggregation ---------------------------------------------------------------------------------

def _p90(values):
    """Nearest-rank 90th percentile."""
    s = sorted(values)
    return s[max(0, math.ceil(0.9 * len(s)) - 1)] if s else None


def _mean(values, digits=2):
    return round(sum(values) / len(values), digits) if values else None


def _minutes(ms_values, f):
    return round(f(ms_values) / 60000, 1) if ms_values else None


def finished(rows):
    return [r for r in rows if r["result"] != "UNKNOWN"]


def aggregate(rows):
    """Figures of a set of runs (one dungeon and context, a whole campaign, ...)."""
    done = finished(rows)
    full = [r for r in done if r["result"] == "FULL_ROUTE"]
    dur = [r["duration_ms"] for r in done]
    full_dur = [r["duration_ms"] for r in full]
    out = {
        "runs": len(rows), "finished": len(done), "unknown": len(rows) - len(done),
        "full_route": len(full),
        "full_route_rate": round(len(full) / len(done), 3) if done else None,
        "duration_min": {"median": _minutes(dur, statistics.median), "p90": _minutes(dur, _p90)},
        "full_route_duration_min": {"median": _minutes(full_dur, statistics.median),
                                    "p90": _minutes(full_dur, _p90)},
        "wipes_per_run": _mean([r["wipes"] for r in done]),
        "deaths_per_run": _mean([r["deaths"] for r in done]),
    }

    dw = [r["dead_target_waits"] for r in done if r["dead_target_waits"] is not None]
    out["dead_target_waits"] = {
        "runs_with_evidence": len(dw),
        "per_run": _mean([x["count"] for x in dw]),
        "seconds_per_run": _mean([x["total_s"] for x in dw], 1),
        "max_s": max((x["max_s"] for x in dw), default=None),
    } if dw else None

    tp = [r["teleports"] for r in done if r["teleports"] is not None]
    if tp:
        by_kind = {}
        for k in run_metrics.TELEPORT_KINDS:
            vals = [x[k] for x in tp if x[k] is not None]
            by_kind[k] = {"total": sum(vals), "runs_with": sum(1 for v in vals if v)} if vals else None
        totals = [x["total"] for x in tp if x["total"] is not None]
        members = [x["member"] for x in tp if x.get("member") is not None]
        reasons = {}
        for x in tp:
            for r, n in (x.get("fallback_reasons") or {}).items():
                reasons[r] = reasons.get(r, 0) + n
        out["teleports"] = {"runs_with_evidence": len(tp), "per_run": _mean(totals),
                            "leader_per_run": _mean([x["leader"] for x in tp]), "by_kind": by_kind,
                            "member_per_run": _mean(members),
                            "member_median": statistics.median(members) if members else None,
                            "fallback_reasons": reasons}
    else:
        out["teleports"] = None

    rb = [r["run_back"] for r in done if r.get("run_back") is not None]
    if rb:
        out["run_back"] = {"runs_with_evidence": len(rb)}
        for k in ("held", "resurrected", "released", "walked_to_portal", "walk_orders", "rejoined_walking", "fallbacks"):
            out["run_back"][k] = sum(x[k] for x in rb)
        backs = [x["back_ms_max"] for x in rb if x.get("back_ms_max") is not None]
        out["run_back"]["back_s_max"] = round(max(backs) / 1000.0, 1) if backs else None
    else:
        out["run_back"] = None

    cc = [r["cc"] for r in done if r["cc"] is not None]
    if cc:
        landed, moons = sum(x["landed"] for x in cc), sum(x["moons"] for x in cc)
        out["cc"] = {"runs_with_evidence": len(cc), "moons": moons, "landed": landed,
                     "released_last_enemy": sum(x["released_last_enemy"] for x in cc),
                     # landed / moons placed, as per run
                     "landed_ratio": round(landed / moons, 3) if moons else None}
    else:
        out["cc"] = None

    wr = [r["wipe_to_resume_s"] for r in done if r["wipe_to_resume_s"] is not None]
    if wr:
        secs = [s for x in wr for s in x["wipes"]]
        known = [s for s in secs if s is not None]
        out["wipe_to_resume_s"] = {"wipes": len(secs), "unresolved": len(secs) - len(known),
                                   "median": round(statistics.median(known), 1) if known else None,
                                   "max": max(known) if known else None}
    else:
        out["wipe_to_resume_s"] = None

    ts = [r["time_split"] for r in done if r["time_split"] is not None]
    total = sum(x["total_s"] for x in ts)
    if ts and total:
        share = lambda v: round(v / total, 3)  # noqa: E731
        out["time_share"] = {c: share(sum(x[f"{c}_s"] for x in ts)) for c in run_metrics.TIME_CATEGORIES}
        out["waiting_share"] = {w: share(sum(x["waiting_by_reason_s"].get(w, 0) for x in ts))
                                for w in run_metrics.WAIT_REASONS}
        out["time_runs_with_evidence"] = len(ts)
    else:
        out["time_share"] = out["waiting_share"] = None
        out["time_runs_with_evidence"] = 0

    bt = [r["backtrack_ratio"] for r in done if r["backtrack_ratio"] is not None]
    walked = sum(x["walked_yd"] for x in bt)
    out["backtrack_ratio"] = {"runs_with_evidence": len(bt), "walked_yd": round(walked, 1),
                              "ratio": round(sum(x["away_yd"] for x in bt) / walked, 3)} if walked else None
    return out


def dungeon_key(r):
    """A dungeon is its LFG id and name together: runs without an LFG id stay apart by name."""
    return (r["lfg_id"] or 0, r["dungeon"] or "?")


def _group_key(r):
    return dungeon_key(r) + (r["context_id"] or "",)


def build(inputs):
    campaigns, rows = load(inputs)
    groups = {}
    for r in rows:
        groups.setdefault(_group_key(r), []).append(r)
    out_groups = []
    for (lfg, name, ctx_id), rs in sorted(groups.items()):
        ctx = next((r["context"] for r in rs if r["context"]), None)
        g = {"dungeon": name, "lfg_id": lfg or None, "context_id": ctx_id or None, "context": ctx,
             "modules": sorted({r["module_sha"] or "?" for r in rs}),
             "profiles": sorted({r["profile"] or "?" for r in rs}),
             "campaigns": sorted({r["campaign_id"] or "-" for r in rs})}
        g.update(aggregate(rs))
        out_groups.append(g)
    return {"summary_version": SUMMARY_VERSION, "metrics_version": run_metrics.METRICS_VERSION,
            "inputs": [os.path.normpath(p) for p in inputs], "campaigns": campaigns,
            "overall": aggregate(rows), "groups": out_groups, "runs": rows}


# -- presentation --------------------------------------------------------------------------------

def _f(v, fmt="{}"):
    return "-" if v is None else fmt.format(v)


def _pct(v):
    return "-" if v is None else f"{v:.0%}"


def _line(g):
    dw, tp, cc, wr, ts, bt = (g["dead_target_waits"], g["teleports"], g["cc"], g["wipe_to_resume_s"],
                              g["time_share"], g["backtrack_ratio"])
    time = "-" if not ts else (f"walk {_pct(ts['walking'])} / fight {_pct(ts['combat'])} / "
                               f"wait {_pct(ts['waiting'])} / recovery {_pct(ts['recovery'])}")
    return (f"| {g['runs']} ({g['unknown']} unknown) | {g['full_route']} | "
            f"{_f(g['duration_min']['median'])} / {_f(g['duration_min']['p90'])} | "
            f"{_f(g['wipes_per_run'])} | {_f(g['deaths_per_run'])} | "
            f"{'-' if not dw else _f(dw['per_run']) + ' (' + _f(dw['seconds_per_run']) + ' s)'} | "
            f"{'-' if not tp else _f(tp['per_run'])} | "
            f"{'-' if not tp or tp.get('member_per_run') is None else _f(tp['member_per_run']) + ' (median ' + _f(tp['member_median']) + ')'} | "
            f"{'-' if not cc else _f(cc['landed_ratio']) + ' of ' + str(cc['moons'])} | "
            f"{'-' if not wr else _f(wr['median'])} | {time} | {'-' if not bt else _pct(bt['ratio'])} |")


HEADER = ("| Runs | Full route | Minutes median / p90 | Wipes / run | Deaths / run | Dead-target waits / run "
          "| Teleports / run | Member teleports / run | CC landed | Wipe to resume (s) | Time | Backtrack |")


def summary_md(s):
    out = ["# Campaign summary", "",
           "Inputs: " + ", ".join(c["campaign_id"] for c in s["campaigns"]) if s["campaigns"] else "Inputs: run directories",
           "", "Per run = per finished run (UNKNOWN runs left out). Minutes over finished runs.", "",
           "| Dungeon | Context | " + HEADER[2:], "|---|---|" + "---|" * 12]
    for g in s["groups"]:
        ctx = (g["context_id"] or "unknown") + (" (incomplete)" if (g["context"] or {}).get("incomplete") else "")
        out.append(f"| {g['dungeon']} | {ctx} " + _line(g))
    out.append("| **All** | | " + _line(s["overall"])[2:])
    out += ["", "## Waiting, by reason (share of all run time)", "",
            "| Dungeon | Context | " + " | ".join(run_metrics.WAIT_REASONS) + " |",
            "|---|---|" + "---|" * len(run_metrics.WAIT_REASONS)]
    for g in s["groups"] + [dict(s["overall"], dungeon="**All**", context_id="")]:
        ws = g["waiting_share"]
        cells = " | ".join(_pct(ws[w]) if ws else "-" for w in run_metrics.WAIT_REASONS)
        out.append(f"| {g['dungeon']} | {g['context_id'] or ''} | {cells} |")
    groups = [g for g in s["groups"] + [dict(s["overall"], dungeon="**All**", context_id="")] if g.get("run_back")]
    if groups:
        out += ["", "## Run back (dead and straggling party bots)", "",
                "| Dungeon | Context | Held for a resurrection | Resurrected in place | Released | Walked to the portal "
                "| Walk orders | Rejoined walking | Run back fallbacks | Member teleport reasons (all) | Longest death to rejoin (s) |",
                "|---|---|---|---|---|---|---|---|---|---|---|"]
        for g in groups:
            rb, tp = g["run_back"], g.get("teleports") or {}
            reasons = ", ".join(f"{k} {v}" for k, v in sorted((tp.get("fallback_reasons") or {}).items())) or "-"
            out.append(f"| {g['dungeon']} | {g['context_id'] or ''} | {rb['held']} | {rb['resurrected']} | {rb['released']} | "
                       f"{rb['walked_to_portal']} | {rb['walk_orders']} | {rb['rejoined_walking']} | {rb['fallbacks']} | {reasons} | "
                       f"{'-' if rb.get('back_s_max') is None else rb['back_s_max']} |")
    return "\n".join(out) + "\n"
