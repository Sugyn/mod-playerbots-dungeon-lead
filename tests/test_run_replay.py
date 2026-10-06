"""Tests for tools/run_replay (no worldserver): a synthetic schema v2 run covering a pull with adds,
a healer death and wipe with checkpoint restore, a recovery, a door interaction and a boss kill."""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tools.run_replay import analyze_run, model, report, schema  # noqa: E402

RUN = "42"


class Run:
    def __init__(self):
        self.seq = 0
        self.events = []

    def ev(self, ms, etype, payload=None, state="travelling", step=0, pack_id=0, x=0.0, y=0.0, outcome="running"):
        self.seq += 1
        self.events.append({
            "schema_version": 2, "event_seq": self.seq, "event_type": etype, "run_id": RUN,
            "campaign_id": "verify-test", "scenario_id": "lfg8-test", "wall_time": f"t{ms}", "run_ms": ms,
            "build": {"commit_sha": "abc1234", "module_version": "0.13.0-alpha"},
            "dungeon": {"lfg_id": 8, "name": "Shadowfang Keep", "map_id": 33, "instance_id": 1},
            "leader": {"guid": "1", "name": "Tank", "x": x, "y": y, "z": 0.0, "o": 0.0, "alive": True},
            "state": state, "step": step, "pack_id": pack_id,
            "outcome": {"outcome": outcome, "failure_domain": "none", "failure_reason": "none"},
            "payload": payload or {},
        })


def unit(guid, name, x=10.0, y=10.0, entry=100):
    return {"guid": guid, "entry": entry, "name": name, "x": x, "y": y, "z": 0.0}


def synthetic():
    r = Run()
    r.ev(0, "start", {"detail": "tank=yes origin=auto_canary"})
    r.ev(100, "party_roster", {"members": [{"name": "Tank", "role": "tank"}, {"name": "Heal", "role": "healer"}]})
    r.ev(1000, "position_sample", {"moving": True, "in_combat": False}, x=0, y=0)
    r.ev(3000, "position_sample", {"moving": True, "in_combat": False}, x=10, y=0)
    # pull of pack 2: two expected members, one add joins
    r.ev(5000, "fight_started", {"fight_id": 1}, state="combat", pack_id=2)
    r.ev(5000, "combat_anchor_set", {"kind": "fight_start", "x": 10.0, "y": 0.0, "z": 0.0, "radius": 30}, state="combat")
    r.ev(5100, "pull_members_resolved", {"pack_id": 2, "objective": "Courtyard pack", "requirement": "optional",
                                         "boss": False, "core_members": [unit("a", "Worg"), unit("b", "Worg")]},
         state="combat", pack_id=2)
    for g in ("a", "b"):
        r.ev(5200, "combat_unit_joined", {"fight_id": 1, "unit": unit(g, "Worg"), "pack_id": 2, "in_pack": True,
                                          "victim": "Tank"}, state="combat", pack_id=2)
    r.ev(7000, "combat_unit_joined", {"fight_id": 1, "unit": unit("c", "Moonwalker", entry=200), "pack_id": 2,
                                      "in_pack": False, "victim": "Heal"}, state="combat", pack_id=2)
    r.ev(9000, "member_died", {"name": "Heal", "role": "healer", "fight_id": 1, "x": 12.0, "y": 1.0, "z": 0.0},
         state="combat")
    r.ev(9500, "mob_died", {"fight_id": 1, "unit": unit("a", "Worg")}, state="combat")
    r.ev(12000, "wipe_detected", {"detail": "wipe #1"}, state="wipe_recovery")
    r.ev(13000, "fight_ended", {"fight_id": 1, "units": 3, "killed": 1, "leader_alive": False}, state="wipe_recovery")
    r.ev(20000, "wipe_recovered", {"detail": "wipe #1"}, state="wipe_recovery")
    r.ev(20000, "checkpoint_restore", {"detail": "from=1 to=1 checkpoint=Rethilgore"})
    # recovery and a door
    r.ev(22000, "recovery_start", {"detail": "party_fragmented member=Heal"}, state="recovery")
    r.ev(25000, "recovery_complete", {"detail": "party_fragmented ms=3000"}, state="recovery")
    r.ev(26000, "interaction_state", {"detail": "door none->resolving target=Cell Door entry=18935 found=on_path dist=7"})
    r.ev(26500, "interaction_state", {"detail": "door resolving->waiting_prerequisite ms=500"})
    r.ev(30000, "interaction_state", {"detail": "door waiting_prerequisite->complete ms=4000"})
    # boss engaged and killed
    r.ev(40000, "pack_state", {"pack_id": 5, "name": "Razorclaw", "type": "boss", "boss": True, "optional": False,
                               "from": "available", "to": "engaged"}, state="boss_combat", step=4, pack_id=5)
    r.ev(52000, "pack_state", {"pack_id": 5, "name": "Razorclaw", "type": "boss", "boss": True, "optional": False,
                               "from": "engaged", "to": "cleared"}, state="boss_combat", step=4, pack_id=5)
    r.ev(60000, "route_complete", {"detail": "0 skipped"}, outcome="complete")
    return r.events


class ReconstructTest(unittest.TestCase):
    def setUp(self):
        self.m = model.reconstruct(synthetic())

    def test_metadata_and_party(self):
        md = self.m["metadata"]
        self.assertEqual(md["run_id"], RUN)
        self.assertEqual(md["build"]["commit_sha"], "abc1234")
        self.assertTrue(md["complete_record"])
        self.assertEqual(len(self.m["party"]["members"]), 2)

    def test_pull_with_add_and_wipe(self):
        (f,) = self.m["pulls"]
        self.assertEqual(f["objective"], "Courtyard pack")
        self.assertEqual(f["expected_count"], 2)
        self.assertEqual(f["engaged_count"], 3)
        self.assertEqual(f["add_count"], 1)
        self.assertEqual(f["unexpected_adds"][0]["name"], "Moonwalker")
        self.assertEqual(f["result"], "wiped")
        self.assertEqual(f["combat_anchor"]["x"], 10.0)
        self.assertEqual([d["name"] for d in f["deaths"]], ["Heal"])

    def test_wipe(self):
        (w,) = self.m["wipes"]
        self.assertEqual(w["wipe"], 1)
        self.assertEqual(w["first_death"], "Heal")
        self.assertEqual(w["recovered_ms"], 20000)
        self.assertEqual(w["checkpoint"]["name"], "Rethilgore")

    def test_recovery_interaction_boss(self):
        (r,) = self.m["recoveries"]
        self.assertEqual((r["reason"], r["member"], r["result"], r["duration_ms"]), ("party_fragmented", "Heal", "complete", 3000))
        (i,) = self.m["interactions"]
        self.assertEqual((i["type"], i["target"], i["entry"], i["result"]), ("door", "Cell Door", 18935, "complete"))
        (b,) = self.m["bosses"]
        self.assertEqual((b["name"], b["result"], b["duration_ms"]), ("Razorclaw", "killed", 12000))

    def test_metrics_and_result(self):
        mt = self.m["metrics"]
        self.assertEqual((mt["fights"], mt["unexpected_adds"], mt["deaths"], mt["wipes"], mt["bosses_killed"]),
                         (1, 1, 1, 1, 1))
        self.assertEqual(mt["path_length_yd"], 10.0)
        self.assertEqual(self.m["result"]["outcome"], "complete")
        self.assertTrue(self.m["route"]["complete"])

    def test_deterministic(self):
        a = json.dumps(model.reconstruct(synthetic()), sort_keys=True)
        b = json.dumps(model.reconstruct(synthetic()), sort_keys=True)
        self.assertEqual(a, b)


class AnalyzeTest(unittest.TestCase):
    def test_wipe_with_adds_is_classified_from_facts(self):
        m = model.reconstruct(synthetic())
        found = analyze_run.failures(m)["findings"]
        (w,) = [f for f in found if f["title"].startswith("Wipe #1")]
        # 1 add vs 2 expected: adds noted, but not enough to name them the cause
        self.assertEqual((w["classification"], w["confidence"]), ("WIPE", "LOW"))
        self.assertIn("F1", w["evidence"])
        self.assertTrue(any("engaged 3" in x for x in w["facts"]))

    def test_verdict_full_route_with_wipe_warning(self):
        v = analyze_run.verdict(model.reconstruct(synthetic()))
        self.assertEqual((v["result"], v["health"]), ("FULL_ROUTE", "warning"))
        self.assertEqual(v["commit_sha"], "abc1234")

    def test_campaign_stopped_run(self):
        evs = [e for e in synthetic() if e["event_type"] != "route_complete"]
        m = model.reconstruct(evs)
        cres = {"record": "run_result", "campaign_id": "verify-test", "started": True, "ended": False, "minutes": 75}
        v = analyze_run.verdict(m, cres)
        self.assertEqual((v["result"], v["health"]), ("UNKNOWN", "unhealthy"))
        self.assertIn("campaign_stopped_live_run", v["unhealthy"])
        (c,) = [f for f in analyze_run.failures(m, cres)["findings"] if f["classification"] == "CAMPAIGN"]
        self.assertEqual(c["confidence"], "HIGH")

    def test_report_renders(self):
        m = model.reconstruct(synthetic())
        md = report.timeline_md(m)
        self.assertIn("WIPE #1", md)
        self.assertIn("Moonwalker", md)
        svg = report.map_svg(m, planned=[])
        self.assertTrue(svg.startswith("<svg") and svg.rstrip().endswith("</svg>"))
        self.assertIn("F1", svg)


class SchemaTest(unittest.TestCase):
    def test_rejects_unknown_version(self):
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
            f.write(json.dumps({"schema_version": 3, "event_type": "x", "run_id": "1", "event_seq": 1}) + "\n")
        try:
            with self.assertRaises(schema.SchemaError):
                schema.load_events(f.name)
        finally:
            os.unlink(f.name)

    def test_orders_by_event_seq_and_filters_run(self):
        evs = synthetic()
        other = dict(evs[0], run_id="7")
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
            for e in [evs[3], other, evs[1], evs[0], evs[2]]:
                f.write(json.dumps(e) + "\n")
        try:
            got, dropped = schema.load_events(f.name, RUN)
            self.assertEqual([e["event_seq"] for e in got], [1, 2, 3, 4])
            self.assertEqual(dropped, [])
        finally:
            os.unlink(f.name)


if __name__ == "__main__":
    unittest.main()
