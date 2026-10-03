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
    // Distinct from low mana (ReadyStatus::LowHealerMana): a dead/ghost healer or one on another map cannot
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

    // ---------------------------------------------------------------------------------------
    // Party readiness (the one place that decides whether the leader may walk on or pull)
    // ---------------------------------------------------------------------------------------
    //
    // DungeonPartyState gathers these facts from the live group; EvaluateReadiness decides.

    struct PartyMemberFacts
    {
        bool isSelf = false;          // the leader itself
        bool isMaster = false;        // the real player the leader belongs to
        bool isHealerBySpec = false;  // talent spec says healer (availability check)
        bool isHealerRole = false;    // mod-playerbots' default healer test (mana check)
        bool gameMaster = false;
        bool alive = false;           // false for dead and ghost alike
        bool sameMap = false;         // same map object as the leader
        bool inCombat = false;
        bool sitting = false;         // eating/drinking (bots) or sitting (players)
        float manaPct = 100.0f;
        float distance = 0.0f;        // to the leader, only meaningful when sameMap
    };

    struct MasterFacts
    {
        bool assigned = false;  // a real player owns this leader (not a bot-only/canary run)
        bool online = false;    // in world with a session
        bool alive = false;
        bool inGroup = true;    // still a member (true when the leader has no group at all)
        bool sameMap = false;
        float distance = 0.0f;
    };

    struct PartyFacts
    {
        bool hasGroup = false;
        bool selfInCombat = false;  // used when there is no group
        MasterFacts master;
        std::vector<PartyMemberFacts> members;  // whole group, leader included
    };

    struct ReadinessPolicy
    {
        float healerManaPct = 20.0f;
        float leash = 60.0f;          // master farther than this (or off-map) -> wait
        float spreadFactor = 1.5f;    // another member farther than leash * this -> wait
    };

    enum class ReadyPurpose : uint8_t
    {
        Walk,  // advance along the route
        Pull,  // open a new fight; being in combat already is the class AI's business
    };

    // In evaluation order - the first one that applies is reported.
    enum class ReadyStatus : uint8_t
    {
        Ready,
        MasterUnavailable,  // dead, offline or left the group
        HealerUnavailable,  // every healer dead/ghost or on another map
        MemberDead,         // another party member dead
        PartyInCombat,      // walk only
        Drinking,           // someone (not the leader) sitting
        LowHealerMana,
        MasterTooFar,       // off-map or beyond leash
        Fragmented,         // another member beyond leash * spreadFactor
    };

    inline char const* ToString(ReadyStatus s)
    {
        switch (s)
        {
            case ReadyStatus::Ready:             return "ready";
            case ReadyStatus::MasterUnavailable: return "master dead/disconnected/left the party";
            case ReadyStatus::HealerUnavailable: return "healer dead or not in the instance";
            case ReadyStatus::MemberDead:        return "party member dead";
            case ReadyStatus::PartyInCombat:     return "group in combat";
            case ReadyStatus::Drinking:          return "someone is eating/drinking";
            case ReadyStatus::LowHealerMana:     return "healer low on mana";
            case ReadyStatus::MasterTooFar:      return "waiting for you, master too far";
            case ReadyStatus::Fragmented:        return "group too spread";
        }
        return "unknown";
    }

    struct Readiness
    {
        ReadyStatus status = ReadyStatus::Ready;
        int offender = -1;  // index into PartyFacts::members for MemberDead/Fragmented, else -1
    };

    inline bool MasterUnavailable(PartyFacts const& f)
    {
        MasterFacts const& m = f.master;
        return m.assigned && (!m.online || !m.alive || !m.inGroup);
    }

    inline bool MasterTooFar(PartyFacts const& f, ReadinessPolicy const& p)
    {
        MasterFacts const& m = f.master;
        if (!m.assigned || !m.alive)
            return false;
        return !m.sameMap || m.distance > p.leash;
    }

    // Farthest member (not the leader) past leash * spreadFactor, or -1.
    inline int FindSpreadMember(PartyFacts const& f, ReadinessPolicy const& p)
    {
        int worst = -1;
        float worstDist = p.leash * p.spreadFactor;
        for (size_t i = 0; i < f.members.size(); ++i)
        {
            PartyMemberFacts const& m = f.members[i];
            if (m.isSelf || !m.alive || !m.sameMap)
                continue;
            if (m.distance > worstDist)
            {
                worstDist = m.distance;
                worst = int(i);
            }
        }
        return worst;
    }

    inline bool AnyInCombat(PartyFacts const& f)
    {
        if (!f.hasGroup)
            return f.selfInCombat;
        for (PartyMemberFacts const& m : f.members)
            if (m.alive && m.sameMap && m.inCombat)
                return true;
        return false;
    }

    inline Readiness EvaluateReadiness(PartyFacts const& f, ReadinessPolicy const& p, ReadyPurpose purpose)
    {
        if (MasterUnavailable(f))
            return {ReadyStatus::MasterUnavailable, -1};
        if (!f.hasGroup)
            return {purpose == ReadyPurpose::Walk && f.selfInCombat ? ReadyStatus::PartyInCombat : ReadyStatus::Ready, -1};

        std::vector<MemberFacts> healerFacts;
        for (PartyMemberFacts const& m : f.members)
        {
            MemberFacts h;
            h.isHealer = m.isHealerBySpec;
            h.alive = m.alive;
            h.sameMap = m.sameMap;
            healerFacts.push_back(h);
        }
        if (EvaluateHealer(healerFacts) == HealerAvailability::Unavailable)
            return {ReadyStatus::HealerUnavailable, -1};

        for (size_t i = 0; i < f.members.size(); ++i)
        {
            PartyMemberFacts const& m = f.members[i];
            if (!m.isSelf && !m.isMaster && !m.alive)
                return {ReadyStatus::MemberDead, int(i)};  // leader's own death and the master's are handled elsewhere
        }

        if (purpose == ReadyPurpose::Walk && AnyInCombat(f))
            return {ReadyStatus::PartyInCombat, -1};

        float lowestHealerMana = 100.0f;
        for (PartyMemberFacts const& m : f.members)
        {
            if (m.isSelf || !m.alive)
                continue;
            if (m.sitting && m.sameMap)
                return {ReadyStatus::Drinking, -1};
            if (m.isHealerRole && !m.gameMaster && m.manaPct < lowestHealerMana)
                lowestHealerMana = m.manaPct;
        }
        if (lowestHealerMana < p.healerManaPct)
            return {ReadyStatus::LowHealerMana, -1};

        if (MasterTooFar(f, p))
            return {ReadyStatus::MasterTooFar, -1};
        int const spread = FindSpreadMember(f, p);
        if (spread >= 0)
            return {ReadyStatus::Fragmented, spread};
        return {};
    }
}

#endif
