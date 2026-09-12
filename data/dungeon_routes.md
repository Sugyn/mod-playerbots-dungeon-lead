# Dungeon routes (LFD 3.3.5a) — boss order per LFD entry

Legend: `spawn` = static spawn in `creature` table (entry + x/y/z resolved), `script` = template exists but is spawned by instance script/summon, `unknown` = no template match. Heroic LFD ids share the normal route (same map); `heroic only` steps apply to the heroic id only. Sources per dungeon are in `routes.tsv` comments.

## Vanilla

### Ragefire Chasm (LFD 4, map 389)

1. **Oggleflint** — entry 11517 @ (-147.5, 38.7, -38.8)
2. **Taragaman the Hungerer** — entry 11520 @ (-244.7, 150.1, -18.7)
3. **Jergosh the Invoker** — entry 11518 @ (-376.8, 209.2, -21.8)
4. **Bazzalan** — entry 11519 @ (-384.9, 146, 7.8) — upper walkway
### Wailing Caverns (LFD 1, map 43)

1. **Lady Anacondra** — entry 3671 @ (-67.8, 122.6, -92.8) — Screaming Gully
2. **Kresh** *(optional)* — entry 3653 @ (-64.4, 319.1, -106.7) — river turtle
3. **Verdan the Everliving** *(optional)* — entry 5775 @ (-81.9, 32.3, -31)
4. **Lord Serpentis** — entry 3673 @ (-120.2, -24.6, -28.6)
5. **Lord Pythas** — entry 3670 @ (36.8, -241.1, -79.5)
6. **Skum** *(optional)* — entry 3674 @ (-285.6, -313, -69.2)
7. **Lord Cobrahn** — entry 3669 @ (-151.1, 414.4, -72.6)
8. **Disciple of Naralex** **[event]** — entry 3678 @ (-135, 125.4, -78.1) — escort from entrance after 4 Fanglords -> spawns Mutanus
9. **Mutanus the Devourer** **[event]** `script` — entry 3654 (no static spawn) — script-spawned by escort
### Deadmines (LFD 6, map 36)

1. **Rhahk'Zor** — entry 644 @ (-192.9, -448.2, 54.4)
2. **Sneed's Shredder** — entry 642 @ (-289.5, -513, 49.7) — Sneed pops out after shredder dies (script)
3. **Gilnid** — entry 1763 @ (-177.4, -574.5, 19.3)
4. **Iron door to Ironclad Cove** **[door]** — Defias Gunpowder on cannon / rogue lockpick
5. **Mr. Smite** — entry 646 @ (-22.8, -797.3, 20.4) — stealthed elites first
6. **Captain Greenskin** — entry 647 @ (-59.6, -820.1, 41.6) — on ship
7. **Edwin VanCleef** — entry 639 @ (-87.4, -819.9, 39.3) — cabin, adds
8. **Cookie** *(optional)* — entry 645 @ (-67.6, -853.7, 17.1) — after VanCleef
### Shadowfang Keep (LFD 8, map 33)

1. **Rethilgore** — entry 3914 @ (-252.1, 2123.1, 81.2)
2. **Courtyard door** **[door]** — opened by Deathstalker Adamant / Sorcerer Ashcrombe after Rethilgore
3. **Razorclaw the Butcher** — entry 3886 @ (-202.6, 2258, 76.3)
4. **Baron Silverlaine** — entry 3887 @ (-275.3, 2297.4, 76.2)
5. **Commander Springvale** — entry 4278 @ (-222.6, 2259.4, 102.8)
6. **Odo the Blindwatcher** — entry 4279 @ (-236.7, 2146.1, 100.1)
7. **Fenrus the Devourer** — entry 4274 @ (-135.6, 2168.7, 128.8)
8. **Wolf Master Nandos** — entry 3927 @ (-120.7, 2162, 155.8) — kills opens Arugal's door
9. **Archmage Arugal** — entry 4275 @ (-76.8, 2152.4, 155.8)
### Blackfathom Deeps (LFD 10, map 48)

1. **Ghamoo-ra** — entry 4887 @ (-442.4, 211.8, -52.6)
2. **Lady Sarevess** *(optional)* — entry 4831 @ (-299.9, 413.8, -57.1) — side cavern
3. **Gelihast** — entry 6243 @ (-412.7, 40.9, -48.1)
4. **Lorgus Jett** — entry 12902 @ (-622.4, -86.9, -39.9) — 3 possible spawns
5. **Baron Aquanis** *(optional)* `script` — entry 12876 (no static spawn) — quest summon
6. **Old Serra'kis** *(optional)* — entry 4830 @ (-746.7, -169.4, -50.6) — underwater
7. **Twilight Lord Kelris** — entry 4832 @ (-818.8, -155.6, -25.8)
8. **Altar candles** **[event]** `unknown` — light 4 braziers -> waves -> spawns Aku'mai
9. **Aku'mai** **[event]** — entry 4829 @ (-848.4, -453.9, -33.9) — script-spawned
### Stormwind Stockade (LFD 12, map 34)

1. **Targorr the Dread** — entry 1696 @ (159.6, 1.3, -25.6) — variable cell
2. **Kam Deepfury** — entry 1666 @ (142.6, -71.9, -34.9) — variable cell
3. **Hamhock** — entry 1717 @ (105.5, -105.8, -35.1) — east/right wing
4. **Bazil Thredd** — entry 1716 @ (89.6, -136.9, -33.9) — beyond Hamhock, east wing
5. **Dextren Ward** — entry 1663 @ (166.8, 134.8, -33.9) — end of west/left wing
6. **Bruegal Ironknuckle** *(optional)* — entry 1720 @ (160.1, 45.7, -34.8) — rare
### Gnomeregan (LFD 14, map 90)

1. **Viscous Fallout** *(optional)* — entry 7079 @ (-471.4, 48.5, -208) — Hall of Gears
2. **Blastmaster Emi Shortfuse** **[event]** — entry 7998 @ (-514.9, -138.5, -152.4) — Grubbis event (script-spawned Grubbis)
3. **Grubbis** **[event]** `script` — entry 7361 (no static spawn)
4. **Electrocutioner 6000** — entry 6235 @ (-552, 502.9, -216.7)
5. **Crowd Pummeler 9-60** — entry 6229 @ (-889.1, 360.7, -272.5)
6. **Dark Iron Ambassador** *(optional)* — entry 6228 @ (-718.1, 565.2, -289.1) — rare
7. **Mekgineer Thermaplugg** — entry 7800 @ (-531.3, 670.2, -325.2) — buttons must be pressed during fight (scripted)
### Razorfen Kraul (LFD 16, map 47)

1. **Roogug** — entry 6168 @ (2143, 1581.6, 80.4)
2. **Aggem Thorncurse** — entry 4424 @ (2082.3, 1463.5, 73.2)
3. **Overlord Ramtusk** — entry 4420 @ (2203.1, 1640.1, 85.9)
4. **Death Speaker Jargba** — entry 4428 @ (2146.4, 1411.2, 74)
5. **Earthcaller Halmgar** *(optional)* — entry 4842 @ (2118.6, 1695.3, 80.3) — rare
6. **Agathelos the Raging** — entry 4422 @ (1994.4, 1976.4, 63.3)
7. **Blind Hunter** *(optional)* — entry 4425 @ (2200.8, 1978.2, 56.7) — rare
8. **Charlga Razorflank** — entry 4421 @ (2190.4, 1864.3, 79.1)
### Scarlet Monastery - Graveyard (LFD 18, map 189)

1. [Graveyard] **Interrogator Vishas** — entry 3983 @ (1786.6, 1124.4, 7.6)
2. [Graveyard] **Ironspine** *(optional)* — entry 6489 @ (1749.6, 1247.4, 18.2) — rare, Forlorn Cloister
3. [Graveyard] **Azshir the Sleepless** *(optional)* — entry 6490 @ (1851, 1392.8, 20.5) — rare
4. [Graveyard] **Fallen Champion** *(optional)* — entry 6488 @ (1756.2, 1345.4, 19.4) — rare
5. [Graveyard] **Bloodmage Thalnos** — entry 4543 @ (1820.3, 1416.7, -7.9)
### Scarlet Monastery - Library (LFD 165, map 189)

1. [Library] **Houndmaster Loksey** — entry 3974 @ (119.9, -260.9, 18.6)
2. [Library] **Arcanist Doan** — entry 6487 @ (148.3, -428.7, 18.5) — drops Scarlet Key
### Scarlet Monastery - Armory (LFD 163, map 189)

1. [Armory] **Herod** — entry 3975 @ (1965.1, -431.6, 6.3) — Scarlet Key door
### Scarlet Monastery - Cathedral (LFD 164, map 189)

1. [Cathedral] **High Inquisitor Fairbanks** *(optional)* — entry 4542 @ (1158.9, 1354.3, 30.4) — hidden room
2. [Cathedral] **Scarlet Commander Mograine** — entry 3976 @ (1153.9, 1398.4, 32.6) — clear whole cathedral first
3. [Cathedral] **High Inquisitor Whitemane** **[event]** — entry 3977 @ (1202.1, 1399.1, 29.1) — enters + resurrects Mograine (scripted)
### Razorfen Downs (LFD 20, map 129)

1. **Tuten'kash** **[event]** `unknown` — gong summons (script)
2. **Mordresh Fire Eye** — entry 7357 @ (2466.6, 671.4, 63.5) — Bone Pile
3. **Glutton** — entry 8567 @ (2468.7, 1006.8, 23.8)
4. **Ragglesnout** *(optional)* — entry 7354 @ (2364.8, 904.5, 28.8) — rare, 3 spots
5. **Amnennar the Coldbringer** — entry 7358 @ (2403.4, 960.9, 55.1)
6. **Plaguemaw the Rotting** *(optional)* `script` — entry 7356 (no static spawn) — quest event
### Uldaman (LFD 22, map 70)

1. **Baelog** — entry 6906 @ (-353, 117.2, -44.4) — with Eric "The Swift" + Olaf
2. **Revelosh** — entry 6910 @ (-225.6, 161.2, -44.5)
3. **Ironaya** *(optional)* — entry 7228 @ (-235.7, 309.6, -47.6) — door needs Staff of Prehistoria
4. **Ancient Stone Keeper** — entry 7206 @ (-38.4, 221.3, -48.4)
5. **Galgann Firehammer** — entry 7291 @ (-10.4, 414.7, -46.9)
6. **Grimlok** — entry 4854 @ (56.7, 455.3, -41)
7. **Archaedas** **[event]** — entry 2748 @ (104.3, 272.3, -51.7) — altar click starts encounter
### Zul'Farrak (LFD 24, map 209)

1. **Theka the Martyr** — entry 7272 @ (1778, 859.7, 8.9)
2. **Antu'sul** — entry 8127 @ (1815.8, 670.4, 15)
3. **Witch Doctor Zum'rah** — entry 7271 @ (1912.2, 1016.1, 11.6)
4. **Sandfury Executioner** — entry 7274 @ (1886.8, 1289.9, 46) — pyramid top
5. **Sergeant Bly** **[event]** — entry 7604 @ (1882.9, 1299.3, 48.4) — stairs event (Divino-matic Rod), spawns Nekrum + Sezz'ziz
6. **Nekrum Gutchewer** **[event]** `script` — entry 7796 (no static spawn)
7. **Shadowpriest Sezz'ziz** **[event]** `unknown`
8. **Chief Ukorz Sandscalp** — entry 7267 @ (1727.5, 1017.3, 54.9) — with Ruuzlu
9. **Hydromancer Velratha** *(optional)* — entry 7795 @ (1698.2, 1210.7, 9.4)
10. **Gahz'rilla** *(optional)* `unknown` — Mallet summon
### Maraudon - Orange Crystals (LFD 26, map 349)

1. [Foulspore Cavern] **Noxxion** — entry 13282 @ (1130.4, -191.3, -80) — Orange Crystals
2. [Foulspore Cavern] **Razorlash** — entry 12258 @ (978.9, -10.2, -62.5)
### Maraudon - Purple Crystals (LFD 272, map 349)

1. [Wicked Grotto] **Lord Vyletongue** — entry 12236 @ (748.9, -219.6, -47.7) — Purple Crystals
### Maraudon - Pristine Waters (LFD 273, map 349)

1. [Earth Song Falls] **Celebras the Cursed** *(optional)* — entry 12225 @ (726.1, 78, -86.6) — optional/shared, not assigned to a specific 3.3.5 LFD wing (Poison Falls, between outer wings and inner)
2. [Earth Song Falls] **Tinkerer Gizlock** — entry 13601 @ (134.9, -313.7, -173.6) — Earth Song Falls in 3.3.5a (moved to Wicked Grotto only in 4.0.3a)
3. [Earth Song Falls] **Landslide** — entry 12203 @ (356.7, -185.5, -59.8)
4. [Earth Song Falls] **Rotgrip** *(optional)* — entry 13596 @ (42.1, -66, -199.6) — lake
5. [Earth Song Falls] **Princess Theradras** — entry 12201 @ (27.9, 83.2, -124.5)
### Sunken Temple (LFD 28, map 109)

1. **Loro** — entry 5714 @ (-466.7, 24.4, -66.8) — Atal'ai Defender 1/6 — all six required, kill order arbitrary; listed order follows a clockwise route
2. **Gasher** — entry 5713 @ (-528, 59.5, -66.7) — Atal'ai Defender 2/6
3. **Zolo** — entry 5712 @ (-528.6, 130.2, -66.8) — Atal'ai Defender 3/6
4. **Mijan** — entry 5717 @ (-406.2, 131.1, -66.9) — Atal'ai Defender 4/6
5. **Zul'Lor** — entry 5716 @ (-467.4, 166, -66.7) — Atal'ai Defender 5/6
6. **Hukku** — entry 5715 @ (-405.5, 60.5, -67.1) — Atal'ai Defender 6/6 -> Jammal'an shield drops
7. **Jammal'an the Prophet** — entry 5710 @ (-425.9, -86.1, -88.2) — with Ogom the Wretched
8. **Dreamscythe** *(optional)* — entry 5721 @ (-453.5, 137.2, -90.8)
9. **Weaver** *(optional)* — entry 5720 @ (-458.8, 127.7, -91.6)
10. **Morphaz** *(optional)* — entry 5719 @ (-667.6, 103.1, -90.8)
11. **Hazzas** *(optional)* — entry 5722 @ (-667.4, 80.8, -90.8)
12. **Shade of Eranikus** — entry 5709 @ (-658.4, -35.8, -90.8)
13. **Atal'alarion** *(optional)* — entry 8580 @ (-480.4, 96.6, -189.7) — statue puzzle, basement
14. **Avatar of Hakkar** *(optional)* `script` — entry 8443 (no static spawn) — quest event
### Blackrock Depths - Prison (LFD 30, map 230)

1. [Prison] **High Interrogator Gerstahn** — entry 9018 @ (310.6, -146.3, -70.3) — LFD completion boss for Prison
2. [Prison] **Lord Roccor** — entry 9025 @ (615.5, -267.4, -83.6)
3. [Prison] **Houndmaster Grebmar** — entry 9319 @ (607.7, -174.8, -84.5)
4. [Prison] **Ring of Law** **[event]** `unknown` — arena, random boss (Anub'shiah/Eviscerator/Gorosh/Grizzle/Hedrum/Ok'thor)
5. [Prison] **Fineous Darkvire** *(optional)* — entry 9056 @ (963.3, -343.7, -71.7) — Dark Iron Legacy / Shadowforge Key
6. [Prison] **Pyromancer Loregrain** — entry 9024 @ (530.2, -243.9, -43)
7. [Prison] **Bael'Gar** — entry 9016 @ (702.4, 184.5, -72)
8. [Prison] **Lord Incendius** — entry 9017 @ (893.5, -267.1, -71.9)
9. [Prison] **Warder Stilgiss** *(optional)* — entry 9041 @ (823.4, -342.3, -50.1) — with Verek
10. [Prison] **Watchman Doomgrip** *(optional)* `script` — entry 9476 (no static spawn) — event: spawns when relic coffers are opened (12 keys)
### Blackrock Depths - Upper City (LFD 276, map 230)

1. [Upper] **General Angerforge** — entry 9033 @ (652.4, 21.4, -60)
2. [Upper] **Golem Lord Argelmach** — entry 8983 @ (846.8, 16.3, -53.6)
3. [Upper] **Ribbly Screwspigot** *(optional)* — entry 9543 @ (878.5, -167.7, -49.7) — Grim Guzzler
4. [Upper] **Phalanx** *(optional)* — entry 9502 @ (869, -225, -43.7) — Grim Guzzler
5. [Upper] **Hurley Blackbreath** *(optional)* — entry 9537 @ (878.1, -153.1, -49.8) — Grim Guzzler
6. [Upper] **Grim Guzzler back door** **[door]** — Plugger bar fight / Rocknot event
7. [Upper] **Ambassador Flamelash** — entry 9156 @ (1009.8, -239, -61.3)
8. [Upper] **Panzor the Invincible** *(optional)* — entry 8923 @ (1135.5, -163, -74.9) — rare
9. [Upper] **Doom'rel** **[event]** — entry 9039 @ (1281.1, -282.2, -78.1) — Chest of the Seven (7 dwarves sequential)
10. [Upper] **Magmus** — entry 9938 @ (1380.7, -659.3, -92) — Lyceum gauntlet before
11. [Upper] **Emperor Dagran Thaurissan** — entry 9019 @ (1380.2, -831.6, -87.6) — LFD completion boss for Upper City; with Princess Moira Bronzebeard; High Priestess of Thaurissan spawns
### Lower Blackrock Spire (LFD 32, map 229)

1. **Highlord Omokk** — entry 9196 @ (-22.8, -300.7, 31.8)
2. **Shadow Hunter Vosh'gajin** — entry 9236 @ (-121.2, -482.2, 24.7)
3. **War Master Voone** — entry 9237 @ (-17, -459.1, -18.6)
4. **Mother Smolderweb** — entry 10596 @ (-135.5, -565.8, 10.2)
5. **Halycon** *(optional)* — entry 10220 @ (-193.9, -338.1, 64.5)
6. **Quartermaster Zigris** — entry 9736 @ (-190.5, -475.6, 87.4)
7. **Gizrul the Slavener** *(optional)* `script` — entry 10268 (no static spawn) — spawns after Halycon
8. **Overlord Wyrmthalak** — entry 9568 @ (-22.6, -486.2, 90.8)
### Scholomance (LFD 2, map 289)

1. **Kirtonos the Herald** *(optional)* `script` — entry 10506 (no static spawn) — summon (Blood of Innocents)
2. **Jandice Barov** — entry 10503 @ (268.2, 73.6, 95.9)
3. **Rattlegore** — entry 11622 @ (137.1, 171.7, 96) — drops Viewing Room Key
4. **Viewing Room door** **[door]** — Rattlegore key
5. **Marduk Blackpool** — entry 10433 @ (150.4, 116.2, 104.7)
6. **Vectus** — entry 10432 @ (143.5, 99.1, 104.7)
7. **The Ravenian** — entry 10507 @ (103.3, -1.7, 75.2)
8. **Lord Alexei Barov** — entry 10504 @ (178.7, -91, 70.9)
9. **Instructor Malicia** — entry 10505 @ (86.7, -2, 85.3)
10. **Doctor Theolen Krastinov** — entry 11261 @ (182.2, -95.4, 85.3)
11. **Lorekeeper Polkelt** — entry 10901 @ (274.9, 1.3, 85.3)
12. **Lady Illucia Barov** — entry 10502 @ (266, 0.9, 75.3)
13. **Ras Frostwhisper** — entry 10508 @ (-25.1, 141.3, 83.9)
14. **Darkmaster Gandling** **[event]** — entry 1853 @ (180.8, -5.4, 75.6) — spawns after all six
### Stratholme - Main Gate (LFD 40, map 329)

1. [Live] **Timmy the Cruel** *(optional)* — entry 10808 @ (3614.7, -3187.6, 131.4) — script-spawned in Market Row
2. [Live] **The Unforgiven** *(optional)* — entry 10516 @ (3719.8, -3426.2, 131.8)
3. [Live] **Postmaster Malown** *(optional)* `script` — entry 11143 (no static spawn) — summon via postboxes
4. [Live] **Scarlet Bastion gate** **[door]** — Scarlet Key / Crusaders' Square
5. [Live] **Cannon Master Willey** — entry 10997 @ (3573.6, -2937.3, 125.1)
6. [Live] **Crimson Hammersmith** *(optional)* `script` — entry 11120 (no static spawn) — summon
7. [Live] **Archivist Galford** — entry 10811 @ (3456, -3103.4, 136.5)
8. [Live] **Grand Crusader Dathrohan** — entry 10812 @ (3415.8, -3044.5, 136.8) — -> Balnazzar at 50%
### Stratholme - Service Entrance (LFD 274, map 329)

1. [Undead] **Magistrate Barthilas** **[event]** — entry 10435 @ (3663.2, -3619.1, 138) — flees on first contact
2. [Undead] **Stonespine** *(optional)* — entry 10809 @ (4058.9, -3530.3, 122.2) — rare
3. [Undead] **Baroness Anastari** — entry 10436 @ (3855.3, -3715.8, 148.2) — ziggurat 1
4. [Undead] **Nerub'enkan** — entry 10437 @ (3855, -3528.8, 144.3) — ziggurat 2
5. [Undead] **Maleki the Pallid** — entry 10438 @ (4035.8, -3647.8, 135.7) — ziggurat 3 -> Slaughter Square gate opens
6. [Undead] **Magistrate Barthilas** — entry 10435 @ (3663.2, -3619.1, 138) — rematch
7. [Undead] **Ramstein the Gorger** **[event]** `script` — entry 10439 (no static spawn) — after abominations
8. [Undead] **Baron Rivendare** — entry 10440 @ (4035.8, -3336.3, 115.1) — 45-min timer for quest only
### Dire Maul - East (LFD 34, map 429)

1. [East] **Pusillin** **[event]** — entry 14354 @ (86.2, -197.9, -4.1) — chase, Crescent Key
2. [East] **Lethtendris** *(optional)* — entry 14327 @ (-5.5, -441.1, 16.4) — with Pimgib
3. [East] **Hydrospawn** *(optional)* — entry 13280 @ (4.6, -438.4, -60)
4. [East] **Zevrim Thornhoof** *(optional)* — entry 11490 @ (-35, -448, -37.9)
5. [East] **Alzzin door** **[door]** — Old Ironbark opens after Zevrim
6. [East] **Alzzin the Wildshaper** — entry 11492 @ (274.8, -427.3, -120)
### Dire Maul - West (LFD 36, map 429)

1. [West] **Crescent Key door** **[door]**
2. [West] **Tendris Warpwood** *(optional)* — entry 11489 @ (14.4, 475.8, -23.3)
3. [West] **Tsu'zee** *(optional)* — entry 11467 @ (128.6, 561.8, -4.3) — rare
4. [West] **Magister Kalendris** *(optional)* — entry 11487 @ (33.1, 575.6, -4.3)
5. [West] **Illyanna Ravenoak** *(optional)* — entry 11488 @ (-14.6, 542.1, 28.6) — with Ferra
6. [West] **Immol'thar** *(optional)* — entry 11496 @ (-38.1, 812.4, -29.5) — 5 pylons
7. [West] **Prince Tortheldrin** — entry 11486 @ (132.6, 625.9, -48.4)
### Dire Maul - North (LFD 38, map 429)

1. [North] **Crescent Key door** **[door]**
2. [North] **Guard Mol'dar** *(optional)* — entry 14326 @ (410.7, -3.2, -24.6)
3. [North] **Stomper Kreeg** *(optional)* — entry 14322 @ (491.2, 97.4, -2.5)
4. [North] **Guard Fengus** *(optional)* — entry 14321 @ (356.8, 258.3, 11.7)
5. [North] **Guard Slip'kik** *(optional)* — entry 14323 @ (550.4, 533.7, -25.3)
6. [North] **Captain Kromcrush** *(optional)* — entry 14325 @ (627.6, 481.7, 29.5)
7. [North] **Cho'Rush the Observer** *(optional)* — entry 14324 @ (834, 489.5, 37.4)
8. [North] **King Gordok** — entry 11501 @ (828.1, 480.8, 37.3)
### The Headless Horseman (LFD 285, map 189)

1. **Headless Horseman** **[event]** `unknown` — Hallow's End only
### Coren Direbrew (LFD 287, map 230)

1. **Coren Direbrew** **[event]** — entry 23872 @ (891.8, -129.2, -49.7) — Brewfest only
## The Burning Crusade

### The Frost Lord Ahune (LFD 286, map 547, heroic LFD 184)

1. **Lord Ahune** **[event]** `unknown` — Midsummer only
## Vanilla

### The Crown Chemical Co. (LFD 288, map 33)

1. **Apothecary Hummel** **[event]** — entry 36296 @ (-208.1, 2217.4, 79.8) — Love is in the Air only
## The Burning Crusade

### Hellfire Ramparts (LFD 136, map 543, heroic LFD 188)

1. **Watchkeeper Gargolmar** — entry 17306 @ (-1187.2, 1530.5, 68.5)
2. **Vazruden the Herald** — entry 17307 @ (-1378.5, 1698.2, 104.1) — Nazan lands after Vazruden dismounts (script)
3. **Omor the Unscarred** — entry 17308 @ (-1122.3, 1718.4, 89.4)
### Blood Furnace (LFD 137, map 542, heroic LFD 187)

1. **The Maker** — entry 17381 @ (327.2, 137.8, 9.6)
2. **Broggok** **[event]** — entry 17380 @ (455.3, -1.8, 9.6) — cell waves lever
3. **Keli'dan the Breaker** — entry 17377 @ (326.5, -86, -24.6) — channelers first
### Shattered Halls (LFD 138, map 540, heroic LFD 189)

1. **Grand Warlock Nethekurse** — entry 16807 @ (172.7, 289.6, -8.1)
2. **Gauntlet of Flame** **[event]** `unknown` — archers gauntlet
4. **Warbringer O'mrogg** — entry 16809 @ (375.1, 57.6, -7.2)
5. **Warchief Kargath Bladefist** — entry 16808 @ (231.2, -83.6, 5)
### Slave Pens (LFD 140, map 547, heroic LFD 184)

1. **Mennu the Betrayer** — entry 17941 @ (49.5, -380.2, 3)
2. **Rokmar the Crackler** — entry 17991 @ (18.3, -448.4, 3.1)
3. **Quagmirran** — entry 17942 @ (-281.1, -667.1, 9.4)
### Underbog (LFD 146, map 546, heroic LFD 186)

1. **Hungarfen** — entry 17770 @ (-121.3, -388.6, 36.9)
2. **Ghaz'an** — entry 18105 @ (193.7, -425, 43.5)
3. **Swamplord Musel'ek** — entry 17826 @ (288.6, -121.8, 29.7) — with Claw
4. **The Black Stalker** — entry 17882 @ (143.4, 9.1, 27.6)
### The Steamvault (LFD 147, map 545, heroic LFD 185)

1. **Hydromancer Thespia** — entry 17797 @ (88.4, -316.1, -7.8)
2. **Mekgineer Steamrigger** — entry 17796 @ (-330.1, -121.5, -8)
3. **Control panels** **[door]** — 2 panels behind first two bosses
4. **Warlord Kalithresh** — entry 17798 @ (-95.4, -552, 8.3)
### Mana-Tombs (LFD 148, map 557, heroic LFD 179)

1. **Pandemonius** — entry 18341 @ (-68, -118.9, -1.2)
2. **Tavarok** — entry 18343 @ (-321.7, -222.2, -0.8)
4. **Nexus-Prince Shaffar** — entry 18344 @ (-184.4, 9.3, 16.8)
### Auchenai Crypts (LFD 149, map 558, heroic LFD 178)

1. **Shirrak the Dead Watcher** — entry 18371 @ (-50.9, -163.1, 26.4)
2. **Exarch Maladaar** — entry 18373 @ (68.1, -387.8, 26.6)
### Sethekk Halls (LFD 150, map 556, heroic LFD 180)

1. **Darkweaver Syth** — entry 18472 @ (-144.8, 173.6, 1.8)
3. **Talon King Ikiss** — entry 18473 @ (44.7, 287, 25.2)
### Shadow Labyrinth (LFD 151, map 555, heroic LFD 181)

1. **Ambassador Hellmaw** **[event]** — entry 18731 @ (-156.7, 5, 8.2) — freed after banishers
2. **Blackheart the Inciter** — entry 18667 @ (-328.2, -39.1, 12.7)
3. **Grandmaster Vorpil** — entry 18732 @ (-253.5, -263.6, 17.2)
4. **Murmur** — entry 18708 @ (-157.9, -497.3, 15.9)
### The Mechanar (LFD 172, map 554, heroic LFD 192)

1. **Gatewatcher Gyro-Kill** — entry 19218 @ (85.5, 20.2, 15)
2. **Nethermancer Sepethrea** *(optional)* — entry 19221 @ (326.5, 13.2, 27.9)
3. **Mechano-Lord Capacitus** *(optional)* — entry 19219 @ (208.2, -13, -2.1)
4. **Gatewatcher Iron-Hand** — entry 19710 @ (181.9, -77.1, 0)
5. **Elevator** **[door]** — after both gatewatchers
6. **Pathaleon the Calculator** — entry 19220 @ (139.5, 149.3, 25.7)
### The Botanica (LFD 173, map 553, heroic LFD 191)

1. **Commander Sarannis** *(optional)* — entry 17976 @ (151, 296, -4.6)
2. **Warp Splinter** — entry 17977 @ (63.8, 391.9, -27.9)
3. **Laj** *(optional)* — entry 17980 @ (-204.1, 391.2, -11.2)
4. **Thorngrin the Tender** *(optional)* — entry 17978 @ (4.9, 596.6, -15.1)
5. **High Botanist Freywinn** *(optional)* — entry 17975 @ (116.3, 455.6, -4.9)
### The Arcatraz (LFD 174, map 552, heroic LFD 190)

1. **Zereketh the Unbound** — entry 20870 @ (273.6, -123, -10)
2. **Wrath-Scryer Soccothrates** — entry 20886 @ (136.2, 168.3, 22.5)
3. **Dalliah the Doomsayer** — entry 20885 @ (137.2, 128.5, 22.5)
4. **Warden Mellichar** **[event]** — entry 20904 @ (445.8, -169, 43.6) — cell event -> Harbinger Skyriss
5. **Harbinger Skyriss** **[event]** `script` — entry 20912 (no static spawn) — script-spawned
### The Escape From Durnholde (LFD 170, map 560, heroic LFD 183)

1. **Escape from Durnholde** **[UNSUPPORTED]** — event dungeon: barrels -> Lieutenant Drake -> Captain Skarloc -> Thrall escort -> Epoch Hunter
### The Black Morass (LFD 171, map 269, heroic LFD 182)

1. **The Black Morass** **[UNSUPPORTED]** — event dungeon: 18 waves, Chrono Lord Deja(6) / Temporus(12) / Aeonus(18), protect Medivh
### Magisters' Terrace (LFD 198, map 585, heroic LFD 201)

1. **Selin Fireheart** — entry 24723 @ (242.1, 0.3, 1.8)
2. **Vexallus** — entry 24744 @ (231.4, -214.3, -6.3)
3. **Priestess Delrissa** — entry 24560 @ (126.9, 19.2, -19.9) — council
4. **Kael'thas Sunstrider** — entry 24664 @ (148.5, 187, -16.6)
## Wrath of the Lich King

### Utgarde Keep (LFD 202, map 574, heroic LFD 242)

1. **Prince Keleseth** — entry 23953 @ (193.1, 197.5, 40.8)
2. **Skarvald the Constructor** — entry 24200 @ (109.5, -33.7, 118.9) — with Dalronn the Controller
3. **Ingvar the Plunderer** — entry 23954 @ (242.7, -333.6, 180.6)
### Utgarde Pinnacle (LFD 203, map 575, heroic LFD 205)

1. **Svala Sorrowgrave** **[event]** `script` — entry 26668 (no static spawn) — ritual (script-spawned)
2. **Gortok Palehoof** **[event]** — entry 26687 @ (320.8, -453.1, 104.8) — animal gauntlet
3. **Skadi the Ruthless** **[event]** — entry 26693 @ (343, -507.3, 104.6) — gauntlet + harpoons
4. **King Ymiron** — entry 26861 @ (392.8, -286.8, 109.3)
### The Nexus (LFD 225, map 576, heroic LFD 226)

2. **Grand Magus Telestra** — entry 26731 @ (494.7, 89.1, -16)
3. **Anomalus** — entry 26763 @ (637.7, -289.1, -9.1)
4. **Ormorok the Tree-Shaper** — entry 26794 @ (265, -225.4, -9)
5. **Keristrasza** — entry 26723 @ (301.5, -5.5, -15.5) — freed after previous three
### The Oculus (LFD 206, map 578, heroic LFD 211)

1. **Drakos the Interrogator** — entry 27654 @ (947.8, 1045.8, 360.1)
2. **The Oculus** **[UNSUPPORTED]** — vehicle dungeon after Drakos: Varos Cloudstrider -> Mage-Lord Urom -> Ley-Guardian Eregos on drakes
### The Culling of Stratholme (LFD 209, map 595, heroic LFD 210)

1. **The Culling of Stratholme** **[UNSUPPORTED]** — event dungeon: Arthas escort, Meathook -> Salramm -> Chrono-Lord Epoch -> (Infinite Corruptor H) -> Mal'Ganis
### Halls of Stone (LFD 208, map 599, heroic LFD 213)

1. **Krystallus** — entry 27977 @ (1008.6, 759.9, 208.7)
2. **Maiden of Grief** — entry 27975 @ (842.6, 666, 190.1)
3. **Brann Bronzebeard** **[event]** — entry 28070 @ (1077.4, 474.2, 207.8) — Tribunal of Ages escort, opens Sjonnir door
4. **Sjonnir The Ironshaper** — entry 27978 @ (1295.2, 667.2, 189.7)
### Drak'Tharon Keep (LFD 214, map 600, heroic LFD 215)

1. **Trollgore** — entry 26630 @ (-266.2, -660.1, 26.5)
2. **Novos the Summoner** **[event]** — entry 26631 @ (-379.3, -737.7, 27.3) — waves
3. **King Dred** — entry 27483 @ (-544.9, -697, 30.3)
4. **The Prophet Tharon'ja** — entry 26632 @ (-236.8, -675.4, 131.9)
### Azjol-Nerub (LFD 204, map 601, heroic LFD 241)

1. **Krik'thir the Gatewatcher** — entry 28684 @ (529.6, 646.2, 777.4) — watchers Gashra/Narjil/Silthik first
2. **Hadronox** *(optional)* — entry 28921 @ (522.5, 544.9, 674.7) — web climb
3. **Anub'arak** — entry 29120 @ (551, 248.3, 224)
### Halls of Lightning (LFD 207, map 602, heroic LFD 212)

1. **General Bjarngrim** — entry 28586 @ (1262, -26.9, 33.5) — patrols
2. **Volkhan** — entry 28587 @ (1332.4, -102.1, 56.8)
3. **Ionar** — entry 28546 @ (1082, -261.8, 61.3)
4. **Loken** — entry 28923 @ (1186.5, 33.8, 60.8)
### Gundrak (LFD 216, map 604, heroic LFD 217)

1. **Slad'ran** — entry 29304 @ (1775.1, 675, 129.3)
2. **Moorabi** — entry 29305 @ (1772.5, 809.5, 129.3)
3. **Drakkari Colossus** — entry 29307 @ (1673, 743.5, 143.3)
4. **Altar bridge** **[door]** — 3 altars -> bridge to Gal'darah
5. **Gal'darah** — entry 29306 @ (1914.8, 743.7, 136.6)
### Violet Hold (LFD 220, map 608, heroic LFD 221)

1. **The Violet Hold** **[UNSUPPORTED]** — event dungeon: 18 waves, 2 random of Erekem/Moragg/Ichoron/Xevozz/Lavanthor/Zuramat, Cyanigosa
### Ahn'kahet: The Old Kingdom (LFD 218, map 619, heroic LFD 219)

1. **Elder Nadox** *(optional)* — entry 29309 @ (679.9, -905.5, 25.7)
2. **Jedoga Shadowseeker** *(optional)* — entry 29310 @ (372.3, -705.3, -0.6)
3. **Prince Taldaram** — entry 29308 @ (528.7, -846, 42) — 2 spheres open door
5. **Herald Volazj** — entry 29311 @ (519.9, -441.8, 26.4)
### The Forge of Souls (LFD 251, map 632, heroic LFD 252)

1. **Bronjahm** — entry 36497 @ (5297.3, 2506.5, 686.2)
2. **Devourer of Souls** — entry 36502 @ (5661.8, 2507.4, 708.9)
### Trial of the Champion (LFD 245, map 650, heroic LFD 249)

1. **Trial of the Champion** **[UNSUPPORTED]** — event dungeon: jousting Grand Champions -> Eadric/Paletress -> The Black Knight
### Pit of Saron (LFD 253, map 658, heroic LFD 254)

1. **Forgemaster Garfrost** — entry 36494 @ (712.1, -215.7, 527.1)
2. **Ick** — entry 36476 @ (852.8, 123.5, 510) — with Krick
3. **Tunnel gauntlet** **[event]** `unknown` — cave-in, run through
4. **Scourgelord Tyrannus** — entry 36794 @ (522.3, 226.4, 548) — with Rimefang
### Halls of Reflection (LFD 255, map 668, heroic LFD 256)

1. **Halls of Reflection** **[UNSUPPORTED]** — event dungeon: 10 waves, Falric(5) / Marwyn(10), Lich King escape
## The Burning Crusade

3. **Yor** *(heroic only)* — summon, quest
2. **Anzu** *(heroic only)* — summon, druid quest
3. **Blood Guard Porung** *(heroic only)*
## Wrath of the Lich King

6. **Eck the Ferocious** *(heroic only)*
4. **Amanitar** *(heroic only)*
1. **Commander Kolurg** *(heroic only)* — Horde sees Kolurg / Alliance sees Commander Stoutbeard
