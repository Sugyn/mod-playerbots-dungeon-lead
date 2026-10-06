/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * Build lineage for telemetry (schema v2): the module version, and the commit the build was made
 * from - written by tools/write_build_info.sh into DungeonLeadBuildInfo.generated.h (not in git)
 * before a build; "unknown" without it.
 */

#ifndef MOD_DUNGEONLEAD_BUILDINFO_H
#define MOD_DUNGEONLEAD_BUILDINFO_H

#if __has_include("DungeonLeadBuildInfo.generated.h")
#include "DungeonLeadBuildInfo.generated.h"
#endif

#ifndef DUNGEONLEAD_COMMIT_SHA
#define DUNGEONLEAD_COMMIT_SHA "unknown"
#endif

#define DUNGEONLEAD_MODULE_VERSION "0.13.0-alpha"

#endif
