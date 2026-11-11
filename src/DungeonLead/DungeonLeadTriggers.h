/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONLEADTRIGGERS_H
#define PLAYERBOTS_DUNGEONLEADTRIGGERS_H

#include "Trigger.h"

class PlayerbotAI;

class DungeonLeadIdleTrigger : public Trigger
{
public:
    DungeonLeadIdleTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon lead idle") {}
    bool IsActive() override;
};

class DungeonLeadBossNearTrigger : public Trigger
{
public:
    DungeonLeadBossNearTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon lead boss near") {}
    bool IsActive() override;
};

class DungeonLeadLeftInstanceTrigger : public Trigger
{
public:
    DungeonLeadLeftInstanceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon lead left instance") {}
    bool IsActive() override;
};

#endif
