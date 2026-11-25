# Character pass (Nov 25 2026) - iterations

## Status
- DONE: audit, 5 lineup rounds, motion round (Run/Sig/E/R) + per-character sheets, kit/trait/bark/UI code, FX iterations 9-10,
  balance report, device sync (141 files md5-identical), GitHub claude-wip pushed. Whole pass uncompiled in Unreal.
- Source of truth: Content/Data/Reapers.csv (Body*, Gait*, HitRadius, Trait*, Bio/Tip/Difficulty/Playstyle, Bark*),
  Kits.csv (E_/R_ Mod/ModValue/ModTime), blender_work/chars/lineup.py (preview reads the same CSV), blender_work/fx2.

## Lineup rounds
| Round | Sheet | Critique -> fix |
|---|---|---|
| 0 | lineup_r0_before | One body plan: 1.5-2.2 m, same legs/head/arms; robed casters merge from behind; brutes merge. |
| 1 | lineup_r1 | Body profile + sizes 1.1-2.9 m + hunch/hover. Keeper and Beak bent into blobs (hunch 30/26); Neckless reads as a second Digby; three brutes too close in size. |
| 2 | lineup_r2 | Hunch 18/14, Lookout 26; Bonesmith shorter + broader, Vexa wider barrel on shorter legs; Collector V-shape; Monk long reaching arms. Neckless still bulky. |
| 3 | lineup_r3 | Neckless slimmer, longer legs; Keeper wider with arms to the ground; Beak taller and thinner. Neckless's mesh stays broad (mesh, not bones). |
| 4 | motion_r4_run_sig_E_R | Run/attack/E/R frames with the body applied: Femur and Lookout lean too far at a run (hunch + lean ~60 deg) -> lean 14 / 12. |
| 5 | lineup_r5_final, lineup_before_after | Final. Every silhouette distinct in black-on-white; heights 1.14-2.94 m. |

Per-character sheets: sheet_<Id>.jpg (Idle / Run / Sig / E / R, front, back at 18 deg, silhouette) - true scale, so Intern is tiny on purpose.

## Effects
fx_v3/FX_E_iter9.jpg, FX_E_iter10.jpg, FX_R_iter10.jpg (log in fx_v3/ITERATIONS.md).
