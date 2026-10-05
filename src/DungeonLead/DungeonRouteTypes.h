/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonRouteTypes.h
 *
 * Route vocabulary, dependency-free so tests/ can use it. Two layers:
 *  - DungeonRouteKind: what the route DATA says a row is (playerbots_dungeon_route.kind). Drives
 *    run accounting (which rows are mandatory for a Complete outcome).
 *  - DungeonRouteNodeType: what the leader is going there to DO. Derived from the kind (plus the
 *    path-anchor convention), so existing route data needs no change.
 */

#ifndef MOD_DUNGEONLEAD_ROUTETYPES_H
#define MOD_DUNGEONLEAD_ROUTETYPES_H

#include <cstdint>
#include <string>

// The DB column stays a plain VARCHAR - this is the C++-side representation, parsed once at
// Load(). A typo becomes Unknown, logged as an error at load time and rejected ahead of time by
// tools/validate_routes.py.
enum class DungeonRouteKind : uint8_t
{
    Boss,
    Optional,
    Required,  // trash that gates progress (an event, a door): fought like optional, never skipped
    HeroicOnly,
    Event,
    Door,
    Skip,
    Unknown,  // failed to parse - never walkable or mandatory, always a load-time LOG_ERROR
};

inline DungeonRouteKind ParseRouteKind(std::string const& s)
{
    if (s == "boss") return DungeonRouteKind::Boss;
    if (s == "optional") return DungeonRouteKind::Optional;
    if (s == "required") return DungeonRouteKind::Required;
    if (s == "heroic_only") return DungeonRouteKind::HeroicOnly;
    if (s == "event") return DungeonRouteKind::Event;
    if (s == "door") return DungeonRouteKind::Door;
    if (s == "skip") return DungeonRouteKind::Skip;
    return DungeonRouteKind::Unknown;
}

// entry=1 is AzerothCore's "Waypoint (Only GM can see it)" creature template - never gameplay
// content, only used in route data as a dummy entry for a pure navigation anchor.
constexpr uint32_t kPathAnchorEntry = 1;

enum class DungeonRouteNodeType : uint8_t
{
    Travel,        // navigation anchor - just pass through
    Pull,          // optional trash/rare: fight what is there
    TankPosition,  // where the tank should hold a pull (no route data yet)
    SafeSpot,      // regroup point (no route data yet)
    Boss,          // encounter
    Door,          // gate/door the party has to get through - waited for until it opens
    Interaction,   // event or object to interact with
    Recovery,      // where stranded members are brought back to (the route's entrance)
    End,           // route finished
};

inline char const* ToString(DungeonRouteNodeType t)
{
    switch (t)
    {
        case DungeonRouteNodeType::Travel:       return "travel";
        case DungeonRouteNodeType::Pull:         return "pull";
        case DungeonRouteNodeType::TankPosition: return "tank_position";
        case DungeonRouteNodeType::SafeSpot:     return "safe_spot";
        case DungeonRouteNodeType::Boss:         return "boss";
        case DungeonRouteNodeType::Door:         return "door";
        case DungeonRouteNodeType::Interaction:  return "interaction";
        case DungeonRouteNodeType::Recovery:     return "recovery";
        case DungeonRouteNodeType::End:          return "end";
    }
    return "unknown";
}

// What a route row asks the leader to do. Skip/Unknown rows are never walked; they classify as
// Travel only so the function is total.
inline DungeonRouteNodeType ClassifyRouteStep(DungeonRouteKind kind, uint32_t entry)
{
    if (entry == kPathAnchorEntry)
        return DungeonRouteNodeType::Travel;
    switch (kind)
    {
        case DungeonRouteKind::Boss:
        case DungeonRouteKind::HeroicOnly: return DungeonRouteNodeType::Boss;
        case DungeonRouteKind::Optional:
        case DungeonRouteKind::Required:   return DungeonRouteNodeType::Pull;
        case DungeonRouteKind::Event:      return DungeonRouteNodeType::Interaction;
        case DungeonRouteKind::Door:       return DungeonRouteNodeType::Door;
        case DungeonRouteKind::Skip:
        case DungeonRouteKind::Unknown:    return DungeonRouteNodeType::Travel;
    }
    return DungeonRouteNodeType::Travel;
}

// How much a route objective matters if it can't be completed. Only Optional content may ever
// be skipped; Required and Boss objectives are retried and, if still unresolved, end the run as
// partial - the route never advances past them.
enum class DungeonObjectiveRequirement : uint8_t
{
    Optional,
    Required,  // must be done, not a boss: `required` trash, a door
    Boss,
};

inline char const* ToString(DungeonObjectiveRequirement r)
{
    switch (r)
    {
        case DungeonObjectiveRequirement::Optional: return "optional";
        case DungeonObjectiveRequirement::Required: return "required";
        case DungeonObjectiveRequirement::Boss:     return "boss";
    }
    return "unknown";
}

// From the existing data: a `boss` row is a mandatory boss; everything else (optional trash,
// heroic-only, events, path anchors) is optional.
inline DungeonObjectiveRequirement ClassifyRequirement(DungeonRouteKind kind, uint32_t entry)
{
    if (entry == kPathAnchorEntry)
        return DungeonObjectiveRequirement::Optional;
    switch (kind)
    {
        case DungeonRouteKind::Boss:     return DungeonObjectiveRequirement::Boss;
        case DungeonRouteKind::Required:
        case DungeonRouteKind::Door:     return DungeonObjectiveRequirement::Required;  // nothing past it is reachable
        default:                         return DungeonObjectiveRequirement::Optional;
    }
}

#endif
