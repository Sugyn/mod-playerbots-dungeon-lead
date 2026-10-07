/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonPack.h"

#include "Creature.h"
#include "DungeonRouteMgr.h"
#include "Group.h"
#include "Player.h"

#include <algorithm>
#include <list>

namespace
{
    // How far from the bot the grid search reaches, and how far from the pack position a creature
    // may stand and still count as part of it - same 150 yd the route walk always used.
    constexpr float kPackProbeRange = 150.0f;
    // A unit spawned this close to the pack's spot is part of it (plus the nearest one, see
    // DungeonLeadKernel::ResolvePack); farther ones with the same entry belong to another group.
    constexpr float kPackMemberRadius = 12.0f;
}

DungeonPack DungeonPacks::ForStep(DungeonRoute const& route, uint32_t stepIndex)
{
    DungeonPack pack;
    if (stepIndex >= route.steps.size())
        return pack;
    DungeonRouteStep const& step = route.steps[stepIndex];
    DungeonRouteNodeType const type = step.NodeType();
    // a door's entry is a game object, not creatures to fight
    if (!step.HasPosition() || type == DungeonRouteNodeType::Travel || step.IsInteractionStep())
        return pack;

    pack.id = stepIndex + 1;
    pack.type = type;
    pack.name = step.boss;
    pack.expectedEntries.push_back(step.entry);
    pack.x = step.x;
    pack.y = step.y;
    pack.z = step.z;
    pack.probeRadius = kPackProbeRange;
    pack.bossPack = type == DungeonRouteNodeType::Boss;
    pack.optional = !step.IsMandatory();
    return pack;
}

DungeonBossStrategy DungeonPacks::BossStrategyFor(DungeonPack const& pack, Creature const* boss, float leashRadius)
{
    DungeonBossStrategy s;
    s.bossEntry = pack.expectedEntries.empty() ? 0 : pack.expectedEntries.front();
    Position const& home = boss ? boss->GetHomePosition() : Position(pack.x, pack.y, pack.z);
    s.tankX = home.GetPositionX();
    s.tankY = home.GetPositionY();
    s.tankZ = home.GetPositionZ();
    s.leashRadius = leashRadius;
    return s;
}

DungeonPackSighting DungeonPacks::Observe(Player* bot, DungeonPack const& pack, uint32_t instanceId,
                                          std::vector<ObjectGuid> const& locked)
{
    DungeonPackSighting sighting;
    if (!pack.Exists())
        return sighting;

    // Candidate units: the expected entries around the bot (filtered to the pack's area), and
    // everything currently attacking a party member.
    std::vector<Creature*> units;
    auto addUnit = [&](Creature* c)
    {
        if (c && std::find(units.begin(), units.end(), c) == units.end())
            units.push_back(c);
    };
    for (uint32_t entry : pack.expectedEntries)
    {
        // DL-001: trash entries repeat across spatially distinct packs (e.g. Deadmines' six
        // Craftsman stops, entry 1731) - only a boss step's own kill is remembered instance-wide.
        if (EntryKillMemoryEligible(pack.type) && sDungeonRouteMgr.IsStepKilled(instanceId, entry))
            sighting.observation.rememberedKilled = true;
        // AzerothCore's grid search is centred on a WorldObject only, so search around the bot and
        // then keep what stands in the pack's area (review DL-015).
        std::list<Creature*> found;
        bot->GetCreatureListWithEntryInGrid(found, entry, kPackProbeRange);
        for (Creature* c : found)
            if (c->GetExactDist(pack.x, pack.y, pack.z) <= pack.probeRadius)
                addUnit(c);
    }
    std::vector<Unit const*> partyAttackers;
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member->GetMap() == bot->GetMap())
                for (Unit* attacker : member->getAttackers())
                    if (Creature* c = attacker ? attacker->ToCreature() : nullptr)
                    {
                        addUnit(c);
                        partyAttackers.push_back(c);
                    }

    std::vector<DungeonLeadKernel::PackUnitFacts> facts;
    std::vector<uint64_t> lockedIds;
    for (ObjectGuid const& g : locked)
        lockedIds.push_back(g.GetRawValue());
    for (Creature* c : units)
    {
        DungeonLeadKernel::PackUnitFacts f;
        f.id = c->GetGUID().GetRawValue();
        f.expectedEntry = std::find(pack.expectedEntries.begin(), pack.expectedEntries.end(), c->GetEntry()) !=
                          pack.expectedEntries.end();
        f.alive = c->IsAlive();
        f.inCombat = c->IsInCombat();
        f.attackingParty = std::find(partyAttackers.begin(), partyAttackers.end(), c) != partyAttackers.end();
        f.distToPack = c->GetExactDist(pack.x, pack.y, pack.z);
        Position const& home = c->GetHomePosition();
        f.homeDistToPack = home.GetExactDist(pack.x, pack.y, pack.z);
        facts.push_back(f);
    }

    DungeonLeadKernel::PackResolution const r = DungeonLeadKernel::ResolvePack(facts, lockedIds, kPackMemberRadius);
    bool const remembered = sighting.observation.rememberedKilled;
    sighting.observation = r.observation;
    sighting.observation.rememberedKilled = remembered;
    sighting.engagedAdds = uint32_t(r.adds.size());
    sighting.rejected = r.rejected;
    for (Creature* c : units)
    {
        uint64_t const id = c->GetGUID().GetRawValue();
        if (std::find(r.core.begin(), r.core.end(), id) != r.core.end())
            sighting.members.push_back(c->GetGUID());
        if (id == r.lead)
            sighting.firstAlive = c;
    }
    return sighting;
}
