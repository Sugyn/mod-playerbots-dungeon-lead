-- Deadmines: the goblin foundry's packs are pulled one by one down the ramp before Gilnid.
-- Evidence: tools route_packs over 11 v2 runs - foundry goblins joined Gilnid (boss_add) in 7-9 of
-- 11 runs; wipe at Gilnid with 19-20 adds in campaigns 66490ea and earlier (PACK_IDENTITY).
-- Replaces the Deadmines route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=6;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(6,36,0,'Deadmines','-',1,'boss','Rhahk\'Zor',644,-192.9,-448.2,54.4,''),
(6,36,0,'Deadmines','-',2,'boss','Sneed\'s Shredder',642,-289.5,-513,49.7,'Sneed pops out after shredder dies (script)'),
(6,36,0,'Deadmines','-',3,'optional','Goblin Craftsman',1731,-183.2,-564.6,51.2,'goblin foundry: the ramp\'s goblins are pulled one by one down to Gilnid - they joined him in 7-9 of 11 runs'),
(6,36,0,'Deadmines','-',4,'optional','Goblin Craftsman',1731,-188.7,-600.5,36.6,'goblin foundry: ramp 2'),
(6,36,0,'Deadmines','-',5,'optional','Goblin Craftsman',1731,-203.3,-602.7,30.4,'goblin foundry: ramp 3'),
(6,36,0,'Deadmines','-',6,'optional','Goblin Engineer',622,-209.0,-590.7,21.0,'goblin foundry: bottom of the ramp'),
(6,36,0,'Deadmines','-',7,'optional','Goblin Craftsman',1731,-209.6,-568.1,21.0,'goblin foundry: floor west'),
(6,36,0,'Deadmines','-',8,'optional','Goblin Engineer',622,-196.8,-582.3,21.0,'goblin foundry: floor centre'),
(6,36,0,'Deadmines','-',9,'optional','Goblin Engineer',622,-208.0,-546.8,19.3,'goblin foundry: floor north'),
(6,36,0,'Deadmines','-',10,'optional','Goblin Craftsman',1731,-186.9,-553.6,19.3,'goblin foundry: by Gilnid north'),
(6,36,0,'Deadmines','-',11,'optional','Goblin Craftsman',1731,-198.9,-603.8,19.3,'goblin foundry: by Gilnid south'),
(6,36,0,'Deadmines','-',12,'boss','Gilnid',1763,-177.4,-574.5,19.3,''),
(6,36,0,'Deadmines','-',13,'use','Defias Gunpowder',17155,-106.409,-617.284,13.8495,'powder keg - opening it brings Defias Taskmasters'),
(6,36,0,'Deadmines','-',14,'use','Defias Cannon',16398,-107.562,-659.674,7.21211,'loaded with the gunpowder - blows the Iron Clad Door'),
(6,36,0,'Deadmines','-',15,'door','Iron door to Ironclad Cove',16397,-100.502,-668.771,7.41049,'Defias Gunpowder on cannon / rogue lockpick'),
(6,36,0,'Deadmines','-',16,'boss','Mr. Smite',646,-22.8,-797.3,20.4,'stealthed elites first'),
(6,36,0,'Deadmines','-',17,'boss','Captain Greenskin',647,-59.6,-820.1,41.6,'on ship'),
(6,36,0,'Deadmines','-',18,'boss','Edwin VanCleef',639,-87.4,-819.9,39.3,'cabin, adds'),
(6,36,0,'Deadmines','-',19,'optional','Cookie',645,-67.6,-853.7,17.1,'after VanCleef');
