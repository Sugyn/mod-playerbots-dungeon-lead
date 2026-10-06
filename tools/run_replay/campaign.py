"""Campaign artifacts: one validation campaign's raw telemetry -> the panel-ready layout.

validation_runs/campaigns/<campaign_id>/
    campaign.json                      the campaign's own records (manifest, results, finish)
    index.json                         one entry per run (result, health, metrics, artifact paths)
    runs/<run_id>/raw/events.jsonl     the run's v2 events, copied verbatim (evidence)
    runs/<run_id>/derived/             run_model.json, metrics.json, failures.json, verdict.json
    runs/<run_id>/report/              timeline.md, map.svg, report.html

Derived and report files can be deleted and regenerated; raw files are never rewritten.
"""
import html
import json
import os

from . import analyze_run, model, report, schema

INDEX_VERSION = 1


def _write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def _json(obj):
    return json.dumps(obj, indent=1, sort_keys=True, ensure_ascii=False) + "\n"


def report_html(m, verdict, failures, svg, timeline):
    md = m["metadata"]
    d = md.get("dungeon") or {}
    rows = "".join(
        f"<tr><td>{html.escape(f['classification'])}</td><td>{html.escape(f['title'])}</td>"
        f"<td>{html.escape(f['probable_cause'] or 'insufficient evidence')}</td>"
        f"<td>{html.escape(f['confidence'] or '-')}</td><td>{html.escape(', '.join(f['evidence']))}</td>"
        f"<td><ul>{''.join('<li>' + html.escape(x) + '</li>' for x in f['facts'])}</ul></td></tr>"
        for f in failures["findings"])
    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>Run {html.escape(md['run_id'])}</title>"
        "<style>body{font-family:sans-serif;margin:16px;max-width:1100px}td,th{border:1px solid #ccc;padding:4px;"
        "vertical-align:top}table{border-collapse:collapse}pre{white-space:pre-wrap}</style></head><body>"
        f"<h1>{html.escape(d.get('name', '?'))} - run {html.escape(md['run_id'])}</h1>"
        f"<p><b>{html.escape(verdict['result'])}</b> ({html.escape(verdict['result_reason'])}), health "
        f"<b>{html.escape(verdict['health'])}</b> {html.escape(', '.join(verdict['unhealthy'] + verdict['warnings']))}"
        f"<br>build {html.escape(str(md['build'].get('commit_sha')))}, campaign "
        f"{html.escape(md.get('campaign_id') or '-')}</p>"
        f"{svg}"
        "<h2>Findings</h2><table><tr><th>Class</th><th>Finding</th><th>Probable cause</th><th>Confidence</th>"
        f"<th>Evidence</th><th>Facts</th></tr>{rows}</table>"
        f"<h2>Timeline and details</h2><pre>{html.escape(timeline)}</pre></body></html>\n")


def build(events_path, campaigns_path, campaign_id, out_root):
    records = [r for r in schema.load_campaigns(campaigns_path) if r.get("campaign_id") == campaign_id]
    if not records:
        raise SystemExit(f"campaign {campaign_id}: no records in {campaigns_path}")
    base = os.path.join(out_root, "campaigns", campaign_id)
    _write(os.path.join(base, "campaign.json"), _json(records))

    # raw lines of this campaign's runs, verbatim
    raw = {}
    with open(events_path, encoding="utf-8") as f:
        for line in f:
            if f'"campaign_id":"{campaign_id}"' not in line:
                continue
            ev = json.loads(line)
            raw.setdefault(ev["run_id"], []).append(line if line.endswith("\n") else line + "\n")

    started = next((r for r in records if r.get("record") == "campaign_started"), {})
    entries = []
    for run_id, lines in raw.items():
        rdir = os.path.join(base, "runs", run_id)
        raw_path = os.path.join(rdir, "raw", "events.jsonl")
        new = "".join(lines)
        old = open(raw_path, encoding="utf-8").read() if os.path.exists(raw_path) else ""
        if new != old:
            # the server file only grows: a later copy may extend the raw record, never change it
            if not new.startswith(old):
                raise SystemExit(f"run {run_id}: raw record changed, refusing to overwrite {raw_path}")
            _write(raw_path, new)
        evs, dropped = schema.load_events(raw_path, run_id)
        m = model.reconstruct(evs, dropped)
        from .cli import campaign_result  # same matching rule as the CLI
        cres = campaign_result(m, records)
        v = analyze_run.verdict(m, cres)
        fl = analyze_run.failures(m, cres)
        timeline = report.timeline_md(m)
        svg = report.map_svg(m)
        _write(os.path.join(rdir, "derived", "run_model.json"), _json(m))
        _write(os.path.join(rdir, "derived", "metrics.json"), _json(m["metrics"]))
        _write(os.path.join(rdir, "derived", "failures.json"), _json(fl))
        _write(os.path.join(rdir, "derived", "verdict.json"), _json(v))
        _write(os.path.join(rdir, "report", "timeline.md"), timeline)
        _write(os.path.join(rdir, "report", "map.svg"), svg)
        _write(os.path.join(rdir, "report", "report.html"), report_html(m, v, fl, svg, timeline))
        d = m["metadata"].get("dungeon") or {}
        entries.append({
            "run_id": run_id, "dungeon": d.get("name"), "lfg_id": d.get("lfg_id"),
            "scenario_id": m["metadata"].get("scenario_id"), "leader": m["metadata"].get("leader"),
            "started_at": m["metadata"].get("started_at"), "duration_ms": m["metadata"]["duration_ms"],
            "result": v["result"], "health": v["health"], "metrics": m["metrics"],
            "findings": [f["classification"] for f in fl["findings"]],
            "artifact": f"runs/{run_id}/derived/run_model.json",
            "report": f"runs/{run_id}/report/report.html",
        })
    entries.sort(key=lambda e: (e["started_at"] or "", e["run_id"]))
    index = {"index_version": INDEX_VERSION, "campaign_id": campaign_id,
             "build": started.get("commit_sha"), "module_version": started.get("module_version"),
             "validation_profile": started.get("validation_profile"), "parallelism": started.get("parallelism"),
             "timeout_minutes": started.get("timeout_minutes"), "dungeons": started.get("dungeons"),
             "finished": any(r.get("record") == "campaign_finished" for r in records), "runs": entries}
    _write(os.path.join(base, "index.json"), _json(index))
    return index
