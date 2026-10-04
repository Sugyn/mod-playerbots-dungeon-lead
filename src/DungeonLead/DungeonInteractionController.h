/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonInteractionController.h
 *
 * Route blockers that walking and pulling can't solve. Only what the routes need so far: a closed
 * door or gate in the way. Doors in dungeons are opened by events, boss deaths or keys, so the
 * leader does not touch it - it waits for the world to show it open (DoorWaitSeconds), then the
 * walk resumes; if it never opens, the step fails through the objective policy with reason
 * "door_closed" instead of the walk thrashing against it. Decisions: DungeonLeadKernel::
 * DecideInteraction. Data-free: the door is found where the walk got stuck.
 */

#ifndef MOD_DUNGEONLEAD_INTERACTIONCONTROLLER_H
#define MOD_DUNGEONLEAD_INTERACTIONCONTROLLER_H

class PlayerbotAI;
struct DungeonLeadState;

namespace DungeonInteractionController
{
    // From the route walk when it is about to give up on reaching (x, y, z): if a closed door
    // between the leader and that point explains it, start a door interaction and return true.
    bool StartIfBlockedByDoor(PlayerbotAI* botAI, DungeonLeadState& st, float x, float y, float z);

    // From GuardActiveSessions() (2 s): advance an active interaction. Returns false if it ended
    // the session (via the objective failure policy) - the caller must not touch it any more.
    bool Update(PlayerbotAI* botAI);
}

#endif
