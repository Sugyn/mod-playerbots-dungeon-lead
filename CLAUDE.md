# CLAUDE.md
# Autonomous Tank Dungeon Leader — Master Implementation & Execution Plan

Repository: `Sugyn/mod-playerbots-dungeon-lead`

This file is the single authoritative instruction set for Claude Code.

It defines:

- project objective,
- architecture,
- implementation roadmap,
- autonomous execution protocol,
- testing strategy,
- build/debug loop,
- git workflow,
- coding constraints,
- progress tracking,
- stop conditions,
- definition of done.

Do not look for a second planning document unless one is explicitly introduced later.

---

# 1. Mission

Transform `mod-playerbots-dungeon-lead` from a route-following dungeon helper into a deterministic autonomous tank party leader for AzerothCore + mod-playerbots.

The tank leader must be able to:

1. acquire party leadership reliably,
2. evaluate party readiness,
3. decide when to wait,
4. decide when to move,
5. navigate toward the next dungeon objective,
6. identify the intended enemy pack,
7. prepare and initiate a pull,
8. establish combat safely,
9. prevent unsafe chain pulls,
10. coordinate target priorities,
11. detect pack completion,
12. evaluate post-combat readiness,
13. recover from party problems,
14. recover from wipes,
15. restore route progress from a safe checkpoint,
16. handle boss preparation and positioning,
17. finish the dungeon cleanly,
18. return leadership reliably.

The leader is an orchestration system.

It is NOT a replacement for Playerbot class combat AI.

---

# 2. Non-negotiable constraints

These rules are mandatory.

## 2.1 No competing module

Do NOT:

- use,
- import,
- copy,
- vendor,
- adapt,
- depend on,
- reverse engineer,
- mirror,
- or use as a fallback

any competing or alternative dungeon-leader module.

The implementation must remain self-contained inside:

`mod-playerbots-dungeon-lead`

Allowed dependencies:

- AzerothCore APIs already available to modules,
- mod-playerbots APIs already available to this module,
- standard C++ library,
- existing libraries already accepted by the target project,
- code already present in this repository.

If functionality is needed, implement it in this module using upstream AzerothCore/mod-playerbots APIs.

## 2.2 Do not replace Playerbot combat AI

The dungeon leader decides the group objective.

Examples:

- wait for healer,
- move to pull point,
- pull pack 7,
- keep combat around tank anchor,
- mark target A as primary,
- recover party,
- resume route.

Playerbot AI remains responsible for:

- spell selection,
- tank rotation,
- DPS rotation,
- healing rotation,
- taunt,
- threat abilities,
- defensives,
- interrupts already owned by Playerbot AI,
- class-specific execution.

Do not turn `DungeonLeadBrain` into a spell rotation engine.

## 2.3 No massive rewrite

Do not rewrite the whole module in one change.

Work phase by phase.

Every completed phase must leave the repository:

- compiling,
- testable,
- reviewable,
- and in a valid working state.

## 2.4 No blocking behavior in game hot paths

Do not add:

- `sleep`,
- blocking waits,
- synchronous polling loops,
- synchronous network operations,
- synchronous database queries

inside hot AI/game paths.

## 2.5 No synchronous DB reads in AI ticks

No database work inside:

- AI update loop,
- movement tick,
- combat tick,
- readiness evaluation,
- pull evaluation.

Static data must be cached or loaded outside hot paths.

## 2.6 No unbounded retries

All retry logic must have:

- retry count or timeout,
- explicit failure state,
- logging.

Never implement endless retry loops.

## 2.7 Queued async work is not success

If an AzerothCore/mod-playerbots operation is asynchronous:

`queued != completed`

Success must be confirmed using actual game state.

Example:

Requesting group leadership transfer is not enough.

Success is confirmed only when:

```cpp
group->GetLeaderGUID() == expectedLeaderGuid
```

## 2.8 Fail safe

If the leader cannot safely decide what to do:

prefer:

```text
STOP
WAIT
RECOVER
ABORT
```

over:

```text
GUESS
CONTINUE
PULL
```

Never pull another pack while state is ambiguous.

## 2.9 One source of truth

Do not leave:

- two party readiness engines,
- two leader state machines,
- two active route decision systems,
- duplicate ownership of the same lifecycle.

During refactoring, transitional duplication may exist briefly, but must be removed before the phase is considered complete.

---

# 3. Autonomous execution mode

Claude Code is expected to work autonomously.

Do not stop after editing code.

The normal autonomous loop is:

```text
READ
  ↓
ANALYZE
  ↓
IMPLEMENT
  ↓
BUILD
  ↓
TEST
  ↓
FAIL?
  ├─ YES → diagnose → fix → rebuild/retest
  └─ NO
       ↓
SELF-REVIEW
       ↓
FIX REVIEW FINDINGS
       ↓
FINAL VALIDATION
       ↓
COMMIT
       ↓
UPDATE PROGRESS
       ↓
NEXT PHASE
```

Compiler errors and test failures are normal iteration signals.

They are NOT reasons to ask the user what to do.

Claude must investigate and repair ordinary implementation problems autonomously.

---

# 4. When Claude may stop and ask for help

Claude should stop only for a genuine blocker that cannot reasonably be solved from:

- repository source,
- compiler output,
- test output,
- runtime logs,
- AzerothCore source,
- mod-playerbots source,
- existing configuration,
- git history.

Valid blockers include:

1. required external dependency unavailable,
2. required source tree missing,
3. permissions prevent required build/test,
4. contradictory product requirements requiring human choice,
5. destructive operation outside the repository is required,
6. environment failure unrelated to code cannot be repaired locally,
7. required runtime environment does not exist and no meaningful substitute validation is possible.

Do NOT stop for:

- compiler errors,
- linker errors,
- failing tests,
- wrong API assumptions,
- broken code introduced by Claude,
- formatting issues,
- ordinary merge conflicts in Claude's own changes,
- incomplete implementation,
- failing canary scenarios.

Fix those.

---

# 5. Source verification rule

Never invent AzerothCore or mod-playerbots APIs.

If an API is uncertain:

1. search the actual source tree,
2. inspect declaration,
3. inspect implementation if needed,
4. inspect call sites,
5. then use it.

Do not assume a method exists because its name seems plausible.

---

# 6. Target architecture

The target architecture is:

```text
                     +----------------------+
                     | DungeonLeadSession   |
                     +----------+-----------+
                                |
                                v
                     +----------------------+
                     | DungeonLeadBrain     |
                     +----------+-----------+
                                |
          +---------------------+----------------------+
          |                     |                      |
          v                     v                      v
+------------------+  +------------------+  +----------------------+
| DungeonPartyState|  | DungeonRouteMgr  |  | DungeonPullController|
+------------------+  +------------------+  +----------------------+
          |                     |                      |
          +---------------------+----------------------+
                                |
                                v
                     +----------------------+
                     | DungeonTargetManager |
                     +----------+-----------+
                                |
                                v
                       Existing Playerbot AI

       DungeonRecoveryController <---- DungeonLeadBrain ----> Telemetry
```

Responsibilities must stay separate.

---

# 7. Component responsibilities

## 7.1 DungeonLeadSession

Owns lifecycle information for one active dungeon-leading session.

Possible responsibilities:

- session identifier,
- leader bot GUID,
- original leader GUID,
- map/instance identity,
- start timestamp,
- lifecycle status,
- references/IDs for current route/session data.

It must not contain all decision logic.

## 7.2 DungeonLeadBrain

The main orchestration state machine.

Responsible for:

- current leader state,
- state transitions,
- current group objective,
- high-level decisions,
- retry counters,
- state timeouts,
- route objective selection,
- deciding when recovery is required.

It must not implement class rotations.

## 7.3 DungeonPartyState

Canonical source of party readiness observations.

It observes and normalizes.

It does not decide route progression.

## 7.4 DungeonRouteMgr

Responsible for dungeon route state.

It knows:

- current node,
- next node,
- route completion,
- checkpoint restoration,
- typed route objectives.

It does not decide if the party is ready.

## 7.5 DungeonPullController

Responsible for pull lifecycle.

It coordinates:

- approach,
- readiness gate,
- target preparation,
- pull initiation,
- establishment,
- failure/timeout.

It does not implement tank spell rotation.

## 7.6 DungeonTargetManager

Responsible for stable target priorities.

It may expose:

- primary target,
- secondary target,
- CC candidates,
- raid markers where supported.

## 7.7 DungeonRecoveryController

Responsible for deterministic recovery flows.

It must model:

- reason,
- entry,
- recovery action,
- success condition,
- timeout,
- escalation.

## 7.8 Telemetry

Responsible for structured diagnostic output.

It must make autonomous behavior debuggable.

---

# 8. Core leader state machine

Introduce an explicit enum similar to:

```cpp
enum class DungeonLeadState
{
    Idle,
    Starting,
    WaitingReady,
    MovingToPull,
    PrePull,
    Pulling,
    Combat,
    PostCombat,
    Recovery,
    WipeRecovery,
    RouteRecovery,
    BossPrep,
    BossCombat,
    Completing,
    Stopping,
    Stopped,
    Aborted
};
```

There must be exactly one authoritative current state.

Do not encode primary lifecycle using a collection of loosely related booleans.

Normal flow:

```text
Idle
  ↓
Starting
  ↓
WaitingReady
  ↓
MovingToPull
  ↓
PrePull
  ↓
Pulling
  ↓
Combat
  ↓
PostCombat
  ↓
WaitingReady
  ↓
MovingToPull
```

Exceptional transitions:

```text
active state
  ├─> Recovery
  ├─> WipeRecovery
  ├─> RouteRecovery
  ├─> Stopping
  └─> Aborted
```

Boss flow:

```text
MovingToPull
  ↓
BossPrep
  ↓
BossCombat
  ↓
PostCombat
```

All state changes must go through one transition function.

Example:

```cpp
void DungeonLeadBrain::TransitionTo(
    DungeonLeadState next,
    DungeonLeadTransitionReason reason);
```

Every transition must be logged.

---

# 9. Party state model

Create a canonical snapshot.

Example:

```cpp
struct DungeonPartyMemberState
{
    ObjectGuid guid;

    bool exists = false;
    bool alive = false;
    bool ghost = false;
    bool online = false;

    bool sameMap = false;
    bool inCombat = false;

    bool drinking = false;
    bool resurrecting = false;

    uint8 healthPct = 0;
    uint8 manaPct = 0;

    float distanceToLeader = 0.0f;
};
```

Aggregate snapshot:

```cpp
struct DungeonPartySnapshot
{
    bool healerPresent = false;
    bool healerAvailable = false;

    bool allRequiredMembersAlive = false;
    bool allRequiredMembersSameMap = false;

    bool anyMemberInCombat = false;
    bool anyMemberDrinking = false;
    bool resurrectionInProgress = false;

    uint8 healerManaPct = 0;

    uint32 nearbyMemberCount = 0;
    float furthestMemberDistance = 0.0f;

    bool fragmented = false;
};
```

The exact shape may be adjusted to actual APIs.

Central evaluation:

```cpp
DungeonPartySnapshot DungeonPartyState::Evaluate(...);
```

Do not duplicate the same readiness checks in multiple unrelated code paths.

Suggested readiness status:

```cpp
enum class DungeonReadyStatus
{
    Ready,
    HealerUnavailable,
    MemberDead,
    MemberMissing,
    DifferentMap,
    Drinking,
    Resurrection,
    LowHealerMana,
    Fragmented,
    PartyInCombat
};
```

---

# 10. Route model

Move from raw coordinates toward typed route objectives.

Start simple.

Do not over-engineer a fully generic graph engine before needed.

Suggested type:

```cpp
enum class DungeonRouteNodeType
{
    Travel,
    Pull,
    TankPosition,
    SafeSpot,
    Boss,
    Door,
    Interaction,
    Recovery,
    End
};
```

Node example:

```cpp
struct DungeonRouteNode
{
    uint32 id = 0;

    DungeonRouteNodeType type;

    Position position;

    uint32 packId = 0;
    uint32 bossId = 0;

    float arrivalRadius = 0.0f;

    bool optional = false;
};
```

Initial implementation may remain linear:

```text
node 1
node 2
node 3
...
```

Add branching only when an actual route requires it.

The leader must know both:

- where it is going,
- why it is going there.

---

# 11. Pack model

Introduce logical enemy packs.

Suggested:

```cpp
struct DungeonPack
{
    uint32 id = 0;

    Position pullPosition;
    Position tankPosition;

    float aggroRadius = 0.0f;
    float combatLeashRadius = 0.0f;

    bool bossPack = false;
    bool optional = false;

    std::vector<uint32> expectedEntries;
};
```

Static route data should prefer creature entries or other stable identifiers.

Do not persist runtime GUIDs as static route definitions.

Suggested runtime state:

```cpp
enum class DungeonPackState
{
    Unknown,
    Available,
    Engaged,
    Cleared,
    Skipped
};
```

Route progress after a pull must depend on pack completion.

Do not advance merely because the tank reached a coordinate.

---

# 12. Pull controller

Suggested pull state:

```cpp
enum class DungeonPullState
{
    None,
    Approaching,
    WaitingParty,
    Marking,
    Initiating,
    Establishing,
    Established,
    Failed
};
```

Flow:

```text
Approaching
  ↓
WaitingParty
  ↓
Marking
  ↓
Initiating
  ↓
Establishing
  ├─> Established
  └─> Failed
```

Minimum pull preconditions:

- tank alive,
- healer available,
- required party members alive,
- required party members on same map,
- no unresolved previous combat,
- healer mana above configured threshold,
- party cohesion acceptable,
- current pack successfully resolved.

All stages require bounded timeout where relevant.

A failed pull transitions to recovery.

Never silently route-advance after a failed pull.

---

# 13. Combat anchor and leash

Each pull must establish a combat anchor.

Preferred anchor:

```text
configured pack tankPosition
```

or a validated equivalent.

The tank may move locally during combat.

The leader must prevent route progression or excessive chase away from the combat area.

Goal:

A fleeing mob must not cause the tank to run into the next pack.

Suggested configuration:

```cpp
struct DungeonLeadConfig
{
    float partySoftRange;
    float partyHardRange;

    float combatLeashRadius;

    uint8 minHealerManaPct;

    uint32 leadershipAcquireTimeoutMs;
    uint32 leadershipReturnTimeoutMs;

    uint32 pullInitiateTimeoutMs;
    uint32 pullEstablishTimeoutMs;

    uint32 recoveryTimeoutMs;
};
```

Final values must be configurable.

---

# 14. Party cohesion

Use party snapshot.

Suggested semantic levels:

```text
OK
SOFT_WARNING
HARD_STOP
LOST_MEMBER
```

Typical behavior:

Out of combat:

- soft warning → do not initiate next pull,
- hard stop → stop advancing,
- lost member → recovery/regroup.

During combat:

- do not abandon the active pack because a DPS moved away,
- healer loss has high priority,
- do not route-advance.

Do not hard-code final distance values into business logic.

---

# 15. Target planning

Create a stable target plan.

Example:

```cpp
struct DungeonTargetPlan
{
    ObjectGuid primary;
    ObjectGuid secondary;

    ObjectGuid cc1;
    ObjectGuid cc2;
};
```

Initial generic target priority may consider:

- boss,
- dangerous caster,
- healer-type NPC,
- elite,
- normal melee.

Keep the first implementation deterministic and simple.

Raid markers may be used if supported:

```text
Skull = primary
Cross = secondary
Moon/Square = CC
```

Internal target plan remains authoritative.

---

# 16. Post-combat gate

Combat end must not immediately advance the route.

Enter:

```text
PostCombat
```

Evaluate:

- remaining hostile mobs,
- pack completion,
- party deaths,
- healer status,
- healer mana,
- drinking,
- resurrection,
- unexpected combat,
- route state.

Possible next states:

```text
WaitingReady
Recovery
MovingToPull
BossPrep
Completing
```

There must be a deliberate gate between two pulls.

---

# 17. Recovery model

Suggested reasons:

```cpp
enum class DungeonRecoveryReason
{
    None,
    HealerDead,
    PartyMemberDead,
    PartyFragmented,
    DifferentMap,
    PullFailed,
    RouteLost,
    UnexpectedCombat,
    TankDead,
    Wipe,
    LeadershipLost,
    Stuck
};
```

Every recovery flow must define:

1. entry condition,
2. action,
3. success condition,
4. timeout,
5. failure escalation.

Never implement generic endless retry.

---

# 18. Wipe recovery and checkpoints

Introduce:

```cpp
struct DungeonCheckpoint
{
    uint32 routeNodeId = 0;
    uint32 lastClearedPackId = 0;
    uint32 lastBossId = 0;
};
```

Update checkpoint only at a confirmed safe milestone.

Examples:

- pack fully cleared,
- boss confirmed dead,
- explicit recovery node reached.

Wipe recovery flow:

```text
detect wipe
  ↓
WipeRecovery
  ↓
resurrection / wait for recovery
  ↓
regroup
  ↓
restore checkpoint
  ↓
verify current world state
  ↓
WaitingReady
```

Never skip an uncleared pack only because route position advanced before the wipe.

---

# 19. Leadership lifecycle

This is a critical correctness area.

## Acquisition

Do not treat queueing a leader change as success.

Flow:

```text
request leadership
  ↓
Starting
  ↓
poll/observe actual group state
  ├─ expected leader confirmed → WaitingReady
  ├─ timeout → retry if budget remains
  └─ retry exhausted → Aborted
```

Success condition:

```cpp
group->GetLeaderGUID() == tankBotGuid
```

Use bounded retries.

## Return leadership

Stopping must also be confirmed.

Flow:

```text
Stopping
  ↓
request leadership return
  ↓
observe actual group state
  ├─ confirmed → Stopped
  ├─ timeout → retry if budget remains
  └─ exhausted → explicit failure state/log
```

Do not destroy all session state before return has been confirmed or safely abandoned according to policy.

---

# 20. Healer availability correctness

Healer availability must be integrated into the real readiness decision path.

The leader must stop advancing when healer is:

- dead,
- ghost,
- not available,
- on another map,
- otherwise unable to support the next pull.

This is different from low mana.

Do not rely on `HealerManaLow()` to represent all healer-unavailable situations.

---

# 21. Test-party / teleport synchronization

If test utilities teleport party members:

queued teleport is not confirmed teleport.

Before session starts, verify required members are:

- on target map,
- in correct instance,
- in acceptable formation,
- in expected preparation state.

Flow:

```text
create party
  ↓
request teleport
  ↓
wait for actual teleport confirmation
  ↓
verify formation/instance
  ↓
start leader session
```

Avoid nondeterministic session startup.

---

# 22. Boss mode

Boss-specific behavior comes after trash leadership is stable.

Initial boss strategy should stay small.

Example:

```cpp
struct BossStrategy
{
    uint32 bossEntry = 0;

    Position tankPosition;

    float preferredFacing = 0.0f;
    float combatLeashRadius = 0.0f;
};
```

Initial boss implementation may support:

- pull position,
- tank position,
- preferred facing,
- boss leash.

Do not build a giant universal boss scripting engine until actual encounters require additional mechanics.

---

# 23. Telemetry

Autonomous behavior must be observable.

Suggested events:

```text
session_start
session_stop

leadership_request
leadership_confirmed
leadership_failed

state_transition

party_ready
party_not_ready

route_node_enter
route_node_complete

pull_start
pull_established
pull_failed

combat_start
combat_end

pack_cleared

recovery_start
recovery_complete
recovery_failed

wipe_detected
checkpoint_restore

boss_prep
boss_combat

session_complete
session_abort
```

Suggested fields:

```text
timestamp
session_id
map_id
instance_id
leader_guid
state
route_node_id
pack_id
reason
```

Avoid expensive file open/write/flush/close operations on every hot-path event if existing logging infrastructure can handle this better.

Do not hold a global lock around slow I/O unnecessarily.

---

# 24. Configuration

Keep configuration focused.

Example:

```ini
DungeonLead.Enable = 1

DungeonLead.MinHealerManaPct = 40

DungeonLead.PartySoftRange = 25
DungeonLead.PartyHardRange = 40

DungeonLead.CombatLeashRadius = 15

DungeonLead.LeadershipAcquireTimeoutMs = 5000
DungeonLead.LeadershipReturnTimeoutMs = 5000

DungeonLead.PullInitiateTimeoutMs = 5000
DungeonLead.PullEstablishTimeoutMs = 8000

DungeonLead.RecoveryTimeoutMs = 30000
```

Every config value must have:

- default,
- validation,
- description/comment.

Do not expose dozens of speculative settings before they are needed.

---

# 25. Coding standards

Follow existing repository style first.

Additional rules:

- prefer scoped enums,
- prefer RAII,
- avoid raw ownership,
- avoid mutable global state,
- make ownership explicit,
- use bounded timeouts,
- no arbitrary sleep,
- no synchronous DB query in hot AI paths,
- no invented upstream API,
- no duplicate source of truth,
- no giant god class,
- no giant switch mixed with unrelated low-level behavior,
- keep responsibilities narrow,
- keep diffs reviewable.

New compiler warnings introduced by Claude should be fixed before commit unless there is a documented unavoidable reason.

---

# 26. Implementation roadmap

Claude must execute phases in order unless a later phase is required to complete an earlier one safely.

Do not skip foundational phases.

---

# Phase 0 — Baseline

Goal:

Understand current repository and establish reproducible validation.

Tasks:

1. inspect repository layout,
2. identify all dungeon lead source files,
3. identify build files,
4. identify config and SQL,
5. identify test/canary utilities,
6. build current baseline,
7. record existing warnings,
8. identify relevant runtime logs,
9. inspect git status.

Do not modify functional behavior.

Record baseline inside the progress section at the bottom of this file.

Acceptance criteria:

- current baseline build status known,
- test/canary command known,
- relevant code ownership understood.

---

# Phase 1 — Correctness and lifecycle

Goal:

Make the existing leader lifecycle deterministic before adding intelligence.

Implement:

1. healer-unavailable handling in real readiness path,
2. explicit `Starting`,
3. confirmed leadership acquisition,
4. bounded leadership retry/timeout,
5. explicit `Stopping`,
6. confirmed leadership return,
7. bounded return retry/timeout,
8. regression/canary scenarios.

Tests must include:

- healer alive and nearby,
- healer dead,
- healer ghost,
- healer on another map,
- healer low mana,
- leader transfer success,
- queued leader transfer never confirms,
- handback queue failure,
- handback retry succeeds.

Acceptance criteria:

No later architecture work begins until lifecycle behavior is deterministic.

---

# Phase 2 — DungeonPartyState

Goal:

Create one canonical readiness snapshot.

Implement:

- `DungeonPartyState.h`
- `DungeonPartyState.cpp`
- member snapshot,
- aggregate snapshot,
- readiness status,
- migration of existing duplicated checks.

Acceptance criteria:

One authoritative readiness evaluation path exists.

---

# Phase 3 — DungeonLeadBrain

Goal:

Introduce explicit state-machine orchestration.

Implement:

- `DungeonLeadBrain.h`
- `DungeonLeadBrain.cpp`
- authoritative current state,
- `TransitionTo`,
- state timeouts,
- state transition telemetry,
- migration of existing high-level leader decisions.

Acceptance criteria:

Primary leader behavior is controlled by one explicit state machine.

---

# Phase 4 — Typed route nodes

Goal:

Move from coordinate-only logic to objective-aware route logic.

Implement route node types:

- Travel,
- Pull,
- TankPosition,
- SafeSpot,
- Boss,
- Door,
- Interaction,
- Recovery,
- End.

Preserve compatibility with current route data where practical.

Acceptance criteria:

Brain can determine both destination and route intent.

---

# Phase 5 — Pack model

Goal:

Represent pullable enemy groups explicitly.

Implement:

- `DungeonPack`,
- runtime pack resolution,
- pack state,
- cleared detection.

Acceptance criteria:

Leader can answer:

- which pack is current,
- whether it is available,
- whether it is engaged,
- whether it is cleared.

---

# Phase 6 — DungeonPullController

Goal:

Create deterministic pull lifecycle.

Implement:

- Approaching,
- WaitingParty,
- Marking,
- Initiating,
- Establishing,
- Established,
- Failed.

Add bounded timeouts.

Acceptance criteria:

Failed pull cannot silently advance route.

---

# Phase 7 — Combat anchor and leash

Goal:

Prevent unsafe chase/chain-pull behavior.

Implement:

- per-pull combat anchor,
- configurable leash,
- route movement suppression during unresolved combat,
- return/containment behavior compatible with existing Playerbot AI.

Acceptance criteria:

A fleeing mob cannot cause route progression into the next pack.

---

# Phase 8 — Party cohesion

Goal:

Make group spacing a first-class readiness signal.

Implement semantic states:

- OK,
- SoftWarning,
- HardStop,
- LostMember.

Acceptance criteria:

Tank does not start a new pull when party is materially fragmented.

---

# Phase 9 — Target manager

Goal:

Create stable target priorities.

Implement:

- primary,
- secondary,
- initial CC candidates,
- optional raid markers.

Acceptance criteria:

Party has a stable target plan for current pack.

---

# Phase 10 — Post-combat gate

Goal:

Stop immediate uncontrolled pull chaining.

Implement explicit `PostCombat`.

Evaluate:

- hostiles,
- deaths,
- mana,
- drinking,
- resurrection,
- pack completion,
- route readiness.

Acceptance criteria:

There is always a deliberate readiness decision between two packs.

---

# Phase 11 — Recovery controller

Goal:

Create deterministic recovery behavior.

Implement:

- recovery reason enum,
- per-reason flow,
- timeout,
- success condition,
- escalation.

Acceptance criteria:

Recovery does not contain indefinite loops.

---

# Phase 12 — Wipe and checkpoints

Goal:

Recover dungeon progression after wipe.

Implement:

- checkpoint state,
- safe checkpoint update rules,
- wipe detection,
- checkpoint restoration,
- route reconciliation.

Acceptance criteria:

Leader does not skip uncleared content after wipe.

---

# Phase 13 — Test party synchronization

Goal:

Make test/canary startup deterministic.

Implement:

- teleport confirmation,
- instance confirmation,
- formation confirmation,
- delayed session start until ready.

Acceptance criteria:

Session never starts merely because teleport was requested.

---

# Phase 14 — Boss foundation

Goal:

Support controlled boss pull and tank positioning.

Implement:

- boss route node integration,
- boss strategy structure,
- tank position,
- facing,
- leash.

Acceptance criteria:

Generic boss entry can be controlled without replacing class combat AI.

---

# Phase 15 — Telemetry hardening

Goal:

Make autonomous runs diagnosable.

Implement structured event logging and remove unnecessary hot-path I/O cost.

Acceptance criteria:

State transitions and failure paths can be reconstructed from logs.

---

# Phase 16 — Cleanup

Goal:

Remove transitional architecture debt.

Remove:

- obsolete readiness code,
- dead legacy branches,
- unused flags,
- duplicate state tracking,
- old decision code no longer authoritative.

Acceptance criteria:

Only one active leader decision engine remains.

---

# 27. Required test matrix

Maintain coverage for at least these scenarios.

## Lifecycle

- tank already leader,
- human/player currently leader,
- acquisition succeeds,
- acquisition times out,
- leadership changes unexpectedly,
- clean stop,
- handback failure,
- handback retry succeeds.

## Party readiness

- all ready,
- healer low mana,
- healer dead,
- healer ghost,
- healer different map,
- DPS dead,
- DPS far away,
- member drinking,
- resurrection in progress,
- party already in combat.

## Route

- normal travel,
- pull node arrival,
- invalid node,
- lost route,
- end of route.

## Pull

- successful pull,
- missing target,
- pull fails to establish combat,
- accidental second pack,
- party fragments before initiation.

## Combat

- normal clear,
- fleeing mob beyond leash,
- unexpected add,
- healer dies,
- DPS dies,
- tank dies.

## Recovery

- dead DPS,
- healer death,
- lost party member,
- wipe,
- checkpoint restoration,
- route reconciliation.

## Test-party flow

- all teleport correctly,
- one remains on old map,
- delayed teleport,
- instance mismatch.

---

# 28. Build and test protocol

Claude must determine the actual repository-specific commands during Phase 0.

Once discovered, record them in the Progress section.

For every implementation batch:

1. run targeted build if available,
2. fix compile errors,
3. rerun until clean,
4. run relevant targeted tests/canaries,
5. fix failures,
6. run broader validation when appropriate,
7. run `git diff --check`,
8. inspect `git diff`.

Before a phase commit:

- build must pass,
- relevant tests must pass,
- introduced warnings must be addressed,
- diff must be reviewed.

Do not commit intentionally broken intermediate work.

---

# 29. Debugging protocol

When build/test/runtime validation fails:

1. capture exact failure,
2. identify first relevant root cause,
3. inspect source/API involved,
4. make minimal fix,
5. rerun the smallest validation proving the fix,
6. rerun phase-level validation,
7. continue until stable.

Do not apply random speculative changes.

Do not mask failures just to get green output.

Do not remove assertions/tests merely because they expose a bug unless the test itself is proven invalid.

---

# 30. Self-review protocol

Before every commit, review the full phase diff.

Check for:

- incorrect ownership,
- lifecycle race,
- state transition hole,
- unbounded retry,
- hidden fallback,
- duplicate logic,
- accidental synchronous I/O,
- accidental DB work in hot path,
- wrong API assumptions,
- route advancement during unresolved combat,
- loss of healer readiness protection,
- stale state after wipe,
- missing timeout,
- error path that destroys recovery state too early,
- unused code,
- logging gaps,
- unrelated edits.

Fix findings before commit.

---

# 31. Git workflow

Before starting a phase:

```bash
git status
```

Understand existing user changes.

Never overwrite unrelated user work.

Do not revert user changes unless explicitly instructed.

Recommended commit scope:

```text
one completed logical phase
or
one independently stable sub-phase
```

Suggested commit style:

```text
fix(dungeon-lead): confirm leadership lifecycle
refactor(dungeon-lead): centralize party readiness
refactor(dungeon-lead): introduce leader state machine
feat(dungeon-lead): add typed route objectives
feat(dungeon-lead): add pack model
feat(dungeon-lead): add pull controller
feat(dungeon-lead): add combat leash
feat(dungeon-lead): add recovery checkpoints
```

Do not commit:

- failed experiments,
- temporary debug junk,
- broken builds,
- unrelated generated files.

---

# 32. Progress tracking

This file also contains the project progress state.

Claude must update the section:

`PROJECT PROGRESS`

after every completed phase.

The update must include:

- phase status,
- commit hash if committed,
- build status,
- tests run,
- important implementation notes,
- remaining risk,
- next phase.

Do not create a separate progress file unless explicitly instructed.

This allows a new Claude Code session to continue without relying on conversation memory.

---

# 33. Autonomous continuation rule

At startup:

1. read this entire file,
2. inspect `PROJECT PROGRESS`,
3. run `git status`,
4. verify repository reality matches recorded progress,
5. identify first unfinished phase,
6. continue from there.

If progress file and code disagree:

repository state is authoritative.

Correct the progress section before continuing.

Continue autonomously through subsequent phases while:

- the current phase passes validation,
- no genuine blocker exists,
- architecture constraints remain satisfied.

Do not ask permission between ordinary phases.

---

# 34. Scope control

Do not opportunistically redesign unrelated Playerbot/AzerothCore systems.

If a discovered upstream problem blocks this module:

- isolate workaround inside this module where reasonable,
- document the limitation,
- avoid broad upstream refactoring unless absolutely required.

Do not turn this project into a general Playerbot rewrite.

---

# 35. Performance rules

Frequently executed leader logic must be cheap.

Prefer:

- cached snapshots,
- stable IDs,
- rate-limited expensive evaluation,
- event-driven updates where appropriate,
- compact state transitions.

Avoid:

- full world scans every AI tick,
- repeated expensive path calculations without need,
- repeated DB queries,
- repeated file open/close in hot path,
- global locks around expensive work.

Measure or reason explicitly about any operation added to a hot loop.

---

# 36. Thread-safety rules

Prefer session-local state.

Avoid mutable globals.

When shared state is necessary:

- document owner,
- protect mutation,
- minimize lock scope,
- do not perform slow I/O under long-held lock,
- avoid calling arbitrary external APIs while holding a lock where re-entry/deadlock risk exists.

Do not claim thread safety without understanding actual calling context.

---

# 37. Definition of done — autonomous trash leadership

The generic autonomous tank leader is not complete until all are true:

1. leadership acquisition is confirmed,
2. leadership return is confirmed or explicitly failed safely,
3. party readiness is centralized,
4. healer unavailability blocks new pulls,
5. state machine is authoritative,
6. route objectives are typed,
7. current pack is explicit,
8. pull lifecycle is explicit,
9. pull failure enters recovery,
10. combat anchor exists,
11. leash prevents unsafe chain pull,
12. route does not advance during unresolved combat,
13. pack completion is confirmed,
14. post-combat gate exists,
15. dead members trigger recovery,
16. wipe triggers checkpoint logic,
17. checkpoint restoration works,
18. test-party startup is deterministic,
19. telemetry explains state transitions,
20. no competing module is used,
21. no duplicate active leader engine remains.

---

# 38. Definition of done — project

The project is complete when:

- all applicable phases are complete,
- build passes,
- relevant tests/canaries pass,
- autonomous trash leadership meets its definition of done,
- boss foundation works for intended initial encounters,
- recovery paths are bounded,
- no competing module dependency exists,
- code cleanup is complete,
- telemetry is sufficient to diagnose failures,
- repository is left in a clean and reviewable state.

---

# 39. Initial execution instruction

When starting from an unimplemented repository state:

Do NOT try to implement everything at once.

Start with Phase 0.

Then Phase 1.

Proceed sequentially.

The first real code objective is:

```text
stabilize leadership lifecycle and healer readiness
```

Do not begin pack intelligence or boss logic before foundational lifecycle work is proven stable.

---

# 40. Compact autonomous operating instruction

If context is constrained, preserve at minimum this operating rule:

```text
Read CLAUDE.md.
Use PROJECT PROGRESS to find the first unfinished phase.
Inspect existing source before changing it.
Implement the smallest coherent change for that phase.
Never use a competing dungeon-leader module.
Never replace Playerbot combat AI.
Never invent upstream APIs.
Build after implementation.
Treat compiler/test failures as problems to diagnose and fix, not reasons to stop.
Run relevant tests/canaries.
Self-review the full diff.
Fix review findings.
Commit only a stable passing state.
Update PROJECT PROGRESS.
Continue to the next phase.
Stop only for a genuine external blocker or contradictory requirement that cannot be resolved from source, build output, tests, logs, or upstream code.
```

---

# PROJECT PROGRESS

This section is intentionally machine-maintained by Claude Code.

Claude must update it as implementation progresses.

## Repository baseline

Status: DONE (2026-10-03)

Build command (on the game server `acore-clean`, module synced from this repo first):
`rsync -rc src/ acore-clean:/home/prgadm/azerothcore/modules/mod-dungeon-lead/src/ && ssh acore-clean "cd /home/prgadm/azerothcore/build && cmake --build . --target worldserver -- -j12"`
(a new/removed .cpp needs a CMake reconfigure first - sources are globbed at configure time)

Targeted test/canary command:
`tools/run_tests.sh` - header-only decision kernels (`src/DungeonLead/DungeonLeadKernels.h`), no worldserver needed.
Live canary: `.dungeonlead canarytest <lfgId>` (GM/console) or the automatic canary
(`AiPlayerbot.DungeonLead.CanaryEnabled`), results in `DungeonLeadRuns.csv` / `DungeonLeadSessions.csv`.

Full validation command:
`tools/run_tests.sh` + server build above + `cmake --build . --target install` +
`sudo systemctl restart ac-worldserver`, then check `env/dist/bin/Playerbots.log` and `Errors.log`
for `[DungeonLead]` lines.

Relevant build directory:
`acore-clean:/home/prgadm/azerothcore/build` (clang++, MODULES=static, worldserver installs to `env/dist/bin`)

Notes:
- Verify working tree before any edit.
- Preserve unrelated user changes. mod-playerbots on the server has uncommitted user WIP plus the
  disabled old patch (`MODULE-MIGRATION-DISABLED`) - never reset or clean it.
- No compiler on the dev box besides g++; clang++ only on the server. Unit tests use g++ locally.

---

## Phase 0 — Baseline

Status: DONE

Commit:
`docs(dungeon-lead): add CLAUDE.md plan and baseline`

Validation:
- Build: PASS - clean rebuild of all module sources, 0 warnings, 0 errors (HEAD fb50d4a)
- Tests: no tests existed before Phase 1

Notes:
- Layout: `src/` (module entry `DungeonLeadModule.cpp`, loader `mod_dungeon_lead_loader.cpp`,
  `DungeonLeadCommandScript.cpp`, config singleton `DungeonLeadConfig.h`, registry access
  `DungeonLeadAccess.h`, overrides `DungeonLeadOverrides.h`), `src/DungeonLead/` (actions,
  strategy/multiplier, triggers, route manager + per-bot session state, canary, test bot pool),
  `conf/mod-dungeon-lead.conf.dist`, `sql/playerbots_dungeon_route.sql`, `data/` + `tools/`
  (route data and validators, CI `.github/workflows/validate-routes.yml`).
- Session ownership: `DungeonRouteMgr::states` (one entry per session), driven by
  `DungeonLead::GuardActiveSessions()` every 2 s from the module's WorldScript::OnUpdate.
- Runtime logs: `Playerbots.log` (`[DungeonLead]`), `Errors.log`, `DungeonLeadSessions.csv`
  (events), `DungeonLeadRuns.csv` (one row per run), `DungeonLeadDebug.log` (debug mode).

Remaining:
- none

---

## Phase 1 — Correctness and lifecycle

Status: DONE (2026-10-03)

Commit:
`fix(dungeon-lead): confirm leadership lifecycle`

Validation:
- Build: PASS (server, clang, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 24 checks (healer alive/dead/ghost/other map/low mana,
  transfer success, queued never confirms -> retry -> give up, handback enqueue failure,
  handback retry succeeds, target gone / third party -> abandon)
- Live: temporary server-only harness (not committed) on a 2-bot group: Starting with
  IsOn()=false until confirmed, acquire confirmed after 166 ms on attempt 1, handback
  confirmed after ~2 s on attempt 1. Harness removed and server redeployed clean.

Notes:
- `DungeonLeadLifecycle {Starting, Active, Stopping}` in `DungeonLeadState`; decisions in
  `DungeonLeadKernel::DecideLeadership` / `EvaluateHealer` (pure, unit-tested).
- Starting applies no strategies; give-up drops the session (run row
  `leadership_not_acquired`). Stopping keeps a minimal entry until handback is observed.
- `DungeonLead::HasSession()` (any stage) replaces `IsOn()` for "busy" checks.
- Config: `LeadershipAcquireTimeoutSeconds`, `LeadershipReturnTimeoutSeconds` (5, range 1-60),
  `LeadershipMaxAttempts` (3, range 1-10).
- Not exercisable live: queue-full and never-confirming transfers (covered by unit tests only).

Remaining:
- none

---

## Phase 2 — DungeonPartyState

Status: DONE (2026-10-03)

Commit:
`refactor(dungeon-lead): single party readiness snapshot`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 46 checks (22 new readiness cases: each status, walk vs
  pull, priority order, no master, no group, offender index)
- Live: temporary server-only harness (not committed): Evaluate() on a real 2-bot group read
  self/healer/alive/map/mana/distance correctly, 3650 yd member -> Fragmented. Removed after.

Notes:
- `src/DungeonLead/DungeonPartyState.{h,cpp}` gathers `DungeonLeadKernel::PartyFacts`;
  `DungeonLeadKernel::EvaluateReadiness` decides (`ReadyStatus`, offender index).
- `DungeonLeadNextAction::isUseful` (walk) and `DungeonLeadMultiplier` (pull + walk) both use
  it; the old per-check helpers (MasterUnavailable, HealerUnavailable, FollowerDead,
  GroupResting, HealerManaLow, MasterTooFar, FindSpreadMember, GroupTooSpread) are gone.
  `GroupInCombat` remains as a thin wrapper for the canary.
- Healer mana now read directly (lowest living healer-role member, GMs ignored, leader
  excluded) - same rule as mod-playerbots' "healer low mana" value, without its cache.
- Not modelled yet (no behavior existed for them): MemberMissing, DifferentMap for non-healers,
  Resurrection in progress.

Remaining:
- none

---

## Phase 3 — DungeonLeadBrain

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): explicit leader state machine`

Validation:
- Build: PASS (server, 0 warnings; full rebuild needed once - see Notes)
- Tests: PASS - `tools/run_tests.sh` 61 checks (15 new: every DecideActive transition,
  PostCombat hold, wipe in/out, Completing, pause, Starting/Stopping not active)
- Live: temporary server-only harness (not committed) on a real bot: none->waiting_ready
  (session_start), KillSelf -> waiting_ready->wipe_recovery (leader_died, wipe #1),
  ResurrectPlayer -> wipe_recovery->waiting_ready (leader_recovered). Removed after.

Notes:
- `DungeonLeadKernel::LeadState` {Starting, WaitingReady, Travelling, Combat, PostCombat,
  WipeRecovery, Completing, Stopping} in `DungeonLeadState::state` is the one authoritative
  state; it replaced Phase 1's `lifecycle` field and the old `tankDeathTs` timestamp.
- `DungeonLeadBrain::TransitionTo` is the only writer (logs + `state_transition` CSV event).
  `DungeonLeadBrain::Update` (from GuardActiveSessions every 2 s and from the route walk's
  isUseful) decides via `DungeonLeadKernel::DecideActive` and owns wipe give-up.
- The route walk only moves in Travelling. Pull gating stays on readiness (Phase 2) until the
  pull controller (Phase 6).
- Not modelled yet (spec names them; added when the behavior exists): MovingToPull/PrePull/
  Pulling (Phase 6), Recovery/RouteRecovery (Phase 11), BossPrep/BossCombat (Phase 14).
- Environment: on 2026-10-03 unattended Ubuntu upgrades touched libc headers, invalidating
  the server's precompiled headers ("file has been modified since the precompiled header").
  Fix: `find build -name '*.pch' -delete` then rebuild (~13 min full worldserver build).
- Harness pitfall: outside an instance the "dungeon lead left instance" trigger stops a
  session on the bot's next AI tick - open-world tests must act within the same tick.

Remaining:
- none

---

## Phase 4 — Typed route nodes

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): typed route objectives`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 71 checks (10 new: kind parsing, anchor-wins, every
  kind -> node type)
- Live: temporary server-only harness (not committed) over the live route table: 96 routes,
  353 walkable steps -> boss 260, pull 76, interaction 44, travel 20, door 14; 0 walkable
  non-anchor Travel, 0 unknown kinds; Wailing Caverns sequence reads correctly. Removed after.

Notes:
- `src/DungeonLead/DungeonRouteTypes.h` (dependency-free): `DungeonRouteKind` + `ParseRouteKind`
  (moved), `DungeonRouteNodeType` {Travel, Pull, TankPosition, SafeSpot, Boss, Door, Interaction,
  Recovery, End}, `ClassifyRouteStep`. Node type is derived (`DungeonRouteStep::NodeType()`),
  so route data/SQL/validator are unchanged.
- `DungeonLeadBrain::CurrentObjective(st)` = destination + intent, derived from state + route
  position (Recovery = `DungeonRoute::RecoveryPoint()` while WipeRecovery, End when Completing
  or past the last step). Shown in every state_transition, `startdungeon status`, and the
  "heading to" chat line.
- SkipOptional now keys on node type Pull (same behavior as the old anchor special case).
- TankPosition/SafeSpot have no route data yet; they are produced once data needs them.

Remaining:
- none

---

## Phase 5 — Pack model

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): pack model and cleared detection`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 82 checks (11 new pack-state cases incl. "reaching the
  spot alone does not clear a live pack", terminal Cleared/Skipped, kill memory)
- Live: temporary server-only harness (not committed): pack built from a real creature
  (Wrekt Warrior, entry 17142) observed found=1 alive=1 -> available; after KillSelf
  found=1 alive=0 -> cleared. Removed after.

Notes:
- `src/DungeonLead/DungeonPack.{h,cpp}`: `DungeonPack` from static route data only (entry +
  position + probe radius, boss/optional flags; id = step index + 1); `DungeonPacks::Observe`
  finds the live creatures each time. Decision: `DungeonLeadKernel::DecidePackState`
  {Unknown, Available, Engaged, Cleared, Skipped}.
- Session: `packId` / `packState` (route-progress fields), changes logged as `pack_state`;
  `startdungeon status` shows the current pack.
- Route walk: a pack node advances only on Cleared (or Skipped by the existing
  StuckSeconds-after-arrival give-up). Travel nodes have no pack and complete on arrival -
  fixes path anchors waiting 45 s and being reported `not_found`/skipped (18 occurrences in
  the run CSV before this change).
- One expected entry per pack (route data has one entry per row). Multi-entry packs need
  route data first.

Remaining:
- none

---

## Phase 6 — DungeonPullController

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): pull controller with bounded retries`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 103 checks (21 new: every DecidePull transition incl.
  marking timeout, initiate/establish timeouts, evade after Established, bounded retry,
  class-AI-opened fight, pack cleared; brain PrePull/Pulling)
- Live: temporary server-only harness (not committed): real bot (lvl 64) + skull on a hostile
  creature 26 yd away + `DoSpecificAction("attack rti target")` -> in combat after 4.0 s
  (initiating->establishing), pack engaged 1 s later (established). Removed after.

Notes:
- `src/DungeonLead/DungeonPullController.{h,cpp}`, decision `DungeonLeadKernel::DecidePull`
  {None, Approaching, WaitingParty, Marking, Initiating, Establishing, Established, Failed}.
  Driven from GuardActiveSessions (2 s) before the brain; session fields pullState /
  pullStateTs / pullAttempts (route progress, reset per step).
- Orchestration only: skull on the pack's first live creature (respecting someone else's live
  mark; owned skull tracked in skullGuid so Stop() clears only ours), then upstream
  "attack rti target". A fight the class AI opens itself (grind) is observed as Established.
- Failure: retried up to PullMaxAttempts, then the pack is Skipped via `DungeonLead::SkipStep`
  (PullPlanning / BossEvade for bosses, ObjectiveTimeout otherwise) with pull_failed +
  pack_skipped events and a chat line - never a silent advance.
- Brain: new LeadState PrePull (Marking) and Pulling (Initiating/Establishing); the route walk
  holds in both.
- Shared route helpers: `DungeonLead::AdvanceStep`, `SkipStep`, `SetPackState` (single writer
  of pack state) replace the walk action's private MarkVisited/SetPackState.
- PullInitiateTimeoutSeconds default 10 (not 5): the live test needed 4.0 s from 26 yd, plus
  the 2 s controller tick. Config: PullRange 30, PullInitiateTimeoutSeconds 10,
  PullEstablishTimeoutSeconds 8, PullMaxAttempts 2.
- In-dungeon live run (2026-10-03, prepared test bots via DungeonTestBotPool - Acquire*TestBot
  logs in offline characters, so no idle online healer is needed): Lord Cobrahn pulled by the
  controller, initiating->established in 2 s, cleared. Lady Anacondra: the order was refused
  (`dist=0 los=0`) because the test party is teleported onto her own route coordinates; follow-up
  fix `fix(dungeon-lead): fail a refused pull order at once` records the refusal facts in
  pull_start and fails the attempt immediately instead of waiting the 10 s timeout.

Remaining:
- none

---

## Phase 7 — Combat anchor and leash

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): combat anchor and chase leash`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 110 checks (5 new ChaseAllowed cases)
- Live: in-dungeon test party in Wailing Caverns (prepared test bots): every fight logged its
  anchor in the state_transition into combat; `leash_hold Kresh dist=55` when Kresh swam off -
  the tank stayed, Kresh was pulled and killed properly afterwards.

Notes:
- Anchor = leader position when the brain enters Combat (session fields anchorSet/anchorX/Y/Z,
  route progress; cleared when the session walks on). No route data has tank positions yet;
  the engagement position is the validated equivalent.
- Containment is compatible with class AI: the leader's DungeonLeadMultiplier (present in its
  combat engine) returns 0 for upstream "reach melee"/"reach spell" when the action's target is
  beyond CombatLeashRadius of the anchor. The tank keeps fighting whatever comes back to it.
- Route movement during unresolved combat was already suppressed by the brain (walk only in
  Travelling); with the leash a fleeing mob can no longer drag the tank into the next pack.
- No active "return to anchor" move: issuing movement from outside the class AI fights its own
  positioning; not chasing is the safe containment.
- Config: CombatLeashRadius 30 (10-80).

Remaining:
- none

---

## Phase 8 — Party cohesion

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): party cohesion levels`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 120 checks (10 new: soft/hard/lost levels, farthest
  offender, hard beats soft, offline = lost, dead elsewhere = dead not lost, master off-map =
  MasterTooFar, soft range holds pulls but not walking)
- Live: Wailing Caverns test party (25 min): a resurrected DPS left 133 yd behind -> walk held
  as "group too spread - Carineva" (HardStop) for the rest of the run, no pull started.

Notes:
- `DungeonLeadKernel::EvaluateCohesion` -> {Ok, SoftWarning, HardStop, LostMember} + offender;
  used by EvaluateReadiness: LostMember -> MemberLost (walk + pull), HardStop -> Fragmented
  (walk + pull), SoftWarning -> PartySpread (pull only).
- PartySoftRange 40, PartyHardRange 90 (= the old leash*1.5 spread limit, validated hard >
  soft). The real player is still held to Leash (60) via MasterTooFar.
- New member fact `online`; a living member on another map/offline is LostMember (the master
  stays MasterTooFar/MasterUnavailable).
- Pull controller's waiting_party transition records the readiness reason and member.
- In combat nothing here abandons the pack: cohesion only gates new pulls and the walk.
- Observed gap (Phase 11): a member who stays beyond follow range (resurrected at the entrance,
  133 yd back) is never regrouped - the leader holds until the session times out. Needs an
  active regroup with timeout/escalation in the recovery controller.

Remaining:
- none (regroup belongs to Phase 11)

---

## Phase 9 — Target manager

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): stable target plan and pull success on the plan`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 132 checks (12 new: priority boss > elite caster > caster >
  elite > normal, deterministic ties, stability while targets live, dead primary replaced,
  kept CC not promoted, empty; pull: primary engaged = established, fight over with pack
  standing = approach again)
- Live: Wailing Caverns test party, 3 runs. Run 1 found that pulls of the boss's trash
  (planned primary) were counted as failed boss pulls -> Lady Anacondra skipped; run 2 found
  that a fight cap counting trash fights skipped her again; run 3 (both fixed): 20 trash fights
  around her, then Lady Anacondra engaged and cleared, route advanced; Kresh pulled and cleared.

Notes:
- `src/DungeonLead/DungeonTargetManager.{h,cpp}` builds candidates (hostiles within 20 yd of the
  current pack's live creature before a pull, units in combat within 40 yd of the leader during
  one), decides with `DungeonLeadKernel::PickTargetPlan`, keeps the plan in the session
  (targetPrimary/Secondary/Cc) and logs `target_plan` changes.
- Marks mirror the plan: skull = primary, cross = secondary, moon = CC (existing CC lifecycle in
  CheckCcMark unchanged). Only icons that are free, on a dead unit, or ours are moved; owned
  cross tracked like skull/moon so Stop() clears only ours.
- Single writer for marks: the old boss-only marking in DungeonLeadMarkAction (and its
  FindCcCandidate) is replaced by a call to the manager; the pull controller no longer marks.
- Pull controller now pulls the plan's primary and counts a pull as established when the pack
  OR the primary is engaged; a fight that ends with the pack standing returns to Approaching
  (progress, not failure). Bounds: failed tries (PullMaxAttempts) and pack resets (the pack
  itself engaged and survived, 3) - trash fights are not capped.
- The route walk's 45 s arrival give-up no longer skips a live pull/boss pack (the controller's
  bounds apply instead); it still covers interactions and empty spots.
- "Healer-type NPC" priority not modelled (no reliable data); casters = mana users.

Remaining:
- none

---

## Phase 10 — Post-combat gate

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): deliberate post-combat gate`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 140 checks (8 new: LowHealth incl. leader, fighting member
  not gated, threshold 0 = off; PostCombat minimum pause, unexpected combat during it)
- Live: Wailing Caverns test party, 2 runs: every fight ended in a logged
  post_combat_decision (pause 4-12 s, lowest health and healer mana recorded). Run 1 found the
  route walk's stuck detector skipping Lady Anacondra ("stuck 196s") because its timer ran
  through 3 min of trash fights - fixed in this commit; run 2: Lady Anacondra and Kresh both
  engaged and cleared, no skips, party on its way to Lord Cobrahn at the cap.

Notes:
- Brain: PostCombat holds for PostCombatMinSeconds (3) before the readiness decision; unexpected
  combat during it goes straight back to Combat. Leaving PostCombat records
  `post_combat_decision` (next state, readiness, lowest health, healer mana, waited ms).
- Readiness: new ReadyStatus::LowHealth (any living same-map member, leader included, out of
  combat, below PostCombatMinHealthPct, default 50, 0 = off); blocks walking and pulls.
  Hostiles / unexpected combat (AnyInCombat), deaths, healer, mana, drinking were already in the
  readiness path; resurrection in progress = MemberDead (a ghost is dead).
- On every entry to Travelling the brain resets the walk's stuck baseline (bestDist/stuckTs/
  stuckAttempts) and the arrival flag, so StuckSeconds measures walking time only.
- Config: PostCombatMinSeconds 3 (0-30), PostCombatMinHealthPct 50 (0-100).

Remaining:
- none

---

## Phase 11 — Recovery controller

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): bounded recovery controller`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 158 checks (18 new: act/escalate/abort windows, never an
  endless loop, readiness -> recovery mapping incl. waits that are not recoveries, brain
  Recovery state, combat interrupts it)
- Live: Wailing Caverns test party, 20 min: the healer died and was returned to the entrance ->
  `recovery_start party_fragmented member=Thyleae`, leader walked back, `recovery_complete`
  after 32 s (the same situation stalled earlier runs for 16-20 min); Lady Anacondra and Kresh
  cleared, party moving on to Cobrahn at the cap. A first attempt was cut short by a test-pool
  start race (characters logged in inside the old instance) - Phase 13 territory.

Notes:
- `DungeonLeadKernel::DecideRecovery` / `RecoveryFor`, `src/DungeonLead/DungeonRecoveryController.{h,cpp}`.
  Reasons: leadership_lost, member_lost, party_fragmented, member_dead. Act ->
  RecoveryTimeoutSeconds (60) -> Escalate -> RecoveryEscalationSeconds (60) -> Abort (Stop, run
  recorded Failed, domain Recovery). The clock runs as long as any recovery problem persists
  (label changes don't reset it); escalation always runs once before an abort.
- Actions: party_fragmented = leader walks back to the straggler after an 8 s grace;
  member_lost/member_dead/leadership_lost = wait. Escalation brings a *bot* member (not real
  player, not selfbot) to the leader; nothing else is escalated. Abort for leadership_lost
  does not try to hand leadership back.
- Ordinary waits (drinking, mana, health, the real player's position/availability) are not
  recoveries and are never timed out here.
- Brain: new LeadState Recovery (while a recovery is open, out of combat); events
  recovery_start / recovery_escalate / recovery_complete / recovery_failed.
- Eligible only in WaitingReady/PostCombat/Recovery with the leader alive; the leader's own
  death stays WipeRecovery (Phase 12).

Remaining:
- none

---

## Phase 12 — Wipe and checkpoints

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): wipe checkpoints and route reconciliation`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 163 checks (5 new ResumeStepAfterWipe cases: no checkpoint,
  skipped steps after the checkpoint reopened, never ahead of the route)
- Live: Wailing Caverns test party with the leader killed 4 min in (harness, not committed):
  wipe_detected -> wipe_recovery, the healer resurrected the leader 14 s later ->
  wipe_recovered -> checkpoint_restore (from=0 to=0, no checkpoint before the first boss) ->
  waiting_ready, route continued at Lady Anacondra.

Notes:
- Checkpoint = `DungeonLeadState::checkpointStep`, set by `DungeonLead::AdvanceStep(st, true)` only
  for confirmed milestones (pack cleared - already_dead - and travel node reached); skips call
  `AdvanceStep(st, false)`. Route-progress field (reset with a new instance).
- `DungeonLead::RestoreCheckpoint` runs when the brain leaves WipeRecovery: resumes at
  `ResumeStepAfterWipe(checkpoint, current)`, reopens steps passed over since (visited cleared,
  removed from the skipped list, a Partial caused only by them is taken back), and resets the
  current step's pack/pull/target/anchor/arrival/stuck state - packs reset on a wipe, so the
  world is re-observed. Steps confirmed killed stay skipped via the instance kill memory.
- Wipe detection and its bounded give-up are the brain's WipeRecovery (Phase 3); regroup after
  the wipe is the recovery controller (Phase 11).
- Not live-exercised: a restore that actually rewinds (needs a skip after a checkpoint and then
  a wipe in the same run) - covered by the kernel tests.

Remaining:
- none

---

## Phase 13 — Test party synchronization

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): start test parties only once assembled`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 170 checks (7 new DecideAssembly cases: requested-not-landed,
  wrong instance, not gathered, dead member, timeout -> abort, never a partial/empty start)
- Live: the exact failure from the Phase 11 run (test characters logged in inside an old
  instance; the session started, the party left to re-enter, the session stopped as "left
  instance") reproduced on purpose: all five left, re-entered instance 1 together, gathered ->
  "party of Farancano assembled ... after 21551 ms - StartSession=true", then an ordinary 8 min
  run. A first attempt aborted at once because a tank in a cross-map teleport is briefly out of
  the world (FindPlayer = null); fixed with FindConnectedPlayer + "in transit = not there yet".

Notes:
- `DungeonLeadKernel::DecideAssembly` / `MemberAssembled`: online, alive, on the dungeon map,
  in the tank's instance, within 30 yd of the tank; Wait / Start / Abort (3 min timeout).
- `RunTestParty` registers a pending start instead of calling StartSession; `TestBotPoolTick`
  (3 s) evaluates and starts it. Releasing a bot drops its pending party. Pending parties count
  in `ActiveCanaryCount()` (via `PendingTestPartyCount()`), so the concurrency cap holds.
- The existing teleport machinery (leader first, followers after it lands, exit-then-enter,
  retries) is unchanged; this phase only stops the session from starting before it worked.

Remaining:
- none

---

## Phase 14 — Boss foundation

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): boss prep and boss combat with a boss anchor`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 176 checks (6 new: marking/pulling a boss -> BossPrep, its
  trash -> Combat, boss engaged -> BossCombat, holds while combat lasts, -> PostCombat)
- Live: Wailing Caverns test party: combat with Lady Anacondra's trash -> `combat->boss_combat
  reason=boss_engaged`, anchor moved to her home spot (boss_combat tank_pos), boss leash held an
  add at 28 yd (leash_hold), she was cleared 16 s later, adds finished, post_combat, route on.
  boss_prep not seen live (that fight began from trash, not a planned boss pull) - kernel-tested.

Notes:
- LeadState BossPrep (Marking/Initiating/Establishing on a boss pack) and BossCombat (in combat
  with the boss pack engaged; holds while combat lasts). TransitionReason boss_engaged.
- `DungeonBossStrategy` {bossEntry, tank position, leash} via `DungeonPacks::BossStrategyFor`:
  generic default = the boss's home position and BossLeashRadius (25). Route data has no
  per-boss positions yet; the struct is where they go when an encounter needs them.
- The pull controller publishes boss facts (bossPackCurrent, bossEngaged, boss tank position)
  each tick; the brain anchors BossCombat at the boss position with its own radius
  (`anchorRadius`, used by the leash multiplier instead of CombatLeashRadius).
- Facing: mod-playerbots' own "tank face" combat strategy (tank specs have it by default; added
  at BossPrep for a non-tank leader). No movement is issued from here - class AI positions.
- Events: boss_prep, boss_combat.

Remaining:
- none

---

## Phase 15 — Telemetry hardening

Status: DONE (2026-10-03)

Commit:
`feat(dungeon-lead): buffered structured telemetry`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 182 checks (6 new TelemetryBuffer cases: capacity, drop
  count, order/target file, drain resets, accepts again)
- Live: Wailing Caverns test party (6 min): every row carries map_id/instance_id/state/step/
  pack_id; 0 dropped lines; the run (start, transitions, fights, post-combat decisions, pulls,
  packs, leash, CC) reconstructs from DungeonLeadSessions.csv alone.

Notes:
- `src/DungeonLead/DungeonTelemetryBuffer.h` (dependency-free, tested): producers (bot AI on map
  threads) only push a formatted line under a short lock - no file I/O, no global lock around
  I/O any more. `DungeonLead::FlushTelemetry` (world thread, from the module's OnUpdate, ~2 s or
  when 1000 lines are queued; forced from WorldScript::OnShutdown) writes all three files with
  one fflush each. Capacity 20000 lines; overflow is logged and written as a telemetry_dropped
  row. Crash durability: up to ~2 s of lines can be lost (accepted trade-off).
- Sessions CSV: columns appended at the end (existing positions unchanged; an existing file
  keeps its old header). Events added: leadership_request / leadership_confirmed /
  leadership_failed (Phase 1's start_failed renamed to leadership_failed).
- Event coverage against the plan's list: session start/stop, leadership_*, state_transition,
  readiness (waiting / post_combat_decision), route (pathing, travel_reached, reached,
  already_dead, route_*), pull_*, pack_*, recovery_*, wipe_*, checkpoint_restore, boss_prep,
  boss_combat, run summary per run in DungeonLeadRuns.csv.

Remaining:
- none

---

## Phase 16 — Cleanup

Status: DONE (2026-10-03)

Commit:
`refactor(dungeon-lead): one leader decision engine`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - `tools/run_tests.sh` 184 checks (StateAllowsNewPull)
- Live: Wailing Caverns test party (10 min) after the change: fights opened normally, Lady
  Anacondra and Kresh cleared, no skips, no failed recovery.

Notes:
- Removed the last duplicate decision: DungeonLeadMultiplier no longer gates the route walk
  (the brain decides walking - Travelling only). It now only gates the class AI's own moves:
  the chase leash, and grind's opportunistic pulls by the brain state
  (`DungeonLeadKernel::StateAllowsNewPull`) plus pull readiness.
- Earlier phases already removed: the per-check readiness helpers (Phase 2), the lifecycle
  flag and death timestamp (folded into the brain state, Phase 3), the walk action's private
  MarkVisited/SetPackState and pull marking (Phases 6/9), the boss-only mark logic (Phase 9),
  per-write file I/O under a global mutex (Phase 15). An audit of DungeonLeadState found no
  unused fields; stale names (GroupTooSpread, HealerManaLow, tankDeathTs, MarkVisited,
  FindCcCandidate, FollowerDead) no longer appear outside history notes.
- README "how it runs" rewritten for the current behavior (kept short).

Remaining:
- none

---

## Definition of done - status (2026-10-03)

Autonomous trash leadership (section 37):
1. leadership acquisition confirmed - yes (Phase 1, live)
2. leadership return confirmed or failed safely - yes (Phase 1, live)
3. party readiness centralized - yes (Phase 2)
4. healer unavailability blocks new pulls - yes (Phases 1/2)
5. state machine authoritative - yes (Phases 3/16)
6. route objectives typed - yes (Phase 4)
7. current pack explicit - yes (Phase 5)
8. pull lifecycle explicit - yes (Phase 6)
9. pull failure enters recovery - bounded retry, then the pack is skipped and recorded
   (pull_failed/pack_skipped, Partial for a boss); not routed through the party recovery
   controller (that one is for party problems) - judged sufficient: never silent, bounded
10. combat anchor exists - yes (Phase 7)
11. leash prevents unsafe chain pull - yes (Phase 7, live leash_hold)
12. route does not advance during unresolved combat - yes (walk only in Travelling)
13. pack completion confirmed - yes (Phase 5)
14. post-combat gate exists - yes (Phase 10)
15. dead members trigger recovery - yes (Phase 11)
16. wipe triggers checkpoint logic - yes (Phase 12, live)
17. checkpoint restoration works - yes for the live path (no rewind needed); rewind kernel-tested
18. test-party startup deterministic - yes (Phase 13, live)
19. telemetry explains state transitions - yes (Phase 15)
20. no competing module used - yes
21. no duplicate active leader engine - yes (Phase 16)

Project (section 38) - open items:
- Only Wailing Caverns exercised live (bot-only test party, up to Lord Cobrahn). Other
  dungeons have route data but no live run; a run with a real player in the group hasn't been
  done since these phases.
- Boss foundation is generic (boss home position + leash, upstream tank face); no per-boss data.
- Doors/gated bosses are still not waited for.
- Commits are local; not pushed (left for the user).

---

# HARDENING PROGRESS

Plan: `DUNGEON_LEADER_HARDENING_PLAN.md` (2026-10-04). Its release numbers (v0.6.0-beta.1 ...)
collide with existing tags (v0.6.0-v0.10.0-alpha). Agreed with the user 2026-10-04: continue the
existing numbering, and keep `-alpha` until the CHANGELOG's own `-beta` bar (most non-event
dungeons live-verified) is met. First hardening release: v0.11.0-alpha (H1-H3).

## H1 — Recovery timer on reason change

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): reset recovery timeout on reason change`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 194 checks (10 new: fragmented 20 s -> lost restarts from 0, dead -> fragmented
  independent window, same reason keeps its clock, flapping bounded by the episode cap, clocks
  cleared when the problem is gone)
- Live: Wailing Caverns test party 12 min, no regression (Anacondra, Kresh cleared); no
  recovery occurred in that run.

Notes:
- `DungeonLeadKernel::RecoveryTimers` {reason, step, reasonSince, episodeSince} replaces the
  three loose session fields; `ObserveRecovery` restarts the reason clock and step on a reason
  change, `DecideRecovery(reason, msInReason, msInEpisode)` keeps per-reason act/escalate/abort
  and adds an episode cap (2 x (act + escalate)) so flapping between reasons stays bounded -
  the property the old shared clock gave.
- The plan's example reasons PullFailed/RouteLost/HealerDead don't exist as recovery reasons
  here (pull failure is the pull controller's, a dead healer is member_dead); tests use the
  existing reasons.
- recovery_start now records the previous reason (`after=`).

## H2 — Combat target selection on the combat anchor

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): anchor combat target selection`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 199 checks (5 new IsFightCandidate cases: tank at anchor, neighbour near the
  moved tank but 50 yd from the anchor rejected, pack member inside the radius kept, add
  attacking the party kept wherever it stands, idle mob near the anchor rejected)
- Live: Wailing Caverns test party 12 min: plans built from 1-3 candidates, pulls/CC as before,
  no skip or recovery failure.

Notes:
- DungeonTargetManager in combat measures from the session's combat anchor (`anchorX/Y/Z`, or
  the boss spot in BossCombat) and falls back to the tank position only without an anchor.
- `DungeonLeadKernel::IsFightCandidate`: in combat AND (within 40 yd of the anchor OR attacking
  a party member). Same boundary as the leash; pack identity hardening follows in H4.

## H3 — Mandatory objective failure policy

Status: DONE (2026-10-04)

Commit:
`fix(dungeon-lead): enforce mandatory objective failure policy`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 211 checks (12 new: requirement classification, optional skipped after 1-3
  rounds, required/boss retried then aborted, a boss/required objective never skipped)
- Live: Wailing Caverns test party with two forced failures on Lady Anacondra (harness, not
  committed): round 1 -> objective_retry, session kept; round 2 -> objective_failed, session
  stopped, run row `partial / pull_planning / boss_evade / objective_failed`.

Notes:
- `DungeonObjectiveRequirement` {Optional, Required, Boss} in DungeonRouteTypes.h, derived from
  existing data (`DungeonRouteStep::Requirement()`): a `boss` row is Boss, everything else
  Optional; Required exists for future route data (no row marks trash as required yet).
- `DungeonLeadKernel::DecideObjectiveFailure` -> Skip (optional only) / Retry / Abort, rounds =
  ObjectiveRetryRounds (2). `DungeonLead::FailObjective` is the single entry for every
  "can't complete this step" path: pull attempts exhausted, pack reset too often, arrival
  give-up (stuck_alive/not_found), route walk stuck (path). Retry = `ResetStepState` (fresh
  observation, same step); Abort = run Partial + objective_failed + run summary + Stop. The
  route never advances past an unresolved boss.
- objective_skipped replaces pack_skipped.

## H4 — Pack identity

Status: DONE (2026-10-04) - dense-room live validation pending (H7 Tier 2)

Commit:
`refactor(dungeon-lead): strengthen runtime pack identity`

Validation:
- Build: PASS (server, 0 warnings)
- Tests: PASS - 220 checks (9 new ResolvePack cases: two same-entry packs 15 yd apart not
  merged, lock on engage, add joins, neighbour that joins counts but its idle friends don't,
  locked members dead -> cleared, a boss's trash is an add not the boss, patrol lead, empty)
- Live: Wailing Caverns test party 14 min (debug on): Lady Anacondra resolved as 1 member with
  the trash around her as adds (up to 7), locked on engage, cleared; Kresh the same; no
  neighbouring group merged, no objective failure.

Notes:
- `DungeonLeadKernel::ResolvePack`: core members = the nearest expected unit (lead) + expected
  units whose spawn (home) position is within 12 yd of the pack spot + expected units attacking
  the party; once engaged the core is locked (`DungeonLeadState::packLocked`); any other unit
  attacking a party member is an add. The pack's observation (found/alive/engaged -> state) uses
  the core only - fighting a boss's trash must not look like fighting the boss.
- `DungeonPacks::Observe` gathers the expected entries (grid search) plus all creatures attacking
  party members (`Unit::getAttackers`); `DungeonLead::TrackPack` = observe + state + lock + debug
  `pack_resolution` (members, adds, rejected, reason) and is shared by the route walk and the
  pull controller.
- Dense-room behaviour (two same-entry groups) is kernel-tested; WC routes are single named
  units, so the live run exercised the add/lock path, not the rejection path - Tier 2 dungeons
  in H7 will.

## H5 — Interaction controller (doors)

Status: DONE (2026-10-04) - door wait not yet triggered live (see Notes)

Commit:
`feat(dungeon-lead): add interaction controller`

Validation:
- Build: PASS (server, CMake reconfigured for the new .cpp, 0 warnings)
- Tests: PASS - 231 checks (11 new DecideInteraction cases: door already open, closed -> wait,
  opens after the event -> complete, never opens -> failed, target missing/vanished,
  act -> wait for confirmation, world-confirmed completion)
- Live: Shadowfang Keep test party (level 22, 20 min): Rethilgore and Razorclaw cleared, the
  Courtyard door (opened by its event after Rethilgore) passed; the door wait itself did not
  trigger because the event had already opened it when the leader got there.

Notes:
- `src/DungeonLead/DungeonInteractionController.{h,cpp}`, decision
  `DungeonLeadKernel::DecideInteraction` {None, Resolving, WaitingPrerequisite, Interacting,
  WaitingConfirmation, Complete, Failed}; only type so far: Door.
- Data-free: route `door` rows have no entry/position, so the door is found where the walk gets
  stuck - when MoveRouteTo is about to give up, a closed (GO_STATE_READY) GAMEOBJECT_TYPE_DOOR
  within 15 yd of the leader and closer to the destination starts a door interaction instead of
  failing the step. The leader holds; success only when the world shows the door open; after
  DoorWaitSeconds (120) it fails through `FailObjective` ("door_closed"/"door_gone").
- The leader never operates doors (dungeon doors are event/key/boss gated). Elevator / NPC /
  game-object interactions are not implemented - no route data needs them yet (plan: only what
  routes require).
- Events: interaction_state; `startdungeon status` shows "waiting at door".

# END OF AUTHORITATIVE CLAUDE CODE INSTRUCTIONS
