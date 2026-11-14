"""Run metrics beyond the counts: dead-target waits, teleports, crowd control, wipe-to-resume, where
the time went and how much of the walk went backwards.

Read from the run model only (model.py builds the sections they come from). Each metric names the
events it is derived from in `source` and is None when the run has no such evidence - never a
guessed 0. A 0 is a measured 0.
"""
import bisect
import statistics

METRICS_VERSION = 3  # 2: dead_target_waits, teleports, cc, wipe_to_resume_s, time_split, backtrack_ratio
# 3: member teleports (member, fallback_reasons), run_back

# Classes that can cast one of the spells the module plans crowd control with (kCcSpells in
# DungeonTargetManager.cpp: Polymorph, Banish / Fear, Hibernate / Entangling Roots / Cyclone,
# Freezing Trap / Scare Beast). By class only: spell rank, talents and the target's creature type
# are not checked.
CC_CLASSES = {3: "hunter", 8: "mage", 9: "warlock", 11: "druid"}

# A dead-target episode shorter than this is the normal latency of moving on (the next guard
# pass), not a wait: it adds to the seconds, not to the count.
DEAD_TARGET_MIN_MS = 1000

TELEPORT_KINDS = ("leader_unstuck", "leader_to_floor", "member_to_leader", "wipe_recovery", "unexpected")
LEADER_TELEPORT_KINDS = ("leader_unstuck", "leader_to_floor")
# Party bots moved: brought to the leader, or put back at the entrance after a death (from
# member_teleport, with its reason, where the run records it).
MEMBER_TELEPORT_KINDS = ("member_to_leader", "wipe_recovery")

TIME_CATEGORIES = ("walking", "pulling", "combat", "boss_prep", "waiting", "recovery", "other")
WAIT_REASONS = ("healer_mana", "party_range", "eating_drinking", "dead_member", "patrol", "low_health",
                "post_combat_check", "dead_target", "other", "unattributed")


def _s(ms):
    return round(ms / 1000.0, 1)


def step_length(a, b):
    """Walked distance between two position samples; None when unknown or a jump (teleport, corpse
    run - not walked)."""
    if None in (a["x"], a["y"], b["x"], b["y"]):
        return None
    d = ((a["x"] - b["x"]) ** 2 + (a["y"] - b["y"]) ** 2 + ((a["z"] or 0) - (b["z"] or 0)) ** 2) ** 0.5
    return d if d < 100 else None


def dead_target_waits(m):
    eps = m.get("dead_target_waits")
    if eps is None:
        return None
    ms = [e["duration_ms"] for e in eps]
    counted = [e for e in eps if e["duration_ms"] >= DEAD_TARGET_MIN_MS]
    return {"source": "pack_state, reached, not_found, already_dead, objective_skipped, objective_failed, "
                      "skip_stuck, step_walk_limit",
            "count": len(counted),
            "found_dead": sum(1 for e in counted if e["kind"] == "found_dead"),
            "not_found": sum(1 for e in counted if e["kind"] == "not_found"),
            "total_s": _s(sum(ms)),
            "max_s": _s(max(ms)) if ms else 0.0}


def teleports(m):
    tps = m.get("teleports")
    if tps is None:
        return None
    out = {"source": "leader_unstuck, leader_to_floor, recovery_escalate (action=brought_to_leader), "
                     "wipe_recovery_teleport, member_teleport, unexpected_teleport (cancelled)"}
    for kind in TELEPORT_KINDS:
        moved = [t for t in tps if t["kind"] == kind]
        # players moved; a recorded escalation that does not say whom it moved makes the count unknown
        out[kind] = None if any(t["who"] is None for t in moved) else sum(len(t["who"]) for t in moved)
    out["leader"] = sum(out[k] for k in LEADER_TELEPORT_KINDS)
    out["member"] = None if any(out[k] is None for k in MEMBER_TELEPORT_KINDS) else sum(out[k] for k in MEMBER_TELEPORT_KINDS)
    out["fallback_reasons"] = {}
    for t in tps:
        if t.get("source") == "payload" and t["kind"] in MEMBER_TELEPORT_KINDS:
            r = t.get("reason") or "unknown"
            out["fallback_reasons"][r] = out["fallback_reasons"].get(r, 0) + 1
    out["total"] = None if any(out[k] is None for k in TELEPORT_KINDS) else sum(out[k] for k in TELEPORT_KINDS)
    return out


def run_back(m):
    """Deaths brought back in place, ghosts that walked to the portal, walks to the party and
    their fallbacks; None for a run recorded before the run back."""
    rb = m.get("run_back")
    if rb is None:
        return None
    walked = [x for x in rb if x["phase"] == "at_entrance" and x.get("how") == "portal"]
    rejoined = [x for x in rb if x["phase"] == "rejoined"]
    return {"source": "run_back",
            "held": sum(1 for x in rb if x["phase"] == "held"),
            "resurrected": sum(1 for x in rb if x["phase"] == "revived" and x.get("how") == "resurrected"),
            "released": sum(1 for x in rb if x["phase"] == "released"),
            "walked_to_portal": len(walked),
            "walk_orders": sum(1 for x in rb if x["phase"] == "walk_order"),
            "rejoined_walking": sum(1 for x in rejoined if x.get("how") == "walk"),
            "fallbacks": sum(1 for x in rb if x["phase"] == "fallback"),
            "back_ms_max": max((x["ms"] or 0 for x in rejoined if not x["straggler"]), default=None)}


def party_cc_classes(m):
    members = (m.get("party") or {}).get("members") or []
    if not members or any("class" not in p for p in members):
        return None
    return sorted({CC_CLASSES[p["class"]] for p in members if p.get("class") in CC_CLASSES})


def cc(m):
    marks = m.get("cc_marks")
    if marks is None:
        return None
    n = {o: sum(1 for x in marks if x["outcome"] == o)
         for o in ("landed", "released", "target_gone", "replaced", "unresolved")}
    classes = party_cc_classes(m)
    return {"source": "mark_moon, cc_landed, cc_released, cc_target_gone, cc_outcome, party_roster",
            "moons": len(marks),
            "landed": n["landed"],
            "released_without_cc": n["released"],
            "released_last_enemy": sum(1 for x in marks if x["outcome"] == "released"
                                       and x["release_reason"] == "last_enemy"),
            "target_gone": n["target_gone"],
            "replaced": n["replaced"],
            "unresolved": n["unresolved"],
            # landed / moons placed
            "landed_ratio": round(n["landed"] / len(marks), 3) if marks else None,
            "landed_by": landed_by(m, marks),
            "party_cc_classes": classes,
            "moons_without_cc_class": (len(marks) if classes == [] else 0) if classes is not None else None}


def landed_by(m, marks):
    """Who landed the CC and with which spell, from cc_outcome: [{caster, spell, spell_id, moons}].
    None when no moon has a cc_outcome (a run recorded before it existed, or no moon placed)."""
    if not any(x.get("cc_outcome") for x in marks):
        return None
    out = {}
    for x in marks:
        if x["outcome"] == "landed":
            by = x["by"] or {}
            key = (by.get("caster"), by.get("spell"), by.get("spell_id"))
            out[key] = out.get(key, 0) + 1
    return [{"caster": c, "spell": s, "spell_id": i, "moons": n}
            for (c, s, i), n in sorted(out.items(), key=lambda kv: (-kv[1], str(kv[0])))]


def wipe_to_resume(m):
    if m.get("time_spans") is None:
        return None  # no state transitions: "travelling again" is not observable
    per = [None if w.get("resumed_ms") is None else _s(w["resumed_ms"] - w["run_ms"]) for w in m["wipes"]]
    known = [x for x in per if x is not None]
    return {"source": "wipe_detected, state_transition",
            "wipes": per,
            "median_s": round(statistics.median(known), 1) if known else None,
            "max_s": max(known) if known else None,
            "unresolved": len(per) - len(known)}


def time_split(m):
    spans = m.get("time_spans")
    if spans is None:
        return None
    cat = dict.fromkeys(TIME_CATEGORIES, 0)
    wait = dict.fromkeys(WAIT_REASONS, 0)
    rec = {}
    for s in spans:
        d = s["end_ms"] - s["start_ms"]
        cat[s["category"]] += d
        if s["category"] == "waiting":
            wait[s["reason"] if s["reason"] in wait else "other"] += d
        elif s["category"] == "recovery":
            rec[s["reason"]] = rec.get(s["reason"], 0) + d
    out = {"source": "state_transition, waiting, post_combat_decision, pull_state, recovery_start, "
                     "dead_target_waits",
           "total_s": _s(sum(cat.values()))}
    out.update({f"{k}_s": _s(v) for k, v in cat.items()})
    out["waiting_by_reason_s"] = {k: _s(v) for k, v in wait.items()}
    out["recovery_by_reason_s"] = {k: _s(v) for k, v in sorted(rec.items())}
    return out


# A sample pair counts as backwards when the way left to the destination grew by more than this
# (the recorded path is thinned to 16 points, so its chords are a little off the walked line).
BACKTRACK_TOLERANCE_YD = 1.0


def backtrack(m):
    """Walked distance (travelling, out of combat) that took the leader farther from the
    destination of its current step, as a share of all such walking.

    "Farther" is measured along the navmesh path of the step's last path_decision - not in a
    straight line, which would count every bend of a ramp or a winding cave as backwards: the way
    left from a position is its distance to the nearest point of that path, plus the path from
    there to its end, plus the end to the destination. Walking with no recorded path for the step
    is `unmeasured_yd`. None without recorded paths."""
    decisions = {}
    for d in m.get("path_decisions") or []:
        if d["dest"]["x"] is not None and len(d.get("path") or []) >= 2:
            decisions.setdefault(d["step"], []).append(d)
    if not decisions:
        return None
    times = {k: [d["run_ms"] for d in v] for k, v in decisions.items()}
    walked = away = unmeasured = 0.0
    path = m.get("path") or []
    for a, b in zip(path, path[1:]):
        if a["state"] != "travelling" or b["state"] != "travelling" or a.get("in_combat") or b.get("in_combat"):
            continue
        d = step_length(a, b)
        if not d:
            continue
        i = bisect.bisect_right(times.get(b["step"], []), b["run_ms"]) - 1
        if i < 0:
            unmeasured += d
            continue
        dec = decisions[b["step"]][i]
        walked += d
        if _way_left(b, dec) > _way_left(a, dec) + BACKTRACK_TOLERANCE_YD:
            away += d
    if walked == 0:
        return None
    return {"source": "position_sample, path_decision",
            "walked_yd": round(walked, 1), "away_yd": round(away, 1), "ratio": round(away / walked, 3),
            "unmeasured_yd": round(unmeasured, 1)}


def _d3(p, q):
    return ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2) ** 0.5


def _way_left(p, dec):
    pts = [tuple(x) for x in dec["path"]]
    pos = (p["x"], p["y"], p["z"] or 0)
    dest = (dec["dest"]["x"], dec["dest"]["y"], dec["dest"]["z"] or 0)
    seg = [_d3(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    suffix = [0.0] * len(pts)
    for i in range(len(pts) - 2, -1, -1):
        suffix[i] = suffix[i + 1] + seg[i]
    best = None  # (offset, way left) at the nearest point of the path
    for i in range(len(pts) - 1):
        a, b, length = pts[i], pts[i + 1], seg[i]
        t = 0.0
        if length > 0:
            t = sum((pos[k] - a[k]) * (b[k] - a[k]) for k in range(3)) / (length * length)
            t = min(1.0, max(0.0, t))
        q = tuple(a[k] + t * (b[k] - a[k]) for k in range(3))
        off = _d3(pos, q)
        if best is None or off < best[0]:
            best = (off, off + (1 - t) * length + suffix[i + 1])
    return best[1] + _d3(pts[-1], dest)


def compute(m):
    return {
        "metrics_version": METRICS_VERSION,
        "dead_target_waits": dead_target_waits(m),
        "teleports": teleports(m),
        "run_back": run_back(m),
        "cc": cc(m),
        "wipe_to_resume_s": wipe_to_resume(m),
        "time_split": time_split(m),
        "backtrack_ratio": backtrack(m),
    }
