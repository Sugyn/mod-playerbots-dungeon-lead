/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadActions.h"

#include "Creature.h"
#include "CreatureData.h"
#include "Event.h"
#include "Formations.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "Log.h"
#include "Timer.h"

#include <algorithm>
#include <cctype>
#include <list>
#include <sstream>

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------
namespace
{
    constexpr float kPathFinderDis = 70.0f;     // below this, hand the destination straight to mmaps
    constexpr float kBossSearchRange = 60.0f;
    constexpr float kCcSearchRange = 40.0f;
    constexpr float kCreatureProbeRange = 150.0f;

    Unit* IconUnit(PlayerbotAI* botAI, Group* group, uint8 index)
    {
        ObjectGuid guid = group->GetTargetIcon(index);
        return guid.IsEmpty() ? nullptr : botAI->GetUnit(guid);
    }

    bool RouteHasEntry(PlayerbotAI* botAI, uint32 entry)
    {
        DungeonLeadState& st = sDungeonRouteMgr.State(botAI->GetBot()->GetGUID());
        DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
        if (!route)
            return false;
        for (DungeonRouteStep const& s : route->steps)
            if (s.entry == entry)
                return true;
        return false;
    }

    // Always-on structured data collection (no toggle, no dependency on worldserver.conf logger
    // config being right) - one CSV row per event, plain fopen/fprintf so it works the same way
    // for every server this patch runs on. Meant to be attached to a bug report as-is.
    std::string CsvEscape(std::string s)
    {
        for (char& c : s)
            if (c == ',' || c == '"' || c == '\n')
                c = ';';
        return s;
    }

    std::string GroupMemberList(Player* bot)
    {
        std::ostringstream out;
        Group* group = bot->GetGroup();
        if (!group)
            return "";
        bool first = true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member)
                continue;
            if (!first)
                out << "|";
            first = false;
            out << member->GetName();
        }
        return out.str();
    }
}

void DungeonLead::RecordEvent(PlayerbotAI* botAI, std::string const& event, std::string const& detail)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    Player* master = botAI->GetMaster();

    static bool headerWritten = false;
    FILE* f = fopen("DungeonLeadSessions.csv", "a");
    if (!f)
        return;
    if (!headerWritten)
    {
        // cheap check: a fresh/empty file needs the header, a pre-existing one from an earlier
        // run of this same worldserver process already has it
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0)
            fprintf(f, "timestamp,player,lfg_id,dungeon,tank,group_members,event,detail\n");
        headerWritten = true;
    }

    time_t now = time(nullptr);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

    DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
    fprintf(f, "%s,\"%s\",%u,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n", ts,
            CsvEscape(master ? master->GetName() : "?").c_str(), st.lfgId,
            CsvEscape(route ? route->name : "?").c_str(), CsvEscape(bot->GetName()).c_str(),
            CsvEscape(GroupMemberList(bot)).c_str(), CsvEscape(event).c_str(), CsvEscape(detail).c_str());
    fflush(f);
    fclose(f);
}

void DungeonLead::RecordDebug(PlayerbotAI* /*botAI*/, std::string const& line)
{
    time_t now = time(nullptr);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

    FILE* f = fopen("DungeonLeadDebug.log", "a");
    if (!f)
        return;
    fprintf(f, "%s %s\n", ts, line.c_str());
    fflush(f);
    fclose(f);
}

bool DungeonLead::InFiveMan(Player* bot)
{
    Map* map = bot ? bot->GetMap() : nullptr;
    return map && map->IsNonRaidDungeon();
}

bool DungeonLead::IsOn(PlayerbotAI* botAI)
{
    return botAI && botAI->HasStrategy("dungeon lead", BOT_STATE_NON_COMBAT);
}

bool DungeonLead::GroupInCombat(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return bot->IsInCombat();

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        if (member->IsInCombat())
            return true;
    }
    return false;
}

bool DungeonLead::GroupResting(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        // bots sit down to eat/drink; a real player sitting is treated the same (AFK-ish)
        if (member->IsSitState())
            return true;
    }
    return false;
}

bool DungeonLead::HealerManaLow(PlayerbotAI* botAI)
{
    Unit* healer = botAI->GetAiObjectContext()->GetValue<Unit*>("healer low mana")->Get();
    if (!healer)
        return false;
    return healer->GetPowerPct(POWER_MANA) < float(sPlayerbotAIConfig.dungeonLeadHealerManaPct);
}

bool DungeonLead::MasterTooFar(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    if (!master || master == bot || !master->IsAlive() || master->GetMapId() != bot->GetMapId())
        return false;
    return bot->GetDistance(master) > sPlayerbotAIConfig.dungeonLeadLeash;
}

bool DungeonLead::GroupTooSpread(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    float leash = sPlayerbotAIConfig.dungeonLeadLeash;

    // never run away from the real player
    if (MasterTooFar(botAI))
        return true;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        if (bot->GetDistance(member) > leash * 1.5f)
            return true;
    }
    return false;
}

Creature* DungeonLead::FindBossNear(PlayerbotAI* botAI, float range)
{
    Player* bot = botAI->GetBot();
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();

    Creature* best = nullptr;
    float bestDist = range;
    for (ObjectGuid const& guid : targets)
    {
        Creature* c = botAI->GetCreature(guid);
        if (!c || !c->IsAlive() || !c->IsInWorld())
            continue;

        CreatureTemplate const* ct = c->GetCreatureTemplate();
        bool isBoss = c->IsDungeonBoss() || (ct && ct->rank == CREATURE_ELITE_WORLDBOSS) ||
                      RouteHasEntry(botAI, c->GetEntry());
        if (!isBoss)
            continue;

        float d = bot->GetDistance(c);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

void DungeonLead::CheckCcMark(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return;

    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (st.ccGuid.IsEmpty())
        return;

    Creature* c = botAI->GetCreature(st.ccGuid);
    if (!c || !c->IsAlive())
    {
        // dead/gone - our job is done, free the icon for whatever comes next
        if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
        st.ccGuid.Clear();
        return;
    }

    if (c->HasBreakableByDamageCrowdControlAura())
    {
        // actually crowd controlled right now: reset the grace window so a later break gets a
        // fresh chance instead of instantly expiring
        st.ccMarkedTs = getMSTime();
        return;
    }

    if (getMSTime() - st.ccMarkedTs < sPlayerbotAIConfig.dungeonLeadCcTimeoutSeconds * IN_MILLISECONDS)
        return;  // still within the grace window, give the CC class a chance to act

    // nobody managed to land CC on it (no CC-capable class in the group, spell unusable on this
    // creature, on cooldown, ...): stop excluding it from normal DPS targeting
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC on {} never landed, releasing mark", bot->GetName(), c->GetName());
    DungeonLead::RecordEvent(botAI, "cc_released", c->GetName());
    if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
        group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
    st.ccGuid.Clear();
}

void DungeonLead::Stop(PlayerbotAI* botAI, bool giveLeaderBack)
{
    Player* bot = botAI->GetBot();

    botAI->ChangeStrategy("-dungeon lead,-grind", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-dungeon lead,-mark rti", BOT_STATE_COMBAT);
    sDungeonRouteMgr.ResetState(bot->GetGUID());

    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot)
                continue;
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            if (!memberAI || !memberAI->GetAiObjectContext())
                continue;
            if (FormationValue* fv = dynamic_cast<FormationValue*>(
                    memberAI->GetAiObjectContext()->GetValue<Formation*>("formation")))
                fv->Load("chaos");
        }

        if (group->GetTargetIcon(RtiTargetValue::starIndex) == bot->GetGUID())
            group->SetTargetIcon(RtiTargetValue::starIndex, bot->GetGUID(), ObjectGuid::Empty);

        Player* master = botAI->GetMaster();
        if (giveLeaderBack && master && master != bot && group->IsMember(master->GetGUID()) &&
            group->GetLeaderGUID() == bot->GetGUID())
        {
            auto op = std::make_unique<GroupSetLeaderOperation>(bot->GetGUID(), master->GetGUID());
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// dungeon lead next: walk to the next route step
// ---------------------------------------------------------------------------------------------
bool DungeonLeadNextAction::isUseful()
{
    if (!bot || !botAI || !bot->IsAlive() || bot->IsInCombat())
        return false;
    if (!DungeonLead::InFiveMan(bot))
        return false;

    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (st.paused)
        return false;  // "startdung pause": stand still until "startdung continue"

    // "waiting for you": a one-shot chat ping on the transition into master-too-far, not spammed
    // every tick, and cleared as soon as the player is back in range.
    bool masterTooFar = DungeonLead::MasterTooFar(botAI);
    if (masterTooFar && !st.farFromMasterTold)
    {
        st.farFromMasterTold = true;
        botAI->TellMaster("We're waiting for you!");
    }
    else if (!masterTooFar)
        st.farFromMasterTold = false;

    char const* wait = nullptr;
    if (DungeonLead::GroupInCombat(botAI))
        wait = "group in combat";
    else if (DungeonLead::GroupResting(botAI))
        wait = "someone is eating/drinking";
    else if (DungeonLead::HealerManaLow(botAI))
        wait = "healer low on mana";
    else if (masterTooFar)
        wait = "waiting for you, master too far";
    else if (DungeonLead::GroupTooSpread(botAI))
        wait = "group too spread";

    if (wait)
    {
        // throttled: one line per 10 s per bot
        uint32 now = getMSTime();
        if (!st.lastWaitLogTs || now - st.lastWaitLogTs > 10000)
        {
            st.lastWaitLogTs = now;
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} waiting: {}", bot->GetName(), wait);
            DungeonLead::RecordEvent(botAI, "waiting", wait);

            // "startdung debug": enough detail on every wait to reconstruct a run from a plain
            // file alone - positions, distances to the whole group, current step - without needing
            // a verbal bug report. Meant to be turned on for one troubleshooting run and attached
            // to a bug report (see README "Debugging" section), not left on permanently. Written
            // via plain file I/O (not the AC logger config) so it works the same on every server
            // regardless of worldserver.conf logger setup.
            if (st.debugMode)
            {
                std::ostringstream dbg;
                dbg << bot->GetName() << " pos=(" << bot->GetPositionX() << ","
                    << bot->GetPositionY() << "," << bot->GetPositionZ() << ") step=" << st.stepIndex
                    << " moving=" << bot->isMoving() << " inCombat=" << bot->IsInCombat();
                if (Player* master = botAI->GetMaster())
                    if (master != bot)
                        dbg << " masterDist=" << bot->GetDistance(master);
                if (Group* group = bot->GetGroup())
                {
                    dbg << " members:";
                    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (!member || member == bot)
                            continue;
                        dbg << " " << member->GetName() << "@"
                            << (member->GetMapId() == bot->GetMapId() ? bot->GetDistance(member) : -1.0f);
                    }
                }
                DungeonLead::RecordDebug(botAI, dbg.str());
            }
        }
        // don't just refuse a new move - a move already committed on a previous tick keeps
        // playing out on its own spline otherwise, so the tank visibly "runs off" from a party
        // member who just sat down to drink or pulled something
        if (bot->isMoving())
            bot->StopMoving();
        return false;
    }
    return true;
}

DungeonRoute const* DungeonLeadNextAction::ResolveRoute(DungeonLeadState& st)
{
    uint32 mapId = bot->GetMapId();
    uint32 instanceId = bot->GetInstanceId();

    if (st.mapId == mapId && st.instanceId == instanceId && st.lfgId)
        return sDungeonRouteMgr.GetByLfgId(st.lfgId);
    if (st.mapId == mapId && st.instanceId == instanceId && st.noRouteTold)
        return nullptr;

    st.Reset();
    st.mapId = mapId;
    st.instanceId = instanceId;

    DungeonRoute const* route = nullptr;
    if (Group* group = bot->GetGroup())
    {
        uint32 lfgId = sLFGMgr->GetDungeon(group->GetGUID(), true);
        if (lfgId)
        {
            route = sDungeonRouteMgr.GetByLfgId(lfgId);
            if (route && route->mapId != mapId)
                route = nullptr;
        }
    }

    if (!route)
    {
        // entered on foot (no LFD id): pick the route on this map/difficulty whose first walkable
        // step is closest — for multi-wing dungeons that is the wing we are standing in
        uint32 difficulty = bot->GetMap() ? uint32(bot->GetMap()->GetDifficulty()) : 0;
        float best = 1e9f;
        for (DungeonRoute const* cand : sDungeonRouteMgr.GetByMap(mapId, difficulty))
        {
            for (DungeonRouteStep const& s : cand->steps)
            {
                if (!s.IsWalkable())
                    continue;
                float d = bot->GetExactDist(s.x, s.y, s.z);
                if (d < best)
                {
                    best = d;
                    route = cand;
                }
                break;
            }
        }
    }

    if (!route)
        return nullptr;

    st.lfgId = route->lfgId;
    st.visited.assign(route->steps.size(), 0);

    uint32 walkable = 0;
    for (DungeonRouteStep const& s : route->steps)
        if (s.IsWalkable())
            ++walkable;

    std::ostringstream out;
    out << "Dungeon lead: route '" << route->name << "', " << walkable << " stops";
    botAI->TellMasterNoFacing(out);
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} route lfg={} '{}' map={} steps={} walkable={}", bot->GetName(),
             route->lfgId, route->name, route->mapId, route->steps.size(), walkable);
    DungeonLead::RecordEvent(botAI, "route_selected", std::to_string(walkable) + " stops");
    return route;
}

void DungeonLeadNextAction::MarkVisited(DungeonLeadState& st)
{
    if (st.stepIndex < st.visited.size())
        st.visited[st.stepIndex] = 1;
    ++st.stepIndex;
    st.bestDist = 0.f;
    st.stuckAttempts = 0;
    st.stuckTs = 0;
}

bool DungeonLeadNextAction::Execute(Event /*event*/)
{
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    DungeonRoute const* route = ResolveRoute(st);
    if (!route)
    {
        if (!st.noRouteTold)
        {
            st.noRouteTold = true;
            botAI->TellMasterNoFacing(
                "Dungeon lead: no route for this dungeon (event/vehicle dungeon?) - I'll just grind what I see");
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} no route for map={} instance={}", bot->GetName(), bot->GetMapId(),
                     bot->GetInstanceId());
            DungeonLead::RecordEvent(botAI, "no_route", "map=" + std::to_string(bot->GetMapId()));
        }
        return false;
    }

    // advance to the next step that is worth walking to
    while (st.stepIndex < route->steps.size())
    {
        DungeonRouteStep const& s = route->steps[st.stepIndex];
        bool skip = st.visited[st.stepIndex] || !s.IsWalkable() ||
                    (s.kind == "optional" && sPlayerbotAIConfig.dungeonLeadSkipOptional) ||
                    (s.entry && sDungeonRouteMgr.IsStepKilled(st.instanceId, s.entry));
        if (!skip)
            break;
        ++st.stepIndex;
    }

    if (st.stepIndex >= route->steps.size())
    {
        if (!st.doneTold)
        {
            st.doneTold = true;
            botAI->TellMasterNoFacing("Dungeon lead: route complete");
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} route complete", bot->GetName());
            DungeonLead::RecordEvent(botAI, "route_complete", "");
        }
        return false;
    }

    DungeonRouteStep const& step = route->steps[st.stepIndex];
    WorldPosition dest(bot->GetMapId(), step.x, step.y, step.z, 0.f);

    if (st.announcedStep != int32(st.stepIndex))
    {
        st.announcedStep = int32(st.stepIndex);
        std::ostringstream headingOut;
        headingOut << "Dungeon lead: heading to " << step.boss;
        botAI->TellMasterNoFacing(headingOut);
    }

    // prefer the live creature position; a dead boss means this stop is done
    std::list<Creature*> found;
    bot->GetCreatureListWithEntryInGrid(found, step.entry, kCreatureProbeRange);
    bool anyAlive = false;
    for (Creature* c : found)
    {
        if (c->IsAlive())
        {
            anyAlive = true;
            dest = WorldPosition(c);
            break;
        }
    }
    if (!found.empty() && !anyAlive)
    {
        std::ostringstream out;
        out << "Dungeon lead: " << step.boss << " is down, moving on";
        botAI->TellMasterNoFacing(out);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}' already dead, next", bot->GetName(), step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "already_dead", step.boss);
        if (step.entry)
            sDungeonRouteMgr.MarkStepKilled(st.instanceId, step.entry);
        MarkVisited(st);
        return true;
    }

    float dist = bot->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
    if (dist <= sPlayerbotAIConfig.dungeonLeadArriveDistance)
    {
        std::ostringstream out;
        out << "Dungeon lead: reached " << step.boss;
        botAI->TellMasterNoFacing(out);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} reached step {} '{}'", bot->GetName(), step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "reached", step.boss);
        MarkVisited(st);
        return true;
    }

    return MoveRouteTo(st, dest, step);
}

// Same approach as NewRpgBaseAction::MoveFarTo (mmap route to the true destination, walk to the
// furthest reachable waypoint, cone-sample when blocked) — but a bot that makes no progress skips
// the stop instead of teleporting to it: teleporting the tank away from its group is worse than
// missing an optional boss.
bool DungeonLeadNextAction::MoveRouteTo(DungeonLeadState& st, WorldPosition const& dest, DungeonRouteStep const& step)
{
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
        return false;

    {
        LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
        if (bot->isMoving() && lastMove.lastMoveToMapId == bot->GetMapId())
        {
            float remaining = bot->GetExactDist(lastMove.lastMoveToX, lastMove.lastMoveToY, lastMove.lastMoveToZ);
            if (remaining > 10.0f)
                return true;
        }
    }

    float disToDest = bot->GetDistance(dest);
    uint32 now = getMSTime();
    if (st.bestDist == 0.f || disToDest + 5.0f < st.bestDist)
    {
        st.bestDist = disToDest;
        st.stuckTs = now;
        st.stuckAttempts = 0;
    }
    else if (++st.stuckAttempts >= 5 && st.stuckTs &&
             GetMSTimeDiffToNow(st.stuckTs) >= sPlayerbotAIConfig.dungeonLeadStuckSeconds * IN_MILLISECONDS)
    {
        std::ostringstream out;
        out << "Dungeon lead: can't reach " << step.boss << ", skipping";
        botAI->TellMasterNoFacing(out);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} stuck {}s at dist {:.1f} to step {} '{}' ({:.1f},{:.1f},{:.1f}) -> skip",
                 bot->GetName(), GetMSTimeDiffToNow(st.stuckTs) / 1000, disToDest, step.step, step.boss,
                 dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
        DungeonLead::RecordEvent(botAI, "skip_stuck",
            step.boss + " dist=" + std::to_string(disToDest) + " pos=(" + std::to_string(dest.GetPositionX()) + "," +
            std::to_string(dest.GetPositionY()) + "," + std::to_string(dest.GetPositionZ()) + ")");
        MarkVisited(st);
        return true;
    }

    float const dx = dest.GetPositionX(), dy = dest.GetPositionY(), dz = dest.GetPositionZ();
    if (disToDest < kPathFinderDis)
        return MoveTo(dest.GetMapId(), dx, dy, dz, false, false, false, true);

    uint32 const typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
    {
        PathGenerator path(bot);
        path.CalculatePath(dx, dy, dz);
        PathType type = path.GetPathType();
        bool canReach = !(type & (~typeOk));
        if (canReach)
        {
            G3D::Vector3 const& endPos = path.GetActualEndPosition();
            float endDistToDest = dest.GetExactDist(endPos.x, endPos.y, endPos.z);
            if (endDistToDest + 5.0f < disToDest)
                return MoveTo(bot->GetMapId(), endPos.x, endPos.y, endPos.z, false, false, false, true);
        }
    }

    // blocked: sample a wide-ish forward cone for a reachable stepping stone. More attempts than a
    // plain forward-only search (so a column/wall corner right on the line to dest doesn't box the
    // bot in), but still biased forward and close-range rather than a full 360 degree search -
    // a real find far off to the side/behind tends to lead away from the corridor a player would
    // actually walk, not around the obstacle and back onto it. See README "known limitations":
    // this whole fallback is a stopgap for point-to-point mmap pathing, not a real fix.
    float minDelta = static_cast<float>(M_PI);
    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    float const baseAngle = bot->GetAngle(&dest);
    float rx = 0.f, ry = 0.f, rz = 0.f;
    bool found = false;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        float delta = (rand_norm() - 0.5f) * static_cast<float>(M_PI);  // +-90 deg, forward-biased
        float sampleDis = (0.2f + rand_norm() * 0.3f) * kPathFinderDis;  // short, cautious probes
        float angle = baseAngle + delta;
        PathGenerator path(bot);
        path.CalculatePath(x + cos(angle) * sampleDis, y + sin(angle) * sampleDis, z + 0.5f);
        PathType type = path.GetPathType();
        bool canReach = !(type & (~typeOk));
        if (canReach && fabs(delta) <= minDelta)
        {
            found = true;
            G3D::Vector3 const& endPos = path.GetActualEndPosition();
            rx = endPos.x;
            ry = endPos.y;
            rz = endPos.z;
            minDelta = fabs(delta);
        }
    }
    if (found)
        return MoveTo(bot->GetMapId(), rx, ry, rz, false, false, false, true);

    return false;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead cc watch: release a stale CC mark - must run regardless of combat state
// ---------------------------------------------------------------------------------------------
bool DungeonLeadCcWatchAction::isUseful()
{
    if (!bot || !botAI || !DungeonLead::InFiveMan(bot))
        return false;
    return !sDungeonRouteMgr.State(bot->GetGUID()).ccGuid.IsEmpty();
}

bool DungeonLeadCcWatchAction::Execute(Event /*event*/)
{
    DungeonLead::CheckCcMark(botAI);
    return true;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead mark: skull on the boss, moon on a CC candidate
// ---------------------------------------------------------------------------------------------
bool DungeonLeadMarkAction::isUseful()
{
    return bot && bot->GetGroup() && DungeonLead::InFiveMan(bot);
}

Creature* DungeonLeadMarkAction::FindCcCandidate(Creature* boss)
{
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    Creature* best = nullptr;
    float bestDist = kCcSearchRange;
    for (ObjectGuid const& guid : targets)
    {
        Creature* c = botAI->GetCreature(guid);
        if (!c || c == boss || !c->IsAlive() || !c->IsInWorld() || c->IsDungeonBoss())
            continue;
        CreatureTemplate const* ct = c->GetCreatureTemplate();
        if (!ct || ct->rank != CREATURE_ELITE_ELITE)
            continue;
        float d = bot->GetDistance(c);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

bool DungeonLeadMarkAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    Creature* boss = DungeonLead::FindBossNear(botAI, kBossSearchRange);
    if (!boss)
        return false;

    bool changed = false;
    Unit* skull = IconUnit(botAI, group, RtiTargetValue::skullIndex);
    if (!skull || !skull->IsAlive())
    {
        group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), boss->GetGUID());
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} skull -> {} ({})", bot->GetName(), boss->GetName(), boss->GetEntry());
        DungeonLead::RecordEvent(botAI, "mark_skull", boss->GetName());
        changed = true;
    }

    if (sPlayerbotAIConfig.dungeonLeadMarkCc)
    {
        Unit* moon = IconUnit(botAI, group, RtiTargetValue::moonIndex);
        if (!moon || !moon->IsAlive())
        {
            if (Creature* cc = FindCcCandidate(boss))
            {
                group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), cc->GetGUID());
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} moon -> {} ({})", bot->GetName(), cc->GetName(), cc->GetEntry());
                DungeonLead::RecordEvent(botAI, "mark_moon", cc->GetName());
                DungeonLeadState& ccSt = sDungeonRouteMgr.State(bot->GetGUID());
                ccSt.ccGuid = cc->GetGUID();
                ccSt.ccMarkedTs = getMSTime();
                changed = true;
            }
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead stop (automatic, e.g. after leaving the instance)
// ---------------------------------------------------------------------------------------------
bool DungeonLeadStopAction::Execute(Event /*event*/)
{
    DungeonLead::Stop(botAI, true);
    botAI->TellMasterNoFacing("Dungeon lead: OFF (not in a 5-man dungeon anymore)");
    return true;
}

// ---------------------------------------------------------------------------------------------
// chat shortcuts: startdung / stopdung
// ---------------------------------------------------------------------------------------------
namespace
{
    std::string TrimLower(std::string s)
    {
        size_t a = s.find_first_not_of(" \t");
        size_t b = s.find_last_not_of(" \t");
        s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }
}

bool StartDungChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    std::string const sub = TrimLower(event.getParam());
    if (sub == "pause" || sub == "continue" || sub == "reset" || sub == "debug")
    {
        if (!DungeonLead::IsOn(botAI))
        {
            botAI->TellMaster("startdung: not currently leading a dungeon - use plain 'startdung' first");
            return false;
        }
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        if (sub == "pause")
        {
            st.paused = true;
            botAI->TellMaster("Dungeon lead: PAUSED - staying put until 'startdung continue'");
        }
        else if (sub == "continue")
        {
            st.paused = false;
            botAI->TellMaster("Dungeon lead: resuming");
        }
        else if (sub == "reset")
        {
            sDungeonRouteMgr.ResetState(bot->GetGUID());
            botAI->TellMaster("Dungeon lead: route progress reset, picking the route back up from the start");
        }
        else  // debug
        {
            st.debugMode = !st.debugMode;
            botAI->TellMaster(st.debugMode
                ? "Dungeon lead debug activated, file is being saved to DungeonLeadDebug.log (same folder as Playerbots.log)"
                : "Dungeon lead debug stopped, file is saved to DungeonLeadDebug.log (same folder as Playerbots.log)");
        }
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} {}", bot->GetName(), sub);
        return true;
    }

    Group* group = bot->GetGroup();
    if (!group)
    {
        botAI->TellMaster("startdung: I'm not in a group");
        return false;
    }
    if (!DungeonLead::InFiveMan(bot))
    {
        botAI->TellMaster("startdung: only works inside a 5-man dungeon");
        return false;
    }
    if (!PlayerbotAI::IsTank(bot))
        botAI->TellMaster("startdung: I'm not a tank, but fine - leading anyway");

    if (group->GetLeaderGUID() != bot->GetGUID())
    {
        auto op = std::make_unique<GroupSetLeaderOperation>(bot->GetGUID(), bot->GetGUID());
        PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op));
    }

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot)
            continue;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI || !memberAI->GetAiObjectContext())
            continue;
        if (FormationValue* fv = dynamic_cast<FormationValue*>(
                memberAI->GetAiObjectContext()->GetValue<Formation*>("formation")))
            fv->Load("leader");
        memberAI->ChangeStrategy("+follow,-passive,-stay,-grind,-dungeon lead", BOT_STATE_NON_COMBAT);
        memberAI->ChangeStrategy("+cc", BOT_STATE_COMBAT);
    }

    sDungeonRouteMgr.ResetState(bot->GetGUID());
    sDungeonRouteMgr.State(bot->GetGUID()).debugMode = sPlayerbotAIConfig.dungeonLeadDebugDefault;
    botAI->Reset();
    ResetReturnPosition();
    ResetStayPosition();
    botAI->ChangeStrategy("+dungeon lead,+grind,-passive,-stay", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("+dungeon lead,+cc,+mark rti", BOT_STATE_COMBAT);

    // mark self with the star icon: a visible "I'm leading, follow me" signal for the party
    group->SetTargetIcon(RtiTargetValue::starIndex, bot->GetGUID(), bot->GetGUID());

    botAI->TellMaster("Dungeon lead: ON - taking the lead, the others will follow me");
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} START by {} in map={} instance={} (tank={})", bot->GetName(),
             master->GetName(), bot->GetMapId(), bot->GetInstanceId(), PlayerbotAI::IsTank(bot));
    DungeonLead::RecordEvent(botAI, "start", "tank=" + std::string(PlayerbotAI::IsTank(bot) ? "yes" : "no"));
    return true;
}

bool StopDungChatShortcutAction::Execute(Event /*event*/)
{
    if (!GetMaster())
        return false;

    DungeonLead::Stop(botAI, true);
    ResetReturnPosition();
    ResetStayPosition();
    botAI->TellMaster("Dungeon lead: OFF");
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} STOP by master", bot->GetName());
    DungeonLead::RecordEvent(botAI, "stop", "");
    return true;
}
