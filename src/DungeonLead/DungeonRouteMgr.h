/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DUNGEONROUTEMGR_H
#define PLAYERBOTS_DUNGEONROUTEMGR_H

#include "Common.h"
#include "ObjectGuid.h"

#include <mutex>
#include <string>
#include <unordered_map>
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

private:
    void EnsureLoaded();

    bool loaded = false;
    std::mutex mtx;
    std::unordered_map<uint32, DungeonRoute> routes;
    std::unordered_map<ObjectGuid, DungeonLeadState> states;
};

#define sDungeonRouteMgr DungeonRouteMgr::instance()

#endif
