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
#include "DungeonTelemetryV2.h"
#include "DBCStores.h"
#include "Group.h"
#include "PathGenerator.h"
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
    // a lever this close to a door is taken to be that door's lever
    constexpr float kLeverRange = 8.0f;

    // A lever is ours to pull if the leader can walk to it without going through that door (DM: the
    // Iron Clad Door's lever is on the ship's side; SFK: a cell's lever is reached from the walkway
    // above by the ramp, not through the cell). A 2D side test got the two-level SFK case wrong.
    bool ReachableWithoutDoor(Player* bot, GameObject* lever, GameObject* door)
    {
        PathGenerator path(bot);
        path.CalculatePath(lever->GetPositionX(), lever->GetPositionY(), lever->GetPositionZ());
        if (path.GetPathType() & ~(PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_SHORT | PATHFIND_FARFROMPOLY_END))
            return false;
        G3D::Vector3 const& end = path.GetActualEndPosition();
        if (lever->GetExactDist(end.x, end.y, end.z) > 6.0f)
            return false;  // can't get next to it
        Movement::PointsArray const& pts = path.GetPath();
        for (size_t i = 1; i < pts.size(); ++i)
        {
            float t = 0.f;
            if (DungeonLeadKernel::SegmentCrossesDoor(door->GetPositionX(), door->GetPositionY(), door->GetOrientation(),
                                                      pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, 3.5f, &t))
            {
                float const z = pts[i - 1].z + t * (pts[i].z - pts[i - 1].z);
                if (z > door->GetPositionZ() - 2.0f && z < door->GetPositionZ() + 5.0f)
                    return false;
            }
        }
        return true;
    }

    // How a player would open this door: who acts, on what. A lock that opens by hand (no skill) ->
    // the leader on the door; a key lock -> the party member with the key, on the door; otherwise a
    // lever next to the door on the leader's side. Event doors (no lock, no lever) -> nobody.
    bool FindOpener(PlayerbotAI* botAI, GameObject* door, Player*& actor, GameObject*& target, std::string& how)
    {
        Player* bot = botAI->GetBot();
        actor = nullptr;
        target = nullptr;
        if (LockEntry const* lock = sLockStore.LookupEntry(door->GetGOInfo()->GetLockId()))
            for (uint8 i = 0; i < MAX_LOCK_CASE && !actor; ++i)
            {
                if (lock->Type[i] == LOCK_KEY_SKILL && lock->Skill[i] == 0 &&
                    (lock->Index[i] == LOCKTYPE_OPEN || lock->Index[i] == LOCKTYPE_QUICK_OPEN ||
                     lock->Index[i] == LOCKTYPE_OPEN_KNEELING))
                {
                    actor = bot;
                    how = "by_hand";
                }
                else if (lock->Type[i] == LOCK_KEY_ITEM && lock->Index[i])
                    if (Group* group = bot->GetGroup())
                        for (GroupReference* ref = group->GetFirstMember(); ref && !actor; ref = ref->next())
                            if (Player* m = ref->GetSource())
                                if (m->IsAlive() && m->GetMap() == bot->GetMap() && m->HasItemCount(lock->Index[i], 1))
                                {
                                    actor = m;
                                    how = "key=" + std::to_string(lock->Index[i]);
                                }
            }
        if (actor)
        {
            target = door;
            return true;
        }

        for (ObjectGuid const& guid :
             botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects no los")->Get())
        {
            GameObject* go = botAI->GetGameObject(guid);
            if (!go || go->GetGoType() != GAMEOBJECT_TYPE_BUTTON || go->GetDistance(door) > kLeverRange ||
                go->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE) || !ReachableWithoutDoor(bot, go, door))
                continue;
            actor = bot;
            target = go;
            how = "lever=" + std::to_string(go->GetEntry());
            return true;
        }
        return false;
    }

    void SetInteraction(PlayerbotAI* botAI, DungeonLeadState& st, InteractionState next, std::string const& detail)
    {
        if (st.interactionState == next)
            return;
        std::string const line = std::string(DungeonLeadKernel::ToString(st.interactionType)) + " " +
                                 DungeonLeadKernel::ToString(st.interactionState) + "->" +
                                 DungeonLeadKernel::ToString(next) + (detail.empty() ? "" : " " + detail);
        DungeonLeadKernel::JsonLine payload;
        payload.Str("type", DungeonLeadKernel::ToString(st.interactionType))
            .Str("from", DungeonLeadKernel::ToString(st.interactionState))
            .Str("to", DungeonLeadKernel::ToString(next));
        if (GameObject* go = st.interactionTarget.IsEmpty() ? nullptr : botAI->GetGameObject(st.interactionTarget))
            payload.Str("target", go->GetName())
                .Num("entry", uint32_t(go->GetEntry()))
                .Num("spawn", uint32_t(go->GetSpawnId()))
                .Num("x", go->GetPositionX())
                .Num("y", go->GetPositionY())
                .Num("z", go->GetPositionZ());
        st.interactionState = next;
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} interaction {}", botAI->GetBot()->GetName(), line);
        DungeonLead::RecordEvent(botAI, "interaction_state", line, payload.Done());
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
    st.interactionActed = false;
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
    // Open it the way a player could, once; event doors (no lock, no lever) are only waited for.
    Player* actor = nullptr;
    GameObject* opener = nullptr;
    std::string how;
    f.canAct = go && !st.interactionActed && FindOpener(botAI, go, actor, opener, how);
    InteractionState const next =
        DungeonLeadKernel::DecideInteraction(f, sDungeonLeadConfig.dungeonLeadDoorWaitSeconds * IN_MILLISECONDS);
    if (next == InteractionState::Interacting && actor && opener)
    {
        st.interactionActed = true;
        opener->Use(actor);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} opening {} - {} by {}", bot->GetName(), go->GetName(), how,
                 actor->GetName());
        DungeonLead::RecordEvent(botAI, "door_opened_by_us", go->GetName() + " " + how + " by=" + actor->GetName());
    }
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
