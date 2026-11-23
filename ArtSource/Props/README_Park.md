# Soul Quota: skatepark pieces (bones, graveyard stone, coffins)

Ten original low-poly park pieces for sliding reapers. They were built from scratch in Blender with Python
(`/home/claude/blender_work/park/park_build.py`). They use no third-party meshes or textures.
Previews: `../Previews/Park_Pieces.png`. Each piece is shown at the left with a 180 cm reaper and a 1 m grid, and its collision hulls at the right.

## Conventions (all pieces)
- **Scale**: real game scale in cm. FBX `UnitScaleFactor` is 100, the same as the character FBXs. **Place the pieces at scale 1.0.** Don't use FitScale/FitMeshHeight on them.
- **Pivot**: the centre of the footprint's bounding box, on the floor. Z = 0 is the ground, so put the actor on the ground surface.
- **Axes**: Blender X is UE X, Blender -Y is UE +Y, and Z is up. The pieces are symmetric in Y except the Femur Ramp, so the Y flip doesn't matter.
- **Ride direction**: **+X** on every piece unless the table says otherwise. Yaw the actor so its local +X points the way players should slide.
- **Collision**: each FBX has hand-authored `UCX_<MeshName>_##` convex hulls built from the same profile points as the visual surface, so collision matches the visual exactly.
  - Curved surfaces are split into convex chord slabs (one per visual segment). There are no steps between slabs.
  - Decorative solids (knobs, teeth, wings, tomb blocks) also have hulls so nothing can be walked through.
  - Flat decals (eyes, cross, plank lines, grave hole) have no collision. They sit 0.3-0.9 cm above the surface.
- **Angles**: every ride face is 40 deg or less. The steepest ride face of each piece is listed below. The quarter pipe's curve runs from 0 to 80 deg by design.
- **Material**: one slot, `M_Park`. The palette is `park_palette.png`: 128x128 pixels, 8x8 swatches of 16 px, the same layout style as the other palettes.
- **Faces**: flat shaded.

| Mesh (`SM_` asset) | Size X x Y x Z (cm) | Footprint X / Y (cm) | Tris | UCX | Steepest ride face | Ride direction / layout (local cm, after centring) |
|---|---|---|---|---|---|---|
| `Park_RibKicker` | 330 x 257 x 120 | -165..165 / -128..128 | 356 | 13 | 37 deg (lip) | **+X**. Curved rib launch ramp, lane 200 wide. Starts at the ground at x=-165, lip at x=+135 (z=120). Stone foot behind to x=+165. Rib bones run along both sides, with knobs at the lip |
| `Park_FemurRamp` | 618 x 234 x 198 | -309..309 / -117..117 | 186 | 12 | 34 deg (edge chamfer), shaft 17 deg | **+X, slide downhill**. Start on the high end at x=-261 (z=180, on a tomb block). Flat 70 cm top (100 cm wide shaft) centred on **y=+15**. Slopes down to z=40 at x=+199, then a dirt run-out to the ground at x=+309. The femur head (off -Y) and trochanter (off +Y) are solid knobs at the high end |
| `Park_SpineWave` | 800 x 300 x 60 | -400..400 / -150..150 | 192 | 36 | 35 deg | **+/-X**. Three vertebra pump bumps, 60 cm high, crests at x = -267 / 0 / +267, touching the ground between them. Lane 180 wide. The side wings are solid and stay below the crest |
| `Park_PelvisQuarter` | 401 x 561 x 249 (wings 331) | -200..200 / -280..280 | 356 | 18 | 0-80 deg curve (exempt) | **+X into the wall**. Roll-in at the ground at x=-200, curve radius 285. Lip at x=+80 (z=236). Bone coping centred at x=+96, top z=249. Deck at z=236 runs to x=+190. Lane 500 wide (y +/-250). Iliac wings stand on the back corners of the deck (solid) |
| `Park_SkullHip` | 420 x 380 x 120 | -210..210 / -190..190 | 55 | 1 | 40 deg | **Any side**. Pyramid hip with a flat 110 x 90 top at z=120. The 4 corner edges are the hips: ride diagonally over an edge to transfer. Skull face on the -X slope |
| `Park_CoffinFunbox` | 700 x 215 x 90 | -350..350 / -107..107 | 176 | 3 | 22 deg | **+/-X**. Plank ramp from x=-350 to -130 (120 wide). Flat coffin lid at z=90 from x=-130 to 130 (120-210 wide). Plank ramp down to x=+350 (150 wide). The sides of the coffin are vertical |
| `Park_TombPad` | 360 x 200 x 30 | -180..180 / -100..100 | 40 | 1 | 37 deg (chamfers) | **+/-X** (manual pad). Ledger tombstone, 30 cm high, with a flat top 280 x 120 cm. All four edges are chamfered |
| `Park_JawBank` | 389 x 805 x 168 (condyles 189) | -194..194 / -403..403 | 264 | 22 | 32 deg | **Outward from the arc centre**. The U opens to -X. Arc centre at local (x=-225, y=0), spanning +/-75 deg. The bank rises from radius 120 (z=0) to radius 330 (z=130). Deck at z=130 out to radius 380. Molar wall behind it to radius 420 (top z=168). Condyle posts on the deck at both tips |
| `Park_GraveRoller` | 526 x 300 x 70 (cross 110) | -263..263 / -150..150 | 172 | 37 | 38 deg (side shoulders), 24 deg along X | **+/-X**. One smooth grave-mound roller, 70 cm high, from x=-263 to +237 with the crest at x=-13. Flat crest band 120 wide; shoulders slope to the ground at y=+/-150. Wooden grave cross (solid) at x=+257 |
| `Park_BoneBridge` | 950 x 341 x 100 | -475..475 / -171..171 | 198 | 11 | 39 deg (kicker lip) | **+X**. Kicker from x=-475 to -225 (z 0 to 100), takeoff deck to x=-165. Then a 280 cm **gap** (open-grave decal) from x=-165 to +115, crossed by a bone bridge 40 cm wide whose flat top is flush at z=100. Landing deck from x=+115 to +175 at z=100, then a 18 deg run-out to the ground at x=+475. Lane 220 wide. The dirt piles beside the gap are visual only |

## In Unreal (after `Scripts/import_art.py`)
- Meshes: `/Game/Art/Props/SM_Park_<Name>` (for example `SM_Park_RibKicker`). Load them with `SQArt::Prop(FName("Park_RibKicker"))`.
  - Collision is the UCX hulls: simple collision, one convex per UCX. Nothing is auto-generated.
  - Give the component `SetCollisionProfileName("BlockAll")`, as the arena does for other props.
  - To keep the camera boom steady, also call `SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore)`, as the terrain does.
- Texture: `/Game/Art/Textures/T_Park`, with nearest filtering and no mips, like the other palettes. In C++:
  `SQArt::CharacterTexture(FName("Park"))`. It looks up `/Game/Art/Textures/T_Park` and falls back to T_Skeleton if it's missing.
- Material: the meshes already carry `/Game/Art/Materials/MI_Park` (M_ArtTint with Tex = T_Park, Color = white) on slot 0.
  For the usual runtime tint, use:
  `SQArt::SetAllMaterials(Comp, SQArt::TintMID(this, SQArt::CharacterTexture(FName("Park")), FLinearColor::White))`.
  Pass a SoftTint colour instead of white to tint per biome. Don't use `SQArt::PropTexture()`: that one is T_HalloweenBits.

## Licence
All ten meshes, their collision and `park_palette.png` are original work by the Soul Quota project (November 2026).
