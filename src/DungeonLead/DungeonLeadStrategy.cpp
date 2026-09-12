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
    bool isPull = name == "attack anything" || name == "pull my target" || name == "pull rti target";
    bool isWalk = name == "dungeon lead next";
    if (!isPull && !isWalk)
        return 1.0f;

    if (sDungeonRouteMgr.State(botAI->GetBot()->GetGUID()).paused)
        return 0.0f;  // "startdung pause": no new pulls either, not just no walking
    if (DungeonLead::HealerManaLow(botAI) || DungeonLead::GroupResting(botAI))
        return 0.0f;
    if (isWalk && DungeonLead::GroupInCombat(botAI))
        return 0.0f;
    // isUseful() stops the ROUTE WALK for these, but "grind"'s own pull actions aren't gated by
    // it at all - without this, the tank could stand still waiting for the player yet still open
    // a brand new fight the moment something wandered into range, which is the opposite of "wait"
    if (DungeonLead::MasterTooFar(botAI) || DungeonLead::GroupTooSpread(botAI))
        return 0.0f;

    return 1.0f;
}
