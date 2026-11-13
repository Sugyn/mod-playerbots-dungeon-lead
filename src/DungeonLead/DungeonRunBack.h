/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef MOD_DUNGEONLEAD_RUNBACK_H
#define MOD_DUNGEONLEAD_RUNBACK_H

#include "DungeonLeadKernels.h"

class Player;
class PlayerbotAI;
struct DungeonLeadState;

namespace DungeonLead
{
    // What the dead-state multiplier of a bot needs to know about its session's run back.
    struct RunBackGate
    {
        bool inSession = false;     // a bot of an active session's party
        bool onInstanceMap = false;  // on the session's dungeon map
        DungeonLeadKernel::RunBackPhase phase = DungeonLeadKernel::RunBackPhase::None;
    };
    RunBackGate RunBackGateFor(Player* bot);

    // From GuardActiveSessions() (every pass): resurrection in place, release, the ghost's walk
    // to the portal, the walk to the party, and the teleport fallback of a failed walk.
    void RunBackMembers(PlayerbotAI* leaderAI, DungeonLeadState& st);

    // A walk order for a living bot straggler (recovery, party_fragmented): it walks to the leader,
    // who waits; RunBackMembers drives the walk and its fallback.
    void OrderStragglerWalk(PlayerbotAI* leaderAI, DungeonLeadState& st, Player* who);

    // `who` is on its way back (dead, walking to the portal or to the party).
    bool RunBackActive(DungeonLeadState const& st, Player* who);
    // Any party bot is on its way back.
    bool RunBackAny(DungeonLeadState const& st);
}

#endif
