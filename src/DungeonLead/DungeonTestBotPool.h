/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONTESTBOTPOOL_H
#define PLAYERBOTS_DUNGEONTESTBOTPOOL_H

#include "Common.h"

#include <string>

class Player;

// Phase 1 of the "targeted unattended testing" architecture (see
// docs/architecture/adr-003-dungeon-test-bot-pool.md): a small pool of deterministically-specced
// test bots that don't depend on the general bot population's momentary class/spec distribution,
// and don't need a live player session to stay logged in (unlike the rejected "addclass under a
// master" approach - see that ADR for why).
//
// Source of identities: the existing upstream AddClass bot pool (account_type=2 in
// playerbots_account_type, ~50 characters per class, currently idle/offline). Reused as-is - this
// does NOT create new accounts or characters. Login goes through the existing masterless path
// (PlayerbotHolder::AddPlayerBot(guid, 0), the same "isRndbot" branch a real random bot uses),
// never a manually-constructed WorldSession.
//
// Explicitly Phase 1 scope only (per the architecture plan): two roles (Tank, Healer), no lease
// ownership beyond a simple in-memory list, no campaign/orchestrator layer, no concurrency beyond
// what a human operator asks for one lease at a time. Everything here is meant to be provably
// correct at small scale before any of that gets built on top.
namespace DungeonLead
{
    enum class TestBotRole : uint8
    {
        Tank,
        Healer,
    };

    enum class TestBotLeaseState : uint8
    {
        LoggingIn,   // AddPlayerBot() called, waiting for the async login to land
        Preparing,   // logged in, PlayerbotFactory profile prep running (synchronous once reached)
        Ready,       // profile applied, role verified at runtime - safe to hand to a test scenario
        Leased,      // handed out (reserved for future use once a scenario runner exists)
        Failed,      // login timed out, or role verification failed after prep
    };

    // Called every world tick from PlayerbotsWorldScript::OnUpdate, next to GuardActiveSessions()/
    // CanaryTick() - throttles internally (~3s), advances any in-flight lease through
    // LoggingIn -> Preparing -> Ready/Failed, and times out a login that never lands (30s).
    void TestBotPoolTick();

    // Reserves one currently-offline AddClass character of the class matching `role`, triggers
    // its masterless login, and starts tracking it as a lease. Returns immediately - login and
    // profile preparation happen asynchronously via TestBotPoolTick(); check TestBotPoolStatus()
    // for progress. Returns false (with a reason in outMessage) if no eligible offline character
    // exists or all of them are already leased.
    bool AcquireTestBot(TestBotRole role, uint32 targetLevel, std::string& outMessage);

    // Human-readable dump of every currently-tracked lease and its state.
    std::string TestBotPoolStatus();

    // Ends a lease: stops any active dungeon-lead session on the bot (never leaves one dangling),
    // logs it out via the normal LogoutPlayerBot() path, and stops tracking it. `botName` matches
    // on the character name given at Acquire time (case-sensitive, as returned by TestBotPoolStatus).
    std::string ReleaseTestBot(std::string const& botName);
}

#endif
