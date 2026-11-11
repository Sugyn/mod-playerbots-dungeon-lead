/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * mod_dungeon_lead_loader.cpp - module entry point.
 *
 * The function name must be Addmod_dungeon_leadScripts() so AzerothCore's generated module
 * script loader (derived from the directory name "mod-dungeon-lead") calls it at startup - see
 * modules/CMakeLists.txt's ConfigureScriptLoader(): directory name, hyphens -> underscores,
 * "Add" + that + "Scripts".
 */

void AddDungeonLeadScripts();
void AddDungeonLeadCommandScripts();

void Addmod_dungeon_leadScripts()
{
    AddDungeonLeadScripts();
    AddDungeonLeadCommandScripts();
}
