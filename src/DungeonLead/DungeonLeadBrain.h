/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadBrain.h
 *
 * The session's state machine. DungeonLeadState::state is its one authoritative state and
 * TransitionTo() is the only thing that changes it (every change logged and recorded as a
 * "state_transition" event). Update() moves an active session along from observed facts; the
 * decision itself is DungeonLeadKernel::DecideActive. Starting/Stopping are driven by the
 * leadership reconciliation in DungeonLeadActions.cpp, which also goes through TransitionTo().
 */

#ifndef MOD_DUNGEONLEAD_BRAIN_H
#define MOD_DUNGEONLEAD_BRAIN_H

#include "DungeonLeadKernels.h"

#include <string>

class PlayerbotAI;
struct DungeonLeadState;
struct DungeonPartySnapshot;

namespace DungeonLeadBrain
{
    void TransitionTo(PlayerbotAI* botAI, DungeonLeadState& st, DungeonLeadKernel::LeadState next,
                      DungeonLeadKernel::TransitionReason reason, std::string const& detail = "");

    // One step for an active session (no-op for Starting/Stopping). Called from
    // GuardActiveSessions() every 2 s and from the route walk's isUseful() every AI tick.
    // Returns false if the session ended during the update (wipe give-up) - the caller must not
    // touch its DungeonLeadState reference afterwards.
    bool Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party);
}

#endif
