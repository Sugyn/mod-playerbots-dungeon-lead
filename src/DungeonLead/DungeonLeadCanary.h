/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONLEADCANARY_H
#define PLAYERBOTS_DUNGEONLEADCANARY_H

// AutoBot Canary: an unattended controller that starts a Dungeon Lead session on its own, on
// groups nobody asked it to touch, so a server operator running many bot dungeons at once gets
// telemetry from all of them instead of only the ones a real player happened to say
// "startdungeon" in. See docs/architecture/ in the repo for the full design review; this is a
// deliberately narrow first slice of it (Stage 0/1 in that review's staged-rollout language):
//
//   - Off by default (AiPlayerbot.DungeonLead.CanaryEnabled = false). A fresh checkout of this
//     patch behaves exactly as if this file didn't exist.
//   - Empty allowlist by default (AiPlayerbot.DungeonLead.CanaryAllowedLfgIds = ""): even with
//     CanaryEnabled=true, nothing auto-starts until an operator names specific dungeons.
//   - PASSIVE sampling only: this watches for groups that organically formed (via the normal
//     playerbot LFG queue, exactly like a human party) and already are a pure-bot 5-man in an
//     allowed dungeon - it does not summon, group, or teleport any bot itself. Actively
//     assembling a specific test party for one named dungeon on demand ("targeted test", so an
//     operator isn't stuck waiting for that exact dungeon to come up by chance) is a real,
//     separate, larger feature - it needs its own bot-selection/grouping/summon code and its own
//     review, and is intentionally NOT part of this slice.
//   - Never touches a group with a real player in it, at either the start check or on every
//     subsequent tick (a real player joining mid-session stops the canary session immediately
//     and hands control back).
//   - Bounded: CanaryMaxConcurrent caps how many canary sessions run at once,
//     CanaryTimeoutMinutes force-stops one that never reaches a terminal outcome.
namespace DungeonLead
{
    // Called every world tick from PlayerbotsWorldScript::OnUpdate, right next to
    // GuardActiveSessions() - throttles and no-ops internally exactly like that function does, so
    // the call site stays a single unconditional line regardless of config.
    void CanaryTick();
}

#endif
