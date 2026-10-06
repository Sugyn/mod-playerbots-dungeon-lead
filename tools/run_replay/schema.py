"""Reading raw schema v2 telemetry (docs/telemetry-schema-v2.md).

Raw files are evidence: this module only reads them. Unknown schema versions are an error, never
guessed at.
"""
import gzip
import json

SUPPORTED_SCHEMA = 2


class SchemaError(Exception):
    pass


def _open(path):
    return gzip.open(path, "rt", encoding="utf-8") if path.endswith(".gz") else open(path, encoding="utf-8")


def load_events(path, run_id=None):
    """Events of one run (or all runs), in canonical order: run, then event_seq."""
    events = []
    with _open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError as e:
                raise SchemaError(f"{path}:{lineno}: not JSON ({e})") from None
            version = ev.get("schema_version")
            if version != SUPPORTED_SCHEMA:
                raise SchemaError(f"{path}:{lineno}: schema_version {version!r}, this tool reads {SUPPORTED_SCHEMA}")
            if ev.get("event_type") == "telemetry_dropped":
                # written by the flush, belongs to no run - kept for every run as an integrity fact
                ev.setdefault("run_id", None)
                events.append(ev)
                continue
            if run_id is not None and ev.get("run_id") != str(run_id):
                continue
            events.append(ev)
    dropped = [e for e in events if e.get("event_type") == "telemetry_dropped"]
    run_events = [e for e in events if e.get("event_type") != "telemetry_dropped"]
    run_events.sort(key=lambda e: (e["run_id"], e["event_seq"]))
    return run_events, dropped


def run_ids(path):
    """Run ids in the file, in order of first appearance."""
    seen = []
    with _open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            ev = json.loads(line)
            rid = ev.get("run_id")
            if rid and rid not in seen:
                seen.append(rid)
    return seen


def load_campaigns(path):
    records = []
    with _open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("schema_version") != SUPPORTED_SCHEMA:
                raise SchemaError(f"{path}:{lineno}: schema_version {rec.get('schema_version')!r}")
            records.append(rec)
    return records
