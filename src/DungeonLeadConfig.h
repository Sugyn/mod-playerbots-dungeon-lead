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
#include "Log.h"
#include <algorithm>
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
    // Leadership transfer at session start / stop - how long one request may stay unobserved
    // before it is retried, and how many requests are made before giving up.
    uint32 dungeonLeadLeadershipAcquireTimeoutSeconds;
    uint32 dungeonLeadLeadershipReturnTimeoutSeconds;
    uint32 dungeonLeadLeadershipMaxAttempts;
    // Pull controller: open a fight once the tank is this close to the pull target, and give a
    // pull this long to start / to settle before counting it as failed.
    float dungeonLeadPullRange;
    uint32 dungeonLeadPullInitiateTimeoutSeconds;
    uint32 dungeonLeadPullEstablishTimeoutSeconds;
    uint32 dungeonLeadPullMaxAttempts;
    // Combat leash: the tank does not chase a target farther than this from where the fight began.
    float dungeonLeadCombatLeashRadius;
    // Party cohesion: a member beyond the soft range holds the next pull, beyond the hard range
    // the leader stops walking too.
    float dungeonLeadPartySoftRange;
    float dungeonLeadPartyHardRange;
    // Post-combat gate: minimum pause between two fights, and the party health the leader waits
    // for before walking or pulling on (0 = don't wait for health).
    uint32 dungeonLeadPostCombatMinSeconds;
    uint32 dungeonLeadPostCombatMinHealthPct;

private:
    static uint32 Bounded(char const* key, uint32 def, uint32 lo, uint32 hi)
    {
        uint32 const v = sConfigMgr->GetOption<uint32>(key, def);
        uint32 const clamped = std::clamp(v, lo, hi);
        if (clamped != v)
            LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] {} = {} out of range [{}, {}], using {}", key, v, lo, hi,
                      clamped);
        return clamped;
    }

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
        dungeonLeadLeadershipAcquireTimeoutSeconds =
            Bounded("AiPlayerbot.DungeonLead.LeadershipAcquireTimeoutSeconds", 5, 1, 60);
        dungeonLeadLeadershipReturnTimeoutSeconds =
            Bounded("AiPlayerbot.DungeonLead.LeadershipReturnTimeoutSeconds", 5, 1, 60);
        dungeonLeadLeadershipMaxAttempts = Bounded("AiPlayerbot.DungeonLead.LeadershipMaxAttempts", 3, 1, 10);
        dungeonLeadPullRange = float(Bounded("AiPlayerbot.DungeonLead.PullRange", 30, 5, 60));
        dungeonLeadPullInitiateTimeoutSeconds = Bounded("AiPlayerbot.DungeonLead.PullInitiateTimeoutSeconds", 10, 1, 30);
        dungeonLeadPullEstablishTimeoutSeconds = Bounded("AiPlayerbot.DungeonLead.PullEstablishTimeoutSeconds", 8, 1, 60);
        dungeonLeadPullMaxAttempts = Bounded("AiPlayerbot.DungeonLead.PullMaxAttempts", 2, 1, 5);
        dungeonLeadCombatLeashRadius = float(Bounded("AiPlayerbot.DungeonLead.CombatLeashRadius", 30, 10, 80));
        dungeonLeadPartySoftRange = float(Bounded("AiPlayerbot.DungeonLead.PartySoftRange", 40, 10, 150));
        dungeonLeadPartyHardRange = float(Bounded("AiPlayerbot.DungeonLead.PartyHardRange", 90, 20, 300));
        dungeonLeadPostCombatMinSeconds = Bounded("AiPlayerbot.DungeonLead.PostCombatMinSeconds", 3, 0, 30);
        dungeonLeadPostCombatMinHealthPct = Bounded("AiPlayerbot.DungeonLead.PostCombatMinHealthPct", 50, 0, 100);
        if (dungeonLeadPartyHardRange <= dungeonLeadPartySoftRange)
        {
            LOG_ERROR("playerbots.dungeonlead", "[DungeonLead] PartyHardRange ({}) must be above PartySoftRange ({}), "
                      "using soft + 10", dungeonLeadPartyHardRange, dungeonLeadPartySoftRange);
            dungeonLeadPartyHardRange = dungeonLeadPartySoftRange + 10.0f;
        }
    }
};

#define sDungeonLeadConfig DungeonLeadConfig::instance()

#endif
