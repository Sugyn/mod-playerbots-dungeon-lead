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

#include <algorithm>
#include <deque>

namespace
{
    // One party at a time per slot; CanaryMaxConcurrent slots run side by side, each in its own
    // dungeon with its own five bots.
    enum class Phase
    {
        Idle,
        Acquire,
        Assemble,
        Running,
        Release
    };

    struct Slot
    {
        Phase phase = Phase::Idle;
        uint32 lfg = 0;
        uint32 phaseTs = 0;
        bool started = false;
        bool retried = false;           // this dungeon already went back to the queue once
        std::vector<std::string> bots;  // tank first
    };

    constexpr uint32 kTickMs = 10000;
    constexpr uint32 kAssembleCapMs = 5 * MINUTE * IN_MILLISECONDS;  // pool login + profile preparation
    constexpr uint32 kStartCapMs = 5 * MINUTE * IN_MILLISECONDS;     // party formed, session not started
    // Sessions end themselves at CanaryTimeoutMinutes; this only catches one that doesn't.
    constexpr uint32 kRunGraceMs = 5 * MINUTE * IN_MILLISECONDS;

    bool g_running = false;
    std::deque<uint32> g_queue;
    std::deque<uint32> g_retry;  // dungeons requeued once, in queue order
    std::vector<Slot> g_slots;
    uint32 g_total = 0;
    uint32 g_lastTick = 0;
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

    void Enter(Slot& slot, Phase p)
    {
        slot.phase = p;
        slot.phaseTs = getMSTime();
    }

    std::string SlotName(size_t i, Slot const& slot)
    {
        return "slot " + std::to_string(i + 1) + " lfg=" + std::to_string(slot.lfg);
    }

    void ReleaseSlot(Slot& slot)
    {
        std::string released;
        for (std::string const& n : slot.bots)
            released += (released.empty() ? "" : ", ") + DungeonLead::ReleaseTestBot(n);
        slot.bots.clear();
        VLog("release " + released);
    }

    void TickSlot(size_t i, Slot& slot)
    {
        uint32 const inPhase = GetMSTimeDiffToNow(slot.phaseTs);
        std::string msg;
        switch (slot.phase)
        {
            case Phase::Idle:
                if (g_queue.empty())
                    return;
                slot.lfg = g_queue.front();
                g_queue.pop_front();
                slot.retried = false;
                if (!g_retry.empty() && g_retry.front() == slot.lfg)
                {
                    slot.retried = true;
                    g_retry.pop_front();
                }
                Enter(slot, Phase::Acquire);
                return;
            case Phase::Acquire:
            {
                uint32 const level = TargetLevel(slot.lfg);
                VLog("dungeon " + std::to_string(slot.lfg) + " level " + std::to_string(level) + " (" + SlotName(i, slot) + ")");
                slot.bots.clear();
                std::string name;
                if (DungeonLead::AcquireTestBot(DungeonLead::TestBotRole::Tank, level, msg, &name))
                    slot.bots.push_back(name);
                if (DungeonLead::AcquireTestBot(DungeonLead::TestBotRole::Healer, level, msg, &name))
                    slot.bots.push_back(name);
                for (uint8 cls : {uint8(CLASS_WARRIOR), uint8(CLASS_MAGE), uint8(CLASS_ROGUE)})
                    if (DungeonLead::AcquireDpsTestBot(cls, level, msg, &name))
                        slot.bots.push_back(name);
                Enter(slot, Phase::Assemble);
                return;
            }
            case Phase::Assemble:
            {
                if (DungeonLead::TestBotsPending(slot.bots) && inPhase < kAssembleCapMs)
                    return;
                VLog("run " + SlotName(i, slot) + ": " + DungeonLead::RunTestParty(slot.lfg, &slot.bots));
                slot.started = false;
                Enter(slot, Phase::Running);
                return;
            }
            case Phase::Running:
            {
                bool assembling = false;
                bool const active = !slot.bots.empty() && DungeonLead::TestPartyActive(slot.bots.front(), assembling);
                if (active && !assembling)
                    slot.started = true;  // the party assembled and its session is running
                uint32 const runCap =
                    sDungeonLeadConfig.dungeonLeadCanaryTimeoutMinutes * MINUTE * IN_MILLISECONDS + kRunGraceMs;
                bool const over = (slot.started && !active) || inPhase >= runCap ||
                                  (!slot.started && !active && inPhase > 20000) ||
                                  (!slot.started && inPhase >= kStartCapMs);
                if (!over)
                    return;
                std::string const result = "RESULT lfg=" + std::to_string(slot.lfg) + " started=" +
                                           std::to_string(slot.started) + " ended=" +
                                           std::to_string(slot.started && !active) +
                                           " minutes=" + std::to_string(inPhase / 60000);
                if (!slot.started && !slot.retried)
                {
                    // the party never got going (a bot failed, a start race): one more try later
                    VLog("requeue " + SlotName(i, slot) + " - the party never started");
                    g_queue.push_back(slot.lfg);
                    g_retry.push_back(slot.lfg);
                    Enter(slot, Phase::Release);
                    return;
                }
                VLog(result);
                g_results.push_back(result);
                Enter(slot, Phase::Release);
                return;
            }
            case Phase::Release:
                ReleaseSlot(slot);
                Enter(slot, Phase::Idle);
                return;
        }
    }
}

std::string DungeonLead::StartValidation(std::vector<uint32> const& lfgIds)
{
    if (g_running)
        return "A validation campaign is already running - " + ValidationStatus();
    if (lfgIds.empty())
        return "No dungeons given";
    for (uint32 id : lfgIds)
        if (!TargetLevel(id))
            return "Unknown LFG dungeon id " + std::to_string(id);
    if (DungeonLead::ActiveCanaryCount() > 0)
        return "A test/canary session is still running - wait for it or release it first";

    g_queue.assign(lfgIds.begin(), lfgIds.end());
    g_retry.clear();
    g_total = uint32(lfgIds.size());
    uint32 const parallel = std::max<uint32>(
        1, std::min({sDungeonLeadConfig.dungeonLeadValidationParallel, sDungeonLeadConfig.dungeonLeadCanaryMaxConcurrent, g_total}));
    g_slots.assign(parallel, Slot());
    g_results.clear();
    g_lastTick = 0;
    g_running = true;
    std::string ids;
    for (uint32 id : lfgIds)
        ids += (ids.empty() ? "" : " ") + std::to_string(id);
    VLog("campaign start: " + ids + " (" + std::to_string(parallel) + " at a time)");
    return "Validation campaign started: " + std::to_string(g_total) + " dungeon(s), " + std::to_string(parallel) +
           " at a time, " + std::to_string(sDungeonLeadConfig.dungeonLeadCanaryTimeoutMinutes) + " min per run at most";
}

std::string DungeonLead::StopValidation()
{
    if (!g_running)
        return "No validation campaign running";
    VLog("campaign stopped by command, " + std::to_string(g_results.size()) + "/" + std::to_string(g_total) + " done");
    for (Slot& slot : g_slots)
        if (!slot.bots.empty())
            ReleaseSlot(slot);
    g_slots.clear();
    g_queue.clear();
    g_running = false;
    return "Validation campaign stopped";
}

std::string DungeonLead::ValidationStatus()
{
    if (!g_running)
        return g_results.empty() ? "No validation campaign running"
                                 : "Last campaign: " + std::to_string(g_results.size()) + " result(s)";
    static char const* const names[] = {"idle", "acquire", "assemble", "running", "release"};
    std::string out = "Validation " + std::to_string(g_results.size()) + "/" + std::to_string(g_total) + " done, " +
                      std::to_string(g_queue.size()) + " queued";
    for (size_t i = 0; i < g_slots.size(); ++i)
        if (g_slots[i].phase != Phase::Idle)
            out += " | " + SlotName(i, g_slots[i]) + " " + names[int(g_slots[i].phase)] + " " +
                   std::to_string(GetMSTimeDiffToNow(g_slots[i].phaseTs) / 1000) + "s";
    return out;
}

void DungeonLead::ValidationTick()
{
    if (!g_running)
        return;
    if (g_lastTick && GetMSTimeDiffToNow(g_lastTick) < kTickMs)
        return;
    g_lastTick = getMSTime();

    for (size_t i = 0; i < g_slots.size(); ++i)
        TickSlot(i, g_slots[i]);

    bool const allIdle = std::all_of(g_slots.begin(), g_slots.end(), [](Slot const& s) { return s.phase == Phase::Idle; });
    if (allIdle && g_queue.empty())
    {
        VLog("RESULT campaign finished (" + std::to_string(g_results.size()) + " dungeons)");
        g_running = false;
    }
}
