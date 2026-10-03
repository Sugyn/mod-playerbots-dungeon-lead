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

    ReadinessPolicy const kReady{20.0f, 60.0f, 40.0f, 90.0f};

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
        Check(!AnyInCombat(f), "off-map member's combat is not ours");

        f = Party();
        f.members[3].sitting = true;
        Check(Walk(f) == ReadyStatus::Drinking, "member drinking -> Drinking");
        f = Party();
        f.members[0].sitting = true;
        Check(Walk(f) == ReadyStatus::Ready, "leader's own sitting is not a wait");

        // post-combat health gate
        f = Party();
        f.members[3].healthPct = 30.0f;
        r = EvaluateReadiness(f, kReady, ReadyPurpose::Walk);
        Check(r.status == ReadyStatus::LowHealth && r.offender == 3, "member below min health -> LowHealth, names who");
        Check(Pull(f) == ReadyStatus::LowHealth, "low health holds pulls too");
        f.members[3].inCombat = true;
        Check(Pull(f) == ReadyStatus::Ready, "health doesn't gate while that member is still fighting");
        f = Party();
        f.members[0].healthPct = 20.0f;
        Check(Walk(f) == ReadyStatus::LowHealth, "the leader's own health counts");
        f = Party();
        f.members[3].healthPct = 30.0f;
        Check(EvaluateReadiness(f, ReadinessPolicy{20.0f, 60.0f, 40.0f, 90.0f, 0.0f}, ReadyPurpose::Walk).status ==
                  ReadyStatus::Ready, "minHealthPct 0 turns the check off");

        f = Party();
        f.master.distance = 61.0f;
        Check(Walk(f) == ReadyStatus::MasterTooFar, "master beyond leash -> MasterTooFar");
        f = Party();
        f.master.sameMap = false;
        Check(Pull(f) == ReadyStatus::MasterTooFar, "master off-map blocks pulls");

        f = Party();
        f.members[4].distance = 95.0f;
        r = EvaluateReadiness(f, kReady, ReadyPurpose::Walk);
        Check(r.status == ReadyStatus::Fragmented && r.offender == 4, "member beyond hard range -> Fragmented, names who");
        f.members[4].distance = 89.0f;
        Check(Walk(f) == ReadyStatus::Ready, "member between soft and hard range -> walking continues");
        r = EvaluateReadiness(f, kReady, ReadyPurpose::Pull);
        Check(r.status == ReadyStatus::PartySpread && r.offender == 4, "member beyond soft range -> no new pull, names who");
        f.members[4].distance = 39.0f;
        Check(Pull(f) == ReadyStatus::Ready, "everyone inside soft range -> pull allowed");

        // cohesion levels
        f = Party();
        Check(EvaluateCohesion(f, kReady).level == Cohesion::Ok, "together -> Ok");
        f.members[3].distance = 50.0f;
        f.members[4].distance = 70.0f;
        CohesionResult c = EvaluateCohesion(f, kReady);
        Check(c.level == Cohesion::SoftWarning && c.offender == 4, "soft warning names the farthest member");
        f.members[3].distance = 100.0f;
        c = EvaluateCohesion(f, kReady);
        Check(c.level == Cohesion::HardStop && c.offender == 3, "hard stop beats soft warning");
        f.members[4].sameMap = false;
        c = EvaluateCohesion(f, kReady);
        Check(c.level == Cohesion::LostMember && c.offender == 4, "living member on another map -> LostMember");
        r = EvaluateReadiness(f, kReady, ReadyPurpose::Walk);
        Check(r.status == ReadyStatus::MemberLost && r.offender == 4, "lost member stops the walk");
        f = Party();
        f.members[3].online = false;
        Check(Walk(f) == ReadyStatus::MemberLost, "offline member -> MemberLost");
        f = Party();
        f.members[4].alive = false;
        f.members[4].sameMap = false;
        Check(Walk(f) == ReadyStatus::MemberDead, "dead member elsewhere is MemberDead, not lost");
        f = Party();
        f.master.sameMap = false;
        f.members[1].sameMap = false;
        Check(Walk(f) == ReadyStatus::MasterTooFar, "master off-map is MasterTooFar, not a lost member");

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
        f.msInState = 5000;
        Check(DecideActive(f).next == LeadState::Travelling, "PostCombat -> Travelling once ready (after the pause)");
        f = Active(LeadState::Combat);
        Check(DecideActive(f).next == LeadState::PostCombat, "Combat always passes through PostCombat");
        f = Active(LeadState::PostCombat);
        f.msInState = 1000;
        Check(DecideActive(f).next == LeadState::PostCombat, "PostCombat holds for its minimum pause even when ready");
        f.msInState = 3000;
        Check(DecideActive(f).next == LeadState::Travelling, "after the pause, ready -> Travelling");
        f.msInState = 1000;
        f.anyInCombat = true;
        Check(DecideActive(f).next == LeadState::Combat, "unexpected combat during the pause -> Combat");

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

        // boss flow: MovingToPull -> BossPrep -> BossCombat -> PostCombat
        f = Active(LeadState::Travelling);
        f.preparingPull = true;
        f.bossPack = true;
        Check(DecideActive(f).next == LeadState::BossPrep, "marking a boss -> BossPrep");
        f.pulling = true;
        Check(DecideActive(f).next == LeadState::BossPrep, "boss pull ordered -> still BossPrep");
        f.anyInCombat = true;
        Check(DecideActive(f).next == LeadState::Combat, "in combat, boss not engaged yet (its trash) -> Combat");
        f.bossEngaged = true;
        t = DecideActive(f);
        Check(t.next == LeadState::BossCombat && t.reason == TransitionReason::BossEngaged, "boss engaged -> BossCombat");
        f.current = LeadState::BossCombat;
        f.bossEngaged = false;
        Check(DecideActive(f).next == LeadState::BossCombat, "boss fight holds while combat lasts");
        f.anyInCombat = false;
        Check(DecideActive(f).next == LeadState::PostCombat, "boss fight over -> PostCombat");
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
        Check(DecidePull(f, kPull) == PullState::Approaching, "fight over, pack still standing -> pull it next, not a failure");
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

        f = Pull(PullState::Establishing);
        f.leaderInCombat = true;
        f.primaryEngaged = true;
        Check(DecidePull(f, kPull) == PullState::Established, "fighting the planned primary (trash next to the boss) -> Established");
        f.primaryEngaged = false;
        f.msInState = 8000;
        Check(DecidePull(f, kPull) == PullState::Failed, "in combat with something unrelated past the timeout -> Failed");

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

namespace
{
    void TestLeash()
    {
        LeashFacts f;
        f.anchorSet = true;
        f.inCombat = true;
        f.hasTarget = true;
        f.targetDistFromAnchor = 20.0f;
        Check(ChaseAllowed(f, 30.0f), "target inside leash -> chase allowed");
        f.targetDistFromAnchor = 45.0f;
        Check(!ChaseAllowed(f, 30.0f), "fleeing target beyond leash -> no chase");
        f.inCombat = false;
        Check(ChaseAllowed(f, 30.0f), "out of combat the leash does not apply");
        f.inCombat = true;
        f.anchorSet = false;
        Check(ChaseAllowed(f, 30.0f), "no anchor (no fight began yet) -> no leash");
        f.anchorSet = true;
        f.hasTarget = false;
        Check(ChaseAllowed(f, 30.0f), "no target -> nothing to judge");
    }
}

namespace
{
    TargetCandidate T(uint64_t id, bool boss, bool caster, bool elite, float dist)
    {
        TargetCandidate c;
        c.id = id;
        c.boss = boss;
        c.caster = caster;
        c.elite = elite;
        c.distToAnchor = dist;
        return c;
    }

    void TestTargets()
    {
        std::vector<TargetCandidate> pack{T(1, false, false, true, 5), T(2, false, true, true, 8),
                                          T(3, true, false, true, 10), T(4, false, false, false, 3)};
        TargetPlan p = PickTargetPlan(pack, TargetPlan(), true);
        Check(p.primary == 3, "boss is primary");
        Check(p.secondary == 2, "elite caster is secondary");
        Check(p.cc == 1, "remaining elite is the CC target");

        std::vector<TargetCandidate> trash{T(10, false, false, false, 9), T(11, false, true, false, 12),
                                           T(12, false, false, true, 4)};
        p = PickTargetPlan(trash, TargetPlan(), true);
        Check(p.primary == 11 && p.secondary == 12, "caster before elite before normal");
        Check(p.cc == 0, "no elite left for CC -> none");

        std::vector<TargetCandidate> twins{T(21, false, false, true, 6), T(20, false, false, true, 6)};
        Check(PickTargetPlan(twins, TargetPlan(), false).primary == 20, "equal rank and distance -> lower id (deterministic)");

        // stability: a kept target holds its slot even if something better shows up
        TargetPlan prev;
        prev.primary = 12;
        prev.secondary = 10;
        std::vector<TargetCandidate> more = trash;
        more.push_back(T(13, true, false, true, 30));
        p = PickTargetPlan(more, prev, false);
        Check(p.primary == 12 && p.secondary == 10, "plan is stable while its targets live");
        // primary died -> the best remaining takes over, secondary stays
        std::vector<TargetCandidate> after{T(10, false, false, false, 9), T(11, false, true, false, 12),
                                           T(13, true, false, true, 30)};
        p = PickTargetPlan(after, prev, false);
        Check(p.primary == 13 && p.secondary == 10, "dead primary replaced by the best remaining, secondary kept");

        // CC stability: a kept CC target is not promoted to a kill target
        TargetPlan prevCc;
        prevCc.cc = 1;
        p = PickTargetPlan(pack, prevCc, true);
        Check(p.cc == 1 && p.primary != 1 && p.secondary != 1, "kept CC target stays CC");

        Check(PickTargetPlan({}, TargetPlan(), true) == TargetPlan(), "no candidates -> empty plan");
    }
}

namespace
{
    void TestRecovery()
    {
        RecoveryPolicy const p{60000, 60000};
        Check(DecideRecovery(RecoveryReason::None, 999999, p) == RecoveryStep::None, "no problem -> nothing to do");
        Check(DecideRecovery(RecoveryReason::PartyFragmented, 0, p) == RecoveryStep::Act, "new problem -> act");
        Check(DecideRecovery(RecoveryReason::PartyFragmented, 59999, p) == RecoveryStep::Act, "inside the act window -> act");
        Check(DecideRecovery(RecoveryReason::MemberLost, 60000, p) == RecoveryStep::Escalate, "act window over -> escalate");
        Check(DecideRecovery(RecoveryReason::MemberDead, 119999, p) == RecoveryStep::Escalate, "inside escalation -> escalate");
        Check(DecideRecovery(RecoveryReason::MemberDead, 120000, p) == RecoveryStep::Abort, "escalation over -> abort (bounded)");
        Check(DecideRecovery(RecoveryReason::LeadershipLost, 500000, p) == RecoveryStep::Abort, "never an endless loop");

        Check(RecoveryFor(ReadyStatus::Fragmented, true) == RecoveryReason::PartyFragmented, "fragmented -> regroup");
        Check(RecoveryFor(ReadyStatus::MemberLost, true) == RecoveryReason::MemberLost, "lost member -> recovery");
        Check(RecoveryFor(ReadyStatus::MemberDead, true) == RecoveryReason::MemberDead, "dead member -> recovery");
        Check(RecoveryFor(ReadyStatus::HealerUnavailable, false) == RecoveryReason::MemberDead, "dead healer -> member dead");
        Check(RecoveryFor(ReadyStatus::HealerUnavailable, true) == RecoveryReason::MemberLost, "healer elsewhere -> member lost");
        Check(RecoveryFor(ReadyStatus::Drinking, true) == RecoveryReason::None, "drinking is a wait, not a recovery");
        Check(RecoveryFor(ReadyStatus::MasterTooFar, true) == RecoveryReason::None, "the real player is waited for, never recovered");
        Check(RecoveryFor(ReadyStatus::LowHealth, true) == RecoveryReason::None, "low health is a wait");

        ActiveFacts a = Active(LeadState::WaitingReady);
        a.walkReady = false;
        a.recovering = true;
        Check(DecideActive(a).next == LeadState::Recovery, "open recovery -> Recovery state");
        a.anyInCombat = true;
        Check(DecideActive(a).next == LeadState::Combat, "combat interrupts recovery");
        a = Active(LeadState::Recovery);
        Check(DecideActive(a).next == LeadState::Travelling, "recovery resolved, ready -> Travelling");
    }
}

namespace
{
    void TestCheckpoint()
    {
        Check(ResumeStepAfterWipe(-1, 0) == 0, "no checkpoint, at the start -> start");
        Check(ResumeStepAfterWipe(-1, 3) == 0, "no checkpoint, steps passed over -> back to the start");
        Check(ResumeStepAfterWipe(2, 3) == 3, "wiped on the step right after the checkpoint -> stay");
        Check(ResumeStepAfterWipe(2, 6) == 3, "steps skipped after the checkpoint -> resume right after it");
        Check(ResumeStepAfterWipe(5, 4) == 4, "never jump ahead of the route");
    }
}

namespace
{
    AssemblyMember Here()
    {
        AssemblyMember m;
        m.online = m.alive = m.onMap = m.sameInstance = true;
        m.distanceToTank = 5.0f;
        return m;
    }

    void TestAssembly()
    {
        std::vector<AssemblyMember> party(5, Here());
        Check(DecideAssembly(party, 0, 30.0f, 180000) == AssemblyStep::Start, "everyone here -> start");
        party[3].onMap = false;
        Check(DecideAssembly(party, 10000, 30.0f, 180000) == AssemblyStep::Wait, "teleport requested, not landed -> wait");
        party[3].onMap = true;
        party[3].sameInstance = false;
        Check(DecideAssembly(party, 10000, 30.0f, 180000) == AssemblyStep::Wait, "landed in another instance -> wait");
        party[3].sameInstance = true;
        party[3].distanceToTank = 80.0f;
        Check(DecideAssembly(party, 10000, 30.0f, 180000) == AssemblyStep::Wait, "not gathered near the tank -> wait");
        party[3].distanceToTank = 5.0f;
        party[1].alive = false;
        Check(DecideAssembly(party, 10000, 30.0f, 180000) == AssemblyStep::Wait, "dead member -> wait");
        Check(DecideAssembly(party, 180000, 30.0f, 180000) == AssemblyStep::Abort, "never assembled -> abort, not a partial start");
        Check(DecideAssembly({}, 0, 30.0f, 180000) == AssemblyStep::Wait, "no members -> never a start");
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
    TestLeash();
    TestTargets();
    TestRecovery();
    TestCheckpoint();
    TestAssembly();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
