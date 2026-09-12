/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadActions.h"

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
#include "RtiTargetValue.h"
#include "Log.h"
#include "Timer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
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
    // server) can be grouped back into one run without guessing from timestamps. Monotonic
    // per-process counter - unique enough to tell runs apart within one worldserver's CSV/debug
    // log, not meant to be globally unique across restarts or servers.
    std::atomic<uint64> g_nextRunId{1};
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
        // dead/gone - our job is done, free the icon for whatever comes next
        if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
        st.ccGuid.Clear();
        return;
    }

    if (c->HasBreakableByDamageCrowdControlAura())
    {
        // actually crowd controlled right now: reset the grace window so a later break gets a
        // fresh chance instead of instantly expiring
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

    // Full Reset() rather than just stripping "-dungeon lead,-grind"/"-mark rti": "startdungeon"
    // itself calls Reset() before adding its own strategies, so anything the leader had beyond
    // dungeon-lead's own additions (e.g. "+cc"/"+mark rti", also added to the leader's own combat
    // state at start) was never tracked and never got removed by the narrow -/- pair alone.
    botAI->Reset();

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
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op));
        }
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
        bool skip = st.visited[st.stepIndex] || !s.IsWalkable() ||
                    (s.kind == DungeonRouteKind::Optional && sPlayerbotAIConfig.dungeonLeadSkipOptional) ||
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
            if (!st.mandatorySkipped)
                st.outcome = DungeonRunOutcome::Complete;
            char const* outcome = st.mandatorySkipped ? "PARTIAL" : "complete";
            std::ostringstream out;
            if (st.skippedSteps.empty())
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
            DungeonLead::RecordEvent(botAI, st.mandatorySkipped ? "route_partial" : "route_complete",
                                      std::to_string(st.skippedSteps.size()) + " skipped");

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
                if (st.outcome == DungeonRunOutcome::Partial)
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
        else if (found.empty() &&
                 GetMSTimeDiffToNow(st.arrivedTs) >= sPlayerbotAIConfig.dungeonLeadStuckSeconds * IN_MILLISECONDS)
        {
            std::ostringstream out;
            out << "Dungeon lead: nothing found at " << step.boss << ", moving on";
            botAI->TellMasterNoFacing(out);
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} step {} '{}' - nothing at destination, giving up", bot->GetName(),
                     step.step, step.boss);
            DungeonLead::RecordEvent(botAI, "not_found", step.boss);
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
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} stuck {}s at dist {:.1f} to step {} '{}' ({:.1f},{:.1f},{:.1f}) -> skip",
                 bot->GetName(), GetMSTimeDiffToNow(st.stuckTs) / 1000, disToDest, step.step, step.boss,
                 dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
        DungeonLead::RecordEvent(botAI, "skip_stuck",
            step.boss + " dist=" + std::to_string(disToDest) + " pos=(" + std::to_string(dest.GetPositionX()) + "," +
            std::to_string(dest.GetPositionY()) + "," + std::to_string(dest.GetPositionZ()) + ")");
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
    // name, ...), so logging after it left every "stop" row with an empty dungeon/lfg_id
    DungeonLead::RecordEvent(botAI, "stop", "auto (left instance)");
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
    if (sub == "pause" || sub == "continue" || sub == "reset" || sub == "debug")
    {
        if (!DungeonLead::IsOn(botAI))
        {
            botAI->TellMaster("startdungeon: not currently leading a dungeon - use plain 'startdungeon' first");
            return false;
        }
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        if (sub != "debug" && st.testMode)
            ++st.manualInterventions;  // pause/continue/reset mid-test: not a clean, hands-off smoke run
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
    if (group->GetLeaderGUID() != bot->GetGUID())
    {
        auto op = std::make_unique<GroupSetLeaderOperation>(bot->GetGUID(), bot->GetGUID());
        PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op));
    }

    // snapshot every follower's current formation/strategies BEFORE touching anything, so
    // "stopdungeon" can put them back exactly - see DungeonLead::Stop()
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

        if (FormationValue* fv = dynamic_cast<FormationValue*>(
                memberAI->GetAiObjectContext()->GetValue<Formation*>("formation")))
            fv->Load("leader");
        memberAI->ChangeStrategy("+follow,-passive,-stay,-grind,-dungeon lead", BOT_STATE_NON_COMBAT);
        memberAI->ChangeStrategy("+cc", BOT_STATE_COMBAT);
    }

    sDungeonRouteMgr.ResetState(bot->GetGUID());
    {
        DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());
        st.debugMode = sPlayerbotAIConfig.dungeonLeadDebugDefault;
        st.memberSnapshots = std::move(snapshots);
        st.runId = NextRunId();
        if (sub == "test")
        {
            // L1.3 first live smoke-test command: same start as plain "startdungeon", just report
            // a structured result once the run reaches a terminal outcome (see the "route
            // complete"/"route PARTIAL" block in Execute()) instead of only the normal chat lines.
            st.testMode = true;
            st.testStartTs = getMSTime();
        }
    }
    botAI->Reset();
    ResetReturnPosition();
    ResetStayPosition();
    botAI->ChangeStrategy("+dungeon lead,+grind,-passive,-stay", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("+dungeon lead,+cc,+mark rti", BOT_STATE_COMBAT);

    // mark self with the star icon: a visible "I'm leading, follow me" signal for the party
    group->SetTargetIcon(RtiTargetValue::starIndex, bot->GetGUID(), bot->GetGUID());

    botAI->TellMaster("Dungeon lead: ON - taking the lead, the others will follow me");
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} START by {} in map={} instance={} (tank={})", bot->GetName(),
             master->GetName(), bot->GetMapId(), bot->GetInstanceId(), PlayerbotAI::IsTank(bot));
    DungeonLead::RecordEvent(botAI, "start", "tank=" + std::string(PlayerbotAI::IsTank(bot) ? "yes" : "no"));
    return true;
}

bool StopDungChatShortcutAction::Execute(Event /*event*/)
{
    if (!GetMaster())
        return false;

    // log BEFORE Stop() - see DungeonLeadStopAction::Execute
    LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} STOP by master", bot->GetName());
    DungeonLead::RecordEvent(botAI, "stop", "");
    DungeonLead::Stop(botAI, true);
    ResetReturnPosition();
    ResetStayPosition();
    botAI->TellMaster("Dungeon lead: OFF");
    return true;
}
