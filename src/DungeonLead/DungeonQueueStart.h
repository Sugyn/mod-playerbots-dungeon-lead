/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef MOD_DUNGEONLEAD_QUEUESTART_H
#define MOD_DUNGEONLEAD_QUEUESTART_H

#include "ObjectGuid.h"

// Start from the dungeon queue (AiPlayerbot.DungeonLead.QueueAutoStart, off by default): when a group
// the dungeon finder formed enters a supported dungeon with a bot tank and a real player, the bot
// tank leads the run by itself (origin queue: the party waits for its player like a manual session).
// A player tank leads themselves; an unsupported dungeon gets one short message. When the route is
// done the leader says so, hands the lead back, and the bots leave the group a little later.
namespace DungeonLead
{
    // From the world update, throttled inside.
    void QueueStartTick();
    // After the route of a queue session: the bots of `group` leave it after kQueueBreakupDelayMs.
    void ScheduleQueueBreakup(ObjectGuid group, ObjectGuid player);
}

#endif
