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
#include "DungeonTelemetryV2.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

#include <cmath>

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

    // In combat etc. (not eligible) an open recovery is kept as it is, and its clocks run on. Once
    // the session is walking on again, whatever it was is resolved.
    if (!eligible && observed == RecoveryReason::None && st.state != LeadState::Travelling)
        return true;

    RecoveryReason const previous = st.recovery.reason;
    uint32 const now = getMSTime();
    if (observed == RecoveryReason::None)
    {
        if (previous != RecoveryReason::None)
        {
            uint32 const ms = getMSTimeDiff(st.recovery.reasonSince, now);
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} recovery {} complete after {} ms", bot->GetName(),
                     DungeonLeadKernel::ToString(previous), ms);
            DungeonLead::RecordEvent(botAI, "recovery_complete",
                                     std::string(DungeonLeadKernel::ToString(previous)) + " ms=" + std::to_string(ms),
                                     DungeonLeadKernel::JsonLine()
                                         .Str("reason", DungeonLeadKernel::ToString(previous))
                                         .Num("ms", uint32_t(ms))
                                         .Done());
        }
        DungeonLeadKernel::ObserveRecovery(st.recovery, observed, now);
        return true;
    }

    // A new reason gets its own fresh window (it must not inherit an almost-expired timeout); the
    // episode as a whole stays capped - see DungeonLeadKernel::ObserveRecovery / DecideRecovery.
    if (DungeonLeadKernel::ObserveRecovery(st.recovery, observed, now, who ? who->GetGUID().GetRawValue() : 0))
    {
        std::string detail = std::string(DungeonLeadKernel::ToString(observed)) + " member=" + (who ? who->GetName() : "-");
        if (previous != RecoveryReason::None)
            detail += " after=" + std::string(DungeonLeadKernel::ToString(previous));
        if (st.recovery.relapse)
            detail += " relapse_after_ms=" + std::to_string(getMSTimeDiff(st.recovery.reasonSince, now));
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} recovery start: {}", bot->GetName(), detail);
        DungeonLead::RecordEvent(botAI, "recovery_start", detail,
                                 DungeonLeadKernel::JsonLine()
                                     .Str("reason", DungeonLeadKernel::ToString(observed))
                                     .Str("member", who ? who->GetName() : "")
                                     .Str("previous", previous != RecoveryReason::None ? DungeonLeadKernel::ToString(previous) : "")
                                     .Bool("relapse", st.recovery.relapse)
                                     .Done());
    }

    DungeonLeadKernel::RecoveryPolicy policy;
    policy.actMs = sDungeonLeadConfig.dungeonLeadRecoveryTimeoutSeconds * IN_MILLISECONDS;
    policy.escalateMs = sDungeonLeadConfig.dungeonLeadRecoveryEscalationSeconds * IN_MILLISECONDS;
    uint32 const msInReason = getMSTimeDiff(st.recovery.reasonSince, now);
    RecoveryStep step = DungeonLeadKernel::DecideRecovery(observed, msInReason,
                                                         getMSTimeDiff(st.recovery.episodeSince, now), policy);
    if (step == RecoveryStep::Abort && st.recovery.step != RecoveryStep::Escalate)
        step = RecoveryStep::Escalate;  // the clock ran on through a fight: still try the escalation once
    bool const newStep = step != st.recovery.step;
    st.recovery.step = step;

    switch (step)
    {
        case RecoveryStep::Act:
            // Regroup: walk back toward the straggler until it is close enough to follow again.
            if (observed == RecoveryReason::PartyFragmented && who && who->GetMap() == bot->GetMap() &&
                msInReason >= kRegroupGraceMs &&
                bot->GetDistance(who) > sDungeonLeadConfig.dungeonLeadPartySoftRange && !bot->isMoving())
            {
                // Only along a real path: a plain MovePoint to a straggler the navmesh can't reach
                // moved the tank straight through rock (H7: Deadmines, SM Library - left with no
                // ground under it). No path -> stay; the escalation brings the straggler instead.
                // Follow exactly the computed points: a MovePoint would path again on its own and
                // fall back to a straight line when that fails (Deadmines: the leader, standing off
                // the mesh after a wipe, walked into the rock). Not from off the mesh at all
                // (FARFROMPOLY_START is not an accepted type).
                PathGenerator path(bot);
                path.CalculatePath(who->GetPositionX(), who->GetPositionY(), who->GetPositionZ());
                if (!(path.GetPathType() & ~(PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_SHORT | PATHFIND_FARFROMPOLY_END)) &&
                    path.GetPath().size() > 1)
                {
                    G3D::Vector3 const& end = path.GetActualEndPosition();
                    if (bot->GetExactDist(end.x, end.y, end.z) > 5.0f)
                    {
                        Movement::PointsArray points = path.GetPath();
                        bot->GetMotionMaster()->MoveSplinePath(&points);
                    }
                }
            }
            break;
        case RecoveryStep::Escalate:
        {
            if (!newStep)
                break;
            // Never bring anyone to a leader that is itself off the ground (inside the rock): wait
            // for it to be put back on its feet (MoveRouteTo's last-good-spot move) and escalate
            // then - Deadmines: four members were brought into the rock with it.
            float const ground = bot->GetMapHeight(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() + 2.0f);
            if (ground <= INVALID_HEIGHT + 1.0f || std::fabs(bot->GetPositionZ() - ground) > 5.0f)
            {
                st.recovery.step = RecoveryStep::Act;  // try the escalation again next tick
                DungeonLead::RecordEvent(botAI, "recovery_escalate_deferred",
                                         std::string(DungeonLeadKernel::ToString(observed)) + " leader_off_ground z=" +
                                             std::to_string(int(bot->GetPositionZ())) + " ground=" + std::to_string(int(ground)));
                break;
            }
            bool const movable = who && IsBotMember(who) && who->IsAlive() &&
                                 (observed == RecoveryReason::PartyFragmented || observed == RecoveryReason::MemberLost);
            std::string detail = std::string(DungeonLeadKernel::ToString(observed)) + " member=" +
                                 (who ? who->GetName() : "-");
            if (movable)
            {
                detail += " action=brought_to_leader";
                who->TeleportTo(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                bot->GetOrientation());
                // Every other bot straggler too: bringing only the farthest left the next one behind
                // to run the same recovery out (RFC/DM, H7).
                if (observed == RecoveryReason::PartyFragmented)
                    if (Group* group = bot->GetGroup())
                        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                        {
                            Player* m = ref->GetSource();
                            if (m && m != who && m != bot && IsBotMember(m) && m->IsAlive() &&
                                m->GetMap() == bot->GetMap() &&
                                bot->GetDistance(m) > sDungeonLeadConfig.dungeonLeadPartyHardRange)
                            {
                                detail += " +" + m->GetName();
                                m->TeleportTo(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(),
                                              bot->GetPositionZ(), bot->GetOrientation());
                            }
                        }
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
