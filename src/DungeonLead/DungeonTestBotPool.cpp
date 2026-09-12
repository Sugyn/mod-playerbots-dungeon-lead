/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonTestBotPool.h"
#include "DungeonLeadActions.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "PlayerbotFactory.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"

#include <algorithm>
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
            "SELECT guid, name FROM characters WHERE class = {} AND online = 0 AND account IN ({}) LIMIT 20",
            classId, idList.str());
        if (!chars)
        {
            outMessage = "No offline AddClass character of the needed class exists";
            return false;
        }

        do
        {
            Field* f = chars->Fetch();
            uint32 const lowGuid = f[0].Get<uint32>();
            std::string const name = f[1].Get<std::string>();
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);

            bool const alreadyLeased = std::any_of(g_leases.begin(), g_leases.end(),
                [&](TestBotLease const& l) { return l.guid == guid; });
            if (alreadyLeased)
                continue;

            sRandomPlayerbotMgr.AddPlayerBot(guid, 0);
            g_leases.push_back({guid, name, role, classId, DungeonLead::TestBotLeaseState::LoggingIn,
                                 getMSTime(), targetLevel});
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead][TestBotPool] acquiring {} as {} (class {}, target level {})",
                     name, RoleName(role), classId, targetLevel);
            outMessage = "Acquiring " + name + " (" + RoleName(role) + ") - logging in, check status shortly";
            return true;
        } while (chars->NextRow());

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
            DungeonLead::Stop(botAI, /*giveLeaderBack*/ false);  // no master to give it back to
        sRandomPlayerbotMgr.LogoutPlayerBot(it->guid);
    }

    std::string const result = "Released " + it->name;
    g_leases.erase(it);
    return result;
}
