# DUNGEON_LEADER_HARDENING_PLAN.md

## Purpose

This document defines the next implementation and validation stage for:

`Sugyn/mod-playerbots-dungeon-lead`

The core architecture is now largely in place.

The next objective is **not** another broad refactor.

The next objective is:

> harden the current autonomous dungeon leader, fix identified correctness gaps, strengthen encounter boundaries, add interaction/gating support, and validate behavior across a much wider dungeon set.

This plan assumes that the existing architecture already contains:

- `DungeonLeadBrain`
- `DungeonPartyState`
- `DungeonPullController`
- `DungeonRecoveryController`
- `DungeonTargetManager`
- typed route nodes
- pack state
- checkpoints
- boss foundation
- structured/buffered telemetry

The work below should improve the existing design rather than replace it.

---

# 1. Hard constraints

The following rules remain mandatory.

## 1.1 No competing module

Do not:

- import,
- copy,
- adapt,
- vendor,
- depend on,
- or use as fallback

any competing or alternative dungeon-leader module.

All implementation must remain self-contained inside:

`mod-playerbots-dungeon-lead`

Allowed dependencies remain:

- AzerothCore APIs,
- mod-playerbots APIs,
- standard C++ library,
- libraries already accepted by the project.

## 1.2 Preserve Playerbot combat responsibility

The dungeon leader remains an orchestration system.

It decides:

- when to wait,
- where to move,
- what pack is current,
- when to pull,
- when to recover,
- when to abort,
- what target plan applies,
- when to continue.

It must not become a replacement spell-rotation engine.

## 1.3 Do not introduce another large architecture rewrite

The current architecture is good enough to harden.

Do not redesign the whole project.

Only introduce a new subsystem when a clear behavior gap requires it.

---

# 2. Current priorities

Priority order:

```text
P0
Recovery timer correctness

P1
Stable combat-anchor target selection
Mandatory pack / boss failure policy

P2
Pack identity hardening
Dungeon interaction / gating controller

P3
Large-scale live dungeon validation
Failure-class-driven fixes

P4
Release hardening and versioned milestones
```

The implementation should proceed in this order unless a discovered blocker requires a small prerequisite change.

---

# 3. Phase H1 — Fix recovery timer state transitions

## Problem

`DungeonRecoveryController` may preserve the previous recovery start timestamp when the recovery reason changes.

Example:

```text
PartyFragmented
   |
   | after 20 seconds
   v
MemberLost
```

If the timer is not reset, `MemberLost` may inherit an almost-expired timeout and escalate immediately.

That is incorrect state-machine behavior.

## Required behavior

When the active recovery reason changes:

- reset recovery start timestamp,
- reset recovery step/substate,
- preserve only state that is intentionally shared across reasons.

Expected logic:

```cpp
if (observed != st.recoveryReason)
{
    st.recoveryReason = observed;
    st.recoverySinceTs = getMSTime();
    st.recoveryStep = RecoveryStep::None;
}
```

The exact code must match the existing implementation and naming.

## Required tests

Add deterministic tests for at least:

```text
PartyFragmented for 20s
→ MemberLost
→ elapsed recovery time must restart from 0

HealerDead
→ PartyFragmented
→ new reason must get independent timeout

PullFailed
→ RouteLost
→ no inherited escalation timer
```

## Acceptance criteria

- each recovery reason gets its own fresh timeout window,
- no stale timer is inherited accidentally,
- all existing recovery tests still pass,
- no unbounded retry is introduced.

## Commit target

Suggested:

```text
fix(dungeon-lead): reset recovery timeout on reason change
```

---

# 4. Phase H2 — Use stable combat anchor in target selection

## Problem

During combat, target candidate selection may use the tank's current position as the effective anchor.

That weakens the intended combat-boundary model.

If the tank moves away from the original engagement point, the target-selection radius moves with the tank and may begin to include nearby unrelated mobs or a neighboring pack.

## Required behavior

During active combat:

Preferred target-selection anchor:

```text
stored combat anchor
```

Fallback:

```text
current tank position
```

only when no valid anchor exists.

Suggested conceptual logic:

```cpp
if (combatState.anchorSet)
{
    ax = combatState.anchorX;
    ay = combatState.anchorY;
    az = combatState.anchorZ;
}
else
{
    ax = bot->GetPositionX();
    ay = bot->GetPositionY();
    az = bot->GetPositionZ();
}
```

Do not copy this blindly if existing state ownership differs.

Inspect current code first.

## Targeting invariant

The following systems should agree on the same encounter boundary whenever possible:

```text
pack identity
combat anchor
combat leash
target candidate selection
pull completion
```

## Required tests

Add scenarios for:

1. tank stays at anchor,
2. tank moves 10–15 yards during combat,
3. neighboring hostile exists near current tank position but outside original combat anchor radius,
4. target manager must not promote unrelated neighbor merely because tank moved,
5. engaged add explicitly linked to current fight must still be considered.

## Acceptance criteria

- combat target selection remains stable while tank moves locally,
- target plan does not drift into adjacent packs,
- legitimate adds remain targetable.

## Commit target

Suggested:

```text
fix(dungeon-lead): anchor combat target selection
```

---

# 5. Phase H3 — Introduce mandatory-objective failure policy

## Problem

Current pull failure handling may skip a pack after retry exhaustion.

That is useful for test exploration but unsafe as a universal production policy.

Not all packs are equivalent.

A failed optional trash pack may be skippable.

A failed mandatory pack or boss should not be silently skipped.

## Required model

Introduce explicit objective criticality.

Suggested semantic model:

```cpp
enum class DungeonObjectiveRequirement
{
    Optional,
    Required,
    Boss
};
```

Alternative naming is acceptable if existing data structures already provide equivalent information.

## Policy

Suggested default:

```text
Optional trash
→ skip allowed after bounded retry

Required trash
→ recovery
→ retry
→ abort if unresolved

Boss
→ recovery
→ retry
→ abort/partial run if unresolved
```

Do not route-advance beyond an unresolved mandatory objective.

## Required configuration/data

Each pack or route objective should expose whether it is:

- optional,
- required,
- boss.

If `optional` already exists, use it rather than duplicating state.

Boss should be treated as required unless route semantics explicitly say otherwise.

## Required tests

Test:

```text
optional trash fails 3 times
→ may be skipped

required trash fails 3 times
→ must not silently advance

boss pull fails
→ must not continue as if boss were complete

required objective later becomes valid
→ recovery/retry may resume
```

## Acceptance criteria

- only explicitly optional content can be automatically skipped,
- bosses cannot be silently bypassed,
- run outcome remains accurate,
- route state remains consistent.

## Commit target

Suggested:

```text
fix(dungeon-lead): enforce mandatory objective failure policy
```

---

# 6. Phase H4 — Harden pack identity

## Problem

Pack membership is currently still partly geometric.

Typical current model:

```text
expected creature entry
+
proximity to configured pack point
```

This can merge nearby groups in dense dungeons.

## Goal

Move from:

```text
radius-based pack guess
```

toward:

```text
configured pack identity
+
runtime encounter membership
+
explicitly engaged adds
```

## Recommended model

A pack should be composed from multiple signals.

### Static identity

Use:

- configured creature entries,
- configured approximate spawn region,
- route pack ID,
- expected count when useful.

### Runtime encounter identity

Add creatures when they are clearly part of the active engagement.

Potential signals:

- threat/combat relationship with tank,
- threat/combat relationship with current party,
- pulled together with configured pack,
- engaged during current pull window,
- explicit add spawned by current encounter.

### Exclusion rules

Avoid pulling unrelated nearby mobs into the logical pack simply because they are geographically close.

## Important

Do not overfit to one dungeon.

Create a generic pack-resolution policy first.

Dungeon-specific overrides should only be added when live evidence proves they are necessary.

## Required telemetry

For debugging, log pack resolution details when verbose/debug mode is enabled:

```text
pack_id
configured_entries
resolved_members
engaged_adds
rejected_nearby_units
resolution_reason
```

## Required tests

Create synthetic tests for:

```text
two packs 15 yd apart
same creature entry in both packs
tank engages only first pack
second pack must not be merged

unexpected add joins combat
add should become active encounter member

configured member missing
pack resolution should remain stable and bounded
```

## Acceptance criteria

- dense-room packs are not merged merely by radius,
- legitimate adds are included,
- pack clear detection becomes more reliable.

## Commit target

Suggested:

```text
refactor(dungeon-lead): strengthen runtime pack identity
```

---

# 7. Phase H5 — Add DungeonInteractionController

## Problem

Many dungeons cannot be completed by pure movement and pull logic.

Typical blockers include:

- doors,
- gates,
- scripted events,
- NPC interaction,
- elevators,
- keys,
- boss-unlock conditions,
- event completion waits.

The route system needs an explicit interaction/gating layer.

## New component

Introduce:

```text
DungeonInteractionController
```

Suggested responsibility:

- resolve current interaction objective,
- detect whether prerequisite is already satisfied,
- perform allowed interaction,
- wait for world-state confirmation,
- timeout safely,
- report success/failure to `DungeonLeadBrain`.

## Suggested interaction states

```cpp
enum class DungeonInteractionState
{
    None,
    Resolving,
    WaitingPrerequisite,
    Interacting,
    WaitingConfirmation,
    Complete,
    Failed
};
```

## Suggested interaction types

Start with a minimal useful set:

```cpp
enum class DungeonInteractionType
{
    Door,
    GameObject,
    Npc,
    Elevator,
    EventWait
};
```

Do not build a universal scripting engine.

Only add types required by actual dungeon routes.

## Route integration

Extend typed route nodes with an interaction objective when needed.

Example:

```text
Travel
Pull
Boss
Interaction
Recovery
End
```

or use existing `Door` / `Interaction` node types if already present.

## Required behavior

Example door flow:

```text
arrive at door
↓
is door already open?
├─ yes → continue
└─ no
    ↓
check prerequisite
    ↓
perform interaction or wait for event
    ↓
confirm actual world state
    ├─ success → continue
    └─ timeout → recovery / blocked / abort
```

## Confirmation rule

Just like leadership:

```text
requested interaction != confirmed interaction
```

World state must confirm success.

## Required tests

At minimum:

```text
door already open
door opens after interaction
door opens after boss death/event
interaction target missing
interaction times out
elevator unavailable
event eventually completes
```

## Acceptance criteria

- route no longer assumes all path blockers are static,
- gated routes can wait and continue safely,
- interaction failure produces explicit reason instead of movement thrashing.

## Commit targets

Potential split:

```text
feat(dungeon-lead): add interaction controller
feat(dungeon-lead): add route interaction objectives
```

---

# 8. Phase H6 — Hardening of recovery and route reconciliation

After H1–H5, review the combined failure paths.

## Scenarios to validate

```text
pull fails
→ recovery
→ pack becomes resolvable
→ retry

mandatory pack fails permanently
→ abort/partial

party fragments during interaction
→ recovery
→ interaction objective preserved

wipe near interaction node
→ checkpoint restore
→ interaction state recomputed from world

boss evade
→ encounter state reset
→ no false boss completion

leadership lost during recovery
→ lifecycle error wins over normal recovery
```

## Required invariant

Recovery must always preserve enough context to answer:

```text
What objective were we trying to complete?
Is that objective still valid?
Can we retry it?
Must we rewind?
Must we abort?
```

## Acceptance criteria

No failure path should leave:

- stale pack state,
- stale interaction state,
- stale target plan,
- stale combat anchor,
- fake route progress.

---

# 9. Phase H7 — Live validation campaign

This is the most important stage after code hardening.

The architecture should now be tested against real dungeon diversity.

## Goal

Move from:

```text
one/few validated dungeons
```

to:

```text
representative multi-expansion coverage
```

## Validation strategy

Do not try to validate every dungeon immediately.

Use tiers.

## Tier 1 — Simple linear dungeons

Purpose:

Validate generic movement, pack, pull, leash, post-combat, recovery.

Suggested examples:

- Ragefire Chasm
- The Deadmines
- Wailing Caverns
- Shadowfang Keep

Success criteria:

- no uncontrolled chain-pulls,
- no route drift,
- no mandatory pack skip,
- healer readiness works,
- wipe recovery works,
- completion/partial reason is accurate.

## Tier 2 — Dense / multi-pack dungeons

Purpose:

Stress pack identity and target selection.

Suggested examples:

- Scarlet Monastery wings
- Razorfen Kraul
- Razorfen Downs
- Zul'Farrak

Focus:

- nearby pack separation,
- fleeing mobs,
- adds,
- patrols,
- overlapping aggro zones.

## Tier 3 — Interaction-heavy dungeons

Purpose:

Validate `DungeonInteractionController`.

Suggested examples should be selected from routes that contain:

- doors,
- gates,
- elevators,
- event triggers,
- NPC interactions.

Record exact interaction types encountered.

## Tier 4 — Complex late-game / expansion dungeons

Purpose:

Validate generality.

Suggested progression:

```text
Classic late-game
→ TBC
→ WotLK normal
→ WotLK heroic
```

Do not begin heroic tuning before normal-mode behavior is reliable.

---

# 10. Failure classification

Every live failure should be classified before code changes are made.

Use categories:

```text
ROUTE
PACK_IDENTITY
TARGETING
PULL
LEASH
READINESS
RECOVERY
WIPE
INTERACTION
BOSS
LEADERSHIP
TELEPORT
UPSTREAM_PLAYERBOT
DATA_ERROR
UNKNOWN
```

For each failure record:

```text
dungeon
map id
route node
pack id
brain state
recovery reason
expected behavior
actual behavior
telemetry excerpt
root cause
fix scope
```

Do not patch blindly from symptoms.

---

# 11. Live validation rule

For each failure:

```text
reproduce
↓
classify
↓
identify root cause
↓
make smallest generic fix
↓
run kernel/unit tests
↓
retest same dungeon scenario
↓
run one regression dungeon
```

Do not introduce dungeon-specific behavior unless a generic solution is not viable.

---

# 12. Validation matrix

Maintain a validation table in the repository.

Suggested structure:

| Dungeon | Route | Pull | Pack ID | Recovery | Wipe | Interaction | Boss | Result |
|---|---|---|---|---|---|---|---|---|
| RFC | PASS | PASS | PASS | PASS | PASS | N/A | PASS | PASS |
| WC | PASS | PASS | PASS | PASS | PASS | N/A | PASS | PASS |
| Deadmines | ... | ... | ... | ... | ... | ... | ... | ... |

Possible result values:

```text
NOT_TESTED
PARTIAL
PASS
FAIL
BLOCKED
```

This table should become the authoritative coverage view.

---

# 13. Telemetry requirements for hardening stage

Ensure logs can reconstruct:

```text
state transition
current route node
current pack
combat anchor
current target plan
party readiness reason
recovery reason
interaction objective
objective requirement
pull retry number
timeout reason
checkpoint
run result
```

Avoid logging every AI tick.

Prefer event/state-change logging.

---

# 14. Performance validation

Before considering hardening complete, verify no new hot-path regressions.

Inspect:

- pack resolution frequency,
- target candidate scans,
- interaction polling,
- recovery polling,
- telemetry flush behavior.

Prefer:

```text
cached
rate-limited
event-driven
state-change-based
```

over continuous expensive scanning.

---

# 15. Release/version plan

Use versioned milestones only after stable groups of changes.

Do not create a release after every commit.

Recommended next milestones:

## v0.6.0-beta.1

Requirements:

- H1 complete,
- H2 complete,
- H3 complete,
- build/tests pass,
- no mandatory-objective skip regression.

Scope:

```text
recovery correctness
stable combat targeting
required/optional objective policy
```

## v0.7.0-beta.1

Requirements:

- H4 complete,
- pack identity tests pass,
- at least two dense-pack dungeons validated.

Scope:

```text
pack identity hardening
target/encounter boundary improvements
```

## v0.8.0-beta.1

Requirements:

- H5 complete,
- interaction controller validated,
- at least one interaction-heavy dungeon passes.

Scope:

```text
door/event/gating support
```

## v0.9.0-rc.2

Requirements:

- H6 complete,
- H7 Tier 1–3 substantially validated,
- no known P0/P1 bugs,
- full build/tests pass,
- cleanup complete.

Scope:

```text
release-candidate hardening
```

## v1.0.0

Requirements:

- stable autonomous trash leadership,
- mandatory objectives handled correctly,
- wipe recovery validated,
- interaction/gating support validated,
- boss foundation validated across representative dungeons,
- broad dungeon validation matrix available,
- no known critical blocker,
- no competing module dependency,
- current Definition of Done satisfied.

---

# 16. Git execution model

Claude may:

- commit stable fixes autonomously,
- push stable commits,
- tag defined milestones,
- create GitHub Releases at defined milestones.

Before each release:

1. working tree clean,
2. full validation passes,
3. version updated where applicable,
4. changelog/release notes prepared,
5. annotated git tag created,
6. tag pushed,
7. GitHub Release created if tooling is available.

Never rewrite a published tag.

---

# 17. Recommended commit sequence

Suggested order:

```text
fix(dungeon-lead): reset recovery timeout on reason change

fix(dungeon-lead): anchor combat target selection

fix(dungeon-lead): enforce mandatory objective failure policy

refactor(dungeon-lead): strengthen runtime pack identity

feat(dungeon-lead): add interaction controller

feat(dungeon-lead): integrate interaction route objectives

fix(dungeon-lead): harden recovery objective reconciliation

test(dungeon-lead): expand dungeon validation coverage
```

Additional commits are acceptable if each remains coherent and stable.

---

# 18. Stop conditions for Claude Code

Claude should continue autonomously through this plan.

Do not stop for:

- compiler errors,
- failing tests,
- bugs introduced by implementation,
- ordinary API discovery,
- failed canary runs,
- telemetry mismatch.

Investigate and fix.

Stop only for genuine blockers such as:

- missing required runtime environment,
- inaccessible dependency/source tree,
- contradictory requirements,
- destructive external action requiring user approval,
- unresolved upstream bug outside this module with no safe workaround.

---

# 19. Final hardening Definition of Done

This hardening stage is complete when:

1. recovery timers reset correctly on recovery-reason transitions,
2. target selection uses stable combat encounter boundaries,
3. mandatory packs cannot be silently skipped,
4. bosses cannot be silently bypassed,
5. pack identity is robust in dense rooms,
6. explicit adds can join active encounter membership,
7. interaction/gating support exists,
8. door/event failures are bounded and observable,
9. route/recovery state remains consistent after failures,
10. live validation covers multiple dungeon archetypes,
11. telemetry can explain all major failure classes,
12. no known P0/P1 defect remains,
13. no competing module is used,
14. full build/tests pass,
15. release candidate can be produced from a clean tree.

---

# 20. Initial execution instruction for Claude Code

Use this as the next autonomous objective:

```text
Read this plan and the repository before editing.

Start with Phase H1.

Work autonomously through the phases in order.

For each phase:

1. inspect the current implementation;
2. identify the exact files and invariants involved;
3. implement the smallest correct change;
4. build;
5. run relevant tests/canaries;
6. diagnose and fix failures;
7. self-review the full diff;
8. commit only a stable passing state;
9. continue to the next phase.

Do not use or copy any competing dungeon-leader module.

Do not replace Playerbot combat AI.

Do not begin another broad architecture rewrite.

The current architecture should be hardened, not replaced.

After code hardening is complete, move into the live validation campaign and fix issues based on reproducible failure classes.
```

---

# END
