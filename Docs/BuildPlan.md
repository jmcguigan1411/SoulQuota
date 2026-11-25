# Build plan (dependency order - build what everything else relies on first)

Rules: multiplayer from day 1 | riskiest tech first | one generic system + lots of data |
lock metrics early (Tuning.csv) | greybox before art | story & voice last.

## Phase 0 - Foundations  [DONE in v0.1]
- [x] UE 5.8 C++ project, config, .gitignore
- [x] Data pipeline: every number in Content/Data/*.csv, loaded at startup (no recompile)
- [x] Debug console commands (SQSpawn, SQSkip, SQGod, SQSouls, SQXP, SQReaper, SQReload)
- [x] Bot teammates (SQAddBot) to test co-op alone
- [ ] Git repo + LFS (run `git init` when ready)

## Phase 1 - Riskiest tech  [BUILT, NEEDS STRESS TEST]
- [x] Listen-server co-op (PIE multi-client, LAN via SQHost / SQJoin)
- [x] Horde manager: server-simulated data (no actor per enemy), spatial grid,
      quantised snapshots at 15 Hz, instanced-mesh rendering + client interpolation
- [ ] STRESS TEST: 4 players (or 1 + 3 bots), `SQSpawn 1000`, record `stat unit` and `stat net`
- [ ] Steam sessions / invites (OnlineSubsystemSteam already enabled; currently Null for LAN)

## Phase 2 - Feel and framework  [FIRST PASS]
- [x] Third-person high camera, movement, jump, dash
- [x] Data-driven weapon system (7 behaviours) + relic stat system + evolutions
- [ ] Movement feel tuning pass
- NOTE: custom lightweight ability system used instead of GAS - GAS is built around
  actors with ability components, which 1,000 data-only enemies are not. Revisit for bosses
  if their move sets grow complex.

## Phase 3 - Core loop  [FIRST PASS]
- [x] Run state machine: night clock -> midnight -> boss -> shop -> next night -> victory/defeat
- [x] Shared XP, per-player level-up picks
- [x] Souls: carry, drop on hit, pick up, bank at lantern, quota, final call, Defiance
- [x] 6 enemy types incl. Wisps (steal) and Gravediggers (rob the lantern)
- [x] 3 bosses (slam / ring / summon / enrage)
- [x] Downed skull + carry revive, Death's Pardon
- [x] Shop between nights, Death's memos, contract pages
- [ ] GO / NO-GO: playtest, Steam page, daily clips

## Phase 4 - Systems that make content cheap
- [ ] CommonUI HUD + menus (current HUD is a debug canvas)
- [ ] Main menu, lobby, reaper select
- [ ] Save system + meta progression
- [ ] Modular arena kit + biome gimmicks
- [x] Graveyard skate park map (2026-11-07, WRITTEN, NOT YET COMPILED OR PLAYTESTED): declutter (no blocking props inside the play space), 140 m arena, slide lanes with take-off tables, bowls, rim quarter-pipe banks, gap jumps, pump waves, bone park pieces (SM_Park_*, procedural stand-ins) with lip pop / vert air triggers, per-biome ParkStyle in Biomes.csv. GameDesign "Map: graveyard skate park"
- [ ] Import the art agent's SM_Park_* meshes + T_Park and playtest the park flow (lane -> kicker -> landing, bowls, gaps, pops online)
- [ ] Clay "beauty corner" art test (materials, stepped animation, Niagara splats)

## Phase 5 - Content production
- [ ] Bonds (co-op relic synergies)
- [ ] More weapons / relics / evolutions / enemies (mostly CSV rows + VFX)
- [ ] 5 biomes, 5 bosses + Merchant of Hours

## Phase 6 - Story, audio, polish, launch
- [ ] Voice lines, music, SFX
- [ ] Game-feel pass (hit-stop, shake, splats)
- [ ] Balance via bot runs, Steam achievements / cloud / Deck

## Known limits in v0.1
- Arena is flat (horde simulates in 2D at Z = 0). Enemies pass through gravestones.
- Placeholder art = engine shapes; effects are simple discs.
- Projectiles replicate as 15 Hz snapshots (slight visual lag on clients).
- Replicated arrays capped by net.MaxRepArraySize = 4096.

## v0.3 - Early Access feature set  [WRITTEN, NOT YET PLAYTESTED]
- [x] 6 reapers (2 unlockable: Lantern Keeper 300 pay, Hollow Monk 500 pay)
- [x] 16 weapons, 15 relics, 16 evolutions (all in Upgrades.csv)
- [x] 8 co-op Bonds (Bonds.csv) - relic A + an ally within 900 holding relic B
- [x] 10 enemies incl. Spitter (ranged), Bomber (explodes), Splitter (bursts into Shamblers)
- [x] 5 biomes (Biomes.csv) with gimmicks: none / Tide / Spotlights / Ice / Time-stop
- [x] 7 bosses: 2 lieutenants + 5 biome legends incl. final boss the Merchant of Hours
- [x] Contract progression: win a story shift -> contract -> unlocks next biome; all 4 -> Clocktower
- [x] Endless Overtime mode (O in lobby): quota x1.35 per night, bosses cycle, run until wipe
- [x] Local profile save: overtime pay, unlocked reapers, contracts, best endless night
- [x] Lobby: reaper select 1-6 (with hiring), host B = biome, O = mode, R = ready
- [x] Clay material (rim-lit), clay splats, hit shake/flash
- [x] Procedural SFX + music loop (Scripts/create_audio.py) and a client-side audio director
- [ ] Steam: switch OnlineSubsystem to Steam + achievements (needs a Steam app id)
- [x] v0.4 art pass: KayKit CC0 animated skeletons for reapers/enemies/bosses (leader-pose pool), Halloween props per biome, art lantern, tint material
- [x] v0.4 HUD overhaul: resolution scaling, Slate fonts, panels/cards/bars
- [ ] v0.5 terrain: plateaus, ramps, hills, basins, jump pads (horde follows ground height)
- [ ] v0.5 native C++ animation blending for reapers (locomotion blend, upper-body attacks, lean, squash)
- [ ] v0.5 enemy animation: rise from ground, attack anims, hit flash/knockback, soul bursts
- [ ] v0.5 reaper personality accessories (lantern, candles, pumpkin head...)
- [x] v0.5.2: own character (Reaper, Junior Grade), HUD remaster (TAB lobby), map remaster (paths, lamps, colours, backdrop), ready-up countdown + frozen lobby
- [x] v0.6: items remaster (24 weapons incl. Chain/Beam, 22 relics, 24 evolutions, 12 bonds, crit/luck/evasion...), rarity cards, freeze-to-pick with cursor, reroll/skip
- [x] v0.7: 21 reapers with scaling passives, roster UI, IP audit + licences
- [ ] Trademark search for the title (UK IPO / USPTO / EUIPO) + check itch.io 'SOUL QUOTA'
- [ ] Keepsakes (needs coffins + Pawnbroker)
- [ ] Funny reaper identities (each a shareable joke), more items/abilities than Megabonk
- [ ] Niagara VFX, recorded audio/voice
- [ ] Custom clay art to replace KayKit (optional, for a unique look)
- [ ] Revert test tuning before release (NightSeconds, UnlockAllReapers 0, UnlockAllBiomes 0, UnlockAllItems 0) - originals in Saved/Backups/2026-11-06_*_tuning. The economy pass was modelled at BalanceNightSeconds 360 (release); prices / HP scale down for test nights
- [ ] CommonUI menus (current UI is a canvas HUD)
- [ ] Balance pass using bot runs

Testing shortcuts (Tuning.csv): UnlockAllReapers=1, UnlockAllBiomes=1, NightSeconds=60.

## v0.9 — Bug + exploit pass (2026-11-07)
- Cheats are host-only and compiled out of Shipping; an end-of-run double-reward guard; ResetRun/EndPlay clear the pause and time dilation.
- Players are kept inside the arena (character check plus the arena safety net); walls reach TerrainMaxZ + 6000; KillZ lowered; jump pad fixes.
- Enemies spawn at least 1500 from players; caps on entities, projectiles and FX; soul-dupe fix; boss unstick and ground clamp.
- Props keep footprint gaps; the lantern spot is validated; HUD/menu mouse viewport offset fixed.
- Before release: add the new Tuning keys (SpawnMinPlayerDistance, SummonMinPlayerDistance, MaxProjectiles, WeaponFxPerSecond) to Tuning.csv; check MaxClientUnlockCost.

## Balance pass (2026-11-07)  [WRITTEN, NOT YET PLAYTESTED]
- [x] Balance model `Scripts/balance_model.py` (reads the CSVs; levels, hits-to-kill, horde clear, boss TTK, shop, soul coins). Write-up: `Docs/Balance.md`
- [x] Enemy HP: +10%/min inside a night, +35% per extra reaper; Enemies.csv Health x1.5, HealthPerNight x6.5 (2-5 hits late, 1-3 early)
- [x] Enemy damage: +30% per night, +3% per minute (EnemyDamagePerNight / EnemyDamagePerMinute)
- [x] Levels: XPGrowth 6 -> 15, XPPerPlayer 0.35 (co-op reapers level like solo)
- [x] Boss HP sized to TargetSeconds: 31k / 88k / 156k (Merchant 178k); `[BossTTK]` log gives the CSV value to use
- [x] Death's Office: escalating prices (ShopPricePerBuy / PerNight / PerPlayer), base costs doubled, 3-5 buys per visit
- [x] Soul coins: PayPerNight 60, KillsPerPay 40 (about 1 unlock per 1-2 runs)
- [x] BalanceNightSeconds 360: short test nights scale the HP step, boss HP and prices down to match
- [x] Tuning.csv now lists every key the code reads (reaper/FX, enemy/boss, v0.9 keys)
- [ ] Play 1 solo + 1 co-op run at NightSeconds 360 and compare `[BossTTK]`, shop souls and kills with the model; put the real kill/banking rates in the model's ASSUMPTIONS
- [ ] Check the quota at 6-min nights (model: 150 solo against ~950 souls banked looks easy)

## Gameplay v11 (2026-11-07)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
- [x] Body blocking: reapers can't run through the horde (jump over short enemies; the dash passes through; bots dash out). Tuning BodyBlock*, DashPassSeconds
- [x] Solo: going down = death + defeat screen (no free Pardon; SoloStartPardons 0). Co-op (humans or bots): skulls + revive, all down = run over
- [x] Zombies: Zombie / ZombieBloat / ZombieRunner models (Minion rig, takes found through the skeleton folder), ~half the horde; new Rotter + Fresh Arrival
- [x] Flyers: Gravebat (swoop with shadow + red marker telegraph) and Wraith (phase-blink ghost); bosses summon them (Bosses.csv SummonFlyer/Count)
- [x] Animation polish (code): eased blends, landing / take-off squash, turn-in-place twist, hit wobble, enemy spawn pop, juicier death tumbles, flyer falls
- [x] Co-op level-up strip: no freeze with 2+ humans, 1/2/3 picks, pill mid-fight, protective bubble (1.5 s per 3 s), auto-pick after 20 s, SETTINGS auto-pick toggle
- [ ] Import the art agent's Zombie / ZombieBloat / ZombieRunner / Gravebat / Wraith (missing art = primitive shapes, as always)
- [ ] Playtest body blocking online (2 PCs): rubber-banding near crowds, pinning feel, dash escape
- Megabonk gaps still open (ranked, from the v11 review; not started):
  - [x] 1. Movement verticality (2026-11-07, WRITTEN, NOT YET COMPILED OR PLAYTESTED): slide (hold C / Left Ctrl / LT: burst, lower capsule, builds speed downhill,
        slide pose + lean, dust, FOV kick), slide-jump keeps momentum, bunny hop (jump within HopWindow of landing keeps speed, +3%/hop to HopMaxSpeed),
        air control keys, fast-fall (slide in the air), gentle fall damage over 9 m (never downs; jump pads, Chimney Sweep glide and a slide-landing roll are safe),
        speed lines. Client-predicted in USQMovementComponent (FSQSavedMove, FLAG_Custom_0). Tuning: Slide*, Hop*, AirControl*, FastFallGravity, FallDamage*, SpeedFx*.
        Open: slide knockback on small enemies needs a horde knockback API (not added); playtest online for corrections at slide start/end.
  - [x] 2. Interactables + shrines on the map (2026-11-07, WRITTEN, NOT YET COMPILED OR PLAYTESTED): Content/Data/Shrines.csv + SQShrine / SQPot,
        scattered fresh every night by ASQArena (deterministic biome + night seed, some on plateau tops). Performance Review Shrine (charge; co-op charges
        faster; everyone in range picks 1 of 3 raises on pedestals), Employee of the Month (golden, Legendary raises), Lost Property Desk (1 of 3 relics),
        Complaints Bell (elite wave, survive 30 s: free chest + gold), Petty Cash Coffin (gold, +8% Difficulty), Collections Altar (map-wide pickup pull 10 s),
        Contract Extension Lectern (temp boss copy at 30% HP: free chest), Spare Ash Urns / Pumpkin Pots (gold / XP / heal), Silver Urn (soul coins).
        Raises live in USQBoostLedger (re-applied after RecomputeStats). No minimap yet: interactables are bAlwaysRelevant, ready for one.
  - [x] 3. In-run chest economy (gold, escalating chest prices, free chests from elites) instead of only the between-night office  [Nov 7 economy pass, WRITTEN NOT COMPILED: SQChest, gold coins, per-reaper prices, crowned elites, Skeleton Key relic; Balance.md section 6]
  - [x] 4. Boss portal you choose to start + a final swarm when time runs out (Megabonk's 10:00 swarm)  [Nov 7: SQPortal "Appointment Bell" from 10:30 PM, Final Swarm tiers in Timeline.csv]
  - [x] 5. Stage timeline beats: miniboss at ~3:00 and ~8:00, swarm waves, elite "crown" enemies with chest drops  [Nov 7: Timeline.csv minibosses at 30% / 70%, 1-2 swarms per night, crowned elites]
  - [x] 6. Map secrets and per-map hazards that change routing (2026-11-07, WRITTEN, NOT YET COMPILED OR PLAYTESTED): SQHazard (Biomes.csv Hazards /
        HazardCounts / Secrets, numbers in Tuning Hazard*). Graveyard: Lights Out (roaming will-o-lamps; the dark drains HP) + open graves. Harbour: fog
        banks + graves. Carnival: gust vents (throw you onto plateaus) + bell shockwaves. Monastery: bells + fog. Clocktower: bells + gusts.
        Secrets: Groundskeeper's Key (high ground) opens the Sealed Crypt (free chest); Buried Bonus (dig up a golden shrine).
  - [x] 7. Quests / challenge modifiers for meta goals (Megabonk: 240 quests, difficulty modifiers with silver multipliers)  [Nov 7: 81 quests (Quests.csv, menu QUESTS page, HUD toasts), 9 Contract Clauses (Modifiers.csv, lobby); Balance.md section 7]

## Economy pass (2026-11-07)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
- [x] Gold (per-reaper purse, shared coin split in co-op), chests with escalating per-reaper prices, free chests (crowned elites, miniboss, boss), Skeleton Key relic
- [x] Appointment Bell (boss portal) + Final Swarm; boss fight freezes the clock; boss kill vacuums pickups
- [x] Timeline.csv beats (minibosses with name bar, swarm waves), Difficulty (AddDifficulty)
- [x] Quests.csv (81), Modifiers.csv (9 Contract Clauses), QUESTS menu page, lobby clause panel, HUD gold / prompts / banners / toasts
- [x] Reaper hire costs retuned (sum 41,250), EconomyBossHealth / EconomyEnemyHealth for chest power
- [ ] Compile + first playtest: check chest counts per night, the bell flow (ring / Final Swarm / boss vacuum), quest toasts, clause toggles
- [ ] GameDesign.md still says "midnight: unbanked souls are lost; the boss rises" - now the bell closes the quota (owner of GameDesign.md to update)
- [ ] Hats / cosmetic quest rewards (no hat slot exists yet)
- [ ] World agent: call ASQPlayerState::CreditQuest(TEXT("ShrinesUsed")) / (TEXT("PotsBroken")); movement agent: CreditQuest(TEXT("SlideKills")) for slide kills

## Nov 7 batch: impostors, collision, tutorial, skate park, items/tomes  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
- [x] Far-enemy impostors: 4-frame flipbook ISMs of each enemy's own art past the skinned ring (SkinnedEnemyRadius 4200, pool 220), seamless hand-off (facing, lean, walk phase), far props, ImpostorShadows
- [x] Collision accuracy: enemy body radius = Enemies.csv Radius blended to the art width (CollisionArtFit), reaper = capsule radius, per-type Reach, telegraphs at the real hit distance, SQShowCollision debug view
- [x] Tutorial: Quick Start cards (lobby / HOW TO PLAY), one-time tips in a run (incl. ramp tip near park pieces), SETTINGS tips toggle + RESET TUTORIAL, SQTutorial console command
- [x] Skate-park map (see Phase 4): lanes, bowls, banks, gap jumps, pump waves, SM_Park_* pieces sized per ArtSource/Props/README_Park.md
- [x] Items/tomes rework: 4 weapon + 4 tome slots, 23 stacking chest items, 16 tomes, filler cards, SQ::FinalizeStats caps; HUD loadout WEAPONS n/4, TOMES n/4, ITEMS xN (Docs/ItemsAndTomes.md)
- [ ] Compile + one test session: import impostor / park FBX (Scripts/import_art.py), check [Collision] log radii vs CSV, park pops online, tutorial flow solo + co-op

## Missions rebuild (2026-11-09)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
Jack: play like a co-op "mission" survivors mode (mechanics only, all names / text / art our own): top-down, mission select, one stage
per map with objectives, minibosses and a boss, Hard modes, augments, permanent upgrades. Docs: GameDesign "Run structure: missions",
Balance.md section 10 (model sections 13-15).
- [x] Missions.csv: 5 maps (one per biome), 16-20 min stages in 3 acts, objective lists, mission boss, 2 minibosses, difficulty + Hard multipliers, unlocks, clear rewards
- [x] Objectives (SQMission: FSQMissionDirector + replicated ASQObjective parts): BankSouls (old quota), DestroySpawners, DefendPoint, Escort, CollectItems, SurviveWave, KillElite; rewards gold / chests / augment / XP; failed = Defiance; spots from ASQArena::GetObjectiveSpots / GetEscortPath
- [x] Stage flow: act smoothing of horde HP, objectives -> Appointment Bell -> boss = victory; overtime = Final Swarm; victory / defeat stats (ASQGameState::RunStats)
- [x] Augments.csv (37) + USQAugments hooks (OnKill / OnHit in the horde, ModifyStats + ExtraSlots in the character, NotifyCast for abilities, subsystem tick); offered on the level-up card path (bAugmentOffer)
- [x] MetaUpgrades.csv (14 lines, 84 ranks) + SQSave MetaRanks / MissionsCleared / HardCleared / PendingMission; USQMissions menu API (GetMissionList, GetMetaUpgradeList, BuyMetaUpgrade, SetPendingMission)
- [x] One meta currency (banked gold; soul coins carried over); Shop.csv retired; quests updated (16 new mission quests); lobby B / O = mission / Hard
- [ ] Compile (needs the map agent's ASQArena::GetObjectiveSpots / GetEscortPath / GetPlayerStart) and one solo + one co-op test at NightSeconds 360
- [x] UI agent: mission select + Death's Office pages in SQMenu, objective tracker / arrows, AUGMENT card style, victory stats screen, BeatKind 5/6/7 (new / complete / failed objective)
- [x] Ability agent: call USQAugments::NotifyCast(Owner, Slot) on a successful cast and apply USQAugments::GetKillCooldownRefund(Owner) in OnOwnerKill (ReduceCooldown)
- [ ] Map agent (optional): re-scatter shrines / pots per act (ASQArena::ServerSpawnInteractables is protected; today they are placed once per stage)
- [ ] Controller owner: ClientRunEnded (old soul coin pay) is no longer called - left in place unused (remove when convenient)
- [ ] Replace the win-model guesses (skill curve, objective durations) with playtest numbers; check act-boundary damage steps (+30%) feel
- [ ] Decide: Keeper hire cost 450 -> ~900 if hour 1 should give 2 reapers, not 3 (Reapers.csv)

## Top-down remake (2026-11-09)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
Jack: play like a top-down horde mission game (inspired by LoL Swarm, mechanics only, no Riot names or art). Skins and art kept.
- [x] Controls: fixed top-down camera, mouse aim API (GetAimPoint), E = slot 2 / R = slot 3 / F interact / Space dash, camera occlusion fade, TopDownMode gating (0 = old third-person movement)
- [x] Kits: USQAbilityComponent (passive + E + R + evolution, bot casting), Kits.csv, signature weapons in their own slot, horde ApplyPull / ApplyConeDamage, old per-level passives removed (Docs/Kits.md)
- [x] Missions: SQMission (FSQMissionDirector, ASQObjective), SQAugments, Missions / Augments / MetaUpgrades CSVs, SQGameMode rewrite, one meta currency (banked gold)
- [x] Maps: SQArena rebuilt for top-down (walls, zones, escort road, objective spots, hazards redone, skate park removed with stubs); horde, bots and the boss steer round walls
- [x] UI: SQHUD / SQMenu / SQMenuShowroom / SQUIStyle / SQTutorial rewritten (studio scene capture, portraits, kit panel, ability bar, objective tracker, mission and upgrade pages, clickable lobby mission panel)
- [ ] Compile + first top-down playtest

## Monster roster + random maps + signature animations (2026-11-09)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
Jack's asks: "They all look like skeletons... headless horseman, pumpkin man, zombie warrior, undead undertaker, ghost, witch, wizard,
serial butcher. Each with their own abilities and animations"; "dash in the direction they are moving"; "the map should always be
random... always works"; "the animations are all so basic".
- [x] Concept previews approved (Design canvas "Soul Quota - Monster Roster").
- [x] 21 new monster bodies on the Reaper rig (ArtSource/Characters/R_<Id>.fbx/.blend + palettes; builders in blender_work/roster4/mr_a/b/c.py), no skulls on any player.
- [x] 16 original weapons W_<Id>.fbx (sabre, greatsword, coffin, broom, hourglass staff, bat cane, bandage whip, bone club, shovel,
      lantern pole, pitchfork, defib paddle, bone boomerang, PAID stamp, marionette crossbar, cleaver); the Shade keeps the scythe;
      Ghost, Pumpkin Man, Gargoyle, Scarecrow fight without a held weapon.
- [x] 84 signature takes (Remaster 4, blender_work/sq_anims4.py): SQ_Idle/Walk/Run_<Id> for all 21 + SQ_Sig_<Id> attack for 20
      (five beats, on each AttackStyle's impact frame). Reapers.csv Idle/Walk/RunAnim + signature weapon AttackAnim point at them.
- [x] Kits re-themed (names, descriptions, glyphs); mechanics and numbers kept. Small E/R damage nudges: Tallow, Widow, Lookout -, Intern +.
- [x] Dash: moving = dash where you move; standing still = toward the cursor (SQPlayerController::DoDashInput).
- [x] Random map every run: replicated ASQArena::RunSeed, flood-fill ValidateLayout (starts, zones, pads, objectives, road,
      no sealed pockets), up to 8 rerolls, then the fixed biome layout. Tuning RandomMaps (1 on).
- [ ] Balance: Widow / Tallow / Lookout (strong) and Intern (weak) still outside the kit band in the model - driver is not E/R damage; investigate.
- [ ] Playtest every monster in game (weapon grip angles, signature attack timing).

## Entrances + themed effects (2026-11-10)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
Jack: "when characters are selecting them they all have an artistic animation suited to each of the characters names. Zombies and
skeletons come out of the ground, witches appear with a poof of the colour they wear... Each of the players attacks and abilities should
suit the characters as well. I approve on the styling."
- [x] 21 entrance takes SQ_Intro_<Id> + hero-pose SQ_Flourish_<Id> (blender_work/sq_anims5.py; Remaster 5).
- [x] Character select (ASQMenuShowroom): each monster arrives its own way - Rise (zombie, ghoul, mummy, bog hag, undertaker, Shade),
      Poof (witch purple, wizard clock rune, vampire bats, imp firework, scarecrow crows, reanimator lightning), Drop (werewolf leap,
      gargoyle swoop, butcher stomp, moth flutter, horseman vault, puppeteer on strings), Materialize (ghost), Grow (pumpkin), Jolt (patchwork).
      The lobby studio camera now also renders the effect particles.
- [x] Effect kit: 20 stylised meshes (ArtSource/FX/SM_FX_*.fbx, blender_work/fx/fx_meshes.py) as new ESQFxShape particle shapes.
- [x] SQFxTheme: per-monster palette + recipes, three beats (anticipation, layered impact, linger), for the entrance, the in-game rise
      flourish, the signature weapon (lands on the impact frame) and E / R casts.
- [ ] Playtest: effect sizes / counts in game, the entrance timings against the intro takes.

## Pickups, no leftovers, cursor + camera (2026-11-10)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
Jack: "Enemies should not leave anything behind just xp... xp green, gold yellow, crystals... when there are loads of crystals they should
turn white... gold version: coins, bundle of coins, coin purse, gold bars"; "no cursor visible unless the camera is free"; "couldn't change the camera".
- [x] No blood: the clay splat under every kill is off (Tuning DeathSplats 0).
- [x] XP crystal ladder (green shard -> pair -> cluster -> white prism) and gold ladder (coin -> stack -> purse -> gold bar), tier by
      value (Tuning XpTier1-3 / GoldTier1-3) sent in the pickup's net Kind byte; lots on the ground merge into bigger tiers (PickupMerge*).
- [x] Souls are soul-teal (no longer gold-coloured).
- [x] FIXED camera: no cursor in a fight; abilities and the dash auto-aim (nearest enemy / movement). FREE: cursor shown, mouse aims.
      Menus, cards, settings, shop, lobby always show the cursor. Switching the mode explains itself on screen.

## Map remaster: readability + interactable cues (2026-11-10)  [WRITTEN, NOT YET COMPILED OR PLAYTESTED]
- [x] USQInteractCue (new): shared "you can use this" look - glowing segmented ground ring + tinted pad, floating diamond, near label; dims/greys once used. Colour per kind: shrine cyan, chest gold, free chest bright gold, pot soft white, bell crimson, hazard red-orange, collect green.
- [x] Cues on pots ("SMASH"), chests ("CHEST [F]" / "FREE CHEST [F]"), the bell portal (waiting / "RING THE BELL [F]" / ringing %).
- [x] Spent shrines turn grey; gloom ring outlines go violet -> warning red-orange while filling / dark.
- [x] Arena: flagstone plazas (hub, landmarks, boss floor in its own shade), kerb stones along the road, glowing marker posts round each landmark (boss crimson), edge props grouped into silhouette clusters, deeper edge framing band, stronger bloom + vignette.
- [ ] Playtest: label range, cue brightness, flagstone size / missing rate, edge darkness (all Tuning: Cue*, MapFlagstone*, MapEdge*, MapKerbSpacing, MapZonePosts, MapPropClusterSize).
- [ ] Collect spots (SQMission) and shrines could adopt USQInteractCue later for full consistency.

## Map revamp (Nov 10 2026)
- [x] 224 m arena with skate districts, lanes, park pieces, district names/beacons
- [x] Minimap (fog of war) + full map on M, district banner, objective cues
- [ ] Playtest: district density, feature heights, minimap range (Tuning Map*/Minimap* keys)
- [ ] Gamepad binding for the full map

- [x] Menu readability pass + click-only character select (Nov 10). Playtest: check 1280x720 and 4K for overflow from the lifted small text.

## Map art pass (Nov 10 2026)
- Done in code: muted ground palette, worn paths, wall contact shade, skate concrete + coping, candle lanes, monuments, vignettes, fireflies, lamp flicker, grade retune, M_GroundV2.
- Still needed for a sellable look (real art / in-editor work): textured ground with normals, bespoke landmark set pieces, modelled ramps, foliage with wind, Niagara VFX, lighting tuned in the viewport, an environment artist pass or a licensed kit.

- 2026-11-12 (design thread, Docs/MarketAudit.md): runs shortened to the genre norm. Stages 960-1200 s -> 600-840 s, objectives 5-6 -> 3-5, boss health mults 0.57-1.18 -> 0.26-0.71 (model: 11-17 min runs, 150-225 s boss). Faster start: spawn 0.6 -> 1.2 /s, opening swarm at 4 s, tutorial hold 60 -> 20 s, solo countdown 1 s (StartDelaySolo), end screen 20 -> 8 s. Meta: first ranks 40% cost, growth 3.3, loss banks 20%. NightSeconds 90 -> 360 so test play = release length. Recommended, not done: auto-bank souls on map 1, RETRY button, solo lobby skip, 3-prompt tutorial, unlock screen.

## Nov 12 2026 - chase camera (camera thread)
- Done: third-person chase camera, auto / manual aim setting, overhead camera + aim indicators removed, tutorial / key hints / CONTROLS tab updated.
- To verify in play: mouse sensitivity feel, camera on steep banks and in bowls (terrain clamp), props hiding between camera and reaper, jump / slide with the horde, fog and far pop-in on the horizon (fog keys belong to the style pass), gamepad look rate.
- Later: rotating minimap option, proper downed spectate cycling, shop-phase camera look while the cursor is up.

- [x] Nov 12: Style pass (Midnight Fairground): sky + horizon, ink outlines, rim light, vivid palette, effect dials. Pending: tune Style* values from screenshots; cel-banded character shading, painterly ground, bespoke landmarks need an artist or licensed kit.

- 2026-11-12 flow pass: end screen has RETRY / PLAY AGAIN (ENTER) and CHANGE REAPER (C), host or solo only (ASQGameMode::RetryRun / EndToLobby, ASQGameState::bSkipLobby); START REAPING on the main menu is now SOLO and skips the lobby (travel option SQSolo, Tuning SoloSkipLobby); co-op hosting moved to PLAY CO-OP > HOST A LOBBY; the Training Shift is 3 prompts (move+look / weapons fire themselves, crystals level you up / the goal), horde holds only until prompt 1, 7 of 13 tips retired; QUESTS page renamed UNLOCKS with NEW badges (USQSaveGame::QuestsNew) and an unlock strip on the end screen. EndScreenSeconds 8 -> 30 (idle fallback only).

- 2026-11-12 (visual pass, from the first real screenshots): motion blur / film grain / lens flare / fringe off (DefaultEngine.ini); ground palette rationed (one hue per biome, StyleGroundVivid 0, MapDistrictTint 0.04, MapSaturation 1.06); new additive glow material M_SQGlow (setup run builds it); souls are additive teal wisps with 4 sizes, merge at SoulMergeAt, 2 per kill, dim teal on a full sack (no grey balls); ASQFx area impacts/telegraphs layered (shockwave, echo ring, flash, cracks, shards, motes; danger fill grows to impact); glowing spark shapes additive (StyleFxAdditive). Docs/ArtDirection.md has the revised rules.

## Lantern trail (Nov 12 2026)
- Done: lantern trail system (see Docs/LanternTrail.md) - FSQMissionDirector trail, ASQLantern beam / dormant + spent posts / footlights, sack growth, HUD row + minimap icons, balance model section 18b.
- Next: map remaster anchored on the lantern sites; bias objectives toward the lit district; play-test LanternQuota and boss TTK (extra chests shorten bosses).

## Nov 12 2026: three-map remaster (map thread)
- Done in code (not yet compiled or seen in Unreal): floor rule; macro ground shapes per map; tide by water level; clock hands; set-piece kit and placement; 3-map data cleanup; objectives near the lit lantern; minimap marks.
- Needs: rebuild + setup run (imports Set_* meshes). Then screenshots to tune TideGlow, ClockHand*, MapAvenue*, StyleSkylineScale, terrace heights.
- Later: bridges / cliffs (authored meshes), per-map sky hills, retire unused Ringmaster / Abbot rows.

- 2026-11-15: Animation Remaster 6 (chase camera): the 20 SQ_Sig attacks and all SQ_Run/SQ_Walk takes re-staged to read from behind (wind-up twist, snap through on the unchanged impact frame, overshoot, arms wide of the body); new SQ_Jump_Air pose strip (USQAnimInstance::SetAirborne, sampled by vertical speed), SQ_Jump_Land, SQ_Dash, authored SQ_Slide. REIMPORT_MARKER -> SQ_Marker_Remaster6 (setup run re-imports). Tuning AnimAirRiseSpeed / AnimAirFallSpeed.

- Nov 15 2026: map art pass (ground art M_GroundV3 + KayKit standing scenery, 46 meshes). Needs rebuild + setup run. Next: check screenshots for house facing (MapKitFrontYaw), ground joint strength, scenery density.

- [x] Nov 15: animation pass 2 (Remaster 7): per-reaper E/R casts, state poses, re-staged shared takes and gaits, look offset. Not done: enemy/boss animation, idles, spring bones. Needs rebuild + setup run.

- 2026-11-19: Animation Remaster 9. All 21 reapers rebuilt on the hand-animated KayKit takes with a character layer each (gait, strike feel, idle act); stride matching is bounded (runs skate x1.0-2.0, walks up to x3.4 on the heavy bodies: short legs at 550 cm/s); victory/defeat/bank, SQ_Jump_Land and SQ_Dash on hand-animated bases. Horde takes in Skeleton_*.fbx rebuilt on hand-animated bases (boss roar kept from Nov 7: the long taunt read worse). REIMPORT_MARKER SQ_Marker_Remaster9 (reaper and the four skeleton bodies re-import on the next setup run). Far impostor walk frames not re-baked. Hook for impostor hit-squash and far death-pop written for the SQHordeManager owner.
- [x] Nov 19: effects remaster checkpoint 5/10: motion kit meshes, per-particle fade (M_SQGlow2), FxRecipes.csv layered effects for all 21 reapers (attack accent, E, R) and shared events, hit starbursts, death wisps, chest / lantern / shrine / level-up / soul hooks, shake. Needs rebuild + art import + style material script. Not verified in the editor.

## Nov 23 2026: co-op pass (findings 1 to 6 of 13)
Jack: "work through every single one of your 13 findings". Details in Docs/CoopFeatures.md.
- Done in code (not compiled or played in Unreal): 1 squad combos + Close Ranks aura + revive rewards + Bonds shown in HUD and lobby; 2 free chests pay every reaper (rest of loot already personal / shared; souls = personal sack, shared quota); 3 ghost shift (gather XP and gold, F to haunt, lantern revive); 4 ping tap + wheel.
- Audited, already right: 5 clients save their own progress; 6 level-ups never pause co-op.
- Needs: rebuild. Then check ping wheel direction, middle-mouse binding, combo strength against boss TTK.
- Later: ghost-near-reviver speed-up, quests for SquadCombos, bond hint on level-up cards, model combos and the aura in balance_model.py.

## Platform pass (findings 7 to 9), Nov 24 2026
Details in Docs/Platform.md and Docs/Steam.md.
- Done in code (not compiled or played in Unreal): 9 graphics options, first-run auto detect, frame-rate governor; 8 controller map, rebinding, glyph caps, rumble, Deck-readable HUD scale, ACCESS tab; 7 late-join catch-up, reconnect to a held reaper, host-quit banking, LAN run list, invite code, Steam-ready session layer.
- Needs: rebuild (new files SQInput, SQPerf, SQSession; Build.cs gained ApplicationCore and Sockets). No setup script.
- To test: pad through every menu; rebinding; governor under a big horde; two machines for the LAN list, drop and rejoin, host quit.
- Later: Steam switch (needs app id), invite overlay button, session player counts, on-screen keyboard for the address box, chest cue labels that flip with the device mid-run.

## Content pass (findings 10 to 13), Nov 24 2026
Details in Docs/Content.md and Docs/Pricing.md. Not compiled or played in Unreal.
- 10 The Long Haunt: optional loops after the boss (CASH OUT / KEEP REAPING), best loop saved per map, reaper and Hard.
- 11 Unlock pacing: Death's Ledger ranks, 66 ladder and milestone quests, 12 lantern trail colours, NEXT UNLOCK panel, model section 27.
- 12 Run events (Events.csv, 12 events incl. King Tide and the two Clocktower events), district conditions, Daily Haunt (local), clause unlocks through play, model section 28.
- 13 Pricing note: Early Access $7.99 with a 10% launch discount, $9.99 at 1.0.
- Needs: rebuild (new files SQEventSite.h/.cpp). No setup script.
- To test: one mission per map watching for 3 events; a site hold solo and with a bot; King Tide; both clock events; the Daily Haunt row; a locked clause; KEEP REAPING once.
- Later: Gilt Scuttler art and flee behaviour; visible spirit ally; sky / fog looks for Blood Moon, Soul Storm, Fog Bank; condition name on the ENTERING banner; online daily board (Steam).

## Map finish (Nov 24 2026) - built, needs a compile + test
- [x] Stairs and stone/timber-faced terrace banks (BuildTerraceFaces)
- [x] One raised walkway per map with collision and horde footprint (PlanWalkway / BuildWalkway)
- [x] Harbour sea horizon + lighthouse beam; Clocktower clock-face skyline (BuildMapSky)
- [x] Blood Moon and Soul Storm event skies; Trapped Spirit companion wisps; Gilt Scuttler flee + gilt look; district condition pennants
- [ ] Horde melee should skip reapers standing on the walkway deck (hook in Docs/Maps.md)
- [ ] Real-art pass for the walkway / lighthouse / skyline clocks (currently engine primitives in map colours)

## Character pass (Nov 25 2026) - each reaper completely different
- [x] Audit: lineup / back / silhouette renders, kit overlap list (Docs/Characters.md, outputs/chars)
- [x] Body proportions + sizes per reaper (Body* columns, ApplyBodyShape), HitRadius, gait personalities
- [x] Showroom / lobby on the native anim instance (own idle, gait, body, size)
- [x] Kit overlaps removed; ability Mods (Charges, Recast, Stacks, Trap, Detonate)
- [x] Movement traits (21 unique), barks, bio / tip / difficulty / playstyle in the lobby kit panel
- [x] FX iterations 9-10 for the reworked kits
- [ ] Compile + test in Unreal (whole pass is uncompiled)
- [ ] Signature-weapon balance pass (kits still outside the band: see Docs/Characters.md, Balance)
- [ ] Voice: record the bark lines in Docs/Characters.md
- [ ] Real per-character rigs for the quadruped / floating plans (werewolf on all fours, horseman's steed) - beyond bone scaling

## Run structure (Nov 25 2026, Docs/RunStructure.md)
- [x] 3-stage run in one level (Graveyard -> Drowned Harbour -> Clocktower), level / items / gold carry over, Difficulty step per stage
- [x] Boss climbs out of the last lantern after an overflow warning; objectives are bonuses (bell behind BossFromLantern 0)
- [x] Map boss move sets: Gravekeeper (tomb walls, burrow, raise, cracks), Drowned King (flood, tentacles, ghost ship, king tide), Merchant (time zones, clock hands, rewind + gear)
- [x] Soul rift + Soul Passage (augment pick, floating chests, host can drop early)
- [x] Endless horde after the last boss with kills / time / souls on the HUD and records; Long Haunt switched off
- [x] Souls bank (second meta currency) + SKINS page, 63 skins, lobby skin cycling, skins replicate to the crew
- [ ] Compile + playtest the run chain (boss TTK flags in Docs/RunStructure.md, rift/passage feel, endless pacing)
- [ ] 5 stages: two more maps (Missions.csv rows + biomes), then Tuning RunStages 5
- [ ] Passage camera looking down (currently the normal chase camera inside the tunnel)
- [ ] Bespoke boss models / animations for the three map bosses (they use the KayKit bodies with props)
