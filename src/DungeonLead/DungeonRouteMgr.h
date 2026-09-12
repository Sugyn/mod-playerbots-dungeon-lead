/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONROUTEMGR_H
#define PLAYERBOTS_DUNGEONROUTEMGR_H

#include "Common.h"
#include "ObjectGuid.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The DB column (playerbots_dungeon_route.kind) stays a plain VARCHAR - this is just the C++-side
// representation, parsed once at Load() (see ParseRouteKind). A typo in the DB used to silently
// become a non-walkable, non-mandatory step with no diagnostic at all; it's now Unknown, logged as
// an error at load time (see DungeonRouteMgr::Load()) and validated ahead of time by
// tools/validate_routes.py besides.
enum class DungeonRouteKind : uint8
{
    Boss,
    Optional,
    HeroicOnly,
    Event,
    Door,
    Skip,
    Unknown,  // failed to parse - never IsWalkable()/IsMandatory(), always a load-time LOG_ERROR
};

DungeonRouteKind ParseRouteKind(std::string const& s);

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
    bool noRouteTold = false;
    bool doneTold = false;
    uint32 lastWaitLogTs = 0;
    bool farFromMasterTold = false;  // one-shot "We're waiting for you!" until the player catches up
    std::string spreadOffenderTold;  // name of the last bot we pinged about for GroupTooSpread, "" if none
    int32 announcedStep = -1;        // one-shot "heading to X" per step, not spammed every tick
    bool arrivedTold = false;        // one-shot "reached X" - doesn't by itself advance the route
    uint32 arrivedTs = 0;            // when arrivedTold was set; used to give up if nothing is ever found there
    std::vector<uint8> visited;
    std::vector<std::string> skippedSteps;  // bosses skipped (stuck/not-found) - reported at "route complete"
    bool mandatorySkipped = false;  // true if any of the above was IsMandatory() - run outcome PARTIAL, not COMPLETE
    DungeonRunOutcome outcome = DungeonRunOutcome::Running;
    DungeonFailureDomain failureDomain = DungeonFailureDomain::None;
    DungeonFailureReason failureReason = DungeonFailureReason::None;

    // --- session: survives a route reset, only a full Reset() (real stop/start) clears these ---
    uint64 runId = 0;      // correlates every telemetry row from one "startdungeon" session
    DungeonLeadSessionOrigin origin = DungeonLeadSessionOrigin::Manual;
    uint32 sessionStartTs = 0;  // getMSTime() at StartSession() - used by the canary controller's
                                 // timeout check; also generally useful (duration is otherwise only
                                 // reconstructable from the CSV's own "start" row timestamp)
    ObjectGuid ccGuid;      // creature currently moon-marked by us, if any
    uint32 ccMarkedTs = 0;  // when it was marked; if no CC lands within CcTimeoutSeconds, unmark it
    ObjectGuid skullGuid;   // boss currently skull-marked by us, if any (so Stop() only clears our own)
    bool paused = false;    // "startdungeon pause" / "startdungeon continue"
    bool debugMode = false; // "startdungeon debug": verbose per-wait diagnostics to DungeonLeadDebug.log
    std::vector<DungeonLeadMemberSnapshot> memberSnapshots;  // pre-"startdungeon" state, for exact restore

    // "startdungeon test" (L1.3 first live smoke-test command): reports a structured RunResult
    // summary once the run reaches a terminal outcome, instead of just the normal chat lines.
    // Deliberately does NOT track deaths/wipes yet - no death/wipe detection exists anywhere in
    // dungeon-lead today, and inventing one just to fill in a report field would be exactly the
    // kind of "new recovery behavior added only to support telemetry" the roadmap says not to do.
    // What's reported is only what's already cheaply and honestly known.
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
        noRouteTold = false;
        doneTold = false;
        lastWaitLogTs = 0;
        farFromMasterTold = false;
        spreadOffenderTold.clear();
        announcedStep = -1;
        arrivedTold = false;
        arrivedTs = 0;
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

    // Per-instance "already killed" memory: survives a per-bot state reset (startdungeon reset, a
    // fresh ResolveRoute after a route mismatch, ...) so a boss confirmed dead once is never
    // walked back to just because its corpse/entity is no longer within probe range or a later
    // bot session lost track of it. Keyed by the WoW instance id, not the bot - shared by every
    // dungeon-lead bot in the same run. Does not survive a worldserver restart (that's fine: a
    // restart also resets the instance's own creature respawns for a fresh instance anyway).
    bool IsStepKilled(uint32 instanceId, uint32 entry);
    void MarkStepKilled(uint32 instanceId, uint32 entry);

private:
    void EnsureLoaded();

    std::once_flag loadOnce;
    std::mutex mtx;
    std::unordered_map<uint32, DungeonRoute> routes;
    std::unordered_map<ObjectGuid, DungeonLeadState> states;
    std::unordered_map<uint32, std::unordered_set<uint32>> killedByInstance;
};

#define sDungeonRouteMgr DungeonRouteMgr::instance()

#endif
