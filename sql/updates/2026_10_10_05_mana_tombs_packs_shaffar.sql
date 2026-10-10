-- Mana-Tombs: 4 packs as pull steps before Nexus-Prince Shaffar. Campaign 0947-4a1a6d7: wipes on the way to Shaffar with 7-14 units (trash fight on the walk); packs met in 3 of 5 runs (tools route_packs.py).
-- Replaces the Mana-Tombs route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=148;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(148,557,0,'Mana-Tombs','-',1,'optional','Ethereal Scavenger',18309,-68.0,-31.7,-1.0,'pack at the start; met on the way in 1/2 runs'),
(148,557,0,'Mana-Tombs','-',2,'optional','Ethereal Scavenger',18309,-97.9,-59.9,-0.8,'pack that joined Pandemonius as adds in 2/2 runs'),
(148,557,0,'Mana-Tombs','-',3,'optional','Ethereal Sorcerer',18313,-67.7,-67.3,-0.8,'pack that joined Pandemonius as adds in 2/2 runs'),
(148,557,0,'Mana-Tombs','-',4,'optional','Ethereal Sorcerer',18313,-46.7,-97.8,-0.9,'pack of 7 next to Pandemonius; joined as adds in 2/2 runs'),
(148,557,0,'Mana-Tombs','-',5,'boss','Pandemonius',18341,-68,-118.9,-1.2,''),
(148,557,0,'Mana-Tombs','-',6,'boss','Tavarok',18343,-321.7,-222.2,-0.8,''),
(148,557,0,'Mana-Tombs','-',8,'optional','Ethereal Spellbinder',18312,-353.0,-70.3,-1.0,'pack met on the way to Shaffar in 3 of 5 runs'),
(148,557,0,'Mana-Tombs','-',9,'optional','Nexus Stalker',18314,-389.2,-73.2,-0.9,'pack met on the way to Shaffar in 3 of 5 runs'),
(148,557,0,'Mana-Tombs','-',10,'optional','Ethereal Spellbinder',18312,-375.6,-37.7,-0.9,'pack met on the way to Shaffar in 3 of 5 runs'),
(148,557,0,'Mana-Tombs','-',11,'optional','Ethereal Spellbinder',18312,-261.0,7.9,16.8,'pack met on the way to Shaffar in 3 of 5 runs'),
(148,557,0,'Mana-Tombs','-',12,'boss','Nexus-Prince Shaffar',18344,-184.4,9.3,16.8,'');
