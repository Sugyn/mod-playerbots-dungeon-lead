"""Human-readable report from a run model: timeline.md and map.svg.

Presentation only: everything shown comes from run_model.json (and the planned route from
data/dungeon_routes.csv); nothing is inferred here.
"""
import csv
import html
import os

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def planned_route(lfg_id, routes_csv=None):
    """Route steps of an LFG dungeon with a position, in step order."""
    path = routes_csv or os.path.join(_ROOT, "data", "dungeon_routes.csv")
    out = []
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["lfg_id"] != str(lfg_id) or not row["x"]:
                continue
            out.append({"step": int(row["step"]), "kind": row["kind"], "name": row["boss"],
                        "x": float(row["x"]), "y": float(row["y"]), "z": float(row["z"])})
    return sorted(out, key=lambda s: s["step"])


def _clock(ms):
    if ms is None:
        return "--:--"
    s = ms // 1000
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60:02d}:{s % 60:02d}"


def _xyz(p):
    if not p or p.get("x") is None:
        return "-"
    return f"({p['x']:.1f}, {p['y']:.1f}, {p['z']:.1f})"


# -- timeline.md ----------------------------------------------------------------------------------

def timeline_md(m):
    md, res, mt = m["metadata"], m["result"], m["metrics"]
    d = md.get("dungeon") or {}
    party = ", ".join("%s (%s)" % (p["name"], p.get("role")) for p in m["party"]["members"]) or "-"
    out = [f"# Run {md['run_id']} - {d.get('name', '?')}", ""]
    out += [
        "| | |", "|---|---|",
        f"| Leader | {md['leader']} |",
        f"| Party | {party} |",
        f"| Build | {md['build'].get('commit_sha')} ({md['build'].get('module_version')}) |",
        f"| Campaign | {md.get('campaign_id') or '-'} |",
        f"| Scenario | {md.get('scenario_id') or '-'} |",
        f"| Started | {md.get('started_at')} |",
        f"| Duration | {_clock(md['duration_ms'])} |",
        f"| Outcome | {res['outcome']} ({res['failure_domain']} / {res['failure_reason']}) |",
        f"| Ended by | {res['terminal_event'] or '-'} {res['terminal_detail'] or ''} |",
        "",
    ]
    if not md.get("complete_record"):
        out += ["> The record of this run starts mid-run (first event_seq "
                f"{md['first_event_seq']}); sections below cover only what was recorded.", ""]
    out += ["## Metrics", "", "| Metric | Value |", "|---|---|"]
    out += [f"| {k} | {v if v is not None else 'unknown'} |" for k, v in sorted(mt.items())]
    out += ["", "## Timeline", ""]
    for t in m["timeline"]:
        ref = f" [{t['ref']}]" if t.get("ref") else ""
        out.append(f"- `{_clock(t['run_ms'])}` {t['text']}{ref}")
    out += ["", "## Fights", ""]
    if m["pulls"]:
        out += ["| # | Start | Objective | Expected | Engaged | Adds | Deaths | Duration | Result |",
                "|---|---|---|---|---|---|---|---|---|"]
        for p in m["pulls"]:
            exp = p["expected_count"] if p["expected_count"] is not None else "?"
            adds = ", ".join(a["name"] for a in p["unexpected_adds"]) or "-"
            what = p["objective"] or (f"(on the way to {p['on_the_way_to']})" if p["on_the_way_to"] else "-")
            out.append(f"| F{p['fight_id']} | {_clock(p['start_ms'])} | {what} | {exp} | "
                       f"{p['engaged_count']} | {adds} | {', '.join(x['name'] for x in p['deaths']) or '-'} | "
                       f"{_clock(p['duration_ms'])} | {p['result']} |")
    else:
        out.append("No fight evidence in this record.")
    out += ["", "## Bosses", ""]
    for b in m["bosses"]:
        out.append(f"- {b['name']}: attempt {b['attempt']} - {b['result']}"
                   + (f" in {b['duration_ms'] // 1000} s" if b.get("duration_ms") is not None else ""))
    if not m["bosses"]:
        out.append("-")
    out += ["", "## Wipes", ""]
    for w in m["wipes"]:
        cp = (w.get("checkpoint") or {}).get("name")
        out.append(f"- W{w['wipe']} at {_clock(w['run_ms'])} {_xyz(w['position'])}: first death "
                   f"{w['first_death'] or 'unknown'}; deaths {', '.join(d['name'] for d in w['deaths_before']) or '-'}; "
                   f"recovered {_clock(w['recovered_ms'])}; checkpoint {cp or '-'}")
    if not m["wipes"]:
        out.append("-")
    out += ["", "## Recoveries", ""]
    for i, r in enumerate(m["recoveries"], 1):
        out.append(f"- R{i} {_clock(r['start_ms'])} {r['reason']} ({r['member'] or '-'}): {r['result']}"
                   + (f" after {r['duration_ms'] // 1000} s" if r.get("duration_ms") is not None else "")
                   + (f", {len(r['escalations'])} escalation events" if r["escalations"] else ""))
    if not m["recoveries"]:
        out.append("-")
    out += ["", "## Interactions", ""]
    for i, it in enumerate(m["interactions"], 1):
        out.append(f"- I{i} {_clock(it['start_ms'])} {it['type']} {it.get('target') or ''}: {it['result']}")
    if not m["interactions"]:
        out.append("-")
    out += ["", "## Alerts", ""]
    for a in m["alerts"]:
        out.append(f"- `{_clock(a['run_ms'])}` **{a['kind']}** {a['text']}")
    if not m["alerts"]:
        out.append("-")
    return "\n".join(out) + "\n"


# -- map.svg --------------------------------------------------------------------------------------

def map_svg(m, planned=None, width=1000, height=800):
    """XY map, north up: screen x = -world y, screen y = -world x (WoW: +x north, +y west)."""
    planned = planned if planned is not None else planned_route((m["metadata"].get("dungeon") or {}).get("lfg_id"))
    pts = [(p["x"], p["y"]) for p in m["path"] if p["x"] is not None]
    pts += [(s["x"], s["y"]) for s in planned]
    for p in m["pulls"]:
        a = p["combat_anchor"] or p["start_position"]
        if a and a.get("x") is not None:
            pts.append((a["x"], a["y"]))
    if not pts:
        pts = [(0.0, 0.0)]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    pad = 60
    span = max(max(xs) - min(xs), max(ys) - min(ys), 20.0)
    scale = min((width - 2 * pad) / span, (height - 2 * pad - 60) / span)

    def sx(x, y):
        return pad + (max(ys) - y) * scale

    def sy(x, y):
        return pad + 60 + (max(xs) - x) * scale

    e = []
    md = m["metadata"]
    d = md.get("dungeon") or {}
    e.append(f'<text x="{pad}" y="28" font-size="18" font-weight="bold">{html.escape(d.get("name", "?"))} - run '
             f'{html.escape(md["run_id"])}</text>')
    e.append(f'<text x="{pad}" y="48" font-size="12">{html.escape(m["result"]["outcome"] or "?")}, '
             f'{_clock(md["duration_ms"])}, build {html.escape(md["build"].get("commit_sha", "?"))}, '
             f'{m["metrics"]["wipes"]} wipes, {m["metrics"]["fights"]} fights</text>')
    if len(planned) > 1:
        poly = " ".join(f"{sx(s['x'], s['y']):.1f},{sy(s['x'], s['y']):.1f}" for s in planned)
        e.append(f'<polyline points="{poly}" fill="none" stroke="#999" stroke-width="2" stroke-dasharray="6,4"/>')
    for s in planned:
        X, Y = sx(s["x"], s["y"]), sy(s["x"], s["y"])
        e.append(f'<circle cx="{X:.1f}" cy="{Y:.1f}" r="3" fill="#999"><title>step {s["step"]} {html.escape(s["kind"])} '
                 f'{html.escape(s["name"])}</title></circle>')
        e.append(f'<text x="{X + 5:.1f}" y="{Y - 5:.1f}" font-size="9" fill="#777">{s["step"]}</text>')
    # actual path, broken at jumps (teleport, corpse run)
    seg = []
    for a, b in zip(m["path"], m["path"][1:]):
        if None in (a["x"], b["x"]):
            continue
        far = ((a["x"] - b["x"]) ** 2 + (a["y"] - b["y"]) ** 2) ** 0.5 > 100
        color = "#c33" if (a.get("in_combat") or b.get("in_combat")) else "#2563eb"
        dash = ' stroke-dasharray="2,4"' if far else ""
        seg.append(f'<line x1="{sx(a["x"], a["y"]):.1f}" y1="{sy(a["x"], a["y"]):.1f}" x2="{sx(b["x"], b["y"]):.1f}" '
                   f'y2="{sy(b["x"], b["y"]):.1f}" stroke="{color}" stroke-width="2"{dash}>'
                   f'<title>{_clock(b["run_ms"])} {html.escape(str(b.get("state")))} step {b.get("step")}</title></line>')
    e += seg
    for p in m["pulls"]:
        a = p["combat_anchor"] or p["start_position"]
        if not a or a.get("x") is None:
            continue
        X, Y = sx(a["x"], a["y"]), sy(a["x"], a["y"])
        fill = {"cleared": "#16a34a", "wiped": "#dc2626"}.get(p["result"], "#a3a3a3")
        tip = (f"F{p['fight_id']} {p['objective'] or ''} expected {p['expected_count']} engaged {p['engaged_count']} "
               f"adds {p['add_count']} {p['result']} {_clock(p['start_ms'])}")
        e.append(f'<circle cx="{X:.1f}" cy="{Y:.1f}" r="7" fill="{fill}" fill-opacity="0.7" stroke="#111">'
                 f'<title>{html.escape(tip)}</title></circle>')
        e.append(f'<text x="{X + 9:.1f}" y="{Y + 4:.1f}" font-size="11">F{p["fight_id"]}</text>')
    for b in m["bosses"]:
        pos = b.get("position")
        if not pos or pos.get("x") is None:
            continue
        X, Y = sx(pos["x"], pos["y"]), sy(pos["x"], pos["y"])
        e.append(f'<text x="{X:.1f}" y="{Y + 6:.1f}" font-size="20" text-anchor="middle" fill="#ca8a04">&#9733;'
                 f'<title>{html.escape(b["name"])} attempt {b["attempt"]}: {b["result"]}</title></text>')
    for w in m["wipes"]:
        pos = w["position"]
        X, Y = sx(pos["x"], pos["y"]), sy(pos["x"], pos["y"])
        e.append(f'<text x="{X:.1f}" y="{Y + 7:.1f}" font-size="22" font-weight="bold" text-anchor="middle" '
                 f'fill="#b91c1c">&#10005;<title>W{w["wipe"]} {_clock(w["run_ms"])}</title></text>')
    for d_ in m["deaths"]:
        pos = d_["position"]
        if pos.get("x") is None:
            continue
        X, Y = sx(pos["x"], pos["y"]), sy(pos["x"], pos["y"])
        e.append(f'<text x="{X:.1f}" y="{Y + 4:.1f}" font-size="12" text-anchor="middle">&#8224;'
                 f'<title>{html.escape(d_["name"])} ({d_["role"]}) {_clock(d_["run_ms"])}</title></text>')
    for i, it in enumerate(m["interactions"], 1):
        pos = it["position"]
        X, Y = sx(pos["x"], pos["y"]), sy(pos["x"], pos["y"])
        e.append(f'<rect x="{X - 5:.1f}" y="{Y - 5:.1f}" width="10" height="10" fill="#2563eb" '
                 f'transform="rotate(45 {X:.1f} {Y:.1f})"><title>I{i} {html.escape(it["type"])} '
                 f'{html.escape(it.get("target") or "")}: {it["result"]}</title></rect>')
    for i, r in enumerate(m["recoveries"], 1):
        pos = r["start_position"]
        X, Y = sx(pos["x"], pos["y"]), sy(pos["x"], pos["y"])
        e.append(f'<polygon points="{X:.1f},{Y - 6:.1f} {X - 6:.1f},{Y + 5:.1f} {X + 6:.1f},{Y + 5:.1f}" '
                 f'fill="#ea580c" fill-opacity="0.8"><title>R{i} {html.escape(str(r["reason"]))} '
                 f'({html.escape(str(r["member"]))}): {r["result"]}</title></polygon>')
    ly = height - 18
    legend = [("#999", "planned route (dashed)"), ("#2563eb", "leader path"), ("#c33", "path in combat"),
              ("#16a34a", "fight cleared"), ("#dc2626", "fight wiped"), ("#ca8a04", "boss"),
              ("#b91c1c", "wipe"), ("#2563eb", "interaction"), ("#ea580c", "recovery")]
    lx = pad
    for color, label in legend:
        e.append(f'<rect x="{lx}" y="{ly - 9}" width="10" height="10" fill="{color}"/>'
                 f'<text x="{lx + 14}" y="{ly}" font-size="11">{label}</text>')
        lx += 14 + 7 * len(label) + 12
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" font-family="sans-serif">'
            f'<rect width="100%" height="100%" fill="#fff"/>' + "".join(e) + "</svg>\n")
