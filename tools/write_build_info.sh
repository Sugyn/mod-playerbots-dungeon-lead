#!/usr/bin/env bash
# Writes src/DungeonLead/DungeonLeadBuildInfo.generated.h with the current commit, for telemetry
# lineage (docs/telemetry-schema-v2.md). Run before syncing the sources to the build host; a tree
# with uncommitted changes gets a "-dirty" suffix, so its runs are never mistaken for the commit.
# Optional argument: the mod-playerbots commit of the build tree (e.g.
# `git -C <core>/modules/mod-playerbots rev-parse --short=12 HEAD` on the build host, with
# "-dirty" if it has local changes); without it the run context records "unknown".
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sha="$(git -C "$root" rev-parse --short=7 HEAD)"
if ! git -C "$root" diff --quiet HEAD -- src sql data conf; then
    sha="$sha-dirty"
fi
out="$root/src/DungeonLead/DungeonLeadBuildInfo.generated.h"
new="#define DUNGEONLEAD_COMMIT_SHA \"$sha\""
if [ -n "${1:-}" ]; then
    new="$new
#define DUNGEONLEAD_PLAYERBOTS_SHA \"$1\""
fi
# rewrite only on change, so an unchanged commit doesn't trigger a rebuild
if [ ! -f "$out" ] || [ "$(cat "$out")" != "$new" ]; then
    echo "$new" > "$out"
fi
echo "$sha"
