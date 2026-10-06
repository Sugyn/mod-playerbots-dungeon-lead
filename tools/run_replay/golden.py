"""Golden fixtures for the replay tools (tests/fixtures/run_reconstruction_v2/<case>/).

Each case holds the raw v2 events of one real run (events.jsonl.gz, never edited), the campaign
records it belongs to (campaigns.jsonl) and the expected outputs: run_model.json.gz, timeline.md,
verdict.json, failures.json. A change to reconstruction or analysis that changes any of them is
a deliberate decision: regenerate with

  python3 -m tools.run_replay.golden --update
"""
import gzip
import json
import os
import sys

from . import analyze_run, model, report, schema
from .cli import campaign_result

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                    "tests", "fixtures", "run_reconstruction_v2")


def _dump(obj):
    return json.dumps(obj, indent=1, sort_keys=True, ensure_ascii=False) + "\n"


def outputs(case_dir):
    evs, dropped = schema.load_events(os.path.join(case_dir, "events.jsonl.gz"))
    m = model.reconstruct(evs, dropped)
    cpath = os.path.join(case_dir, "campaigns.jsonl")
    cres = campaign_result(m, schema.load_campaigns(cpath)) if os.path.exists(cpath) else None
    # the planned route comes from data/, which changes with the routes: keep the map out of goldens
    return {
        "run_model.json.gz": _dump(m),
        "timeline.md": report.timeline_md(m),
        "verdict.json": _dump(analyze_run.verdict(m, cres)),
        "failures.json": _dump(analyze_run.failures(m, cres)),
    }


def cases():
    return sorted(d for d in os.listdir(ROOT) if os.path.isdir(os.path.join(ROOT, d)))


def read(path):
    if path.endswith(".gz"):
        with gzip.open(path, "rt", encoding="utf-8") as f:
            return f.read()
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    if path.endswith(".gz"):
        with gzip.GzipFile(path, "wb", mtime=0) as f:
            f.write(text.encode("utf-8"))
    else:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)


def main():
    update = "--update" in sys.argv
    bad = 0
    for case in cases():
        d = os.path.join(ROOT, case)
        for name, text in outputs(d).items():
            path = os.path.join(d, name)
            if update:
                write(path, text)
            elif not os.path.exists(path) or read(path) != text:
                print(f"{case}/{name}: differs from the golden")
                bad += 1
    print("updated" if update else f"{len(cases())} cases, {bad} differences")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
