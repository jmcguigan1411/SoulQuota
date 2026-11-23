# Soul Quota - Content Plan to Beat Megabonk

Based on `MegabonkAnalysis.md` (Nov 2026 research, sources there) and the current design (`GameDesign.md`, `Content/Data/*.csv`).
All names below are original. IP risks are flagged in section 14.

**Priority key:** **EA** = Early Access launch. **PL** = post-launch (free updates). **Stretch** = only if time allows.

---

## 1. The strategy in one paragraph

Megabonk won on an addictive loop, 3D verticality, silly characters and many unlocks. It is weak on: **no co-op**, **only 3 maps**, **RNG decides runs**, **players need a wiki to understand anything**, **no easy mode**, **no updates for 9 months**, **an objective that is just "survive"**, and **meme humour that annoys some players**. Soul Quota should **match** Megabonk's density of unlocks and secrets, then **beat** it where it is weak: co-op, a real objective (the soul quota), more distinct maps, readable systems and a steady update cadence. Do not try to beat 85 items on day one. Beat it on **variety per hour** and **things to do with friends**.

---

## 2. Count comparison (summary table)

| Category | Megabonk (Nov 2026) | Soul Quota now | **EA target** | **Post-launch target** |
|---|---|---|---|---|
| Playable characters | 21 | 6 reapers | **10** | 16 |
| Weapons | 30 (no evolutions) | 24 | **24** | 36 |
| Weapon evolutions | 0 | 24 | **24** (one per weapon) | 36 + 8 Duet evolutions (co-op) |
| Stat passives (Megabonk Tomes -> our **Tomes**, 4 slots) | 23 | 16 | **22** | 28 |
| Items (chest drops, stack, no slots) | 85 | 23 | **40** | 90 |
| Co-op Bonds | 0 | 8 | **12** | 20 |
| Maps | 3 | 5 biomes | **6** | 9 (8 + endless office) |
| Bosses + minibosses + secret bosses | ~15-16 | 7 bosses | **7 bosses + 6 minibosses + 2 secret** | 10 + 10 + 5 |
| Enemy types | ~20 (est.) | 10 | **16** | 26 |
| Interactable types | ~15 | lantern + shop | **12** | 18 |
| Quests / unlock goals | 240 | 0 | **120** | 250 |
| Achievements | ~102 | 0 | **60** | 120 |
| Cosmetics | 21 hats + 1 skin | 0 | **20 hats** | 40 hats + reaper skins |
| Difficulty | 3 tiers per map + ~13-17 challenges per map | 1 | **3 Quota Grades per map + 10 Grievances + Union Rules (assist)** | 15 Grievances, Endless ranks |
| Modes | Tiers, Final Swarm (endless), leaderboards | Endless planned | **Story run, Endless, Daily Roster (seeded)** | Weekly Contract, co-op leaderboards |

Why 40 items and not 85: Megabonk's 85 includes ~30 simple "+X% stat" items. Ours should each do something visible (proc, behaviour or co-op effect). 40 distinct ones plus 24 evolutions plus 12 Bonds is more *decisions* than Megabonk's set.

---

## 3. Characters - Reapers (Megabonk 21 -> EA 10, PL 16)

Megabonk's best-loved characters change **how you move or play** (Monke climbs, Birdo glides, Roberto spawns chests), not just "+1% stat". Every reaper should have **one mechanical passive** and **one co-op role**. Megabonk cannot do roles because it has no co-op.

| # | Reaper | Role | Start weapon | Passive (mechanical) | Unlock | Phase |
|---|---|---|---|---|---|---|
| 1 | Grim | All-rounder | Scythe Sweep | +10% damage per nearby teammate | Start | EA (exists) |
| 2 | Swift the Hauler (renamed from Mort the Courier, see IP) | Hauler | Thrown Sickles | +50% carry, faster dash | Start | EA (exists) |
| 3 | The Wailing Widow | Control | Banshee Wail | Slowed enemies drop +1 soul | Start | EA (exists) |
| 4 | Bonesmith | Defender | Bone Turret | +1 turret | Start | EA (exists) |
| 5 | The Lantern Keeper | Banker | Lantern Flare | Banks 3x faster, +10% banked | Soul coins | EA (exists) |
| 6 | The Hollow Monk | Striker | Prayer Beads | Dash damages everything in its path | Soul coins | EA (exists) |
| 7 | **Doctor Beak** (plague doctor) | **Healer** | Embalmer's Syringe (new) | Heals the lowest-HP teammate in range; healing overflow becomes shield | Revive 25 teammates (solo: revive yourself 5 times via Pardon) | EA |
| 8 | **The Intern** | Wildcard | Random weapon each run | "Learning on the Job": +XP; each level-up rerolls one stat at random (Dicehead-like) | Complete 30 Performance Reviews | EA |
| 9 | **Femur the Hound** (skeletal hound reaper; shipped name) | Fetcher | Skull Bowling (new) | Dropped souls within range fly to him; can carry a downed teammate's skull at full speed | Find the Dog Collar secret in the Graveyard | EA |
| 10 | **The Graveyard Lookout** (shipped name) | Scout | Watchman's Rattle (new) | Reveals Keepsake boxes and elites on the map; pings mark enemies for +15% team damage | Bank 5,000 souls total | EA |
| 11 | **The Pallbearers** (four tiny skeletons carrying a coffin) | Mobile bank | Coffin Slam (new) | The coffin is a **mobile mini-lantern**: teammates can bank 25% of their stack into it; slow movement | Bank in Final Call 50 times | PL |
| 12 | **The Actuary** (shipped name) | Debuffer | Red Ink (new) | Marked enemies take +20% damage and drop +1 soul; crits mark | Meet quota on every night of a run without anyone downed | PL |
| 13 | **Madame Ectoplasm** (medium) | Summoner | Crystal Séance (new) | Killed elites return as allied ghosts for 10s | Defeat 3 bosses with 0 Defiance stacks | PL |
| 14 | **The Chimney Sweep** | Climber | Soot Brush (new) | **Climbs walls and runs along rooftops** (Monke-like) | Clear Gaslight Rooftops Grade 2 | PL |
| 15 | **Sir Neckless** (shipped name) | Speed | Pumpkin Lob (new) | Mounted: very fast, cannot jump; can tow one teammate | Clear Hollow Acre Grade 2 | PL |
| 16 | **Old Tickwell** (shipped name) | Time | Pendulum (new) | Every 30s stops time around him for 1.5s (ties to the Merchant) | Defeat the Merchant of Hours | PL |

Unlock style: copy Megabonk's "do a weird thing" quests (players love hunting them), but **show the quest in-game** (Megabonk players rely on wikis).

---

## 4. Weapons (Megabonk 30 -> EA 24, PL 36)

Current behaviours: Arc, Projectile, Aura, Orbit, Nova, Drop, Turret. Megabonk behaviours we lack are listed in the analysis. Add them with our own spin, plus **co-op-only behaviours** that Megabonk cannot have.

**EA additions (+8):**

| Weapon | Behaviour | Note |
|---|---|---|
| Séance Chain | Chain (ghost lightning jumps between enemies) | |
| Ectoplasm Trail | Trail behind you | Synergy with hauling routes |
| Grave Pit | Pull vortex | Groups the horde for Drop weapons |
| Rat Traps | Traps/mines | Place near the lantern to guard it |
| Homing Moths | Homing swarm | |
| **Soul Leash** | **Tether** between you and the nearest teammate (solo: your lantern). Enemies crossing it take damage | **Co-op signature weapon** |
| Skull Bowling | Bouncing skulls | Femur the Hound's starter |
| Final Notice | Execute: enemies below X% HP are "served notice" and die | Boss-safe: deals bonus damage instead |

**PL additions (+12):** Boomerang Headstone (boomerang), Candle Beam (beam), Embalmer's Syringe (DoT darts, Doctor Beak), Watchman's Rattle (cone stun), Coffin Slam, Red Ink, Crystal Séance, Soot Brush, Pumpkin Lob (mortar), Pendulum (sweeping pendulum), Ferry Coin (crit roll: heads = crit forever this run; Dice-like), Organ Dirge (manual-aim cannon, optional aim-assist).

**Evolutions:** one per weapon (EA 24). Show recipes in the Codex from the start. Megabonk has no evolutions, so this is a clear "more depth" selling point.
**Duet Evolutions (PL, co-op only):** two reapers each holding a maxed weapon and standing together for 3s fuse a shared super-weapon, e.g. Scythe Sweep + Crow Swarm = "The Reaping Flock". Very clip-able (pillar 2).

---

## 5. Relics = our Tomes (Megabonk 23 -> EA 22, PL 28)

Keep relics as levelled stat passives in limited slots (like tomes). Add the stats Megabonk has and we lack:

| New relic | Stat | Phase |
|---|---|---|
| Skeletal Rabbit's Foot | Luck (rarity of offers) | EA |
| Monocle of Scrutiny | Crit chance | EA |
| Night School Diploma | XP gain | EA |
| Shroud of Mist | Evasion | EA |
| Embalmer's Patience | Duration | EA |
| Grave Wind | Knockback | EA |
| Soul Coin Purse | Soul coin (meta currency) gain | EA |
| Cursed Contract | +Difficulty, +souls (Cursed Tome analogue; ties to the quota greed pillar) | PL |
| Rubber Stamp of Chaos | Random stat each level | PL |
| Dust Ward | Shield | PL |
| Quickened Quill | Projectile speed | PL |
| Soul Ballast | Carrying souls no longer slows you (see Movement) | PL |
| Esprit de Corpse | +stats per teammate alive (solo: per relic slot filled) | PL |

---

## 6. Items = **Keepsakes** ("Lost Property" the dead leave behind) (Megabonk 85 -> EA 40, PL 90)

A new category. They come from coffins, the Lost Property Box and the Pawnbroker. They stack. Rarities: Common / Rare / Epic / Legendary. Each must have a **visible** effect. **Co-op Keepsakes** (marked C) affect teammates - Megabonk has none.

**Currency choice (big design win):** buy coffins and Pawnbroker goods with **carried souls**. Every purchase is souls you are not banking toward quota. This drives pillar 1 (greed vs safety) in a way Megabonk's gold never can.

**EA Common (12):** Grandad's Dentures (+dmg to adjacent enemies), Funeral Sandwich (kills 2% drop a heal sandwich), Wilted Lilies (+regen), Rusty Pocket Watch (+XP), Lucky Penny (+luck), Black Armband (+armor), Comfy Slippers (+move speed), Thermos of Tea (+attack speed), Spare Femur (+max HP), Hymn Sheet (-cooldown), "Hello, I'm Dead" Name Tag (+elite spawns, +loot), Shovel Head (coffins cost 15% fewer souls).
**EA Rare (10):** Bag of Marbles (+1 projectile on all weapons), Toe Tag (elites drop a free Keepsake 10%), Ouija Planchette (hits 10% summon a spirit bolt), Whoopee Cushion (when hit: stink cloud poisons nearby), Mourning Veil (after a dash, next hit 2x), Grave Robber's Gloves (2% chance kills drop 3 souls), Wind-up Bat (bats orbit after level-up), Tea Party Set (C: standing next to a teammate heals both), Carpool Lantern (C: +15% move speed while within 5m of a teammate), Bell Jar (when banking, emit a damaging pulse).
**EA Epic (10):** Haunted Music Box (stand still: damage builds to +80%), Last Will and Testament (C: if you go down, teammates get 25% of your damage for 20s), Matching Tattoos (C: when a linked teammate crits, you heal), Gravedigger's Contract (Gravediggers drop Keepsakes but spawn 2x), Crow's Feathered Cloak (+1 jump, +40% damage while airborne), Tombstone Sneakers (every 20m moved drops a tombstone that hits enemies), Moth Bomb Jar (kills 15% release homing moths), Mourning Statue (enemies that look at you are slowed), Rattling Ribcage (thorns explode), Bottled Midnight (Final Call lasts 10s longer).
**EA Legendary (8):** Stopped Clock (stops one fatal hit by freezing time; 3 per run - the Merchant's own watch), The Big Snip (2% of hits deal 20x), Death's Spare Hood (+1 on-hit proc chance), Golden Urn (+5 weapon level caps), Bottomless Pocket (+50% carry cap; carried souls add damage), Choir of the Departed (kills spawn 2 homing souls; ties to the Soul Harvester role), Signed Resignation (C: one teammate per night can be instantly revived anywhere), Ferryman's Oar (dash leaves a river that drags enemies away).

**PL (+50):** fill out each rarity to ~22, add 10 more Co-op Keepsakes, 6 biome-specific Keepsakes (Harbour pearl, Carnival ticket, Monastery prayer bell...) and joke items like Megabonk's Kevin (e.g. "Clingy Ghost": 25% chance a ghost hugs you - harmless, blocks one hit).

---

## 7. Maps (Megabonk 3 -> EA 6, PL 9)

Megabonk: 3 procedural maps (Forest, Desert, Graveyard + Crypt). IGN called out the "lack of changing scenery". Players praise drops, bottlenecks and rewards out of reach. They dislike precise platforming and lava floors. Our maps use the existing **height-grid terrain** (plateaus, ramps, ridges, jump pads in `Tuning.csv`). Each map adds **one signature vertical feature** and **one gimmick**. No precise platforming; fall damage is gentle (big drops only, never downs you; a slide-roll landing is safe).

| # | Map | Theme | Layout / verticality | Gimmick | Night bosses (N1 / N2 / N3) | Phase |
|---|---|---|---|---|---|---|
| 1 | **The Overflowing Graveyard** | Classic cemetery at night | Rolling hills, 4 plateaus with mausoleum roofs, **sunken crypt basements** (2nd level below ground, short-cut tunnels) | None (tutorial). Open graves pop out bonus Shamblers | Bell Ringer / Gravewarden / **The Sexton** | EA (exists) |
| 2 | **The Drowned Harbour** | Rotting docks, shipwrecks | 3 layers: **seabed** (only walkable at low tide, full of loot), **piers**, **ship decks and crow's nests**. **Rope slides** between masts | **Tide** every 45s: slows players, drowns the horde, exposes or covers the seabed | Gravewarden / Bloated Mourner miniboss / **The Drowned King** | EA (exists) |
| 3 | **The Plague Carnival** | Cursed travelling fair | **Rollercoaster grind rail** loops the whole map (fast hauling route), rotating **Ferris wheel** platforms, big-top tent interior | **Spotlights** roam: kills inside drop double souls | Strongman miniboss / Bell Ringer / **The Ringmaster** | EA (exists) |
| 4 | **The Frozen Monastery** | Mountain monastery in snow | Cliff terraces linked by stairs, two bell towers, a **frozen lake** at the bottom (slopes = slide speed) | **Ice**: players slide fast, horde crawls; avalanche lanes at Grade 3 | Ice Bloat miniboss / Gravewarden / **The Abbot Who Would Not Kneel** | EA (exists) |
| 5 | **The Ossuary Mines** | Bone mines under the city | Underground, 3 levels joined by **lifts and minecart rails**; collapsing tunnels | **Darkness**: you see only inside lantern light and carried-soul glow (greed = visibility). Lantern is on a cart that moves every night | Pit Canary swarm miniboss / Tunnel Brute / **The Foreman of Pit Nine** (signed to "never stop digging") | **EA (new)** |
| 6 | **The Clocktower** | The Merchant's lair | Vertical **spiral** around the giant clock face, **rotating gear platforms**, pendulum bridge | **Time stop** every 30s for 4s | Gear Golem miniboss / Bell Ringer revisited / **The Merchant of Hours** (final) | EA (exists) |
| 7 | **Gaslight Rooftops** | Victorian city of smoke | Streets (horde) vs rooftops (safer, slower to bank); **washing-line rails** and **chimney jump pads** link roofs | **Pea-souper fog** rolls in (like Megabonk's sandstorm), but the lamplighter lights a path to the lantern | Pickpocket Gang (souls thieves) / Lamplighter / **The Chimney King** | PL |
| 8 | **Hollow Acre** | Harvest fields and windmill | Cornfield maze whose rows regrow each night, windmill tower (sails are a rotating platform), scarecrow posts | **The Thresher**: a giant harvester sweeps rows - flattens the horde, knocks souls out of careless players | Crow Matron / Scarecrow Pair / **Old Strawman** (refused to be taken down) | PL |
| 9 | **The Waiting Room** (endless) | Infinite afterlife bureaucracy | Filing-cabinet towers, conveyor-belt in-trays (moving floors), take-a-number queue | "**Now Serving**": a random ticket number is called; that player's banking pays 3x for 20s | Rotating bosses; **The Undersecretary of Delays** at night 10 | PL (Endless mode) |

**Map rules for everyone:**
- Each map has **3 Quota Grades** (like Megabonk's tiers). Grade 1 = 1 night, Grade 2 = 2 nights, Grade 3 = full 3-night run.
- Procedural layout with **hand-made landmark pieces** (crypt, Ferris wheel, clock), so each map is recognisable but each run is different.
- Each map has **2-3 secrets** (Megabonk's banana, Monke cage and boomboxes are much-loved): e.g. Graveyard Dog Collar, Harbour message in a bottle, Carnival "strength tester" bell (ring it with a Drop weapon), Monastery hidden choir loft.

---

## 8. Interactables (Megabonk ~15 types -> EA 12, PL 18)

Office-humour versions of Megabonk's shrines, plus co-op ones.

| Interactable | Megabonk analogue | What it does | Phase |
|---|---|---|---|
| **Signing Desk** | Charge Shrine | Stand in the radius to charge; pick 1 of 3 relic boosts. **Charges faster per reaper inside** (co-op) | EA |
| **Golden Signing Desk** | Golden Charge Shrine | Rare; Legendary choices | EA |
| **Lost Property Box** | Moai | Pick 1 of 3 Keepsakes | EA |
| **Coffins** | Chests | Cost **carried souls**; price rises each time. Free coffins from elites and minibosses | EA |
| **Double-Locked Coffin** | none | Needs **two reapers** to pull the handles at once (solo: needs a Skeleton Key Keepsake). Guaranteed Epic+ | EA |
| **The Pawnbroker of the Dead** | Shady Guy | Sells 2-3 Keepsakes for souls; his hat colour shows rarity | EA |
| **The Photocopier** | Microwave | Feed it a Keepsake + one of the same rarity -> a copy. Jams sometimes (joke). | EA |
| **Greed Ledger** | Greed Shrine | +5% difficulty, +souls per kill this night | EA |
| **Complaints Box** | Boss Curse | File a complaint: the boss gets a twin, everyone gets an extra coffin | EA |
| **Disciplinary Hearing** | Challenge Shrine | Summons an elite pack; reward coffin | EA |
| **Lost-and-Found Bell** | Shrine of Succ | Pulls all XP and **dropped souls** on the map to the ringer | EA |
| **Funeral Urns** (+ rare **Coin Urns**) | Pots / silver pots | Souls, heals, XP; Coin Urns give soul coins (meta currency) | EA |
| **Tax Dodgers** | Gold/blue skeletons, tumbleweeds | Fleeing skeletons carrying soul sacks; catch them for a burst of souls or a level-up | PL |
| **Water Cooler** | none | Team heal station; heals faster with more reapers chatting around it (co-op hub moment) | PL |
| **Notice Board** | none | Optional mid-night Memo bounty (e.g. "kill the elite within 60s", "bank 50 in one trip") for bonus soul coins. Megabonk has no dynamic objectives | PL |
| **Grave Springs / rails / lifts** | tornadoes | Movement interactables per map (see Movement) | EA (jump pads exist) |
| **Jukebox at the Wake** | Boombox | Find all 3 parts in a run to unlock music choice | PL |
| **Secret statues** | Bush / Bandit statues | Summon a secret boss that unlocks a reaper | EA (2), PL (5) |

---

## 9. Bosses (Megabonk ~15-16 named -> EA 7 + 6 minibosses + 2 secret)

Keep the existing 7 (Bell Ringer, Gravekeeper, Sexton, Drowned King, Ringmaster, Abbot, Merchant of Hours) and add:

- **Minibosses (EA 6):** Strongman (Carnival), Ice Bloat (Monastery), Tunnel Brute (Mines), Gear Golem (Clocktower), Grave Brute Captain (Graveyard), Barnacle Bloat (Harbour). Each appears at about 10:00 and 11:00 in-game time. Megabonk spawns minibosses at fixed times too.
- **New map bosses (PL):** The Foreman of Pit Nine (EA if Mines ships), The Chimney King, Old Strawman, The Undersecretary of Delays.
- **Secret bosses (EA 2, PL 5):** **The Unpaid Intern's Ghost** (a statue in Death's Office reception, unlocks The Intern variant), **The Lost Dog** (unlocks Femur the Hound), **The Tax Collector** (appears if you hoard 500+ unbanked souls - punishes greed, drops a Legendary), **Death's Twin Brother** (PL, ultimate secret), **The Floor Supervisor** (PL, judges your quota grade).
- **Boss design rules learned from Megabonk complaints:** no surprise insta-kills; every lethal attack is telegraphed. Do not strip the player's weapons (Megabonk's final boss disables non-starter weapons, which players call frustrating). Use **co-op mechanics** instead: e.g. the Merchant has 3 hourglass pylons that must be held **at the same time** (1 player per pylon in co-op, a timed sequence solo).

---

## 10. Quests / unlocks ("Performance Reviews") (Megabonk 240 -> EA 120, PL 250)

- Every quest is visible in an in-game **Performance Review** board (Megabonk players depend on wikis).
- Quest completion unlocks an item; **soul coins** buy it (no stat grind, matching pillar 4).
- **Categories:** Weapon mastery (kills with X, evolve X), Reaper mastery (clear Grade 2 with each reaper), Map (clear grades, find secrets), Quota (meet quota with 0 hits; bank 200 in one trip; bank in Final Call 10 times), Co-op (revive 10 teammates; activate every Bond; Duet Evolution), Greed (carry 300 souls at once; never bank until Final Call and still meet quota), Weird (win a night with only Turrets; let a Wisp steal 100 souls then kill it).
- **Achievements:** EA 60, PL 120. **Hats:** 20 at EA (KayKit props make this cheap: pumpkin head, candle crown, coffin lid, office tie, "World's Okayest Reaper" mug hat).
- **Office upgrades** (meta, cosmetic): Death's Office fills with trophies, boss contract pages on the wall, a fish tank of ghost fish. This is visible progress without stat creep.

---

## 11. Difficulty (Megabonk: tiers + challenges, no easy mode)

- **Quota Grades 1-3** per map (unlocked in order), the same shape as Megabonk tiers.
- **Grievances** (EA 10, PL 15): stackable modifiers, each adds +x% soul coins. Examples: Short-Staffed (quota +25%), Budget Cuts (no Death's Office between nights), Health and Safety Breach (no Pardons), Fog of Bureaucracy (Blind), Union Strike (no relics), Tight Deadline (nights 20% shorter), Wisp Infestation, Brute Force (enemies 1.5x), Early Closing (can't bank in Final Call), Flipped Org Chart (Megabonk's Weird Controls; joke only).
- **Union Rules (assist, EA):** slower horde, bigger carry cap, auto-bank radius, 1 extra Pardon. Achievements still count, with a badge. This answers the "no easy mode" complaint.
- **Endless:** quota grows 1.35x per night (already in Tuning). Leaderboards for solo / duo / squad.
- **Daily Roster:** seeded daily run, fixed crew loadout, co-op leaderboard.

---

## 12. Movement (Megabonk: jump, extra jumps, slide, bhop, fall damage, no dash)

Soul Quota already has **jump, dash, plateaus with ramps, and jump pads**. Plan:

- **EA:** keep **dash as the dodge** (Megabonk has none - guides say "save a jump to dodge"). Add **slide on slopes and ice** (speed builds downhill). Add **+1 jump** via relics/Keepsakes. **Gentle fall damage** (done Nov 7: big drops only, capped, never downs; slide held on landing = a safe roll; avoids Megabonk's wall-hug frustration). Add **ledge-grab assist** onto plateau rims. Add **grind rails** on Carnival and Harbour (hold jump to hop on; carry souls fast).
- **Signature mechanic - Soul Weight (EA):** the more souls you carry, the slower you move and the lower you jump. Banking makes you light and fast again. This ties movement to the greed pillar. Megabonk's movement has no link to its objective.
- **PL:** reaper-specific movement like Megabonk's best characters: Chimney Sweep climbs walls, Sir Neckless rides, Femur the Hound speeds up when hauling. Add a **"soul toss"** (throw part of your stack to a teammate, or into the lantern from close range - a co-op skill shot).
- Keep bunny-hop-style tech **optional**. Never require precise platforming over hazards.

---

## 13. Co-op features Megabonk does not have (the main selling point)

**EA (must have):**
1. 1-4 online co-op with shared XP and team level-up choices (exists).
2. Carry a downed teammate's skull to the lantern to revive them (exists).
3. **Bonds** (EA 12): linked relics give a team buff (8 exist, add 4).
4. **Role-based reapers** (Healer, Hauler, Banker, Defender, Scout).
5. **Co-op interactables**: Double-Locked Coffin, Signing Desk charging faster with more players, Water Cooler.
6. **Co-op Keepsakes** (Tea Party Set, Last Will, Matching Tattoos, Signed Resignation).
7. **Soul Leash** tether weapon.
8. **Ping and quick-chat** ("Coffin here", "Souls dropped!", "Banking!").
9. **Drop-in / drop-out** between nights in Death's Office.
10. **End-of-run awards** ("Employee of the Month", "Most Souls Dropped", "Biggest Haul") - shareable, clip-friendly.

**PL:** Duet Evolutions, soul toss, Pallbearers mobile bank, co-op leaderboards and Daily Roster, proximity voice (stretch), team shop vote (one shared item per night), co-op-only Grievances ("Mandatory Team Building": bonds doubled, solo banking disabled).

---

## 14. IP-risk flags

Generic death folklore (Grim Reaper, Charon, banshee, plague doctor, Headless Horseman - public domain since 1820) is safe. Avoid copying Megabonk's habit of pop-culture references (Za Warudo = JoJo; Bark Vader = Star Wars).

| Name (current or proposed) | Risk | Recommendation |
|---|---|---|
| **Mort** the Courier | **Medium**: *Mort* (Terry Pratchett) is famously Death's apprentice; Death + apprentice named Mort reads as Discworld | **Done:** shown as "Swift the Hauler" (internal Id `Mort` kept, never shown). See `IP_Audit.md` |
| **Harvest Moon** (Scythe evolution) | **Medium**: a game franchise trademark (Natsume) | Rename "Reaping Moon" |
| **Soul Storm** (evolution) | Low: also a Dawn of War expansion title (*Soulstorm*) | Optional rename "Soul Squall" |
| **Gravewarden** | Low: close to Elden Ring's "Grave Warden Duelist" | **Done:** shown as "The Gravekeeper" (Id kept) |
| **Soul Reaver** | High (Legacy of Kain) | Never use |
| **Five Nights...**, **Lethal**-anything, **Hoover** | Trademarks | Avoid (hence "Lost-and-Found Bell", not "Hoover") |
| **Bonk** | Not a legal risk, but reads as a copy of Megabonk | Avoid in names |
| KayKit art | CC0, no legal risk; widely used, so **distinctiveness** risk | Recolour, add props, add unique silhouettes for reapers |
| Art style | Note: `GameDesign.md` says claymation; the brief says goofy low-poly KayKit | Align the GDD with the actual art direction |

Do a trademark search (USPTO / EUIPO / UKIPO) on "Soul Quota" itself before the Steam page goes up.

---

## 15. Early Access vs post-launch summary

**Early Access launch (must ship):**
- 10 reapers; 24 weapons + 24 evolutions; 16-22 tomes; 40 items (23 now); 12 Bonds.
- 6 maps (Graveyard, Harbour, Carnival, Monastery, **Ossuary Mines**, Clocktower) x 3 Quota Grades.
- 7 bosses + 6 minibosses + 2 secret bosses; 16 enemy types.
- 12 interactable types (incl. coffins bought with carried souls, Photocopier, Double-Locked Coffin).
- 120 Performance Reviews, 60 achievements, 20 hats.
- Quota Grades, 10 Grievances, Union Rules assist, Endless mode, Daily Roster.
- Soul Weight, slide, rails on 2 maps, gentle fall damage (never downs).
- In-game Codex (every number visible), pings, end-of-shift awards.
- Public roadmap with a dated first update (Megabonk's 9-month silence is a top complaint).

**Post-launch (3 free updates, ~every 8-10 weeks):**
- Update 1 "Rooftops Update": Gaslight Rooftops, Chimney Sweep + Pallbearers, +4 weapons, +15 Keepsakes, Duet Evolutions.
- Update 2 "Harvest": Hollow Acre, Sir Neckless + The Actuary, +4 weapons, +15 Keepsakes, Notice Board bounties.
- Update 3 "The Waiting Room": endless office map, Madame Ectoplasm + Old Tickwell, +4 weapons, +20 Keepsakes, co-op leaderboards, Water Cooler, Tax Dodgers.

---

## 16. Story recommendation: light framing (evidence in MegabonkAnalysis.md section 12)

- None of the top survivors-likes sold on story: Vampire Survivors' developer says it "has no plot", Brotato (10M+) has none, Megabonk has none.
- Hits with a strong **premise** and a light **meta wrapper** do well: Ball x Pit's pit and village, Deep Rock Galactic Survivor's world.
- **Do:** keep the current setup - Death's memos between nights, a one-line boss intro, contract pages as collectible lore, and the office filling up with trophies. Make it all skippable and short (1-3 lines). Use the premise (temp reapers on zero-hour contracts) in the Steam capsule and trailer; that is the marketing hook.
- **Don't:** cutscenes, dialogue trees or a campaign mode at EA.
- **Maybe (PL):** "Special Assignments" - short optional story challenges with fixed loadouts, like Vampire Survivors' Adventures.
