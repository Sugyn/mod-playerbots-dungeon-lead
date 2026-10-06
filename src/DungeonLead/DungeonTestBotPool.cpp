/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonTestBotPool.h"
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
        uint32 accountId = 0;  // 2026-09-15 (independent architecture review DL-005/DL-019): an
                                // AddClass account owns ~10 characters (one per class), and
                                // AzerothCore's AccountInstancesPerHour throttle is counted per
                                // ACCOUNT, not per character - acquiring several characters that
                                // happen to share an account concentrates instance-entry load onto
                                // that one account's budget instead of spreading it, which is
                                // exactly what silently caused today's "left instance" mystery
                                // (see HISTORY.md 2026-09-15). AcquireBot() now prefers a candidate
                                // whose account isn't already represented among active leases.

        // 2026-09-15: some party members teleported by RunTestParty() were left sitting on map 1
        // ("some bots stay outside, I had to /summon them" - reported very early in this project),
        // each spamming "STOP - auto (left instance)" forever with nothing retrying their teleport.
        // First suspected a cross-map TeleportTo() race (it IS async - confirmed via the
        // .playerbots tpbot tool the same day); built this retry mechanism for that. Live-tested
        // under load it changed nothing: the exact same bots failed all 3 retries identically. Root
        // cause turned out to be upstream of Dungeon Lead entirely - AzerothCore's standard
        // AccountInstancesPerHour throttle (worldserver.conf, default 5): MapMgr::PlayerCannotEnter
        // -> Player::CheckInstanceCount silently refuses entry once an account has opened that many
        // *distinct* dungeon instances in the last hour, and RunTestParty forms a fresh instance
        // every call - exactly what dozens of test runs against the same small AddClass account
        // pool does in a single evening. Raised to 500 in worldserver.conf (reloadable, no restart
        // needed - the check reads sWorld->getIntConfig() live) once this was found; confirmed live
        // (.playerbots tpbot) that all 3 previously-stuck bots teleport in immediately afterward.
        // Kept the retry loop anyway as cheap defense-in-depth for a genuine transient teleport
        // failure, even though it wasn't what was actually happening here.
        uint32 entryMapId = 0;
        float entryX = 0.f, entryY = 0.f, entryZ = 0.f;
        uint8 teleportRetries = 0;

        // 2026-09-17: who this bot has to follow into the dungeon, empty for the leader itself.
        //
        // Teleporting the whole party in one loop put every member in its OWN instance of the same
        // map - measured live: healer on map=43 instance=1 while the leader sat on map=43
        // instance=6, five separate copies of Wailing Caverns that can never meet. TeleportTo()
        // takes no instance id (only a newInstance bool), so the instance a player lands in is
        // decided by the group's instance binding - and that binding does not exist until the
        // first member has actually entered. Teleporting everyone simultaneously means nobody has
        // it yet, so each one creates a fresh instance.
        //
        // So the leader goes in alone, and everyone else waits here until it has genuinely landed
        // (TestBotPoolTick below); by then the binding exists and they follow it into the same
        // instance. This also has to be checked on arrival, not just on departure - see the
        // instance comparison in the straggler check, which used to compare map ids only and
        // therefore called a bot in the wrong instance "arrived".
        ObjectGuid entryFollowLeader;

        // 2026-09-17: set while this bot still has to LEAVE the dungeon before it can enter the
        // right copy of it. TeleportTo() into the map a player is already standing on does not
        // move them between instances - AzerothCore treats it as movement within the map, which is
        // why LFGMgr passes newInstance=(mapid == player->GetMapId()). Measured after the unbind
        // fix was in place: the log said "Thyleae following Farancano into map 43 instance 6" and
        // Thyleae stayed in instance 1, because it had never left map 43 since the previous run.
        // So anyone already inside is sent out via TeleportToEntryPoint() first, and only enters
        // once they are genuinely on another map.
        bool entryNeedsExit = false;
        TeamId team = TEAM_NEUTRAL;  // from the race; a test party is one faction (AcquireBot)
    };

    // Session-scoped, in-memory - resets on restart, same as every other Dungeon Lead session
    // state. Console-command handlers and TestBotPoolTick() both run on the world thread (same
    // pattern already relied on by CanaryTick()/GuardActiveSessions() and the canarytest/lfgstate
    // console commands this build), so no locking beyond that shared assumption.
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
    // (LfgActions.cpp) - the only place in this codebase that documents the spec-tab-index <->
    // role mapping, cross-checked against the assumption that InitTalentsBySpecNo()'s specNo uses
    // the same tab-index convention (both ultimately index by real TalentTab.dbc tab order).
    // NOT blindly trusted: VerifyReady() re-checks with the same PlayerbotAI::IsTank/IsHeal logic
    // the rest of Dungeon Lead already uses - both guesses verified correct on the first live test
    // (2026-09-12), but a future wrong guess for a new role surfaces as Failed, never as a
    // silently-wrong "Ready" bot.
    struct RoleClassSpec { uint8 cls; uint32 specNo; };
    RoleClassSpec ClassSpecFor(DungeonLead::TestBotRole role)
    {
        if (role == DungeonLead::TestBotRole::Tank)
            return {CLASS_WARRIOR, 2};  // Protection
        return {CLASS_PRIEST, 1};       // Holy
    }

    // Known low-rank CC spell to check for, per class, when that class is requested as a Dps test
    // bot - "For CC-focused scenarios, verify the actual required capability rather than assuming
    // a talent-tree number implies it" (ADR-003). Only Mage/Polymorph is verified so far: it's a
    // baseline class spell learned via InitClassSpells() regardless of spec/talents, unlike e.g.
    // Warlock Banish (talent-gated) or Druid Cyclone (talent-gated) which would need a specific
    // spec forced first - deliberately deferred rather than guessed at.
    uint32 CcSpellFor(uint8 classId)
    {
        return classId == CLASS_MAGE ? 118 /*Polymorph (rank 1)*/ : 0;
    }

    // Deterministic profile prep. Reuses PlayerbotFactory::Randomize() for the proven level/
    // skills/spells/quests/equipment pipeline (see PlayerbotFactory.cpp) rather than
    // re-implementing its dozens of sub-steps. For Tank/Healer, overrides just the one piece that
    // needs to be deterministic instead of random (the talent spec) and re-runs InitEquipment()
    // once more so gear matches the FINAL spec. For Dps, leaves Randomize()'s own spec choice as-is
    // (per ADR-003: pure-DPS classes don't need one optimized spec the way Tank/Healer do).
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
    // IsHeal() called with bySpec=true - NOT the default. Caught live (2026-09-12): the default
    // (bySpec=false) checks the bot's current AI *strategy* (ContainsStrategy(STRATEGY_TYPE_HEAL)),
    // not the talent spec directly, and that strategy assignment lagged behind the talent change
    // just applied by PrepareProfile() on one live run (same test Priest character, same procedure,
    // verified Ready once and Failed the next time). bySpec=true checks AiFactory::GetPlayerSpecTab()
    // against the real talent tab instead (WARRIOR_TAB_PROTECTION=2, PRIEST_TAB_HOLY=1 - both
    // confirmed against PlayerbotAI.h, matching the specNo values ClassSpecFor() already used) -
    // grounded in the same state InitTalentsBySpecNo() just set, no strategy-engine timing
    // dependency. Dps: no role check (any spec is valid DPS), but if the requested class has a
    // known CC spell (see CcSpellFor), that spell must actually be known - a silent "Ready" bot
    // that can't actually CC would defeat the point of acquiring it for a CC scenario.
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
        uint32 mapId = 0;
        uint32 lfgId = 0;
        uint32 since = 0;
    };
    std::vector<PendingStart> g_pendingStarts;

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

        QueryResult chars = CharacterDatabase.Query(
            "SELECT guid, name, account, race FROM characters WHERE class = {} AND online = 0 AND account IN ({}) LIMIT 40",
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
        // faction's members attack faction NPCs the run needs (Shadowfang Keep: the Horde bots
        // killed Sorcerer Ashcrombe, the Alliance prisoner who opens the courtyard door).
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
        // In the world is not enough: mod-playerbots attaches the bot's AI a little later (seen
        // when a character logs in right after its previous release: profiles failed with ai=false).
        if (bot && bot->IsInWorld() && GET_PLAYERBOT_AI(bot))
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] {} logged in, preparing profile ({})",
                     lease.name, RoleName(lease.role));
            PrepareProfile(bot, lease.role, lease.targetLevel);
            bool const ok = VerifyReady(bot, lease.role);
            lease.state = ok ? TestBotLeaseState::Ready : TestBotLeaseState::Failed;
            lease.stateTs = now;
            // why a profile failed, not just that it did (seen: a whole party failing when asked to
            // go down from level 22 to 16)
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

        // Arrival means the right map AND the leader's own instance. Comparing map ids alone
        // reported a bot sitting in a separate copy of the dungeon as "arrived", which is how five
        // bots ended up in five instances with nothing noticing.
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
            bool const ok = tankAI && group &&
                            DungeonLead::StartSession(tankAI, group, DungeonLeadSessionOrigin::AutoCanary,
                                                      /*master*/ nullptr, /*testMode*/ true);
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
    return uint32(g_pendingStarts.size());
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
    return botAI && DungeonLead::HasSession(botAI);
}

// Phase 3, take 2. The first implementation queued every Ready lease through the real LFG tool
// (DungeonLead::QueueBotForLfg(), the same primitive AutoBot Canary's TriggerTargetedTest() uses)
// and relied on CanaryTick() to notice the resulting pure-bot group once LFG matched and teleported
// it in. Built, deployed, tested live (2026-09-12): 15 leases queued, confirmed QUEUED via
// ".playerbots lfgstate", and matchmaking never progressed to PROPOSAL after 5+ minutes despite the
// queue holding exactly 3 role-complete parties. Independently, a real player-initiated LFG run on
// this same server left several bots stranded outside the instance requiring a GM ".summon" - LFG's
// own accept/teleport step can apparently be blocked by things as ordinary as a bot mid-combat.
// Conclusion: LFG matchmaking is not a reliable on-ramp for this server, queue-side or teleport-side.
//
// This version bypasses LFG entirely. It already knows exactly which bots it wants together (the
// leases themselves), so it forms the Group object directly (Group::Create() + GroupMgr::AddGroup(),
// the exact sequence GroupHandler.cpp uses for a real party invite) and teleports every member
// straight to the dungeon's entrance (DungeonRouteMgr::GetEntrance - the map's entrance teleport
// target, falling back to the first walkable route step), then calls DungeonLead::StartSession() itself instead of waiting for
// CanaryTick() to spot an LFG-formed group. Same role-scarcity discipline as TriggerTargetedTest():
// never forms a tank-less or healer-less party.
std::string DungeonLead::RunTestParty(uint32 lfgId, std::vector<std::string> const* only)
{
    DungeonRoute const* route = sDungeonRouteMgr.GetByLfgId(lfgId);
    if (!route)
        return "No route data for lfgId " + std::to_string(lfgId) + " - can't determine entrance coordinates.";

    // The real instance entrance (where players arrive), not the first route step - that is
    // usually the first boss, and arriving on top of it left the tank without line of sight to
    // it (Lady Anacondra, found in the first in-dungeon pull-controller run).
    DungeonRouteMgr::Entrance entrancePos;
    if (!sDungeonRouteMgr.GetEntrance(*route, entrancePos))
        return "Route for lfgId " + std::to_string(lfgId) + " has no entrance teleport and no walkable step.";
    DungeonRouteMgr::Entrance const* entrance = &entrancePos;

    struct Candidate { size_t leaseIdx; Player* bot; PlayerbotAI* botAI; };
    std::vector<Candidate> tanks, heals, dps;
    for (size_t i = 0; i < g_leases.size(); ++i)
    {
        TestBotLease const& lease = g_leases[i];
        // Ready: never touched yet. Leased: survivor of the abandoned LFG-queue attempt above -
        // still a perfectly good idle character, just mis-flagged by that dead-end code path. Either
        // way, the group check below is the real source of truth, not our own bookkeeping: a bot
        // already sitting in a group (real player, some other test party) is left alone.
        if (lease.state != TestBotLeaseState::Ready && lease.state != TestBotLeaseState::Leased)
            continue;
        if (only && std::find(only->begin(), only->end(), lease.name) == only->end())
            continue;  // another validation slot's bots

        Player* bot = ObjectAccessor::FindPlayer(lease.guid);
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        // 2026-09-15 (found live, watching a run with the user: a dead healer never made it into
        // the instance - "reached"/"pathing" events kept coming from the other 4 members while she
        // sat corpsed back in the open world, and nothing here had noticed. LFG's own teleport
        // silently drops a dead character rather than erroring, so a candidate that dies between
        // being leased and being drawn into a party here would otherwise look identical to a live
        // one right up until the group is short a healer mid-dungeon. Independent architecture
        // review DL-004's "verified role/... capabilities" scope, found by direct observation
        // rather than by reading the review text.
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

    // 2026-09-15 (independent architecture review, DL-004 - "direct targeted tests do not prove
    // the requested scenario"): this used to be min(tanks, heals) with no dps floor - the per-party
    // loop below hands out up to 3 dps per party from one shared cursor, so once dps ran out mid-
    // loop, later "parties" silently formed with 2-4 members instead of 5 and were started anyway.
    // A short/incomplete party is not the scenario a caller asked for; require dps/3 too so every
    // party this function forms actually has 1 tank + 1 healer + 3 dps, never fewer.
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

    // 2026-09-16 (independent architecture review DL-019 - "the pool cannot safely sustain a
    // realistic 10x5 campaign", minimal-fix slice: "pace admission 1->3->10"): everything below
    // this point forms and teleports every remaining party in one synchronous burst, all on this
    // same world tick - a real 10-party request used to do exactly that in a single call. Not the
    // review's full gradual-admission scheduler (that needs a tick-based queue), but a hard per-
    // call ceiling: ramping past MaxPartiesPerRun (default 5) requires separate, deliberately
    // spaced-out "run" calls rather than one big same-tick spike.
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

        Group* group = new Group();
        group->Create(tank.bot);
        sGroupMgr->AddGroup(group);
        for (size_t m = 1; m < members.size(); ++m)
            group->AddMember(members[m]->bot);

        // The tank goes in first and alone. Everyone else is left pending and follows from
        // TestBotPoolTick() once the tank has actually landed - teleporting the whole party at
        // once gave every member its own instance of the dungeon, because the group's instance
        // binding (which is what makes them land together) does not exist until someone is
        // already inside. See TestBotLease::entryFollowLeader for the measurements.
        for (Candidate* c : members)
        {
            bool const isTank = c == &tank;

            // Drop any personal save this bot still holds for this dungeon before it goes in.
            // Without this the group's instance binding is irrelevant: AzerothCore honours the
            // player's OWN bind first, so a bot that ran this dungeon on a previous test lands
            // back in ITS old instance no matter where the rest of the party is. Measured live
            // after the follow-the-leader fix below was already working - the log said
            // "Thyleae following Farancano into map 43 instance 6" and Thyleae still arrived in
            // instance 1, which is its bind from an earlier run. These are throwaway test
            // characters doing a fresh run every time, so there is never anything worth keeping
            // in an old save; this is the same thing ".instance unbind" does by hand.
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
            // 2026-09-15 (independent architecture review DL-006, extended past its original two
            // wired paths after live use exposed the gap directly): releasing a leased tank mid-run
            // - the normal way an operator reclaims a test bot - called Stop() same as every other
            // exit path, but unlike route completion and canary timeout, this one never wrote a
            // DungeonLeadRuns.csv row. A run ended by release looked, from the telemetry alone,
            // like it never ended at all.
            DungeonLead::RecordRunSummary(botAI, "test_pool_released");
            DungeonLead::Stop(botAI, /*giveLeaderBack*/ false);  // no master to give it back to
        }
        sRandomPlayerbotMgr.LogoutPlayerBot(it->guid);
    }

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
