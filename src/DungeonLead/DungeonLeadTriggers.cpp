/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
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
