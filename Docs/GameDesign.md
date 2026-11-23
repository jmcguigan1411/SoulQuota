# Soul Quota - Game Design Document v0.1

> **Numbers:** Docs/Balance.md section 11 (remake balance pass, 2026-11-09) is the source of truth for all balance numbers (Hard mode multipliers, Death's Office ranks, augment tiers, pacing). Figures quoted below may be older.


## Pitch
Death is understaffed. You and up to 3 friends are his temp reaper crew. Pick a mission, cut through a horde
of escaped souls, close the objectives on the map's timeline, then ring the Appointment Bell and put down
the legend who refused to die. Bank your gold for Death's Office and go again, harder.

- Genre: co-op top-down horde mission game (survivors-like; mission format inspired by LoL Swarm, mechanics only, no Riot names or art)
- Players: 1-4 online co-op, fully playable solo
- Price target: $7.99, Steam Early Access then 1.0
- Look: claymation 3D, stop-motion stepped animation, fixed top-down camera (since Nov 9)
- Engine: Unreal Engine 5.8, C++, data-driven CSVs

## Design pillars
1. Greed vs safety - carrying more pays more but risks more.
2. Chaos worth clipping - every system should create a moment people record.
3. Strong alone, stronger together - solo works; co-op roles add depth.
4. Respect the player's time - no grind walls, unlocks add variety not raw power.

## Market evidence (Nov 2026 research)
- Co-op games: median ~38k units vs ~5k for non co-op (VG Insights, 2023 cohort).
- Co-op games grossed ~$8.2B on Steam in 2025 (Alinea).
- The Spell Brigade (top-down 3D co-op survivors-like) sold 1M+ in Early Access.
- Megabonk (3D survivors-like) is single-player; players asked for multiplayer.
- Top 2025/2026 earners are overwhelmingly 3D; 2D platformers have the lowest hit rate.
- Quota-before-deadline proven by Lethal Company (10M+ est.) and Gamble With Your Friends (2M+).
- Risk: bullet heaven is "no longer a quick-win genre" - quality bar is high.

## Story
The Merchant of Hours sells eternal-life contracts. Everyone who signs refuses to die, and the
afterlife is overflowing. Death (exhausted, sarcastic manager) hires temp reapers on zero-hour
contracts. Each boss is a contract holder; defeating them drops their contract page (lore).
Collecting them leads to the final boss: the Merchant himself, who can stop the midnight clock.

Delivery: Death's memos on the victory screen, boss intro lines, contract pages (one per mission boss). No cutscenes.

## Run structure: missions (Nov 9 2026 rebuild, ~15-20 min per run)
A run is ONE **mission**: a single stage on one map, with a timeline of objectives, two minibosses and a final boss.
Mechanics follow a co-op survivor "mission" format (mechanics only; every name, text and asset is our own).
Data: `Content/Data/Missions.csv` (one row per map), `Timeline.csv` (act swarms, Final Swarm tiers), `Augments.csv`, `MetaUpgrades.csv`.
Code: `SQMission.h/.cpp` (USQMissions data + menu API, FSQMissionDirector, ASQObjective), `SQAugments.h/.cpp`, `ASQGameMode`.

### The loop, start to finish
1. **Main menu: mission select** (UI: `USQMissions::GetMissionList`). Five maps, one per biome, in order of difficulty. Map N+1 unlocks by
   beating map N; each map's **Hard mode** unlocks by beating it. The host's choice is stored with `USQMissions::SetPendingMission`.
2. **Death's Office (main menu)**: permanent upgrades bought with banked gold (`MetaUpgrades.csv`, 14 lines) and reaper hires.
3. **Lobby**: everyone readies up (R). Host keys: **B** = next unlocked mission, **O** = Hard mode on/off (once beaten); the host can also
   click the lobby MISSION panel (name = next mission, NORMAL / HARD chip = Hard mode). Clauses as before.
4. **The stage** (Missions.csv Duration, 16-20 min at release) starts at the map's player starts (`ASQArena::GetPlayerStart`).
   It runs in **3 acts** (a third each). The act is the old "night" for the horde: enemy mix columns, the HP / damage step and gold
   growth carry over, and the HP ramp is eased across act changes (`StageHpRefRatio`), so there is no jump at an act boundary.
5. **Objectives** appear one at a time on a schedule (objective k at k / count of the stage; finishing early pulls the next one up to
   12% sooner, `ObjectiveEarlyFactor`). Each has a time limit; a failed objective gives no reward and adds a **Defiance** stack to the
   boss (+25% HP each, as the old missed-quota rule). Rewards: gold (every reaper), free chests (one each), an augment pick (everyone)
   or XP (60% of a level).
6. **Minibosses** at ~35% and ~68% of the stage (Missions.csv Minibosses / MinibossAt). The 1st and 2nd miniboss kills offer every
   reaper an **augment**; the last one drops a free chest each. Act swarms come from Timeline.csv.
7. **The Appointment Bell** rises at the map's boss spot once every objective is resolved (done or failed). Ring it (interact, hold
   `PortalRingSeconds`): the stage clock stops, carried souls are banked as gold, and the **mission boss** rises.
8. **Overtime = the FINAL SWARM**: if the stage clock runs out before the boss is summoned, unfinished objectives fail, the bell rises
   anyway and the Final Swarm escalates (Timeline.csv FinalSwarm tiers) until the boss is rung and beaten.
9. **Win** = boss beaten: every pickup is vacuumed, the boss purse is paid, the **victory screen** shows stats (time, level, objectives,
   minibosses, boss time, per-reaper kills / gold earned / gold banked / augments). **Lose** = every reaper down at once (solo: down =
   dead). Either way, gold is banked and the lobby returns after `EndScreenSeconds` (20 s).

### Objective types (FSQMissionDirector; parts are replicated ASQObjective actors)
| Type | What you do | Count in Missions.csv | Fails after |
|---|---|---|---|
| BankSouls | Bank N souls at the lantern (the old quota) | souls at 4 players (solo 50%) | 150 s |
| DestroySpawners | Break N nests. Each has HP, spawns enemies every 5 s; reapers next to it siege it and weapon hits on enemies near it chip it | nests | 150 s |
| DefendPoint | Stand in the zone: the meter fills (faster with more reapers), drains when empty; pressure waves push in | seconds to fill | 150 s |
| Escort | The hearse rolls along the map's escort road (`ASQArena::GetEscortPath`) only while a reaper is near it; ambushes | - | 200 s |
| CollectItems | Walk over N soul urns scattered over the map | urns | 120 s |
| SurviveWave | Survive N seconds of swarm bursts | seconds | (its own timer) |
| KillElite | Hunt N crowned elites spawned at a landmark (any crowned / elite kill counts) | elites | 120 s |
Spots come from `ASQArena::GetObjectiveSpots(Type, Count)`; missing spots fall back to clear random ground (`FindLanternSpot`).

### Missions (Missions.csv)
| # | Map (biome) | Length | Objectives | Boss | Need* | Unlock |
|---|---|---|---|---|---|---|
| 1 | The Overflowing Graveyard | 16 min | Bank 100, 3 nests, 6 urns, defend 40 s, 2 elites | The Sexton | 0.79 | start |
| 2 | The Drowned Harbour | 17 min | Escort, bank 140, survive 45 s, 4 nests, 8 urns | The Drowned King | 0.93 | map 1 |
| 3 | The Plague Carnival | 18 min | 3 nests, defend 45 s, 3 elites, escort, bank 180, survive 50 s | The Ringmaster | 1.12 | map 2 |
| 4 | The Frozen Monastery | 19 min | 8 urns, escort, 5 nests, bank 200, defend 50 s, 4 elites | The Abbot Who Would Not Kneel | 1.33 | map 3 |
| 5 | The Clocktower | 20 min | defend 50 s, 5 nests, escort, survive 60 s, bank 240, 4 elites | The Merchant of Hours | 1.56 | map 4 |
*Need = the difficulty bar in the balance model (1.0 = the old 3-night run's on-curve tuning). Hard mode: horde HP x1.5, damage x1.35,
spawns x1.2, boss HP x1.4, gold x1.5 (need x~1.6).

### Augments (Augments.csv, 37)
Rare, run-defining upgrades, separate from weapons / tomes / items. Offered 1 of 3 at the 1st and 2nd miniboss kill, on objectives
with an Augment reward and at team levels 10 and 20 (~5 per run). They use the level-up card path with the AUGMENT card style
(`ASQPlayerState::bAugmentOffer`; Offers hold the ids, OfferRarity the tier 0 Silver / 1 Gold / 2 Prism); they can be rerolled but not
skipped. Tier odds depend on the source (2nd miniboss = best odds). Effects run on the server through USQAugments hooks (kill, hit,
cast, stats, tick): stat packages, kill explosions (can chain), hits splitting into bolts, kill cooldown refunds, +1 weapon / tome slot,
orbiting gravestones, execute, overkill splash, lifesteal / gold on kill, elite hunter, last stand, cast novas / haste, hurt novas,
Death's Pardon, instant level-ups / rerolls / chests, team XP, and co-op only: damage / armor / regen auras and on-the-spot revives.

### Meta progression (Death's Office, MetaUpgrades.csv)
One meta currency: **gold**. Gold earned in a mission (every coin, before chest spending) is banked at the end: 12% on a win
(+ the mission's ClearGold, + FirstClearGold the first time, + HardFirstClearGold on the first Hard win), 10% x the share of the
stage survived (min half) on a loss, x the Contract Clauses multiplier. The old soul coins carry over 1:1 (same save field).
14 permanent lines with 1-10 ranks and escalating costs (209k gold in all): damage, max health, regen, move speed, pickup range,
attack speed, armor, luck, team XP, gold gain, Field Medic (revive a downed teammate by standing next to them: 8 s -> 4 s), rerolls,
free starting level-ups, Death's Pardon (a free revive every mission). Applied at spawn through the stat pipeline
(raw stats -> `SQ::FinalizeStats`). Reapers still unlock by quest or gold (Reapers.csv costs unchanged).
Pacing goals (balance model section 15): map 1 cleared on attempt 1-3, 3-4 upgrades and 2-3 reapers in hour 1, all maps ~6-7 h,
all Hard modes ~29 h, everything ~45 h.

### Souls (kept, re-purposed)
- Enemies still drop XP (shared) and souls (carried, cap 50 base, a hit drops 25%). Bank at the lantern by standing in its ring.
- Banked souls feed the **Bank Souls** objective and always turn into gold for the crew (`SoulGold` 0.25 per soul, shared purse).
  The souls you carry when the bell rings are banked as gold. The soul reapers (Keeper, Pallbearers, Collector...) are economy picks.

### What was kept, replaced, removed (Nov 9 rebuild)
- Kept: the horde and its per-night tuning (now per act), bosses with abilities / phases, minibosses, swarm waves, the Final Swarm
  (now overtime), the Appointment Bell (now the boss summon), chests + gold + escalating prices, shrines / pots, quests, clauses,
  Defiance (now = failed objectives), the solo / co-op down rules, souls and the lantern.
- Replaced: 3 nights + quota -> one mission stage with objectives (the quota is the Bank Souls objective); Death's Office between nights
  -> the permanent upgrade shop in the main menu; soul coins -> banked gold; contracts -> mission clears (contracts are still collected);
  biome / Endless pickers (B / O) -> mission / Hard mode pickers.
- Removed: the between-nights shop (Shop.csv retired), Final Call, Endless mode, the quota-close at the bell.

## Reapers (21 - Nov 2026 update; Megabonk has 21)

Every reaper has one **co-op role** and, since the Nov 9 kits pass, a full **kit**: a signature weapon (own slot, evolves with its paired
tome), a passive (one identity mechanic, no per-level scaling), an **E** ability (team level 3) and an **R** ability (team level 6), both aimed
at the mouse cursor. The kits live in `Content/Data/Kits.csv` and `Docs/Kits.md` (runtime: `USQAbilityComponent`, SQAbility.h/.cpp).
The old per-level passives (`PassiveBase + PassivePerLevel x (TeamLevel - 1)`) are gone. The table below is kept for the roster, roles,
hire costs and the legacy passive names; **Docs/Kits.md is the source of truth for weapons, passives and abilities.**
Cosmetics per reaper: Model + Weapon + Accessory (AccessorySlot Hand/Head, AccessorySize cm) + ArtScale + idle/walk/run style clips + gait profile + ability animation + Color tint (see "Reaper movement personality" below).

Legacy table (pre-Nov 9 passives and unlock quests; see Kits.md and Quests.csv for the current ones):

| # | Reaper (Id) | Role | Start weapon | Passive (Id): text | Base / per level | Unlock cost | Unlock quest (planned) |
|---|---|---|---|---|---|---|---|
| 1 | Grim (Grim) | All-rounder | Scythe Sweep | TeamDamage: +10% damage per teammate within 900 (+1%/level). Death works better in a team. | 0.1 / 0.01 | 0 | Starter reaper |
| 2 | Swift the Hauler (Mort) | Hauler | Thrown Sickles | Hauler: +50% soul capacity (+3%/level) and a 1.6s dash. Travels light; hauls heavy. | 0.5 / 0.03 | 0 | Starter reaper |
| 3 | The Wailing Widow (Widow) | Control | Banshee Wail | SlowSouls: Slowed enemies drop +1 soul for the whole crew (+5% chance of 1 more per level). | 1 / 0.05 | 0 | Starter reaper |
| 4 | Bonesmith (Bonesmith) | Defender | Bone Turret | Builder: +1 turret per turret weapon (+1 more every 10 levels). Some assembly required. | 1 / 0.1 | 0 | Starter reaper |
| 5 | The Lantern Keeper (Keeper) | Banker | Lantern Flare | Banker: Banks 3x faster and +10% banked souls (+1%/level). Never loses a receipt. | 0.1 / 0.01 | 300 | Bank 2,000 souls in total |
| 6 | The Hollow Monk (Monk) | Striker | Prayer Beads | DashStrike: Dash is an attack: 30 damage along its path (+3/level). Vow of violence. | 30 / 3 | 500 | Dash 500 times |
| 7 | The Intern (Intern) | Wildcard | Random unclaimed (Frost Ring / Spirit Totem / Ectoplasm Puddle / Gargoyle Perch) | OnTheJob: Starts with a random unclaimed weapon. Every team level: a random stat bump (+5% bigger per level). Unpaid, keen. | 1 / 0.05 | 0 | Complete 30 Performance Reviews |
| 8 | Femur the Hound (Femur) | Fetcher | Gravequake | Fetch: Souls within 900 fly to him (+30/level); 25% faster while carrying a skull. Who's a good reaper? | 900 / 30 | 0 | Find the dog collar buried in the Graveyard |
| 9 | Digby Sixfeet (Digby) | Defender | Shovel Smack | Bulwark: Reapers within 600 (him too) take 12% less damage (+0.5%/level). Built like a headstone. | 0.12 / 0.005 | 100 | Take 10,000 damage in total |
| 10 | The Debt Collector (Collector) | Greed | Overdue Notices | Collections: +1% damage per soul carried (+0.03%/level). Interest compounds; so does panic. | 0.01 / 0.0003 | 200 | Carry 150 souls at once |
| 11 | Doctor Beak (Beak) | Healer | Plague Cloud | Triage: Every 3s heals the most hurt reaper within 900 for 10 HP (+1/level); overheal becomes a shield. No appointment needed. | 10 / 1 | 350 | Revive 25 teammates |
| 12 | Tallow (Tallow) | Striker | Soul Bolt | Scorch: Dashing leaves a burning wax trail for 3s: 20 damage/s (+3/level). Burns at both ends. | 20 / 3 | 400 | Kill 5,000 enemies |
| 13 | The Graveyard Lookout (Lookout) | Scout | Candle Beam | Lookout: Every 4s marks the toughest enemy within 1500: +25% team damage to it (+2%/level) and +2 souls when it drops. | 0.25 / 0.02 | 500 | Kill 50 elites |
| 14 | Vexa the Veiled (Vexa) | Berserker | Wreath of Thorns | Spite: Up to +60% damage as her HP drops (+3%/level). Do not RSVP. | 0.6 / 0.03 | 600 | Win a night while below 10% HP |
| 15 | The Actuary (Actuary) | Economy | Rubber Stamp | Audit: Kills have a 15% chance of an extra soul (+1%/level). Every soul accounted for. | 0.15 / 0.01 | 700 | Meet quota every night of a run with nobody downed |
| 16 | The Chimney Sweep (Sweep) | Mobility | Crow Swarm | Rooftops: +2 mid-air jumps (+1 every 10 levels) and glides down after jumping. Rooftops are a state of mind. | 2 / 0.1 | 800 | Stand on top of 20 plateaus |
| 17 | Sister Solace (Solace) | Reviver | Funeral Hymn | LastRites: Stand by a downed reaper for 4s (-0.1s/level, min 1.5s) to revive them on the spot. House calls only. | 4 / -0.1 | 900 | Carry 10 skulls back to the lantern |
| 18 | The Pallbearers (Pallbearers) | Mobile bank | Coffin Nails | MobileBank: Every 2s, reapers within 350 bank 20% of their souls into the coffin (+1%/level). Slow walkers, fast paperwork. | 0.2 / 0.01 | 1000 | Bank during Final Call 50 times |
| 19 | Madame Ectoplasm (Ectoplasm) | Summoner | Seance Chain | Seance: Kills have a 3% chance (+0.3%/level) to raise a ghost turret for 8s, max 3; elites always do. The spirits are willing. | 0.03 / 0.003 | 1100 | Defeat 3 bosses with 0 Defiance stacks |
| 20 | Sir Neckless (Neckless) | Speed | Tombstone Drop | Stampede: At full gallop tramples enemies within 150 for 14 damage every 0.25s (+2/level). Never looks where he's going. | 14 / 2 | 1250 | Win a run in under 20 minutes |
| 21 | Old Tickwell (Tickwell) | Time | Grave Bells | Stopwatch: Every 15s stops time within 700 for 1.5s (+0.05s/level). Punctual to the point of rudeness. | 1.5 / 0.05 | 1500 | Survive 10 nights in Endless |

(Legacy) What the passive number meant: TeamDamage = damage per teammate; Hauler = soul capacity bonus fraction; SlowSouls = extra souls per slowed kill (fraction = chance of one more);
Builder = extra turrets (floored); Banker = banked bonus fraction (plus fixed 3x speed); DashStrike = dash damage; Triage = heal HP; OnTheJob = stat bump size multiplier;
Fetch = soul pull range; Lookout = marked enemy damage bonus; MobileBank = fraction of stack banked; Audit = extra-soul chance; Seance = ghost chance per kill;
Rooftops = extra jumps (floored); Stampede = trample damage; Stopwatch = freeze seconds; Bulwark = damage cut; Collections = damage per soul carried;
Spite = damage bonus at 0 HP; Scorch = trail damage per second; LastRites = field revive seconds (min 1.5).
All 24 weapons are a start weapon: 20 directly, the other 4 through The Intern's random pick.

### Reaper movement personality (gait + style clips, Nov 2026)

Every reaper walks differently. Two layers, both per reaper in Reapers.csv:
1. **Style clips**: `IdleAnim` / `WalkAnim` / `RunAnim` are `|`-separated fallback lists, first clip that exists on the model wins
   (e.g. `SQ_Walk_Swagger|Walking_C`). The `SQ_*` clips are authored in Blender on the shared KayKit rig; until they are imported
   the KayKit clip after the `|` plays, so nothing ever T-poses. Empty = the model's default.
2. **Gait profile** (procedural, `USQAnimInstance`, applied in component space on top of whatever clip plays; faded with speed;
   switched off under full-body actions, downed, victory and defeat). Neutral = 0, except ArmSwing and Stride = 1.

| Column | Unit | What it does |
|---|---|---|
| GaitBounce | cm | Extra hop of the hips between footfalls (x walk/run blend) |
| GaitSway | deg | Side-to-side hip roll once per stride (spine counter-rolls half); a slow idle sway at 25% |
| GaitHunch | deg | Forward bend of spine (60%) + chest (40%), always on. Negative = chest out (proud) |
| GaitLean | deg | Extra forward spine lean, faded in with running only |
| GaitArmSwing | x | Upper-arm swing around where the arms hang (average of two opposite phases of the clip): 0 still, 1 clip, 2 exaggerated |
| GaitStride | x | Walk/run cycle rate at the same ground speed: >1 short quick steps, <1 long strides |
| GaitHeadBob | deg | Head nod on every footfall |
| GaitLimp | 0..1 | Asymmetry: phase warp (one leg's step slow, the other quick) + hip drop and lurch on that leg's step |
| GaitHover | cm | The whole body floats with a gentle bob (ghosts); kept during actions |

| # | Reaper | Idle | Walk | Run | Bounce | Sway | Hunch | Lean | Arms | Stride | HeadBob | Limp | Hover | Reads as |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Grim | Idle | Walking_A | SQ_Run_Sprint | 0 | 0 | -3 | 6 | 1.0 | 1.0 | 0 | 0 | 0 | The heroic default: upright, clean sprint |
| 2 | Mort | Idle_B | Walking_C | SQ_Run_Sprint | 3 | 0 | 0 | 14 | 1.4 | 1.25 | 2 | 0 | 0 | Courier: quick light steps, pumps his arms, leans into the run |
| 3 | Widow | SQ_Idle_Float | Walking_B | SQ_Run_Float | 0 | 3 | 14 | 0 | 0.2 | 0.8 | 0 | 0 | 10 | Bowed in grief, drifting just off the ground |
| 4 | Bonesmith | SQ_Idle_Proud | Walking_D_Skeletons | SQ_Run_Scurry | 2 | 1 | 3 | 6 | 1.1 | 1.1 | 3 | 0 | 0 | Proud craftsman, scurries off to the next job |
| 5 | Keeper | Idle | SQ_Walk_Sneak | Running_A | 0 | 1 | 6 | 3 | 0.3 | 1.15 | 0 | 0 | 0 | Tiptoes so the lantern flame never gutters; arms steady |
| 6 | Monk | SQ_Idle_Hunched | Walking_A | SQ_Run_Sprint | 0 | 0 | 8 | 16 | 0.15 | 0.8 | 0 | 0 | 0 | Hands clasped, slow serene steps, explosive lean when running |
| 7 | Intern | SQ_Idle_Nervous | SQ_Walk_Prance | SQ_Run_Scurry | 4 | 0 | 4 | 8 | 1.6 | 1.35 | 4 | 0 | 0 | Over-keen: bouncy, flappy, too many steps |
| 8 | Femur | SQ_Idle_Nervous | SQ_Walk_Sneak | SQ_Run_Gallop | 9 | 0 | 18 | 10 | 1.2 | 1.2 | 6 | 0 | 0 | Hound: nose down, bounding gallop |
| 9 | Digby | 2H_Melee_Idle | SQ_Walk_Waddle | SQ_Run_Heavy | 0 | 4 | 5 | 2 | 0.7 | 0.8 | 2 | 0 | 0 | Headstone-heavy plod, rolls side to side |
| 10 | Collector | SQ_Idle_Menace | SQ_Walk_Swagger | Running_A | 1 | 7 | -4 | 3 | 1.3 | 0.85 | 1 | 0 | 0 | Chest out, hips rolling, long confident strides |
| 11 | Beak | SQ_Idle_Hunched | SQ_Walk_Sneak | SQ_Run_Scurry | 0 | 0 | 20 | 6 | 0.4 | 1.1 | 7 | 0 | 0 | Hunched plague doctor, beak pecking every step |
| 12 | Tallow | SQ_Idle_Nervous | Walking_A | SQ_Run_Sprint | 2 | 4 | -2 | 18 | 1.5 | 1.15 | 0 | 0 | 0 | Flickering candle: jittery idle, burns forward when running |
| 13 | Lookout | Idle_Combat | SQ_Walk_March | Running_A | 0 | 0 | -2 | 5 | 0.9 | 1.0 | 5 | 0 | 0 | Alert sentry march, head scanning |
| 14 | Vexa | SQ_Idle_Menace | SQ_Walk_Limp | SQ_Run_Heavy | 0 | 2 | 8 | 12 | 0.8 | 0.9 | 0 | 0.35 | 0 | Wounded berserker: menacing limp, charging run |
| 15 | Actuary | Idle | SQ_Walk_March | SQ_Run_Waddle | 0 | 0 | 2 | 2 | 0.1 | 1.4 | 0 | 0 | 0 | Stiff, arms pinned to the clipboard, prim quick steps |
| 16 | Sweep | Idle | SQ_Walk_Prance | Running_B | 7 | 2 | 0 | 8 | 1.8 | 1.1 | 3 | 0 | 0 | Springy rooftop dancer, big arm swings |
| 17 | Solace | SQ_Idle_Proud | SQ_Walk_Shuffle | SQ_Run_Float | 0 | 0 | -2 | 0 | 0.1 | 1.2 | 0 | 0 | 5 | Nun: tiny steps under the habit, glides, hands in prayer |
| 18 | Pallbearers | SQ_Idle_Hunched | SQ_Walk_March | SQ_Run_Heavy | 0 | 6 | 6 | 3 | 0.0 | 0.7 | 0 | 0 | 0 | Slow funeral march, coffin rocking, arms locked on it |
| 19 | Ectoplasm | SQ_Idle_Float | SQ_Run_Float | SQ_Run_Float | 0 | 5 | 0 | 0 | 1.6 | 0.7 | 0 | 0 | 25 | Medium in a trance: floats high, waves her arms |
| 20 | Neckless | SQ_Idle_Proud | SQ_Walk_March | SQ_Run_Gallop | 6 | 0 | -6 | 10 | 1.2 | 0.9 | 0 | 0 | 0 | Headless knight: chest out, parade march, galloping charge |
| 21 | Tickwell | SQ_Idle_Hunched | SQ_Walk_Shuffle | SQ_Run_Waddle | 0 | 3 | 16 | 0 | 0.6 | 1.5 | 3 | 0.15 | 0 | Old clockmaker: bent over, tick-tock quick shuffle |

KayKit fallbacks (after the `|` in the CSV) keep each reaper close to their old look until the SQ_ clips are imported.

### Ability animations (passive triggers)

When a passive **triggers**, the server calls `QueueAbilityAnim(Variant)` -> one unreliable `MulticastAbilityAnim(uint8 Variant)`
(throttled per reaper by Tuning `AbilityAnimMinInterval`, 1.5 s). Every machine plays `A_SQ_Ability_<PassiveId>` at its authored
speed (KayKit fallback otherwise), as an upper-body layer while moving and full body when standing still (or always, for in-place moments).
Ability actions are "priority": they can cut a weapon attack animation and attacks can't cut them early.
A ring + burst of sparks in the reaper's colour plays at the same moment unless the passive already draws its own effect at the reaper
(Variant 1 = big moment: always the bigger burst).

| Passive | Triggers on (server) | Fallback clips | Body |
|---|---|---|---|
| TeamDamage | Allies in range went up (checked every 0.5 s, during fights) | Cheer, Taunt | upper/full |
| Hauler | Stack reaches 50% / 100% of capacity (100% = big) | PickUp, Interact | upper/full |
| SlowSouls | Widow kill within `SlowSoulsAnimWindow` s of her weapons slowing enemies; every `SlowSoulsAnimInterval` s at most (*inferred*: the horde doesn't report the real bonus) | Spellcast_Raise, Taunt | upper/full |
| Builder | Bonesmith places a turret (big when it fills a Builder bonus slot) | Interact, Use_Item | upper/full |
| Banker | Banking starts at the lantern (once per `BankerAnimInterval` s) | Use_Item, Interact | upper/full |
| DashStrike / Scorch | The dash itself: played locally by the existing dash detection on every machine (no extra RPC), replacing Dodge_Forward | Dodge_Forward, kick / Spellcast_Shoot | full |
| OnTheJob | Intern's level-up stat bump | Cheer, Taunt | upper/full |
| Fetch | A pulled soul lands (once per `FetchAnimInterval` s) | PickUp, Interact | upper/full |
| Bulwark | Every fight start (big), then every `BulwarkAnimInterval` s while an ally stands in his aura | Block, Taunt | upper/full |
| Collections | Every `CollectionsAnimStep` souls carried (4th step+ = big) | Taunt, Interact | upper/full |
| Triage | A heal lands | Spellcast_Raise, Interact | upper/full |
| Lookout | A new mark | Spellcast_Shoot, Interact | upper/full |
| Spite | Health crosses below 50% / 25% (25% = big) | Taunt_Longer, Taunt | upper/full |
| Audit | Kill, at the same odds as the bonus soul (*stand-in*: plays as often as the bonus pays, not on the same kills) | Use_Item, Interact | upper/full |
| Rooftops | An extra (mid-air) jump (3rd jump = big) | Jump_Full_Short, Jump_Start | full |
| LastRites | A field revive starts | Spellcast_Long, Spellcast_Raise | full |
| MobileBank | A coffin pulse banks souls (20+ = big) | Interact, Use_Item | upper/full |
| Seance | A ghost turret rises (elite = big) | Spellcast_Summon, Spellcast_Raise | upper/full |
| Stampede | Gallop starts (re-arms below 60% of gallop speed) | Taunt, Cheer | upper/full |
| Stopwatch | Time stops | Spellcast_Raise, Spellcast_Long | upper/full |

## Combat and upgrades (items remaster, Nov 2026)
- Weapons fire automatically. Shared XP; on each team level-up every player picks 1 of 3 cards.
- **Picking (Nov 7):** solo (one human; bots don't count) freezes the world with the big cards, cursor, reroll and skip (max `PickPauseMaxSeconds`).
  Co-op (2+ humans) never freezes: level-ups bank per player and show as a bottom-centre strip. In a fight it starts as a pulsing
  "LEVEL UP (N)" pill; 1/2/3 or Tab expands it (or it opens itself when no enemy is within `PickStripEnemyRange` 600 cm). Then 1/2/3 pick,
  R or Q reroll, X skip, "+N" shows more banked picks, hold Left Alt for a cursor to click (camera and aim stay free otherwise).
  While the strip is open the picker gets a protective bubble with a gold shimmer: `PickBubbleSeconds` 1.5 per new card set,
  at most once per `PickBubbleCooldown` 3 s. A set left alone for `PickAutoSeconds` 20 takes its rarest card. SETTINGS "Auto-pick
  level-ups" (SQSave, default off) picks the rarest card at once everywhere. Bots auto-pick. The server stays authoritative (OfferSerial).
- **Weapons / tomes / items** (Nov 7, `Docs/ItemsAndTomes.md`): level-ups fill **4 weapon slots** (`WeaponSlots`, max level 8) and
  **4 tome slots** (`TomeSlots`; 16 tomes = the old relics, max 5). **Items** (23) come from chests / Lost Property only, take no slot
  and stack (`MaxStacks`). Once slots are full, cards only offer what you own; then filler cards (gold / team XP / heal).
  Stats are summed raw and capped once (`SQ::FinalizeStats`: attack speed, crit overflow, armor / evasion / luck curves).
- **Card rarity** (Common / Rare / Epic / Legendary, rolled per card, Luck raises the odds). The same item is stronger when its card is rarer:
  - Weapons: +1 level and +0 / +10% / +22% / +40% permanent power (damage x power; area +25% of the bonus; multi-shot weapons +1 count per 0.4 power). A new weapon at Epic/Legendary starts with the bonus.
  - Relics: the per-pick stat gain is multiplied by 1.0 / 1.4 / 1.9 / 2.6.
  - Evolutions are always Legendary cards and keep the power the base weapon earned.
- Every card shows exact numbers (`SQ::DescribeUpgradeGain`), e.g. "+18% damage, +1 projectile".
- **Evolutions are deterministic:** max-level weapon + its paired tome = evolved weapon (replaces it in its slot) (24 recipes, one per weapon). No luck involved beyond being offered.
- Behaviours: Arc, Projectile, Aura, Orbit, Nova, Drop (telegraphed), Turret, **Chain** (ghost-lightning jumps between enemies), **Beam** (hits everything on a line).
- Base combat stats: 5% crit chance, crits deal 150%. Crit is rolled per volley (drops and turret shots roll their own).
- Bonds (co-op): two reapers within `BondRange`, one holding relic A and the other relic B, both get the bond stat (12 bonds, `Bonds.csv`).

### Weapons (24) -> evolutions (24)
| Weapon | Behaviour | Tag | Lv1 | Evolves with | Evolution |
|---|---|---|---|---|---|
| Scythe Sweep | Arc | Melee | 14 dmg / 1.1s | Whetstone of Woe | Reaping Moon (Arc) |
| Thrown Sickles | Projectile | Ranged | 12 dmg / 0.9s | Split Soul | Ferryman's Sickles (Projectile) |
| Banshee Wail | Aura | Area | 4 dmg / 0.4s | Hourglass Shard | Banshee Choir (Aura) |
| Bone Turret | Turret | Summon | 10 dmg / 4s | Spare Parts Bin | Ossuary (Turret) |
| Grave Bells | Nova | Area | 30 dmg / 4s | Grave Dust | Requiem (Nova) |
| Crow Swarm | Orbit | Summon | 8 dmg / 0.25s | Moth Lure | Murder of Crows (Orbit) |
| Tombstone Drop | Drop | Area | 45 dmg / 3s | Extra Ribs | Mausoleum (Drop) |
| Frost Ring | Nova | Area | 10 dmg / 3s | Funeral Shoes | Permafrost (Nova) |
| Soul Bolt | Projectile | Ranged | 9 dmg / 0.6s | Monocle of Scrutiny | Soul Conduit (Chain) |
| Coffin Nails | Projectile | Ranged | 40 dmg / 2.2s | Thorned Shroud | Spiked Coffin Lid (Projectile) |
| Plague Cloud | Aura | Area | 3 dmg / 0.5s | Embalming Salts | Black Death (Aura) |
| Lantern Flare | Nova | Area | 16 dmg / 1.6s | Golden Key | Sunrise (Beam) |
| Prayer Beads | Orbit | Melee | 10 dmg / 0.3s | Feather of Charon | Rosary of Ages (Orbit) |
| Gravequake | Drop | Area | 70 dmg / 5s | Vampire Fang | Earthen Maw (Drop) |
| Spirit Totem | Turret | Summon | 7 dmg / 3s | Embalmer's Patience | Ancestor Shrine (Turret) |
| Funeral Hymn | Arc | Area | 9 dmg / 1.4s | Tea Urn of Condolence | Last Rites (Arc) |
| Seance Chain | Chain | Ranged | 9 dmg / 1.2s | Skeletal Rabbit's Foot | Grand Seance (Chain) |
| Candle Beam | Beam | Ranged | 16 dmg / 1.6s | Red Ink Pen | Thousand-Candle Vigil (Beam) |
| Overdue Notices | Projectile | Ranged | 5 dmg / 1s | Reaper's Ledger | Final Demand (Projectile) |
| Rubber Stamp | Drop | Area | 18 dmg / 1.4s | Bottomless Sack | Denied in Triplicate (Drop) |
| Shovel Smack | Arc | Melee | 26 dmg / 1.4s | Bone Plating | Six Feet Down (Arc) |
| Wreath of Thorns | Orbit | Melee | 6 dmg / 0.2s | Thorned Shroud | Briar Crown (Orbit) |
| Ectoplasm Puddle | Aura | Area | 9 dmg / 0.5s | Levitation Night Class | Poltergeist Flood (Aura) |
| Gargoyle Perch | Turret | Summon | 14 dmg / 6s | Spare Parts Bin | Gargoyle Roost (Turret) |

### Relics (22)
| Relic | Per pick (Common) | Max | Tag |
|---|---|---|---|
| Whetstone of Woe | Damage 0.08, CritDamage 0.05 | 5 | Offence |
| Hourglass Shard | Cooldown 0.06, Duration 0.04 | 5 | Offence |
| Grave Dust | Area 0.1 | 5 | Area |
| Bottomless Sack | CarryCap 12, MaxHealth 5 | 5 | Economy |
| Moth Lure | Magnet 70, SoulBonus 0.02 | 5 | Economy |
| Funeral Shoes | MoveSpeed 35, DashCooldown 0.08 | 5 | Mobility |
| Extra Ribs | MaxHealth 20, Regen 0.1 | 5 | Defence |
| Bone Plating | Armor 0.05, MaxHealth 5 | 5 | Defence |
| Split Soul | Projectiles 1 | 3 | Ranged |
| Embalming Salts | Regen 0.5 | 5 | Defence |
| Feather of Charon | DashCooldown 0.2, Evasion 0.04 | 3 | Mobility |
| Reaper's Ledger | SoulBonus 0.07 | 5 | Economy |
| Golden Key | BankSpeed 0.25, CarryCap 4 | 5 | Economy |
| Thorned Shroud | Thorns 10, Armor 0.02 | 5 | Defence |
| Vampire Fang | PickupHeal 0.3 | 5 | Defence |
| Skeletal Rabbit's Foot | Luck 0.25 | 5 | Economy |
| Monocle of Scrutiny | CritChance 0.06 | 5 | Offence |
| Red Ink Pen | CritDamage 0.25, CritChance 0.02 | 5 | Offence |
| Embalmer's Patience | Duration 0.15, Area 0.03 | 5 | Summon |
| Spare Parts Bin | TurretBonus 1, Duration 0.05 | 3 | Summon |
| Levitation Night Class | ExtraJumps 1, MoveSpeed 15 | 2 | Mobility |
| Tea Urn of Condolence | AllyRegen 0.6, Regen 0.2 | 5 | Co-op |

New stats (FSQStats): Luck, CritChance, CritDamage, Duration (turret life, slows, projectile flight), ExtraJumps, Evasion (cap 50%), AllyRegen (heals teammates within BondRange).
Keepsakes (ContentPlan section 6) are not in yet: they need the coffin / Pawnbroker systems first.

## Controls (top-down, Nov 9; TopDownMode 1, the default)
- Fixed top-down camera; WASD moves in camera space; the mouse cursor is always shown and aims; mouse wheel zooms.
- **E** = ability 1 (slot 2, team level 3), **R** = ability 2 / ultimate (slot 3, team level 6), both cast toward the cursor.
  In the lobby R is still ready-up; while solo level-up cards are up R rerolls (as Q).
- **Space** (or Shift / gamepad A) = dash toward the cursor or the move direction; **F** = interact (chests, the bell, skulls).
- Gamepad: left stick move, right stick aim (auto-aim at the nearest enemy when idle), LT / RT abilities.
- No jump, slide, bunny hop or fast-fall in this mode; walls (Biomes.csv MapLayout) block reapers, the boss and the horde.

## Movement (Nov 7, Megabonk-style; TopDownMode 0 only since Nov 9; client-predicted in USQMovementComponent)
- **Slide:** hold C / Left Ctrl / LT on the ground while moving faster than `SlideEnterSpeed`: a speed burst (`SlideBoost`, once per
  `SlideBoostCooldown`), a lower capsule, slow steering; speed builds downhill and bleeds uphill. Ends below `SlideExitSpeed`.
  A slide shoves small ground enemies aside (`SlideKnock*`). (The Slippery Customer review was removed with sliding.)
- **Slide-jump:** jumping out of a slide keeps its speed.
- **Bunny hop:** jump within `HopWindow` of landing to keep the landing speed (x`HopSpeedGain` per hop, up to `HopMaxSpeed`).
- **Fast-fall:** hold slide in the air after the apex for `FastFallGravity` x gravity.
- **Fall damage:** falls over `FallDamageMinHeight` (from the top of the arc) cost `FallDamagePerMeter` of max HP per metre, at most
  `FallDamageMaxFraction`, and never down a reaper. Landing with slide held is a roll (no damage, the slide carries on); jump pads and
  the Chimney Sweep glide are always safe.

## Body blocking (Nov 7, Megabonk-style)
- Reapers can't walk through the horde. Every ground enemy is a solid circle; a reaper is a circle of its capsule radius (40;
  `BodyBlockUseCapsule`). Collision pass: an enemy's circle is Enemies.csv Radius blended to the width of its art (`CollisionArtFit` 1 =
  art); the same circle is used for blocking, crowd spacing, weapon hits and the slide shove. Melee lands at body + capsule + `Reach`.
  Walk into a crowd and you stop; get careless and you are surrounded and pinned. The horde crowds around you instead of overlapping you.
- Jump over: if your feet are higher than an enemy's art height x `BodyBlockHeightMult` 0.8, it doesn't block you.
- **The dash passes through** enemy bodies for `DashPassSeconds` 0.3 (no knockback): the way out when pinned. Bots dash out when 3+ bodies touch them.
- Flyers (Gravebat, Wraith) and wisps never block. Enemies still spawning in don't block.
- Netcode: the same clamp runs inside CharacterMovement (USQMovementComponent::CalcVelocity) on the server (sim positions) and on the
  owning client (its interpolated positions): inward velocity removed, overlap eased out with a capped velocity (never a teleport).
  While a remote reaper is near bodies or dashing, the server skips corrections and keeps the client's position (`BodyBlockGrace*`).

## Enemies
Zombies (Nov 7) are mesh-only models on the Minion rig (Zombie, ZombieBloat, ZombieRunner); about half the horde is zombies on every night.
Flyers: **Gravebat** (own rig: swarms in a zig-zag at altitude, telegraphs with its shadow + a red marker on the reaper, dives, climbs back)
and **Wraith** (Mage rig: legless ghost hovering low, drifts through the crowd, phases out and blinks closer every ~3.5 s). Flyers are hit by
every weapon (the sim is 2D), never body-block, ignore terrain and the tide. Later bosses add flyers to their summons (Bosses.csv SummonFlyer).

| Enemy | Behaviour | Quota impact |
|---|---|---|
| Shambler | Swarm fodder | Main soul source |
| Lurcher | Tougher chaser | More souls |
| Soul Wisp | Hunts whoever carries most souls, steals them, flees | Punishes greed |
| Gravedigger | Ignores players, walks to the lantern, steals banked souls | Forces defence |
| Bone Hound | Fast flanker | Knocks souls loose |
| Grave Brute | Elite | Soul burst |

## Bosses
| Night | Boss | Attacks |
|---|---|---|
| 1 | The Bell Ringer | Slam, projectile ring, summons Shamblers |
| 2 | The Gravekeeper | Bigger slam, denser ring, summons Gravediggers |
| 3 | The Drowned King | Biggest slam, 18-projectile ring, summons Lurchers |
| Final (planned) | The Merchant of Hours | Stops the clock, pulls souls out of the lantern |
All bosses enrage below 50% HP. Planned per-biome gimmicks: tides (Harbour), spotlights
(Carnival), frozen time zones (Monastery).

## Downed / revive (Nov 7 rules)
- Co-op (2+ reapers, humans or bots - a bot crew counts as co-op for testing, and bots carry skulls): a downed reaper becomes a
  rolling skull (slow). A teammate picks it up (E) and carries it to the lantern to revive at 50% HP. If every reaper is down at
  the same time, the run is over.
- Solo (one reaper): going down is death and the run ends at once (defeat screen). No free Pardon any more (`SoloStartPardons` 0).
- A Death's Pardon bought in the office still revives you once after 3 s (solo or co-op).

## Death's Office
Since the Nov 9 missions rebuild Death's Office is the main-menu meta shop (see "Meta progression" under Run structure). The old
between-nights shop (Mend Bones, Crash Course, Bigger Sacks, Death's Pardon) is retired; Death's Pardon is now a meta upgrade and an augment.

## Art direction
Claymation: chunky squishy forms, thumbprint-textured materials, characters animated on
stepped frames (~12fps) while camera/VFX run at full rate. Enemies splat into clay blobs.
Warm player colours vs muted horde; souls are always one bright gold.

## Audio (planned)
Spooky-swing soundtrack, squelch on kills, midnight bell countdown, proximity voice (stretch).

## Content target for Early Access (12-month plan)
6 reapers, 5 biomes, 5 bosses + final, 24 weapons (done), 16 tomes + 23 items (done), 24 evolutions (done), 12 bonds (done), 40 keepsakes,
5 missions with Hard modes (Endless retired Nov 9), daily seed.

## Maps: top-down mission arenas (Nov 9 2026)
Remake for a fixed top-down camera (pitch ~-55..-60, fixed yaw), WASD + mouse aim, no jumping, one 15-20 min mission per map.
The arena (ASQArena) is built from code: Biomes.csv Map* columns pick the layout, Tuning.csv Map* rows hold the numbers.
- **Size:** 112 m square (`ArenaHalfSize` 5600, appended override of the old 7000 row).
- **Ground:** flat with a 25 cm roll (`MapRoll`), dead level on every mission floor. Height = low tiers only: at most 2.5 m
  (`MapTierMaxHeight`), smoothstep ramps no steeper than 16 deg (`MapTierSlope`), a ring of rim stones marks each edge. The ground
  rises 60 cm into the edge walls. Harbour water is cut 80 cm down inside its colliders.
- **Zones:** hub (r 14 m, `MapHubRadius`) + an open kiting ring (no walls out to r 25 m, `MapKiteRing`). 4 landmarks (r 10 m, inner
  80% open, `MapLandmarkRadius`) and the boss arena (r 18 m, floor fully open, ringed by posts with wide gaps) evenly on a ring at
  0.62 x half size. Floors are painted for top-down: paved discs with pale edge rings, the boss floor its own colour.
- **Escort road:** 6 m wide (`MapRoadWidth`), ~80-90 m long, a point every 2.5 m. Starts 12 m in from the edge in the gap opposite the
  boss, drops into the kiting ring, sweeps round past the landmarks' inner edges, ends inside the boss arena. Painted with dark kerb
  lines; walls, props, hazards and shrines keep off it; lamps line it every ~18 m.
- **Spawner pads:** up to 8 (`MapSpawnerCount`), 10 m in from the edge, in the gaps between zones and in the corners; scorched discs
  ringed in the lamp hue; spread order (any first N are far apart).
- **Walls** (block reapers, the boss AND the horde): per-biome layout, clipped wherever they would cross the hub ring, a landmark core,
  the boss floor, the road, a pad, a tier ramp or the 3.5 m edge lane. Low walls (< 2 m) are instanced with a pale cap line; anything
  taller is its own component tagged `SQFadeable`.
  - *GraveRows* (Graveyard): a ring of grave rows (plinth + headstones) just outside the kiting ring (26 deg rows, 9 deg gaps =
    ~4.6 m chokepoints), two more rows across every open sector further out, iron fences round each landmark's outer side, crypts.
  - *Docks* (Harbour): a 9 m water band along one side + two canals reaching in (water is a wall; the road crosses on dry land),
    crate-stack walls on the harbour grid (some L-shaped), low bollard rings round the landmarks.
  - *Tents* (Carnival): stall avenues (two parallel 15 m rows, 6 m lane) out through every open sector, big round tents, barrier
    show rings round the landmarks, tall big-top poles round the boss.
  - *Cloisters* (Monastery): every landmark is a square cloister with an opening in the middle of each side and corner pillars; two
    small garden cloisters; pillar colonnades out through the sectors.
  - *Gears* (Clocktower): landmarks raised on 1.4 m gear platforms (tiers) ringed by gear teeth, radial "clock hand" walls broken
    in the middle, an outer gear ring with gaps.
- **Horde:** ASQArena::BuildHordeGrid buckets every wall + blocking prop + lamp footprint (4 m cells). Walkers steer round the wall
  ahead (`HordeWallLook` 2.6 m, picks the end that is the shorter trip) and are pushed back out of any overlap; flyers pass over.
- **Mission API** (ASQArena): `GetObjectiveSpots(Type, Count, Out)` for DestroySpawners / DefendPoint / Escort / CollectItems /
  BankSouls / SurviveWave / KillElite / Boss, `GetEscortPath(Points)`, `GetPlayerStart(Index)`. Deterministic per biome seed,
  validated (flat, dry, clear of walls / props / hazards / ice).
- **Removed for top-down:** the skate park (slide lanes, bowls, quarter-pipe banks, gap jumps, pump waves), every bone park piece
  and their launch pops, the jump pads, the 5.5 m plateaus, the moon disc. Park pieces stay in the project (ArtSource/Props) but
  are not placed: with no jumping or sliding their ramps are walk-over clutter that reads like a wall from above.
- **Hazards (top-down):** Lights Out -> roaming gloom rings (violet outline, fill in from the centre as the warning, drain health
  inside when dark; the lantern's ring is safe). Bell shockwave -> a glowing ground ring, reach outlined while it winds up, dodged by
  dash i-frames. Gust vents -> a harmless hop (onto a tier top, or onward). Fog banks -> a low outlined cloud layer that hides what
  is under it (no screen-wide fog change). Graves -> pale rim + dark pit, dash out.
- **Look:** high moon (-58 deg) for short crisp shadows, more sky fill, fog only past the far edge of the view, lighter vignette,
  brighter ground (`MapGroundBrightness`). Tall decoration (crypts, edge graves, lamps, backdrop, edge wall) lives at the edges and
  is `SQFadeable`.

## Biomes (v0.3)
| Biome | Gimmick | Boss | Unlock |
|---|---|---|---|
| Overflowing Graveyard | none | The Sexton | start |
| Drowned Harbour | Tide sweeps the map every 45s (slows players, drowns the horde) | The Drowned King | Graveyard contract |
| Plague Carnival | Roaming spotlights: double souls inside | The Ringmaster | Harbour contract |
| Frozen Monastery | Ice patches: players slide fast, horde crawls | The Abbot Who Would Not Kneel | Carnival contract |
| The Clocktower | Time stops every 30s for 4s | The Merchant of Hours (final) | all 4 contracts |
