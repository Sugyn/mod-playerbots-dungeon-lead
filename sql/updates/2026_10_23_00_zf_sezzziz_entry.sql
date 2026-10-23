-- Zul'Farrak step 10 (wave 3): Shadowpriest Sezz'ziz had no creature entry (the resolver found no
-- static spawn - he is summoned by instance_zulfarrak.cpp SpawnPyramidWave(3), NPC_SHADOWPRIEST_SEZZZIZ
-- = 7275 in zulfarrak.h). The talk step uses the route's event entries to find wave creatures standing
-- idle while Weegli waits for the wave to end. No position: still not walked.
-- Apply to acore_playerbots on an existing install; a fresh install gets this from
-- playerbots_dungeon_route.sql.
UPDATE `playerbots_dungeon_route` SET `entry`=7275
  WHERE `lfg_id`=24 AND `step`=10 AND `kind`='event' AND `boss`='Shadowpriest Sezz\'ziz';
