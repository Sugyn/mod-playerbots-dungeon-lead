-- Deadmines: the Iron Clad Door is blown by the Defias Cannon - the gunpowder from its keg loads
-- and fires it. Both are use steps before the door. Replaces the Deadmines route.
DELETE FROM `playerbots_dungeon_route` WHERE `lfg_id`=6;
INSERT INTO `playerbots_dungeon_route` (`lfg_id`,`map_id`,`difficulty`,`name`,`wing`,`step`,`kind`,`boss`,`entry`,`x`,`y`,`z`,`note`) VALUES
(6,36,0,'Deadmines','-',1,'boss','Rhahk\'Zor',644,-192.9,-448.2,54.4,''),
(6,36,0,'Deadmines','-',2,'boss','Sneed\'s Shredder',642,-289.5,-513,49.7,'Sneed pops out after shredder dies (script)'),
(6,36,0,'Deadmines','-',3,'boss','Gilnid',1763,-177.4,-574.5,19.3,''),
(6,36,0,'Deadmines','-',4,'use','Defias Gunpowder',17155,-106.409,-617.284,13.8495,'powder keg - opening it brings Defias Taskmasters'),
(6,36,0,'Deadmines','-',5,'use','Defias Cannon',16398,-107.562,-659.674,7.21211,'loaded with the gunpowder - blows the Iron Clad Door'),
(6,36,0,'Deadmines','-',6,'door','Iron door to Ironclad Cove',16397,-100.502,-668.771,7.41049,'Defias Gunpowder on cannon / rogue lockpick'),
(6,36,0,'Deadmines','-',7,'boss','Mr. Smite',646,-22.8,-797.3,20.4,'stealthed elites first'),
(6,36,0,'Deadmines','-',8,'boss','Captain Greenskin',647,-59.6,-820.1,41.6,'on ship'),
(6,36,0,'Deadmines','-',9,'boss','Edwin VanCleef',639,-87.4,-819.9,39.3,'cabin, adds'),
(6,36,0,'Deadmines','-',10,'optional','Cookie',645,-67.6,-853.7,17.1,'after VanCleef');
