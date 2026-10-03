/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonPartyState.h"

#include "DungeonLeadConfig.h"
#include "Group.h"
#include "Player.h"
#include "Playerbots.h"

DungeonPartySnapshot DungeonPartyState::Evaluate(PlayerbotAI* leaderAI)
{
    DungeonPartySnapshot snap;
    DungeonLeadKernel::PartyFacts& f = snap.facts;
    Player* bot = leaderAI->GetBot();
    Group* group = bot->GetGroup();

    f.hasGroup = group != nullptr;
    f.selfInCombat = bot->IsInCombat();

    Player* master = leaderAI->GetMaster();
    if (master && master != bot)
    {
        f.master.assigned = true;
        f.master.online = master->IsInWorld() && master->GetSession();
        f.master.alive = master->IsAlive();
        f.master.inGroup = !group || group->IsMember(master->GetGUID());
        f.master.sameMap = master->GetMap() == bot->GetMap();
        if (f.master.sameMap)
            f.master.distance = bot->GetDistance(master);
    }

    if (!group)
        return snap;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member)
            continue;
        DungeonLeadKernel::PartyMemberFacts m;
        m.isSelf = member == bot;
        m.isMaster = f.master.assigned && member == master;
        // bySpec=true for availability: the default lags behind a fresh talent change (see
        // DungeonTestBotPool's VerifyReady); the mana check keeps mod-playerbots' own default,
        // same as its "healer low mana" value did.
        m.isHealerBySpec = PlayerbotAI::IsHeal(member, /*bySpec*/ true);
        m.isHealerRole = PlayerbotAI::IsHeal(member);
        m.gameMaster = member->IsGameMaster();
        m.alive = member->IsAlive();  // a ghost is DeathState::Dead too
        m.online = member->IsInWorld() && member->GetSession();
        m.sameMap = member->GetMap() == bot->GetMap();
        m.inCombat = member->IsInCombat();
        m.sitting = member->IsSitState();
        m.manaPct = member->GetPowerPct(POWER_MANA);
        m.healthPct = member->GetHealthPct();
        if (m.sameMap)
            m.distance = bot->GetDistance(member);
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
