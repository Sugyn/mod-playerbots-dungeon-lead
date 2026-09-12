/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DUNGEONLEADACTIONS_H
#define PLAYERBOTS_DUNGEONLEADACTIONS_H

#include "ChatShortcutActions.h"
#include "DungeonRouteMgr.h"
#include "NewRpgBaseAction.h"

class Creature;
class PlayerbotAI;
class Player;

// "Dungeon lead": a bot (normally the tank) in a group WITH a real player walks the hand-authored
// route of the current 5-man dungeon (playerbots_dungeon_route), pulling with "grind" on the way,
// marking bosses/CC targets and holding back when the healer is low on mana or the group is spread.
// Everything lives in the non-combat engine; combat itself is handled by the existing class AI.
namespace DungeonLead
{
    bool InFiveMan(Player* bot);
    bool IsOn(PlayerbotAI* botAI);
    bool GroupInCombat(PlayerbotAI* botAI);
    bool GroupResting(PlayerbotAI* botAI);
    bool HealerManaLow(PlayerbotAI* botAI);
    bool GroupTooSpread(PlayerbotAI* botAI);
    Creature* FindBossNear(PlayerbotAI* botAI, float range);
    void Stop(PlayerbotAI* botAI, bool giveLeaderBack);
}

class DungeonLeadNextAction : public NewRpgBaseAction
{
public:
    DungeonLeadNextAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "dungeon lead next") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    DungeonRoute const* ResolveRoute(DungeonLeadState& st);
    void MarkVisited(DungeonLeadState& st);
    bool MoveRouteTo(DungeonLeadState& st, WorldPosition const& dest, DungeonRouteStep const& step);
};

class DungeonLeadMarkAction : public Action
{
public:
    DungeonLeadMarkAction(PlayerbotAI* botAI) : Action(botAI, "dungeon lead mark") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    Creature* FindCcCandidate(Creature* boss);
};

class DungeonLeadStopAction : public Action
{
public:
    DungeonLeadStopAction(PlayerbotAI* botAI) : Action(botAI, "dungeon lead stop") {}

    bool Execute(Event event) override;
};

class StartDungChatShortcutAction : public PositionsResetAction
{
public:
    StartDungChatShortcutAction(PlayerbotAI* botAI) : PositionsResetAction(botAI, "startdung chat shortcut") {}

    bool Execute(Event event) override;
};

class StopDungChatShortcutAction : public PositionsResetAction
{
public:
    StopDungChatShortcutAction(PlayerbotAI* botAI) : PositionsResetAction(botAI, "stopdung chat shortcut") {}

    bool Execute(Event event) override;
};

#endif
