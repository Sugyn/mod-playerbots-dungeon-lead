/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonPullController.h"

#include "Creature.h"
#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonPack.h"
#include "DungeonPartyState.h"
#include "DungeonRouteMgr.h"
#include "Event.h"
#include "Group.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "Timer.h"

#include <algorithm>

using DungeonLeadKernel::PullState;

namespace
{
    void SetPullState(PlayerbotAI* botAI, DungeonLeadState& st, DungeonPack const& pack, PullState next,
                      std::string const& detail = "")
    {
        if (st.pullState == next)
            return;
        std::string line = "#" + std::to_string(pack.id) + " " + pack.name + " " +
                           DungeonLeadKernel::ToString(st.pullState) + "->" + DungeonLeadKernel::ToString(next) +
                           " attempt=" + std::to_string(st.pullAttempts);
        if (!detail.empty())
            line += " " + detail;
        st.pullState = next;
        st.pullStateTs = getMSTime();
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} pull {}", botAI->GetBot()->GetName(), line);
        DungeonLead::RecordEvent(botAI, "pull_state", line);
    }

    bool IsPackMember(DungeonPack const& pack, Unit const* u)
    {
        Creature const* c = u ? u->ToCreature() : nullptr;
        return c && c->IsAlive() &&
               std::find(pack.expectedEntries.begin(), pack.expectedEntries.end(), c->GetEntry()) !=
                   pack.expectedEntries.end() &&
               c->GetExactDist(pack.x, pack.y, pack.z) <= pack.probeRadius;
    }
}

void DungeonPullController::Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    Group* group = bot->GetGroup();

    // Only while the session is actively working the route.
    using DungeonLeadKernel::LeadState;
    bool const working = DungeonLeadKernel::IsActive(st.state) && st.state != LeadState::WipeRecovery &&
                         st.state != LeadState::Completing && !st.paused && bot->IsAlive() && group &&
                         DungeonLead::InFiveMan(bot);
    DungeonRoute const* route = working && st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
    DungeonPack const pack = route ? DungeonPacks::ForStep(*route, st.stepIndex) : DungeonPack();
    bool const pullType = pack.Exists() && (pack.type == DungeonRouteNodeType::Pull ||
                                            pack.type == DungeonRouteNodeType::Boss);
    if (!pullType)
    {
        if (st.pullState != PullState::None)
            SetPullState(botAI, st, pack, PullState::None);
        return;
    }

    DungeonPackSighting const sighting = DungeonPacks::Observe(bot, pack, st.instanceId);
    DungeonLeadKernel::PackState const prevPack = st.packId == pack.id ? st.packState : DungeonLeadKernel::PackState::Unknown;
    DungeonLead::SetPackState(botAI, st, pack, DungeonLeadKernel::DecidePackState(prevPack, sighting.observation));

    Creature* target = sighting.firstAlive;
    Unit* skull = botAI->GetUnit(group->GetTargetIcon(RtiTargetValue::skullIndex));

    DungeonLeadKernel::PullFacts f;
    f.current = st.pullState;
    f.msInState = st.pullStateTs ? GetMSTimeDiffToNow(st.pullStateTs) : 0;
    f.pullablePack = st.packState != DungeonLeadKernel::PackState::Cleared &&
                     st.packState != DungeonLeadKernel::PackState::Skipped;
    f.packAlive = sighting.observation.alive > 0;
    f.packEngaged = sighting.observation.engaged > 0;
    f.inPullRange = target && bot->GetDistance(target) <= sDungeonLeadConfig.dungeonLeadPullRange;
    DungeonLeadKernel::Readiness const ready = DungeonPartyState::Readiness(party, DungeonLeadKernel::ReadyPurpose::Pull);
    f.partyReady = ready.status == DungeonLeadKernel::ReadyStatus::Ready;
    f.targetMarked = IsPackMember(pack, skull);
    f.leaderInCombat = bot->IsInCombat();
    f.orderRefused = st.pullOrderRefused;
    f.attempts = st.pullAttempts;

    DungeonLeadKernel::PullPolicy policy;
    policy.initiateTimeoutMs = sDungeonLeadConfig.dungeonLeadPullInitiateTimeoutSeconds * IN_MILLISECONDS;
    policy.establishTimeoutMs = sDungeonLeadConfig.dungeonLeadPullEstablishTimeoutSeconds * IN_MILLISECONDS;
    policy.maxAttempts = uint8(sDungeonLeadConfig.dungeonLeadPullMaxAttempts);

    PullState const next = DungeonLeadKernel::DecidePull(f, policy);
    bool const changed = next != st.pullState;
    std::string waitReason;
    if (next == PullState::WaitingParty)
    {
        waitReason = std::string("reason=") + DungeonLeadKernel::ToString(ready.status);
        if (Player* who = party.Member(ready.offender))
            waitReason += " (" + who->GetName() + ")";
    }
    SetPullState(botAI, st, pack, next, waitReason);

    switch (next)
    {
        case PullState::Marking:
            // Put the skull on the pull target unless someone else's live mark holds it.
            if (target && !f.targetMarked && (!skull || !skull->IsAlive() || skull->GetGUID() == st.skullGuid))
            {
                group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), target->GetGUID());
                st.skullGuid = target->GetGUID();  // so Stop() only clears our own
                DungeonLead::RecordEvent(botAI, "mark_skull", target->GetName());
            }
            break;
        case PullState::Initiating:
            if (changed)
            {
                ++st.pullAttempts;
                bool const ordered = botAI->DoSpecificAction("attack rti target", Event(), /*silent*/ true);
                st.pullOrderRefused = !ordered;
                // What the attack order saw - upstream's Attack() refuses silently (line of sight,
                // friendly, dead, ...), so record the facts it checks next to whether it took.
                std::string detail = pack.name + " attempt=" + std::to_string(st.pullAttempts) +
                                     " order=" + (ordered ? "accepted" : "refused");
                if (target)
                    detail += " dist=" + std::to_string(int(bot->GetDistance(target))) +
                              " los=" + std::to_string(bot->IsWithinLOSInMap(target)) +
                              " attackable=" + std::to_string(bot->IsValidAttackTarget(target)) +
                              " skull=" + std::to_string(f.targetMarked);
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} pulling #{} {}", bot->GetName(), pack.id, detail);
                DungeonLead::RecordEvent(botAI, "pull_start", detail);
            }
            break;
        case PullState::Established:
            if (changed)
                DungeonLead::RecordEvent(botAI, "pull_established", pack.name);
            break;
        case PullState::Failed:
        {
            if (!changed)
                break;
            DungeonLead::RecordEvent(botAI, "pull_failed", pack.name + " attempt=" + std::to_string(st.pullAttempts));
            if (st.pullAttempts < policy.maxAttempts)
                break;  // DecidePull goes back to Approaching for another attempt

            // Out of attempts: skip the pack, recorded - never a silent route advance.
            DungeonRouteStep const& step = route->steps[st.stepIndex];
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} giving up on #{} {} after {} failed pull(s)",
                     bot->GetName(), pack.id, pack.name, st.pullAttempts);
            botAI->TellMasterNoFacing("Dungeon lead: can't pull " + pack.name + ", moving on");
            DungeonLead::SetPackState(botAI, st, pack, DungeonLeadKernel::PackState::Skipped);
            DungeonLead::RecordEvent(botAI, "pack_skipped", pack.name + " reason=pull_failed");
            DungeonLead::SkipStep(st, step, DungeonFailureDomain::PullPlanning,
                                  pack.bossPack ? DungeonFailureReason::BossEvade : DungeonFailureReason::ObjectiveTimeout);
            break;
        }
        default:
            break;
    }
}
