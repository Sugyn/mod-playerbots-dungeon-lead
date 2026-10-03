/*
 * mod-dungeon-lead - DungeonLeadOverrides.h
 *
 * The two points where the old patch changed EXISTING mod-playerbots behavior rather than
 * adding something new - can't be done via a plain registry append, but both target methods
 * are `virtual`, so a subclass + a registry-entry OVERWRITE (same creators map, same key,
 * `Add()` applied after the stock registration simply replaces it - see
 * SharedNamedObjectContextList::Add() in NamedObjectContext.h) reaches both without editing
 * mod-playerbots:
 *
 * - "formation" (Value) -> stock FormationValue::Load() doesn't know "leader". Subclass adds
 *   the one case and delegates everything else to the base implementation.
 * - "unknown dungeon" (Trigger) -> stock UnknownDungeonTrigger::IsActive() hands control back
 *   to the real player whenever the bot is in a dungeon with a live master, which would keep
 *   undoing "dungeon lead"/"grind". Subclass short-circuits to false while either is active,
 *   defers to the base implementation otherwise.
 */

#ifndef MOD_DUNGEONLEAD_OVERRIDES_H
#define MOD_DUNGEONLEAD_OVERRIDES_H

#include "Formations.h"
#include "LfgTriggers.h"
#include "Playerbots.h"

class DungeonLeadLeaderFormation : public FollowFormation
{
public:
    DungeonLeadLeaderFormation(PlayerbotAI* botAI) : FollowFormation(botAI, "leader") {}

    std::string const GetTargetName() override { return "group leader"; }
};

class DungeonLeadFormationValue : public FormationValue
{
public:
    using FormationValue::FormationValue;

    bool Load(std::string const val) override
    {
        if (val == "leader")
        {
            if (value)
                delete value;

            value = new DungeonLeadLeaderFormation(botAI);
            return true;
        }

        return FormationValue::Load(val);
    }
};

class DungeonLeadAwareUnknownDungeonTrigger : public UnknownDungeonTrigger
{
public:
    using UnknownDungeonTrigger::UnknownDungeonTrigger;

    bool IsActive() override
    {
        if (botAI->HasStrategy("dungeon lead", BOT_STATE_NON_COMBAT))
            return false;
        if (botAI->HasStrategy("grind", BOT_STATE_NON_COMBAT))
            return false;

        return UnknownDungeonTrigger::IsActive();
    }
};

#endif
