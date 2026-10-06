/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadStrategy.h"

#include "Action.h"
#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "Timer.h"
#include "MotionMaster.h"
#include "Creature.h"
#include "DungeonPartyState.h"
#include "Playerbots.h"

void DungeonLeadStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // below "attack anything" (4.0, from grind) and food/drink (4.1/4.2): fight and eat first, walk on after
    triggers.push_back(new TriggerNode("dungeon lead idle", { NextAction("dungeon lead next", 3.5f) }));
    triggers.push_back(new TriggerNode("dungeon lead boss near", { NextAction("dungeon lead mark", 15.0f) }));  // > ACTION_NORMAL (mark rti), boss keeps the skull over a low-HP add
    triggers.push_back(new TriggerNode("dungeon lead left instance", { NextAction("dungeon lead stop", 9.0f) }));
    // "often" fires regardless of combat state - unlike "dungeon lead idle" above, which must not
    // fire mid-fight. A stale CC mark needs releasing *during* combat (the marked target staying
    // alive and fighting keeps everyone flagged in combat, so gating this on !IsInCombat() would
    // mean it can never run in exactly the situation that created the problem).
    triggers.push_back(new TriggerNode("often", { NextAction("dungeon lead cc watch", ACTION_NORMAL) }));
}

void DungeonLeadStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new DungeonLeadMultiplier(botAI));
}

float DungeonLeadMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    std::string const name = action->getName();

    // Combat leash: don't chase a target that has left the area the fight began in.
    if (name == "reach melee" || name == "reach spell")
    {
        Player* bot = botAI->GetBot();
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        Unit* target = action->GetTarget();
        DungeonLeadKernel::LeashFacts leash;
        leash.anchorSet = st.anchorSet;
        leash.inCombat = bot->IsInCombat();
        leash.hasTarget = target != nullptr;
        if (target)
        {
            leash.targetDistFromAnchor = target->GetExactDist(st.anchorX, st.anchorY, st.anchorZ);
            Unit* victim = target->GetVictim();
            Player* victimPlayer = victim ? victim->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            leash.targetAttackingParty = victimPlayer && (victimPlayer == bot || bot->IsInSameGroupWith(victimPlayer));
            if (Creature* c = target->ToCreature())
                leash.targetRunningForHelp = c->HasUnitState(UNIT_STATE_FLEEING) ||
                    c->GetMotionMaster()->GetCurrentMovementGeneratorType() == ASSISTANCE_MOTION_TYPE;
        }
        float const radius = st.anchorRadius > 0.f ? st.anchorRadius : sDungeonLeadConfig.dungeonLeadCombatLeashRadius;
        if (DungeonLeadKernel::ChaseAllowed(leash, radius))
            return 1.0f;
        if (!st.lastLeashLogTs || GetMSTimeDiffToNow(st.lastLeashLogTs) > 5000)
        {
            st.lastLeashLogTs = getMSTime();
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} leash: not chasing {} ({:.0f} yd from the combat anchor)",
                     bot->GetName(), target->GetName(), leash.targetDistFromAnchor);
            DungeonLead::RecordEvent(botAI, "leash_hold",
                                     target->GetName() + " dist=" + std::to_string(int(leash.targetDistFromAnchor)));
        }
        return 0.0f;
    }

    // Walking on is the brain's decision alone (the route walk runs only in Travelling). What is
    // gated here is "grind"'s own pull actions, which nothing else would stop: the tank could be
    // holding for the party yet open a fight the moment something wandered into range. They follow
    // the brain's state (no new fight while waiting, recovering, ...) and the pull readiness.
    bool const isPull = name == "attack anything" || name == "pull my target" || name == "pull rti target";
    if (!isPull)
        return 1.0f;

    DungeonLeadState const& st = sDungeonRouteMgr.State(botAI->GetBot()->GetGUID());
    if (st.paused || !DungeonLeadKernel::StateAllowsNewPull(st.state))
        return 0.0f;
    DungeonPartySnapshot const snap = DungeonPartyState::Evaluate(botAI);
    if (DungeonPartyState::Readiness(snap, DungeonLeadKernel::ReadyPurpose::Pull).status !=
        DungeonLeadKernel::ReadyStatus::Ready)
        return 0.0f;
    return 1.0f;
}
