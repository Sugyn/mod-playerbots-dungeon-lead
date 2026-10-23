"""Tests for tools/run_replay (no worldserver): a synthetic schema v2 run covering a pull with adds,
a healer death and wipe with checkpoint restore, a recovery, a door interaction and a boss kill."""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tools.run_replay import analyze_run, campaign, model, report, schema  # noqa: E402

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

    def test_en_route_wipe_into_a_crowd(self):
        evs = [e for e in synthetic() if e["event_type"] != "pull_members_resolved"]
        m = model.reconstruct(evs)
        self.assertEqual(m["pulls"][0]["kind"], "en_route")
        (w,) = [f for f in analyze_run.failures(m)["findings"] if f["title"].startswith("Wipe #1")]
        # 3 units, 2 together and 1 two seconds later: small fight, low confidence
        self.assertEqual((w["classification"], w["confidence"]), ("WIPE", "LOW"))

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


class CompareTest(unittest.TestCase):
    def test_compare_flags_context(self):
        from tools.run_replay import compare_runs
        old = model.reconstruct(synthetic())
        evs = synthetic()
        for e in evs:
            e["build"] = dict(e["build"], commit_sha="def5678")
        new = model.reconstruct(evs)
        c = compare_runs.compare(old, new)
        self.assertTrue(c["directly_comparable"])  # only the commit differs
        self.assertIn("commit abc1234 vs def5678", c["differences"])
        for e in evs:
            e["scenario_id"] = "other"
        c = compare_runs.compare(old, model.reconstruct(evs))
        self.assertFalse(c["directly_comparable"])
        self.assertIn("NOT DIRECTLY COMPARABLE", compare_runs.comparison_md(c))


class GoldenTest(unittest.TestCase):
    """Real runs (tests/fixtures/run_reconstruction_v2): outputs match the goldens, and each case
    still shows what it was chosen for (plan acceptance cases A-F)."""
    EXPECT = {
        "A_clean_full_route": ("FULL_ROUTE", "clean", []),
        "B_wipe_and_checkpoint": ("FULL_ROUTE", "warning", ["PACK_IDENTITY"]),
        "C_unexpected_adds": ("PARTIAL", "unhealthy", ["PACK_IDENTITY", "ROUTE"]),
        "D_interaction_event": ("FULL_ROUTE", "clean", []),
        "E_objective_failure": ("PARTIAL", "unhealthy", ["INTERACTION"]),
        "F_record_ends_mid_run": ("UNKNOWN", "clean", []),
    }

    def test_goldens(self):
        from tools.run_replay import golden
        for case in golden.cases():
            d = os.path.join(golden.ROOT, case)
            for name, text in golden.outputs(d).items():
                with self.subTest(case=case, file=name):
                    self.assertEqual(golden.read(os.path.join(d, name)), text,
                                     "reconstruction changed - if intended: python3 -m tools.run_replay.golden --update")

    def test_cases_show_their_point(self):
        from tools.run_replay import golden
        self.assertEqual(sorted(golden.cases()), sorted(self.EXPECT))
        for case, (result, health, classes) in self.EXPECT.items():
            d = os.path.join(golden.ROOT, case)
            v = json.loads(golden.read(os.path.join(d, "verdict.json")))
            f = json.loads(golden.read(os.path.join(d, "failures.json")))
            with self.subTest(case=case):
                self.assertEqual((v["result"], v["health"]), (result, health))
                self.assertEqual([x["classification"] for x in f["findings"]], classes)
        m = json.loads(golden.read(os.path.join(golden.ROOT, "B_wipe_and_checkpoint", "run_model.json.gz")))
        self.assertTrue(m["wipes"] and m["wipes"][0]["checkpoint"], "case B restores a checkpoint")
        m = json.loads(golden.read(os.path.join(golden.ROOT, "D_interaction_event", "run_model.json.gz")))
        self.assertGreaterEqual(len(m["interactions"]), 5, "case D runs an event")


class StatusTest(unittest.TestCase):
    def test_verified_needs_two_clean_full_routes_on_one_commit(self):
        from tools.run_replay import status
        r = lambda rid, build, result="FULL_ROUTE", health="clean", t="2026-10-06T10": {
            "run_id": rid, "build": build, "profile": "p", "result": result, "health": health, "started_at": t}
        self.assertIsNone(status.verified([r("1", "a"), r("2", "b")]), "two commits are not a pair")
        self.assertIsNone(status.verified([r("1", "a"), r("2", "a", health="unhealthy")]), "unhealthy does not count")
        self.assertIsNone(status.verified([r("1", "a"), r("2", "a", result="PARTIAL")]))
        self.assertEqual(status.verified([r("1", "a"), r("2", "a", health="warning")]), ("a", ["1", "2"], "pre-context"))
        newer = status.verified([r("1", "a"), r("2", "a"), r("3", "b", t="2026-10-07"), r("4", "b", t="2026-10-07")])
        self.assertEqual(newer[0], "b", "the newest verified commit wins")


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

    def test_rejects_duplicate_event_seq(self):
        # DL-010: a producer bug (or a stale Stop()/Stopping reconstruction restarting the
        # counter) must be flagged, not silently resorted into a plausible-looking timeline.
        evs = synthetic()
        dup = dict(evs[1], event_seq=evs[0]["event_seq"])  # same seq as evs[0], different content
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
            for e in [evs[0], dup]:
                f.write(json.dumps(e) + "\n")
        try:
            with self.assertRaises(schema.SchemaError):
                schema.load_events(f.name, RUN)
        finally:
            os.unlink(f.name)

    def test_rejects_run_ms_going_backwards(self):
        # DL-010: run_ms must never decrease within a run even though event_seq always advances -
        # a lost sessionStartTs (the same Stopping-reconstruction bug) would otherwise read as a
        # legitimate clock reset instead of the lineage break it actually is.
        evs = synthetic()
        later = dict(evs[1], event_seq=evs[1]["event_seq"] + 1000, run_ms=0)  # evs[1].run_ms == 100
        with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as f:
            for e in [evs[1], later]:
                f.write(json.dumps(e) + "\n")
        try:
            with self.assertRaises(schema.SchemaError):
                schema.load_events(f.name, RUN)
        finally:
            os.unlink(f.name)



class DeltaEventsTest(unittest.TestCase):
    """DLN-003: events added through 1e2cf6e and the DLN fixes are carried or ignored safely - the
    reconstruction of the same run does not change, and heartbeat records are not campaigns."""

    def with_new_events(self):
        evs = synthetic()
        extra = [
            ("dead_member_state", {"detail": "Heal ghost=1 map=33 dist=40"}),
            ("checkpoint_walk_back", {"detail": "step 5 -> 0, resume 630 yd away"}),
            ("use_needs_player", {"detail": "Troll Cage key=8444 observable=1"}),
            ("door_needs_player", {"detail": "Cell Door key=1"}),
            ("leadership_request_rejected", {"detail": "leader_changed generation=7"}),
            ("pause", {"detail": "travelling"}),
            ("continue", {"detail": "waiting_ready"}),
            ("reset_rejected", {"detail": "run finished outcome=complete"}),
        ]
        last = evs[-1]
        out = list(evs[:-1])
        seq = max(e["event_seq"] for e in evs)
        for i, (etype, payload) in enumerate(extra):
            seq += 1
            out.append(dict(evs[1], event_seq=seq, event_type=etype, run_ms=evs[1]["run_ms"] + i, payload=payload))
        out.append(dict(last, event_seq=seq + 1))
        return out

    def test_reconstruction_unchanged(self):
        base = model.reconstruct(synthetic())
        new = model.reconstruct(self.with_new_events())
        for key in ("pulls", "wipes", "recoveries", "interactions", "bosses", "result"):
            self.assertEqual(json.dumps(base[key], sort_keys=True), json.dumps(new[key], sort_keys=True), key)
        self.assertEqual(base["metrics"]["wipes"], new["metrics"]["wipes"])

    def test_heartbeat_is_not_a_campaign(self):
        with tempfile.TemporaryDirectory() as d:
            ev_path = os.path.join(d, "events.jsonl")
            with open(ev_path, "w") as f:
                for e in synthetic():
                    f.write(json.dumps(e, separators=(",", ":")) + "\n")
            camp_path = os.path.join(d, "campaigns.jsonl")
            hb = {"schema_version": 2, "record": "heartbeat", "time": "2026-10-23 02:10:38",
                  "active_sessions": 2, "queued_lines": 1}
            recs = [hb,
                    {"schema_version": 2, "record": "campaign_started", "campaign_id": "verify-test",
                     "commit_sha": "abc1234", "dungeons": [8]},
                    hb,
                    {"schema_version": 2, "record": "run_result", "campaign_id": "verify-test", "lfg_id": 8,
                     "tank": "Tank", "started": True, "ended": True, "minutes": 1},
                    hb,
                    {"schema_version": 2, "record": "campaign_finished", "campaign_id": "verify-test", "runs": 1}]
            with open(camp_path, "w") as f:
                for r in recs:
                    f.write(json.dumps(r) + "\n")
            campaign.build(ev_path, camp_path, "verify-test", d)
            with open(os.path.join(d, "campaigns", "verify-test", "campaign.json")) as f:
                written = json.load(f)
            self.assertEqual([r["record"] for r in written], ["campaign_started", "run_result", "campaign_finished"])



class ContextTest(unittest.TestCase):
    """DLN-022: the run context in the start event reaches the model; older runs have none."""

    def test_context_from_start_event(self):
        evs = synthetic()
        ctx = {"context_id": "0123456789abcdef", "module_sha": "abc1234", "core_sha": "f19a18799a35",
               "playerbots_sha": "037c01418b5d", "route_digest": "1111111111111111", "route_rows": 497,
               "config_digest": "2222222222222222", "config_keys": 15, "mmap_digest": "3333333333333333",
               "mmap_files": 3780, "incomplete": False}
        start = next(e for e in evs if e["event_type"] == "start")
        start["payload"] = dict(start["payload"], context=ctx)
        md = model.reconstruct(evs)["metadata"]
        self.assertEqual(md["context"], ctx)

    def test_no_context_before_it_existed(self):
        self.assertIsNone(model.reconstruct(synthetic())["metadata"]["context"])


class EligibilityTest(unittest.TestCase):
    """DLN-024: Verified needs two distinct complete runs on one complete run context."""

    @staticmethod
    def r(rid, ctx="c1", build="m1", incomplete=False, result="FULL_ROUTE", health="clean",
          t="2026-10-23T10", complete=True):
        return {"run_id": rid, "build": build, "profile": "p", "result": result, "health": health,
                "started_at": t, "context_id": ctx, "context_incomplete": incomplete, "complete_record": complete}

    def test_one_run_in_two_campaigns_is_one_run(self):
        from tools.run_replay import status
        self.assertIsNone(status.verified([self.r("1"), dict(self.r("1"), campaign="other")]))

    def test_two_distinct_runs_same_context(self):
        from tools.run_replay import status
        self.assertEqual(status.verified([self.r("1"), self.r("2")]), ("m1", ["1", "2"], "context"))

    def test_incomplete_or_mixed_context_does_not_qualify(self):
        from tools.run_replay import status
        self.assertIsNone(status.verified([self.r("1", incomplete=True), self.r("2", incomplete=True)]),
                          "unknown part / dirty module")
        self.assertIsNone(status.verified([self.r("1", ctx="c1"), self.r("2", ctx="c2")]),
                          "same module, different route/config/stack")
        self.assertIsNone(status.verified([self.r("1"), self.r("2", complete=False)]), "a record starting mid-run")

    def test_context_pair_wins_over_pre_context(self):
        from tools.run_replay import status
        old = [dict(self.r("1", t="2026-10-24"), context_id=None), dict(self.r("2", t="2026-10-24"), context_id=None)]
        new = [self.r("3", t="2026-10-23"), self.r("4", t="2026-10-23")]
        self.assertEqual(status.verified(old + new)[2], "context")
        self.assertEqual(status.verified(old)[2], "pre-context")

    def test_compare_flags_context_parts(self):
        from tools.run_replay import compare_runs
        a = model.reconstruct(synthetic())
        b = model.reconstruct(synthetic())
        ctx = {"core_sha": "c", "playerbots_sha": "p", "route_digest": "r1", "config_digest": "k", "mmap_digest": "m"}
        a["metadata"]["context"] = dict(ctx)
        b["metadata"]["context"] = dict(ctx)
        self.assertTrue(compare_runs.compare(a, b)["directly_comparable"])
        b["metadata"]["context"]["route_digest"] = "r2"
        c = compare_runs.compare(a, b)
        self.assertFalse(c["directly_comparable"])
        self.assertTrue(any("route_digest" in d for d in c["differences"]))
        b["metadata"]["context"] = None
        c = compare_runs.compare(a, b)
        self.assertTrue(c["directly_comparable"], "missing context is reported, not blocking")
        self.assertTrue(any("run context unknown for new" in d for d in c["differences"]))

if __name__ == "__main__":
    unittest.main()
