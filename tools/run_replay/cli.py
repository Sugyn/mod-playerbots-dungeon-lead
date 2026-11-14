"""Run replay tools.

  python3 -m tools.run_replay.cli runs EVENTS.jsonl
      list the runs in a v2 events file
  python3 -m tools.run_replay.cli reconstruct EVENTS.jsonl RUN_ID [-o run_model.json]
      the canonical run model of one run (stdout without -o)
  python3 -m tools.run_replay.cli report run_model.json [-d DIR]
      timeline.md and map.svg from a run model
  python3 -m tools.run_replay.cli analyze run_model.json [-d DIR] [--campaigns CAMPAIGNS.jsonl]
      metrics.json, failures.json, verdict.json
  python3 -m tools.run_replay.cli campaign EVENTS.jsonl CAMPAIGNS.jsonl CAMPAIGN_ID [-o validation_runs]
      every run of a validation campaign: raw copy, derived artifacts, reports, index.json
  python3 -m tools.run_replay.cli compare OLD_run_model.json NEW_run_model.json [-d DIR]
      comparison.json and comparison.md
  python3 -m tools.run_replay.cli summary INPUT... [-d DIR]
      summary.json and summary.md per dungeon and run context (INPUT: campaign, run or
      validation_runs directory)
  python3 -m tools.run_replay.cli compare-campaigns A B [-d DIR] [--accept PART]...
      campaign_comparison.json and .md: B better / worse / same as A (A, B: summary inputs or
      summary.json)
"""
import argparse
import json
import os
import sys

from . import analyze_run, model, report, schema


def _dump(obj, out):
    text = json.dumps(obj, indent=1, sort_keys=True, ensure_ascii=False) + "\n"
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


def campaign_result(m, records):
    """The campaign's run_result record for this run: same campaign, dungeon and tank."""
    md = m["metadata"]
    lfg = (md.get("dungeon") or {}).get("lfg_id")
    for r in records:
        if (r.get("record") == "run_result" and r.get("campaign_id") == md.get("campaign_id")
                and r.get("lfg_id") == lfg and r.get("tank") == md.get("leader")):
            return r
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(prog="run_replay")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("runs")
    r.add_argument("events")
    c = sub.add_parser("reconstruct")
    c.add_argument("events")
    c.add_argument("run_id")
    c.add_argument("-o", "--out")
    p = sub.add_parser("report")
    p.add_argument("run_model")
    p.add_argument("-d", "--dir", default=".")
    z = sub.add_parser("analyze")
    z.add_argument("run_model")
    z.add_argument("-d", "--dir", default=".")
    z.add_argument("--campaigns")
    k = sub.add_parser("campaign")
    k.add_argument("events")
    k.add_argument("campaigns")
    k.add_argument("campaign_id")
    k.add_argument("-o", "--out", default="validation_runs")
    q = sub.add_parser("compare")
    q.add_argument("old")
    q.add_argument("new")
    q.add_argument("-d", "--dir", default=".")
    s = sub.add_parser("summary")
    s.add_argument("inputs", nargs="+")
    s.add_argument("-d", "--dir", default=".")
    cc = sub.add_parser("compare-campaigns")
    cc.add_argument("a")
    cc.add_argument("b")
    cc.add_argument("-d", "--dir", default=".")
    cc.add_argument("--accept", action="append", default=[],
                    choices=("core_sha", "playerbots_sha", "route_digest", "config_digest", "mmap_digest"),
                    help="a run-context part whose difference is intended (repeatable)")
    a = ap.parse_args(argv)

    try:
        if a.cmd == "runs":
            for rid in schema.run_ids(a.events):
                evs, _ = schema.load_events(a.events, rid)
                first, last = evs[0], evs[-1]
                print(f"{rid}  {last['dungeon'].get('name') or '?':32} {first['leader']['name']:12} "
                      f"{last['run_ms'] // 60000:3d} min  {last['outcome']['outcome']:9} "
                      f"{first.get('campaign_id') or '-'}")
        elif a.cmd == "reconstruct":
            evs, dropped = schema.load_events(a.events, a.run_id)
            if not evs:
                sys.exit(f"run {a.run_id}: no events in {a.events}")
            _dump(model.reconstruct(evs, dropped), a.out)
        elif a.cmd == "report":
            with open(a.run_model, encoding="utf-8") as f:
                m = json.load(f)
            if m.get("model_version") != model.MODEL_VERSION:
                sys.exit(f"error: model_version {m.get('model_version')!r}, this tool reads {model.MODEL_VERSION}")
            os.makedirs(a.dir, exist_ok=True)
            with open(os.path.join(a.dir, "timeline.md"), "w", encoding="utf-8") as f:
                f.write(report.timeline_md(m))
            with open(os.path.join(a.dir, "map.svg"), "w", encoding="utf-8") as f:
                f.write(report.map_svg(m))
        elif a.cmd == "analyze":
            with open(a.run_model, encoding="utf-8") as f:
                m = json.load(f)
            if m.get("model_version") != model.MODEL_VERSION:
                sys.exit(f"error: model_version {m.get('model_version')!r}, this tool reads {model.MODEL_VERSION}")
            cres = campaign_result(m, schema.load_campaigns(a.campaigns)) if a.campaigns else None
            os.makedirs(a.dir, exist_ok=True)
            _dump(m["metrics"], os.path.join(a.dir, "metrics.json"))
            _dump(analyze_run.failures(m, cres), os.path.join(a.dir, "failures.json"))
            _dump(analyze_run.verdict(m, cres), os.path.join(a.dir, "verdict.json"))
        elif a.cmd == "campaign":
            from . import campaign
            idx = campaign.build(a.events, a.campaigns, a.campaign_id, a.out)
            for r in idx["runs"]:
                print(f"{r['run_id']}  {r['dungeon'] or '?':32} {r['result']:10} {r['health']:9} "
                      f"wipes={r['metrics']['wipes']} fights={r['metrics']['fights']}")
        elif a.cmd == "compare":
            from . import compare_runs
            models = []
            for path in (a.old, a.new):
                with open(path, encoding="utf-8") as f:
                    models.append(json.load(f))
            c = compare_runs.compare(*models, analyze_run.verdict(models[0]), analyze_run.verdict(models[1]))
            os.makedirs(a.dir, exist_ok=True)
            _dump(c, os.path.join(a.dir, "comparison.json"))
            with open(os.path.join(a.dir, "comparison.md"), "w", encoding="utf-8") as f:
                f.write(compare_runs.comparison_md(c))
        elif a.cmd == "summary":
            from . import summary
            sm = summary.build(a.inputs)
            os.makedirs(a.dir, exist_ok=True)
            _dump(sm, os.path.join(a.dir, "summary.json"))
            with open(os.path.join(a.dir, "summary.md"), "w", encoding="utf-8") as f:
                f.write(summary.summary_md(sm))
            for g in sm["groups"]:
                print(f"{g['dungeon'] or '?':32} {g['context_id'] or '-':16} runs={g['runs']} "
                      f"full_route={g['full_route']} median_min={g['duration_min']['median']}")
        elif a.cmd == "compare-campaigns":
            from . import compare_campaigns
            c = compare_campaigns.compare(compare_campaigns.load_side(a.a), compare_campaigns.load_side(a.b),
                                          a.accept)
            os.makedirs(a.dir, exist_ok=True)
            _dump(c, os.path.join(a.dir, "campaign_comparison.json"))
            with open(os.path.join(a.dir, "campaign_comparison.md"), "w", encoding="utf-8") as f:
                f.write(compare_campaigns.comparison_md(c))
            for d in c["dungeons"]:
                notes = d["why"] + [f"{k}: {', '.join(d[k])}" for k in ("worse", "better") if d.get(k)]
                if d.get("low_sample"):
                    notes.append("low sample")
                print(f"{d['dungeon'] or '?':32} {d['verdict']:15} {'; '.join(notes)}")
            notes = (["low sample"] if c["low_sample"] else []) + \
                ([f"not comparable: {', '.join(c['not_comparable'])}"] if c["not_comparable"] else [])
            print(f"{'overall':32} {c['verdict']:15} {'; '.join(notes)}")
    except schema.SchemaError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
