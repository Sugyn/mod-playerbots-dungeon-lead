/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadActions.h"
#include "DungeonLeadCanary.h"

#include "Creature.h"
#include "CreatureData.h"
#include "Event.h"
#include "Formations.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "RtiTargetValue.h"
#include "Log.h"
#include "Timer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <list>
#include <mutex>
#include <sstream>

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------
namespace
{
    constexpr float kPathFinderDis = 70.0f;     // below this, hand the destination straight to mmaps
    constexpr float kBossSearchRange = 60.0f;
    constexpr float kCcSearchRange = 40.0f;
    constexpr float kCreatureProbeRange = 150.0f;

    Unit* IconUnit(PlayerbotAI* botAI, Group* group, uint8 index)
    {
        ObjectGuid guid = group->GetTargetIcon(index);
        return guid.IsEmpty() ? nullptr : botAI->GetUnit(guid);
    }

    bool RouteHasEntry(PlayerbotAI* botAI, uint32 entry)
    {
        DungeonLeadState& st = sDungeonRouteMgr.State(botAI->GetBot()->GetGUID());
        DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
        if (!route)
            return false;
        for (DungeonRouteStep const& s : route->steps)
            if (s.entry == entry)
                return true;
        return false;
    }

    // Always-on structured data collection (no toggle, no dependency on worldserver.conf logger
    // config being right) - one CSV row per event, plain fopen/fprintf so it works the same way
    // for every server this patch runs on. Meant to be attached to a bug report as-is.
    //
    // Real RFC 4180 escaping: every field written by RecordEvent() is already wrapped in literal
    // quotes by its format string, so the only transformation needed here is doubling embedded
    // quotes. Commas/newlines inside a quoted field are valid CSV and don't need mangling (an
    // earlier version replaced them with ';', silently corrupting boss/player names containing one).
    std::string CsvEscape(std::string const& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"')
                out += "\"\"";
            else
                out += c;
        }
        return out;
    }

    std::string GroupMemberList(Player* bot)
    {
        std::ostringstream out;
        Group* group = bot->GetGroup();
        if (!group)
            return "";
        bool first = true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member)
                continue;
            if (!first)
                out << "|";
            first = false;
            out << member->GetName();
        }
        return out.str();
    }

    // Both log files are written from any bot's own AI update, which can run on any map-update
    // thread - fopen/fprintf/fclose racing across threads can interleave partial lines, and the
    // old `static bool headerWritten` was itself an unguarded data race. One mutex serializes both
    // files (they're never on the hot path - one line per key event, not per tick).
    std::mutex g_dungeonLeadLogMutex;

    // Session correlation id: every telemetry row from one "startdungeon" run carries the same
    // value, so a bug report's CSV rows (which interleave every dungeon-lead bot on the whole
    // server) can be grouped back into one run without guessing from timestamps.
    //
    // 2026-09-15 (independent architecture review, DL-006 - "telemetry cannot yield an
    // authoritative run result"): this used to restart at 1 every worldserver restart while the
    // CSV file itself keeps appending across restarts (it's a plain fopen(...,"a"), never
    // truncated) - run 1 from today's first process and run 1 from the process after a crash
    // restart were indistinguishable in the same file, and the review's own dashboard-correctness
    // reasoning depends on run_id actually being unique. Seeded from the process start time
    // (seconds since epoch, left-shifted to leave room for up to ~1M runs/second before two
    // processes that started in the same second could theoretically collide - nowhere close to
    // this project's real scale) instead of a fixed 1, so two different worldserver processes
    // essentially never hand out the same run_id. Still not a real globally-unique ID scheme
    // (no commit_sha/campaign_id/scenario_id columns yet - that's the rest of DL-006, not done
    // here), but the single most load-bearing part - two runs never being confusable as the same
    // run - now actually holds.
    std::atomic<uint64> g_nextRunId{static_cast<uint64>(time(nullptr)) << 20};
    uint64 NextRunId() { return g_nextRunId.fetch_add(1); }

    std::string FormatLogTimestamp()
    {
        time_t now = time(nullptr);
        struct tm tmBuf {};
#ifdef _WIN32
        localtime_s(&tmBuf, &now);
#else
        localtime_r(&now, &tmBuf);
#endif
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmBuf);
        return ts;
    }

    // "startdungeon"/"stopdungeon" exact session restore: snapshot each follower's formation and
    // active strategies before dungeon-lead touches them, so stopping the run can put them back
    // exactly instead of to a generic default (chaos formation, whatever strategies happened to
    // still be set after dungeon-lead's own +/- deltas).
    DungeonLeadMemberSnapshot SnapshotMember(Player* member, PlayerbotAI* memberAI)
    {
        DungeonLeadMemberSnapshot snap;
        snap.guid = member->GetGUID();
        if (memberAI->GetAiObjectContext())
            if (FormationValue* fv = dynamic_cast<FormationValue*>(
                    memberAI->GetAiObjectContext()->GetValue<Formation*>("formation")))
                snap.formation = fv->Save();
        snap.nonCombatStrategies = memberAI->GetStrategies(BOT_STATE_NON_COMBAT);
        snap.combatStrategies = memberAI->GetStrategies(BOT_STATE_COMBAT);
        return snap;
    }

    // Builds a ChangeStrategy delta ("+missing,-extra") that turns `current` into `target`, empty
    // if they already match.
    std::string StrategyDelta(std::vector<std::string> const& current, std::vector<std::string> const& target)
    {
        std::string delta;
        for (std::string const& name : target)
            if (std::find(current.begin(), current.end(), name) == current.end())
                delta += "+" + name + ",";
        for (std::string const& name : current)
            if (std::find(target.begin(), target.end(), name) == target.end())
                delta += "-" + name + ",";
        if (!delta.empty())
            delta.pop_back();  // trailing comma
        return delta;
    }

    void RestoreMember(PlayerbotAI* memberAI, DungeonLeadMemberSnapshot const& snap)
    {
        std::string delta = StrategyDelta(memberAI->GetStrategies(BOT_STATE_NON_COMBAT), snap.nonCombatStrategies);
        if (!delta.empty())
            memberAI->ChangeStrategy(delta, BOT_STATE_NON_COMBAT);

        delta = StrategyDelta(memberAI->GetStrategies(BOT_STATE_COMBAT), snap.combatStrategies);
        if (!delta.empty())
            memberAI->ChangeStrategy(delta, BOT_STATE_COMBAT);

        if (!snap.formation.empty() && memberAI->GetAiObjectContext())
            if (FormationValue* fv = dynamic_cast<FormationValue*>(
                    memberAI->GetAiObjectContext()->GetValue<Formation*>("formation")))
                if (fv->Save() != snap.formation)
                    fv->Load(snap.formation);
    }

    // The actual desired strategy state for an active dungeon-lead session: followers on "leader"
    // formation with +follow/+cc, the leader itself on +dungeon lead/+grind/+cc/+mark rti.
    // Safe to call repeatedly - both at "startdungeon" itself and from the reconciliation loop
    // (GuardActiveSessions) that keeps reasserting it for as long as the session is active - but
    // NOT a free no-op upstream (see DL-020): ChangeStrategy()/FormationValue::Load() remove and
    // reinitialize engines even when the desired state already matches, so this function only
    // mutates what logWipeDetection's own comparison found actually wiped.
    // logWipeDetection: only meaningful when called from the reconciliation loop (GuardActiveSessions)
    // - checks and logs, per bot, whether it actually needed fixing, and mutates ONLY the pieces
    // (formation / follower strategy / leader strategy) that did, so a healed external reset is
    // directly observable in the log instead of only inferable by elimination, and a stable session
    // stops paying for engine rebuilds it doesn't need every ~2s.
    // Off at "startdungeon" itself, where everyone is expected to need the full application anyway.
    void ApplyLeaderFollowerStrategies(PlayerbotAI* botAI, Group* group, bool logWipeDetection = false)
    {
        Player* bot = botAI->GetBot();
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot)
                continue;
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            if (!memberAI || !memberAI->GetAiObjectContext())
                continue;

            FormationValue* fv = dynamic_cast<FormationValue*>(
                memberAI->GetAiObjectContext()->GetValue<Formation*>("formation"));
            // 2026-09-15 (independent architecture review DL-020 - "periodic mutation, not
            // idempotent repair"): logWipeDetection already computed formationWiped/strategyWiped
            // to decide whether to LOG a restore, then reapplied both unconditionally regardless of
            // the answer - ChangeStrategy()/fv->Load() are not free no-ops upstream (they remove and
            // reinitialize engines even when nothing actually changed), so a stable session with N
            // active parties was rewriting every follower's formation and strategy every ~2s for no
            // reason. Skip each mutation when its own wiped flag is false - startdungeon itself
            // (logWipeDetection=false) is untouched, everyone there is expected to need the full
            // apply anyway.
            bool formationWiped = fv && fv->Save() != "leader";
            bool strategyWiped = !memberAI->HasStrategy("follow", BOT_STATE_NON_COMBAT);
            if (logWipeDetection && (formationWiped || strategyWiped))
            {
                LOG_INFO("playerbots.dungeonlead",
                         "[DungeonLead] follower {} was missing formation/strategy (external AI "
                         "reset) - reconciliation loop restoring it (formation={} strategy={})",
                         member->GetName(), formationWiped, strategyWiped);
                DungeonLead::RecordEvent(botAI, "strategy_restored", "follower=" + member->GetName());
            }
            if (fv && (!logWipeDetection || formationWiped))
                fv->Load("leader");
            if (!logWipeDetection || strategyWiped)
            {
                memberAI->ChangeStrategy("+follow,-passive,-stay,-grind,-dungeon lead", BOT_STATE_NON_COMBAT);
                memberAI->ChangeStrategy("+cc", BOT_STATE_COMBAT);
            }
        }
        // Same DL-020 guard for the leader's own two lines: only reassert if the reconciliation
        // loop actually found it missing. HasStrategy("dungeon lead", ...) mirrors DungeonLead::IsOn
        // exactly (see its declaration) - the leader's non-combat strategy is the canonical "is this
        // session actually active" signal.
        bool leaderStrategyWiped = !botAI->HasStrategy("dungeon lead", BOT_STATE_NON_COMBAT);
        if (logWipeDetection && leaderStrategyWiped)
        {
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead] leader {} was missing its own strategy (external AI reset) - "
                     "reconciliation loop restoring it", bot->GetName());
            DungeonLead::RecordEvent(botAI, "strategy_restored", "leader=" + std::string(bot->GetName()));
        }
        if (!logWipeDetection || leaderStrategyWiped)
        {
            botAI->ChangeStrategy("+dungeon lead,+grind,-passive,-stay", BOT_STATE_NON_COMBAT);
            botAI->ChangeStrategy("+dungeon lead,+cc,+mark rti", BOT_STATE_COMBAT);
        }
    }

    // Equivalent to PositionsResetAction::ResetReturnPosition()/ResetStayPosition() (see
    // ChatShortcutActions.cpp) reimplemented as a free function: StartSession() is shared between
    // the chat-command Action (which has that base class) and the AutoBot Canary controller (a
    // plain namespace function, no Action to inherit it from). Same AiObjectContext "position"
    // value both ways - a leftover stay/return position (e.g. from RPG wandering right before the
    // canary controller picked this bot) must not fight the leader's own movement.
    void ResetPositions(PlayerbotAI* botAI)
    {
        PositionMap& posMap = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
        for (char const* key : {"return", "stay"})
        {
            PositionInfo pos = posMap[key];
            pos.Reset();
            posMap[key] = pos;
        }
    }
}

void DungeonLead::RecordEvent(PlayerbotAI* botAI, std::string const& event, std::string const& detail)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    Player* master = botAI->GetMaster();
    DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;

    // gather everything before taking the log mutex - keep the critical section to the file I/O
    std::string const ts = FormatLogTimestamp();
    std::string const masterName = CsvEscape(master ? master->GetName() : "?");
    std::string const dungeonName = CsvEscape(route ? route->name : "?");
    std::string const botName = CsvEscape(bot->GetName());
    std::string const members = CsvEscape(GroupMemberList(bot));
    std::string const eventEsc = CsvEscape(event);
    std::string const detailEsc = CsvEscape(detail);
    uint32 const lfgId = st.lfgId;
    uint64 const runId = st.runId;
    char const* outcome = ToString(st.outcome);
    char const* failureDomain = ToString(st.failureDomain);
    char const* failureReason = ToString(st.failureReason);

    std::lock_guard<std::mutex> lock(g_dungeonLeadLogMutex);
    static bool headerWritten = false;
    FILE* f = fopen("DungeonLeadSessions.csv", "a");
    if (!f)
        return;
    if (!headerWritten)
    {
        // cheap check: a fresh/empty file needs the header, a pre-existing one from an earlier
        // run of this same worldserver process already has it
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0)
            fprintf(f, "timestamp,run_id,player,lfg_id,dungeon,tank,group_members,event,detail,outcome,"
                       "failure_domain,failure_reason\n");
        headerWritten = true;
    }

    fprintf(f, "%s,%llu,\"%s\",%u,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n", ts.c_str(),
            static_cast<unsigned long long>(runId), masterName.c_str(), lfgId, dungeonName.c_str(), botName.c_str(),
            members.c_str(), eventEsc.c_str(), detailEsc.c_str(), outcome, failureDomain, failureReason);
    fflush(f);
    fclose(f);
}

void DungeonLead::RecordRunSummary(PlayerbotAI* botAI, std::string const& terminalReason)
{
    Player* bot = botAI->GetBot();
    DungeonLead::RecordRunSummary(bot->GetGUID(), bot->GetName(), terminalReason);
}

// 2026-09-15 (independent architecture review DL-006 - the last silent exit path: a bot that hard-
// disconnects runs no Stop() call site at all, so none of the other four RecordRunSummary() call
// sites ever fire for it): the GUID/name-based core those all secretly needed anyway (the
// PlayerbotAI* overload above just reads both off a live Player*) - lets
// GuardActiveSessions() close out a session whose character object is already gone, using the
// name cached in DungeonLeadState::tankName at StartSession() time instead of a live GetName().
void DungeonLead::RecordRunSummary(ObjectGuid guid, std::string const& tankName, std::string const& terminalReason)
{
    DungeonLeadState& st = sDungeonRouteMgr.State(guid);
    DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;

    std::string const ts = FormatLogTimestamp();
    std::string const dungeonName = CsvEscape(route ? route->name : "?");
    std::string const botName = CsvEscape(tankName);
    std::string const reasonEsc = CsvEscape(terminalReason);
    uint32 const lfgId = st.lfgId;
    uint64 const runId = st.runId;
    char const* outcome = ToString(st.outcome);
    char const* failureDomain = ToString(st.failureDomain);
    char const* failureReason = ToString(st.failureReason);
    size_t const skipped = st.skippedSteps.size();
    uint32 const durationMs = st.sessionStartTs ? GetMSTimeDiffToNow(st.sessionStartTs) : 0;
    char const* origin = ToString(st.origin);

    std::lock_guard<std::mutex> lock(g_dungeonLeadLogMutex);
    static bool headerWritten = false;
    FILE* f = fopen("DungeonLeadRuns.csv", "a");
    if (!f)
        return;
    if (!headerWritten)
    {
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0)
            fprintf(f, "ended_at,run_id,tank,lfg_id,dungeon,origin,outcome,failure_domain,failure_reason,"
                       "skipped_steps,duration_ms,terminal_reason\n");
        headerWritten = true;
    }
    fprintf(f, "%s,%llu,\"%s\",%u,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%zu,%u,\"%s\"\n", ts.c_str(),
            static_cast<unsigned long long>(runId), botName.c_str(), lfgId, dungeonName.c_str(), origin,
            outcome, failureDomain, failureReason, skipped, durationMs, reasonEsc.c_str());
    fflush(f);
    fclose(f);
}

void DungeonLead::RecordDebug(PlayerbotAI* /*botAI*/, std::string const& line)
{
    std::string const ts = FormatLogTimestamp();

    std::lock_guard<std::mutex> lock(g_dungeonLeadLogMutex);
    FILE* f = fopen("DungeonLeadDebug.log", "a");
    if (!f)
        return;
    fprintf(f, "%s %s\n", ts.c_str(), line.c_str());
    fflush(f);
    fclose(f);
}

bool DungeonLead::InFiveMan(Player* bot)
{
    Map* map = bot ? bot->GetMap() : nullptr;
    return map && map->IsNonRaidDungeon();
}

bool DungeonLead::IsOn(PlayerbotAI* botAI)
{
    return botAI && botAI->HasStrategy("dungeon lead", BOT_STATE_NON_COMBAT);
}

bool DungeonLead::GroupInCombat(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return bot->IsInCombat();

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMap() != bot->GetMap())
            continue;
        if (member->IsInCombat())
            return true;
    }
    return false;
}

bool DungeonLead::GroupResting(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMap() != bot->GetMap())
            continue;
        // bots sit down to eat/drink; a real player sitting is treated the same (AFK-ish)
        if (member->IsSitState())
            return true;
    }
    return false;
}

bool DungeonLead::HealerManaLow(PlayerbotAI* botAI)
{
    Unit* healer = botAI->GetAiObjectContext()->GetValue<Unit*>("healer low mana")->Get();
    if (!healer)
        return false;
    return healer->GetPowerPct(POWER_MANA) < float(sPlayerbotAIConfig.dungeonLeadHealerManaPct);
}

// The real player is part of the run contract (see the architecture roadmap's L0 closeout): dead,
// disconnected, or no longer in the group at all must block new pulls and route advancement, not
// just "wait for them to catch up" the way a merely-far-away-but-fine player does. A dead player
// still needs to release/res/run back before the dungeon should continue without them.
bool DungeonLead::MasterUnavailable(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    if (!master || master == bot)
        return false;  // no real player assigned as master - nothing to block on
    if (!master->IsInWorld() || !master->GetSession())
        return true;  // logged off / disconnected mid-session
    if (!master->IsAlive())
        return true;
    if (Group* group = bot->GetGroup())
        if (!group->IsMember(master->GetGUID()))
            return true;  // left the party entirely
    return false;
}

bool DungeonLead::MasterTooFar(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Player* master = botAI->GetMaster();
    if (!master || master == bot || !master->IsAlive())
        return false;
    // left the instance/teleported away/logged off elsewhere entirely - that is "too far" by any
    // reasonable reading of a leash, not an exemption from it (a same-map-only check let the bot
    // wander off freely the instant the real player wasn't literally on the same map anymore)
    if (master->GetMap() != bot->GetMap())
        return true;
    return bot->GetDistance(master) > sPlayerbotAIConfig.dungeonLeadLeash;
}

// The farthest-behind group member past the spread threshold, if any - named so isUseful() can
// tell the player WHO it's waiting for, not just that the group is "too spread" (a silent block
// with no obvious cause was reported during live testing: the tank was actually correctly waiting
// on one specific bot the whole time, but nothing in chat said so).
Player* DungeonLead::FindSpreadMember(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    float const threshold = sPlayerbotAIConfig.dungeonLeadLeash * 1.5f;

    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Player* worst = nullptr;
    float worstDist = threshold;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMap() != bot->GetMap())
            continue;
        float d = bot->GetDistance(member);
        if (d > worstDist)
        {
            worstDist = d;
            worst = member;
        }
    }
    return worst;
}

bool DungeonLead::GroupTooSpread(PlayerbotAI* botAI)
{
    // never run away from the real player
    return MasterTooFar(botAI) || FindSpreadMember(botAI) != nullptr;
}

Creature* DungeonLead::FindBossNear(PlayerbotAI* botAI, float range)
{
    Player* bot = botAI->GetBot();
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();

    Creature* best = nullptr;
    float bestDist = range;
    for (ObjectGuid const& guid : targets)
    {
        Creature* c = botAI->GetCreature(guid);
        if (!c || !c->IsAlive() || !c->IsInWorld())
            continue;

        CreatureTemplate const* ct = c->GetCreatureTemplate();
        bool isBoss = c->IsDungeonBoss() || (ct && ct->rank == CREATURE_ELITE_WORLDBOSS) ||
                      RouteHasEntry(botAI, c->GetEntry());
        if (!isBoss)
            continue;

        float d = bot->GetDistance(c);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

void DungeonLead::CheckCcMark(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return;

    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (st.ccGuid.IsEmpty())
        return;

    Creature* c = botAI->GetCreature(st.ccGuid);
    if (!c || !c->IsAlive())
    {
        // dead/gone - our job is done, free the icon for whatever comes next. Logged (unlike the
        // original version of this branch): found live (2026-09-13) that a silent clear here reads
        // indistinguishably from a genuine multi-minute stall in the CSV/log - a moon mark that
        // never resolved via the timeout path below and never showed as a stuck party either just
        // looks like nothing is happening, when really the target quietly died to something else
        // (cleave, an add, whatever) and cleared itself here with no trace.
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} cc target gone (dead/despawned), clearing mark", bot->GetName());
        DungeonLead::RecordEvent(botAI, "cc_target_gone", "");
        if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
        st.ccGuid.Clear();
        return;
    }

    if (c->HasBreakableByDamageCrowdControlAura())
    {
        // actually crowd controlled right now: reset the grace window so a later break gets a
        // fresh chance instead of instantly expiring. Logged once per landing (not every tick
        // it stays up) so the CSV can distinguish "never landed at all" from "landed, then broke
        // and was never reapplied" - the two have different causes (the first is a casting/
        // targeting problem, the second is more likely something damaging it early, e.g. cleave/
        // AoE splash from the party's own attacks on an adjacent target - raised live 2026-09-13).
        if (!st.ccLandedTold)
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC landed on {}", bot->GetName(), c->GetName());
            DungeonLead::RecordEvent(botAI, "cc_landed", c->GetName());
            st.ccLandedTold = true;
        }
        st.ccMarkedTs = getMSTime();
        return;
    }

    // Absolute ceiling, independent of the combat-aware wait below: if this creature has been
    // marked for a long time and STILL never got crowd controlled - whether because it's stuck
    // waiting to enter combat (see below) or because it did enter combat and the timeout window
    // just kept getting hit - stop waiting on it unconditionally. Without this, a candidate that
    // never aggros onto anyone (picked by proximity to the boss, not confirmed to actually be part
    // of the pull) would hold the grace window open forever under the combat-aware wait below,
    // permanently excluding it from normal DPS targeting for no reason - worse than the original
    // "released too early" bug this was meant to fix.
    if (GetMSTimeDiffToNow(st.ccMarkedAbsoluteTs) >= sPlayerbotAIConfig.dungeonLeadCcAbsoluteTimeoutSeconds * IN_MILLISECONDS)
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC on {} never landed within the absolute ceiling ({}s since marked), releasing mark",
                 bot->GetName(), c->GetName(), sPlayerbotAIConfig.dungeonLeadCcAbsoluteTimeoutSeconds);
        DungeonLead::RecordEvent(botAI, "cc_released", c->GetName());
        if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
        st.ccGuid.Clear();
        return;
    }

    // The base mod-playerbots CC pipeline (CcTargetValue::Calculate -> TargetValue::FindTarget)
    // only ever considers creatures already in the bot's own "attackers" list - confirmed by
    // reading TargetValue::FindTarget directly, not assumed - so a moon-marked creature that
    // isn't yet fighting anyone is invisible to it regardless of the RTI icon. DungeonLeadMarkAction
    // marks a CC candidate as soon as one is found near the skulled boss (by proximity, via
    // "possible targets"), which is routinely before that specific creature has aggroed onto
    // anyone - so a chunk, and on a slow approach the *entirety*, of the timeout window was ticking
    // away against a mechanism that could not possibly see the target yet. Found live
    // (2026-09-13): 49 cc_released events in one batch, most within ~10-13s of the mark - right at
    // (or before) CcTimeoutSeconds's default of 10. Hold the grace window open (instead of counting
    // down) until the marked creature itself is actually in combat, so the caster gets the full
    // configured window from the moment it's first even eligible to try - bounded by the absolute
    // ceiling just above, so this can't hold forever. (An earlier version of this fix gated on
    // DungeonLead::GroupInCombat() - any party member in combat - which turned out to still be too
    // loose: the party can easily be fighting something else nearby while the specific marked
    // creature is still standing there unaggroed, same failure by a different path. Gating on the
    // marked creature's own combat state is the precise match for what TargetValue::FindTarget
    // actually requires.)
    if (!c->IsInCombat())
    {
        st.ccMarkedTs = getMSTime();
        return;
    }

    if (getMSTime() - st.ccMarkedTs < sPlayerbotAIConfig.dungeonLeadCcTimeoutSeconds * IN_MILLISECONDS)
        return;  // still within the grace window, give the CC class a chance to act

    // nobody managed to land CC on it (no CC-capable class in the group, spell unusable on this
    // creature, on cooldown, ...): stop excluding it from normal DPS targeting
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC on {} never landed, releasing mark", bot->GetName(), c->GetName());
    DungeonLead::RecordEvent(botAI, "cc_released", c->GetName());
    if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
        group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
    st.ccGuid.Clear();
}

std::string DungeonLead::DiagnosePath(Player* bot, float dx, float dy, float dz)
{
    std::ostringstream out;
    out << bot->GetName() << " at (" << bot->GetPositionX() << "," << bot->GetPositionY() << ","
        << bot->GetPositionZ() << ") -> (" << dx << "," << dy << "," << dz << "): ";

    float const disToDest = bot->GetExactDist(dx, dy, dz);
    out << "straight-line dist=" << disToDest << ". ";

    PathGenerator path(bot);
    path.CalculatePath(dx, dy, dz);
    PathType const type = path.GetPathType();

    std::ostringstream typeStr;
    if (type & PATHFIND_NORMAL) typeStr << "NORMAL ";
    if (type & PATHFIND_SHORTCUT) typeStr << "SHORTCUT ";
    if (type & PATHFIND_INCOMPLETE) typeStr << "INCOMPLETE ";
    if (type & PATHFIND_NOPATH) typeStr << "NOPATH ";
    if (type & PATHFIND_NOT_USING_PATH) typeStr << "NOT_USING_PATH ";
    if (type & PATHFIND_SHORT) typeStr << "SHORT ";
    if (type & PATHFIND_FARFROMPOLY_START) typeStr << "FARFROMPOLY_START ";
    if (type & PATHFIND_FARFROMPOLY_END) typeStr << "FARFROMPOLY_END ";
    if (typeStr.str().empty()) typeStr << "BLANK ";
    out << "type=" << typeStr.str();

    uint32 const typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
    bool const canReach = !(type & (~typeOk));
    out << "canReach=" << canReach << ". ";

    G3D::Vector3 const& endPos = path.GetActualEndPosition();
    float const actualToDest = std::sqrt((endPos.x - dx) * (endPos.x - dx) + (endPos.y - dy) * (endPos.y - dy) +
                                          (endPos.z - dz) * (endPos.z - dz));
    out << "actualEnd=(" << endPos.x << "," << endPos.y << "," << endPos.z << ") "
        << "actualEndToDest=" << actualToDest << " "
        << "wouldImprove=" << (actualToDest + 5.0f < disToDest) << ". ";

    Movement::PointsArray const& pts = path.GetPath();
    out << "waypoints=" << pts.size();
    if (!pts.empty())
    {
        out << " [";
        size_t const step = pts.size() > 8 ? pts.size() / 8 : 1;
        for (size_t i = 0; i < pts.size(); i += step)
            out << "(" << pts[i].x << "," << pts[i].y << "," << pts[i].z << ") ";
        out << "]";
    }
    return out.str();
}

void DungeonLead::GuardActiveSessions()
{
    // Called every world tick from PlayerbotsWorldScript::OnUpdate - throttle internally so the
    // call site there stays a single unconditional line, not something that needs its own timer.
    //
    // Thread-affinity (raised in external review, verified by reading AC core, not assumed):
    // this mutates bot AI state from the WORLD thread while bots' own AI ticks run on MAP UPDATE
    // threads (MapUpdate.Threads = 6 on this server) - a naive reading suggests those could race.
    // They cannot, because of AC's own fork-join barrier: World::Update() calls
    // sMapMgr->Update(diff) (World.cpp) BEFORE sScriptMgr->OnWorldUpdate(diff) (the hook that
    // invokes PlayerbotsWorldScript::OnUpdate, i.e. this function) much later in the same
    // function. MapMgr::Update() (MapMgr.cpp) schedules every map's update and then calls
    // m_updater.wait() before returning, which blocks (MapUpdater.cpp) on a condition variable
    // until every dispatched map-update task has actually finished. So by construction, no map
    // thread is still ticking any bot's AI by the time this function's call site is even reached
    // for that same world tick - the join has already fully happened. Safe today; would need
    // re-checking only if AC's own fork-join ordering in World::Update() ever changes.
    static uint32 lastRunTs = 0;
    uint32 now = getMSTime();
    if (lastRunTs && now - lastRunTs < 2000)
        return;
    lastRunTs = now;

    for (ObjectGuid const& guid : sDungeonRouteMgr.GetActiveSessionGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        // 2026-09-15 (independent architecture review DL-006 - the last silent exit path): a bot
        // that hard-disconnects (logs out, character deleted from memory - ObjectAccessor can no
        // longer find it at all, distinct from bot->IsInWorld()==false, which a bot mid-teleport
        // hits routinely and is NOT a reason to tear anything down) never runs any Stop() call
        // site - it just silently drops out of every future GuardActiveSessions() pass forever,
        // with no DungeonLeadRuns.csv row and its GetActiveSessionGuids() entry never cleared.
        // Close it out here instead: record it as its own terminal reason and stop tracking it.
        if (!bot)
        {
            DungeonLeadState const& goneSt = sDungeonRouteMgr.State(guid);
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead] {} hard-disconnected while leading (run={}) - closing out the "
                     "session instead of leaving it silently active", goneSt.tankName, goneSt.runId);
            DungeonLead::RecordRunSummary(guid, goneSt.tankName, "hard_disconnect");
            sDungeonRouteMgr.ResetState(guid);
            continue;
        }
        if (!bot->IsInWorld())
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;
        Group* group = bot->GetGroup();
        if (!group)
            continue;

        // Observability only - the reapply below is unconditional regardless of this check (a
        // FOLLOWER-only wipe wouldn't show up here at all, see below), but logging specifically
        // when the LEADER's own strategy was found missing gives a direct, queryable confirmation
        // that a wipe actually happened and was healed here, instead of having to infer it by
        // elimination from "AI was reset to defaults" chat lines plus "nothing else could have
        // reapplied it".
        if (!DungeonLead::IsOn(botAI))
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} strategy was missing (external AI reset) - "
                     "reconciliation loop restoring it", bot->GetName());
            DungeonLead::RecordEvent(botAI, "strategy_restored", "external reset detected");
        }

        // Unconditional, not "only if something looks wrong": ApplyLeaderFollowerStrategies is
        // idempotent, and checking only the leader's own HasStrategy("dungeon lead") would miss the
        // case where only a FOLLOWER's strategy got wiped (its own independent packet-processing
        // tick, not tied to the leader's at all) while the leader's own happened to survive - hence
        // logWipeDetection=true here, which checks and logs each follower individually too. Cheap
        // enough at the handful of concurrent sessions a server actually has running at once.
        ApplyLeaderFollowerStrategies(botAI, group, /*logWipeDetection*/ true);
    }
}

void DungeonLead::Stop(PlayerbotAI* botAI, bool giveLeaderBack)
{
    Player* bot = botAI->GetBot();

    // capture what we own BEFORE the state disappears below - ResetState() erases ccGuid/skullGuid/
    // memberSnapshots, and without this a stop while a moon mark was active left that mark
    // permanently excluding its target from normal DPS priority with nothing left to ever release
    // it, and followers had no way back to whatever they were doing before "startdungeon"
    DungeonLeadState const& st = sDungeonRouteMgr.State(bot->GetGUID());
    ObjectGuid const ownedCc = st.ccGuid;
    ObjectGuid const ownedSkull = st.skullGuid;
    std::vector<DungeonLeadMemberSnapshot> const snapshots = st.memberSnapshots;
    DungeonLeadMemberSnapshot const leaderSnap = st.leaderSnapshot;
    bool const hasLeaderSnap = st.hasLeaderSnapshot;

    // Full Reset() rather than just stripping "-dungeon lead,-grind"/"-mark rti": "startdungeon"
    // itself calls Reset() before adding its own strategies, so anything the leader had beyond
    // dungeon-lead's own additions (e.g. "+cc"/"+mark rti", also added to the leader's own combat
    // state at start) was never tracked and never got removed by the narrow -/- pair alone.
    botAI->Reset();

    // 2026-09-15 (independent architecture review DL-002 - "Stop() does not turn Dungeon Lead
    // off"): upstream Reset() alone does not reliably strip an active strategy back off, so
    // IsOn() could still read true here despite the run being reported stopped. Restore the
    // leader to its exact pre-"startdungeon" snapshot (same RestoreMember() path already used for
    // every follower below) rather than trusting Reset()'s side effects, then verify the
    // postcondition explicitly instead of assuming it.
    if (hasLeaderSnap)
        RestoreMember(botAI, leaderSnap);
    if (DungeonLead::IsOn(botAI))
        LOG_ERROR("playerbots.dungeonlead",
                   "[DungeonLead] {} Stop() completed but IsOn() is still true (hasLeaderSnapshot={}) "
                   "- dungeon lead did not actually turn off",
                   bot->GetName(), hasLeaderSnap);

    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot)
                continue;
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            if (!memberAI || !memberAI->GetAiObjectContext())
                continue;

            auto snapIt = std::find_if(snapshots.begin(), snapshots.end(),
                [&](DungeonLeadMemberSnapshot const& s) { return s.guid == member->GetGUID(); });
            if (snapIt != snapshots.end())
                RestoreMember(memberAI, *snapIt);
            else if (FormationValue* fv = dynamic_cast<FormationValue*>(
                    memberAI->GetAiObjectContext()->GetValue<Formation*>("formation")))
                fv->Load("chaos");  // joined mid-run, no snapshot to restore to - fall back to the old default
        }

        // only clear marks we actually placed - never touch one the player set/changed since
        if (group->GetTargetIcon(RtiTargetValue::starIndex) == bot->GetGUID())
            group->SetTargetIcon(RtiTargetValue::starIndex, bot->GetGUID(), ObjectGuid::Empty);
        if (!ownedSkull.IsEmpty() && group->GetTargetIcon(RtiTargetValue::skullIndex) == ownedSkull)
            group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), ObjectGuid::Empty);
        if (!ownedCc.IsEmpty() && group->GetTargetIcon(RtiTargetValue::moonIndex) == ownedCc)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);

        Player* master = botAI->GetMaster();
        if (giveLeaderBack && master && master != bot && group->IsMember(master->GetGUID()) &&
            group->GetLeaderGUID() == bot->GetGUID())
        {
            auto op = std::make_unique<GroupSetLeaderOperation>(bot->GetGUID(), master->GetGUID());
            // 2026-09-15 (independent architecture review, DL-009 - "enqueue success ... [is] not
            // required before ... committed"): QueueOperation() already LOG_ERRORs internally when
            // the bounded world-thread queue is full and drops the operation, but the return value
            // itself used to be discarded here - the leader handback could silently never happen
            // and nothing downstream would know. Not the review's full fix (no retry, no owned
            // completion/rollback transaction - this session is finishing regardless of whether the
            // handback landed), just making the failure visible instead of silent.
            if (!PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op)))
                LOG_ERROR("playerbots.dungeonlead",
                          "[DungeonLead] {} Stop() could not queue leader handback to {} - world "
                          "thread queue full, master stays non-leader until something else fixes it",
                          bot->GetName(), master->GetName());
        }
    }

    sDungeonRouteMgr.ResetState(bot->GetGUID());
}

// Shared tail of both "startdungeon" and the AutoBot Canary controller - see the declaration
// comment in DungeonLeadActions.h for the split of responsibilities. `bot` must already be a
// member of `group`; the caller has already decided this bot should lead.
bool DungeonLead::StartSession(PlayerbotAI* botAI, Group* group, DungeonLeadSessionOrigin origin, Player* master,
                                bool testMode)
{
    Player* bot = botAI->GetBot();

    // 2026-09-15 (independent architecture review DL-002): reject a start on a bot that is
    // already leading a session instead of re-initializing over it - re-snapshotting a bot whose
    // "current" strategies are already dungeon-lead's own would capture the WRONG baseline for
    // Stop() to later restore to, silently corrupting the exact-restore guarantee this function
    // otherwise provides. Caller must Stop() (or the run must terminate) before starting again.
    if (DungeonLead::IsOn(botAI))
    {
        LOG_ERROR("playerbots.dungeonlead",
                   "[DungeonLead] {} StartSession refused - already active (call Stop() first)",
                   bot->GetName());
        return false;
    }

    // snapshot every follower's current formation/strategies BEFORE touching anything (and before
    // any leadership change below), so Stop() can put them back to what they *actually* had going
    // in, not to whatever an external reset leaves them at - see Stop() above.
    std::vector<DungeonLeadMemberSnapshot> snapshots;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot)
            continue;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (!memberAI || !memberAI->GetAiObjectContext())
            continue;
        snapshots.push_back(SnapshotMember(member, memberAI));
    }

    // Snapshot the leader's own pre-"startdungeon" state too, same as every follower above and for
    // the same reason (see DungeonLeadState::leaderSnapshot) - taken here, before
    // ApplyLeaderFollowerStrategies below makes its first change to the leader's own engine.
    DungeonLeadMemberSnapshot const leaderSnap = SnapshotMember(bot, botAI);

    if (group->GetLeaderGUID() != bot->GetGUID())
    {
        // 2026-09-15 (independent architecture review, DL-009 - "the caller gets success
        // immediately [without] enqueue success ... required before ... a started session are
        // committed"): this used to queue the leader-change operation and proceed regardless of
        // whether it was actually accepted. If the bounded world-thread queue was full,
        // QueueOperation() drops the operation (logging its own error) - `bot` would then never
        // actually become group leader, while everything below still commits a started session,
        // applies follower strategies, and reports success. Refuse instead of starting a session
        // whose leadership change we know for certain never got queued.
        //
        // Not the review's full fix: this only catches the enqueue itself failing, not the
        // operation later being processed-but-rejected on the world thread (e.g. a stale roster),
        // and there's still no wait for GetLeaderGUID() to actually match before committing state
        // below - that would need this function to become asynchronous, out of scope here. A
        // group already led by someone else (GetLeaderGUID() mismatch after a successful enqueue)
        // remains an open DL-009 gap.
        auto op = std::make_unique<GroupSetLeaderOperation>(bot->GetGUID(), bot->GetGUID());
        if (!PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op)))
        {
            LOG_ERROR("playerbots.dungeonlead",
                      "[DungeonLead] {} StartSession refused - could not queue leader change "
                      "(world thread queue full), {} would lead a group it was never made leader of",
                      bot->GetName(), bot->GetName());
            return false;
        }
    }

    ApplyLeaderFollowerStrategies(botAI, group);

    sDungeonRouteMgr.ResetState(bot->GetGUID());
    {
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        st.debugMode = sPlayerbotAIConfig.dungeonLeadDebugDefault;
        st.memberSnapshots = std::move(snapshots);
        st.leaderSnapshot = leaderSnap;
        st.hasLeaderSnapshot = true;
        st.runId = NextRunId();
        st.origin = origin;
        st.sessionStartTs = getMSTime();
        st.tankName = bot->GetName();
        if (testMode)
        {
            st.testMode = true;
            st.testStartTs = getMSTime();
        }
    }
    botAI->Reset();
    ResetPositions(botAI);
    ApplyLeaderFollowerStrategies(botAI, group);  // Reset() above wipes the leader's own strategies again

    // mark self with the star icon: a visible "I'm leading, follow me" signal for the party
    group->SetTargetIcon(RtiTargetValue::starIndex, bot->GetGUID(), bot->GetGUID());

    if (master)
        botAI->TellMaster("Dungeon lead: ON - taking the lead, the others will follow me");
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} START by {} origin={} in map={} instance={} (tank={})",
             bot->GetName(), master ? master->GetName() : "canary", ToString(origin), bot->GetMapId(),
             bot->GetInstanceId(), PlayerbotAI::IsTank(bot));
    DungeonLead::RecordEvent(botAI, "start",
                              "tank=" + std::string(PlayerbotAI::IsTank(bot) ? "yes" : "no") +
                              " origin=" + ToString(origin));

    // CRITICAL BUG found in live testing, fixed by the reconciliation loop below rather than by
    // this function: a group leadership change (needed above whenever the tank wasn't already
    // leader) makes every bot in the group receive SMSG_GROUP_LIST, which the base module's
    // WorldPacketHandlerStrategy maps straight to ResetAiAction ("reset botAI" - wipes ALL
    // strategies back to class/spec defaults; see WorldPacketHandlerStrategy.cpp's "group list"
    // trigger). GroupSetLeaderOperation runs asynchronously on the world thread, and every other
    // bot processes that packet on ITS OWN AI tick, entirely decoupled from this function's own
    // timing - so there is no fixed delay (2 seconds, 6 seconds, anything) that is guaranteed long
    // enough under real conditions (world-thread queue backlog, a slow tick, N bots all reacting
    // at slightly different times). A single "wait then apply once" fix is just a race condition
    // with smaller odds, not a fix. See DungeonLead::GuardActiveSessions() (called every ~2s from
    // PlayerbotsWorldScript::OnUpdate, independent of any bot's own Strategy/Engine state - the
    // wipe removes the "dungeon lead" strategy object itself, so nothing owned by that strategy
    // could ever detect or heal its own absence) for the actual, unbounded fix: continuously
    // reassert the desired strategy state for every active session for as long as it's active, so
    // this self-heals whenever the external reset actually lands, however long that takes.
    return true;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead next: walk to the next route step
// ---------------------------------------------------------------------------------------------
bool DungeonLeadNextAction::isUseful()
{
    if (!bot || !botAI || !bot->IsAlive() || bot->IsInCombat())
        return false;
    if (!DungeonLead::InFiveMan(bot))
        return false;

    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (st.paused)
        return false;  // "startdungeon pause": stand still until "startdungeon continue"

    // "waiting for you": a one-shot chat ping on the transition into master-too-far, not spammed
    // every tick, and cleared as soon as the player is back in range.
    bool masterTooFar = DungeonLead::MasterTooFar(botAI);
    if (masterTooFar && !st.farFromMasterTold)
    {
        st.farFromMasterTold = true;
        botAI->TellMaster("We're waiting for you!");
    }
    else if (!masterTooFar)
        st.farFromMasterTold = false;

    // Same one-shot idea for a specific bot falling behind: "group too spread" alone gave no way
    // to tell WHO the group was actually waiting on (found during live testing - the tank was
    // correctly waiting the whole time on one bot stuck on terrain, but nothing in chat said so).
    Player* spreadMember = masterTooFar ? nullptr : DungeonLead::FindSpreadMember(botAI);
    std::string const spreadName = spreadMember ? spreadMember->GetName() : std::string();
    if (spreadMember && st.spreadOffenderTold != spreadName)
    {
        st.spreadOffenderTold = spreadName;
        botAI->TellMaster("We're waiting for " + spreadName + " to catch up!");
    }
    else if (!spreadMember)
        st.spreadOffenderTold.clear();

    char const* wait = nullptr;
    std::string waitDetail;
    if (DungeonLead::MasterUnavailable(botAI))
        wait = "master dead/disconnected/left the party";
    else if (DungeonLead::GroupInCombat(botAI))
        wait = "group in combat";
    else if (DungeonLead::GroupResting(botAI))
        wait = "someone is eating/drinking";
    else if (DungeonLead::HealerManaLow(botAI))
        wait = "healer low on mana";
    else if (masterTooFar)
        wait = "waiting for you, master too far";
    else if (spreadMember)
    {
        wait = "group too spread";
        waitDetail = spreadName;
    }

    if (wait)
    {
        // throttled: one line per 10 s per bot
        uint32 now = getMSTime();
        if (!st.lastWaitLogTs || now - st.lastWaitLogTs > 10000)
        {
            st.lastWaitLogTs = now;

            // DIAGNOSTIC (temporary): DungeonLeadDebug.log has stayed 0 bytes all session despite
            // DebugDefault=1 and RecordEvent (CSV) firing correctly from this same throttled block
            // - meaning either st.debugMode isn't actually true at this point, or RecordDebug's
            // fopen is silently failing. This unconditional line (not gated on debugMode) settles
            // which, directly from the next test run, instead of guessing further. Remove once
            // the debug log is confirmed working again.
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} debugMode={} runId={}", bot->GetName(),
                     st.debugMode, st.runId);
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} waiting: {}{}{}", bot->GetName(), wait,
                     waitDetail.empty() ? "" : " - ", waitDetail);
            DungeonLead::RecordEvent(botAI, "waiting", waitDetail.empty() ? wait : std::string(wait) + " - " + waitDetail);

            // "startdungeon debug": enough detail on every wait to reconstruct a run from a plain
            // file alone - positions, distances to the whole group, current step - without needing
            // a verbal bug report. Meant to be turned on for one troubleshooting run and attached
            // to a bug report (see README "Debugging" section), not left on permanently. Written
            // via plain file I/O (not the AC logger config) so it works the same on every server
            // regardless of worldserver.conf logger setup.
            if (st.debugMode)
            {
                std::ostringstream dbg;
                dbg << "run=" << st.runId << " " << bot->GetName() << " pos=(" << bot->GetPositionX() << ","
                    << bot->GetPositionY() << "," << bot->GetPositionZ() << ") step=" << st.stepIndex
                    << " moving=" << bot->isMoving() << " inCombat=" << bot->IsInCombat();
                if (Player* master = botAI->GetMaster())
                    if (master != bot)
                        dbg << " masterDist=" << bot->GetDistance(master);
                if (Group* group = bot->GetGroup())
                {
                    dbg << " members:";
                    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (!member || member == bot)
                            continue;
                        dbg << " " << member->GetName() << "@"
                            << (member->GetMap() == bot->GetMap() ? bot->GetDistance(member) : -1.0f);
                    }
                }
                DungeonLead::RecordDebug(botAI, dbg.str());
            }
        }
        // don't just refuse a new move - a move already committed on a previous tick keeps
        // playing out on its own spline otherwise, so the tank visibly "runs off" from a party
        // member who just sat down to drink or pulled something
        if (bot->isMoving())
            bot->StopMoving();
        return false;
    }
    return true;
}

DungeonRoute const* DungeonLeadNextAction::ResolveRoute(DungeonLeadState& st)
{
    uint32 mapId = bot->GetMapId();
    uint32 instanceId = bot->GetInstanceId();

    if (st.mapId == mapId && st.instanceId == instanceId && st.lfgId)
        return sDungeonRouteMgr.GetByLfgId(st.lfgId);
    if (st.mapId == mapId && st.instanceId == instanceId && st.noRouteTold)
        return nullptr;

    // route fields only - NOT a full Reset(). This used to wipe debugMode/paused/owned marks too,
    // which meant they reverted the moment the very first route-resolution tick ran after every
    // single "startdungeon" (this branch always runs once right after a fresh state, since lfgId
    // starts at 0) - see CHANGELOG and DungeonLeadState's own comment.
    st.ResetRouteProgress();
    st.mapId = mapId;
    st.instanceId = instanceId;

    DungeonRoute const* route = nullptr;
    if (Group* group = bot->GetGroup())
    {
        uint32 lfgId = sLFGMgr->GetDungeon(group->GetGUID(), true);
        if (lfgId)
        {
            route = sDungeonRouteMgr.GetByLfgId(lfgId);
            if (route && route->mapId != mapId)
                route = nullptr;
        }
    }

    if (!route)
    {
        // entered on foot (no LFD id): pick the route on this map/difficulty whose first walkable
        // step is closest — for multi-wing dungeons that is the wing we are standing in
        uint32 difficulty = bot->GetMap() ? uint32(bot->GetMap()->GetDifficulty()) : 0;
        float best = 1e9f;
        for (DungeonRoute const* cand : sDungeonRouteMgr.GetByMap(mapId, difficulty))
        {
            for (DungeonRouteStep const& s : cand->steps)
            {
                if (!s.IsWalkable())
                    continue;
                float d = bot->GetExactDist(s.x, s.y, s.z);
                if (d < best)
                {
                    best = d;
                    route = cand;
                }
                break;
            }
        }
    }

    if (!route)
        return nullptr;

    st.lfgId = route->lfgId;
    st.visited.assign(route->steps.size(), 0);

    uint32 walkable = 0;
    for (DungeonRouteStep const& s : route->steps)
        if (s.IsWalkable())
            ++walkable;

    std::ostringstream out;
    out << "Dungeon lead: route '" << route->name << "', " << walkable << " stops";
    botAI->TellMasterNoFacing(out);
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} route lfg={} '{}' map={} steps={} walkable={}", bot->GetName(),
             route->lfgId, route->name, route->mapId, route->steps.size(), walkable);
    DungeonLead::RecordEvent(botAI, "route_selected", std::to_string(walkable) + " stops");
    return route;
}

void DungeonLeadNextAction::MarkVisited(DungeonLeadState& st)
{
    if (st.stepIndex < st.visited.size())
        st.visited[st.stepIndex] = 1;
    ++st.stepIndex;
    st.bestDist = 0.f;
    st.stuckAttempts = 0;
    st.stuckTs = 0;
}

bool DungeonLeadNextAction::Execute(Event /*event*/)
{
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    DungeonRoute const* route = ResolveRoute(st);
    if (!route)
    {
        if (!st.noRouteTold)
        {
            st.noRouteTold = true;
            botAI->TellMasterNoFacing(
                "Dungeon lead: no route for this dungeon (event/vehicle dungeon?) - I'll just grind what I see");
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} no route for map={} instance={}", bot->GetName(), bot->GetMapId(),
                     bot->GetInstanceId());
            DungeonLead::RecordEvent(botAI, "no_route", "map=" + std::to_string(bot->GetMapId()));
        }
        return false;
    }

    // advance to the next step that is worth walking to
    while (st.stepIndex < route->steps.size())
    {
        DungeonRouteStep const& s = route->steps[st.stepIndex];
        // 2026-09-15 (DL-011): !s.IsPathAnchor() added to the SkipOptional condition - see
        // DungeonRouteStep::IsPathAnchor()'s comment. A pure navigation waypoint is never
        // "optional content" in the sense SkipOptional means (skip a boss/mob nobody needs to
        // fight), so it must not be deletable by that same switch.
        bool skip = st.visited[st.stepIndex] || !s.IsWalkable() ||
                    (s.kind == DungeonRouteKind::Optional && !s.IsPathAnchor() && sPlayerbotAIConfig.dungeonLeadSkipOptional) ||
                    (s.entry && sDungeonRouteMgr.IsStepKilled(st.instanceId, s.entry));
        if (!skip)
            break;
        ++st.stepIndex;
    }

    if (st.stepIndex >= route->steps.size())
    {
        if (!st.doneTold)
        {
            st.doneTold = true;
            // RunOutcome: a mandatory stop (IsMandatory()) that got skipped/stuck/not-found means
            // this run is PARTIAL, not COMPLETE, no matter how many optional stops were also
            // skipped - see the architecture roadmap's "mandatory objective failure must not
            // become COMPLETE". st.outcome was already set to Partial at the skip site if this
            // applies; this is only reached for Complete otherwise. The RecordEvent name itself
            // carries the outcome for anyone parsing the CSV, not just the wording of the chat line.
            //
            // DL-003 (2026-09-15 independent architecture review): a route with no IsMandatory()
            // step AT ALL (every row is optional/event/door/skip - 14 of 96 configured routes,
            // confirmed by the review) reaches here having proven nothing whatsoever, not even
            // "no mandatory step happened to fail" - there was never one to fail. Downgrade that
            // specific case to Blocked/UnsupportedEvent rather than Complete, so it reads
            // honestly instead of looking identical to a real full clear.
            bool const hadNothingToVerify = !route->HasAnyMandatory();
            if (hadNothingToVerify)
            {
                st.outcome = DungeonRunOutcome::Blocked;
                st.failureDomain = DungeonFailureDomain::Encounter;
                st.failureReason = DungeonFailureReason::UnsupportedEvent;
            }
            else if (!st.mandatorySkipped)
                st.outcome = DungeonRunOutcome::Complete;
            char const* outcome = hadNothingToVerify ? "BLOCKED (no mandatory objective on this route)"
                                   : st.mandatorySkipped ? "PARTIAL" : "complete";
            std::ostringstream out;
            if (st.skippedSteps.empty() && !hadNothingToVerify)
                out << "Dungeon lead: route complete";
            else
            {
                out << "Dungeon lead: route " << outcome << " (" << st.skippedSteps.size() << " stop(s) skipped:";
                for (size_t i = 0; i < st.skippedSteps.size(); ++i)
                    out << (i ? ", " : " ") << st.skippedSteps[i];
                out << ")";
            }
            botAI->TellMasterNoFacing(out);
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} route {} ({} skipped)", bot->GetName(), outcome, st.skippedSteps.size());
            DungeonLead::RecordEvent(botAI, hadNothingToVerify ? "route_blocked" :
                                      st.mandatorySkipped ? "route_partial" : "route_complete",
                                      std::to_string(st.skippedSteps.size()) + " skipped");
            DungeonLead::RecordRunSummary(botAI, "route_end");

            // 2026-09-15 (independent architecture review DL-018 - "a route-complete test clears
            // testMode but leaves the session active"): capture what Stop() below will erase before
            // it erases it - same reasoning as RecordRunSummary/RecordEvent needing to run before
            // Stop() elsewhere in this file.
            bool const wasTestMode = st.testMode;
            DungeonLeadSessionOrigin const origin = st.origin;

            // "startdungeon test" (L1.3): report a structured RunResult at the terminal outcome,
            // then clear test mode so any further play this session isn't mislabeled as a test.
            // One line, not several - TellMasterNoFacing sends a single WoW chat packet, and this
            // codebase has no existing convention for a whisper that reliably renders as multiple
            // lines client-side.
            if (st.testMode)
            {
                uint32 durationMs = GetMSTimeDiffToNow(st.testStartTs);
                std::ostringstream report;
                report << "DungeonLead Test #" << st.runId << ": " << (route ? route->name : "?")
                       << " -> " << ToString(st.outcome);
                if (st.outcome == DungeonRunOutcome::Partial || st.outcome == DungeonRunOutcome::Blocked)
                    report << " (" << ToString(st.failureDomain) << "/" << ToString(st.failureReason) << ")";
                report << " | duration " << (durationMs / 60000) << "m" << ((durationMs / 1000) % 60) << "s"
                       << " | skipped " << st.skippedSteps.size()
                       << " | manual interventions " << st.manualInterventions
                       << " (deaths/wipes not tracked yet)";
                botAI->TellMasterNoFacing(report);
                DungeonLead::RecordEvent(botAI, "test_result",
                    std::string(ToString(st.outcome)) + " duration_ms=" + std::to_string(durationMs) +
                    " manual_interventions=" + std::to_string(st.manualInterventions));
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} TEST RESULT run={} outcome={} duration_ms={}",
                         bot->GetName(), st.runId, ToString(st.outcome), durationMs);
                st.testMode = false;
            }

            // 2026-09-15 (DL-018): every AutoCanary session (organic or targeted) is always
            // testMode=true (see StartSession() call sites in DungeonLeadCanary.cpp/
            // DungeonTestBotPool.cpp), so wasTestMode alone already covers "this was a canary run" -
            // stopping here is exactly the fix the review names: without it, a canary session that
            // finished its route stayed active, kept its CanaryMaxConcurrent slot occupied, and
            // later got force-stopped by the unrelated timeout supervisor, which then recorded
            // "canary_timeout" as the terminal reason for a run that had actually already completed.
            // A non-test "startdungeon" from a real human (origin==Manual, wasTestMode==false) is
            // left running - a player who typed that command may still want the group held/formed
            // even after the pathed content runs out (farming, waiting on something the route
            // doesn't model), and auto-stopping their session out from under them was never the
            // problem this finding described.
            if (wasTestMode || origin != DungeonLeadSessionOrigin::Manual)
                DungeonLead::Stop(botAI, /*giveLeaderBack*/ true);
        }
        return false;
    }

    DungeonRouteStep const& step = route->steps[st.stepIndex];
    WorldPosition dest(bot->GetMapId(), step.x, step.y, step.z, 0.f);

    if (st.announcedStep != int32(st.stepIndex))
    {
        st.announcedStep = int32(st.stepIndex);
        st.arrivedTold = false;
        std::ostringstream headingOut;
        headingOut << "Dungeon lead: heading to " << step.boss;
        botAI->TellMasterNoFacing(headingOut);
    }

    // prefer the live creature position; a dead boss means this stop is done
    std::list<Creature*> found;
    bot->GetCreatureListWithEntryInGrid(found, step.entry, kCreatureProbeRange);
    bool anyAlive = false;
    for (Creature* c : found)
    {
        if (c->IsAlive())
        {
            anyAlive = true;
            dest = WorldPosition(c);
            break;
        }
    }
    if (!found.empty() && !anyAlive)
    {
        std::ostringstream out;
        out << "Dungeon lead: " << step.boss << " is down, moving on";
        botAI->TellMasterNoFacing(out);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}' already dead, next", bot->GetName(), step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "already_dead", step.boss);
        if (step.entry)
            sDungeonRouteMgr.MarkStepKilled(st.instanceId, step.entry);
        MarkVisited(st);
        return true;
    }

    float dist = bot->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
    if (dist <= sPlayerbotAIConfig.dungeonLeadArriveDistance)
    {
        // Arriving next to a still-alive boss is NOT the same as completing this stop - a pull can
        // still evade, wipe, or just not happen this tick. Hold here (isUseful() already blocks
        // walking on while the group is in combat) and only advance once the "already_dead" branch
        // above confirms an actual kill on a later tick. The one exception: if nothing at all shows
        // up here (not even a corpse) after a while, there is nothing left to confirm a kill on, so
        // give up and move on rather than parking here forever.
        if (!st.arrivedTold)
        {
            st.arrivedTold = true;
            st.arrivedTs = getMSTime();
            std::ostringstream out;
            out << "Dungeon lead: reached " << step.boss;
            botAI->TellMasterNoFacing(out);
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} reached step {} '{}'", bot->GetName(), step.step, step.boss);
            DungeonLead::RecordEvent(botAI, "reached", step.boss);
        }
        else if (GetMSTimeDiffToNow(st.arrivedTs) >= sPlayerbotAIConfig.dungeonLeadStuckSeconds * IN_MILLISECONDS)
        {
            // 2026-09-15 (independent architecture review DL-016 - "required interactions and
            // scripted events have no executor"): this used to require found.empty() too, so a
            // step whose target is found and stays alive - a friendly NPC that needs a gossip
            // interaction to progress (Wailing Caverns' Disciple of Naralex escort trigger is
            // exactly this, confirmed live tonight) - never timed out here at all. already_dead
            // above only fires once the target actually dies, which a gossip-only NPC never does,
            // so the run just sat at "reached" indefinitely - not hung forever in practice only
            // because the unrelated canary timeout (up to 45 minutes) eventually force-stopped the
            // whole session and mislabeled a stuck objective as a timed-out run.
            //
            // Not DL-016's full fix - no INTERACT/ESCORT executor exists, so this still can't
            // actually progress an interaction-gated objective - but it now gives up and reports
            // Partial within the same dungeonLeadStuckSeconds window as every other stuck case,
            // instead of silently occupying the session until something else notices.
            bool const stillAlive = !found.empty();
            std::ostringstream out;
            out << "Dungeon lead: " << (stillAlive ? "can't progress past " : "nothing found at ") << step.boss
                << ", moving on";
            botAI->TellMasterNoFacing(out);
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead] {} step {} '{}' - {}, giving up", bot->GetName(), step.step, step.boss,
                     stillAlive ? "target alive but not progressing (needs an interaction this module can't do)"
                                : "nothing at destination");
            DungeonLead::RecordEvent(botAI, stillAlive ? "stuck_alive" : "not_found", step.boss);
            st.skippedSteps.push_back(step.boss);
            if (step.IsMandatory())
            {
                st.mandatorySkipped = true;
                st.outcome = DungeonRunOutcome::Partial;
                st.failureDomain = DungeonFailureDomain::Navigation;
                st.failureReason = DungeonFailureReason::ObjectiveTimeout;
            }
            MarkVisited(st);
        }
        return true;
    }

    return MoveRouteTo(st, dest, step);
}

// Same approach as NewRpgBaseAction::MoveFarTo (mmap route to the true destination, walk to the
// furthest reachable waypoint, cone-sample when blocked) — but a bot that makes no progress skips
// the stop instead of teleporting to it: teleporting the tank away from its group is worse than
// missing an optional boss.
bool DungeonLeadNextAction::MoveRouteTo(DungeonLeadState& st, WorldPosition const& dest, DungeonRouteStep const& step)
{
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
        return false;

    {
        LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
        if (bot->isMoving() && lastMove.lastMoveToMapId == bot->GetMapId())
        {
            float remaining = bot->GetExactDist(lastMove.lastMoveToX, lastMove.lastMoveToY, lastMove.lastMoveToZ);
            if (remaining > 10.0f)
                return true;
        }
    }

    float disToDest = bot->GetDistance(dest);
    uint32 now = getMSTime();

    // Actual walking telemetry - not just milestone events (reached/stuck/mark). Task from the
    // user 2026-09-13: "pathfinding is the only job right now - log every step, evaluate it,
    // only then fix anything." Throttled to ~1 line per 3s per bot (this function can otherwise
    // be reached many times a second while a move is in flight and short-circuits above); sent to
    // BOTH LOG_INFO (DungeonLeadDebug.log) and RecordEvent (CSV -> sync -> panel dungeon_lead_events)
    // so the walking trace is visible in both places this session's earlier telemetry claims turned
    // out to be wrong about (RecordDebug's per-tick dump never actually wrote anything, and this
    // MoveRouteTo() function itself had zero position logging outside the one-shot stuck/skip line).
    if (!st.lastPathLogTs || now - st.lastPathLogTs > 3000)
    {
        st.lastPathLogTs = now;
        LOG_INFO("playerbots.dungeonlead",
                 "[DungeonLead] {} pathing step {} '{}' pos=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) "
                 "dist={:.1f} bestDist={:.1f} moving={} stuckAttempts={}",
                 bot->GetName(), step.step, step.boss, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                 dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), disToDest, st.bestDist,
                 bot->isMoving(), st.stuckAttempts);
        std::ostringstream pd;
        pd << step.boss << " pos=(" << bot->GetPositionX() << "," << bot->GetPositionY() << "," << bot->GetPositionZ()
           << ") target=(" << dest.GetPositionX() << "," << dest.GetPositionY() << "," << dest.GetPositionZ() << ")"
           << " dist=" << disToDest << " bestDist=" << st.bestDist << " moving=" << bot->isMoving()
           << " stuckAttempts=" << st.stuckAttempts;
        DungeonLead::RecordEvent(botAI, "pathing", pd.str());
    }

    if (st.bestDist == 0.f || disToDest + 5.0f < st.bestDist)
    {
        st.bestDist = disToDest;
        st.stuckTs = now;
        st.stuckAttempts = 0;
    }
    else if (++st.stuckAttempts >= 5 && st.stuckTs &&
             GetMSTimeDiffToNow(st.stuckTs) >= sPlayerbotAIConfig.dungeonLeadStuckSeconds * IN_MILLISECONDS)
    {
        std::ostringstream out;
        out << "Dungeon lead: can't reach " << step.boss << ", skipping";
        botAI->TellMasterNoFacing(out);
        // Both positions on purpose: `dest` alone (the target's own coordinates, effectively just
        // repeating the already-known route waypoint) was useless for telling where the bot
        // actually ended up stuck versus where it was trying to go - `bot->GetPosition*()` is the
        // bot's own real position at the moment it gave up.
        LOG_INFO("playerbots.dungeonlead",
                 "[DungeonLead] {} stuck {}s at dist {:.1f} to step {} '{}', target=({:.1f},{:.1f},{:.1f}) "
                 "actual=({:.1f},{:.1f},{:.1f}) -> skip",
                 bot->GetName(), GetMSTimeDiffToNow(st.stuckTs) / 1000, disToDest, step.step, step.boss,
                 dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
                 bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        DungeonLead::RecordEvent(botAI, "skip_stuck",
            step.boss + " dist=" + std::to_string(disToDest) +
            " target=(" + std::to_string(dest.GetPositionX()) + "," + std::to_string(dest.GetPositionY()) + "," +
            std::to_string(dest.GetPositionZ()) + ")" +
            " actual=(" + std::to_string(bot->GetPositionX()) + "," + std::to_string(bot->GetPositionY()) + "," +
            std::to_string(bot->GetPositionZ()) + ")");
        st.skippedSteps.push_back(step.boss);
        if (step.IsMandatory())
        {
            st.mandatorySkipped = true;
            st.outcome = DungeonRunOutcome::Partial;
            st.failureDomain = DungeonFailureDomain::Navigation;
            st.failureReason = DungeonFailureReason::PathFailed;
        }
        MarkVisited(st);
        return true;
    }

    float const dx = dest.GetPositionX(), dy = dest.GetPositionY(), dz = dest.GetPositionZ();
    if (disToDest < kPathFinderDis)
        return MoveTo(dest.GetMapId(), dx, dy, dz, false, false, false, true);

    uint32 const typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
    {
        PathGenerator path(bot);
        path.CalculatePath(dx, dy, dz);
        PathType type = path.GetPathType();
        bool canReach = !(type & (~typeOk));
        if (canReach)
        {
            G3D::Vector3 const& endPos = path.GetActualEndPosition();
            float endDistToDest = dest.GetExactDist(endPos.x, endPos.y, endPos.z);
            if (endDistToDest + 5.0f < disToDest)
                return MoveTo(bot->GetMapId(), endPos.x, endPos.y, endPos.z, false, false, false, true);
        }
    }

    // blocked: sample a wide-ish forward cone for a reachable stepping stone. More attempts than a
    // plain forward-only search (so a column/wall corner right on the line to dest doesn't box the
    // bot in), but still biased forward and close-range rather than a full 360 degree search -
    // a real find far off to the side/behind tends to lead away from the corridor a player would
    // actually walk, not around the obstacle and back onto it. See README "known limitations":
    // this whole fallback is a stopgap for point-to-point mmap pathing, not a real fix.
    float minDelta = static_cast<float>(M_PI);
    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    float const baseAngle = bot->GetAngle(&dest);
    float rx = 0.f, ry = 0.f, rz = 0.f;
    bool found = false;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        float delta = (rand_norm() - 0.5f) * static_cast<float>(M_PI);  // +-90 deg, forward-biased
        float sampleDis = (0.2f + rand_norm() * 0.3f) * kPathFinderDis;  // short, cautious probes
        float angle = baseAngle + delta;
        PathGenerator path(bot);
        path.CalculatePath(x + cos(angle) * sampleDis, y + sin(angle) * sampleDis, z + 0.5f);
        PathType type = path.GetPathType();
        bool canReach = !(type & (~typeOk));
        if (canReach && fabs(delta) <= minDelta)
        {
            found = true;
            G3D::Vector3 const& endPos = path.GetActualEndPosition();
            rx = endPos.x;
            ry = endPos.y;
            rz = endPos.z;
            minDelta = fabs(delta);
        }
    }
    if (found)
        return MoveTo(bot->GetMapId(), rx, ry, rz, false, false, false, true);

    return false;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead cc watch: release a stale CC mark - must run regardless of combat state
// ---------------------------------------------------------------------------------------------
bool DungeonLeadCcWatchAction::isUseful()
{
    if (!bot || !botAI || !DungeonLead::InFiveMan(bot))
        return false;
    return !sDungeonRouteMgr.State(bot->GetGUID()).ccGuid.IsEmpty();
}

bool DungeonLeadCcWatchAction::Execute(Event /*event*/)
{
    DungeonLead::CheckCcMark(botAI);
    return true;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead mark: skull on the boss, moon on a CC candidate
// ---------------------------------------------------------------------------------------------
bool DungeonLeadMarkAction::isUseful()
{
    return bot && bot->GetGroup() && DungeonLead::InFiveMan(bot);
}

Creature* DungeonLeadMarkAction::FindCcCandidate(Creature* boss)
{
    GuidVector targets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get();
    Creature* best = nullptr;
    float bestDist = kCcSearchRange;
    for (ObjectGuid const& guid : targets)
    {
        Creature* c = botAI->GetCreature(guid);
        if (!c || c == boss || !c->IsAlive() || !c->IsInWorld() || c->IsDungeonBoss())
            continue;
        CreatureTemplate const* ct = c->GetCreatureTemplate();
        if (!ct || ct->rank != CREATURE_ELITE_ELITE)
            continue;
        float d = bot->GetDistance(c);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

bool DungeonLeadMarkAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    Creature* boss = DungeonLead::FindBossNear(botAI, kBossSearchRange);
    if (!boss)
        return false;

    bool changed = false;
    Unit* skull = IconUnit(botAI, group, RtiTargetValue::skullIndex);
    if (!skull || !skull->IsAlive())
    {
        group->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), boss->GetGUID());
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} skull -> {} ({})", bot->GetName(), boss->GetName(), boss->GetEntry());
        DungeonLead::RecordEvent(botAI, "mark_skull", boss->GetName());
        sDungeonRouteMgr.State(bot->GetGUID()).skullGuid = boss->GetGUID();  // so Stop() only clears our own
        changed = true;
    }

    if (sPlayerbotAIConfig.dungeonLeadMarkCc)
    {
        Unit* moon = IconUnit(botAI, group, RtiTargetValue::moonIndex);
        if (!moon || !moon->IsAlive())
        {
            if (Creature* cc = FindCcCandidate(boss))
            {
                group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), cc->GetGUID());
                LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} moon -> {} ({})", bot->GetName(), cc->GetName(), cc->GetEntry());
                DungeonLead::RecordEvent(botAI, "mark_moon", cc->GetName());
                DungeonLeadState& ccSt = sDungeonRouteMgr.State(bot->GetGUID());
                ccSt.ccGuid = cc->GetGUID();
                ccSt.ccMarkedTs = getMSTime();
                ccSt.ccMarkedAbsoluteTs = ccSt.ccMarkedTs;
                ccSt.ccLandedTold = false;
                changed = true;
            }
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------------------------
// dungeon lead stop (automatic, e.g. after leaving the instance)
// ---------------------------------------------------------------------------------------------
bool DungeonLeadStopAction::Execute(Event /*event*/)
{
    // log BEFORE Stop() - it erases the route/session state RecordEvent reads (lfg_id, dungeon
    // name, ...), so logging after it left every "stop" row with an empty dungeon/lfg_id.
    // Also LOG_INFO (not just RecordEvent) - this event previously only reached the CSV, invisible
    // to anyone reading DungeonLeadDebug.log live, which made a real "left instance" bug (found
    // live 2026-09-13, still open) look like a silent multi-minute stall instead of what it was.
    Player* bot = botAI->GetBot();
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} STOP - auto (left instance), now map={} instance={} pos=({:.1f},{:.1f},{:.1f})",
             bot->GetName(), bot->GetMapId(), bot->GetInstanceId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    DungeonLead::RecordEvent(botAI, "stop", "auto (left instance)");
    // 2026-09-15 (DL-006, closing the 4th of 5 known Stop() call sites): this is the single most
    // common exit path in practice - watching live runs today, most ended here, not via route
    // completion or a canary timeout - and it had no DungeonLeadRuns.csv row until now.
    DungeonLead::RecordRunSummary(botAI, "left_instance");
    DungeonLead::Stop(botAI, true);
    botAI->TellMasterNoFacing("Dungeon lead: OFF (not in a 5-man dungeon anymore)");
    return true;
}

// ---------------------------------------------------------------------------------------------
// chat shortcuts: startdungeon / stopdungeon
// ---------------------------------------------------------------------------------------------
namespace
{
    std::string TrimLower(std::string s)
    {
        size_t a = s.find_first_not_of(" \t");
        size_t b = s.find_last_not_of(" \t");
        s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }
}

bool StartDungChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    std::string const sub = TrimLower(event.getParam());
    if (sub == "pause" || sub == "continue" || sub == "reset" || sub == "debug" || sub == "status")
    {
        if (!DungeonLead::IsOn(botAI))
        {
            botAI->TellMaster("startdungeon: not currently leading a dungeon - use plain 'startdungeon' first");
            return false;
        }
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        if (sub != "debug" && sub != "status" && st.testMode)
            ++st.manualInterventions;  // pause/continue/reset mid-test: not a clean, hands-off smoke run
        if (sub == "status")
        {
            // Read-only, no state change: current route/step/outcome on demand (L1.1/L1.3 - the
            // roadmap's own "Current Objective, Party Readiness, Run Status" without needing a UI).
            DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
            std::string stepName = "?";
            if (route && st.stepIndex < route->steps.size())
                stepName = route->steps[st.stepIndex].boss;
            else if (route)
                stepName = "(route complete)";
            std::ostringstream out;
            out << "Dungeon lead status: run #" << st.runId << " " << (route ? route->name : "no route")
                << " | step " << stepName << " | outcome " << ToString(st.outcome);
            if (st.outcome == DungeonRunOutcome::Partial)
                out << " (" << ToString(st.failureDomain) << "/" << ToString(st.failureReason) << ")";
            out << " | " << (st.paused ? "PAUSED" : "running")
                << (st.debugMode ? " | debug ON" : "") << (st.testMode ? " | TEST MODE" : "");
            botAI->TellMaster(out);
            return true;
        }
        if (sub == "pause")
        {
            st.paused = true;
            botAI->TellMaster("Dungeon lead: PAUSED - staying put until 'startdungeon continue'");
        }
        else if (sub == "continue")
        {
            st.paused = false;
            botAI->TellMaster("Dungeon lead: resuming");
        }
        else if (sub == "reset")
        {
            // route progress only - debug mode, pause state and any owned mark survive this,
            // matching the README's "without redoing leadership/formations" (a full ResetState()
            // used to also silently turn debug mode back off)
            sDungeonRouteMgr.ResetRouteProgress(bot->GetGUID());
            botAI->TellMaster("Dungeon lead: route progress reset, picking the route back up from the start");
        }
        else  // debug
        {
            st.debugMode = !st.debugMode;
            botAI->TellMaster(st.debugMode
                ? "Dungeon lead debug activated, file is being saved to DungeonLeadDebug.log (same folder as Playerbots.log)"
                : "Dungeon lead debug stopped, file is saved to DungeonLeadDebug.log (same folder as Playerbots.log)");
        }
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} {}", bot->GetName(), sub);
        return true;
    }

    Group* group = bot->GetGroup();
    if (!group)
    {
        botAI->TellMaster("startdungeon: I'm not in a group");
        return false;
    }
    if (!DungeonLead::InFiveMan(bot))
    {
        botAI->TellMaster("startdungeon: only works inside a 5-man dungeon");
        return false;
    }
    if (!PlayerbotAI::IsTank(bot))
        botAI->TellMaster("startdungeon: I'm not a tank, but fine - leading anyway");

    // only take leadership away from whoever currently holds it if the person asking IS that
    // leader (or the bot already is) - otherwise any member could hand themselves control of the
    // group's leadership through their own bot without the actual leader's say-so
    if (group->GetLeaderGUID() != bot->GetGUID() && group->GetLeaderGUID() != master->GetGUID())
    {
        botAI->TellMaster("startdungeon: only the current party leader can start this");
        return false;
    }

    // L1.3 first live smoke-test command: same start as plain "startdungeon", just report a
    // structured result once the run reaches a terminal outcome (see the "route complete"/"route
    // PARTIAL" block below) instead of only the normal chat lines.
    DungeonLead::StartSession(botAI, group, DungeonLeadSessionOrigin::Manual, master, /*testMode*/ sub == "test");
    return true;
}

bool StopDungChatShortcutAction::Execute(Event /*event*/)
{
    if (!GetMaster())
        return false;

    // log BEFORE Stop() - see DungeonLeadStopAction::Execute
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} STOP by master", bot->GetName());
    DungeonLead::RecordEvent(botAI, "stop", "");
    // 2026-09-15 (DL-006, closing the 5th and last known Stop() call site): a human explicitly
    // typing "stopdungeon" is a deliberate, meaningful end to a run too - it should read the same
    // in DungeonLeadRuns.csv as any other terminal reason, not look like the run never ended.
    DungeonLead::RecordRunSummary(botAI, "stopdungeon_by_master");
    DungeonLead::Stop(botAI, true);
    ResetReturnPosition();
    ResetStayPosition();
    botAI->TellMaster("Dungeon lead: OFF");
    return true;
}

bool CanaryTestChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    // Manipulates other bots server-wide, not just this whisper's own party - unlike every other
    // command in this file, gated behind GM level rather than open to anyone (see README).
    if (!master->GetSession() || master->GetSession()->GetSecurity() < SEC_GAMEMASTER)
    {
        botAI->TellMaster("canarytest: GM level required (this queues OTHER bots server-wide, not just yours)");
        return false;
    }

    std::string const param = TrimLower(event.getParam());
    uint32 lfgId = 0, groups = 1;
    try
    {
        std::istringstream iss(param);
        std::string lfgTok, groupsTok;
        iss >> lfgTok;
        lfgId = static_cast<uint32>(std::stoul(lfgTok));
        if (iss >> groupsTok)
            groups = std::max(1u, static_cast<uint32>(std::stoul(groupsTok)));
    }
    catch (std::exception const&)
    {
        botAI->TellMaster("canarytest: usage \"canarytest <lfgId> [groups]\" - e.g. \"canarytest 1 10\" for "
                           "10 parallel Ragefire Chasm runs");
        return false;
    }

    std::string const result = DungeonLead::TriggerTargetedTest(master, lfgId, groups);
    botAI->TellMaster("canarytest: " + result);
    return true;
}
