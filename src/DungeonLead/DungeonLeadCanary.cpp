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

#include "DBCStores.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"
#include "WorldPacket.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

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
        DungeonLead::RecordRunSummary(botAI, std::string("canary_") + reason);  // before Stop() erases state
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

namespace
{
    // Same three-way split LfgJoinAction::GetRoles() uses for a non-random-bot bot, minus the
    // large per-class random-bot spec switch that function also has - good enough to bias
    // selection toward a plausible group, not a guarantee (LFG's own matchmaking is the real
    // arbiter of whether a composition can actually be teleported in).
    uint32 RoleMaskFor(PlayerbotAI* botAI, Player* bot)
    {
        if (botAI->IsTank(bot))
            return lfg::PLAYER_ROLE_TANK;
        if (botAI->IsHeal(bot))
            return lfg::PLAYER_ROLE_HEALER;
        return lfg::PLAYER_ROLE_DAMAGE;
    }
}

// Mirrors LfgJoinAction::Execute's own CMSG_LFG_JOIN construction (LfgActions.cpp) exactly, field
// for field, except the dungeon list is forced to `lfgId` alone instead of the bot's own computed
// acceptable-dungeon set - that one substitution is this whole function's purpose. Public (see
// DungeonLeadCanary.h) so DungeonTestBotPool can queue its own specific leased bots the same way.
void DungeonLead::QueueBotForLfg(PlayerbotAI* botAI, Player* bot, uint32 lfgId, uint32 roleMask)
{
    std::string const gearScore = std::to_string(botAI->GetEquipGearScore(bot));

    WorldPacket* data = new WorldPacket(CMSG_LFG_JOIN);
    *data << (uint32)roleMask;
    *data << (bool)false;
    *data << (bool)false;
    // Slots
    *data << (uint8)1;  // exactly one dungeon - the whole point of this function
    *data << (uint32)lfgId;
    // Needs
    *data << (uint8)3 << (uint8)0 << (uint8)0 << (uint8)0;
    *data << gearScore;
    bot->GetSession()->QueuePacket(data);
}

// Shared with CanaryTick()'s own pass-1 count: how many AutoCanary-origin sessions are active
// right now, regardless of who/what started them. Recomputed rather than cached anywhere - cheap
// enough to call once per TriggerTargetedTest()/RunTestParty() invocation, never per-tick. Exposed
// (not file-local) so DungeonTestBotPool's RunTestParty() can respect the same
// CanaryMaxConcurrent budget instead of keeping a second, divergent count.
uint32 DungeonLead::ActiveCanaryCount()
{
    uint32 n = 0;
    for (ObjectGuid const& guid : sDungeonRouteMgr.GetActiveSessionGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (bot && bot->IsInWorld())
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot); botAI)
                if (sDungeonRouteMgr.State(guid).origin == DungeonLeadSessionOrigin::AutoCanary)
                    ++n;
    }
    return n;
}

std::string DungeonLead::TriggerTargetedTest(Player* master, uint32 lfgId, uint32 groups)
{
    if (!sPlayerbotAIConfig.dungeonLeadCanaryEnabled)
        return "AutoBot Canary is disabled (AiPlayerbot.DungeonLead.CanaryEnabled = 0)";

    std::unordered_set<uint32> const allowed = ParseAllowedLfgIds(sPlayerbotAIConfig.dungeonLeadCanaryAllowedLfgIds);
    if (!allowed.count(lfgId))
        return "lfg id " + std::to_string(lfgId) + " is not in AiPlayerbot.DungeonLead.CanaryAllowedLfgIds";

    LFGDungeonEntry const* dungeon = sLFGDungeonStore.LookupEntry(lfgId);
    if (!dungeon)
        return "Unknown LFG dungeon id " + std::to_string(lfgId);

    uint32 const activeNow = DungeonLead::ActiveCanaryCount();
    uint32 const cap = sPlayerbotAIConfig.dungeonLeadCanaryMaxConcurrent;
    if (activeNow >= cap)
        return "AiPlayerbot.DungeonLead.CanaryMaxConcurrent (" + std::to_string(cap) +
               ") already reached (" + std::to_string(activeNow) + " running) - nothing queued";
    groups = std::min(groups, cap - activeNow);

    struct Candidate { Player* bot; PlayerbotAI* botAI; uint32 role; };
    std::vector<Candidate> tanks, heals, dps;

    for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->GetGroup())
            continue;
        if (DungeonLead::InFiveMan(bot))
            continue;  // already inside some dungeon - not idle
        if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            continue;  // already mid-queue/mid-proposal from an earlier call or organic activity -
                       // don't re-blast a join packet at a bot LFG is already processing
        if (dungeon->MinLevel && (bot->GetLevel() < dungeon->MinLevel || bot->GetLevel() > dungeon->MaxLevel))
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;

        uint32 role = RoleMaskFor(botAI, bot);
        (role == lfg::PLAYER_ROLE_TANK ? tanks : role == lfg::PLAYER_ROLE_HEALER ? heals : dps)
            .push_back({bot, botAI, role});
    }

    // Real LFG matchmaking needs exactly 1 tank + 1 healer per completed group (LFG_TANKS_NEEDED/
    // LFG_HEALERS_NEEDED in LFG.h) - and since these are queued as individually-solo bots (not
    // pre-grouped), AC pools ALL solo queuers for this dungeon together and matches from that
    // shared pool, not just "this function's own 5 picks" against each other. A party missing
    // either role can therefore NEVER complete, no matter how long it waits - so cap how many
    // parties we even attempt at min(tanks available, heals available), rather than manufacturing
    // structurally-incomplete parties that just sit in the queue forever wasting a canary slot.
    // (Discovered live: an earlier version filled remaining parties with leftover dps once tanks/
    // heals ran out, producing tank-less/healer-less parties that queued successfully - confirmed
    // via the new "lfgstate" diagnostic - but could structurally never be matched.)
    uint32 const feasible = std::min<uint32>({groups, uint32(tanks.size()), uint32(heals.size())});

    auto pickOneParty = [&]() -> std::vector<Candidate>
    {
        std::vector<Candidate> picked;
        auto takeFront = [&](std::vector<Candidate>& bucket)
        {
            if (bucket.empty())
                return false;
            picked.push_back(bucket.front());
            bucket.erase(bucket.begin());
            return true;
        };
        takeFront(tanks);
        takeFront(heals);
        while (picked.size() < 5 && takeFront(dps)) {}
        return picked;
    };

    std::ostringstream summary;
    uint32 startedGroups = 0;
    for (uint32 g = 0; g < feasible; ++g)
    {
        std::vector<Candidate> picked = pickOneParty();
        if (picked.size() < 2)
            break;  // pool exhausted - report what we managed below, not an error

        if (startedGroups)
            summary << " | ";
        summary << "party " << (startedGroups + 1) << ": ";
        for (size_t i = 0; i < picked.size(); ++i)
        {
            if (i)
                summary << ", ";
            summary << picked[i].bot->GetName()
                    << (picked[i].role == lfg::PLAYER_ROLE_TANK ? " (tank)"
                        : picked[i].role == lfg::PLAYER_ROLE_HEALER ? " (heal)" : " (dps)");
            QueueBotForLfg(picked[i].botAI, picked[i].bot, lfgId, picked[i].role);
        }
        ++startedGroups;
    }

    if (!startedGroups)
        return "No complete party possible right now for " + std::string(dungeon->Name[0]) +
               " (level " + std::to_string(dungeon->MinLevel) + "-" + std::to_string(dungeon->MaxLevel) +
               "): " + std::to_string(tanks.size()) + " idle tank(s), " + std::to_string(heals.size()) +
               " idle healer(s) found - a real LFG group needs at least one of each, queuing a party "
               "without one would just sit unmatched forever.";

    std::string const scaledDownNote = feasible < groups
        ? " (scaled down from " + std::to_string(groups) + " requested: only " + std::to_string(tanks.size()) +
          " tank(s)/" + std::to_string(heals.size()) + " healer(s) idle right now)"
        : "";

    LOG_INFO("playerbots.dungeonlead", "[DungeonLead][Canary] targeted test requested by {} for {} ({}): {} part{} - {}",
             master ? master->GetName() : "console", dungeon->Name[0], lfgId, startedGroups,
             startedGroups == 1 ? "y" : "ies", summary.str());
    return "Queued " + std::to_string(startedGroups) + "/" + std::to_string(groups) + " requested part" +
           (groups == 1 ? "y" : "ies") + " for " + dungeon->Name[0] + scaledDownNote + " (" +
           std::to_string(activeNow) + "/" + std::to_string(cap) + " canary slots already in use before this): " +
           summary.str() + " - AutoBot Canary will take over each one automatically once LFG groups and "
           "teleports it in.";
}
