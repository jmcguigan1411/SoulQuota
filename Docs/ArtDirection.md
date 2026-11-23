# Soul Quota art direction: "Midnight Fairground"

Set Nov 12 2026 (style pass). One look for the whole game. Every asset, effect, menu and map is judged against this page.

## The look in one line

A spooky-but-fun graveyard at night, lit like a fairground: a magenta horizon under an indigo sky, a huge pale moon,
emerald grass, warm orange lanterns, chunky toy-like monsters with a plum ink outline and a cool rim light, and
effects in bright candy colours. Scary shapes, friendly colours. Never grey, never muddy.

## Palette

Rule of use: about 60% ground and sky tones, 30% stone and silhouettes, 10% accents. The accents are reserved for
gameplay. A new colour is added as a shade of one of these first; a new hue only with a reason written here.

### Fixed gameplay colours (same in every biome, nothing else may use them at full strength)

| Role | Hex |
|---|---|
| XP crystals | `#2BFF5E` |
| Gold | `#FFC61A` |
| Souls | `#3DF5E0` |
| Danger / enemy telegraphs / hazards | `#FF3B3B` |
| Shrines / friendly interactables | `#35C8FF` |
| Rim light on bodies | `#73BFFF` |
| Ink line (multiplies the scene, never pure black) | plum, about `#52346B` on a mid-grey |

### Graveyard (Biomes.csv row `Graveyard`)

| Role | Hex |
|---|---|
| Grass | `#3F9A4A` |
| Grass alt (cool patches) | `#2E8F7A` |
| Hilltop / highlights | `#8FD05A` |
| Path, road | `#B07A4A`, `#C08A5A` |
| Basin / low ground | `#5A3FA0` |
| Stone props | `#B4AEC8` |
| Fog | `#3A2466` |
| Moon key light | `#9FD0FF` |
| Lantern light | `#FFA53A` |
| Far silhouettes | `#140F2E` |
| Sky: horizon / mid / zenith | `#C2418F` / `#4B2A8C` / `#0B0A2A` |

### Other biomes (sky horizon / mid / zenith; ground colours keep their own hue family, pushed by `StyleGroundVivid`)

| Biome | Horizon | Mid | Zenith | Mood |
|---|---|---|---|---|
| Drowned Harbour | `#2FB7A6` | `#1F4F8C` | `#06142E` | sea-green glow, cold water |
| Plague Carnival | `#FF7A3D` | `#8C2A7A` | `#1A0A33` | hot orange big-top glow |
| Frozen Monastery | `#8FD8FF` | `#3A5FB0` | `#0A1238` | ice-blue dawn |
| Clocktower | `#E0A040` | `#5A2E5E` | `#120A1E` | brass and smoke |

## Lighting recipe

1. Cool moon key (`MoonColor`), one direction, soft-edged shadows.
2. Coloured fill from the sky dome (the sky light captures it): shadows lean violet, never black.
3. Warm lantern pools along lanes and at landmarks: the complementary accent to the cool key.
4. One accent colour per district (beacon orb, lane candles), low strength.
5. Rim light on every art mesh so reapers and the horde separate from the ground when seen from behind.
6. Bloom on purpose: only emissive things bloom (pickups, lanterns, moon, effects, interactable cues).

## Shading and outline rules

- Ink outline in post (`M_SQInk`): depth and crease edges, about 1.5 px, plum, multiplied over the scene.
- Lines fade out with distance (`StyleInkFadeDistance`): near bodies are crisp, the far horde and the skyline have none.
- Effects are drawn after the ink, so sparks and glows are never outlined.
- Shapes stay chunky and low-poly. No realistic textures, no noise-heavy surfaces: large flat colour shapes.

## Sky and horizon (third-person camera)

- Gradient dome (`M_SQSky`): horizon glow, mid band, dark zenith, per biome.
- A big moon low over the horizon, stars above the haze line.
- Two rings of rolling hills outside the walls: the near ring dark, the far ring paler (depth by value, not by fog alone).
- A giant skyline of the biome's own silhouettes (dead trees, crypts) on the near hills.
- Fog colour leans to the horizon colour so the far ground melts into the sky. There is no visible world edge.

## VFX rules

- Every effect colour is pushed away from grey (`StyleFxVivid`); grey smoke is the only matte exception.
- Hits read in a crowd: each burst has a fat, very short flash at its centre.
- Bigger and more of them than feels safe (`StyleFxSize`, `StyleFxCount`), short lifetimes so the screen clears.
- An effect uses its owner's colour (reaper theme) or a gameplay colour, never a random one.

## Where each rule lives

| Rule | Where |
|---|---|
| Palette | `Content/Data/Biomes.csv` colour columns, incl. `SkyHorizonColor` / `SkyMidColor` / `SkyZenithColor` |
| Strengths | `Content/Data/Tuning.csv`, keys starting `Style` (plus `MapSaturation`, `MapBloom`, `MapGroundDesat`) |
| Materials | `Scripts/create_style_materials.py` (`M_SQSky`, `M_SQInk`, `M_ArtTintV2`) |
| Sky, hills, ink hookup | `ASQArena::BuildSky`, `ASQArena::ApplyAtmosphere` |
| Rim light | `SQArt::TintMID` |
| Effect dials | `ASQSparks::InitPool` |

## Not yet at this standard (needs an artist or a licensed kit)

- Cel-banded shading on characters (needs a custom-lit master material tuned by eye).
- Textured, painterly ground (still vertex colour plus procedural breakup).
- Bespoke landmark set pieces per district and per biome.
- Animated foliage, cloud layers, and authored particle effects (Niagara).

## Research notes (principles only; no assets, palettes or characters copied)

- Post-process outlines compare neighbouring depth and normals; width or strength should vary with distance or distant
  objects are overwhelmed. Shadows read better hue-shifted than simply darkened. Saturation is best reserved for
  gameplay cues. (Stray Spark, "Stylized rendering in UE5")
- A fixed, limited palette with roles and a 60-30-10 split keeps accents meaningful; the highest-contrast hue goes to
  interactables and threats; per-biome palettes derive from the master. (Bugnet, "Color palettes for indie games")
- Toon outline construction in Unreal post-process materials. (Kodeco, "Unreal Engine Toon Outlines Tutorial")

Sources:
- https://www.strayspark.studio/blog/stylized-rendering-ue5-non-photorealistic
- https://bugnet.io/blog/color-palettes-for-indie-games
- https://www.kodeco.com/92-unreal-engine-4-toon-outlines-tutorial
- https://bugnet.io/blog/low-poly-3d-a-style-guide-for-small-teams

## Nov 12 2026 revision (from the first real screenshots)

The 17:13 build showed what the blind pass got wrong: the ground was painted at full chroma in three competing hues
(neon green, yellow, cyan) under a magenta sky, with near-white flagstones. Rule from now on: **colour is rationed**.

- **Ground: one dominant hue per biome, darker and less saturated than anything that matters.** Graveyard is deep
  emerald `#2A5C44` (alt `#275747`, raised tops `#3A7350`), paths dusky brown `#6A5448` / road `#75604F`, basins and
  boss floor indigo `#33305E`, plaza stone lavender-grey from cap `#8C86A0` + cliff `#5A5568`. `StyleGroundVivid` 0,
  `MapDistrictTint` 0.04, `MapGroundBrightness` 0.85, `MapSaturation` 1.06, `MapBloom` 0.6.
- **District identity comes from accents** (lantern colour, monument orb, props, trim), not from repainting the floor.
- **Saturation budget:** the sky gradient, lantern light, pickups (XP green, gold, soul teal), danger red and effects
  carry the colour. Characters and the horde are the brightest solids.
- **Light is light, solids are solids.** Glows (souls, halos, shockwaves, telegraph fills, sparks, flames) use the
  additive `M_SQGlow`; they never read as opaque balls. Objects (shards, clods, coins, crystals, warning rims) stay solid.
- **Souls** are teal light wisps: small bright core, soft halo, see-through flame tail, faint ground pool; merged souls
  grow in four sizes. A full sack only dims them.
- **Area effects are never one flat circle.** Impact = light wash + bright-edged shockwave + echo ring + flash +
  glowing cracks + thrown shards + rising motes. Warning = solid rim ring(s) + red fill that grows to the impact.
- **No motion blur, film grain, lens flares or chromatic fringe.**
