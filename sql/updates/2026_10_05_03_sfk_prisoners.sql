-- Shadowfang Keep: the courtyard door is opened by the prisoner of the party's faction once asked
-- ("Please unlock the courtyard door") after Rethilgore - both prisoners are talk steps; the one
-- hostile to the leader is skipped. Replaces the Shadowfang Keep route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=8;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(8,33,0,'Shadowfang Keep','-',1,'boss','Rethilgore',3914,-252.1,2123.1,81.2,''),
(8,33,0,'Shadowfang Keep','-',2,'talk','Deathstalker Adamant',3849,-243.712,2113.72,81.2629,'Horde prisoner: \'Please unlock the courtyard door\' after Rethilgore'),
(8,33,0,'Shadowfang Keep','-',3,'talk','Sorcerer Ashcrombe',3850,-240.904,2122.55,81.2629,'Alliance prisoner: \'Please unlock the courtyard door\' after Rethilgore'),
(8,33,0,'Shadowfang Keep','-',4,'door','Courtyard door',18895,-242.581,2159.05,90.6226,'opened by Deathstalker Adamant / Sorcerer Ashcrombe after Rethilgore'),
(8,33,0,'Shadowfang Keep','-',5,'boss','Razorclaw the Butcher',3886,-202.6,2258,76.3,''),
(8,33,0,'Shadowfang Keep','-',6,'boss','Baron Silverlaine',3887,-275.3,2297.4,76.2,''),
(8,33,0,'Shadowfang Keep','-',7,'boss','Commander Springvale',4278,-222.6,2259.4,102.8,''),
(8,33,0,'Shadowfang Keep','-',8,'boss','Odo the Blindwatcher',4279,-236.7,2146.1,100.1,''),
(8,33,0,'Shadowfang Keep','-',9,'boss','Fenrus the Devourer',4274,-135.6,2168.7,128.8,''),
(8,33,0,'Shadowfang Keep','-',10,'boss','Wolf Master Nandos',3927,-120.7,2162,155.8,'kills opens Arugal\'s door'),
(8,33,0,'Shadowfang Keep','-',11,'boss','Archmage Arugal',4275,-76.8,2152.4,155.8,'');
