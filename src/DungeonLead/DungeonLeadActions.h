/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
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
    Player* FindSpreadMember(PlayerbotAI* botAI);  // who's causing GroupTooSpread, for messaging
    bool MasterTooFar(PlayerbotAI* botAI);
    bool MasterUnavailable(PlayerbotAI* botAI);
    Creature* FindBossNear(PlayerbotAI* botAI, float range);
    void CheckCcMark(PlayerbotAI* botAI);
    void Stop(PlayerbotAI* botAI, bool giveLeaderBack);
    // Always-on structured logging to DungeonLeadSessions.csv (player, dungeon, tank, group,
    // event, detail) - see README "Debugging". Not gated behind "startdungeon debug".
    void RecordEvent(PlayerbotAI* botAI, std::string const& event, std::string const& detail);
    // "startdungeon debug" verbose dump (position/distances every wait), plain file, no logger config
    // dependency. Gated behind DungeonLeadState::debugMode, defaulting to
    // AiPlayerbot.DungeonLead.DebugDefault (0 for a fresh checkout of this patch).
    void RecordDebug(PlayerbotAI* botAI, std::string const& line);
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

// Releases a moon (CC) mark nobody manages to actually crowd-control within CcTimeoutSeconds.
// Its own trigger must fire regardless of combat state: while the marked target is alive and
// fighting anyone in the group (even just a shaman's totem), the tank's own IsInCombat() stays
// true via shared combat tagging, so this can NOT live inside DungeonLeadNextAction::isUseful()
// (which bails out on IsInCombat()) - that left CC marks stuck forever whenever nobody in the
// group could actually cast the crowd control, since the mob just sat there excluded from DPS
// targeting until the game session ended.
class DungeonLeadCcWatchAction : public Action
{
public:
    DungeonLeadCcWatchAction(PlayerbotAI* botAI) : Action(botAI, "dungeon lead cc watch") {}

    bool Execute(Event event) override;
    bool isUseful() override;
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
    StartDungChatShortcutAction(PlayerbotAI* botAI) : PositionsResetAction(botAI, "startdungeon chat shortcut") {}

    bool Execute(Event event) override;
};

class StopDungChatShortcutAction : public PositionsResetAction
{
public:
    StopDungChatShortcutAction(PlayerbotAI* botAI) : PositionsResetAction(botAI, "stopdungeon chat shortcut") {}

    bool Execute(Event event) override;
};

#endif
