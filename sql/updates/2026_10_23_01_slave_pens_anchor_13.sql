-- Slave Pens step 30, path anchor "Mennu to Rokmar 13": (-45.7, -478.5, 7.2) is 9 yd above the floor
-- the leader walks; no path reached it (NOPATH|SHORTCUT, `skip_stuck` on 41f4375 and 3e3d6d2, 11
-- `direct_rejected` on 3e3d6d2). Moved to (-42.0, -473.8, -1.6), where leaders arrived from anchor 12
-- in every logged run (position_sample, campaigns 0740-41f4375 and 1421-3e3d6d2) and from where the
-- walk reached anchor 14 each time.
-- Apply to acore_playerbots on an existing install; a fresh install gets this from
-- playerbots_dungeon_route.sql.
UPDATE `playerbots_dungeon_route` SET `x`=-42.0, `y`=-473.8, `z`=-1.6
  WHERE `lfg_id`=140 AND `step`=30 AND `boss`='Slave Pens path Mennu to Rokmar 13';
