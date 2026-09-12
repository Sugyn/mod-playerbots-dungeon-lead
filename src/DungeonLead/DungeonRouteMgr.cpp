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

DungeonRouteKind ParseRouteKind(std::string const& s)
{
    if (s == "boss") return DungeonRouteKind::Boss;
    if (s == "optional") return DungeonRouteKind::Optional;
    if (s == "heroic_only") return DungeonRouteKind::HeroicOnly;
    if (s == "event") return DungeonRouteKind::Event;
    if (s == "door") return DungeonRouteKind::Door;
    if (s == "skip") return DungeonRouteKind::Skip;
    return DungeonRouteKind::Unknown;
}

void DungeonRouteMgr::Load()
{
    std::lock_guard<std::mutex> lock(mtx);
    routes.clear();

    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT lfg_id, map_id, difficulty, name, step, kind, boss, IFNULL(entry, 0), IFNULL(x, 0), IFNULL(y, 0), "
        "IFNULL(z, 0), IFNULL(note, '') FROM playerbots_dungeon_route ORDER BY lfg_id, step");

    uint32 count = 0;
    uint32 unknownKinds = 0;
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
            std::string kindStr = f[5].Get<std::string>();
            step.kind = ParseRouteKind(kindStr);
            step.boss = f[6].Get<std::string>();
            step.entry = f[7].Get<uint32>();
            step.x = f[8].Get<float>();
            step.y = f[9].Get<float>();
            step.z = f[10].Get<float>();
            step.note = f[11].Get<std::string>();

            // tools/validate_routes.py should already reject this before it ever reaches the live
            // DB, but a typo that slips through anyway used to silently become a non-walkable,
            // non-mandatory step with no diagnostic at all - never a visible failure, exactly the
            // class of bug the roadmap's L0 closeout was about.
            if (step.kind == DungeonRouteKind::Unknown)
            {
                ++unknownKinds;
                LOG_ERROR("playerbots", "DungeonRouteMgr: unknown kind '{}' for lfg_id={} step={} '{}' - "
                          "treated as unwalkable/non-mandatory", kindStr, lfgId, step.step, step.boss);
            }

            route.steps.push_back(step);
            ++count;
        } while (result->NextRow());
    }

    LOG_INFO("playerbots", "Loaded {} dungeon route steps for {} LFD entries ({} unknown kind)", count,
             routes.size(), unknownKinds);
}

void DungeonRouteMgr::EnsureLoaded()
{
    std::call_once(loadOnce, [this] { Load(); });
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

void DungeonRouteMgr::ResetRouteProgress(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(mtx);
    auto it = states.find(guid);
    if (it != states.end())
        it->second.ResetRouteProgress();
}

std::vector<ObjectGuid> DungeonRouteMgr::GetActiveSessionGuids()
{
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<ObjectGuid> guids;
    guids.reserve(states.size());
    for (auto const& kv : states)
        guids.push_back(kv.first);
    return guids;
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

char const* ToString(DungeonRunOutcome v)
{
    switch (v)
    {
        case DungeonRunOutcome::Running:  return "running";
        case DungeonRunOutcome::Complete: return "complete";
        case DungeonRunOutcome::Partial:  return "partial";
        case DungeonRunOutcome::Blocked:  return "blocked";
        case DungeonRunOutcome::Failed:   return "failed";
        case DungeonRunOutcome::Aborted:  return "aborted";
    }
    return "unknown";
}

char const* ToString(DungeonFailureDomain v)
{
    switch (v)
    {
        case DungeonFailureDomain::None:            return "none";
        case DungeonFailureDomain::Navigation:       return "navigation";
        case DungeonFailureDomain::PartyCoordination: return "party_coordination";
        case DungeonFailureDomain::PullPlanning:     return "pull_planning";
        case DungeonFailureDomain::Combat:           return "combat";
        case DungeonFailureDomain::Encounter:        return "encounter";
        case DungeonFailureDomain::Recovery:         return "recovery";
        case DungeonFailureDomain::Infrastructure:   return "infrastructure";
    }
    return "unknown";
}

char const* ToString(DungeonFailureReason v)
{
    switch (v)
    {
        case DungeonFailureReason::None:             return "none";
        case DungeonFailureReason::PathFailed:        return "path_failed";
        case DungeonFailureReason::ObjectiveTimeout:  return "objective_timeout";
        case DungeonFailureReason::BossEvade:         return "boss_evade";
        case DungeonFailureReason::PartyWipe:         return "party_wipe";
        case DungeonFailureReason::PlayerMissing:     return "player_missing";
        case DungeonFailureReason::UnsupportedEvent:  return "unsupported_event";
        case DungeonFailureReason::InternalInvariant: return "internal_invariant";
    }
    return "unknown";
}
