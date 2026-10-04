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
        bool online = true;           // in world with a session
        bool sameMap = false;         // same map object as the leader
        bool inCombat = false;
        bool sitting = false;         // eating/drinking (bots) or sitting (players)
        float manaPct = 100.0f;
        float healthPct = 100.0f;
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
        float softRange = 40.0f;      // a member farther than this -> no new pull (PartySoftRange)
        float hardRange = 90.0f;      // a member farther than this -> stop walking too (PartyHardRange)
        float minHealthPct = 50.0f;   // out of combat, anyone below this -> wait (0 = off)
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
        LowHealth,          // someone (leader included) below minHealthPct, out of combat
        LowHealerMana,
        MemberLost,         // a living member not on the leader's map, or offline
        MasterTooFar,       // off-map or beyond leash
        Fragmented,         // a member beyond PartyHardRange
        PartySpread,        // a member beyond PartySoftRange - pulls only
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
            case ReadyStatus::LowHealth:         return "someone is low on health";
            case ReadyStatus::LowHealerMana:     return "healer low on mana";
            case ReadyStatus::MasterTooFar:      return "waiting for you, master too far";
            case ReadyStatus::MemberLost:        return "party member lost (other map or offline)";
            case ReadyStatus::Fragmented:        return "group too spread";
            case ReadyStatus::PartySpread:       return "group spread out, holding the pull";
        }
        return "unknown";
    }

    struct Readiness
    {
        ReadyStatus status = ReadyStatus::Ready;
        int offender = -1;  // index into PartyFacts::members for MemberDead/MemberLost/Fragmented/PartySpread
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

    // ---------------------------------------------------------------------------------------
    // Party cohesion: how far apart the party is, as one semantic level
    // ---------------------------------------------------------------------------------------

    enum class Cohesion : uint8_t
    {
        Ok,
        SoftWarning,  // someone beyond softRange: finish what we're doing, don't open a new pull
        HardStop,     // someone beyond hardRange: stop advancing
        LostMember,   // a living member not on our map / offline: needs a regroup
    };

    inline char const* ToString(Cohesion c)
    {
        switch (c)
        {
            case Cohesion::Ok:          return "ok";
            case Cohesion::SoftWarning: return "soft_warning";
            case Cohesion::HardStop:    return "hard_stop";
            case Cohesion::LostMember:  return "lost_member";
        }
        return "unknown";
    }

    struct CohesionResult
    {
        Cohesion level = Cohesion::Ok;
        int offender = -1;  // the member that sets the level (farthest one for soft/hard)
    };

    // The master's own distance/presence is judged by MasterTooFar/MasterUnavailable; it still
    // counts toward soft/hard spread like everyone else.
    inline CohesionResult EvaluateCohesion(PartyFacts const& f, ReadinessPolicy const& p)
    {
        CohesionResult r;
        float worstDist = 0.0f;
        for (size_t i = 0; i < f.members.size(); ++i)
        {
            PartyMemberFacts const& m = f.members[i];
            if (m.isSelf || !m.alive)
                continue;
            if (!m.isMaster && (!m.online || !m.sameMap))
                return {Cohesion::LostMember, int(i)};
            if (!m.sameMap)
                continue;
            Cohesion const level = m.distance > p.hardRange ? Cohesion::HardStop
                                 : m.distance > p.softRange ? Cohesion::SoftWarning
                                                            : Cohesion::Ok;
            if (level > r.level || (level == r.level && level != Cohesion::Ok && m.distance > worstDist))
            {
                r.level = level;
                r.offender = int(i);
                worstDist = m.distance;
            }
        }
        return r;
    }

    // Farthest member (not the leader) past hardRange, or -1 - named in the "waiting for X" chat line.
    inline int FindSpreadMember(PartyFacts const& f, ReadinessPolicy const& p)
    {
        int worst = -1;
        float worstDist = p.hardRange;
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

        CohesionResult const cohesion = EvaluateCohesion(f, p);
        if (cohesion.level == Cohesion::LostMember)
            return {ReadyStatus::MemberLost, cohesion.offender};

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
        for (size_t i = 0; i < f.members.size(); ++i)
        {
            PartyMemberFacts const& m = f.members[i];
            if (m.alive && m.sameMap && !m.inCombat && m.healthPct < p.minHealthPct)
                return {ReadyStatus::LowHealth, int(i)};
        }
        if (lowestHealerMana < p.healerManaPct)
            return {ReadyStatus::LowHealerMana, -1};

        if (MasterTooFar(f, p))
            return {ReadyStatus::MasterTooFar, -1};
        if (cohesion.level == Cohesion::HardStop)
            return {ReadyStatus::Fragmented, cohesion.offender};
        if (cohesion.level == Cohesion::SoftWarning && purpose == ReadyPurpose::Pull)
            return {ReadyStatus::PartySpread, cohesion.offender};
        return {};
    }

    // ---------------------------------------------------------------------------------------
    // Leader state machine (DungeonLeadBrain) - the one authoritative state of a session
    // ---------------------------------------------------------------------------------------

    enum class LeadState : uint8_t
    {
        Starting,      // leadership requested, not yet observed; nothing applied
        WaitingReady,  // party not ready (or paused) - hold position, no new pulls
        Travelling,    // party ready, walking the route
        PrePull,       // in range of the pack, preparing the pull (marking)
        Pulling,       // attack ordered, fight not established yet
        BossPrep,      // the current pack is a boss: preparing / opening the pull
        Combat,        // someone in the party is fighting
        BossCombat,    // fighting the current boss (its own anchor and leash)
        PostCombat,    // fight over, party not ready yet
        WipeRecovery,  // leader dead - waiting for upstream's release/corpse-run/res chain
        Recovery,      // a bounded recovery is running (DungeonRecoveryController)
        Completing,    // route finished, session held until stopped
        Stopping,      // strategies restored, leader handback not yet observed
    };

    inline char const* ToString(LeadState s)
    {
        switch (s)
        {
            case LeadState::Starting:     return "starting";
            case LeadState::WaitingReady: return "waiting_ready";
            case LeadState::Travelling:   return "travelling";
            case LeadState::PrePull:      return "pre_pull";
            case LeadState::Pulling:      return "pulling";
            case LeadState::BossPrep:     return "boss_prep";
            case LeadState::Combat:       return "combat";
            case LeadState::BossCombat:   return "boss_combat";
            case LeadState::PostCombat:   return "post_combat";
            case LeadState::WipeRecovery: return "wipe_recovery";
            case LeadState::Recovery:     return "recovery";
            case LeadState::Completing:   return "completing";
            case LeadState::Stopping:     return "stopping";
        }
        return "unknown";
    }

    // Starting and Stopping are driven by the leadership reconciliation, everything else by
    // DecideActive below.
    inline bool IsActive(LeadState s) { return s != LeadState::Starting && s != LeadState::Stopping; }

    // Whether the brain's current state allows a fight to be opened (grind's opportunistic pulls
    // and the pull controller's own): only while working the route or already fighting - never
    // while waiting, recovering, after a wipe, done, or outside an active session.
    inline bool StateAllowsNewPull(LeadState s)
    {
        switch (s)
        {
            case LeadState::Travelling:
            case LeadState::PrePull:
            case LeadState::Pulling:
            case LeadState::BossPrep:
            case LeadState::Combat:
            case LeadState::BossCombat:
                return true;
            default:
                return false;
        }
    }

    enum class TransitionReason : uint8_t
    {
        SessionStart,
        LeadershipConfirmed,
        StopRequested,
        Paused,
        PartyNotReady,
        PartyReady,
        CombatStarted,
        CombatEnded,
        LeaderDied,
        LeaderRecovered,
        RouteComplete,
        PullPreparing,
        PullStarted,
        RecoveryNeeded,
        BossEngaged,
    };

    inline char const* ToString(TransitionReason r)
    {
        switch (r)
        {
            case TransitionReason::SessionStart:        return "session_start";
            case TransitionReason::LeadershipConfirmed: return "leadership_confirmed";
            case TransitionReason::StopRequested:       return "stop_requested";
            case TransitionReason::Paused:              return "paused";
            case TransitionReason::PartyNotReady:       return "party_not_ready";
            case TransitionReason::PartyReady:          return "party_ready";
            case TransitionReason::CombatStarted:       return "combat_started";
            case TransitionReason::CombatEnded:         return "combat_ended";
            case TransitionReason::LeaderDied:          return "leader_died";
            case TransitionReason::LeaderRecovered:     return "leader_recovered";
            case TransitionReason::RouteComplete:       return "route_complete";
            case TransitionReason::PullPreparing:       return "pull_preparing";
            case TransitionReason::PullStarted:         return "pull_started";
            case TransitionReason::RecoveryNeeded:      return "recovery_needed";
            case TransitionReason::BossEngaged:         return "boss_engaged";
        }
        return "unknown";
    }

    struct ActiveFacts
    {
        LeadState current = LeadState::WaitingReady;
        bool leaderAlive = true;
        bool paused = false;
        bool routeComplete = false;
        bool anyInCombat = false;  // leader or a living same-map member
        bool walkReady = false;    // EvaluateReadiness(..., Walk) == Ready
        bool preparingPull = false;  // pull controller: Marking
        bool pulling = false;        // pull controller: Initiating / Establishing
        uint32_t msInState = 0;
        uint32_t postCombatMinMs = 3000;  // shortest pause between two fights
        bool recovering = false;          // DungeonRecoveryController has an open problem
        bool bossPack = false;            // the current pack is a boss
        bool bossEngaged = false;         // ... and it is in the fight
    };

    struct Transition
    {
        LeadState next;
        TransitionReason reason;
    };

    // Next state for an active session. Returns `current` unchanged (with the reason that keeps
    // it there) when nothing changed - the caller only logs actual changes.
    inline Transition DecideActive(ActiveFacts const& f)
    {
        if (!f.leaderAlive)
            return {LeadState::WipeRecovery, TransitionReason::LeaderDied};
        if (f.current == LeadState::WipeRecovery)
            return {LeadState::WaitingReady, TransitionReason::LeaderRecovered};
        if (f.routeComplete)
            return {LeadState::Completing, TransitionReason::RouteComplete};
        if (f.anyInCombat)
        {
            // A boss fight stays a boss fight even if the boss is briefly not seen engaged.
            if (f.bossEngaged || f.current == LeadState::BossCombat)
                return {LeadState::BossCombat, TransitionReason::BossEngaged};
            return {LeadState::Combat, TransitionReason::CombatStarted};
        }
        if (f.current == LeadState::Combat || f.current == LeadState::BossCombat)
            return {LeadState::PostCombat, TransitionReason::CombatEnded};
        // The deliberate gate between two packs: always a pause, then a full readiness check.
        if (f.current == LeadState::PostCombat && f.msInState < f.postCombatMinMs)
            return {LeadState::PostCombat, TransitionReason::CombatEnded};
        if (f.paused)
            return {LeadState::WaitingReady, TransitionReason::Paused};
        if (f.pulling)  // attack already ordered
            return {f.bossPack ? LeadState::BossPrep : LeadState::Pulling, TransitionReason::PullStarted};
        if (f.recovering)
            return {LeadState::Recovery, TransitionReason::RecoveryNeeded};
        if (!f.walkReady)
            return {f.current == LeadState::PostCombat ? LeadState::PostCombat : LeadState::WaitingReady,
                    TransitionReason::PartyNotReady};
        if (f.preparingPull)
            return {f.bossPack ? LeadState::BossPrep : LeadState::PrePull, TransitionReason::PullPreparing};
        return {LeadState::Travelling, TransitionReason::PartyReady};
    }

    // ---------------------------------------------------------------------------------------
    // Pack state (the enemy group a Pull/Boss/Interaction route node is about)
    // ---------------------------------------------------------------------------------------
    //
    // A pack is identified by static data only (expected creature entries near the node's
    // position); runtime GUIDs are observed each tick, never stored as the definition. Route
    // progress depends on the pack becoming Cleared (or explicitly Skipped), never on the tank
    // merely reaching the coordinate.

    enum class PackState : uint8_t
    {
        Unknown,    // nothing seen yet (not in range, or nothing spawned)
        Available,  // alive, not fighting
        Engaged,    // alive and in combat
        Cleared,    // seen dead (or remembered killed in this instance)
        Skipped,    // given up on (stuck / not progressing) - terminal
    };

    inline char const* ToString(PackState s)
    {
        switch (s)
        {
            case PackState::Unknown:   return "unknown";
            case PackState::Available: return "available";
            case PackState::Engaged:   return "engaged";
            case PackState::Cleared:   return "cleared";
            case PackState::Skipped:   return "skipped";
        }
        return "unknown";
    }

    struct PackObservation
    {
        uint32_t found = 0;            // matching creatures near the pack position, dead or alive
        uint32_t alive = 0;
        uint32_t engaged = 0;          // alive and in combat
        bool rememberedKilled = false; // instance kill memory says this pack is done
    };

    inline PackState DecidePackState(PackState prev, PackObservation const& o)
    {
        if (prev == PackState::Cleared || prev == PackState::Skipped)
            return prev;  // terminal
        if (o.rememberedKilled)
            return PackState::Cleared;
        if (o.engaged > 0)
            return PackState::Engaged;
        if (o.alive > 0)
            return PackState::Available;
        if (o.found > 0)
            return PackState::Cleared;  // only corpses left
        // Nothing in range: an engaged pack may just have been pulled out of probe range.
        return prev == PackState::Engaged ? PackState::Engaged : PackState::Unknown;
    }

    // ---------------------------------------------------------------------------------------
    // Combat anchor and leash
    // ---------------------------------------------------------------------------------------
    //
    // When a fight starts the leader's position becomes the combat anchor. While the fight lasts
    // the tank may move freely around it, but it does not chase a target that has left the leash
    // radius around the anchor - a fleeing mob must not drag the party into the next pack.

    struct LeashFacts
    {
        bool anchorSet = false;
        bool inCombat = false;
        bool hasTarget = false;
        float targetDistFromAnchor = 0.0f;
    };

    inline bool ChaseAllowed(LeashFacts const& f, float leashRadius)
    {
        if (!f.anchorSet || !f.inCombat || !f.hasTarget)
            return true;
        return f.targetDistFromAnchor <= leashRadius;
    }

    // ---------------------------------------------------------------------------------------
    // Pull lifecycle (DungeonPullController) for the current Pull/Boss pack
    // ---------------------------------------------------------------------------------------
    //
    // Orchestration only: the controller decides when to open the fight, marks the target and
    // starts the attack through mod-playerbots' own "attack rti target"; the class AI fights.

    enum class PullState : uint8_t
    {
        None,          // no pullable pack (travel node, cleared, interaction)
        Approaching,   // pack known, tank not in pull range yet
        WaitingParty,  // in range, party not ready for a pull
        Marking,       // ready - put the skull on the pull target
        Initiating,    // attack ordered, waiting for the tank to be in combat
        Establishing,  // tank fighting, waiting for the pack to be engaged on it
        Established,   // tank fighting the pack or its planned primary - the class AI owns it now
        Failed,        // order refused / no fight within the timeouts; retried until the budget is spent
    };

    inline char const* ToString(PullState s)
    {
        switch (s)
        {
            case PullState::None:         return "none";
            case PullState::Approaching:  return "approaching";
            case PullState::WaitingParty: return "waiting_party";
            case PullState::Marking:      return "marking";
            case PullState::Initiating:   return "initiating";
            case PullState::Establishing: return "establishing";
            case PullState::Established:  return "established";
            case PullState::Failed:       return "failed";
        }
        return "unknown";
    }

    struct PullFacts
    {
        PullState current = PullState::None;
        uint32_t msInState = 0;
        bool pullablePack = false;  // current node is a Pull/Boss pack that is not cleared/skipped
        bool packAlive = false;     // something of the pack seen alive
        bool packEngaged = false;   // something of the pack alive and in combat
        bool primaryEngaged = false;  // the target plan's primary (may be trash next to the pack) fighting
        bool inPullRange = false;   // tank within PullRange of the pull target
        bool partyReady = false;    // EvaluateReadiness(..., Pull) == Ready
        bool targetMarked = false;  // skull is on a living member of the pack
        bool leaderInCombat = false;
        bool orderRefused = false;  // the attack order for this attempt was not accepted
        uint8_t attempts = 0;       // pulls initiated so far for this pack
    };

    struct PullPolicy
    {
        uint32_t initiateTimeoutMs = 10000;
        uint32_t establishTimeoutMs = 8000;
        uint8_t maxAttempts = 2;
    };

    inline PullState DecidePull(PullFacts const& f, PullPolicy const& p)
    {
        if (!f.pullablePack)
            return PullState::None;
        if (f.current == PullState::Failed)
            return f.attempts < p.maxAttempts ? PullState::Approaching : PullState::Failed;
        if (f.leaderInCombat && (f.packEngaged || f.primaryEngaged))
            return PullState::Established;  // however it started (ours, or the class AI's own)

        switch (f.current)
        {
            case PullState::Established:
                // Fight over. If the pack is still standing that fight was its trash (or it reset):
                // go for it again - a fight that happened is progress, not a failed pull. Repeated
                // fights on one pack are bounded by the caller.
                return f.packAlive ? PullState::Approaching : PullState::None;
            case PullState::Initiating:
                if (f.leaderInCombat || f.packEngaged)
                    return PullState::Establishing;
                if (f.orderRefused)
                    return PullState::Failed;  // nothing is coming of it - don't wait out the timeout
                return f.msInState >= p.initiateTimeoutMs ? PullState::Failed : PullState::Initiating;
            case PullState::Establishing:
                return f.msInState >= p.establishTimeoutMs ? PullState::Failed : PullState::Establishing;
            case PullState::Marking:
                if (!f.partyReady)
                    return PullState::WaitingParty;
                if (f.targetMarked)
                    return PullState::Initiating;
                // the skull is held by someone else's live mark - don't fight over it forever
                return f.msInState >= p.initiateTimeoutMs ? PullState::Failed : PullState::Marking;
            default:  // None, Approaching, WaitingParty
                if (!f.packAlive || !f.inPullRange)
                    return PullState::Approaching;
                return f.partyReady ? PullState::Marking : PullState::WaitingParty;
        }
    }

    // ---------------------------------------------------------------------------------------
    // Target plan (DungeonTargetManager): primary / secondary / CC for the current fight
    // ---------------------------------------------------------------------------------------
    //
    // Deterministic and stable: a planned target keeps its slot for as long as it is a live
    // candidate, so marks don't flicker between equally good choices. Priority: boss, elite
    // caster, caster, elite, normal; ties by distance to the anchor, then id.

    struct TargetCandidate
    {
        uint64_t id = 0;
        bool boss = false;
        bool caster = false;    // mana user
        bool elite = false;
        float distToAnchor = 0.0f;
    };

    struct TargetPlan
    {
        uint64_t primary = 0;    // skull
        uint64_t secondary = 0;  // cross
        uint64_t cc = 0;         // moon - an elite, never the boss or a kill target

        bool operator==(TargetPlan const& o) const
        {
            return primary == o.primary && secondary == o.secondary && cc == o.cc;
        }
    };

    inline int TargetRank(TargetCandidate const& c)
    {
        if (c.boss)
            return 0;
        if (c.caster && c.elite)
            return 1;
        if (c.caster)
            return 2;
        if (c.elite)
            return 3;
        return 4;
    }

    inline bool TargetBefore(TargetCandidate const& a, TargetCandidate const& b)
    {
        int const ra = TargetRank(a), rb = TargetRank(b);
        if (ra != rb)
            return ra < rb;
        if (a.distToAnchor != b.distToAnchor)
            return a.distToAnchor < b.distToAnchor;
        return a.id < b.id;
    }

    inline TargetPlan PickTargetPlan(std::vector<TargetCandidate> const& candidates, TargetPlan const& previous,
                                     bool wantCc)
    {
        auto find = [&](uint64_t id) -> TargetCandidate const*
        {
            for (TargetCandidate const& c : candidates)
                if (c.id == id)
                    return &c;
            return nullptr;
        };
        auto best = [&](auto accept) -> uint64_t
        {
            TargetCandidate const* pick = nullptr;
            for (TargetCandidate const& c : candidates)
                if (accept(c) && (!pick || TargetBefore(c, *pick)))
                    pick = &c;
            return pick ? pick->id : 0;
        };

        TargetPlan plan;
        // CC first among the kept slots: a kept CC target must not be promoted to a kill target.
        TargetCandidate const* keptCc = wantCc && previous.cc ? find(previous.cc) : nullptr;
        if (keptCc && keptCc->elite && !keptCc->boss)
            plan.cc = keptCc->id;

        plan.primary = previous.primary && previous.primary != plan.cc && find(previous.primary)
                           ? previous.primary
                           : best([&](TargetCandidate const& c) { return c.id != plan.cc; });
        plan.secondary = previous.secondary && previous.secondary != plan.primary && previous.secondary != plan.cc &&
                                 find(previous.secondary)
                             ? previous.secondary
                             : best([&](TargetCandidate const& c) { return c.id != plan.primary && c.id != plan.cc; });
        if (wantCc && !plan.cc)
        {
            // Only worth it with more than two enemies: CC the best remaining elite that is
            // neither kill target.
            uint64_t const cc = best([&](TargetCandidate const& c)
                { return c.elite && !c.boss && c.id != plan.primary && c.id != plan.secondary; });
            if (cc && candidates.size() > 2)
                plan.cc = cc;
        }
        return plan;
    }

    // ---------------------------------------------------------------------------------------
    // Recovery (DungeonRecoveryController) - problems that waiting alone may not fix
    // ---------------------------------------------------------------------------------------
    //
    // Every recovery is bounded: act, then escalate, then abort the session with a recorded
    // failure. Each reason gets its own fresh window when the reason changes (a new problem must
    // not inherit an almost-expired timeout), and the episode as a whole - as long as *some*
    // recovery problem persists - has a hard cap, so flapping between reasons can't loop forever.
    // Ordinary waits (drinking, mana, health, the real player's position) are not recoveries.

    enum class RecoveryReason : uint8_t
    {
        None,
        LeadershipLost,   // someone else is group leader now
        MemberLost,       // a living member on another map / offline
        PartyFragmented,  // a member beyond the hard range
        MemberDead,       // a party member (not the leader) dead
    };

    inline char const* ToString(RecoveryReason r)
    {
        switch (r)
        {
            case RecoveryReason::None:            return "none";
            case RecoveryReason::LeadershipLost:  return "leadership_lost";
            case RecoveryReason::MemberLost:      return "member_lost";
            case RecoveryReason::PartyFragmented: return "party_fragmented";
            case RecoveryReason::MemberDead:      return "member_dead";
        }
        return "unknown";
    }

    enum class RecoveryStep : uint8_t
    {
        None,      // nothing to recover
        Act,       // the reason's own action (regroup, wait for a resurrection, ...)
        Escalate,  // stronger action, once (bring a bot member to the leader, ...)
        Abort,     // give up: stop the session, recorded as failed
    };

    inline char const* ToString(RecoveryStep s)
    {
        switch (s)
        {
            case RecoveryStep::None:     return "none";
            case RecoveryStep::Act:      return "act";
            case RecoveryStep::Escalate: return "escalate";
            case RecoveryStep::Abort:    return "abort";
        }
        return "unknown";
    }

    struct RecoveryPolicy
    {
        uint32_t actMs = 60000;       // RecoveryTimeoutSeconds
        uint32_t escalateMs = 60000;  // RecoveryEscalationSeconds
        // Cap for one recovery episode across reason changes: two full windows.
        uint32_t EpisodeCapMs() const { return 2u * (actMs + escalateMs); }
    };

    // Recovery clocks. `reasonSince` restarts whenever the reason changes; `episodeSince` only
    // when a new episode begins (no problem before). Timestamps are getMSTime()-style, compared by
    // wrap-safe differences.
    struct RecoveryTimers
    {
        RecoveryReason reason = RecoveryReason::None;
        RecoveryStep step = RecoveryStep::None;  // last step taken for the current reason
        uint32_t reasonSince = 0;
        uint32_t episodeSince = 0;
    };

    // Feed one observation (only while the controller is eligible to judge - in combat an open
    // recovery is simply kept). Returns true if the reason changed (a new recovery started).
    inline bool ObserveRecovery(RecoveryTimers& t, RecoveryReason observed, uint32_t now)
    {
        if (observed == RecoveryReason::None)
        {
            t = RecoveryTimers();
            return false;
        }
        if (t.reason == RecoveryReason::None)
            t.episodeSince = now;
        if (observed == t.reason)
            return false;
        t.reason = observed;
        t.reasonSince = now;
        t.step = RecoveryStep::None;
        return true;
    }

    inline RecoveryStep DecideRecovery(RecoveryReason observed, uint32_t msInReason, uint32_t msInEpisode,
                                       RecoveryPolicy const& p)
    {
        if (observed == RecoveryReason::None)
            return RecoveryStep::None;
        if (msInEpisode >= p.EpisodeCapMs())
            return RecoveryStep::Abort;  // flapping between reasons: the episode as a whole is bounded
        if (msInReason < p.actMs)
            return RecoveryStep::Act;
        if (msInReason < p.actMs + p.escalateMs)
            return RecoveryStep::Escalate;
        return RecoveryStep::Abort;
    }

    // Which recovery a readiness result calls for (LeadershipLost is observed separately).
    // `healerAlive`: for HealerUnavailable, whether the healer is alive (then it is elsewhere).
    inline RecoveryReason RecoveryFor(ReadyStatus status, bool healerAlive)
    {
        switch (status)
        {
            case ReadyStatus::MemberLost:        return RecoveryReason::MemberLost;
            case ReadyStatus::Fragmented:        return RecoveryReason::PartyFragmented;
            case ReadyStatus::MemberDead:        return RecoveryReason::MemberDead;
            case ReadyStatus::HealerUnavailable: return healerAlive ? RecoveryReason::MemberLost : RecoveryReason::MemberDead;
            default:                             return RecoveryReason::None;
        }
    }

    // ---------------------------------------------------------------------------------------
    // Wipe checkpoints
    // ---------------------------------------------------------------------------------------
    //
    // The checkpoint is the last step confirmed safe: a pack confirmed cleared or a travel node
    // reached - never a skip. After a wipe the route resumes right after it, so nothing that was
    // only passed over (skipped, given up on) before the wipe stays skipped because of it.

    // Step index to resume from after a wipe: the step after the checkpoint (-1 = none yet),
    // never ahead of where the route already is.
    inline uint32_t ResumeStepAfterWipe(int32_t checkpointStep, uint32_t currentStep)
    {
        uint32_t const afterCheckpoint = checkpointStep < 0 ? 0u : uint32_t(checkpointStep) + 1u;
        return afterCheckpoint < currentStep ? afterCheckpoint : currentStep;
    }

    // ---------------------------------------------------------------------------------------
    // Test-party assembly (DungeonTestBotPool): start only once the party is really there
    // ---------------------------------------------------------------------------------------
    //
    // A requested teleport is not an arrival. A test session starts only when every member is
    // confirmed alive, on the dungeon map, in the tank's instance and gathered near the tank.

    struct AssemblyMember
    {
        bool online = false;
        bool alive = false;
        bool onMap = false;         // on the dungeon's map
        bool sameInstance = false;  // in the tank's instance
        float distanceToTank = 0.0f;
    };

    inline bool MemberAssembled(AssemblyMember const& m, float gatherRange)
    {
        return m.online && m.alive && m.onMap && m.sameInstance && m.distanceToTank <= gatherRange;
    }

    enum class AssemblyStep : uint8_t
    {
        Wait,
        Start,
        Abort,  // never assembled within the timeout - don't start a half-there party
    };

    inline AssemblyStep DecideAssembly(std::vector<AssemblyMember> const& members, uint32_t msWaiting,
                                       float gatherRange, uint32_t timeoutMs)
    {
        bool all = !members.empty();
        for (AssemblyMember const& m : members)
            all = all && MemberAssembled(m, gatherRange);
        if (all)
            return AssemblyStep::Start;
        return msWaiting >= timeoutMs ? AssemblyStep::Abort : AssemblyStep::Wait;
    }
}

#endif
