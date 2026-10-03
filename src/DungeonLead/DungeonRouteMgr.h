/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONROUTEMGR_H
#define PLAYERBOTS_DUNGEONROUTEMGR_H

#include "DungeonLeadKernels.h"
#include "DungeonRouteTypes.h"
#include "Common.h"
#include "ObjectGuid.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// One step of a hand-authored dungeon route (table playerbots_dungeon_route).
struct DungeonRouteStep
{
    uint32 step = 0;
    DungeonRouteKind kind = DungeonRouteKind::Unknown;
    std::string boss;
    uint32 entry = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    std::string note;

    bool HasPosition() const { return entry != 0 && !(x == 0.f && y == 0.f && z == 0.f); }
    bool IsWalkable() const
    {
        return HasPosition() && (kind == DungeonRouteKind::Boss || kind == DungeonRouteKind::Optional ||
                                  kind == DungeonRouteKind::HeroicOnly || kind == DungeonRouteKind::Event);
    }

    // Policy boundary for "must this be satisfied for the run to count as Complete rather than
    // Partial" - kept as one named function instead of repeating `kind == Boss` at every call
    // site, since a future mandatory kind (a required door/event, not just a boss) should only
    // need this one line updated, not every place that currently checks it. See the architecture
    // roadmap's L0 closeout notes.
    bool IsMandatory() const { return kind == DungeonRouteKind::Boss; }

    // 2026-09-15 (independent architecture review DL-011 - "navigation identity, execution, and
    // observation are conflated"): entry=1 is AzerothCore's universal "Waypoint (Only GM can see
    // it)" creature template (confirmed against the live creature_template table, not just this
    // dungeon's data) - never real gameplay content, only ever used here as a dummy entry for a
    // pure navigation anchor (Wailing Caverns' six bridge waypoints, kind=Optional since there was
    // no better-fitting kind when they were added). Without this distinction,
    // AiPlayerbot.DungeonLead.SkipOptional=1 would silently delete the anchors some routes need
    // just to be walkable at all, reintroducing the direct NOPATH hops they exist to prevent - see
    // the skip condition in DungeonLeadNextAction::Execute.
    bool IsPathAnchor() const { return entry == kPathAnchorEntry; }

    // What the leader goes there to do (see DungeonRouteTypes.h) - derived, not stored.
    DungeonRouteNodeType NodeType() const { return ClassifyRouteStep(kind, entry); }
};

// Run-level outcome, shared vocabulary between the runtime and (eventually) an automated test
// harness - see the architecture roadmap's L1 RunResult contract. Deliberately small for now:
// only Running/Complete/Partial are actually produced by the current engine (Blocked/Failed/
// Aborted are reserved for later recovery-manager/test-harness work, not populated yet).
enum class DungeonRunOutcome : uint8
{
    Running,
    Complete,
    Partial,
    Blocked,
    Failed,
    Aborted
};

enum class DungeonFailureDomain : uint8
{
    None,
    Navigation,
    PartyCoordination,
    PullPlanning,
    Combat,
    Encounter,
    Recovery,
    Infrastructure
};

enum class DungeonFailureReason : uint8
{
    None,
    PathFailed,
    ObjectiveTimeout,
    BossEvade,
    PartyWipe,
    PlayerMissing,
    UnsupportedEvent,
    InternalInvariant
};

char const* ToString(DungeonRunOutcome v);
char const* ToString(DungeonFailureDomain v);
char const* ToString(DungeonFailureReason v);

// Who/what asked for this session - lets the reconciliation loop, logging, and the CSV all tell a
// human-requested run apart from one the AutoBot Canary controller started on its own (see
// DungeonLeadCanary.h). Manual is the only origin that existed before the canary controller;
// nothing about Manual's behavior changes because this enum exists.
enum class DungeonLeadSessionOrigin : uint8
{
    Manual,      // "startdungeon" chat command, requested by a real player
    AutoCanary,  // AutoBot Canary controller - unattended, config-gated, off by default
};

char const* ToString(DungeonLeadSessionOrigin v);


struct DungeonRoute
{
    uint32 lfgId = 0;
    uint32 mapId = 0;
    uint32 difficulty = 0;
    std::string name;
    std::vector<DungeonRouteStep> steps;

    // 2026-09-15 (independent architecture review, DL-003 - "route completion can be a zero-work
    // false positive"): reaching the end of the route with no IsMandatory() step ever skipped was
    // reported as Complete even if the route has no mandatory step AT ALL to skip - a route with
    // only optional/event/door/skip rows (12 one-row 'skip' dungeons plus the unpositioned
    // Headless Horseman/Ahune rows, confirmed by the review) reports Complete having done zero
    // movement or combat. Used at the terminal-outcome call site to downgrade that specific case
    // to Blocked instead of Complete - not a full fix (still doesn't verify an *optional* step's
    // own claimed completion, and doesn't touch InstanceScript/encounter state), just closes the
    // worst false positive: a route that structurally could never prove anything.
    bool HasAnyMandatory() const
    {
        for (DungeonRouteStep const& s : steps)
            if (s.IsMandatory())
                return true;
        return false;
    }

    // First walkable step - the fallback entrance when the map has no entrance teleport (see
    // DungeonRouteMgr::GetEntrance). Note it is usually the first boss/pack, not a safe spot.
    DungeonRouteStep const* RecoveryPoint() const
    {
        for (DungeonRouteStep const& s : steps)
            if (s.IsWalkable())
                return &s;
        return nullptr;
    }
};

// A follower's formation/strategy set as it was right before "startdungeon" touched it, so
// "stopdungeon" can restore it exactly instead of applying a generic default (chaos formation,
// whatever strategies happen to be left over from dungeon-lead's own +/- deltas).
struct DungeonLeadMemberSnapshot
{
    ObjectGuid guid;
    std::string formation;
    std::vector<std::string> nonCombatStrategies;
    std::vector<std::string> combatStrategies;
};

// Per-bot progress through a route (kept here instead of an AI value to survive strategy resets).
//
// Split into two groups on purpose: route-progress fields are cleared by ResetRouteProgress()
// (a fresh ResolveRoute() after entering a new instance, or "startdungeon reset"), while session
// fields survive that and are only cleared by a full Reset() (a real "startdungeon"/"stopdungeon").
// Before this split, ResolveRoute()'s internal wipe used the same Reset() as a real stop/start,
// which meant debugMode (and any owned mark) silently reverted seconds after every single
// "startdungeon", the moment the first route-resolution tick ran - see CHANGELOG.
struct DungeonLeadState
{
    // --- route progress: cleared by ResetRouteProgress() ---
    uint32 lfgId = 0;
    uint32 mapId = 0;
    uint32 instanceId = 0;
    uint32 stepIndex = 0;
    uint32 stuckTs = 0;
    uint32 stuckAttempts = 0;
    float bestDist = 0.f;
    uint32 lastPathLogTs = 0;  // throttle for the "pathing" telemetry line in MoveRouteTo() -
                                // one line per ~3s per bot, position/target/distance/path-type,
                                // sent to BOTH LOG_INFO and RecordEvent (CSV -> panel) so the
                                // actual walking trace is visible in both places, not just
                                // milestone events (reached/stuck/mark). Added 2026-09-13 - see
                                // ADR/CHANGELOG: neither the log nor the panel had this before.
    bool noRouteTold = false;
    bool doneTold = false;
    uint32 lastWaitLogTs = 0;
    bool farFromMasterTold = false;  // one-shot "We're waiting for you!" until the player catches up
    std::string spreadOffenderTold;  // name of the last bot we pinged about for a too-spread group, "" if none
    int32 announcedStep = -1;        // one-shot "heading to X" per step, not spammed every tick
    bool arrivedTold = false;        // one-shot "reached X" - doesn't by itself advance the route
    uint32 arrivedTs = 0;            // when arrivedTold was set; used to give up if nothing is ever found there
    uint32 packId = 0;               // DungeonPack::id of the pack being worked on (0 = none)
    DungeonLeadKernel::PackState packState = DungeonLeadKernel::PackState::Unknown;
    DungeonLeadKernel::PullState pullState = DungeonLeadKernel::PullState::None;  // DungeonPullController
    uint32 pullStateTs = 0;          // getMSTime() of the last pull state change
    uint8 pullAttempts = 0;          // pulls initiated on the current pack
    bool pullOrderRefused = false;   // upstream's Attack() refused the current attempt's order
    // Combat anchor: where the current fight began (set by DungeonLeadBrain on entering Combat,
    // cleared once the session walks on). The tank does not chase beyond CombatLeashRadius of it.
    bool anchorSet = false;
    float anchorX = 0.f, anchorY = 0.f, anchorZ = 0.f;
    uint32 lastLeashLogTs = 0;
    // 2026-09-16 (DL-013, the actual recovery this time): upstream's death handling is complete
    // and works - BOT_STATE_DEAD installs "dead", which does auto release -> find corpse ->
    // revive from corpse. Exactly one step of it is impossible in a dungeon: releasing inside an
    // instance with no graveyard of its own (Wailing Caverns has none) drops the ghost at the
    // nearest OUTDOOR graveyard - measured 2697 yards away, on a different map - and
    // FindCorpseAction's MoveTo() to the corpse's map can never path there. The bot then stands
    // on that graveyard indefinitely; the spirit-healer fallback did not fire either (observed
    // dead and motionless for 8+ minutes). Teleporting the ghost back onto the instance map was
    // verified by hand to fix it outright: the bot resurrected by itself within 20 seconds.
    // recoveryTs throttles those teleports so a member who dies repeatedly can't be yanked every
    // tick; wipeCount is how many times the leader has died this run, reported in the run summary
    // so a "completed" result can never hide that it took several wipes to get there.
    uint32 recoveryTs = 0;
    uint32 wipeCount = 0;
    std::vector<uint8> visited;
    std::vector<std::string> skippedSteps;  // bosses skipped (stuck/not-found) - reported at "route complete"
    bool mandatorySkipped = false;  // true if any of the above was IsMandatory() - run outcome PARTIAL, not COMPLETE
    DungeonRunOutcome outcome = DungeonRunOutcome::Running;
    DungeonFailureDomain failureDomain = DungeonFailureDomain::None;
    DungeonFailureReason failureReason = DungeonFailureReason::None;

    // --- session: survives a route reset, only a full Reset() (real stop/start) clears these ---
    uint64 runId = 0;      // correlates every telemetry row from one "startdungeon" session
    // The session's one authoritative state - only DungeonLeadBrain::TransitionTo changes it.
    // Starting: leadership requested, not observed yet. Stopping: strategies restored, handback
    // not observed yet. Everything in between is an active session (see DungeonLeadBrain).
    DungeonLeadKernel::LeadState state = DungeonLeadKernel::LeadState::WaitingReady;
    uint32 stateSinceTs = 0;  // getMSTime() of the last transition
    // Leadership transfer in flight (Starting: to the tank; Stopping: back to leadershipTarget).
    ObjectGuid leadershipTarget;   // who must end up leader
    ObjectGuid leadershipFrom;     // who held it when the transfer was requested
    uint32 leadershipRequestTs = 0;  // getMSTime() of the last request
    uint8 leadershipAttempts = 0;    // requests made so far, failed enqueues included
    DungeonLeadSessionOrigin origin = DungeonLeadSessionOrigin::Manual;
    // 2026-09-15 (independent architecture review DL-006 - the one remaining silent exit path: a
    // hard disconnect never runs any Stop() call site at all): every other RecordRunSummary() call
    // site reads the tank's name straight off a live Player*, which a disconnected bot no longer
    // has. Cached here at StartSession() specifically so GuardActiveSessions() can still name the
    // run in DungeonLeadRuns.csv after the character object itself is gone.
    std::string tankName;

    uint32 sessionStartTs = 0;  // getMSTime() at StartSession() - used by the canary controller's
                                 // timeout check; also generally useful (duration is otherwise only
                                 // reconstructable from the CSV's own "start" row timestamp)
    ObjectGuid ccGuid;      // creature currently moon-marked by us, if any
    uint32 ccMarkedTs = 0;  // when it was marked; if no CC lands within CcTimeoutSeconds, unmark it
    bool ccLandedTold = false;  // whether "cc_landed" has already fired for the current ccGuid -
                                 // logged once per landing, not every tick it stays crowd
                                 // controlled. Reset alongside ccGuid.
    uint32 ccMarkedAbsoluteTs = 0;  // when it was FIRST marked - never reset (unlike ccMarkedTs,
                                     // which holds open while waiting for the target to enter
                                     // combat/for CC to land). Absolute ceiling: a target that
                                     // never enters combat at all would otherwise hold the mark
                                     // forever under the combat-aware wait, permanently excluding
                                     // it from normal DPS targeting for no reason - see
                                     // CcAbsoluteTimeoutSeconds and CheckCcMark().
    ObjectGuid skullGuid;   // boss currently skull-marked by us, if any (so Stop() only clears our own)
    bool paused = false;    // "startdungeon pause" / "startdungeon continue"
    bool debugMode = false; // "startdungeon debug": verbose per-wait diagnostics to DungeonLeadDebug.log
    std::vector<DungeonLeadMemberSnapshot> memberSnapshots;  // pre-"startdungeon" state, for exact restore

    // 2026-09-15: the leader itself was never snapshotted - Stop() relied on botAI->Reset() alone
    // to strip "dungeon lead" back off the leader's own engine, but a plain Reset() does not
    // remove active strategies (see independent architecture review, DL-002: "Stop() does not
    // turn Dungeon Lead off" - IsOn() could still read true after a reported stop). Captured the
    // same way as memberSnapshots, before ApplyLeaderFollowerStrategies first touches the leader
    // in StartSession(), so Stop() can restore the leader to its exact pre-"startdungeon" state via
    // the same RestoreMember() helper already used for followers, instead of hoping Reset() side
    // effects happen to cover it.
    DungeonLeadMemberSnapshot leaderSnapshot;
    bool hasLeaderSnapshot = false;

    // "startdungeon test" (L1.3 first live smoke-test command): reports a structured RunResult
    // summary once the run reaches a terminal outcome, instead of just the normal chat lines.
    // Wipes are reported via wipeCount above (added by DL-013, after this comment was first
    // written - it used to say deaths/wipes weren't tracked at all, which stopped being true).
    bool testMode = false;
    uint32 testStartTs = 0;
    uint32 manualInterventions = 0;  // pause/continue/reset invoked mid-test - not a clean smoke run

    void ResetRouteProgress()
    {
        lfgId = 0;
        mapId = 0;
        instanceId = 0;
        stepIndex = 0;
        stuckTs = 0;
        stuckAttempts = 0;
        bestDist = 0.f;
        lastPathLogTs = 0;
        noRouteTold = false;
        doneTold = false;
        lastWaitLogTs = 0;
        farFromMasterTold = false;
        spreadOffenderTold.clear();
        announcedStep = -1;
        arrivedTold = false;
        arrivedTs = 0;
        packId = 0;
        packState = DungeonLeadKernel::PackState::Unknown;
        pullState = DungeonLeadKernel::PullState::None;
        pullStateTs = 0;
        pullAttempts = 0;
        pullOrderRefused = false;
        anchorSet = false;
        visited.clear();
        skippedSteps.clear();
        mandatorySkipped = false;
        outcome = DungeonRunOutcome::Running;
        failureDomain = DungeonFailureDomain::None;
        failureReason = DungeonFailureReason::None;
    }

    void Reset() { *this = DungeonLeadState(); }
};

class DungeonRouteMgr
{
public:
    static DungeonRouteMgr& instance()
    {
        static DungeonRouteMgr mgr;
        return mgr;
    }

    // Loads the route table exactly once per worldserver process, the first time any of the
    // getters below is called (std::call_once - no unsynchronized "loaded" bool read/write race).
    // Routes are immutable after that: nothing ever inserts/erases from `routes` again, so handing
    // out `DungeonRoute const*` into it and holding it past the lock below is safe.
    void Load();
    DungeonRoute const* GetByLfgId(uint32 lfgId);
    std::vector<DungeonRoute const*> GetByMap(uint32 mapId, uint32 difficulty);

    // DungeonLeadState is only ever read/written by its own bot's own AI update (one thread at a
    // time per bot - dungeon-lead never reaches into another bot's state), so the mutex here only
    // needs to protect the `states` map's own structure (insertion via operator[], erasure), not
    // the returned DungeonLeadState& itself. The one real hazard is a caller holding a reference
    // across a ResetState()/ResetRouteProgress() call on the SAME guid from the SAME call chain;
    // callers avoid that by capturing any fields they still need before resetting (see Stop()).
    DungeonLeadState& State(ObjectGuid guid);
    void ResetState(ObjectGuid guid);          // full wipe: real "startdungeon"/"stopdungeon" only
    void ResetRouteProgress(ObjectGuid guid);  // route fields only - "startdungeon reset" / re-resolution

    // Every guid with an entry here is, by definition, an active dungeon-lead session (ResetState()
    // is the only thing that erases an entry, and that only happens on a real stop). Used by
    // DungeonLead::GuardActiveSessions() to know which bots' strategy state to keep reasserting -
    // see that function for why this needs to be unbounded/ongoing rather than a one-shot check.
    std::vector<ObjectGuid> GetActiveSessionGuids();

    // Where a party enters the dungeon: the target of the map's entrance teleport (the spot real
    // players arrive at), choosing the one nearest the route's first step when a map has several
    // (Scarlet Monastery, Dire Maul, Maraudon wings). Falls back to the first walkable step.
    // Used to place test parties and to bring stranded members back. Cached per route.
    struct Entrance
    {
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;
        bool fromTrigger = false;
    };
    bool GetEntrance(DungeonRoute const& route, Entrance& out);

    // Read-only membership test - unlike State(), never creates an entry. True for any lifecycle
    // stage, so "is this bot already busy with a session" includes Starting and Stopping.
    bool HasState(ObjectGuid guid);

    // Per-instance "already killed" memory: survives a per-bot state reset (startdungeon reset, a
    // fresh ResolveRoute after a route mismatch, ...) so a boss confirmed dead once is never
    // walked back to just because its corpse/entity is no longer within probe range or a later
    // bot session lost track of it. Keyed by the WoW instance id, not the bot - shared by every
    // dungeon-lead bot in the same run. Does not survive a worldserver restart (that's fine: a
    // restart also resets the instance's own creature respawns for a fresh instance anyway).
    bool IsStepKilled(uint32 instanceId, uint32 entry);
    void MarkStepKilled(uint32 instanceId, uint32 entry);

    // Drops one instance's kill memory outright - called when that instance is actually destroyed
    // (every dungeon-lead session in it is long over by then), so this map doesn't otherwise grow
    // for as long as the process runs. Harmless to call for an instance id that was never marked.
    void ClearInstance(uint32 instanceId);

private:
    void EnsureLoaded();

    std::once_flag loadOnce;
    std::mutex mtx;
    std::unordered_map<uint32, DungeonRoute> routes;
    std::unordered_map<ObjectGuid, DungeonLeadState> states;
    std::unordered_map<uint32, std::unordered_set<uint32>> killedByInstance;
    std::unordered_map<uint32, Entrance> entranceByLfg;
};

#define sDungeonRouteMgr DungeonRouteMgr::instance()

#endif
