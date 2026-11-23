# The Lantern Trail (Nov 12 2026)

Why: play-test screenshots showed the whole run being fought on the hub plaza round one lantern. Jack: "players are forced to
play around the lantern system too much. There's very little map exploration... multiple inactive lantern locations which become
active after you've banked enough souls. This way it feels like you're progressing through the map whilst the enemies can become
stronger."

## Rules (what the player sees)

1. Every map has several lantern sites: the hub ("Lantern Square") and one in each district. Exactly ONE is lit.
2. The lit lantern wants a soul quota. Stand in its ring and your sack pours in (0.7 s).
3. Fill it and it is spent: free chest for every reaper (the last lantern gives an augment pick), a little gold, and the next
   lantern lights in another district. A tall beam, an on-screen arrow with the distance, a line on the minimap and footlights
   along the lanes show the way.
4. Every lantern behind you makes every sack bigger, and the horde stronger for the rest of the run.
5. Each lantern has its share of the stage clock. If the quota is not banked in time it burns out: no reward, the next one
   lights anyway. Half of what was banked carries over.
6. The last lantern stands in the district beside the boss arena. Trail walked + objectives resolved = the Appointment Bell rises
   there. The stage clock running out still forces the Final Swarm and the bell, as before.
7. Everything else is unchanged: enemies drop souls, XP and gold; souls banked still turn into gold; nests, elites, escorts,
   shrines, pots, chests, minibosses and acts work as they did. Banking is no longer an objective row of its own.

## Defaults (Content/Data/Tuning.csv, all `Lantern*`)

| Key | Value | Meaning |
|---|---|---|
| LanternTrail | 1 | 0 = the old single hub lantern |
| LanternStages | 4 | hub + 3 districts (capped by the map's districts) |
| LanternQuota / Growth | 200 / 1.5 | 4 reapers: 200, 500, 800, 1100. Solo (x0.5): 100, 250, 400, 550 |
| LanternQuotaRefSeconds | 600 | longer maps ask more (Duration / 600) |
| LanternSackGrowth | 1.5 | a 50 sack becomes 50 / 125 / 200 / 275: about two sacks a lantern |
| LanternDeadlineShare | 0.94 | lantern k burns out at k/4 x 94% of the stage (2:21, 4:42, 7:03, 9:24 on a 10-minute map) |
| LanternMinSeconds | 45 | a lantern lit late still burns this long |
| LanternDifficulty | 0.06 | Difficulty added per lantern (spawn rate, gold, some enemy HP) |
| LanternGold | 20 | gold each when a lantern is banked |
| LanternFinalAugment | 1 | last lantern: augment pick instead of chests |
| BankSeconds | 0.7 | was 1.5 |
| LanternBeam* / LanternFootlight* | | look of the beam and footlights |

Route: `ASQArena::GetLanternTrail` - hub first; the first hop may be any district, later hops take one of the two farthest
districts left (rolled from the run seed); the final site is the district nearest the boss arena. A district with no clear flat
spot is skipped.

## Model (Scripts/balance_model.py section 18b, release length, seed 0)

| Map | Solo: lanterns banked at (min) | Bell | 4 reapers | Bell |
|---|---|---|---|---|
| Graveyard (10 min) | 1.2, 2.8, 4.9, 7.3 | 7.8 | 0.9, 2.4, 4.2, 6.0 | 6.4 |
| Drowned Harbour | 1.2, 2.8, 4.9, 7.1 | 8.4 | 0.9, 2.5, 4.2, 6.2 | 8.0 |
| Plague Carnival | 1.2, 2.9, 5.0, 7.3 | 8.5 | 1.0, 2.7, 4.3, 6.5 | 8.3 |
| Frozen Monastery | 1.2, 3.0, 5.2, 7.6 | 11.5 | 1.0, 2.7, 4.2, 6.6 | 10.9 |
| Clocktower | 1.2, 3.1, 5.2, 7.8 | 12.7 | 1.0, 2.8, 4.4, 6.8 | 12.1 |

Graveyard solo run: 11.2 min (target 10 to 12). The model's soul income is an assumption (0.55 souls banked per kill): retune
`LanternQuota` after play-tests. Flag: the extra chests / augment make boss fights shorter (boss TTK / target 0.56 to 1.10).

## Where it lives

- `FSQMissionDirector` (SQMission.*): `BeginTrail`, `TickTrail`, `FinishTrailStage`, `LightTrailStage`. Counts what is banked via
  `ASQGameState::Banked`, moves `ASQGameState::Lantern` (the lit lantern is always that one actor, so the horde, hazards, bots,
  revives and banking follow it without changes). Adds a `LanternTrail` row at the END of `ASQGameState::Objectives`.
- `ASQGameState`: `TrailSites`, `TrailStage`, `TrailMissedMask`, `TrailBanked`, `TrailQuota` (replicated).
- `ASQLantern`: beam, dormant (cold blue) and spent (ember) posts at the other sites, footlights. Cosmetic, every machine.
- `ASQCharacter::RecomputeStats`: sack growth per lantern.
- `ASQHUD`: trail row drawn first in OBJECTIVES; minimap lit / dark / spent lantern icons and a pointer line.
- Missions.csv: `BankSouls` rows removed from every map (the type still works if added back).

## What a map remaster should assume

- **Lantern sites are the anchors of each district.** Build each district's set piece, lighting and open fighting ground round its
  lantern site (a flat clear disc of at least 9 m radius, 7.5 m in from the landmark towards the hub), not round the landmark alone.
- The route between consecutive sites runs down one lane to the hub and up another: lanes are the main streets and should read as
  such. Direct district-to-district links (ring road, shortcuts over ramps) would make the journey more interesting than
  hub-and-spoke; `GetLanternTrail` only needs sites, so links can be added freely.
- The final lantern's district and the boss arena should be neighbours with a clear, dramatic approach between them.
- The hub is only the opening two minutes now: it does not need to be the biggest or most detailed space.
- Dormant lanterns should be visible from a distance (silhouette on a rise) so players learn where the trail can go.
- Spawner pads, nests, elites and urns are still placed map-wide; a remaster could bias the active objective toward the lit
  lantern's district so the two pull the same way.
