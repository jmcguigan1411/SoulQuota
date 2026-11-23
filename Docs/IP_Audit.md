# Soul Quota - IP / Infringement Audit (7 Nov 2026)

Scope: every player-visible name and text in `Content/Data/*.csv`, the player-visible `TEXT("...")` strings in `Source/SoulQuota/*.cpp` (HUD, menus, credits, messages), `Docs/*.md`, `README.md`, `CLAUDE.md`, art, fonts, audio and the title.
**This is not legal advice.** The audit flags risk conservatively. A trademark search by a professional is still needed before release (see the end of this file).
Backups of every edited file: `sq_backups/ip_audit_pre/` (cloud copy).

Verdicts: **OK** = no action. **Renamed** = changed in this audit. **Flag** = low risk, kept, Jack decides.

## 1. Renames and rewrites made

Internal Ids are unchanged, so code, saves and Bonds still work. Ids are never shown to players.

| File | Was (player-visible) | Now | Reason |
|---|---|---|---|
| Reapers.csv (Id `Mort`) | Swift the Courier / "Signs for every parcel." | **Swift the Hauler** / "Travels light; hauls heavy." | Courier/parcel = delivery wording (Highly Incompetent). The name Mort was already gone from display (Discworld). The internal Id `Mort` is only shown in the dev console command `SQReaper Mort` |
| Upgrades.csv (Id `SixFeetUnder`) | Six Feet Under | **Six Feet Down** | Exact title of the HBO series (title/brand). Idiom-level risk only, renamed to be safe |
| Upgrades.csv `Sunrise` text | "Night shift is officially over." | "Rise and shine, horde." | "night shift" = Highly Incompetent wording |
| Bosses.csv (Id `Gravewarden`) | The Gravewarden | **The Gravekeeper** | Close to Elden Ring's "Grave Warden Duelist" (FromSoftware) |
| Bosses.csv `Ringmaster` intro | "...and the recently deceased - welcome!" | "...and the freshly dead - welcome!" | Echoes Beetlejuice's "Handbook for the Recently Deceased" |
| Bosses.csv `Merchant` contract | "...Death takes a day off." | "...Death gets a quiet night." | Too close to the film/play title *Death Takes a Holiday* |
| Enemies.csv (Id `Spitter`) | Bile Spitter | **Bile Gargler** | "Spitter" plus bile reads as Left 4 Dead's special infected (Spitter, Boomer bile) |
| Bonds.csv (Id `WorkshopOvertime`) | Workshop Overtime | **Assembly Line** | "Overtime" = Highly Incompetent wording |
| Shop.csv (Id `Overtime`) | Overtime Training | **Crash Course** | Same |
| Story.csv `Memo1` | "That is your overtime." | "That is now your problem." | Same |
| Story.csv `Win` | "Shift complete... Clock in tomorrow." | "Quota complete... Same time tomorrow." | "clock in" is banned wording |
| Biomes.csv `Graveyard` | "Where every shift begins." | "Where every reaping begins." | Shift wording |
| Tuning.csv notes (dev only) | "Overtime pay..." / "Pay for finishing a story shift" | "Soul coins..." | Consistency (not player-visible) |
| SQGameMode.cpp | "SHIFT COMPLETE!", "Mode: ENDLESS OVERTIME", "Mode: STORY SHIFT" | "QUOTA COMPLETE!", "Mode: ENDLESS", "Mode: STORY" | Highly Incompetent wording |
| SQHUD.cpp | "Every soul delivered." | "Every soul banked." | Delivery wording |
| SQMenu.cpp | "ON THE PAYROLL", "HIRE FOR %d PAY", "NEED %d PAY" | "%d OF %d HIRED", "HIRE FOR %d COINS", "NEED %d COINS" | Meta currency must read as soul coins |
| SQMenu.cpp credits | "Made with Unreal Engine" + copyright line | "Soul Quota uses Unreal® Engine." + trademark line (2 lines) + copyright line | Exact EULA credit wording (section 4) |
| ContentPlan.md / GameDesign.md (planned names) | Good Boy, The Night Watchman, The Auditor, The Headless Rider, Old Tock, Weeping Statue, Crow Mother, The Combine, Team Spirit, Clock-In Machine, Golden Clock-In, Unpaid Overtime Shrine, Overtime Timesheet, Overtime Urns, Night Shift Supervisor, "Night Shift Expansion", Shift Grades, Overtime mode/pay | Femur the Hound, The Graveyard Lookout, The Actuary, Sir Neckless, Old Tickwell (shipped names); Mourning Statue, Crow Matron, The Thresher, Esprit de Corpse, Signing Desk, Golden Signing Desk, Greed Ledger, Soul Coin Purse, Coin Urns, The Floor Supervisor, "Rooftops Update", Quota Grades, Endless / soul coins | Good Boy = 2025 dog horror film title; Night Watch/Auditors = Discworld; Headless Rider = *Durarara!!* (anime) character; Tock = *The Phantom Tollbooth*; Weeping Angels = Doctor Who; Crow Mother = Hopi kachina (cultural); Combine = Half-Life; Team Spirit = esports brand; the rest = Highly Incompetent wording |
| README_Reaper.md | "a small reaper on the night shift" | "a small reaper on the job" | Wording |

## 2. Everything checked

### Reapers (Reapers.csv)
| Item | Verdict | Reason |
|---|---|---|
| Grim | Flag (low) | Generic nickname for the Grim Reaper. Note: Cartoon Network has a "Grim" in *The Grim Adventures of Billy & Mandy*; ours is a different design. Keep |
| Swift the Hauler (Id Mort) | Renamed | See above |
| The Wailing Widow, Bonesmith, The Lantern Keeper, The Hollow Monk, The Intern, Femur the Hound, Digby Sixfeet, The Debt Collector, Tallow, The Graveyard Lookout, Vexa the Veiled, The Actuary, The Chimney Sweep, Sister Solace, The Pallbearers, Madame Ectoplasm, Old Tickwell | OK | Original or generic. No known character matches |
| Doctor Beak | OK | Generic plague-doctor archetype |
| Sir Neckless | OK | Original pun on the public-domain Headless Horseman. The "Sir X" pattern is also Megabonk's (Sir Oofie, Sir Chadwell), but it is a common English title |
| Passive texts ("Who's a good reaper?", "Do not RSVP", "Burns at both ends"...) | OK | Common idioms |
| Unlock hint "Complete 30 Performance Reviews" | OK | Generic office satire |

### Weapons / relics / evolutions (Upgrades.csv, 70 rows)
| Item | Verdict | Reason |
|---|---|---|
| All 24 weapons (Scythe Sweep ... Gargoyle Perch) | OK | Generic folklore and office nouns. No match with Vampire Survivors (Holy Water, King Bible, Garlic, Runetracer...), Megabonk (Bonk, Bananarang, Aegis, Dexecutioner...), Brotato, Halls of Torment or Hades |
| Grave Bells text "Ask not for whom" / Bell Ringer "The bell tolls for YOU" | OK | John Donne, 1624: public domain. Not Metallica's lyrics or Hemingway's text |
| All 22 relics | OK | Charon is Greek myth (public domain). "Vampire Fang", "Golden Key", "Hourglass" are generic. No tome-style names ("X Tome") |
| Reaping Moon | OK | Already renamed from "Harvest Moon" (Natsume trademark) |
| Spiked Coffin Lid | OK | Already renamed from "Iron Maiden" (band trademark) |
| Six Feet Down | Renamed | See section 1 |
| Requiem, Ossuary, Mausoleum, Permafrost, Black Death, Sunrise, Last Rites, Murder of Crows, Soul Conduit, Final Demand, Denied in Triplicate, Briar Crown, Poltergeist Flood, Ancestor Shrine, Grand Seance, Thousand-Candle Vigil, Earthen Maw, Banshee Choir, Ferryman's Sickles, Rosary of Ages, Gargoyle Roost | OK | Dictionary words, history or folklore. "Poltergeist" is a common German-origin noun (the film title does not own the word). "Murder of Crows" is the standard collective noun |
| "Heavy is the head" (Briar Crown) | OK | Common paraphrase of Shakespeare (public domain) |

### Bonds (Bonds.csv)
| Item | Verdict | Reason |
|---|---|---|
| Grim Rhythm, Ossified, Pack Mules, Scattered Ashes, Pallbearers, Undying, The Accountants, Spiked Coffin, Second Opinion, Beginner's Luck, Tea and Sympathy | OK | Idioms and dictionary words ("Undying" and "Tea and Sympathy" are also titles, but titles of single works are not protected and these are everyday phrases) |
| Assembly Line | Renamed | See section 1 |

### Bosses (Bosses.csv)
| Item | Verdict | Reason |
|---|---|---|
| The Bell Ringer, The Sexton, The Ringmaster, The Abbot Who Would Not Kneel, The Merchant of Hours | OK | Generic roles. Note: The Sims 4: Life & Death has a reaper career with a "soul quota" and a "Mysterious Merchant" NPC; "merchant" is generic and our Merchant of Hours is a different character |
| The Drowned King | OK | Search found no game boss with this exact name (closest: Salt and Sacrifice's "Drowned Behemoth", Diablo IV "Drowned Seahag") |
| The Gravekeeper | Renamed | See section 1 |
| Intro lines and contract texts | OK after 2 rewrites | No lyrics or film quotes. Ringmaster and Merchant lines were rewritten |

### Enemies (Enemies.csv)
| Item | Verdict | Reason |
|---|---|---|
| Shambler | Flag (low) | Also a Quake monster, but "shambler" is a generic zombie word used across the genre |
| Lurcher, Ghoul, Soul Wisp, Gravedigger, Bone Hound, Powder Corpse, Bloated Mourner, Grave Brute | OK | Generic. Internal Id `Bloat` is not shown (The Last of Us "Bloater") |
| Bile Gargler | Renamed | See section 1 |

### Biomes, Story, Shop
| Item | Verdict | Reason |
|---|---|---|
| The Overflowing Graveyard, The Drowned Harbour, The Plague Carnival, The Frozen Monastery | OK | Descriptive. Megabonk also has a "Graveyard" map, but the word is generic |
| The Clocktower | Flag (low) | "Clock Tower" is also a horror game series title. As a place name it is descriptive; keep |
| Gimmick texts (TIDE, SPOTLIGHTS, ICE, TIME STOPS) | OK | Time-stop is a mechanic (Megabonk's "Za Warudo" item is a JoJo reference; we use no such name). Game mechanics are not protected by copyright |
| Story memos (speaker "Death") | OK after rewrites | Death as a sarcastic manager is a generic personification. Uses caps only for single-word emphasis, not Discworld-style all-caps speech. No Mort, Auditors, Night Watch or "THE DEAD"-style speech |
| Mend Bones, Bigger Sacks, Death's Pardon, Crash Course | OK | |

### C++ player-visible strings (HUD, menu, credits, messages, about 330 strings)
| Item | Verdict | Reason |
|---|---|---|
| Title/menu taglines ("THE DEAD WON'T COLLECT THEMSELVES.", "START REAPING", "LET'S REAP", "RAISING THE DEAD...") | OK | Original. The caps come from the UI style, not Discworld's Death |
| "YOU DIED"-style defeat text | OK | None present. Defeat lines are "QUOTA FAILED" and "The whole crew is skulls..." |
| "FINAL CALL" | OK | Not Megabonk's "Final Swarm" |
| "SOUL LEDGER · FORM 13-B", "PERSONNEL FILE", "DEATH'S OFFICE" | OK | Original office satire |
| Shift/overtime/pay/delivered strings | Renamed | See section 1 |
| Credits: "with help from Claude (Anthropic)" | OK | Factual (nominative) use of a name |
| Credits: font and KayKit lines | OK | Match the licence holders |

### Docs, README, CLAUDE.md
| Item | Verdict | Reason |
|---|---|---|
| MegabonkAnalysis.md | OK (internal) | Research only. No Megabonk names leaked into the CSVs or C++ (checked: Megachad, Monke, Roberto, Bonk, Sir Oofie, CL4NK, Silver, Tome, Final Swarm, Shady Guy, Moai) |
| ContentPlan.md planned Keepsakes | Flag (design) | Several copy Megabonk item **mechanics** 1:1: Stopped Clock = Za Warudo, The Big Snip = Big Bonk (2% for 20x), Death's Spare Hood = Overpowered Lamp, Bag of Marbles = Backpack, Golden Urn = Pot, Pawnbroker hat colour = Shady Guy, Photocopier = Microwave. Mechanics are not copyrightable, but copying this many 1:1 invites "clone" reviews. Change the numbers or twists before shipping |
| ContentPlan.md "World's Okayest Reaper", "Employee of the Month", "Hello, I'm Dead" | OK | Common phrases |
| CLAUDE.md decisions log, README dev console (`SQReaper Mort`) | OK (internal) | Not shipped to players |
| README_Reaper.md "punch-card name badge" on the player model | Flag | Not IP. The model has a time-card style badge, which is Highly Incompetent imagery. Consider a soul-coin badge in a later art pass |

## 3. Assets and licences

| Asset | Licence | Status |
|---|---|---|
| KayKit Character Pack: Skeletons 1.0 (Kay Lousberg) | CC0 1.0. Commercial use OK, credit not required | Licence file present on the device (`ArtSource/LICENSE_KayKit_Skeletons.txt`), now also copied to `Docs/Licenses/`. Credited in README and the game credits |
| KayKit: Halloween Bits 1.0 | CC0 1.0 | Same (`LICENSE_KayKit_HalloweenBits.txt`). Device `ArtSource/Props` has 64 FBX files, all KayKit names |
| Reaper, Junior Grade (mesh, scythe, clipboard, 6 animations) | Original work | KayKit rig plus 95 KayKit animations (CC0) |
| Creepster (Font Diner, Inc.), Lilita One (Juan Montoreano), Rubik (The Rubik Project Authors) | SIL OFL 1.1 | The OFL lets you bundle and embed the fonts in commercial software or games, as long as the fonts are not sold on their own and the copyright notice and licence travel with them. Reserved Font Names only matter if you **modify** a font; ours are imported unmodified as UE font faces. OFL files are now in `Docs/Licenses/`. **Must ship with the game** (see the to-do list) |
| Audio (12 SFX + music loop) | Original | `Scripts/create_audio.py` only makes sine, noise and bell partials. No downloads or samples. The device's `Content/Audio` holds exactly those 12 generated assets. Melodies are plain arpeggios and scales |
| Unreal Engine 5.8 | UE EULA | 5% royalty on lifetime gross revenue above $1M per product, paid regardless of who collects it; Epic Games Store sales are royalty-free; send the Release Notification Form after shipping and report royalties quarterly ([unrealengine.com/license](https://www.unrealengine.com/en-US/license)). If the product has credits, they must contain: "[Product] uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere. Unreal® Engine, Copyright 1998 – xxxx, Epic Games, Inc. All rights reserved." ([EULA clause summary](https://conductatlas.com/platform/unreal-engine/unreal-engine-eula/provision/CA-P-059503/required-product-credits-attribution-to-unreal-engine/), [UE forum](https://forums.unrealengine.com/t/do-i-have-to-put-the-unreal-engine-intro-in-my-game/664131)). The splash/logo is **not** required; if you use the logo, follow Epic's branding guidelines. Credits now use this exact wording |
| Blender | GPL (tool only) | Files you make with Blender are not covered by the GPL |

New files: `Docs/Licenses/` (3 OFL texts, 2 KayKit licences, `THIRD_PARTY_NOTICES.md`).

## 4. Title search: "Soul Quota" (7 Nov 2026)

| Finding | Risk |
|---|---|
| **"SOUL QUOTA by ZARCK5678" on itch.io** (https://zarck5678.itch.io/soul): an existing small itch.io project with the **exact same title**. The page could not be opened from here (blocked), so the genre and date are unknown | **Medium**. Unregistered and probably tiny, but it is a prior use of the identical name. Check it yourself |
| "reapers quota" on itch.io (https://jake-hub.itch.io/reapers-quota) | Low-medium. Similar reaper + quota concept and name |
| "soul ledger" (https://idfc-studios.itch.io/soul-ledger), "DEADline Reaper" (https://samci.itch.io/deadline-reaper), "The Grave Shift" (https://zephyreds.itch.io/the-grave-shift), "deaths debt" (https://odalawa.itch.io/deaths-debt) | Low. Same theme, different names; shows the reaper-at-work premise is crowded |
| The Sims 4: Life & Death (EA, 2024) reaper career uses a "soul quota" mechanic (e.g. [GameRant](https://gamerant.com/the-sims-4-grim-reaper-active-career-guide/), [X post](https://x.com/onlyabidoang/status/1852538574664798381)) | Low. Used as a phrase, not a product name |
| No Steam game, and no trademark found in web results, named "Soul Quota" | A web search cannot clear a trademark. Registers must be searched directly |

## 5. What Jack must do before release
1. **Trademark search for "Soul Quota"** (classes 9 and 41, video games / entertainment): UK IPO (https://trademarks.ipo.gov.uk), USPTO (https://tmsearch.uspto.gov), EUIPO eSearch / TMview (https://www.tmdn.org/tmview). Also search the Steam store and itch.io. Open https://zarck5678.itch.io/soul and decide whether to keep the name, contact the developer, or pick an alternative. Consider registering the mark (UK first) once you are sure.
2. **Ship the licence texts:** copy `Docs/Licenses/` into the packaged build next to the exe (or move it under `Content/` and add it to Project Settings > Packaging > "Additional Non-Asset Directories to Package"). The credits say "The licence texts come with the game", so this must be true.
3. **Credits year:** change "1998 - 2026" to the release year in `SQMenu.cpp` credits and `THIRD_PARTY_NOTICES.md`.
4. **UE royalties:** after launch, send Epic's Release Notification Form. Royalties start only above $1M lifetime gross (reported quarterly).
5. **Megabonk clone risk (design, not legal):** change the 1:1 item mechanics listed in section 2 before Keepsakes ship.
6. **Professional check (recommended before the Steam page goes up):** a short trademark clearance by a UK IP attorney or solicitor. Also an optional review of the Steam store page text and trailer.
7. Keep this rule for new content: no names from other games, films, books, TV, anime or brands, and no "clock in / time card / night shift / overtime / delivery" wording. Re-run this audit for each content update.
