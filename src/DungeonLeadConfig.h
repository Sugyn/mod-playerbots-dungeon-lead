/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadConfig.h
 *
 * Own config singleton, reading AiPlayerbot.DungeonLead.* directly via sConfigMgr - mirrors
 * PlayerbotAIConfig's own field names/defaults exactly (see mod-playerbots src/PlayerbotAIConfig.h)
 * but as a module-owned class, so nothing in mod-playerbots needs editing to add these fields.
 * Same lazy-Meyer's-singleton shape as PlayerbotAIConfig itself.
 */

#ifndef MOD_DUNGEONLEAD_CONFIG_H
#define MOD_DUNGEONLEAD_CONFIG_H

#include "Config.h"
#include <string>

class DungeonLeadConfig
{
public:
    static DungeonLeadConfig& instance()
    {
        static DungeonLeadConfig instance;
        return instance;
    }

    uint32 dungeonLeadHealerManaPct;
    uint32 dungeonLeadStuckSeconds;
    uint32 dungeonLeadCcTimeoutSeconds;
    uint32 dungeonLeadCcAbsoluteTimeoutSeconds;
    uint32 dungeonLeadWipeRecoverySeconds;
    uint32 dungeonLeadMaxWipesPerRun;
    bool dungeonLeadDebugDefault;
    float dungeonLeadLeash;
    float dungeonLeadArriveDistance;
    bool dungeonLeadSkipOptional;
    bool dungeonLeadMarkCc;
    bool dungeonLeadCanaryEnabled;
    uint32 dungeonLeadCanaryMaxConcurrent;
    uint32 dungeonLeadCanaryTimeoutMinutes;
    std::string dungeonLeadCanaryAllowedLfgIds;
    uint32 dungeonLeadMaxPartiesPerRun;

private:
    DungeonLeadConfig()
    {
        dungeonLeadHealerManaPct = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.HealerManaPct", 20);
        dungeonLeadStuckSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.StuckSeconds", 45);
        dungeonLeadCcTimeoutSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.CcTimeoutSeconds", 10);
        dungeonLeadCcAbsoluteTimeoutSeconds =
            sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.CcAbsoluteTimeoutSeconds", 45);
        dungeonLeadWipeRecoverySeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.WipeRecoverySeconds", 300);
        dungeonLeadMaxWipesPerRun = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.MaxWipesPerRun", 3);
        dungeonLeadLeash = sConfigMgr->GetOption<float>("AiPlayerbot.DungeonLead.Leash", 60.0f);
        dungeonLeadArriveDistance = sConfigMgr->GetOption<float>("AiPlayerbot.DungeonLead.ArriveDistance", 8.0f);
        dungeonLeadSkipOptional = sConfigMgr->GetOption<bool>("AiPlayerbot.DungeonLead.SkipOptional", false);
        dungeonLeadMarkCc = sConfigMgr->GetOption<bool>("AiPlayerbot.DungeonLead.MarkCc", true);
        dungeonLeadDebugDefault = sConfigMgr->GetOption<bool>("AiPlayerbot.DungeonLead.DebugDefault", false);
        dungeonLeadCanaryEnabled = sConfigMgr->GetOption<bool>("AiPlayerbot.DungeonLead.CanaryEnabled", false);
        dungeonLeadCanaryMaxConcurrent = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.CanaryMaxConcurrent", 1);
        dungeonLeadCanaryTimeoutMinutes =
            sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.CanaryTimeoutMinutes", 45);
        dungeonLeadCanaryAllowedLfgIds =
            sConfigMgr->GetOption<std::string>("AiPlayerbot.DungeonLead.CanaryAllowedLfgIds", "");
        dungeonLeadMaxPartiesPerRun = sConfigMgr->GetOption<uint32>("AiPlayerbot.DungeonLead.MaxPartiesPerRun", 5);
    }
};

#define sDungeonLeadConfig DungeonLeadConfig::instance()

#endif
