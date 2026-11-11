/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#ifndef PLAYERBOTS_DUNGEONVALIDATIONCAMPAIGN_H
#define PLAYERBOTS_DUNGEONVALIDATIONCAMPAIGN_H

#include "Common.h"

#include <string>
#include <vector>

// Live validation campaign (`.dungeonlead validate ...`): runs a list of dungeons, up to
// CanaryMaxConcurrent at a time, each with its own fresh bot-only test party (DungeonTestBotPool:
// warrior tank, priest healer, warrior + mage + rogue dps, one faction) at the dungeon's LFG target
// level. A run ends when its session ends - route end, failure, or CanaryTimeoutMinutes - and that
// party's bots are released before its slot takes the next dungeon. One
// `[DungeonLead][Validation] RESULT` line per dungeon; details per run come from the telemetry CSVs
// (tools/summarize_runs.py). See tools/live_validation/README.md.
namespace DungeonLead
{
    std::string StartValidation(std::vector<uint32> const& lfgIds);
    std::string StopValidation();
    std::string ValidationStatus();

    // World thread, every update (throttled internally).
    void ValidationTick();

    // Telemetry lineage: when `tankName` leads a party of the running campaign, its campaign_id and
    // scenario_id (both left empty otherwise).
    void ValidationLineage(std::string const& tankName, std::string& campaignId, std::string& scenarioId);
}

#endif
