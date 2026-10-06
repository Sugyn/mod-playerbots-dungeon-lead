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

## Phases 0-16 — core implementation

Status: DONE (2026-10-03), commits `docs(dungeon-lead): add CLAUDE.md plan and baseline` through
`refactor(dungeon-lead): one leader decision engine`.

The per-phase record (validation, notes, the 2026-10-03 definition-of-done check) is history, not
current state: `docs/history/implementation-progress-2026-10-03.md`.

---

## Hardening H1-H7 - live validation

Status: DONE (2026-10-04/05) - record in `docs/history/hardening-2026-10-04.md`.

---

# CURRENT STATE

Only this section describes the present. History: `docs/history/` (phases 0-16, hardening H1-H7).
Per-dungeon validation truth: `docs/testing-status.md`. Release history: `CHANGELOG.md`.
Keep this section short and current - update it with every milestone, don't append to it.

## Current release

v0.13.0-alpha (2026-10-06). Stays `-alpha` until most supported non-event dungeons are Verified
(CHANGELOG versioning); 1.0 = every non-event/non-vehicle route Verified.

## Current implementation

- Leader state machine (DungeonLeadBrain) over party readiness, pull controller, target manager,
  recovery controller, interaction controller, route manager - see sections 6-8 for roles.
- Route step kinds: boss, optional, required, door, use, talk, event (not executed), skip.
  Requirement: boss -> Boss; required/door/use/talk -> Required; others Optional
  (DungeonRouteTypes.h ClassifyRequirement).
- Interactions: closed doors are not walked through (path crosses the door plane); a door is opened
  as a player could (hand lock -> Opening spell, key -> holder, lever on our side) or waited for;
  `use` (Opening spell / key spell / use, chests looted, a missing key looted from a corpse);
  `talk` (gossip like a client, only to an NPC standing still 20 s, assists the event's fight
  while waiting, hostile NPCs skipped); scripted area triggers fired for the leader.
- Live validation built in: `.dungeonlead validate <lfgId...>`, `tools/live_validation/`,
  `tools/summarize_runs.py` (ALERT lines).

## Current validation

From `docs/testing-status.md` (2026-10-06): Full route - RFC, Deadmines, Shadowfang Keep, SM
Graveyard/Library/Armory/Cathedral, Razorfen Downs, Zul'Farrak; Partial - Wailing Caverns,
Razorfen Kraul; Verified - none; TBC/WotLK - not run.

## Open findings (audit 2026-10-06)

- No dungeon Verified yet (needs a second clean full route on the same code).
- Coverage is Vanilla only; TBC normal not run.
- Interaction-heavy dungeons (Uldaman, Scholomance, Gnomeregan, BRD) not run.
- Wailing Caverns: Mutanus needs the Disciple of Naralex escort (not supported).

## Next objective

Validation maturity milestone (target release v0.14.0-alpha):
1. Second clean full-route pass, no code change between the two runs unless a bug is found:
   RFC, Deadmines, SFK, SM Library/Armory/Cathedral, RFD, Zul'Farrak -> Verified.
2. TBC normal: Hellfire Ramparts, Blood Furnace, Slave Pens, Underbog, Mana-Tombs.
3. One interaction-heavy dungeon (Uldaman / Scholomance / Gnomeregan / BRD).
Working loop: run -> fail -> classify (ROUTE, PACK_IDENTITY, TARGETING, PULL, LEASH, READINESS,
RECOVERY, WIPE, INTERACTION, BOSS, LEADERSHIP, TELEPORT, UPSTREAM_PLAYERBOT, DATA_ERROR,
UNKNOWN) -> reproduce -> smallest generic fix -> kernel test -> same dungeon -> one unrelated
dungeon -> testing-status -> commit. No new interaction type or required row without a live
failure that needs it. No architecture rewrite.

# END OF AUTHORITATIVE CLAUDE CODE INSTRUCTIONS
