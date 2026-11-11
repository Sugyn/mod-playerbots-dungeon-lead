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
