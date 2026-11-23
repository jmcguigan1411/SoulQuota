# Reaper, Junior Grade (Soul Quota player character)

Original low-poly player character for Soul Quota: a small reaper on the job.
Chunky proportions (the hood and skull are about a third of the height), a deep hood with a
cute skull face (glowing teal eyes, stitched grin), a short tattered robe split into left and right
panels so the bony legs and chunky boots show when walking, a rope belt, a soul lantern on the left hip,
and a punch-card name badge on the chest.

## Files
| File | What |
|---|---|
| `Reaper_Custom.fbx` | Skeletal mesh `Reaper_Body` + armature `Rig` + 140 animation takes (incl. 39 `SQ_*` personality actions, see `README_Animations.md`) |
| `Reaper_Scythe.fbx` | Static mesh weapon. Pivot at the grip, pole along +Z, blade toward +Y (same convention as `Skeleton_Axe.fbx`). Attach to `handslot.r` |
| `Reaper_Clipboard.fbx` | Optional small static prop for `Reaper_Taunt_Clipboard`. Attach to `handslot.l` |
| `reaper_palette.png` | 128x128 palette (8x8 swatches of 16 px). Every face's UVs sit in the centre of one swatch |
| `reaper_palette_tintmask.png` | Optional mask (white = robe swatches, black = everything else) if you want the player tint on the robe only |
| `Reaper_Custom.blend`, `Reaper_Custom_base.blend`, `Reaper_Scythe.blend`, `Reaper_Clipboard.blend` | Blender 5.2 sources |
| `../Previews/*.png` | Eevee preview renders |

Build scripts (Blender 5.2 Python, run with `python3`) are in `/home/claude/blender_work/`:
`sq_build.py` (mesh, palette) -> `sq_anims.py` (new actions) -> `sq_export.py` (FBX export and check),
plus `sq_scythe.py`, `sq_clipboard.py`, `sq_preview.py`, `sq_verify.py`.

## Mesh
- Body: 3,725 triangles, 2,048 vertices, one mesh. Scythe: 430 triangles. Clipboard: 224 triangles.
- Flat shaded. Two material slots that use the same palette texture:
  - `M_Reaper` for everything except the glow parts
  - `M_Reaper_Glow` for the eyes, eye highlights and lantern core (and the scythe's soul gems), so you can make it emissive
- The robe swatches are a light neutral grey, so a multiply tint (Tex * Color) recolours the reaper.
  A plain multiply tints the skull, boots and rope too. To tint only the robe, use
  `lerp(Tex, Tex*Color, Mask)` with `reaper_palette_tintmask.png`.

## Rig and skinning
- The armature is the unchanged KayKit "Rig" (from `Skeleton_Mage.fbx`), with the same bone names, rest pose and scale.
  The original KayKit mesh was removed and replaced.
- Skinning is mostly rigid, one part to one bone: skull, face and hood to `head`; hands to `hand.*`;
  bell sleeves to `lowerarm.*`; thighs to `upperleg.*`; knee and shin to `lowerleg.*`; boots to `foot.*`;
  toe caps to `toes.*`; belt and lantern to `hips`; capelet to `chest`.
- Some parts blend by height so the cloth bends smoothly:
  - torso: hips -> spine -> chest
  - upper sleeves: chest/upperarm at the shoulder
  - each skirt half: hips at the waist -> `upperleg.l/.r` at the hem
- The robe ends just above the knee and is split front and back, so each leg carries its own panel.
  This keeps the legs readable in every walk and run.

## Export settings and scale check
- Export settings:
  - FBX units scale (`UnitScaleFactor` = 100, the same as the source)
  - forward -Z, up Y
  - `add_leaf_bones=False`, primary bone axis Y
  - `bake_anim=True`, all actions, NLA strips off
- Take names are plain action names (for example `Idle`, `Running_A`), the same as the source FBX.
  Blender would normally write `Rig|Idle`; the export script patches the take name.
- Checked by re-importing into a fresh scene:
  - armature rest dimensions are the same as `Skeleton_Mage.fbx` (3.1247 x 1.4983 x 2.863)
  - armature world matrix is the same
  - bone names match and bone head/tail positions match (difference 0.0)
  - posed bone positions for Running_A, Walking_A, Death_A and Cheer match to within 0.002 cm
  - all 95 original frame ranges are unchanged

## Actions (140)
The 39 `SQ_*` personality actions are documented in `README_Animations.md`.

Reaper actions (30 fps, keyed on every frame):

| Action | Frames | Notes |
|---|---|---|
| `Reaper_Scythe_Swing` | 1-22 (0.7 s) | Big horizontal two-hand sweep: wind-up to the right, sweep to the left, left hand IK'd onto the pole |
| `Reaper_Scythe_Slam` | 1-28 (0.9 s) | Overhead raise with a lean back, then a fast slam and crouch; impact at about frame 15 |
| `Reaper_Carry_Run` | 1-25 (loop) | Running_A with a forward lean and both arms hugging a bundle |
| `Reaper_Bank_Channel` | 1-37 (1.2 s loop) | Leaning over, alternating pouring hands, knees bobbing |
| `Reaper_Victory_Dance` | 1-49 (1.6 s loop) | Hip twist, knee bounce, "raise the roof" arms |
| `Reaper_Taunt_Clipboard` | 1-61 (2.0 s) | Looks at the clipboard (left hand), taps it, then shakes its head. Use with `Reaper_Clipboard` on `handslot.l` |

Original KayKit actions (95):
1H_Melee_Attack_Chop, 1H_Melee_Attack_Jump_Chop, 1H_Melee_Attack_Slice_Diagonal, 1H_Melee_Attack_Slice_Horizontal,
1H_Melee_Attack_Stab, 1H_Ranged_Aiming, 1H_Ranged_Reload, 1H_Ranged_Shoot, 1H_Ranged_Shooting, 2H_Melee_Attack_Chop,
2H_Melee_Attack_Slice, 2H_Melee_Attack_Spin, 2H_Melee_Attack_Spinning, 2H_Melee_Attack_Stab, 2H_Melee_Idle,
2H_Ranged_Aiming, 2H_Ranged_Reload, 2H_Ranged_Shoot, 2H_Ranged_Shooting, Block, Block_Attack, Block_Hit, Blocking,
Cheer, Death_A, Death_A_Pose, Death_B, Death_B_Pose, Death_C_Pose, Death_C_Skeletons, Death_C_Skeletons_Resurrect,
Dodge_Backward, Dodge_Forward, Dodge_Left, Dodge_Right, Dualwield_Melee_Attack_Chop, Dualwield_Melee_Attack_Slice,
Dualwield_Melee_Attack_Stab, Hit_A, Hit_B, Idle, Idle_B, Idle_Combat, Interact, Jump_Full_Long, Jump_Full_Short,
Jump_Idle, Jump_Land, Jump_Start, Lie_Down, Lie_Idle, Lie_Pose, Lie_StandUp, PickUp, Running_A, Running_B, Running_C,
Running_Strafe_Left, Running_Strafe_Right, Sit_Chair_Down, Sit_Chair_Idle, Sit_Chair_Pose, Sit_Chair_StandUp,
Sit_Floor_Down, Sit_Floor_Idle, Sit_Floor_Pose, Sit_Floor_StandUp, Skeleton_Inactive_Standing_Pose,
Skeletons_Awaken_Floor, Skeletons_Awaken_Floor_Long, Skeletons_Awaken_Standing, Skeletons_Inactive_Floor_Pose,
Spawn_Air, Spawn_Ground, Spawn_Ground_Skeletons, Spellcast_Long, Spellcast_Raise, Spellcast_Shoot, Spellcast_Summon,
Spellcasting, T-Pose, Taunt, Taunt_Longer, Throw, Unarmed_Idle, Unarmed_Melee_Attack_Kick,
Unarmed_Melee_Attack_Punch_A, Unarmed_Melee_Attack_Punch_B, Unarmed_Pose, Use_Item, Walking_A, Walking_B,
Walking_Backwards, Walking_C, Walking_D_Skeletons

## Known limitations
- The weapon orientation was checked in Blender by bone-parenting to `handslot.r`, which is the convention the KayKit
  axe uses. If the blade faces backwards on the Unreal socket, rotate the socket 180 degrees around Z.
- The new actions were made by layering poses procedurally, not hand-keyed. They read well at gameplay distance but
  are not polished. Some clipping (sleeves through the capelet, skirt against the lantern) shows in extreme poses.
- The soul bundle for `Reaper_Carry_Run` is not modelled. The arms hug an empty space in front of the chest.
- The optional head variants (top hat, pumpkin hood) were not made.

## Licence
- The reaper mesh, palette, scythe, clipboard and the six `Reaper_*` actions are original work by the Soul Quota project.
- The armature and the 95 base animations are from KayKit Skeletons by Kay Lousberg (CC0 1.0, public domain).
  Credit is appreciated but not required.
