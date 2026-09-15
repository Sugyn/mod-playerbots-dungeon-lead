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
// groups nobody asked it to touch, so a server operator running many bot dungeons at once gets
// telemetry from all of them instead of only the ones a real player happened to say
// "startdungeon" in. See docs/architecture/ in the repo for the full design review.
//
// Two ways a session gets started, both gated by the same config:
//   - CanaryTick() (Stage 0/1): PASSIVE - watches for groups that organically formed via the
//     normal playerbot LFG queue and already are a pure-bot 5-man in an allowed dungeon.
//   - TriggerTargetedTest() (Stage 2): ON-DEMAND - forces specific idle bots to queue for a named
//     dungeon right now, instead of waiting for CanaryTick() to spot one by chance. Reachable
//     without a live game session via ".playerbots canarytest <lfgId> [groups]" (SOAP-friendly).
//
// Neither one ever forms a group or teleports a bot directly - both go through the real LFG
// queue-and-match pipeline, just with the acceptable-dungeon list narrowed. Shared safety rules:
//   - Off by default (AiPlayerbot.DungeonLead.CanaryEnabled = false). A fresh checkout of this
//     patch behaves exactly as if this file didn't exist.
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
    // GuardActiveSessions() - throttles and no-ops internally exactly like that function does, so
    // the call site stays a single unconditional line regardless of config. Thin wrapper: runs
    // CanarySupervisorTick() unconditionally, then MaybeStartCanary() only if CanaryEnabled - see
    // DL-008 on why supervision and creation are no longer gated by the same check.
    void CanaryTick();

    // DL-008 pass 1: safety-checks every currently-active AutoCanary session (real-player-join,
    // timeout) and stops any that need it, REGARDLESS of CanaryEnabled - an existing session must
    // not go unsupervised just because the flag that would have started a new one is off. Returns
    // the resulting active-canary count for MaybeStartCanary()'s capacity check. Exposed (not
    // file-local) only so CanaryTick() can sequence it before MaybeStartCanary(); not intended to
    // be called from anywhere else.
    uint32 CanarySupervisorTick();

    // DL-008 pass 2: the old CanaryTick()'s "try to start one new canary session" logic, now gated
    // by CanaryEnabled on its own instead of gating supervision too. `activeCanaryCount` is
    // CanarySupervisorTick()'s return value - avoids a second scan of GetActiveSessionGuids().
    void MaybeStartCanary(uint32 activeCanaryCount);

    // Stage 2: on-demand targeted test, added after live use of Stage 0/1 surfaced the obvious
    // problem with pure passive sampling - "wait for the right bots to randomly queue for the
    // dungeon I actually want tested" can take arbitrarily long for a low-traffic dungeon, and
    // provides no way to deliberately run several in parallel to accumulate data faster. Instead
    // of writing new group-formation/teleport code (the larger, riskier feature ADR-002 explicitly
    // deferred), this forces idle bots to queue via the REAL LFG tool for exactly `lfgId` - the
    // same CMSG_LFG_JOIN path every organic bot dungeon run already goes through, just with the
    // dungeon list forced to one entry instead of the bot's own computed set. Normal LFG
    // matchmaking then groups and teleports each resulting party exactly as it would for any real
    // group, and the existing passive CanaryTick() detects and takes over each one automatically -
    // no separate "start" step needed here. Still gated by CanaryEnabled and CanaryAllowedLfgIds
    // (this does not bypass the safety allowlist, it only skips the *waiting*), and by
    // CanaryMaxConcurrent (shared with CanaryTick() - requesting more groups than there is room
    // for starts as many as fit, never more).
    // `master`: the real player who asked for this, for the chat report only - null for a
    // console/SOAP-triggered call, which has no associated player.
    // `groups`: how many separate 5-bot parties to queue at once (e.g. 10, to run ten dungeons in
    // parallel and gather data ten times as fast - the actual motivating use case for this
    // function). Each gets a disjoint set of bots; LFG may still merge/split them differently once
    // matchmaking actually runs, same as it could for any organically-queued groups.
    // Returns a human-readable result string (what was queued, or the specific reason nothing was).
    std::string TriggerTargetedTest(Player* master, uint32 lfgId, uint32 groups = 1);

    // How many AutoCanary-origin sessions (CanaryTick()-spotted or TriggerTargetedTest()/
    // RunTestParty()-started, no matter which) are active right now - the live count
    // CanaryMaxConcurrent is checked against. Exposed so every caller that needs to respect that
    // one shared budget (DungeonTestBotPool::RunTestParty() included) reads the same number
    // instead of keeping its own possibly-divergent count.
    uint32 ActiveCanaryCount();

    // The single-bot primitive TriggerTargetedTest() builds its parties out of - exposed
    // separately for DungeonTestBotPool (ADR-003), which already knows exactly which bots it
    // wants grouped (its own leased Tank/Healer/Dps identities) and just needs them queued for the
    // same dungeon so real LFG matchmaking puts them together. `roleMask` is a PLAYER_ROLE_* value
    // (lfg::PLAYER_ROLE_TANK/HEALER/DAMAGE). Same CMSG_LFG_JOIN path, same safety properties (does
    // not bypass CanaryEnabled/CanaryAllowedLfgIds - the resulting group still needs CanaryTick()
    // to pick it up and start a session, same as any other targeted-test party).
    void QueueBotForLfg(PlayerbotAI* botAI, Player* bot, uint32 lfgId, uint32 roleMask);
}

#endif
