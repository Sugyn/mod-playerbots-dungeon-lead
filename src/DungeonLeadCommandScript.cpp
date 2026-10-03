/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonLeadCommandScript.cpp
 *
 * Own GM command root (`.dungeonlead ...`), separate from mod-playerbots' own `.playerbots ...`
 * table - a fresh CommandScript under a new root needs no edit to PlayerbotCommandScript.cpp
 * (AzerothCore's own CommandScript registration is already a patchless extension point).
 *
 * For now: just `canarytest`, enough to drive an on-demand, SOAP-reachable end-to-end test of the
 * ported DungeonLead logic without a live game session - see ADR-004 (docs/architecture/) for why
 * this exists as its own command root instead of extending mod-playerbots' own command table.
 */

#include "Chat.h"
#include "DungeonLead/DungeonLeadCanary.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

using namespace Acore::ChatCommands;

namespace
{
    bool HandleDungeonLeadCanaryTestCommand(ChatHandler* handler, char const* args)
    {
        uint32 lfgId = 0, groups = 1;
        try
        {
            if (args && *args)
            {
                std::istringstream iss(args);
                std::string lfgTok, groupsTok;
                iss >> lfgTok;
                lfgId = static_cast<uint32>(std::stoul(lfgTok));
                if (iss >> groupsTok)
                    groups = std::max(1u, static_cast<uint32>(std::stoul(groupsTok)));
            }
        }
        catch (std::exception const&)
        {
            lfgId = 0;
        }
        if (!lfgId)
        {
            handler->PSendSysMessage("Usage: .dungeonlead canarytest <lfgId> [groups]");
            return true;
        }

        std::string const result = DungeonLead::TriggerTargetedTest(nullptr, lfgId, groups);
        handler->PSendSysMessage(result);
        return true;
    }

    class dungeon_lead_commandscript : public CommandScript
    {
    public:
        dungeon_lead_commandscript() : CommandScript("dungeon_lead_commandscript") {}

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable dungeonLeadTable = {
                {"canarytest", HandleDungeonLeadCanaryTestCommand, SEC_GAMEMASTER, Console::Yes},
            };
            static ChatCommandTable root = {{"dungeonlead", dungeonLeadTable}};
            return root;
        }
    };

}  // namespace

void AddDungeonLeadCommandScripts()
{
    new dungeon_lead_commandscript();
}
