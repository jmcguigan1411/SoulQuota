# Soul Quota - project rules for Claude

You're helping me build **Soul Quota**, a co-op top-down horde mission game (1-4 reapers clear a
mission's objectives with mouse-aimed kits, then fight the boss that rises) in **Unreal Engine 5.8** using **C++**
(data in `Content/Data/*.csv`, editor setup via Python in `Scripts/`), for release on **Steam (PC)**.

- Project path: `E:\Projects\SoulQuota`
- Engine: `E:\UnrealEngine\UE_5.8`
- Main level: `/Game/Maps/Graveyard` (arena itself is spawned by C++ from Biomes.csv)
- Roadmap: `Docs/BuildPlan.md` - add every new pre-release ask there
- Design: `Docs/GameDesign.md`

## About me
Experienced developer, new to Unreal. Never guess at the editor UI. If you aren't sure where a
button or setting is, say so.

## How to work with me
- One step at a time, clear numbered steps. Short replies, no hedging, no filler.
- Code changes: you may edit files in this project directly, but back them up first
  (copy to `Saved/Backups/<date>/` or commit). Otherwise send filename + Before/After snippets.
- Don't create new files, assets or features without my permission.
- Never control my mouse or keyboard; no browser extensions. Give manual steps instead.
- Test steps in bullet points: exactly what to check and what to send back (log lines, screenshots).
- Blueprint/graph changes: show as a node-and-wire diagram, not prose.
- When I say something is "perfect" or "works": keep an exact backup copy or git tag of that state.
- Only original assets and names (no copyrighted games, characters or real brands). Flag any IP risk.
- Put the `cd <folder>` line inside every command block.
- I prefer big batches of work and one test session over testing every small change.

## Build (treat warnings as errors - report any warning as a failure to fix)
Close the editor first.
```powershell
cd E:\Projects\SoulQuota
& "E:\UnrealEngine\UE_5.8\Engine\Build\BatchFiles\Build.bat" SoulQuotaEditor Win64 Development -Project="E:\Projects\SoulQuota\SoulQuota.uproject" -WaitMutex | Tee-Object build_log.txt
```
One-time setup scripts (the editor runs them, then CLOSES itself - that's normal):
```powershell
cd E:\Projects\SoulQuota
& "E:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "E:\Projects\SoulQuota\SoulQuota.uproject" -ExecutePythonScript="E:\Projects\SoulQuota\Scripts\setup_project.py"
```
Normal launch:
```powershell
cd E:\Projects\SoulQuota
& "E:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "E:\Projects\SoulQuota\SoulQuota.uproject"
```
If the editor offers to import changed CSV files from Content/Data, choose Don't Import (the game reads the CSVs directly).
If a build fails, read `build_log.txt` yourself.

## Git
Remote: none yet - set up GitHub once the game is worth saving. Until then back up to Saved/Backups/<date>/ before edits. Commit only after I confirm a test passes, with the editor closed.

## Decisions log
- Genre/market: co-op horde game (research Nov 2026: co-op sells ~8x non-co-op median). Since Nov 9: top-down missions (see the 2026-11-09 entries).
- Engine: UE 5.8 C++, listen-server co-op, horde simulated as data (no actor per enemy).
- Custom data-driven ability system instead of GAS (GAS is per-actor; 1,000 data enemies are not actors).
- Art direction: claymation (rim-lit clay material, stepped ~12fps animation, splats). Placeholder shapes until real art.
- Scope: Early Access = 6 reapers, 5 biomes, 7 bosses, 24 weapons/16 tomes/23 items/24 evolutions (Nov 7 items/tomes pass: 4 weapon + 4 tome slots, chest items stack), 8 Bonds. (Nov 9: Endless retired; single-stage missions with Hard modes instead.)
- Price target $7.99. Steam integration pending a Steam app id (OnlineSubsystem is Null/LAN for now).
- Art (Nov 2026): CC0 KayKit packs by Kay Lousberg (Skeletons + Halloween Bits) in ArtSource/, imported by Scripts/import_art.py to /Game/Art. CC0 = no attribution required, commercial use OK; credited anyway. IP note: these are third-party (not original) but public-domain; no trademarked names used. Replace with custom clay art later if desired.
- Every art hookup has a primitive-shape fallback; art names live in the CSVs (Reapers Model/Weapon, Enemies Model/Anim/Height, Bosses Model/Weapon, Biomes Props/Decor).
- Renamed evolution "Iron Maiden" -> "Spiked Coffin Lid" (band-name IP risk).
- Jack (Nov 6): "Don't ask me to test again" - work in big batches, hand over one build + setup command.
- Art direction (Nov 7, from sales research): goofy, chunky low-poly 3D with strong personality (Megabonk / R.E.P.O. / PEAK pattern). Beat Megabonk on: co-op, nicer styling, much better animation, more items/abilities.
- Jack (Nov 7): animation quality is the #1 issue; map must have multiple height levels like Megabonk (top-down maps since Nov 9: low terraces only, no jumping).
- Wording (Nov 7): no "clock in / clock off / time card / night shift" phrasing anywhere in Soul Quota (that's Highly Incompetent's theme). Main button = START REAPING. Meta currency is banked gold (code field OvertimePay; was shown as SOUL COINS before Nov 9).
- Lobby (Nov 7): players can't move until everyone is ready; then a 3-2-1 countdown, everyone is placed at the map's player starts, rises from the ground, the mission starts. Host: B = next unlocked mission, O = Hard mode (or click the lobby MISSION panel). Lobby panel docks right, TAB hides it.
- Own character (Nov 7): "Reaper, Junior Grade" built in Blender (ArtSource/Characters/Reaper_Custom.fbx, rigged to the KayKit rig, 101 anims incl. 6 custom). Grim uses it.
- Reapers (Nov 7): 21 reapers; roster grid in menu + lobby. (Nov 9: the per-level passives are replaced by kits: Kits.csv passive + E + R + signature weapon, see Docs/Kits.md.)
- IP (Nov 7): Jack wants no copyright/infringement risk. Docs/IP_Audit.md is the record; licences in Content/Licenses (staged with the build). Open risk: an itch.io project already named "SOUL QUOTA" (zarck5678) - trademark search needed before the Steam page.
- 2026-11-09: Game remade to play like a top-down horde mission game (inspired by LoL Swarm, mechanics only, no Riot names or art): top-down camera, mouse-aimed E/R kits with signature weapons, single-stage missions with objectives, augments, Hard modes, Death's Office meta upgrades. Skins and art kept.
- 2026-11-09: One meta currency: banked gold.
- 2026-11-09: Jumping, sliding and the skate park removed from play (kept behind TopDownMode 0).
- 2026-11-09: Playable roster redesigned as 21 original monsters (no skeleton look for players): Headless Horseman (Sir Hollowhead, public-domain folklore; no Disney/Burton look), Pumpkin Man, Zombie Warrior, Undertaker, Ghost, Witch, Wizard, Butcher (comic, no slasher-film masks/chainsaws), Shade, Vampire (no Universal Dracula look), Mummy, Werewolf, Patchwork Brute (no flat head/neck bolts), Gargoyle, Bog Hag, Imp, Scarecrow, Reanimator, Ghoul Courier, Moth Auditor, Puppeteer. Ids unchanged; roles + kit mechanics carried over.
- 2026-11-09: Every monster has its own idle/walk/run/signature attack (SQ_*_<Id>); animations must stay bespoke per character, never generic.
- 2026-11-09: Dash goes the way you are moving; only a standing player dashes toward the cursor.
- 2026-11-09: Maps are random every run (RunSeed), validated so every start/zone/pad/objective is reachable; fallback = fixed layout.
- 2026-11-10: Effects style approved: stylised shaped effects (effect kit meshes) in each monster's own colours, three beats (anticipation, bright layered impact, lingering tail). Every monster has its own entrance on character select suited to its name, and its attacks / E / R use its own effect theme (SQFxTheme).
- 2026-11-10: Enemies leave nothing behind except XP. XP = green crystals (tiers up to a white prism when many merge); gold = yellow coins -> coin stack -> purse -> gold bar. Souls stay soul-teal.
- 2026-11-10: Cursor only visible in a fight with the FREE camera; FIXED camera auto-aims.

- 2026-11-10: import_art.drop_if_stale re-imports roster bodies, palettes, weapons and FX meshes when the source file is newer than the saved .uasset (delete_directory had left the old R_* bodies on disk, so new art never loaded).

- 2026-11-10: Found SQArt.cpp/.h and Bosses.csv still at v1.0 on the PC (earlier sync missed them): monster bodies got no animations and no own palettes. Synced; added full-tree md5 compare as the sync check.
- 2026-11-10: Map remaster: every interactable uses one shared cue (USQInteractCue: ground ring + floating diamond + near label, colour-coded by kind, greyed once used). Plazas are laid flagstones, the road has kerbs, landmarks have glowing marker posts, edge props come in silhouette clusters inside a darker framing band.

- 2026-11-10: Souls render as teal flame wisps (SM_FX_Flame), grey when the local sack is full + hint; XP crystal sizes 56/68/84/104 cm, tilted 28 deg for the top-down camera.

- 2026-11-10 Map revamp: arena 224 m (ArenaHalfSize 11200, 4x area). Six named districts + boss district, each with an original skate theme (Bowl / Banks / Rollers / Quarter / Spine / Kickers) baked into the height grid (walkable) plus bone Park_* pieces with collision; lanes from the hub to every district with glowing chevrons, beacons and floor name plates. Minimap bottom-right, north-up (= fixed camera screen-up) with fog of war; M toggles a full map with district names + legend; ENTERING <district> banner. Objectives use USQInteractCue. Counts scaled for the area. Skate games inspired principles only; no layouts, names or art copied.

- 2026-11-10: Souls remade as an original spectral wisp (white core orb, teal flame body, 3 orbiting motes, ground glow) with a client-side collection flourish (arc into the reaper + ring burst). Inspired by the readability of LoL-style soul pickups, no Riot shapes/colours copied.

- 2026-11-10 (menu pass): Character select hover only highlights a card; the showroom model/entrance changes on click or keys (lobby LobbyHover, main menu ReaperHoverCard). Readability: SQUI::ReadablePx lifts small text (toward 16 px, min ~10 real px at low res) inside HudTxt/Txt + wrap line steps; HUD shadowed text gets a 1 px outline; menu text a faint drop; Disabled colour lifted to 8A80A0; key in-run labels (XP bar, augments, crew, R-click hint) enlarged; interact-cue world labels 36 cm with a dark drop-shadow twin.

- 2026-11-10 (map art pass): ground text removed; ground repainted to a muted value band with worn paths, wall contact shade and concrete skate ground + coping; candle-lit lanes and stone monuments replace neon chevrons/beacons; composed vignettes between districts; fireflies + lamp flicker; grade retune; M_GroundV2 material (needs a setup run, falls back to M_Ground).

- 2026-11-12: USQAutoShot (dev only) saves a screenshot every AutoShotEvery seconds to Saved/SQShots so Claude can review how screens actually look. Direction change: third-person chase camera (Megabonk-style framing, original implementation), top-down camera and weapon aim indicators removed, manual aim optional in settings.

- 2026-11-12 (design thread, Docs/MarketAudit.md): runs shortened to the genre norm. Stages 960-1200 s -> 600-840 s, objectives 5-6 -> 3-5, boss health mults 0.57-1.18 -> 0.26-0.71 (model: 11-17 min runs, 150-225 s boss). Faster start: spawn 0.6 -> 1.2 /s, opening swarm at 4 s, tutorial hold 60 -> 20 s, solo countdown 1 s (StartDelaySolo), end screen 20 -> 8 s. Meta: first ranks 40% cost, growth 3.3, loss banks 20%. NightSeconds 90 -> 360 so test play = release length. Recommended, not done: auto-bank souls on map 1, RETRY button, solo lobby skip, 3-prompt tutorial, unlock screen.

- 2026-11-12 (camera thread): Third-person chase camera is the only gameplay camera (Megabonk-style behaviour, original code). Mouse / right stick orbit, camera-relative WASD, Space jump + Ctrl/C slide (Tuning ChaseJump), Shift / R-click dash, FOV 90, distance 850 (wheel 450..1500), terrain clamp from the arena height field. FIXED/FREE overhead modes, MMB orbit, cursor lean, the reap ground cone, the cursor cooldown ring and the E/R range/aim indicators are removed (AbilityIndicators 1 restores the last). SETTINGS > CAMERA: Aiming AUTO|MANUAL (USQSaveGame::AimMode), camera distance, mouse sensitivity, follow smoothing, invert Y, shake. Cursor hidden in fights; hold Left Alt to free it. Minimap stays north-up with a camera-view wedge. Old profiles reset zoom/tilt once (SettingsVersion 2).

- 2026-11-12: Style pass - art direction "Midnight Fairground" (Docs/ArtDirection.md). Sky dome/moon/stars/hills/skyline (ASQArena::BuildSky), ink outline post-process M_SQInk, rim light M_ArtTintV2 via SQArt::TintMID, vivid Graveyard palette + SkyHorizon/Mid/Zenith columns in Biomes.csv, fog blends to horizon, global effect dials (StyleFx*). Every strength is a Style* key in Tuning.csv. Reverses the Nov 10 ground desaturation (MapGroundDesat 0). Needs setup run for the three materials.

- 2026-11-12 flow pass: end screen has RETRY / PLAY AGAIN (ENTER) and CHANGE REAPER (C), host or solo only (ASQGameMode::RetryRun / EndToLobby, ASQGameState::bSkipLobby); START REAPING on the main menu is now SOLO and skips the lobby (travel option SQSolo, Tuning SoloSkipLobby); co-op hosting moved to PLAY CO-OP > HOST A LOBBY; the Training Shift is 3 prompts (move+look / weapons fire themselves, crystals level you up / the goal), horde holds only until prompt 1, 7 of 13 tips retired; QUESTS page renamed UNLOCKS with NEW badges (USQSaveGame::QuestsNew) and an unlock strip on the end screen. EndScreenSeconds 8 -> 30 (idle fallback only).

- 2026-11-12 (visual pass, from the first real screenshots): motion blur / film grain / lens flare / fringe off (DefaultEngine.ini); ground palette rationed (one hue per biome, StyleGroundVivid 0, MapDistrictTint 0.04, MapSaturation 1.06); new additive glow material M_SQGlow (setup run builds it); souls are additive teal wisps with 4 sizes, merge at SoulMergeAt, 2 per kill, dim teal on a full sack (no grey balls); ASQFx area impacts/telegraphs layered (shockwave, echo ring, flash, cracks, shards, motes; danger fill grows to impact); glowing spark shapes additive (StyleFxAdditive). Docs/ArtDirection.md has the revised rules.

- 2026-11-12: Lantern trail (Docs/LanternTrail.md). Lantern sites across the map light one at a time (hub, then districts, last one beside the boss arena; route seeded). Banking the lit lantern quota lights the next (beam, arrow, minimap line, footlights), gives chests / a final augment, grows the sacks and the Difficulty; a lantern not filled in its share of the clock burns out and the next lights anyway. Banking is no longer an objective row (BankSouls removed from Missions.csv); the bell needs trail + objectives. Tuning Lantern* keys; BankSeconds 0.7. Model section 18b.

- 2026-11-12 (map thread): three-map remaster. Floor rule (nothing on the floor but pickups: MapFloorClutter 0, LanternFootlights 0). Maps cut to 3: Graveyard (Terraces), Drowned Harbour (Causeways + rising tide), Clocktower (Climb + sweeping clock hands to jump); Plague Carnival and Frozen Monastery retired. New original set pieces (ArtSource/Props/Set_*.fbx, set_palette.png -> T_Set/MI_Set) placed by ASQArena::BuildSetPieces: landmark buildings, gates, yard walls, avenues, lamp posts, statues. Objectives are placed nearest the lit lantern. Backdrop giants scaled down. See Docs/Maps.md.

- 2026-11-15: Animation Remaster 6 (chase camera): the 20 SQ_Sig attacks and all SQ_Run/SQ_Walk takes re-staged to read from behind (wind-up twist, snap through on the unchanged impact frame, overshoot, arms wide of the body); new SQ_Jump_Air pose strip (USQAnimInstance::SetAirborne, sampled by vertical speed), SQ_Jump_Land, SQ_Dash, authored SQ_Slide. REIMPORT_MARKER -> SQ_Marker_Remaster6 (setup run re-imports). Tuning AnimAirRiseSpeed / AnimAirFallSpeed.

- 2026-11-15: Map art pass. Ground art: terrain-only M_GroundV3 (flagstones / planks / chequered tiles, plaza mosaic, wet tidal mud, distance-faded joints) driven by UV masks + painted moss and lantern wear. Standing scenery: ASQArena::BuildScenery places two more CC0 KayKit packs (Dungeon Remastered -> KitD_*, Medieval Hexagon -> KitM_*; licences in ArtSource/LICENSE_KayKit_*.txt, no attribution required) as house fronts, towers, cargo against buildings, wall runs with banners, lane pillars, groves and a forest backdrop. No IP risk: CC0 assets, original layout.

- 2026-11-15 (animation pass 2, Remaster 7): every reaper has its own E and R cast take (SQ_E_<Id>, SQ_R_<Id>; ten cast shapes chosen by what the ability does; R holds its pose) built on its own idle; Kits.csv lists them first. New per-reaper SQ_Victory_/SQ_Defeat_/SQ_Downed_/SQ_Revive_/SQ_Bank_<Id>, SQ_Sig_Grim. Shared cast/attack/ability takes and all walks/runs re-staged again. Code: own state loops in USQAnimInstance::SetStyle, look offset (SetLook, Tuning AnimLookScale), SQ_Dash plays on every machine. Generator: blender_work/sq_anims7.py (starts from backup/Reaper_Custom_pre7.blend). REIMPORT_MARKER Remaster7: needs a setup run.

- 2026-11-19: Animation Remaster 9. All 21 reapers rebuilt on the hand-animated KayKit takes with a character layer each (gait, strike feel, idle act); stride matching is bounded (runs skate x1.0-2.0, walks up to x3.4 on the heavy bodies: short legs at 550 cm/s); victory/defeat/bank, SQ_Jump_Land and SQ_Dash on hand-animated bases. Horde takes in Skeleton_*.fbx rebuilt on hand-animated bases (boss roar kept from Nov 7: the long taunt read worse). REIMPORT_MARKER SQ_Marker_Remaster9 (reaper and the four skeleton bodies re-import on the next setup run). Far impostor walk frames not re-baked. Hook for impostor hit-squash and far death-pop written for the SQHordeManager owner.

- 2026-11-19: Effects remaster (round two, checkpoint 5 of 10). Motion kit: 14 new ArtSource/FX meshes (Swoosh, Arc, Streak, Wave, Teeth, Spikes, Ember, Petal, Chevron, Wisp, Sigil, Cog, Web, Casket) and M_SQGlow2 (M_SQGlow x per-instance custom data 0 = brightness, so particles fade). Content/Data/FxRecipes.csv is the effect book: one row = one layer (verbs Flash Glint Wave Swoosh Mark Pop Pillar Pillars Lance Burst Dust Rise Ring Fan Line Shot Bolt Rain Cracks Shake), keyed <ReaperId or *>|<Event>; SQFxRecipe.cpp plays it, SQFxTheme PlayAttack/PlayCast and ASQFx::Spawn try it first and fall back to the old code. It only plays when the kit meshes are imported (SQFx::HasMotionKit; Tuning StyleFxMotionKit 0 turns it off). The cloud preview blender_work/fx2 (sim.py, recipe.py, gen.py) mirrors it: edit gen.py/bespoke.py, run gen.py, never hand-edit the CSV. Lesson: from the chase camera a flat line on the ground ahead of the reaper is foreshortened to nothing; use vertical pieces (Pillars, Lance, Bolt). Needs a C++ rebuild and one run of the art import + style material scripts.

- 2026-11-23: Co-op pass (findings 1 to 6). Squad combos (ability primes, another reaper's hit bursts; solo: own weapons detonate), Close Ranks aura, revive rewards, ghost shift for downed reapers (gather XP/gold, F haunt, revive on lantern change; DownedSpeed 160 -> 300), ping tap/wheel on middle mouse, free chests roll for every reaper. Souls stay a personal sack with a shared quota. Items 5 (client saves) and 6 (non-pausing level-ups) were already correct. All keys in Tuning.csv (Co-op pass block); notes in Docs/CoopFeatures.md. Not compiled in Unreal yet.

- 2026-11-24: Platform pass (findings 7 to 9). SQPerf: SETTINGS > GRAPHICS / DISPLAY, presets, first-run auto detect, frame-rate governor (cosmetic only, per machine). SQInput: full controller map, rebinding, text glyph caps (no console logos), rumble, ACCESS tab (HUD size, shapes with colours, reduce flashes, hold to confirm). SQSession: the only code that touches OnlineSubsystem (LAN run list, invite code, Steam by config later: Docs/Steam.md). Reconnect: a dropped player's reaper is held by a bot for ReconnectGraceSeconds, keyed by USQSaveGame::PlayerId. Host quit: clients bank a loss share and return to the menu; no host migration. Uncompiled. See Docs/Platform.md.

- 2026-11-24: Content pass (findings 10 to 13). The Long Haunt (optional loops after the boss, host decides). Death's Ledger ranks + 66 quests + 12 trail colours + NEXT UNLOCK panel. Run events from Events.csv (SQEventSite, ASQGameMode::TickEvents; 12 events incl. Harbour King Tide and Clocktower fast / backwards), 2 district conditions per run, Daily Haunt (local date seed), contract clauses now unlock through play (Modifiers.csv Unlock; blank = open). Pricing: Early Access $7.99, $9.99 at 1.0 (Docs/Pricing.md). Original names only (Tithe Caravan, Gilt Scuttler, Wandering Reliquary, Tolling Totem): no IP risk found. Uncompiled. See Docs/Content.md.
