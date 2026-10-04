/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonTargetManager.h"

#include "Creature.h"
#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonPack.h"
#include "DungeonRouteMgr.h"
#include "Group.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "Timer.h"

namespace
{
    // Before a pull: hostiles this close to the pack's live creature belong to its fight.
    // During a fight: units in combat this close to the leader.
    constexpr float kPackRadius = 20.0f;
    constexpr float kFightRadius = 40.0f;

    bool IsElite(Creature const* c)
    {
        CreatureTemplate const* ct = c->GetCreatureTemplate();
        return ct && (ct->rank == CREATURE_ELITE_ELITE || ct->rank == CREATURE_ELITE_RAREELITE ||
                      ct->rank == CREATURE_ELITE_WORLDBOSS);
    }

    bool IsBoss(Creature const* c)
    {
        CreatureTemplate const* ct = c->GetCreatureTemplate();
        return c->IsDungeonBoss() || (ct && ct->rank == CREATURE_ELITE_WORLDBOSS);
    }

    std::string NameOf(PlayerbotAI* botAI, ObjectGuid guid)
    {
        Unit* u = guid.IsEmpty() ? nullptr : botAI->GetUnit(guid);
        return u ? u->GetName() : "-";
    }

    // Moves `index` to `want` only if the icon is free, on a dead unit, or ours already - never
    // over a mark someone else placed on a living unit. Tracks ownership in `owned`.
    bool SyncMark(PlayerbotAI* botAI, Group* group, int8 index, ObjectGuid want, ObjectGuid& owned)
    {
        Player* bot = botAI->GetBot();
        ObjectGuid const current = group->GetTargetIcon(index);
        if (current == want)
            return false;
        Unit* holder = current.IsEmpty() ? nullptr : botAI->GetUnit(current);
        bool const free = !holder || !holder->IsAlive() || current == owned;
        if (!free)
            return false;
        if (want.IsEmpty() && current != owned)
            return false;  // nothing to place, and the icon isn't ours to clear
        group->SetTargetIcon(index, bot->GetGUID(), want);
        owned = want;
        return true;
    }
}

bool DungeonTargetManager::Update(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Group* group = bot->GetGroup();
    if (!group || !DungeonLead::InFiveMan(bot))
        return false;
    DungeonLeadState& st = sDungeonRouteMgr.State(bot->GetGUID());

    // Where the fight is (or is about to be).
    bool const inCombat = bot->IsInCombat();
    bool haveAnchor = false;
    float ax = 0.f, ay = 0.f, az = 0.f;
    if (inCombat)
    {
        // The fight's own anchor (where it began, or the boss's spot), so the candidate area does
        // not move with the tank into a neighbouring pack; the tank's position only as a fallback.
        haveAnchor = true;
        ax = st.anchorSet ? st.anchorX : bot->GetPositionX();
        ay = st.anchorSet ? st.anchorY : bot->GetPositionY();
        az = st.anchorSet ? st.anchorZ : bot->GetPositionZ();
    }
    else if (DungeonRoute const* route = st.lfgId ? sDungeonRouteMgr.GetByLfgId(st.lfgId) : nullptr)
    {
        DungeonPack const pack = DungeonPacks::ForStep(*route, st.stepIndex);
        if (pack.Exists() && (pack.type == DungeonRouteNodeType::Pull || pack.type == DungeonRouteNodeType::Boss))
            if (Creature* lead = DungeonPacks::Observe(bot, pack, st.instanceId).firstAlive)
            {
                haveAnchor = true;
                ax = lead->GetPositionX();
                ay = lead->GetPositionY();
                az = lead->GetPositionZ();
            }
    }

    std::vector<DungeonLeadKernel::TargetCandidate> candidates;
    std::vector<ObjectGuid> guids;  // parallel: candidate id -> guid
    if (haveAnchor)
    {
        float const radius = inCombat ? kFightRadius : kPackRadius;
        for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets")->Get())
        {
            Creature* c = botAI->GetCreature(guid);
            if (!c || !c->IsAlive() || !c->IsInWorld() || !bot->IsValidAttackTarget(c))
                continue;
            float const d = c->GetExactDist(ax, ay, az);
            if (inCombat)
            {
                Unit* victim = c->GetVictim();
                Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
                DungeonLeadKernel::FightCandidateFacts ff;
                ff.inCombat = c->IsInCombat();
                ff.attackingParty = victimPlayer && group->IsMember(victimPlayer->GetGUID());
                ff.distToAnchor = d;
                if (!DungeonLeadKernel::IsFightCandidate(ff, radius))
                    continue;
            }
            else if (d > radius)
                continue;
            DungeonLeadKernel::TargetCandidate t;
            t.id = guids.size() + 1;
            t.boss = IsBoss(c);
            t.caster = c->getPowerType() == POWER_MANA;
            t.elite = IsElite(c);
            t.distToAnchor = d;
            candidates.push_back(t);
            guids.push_back(guid);
        }
    }

    // Map the previous plan's guids to this round's candidate ids.
    auto idOf = [&](ObjectGuid g) -> uint64_t
    {
        for (size_t i = 0; i < guids.size(); ++i)
            if (guids[i] == g)
                return i + 1;
        return 0;
    };
    auto guidOf = [&](uint64_t id) { return id ? guids[id - 1] : ObjectGuid::Empty; };

    DungeonLeadKernel::TargetPlan prev;
    prev.primary = idOf(st.targetPrimary);
    prev.secondary = idOf(st.targetSecondary);
    prev.cc = idOf(st.targetCc);
    DungeonLeadKernel::TargetPlan const plan =
        DungeonLeadKernel::PickTargetPlan(candidates, prev, sDungeonLeadConfig.dungeonLeadMarkCc);

    ObjectGuid const primary = guidOf(plan.primary);
    ObjectGuid const secondary = guidOf(plan.secondary);
    ObjectGuid const cc = guidOf(plan.cc);
    if (primary != st.targetPrimary || secondary != st.targetSecondary || cc != st.targetCc)
    {
        st.targetPrimary = primary;
        st.targetSecondary = secondary;
        st.targetCc = cc;
        if (!primary.IsEmpty() || !secondary.IsEmpty() || !cc.IsEmpty())
        {
            std::string const line = "primary=" + NameOf(botAI, primary) + " secondary=" + NameOf(botAI, secondary) +
                                     " cc=" + NameOf(botAI, cc) + " candidates=" + std::to_string(candidates.size());
            LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} target plan {}", bot->GetName(), line);
            DungeonLead::RecordEvent(botAI, "target_plan", line);
        }
    }

    // Mirror the plan in the raid marks. An empty plan leaves existing marks alone (a fight's
    // marks stay until their units die, and the next plan reuses the icons).
    bool changed = false;
    if (!primary.IsEmpty())
        changed |= SyncMark(botAI, group, RtiTargetValue::skullIndex, primary, st.skullGuid);
    if (!secondary.IsEmpty())
        changed |= SyncMark(botAI, group, RtiTargetValue::crossIndex, secondary, st.crossGuid);

    // The moon has its own lifecycle (DungeonLead::CheckCcMark releases it when CC doesn't land in
    // time): place it only when no CC mark of ours is live.
    if (!cc.IsEmpty() && sDungeonLeadConfig.dungeonLeadMarkCc)
    {
        Unit* moon = botAI->GetUnit(group->GetTargetIcon(RtiTargetValue::moonIndex));
        if (!moon || !moon->IsAlive())
        {
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), cc);
            st.ccGuid = cc;
            st.ccMarkedTs = getMSTime();
            st.ccMarkedAbsoluteTs = st.ccMarkedTs;
            st.ccLandedTold = false;
            DungeonLead::RecordEvent(botAI, "mark_moon", NameOf(botAI, cc));
            changed = true;
        }
    }
    return changed;
}
