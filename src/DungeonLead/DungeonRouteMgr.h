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

// One step of a hand-authored dungeon route (table playerbots_dungeon_route).
// kind: boss | optional | heroic_only | event | door | skip
struct DungeonRouteStep
{
    uint32 step = 0;
    std::string kind;
    std::string boss;
    uint32 entry = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    std::string note;

    bool HasPosition() const { return entry != 0 && !(x == 0.f && y == 0.f && z == 0.f); }
    bool IsWalkable() const { return HasPosition() && (kind == "boss" || kind == "optional" || kind == "heroic_only" || kind == "event"); }
};

struct DungeonRoute
{
    uint32 lfgId = 0;
    uint32 mapId = 0;
    uint32 difficulty = 0;
    std::string name;
    std::vector<DungeonRouteStep> steps;
};

// Per-bot progress through a route (kept here instead of an AI value to survive strategy resets).
struct DungeonLeadState
{
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
    ObjectGuid ccGuid;      // creature currently moon-marked by us, if any
    uint32 ccMarkedTs = 0;  // when it was marked; if no CC lands within CcTimeoutSeconds, unmark it
    bool farFromMasterTold = false;  // one-shot "We're waiting for you!" until the player catches up
    bool paused = false;             // "startdung pause" / "startdung continue"
    int32 announcedStep = -1;        // one-shot "heading to X" per step, not spammed every tick
    bool debugMode = false;          // "startdung debug": verbose per-wait diagnostics to DungeonLeadDebug.log
    std::vector<uint8> visited;

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

    void Load();
    DungeonRoute const* GetByLfgId(uint32 lfgId);
    std::vector<DungeonRoute const*> GetByMap(uint32 mapId, uint32 difficulty);
    DungeonLeadState& State(ObjectGuid guid);
    void ResetState(ObjectGuid guid);

    // Per-instance "already killed" memory: survives a per-bot state reset (startdung reset, a
    // fresh ResolveRoute after a route mismatch, ...) so a boss confirmed dead once is never
    // walked back to just because its corpse/entity is no longer within probe range or a later
    // bot session lost track of it. Keyed by the WoW instance id, not the bot - shared by every
    // dungeon-lead bot in the same run. Does not survive a worldserver restart (that's fine: a
    // restart also resets the instance's own creature respawns for a fresh instance anyway).
    bool IsStepKilled(uint32 instanceId, uint32 entry);
    void MarkStepKilled(uint32 instanceId, uint32 entry);

private:
    void EnsureLoaded();

    bool loaded = false;
    std::mutex mtx;
    std::unordered_map<uint32, DungeonRoute> routes;
    std::unordered_map<ObjectGuid, DungeonLeadState> states;
    std::unordered_map<uint32, std::unordered_set<uint32>> killedByInstance;
};

#define sDungeonRouteMgr DungeonRouteMgr::instance()

#endif
