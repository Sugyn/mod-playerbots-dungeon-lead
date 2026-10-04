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
#include "DungeonLeadKernels.h"
#include "DungeonRouteMgr.h"
#include "NewRpgBaseAction.h"

class Creature;
struct DungeonPack;
struct DungeonPackSighting;
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
    // A session exists for this bot in any state (Starting, active or Stopping) - IsOn()
    // only becomes true once it is Active. Use this for "is this bot/group already busy".
    bool HasSession(PlayerbotAI* botAI);
    // Readiness (healer, deaths, resting, mana, leash, spread) lives in DungeonPartyState; this
    // is the one piece of it other code (the canary's "don't take over mid-pull") needs directly.
    bool GroupInCombat(PlayerbotAI* botAI);
    Creature* FindBossNear(PlayerbotAI* botAI, float range);

    // Route progress, shared by the route walk and the pull controller.
    // Current step done - move on. `confirmed`: it was cleared/reached (a wipe checkpoint), not
    // merely given up on.
    void AdvanceStep(DungeonLeadState& st, bool confirmed);
    // After a wipe: resume right after the checkpoint and re-observe everything about the current
    // step (packs reset when a party wipes). Records "checkpoint_restore".
    void RestoreCheckpoint(PlayerbotAI* botAI, DungeonLeadState& st);
    // Give up on the current step, recorded (never silent): a skipped mandatory step makes the
    // run Partial with the given failure domain/reason. Only FailObjective decides to skip.
    void SkipStep(DungeonLeadState& st, DungeonRouteStep const& step, DungeonFailureDomain domain,
                  DungeonFailureReason reason);
    // The current step's objective could not be completed (`why` for the record). Optional content
    // is skipped; a boss/required objective gets another round (step state reset) and, when out of
    // rounds, ends the run as partial - the route never advances past it. Returns true if the
    // session was stopped (the caller must not touch it any more).
    bool FailObjective(PlayerbotAI* botAI, DungeonLeadState& st, DungeonRouteStep const& step,
                       DungeonFailureDomain domain, DungeonFailureReason reason, std::string const& why);
    // Forget everything observed about the current step (pack, pull, target plan, anchor,
    // arrival, stuck baseline) so it is looked at afresh. Not the objective failure count.
    void ResetStepState(DungeonLeadState& st);
    // The only writer of DungeonLeadState::packId/packState (logs "pack_state" on change).
    void SetPackState(PlayerbotAI* botAI, DungeonLeadState& st, DungeonPack const& pack,
                      DungeonLeadKernel::PackState next);
    // Observe the current pack, update its state, lock its members once it engages, and (debug
    // mode) record how it was resolved. Used by the route walk and the pull controller alike.
    DungeonPackSighting TrackPack(PlayerbotAI* botAI, DungeonLeadState& st, DungeonPack const& pack);
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

    // Teleports party members whom death has stranded on another map back to the instance
    // entrance, so upstream's own corpse-run/resurrect chain can finish - it handles everything
    // except pathing to a corpse across a map boundary, which is what releasing inside a
    // graveyard-less dungeon forces it to attempt. See the definition for the measurements.
    // Called from GuardActiveSessions() on every tick of an active session.
    void RecoverStrandedMembers(PlayerbotAI* botAI, DungeonLeadState& st);

    // Re-validates the instance of party members who are in the leader's group and instance but
    // were marked invalid by AzerothCore after a group change (they would be sent out in 60 s).
    void KeepInstanceValid(PlayerbotAI* botAI);

    // Shared session-start logic behind both the "startdungeon" chat command and the AutoBot
    // Canary controller (DungeonLeadCanary.h) - leadership takeover, follower snapshot, strategy
    // application, state reset, the star icon, logging. Callers do their OWN preconditions first
    // (group/5-man/permission checks for the chat command; pure-bot/allowlist/concurrency checks
    // for the canary controller) since those differ by origin - this function assumes the caller
    // has already decided "yes, start here" and `bot` is already in `group`.
    // `master`: who to attribute/notify for a Manual session; pass nullptr for AutoCanary (nothing
    // asked permission, nothing to tell).
    // Returns true when the session was accepted: Active right away if `bot` already leads the
    // group, otherwise Starting until GuardActiveSessions() confirms the leadership transfer (or
    // gives up on it). False only when the bot or another member already has a session.
    bool StartSession(PlayerbotAI* botAI, Group* group, DungeonLeadSessionOrigin origin, Player* master,
                       bool testMode = false);
    // Ends an Active session (restores strategies, marks), then waits in Stopping until the
    // handback to the master is confirmed. A Starting session is dropped; a Stopping one is left
    // to finish.
    void Stop(PlayerbotAI* botAI, bool giveLeaderBack);

    // Leadership lifecycle internals, see StartSession()/Stop() and DungeonLeadBrain.
    void ActivateSession(PlayerbotAI* botAI, Group* group, Player* master, DungeonLeadKernel::TransitionReason reason);
    void RequestLeadership(Player* bot, DungeonLeadState& st);
    void ReconcileLeadership(PlayerbotAI* botAI);
    // Always-on structured logging to DungeonLeadSessions.csv (player, dungeon, tank, group,
    // event, detail) - see README "Debugging". Not gated behind "startdungeon debug".
    void RecordEvent(PlayerbotAI* botAI, std::string const& event, std::string const& detail);
    // "startdungeon debug" verbose dump (position/distances every wait), plain file, no logger config
    // dependency. Gated behind DungeonLeadState::debugMode, defaulting to
    // AiPlayerbot.DungeonLead.DebugDefault (0 for a fresh checkout of this patch).
    void RecordDebug(PlayerbotAI* botAI, std::string const& line);
    // Writes buffered telemetry (RecordEvent/RecordRunSummary/RecordDebug only queue lines) to the
    // files. World thread only: every world tick (self-throttled to ~2 s, sooner if the buffer
    // fills) and with `force` at shutdown.
    void FlushTelemetry(bool force = false);

    // Called before any player teleport (PlayerScript hook). False cancels it: a living bot of an
    // active session must not be moved out of the dungeon by another system mid-run (seen live:
    // the leader vanished from Shadowfang Keep and Wailing Caverns while the party was drinking).
    // Real players, the dead, teleports within the map, and a party whose real player has already
    // left are not touched. Every cancel is recorded as "unexpected_teleport" with its target and
    // the spell being cast, so the source can be found.
    bool AllowTeleport(Player* player, uint32 mapId, float x, float y, float z, uint32 options);

    // 2026-09-15 (independent architecture review, DL-006): DungeonLeadSessions.csv is a per-event
    // stream - reconstructing "how many runs actually reached Complete" means grouping potentially
    // thousands of interleaved rows by run_id and inferring the ending from whichever event came
    // last, which a restart or a crash can leave with no terminal row at all. This writes ONE row
    // to DungeonLeadRuns.csv at the point a run's outcome becomes final, so "how did run N end" is
    // a single lookup instead of a reconstruction. As of 2026-09-15 this is wired into every known
    // Stop()/left-instance/route-completion exit plus a hard-disconnect cleanup in
    // GuardActiveSessions() - the only gap left is the review's full state machine
    // (Requested->...->Closed) itself, out of scope for this pass.
    void RecordRunSummary(PlayerbotAI* botAI, std::string const& terminalReason);

    // Core the overload above delegates to (it just reads guid/name off a live Player*) - exposed
    // separately so GuardActiveSessions() can close out a session whose character object is already
    // gone (a hard disconnect), using DungeonLeadState::tankName cached at StartSession() instead of
    // a live GetName().
    void RecordRunSummary(ObjectGuid guid, std::string const& tankName, std::string const& terminalReason);
}

class DungeonLeadNextAction : public NewRpgBaseAction
{
public:
    DungeonLeadNextAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "dungeon lead next") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    DungeonRoute const* ResolveRoute(DungeonLeadState& st);
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
