#!/usr/bin/env bash
# Writes src/DungeonLead/DungeonLeadBuildInfo.generated.h with the current commit, for telemetry
# lineage (docs/telemetry-schema-v2.md). Run before syncing the sources to the build host; a tree
# with uncommitted changes gets a "-dirty" suffix, so its runs are never mistaken for the commit.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sha="$(git -C "$root" rev-parse --short=7 HEAD)"
if ! git -C "$root" diff --quiet HEAD -- src sql data conf; then
    sha="$sha-dirty"
fi
out="$root/src/DungeonLead/DungeonLeadBuildInfo.generated.h"
new="#define DUNGEONLEAD_COMMIT_SHA \"$sha\""
# rewrite only on change, so an unchanged commit doesn't trigger a rebuild
if [ ! -f "$out" ] || [ "$(cat "$out")" != "$new" ]; then
    echo "$new" > "$out"
fi
echo "$sha"
