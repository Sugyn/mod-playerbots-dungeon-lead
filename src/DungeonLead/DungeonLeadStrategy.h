/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
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
