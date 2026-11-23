# Soul Quota: personality animations (`SQ_*`)

There are 39 new actions on the shared KayKit `Rig` armature. They give each reaper its own way of walking,
running, idling and using its ability. They work on every body because all five rigs are identical
(same bone names and rest pose). The export script checks this.

## Files
| File | Contents |
|---|---|
| `Reaper_Custom.fbx` | Custom reaper: 95 KayKit + 6 `Reaper_*` + 39 `SQ_*` = **140 takes** |
| `Skeleton_Mage.fbx`, `Skeleton_Rogue.fbx`, `Skeleton_Warrior.fbx`, `Skeleton_Minion.fbx` | Original KayKit meshes, materials and UVs, plus 95 original + 39 `SQ_*` = **134 takes** each |
| `skeleton_texture.png` | KayKit texture (unchanged copy). The four `Skeleton_*.fbx` files look for it next to themselves |
| `../Previews/Anim_Locomotion_Sheet.png`, `../Previews/Anim_Abilities_Sheet.png` | Labelled contact sheets of the custom reaper |

All files use the same format as the KayKit source:
- FBX 7400, `UnitScaleFactor` 100, Y up, forward -Z
- no leaf bones
- take names are plain action names (for example `SQ_Walk_Sneak`, not `Rig|SQ_Walk_Sneak`)

## Conventions
- 30 fps, keyed on every frame.
- **Loops**: the last frame is the same as the first, as in the KayKit cycles (`Walking_A` is 1-33).
  Turn on looping in Unreal. The duplicate end frame is harmless.
- **No root motion**: the `root` bone stays at rest on every frame. Cycles play in place.
- Feet are placed with 2-bone IK onto planned foot paths:
  - A planted foot moves back at a constant speed and never goes through the floor.
  - The matching ground speed is in the "Speed" column (metres per second at 1.0 play rate; x100 for cm/s in Unreal).
  - Scale the play rate by `actual speed / listed speed` to keep feet from sliding.
- Hover actions (`*_Float`) lift the hips 20-30 cm, so the body floats above the capsule's floor.
- Abilities start and end close to a neutral standing pose, so they blend in and out of any idle.
  Feet stay planted except where noted.

## Locomotion and idles (loops)
| Action | Frames | Loop | Speed (m/s) | Description |
|---|---|---|---|---|
| `SQ_Walk_Sneak` | 1-49 (1.6 s) | yes | 0.50 | Crouched tiptoe: heels never touch down, long high steps, hands up and out like a cartoon burglar, head turns left and right |
| `SQ_Walk_March` | 1-33 (1.07 s) | yes | 0.70 | Stiff-legged knees-high march: upright, chest out, straight arms swinging to shoulder height |
| `SQ_Walk_Waddle` | 1-29 (0.93 s) | yes | 0.45 | Wide penguin waddle: feet turned out, whole body rocks over each foot, elbows flapping |
| `SQ_Walk_Swagger` | 1-41 (1.33 s) | yes | 0.64 | Cocky strut: big shoulder roll and counter-twist, leaning back, chin up, loose wide arm swing, head bob |
| `SQ_Walk_Limp` | 1-41 (1.33 s) | yes | 0.41 | Zombie-ish limp: right leg drags with its toe scuffing, quick lurch and dip onto the bad leg, right arm dangles, head lolls |
| `SQ_Walk_Prance` | 1-25 (0.83 s) | yes | 0.88 | Light skipping prance: big bounce, pointed toes, arms floating up and out |
| `SQ_Walk_Shuffle` | 1-33 (1.07 s) | yes | 0.23 | Old-man shuffle: tiny low steps, hunched over, gentle side-to-side rock |
| `SQ_Run_Sprint` | 1-19 (0.6 s) | yes | 2.58 | Athletic sprint: forward lean, high knees, flight phase, arms pumping with bent elbows |
| `SQ_Run_Scurry` | 1-15 (0.47 s) | yes | 2.04 | Low fast scurry on the balls of the feet, arms swept straight back, head forward |
| `SQ_Run_Gallop` | 1-21 (0.67 s) | yes | 1.88 | Bounding gallop: both feet land almost together, long flight, body pitches like a horse, hands forward holding imaginary reins |
| `SQ_Run_Heavy` | 1-25 (0.83 s) | yes | 1.31 | Stomping brute run: wide stance, hard hip drop on each footfall, rolling shoulders, wide swinging arms |
| `SQ_Run_Float` | 1-41 (1.33 s) | yes | n/a | Ghostly drift: hovers about 25 cm up, legs trail and dangle, body leans and sways, arms trail back with a wave lag |
| `SQ_Run_Waddle` | 1-19 (0.6 s) | yes | 1.10 | Fast waddle: same rock and flapping elbows as the walk, quicker and lower |
| `SQ_Idle_Nervous` | 1-73 (2.4 s) | yes | - | Fidgety: wrings hands, shifts weight, darts quick looks left and right, right toe taps |
| `SQ_Idle_Proud` | 1-61 (2.0 s) | yes | - | Chest out, chin up, fists on the waist with elbows out (arm IK), slow breathing |
| `SQ_Idle_Hunched` | 1-61 (2.0 s) | yes | - | Old and bent over, knees bent, arms dangling, slow sway and head bob |
| `SQ_Idle_Float` | 1-61 (2.0 s) | yes | - | Hovers about 30 cm up with a 5 cm bob, legs dangle with lag, arms drift |
| `SQ_Idle_Menace` | 1-49 (1.6 s) | yes | - | Leaning forward in a staggered stance, swaying side to side, clawed arms wide, head low and rolling |

## Abilities (one-shots, `SQ_Ability_<PassiveId>`)
The "key" column is the frame used on the contact sheet. It is a sensible time for gameplay events (VFX, sound, damage).

| Action | Frames | Key | Description |
|---|---|---|---|
| `SQ_Ability_TeamDamage` | 1-32 (1.07 s) | 12 | Rallying fist pump: crouch and cock the fist, punch straight up, second smaller pump, chest out |
| `SQ_Ability_Hauler` | 1-38 (1.27 s) | 22 | Bends to grab a sack, heaves it over the right shoulder with a twist, knees buckle under the weight |
| `SQ_Ability_SlowSouls` | 1-40 (1.33 s) | 13-32 | Wail: breath in, then head thrown back, arms flung wide, up on the toes, trembling hold |
| `SQ_Ability_Builder` | 1-36 (1.2 s) | 14 | Hammer raised overhead, drops to one knee (right knee down, toes tucked) and slams down. Left foot steps forward |
| `SQ_Ability_Banker` | 1-42 (1.4 s) | 12-34 | Pours coins from the right hand into a cupped left palm, then counts three taps with nods |
| `SQ_Ability_DashStrike` | 1-26 (0.87 s) | 11 | Winds up with a twist, lunges forward (left foot steps) with a straight palm strike |
| `SQ_Ability_OnTheJob` | 1-42 (1.4 s) | 4-22, 30 | Frantic scribbling on a notepad held out front, then a big thumbs-up with a head tilt |
| `SQ_Ability_Fetch` | 1-38 (1.27 s) | 20 | Sniffs around (head darting), crouches, pounces forward with both hands and pins on landing. Both feet hop forward about 16 cm |
| `SQ_Ability_Bulwark` | 1-32 (1.07 s) | 8-26 | Steps into a wide braced stance, forearm held across in front like a shield, other palm bracing it, absorbs a hit at frame 11 |
| `SQ_Ability_Collections` | 1-42 (1.4 s) | 29 | Greedy hand-rubbing, then leans in and points straight ahead |
| `SQ_Ability_Triage` | 1-28 (0.93 s) | 12 | Raises the "syringe" in an ice-pick grip, steps in and jabs forward-down, small push |
| `SQ_Ability_Scorch` | 1-24 (0.8 s) | 9 | Hand pulled to the chest, then flicked out to the side with a wrist snap and shake (sparks at frame 9) |
| `SQ_Ability_Lookout` | 1-44 (1.47 s) | 8-24, 30 | On tiptoe, shades the eyes with the left hand, scans left and right, then points forward with the right arm |
| `SQ_Ability_Spite` | 1-38 (1.27 s) | 10, 14-30 | Knee up, furious stomp (frame 10), then a scream with head back, fists down and back, shaking |
| `SQ_Ability_Audit` | 1-28 (0.93 s) | 13 | Lifts the stamp high, slams it down onto the "desk" at the left hand, impact shake |
| `SQ_Ability_Rooftops` | 1-32 (1.07 s) | 12 (takeoff), 15-19 (air) | Deep crouch with arms back, springs up about 28 cm in place with feet tucked and arms up, lands in a crouch |
| `SQ_Ability_LastRites` | 1-44 (1.47 s) | 20-36 | Bows over hands pressed together, then rises onto the toes with arms raised in a V and head back |
| `SQ_Ability_MobileBank` | 1-40 (1.33 s) | 23 | Squats and strains (trembling) under a heavy load, heaves it overhead, wobbles to keep balance |
| `SQ_Ability_Seance` | 1-44 (1.47 s) | 9-36 | Arms raised forward, hands trace two conjuring circles while the body and head sway in a circle |
| `SQ_Ability_Stampede` | 1-38 (1.27 s) | 12-22, 28 | Rears up like a horse (lean back, left knee up, hooves pawing), then slams forward and down |
| `SQ_Ability_Stopwatch` | 1-30 (1.0 s) | 13 | Raises a pocket-watch hand up and out in front of the face, right hand clicks it (frame 13, small body jolt), looks at it |

## How they were made
- Script: `/home/claude/blender_work/sq_anims2.py`. It writes into `Reaper_Custom.blend`.
- Each action starts as a copy of a KayKit action:
  - `Walking_A` for walks, `Running_A` for runs, `Idle` for idles and abilities
  - so object channels and helper bones are the same as in the source
- The deform chain is then rebuilt on every frame:
  - hips, spine, chest and head: absolute rotations
  - legs: 2-bone IK onto planned foot paths. These include heel strike, toe-off and ball pivot.
    The hips drop if a leg cannot reach, and a floor clamp keeps the toes above the ground.
  - arms: swing, spread and elbow angles with a stable elbow axis (no twist flips), or arm IK for `SQ_Idle_Proud`
- Bodies: `sq_export_bodies.py` imports each KayKit FBX and checks its rest skeleton against the reaper rig
  (difference 0.0), then copies the `SQ_*` actions in. No retargeting or baking was needed.
- Checks:
  - `sq_anim_check.py`: loop seams, root motion, floor contact, sideways skating
  - `sq_verify_anims.py`: re-imports every exported FBX and compares it with its source

## Verification results
- **All 5 FBX files pass.** In every file:
  - Header matches the source: `UnitScaleFactor` 100, the same axes
  - All original takes are present, with frame ranges unchanged
  - All 39 new takes are present with the right frame ranges
  - Take names are plain (no `Rig|` prefix)
  - Armature rest dimensions are 3.1247 x 1.4983 x 2.863, the same as the source
  - World matrix is the same as the source
  - Bone rest positions match to within 1e-7 m
- **KayKit bodies:** every mesh object matches the source:
  - vertex and face counts, material names, UV data, vertex groups and texture image
  - bone parenting (the hat, hood and helmet stay parented to `head`, the cape to `chest`)
- **Posed bones:**
  - Original actions match the source to within 0.0016 cm.
  - `SQ_*` actions match the .blend to within 0.63 cm. This small error comes from the exporter's
    key reduction (`bake_anim_simplify_factor=1.0`, the same setting as before).
  - With the reduction turned off the error is 0.0008 cm, but the files grow from about 31 MB to 45 MB.
- **Animation checks:**
  - Loop seams: 0.00 cm on all 18 loops
  - Root: 0.00 cm horizontal movement
  - Floor: no part of a planted foot goes through the floor
  - No sideways foot skating
  - Fastest move between frames: about 108 degrees per frame, on the deliberate impacts
    (Builder hammer, Spite stomp). There are no quaternion or twist flips.

## Known limitations
- The motion is procedural, not hand-polished. It reads well at gameplay distance, but close up it is simpler than hand-keyed animation.
- No props are modelled for the abilities (sack, hammer, coins, notepad, syringe, stamp, pocket watch).
  The hands mime them. Attach props to `handslot.l/.r` if needed.
- The reaper's large hood hides some details from a straight-down camera, so the poses push the hands out beyond it:
  - Hauler's hands at the top of the heave and Lookout's brow-shading hand are partly behind the hood at some angles.
  - Robe sleeves can clip through the capelet in the most extreme arm poses.
- Builder, DashStrike, Triage, Fetch and Bulwark move the feet a little within the clip and return them by the last frame.
  `SQ_Ability_Fetch` hops both feet forward about 16 cm and back. There is no root motion.

## Licence
- The `SQ_*` actions are original Soul Quota work.
- The rig and the KayKit meshes and texture are from KayKit Skeletons by Kay Lousberg (CC0 1.0).

## Remaster 2 (2026-11-07)
- All `SQ_*` takes were regenerated through a new shared pipeline in `sq_anims2.py` (`remaster_specs`/`make_core`, also used by `sq_enemy_anims.py`):
  monotone-cubic spline keys (no stop at every key), new eases (`k` overshoot, `x` expo snap, `c` cubic hit),
  overlapping action (hips lead, chest/head/arms follow 1-4 frames), damped springs on head (drag + nod), forearm and wrist
  (weapon-hand whip), one-shot pose exaggeration, and 2-3 frame hand/handslot scale pops on impact frames.
- Frame counts, loop lengths, ground speeds and documented key/impact frames are unchanged, except `SQ_Enemy_Swipe`
  (arm strike now lands on f14, was ~f15, to match the code's 45% rule).
- New takes: `SQ_Walk_Remaster`, `SQ_Run_Remaster`, `SQ_Idle_Remaster`, `SQ_Hit_A_R2`, `SQ_Hit_B_R2`, `SQ_Attack_Chop_R2`,
  `SQ_Attack_Slash_R2`, `SQ_Attack_Punch_R2`, `SQ_Throw_R2`, `SQ_Cast_Shoot_R2`, `SQ_Cast_Raise_R2`, `SQ_Cast_Summon_R2`,
  `SQ_Cast_Long_R2`, `SQ_Enemy_Lurch` (Skeleton_* only) and the importer marker `SQ_Marker_Remaster2` (2-frame Idle copy).
- Takes: Skeleton_* 158 each, Reaper_Custom 154. Before/after sheets: `../Previews/Remaster/`. Backups: `Saved/Backups/2026-11-07_anim_remaster/`.

## Remaster 3 (2026-11-09)
- Script: `/home/claude/blender_work/sq_anims3.py` (reuses the `sq_anims2.py` pipeline; run it after `sq_anims2.py`, then `sq_export.py` and `sq_export_bodies.py`).
- New: `SQ_Rise_FromGrave` (1-49, 1.6 s), `SQ_Reap_Swing` (1-21, impact f9 = 40%), `SQ_Attack_Slam_R3` (1-31, impact f16 = 50%),
  `SQ_Attack_Thrust_R3` (1-24, impact f9 = 35%), and the importer marker `SQ_Marker_Remaster3`.
- Remastered in place (same names and frame counts) with five beats (anticipation ~4 f, smear 1-2 f, impact pop, follow-through ~5 f, settle ~6 f):
  `SQ_Attack_Slash_R2` f9, `SQ_Attack_Chop_R2` f13 (was f14), `SQ_Attack_Punch_R2` f11, `SQ_Cast_Shoot_R2` f7, `SQ_Cast_Raise_R2` f14, `SQ_Throw_R2` f8.
  Impact frame = 1 + ImpactFrac x (frames - 1).
- Rise: no root motion. The body (hips subtree) starts 1.50 m below its place in the rig's units (about 68% of the 2.19 m height to the hood tip) and rises.
  Beats: hand breaks the surface at f17 and is fully up at f19, hood breaks at f21, both palms planted f24-30
  (0.30 m forward, 0.24 m to each side), dragged up f24-28, feet on the surface f31 (left) and f35 (right), shake f35-43, hero pose f45-49.
  The feet end exactly on the KayKit `Idle` footprints. Anything below the floor is meant to be hidden by the ground.
- Takes: Skeleton_* 163 each, Reaper_Custom 159. Sheets: `../Previews/Remaster3/`. Backups: `Saved/Backups/2026-11-09_anims3/`.
