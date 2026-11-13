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
#include "GridDefines.h"
#include "Map.h"
#include "MapCollisionData.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
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
    // Retries of a release or of a teleport that did not happen, at most this often.
    constexpr uint32 kRetryMs = 5000;
    // A leg toward a target the search could not reach: this far along the straight line.
    constexpr float kLegAheadYd = 50.0f;
    // Marks a short leg in the `path_type` diagnostics.
    constexpr uint32 kAheadLegFlag = 0x10000;

    // Whether a path search can start where `u` stands: a one-yard probe from it (diagnostics).
    uint32 ProbeType(Unit* u)
    {
        PathGenerator probe(u);
        probe.CalculatePath(u->GetPositionX() + 1.0f, u->GetPositionY(), u->GetPositionZ());
        return uint32(probe.GetPathType());
    }

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

    // The gate the bots' own AI reads (their dead-state and follow multiplier). It is written here,
    // on the world thread, and read from the bot's map thread - a copy under a lock, never the
    // leader's session state itself. A gate not refreshed for kGateStaleMs belongs to no session.
    constexpr uint32 kGateStaleMs = 10000;
    std::mutex g_gateMtx;
    std::unordered_map<uint64, std::pair<DungeonLead::RunBackGate, uint32>> g_gates;

    void PublishGate(uint64 key, DungeonLead::RunBackGate const& gate, uint32 now)
    {
        std::lock_guard<std::mutex> lock(g_gateMtx);
        g_gates[key] = {gate, now};
        static uint32 lastPrune = 0;
        if (!lastPrune || getMSTimeDiff(lastPrune, now) > 60000)
        {
            lastPrune = now;
            for (auto it = g_gates.begin(); it != g_gates.end();)
                if (getMSTimeDiff(it->second.second, now) > kGateStaleMs)
                    it = g_gates.erase(it);
                else
                    ++it;
        }
    }

    struct Portal
    {
        float x = 0.f, y = 0.f, z = 0.f;
        float reach = DungeonLeadKernel::kPortalReachYd;
    };

    // The dungeon's portals on one outdoor map: the area triggers that teleport into the instance
    // map, by the map they stand on. Built once per (instance map, outdoor map), on the world thread.
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
    // Outside a dungeon only the navmesh tiles of loaded grids exist: the target's tile is loaded
    // first, or the search finds no polygon there and answers with a straight shortcut.
    bool WalkLeg(Player* walker, float x, float y, float z, DungeonRunBackEntry& e)
    {
        Map* map = walker->GetMap();
        if (map && !map->Instanceable())
        {
            GridCoord const gc = Acore::ComputeGridCoord(x, y);
            map->GetMapCollisionData().LoadMMapTile(gc.x_coord, gc.y_coord);
        }
        PathGenerator path(walker);
        path.CalculatePath(x, y, z);
        // No polygon at a far target (a tile not loaded, or a target just off the mesh): walk toward
        // it in a shorter leg - a point on the ground kLegAheadYd along the straight line.
        float const dist = walker->GetExactDist(x, y, z);
        if ((path.GetPathType() & PATHFIND_NOT_USING_PATH) && dist > kLegAheadYd && map)
        {
            float const t = kLegAheadYd / dist;
            float const ax = walker->GetPositionX() + (x - walker->GetPositionX()) * t;
            float const ay = walker->GetPositionY() + (y - walker->GetPositionY()) * t;
            float const az = map->GetHeight(walker->GetPhaseMask(), ax, ay, std::max(walker->GetPositionZ(), z) + 30.f);
            if (!map->Instanceable())
            {
                GridCoord const agc = Acore::ComputeGridCoord(ax, ay);
                map->GetMapCollisionData().LoadMMapTile(agc.x_coord, agc.y_coord);
            }
            if (az > INVALID_HEIGHT + 1.f)
            {
                PathGenerator ahead(walker);
                ahead.CalculatePath(ax, ay, az);
                if (!(ahead.GetPathType() & PATHFIND_NOT_USING_PATH))
                {
                    e.lastPathType = uint32(ahead.GetPathType()) | kAheadLegFlag;
                    ++e.legs;
                    if (ahead.GetPathType() & ~(PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_SHORT | PATHFIND_FARFROMPOLY_END) ||
                        ahead.GetPath().size() < 2)
                        return false;
                    Movement::PointsArray points = ahead.GetPath();
                    walker->GetMotionMaster()->Clear();
                    walker->GetMotionMaster()->MoveSplinePath(&points);
                    return true;
                }
            }
        }
        e.lastPathType = uint32(path.GetPathType());
        ++e.legs;
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

    // The walk's own leg is still running (a spline path); anything else - standing, or another
    // movement that took over - gets a new leg.
    bool OnWalkLeg(Player* walker)
    {
        return walker->isMoving() && walker->GetMotionMaster()->GetCurrentMovementGeneratorType() == ESCORT_MOTION_TYPE;
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
        e.legs = 0;
        e.lastPathType = 0;
        e.limitMs = DungeonLeadKernel::WalkLimitMs(dist, walker->GetSpeed(MOVE_RUN), maxMs);
    }

    // A fight pauses a walk: its clocks start again when the fight is over.
    void PauseWalk(DungeonRunBackEntry& e, uint32 now)
    {
        e.walkTs = now;
        e.progressTs = now;
        e.bestDist = -1.f;
    }

    void Release(Player* m)
    {
        m->DurabilityRepairAll(false, 1.0f, false);  // as upstream's own release does for bots
        WorldPacket packet(CMSG_REPOP_REQUEST);
        packet << uint8(0);
        m->GetSession()->HandleRepopRequestOpcode(packet);
    }

    bool RetryDue(uint32& ts, uint32 now)
    {
        if (ts && getMSTimeDiff(ts, now) < kRetryMs)
            return false;
        ts = now;
        return true;
    }
}

bool DungeonLead::TeleportMember(PlayerbotAI* leaderAI, Player* m, uint32_t mapId, float x, float y, float z, float o,
                                 std::string const& reason, char const* context)
{
    if (!IsSessionBot(m))
        return false;  // a real player is never moved
    float const dist = m->GetMapId() == mapId ? m->GetExactDist(x, y, z) : -1.f;
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} teleported ({} {}): map {} ({:.0f},{:.0f},{:.0f}) -> map {} "
             "({:.0f},{:.0f},{:.0f})", m->GetName(), context, reason, m->GetMapId(), m->GetPositionX(), m->GetPositionY(),
             m->GetPositionZ(), mapId, x, y, z);
    DungeonLead::RecordEvent(leaderAI, "member_teleport", m->GetName() + " " + context + " " + reason,
                             DungeonLeadKernel::JsonLine()
                                 .Str("who", m->GetName())
                                 .Str("role", Role(m))
                                 .Str("context", context)
                                 .Str("reason", reason)
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
    return m->TeleportTo(mapId, x, y, z, o);
}

DungeonLead::RunBackGate DungeonLead::RunBackGateFor(Player* bot)
{
    if (!bot)
        return RunBackGate();
    std::lock_guard<std::mutex> lock(g_gateMtx);
    auto const it = g_gates.find(bot->GetGUID().GetRawValue());
    if (it == g_gates.end() || getMSTimeDiff(it->second.second, getMSTime()) > kGateStaleMs)
        return RunBackGate();
    return it->second.first;
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

    // The party's fight: a resurrecter casts only after it, and walks inside wait for it.
    bool partyInCombat = false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* p = ref->GetSource())
            if (p->IsInWorld() && p->IsAlive() && p->GetMapId() == st.mapId && p->IsInCombat())
                partyInCombat = true;
    if (partyInCombat)
        st.partyOutOfCombatSince = 0;
    else if (!st.partyOutOfCombatSince)
        st.partyOutOfCombatSince = now;

    // Every member of the group - also one in the middle of a teleport, out of the world for a moment.
    std::set<uint64> members;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        members.insert(slot.guid.GetRawValue());

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->GetSource();
        if (!m || !m->IsInWorld() || !IsSessionBot(m))
            continue;
        uint64 const key = m->GetGUID().GetRawValue();
        bool const onMap = m->GetMapId() == st.mapId;
        RunBackGate gate;
        gate.inSession = true;
        gate.onInstanceMap = onMap;

        if (!m->IsAlive())
        {
            DungeonRunBackEntry& e = st.runBack[key];
            if (e.straggler || !e.diedTs || e.phase == RunBackPhase::Rejoining)
            {
                e = DungeonRunBackEntry();  // a new death
                e.diedTs = now;
            }
            uint32 const msDead = getMSTimeDiff(e.diedTs, now);
            bool const ghost = m->HasPlayerFlag(PLAYER_FLAGS_GHOST);
            if (msDead >= DungeonLeadKernel::kRunBackEntryMaxMs)
            {
                // Far past every bound of its own: stop holding it; the recovery decides.
                RunBackEvent(leaderAI, m, e, "fallback", DungeonLeadKernel::JsonLine().Str("reason", "time_limit"));
                st.runBack.erase(key);
                PublishGate(key, gate, now);
                continue;
            }
            if (!ghost)
            {
                // The corpse where it fell: wait for a resurrecter of the party, or release.
                DungeonLeadKernel::RezFacts rez;
                rez.partyInCombat = partyInCombat;
                rez.msDead = msDead;
                uint32 const ooc = st.partyOutOfCombatSince ? getMSTimeDiff(st.partyOutOfCombatSince, now) : 0;
                rez.msOutOfCombat = std::min(ooc, msDead);  // the cast window starts at the death at the earliest
                std::string resurrecter;
                if (onMap)
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
                else
                {
                    if (e.phase == RunBackPhase::Held)
                        e.phase = RunBackPhase::None;
                    if (m->GetSession() && RetryDue(e.retryTs, now))
                        Release(m);
                }
                gate.phase = e.phase;
                PublishGate(key, gate, now);
                continue;
            }
            if (!onMap)
            {
                // A ghost at an outdoor graveyard: walk to the dungeon's portal and step in.
                if (!haveEntrance)
                {
                    // Nowhere to bring it: upstream's own corpse run takes over.
                    e.phase = RunBackPhase::None;
                    gate.phase = e.phase;
                    PublishGate(key, gate, now);
                    continue;
                }
                Portal const* portal = NearestPortal(st.mapId, m);
                float const dist = portal ? m->GetExactDist(portal->x, portal->y, portal->z) : -1.f;
                if (e.phase != RunBackPhase::Released && e.phase != RunBackPhase::Entered)
                {
                    e.phase = RunBackPhase::Released;
                    StartWalk(e, m, dist > 0.f ? dist : 0.f, DungeonLeadKernel::kRunBackMaxMs, now);
                    RunBackEvent(leaderAI, m, e, "released",
                                 DungeonLeadKernel::JsonLine()
                                     .Num("portal_dist", dist)
                                     .Num("limit_ms", e.limitMs)
                                     .Bool("portal", portal != nullptr));
                }
                if (e.phase == RunBackPhase::Entered)
                {
                    // Sent in, still outside: the teleport did not happen - try again.
                    if (RetryDue(e.retryTs, now))
                        m->TeleportTo(st.mapId, entrance.x, entrance.y, entrance.z, entrance.o);
                    gate.phase = e.phase;
                    PublishGate(key, gate, now);
                    continue;
                }
                WalkFallback verdict = WalkFallback::NoPortal;
                if (portal)
                {
                    if (dist <= portal->reach)
                    {
                        // Into the portal, as a player steps in; the instance revives the ghost.
                        RunBackEvent(leaderAI, m, e, "at_entrance",
                                     DungeonLeadKernel::JsonLine().Str("how", "portal").Num("walk_ms", getMSTimeDiff(e.walkTs, now)));
                        m->GetMotionMaster()->Clear();
                        e.retryTs = now;
                        if (m->TeleportTo(st.mapId, entrance.x, entrance.y, entrance.z, entrance.o))
                            e.phase = RunBackPhase::Entered;
                        gate.phase = e.phase;
                        PublishGate(key, gate, now);
                        continue;
                    }
                    if (!OnWalkLeg(m))
                        e.pathFound = WalkLeg(m, portal->x, portal->y, portal->z, e);
                    verdict = Advance(e, dist, now);
                }
                if (verdict != WalkFallback::None && RetryDue(e.retryTs, now))
                {
                    RunBackEvent(leaderAI, m, e, "fallback",
                                 DungeonLeadKernel::JsonLine()
                                     .Str("reason", DungeonLeadKernel::ToString(verdict))
                                     .Num("portal_dist", dist)
                                     .Num("best_dist", e.bestDist)
                                     .Num("path_type", e.lastPathType)
                                     .Num("walker_probe", ProbeType(m))
                                     .Num("legs", e.legs));
                    if (TeleportMember(leaderAI, m, st.mapId, entrance.x, entrance.y, entrance.z, entrance.o,
                                       DungeonLeadKernel::ToString(verdict), "run_back"))
                    {
                        e.phase = RunBackPhase::Entered;
                        e.teleported = true;
                    }
                }
                gate.phase = e.phase;
                PublishGate(key, gate, now);
                continue;
            }
            // A ghost inside the dungeon: the instance revives it on entry, or it reclaims its
            // corpse on its own (upstream's corpse run is free inside).
            gate.phase = e.phase;
            PublishGate(key, gate, now);
            continue;
        }

        auto it = st.runBack.find(key);
        if (it == st.runBack.end())
        {
            PublishGate(key, gate, now);
            continue;
        }
        DungeonRunBackEntry& e = it->second;

        if (!onMap)
        {
            // Alive outside the dungeon (revived at a graveyard): there is no walk back from there.
            if (haveEntrance && RetryDue(e.retryTs, now))
            {
                RunBackEvent(leaderAI, m, e, "fallback", DungeonLeadKernel::JsonLine().Str("reason", "other_map"));
                if (TeleportMember(leaderAI, m, st.mapId, entrance.x, entrance.y, entrance.z, entrance.o,
                                   DungeonLeadKernel::ToString(WalkFallback::OtherMap), "run_back"))
                {
                    e.teleported = true;
                    e.phase = RunBackPhase::Entered;
                }
            }
            gate.phase = e.phase;
            PublishGate(key, gate, now);
            continue;
        }

        if (!e.straggler && e.phase != RunBackPhase::Rejoining)
        {
            char const* how = e.phase == RunBackPhase::Held ? "resurrected"
                              : e.phase == RunBackPhase::None ? "corpse"
                              : e.teleported ? "teleport" : "portal";
            RunBackEvent(leaderAI, m, e, "revived", DungeonLeadKernel::JsonLine().Str("how", how));
            bool const inPlace = e.phase == RunBackPhase::Held || e.phase == RunBackPhase::None;
            if (inPlace || m == leader)
            {
                // Back where the party is (or the leader itself, where the party gathers).
                st.runBack.erase(it);
                PublishGate(key, gate, now);
                continue;
            }
            e.phase = RunBackPhase::Rejoining;
            StartWalk(e, m, leader->GetMap() == m->GetMap() ? m->GetDistance(leader) : 0.f, DungeonLeadKernel::kRejoinMaxMs, now);
        }
        gate.phase = e.phase;
        gate.walkHasPath = e.pathFound;
        PublishGate(key, gate, now);

        // Rejoining: walk to the leader, who waits for the party. A fight pauses it.
        if (!leader->IsAlive() || leader->GetMap() != m->GetMap() || partyInCombat || m->IsInCombat())
        {
            PauseWalk(e, now);
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
            gate.phase = RunBackPhase::None;
            PublishGate(key, gate, now);
            continue;
        }
        if (!OnWalkLeg(m))
            e.pathFound = WalkLeg(m, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), e);
        WalkFallback const verdict = Advance(e, dist, now);
        if (verdict != WalkFallback::None && RetryDue(e.retryTs, now))
        {
            RunBackEvent(leaderAI, m, e, "fallback",
                         DungeonLeadKernel::JsonLine()
                             .Str("reason", DungeonLeadKernel::ToString(verdict))
                             .Num("leader_dist", dist)
                             .Num("best_dist", e.bestDist)
                             .Num("path_type", e.lastPathType)
                             .Num("walker_probe", ProbeType(m))
                             .Num("leader_probe", ProbeType(leader))
                             .Num("legs", e.legs));
            if (TeleportMember(leaderAI, m, leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(),
                               leader->GetPositionZ(), leader->GetOrientation(), DungeonLeadKernel::ToString(verdict),
                               e.straggler ? "straggler" : "rejoin"))
            {
                if (e.straggler)
                    ++st.stragglerBrought[key];
                e.teleported = true;
                RunBackEvent(leaderAI, m, e, "rejoined", DungeonLeadKernel::JsonLine().Str("how", "teleport"));
                st.runBack.erase(it);
                gate.phase = RunBackPhase::None;
                PublishGate(key, gate, now);
            }
        }
    }

    // Members who left the group are no longer walked.
    for (auto it = st.runBack.begin(); it != st.runBack.end();)
        if (!members.count(it->first))
            it = st.runBack.erase(it);
        else
            ++it;
}
