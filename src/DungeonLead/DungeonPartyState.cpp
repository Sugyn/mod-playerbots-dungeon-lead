/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonPartyState.h"

#include "DungeonLeadConfig.h"
#include "DungeonRouteMgr.h"
#include "Timer.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"

#include <algorithm>

namespace
{
    // In combat and either attacked or attacking - or not for long (DungeonLeadKernel::CombatCounts).
    // The per-member clock lives in the leader's session state; outside a session the plain flag.
    bool CombatWithEnemy(Player* leader, Player* member)
    {
        bool const inCombat = member->IsInCombat();
        bool const hasEnemy = !member->getAttackers().empty() || member->GetVictim() != nullptr;
        if (!sDungeonRouteMgr.HasState(leader->GetGUID()))
            return inCombat;
        DungeonLeadState& st = sDungeonRouteMgr.State(leader->GetGUID());
        auto it = std::find_if(st.staleCombatSince.begin(), st.staleCombatSince.end(),
                               [&](auto const& p) { return p.first == member->GetGUID(); });
        if (!inCombat || hasEnemy)
        {
            if (it != st.staleCombatSince.end())
                st.staleCombatSince.erase(it);
            return inCombat;
        }
        if (it == st.staleCombatSince.end())
        {
            st.staleCombatSince.emplace_back(member->GetGUID(), getMSTime());
            return true;
        }
        return DungeonLeadKernel::CombatCounts(inCombat, hasEnemy, GetMSTimeDiffToNow(it->second));
    }
}

DungeonPartySnapshot DungeonPartyState::Evaluate(PlayerbotAI* leaderAI)
{
    DungeonPartySnapshot snap;
    DungeonLeadKernel::PartyFacts& f = snap.facts;
    Player* bot = leaderAI->GetBot();
    Group* group = bot->GetGroup();

    f.hasGroup = group != nullptr;
    f.selfInCombat = CombatWithEnemy(bot, bot);

    // Only touch session state when a session actually exists for this leader (readiness is also
    // evaluated outside an active session, e.g. canary probing) - same guard CombatWithEnemy() uses.
    bool const haveSession = sDungeonRouteMgr.HasState(bot->GetGUID());
    DungeonLeadState* st = haveSession ? &sDungeonRouteMgr.State(bot->GetGUID()) : nullptr;

    Player* master = leaderAI->GetMaster();
    ObjectGuid masterGuid;
    if (master && master != bot)
    {
        f.master.assigned = true;
        f.master.online = master->IsInWorld() && master->GetSession();
        f.master.alive = master->IsAlive();
        f.master.inGroup = !group || group->IsMember(master->GetGUID());
        f.master.sameMap = master->GetMap() == bot->GetMap();
        if (f.master.sameMap)
            f.master.distance = bot->GetDistance(master);
        masterGuid = master->GetGUID();
        if (st)
            st->masterGuid = masterGuid;
    }
    else if (st && st->masterGuid)
    {
        // DL-004 (the master is the same "offline identity disappears" problem one level up, fed
        // into DL-003's recovery mapping): PlayerbotAI::GetMaster() returns null once
        // mod-playerbots' own RandomPlayerbotMgr::OnPlayerLogout clears it on a full logout
        // (verified against the real source on acore-clean) - by then the Player object is
        // already deleted, so there is nothing left to query online/alive/inGroup on directly.
        // Report the last-known master as offline instead of letting it drop out of f.master
        // entirely: without this, ReadyStatus::MasterUnavailable could never actually fire for
        // the single most common case (the real player's client disconnecting), since
        // f.master.assigned would simply go back to false.
        f.master.assigned = true;
        f.master.online = false;
        f.master.alive = false;
        f.master.inGroup = !group || group->IsMember(st->masterGuid);
        f.master.sameMap = false;
        f.master.distance = 0.0f;
        masterGuid = st->masterGuid;
    }

    if (!group)
        return snap;

    // DL-004: Group::GetFirstMember() only walks currently-resolvable (online) Player* objects -
    // a fully offline member's slot never produces one at all, so it silently disappears from
    // f.members instead of appearing unavailable. GetMemberSlots() is the full roster (online and
    // offline alike); resolve each slot individually instead.
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        DungeonLeadKernel::PartyMemberFacts m;
        m.isSelf = slot.guid == bot->GetGUID();
        m.isMaster = masterGuid && slot.guid == masterGuid;
        if (member)
        {
            // bySpec=true for availability: the default lags behind a fresh talent change (see
            // DungeonTestBotPool's VerifyReady); the mana check keeps mod-playerbots' own default,
            // same as its "healer low mana" value did.
            m.isHealerBySpec = PlayerbotAI::IsHeal(member, /*bySpec*/ true);
            m.isHealerRole = PlayerbotAI::IsHeal(member);
            m.gameMaster = member->IsGameMaster();
            m.alive = member->IsAlive();  // a ghost is DeathState::Dead too
            m.online = member->IsInWorld() && member->GetSession();
            m.sameMap = member->GetMap() == bot->GetMap();
            m.inCombat = CombatWithEnemy(bot, member);
            m.sitting = member->IsSitState();
            m.manaPct = member->GetPowerPct(POWER_MANA);
            m.healthPct = member->GetHealthPct();
            if (m.sameMap)
                m.distance = bot->GetDistance(member);
            if (st)
            {
                auto it = std::find_if(st->knownHealerRole.begin(), st->knownHealerRole.end(),
                                       [&](auto const& p) { return p.first == slot.guid; });
                if (it == st->knownHealerRole.end())
                    st->knownHealerRole.emplace_back(slot.guid, m.isHealerRole);
                else
                    it->second = m.isHealerRole;
            }
        }
        else
        {
            // Offline: no live Player* to ask anything of. online/alive/sameMap/distance all read
            // as "not here", matching PartyMemberFacts' existing semantics (see
            // DungeonLeadKernels.h) - this is what lets EvaluateCohesion()/EvaluateHealer() see a
            // LostMember/Unavailable instead of nothing. The role can only come from the last time
            // this guid was actually seen online in this session; a guid never seen before
            // defaults to isHealerRole = isHealerBySpec = false - an explicit "unknown", not a
            // guess, same policy as the master cache above.
            m.online = false;
            m.alive = false;
            m.sameMap = false;
            m.distance = 0.0f;
            if (st)
            {
                auto it = std::find_if(st->knownHealerRole.begin(), st->knownHealerRole.end(),
                                       [&](auto const& p) { return p.first == slot.guid; });
                if (it != st->knownHealerRole.end())
                    m.isHealerBySpec = m.isHealerRole = it->second;
            }
        }
        f.members.push_back(m);
        snap.members.push_back(member);
    }
    return snap;
}

DungeonLeadKernel::ReadinessPolicy DungeonPartyState::Policy()
{
    DungeonLeadKernel::ReadinessPolicy p;
    p.healerManaPct = float(sDungeonLeadConfig.dungeonLeadHealerManaPct);
    p.leash = sDungeonLeadConfig.dungeonLeadLeash;
    p.softRange = sDungeonLeadConfig.dungeonLeadPartySoftRange;
    p.hardRange = sDungeonLeadConfig.dungeonLeadPartyHardRange;
    p.minHealthPct = float(sDungeonLeadConfig.dungeonLeadPostCombatMinHealthPct);
    return p;
}

DungeonLeadKernel::Readiness DungeonPartyState::Readiness(DungeonPartySnapshot const& snap,
                                                          DungeonLeadKernel::ReadyPurpose purpose)
{
    return DungeonLeadKernel::EvaluateReadiness(snap.facts, Policy(), purpose);
}
