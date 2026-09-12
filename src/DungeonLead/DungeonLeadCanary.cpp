/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonLeadCanary.h"
#include "DungeonLeadActions.h"
#include "DungeonRouteMgr.h"

#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"

#include <sstream>
#include <unordered_set>

namespace
{
    std::unordered_set<uint32> ParseAllowedLfgIds(std::string const& csv)
    {
        std::unordered_set<uint32> out;
        std::stringstream ss(csv);
        std::string tok;
        while (std::getline(ss, tok, ','))
        {
            try
            {
                out.insert(static_cast<uint32>(std::stoul(tok)));
            }
            catch (std::exception const&)
            {
                // a malformed entry (blank, non-numeric) just doesn't allowlist anything - never
                // crashes config load, and validate_routes.py-style CI has no reach into a live
                // config file anyway, so this is the only real guard for a fat-fingered entry
            }
        }
        return out;
    }

    // Real player anywhere in the group -> never a canary candidate, at start OR mid-session.
    bool GroupHasRealPlayer(Group* group)
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !GET_PLAYERBOT_AI(member))
                return true;
        }
        return false;
    }
}

void DungeonLead::CanaryTick()
{
    if (!sPlayerbotAIConfig.dungeonLeadCanaryEnabled)
        return;

    static uint32 lastRunTs = 0;
    uint32 now = getMSTime();
    if (lastRunTs && now - lastRunTs < 5000)
        return;
    lastRunTs = now;

    // --- pass 1: safety checks on every currently-active canary session, regardless of whether
    // we're about to try starting a new one this tick. Both checks below matter even at
    // CanaryMaxConcurrent capacity - a stuck/abandoned session must not permanently occupy a slot.
    uint32 activeCanaryCount = 0;
    for (ObjectGuid const& guid : sDungeonRouteMgr.GetActiveSessionGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld())
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;
        DungeonLeadState& st = sDungeonRouteMgr.State(guid);
        if (st.origin != DungeonLeadSessionOrigin::AutoCanary)
            continue;
        ++activeCanaryCount;

        Group* group = bot->GetGroup();
        bool const realPlayerJoined = group && GroupHasRealPlayer(group);
        bool const timedOut = sPlayerbotAIConfig.dungeonLeadCanaryTimeoutMinutes > 0 &&
            GetMSTimeDiffToNow(st.sessionStartTs) >=
                sPlayerbotAIConfig.dungeonLeadCanaryTimeoutMinutes * MINUTE * IN_MILLISECONDS;

        if (!realPlayerJoined && !timedOut)
            continue;

        char const* reason = realPlayerJoined ? "real_player_joined" : "timeout";
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead][Canary] {} stopping - {}", bot->GetName(), reason);
        DungeonLead::RecordEvent(botAI, "canary_stop", std::string("reason=") + reason);
        DungeonLead::Stop(botAI, /*giveLeaderBack*/ true);  // no-op leader handoff for a canary
                                                             // session - GetMaster() is null/self
                                                             // for a bot nobody is playing
        --activeCanaryCount;
    }

    // --- pass 2: try to start one new canary session if there's a free concurrency slot.
    if (activeCanaryCount >= sPlayerbotAIConfig.dungeonLeadCanaryMaxConcurrent)
        return;

    std::unordered_set<uint32> const allowed = ParseAllowedLfgIds(sPlayerbotAIConfig.dungeonLeadCanaryAllowedLfgIds);
    if (allowed.empty())
        return;  // safe default: nothing allowlisted, nothing ever auto-starts

    Group* candidateGroup = nullptr;
    Player* candidateTank = nullptr;
    ObjectGuid bestGuid;  // deterministic tie-break: lowest guid wins across multiple eligible
                          // groups spotted on the same tick, so which one gets picked is
                          // reproducible rather than "whatever GetAllBots() iterates first"

    for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
    {
        if (!bot || !bot->IsInWorld())
            continue;
        if (!DungeonLead::InFiveMan(bot))
            continue;

        Group* group = bot->GetGroup();
        if (!group || group->GetMembersCount() != 5)
            continue;
        if (GroupHasRealPlayer(group))
            continue;  // never touch a group with a real player, ever

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || DungeonLead::GroupInCombat(botAI))
            continue;  // don't take over a group mid-pull

        // already led by someone (this bot or another member)? skip - GuardActiveSessions already
        // owns that session, CanaryTick has nothing to add
        bool alreadyLed = false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && DungeonLead::IsOn(GET_PLAYERBOT_AI(member)))
            {
                alreadyLed = true;
                break;
            }
        }
        if (alreadyLed)
            continue;

        // only groups that queued via the real LFG tool for one of the allowlisted dungeons -
        // never guesses at "closest route by position" the way DungeonLeadNextAction::ResolveRoute
        // does for a manual on-foot start, since a canary session must know FOR SURE which
        // dungeon it's testing, not infer one
        uint32 const lfgId = sLFGMgr->GetDungeon(group->GetGUID(), true);
        if (!lfgId || !allowed.count(lfgId))
            continue;

        if (candidateGroup && !(bot->GetGUID() < bestGuid))
            continue;  // already have an equal-or-better (lower-guid) candidate this tick -
                       // ObjectGuid only defines <, <=, ==, != (no >, >=), see ObjectGuid.h

        Player* tank = nullptr;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && PlayerbotAI::IsTank(member))
            {
                tank = member;
                break;
            }

        candidateGroup = group;
        candidateTank = tank ? tank : bot;  // no tank spec found (odd comp) - lead with whoever we found
        bestGuid = bot->GetGUID();
    }

    if (!candidateGroup || !candidateTank)
        return;

    PlayerbotAI* tankAI = GET_PLAYERBOT_AI(candidateTank);
    if (!tankAI)
        return;

    LOG_INFO("playerbots.dungeonlead", "[DungeonLead][Canary] starting on {} (group of {})",
             candidateTank->GetName(), candidateGroup->GetMembersCount());
    DungeonLead::StartSession(tankAI, candidateGroup, DungeonLeadSessionOrigin::AutoCanary,
                               /*master*/ nullptr, /*testMode*/ true);
}
