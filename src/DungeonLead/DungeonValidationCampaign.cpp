/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonValidationCampaign.h"

#include "DBCStores.h"
#include "DungeonLeadCanary.h"
#include "DungeonLeadConfig.h"
#include "DungeonTestBotPool.h"
#include "Log.h"
#include "SharedDefines.h"
#include "Timer.h"

namespace
{
    enum class Phase
    {
        Idle,
        Acquire,
        Assemble,
        Running,
        Release
    };

    constexpr uint32 kTickMs = 10000;
    constexpr uint32 kAssembleCapMs = 5 * MINUTE * IN_MILLISECONDS;  // pool login + party assembly
    constexpr uint32 kStartCapMs = 5 * MINUTE * IN_MILLISECONDS;     // party formed, session not started
    // Sessions end themselves at CanaryTimeoutMinutes; this only catches one that doesn't.
    constexpr uint32 kRunGraceMs = 5 * MINUTE * IN_MILLISECONDS;

    Phase g_phase = Phase::Idle;
    std::vector<uint32> g_list;
    size_t g_index = 0;
    uint32 g_phaseTs = 0;
    uint32 g_lastTick = 0;
    bool g_started = false;
    std::vector<std::string> g_results;

    void VLog(std::string const& line)
    {
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead][Validation] {}", line);
    }

    uint32 TargetLevel(uint32 lfgId)
    {
        LFGDungeonEntry const* d = sLFGDungeonStore.LookupEntry(lfgId);
        if (!d)
            return 0;
        return d->TargetLevel ? d->TargetLevel : (d->MinLevel + d->MaxLevel) / 2;
    }

    void Enter(Phase p)
    {
        g_phase = p;
        g_phaseTs = getMSTime();
    }
}

std::string DungeonLead::StartValidation(std::vector<uint32> const& lfgIds)
{
    if (g_phase != Phase::Idle)
        return "A validation campaign is already running - " + ValidationStatus();
    if (lfgIds.empty())
        return "No dungeons given";
    for (uint32 id : lfgIds)
        if (!TargetLevel(id))
            return "Unknown LFG dungeon id " + std::to_string(id);
    if (DungeonLead::ActiveCanaryCount() > 0)
        return "A test/canary session is still running - wait for it or release it first";

    g_list = lfgIds;
    g_index = 0;
    g_results.clear();
    g_lastTick = 0;
    Enter(Phase::Acquire);
    std::string ids;
    for (uint32 id : lfgIds)
        ids += (ids.empty() ? "" : " ") + std::to_string(id);
    VLog("campaign start: " + ids);
    return "Validation campaign started: " + std::to_string(lfgIds.size()) + " dungeon(s), " +
           std::to_string(sDungeonLeadConfig.dungeonLeadCanaryTimeoutMinutes) + " min per run at most";
}

std::string DungeonLead::StopValidation()
{
    if (g_phase == Phase::Idle)
        return "No validation campaign running";
    VLog("campaign stopped by command at dungeon " + std::to_string(g_index + 1) + "/" + std::to_string(g_list.size()));
    std::string const released = DungeonLead::ReleaseAllTestBots();
    Enter(Phase::Idle);
    return "Validation campaign stopped; " + released;
}

std::string DungeonLead::ValidationStatus()
{
    if (g_phase == Phase::Idle)
        return g_results.empty() ? "No validation campaign running"
                                 : "Last campaign: " + std::to_string(g_results.size()) + " result(s)";
    static char const* const names[] = {"idle", "acquire", "assemble", "running", "release"};
    return "Validation " + std::to_string(g_index + 1) + "/" + std::to_string(g_list.size()) + " lfg=" +
           std::to_string(g_list[g_index]) + " phase=" + names[int(g_phase)] + " for " +
           std::to_string(GetMSTimeDiffToNow(g_phaseTs) / 1000) + "s";
}

void DungeonLead::ValidationTick()
{
    if (g_phase == Phase::Idle)
        return;
    if (g_lastTick && GetMSTimeDiffToNow(g_lastTick) < kTickMs)
        return;
    g_lastTick = getMSTime();

    uint32 const lfg = g_list[g_index];
    uint32 const inPhase = GetMSTimeDiffToNow(g_phaseTs);
    std::string msg;
    switch (g_phase)
    {
        case Phase::Acquire:
        {
            uint32 const level = TargetLevel(lfg);
            VLog("dungeon " + std::to_string(lfg) + " level " + std::to_string(level));
            DungeonLead::AcquireTestBot(TestBotRole::Tank, level, msg);
            DungeonLead::AcquireTestBot(TestBotRole::Healer, level, msg);
            for (uint8 cls : {uint8(CLASS_WARRIOR), uint8(CLASS_MAGE), uint8(CLASS_ROGUE)})
                DungeonLead::AcquireDpsTestBot(cls, level, msg);
            Enter(Phase::Assemble);
            return;
        }
        case Phase::Assemble:
        {
            std::string const pool = DungeonLead::TestBotPoolStatus();
            bool const pending = pool.find("LoggingIn") != std::string::npos || pool.find("Preparing") != std::string::npos;
            if (pending && inPhase < kAssembleCapMs)
                return;
            VLog("pool " + pool);
            VLog("run " + DungeonLead::RunTestParty(lfg));
            g_started = false;
            Enter(Phase::Running);
            return;
        }
        case Phase::Running:
        {
            bool const active = DungeonLead::ActiveCanaryCount() > 0;
            if (active && DungeonLead::PendingTestPartyCount() == 0)
                g_started = true;  // the party assembled and its session is running
            uint32 const runCap = sDungeonLeadConfig.dungeonLeadCanaryTimeoutMinutes * MINUTE * IN_MILLISECONDS + kRunGraceMs;
            bool const over = (g_started && !active) || inPhase >= runCap || (!g_started && !active && inPhase > 20000) ||
                              (!g_started && inPhase >= kStartCapMs);
            if (!over)
                return;
            std::string const result = "RESULT lfg=" + std::to_string(lfg) + " started=" + std::to_string(g_started) +
                                       " ended=" + std::to_string(g_started && !active) +
                                       " minutes=" + std::to_string(inPhase / 60000);
            VLog(result);
            g_results.push_back(result);
            Enter(Phase::Release);
            return;
        }
        case Phase::Release:
            VLog("release " + DungeonLead::ReleaseAllTestBots());
            if (++g_index >= g_list.size())
            {
                VLog("RESULT campaign finished (" + std::to_string(g_results.size()) + " dungeons)");
                Enter(Phase::Idle);
                return;
            }
            Enter(Phase::Acquire);
            return;
        default:
            return;
    }
}
