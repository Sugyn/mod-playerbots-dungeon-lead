/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonPack.h
 *
 * The enemy group a route node is about. Defined from static route data only (creature entries
 * near the node's position); ObservePack() finds the live creatures each time it is asked, and
 * DungeonLeadKernel::DecidePackState turns that into Available / Engaged / Cleared.
 */

#ifndef MOD_DUNGEONLEAD_PACK_H
#define MOD_DUNGEONLEAD_PACK_H

#include "DungeonLeadKernels.h"
#include "DungeonRouteTypes.h"

#include <string>
#include <vector>

class Creature;
class Player;
struct DungeonRoute;

struct DungeonPack
{
    uint32_t id = 0;  // route step index + 1 (0 = no pack)
    DungeonRouteNodeType type = DungeonRouteNodeType::Travel;
    std::string name;
    std::vector<uint32_t> expectedEntries;
    float x = 0.f, y = 0.f, z = 0.f;  // pull position: where the pack is expected
    float probeRadius = 0.f;          // how far from that position a creature still belongs to it
    bool bossPack = false;
    bool optional = false;

    bool Exists() const { return id != 0; }
};

// Kept deliberately small (no scripting engine): where to tank a boss and how far the tank may
// follow it. The generic default tanks the boss where it lives (its home position) - facing is
// left to mod-playerbots' own "tank face" combat strategy, which tank specs get by default.
struct DungeonBossStrategy
{
    uint32_t bossEntry = 0;
    float tankX = 0.f, tankY = 0.f, tankZ = 0.f;
    float leashRadius = 0.f;
};

struct DungeonPackSighting
{
    DungeonLeadKernel::PackObservation observation;
    Creature* firstAlive = nullptr;  // valid for the current tick only
};

namespace DungeonPacks
{
    // The pack for a route step, or an empty pack (Exists()==false) for nodes that have none:
    // Travel anchors, rows without a creature entry or position.
    DungeonPack ForStep(DungeonRoute const& route, uint32_t stepIndex);

    // Live creatures of `pack` around its position, as seen from `bot`, plus the instance kill
    // memory for `instanceId`.
    DungeonPackSighting Observe(Player* bot, DungeonPack const& pack, uint32_t instanceId);

    // Generic boss strategy for a live boss of `pack` (route data has no per-boss positions yet).
    DungeonBossStrategy BossStrategyFor(DungeonPack const& pack, Creature const* boss, float leashRadius);
}

#endif
