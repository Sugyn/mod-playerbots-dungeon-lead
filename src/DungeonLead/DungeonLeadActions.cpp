/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonLeadCanary.h"
#include "DungeonLeadBrain.h"
#include "DungeonLeadKernels.h"
#include "DungeonPack.h"
#include "DungeonInteractionController.h"
#include "GameObject.h"
#include "DungeonPullController.h"
#include "DungeonTelemetryBuffer.h"
#include "Spell.h"
#include "DungeonRecoveryController.h"
#include "DungeonTargetManager.h"
#include "DungeonPartyState.h"

#include "Creature.h"
#include "DBCStores.h"
#include "GossipDef.h"
#include "CreatureData.h"
#include "Event.h"
#include "Formations.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LootMgr.h"
#include "LastMovementValue.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "RtiTargetValue.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "Log.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>
#include <sstream>

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------
namespace
{
    constexpr float kPathFinderDis = 70.0f;     // below this, hand the destination straight to mmaps
    constexpr float kDoorSightRange = 60.0f;    // a route door's state is read from this far
    constexpr float kDoorWaitDistance = 8.0f;   // wait at a closed route door from this close
    // A closed door this close to the leader that its path goes through (crosses its plane within
    // kDoorHalfWidth of it): mmaps don't know doors, so bots would walk straight through it.
    constexpr float kDoorOnPathRange = 25.0f;
    constexpr float kDoorHalfWidth = 3.5f;

    GameObject* ClosedDoorOnPath(PlayerbotAI* botAI, Player* bot, float x, float y, float z)
    {
        std::vector<GameObject*> doors;
        for (ObjectGuid const& guid :
             botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects no los")->Get())
            if (GameObject* go = botAI->GetGameObject(guid))
                if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR && go->GetGoState() == GO_STATE_READY &&
                    bot->GetDistance(go) <= kDoorOnPathRange &&
                    go->GetExactDist(x, y, z) > 4.0f)  // the destination itself (a cage to use)
                    doors.push_back(go);
        if (doors.empty())
            return nullptr;
        PathGenerator path(bot);
        path.CalculatePath(x, y, z);
        Movement::PointsArray const& pts = path.GetPath();
        for (size_t i = 1; i < pts.size(); ++i)
            for (GameObject* door : doors)
            {
                float t = 0.f;
                if (!DungeonLeadKernel::SegmentCrossesDoor(door->GetPositionX(), door->GetPositionY(),
                                                           door->GetOrientation(), pts[i - 1].x, pts[i - 1].y,
                                                           pts[i].x, pts[i].y, kDoorHalfWidth, &t))
                    continue;
                // through the doorway, not over or under it (SFK: a walkway above the cells)
                float const z = pts[i - 1].z + t * (pts[i].z - pts[i - 1].z);
                if (z > door->GetPositionZ() - 2.0f && z < door->GetPositionZ() + 5.0f)
                    return door;
            }
        return nullptr;
    }
    // Minimum gap between wipe-recovery teleports (see RecoverStrandedMembers). Long enough that a
    // member dying repeatedly isn't yanked every tick, short enough that a real wipe is back on
    // its feet well inside WipeRecoverySeconds.
    constexpr uint32 kRecoveryRetrySeconds = 15;

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

    // Telemetry lines are produced from bot AI updates on any map-update thread; they only go
    // into this bounded buffer (a short lock, no file I/O). The world thread writes them out in
    // FlushTelemetry() - see DungeonTelemetryBuffer.h. 20000 lines is minutes of a busy server.
    DungeonLeadKernel::TelemetryBuffer g_telemetry(20000);

    // Files are opened once, on first write, and kept open for the life of the process. Only the
    // world thread (FlushTelemetry) touches them.
    FILE* OpenPersistent(char const* path, FILE*& handle)
    {
        if (!handle)
            handle = fopen(path, "a");
        return handle;
    }

    FILE* g_sessionsLogFile = nullptr;
    FILE* g_runsLogFile = nullptr;
    FILE* g_debugLogFile = nullptr;

    // Writes the header into a fresh (empty) file the first time it is opened.
    void EnsureHeader(FILE* f, bool& done, char const* header)
    {
        if (done)
            return;
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0)
            fputs(header, f);
        done = true;
    }

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
                // -new rpg/-rpg: upstream's RPG wandering teleports a bot to its RPG destination when
                // it can't walk there (NewRpgBaseAction::MoveFarTo) - from inside an instance that
                // means out of the dungeon (seen live in Shadowfang Keep: the leader landed in
                // Silverpine while the party was drinking). Restored with the snapshot at Stop().
                memberAI->ChangeStrategy("+follow,-passive,-stay,-grind,-dungeon lead,-new rpg,-rpg",
                                         BOT_STATE_NON_COMBAT);
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
            botAI->ChangeStrategy("+dungeon lead,+grind,-passive,-stay,-new rpg,-rpg", BOT_STATE_NON_COMBAT);
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

    // Columns after failure_reason were added later (2026-10-03); older rows simply lack them.
    std::ostringstream line;
    line << FormatLogTimestamp() << "," << st.runId << ",\"" << CsvEscape(master ? master->GetName() : "?")
         << "\"," << st.lfgId << ",\"" << CsvEscape(route ? route->name : "?") << "\",\""
         << CsvEscape(bot->GetName()) << "\",\"" << CsvEscape(GroupMemberList(bot)) << "\",\"" << CsvEscape(event)
         << "\",\"" << CsvEscape(detail) << "\",\"" << ToString(st.outcome) << "\",\"" << ToString(st.failureDomain)
         << "\",\"" << ToString(st.failureReason) << "\"," << bot->GetMapId() << "," << bot->GetInstanceId() << ",\""
         << DungeonLeadKernel::ToString(st.state) << "\"," << st.stepIndex << "," << st.packId << "\n";
    g_telemetry.Push(DungeonLeadKernel::TelemetryFile::Sessions, line.str());
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

    // st.wipeCount: how many times the leader died this run - a run can recover from a wipe and
    // carry on, so "completed" on its own would otherwise hide that it took several attempts.
    std::ostringstream line;
    line << FormatLogTimestamp() << "," << st.runId << ",\"" << CsvEscape(tankName) << "\"," << st.lfgId << ",\""
         << CsvEscape(route ? route->name : "?") << "\",\"" << ToString(st.origin) << "\",\"" << ToString(st.outcome)
         << "\",\"" << ToString(st.failureDomain) << "\",\"" << ToString(st.failureReason) << "\","
         << st.skippedSteps.size() << "," << st.wipeCount << ","
         << (st.sessionStartTs ? GetMSTimeDiffToNow(st.sessionStartTs) : 0) << ",\"" << CsvEscape(terminalReason)
         << "\"\n";
    g_telemetry.Push(DungeonLeadKernel::TelemetryFile::Runs, line.str());
}

void DungeonLead::RecordDebug(PlayerbotAI* /*botAI*/, std::string const& line)
{
    g_telemetry.Push(DungeonLeadKernel::TelemetryFile::Debug, FormatLogTimestamp() + " " + line + "\n");
}

void DungeonLead::FlushTelemetry(bool force)
{
    static uint32 lastFlushTs = 0;
    if (!force && lastFlushTs && GetMSTimeDiffToNow(lastFlushTs) < 2000 && g_telemetry.Size() < 1000)
        return;
    lastFlushTs = getMSTime();

    uint64 dropped = 0;
    std::vector<DungeonLeadKernel::TelemetryLine> const lines = g_telemetry.Drain(dropped);
    if (lines.empty() && !dropped)
        return;

    static bool sessionsHeader = false, runsHeader = false;
    FILE* sessions = OpenPersistent("DungeonLeadSessions.csv", g_sessionsLogFile);
    FILE* runs = OpenPersistent("DungeonLeadRuns.csv", g_runsLogFile);
    FILE* debug = OpenPersistent("DungeonLeadDebug.log", g_debugLogFile);
    if (sessions)
        EnsureHeader(sessions, sessionsHeader,
                     "timestamp,run_id,player,lfg_id,dungeon,tank,group_members,event,detail,outcome,"
                     "failure_domain,failure_reason,map_id,instance_id,state,step,pack_id\n");
    if (runs)
        EnsureHeader(runs, runsHeader,
                     "ended_at,run_id,tank,lfg_id,dungeon,origin,outcome,failure_domain,failure_reason,"
                     "skipped_steps,wipes,duration_ms,terminal_reason\n");

    for (DungeonLeadKernel::TelemetryLine const& l : lines)
    {
        FILE* f = l.file == DungeonLeadKernel::TelemetryFile::Sessions ? sessions
                : l.file == DungeonLeadKernel::TelemetryFile::Runs     ? runs
                                                                       : debug;
        if (f)
            fputs(l.text.c_str(), f);
    }
    if (dropped)
    {
        // a gap in the record is reported, never silent
        LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] telemetry buffer full - {} line(s) dropped", dropped);
        if (sessions)
            fprintf(sessions, "%s,0,\"?\",0,\"?\",\"?\",\"\",\"telemetry_dropped\",\"%llu lines\",\"\",\"\",\"\",0,0,\"\",0,0\n",
                    FormatLogTimestamp().c_str(), static_cast<unsigned long long>(dropped));
    }
    for (FILE* f : {sessions, runs, debug})
        if (f)
            fflush(f);
}

bool DungeonLead::AllowTeleport(Player* player, uint32 mapId, float x, float y, float z, uint32 options)
{
    if (!player || IsRealPlayer(player) || IsSelfBot(player) || !player->IsAlive() || !player->IsInWorld())
        return true;
    if (mapId == player->GetMapId() || !InFiveMan(player))
        return true;  // within the map, or not in a dungeon at all

    // Whose session is this bot part of (its own as leader, or its group leader's)?
    Group* group = player->GetGroup();
    Player* leader = player;
    if (!sDungeonRouteMgr.HasState(leader->GetGUID()))
    {
        leader = group ? ObjectAccessor::FindPlayer(group->GetLeaderGUID()) : nullptr;
        if (!leader || !sDungeonRouteMgr.HasState(leader->GetGUID()))
            return true;
    }
    DungeonLeadState const& st = sDungeonRouteMgr.State(leader->GetGUID());
    if (!DungeonLeadKernel::IsActive(st.state) || leader->GetMapId() != player->GetMapId())
        return true;
    // The real player has left the dungeon: following them out is right.
    PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader);
    if (Player* master = leaderAI ? leaderAI->GetMaster() : nullptr)
        if (master != leader && IsRealPlayer(master) && master->GetMapId() != player->GetMapId())
            return true;

    // mod-playerbots' "within area trigger" -> "area trigger" action re-sends the area trigger for a
    // bot standing in one, every tick (seen in Ragefire Chasm: the whole party at the instance exit).
    // Forget the remembered trigger so it stops retrying - each retry also used up the bot's action
    // for that tick, which kept the leader from walking at all.
    if (PlayerbotAI* playerAI = GET_PLAYERBOT_AI(player))
        if (AiObjectContext* ctx = playerAI->GetAiObjectContext())
            ctx->GetValue<LastMovement&>("last area trigger")->Get().lastAreaTrigger = 0;

    // Whatever retries is recorded once per bot per 30 s, with the number of attempts cancelled since.
    struct CancelLog
    {
        uint32 lastTs = 0;
        uint32 suppressed = 0;
    };
    static std::unordered_map<ObjectGuid, CancelLog> cancelLog;  // world/map update threads only
    static std::mutex cancelLogMutex;
    uint32 suppressed = 0;
    {
        std::lock_guard<std::mutex> lock(cancelLogMutex);
        CancelLog& entry = cancelLog[player->GetGUID()];
        if (entry.lastTs && GetMSTimeDiffToNow(entry.lastTs) < 30000)
        {
            ++entry.suppressed;
            return false;
        }
        suppressed = entry.suppressed;
        entry.lastTs = getMSTime();
        entry.suppressed = 0;
    }

    Spell const* spell = player->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    uint32 const spellId = spell && spell->m_spellInfo ? spell->m_spellInfo->Id : 0;
    std::string const detail = player->GetName() + " to map " + std::to_string(mapId) + " (" +
                               std::to_string(int(x)) + "," + std::to_string(int(y)) + "," + std::to_string(int(z)) +
                               ") options=" + std::to_string(options) + " spell=" + std::to_string(spellId) +
                               " repeats_since_last=" + std::to_string(suppressed);
    LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] cancelled a teleport out of the dungeon mid-run: {}", detail);
    if (leaderAI)
        DungeonLead::RecordEvent(leaderAI, "unexpected_teleport", detail);
    return false;
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

bool DungeonLead::HasSession(PlayerbotAI* botAI)
{
    return botAI && sDungeonRouteMgr.HasState(botAI->GetBot()->GetGUID());
}

bool DungeonLead::GroupInCombat(PlayerbotAI* botAI)
{
    return DungeonLeadKernel::AnyInCombat(DungeonPartyState::Evaluate(botAI).facts);
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
    if (GetMSTimeDiffToNow(st.ccMarkedAbsoluteTs) >= sDungeonLeadConfig.dungeonLeadCcAbsoluteTimeoutSeconds * IN_MILLISECONDS)
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC on {} never landed within the absolute ceiling ({}s since marked), releasing mark",
                 bot->GetName(), c->GetName(), sDungeonLeadConfig.dungeonLeadCcAbsoluteTimeoutSeconds);
        DungeonLead::RecordEvent(botAI, "cc_released", c->GetName());
        if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
        st.ccFailed.push_back(st.ccGuid);
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

    if (getMSTime() - st.ccMarkedTs < sDungeonLeadConfig.dungeonLeadCcTimeoutSeconds * IN_MILLISECONDS)
        return;  // still within the grace window, give the CC class a chance to act

    // nobody managed to land CC on it (no CC-capable class in the group, spell unusable on this
    // creature, on cooldown, ...): stop excluding it from normal DPS targeting
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC on {} never landed, releasing mark", bot->GetName(), c->GetName());
    DungeonLead::RecordEvent(botAI, "cc_released", c->GetName());
    if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
        group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
    st.ccFailed.push_back(st.ccGuid);
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

// 2026-09-16 (independent architecture review DL-013, the recovery half): put any party member
// that death has stranded on a different map back onto the instance map, at the route's entrance.
//
// Why this is all that's needed: upstream mod-playerbots already implements death recovery in
// full, on its own engine (BOT_STATE_DEAD, installed unconditionally by
// AiFactory::AddDefaultDeadStrategies, triggering auto release -> find corpse ->
// revive from corpse). Nothing in Dungeon Lead blocks it. It has exactly one step it physically
// cannot perform: a dungeon with no graveyard of its own - Wailing Caverns has none - releases the
// ghost to the nearest outdoor graveyard, which is on another map, and FindCorpseAction's
// MoveTo() towards the corpse cannot path across maps. Measured on a live stranded tank: 2697
// yards away, canReach=0, still dead and motionless 8 minutes later, with the spirit-healer
// fallback never firing either. Teleporting that ghost back onto the instance map was verified by
// hand to resolve it completely - the bot resurrected by itself within 20 seconds. So this does
// the one impossible step and lets upstream do the rest; it deliberately does not reimplement
// release, corpse-running or resurrection.
//
// The entrance is the aim point rather than the place of death: it is a known-walkable step
// (the same one RunTestParty teleports parties to), it has no mobs standing on it, and a corpse
// run from there is an ordinary same-map run that works. recoveryTs throttles this so a member
// dying over and over cannot be teleported every single tick.
void DungeonLead::KeepInstanceValid(PlayerbotAI* botAI)
{
    // AzerothCore marks a player's instance invalid when a group is left or disbanded while inside
    // (Group::_homebindIfInstance) and sends them out 60 s later - and only re-validates on rejoining
    // that same group. Test characters log back into their previous run's group, which then gets
    // disbanded while they are already in the new dungeon: the whole party was sent out ~40 s into
    // runs (Ragefire Chasm, Deadmines). A member who is in the leader's group and the leader's
    // instance does belong there - the same condition AzerothCore's own
    // Group::_cancelHomebindIfInstance applies.
    Player* leader = botAI->GetBot();
    Group* group = leader->GetGroup();
    if (!group || !InFiveMan(leader))
        return;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member->m_InstanceValid || member->IsGameMaster() || member->GetMap() != leader->GetMap())
            continue;
        member->m_InstanceValid = true;
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {}: instance marked invalid while in the party's own "
                 "instance - restored", member->GetName());
        DungeonLead::RecordEvent(botAI, "instance_validity_restored", member->GetName());
    }
}

namespace
{
    // Scripted, non-teleport area triggers per map, built once from ObjectMgr (ids are small - same
    // bounded scan as DungeonRouteMgr::GetEntrance). World thread only (GuardActiveSessions).
    std::vector<AreaTrigger const*> const& ScriptedAreaTriggers(uint32 mapId)
    {
        static std::unordered_map<uint32, std::vector<AreaTrigger const*>> cache;
        auto it = cache.find(mapId);
        if (it != cache.end())
            return it->second;
        std::vector<AreaTrigger const*>& list = cache[mapId];
        for (uint32 id = 1; id < 20000; ++id)
        {
            AreaTrigger const* at = sObjectMgr->GetAreaTrigger(id);
            if (at && at->map == mapId && sObjectMgr->GetAreaTriggerScriptId(id) &&
                !sObjectMgr->GetAreaTriggerTeleport(id))
                list.push_back(at);
        }
        return list;
    }
}

void DungeonLead::FireAreaTriggers(PlayerbotAI* botAI, DungeonLeadState& st)
{
    // H7 / Zul'Farrak: Witch Doctor Zum'rah turns hostile from area trigger 962 (SmartTrigger) on
    // his spot. Only a client sends that; with bots alone he stayed friendly and every pull failed.
    Player* leader = botAI->GetBot();
    if (!leader->IsAlive() || leader->IsInFlight() || !InFiveMan(leader))
        return;
    for (AreaTrigger const* at : ScriptedAreaTriggers(leader->GetMapId()))
    {
        if (st.firedAreaTriggers.count(at->entry) || !leader->IsInAreaTriggerRadius(at))
            continue;
        st.firedAreaTriggers.insert(at->entry);
        bool const handled = sScriptMgr->OnAreaTrigger(leader, at);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} area trigger {} fired (handled={})", leader->GetName(),
                 at->entry, handled);
        DungeonLead::RecordEvent(botAI, "area_trigger", std::to_string(at->entry) + " handled=" + std::to_string(handled));
    }
}

void DungeonLead::RecoverStrandedMembers(PlayerbotAI* botAI, DungeonLeadState& st)
{
    Player* leader = botAI ? botAI->GetBot() : nullptr;
    if (!leader || !st.mapId)
        return;

    Group* group = leader->GetGroup();
    if (!group)
        return;

    if (st.recoveryTs && GetMSTimeDiffToNow(st.recoveryTs) < kRecoveryRetrySeconds * IN_MILLISECONDS)
        return;

    DungeonRoute const* route = sDungeonRouteMgr.GetByLfgId(st.lfgId);
    if (!route)
        return;
    DungeonRouteMgr::Entrance entrance;
    if (!sDungeonRouteMgr.GetEntrance(*route, entrance))
        return;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld())
            continue;
        // Only the stranded dead. A living member outside the instance is a different problem
        // (never entered, or walked out) and is not this function's business; a dead member still
        // inside can reach its own corpse unaided, which is exactly what we want it to do.
        if (member->IsAlive() || member->GetMapId() == st.mapId)
            continue;

        LOG_INFO("playerbots.dungeonlead",
                 "[DungeonLead] {} died and was released to map {} - returning it to the map {} entrance "
                 "so it can reach its corpse (run={})",
                 member->GetName(), member->GetMapId(), st.mapId, st.runId);
        DungeonLead::RecordEvent(botAI, "wipe_recovery_teleport", member->GetName());

        member->GetMotionMaster()->Clear();
        member->TeleportTo(st.mapId, entrance.x, entrance.y, entrance.z, entrance.o);
        st.recoveryTs = getMSTime();
    }
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
            if (goneSt.state == DungeonLeadKernel::LeadState::Stopping)
            {
                // run summary was already written when Stop() ran - only the handback was pending
                LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] {} disconnected before the leader "
                          "handback was confirmed (run={})", goneSt.tankName, goneSt.runId);
                sDungeonRouteMgr.ResetState(guid);
                continue;
            }
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

        // Starting/Stopping sessions only wait for a leadership change - none of the strategy
        // reassertion or wipe handling below applies until (or after) the tank actually leads.
        if (!DungeonLeadKernel::IsActive(sDungeonRouteMgr.State(guid).state))
        {
            ReconcileLeadership(botAI);
            continue;
        }

        Group* group = bot->GetGroup();
        if (!group)
            continue;

        // The route-walk action does not run while the leader is dead, so death and wipe recovery
        // are driven from here, through DungeonLeadBrain::Update() below (WipeRecovery state,
        // WipeRecoverySeconds / MaxWipesPerRun give-up). Measured 2026-09-16: upstream's own
        // dead-state chain (release, corpse run, resurrect) works but needs minutes, not 45 s.
        DungeonLeadState& st = sDungeonRouteMgr.State(guid);

        // Latch which dungeon this session is actually in, as soon as the leader is standing in
        // one. st.mapId is otherwise only set by ResolveRoute(), which runs from the route-walk
        // action - and that action is deliberately suppressed while the party is in combat. A
        // party that wipes on the first pull therefore never reaches it, leaving st.mapId at 0
        // with nothing for the recovery below to compare against. Measured live: the tank died on
        // Lady Anacondra with stMap=0, so RecoverStrandedMembers() returned immediately and the
        // wipe recovery could not run at all. ResolveRoute() still owns the value afterwards - it
        // re-derives map and instance together on its first pass regardless of what is here.
        if (!st.mapId && bot->IsAlive() && DungeonLead::InFiveMan(bot))
        {
            st.mapId = bot->GetMapId();
            st.instanceId = bot->GetInstanceId();
        }

        // Pull every party member - leader included - back onto the instance map if death has
        // stranded them outside it. See DungeonLeadState::recoveryTs for the measurements behind
        // this; the short version is that upstream's own recovery chain is fine except for one
        // step it cannot perform (pathing to a corpse on another map), and putting the ghost back
        // on the right map is enough for it to finish on its own. We deliberately aim at the
        // route's entrance rather than where the member died: the entrance is a known-walkable,
        // mob-free spot, and a corpse run from there is an ordinary same-map run that works.
        DungeonLead::RecoverStrandedMembers(botAI, st);
        DungeonLead::KeepInstanceValid(botAI);
        DungeonLead::FireAreaTriggers(botAI, st);

        // Death, wipe recovery and its give-up live in the brain (WipeRecovery state).
        DungeonPartySnapshot const party = DungeonPartyState::Evaluate(botAI);
        DungeonTargetManager::Update(botAI);
        DungeonPullController::Update(botAI, party);
        if (!DungeonInteractionController::Update(botAI))
            continue;  // session ended (blocked for good)
        if (!DungeonRecoveryController::Update(botAI, party))
            continue;  // session ended (recovery failed)
        if (!DungeonLeadBrain::Update(botAI, party))
            continue;  // session ended (wipe give-up)
        if (!bot->IsAlive())
            continue;  // nothing below applies to a corpse

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
    if (!sDungeonRouteMgr.HasState(bot->GetGUID()))
        return;  // nothing to stop - and State() below would otherwise create an entry

    switch (sDungeonRouteMgr.State(bot->GetGUID()).state)
    {
        case DungeonLeadKernel::LeadState::Stopping:
            return;  // already stopped, handback in flight - GuardActiveSessions() finishes it
        case DungeonLeadKernel::LeadState::Starting:
            // nothing applied yet (strategies wait for confirmed leadership), so nothing to restore
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} stopped while still acquiring leadership",
                     bot->GetName());
            sDungeonRouteMgr.ResetState(bot->GetGUID());
            return;
        default:
            break;
    }

    // capture what we own BEFORE the state disappears below - ResetState() erases ccGuid/skullGuid/
    // memberSnapshots, and without this a stop while a moon mark was active left that mark
    // permanently excluding its target from normal DPS priority with nothing left to ever release
    // it, and followers had no way back to whatever they were doing before "startdungeon"
    DungeonLeadState const& st = sDungeonRouteMgr.State(bot->GetGUID());
    ObjectGuid const ownedCc = st.ccGuid;
    ObjectGuid const ownedSkull = st.skullGuid;
    ObjectGuid const ownedCross = st.crossGuid;
    std::vector<DungeonLeadMemberSnapshot> const snapshots = st.memberSnapshots;
    DungeonLeadMemberSnapshot const leaderSnap = st.leaderSnapshot;
    bool const hasLeaderSnap = st.hasLeaderSnapshot;
    uint64 const runId = st.runId;
    DungeonLeadSessionOrigin const origin = st.origin;
    std::string const tankName = st.tankName;
    ObjectGuid handbackTo;

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
        if (!ownedCross.IsEmpty() && group->GetTargetIcon(RtiTargetValue::crossIndex) == ownedCross)
            group->SetTargetIcon(RtiTargetValue::crossIndex, bot->GetGUID(), ObjectGuid::Empty);
        if (!ownedCc.IsEmpty() && group->GetTargetIcon(RtiTargetValue::moonIndex) == ownedCc)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);

        Player* master = botAI->GetMaster();
        if (giveLeaderBack && master && master != bot && group->IsMember(master->GetGUID()) &&
            group->GetLeaderGUID() == bot->GetGUID())
            handbackTo = master->GetGUID();
    }

    if (handbackTo.IsEmpty())
    {
        sDungeonRouteMgr.ResetState(bot->GetGUID());
        return;
    }
    DungeonLeadBrain::TransitionTo(botAI, sDungeonRouteMgr.State(bot->GetGUID()), DungeonLeadKernel::LeadState::Stopping,
                                   DungeonLeadKernel::TransitionReason::StopRequested);
    uint32 const stoppingSinceTs = sDungeonRouteMgr.State(bot->GetGUID()).stateSinceTs;
    sDungeonRouteMgr.ResetState(bot->GetGUID());

    // Queueing the handback is not the handback (audit AUDIT-003): the session stays registered
    // as Stopping - strategies are already restored above, so nothing else runs for it - until
    // GuardActiveSessions() observes the original owner as leader again, retrying a bounded
    // number of times. A failed enqueue counts as an attempt and is retried the same way.
    DungeonLeadState& stopping = sDungeonRouteMgr.State(bot->GetGUID());
    stopping.state = DungeonLeadKernel::LeadState::Stopping;  // carried over, transition logged above
    stopping.stateSinceTs = stoppingSinceTs;
    stopping.runId = runId;
    stopping.origin = origin;
    stopping.tankName = tankName;
    stopping.leadershipTarget = handbackTo;
    stopping.leadershipFrom = bot->GetGUID();
    RequestLeadership(bot, stopping);
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
    if (DungeonLead::HasSession(botAI) || DungeonLead::IsOn(botAI))
    {
        LOG_ERROR("playerbots.dungeonlead",
                   "[DungeonLead] {} StartSession refused - already has a session (call Stop() first)",
                   bot->GetName());
        return false;
    }

    // 2026-09-16 (independent architecture review DL-009 - "two tank bots receive start before
    // the world-thread leader operation executes... both install Dungeon Lead and record active
    // state"): the check above only looks at THIS bot's own state - nothing stopped a second
    // member of the SAME group from independently becoming a second active leader for it, which
    // is the review's actual named failure, not a data race in the C++ sense (this function runs
    // single-threaded, start to finish, on the world thread - see GuardActiveSessions()' own
    // fork-join note). Scan the group directly instead of trusting per-bot state alone: if any
    // OTHER member is already leading, refuse - a group has at most one active leader by
    // construction, whichever member asked first.
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot)
            continue;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        if (memberAI && (DungeonLead::HasSession(memberAI) || DungeonLead::IsOn(memberAI)))
        {
            LOG_ERROR("playerbots.dungeonlead",
                       "[DungeonLead] {} StartSession refused - {} is already leading this group "
                       "(call Stop() on them first)", bot->GetName(), member->GetName());
            return false;
        }
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

    sDungeonRouteMgr.ResetState(bot->GetGUID());
    {
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        st.debugMode = sDungeonLeadConfig.dungeonLeadDebugDefault;
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

    if (group->GetLeaderGUID() == bot->GetGUID())
    {
        ActivateSession(botAI, group, master, DungeonLeadKernel::TransitionReason::SessionStart);
        return true;
    }

    // Not leader yet (audit AUDIT-002): a queued GroupSetLeaderOperation is not leadership - it
    // runs later on the world thread and can be dropped or overtaken. The session waits in
    // Starting, with no strategies applied, until GuardActiveSessions() observes the tank as
    // leader; the request is retried a bounded number of times and the start abandoned if it
    // never lands.
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    DungeonLeadBrain::TransitionTo(botAI, st, DungeonLeadKernel::LeadState::Starting,
                                   DungeonLeadKernel::TransitionReason::SessionStart);
    st.leadershipTarget = bot->GetGUID();
    st.leadershipFrom = group->GetLeaderGUID();
    RequestLeadership(bot, st);
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} STARTING by {} origin={} - waiting to become group leader",
             bot->GetName(), master ? master->GetName() : "canary", ToString(origin));
    DungeonLead::RecordEvent(botAI, "starting", "origin=" + std::string(ToString(origin)));
    if (master)
        botAI->TellMaster("Dungeon lead: taking the lead...");
    return true;
}

// Starting -> Active, once the tank is confirmed as group leader (or already was).
void DungeonLead::ActivateSession(PlayerbotAI* botAI, Group* group, Player* master,
                                  DungeonLeadKernel::TransitionReason reason)
{
    Player* bot = botAI->GetBot();
    DungeonLeadSessionOrigin origin;
    {
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        DungeonLeadBrain::TransitionTo(botAI, st, DungeonLeadKernel::LeadState::WaitingReady, reason);
        st.leadershipTarget = ObjectGuid::Empty;
        st.leadershipFrom = ObjectGuid::Empty;
        st.leadershipRequestTs = 0;
        st.leadershipAttempts = 0;
        origin = st.origin;
    }

    ApplyLeaderFollowerStrategies(botAI, group);
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
}

// One leadership request. Counted as an attempt whether or not the enqueue itself succeeds - a
// full queue is just a request that will not be observed, and is retried on the same schedule.
void DungeonLead::RequestLeadership(Player* bot, DungeonLeadState& st)
{
    ++st.leadershipAttempts;
    st.leadershipRequestTs = getMSTime();
    auto op = std::make_unique<GroupSetLeaderOperation>(bot->GetGUID(), st.leadershipTarget);
    bool const queued = PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op));
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        DungeonLead::RecordEvent(botAI, "leadership_request",
                                 std::string(st.state == DungeonLeadKernel::LeadState::Stopping ? "handback" : "acquire") +
                                     " attempt=" + std::to_string(st.leadershipAttempts) +
                                     " queued=" + (queued ? "1" : "0"));
    if (!queued)
        LOG_ERROR("playerbots.dungeonlead",
                  "[DungeonLead] {} could not queue leader change (attempt {}, world thread queue full)",
                  bot->GetName(), st.leadershipAttempts);
}

// Drives a Starting or Stopping session to its end: observe the group leader, then confirm,
// wait, retry or give up. The decision itself is DungeonLeadKernel::DecideLeadership (tests/).
void DungeonLead::ReconcileLeadership(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    bool const starting = st.state == DungeonLeadKernel::LeadState::Starting;
    Group* group = bot->GetGroup();

    DungeonLeadKernel::LeadershipObservation obs;
    if (group)
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        obs.targetIsLeader = leader == st.leadershipTarget;
        obs.targetEligible =
            ObjectAccessor::FindPlayer(st.leadershipTarget) && group->IsMember(st.leadershipTarget);
        obs.thirdPartyIsLeader = leader != st.leadershipTarget && leader != st.leadershipFrom;
    }
    else
        obs.targetEligible = false;  // no group left - nothing to lead or hand back
    obs.msSinceRequest = GetMSTimeDiffToNow(st.leadershipRequestTs);
    obs.attempts = st.leadershipAttempts;

    DungeonLeadKernel::LeadershipPolicy policy;
    policy.timeoutMs = (starting ? sDungeonLeadConfig.dungeonLeadLeadershipAcquireTimeoutSeconds
                                 : sDungeonLeadConfig.dungeonLeadLeadershipReturnTimeoutSeconds) * IN_MILLISECONDS;
    policy.maxAttempts = uint8(sDungeonLeadConfig.dungeonLeadLeadershipMaxAttempts);

    char const* const what = starting ? "leadership" : "leader handback";
    switch (DungeonLeadKernel::DecideLeadership(obs, policy))
    {
        case DungeonLeadKernel::LeadershipStep::Wait:
            return;
        case DungeonLeadKernel::LeadershipStep::Retry:
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} {} not observed after {} ms - retrying ({}/{})",
                     bot->GetName(), what, obs.msSinceRequest, st.leadershipAttempts + 1, policy.maxAttempts);
            RequestLeadership(bot, st);
            return;
        case DungeonLeadKernel::LeadershipStep::Confirmed:
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} {} confirmed after {} attempt(s) (run={})",
                     bot->GetName(), what, st.leadershipAttempts, st.runId);
            DungeonLead::RecordEvent(botAI, "leadership_confirmed",
                                     std::string(starting ? "acquire" : "handback") +
                                         " attempts=" + std::to_string(st.leadershipAttempts));
            if (starting)
                ActivateSession(botAI, group,
                                st.origin == DungeonLeadSessionOrigin::Manual ? botAI->GetMaster() : nullptr,
                                DungeonLeadKernel::TransitionReason::LeadershipConfirmed);
            else
                sDungeonRouteMgr.ResetState(bot->GetGUID());
            return;
        case DungeonLeadKernel::LeadershipStep::GiveUp:
        case DungeonLeadKernel::LeadershipStep::Abandon:
            break;
    }

    std::string const reason = !obs.targetEligible ? "target no longer in the group"
                             : obs.thirdPartyIsLeader ? "someone else became leader"
                             : "not observed after " + std::to_string(st.leadershipAttempts) + " attempt(s)";
    if (starting)
    {
        // Fail safe: no strategies were applied yet, so dropping the session leaves the party as it was.
        LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] {} could not become group leader ({}) - not starting (run={})",
                  bot->GetName(), reason, st.runId);
        st.outcome = DungeonRunOutcome::Failed;
        st.failureDomain = DungeonFailureDomain::PartyCoordination;
        DungeonLead::RecordEvent(botAI, "leadership_failed", "acquire " + reason);
        DungeonLead::RecordRunSummary(botAI, "leadership_not_acquired");
        if (st.origin == DungeonLeadSessionOrigin::Manual)
            botAI->TellMaster("Dungeon lead: could not take the party lead - not starting");
    }
    else
    {
        LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] {} leader handback failed ({}) (run={})",
                  bot->GetName(), reason, st.runId);
        DungeonLead::RecordEvent(botAI, "leadership_failed", "handback " + reason);
    }
    sDungeonRouteMgr.ResetState(bot->GetGUID());
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

    // The brain decides whether to walk (Travelling); this only reports why not.
    DungeonPartySnapshot const party = DungeonPartyState::Evaluate(botAI);
    if (!DungeonLeadBrain::Update(botAI, party))
        return false;
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
    if (st.paused)
        return false;  // "startdungeon pause": stand still until "startdungeon continue"

    // "waiting for you": a one-shot chat ping on the transition into master-too-far, not spammed
    // every tick, and cleared as soon as the player is back in range.
    DungeonLeadKernel::ReadinessPolicy const policy = DungeonPartyState::Policy();
    bool masterTooFar = DungeonLeadKernel::MasterTooFar(party.facts, policy);
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
    Player* spreadMember =
        masterTooFar ? nullptr : party.Member(DungeonLeadKernel::FindSpreadMember(party.facts, policy));
    std::string const spreadName = spreadMember ? spreadMember->GetName() : std::string();
    if (spreadMember && st.spreadOffenderTold != spreadName)
    {
        st.spreadOffenderTold = spreadName;
        botAI->TellMaster("We're waiting for " + spreadName + " to catch up!");
    }
    else if (!spreadMember)
        st.spreadOffenderTold.clear();

    DungeonLeadKernel::Readiness const ready =
        DungeonLeadKernel::EvaluateReadiness(party.facts, policy, DungeonLeadKernel::ReadyPurpose::Walk);
    char const* wait = ready.status == DungeonLeadKernel::ReadyStatus::Ready ? nullptr : DungeonLeadKernel::ToString(ready.status);
    std::string waitDetail;
    if (Player* offender = party.Member(ready.offender))
    {
        waitDetail = offender->GetName();
        // a fight that doesn't end: who is it with, and how far away
        if (ready.status == DungeonLeadKernel::ReadyStatus::PartyInCombat)
        {
            Unit* nearest = nullptr;
            for (Unit* a : offender->getAttackers())
                if (!nearest || offender->GetDistance(a) < offender->GetDistance(nearest))
                    nearest = a;
            waitDetail += " attackers=" + std::to_string(offender->getAttackers().size());
            if (nearest)
                waitDetail += " nearest=" + nearest->GetName() + " dist=" + std::to_string(int(offender->GetDistance(nearest))) +
                              " evading=" + std::to_string(nearest->ToCreature() && nearest->ToCreature()->IsInEvadeMode());
            if (Unit* victim = offender->GetVictim())
                waitDetail += " victim=" + victim->GetName();
        }
    }

    if (st.state != DungeonLeadKernel::LeadState::Travelling)
    {
        // throttled: one line per 10 s per bot
        uint32 now = getMSTime();
        if (wait && (!st.lastWaitLogTs || now - st.lastWaitLogTs > 10000))
        {
            st.lastWaitLogTs = now;

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

void DungeonLead::SetPackState(PlayerbotAI* botAI, DungeonLeadState& st, DungeonPack const& pack,
                               DungeonLeadKernel::PackState next)
{
    if (st.packId != pack.id)
    {
        st.packId = pack.id;  // a new pack: its state starts over
        st.packState = DungeonLeadKernel::PackState::Unknown;
        st.packLocked.clear();
        st.packResolutionKey = 0;
    }
    if (st.packState == next)
        return;
    std::string const line = "#" + std::to_string(pack.id) + " " + pack.name + " (" + ToString(pack.type) + ") " +
                             DungeonLeadKernel::ToString(st.packState) + "->" + DungeonLeadKernel::ToString(next);
    st.packState = next;
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} pack {}", botAI->GetBot()->GetName(), line);
    DungeonLead::RecordEvent(botAI, "pack_state", line);
}

DungeonPackSighting DungeonLead::TrackPack(PlayerbotAI* botAI, DungeonLeadState& st, DungeonPack const& pack)
{
    bool const samePack = st.packId == pack.id;
    std::vector<ObjectGuid> const noLock;
    DungeonPackSighting sighting =
        DungeonPacks::Observe(botAI->GetBot(), pack, st.instanceId, samePack ? st.packLocked : noLock);
    DungeonLeadKernel::PackState const prev = samePack ? st.packState : DungeonLeadKernel::PackState::Unknown;
    SetPackState(botAI, st, pack, DungeonLeadKernel::DecidePackState(prev, sighting.observation));

    // Once the pack itself is fighting, its membership is fixed - a neighbouring group with the
    // same entry can't join it afterwards except by actually attacking the party (as an add).
    if (st.packState == DungeonLeadKernel::PackState::Engaged && st.packLocked.empty())
        st.packLocked = sighting.members;

    if (st.debugMode)
    {
        // membership, rejections and the lock - not the add count, which changes all fight long
        uint32 const key = uint32(sighting.members.size()) | (sighting.rejected << 16) |
                           (uint32(st.packLocked.empty() ? 0 : 1) << 24);
        if (key != st.packResolutionKey)
        {
            st.packResolutionKey = key;
            std::string entries;
            for (uint32 e : pack.expectedEntries)
                entries += (entries.empty() ? "" : "/") + std::to_string(e);
            DungeonLead::RecordEvent(botAI, "pack_resolution",
                                     "#" + std::to_string(pack.id) + " entries=" + entries +
                                         " members=" + std::to_string(sighting.members.size()) +
                                         " adds=" + std::to_string(sighting.engagedAdds) +
                                         " rejected=" + std::to_string(sighting.rejected) +
                                         " reason=" + (st.packLocked.empty() ? "spawn_area" : "locked_on_engage"));
        }
    }
    return sighting;
}

void DungeonLead::AdvanceStep(DungeonLeadState& st, bool confirmed)
{
    if (confirmed)
        st.checkpointStep = int32(st.stepIndex);
    if (st.stepIndex < st.visited.size())
        st.visited[st.stepIndex] = 1;
    ++st.stepIndex;
    st.bestDist = 0.f;
    st.stuckAttempts = 0;
    st.stuckTs = 0;
    st.unstuckUsed = false;
    st.eventWaitMs = st.eventWaitLastTs = st.talkTriedTs = 0;
    // the next step's pack and pull (if any) start from scratch
    st.packId = 0;
    st.packState = DungeonLeadKernel::PackState::Unknown;
    st.packLocked.clear();
    st.pullState = DungeonLeadKernel::PullState::None;
    st.pullStateTs = 0;
    st.pullAttempts = 0;
    st.pullOrderRefused = false;
    st.pullFights = 0;
    st.pullPackFought = false;
    st.objectiveFailures = 0;
    st.interactionType = DungeonLeadKernel::InteractionType::None;
    st.interactionState = DungeonLeadKernel::InteractionState::None;
    st.targetPrimary = st.targetSecondary = st.targetCc = ObjectGuid::Empty;  // that fight's plan is done
    st.ccFailed.clear();
}

void DungeonLead::SkipStep(DungeonLeadState& st, DungeonRouteStep const& step, DungeonFailureDomain domain,
                           DungeonFailureReason reason)
{
    st.skippedSteps.push_back(step.boss);
    if (step.IsMandatory())
    {
        st.mandatorySkipped = true;
        st.outcome = DungeonRunOutcome::Partial;
        st.failureDomain = domain;
        st.failureReason = reason;
    }
    AdvanceStep(st, /*confirmed*/ false);
}

void DungeonLead::ResetStepState(DungeonLeadState& st)
{
    st.packId = 0;
    st.packState = DungeonLeadKernel::PackState::Unknown;
    st.packLocked.clear();
    st.pullState = DungeonLeadKernel::PullState::None;
    st.pullStateTs = 0;
    st.pullAttempts = 0;
    st.pullOrderRefused = false;
    st.pullFights = 0;
    st.pullPackFought = false;
    st.anchorSet = false;
    st.targetPrimary = st.targetSecondary = st.targetCc = ObjectGuid::Empty;
    st.arrivedTold = false;
    st.bestDist = 0.f;
    st.stuckTs = 0;
    st.stuckAttempts = 0;
    st.unstuckUsed = false;
    st.eventWaitMs = st.eventWaitLastTs = st.talkTriedTs = 0;
    st.ccFailed.clear();
    st.interactionType = DungeonLeadKernel::InteractionType::None;
    st.interactionState = DungeonLeadKernel::InteractionState::None;
}

bool DungeonLead::FailObjective(PlayerbotAI* botAI, DungeonLeadState& st, DungeonRouteStep const& step,
                                DungeonFailureDomain domain, DungeonFailureReason reason, std::string const& why)
{
    DungeonObjectiveRequirement const requirement = step.Requirement();
    ++st.objectiveFailures;
    DungeonLeadKernel::ObjectiveFailureAction const action = DungeonLeadKernel::DecideObjectiveFailure(
        requirement, st.objectiveFailures, sDungeonLeadConfig.dungeonLeadObjectiveRetryRounds);
    std::string const detail = step.boss + " requirement=" + ToString(requirement) + " why=" + why +
                               " round=" + std::to_string(st.objectiveFailures) + " action=" +
                               DungeonLeadKernel::ToString(action);
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} objective failed: {}", botAI->GetBot()->GetName(), detail);

    switch (action)
    {
        case DungeonLeadKernel::ObjectiveFailureAction::Skip:
        {
            if (DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr)
            {
                DungeonPack const pack = DungeonPacks::ForStep(*route, st.stepIndex);
                if (pack.Exists())
                    SetPackState(botAI, st, pack, DungeonLeadKernel::PackState::Skipped);
            }
            DungeonLead::RecordEvent(botAI, "objective_skipped", detail);
            botAI->TellMasterNoFacing("Dungeon lead: can't do " + step.boss + ", moving on");
            SkipStep(st, step, domain, reason);
            return false;
        }
        case DungeonLeadKernel::ObjectiveFailureAction::Retry:
            DungeonLead::RecordEvent(botAI, "objective_retry", detail);
            botAI->TellMasterNoFacing("Dungeon lead: " + step.boss + " didn't work out, trying again");
            ResetStepState(st);
            return false;
        case DungeonLeadKernel::ObjectiveFailureAction::Abort:
            break;
    }

    // Out of rounds on something that must not be skipped: stop here rather than walk past it.
    st.outcome = DungeonRunOutcome::Partial;
    st.failureDomain = domain;
    st.failureReason = reason;
    st.skippedSteps.push_back(step.boss);
    st.mandatorySkipped = true;
    DungeonLead::RecordEvent(botAI, "objective_failed", detail);
    DungeonLead::RecordRunSummary(botAI, "objective_failed");
    botAI->TellMasterNoFacing("Dungeon lead: can't get past " + step.boss + " - stopping here");
    Stop(botAI, /*giveLeaderBack*/ true);
    return true;
}

void DungeonLead::RestoreCheckpoint(PlayerbotAI* botAI, DungeonLeadState& st)
{
    DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr;
    uint32 const from = st.stepIndex;
    uint32 const to = DungeonLeadKernel::ResumeStepAfterWipe(st.checkpointStep, st.stepIndex);

    if (route && to < from)
    {
        // Reopen what was only passed over since the checkpoint; drop those from the skipped list
        // and take back a Partial verdict that only they caused - they get a fresh try.
        for (uint32 i = to; i < from && i < route->steps.size(); ++i)
        {
            if (i < st.visited.size())
                st.visited[i] = 0;
            std::string const& name = route->steps[i].boss;
            st.skippedSteps.erase(std::remove(st.skippedSteps.begin(), st.skippedSteps.end(), name),
                                  st.skippedSteps.end());
        }
        bool mandatoryLeft = false;
        for (std::string const& name : st.skippedSteps)
            for (DungeonRouteStep const& s : route->steps)
                if (s.boss == name && s.IsMandatory())
                    mandatoryLeft = true;
        if (st.mandatorySkipped && !mandatoryLeft && st.outcome == DungeonRunOutcome::Partial)
        {
            st.outcome = DungeonRunOutcome::Running;
            st.failureDomain = DungeonFailureDomain::None;
            st.failureReason = DungeonFailureReason::None;
        }
        st.mandatorySkipped = mandatoryLeft;
        st.stepIndex = to;
    }

    // Whatever the current step is, look at it afresh: packs reset when the party wipes.
    ResetStepState(st);
    st.objectiveFailures = 0;

    std::string const checkpoint = route && st.checkpointStep >= 0 && size_t(st.checkpointStep) < route->steps.size()
                                       ? route->steps[st.checkpointStep].boss
                                       : std::string("none");
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} checkpoint restore: step {} -> {} (checkpoint {})",
             botAI->GetBot()->GetName(), from, st.stepIndex, checkpoint);
    DungeonLead::RecordEvent(botAI, "checkpoint_restore", "from=" + std::to_string(from) + " to=" +
                                                              std::to_string(st.stepIndex) + " checkpoint=" + checkpoint);
}

bool DungeonLeadNextAction::Execute(Event /*event*/)
{
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());

    DungeonRoute const* route = ResolveRoute(st);
    // Waiting at a closed door (DungeonInteractionController): hold here until it opens or fails.
    if (route && DungeonLeadKernel::InteractionActive(st.interactionState))
        return false;
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
        // SkipOptional skips optional fights (Pull nodes) only - a path anchor on an optional row
        // is a Travel node and must stay, or routes that need it stop being walkable (DL-011).
        bool skip = st.visited[st.stepIndex] || !s.IsWalkable() ||
                    (s.NodeType() == DungeonRouteNodeType::Pull && sDungeonLeadConfig.dungeonLeadSkipOptional) ||
                    (s.entry && !s.IsInteractionStep() && sDungeonRouteMgr.IsStepKilled(st.instanceId, s.entry));
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
                       << " | wipes " << st.wipeCount;
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
        headingOut << "Dungeon lead: heading to " << step.boss << " (" << ToString(step.NodeType()) << ")";
        botAI->TellMasterNoFacing(headingOut);
    }

    if (step.NodeType() == DungeonRouteNodeType::Door)
        return WalkDoorStep(st, dest, step);
    if (step.kind == DungeonRouteKind::Use)
        return WalkUseStep(st, dest, step);
    if (step.kind == DungeonRouteKind::Talk)
        return WalkTalkStep(st, dest, step);

    // Pack nodes (pull/boss/interaction) advance only once their pack is Cleared (or Skipped
    // below) - never just because the tank reached the coordinate. Travel nodes have no pack.
    DungeonPack const pack = DungeonPacks::ForStep(*route, st.stepIndex);
    DungeonPackSighting sighting;
    if (pack.Exists())
    {
        sighting = DungeonLead::TrackPack(botAI, st, pack);
        if (sighting.firstAlive)
            dest = WorldPosition(sighting.firstAlive);  // prefer the live creature position
    }
    if (st.packState == DungeonLeadKernel::PackState::Cleared && pack.Exists())
    {
        std::ostringstream out;
        out << "Dungeon lead: " << step.boss << " is down, moving on";
        botAI->TellMasterNoFacing(out);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}' already dead, next", bot->GetName(), step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "already_dead", step.boss);
        if (step.entry)
            sDungeonRouteMgr.MarkStepKilled(st.instanceId, step.entry);
        DungeonLead::AdvanceStep(st, /*confirmed*/ true);
        return true;
    }

    float dist = bot->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
    if (dist <= sDungeonLeadConfig.dungeonLeadArriveDistance)
    {
        if (!pack.Exists())
        {
            // A travel node is done once reached - there is nothing to fight or confirm. (Before
            // typed nodes, path anchors waited StuckSeconds here and then logged "not_found".)
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} passed step {} '{}'", bot->GetName(), step.step, step.boss);
            DungeonLead::RecordEvent(botAI, "travel_reached", step.boss);
            DungeonLead::AdvanceStep(st, /*confirmed*/ true);
            return true;
        }

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
        // A live pull/boss pack is the pull controller's (bounded attempts and fights); this timeout
        // is for what it can't handle - an interaction it can't perform, or nothing there at all.
        else if (!((pack.type == DungeonRouteNodeType::Pull || pack.type == DungeonRouteNodeType::Boss) &&
                   sighting.observation.alive > 0) &&
                 GetMSTimeDiffToNow(st.arrivedTs) >= sDungeonLeadConfig.dungeonLeadStuckSeconds * IN_MILLISECONDS)
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
            bool const stillAlive = sighting.observation.alive > 0;
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead] {} step {} '{}' - {}, giving up", bot->GetName(), step.step, step.boss,
                     stillAlive ? "target alive but not progressing (needs an interaction this module can't do)"
                                : "nothing at destination");
            DungeonLead::RecordEvent(botAI, stillAlive ? "stuck_alive" : "not_found", step.boss);
            DungeonLead::FailObjective(botAI, st, step, DungeonFailureDomain::Navigation,
                                       DungeonFailureReason::ObjectiveTimeout, stillAlive ? "stuck_alive" : "not_found");
        }
        return true;
    }

    return MoveRouteTo(st, dest, step);
}

// A door step with its game object in the route data: done once the world shows that door open
// (seen from wherever the leader is); a closed one is waited for at the door by the interaction
// controller (DoorWaitSeconds, then the objective policy - a door is Required); no such object at
// the spot means nothing is in the way.
bool DungeonLeadNextAction::WalkDoorStep(DungeonLeadState& st, WorldPosition const& dest, DungeonRouteStep const& step)
{
    float const dist = bot->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
    GameObject* door = nullptr;
    if (dist < kDoorSightRange)
        if (GameObject* go = bot->FindNearestGameObject(step.entry, kDoorSightRange))
            if (go->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ()) < 10.0f)
                door = go;

    if (door && door->GetGoState() != GO_STATE_READY)
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}' is open", bot->GetName(), step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "door_open", step.boss + " entry=" + std::to_string(step.entry));
        DungeonLead::AdvanceStep(st, /*confirmed*/ true);
        return true;
    }
    if (dist > std::max(sDungeonLeadConfig.dungeonLeadArriveDistance, kDoorWaitDistance))
        return MoveRouteTo(st, dest, step);

    if (!door)
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}': no door here, moving on", bot->GetName(),
                 step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "door_not_found", step.boss + " entry=" + std::to_string(step.entry));
        DungeonLead::AdvanceStep(st, /*confirmed*/ false);
        return true;
    }
    DungeonInteractionController::StartForDoor(botAI, st, door, "route");
    return true;
}

// Time a use/talk step has waited for its event - counted only while the route walk runs here
// (Travelling), so the fights an event brings don't use it up. True once EventWaitSeconds is spent.
bool DungeonLeadNextAction::EventWaitExpired(DungeonLeadState& st)
{
    uint32 const now = getMSTime();
    if (st.eventWaitLastTs)
    {
        uint32 const delta = getMSTimeDiff(st.eventWaitLastTs, now);
        if (delta < 5000)  // consecutive walk ticks; a longer gap was a fight or a recovery
            st.eventWaitMs += delta;
    }
    st.eventWaitLastTs = now;
    return st.eventWaitMs >= sDungeonLeadConfig.dungeonLeadEventWaitSeconds * IN_MILLISECONDS;
}

// "use" step: the leader goes to the object; if its lock takes a key, a party member holding it
// uses it (that's who could, in the client); otherwise the leader. Done once used.
bool DungeonLeadNextAction::WalkUseStep(DungeonLeadState& st, WorldPosition const& dest, DungeonRouteStep const& step)
{
    float const dist = bot->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
    if (dist > INTERACTION_DISTANCE)
        return MoveRouteTo(st, dest, step);

    GameObject* go = bot->FindNearestGameObject(step.entry, 15.0f);
    if (!go)
    {
        DungeonLead::RecordEvent(botAI, "use_failed", step.boss + " not found");
        DungeonLead::FailObjective(botAI, st, step, DungeonFailureDomain::Encounter,
                                   DungeonFailureReason::ObjectiveTimeout, "use_target_missing");
        return true;
    }

    // a key the lock needs, and who in the party has it; or a lock that opens by hand, which the
    // client opens by casting the matching "Opening" spell (a chest: that is what loots it)
    Player* user = bot;
    uint32 keyItem = 0;
    uint32 openSpell = 0;
    if (LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId()))
        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] == LOCK_KEY_ITEM && lock->Index[i] && !keyItem)
                keyItem = lock->Index[i];
            else if (lock->Type[i] == LOCK_KEY_SKILL && lock->Skill[i] == 0 && !openSpell)
                openSpell = lock->Index[i] == LOCKTYPE_OPEN         ? 3365u   // Opening
                            : lock->Index[i] == LOCKTYPE_QUICK_OPEN ? 6247u   // Opening (quick)
                            : lock->Index[i] == LOCKTYPE_OPEN_KNEELING ? 6478u  // Opening (kneeling)
                                                                    : 0u;
        }
    if (keyItem)
    {
        user = nullptr;
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref && !user; ref = ref->next())
                if (Player* m = ref->GetSource())
                    if (m->IsAlive() && m->GetMap() == bot->GetMap() && m->HasItemCount(keyItem, 1))
                        user = m;
        if (!user)
        {
            if (EventWaitExpired(st))
            {
                DungeonLead::FailObjective(botAI, st, step, DungeonFailureDomain::Encounter,
                                           DungeonFailureReason::ObjectiveTimeout, "use_no_key");
                return true;
            }
            // Nobody has it: a corpse nearby that holds it (Zul'Farrak: the Executioner's Key) is
            // looted by the leader, as a player would - the bots' own looting may never pick it up.
            for (ObjectGuid const& guid : AI_VALUE(GuidVector, "nearest corpses"))
            {
                Creature* corpse = botAI->GetCreature(guid);
                if (!corpse || corpse->IsAlive())
                    continue;
                auto const it = std::find_if(corpse->loot.items.begin(), corpse->loot.items.end(),
                                             [&](LootItem const& li) { return li.itemid == keyItem && !li.is_looted; });
                if (it == corpse->loot.items.end())
                    continue;
                if (bot->GetDistance(corpse) > INTERACTION_DISTANCE)
                    return MoveRouteTo(st, WorldPosition(corpse), step);
                WorldPacket open(CMSG_LOOT, 8);
                open << corpse->GetGUID();
                bot->GetSession()->HandleLootOpcode(open);
                WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                take << uint8(it - corpse->loot.items.begin());
                bot->GetSession()->HandleAutostoreLootItemOpcode(take);
                WorldPacket release(CMSG_LOOT_RELEASE, 8);
                release << corpse->GetGUID();
                bot->GetSession()->HandleLootReleaseOpcode(release);
                DungeonLead::RecordEvent(botAI, "key_looted",
                                         std::to_string(keyItem) + " from=" + corpse->GetName() +
                                             " have=" + std::to_string(bot->HasItemCount(keyItem, 1)));
                return true;  // next tick: the key holder (if it worked) uses the object
            }
            if (!st.arrivedTold)
            {
                st.arrivedTold = true;
                DungeonLead::RecordEvent(botAI, "use_waiting", step.boss + " key=" + std::to_string(keyItem));
                botAI->TellMasterNoFacing("Dungeon lead: " + step.boss + " needs a key nobody here has - waiting");
            }
            return true;
        }
    }

    // A key whose item has its own spell (Deadmines: the gunpowder loads and fires the cannon) is
    // used by casting that spell on the object; anything else is a plain use.
    SpellInfo const* keySpell = nullptr;
    if (keyItem)
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(keyItem))
            if (proto->Spells[0].SpellId > 0)
                keySpell = sSpellMgr->GetSpellInfo(proto->Spells[0].SpellId);
    SpellInfo const* opening = !keySpell && openSpell ? sSpellMgr->GetSpellInfo(openSpell) : nullptr;
    if (keySpell)
    {
        SpellCastTargets targets;
        targets.SetGOTarget(go);
        user->CastSpell(targets, keySpell, nullptr, TRIGGERED_FULL_MASK, user->GetItemByEntry(keyItem));
    }
    else if (opening)
    {
        SpellCastTargets targets;
        targets.SetGOTarget(go);
        user->CastSpell(targets, opening, nullptr, TRIGGERED_FULL_MASK);
    }
    else
        go->Use(user);

    // A chest: take what is in it, the way a client does (autostore each slot, release).
    int lootSlots = -1;  // -1: nothing was looted (not a chest, or no loot window opened)
    if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST && user->GetLootGUID() == go->GetGUID())
    {
        lootSlots = int(go->loot.items.size());
        for (uint8 slot = 0; slot < go->loot.items.size(); ++slot)
        {
            WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM, 1);
            take << slot;
            user->GetSession()->HandleAutostoreLootItemOpcode(take);
        }
        WorldPacket release(CMSG_LOOT_RELEASE, 8);
        release << go->GetGUID();
        user->GetSession()->HandleLootReleaseOpcode(release);
    }
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} used {} (entry {}) via {}{}", bot->GetName(), go->GetName(),
             step.entry, user->GetName(), keySpell ? " (key spell)" : opening ? " (opening)" : "");
    DungeonLead::RecordEvent(botAI, "use", step.boss + " entry=" + std::to_string(step.entry) + " by=" + user->GetName() +
                                           (keyItem ? " key=" + std::to_string(keyItem) : "") +
                                           (opening ? " opening=" + std::to_string(openSpell) : "") +
                                           (go->GetGoType() == GAMEOBJECT_TYPE_CHEST
                                                ? " loot_slots=" + std::to_string(lootSlots)
                                                : ""));
    DungeonLead::AdvanceStep(st, /*confirmed*/ true);
    return true;
}

// "talk" step: the leader goes to the NPC (wherever it walked to) and talks to it the way a client
// does (gossip hello, then the first option). An NPC whose event isn't ready offers no option yet:
// keep asking until it does - fights in between are the event running - bounded by
// EventWaitSeconds.
bool DungeonLeadNextAction::WalkTalkStep(DungeonLeadState& st, WorldPosition const& dest, DungeonRouteStep const& step)
{
    Creature* npc = bot->FindNearestCreature(step.entry, 250.0f, /*alive*/ true);
    if (!npc)
    {
        if (EventWaitExpired(st))
            DungeonLead::FailObjective(botAI, st, step, DungeonFailureDomain::Encounter,
                                       DungeonFailureReason::ObjectiveTimeout, "talk_target_missing");
        else if (bot->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ()) > INTERACTION_DISTANCE)
            return MoveRouteTo(st, dest, step);
        return true;
    }
    // Faction-specific NPCs (SFK's prisoners: Adamant for the Horde, Ashcrombe for the Alliance) -
    // the route lists both; the one hostile to the leader isn't ours to talk to.
    if (npc->IsHostileTo(bot))
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}': hostile to us, not ours to talk to",
                 bot->GetName(), step.step, step.boss);
        DungeonLead::RecordEvent(botAI, "talk_not_ours", step.boss);
        DungeonLead::AdvanceStep(st, /*confirmed*/ false);
        return true;
    }
    if (bot->GetDistance(npc) > INTERACTION_DISTANCE)
        return MoveRouteTo(st, WorldPosition(npc), step);

    if (st.talkTriedTs && GetMSTimeDiffToNow(st.talkTriedTs) < 5000)
        return true;
    st.talkTriedTs = getMSTime();

    bot->StopMoving();
    bot->SetFacingToObject(npc);
    WorldPacket hello(CMSG_GOSSIP_HELLO);
    hello << npc->GetGUID();
    bot->GetSession()->HandleGossipHelloOpcode(hello);

    GossipMenu& menu = bot->PlayerTalkClass->GetGossipMenu();
    if (menu.Empty() || menu.GetSenderGUID() != npc->GetGUID())
    {
        if (!st.arrivedTold)
        {
            st.arrivedTold = true;
            DungeonLead::RecordEvent(botAI, "talk_waiting", step.boss);
        }
        bot->PlayerTalkClass->SendCloseGossip();
        if (EventWaitExpired(st))
            DungeonLead::FailObjective(botAI, st, step, DungeonFailureDomain::Encounter,
                                       DungeonFailureReason::ObjectiveTimeout, "talk_not_available");
        return true;
    }

    WorldPacket select(CMSG_GOSSIP_SELECT_OPTION);
    select << npc->GetGUID() << uint32(menu.GetMenuId()) << uint32(0);
    bot->GetSession()->HandleGossipSelectOptionOpcode(select);
    bot->PlayerTalkClass->SendCloseGossip();
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} talked to {} (menu {})", bot->GetName(), npc->GetName(),
             menu.GetMenuId());
    DungeonLead::RecordEvent(botAI, "talk", step.boss + " menu=" + std::to_string(menu.GetMenuId()));
    DungeonLead::AdvanceStep(st, /*confirmed*/ true);
    return true;
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
        if (st.bestDist != 0.f)
        {
            // real progress: this is a spot the walk can path from
            st.lastGoodSet = true;
            st.lastGoodX = bot->GetPositionX();
            st.lastGoodY = bot->GetPositionY();
            st.lastGoodZ = bot->GetPositionZ();
        }
        st.bestDist = disToDest;
        st.stuckTs = now;
        st.stuckAttempts = 0;
    }
    else if (++st.stuckAttempts >= 5 && st.stuckTs &&
             GetMSTimeDiffToNow(st.stuckTs) >= sDungeonLeadConfig.dungeonLeadStuckSeconds * IN_MILLISECONDS)
    {
        // A closed door in the way is something to wait for, not a reason to give up on the step.
        if (DungeonInteractionController::StartIfBlockedByDoor(botAI, st, dest.GetPositionX(), dest.GetPositionY(),
                                                               dest.GetPositionZ()))
            return true;
        float const ground = bot->GetMapHeight(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() + 30.f);
        // A leader left somewhere it can't path from at all (H7: SM Library at z=0 under the floor,
        // Deadmines below the tunnel floor - both after a fight, warrior tank): once per step, go
        // back to the last spot the walk made progress from, then let the walk try again. If it is
        // stuck from there too, the step fails as before.
        float const backDist = bot->GetExactDist(st.lastGoodX, st.lastGoodY, st.lastGoodZ);
        // no ground at all under it (INVALID_HEIGHT): it is inside the rock, any good spot is better
        bool const noGround = ground <= INVALID_HEIGHT + 1.0f;
        if (!st.unstuckUsed && st.lastGoodSet && backDist > 5.0f && (backDist < 80.0f || noGround))
        {
            st.unstuckUsed = true;
            std::string const detail =
                step.boss + " from=(" + std::to_string(int(bot->GetPositionX())) + "," +
                std::to_string(int(bot->GetPositionY())) + "," + std::to_string(int(bot->GetPositionZ())) +
                ") ground=" + std::to_string(int(ground)) + " to=(" + std::to_string(int(st.lastGoodX)) + "," +
                std::to_string(int(st.lastGoodY)) + "," + std::to_string(int(st.lastGoodZ)) + ")";
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} no path from here - back to the last good spot: {}",
                     bot->GetName(), detail);
            DungeonLead::RecordEvent(botAI, "leader_unstuck", detail);
            bot->GetMotionMaster()->Clear();
            bot->NearTeleportTo(st.lastGoodX, st.lastGoodY, st.lastGoodZ, bot->GetOrientation());
            st.bestDist = 0.f;
            st.stuckTs = 0;
            st.stuckAttempts = 0;
            return true;
        }
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
            std::to_string(bot->GetPositionZ()) + ")" +
            // the floor under the bot: tells "stuck on terrain" from "fell under the map"
            // (SM Library, H7: the tank froze at z=0 under an 18 yd floor)
            " ground=" + std::to_string(ground) + " path: " + DungeonLead::DiagnosePath(bot, dest.GetPositionX(),
                                                                                      dest.GetPositionY(),
                                                                                      dest.GetPositionZ()));
        DungeonLead::FailObjective(botAI, st, step, DungeonFailureDomain::Navigation, DungeonFailureReason::PathFailed,
                                   "path");
        return true;
    }

    float const dx = dest.GetPositionX(), dy = dest.GetPositionY(), dz = dest.GetPositionZ();
    // Don't walk through a closed door (ZF: through the End Door, skipping the pyramid event).
    if (GameObject* door = ClosedDoorOnPath(botAI, bot, dx, dy, dz))
    {
        DungeonInteractionController::StartForDoor(botAI, st, door, "on_path");
        return true;
    }
    if (disToDest < kPathFinderDis)
        return MoveTo(dest.GetMapId(), dx, dy, dz, false, false, false, true);

    // SHORT = a path cut at the point limit (a long way): as usable as a whole one. Without it every
    // destination more than ~300 yd of path away was "unreachable" (Deadmines, H7: the leader stood
    // on the upper level unable to head for the cove).
    uint32 const typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY | PATHFIND_SHORT;
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
            // A full path cut at the point limit (SHORT, not INCOMPLETE) is a real way there even when
            // its first stretch leads away - a detour (Deadmines: from the upper level near the
            // entrance down to the cove the way first goes back west). Rejecting it left the leader
            // standing with "no path" (H7, twice at the same spot).
            if ((type & PATHFIND_SHORT) && !(type & PATHFIND_INCOMPLETE) &&
                bot->GetExactDist(endPos.x, endPos.y, endPos.z) > 5.0f)
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

// The target plan (primary/secondary/CC) and its marks are owned by DungeonTargetManager, which
// also runs every GuardActiveSessions() tick; a boss coming into range just refreshes it now.
bool DungeonLeadMarkAction::Execute(Event /*event*/)
{
    return DungeonTargetManager::Update(botAI);
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
                << " | step " << stepName << " | objective " << DungeonLeadBrain::CurrentObjective(st).Describe()
                << " | pack " << (st.packId ? "#" + std::to_string(st.packId) + " " + DungeonLeadKernel::ToString(st.packState)
                                          : std::string("none"))
                << " | pull " << DungeonLeadKernel::ToString(st.pullState)
                << (DungeonLeadKernel::InteractionActive(st.interactionState)
                        ? std::string(" | waiting at ") + DungeonLeadKernel::ToString(st.interactionType)
                        : std::string())
                << " | outcome " << ToString(st.outcome);
            if (st.outcome == DungeonRunOutcome::Partial)
                out << " (" << ToString(st.failureDomain) << "/" << ToString(st.failureReason) << ")";
            out << " | " << (st.paused ? "PAUSED" : DungeonLeadKernel::ToString(st.state))
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
    if (!DungeonLead::StartSession(botAI, group, DungeonLeadSessionOrigin::Manual, master, /*testMode*/ sub == "test"))
    {
        botAI->TellMaster("startdungeon: a dungeon lead session is already running in this group");
        return false;
    }
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
