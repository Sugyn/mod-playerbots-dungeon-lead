/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DungeonLeadStrategy.h"

#include "Action.h"
#include "DungeonLeadActions.h"
#include "Playerbots.h"

void DungeonLeadStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // below "attack anything" (4.0, from grind) and food/drink (4.1/4.2): fight and eat first, walk on after
    triggers.push_back(new TriggerNode("dungeon lead idle", { NextAction("dungeon lead next", 3.5f) }));
    triggers.push_back(new TriggerNode("dungeon lead boss near", { NextAction("dungeon lead mark", 5.0f) }));
    triggers.push_back(new TriggerNode("dungeon lead left instance", { NextAction("dungeon lead stop", 9.0f) }));
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

    if (DungeonLead::HealerManaLow(botAI) || DungeonLead::GroupResting(botAI))
        return 0.0f;
    if (isWalk && DungeonLead::GroupInCombat(botAI))
        return 0.0f;

    return 1.0f;
}
