/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

// Run back on foot (DungeonLeadKernels.h, "Run back on foot"): a dead bot of the party is brought
// back in place by a resurrecter of the party when one can, otherwise it releases, walks as a ghost
// from the graveyard to the dungeon's portal and steps in (the instance revives a ghost whose
// corpse lies inside), then walks to the party. A living straggler gets the same walk order to the
// leader. A teleport is only the fallback of a walk that failed within its bound, and every one
// writes a member_teleport event with its reason. A real player is never released, walked or moved.

#include "DungeonRunBack.h"

#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonLeadKernels.h"
#include "DungeonRouteMgr.h"
#include "DungeonTelemetryV2.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

using DungeonLeadKernel::RunBackPhase;
using DungeonLeadKernel::WalkFallback;

namespace
{
    // Resurrection spells usable out of combat, every rank (a bot knows the ranks of its level).
    constexpr uint32 kResurrectionSpells[] = {
        2006,  2010,  10880, 10881, 20770, 25435, 48171,  // Resurrection (priest)
        7328,  10322, 10324, 20772, 20773, 48949, 48950,  // Redemption (paladin)
        2008,  20609, 20610, 20776, 20777, 25590, 49277,  // Ancestral Spirit (shaman)
    };

    bool CanResurrect(Player* p)
    {
        for (uint32 id : kResurrectionSpells)
            if (p->HasSpell(id))
                return true;
        return false;
    }

    bool IsSessionBot(Player* p)
    {
        return p && DungeonLead::IsBotActor(p);
    }

    struct Portal
    {
        float x = 0.f, y = 0.f, z = 0.f;
        float reach = DungeonLeadKernel::kPortalReachYd;
    };

    // The dungeon's portals on one outdoor map: the area triggers that teleport into the instance
    // map, by the map they stand on. Built once per (instance map, outdoor map).
    std::vector<Portal> const& PortalsOn(uint32 instanceMap, uint32 onMap)
    {
        static std::map<std::pair<uint32, uint32>, std::vector<Portal>> cache;
        auto const key = std::make_pair(instanceMap, onMap);
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        std::vector<Portal> out;
        for (uint32 id = 1; id < 20000; ++id)
        {
            AreaTriggerTeleport const* t = sObjectMgr->GetAreaTriggerTeleport(id);
            if (!t || t->target_mapId != instanceMap)
                continue;
            AreaTrigger const* at = sObjectMgr->GetAreaTrigger(id);
            if (!at || at->map != onMap)
                continue;
            Portal p;
            p.x = at->x;
            p.y = at->y;
            p.z = at->z;
            float const size = at->radius > 0.f ? at->radius : std::max(at->length, at->width) / 2.f;
            p.reach = DungeonLeadKernel::kPortalReachYd + std::max(0.f, size);
            out.push_back(p);
        }
        return cache.emplace(key, std::move(out)).first->second;
    }

    Portal const* NearestPortal(uint32 instanceMap, Player* ghost)
    {
        Portal const* best = nullptr;
        float bestDist = 0.f;
        for (Portal const& p : PortalsOn(instanceMap, ghost->GetMapId()))
        {
            float const d = ghost->GetExactDist(p.x, p.y, p.z);
            if (!best || d < bestDist)
            {
                best = &p;
                bestDist = d;
            }
        }
        return best;
    }

    // One leg of a walk along the navmesh: the computed points, not a straight line. A long way
    // comes back as a partial path; the next leg starts when this one ends. False = no path at all.
    bool WalkLeg(Player* walker, float x, float y, float z)
    {
        PathGenerator path(walker);
        path.CalculatePath(x, y, z);
        if (path.GetPathType() & ~(PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_SHORT | PATHFIND_FARFROMPOLY_END))
            return false;
        if (path.GetPath().size() < 2)
            return walker->GetExactDist(x, y, z) < 5.0f;
        G3D::Vector3 const& end = path.GetActualEndPosition();
        if (walker->GetExactDist(end.x, end.y, end.z) < 1.5f)
            return walker->GetExactDist(x, y, z) < 5.0f;  // the mesh ends here: no way on
        Movement::PointsArray points = path.GetPath();
        walker->GetMotionMaster()->Clear();
        walker->GetMotionMaster()->MoveSplinePath(&points);
        return true;
    }

    std::string Role(Player* p)
    {
        return PlayerbotAI::IsTank(p) ? "tank" : PlayerbotAI::IsHeal(p) ? "healer" : "dps";
    }

    void RunBackEvent(PlayerbotAI* leaderAI, Player* m, DungeonRunBackEntry const& e, char const* phase,
                      DungeonLeadKernel::JsonLine extra = DungeonLeadKernel::JsonLine())
    {
        uint32 const ms = e.diedTs ? GetMSTimeDiffToNow(e.diedTs) : 0;
        std::string const payload = extra.Str("phase", phase)
                                        .Str("who", m->GetName())
                                        .Str("role", Role(m))
                                        .Bool("straggler", e.straggler)
                                        .Num("ms", ms)
                                        .Num("map", m->GetMapId())
                                        .Num("x", m->GetPositionX())
                                        .Num("y", m->GetPositionY())
                                        .Num("z", m->GetPositionZ())
                                        .Done();
        DungeonLead::RecordEvent(leaderAI, "run_back", std::string(phase) + " " + m->GetName() + " ms=" + std::to_string(ms),
                                 payload);
    }

    // The fallback: a walk that failed within its bound. Always with an event and its reason.
    void TeleportMember(PlayerbotAI* leaderAI, Player* m, uint32 mapId, float x, float y, float z, float o,
                        WalkFallback reason, char const* context)
    {
        float const dist = m->GetMapId() == mapId ? m->GetExactDist(x, y, z) : -1.f;
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} teleported ({} {}): map {} ({:.0f},{:.0f},{:.0f}) -> map {} "
                 "({:.0f},{:.0f},{:.0f})", m->GetName(), context, DungeonLeadKernel::ToString(reason), m->GetMapId(),
                 m->GetPositionX(), m->GetPositionY(), m->GetPositionZ(), mapId, x, y, z);
        DungeonLead::RecordEvent(leaderAI, "member_teleport",
                                 m->GetName() + " " + context + " " + DungeonLeadKernel::ToString(reason),
                                 DungeonLeadKernel::JsonLine()
                                     .Str("who", m->GetName())
                                     .Str("role", Role(m))
                                     .Str("context", context)
                                     .Str("reason", DungeonLeadKernel::ToString(reason))
                                     .Num("from_map", m->GetMapId())
                                     .Num("from_x", m->GetPositionX())
                                     .Num("from_y", m->GetPositionY())
                                     .Num("from_z", m->GetPositionZ())
                                     .Num("to_map", mapId)
                                     .Num("to_x", x)
                                     .Num("to_y", y)
                                     .Num("to_z", z)
                                     .Num("distance", dist)
                                     .Done());
        m->GetMotionMaster()->Clear();
        m->TeleportTo(mapId, x, y, z, o);
    }

    // Progress bookkeeping of a walk toward a target `dist` away; returns the fallback verdict.
    WalkFallback Advance(DungeonRunBackEntry& e, float dist, uint32 now)
    {
        if (e.bestDist < 0.f || e.bestDist - dist >= DungeonLeadKernel::kWalkProgressYd)
        {
            e.bestDist = dist;
            e.progressTs = now;
        }
        DungeonLeadKernel::WalkFacts f;
        f.pathFound = e.pathFound;
        f.msWalking = getMSTimeDiff(e.walkTs, now);
        f.limitMs = e.limitMs;
        f.msSinceProgress = getMSTimeDiff(e.progressTs, now);
        return DungeonLeadKernel::DecideWalk(f);
    }

    void StartWalk(DungeonRunBackEntry& e, Player* walker, float dist, uint32 maxMs, uint32 now)
    {
        e.walkTs = now;
        e.progressTs = now;
        e.bestDist = -1.f;
        e.pathFound = true;
        e.limitMs = DungeonLeadKernel::WalkLimitMs(dist, walker->GetSpeed(MOVE_RUN), maxMs);
    }

    void Release(Player* m)
    {
        m->DurabilityRepairAll(false, 1.0f, false);  // as upstream's own release does for bots
        WorldPacket packet(CMSG_REPOP_REQUEST);
        packet << uint8(0);
        m->GetSession()->HandleRepopRequestOpcode(packet);
    }
}

DungeonLead::RunBackGate DungeonLead::RunBackGateFor(Player* bot)
{
    RunBackGate gate;
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group || !IsBotActor(bot))
        return gate;
    ObjectGuid const leaderGuid = group->GetLeaderGUID();
    if (!sDungeonRouteMgr.HasState(leaderGuid))
        return gate;
    Player* leader = ObjectAccessor::FindPlayer(leaderGuid);
    PlayerbotAI* leaderAI = leader ? GET_PLAYERBOT_AI(leader) : nullptr;
    if (!leaderAI || !IsOn(leaderAI))
        return gate;
    DungeonLeadState& st = sDungeonRouteMgr.State(leaderGuid);
    gate.inSession = true;
    gate.onInstanceMap = bot->GetMapId() == st.mapId;
    auto const it = st.runBack.find(bot->GetGUID().GetRawValue());
    if (it != st.runBack.end())
        gate.phase = it->second.phase;
    return gate;
}

bool DungeonLead::RunBackActive(DungeonLeadState const& st, Player* who)
{
    return who && st.runBack.count(who->GetGUID().GetRawValue()) != 0;
}

bool DungeonLead::RunBackAny(DungeonLeadState const& st)
{
    return !st.runBack.empty();
}

void DungeonLead::OrderStragglerWalk(PlayerbotAI* leaderAI, DungeonLeadState& st, Player* who)
{
    Player* leader = leaderAI->GetBot();
    if (!IsSessionBot(who) || !who->IsAlive() || who == leader || who->GetMap() != leader->GetMap())
        return;
    uint64 const key = who->GetGUID().GetRawValue();
    if (st.runBack.count(key))
        return;
    uint32 const now = getMSTime();
    DungeonRunBackEntry& e = st.runBack[key];
    e.straggler = true;
    e.phase = RunBackPhase::Rejoining;
    e.diedTs = now;
    StartWalk(e, who, who->GetDistance(leader), DungeonLeadKernel::kRejoinMaxMs, now);
    RunBackEvent(leaderAI, who, e, "walk_order",
                 DungeonLeadKernel::JsonLine().Num("leader_dist", who->GetDistance(leader)).Num("limit_ms", e.limitMs));
}

void DungeonLead::RunBackMembers(PlayerbotAI* leaderAI, DungeonLeadState& st)
{
    Player* leader = leaderAI ? leaderAI->GetBot() : nullptr;
    Group* group = leader ? leader->GetGroup() : nullptr;
    if (!group || !st.mapId)
        return;
    DungeonRoute const* route = sDungeonRouteMgr.GetByLfgId(st.lfgId);
    DungeonRouteMgr::Entrance entrance;
    bool const haveEntrance = route && sDungeonRouteMgr.GetEntrance(*route, entrance);
    uint32 const now = getMSTime();

    // The party's fight: a resurrecter casts only after it.
    bool partyInCombat = false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* p = ref->GetSource())
            if (p->IsInWorld() && p->IsAlive() && p->GetMapId() == st.mapId && p->IsInCombat())
                partyInCombat = true;
    if (partyInCombat)
        st.partyOutOfCombatSince = 0;
    else if (!st.partyOutOfCombatSince)
        st.partyOutOfCombatSince = now;

    std::vector<uint64> present;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->GetSource();
        if (!m || !m->IsInWorld() || !IsSessionBot(m))
            continue;
        uint64 const key = m->GetGUID().GetRawValue();
        present.push_back(key);
        bool const onMap = m->GetMapId() == st.mapId;

        if (!m->IsAlive())
        {
            DungeonRunBackEntry& e = st.runBack[key];
            if (e.straggler || !e.diedTs)
            {
                e = DungeonRunBackEntry();
                e.diedTs = now;
            }
            bool const ghost = m->HasPlayerFlag(PLAYER_FLAGS_GHOST);
            if (!ghost && onMap)
            {
                // The corpse where it fell: wait for a resurrecter of the party, or release.
                DungeonLeadKernel::RezFacts rez;
                rez.partyInCombat = partyInCombat;
                rez.msDead = getMSTimeDiff(e.diedTs, now);
                rez.msOutOfCombat = st.partyOutOfCombatSince ? getMSTimeDiff(st.partyOutOfCombatSince, now) : 0;
                std::string resurrecter;
                for (GroupReference* r2 = group->GetFirstMember(); r2; r2 = r2->next())
                {
                    Player* p = r2->GetSource();
                    if (p && p != m && p->IsInWorld() && p->IsAlive() && p->GetMapId() == st.mapId &&
                        IsSessionBot(p) && CanResurrect(p))
                    {
                        rez.resurrecterAlive = true;
                        resurrecter = p->GetName();
                        break;
                    }
                }
                if (DungeonLeadKernel::HoldRelease(rez))
                {
                    if (e.phase != RunBackPhase::Held)
                    {
                        e.phase = RunBackPhase::Held;
                        RunBackEvent(leaderAI, m, e, "held", DungeonLeadKernel::JsonLine().Str("resurrecter", resurrecter));
                    }
                }
                else if (m->GetSession())
                    Release(m);
                continue;
            }
            if (!onMap)
            {
                // A ghost at an outdoor graveyard: walk to the dungeon's portal and step in.
                Portal const* portal = NearestPortal(st.mapId, m);
                float const dist = portal ? m->GetExactDist(portal->x, portal->y, portal->z) : -1.f;
                if (e.phase != RunBackPhase::Released)
                {
                    e.phase = RunBackPhase::Released;
                    StartWalk(e, m, dist > 0.f ? dist : 0.f, DungeonLeadKernel::kRunBackMaxMs, now);
                    RunBackEvent(leaderAI, m, e, "released",
                                 DungeonLeadKernel::JsonLine()
                                     .Num("portal_dist", dist)
                                     .Num("limit_ms", e.limitMs)
                                     .Bool("portal", portal != nullptr));
                }
                if (!haveEntrance)
                    continue;
                WalkFallback verdict = WalkFallback::NoPortal;
                if (portal)
                {
                    if (dist <= portal->reach)
                    {
                        // Into the portal, as a player steps in; the instance revives the ghost.
                        e.phase = RunBackPhase::Entered;
                        RunBackEvent(leaderAI, m, e, "at_entrance",
                                     DungeonLeadKernel::JsonLine().Str("how", "portal").Num("walk_ms", getMSTimeDiff(e.walkTs, now)));
                        m->GetMotionMaster()->Clear();
                        m->TeleportTo(st.mapId, entrance.x, entrance.y, entrance.z, entrance.o);
                        continue;
                    }
                    if (!m->isMoving())
                        e.pathFound = WalkLeg(m, portal->x, portal->y, portal->z);
                    verdict = Advance(e, dist, now);
                }
                if (verdict != WalkFallback::None)
                {
                    RunBackEvent(leaderAI, m, e, "fallback",
                                 DungeonLeadKernel::JsonLine().Str("reason", DungeonLeadKernel::ToString(verdict)).Num("portal_dist", dist));
                    TeleportMember(leaderAI, m, st.mapId, entrance.x, entrance.y, entrance.z, entrance.o, verdict, "run_back");
                    e.phase = RunBackPhase::Entered;
                    e.teleported = true;
                }
                continue;
            }
            // A ghost inside the dungeon: the instance revives it on entry, or it reclaims its
            // corpse on its own (upstream's corpse run is free inside).
            continue;
        }

        auto it = st.runBack.find(key);
        if (it == st.runBack.end())
            continue;
        DungeonRunBackEntry& e = it->second;

        if (!onMap)
        {
            // Alive outside the dungeon (revived at a graveyard): there is no walk back from there.
            if (haveEntrance)
                TeleportMember(leaderAI, m, st.mapId, entrance.x, entrance.y, entrance.z, entrance.o, WalkFallback::OtherMap,
                               "run_back");
            e.teleported = true;
            e.phase = RunBackPhase::Entered;
            continue;
        }

        if (!e.straggler && e.phase != RunBackPhase::Rejoining)
        {
            bool const inPlace = e.phase == RunBackPhase::Held || e.phase == RunBackPhase::None;
            RunBackEvent(leaderAI, m, e, "revived",
                         DungeonLeadKernel::JsonLine().Str("how", inPlace ? "resurrected" : e.teleported ? "teleport" : "portal"));
            if (inPlace || m == leader)
            {
                // Back where the party is (or the leader itself, where the party gathers).
                st.runBack.erase(it);
                continue;
            }
            e.phase = RunBackPhase::Rejoining;
            StartWalk(e, m, leader->GetMap() == m->GetMap() ? m->GetDistance(leader) : 0.f, DungeonLeadKernel::kRejoinMaxMs, now);
        }

        // Rejoining: walk to the leader, who waits for the party.
        if (!leader->IsAlive() || leader->GetMap() != m->GetMap())
        {
            e.walkTs = e.progressTs = now;  // the walk waits for the leader
            continue;
        }
        float const dist = m->GetDistance(leader);
        if (DungeonLeadKernel::Rejoined(dist, sDungeonLeadConfig.dungeonLeadPartySoftRange))
        {
            RunBackEvent(leaderAI, m, e, "rejoined",
                         DungeonLeadKernel::JsonLine()
                             .Str("how", e.teleported ? "teleport" : "walk")
                             .Num("walk_ms", e.walkTs ? getMSTimeDiff(e.walkTs, now) : 0u));
            st.runBack.erase(it);
            continue;
        }
        if (!m->isMoving())
            e.pathFound = WalkLeg(m, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ());
        WalkFallback const verdict = Advance(e, dist, now);
        if (verdict != WalkFallback::None)
        {
            RunBackEvent(leaderAI, m, e, "fallback",
                         DungeonLeadKernel::JsonLine().Str("reason", DungeonLeadKernel::ToString(verdict)).Num("leader_dist", dist));
            TeleportMember(leaderAI, m, leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(),
                           leader->GetPositionZ(), leader->GetOrientation(), verdict, e.straggler ? "straggler" : "rejoin");
            if (e.straggler)
                ++st.stragglerBrought[key];
            e.teleported = true;
            RunBackEvent(leaderAI, m, e, "rejoined", DungeonLeadKernel::JsonLine().Str("how", "teleport"));
            st.runBack.erase(it);
        }
    }

    // Members who left the group are no longer walked.
    for (auto it = st.runBack.begin(); it != st.runBack.end();)
        if (std::find(present.begin(), present.end(), it->first) == present.end())
            it = st.runBack.erase(it);
        else
            ++it;
}
