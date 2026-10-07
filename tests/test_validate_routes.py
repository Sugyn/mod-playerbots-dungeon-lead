"""Tests for tools/validate_routes.py's mandatory-row position check (audit finding DL-008).

validate_routes.py mirrors DungeonRouteStep::IsMandatory()/HasPosition() in Python so bad route
data is caught before it ever reaches the (AzerothCore-dependent, not buildable in this sandbox)
C++ loader. DL-008 hardened that mirror: a required/door/use/talk row with no resolved position
used to fall through to a silent pass (no error, no warning at all) unless it happened to be
"boss" or "heroic_only" specifically. It now hard-errors for every kind DungeonRouteStep::
IsMandatory() (now derived from ClassifyRequirement() in DungeonRouteTypes.h) treats as mandatory:
boss, required, door, use, talk. An optional row with no position is unaffected - still silently
fine, exactly as before DL-008.
"""
import csv
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tools.validate_routes import validate  # noqa: E402

LFG_HEADER = "id\tname\tminL\tmaxL\tmap\tdiff\ttype\texp\tgroup\n"
LFG_ROW = "1\tTest Dungeon\t1\t1\t1\t0\t1\t0\t1\n"

CSV_HEADER = "lfg_id,lfg_name,map,difficulty,expansion,wing,step,kind,boss,entry,x,y,z,source,note\n"


def row(step, kind, boss, entry="", x="", y="", z="", source=""):
    """One data/dungeon_routes.csv row for lfg_id=1 (map=1, difficulty=0, matching LFG_ROW)."""
    return f"1,Test Dungeon,1,0,0,-,{step},{kind},{boss},{entry},{x},{y},{z},{source},\n"


def run_validate(rows):
    """Writes a fixture lfg_dungeons.tsv + dungeon_routes.csv to a temp dir and runs validate()
    against it - same code path as the real data/, just pointed at throwaway fixture files."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        (tmp_path / "lfg_dungeons.tsv").write_text(LFG_HEADER + LFG_ROW)
        (tmp_path / "dungeon_routes.csv").write_text(CSV_HEADER + "".join(rows))
        return validate(tmp_path)


class TestMandatoryRowsErrorOnNoPosition(unittest.TestCase):
    """required/door/use/talk rows with no resolved position now hard-ERROR (DL-008) - this used
    to be a silent pass for every one of these kinds except "boss"."""

    def test_required_no_position_errors(self):
        _, errors, _, _, _ = run_validate([row(1, "required", "Trash gate")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("mandatory 'required'", errors[0])

    def test_door_no_position_errors(self):
        _, errors, _, _, _ = run_validate([row(1, "door", "Locked door")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("mandatory 'door'", errors[0])

    def test_use_no_position_errors(self):
        _, errors, _, _, _ = run_validate([row(1, "use", "Lever")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("mandatory 'use'", errors[0])

    def test_talk_no_position_errors(self):
        _, errors, _, _, _ = run_validate([row(1, "talk", "NPC gossip")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("mandatory 'talk'", errors[0])

    def test_mandatory_with_resolved_position_is_clean(self):
        """The same kinds, each WITH a resolved position, must not error - this check is about a
        missing position, not about the kind itself."""
        rows = [
            row(1, "required", "Trash gate", entry=100, x=1, y=2, z=3),
            row(2, "door", "Locked door", entry=101, x=4, y=5, z=6),
            row(3, "use", "Lever", entry=102, x=7, y=8, z=9),
            row(4, "talk", "NPC gossip", entry=103, x=10, y=11, z=12),
        ]
        _, errors, _, _, _ = run_validate(rows)
        self.assertEqual(errors, [])


class TestOptionalRowsUnaffected(unittest.TestCase):
    """An optional row with no position is exactly as before DL-008: not an error. (It is not even
    a warning today - any_unresolved-and-not-has_position silently passes for optional/event rows
    with no other flag set; this test locks in that it did NOT newly become an error.)"""

    def test_optional_no_position_does_not_error(self):
        _, errors, _, _, _ = run_validate([row(1, "optional", "Optional trash")])
        self.assertEqual(errors, [])

    def test_event_no_position_does_not_error(self):
        _, errors, _, _, _ = run_validate([row(1, "event", "Scripted event")])
        self.assertEqual(errors, [])

    def test_heroic_only_no_position_still_only_warns(self):
        """heroic_only is deliberately excluded from the DL-008 hard-error set (see MANDATORY_KINDS'
        comment and DL-021) - it keeps its own pre-existing warning instead of becoming an error."""
        _, errors, warnings, _, _ = run_validate([row(1, "heroic_only", "Heroic add")])
        self.assertEqual(errors, [])
        self.assertEqual(len(warnings), 1, warnings)
        self.assertIn("heroic_only step has no resolved position", warnings[0])


class TestBossRowCheckUnaffected(unittest.TestCase):
    """The original boss-mandatory check (pre-dating DL-008) must keep working unchanged."""

    def test_boss_no_position_still_errors(self):
        _, errors, _, _, _ = run_validate([row(1, "boss", "End boss")])
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("mandatory 'boss'", errors[0])

    def test_boss_with_position_is_clean(self):
        _, errors, _, _, _ = run_validate([row(1, "boss", "End boss", entry=999, x=1, y=1, z=1)])
        self.assertEqual(errors, [])


class TestMixedRoute(unittest.TestCase):
    """A realistic route mixing a resolved boss with an unresolved required door (the DL-008
    scenario: the boss alone used to let the run report Complete, never having attempted the
    broken door at all) - exactly one error, naming the door, not the boss."""

    def test_resolved_boss_plus_unresolved_required_door(self):
        rows = [
            row(1, "boss", "First boss", entry=1, x=1, y=1, z=1),
            row(2, "required", "Broken door", entry="", x="", y="", z=""),
        ]
        _, errors, _, _, _ = run_validate(rows)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("Broken door", errors[0])
        self.assertIn("mandatory 'required'", errors[0])


if __name__ == "__main__":
    unittest.main()
