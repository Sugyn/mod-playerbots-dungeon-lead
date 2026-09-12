# ADR-002: AutoBot Canary — unattended dungeon-lead testing

Status: **Stage 0/1 implemented and deployed** (`DungeonLeadSessionOrigin`, `DungeonLead::StartSession`,
`DungeonLeadCanary.h/.cpp`, config in `playerbots.conf.dist`). Later stages below are **not**
implemented — this ADR documents the whole staged plan so later work has a map, but only the first
slice actually exists in code today.

## Context

Every dungeon-lead run so far has needed a real player to type `startdungeon`. That's fine for
manual QA, but it caps how much telemetry accumulates: one person can only run one dungeon at a
time, and the single most useful finding from this project's first day of live data (Verdan the
Everliving's 100% stuck-skip rate in Wailing Caverns) came from repetition, not depth — the same
route walked wrong the same way, three separate times.

The ask: let the server generate that repetition on its own, from bot-only groups nobody is
actually playing, without needing a human to babysit it.

## What "unattended" must never do

This runs on a live server with real players sharing it. Before any eligibility/selection logic,
three properties are non-negotiable and are enforced independently of each other (not just at the
one place they're most likely to matter):

1. **Never touch a group with a real player in it.** Checked before starting a session, and again
   on every tick of an already-running one — a real player joining an ostensibly-bot group stops
   the canary session immediately.
2. **Never run without an explicit opt-in per dungeon.** A blanket "test everything" mode is not
   the default; `CanaryEnabled=1` alone starts nothing until specific LFG dungeon ids are named.
3. **Never run unbounded.** A concurrency cap and a timeout both exist so a bug that would
   otherwise make an unattended run silently occupy resources forever has a backstop that doesn't
   depend on the bug itself being well-behaved.

## Design

- **`DungeonLeadSessionOrigin`** (`Manual` / `AutoCanary`) on `DungeonLeadState`: threaded through
  logging (CSV, `LOG_INFO`) so a canary-started run is distinguishable from a human-started one in
  every downstream consumer (CSV analysis, the panel's Dungeon Lead page, a bug report) without
  guessing from context.
- **`DungeonLead::StartSession()`**: the shared "take leadership, snapshot followers, apply
  strategies, reset state, set the star icon" tail of both `startdungeon` and the canary
  controller. Each caller keeps its own preconditions (the chat command's group/5-man/leader-
  permission checks; the canary controller's pure-bot/allowlist/concurrency checks) — those
  genuinely differ by origin and don't belong in the shared function.
- **`DungeonLead::CanaryTick()`**, called from `PlayerbotsWorldScript::OnUpdate` next to
  `GuardActiveSessions()`, throttled internally (~5s):
  1. Safety pass over every currently-active `AutoCanary` session: stop-on-real-player-join,
     stop-on-timeout. Runs regardless of whether a new session is about to start.
  2. If under `CanaryMaxConcurrent`, scan `sRandomPlayerbotMgr.GetAllBots()` for an already-formed,
     all-bot, idle (not in combat), not-already-led 5-man group that queued via the real LFG tool
     (`sLFGMgr->GetDungeon`) for a dungeon in `CanaryAllowedLfgIds`. Deterministic tie-break
     (lowest `ObjectGuid`) if more than one is eligible on the same tick.
  3. Elects a tank-spec member if one exists in the group, otherwise leads with whoever was found.

## Why passive sampling, not active assembly, for this stage

The most valuable version of this feature is arguably "test Wailing Caverns right now" on demand —
actively selecting eligible idle bots, forming a party, and either queueing them for one specific
dungeon or bypassing LFG entirely (direct group + summon to the instance entrance, similar to how
`startdungeon` already manually sets up formations). That is real, useful, and explicitly **not**
part of this stage: it needs new bot-selection, grouping, and teleport code this patch has never
touched before, each with its own failure modes (picking a bot mid-quest, forming a group across
two different real parties, summoning into a hostile spawn) that deserve their own careful pass
and review rather than being folded into the same change as the safety-critical stop conditions
above. Stage 0/1 only *observes* groups the existing playerbot LFG system already formed on its
own — zero new ways to disturb a bot that wasn't already about to enter a dungeon anyway.

## Staged rollout

- **Stage 0 (this ADR, implemented):** code present, wired into the world tick, `CanaryEnabled=0`
  by default. Deploying this changes nothing observable on a server that doesn't opt in.
- **Stage 1 (this ADR, implemented, operator-gated):** `CanaryEnabled=1` with one dungeon in
  `CanaryAllowedLfgIds`, `CanaryMaxConcurrent=1`. Passive sampling only, as described above.
- **Stage 2 (not implemented):** targeted/on-demand testing — actively assemble a party for one
  named dungeon instead of waiting for LFG to organically produce one. New subsystem, new review.
- **Stage 3 (not implemented):** multiple dungeons and higher concurrency once Stage 1/2 have
  accumulated enough clean runs to trust the safety properties under real load.
- **Stage 4 (not implemented):** the L1.4 capability-scenario framework (ADR-001) layered on top,
  isolating *which* subsystem a canary failure points at instead of only pass/fail for the whole run.

## Explicitly deferred, not forgotten

- Original-group-leader snapshot/restore for a canary session (today, `Stop()`'s leader handback is
  a no-op for `AutoCanary` since there's no real-player master to hand back to — harmless for a
  pure-bot group, since no human perceives who holds the mechanical leader flag, but worth revisiting
  if Stage 2's active assembly ever needs to un-group bots back to where it found them).
- A structured one-row-per-run summary table/CSV (`DungeonLeadRuns.csv` or equivalent) distinct from
  the existing per-event `DungeonLeadSessions.csv` — `startdungeon test`'s chat report already
  covers this for a manual run; canary runs currently rely on the same CSV plus `origin=auto_canary`
  in the `start` event detail.
