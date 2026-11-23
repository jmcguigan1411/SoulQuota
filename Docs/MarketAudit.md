# Soul Quota vs the category best sellers (market audit, 12 Nov 2026)

Jack's four questions: is it as enjoyable, does it make sense, is it simple, is it quick to play?
This audit reads the real game data and code, compares it with the best sellers, and records what was changed the same day.
Nothing here was play-tested by a person; timings come from the data, the code and `Scripts/balance_model.py`.

## 1. The best sellers

"As far as I know" marks facts from general knowledge rather than a source fetched for this audit.

| Game | Run length | Inputs | Systems in minute 1 | Reward cadence | Meta / unlocks | Praise | Complaints | Price |
|---|---|---|---|---|---|---|---|---|
| Megabonk | 10-minute limit per stage to find and summon the boss; 3 levels | move, jump, slide; auto-attack | move, kill, pick 1 of 3 | very generous early, slows later | characters by kill counts and challenges, 108 achievements | "brilliant dumb fun", movement physics, replay value, streamable gags | some unfair deaths, precise-platforming areas | $9.99 |
| Vampire Survivors | 30 minutes (15 on some stages), as far as I know | move only | move, collect gems, pick 1 of 3 | level-up every few seconds early | new unlock almost every run, visible unlock screen | "one more go", absurd power | repetitive late | about $5, as far as I know |
| Brotato | about 15 minutes, in short waves | move only | move, shop between waves | a shop every wave | characters and items by challenges | snappy, replayable | visual sameness | about $5, as far as I know |
| Deep Rock Galactic: Survivor | 15-30 minutes | move, mine | move, mine, pick upgrade | level-ups plus stage shop | slow meta grind | fun loop, charm, short sessions | grind, repetition, difficulty spikes, no co-op | fair range $6.50-$12.99 per reviews |
| Halls of Torment | 30 minutes, as far as I know | move, optional aim | move, pick trait | level-ups, item pickups | quests unlock items and characters | visual scale of builds | slower start | about $8, as far as I know |
| HoloCure | 20 minutes, as far as I know | move, optional aim | move, pick 1 of 4 | level-ups | gacha characters with coins | tight weapon design | fan-specific theme | free |
| The Spell Brigade (co-op) | not stated; "runs feel longer than they are" | move, spells auto | move, objectives on map | objectives during the run | gold buys upgrades and characters | fun with friends, spell combos | severe grind, weak solo, some objectives near impossible, slow movement | free-to-play per Choost |

What they share:

- **One input to start.** You move; the weapon fires itself. Megabonk adds a jump because movement is its hook.
- **A reward inside the first 20-30 seconds**, then one every few seconds.
- **One screen of rules.** Kill, collect, pick one of three. Everything else is discovered later.
- **Short or chunked sessions**: 10 minutes (Megabonk stage), about 15 (Brotato), 15-30 (DRG:S).
- **A new toy most runs**, on a screen that shows what is still locked.
- **Players forgive rough graphics for a tight, fun loop** (Steam analyst Chris Zukowski, via GamesRadar). They do not forgive grind (DRG:S, Spell Brigade) or slow movement (Spell Brigade).

## 2. Soul Quota, measured (before today's changes)

| Measure | Soul Quota | Genre norm |
|---|---|---|
| Clicks from desktop to moving | main menu, mission, reaper, lobby, press R, 3 s countdown, 1.6 s rise | 2-3 clicks |
| First run: time the horde is held back by the tutorial | up to 60 s | 0 |
| Spawn rate in minute 0 (solo) | 0.66 per second, 31% of what the player can kill | player is busy at once |
| First level-up | roughly 30-40 s after gaining control | 10-30 s |
| Stage length at release | 16-20 minutes plus a boss fight of about 3.5 minutes: 17-19 minute runs | 10-15 |
| Stage length Jack was actually testing | 4 minutes (test knob `NightSeconds` 90) | - |
| Objectives per run | 5-6, each with a fail timer that buffs the boss | 0-3 |
| First three runs (model) | three losses of 13 minutes each, +125 gold each | a visible unlock per run |
| First permanent upgrade | after 27 minutes | after run 1 |
| First win | about 59 minutes in | 20-40 minutes |
| Longest gap between rewards, first 10 hours | 41 minutes | under 15 |
| Tutorial | 8 training steps plus 13 one-time tip cards | 0-3 prompts |

Concepts a new player meets in the first two minutes: walk, cursor facing, auto-swing, **reap** glowing foes with left click, dash, level-up cards, souls, the soul sack limit, banking at the lantern, the lantern arrow, gold, chests with rising prices, pots, shrines, objectives with fail timers, Defiance stacks, the stage clock and acts, swarms, districts, the minimap, E and R abilities with level gates, augments, minibosses. That is over twenty. The best sellers ask for three.

## 3. Verdict on the four questions

**Is it as enjoyable?** Not yet, and I can't claim otherwise without anyone having played a full release-length run. The parts that the genre says matter are present: auto-attack, screen-filling hordes, 132 upgrades, evolutions, 21 distinct characters, co-op. What was missing is pace: the first minute was empty, rewards between runs were 13-41 minutes apart, and the first hour was three losses. Those are the things reviews punish.

**Does it make sense?** Partly. The fantasy is clear: you are Death's temp, reap souls, kill the boss. The rules are not: souls are both an objective currency, an ultimate charge and gold, with a carry limit and a place to bank them. A new player cannot tell XP, souls and gold apart in purpose within a minute, and Jack's own playtest confused souls with XP.

**Is it simple?** No. It has more systems in the first two minutes than any best seller has in its first hour. This is the largest gap.

**Is it quick?** It was not: 17-19 minute runs with a long menu-to-play path. After today's changes the first map is about 11 minutes and a loss costs about 8.

## 4. Changed today (data and flow only)

| Change | Before | After |
|---|---|---|
| Stage length (5 maps) | 960-1200 s | 600-840 s |
| Objectives per map | 5, 5, 6, 6, 6 | 3, 4, 4, 5, 5 |
| Boss health multiplier (so the fight stays short at the lower level) | 0.57-1.18 | 0.26-0.71 |
| Boss fight (model, normal) | about 210 s target | 150-225 s |
| Full run (model, normal, solo) | 17.1-20+ minutes | 11.0-16.7 minutes |
| Lost run, first attempts | 13.3 minutes | 8.4 minutes |
| Spawn rate at the start (solo) | 0.6 per second | 1.2 per second |
| Spawn rate at the early mark | 2 per second at 2 minutes | 3 per second at 1.5 minutes |
| Opening wave | none | 10 Shamblers per reaper about 4 s in ("Welcoming Committee") |
| Level at minute 1 (model) | 5 | 8 |
| Tutorial hold on the horde, first run | up to 60 s | up to 20 s |
| Rise-from-grave freeze | 1.6 s | 1.1 s |
| Lobby countdown, solo | 3 s | 1 s (`StartDelaySolo`; co-op keeps 3 s) |
| End screen before the lobby returns | 20 s | 8 s |
| First objective appears | 20 s in | 40 s in |
| Tip card lifetime | 10 s | 6 s |
| Gold banked from a loss | 10% of earned | 20% |
| First rank of the ten stat upgrades | 650-1400 gold | 260-560 gold (later ranks cost more: growth 2 to 3.3) |
| Field Medic / Second Opinion / Head Start / Pardon | 1500 / 1800 / 3500 / 15000 | 900 / 1000 / 2000 / 9000 |
| First permanent upgrade (model) | 27 minutes | 8 minutes, after run 1 |
| First win (model) | attempt 4, 59 minutes | attempt 3, 28 minutes |
| Upgrades bought in hour 1 (model) | 2 | 8 |
| Test knob `NightSeconds` | 90 (4-minute stages) | 360 (what you play is the release length) |

Model flags after the change (not fixed):

- Kit balance: ten reapers now sit outside the +-12% band (five before). The shorter run changes who peaks when, and the model is noisy here.
- Late hits-to-kill on normal reach 5.6 in 2-player (band 2-5).
- Boss time-to-kill is 0.65-1.08 of the old 210 s target. This is deliberate: the fights are shorter.
- The longest reward gap in the first 10 hours is 62 minutes (41 before), at the Plague Carnival wall, even though the first hour is far better. Map 3 needs a difficulty or reward look.
- All Hard modes take 12 hours (32 before); everything takes 31 hours (42 before).

## 5. Recommended, not done (needs Jack's call or another thread's files)

Ranked by impact.

1. **Cut the soul sack and banking from the first map.** Souls would bank themselves on pickup on the Graveyard; the lantern and carry limit arrive on map 2 as a new rule. Jack asked for souls, XP and gold to drop, and they still would. This removes four concepts from minute one. Needs `SQCharacter` (camera thread's file) or a tuning key there.
2. **One objective type on the first run.** The Graveyard now has three; the ideal first run is "survive, then ring the bell", with objectives unlocking after the first clear.
3. **A RETRY button on the defeat screen** that restarts the same mission with the same reaper, beside CHANGE REAPER. Today the end screen times out to the lobby and you press R again. Needs `SQHUD` and `SQPlayerController`.
4. **Solo: skip the lobby.** Pick the reaper in the main menu and drop straight in. The lobby stays for co-op.
5. **Replace the 8-step Training Shift with three prompts** (move, reap, pick a card) and let the other thirteen tips appear only when the thing is first used. Needs a rewrite of the tip tables in `SQTutorial`; left alone because the camera change is about to alter controls and tip wording.
6. **An unlock screen** in the main menu showing every locked reaper, item and map with its condition. The 109 quests already grant these; players can't see what is next.
7. **Merge gold and souls?** Three pickup currencies is one more than any best seller. Not recommended unless playtests still show confusion after item 1.
8. **Price:** the plan says $7.99. The category sits at $5-$10; with co-op that is defensible once the look improves.

## 6. Sources

- [PC Gamer: Megabonk review](https://www.pcgamer.com/games/roguelike/megabonk-review)
- [PC Gamer: Megabonk sells a million in two weeks](https://www.pcgamer.com/games/roguelike/vampire-survivors-like-megabonk-makes-megabucks-selling-a-million-copies-in-2-weeks-and-currently-has-more-daily-players-than-borderlands-4-on-steam-ill-be-eating-spaghetti-with-extra-sauce-tonight)
- [PC Gamer: Megabonk as a slot machine for gags](https://www.pcgamer.com/games/roguelike/a-slot-machine-for-gags-disguised-as-a-vampire-survivors-like-is-taking-over-steam-and-im-just-as-addicted-to-pulling-the-lever-as-the-other-90k-people-playing-it)
- [GamesRadar: rough-but-fun games, "Steam players want fun first"](https://www.gamesradar.com/games/steam-expert-hails-pc-gaming-golden-age-of-rough-but-fun-games-that-devs-can-make-quickly-and-players-love-from-peak-to-megabonk-steam-players-want-fun-first)
- [TweakTown: Megabonk price and player counts](https://www.tweaktown.com/news/108086/steams-new-big-game-attracts-100000-plus-players-who-want-to-grow-absurdly-powerful/index.html)
- [Summer Engine: games like Vampire Survivors (Brotato run length)](https://www.summerengine.com/blog/games-like-vampire-survivors)
- [Vaporlens: Deep Rock Galactic: Survivor review summary](https://vaporlens.app/app/2321470/deep_rock_galactic_survivor.md)
- [TheSixthAxis: The Spell Brigade review](https://www.thesixthaxis.com/2026/04/28/the-spell-brigade-review-a-co-op-survivors-like-thats-all-about-the-grind/)
- [Choost: best survivors-like games 2026](https://choostgames.com/blog/best-survivors-like-games/)
- [The Conversation: Vampire Survivors and reward psychology](https://theconversation.com/vampire-survivors-how-developers-used-gambling-psychology-to-create-a-bafta-winning-game-203613)
- [Birdor: Vampire Survivors low-friction case study](https://birdor-blog.pages.dev/vampire-survivors-low-friction-success-case-study/)
- [feedme.design: why roguelites keep us playing](https://www.feedme.design/why-we-cant-stop-playing-these-roguelites)
