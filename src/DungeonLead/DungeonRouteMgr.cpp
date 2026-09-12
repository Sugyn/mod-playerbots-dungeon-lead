/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonRouteMgr.h"

#include "Log.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"  // Field/ResultSet are only forward-declared by DatabaseEnv.h

#include <algorithm>

void DungeonRouteMgr::Load()
{
    std::lock_guard<std::mutex> lock(mtx);
    routes.clear();

    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT lfg_id, map_id, difficulty, name, step, kind, boss, IFNULL(entry, 0), IFNULL(x, 0), IFNULL(y, 0), "
        "IFNULL(z, 0), IFNULL(note, '') FROM playerbots_dungeon_route ORDER BY lfg_id, step");

    uint32 count = 0;
    if (result)
    {
        do
        {
            Field* f = result->Fetch();
            uint32 lfgId = f[0].Get<uint32>();
            DungeonRoute& route = routes[lfgId];
            route.lfgId = lfgId;
            route.mapId = f[1].Get<uint32>();
            route.difficulty = f[2].Get<uint8>();
            route.name = f[3].Get<std::string>();

            DungeonRouteStep step;
            step.step = f[4].Get<uint32>();
            step.kind = f[5].Get<std::string>();
            step.boss = f[6].Get<std::string>();
            step.entry = f[7].Get<uint32>();
            step.x = f[8].Get<float>();
            step.y = f[9].Get<float>();
            step.z = f[10].Get<float>();
            step.note = f[11].Get<std::string>();
            route.steps.push_back(step);
            ++count;
        } while (result->NextRow());
    }

    loaded = true;
    LOG_INFO("playerbots", "Loaded {} dungeon route steps for {} LFD entries", count, routes.size());
}

void DungeonRouteMgr::EnsureLoaded()
{
    if (!loaded)
        Load();
}

DungeonRoute const* DungeonRouteMgr::GetByLfgId(uint32 lfgId)
{
    EnsureLoaded();
    std::lock_guard<std::mutex> lock(mtx);
    auto it = routes.find(lfgId);
    return it == routes.end() ? nullptr : &it->second;
}

std::vector<DungeonRoute const*> DungeonRouteMgr::GetByMap(uint32 mapId, uint32 difficulty)
{
    EnsureLoaded();
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<DungeonRoute const*> out;
    for (auto const& kv : routes)
        if (kv.second.mapId == mapId && kv.second.difficulty == difficulty)
            out.push_back(&kv.second);
    return out;
}

DungeonLeadState& DungeonRouteMgr::State(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(mtx);
    return states[guid];
}

void DungeonRouteMgr::ResetState(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(mtx);
    states.erase(guid);
}

bool DungeonRouteMgr::IsStepKilled(uint32 instanceId, uint32 entry)
{
    std::lock_guard<std::mutex> lock(mtx);
    auto it = killedByInstance.find(instanceId);
    return it != killedByInstance.end() && it->second.count(entry) > 0;
}

void DungeonRouteMgr::MarkStepKilled(uint32 instanceId, uint32 entry)
{
    std::lock_guard<std::mutex> lock(mtx);
    killedByInstance[instanceId].insert(entry);
}
