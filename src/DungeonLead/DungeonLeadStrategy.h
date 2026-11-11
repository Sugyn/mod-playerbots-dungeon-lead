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

// Added to BOTH engines by the "startdungeon" chat command (see DungeonLeadActions.cpp).
class DungeonLeadStrategy : public Strategy
{
public:
    DungeonLeadStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "dungeon lead"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

// Gates the class AI's own movement/pulls by the session: no chasing beyond the combat leash, and
// no opportunistic pull ("grind") unless the brain's state and the party's pull readiness allow it.
class DungeonLeadMultiplier : public Multiplier
{
public:
    DungeonLeadMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "dungeon lead") {}
    float GetValue(Action* action) override;
};

#endif
