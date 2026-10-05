// Unit tests for src/DungeonLead/DungeonLeadKernels.h - no worldserver needed.
// Run: tools/run_tests.sh

#include "DungeonLeadKernels.h"
#include "DungeonRouteTypes.h"
#include "DungeonTelemetryBuffer.h"

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
        Check(StateAllowsNewPull(LeadState::Travelling) && StateAllowsNewPull(LeadState::BossPrep) &&
                  StateAllowsNewPull(LeadState::Combat),
              "working the route or fighting -> a fight may be opened");
        Check(!StateAllowsNewPull(LeadState::WaitingReady) && !StateAllowsNewPull(LeadState::PostCombat) &&
                  !StateAllowsNewPull(LeadState::Recovery) && !StateAllowsNewPull(LeadState::WipeRecovery) &&
                  !StateAllowsNewPull(LeadState::Completing) && !StateAllowsNewPull(LeadState::Starting) &&
                  !StateAllowsNewPull(LeadState::Stopping),
              "waiting, recovering, wiped, done or not active -> no new fight");

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

        // H7 (SFK): a CC'd leftover keeps the tank in combat - the next pull waits for it
        f = Pull(PullState::Approaching);
        f.leaderInCombat = true;
        Check(DecidePull(f, kPull) == PullState::Approaching, "in a fight with something else -> no marking yet");
        f = Pull(PullState::Established);
        f.leaderInCombat = true;
        Check(DecidePull(f, kPull) == PullState::Approaching, "fight with the pack's trash goes on -> approach, not a new pull");

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

        // live report 2026-10-05: polymorph on the last, almost dead mob held the whole party
        std::vector<TargetCandidate> lastOne{T(1, false, false, true, 5)};
        p = PickTargetPlan(lastOne, prevCc, true);
        Check(p.cc == 0 && p.primary == 1, "CC'd mob is the only enemy left -> released, it is the kill target");
        std::vector<TargetCandidate> hurt = pack;
        hurt[0].healthPct = 30;  // the would-be CC target, already half dead
        p = PickTargetPlan(hurt, TargetPlan(), true);
        Check(p.cc == 0, "no new CC on a mob the party has already damaged");
        hurt[0].healthPct = 90;
        Check(PickTargetPlan(hurt, TargetPlan(), true).cc == 1, "a fresh elite still gets CC");

        // live report 2026-10-05 (Zul'Farrak): lava totems left standing, a sheep given the skull
        std::vector<TargetCandidate> withTotem = pack;
        withTotem.push_back(T(9, false, false, false, 20));
        withTotem.back().totem = true;
        TargetPlan prevKill;
        prevKill.primary = 3;
        p = PickTargetPlan(withTotem, prevKill, true);
        Check(p.primary == 9, "an enemy totem takes the skull, even from the kept primary");
        std::vector<TargetCandidate> sheep = trash;  // 10 normal, 11 caster, 12 elite
        sheep[1].controlled = true;                  // the caster is a sheep already
        p = PickTargetPlan(sheep, TargetPlan(), true);
        Check(p.primary != 11 && p.secondary != 11, "a crowd-controlled mob is not a kill target while others are up");
        Check(p.cc == 11, "the mob already under CC keeps the moon");
        TargetPlan prevSheep;
        prevSheep.primary = 11;
        sheep[1].controlled = true;
        Check(PickTargetPlan(sheep, prevSheep, false).primary != 11, "a kept primary that got sheeped is replaced");
        // Zul'Farrak (Ruuzlu): CC never landed, the mob was re-marked and left beating the healer
        std::vector<TargetCandidate> immune = pack;
        immune[0].ccRefused = true;
        TargetPlan prevImmune;
        prevImmune.cc = 1;
        p = PickTargetPlan(immune, prevImmune, true);
        Check(p.cc != 1, "a mob our CC failed on is not kept or marked for CC again");

        // H2: fight membership is measured from the combat anchor, not the tank
        float const radius = 40.0f;
        auto fight = [](bool combat, bool attacking, float dist)
        {
            FightCandidateFacts f;
            f.inCombat = combat;
            f.attackingParty = attacking;
            f.distToAnchor = dist;
            return f;
        };
        Check(IsFightCandidate(fight(true, true, 5.0f), radius), "tank at the anchor: its target counts");
        // the tank moved 15 yd; a mob 10 yd from the tank's new spot is 50 yd from the anchor
        Check(!IsFightCandidate(fight(true, false, 50.0f), radius),
              "neighbour near the moved tank but outside the anchor radius is not promoted");
        Check(IsFightCandidate(fight(true, false, 35.0f), radius), "pack member still inside the anchor radius counts");
        Check(IsFightCandidate(fight(true, true, 70.0f), radius), "an add attacking the party counts wherever it stands");
        Check(!IsFightCandidate(fight(false, false, 3.0f), radius), "idle mob next to the anchor is not part of the fight");
    }
}

namespace
{
    void TestRecovery()
    {
        RecoveryPolicy const p{60000, 60000};
        Check(DecideRecovery(RecoveryReason::None, 999999, 999999, p) == RecoveryStep::None, "no problem -> nothing to do");
        Check(DecideRecovery(RecoveryReason::PartyFragmented, 0, 0, p) == RecoveryStep::Act, "new problem -> act");
        Check(DecideRecovery(RecoveryReason::PartyFragmented, 59999, 59999, p) == RecoveryStep::Act, "inside the act window -> act");
        Check(DecideRecovery(RecoveryReason::MemberLost, 60000, 60000, p) == RecoveryStep::Escalate, "act window over -> escalate");
        Check(DecideRecovery(RecoveryReason::MemberDead, 119999, 119999, p) == RecoveryStep::Escalate, "inside escalation -> escalate");
        Check(DecideRecovery(RecoveryReason::MemberDead, 120000, 120000, p) == RecoveryStep::Abort, "escalation over -> abort (bounded)");
        Check(DecideRecovery(RecoveryReason::LeadershipLost, 500000, 500000, p) == RecoveryStep::Abort, "never an endless loop");

        // H1: a reason change restarts that reason's clock; the episode stays bounded
        RecoveryTimers t;
        uint32_t now = 1000;
        Check(ObserveRecovery(t, RecoveryReason::PartyFragmented, now), "first problem starts a recovery");
        now += 20000;  // fragmented for 20 s
        Check(ObserveRecovery(t, RecoveryReason::MemberLost, now), "reason change starts a new recovery");
        Check(now - t.reasonSince == 0 && t.step == RecoveryStep::None, "fragmented 20 s -> lost: elapsed restarts from 0");
        Check(now - t.episodeSince == 20000, "the episode clock keeps running across the change");
        Check(DecideRecovery(t.reason, now - t.reasonSince, now - t.episodeSince, p) == RecoveryStep::Act,
              "new reason starts at act, not near escalation");

        t = RecoveryTimers();
        now = 5000;
        ObserveRecovery(t, RecoveryReason::MemberDead, now);  // healer dead
        now += 70000;  // past its act window
        Check(DecideRecovery(t.reason, now - t.reasonSince, now - t.episodeSince, p) == RecoveryStep::Escalate,
              "dead healer after 70 s -> escalate");
        ObserveRecovery(t, RecoveryReason::PartyFragmented, now);
        Check(DecideRecovery(t.reason, now - t.reasonSince, now - t.episodeSince, p) == RecoveryStep::Act,
              "dead -> fragmented gets its own independent timeout");

        t = RecoveryTimers();
        now = 0;
        ObserveRecovery(t, RecoveryReason::PartyFragmented, now);
        Check(!ObserveRecovery(t, RecoveryReason::PartyFragmented, now + 30000) && t.reasonSince == 0,
              "same reason again keeps its clock");

        // flapping between two reasons every 50 s never escalates per reason, but the episode cap ends it
        t = RecoveryTimers();
        now = 0;
        RecoveryStep last = RecoveryStep::None;
        uint32_t abortAt = 0;
        for (int i = 0; i < 20 && last != RecoveryStep::Abort; ++i)
        {
            ObserveRecovery(t, i % 2 ? RecoveryReason::MemberLost : RecoveryReason::PartyFragmented, now);
            last = DecideRecovery(t.reason, now - t.reasonSince, now - t.episodeSince, p);
            abortAt = now;
            now += 50000;
        }
        Check(last == RecoveryStep::Abort && abortAt < p.EpisodeCapMs() + 50000,
              "flapping reasons are bounded by the episode cap");

        Check(!ObserveRecovery(t, RecoveryReason::None, now) && t.reason == RecoveryReason::None && t.episodeSince == 0,
              "problem gone -> clocks cleared");

        // H7: a straggler stuck on terrain - caught up while the leader walks back, behind again
        // 20 s after it walks on - is one episode that reaches escalation, not a fresh act forever
        t = RecoveryTimers();
        now = 0;
        bool escalated = false;
        for (int i = 0; i < 10 && !escalated; ++i)
        {
            ObserveRecovery(t, RecoveryReason::PartyFragmented, now, 7);
            escalated = DecideRecovery(t.reason, now - t.reasonSince, now - t.episodeSince, p) == RecoveryStep::Escalate;
            now += 12000;
            ObserveRecovery(t, RecoveryReason::None, now);
            now += 20000;
        }
        Check(escalated && now < 160000, "relapsing straggler escalates within ~2 act windows");
        t = RecoveryTimers();
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 0, 7);
        ObserveRecovery(t, RecoveryReason::None, 10000);
        Check(ObserveRecovery(t, RecoveryReason::PartyFragmented, 30000, 7) && t.relapse && t.reasonSince == 0,
              "same reason + member soon after -> resumes the old clock");
        t = RecoveryTimers();
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 0, 7);
        ObserveRecovery(t, RecoveryReason::None, 10000);
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 30000, 8);
        Check(!t.relapse && t.reasonSince == 30000, "another member -> fresh recovery");
        t = RecoveryTimers();
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 0, 7);
        ObserveRecovery(t, RecoveryReason::None, 10000);
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 10000 + kRecoveryRelapseMs, 7);
        Check(!t.relapse && t.reasonSince == 10000 + kRecoveryRelapseMs, "long after it cleared -> fresh recovery");

        // H7 (RFC 60-min pass): straggler A escalated, then B falls behind - B gets its own window
        t = RecoveryTimers();
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 0, 7);
        t.step = RecoveryStep::Escalate;
        Check(ObserveRecovery(t, RecoveryReason::PartyFragmented, 70000, 8) && t.reasonSince == 70000 &&
                  t.step == RecoveryStep::None && t.episodeSince == 0,
              "same reason, another member -> new recovery, episode goes on");
        Check(DecideRecovery(t.reason, 70000 - t.reasonSince, 70000 - t.episodeSince, p) == RecoveryStep::Act,
              "the second straggler starts at act, not at abort");
        Check(!ObserveRecovery(t, RecoveryReason::PartyFragmented, 80000, 8), "same member again -> same recovery");
        t = RecoveryTimers();
        ObserveRecovery(t, RecoveryReason::PartyFragmented, 0, 7);
        Check(!ObserveRecovery(t, RecoveryReason::PartyFragmented, 30000, 8) && t.reasonSince == 0,
              "farthest member swaps while still acting -> clock keeps running (escalation is reached)");

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

namespace
{
    void TestTelemetryBuffer()
    {
        TelemetryBuffer buf(3);
        Check(buf.Push(TelemetryFile::Sessions, "a\n") && buf.Push(TelemetryFile::Runs, "b\n") &&
                  buf.Push(TelemetryFile::Debug, "c\n"),
              "lines up to capacity are accepted");
        Check(!buf.Push(TelemetryFile::Sessions, "d\n"), "full buffer refuses, does not grow");
        uint64_t dropped = 0;
        std::vector<TelemetryLine> lines = buf.Drain(dropped);
        Check(lines.size() == 3 && dropped == 1, "drain returns everything queued and the drop count");
        Check(lines[0].text == "a\n" && lines[1].file == TelemetryFile::Runs, "order and target file kept");
        Check(buf.Size() == 0 && buf.Drain(dropped).empty() && dropped == 0, "drained buffer is empty, count reset");
        Check(buf.Push(TelemetryFile::Sessions, "e\n"), "accepts again after a drain");
    }
}

namespace
{
    void TestObjectivePolicy()
    {
        using R = DungeonObjectiveRequirement;
        Check(ClassifyRequirement(DungeonRouteKind::Boss, 3653) == R::Boss, "boss row -> Boss");
        Check(ClassifyRequirement(DungeonRouteKind::Optional, 3654) == R::Optional, "optional trash -> Optional");
        Check(ClassifyRequirement(DungeonRouteKind::Boss, 1) == R::Optional, "path anchor is never mandatory");
        Check(ClassifyRequirement(DungeonRouteKind::Event, 42) == R::Optional, "event row -> Optional");
        // audit 2026-10-05: explicit requirement in the route data
        Check(ParseRouteKind("required") == DungeonRouteKind::Required, "kind 'required' parses");
        Check(ClassifyRequirement(DungeonRouteKind::Required, 4424) == R::Required, "required trash -> Required");
        Check(ClassifyRouteStep(DungeonRouteKind::Required, 4424) == DungeonRouteNodeType::Pull,
              "required trash is fought like a pull");
        Check(ClassifyRequirement(DungeonRouteKind::Door, 16397) == R::Required, "a door is never skipped");
        Check(DecideObjectiveFailure(R::Required, 1, 2) == ObjectiveFailureAction::Retry &&
                  DecideObjectiveFailure(R::Required, 2, 2) == ObjectiveFailureAction::Abort,
              "required objective: retry, then abort - never skip");
        // door heuristic: on the way = near the line leader -> destination
        Check(DistanceToSegment2D(5, 2, 0, 0, 10, 0) < 2.01f, "door 2 yd off the corridor line is on the way");
        Check(DistanceToSegment2D(5, 14, 0, 0, 10, 0) > 10.f, "side-room door 14 yd off the line is not");
        Check(DistanceToSegment2D(-3, 0, 0, 0, 10, 0) > 2.99f, "behind the leader measures to the leader");
        // walking through a closed door (live, ZF End Door): plane crossing, not just nearness
        // SFK Courtyard Door at (-242.6, 2159.05), orientation -1.946 - the way north goes through it
        Check(SegmentCrossesDoor(-242.6f, 2159.05f, -1.946f, -243.f, 2145.f, -241.f, 2172.f, 6.f),
              "path north through the Courtyard Door crosses it");
        // SFK cells at (-244.1, 2134.4), orientation -0.375, lined along the corridor wall
        Check(!SegmentCrossesDoor(-244.1f, 2134.4f, -0.375f, -251.f, 2120.f, -240.f, 2147.f, 6.f),
              "path along the corridor past a cell door does not cross it");
        Check(!SegmentCrossesDoor(0.f, 0.f, 0.f, -5.f, 20.f, 5.f, 20.f, 6.f),
              "crossing the door's plane far from the door doesn't count");

        for (uint32_t rounds = 1; rounds <= 3; ++rounds)
            Check(DecideObjectiveFailure(R::Optional, rounds, 2) == ObjectiveFailureAction::Skip,
                  "optional trash failing (1-3 rounds) may be skipped");
        Check(DecideObjectiveFailure(R::Required, 1, 2) == ObjectiveFailureAction::Retry,
              "required trash failing -> retried, not skipped");
        Check(DecideObjectiveFailure(R::Required, 2, 2) == ObjectiveFailureAction::Abort,
              "required trash still failing -> run stops, never silently advances");
        Check(DecideObjectiveFailure(R::Boss, 1, 2) == ObjectiveFailureAction::Retry, "boss pull fails -> retried");
        Check(DecideObjectiveFailure(R::Boss, 2, 2) == ObjectiveFailureAction::Abort, "boss still unresolved -> run stops");
        bool neverSkips = true;
        for (uint32_t rounds = 1; rounds < 10; ++rounds)
            neverSkips = neverSkips && DecideObjectiveFailure(R::Boss, rounds, 2) != ObjectiveFailureAction::Skip &&
                         DecideObjectiveFailure(R::Required, rounds, 2) != ObjectiveFailureAction::Skip;
        Check(neverSkips, "a boss or required objective is never skipped, whatever the count");
    }
}

namespace
{
    PackUnitFacts U(uint64_t id, bool expected, bool alive, bool inCombat, bool attacking, float dist, float home)
    {
        PackUnitFacts u;
        u.id = id;
        u.expectedEntry = expected;
        u.alive = alive;
        u.inCombat = inCombat;
        u.attackingParty = attacking;
        u.distToPack = dist;
        u.homeDistToPack = home;
        return u;
    }

    bool Has(std::vector<uint64_t> const& v, uint64_t id)
    {
        for (uint64_t m : v)
            if (m == id)
                return true;
        return false;
    }

    void TestPackIdentity()
    {
        float const radius = 10.0f;
        // pack A at its spot (ids 1-3), pack B with the same entry 15 yd away (ids 4-6)
        std::vector<PackUnitFacts> room{U(1, true, true, false, false, 2, 2), U(2, true, true, false, false, 4, 4),
                                        U(3, true, true, false, false, 5, 5), U(4, true, true, false, false, 15, 15),
                                        U(5, true, true, false, false, 16, 16), U(6, true, true, false, false, 17, 17)};
        PackResolution r = ResolvePack(room, {}, radius);
        Check(r.core.size() == 3 && Has(r.core, 1) && !Has(r.core, 4) && r.rejected == 3,
              "two packs 15 yd apart, same entry: only the near one is this pack");
        Check(r.lead == 1, "lead is the nearest live member");

        // tank engages pack A only: lock it, pack B stays out
        std::vector<uint64_t> locked = r.core;
        room[0].inCombat = room[1].inCombat = true;
        room[0].attackingParty = true;
        r = ResolvePack(room, locked, radius);
        Check(r.core.size() == 3 && !Has(r.core, 4) && r.observation.engaged == 2, "engaged pack A locked, B not merged");

        // an unexpected add (other entry) joins the fight
        room.push_back(U(9, false, true, true, true, 25, 40));
        r = ResolvePack(room, locked, radius);
        Check(Has(r.adds, 9) && r.adds.size() == 1, "add attacking the party becomes an encounter member");
        // a pack-B unit pulled into the fight joins as an add, the rest of B does not
        room[3].inCombat = room[3].attackingParty = true;
        r = ResolvePack(room, locked, radius);
        Check(Has(r.adds, 4) && !Has(r.adds, 5) && !Has(r.core, 5) && r.adds.size() == 2,
              "neighbour that actually joins counts, its idle friends don't");

        // a locked member that died and then despawned/moved out of sight: still bounded and stable
        std::vector<PackUnitFacts> later{U(2, true, false, false, false, 4, 4), U(3, true, false, false, false, 5, 5)};
        r = ResolvePack(later, locked, radius);
        Check(r.core.size() == 2 && r.observation.alive == 0 &&
                  DecidePackState(PackState::Engaged, r.observation) == PackState::Cleared,
              "locked members dead (one missing) -> cleared");

        // fighting a boss's trash is not fighting the boss
        std::vector<PackUnitFacts> bossRoom{U(20, true, true, false, false, 3, 3), U(21, false, true, true, true, 15, 25)};
        r = ResolvePack(bossRoom, {}, radius);
        Check(r.observation.engaged == 0 && r.adds.size() == 1 && r.lead == 20,
              "trash attacking the party is an add; the idle boss is not engaged");

        // a patrolling named mob far from its spot is still the pack (the lead)
        std::vector<PackUnitFacts> patrol{U(7, true, true, false, false, 90, 0)};
        r = ResolvePack(patrol, {}, radius);
        Check(r.lead == 7 && r.core.size() == 1, "patrol away from its spot is still the pack's lead");

        Check(ResolvePack({}, {}, radius).core.empty(), "nothing around -> empty, Unknown");
    }
}

namespace
{
    InteractionFacts I(InteractionState current, uint32_t ms, bool found, bool satisfied, bool canAct = false)
    {
        InteractionFacts f;
        f.current = current;
        f.msInInteraction = ms;
        f.targetFound = found;
        f.satisfied = satisfied;
        f.canAct = canAct;
        return f;
    }

    void TestInteraction()
    {
        uint32_t const timeout = 120000;
        using S = InteractionState;
        Check(DecideInteraction(I(S::Resolving, 0, true, true), timeout) == S::Complete, "door already open -> complete");
        Check(DecideInteraction(I(S::Resolving, 0, true, false), timeout) == S::WaitingPrerequisite,
              "closed, nothing we can do -> wait for its event/key");
        Check(DecideInteraction(I(S::WaitingPrerequisite, 30000, true, false), timeout) == S::WaitingPrerequisite,
              "still closed inside the window -> keep waiting");
        Check(DecideInteraction(I(S::WaitingPrerequisite, 45000, true, true), timeout) == S::Complete,
              "door opens after the boss/event -> complete (world-confirmed)");
        Check(DecideInteraction(I(S::WaitingPrerequisite, timeout, true, false), timeout) == S::Failed,
              "never opens -> failed, bounded");
        Check(DecideInteraction(I(S::Resolving, 1000, false, false), timeout) == S::Resolving,
              "target not seen yet -> keep resolving");
        Check(DecideInteraction(I(S::WaitingPrerequisite, 1000, false, false), timeout) == S::Failed,
              "interaction target vanished -> failed");
        Check(DecideInteraction(I(S::Resolving, 0, true, false, true), timeout) == S::Interacting, "actionable -> interacting");
        Check(DecideInteraction(I(S::Interacting, 500, true, false, true), timeout) == S::WaitingConfirmation,
              "acted -> wait for confirmation, not done yet");
        Check(DecideInteraction(I(S::WaitingConfirmation, 2000, true, true, true), timeout) == S::Complete,
              "world confirms -> complete");
        Check(DecideInteraction(I(S::None, 0, false, false), timeout) == S::None && !InteractionActive(S::Complete),
              "no interaction -> nothing to do");

        // H6: the party fragments while waiting at a door - recovery time is not door time
        uint32_t ms = 0;
        ms = AdvanceInteractionClock(ms, 60000, true);    // a minute at the door
        ms = AdvanceInteractionClock(ms, 300000, false);  // five minutes of recovery elsewhere
        Check(ms == 60000 && DecideInteraction(I(S::WaitingPrerequisite, ms, true, false), timeout) ==
                                 S::WaitingPrerequisite,
              "recovery during a door wait doesn't use up the door's budget");
        ms = AdvanceInteractionClock(ms, 60000, true);
        Check(DecideInteraction(I(S::WaitingPrerequisite, ms, true, false), timeout) == S::Failed,
              "back at the door, its own time still runs out");
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
    TestTelemetryBuffer();
    TestObjectivePolicy();
    TestPackIdentity();
    TestInteraction();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
