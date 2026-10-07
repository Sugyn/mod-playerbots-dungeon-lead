"""Evidence analyzer: run model -> verdict and failure classifications.

Reads run_model.json only (never raw events). Every classification separates FACTS (from the
model), the CLASSIFICATION, a PROBABLE CAUSE with a CONFIDENCE, and EVIDENCE references (fight,
wipe, alert ids and times). When the model can't support a cause, the class is OBSERVABILITY - no
guessing.
"""

ANALYSIS_VERSION = 1

CLASSES = ("ROUTE", "PACK_IDENTITY", "TARGETING", "PULL", "LEASH", "READINESS", "RECOVERY", "WIPE",
           "INTERACTION", "BOSS", "LEADERSHIP", "TELEPORT", "UPSTREAM_PLAYERBOT", "DATA_ERROR", "CAMPAIGN",
           "UNKNOWN", "OBSERVABILITY")

_WHY_CLASS = {
    "door_closed": ("INTERACTION", "a closed door blocked the way to the objective"),
    "pull_failed": ("PULL", "the pull could not be started or established"),
    "not_found": ("ROUTE", "the objective was not found where the route expects it"),
    "path": ("ROUTE", "no usable path to the objective"),
    "stuck": ("ROUTE", "the leader got stuck on the way"),
    "reset_too_often": ("BOSS", "the objective's pack kept resetting (evade, a scripted move or teleport)"),
}


def _finding(cls, title, facts, cause=None, confidence=None, evidence=(), run_ms=None):
    assert cls in CLASSES, cls
    return {"classification": cls, "title": title, "run_ms": run_ms, "facts": list(facts),
            "probable_cause": cause, "confidence": confidence, "evidence": list(evidence)}


def _last_wiped_fight(m, before_ms):
    fights = [f for f in m["pulls"] if f["result"] == "wiped" and f["start_ms"] <= before_ms]
    return fights[-1] if fights else None


# An en-route fight (no unit of the objective's pack in it): trash met on the walk. Units that
# joined within this many ms of the fight's start came together; later ones kept joining.
_TOGETHER_MS = 5000


def _en_route_wipe(w, f, facts, evidence):
    together = [u for u in f["units"] if u["joined_ms"] - f["start_ms"] <= _TOGETHER_MS]
    later = [u for u in f["units"] if u["joined_ms"] - f["start_ms"] > _TOGETHER_MS]
    span = max((u["joined_ms"] for u in f["units"]), default=f["start_ms"]) - f["start_ms"]
    facts = facts + [f"fight F{f['fight_id']} on the way to {f.get('on_the_way_to') or 'unknown'} (no planned pack)",
                     f"engaged {f['engaged_count']}: {len(together)} within {_TOGETHER_MS // 1000} s, "
                     f"{len(later)} later over {span // 1000} s"]
    title = f"Wipe #{w['wipe']}: trash fight on the walk"
    if len(together) >= 6:
        return _finding("ROUTE", title, facts,
                        f"the walk ran into {len(together)} units at once - the route has no pull steps for "
                        "the packs on this way", "MEDIUM", evidence, w["run_ms"])
    if len(later) >= 4:
        return _finding("PULL", title, facts,
                        f"units kept joining for {span // 1000} s - neighbouring packs chain-pulled into the fight",
                        "MEDIUM", evidence, w["run_ms"])
    return _finding("WIPE", title, facts, "a small trash fight beat the party", "LOW", evidence, w["run_ms"])


def _wipe_findings(m):
    out = []
    for w in m["wipes"]:
        f = _last_wiped_fight(m, w["run_ms"])
        facts = [f"wipe #{w['wipe']} at {w['run_ms'] // 1000} s",
                 f"first death: {w['first_death'] or 'not recorded'}"]
        first = w["deaths_before"][0] if w["deaths_before"] else None
        healer = next((p for p in (first.get("party") or []) if p.get("role") == "healer"), None) if first else None
        if healer:
            facts.append(f"healer {healer['name']} at {first['name']}'s death: {healer['dist']:.0f} yd, "
                         f"{'in' if healer.get('los') else 'out of'} sight, mana {healer.get('mana_pct')}%, "
                         f"{'casting' if healer.get('casting') else 'not casting'}"
                         f"{'' if healer.get('alive') else ', dead'}")
        evidence = [f"W{w['wipe']}"]
        if f is None:
            out.append(_finding("OBSERVABILITY", f"Wipe #{w['wipe']}: no fight evidence", facts,
                                "insufficient evidence - no fight recorded for this wipe", None, evidence, w["run_ms"]))
            continue
        evidence.append(f"F{f['fight_id']}")
        if f.get("kind") == "en_route":
            out.append(_en_route_wipe(w, f, facts, evidence))
            continue
        facts += [f"fight F{f['fight_id']} objective: {f['objective'] or 'unknown'}",
                  f"expected {f['expected_count'] if f['expected_count'] is not None else 'unknown'}, "
                  f"engaged {f['engaged_count']}, adds {f['add_count'] if f['add_count'] is not None else 'unknown'}"]
        if f["add_count"] and f["expected_count"] and f["add_count"] >= f["expected_count"]:
            names = sorted({a["name"] for a in f["unexpected_adds"]})
            out.append(_finding("PACK_IDENTITY", f"Wipe #{w['wipe']}: more adds than the planned pack", facts,
                                f"other packs joined the fight ({', '.join(names)})", "HIGH", evidence, w["run_ms"]))
        elif f["add_count"]:
            out.append(_finding("WIPE", f"Wipe #{w['wipe']}: adds joined", facts,
                                "adds joined the fight; not enough alone to explain the wipe", "LOW", evidence,
                                w["run_ms"]))
        elif f["expected_count"] is None:
            out.append(_finding("OBSERVABILITY", f"Wipe #{w['wipe']}: pack membership unknown", facts,
                                "insufficient evidence - the pack's members were not resolved", None, evidence,
                                w["run_ms"]))
        else:
            out.append(_finding("WIPE", f"Wipe #{w['wipe']}: planned pack only", facts,
                                "the planned pack alone beat the party (difficulty at this level, or class AI)",
                                "LOW", evidence, w["run_ms"]))
    return out


def _route_findings(m):
    out = []
    for step in m["route"]["step_events"]:
        for ev in step["events"]:
            if ev["event"] != "objective_failed":
                continue
            cls, cause = _WHY_CLASS.get(ev.get("why"), ("UNKNOWN", None))
            facts = [f"step {step['step']} {step['name']}: {ev.get('requirement')} objective failed",
                     f"reason {ev.get('why')}, round {ev.get('round')}, action {ev.get('action')}"]
            evidence = []
            if ev.get("why") == "door_closed":
                doors = [i for i in m["interactions"] if i["type"] == "door" and i["result"] == "failed"]
                for idx, it in enumerate(m["interactions"], 1):
                    if it in doors:
                        evidence.append(f"I{idx}")
                facts += [f"door {d.get('target')} (entry {d.get('entry')}) failed at {d['start_ms'] // 1000} s"
                          for d in doors]
            out.append(_finding(cls, f"Objective failed: {step['name']}", facts, cause,
                                "MEDIUM" if cause else None, evidence, ev["run_ms"]))
    return out


def _alert_findings(m):
    out = []
    for a in m["alerts"]:
        if a["kind"] == "unexpected_teleport":
            out.append(_finding("TELEPORT", "Unexpected teleport", [a["text"]], None, None, [], a["run_ms"]))
        elif a["kind"] == "recovery_failed":
            out.append(_finding("RECOVERY", "Recovery failed", [a["text"]], None, None, [], a["run_ms"]))
        elif a["kind"] == "leadership_failed":
            out.append(_finding("LEADERSHIP", "Leadership failed", [a["text"]], None, None, [], a["run_ms"]))
        elif a["kind"] == "telemetry_dropped":
            out.append(_finding("OBSERVABILITY", "Telemetry lines dropped", [a["text"]],
                                "the telemetry buffer was full - the record has gaps", "HIGH", [], a["run_ms"]))
    return out


def _campaign_findings(m, campaign_result):
    if not campaign_result:
        return []
    if campaign_result.get("started") and not campaign_result.get("ended"):
        return [_finding("CAMPAIGN", "The campaign stopped a live run",
                         [f"campaign {campaign_result.get('campaign_id')} recorded ended=false after "
                          f"{campaign_result.get('minutes')} min"],
                         "the validation campaign released the party while its session was active "
                         "(time cap or campaign bug) - not a dungeon AI failure", "HIGH", [], None)]
    return []


def verdict(m, campaign_result=None):
    res, route, mt, md = m["result"], m["route"], m["metrics"], m["metadata"]
    mandatory_failed = any(ev["event"] == "objective_failed" and ev.get("requirement") in ("boss", "required")
                           for s in route["step_events"] for ev in s["events"])
    terminal = res["terminal_event"]
    if not md.get("complete_record"):
        result, why = "UNKNOWN", "the record starts mid-run"
    elif res["outcome"] == "running" and terminal in (None, "stop"):
        result, why = "UNKNOWN", "the record ends while the run was still running"
    elif route["complete"] and res["outcome"] == "complete" and not mandatory_failed:
        result, why = "FULL_ROUTE", "route end reached, no mandatory objective failed"
    elif any(a["kind"] == "leadership_failed" for a in m["alerts"]):
        result, why = "FAILED", "leadership was never established"
    elif mt["bosses_killed"] or (res["final_step"] or 0) > 0:
        result, why = "PARTIAL", f"progress to step {res['final_step']}, outcome {res['outcome']}"
    else:
        result, why = "FAILED", "no route progress"

    unhealthy, warnings = [], []
    kinds = [a["kind"] for a in m["alerts"]]
    for k in ("telemetry_dropped", "unexpected_teleport", "recovery_failed"):
        if k in kinds:
            unhealthy.append(k)
    if mandatory_failed:
        unhealthy.append("mandatory_objective_failed")
    if campaign_result and campaign_result.get("started") and not campaign_result.get("ended"):
        unhealthy.append("campaign_stopped_live_run")
    for k in ("skip_stuck", "leader_unstuck", "strategy_restored"):
        if k in kinds:
            warnings.append(k)
    if mt["wipes"]:
        warnings.append(f"wipes={mt['wipes']}")
    health = "unhealthy" if unhealthy else "warning" if warnings else "clean"
    return {"analysis_version": ANALYSIS_VERSION, "run_id": md["run_id"], "result": result, "result_reason": why,
            "health": health, "unhealthy": unhealthy, "warnings": warnings,
            "commit_sha": md["build"].get("commit_sha"), "campaign_id": md.get("campaign_id"),
            "scenario_id": md.get("scenario_id")}


def failures(m, campaign_result=None):
    out = _route_findings(m) + _wipe_findings(m) + _alert_findings(m) + _campaign_findings(m, campaign_result)
    out.sort(key=lambda f: (f["run_ms"] is None, f["run_ms"] or 0, f["title"]))
    return {"analysis_version": ANALYSIS_VERSION, "run_id": m["metadata"]["run_id"], "findings": out}
