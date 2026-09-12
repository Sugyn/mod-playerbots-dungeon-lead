/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
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
