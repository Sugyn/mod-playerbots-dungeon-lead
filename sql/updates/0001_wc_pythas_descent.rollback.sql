-- Rollback of 0001_wc_pythas_descent: puts the previous Wailing Caverns route back.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=1;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(1,43,0,'Wailing Caverns','-',1,'boss','Lady Anacondra',3671,-67.8,122.6,-92.8,'Screaming Gully'),
(1,43,0,'Wailing Caverns','-',2,'optional','Kresh',3653,-64.4,319.1,-106.7,'river turtle'),
(1,43,0,'Wailing Caverns','-',3,'optional','Cobrahn approach (bridge)',1,-104.533,427.733,-99.2298,'path bridge: the direct Kresh->Cobrahn path ends here; Cobrahn is reachable from here (entry=1 waypoint template that never spawns)'),
(1,43,0,'Wailing Caverns','-',4,'boss','Lord Cobrahn',3669,-151.1,414.4,-72.6,'Fanglord 1/4. Dead end: no direct path from here to Verdan/Serpentis/Pythas/Skum/Naralex - return via the step 3 bridge'),
(1,43,0,'Wailing Caverns','-',5,'optional','Cobrahn return (bridge)',1,-104.533,427.733,-99.2298,'same point as step 3 (return leg) - no direct path Cobrahn->Verdan'),
(1,43,0,'Wailing Caverns','-',6,'optional','Verdan approach (bridge)',1,-55.7333,67.4667,-85.9882,'path bridge hub (also reached from Kresh/Naralex directly); Verdan is reachable from here'),
(1,43,0,'Wailing Caverns','-',7,'optional','Verdan the Everliving',5775,-81.9,32.3,-31,'Fanglord 2/4.'),
(1,43,0,'Wailing Caverns','-',8,'boss','Lord Serpentis',3673,-120.2,-24.6,-28.6,'Fanglord 3/4. Reachable directly from Verdan'),
(1,43,0,'Wailing Caverns','-',9,'optional','Pythas approach (bridge)',1,-101.333,-42.6667,-22.0857,'path bridge: the direct Serpentis->Pythas path ends here; Pythas is reachable from here'),
(1,43,0,'Wailing Caverns','-',10,'boss','Lord Pythas',3670,36.8,-241.1,-79.5,'Fanglord 4/4 - all 4 Fanglords dead after this Naralex escort can spawn.'),
(1,43,0,'Wailing Caverns','-',11,'optional','Skum',3674,-285.6,-313,-69.2,'reachable directly from Pythas; dead end - return via Pythas'),
(1,43,0,'Wailing Caverns','-',12,'optional','Pythas return (bridge)',1,36.8,-241.1,-79.5,'same point as step 10; entry=1 waypoint (a boss entry would be skipped once that boss is dead); no direct path Skum->Naralex'),
(1,43,0,'Wailing Caverns','-',13,'optional','Naralex approach (bridge)',1,-162.133,92.2667,-76.0141,'path bridge: the direct Pythas->Naralex path ends here; Naralex is reachable from here'),
(1,43,0,'Wailing Caverns','-',14,'event','Disciple of Naralex',3678,-135,125.4,-78.1,'escort from entrance after 4 Fanglords -> spawns Mutanus'),
(1,43,0,'Wailing Caverns','-',15,'event','Mutanus the Devourer',3654,NULL,NULL,NULL,'script-spawned by escort');
