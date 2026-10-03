/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonPartyState.h
 *
 * The one place that observes the party for readiness. Evaluate() reads the live group once into
 * plain facts (DungeonLeadKernel::PartyFacts); the decision is DungeonLeadKernel::EvaluateReadiness.
 * The route walk (DungeonLeadNextAction::isUseful) and the pull gate (DungeonLeadMultiplier) both
 * go through here, so they cannot disagree about whether the party is ready.
 */

#ifndef MOD_DUNGEONLEAD_PARTYSTATE_H
#define MOD_DUNGEONLEAD_PARTYSTATE_H

#include "DungeonLeadKernels.h"

#include <vector>

class Player;
class PlayerbotAI;

struct DungeonPartySnapshot
{
    DungeonLeadKernel::PartyFacts facts;
    std::vector<Player*> members;  // parallel to facts.members, valid for the current tick only

    Player* Member(int index) const
    {
        return index >= 0 && size_t(index) < members.size() ? members[index] : nullptr;
    }
};

namespace DungeonPartyState
{
    DungeonPartySnapshot Evaluate(PlayerbotAI* leaderAI);
    DungeonLeadKernel::ReadinessPolicy Policy();  // from DungeonLeadConfig
    DungeonLeadKernel::Readiness Readiness(DungeonPartySnapshot const& snap, DungeonLeadKernel::ReadyPurpose purpose);
}

#endif
