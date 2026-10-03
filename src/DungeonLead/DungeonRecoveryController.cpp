/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonRecoveryController.h"

#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonPartyState.h"
#include "DungeonRouteMgr.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

using DungeonLeadKernel::LeadState;
using DungeonLeadKernel::RecoveryReason;
using DungeonLeadKernel::RecoveryStep;

namespace
{
    // A straggler usually just catches up: only walk back to it once it has stayed behind this long.
    constexpr uint32 kRegroupGraceMs = 8000;

    bool IsBotMember(Player* p)
    {
        return p && !IsRealPlayer(p) && !IsSelfBot(p);
    }

    // What needs recovering right now, and who it is about.
    RecoveryReason Observe(PlayerbotAI* botAI, DungeonPartySnapshot const& party, Player*& who)
    {
        who = nullptr;
        Player* bot = botAI->GetBot();
        Group* group = bot->GetGroup();
        if (group && group->GetLeaderGUID() != bot->GetGUID())
            return RecoveryReason::LeadershipLost;

        DungeonLeadKernel::Readiness const r = DungeonPartyState::Readiness(party, DungeonLeadKernel::ReadyPurpose::Walk);
        bool healerAlive = false;
        if (r.status == DungeonLeadKernel::ReadyStatus::HealerUnavailable)
            for (size_t i = 0; i < party.facts.members.size(); ++i)
                if (party.facts.members[i].isHealerBySpec && party.facts.members[i].alive)
                {
                    healerAlive = true;
                    who = party.Member(int(i));
                }
        if (!who)
            who = party.Member(r.offender);
        return DungeonLeadKernel::RecoveryFor(r.status, healerAlive);
    }
}

bool DungeonRecoveryController::Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());

    // Only between fights, with the leader alive: in combat the fight comes first, and the
    // leader's own death is WipeRecovery.
    bool const eligible = (st.state == LeadState::WaitingReady || st.state == LeadState::PostCombat ||
                           st.state == LeadState::Recovery) &&
                          bot->IsAlive() && !st.paused;
    Player* who = nullptr;
    RecoveryReason const observed = eligible ? Observe(botAI, party, who) : RecoveryReason::None;

    if (observed == RecoveryReason::None)
    {
        if (st.recoveryReason != RecoveryReason::None && eligible)
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} recovery {} complete after {} ms", bot->GetName(),
                     DungeonLeadKernel::ToString(st.recoveryReason), GetMSTimeDiffToNow(st.recoverySinceTs));
            DungeonLead::RecordEvent(botAI, "recovery_complete",
                                     std::string(DungeonLeadKernel::ToString(st.recoveryReason)) +
                                         " ms=" + std::to_string(GetMSTimeDiffToNow(st.recoverySinceTs)));
        }
        if (eligible || st.state == LeadState::Travelling)
        {
            st.recoveryReason = RecoveryReason::None;
            st.recoveryStep = RecoveryStep::None;
            st.recoverySinceTs = 0;
        }
        return true;  // in combat etc. an open recovery is kept, and its clock keeps running
    }

    if (st.recoveryReason == RecoveryReason::None)
    {
        st.recoverySinceTs = getMSTime();
        st.recoveryStep = RecoveryStep::None;
    }
    if (observed != st.recoveryReason)
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} recovery start: {} ({})", bot->GetName(),
                 DungeonLeadKernel::ToString(observed), who ? who->GetName() : "-");
        DungeonLead::RecordEvent(botAI, "recovery_start",
                                 std::string(DungeonLeadKernel::ToString(observed)) + " member=" +
                                     (who ? who->GetName() : "-"));
        st.recoveryReason = observed;
    }

    DungeonLeadKernel::RecoveryPolicy policy;
    policy.actMs = sDungeonLeadConfig.dungeonLeadRecoveryTimeoutSeconds * IN_MILLISECONDS;
    policy.escalateMs = sDungeonLeadConfig.dungeonLeadRecoveryEscalationSeconds * IN_MILLISECONDS;
    RecoveryStep step = DungeonLeadKernel::DecideRecovery(observed, GetMSTimeDiffToNow(st.recoverySinceTs), policy);
    if (step == RecoveryStep::Abort && st.recoveryStep != RecoveryStep::Escalate)
        step = RecoveryStep::Escalate;  // the clock ran on through a fight: still try the escalation once
    bool const newStep = step != st.recoveryStep;
    st.recoveryStep = step;

    switch (step)
    {
        case RecoveryStep::Act:
            // Regroup: walk back toward the straggler until it is close enough to follow again.
            if (observed == RecoveryReason::PartyFragmented && who && who->GetMap() == bot->GetMap() &&
                GetMSTimeDiffToNow(st.recoverySinceTs) >= kRegroupGraceMs &&
                bot->GetDistance(who) > sDungeonLeadConfig.dungeonLeadPartySoftRange && !bot->isMoving())
                bot->GetMotionMaster()->MovePoint(0, who->GetPositionX(), who->GetPositionY(), who->GetPositionZ());
            break;
        case RecoveryStep::Escalate:
        {
            if (!newStep)
                break;
            bool const movable = who && IsBotMember(who) && who->IsAlive() &&
                                 (observed == RecoveryReason::PartyFragmented || observed == RecoveryReason::MemberLost);
            std::string detail = std::string(DungeonLeadKernel::ToString(observed)) + " member=" +
                                 (who ? who->GetName() : "-");
            if (movable)
            {
                detail += " action=brought_to_leader";
                who->TeleportTo(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                bot->GetOrientation());
            }
            else
                detail += " action=none";
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} recovery escalate: {}", bot->GetName(), detail);
            DungeonLead::RecordEvent(botAI, "recovery_escalate", detail);
            break;
        }
        case RecoveryStep::Abort:
        {
            std::string const detail = std::string(DungeonLeadKernel::ToString(observed)) + " member=" +
                                       (who ? who->GetName() : "-");
            LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] {} recovery failed ({}) - stopping the run (run={})",
                      bot->GetName(), detail, st.runId);
            st.outcome = DungeonRunOutcome::Failed;
            st.failureDomain = DungeonFailureDomain::Recovery;
            st.failureReason = observed == RecoveryReason::LeadershipLost ? DungeonFailureReason::InternalInvariant
                                                                           : DungeonFailureReason::PlayerMissing;
            DungeonLead::RecordEvent(botAI, "recovery_failed", detail);
            DungeonLead::RecordRunSummary(botAI, "recovery_failed");
            botAI->TellMasterNoFacing("Dungeon lead: can't recover (" + detail + "), stopping");
            DungeonLead::Stop(botAI, /*giveLeaderBack*/ observed != RecoveryReason::LeadershipLost);
            return false;
        }
        default:
            break;
    }
    return true;
}
