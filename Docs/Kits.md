# Soul Quota - Reaper Kits (Nov 9 2026 kits pass)

Jack asked for the game to play like a horde-mode "Swarm"-style survivors game (mechanics only: no outside names, art or text)
and for **better character abilities**. Every one of the 21 reapers now has a full kit:

- a **signature weapon** nobody else can get (auto-fire). It starts at level 1, levels through level-up cards (only its owner
  is ever offered it) and evolves at max level + its paired **tome**. It does **not** use a shared weapon slot:
  a reaper holds **signature + `WeaponSlots` (4) shared weapons**.
- a **passive**: one identity mechanic (no per-level scaling any more; the old Reapers.csv `PassiveBase/PassivePerLevel` are gone).
- an **E** (unlocks at team level `AbilityUnlockE` 3, 6-10 s cooldown) and an **R** (team level `AbilityUnlockR` 6, 55-90 s),
  aimed at the cursor (top-down). Input: slot 2 = E, slot 3 = R (`ASQCharacter::TryCastAbility`).
- **Ability evolution**: when the signature's paired tome reaches max level, E and R evolve (Kits.csv `Evo*` columns: more damage
  and effect, bigger area, lower cooldown, and often an extra effect).

Everything is data: **Content/Data/Kits.csv** (one row per reaper) + the `Signature 1` rows of Upgrades.csv + the
"Reaper kits" block of Tuning.csv. Runtime: `USQAbilityComponent` (Source/SoulQuota/SQAbility.h/.cpp).

## 1. The 21 kits

Damage numbers in this table are the kits-pass values; **the current numbers are in section 6** (remake balance pass). Real hits: ability damage x (1 + `AbilityDamagePerLevel` 0.03 x (team level - 1))
x the damage stat x crit; cooldown x CooldownMult ^ 0.5; radius x Area ^ 0.5; durations / slows x Duration.

| # | Reaper | Role | Signature weapon (lv1 -> evolution, tome) | Passive | E (unlock lv 3) | R (unlock lv 6) |
|---|---|---|---|---|---|---|
| 1 | Grim (Grim) | All-rounder | **Company Scythe** (Arc, 11.6+1.97 dmg, 1s) -> **Severance Package** (45 dmg), Edge Maintenance Manual | **Death's Company** (TeamDamage): Every 60 kills Grim rallies the crew: he and every reaper within 900 deal +15% damage for 4s. | **Performance Review**: Dash 19 dmg + 19 final, r 220, range 520, cd 8s, Knockback. Dashes up to 520 toward the cursor, slashing everything on the way, and knocks the crowd back at the end. *Evolved: +30% damage, +20% area, -15% cooldown, slows 1.5s.* | **Mass Layoff**: Nova 107 dmg, r 720, cd 70s, Pull + Slow. Raises the scythe, drags everything within 720 toward him, then one colossal sweep. Everyone's let go. *Evolved: +30% damage, +25% area, -15% cooldown, +25% crew damage for 6s.* |
| 2 | Swift the Hauler (Mort) | Hauler | **Express Sickles** (Projectile, 7.2+1.81 dmg, 0.85s) -> **Next-Day Delivery** (21 dmg), Commuter's Handbook | **Hit and Run** (HitAndRun): Every kill takes 0.25s off his E (up to half of each cooldown) and gives +12% speed for 1.5s. The faster he works, the faster he works. | **Sickle Fan**: Volley 35 dmg, r 32, range 1100, x7, cd 7s, -. Fans 7 sickles toward the cursor in a 45-degree spread; each pierces 3 enemies. *Evolved: +25% damage, +4 projectiles, -15% cooldown.* | **Rush Hour**: Self 28 dmg, r 260, 8s, cd 60s, Haste + Knockback. For 8s Swift and every reaper within 260 run 40% faster; a slipstream around him shreds and shoves anything close. *Evolved: +30% damage and effect, +20% area, -15% cooldown, +25% duration.* |
| 3 | The Wailing Widow (Widow) | Control | **Lament** (Aura, 4.8+1.74 dmg, 0.4s) -> **Eternal Lament** (24 dmg), Deadline Binder | **Grief Tax** (SlowSouls): Slowed enemies drop +1 soul when anyone on the crew kills them. Grief has a price. | **Shriek**: Cone 31 dmg, range 620, cd 8s, Slow + Knockback. A 70-degree shriek toward the cursor: damages, slows for 2s and blows the crowd back. *Evolved: +30% damage, +20% area, -15% cooldown, freezes 1s.* | **Wake of Sorrow**: Zone 18 dmg + 96 final, r 460, range 1000, 6s, cd 75s, Pull + Slow. A whirlpool of grief at the cursor for 6s: drags enemies to its centre, slows and wears them down, then collapses. *Evolved: +30% damage, +25% area, -15% cooldown, freezes 2s.* |
| 4 | Bonesmith (Bonesmith) | Defender | **Ossified Sentry** (Turret, 10.5+3.14 dmg, 3.6s) -> **Bone Citadel** (30 dmg), Health and Safety Binder | **Scrap Sentries** (Builder): Every 18 kills he bolts the scrap into a bone sentry at his feet (10s, 2 at once) that shoots for 9 damage. | **Bone Bulwark**: Circle 73 dmg, r 380, range 900, 4s, cd 10s, Shield + Knockback. Slams a bone barricade at the cursor: knocks enemies out of 380 and gives every reaper inside a shield of 18% max HP for 4s. *Evolved: +30% damage and effect, +20% area, -15% cooldown.* | **Calcified Fortress**: Totem 92 dmg, r 160, range 900, 12s, x3, cd 80s, Pull. Raises an ossuary tower at the cursor for 12s: it shoots bone bolts at the 3 nearest enemies and drags the horde toward it. *Evolved: +30% damage, +20% area, +1 targets per shot, -15% cooldown, +20% duration.* |
| 5 | The Lantern Keeper (Keeper) | Banker | **Lantern Pulse** (Nova, 13.3+3.37 dmg, 1.5s) -> **Lighthouse** (61 dmg), Plot Expansion Plans | **Lamplight** (Lamplight): Her lamp lights 450 around her: reapers inside heal 1.5 HP/s and enemies inside are slowed. | **Flare Toss**: Circle 105 dmg, r 270, range 950, cd 8s, Mark + Slow. Lobs a flare at the cursor: burns everything within 270, slows it, and marks the toughest for +25% team damage. *Evolved: +30% damage and effect, +25% area, -15% cooldown.* | **Daybreak**: Nova 368 dmg, r 820, cd 75s, Heal + Slow. Holds the lantern high: a pillar of dawn sears and slows everything within 820 and heals every reaper in it for 30%. *Evolved: +30% damage and effect, +20% area, -15% cooldown, revives the downed at 35%.* |
| 6 | The Hollow Monk (Monk) | Striker | **Mala Orbit** (Orbit, 6.1+1.94 dmg, 0.3s) -> **Hundred-Bead Halo** (18 dmg), Fine Print Ledger | **Vow of Violence** (DashStrike): His dash is a strike: 30 damage along the path, a moment of invulnerability, and 0.2s off his E per enemy hit (max 2s a dash). | **Palm Strike**: Dash 43 dmg + 76 final, r 230, range 420, cd 6s, Knockback. Glides up to 420 toward the cursor and finishes with a palm that blasts everything in 230 away. *Evolved: +30% damage, +20% area, -20% cooldown, freezes 0.8s.* | **Hundred Hands**: Barrage 62 dmg, r 170, range 900, 3s, x18, cd 55s, Knockback. 18 spectral palms rain down around the cursor over 3s, each blasting 170 around it. *Evolved: +30% damage, +15% area, +6 strikes, -15% cooldown.* |
| 7 | The Intern (Intern) | Wildcard | **Reply-All** (Chain, 21.1+5.77 dmg, 1.2s) -> **Reply-All Storm** (77 dmg), Reaper's Ledger | **Overeager** (OnTheJob): Every 3rd ability he casts happens twice: an echo 0.5s later at 60% strength. Unpaid, keen, double-booked. | **Photocopier Jam**: Volley 24 dmg, r 28, range 1000, x5, cd 6s, -. Hurls 5 jammed copies in a 50-degree fan toward the cursor; each pierces 2. *Evolved: +30% damage, +3 projectiles, -15% cooldown.* | **All-Hands Meeting**: Zone 0 dmg + 269 final, r 700, range 1000, 2.5s, cd 65s, Pull. Calls a meeting at the cursor: everything within 700 is dragged in for 2.5s, then the agenda explodes. *Evolved: +30% damage, +20% area, -15% cooldown, freezes 2s.* |
| 8 | Femur the Hound (Femur) | Fetcher | **Buried Bone** (Drop, 58.8+12.66 dmg, 4.8s) -> **Excavation** (191 dmg), Lost and Found Register | **Fetch** (Fetch): Souls within 1000 fly to him; he runs 25% faster carrying a teammate's skull. Who's a good reaper? | **Pounce**: Dash 67 dmg + 134 final, r 220, range 560, cd 7s, Knockback. Pounces up to 560 toward the cursor and lands with a thump that knocks everything in 220 away. *Evolved: +30% damage, +20% area, -15% cooldown, slows 2s.* | **Howl at the Moon**: Nova 335 dmg, r 650, cd 60s, Haste + Knockback. A howl that hurts and scatters everything within 650; every reaper in range runs 35% faster for 8s. *Evolved: +30% damage and effect, +20% area, -15% cooldown, +25% duration, +20% crew damage for 8s.* |
| 9 | Digby Sixfeet (Digby) | Defender | **Shovel Sweep** (Arc, 23.5+6.45 dmg, 1.4s) -> **Mass Grave** (82 dmg), Spare Ribs Catalogue | **Bulwark** (Bulwark): Reapers within 600 (him too) take 18% less damage. Built like a headstone. | **Dirt Toss**: Cone 38 dmg, range 480, cd 7s, Slow + Knockback. Flings a shovel of grave dirt in a 65-degree cone: damages, slows 2.5s and shoves the crowd back. *Evolved: +30% damage, +20% area, -15% cooldown.* | **Open Grave**: Zone 0 dmg + 299 final, r 460, range 900, 2.5s, cd 70s, Freeze + Pull. Opens a grave at the cursor: everything within 460 is held fast for 2.5s, then buried for huge damage. *Evolved: +30% damage, +20% area, -15% cooldown, -30% damage taken for 5s.* |
| 10 | The Debt Collector (Collector) | Greed | **Final Reminders** (Projectile, 6.5+1.94 dmg, 1s) -> **Debt Spiral** (18 dmg), Soul Inventory Ledger | **Collections** (Collections): +0.8% damage per soul carried (max +60%). Interest compounds; so does panic. | **Repossess**: Line 114 dmg, range 850, cd 7s, Pull. A chain of notices 850 long toward the cursor: hurts everything on it and drags it back to the Collector. *Evolved: +30% damage, +25% area, -15% cooldown, slows 1.5s.* | **Bankruptcy Notice**: Circle 580 dmg, r 520, range 1100, cd 70s, Mark + Slow. A colossal stamp lands at the cursor after 0.8s: crushes everything within 520 and marks the toughest for +60% team damage. *Evolved: +30% damage and effect, +20% area, -15% cooldown, freezes 1.5s.* |
| 11 | Doctor Beak (Beak) | Healer | **Miasma** (Aura, 5.5+1.87 dmg, 0.5s) -> **Quarantine** (28 dmg), Embalming Procedures Manual | **Triage** (Triage): Every 3s heals the most hurt reaper within 900 for 6% max HP + 8; overheal becomes a shield (max 40%). | **Tonic Flask**: Circle 130 dmg, r 300, range 900, cd 9s, Heal + Slow. Throws a flask at the cursor: poisons and slows enemies within 300 and heals every reaper in the splash for 12%. *Evolved: +30% damage and effect, +20% area, -15% cooldown.* | **House Call**: Global 390 dmg, r 600, cd 90s, Heal + Revive. Heals every reaper on the map for 35%, revives the downed at 35% where they lie, and blasts everything within 600 of each. *Evolved: +30% damage and effect, +20% area, -15% cooldown, overheal becomes a shield of 20% for 6s.* |
| 12 | Tallow (Tallow) | Striker | **Wick Bolt** (Projectile, 4.7+1.27 dmg, 0.55s) -> **Blowtorch** (23 dmg), Red Ink Style Guide | **Scorch** (Scorch): Dashing leaves a burning wax trail for 3s: 35 damage per second to anything standing in it. | **Hot Wax**: Blink 75 dmg, r 230, range 480, cd 6s, Slow. Melts and reforms up to 480 toward the cursor: a wax splash at both ends burns and slows everything in 230. *Evolved: +30% damage, +20% area, -20% cooldown.* | **Burn Both Ends**: Line 58 dmg, range 1600, 3s, cd 55s, Slow. A white-hot beam 1600 long toward the cursor, burning for 3s. Everything in it melts. *Evolved: +30% damage, +25% area, -15% cooldown, +25% duration.* |
| 13 | The Graveyard Lookout (Lookout) | Scout | **Searchlight** (Beam, 13.5+4.21 dmg, 1.6s) -> **Floodlights** (55 dmg), Red Ink Style Guide | **Spotter** (Lookout): Every 5s marks the toughest enemy within 1500 for 4s: +20% damage from everyone and +2 souls when it drops. | **Spotting Shot**: Line 25 dmg, range 1500, cd 8s, Mark. A piercing shot 1500 long toward the cursor; the toughest enemy near its path is marked for +20% team damage for 5s. *Evolved: +30% damage and effect, +20% area, -15% cooldown.* | **Watchtower**: Totem 18 dmg, r 140, range 900, 14s, x1, cd 70s, Empower. Raises a watchtower at the cursor for 14s: it beams the nearest enemy twice a second, and reapers within 600 of it deal +15% damage. *Evolved: +30% damage and effect, +20% area, +1 targets per shot, -15% cooldown, +20% duration.* |
| 14 | Vexa the Veiled (Vexa) | Berserker | **Spite Wreath** (Orbit, 3.2+1.1 dmg, 0.2s) -> **Crown of Spite** (12 dmg), Edge Maintenance Manual | **Spite** (Spite): Up to +40% damage as her health drops. Do not RSVP. | **Spiteful Lunge**: Dash 22 dmg + 20 final, r 200, range 450, cd 7s, Lifesteal. Lunges up to 450 toward the cursor, cutting through everything; heals 2.5% max HP per enemy hit. *Evolved: +30% damage and effect, +20% area, -15% cooldown.* | **Black Veil**: Self 13 dmg, r 300, 8s, cd 60s, Empower + Fortify. For 8s Vexa deals +40% damage, takes 40% less, and a storm of thorns shreds everything within 300. *Evolved: +30% damage and effect, +20% area, -15% cooldown, +25% duration.* |
| 15 | The Actuary (Actuary) | Economy | **Denial Stamp** (Drop, 7.9+2.23 dmg, 1.3s) -> **Audit Trail** (32 dmg), Reaper's Ledger | **Compound Interest** (Audit): Every 10th kill is audited: +2 souls and a stamp blast of 40 damage in 180 around the body. | **Red Tape**: Line 129 dmg, range 950, cd 8s, Slow. Unrolls 950 of red tape toward the cursor: everything it crosses is hurt and slowed for 2.5s. *Evolved: +30% damage, +20% area, -15% cooldown, freezes 1s.* | **Hostile Audit**: Barrage 296 dmg, r 230, range 1000, 2s, x12, cd 65s, Slow. 12 enormous DENIED stamps rain on the area around the cursor over 2s, each crushing 230 around it. *Evolved: +30% damage, +15% area, +4 strikes, -15% cooldown.* |
| 16 | The Chimney Sweep (Sweep) | Mobility | **Crow Relay** (Chain, 19.8+5.43 dmg, 1.15s) -> **Murmuration** (76 dmg), Commuter's Handbook | **Flock** (Flock): Kills call a crow (max 8; one leaves every 1.5s). The flock pecks everything within 230 for 6 damage per crow, twice a second. | **Chimney Drop**: Blink 27 dmg, r 260, range 620, cd 7s, Slow + Knockback. Vanishes up a flue and drops out up to 620 toward the cursor in a soot burst that hurts, slows and scatters. *Evolved: +30% damage, +20% area, -20% cooldown.* | **Black Cyclone**: Volley 32 dmg, r 34, range 1100, 1.5s, x24, cd 55s, -. Three waves of 24 crows burst out in every direction over 1.5s, each pecking through 3 enemies. *Evolved: +30% damage, +8 projectiles, -15% cooldown.* |
| 17 | Sister Solace (Solace) | Reviver | **Hymn of Solace** (Arc, 10.8+3.66 dmg, 1.35s) -> **Requiem Mass** (55 dmg), Spare Ribs Catalogue | **Last Rites** (LastRites): Stand by a downed reaper for 2.5s to revive them on the spot at 40% health. House calls only. | **Sanctuary**: Circle 29 dmg, r 360, range 900, 5s, cd 10s, Shield + Knockback. Blesses the ground at the cursor: enemies are blown out of 360 and every reaper inside gets a shield of 20% max HP for 5s. *Evolved: +30% damage and effect, +20% area, -15% cooldown, heals 15%.* | **Resurrection**: Global 164 dmg, r 650, cd 90s, Revive + Heal. Revives every downed reaper on the map at 50%, heals the rest by 50%, and smites everything within 650 of Solace. *Evolved: +30% damage and effect, +20% area, -20% cooldown, -30% damage taken for 6s.* |
| 18 | The Pallbearers (Pallbearers) | Mobile bank | **Mourners' Bench** (Turret, 10.7+3.1 dmg, 5.5s) -> **Full Procession** (38 dmg), Health and Safety Binder | **Mobile Bank** (MobileBank): Every 2s reapers within 350 bank 25% of their souls straight into the coffin. Slow walkers, fast paperwork. | **Coffin Slam**: Circle 82 dmg, r 300, range 700, cd 9s, Knockback + Slow. Heaves the coffin onto the cursor after a short swing: crushes everything in 300 and knocks it flat. *Evolved: +30% damage, +20% area, -15% cooldown, freezes 1s.* | **Grand Procession**: Self 37 dmg, r 340, 8s, cd 75s, Fortify + Haste. For 8s the procession marches: they take 30% less damage, run 30% faster, and trample everything within 340. *Evolved: +30% damage and effect, +20% area, -15% cooldown, +25% duration.* |
| 19 | Madame Ectoplasm (Ectoplasm) | Summoner | **Seance Link** (Chain, 21.1+5.77 dmg, 1.2s) -> **Full Seance** (77 dmg), Plot Expansion Plans | **Seance** (Seance): Kills have a 5% chance (elites always) to raise a ghost for 8s (max 3) that shoots for 14 damage. | **Spirit Board**: Chain 38 dmg, r 600, range 900, x6, cd 7s, Slow. Ghost-lightning leaps from the cursor through 6 enemies, slowing each for 1s. *Evolved: +30% damage, +20% area, +3 jumps, -15% cooldown.* | **Seance Circle**: Totem 35 dmg, r 180, range 900, 12s, x3, cd 70s, Slow. Opens a seance circle at the cursor for 12s: three spirits rise and zap the nearest enemies, slowing them. *Evolved: +30% damage, +20% area, +1 targets per shot, -15% cooldown, +20% duration.* |
| 20 | Sir Neckless (Neckless) | Speed | **Pumpkin Glare** (Beam, 12.6+3.92 dmg, 1.55s) -> **Headless Horror** (53 dmg), Fine Print Ledger | **Stampede** (Stampede): At full gallop he tramples everything within 160 for 7 damage every 0.25s. Never looks where he's going. | **Headless Charge**: Dash 21 dmg, r 200, range 650, cd 7s, Knockback. Gallops up to 650 toward the cursor, trampling and tossing everything in the way. *Evolved: +30% damage, +20% area, -15% cooldown, slows 1.5s.* | **Wild Hunt**: Self 17 dmg, r 300, 8s, cd 60s, Haste + Knockback. For 8s he rides like the wind: +50% speed for him and every reaper within 300, and a burning trample around him. *Evolved: +30% damage and effect, +20% area, -15% cooldown, +25% duration, +20% crew damage for 8s.* |
| 21 | Old Tickwell (Tickwell) | Time | **Tolling Hour** (Nova, 32.5+8.75 dmg, 3.8s) -> **Midnight** (176 dmg), Deadline Binder | **Stopwatch** (Stopwatch): Every 14s stops time within 700 for 1.75s. Punctual to the point of rudeness. | **Rewind**: Blink 56 dmg, r 240, range 550, cd 7s, Slow. Steps up to 550 toward the cursor through a moment that never happened; time slows around both ends. *Evolved: +30% damage, +20% area, -20% cooldown, freezes 1s.* | **Time Stop**: Nova 264 dmg, r 900, cd 80s, Freeze. Stops the clock: every enemy within 900 is hurt and frozen solid for 4s. The crew keeps moving. *Evolved: +30% damage, +15% area, -15% cooldown, +25% duration.* |


Co-op kits: Grim (crew damage), Swift (crew haste), Bonesmith (shields + a tower that drags the horde), Lantern Keeper (heal aura,
crew heal, evolved revive), Femur (crew haste, evolved crew damage), Digby (damage cut aura, evolved fortify), Doctor Beak (heals,
global heal + revive), Lookout (marks, crew damage tower), Sister Solace (field revive, shields, global revive), Pallbearers
(mobile bank), Sir Neckless (crew haste, evolved crew damage). Pulls / taunts: Grim R, Widow R, Bonesmith R, Intern R, Digby R,
Collector E.

## 2. Kits.csv columns

| Column | Meaning |
|---|---|
| ReaperId, SignatureWeaponId, EvoTome | The reaper, its Upgrades.csv signature weapon (`Signature 1`), and the tome that evolves the weapon AND E / R |
| PassiveId, PassiveName, PassiveGlyph, PassiveDesc | Passive code id (one of 21 implemented in SQAbility.cpp `TickPassive` / `OnOwnerKill` / `OnOwnerDash`), UI name, icon letters, text |
| PassiveP1..P4 | Passive numbers (meaning per passive: see the table below) |
| PassiveAnim | Clip fallback list for passive moments (`SQ_Ability_*` takes first) |
| E_/R_ Name, Glyph, Desc, Color | UI text, icon letters, effect colour |
| Shape | Circle (at the cursor), Nova (around the caster), Cone (Width = degrees), Line (Width = half width; Duration > 0 = a beam that follows the caster), Dash (move to the cursor, damage along the path, FinalDamage at the end in Radius), Blink (teleport, Damage at both ends), Zone (persistent at the cursor: Damage every Tick for Duration, then FinalDamage), Volley (Count projectiles in a Width-degree fan, Pierce; Duration/Tick = extra waves; Width 360 = all round), Barrage (Count strikes of Radius scattered within Width over Duration, Delay telegraph each), Chain (from the cursor: Count hits, Radius first reach, Width jump range), Self (transformation: buffs for Duration + a damage aura of Radius every Tick), Totem (summon at the cursor for Duration: every Tick shoots the Count nearest enemies within Width), Global (every reaper on the map + a blast of Radius around each) |
| Effect | A list joined with the pipe character, of: Slow / Freeze (EffectTime s), Knockback / Pull (Tuning `AbilityKnockback` / `AbilityPullStrength`), Mark (toughest enemy takes +EffectValue damage for EffectTime), Heal (+EffectValue x max HP), Shield (EffectValue x max HP for the buff time), Haste / Empower / Fortify (+speed / +damage / -damage taken by EffectValue for the buff time), Revive (downed reapers stand up at EffectValue), Lifesteal (EffectValue x max HP per enemy hit, max 25% a cast) |
| Damage, FinalDamage, Radius, Range, Width, Count, Pierce, Cooldown, Duration, Tick, EffectValue, EffectTime, Delay | Numbers for the shape (Delay = telegraph before a Circle / Nova / Global lands; pulls start at once) |
| EvoDamage, EvoRadius, EvoCount, EvoCooldown, EvoDuration | Evolved multipliers / additions (EvoDamage also scales EffectValue; EvoRadius also scales Line / Dash / Cone width) |
| EvoEffect, EvoValue, EvoTime | An extra effect when evolved, with its own magnitude / seconds |
| EvoText | UI line for the evolution |
| Anim, Fx | Cast clip fallback list (`SQ_Cast_*_R2`, `SQ_Attack_*_R2`, `SQ_Throw_R2`, `SQ_Ability_*`, `Reaper_Scythe_*`, then KayKit), effect flavour (Pillar, Embers, Holy, Time = rising light; Shards, Stamp, Dust, Paper = falling debris; Smoke, Ghost, Blood, Crows = smoke; Sparks, Slash, Streaks, Lightning = sparks) |

Buff time = Duration when > 0, else EffectTime. Self shapes: the caster's own Empower / Fortify are personal; Haste and evolved
effects also reach reapers within Radius. Buffs of the same kind do not stack (the strongest / longest wins).

### Passive numbers

| PassiveId | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| TeamDamage | kills per rally | crew damage bonus | seconds | range |
| HitAndRun | E seconds cut per kill | speed bonus for 1.5 s | max share of each E cooldown cut | - |
| SlowSouls | extra souls per slowed kill (read by ASQHordeManager) | - | - | - |
| Builder | kills per sentry | sentry seconds | sentries at once (+ TurretBonus) | shot damage |
| Lamplight | radius | HP/s to reapers inside | slow seconds per pulse | - |
| DashStrike | dash damage | E seconds cut per enemy hit | max seconds cut per dash | - |
| OnTheJob | every Nth cast echoes | echo delay | echo strength | - |
| Fetch | soul pull range | speed x while carrying a skull | - | - |
| Bulwark | range | damage cut (best Bulwark counts, max 60%) | - | - |
| Collections | damage per soul carried | max bonus | - | - |
| Triage | seconds between heals | range | heal x max HP (+8) | shield cap x max HP |
| Scorch | burn damage / s | patch radius | patch seconds | seconds of trail per dash |
| Lookout | seconds between marks | range | mark bonus | mark seconds |
| Spite | max damage bonus at 0 HP | - | - | - |
| Audit | every Nth kill | extra souls | blast radius | blast damage |
| Flock | max crows | seconds per crow leaving | aura radius | damage per crow per hit (2 hits / s) |
| LastRites | seconds to revive | range | health given back | - |
| MobileBank | seconds between deposits | range | share of each sack banked | - |
| Seance | chance per kill (elites always) | ghost seconds | ghosts at once | shot damage |
| Stampede | trample damage | radius | seconds between tramples | share of max speed that counts as a gallop |
| Stopwatch | seconds between time stops | radius | freeze seconds | - |

## 3. How it runs (USQAbilityComponent)

- **Casting** is server-authoritative. The owner calls `TryCastAbility(2 or 3, cursor ground point)`; the component clamps the
  range and sends `ServerCast` (reliable, `FVector_NetQuantize`). The server checks fighting phase, not downed / carried / picking,
  team level >= unlock, cooldown (0.2 s slack) and clamps again, then applies everything through ASQHordeManager
  (`ApplyAreaDamage`, `ApplyLineDamage`, `ApplyChainDamage`, `SpawnProjectile`, `ApplyKnockback`, `FreezeArea`, `MarkToughestEnemy`
  and two new minimal APIs: `ApplyPull`, `ApplyConeDamage`). Kills and boss damage are credited to the caster (quests, souls,
  damage numbers through `MulticastDamagePops`).
- **Cooldowns** replicate to the owner only (`ReadyAtE/R`, server time). Haste buffs replicate to everyone (movement prediction).
- **Cosmetics**: `MulticastCast` (anim + effect, local from Kits.csv), `MulticastAbilityHit` (telegraphs, impacts, buff bursts),
  `MulticastAbilityLines` (bolts, chains). Every R adds a light pillar, staggered shockwaves and a white core flash.
  Passive moments play through the existing `MulticastAbilityAnim` path (clips from `PassiveAnim`).
- **Aiming**: `QuickCast 1` (default) casts on press with a short indicator flash. `QuickCast 0`: first call starts aiming (a
  local dotted ring / line / cone under the cursor, range ring around the reaper), the next call for the same slot casts.
- **Bots** cast from the component every `AbilityBotInterval`: R at >= `AbilityBotDensityR` enemies in reach (or a boss, or a
  downed teammate for revive Rs), E at >= `AbilityBotDensityE`, aimed at the densest cluster (or the boss).
- **View API** for the HUD / lobby: `GetAbilityView(Slot)` (0 weapon, 1 passive, 2 E, 3 R), `GetPassiveName()`,
  `GetPassiveDescription()`, static `DescribeKit(Data, ReaperId, OutSlots)`.

## 4. Balance (Scripts/balance_model.py section 12) - superseded by section 6 (remake balance pass)

The model treats the signature as the main weapon (outside the shared slots), adds E / R / passive DPS and utility to every build,
and compares reapers on **clear speed = 0.6 x horde DPS + 0.4 x single-target DPS** (each vs the median reaper), against a role target
(utility roles clear a little slower). Signature weapons were fitted to a weapon score of 1.08 (beams and tight orbits a bit lower,
they clear crowds better than their score says), evolutions to a +55% spike. Each reaper's E / R damage was then fitted to its role
target at nights 2-3.

```
-- 12. Kits: clear speed and power vs the median reaper (night 1 / 2 / 3); Abil = E+R share of horde DPS at night 3
  Reaper       Role         Target Clear n1/n2/n3    Horde n1/n2/n3    Power n1/n2/n3    Util3  Abil3  Dev3   Flag
  Vexa         Berserker    1.08   1.03/1.03/1.14    0.80/0.89/0.98    1.02/1.09/1.17       23%   20%     +6% 
  Monk         Striker      1.08   1.06/1.07/1.13    0.77/0.88/0.93    0.98/0.99/1.01        7%   25%     +4% 
  Tallow       Striker      1.08   1.05/1.07/1.11    1.03/0.97/1.00    0.97/0.96/0.95        3%   36%     +2% 
  Neckless     Speed        1.04   0.86/1.01/1.09    0.75/1.03/1.07    0.81/1.00/1.04       14%   23%     +5% 
  Grim         All-rounder  1.00   1.04/0.99/1.07    1.18/1.16/1.17    1.01/0.98/1.02       15%   14%     +7% 
  Lookout      Scout        1.00   0.84/0.97/1.04    0.60/0.80/0.82    0.88/1.03/1.07       23%    6%     +4% 
  Collector    Greed        1.02   1.18/1.06/1.04    1.17/0.96/0.93    1.10/0.98/0.92        7%   38%     +2% 
  Intern       Wildcard     1.00   1.10/1.10/1.01    1.22/1.31/1.15    1.02/1.02/0.90        7%   33%     +1% 
  Tickwell     Time         0.96   1.12/0.97/1.01    1.34/1.17/1.15    1.11/0.99/0.99       18%   25%     +5% 
  Ectoplasm    Summoner     1.00   1.14/1.07/1.01    1.00/1.10/1.00    1.05/0.96/0.86        2%   15%     +1% 
  Widow        Control      0.96   0.95/0.96/1.01    1.00/1.10/1.10    0.97/1.00/1.01       21%   24%     +5% 
  Mort         Hauler       0.98   1.04/1.03/1.00    0.99/0.91/0.87    1.00/0.98/0.91        9%   46%     +2% 
  Sweep        Mobility     1.00   1.26/1.09/1.00    1.25/1.24/1.09    1.18/1.02/0.89        7%   23%     -0% 
  Femur        Fetcher      0.96   0.91/1.00/0.97    0.88/1.00/0.95    0.97/1.10/1.03       27%   36%     +1% 
  Actuary      Economy      0.96   0.86/1.00/0.97    0.84/0.99/0.95    0.83/0.95/0.88        9%   35%     +1% 
  Digby        Defender     0.92   0.91/0.89/0.96    0.88/0.97/1.01    0.98/0.97/1.00       25%   22%     +5% 
  Keeper       Banker       0.92   1.10/0.98/0.95    1.32/1.17/1.09    1.42/1.32/1.22       53%   33%     +3% 
  Bonesmith    Defender     0.92   0.91/0.95/0.93    0.66/0.70/0.71    0.97/1.04/0.98       27%   43%     +1% 
  Solace       Reviver      0.88   0.95/0.89/0.93    1.12/1.06/1.04    1.17/1.25/1.26       62%   16%     +6% 
  Beak         Healer       0.88   1.03/0.95/0.90    1.17/1.09/0.99    1.30/1.25/1.13       51%   34%     +2% 
  Pallbearers  Mobile bank  0.88   0.78/0.89/0.89    0.64/0.75/0.77    0.82/0.98/0.95       28%   49%     +1% 
  0 / 21 reapers outside +-15% of their role target at night 2 or 3
```

**Boss HP.** The extra signature slot and E / R raise single-target DPS by ~25-35%. `EconomyBossHealth` went 0.87 / 0.9 / 1.3 ->
**1.1 / 1.09 / 1.41**: boss time-to-kill is back to **0.91-1.09** of target (1, 2 and 4 players). Horde HP was left alone, so
hits-to-kill with the main weapon is unchanged (night 3 solo 2.1-3.2); the horde clear ratio rises (that is what the kits are for).

Model limits: ability hit counts use a flat enemy density (5 enemies inside 300 cm) with caps (16 for E, 32 for R); control and
utility values (`CONTROL_K`, crew buff reach, heal / shield value) are judgement; casts are assumed at 80-85% uptime. Check in play:
R one-shots most of the night-3 horde by design; if a kit feels weak or strong, change its `Damage` in Kits.csv and rerun the model.

## 5. Changed systems

- Reapers.csv: `StartWeapon` = the signature; `Passive`, `PassiveBase`, `PassivePerLevel`, `PassiveText` columns are blank and no longer
  read (SQData copies the kit's passive id / text into `FSQReaperDef::Passive / PassiveText` for existing UI code).
- Old passives removed from ASQCharacter (ServerTickPassive, Intern bumps, Chimney Sweep glide and extra jumps, Banker bank speed,
  Builder / Hauler / Audit / Rooftops stat passives). Kept mechanics moved into the component with new numbers.
- The Intern no longer starts with a random weapon: he has his own (Reply-All).
- Tuning: new "Reaper kits" block; old passive keys removed; `WeaponSlots` now means shared slots on top of the signature.

## 6. Remake balance pass (Nov 9 2026; Docs/Balance.md section 11)

The kits were re-fitted inside the new **run simulation** (`Scripts/balance_model.py` section 22): each reaper plays the reference
mission (Plague Carnival, normal, solo, the Death's Office ranks a player has by then) over 10 seeds with real level-up cards, chest
items, shrine raises and augments; clear speed = 0.6 x horde DPS + 0.4 x single-target DPS of the reaper's own build (augments
removed, so augment luck is not credited to the kit), vs the median reaper, against the role target (ROLE_CLEAR). Signature weapon
damage and E / R damage were scaled together until every reaper sat on its target; evolutions were re-fitted to +55%.

What changed in the way the kits are judged:
- **Marks no longer count against bosses** (the code only marks horde enemies): Lookout, Lantern Keeper and Debt Collector lost
  boss value; Lookout's passive mark went 20% -> 12% so its elite value is not overstated either.
- **Drops use their radius**: Buried Bone and Denial Stamp (and shared Gravequake / Tombstone) were under-valued by the old flat model.
- **Utility trims** (supports' power index was 1.13-1.26): Keeper lamp 450 cm / 1.5 HP/s -> 350 / 0.6, Daybreak heal 30% -> 15%;
  Beak Triage 6% -> 4.5%, Tonic 12% -> 8%, House Call 35% -> 25%; Solace Sanctuary shield 20% -> 12%, Resurrection 50% -> 20%;
  Vexa Black Veil 40% -> 30%. Tallow's Scorch trail 35 -> 12 /s (it was most of Tallow's damage). Power index now 0.82-1.09.
- `AbilityDamagePerLevel` 0.05 -> 0.03 and the biggest Rs trimmed (Bankruptcy Notice 580 -> 186 with cooldown 70 -> 55 s, Daybreak,
  House Call, Howl, Open Grave) so late ability crits stay in the low thousands (~3.4k; a few single R crits ~4k).
- Signature weapon card Weight 10 -> 25: the reaper's own weapon is offered ~2.5x as often (it was ~9% of cards and often not maxed
  by the bell).

Current numbers (level-1 values; E / R hits x (1 + 0.03 x (team level - 1)) x damage x crit; evolved x the Evo columns):

| Reaper | Signature lv1 dmg (+/lv) -> evolution | Passive P1-P4 | E dmg (+final) / cd | R dmg (+final) / cd | E/R effect value |
|---|---|---|---|---|---|
| Grim | CompanyScythe 7.99 (+1.36) -> SeverancePackage 30.9 | 60 / 0.15 / 4 / 900 | 13 +13 / 8s | 74 / 70s | 0 / 0 |
| Mort | ExpressSickles 8.83 (+2.22) -> NextDayDelivery 25.8 | 0.25 / 0.12 / 0.5 / 0 | 43 / 7s | 35 / 60s | 0 / 0.4 |
| Widow | Lament 5.11 (+1.85) -> EternalLament 22.1 | 1 / 0 / 0 / 0 | 33 / 8s | 19 +102 / 75s | 0 / 0 |
| Bonesmith | OssifiedSentry 17.78 (+5.32) -> BoneCitadel 50.9 | 18 / 10 / 2 / 9 | 123 / 10s | 156 / 80s | 0.18 / 0 |
| Keeper | LanternPulse 16.69 (+4.24) -> Lighthouse 65.6 | 350 / 0.6 / 0.6 / 0 | 132 / 8s | 318 / 75s | 0.25 / 0.15 |
| Monk | MalaOrbit 5.61 (+1.79) -> HundredBeadHalo 16.6 | 30 / 0.2 / 2 / 0 | 40 +70 / 6s | 57 / 55s | 0 / 0 |
| Intern | ReplyAll 17.31 (+4.74) -> ReplyAllStorm 63.2 | 3 / 0.5 / 0.6 / 0 | 19 / 6s | 0 +199 / 65s | 0 / 0 |
| Femur | BuriedBone 29.93 (+6.46) -> Excavation 97.2 | 1000 / 1.25 / 0 / 0 | 33 +68 / 7s | 163 / 60s | 0 / 0.35 |
| Digby | ShovelSweep 26.82 (+7.35) -> MassGrave 93.6 | 600 / 0.18 / 0 / 0 | 49 / 7s | 0 +235 / 70s | 0 / 0 |
| Collector | FinalReminders 3.68 (+1.1) -> DebtSpiral 10.2 | 0.008 / 0.6 / 0 / 0 | 64 / 7s | 186 / 55s | 0 / 0.6 |
| Beak | Miasma 5.94 (+2.02) -> Quarantine 30.3 | 3 / 900 / 0.045 / 0.4 | 156 / 9s | 246 / 90s | 0.08 / 0.25 |
| Tallow | WickBolt 3 (+0.8) -> Blowtorch 14 | 12 / 110 / 3 / 0.35 | 40 / 6s | 30 / 55s | 0 / 0 |
| Lookout | Searchlight 7.5 (+2.3) -> Floodlights 30 | 5 / 1500 / 0.12 / 4 | 12 / 8s | 9 / 70s | 0.2 / 0.15 |
| Vexa | SpiteWreath 2.02 (+0.69) -> CrownOfSpite 7.4 | 0.4 / 0 / 0 / 0 | 14 +13 / 7s | 8 / 60s | 0.025 / 0.3 |
| Actuary | DenialStamp 4.75 (+1.34) -> AuditTrail 19.4 | 10 / 2 / 180 / 40 | 77 / 8s | 179 / 65s | 0 / 0 |
| Sweep | CrowRelay 16.54 (+4.54) -> Murmuration 63.5 | 8 / 1.5 / 230 / 6 | 22 / 7s | 27 / 55s | 0 / 0 |
| Solace | HymnOfSolace 11.72 (+3.98) -> RequiemMass 59.7 | 2.5 / 250 / 0.4 / 0 | 32 / 10s | 178 / 90s | 0.12 / 0.2 |
| Pallbearers | PallTurret 11.13 (+3.22) -> FullProcession 39.5 | 2 / 350 / 0.25 / 0 | 85 / 9s | 38 / 75s | 0 / 0.3 |
| Ectoplasm | SeanceLink 19.52 (+5.34) -> FullSeance 71.3 | 0.05 / 8 / 3 / 14 | 35 / 7s | 32 / 70s | 0 / 0 |
| Neckless | PumpkinGlare 12.38 (+3.85) -> HeadlessHorror 52 | 7 / 160 / 0.25 / 0.8 | 21 / 7s | 16 / 60s | 0 / 0.5 |
| Tickwell | TollingHour 17.98 (+4.84) -> Midnight 97.4 | 14 / 700 / 1.75 / 0 | 32 / 7s | 146 / 80s | 0 / 0 |

Model result at the bell (section 22; Target = role clear target, Power = clear x (1 + utility %) vs the median):
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
Known gaps: Tallow is kept +13% over its role target so Wick Bolt hits stay readable (3 + 0.8 / level); Intern and Debt Collector are
~7-10% under. With 10 seeds the per-reaper noise is ~+-6%, so anything inside +-12% is "on target" until playtests say otherwise.

## 7. Reap core (Nov 9 2026; Docs/Balance.md section 12)

- **R is charged, not cooled down.** R (slot 3) fills from reaped souls (`UltChargePerReap` 0.015 each) and kills (`UltChargePerKill`
  0.00075), both x `UltRefCooldown` 70 / the kit's R cooldown below (evolution and haste included), so the R Cooldown column is now
  the R's charge pace: a 55 s R fills ~1.6x as fast as a 90 s one. Casts at 100% and resets; level-6 unlock kept. Reaping within 600 cm
  of a teammate gives them 50% of the raw charge. Augment refunds on the R (Ruthless Rhythm) become charge. E keeps its cooldown.
  API: `USQAbilityComponent::GetUltCharge01()`; `GetAbilityView(3)` reports Cooldown = missing charge in percent of CooldownMax 100.
- Model: the average stage R pace is 1.07-1.21 x the old cooldown solo and 0.87-0.91 x in 4-player crews (shared charge), so R damage
  was left as it was.
- **Kill-count passives** (about half the kills per run with the early curve), re-fitted to the same uptime: Grim rally 60 -> 32 kills,
  Swift E cut per kill 0.25 -> 0.45 s, Bonesmith sentry 18 -> 10 kills, Actuary audit every 10th -> 6th kill, Chimney Sweep crow leaves
  every 1.5 -> 2.8 s, Ectoplasm ghost chance 5% -> 9%.
- **Outliers trimmed / lifted**: Intern E 19 -> 28, R final 199 -> 290; Chimney Sweep E 22 -> 28, R 27 -> 36; Widow E 33 -> 26,
  R final 102 -> 75; Lookout E 12 -> 10, passive mark +12% -> +6%. Still out (model section 22): Tallow +16% (kept), Widow +16% and
  Lookout +13% (their strength is the signature weapon, Upgrades.csv), Intern -10%.

## Ability mods (character pass, Nov 25 2026)
`Kits.csv` columns `<Slot>_Mod`, `<Slot>_ModValue`, `<Slot>_ModTime` (empty = none). Unknown mods are reported in the data error banner.
| Mod | Slot | ModValue | ModTime | Who |
|---|---|---|---|---|
| Charges | E | charges (each refills in one cooldown, one at a time) | - | Mort |
| Recast | E | snap-back damage x Damage | window to press E again | Tickwell |
| Stacks | E | +damage per stack (max 3) | chain window | Monk |
| Trap | Circle | most armed at once (oldest goes off) | life | Pallbearers |
| Detonate | any | extra bursts (FinalDamage, else Damage) at the target | gap between bursts | Intern E + R, Neckless E |
Tuning: ChargeLockout, RecastLockout. The HUD shows charges / stacks on the E slot ("x2").
Movement traits live in `Reapers.csv` (Trait*), see Docs/Characters.md.
