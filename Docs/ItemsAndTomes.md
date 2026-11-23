# Soul Quota - Items and Tomes (Nov 7 2026 items/tomes pass)

Jack: "The items and tomes system in Megabonk is very balanced. I can't tell if this game is yet."
Short answer: **it wasn't.** This doc records the audit, the new Megabonk-style split, the stat caps and the balance pass.
Numbers come from `Scripts/balance_model.py` (sections 9-11 are new):

```powershell
cd E:\Projects\SoulQuota
python Scripts\balance_model.py
```

## 1. Megabonk, for reference (Docs/MegabonkAnalysis.md sections 2-4)

- **Weapons:** limited slots, levelled by level-up cards. No evolutions.
- **Tomes:** limited slots, passive stat books, a fixed +X per level, levelled by level-up cards.
- **Items:** from chests, shrines and merchants. No slots, they stack, many proc on hit.
- **Rarity** raises the rolled values. **Luck** shifts the rarity.
- Base slot counts are not confirmed by any source we found (extra slots are bought after quest milestones).

## 2. Audit: what we had before this pass

### Where power came from

| Source | What it gave | Slots | Stacking |
|---|---|---|---|
| Level-up cards | 24 weapons (levels 1-8), 22 relics (levels 1-5 or 1-3), evolutions | 6 weapons + 6 relics | Relic level x rarity multiplier (1.0 / 1.4 / 1.9 / 2.6) |
| Chests | A level of one of the **same 22 relics** (in the same 6 slots); a weapon level when every relic was maxed | shared with level-ups | as above |
| Lost Property shrine | One of the same relics | shared | as above |
| Performance Review shrines | A permanent stat raise (1 of 3), any number per run | none | linear, no cap (only the hard clamps below) |
| Death's Office | Heal, a level-up card, +15 carry, a revive | - | - |
| Bonds | A stat while two reapers hold a relic pair within 900 cm | - | flat |

### Top problems

1. **No real split.** "Relic" meant both the level-up stat card and the chest reward. Chests just levelled the same 6 relics, so
   about 19 chest rolls a run poured into crit and cooldown relics. That is why the economy pass had to add
   `EconomyBossHealth 1.25 / 1.8 / 2.0`: chests doubled DPS by night 3.
2. **Too many slots for the picks.** 6 weapons x 8 + 6 relics x 5 = 78 picks to max, against about 48 level-ups. There was no
   "slots are full" moment, so builds never had to commit.
3. **Dominant and dead picks** (value of one Common pick at night 2, as a multiple of the median):
   Hourglass **2.11x** (linear cooldown `1 - sum`: each level was worth more than the last, up to a 0.3 floor), Moth Lure 1.70x,
   Red Ink 1.55x, Ledger 1.32x. Dead or weak: Thorned Shroud 0.30x (flat thorns), Split Soul 0.50x, Rabbit's Foot 0.53x,
   Golden Key 0.54x, Tea Urn 0.61x, Embalmer's Patience 0.64x, Embalming 0.72x, Spare Parts 0.73x, Grave Dust 0.74x.
4. **Weapons:** max-level scores ranged 0.41 (Frost Ring) to 1.84 (Rubber Stamp); 16 of 24 were outside 0.8-1.2.
5. **Evolutions:** spikes from **-65%** (Soul Conduit was weaker than the weapon it replaced) to **+126%** (Poltergeist Flood);
   20 of 24 outside +40-70%.
6. **Caps.** Crit chance was capped at 100% but chance past the cap was wasted. Cooldown was linear with a 0.3 floor. Armor was
   linear up to 80% and evasion linear up to 50%. Together that allowed damage taken x0.1 (10x effective HP). Area, luck and
   duration had no cap. Shrine raises stacked linearly with no limit.
7. **Exploit:** after evolving a weapon you could take the base weapon again as a new weapon (its id was no longer owned).
8. **Confusing text:** cards said "-6% weapon cooldowns" and "5% less damage" while the real effect stacked linearly into a
   clamp. Red Ink / Monocle crit damage did almost nothing early (5% base crit).
9. **Infinite loops:** none truly infinite (relic level caps stopped luck and key chains), but shrine raises had no ceiling at all.

## 3. The new structure

| | WEAPONS | TOMES | ITEMS | EVOLUTIONS |
|---|---|---|---|---|
| From | Level-up cards | Level-up cards | Chests, Lost Property, altar / challenge chests | Level-up card (always Legendary) |
| Slots | `WeaponSlots` 4 shared + the reaper's signature (section 9) | `TomeSlots` 4 | none | Takes its weapon's slot |
| Levels | 1-8 | 1-5 (Duplicate Forms 1-3) | Stacks: 0 = no limit (`MaxStacks`) | - |
| Rarity | +0.10 / +0.22 / +0.40 weapon power | Gain x 1.0 / 1.4 / 1.9 / 2.6 | Gain x 1.0 / 1.4 / 1.9 / 2.6 | - |
| Count | 24 | 16 (the old level-up relics) | 23 (7 old relics + 16 new) | 24 |

**Level-up offer rules** (`ASQPlayerState::RollOffers`, `ASQCharacter::CanTakeUpgrade`):
- Cards are weapons, tomes and ready evolutions only. Items never appear on level-up cards.
- A new weapon / tome is only offered while that row has a free slot. Once both rows are full, only owned weapons / tomes
  (and ready evolutions) are offered.
- If fewer than 3 real cards are left, the empty spots are filled with **filler cards** (Upgrades.csv `Category Fallback`):
  **Expense Claim** (25 gold x rarity x night growth), **Training Seminar** (25% of a team level x rarity, at most 50%, split by
  crew size), **Sick Day** (full heal). The XP card can't loop: a pick gives at most 0.5 / crew of a level, so 4 maxed
  reapers get at most 0.5 level per level. Bots never get the XP card. Auto-pick prefers real cards over filler.
- Reroll (Q) and skip (X) are unchanged. A "swap" card was not added: it needs a pick-what-to-drop UI. Flagged as a follow-up.
- The base weapon of an evolution can no longer be taken again (exploit fixed).

**Chests** (`ASQChest::GrantChestRoll`): one item, random by `Weight`, rarity from the opener's luck + `ChestLuckBonus`.
The toast shows `[xN]` when it stacks. If every item is at its stack limit: gold and a full heal.
**Lost Property** (`ASQShrine`): offers items now, not tomes.

**Evolutions:** a weapon at level 8 + its paired **tome** (Upgrades.csv `RequiresRelic`). Every tome pairs with 1-2 weapons.
8 recipes used old relics that are now items, so they moved to tomes. Bone Turret and Prayer Beads were also moved
**off** quest-locked tomes (Extended Warranty Binder, Condolence Card Binder), so Bonesmith's starter can evolve from run 1.
SQData now logs an error if an evolution's `RequiresRelic` is not a tome.

**Bonds** are unchanged (ids kept). Five bonds now pair a tome with an item (for example Pallbearers = Commuter's Handbook +
Feather of Charon), which nicely mixes the two systems between teammates. Bond stats are raw, so the caps below still apply.

**Ids are stable.** Quests.csv (`Item:SkeletonKey / VampireFang / NightClass / RabbitFoot / TeaUrn / EmbalmerPatience`, weapons),
Bonds.csv and Reapers.csv `StartWeapon` all still resolve (checked by script). Shrines.csv and Timeline.csv hold no upgrade ids.

### Tomes (16)

| Id (stable) | Tome | Was (relic) | Gain per level (Common) | Max lvl | Evolves |
|---|---|---|---|---|---|
| Whetstone | Edge Maintenance Manual | Whetstone of Woe | Damage 0.08 | 5 | Reaping Moon (Scythe Sweep); Briar Crown (Wreath of Thorns) |
| Hourglass | Deadline Binder | Hourglass Shard | Cooldown 0.07 | 5 | Banshee Choir (Banshee Wail); Sunrise (Lantern Flare) |
| GraveDust | Plot Expansion Plans | Grave Dust | Area 0.12 | 5 | Requiem (Grave Bells); Poltergeist Flood (Ectoplasm Puddle) |
| Sack | Soul Inventory Ledger | Bottomless Sack | CarryCap 6 | 5 | Denied in Triplicate (Rubber Stamp) |
| MothLure | Lost and Found Register | Moth Lure | Magnet 55 | 5 | Murder of Crows (Crow Swarm) |
| FuneralShoes | Commuter's Handbook | Funeral Shoes | MoveSpeed 30, DashCooldown 0.08 | 5 | Permafrost (Frost Ring) |
| ExtraRibs | Spare Ribs Catalogue | Extra Ribs | MaxHealth 15 | 5 | Mausoleum (Tombstone Drop) |
| BonePlating | Health and Safety Binder | Bone Plating | Armor 0.08 | 5 | Earthen Maw (Gravequake); Six Feet Down (Shovel Smack) |
| SplitSoul | Duplicate Forms Binder | Split Soul | Projectiles 1, Damage 0.04 | 3 | Ferryman's Sickles (Thrown Sickles) |
| Embalming | Embalming Procedures Manual | Embalming Salts | Regen 0.45 | 5 | Black Death (Plague Cloud); Rosary of Ages (Prayer Beads) |
| Ledger | Reaper's Ledger | Reaper's Ledger | SoulBonus 0.05 | 5 | Final Demand (Overdue Notices); Gargoyle Roost (Gargoyle Perch) |
| RabbitFoot | Lucky Numbers Almanac | Skeletal Rabbit's Foot | Luck 0.35, CritChance 0.02 | 5 | Grand Seance (Seance Chain) |
| Monocle | Fine Print Ledger | Monocle of Scrutiny | CritChance 0.07, CritDamage 0.03 | 5 | Ossuary (Bone Turret); Soul Conduit (Soul Bolt) |
| RedInk | Red Ink Style Guide | Red Ink Pen | CritDamage 0.1, CritChance 0.02 | 5 | Spiked Coffin Lid (Coffin Nails); Thousand-Candle Vigil (Candle Beam) |
| EmbalmerPatience | Extended Warranty Binder | Embalmer's Patience | Duration 0.15, Area 0.05 | 5 | Ancestor Shrine (Spirit Totem) |
| TeaUrn | Condolence Card Binder | Tea Urn of Condolence | AllyRegen 0.6, Regen 0.15 | 5 | Last Rites (Funeral Hymn) |

### Items (23)

| Id (stable) | Item | Gain per stack (Common) | Max stacks | Weight | New? |
|---|---|---|---|---|---|
| CharonFeather | Feather of Charon | Evasion 0.06 | no limit | 7 | was relic "Feather of Charon" |
| GoldenKey | Golden Key | BankSpeed 0.3, CarryCap 3 | no limit | 6 | was relic "Golden Key" |
| ThornedShroud | Thorned Shroud | Armor 0.05, Thorns 15 | no limit | 6 | was relic "Thorned Shroud" |
| VampireFang | Vampire Fang | PickupHeal 0.15 | no limit | 6 | was relic "Vampire Fang" |
| SpareParts | Spare Parts Bin | TurretBonus 1 | 3 | 4 | was relic "Spare Parts Bin" |
| NightClass | Levitation Night Class | ExtraJumps 1 | 2 | 4 | was relic "Levitation Night Class" |
| SkeletonKey | Skeleton Key | ChestKey 0.1 | no limit | 5 | was relic "Skeleton Key" |
| CoffeeMug | Bottomless Coffee Mug | Cooldown 0.05 | no limit | 10 | new |
| BoneMeal | Bone Meal Shake | Damage 0.06 | no limit | 10 | new |
| ReadingGlasses | Half-Moon Reading Glasses | CritChance 0.05 | no limit | 9 | new |
| Highlighter | Dried-Out Highlighter | CritDamage 0.1 | no limit | 8 | new |
| TapeMeasure | Undertaker's Tape Measure | Area 0.09 | no limit | 8 | new |
| Thermos | Thermos of Cold Tea | Duration 0.18 | no limit | 6 | new |
| Insoles | Orthopaedic Insoles | MoveSpeed 25 | no limit | 7 | new |
| LumbarCushion | Lumbar Support Cushion | MaxHealth 12 | no limit | 9 | new |
| EmbalmingFlask | Hip Flask of Embalming Fluid | Regen 0.3 | no limit | 8 | new |
| HiVisShroud | Hi-Vis Shroud | Armor 0.06 | no limit | 8 | new |
| LintRoller | Lint Roller | Magnet 40 | no limit | 7 | new |
| LuckyPaperclip | Lucky Paperclip | Luck 0.15, ChestKey 0.04 | no limit | 6 | new |
| TipJar | Tip Jar | SoulBonus 0.04 | no limit | 6 | new |
| ExpandingFile | Expanding File | CarryCap 4 | no limit | 7 | new |
| SympathyBiscuits | Tin of Sympathy Biscuits | AllyRegen 0.5, Regen 0.15 | no limit | 6 | new |
| LateSlip | Running-Late Slip | DashCooldown 0.2, MoveSpeed 10 | no limit | 6 | new |

### Weapon / evolution damage changes (Damage + DamagePerLevel; level 8 = Damage + 7 x DamagePerLevel)

| Weapon | Before | After | Evolution | Tome | Before | After |
|---|---|---|---|---|---|---|
| Scythe Sweep | 14 + 4 | 13 + 2 | Reaping Moon | Edge Maintenance Manual | 75 | 45 |
| Thrown Sickles | 12 + 3 | 8.5 + 2.1 | Ferryman's Sickles | Duplicate Forms Binder | 30 | 25 |
| Banshee Wail | 4 + 1.5 | 5 + 1.8 | Banshee Choir | Deadline Binder | 11 | 20 |
| Bone Turret | 10 + 3 | 10 + 3 | Ossuary | Fine Print Ledger (was Spare Parts Bin) | 24 | 29 |
| Grave Bells | 30 + 8 | 33 + 8.9 | Requiem | Plot Expansion Plans | 85 | 170 |
| Crow Swarm | 8 + 2.5 | 5 + 1.6 | Murder of Crows | Lost and Found Register | 20 | 16 |
| Tombstone Drop | 45 + 12 | 24 + 6.3 | Mausoleum | Spare Ribs Catalogue | 95 | 89 |
| Frost Ring | 10 + 3 | 23 + 6.8 | Permafrost | Commuter's Handbook | 30 | 100 |
| Soul Bolt | 9 + 2.5 | 6 + 1.6 | Soul Conduit | Fine Print Ledger | 24 | 68 |
| Coffin Nails | 40 + 10 | 29 + 7.3 | Spiked Coffin Lid | Red Ink Style Guide (was Thorned Shroud) | 120 | 130 |
| Plague Cloud | 3 + 1 | 5 + 1.7 | Black Death | Embalming Procedures Manual | 9 | 25 |
| Lantern Flare | 16 + 4 | 15 + 3.7 | Sunrise | Deadline Binder (was Golden Key) | 55 | 47 |
| Prayer Beads | 10 + 3 | 6 + 1.8 | Rosary of Ages | Embalming Procedures Manual (was Feather of Charon) | 24 | 17 |
| Gravequake | 70 + 15 | 61 + 13 | Earthen Maw | Health and Safety Binder (was Vampire Fang) | 160 | 188 |
| Spirit Totem | 7 + 2 | 7.5 + 2.2 | Ancestor Shrine | Extended Warranty Binder | 16 | 24 |
| Funeral Hymn | 9 + 3 | 10 + 3.4 | Last Rites | Condolence Card Binder | 40 | 48 |
| Seance Chain | 9 + 2.5 | 20 + 5.5 | Grand Seance | Lucky Numbers Almanac | 30 | 73 |
| Candle Beam | 16 + 5 | 16 + 5 | Thousand-Candle Vigil | Red Ink Style Guide | 70 | 65 |
| Overdue Notices | 5 + 1.5 | 6 + 1.8 | Final Demand | Reaper's Ledger | 16 | 17 |
| Rubber Stamp | 18 + 5 | 8.5 + 2.4 | Denied in Triplicate | Soul Inventory Ledger | 60 | 32 |
| Shovel Smack | 26 + 7 | 23 + 6.3 | Six Feet Down | Health and Safety Binder | 110 | 80 |
| Wreath of Thorns | 6 + 2 | 3.5 + 1.2 | Briar Crown | Edge Maintenance Manual (was Thorned Shroud) | 22 | 13 |
| Ectoplasm Puddle | 9 + 3 | 10 + 3.4 | Poltergeist Flood | Plot Expansion Plans (was Levitation Night Class) | 26 | 20 |
| Gargoyle Perch | 14 + 4 | 10 + 2.9 | Gargoyle Roost | Reaper's Ledger (was Spare Parts Bin) | 40 | 35 |


## 4. Stat caps and stacking rules (`SQ::FinalizeStats`, Tuning.csv)

Every source (reaper, tomes, items, bonds, shrine raises, the Intern) adds **raw** values. The curves run **once** on the total.

| Stat | Rule | Tuning |
|---|---|---|
| Attack speed (CSV stat `Cooldown`) | Cooldown x 1 / (1 + sum), never below 40% | `CooldownFloor 0.4` |
| Crit chance | Capped at 100%; chance past 100% becomes crit damage 1:1 (decision: one crit tier, readable numbers, no dead stat) | `CritChanceMax 1`, `CritOverflowToDamage 1` |
| Area | At most 3x | `AreaMax 3` |
| Duration | At most 3x | `DurationMax 3` |
| Armor | Damage taken x 1 / (1 + armor). 0.01 armor = +1% effective HP (linear EHP, diminishing damage cut). At most 75% | `ArmorK 1`, `ArmorMax 0.75` |
| Evasion | Dodge = x / (x + 1), at most 40% | `EvasionK 1`, `EvasionMax 0.4` |
| Luck | 2 x L / (L + 2): about linear while small, never above 2 | `LuckSoftCap 2` |
| Free-chest key | x / (x + 1) (unchanged) | - |
| Extra projectiles / turrets | At most +4 / +6 | `ProjectileBonusMax 4`, `TurretBonusMax 6` |
| Move speed | At most 1100 (slides and bunny hops can still go faster) | `MoveSpeedMax 1100` |
| Soul bonus, jumps, dash | Soul bonus <= 100%, jumps <= 4, dash >= 0.4 s | - |
| Damage, HP, regen, carry, magnet, thorns, bank speed | Linear (each extra point is a smaller share; bank time = base / (1 + x)) | - |

Card text now says what really happens: "+7% attack speed", "+8 armor (+8% effective HP)", "+6% dodge chance (diminishing)",
"+35% luck (diminishing)". Shrine raises go through the same curves (the shrine boost ledger now adds to `RawStats`).

**Loops checked:** luck -> rarer items -> more luck is capped by the luck curve. Key -> free chests -> more keys is capped by
x/(x+1) and by the chests on the map. Gold filler -> chests -> items has no feedback. The XP filler converges (see above).

## 5. Balance pass results (model, 360 s nights)

**How a pick is valued:** the % gain of the metric it serves, on an on-curve build at the end of each night.
DPS = half horde DPS + half single-target DPS. EHP = (max HP + 30 s of healing) / damage taken, counted at 0.75 (damage wins
runs). Utility (carry, magnet, souls, banking, luck, keys, movement) uses the elasticity guesses at the top of the model's
items section. AllyRegen is valued in a 2-reaper crew. Rarity scales every row the same way, so the ratio to the median holds
at every rarity. Goal: every pick between 0.77x and 1.3x of the median.

### Value of one Common pick (D / E / U = DPS / EHP / Utility %, night 1 and 2; night 3 total; x median at night 2)

```
  TOMES (median total %: 5.3 / 4.8 / 4.7)
  Id                Stat        Night1 D/E/U       Night2 D/E/U   Night3 tot  xMed   Flag
  FuneralShoes      MoveSpeed    0.0/ 0.0/ 5.5     0.0/ 0.0/ 5.5    5.4      1.13   
  BonePlating       Armor        0.0/ 5.7/ 0.0     0.0/ 5.5/ 0.0    5.2      1.13   
  Sack              CarryCap     0.0/ 0.0/ 5.7     0.0/ 0.0/ 5.5    5.2      1.13   
  SplitSoul         Projectiles  6.9/ 0.0/ 0.0     5.4/ 0.0/ 0.0    5.0      1.12   
  Whetstone         Damage       5.8/ 0.0/ 0.0     5.1/ 0.0/ 0.0    4.6      1.06   
  Monocle           CritChance   4.6/ 0.0/ 0.0     5.1/ 0.0/ 0.0    5.3      1.05   
  MothLure          Magnet       0.0/ 0.0/ 5.3     0.0/ 0.0/ 5.0    4.7      1.03   
  Ledger            SoulBonus    0.0/ 0.0/ 4.9     0.0/ 0.0/ 4.9    4.8      1.01   
  ExtraRibs         MaxHealth    0.0/ 6.4/ 0.0     0.0/ 4.8/ 0.0    4.1      0.99   
  Hourglass         Cooldown     5.3/ 0.0/ 0.0     4.8/ 0.0/ 0.0    4.3      0.99   
  RedInk            CritDamage   3.5/ 0.0/ 0.0     4.4/ 0.0/ 0.0    5.0      0.90   
  Embalming         Regen        0.0/ 5.8/ 0.0     0.0/ 4.3/ 0.0    3.7      0.89   
  EmbalmerPatience  Duration     4.5/ 0.0/ 0.0     4.2/ 0.0/ 0.0    3.9      0.87   
  GraveDust         Area         4.5/ 0.0/ 0.0     4.2/ 0.0/ 0.0    3.9      0.86   
  RabbitFoot        Luck         1.1/ 0.0/ 3.1     1.1/ 0.0/ 2.9    3.8      0.83   
  TeaUrn            AllyRegen    0.0/ 5.0/ 0.0     0.0/ 3.9/ 0.0    3.4      0.80   
  ITEMS (median total %: 3.8 / 3.6 / 3.4)
  Id                Stat        Night1 D/E/U       Night2 D/E/U   Night3 tot  xMed   Flag
  CharonFeather     Evasion      0.0/ 4.4/ 0.0     0.0/ 4.3/ 0.0    4.2      1.18   
  LateSlip          DashCooldown  0.0/ 0.0/ 4.2     0.0/ 0.0/ 4.2    4.3      1.17   
  SkeletonKey       ChestKey     0.0/ 0.0/ 4.3     0.0/ 0.0/ 4.1    3.9      1.13   
  HiVisShroud       Armor        0.0/ 4.3/ 0.0     0.0/ 4.1/ 0.0    3.9      1.13   
  GoldenKey         BankSpeed    0.0/ 0.0/ 4.2     0.0/ 0.0/ 4.0    3.7      1.10   
  TipJar            SoulBonus    0.0/ 0.0/ 4.0     0.0/ 0.0/ 3.9    3.8      1.07   
  BoneMeal          Damage       4.4/ 0.0/ 0.0     3.9/ 0.0/ 0.0    3.4      1.06   
  LumbarCushion     MaxHealth    0.0/ 5.1/ 0.0     0.0/ 3.8/ 0.0    3.3      1.06   
  Insoles           MoveSpeed    0.0/ 0.0/ 3.8     0.0/ 0.0/ 3.7    3.6      1.02   
  ThornedShroud     Armor        0.8/ 3.6/ 0.0     0.2/ 3.4/ 0.0    3.4      1.01   
  ExpandingFile     CarryCap     0.0/ 0.0/ 3.8     0.0/ 0.0/ 3.6    3.5      1.00   
  LintRoller        Magnet       0.0/ 0.0/ 3.8     0.0/ 0.0/ 3.6    3.5      1.00   
  VampireFang       PickupHeal   0.0/ 4.8/ 0.0     0.0/ 3.6/ 0.0    3.1      0.99   
  CoffeeMug         Cooldown     3.8/ 0.0/ 0.0     3.4/ 0.0/ 0.0    3.1      0.94   
  SympathyBiscuits  AllyRegen    0.0/ 4.3/ 0.0     0.0/ 3.3/ 0.0    2.9      0.92   
  LuckyPaperclip    Luck         0.0/ 0.0/ 3.3     0.0/ 0.0/ 3.1    2.9      0.86   
  SpareParts        TurretBonus  3.1/ 0.0/ 0.0     3.1/ 0.0/ 0.0    3.1      0.86   
  TapeMeasure       Area         3.3/ 0.0/ 0.0     3.1/ 0.0/ 0.0    2.9      0.85   
  Highlighter       CritDamage   2.3/ 0.0/ 0.0     3.1/ 0.0/ 0.0    3.7      0.85   
  Thermos           Duration     3.2/ 0.0/ 0.0     3.0/ 0.0/ 0.0    2.8      0.83   
  NightClass        ExtraJumps   0.0/ 0.0/ 3.0     0.0/ 0.0/ 3.0    3.0      0.83   
  EmbalmingFlask    Regen        0.0/ 3.8/ 0.0     0.0/ 2.9/ 0.0    2.4      0.79   
  ReadingGlasses    CritChance   2.7/ 0.0/ 0.0     2.8/ 0.0/ 0.0    2.9      0.78   
```

No flags: tomes 0.80-1.13x, items 0.78-1.18x (before: relics 0.30-2.11x). Items give about 75% of a tome level per stack:
they are random, free of slots and come about 20 times a run.

### Weapons at max level (night-3 build) and evolutions

Each weapon's Damage and DamagePerLevel was scaled by the model so its role-weighted score is ~1.0 (area weapons 70% horde /
30% single target, the rest 30 / 70; weapons that slow get up to 12% credit). Scythe Sweep (Grim's starter) was then
front-loaded: same level-8 damage, more at level 1 (13 + 2 instead of 9 + 2.6). Evolution damage was scaled for a +55% spike.

```
  Weapon          Behaviour  Role   HordeDPS BossDPS  Score  H/B    Flag
  GraveBells      Nova       area   3134     224      1.02    1.2/0.5  
  Gravequake      Drop       area   2610     428      1.02    1.0/1.0  
  Tombstone       Drop       area   2599     427      1.02    1.0/1.0  
  RubberStamp     Drop       area   2586     424      1.01    1.0/1.0  
  LanternFlare    Nova       area   3034     240      1.01    1.2/0.6  
  PlagueCloud     Aura       area   2867     287      0.99    1.1/0.7  
  ScytheSweep     Arc        area   2998     220      0.98    1.2/0.5  
  ShovelSmack     Arc        area   2483     418      0.98    1.0/1.0  
  Ectoplasm       Aura       area   2043     573      0.97    0.8/1.3  
  Hymn            Arc        area   2949     211      0.96    1.2/0.5  
  WailCone        Aura       area   2533     326      0.93    1.0/0.8  
  FrostRing       Nova       area   2794     200      0.91    1.1/0.5  
  CrowSwarm       Orbit      single 1187     554      1.04    0.5/1.3  
  BoneTurret      Turret     single 947      568      1.03    0.4/1.3  
  SeanceChain     Chain      single 2942     420      1.03    1.2/1.0  
  CandleBeam      Beam       single 2525     449      1.03    1.0/1.0  
  CoffinNails     Projectile single 1957     489      1.03    0.8/1.1  
  PrayerBeads     Orbit      single 1363     530      1.02    0.5/1.2  
  SoulBolt        Projectile single 2155     471      1.02    0.9/1.1  
  SpiritTotem     Turret     single 932      559      1.02    0.4/1.3  
  Sickles         Projectile single 2285     457      1.01    0.9/1.1  
  GargoylePerch   Turret     single 925      555      1.01    0.4/1.3  
  WreathOfThorns  Orbit      single 1526     509      1.01    0.6/1.2  
  OverdueNotices  Projectile single 2524     434      1.00    1.0/1.0  
```

```
  Evolution           From            Tome              Spike   H / B   Flag
  FinalDemand         OverdueNotices  Ledger            +60   % 1.01/1.86 
  FerrymanSickles     Sickles         SplitSoul         +60   % 1.09/1.82 
  ReapingMoon         ScytheSweep     Whetstone         +59   % 1.60/1.56 
  BriarCrown          WreathOfThorns  Whetstone         +58   % 1.87/1.46 
  RosaryOfAges        PrayerBeads     Embalming         +58   % 2.06/1.37 
  AncestorShrine      SpiritTotem     EmbalmerPatience  +57   % 1.57/1.57 
  Sunrise             LanternFlare    Hourglass         +57   % 1.42/1.91 
  Ossuary             BoneTurret      Monocle           +56   % 1.56/1.56 
  ThousandCandles     CandleBeam      RedInk            +56   % 1.89/1.41 
  GrandSeance         SeanceChain     RabbitFoot        +55   % 2.08/1.33 
  SixFeetUnder        ShovelSmack     BonePlating       +55   % 1.72/1.17 
  SpikedCoffinLid     CoffinNails     RedInk            +55   % 1.01/1.78 
  LastRites           Hymn            TeaUrn            +55   % 1.55/1.55 
  Requiem             GraveBells      GraveDust         +55   % 1.55/1.55 
  PoltergeistFlood    Ectoplasm       GraveDust         +54   % 1.90/0.71 
  SoulConduit         SoulBolt        Monocle           +54   % 2.20/1.26 
  GargoyleRoost       GargoylePerch   Ledger            +54   % 1.54/1.54 
  DeniedInTriplicate  RubberStamp     Sack              +53   % 1.53/1.53 
  EarthenMaw          Gravequake      BonePlating       +53   % 1.53/1.53 
  MurderOfCrows       CrowSwarm       MothLure          +53   % 2.22/1.23 
  Permafrost          FrostRing       FuneralShoes      +53   % 1.53/1.53 
  Mausoleum           Tombstone       ExtraRibs         +53   % 1.53/1.53 
  BlackDeath          PlagueCloud     Embalming         +52   % 1.52/1.52 
  BansheeChoir        WailCone        Hourglass         +50   % 1.61/1.25 
```

### Build archetypes (section 11)

```
  Build     P   Night Lvl   Weapons (lv) / tomes     Horde    Single   EHP    TTK     Target
  typical   1   1     25    7+3,3,2 Wh3,Ho3,Ex2,Mo2  1427     223      187    161     150   
  typical   1   2     37    E1+5,4,4 Wh4,Ho4,Ex4,Mo3 4713     552      269    191     180   
  typical   1   3     48    E1+7,7,6 Wh5,Ho5,Ex5,Mo4 9253     1231     341    220     210   
  weapons   1   1     25    E1+4,3,3 Wh2,Ho2,Gr2     2420     258      144    139     150   
  weapons   1   2     37    E1+7,6,6 Wh3,Ho3,Gr3     4834     602      176    175     180   
  weapons   1   3     48    E1+8,8,8 Wh5,Ho5,Gr5     9737     1228     213    220     210   
  tomes     1   1     25    5+4 Wh4,Ho4,Gr4,Mo4      1380     164      144    220     150   
  tomes     1   2     37    E1+8 Wh5,Ho5,Gr5,Mo5     6693     692      176    153     180   
  tomes     1   3     48    E1+8 Wh5,Ho5,Gr5,Mo5     7892     811      213    334     210   
  crit      1   1     25    7+3,2,2 Wh3,Mo3,Re3,Ho2  1569     245      146    147     150   
  crit      1   2     37    E1+4,4,4 Wh4,Mo4,Re4,Ho4 6253     712      180    148     180   
  crit      1   3     48    E1+7,6,6 Wh5,Mo5,Re5,Ho5 13278    1745     219    155     210   
  support   4   1     25    7+3,3 Wh3,Te3,Ex3,Bo3    868      127      292    150     150   
  support   4   2     37    E1+5,5 Wh5,Te5,Ex4,Bo4   2711     300      420    184     180   
  support   4   3     48    E1+8,8 Wh5,Te5,Ex5,Bo5   4253     548      553    223     210   
  typical   4   1     25    7+3,3,2 Wh3,Ho3,Ex2,Mo2  1372     217      180    135     150   
  typical   4   2     37    E1+5,4,4 Wh4,Ho4,Ex4,Mo3 4441     522      252    164     180   
  typical   4   3     48    E1+7,7,6 Wh5,Ho5,Ex5,Mo4 8435     1130     311    194     210   
```

- **Typical** (evolution tome + Deadline Binder + Spare Ribs + Fine Print Ledger) is what boss HP is tuned for: TTK 1.0-1.07x.
- **Weapon-heavy** (4 weapons, 3 tomes) matches typical on DPS with less EHP.
- **Tome-heavy** (2 weapons) peaks mid-run (night 2 TTK 153 s) then **plateaus**: everything is maxed by level ~37 and later
  picks are filler. Filling your slots matters, as in Megabonk.
- **Crit** (Fine Print Ledger + Red Ink + 2 offence tomes) is the glass cannon: 1.40x the single-target DPS of a non-crit
  4-offence build at night 3 (1.27x horde) for -36% EHP. Crit chance and crit damage multiply, so the pair is worth more than
  each card's own value. It was 1.45x before Red Ink / Highlighter crit damage was trimmed. Watch it in play.
- **Support** (Condolence Card Binder, Spare Ribs, Health and Safety, Embalming) in a 4-reaper crew: +78% EHP and team TTK still on
  target, because the other three carry the damage.

### Power curve, typical solo build (before -> after)

| Night / min | Level | Single DPS before -> after | Horde DPS before -> after | EHP before -> after | Item/relic rolls so far |
|---|---|---|---|---|---|
| 1 / 1 | 5 | 40 -> 35 (x0.87) | 147 -> 136 (x0.93) | 136 -> 126 | 1.3 -> 1.5 |
| 1 / 3 | 13 | 128 -> 104 (x0.81) | 532 -> 460 (x0.87) | 192 -> 160 | 3.9 -> 4.5 |
| 1 / 6 | 25 | 360 -> 237 (x0.66) | 2114 -> 1556 (x0.74) | 262 -> 202 | 7.9 -> 9.1 |
| 2 / 1 | 25 | 368 -> 250 (x0.68) | 2159 -> 1674 (x0.78) | 294 -> 216 | 10.2 -> 11.6 |
| 2 / 3 | 29 | 682 -> 375 (x0.55) | 6504 -> 3553 (x0.55) | 294 -> 255 | 13.0 -> 14.8 |
| 2 / 6 | 37 | 1386 -> 621 (x0.45) | 12388 -> 5345 (x0.43) | 294 -> 306 | 17.0 -> 19.4 |
| 3 / 1 | 38 | 1443 -> 697 (x0.48) | 12616 -> 5850 (x0.46) | 294 -> 323 | 19.3 -> 21.9 |
| 3 / 3 | 41 | 1557 -> 891 (x0.57) | 13072 -> 7194 (x0.55) | 294 -> 342 | 21.8 -> 24.8 |
| 3 / 6 | 48 | 1892 -> 1433 (x0.76) | 14544 -> 10914 (x0.75) | 294 -> 398 | 25.5 -> 29.1 |

After the pass the player is weaker mid-run (night 2: about 0.45x the old DPS) because chests no longer level your crit and
cooldown relics. The economy health multipliers were re-fitted to match (Section 6).

### Before -> after

| Number | Before | After |
|---|---|---|
| Slots | 6 weapons + 6 relics (chests used them too) | 4 weapons + 4 tomes; items unlimited |
| Picks to max everything | 78 (> 48 level-ups) | 52 + evolutions (filler after that) |
| Chest reward | A level of a level-up relic | One of 23 stacking items |
| Tome / relic value per pick (x median) | 0.30-2.11; 4 dominant, 2 dead, 7 weak | 0.80-1.13; none flagged |
| Item value per stack (x median) | - | 0.78-1.18 |
| Weapon score at max level | 0.41-1.84 (16 / 24 outside 0.8-1.2) | 0.91-1.04 |
| Evolution spike | -65% .. +126% (20 / 24 outside 40-70%) | +50% .. +60% |
| Boss TTK / target (1-4 players) | 0.87-1.12 | 0.90-1.08 |
| Hits-to-kill night 1 solo (main / median starter) | 0.7-1.3 / 0.7-1.2 | 1.0-1.6 / 0.6-1.2 |
| Hits-to-kill night 3 solo / duo / 4p (main) | 1.7-2.7 / 2.2-3.7 / 3.4-5.6 | 2.1-3.1 / 2.9-4.3 / 4.4-6.6 |
| Hits-to-kill night 3 4p (median starter) | 4.4-7.3 | 3.0-4.4 |
| Biggest damage number | 540 | ~210 |
| `EconomyBossHealth` 1 / 2 / 3 | 1.25 / 1.8 / 2.0 | 0.87 / 0.9 / 1.3 |
| `EconomyEnemyHealth` 1 / 2 / 3 | 1.05 / 1.2 / 1.25 | 1.05 / 1.05 / 1.0 |
| Standard win run length | 28.9 min | 28.4 min |

## 6. Model limits (read before trusting the tables)

- The horde-DPS model caps Arc / Nova at 14 targets and Aura at 10, and **Drop ignores its radius** (Rubber Stamp's 140 cm
  counts like Gravequake's 320 cm). So Rubber Stamp (-52%) and Tombstone (-47%) cuts may be over-corrections. Check in play.
- Area / Duration value comes from the model's own assumptions (`AREA_DENSITY 0.5`; slows and turrets at half value).
- Utility elasticities, the 0.75 EHP weight, 30 s regen window, 2.5 souls/s and 2 shrine raises/night are guesses.
- Only the main weapon evolves in the model; the other weapons are an average weapon.
- Level-up cards are assumed to offer what the plan wants (no bad luck); luck is not fed back into card rarity.

## 7. How to retune

1. A tome or item too strong / weak: change `StatPerLevel` in Upgrades.csv, run the model, read section 9 (`xMed`, goal 0.77-1.3).
2. A weapon: change `Damage` and `DamagePerLevel` together, read section 10 (goal 0.8-1.2).
3. An evolution: change its `Damage`, read the evolution table (goal +40-70%).
4. Slots and caps: Tuning.csv keys in Section 4.
5. Bosses too slow / fast after any of this: `EconomyBossHealth1-3`; horde: `EconomyEnemyHealth1-3`; read sections 2 and 5.

## 8. Follow-ups for other owners

- **SQHUD loadout panel** (not touched): still says `WEAPONS n/6` and `RELICS n/6` and mixes tomes with items. It should read
  `GetWeaponSlots()` / `GetTomeSlots()`, show TOMES and ITEMS separately and show item stacks as `xN`.
- **Text that still says "relic":** Shrines.csv Lost Property prompt and description, Quests.csv `Legendary1`
  ("a Legendary relic from a chest"), SQHUD.cpp around lines 3213 / 3218 ("a relic, on the house", "a random relic").
- **Docs:** GameDesign.md / ContentPlan.md / CLAUDE.md scope still say "16 weapons / 15 relics / 16 evolutions".
- `DamageNumberHuge 300` is now rarely reached (biggest hit ~210); consider 200.
- A swap card ("drop a tome for this one") would need a choice UI.

## 9. Signature weapons (Nov 9 kits pass; Docs/Kits.md)

Every reaper has a **signature weapon** (Upgrades.csv `Signature 1`, last column). Rules (`ASQCharacter::CanTakeUpgrade`,
`CountWeapons`, SQData kit loader):

- Only its owner (Kits.csv `ReaperId` -> `SignatureWeaponId`) is ever offered it or its evolution. Every other reaper's cards,
  shrines and chests skip it.
- The owner **starts** with it at level 1 (Reapers.csv `StartWeapon` = the signature; Kits.csv overrides it if they differ) and
  levels it 1-8 through normal level-up cards (card chip: `SIGNATURE`).
- It does **not** use a shared slot: `WeaponSlots` (4) now counts shared weapons only, so a reaper holds **signature + 4**.
  `CountWeapons()` excludes signatures (HUD: draw the signature on its own, then `CountWeapons()/GetWeaponSlots()`).
- It evolves like any weapon: max level + its paired **tome** at any level (always a Legendary card). The same tome at **max
  level** also evolves the reaper's E and R (Kits.csv `EvoTome`).
- The Intern's random start weapon is gone (he has Reply-All). The 24 shared weapons are all in the pool for everyone.
- Signatures were fitted to a weapon score of ~1.08 (beams / tight orbits lower), evolutions to +55%
  (balance_model.py section 12; the shared-weapon tables in sections 10-11 exclude signatures).

| Reaper | Signature | Behaviour | Damage + per level | Paired tome | Evolution | Evo damage |
|---|---|---|---|---|---|---|
| Grim | Company Scythe (`CompanyScythe`) | Arc | 11.6 + 1.97 (lv8 25) | Edge Maintenance Manual | Severance Package (`SeverancePackage`) | 45 |
| Swift the Hauler | Express Sickles (`ExpressSickles`) | Projectile | 7.2 + 1.81 (lv8 20) | Commuter's Handbook | Next-Day Delivery (`NextDayDelivery`) | 21 |
| The Wailing Widow | Lament (`Lament`) | Aura | 4.8 + 1.74 (lv8 17) | Deadline Binder | Eternal Lament (`EternalLament`) | 24 |
| Bonesmith | Ossified Sentry (`OssifiedSentry`) | Turret | 10.5 + 3.14 (lv8 32) | Health and Safety Binder | Bone Citadel (`BoneCitadel`) | 30 |
| The Lantern Keeper | Lantern Pulse (`LanternPulse`) | Nova | 13.3 + 3.37 (lv8 37) | Plot Expansion Plans | Lighthouse (`Lighthouse`) | 61 |
| The Hollow Monk | Mala Orbit (`MalaOrbit`) | Orbit | 6.1 + 1.94 (lv8 20) | Fine Print Ledger | Hundred-Bead Halo (`HundredBeadHalo`) | 18 |
| The Intern | Reply-All (`ReplyAll`) | Chain | 21.1 + 5.77 (lv8 61) | Reaper's Ledger | Reply-All Storm (`ReplyAllStorm`) | 77 |
| Femur the Hound | Buried Bone (`BuriedBone`) | Drop | 58.8 + 12.66 (lv8 147) | Lost and Found Register | Excavation (`Excavation`) | 191 |
| Digby Sixfeet | Shovel Sweep (`ShovelSweep`) | Arc | 23.5 + 6.45 (lv8 69) | Spare Ribs Catalogue | Mass Grave (`MassGrave`) | 82 |
| The Debt Collector | Final Reminders (`FinalReminders`) | Projectile | 6.5 + 1.94 (lv8 20) | Soul Inventory Ledger | Debt Spiral (`DebtSpiral`) | 18 |
| Doctor Beak | Miasma (`Miasma`) | Aura | 5.5 + 1.87 (lv8 19) | Embalming Procedures Manual | Quarantine (`Quarantine`) | 28 |
| Tallow | Wick Bolt (`WickBolt`) | Projectile | 4.7 + 1.27 (lv8 14) | Red Ink Style Guide | Blowtorch (`Blowtorch`) | 23 |
| The Graveyard Lookout | Searchlight (`Searchlight`) | Beam | 13.5 + 4.21 (lv8 43) | Red Ink Style Guide | Floodlights (`Floodlights`) | 55 |
| Vexa the Veiled | Spite Wreath (`SpiteWreath`) | Orbit | 3.2 + 1.1 (lv8 11) | Edge Maintenance Manual | Crown of Spite (`CrownOfSpite`) | 12 |
| The Actuary | Denial Stamp (`DenialStamp`) | Drop | 7.9 + 2.23 (lv8 24) | Reaper's Ledger | Audit Trail (`AuditTrail`) | 32 |
| The Chimney Sweep | Crow Relay (`CrowRelay`) | Chain | 19.8 + 5.43 (lv8 58) | Commuter's Handbook | Murmuration (`Murmuration`) | 76 |
| Sister Solace | Hymn of Solace (`HymnOfSolace`) | Arc | 10.8 + 3.66 (lv8 36) | Spare Ribs Catalogue | Requiem Mass (`RequiemMass`) | 55 |
| The Pallbearers | Mourners' Bench (`PallTurret`) | Turret | 10.7 + 3.1 (lv8 32) | Health and Safety Binder | Full Procession (`FullProcession`) | 38 |
| Madame Ectoplasm | Seance Link (`SeanceLink`) | Chain | 21.1 + 5.77 (lv8 61) | Plot Expansion Plans | Full Seance (`FullSeance`) | 77 |
| Sir Neckless | Pumpkin Glare (`PumpkinGlare`) | Beam | 12.6 + 3.92 (lv8 40) | Fine Print Ledger | Headless Horror (`HeadlessHorror`) | 53 |
| Old Tickwell | Tolling Hour (`TollingHour`) | Nova | 32.5 + 8.75 (lv8 94) | Deadline Binder | Midnight (`Midnight`) | 176 |

## 10. Remake balance pass (Nov 9 2026; Docs/Balance.md section 11)

Measured on the **run simulation's** reference builds (Grim, Plague Carnival normal, the ranks a player has by then; three real runs,
end of act 1 / act 2 / bell) instead of the old "night" builds. Contracts: weapons within +-15% at max level, evolutions +45-65%,
no tome or item above 1.3x the median of its rarity.

Main changes (full list: Balance.md 11.7):
- **Drop weapons now use their radius** (a drop lands on an enemy and hits everything within it, same crowd density as Arc / Nova).
  Gravequake (r 320) was hiding a 3x score and Tombstone 1.9x: Gravequake radius 320 -> 240 (+10 / level) and damage 61 -> 26.7,
  Tombstone 24 -> 12.9, Rubber Stamp 8.5 -> 6.9 (the old "over-nerfed stamp / tombstone" worry was the other way round:
  the stamp was overrated, the tombstone underrated, Gravequake badly underrated). Every weapon then fitted to 1.00.
- **Split Soul** +1 projectile per level -> +0.6 (whole projectiles per source, so about every other pick; max level 3 -> 5):
  one projectile per pick was 2x the median. Red Ink 0.10 -> 0.15 crit damage, Highlighter 0.10 -> 0.22.
- **Healing / HP picks** were the new dominant ones once the sim's builds carried less HP: Extra Ribs 15 -> 12, Embalming Salts
  0.45 -> 0.38, Lumbar Cushion 12 -> 9, Vampire Fang 0.15 -> 0.11; Tea Urn 0.6 -> 0.5 ally regen, Sympathy Biscuits 0.5 -> 0.3.
- Moth Lure 55 -> 75 and Lint Roller 40 -> 55 pickup range (were weak).

```
-- 21. WEAPONS, EVOLUTIONS, TOMES, ITEMS on the sim's reference builds (Grim, PlagueCarnival normal, journey meta; end of act 1 / 2 / bell)

  TOMES (value of one Common pick, % power; median 5.2 / 4.9 / 4.5); contract: none above 1.3x the median of its rarity
  Id                Stat        Act1 D/E/U         Act2 D/E/U         xMed   Flag
  MothLure          Magnet       0.0/ 0.0/ 6.3     0.0/ 0.0/ 6.0    1.25   
  Monocle           CritChance   6.1/ 0.0/ 0.0     5.8/ 0.0/ 0.0    1.16   
  ExtraRibs         MaxHealth    0.0/ 6.7/ 0.0     0.0/ 5.7/ 0.0    1.22   
  BonePlating       Armor        0.0/ 5.7/ 0.0     0.0/ 5.6/ 0.0    1.16   
  Sack              CarryCap     0.0/ 0.0/ 5.7     0.0/ 0.0/ 5.5    1.14   
  Embalming         Regen        0.0/ 6.4/ 0.0     0.0/ 5.4/ 0.0    1.16   
  FuneralShoes      MoveSpeed    0.0/ 0.0/ 5.2     0.0/ 0.0/ 5.2    1.07   
  Hourglass         Cooldown     5.2/ 0.0/ 0.0     5.1/ 0.0/ 0.0    1.01   
  TeaUrn            AllyRegen    0.0/ 5.5/ 0.0     0.0/ 4.7/ 0.0    1.01   
  Ledger            SoulBonus    0.0/ 0.0/ 4.8     0.0/ 0.0/ 4.7    0.95   
  GraveDust         Area         4.6/ 0.0/ 0.0     4.6/ 0.0/ 0.0    0.92   
  RabbitFoot        Luck         1.6/ 0.0/ 3.3     1.5/ 0.0/ 2.8    0.90   
  Whetstone         Damage       4.3/ 0.0/ 0.0     4.2/ 0.0/ 0.0    0.87   
  EmbalmerPatience  Duration     4.6/ 0.0/ 0.0     4.1/ 0.0/ 0.0    0.86   
  RedInk            CritDamage   3.3/ 0.0/ 0.0     3.5/ 0.0/ 0.0    0.69   weak
  SplitSoul         Projectiles  1.6/ 0.0/ 0.0     1.6/ 0.0/ 0.0    0.33   weak

  ITEMS (value of one Common pick, % power; median 3.8 / 3.7 / 3.5); contract: none above 1.3x the median of its rarity
  Id                Stat        Act1 D/E/U         Act2 D/E/U         xMed   Flag
  SkeletonKey       ChestKey     0.0/ 0.0/ 4.5     0.0/ 0.0/ 4.5    1.24   
  LintRoller        Magnet       0.0/ 0.0/ 4.6     0.0/ 0.0/ 4.4    1.22   
  CharonFeather     Evasion      0.0/ 4.5/ 0.0     0.0/ 4.3/ 0.0    1.19   
  LumbarCushion     MaxHealth    0.0/ 5.0/ 0.0     0.0/ 4.3/ 0.0    1.22   
  EmbalmingFlask    Regen        0.0/ 5.0/ 0.0     0.0/ 4.3/ 0.0    1.22   
  HiVisShroud       Armor        0.0/ 4.3/ 0.0     0.0/ 4.2/ 0.0    1.15   
  LateSlip          DashCooldown  0.0/ 0.0/ 4.1     0.0/ 0.0/ 4.1    1.13   
  GoldenKey         BankSpeed    0.0/ 0.0/ 4.3     0.0/ 0.0/ 4.0    1.11   
  VampireFang       PickupHeal   0.0/ 4.6/ 0.0     0.0/ 3.9/ 0.0    1.11   
  ReadingGlasses    CritChance   4.1/ 0.0/ 0.0     3.8/ 0.0/ 0.0    1.01   
  TipJar            SoulBonus    0.0/ 0.0/ 3.8     0.0/ 0.0/ 3.7    1.01   
  ExpandingFile     CarryCap     0.0/ 0.0/ 3.8     0.0/ 0.0/ 3.7    1.01   
  CoffeeMug         Cooldown     3.7/ 0.0/ 0.0     3.7/ 0.0/ 0.0    0.95   
  ThornedShroud     Armor        0.3/ 3.6/ 0.0     0.1/ 3.5/ 0.0    0.99   
  Insoles           MoveSpeed    0.0/ 0.0/ 3.5     0.0/ 0.0/ 3.5    0.95   
  TapeMeasure       Area         3.4/ 0.0/ 0.0     3.4/ 0.0/ 0.0    0.91   
  LuckyPaperclip    Luck         0.0/ 0.0/ 3.5     0.0/ 0.0/ 3.3    0.91   
  BoneMeal          Damage       3.2/ 0.0/ 0.0     3.2/ 0.0/ 0.0    0.87   
  SympathyBiscuits  AllyRegen    0.0/ 3.6/ 0.0     0.0/ 3.1/ 0.0    0.89   
  SpareParts        TurretBonus  3.1/ 0.0/ 0.0     3.1/ 0.0/ 0.0    0.85   
  NightClass        ExtraJumps   0.0/ 0.0/ 3.0     0.0/ 0.0/ 3.0    0.82   
  Thermos           Duration     3.2/ 0.0/ 0.0     2.7/ 0.0/ 0.0    0.78   
  Highlighter       CritDamage   2.1/ 0.0/ 0.0     2.6/ 0.0/ 0.0    0.68   weak

  Weapons at max level with the late build (level 45); score = role-weighted DPS vs the median (contract 0.85-1.15)
  Ectoplasm       Aura       area   2165     716      0.98   
  Hymn            Arc        area   3327     238      0.98   
  ShovelSmack     Arc        area   2632     521      0.98   
  WailCone        Aura       area   2851     431      0.98   
  RubberStamp     Drop       area   2942     389      0.98   
  ScytheSweep     Arc        area   3215     277      0.98   
  LanternFlare    Nova       area   3171     295      0.98   
  Gravequake      Drop       area   3372     212      0.98   
  FrostRing       Nova       area   3311     236      0.98   
  Tombstone       Drop       area   3255     259      0.98   
  PlagueCloud     Aura       area   3124     312      0.98   
  GraveBells      Nova       area   3310     236      0.98   
  PrayerBeads     Orbit      single 1538     598      1.02   
  SpiritTotem     Turret     single 1052     631      1.02   
  Sickles         Projectile single 2579     516      1.02   
  GargoylePerch   Turret     single 1044     626      1.01   
  WreathOfThorns  Orbit      single 1722     574      1.01   
  OverdueNotices  Projectile single 2848     490      1.01   
  CrowSwarm       Orbit      single 1280     597      1.00   
  BoneTurret      Turret     single 1026     615      0.99   
  SoulBolt        Projectile single 2354     515      0.99   
  CoffinNails     Projectile single 2126     532      0.99   
  CandleBeam      Beam       single 2730     485      0.99   
  SeanceChain     Chain      single 3163     452      0.99   

  Evolutions: spike vs the maxed base weapon (contract +45-65%)
  SoulConduit         SoulBolt        Monocle           +59  % 
  BriarCrown          WreathOfThorns  Whetstone         +58  % 
  RosaryOfAges        PrayerBeads     Embalming         +58  % 
  BlackDeath          PlagueCloud     Embalming         +58  % 
  AncestorShrine      SpiritTotem     EmbalmerPatience  +57  % 
  FerrymanSickles     Sickles         SplitSoul         +55  % 
  MurderOfCrows       CrowSwarm       MothLure          +55  % 
  SixFeetUnder        ShovelSmack     BonePlating       +55  % 
  Ossuary             BoneTurret      Monocle           +55  % 
  Permafrost          FrostRing       FuneralShoes      +55  % 
  BansheeChoir        WailCone        Hourglass         +55  % 
  GrandSeance         SeanceChain     RabbitFoot        +55  % 
  Requiem             GraveBells      GraveDust         +55  % 
  SpikedCoffinLid     CoffinNails     RedInk            +55  % 
  ThousandCandles     CandleBeam      RedInk            +55  % 
  Sunrise             LanternFlare    Hourglass         +55  % 
  Mausoleum           Tombstone       ExtraRibs         +55  % 
  EarthenMaw          Gravequake      BonePlating       +55  % 
  DeniedInTriplicate  RubberStamp     Sack              +55  % 
  ReapingMoon         ScytheSweep     Whetstone         +55  % 
  FinalDemand         OverdueNotices  Ledger            +55  % 
  PoltergeistFlood    Ectoplasm       GraveDust         +55  % 
  LastRites           Hymn            TeaUrn            +55  % 
  GargoyleRoost       GargoylePerch   Ledger            +54  % 

  Signature evolutions (spike, contract +45-65%): CompanyScythe +55%, ExpressSickles +52%, Lament +55%, OssifiedSentry +54%, LanternPulse +55%, MalaOrbit +58%, ReplyAll +56%, BuriedBone +55%, ShovelSweep +54%, FinalReminders +57%, Miasma +55%, WickBolt +50%, Searchlight +55%, SpiteWreath +56%, DenialStamp +52%, CrowRelay +54%, HymnOfSolace +56%, PallTurret +56%, SeanceLink +56%, PumpkinGlare +54%, TollingHour +55%
```

### Augments: value per pick (model section 20)

Value = % power one pick adds on the reference run: augments that change the build in place (stats, procs, auras, Pardon) are added
to the builds at the bell (0.5 x horde DPS gain + 0.5 x single-target gain, elites and boss averaged + 0.75 x EHP gain + utility);
run-level ones (slots, level-ups, chests, XP, rerolls, gold) are two full runs with the same random streams, forced at 35% of the
stage. Bosses take no OnHit augment effects (Execute, Overkill, split bolts, Elite Hunter, Last Stand), only stats and area blasts.
Targets: Silver 1.0x, Gold 1.5x, Prism 2.2x the median Silver (+-30%). All 37 are in band.

| Augment | Tier | Effect | Numbers (Value / Value2 / Value3) | Value per pick (% power) | x median Silver |
|---|---|---|---|---|---|
| Leech Jar | Silver | KillHeal | 2 / 0.06 / - | 16.4 | 1.29 |
| Grave Dirt Poultice | Silver | Stat | 1.2 / - / - | 15.6 | 1.23 |
| Restless Feet | Silver | Stat | 60 / 0.3 / - | 15.5 | 1.22 |
| Thick Shroud | Silver | Stat | 25 / 0.06 / - | 15.1 | 1.19 |
| Spare Change | Silver | KillGold | 0.12 / 3 / - | 14.8 | 1.16 |
| Steady Hands | Silver | Stat | 0.2 / - / - | 14.3 | 1.13 |
| Grave Grit | Silver | Stat | 0.2 / - / - | 14.1 | 1.11 |
| Long Reach | Silver | Stat | 120 / 0.06 / - | 14.0 | 1.11 |
| Spell Echo | Silver | CastNova | 0 / 500 / 300 | 12.7 | 1.00 |
| Volley | Silver | Stat | 2 / 0.1 / - | 12.7 | 1.00 |
| Bounty Hunter | Silver | EliteHunter | 0.5 / - / - | 12.6 | 0.99 |
| Clay Bellows | Silver | Stat | 0.5 / - / - | 12.1 | 0.95 |
| Second Opinion | Silver | Rerolls | 8 / - / - | 12.0 | 0.95 |
| Lucky Knucklebones | Silver | Stat | 0.8 / 0.1 / - | 11.9 | 0.94 |
| Pocket Chest | Silver | ChestNow | 4 / - / - | 11.3 | 0.89 |
| Tailwind Rite | Silver | CastHaste | 350 / 3 / - | 11.1 | 0.87 |
| Last Stand | Silver | LowHPDamage | 1.2 / 0.5 / - | 10.5 | 0.83 |
| Crash Course | Gold | LevelUps | 3 / - / - | 23.0 | 1.81 |
| Orbiting Gravestones | Gold | Gravestones | 3 / 260 / 30 | 20.1 | 1.58 |
| Glass Edge | Gold | Stat | 0.45 / -30 / - | 18.8 | 1.48 |
| Extra Holster | Gold | WeaponSlot | 1 / 4 / - | 17.4 | 1.37 |
| Extra Bookshelf | Gold | TomeSlot | 1 / 1 / - | 17.4 | 1.37 |
| Midas Shroud | Gold | KillGold | 0.3 / 5 / - | 17.3 | 1.37 |
| Spite Bloom | Gold | HurtNova | 400 / 2 / 80 | 17.2 | 1.35 |
| Swift Last Rites | Gold | QuickRevive | 1.5 / 0.5 / - | 17.2 | 1.36 |
| Shrapnel Shroud | Gold | HitSplit | 0.25 / 0.9 / 2 | 16.4 | 1.30 |
| Ruthless Rhythm | Gold | KillRefund | 0.25 / - / - | 16.4 | 1.29 |
| Overkill | Gold | Overkill | 2 / 200 / - | 16.4 | 1.29 |
| Reaper's Mercy | Gold | Execute | 0.25 / - / - | 15.9 | 1.25 |
| Powder Keg Souls | Gold | KillBlast | 0.2 / 220 / 35 | 13.9 | 1.09 |
| Wake Keeper | Prism | AuraStat | 1.5 / 900 / - | 31.7 | 2.50 |
| Gravestone Ring | Prism | Gravestones | 6 / 340 / 22 | 29.4 | 2.32 |
| Study Group | Prism | XPBoost | 0.3 / - / - | 27.7 | 2.19 |
| Shared Plot | Prism | AuraStat | 0.25 / 900 / - | 26.8 | 2.12 |
| Death's Pardon | Prism | Pardon | 1 / - / - | 26.2 | 2.07 |
| Funeral Choir | Prism | AuraStat | 0.2 / 900 / - | 25.6 | 2.02 |
| Chain Reaction | Prism | KillBlast | 0.2 / 300 / 30 | 22.6 | 1.78 |

Tier moves: Pocket Chest, Last Stand, Tailwind Rite, Spell Echo and Volley -> Silver (each was worth about a Silver); Death's Pardon
-> Prism (a free revive solo); Midas Shroud and Swift Last Rites -> Gold. Extra Holster / Extra Bookshelf now also give 4 / 1 free
level-up picks (code: `USQAugments::ApplyPicked`), because a bare slot was a trap in a 15-20 minute run (picks spread thinner).
Loop caps (chain depth, refund budget, Pardon cap) are in Balance.md 11.4.
