-- Shadowfang Keep courtyard: the trash packs between the courtyard door and Razorclaw stand close
-- together; walking straight to Razorclaw crossed the middle and pulled three packs at once (live,
-- GM-observed 2026-10-06). They are optional pull steps now, taken one by one from the door side.
-- Replaces the Shadowfang Keep route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=8;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(8,33,0,'Shadowfang Keep','-',1,'boss','Rethilgore',3914,-252.1,2123.1,81.2,''),
(8,33,0,'Shadowfang Keep','-',2,'talk','Deathstalker Adamant',3849,-243.712,2113.72,81.2629,'Horde prisoner: \'Please unlock the courtyard door\' after Rethilgore'),
(8,33,0,'Shadowfang Keep','-',3,'talk','Sorcerer Ashcrombe',3850,-240.904,2122.55,81.2629,'Alliance prisoner: \'Please unlock the courtyard door\' after Rethilgore'),
(8,33,0,'Shadowfang Keep','-',4,'door','Courtyard door',18895,-242.581,2159.05,90.6226,'opened by Deathstalker Adamant / Sorcerer Ashcrombe after Rethilgore'),
(8,33,0,'Shadowfang Keep','-',5,'optional','Slavering Worg',3862,-231.9,2165.9,79.8,'courtyard: worg by the door - the courtyard packs are pulled one by one - not crossed'),
(8,33,0,'Shadowfang Keep','-',6,'optional','Shadowfang Moonwalker',3853,-223.4,2202.7,79.8,'courtyard: pair west of the fountain'),
(8,33,0,'Shadowfang Keep','-',7,'optional','Slavering Worg',3862,-204,2184.1,79.8,'courtyard: worg + moonwalkers east'),
(8,33,0,'Shadowfang Keep','-',8,'optional','Haunted Servitor',3875,-205.2,2208.9,79.8,'courtyard: servitor + worg patrol at the fountain'),
(8,33,0,'Shadowfang Keep','-',9,'optional','Shadowfang Moonwalker',3853,-217.1,2225.2,79.8,'courtyard: north of the fountain'),
(8,33,0,'Shadowfang Keep','-',10,'optional','Shadowfang Moonwalker',3853,-199.1,2221,79.8,'courtyard: moonwalker + worg north-east'),
(8,33,0,'Shadowfang Keep','-',11,'optional','Fel Steed',3864,-223.6,2245.7,79.9,'courtyard: fel steeds + moonwalker below Razorclaw'),
(8,33,0,'Shadowfang Keep','-',12,'boss','Razorclaw the Butcher',3886,-202.6,2258,76.3,''),
(8,33,0,'Shadowfang Keep','-',13,'boss','Baron Silverlaine',3887,-275.3,2297.4,76.2,''),
(8,33,0,'Shadowfang Keep','-',14,'boss','Commander Springvale',4278,-222.6,2259.4,102.8,''),
(8,33,0,'Shadowfang Keep','-',15,'boss','Odo the Blindwatcher',4279,-236.7,2146.1,100.1,''),
(8,33,0,'Shadowfang Keep','-',16,'boss','Fenrus the Devourer',4274,-135.6,2168.7,128.8,''),
(8,33,0,'Shadowfang Keep','-',17,'boss','Wolf Master Nandos',3927,-120.7,2162,155.8,'kills opens Arugal\'s door'),
(8,33,0,'Shadowfang Keep','-',18,'boss','Archmage Arugal',4275,-76.8,2152.4,155.8,'');
