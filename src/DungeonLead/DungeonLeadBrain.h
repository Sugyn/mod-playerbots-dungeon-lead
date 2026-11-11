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
#include "DungeonRouteTypes.h"

#include <string>

class PlayerbotAI;
struct DungeonLeadState;
struct DungeonPartySnapshot;

// Where the leader is going and why. Derived on demand from the session's route position and
// state - never stored, so it cannot drift from them.
struct DungeonLeadObjective
{
    bool valid = false;  // false: no route resolved yet (or none exists for this dungeon)
    DungeonRouteNodeType type = DungeonRouteNodeType::End;
    int32_t stepIndex = -1;  // -1 for End
    std::string name;
    float x = 0.f, y = 0.f, z = 0.f;

    std::string Describe() const;  // "boss:Lady Anacondra@3", "recovery:entrance@-1", "end", "none"
};

namespace DungeonLeadBrain
{
    DungeonLeadObjective CurrentObjective(DungeonLeadState const& st);

    void TransitionTo(PlayerbotAI* botAI, DungeonLeadState& st, DungeonLeadKernel::LeadState next,
                      DungeonLeadKernel::TransitionReason reason, std::string const& detail = "");

    // One step for an active session (no-op for Starting/Stopping). Called from
    // GuardActiveSessions() every 2 s and from the route walk's isUseful() every AI tick.
    // Returns false if the session ended during the update (wipe give-up) - the caller must not
    // touch its DungeonLeadState reference afterwards.
    bool Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party);
}

#endif
