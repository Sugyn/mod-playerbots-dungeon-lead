/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonInteractionController.h"

#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonRouteMgr.h"
#include "GameObject.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

using DungeonLeadKernel::InteractionState;
using DungeonLeadKernel::InteractionType;

namespace
{
    // A closed door this close to the leader, on the way to where it was going, is the blocker.
    constexpr float kDoorSearchRange = 15.0f;
    // ... and "on the way" = this close to the straight line from the leader to its destination, so
    // a side-room door next to the corridor isn't taken for the blocker.
    constexpr float kDoorOffLine = 10.0f;

    void SetInteraction(PlayerbotAI* botAI, DungeonLeadState& st, InteractionState next, std::string const& detail)
    {
        if (st.interactionState == next)
            return;
        std::string const line = std::string(DungeonLeadKernel::ToString(st.interactionType)) + " " +
                                 DungeonLeadKernel::ToString(st.interactionState) + "->" +
                                 DungeonLeadKernel::ToString(next) + (detail.empty() ? "" : " " + detail);
        st.interactionState = next;
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} interaction {}", botAI->GetBot()->GetName(), line);
        DungeonLead::RecordEvent(botAI, "interaction_state", line);
    }
}

bool DungeonInteractionController::StartIfBlockedByDoor(PlayerbotAI* botAI, DungeonLeadState& st, float x, float y,
                                                        float z)
{
    if (DungeonLeadKernel::InteractionActive(st.interactionState))
        return true;  // already handling one
    Player* bot = botAI->GetBot();
    float const botToDest = bot->GetExactDist(x, y, z);

    GameObject* door = nullptr;
    float best = kDoorSearchRange;
    for (ObjectGuid const& guid :
         botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects no los")->Get())
    {
        GameObject* go = botAI->GetGameObject(guid);
        if (!go || go->GetGoType() != GAMEOBJECT_TYPE_DOOR || go->GetGoState() != GO_STATE_READY)
            continue;
        float const d = bot->GetDistance(go);
        if (d > best || go->GetExactDist(x, y, z) >= botToDest ||
            DungeonLeadKernel::DistanceToSegment2D(go->GetPositionX(), go->GetPositionY(), bot->GetPositionX(),
                                                   bot->GetPositionY(), x, y) > kDoorOffLine)
            continue;  // too far, or not on the way
        best = d;
        door = go;
    }
    if (!door)
        return false;
    StartForDoor(botAI, st, door, "blocked");
    return true;
}

void DungeonInteractionController::StartForDoor(PlayerbotAI* botAI, DungeonLeadState& st, GameObject* door,
                                                char const* how)
{
    if (DungeonLeadKernel::InteractionActive(st.interactionState))
        return;
    st.interactionType = InteractionType::Door;
    st.interactionState = InteractionState::None;
    st.interactionTarget = door->GetGUID();
    st.interactionActiveMs = 0;
    st.interactionLastTs = getMSTime();
    SetInteraction(botAI, st, InteractionState::Resolving,
                   "target=" + door->GetName() + " entry=" + std::to_string(door->GetEntry()) + " found=" + how +
                       " dist=" + std::to_string(int(botAI->GetBot()->GetDistance(door))));
    botAI->TellMasterNoFacing("Dungeon lead: " + door->GetName() + " is closed - waiting for it to open");
}

bool DungeonInteractionController::Update(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (!DungeonLeadKernel::InteractionActive(st.interactionState))
        return true;

    // Count only the time the leader is actually holding at it (route walk active = Travelling);
    // a fight or a recovery in between doesn't use up the wait.
    uint32 const now = getMSTime();
    st.interactionActiveMs = DungeonLeadKernel::AdvanceInteractionClock(
        st.interactionActiveMs, getMSTimeDiff(st.interactionLastTs, now), st.state == DungeonLeadKernel::LeadState::Travelling);
    st.interactionLastTs = now;

    GameObject* go = botAI->GetGameObject(st.interactionTarget);
    DungeonLeadKernel::InteractionFacts f;
    f.current = st.interactionState;
    f.msInInteraction = st.interactionActiveMs;
    f.targetFound = go != nullptr;
    f.satisfied = go && go->GetGoState() != GO_STATE_READY;  // the world says it's open
    f.canAct = false;  // dungeon doors are opened by their events/keys, not by us
    InteractionState const next =
        DungeonLeadKernel::DecideInteraction(f, sDungeonLeadConfig.dungeonLeadDoorWaitSeconds * IN_MILLISECONDS);
    if (next == st.interactionState)
        return true;
    SetInteraction(botAI, st, next, "ms=" + std::to_string(f.msInInteraction));

    if (next == InteractionState::Complete)
    {
        // walk on - the stuck measurement starts over past the door
        st.bestDist = 0.f;
        st.stuckTs = 0;
        st.stuckAttempts = 0;
        botAI->TellMasterNoFacing("Dungeon lead: the way is open, moving on");
        return true;
    }
    if (next != InteractionState::Failed)
        return true;

    DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
    if (!route || st.stepIndex >= route->steps.size())
        return true;
    return !DungeonLead::FailObjective(botAI, st, route->steps[st.stepIndex], DungeonFailureDomain::Navigation,
                                       DungeonFailureReason::PathFailed, f.targetFound ? "door_closed" : "door_gone");
}
