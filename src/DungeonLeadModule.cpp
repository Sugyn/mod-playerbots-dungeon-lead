/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadModule.cpp
 *
 * Registers the DungeonLead/ business logic (unchanged from the pre-module patch, only its
 * sPlayerbotAIConfig.dungeonLead* reads redirected to our own DungeonLeadConfig) into
 * mod-playerbots via the patchless registry-access technique in DungeonLeadAccess.h - see
 * ADR-004 (docs/architecture/) for the full design and why this doesn't edit mod-playerbots.
 *
 * Also wires the two behavior-OVERRIDE points (DungeonLeadOverrides.h) via subclass +
 * registry-entry-overwrite: "formation" (Value) and "unknown dungeon" (Trigger) already exist in
 * mod-playerbots' own registries, and our registration runs after the stock one, so the same
 * `Add()` call that would add a new key simply replaces those two instead.
 *
 * Not yet ported (see ADR-004 "Known gaps"): GM commands .playerbots testbotpool/lfgstate/
 * pathcheck/... - need their own CommandScript entries under the new .dungeonlead root
 * (canarytest is already wired, see DungeonLeadCommandScript.cpp).
 */

#include "ChatCommandTrigger.h"
#include "DKAiObjectContext.h"
#include "DruidAiObjectContext.h"
#include "DungeonLead/DungeonLeadActions.h"
#include "DungeonLead/DungeonLeadCanary.h"
#include "DungeonLead/DungeonLeadStrategy.h"
#include "DungeonLead/DungeonLeadTriggers.h"
#include "DungeonLead/DungeonTestBotPool.h"
#include "DungeonLeadAccess.h"
#include "DungeonLeadOverrides.h"
#include "HunterAiObjectContext.h"
#include "MageAiObjectContext.h"
#include "Map.h"
#include "PaladinAiObjectContext.h"
#include "PassThroughStrategy.h"
#include "Playerbots.h"
#include "PriestAiObjectContext.h"
#include "RogueAiObjectContext.h"
#include "ScriptMgr.h"
#include "ShamanAiObjectContext.h"
#include "WarlockAiObjectContext.h"
#include "WarriorAiObjectContext.h"

namespace
{
    // Stands in for the three TriggerNode lines the original patch added to mod-playerbots' own
    // "chat" strategy (ChatCommandHandlerStrategy) - kept as our OWN always-on strategy instead,
    // so nothing in mod-playerbots needs editing. PassThroughStrategy (same base "chat" uses)
    // gives the default relevance=100.0f these chat-shortcut dispatches expect.
    class DungeonLeadCommandsStrategy : public PassThroughStrategy
    {
    public:
        DungeonLeadCommandsStrategy(PlayerbotAI* botAI) : PassThroughStrategy(botAI) {}

        std::string const getName() override { return "dungeon lead commands"; }

        void InitTriggers(std::vector<TriggerNode*>& triggers) override
        {
            triggers.push_back(new TriggerNode("startdungeon", {NextAction("startdungeon chat shortcut", relevance)}));
            triggers.push_back(new TriggerNode("stopdungeon", {NextAction("stopdungeon chat shortcut", relevance)}));
            triggers.push_back(new TriggerNode("canarytest", {NextAction("canarytest chat shortcut", relevance)}));
        }
    };

    class DungeonLeadStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        DungeonLeadStrategyContext()
        {
            creators["dungeon lead"] = &dungeon_lead;
            creators["dungeon lead commands"] = &dungeon_lead_commands;
        }

    private:
        static Strategy* dungeon_lead(PlayerbotAI* botAI) { return new DungeonLeadStrategy(botAI); }
        static Strategy* dungeon_lead_commands(PlayerbotAI* botAI) { return new DungeonLeadCommandsStrategy(botAI); }
    };

    class DungeonLeadTriggerContext : public NamedObjectContext<Trigger>
    {
    public:
        DungeonLeadTriggerContext()
        {
            creators["dungeon lead idle"] = &idle;
            creators["dungeon lead boss near"] = &boss_near;
            creators["dungeon lead left instance"] = &left_instance;
            creators["startdungeon"] = &startdungeon;
            creators["stopdungeon"] = &stopdungeon;
            creators["canarytest"] = &canarytest;
            // Overwrites mod-playerbots' own "unknown dungeon" entry (registered first, during
            // its own BuildAllSharedContexts()) - see DungeonLeadOverrides.h for why.
            creators["unknown dungeon"] = &unknown_dungeon;
        }

    private:
        static Trigger* idle(PlayerbotAI* botAI) { return new DungeonLeadIdleTrigger(botAI); }
        static Trigger* boss_near(PlayerbotAI* botAI) { return new DungeonLeadBossNearTrigger(botAI); }
        static Trigger* left_instance(PlayerbotAI* botAI) { return new DungeonLeadLeftInstanceTrigger(botAI); }
        static Trigger* startdungeon(PlayerbotAI* botAI) { return new ChatCommandTrigger(botAI, "startdungeon"); }
        static Trigger* stopdungeon(PlayerbotAI* botAI) { return new ChatCommandTrigger(botAI, "stopdungeon"); }
        static Trigger* canarytest(PlayerbotAI* botAI) { return new ChatCommandTrigger(botAI, "canarytest"); }
        static Trigger* unknown_dungeon(PlayerbotAI* botAI) { return new DungeonLeadAwareUnknownDungeonTrigger(botAI); }
    };

    class DungeonLeadValueContext : public NamedObjectContext<UntypedValue>
    {
    public:
        DungeonLeadValueContext()
        {
            // Overwrites mod-playerbots' own "formation" entry - see DungeonLeadOverrides.h.
            creators["formation"] = &formation;
        }

    private:
        static UntypedValue* formation(PlayerbotAI* botAI) { return new DungeonLeadFormationValue(botAI); }
    };

    class DungeonLeadActionContext : public NamedObjectContext<Action>
    {
    public:
        DungeonLeadActionContext()
        {
            creators["dungeon lead next"] = &next;
            creators["dungeon lead mark"] = &mark;
            creators["dungeon lead cc watch"] = &cc_watch;
            creators["dungeon lead stop"] = &stop;
            creators["startdungeon chat shortcut"] = &startdungeon;
            creators["stopdungeon chat shortcut"] = &stopdungeon;
            creators["canarytest chat shortcut"] = &canarytest;
        }

    private:
        static Action* next(PlayerbotAI* botAI) { return new DungeonLeadNextAction(botAI); }
        static Action* mark(PlayerbotAI* botAI) { return new DungeonLeadMarkAction(botAI); }
        static Action* cc_watch(PlayerbotAI* botAI) { return new DungeonLeadCcWatchAction(botAI); }
        static Action* stop(PlayerbotAI* botAI) { return new DungeonLeadStopAction(botAI); }
        static Action* startdungeon(PlayerbotAI* botAI) { return new StartDungChatShortcutAction(botAI); }
        static Action* stopdungeon(PlayerbotAI* botAI) { return new StopDungChatShortcutAction(botAI); }
        static Action* canarytest(PlayerbotAI* botAI) { return new CanaryTestChatShortcutAction(botAI); }
    };

    // Every real bot is built from its class's own AiObjectContext, never the base one (see
    // DungeonLeadAccess.h's comment) - register into all 11. Each `.Add()` needs its own fresh
    // instance: SharedNamedObjectContextList's destructor deletes everything it holds.
    void RegisterEverywhere()
    {
        dl_access::BaseTriggerContexts().Add(new DungeonLeadTriggerContext());
        dl_access::BaseActionContexts().Add(new DungeonLeadActionContext());
        dl_access::BaseStrategyContexts().Add(new DungeonLeadStrategyContext());
        dl_access::BaseValueContexts().Add(new DungeonLeadValueContext());

        WarriorAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        WarriorAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        WarriorAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        WarriorAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        DruidAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        DruidAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        DruidAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        DruidAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        PriestAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        PriestAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        PriestAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        PriestAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        HunterAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        HunterAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        HunterAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        HunterAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        RogueAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        RogueAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        RogueAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        RogueAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        MageAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        MageAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        MageAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        MageAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        WarlockAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        WarlockAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        WarlockAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        WarlockAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        DKAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        DKAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        DKAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        DKAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        PaladinAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        PaladinAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        PaladinAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        PaladinAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());

        ShamanAiObjectContext::sharedTriggerContexts.Add(new DungeonLeadTriggerContext());
        ShamanAiObjectContext::sharedActionContexts.Add(new DungeonLeadActionContext());
        ShamanAiObjectContext::sharedStrategyContexts.Add(new DungeonLeadStrategyContext());
        ShamanAiObjectContext::sharedValueContexts.Add(new DungeonLeadValueContext());
    }

    // "dungeon lead commands" must be on every bot from the start so it can even hear
    // "startdungeon" being whispered - mirrors how mod-playerbots' own stock "chat" strategy
    // reaches every bot: appended into the default non-combat strategy strings mod-playerbots
    // itself reads (sPlayerbotAIConfig.nonCombatStrategies / randomBotNonCombatStrategies, both
    // plain public std::string fields - no patch needed to touch them). Runs before any bot can
    // plausibly have logged in (first world tick), so every bot's default set picks it up.
    void InjectIntoDefaultStrategies()
    {
        auto append = [](std::string& strategies)
        {
            if (!strategies.empty())
                strategies += ",";
            strategies += "+dungeon lead commands";
        };
        append(sPlayerbotAIConfig.nonCombatStrategies);
        append(sPlayerbotAIConfig.randomBotNonCombatStrategies);
    }

    // Registers on the first world tick - guaranteed to run after mod-playerbots' own
    // AiObjectContext::BuildAllSharedContexts() (and PlayerbotAIConfig::Initialize(), which is
    // what InjectIntoDefaultStrategies() depends on) have already run, with no module-load-order
    // race to get right.
    class DungeonLeadWorldScript : public WorldScript
    {
    public:
        DungeonLeadWorldScript()
            : WorldScript("DungeonLeadWorldScript", {WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN})
        {
        }

        void OnShutdown() override { DungeonLead::FlushTelemetry(/*force*/ true); }

        void OnUpdate(uint32 /*diff*/) override
        {
            static bool registered = false;
            if (!registered)
            {
                registered = true;
                LOG_INFO("playerbots", "[DungeonLead] module loaded, world tick reached - registering...");
                RegisterEverywhere();
                InjectIntoDefaultStrategies();
                LOG_INFO("playerbots",
                         "[DungeonLead] registered into base + 10 per-class contexts, "
                         "'dungeon lead commands' added to default non-combat strategies");
            }

            // Same per-tick reconciliation the patch hooked into mod-playerbots' own OnUpdate
            // (Script/Playerbots.cpp) - here it's our own hook instead, no patch needed.
            DungeonLead::GuardActiveSessions();
            DungeonLead::CanaryTick();
            DungeonLead::TestBotPoolTick();
            DungeonLead::FlushTelemetry();
        }
    };

    // DungeonRouteMgr::killedByInstance (per-instance "already killed" memory, see its own comment)
    // had no lifecycle cleanup - it only ever grew for as long as the process ran. AllMapScript's
    // OnDestroyMap fires for every map/instance the core tears down, dungeon-lead's or not, which
    // is exactly when that instance's kill memory can never be read again - a stock AzerothCore
    // hook, no mod-playerbots involvement at all.
    class DungeonLeadMapScript : public AllMapScript
    {
    public:
        DungeonLeadMapScript() : AllMapScript("DungeonLeadMapScript", {ALLMAPHOOK_ON_DESTROY_MAP}) {}

        void OnDestroyMap(Map* map) override
        {
            if (map)
                sDungeonRouteMgr.ClearInstance(map->GetInstanceId());
        }
    };

}  // namespace

void AddDungeonLeadScripts()
{
    new DungeonLeadWorldScript();
    new DungeonLeadMapScript();
}
