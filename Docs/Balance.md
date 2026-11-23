# Soul Quota - Balance (Nov 2026 balance pass; Nov 9 missions rebuild: section 10; **Nov 9 remake balance pass: section 11 = the contract**)

Model: `Scripts/balance_model.py` (plain Python 3, reads `Content/Data/*.csv`).

```powershell
cd E:\Projects\SoulQuota
python Scripts\balance_model.py              # release night length (360 s)
python Scripts\balance_model.py --night 90   # current test NightSeconds
```

**Nov 9 2026: a run is now one mission stage of 3 acts (section 10); an act plays the role of the old night below.** All numbers are tuned for **6-minute nights** (`BalanceNightSeconds 360`, GameDesign.md). Shorter test
nights (NightSeconds 90 now) scale the per-night HP step, boss HP and shop prices by
`NightSeconds / BalanceNightSeconds`, so a test run still plays close to the targets.

## 1. Goals

| Goal | Target | Model result (360 s nights) |
|---|---|---|
| Hits to kill a typical enemy, on-curve build | 1-3 early, 2-5 late | Items/tomes pass (main weapon): night 1 1.0-1.6 solo; night 3 2.1-3.1 solo, 2.9-4.3 duo, 4.4-6.6 four players (median starter 3.0-4.4) |
| Boss time-to-kill at expected team DPS | Bosses.csv TargetSeconds (150-240 s) | 0.90x-1.08x of target, 1-4 players (items/tomes pass) |
| Death's Office | Prices climb per buy, 3-5 buys per visit | 3-5 buys (1, 2 and 4 players, both shops) |
| Meta (soul coins) | ~1 reaper unlock per 1-2 runs | 1.4 runs (solo) to 1.9 runs (4 players) per unlock |
| Damage numbers | Tens to low thousands | Night 1: 14-50. Final night: ~110 normal hits, ~200 crits/evolutions (items/tomes pass) |

## 2. Formulas (what the C++ does)

**Levels.** XP to next level = `(XPBase + XPGrowth x level) x (1 + XPPerPlayer x (players - 1))`.
XPGrowth went from 6 to 15. Each extra player adds +35% spawns (and XP), and XPPerPlayer 0.35 adds +35% XP per level, so
every reaper levels at about the solo pace. Before this, a 4-player team out-levelled solo by 40% and melted bosses.

**Enemy HP** (`ASQHordeManager::SpawnEnemy`):
```
HP = (Health + HealthPerNight x night x NightSeconds/BalanceNightSeconds)
     x (1 + EnemyHealthPerMinute x minutes into the night)        0.10 = +10%/min (Megabonk ~+10%/min)
     x (1 + EnemyHealthPerPlayer x (players - 1))                 0.35 = +35% per extra reaper
```
A boss fight counts as midnight (full ramp). Summons and splits use the same formula.

**Enemy damage** (`ASQHordeManager::SimEnemies`, every hit / bomb / spit):
```
Damage = Enemies.csv Damage x (1 + EnemyDamagePerNight x night) x (1 + EnemyDamagePerMinute x minutes into the night)
                                    0.30                                 0.03 (Megabonk ~+2.8%/min)
```

**Boss HP** (`ASQBoss::InitBoss`, scale passed in by `ASQGameMode::Midnight`):
```
HP = Bosses.csv Health x (1 + BossHealthPerPlayer x (players - 1)) x (1 + 0.25 x Defiance) x endless 1.3^n x NightSeconds/BalanceNightSeconds
```
Each kill logs `[BossTTK] ... CSV Health for the target: N`. Paste N into Bosses.csv when a real fight is far off.

**Shop price** (`ASQGameState::ShopPrice`, used by the HUD and `ASQGameMode::TryBuy`):
```
Price = Shop.csv Cost x (1 + ShopPricePerBuy x buys this visit) x (1 + ShopPricePerNight x night)
        x (1 + ShopPricePerPlayer x (players - 1)) x NightSeconds/BalanceNightSeconds
            0.75                             0.5                           0.75
```
Any purchase raises every price. Prices reset to base at the start of each visit (`ShopBuys`).
Solo prices for buys 1-5 after night 1: 80, 140, 200, 260, 320 (Megabonk chests: 30, 72, 115 ...).

**Player damage** (unchanged): `(Damage + DamagePerLevel x (level-1)) x Power x DamageMult x crit`. Crit = `1 + CritDamage` on a
`CritChance` roll. CritChance is capped at 100%: no Megabonk-style overcrit, which keeps numbers readable. Since the items/tomes
pass, crit chance past 100% turns into crit damage, and every stat runs through `SQ::FinalizeStats` (Section 8).

**Soul coins** (`SQPlayerController`): `PayPerNight x nights + your kills / KillsPerPay (+ PayWinBonus)`. Now 60 / 40 / 150 (was 40 / 10 / 150).

## 3. Key numbers per night (solo, 360 s nights; 4 players in brackets)

| | Night 1 | Night 2 | Night 3 |
|---|---|---|---|
| Team level (start -> midnight) | 1 -> 25 | 25 -> 37 | 37 -> 48 |
| Shambler HP (start -> midnight) | 18 -> 29 (59) | 83 -> 133 (272) | 148 -> 237 (485) |
| Average horde HP at midnight | 43 (89) | 193 (395) | 347 (711) |
| Grave Brute HP (start -> midnight) | 450 -> 720 | 1750 -> 2800 | 3050 -> 4880 |
| Shambler hit (start -> midnight), reaper has 120+ HP | 8 -> 9.4 | 10.4 -> 12.3 | 12.8 -> 15.1 |
| Main weapon hit, Scythe line (start -> midnight) | 14 -> 62 | 62 -> 139 (Reaping Moon) | 139 |
| Single-target DPS per reaper (midnight) | 244 | 582 | 880 |
| Boss HP solo (4 players) | Bell Ringer 31,000 (100,750) | Gravekeeper 88,000 (286,000) | Legends 156,000 (507,000); Merchant 178,000 (578,500) |
| Boss TTK solo / 4 players (target) | 169 s / 137 s (150) | 202 s / 164 s (180) | 236 s / 192 s (210) |
| Shop after the night, buy-1 prices solo (4p) | 80 / 160 / 120 / 240 (260 / 520 / 390 / 780) | 120 / 240 / 180 / 360 (390 / 780 / 585 / 1170) | (run ends) |
| Expected surplus souls solo (4p) | ~820 (~3,500) | ~720 (~3,300) | |

At the test length (90 s nights) the model gives levels 12 / 19 / 24, boss HP x0.25 (Bell Ringer 7,750 solo) and
boss TTK 0.6x-1.1x of target.

### Before -> after

| Number | Before | After |
|---|---|---|
| XPGrowth | 6 (solo level 39 / 59 / 75 at 6-min nights) | 15 + XPPerPlayer 0.35 (25 / 37 / 48 at any team size) |
| Shambler HP night 1 / 2 / 3 | 12 / 22 / 32, flat in the night, same in co-op | 18-29 / 83-133 / 148-237, +35% per extra reaper |
| Enemy damage | flat (Shambler 8 all run) | 8 -> 15 by the last midnight |
| Boss HP | 1,500 / 2,600 / 4,200-7,500 (TTK 2-15 s: model says they melted) | 31,000 / 88,000 / 156,000-178,000 |
| Shop | flat 40 / 80 / 60 / 120 (10+ buys per visit at 6-min nights) | 80 / 160 / 120 / 240 base, escalating, 3-5 buys |
| Soul coins per run (solo) | ~1,230 (0.6 runs per unlock) | ~490 (1.4 runs per unlock) |

### Zombies + flyers (Nov 7, gameplay v11)
Enemies.csv now has 18 types. Shambler weight halved (58/45/36 -> 30/23/18) and the other half given to the new zombie **Rotter**;
Lurcher, Ghoul, Bloat (splits into Rotters), Shortchanged, Bomber and Gravedigger now use zombie models. Zombie share of spawns:
53% / 48% / 47% (nights 1-3). New: Fresh Arrival (fast zombie, night 2+), Gravebat (flyer, swoops), Wraith (flyer, phase-blinks).
Their HP / XP were set so the weighted horde matches the old one: model night-3 average horde HP 348 (was 347), team level 48 at
the last midnight (unchanged), hits-to-kill unchanged, boss TTK 0.91-1.13 of target (unchanged).

## 4. How to retune

1. Change a CSV value, run the model, read section 5 (Verdict).
2. **Enemies too tanky/soft:** scale `Health` (early game) or `HealthPerNight` (late game) in Enemies.csv, or `EnemyHealthPerMinute` (the in-night ramp).
3. **Co-op too hard/easy:** `EnemyHealthPerPlayer`, `BossHealthPerPlayer`, `XPPerPlayer`, `ShopPricePerPlayer`.
4. **Bosses:** play, read `[BossTTK]` in the log, and use its "CSV Health for the target" value.
5. **Shop:** the model's "Buys" column should be 3-5. Lower `ShopPricePerBuy` for more buys per visit, lower base `Cost` for a cheaper first buy.
6. **Meta pace:** `KillsPerPay` (kill share of pay), `PayPerNight`, `PayWinBonus`.
7. **Real data beats the model.** The model guesses kill share (75%), pickup (85% XP / 70% souls), soul loss (30%), a 30 s lantern trip and 75% boss uptime (ASSUMPTIONS block at the top of the script). When bot runs log kills and banked souls per night, put the real numbers there.

Known model limits: the whole build is one "main weapon + average of the other weapons"; orbit and turret weapons are
estimated (orbit weapons look strongest on bosses: Wreath of Thorns and Prayer Beads); only the main weapon evolves; the
quota itself (QuotaNight1-3) was not changed. At 6-minute nights the quota is easy for an on-curve team (150 solo against
~950 souls banked). Check that in a real run before release.

## 5. Megabonk comparison

| | Megabonk | Soul Quota (after) |
|---|---|---|
| Enemy HP ramp | ~+10% per minute, linear | +10% per minute inside a night plus a per-night step (Enemies.csv HealthPerNight) |
| Enemy damage ramp | ~+2.8% per minute | +3% per minute, +30% per night |
| Co-op | none (solo game) | +35% enemy HP, +35% spawns and +35% XP per level per extra reaper; boss +75% HP per extra reaper |
| Chest / shop prices | rise every buy (30, 72, 115 ... thousands) | rise every buy (+75% of base), +50% per night, reset each visit, so they never reach thousands solo |
| Free chests (Key, x/(x+1)) | 1 key ~9%, 10 keys 50% | Skeleton Key item, x/(x+1) on paid chests. Luck now has a soft cap: 2 x L / (L + 2) (items/tomes pass) |
| Rarity weights | ~70 / 15 / 6 / 1.5 | 60 / 27 / 10 / 3 (more generous; card power per rarity is small: +0.10 / +0.22 / +0.40 weapon power) |
| Crit | over 100% crits again (recursive) | capped at 100%, one crit tier (readable numbers); the overflow becomes crit damage |
| Boss HP | ~12k -> 500k -> 18M per tier ("bullet sponge" final boss) | 31k -> 88k -> 156-178k, sized for 2.5-4 min fights |
| Damage numbers | grow to millions | tens early, ~140-600 on the final night |

## 6. Economy (Nov 7 economy pass: gold, chests, timeline, Appointment Bell, Final Swarm)

Model sections 6-7 (`python Scripts/balance_model.py`). Everything is modelled at the **release** length (`BalanceNightSeconds 360`);
the test `NightSeconds 90` stays as it is (prices and HP shrink with `NightSeconds / BalanceNightSeconds`, like the office).

**Two currencies, two jobs.** *Gold* is each reaper's own in-run purse (resets every run) and buys **chests**. *Souls* stay the team's
quota and Death's Office money. Soul coins (meta) are untouched apart from the clause multiplier.

**Gold income** (`ASQHordeManager::KillEnemy`, `GiveGold`):
```
normal kill: GoldDropChance 4% -> a coin worth XP x GoldPerXP x (1 + GoldPerNight 1.5 x night) x (1 + Difficulty) x clause GoldMult
elite: a pile of EliteGold 4 (x3 if crowned) x the same growth   miniboss: MinibossGold 20 to EVERY reaper   boss: BossGold 30 to EVERY reaper
co-op shared purse: a coin anyone picks up pays every reaper value / (1 + 0.35 x extra reapers)  (GoldShareCoop 1)
```
The team picks up ~(1 + 0.35 x extra reapers) times a solo crew's coins, so the shared purse gives every reaper about a **solo income**.
Pots and greed shrines (world agent) pay through `ASQPlayerState::AddGold`.

**Chest price** (`ASQGameState::ChestPriceFor`, per reaper; free opens never count):
```
price = ChestBasePrice 30 x (1 + ChestPriceK 1 x paid chests you opened this run) ^ ChestPriceExp 1.4, capped at ChestPriceCap 1500,
        x NightSeconds/BalanceNightSeconds x clause ChestPriceMult
1st..12th chest: 30, 79, 140, 209, 286, 369, 457, 551, 650, 754, 861, 973   (Megabonk: 30, 72, 115 ...)
```
*Co-op decision: per-reaper price, not a team price.* With a team counter one reaper's chest makes every teammate's dearer
(4 players would hit thousands by night 2 and fight over who opens). Per-reaper purses + per-reaper escalation + the shared coin split
mean a 4-player crew pays exactly solo prices, nobody's chest costs a friend anything, and the cap stops a lucky farmer running away.
Chest count scales with the crew instead: `ChestsPerNight 6 + ChestsPerExtraPlayer 4` per night (max 24 unopened on the map).

**Free chests:** crowned elites (`CrownedChance 30%` of elites, `CrownedChestChance 15%`), the night's second miniboss (one per reaper,
Timeline.csv `FreeChest`), and the boss (every reaper gets a chest roll). The **Skeleton Key** item (Upgrades.csv, quest-locked until
5 chests) makes a paid chest free with chance `x / (x + 1)`, x = 0.1 per pick (1 Common pick 9%, 5 picks 33%, 5 Legendary 56%).

| Per reaper (any crew size) | Night 1 | Night 2 | Night 3 |
|---|---|---|---|
| Gold earned | ~350 | ~1,060 | ~1,930 |
| Paid chests (prices) | 3 (30 / 79 / 140) | 3 (209 / 286 / 369) | 3 (457 / 551 / 650) |
| Free chests (solo; 4p) | 2.9; 2.5 | 3.1; 2.6 | 3.5; 2.8 |

Solo run: **~9 paid + ~10 free = ~19 item rolls** (+ ~4 from Lost Property) on top of ~48 level-up picks (a chest = one ITEM
at the opener's Luck + `ChestLuckBonus 0.25`; items stack, see `Docs/ItemsAndTomes.md`).

**Chest power is paid for.** *(Superseded by the items/tomes pass, Section 8.)* Chest relics used to level the same 6 level-up
relics and roughly doubled DPS by night 3, so bosses and the horde got `EconomyBossHealth 1.25 / 1.8 / 2.0` and
`EconomyEnemyHealth 1.05 / 1.2 / 1.25`. Chests now give stacking items spread over 23 kinds, so these are now
**0.87 / 0.9 / 1.3** and **1.05 / 1.05 / 1.0** (Endless uses night 3).

**Run timeline (Timeline.csv).** Per night: miniboss at 30% (gold) and 70% (gold + a free chest each), 1-2 announced swarm waves.
Minibosses are scaled-up elites (`HealthMult` 5-30, `DamageMult` 1.3-2) with a big crown, a red ground ring and a HUD name bar.

**Appointment Bell + Final Swarm.** The boss no longer rises at midnight. A bell (deterministic spot per run seed + night, at least
`PortalMinLanternDist 2200` from the lantern) can be rung (E, hold 2 s in its circle) from `PortalMinProgress 0.5` (10:30 PM).
Ringing **closes the quota** (Defiance, unbanked souls lost, as midnight used to), stops the clock and raises the boss at the bell.
Midnight without the bell starts the **Final Swarm**: the normal horde at full strength plus Timeline tiers (Wraiths, then Fresh
Arrivals at +1 min, Gravebats +3, Brutes +5), all growing `+50%/min` spawn, `+30%/min` HP, `+15%/min` damage, until the bell is rung
and the boss beaten. Farming longer = more XP, gold and souls (the quota stays open), but every minute after midnight costs more.
Beating the boss vacuums every pickup (XP to the team, gold shared, souls to the office).

| Run length (release values, solo) | Win | Loss after 2 nights |
|---|---|---|
| Standard (bell at ~11:50 PM) | 28.9 min | 27.2 min |
| Rush (bell at 10:30 PM every night: lower level, longer bosses) | 29.6 min | 27.2 min |
| Farm (60 s of Final Swarm every night) | 32.0 min | 30.5 min |

Inside the 20-30 min target (rushing only pays off for a strong build: the model's fixed build makes the bosses slower).
At the test length (90 s) a standard win is ~14 min.

**Difficulty** (`ASQGameState::AddDifficulty`, greed shrines and clauses): spawn rate and gold x(1 + Difficulty), enemy HP
x(1 + 0.5 x Difficulty).

## 7. Meta pacing (quests, clauses, reaper costs)

Model section 8: a new **solo** player plays back to back at release values (assumed outcomes: lose, lose, lose, win, then ~62% wins;
clauses from run 15). Soul coins = the old pay x the clause multiplier, plus quest coins.

**Quests.csv** - 81 "Performance Reviews" in 7 categories (Reapers 17, Combat 15, Progress 12, Challenge 12, Economy 10,
Boss 9, Mastery 6). Rewards: soul coins, a free hire of a reaper (the reaper's old "planned unlock" text is now a real quest), or an item
(11 items - 5 weapons, 6 relics incl. the Skeleton Key - stay out of level-up cards and chests until earned; a reaper that starts with
one still has it; `UnlockAllItems 1` while testing).

**Co-op rule:** the server credits each reaper's *own* events (kills, chests, gold, dashes, damage taken, revives) to that reaper and
*team* events (nights cleared, bosses, quota met, souls banked) to everyone in the run. Every PC keeps its own profile (local SQSave);
bots never count. Progress is sent in batches every `QuestFlushSeconds 2`.

**Contract Clauses (Modifiers.csv, host picks in the lobby):** Double Booking (2x horde, +50%), Glass Reaper (2x enemy damage, +50%),
Rush Hour (2.5x HP/damage ramp, +40%), No Sick Days (no healing, +60%), Overbooked (nights x0.67, +30%), Cost of Living (2x chest
prices, -25% gold, +30%), Management Visit (3x elites, all crowned, +40%), Quota Crunch (1.5x quota, +30%), Probation (no level-ups
before 10 PM, +30%). Soul coins x(1 + sum of bonuses), capped at `ModifierCoinCap 3`.

**Reaper hire costs (Reapers.csv UnlockCost):** 150, 300, 450, 650, 900, 1,200, 1,500, 1,800, 2,200, 2,600, 3,000, 3,400, 3,800,
4,200, 4,600, 5,000, 5,500 (sum 41,250; was 10,200). The Intern and Femur now have a coin price too (they were free with only a planned
quest). Every one of these can also be hired free by its quest.

| Play time | Runs | Quests done | Reapers hired | Items unlocked |
|---|---|---|---|---|
| 15 min | 0 (mid-run) | 8 | 0 | 2 |
| 30 min | 1 | 11 | 2 | 3 |
| 45 min | 2 | 12 | 2 | 3 |
| 60 min | 2 | 12 | 2 | 3 |
| 2 h | 5 | 17 | 5 | 4 |
| 5 h | 11 | 35 | 11 | 9 |
| 10 h | 22 | 55 | 16 | 11 |

| Target | Model |
|---|---|
| First unlock within ~15 min | **8.7 min** (First Day on the Job; the first run's night 1 alone finishes 8 quests incl. the Skeleton Key and Candle Beam) |
| 4+ meaningful unlocks / rewards in hour 1 | **14** (12 quests incl. 3 item unlocks, 2 reaper hires) |
| Steady goals through ~10 h | 4-9 unlocks every hour except hour 7 (0: the run of expensive hires) - longest gap 67 min |
| All 21 reapers | 11.6 h (coins; quests hire some sooner) |
| Full completion 30-50 h | 75 / 81 quests by 40 h, 78 by ~110 h; never in the model: Beak / Solace (co-op revives only - coins still hire them) and "all 9 clauses" (a trophy) |

Assumptions (top of the economy block in the script) are the weak point: survival odds, damage taken, dashes, shrines used, how often
players ring early or farm the swarm. Replace them with real numbers from the first playtests (the quest stats are logged in SQSave).

## 8. Items and tomes pass (Nov 7; full write-up: `Docs/ItemsAndTomes.md`; model sections 9-11)

Megabonk-style split: **4 weapon slots** (levels 1-8) and **4 tome slots** (16 tomes = the old level-up relics, levels 1-5, a fixed
gain x rarity) from level-ups; **23 items** from chests / Lost Property (no slots, they stack); evolutions = max weapon + its
paired tome. Once slots are full, cards only offer what you own; when little is left, filler cards (gold / team XP / heal).
All stats are summed raw, then `SQ::FinalizeStats` applies: attack speed 1/(1+x) floored at 40%, crit 100% (overflow -> crit
damage), area and duration 3x, armor x/(x+1) up to 75%, evasion x/(x+1) up to 40%, luck soft cap 2 (Tuning.csv).

| Number | Before | After |
|---|---|---|
| Tome / relic value per Common pick (x median, night 2) | 0.30-2.11 (Hourglass 2.11 dominant; Thorned Shroud 0.30 dead) | 0.80-1.13 |
| Item value per stack (x median) | - | 0.78-1.18 |
| Weapon score at max level (role-weighted) | 0.41-1.84, 16 / 24 outside 0.8-1.2 | 0.91-1.04 |
| Evolution spike | -65% .. +126% | +50% .. +60% |
| Boss TTK / target | 0.87-1.12 | 0.90-1.08 |
| Hits-to-kill night 3, main weapon, solo / 4p | 1.7-2.7 / 3.4-5.6 | 2.1-3.1 / 4.4-6.6 |
| Biggest damage number | 540 | ~210 |
| `EconomyBossHealth` / `EconomyEnemyHealth` (night 1 / 2 / 3) | 1.25 / 1.8 / 2.0 and 1.05 / 1.2 / 1.25 | 0.87 / 0.9 / 1.3 and 1.05 / 1.05 / 1.0 |
| Standard win run length | 28.9 min | 28.4 min |

| Build (night 3) | Single DPS | EHP | Boss TTK (target 210 s) |
|---|---|---|---|
| Typical solo (evo tome + attack speed + HP + crit chance) | 1231 | 341 | 220 s |
| Weapon-heavy solo (4 weapons, 3 tomes) | 1228 | 213 | 220 s |
| Tome-heavy solo (2 weapons; plateaus once maxed) | 811 | 213 | 334 s |
| Crit solo (glass cannon) | 1745 | 219 | 155 s |
| Support in a 4-reaper crew | 548 | 553 | 223 s (team) |

Known limits: the horde model ignores Drop radius and caps Arc / Nova targets at 14, so the Rubber Stamp / Tombstone cuts may
be too big; crit chance + crit damage together are ~1.4x a non-crit offence build at night 3 (each card is in band on its own).

## 9. Collision radius note (Nov 7)
Enemy body radius is no longer just Enemies.csv `Radius`: `ASQHordeManager::EnemyRadius` blends it toward the measured width of the
impostor art (`CollisionArtFit` 1 = art), clamped 5-200 cm. Weapon / area / line / chain hits, crowd spacing, body blocking and the slide
shove (`SlideKnockMaxEnemyRadius` 62: Lurcher 59 in; Spitter 65, Bloat 90, Brute 110 out) all use it. Melee lands at body + reaper
capsule (40) + `Reach`. The balance model still uses the CSV radii, so check `[Collision]` log lines after import: if an art radius
differs a lot from the CSV, hit rates and area-weapon value move with it.

## 10. Missions (Nov 9 2026 rebuild; model sections 13-15)

The run is ONE mission stage (Missions.csv, one map per biome). Sections 4, 6 and 8 of the model (old soul coins, 3-night run length,
old meta pacing) are retired behind `LEGACY_NIGHT_RUN = False`. Everything below is at RELEASE length (Missions.csv Duration; Tuning
`NightSeconds 360`). The test knob still works: a stage lasts Duration x NightSeconds / BalanceNightSeconds, and objective timers,
fill times, nest HP and Bank Souls counts shrink with it (at the current test value 90 a 16-minute map lasts 4 minutes; time limits
and the hearse speed are never compressed past half). Use NightSeconds 360 for the first real mission test.

**Acts.** A stage is 3 acts (Duration / 3). `ASQGameState::NightIndex` = the act, so Enemies.csv Weight1-3, HealthPerNight,
EnemyDamagePerNight, GoldPerNight and Timeline.csv "Nights" keep working. The per-minute HP ramp restarts each act; the horde's HP
is multiplied by `ASQGameState::StageHealthSmoothing` so it eases log-linearly from one act start to the next (anchors: the old night
starts and the old last midnight; `StageHpRefRatio 3` = the horde's average HealthPerNight / Health). Enemy damage still steps +30%
per act (small; flagged for playtest).

**Mission difficulty** rides on top (`ASQGameState::ModProduct` multiplies the mission like an always-on clause; HP through
`EconomyHealthMult`):
```
horde HP     x HealthMult      (Hard x1.5)        spawn rate x SpawnMult (Hard x1.2)
enemy damage x DamageMult      (Hard x1.35)       gold       x GoldMult  (Hard x1.5)
boss HP = Bosses.csv Health x (1 + 0.75 x extra reapers) x (1 + 0.25 x Defiance) x EconomyBossHealth3 x BossHealthMult (Hard x1.4)
Defiance = failed objectives (1 stack each)
```

| Mission | Stage | Bell (min) | Level at bell | Need | Boss TTK solo / 4p (target) | Hits to kill at the end (solo) | Gold earned / banked on a win (solo) |
|---|---|---|---|---|---|---|---|
| Graveyard | 16 | 12.9 | 36 | 0.79 | 159 / 138 s (210) | 2.9 | 2,607 / 413 |
| Drowned Harbour | 17 | 13.8 | 39 | 0.93 | 174 / 151 s (210) | 3.2 | 3,769 / 627 |
| Plague Carnival | 18 | 14.7 | 41 | 1.12 | 177 / 154 s (210) | 3.9 | 4,791 / 825 |
| Frozen Monastery | 19 | 15.6 | 44 | 1.33 | 162 / 142 s (210) | 4.7 | 6,328 / 1,109 |
| Clocktower | 20 | 16.3 | 47 | 1.56 | 185 / 162 s (240) | 5.4 | 8,104 / 1,422 |
| Hard modes | | same | +3-4 | 1.25-2.47 | 193-209 s solo | 4.3-8.1 | x1.7 |

"Need" is the model's difficulty bar (1.0 = the old 3-night run's on-curve tuning). Win chance:
`P = 1 / (1 + (need / power)^4)`, `power = skill x offence^0.6 x defence^0.4`. Skill: 0.74 for a new player, closing to 1.04 over
~15 runs. Offence / defence come from the meta upgrades through the Build model (DPS and effective HP at the bell vs no upgrades).
Augments (~5 per run) are part of the on-curve power and add 6% boss DPS each in the TTK.

**Objective schedule** (`ObjectiveFirstDelay 20`, `ObjectiveSpread 1`, `ObjectiveEarlyFactor 0.88`, `ObjectiveGap 15`): objective k
is scheduled at k / count of the stage; finishing fast pulls the next one forward by up to 12%. The bell rises after the last
objective (~81% of the stage), so the boss is fought at roughly the old night-3 level. Time limits: 120-200 s (`ObjectiveTime<Type>`).

**Gold and banking (one meta currency).** In-run gold is unchanged (coins, elite piles, minibosses, boss, chests priced per reaper)
plus objective gold (`ObjectiveGold 40` x act growth), nest piles and souls banked at the lantern (`SoulGold 0.25`/soul, shared).
At the end each reaper banks `earned x BankShareWin 0.12` on a win (+ ClearGold x Hard 1.5, + FirstClearGold / HardFirstClearGold once
per profile) or `earned x BankShareLoss 0.1 x max(0.5, stage share)` on a loss, x the clauses' multiplier. Earned = every coin, so
buying chests never costs the bank. Old soul coins = gold 1:1. Quest "Coins:N" rewards now bank N gold.

**Death's Office** (MetaUpgrades.csv, 14 lines, 84 ranks, 209,220 gold; reapers 41,250 more): next rank = BaseCost x CostGrowth^rank
(rounded to 5). First ranks 450-1,300 (Pardon 12,000). Everything bought: offence x2.33, defence x2.21 at the Clocktower bell; a
practised player's Clocktower win chance goes 16% -> 84%, Clocktower Hard 3% -> 44%.

**Pacing (a new solo player, median of 25 seeds; greedy buyer: cheapest upgrade or hire first; farms the richest cleared map when the
next one is below 28%):**

| Goal | Target | Model |
|---|---|---|
| Map 1 cleared | after 1-3 attempts | median attempt 2 (spread 1-7), at ~28 min |
| Hour 1 | 2-4 upgrades, 1-2 new reapers | 3-4 upgrades, 3 reapers (the three cheapest reapers cost 150 / 300 / 450: raise Keeper to ~900 in Reapers.csv for 2) |
| Steady goals to ~10 h | | maps 2 / 3 / 5 cleared at ~1 / 2 / 6.4 h; first Hard clears from ~8 h; 37 / 84 ranks and 71 quests at 10 h |
| Hard modes + completion | 30-50 h | all Hard modes ~29 h; everything (every Hard, rank and reaper) ~45 h |

| Hours | 1 | 2 | 5 | 10 | 20 | 30 | 40 | 50 |
|---|---|---|---|---|---|---|---|---|
| Runs | 4 | 8 | 20 | 39 | 77 | 114 | 154 | 172 |
| Maps cleared | 2 | 3 | 4 | 5 | 5 | 5 | 5 | 5 |
| Hard cleared | 0 | 0 | 0 | 3 | 4 | 5 | 5 | 5 |
| Meta ranks (of 84) | 4 | 6 | 19 | 37 | 55 | 71 | 81 | 84 |
| Reapers hired | 3 | 5 | 12 | 15 | 16 | 17 | 17 | 17 |
| Quests | 20 | 24 | 50 | 71 | 81 | 87 | 89 | 89 |

**Retune knobs:** map difficulty = Missions.csv multipliers (Need); boss length = BossHealthMult; stage shape = ObjectiveSpread /
EarlyFactor / time limits; meta speed = BankShareWin / BankShareLoss / ClearGold / MetaUpgrades costs; Hard = Hard* columns.

**Weak points (replace with playtest data):** the win model (skill curve, WIN_K, need weights) is a guess; objective durations
(`OBJ_SECONDS`) are guesses; the model ignores act smoothing (it reads the act ends, where the smoothing is 1); co-op pacing is not
modelled (shared gold keeps each reaper near a solo purse); quests use rough per-run stats. Log real objective times, win rates and
banked gold per run, then update the ASSUMPTIONS block of section 13-15.


## 11. Remake balance pass (Nov 9 2026, after the Swarm-style remake: kits, augments, missions)

**This section is the contract** (targets). **The current numbers are in section 12** (Nov 9 reap core + early curve re-fit, same
simulation, same targets). Everything above it (sections 1-10) describes older passes; where numbers disagree, this
section and `Scripts/balance_model.py` sections 16-25 win. All numbers are at **release length** (Missions.csv Duration;
the test knob `NightSeconds 90`, `UnlockAll* 1` and `AllowCheats 1` are untouched and ignored by the model).

```powershell
cd E:\Projects\SoulQuota
python Scripts\balance_model.py            # unified run simulation + every table + sensitivity (~1 min)
python Scripts\balance_model.py --quick    # same without the sensitivity pass
python Scripts\balance_model.py --legacy   # also print the retired 3-night sections 1-15
```

### 11.1 The contract and where we are

| Target | Contract | Before (old data, same simulation) | After |
|---|---|---|---|
| Normal enemy hits-to-kill, early (1/6 of the stage) | 1-3 | 3.0-12.0 | **1.4-3.0** |
| Normal enemy hits-to-kill, late (at the bell) | 2-5 | 4.1-13.8 | **2.0-3.7** |
| Hard hits-to-kill, late | 3-6 | 5.7-21.0 | **3.1-6.1** |
| Boss TTK / TargetSeconds, every mission x mode x 1-4 players, at that mission's journey meta | 0.85-1.15 | 0.32-0.80 | **0.80-1.16** (38 of 40 cells in band; out: Plague Carnival 4p normal 0.83, Hard 0.80) |
| Reaper clear speed vs its role target | +-12% | 16 of 21 out | **18 of 21 in**; Tallow +13% (kept for readable Wick Bolt numbers), Intern -7% / Collector -10% clear (power 0.82) |
| Shared weapons at max level (role-weighted score) | +-15% | 6 out | **0.98-1.02** |
| Evolutions (shared and signature) | +45-65% | 4 out | **+50% .. +59%** |
| Tome / item value per pick | <= 1.3x the median of its rarity | 7 above | **none above** (Red Ink, Highlighter, Split Soul are weak, not dominant) |
| Augments: Silver ~1.0 / Gold ~1.5 / Prism ~2.2 x median Silver (+-30%) | per tier | 26 of 37 out (Chain Reaction 15.8x) | **37 of 37 in** |
| Unbounded augment loops | none | chains, refunds, Pardon stacking | **capped in code** (11.4) |
| Co-op: per-person pace vs solo | about 1.0 | 0.41-1.39 | **0.80-1.20** |
| Maxed meta power (offence^0.6 x EHP^0.4) | <= ~1.6 | x3.08 | **x1.53** (offence x1.65, EHP x1.36) |
| First mission clear | 1-3 attempts, 30-60 min | attempt 2, 28 min | **attempt 3 (10-90%: 1-5), 43 min** |
| Hour 1 | 2-4 upgrades + 1-2 reapers | 4 + 3 | **2 + 1** (and 19 quests) |
| Steady goals to 10 h | | | maps 1-5 by 2.7 h, first Hard clears from ~5 h, 26 / 50 ranks and 15 reapers at 10 h, longest dry spell ~45-65 min |
| Hard modes + completion | 30-50 h | 5 h / 23 h | **all Hard 22 h, everything 40 h** (Hard a little early - see 11.6) |
| Readable numbers | tens to low thousands | ability crits 8,368 | weapon crits <= ~900, ability crits <= ~3,400 (single R crits on Digby / Keeper / Beak reach ~4k) |

"Before" is the original data run through the new simulation (so the before/after compare like with like). The old analytic
sections said boss TTK 0.76-0.88 solo; the run simulation shows the real gap was far bigger once the signature weapon, E / R,
chest items, shrine raises and ~5 augments are all in one build.

### 11.2 The run simulation (one coherent model)

`sim_run()` plays one mission second by second (5 s steps) for a crew of 1-4 reapers, each with its own build:

- **Stage:** Duration, 3 acts, spawn rate lerp(SpawnRateStart, SpawnRatePeak) by act progress x crew x SpawnMult; enemy HP and hits
  exactly as `SpawnEnemyEx` / `SimEnemies` (act step, per-minute ramp, crew, EconomyEnemyHealth, mission x Hard multipliers,
  `StageHealthSmoothing` and the new damage smoothing); kill share = 0.75 x clear^0.3 (a weak build kills less and levels slower).
- **Level-ups:** team XP (XPBase / XPGrowth / XPPerPlayer, Night School, Study Group); every reaper gets 3 cards from the real pool
  rules (`CanTakeUpgrade`: signature + 4 shared weapon slots, 4 tome slots, evolutions = max weapon + its tome, card Weight, rarity by
  Luck) and an AI picks: evolution > signature > its evolution tome > weapons > damage / attack speed / crit tomes > one defence tome;
  it keeps the last tome slot for the evolution tome, rerolls junk and skips a dead tome rather than burn a slot.
- **Gold and chests:** coins, elite piles, banked souls (shared purse), objective / nest / miniboss gold, pots; per-reaper chest prices
  (ChestBasePrice x (1 + opened)^1.4) while chests are on the map; free chests (objective rewards, last miniboss, crowned elites,
  shrines), Lost Property, Skeleton Key, shrine raises.
- **Objectives** on the director's schedule (FirstDelay + Duration x Spread x k / n, EarlyFactor, Gap, time limits); completion time from
  the crew's power (nest HP vs siege + hit share, elite kills from the horde + hunting, souls per second for Bank Souls); a failure is a
  Defiance stack. **Minibosses** at MinibossAt (augment offers at the 1st / 2nd kill, chest at the last). **Augments** at levels 10 / 20,
  minibosses and Augment objectives with the real tier odds per source; the AI takes the best value (section 20).
- **Kit:** E / R / passive through `kit_power` (cooldowns with haste, E / R evolution at max tome, refunds); marks never count against
  bosses (`MarkToughestEnemy` only marks horde enemies).
- **Boss:** HP = Health x (1 + BossHealthPerPlayer x extra) x (1 + 0.25 x Defiance) x EconomyBossHealth3 x BossHealthMult (x Hard);
  team single-target DPS (no OnHit augments: the boss takes damage through `TakeBossDamage`) x 75% uptime. Overtime = the Final Swarm.
- **Win proxy:** power = skill x offence^0.6 x defence^0.4, offence = sqrt(clear at the bell) x sqrt(TargetSeconds / TTK), defence =
  hits-to-down / 16 x lives (Pardons, co-op revives). P(win) = 1 / (1 + power^-4), calibrated so a brand-new player's first Graveyard
  attempt with no upgrades wins ~35%.
- **Journey:** the meta-pacing player (section 23) uses the sim's P(win), run length and gold. The Death's Office ranks it owns when it
  first beats each mission (normal / Hard) are the "expected meta" every table above is measured at:
  Graveyard 1 / 10, Harbour 2 / 23, Carnival 4 / 13, Monastery 5 / 38, Clocktower 8 / 37 (of 50).

### 11.3 Results

Per mission, mode and crew (12 seeds each; the reaper rotates through 12 reapers):
```
  Mission         Mode P  Meta Bell  Lvl  Objs ok   Hits early  Hits late    Clear  HTD   BHTD  Boss TTK/tgt  MaxNum W/A  RunMin Pwin 
  Graveyard       norm 1  1    12.9m 40   5.0/5      1.4         2.0 ( 5.2)  4.32   10.6  8.3    210 s 1.00     268/698   17.1    68%
  Graveyard       norm 2  1    12.7m 39   5.0/5      2.3         3.0 ( 7.5)  3.83   15.6  12.3   243 s 1.15     277/1613  17.5    82%
  Graveyard       norm 3  1    12.7m 38   5.0/5      2.3         3.7 ( 8.5)  3.63   19.2  15.1   222 s 1.06     292/1553  17.1    88%
  Graveyard       norm 4  1    12.6m 39   5.0/5      2.6         3.4 ( 8.3)  3.65   17.8  13.9   200 s 0.95     401/2182  16.7    92%
  Graveyard       HARD 1  10   13.0m 42   5.0/5      2.3         3.2 ( 9.5)  2.16   3.7   5.2    190 s 0.91     310/875   16.9    33%
  Graveyard       HARD 2  10   12.8m 40   5.0/5      3.2         3.7 ( 9.1)  2.03   5.0   7.0    199 s 0.95     372/2016  16.8    48%
  Graveyard       HARD 3  10   12.7m 40   5.0/5      3.7         4.6 (13.8)  1.70   6.6   9.3    210 s 1.00     401/1696  17.0    58%
  Graveyard       HARD 4  10   12.7m 41   5.0/5      4.2         6.1 (14.0)  1.84   6.4   9.0    222 s 1.06     422/2242  17.1    64%
  DrownedHarbour  norm 1  2    14.2m 44   5.0/5      1.5         2.4 ( 5.6)  2.91   11.6  9.7    212 s 1.01     240/1117  18.5    76%
  DrownedHarbour  norm 2  2    13.8m 43   5.0/5      1.5         2.1 ( 5.3)  3.95   18.8  15.2   223 s 1.06     325/1484  18.2    86%
  DrownedHarbour  norm 3  2    13.5m 43   5.0/5      1.9         3.3 ( 5.9)  4.06   16.9  13.6   217 s 1.03     395/1470  17.9    89%
  DrownedHarbour  norm 4  2    13.5m 43   5.0/5      2.1         3.1 ( 6.3)  3.64   19.4  15.5   205 s 0.97     411/2204  17.7    92%
  DrownedHarbour  HARD 1  23   14.2m 47   5.0/5      2.3         3.1 ( 7.6)  1.72   4.2   6.3    217 s 1.03     366/880   18.6    26%
  DrownedHarbour  HARD 2  23   13.8m 46   5.0/5      2.4         3.3 ( 8.7)  2.38   6.3   9.1    243 s 1.16     469/1906  18.5    49%
  DrownedHarbour  HARD 3  23   13.5m 45   5.0/5      3.2         4.5 ( 9.5)  2.31   5.8   8.2    201 s 0.96     514/2178  17.6    60%
  DrownedHarbour  HARD 4  23   13.5m 45   5.0/5      3.3         4.2 (11.4)  2.31   6.5   9.4    209 s 0.99     565/2498  17.7    71%
  PlagueCarnival  norm 1  4    14.8m 45   6.0/6      2.1         2.9 ( 6.7)  4.24   13.5  13.2   201 s 0.96     213/783   18.9    78%
  PlagueCarnival  norm 2  4    14.8m 45   6.0/6      2.0         2.4 ( 5.3)  3.64   15.5  15.1   197 s 0.94     399/1656  18.8    84%
  PlagueCarnival  norm 3  4    14.8m 44   6.0/6      2.4         3.5 ( 7.3)  3.14   20.9  20.5   212 s 1.01     371/1472  19.0    91%
  PlagueCarnival  norm 4  4    14.8m 45   6.0/6      2.7         3.3 ( 7.6)  3.33   16.8  16.5   173 s 0.82     623/2722  18.4    92%
  PlagueCarnival  HARD 1  13   14.8m 47   5.5/6      2.9         4.1 ( 9.8)  2.12   4.1   6.5    216 s 1.03     351/905   19.1    26%
  PlagueCarnival  HARD 2  13   14.8m 46   6.0/6      3.6         4.4 ( 9.5)  1.78   5.6   9.8    216 s 1.03     410/1869  19.1    45%
  PlagueCarnival  HARD 3  13   14.8m 46   6.0/6      3.8         4.8 (12.3)  1.83   6.1   10.6   213 s 1.02     586/2191  19.1    53%
  PlagueCarnival  HARD 4  13   14.8m 46   6.0/6      4.8         5.5 (12.1)  1.73   6.2   10.9   167 s 0.80     709/3104  18.3    62%
  FrozenMonastery norm 1  5    16.2m 50   6.0/6      1.9         2.5 ( 5.2)  3.03   11.5  10.0   206 s 0.98     287/1098  20.4    67%
  FrozenMonastery norm 2  5    15.9m 49   6.0/6      2.3         3.1 ( 5.5)  2.83   16.9  14.4   240 s 1.14     365/1880  20.7    80%
  FrozenMonastery norm 3  5    15.8m 50   6.0/6      2.3         2.7 ( 7.2)  3.03   12.1  10.3   215 s 1.02     495/2399  20.1    81%
  FrozenMonastery norm 4  5    15.8m 47   6.0/6      2.8         3.1 ( 8.7)  3.01   15.7  13.3   204 s 0.97     813/2463  19.9    91%
  FrozenMonastery HARD 1  38   16.2m 55   6.0/6      2.7         3.7 ( 7.6)  1.87   4.0   6.3    210 s 1.00     475/1205  20.6    28%
  FrozenMonastery HARD 2  38   16.0m 53   6.0/6      2.9         3.3 ( 8.2)  1.80   5.2   8.0    206 s 0.98     649/2412  20.2    44%
  FrozenMonastery HARD 3  38   15.9m 52   6.0/6      4.0         3.8 ( 8.6)  1.71   5.2   8.1    219 s 1.04     856/2388  20.3    52%
  FrozenMonastery HARD 4  38   15.8m 55   6.0/6      3.7         4.5 (11.2)  1.66   6.2   9.4    213 s 1.01     774/3405  20.1    58%
  Clocktower      norm 1  8    16.9m 54   6.0/6      2.0         2.2 ( 5.5)  2.93   9.5   8.0    253 s 1.05     521/1027  21.8    62%
  Clocktower      norm 2  8    16.6m 53   6.0/6      2.3         2.8 ( 5.8)  2.86   13.1  10.8   258 s 1.08     445/1865  21.7    78%
  Clocktower      norm 3  8    16.5m 54   6.0/6      3.0         3.3 ( 7.7)  3.55   12.9  10.6   241 s 1.00     531/2300  21.3    82%
  Clocktower      norm 4  8    16.4m 56   6.0/6      2.9         2.8 ( 7.2)  3.48   16.6  13.4   222 s 0.93     892/2968  20.9    91%
  Clocktower      HARD 1  37   17.1m 57   6.0/6      2.6         3.5 ( 8.1)  1.64   4.2   6.3    244 s 1.02     529/1108  21.9    23%
  Clocktower      HARD 2  37   16.8m 56   6.0/6      3.2         3.5 ( 8.6)  1.90   4.9   7.3    218 s 0.91     600/2067  21.1    43%
  Clocktower      HARD 3  37   16.6m 56   6.0/6      4.3         4.0 (11.6)  1.99   5.9   8.5    219 s 0.91     730/2431  21.0    55%
  Clocktower      HARD 4  37   16.6m 54   6.0/6      4.5         4.9 (12.5)  2.01   5.0   7.3    244 s 1.02     772/3260  21.4    55%
```
Meta tiers (solo, ranks bought cheapest first):
```
-- 17. Meta tiers (solo): Death's Office ranks 0 / half / all (greedy cheapest-first order). Boss TTK / target and P(win) for a practised player
  Mission         Mode 0 ranks: TTK  Pwin  Hits   25 ranks                   50 ranks (max)            
  Graveyard       norm 1.11   59%   1.3           1.16   76%   1.4           0.68   85%   1.3          
  Graveyard       HARD 1.00   38%   2.9           0.75   50%   3.1           0.53   68%   2.1          
  DrownedHarbour  norm 0.88   76%   2.7           0.77   75%   2.4           0.63   90%   1.5          
  DrownedHarbour  HARD 1.06   14%   3.1           1.15   23%   2.7           0.80   52%   3.0          
  PlagueCarnival  norm 1.06   60%   3.0           0.93   71%   2.9           0.61   86%   2.7          
  PlagueCarnival  HARD 1.03   20%   5.8           0.92   29%   3.2           0.76   59%   2.5          
  FrozenMonastery norm 1.00   61%   2.3           0.89   72%   1.7           0.81   87%   1.4          
  FrozenMonastery HARD 1.23   11%   4.8           0.98   17%   3.9           0.85   43%   3.2          
  Clocktower      norm 0.85   65%   1.5           0.73   78%   1.5           0.64   89%   1.2          
  Clocktower      HARD 1.23    9%   5.6           1.05   17%   4.5           0.96   39%   1.8          
   Maxed meta vs none (PlagueCarnival bell): offence x1.65, defence (EHP) x1.36, combined power x1.53 (contract <= ~1.6).  First 4 ranks: offence x1.04, EHP x1.12
```
One run in detail:
```
-- 18. Curves through one run (PlagueCarnival, solo, journey meta; seed 0, Grim): every 2 minutes
  norm  min: lvl / avg enemy HP / hits-to-kill / enemy hit / hits-to-down / clear / items / augments
         0:   1     21  2.6  10.3  12.6 0.40  0 0
         2:   9     49  3.0  12.2  11.8 0.60  2 0
         4:  17     78  2.1  13.5   9.4 0.67  4 1
         6:  26    119  1.1  13.4   9.7 16.79  8 3
         8:  29    178  1.6  15.9   8.1 4.46 10 4
        10:  35    224  2.0  17.4   8.7 3.06 12 4
        12:  42    205  1.8  16.5  10.7 22.09 16 4
        14:  44    299  2.6  19.6   9.2 5.65 17 5
    objectives: DestroySpawners ok 120s, DefendPoint ok 58s, KillElite ok 14s, Escort ok 95s, BankSouls ok 26s, SurviveWave ok 50s
    boss 126 s (target 210), bell at 14.8 min, defiance 0, augments ['GlassEdge', 'ChainReaction', 'PardonSlip', 'RuthlessRhythm', 'SpellEcho', 'TwinStones']
  HARD  min: lvl / avg enemy HP / hits-to-kill / enemy hit / hits-to-down / clear / items / augments
         0:   1     38  4.6  32.8   4.4 0.19  0 0
         2:   9     88  4.6  39.0   4.4 0.32  2 0
         4:  17    140  5.5  43.2   4.2 0.40  5 1
         6:  26    214  3.1  42.8   4.5 4.82  8 3
         8:  29    321  4.4  51.0   4.0 1.66 11 4
        10:  36    402  5.2  55.8   3.9 1.14 14 4
        12:  43    370  4.0  52.8   4.6 8.32 17 4
        14:  45    537  5.7  62.7   3.9 2.94 19 5
    objectives: DestroySpawners FAIL 188s, DefendPoint ok 58s, KillElite ok 16s, Escort ok 95s, BankSouls ok 26s, SurviveWave ok 50s
    boss 195 s (target 210), bell at 14.8 min, defiance 1, augments ['Shrapnel', 'ExtraHolster', 'RuthlessRhythm', 'OrbitingStones', 'ChainReaction', 'CrashCourse']
```
Co-op:
```
-- 19. Co-op scaling (normal, journey meta): per-person pace = team clear ratio at 60% of the stage vs solo's (1.00 = same pace per person)
  Mission         Clear per person P2/P3/P4    Boss TTK/tgt P1/P2/P3/P4     Objectives time P1/P4   
  Graveyard       0.96 / 0.93 / 0.94           1.00 / 1.15 / 1.06 / 0.95     264 /  158 s
  DrownedHarbour  0.96 / 0.93 / 0.80           1.01 / 1.06 / 1.03 / 0.97     311 /  238 s
  PlagueCarnival  1.20 / 1.19 / 1.00           0.96 / 0.94 / 1.01 / 0.82     348 /  245 s
  FrozenMonastery 0.99 / 1.05 / 0.91           0.98 / 1.14 / 1.02 / 0.97     443 /  304 s
  Clocktower      0.98 / 0.89 / 0.84           1.05 / 1.08 / 1.00 / 0.93     418 /  317 s
```
Kits:
```
-- 22. KITS: clear speed (0.6 horde + 0.4 single-target incl. kit and passive) vs the median reaper, from full run sims
   (solo, PlagueCarnival normal, journey meta, 10 seeds per reaper, at the bell). Contract: within +-12% of the role target (ROLE_CLEAR)
  Reaper       Role         Target Clear  Horde  Single Util%  Power  TTK/tgt MaxAb  Flag
  Tallow       Striker      1.08   1.22   1.10   1.39   3      1.02   0.74    682    STRONG
  Monk         Striker      1.08   1.18   1.00   1.44   9      1.05   0.76    766    
  Vexa         Berserker    1.08   1.11   1.07   1.17   14     1.03   0.83    209    
  Lookout      Scout        1.00   1.09   0.93   1.32   19     1.06   1.06    188    
  Ectoplasm    Summoner     1.00   1.09   1.01   1.21   2      0.90   0.97    510    
  Mort         Hauler       0.98   1.07   0.94   1.28   8      0.95   0.92    580    
  Neckless     Speed        1.04   1.06   1.00   1.15   14     0.98   0.97    269    
  Widow        Control      0.96   1.04   1.11   0.93   20     1.02   1.18    1299   
  Sweep        Mobility     1.00   1.03   1.05   1.00   7      0.90   1.12    422    
  Grim         All-rounder  1.00   1.02   1.06   0.97   12     0.94   1.05    1153   
  Tickwell     Time         0.96   1.02   1.10   0.89   18     0.98   1.11    1958   
  Femur        Fetcher      0.96   1.01   1.17   0.77   28     1.06   1.49    1790   
  Bonesmith    Defender     0.92   0.99   0.81   1.27   26     1.02   0.91    1520   
  Actuary      Economy      0.96   0.99   1.02   0.94   8      0.87   1.12    2270   
  Pallbearers  Mobile bank  0.88   0.97   0.84   1.16   27     1.00   0.91    1063   
  Keeper       Banker       0.92   0.95   1.02   0.84   35     1.05   1.16    3582   
  Digby        Defender     0.92   0.93   0.93   0.92   23     0.94   1.13    4022   
  Intern       Wildcard     1.00   0.93   0.99   0.83   7      0.81   1.27    2062   WEAK
  Solace       Reviver      0.88   0.93   0.91   0.95   42     1.08   1.10    2786   
  Collector    Greed        1.02   0.92   0.83   1.06   7      0.81   1.01    2100   WEAK
  Beak         Healer       0.88   0.91   0.94   0.88   41     1.05   1.20    3298   
  3 / 21 reapers outside +-12% of their role target (or power outside 0.85-1.15)
```
Augment values per pick are in `Docs/ItemsAndTomes.md` section 10; weapons / tomes / items in section 10 there too.

### 11.4 Runaway loops found, and the caps added in code

```
   Runaway checks (late game, level 45, horde HP of the Clocktower end; branching >= 1 = an unbounded chain):
   ChainReaction              blast  218 dmg,  6.6 targets: branching 0.73 (packed crowd 1.75) -> blasts per kill INF uncapped, 0.55 with depth cap 1
   PowderKeg                  blast  255 dmg,  3.9 targets: branching 0.50 (packed crowd 1.25) -> blasts per kill INF uncapped, 0.45 with depth cap 1
   ChainReaction+PowderKeg    blast  237 dmg,  5.3 targets: branching 1.25 (packed crowd 3.13) -> blasts per kill INF uncapped, 1.65 with depth cap 1
   Shrapnel (HitSplit)        bolts per hit 0.240, damage recursion 0.053 (converges: each generation is 0.4x the last)
   Ruthless Rhythm (refund)      4 kills/s: cooldowns tick x2.0 faster uncapped, x2.0 with the cap
   Ruthless Rhythm (refund)     10 kills/s: cooldowns tick x3.5 faster uncapped, x2.0 with the cap
   Ruthless Rhythm (refund)     20 kills/s: cooldowns tick x6.0 faster uncapped, x2.0 with the cap
   Death's Pardon             MaxStacks 1, character cap 2  |  Holster/Shelf slots clamp +4 (USQAugments::ExtraSlots), MaxStacks 1 each
```
| Loop | Why it ran away | Cap |
|---|---|---|
| Chain Reaction (+ Powder Keg) kill explosions | a blast's kills fire new blasts; at late HP one blast kills ~3-5, so 0.45 x 6.6 targets = branching 2.2 (5+ in a pulled crowd): every kill wiped the screen, limited only by the 256 queue | `FPendingBlast::Depth` + `USQAugments::ActiveBlastDepth`: blasts queued while a blast is applied are one generation deeper; past `AugmentChainMaxDepth 1` they are dropped (SQAugments.h/.cpp). Chain Reaction 45% / 45 dmg -> 20% / 30 |
| Ruthless Rhythm cooldown refunds | 0.25 s per kill at 10-20 kills/s = cooldowns 3.5-6x faster; with the Monk's dash strike or Tallow's trail the dash (and its i-frames) was always ready | per-reaper refund budgets (abilities and dash), refilled at `AugmentRefundPerSecond 1` per second, max 1 s banked: cooldowns at most 2x as fast |
| Death's Pardon stacking | MaxStacks 0 (any number) and a character cap of 9 | Augments.csv MaxStacks 1; `MaxPardons 2` in `USQAugments::ApplyPicked` (meta + augment) |
| The Actuary's audit blast | its kills re-enter `OnOwnerKill` synchronously (stack recursion) | function-local nesting guard (2 deep) in SQAbility.cpp |
| Shrapnel split bolts | converges (each generation is 0.4-0.9x the last, <= 0.5 bolts per hit); per-frame cap 32 already | none needed |
| Overkill splash | excess shrinks every generation; now also inside the blast depth cap | (depth cap) |
| +1 weapon / tome slot | already clamped to +4 and MaxStacks 1; the problem was the opposite: a bare slot is a trap in a 15-20 min run (fewer levels per weapon) | slot augments now also grant `Value2` level-up picks (4 weapon / 1 tome) in `ApplyPicked` |

Other code changes (formula fixes, all small): enemy hits are eased between acts like HP (`SQHordeStageDamageSmoothing` in
SQHordeManager.cpp, Tuning `StageDamageSmoothing 1`; the old +30% act steps showed as +12% / +6% jumps); boss hits now scale with
the mission / Hard / Glass Reaper damage multiplier at `BossMissionDamageShare 0.5` (`ASQBoss::DamageMult`; a Hard boss used to hit
exactly like the normal one). **Needs a build** (MSVC, unity, warnings as errors: ASCII only, no shadowing, prefixed names).

### 11.5 Sensitivity: the 10 numbers that move the outcome most (tuning guide for playtests)

```
-- 24. SENSITIVITY: +10% on one number at a time; elasticity = % change of the output / 10% (2 seeds; missions 1 + 5 normal, 3 Hard)
  Source    Number                 BossTTK    HitsLate   Power      HitsToDown
  Tuning    SpawnRatePeak          -1.03      -1.80      +0.37      -0.07     
  Assume    SIM_KILL_SHARE         -0.96      -1.80      +0.33      -0.08     
  Assume    XP_PICKUP              -1.09      -1.75      +0.53      +0.17     
  Tuning    RarityRare             -0.88      -1.35      +0.16      -0.53     
  Tuning    XPGrowth               +1.33      +0.43      -0.55      -0.11     
  Enemies   Health*                +0.14      +1.03      -0.19      -0.02     
  Missions  HealthMult*            +0.14      +1.03      -0.19      -0.02     
  Tuning    EconomyBossHealth3     +1.00      +0.00      -0.28      +0.00     
  Assume    BOSS_UPTIME            -0.91      +0.00      +0.29      +0.00     
  Tuning    EnemyDamagePerNight    +0.00      +0.00      -0.15      -0.36     
  Tuning    ChestPriceExp          +0.34      +0.09      -0.15      +0.00     
  Upgrades  Signature Damage*      -0.19      -0.06      +0.09      +0.01     
  Tuning    EnemyHealthPerMinute   +0.00      +0.18      -0.02      +0.00     
  Tuning    StageHpRefRatio        +0.14      +0.00      -0.07      +0.00     
  Kits      E/R Damage*            -0.05      +0.00      +0.02      +0.00     
  Assume    DENSITY                -0.04      +0.02      +0.04      +0.00     
  Tuning    AbilityDamagePerLevel  -0.02      +0.00      +0.01      +0.00     
  Meta      Damage PerRank         -0.02      -0.02      +0.01      +0.00     
  Tuning    GoldDropChance         -0.01      +0.00      +0.00      +0.00     
  Assume    SHRINE_RAISES_STAGE    -0.00      +0.00      +0.00      +0.00
```
1. **XP pace** - `XPGrowth` (and the XP_PICKUP assumption): elasticity ~1.0-1.3 on boss TTK. Level is everything. Tune first.
2. **`SpawnRatePeak`** (and the kill-share assumption): more spawns = more XP = faster everything (-1.0 boss TTK, -1.8 hits).
3. **Enemy `Health` / mission `HealthMult`**: hits-to-kill 1:1, almost no effect on boss TTK. The clean knob for "feel".
4. **`EconomyBossHealth3`** / Missions `BossHealthMult`: boss TTK 1:1, nothing else. Use the `[BossTTK]` log line.
5. **`RarityRare`** (card / chest rarity): -0.8 boss TTK, -1.4 hits. Luck is stronger than it looks.
6. **Missions `HardDamageMult` (3.2)** / `EnemyDamagePerNight`: the only real Hard-mode lever on survival; sets when Hard is beatable.
7. **`BOSS_UPTIME` (assumption, 75%)**: boss TTK ~1:1. Measure it (shield phases, pylons, dodging).
8. **`ChestPriceExp`** / gold drops: +0.35 boss TTK; items matter less than levels.
9. **`BossHealthPerPlayer` (1.0)** and **`EnemyHealthPerPlayer` / `PlayerSpawnRateBonus` (0.2 / 0.65)**: the co-op balance.
10. **`AbilityDamagePerLevel` (0.03)** and Kits.csv E / R damage: small on bosses (abilities are crowd tools), large on clear speed.

### 11.6 What the model guesses (replace with playtest data)

- **The win proxy** (power formula, WIN_K 4, HTD_REF 16, the 35% first-attempt calibration, skill 0.74 -> 1.04 over ~18 runs) is a
  judgement. It drives the journey, so it drives "expected meta", Hard timing and completion hours. Log real win rates per mission.
- **Kill share 0.75 x clear^0.3, XP pickup 85%, souls 0.55 banked per kill:** level pace hangs on these.
- **Objective times:** walking (12 s between nests / urns / elites), escort 95 s, defend fill. Log `[Objective]` times per type.
- **Boss uptime 75%** and the boss damage share a reaper eats (35%).
- **Crowd density** (5 enemies inside 300 cm, ability target caps) and blast kill fractions: augment and kit horde values.
- **Utility values** (control, heals, shields, revives, movement, rerolls 1.5% each, Pardon +35% defence) are judgement; support kits
  are balanced on clear speed against role targets, and their utility is trimmed so power stays 1.08-1.09.
- **The AI player** (card scores, tome preferences, augment choice by value) is one sensible player, not the community meta.
- **Hard modes land at ~22 h** (target 30-50 h): Hard is now gated by enemy damage (x3.2) because meta is capped at +53%. If Hard
  needs to be later, raise `HardDamageMult` a little or Hard boss HP - but check hits-to-down on Hard first (4-6 hits solo).
- **4-player Hard spawns** peak around 22 x 2.95 x 1.5 x 1.2 = ~115 / s on the Clocktower: `MaxEnemies 800` will cap it. Watch the server.
- **Spawn rate resets at every act start** (SimSpawning lerps by act progress): each act opens with a breather. Intended? The HP and
  damage ramps are now smooth; the spawn sawtooth is not.
- **Slot augments, marks and Volley are build-dependent:** values are on a Grim reference build (melee main, mixed weapons).

### 11.7 Changelog (every number changed, before -> after)

Text-only changes (descriptions updated to the new numbers: MetaUpgrades, Augments, five Kits descriptions, five Tuning notes) are not listed.
Code: SQAugments.h/.cpp, SQAbility.cpp, SQHordeManager.cpp, SQBoss.cpp (11.4). Backups: `Saved/Backups/2026-11-09_balance/`.

| File | Row | Changes (before -> after) |
|---|---|---|
| Tuning | PlayerSpawnRateBonus | Value 0.35 -> 0.65 |
| Tuning | XPPerPlayer | Value 0.35 -> 0.65 |
| Tuning | AbilityDamagePerLevel | Value 0.05 -> 0.03 |
| Tuning | EnemyHealthPerPlayer | Value 0.35 -> 0.2 |
| Tuning | BossHealthPerPlayer | Value 0.75 -> 1 |
| Tuning | EconomyBossHealth3 | Value 1.41 -> 2.6 |
| Tuning | ObjectiveTimeKillElite | Value 120 -> 150 |
| Tuning | NestHealth | Value 1500 -> 1100 |
| Tuning | NestHealthPerAct | Value 1 -> 1.2 |
| Tuning | KillEliteHealthMult | Value 4 -> 3 |
| Tuning | StageDamageSmoothing | (row) - -> 1 |
| Tuning | BossMissionDamageShare | (row) - -> 0.5 |
| Tuning | AugmentChainMaxDepth | (row) - -> 1 |
| Tuning | AugmentRefundPerSecond | (row) - -> 1 |
| Tuning | MaxPardons | (row) - -> 2 |
| Missions | Graveyard | HealthMult 0.95 -> 1.1; BossHealthMult 0.5 -> 0.57; HardHealthMult 1.5 -> 1.7; HardDamageMult 1.35 -> 3.2; HardBossHealthMult 1.4 -> 1.11 |
| Missions | DrownedHarbour | BossHealthMult 0.6 -> 0.69; HardHealthMult 1.5 -> 1.8; HardDamageMult 1.35 -> 3.2; HardBossHealthMult 1.4 -> 1.26 |
| Missions | PlagueCarnival | BossHealthMult 0.68 -> 0.73; HardHealthMult 1.5 -> 1.8; HardDamageMult 1.35 -> 3.2; HardBossHealthMult 1.4 -> 1.12 |
| Missions | FrozenMonastery | HealthMult 1.45 -> 1.35; BossHealthMult 0.75 -> 0.98; HardHealthMult 1.5 -> 1.8; HardDamageMult 1.35 -> 3.2; HardBossHealthMult 1.4 -> 1.37 |
| Missions | Clocktower | HealthMult 1.65 -> 1.5; BossHealthMult 0.85 -> 1.18; HardHealthMult 1.5 -> 1.8; HardDamageMult 1.35 -> 3.2; HardBossHealthMult 1.4 -> 1.22 |
| MetaUpgrades | Damage | MaxRank 10 -> 4; BaseCost 640 -> 900; CostGrowth 1.3 -> 2 |
| MetaUpgrades | Health | PerRank 8 -> 9; MaxRank 10 -> 4; BaseCost 520 -> 800; CostGrowth 1.3 -> 2 |
| MetaUpgrades | Regen | PerRank 0.15 -> 0.12; MaxRank 6 -> 4; BaseCost 790 -> 1150; CostGrowth 1.38 -> 2 |
| MetaUpgrades | Speed | PerRank 12 -> 15; MaxRank 5 -> 4; BaseCost 640 -> 900; CostGrowth 1.45 -> 2 |
| MetaUpgrades | Pickup | PerRank 25 -> 30; MaxRank 6 -> 4; BaseCost 450 -> 650; CostGrowth 1.42 -> 2 |
| MetaUpgrades | AttackSpeed | PerRank 0.03 -> 0.025; MaxRank 8 -> 4; BaseCost 790 -> 1150; CostGrowth 1.32 -> 2 |
| MetaUpgrades | Armor | PerRank 0.03 -> 0.02; MaxRank 6 -> 4; BaseCost 950 -> 1300; CostGrowth 1.38 -> 2 |
| MetaUpgrades | Luck | PerRank 0.05 -> 0.04; MaxRank 5 -> 4; BaseCost 1050 -> 1400; CostGrowth 1.45 -> 2 |
| MetaUpgrades | XPGain | PerRank 0.04 -> 0.025; MaxRank 8 -> 4; BaseCost 790 -> 1150; CostGrowth 1.32 -> 2 |
| MetaUpgrades | GoldGain | MaxRank 8 -> 4; BaseCost 640 -> 850; CostGrowth 1.32 -> 2 |
| MetaUpgrades | Revive | BaseCost 1300 -> 1500 |
| MetaUpgrades | Rerolls | BaseCost 1600 -> 1800 |
| MetaUpgrades | HeadStart | MaxRank 3 -> 1; BaseCost 2100 -> 3500; CostGrowth 1.8 -> 2 |
| MetaUpgrades | Pardon | BaseCost 12000 -> 15000 |
| Reapers | Keeper | UnlockCost 450 -> 1400 |
| Reapers | Monk | UnlockCost 1200 -> 2400 |
| Reapers | Intern | UnlockCost 5500 -> 7000 |
| Reapers | Femur | UnlockCost 3800 -> 5200 |
| Reapers | Digby | UnlockCost 150 -> 800 |
| Reapers | Collector | UnlockCost 300 -> 1100 |
| Reapers | Beak | UnlockCost 650 -> 1700 |
| Reapers | Tallow | UnlockCost 900 -> 2000 |
| Reapers | Lookout | UnlockCost 1500 -> 2800 |
| Reapers | Vexa | UnlockCost 1800 -> 3200 |
| Reapers | Actuary | UnlockCost 2200 -> 3600 |
| Reapers | Sweep | UnlockCost 2600 -> 4000 |
| Reapers | Solace | UnlockCost 3000 -> 4400 |
| Reapers | Pallbearers | UnlockCost 3400 -> 4800 |
| Reapers | Ectoplasm | UnlockCost 4200 -> 5600 |
| Reapers | Neckless | UnlockCost 4600 -> 6000 |
| Reapers | Tickwell | UnlockCost 5000 -> 6500 |
| Enemies | Shambler | Health 18 -> 15 |
| Enemies | Lurcher | Health 45 -> 38 |
| Enemies | Ghoul | Health 27 -> 23 |
| Enemies | Wisp | Health 15 -> 13 |
| Enemies | Gravedigger | Health 60 -> 51 |
| Enemies | BoneHound | Health 21 -> 18 |
| Enemies | Spitter | Health 33 -> 28 |
| Enemies | Bomber | Health 30 -> 26 |
| Enemies | Bloat | Health 90 -> 76 |
| Enemies | Brute | Health 450 -> 382 |
| Enemies | Tower | Health 68 -> 58 |
| Enemies | WickRunner | Health 14 -> 12 |
| Enemies | Shortchanged | Health 21 -> 18 |
| Enemies | CoffinHopper | Health 30 -> 26 |
| Enemies | Rotter | Health 20 -> 17 |
| Enemies | FreshArrival | Health 18 -> 15 |
| Enemies | Gravebat | Health 16 -> 14 |
| Enemies | Wraith | Health 26 -> 22 |
| Augments | GraveGrit | Value 0.15 -> 0.2 |
| Augments | ThickShroud | Value 40 -> 25; Value2 0.1 -> 0.06 |
| Augments | ClayBellows | Value 0.2 -> 0.5 |
| Augments | LongReach | Value 150 -> 120; Value2 0.1 -> 0.06 |
| Augments | SteadyHands | Value 0.15 -> 0.2 |
| Augments | GraveDirt | Value 2 -> 1.2 |
| Augments | LuckyBones | Value 0.4 -> 0.8; Value2 0.05 -> 0.1 |
| Augments | SpareChange | Value 0.08 -> 0.12 |
| Augments | Leech | Value2 0.25 -> 0.06 |
| Augments | SecondOpinion | Value 3 -> 8 |
| Augments | PowderKeg | Value3 30 -> 35 |
| Augments | Shrapnel | Value 0.12 -> 0.25; Value2 0.4 -> 0.9 |
| Augments | ExtraHolster | Value2 - -> 4 |
| Augments | ExtraBookshelf | Value2 - -> 1 |
| Augments | OrbitingStones | Value3 20 -> 30 |
| Augments | ReapersMercy | Value 0.12 -> 0.25 |
| Augments | Overkill | Value 0.6 -> 2 |
| Augments | SpellEcho | Tier 1 -> 0; Value2 350 -> 500; Value3 60 -> 300 |
| Augments | TailwindRite | Tier 1 -> 0; Value 250 -> 350; Value2 2.5 -> 3 |
| Augments | CrashCourse | Value 2 -> 3 |
| Augments | PardonSlip | Tier 1 -> 2; MaxStacks 0 -> 1 |
| Augments | GlassEdge | Value 0.35 -> 0.45 |
| Augments | LastStand | Tier 1 -> 0; Value 0.6 -> 1.2; Value2 0.35 -> 0.5 |
| Augments | PocketChest | Tier 1 -> 0; Value 2 -> 4 |
| Augments | Volley | Tier 2 -> 0; Value2 - -> 0.1; Stat2 - -> Damage |
| Augments | ChainReaction | Value 0.45 -> 0.2; Value3 45 -> 30 |
| Augments | TwinStones | Value3 35 -> 22 |
| Augments | MidasShroud | Tier 2 -> 1; Value 0.2 -> 0.3; Value2 4 -> 5 |
| Augments | WakeKeeper | Value 3 -> 1.5 |
| Augments | SwiftRites | Tier 2 -> 1 |
| Augments | StudyGroup | Value 0.2 -> 0.3 |
| Kits | Grim | E_Damage 19 -> 13; E_FinalDamage 19 -> 13; R_Damage 107 -> 74 |
| Kits | Mort | E_Damage 35 -> 43; R_Damage 28 -> 35 |
| Kits | Widow | E_Damage 31 -> 33; R_Damage 18 -> 19; R_FinalDamage 96 -> 102 |
| Kits | Bonesmith | E_Damage 73 -> 123; R_Damage 92 -> 156 |
| Kits | Keeper | PassiveP1 450 -> 350; PassiveP2 1.5 -> 0.6; E_Damage 105 -> 132; R_Damage 368 -> 318; R_EffectValue 0.3 -> 0.15 |
| Kits | Monk | E_Damage 43 -> 40; E_FinalDamage 76 -> 70; R_Damage 62 -> 57 |
| Kits | Intern | E_Damage 24 -> 19; R_FinalDamage 269 -> 199 |
| Kits | Femur | E_Damage 67 -> 33; E_FinalDamage 134 -> 68; R_Damage 335 -> 163 |
| Kits | Digby | E_Damage 38 -> 49; R_FinalDamage 299 -> 235 |
| Kits | Collector | E_Damage 114 -> 64; R_Damage 580 -> 186; R_Cooldown 70 -> 55 |
| Kits | Beak | PassiveP3 0.06 -> 0.045; E_Damage 130 -> 156; E_EffectValue 0.12 -> 0.08; R_Damage 390 -> 246; R_EffectValue 0.35 -> 0.25 |
| Kits | Tallow | PassiveP1 35 -> 12; E_Damage 75 -> 40; R_Damage 58 -> 30 |
| Kits | Lookout | PassiveP3 0.2 -> 0.12; E_Damage 25 -> 12; R_Damage 18 -> 9 |
| Kits | Vexa | E_Damage 22 -> 14; E_FinalDamage 20 -> 13; R_Damage 13 -> 8; R_EffectValue 0.4 -> 0.3 |
| Kits | Actuary | E_Damage 129 -> 77; R_Damage 296 -> 179 |
| Kits | Sweep | E_Damage 27 -> 22; R_Damage 32 -> 27 |
| Kits | Solace | E_Damage 29 -> 32; E_EffectValue 0.2 -> 0.12; R_Damage 164 -> 178; R_EffectValue 0.5 -> 0.2 |
| Kits | Pallbearers | E_Damage 82 -> 85; R_Damage 37 -> 38 |
| Kits | Ectoplasm | E_Damage 38 -> 35; R_Damage 35 -> 32 |
| Kits | Neckless | R_Damage 17 -> 16 |
| Kits | Tickwell | E_Damage 56 -> 32; R_Damage 264 -> 146 |
| Upgrades | ScytheSweep | Damage 13 -> 14.5; DamagePerLevel 2 -> 2.23 |
| Upgrades | WailCone | Damage 5 -> 5.85; DamagePerLevel 1.8 -> 2.11 |
| Upgrades | BoneTurret | Damage 10 -> 9.61; DamagePerLevel 3 -> 2.88 |
| Upgrades | GraveBells | Damage 33 -> 30.9; DamagePerLevel 8.9 -> 8.33 |
| Upgrades | CrowSwarm | Damage 5 -> 4.77; DamagePerLevel 1.6 -> 1.53 |
| Upgrades | Tombstone | Damage 24 -> 12.92; DamagePerLevel 6.3 -> 3.39 |
| Upgrades | FrostRing | Damage 23 -> 24.15; DamagePerLevel 6.8 -> 7.14 |
| Upgrades | SoulBolt | Damage 6 -> 5.8; DamagePerLevel 1.6 -> 1.55 |
| Upgrades | CoffinNails | Damage 29 -> 27.93; DamagePerLevel 7.3 -> 7.03 |
| Upgrades | PlagueCloud | Damage 5 -> 4.84; DamagePerLevel 1.7 -> 1.64 |
| Upgrades | LanternFlare | Damage 15 -> 16.31; DamagePerLevel 3.7 -> 4.02 |
| Upgrades | Gravequake | Damage 61 -> 26.68; DamagePerLevel 13 -> 5.69; Radius 320 -> 240; RadiusPerLevel 15 -> 10 |
| Upgrades | SeanceChain | Damage 20 -> 19.06; DamagePerLevel 5.5 -> 5.24 |
| Upgrades | CandleBeam | Damage 16 -> 15.33; DamagePerLevel 5 -> 4.79 |
| Upgrades | RubberStamp | Damage 8.5 -> 6.92; DamagePerLevel 2.4 -> 1.95 |
| Upgrades | ShovelSmack | Damage 23 -> 25.39; DamagePerLevel 6.3 -> 6.96 |
| Upgrades | Ectoplasm | Damage 10 -> 11.06; DamagePerLevel 3.4 -> 3.76 |
| Upgrades | MothLure | StatPerLevel 55 -> 75 |
| Upgrades | ExtraRibs | StatPerLevel 15 -> 12 |
| Upgrades | SplitSoul | MaxLevel 3 -> 5; StatPerLevel 1 -> 0.6; Stat2PerLevel 0.04 -> 0.03 |
| Upgrades | Embalming | StatPerLevel 0.45 -> 0.38 |
| Upgrades | RedInk | StatPerLevel 0.1 -> 0.15 |
| Upgrades | TeaUrn | StatPerLevel 0.6 -> 0.5; Stat2PerLevel 0.15 -> 0.1 |
| Upgrades | ReapingMoon | Damage 45 -> 43.5 |
| Upgrades | FerrymanSickles | Damage 25 -> 24.2 |
| Upgrades | BansheeChoir | Damage 20 -> 23.1 |
| Upgrades | Ossuary | Damage 29 -> 27.7 |
| Upgrades | Requiem | Damage 170 -> 159.5 |
| Upgrades | MurderOfCrows | Damage 16 -> 15.5 |
| Upgrades | Mausoleum | Damage 89 -> 50.7 |
| Upgrades | Permafrost | Damage 100 -> 106.4 |
| Upgrades | SpikedCoffinLid | Damage 130 -> 125.4 |
| Upgrades | Sunrise | Damage 47 -> 45.5 |
| Upgrades | EarthenMaw | Damage 188 -> 54.4 |
| Upgrades | GrandSeance | Damage 73 -> 69.4 |
| Upgrades | ThousandCandles | Damage 65 -> 62 |
| Upgrades | FinalDemand | Damage 17 -> 16.4 |
| Upgrades | DeniedInTriplicate | Damage 32 -> 27.1 |
| Upgrades | SixFeetUnder | Damage 80 -> 88.2 |
| Upgrades | PoltergeistFlood | Damage 20 -> 22.1 |
| Upgrades | CompanyScythe | Damage 11.6 -> 7.99; DamagePerLevel 1.97 -> 1.36; Weight 10 -> 25 |
| Upgrades | ExpressSickles | Damage 7.2 -> 8.83; DamagePerLevel 1.81 -> 2.22; Weight 10 -> 25 |
| Upgrades | Lament | Damage 4.8 -> 5.11; DamagePerLevel 1.74 -> 1.85; Weight 10 -> 25 |
| Upgrades | OssifiedSentry | Damage 10.5 -> 17.78; DamagePerLevel 3.14 -> 5.32; Weight 10 -> 25 |
| Upgrades | LanternPulse | Damage 13.3 -> 16.69; DamagePerLevel 3.37 -> 4.24; Weight 10 -> 25 |
| Upgrades | MalaOrbit | Damage 6.1 -> 5.61; DamagePerLevel 1.94 -> 1.79; Weight 10 -> 25 |
| Upgrades | ReplyAll | Damage 21.1 -> 17.31; DamagePerLevel 5.77 -> 4.74; Weight 10 -> 25 |
| Upgrades | BuriedBone | Damage 58.8 -> 29.93; DamagePerLevel 12.66 -> 6.46; Weight 10 -> 25 |
| Upgrades | ShovelSweep | Damage 23.5 -> 26.82; DamagePerLevel 6.45 -> 7.35; Weight 10 -> 25 |
| Upgrades | FinalReminders | Damage 6.5 -> 3.68; DamagePerLevel 1.94 -> 1.1; Weight 10 -> 25 |
| Upgrades | Miasma | Damage 5.5 -> 5.94; DamagePerLevel 1.87 -> 2.02; Weight 10 -> 25 |
| Upgrades | WickBolt | Damage 4.7 -> 3; DamagePerLevel 1.27 -> 0.8; Weight 10 -> 25 |
| Upgrades | Searchlight | Damage 13.5 -> 7.5; DamagePerLevel 4.21 -> 2.3; Weight 10 -> 25 |
| Upgrades | SpiteWreath | Damage 3.2 -> 2.02; DamagePerLevel 1.1 -> 0.69; Weight 10 -> 25 |
| Upgrades | DenialStamp | Damage 7.9 -> 4.75; DamagePerLevel 2.23 -> 1.34; Weight 10 -> 25 |
| Upgrades | CrowRelay | Damage 19.8 -> 16.54; DamagePerLevel 5.43 -> 4.54; Weight 10 -> 25 |
| Upgrades | HymnOfSolace | Damage 10.8 -> 11.72; DamagePerLevel 3.66 -> 3.98; Weight 10 -> 25 |
| Upgrades | PallTurret | Damage 10.7 -> 11.13; DamagePerLevel 3.1 -> 3.22; Weight 10 -> 25 |
| Upgrades | SeanceLink | Damage 21.1 -> 19.52; DamagePerLevel 5.77 -> 5.34; Weight 10 -> 25 |
| Upgrades | PumpkinGlare | Damage 12.6 -> 12.38; DamagePerLevel 3.92 -> 3.85; Weight 10 -> 25 |
| Upgrades | TollingHour | Damage 32.5 -> 17.98; DamagePerLevel 8.75 -> 4.84; Weight 10 -> 25 |
| Upgrades | SeverancePackage | Damage 45 -> 30.9 |
| Upgrades | NextDayDelivery | Damage 21 -> 25.8 |
| Upgrades | EternalLament | Damage 24 -> 22.1 |
| Upgrades | BoneCitadel | Damage 30 -> 50.9 |
| Upgrades | Lighthouse | Damage 61 -> 65.6 |
| Upgrades | HundredBeadHalo | Damage 18 -> 16.6 |
| Upgrades | ReplyAllStorm | Damage 77 -> 63.2 |
| Upgrades | Excavation | Damage 191 -> 97.2 |
| Upgrades | MassGrave | Damage 82 -> 93.6 |
| Upgrades | DebtSpiral | Damage 18 -> 10.2 |
| Upgrades | Quarantine | Damage 28 -> 30.3 |
| Upgrades | Blowtorch | Damage 23 -> 14 |
| Upgrades | Floodlights | Damage 55 -> 30 |
| Upgrades | CrownOfSpite | Damage 12 -> 7.4 |
| Upgrades | AuditTrail | Damage 32 -> 19.4 |
| Upgrades | Murmuration | Damage 76 -> 63.5 |
| Upgrades | RequiemMass | Damage 55 -> 59.7 |
| Upgrades | FullProcession | Damage 38 -> 39.5 |
| Upgrades | FullSeance | Damage 77 -> 71.3 |
| Upgrades | HeadlessHorror | Damage 53 -> 52 |
| Upgrades | Midnight | Damage 176 -> 97.4 |
| Upgrades | VampireFang | StatPerLevel 0.15 -> 0.11 |
| Upgrades | Highlighter | StatPerLevel 0.1 -> 0.22 |
| Upgrades | LumbarCushion | StatPerLevel 12 -> 9 |
| Upgrades | LintRoller | StatPerLevel 40 -> 55 |
| Upgrades | SympathyBiscuits | StatPerLevel 0.5 -> 0.3; Stat2PerLevel 0.15 -> 0.1 |

## 12. Reap system + early curve (Nov 9 2026, reap core; model section 26)

Approved design (preview "Sow, Soften, Reap" + "Fewer enemies early, then grow with the player"). Code: `SQReap.h/.cpp`
(USQReapComponent), ASQHordeManager (ripe flag, ApplyReap / ReapAlongPath, spawn time curve, on-screen cap, catch-up, early HP),
USQAbilityComponent (R charge), ASQCharacter (harvest revive, rise lock), ASQGameMode (rise hook, Training Shift hold).

### 12.1 What runs
- **Ripe**: an enemy below `ReapRipeFraction` 0.25 of its spawn HP (never minibosses / bosses) sets FSQEnemyNet::State bit 6;
  clients draw a soul-teal ring at its feet and a skull tag over it (two pooled ISMs for the whole horde, skinned or impostor).
- **Reap** (left click, `ReapCooldown` 1.2 s, cone `ReapRange` 380 x `ReapHalfAngle` 35 deg): ripe enemies in the cone are executed,
  the rest take `ReapDamage` 8 x (1 + 0.04 x (level - 1)) x damage stat x crit; bosses / minibosses take `ReapBossBonus` 3 x that.
  Every reaped soul (execution or scythe kill): +1 soul into the sack, `ReapXP` 1 x streak team XP, R charge.
- **Reap Streak**: reaps within `StreakWindow` 2 s chain x2 .. x5 (`StreakMax`); from x2 each reaped soul also pays
  `ReapStreakGold` 0.5 x (streak - 1) x gold multipliers x act growth (shared purse).
- **R from souls**: no cooldown. Charge per reaped soul `UltChargePerReap` 0.015, per kill `UltChargePerKill` 0.00075, both x
  `UltRefCooldown` 70 / the kit's R cooldown (evolution and haste included), so each kit keeps its R's relative pace. Casts at 100%.
  Reaping within 600 cm of a teammate gives them 50% of the raw charge (`UltShareFraction`). Level-6 unlock kept (charge may build before).
- **Reaper's Passage**: during the dash pass-through window (`DashPassSeconds`) ripe enemies touching the dash path are reaped.
- **Harvest Revive**: 10 souls (`HarvestReviveSouls`) reaped within 700 cm of a downed teammate stand them up at ReviveFraction.
  The F skull carry to the lantern stays (no conflict: a carried skull counts souls reaped near its carrier). Solo: down = run over.
- **Early curve** (solo; x (1 + 0.5 per extra reaper) x mission / Hard / Difficulty spawn multipliers):
  act 1 = 0.6 /s -> 2 /s at 2:00 (eased, `SpawnEarlyEaseExp` 1.5) -> 8 /s at the act end; acts 2-3 = 6 -> 8 /s.
  On-screen cap for the regular loop: 30 up to minute 1, +25 per minute after (x crew x spawn mults; MaxEnemies stays the hard cap;
  swarms, nests and summons ignore it). Act-1 HP x `EarlyHealthStart` 0.4 easing to 1 at 6:00 (Graveyard Shambler 15 x 1.155 -> ~7,
  one Company Scythe hit; every early type gets the same). Catch-up: new enemies' HP x 0.75-1.25 (`CatchUpMax`), drifting 1%/s toward
  (3 s / measured seconds an enemy survives near the crew) ^ 0.5 (Little's law: enemies within 800 cm / kills per s).
- The model's catch-up uses (kill capacity / spawn rate) / 1.33 ^ 0.5 instead (same intent, model-friendly).
- **Why HP is a Tuning ease and not Enemies.csv Health 15 -> 7**: `StageHealthSmoothing` assumes HealthPerNight ~3 x Health;
  halving Health in the CSV would make the Shambler jump 28 -> 72 HP at the act-2 boundary.

### 12.2 Before -> after (solo unless noted)
| Number | Before | After |
|---|---|---|
| Spawn rate at start | 4 /s | 0.6 /s |
| Spawn rate at 2:00 | ~10 /s | 2.2 /s |
| Act-end peak | 22 /s | 8 /s |
| Act 2-3 start | 4 /s | 6 /s |
| Extra spawns per teammate | +65% | +50% (EnemyHealthPerPlayer 0.2 -> 0.3 keeps per-person load: 4p = 4.75x solo HP inflow) |
| Shambler HP at start (Graveyard) | 17 | ~7 (EarlyHealthStart 0.4) |
| XP to level 2 | 23 (27 kills) | 10 (XPBase 8 -> 1, XPGrowth 15 -> 9; ~5 kills when reaping, 10 without) |
| Max on screen, minute 1 | 800 | 30 |
| XP per extra player (XPPerPlayer) | 0.65 | 0.5 |
| Kills per run (PlagueCarnival solo) | ~9,900 | ~4,850 |
| Gold earned per run (PlagueCarnival solo) | 5,107 | ~4,900 (GoldDropChance 0.04 -> 0.08, EliteGold 4 -> 8, + streak gold) |
| Reaps per reaper per run | - | 340-570 (12-17% of XP), average streak x2.1-2.5 |
| R charge time / old R cooldown | 1.0 | 1.07-1.21 solo, 0.87-0.91 4p (shared charge); boss fight 78-92 s solo, 53-57 s 4p |
| Scythe share of boss DPS | - | 2-3% |

Early curve, PlagueCarnival solo (Grim, seed 0): spawn / kill capacity 0.31 (0:00), 0.35 (1:00), 0.48 (2:00), 0.68 (3:00),
0.79 (4:00), 0.62 (5:00), 0.47-0.63 through act 2, ~0.25-0.35 late act 3. Kill capacity is a model assumption (geometric mean of
one-target and full-crowd kills/s); the first minutes sit under the 75% target on purpose (approved 0.6 /s start), acts 1-2 are on it,
late act 3 is under it (strong builds; catch-up adds +15-25% HP there). If late game feels empty, raise `SpawnRatePeak` first.

### 12.3 Contract after the re-fit (model --quick, 12 seeds)
| Target | Contract | Before (11.1) | After |
|---|---|---|---|
| Hits-to-kill early, normal | 1-3 | 1.4-3.0 | **1.2-2.9** |
| Hits-to-kill late, normal / Hard | 2-5 / 3-6 | 2.0-3.7 / 3.1-5.7 | **2.4-4.9 / 3.0-6.3** (Graveyard Hard 4p 6.3) |
| Boss TTK / target, 40 cells | 0.85-1.15 | 37 in (0.80-1.16) | **33 in (0.67-1.19)**: out = PlagueCarnival Hard 1/3/4p 0.83/0.81/0.67 + normal 4p 0.82, DrownedHarbour Hard 4p 0.79, Graveyard Hard 3p 0.83, FrozenMonastery normal solo 1.19 |
| Co-op per-person pace | ~1.0 | 0.80-1.20 | **0.83-1.22** |
| Kits within +-12% | | 18 of 21 | **17 of 21**: Tallow +16% (known), Intern -10%, Widow +16%, Lookout +13% (their signature weapons, Upgrades.csv) |
| Maxed meta power | <= 1.6 | 1.53 | **1.44** |
| First clear | 1-3 attempts, 30-60 min | attempt 3, 43 min | **attempt 3, 45 min** |
| Hour 1 | 2-4 upgrades + 1-2 reapers | 2 + 1 | **2 + 1** |
| All Hard / everything | 30-50 h | 22 h / 40 h | **37 h / 42 h** |
| Readable numbers | tens to low thousands | crits 892 / 3,405 | **1,075 / 3,652** |

Recommended next (outside this pass's files): Missions.csv PlagueCarnival HardBossHealthMult 1.12 -> ~1.30, FrozenMonastery
BossHealthMult 0.98 -> ~0.90; Upgrades.csv Lament / Searchlight signature damage -8%. Model changes: `spawn_curve`, `early_hp_mult`,
`on_screen_cap`, `reap_numbers`, `ult_period`, catch-up and reap payouts in `sim_run`, R period in `ability_power`, the win proxy's
clear term now uses the crowd-limited capacity, Bank Souls counts reaped souls; new report section 26.

```
-- 16. RUN SIMULATION per mission x mode x crew, at the Death's Office ranks a player typically owns when they first beat it
   (journey medians from section 23; 12 seeds per cell; reaper rotates with the seed). Hits = avg normal enemy HP / signature hit (median-weapon hit in brackets); HTD = hits a reaper can take (EHP / avg enemy hit); Clear = team horde DPS / HP arriving per s
  Mission         Mode P  Meta Bell  Lvl  Objs ok   Hits early  Hits late    Clear  HTD   BHTD  Boss TTK/tgt  MaxNum W/A  RunMin Pwin 
  Graveyard       norm 1  1    13.0m 39   5.0/5      1.2         2.4 ( 6.5)  4.69   9.7   7.7    208 s 0.99     273/685   17.1    68%
  Graveyard       norm 2  1    12.8m 40   5.0/5      1.9         3.8 ( 9.1)  4.77   15.5  12.2   235 s 1.12     277/1593  17.5    87%
  Graveyard       norm 3  1    12.7m 39   5.0/5      2.3         4.9 ( 9.5)  4.50   19.2  15.1   224 s 1.07     287/1584  17.2    91%
  Graveyard       norm 4  1    12.7m 39   5.0/5      2.7         4.4 (10.8)  4.54   17.3  13.6   196 s 0.93     367/2007  16.7    94%
  Graveyard       HARD 1  10   13.1m 44   5.0/5      1.8         3.0 ( 9.2)  3.52   3.5   5.1    193 s 0.92     308/875   17.1    31%
  Graveyard       HARD 2  10   12.8m 42   5.0/5      2.5         3.9 ( 9.9)  3.65   5.0   7.0    190 s 0.91     389/2056  16.7    49%
  Graveyard       HARD 3  10   12.8m 42   5.0/5      3.0         5.0 (13.5)  3.04   6.5   9.1    174 s 0.83     459/1821  16.4    68%
  Graveyard       HARD 4  10   12.7m 44   5.0/5      3.7         6.3 (14.1)  3.27   6.4   9.0    188 s 0.89     440/2239  16.5    70%
  DrownedHarbour  norm 1  2    14.2m 43   5.0/5      1.3         3.0 ( 7.1)  4.46   10.7  9.0    202 s 0.96     288/988   18.4    76%
  DrownedHarbour  norm 2  2    13.8m 42   5.0/5      1.8         3.0 ( 7.4)  4.57   17.5  14.1   226 s 1.08     338/1553  18.3    88%
  DrownedHarbour  norm 3  2    13.5m 42   5.0/5      2.1         3.3 ( 8.8)  4.88   17.9  14.4   210 s 1.00     381/1570  17.7    92%
  DrownedHarbour  norm 4  2    13.5m 42   5.0/5      2.3         4.1 ( 8.4)  4.49   20.2  16.2   223 s 1.06     424/2176  18.0    94%
  DrownedHarbour  HARD 1  25   14.2m 47   5.0/5      2.0         3.2 ( 7.7)  3.23   4.2   6.3    221 s 1.05     328/821   18.7    26%
  DrownedHarbour  HARD 2  25   13.8m 46   5.0/5      1.9         3.8 ( 9.7)  3.85   6.0   8.7    221 s 1.05     455/1644  18.2    53%
  DrownedHarbour  HARD 3  25   13.5m 46   5.0/5      2.9         4.4 (11.8)  3.89   6.0   8.6    182 s 0.87     563/2100  17.3    67%
  DrownedHarbour  HARD 4  25   13.5m 46   5.0/5      3.2         4.9 (14.3)  3.67   6.3   9.1    166 s 0.79     575/2858  17.0    74%
  PlagueCarnival  norm 1  4    14.8m 45   6.0/6      1.7         3.7 ( 8.0)  4.88   13.1  12.9   214 s 1.02     210/748   19.1    81%
  PlagueCarnival  norm 2  4    14.8m 44   6.0/6      1.9         3.1 ( 7.3)  4.55   16.2  15.8   193 s 0.92     406/1478  18.7    88%
  PlagueCarnival  norm 3  4    14.8m 45   6.0/6      2.2         4.5 ( 8.9)  4.49   20.6  20.2   200 s 0.95     363/1513  18.8    93%
  PlagueCarnival  norm 4  4    14.8m 45   6.0/6      2.6         4.6 (10.3)  4.67   16.8  16.4   173 s 0.82     671/2757  18.4    94%
  PlagueCarnival  HARD 1  16   14.8m 48   5.9/6      2.2         3.8 ( 9.4)  3.51   4.2   7.3    175 s 0.83     345/843   18.4    31%
  PlagueCarnival  HARD 2  16   14.8m 48   6.0/6      2.4         4.0 (10.2)  3.42   5.7   9.9    193 s 0.92     488/2072  18.7    51%
  PlagueCarnival  HARD 3  16   14.8m 48   6.0/6      3.0         4.4 (11.9)  3.41   6.4   11.1   170 s 0.81     696/2395  18.3    61%
  PlagueCarnival  HARD 4  16   14.8m 50   6.0/6      4.0         6.1 (13.8)  3.55   6.3   11.0   141 s 0.67     766/3302  17.9    73%
  FrozenMonastery norm 1  5    16.3m 52   6.0/6      1.6         2.6 ( 5.9)  4.22   11.4  9.9    249 s 1.19     290/1099  21.4    66%
  FrozenMonastery norm 2  5    16.0m 49   6.0/6      2.1         3.4 ( 7.0)  4.08   16.0  13.9   241 s 1.15     369/1767  20.8    81%
  FrozenMonastery norm 3  5    15.9m 49   6.0/6      2.1         3.7 ( 9.7)  4.08   14.4  12.4   219 s 1.04     456/2298  20.4    84%
  FrozenMonastery norm 4  5    15.8m 48   6.0/6      2.6         4.2 (10.8)  4.09   14.8  12.8   182 s 0.87     792/2819  19.6    91%
  FrozenMonastery HARD 1  27   16.4m 54   6.0/6      1.9         3.6 ( 8.6)  3.19   3.6   5.7    231 s 1.10     419/1167  21.1    25%
  FrozenMonastery HARD 2  27   16.1m 54   6.0/6      2.4         3.3 ( 8.3)  3.10   5.0   7.8    224 s 1.07     661/2212  20.6    37%
  FrozenMonastery HARD 3  27   16.0m 54   6.0/6      3.1         4.2 ( 9.1)  3.10   4.9   7.5    214 s 1.02    1075/2544  20.3    50%
  FrozenMonastery HARD 4  27   15.9m 58   6.0/6      3.2         5.6 (12.9)  2.94   5.9   9.1    209 s 0.99     800/3238  20.1    58%
  Clocktower      norm 1  8    17.0m 52   6.0/6      1.7         2.5 ( 6.1)  4.23   9.7   8.1    267 s 1.11     434/948   22.3    64%
  Clocktower      norm 2  8    16.8m 52   6.0/6      2.3         3.4 ( 7.4)  4.47   12.8  10.7   244 s 1.02     481/1923  21.5    79%
  Clocktower      norm 3  8    16.6m 56   6.0/6      2.7         4.4 (10.0)  4.81   12.2  10.0   230 s 0.96     451/2327  21.2    84%
  Clocktower      norm 4  8    16.5m 57   6.0/6      2.9         4.0 (10.3)  4.90   16.2  13.0   217 s 0.90     864/3070  20.9    93%
  Clocktower      HARD 1  47   17.0m 58   6.0/6      1.8         3.1 ( 7.3)  3.53   4.4   6.7    234 s 0.98     584/1275  21.7    30%
  Clocktower      HARD 2  47   16.8m 58   6.0/6      2.6         3.9 ( 9.6)  3.52   5.3   7.8    215 s 0.90     659/2050  21.1    46%
  Clocktower      HARD 3  47   16.7m 58   6.0/6      3.5         4.4 (10.1)  3.41   6.0   8.8    209 s 0.87     821/2572  20.9    58%
  Clocktower      HARD 4  47   16.6m 58   6.0/6      3.9         6.0 (12.1)  3.82   5.5   8.1    208 s 0.86     850/3652  20.8    64%

```

