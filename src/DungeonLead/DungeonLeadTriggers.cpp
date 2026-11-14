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
#include "DungeonPartyState.h"
#include "Group.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"

bool DungeonLeadIdleTrigger::IsActive()
{
    return DungeonLead::IsOn(botAI) && DungeonLead::InFiveMan(bot) && !DungeonPartyState::LeaderFightsEnemy(bot);
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
    if (!DungeonLead::IsOn(botAI) || DungeonLead::InFiveMan(bot))
        return false;

    // Being outside the dungeon while DEAD is a wipe in progress, not an abandoned run. A dungeon
    // with no graveyard of its own releases the ghost to the nearest outdoor graveyard, so every
    // wipe puts the leader out here for a few seconds - and this trigger fires at relevance 9.0,
    // which would stop the session before RecoverStrandedMembers() had any chance to teleport it
    // back.
    //
    // Hanging forever is not the risk here: GuardActiveSessions() independently fails the run
    // once the leader has been dead for WipeRecoverySeconds, so an unrecoverable wipe still ends,
    // just as a wipe rather than as a mislabelled "left instance".
    if (!bot->IsAlive())
        return false;

    return true;
}
