/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonRecoveryController.h
 *
 * Bounded recovery for problems that waiting alone may never fix. Each reason has an action, a
 * success condition (the problem is no longer observed), a timeout, an escalation and finally an
 * abort that stops the session as failed - decided by DungeonLeadKernel::DecideRecovery.
 *
 *   reason              act (RecoveryTimeoutSeconds)       escalate (RecoveryEscalationSeconds)
 *   party_fragmented    leader walks back to the member    bring that bot member to the leader
 *   member_lost         wait (zoning, reconnect)           bring that bot member to the leader
 *   member_dead         wait for a resurrection             none - recorded
 *   leadership_lost     wait for it to come back           none - recorded
 *   master_unavailable  wait (reconnect, release, rejoin)  none - recorded (DL-003)
 *   master_too_far      wait for the real player to return  none - recorded (DL-003)
 *   persistent_wait     wait (drink/heal/mana up)          none - recorded (DL-003)
 *
 * A real player is never moved: only bot members are brought over (see DungeonRecoveryController
 * .cpp's `movable` check in the Escalate step - only party_fragmented/member_lost ever qualify).
 * master_unavailable/master_too_far/persistent_wait exist so a session can't be held open forever
 * by a dead/departed real player, one stuck past the leash, or a resource wait that never
 * resolves - but escalation for them is still a no-op: waiting is the only "action", abort is the
 * actual bound. Ordinary, brief instances of these (a normal drink, a moment of low health) never
 * reach escalate/abort at all; only a persistent one does.
 * The leader's own death is the brain's WipeRecovery, not this.
 */

#ifndef MOD_DUNGEONLEAD_RECOVERYCONTROLLER_H
#define MOD_DUNGEONLEAD_RECOVERYCONTROLLER_H

class PlayerbotAI;
struct DungeonPartySnapshot;

namespace DungeonRecoveryController
{
    // From GuardActiveSessions() (2 s), before the brain. Returns false if it ended the session
    // (abort) - the caller must not touch the session afterwards.
    bool Update(PlayerbotAI* botAI, DungeonPartySnapshot const& party);
}

#endif
