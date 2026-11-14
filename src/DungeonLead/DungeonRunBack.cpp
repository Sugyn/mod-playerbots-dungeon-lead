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
#include "MapDefines.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

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
    // Marks a short leg / a leg over the ground in the `path_type` diagnostics.
    constexpr uint32 kAheadLegFlag = 0x10000;
    constexpr uint32 kGroundLegFlag = 0x20000;
    constexpr uint32 kOwnPathFlag = 0x40000;

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

    // The run back's own path search on the map's navmesh. The core's search for bots leaves out
    // steep ground (and lava), so some ways a player walks - down into a cleft, into a mine - do not
    // exist for a bot; a party member walking back may use them: steep ground for everyone, lava
    // and slime for a ghost only (a ghost takes no damage). Coordinates in Detour order (y, z, x).
    // Fills `out` with the corner points of the way (to the target, or as far as it goes); false =
    // no polygon at either end or no way at all. `reached`: the way ends at the target's polygon.
    constexpr int kMaxPolys = 1024;
    // The map's shared query searches at most 1024 nodes - too few for a long way down a mine or a
    // cleft, which then ends at the nearest point above. The run back keeps its own query with a
    // larger node pool for the outdoor maps (world thread only); their navmesh lives as long as the
    // server. Inside an instance - small, and unloaded with the instance - the map's query is used.
    constexpr int kOwnQueryNodes = 16384;
    dtNavMeshQuery const* OwnQuery(dtNavMesh const* mesh)
    {
        static std::map<dtNavMesh const*, dtNavMeshQuery*> queries;
        auto it = queries.find(mesh);
        if (it != queries.end())
            return it->second;
        dtNavMeshQuery* q = dtAllocNavMeshQuery();
        if (q && dtStatusFailed(q->init(mesh, kOwnQueryNodes)))
        {
            dtFreeNavMeshQuery(q);
            q = nullptr;
        }
        queries[mesh] = q;
        return q;
    }
    constexpr int kMaxCorners = 256;
    constexpr float kLegStepYd = 4.0f;
    constexpr float kMaxLegYd = 200.0f;
    constexpr size_t kMaxLegPoints = 74;
    bool OwnPath(Player* walker, float x, float y, float z, bool ghost, Movement::PointsArray& out, bool& reached)
    {
        Map* map = walker->GetMap();
        if (!map)
            return false;
        MMapData& mmap = map->GetMapCollisionData().GetMMapData();
        dtNavMesh const* mesh = mmap.GetNavMesh();
        dtNavMeshQuery const* query = !mesh ? nullptr : map->Instanceable() ? mmap.GetNavMeshQuery() : OwnQuery(mesh);
        if (!mesh || !query)
            return false;
        dtQueryFilter filter;
        uint16 include = NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER;
        if (ghost)
            include |= NAV_MAGMA | NAV_SLIME;
        filter.setIncludeFlags(include);
        filter.setExcludeFlags(ghost ? 0 : (NAV_MAGMA | NAV_SLIME));

        float const start[3] = {walker->GetPositionY(), walker->GetPositionZ(), walker->GetPositionX()};
        float const end[3] = {y, z, x};
        float const nearExt[3] = {3.0f, 5.0f, 3.0f};
        float const farExt[3] = {10.0f, 20.0f, 10.0f};
        dtPolyRef startRef = 0, endRef = 0;
        float startPt[3], endPt[3];
        query->findNearestPoly(start, nearExt, &filter, &startRef, startPt);
        if (!startRef)
            query->findNearestPoly(start, farExt, &filter, &startRef, startPt);
        query->findNearestPoly(end, nearExt, &filter, &endRef, endPt);
        if (!endRef)
            query->findNearestPoly(end, farExt, &filter, &endRef, endPt);
        if (!startRef || !endRef)
            return false;

        dtPolyRef polys[kMaxPolys];
        int polyCount = 0;
        if (dtStatusFailed(query->findPath(startRef, endRef, startPt, endPt, &filter, polys, &polyCount, kMaxPolys)) ||
            polyCount <= 0)
            return false;
        reached = polys[polyCount - 1] == endRef;
        float target[3] = {endPt[0], endPt[1], endPt[2]};
        if (!reached && dtStatusFailed(query->closestPointOnPoly(polys[polyCount - 1], endPt, target, nullptr)))
            return false;

        float corners[kMaxCorners * 3];
        int cornerCount = 0;
        if (dtStatusFailed(query->findStraightPath(startPt, target, polys, polyCount, corners, nullptr, nullptr,
                                                   &cornerCount, kMaxCorners)) ||
            cornerCount < 2)
            return false;
        // Corners can be far apart outside: walk between them in short steps with each point's height
        // from the navmesh polygon under it, so a leg neither cuts through a hill nor floats; and keep
        // a leg short enough for the movement packets (kMaxLegYd, kMaxLegPoints).
        out.clear();
        G3D::Vector3 prev(walker->GetPositionX(), walker->GetPositionY(), walker->GetPositionZ());
        out.push_back(prev);
        float legLength = 0.f;
        for (int i = 1; i < cornerCount && out.size() < kMaxLegPoints && legLength < kMaxLegYd; ++i)
        {
            G3D::Vector3 const corner(corners[i * 3 + 2], corners[i * 3 + 0], corners[i * 3 + 1]);
            float const segment = (corner - prev).length();
            int const steps = std::max(1, int(std::ceil(segment / kLegStepYd)));
            for (int s = 1; s <= steps && out.size() < kMaxLegPoints && legLength < kMaxLegYd; ++s)
            {
                G3D::Vector3 p = prev + (corner - prev) * (float(s) / steps);
                float const pt[3] = {p.y, p.z, p.x};
                dtPolyRef ref = 0;
                float onPoly[3];
                float h = 0.f;
                if (dtStatusSucceed(query->findNearestPoly(pt, nearExt, &filter, &ref, onPoly)) && ref &&
                    dtStatusSucceed(query->getPolyHeight(ref, onPoly, &h)))
                    p.z = h;
                legLength += (p - out.back()).length();
                out.push_back(p);
            }
            prev = corner;
        }
        if (out.size() < kMaxLegPoints && legLength < kMaxLegYd)
            return true;
        reached = false;  // the leg was cut: the next one goes on from its end
        return true;
    }

    // A navmesh tile of an outdoor map loaded for a walk, at most once per kTileRetryMs (loading reads
    // the tile file; a grid that unloads later may take its tile with it), and at most
    // kTilesPerPass tile files per guard pass - the rest follow on the next passes. A tile loaded for
    // a grid that never loads stays in memory (a few tiles per dungeon portal's surroundings).
    constexpr uint32 kTileRetryMs = 5 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 kTilesPerPass = 4;
    uint32 g_tileBudget = kTilesPerPass;
    void EnsureTile(Map* map, uint32 gx, uint32 gy)
    {
        static std::map<uint64, uint32> loaded;
        uint64 const key = (uint64(map->GetId()) << 16) | (gx << 8) | gy;
        uint32 const now = getMSTime();
        auto it = loaded.find(key);
        if ((it != loaded.end() && getMSTimeDiff(it->second, now) < kTileRetryMs) || !g_tileBudget)
            return;
        --g_tileBudget;
        loaded[key] = now;
        map->GetMapCollisionData().LoadMMapTile(gx, gy);
    }

    // One leg of a walk along the navmesh: the computed points, not a straight line. A long way
    // comes back as a partial path; the next leg starts when this one ends. False = no path at all.
    // Outside a dungeon only the navmesh tiles of loaded grids exist: the tiles between the walker
    // and its target are loaded first (EnsureTile, a few per pass), or the search ends at the point
    // nearest the target as the crow flies.
    bool WalkLeg(Player* walker, float x, float y, float z, DungeonRunBackEntry& e)
    {
        Map* map = walker->GetMap();
        if (map && !map->Instanceable())
        {
            // Every navmesh tile between the walker and its target (and one around): a search
            // through tiles that are not loaded ends at the point nearest the target as the crow
            // flies - on a hill above a cave mouth - instead of finding the way in.
            GridCoord const a = Acore::ComputeGridCoord(walker->GetPositionX(), walker->GetPositionY());
            GridCoord const b = Acore::ComputeGridCoord(x, y);
            uint32 const x0 = std::min(a.x_coord, b.x_coord), x1 = std::max(a.x_coord, b.x_coord);
            uint32 const y0 = std::min(a.y_coord, b.y_coord), y1 = std::max(a.y_coord, b.y_coord);
            for (uint32 gx = x0 ? x0 - 1 : 0; gx <= std::min(x1 + 1, uint32(MAX_NUMBER_OF_GRIDS - 1)); ++gx)
                for (uint32 gy = y0 ? y0 - 1 : 0; gy <= std::min(y1 + 1, uint32(MAX_NUMBER_OF_GRIDS - 1)); ++gy)
                    EnsureTile(map, gx, gy);
        }
        // The run back's own search first (steep ground included); the core's search below is the
        // fallback when it finds nothing.
        {
            Movement::PointsArray own;
            bool reached = false;
            if (OwnPath(walker, x, y, z, walker->HasPlayerFlag(PLAYER_FLAGS_GHOST), own, reached))
            {
                G3D::Vector3 const& last = own.back();
                if (walker->GetExactDist(last.x, last.y, last.z) >= 1.5f)
                {
                    e.lastPathType = reached ? kOwnPathFlag : (kOwnPathFlag | PATHFIND_INCOMPLETE);
                    ++e.legs;
                    walker->GetMotionMaster()->Clear();
                    walker->GetMotionMaster()->MoveSplinePath(&own);
                    return true;
                }
            }
        }
        PathGenerator path(walker);
        path.CalculatePath(x, y, z);
        // No polygon at a far target (a tile not loaded, or a target just off the mesh): walk toward
        // it in a shorter leg - a point on the ground kLegAheadYd along the straight line.
        float const dist = walker->GetExactDist(x, y, z);
        if ((path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)) && dist > kLegAheadYd && map)
        {
            float const t = kLegAheadYd / dist;
            float const ax = walker->GetPositionX() + (x - walker->GetPositionX()) * t;
            float const ay = walker->GetPositionY() + (y - walker->GetPositionY()) * t;
            float const az = map->GetHeight(walker->GetPhaseMask(), ax, ay, std::max(walker->GetPositionZ(), z) + 30.f);
            if (!map->Instanceable())
            {
                GridCoord const agc = Acore::ComputeGridCoord(ax, ay);
                EnsureTile(map, agc.x_coord, agc.y_coord);
            }
            if (az > INVALID_HEIGHT + 1.f)
            {
                PathGenerator ahead(walker);
                ahead.CalculatePath(ax, ay, az);
                if (!(ahead.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)))
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

    // A ghost outside a dungeon whose navmesh search finds no way (bots may not path over steep
    // ground or lava): a leg over the ground itself - points every kGroundStepYd along the straight
    // line, each on the floor found below the last one, as far as the ground goes on without a drop.
    // A ghost is not seen or attacked, so it may cross what a bot's navmesh filter leaves out.
    constexpr float kGroundStepYd = 5.0f;
    constexpr float kGroundLegYd = 60.0f;
    constexpr float kGroundMaxStepZ = 6.0f;
    bool GroundLeg(Player* ghost, float x, float y)
    {
        Map* map = ghost->GetMap();
        if (!map || map->Instanceable())
            return false;
        float const dx = x - ghost->GetPositionX(), dy = y - ghost->GetPositionY();
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f)
            return false;
        Movement::PointsArray points;
        points.push_back(G3D::Vector3(ghost->GetPositionX(), ghost->GetPositionY(), ghost->GetPositionZ()));
        float lastZ = ghost->GetPositionZ();
        for (float d = kGroundStepYd; d <= std::min(len, kGroundLegYd); d += kGroundStepYd)
        {
            float const px = ghost->GetPositionX() + dx / len * d, py = ghost->GetPositionY() + dy / len * d;
            float const pz = map->GetHeight(ghost->GetPhaseMask(), px, py, lastZ + kGroundMaxStepZ);
            if (pz <= INVALID_HEIGHT + 1.f || std::fabs(pz - lastZ) > kGroundMaxStepZ)
                break;
            // no step through a wall or the rock: the last point must see the next one
            G3D::Vector3 const& last = points.back();
            if (!map->isInLineOfSight(last.x, last.y, last.z + 1.5f, px, py, pz + 1.5f, ghost->GetPhaseMask(),
                                      LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2))
                break;
            points.push_back(G3D::Vector3(px, py, pz));
            lastZ = pz;
        }
        if (points.size() < 2)
            return false;
        ghost->GetMotionMaster()->Clear();
        ghost->GetMotionMaster()->MoveSplinePath(&points);
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
    g_tileBudget = kTilesPerPass;

    if (!sDungeonLeadConfig.dungeonLeadRunBackOnFoot)
    {
        // The run back is switched off: no gates (upstream's release and corpse run work as they
        // do outside a session), and a dead bot released outside the dungeon is put back at the
        // entrance, from where its corpse run is a walk inside the dungeon.
        st.runBack.clear();
        if (!haveEntrance)
            return;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (!m || !m->IsInWorld() || !IsSessionBot(m))
                continue;
            bool const deadOutside = !m->IsAlive() && m->GetMapId() != st.mapId;
            if (!DungeonLeadKernel::TeleportReturnDue(false, deadOutside, st.teleportReturnTs != 0,
                                                      st.teleportReturnTs ? getMSTimeDiff(st.teleportReturnTs, now) : 0))
                continue;
            if (TeleportMember(leaderAI, m, st.mapId, entrance.x, entrance.y, entrance.z, entrance.o, "teleport_return",
                               "run_back"))
                st.teleportReturnTs = now;
        }
        return;
    }

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
                    {
                        // the navmesh; over the ground only where it finds no way at all (a straight
                        // line toward a portal in a cave leads away from its mouth)
                        e.pathFound = WalkLeg(m, portal->x, portal->y, portal->z, e);
                        if (!e.pathFound && GroundLeg(m, portal->x, portal->y))
                        {
                            e.pathFound = true;
                            e.lastPathType |= kGroundLegFlag;
                        }
                    }
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
        {
            e.pathFound = WalkLeg(m, leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), e);
            gate.walkHasPath = e.pathFound;  // the follow gate learns this pass's answer
            PublishGate(key, gate, now);
        }
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
