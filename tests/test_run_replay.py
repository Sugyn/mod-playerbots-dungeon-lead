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

    def test_campaign_result_repeated_dungeon(self):
        from tools.run_replay.cli import campaign_result
        m = {"metadata": {"campaign_id": "verify-test", "dungeon": {"lfg_id": 8}, "leader": "Tank",
                          "started_at": "2026-01-01T02:01:10.500Z"}}
        first = {"record": "run_result", "campaign_id": "verify-test", "lfg_id": 8, "tank": "Tank", "pass": 1,
                 "run_started_at": "2026-01-01T01:00:00Z", "ended": True}
        second = dict(first, **{"pass": 2, "run_started_at": "2026-01-01T02:01:00Z", "ended": False})
        other = dict(first, lfg_id=4)
        self.assertIs(campaign_result(m, [other, first, second]), second)
        self.assertIs(campaign_result(m, [other, first]), first)
        old = {k: v for k, v in first.items() if k != "run_started_at"}
        self.assertIs(campaign_result(m, [old, dict(old, ended=False)]), old)
    def test_absent_optional_is_not_a_failure(self):
        evs = synthetic()
        r = Run()
        r.seq = len(evs)
        r.events = evs
        r.ev(35000, "objective_absent", {"name": "Rare", "step": 3, "entry": 99, "spots": 1, "detail": "Rare"}, step=3)
        r.events.sort(key=lambda e: e["run_ms"])
        m = model.reconstruct(r.events)
        (st,) = [s for s in m["route"]["step_events"] if s["step"] == 3]
        self.assertEqual([(x["event"], x["why"], x["action"]) for x in st["events"]], [("objective_absent", "absent", "pass")])
        v = analyze_run.verdict(m)
        self.assertEqual(v["result"], analyze_run.verdict(model.reconstruct(synthetic()))["result"])
        self.assertFalse([f for f in analyze_run.failures(m)["findings"] if "Rare" in json.dumps(f)])

    def test_leader_disconnected_is_a_terminal_event(self):
        r = Run()
        r.ev(0, "start", {"detail": "tank=yes origin=auto_canary"})
        r.ev(5000, "position_sample", {"moving": True, "in_combat": False}, x=10, y=0, step=2)
        r.ev(9000, "leader_disconnected", {"how": "hard_disconnect", "last_state": "combat", "last_step": 2},
             state="combat", step=2, outcome="failed")
        m = model.reconstruct(r.events)
        self.assertEqual(m["result"]["terminal_event"], "leader_disconnected")
        v = analyze_run.verdict(m)
        self.assertNotEqual(v["result"], "UNKNOWN")
        self.assertIn("leader_disconnected", v["unhealthy"])

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
    # A, D and F were clean before the run-metric warnings: their only warnings are those (a CC
    # landed ratio under 0.5 in all three, plus the 45 s wait on the absent Velratha in D)
    EXPECT = {
        "A_clean_full_route": ("FULL_ROUTE", "warning", []),
        "B_wipe_and_checkpoint": ("FULL_ROUTE", "warning", ["PACK_IDENTITY"]),
        "C_unexpected_adds": ("PARTIAL", "unhealthy", ["PACK_IDENTITY", "ROUTE"]),
        "D_interaction_event": ("FULL_ROUTE", "warning", []),
        "E_objective_failure": ("PARTIAL", "unhealthy", ["INTERACTION"]),
        "F_record_ends_mid_run": ("UNKNOWN", "warning", []),
    }
    METRIC_WARNINGS = {
        "A_clean_full_route": ["cc_landed_ratio=0.267"],
        "D_interaction_event": ["dead_target_wait_s=45.2", "cc_landed_ratio=0.25"],
        "F_record_ends_mid_run": ["cc_landed_ratio=0.143"],
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
                if case in self.METRIC_WARNINGS:
                    self.assertEqual(v["warnings"], self.METRIC_WARNINGS[case])
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
        # A producer bug (e.g. a Stop()/Stopping reconstruction restarting the
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
        # run_ms must never decrease within a run even though event_seq always advances - a lost
        # sessionStartTs would otherwise read as a legitimate clock reset instead of the lineage
        # break it actually is.
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
    """Newer event types are carried or ignored safely - the
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
    """The run context in the start event reaches the model; older runs have none."""

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
    """Verified needs two distinct complete runs on one complete run context."""

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


class ScopeTest(unittest.TestCase):
    """FULL_ROUTE keeps its meaning; whether it is the whole dungeon is said separately."""

    def with_scope(self, scope, unmodeled):
        evs = synthetic()
        evs.insert(1, dict(evs[0], event_seq=evs[0]["event_seq"], event_type="route_selected",
                           payload={"detail": "12 stops", "scope": scope, "unmodeled": unmodeled}))
        for i, e in enumerate(evs):
            e["event_seq"] = i + 1
        return model.reconstruct(evs)

    def test_partial_model(self):
        m = self.with_scope("partial_model", ["The Oculus"])
        self.assertEqual(m["metadata"]["route_scope"], {"scope": "partial_model", "unmodeled": ["The Oculus"]})
        v = analyze_run.verdict(m)
        self.assertEqual((v["result"], v["dungeon_support"], v["unmodeled"]),
                         ("FULL_ROUTE", "modeled_route_only", ["The Oculus"]))

    def test_full_scope(self):
        v = analyze_run.verdict(self.with_scope("full", []))
        self.assertEqual((v["result"], v["dungeon_support"]), ("FULL_ROUTE", "dungeon_modeled_in_full"))

    def test_before_scope_existed(self):
        v = analyze_run.verdict(model.reconstruct(synthetic()))
        self.assertEqual((v["result"], v["dungeon_support"]), ("FULL_ROUTE", "unknown"))

    def test_status_says_modeled_route_only(self):
        from tools.run_replay import status
        rs = [{"run_id": i, "build": "m", "profile": "p", "result": "FULL_ROUTE", "health": "clean",
               "started_at": "t", "context_id": "c", "context_incomplete": False, "complete_record": True,
               "dungeon_support": "modeled_route_only"} for i in ("1", "2")]
        self.assertEqual(status.verified(rs), ("m", ["1", "2"], "context"))


# -- run metrics v2 -------------------------------------------------------------------------------

def transition(r, ms, frm, to, reason="x", **kw):
    r.ev(ms, "state_transition", {"from": frm, "to": to, "reason": reason, "detail": f"{frm}->{to}"}, state=to, **kw)


def pack(r, ms, pack_id, name, frm, to, step, state="travelling"):
    r.ev(ms, "pack_state", {"pack_id": pack_id, "name": name, "type": "pull", "boss": False, "optional": True,
                            "from": frm, "to": to}, state=state, step=step, pack_id=pack_id)


def metrics_of(r):
    return model.reconstruct(r.events)["metrics"]


class DeadTargetWaitTest(unittest.TestCase):
    def test_no_step_evidence_is_none(self):
        r = Run()
        r.ev(0, "start")
        r.ev(5000, "position_sample", {"moving": True, "in_combat": False})
        self.assertIsNone(model.reconstruct(r.events)["dead_target_waits"])
        self.assertIsNone(metrics_of(r)["dead_target_waits"])

    def test_found_dead_counts_until_the_step_is_left(self):
        r = Run()
        transition(r, 0, "none", "travelling", step=3)
        pack(r, 10000, 7, "Worg", "unknown", "cleared", step=3)
        r.ev(14000, "already_dead", {"detail": "Worg"}, step=3, pack_id=7)
        r.ev(15000, "position_sample", {}, step=4)
        (ep,) = model.reconstruct(r.events)["dead_target_waits"]
        self.assertEqual((ep["kind"], ep["duration_ms"], ep["ended_by"], ep["name"]), ("found_dead", 4000,
                                                                                         "already_dead", "Worg"))
        mt = metrics_of(r)["dead_target_waits"]
        self.assertEqual((mt["count"], mt["total_s"], mt["max_s"], mt["found_dead"]), (1, 4.0, 4.0, 1))

    def test_a_pack_the_party_fought_is_not_a_dead_target(self):
        r = Run()
        transition(r, 0, "none", "travelling", step=3)
        pack(r, 5000, 7, "Worg", "available", "engaged", step=3, state="combat")
        pack(r, 9000, 7, "Worg", "engaged", "cleared", step=3, state="combat")
        r.ev(14000, "already_dead", {"detail": "Worg"}, step=3, pack_id=7)
        mt = metrics_of(r)["dead_target_waits"]
        self.assertEqual((mt["count"], mt["total_s"], mt["max_s"]), (0, 0.0, 0.0), "a measured zero, not None")

    def test_not_found_from_arrival_and_warning(self):
        r = Run()
        transition(r, 0, "none", "travelling", step=5)
        r.ev(20000, "reached", {"detail": "Velratha"}, step=5, pack_id=9)
        r.ev(65000, "not_found", {"detail": "Velratha"}, step=5, pack_id=9)
        r.ev(65000, "objective_skipped", {"detail": "x", "name": "Velratha", "why": "not_found"}, step=5, pack_id=9)
        r.ev(66000, "position_sample", {}, step=6)
        (ep,) = model.reconstruct(r.events)["dead_target_waits"]
        self.assertEqual((ep["kind"], ep["start_ms"], ep["duration_ms"]), ("not_found", 20000, 45000))
        v = analyze_run.verdict(model.reconstruct(r.events))
        self.assertIn("dead_target_wait_s=45.0", v["warnings"])

    def test_under_a_second_adds_seconds_not_count(self):
        r = Run()
        transition(r, 0, "none", "travelling", step=3)
        pack(r, 10000, 7, "Worg", "unknown", "cleared", step=3)
        r.ev(10400, "already_dead", {"detail": "Worg"}, step=3, pack_id=7)
        r.ev(11000, "position_sample", {}, step=4)
        mt = metrics_of(r)["dead_target_waits"]
        self.assertEqual((mt["count"], mt["found_dead"], mt["total_s"], mt["max_s"]), (0, 0, 0.4, 0.4))

    def test_overlapping_waits_are_one_episode_counted_on_the_step_only(self):
        # found dead, then a fight elsewhere holds the leader, then nothing at the spot: one episode,
        # and only the walking time counts (the fight has its own reason)
        r = Run()
        transition(r, 0, "none", "travelling", step=2)
        pack(r, 10000, 4, "Ogre", "unknown", "cleared", step=2)
        r.ev(10500, "reached", {"detail": "Ogre"}, step=2, pack_id=4)
        transition(r, 11000, "travelling", "combat", step=2, pack_id=4)
        transition(r, 30000, "combat", "travelling", step=2, pack_id=4)
        r.ev(35000, "not_found", {"detail": "Ogre"}, step=2, pack_id=4)
        m = model.reconstruct(r.events)
        (ep,) = m["dead_target_waits"]
        self.assertEqual((ep["kind"], ep["elapsed_ms"], ep["duration_ms"], ep["ended_by"]),
                         ("found_dead", 25000, 6000, "not_found"))
        ts = m["metrics"]["time_split"]
        self.assertEqual(ts["waiting_by_reason_s"]["dead_target"], 6.0)
        self.assertEqual(ts["combat_s"], 19.0)


class TeleportTest(unittest.TestCase):
    def test_kinds_and_leader_warning(self):
        r = Run()
        r.ev(0, "start")
        r.ev(1000, "leader_unstuck", {"detail": "Worg from=(1,2,3) ground=0 to=(4,5,6)"})
        r.ev(2000, "leader_to_floor", {"detail": "Worg z=-10 floor=-5"})
        r.ev(3000, "recovery_escalate", {"detail": "party_fragmented member=Ogan action=brought_to_leader +Izl +Nob"})
        r.ev(4000, "recovery_escalate", {"detail": "member_dead member=Heal action=none"})
        r.ev(5000, "wipe_recovery_teleport", {"detail": "Heal"})
        r.ev(6000, "unexpected_teleport", {"detail": "Ogan to map 0 (1,2,3) options=0 spell=8690 repeats_since_last=0"})
        m = model.reconstruct(r.events)
        tp = m["metrics"]["teleports"]
        self.assertEqual({k: tp[k] for k in ("leader_unstuck", "leader_to_floor", "member_to_leader", "wipe_recovery",
                                              "unexpected", "leader", "total")},
                         {"leader_unstuck": 1, "leader_to_floor": 1, "member_to_leader": 3, "wipe_recovery": 1,
                          "unexpected": 1, "leader": 2, "total": 7})
        self.assertEqual([t["who"] for t in m["teleports"] if t["kind"] == "member_to_leader"], [["Ogan", "Izl", "Nob"]])
        v = analyze_run.verdict(m)
        self.assertIn("leader_teleports=2", v["warnings"])
        self.assertEqual(v["health"], "unhealthy", "the unexpected teleport keeps its existing level")

    def test_member_teleports_and_run_back(self):
        r = Run()
        r.ev(0, "start")
        r.ev(1000, "run_back", {"phase": "held", "who": "Ogan", "ms": 0})
        r.ev(9000, "run_back", {"phase": "revived", "who": "Ogan", "how": "resurrected", "ms": 8000})
        r.ev(10000, "run_back", {"phase": "released", "who": "Izl", "ms": 0})
        r.ev(70000, "run_back", {"phase": "at_entrance", "who": "Izl", "how": "portal", "ms": 60000})
        r.ev(71000, "run_back", {"phase": "revived", "who": "Izl", "how": "portal", "ms": 61000})
        r.ev(99000, "run_back", {"phase": "rejoined", "who": "Izl", "how": "walk", "ms": 89000})
        r.ev(100000, "run_back", {"phase": "walk_order", "who": "Nob", "straggler": True, "ms": 0})
        r.ev(130000, "run_back", {"phase": "fallback", "who": "Nob", "straggler": True, "reason": "stuck", "ms": 30000})
        r.ev(130000, "member_teleport", {"who": "Nob", "context": "straggler", "reason": "stuck"})
        r.ev(130500, "member_teleport", {"who": "Izl", "context": "run_back", "reason": "no_path"})
        # the same teleports, also recorded by the recovery escalation: not counted twice
        r.ev(131000, "member_teleport", {"who": "Heal", "context": "recovery", "reason": "escalation"})
        r.ev(131000, "member_teleport", {"who": "Ogan", "context": "recovery", "reason": "escalation"})
        r.ev(131000, "recovery_escalate", {"detail": "party_fragmented member=Heal action=brought_to_leader +Ogan"})
        m = model.reconstruct(r.events)
        tp = m["metrics"]["teleports"]
        self.assertEqual((tp["member_to_leader"], tp["wipe_recovery"], tp["member"], tp["total"]), (3, 1, 4, 4))
        self.assertEqual(tp["fallback_reasons"], {"stuck": 1, "no_path": 1, "escalation": 2})
        rb = m["metrics"]["run_back"]
        self.assertEqual({k: rb[k] for k in ("held", "resurrected", "released", "walked_to_portal", "walk_orders",
                                             "rejoined_walking", "fallbacks", "back_ms_max")},
                         {"held": 1, "resurrected": 1, "released": 1, "walked_to_portal": 1, "walk_orders": 1,
                          "rejoined_walking": 1, "fallbacks": 1, "back_ms_max": 89000})

    def test_run_back_unknown_before_it_existed(self):
        self.assertIsNone(metrics_of_synthetic()["run_back"])

    def test_run_back_measured_zero_when_the_run_has_it(self):
        r = Run()
        r.ev(0, "start", {"detail": "tank=yes origin=auto_canary", "run_back": True})
        r.ev(5000, "route_complete", {"detail": "0 skipped"}, outcome="complete")
        rb = model.reconstruct(r.events)["metrics"]["run_back"]
        self.assertEqual((rb["released"], rb["fallbacks"], rb["back_ms_max"]), (0, 0, None))

    def test_none_is_a_measured_zero(self):
        tp = metrics_of_synthetic()["teleports"]
        self.assertEqual((tp["total"], tp["leader"]), (0, 0))


def metrics_of_synthetic():
    return model.reconstruct(synthetic())["metrics"]


class CcTest(unittest.TestCase):
    def run_with_moons(self, classes=(1, 5, 8)):
        r = Run()
        r.ev(0, "start")
        r.ev(100, "party_roster", {"members": [{"name": f"m{c}", "role": "dps", "class": c} for c in classes]})
        r.ev(1000, "mark_moon", {"detail": "Evoker"}, state="combat")
        r.ev(3000, "cc_landed", {"detail": "Evoker"}, state="combat")
        r.ev(9000, "cc_released", {"detail": "Evoker reason=last_enemy"}, state="combat")
        r.ev(10000, "mark_moon", {"detail": "Pirate"}, state="combat")
        r.ev(20000, "cc_released", {"detail": "Pirate"}, state="combat")
        r.ev(21000, "mark_moon", {"detail": "Wizard"}, state="combat")
        r.ev(22000, "cc_target_gone", {"detail": ""}, state="combat")
        r.ev(23000, "mark_moon", {"detail": "Squallshaper"}, state="combat")
        return r

    def test_outcomes_ratio_and_warning(self):
        m = model.reconstruct(self.run_with_moons().events)
        self.assertEqual([x["outcome"] for x in m["cc_marks"]], ["landed", "released", "target_gone", "unresolved"])
        cc = m["metrics"]["cc"]
        self.assertEqual((cc["moons"], cc["landed"], cc["released_without_cc"], cc["target_gone"], cc["unresolved"],
                          cc["replaced"]), (4, 1, 1, 1, 1, 0))
        self.assertEqual(cc["landed_ratio"], 0.25, "landed / moons placed")
        self.assertIsNone(cc["landed_by"], "no cc_outcome: the caster is not recorded")
        self.assertEqual((cc["party_cc_classes"], cc["moons_without_cc_class"]), (["mage"], 0))
        self.assertIn("cc_landed_ratio=0.25", analyze_run.verdict(m)["warnings"])

    def test_replaced_is_not_unresolved(self):
        r = Run()
        r.ev(0, "start")
        r.ev(1000, "mark_moon", {"detail": "A"})
        r.ev(2000, "mark_moon", {"detail": "B"})
        r.ev(3000, "cc_released", {"detail": "B"})
        m = model.reconstruct(r.events)
        cc = m["metrics"]["cc"]
        self.assertEqual((cc["replaced"], cc["unresolved"], cc["released_without_cc"]), (1, 0, 1))
        md = report.timeline_md(m)
        self.assertIn("1 replaced by a new moon", md)
        self.assertNotIn("still marked", md)

    def test_landed_by_from_cc_outcome(self):
        r = Run()
        r.ev(0, "start")
        r.ev(1000, "mark_moon", {"detail": "Evoker"})
        r.ev(2000, "cc_landed", {"detail": "Evoker"})
        r.ev(2000, "cc_outcome", {"outcome": "landed", "name": "Evoker", "spell_id": 118, "spell": "Polymorph",
                                  "caster": "Mage", "casters": []})
        r.ev(5000, "cc_released", {"detail": "Evoker reason=last_enemy"})
        r.ev(6000, "mark_moon", {"detail": "Pirate"})
        r.ev(9000, "cc_released", {"detail": "Pirate"})
        r.ev(9000, "cc_outcome", {"outcome": "released_timeout", "name": "Pirate", "casters": []})
        m = model.reconstruct(r.events)
        self.assertEqual([x["cc_outcome"] for x in m["cc_marks"]], ["landed", "released_timeout"])
        self.assertEqual(m["cc_marks"][0]["by"], {"caster": "Mage", "spell": "Polymorph", "spell_id": 118})
        self.assertEqual(m["metrics"]["cc"]["landed_by"],
                         [{"caster": "Mage", "spell": "Polymorph", "spell_id": 118, "moons": 1}])

    def test_rogue_and_paladin_are_not_cc_classes(self):
        # the module plans crowd control only with mage, warlock, druid and hunter spells
        cc = metrics_of(self.run_with_moons(classes=(2, 4)))["cc"]
        self.assertEqual((cc["party_cc_classes"], cc["moons_without_cc_class"]), ([], 4))

    def test_moon_without_a_cc_class_in_the_party(self):
        cc = metrics_of(self.run_with_moons(classes=(1, 5)))["cc"]
        self.assertEqual((cc["party_cc_classes"], cc["moons_without_cc_class"]), ([], 4))

    def test_roster_without_classes_is_unknown(self):
        cc = metrics_of_synthetic()["cc"]  # the synthetic roster has no class field
        self.assertEqual((cc["moons"], cc["landed_ratio"], cc["party_cc_classes"], cc["moons_without_cc_class"]),
                         (0, None, None, None))

    def test_no_warning_below_three_moons(self):
        r = Run()
        r.ev(0, "start")
        r.ev(1000, "mark_moon", {"detail": "A"})
        r.ev(2000, "cc_released", {"detail": "A"})
        v = analyze_run.verdict(model.reconstruct(r.events))
        self.assertFalse(any(w.startswith("cc_landed_ratio") for w in v["warnings"]))


class WipeToResumeTest(unittest.TestCase):
    def test_per_wipe_and_a_wipe_without_recovery(self):
        r = Run()
        transition(r, 0, "none", "travelling")
        r.ev(10000, "wipe_detected", {"detail": "wipe #1", "wipe": 1}, state="wipe_recovery")
        transition(r, 10000, "combat", "wipe_recovery")
        transition(r, 40000, "wipe_recovery", "waiting_ready")
        transition(r, 52500, "waiting_ready", "travelling")
        r.ev(90000, "wipe_detected", {"detail": "wipe #2", "wipe": 2}, state="wipe_recovery")
        transition(r, 90000, "travelling", "wipe_recovery")
        r.ev(120000, "position_sample", {}, state="wipe_recovery")
        m = model.reconstruct(r.events)
        self.assertEqual([w["resumed_ms"] for w in m["wipes"]], [52500, None])
        wr = m["metrics"]["wipe_to_resume_s"]
        self.assertEqual((wr["wipes"], wr["median_s"], wr["max_s"], wr["unresolved"]), ([42.5, None], 42.5, 42.5, 1))

    def test_no_state_transitions_is_none(self):
        self.assertIsNone(metrics_of_synthetic()["wipe_to_resume_s"])
        self.assertIsNone(metrics_of_synthetic()["time_split"])


class TimeSplitTest(unittest.TestCase):
    def events(self):
        r = Run()
        transition(r, 0, "none", "waiting_ready", reason="session_start")
        r.ev(4000, "waiting", {"detail": "healer low on mana - Heal"}, state="waiting_ready")
        transition(r, 6000, "waiting_ready", "travelling", reason="party_ready")
        r.ev(10000, "pull_state", {"detail": "#5 Worg approaching->waiting_patrol attempt=0 patrol=Wolf spawn=1 dist=16"})
        r.ev(13000, "pull_state", {"detail": "#5 Worg waiting_patrol->waiting_party attempt=0 reason=group too spread (Osan)"})
        r.ev(15000, "pull_state", {"detail": "#5 Worg waiting_party->established attempt=0"})
        transition(r, 16000, "travelling", "combat", reason="combat_started")
        transition(r, 30000, "combat", "post_combat", reason="combat_ended")
        r.ev(34000, "post_combat_decision", {"detail": "travelling readiness=ready lowest_health=90 healer_mana=80 "
                                                       "waited_ms=4000"}, state="post_combat")
        transition(r, 34000, "post_combat", "travelling", reason="party_ready")
        r.ev(39000, "recovery_start", {"reason": "party_fragmented", "member": "Osan", "detail": "x"},
             state="travelling")
        transition(r, 40000, "travelling", "recovery", reason="recovery_needed")
        r.ev(45000, "recovery_complete", {"detail": "party_fragmented ms=5000"}, state="recovery")
        transition(r, 45000, "recovery", "boss_prep", reason="pull_preparing")
        transition(r, 47000, "boss_prep", "boss_combat", reason="boss_engaged")
        r.ev(50000, "route_complete", {"detail": "0 skipped"}, state="boss_combat", outcome="complete")
        return r.events

    def test_categories_reasons_and_cover_exactly_once(self):
        m = model.reconstruct(self.events())
        spans = m["time_spans"]
        self.assertEqual(spans[0]["start_ms"], 0)
        self.assertEqual(spans[-1]["end_ms"], 50000)
        self.assertTrue(all(a["end_ms"] == b["start_ms"] for a, b in zip(spans, spans[1:])), "no gap, no overlap")
        ts = m["metrics"]["time_split"]
        self.assertEqual(ts["total_s"], 50.0)
        # walking 6-10, 15-16, 34-40 s; waiting 0-6, 10-15 (pull held), 30-34 s
        self.assertEqual((ts["walking_s"], ts["combat_s"], ts["waiting_s"], ts["recovery_s"], ts["boss_prep_s"]),
                         (11.0, 17.0, 15.0, 5.0, 2.0))
        w = ts["waiting_by_reason_s"]
        # waiting_ready 0-6 s: the throttled waiting line at 4 s explains it from the start
        self.assertEqual(w["healer_mana"], 6.0)
        self.assertEqual((w["patrol"], w["party_range"], w["post_combat_check"]), (3.0, 2.0, 4.0))
        # the recovery_start comes just before the switch to the recovery state
        self.assertEqual(ts["recovery_by_reason_s"], {"party_fragmented": 5.0})

    def test_an_old_finished_recovery_does_not_name_a_later_one(self):
        r = Run()
        transition(r, 0, "none", "travelling")
        r.ev(1000, "recovery_start", {"reason": "member_dead", "member": "Heal", "detail": "x"})
        r.ev(3000, "recovery_complete", {"detail": "member_dead ms=2000"})
        transition(r, 20000, "travelling", "recovery", reason="recovery_needed")
        transition(r, 25000, "recovery", "travelling", reason="party_ready")
        r.ev(30000, "position_sample", {})
        ts = metrics_of(r)["time_split"]
        self.assertEqual(ts["recovery_by_reason_s"], {"unattributed": 5.0})

    def test_overlapping_hold_and_dead_target_count_once(self):
        r = Run()
        transition(r, 0, "none", "travelling", step=1)
        pack(r, 2000, 3, "Ogre", "unknown", "cleared", step=1)
        r.ev(3000, "pull_state", {"detail": "#3 Ogre approaching->waiting_party attempt=0 reason=group too spread"},
             step=1, pack_id=3)
        r.ev(8000, "pull_state", {"detail": "#3 Ogre waiting_party->none attempt=0"}, step=1, pack_id=3)
        r.ev(9000, "already_dead", {"detail": "Ogre"}, step=1, pack_id=3)
        r.ev(10000, "position_sample", {}, step=2)
        ts = metrics_of(r)["time_split"]
        w = ts["waiting_by_reason_s"]
        self.assertEqual((w["dead_target"], w["party_range"], ts["walking_s"], ts["total_s"]), (7.0, 0.0, 3.0, 10.0))


class BacktrackTest(unittest.TestCase):
    @staticmethod
    def walk(points, path, dest):
        r = Run()
        transition(r, 0, "none", "travelling", step=1)
        r.ev(500, "path_decision", {"kind": "direct", "objective": "Boss", "dest_x": dest[0], "dest_y": dest[1],
                                    "dest_z": 0.0, "path": [[x, y, 0.0] for x, y in path]}, step=1)
        for i, (x, y) in enumerate(points):
            r.ev(1000 + 2000 * i, "position_sample", {"moving": True, "in_combat": False}, step=1, x=x, y=y)
        return metrics_of(r)["backtrack_ratio"]

    def test_away_along_the_path(self):
        bt = self.walk([(0, 0), (10, 0), (20, 0), (10, 0)], [(0, 0), (100, 0)], (100, 0))
        self.assertEqual((bt["walked_yd"], bt["away_yd"], bt["ratio"]), (30.0, 10.0, 0.333))

    def test_a_bend_is_not_backwards(self):
        # the path turns back toward the start: straight-line distance to the end grows, the way
        # left along the path does not
        u = [(0, 0), (50, 0), (50, 50), (0, 50)]
        bt = self.walk([(0, 0), (25, 0), (50, 0), (50, 25)], u, (0, 50))
        self.assertEqual((bt["away_yd"], bt["ratio"]), (0.0, 0.0))

    def test_no_recorded_path_is_none(self):
        self.assertIsNone(metrics_of_synthetic()["backtrack_ratio"])


# -- summary and campaign comparison --------------------------------------------------------------

CTX = {"context_id": "c1", "module_sha": "abc1234", "core_sha": "core1", "playerbots_sha": "pb1",
       "route_digest": "r1", "config_digest": "k1", "mmap_digest": "m1", "incomplete": False}


def campaign_run(run_id, campaign_id, lfg=8, minutes=30, full=True, unstuck=False, wipes=0, ctx=None):
    r = Run()
    r.ev(0, "start", {"detail": "tank=yes", "context": dict(ctx or CTX)})
    transition(r, 0, "none", "travelling")
    for i in range(wipes):
        r.ev(60000 * (i + 1), "wipe_detected", {"detail": f"wipe #{i + 1}", "wipe": i + 1}, state="wipe_recovery")
        transition(r, 60000 * (i + 1), "travelling", "wipe_recovery")
        transition(r, 60000 * (i + 1) + 20000, "wipe_recovery", "travelling")
    if unstuck:
        r.ev(70000, "leader_unstuck", {"detail": "x from=(1,1,1) ground=0 to=(2,2,2)"})
    r.ev(minutes * 60000, "route_complete" if full else "stop", {"detail": "0 skipped"},
         outcome="complete" if full else "failed", step=3 if not full else 0)
    for e in r.events:
        e["run_id"], e["campaign_id"] = run_id, campaign_id
        e["scenario_id"] = f"lfg{lfg}-lvl20-prof"
        e["dungeon"] = dict(e["dungeon"], lfg_id=lfg, name={8: "Shadowfang Keep", 4: "Ragefire Chasm"}[lfg])
    return r.events


def write_campaign(root, campaign_id, runs):
    ev_path = os.path.join(root, f"{campaign_id}.events.jsonl")
    with open(ev_path, "w") as f:
        for evs in runs:
            for e in evs:
                f.write(json.dumps(e, separators=(",", ":")) + "\n")
    camp_path = os.path.join(root, f"{campaign_id}.campaigns.jsonl")
    with open(camp_path, "w") as f:
        f.write(json.dumps({"schema_version": 2, "record": "campaign_started", "campaign_id": campaign_id,
                            "commit_sha": "abc1234", "validation_profile": "prof", "dungeons": [8, 4]}) + "\n")
        f.write(json.dumps({"schema_version": 2, "record": "campaign_finished", "campaign_id": campaign_id}) + "\n")
    campaign.build(ev_path, camp_path, campaign_id, root)
    return os.path.join(root, "campaigns", campaign_id)


class SummaryTest(unittest.TestCase):
    def test_groups_rates_and_inputs(self):
        from tools.run_replay import summary
        with tempfile.TemporaryDirectory() as d:
            a = write_campaign(d, "camp-a", [campaign_run("1", "camp-a", minutes=30),
                                             campaign_run("2", "camp-a", minutes=40, wipes=1),
                                             campaign_run("3", "camp-a", lfg=4, minutes=20, full=False)])
            s = summary.build([a, os.path.join(a, "runs", "1")])  # a run reached twice counts once
            self.assertEqual(len(s["runs"]), 3)
            self.assertEqual({r["evidence"] for r in s["runs"]}, {"raw"})
            sfk = next(g for g in s["groups"] if g["lfg_id"] == 8)
            self.assertEqual((sfk["runs"], sfk["full_route"], sfk["full_route_rate"]), (2, 2, 1.0))
            self.assertEqual(sfk["duration_min"], {"median": 35.0, "p90": 40.0})
            self.assertEqual((sfk["wipes_per_run"], sfk["wipe_to_resume_s"]["median"]), (0.5, 20.0))
            self.assertEqual(sfk["context_id"], "c1")
            self.assertEqual(s["overall"]["runs"], 3)
            md = summary.summary_md(s)
            self.assertIn("| Shadowfang Keep | c1 |", md)
            # the whole validation_runs directory reads the same runs
            self.assertEqual(len(summary.build([d])["runs"]), 3)

    def test_older_artifacts_without_raw_events(self):
        # a run directory with only a version 1 run model: read as it is, new metrics None
        from tools.run_replay import summary
        import shutil
        with tempfile.TemporaryDirectory() as d:
            a = write_campaign(d, "camp-a", [campaign_run("1", "camp-a")])
            rdir = os.path.join(a, "runs", "1")
            shutil.rmtree(os.path.join(rdir, "raw"))
            path = os.path.join(rdir, "derived", "run_model.json")
            with open(path) as f:
                m = json.load(f)
            m["model_version"] = 1
            m["metrics"] = {k: v for k, v in m["metrics"].items() if not isinstance(v, dict) and k != "metrics_version"}
            with open(path, "w") as f:
                json.dump(m, f)
            (row,) = summary.build([rdir])["runs"]
            self.assertEqual((row["evidence"], row["result"], row["teleports"], row["time_split"]),
                             ("run_model_v1", "FULL_ROUTE", None, None))
            g = summary.build([rdir])["groups"][0]
            self.assertEqual((g["full_route"], g["teleports"], g["time_share"]), (1, None, None))

    def test_not_an_input(self):
        from tools.run_replay import summary
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit):
                summary.build([d])


class CompareCampaignsTest(unittest.TestCase):
    def sides(self, b_runs, b_ctx=None):
        from tools.run_replay import compare_campaigns, summary
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        d = tmp.name
        a = write_campaign(d, "camp-a", [campaign_run(str(i), "camp-a", minutes=30) for i in range(1, 6)])
        b = write_campaign(d, "camp-b", [campaign_run(str(10 + i), "camp-b", ctx=b_ctx, **kw)
                                         for i, kw in enumerate(b_runs)])
        sa = summary.build([a])
        path = os.path.join(d, "a.json")
        with open(path, "w") as f:
            json.dump(sa, f)
        return compare_campaigns.load_side(path), compare_campaigns.load_side(b)

    def test_same(self):
        from tools.run_replay import compare_campaigns
        c = compare_campaigns.compare(*self.sides([{"minutes": 31}] * 5))
        self.assertEqual((c["verdict"], c["low_sample"], c["not_comparable"]), ("same", False, []))
        self.assertFalse(c["dungeons"][0]["low_sample"])

    def test_overall_low_sample(self):
        from tools.run_replay import compare_campaigns
        c = compare_campaigns.compare(*self.sides([{"minutes": 31}] * 2))
        self.assertEqual((c["verdict"], c["low_sample"]), ("same", True))
        self.assertIn("(low sample)", compare_campaigns.comparison_md(c).splitlines()[0])

    def test_runs_without_lfg_id_stay_apart_by_dungeon(self):
        from tools.run_replay import compare_campaigns, summary
        a, b = self.sides([{}] * 5)
        for side, name in ((a, "Shadowfang Keep"), (b, "Other Dungeon")):
            for r in side["runs"]:
                r["lfg_id"], r["dungeon"] = None, name
        c = compare_campaigns.compare(a, b)
        self.assertEqual(sorted((d["dungeon"], d["runs_a"], d["runs_b"]) for d in c["dungeons"]),
                         [("Other Dungeon", 0, 5), ("Shadowfang Keep", 5, 0)])
        self.assertEqual((c["verdict"], sorted(c["not_comparable"])),
                         ("not comparable", ["Other Dungeon", "Shadowfang Keep"]))
        self.assertEqual(summary.dungeon_key(a["runs"][0]), (0, "Shadowfang Keep"))

    def test_worse_on_full_route_rate_duration_and_a_new_teleport(self):
        from tools.run_replay import compare_campaigns
        c = compare_campaigns.compare(*self.sides([{"full": False}] + [{"minutes": 30}] * 4))
        (d,) = c["dungeons"]
        self.assertEqual((d["verdict"], d["worse"]), ("worse", ["full_route_rate"]))
        c = compare_campaigns.compare(*self.sides([{"minutes": 34}] * 5))
        self.assertEqual(c["dungeons"][0]["worse"], ["full_route_median_min"])
        c = compare_campaigns.compare(*self.sides([{"unstuck": True}] + [{}] * 4))
        self.assertEqual((c["verdict"], c["dungeons"][0]["worse"]), ("worse", ["teleports_leader_unstuck"]))
        self.assertIn("WORSE", compare_campaigns.comparison_md(c))

    def test_better(self):
        from tools.run_replay import compare_campaigns
        c = compare_campaigns.compare(*self.sides([{"minutes": 25}] * 5))
        self.assertEqual((c["verdict"], c["dungeons"][0]["better"]), ("better", ["full_route_median_min"]))

    def test_context_blocks_unless_accepted_and_module_never_blocks(self):
        from tools.run_replay import compare_campaigns
        a, b = self.sides([{}] * 5, b_ctx=dict(CTX, context_id="c2", route_digest="r2"))
        c = compare_campaigns.compare(a, b)
        self.assertEqual(c["verdict"], "not comparable")
        self.assertTrue(any("route_digest" in w for w in c["dungeons"][0]["why"]))
        self.assertEqual(compare_campaigns.compare(a, b, accept=["route_digest"])["verdict"], "same")
        a, b = self.sides([{}] * 5, b_ctx=dict(CTX, context_id="c3", module_sha="def5678"))
        c = compare_campaigns.compare(a, b)
        self.assertEqual(c["verdict"], "same")
        self.assertTrue(any(x.startswith("module_sha") for x in c["dungeons"][0]["differences"]))

    def test_dungeon_on_one_side_only(self):
        from tools.run_replay import compare_campaigns
        c = compare_campaigns.compare(*self.sides([{"lfg": 4}]))
        self.assertEqual(c["verdict"], "not comparable")
        self.assertEqual({d["verdict"] for d in c["dungeons"]}, {"not comparable"})


if __name__ == "__main__":
    unittest.main()
