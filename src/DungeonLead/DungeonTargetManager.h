/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonTargetManager.h
 *
 * One stable target plan per fight: primary (skull), secondary (cross) and a CC candidate
 * (moon). Candidates are the hostiles around the current pack before a pull, and the units in
 * combat around the leader during one. The plan (DungeonLeadKernel::PickTargetPlan) is
 * authoritative; raid marks only mirror it, and only marks this module placed are ever moved or
 * cleared - a player's own mark is left alone. The class AI picks its targets from those marks.
 */

#ifndef MOD_DUNGEONLEAD_TARGETMANAGER_H
#define MOD_DUNGEONLEAD_TARGETMANAGER_H

#include <cstdint>

class Creature;
class PlayerbotAI;

namespace DungeonTargetManager
{
    // Records how one moon ended ("cc_outcome"): the breakable crowd control aura on the target
    // and its caster, or, for each party bot that can crowd control, why it did not (range, line
    // of sight, spell not castable, busy casting). Called once per moon outcome, never per tick.
    void RecordCcOutcome(PlayerbotAI* botAI, Creature* target, char const* outcome, uint32_t markedMs);

    // Rebuilds the plan and syncs the marks. Called from GuardActiveSessions() before the pull
    // controller, and on demand by the "dungeon lead mark" action when a boss comes into range.
    // Returns true if any mark changed.
    bool Update(PlayerbotAI* botAI);
}

#endif
