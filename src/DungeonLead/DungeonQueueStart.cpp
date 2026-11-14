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
#include "RandomPlayerbotMgr.h"
#include "Timer.h"

#include <map>
#include <mutex>
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
    std::vector<Breakup> g_breakups;  // under g_breakupMtx: scheduled from bot AI (map threads)
    std::mutex g_breakupMtx;
    // Groups this tick has decided for already (a start, a player tank, an unsupported dungeon), with
    // the instance it decided in: the same group in another instance - or a new group that got a
    // freed group id - is a new run. Entries of groups that are gone are dropped.
    std::map<ObjectGuid, uint32> g_handled;

    void PruneHandled()
    {
        for (auto it = g_handled.begin(); it != g_handled.end();)
            if (!sGroupMgr->GetGroupByGUID(it->first.GetCounter()))
                it = g_handled.erase(it);
            else
                ++it;
    }

    bool Handled(Group* group, Player* player)
    {
        auto it = g_handled.find(group->GetGUID());
        return it != g_handled.end() && it->second == player->GetInstanceId();
    }

    // The party's own message, said by one of its bots (the player sees it in party chat).
    void SayToParty(Group* group, Player* preferred, std::string const& text)
    {
        Player* speaker = preferred;
        if (!speaker || !GET_PLAYERBOT_AI(speaker))
        {
            speaker = nullptr;
            for (GroupReference* ref = group->GetFirstMember(); ref && !speaker; ref = ref->next())
                if (Player* m = ref->GetSource())
                    if (m->IsInWorld() && DungeonLead::IsBotActor(m) && GET_PLAYERBOT_AI(m))
                        speaker = m;
        }
        if (speaker)
            GET_PLAYERBOT_AI(speaker)->SayToParty(text);
    }

    bool IsPlayerLike(Player* p)
    {
        return p && !DungeonLead::IsBotActor(p);
    }

    void RunBreakups(uint32 now)
    {
        std::lock_guard<std::mutex> lock(g_breakupMtx);
        for (auto it = g_breakups.begin(); it != g_breakups.end();)
        {
            if (getMSTimeDiff(it->sinceTs, now) < DungeonLeadKernel::kQueueBreakupDelayMs)
            {
                ++it;
                continue;
            }
            if (Group* group = sGroupMgr->GetGroupByGUID(it->group.GetCounter()))
            {
                // every bot of the group, offline ones too; the player's own characters stay
                std::vector<ObjectGuid> bots;
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                {
                    // only server bots leave (random and AddClass), never a player's own characters
                    Player* m = ObjectAccessor::FindConnectedPlayer(slot.guid);
                    bool const serverBot = (RandomPlayerbotMgr::instance().IsRandomBot(slot.guid.GetCounter()) ||
                                            RandomPlayerbotMgr::instance().IsAddclassBot(slot.guid.GetCounter())) &&
                                           (!m || DungeonLead::IsBotActor(m));
                    if (!serverBot)
                        continue;
                    bots.push_back(slot.guid);
                    // nobody of the group keeps the player as its master after it leaves
                    if (PlayerbotAI* ai = m ? GET_PLAYERBOT_AI(m) : nullptr)
                        if (Player* master = ai->GetMaster())
                            if (master->GetGUID() == it->player)
                                ai->SetMaster(nullptr);
                }
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
    std::lock_guard<std::mutex> lock(g_breakupMtx);
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
    PruneHandled();
    if (!sDungeonLeadConfig.dungeonLeadQueueAutoStart)
        return;

    for (auto const& kv : ObjectAccessor::GetPlayers())
    {
        Player* player = kv.second;
        if (!player || !player->IsInWorld() || !IsPlayerLike(player))
            continue;
        Group* group = player->GetGroup();
        if (!group || Handled(group, player))
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
            // the role it queued in; its spec only when the finder has none for it
            uint8 const queued = sLFGMgr->GetRoles(m->GetGUID()) &
                                 (lfg::PLAYER_ROLE_TANK | lfg::PLAYER_ROLE_HEALER | lfg::PLAYER_ROLE_DAMAGE);
            bool const isTank = queued ? (queued & lfg::PLAYER_ROLE_TANK) != 0 : PlayerbotAI::IsTank(m);
            if (!isTank)
                continue;
            if (IsPlayerLike(m))
                f.tankIsPlayer = true;
            else if (!tank && ai && m->IsInWorld() && m->IsAlive() && m->GetMapId() == player->GetMapId() &&
                     m->GetInstanceId() == player->GetInstanceId())
                tank = m;
        }
        f.tankBotReady = tank != nullptr;

        switch (DungeonLeadKernel::DecideQueueStart(f))
        {
            case DungeonLeadKernel::QueueStart::Nothing:
            case DungeonLeadKernel::QueueStart::Wait:
                break;
            case DungeonLeadKernel::QueueStart::PlayerTanks:
                g_handled[group->GetGUID()] = player->GetInstanceId();
                break;
            case DungeonLeadKernel::QueueStart::NotSupported:
                g_handled[group->GetGUID()] = player->GetInstanceId();
                SayToParty(group, tank, "Dungeon lead: no route for this dungeon - the party plays it the usual way");
                break;
            case DungeonLeadKernel::QueueStart::Start:
            {
                g_handled[group->GetGUID()] = player->GetInstanceId();
                PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank);
                tankAI->SetMaster(player);
                // a simulated player follows its leader, as in a party sent in directly
                if (DungeonLead::IsSimulatedPlayer(player))
                    if (PlayerbotAI* simAI = GET_PLAYERBOT_AI(player))
                        simAI->SetMaster(tank);
                // a simulated player's run is a validation run: it reports its result
                bool const ok = DungeonLead::StartSession(tankAI, group, DungeonLeadSessionOrigin::Queue, player,
                                                          /*testMode*/ DungeonLead::IsSimulatedPlayer(player));
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] queue group of {} in lfg {}: {} leads - StartSession={}",
                         player->GetName(), lfgId, tank->GetName(), ok);
                if (!ok)
                {
                    tankAI->SetMaster(nullptr);
                    g_handled.erase(group->GetGUID());
                }
                break;
            }
        }
    }
}
