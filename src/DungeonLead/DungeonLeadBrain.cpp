/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadBrain.h"

#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonPartyState.h"
#include "DungeonRouteMgr.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "Timer.h"

#include <algorithm>

using DungeonLeadKernel::LeadState;
using DungeonLeadKernel::TransitionReason;

std::string DungeonLeadObjective::Describe() const
{
    if (!valid)
        return "none";
    if (type == DungeonRouteNodeType::End)
        return "end";
    return std::string(ToString(type)) + ":" + name + "@" + std::to_string(stepIndex);
}

DungeonLeadObjective DungeonLeadBrain::CurrentObjective(DungeonLeadState const& st)
{
    DungeonLeadObjective o;
    DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
    if (!route)
        return o;
    o.valid = true;

    if (st.state == LeadState::WipeRecovery)
    {
        // stranded members are brought back to the entrance (RecoverStrandedMembers)
        DungeonRouteMgr::Entrance e;
        o.type = DungeonRouteNodeType::Recovery;
        o.name = "entrance";
        if (sDungeonRouteMgr.GetEntrance(*route, e))
        {
            o.x = e.x;
            o.y = e.y;
            o.z = e.z;
        }
        return o;
    }

    DungeonRouteStep const* step = nullptr;
    if (st.state != LeadState::Completing && st.stepIndex < route->steps.size())
    {
        step = &route->steps[st.stepIndex];
        o.type = step->NodeType();
    }
    if (!step)
    {
        o.type = DungeonRouteNodeType::End;
        return o;
    }
    o.stepIndex = int32_t(step - route->steps.data());
    o.name = step->boss;
    o.x = step->x;
    o.y = step->y;
    o.z = step->z;
    return o;
}

void DungeonLeadBrain::TransitionTo(PlayerbotAI* botAI, DungeonLeadState& st, LeadState next,
                                    TransitionReason reason, std::string const& detail)
{
    // A fresh entry has no state yet; report it as such instead of as the default value.
    bool const fresh = st.stateSinceTs == 0;
    if (!fresh && st.state == next)
        return;

    char const* const from = fresh ? "none" : DungeonLeadKernel::ToString(st.state);
    uint32 const inStateMs = fresh ? 0 : GetMSTimeDiffToNow(st.stateSinceTs);
    st.state = next;
    st.stateSinceTs = getMSTime();
    if (!st.stateSinceTs)
        st.stateSinceTs = 1;  // 0 means "no state yet" above

    std::string line = std::string(from) + "->" + DungeonLeadKernel::ToString(next) + " reason=" +
                       DungeonLeadKernel::ToString(reason) + " after_ms=" + std::to_string(inStateMs) +
                       " objective=" + CurrentObjective(st).Describe();
    if (!detail.empty())
        line += " " + detail;
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} state {} (run={})", botAI->GetBot()->GetName(), line,
             st.runId);
    DungeonLead::RecordEvent(botAI, "state_transition", line);
}

bool DungeonLeadBrain::Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (!DungeonLeadKernel::IsActive(st.state))
        return true;

    DungeonLeadKernel::ActiveFacts f;
    f.current = st.state;
    f.leaderAlive = bot->IsAlive();
    f.paused = st.paused;
    f.routeComplete = st.doneTold;
    f.anyInCombat = DungeonLeadKernel::AnyInCombat(party.facts);
    f.walkReady = DungeonPartyState::Readiness(party, DungeonLeadKernel::ReadyPurpose::Walk).status ==
                  DungeonLeadKernel::ReadyStatus::Ready;
    f.msInState = st.stateSinceTs ? GetMSTimeDiffToNow(st.stateSinceTs) : 0;
    f.postCombatMinMs = sDungeonLeadConfig.dungeonLeadPostCombatMinSeconds * IN_MILLISECONDS;
    f.preparingPull = st.pullState == DungeonLeadKernel::PullState::Marking;
    f.recovering = st.recovery.reason != DungeonLeadKernel::RecoveryReason::None;
    f.bossPack = st.bossPackCurrent;
    f.bossEngaged = st.bossEngaged;
    f.pulling = st.pullState == DungeonLeadKernel::PullState::Initiating ||
                st.pullState == DungeonLeadKernel::PullState::Establishing;

    DungeonLeadKernel::Transition const t = DungeonLeadKernel::DecideActive(f);
    if (t.next != st.state)
    {
        // Wipe bookkeeping rides on the transitions in and out of WipeRecovery. wipeCount is
        // never cleared: it is the run's honest tally, reported in the run summary.
        if (t.next == LeadState::WipeRecovery)
        {
            ++st.wipeCount;
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} died while leading (run={}, wipe #{}) - "
                     "recovering, {}s before giving up", bot->GetName(), st.runId, st.wipeCount,
                     sDungeonLeadConfig.dungeonLeadWipeRecoverySeconds);
            DungeonLead::RecordEvent(botAI, "wipe_detected", "wipe #" + std::to_string(st.wipeCount));
        }
        else if (st.state == LeadState::WipeRecovery)
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} recovered from wipe #{} on run={} - resuming",
                     bot->GetName(), st.wipeCount, st.runId);
            DungeonLead::RecordEvent(botAI, "wipe_recovered", "wipe #" + std::to_string(st.wipeCount));
            DungeonLead::RestoreCheckpoint(botAI, st);
        }
        if (t.next == LeadState::Combat && !st.anchorSet)
        {
            st.anchorSet = true;
            st.anchorX = bot->GetPositionX();
            st.anchorY = bot->GetPositionY();
            st.anchorZ = bot->GetPositionZ();
            st.anchorRadius = 0.f;  // CombatLeashRadius
        }
        else if (t.next == LeadState::BossCombat && st.bossLeash > 0.f)
        {
            // A boss fight is held where the boss lives, with the boss leash - also when it starts
            // out of a fight with its trash.
            st.anchorSet = true;
            st.anchorX = st.bossTankX;
            st.anchorY = st.bossTankY;
            st.anchorZ = st.bossTankZ;
            st.anchorRadius = st.bossLeash;
            DungeonLead::RecordEvent(botAI, "boss_combat",
                                     "tank_pos=(" + std::to_string(int(st.bossTankX)) + "," +
                                         std::to_string(int(st.bossTankY)) + ") leash=" +
                                         std::to_string(int(st.bossLeash)));
        }
        else if (t.next == LeadState::BossPrep)
        {
            // Facing is mod-playerbots' "tank face" (turns the target away from the group); tank
            // specs get it by default. A non-tank leader gets it for the boss.
            bool const hadFace = botAI->HasStrategy("tank face", BOT_STATE_COMBAT);
            if (!hadFace)
                botAI->ChangeStrategy("+tank face", BOT_STATE_COMBAT);
            DungeonLead::RecordEvent(botAI, "boss_prep",
                                     "tank_pos=(" + std::to_string(int(st.bossTankX)) + "," +
                                         std::to_string(int(st.bossTankY)) + ") leash=" +
                                         std::to_string(int(st.bossLeash)) +
                                         " tank_face=" + (hadFace ? "default" : "added"));
        }
        else if (t.next == LeadState::Travelling)
        {
            st.anchorSet = false;  // walking on - the next fight gets its own anchor
            // Stuck/arrival timers measure walking only: time spent fighting or waiting is not
            // being stuck (a boss was skipped as "stuck 196s" after 3 minutes of trash fights).
            st.bestDist = 0.f;
            st.stuckTs = 0;
            st.stuckAttempts = 0;
            st.arrivedTold = false;
        }
        std::string detail;
        if (t.next == LeadState::Combat || t.next == LeadState::BossCombat)
            detail = "anchor=(" + std::to_string(int(st.anchorX)) + "," + std::to_string(int(st.anchorY)) + ")";
        else if (st.state == LeadState::PostCombat)
        {
            // The decision between two packs, with what it was based on.
            float lowestHealth = 100.f, healerMana = 100.f;
            for (DungeonLeadKernel::PartyMemberFacts const& m : party.facts.members)
            {
                if (!m.alive)
                    continue;
                lowestHealth = std::min(lowestHealth, m.healthPct);
                if (m.isHealerRole && !m.isSelf)
                    healerMana = std::min(healerMana, m.manaPct);
            }
            DungeonLeadKernel::Readiness const r =
                DungeonPartyState::Readiness(party, DungeonLeadKernel::ReadyPurpose::Walk);
            detail = std::string("readiness=") + DungeonLeadKernel::ToString(r.status) +
                     " lowest_health=" + std::to_string(int(lowestHealth)) +
                     " healer_mana=" + std::to_string(int(healerMana)) +
                     " waited_ms=" + std::to_string(f.msInState);
            DungeonLead::RecordEvent(botAI, "post_combat_decision",
                                     std::string(DungeonLeadKernel::ToString(t.next)) + " " + detail);
        }
        TransitionTo(botAI, st, t.next, t.reason, detail);
    }

    if (st.state != LeadState::WipeRecovery)
        return true;

    // Upstream's BOT_STATE_DEAD engine does the release / corpse run / resurrect; this only has to
    // give it enough time (WipeRecoverySeconds - 45 s demonstrably cut it off) and then give up
    // honestly instead of holding the session forever. Too many wipes means the party cannot do
    // this dungeon and more recovery will not change that.
    char const* giveUp = nullptr;
    if (sDungeonLeadConfig.dungeonLeadMaxWipesPerRun && st.wipeCount > sDungeonLeadConfig.dungeonLeadMaxWipesPerRun)
        giveUp = "wipe limit reached";
    else if (GetMSTimeDiffToNow(st.stateSinceTs) >= sDungeonLeadConfig.dungeonLeadWipeRecoverySeconds * IN_MILLISECONDS)
        giveUp = "recovery timed out";
    if (!giveUp)
        return true;

    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} giving up run={} after wipe #{} ({})", bot->GetName(),
             st.runId, st.wipeCount, giveUp);
    st.outcome = DungeonRunOutcome::Partial;
    st.failureDomain = DungeonFailureDomain::Combat;
    st.failureReason = DungeonFailureReason::PartyWipe;
    DungeonLead::RecordEvent(botAI, "wipe_giveup", giveUp);
    DungeonLead::RecordRunSummary(botAI, "wipe");
    DungeonLead::Stop(botAI, /*giveLeaderBack*/ true);
    return false;
}
