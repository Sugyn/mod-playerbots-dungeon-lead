/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonPullController.h
 *
 * Pull lifecycle for the current Pull/Boss pack: Approaching -> WaitingParty -> Marking ->
 * Initiating -> Establishing -> Established, or Failed (bounded retries, then the pack is skipped
 * and recorded - never a silent route advance). Decisions in DungeonLeadKernel::DecidePull.
 *
 * Orchestration only: the pull target is the target plan's primary, skull-marked by
 * DungeonTargetManager; the controller waits for that mark and starts the attack through
 * mod-playerbots' own "attack rti target"; the class AI does the fighting. A fight the class AI
 * opens on its own (grind) is simply observed as Established.
 */

#ifndef MOD_DUNGEONLEAD_PULLCONTROLLER_H
#define MOD_DUNGEONLEAD_PULLCONTROLLER_H

class PlayerbotAI;
struct DungeonLeadState;
struct DungeonPartySnapshot;

namespace DungeonPullController
{
    // One step for an active session, from GuardActiveSessions() (every 2 s, world thread, after
    // all map updates have joined). Runs before DungeonLeadBrain::Update so the brain sees the
    // current pull state.
    void Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party);

    // For the walk (every AI tick): the leader has just come into pull range and sight of the
    // current pack's pull target while the controller is still Approaching - stop here instead
    // of running on until its next pass (2 s, ~14 yd: into aggro in SFK's courtyard). Bounded:
    // a stop the controller doesn't pick up within a few seconds lets the walk go on.
    bool StopForPull(PlayerbotAI* botAI, DungeonLeadState& st);
}

#endif
