/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadTriggers.h"

#include "Creature.h"
#include "DungeonLeadActions.h"
#include "Group.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"

bool DungeonLeadIdleTrigger::IsActive()
{
    return DungeonLead::IsOn(botAI) && DungeonLead::InFiveMan(bot) && !bot->IsInCombat();
}

bool DungeonLeadBossNearTrigger::IsActive()
{
    if (!DungeonLead::IsOn(botAI) || !DungeonLead::InFiveMan(bot))
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    Creature* boss = DungeonLead::FindBossNear(botAI, 60.0f);
    if (!boss)
        return false;

    ObjectGuid skull = group->GetTargetIcon(RtiTargetValue::skullIndex);
    if (skull.IsEmpty())
        return true;

    Unit* marked = botAI->GetUnit(skull);
    return !marked || !marked->IsAlive();
}

bool DungeonLeadLeftInstanceTrigger::IsActive()
{
    return DungeonLead::IsOn(botAI) && !DungeonLead::InFiveMan(bot);
}
