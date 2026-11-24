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
- 2026-11-10: Map remaster: every interactable uses one shared cue (USQInteractCue: ground ring + floating diamond + near label, colour-coded by kind, greyed once used). Plazas are laid flagstones, the road has kerbs, landmarks have glowing marker posts, edge props come in silhouette clusters inside a darker framing band.

- 2026-11-10 Map revamp: arena 224 m (ArenaHalfSize 11200, 4x area). Six named districts + boss district round the ring, each with an original theme (Bowl / Banks / Rollers / Quarter / Spine / Kickers) baked into the height grid (walkable for reapers and horde, under MapTierMaxHeight) plus bone Park_* pieces with UCX collision; lanes from the hub to every district (open, flat, glowing chevrons pointing out), beacons and floor name plates. Minimap bottom-right, north-up (= fixed camera screen-up) with fog of war; M toggles a full map with district names + legend; "ENTERING <district>" banner. Objectives use USQInteractCue. Counts scaled for the area (MapPropDensity, MapInteractScale, chests, lamps, spawner pads, hazards x2). Skate games only inspired principles (connected districts, continuous lines); no layouts, names or art copied.

- 2026-11-10 (menu pass): Character select hover only highlights; showroom changes on click/keys. SQUI::ReadablePx lifts small text in HudTxt/Txt (+wrap line steps); HUD text outline; Disabled colour 8A80A0; key in-run labels enlarged; cue labels 36 cm + shadow twin.

- 2026-11-10 (map art pass): ground text removed (district names live on the minimap + banner only). Ground repainted to one muted value band (MapGroundDesat), no bright rings/stripes, ragged worn roads and lanes, wall contact shade, concrete skate ground with stone coping. Neon chevrons -> candle-lit lanes; neon beacons -> stone monuments (blocking, footprinted). Composed vignettes between districts (MapVignettes). Lamp flicker + fireflies (TickAmbience). M_GroundV2 material (setup run) with M_Ground fallback. Grade: lower moon, less sky fill, less saturation, more contrast.

- 2026-11-12 (camera thread): Third-person chase camera is the only gameplay camera (Megabonk-style behaviour, original code). Mouse / right stick orbit, camera-relative WASD, Space jump + Ctrl/C slide (Tuning ChaseJump), Shift / R-click dash, FOV 90, distance 850 (wheel 450..1500), terrain clamp from the arena height field. FIXED/FREE overhead modes, MMB orbit, cursor lean, the reap ground cone, the cursor cooldown ring and the E/R range/aim indicators are removed (AbilityIndicators 1 restores the last). SETTINGS > CAMERA: Aiming AUTO|MANUAL (USQSaveGame::AimMode), camera distance, mouse sensitivity, follow smoothing, invert Y, shake. Cursor hidden in fights; hold Left Alt to free it. Minimap stays north-up with a camera-view wedge. Old profiles reset zoom/tilt once (SettingsVersion 2).

- 2026-11-12 flow pass: end screen has RETRY / PLAY AGAIN (ENTER) and CHANGE REAPER (C), host or solo only (ASQGameMode::RetryRun / EndToLobby, ASQGameState::bSkipLobby); START REAPING on the main menu is now SOLO and skips the lobby (travel option SQSolo, Tuning SoloSkipLobby); co-op hosting moved to PLAY CO-OP > HOST A LOBBY; the Training Shift is 3 prompts, horde holds only until prompt 1, 7 of 13 tips retired; QUESTS page renamed UNLOCKS with NEW badges (USQSaveGame::QuestsNew) and an unlock strip on the end screen. EndScreenSeconds 8 -> 30 (idle fallback only).

- 2026-11-12: Lantern trail (Docs/LanternTrail.md). Lantern sites across the map light one at a time (hub, then districts, last one beside the boss arena; route seeded). Banking the lit lantern's quota lights the next (beam, arrow, minimap line, footlights), gives chests / a final augment, grows the sacks and the Difficulty; a lantern not filled in its share of the clock burns out and the next lights anyway. Banking is no longer an objective row (BankSouls removed from Missions.csv); the bell needs trail + objectives. Tuning Lantern* keys; BankSeconds 0.7. Model section 18b.

- 2026-11-12 (map thread): three-map remaster. Floor rule (nothing on the floor but pickups: MapFloorClutter 0, LanternFootlights 0). Maps cut to 3: Graveyard (Terraces), Drowned Harbour (Causeways + rising tide), Clocktower (Climb + sweeping clock hands to jump); Plague Carnival and Frozen Monastery retired. New original set pieces (ArtSource/Props/Set_*.fbx, set_palette.png -> T_Set/MI_Set) placed by ASQArena::BuildSetPieces: landmark buildings, gates, yard walls, avenues, lamp posts, statues. Objectives are placed nearest the lit lantern. Backdrop giants scaled down. See Docs/Maps.md.

- 2026-11-15: Map art pass. Ground art: terrain-only M_GroundV3 (flagstones / planks / chequered tiles, plaza mosaic, wet tidal mud, distance-faded joints) driven by UV masks + painted moss and lantern wear. Standing scenery: ASQArena::BuildScenery places two more CC0 KayKit packs (Dungeon Remastered -> KitD_*, Medieval Hexagon -> KitM_*; licences in ArtSource/LICENSE_KayKit_*.txt, no attribution required) as house fronts, towers, cargo against buildings, wall runs with banners, lane pillars, groves and a forest backdrop. No IP risk: CC0 assets, original layout.

- 2026-11-24: Platform pass (findings 7 to 9). SQPerf: SETTINGS > GRAPHICS / DISPLAY, presets, first-run auto detect, frame-rate governor (cosmetic only, per machine). SQInput: full controller map, rebinding, text glyph caps (no console logos), rumble, ACCESS tab (HUD size, shapes with colours, reduce flashes, hold to confirm). SQSession: the only code that touches OnlineSubsystem (LAN run list, invite code, Steam by config later: Docs/Steam.md). Reconnect: a dropped player's reaper is held by a bot for ReconnectGraceSeconds, keyed by USQSaveGame::PlayerId. Host quit: clients bank a loss share and return to the menu; no host migration. Uncompiled. See Docs/Platform.md.
