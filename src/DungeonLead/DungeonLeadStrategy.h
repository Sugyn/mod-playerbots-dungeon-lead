/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONLEADSTRATEGY_H
#define PLAYERBOTS_DUNGEONLEADSTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class PlayerbotAI;

// Added to BOTH engines by the "startdung" chat command (see DungeonLeadActions.cpp).
class DungeonLeadStrategy : public Strategy
{
public:
    DungeonLeadStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "dungeon lead"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// Holds the leader back (no new pulls, no walking on) while the healer is low on mana / someone is
// drinking, and never walks on while anyone in the group is still fighting.
class DungeonLeadMultiplier : public Multiplier
{
public:
    DungeonLeadMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "dungeon lead") {}
    float GetValue(Action* action) override;
};

#endif
