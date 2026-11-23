# Soul Quota: original monsters (zombies, wraith, gravebat)

Five original low-poly enemies in the goofy, chunky Soul Quota style. Each model was built from scratch
in Blender with Python and uses its own small palette texture. No third-party meshes or textures were used.

## Files
| File | Contents |
|---|---|
| `Zombie.fbx` | **Mesh only**, skinned to the KayKit `Rig` (Minion skeleton). Classic shambler: green-grey skin, torn blue shirt, exposed ribs, one bulging eye and one glowing socket, mismatched feet |
| `ZombieBloat.fbx` | **Mesh only**, Minion skeleton. Fat zombie: huge stitched belly with pustules, tank top, suspenders, tongue out |
| `ZombieRunner.fbx` | **Mesh only**, Minion skeleton. Lanky and ragged: thin limbs, long claws, back hump, head pushed forward and low (reads hunched even in upright cycles), gaping fanged mouth, red eyes |
| `Wraith.fbx` | **Mesh only**, KayKit `Rig` (Mage skeleton). Legless robed ghost: pointed hood, dark void face with glowing slit eyes, bell sleeves, ghost hands, tattered glowing robe tail |
| `Gravebat.fbx` | Full skeletal mesh + own rig + 6 takes (`Fly`, `Glide`, `Swoop`, `Hit`, `Death`, `Spawn`). A bat-winged skull with tattered membranes |
| `zombie_palette.png`, `zombiebloat_palette.png`, `zombierunner_palette.png`, `wraith_palette.png`, `gravebat_palette.png` | 128x128 palettes (8x8 swatches of 16 px, the same layout as `reaper_palette.png`). Every face's UVs sit in the centre of one swatch |
| `Zombie.blend`, `ZombieBloat.blend`, `ZombieRunner.blend`, `Wraith.blend`, `Gravebat.blend` | Blender 5.2 sources (rig + mesh; the Gravebat file also holds its 6 actions) |
| `../Previews/Monster_<Model>_Sheet.png`, `../Previews/Monster_<Model>_Anims.png` | Turnarounds and animation contact sheets |

Build scripts (Blender 5.2 Python, run with `python3`) are in `/home/claude/blender_work/monsters/`:
`mq_lib.py` (shared helpers, palette, skinning, FBX export), `mq_zombies.py` (zombies + wraith),
`mq_gravebat.py` (bat rig, mesh and takes), `mq_render.py` (contact sheets), `mq_verify.py` (re-import check).

## Mesh-only models (zombies and wraith)
- The armature in each FBX is the unchanged KayKit `Rig`: same 41 bone names, hierarchy, rest pose and object transform as
  `Skeleton_Minion.fbx` / `Skeleton_Mage.fbx` (checked by re-importing: max head/tail difference 0.0000003 m).
- Import them onto the owner's existing Skeleton asset (`Scripts/import_art.py` does this), so they play every Minion or Mage animation.
- Skinning (max 4 influences, normalised):
  - torso and belly blend hips -> spine -> chest by height
  - arms are one continuous tube with smooth distance-based weights across the shoulder, elbow and wrist
  - knees blend upper leg -> lower leg; hands, heads, feet and toes are rigid
  - Wraith: the robe is rigid to the spine chain. Only the hanging tatters below the hips take up to 45 % of the
    upper leg on their side, so walk and run cycles make the tail flutter. Nothing else is driven by the leg bones.
    The robe ends 2 cm above the floor at rest; the `SQ_*_Float` cycles lift it clear.
- Checked in Blender with `Walking_D_Skeletons`, `SQ_Enemy_Shamble`, `1H_Melee_Attack_Chop` and `Death_A`
  (Wraith: `SQ_Run_Float`, `SQ_Idle_Float`, `Walking_A`, `Spellcast_Shoot`, `Hit_A`, `Death_A`).

## Gravebat rig and takes
Bones: `root` (on the floor, no skin), `body`, `head`, `jaw`, `wing1/2/3.l`, `wing1/2/3.r` (arm, forearm, hand + fingers), `tail1`, `tail2`.
The skull hovers about 1.5 m above `root` at rest. 30 fps, keyed on every frame, no root motion (the `root` bone never moves).

| Take | Frames | Loop | Notes |
|---|---|---|---|
| `Fly` | 1-16 (0.5 s beat) | yes | Fast downstroke and slower upstroke, forearm and hand lag the arm (follow-through on the tips), wing twist, body rises on the downstroke, head and tail overlap |
| `Glide` | 1-61 (2 s) | yes | Wings spread with dihedral, slow banking, tip flutter, head looks around, tail sways |
| `Swoop` | 1-37 (1.2 s) | no | Anticipation (rises and pulls back, nose up, wings high) 1-10, dive with wings swept back 10-21, **bite impact at frame 22 (about 60 %)** about 70 cm forward and 50 cm down, flaps back up to the hover pose by 37 |
| `Hit` | 1-13 (0.4 s) | no | Recoil back and up, wings flung, squash, settle |
| `Death` | 1-46 (1.5 s) | no | Jolt, frantic flapping spiral fall (two turns), hits the floor at frame 36, small bounce, lies limp from 42 |
| `Spawn` | 1-31 (1 s) | no | Starts tiny at floor level wrapped in its wings, grows and bursts open with a shriek at frame 13, flaps up to the hover pose |

## Export settings
Same as the Reaper: FBX 7400, `UnitScaleFactor` 100, forward -Z, up Y, no leaf bones, primary bone axis Y,
flat face smoothing, take names are plain action names. Mesh-only files have no animation stacks.
Two material slots per model: `M_<Model>` and `M_<Model>_Glow` (eyes, ghost light), so the game's "Glow" slot logic picks them up.

## Licence and credits
- All five meshes, the Gravebat rig, its six actions and the five palettes are **original work by the Soul Quota project**
  (built November 2026). No reference models, textures or characters from other games were used.
- The zombie and wraith meshes are skinned to the KayKit Skeletons armature (Kay Lousberg, CC0 1.0, public domain)
  so they can reuse its animations. The armature itself is not original; credit is appreciated but not required.
