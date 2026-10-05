-- Zul'Farrak pyramid event: the route now runs it - Troll Cage used with the Executioner's Key,
-- Sergeant Bly talked to once his crew is downstairs, the End Door (blown by Weegli) before Chief
-- Ukorz. Replaces the whole Zul'Farrak route (supersedes 2026_10_05_01_zf_end_door.sql).
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=24;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(24,209,0,'Zul\'Farrak','-',1,'boss','Theka the Martyr',7272,1778,859.7,8.9,''),
(24,209,0,'Zul\'Farrak','-',2,'boss','Antu\'sul',8127,1815.8,670.4,15,''),
(24,209,0,'Zul\'Farrak','-',3,'boss','Witch Doctor Zum\'rah',7271,1912.2,1016.1,11.6,''),
(24,209,0,'Zul\'Farrak','-',4,'boss','Sandfury Executioner',7274,1886.8,1289.9,46,'pyramid top, drops Executioner\'s Key'),
(24,209,0,'Zul\'Farrak','-',5,'use','Troll Cage',141070,1890.96,1294.47,48.1535,'opened with Executioner\'s Key - starts the pyramid event (waves on the stairs)'),
(24,209,0,'Zul\'Farrak','-',6,'talk','Sergeant Bly',7604,1882.9,1299.3,48.4,'gossip once the crew is downstairs - he and his crew turn hostile, Weegli blows the End Door'),
(24,209,0,'Zul\'Farrak','-',7,'event','Nekrum Gutchewer',7796,NULL,NULL,NULL,'wave 3'),
(24,209,0,'Zul\'Farrak','-',8,'event','Shadowpriest Sezz\'ziz',NULL,NULL,NULL,NULL,'wave 3'),
(24,209,0,'Zul\'Farrak','-',9,'door','End Door',146084,1854.53,1142.89,16.0846,'blown by Weegli after the Bly talk; Ukorz is behind it'),
(24,209,0,'Zul\'Farrak','-',10,'boss','Chief Ukorz Sandscalp',7267,1727.5,1017.3,54.9,'with Ruuzlu'),
(24,209,0,'Zul\'Farrak','-',11,'optional','Hydromancer Velratha',7795,1698.2,1210.7,9.4,''),
(24,209,0,'Zul\'Farrak','-',12,'optional','Gahz\'rilla',NULL,NULL,NULL,NULL,'Mallet summon');
