-- SM Cathedral: the nave's packs (three rows of pews) are pulled one by one from the door.
-- Evidence: campaign verify-20261006-1626-d1f1ddf, both Cathedral runs - the walk to Fairbanks met
-- 13 units at once and 6+12 units (two wipes, findings ROUTE).
-- Replaces the Scarlet Monastery - Cathedral route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=164;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',1,'optional','Scarlet Centurion',4301,1064.4,1387.3,30.8,'cathedral nave: the packs are pulled one by one from the door - crossing the nave pulled 13 at once'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',2,'optional','Scarlet Centurion',4301,1064.8,1410.8,30.8,'cathedral nave: centurions north of the door'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',3,'optional','Scarlet Chaplain',4299,1081.6,1379.5,30.4,'cathedral nave: south pews 1'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',4,'optional','Scarlet Wizard',4300,1082.9,1417.3,30.3,'cathedral nave: north pews 1'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',5,'optional','Scarlet Monk',4540,1093.9,1391,30.4,'cathedral nave: aisle 1 south'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',6,'optional','Scarlet Abbot',4303,1093.9,1405.2,30.4,'cathedral nave: aisle 1 north'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',7,'optional','Scarlet Champion',4302,1106.8,1379.2,30.3,'cathedral nave: south pews 2'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',8,'optional','Scarlet Champion',4302,1104,1419.4,30.4,'cathedral nave: north pews 2'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',9,'optional','Scarlet Abbot',4303,1106.5,1395.5,30.4,'cathedral nave: aisle 2'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',10,'optional','Scarlet Wizard',4300,1113.1,1380.5,30.3,'cathedral nave: south pews 3'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',11,'optional','Scarlet Champion',4302,1123.8,1419.3,30.4,'cathedral nave: north pews 3'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',12,'optional','Scarlet Abbot',4303,1118.6,1394.8,30.4,'cathedral nave: aisle 3'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',13,'optional','Scarlet Champion',4302,1131.1,1393,30.4,'cathedral nave: aisle 4 south'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',14,'optional','Scarlet Abbot',4303,1131.2,1405.1,30.4,'cathedral nave: aisle 4 north'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',15,'optional','Scarlet Champion',4302,1138.5,1369.4,30.4,'cathedral nave: by the altar'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',16,'optional','High Inquisitor Fairbanks',4542,1158.9,1354.3,30.4,'hidden room'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',17,'boss','Scarlet Commander Mograine',3976,1153.9,1398.4,32.6,'clear whole cathedral first'),
(164,189,0,'Scarlet Monastery - Cathedral','Cathedral',18,'event','High Inquisitor Whitemane',3977,1202.1,1399.1,29.1,'enters + resurrects Mograine (scripted)');
