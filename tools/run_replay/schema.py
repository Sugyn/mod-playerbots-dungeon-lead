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
    _validate_lineage(run_events)
    return run_events, dropped


def _validate_lineage(run_events):
    """DL-010: flag duplicate/out-of-order telemetry lineage rather than silently accepting it - a
    producer bug (or a corrupted/concatenated file) replaying as a clean timeline would be a worse
    failure than refusing to replay it at all. Checked per run_id: event_seq must be strictly
    increasing (no duplicates/repeats) and run_ms must never go backwards within a run - both
    invariants the producer (DungeonLeadActions.cpp's event envelope) is supposed to guarantee."""
    last_seq_by_run = {}
    last_ms_by_run = {}
    for e in run_events:  # already sorted by (run_id, event_seq)
        run_id = e["run_id"]
        seq = e["event_seq"]
        ms = e.get("run_ms")
        last_seq = last_seq_by_run.get(run_id)
        if last_seq is not None and seq == last_seq:
            raise SchemaError(f"run {run_id}: duplicate event_seq {seq}")
        last_ms = last_ms_by_run.get(run_id)
        if last_ms is not None and ms is not None and ms < last_ms:
            raise SchemaError(f"run {run_id}: run_ms went backwards ({last_ms} -> {ms}) at event_seq {seq}")
        last_seq_by_run[run_id] = seq
        if ms is not None:
            last_ms_by_run[run_id] = ms


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
