# Platform pass (findings 7 to 9)

## Status (keep this current: a successor picks up from here)
- [x] 9. Graphics options + governor: code written (SQPerf.*, SETTINGS > GRAPHICS / DISPLAY), wired into sparks and the animated-enemy radius. Uncompiled.
- [x] 8. Controller / Steam Deck / accessibility: code written (SQInput.*, SETTINGS > CONTROLS / ACCESS, glyph caps, rumble, hold to confirm, HUD scale). Uncompiled.
- [x] 7. Join in progress / reconnect / host quit / LAN browser / Steam-ready sessions: code written (SQSession.*, SQGameMode seat holding, menu list + invite code). Uncompiled. All three items are done in code; what is left is a real build and a two-machine test.

## 9. Graphics options (SQPerf)
- SETTINGS > GRAPHICS: preset LOW / MEDIUM / HIGH / ULTRA (CUSTOM once a row is edited), Effects, Horde detail, Shadows, View distance, Outlines, Bloom, Auto quality.
- SETTINGS > DISPLAY: window mode, resolution, resolution scale (50-100%), frame cap, V-sync, FPS counter.
- Engine side: UGameUserSettings scalability groups, resolution scale, frame cap, V-sync (not pushed at the editor viewport in PIE).
- Game side: local cosmetic Tuning overrides (USQData::SetLocalTune): spark pools, effect counts, area-effect layers, animated enemies, impostor shadows, decor distance, fireflies, ink outlines, bloom. Never collision, spawning or damage, so co-op machines may differ. Read when a map starts (apply on the next run); engine groups apply at once.
- First launch: Unreal's hardware benchmark picks the preset once per profile (Tuning PerfAutoDetect; skipped in the editor).
- Governor: smoothed real frame time against PerfTargetFps (or 92% of the frame cap). Over budget for 1.5 s: pressure rises over about 4 s; SQPerf::FxScale() multiplies spark counts (floor PerfFxFloor), SQPerf::HordeScale() the animated-enemy radius (floor PerfHordeFloor). Restores with headroom. Off while paused or in the first 5 s of a map.
- Budgets (reasoned from code, not measured): sparks are pooled ISMs (SparkPool 600 x StyleFxCount, capped at the pool, so cost is bounded whatever the horde size); animated enemies are capped by SkinnedEnemyPool (220 skeletal meshes) + SkinnedRiderPool (24), everything else is impostor ISMs (4 walk frames per type); scenery is capped at 260 pieces + 34 vignettes, ISM-batched; ink outline is one full-screen post-process pass. Worst case is 220 skinned bodies with shadows at ULTRA plus full spark pool: that is what LOW/MEDIUM and the governor cut first.

## 8. Controller, Steam Deck, accessibility (SQInput)
- Default pad map: left stick move, right stick camera, A jump, RB dash, LT slide, X interact, RT E, LB/Y per BuildMappings for R and reap, left-stick click ping (hold = wheel), VIEW map, MENU settings; d-pad picks cards; Y reroll, B skip. See ASQPlayerController::BuildMappings / UpdatePadButtons for the exact table.
- Rebinding: SETTINGS > CONTROLS, keyboard and gamepad columns; only changed binds are stored (USQSaveGame::KeyBinds / PadBinds); swapping avoids unbound actions; movement, camera and card keys are fixed.
- Glyphs: text caps (A B X Y LB RB LT RT LS RS VIEW MENU), no console logos. HudKeyCap and the ability slots translate through SQInput::TranslateCap; "[F]" markers in sentences through TranslateInline; the hint line through ControlsLine. The device in use is whichever was touched last (SQInput::Poll).
- Sticks: dead zone and sensitivity sliders, invert (existing), camera assist (eases behind your run direction while the right stick rests).
- Vibration: hits, downed, team level-up; off switch.
- Menus: main menu, lobby roster, cards, augments, end screen and the settings panel all take d-pad / left stick + A / B (stick repeat via SQInput::ReadNav in the settings panel).
- ACCESS tab: HUD size (0.8-1.35, design width never under 1440), Shapes as well as colours (danger zones fill in five ratchet steps), Reduce flashes (hit flash and danger flicker at 30%, StyleFxFlash scaled), Hold to confirm (end-screen keys need a 0.45 s hold; mouse clicks stay instant), Slide hold/toggle.
- Not done: cue labels on chests are translated when the chest spawns, so they do not flip if you swap device mid-run; no on-screen keyboard for the join-by-IP box on Deck (Steam's own keyboard covers it).

## 7. Joining and reconnecting (SQSession, SQGameMode)
What already existed: joining a run in progress (40 s reaper pick overlay, spawn beside the lit lantern, RunJoinNight scaling), direct IP join, failed-join note on the menu.

What this pass added:
- Catch-up: a fresh mid-run joiner banks floor((TeamLevel - 1) x LateJoinLevelShare) level-up picks and the squad's average gold (Tuning LateJoinCatchUp, LateJoinLevelShare).
- Reconnect: every profile has a random stable id (USQSaveGame::PlayerId), sent as travel option SQId. When a remote human drops mid-run their reaper is kept: a bot plays it (name + " (away)") holding gold, augments, pending picks and rerolls for ReconnectGraceSeconds (300). Joining again with the same id hands the same pawn back, no reaper pick. After the grace the bot simply carries on as a squad bot. ResetRun clears held seats.
- Host quit / link lost: the client banks a loss-sized share of the gold it was carrying (BankShareLoss x 0.5), returns to the main menu and reads a note saying so. The host's run cannot continue without the host (listen server): host migration is not attempted.
- Session service (SQSession, the only code that touches OnlineSubsystem): a listen server advertises itself (Tuning SessionAdvertise); PLAY CO-OP lists found runs (same network on Null) with pad navigation; picking one resolves the address through the service and travels. Any session is dropped when the main menu opens.
- Invite code: the host's lobby shows its address and a ten-letter code for it (F2 copies). The join box accepts the code or an IP. The code is the LAN address the socket layer reports, so it works for the same network; internet play still needs the host's public IP and port 7777 forwarded until Steam is on.
- Steam: Docs/Steam.md. Same SQSession calls; no Steam SDK types in project code.

Replication: nothing new replicated. StableId and bHeldSeat are server-only fields on ASQPlayerState; notices use the existing ClientNotice RPC.
Save: PlayerId (made on first load); LastJoinAddress now stores the decoded address when a code was typed.
Bots: a held seat is an ASQBotController on the leaver's pawn; ordinary squad bots are untouched.

Not done / to check in a real build:
- The LAN list needs two machines (or two PIE instances) to prove; Null's beacon uses UDP 14001, which a firewall prompt may block the first time.
- A held seat's bot uses the bot brain's default behaviour for that reaper; it does not spend the leaver's pending picks.
- Session player counts are not kept up to date (the list shows host name and ping only).
- The clipboard and local address calls need the ApplicationCore and Sockets modules (added to SoulQuota.Build.cs).
