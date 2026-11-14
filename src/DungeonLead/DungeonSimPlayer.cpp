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

#include <atomic>
#include <mutex>
#include <random>
#include <unordered_map>

using DungeonLeadKernel::SimAction;

namespace
{
    constexpr uint32 kGlobalTickMs = 1000;
    // A logout that has not happened this long after its planned return did not happen at all.
    constexpr uint32 kLogoutGraceMs = 30 * IN_MILLISECONDS;

    struct SimState
    {
        uint32 seed = 0;
        std::mt19937 rng;
        ObjectGuid self;
        ObjectGuid leader;  // the leader of the session it last played in
        SimAction action = SimAction::Follow;
        uint32 actionSince = 0;
        uint32 actionMs = 0;
        uint32 lastDecideTs = 0;
        bool loggedOut = false;
        bool remark = false;  // logged back in: its own strategies and the masters to set again
        bool left = false;
        uint32 deadSince = 0;
        uint32 rezAfterMs = 0;
    };

    // Which characters are simulated players: read from every thread (the teleport hook, bot AI).
    std::mutex g_mtx;
    std::unordered_map<uint64, uint32> g_marked;  // guid -> seed
    std::atomic<size_t> g_markedCount{0};          // the fast path while none is marked
    // The script state: world thread only (GuardActiveSessions, SimPlayersTick, the test pool).
    std::unordered_map<uint64, SimState> g_state;

    SimState& StateFor(ObjectGuid guid, uint32 seed)
    {
        auto it = g_state.find(guid.GetRawValue());
        if (it == g_state.end())
        {
            SimState s;
            s.seed = seed;
            s.rng.seed(seed);
            s.self = guid;
            it = g_state.emplace(guid.GetRawValue(), s).first;
        }
        return it->second;
    }

    // The leader whose session it plays in, while that session runs.
    PlayerbotAI* SessionLeaderAI(SimState const& s)
    {
        Player* leader = s.leader ? ObjectAccessor::FindPlayer(s.leader) : nullptr;
        PlayerbotAI* ai = leader ? GET_PLAYERBOT_AI(leader) : nullptr;
        return ai && DungeonLead::IsOn(ai) ? ai : nullptr;
    }

    // Recorded in the session it plays in; outside a session only logged.
    void SimEvent(PlayerbotAI* leaderAI, std::string const& who, SimAction action, char const* phase, uint32 ms,
                  uint32 seed)
    {
        if (!leaderAI)
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] simulated player {} {} {} ({} ms, no session)", who,
                     DungeonLeadKernel::ToString(action), phase, ms);
            return;
        }
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

    // Back in the world after a logout: its release waits for its script again, it follows the
    // leader again, and the leader's session has its player back.
    void Remark(SimState& s, Player* sim)
    {
        PlayerbotAI* simAI = GET_PLAYERBOT_AI(sim);
        if (!simAI)
            return;
        simAI->ChangeStrategy("+dungeon lead run back", BOT_STATE_DEAD);
        Player* leader = s.leader ? ObjectAccessor::FindPlayer(s.leader) : nullptr;
        Group* group = sim->GetGroup();
        if (leader && group && group->IsMember(leader->GetGUID()))
        {
            simAI->SetMaster(leader);
            if (PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader))
                if (DungeonLead::IsOn(leaderAI) &&
                    sDungeonRouteMgr.State(leader->GetGUID()).masterGuid == sim->GetGUID())
                    leaderAI->SetMaster(sim);
        }
        s.remark = false;
    }

    // What runs on time whether or not a session still runs: logging back in, being resurrected,
    // an action running out.
    void TimedStep(SimState& s, uint32 now)
    {
        PlayerbotAI* leaderAI = SessionLeaderAI(s);
        Player* sim = ObjectAccessor::FindPlayer(s.self);
        std::string const name = sim ? sim->GetName() : std::to_string(s.self.GetCounter());

        if (s.loggedOut)
        {
            uint32 const away = getMSTimeDiff(s.actionSince, now);
            if (sim)
            {
                if (away < s.actionMs + kLogoutGraceMs)
                    return;  // the logout is still on its way
                s.loggedOut = false;  // it never went
                s.action = SimAction::Follow;
                s.remark = true;
                SimEvent(leaderAI, name, SimAction::Logout, "failed", away, s.seed);
            }
            else
            {
                if (away < s.actionMs)
                    return;
                s.loggedOut = false;
                s.action = SimAction::Follow;
                s.remark = true;
                RandomPlayerbotMgr::instance().AddPlayerBot(s.self, 0);
                SimEvent(leaderAI, name, SimAction::Logout, "back", away, s.seed);
                return;
            }
        }
        if (!sim || !sim->IsInWorld())
            return;
        // The dungeon finder's accept resets a bot's AI: its release keeps waiting for the script.
        if (PlayerbotAI* ai = GET_PLAYERBOT_AI(sim))
            if (!ai->HasStrategy("dungeon lead run back", BOT_STATE_DEAD))
                ai->ChangeStrategy("+dungeon lead run back", BOT_STATE_DEAD);
        if (s.remark)
            Remark(s, sim);

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
    }
}

void DungeonLead::MarkSimulatedPlayer(Player* player, uint32_t seed)
{
    if (!player)
        return;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_marked[player->GetGUID().GetRawValue()] = seed;
        g_markedCount = g_marked.size();
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
        g_markedCount = g_marked.size();
    }
    g_state.erase(guidRaw);
}

bool DungeonLead::IsSimulatedPlayer(Player* player)
{
    if (!player || !g_markedCount.load(std::memory_order_relaxed))
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

void DungeonLead::SimPlayersTick()
{
    if (g_state.empty())
        return;
    static uint32 lastTs = 0;
    uint32 const now = getMSTime();
    if (lastTs && getMSTimeDiff(lastTs, now) < kGlobalTickMs)
        return;
    lastTs = now;
    for (auto& kv : g_state)
        if (!kv.second.left)
            TimedStep(kv.second, now);
}

void DungeonLead::SimPlayerTick(PlayerbotAI* leaderAI, DungeonLeadState& st)
{
    if (!leaderAI || st.masterGuid.IsEmpty())
        return;
    uint32 seed = 0;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        auto it = g_marked.find(st.masterGuid.GetRawValue());
        if (it == g_marked.end())
            return;
        seed = it->second;
    }
    SimState& s = StateFor(st.masterGuid, seed);
    Player* leader = leaderAI->GetBot();
    s.leader = leader->GetGUID();
    if (s.left || s.loggedOut || s.remark)
        return;
    Player* sim = ObjectAccessor::FindPlayer(st.masterGuid);
    if (!sim || !sim->IsInWorld() || !sim->IsAlive() || s.deadSince)
        return;  // SimPlayersTick() brings it back

    uint32 const now = getMSTime();
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
    std::string const name = sim->GetName();
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
            s.action = SimAction::Follow;  // SimPlayersTick() brings it back
            break;
        case SimAction::Logout:
            // The leader's master would point at a deleted player: the session waits for its
            // player by st.masterGuid meanwhile, and gets it back when it logs in (Remark).
            s.loggedOut = true;
            leaderAI->SetMaster(nullptr);
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
