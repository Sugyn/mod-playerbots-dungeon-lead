-- Zul'Farrak pyramid event, as players run it: Weegli is asked first (he blows the End Door and
-- leaves), then Sergeant Bly (he and his crew turn hostile) - asked the other way round, Weegli
-- turns hostile with them and dies before the door is blown. Bly is fought downstairs.
-- Replaces the Zul'Farrak route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=24;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(24,209,0,'Zul\'Farrak','-',1,'boss','Theka the Martyr',7272,1778,859.7,8.9,''),
(24,209,0,'Zul\'Farrak','-',2,'boss','Antu\'sul',8127,1815.8,670.4,15,''),
(24,209,0,'Zul\'Farrak','-',3,'boss','Witch Doctor Zum\'rah',7271,1912.2,1016.1,11.6,''),
(24,209,0,'Zul\'Farrak','-',4,'boss','Sandfury Executioner',7274,1886.8,1289.9,46,'pyramid top, drops Executioner\'s Key'),
(24,209,0,'Zul\'Farrak','-',5,'use','Troll Cage',141070,1890.96,1294.47,48.1535,'opened with Executioner\'s Key - the waves come up the stairs at once'),
(24,209,0,'Zul\'Farrak','-',6,'talk','Weegli Blastfuse',7607,1881.05,1297.36,48.419,'once the crew is downstairs: he blows the End Door - first, or he turns hostile with Bly'),
(24,209,0,'Zul\'Farrak','-',7,'talk','Sergeant Bly',7604,1882.89,1299.27,48.3843,'after Weegli: Bly and his crew turn hostile'),
(24,209,0,'Zul\'Farrak','-',8,'required','Sergeant Bly',7604,1883.82,1200.83,8.87,'fought where the crew waits downstairs'),
(24,209,0,'Zul\'Farrak','-',9,'event','Nekrum Gutchewer',7796,NULL,NULL,NULL,'wave 3'),
(24,209,0,'Zul\'Farrak','-',10,'event','Shadowpriest Sezz\'ziz',NULL,NULL,NULL,NULL,'wave 3'),
(24,209,0,'Zul\'Farrak','-',11,'door','End Door',146084,1854.53,1142.89,16.0846,'blown by Weegli; Ukorz is behind it'),
(24,209,0,'Zul\'Farrak','-',12,'boss','Chief Ukorz Sandscalp',7267,1727.5,1017.3,54.9,'with Ruuzlu'),
(24,209,0,'Zul\'Farrak','-',13,'optional','Hydromancer Velratha',7795,1698.2,1210.7,9.4,''),
(24,209,0,'Zul\'Farrak','-',14,'optional','Gahz\'rilla',NULL,NULL,NULL,NULL,'Mallet summon');
