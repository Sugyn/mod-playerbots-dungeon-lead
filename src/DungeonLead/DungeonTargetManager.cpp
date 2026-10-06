/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 */

#include "DungeonTargetManager.h"

#include <algorithm>

#include "Creature.h"
#include "DungeonLeadActions.h"
#include "DungeonLeadConfig.h"
#include "DungeonPack.h"
#include "DungeonRouteMgr.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "DungeonTelemetryV2.h"
#include "Player.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "Timer.h"

namespace
{
    // Running for help (flee for assistance: AttackStop + an assistance move) or plain fleeing.
    bool IsRunningForHelp(Creature* c)
    {
        return c->HasUnitState(UNIT_STATE_FLEEING) ||
               c->GetMotionMaster()->GetCurrentMovementGeneratorType() == ASSISTANCE_MOTION_TYPE;
    }
}

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
            bool const fleeing = inCombat && c->IsInCombat() && IsRunningForHelp(c);
            if (fleeing && std::find(st.fleeReported.begin(), st.fleeReported.end(), guid) == st.fleeReported.end())
            {
                st.fleeReported.push_back(guid);
                DungeonLead::RecordEventV2(botAI, "mob_fleeing",
                                           DungeonLeadKernel::JsonLine()
                                               .Str("name", c->GetName())
                                               .Num("entry", uint32_t(c->GetEntry()))
                                               .Num("spawn", uint32_t(c->GetSpawnId()))
                                               .Num("health_pct", uint32_t(c->GetHealthPct()))
                                               .Num("x", c->GetPositionX())
                                               .Num("y", c->GetPositionY())
                                               .Num("z", c->GetPositionZ())
                                               .Num("fight_id", uint32_t(st.fightId))
                                               .Done());
            }
            if (inCombat)
            {
                Unit* victim = c->GetVictim();
                Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
                DungeonLeadKernel::FightCandidateFacts ff;
                // an enemy totem in the fight area counts even when it isn't flagged in combat
                ff.inCombat = c->IsInCombat() || c->IsTotem();
                ff.attackingParty = victimPlayer && group->IsMember(victimPlayer->GetGUID());
                ff.fleeing = fleeing;
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
            t.healthPct = uint8(c->GetHealthPct());
            t.totem = c->IsTotem();
            t.controlled = c->HasBreakableByDamageCrowdControlAura();
            t.ccRefused = std::find(st.ccFailed.begin(), st.ccFailed.end(), guid) != st.ccFailed.end();
            t.fleeing = fleeing;
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

    // Our CC target dropped out of the plan while still alive (it is the last enemy): take the moon
    // off so the party kills it now instead of waiting for the CC to break.
    if (!st.targetCc.IsEmpty() && cc.IsEmpty() && st.ccGuid == st.targetCc && idOf(st.targetCc))
    {
        if (group->GetTargetIcon(RtiTargetValue::moonIndex) == st.ccGuid)
            group->SetTargetIcon(RtiTargetValue::moonIndex, bot->GetGUID(), ObjectGuid::Empty);
        LOG_INFO("playerbots.dungeonlead", "[DungeonLead] {} CC on {} released - last enemy left", bot->GetName(),
                 NameOf(botAI, st.ccGuid));
        DungeonLead::RecordEvent(botAI, "cc_released", NameOf(botAI, st.ccGuid) + " reason=last_enemy");
        st.ccGuid.Clear();
    }
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
