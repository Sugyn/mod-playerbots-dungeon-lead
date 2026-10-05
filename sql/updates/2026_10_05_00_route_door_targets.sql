-- Route door rows name their game object (entry = gameobject entry) and its position, so the
-- leader walks to that door and waits for it to open instead of guessing from nearby doors.
-- Apply to acore_playerbots on an existing install; a fresh install gets this from
-- playerbots_dungeon_route.sql.
UPDATE `playerbots_dungeon_route` SET `entry`=16397, `x`=-100.502, `y`=-668.771, `z`=7.41049
  WHERE `lfg_id`=6 AND `step`=4 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `entry`=18895, `x`=-242.581, `y`=2159.05, `z`=90.6226
  WHERE `lfg_id`=8 AND `step`=2 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `entry`=175167, `x`=174.378, `y`=77.9398, `z`=104.802
  WHERE `lfg_id`=2 AND `step`=4 AND `kind`='door';
