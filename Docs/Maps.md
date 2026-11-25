# Soul Quota: the three maps (Nov 12 2026 remaster)

The game ships three maps. Each keeps the core loop (auto-attack horde, XP / gold / souls, the lantern trail from
`Docs/LanternTrail.md`, the bell and the boss, 1 to 4 reapers) and changes HOW you play it through its ground shape and
one map rule. Plague Carnival and Frozen Monastery are retired (rows removed from Biomes.csv / Missions.csv; their boss
rows stay in Bosses.csv unused; old saves that point at them fall back to the first unlocked map).

## Rules for every map
- **Floor rule:** nothing lies on the floor except what you can pick up. No bones, candles, kerbs, loose slabs, grass
  spikes, coping stones, lane candles or footlight discs (`MapFloorClutter` 0, `LanternFootlights` 0). Ground detail is
  the ground itself (shape, paint, material); everything else is a standing structure with collision.
- **Districts are places:** each landmark has a building behind it, a gate where its lane arrives, a broken yard wall
  round it, an avenue of trees (or pilings) and lamp posts along the lane, statues round the lantern square.
  (`ASQArena::BuildSetPieces`, Biomes.csv `SetPieces` = `Role:Mesh|...`, meshes `ArtSource/Props/Set_*.fbx`.)
- **Shape:** Biomes.csv `MapShape` / `MapShapeTier` / `MapShapeSteps` (`ASQArena::MacroHeight`). Mission floors stay
  level; lanes and roads ramp between levels (slope clamp `MapMaxSlope`).
- Objectives (nests, elites, urns) are placed nearest the LIT lantern, so they sit on the trail.
- Layouts are still rolled from the run seed and validated; set pieces keep capsule-wide gaps and never seal anything.

## 1. The Overflowing Graveyard: the open map (learn the game)
- **Shape `Terraces`:** two broad raised burial terraces (230 cm each) that the lanes climb; skate bowls and banks in the
  districts; hub-and-spokes.
- **Places:** chapel, mausoleum, bell tower; iron-arch gates; stone yard walls; yew avenues; mourner statues.
- **Rule:** none. This is the readable map: walk the trail, learn banking, bell, boss.

## 2. The Drowned Harbour: the tide is the map
- **Shape `Causeways`:** low tidal flats. The hub, every district, the road and every lane are raised quays and
  boardwalks (230 cm); a few islets stand out on the flats.
- **Rule: the tide** (`TideCycle` 60 s: 40% low, 15% rising, 30% high, 15% falling). At high tide the water covers the
  flats (`TideHighShare` 0.62 of the quay height). Flooded ground slows reapers (`TideSlow` 0.6) and slows the horde to
  half speed and hurts it (`TideEnemyDps` 10).
- **How it plays:** at low tide the whole map is open: cut across the flats, ride the features, farm wide. When the tide
  turns you run for the boardwalks; the horde wading in is slow, so the fight funnels onto narrow causeways where area
  weapons shine. Chests and pickups left on the flats wait under the water for the ebb.
- **Places:** lighthouse, boathouse, dock crane; pilings line the boardwalks.

## 3. The Clocktower: the climb and the hands
- **Shape `Climb`:** a staircase hillside, five 210 cm steps from the far side of the map, through the hub, up to the boss
  arena at the top. The lantern trail is walked in order of height: it only goes up.
- **Rule: the clock hands** (`ClockHands` 2). The map is a clock face. A long hand sweeps the whole map once every
  `ClockHandPeriod` 50 s, a short hand turns slowly the other way. A hand hits any grounded reaper it touches
  (`ClockHandDamage` 22) and flattens the horde under it (`ClockHandEnemyDps` 600). JUMP it (Space). The old rule stays:
  every 30 s time stops for 4 s and the horde freezes.
- **How it plays:** you read the hands and move with them: kite the horde into a hand, jump it yourself, climb between
  sweeps. Bots are not hit by the hands (they cannot time the jump).
- **Places:** clock tower, a giant standing gear, gear stacks; gates and walls as in the graveyard.

## For later work
- Bridges and true cliffs need more than the height grid (it has no overhangs): a later pass with authored meshes.
- The sky hills sit at a fixed height: on the Clocktower the high side of the map rises above them.
- Retired content still on disk: Ringmaster / Abbot boss rows, PumpkinPot, spotlight and ice code paths.

## Map art pass (Nov 15 2026): ground art and standing scenery

Jack: "I want the map to be full of art, nothing basic", with a clean floor (nothing lies on it but pickups).

**Ground art (flat, in the surface).** The terrain uses `M_GroundV3` (built by `Scripts/create_ground_material.py`;
falls back to V2 if missing or if `MapGroundArt` is 0). `ASQArena::BuildTerrainMesh` writes masks into the mesh:
UV0.x = stone, UV0.y = wood, UV1 = plaza-local emblem coordinates, UV2 = world XY, vertex alpha = wetness.

| Map | Paved surfaces | Plaza emblem | Extra |
|---|---|---|---|
| Graveyard | staggered flagstones on the hub and plazas; lanes stay worn dirt | 8-spoke mosaic | moss at walls, lantern wear |
| Drowned Harbour | timber planks on quays, lanes and road | 16-point compass | dark, glossy tidal mud on the flats |
| Clocktower | chequered square tiles on plazas, lanes and road | 12-hour dial | moss at walls, lantern wear |

Joints widen with distance and fade out between about 18 m and 45 m, so nothing shimmers from the chase camera.
Keys: `MapGroundArt`, `MapGroundMoss`, `MapGroundJoint`.

**Standing scenery (`ASQArena::BuildScenery`).** Two more CC0 KayKit packs by Kay Lousberg, the same artist and style as
the Halloween kit already in the game: Dungeon Remastered (`KitD_*`) and Medieval Hexagon (`KitM_*`); licences in
`ArtSource/LICENSE_KayKit_DungeonRemastered.txt` and `LICENSE_KayKit_MedievalHexagon.txt`. Per district: about five
house fronts round the plaza facing in, cargo or stalls pushed against a side wall, two towers behind, two wall runs
with a banner; pillar pairs along the lanes; groves of trees in the open ground; a forest ring beyond the arena edge.
Each map has its own mix (Graveyard: church, homes, tavern, well, ruin; Harbour: mills, market, smithy, barracks, heavy
cargo; Clocktower: scaffolds, stage, smithy, castle and tall towers). Everything blocks, has a horde footprint and
fades when between the camera and your reaper. Keys: `MapScenery` (density), `MapKitFrontYaw` (-90; use 90 if the
houses face away from their plaza).

## Map finish (Nov 24 2026): stairs, walkway, per-map skies, event looks

- **Stairs and faced banks** (ASQArena::BuildTerraceFaces, look only, the terrain still carries the walking): every terrace bank steeper than MapTerraceFaceSlope (12 deg) gets stone face slabs laid on the slope (Harbour: timber) and a coping line along its top edge. Where a lane or the road climbs a bank it gets stair treads (MapStairRise 24 cm) with cheek walls both sides.
- **One raised walkway per map** (PlanWalkway / BuildWalkway): a footbridge over one lane, 320 cm clear (MapWalkwayHeight), 340 cm wide, 20 deg ramps landing clear of walls, water, mission floors and other lanes. Deck, ramps, rails, piers and stepped abutments have real collision. Horde footprint: the ramps/abutments and piers go into BlockerSpots (so the horde grid steers round them), the span over the lane stays open and the horde walks under it. Lane lamps and lane spans keep out from under the deck. Themes: Graveyard stone footbridge, Harbour timber boardwalk, Clocktower iron gantry with brass rails. No seed that has no clear crossing gets one (logged).
- **Per-map skies** (BuildMapSky): Drowned Harbour, open sea on one side (no hills or skyline within 62 deg of StyleMapSkyYaw), a striped lighthouse on a rock with two slow sweeping beams. Clocktower, three lit clock faces on towers beyond the summit; with the clock twist their hands show the arena's hand angles.
- **Event skies** (TickMapFinish, every machine, from the replicated active event): Blood Moon blends the sky to deep red and the moon to red; Soul Storm blends it teal and swirls 48 teal streaks round the sky. Both blend back out when the event ends.
- **Trapped Spirit**: freeing it (Ally reward) calls ASQArena::StartCompanion; a replicated end time draws a companion wisp (core + three trailing motes) circling every reaper while the boost lasts.
- **Gilt Scuttler**: Events.csv Special `Flee`. SQGameMode::TickEvents pushes it away from the nearest standing reaper with the public ApplyKnockback API (36 cm radius, so only it moves), weaving a little so it can be cut off (EventFleeRange 1500, EventFleeSpeed 460). Its look: a gilt shell and four circling coin glints, found from the replicated miniboss bit in EnemyNet. No SQHordeManager internals changed.
- **District conditions**: two pennant standards at the conditioned district's gate, flag and orb in the condition colour (the colour of the pickup it boosts).
- **Hook for the SQHordeManager owner**: a reaper standing on the walkway deck is above the horde under it. Body blocking already ignores enemies shorter than the reaper's feet height; melee/contact damage should do the same (skip a hit when the reaper's feet are more than the enemy's height above GroundZ at its position).
- Tuning keys: Map finish block (MapTerraceFaces ... EventFleeSpeed). Uncompiled in Unreal; syntax-checked.
