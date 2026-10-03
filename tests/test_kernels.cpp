// Unit tests for src/DungeonLead/DungeonLeadKernels.h - no worldserver needed.
// Run: tools/run_tests.sh

#include "DungeonLeadKernels.h"

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

int main()
{
    TestLeadership();
    TestHealer();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
