/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonSimPlayer.h"

#include "DungeonLeadActions.h"
#include "DungeonLeadKernels.h"
#include "DungeonRouteMgr.h"
#include "DungeonTelemetryV2.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"

#include <mutex>
#include <random>
#include <unordered_map>

using DungeonLeadKernel::SimAction;

namespace
{
    struct SimState
    {
        uint32 seed = 0;
        std::mt19937 rng;
        SimAction action = SimAction::Follow;
        uint32 actionSince = 0;
        uint32 actionMs = 0;
        uint32 lastDecideTs = 0;
        bool loggedOut = false;
        bool remark = false;  // logged back in: its own strategies and master to set again
        bool left = false;
        uint32 deadSince = 0;
        uint32 rezAfterMs = 0;
    };

    // Which characters are simulated players: read from every thread (the teleport hook, bot AI).
    std::mutex g_mtx;
    std::unordered_map<uint64, uint32> g_marked;  // guid -> seed
    // The script state: world thread only (GuardActiveSessions).
    std::unordered_map<uint64, SimState> g_state;

    SimState& StateFor(uint64 key, uint32 seed)
    {
        auto it = g_state.find(key);
        if (it == g_state.end())
        {
            SimState s;
            s.seed = seed;
            s.rng.seed(seed);
            it = g_state.emplace(key, s).first;
        }
        return it->second;
    }

    void SimEvent(PlayerbotAI* leaderAI, std::string const& who, SimAction action, char const* phase, uint32 ms,
                  uint32 seed)
    {
        DungeonLead::RecordEvent(leaderAI, "sim_player",
                                 who + " " + DungeonLeadKernel::ToString(action) + " " + phase,
                                 DungeonLeadKernel::JsonLine()
                                     .Str("who", who)
                                     .Str("action", DungeonLeadKernel::ToString(action))
                                     .Str("phase", phase)
                                     .Num("ms", ms)
                                     .Num("seed", seed)
                                     .Done());
    }

    // The simulated player's own movement: its follow strategy, or standing (its AI, not the module).
    void SetStanding(Player* sim, bool stand)
    {
        if (PlayerbotAI* ai = GET_PLAYERBOT_AI(sim))
            ai->ChangeStrategy(stand ? "-follow,+stay" : "-stay,+follow", BOT_STATE_NON_COMBAT);
    }
}

void DungeonLead::MarkSimulatedPlayer(Player* player, uint32_t seed)
{
    if (!player)
        return;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_marked[player->GetGUID().GetRawValue()] = seed;
    }
    // Its release waits for its script, as a player decides when to release (see SimPlayerHoldsRelease).
    if (PlayerbotAI* ai = GET_PLAYERBOT_AI(player))
        ai->ChangeStrategy("+dungeon lead run back", BOT_STATE_DEAD);
}

void DungeonLead::UnmarkSimulatedPlayer(uint64_t guidRaw)
{
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_marked.erase(guidRaw);
    }
    g_state.erase(guidRaw);
}

bool DungeonLead::IsSimulatedPlayer(Player* player)
{
    if (!player)
        return false;
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_marked.count(player->GetGUID().GetRawValue()) != 0;
}

bool DungeonLead::SimPlayerHoldsRelease(Player* player)
{
    return IsSimulatedPlayer(player);
}

void DungeonLead::SimPlayerTeleported(Player* player, uint32_t mapId, float x, float y, float z)
{
    if (!IsSimulatedPlayer(player))
        return;
    Group* group = player->GetGroup();
    Player* leader = group ? ObjectAccessor::FindPlayer(group->GetLeaderGUID()) : nullptr;
    PlayerbotAI* leaderAI = leader ? GET_PLAYERBOT_AI(leader) : nullptr;
    if (!leaderAI || !DungeonLead::IsOn(leaderAI))
        return;  // outside a session: entering or leaving the dungeon
    LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] simulated player {} teleported during a session: map {} -> {}",
              player->GetName(), player->GetMapId(), mapId);
    DungeonLead::RecordEvent(leaderAI, "sim_player", player->GetName() + " teleported",
                             DungeonLeadKernel::JsonLine()
                                 .Str("who", player->GetName())
                                 .Str("action", "teleported")
                                 .Str("phase", "observed")
                                 .Num("from_map", player->GetMapId())
                                 .Num("to_map", mapId)
                                 .Num("to_x", x)
                                 .Num("to_y", y)
                                 .Num("to_z", z)
                                 .Done());
}

void DungeonLead::SimPlayerTick(PlayerbotAI* leaderAI, DungeonLeadState& st)
{
    if (!leaderAI || st.masterGuid.IsEmpty())
        return;
    uint64 const key = st.masterGuid.GetRawValue();
    uint32 seed = 0;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        auto it = g_marked.find(key);
        if (it == g_marked.end())
            return;
        seed = it->second;
    }
    SimState& s = StateFor(key, seed);
    uint32 const now = getMSTime();
    Player* leader = leaderAI->GetBot();
    Player* sim = ObjectAccessor::FindPlayer(st.masterGuid);
    std::string const name = sim ? sim->GetName() : std::to_string(st.masterGuid.GetCounter());

    if (s.left)
        return;
    if (s.loggedOut)
    {
        if (getMSTimeDiff(s.actionSince, now) < s.actionMs || sim)
            return;
        s.loggedOut = false;
        s.action = SimAction::Follow;
        s.remark = true;
        RandomPlayerbotMgr::instance().AddPlayerBot(st.masterGuid, 0);
        SimEvent(leaderAI, name, SimAction::Logout, "back", getMSTimeDiff(s.actionSince, now), s.seed);
        return;
    }
    if (!sim || !sim->IsInWorld())
        return;
    if (s.remark)
    {
        if (PlayerbotAI* ai = GET_PLAYERBOT_AI(sim))
        {
            ai->ChangeStrategy("+dungeon lead run back", BOT_STATE_DEAD);
            ai->SetMaster(leader);
            s.remark = false;
        }
    }

    // dead: it waits, then accepts a resurrection (a party healer may bring it back earlier)
    if (!sim->IsAlive())
    {
        if (!s.deadSince)
        {
            s.deadSince = now;
            s.rezAfterMs = DungeonLeadKernel::SimActionMs(SimAction::Die, s.rng() % 1000);
        }
        if (getMSTimeDiff(s.deadSince, now) >= s.rezAfterMs)
        {
            sim->ResurrectPlayer(0.5f);
            sim->SpawnCorpseBones();
            SimEvent(leaderAI, name, SimAction::Die, "back", getMSTimeDiff(s.deadSince, now), s.seed);
            s.deadSince = 0;
        }
        return;
    }
    if (s.deadSince)
    {
        SimEvent(leaderAI, name, SimAction::Die, "resurrected", getMSTimeDiff(s.deadSince, now), s.seed);
        s.deadSince = 0;
    }

    // an action running out
    if (s.action != SimAction::Follow && getMSTimeDiff(s.actionSince, now) >= s.actionMs)
    {
        if (s.action == SimAction::Away)
            sim->RemovePlayerFlag(PLAYER_FLAGS_AFK);
        SetStanding(sim, false);
        SimEvent(leaderAI, name, s.action, "end", s.actionMs, s.seed);
        s.action = SimAction::Follow;
    }
    if (s.action != SimAction::Follow || (s.lastDecideTs && getMSTimeDiff(s.lastDecideTs, now) < DungeonLeadKernel::kSimDecideMs))
        return;
    if (!s.lastDecideTs)
    {
        s.lastDecideTs = now;  // the first decision after a minute of playing along
        return;
    }
    s.lastDecideTs = now;

    bool partyInCombat = false;
    if (Group* group = leader->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* p = ref->GetSource())
                if (p->IsInCombat())
                    partyInCombat = true;
    SimAction const next = DungeonLeadKernel::ChooseSimAction(s.rng() % 1000, partyInCombat);
    if (next == SimAction::Follow)
        return;
    s.action = next;
    s.actionSince = now;
    s.actionMs = DungeonLeadKernel::SimActionMs(next, s.rng() % 1000);
    SimEvent(leaderAI, name, next, "start", s.actionMs, s.seed);
    switch (next)
    {
        case SimAction::Lag:
        case SimAction::Still:
            SetStanding(sim, true);
            break;
        case SimAction::Away:
            sim->SetPlayerFlag(PLAYER_FLAGS_AFK);
            SetStanding(sim, true);
            break;
        case SimAction::Die:
            sim->KillPlayer();
            s.action = SimAction::Follow;  // the dead branch above brings it back
            break;
        case SimAction::Logout:
            s.loggedOut = true;
            RandomPlayerbotMgr::instance().LogoutPlayerBot(st.masterGuid);
            break;
        case SimAction::Leave:
            s.left = true;
            if (Group* group = sim->GetGroup())
                group->RemoveMember(sim->GetGUID());
            break;
        default:
            break;
    }
}
