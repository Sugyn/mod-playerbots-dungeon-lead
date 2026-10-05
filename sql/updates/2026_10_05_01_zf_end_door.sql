-- Zul'Farrak: the End Door (opened when Sergeant Bly dies, after the pyramid event) guards Chief
-- Ukorz Sandscalp. It is a route step before him, so the leader waits at it instead of walking
-- through it (mmaps don't know doors).
UPDATE `playerbots_dungeon_route` SET `step`=11 WHERE `lfg_id`=24 AND `step`=10 AND `boss`='Gahz\'rilla';
UPDATE `playerbots_dungeon_route` SET `step`=10 WHERE `lfg_id`=24 AND `step`=9 AND `boss`='Hydromancer Velratha';
UPDATE `playerbots_dungeon_route` SET `step`=9 WHERE `lfg_id`=24 AND `step`=8 AND `boss`='Chief Ukorz Sandscalp';
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`)
  SELECT 24,209,0,'Zul\'Farrak','-',8,'door','End Door',146084,1854.53,1142.89,16.0846,'opens when Sergeant Bly dies (pyramid event); Ukorz is behind it'
  FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `playerbots_dungeon_route` WHERE `lfg_id`=24 AND `step`=8);
