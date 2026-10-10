-- Slave Pens: path anchors Mennu to Rokmar. Campaign 0709-fcdda84 runs 7554/7559: no path from the entrance to Mennu (path type NOPATH). The mmtile route is 87 polygons (823 yd) for 240 yd as the crow flies, more than one PathGenerator query handles; anchors from an offline A* over the mesh.
-- Replaces the Slave Pens route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=140;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(140,547,0,'Slave Pens','-',1,'boss','Mennu the Betrayer',17941,49.5,-380.2,3,''),
(140,547,0,'Slave Pens','-',2,'optional','Slave Pens path Mennu to Rokmar 1',1,79.5,-334.2,3.3,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',3,'optional','Slave Pens path Mennu to Rokmar 2',1,32.0,-323.5,3.3,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',4,'optional','Slave Pens path Mennu to Rokmar 3',1,-10.3,-306.7,2.6,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',5,'optional','Slave Pens path Mennu to Rokmar 4',1,-10.7,-330.7,-13.0,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',6,'optional','Slave Pens path Mennu to Rokmar 5',1,-15.1,-373.3,-13.0,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',7,'optional','Slave Pens path Mennu to Rokmar 6',1,-8.6,-425.8,-13.0,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',8,'optional','Slave Pens path Mennu to Rokmar 7',1,-41.5,-452.0,-12.8,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',9,'optional','Slave Pens path Mennu to Rokmar 8',1,-74.7,-472.0,-13.0,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',10,'optional','Slave Pens path Mennu to Rokmar 9',1,-126.6,-481.3,-10.4,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',11,'optional','Slave Pens path Mennu to Rokmar 10',1,-101.0,-516.9,-9.5,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',12,'optional','Slave Pens path Mennu to Rokmar 11',1,-96.4,-519.8,-2.0,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',13,'optional','Slave Pens path Mennu to Rokmar 12',1,-74.1,-488.5,0.1,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',14,'optional','Slave Pens path Mennu to Rokmar 13',1,-45.7,-478.5,7.2,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',15,'optional','Slave Pens path Mennu to Rokmar 14',1,-11.0,-457.1,2.6,'path anchor; navmesh route is 87 polygons long so one path query fails (offline A* on mmtile polygons)'),
(140,547,0,'Slave Pens','-',16,'boss','Rokmar the Crackler',17991,18.3,-448.4,3.1,''),
(140,547,0,'Slave Pens','-',17,'boss','Quagmirran',17942,-281.1,-667.1,9.4,'');
