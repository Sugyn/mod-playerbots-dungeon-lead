/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonTestBotPool.h"
#include "DungeonSimPlayer.h"
#include "LFGMgr.h"
#include "DungeonLeadActions.h"
#include "DungeonLeadCanary.h"
#include "DungeonLeadConfig.h"

#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotRepository.h"
// Since the playerbots database moved from the core fork into mod-playerbots, DatabaseEnv.h no
// longer brings in PlayerbotsDatabase; older trees still ship it there.
#if __has_include("PlayerbotsDatabase.h")
#include "PlayerbotsDatabase.h"
#endif
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <vector>

namespace
{
    struct TestBotLease
    {
        ObjectGuid guid;
        std::string name;
        DungeonLead::TestBotRole role;
        uint8 classId;         // authoritative for Dps; redundant with role for Tank/Healer
        DungeonLead::TestBotLeaseState state;
        uint32 stateTs;        // getMSTime() of the last state transition - for the login timeout
        uint32 targetLevel;
        uint32 accountId = 0;  // an AddClass account owns ~10 characters (one per
                                // class), and AzerothCore's AccountInstancesPerHour throttle
                                // is counted per ACCOUNT - acquiring several characters that
                                // share an account concentrates instance-entry load onto that
                                // one account's budget. AcquireBot() prefers a candidate whose
                                // account isn't already represented among active leases.

        // A cross-map TeleportTo() is asynchronous and can fail to land, e.g. when AzerothCore's
        // AccountInstancesPerHour throttle (worldserver.conf, default 5) silently refuses entry
        // (MapMgr::PlayerCannotEnter -> Player::CheckInstanceCount) once an account has opened that
        // many *distinct* instances in the last hour - which many test runs against the same small
        // AddClass account pool easily do. Raise that limit for test campaigns. The bounded retry
        // here is defense-in-depth for a genuine transient teleport failure.
        uint32 entryMapId = 0;
        float entryX = 0.f, entryY = 0.f, entryZ = 0.f;
        uint8 teleportRetries = 0;

        // Who this bot has to follow into the dungeon, empty for the leader itself.
        //
        // Teleporting the whole party in one loop can put every member in its OWN instance of the
        // same map. TeleportTo() takes no instance id (only a newInstance bool), so the instance a
        // player lands in is decided by the group's instance binding - and that binding does not
        // exist until the first member has actually entered.
        //
        // So the leader goes in alone, and everyone else waits here until it has genuinely landed
        // (TestBotPoolTick below); by then the binding exists and they follow it into the same
        // instance. Arrival is checked by instance, not only by map id (see the straggler check).
        ObjectGuid entryFollowLeader;

        // Set while this bot still has to LEAVE the dungeon before it can enter the right copy of
        // it. TeleportTo() into the map a player is already standing on does not move them between
        // instances - AzerothCore treats it as movement within the map, which is why LFGMgr passes
        // newInstance=(mapid == player->GetMapId()). So anyone already inside is sent out via
        // TeleportToEntryPoint() first, and only enters once they are genuinely on another map.
        bool entryNeedsExit = false;
        TeamId team = TEAM_NEUTRAL;  // from the race; a test party is one faction (AcquireBot)
    };

    // Session-scoped, in-memory - resets on restart, same as every other Dungeon Lead session
    // state. Console-command handlers and TestBotPoolTick() both run on the world thread (as do
    // CanaryTick()/GuardActiveSessions()), so no locking beyond that shared assumption.
    std::vector<TestBotLease> g_leases;

    char const* RoleName(DungeonLead::TestBotRole role)
    {
        switch (role)
        {
            case DungeonLead::TestBotRole::Tank:   return "Tank";
            case DungeonLead::TestBotRole::Healer: return "Healer";
            case DungeonLead::TestBotRole::Dps:    return "Dps";
        }
        return "?";
    }

    char const* StateName(DungeonLead::TestBotLeaseState state)
    {
        switch (state)
        {
            case DungeonLead::TestBotLeaseState::LoggingIn: return "LoggingIn";
            case DungeonLead::TestBotLeaseState::Preparing: return "Preparing";
            case DungeonLead::TestBotLeaseState::Ready:     return "Ready";
            case DungeonLead::TestBotLeaseState::Leased:    return "Leased";
            case DungeonLead::TestBotLeaseState::Failed:    return "Failed";
        }
        return "?";
    }

    // Class + InitTalentsBySpecNo() specNo for Tank/Healer. Derived from LfgJoinAction::GetRoles()
    // (LfgActions.cpp), which documents the spec-tab-index <-> role mapping; InitTalentsBySpecNo()'s
    // specNo uses the same tab-index convention (both index by real TalentTab.dbc tab order).
    // Not blindly trusted: VerifyReady() re-checks with the same PlayerbotAI::IsTank/IsHeal logic
    // the rest of Dungeon Lead uses, so a wrong mapping surfaces as Failed, never as a
    // silently-wrong "Ready" bot.
    struct RoleClassSpec { uint8 cls; uint32 specNo; };
    RoleClassSpec ClassSpecFor(DungeonLead::TestBotRole role)
    {
        if (role == DungeonLead::TestBotRole::Tank)
            return {CLASS_WARRIOR, 2};  // Protection
        return {CLASS_PRIEST, 1};       // Holy
    }

    // Known low-rank CC spell to check for, per class, when that class is requested as a Dps test
    // bot: verify the actual capability rather than assuming a talent tree implies it. Only
    // Mage/Polymorph is verified so far: it's a baseline class spell learned via InitClassSpells()
    // regardless of spec/talents, unlike e.g. Warlock Banish or Druid Cyclone, which would need a
    // specific spec forced first.
    uint32 CcSpellFor(uint8 classId)
    {
        return classId == CLASS_MAGE ? 118 /*Polymorph (rank 1)*/ : 0;
    }

    // Deterministic profile prep. Reuses PlayerbotFactory::Randomize() for the proven level/
    // skills/spells/quests/equipment pipeline (see PlayerbotFactory.cpp) rather than
    // re-implementing its dozens of sub-steps. For Tank/Healer, overrides just the one piece that
    // needs to be deterministic instead of random (the talent spec) and re-runs InitEquipment()
    // once more so gear matches the FINAL spec. For Dps, leaves Randomize()'s own spec choice as-is
    // (pure-DPS classes don't need one optimized spec the way Tank/Healer do).
    void PrepareProfile(Player* bot, DungeonLead::TestBotRole role, uint32 targetLevel)
    {
        PlayerbotFactory factory(bot, targetLevel);
        factory.Randomize(false);

        if (role != DungeonLead::TestBotRole::Dps)
        {
            RoleClassSpec const rc = ClassSpecFor(role);
            PlayerbotFactory::InitTalentsBySpecNo(bot, rc.specNo, /*reset*/ true);
            factory.InitEquipment(false, false);
        }

        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        {
            PlayerbotRepository::instance().Reset(botAI);
            botAI->ResetStrategies(false);
        }

        if (bot->isDead())
            bot->ResurrectPlayer(1.0f, false);
        bot->SetFullHealth();
        if (bot->getPowerType() == POWER_MANA)
            bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));
    }

    // Ready/Failed determination for whatever role/class was requested. Tank/Healer: IsTank()/
    // IsHeal() called with bySpec=true - NOT the default: the default (bySpec=false) checks the
    // bot's current AI *strategy* (ContainsStrategy(STRATEGY_TYPE_HEAL)), which can lag behind the
    // talent change PrepareProfile() just applied. bySpec=true checks AiFactory::GetPlayerSpecTab()
    // against the real talent tab (WARRIOR_TAB_PROTECTION=2, PRIEST_TAB_HOLY=1, matching the specNo
    // values ClassSpecFor() uses) - the same state InitTalentsBySpecNo() just set. Dps: no role
    // check (any spec is valid DPS), but if the requested class has a known CC spell (see
    // CcSpellFor), that spell must actually be known.
    bool VerifyReady(Player* bot, DungeonLead::TestBotRole role)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return false;

        if (role == DungeonLead::TestBotRole::Tank)
            return PlayerbotAI::IsTank(bot, /*bySpec*/ true);
        if (role == DungeonLead::TestBotRole::Healer)
            return PlayerbotAI::IsHeal(bot, /*bySpec*/ true);

        uint32 const ccSpell = CcSpellFor(bot->getClass());
        return ccSpell == 0 || bot->HasSpell(ccSpell);
    }

    constexpr uint32 LOGIN_TIMEOUT_MS = 30000;

    // See the TestBotLease struct comment: a cross-map TeleportTo() fired from RunTestParty() is
    // fire-and-forget and can silently not land for some party members. Give it a few seconds to
    // resolve on its own before checking (same-tick checks would always fail, since no world tick
    // has actually passed), then retry a bounded number of times rather than leaving a straggler
    // stuck outside the instance for the rest of the run.
    constexpr uint32 TELEPORT_CHECK_GRACE_MS = 8000;
    constexpr uint8 TELEPORT_MAX_RETRIES = 3;

    // Session start waits until the party has actually assembled (DungeonLeadKernel::
    // DecideAssembly): everyone alive, on the dungeon map, in the tank's instance, within this
    // range of the tank. A party that never gets there is given up on, not started half-present.
    constexpr float ASSEMBLY_GATHER_RANGE = 30.0f;
    constexpr uint32 ASSEMBLY_TIMEOUT_MS = 3 * MINUTE * IN_MILLISECONDS;

    struct PendingStart
    {
        ObjectGuid tank;
        std::vector<ObjectGuid> members;  // tank included
        ObjectGuid sim;                   // the simulated player of this party (ValidationSimPlayer), if any
        uint32 mapId = 0;
        uint32 lfgId = 0;
        uint32 since = 0;
    };
    std::vector<PendingStart> g_pendingStarts;

    // ValidationQueue: parties sent through the dungeon finder; the session starts from the queue
    // start (QueueAutoStart) once the finder has formed the group and brought it in.
    struct QueuedParty
    {
        ObjectGuid tank;
        uint32 since = 0;
    };
    std::vector<QueuedParty> g_queuedParties;
    constexpr uint32 kQueuedPartyTimeoutMs = 5 * MINUTE * IN_MILLISECONDS;

    // Shared reservation logic for both public Acquire* entrypoints: find an offline, unleased
    // AddClass character of `classId`, trigger its masterless login, start tracking the lease.
    bool AcquireBot(DungeonLead::TestBotRole role, uint8 classId, uint32 targetLevel, std::string& outMessage,
                    std::string* outName)
    {
        QueryResult accIds = PlayerbotsDatabase.Query("SELECT account_id FROM playerbots_account_type WHERE account_type = 2");
        if (!accIds)
        {
            outMessage = "No AddClass accounts found (playerbots_account_type has no account_type=2 rows)";
            return false;
        }
        std::ostringstream idList;
        bool first = true;
        do
        {
            if (!first)
                idList << ",";
            first = false;
            idList << (*accIds)[0].Get<uint32>();
        } while (accIds->NextRow());

        // Characters leased in the same tick are still offline in the database, so the result has
        // to cover the whole pool: with a small limit, parallel parties run out of candidates
        // although most of the pool is idle.
        QueryResult chars = CharacterDatabase.Query(
            "SELECT guid, name, account, race FROM characters WHERE class = {} AND online = 0 AND account IN ({}) LIMIT 1000",
            classId, idList.str());
        if (!chars)
        {
            outMessage = "No offline AddClass character of the needed class exists";
            return false;
        }

        struct Candidate { ObjectGuid guid; std::string name; uint32 accountId; TeamId team; };
        std::vector<Candidate> candidates;
        do
        {
            Field* f = chars->Fetch();
            candidates.push_back({ObjectGuid::Create<HighGuid::Player>(f[0].Get<uint32>()),
                                   f[1].Get<std::string>(), f[2].Get<uint32>(),
                                   Player::TeamIdForRace(f[3].Get<uint8>())});
        } while (chars->NextRow());

        std::set<uint32> leasedAccounts;
        for (TestBotLease const& l : g_leases)
            leasedAccounts.insert(l.accountId);

        // One faction per party, the first lease's: a mixed party isn't a real one, and the other
        // faction's members attack faction NPCs the run needs (e.g. a prisoner who opens a door).
        TeamId const partyTeam = g_leases.empty() ? TEAM_NEUTRAL : g_leases.front().team;

        // Passes: same faction and a fresh account first (fresh accounts spread
        // AccountInstancesPerHour load - see the struct comment on accountId), then same faction on
        // any account, then anything. Never refuse an acquisition - degrade gracefully, and say so.
        for (int pass = 0; pass < 3; ++pass)
        {
            bool const requireFreshAccount = pass == 0;
            bool const requireTeam = pass < 2 && partyTeam != TEAM_NEUTRAL;
            for (Candidate const& c : candidates)
            {
                if (requireTeam && c.team != partyTeam)
                    continue;
                bool const alreadyLeased = std::any_of(g_leases.begin(), g_leases.end(),
                    [&](TestBotLease const& l) { return l.guid == c.guid; });
                if (alreadyLeased)
                    continue;
                if (requireFreshAccount && leasedAccounts.count(c.accountId))
                    continue;

                sRandomPlayerbotMgr.AddPlayerBot(c.guid, 0);
                g_leases.push_back({c.guid, c.name, role, classId, DungeonLead::TestBotLeaseState::LoggingIn,
                                     getMSTime(), targetLevel, c.accountId});
                g_leases.back().team = c.team;
                LOG_INFO("playerbots.dungeonlead",
                         "[DungeonLead][TestBotPool] acquiring {} as {} (class {}, target level {}, account {}, {}{}{})",
                         c.name, RoleName(role), classId, targetLevel, c.accountId,
                         c.team == TEAM_ALLIANCE ? "alliance" : "horde",
                         requireFreshAccount ? "" : " - REUSED, no fresh account left in this batch",
                         partyTeam != TEAM_NEUTRAL && c.team != partyTeam ? " - OTHER FACTION, none of the party's left" : "");
                outMessage = "Acquiring " + c.name + " (" + RoleName(role) + ") - logging in, check status shortly";
                if (outName)
                    *outName = c.name;
                return true;
            }
        }

        outMessage = "All offline AddClass characters of that class are already leased";
        return false;
    }
}

void DungeonLead::TestBotPoolTick()
{
    static uint32 lastRun = 0;
    uint32 const now = getMSTime();
    if (lastRun && now - lastRun < 3000)
        return;
    lastRun = now;

    for (TestBotLease& lease : g_leases)
    {
        if (lease.state != TestBotLeaseState::LoggingIn)
            continue;

        Player* bot = ObjectAccessor::FindPlayer(lease.guid);
        // In the world is not enough: mod-playerbots attaches the bot's AI a little later (a
        // character logging in right after its previous release can briefly have none).
        if (bot && bot->IsInWorld() && GET_PLAYERBOT_AI(bot))
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] {} logged in, preparing profile ({})",
                     lease.name, RoleName(lease.role));
            PrepareProfile(bot, lease.role, lease.targetLevel);
            bool const ok = VerifyReady(bot, lease.role);
            lease.state = ok ? TestBotLeaseState::Ready : TestBotLeaseState::Failed;
            lease.stateTs = now;
            // why a profile failed, not just that it did (e.g. a level change the factory refuses)
            PlayerbotAI* const preparedAI = GET_PLAYERBOT_AI(bot);
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead][TestBotPool] {} profile prepared, verified={} -> {} (level {} wanted {}, ai={}, "
                     "tank={}, heal={})",
                     lease.name, ok, StateName(lease.state), bot->GetLevel(), lease.targetLevel, preparedAI != nullptr,
                     PlayerbotAI::IsTank(bot, true), PlayerbotAI::IsHeal(bot, true));
        }
        else if (GetMSTimeDiffToNow(lease.stateTs) > LOGIN_TIMEOUT_MS)
        {
            lease.state = TestBotLeaseState::Failed;
            lease.stateTs = now;
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] {} login timed out after {}ms",
                     lease.name, LOGIN_TIMEOUT_MS);
        }
    }

    // Straggler check: a party member RunTestParty() teleported into the dungeon but who never
    // actually landed there (see the TestBotLease struct comment). entryMapId is cleared once a
    // bot is confirmed to have arrived, so this only ever does work for bots still pending.
    for (TestBotLease& lease : g_leases)
    {
        if (lease.state != TestBotLeaseState::Leased || lease.entryMapId == 0)
            continue;
        if (GetMSTimeDiffToNow(lease.stateTs) < TELEPORT_CHECK_GRACE_MS)
            continue;

        Player* bot = ObjectAccessor::FindPlayer(lease.guid);
        if (!bot || !bot->IsInWorld())
            continue;  // logged out mid-run or similar - nothing this tick can do about that

        // Still on the way out of the dungeon - wait until the bot is genuinely on another map,
        // because entering from inside does not move it between instances. See
        // TestBotLease::entryNeedsExit.
        if (lease.entryNeedsExit)
        {
            if (bot->GetMapId() == lease.entryMapId)
                continue;  // hasn't left yet

            lease.entryNeedsExit = false;
            lease.stateTs = now;
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] {} left the dungeon (now map {}) - ready to enter",
                     lease.name, bot->GetMapId());
            if (!lease.entryFollowLeader)  // the leader goes straight back in; followers wait for it
                bot->TeleportTo(lease.entryMapId, lease.entryX, lease.entryY, lease.entryZ, 0.0f);
            continue;
        }

        // Followers hold here until the leader is genuinely inside, so that the group's instance
        // binding exists before they teleport - otherwise each of them lands in a fresh instance
        // of its own. See TestBotLease::entryFollowLeader.
        Player* entryLeader = lease.entryFollowLeader ? ObjectAccessor::FindPlayer(lease.entryFollowLeader) : nullptr;
        if (lease.entryFollowLeader)
        {
            // The leader's own lease still tracking an entry means it hasn't finished arriving
            // (it may be on the way out, or back in but unconfirmed). Following it while it is
            // still standing in its OLD instance would put everyone right back where we started,
            // so wait for its entry to be closed out before moving.
            bool leaderStillEntering = false;
            for (TestBotLease const& other : g_leases)
                if (other.guid == lease.entryFollowLeader && other.entryMapId != 0)
                {
                    leaderStillEntering = true;
                    break;
                }
            if (leaderStillEntering)
                continue;

            if (!entryLeader || entryLeader->GetMapId() != lease.entryMapId)
            {
                // Leader gone (logged out / released) - stop waiting on it and go in alone rather
                // than sit here forever; a lone bot in the wrong instance is at least visible to
                // the checks downstream, an eternally-pending lease is not.
                if (!entryLeader && GetMSTimeDiffToNow(lease.stateTs) > TELEPORT_CHECK_GRACE_MS * 4)
                    lease.entryFollowLeader.Clear();
                continue;
            }

            lease.entryFollowLeader.Clear();
            lease.stateTs = now;
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead][TestBotPool] {} following {} into map {} instance {}",
                     lease.name, entryLeader->GetName(), lease.entryMapId, entryLeader->GetInstanceId());
            bot->TeleportTo(lease.entryMapId, lease.entryX, lease.entryY, lease.entryZ, 0.0f);
            continue;
        }

        // Arrival means the right map AND the leader's own instance - a bot sitting in a separate
        // copy of the dungeon has the right map id but has not arrived.
        if (bot->GetMapId() == lease.entryMapId)
        {
            if (entryLeader && entryLeader->GetInstanceId() != bot->GetInstanceId())
            {
                if (lease.teleportRetries < TELEPORT_MAX_RETRIES)
                {
                    ++lease.teleportRetries;
                    lease.stateTs = now;
                    LOG_INFO("playerbots.dungeonlead",
                             "[DungeonLead][TestBotPool] {} landed in instance {} but {} is in {} - retry {}/{}",
                             lease.name, bot->GetInstanceId(), entryLeader->GetName(),
                             entryLeader->GetInstanceId(), lease.teleportRetries, TELEPORT_MAX_RETRIES);
                    bot->TeleportTo(lease.entryMapId, lease.entryX, lease.entryY, lease.entryZ, 0.0f);
                    continue;
                }
                LOG_ERROR("playerbots.dungeonlead",
                          "[DungeonLead][TestBotPool] {} stuck in instance {} while {} is in {} after {} retries "
                          "- party is split across instances",
                          lease.name, bot->GetInstanceId(), entryLeader->GetName(),
                          entryLeader->GetInstanceId(), TELEPORT_MAX_RETRIES);
            }
            lease.entryMapId = 0;  // arrived - stop tracking
            continue;
        }

        if (lease.teleportRetries >= TELEPORT_MAX_RETRIES)
        {
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead][TestBotPool] {} still on map {} (wanted {}) after {} retries - giving up",
                     lease.name, bot->GetMapId(), lease.entryMapId, TELEPORT_MAX_RETRIES);
            lease.entryMapId = 0;  // stop retrying; the "left instance" auto-stop will catch this
            continue;
        }

        ++lease.teleportRetries;
        lease.stateTs = now;
        LOG_INFO("playerbots.dungeonlead",
                 "[DungeonLead][TestBotPool] {} never arrived on map {} (still on {}) - retry {}/{}",
                 lease.name, lease.entryMapId, bot->GetMapId(), lease.teleportRetries, TELEPORT_MAX_RETRIES);
        bot->TeleportTo(lease.entryMapId, lease.entryX, lease.entryY, lease.entryZ, 0.0f);
    }

    // Start each formed party's session once it has genuinely assembled (or give up on it).
    for (auto it = g_pendingStarts.begin(); it != g_pendingStarts.end();)
    {
        // FindConnectedPlayer: a player in the middle of a cross-map teleport is briefly out of the
        // world (FindPlayer returns nothing) - that is "not there yet", not "gone".
        Player* tank = ObjectAccessor::FindConnectedPlayer(it->tank);
        if (tank && !tank->IsInWorld())
            tank = nullptr;
        std::vector<DungeonLeadKernel::AssemblyMember> facts;
        std::string missing;
        for (ObjectGuid const& guid : it->members)
        {
            Player* m = ObjectAccessor::FindConnectedPlayer(guid);
            DungeonLeadKernel::AssemblyMember f;
            f.online = m && m->IsInWorld();
            f.alive = m && m->IsAlive();
            f.onMap = m && m->GetMapId() == it->mapId;
            f.sameInstance = m && tank && m->GetInstanceId() == tank->GetInstanceId();
            f.distanceToTank = (m && tank && m->GetMap() == tank->GetMap()) ? m->GetDistance(tank) : 1e9f;
            if (!DungeonLeadKernel::MemberAssembled(f, ASSEMBLY_GATHER_RANGE))
                missing += (missing.empty() ? "" : ",") + (m ? m->GetName() : std::to_string(guid.GetCounter()));
            facts.push_back(f);
        }

        // A tank in transit counts as a member not there yet (its own facts say so).
        DungeonLeadKernel::AssemblyStep const step = DungeonLeadKernel::DecideAssembly(
            facts, GetMSTimeDiffToNow(it->since), ASSEMBLY_GATHER_RANGE, ASSEMBLY_TIMEOUT_MS);
        if (step == DungeonLeadKernel::AssemblyStep::Wait)
        {
            ++it;
            continue;
        }
        if (step == DungeonLeadKernel::AssemblyStep::Start && tank)
        {
            PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank);
            Group* group = tank->GetGroup();
            // With a simulated player the session is the one a player starts: origin manual, the
            // player is the master the party waits for.
            Player* sim = it->sim.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(it->sim);
            if (sim && tankAI)
            {
                tankAI->SetMaster(sim);
                if (PlayerbotAI* simAI = GET_PLAYERBOT_AI(sim))
                    simAI->SetMaster(tank);
            }
            bool const ok = tankAI && group &&
                            DungeonLead::StartSession(tankAI, group,
                                                      sim ? DungeonLeadSessionOrigin::Manual : DungeonLeadSessionOrigin::AutoCanary,
                                                      sim, /*testMode*/ true);
            LOG_INFO("playerbots.dungeonlead",
                     "[DungeonLead][TestBotPool] party of {} assembled in map {} instance {} after {} ms - StartSession={}",
                     tank->GetName(), it->mapId, tank->GetInstanceId(), GetMSTimeDiffToNow(it->since), ok);
        }
        else
            LOG_ERROR("playerbots.dungeonlead",
                      "[DungeonLead][TestBotPool] party of {} never assembled within {} s (missing: {}) - not started",
                      tank ? tank->GetName() : std::to_string(it->tank.GetCounter()), ASSEMBLY_TIMEOUT_MS / 1000,
                      missing);
        it = g_pendingStarts.erase(it);
    }
}

uint32 DungeonLead::PendingTestPartyCount()
{
    // a party still in the dungeon finder holds its slot too, until its session runs
    uint32 queued = 0;
    for (QueuedParty const& q : g_queuedParties)
    {
        Player* tank = ObjectAccessor::FindPlayer(q.tank);
        PlayerbotAI* ai = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
        if (!ai || !DungeonLead::HasSession(ai))
            ++queued;
    }
    return uint32(g_pendingStarts.size()) + queued;
}

bool DungeonLead::AcquireTestBot(TestBotRole role, uint32 targetLevel, std::string& outMessage, std::string* outName)
{
    RoleClassSpec const rc = ClassSpecFor(role);
    return AcquireBot(role, rc.cls, targetLevel, outMessage, outName);
}

bool DungeonLead::AcquireDpsTestBot(uint8 classId, uint32 targetLevel, std::string& outMessage, std::string* outName)
{
    return AcquireBot(TestBotRole::Dps, classId, targetLevel, outMessage, outName);
}

bool DungeonLead::TestBotsPending(std::vector<std::string> const& names)
{
    for (TestBotLease const& l : g_leases)
        if ((l.state == TestBotLeaseState::LoggingIn || l.state == TestBotLeaseState::Preparing) &&
            std::find(names.begin(), names.end(), l.name) != names.end())
            return true;
    return false;
}

bool DungeonLead::TestPartyActive(std::string const& tankName, bool& assembling)
{
    assembling = false;
    auto it = std::find_if(g_leases.begin(), g_leases.end(), [&](TestBotLease const& l) { return l.name == tankName; });
    if (it == g_leases.end())
        return false;
    for (PendingStart const& p : g_pendingStarts)
        if (p.tank == it->guid)
        {
            assembling = true;
            return true;
        }
    Player* tank = ObjectAccessor::FindPlayer(it->guid);
    PlayerbotAI* botAI = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
    bool const session = botAI && DungeonLead::HasSession(botAI);
    for (auto q = g_queuedParties.begin(); q != g_queuedParties.end(); ++q)
        if (q->tank == it->guid)
        {
            if (session || GetMSTimeDiffToNow(q->since) >= kQueuedPartyTimeoutMs)
            {
                g_queuedParties.erase(q);
                break;
            }
            assembling = true;
            return true;
        }
    return session;
}

// Forms the parties directly instead of going through LFG: matchmaking can stall indefinitely and
// LFG's own accept/teleport step can be blocked by things as ordinary as a bot in combat, which
// makes it unreliable for unattended tests.
//
// It already knows exactly which bots it wants together (the leases themselves), so it forms the
// Group object directly (Group::Create() + GroupMgr::AddGroup(), the exact sequence
// GroupHandler.cpp uses for a real party invite) and teleports every member straight to the
// dungeon's entrance (DungeonRouteMgr::GetEntrance - the map's entrance teleport target, falling
// back to the first walkable route step); the session is started from TestBotPoolTick() once the
// party has assembled. Same role-scarcity discipline as TriggerTargetedTest(): never forms a
// tank-less or healer-less party.
std::string DungeonLead::RunTestParty(uint32 lfgId, std::vector<std::string> const* only)
{
    DungeonRoute const* route = sDungeonRouteMgr.GetByLfgId(lfgId);
    if (!route)
        return "No route data for lfgId " + std::to_string(lfgId) + " - can't determine entrance coordinates.";

    // The real instance entrance (where players arrive), not the first route step - that is
    // usually the first boss, and arriving on top of it can leave the tank without line of sight.
    DungeonRouteMgr::Entrance entrancePos;
    if (!sDungeonRouteMgr.GetEntrance(*route, entrancePos))
        return "Route for lfgId " + std::to_string(lfgId) + " has no entrance teleport and no walkable step.";
    DungeonRouteMgr::Entrance const* entrance = &entrancePos;

    struct Candidate { size_t leaseIdx; Player* bot; PlayerbotAI* botAI; };
    std::vector<Candidate> tanks, heals, dps;
    for (size_t i = 0; i < g_leases.size(); ++i)
    {
        TestBotLease const& lease = g_leases[i];
        // Ready: never touched yet. Leased: an earlier lease that is idle again. Either way, the
        // group check below is the real source of truth, not our own bookkeeping: a bot already
        // sitting in a group (real player, some other test party) is left alone.
        if (lease.state != TestBotLeaseState::Ready && lease.state != TestBotLeaseState::Leased)
            continue;
        if (only && std::find(only->begin(), only->end(), lease.name) == only->end())
            continue;  // another validation slot's bots

        Player* bot = ObjectAccessor::FindPlayer(lease.guid);
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        // A dead candidate is skipped: a teleport silently drops a dead character rather than
        // erroring, so a candidate that died after being leased would look identical to a live
        // one until the group is short a member mid-dungeon.
        // A validation slot's own bots may still sit in their group from an earlier run (groups are
        // saved): those are ours to take out of it. Anyone else in a group is left alone.
        if (only && bot && bot->GetGroup())
            bot->RemoveFromGroup();
        if (!bot || !botAI || bot->GetGroup() || !bot->IsAlive())
            continue;

        Candidate c{i, bot, botAI};
        if (lease.role == TestBotRole::Tank)
            tanks.push_back(c);
        else if (lease.role == TestBotRole::Healer)
            heals.push_back(c);
        else
            dps.push_back(c);
    }

    // Every party this function forms has 1 tank + 1 healer + 3 dps, never fewer: the per-party
    // loop hands out up to 3 dps per party from one shared cursor, so without the dps/3 floor later
    // parties would form with 2-4 members - not the scenario the caller asked for.
    size_t parties = std::min({tanks.size(), heals.size(), dps.size() / 3});
    if (!parties)
    {
        return "Not enough idle tank+healer+dps(x3) leases to form even one full 5-bot party "
               "(tanks=" + std::to_string(tanks.size()) + ", healers=" + std::to_string(heals.size()) +
               ", dps=" + std::to_string(dps.size()) + "). Acquire more first.";
    }

    // Same shared budget CanaryTick()/TriggerTargetedTest() respect (AiPlayerbot.DungeonLead.
    // CanaryMaxConcurrent) - StartSession() itself doesn't enforce it (by design: it assumes the
    // caller already decided "yes, start here" - see its own doc comment), so every path that can
    // start an AutoCanary session has to check it. Forms as many parties as fit, never more.
    uint32 const activeNow = DungeonLead::ActiveCanaryCount();
    uint32 const cap = sDungeonLeadConfig.dungeonLeadCanaryMaxConcurrent;
    if (activeNow >= cap)
    {
        return "AiPlayerbot.DungeonLead.CanaryMaxConcurrent (" + std::to_string(cap) +
               ") already reached (" + std::to_string(activeNow) + " running) - nothing started";
    }
    parties = std::min<size_t>(parties, cap - activeNow);

    // Hard per-call ceiling on parties formed and teleported in one synchronous burst on this
    // world tick: ramping past MaxPartiesPerRun (default 5) requires separate, spaced-out "run"
    // calls rather than one big same-tick spike.
    uint32 const maxPerRun = sDungeonLeadConfig.dungeonLeadMaxPartiesPerRun;
    bool const pacedDown = maxPerRun && parties > maxPerRun;
    if (pacedDown)
        parties = maxPerRun;

    size_t dpsCursor = 0;
    uint32 started = 0;
    std::ostringstream out;
    for (size_t p = 0; p < parties; ++p)
    {
        if (p)
            out << " | ";

        Candidate& tank = tanks[p];
        Candidate& heal = heals[p];

        std::vector<Candidate*> members{&tank, &heal};
        for (uint32 d = 0; d < 3 && dpsCursor < dps.size(); ++d, ++dpsCursor)
            members.push_back(&dps[dpsCursor]);

        if (sDungeonLeadConfig.dungeonLeadValidationQueue && sDungeonLeadConfig.dungeonLeadQueueAutoStart &&
            sDungeonLeadConfig.dungeonLeadValidationSimPlayer)
        {
            // Through the dungeon finder, as a player's run starts: the party queues for this one
            // dungeon, the finder brings it in, and the queue start hands the run to the bot tank.
            Candidate* sim = sDungeonLeadConfig.dungeonLeadValidationSimPlayer == 2 ? &heal
                             : members.size() > 2 ? members[2] : nullptr;
            if (sim)
            {
                uint32 const base = sDungeonLeadConfig.dungeonLeadValidationSimPlayerSeed
                                        ? sDungeonLeadConfig.dungeonLeadValidationSimPlayerSeed
                                        : uint32(time(nullptr));
                DungeonLead::MarkSimulatedPlayer(sim->bot, base ^ (uint32(sim->bot->GetGUID().GetCounter()) * 2654435761u));
            }
            // The party queues as one group, its simulated player the leader (a player who queues
            // with friends): the finder cannot mix two validation parties or take a bot-only part of
            // one. The members' role check is answered here, with the roles of the party.
            auto roleOf = [&](Candidate const* c) -> uint8
            {
                return c == &tank ? lfg::PLAYER_ROLE_TANK : c == &heal ? lfg::PLAYER_ROLE_HEALER : lfg::PLAYER_ROLE_DAMAGE;
            };
            Player* const leader = sim ? sim->bot : tank.bot;
            Group* group = new Group();
            group->Create(leader);
            sGroupMgr->AddGroup(group);
            for (Candidate* c : members)
                if (c->bot != leader)
                    group->AddMember(c->bot);
            for (Candidate* c : members)
                for (uint8 diff = 0; diff < MAX_DIFFICULTY; ++diff)
                    sInstanceSaveMgr->PlayerUnbindInstance(c->bot->GetGUID(), route->mapId, Difficulty(diff),
                                                           /*deleteFromDB*/ true, c->bot);
            lfg::LfgDungeonSet dungeons;
            dungeons.insert(lfgId);
            Candidate const* leaderCandidate = sim ? sim : &tank;
            sLFGMgr->JoinLfg(leader, uint8(roleOf(leaderCandidate) | lfg::PLAYER_ROLE_LEADER), dungeons, "");
            for (Candidate* c : members)
                if (c->bot != leader)
                    sLFGMgr->UpdateRoleCheck(group->GetGUID(), c->bot->GetGUID(), roleOf(c));
            if (sLFGMgr->GetState(group->GetGUID()) != lfg::LFG_STATE_QUEUED)
                LOG_ERROR("playerbots.dungeonlead",
                          "[DungeonLead][TestBotPool] party of {} not in the dungeon finder queue for lfg {} (state {}) - "
                          "the join or its role check was refused",
                          leader->GetName(), lfgId, uint32(sLFGMgr->GetState(group->GetGUID())));
            for (Candidate* c : members)
            {
                TestBotLease& lease = g_leases[c->leaseIdx];
                lease.state = TestBotLeaseState::Leased;
                lease.stateTs = getMSTime();
                lease.entryMapId = 0;  // the dungeon finder brings it in
            }
            QueuedParty q;
            q.tank = tank.bot->GetGUID();
            q.since = getMSTime();
            g_queuedParties.push_back(q);
            ++started;
            out << "Party " << (p + 1) << " (tank " << tank.bot->GetName() << "): queued for lfg " << lfgId
                << (sim ? " with simulated player " + sim->bot->GetName() : std::string());
            continue;
        }

        Group* group = new Group();
        group->Create(tank.bot);
        sGroupMgr->AddGroup(group);
        for (size_t m = 1; m < members.size(); ++m)
            group->AddMember(members[m]->bot);

        // The tank goes in first and alone. Everyone else is left pending and follows from
        // TestBotPoolTick() once the tank has actually landed - the group's instance binding
        // (which is what makes them land together) does not exist until someone is already
        // inside. See TestBotLease::entryFollowLeader.
        for (Candidate* c : members)
        {
            bool const isTank = c == &tank;

            // Drop any personal save this bot still holds for this dungeon before it goes in.
            // AzerothCore honours the player's OWN bind first, so a bot that ran this dungeon on a
            // previous test would land back in ITS old instance no matter where the rest of the
            // party is. These are throwaway test characters doing a fresh run every time, so there
            // is never anything worth keeping in an old save; this is what ".instance unbind" does
            // by hand.
            for (uint8 diff = 0; diff < MAX_DIFFICULTY; ++diff)
                sInstanceSaveMgr->PlayerUnbindInstance(c->bot->GetGUID(), route->mapId, Difficulty(diff),
                                                       /*deleteFromDB*/ true, c->bot);

            // Anyone left inside this dungeon from an earlier run has to come out before it can go
            // back into the right copy - see TestBotLease::entryNeedsExit.
            bool const needsExit = c->bot->GetMapId() == route->mapId;
            if (needsExit)
                c->bot->TeleportToEntryPoint();
            else if (isTank)
                c->bot->TeleportTo(route->mapId, entrance->x, entrance->y, entrance->z, entrance->o);

            TestBotLease& lease = g_leases[c->leaseIdx];
            lease.entryNeedsExit = needsExit;
            lease.state = TestBotLeaseState::Leased;
            lease.stateTs = getMSTime();
            // See the struct comment: this TeleportTo() is fire-and-forget and can silently not
            // land for some members of the party. Remember where this bot is supposed to end up so
            // TestBotPoolTick() can notice and retry if it doesn't.
            lease.entryMapId = route->mapId;
            lease.entryX = entrance->x;
            lease.entryY = entrance->y;
            lease.entryZ = entrance->z;
            lease.teleportRetries = 0;
            lease.entryFollowLeader = isTank ? ObjectGuid::Empty : tank.bot->GetGUID();
        }

        // The session starts from TestBotPoolTick() once the party has really assembled inside -
        // a requested teleport is not an arrival (see PendingStart).
        PendingStart pending;
        pending.tank = tank.bot->GetGUID();
        // ValidationSimPlayer (test tool): the healer (2) or the first dps (1) plays a player.
        uint32 const simRole = sDungeonLeadConfig.dungeonLeadValidationSimPlayer;
        if (simRole && members.size() > 2)
        {
            Candidate* sim = simRole == 2 ? &heal : members[2];
            uint32 const base = sDungeonLeadConfig.dungeonLeadValidationSimPlayerSeed
                                    ? sDungeonLeadConfig.dungeonLeadValidationSimPlayerSeed
                                    : uint32(time(nullptr));
            uint32 const seed = base ^ (uint32(sim->bot->GetGUID().GetCounter()) * 2654435761u);
            DungeonLead::MarkSimulatedPlayer(sim->bot, seed);
            pending.sim = sim->bot->GetGUID();
            out << " [simulated player " << sim->bot->GetName() << " seed " << seed << "]";
        }
        for (Candidate* c : members)
            pending.members.push_back(c->bot->GetGUID());
        pending.mapId = route->mapId;
        pending.lfgId = lfgId;
        pending.since = getMSTime();
        g_pendingStarts.push_back(pending);
        ++started;

        out << "Party " << (p + 1) << " (tank " << tank.bot->GetName() << ", " << members.size()
            << " members): assembling, the session starts once everyone is inside";

        LOG_INFO("playerbots.dungeonlead",
                 "[DungeonLead][TestBotPool] direct-formed party {} for lfg {} at map {} ({}, {}, {}), tank={} - assembling",
                 p + 1, lfgId, route->mapId, entrance->x, entrance->y, entrance->z, tank.bot->GetName());
    }

    return "Formed " + std::to_string(parties) + " part" + (parties == 1 ? "y" : "ies") +
           ", " + std::to_string(started) + " assembling: " + out.str() +
           (pacedDown ? " (MaxPartiesPerRun=" + std::to_string(maxPerRun) +
                        " reached - more idle leases were available; run again to admit more)"
                      : "");
}

std::string DungeonLead::TestBotPoolStatus()
{
    if (g_leases.empty())
        return "No test bot leases tracked.";
    std::ostringstream out;
    for (size_t i = 0; i < g_leases.size(); ++i)
    {
        if (i)
            out << " | ";
        out << g_leases[i].name << " (" << RoleName(g_leases[i].role) << "): " << StateName(g_leases[i].state);
    }
    return out.str();
}

std::string DungeonLead::ReleaseTestBot(std::string const& botName)
{
    auto it = std::find_if(g_leases.begin(), g_leases.end(),
        [&](TestBotLease const& l) { return l.name == botName; });
    if (it == g_leases.end())
        return "No lease tracked for '" + botName + "'";

    if (Player* bot = ObjectAccessor::FindPlayer(it->guid))
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && DungeonLead::HasSession(botAI))
        {
            // Releasing a leased tank mid-run (the normal way an operator reclaims a test bot)
            // writes its DungeonLeadRuns.csv row like every other exit path, so the run is seen
            // to end.
            DungeonLead::RecordRunSummary(botAI, "test_pool_released");
            DungeonLead::Stop(botAI, /*giveLeaderBack*/ false);  // no master to give it back to
        }
        sRandomPlayerbotMgr.LogoutPlayerBot(it->guid);
    }

    // A simulated player goes back to being a test bot.
    if (Player* bot = ObjectAccessor::FindPlayer(it->guid))
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
            if (DungeonLead::IsSimulatedPlayer(bot))
            {
                botAI->ChangeStrategy("-dungeon lead run back", BOT_STATE_DEAD);
                botAI->ChangeStrategy("-stay", BOT_STATE_NON_COMBAT);
                bot->RemovePlayerFlag(PLAYER_FLAGS_AFK);
            }
    DungeonLead::UnmarkSimulatedPlayer(it->guid.GetRawValue());

    // A party this bot was assembling for can no longer start.
    ObjectGuid const released = it->guid;
    g_pendingStarts.erase(std::remove_if(g_pendingStarts.begin(), g_pendingStarts.end(),
                                         [&](PendingStart const& p)
                                         {
                                             return std::find(p.members.begin(), p.members.end(), released) !=
                                                    p.members.end();
                                         }),
                          g_pendingStarts.end());

    std::string const result = "Released " + it->name;
    g_leases.erase(it);
    return result;
}

std::string DungeonLead::ReleaseAllTestBots()
{
    std::vector<std::string> names;
    for (TestBotLease const& l : g_leases)
        names.push_back(l.name);
    std::string out;
    for (std::string const& n : names)
        out += (out.empty() ? "" : ", ") + ReleaseTestBot(n);
    return out.empty() ? "No leases tracked" : out;
}
