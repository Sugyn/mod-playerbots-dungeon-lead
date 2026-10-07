-- SM Cathedral: back to the route of the Verified runs (d1f1ddf, 5f3d706). The nave pull steps
-- (2026_10_06_01) and the Mograine aisle/guard steps (2026_10_07_00) did not help: campaigns
-- cf5ea8b, b0a9c1e and 66490ea had 1-2 wipes per run and fights of 19-43 units, against 0-2 wipes
-- before. The fights grow by flee-for-assistance chains across the nave (51-68 runners a run);
-- pull steps put the tank among the groups. The cure is fighting away from the other groups.
-- Replaces the Scarlet Monastery - Cathedral route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=164;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',1,'optional','High Inquisitor Fairbanks',4542,1158.9,1354.3,30.4,'hidden room'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',2,'boss','Scarlet Commander Mograine',3976,1153.9,1398.4,32.6,'clear whole cathedral first'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',3,'event','High Inquisitor Whitemane',3977,1202.1,1399.1,29.1,'enters + resurrects Mograine (scripted)');
