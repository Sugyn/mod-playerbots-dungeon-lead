-- Door rows that had no game object and no position (validate_routes.py: 11 errors, DL-008).
-- Three now name their door (entry from the world DB, lock checked in Lock.dbc against the key item):
--   BRD Grim Guzzler Bar Door 170571 (lock 739, script-opened), Stratholme The Bastion Door 175967
--   (lock 299 = The Scarlet Key), Dire Maul North door 179549 (lock 1562 = Crescent Key).
-- The other eight cannot be opened as a door by the leader (access panels, elevator, altars) or have
-- no verified door object; they are marked `skip` (never walked, optional) instead of a mandatory
-- door that would stop every run there.
-- Apply to acore_playerbots on an existing install; a fresh install gets this from
-- playerbots_dungeon_route.sql.
UPDATE `playerbots_dungeon_route` SET `entry`=170571, `x`=870.693, `y`=-228.936, `z`=-43.7509, `note`='Plugger bar fight / Rocknot event; Bar Door; lock 739 = Grim Guzzler Key (11602); opened by the Plugger bar-fight script'
  WHERE `lfg_id`=276 AND `step`=6 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `entry`=175967, `x`=3645.56, `y`=-3136.76, `z`=134.76, `note`='Scarlet Key / Crusaders\' Square; The Bastion Door; lock 299 = The Scarlet Key (7146)'
  WHERE `lfg_id`=40 AND `step`=4 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `entry`=179549, `x`=351.568, `y`=88.6734, `z`=-36.393, `note`='Door 179549; lock 1562 = Crescent Key (18249); the only Crescent Key door within 390 yd of this wing\'s route'
  WHERE `lfg_id`=38 AND `step`=1 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='Old Ironbark opens after Zevrim; unsupported: Alzzin door; opened by an Old Ironbark gossip event; no verified door object'
  WHERE `lfg_id`=34 AND `step`=5 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='unsupported: Crescent Key door - two candidates (177221; 179550) in this wing; which one is step 1 is unverified'
  WHERE `lfg_id`=36 AND `step`=1 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='2 panels behind first two bosses; unsupported: 2 access panels (184125; 184126) open the Main Chambers Door by script; not a door the leader can open'
  WHERE `lfg_id`=147 AND `step`=3 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='2 panels behind first two bosses; unsupported: 2 access panels (184125; 184126) open the Main Chambers Door by script; not a door the leader can open'
  WHERE `lfg_id`=185 AND `step`=3 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='after both gatewatchers; unsupported: elevator (Doodad_FactoryElevator01; a transport) - no door object to open'
  WHERE `lfg_id`=172 AND `step`=5 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='after both gatewatchers; unsupported: elevator (Doodad_FactoryElevator01; a transport) - no door object to open'
  WHERE `lfg_id`=192 AND `step`=5 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='3 altars -> bridge to Gal\'darah; unsupported: 3 altars (192518-192520) must be used to raise the bridge (193188); altars are not walked'
  WHERE `lfg_id`=216 AND `step`=4 AND `kind`='door';
UPDATE `playerbots_dungeon_route` SET `kind`='skip', `note`='3 altars -> bridge to Gal\'darah; unsupported: 3 altars (192518-192520) must be used to raise the bridge (193188); altars are not walked'
  WHERE `lfg_id`=217 AND `step`=4 AND `kind`='door';
