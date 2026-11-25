# Content pass (findings 10 to 13), Nov 24 2026

Nothing here has been compiled or played in Unreal. Checked only with the cloud syntax harness.

## Status
- 10 The Long Haunt: DONE (commit b69d667).
- 11 Unlock pacing: DONE (see below).
- 13 Pricing note: DONE, Docs/Pricing.md (commit e41638f).
- 12 Events and modifiers: DONE in code (commits e1c2824, 51ca8a2 and the docs commit after them). Built / not built listed in section 12.

## 10. The Long Haunt (optional loops after the boss)

> Nov 25 2026: switched off (Tuning EndlessLoops 0). The 3-stage run chain and the endless horde after the last boss replace it (Docs/RunStructure.md); its quests were retired.
- After the boss the host (or solo player) chooses CASH OUT or KEEP REAPING. Host decides; there is no vote.
- A loop relights the lantern trail on a new seeded route, ramps health, damage, elites and Difficulty, and brings the boss back with a cycling twist.
- Rewards lock each time a loop's boss falls; dying later keeps what was locked.
- Best loop saved per map, per reaper and for Hard (USQSaveGame LoopBest*). Leaderboard call is a stub in SQSession.
- Tuning: Endless* keys. Quests: 4 loop quests plus HauntFirst.

## 11. Unlock pacing
- Death's Ledger: every finished run (win or lose) gives at least one profile rank. A rank pays 25 banked gold, every 5th pays 100 more. No stats. Tuning Ledger* keys; LedgerRanks 0 turns it off. Save: LedgerXP, LedgerRank.
- 66 new quests fill the gaps in the ladders (kills, objectives, wins, chests, augments, bosses, banked gold, minibosses, lanterns, elites, events, gold earned, runs played) and add first-time milestones: CoopFirst, ComboFirst, HauntFirst, Events1, Runs2/4/6/8 (FirstBoss and QuotaMet1 already existed).
- Cosmetics.csv: 12 lantern trail colours (your own minimap trail marker and guide line), earned from quests, never bought. Equip on the UNLOCKS page with T (pad Y). Save: CosmeticsOwned, CosmeticsEquipped. Local to your screen, nothing replicated.
- NEXT UNLOCK panel on the main menu reaper card (closest unfinished quest, reward, progress bar). The end screen already had the unlock strip (Nov 12).
- UNLOCKS page header shows the Ledger rank.
- Model (Scripts/balance_model.py section 27, three maps): first 10 runs with nothing new (quest, hire or upgrade): median 0, worst seed 2. Counting Ledger ranks, the longest gap in the first 10 hours is 13 minutes. Without the Ledger the longest gap is 29 minutes median (39 worst). First clear on attempt 3 at 32 minutes. All Hard modes 15 h, everything 44 h.
- Not done: reaper palette and ping style cosmetics (no cheap hook: palettes are baked textures; ping colour is per player slot). Meta upgrade caps were left as they are (50 ranks total).

## 12. Run events, district conditions, Daily Haunt, clause unlocks
All data lives in `Content/Data/Events.csv` (one row per event or condition; column notes at the top of the file).

### How it works
- `ASQGameMode::PlanEvents` runs at mission start and at every Long Haunt loop. It spreads EventsPerRun (3; +EventLoopExtra per loop) start points between EventFirstAt and EventLastAt of the stage clock, seeded by the run seed.
- `ASQGameMode::TickEvents` (server) starts the next event when its point is reached: weighted pick from the map's pool, unused events first. One event at a time. Events stop when the Final Swarm starts or the bell is ready.
- Three kinds. Ambient: map-wide multipliers for a time. Hunt: a named miniboss (the horde's existing miniboss slot), done when it dies. Site: `ASQEventSite` (new actor) at a clear spot found with the shrine placement rules; reapers stand in the ring until the hold bar fills.
- Replicated on ASQGameState: EventId, EventPos, EventStartTime / EventEndTime, EventHoldPct, EventsDone, ClockRate / ClockOffset, ConditionIds / ConditionDistricts, bDailyRun. Clients read the rest from Events.csv.
- Announce: banner (EVENT in the event's colour, kind 8; EVENT COMPLETE, kind 9), a tracker box under the miniboss bar (name, time left, hold bar or hint), a pulsing cross on the minimap (pinned to the border when off the corner map), name on the full map.
- Every finished event credits the quest stat EventsDone to every human (the ladder from item 11).

### The events (12)
| Event | Kind | Map | What it does | Look |
|---|---|---|---|---|
| The Tithe Caravan | Hunt | any | Grave Brute x2.5 HP with 8 Lurchers; free chest per reaper when it dies; 80 s | existing miniboss bar and models |
| The Gilt Scuttler | Hunt | any | fast Wick Runner x14 HP; 120 gold each when caught; 45 s | NEEDS NEW ART (see below) |
| Soul Storm | Ambient | any | souls x2 for 40 s | banner + tracker only |
| Blood Moon | Ambient | any | horde x1.35, elites x2.5, XP x1.5 for 60 s | banner + tracker only (red sky wanted) |
| Rolling Fog Bank | Ambient | any | corner minimap blind, XP x1.3 for 50 s | grey minimap cover (world fog wanted) |
| The Cursed Chest | Site | any | 3 s hold; free chest per reaper, then 14 Ghouls burst out | coffin_decorated prop, purple tint, gold ring |
| The Wandering Reliquary | Site | any | 2.5 s hold; +15% damage for 90 s and 40 gold each | shrine_candles prop, cyan |
| The Tolling Totem | Site | any | horde x1.5 until broken (4 s hold) or 75 s; XP and 50 gold | pillar prop, orange |
| The Trapped Spirit | Site | any | 3.5 s hold; "ally": +20% damage for 60 s for every reaper | gravestone prop, pale blue |
| King Tide | Ambient | Harbour | water climbs KingTideRise (120 cm) above normal high tide for 45 s, eased in and out; souls and gold x1.5 | existing tide water |
| The Clock Runs Fast | Ambient | Clocktower | hands at ClockFastRate (x2) for 40 s; XP x1.3 | existing clock hands |
| The Clock Runs Backwards | Ambient | Clocktower | hands reverse for 40 s; XP x1.3 | existing clock hands |

Site looks use existing props (primitive plinth + orb fallback), the shared interact ring, and existing SQFx recipes (shockwave + burst on arrival, flash + shockwave + rising burst on completion). The site removes itself 3 s after the event ends.

### Needs new art (placeholders in use)
- The Gilt Scuttler: original loot-runner design (a hunched crab-like bone creature with a coin-crusted shell). Today it is a re-used Wick Runner with a name and a health bar. It also does not flee yet: it chases like any enemy. A flee behaviour is a new horde behaviour (not built).
- The Trapped Spirit: no visible companion. The "ally" is a timed damage boost. A following spirit needs a model and a summon behaviour.
- Blood Moon and Soul Storm: no sky or weather change yet. Fog Bank: no world fog, only the minimap cover.
- Wandering Reliquary: it does not sell anything. It gives a blessing and gold.

### District conditions
- DistrictConditions (2) per run, seeded, never on the boss district. Three in the file: Gilded Ground (gold x1.6), Hallowed Ground (XP x1.3), Soul-Thick Ground (souls x1.4). Applied to kill drops inside that district (`ASQGameState::DropMult`).
- Shown as a hollow coloured square on the minimap, with the name on the full map. Not shown anywhere else yet (the ENTERING banner does not name the condition).

### Daily Haunt (local)
- Lobby, host: Daily Haunt row in the CONTRACT CLAUSES panel (click, Y, or LS click on a pad). Uses today's local date as the seed for the map, events and conditions on the chosen mission.
- First daily finished each day pays DailyGoldBonus (40). Save: DailyLastKey, DailyBestKills, DailyRuns. Quests: Daily1, Daily7. No online board; Steam later.
- Co-op: the host's date decides the seed; each player's own date decides their bonus.

### Clause unlocks ("pre-run toggles unlocked through play")
- Modifiers.csv has a new Unlock column (Stat:Target against the host's lifetime quest totals). All 9 clauses now unlock through play (1 to 6 wins, 3 boss kills, 40 chests, 150 elite kills). Locked rows are greyed with progress; the server refuses a locked clause.
- THIS CHANGES EXISTING BEHAVIOUR: clauses used to be open from the start. Blank the Unlock cells to open them again.

### Rules respected
- Bots: count for site holds; they do not walk to events on purpose. They get gold and boosts like humans. Quest credit skips bots.
- Co-op: all rules on the server; state replicated on the game state; the site actor is replicated.
- Lantern trail: event sites use the shrine placement rules (clear of the lit lantern, objectives, roads, hub, boss arena).
- The Long Haunt: events are re-planned every loop with one more per loop.
- Floor rule: a site is a standing prop that removes itself; rewards are pickups, chests or direct gold.
- Performance governor: Blood Moon and the Totem raise the spawn rate only; the on-screen cap and hard cap still bound the horde. Effects use SQFx, which the governor already scales.

### Tuning keys
EventsPerRun, EventFirstAt, EventLastAt, EventSiteRadius, EventLoopExtra, EventAllyDamage, DistrictConditions, DailyGoldBonus, KingTideRise, ClockFastRate.

### Model (Scripts/balance_model.py section 28)
- 2.3 event minutes per mission. Run-average multipliers: horde x1.01 to x1.02, XP x1.02, souls x1.02 to x1.035, gold up to x1.015. Events are variety, not economy.
- Unlock pacing unchanged by item 12: first 10 runs with nothing new median 0 (worst 2); longest gap in the first 10 h 13 min with Ledger ranks; 168 quests.

### Not tested without Unreal
- Everything. Specific risks: ASQEventSite replication and visuals; site spot quality on each map; the Hunt sharing the single miniboss slot with timeline minibosses (an event is skipped if a miniboss is alive for too long); King Tide flooding and bots; clock hand continuity when the rate changes; minimap marks; clause lock display; the Daily seed giving the same map on two machines.
