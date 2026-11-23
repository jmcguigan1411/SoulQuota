# ====================================================================== E / R casts on hand-animated bases
E_BASE = {"Grim": "1H_Melee_Attack_Slice_Horizontal", "Mort": "Throw", "Widow": "Spellcast_Shoot", "Bonesmith": "Block_Attack", "Keeper": "Throw",
          "Monk": "Unarmed_Melee_Attack_Punch_A", "Intern": "2H_Melee_Attack_Spin", "Femur": "Jump_Full_Short", "Digby": "2H_Melee_Attack_Slice",
          "Collector": "Throw", "Beak": "Throw", "Tallow": "Jump_Full_Short", "Lookout": "1H_Melee_Attack_Stab", "Vexa": "1H_Melee_Attack_Stab",
          "Actuary": "1H_Ranged_Shoot", "Sweep": "Jump_Full_Short", "Solace": "1H_Melee_Attack_Chop", "Pallbearers": "2H_Melee_Attack_Chop",
          "Ectoplasm": "Spellcast_Shoot", "Neckless": "Throw", "Tickwell": "Dodge_Forward"}
R_BASE = {"Grim": "2H_Melee_Attack_Spin", "Mort": "Cheer", "Widow": "Spellcast_Long", "Bonesmith": "Spellcast_Summon", "Keeper": "Spellcast_Raise",
          "Monk": "Unarmed_Melee_Attack_Punch_B", "Intern": "Spellcast_Summon", "Femur": "Taunt_Longer", "Digby": "1H_Melee_Attack_Jump_Chop",
          "Collector": "1H_Melee_Attack_Jump_Chop", "Beak": "Spellcast_Raise", "Tallow": "Spellcast_Long", "Lookout": "1H_Melee_Attack_Jump_Chop",
          "Vexa": "Taunt_Longer", "Actuary": "Spellcast_Summon", "Sweep": "Spellcast_Raise", "Solace": "Spellcast_Summon", "Pallbearers": "Taunt",
          "Ectoplasm": "Spellcast_Summon", "Neckless": "Cheer", "Tickwell": "2H_Melee_Attack_Chop"}
def gen_e(rid):
    I = 8 if rid in HEAVY else (6 if rid in QUICK else 7)
    shot8("SQ_E_" + rid, rid, E_BASE[rid], 24, I, {}, big=1.1)
def gen_r(rid):
    I = 12 if rid in HEAVY else (10 if rid in QUICK else 11)
    shot8("SQ_R_" + rid, rid, R_BASE[rid], 40, I, {}, big=1.3, hold=10)
FAMS["e"] = gen_e
FAMS["r"] = gen_r
