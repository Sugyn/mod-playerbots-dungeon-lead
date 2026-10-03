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
#include "Player.h"

#include <list>

namespace
{
    // How far from the bot the grid search reaches, and how far from the pack position a creature
    // may stand and still count as part of it - same 150 yd the route walk always used.
    constexpr float kPackProbeRange = 150.0f;
}

DungeonPack DungeonPacks::ForStep(DungeonRoute const& route, uint32_t stepIndex)
{
    DungeonPack pack;
    if (stepIndex >= route.steps.size())
        return pack;
    DungeonRouteStep const& step = route.steps[stepIndex];
    DungeonRouteNodeType const type = step.NodeType();
    if (!step.HasPosition() || type == DungeonRouteNodeType::Travel)
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

DungeonPackSighting DungeonPacks::Observe(Player* bot, DungeonPack const& pack, uint32_t instanceId)
{
    DungeonPackSighting sighting;
    if (!pack.Exists())
        return sighting;

    for (uint32_t entry : pack.expectedEntries)
    {
        if (sDungeonRouteMgr.IsStepKilled(instanceId, entry))
            sighting.observation.rememberedKilled = true;

        // AzerothCore's grid search is centred on a WorldObject only, so search around the bot and
        // then keep what stands near the pack's own position - an unrelated creature with the same
        // entry elsewhere in range must not be mistaken for this pack (review DL-015).
        std::list<Creature*> found;
        bot->GetCreatureListWithEntryInGrid(found, entry, kPackProbeRange);
        for (Creature* c : found)
        {
            if (c->GetExactDist(pack.x, pack.y, pack.z) > pack.probeRadius)
                continue;
            ++sighting.observation.found;
            if (!c->IsAlive())
                continue;
            ++sighting.observation.alive;
            if (c->IsInCombat())
                ++sighting.observation.engaged;
            if (!sighting.firstAlive)
                sighting.firstAlive = c;
        }
    }
    return sighting;
}
