"""Run reconstruction: raw schema v2 events of one run -> the canonical run model.

Deterministic: the same events give the same model (ordered by event_seq, no wall-clock reads, no
randomness). Facts only - classification and verdicts are analyze_run.py's job and read this
model, never the raw events.

Where a v1 event has no structured payload yet, its free-text `detail` is read by legacy.py - the
only place that parses text; each such field is marked in the model as `"source": "detail"`.
"""
from . import legacy, run_metrics

# 2: teleports, cc_marks, dead_target_waits, time_spans, wipes[].resumed_ms and the metrics built on
#    them (additive - every version 1 field keeps its meaning)
MODEL_VERSION = 2

_TIMELINE_STATES = {"combat", "boss_combat", "wipe_recovery", "recovery", "boss_prep", "stopped", "aborted"}


# Where the run's time goes, by leader state. Unlisted states (starting, completing, stopping) are
# "other".
_STATE_CATEGORY = {
    "travelling": "walking",
    "pre_pull": "pulling", "pulling": "pulling",
    "combat": "combat", "boss_combat": "combat",
    "boss_prep": "boss_prep",
    "waiting_ready": "waiting", "post_combat": "waiting",
    "recovery": "recovery", "wipe_recovery": "recovery",
}

# Readiness texts (DungeonLeadKernel ReadyStatus) -> waiting reasons. A text not listed is "other";
# no evidence at all is "unattributed".
_READY_REASON = {
    "healer low on mana": "healer_mana",
    "group too spread": "party_range",
    "group spread out, holding the pull": "party_range",
    "waiting for you, master too far": "party_range",
    "party member lost (other map or offline)": "party_range",
    "healer out of sight or range of the tank, holding the pull": "party_range",
    "someone is eating/drinking": "eating_drinking",
    "party member dead": "dead_member",
    "healer dead or not in the instance": "dead_member",
    "someone is low on health": "low_health",
}


# A recovery_start this close before the switch to the recovery state names its reason even if
# that episode is already recorded as over.
_RECOVERY_PRIOR_MS = 5000


def _wait_reason(text):
    if not text:
        return "unattributed"
    return _READY_REASON.get(text, "other")


def _split(a, b, evidence, prior):
    """[a, b) labelled by evidence [(ms, label)]: each label holds from its time to the next one.
    Before the first, `prior` - or, without one, the first label (the waiting line is throttled and
    comes up to 10 s after the wait began)."""
    evidence = sorted(evidence)
    if not evidence:
        return [(a, b, prior or "unattributed")]
    out = [(a, max(a, evidence[0][0]), prior or evidence[0][1])]
    for i, (ms, label) in enumerate(evidence):
        nxt = evidence[i + 1][0] if i + 1 < len(evidence) else b
        out.append((max(a, ms), min(b, nxt), label))
    return [s for s in out if s[1] > s[0]]


def _carve(a, b, overrides):
    """[a, b) with the parts covered by override intervals (start, end, reason, rank) labelled by
    the lowest-ranked one covering them; the rest keeps reason None."""
    cuts = sorted({a, b} | {x for s, e, _, _ in overrides for x in (s, e) if a < x < b})
    out = []
    for lo, hi in zip(cuts, cuts[1:]):
        cover = [o for o in overrides if o[0] <= lo and o[1] >= hi]
        out.append((lo, hi, min(cover, key=lambda o: o[3])[2] if cover else None))
    return out


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
        start = next((e for e in self.events if e["event_type"] == "start"), None)
        selected = next((e for e in self.events if e["event_type"] == "route_selected"), None)
        scope = (selected or {}).get("payload", {}).get("scope")
        return {
            "run_id": first["run_id"],
            "campaign_id": first.get("campaign_id", ""),
            "scenario_id": first.get("scenario_id", ""),
            "build": first.get("build", {}),
            # module/core/playerbots commits and route/config/mmap digests; None for runs
            # recorded before the context existed (not invented after the fact)
            "context": (start or {}).get("payload", {}).get("context"),
            # what the route models of the dungeon (full / partial_model / no_mandatory)
            # and the rows it does not model; None for runs recorded before
            "route_scope": {"scope": scope, "unmodeled": selected["payload"].get("unmodeled", [])} if scope else None,
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
                 "party": e["payload"].get("party"),
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
        # the party moving on again: the first switch to travelling after the wipe and before the
        # next one (None: the record ends, or the next wipe comes, before it travels again)
        travel = [e["run_ms"] for e in self.events
                  if e["event_type"] == "state_transition" and e["payload"].get("to") == "travelling"]
        for i, w in enumerate(out):
            nxt = out[i + 1]["run_ms"] if i + 1 < len(out) else float("inf")
            w["resumed_ms"] = next((t for t in travel if w["run_ms"] < t < nxt), None)
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

    def teleports(self):
        """Every teleport the module made, plus the ones it cancelled (`unexpected`): one entry per
        event, `who` = the players moved (None where the record does not say)."""
        out = []
        # A run that records member_teleport names every party bot it moved there; its
        # recovery_escalate lines then repeat the same teleports and are not counted again.
        explicit = any(e["event_type"] == "member_teleport" for e in self.events)
        for e in self.events:
            t, d = e["event_type"], e["payload"].get("detail", "")
            if t in ("leader_unstuck", "leader_to_floor"):
                kind, who = t, [(e.get("leader") or {}).get("name")]
            elif t == "member_teleport":
                # one party bot moved, with its reason: back to the entrance after a death
                # (wipe_recovery) or to the leader (member_to_leader) - the same kinds as before
                p = e["payload"]
                kind = "wipe_recovery" if p.get("context") == "run_back" else "member_to_leader"
                out.append({"run_ms": e["run_ms"], "kind": kind, "who": [p.get("who")],
                            "reason": p.get("reason"), "context": p.get("context"), "detail": d,
                            "position": _pos(e), "source": "payload"})
                continue
            elif t == "recovery_escalate":
                if explicit:
                    continue
                x = legacy.escalation(d)
                if x["action"] == "none":
                    continue
                # the leader brings the straggler and every other far bot in one escalation
                kind, who = "member_to_leader", ([x["member"]] + x["others"]) if x["action"] else None
            elif t == "wipe_recovery_teleport":
                kind, who = "wipe_recovery", [d or None]
            elif t == "unexpected_teleport":
                kind, who = "unexpected", [legacy.teleport_player(d)]
            else:
                continue
            out.append({"run_ms": e["run_ms"], "kind": kind, "who": who, "detail": d, "position": _pos(e),
                        "source": "detail"})
        return out

    def run_back(self):
        """The run back of dead and straggling party bots (`run_back` events), or None for a run
        recorded before them."""
        evs = [e for e in self.events if e["event_type"] == "run_back"]
        start = next((e for e in self.events if e["event_type"] == "start"), None)
        if not evs and not (start and start["payload"].get("run_back")):
            return None
        out = []
        for e in evs:
            p = e["payload"]
            out.append({"run_ms": e["run_ms"], "phase": p.get("phase"), "who": p.get("who"),
                        "straggler": bool(p.get("straggler")), "ms": p.get("ms"), "how": p.get("how"),
                        "reason": p.get("reason")})
        return out

    def cc_marks(self):
        """Each moon (crowd-control mark) and how it ended: landed (CC seen on the target before the
        mark ended), released (taken off without CC), target_gone (died or despawned without CC),
        replaced (a new moon while this one had no outcome) or unresolved (record ended).
        `cc_outcome` (newer runs) adds its own outcome and, for a landed one, `by`: the caster and
        spell of the control aura; `by` stays None where it is not recorded."""
        out, cur = [], None
        for e in self.events:
            t, d = e["event_type"], e["payload"].get("detail", "")
            if t == "mark_moon":
                if cur is not None:
                    cur.update(end_ms=e["run_ms"], end_event=t,
                               outcome="landed" if cur["landed_ms"] is not None else "replaced")
                cur = {"placed_ms": e["run_ms"], "target": d or None, "step": e.get("step"),
                       "pack_id": e.get("pack_id"), "landed_ms": None, "end_ms": None, "end_event": None,
                       "release_reason": None, "outcome": "unresolved", "cc_outcome": None,
                       "by": None, "source": "detail"}
                out.append(cur)
            elif t == "cc_outcome":
                # written right after cc_landed (moon still open) or cc_released (just closed)
                mark = cur if cur is not None else (out[-1] if out and out[-1]["cc_outcome"] is None else None)
                if mark is None:
                    continue
                p = e["payload"]
                mark["cc_outcome"] = p.get("outcome")
                if p.get("outcome") == "landed":
                    if mark["landed_ms"] is None:
                        mark["landed_ms"] = e["run_ms"]
                        if mark is not cur:
                            mark["outcome"] = "landed"
                    if p.get("spell_id") or p.get("caster"):
                        mark["by"] = {"caster": p.get("caster") if p.get("caster") not in (None, "", "-") else None,
                                      "spell": p.get("spell") or None, "spell_id": p.get("spell_id")}
            elif cur is None:
                continue
            elif t == "cc_landed":
                if cur["landed_ms"] is None:
                    cur["landed_ms"] = e["run_ms"]
            elif t in ("cc_released", "cc_target_gone"):
                landed = cur["landed_ms"] is not None
                cur.update(end_ms=e["run_ms"], end_event=t,
                           release_reason=legacy.cc_release_reason(d) if t == "cc_released" else None,
                           outcome="landed" if landed else ("released" if t == "cc_released" else "target_gone"))
                cur = None
        return out

    def dead_target_waits(self):
        """Time on a route step whose target was already dead or not there, until the step was left
        (advanced, skipped or given up). Two kinds of evidence:
          found_dead - the step's pack went to `cleared` without this party engaging it on this
                       visit of the step (pack_state); from then until the step is left.
          not_found  - nothing at the destination (not_found); from the arrival (reached) on.
        One episode per step visit: a found-dead wait that ends in not_found stays one episode.
        `duration_ms` counts only the time the leader was walking or pulling (the states in which it
        works on the step itself) - a fight, a readiness wait or a recovery meanwhile holds it for
        its own reason; `elapsed_ms` is the whole episode. Without state transitions both are the
        same. None when the record has no step tracking at all."""
        tracked = {"pack_state", "reached", "travel_reached", "already_dead", "not_found"}
        if not any(e["event_type"] in tracked for e in self.events):
            return None
        states = self.state_spans()
        active = [(a, b) for s, a, b in states or [] if _STATE_CATEGORY.get(s) in ("walking", "pulling")]

        def on_step_ms(a, b):
            if states is None:
                return b - a
            return sum(max(0, min(b, y) - max(a, x)) for x, y in active)

        ends = {"already_dead", "objective_skipped", "objective_failed", "skip_stuck", "step_walk_limit",
                "travel_reached", "stuck_alive", "not_found"}
        out, ep = [], None
        cur_step, visit_start, engaged, reached = object(), 0, set(), None

        def close(ms, how):
            nonlocal ep
            if ep is not None:
                ep.update(end_ms=ms, elapsed_ms=ms - ep["start_ms"], duration_ms=on_step_ms(ep["start_ms"], ms),
                          ended_by=how)
                out.append(ep)
                ep = None

        for e in self.events:
            t, p = e["event_type"], e["payload"]
            if e.get("step") != cur_step:
                close(e["run_ms"], "step_changed")
                cur_step, visit_start, engaged, reached = e.get("step"), e["run_ms"], set(), None
            if t == "pack_state" and "to" in p:
                if p["to"] == "engaged":
                    engaged.add(p.get("pack_id"))
                elif (p["to"] == "cleared" and p.get("from") != "engaged" and p.get("pack_id") not in engaged
                      and p.get("pack_id") == e.get("pack_id") and ep is None):
                    ep = {"step": e.get("step"), "name": p.get("name"), "pack_id": p.get("pack_id"),
                          "kind": "found_dead", "start_ms": e["run_ms"]}
            elif t == "reached" and reached is None:
                reached = e["run_ms"]
            if t == "not_found" and ep is None:
                ep = {"step": e.get("step"), "name": p.get("detail") or None, "pack_id": e.get("pack_id"),
                      "kind": "not_found", "start_ms": reached if reached is not None else visit_start}
            if t in ends:
                close(e["run_ms"], t)
        close(self.events[-1]["run_ms"], "record_end")
        return out

    def state_spans(self):
        """[(state, start_ms, end_ms)] from 0 to the last event, from state_transition; None without
        any transition."""
        transitions = [e for e in self.events if e["event_type"] == "state_transition"]
        if not transitions:
            return None
        end_ms = self.events[-1]["run_ms"]
        states, cur, start = [], self.events[0].get("state"), 0
        for e in transitions:
            to = e["payload"].get("to") or e.get("state")
            if to == cur:
                continue
            if e["run_ms"] > start:
                states.append((cur, start, e["run_ms"]))
            cur, start = to, e["run_ms"]
        if end_ms > start:
            states.append((cur, start, end_ms))
        return states

    def time_spans(self, recoveries, dead_waits):
        """The run cut into consecutive spans that cover it exactly once: state, category (walking,
        pulling, combat, boss_prep, waiting, recovery, other) and, for waiting and recovery, why.
        Waiting is read from the waiting / post-combat states and, inside walking, from a pull
        held for the party or a patrol and from a dead-target wait. None without state transitions."""
        states = self.state_spans()
        if states is None:
            return None
        end_ms = self.events[-1]["run_ms"]
        waiting_ev = [(e["run_ms"], _wait_reason(legacy.readiness(e["payload"].get("detail", ""))))
                      for e in self.events if e["event_type"] == "waiting"]
        decisions = [(e["run_ms"], legacy.post_combat_decision(e["payload"].get("detail", ""))["readiness"])
                     for e in self.events if e["event_type"] == "post_combat_decision"]
        holds, hold = [], None  # pull held for the party or a patrol: (start, end, reason)
        for e in self.events:
            if e["event_type"] != "pull_state":
                continue
            p = e["payload"]
            s = {"to": p["to"], "reason": p.get("reason")} if "to" in p else legacy.pull_state(p.get("detail", ""))
            if hold is not None:
                holds.append((hold[0], e["run_ms"], hold[1]))
                hold = None
            if s["to"] == "waiting_patrol":
                hold = (e["run_ms"], "patrol")
            elif s["to"] == "waiting_party":
                hold = (e["run_ms"], _wait_reason(s["reason"]))
        if hold is not None:
            holds.append((hold[0], end_ms, hold[1]))
        # a dead-target wait outranks a pull hold where both cover the same time
        overrides = [(d["start_ms"], d["end_ms"], "dead_target", 0) for d in (dead_waits or [])] + \
                    [(a, b, r, 1) for a, b, r in holds]
        rec_ev = [(r["start_ms"], r["reason"] or "unattributed") for r in recoveries]

        out = []
        for state, a, b in states:
            cat = _STATE_CATEGORY.get(state, "other")
            if cat == "waiting":
                ev = [(ms, r) for ms, r in waiting_ev if a <= ms <= b]
                if not ev and state == "post_combat":
                    ev = [(ms, "post_combat_check" if r == "ready" else _wait_reason(r))
                          for ms, r in decisions if a <= ms <= b][:1]
                segs = _split(a, b, ev, None)
            elif cat == "recovery":
                if state == "wipe_recovery":
                    segs = [(a, b, "wipe")]
                else:
                    # the reason of the episode still open when the state began, or of one that
                    # started just before it - an older, finished episode says nothing about this one
                    last = next((r for r in reversed(recoveries) if r["start_ms"] <= a), None)
                    prior = None
                    if last and (last.get("end_ms") is None or last["end_ms"] > a
                                 or a - last["start_ms"] <= _RECOVERY_PRIOR_MS):
                        prior = last["reason"] or "unattributed"
                    segs = _split(a, b, [(ms, r) for ms, r in rec_ev if a < ms < b], prior)
            elif cat in ("walking", "pulling"):
                segs = _carve(a, b, overrides)
            else:
                segs = [(a, b, None)]
            for sa, sb, reason in segs:
                c = cat
                if cat in ("walking", "pulling") and reason is not None:
                    c = "waiting"
                if sb <= sa:
                    continue
                last = out[-1] if out else None
                if last and last["state"] == state and last["category"] == c and last["reason"] == reason \
                        and last["end_ms"] == sa:
                    last["end_ms"] = sb
                else:
                    out.append({"start_ms": sa, "end_ms": sb, "state": state, "category": c, "reason": reason})
        return out

    def collect_alerts(self):
        for e in self.dropped:
            self.alerts.append({"run_ms": None, "event_seq": None, "kind": "telemetry_dropped",
                                "text": f"{e['payload'].get('lines')} lines dropped at {e.get('wall_time')}",
                                "position": None})
        kinds = {"unexpected_teleport", "recovery_failed", "skip_stuck", "leader_unstuck", "strategy_restored",
                 "objective_failed", "wipe_giveup", "leadership_failed", "session_stalled", "walk_blocked"}
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
    dead_waits = b.dead_target_waits()
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
        "teleports": b.teleports(),
        "run_back": b.run_back(),
        "cc_marks": b.cc_marks(),
        "dead_target_waits": dead_waits,
        "time_spans": b.time_spans(recoveries, dead_waits),
    }
    model["metrics"] = metrics(model)
    return model


def metrics(model):
    pulls = model["pulls"]
    known_adds = [p["add_count"] for p in pulls if p["add_count"] is not None]
    out = {
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
    out.update(run_metrics.compute(model))
    return out


def _path_length(path):
    steps = (run_metrics.step_length(a, b) for a, b in zip(path, path[1:]))
    return sum((d for d in steps if d is not None), 0.0)
