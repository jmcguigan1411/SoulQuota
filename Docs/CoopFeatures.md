# Co-op features (Nov 23 2026 pass)

Jack asked for all 13 research findings to be built. This file covers items 1 to 6 (co-op). Nothing here has been compiled or played in Unreal yet; the last section lists what to check. Every number is a Tuning.csv key.

## 1. Squad synergies
- **Squad combos.** An ability hit (Q / E / R, anything that goes through HitCircle or LineHit) *primes* each enemy it hurts for `ComboWindow` seconds. A hit from a different reaper on a primed enemy makes it burst: `ComboBurst` x the priming damage in `ComboRadius`, credited to the detonator. The squad gets `ComboUltCharge` each burst; nearby players see a burst effect and "COMBO xN". Solo: with one reaper your own weapons detonate your own primes (`ComboSolo`). Bots prime and detonate like players. `ComboCooldown` limits bursts per reaper; `ComboEnabled 0` turns it off.
- **Close Ranks.** Standing within `SquadAuraRange` of up to `SquadAuraMax` reapers gives `SquadAuraDamage` damage and `SquadAuraArmor` damage reduction per ally. Solo gets `SquadAuraSolo` stacks for free. HUD chip "CLOSE RANKS xN" (co-op only).
- **Revive rewards.** Reviver: `ReviveRewardHeal`, `ReviveRewardUlt`. Both: `ReviveRewardHaste` for `ReviveRewardHasteTime`. Counts for the Revives quests.
- **Bonds** (existing, Bonds.csv: 13 relic pairs across two reapers). Now shown: active bonds as chips above the ability bar; the lobby bottom bar cycles through every bond and its two relics.
- **Kit support hooks** already existed and are data-driven (Kits.csv effect columns: Heal, Shield, Haste, Empower, Fortify, Revive on allies). Not changed.

## 2. Personal loot
- XP: team XP, everyone gets the full amount (existing, AddTeamXP).
- Gold: shared purse, every reaper is paid from any coin (existing, `GoldShareCoop`).
- Level-up and augment picks: per player (existing).
- Shrines and lanterns: squad-wide (existing).
- Chests: paid chests belong to the buyer (their own gold). **New:** a FREE chest gives every other reaper their own roll too (`ChestFreeForAll`).
- Souls: **decision** = the sack is personal (your own carry limit and risk) and the lantern quota is shared. Nobody can take souls from you; banking helps everyone.
- Pickups are simulated on the server and sent as snapshots; no pickup is client-authoritative, so there is nothing to race.

## 3. Ghost shift (downed players)
- A downed reaper keeps moving (`DownedSpeed`, now 300) and still gathers XP and gold (`GhostCollect`), never souls.
- **F = Haunt:** marks the toughest enemy within `GhostHauntRange`; it takes `GhostHauntBonus` more damage for `GhostHauntSeconds`. Recharge `GhostHauntCooldown`. Bots haunt on their own.
- Revive paths: soul harvest near the ghost and skull carry (existing), kit revives (existing), and **new:** everyone down stands up when the lantern trail moves on (`GhostLanternRevive`).
- HUD: "GHOST SHIFT" panel with the haunt prompt / recharge.
- Not built: "faster revive when the ghost floats near the reviver". The existing revive is fed by souls reaped near the ghost, so drifting to the crew already speeds it up.

## 4. Pings
- Tap middle mouse (or click the left stick) = "Here" where you look. Hold = wheel: Help me, Lantern, Chest, Danger, Thanks (drag toward one, release).
- Shows a marker with the player's name, an edge arrow when off screen, a minimap blip, a short sound. One colour per player slot. Lasts 8 s, one live ping per player.
- Rate limit: 3 in hand, one back every `PingCooldown` s (server side).

## 5. Everyone keeps progress
Audited, already correct: at run end the server sends each player their own results (ClientMissionEnd, ClientQuestRunEnd, ClientQuestProgress) and each client saves banked gold, unlocks, quests and meta ranks to its own profile. No code change.

## 6. Level-up never freezes the squad
Audited, already correct: in co-op picks are per player, non-pausing, opened with TAB or 1/2/3, queued (PendingLevelUps), with auto-pick. Solo keeps the pause. No code change.

## Replication added
- ASQCharacter: replicated `SquadNear`, `GhostHauntCharge`; `ServerPing` (unreliable) and `MulticastPing` (unreliable); combo effect rides the existing MulticastFX (new kind 77).
- Combos, aura, ghost rules, chest share, revive rewards all run on the server only.
- Pings are kept locally on each machine (ASQGameState::LocalPings), not replicated state: late joiners do not see old pings.

## Tips (one-time, SQTutorial)
Ghost ("Down, not out!..."), Ping ("Tap the mouse wheel..."), Combo ("COMBO! ...").

## Untested (needs Unreal)
- Everything above compiles only in the cloud syntax harness.
- Ping wheel direction: assumes look input Y is positive = up. If Lantern / Danger feel mirrored, flip the sign in ASQPlayerController::GetPingWheelPick.
- Middle mouse may already be bound in the input mapping; check nothing else fires.
- Combo feel and balance: Scripts/balance_model.py does not model combos or Close Ranks (roughly +4 to 8% damage, 3 to 6% less damage taken, plus bursts). Watch boss time-to-kill; turn `ComboBurst` or `SquadAuraDamage` down if bosses die too fast.
- HUD positions of the COMBO callout, ghost panel and chips against the other HUD parts.
- The new tips add three ESQTip values at the end of the enum; saved "tips seen" are stored by name so old saves are fine.
