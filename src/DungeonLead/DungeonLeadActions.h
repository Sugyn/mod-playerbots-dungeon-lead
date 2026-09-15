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

    // One-shot diagnostic (2026-09-12 night's 49-party scale test): runs the EXACT same
    // PathGenerator call MoveRouteTo() uses in production, from `bot`'s current position to
    // (dx, dy, dz), and dumps path type + actual end position + a sample of waypoints - to see
    // directly why a bot stuck near the Lady Anacondra/Kresh floor never made progress toward
    // Verdan the Everliving/Lord Serpentis's platform (~50-90 units higher in Z), instead of
    // guessing at a fix from telemetry alone. Not wired into any automatic path - console/SOAP
    // only, via ".playerbots pathcheck".
    std::string DiagnosePath(Player* bot, float dx, float dy, float dz);

    // Reconciliation loop (see the big comment at its call site in Playerbots.cpp / its definition
    // in DungeonLeadActions.cpp): continuously re-asserts the desired strategy state for every
    // active session, for as long as it stays active, to heal an external AI reset regardless of
    // how long it takes to actually land. Called from PlayerbotsWorldScript::OnUpdate - not tied
    // to any particular bot's own Strategy/Engine state (which is exactly what can get wiped).
    void GuardActiveSessions();

    // Shared session-start logic behind both the "startdungeon" chat command and the AutoBot
    // Canary controller (DungeonLeadCanary.h) - leadership takeover, follower snapshot, strategy
    // application, state reset, the star icon, logging. Callers do their OWN preconditions first
    // (group/5-man/permission checks for the chat command; pure-bot/allowlist/concurrency checks
    // for the canary controller) since those differ by origin - this function assumes the caller
    // has already decided "yes, start here" and `bot` is already in `group`.
    // `master`: who to attribute/notify for a Manual session; pass nullptr for AutoCanary (nothing
    // asked permission, nothing to tell).
    bool StartSession(PlayerbotAI* botAI, Group* group, DungeonLeadSessionOrigin origin, Player* master,
                       bool testMode = false);
    void Stop(PlayerbotAI* botAI, bool giveLeaderBack);
    // Always-on structured logging to DungeonLeadSessions.csv (player, dungeon, tank, group,
    // event, detail) - see README "Debugging". Not gated behind "startdungeon debug".
    void RecordEvent(PlayerbotAI* botAI, std::string const& event, std::string const& detail);
    // "startdungeon debug" verbose dump (position/distances every wait), plain file, no logger config
    // dependency. Gated behind DungeonLeadState::debugMode, defaulting to
    // AiPlayerbot.DungeonLead.DebugDefault (0 for a fresh checkout of this patch).
    void RecordDebug(PlayerbotAI* botAI, std::string const& line);

    // 2026-09-15 (independent architecture review, DL-006): DungeonLeadSessions.csv is a per-event
    // stream - reconstructing "how many runs actually reached Complete" means grouping potentially
    // thousands of interleaved rows by run_id and inferring the ending from whichever event came
    // last, which a restart or a crash can leave with no terminal row at all. This writes ONE row
    // to DungeonLeadRuns.csv at the point a run's outcome becomes final, so "how did run N end" is
    // a single lookup instead of a reconstruction. Not exhaustive - only wired into the two most
    // common terminal paths (normal route-completion and the canary timeout), not every Stop()/
    // left-instance exit; a real fix needs the review's full state machine (Requested->...->
    // Closed), out of scope for this pass.
    void RecordRunSummary(PlayerbotAI* botAI, std::string const& terminalReason);
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

// "canarytest <lfgId>" (Stage 2, see DungeonLeadCanary.h): GM-only, on-demand equivalent of
// waiting for CanaryTick() to spot the right bots organically. Whichever bot is whispered is just
// the entry point - it does not itself join anything, it dispatches DungeonLead::TriggerTargetedTest()
// which acts on other, currently-idle bots server-wide.
class CanaryTestChatShortcutAction : public Action
{
public:
    CanaryTestChatShortcutAction(PlayerbotAI* botAI) : Action(botAI, "canarytest chat shortcut") {}

    bool Execute(Event event) override;
};

#endif
