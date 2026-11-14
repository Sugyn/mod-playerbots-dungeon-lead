/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonQueueStart.h"

#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonLeadKernels.h"
#include "DungeonRouteMgr.h"
#include "DungeonSimPlayer.h"
#include "Group.h"
#include "GroupMgr.h"
#include "LFGMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

#include <set>
#include <vector>

namespace
{
    constexpr uint32 kTickMs = 5000;

    struct Breakup
    {
        ObjectGuid group;
        ObjectGuid player;
        uint32 sinceTs = 0;
    };
    std::vector<Breakup> g_breakups;
    // Groups this tick has decided for already (a start, a player tank, an unsupported dungeon).
    std::set<ObjectGuid> g_handled;

    bool IsPlayerLike(Player* p)
    {
        return p && !DungeonLead::IsBotActor(p);
    }

    void RunBreakups(uint32 now)
    {
        for (auto it = g_breakups.begin(); it != g_breakups.end();)
        {
            if (getMSTimeDiff(it->sinceTs, now) < DungeonLeadKernel::kQueueBreakupDelayMs)
            {
                ++it;
                continue;
            }
            if (Group* group = sGroupMgr->GetGroupByGUID(it->group.GetCounter()))
            {
                std::vector<ObjectGuid> bots;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player* m = ref->GetSource())
                        if (DungeonLead::IsBotActor(m))
                            bots.push_back(m->GetGUID());
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] queue run done - {} bot(s) leave the group", bots.size());
                for (ObjectGuid const& g : bots)
                    if (Group* still = sGroupMgr->GetGroupByGUID(it->group.GetCounter()))
                        still->RemoveMember(g, GROUP_REMOVEMETHOD_LEAVE);
            }
            g_handled.erase(it->group);
            it = g_breakups.erase(it);
        }
    }
}

void DungeonLead::ScheduleQueueBreakup(ObjectGuid group, ObjectGuid player)
{
    Breakup b;
    b.group = group;
    b.player = player;
    b.sinceTs = getMSTime();
    g_breakups.push_back(b);
}

void DungeonLead::QueueStartTick()
{
    static uint32 lastTs = 0;
    uint32 const now = getMSTime();
    if (lastTs && getMSTimeDiff(lastTs, now) < kTickMs)
        return;
    lastTs = now;
    RunBreakups(now);
    if (!sDungeonLeadConfig.dungeonLeadQueueAutoStart)
        return;

    for (auto const& kv : ObjectAccessor::GetPlayers())
    {
        Player* player = kv.second;
        if (!player || !player->IsInWorld() || !IsPlayerLike(player))
            continue;
        Group* group = player->GetGroup();
        if (!group || g_handled.count(group->GetGUID()))
            continue;

        DungeonLeadKernel::QueueStartFacts f;
        f.lfgGroup = group->isLFGGroup();
        if (!f.lfgGroup)
            continue;
        f.playerPresent = true;
        uint32 const lfgId = sLFGMgr->GetDungeon(group->GetGUID(), true);
        DungeonRoute const* route = lfgId ? sDungeonRouteMgr.GetByLfgId(lfgId) : nullptr;
        f.routeKnown = route != nullptr;
        LFGDungeonEntry const* entry = lfgId ? sLFGDungeonStore.LookupEntry(lfgId) : nullptr;
        f.inDungeon = entry ? player->GetMapId() == entry->MapID : player->GetMap()->IsDungeon();

        Player* tank = nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (!m)
                continue;
            PlayerbotAI* ai = GET_PLAYERBOT_AI(m);
            if (ai && (DungeonLead::HasSession(ai) || DungeonLead::IsOn(ai)))
                f.sessionRunning = true;
            if (m->IsInCombat())
                f.inCombat = true;
            bool const isTank = (sLFGMgr->GetRoles(m->GetGUID()) & lfg::PLAYER_ROLE_TANK) ||
                                PlayerbotAI::IsTank(m);
            if (!isTank)
                continue;
            if (IsPlayerLike(m))
                f.tankIsPlayer = true;
            else if (!tank && ai && m->GetMapId() == player->GetMapId())
                tank = m;
        }
        f.tankBotReady = tank != nullptr;

        switch (DungeonLeadKernel::DecideQueueStart(f))
        {
            case DungeonLeadKernel::QueueStart::Nothing:
            case DungeonLeadKernel::QueueStart::Wait:
                break;
            case DungeonLeadKernel::QueueStart::PlayerTanks:
                g_handled.insert(group->GetGUID());
                break;
            case DungeonLeadKernel::QueueStart::NotSupported:
                g_handled.insert(group->GetGUID());
                if (Player* any = tank)
                    if (PlayerbotAI* ai = GET_PLAYERBOT_AI(any))
                        ai->TellMasterNoFacing("Dungeon lead: no route for this dungeon - the party plays it the usual way");
                break;
            case DungeonLeadKernel::QueueStart::Start:
            {
                g_handled.insert(group->GetGUID());
                PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank);
                tankAI->SetMaster(player);
                bool const ok = DungeonLead::StartSession(tankAI, group, DungeonLeadSessionOrigin::Queue, player,
                                                          /*testMode*/ false);
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] queue group of {} in lfg {}: {} leads - StartSession={}",
                         player->GetName(), lfgId, tank->GetName(), ok);
                if (!ok)
                    g_handled.erase(group->GetGUID());
                break;
            }
        }
    }
}
