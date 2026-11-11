/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadAccess.h
 *
 * mod-playerbots exposes no extension seam for new Strategies/Actions/Triggers: the base
 * AiObjectContext's shared registries are PRIVATE static members, populated once at startup by
 * hardcoded functions. This reaches their addresses without editing a single mod-playerbots
 * file, using the standard explicit-instantiation access bypass: naming a private member in an
 * explicit template instantiation argument is exempt from access control
 * ([temp.explicit]/[class.access.general]) - well-defined C++, not `#define private public`.
 * Silences GCC's -Wnon-template-friend locally, the only cost.
 *
 * The per-WoW-class AiObjectContext subclasses (WarriorAiObjectContext, ...) declare their OWN
 * SEPARATE shared registries, and those are PUBLIC static members - reached directly below, no
 * trick needed. Every real bot is built from its class's context, never the base one, so a
 * registration that only reaches the base lists reaches no bot at all.
 */

#ifndef MOD_DUNGEONLEAD_ACCESS_H
#define MOD_DUNGEONLEAD_ACCESS_H

#include "Action.h"
#include "AiObjectContext.h"
#include "NamedObjectContext.h"
#include "Strategy.h"
#include "Trigger.h"
#include "Value.h"

namespace dl_access
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#endif

    template <typename Tag, typename Tag::type Ptr>
    struct Robber
    {
        friend typename Tag::type get(Tag) { return Ptr; }
    };

    struct StrategyListTag
    {
        typedef SharedNamedObjectContextList<Strategy>* type;
        friend type get(StrategyListTag);
    };
    template struct Robber<StrategyListTag, &AiObjectContext::sharedStrategyContexts>;

    struct ActionListTag
    {
        typedef SharedNamedObjectContextList<Action>* type;
        friend type get(ActionListTag);
    };
    template struct Robber<ActionListTag, &AiObjectContext::sharedActionContexts>;

    struct TriggerListTag
    {
        typedef SharedNamedObjectContextList<Trigger>* type;
        friend type get(TriggerListTag);
    };
    template struct Robber<TriggerListTag, &AiObjectContext::sharedTriggerContexts>;

    struct ValueListTag
    {
        typedef SharedNamedObjectContextList<UntypedValue>* type;
        friend type get(ValueListTag);
    };
    template struct Robber<ValueListTag, &AiObjectContext::sharedValueContexts>;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    inline SharedNamedObjectContextList<Strategy>& BaseStrategyContexts() { return *get(StrategyListTag()); }
    inline SharedNamedObjectContextList<Action>& BaseActionContexts() { return *get(ActionListTag()); }
    inline SharedNamedObjectContextList<Trigger>& BaseTriggerContexts() { return *get(TriggerListTag()); }
    inline SharedNamedObjectContextList<UntypedValue>& BaseValueContexts() { return *get(ValueListTag()); }

}  // namespace dl_access

#endif
