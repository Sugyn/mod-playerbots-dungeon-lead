/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef MOD_DUNGEONLEAD_SIMPLAYER_H
#define MOD_DUNGEONLEAD_SIMPLAYER_H

#include <cstdint>
#include <string>

class Player;
class PlayerbotAI;
struct DungeonLeadState;

// Simulated player (test tool, AiPlayerbot.DungeonLead.ValidationSimPlayer, off by default): one bot
// of a validation party that the module treats exactly like a real player - the session is started
// for it (it is the master), it is never released, walked, moved or teleported by the module - while
// a small seeded script plays it: it follows the party, lags behind, stands still,
// goes away (AFK), dies, logs out and back in, or leaves the group. Every scripted action and every
// teleport of it is recorded (`sim_player`), so a campaign shows that the module never moved it.
namespace DungeonLead
{
    // Marks / unmarks a character as a simulated player (by guid; survives its logout and login).
    void MarkSimulatedPlayer(Player* player, uint32_t seed);
    void UnmarkSimulatedPlayer(uint64_t guidRaw);
    // Safe from any thread.
    bool IsSimulatedPlayer(Player* player);

    // From GuardActiveSessions() for a session whose master is a simulated player: its next action.
    void SimPlayerTick(PlayerbotAI* leaderAI, DungeonLeadState& st);
    // From the world update: what runs on time, in a session or after it (logging back in, being
    // resurrected, an action running out).
    void SimPlayersTick();

    // From the teleport hook: a teleport of a simulated player during a session. Records it.
    void SimPlayerTeleported(Player* player, uint32_t mapId, float x, float y, float z);

    // The release of a dead simulated player waits for its script (a player decides that).
    bool SimPlayerHoldsRelease(Player* player);
}

#endif
