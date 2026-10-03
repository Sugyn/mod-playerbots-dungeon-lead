// Unit tests for src/DungeonLead/DungeonLeadKernels.h - no worldserver needed.
// Run: tools/run_tests.sh

#include "DungeonLeadKernels.h"
#include "DungeonRouteTypes.h"

#include <cstdio>
#include <string>

using namespace DungeonLeadKernel;

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void Check(bool ok, char const* what)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            std::printf("FAIL: %s\n", what);
        }
    }

    LeadershipPolicy const kPolicy{5000, 3};

    LeadershipObservation Obs(bool targetIsLeader, uint32_t ms, uint8_t attempts)
    {
        LeadershipObservation o;
        o.targetIsLeader = targetIsLeader;
        o.msSinceRequest = ms;
        o.attempts = attempts;
        return o;
    }

    void TestLeadership()
    {
        // tank already leader / transfer landed
        Check(DecideLeadership(Obs(true, 0, 1), kPolicy) == LeadershipStep::Confirmed, "transfer observed -> Confirmed");
        Check(DecideLeadership(Obs(true, 99999, 3), kPolicy) == LeadershipStep::Confirmed,
              "observed even after timeout -> Confirmed (late success still counts)");

        // queued, not yet observed
        Check(DecideLeadership(Obs(false, 0, 1), kPolicy) == LeadershipStep::Wait, "just requested -> Wait");
        Check(DecideLeadership(Obs(false, 4999, 1), kPolicy) == LeadershipStep::Wait, "inside timeout -> Wait");

        // queued transfer never confirms (or the enqueue itself failed)
        Check(DecideLeadership(Obs(false, 5000, 1), kPolicy) == LeadershipStep::Retry, "timeout, 1/3 -> Retry");
        Check(DecideLeadership(Obs(false, 5000, 2), kPolicy) == LeadershipStep::Retry, "timeout, 2/3 -> Retry");
        Check(DecideLeadership(Obs(false, 5000, 3), kPolicy) == LeadershipStep::GiveUp, "timeout, 3/3 -> GiveUp");

        // retry succeeds
        Check(DecideLeadership(Obs(true, 100, 2), kPolicy) == LeadershipStep::Confirmed, "second attempt lands -> Confirmed");

        // can no longer succeed
        LeadershipObservation gone = Obs(false, 0, 1);
        gone.targetEligible = false;
        Check(DecideLeadership(gone, kPolicy) == LeadershipStep::Abandon, "target left the group -> Abandon");
        LeadershipObservation contested = Obs(false, 0, 1);
        contested.thirdPartyIsLeader = true;
        Check(DecideLeadership(contested, kPolicy) == LeadershipStep::Abandon, "third party took the lead -> Abandon");

        // handback (same decision, target = original leader). A failed enqueue is counted as an
        // attempt and looks exactly like a request that was never observed: wait, then retry.
        Check(DecideLeadership(Obs(false, 100, 1), kPolicy) == LeadershipStep::Wait,
              "handback enqueue failed -> Wait, not an immediate GiveUp");
        Check(DecideLeadership(Obs(false, 5000, 1), kPolicy) == LeadershipStep::Retry,
              "handback enqueue failed, timeout -> Retry");
        Check(DecideLeadership(Obs(true, 300, 2), kPolicy) == LeadershipStep::Confirmed,
              "handback retry succeeds -> Confirmed");
        LeadershipObservation masterLeft = Obs(false, 0, 1);
        masterLeft.targetEligible = false;
        Check(DecideLeadership(masterLeft, kPolicy) == LeadershipStep::Abandon,
              "handback target left/disconnected -> Abandon");

        // bounded: a single attempt policy gives up after its first timeout
        Check(DecideLeadership(Obs(false, 5000, 1), LeadershipPolicy{5000, 1}) == LeadershipStep::GiveUp,
              "maxAttempts=1 -> GiveUp after first timeout");
    }

    MemberFacts M(bool isHealer, bool alive, bool sameMap)
    {
        MemberFacts m;
        m.isHealer = isHealer;
        m.alive = alive;
        m.sameMap = sameMap;
        return m;
    }

    void TestHealer()
    {
        MemberFacts const tank = M(false, true, true);
        MemberFacts const dps = M(false, true, true);

        Check(EvaluateHealer({tank, M(true, true, true), dps}) == HealerAvailability::Available,
              "healer alive and nearby -> Available");
        Check(EvaluateHealer({tank, M(true, false, true), dps}) == HealerAvailability::Unavailable,
              "healer dead -> Unavailable");
        // a ghost is DeathState::Dead in AzerothCore, i.e. alive=false - and usually on the
        // graveyard's map too
        Check(EvaluateHealer({tank, M(true, false, false), dps}) == HealerAvailability::Unavailable,
              "healer ghost -> Unavailable");
        Check(EvaluateHealer({tank, M(true, true, false), dps}) == HealerAvailability::Unavailable,
              "healer on another map -> Unavailable");
        // low mana is HealerManaLow()'s job, not an availability failure
        Check(EvaluateHealer({tank, M(true, true, true), dps}) == HealerAvailability::Available,
              "healer low mana -> still Available");
        Check(EvaluateHealer({tank, dps, dps}) == HealerAvailability::NoHealerRole, "no healer role -> NoHealerRole");
        Check(EvaluateHealer({}) == HealerAvailability::NoHealerRole, "empty roster -> NoHealerRole");
        Check(EvaluateHealer({tank, M(true, false, true), M(true, true, true)}) == HealerAvailability::Available,
              "two healers, one dead -> Available");
        Check(EvaluateHealer({M(true, false, true), M(true, true, false)}) == HealerAvailability::Unavailable,
              "two healers, both unusable -> Unavailable");
    }
}

namespace
{
    // leader (tank) + master (player, dps) + healer + 2 dps, all alive, together, rested
    PartyFacts Party()
    {
        PartyFacts f;
        f.hasGroup = true;
        f.master.assigned = true;
        f.master.online = true;
        f.master.alive = true;
        f.master.inGroup = true;
        f.master.sameMap = true;
        f.master.distance = 10.0f;
        for (int i = 0; i < 5; ++i)
        {
            PartyMemberFacts m;
            m.isSelf = i == 0;
            m.isMaster = i == 1;
            m.isHealerBySpec = m.isHealerRole = i == 2;
            m.alive = true;
            m.sameMap = true;
            m.distance = i == 0 ? 0.0f : 10.0f;
            f.members.push_back(m);
        }
        return f;
    }

    ReadinessPolicy const kReady{20.0f, 60.0f, 1.5f};

    ReadyStatus Walk(PartyFacts const& f) { return EvaluateReadiness(f, kReady, ReadyPurpose::Walk).status; }
    ReadyStatus Pull(PartyFacts const& f) { return EvaluateReadiness(f, kReady, ReadyPurpose::Pull).status; }

    void TestReadiness()
    {
        Check(Walk(Party()) == ReadyStatus::Ready, "healthy party -> Ready");

        PartyFacts f = Party();
        f.master.alive = false;
        Check(Walk(f) == ReadyStatus::MasterUnavailable, "master dead -> MasterUnavailable");
        f = Party();
        f.master.online = false;
        Check(Pull(f) == ReadyStatus::MasterUnavailable, "master offline blocks pulls too");
        f = Party();
        f.master.inGroup = false;
        Check(Walk(f) == ReadyStatus::MasterUnavailable, "master left group -> MasterUnavailable");

        f = Party();
        f.members[2].alive = false;
        Check(Walk(f) == ReadyStatus::HealerUnavailable, "healer dead -> HealerUnavailable");
        f = Party();
        f.members[2].sameMap = false;
        Check(Pull(f) == ReadyStatus::HealerUnavailable, "healer on another map blocks pulls");
        f = Party();
        f.members[2].manaPct = 10.0f;
        Check(Walk(f) == ReadyStatus::LowHealerMana, "healer low mana -> LowHealerMana");
        f.members[2].gameMaster = true;
        Check(Walk(f) == ReadyStatus::Ready, "GM healer's mana ignored (as upstream)");

        f = Party();
        f.members[3].alive = false;
        Readiness r = EvaluateReadiness(f, kReady, ReadyPurpose::Walk);
        Check(r.status == ReadyStatus::MemberDead && r.offender == 3, "dps dead -> MemberDead, names who");

        f = Party();
        f.members[4].inCombat = true;
        Check(Walk(f) == ReadyStatus::PartyInCombat, "member in combat stops the walk");
        Check(Pull(f) == ReadyStatus::Ready, "member in combat does not block pulls");
        f.members[4].sameMap = false;
        Check(Walk(f) == ReadyStatus::Ready, "off-map member's combat is not ours");

        f = Party();
        f.members[3].sitting = true;
        Check(Walk(f) == ReadyStatus::Drinking, "member drinking -> Drinking");
        f = Party();
        f.members[0].sitting = true;
        Check(Walk(f) == ReadyStatus::Ready, "leader's own sitting is not a wait");

        f = Party();
        f.master.distance = 61.0f;
        Check(Walk(f) == ReadyStatus::MasterTooFar, "master beyond leash -> MasterTooFar");
        f = Party();
        f.master.sameMap = false;
        Check(Pull(f) == ReadyStatus::MasterTooFar, "master off-map blocks pulls");

        f = Party();
        f.members[4].distance = 95.0f;
        r = EvaluateReadiness(f, kReady, ReadyPurpose::Walk);
        Check(r.status == ReadyStatus::Fragmented && r.offender == 4, "member beyond leash*1.5 -> Fragmented, names who");
        f.members[4].distance = 89.0f;
        Check(Walk(f) == ReadyStatus::Ready, "member inside leash*1.5 -> Ready");

        // priority: the most fundamental problem is reported first
        f = Party();
        f.master.alive = false;
        f.members[2].alive = false;
        Check(Walk(f) == ReadyStatus::MasterUnavailable, "master problem reported before healer");

        // bot-only (canary) party: no master assigned
        f = Party();
        f.master = MasterFacts();
        f.members[1].isMaster = false;
        Check(Walk(f) == ReadyStatus::Ready, "no master assigned -> Ready");

        // no group at all
        PartyFacts solo;
        solo.selfInCombat = true;
        Check(Walk(solo) == ReadyStatus::PartyInCombat, "solo in combat -> walk waits");
        Check(Pull(solo) == ReadyStatus::Ready, "solo -> pulls allowed");
    }
}

namespace
{
    ActiveFacts Active(LeadState current)
    {
        ActiveFacts f;
        f.current = current;
        f.walkReady = true;
        return f;
    }

    void TestBrain()
    {
        Check(DecideActive(Active(LeadState::WaitingReady)).next == LeadState::Travelling, "ready -> Travelling");
        ActiveFacts f = Active(LeadState::Travelling);
        f.walkReady = false;
        Transition t = DecideActive(f);
        Check(t.next == LeadState::WaitingReady && t.reason == TransitionReason::PartyNotReady,
              "not ready -> WaitingReady (party_not_ready)");
        f = Active(LeadState::Travelling);
        f.anyInCombat = true;
        Check(DecideActive(f).next == LeadState::Combat, "fight starts -> Combat");
        f = Active(LeadState::Combat);
        f.walkReady = false;
        Check(DecideActive(f).next == LeadState::PostCombat, "fight over -> PostCombat");
        f.current = LeadState::PostCombat;
        Check(DecideActive(f).next == LeadState::PostCombat, "PostCombat holds while not ready");
        f.walkReady = true;
        Check(DecideActive(f).next == LeadState::Travelling, "PostCombat -> Travelling once ready");
        f = Active(LeadState::Combat);
        Check(DecideActive(f).next == LeadState::PostCombat, "Combat always passes through PostCombat");

        f = Active(LeadState::Combat);
        f.leaderAlive = false;
        t = DecideActive(f);
        Check(t.next == LeadState::WipeRecovery && t.reason == TransitionReason::LeaderDied, "leader dies -> WipeRecovery");
        f.current = LeadState::WipeRecovery;
        Check(DecideActive(f).next == LeadState::WipeRecovery, "still dead -> stays WipeRecovery");
        f.leaderAlive = true;
        f.anyInCombat = true;
        t = DecideActive(f);
        Check(t.next == LeadState::WaitingReady && t.reason == TransitionReason::LeaderRecovered,
              "resurrected -> WaitingReady first, even if combat is around");

        f = Active(LeadState::Travelling);
        f.routeComplete = true;
        Check(DecideActive(f).next == LeadState::Completing, "route done -> Completing");
        f.current = LeadState::Completing;
        f.routeComplete = false;
        Check(DecideActive(f).next == LeadState::Travelling, "route reset -> back to walking");

        f = Active(LeadState::Travelling);
        f.paused = true;
        t = DecideActive(f);
        Check(t.next == LeadState::WaitingReady && t.reason == TransitionReason::Paused, "paused -> WaitingReady");
        f.anyInCombat = true;
        Check(DecideActive(f).next == LeadState::Combat, "combat still tracked while paused");

        Check(!IsActive(LeadState::Starting) && !IsActive(LeadState::Stopping) && IsActive(LeadState::Combat),
              "Starting/Stopping are not active states");
    }
}

namespace
{
    void TestRouteTypes()
    {
        Check(ParseRouteKind("boss") == DungeonRouteKind::Boss && ParseRouteKind("door") == DungeonRouteKind::Door,
              "existing kinds still parse");
        Check(ParseRouteKind("Boss") == DungeonRouteKind::Unknown, "typo/case -> Unknown");
        Check(ClassifyRouteStep(DungeonRouteKind::Boss, 3653) == DungeonRouteNodeType::Boss, "boss row -> Boss");
        Check(ClassifyRouteStep(DungeonRouteKind::HeroicOnly, 1) == DungeonRouteNodeType::Travel,
              "anchor entry wins over kind");
        Check(ClassifyRouteStep(DungeonRouteKind::HeroicOnly, 500) == DungeonRouteNodeType::Boss, "heroic-only boss -> Boss");
        Check(ClassifyRouteStep(DungeonRouteKind::Optional, 1) == DungeonRouteNodeType::Travel,
              "optional path anchor -> Travel (not skippable)");
        Check(ClassifyRouteStep(DungeonRouteKind::Optional, 3654) == DungeonRouteNodeType::Pull, "optional mob -> Pull");
        Check(ClassifyRouteStep(DungeonRouteKind::Event, 42) == DungeonRouteNodeType::Interaction, "event -> Interaction");
        Check(ClassifyRouteStep(DungeonRouteKind::Door, 42) == DungeonRouteNodeType::Door, "door -> Door");
        Check(std::string(ToString(DungeonRouteNodeType::TankPosition)) == "tank_position", "node type names");
    }
}

namespace
{
    PackObservation Seen(uint32_t found, uint32_t alive, uint32_t engaged)
    {
        PackObservation o;
        o.found = found;
        o.alive = alive;
        o.engaged = engaged;
        return o;
    }

    void TestPack()
    {
        Check(DecidePackState(PackState::Unknown, Seen(0, 0, 0)) == PackState::Unknown, "nothing seen -> Unknown");
        Check(DecidePackState(PackState::Unknown, Seen(3, 3, 0)) == PackState::Available, "alive, idle -> Available");
        Check(DecidePackState(PackState::Available, Seen(3, 3, 1)) == PackState::Engaged, "one in combat -> Engaged");
        Check(DecidePackState(PackState::Engaged, Seen(3, 1, 1)) == PackState::Engaged, "partly dead, fighting -> Engaged");
        Check(DecidePackState(PackState::Engaged, Seen(3, 0, 0)) == PackState::Cleared, "only corpses -> Cleared");
        Check(DecidePackState(PackState::Engaged, Seen(0, 0, 0)) == PackState::Engaged,
              "engaged pack pulled out of probe range stays Engaged");
        Check(DecidePackState(PackState::Available, Seen(0, 0, 0)) == PackState::Unknown, "available pack gone -> Unknown");
        PackObservation remembered = Seen(0, 0, 0);
        remembered.rememberedKilled = true;
        Check(DecidePackState(PackState::Unknown, remembered) == PackState::Cleared, "instance kill memory -> Cleared");
        Check(DecidePackState(PackState::Cleared, Seen(3, 3, 3)) == PackState::Cleared, "Cleared is terminal (respawn ignored)");
        Check(DecidePackState(PackState::Skipped, Seen(0, 0, 0)) == PackState::Skipped, "Skipped is terminal");
        Check(DecidePackState(PackState::Available, Seen(1, 1, 0)) == PackState::Available,
              "reaching the spot alone does not clear a live pack");
    }
}

namespace
{
    PullFacts Pull(PullState current)
    {
        PullFacts f;
        f.current = current;
        f.pullablePack = true;
        f.packAlive = true;
        f.inPullRange = true;
        f.partyReady = true;
        return f;
    }

    PullPolicy const kPull{5000, 8000, 2};

    void TestPull()
    {
        PullFacts f = Pull(PullState::None);
        f.inPullRange = false;
        Check(DecidePull(f, kPull) == PullState::Approaching, "pack out of range -> Approaching");
        f = Pull(PullState::Approaching);
        f.partyReady = false;
        Check(DecidePull(f, kPull) == PullState::WaitingParty, "in range, party not ready -> WaitingParty");
        f.partyReady = true;
        Check(DecidePull(f, kPull) == PullState::Marking, "in range, ready -> Marking");
        f = Pull(PullState::Marking);
        Check(DecidePull(f, kPull) == PullState::Marking, "not marked yet -> stays Marking");
        f.targetMarked = true;
        Check(DecidePull(f, kPull) == PullState::Initiating, "marked -> Initiating");
        f = Pull(PullState::Marking);
        f.msInState = 5000;
        Check(DecidePull(f, kPull) == PullState::Failed, "skull held elsewhere past timeout -> Failed");

        f = Pull(PullState::Initiating);
        f.msInState = 2000;
        Check(DecidePull(f, kPull) == PullState::Initiating, "attack ordered, waiting -> Initiating");
        f.leaderInCombat = true;
        Check(DecidePull(f, kPull) == PullState::Establishing, "tank in combat -> Establishing");
        f.packEngaged = true;
        Check(DecidePull(f, kPull) == PullState::Established, "pack engaged on fighting tank -> Established");
        f = Pull(PullState::Initiating);
        f.msInState = 5000;
        Check(DecidePull(f, kPull) == PullState::Failed, "no combat within initiate timeout -> Failed");
        f = Pull(PullState::Initiating);
        f.orderRefused = true;
        Check(DecidePull(f, kPull) == PullState::Failed, "attack order refused (e.g. no line of sight) -> Failed at once");
        f.packEngaged = true;
        Check(DecidePull(f, kPull) == PullState::Establishing, "refused, but the pack engaged anyway -> Establishing");
        f = Pull(PullState::Establishing);
        f.leaderInCombat = true;
        f.msInState = 8000;
        Check(DecidePull(f, kPull) == PullState::Failed, "pack never engaged within establish timeout -> Failed");

        f = Pull(PullState::Established);
        Check(DecidePull(f, kPull) == PullState::Failed, "pack alive, nobody fighting -> evade -> Failed");
        f.packAlive = false;
        Check(DecidePull(f, kPull) == PullState::None, "pack dead after established -> None");

        f = Pull(PullState::Failed);
        f.attempts = 1;
        Check(DecidePull(f, kPull) == PullState::Approaching, "failed, attempts left -> retry");
        f.attempts = 2;
        Check(DecidePull(f, kPull) == PullState::Failed, "failed, out of attempts -> stays Failed (pack gets skipped)");

        f = Pull(PullState::Approaching);
        f.inPullRange = false;
        f.packEngaged = true;
        f.leaderInCombat = true;
        Check(DecidePull(f, kPull) == PullState::Established, "class AI opened the fight itself -> Established");

        f = Pull(PullState::Initiating);
        f.pullablePack = false;
        Check(DecidePull(f, kPull) == PullState::None, "pack cleared/skipped -> None");

        // brain: pull phases
        ActiveFacts a = Active(LeadState::Travelling);
        a.preparingPull = true;
        Check(DecideActive(a).next == LeadState::PrePull, "marking -> PrePull");
        a.pulling = true;
        Check(DecideActive(a).next == LeadState::Pulling, "attack ordered -> Pulling");
        a.walkReady = false;
        Check(DecideActive(a).next == LeadState::Pulling, "pull in flight is not aborted by a readiness blip");
        a.anyInCombat = true;
        Check(DecideActive(a).next == LeadState::Combat, "fight started -> Combat");
    }
}

int main()
{
    TestLeadership();
    TestHealer();
    TestReadiness();
    TestBrain();
    TestRouteTypes();
    TestPack();
    TestPull();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
