"""Run reconstruction: raw schema v2 events of one run -> the canonical run model.

Deterministic: the same events give the same model (ordered by event_seq, no wall-clock reads, no
randomness). Facts only - classification and verdicts are analyze_run.py's job and read this
model, never the raw events.

Where a v1 event has no structured payload yet, its free-text `detail` is read by legacy.py - the
only place that parses text; each such field is marked in the model as `"source": "detail"`.
"""
from . import legacy

MODEL_VERSION = 1

_TIMELINE_STATES = {"combat", "boss_combat", "wipe_recovery", "recovery", "boss_prep", "stopped", "aborted"}


def _pos(ev):
    ld = ev.get("leader") or {}
    return {"x": ld.get("x"), "y": ld.get("y"), "z": ld.get("z")}


def _unit_pos(u):
    return {"x": u.get("x"), "y": u.get("y"), "z": u.get("z")}


class _Builder:
    def __init__(self, events, dropped):
        self.events = events
        self.dropped = dropped
        self.timeline = []
        self.alerts = []

    def note(self, ev, kind, text, ref=None):
        entry = {"run_ms": ev["run_ms"], "event_seq": ev["event_seq"], "kind": kind, "text": text}
        if ref is not None:
            entry["ref"] = ref
        self.timeline.append(entry)

    def alert(self, ev, kind, text):
        self.alerts.append({"run_ms": ev["run_ms"], "event_seq": ev["event_seq"], "kind": kind, "text": text,
                            "position": _pos(ev)})

    # -- sections -----------------------------------------------------------------------------

    def metadata(self):
        first, last = self.events[0], self.events[-1]
        dungeon = next((e["dungeon"] for e in reversed(self.events) if (e.get("dungeon") or {}).get("lfg_id")),
                       first.get("dungeon"))
        return {
            "run_id": first["run_id"],
            "campaign_id": first.get("campaign_id", ""),
            "scenario_id": first.get("scenario_id", ""),
            "build": first.get("build", {}),
            "dungeon": dungeon,
            "leader": (first.get("leader") or {}).get("name"),
            "started_at": first.get("wall_time"),
            "ended_at": last.get("wall_time"),
            "duration_ms": last["run_ms"],
            "events": len(self.events),
            "first_event_seq": first["event_seq"],
            "last_event_seq": last["event_seq"],
            # a run whose first event isn't seq 1 started before this file - the model is partial
            "complete_record": first["event_seq"] == 1,
        }

    def party(self):
        for ev in self.events:
            if ev["event_type"] == "party_roster":
                return {"members": ev["payload"].get("members", []), "source": "party_roster"}
        return {"members": [], "source": None}

    def path(self):
        samples = [e for e in self.events if e["event_type"] == "position_sample"]
        out = []
        for e in samples:
            p = _pos(e)
            p.update({"run_ms": e["run_ms"], "state": e.get("state"), "step": e.get("step"),
                      "pack_id": e.get("pack_id"), "moving": e["payload"].get("moving"),
                      "in_combat": e["payload"].get("in_combat")})
            out.append(p)
        return out

    def path_decisions(self):
        """How the route walk chose its way (MoveRouteTo): branch, path type and the computed path."""
        out = []
        for e in self.events:
            if e["event_type"] != "path_decision":
                continue
            p = e["payload"]
            out.append({"run_ms": e["run_ms"], "step": e.get("step"), "kind": p.get("kind"),
                        "objective": p.get("objective"), "from": _pos(e),
                        "dest": {"x": p.get("dest_x"), "y": p.get("dest_y"), "z": p.get("dest_z")},
                        "move_to": {"x": p.get("move_x"), "y": p.get("move_y"), "z": p.get("move_z")},
                        "path_type": p.get("path_type"), "path_length": p.get("path_length"),
                        "path": p.get("path") or []})
        return out

    def route(self):
        steps = {}
        for e in self.events:
            t = e["event_type"]
            if t in ("objective_skipped", "objective_failed", "objective_retry"):
                p = e["payload"]
                if "why" in p:
                    o = {k: p.get(k) for k in ("name", "requirement", "why", "round", "action")}
                    src = "payload"
                else:
                    o, src = legacy.objective(p.get("detail", "")), "detail"
                steps.setdefault(e["step"], {"step": e["step"], "name": o["name"], "events": []})["events"].append(
                    {"run_ms": e["run_ms"], "event": t, **o, "source": src})
            elif t == "skip_stuck":
                steps.setdefault(e["step"], {"step": e["step"], "name": legacy.first_word_name(e["payload"].get("detail", "")),
                                             "events": []})["events"].append(
                    {"run_ms": e["run_ms"], "event": t, "detail": e["payload"].get("detail", ""), "source": "detail"})
        complete = [e for e in self.events if e["event_type"] == "route_complete"]
        return {
            "selected": next((e["payload"].get("detail") for e in self.events if e["event_type"] == "route_selected"), None),
            "complete": bool(complete),
            "complete_detail": complete[-1]["payload"].get("detail") if complete else None,
            "last_step": self.events[-1].get("step"),
            "step_events": [steps[k] for k in sorted(steps)],
        }

    def fights(self):
        """Fights from fight_started / combat_unit_joined / mob_died / fight_ended, with the pack the
        tank was working on and its members (pull_members_resolved)."""
        resolved = {}  # pack_id -> last pull_members_resolved payload (+ run_ms)
        pack_names = {}  # pack_id -> name, from pack_state
        fights, cur = [], None
        wipes_at = [e["run_ms"] for e in self.events if e["event_type"] == "wipe_detected"]
        for e in self.events:
            t, p = e["event_type"], e["payload"]
            if t == "pack_state" and "pack_id" in p:
                pack_names[p["pack_id"]] = p.get("name")
            if t == "pull_members_resolved":
                resolved[p["pack_id"]] = dict(p, run_ms=e["run_ms"])
            elif t == "fight_started":
                cur = {"fight_id": p["fight_id"], "start_ms": e["run_ms"], "start_position": _pos(e),
                       "state_at_start": e.get("state"), "step": e.get("step"), "pack_id": e.get("pack_id"),
                       "anchor": None, "units": [], "killed": [], "deaths": [], "end_ms": None}
                fights.append(cur)
            elif cur is None:
                continue
            elif t == "combat_anchor_set":
                if cur["anchor"] is None or p.get("kind") != "fight_start":
                    cur["anchor"] = {k: p.get(k) for k in ("kind", "x", "y", "z", "radius")}
            elif t == "combat_unit_joined" and p.get("fight_id") == cur["fight_id"]:
                u = p["unit"]
                cur["units"].append({"guid": u["guid"], "spawn": u.get("spawn"), "entry": u["entry"], "name": u["name"],
                                     "position": _unit_pos(u), "level": p.get("level"), "elite": p.get("elite"),
                                     "joined_ms": e["run_ms"], "pack_id": p.get("pack_id"),
                                     "in_pack_flag": p.get("in_pack"), "victim": p.get("victim")})
            elif t == "mob_died" and p.get("fight_id") == cur["fight_id"]:
                cur["killed"].append({"guid": p["unit"]["guid"], "name": p["unit"]["name"], "run_ms": e["run_ms"]})
            elif t == "member_died":
                cur["deaths"].append({"name": p["name"], "role": p.get("role"), "run_ms": e["run_ms"]})
            elif t == "fight_ended" and p.get("fight_id") == cur["fight_id"]:
                cur["end_ms"] = e["run_ms"]
                cur["leader_alive_at_end"] = p.get("leader_alive")
                cur = None

        out = []
        for f in fights:
            pack_ids = [u["pack_id"] for u in f["units"] if u["pack_id"]] or ([f["pack_id"]] if f["pack_id"] else [])
            pack_id = max(set(pack_ids), key=pack_ids.count) if pack_ids else 0
            res = resolved.get(pack_id)
            core = {m["guid"] for m in res["core_members"]} if res else set()
            guids = {u["guid"] for u in f["units"]}
            # A fight is the pull of the current objective's pack only if one of the pack's own
            # members fought in it; anything else is a fight on the way there (en route), whose
            # units are not "adds" of a pull.
            planned = bool(core & guids)
            for u in f["units"]:
                u["in_pack"] = (u["guid"] in core) if planned else None
            adds = [u for u in f["units"] if u["in_pack"] is False]
            end = f["end_ms"]
            wiped = any(f["start_ms"] <= w <= (end if end is not None else float("inf")) for w in wipes_at)
            killed = {k["guid"] for k in f["killed"]}
            if wiped:
                result = "wiped"
            elif end is None:
                result = "unknown"  # the record ends mid-fight
            elif f["units"] and all(u["guid"] in killed for u in f["units"]):
                result = "cleared"
            else:
                result = "ended"  # combat over, not every unit seen dead (reset, fled, despawned)
            out.append({
                "fight_id": f["fight_id"],
                "kind": "planned" if planned else "en_route",
                "pack_id": pack_id,
                "objective": (res["objective"] if planned else None),
                "on_the_way_to": None if planned else (pack_names.get(pack_id) or (res or {}).get("objective")),
                "requirement": res["requirement"] if planned else None,
                "boss": res["boss"] if planned else False,
                "start_ms": f["start_ms"],
                "end_ms": end,
                "duration_ms": (end - f["start_ms"]) if end is not None else None,
                "start_position": f["start_position"],
                "combat_anchor": f["anchor"],
                "expected_members": res["core_members"] if planned else None,
                "expected_count": len(res["core_members"]) if planned else None,
                "engaged_count": len(f["units"]),
                "add_count": len(adds) if planned else None,
                "units": f["units"],
                "unexpected_adds": [{"guid": u["guid"], "entry": u["entry"], "name": u["name"], "position": u["position"],
                                     "joined_ms": u["joined_ms"]} for u in adds],
                "killed": f["killed"],
                "deaths": f["deaths"],
                "result": result,
            })
        return out

    def bosses(self):
        attempts, open_ = [], {}
        wipe_ms = [e["run_ms"] for e in self.events if e["event_type"] == "wipe_detected"]
        for e in self.events:
            if e["event_type"] != "pack_state" or not e["payload"].get("boss"):
                continue
            p = e["payload"]
            key = p["pack_id"]
            if p["to"] == "engaged":
                n = sum(1 for a in attempts if a["pack_id"] == key) + 1
                open_[key] = {"pack_id": key, "name": p["name"], "step": e.get("step"), "attempt": n,
                              "engaged_ms": e["run_ms"], "end_ms": None, "result": "unknown",
                              "position": _pos(e)}
                attempts.append(open_[key])
            elif p["to"] == "cleared":
                a = open_.pop(key, None)
                if a:
                    a.update(end_ms=e["run_ms"], result="killed", duration_ms=e["run_ms"] - a["engaged_ms"])
                else:
                    attempts.append({"pack_id": key, "name": p["name"], "step": e.get("step"), "attempt": 0,
                                     "engaged_ms": None, "end_ms": e["run_ms"], "result": "found_dead",
                                     "position": _pos(e)})
            elif p["to"] in ("available", "unknown") and key in open_:
                a = open_.pop(key)
                wiped = any(a["engaged_ms"] <= w <= e["run_ms"] for w in wipe_ms)
                a.update(end_ms=e["run_ms"], result="wipe" if wiped else "reset",
                         duration_ms=e["run_ms"] - a["engaged_ms"])
        return attempts

    def deaths(self):
        return [{"run_ms": e["run_ms"], "name": e["payload"]["name"], "role": e["payload"].get("role"),
                 "leader": e["payload"].get("leader"), "fight_id": e["payload"].get("fight_id"),
                 "attackers": e["payload"].get("attackers"),
                 "position": _unit_pos(e["payload"])}
                for e in self.events if e["event_type"] == "member_died"]

    def wipes(self, deaths):
        out = []
        for e in self.events:
            t = e["event_type"]
            if t == "wipe_detected":
                n = e["payload"].get("wipe") or legacy.wipe_number(e["payload"].get("detail", ""))
                window = [d for d in deaths if e["run_ms"] - 60000 <= d["run_ms"] <= e["run_ms"]]
                out.append({"wipe": n, "run_ms": e["run_ms"], "position": _pos(e), "step": e.get("step"),
                            "pack_id": e.get("pack_id"), "deaths_before": window,
                            "first_death": window[0]["name"] if window else None,
                            "recovered_ms": None, "checkpoint": None})
            elif t == "wipe_recovered" and out:
                out[-1]["recovered_ms"] = e["run_ms"]
            elif t == "checkpoint_restore" and out:
                p = e["payload"]
                out[-1]["checkpoint"] = ({"from_step": p["from_step"], "to_step": p["to_step"], "name": p["checkpoint"],
                                          "source": "payload"} if "checkpoint" in p
                                         else dict(legacy.checkpoint(p.get("detail", "")), source="detail"))
            elif t == "wipe_giveup" and out:
                out[-1]["gave_up_ms"] = e["run_ms"]
        return out

    def recoveries(self):
        out, cur = [], None
        for e in self.events:
            t, d = e["event_type"], e["payload"].get("detail", "")
            if t == "recovery_start":
                if cur:
                    cur["result"] = "superseded"
                p = e["payload"]
                r, src = ({"reason": p["reason"], "member": p.get("member") or None}, "payload") if "reason" in p \
                    else (legacy.recovery(d), "detail")
                cur = {"reason": r["reason"], "member": r["member"], "start_ms": e["run_ms"],
                       "start_position": _pos(e), "escalations": [], "result": "unknown", "source": src}
                out.append(cur)
            elif cur is None:
                continue
            elif t in ("recovery_escalate", "recovery_escalate_deferred"):
                if len(cur["escalations"]) < 20:
                    cur["escalations"].append({"run_ms": e["run_ms"], "event": t, "detail": d})
            elif t in ("recovery_complete", "recovery_failed"):
                cur.update(end_ms=e["run_ms"], duration_ms=e["run_ms"] - cur["start_ms"], end_position=_pos(e),
                           result="complete" if t == "recovery_complete" else "failed")
                cur = None
        return out

    def interactions(self):
        out, cur = [], None
        for e in self.events:
            t, d = e["event_type"], e["payload"].get("detail", "")
            if t == "interaction_state":
                p = e["payload"]
                s = ({"type": p["type"], "from": p["from"], "to": p["to"], "target": p.get("target"), "entry": p.get("entry")}
                     if "to" in p and "type" in p else legacy.interaction_state(d))
                if s["from"] == "none":
                    cur = {"type": s["type"], "target": s.get("target"), "entry": s.get("entry"),
                           "start_ms": e["run_ms"], "position": _pos(e), "states": [], "result": "unknown",
                           "source": "detail"}
                    out.append(cur)
                if cur is not None:
                    cur["states"].append({"run_ms": e["run_ms"], "to": s["to"]})
                    if s["to"] in ("complete", "failed"):
                        cur.update(end_ms=e["run_ms"], duration_ms=e["run_ms"] - cur["start_ms"], result=s["to"])
                        cur = None
            elif t in ("door_open", "door_opened_by_us", "use", "use_failed", "talk", "talk_not_ours",
                       "key_looted", "area_trigger", "door_not_found"):
                out.append({"type": t, "target": legacy.first_word_name(d), "detail": d, "start_ms": e["run_ms"],
                            "end_ms": e["run_ms"], "position": _pos(e),
                            "result": "failed" if t in ("use_failed", "door_not_found") else "done", "source": "detail"})
        return out

    def collect_alerts(self):
        for e in self.dropped:
            self.alerts.append({"run_ms": None, "event_seq": None, "kind": "telemetry_dropped",
                                "text": f"{e['payload'].get('lines')} lines dropped at {e.get('wall_time')}",
                                "position": None})
        kinds = {"unexpected_teleport", "recovery_failed", "skip_stuck", "leader_unstuck", "strategy_restored",
                 "objective_failed", "wipe_giveup", "leadership_failed"}
        for e in self.events:
            if e["event_type"] in kinds:
                self.alert(e, e["event_type"], e["payload"].get("detail", ""))

    def result(self):
        last = self.events[-1]
        oc = last.get("outcome") or {}
        terminal = next((e for e in reversed(self.events)
                         if e["event_type"] in ("stop", "canary_stop", "test_result", "route_complete")), None)
        return {
            "outcome": oc.get("outcome"),
            "failure_domain": oc.get("failure_domain"),
            "failure_reason": oc.get("failure_reason"),
            "terminal_event": terminal["event_type"] if terminal else None,
            "terminal_detail": terminal["payload"].get("detail") if terminal else None,
            "final_state": last.get("state"),
            "final_step": last.get("step"),
        }

    def build_timeline(self, fights, bosses, wipes, deaths, recoveries, interactions):
        for e in self.events:
            t, p = e["event_type"], e["payload"]
            if t == "start":
                self.note(e, "run", "Run started")
            elif t == "state_transition" and p.get("to") in _TIMELINE_STATES:
                self.note(e, "state", f"{p.get('from')} -> {p.get('to')} ({p.get('reason')})")
            elif t == "mob_fleeing":
                self.note(e, "flee", f"{p.get('name')} runs for help at {p.get('health_pct')}% (fight {p.get('fight_id')})")
            elif t == "boss_killed" and not p.get("current_step"):
                self.note(e, "boss", f"Boss {p.get('name')} killed during another step's fight (step {p.get('step')})")
            elif t in ("objective_skipped", "objective_failed", "route_complete", "canary_stop", "stop"):
                self.note(e, "route" if t != "stop" else "run", f"{t}: {p.get('detail', '')}")
        for f in fights:
            what = f["objective"] or (f"on the way to {f['on_the_way_to']}" if f["on_the_way_to"] else "unplanned fight")
            adds = f" ({f['add_count']} adds)" if f["add_count"] else ""
            self.timeline.append({"run_ms": f["start_ms"], "event_seq": None, "kind": "fight",
                                  "text": f"Fight #{f['fight_id']} - {what}: {f['engaged_count']} units{adds}",
                                  "ref": f"F{f['fight_id']}"})
            if f["end_ms"] is not None:
                self.timeline.append({"run_ms": f["end_ms"], "event_seq": None, "kind": "fight",
                                      "text": f"Fight #{f['fight_id']} {f['result']} - {f['duration_ms'] // 1000} s",
                                      "ref": f"F{f['fight_id']}"})
        for b in bosses:
            if b["engaged_ms"] is not None:
                self.timeline.append({"run_ms": b["engaged_ms"], "event_seq": None, "kind": "boss",
                                      "text": f"Boss {b['name']} engaged (attempt {b['attempt']})"})
            if b["end_ms"] is not None:
                self.timeline.append({"run_ms": b["end_ms"], "event_seq": None, "kind": "boss",
                                      "text": f"Boss {b['name']} {b['result']}"})
        for d in deaths:
            self.timeline.append({"run_ms": d["run_ms"], "event_seq": None, "kind": "death",
                                  "text": f"{d['name']} ({d['role']}) died"})
        for w in wipes:
            self.timeline.append({"run_ms": w["run_ms"], "event_seq": None, "kind": "wipe",
                                  "text": f"WIPE #{w['wipe']}", "ref": f"W{w['wipe']}"})
        for i, r in enumerate(recoveries, 1):
            self.timeline.append({"run_ms": r["start_ms"], "event_seq": None, "kind": "recovery",
                                  "text": f"Recovery {r['reason']} ({r['member']}) - {r['result']}",
                                  "ref": f"R{i}"})
        for i, it in enumerate(interactions, 1):
            self.timeline.append({"run_ms": it["start_ms"], "event_seq": None, "kind": "interaction",
                                  "text": f"{it['type']} {it.get('target') or ''} - {it['result']}".strip(),
                                  "ref": f"I{i}"})
        # stable order: time, then the raw sequence (None after numbered), then text
        self.timeline.sort(key=lambda x: (x["run_ms"], x["event_seq"] is None, x["event_seq"] or 0, x["text"]))
        return self.timeline


def reconstruct(events, dropped=()):
    if not events:
        raise ValueError("no events for this run")
    b = _Builder(events, list(dropped))
    fights = b.fights()
    bosses = b.bosses()
    deaths = b.deaths()
    wipes = b.wipes(deaths)
    recoveries = b.recoveries()
    interactions = b.interactions()
    b.collect_alerts()
    timeline = b.build_timeline(fights, bosses, wipes, deaths, recoveries, interactions)
    model = {
        "model_version": MODEL_VERSION,
        "schema_version": 2,
        "metadata": b.metadata(),
        "party": b.party(),
        "route": b.route(),
        "path": b.path(),
        "path_decisions": b.path_decisions(),
        "timeline": timeline,
        "pulls": fights,
        "bosses": bosses,
        "deaths": deaths,
        "wipes": wipes,
        "recoveries": recoveries,
        "interactions": interactions,
        "alerts": b.alerts,
        "result": b.result(),
    }
    model["metrics"] = metrics(model)
    return model


def metrics(model):
    pulls = model["pulls"]
    known_adds = [p["add_count"] for p in pulls if p["add_count"] is not None]
    return {
        "duration_ms": model["metadata"]["duration_ms"],
        "fights": len(pulls),
        "fights_planned": sum(1 for p in pulls if p["kind"] == "planned"),
        "fights_en_route": sum(1 for p in pulls if p["kind"] == "en_route"),
        "fights_cleared": sum(1 for p in pulls if p["result"] == "cleared"),
        "units_engaged": sum(p["engaged_count"] for p in pulls),
        "unexpected_adds": sum(known_adds) if known_adds else None,
        "bosses_killed": sum(1 for b in model["bosses"] if b["result"] in ("killed", "found_dead")),
        "boss_attempts_failed": sum(1 for b in model["bosses"] if b["result"] in ("wipe", "reset")),
        "deaths": len(model["deaths"]),
        "wipes": len(model["wipes"]),
        "recoveries": len(model["recoveries"]),
        "recoveries_failed": sum(1 for r in model["recoveries"] if r["result"] == "failed"),
        "interactions": len(model["interactions"]),
        "interactions_failed": sum(1 for i in model["interactions"] if i["result"] == "failed"),
        "path_samples": len(model["path"]),
        "path_length_yd": round(_path_length(model["path"]), 1),
        "alerts": len(model["alerts"]),
    }


def _path_length(path):
    total = 0.0
    for a, b in zip(path, path[1:]):
        if None in (a["x"], a["y"], b["x"], b["y"]):
            continue
        d = ((a["x"] - b["x"]) ** 2 + (a["y"] - b["y"]) ** 2 + ((a["z"] or 0) - (b["z"] or 0)) ** 2) ** 0.5
        if d < 100:  # a jump (teleport, corpse run) is not walked distance
            total += d
    return total
