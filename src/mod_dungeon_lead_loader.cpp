/*
 * mod-dungeon-lead (PoC) - module entry point.
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
