/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadKernels.h
 *
 * Pure decision functions: plain data in, decision out, no AzerothCore/mod-playerbots types. The
 * game-facing code gathers the facts and acts on the result; these decide. Kept header-only and
 * dependency-free so tests/ can compile and run them without a worldserver.
 */

#ifndef MOD_DUNGEONLEAD_KERNELS_H
#define MOD_DUNGEONLEAD_KERNELS_H

#include <cstdint>
#include <vector>

namespace DungeonLeadKernel
{
    // ---------------------------------------------------------------------------------------
    // Leadership transfer (acquire at session start, return at session stop)
    // ---------------------------------------------------------------------------------------
    //
    // Queueing a GroupSetLeaderOperation is not success - it runs later on the world thread and
    // can be dropped (queue full) or overtaken by someone else changing the leader first. The
    // only success signal is observing group->GetLeaderGUID() == the expected leader.

    enum class LeadershipStep : uint8_t
    {
        Confirmed,  // expected leader observed
        Wait,       // request still within its timeout
        Retry,      // timed out, attempts left - request again
        GiveUp,     // timed out, no attempts left
        Abandon,    // can no longer succeed (target gone, or a third party took the lead)
    };

    struct LeadershipObservation
    {
        bool targetIsLeader = false;       // GetLeaderGUID() == expected leader
        bool targetEligible = true;        // expected leader still exists and is in the group
        bool thirdPartyIsLeader = false;   // leader is neither the expected one nor the one we're moving away from
        uint32_t msSinceRequest = 0;       // since the last request was sent
        uint8_t attempts = 0;              // requests sent so far, including the first
    };

    struct LeadershipPolicy
    {
        uint32_t timeoutMs = 5000;  // per attempt
        uint8_t maxAttempts = 3;
    };

    inline LeadershipStep DecideLeadership(LeadershipObservation const& o, LeadershipPolicy const& p)
    {
        if (o.targetIsLeader)
            return LeadershipStep::Confirmed;
        if (!o.targetEligible || o.thirdPartyIsLeader)
            return LeadershipStep::Abandon;
        if (o.msSinceRequest < p.timeoutMs)
            return LeadershipStep::Wait;
        if (o.attempts < p.maxAttempts)
            return LeadershipStep::Retry;
        return LeadershipStep::GiveUp;
    }

    // ---------------------------------------------------------------------------------------
    // Healer availability
    // ---------------------------------------------------------------------------------------
    //
    // Distinct from low mana (HealerManaLow): a dead/ghost healer or one on another map cannot
    // support the next pull at all. A party with no healer role is not blocked - it advances
    // opportunistically, same as before this check existed.

    enum class HealerAvailability : uint8_t
    {
        NoHealerRole,
        Available,
        Unavailable,
    };

    struct MemberFacts
    {
        bool isHealer = false;
        bool alive = false;    // false for dead and ghost alike (both are DeathState::Dead in AC)
        bool sameMap = false;  // same map object as the leader
    };

    inline HealerAvailability EvaluateHealer(std::vector<MemberFacts> const& members)
    {
        bool healerInRoster = false;
        for (MemberFacts const& m : members)
        {
            if (!m.isHealer)
                continue;
            healerInRoster = true;
            if (m.alive && m.sameMap)
                return HealerAvailability::Available;
        }
        return healerInRoster ? HealerAvailability::Unavailable : HealerAvailability::NoHealerRole;
    }
}

#endif
