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

#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
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

    // Shared reservation logic for both public Acquire* entrypoints: find an offline, unleased
    // AddClass character of `classId`, trigger its masterless login, start tracking the lease.
    bool AcquireBot(DungeonLead::TestBotRole role, uint8 classId, uint32 targetLevel, std::string& outMessage)
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
            "SELECT guid, name, account FROM characters WHERE class = {} AND online = 0 AND account IN ({}) LIMIT 20",
            classId, idList.str());
        if (!chars)
        {
            outMessage = "No offline AddClass character of the needed class exists";
            return false;
        }

        struct Candidate { ObjectGuid guid; std::string name; uint32 accountId; };
        std::vector<Candidate> candidates;
        do
        {
            Field* f = chars->Fetch();
            candidates.push_back({ObjectGuid::Create<HighGuid::Player>(f[0].Get<uint32>()),
                                   f[1].Get<std::string>(), f[2].Get<uint32>()});
        } while (chars->NextRow());

        std::set<uint32> leasedAccounts;
        for (TestBotLease const& l : g_leases)
            leasedAccounts.insert(l.accountId);

        // Two passes: first only candidates whose account isn't already leased (spreads
        // AccountInstancesPerHour load across accounts - see the struct comment on accountId),
        // then fall back to any offline candidate if every account among the first 20 rows is
        // already in use. Never refuse an acquisition just to keep the spread - degrade gracefully.
        for (bool requireFreshAccount : {true, false})
        {
            for (Candidate const& c : candidates)
            {
                bool const alreadyLeased = std::any_of(g_leases.begin(), g_leases.end(),
                    [&](TestBotLease const& l) { return l.guid == c.guid; });
                if (alreadyLeased)
                    continue;
                if (requireFreshAccount && leasedAccounts.count(c.accountId))
                    continue;

                sRandomPlayerbotMgr.AddPlayerBot(c.guid, 0);
                g_leases.push_back({c.guid, c.name, role, classId, DungeonLead::TestBotLeaseState::LoggingIn,
                                     getMSTime(), targetLevel, c.accountId});
                LOG_INFO("playerbots.dungeonlead",
                         "[DungeonLead][TestBotPool] acquiring {} as {} (class {}, target level {}, account {}{})",
                         c.name, RoleName(role), classId, targetLevel, c.accountId,
                         requireFreshAccount ? "" : " - REUSED, no fresh account left in this batch");
                outMessage = "Acquiring " + c.name + " (" + RoleName(role) + ") - logging in, check status shortly";
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
        if (bot && bot->IsInWorld())
        {
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] {} logged in, preparing profile ({})",
                     lease.name, RoleName(lease.role));
            PrepareProfile(bot, lease.role, lease.targetLevel);
            bool const ok = VerifyReady(bot, lease.role);
            lease.state = ok ? TestBotLeaseState::Ready : TestBotLeaseState::Failed;
            lease.stateTs = now;
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] {} profile prepared, verified={} -> {}",
                     lease.name, ok, StateName(lease.state));
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

        if (bot->GetMapId() == lease.entryMapId)
        {
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
}

bool DungeonLead::AcquireTestBot(TestBotRole role, uint32 targetLevel, std::string& outMessage)
{
    RoleClassSpec const rc = ClassSpecFor(role);
    return AcquireBot(role, rc.cls, targetLevel, outMessage);
}

bool DungeonLead::AcquireDpsTestBot(uint8 classId, uint32 targetLevel, std::string& outMessage)
{
    return AcquireBot(TestBotRole::Dps, classId, targetLevel, outMessage);
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
// straight to the dungeon's own entrance coordinates (the first walkable step of the hand-authored
// route - see DungeonRouteMgr), then calls DungeonLead::StartSession() itself instead of waiting for
// CanaryTick() to spot an LFG-formed group. Same role-scarcity discipline as TriggerTargetedTest():
// never forms a tank-less or healer-less party.
std::string DungeonLead::RunTestParty(uint32 lfgId)
{
    DungeonRoute const* route = sDungeonRouteMgr.GetByLfgId(lfgId);
    if (!route)
        return "No route data for lfgId " + std::to_string(lfgId) + " - can't determine entrance coordinates.";

    DungeonRouteStep const* entrance = nullptr;
    for (DungeonRouteStep const& step : route->steps)
    {
        if (step.IsWalkable())
        {
            entrance = &step;
            break;
        }
    }
    if (!entrance)
        return "Route for lfgId " + std::to_string(lfgId) + " has no walkable step to use as an entrance.";

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
    uint32 const cap = sPlayerbotAIConfig.dungeonLeadCanaryMaxConcurrent;
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
    uint32 const maxPerRun = sPlayerbotAIConfig.dungeonLeadMaxPartiesPerRun;
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

        for (Candidate* c : members)
        {
            c->bot->TeleportTo(route->mapId, entrance->x, entrance->y, entrance->z, 0.0f);
            TestBotLease& lease = g_leases[c->leaseIdx];
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
        }

        bool const ok = DungeonLead::StartSession(tank.botAI, group, DungeonLeadSessionOrigin::AutoCanary,
                                                    /*master*/ nullptr, /*testMode*/ true);
        if (ok)
            ++started;

        out << "Party " << (p + 1) << " (tank " << tank.bot->GetName() << ", " << members.size()
            << " members): " << (ok ? "started" : "StartSession refused");

        LOG_INFO("playerbots.dungeonlead",
                 "[DungeonLead][TestBotPool] direct-formed party {} for lfg {} at map {} ({}, {}, {}), tank={}, StartSession={}",
                 p + 1, lfgId, route->mapId, entrance->x, entrance->y, entrance->z, tank.bot->GetName(), ok);
    }

    return "Formed " + std::to_string(parties) + " part" + (parties == 1 ? "y" : "ies") +
           ", started " + std::to_string(started) + ": " + out.str() +
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
        if (botAI && DungeonLead::IsOn(botAI))
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

    std::string const result = "Released " + it->name;
    g_leases.erase(it);
    return result;
}
