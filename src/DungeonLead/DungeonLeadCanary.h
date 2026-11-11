/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONLEADCANARY_H
#define PLAYERBOTS_DUNGEONLEADCANARY_H

#include "Common.h"

#include <string>

// AutoBot Canary: an unattended controller that starts Dungeon Lead sessions on its own, on
// bot-only groups, so a server operator running many bot dungeons at once gets telemetry from all
// of them instead of only the ones a real player said "startdungeon" in. See docs/architecture.md.
//
// Two ways a session gets started, both gated by the same config:
//   - CanaryTick(): PASSIVE - watches for groups that formed via the normal playerbot LFG queue
//     and already are a pure-bot 5-man in an allowed dungeon.
//   - TriggerTargetedTest(): ON-DEMAND - makes specific idle bots queue for a named dungeon right
//     now, instead of waiting for CanaryTick() to spot one by chance. Reachable without a live
//     game session via ".dungeonlead canarytest <lfgId> [groups]" (SOAP-friendly).
//
// Neither one ever forms a group or teleports a bot directly - both go through the real LFG
// queue-and-match pipeline, just with the acceptable-dungeon list narrowed. Shared safety rules:
//   - Off by default (AiPlayerbot.DungeonLead.CanaryEnabled = false).
//   - Empty allowlist by default (AiPlayerbot.DungeonLead.CanaryAllowedLfgIds = ""): even with
//     CanaryEnabled=true, nothing runs until an operator names specific dungeons.
//   - Never touches a group with a real player in it, at either the start check or on every
//     subsequent tick (a real player joining mid-session stops the canary session immediately
//     and hands control back).
//   - Bounded: CanaryMaxConcurrent caps how many canary sessions run at once - both CanaryTick()
//     and TriggerTargetedTest() count against the same shared cap - and CanaryTimeoutMinutes
//     force-stops one that never reaches a terminal outcome.
class Player;
class PlayerbotAI;

namespace DungeonLead
{
    // Called every world tick from PlayerbotsWorldScript::OnUpdate, right next to
    // GuardActiveSessions() - throttles and no-ops internally like that function does. Runs
    // CanarySupervisorTick() unconditionally, then MaybeStartCanary() only if CanaryEnabled.
    void CanaryTick();

    // Supervision pass: safety-checks every active AutoCanary session (real-player join, timeout)
    // and stops any that need it, REGARDLESS of CanaryEnabled. Returns the resulting active-canary
    // count for MaybeStartCanary()'s capacity check. Only meant to be called from CanaryTick().
    uint32 CanarySupervisorTick();

    // Creation pass: tries to start one new canary session, gated by CanaryEnabled.
    // `activeCanaryCount` is CanarySupervisorTick()'s return value.
    void MaybeStartCanary(uint32 activeCanaryCount);

    // On-demand targeted test: makes idle bots queue via the REAL LFG tool for exactly `lfgId`
    // (the same CMSG_LFG_JOIN path every bot dungeon run goes through, with the dungeon list forced
    // to one entry). Normal LFG matchmaking then groups and teleports each party, and the passive
    // CanaryTick() detects and takes over each one - no separate "start" step here. Still gated by
    // CanaryEnabled and CanaryAllowedLfgIds (it only skips the *waiting*), and by
    // CanaryMaxConcurrent (shared with CanaryTick(); requesting more groups than there is room
    // for starts as many as fit).
    // `master`: the real player who asked for this, for the chat report only - null for a
    // console/SOAP-triggered call.
    // `groups`: how many separate 5-bot parties to queue at once. Each gets a disjoint set of bots;
    // LFG may still merge/split them differently once matchmaking runs.
    // Returns a human-readable result string (what was queued, or why nothing was).
    std::string TriggerTargetedTest(Player* master, uint32 lfgId, uint32 groups = 1);

    // How many AutoCanary-origin sessions (CanaryTick()-spotted or TriggerTargetedTest()/
    // RunTestParty()-started, no matter which) are active right now - the live count
    // CanaryMaxConcurrent is checked against. Exposed so every caller that needs to respect that
    // one shared budget (DungeonTestBotPool::RunTestParty() included) reads the same number
    // instead of keeping its own possibly-divergent count.
    uint32 ActiveCanaryCount();

    // The single-bot primitive TriggerTargetedTest() builds its parties out of - exposed
    // separately for DungeonTestBotPool, which already knows which bots it wants grouped (its own
    // leased Tank/Healer/Dps identities) and just needs them queued for the same dungeon so real
    // LFG matchmaking puts them together. `roleMask` is a PLAYER_ROLE_* value
    // (lfg::PLAYER_ROLE_TANK/HEALER/DAMAGE). Same CMSG_LFG_JOIN path, same safety properties (does
    // not bypass CanaryEnabled/CanaryAllowedLfgIds - the resulting group still needs CanaryTick()
    // to pick it up and start a session, same as any other targeted-test party).
    void QueueBotForLfg(PlayerbotAI* botAI, Player* bot, uint32 lfgId, uint32 roleMask);
}

#endif
