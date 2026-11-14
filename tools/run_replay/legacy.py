"""The only place that reads v1 free-text `detail`.

For events that have no structured v2 payload yet. Each function returns what it could read and
None for the rest - never a guess. Retire a function once its event carries the fields itself.
"""
import re


def objective(detail):
    # "Razorclaw the Butcher requirement=boss why=door_closed round=2 action=abort"
    m = re.match(r"(?P<name>.*?) requirement=(?P<req>\S+) why=(?P<why>\S+) round=(?P<round>\d+) action=(?P<action>\S+)",
                 detail)
    if not m:
        return {"name": None, "requirement": None, "why": None, "round": None, "action": None}
    return {"name": m["name"], "requirement": m["req"], "why": m["why"], "round": int(m["round"]),
            "action": m["action"]}


def wipe_number(detail):
    m = re.search(r"wipe #(\d+)", detail)
    return int(m.group(1)) if m else None


def checkpoint(detail):
    # "from=5 to=5 checkpoint=Razorclaw the Butcher"
    m = re.match(r"from=(-?\d+) to=(-?\d+) checkpoint=(.*)", detail)
    if not m:
        return {"from_step": None, "to_step": None, "name": None}
    return {"from_step": int(m.group(1)), "to_step": int(m.group(2)), "name": m.group(3)}


def recovery(detail):
    # "party_fragmented member=Ogan relapse_after_ms=26230"
    m = re.match(r"(?P<reason>\S+)(?: member=(?P<member>\S+))?", detail)
    if not m:
        return {"reason": None, "member": None}
    member = m["member"]
    return {"reason": m["reason"], "member": None if member in (None, "-") else member}


def interaction_state(detail):
    # "door none->resolving target=Cell Door entry=18935 found=on_path dist=7"
    m = re.match(r"(?P<type>\S+) (?P<from>\w+)->(?P<to>\w+)(?P<rest>.*)", detail)
    if not m:
        return {"type": None, "from": None, "to": None}
    out = {"type": m["type"], "from": m["from"], "to": m["to"]}
    t = re.search(r"target=(.*?)(?= \w+=|$)", m["rest"])
    if t:
        out["target"] = t.group(1)
    e = re.search(r"entry=(\d+)", m["rest"])
    if e:
        out["entry"] = int(e.group(1))
    return out


def first_word_name(detail):
    """The leading name of details like "Defias Cannon key=5397" / "Cookie dist=406.2 ..."."""
    m = re.match(r"(.*?)(?= \w+=|$)", detail)
    return m.group(1) if m else detail


def readiness(detail):
    """The readiness text of a `waiting` detail: "group too spread - Osan" -> "group too spread"."""
    return detail.split(" - ", 1)[0].strip() or None


def post_combat_decision(detail):
    # "recovery readiness=someone is low on health lowest_health=31 healer_mana=8 waited_ms=4011"
    m = re.match(r"(?P<next>\S+) readiness=(?P<ready>.*?)(?= lowest_health=| healer_mana=| waited_ms=|$)", detail)
    if not m:
        return {"next": None, "readiness": None}
    return {"next": m["next"], "readiness": m["ready"]}


def pull_state(detail):
    # "#5 Slavering Worg approaching->waiting_party attempt=0 reason=group spread out, holding the pull (Osan)"
    # "#7 Slavering Worg approaching->waiting_patrol attempt=0 patrol=Shadowfang Wolfguard spawn=134516 dist=16"
    m = re.search(r"(?P<from>[a-z_]+)->(?P<to>[a-z_]+)(?: attempt=\d+)?(?P<rest>.*)", detail)
    if not m:
        return {"from": None, "to": None, "reason": None, "patrol": None}
    r = re.search(r" reason=(.*?)(?: \([^)]*\))?$", m["rest"])
    p = re.search(r" patrol=(.*?)(?= spawn=| dist=|$)", m["rest"])
    return {"from": m["from"], "to": m["to"], "reason": r.group(1) if r else None,
            "patrol": p.group(1) if p else None}


def escalation(detail):
    # "party_fragmented member=Ogan action=brought_to_leader +Izluun +Nobuan"
    m = re.match(r"(?P<reason>\S+) member=(?P<member>\S+)(?: action=(?P<action>\S+))?(?P<rest>.*)", detail)
    if not m:
        return {"reason": None, "member": None, "action": None, "others": []}
    return {"reason": m["reason"], "member": None if m["member"] == "-" else m["member"], "action": m["action"],
            "others": re.findall(r"\+(\S+)", m["rest"])}


def cc_release_reason(detail):
    """The reason of a `cc_released` detail ("Defias Pirate reason=last_enemy"), None if it names none."""
    m = re.search(r" reason=(\S+)", detail)
    return m.group(1) if m else None


def teleport_player(detail):
    """The player of an `unexpected_teleport` detail: "Ogan to map 0 (...) options=0 spell=8690 ..."."""
    m = re.match(r"(\S+) to map ", detail)
    return m.group(1) if m else None
