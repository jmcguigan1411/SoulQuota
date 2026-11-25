# Run structure (Nov 25 2026)

Jack: "after all the lanterns have been filled, a boss should rise... each time a different map after the boss is killed...
we jump into his soul and it teleports us to another underworld." / "At the end of the final stage I want it to just be an
endless horde... count kills, souls banked... use these souls as a currency in the menu to buy skins for each character."

## STATUS (keep current; a resumed thread starts here)
- [x] 1 Stage chain (3 stages, one level, arena rebuilt per stage) - ASQGameMode StartStage / BeginPassage / StartNextStage
- [x] 2 Boss rises from the last lantern (overflow beat) - BeginOverflow, ASQLantern overflow look, ASQBoss entrance
- [x] 3 Boss move sets (Gravekeeper / Drowned King / Merchant of Hours) - SQBossMap.cpp, Bosses.csv
- [x] 4 Soul rift + Soul Passage - ASQSoulRift (SQSoulRift.h/.cpp), BeginSoulRift / EnterRift / TickSoulPassage
- [x] 5 Endless horde after stage 3 - BeginEndless / TickEndless (Long Haunt off: Tuning EndlessLoops 0)
- [x] 6 Souls as meta currency (USQSaveGame::SoulBank) - ASQPlayerState::RunSoulsBanked, ClientRunRecords
- [x] 7 Skins shop + skin textures + previews - SKINS page, Scripts/make_skins.py, Cosmetics.csv, outputs/skins
- [x] HUD / menu / quests / balance (section 27) / docs
- [ ] Sync to the PC + GitHub claude-wip (see the end of the thread)
Uncompiled in Unreal (syntax harness clean). Needs a C++ rebuild and a setup run (skin textures, chest).

## A run, start to finish
1. Lobby / solo START REAPING as before. The run is a CHAIN of RunStages (Tuning, 3) missions taken from Missions.csv in
   order, starting at the mission the host picked (wrapping): Graveyard -> Drowned Harbour -> Clocktower.
2. Each stage (~7 min): lantern trail + optional objectives (bonus rewards only, no gate).
3. The last lantern filled (or its deadline passed) -> THE LANTERN OVERFLOWS (Tuning OverflowSeconds) -> the stage boss
   climbs out at the lantern.
4. Boss dies -> its soul tears out as a giant spinning soul rift. Walk into it (solo: instant; co-op: everyone standing, or
   SoulPortalWait seconds). Downed reapers revive on entry.
5. The Soul Passage (SoulPassageSeconds): a dark tunnel of rising souls high above the next map. Everyone gets an augment
   pick and is healed; two paid bone chests per reaper float in the tunnel. The host can press F to drop early once the picks
   are done. Meanwhile the arena is rebuilt as the next map.
6. Landing: the next stage starts at its player starts. Level, weapons, tomes, items, augments, gold carry over;
   Difficulty += StageDifficultyStep.
7. After the LAST stage's boss: the ENDLESS HORDE on that map until the crew wipes. Lanterns keep relighting around the
   map (EndlessHordeLanternSeconds), elites and minibosses on timers, spawn rate / HP climb on the Final Swarm curve plus
   EndlessHorde* keys. HUD: kills, time, souls banked. The run is a WIN however it ends (the last boss fell).
8. End screen: stages cleared, kills, endless time, souls banked (and the souls added to your bank), gold banked, records.

## Why one level
The arena is spawned by C++ (ASQArena::SetBiome rebuilds it in place, already used by the lobby). Staying in one level
keeps every PlayerState / character / ability / augment exactly as it is, needs no seamless-travel plumbing, and co-op
clients just see the replicated arena rebuild. The rebuild hitch happens while the crew is in the Soul Passage.

## Bosses (Bosses.csv Abilities / Phase2 / Phase3; code in Source/SoulQuota/SQBossMap.cpp)
Every attack is telegraphed with vertical pieces (warning pillars, standing posts, impact columns: MulticastFX kinds 10-15)
because flat ground lines vanish from the chase camera. Strikes spread one per standing reaper; boss HP already scales
with crew size (BossHealthPerPlayer). Entrance: climbs out of the lantern for BossEntranceSeconds (invulnerable).
Death: a column of light, rings and a local camera beat (no slow motion: co-op).

| Boss (map) | Phase 1 | Phase 2-3 |
|---|---|---|
| The Gravekeeper (Graveyard) | TOMBSTONE WALL: a line of posts bursts up across the victim's path and stays 6-7 s (shoves you out: go round). BURROW: he sinks (invulnerable), a marker hunts the victim slower than a run, he erupts with a knock-up. RAISE: graves open along the path he walked and the dead climb out. | CRACKS: lines of strikes race from him toward each reaper and leave pits for 4-6 s. |
| The Drowned King (Drowned Harbour) | FLOOD: the tide climbs past its high mark for 9 s (flooded ground slows: keep to the causeways). TENTACLES: three bursts in a line where each reaper is heading (change direction). GHOST SHIP: a lane is marked, then a ship sails it ramming and firing broadsides both ways. | KING TIDE: 10 s of very high water; flooded ground burns (only the walkway / high ground is safe). |
| The Merchant of Hours (Clocktower, final) | TIME ZONES: slow / freeze circles under the crew, a haste circle to run for (8 s). CLOCK SWEEP: a clock hand (two in phase 3) turns a full circle from him: jump it. | REWIND: a glowing gear appears; stand on it for 0.6 s within 5 s or he heals back to his health 5 s ago (jamming it staggers him). |
Off their own map the abilities still work (Flood falls back to pools when there is no tide).

## Endless horde
EndlessHorde 1. After the last stage boss: no rift; Final Swarm tiers + ramps run on the last act's horde, Difficulty
climbs EndlessHordeDifficultyPerMinute, an elite pack every EndlessHordeEliteSeconds, minibosses every
EndlessHordeMinibossSeconds, the lantern moves every EndlessHordeLanternSeconds (the flame flies there, the beam
follows). HUD: big kill counter, time, souls banked. Ends on a wipe; the run counts as a WIN (all gold at the win share).
Records per profile: EndlessBestSeconds, EndlessBestKills, EndlessBestKillsByReaper, RunsFullCleared.
The Long Haunt (Nov 24) is switched off (EndlessLoops 0); its code is still there behind the switch.

## Souls and skins
- Every soul a reaper banks (lantern, Keeper-style passives) counts to that player (ASQPlayerState::RunSoulsBanked) and
  goes into their own profile's SoulBank at run end, win or lose (ClientRunRecords). Banked gold stays the Death's
  Office currency.
- Skins: Cosmetics.csv Kind Skin rows (Id, Name, Color, Reaper, Price, Tex, Desc), written by Scripts/make_skins.py.
  3 per reaper = 63: two of Bone Moon / Ember Wake / Abyssal / Verdigris / Grave Rose (the themes furthest from the
  reaper's own hue, rotated) at 2500 and 6000 souls, and Spectral at 12000. Textures ArtSource/Characters/skins
  R_<Id>_<Theme>.png -> T_R_<Id>_<Theme> (import_art.py). A skin is a gradient map per colour family (main, second,
  trim, neutral) so the painted shading and the material separation survive.
- SKINS page (main menu): Up/Down reaper, Left/Right skin, Enter/click buys or wears; the showroom previews the focused
  skin. Lobby: the kit panel shows the worn skin; K / click cycles owned ones. The worn skin replicates
  (ASQPlayerState::SkinId) so the crew sees it.
- Pricing (balance_model section 27): a solo full run banks ~2900 souls, a 4-player run ~1460 each; a run that
  falls early banks less. First skin after ~1-2 runs, a Spectral after ~5-8.
- Previews: /mnt/user-data/outputs/skins/skins_final_all.jpg (rows: standard, skin 1, skin 2, Spectral).

## Balance (Scripts/balance_model.py section 27, normal, no meta, seed 0)
Stages 420 / 450 / 480 s, 4 lanterns each. Solo: boss up at 5.8 / 5.5 / 5.7 min, TTK 203 / 173 / 95 s, 26 min to the
endless horde. 4 players: 4.8 / 4.7 / 4.8 min, TTK 100 / 180 / 212 s, 24 min. Boss HP mults 0.30 / 0.55 / 0.48
(Graveyard now uses the Gravekeeper). Flags: Clocktower solo TTK is short (95 s) while 4-player is on target;
Graveyard 4-player is short (100 s). Tune after a playtest.

## Five stages later
Data only: add two Missions.csv rows (new biomes) and set Tuning RunStages 5.
