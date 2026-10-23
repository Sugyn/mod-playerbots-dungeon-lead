/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadOverrides.h
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
 * - "unknown dungeon" (Trigger) -> stock UnknownDungeonTrigger::IsActive() fires "give leader
 *   in dungeon" (group leadership back to the real master) whenever a group-leading bot is in a
 *   dungeon with a live master, which would undo the session's leadership. Subclass
 *   short-circuits to false while this bot leads a Dungeon Lead session (any state - Starting,
 *   active, Stopping: the session acquires and hands back leadership itself) and defers to the
 *   base implementation otherwise. Followers never lead the group, so the stock action does
 *   nothing for them either way. DLN-010: this used to also suppress the trigger for any bot
 *   with "grind" - every grinding bot on the server, session or not.
 */

#ifndef MOD_DUNGEONLEAD_OVERRIDES_H
#define MOD_DUNGEONLEAD_OVERRIDES_H

#include "DungeonLead/DungeonLeadActions.h"
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
        if (DungeonLead::HasSession(botAI))
            return false;

        return UnknownDungeonTrigger::IsActive();
    }
};

#endif
