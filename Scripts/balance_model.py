"""Soul Quota balance model.

Nov 9 2026 remake balance pass: the default output is the UNIFIED RUN SIMULATION (sections 16-25, Docs/Balance.md
section 11): one mission run simulated step by step (stage, acts, objectives, minibosses, level-up cards, chests, augments,
kits, meta ranks, boss, overtime) for every mission x normal / Hard x 1-4 players, plus weapon / tome / item / augment / kit
valuation on the simulation's builds, the meta journey, a sensitivity pass and a verdict against the contract.
    python Scripts/balance_model.py            # unified report (~1 min)
    python Scripts/balance_model.py --quick    # without the sensitivity pass
    python Scripts/balance_model.py --legacy   # the retired 3-night sections 1-15 (below)

Legacy description - reads Content/Data/*.csv and prints, for each night / minute / player count:
  expected player DPS (starting weapon + typical level-up picks), enemy HP and damage,
  hits-to-kill for typical enemies, boss time-to-kill against Bosses.csv TargetSeconds,
  soul income against shop prices, and soul-coin pacing against reaper unlock costs.

Run (plain Python 3, no packages needed):
    python Scripts/balance_model.py                 # release night length (360 s, GameDesign.md)
    python Scripts/balance_model.py --night 90      # the current test NightSeconds
    python Scripts/balance_model.py --csv-night     # whatever Tuning.csv NightSeconds says

The formulas mirror the C++ (see Docs/Balance.md). The ASSUMPTIONS block below is the only
guesswork: how much of the horde a team kills and how many drops it collects. Replace them
with real numbers once bot runs log kills / banked souls per night.
"""
import argparse
import csv
import os
import statistics

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "Content", "Data")

# ---------------------------------------------------------------- ASSUMPTIONS (on-curve team)
KILL_SHARE = 0.75      # share of spawned enemies the team kills
XP_PICKUP = 0.85       # share of dropped XP collected
SOUL_PICKUP = 0.70     # share of dropped souls picked up
SOUL_LOSS = 0.30       # share of carried souls lost (each hit drops HitDropFraction of the sack) or unbanked at midnight
TRIP_SECONDS = 30.0    # one full-sack trip to the lantern and back incl. the 1.5 s channel (banking cap)
BOSS_UPTIME = 0.75     # share of a boss fight spent hitting it (shields 10%, roars, dodging, summons)
WEAPON_PICK_SHARE = 0.6  # share of level-up picks spent on weapons (rest on relics)
DESIGN_NIGHT = 360.0   # GameDesign.md: 9pm -> midnight = 6 minutes
WIN_RATE = 0.5        # share of runs won, for meta pacing

# How each weapon behaviour lands on a single big target (the boss) per activation
BOSS_HITS = {"Arc": 1.0, "Nova": 1.0, "Aura": 1.0, "Chain": 1.0, "Beam": 0.8,
             "Projectile": None, "Orbit": None, "Drop": None, "Turret": None}


def read_csv(name, data_dir=None):
    rows = []
    with open(os.path.join(data_dir or DATA, name), newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            first = next(iter(row.values())) or ""
            if first.startswith("#") or not first:
                continue
            rows.append(row)
    return rows


def num(v, d=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return d


class Data:
    def __init__(self, data_dir=None):
        rd = lambda name: read_csv(name, data_dir)
        self.tune = {r["Key"]: num(r["Value"]) for r in rd("Tuning.csv")}
        self.ups = {r["Id"]: r for r in rd("Upgrades.csv")}
        self.enemies = rd("Enemies.csv")
        self.bosses = rd("Bosses.csv")
        self.shop = rd("Shop.csv")
        self.reapers = rd("Reapers.csv")
        self.biomes = rd("Biomes.csv")
        self.quests = rd("Quests.csv")
        self.timeline = rd("Timeline.csv")
        self.modifiers = rd("Modifiers.csv")
        # Reaper kits (Nov 9 kits pass, Docs/Kits.md): signature weapon + passive + E + R per reaper
        try:
            self.kits = {r["ReaperId"]: r for r in rd("Kits.csv")}
        except FileNotFoundError:
            self.kits = {}
        # Collision pass (Nov 7): spawn-weighted mean enemy body radius (Enemies.csv Radius, now measured from the art).
        # Area weapons hit an enemy when weapon radius + body radius reach it, so bigger bodies = more targets per swing.
        w = [sum(num(e.get("Weight%d" % n)) for n in (1, 2, 3)) for e in self.enemies]
        self.enemy_radius = sum(wi * num(e["Radius"], 40.0) for wi, e in zip(w, self.enemies)) / (sum(w) or 1.0)

    def t(self, key, default):
        return self.tune.get(key, default)


# ---------------------------------------------------------------- mirrors of the C++ formulas
def upgrade_damage(u, level, power):          # SQ::UpgradeDamage
    return (num(u["Damage"]) + num(u["DamagePerLevel"]) * (level - 1)) * max(0.1, power)


def upgrade_cooldown(u, level):               # SQ::UpgradeCooldown
    return max(0.08, num(u["Cooldown"]) + num(u["CooldownPerLevel"]) * (level - 1))


def upgrade_radius(u, level, power):          # SQ::UpgradeRadius
    return (num(u["Radius"]) + num(u["RadiusPerLevel"]) * (level - 1)) * (1 + 0.25 * max(0.0, power - 1))


# The area targets below were calibrated when the weighted enemy radius was this (CSV before the Nov 7 collision pass)
ENEMY_RADIUS_CALIBRATED = 46.8


def area_reach_factor(d, weapon_radius):
    """(weapon radius + mean body radius)^2 against the calibration: how many more enemies an area hit touches"""
    return ((weapon_radius + d.enemy_radius) / (weapon_radius + ENEMY_RADIUS_CALIBRATED)) ** 2


def upgrade_count(u, level, power):           # SQ::UpgradeCount
    base = max(1, int(num(u["Count"]) + num(u["CountPerLevel"]) * (level - 1)))
    multi = num(u["Count"]) >= 2 or num(u["CountPerLevel"]) > 0
    return base + (int(max(0.0, power - 1.0) * 2.5 + 0.001) if multi else 0)


def rarity_odds(d, luck=0.0):                 # ASQPlayerState::RollRarity
    r, e, l = d.t("RarityRare", 27), d.t("RarityEpic", 10), d.t("RarityLegendary", 3)
    w = [max(0, 100 - r - e - l), r * (1 + 0.6 * luck), e * (1 + luck), l * (1 + 1.5 * luck)]
    s = sum(w)
    return [x / s for x in w]


WEAPON_POWER = [0.0, 0.10, 0.22, 0.40]        # SQ::RarityWeaponPower
STAT_MULT = [1.0, 1.4, 1.9, 2.6]              # SQ::RarityStatMult


def xp_to_next(d, level, players=1):          # ASQGameState::AddTeamXP
    mult = 1 + d.t("XPPerPlayer", 0.35) * (players - 1)
    return max(1, round((d.t("XPBase", 8) + d.t("XPGrowth", 15) * level) * mult))


def spawn_rate(d, progress, players):         # ASQHordeManager::SimSpawning
    r = d.t("SpawnRateStart", 4) + (d.t("SpawnRatePeak", 22) - d.t("SpawnRateStart", 4)) * progress
    return r * (1 + d.t("PlayerSpawnRateBonus", 0.35) * (players - 1))


def enemy_mix(d, night, progress):
    col = "Weight%d" % (min(night, 2) + 1)
    live = [e for e in d.enemies if progress >= num(e["MinProgress"])]
    total = sum(num(e[col]) for e in live) or 1.0
    return [(e, num(e[col]) / total) for e in live if num(e[col]) > 0]


def night_scale(d):
    """NightSeconds / BalanceNightSeconds: short test nights shrink the per-night HP step, boss HP and shop prices"""
    return min(4.0, max(0.1, d.t("NightSeconds", 360) / max(1.0, d.t("BalanceNightSeconds", 360))))


def enemy_hp(d, e, night, minute, players):   # ASQHordeManager::SpawnEnemy
    base = num(e["Health"]) + num(e["HealthPerNight"]) * night_scale(d) * night
    ramp = 1 + d.t("EnemyHealthPerMinute", 0.1) * minute
    coop = 1 + d.t("EnemyHealthPerPlayer", 0.35) * (players - 1)
    return base * ramp * coop * econ_mult(d, "EconomyEnemyHealth", night)


def econ_mult(d, key, night):
    """Economy pass: chest relics roughly double a reaper's DPS by night 3, so enemies / bosses get EconomyEnemyHealth<N> /
    EconomyBossHealth<N> more HP on night N (Endless uses night 3's). Mirrors ASQGameState::EconomyHealthMult."""
    return max(0.1, d.t("%s%d" % (key, min(3, night + 1)), 1.0))


def chest_picks_at(d, night_s, players, night, frac):
    """Chest relic rolls a reaper has had frac (0..1) of the way through a night (economy pass; linear inside the night)"""
    picks = chest_picks_before_bosses(d, night_s, players)
    before = (picks[night - 1] + 1) if night > 0 else 0.0
    return before + (picks[min(night, 2)] - before) * max(0.0, min(1.0, frac))


def enemy_damage_mult(d, night, minute):      # ASQHordeManager::EnemyDamageScale
    return (1 + d.t("EnemyDamagePerNight", 0.3) * night) * (1 + d.t("EnemyDamagePerMinute", 0.03) * minute)


def shop_price(d, item, night, buys, players):  # ASQGameState::ShopPrice
    c = max(0, int(num(item["Cost"])))
    m = (1 + d.t("ShopPricePerBuy", 0.75) * buys) * (1 + d.t("ShopPricePerNight", 0.5) * night) \
        * (1 + d.t("ShopPricePerPlayer", 0.75) * (players - 1)) * night_scale(d)
    return int(round(c * m))


# ---------------------------------------------------------------- typical build after N picks
# Items/tomes pass (Nov 7, Docs/ItemsAndTomes.md): Tuning WeaponSlots weapons (levels 1-8), TomeSlots tomes (levels 1-MaxLevel,
# a fixed gain per level x rarity), items from chests / Lost Property (no slots, stack, random by Weight), shrine raises.
# Stats are summed RAW and then run through finalize_stats (mirror of SQ::FinalizeStats).
OFFENCE_RELICS = ["Whetstone", "Hourglass", "Monocle", "RedInk"]      # kept for older callers
OTHER_RELICS = ["Sack", "ExtraRibs", "BonePlating", "MothLure"]

# Build plans: which tomes a player takes (the main weapon's evolution tome is always first), how many weapons, pick share
PLANS = {
    "typical": dict(tomes=["Whetstone", "Hourglass", "ExtraRibs", "Monocle"], weapons=4, weapon_share=0.6,
                    raise_pref=["Damage", "Cooldown", "CritChance", "Area", "MaxHealth", "Armor", "CritDamage", "Regen",
                                "MoveSpeed", "Duration", "Magnet", "Luck"]),
    "weapons": dict(tomes=["Whetstone", "Hourglass", "GraveDust"], weapons=4, weapon_share=0.75,
                    raise_pref=["Damage", "Cooldown", "Area", "CritChance", "CritDamage", "Duration", "MaxHealth", "Armor",
                                "Regen", "MoveSpeed", "Magnet", "Luck"]),
    "tomes": dict(tomes=["Whetstone", "Hourglass", "GraveDust", "Monocle"], weapons=2, weapon_share=0.35,
                  raise_pref=["Damage", "Cooldown", "Area", "CritChance", "CritDamage", "Duration", "MaxHealth", "Armor",
                              "Regen", "MoveSpeed", "Magnet", "Luck"]),
    "crit": dict(tomes=["Monocle", "RedInk", "Whetstone", "Hourglass"], weapons=4, weapon_share=0.55, evolve=True,
                 raise_pref=["CritChance", "CritDamage", "Damage", "Cooldown", "Area", "MaxHealth", "Armor", "Regen",
                             "MoveSpeed", "Duration", "Magnet", "Luck"]),
    "support": dict(tomes=["TeaUrn", "ExtraRibs", "BonePlating", "Embalming"], weapons=3, weapon_share=0.5,
                    raise_pref=["MaxHealth", "Armor", "Regen", "Damage", "Cooldown", "MoveSpeed", "Area", "CritChance",
                                "CritDamage", "Duration", "Magnet", "Luck"]),
}
# ---------------------------------------------------------------- ASSUMPTIONS (items / tomes; replace with playtest numbers)
LOST_PROPERTY_ITEMS = 1.2      # items per night from the Lost Property desks (2 desks, first come) - split across the crew
SHRINE_RAISES_PER_NIGHT = 2.0  # Performance Review raises a reaper takes per night (3 shrines; not every one is reached)
LEGENDARY_RAISE_SHARE = 0.15   # share of raises from golden / buried shrines (Legendary only)
REGEN_WINDOW = 30.0            # seconds of regen / healing counted into effective HP ("between big hits")
SOULS_PER_SECOND = 2.5         # souls a reaper picks up per second (PickupHeal)
CONTACTS_PER_SECOND = 1.5      # enemy touches per second (Thorns)


def is_new_data(d):
    return any((u.get("Category") or "") for u in d.ups.values())


def category(u):
    c = (u.get("Category") or "").strip()
    if c:
        return c
    return {"Relic": "Tome"}.get(u["Kind"], u["Kind"])


def finalize_stats(d, raw, rules="new"):
    """Mirror of SQ::FinalizeStats (rules="new") and the old ASQCharacter::RecomputeStats clamps (rules="old").
    raw: dict of summed stats (reaper base + tomes + items + raises); returns the effective stats."""
    g = lambda k, dflt: raw.get(k, dflt)
    out = dict(raw)
    speed = g("Cooldown", 0.0)                 # summed "Cooldown" values
    if rules == "new":
        out["CooldownMult"] = min(4.0, max(d.t("CooldownFloor", 0.4), 1 / (1 + speed) if speed >= 0 else 1 - speed))
        cc, cd = g("CritChance", 0.05), max(0.0, g("CritDamage", 0.5))
        cap = d.t("CritChanceMax", 1.0)
        if cc > cap:
            cd += (cc - cap) * d.t("CritOverflowToDamage", 1.0)
            cc = cap
        out["CritChance"], out["CritDamage"] = max(0.0, cc), cd
        out["Area"] = min(max(1.0, d.t("AreaMax", 3.0)), max(0.25, 1 + g("Area", 0.0)))
        out["Duration"] = min(max(1.0, d.t("DurationMax", 3.0)), max(0.25, 1 + g("Duration", 0.0)))
        a = max(0.0, g("Armor", 0.0))
        out["Armor"] = min(d.t("ArmorMax", 0.75), a / (a + d.t("ArmorK", 1.0)))
        e = max(0.0, g("Evasion", 0.0))
        out["Evasion"] = min(d.t("EvasionMax", 0.4), e / (e + d.t("EvasionK", 1.0)))
        lk, lc = max(0.0, g("Luck", 0.0)), d.t("LuckSoftCap", 2.0)
        out["Luck"] = lc * lk / (lk + lc)
        out["Projectiles"] = min(int(d.t("ProjectileBonusMax", 4)), int(g("Projectiles", 0) + 0.001))
        out["MoveSpeed"] = min(d.t("MoveSpeedMax", 1100), max(200.0, g("MoveSpeed", 650.0)))
    else:
        out["CooldownMult"] = max(0.3, 1 - speed)
        out["CritChance"], out["CritDamage"] = min(1.0, g("CritChance", 0.05)), g("CritDamage", 0.5)
        out["Area"] = 1 + g("Area", 0.0)
        out["Duration"] = max(0.25, 1 + g("Duration", 0.0))
        out["Armor"] = min(0.8, max(0.0, g("Armor", 0.0)))
        out["Evasion"] = min(0.5, max(0.0, g("Evasion", 0.0)))
        out["Luck"] = max(0.0, g("Luck", 0.0))
        out["Projectiles"] = int(g("Projectiles", 0) + 0.001)
        out["MoveSpeed"] = max(200.0, g("MoveSpeed", 650.0))
    out["SoulBonus"] = min(1.0, max(0.0, g("SoulBonus", 0.0)))
    out["DamageMult"] = max(0.1, 1 + g("Damage", 0.0))
    return out


def add_stats(d, raw, owned):
    """owned: {upgrade id: power (picks x rarity multipliers)} -> adds Stat / Stat2 x power into raw (SQ::ApplyStat)"""
    for uid, power in owned.items():
        u = d.ups[uid]
        for stat, per in ((u["Stat"], num(u["StatPerLevel"])), (u.get("Stat2", ""), num(u.get("Stat2PerLevel")))):
            if stat:
                sign = -1.0 if stat == "DashCooldown" else 1.0   # SQ::ApplyStat subtracts dash cooldown
                raw[stat] = raw.get(stat, 0.0) + sign * per * power
    return raw


def reaper_for(d, weapon):
    kit = next((k for k in d.kits.values() if k["SignatureWeaponId"] == weapon), None)
    if kit:
        return next((r for r in d.reapers if r["Id"] == kit["ReaperId"]), d.reapers[0])
    return next((r for r in d.reapers if r["StartWeapon"] == weapon), d.reapers[0])


def is_signature(u):
    """Upgrades.csv Signature column (kits pass): a reaper's own weapon / evolution, never in the shared pool"""
    return (u.get("Signature") or "").strip() == "1"


def shared_weapons(d):
    """The shared level-up weapon pool (signature weapons excluded)"""
    return [u for u in d.ups.values() if u["Kind"] == "Weapon" and not is_signature(u)]


def default_start(d):
    """Main weapon for the model: Grim's signature weapon when kits exist (the old default was Scythe Sweep)"""
    grim = d.kits.get("Grim")
    return grim["SignatureWeaponId"] if grim and grim["SignatureWeaponId"] in d.ups else "ScytheSweep"


def item_pool(d):
    return [u for u in d.ups.values() if category(u) == "Item" and num(u["Weight"]) > 0]


_RAISE_CACHE = {}


def raise_table(d, pref):
    key = (id(d), tuple(pref))
    if key not in _RAISE_CACHE:
        _RAISE_CACHE[key] = _raise_table(d, pref)
    return _RAISE_CACHE[key]


def _raise_table(d, pref):
    """Expected raw stat gain of ONE Performance Review raise: 3 random stats from the shrine's Boosts, the player takes
    the first one in their preference list, at an expected rarity (Common-up roll, LEGENDARY_RAISE_SHARE golden)."""
    import itertools
    shrine = next((r for r in read_csv("Shrines.csv") if r["Id"] == "PerformanceReview"), None)
    if not shrine:
        return {}
    boosts = [(p.split(":")[0], num(p.split(":")[1])) for p in shrine["Boosts"].split("|") if ":" in p]
    exp_r = sum(p * m for p, m in zip(rarity_odds(d), STAT_MULT))
    mult = (1 - LEGENDARY_RAISE_SHARE) * exp_r + LEGENDARY_RAISE_SHARE * STAT_MULT[3]
    combos = list(itertools.combinations(boosts, 3))
    out = {}
    for c in combos:
        best = min(c, key=lambda b: pref.index(b[0]) if b[0] in pref else 99)
        out[best[0]] = out.get(best[0], 0.0) + best[1] * mult / len(combos)
    return out


class Build:
    def __init__(self, d, start_weapon, picks, chest_picks=0.0, plan=None, raises=None, rules=None):
        self.d = d
        self.rules = rules or ("new" if is_new_data(d) else "old")
        self.plan = dict(PLANS["typical"], **(PLANS.get(plan, {}) if isinstance(plan, str) else (plan or {})))
        slots_w = int(d.t("WeaponSlots", 4)) if self.rules == "new" else 6
        slots_t = int(d.t("TomeSlots", 4)) if self.rules == "new" else 6
        exp_w = sum(p * w for p, w in zip(rarity_odds(d), WEAPON_POWER))
        exp_r = sum(p * m for p, m in zip(rarity_odds(d), STAT_MULT))
        start = d.ups[start_weapon]
        evo = next((u for u in d.ups.values() if u["Kind"] == "Evolution" and u["EvolvesFrom"] == start_weapon), None)
        tome_order = []
        if evo and evo["RequiresRelic"] and self.plan.get("evolve", True):
            tome_order.append(evo["RequiresRelic"])   # the player learns the recipe: the evolution tome first
        for t in self.plan["tomes"]:
            if t not in tome_order and t in d.ups:
                tome_order.append(t)
        tome_order = tome_order[:slots_t]
        tmax = {t: int(num(d.ups[t]["MaxLevel"], 5)) for t in tome_order}
        wmax = int(num(start["MaxLevel"], 8))
        # Kits pass: a signature main weapon sits outside the WeaponSlots shared slots (signature + WeaponSlots weapons)
        self.signature = is_signature(start)
        max_others = max(0, min(int(self.plan["weapons"]), slots_w) - (0 if self.signature else 1))
        self.level = picks + 1   # team level (one pick per level-up)
        self.kit = kit_for_weapon(d, start_weapon) if self.signature else None
        self.main = {"id": start_weapon, "level": 1, "power": 1.0}
        self.others = []          # generic weapons: [level, power]
        self.tomes = {}           # id -> [level, power]
        self.evolved = False
        self.fallback_picks = 0
        acc, wpicks, tpicks = 0.0, 0, 0
        for _ in range(picks):
            if evo and not self.evolved and self.main["level"] >= wmax and evo["RequiresRelic"] in self.tomes:
                self.evolved = True   # evolution card (always Legendary) replaces the main weapon in its slot
                self.main = {"id": evo["Id"], "level": 1, "power": self.main["power"]}
                continue
            main_room = not self.evolved and self.main["level"] < wmax
            weapon_room = main_room or len(self.others) < max_others or any(o[0] < 8 for o in self.others)
            tome_room = len(self.tomes) < len(tome_order) or any(v[0] < tmax[t] for t, v in self.tomes.items())
            if not weapon_room and not tome_room:
                self.fallback_picks += 1   # gold / XP / heal filler cards: no power
                continue
            acc += self.plan["weapon_share"]
            if (acc >= 1.0 and weapon_room) or not tome_room:
                acc = max(0.0, acc - 1.0)
                wpicks += 1
                low = [o for o in self.others if o[0] < 8]
                if len(self.others) < max_others and (wpicks in (2, 4, 7, 10) or (not main_room and not low)):
                    self.others.append([1, 1.0 + exp_w])
                elif main_room and (wpicks % 2 == 1 or not low):
                    self.main["level"] += 1
                    self.main["power"] += exp_w
                elif low:
                    o = min(low, key=lambda x: x[0])
                    o[0] += 1
                    o[1] += exp_w
                elif main_room:
                    self.main["level"] += 1
                    self.main["power"] += exp_w
            else:
                tpicks += 1
                fresh = [t for t in tome_order if t not in self.tomes]
                room = [t for t, v in self.tomes.items() if v[0] < tmax[t]]
                if fresh and (tpicks in (1, 2, 4, 6) or not room):
                    self.tomes[fresh[0]] = [1, exp_r]
                elif room:
                    t = min(room, key=lambda x: self.tomes[x][0])
                    self.tomes[t][0] += 1
                    self.tomes[t][1] += exp_r
        # Items: chest + Lost Property rolls, random by Weight at the chest's luck (expected value); MaxStacks respected
        self.items = {}
        exp_c = sum(p * m for p, m in zip(rarity_odds(d, d.t("ChestLuckBonus", 0.25)), STAT_MULT))
        if self.rules == "new":
            pool = item_pool(d)
            wsum = sum(num(u["Weight"]) for u in pool) or 1.0
            for u in pool:
                stacks = chest_picks * num(u["Weight"]) / wsum
                cap = int(num(u.get("MaxStacks"), 0))
                if cap > 0:
                    stacks = min(stacks, cap)
                if stacks > 0:
                    self.items[u["Id"]] = stacks * exp_c
        else:
            # old rule: chest relic rolls levelled the build's relics (5 picks max each)
            left = chest_picks
            while left > 1e-6:
                step = min(1.0, left)
                left -= step
                fresh = [r for r in tome_order if r not in self.tomes]
                room = [r for r in self.tomes if self.tomes[r][1] < 5 * exp_r - 1e-6]
                if fresh and len(self.tomes) < 6 and (not room or len(self.tomes) < 4):
                    self.tomes[fresh[0]] = [1, exp_c * step]
                elif room:
                    r = min(room, key=lambda x: self.tomes[x][1])
                    self.tomes[r][1] = min(5 * exp_r, self.tomes[r][1] + exp_c * step)
        # Shrine raises (Performance Review): scale with item rolls (both come from walking the map)
        if raises is None:
            raises = chest_picks * SHRINE_RAISES_PER_NIGHT / 6.5 if self.rules == "new" else 0.0
        self.raises = raises
        self.relics = {t: v[1] for t, v in self.tomes.items()}   # older callers: id -> power
        self.relics.update(self.items)
        self.recompute()

    def raw_stats(self, extra=None):
        d = self.d
        r = reaper_for(d, self.main["id"] if not self.evolved else
                       next(u["EvolvesFrom"] for u in d.ups.values() if u["Id"] == self.main["id"]))
        raw = {"MaxHealth": num(r["MaxHealth"], 120), "MoveSpeed": num(r["MoveSpeed"], 650), "CarryCap": num(r["CarryCap"], 50),
               "Magnet": num(r["Magnet"], 300), "DashCooldown": num(r["DashCooldown"], 2.5), "CritChance": 0.05, "CritDamage": 0.5}
        add_stats(d, raw, {t: v[1] for t, v in self.tomes.items()})
        add_stats(d, raw, self.items)
        if self.raises > 0:
            for stat, amount in raise_table(d, self.plan["raise_pref"]).items():
                raw[stat] = raw.get(stat, 0.0) + amount * self.raises
        for k, v in (extra or {}).items():
            raw[k] = raw.get(k, 0.0) + v
        return raw

    def recompute(self, extra=None):
        """stats (ASQCharacter::RecomputeStats + SQ::FinalizeStats)"""
        s = finalize_stats(self.d, self.raw_stats(extra), self.rules)
        self.stats = s
        self.damage_mult = s["DamageMult"]
        self.cooldown_mult = s["CooldownMult"]
        self.crit_chance = s["CritChance"]
        self.crit_damage = s["CritDamage"]
        self.area = s["Area"]
        self.projectiles = s["Projectiles"]
        self.duration = s["Duration"]
        self.max_hp = s.get("MaxHealth", 120)
        self.armor = s["Armor"]
        self.evasion = s["Evasion"]
        return self

    def crit_avg(self):
        return 1 + self.crit_chance * self.crit_damage

    def hit(self, u, level, power):
        """Non-crit damage of one hit"""
        return upgrade_damage(u, level, power) * self.damage_mult

    def boss_dps(self, u, level, power):
        beh = u["Behaviour"]
        dmg = self.hit(u, level, power) * self.crit_avg()
        cd = max(0.08, upgrade_cooldown(u, level) * self.cooldown_mult)
        count = upgrade_count(u, level, power)
        if beh == "Turret":
            alive = min(count, 10 * self.duration / cd)
            return dmg * alive / (0.6 * self.cooldown_mult) * 0.6
        if beh == "Projectile":
            hits = 1 + 0.25 * (count + self.projectiles - 1)
        elif beh == "Orbit":
            hits = 0.35 * min(count, 4)
        elif beh == "Drop":
            hits = 0.5 * count
        elif beh == "Beam":
            hits = 0.8 * min(2, count + self.projectiles)
        else:
            hits = BOSS_HITS.get(beh, 1.0) or 1.0
        return dmg * hits / cd

    def carry_cap(self):
        cap = 50
        for rid, power in self.relics.items():
            u = self.d.ups[rid]
            for stat, per in ((u["Stat"], num(u["StatPerLevel"])), (u["Stat2"], num(u["Stat2PerLevel"]))):
                if stat == "CarryCap":
                    cap += int(per * power)
        return cap

    def aoe_dps(self, u, level, power):
        """Damage per second spread over the horde (rough enemies-hit per activation by behaviour)"""
        beh = u["Behaviour"]
        dmg = self.hit(u, level, power) * self.crit_avg()
        cd = max(0.08, upgrade_cooldown(u, level) * self.cooldown_mult)
        count = upgrade_count(u, level, power)
        area = self.area
        reach = area_reach_factor(self.d, upgrade_radius(u, level, power) * area ** 0.5)
        if beh in ("Arc", "Nova"):
            targets = min(14, 5 * area * (upgrade_radius(u, level, power) / 300.0) ** 2 * reach)
        elif beh == "Aura":
            targets = min(10, 3 * area * (upgrade_radius(u, level, power) / 250.0) ** 2 * reach)
        elif beh == "Projectile":
            targets = (count + self.projectiles) * min(2, 1 + level // 3)
        elif beh == "Orbit":
            targets = 0.6 * count
        elif beh == "Drop":
            # Remake pass: a drop lands ON one of the nearest enemies (ASQCharacter::FireWeapon) and hits everything within its
            # radius, so it uses the same crowd density as Arc / Nova (5 enemies per 300 cm circle) plus the target itself.
            # (The old flat 2.5 per drop ignored the radius: Rubber Stamp r140 was overrated, Gravequake r320 underrated.)
            targets = count * min(14, 1 + 5 * area * (upgrade_radius(u, level, power) / 300.0) ** 2 * reach)
        elif beh == "Chain":
            targets = count + self.projectiles
        elif beh == "Beam":
            targets = 3 * (count + self.projectiles)
        elif beh == "Turret":
            alive = min(count, 10 * self.duration / cd)
            return dmg * alive / (0.6 * self.cooldown_mult)
        else:
            targets = 1
        return dmg * max(1.0, targets) / cd

    def horde_dps(self, generic, kit=True):
        total = self.aoe_dps(self.main_def(), self.main["level"], self.main["power"])
        for level, power in self.others:
            total += statistics.mean(self.aoe_dps(u, level, power) for u in generic)
        if kit and self.kit:
            k = kit_power(self.d, self.kit, self, total, None)
            total = (total + k["horde_add"]) * k["self_mult"]
        return total

    def main_def(self):
        return self.d.ups[self.main["id"]]

    def main_hit(self):
        return self.hit(self.main_def(), self.main["level"], self.main["power"])

    def max_hit(self):
        """Biggest single number this build can show (crit included)"""
        best = self.main_hit() * (1 + self.crit_damage)
        return best

    def single_target_dps(self, generic, kit=True):
        total = self.boss_dps(self.main_def(), self.main["level"], self.main["power"])
        for level, power in self.others:
            total += statistics.mean(self.boss_dps(u, level, power) for u in generic)
        if kit and self.kit:
            k = kit_power(self.d, self.kit, self, None, total)
            total = (total + k["boss_add"]) * k["self_mult"] * k["boss_mult"]
        return total


# ---------------------------------------------------------------- the run timeline
def simulate(d, night_seconds, players, nights=3, step=60.0, start_weapon="ScytheSweep"):
    """Yields (night, minute, team_level, cumulative_kills_per_player, banked_this_night, collected_this_night)"""
    xp = 0.0
    level = 1
    need = xp_to_next(d, 1, players)
    kills = 0.0
    out = []
    for night in range(nights):
        t = 0.0
        souls = 0.0
        night_kills = 0.0
        while t < night_seconds - 1e-6:
            dt = min(step, night_seconds - t)
            progress = (t + dt * 0.5) / night_seconds
            rate = spawn_rate(d, progress, players)
            mix = enemy_mix(d, night, progress)
            k = rate * dt * KILL_SHARE
            kills += k
            night_kills += k
            xp += k * sum(num(e["XP"]) * w for e, w in mix) * XP_PICKUP
            souls += k * sum(num(e["Souls"]) * w for e, w in mix) * SOUL_PICKUP
            while xp >= need:
                xp -= need
                level += 1
                need = xp_to_next(d, level, players)
            t += dt
            out.append((night, t / 60.0, level, kills / players, souls))
        sack = Build(d, start_weapon, level - 1).carry_cap()
        cap = players * sack * night_seconds / TRIP_SECONDS  # one full sack per trip per reaper
        banked = min(souls * (1 - SOUL_LOSS), cap)
        out[-1] = out[-1][:4] + (banked,)
        xp += d.t("BossXP", 40)
        while xp >= need:
            xp -= need
            level += 1
            need = xp_to_next(d, level, players)
    return out


def quota(d, night, players):
    solo = d.t("SoloQuotaMult", 0.5)
    mult = solo + (1 - solo) * min(1, max(0, (players - 1) / 3))
    base = d.t("QuotaNight%d" % (night + 1), 300 + 250 * night)
    return max(10, round(base * mult))


def boss_for(d, night):
    """Graveyard story: lieutenants on nights 1-2 (by Night column), the biome legend on night 3"""
    if night < 2:
        return next(b for b in d.bosses if int(num(b["Night"])) == night + 1)
    legend = d.biomes[0]["BossId"]
    return next(b for b in d.bosses if b["Id"] == legend)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--night", type=float, default=DESIGN_NIGHT, help="night length in seconds (default: release 360)")
    ap.add_argument("--csv-night", action="store_true", help="use Tuning.csv NightSeconds")
    ap.add_argument("--start", default="", help="main (starting) weapon id (default: Grim's signature weapon)")
    ap.add_argument("--legacy", action="store_true", help="also print the retired 3-night sections 1-15")
    ap.add_argument("--quick", action="store_true", help="unified report without the sensitivity pass, fewer pacing seeds")
    args = ap.parse_args()
    d = Data()
    if not args.legacy:
        unified_report(d, quick=args.quick)
        return
    args.start = args.start or default_start(d)
    night_s = d.t("NightSeconds", 360) if args.csv_night else args.night
    d.tune["NightSeconds"] = night_s  # the game scales HP / prices by NightSeconds / BalanceNightSeconds
    generic = shared_weapons(d)
    starters = sorted({r["StartWeapon"] for r in d.reapers if r["StartWeapon"] in d.ups})
    tick_kinds = ("Aura", "Orbit")
    hit_starters = [w for w in starters if d.ups[w]["Behaviour"] not in tick_kinds]

    print("=" * 100)
    print("SOUL QUOTA BALANCE MODEL  | night length %.0f s | main weapon %s | starters with real hits: %d"
          % (night_s, args.start, len(hit_starters)))
    print("Assumes: kill %.0f%% of spawns, collect %.0f%% XP / %.0f%% souls, %.0f%% of picks on weapons, boss uptime %.0f%%"
          % (KILL_SHARE * 100, XP_PICKUP * 100, SOUL_PICKUP * 100, WEAPON_PICK_SHARE * 100, BOSS_UPTIME * 100))
    print("Enemy ramp: HP x(1+%.2f/min) x(1+%.2f/extra player), damage x(1+%.2f/night)x(1+%.3f/min)"
          % (d.t("EnemyHealthPerMinute", 0.1), d.t("EnemyHealthPerPlayer", 0.35), d.t("EnemyDamagePerNight", 0.3), d.t("EnemyDamagePerMinute", 0.03)))

    # ---------------- per minute: level, hits to kill, enemy damage
    print("\n-- 1. Horde vs an on-curve reaper (per player count; minute = minute into the night) --")
    print("%-3s %-5s %-3s %-5s %-7s %-8s %-8s %-8s %-6s %-7s %-7s %-8s %-6s %-5s"
          % ("P", "Night", "Min", "Lvl", "MainHit", "Med.Hit", "AvgEnHP", "BruteHP", "Hits", "MedHits", "EnDmg", "HitsToDn", "MaxNum", "Clear"))
    summary = {}
    for players in (1, 2, 4):
        timeline = simulate(d, night_s, players)
        for night, minute, level, _kills, _s in timeline:
            nmin = minute - 0  # minute into this night (simulate resets per night)
            show = abs(nmin - round(nmin)) < 1e-6 and (round(nmin) in (1, 3) or abs(nmin - night_s / 60) < 1e-6)
            if not show:
                continue
            b = Build(d, args.start, level - 1, chest_picks_at(d, night_s, players, night, nmin * 60 / night_s))
            mix = enemy_mix(d, night, min(1.0, nmin * 60 / night_s))
            avg_hp = sum(enemy_hp(d, e, night, nmin, players) * w for e, w in mix if e["Behaviour"] != "Elite")
            avg_hp /= max(1e-6, sum(w for e, w in mix if e["Behaviour"] != "Elite"))
            brute = next(e for e in d.enemies if e["Id"] == "Brute")
            brute_hp = enemy_hp(d, brute, night, nmin, players)
            meds = []
            for w in hit_starters:
                bw = Build(d, w, level - 1, chest_picks_at(d, night_s, players, night, nmin * 60 / night_s))
                meds.append(bw.main_hit())
            med_hit = statistics.median(meds)
            hits = avg_hp / max(0.01, b.main_hit())
            med_hits = avg_hp / max(0.01, med_hit)
            en_dmg = sum(num(e["Damage"]) * w for e, w in mix if e["Behaviour"] in ("Chase", "Hound", "Splitter", "Spitter")) \
                / max(1e-6, sum(w for e, w in mix if e["Behaviour"] in ("Chase", "Hound", "Splitter", "Spitter")))
            en_dmg *= enemy_damage_mult(d, night, nmin)
            max_hp, armor = b.max_hp, b.armor   # items/tomes pass: from the build's finalized stats
            hits_down = max_hp / max(0.01, en_dmg * (1 - armor))
            # Clear = team horde DPS / HP arriving per second (spawns x average HP); >= 1 means the team keeps up
            inflow = spawn_rate(d, min(1.0, nmin * 60 / night_s), players) * avg_hp
            clear = b.horde_dps(generic) * players / max(1e-6, inflow)
            print("%-3d %-5d %-3.0f %-5d %-7.0f %-8.0f %-8.0f %-8.0f %-6.1f %-7.1f %-7.1f %-8.1f %-6.0f %-5.1f"
                  % (players, night + 1, nmin, level, b.main_hit(), med_hit, avg_hp, brute_hp, hits, med_hits, en_dmg, hits_down, b.max_hit(), clear))
            summary.setdefault(players, []).append((night, nmin, level, hits, med_hits, b.max_hit(), en_dmg, avg_hp, clear))

    # ---------------- bosses
    print("\n-- 2. Bosses (fought at the night's end level; HP x(1 + %.2f x extra players); uptime %.0f%%) --"
          % (d.t("BossHealthPerPlayer", 0.75), BOSS_UPTIME * 100))
    print("%-16s %-3s %-7s %-9s %-10s %-8s %-7s %-10s" % ("Boss", "P", "Lvl", "HP", "TeamDPS", "TTK s", "Target", "HP@target"))
    boss_rows = []
    for players in (1, 2, 4):
        timeline = simulate(d, night_s, players)
        ends = [row for row in timeline if abs(row[1] - night_s / 60) < 1e-6]
        legends = [boss_for(d, 0), boss_for(d, 1)] + [b for b in d.bosses if int(num(b["Night"])) == 3]
        for boss in legends:
            night = min(2, int(num(boss["Night"])) - 1)
            level = ends[night][2]
            b = Build(d, args.start, level - 1, chest_picks_before_bosses(d, night_s, players)[night])
            dps = b.single_target_dps(generic) * players * BOSS_UPTIME
            hp = num(boss["Health"]) * (1 + d.t("BossHealthPerPlayer", 0.75) * (players - 1)) * night_scale(d) * econ_mult(d, "EconomyBossHealth", night)  # ASQBoss::InitBoss
            ttk = hp / max(0.01, dps)
            target = num(boss["TargetSeconds"])
            solo_equiv = dps * target / (1 + d.t("BossHealthPerPlayer", 0.75) * (players - 1)) / night_scale(d) / econ_mult(d, "EconomyBossHealth", night)
            print("%-16s %-3d %-7d %-9.0f %-10.0f %-8.0f %-7.0f %-10.0f" % (boss["Id"], players, level, hp, dps, ttk, target, solo_equiv))
            boss_rows.append((boss["Id"], players, ttk, target, solo_equiv))

    # ---------------- shop
    print("\n-- 3. Death's Office: surplus souls vs prices (price x(1+%.2f/buy this visit) x(1+%.2f/night) x(1+%.2f/extra player)) --"
          % (d.t("ShopPricePerBuy", 0.75), d.t("ShopPricePerNight", 0.5), d.t("ShopPricePerPlayer", 0.75)))
    print("%-3s %-5s %-7s %-7s %-8s %-8s %-6s %s" % ("P", "Night", "Banked", "Quota", "Surplus", "1stBuy", "Buys", "Prices of buy 1..5 (cheapest item)"))
    for players in (1, 2, 4) if d.shop else ():   # Shop.csv retired (Nov 9 missions): nothing to price
        timeline = simulate(d, night_s, players)
        ends = [row for row in timeline if abs(row[1] - night_s / 60) < 1e-6]
        for night in range(2):  # shops after nights 1 and 2
            banked = ends[night][4]
            q = quota(d, night, players)
            surplus = max(0, banked - q) + (d.t("QuotaMetBonus", 25) if banked >= q else 0)
            # greedy: buy the cheapest item each time (prices of all items climb after any buy)
            left, buys, seq = surplus, 0, []
            while buys < 12:
                prices = [shop_price(d, it, night, buys, players) for it in d.shop]
                p = min(prices)
                seq.append(p)
                if p > left:
                    break
                left -= p
                buys += 1
            print("%-3d %-5d %-7.0f %-7d %-8.0f %-8d %-6d %s" % (players, night + 1, banked, q, surplus, seq[0], buys, seq[:5]))
    print("   Item base costs: " + ", ".join("%s %s" % (it["Name"], it["Cost"]) for it in d.shop))

    # ---------------- meta
    print("\n-- 4. Soul coins (meta) per run vs reaper unlock costs --")
    if LEGACY_NIGHT_RUN is False:
        print("   Retired (Nov 9 missions): one meta currency, banked GOLD - see sections 13-15.")
    locked = sorted(int(num(r["UnlockCost"])) for r in d.reapers if num(r["UnlockCost"]) > 0)
    avg_cost = statistics.mean(locked) if locked else 0
    for players in (1, 2, 4) if LEGACY_NIGHT_RUN else ():
        timeline = simulate(d, night_s, players)
        ends = [row for row in timeline if abs(row[1] - night_s / 60) < 1e-6]
        per_kill = max(1, d.t("KillsPerPay", 10))
        # SQPlayerController: PayPerNight x nights cleared + own kills / KillsPerPay (+ PayWinBonus on a win)
        win = d.t("PayPerNight", 40) * 3 + ends[2][3] / per_kill + d.t("PayWinBonus", 150)
        loss = d.t("PayPerNight", 40) * 2 + ends[2][3] / per_kill   # wiped by the night-3 boss: 2 nights cleared, all 3 nights of kills
        avg = WIN_RATE * win + (1 - WIN_RATE) * loss
        print("P%d: win %4.0f coins, loss %4.0f, average (%.0f%% wins) %4.0f -> %.1f runs per unlock (avg locked reaper %.0f, %d locked, all %d)"
              % (players, win, loss, WIN_RATE * 100, avg, avg_cost / max(1, avg), avg_cost, len(locked), sum(locked)))

    # ---------------- verdict
    print("\n-- 5. Verdict vs goals --")
    for players, rows in summary.items():
        early = [r for r in rows if r[0] == 0]
        late = [r for r in rows if r[0] == 2]
        print("P%d: hits-to-kill (main / median starter) night 1 %.1f-%.1f / %.1f-%.1f (goal 1-3), night 3 %.1f-%.1f / %.1f-%.1f (goal 2-5); biggest number %.0f"
              % (players, min(r[3] for r in early), max(r[3] for r in early), min(r[4] for r in early), max(r[4] for r in early),
                 min(r[3] for r in late), max(r[3] for r in late), min(r[4] for r in late), max(r[4] for r in late), max(r[5] for r in rows)))
    for players, rows in summary.items():
        print("P%d: horde clear ratio %.1f - %.1f (>= 1 keeps up with the spawns; KILL_SHARE assumes ~%.2f)"
              % (players, min(r[8] for r in rows), max(r[8] for r in rows), KILL_SHARE))
    worst = max(boss_rows, key=lambda r: abs(r[2] / r[3] - 1))
    print("Boss TTK / target range: %.2f - %.2f (worst %s P%d)"
          % (min(r[2] / r[3] for r in boss_rows), max(r[2] / r[3] for r in boss_rows), worst[0], worst[1]))
    print("Suggested Bosses.csv Health (mean over 1/2/4 players): " + ", ".join(
        "%s %d" % (bid, round(statistics.mean(r[4] for r in boss_rows if r[0] == bid), -2)) for bid in dict.fromkeys(r[0] for r in boss_rows)))
    economy_sections(d, night_s)
    items_tomes_sections(d, night_s)
    kits_section(d, night_s)
    missions_sections(d)   # Nov 9 2026: the run is one mission stage (sections 13-15)



# ================================================================ ECONOMY + META PACING (Nov 7 economy pass)
# Mirrors: ASQHordeManager::KillEnemy (gold coins), ASQGameState::ChestPrice, ASQGameMode (Appointment Bell, Final Swarm),
# SQQuests (Quests.csv / Modifiers.csv), ASQPlayerState::ClientQuestRunEnd (soul coins x modifier multiplier).
# ---------------------------------------------------------------- ASSUMPTIONS (economy / meta; replace with playtest numbers)
LEGACY_NIGHT_RUN = False   # Nov 9 2026 missions: the old 3-night run sections (4, 6, 8) are retired; True prints them again
GOLD_PICKUP = 0.85          # share of dropped gold coins collected
RING_AT = 0.95              # standard play rings the Appointment Bell at ~11:50 PM (share of the night)
LOBBY_SECONDS = 25.0        # lobby + ready-up + countdown, per run
DEATH_INTO_BOSS = 0.6       # a run lost at a boss ends this far into the fight
QUOTA_MET_SHARE = 0.85      # cleared nights whose quota was met
DAMAGE_PER_NIGHT = 450.0    # health a reaper loses per night (healed back)
DASHES_PER_NIGHT = 45.0
FINALCALL_BANKS_PER_NIGHT = 1.0
PLATEAUS_PER_RUN = 2.0
SHRINES_PER_RUN = 4.0       # world agent's shrines (credited through ASQPlayerState::CreditQuest)
SLIDE_KILLS_PER_RUN = 8.0   # movement agent's slide (credited through ASQPlayerState::CreditQuest)
OFFICE_BUYS_PER_VISIT = 3.5
P_EARLY_BELL = 0.25         # runs (after the 3rd) where the player rings before 11 PM
P_LOW_HP_NIGHT = 0.08       # cleared nights finished below 10% HP
P_NO_HIT_NIGHT = 0.02
P_PERFECT_WIN = 0.15        # wins with every quota met and nobody downed
P_FAST_BOSS = 0.15          # bosses beaten within 2 min of the bell (needs a strong build)
P_BLITZ_BOSS = 0.02
P_SWARM_BOSS = 0.2          # bosses fought after midnight (Final Swarm)
SWARM_SECONDS = 40.0        # Final Swarm seconds survived when it happens
# A new player's first runs: number = nights cleared before dying, W = win. After these, the last 8 repeat (~62% wins).
OUTCOMES = ["1", "1", "2", "W", "2", "W", "W", "2", "W", "W", "1", "W", "W", "2", "W", "W"]
MOD_FROM_RUN = 15           # from this run on the player turns challenge modifiers on (1, then 2, then 3)
ENDLESS_EVERY = 8           # from run 20, every Nth run is an Endless attempt
CHECKPOINTS = [15, 30, 45, 60, 120, 300, 600]   # minutes of play to report


def night_mix_shares(d, night, steps=12):
    """Spawn-weighted share of each enemy type over a whole night"""
    acc = {}
    for k in range(steps):
        prog = (k + 0.5) / steps
        rate = spawn_rate(d, prog, 1)
        for e, w in enemy_mix(d, night, prog):
            acc[e["Id"]] = acc.get(e["Id"], 0.0) + rate * w
    tot = sum(acc.values()) or 1.0
    return {k: v / tot for k, v in acc.items()}


def nights_match(text, night):
    """Timeline.csv Nights: * = every night, 2 = night 2 only, 3+ = night 3 and later (night is 0-based here)"""
    text = (text or "*").strip()
    if text == "*": return True
    if text.endswith("+"): return night + 1 >= int(num(text[:-1], 1))
    return night + 1 == int(num(text, 1))


def chest_price(d, opened):                    # ASQGameState::ChestPrice (per reaper; free opens don't count)
    base = d.t("ChestBasePrice", 25) * (1 + d.t("ChestPriceK", 1) * opened) ** d.t("ChestPriceExp", 1.2)
    return int(round(min(base, d.t("ChestPriceCap", 1500)) * night_scale(d)))


def gold_night(d, night, team_kills, players):
    """(gold per reaper, free chests per reaper, elites, crowned, team kills) for one night"""
    shares = night_mix_shares(d, min(night, 2))
    ups = {e["Id"]: e for e in d.enemies}
    elite_share = sum(v for k, v in shares.items() if ups[k]["Behaviour"] == "Elite")
    avg_xp = sum(num(ups[k]["XP"]) * v for k, v in shares.items() if ups[k]["Behaviour"] != "Elite") / max(1e-6, 1 - elite_share)
    growth = 1 + d.t("GoldPerNight", 0.6) * night
    coins = team_kills * (1 - elite_share) * d.t("GoldDropChance", 0.3) * avg_xp * d.t("GoldPerXP", 1) * growth * GOLD_PICKUP
    elites = team_kills * elite_share
    crowned = elites * d.t("CrownedChance", 0.35)
    elite_gold = ((elites - crowned) + crowned * d.t("CrownedGoldMult", 2)) * d.t("EliteGold", 20) * growth * GOLD_PICKUP
    share = 1.0 / (1 + d.t("PlayerSpawnRateBonus", 0.35) * (players - 1)) if d.t("GoldShareCoop", 1) > 0.5 else 1.0 / players
    per = (coins + elite_gold) * share + (2 * d.t("MinibossGold", 60) + d.t("BossGold", 100)) * growth
    minis = sum(1 for r in d.timeline if r["Kind"] == "Miniboss" and num(r.get("FreeChest")) > 0 and nights_match(r["Nights"], night))
    free = crowned * d.t("CrownedChestChance", 0.5) / players + minis + 1    # crowned chests (first come), a miniboss chest + the boss roll (each reaper)
    return per, free, elites, crowned


def run_economy(d, night_s, players, ring=RING_AT):
    """Per night: gold per reaper, paid chests opened (greedy, gold carries over), free chests, prices"""
    timeline = simulate(d, night_s * ring, players)
    ends = [row for row in timeline if abs(row[1] - night_s * ring / 60) < 1e-6]
    out = []
    gold = 0.0
    opened = 0
    prev_kills = 0.0
    for night in range(3):
        team_kills = (ends[night][3] - prev_kills) * players
        prev_kills = ends[night][3]
        per, free, elites, crowned = gold_night(d, night, team_kills, players)
        gold += per
        bought = []
        while gold >= chest_price(d, opened):
            p = chest_price(d, opened)
            gold -= p
            bought.append(p)
            opened += 1
        out.append(dict(night=night, gold=per, paid=len(bought), prices=bought, free=free, elites=elites, crowned=crowned,
                        left=gold, level=ends[night][2], kills=team_kills / players, opened=opened))
    return out


def boss_ttk_with_chests(d, night_s, players, start, chest_picks_by_night, ring=RING_AT):
    timeline = simulate(d, night_s * ring, players)
    ends = [row for row in timeline if abs(row[1] - night_s * ring / 60) < 1e-6]
    generic = [u for u in d.ups.values() if u["Kind"] == "Weapon"]
    rows = []
    for night in range(3):
        boss = boss_for(d, night)
        level = ends[night][2]
        hp = num(boss["Health"]) * (1 + d.t("BossHealthPerPlayer", 0.75) * (players - 1)) * night_scale(d) * econ_mult(d, "EconomyBossHealth", night)
        no_chest = Build(d, start, level - 1).single_target_dps(generic) * players * BOSS_UPTIME
        with_chest = Build(d, start, level - 1, chest_picks_by_night[night]).single_target_dps(generic) * players * BOSS_UPTIME
        rows.append((boss["Id"], level, hp / max(0.01, no_chest), hp / max(0.01, with_chest), num(boss["TargetSeconds"])))
    return rows


def chest_picks_before_bosses(d, night_s, players, ring=RING_AT):
    """Item rolls a reaper has had before each night's boss: paid + crowned + miniboss chests (the boss roll comes after its
    fight) + their share of the Lost Property desks (items/tomes pass)"""
    out, acc = [], 0.0
    lost_property = LOST_PROPERTY_ITEMS / players if is_new_data(d) else 0.0   # items/tomes pass: Lost Property desks (first come)
    for e in run_economy(d, night_s, players, ring):
        acc += e["paid"] + e["free"] - 1 + lost_property
        out.append(acc)
        acc += 1
    return out


def run_length_minutes(d, night_s, nights_cleared, won, players=1, ring=RING_AT, swarm_s=0.0, night_mult=1.0):
    """Release run length: lobby + nights to the bell (+ Final Swarm) + boss fights + Death's Office + end screen"""
    ttk = [r[3] for r in boss_ttk_with_chests(d, night_s, players, default_start(d), chest_picks_before_bosses(d, night_s, players, ring), ring)]
    t = LOBBY_SECONDS
    played = 3 if won else nights_cleared + 1
    for n in range(played):
        t += night_s * night_mult * ring + swarm_s
        if n < nights_cleared or won:
            t += ttk[n]
            if n < played - 1:
                t += d.t("ShopSeconds", 30) * 0.7     # most crews ready up before the office timer runs out
        else:
            t += ttk[n] * DEATH_INTO_BOSS
    return (t + d.t("EndScreenSeconds", 12)) / 60.0


def parse_reward(text):
    out = []
    for part in (text or "").split("|"):
        if ":" in part:
            k, v = part.split(":", 1)
            out.append((k.strip(), v.strip()))
    return out


def meta_pacing(d, night_s):
    """A new solo player plays back to back. Returns the event log [(minute, text)] and checkpoints."""
    quests = [q for q in d.quests if q.get("Id")]
    reapers = {r["Id"]: r for r in d.reapers}
    mods = [m for m in d.modifiers if m.get("Id")]
    biomes = [b["Id"] for b in d.biomes]
    totals, bests = {}, {}
    done = set()
    hired = {rid for rid, r in reapers.items() if num(r["UnlockCost"]) <= 0}
    items = set()
    contracts = set()
    won_with = set()
    coins = 0.0
    lifetime_coins = 0.0
    clock = 0.0
    log = []
    econ = run_economy(d, night_s, 1)
    sim1 = simulate(d, night_s * RING_AT, 1)
    ends = [row for row in sim1 if abs(row[1] - night_s * RING_AT / 60) < 1e-6]
    shares = [night_mix_shares(d, n) for n in range(3)]
    elite_frac = []
    for n in range(3):
        elite_frac.append(sum(v for k, v in shares[n].items() if next(e for e in d.enemies if e["Id"] == k)["Behaviour"] == "Elite"))
    pay_kill = max(1, d.t("KillsPerPay", 40))
    run = 0
    biome_idx = 0

    def value(stat, run_stats):
        if stat == "QuestsDone": return len(done)
        if stat == "ReapersHired": return len(hired)
        if stat == "Contracts": return len(contracts)
        if stat == "ReaperWins": return len(won_with)
        return None

    def check(run_stats, minute):
        nonlocal coins, lifetime_coins
        changed = True
        while changed:
            changed = False
            for q in quests:
                if q["Id"] in done: continue
                st, tgt, mode = q["Stat"], num(q["Target"]), q["Mode"]
                v = value(st, run_stats)
                if v is None:
                    v = totals.get(st, 0) if mode == "Total" else max(bests.get(st, 0), run_stats.get(st, 0))
                if v >= tgt:
                    done.add(q["Id"])
                    changed = True
                    what = []
                    for k, val in parse_reward(q["Reward"]):
                        if k == "Coins":
                            coins += num(val); lifetime_coins += num(val); what.append("%s coins" % val)
                        elif k == "Reaper" and val not in hired:
                            hired.add(val); what.append("reaper " + val)
                        elif k == "Item":
                            items.add(val); what.append("item " + val)
                    log.append((minute, "quest", q["Name"], ", ".join(what)))

    def buy(minute):
        nonlocal coins
        while True:
            locked = sorted((num(r["UnlockCost"]), rid) for rid, r in reapers.items() if rid not in hired)
            if not locked or locked[0][0] > coins: return
            cost, rid = locked[0]
            coins -= cost
            hired.add(rid)
            log.append((minute, "hire", reapers[rid]["Name"], "%d coins" % cost))

    while clock < 200 * 60 and (len(done) < len(quests) or len(hired) < len(reapers)):
        outcome = OUTCOMES[run] if run < len(OUTCOMES) else OUTCOMES[len(OUTCOMES) - 8 + (run - len(OUTCOMES)) % 8]
        endless = run >= 20 and run % ENDLESS_EVERY == 0
        won = outcome == "W"
        cleared = 3 if won else int(outcome)
        nmods = 0 if run < MOD_FROM_RUN else (1 if run < 30 else (2 if run < 60 else 3))
        active = [mods[(run + k) % len(mods)] for k in range(nmods)] if mods else []
        mult = min(d.t("ModifierCoinCap", 3), 1 + sum(num(m["CoinMult"], 1) - 1 for m in active))
        night_mult = 1.0
        for m in active:
            if num(m.get("NightMult"), 0) > 0: night_mult *= num(m["NightMult"])
        mine = sorted(hired, key=lambda r: (r in won_with, r))[0]   # play a reaper you haven't won with yet
        run_stats = {}
        nights_played = 3 if won else cleared + 1
        if endless:
            nights_played = min(30, 6 + run // 20)
            cleared = nights_played - 1
            won = False
        t0 = clock
        mins = run_length_minutes(d, night_s, cleared if not endless else 2, won, ring=RING_AT, night_mult=night_mult)
        if endless:
            mins = mins / 3.0 * nights_played
        per_night = mins / max(1, nights_played)

        def add(st, v):
            run_stats[st] = run_stats.get(st, 0) + v
            totals[st] = totals.get(st, 0) + v

        add("TeamLevel", 1)
        for n in range(nights_played):
            ni = min(n, 2)
            kills = ends[ni][3] - (ends[ni - 1][3] if ni > 0 else 0)
            add("Kills", kills)
            for eid, share in shares[ni].items():
                add("Kill_" + eid, kills * share)
            elites = kills * elite_frac[ni]
            add("EliteKills", elites)
            add("CrownedKills", elites * d.t("CrownedChance", 0.35))
            add("MinibossKills", 2)
            e = econ[ni]
            add("GoldEarned", e["gold"])
            paid = e["paid"] if n < 3 else 3
            add("ChestsOpened", paid + e["free"])
            add("FreeChests", e["free"])
            odds = rarity_odds(d, d.t("ChestLuckBonus", 0.25))
            add("LegendaryChest", (paid + e["free"]) * odds[3])
            lvl_now = ends[ni][2] if n < 3 else ends[2][2] + 6 * (n - 2)
            add("TeamLevel", lvl_now - run_stats.get("TeamLevel", 0))
            add("DamageTaken", DAMAGE_PER_NIGHT)
            add("Dashes", DASHES_PER_NIGHT)
            add("FinalCallBanks", FINALCALL_BANKS_PER_NIGHT)
            add("SoulsBanked", 150 + 120 * ni)
            run_stats["MaxCarried"] = max(run_stats.get("MaxCarried", 0), (160 if mine == "Collector" else 50 + 12 * min(5, run // 3)))
            cleared_this = n < cleared or (won and n < 3)
            if cleared_this:
                add("NightsCleared", 1)
                add("BossKills", 1)
                add("QuotaMet", QUOTA_MET_SHARE)
                add("BossNoDefiance", QUOTA_MET_SHARE)
                add("LowHPNight", P_LOW_HP_NIGHT)
                add("NoHitNights", P_NO_HIT_NIGHT)
                if run >= 6:
                    add("BossFast", P_FAST_BOSS)
                    add("BossBlitz", P_BLITZ_BOSS)
                    add("SwarmBoss", P_SWARM_BOSS)
                    add("SwarmKills", P_SWARM_BOSS * 150)
                    run_stats["SwarmSeconds"] = max(run_stats.get("SwarmSeconds", 0), SWARM_SECONDS * min(1.0, P_SWARM_BOSS * 5) * (1 + run / 40.0))
                if n < 2:
                    add("OfficeBuys", OFFICE_BUYS_PER_VISIT)
            check(run_stats, t0 + per_night * (n + 1))
        add("RunsPlayed", 1)
        add("PlateausClimbed", PLATEAUS_PER_RUN)
        add("ShrinesUsed", SHRINES_PER_RUN)
        add("SlideKills", SLIDE_KILLS_PER_RUN)
        if run >= 3 and (run % 4 == 0):
            add("EarlyBell", 1)
        if endless:
            run_stats["EndlessNights"] = nights_played - 1
        if won:
            add("RunsWon", 1)
            add("Win_" + mine, 1)
            won_with.add(mine)
            add("PerfectRun", P_PERFECT_WIN)
            if mins < d.t("FastWinMinutes", 20) * night_scale(d) or (run >= 20 and run % 12 == 0):
                add("FastWin", 1)   # a deliberate rush run (bell at 10:30 PM every night) now and then
            if biome_idx < len(biomes):
                contracts.add(biomes[biome_idx]); biome_idx += 1
            for m in active:
                add("ModWin_" + m["Id"], 1)
            run_stats["ModsInWin"] = len(active)
        run_stats["DifficultyPct"] = 20 + (40 if run >= 30 and run % 5 == 0 else 0)
        pay = d.t("PayPerNight", 60) * (cleared if not endless else nights_played - 1) + run_stats["Kills"] / pay_kill \
            + (d.t("PayWinBonus", 150) if won else 0)
        pay *= mult
        coins += pay
        lifetime_coins += pay
        clock = t0 + mins
        for k, v in run_stats.items():
            bests[k] = max(bests.get(k, 0), v)
        check(run_stats, clock)
        log.append((clock, "run", "Run %d %s%s" % (run + 1, "WIN" if won else ("Endless" if endless else "loss (%d nights)" % cleared),
                                                    (" mods x%.2f" % mult) if active else ""), "+%d coins (%.1f min)" % (pay, mins)))
        buy(clock)
        run += 1
    return log, done, hired, items, quests, reapers, lifetime_coins


def economy_sections(d, night_s):
    print("\n" + "=" * 100)
    print("ECONOMY + META PACING (night length %.0f s; test NightSeconds only shrink prices / HP by NightSeconds / BalanceNightSeconds)" % night_s)
    # ---------------- 6. run length (old 3-night run; retired by the Nov 9 missions, kept behind LEGACY_NIGHT_RUN)
    if not LEGACY_NIGHT_RUN:
        print("\n-- 6. Run length: retired (Nov 9 missions: one stage per run, see section 13) --")
    else:
        print("\n-- 6. Run length at release values (BalanceNightSeconds %.0f; bell at %.0f%% of the night unless noted) --"
              % (d.t("BalanceNightSeconds", 360), RING_AT * 100))
        for label, ring, swarm in (("rush (bell at 10:30 PM)", d.t("PortalMinProgress", 0.5), 0.0), ("standard", RING_AT, 0.0),
                                   ("farm (60 s of Final Swarm each night)", 1.0, 60.0)):
            print("  %-40s win %4.1f min   loss after 2 nights %4.1f min" % (label, run_length_minutes(d, night_s, 3, True, 1, ring, swarm),
                                                                               run_length_minutes(d, night_s, 2, False, 1, ring, swarm)))
        for label, players in (("solo", 1), ("4 players", 4)):
            picks = chest_picks_before_bosses(d, night_s, players)
            ttk = boss_ttk_with_chests(d, night_s, players, default_start(d), picks)
            print("  Boss TTK %s (s, level-ups only -> + %s item rolls; boss HP x EconomyBossHealth): %s" % (label, "/".join("%.0f" % p for p in picks), ", ".join("%s %.0f->%.0f (target %.0f)" % (r[0], r[2], r[3], r[4]) for r in ttk)))
    # ---------------- 7. gold vs chests
    print("\n-- 7. Gold income vs chest prices (per reaper; price = %.0f x (1 + %.2f x opened)^%.2f, cap %.0f; shared purse in co-op) --"
          % (d.t("ChestBasePrice", 25), d.t("ChestPriceK", 1), d.t("ChestPriceExp", 1.2), d.t("ChestPriceCap", 1500)))
    print("   Prices of chest 1..12: %s" % [chest_price(d, n) for n in range(12)])
    print("%-3s %-5s %-6s %-8s %-6s %-6s %-7s %-8s %s" % ("P", "Night", "Level", "Gold", "Paid", "Free", "Elites", "Crowned", "Prices paid"))
    for players in (1, 2, 4):
        for e in run_economy(d, night_s, players):
            print("%-3d %-5d %-6d %-8.0f %-6d %-6.1f %-7.1f %-8.1f %s" % (players, e["night"] + 1, e["level"], e["gold"], e["paid"], e["free"],
                                                                     e["elites"], e["crowned"], e["prices"]))
    econ = run_economy(d, night_s, 1)
    paid = sum(e["paid"] for e in econ)
    free = sum(e["free"] for e in econ)
    print("   Solo run: %d paid + %.0f free chests = items (+ ~%.0f Lost Property items, + ~%d level-up picks for %d weapon + %d tome slots)."
          % (paid, free, 3 * LOST_PROPERTY_ITEMS, simulate(d, night_s, 1)[-1][2], d.t("WeaponSlots", 4), d.t("TomeSlots", 4)))
    # ---------------- 8. meta pacing
    if not LEGACY_NIGHT_RUN:
        print("\n-- 8. Old 3-night meta pacing: retired (Nov 9 missions; see section 15) --")
        return
    print("\n-- 8. META PACING: a new solo player (release values; outcomes %s...; modifiers from run %d) --" % (" ".join(OUTCOMES[:8]), MOD_FROM_RUN))
    locked = sorted(int(num(r["UnlockCost"])) for r in d.reapers if num(r["UnlockCost"]) > 0)
    print("   Reaper hire costs: %s (sum %d)" % (locked, sum(locked)))
    log, done, hired, items, quests, reapers, lifetime = meta_pacing(d, night_s)
    unlock_kinds = ("quest", "hire")
    for cp in CHECKPOINTS:
        evts = [e for e in log if e[0] <= cp and e[1] in unlock_kinds]
        reaper_n = sum(1 for e in evts if e[1] == "hire" or "reaper" in e[3])
        item_n = sum(1 for e in evts if "item" in e[3])
        runs = sum(1 for e in log if e[0] <= cp and e[1] == "run")
        print("  %4d min (%2d runs): %2d quests, %2d reapers hired, %2d items unlocked | latest: %s"
              % (cp, runs, sum(1 for e in evts if e[1] == "quest"), reaper_n, item_n,
                 "; ".join("%s (%s)" % (e[2], e[3]) for e in evts[-3:]) if evts else "-"))
    first = next((e for e in log if e[1] in unlock_kinds), None)
    print("   First unlock: %.1f min (%s: %s)" % (first[0], first[2], first[3]) if first else "   First unlock: none")
    hour1 = [e for e in log if e[0] <= 60 and e[1] in unlock_kinds]
    print("   Hour 1: %d unlocks / quest rewards" % len(hour1))
    # steadiness: the longest gap between unlocks through 10 hours
    times = [0.0] + [e[0] for e in log if e[1] in unlock_kinds and e[0] <= 600] + [600.0]
    gaps = [b - a for a, b in zip(times, times[1:])]
    print("   Longest gap between unlocks in the first 10 h: %.0f min; unlocks in hours 1-10: %s"
          % (max(gaps), [sum(1 for e in log if e[1] in unlock_kinds and h * 60 < e[0] <= (h + 1) * 60) for h in range(10)]))
    last_reaper = max((e[0] for e in log if e[1] == "hire" or (e[1] == "quest" and "reaper" in e[3])), default=0)
    last_quest = max((e[0] for e in log if e[1] == "quest"), default=0)
    print("   All %d reapers: %.1f h | %d / %d quests done, last at %.1f h | lifetime soul coins %.0f"
          % (len(hired), last_reaper / 60, len(done), len(quests), last_quest / 60, lifetime))
    missing = [q["Id"] for q in quests if q["Id"] not in done]
    if missing:
        print("   Not done within 200 h: %s" % missing)
    print("   Quest completion by hour: %s" % [sum(1 for e in log if e[1] == "quest" and e[0] <= h * 60) for h in (1, 2, 5, 10, 20, 30, 40, 50)])

# ================================================================ ITEMS + TOMES (Nov 7 items/tomes pass; Docs/ItemsAndTomes.md)
# Mirrors: ASQCharacter::CanTakeUpgrade (slots), ASQPlayerState::RollOffers (cards), ASQChest::GrantChestRoll (items),
# SQ::FinalizeStats (caps + curves). Value of one pick = % gain of the metric it serves, at an on-curve build at the end
# of night 1 / 2 / 3 (solo): DPS = half horde DPS + half single-target DPS; EHP = (max HP + 30 s of healing) / damage taken;
# Utility = the stat's own job (economy / movement), scaled by the ELASTICITY guesses below.
ELASTICITY = {"CarryCap": 0.5, "Magnet": 0.3, "SoulBonus": 1.0, "BankSpeed": 0.05, "MoveSpeed": 1.0, "DashCooldown": 0.3}
EHP_WEIGHT = 0.75        # 1% more effective HP counts as 0.75% power (damage wins runs; Megabonk tier lists agree)
AREA_DENSITY = 0.5       # share of extra weapon coverage that finds enemies (the horde DPS model caps targets per hit)
VALUE_CREW = 2           # AllyRegen is valued in a 2-reaper crew: power = average of your EHP gain and your ally's
ROLE_OF = {"Damage": "DPS", "Cooldown": "DPS", "Area": "DPS", "CritChance": "DPS", "CritDamage": "DPS", "Projectiles": "DPS",
           "Duration": "DPS", "Thorns": "DPS", "TurretBonus": "DPS", "MaxHealth": "EHP", "Armor": "EHP", "Evasion": "EHP",
           "Regen": "EHP", "PickupHeal": "EHP", "AllyRegen": "EHP"}
AREA_BEHAVIOURS = ("Arc", "Nova", "Aura", "Drop")       # "area" role; the rest are "single" (projectiles, chains, beams, orbits, turrets)
ROLE_WEIGHT = {"area": 0.7, "single": 0.3}              # weapon score = w x horde DPS + (1 - w) x single-target DPS (each vs all-weapon median)
GOOD_BAND = (0.77, 1.3)                                  # value vs the median: below = weak / dead, above = dominant


def _generic(d):
    return shared_weapons(d)


def ref_build(d, night_s, night, players=1, plan=None, start=None):
    """On-curve build at the end of a night (the level and item rolls the economy model gives)"""
    timeline = simulate(d, night_s, players)
    ends = [row for row in timeline if abs(row[1] - night_s / 60) < 1e-6]
    picks = chest_picks_before_bosses(d, night_s, players)[night]
    return Build(d, start or default_start(d), ends[night][2] - 1, picks, plan=plan), ends[night][2]


def metrics(b, generic, extra=None):
    b.recompute(extra)
    s = b.stats
    horde = b.horde_dps(generic) + s.get("Thorns", 0.0) * CONTACTS_PER_SECOND
    boss = b.single_target_dps(generic)
    heal = (s.get("Regen", 0.0) + s.get("PickupHeal", 0.0) * SOULS_PER_SECOND) * REGEN_WINDOW
    ehp = (b.max_hp + heal) / max(0.05, (1 - b.armor) * (1 - b.evasion))
    ally = s.get("AllyRegen", 0.0) * REGEN_WINDOW
    return dict(horde=horde, boss=boss, ehp=ehp, ally=ally, stats=s)


def pick_value(d, b, generic, u, mult=1.0):
    """Gain of one level / stack of upgrade u at rarity mult, as % of each metric. Returns (dps%, ehp%, util%, total%)"""
    base = metrics(b, generic)
    extra = {}
    for stat, per in ((u["Stat"], num(u["StatPerLevel"])), (u.get("Stat2", ""), num(u.get("Stat2PerLevel")))):
        if stat:
            extra[stat] = extra.get(stat, 0.0) + (-1.0 if stat == "DashCooldown" else 1.0) * per * mult
    if "Projectiles" in extra:
        # whole-number stat: compare against the build's current total (ApplyStat floors the SUM)
        cur = sum(num(d.ups[t]["StatPerLevel"]) * p for t, p in b.relics.items() if d.ups[t]["Stat"] == "Projectiles")
        extra["Projectiles"] = int(cur + extra["Projectiles"] + 0.001) - int(cur + 0.001) + 1e-9
    area_add = extra.pop("Area", 0.0)
    dur_add = extra.pop("Duration", 0.0)
    new = metrics(b, generic, extra)
    b.recompute()
    dps = 50 * (new["horde"] / base["horde"] - 1) + 50 * (new["boss"] / base["boss"] - 1)
    s_area, s_dur = base["stats"]["Area"], base["stats"]["Duration"]
    if area_add:
        # coverage grows with radius squared; the share of horde DPS from area weapons gets AREA_DENSITY of it
        area_share = 0.5 * (b.main_def()["Behaviour"] in AREA_BEHAVIOURS) + 0.5 * sum(
            1 for u2 in generic if u2["Behaviour"] in AREA_BEHAVIOURS) / max(1, len(generic))
        a1 = min(d.t("AreaMax", 3.0), s_area + area_add)
        dps += 50 * area_share * ((a1 / s_area) ** 2 - 1) * AREA_DENSITY
    if dur_add:
        # turret life / slows / flight: weapons with a slow or a turret get the duration, at half value
        dur_share = sum(1 for u2 in generic if num(u2["Slow"]) > 0 or u2["Behaviour"] == "Turret") / max(1, len(generic))
        d1 = min(d.t("DurationMax", 3.0), s_dur + dur_add)
        dps += 50 * dur_share * (d1 / s_dur - 1)
    own = 100 * (new["ehp"] / base["ehp"] - 1)
    ally = 100 * (new["ally"] - base["ally"]) / max(1.0, b.max_hp)
    ehp = EHP_WEIGHT * ((own + ally * (VALUE_CREW - 1)) / VALUE_CREW if ally > 0 else own)
    util = 0.0
    s0, s1 = base["stats"], new["stats"]
    for stat, e in ELASTICITY.items():
        a0, a1 = s0.get(stat, 0.0), s1.get(stat, 0.0)
        if stat == "DashCooldown":
            util += 100 * e * (a0 / max(0.4, a1) - 1)
        elif stat == "BankSpeed":
            util += 100 * e * ((1 + a1) / (1 + a0) - 1)
        elif stat == "SoulBonus":
            util += 100 * e * ((1 + a1) / (1 + a0) - 1)
        elif a0 > 0:
            util += 100 * e * (a1 / a0 - 1)
    if s1.get("Luck", 0) != s0.get("Luck", 0):
        r0 = sum(p * m for p, m in zip(rarity_odds(d, s0["Luck"]), STAT_MULT))
        r1 = sum(p * m for p, m in zip(rarity_odds(d, s1["Luck"]), STAT_MULT))
        util += 100 * (r1 / r0 - 1)                       # % stronger future cards and chest items
    if extra.get("ChestKey"):
        k0 = sum(num(d.ups[t]["StatPerLevel"]) * p for t, p in b.relics.items() if d.ups[t]["Stat"] == "ChestKey")
        k1 = k0 + extra["ChestKey"]
        util += 100 * 0.5 * (k1 / (k1 + 1) - k0 / (k0 + 1))   # half of item rolls are paid chests: % more items
    if extra.get("ExtraJumps"):
        util += 3.0 * extra["ExtraJumps"]                  # judgement: a jump ~ a small movement upgrade
    if extra.get("TurretBonus"):
        turrets = sum(1 for u2 in generic if u2["Behaviour"] == "Turret") / max(1, len(generic))
        dps += 100 * 0.25 * turrets * extra["TurretBonus"]  # a maxed turret weapon keeps ~4-5 turrets up: +1 ~ +25%
    return dps, ehp, util, dps + ehp + util


def weapon_table(d, b, generic):
    """Every weapon at max level and every evolution, with the reference build's stats. Power = expected rarity power."""
    exp_w = sum(p * w for p, w in zip(rarity_odds(d), WEAPON_POWER))
    rows = []
    for u in generic:
        lv = int(num(u["MaxLevel"], 8))
        p = 1 + exp_w * lv
        rows.append(dict(id=u["Id"], beh=u["Behaviour"], horde=b.aoe_dps(u, lv, p), boss=b.boss_dps(u, lv, p),
                         role="area" if u["Behaviour"] in AREA_BEHAVIOURS else "single", power=p, lv=lv))
    mh = statistics.median(r["horde"] for r in rows)
    mb = statistics.median(r["boss"] for r in rows)
    for r in rows:
        r["hn"], r["bn"] = r["horde"] / mh, r["boss"] / mb
        w = ROLE_WEIGHT[r["role"]]
        r["score"] = w * r["hn"] + (1 - w) * r["bn"]     # role-weighted: area weapons are mostly judged on the horde
    evos = []
    for e in d.ups.values():
        if e["Kind"] != "Evolution" or is_signature(e):
            continue
        base = next(r for r in rows if r["id"] == e["EvolvesFrom"])
        eh, eb = b.aoe_dps(e, 1, base["power"]), b.boss_dps(e, 1, base["power"])
        w = ROLE_WEIGHT[base["role"]]
        key_ratio = w * eh / base["horde"] + (1 - w) * eb / base["boss"]
        evos.append(dict(id=e["Id"], base=base["id"], tome=e["RequiresRelic"], horde=eh / base["horde"], boss=eb / base["boss"],
                         spike=key_ratio, both=0.5 * (eh / base["horde"] + eb / base["boss"])))
    return rows, evos


def items_tomes_sections(d, night_s, detail=True):
    print("\n" + "=" * 100)
    print("ITEMS + TOMES (Docs/ItemsAndTomes.md): slots %d weapons / %d tomes; items stack; caps: crit %.0f%%, cooldown floor %.0f%%, "
          "area %.1fx, armor x/(x+%.1f) max %.0f%%, evasion max %.0f%%, luck soft cap %.1f"
          % (d.t("WeaponSlots", 4), d.t("TomeSlots", 4), 100 * d.t("CritChanceMax", 1), 100 * d.t("CooldownFloor", 0.4),
             d.t("AreaMax", 3), d.t("ArmorK", 1), 100 * d.t("ArmorMax", 0.75), 100 * d.t("EvasionMax", 0.4), d.t("LuckSoftCap", 2)))
    generic = _generic(d)
    tomes = [u for u in d.ups.values() if category(u) == "Tome"]
    items = item_pool(d)
    refs = [ref_build(d, night_s, n) for n in range(3)]
    # ---------------- 9. value per pick
    print("\n-- 9. Value of ONE Common pick (tome level / item stack) on the on-curve build, end of night 1 / 2 / 3 (solo).")
    print("   %% gain: DPS (half horde, half single target), EHP ((HP + 30 s healing) / damage taken), Utility (economy, movement).")
    print("   Rarity multiplies every row the same way (x1.4 / 1.9 / 2.6), so the ratio to the median holds at every rarity.")
    summary = {}
    for label, group in (("TOMES", tomes), ("ITEMS", items)):
        vals = {u["Id"]: [pick_value(d, rb, generic, u) for rb, _lv in refs] for u in group}
        med = [statistics.median(v[n][3] for v in vals.values()) for n in range(3)]
        print("\n  %s (median total %%: %s)" % (label, " / ".join("%.1f" % m for m in med)))
        print("  %-17s %-11s %-18s %-26s %-6s %s" % ("Id", "Stat", "Night1 D/E/U", "Night2 D/E/U   Night3 tot", "xMed", "Flag"))
        for u in sorted(group, key=lambda x: -vals[x["Id"]][1][3]):
            v = vals[u["Id"]]
            ratio = v[1][3] / max(1e-6, med[1])
            flag = "DOMINANT" if ratio > GOOD_BAND[1] else ("weak" if ratio < GOOD_BAND[0] else "")
            if ratio < 0.5:
                flag = "DEAD"
            summary[u["Id"]] = (label, ratio, v)
            print("  %-17s %-11s %4.1f/%4.1f/%4.1f    %4.1f/%4.1f/%4.1f  %5.1f      %-6.2f %s"
                  % (u["Id"], u["Stat"], v[0][0], v[0][1], v[0][2], v[1][0], v[1][1], v[1][2], v[2][3], ratio, flag))
    # ---------------- 10. weapons + evolutions
    rb, lv = refs[2]
    rows, evos = weapon_table(d, rb, generic)
    print("\n-- 10. Weapons at max level with the night-3 build (level %d). Score = 0.7 horde + 0.3 single-target DPS for area weapons"
          " (Arc / Nova / Aura / Drop), 0.3 / 0.7 for the rest; each vs the all-weapon median (goal: 0.8-1.2)" % lv)
    print("  %-15s %-10s %-6s %-8s %-8s %-6s %-6s %s" % ("Weapon", "Behaviour", "Role", "HordeDPS", "BossDPS", "Score", "H/B", "Flag"))
    for r in sorted(rows, key=lambda x: (x["role"], -x["score"])):
        flag = "" if 0.8 <= r["score"] <= 1.2 else ("STRONG" if r["score"] > 1.2 else "WEAK")
        print("  %-15s %-10s %-6s %-8.0f %-8.0f %-6.2f %4.1f/%-4.1f %s" % (r["id"], r["beh"], r["role"], r["horde"], r["boss"], r["score"], r["hn"], r["bn"], flag))
    print("\n  Evolutions (same power as the maxed weapon; spike = role-weighted DPS ratio vs the maxed base weapon; goal +40-70%):")
    print("  %-19s %-15s %-17s %-7s %-7s %s" % ("Evolution", "From", "Tome", "Spike", "H / B", "Flag"))
    for e in sorted(evos, key=lambda x: -x["spike"]):
        flag = "" if 1.4 <= e["spike"] <= 1.7 else ("TOO BIG" if e["spike"] > 1.7 else "TOO SMALL")
        print("  %-19s %-15s %-17s +%-5.0f%% %4.2f/%-4.2f %s" % (e["id"], e["base"], e["tome"], 100 * (e["spike"] - 1), e["horde"], e["boss"], flag))
    # ---------------- 11. builds
    main_id = default_start(d)
    print("\n-- 11. Build archetypes (end of each night, solo unless noted; %s main, kit included). DPS horde / single, EHP, boss TTK vs target" % main_id)
    print("  %-9s %-3s %-5s %-5s %-24s %-8s %-8s %-6s %-7s %-6s" % ("Build", "P", "Night", "Lvl", "Weapons (lv) / tomes", "Horde", "Single", "EHP", "TTK", "Target"))
    plans = [("typical", 1), ("weapons", 1), ("tomes", 1), ("crit", 1), ("support", 4), ("typical", 4)]
    for plan, players in plans:
        timeline = simulate(d, night_s, players)
        ends = [row for row in timeline if abs(row[1] - night_s / 60) < 1e-6]
        cps = chest_picks_before_bosses(d, night_s, players)
        for night in range(3):
            b = Build(d, main_id, ends[night][2] - 1, cps[night], plan=plan)
            m = metrics(b, generic)
            boss = boss_for(d, night)
            hp = num(boss["Health"]) * (1 + d.t("BossHealthPerPlayer", 0.75) * (players - 1)) * night_scale(d) * econ_mult(d, "EconomyBossHealth", night)
            team = m["boss"] * players * BOSS_UPTIME if plan != "support" else (m["boss"] + 3 * Build(d, main_id, ends[night][2] - 1, cps[night]).single_target_dps(generic)) * BOSS_UPTIME
            wl = "%s%d+%s" % ("E" if b.evolved else "", b.main["level"], ",".join(str(o[0]) for o in b.others))
            tl = ",".join("%s%d" % (t[:2], v[0]) for t, v in b.tomes.items())
            print("  %-9s %-3d %-5d %-5d %-24s %-8.0f %-8.0f %-6.0f %-7.0f %-6.0f" % (plan, players, night + 1, ends[night][2], (wl + " " + tl)[:24],
                                                                                 m["horde"], m["boss"], m["ehp"], hp / max(1, team), num(boss["TargetSeconds"])))
    return summary, rows, evos


# ================================================================ REAPER KITS (Nov 9 kits pass; Docs/Kits.md; Content/Data/Kits.csv)
# Mirrors USQAbilityComponent (SQAbility.cpp). Every reaper: signature weapon (Upgrades.csv Signature 1, outside the shared
# WeaponSlots), a passive, an E (team level AbilityUnlockE) and an R (AbilityUnlockR). E / R evolve with the signature's paired tome.
#   ability hit = Damage x (1 + AbilityDamagePerLevel x (team level - 1)) x damage mult x crit x (EvoDamage when evolved)
#   cooldown    = Cooldown x EvoCooldown x CooldownMult ^ AbilityHasteExp        area = Radius x EvoRadius x Area ^ AbilityAreaExp
# ---------------------------------------------------------------- ASSUMPTIONS (kits; replace with playtest numbers)
ABILITY_UPTIME = {"E": 0.8, "R": 0.85}   # share of the cooldown actually used (aim, travel, saving R for a crowd)
DENSITY = 5.0 / (3.14159 * 300.0 ** 2)   # enemies per cm^2 around a reaper in a fight (the Arc model: 5 inside 300)
TARGET_CAP = {"E": 16, "R": 32}          # most enemies one hit can find
PULL_DENSITY = 1.6                       # a pull (vortex) packs the crowd: x targets for the hits that follow
CONTROL_K = 0.02                         # control value: % power per enemy-second slowed (x0.4), frozen (x1), knocked (x0.6 s), pulled (x0.8 s)
ECON_WEIGHT = 0.25                       # economy utility counts a quarter (souls don't kill anything)
ECON_CAP = 15.0                          # % cap of one passive's economy utility
REVIVE_VALUE = 6.0                       # % power of a revive effect (co-op insurance)
DASH_EVERY = 2.6                         # seconds between dashes in a fight
CARRY_SHARE = 0.5                        # average sack fullness (Collections)
MISSING_HP = 0.35                        # average missing health (Spite)
GALLOP_SHARE = 0.5                       # share of the time Neckless is at full gallop
# Role targets for clear speed (horde DPS incl. kit) vs the median reaper; utility roles clear a little slower
ROLE_CLEAR = {"Striker": 1.08, "Berserker": 1.08, "Speed": 1.04, "All-rounder": 1.0, "Scout": 1.0, "Wildcard": 1.0, "Greed": 1.02,
              "Time": 0.96, "Summoner": 1.0, "Control": 0.96, "Mobility": 1.0, "Hauler": 0.98, "Defender": 0.92, "Healer": 0.88,
              "Reviver": 0.88, "Banker": 0.92, "Mobile bank": 0.88, "Economy": 0.96, "Fetcher": 0.96}
CLEAR_BAND = 0.15
CLEAR_W_HORDE = 0.6                      # clear speed = 0.6 x horde DPS + 0.4 x single-target DPS (elites, minibosses, bosses), each vs the median


def kit_for_weapon(d, weapon_id):
    u = d.ups.get(weapon_id)
    if u and u["Kind"] == "Evolution":
        weapon_id = u["EvolvesFrom"]
    return next((k for k in d.kits.values() if k["SignatureWeaponId"] == weapon_id), None)


def kill_rate(level, players=1):
    """kills per second per reaper (solo spawn ramp 4 -> 22/s at KILL_SHARE; levels 1-48 cover nights 1-3)"""
    return min(15.0, 2.0 + 0.25 * level)


def _f(k, col, dflt=0.0):
    return num(k.get(col), dflt)


def ability_numbers(d, k, slot, b, evolved):
    g = lambda c, dflt=0.0: _f(k, "%s_%s" % (slot, c), dflt)
    evo = evolved
    area = b.area ** d.t("AbilityAreaExp", 0.5)
    a = dict(shape=k["%s_Shape" % slot], effects=[e for e in (k["%s_Effect" % slot] or "").split("|") if e],
             dmg=g("Damage") * (g("EvoDamage", 1) if evo else 1), final=g("FinalDamage") * (g("EvoDamage", 1) if evo else 1),
             radius=g("Radius") * (g("EvoRadius", 1) if evo else 1) * area, rng=g("Range"),
             width=g("Width") * ((g("EvoRadius", 1) if evo else 1) if k["%s_Shape" % slot] in ("Line", "Dash", "Cone") else 1),
             count=g("Count") + (g("EvoCount") if evo else 0), pierce=g("Pierce"),
             cd=max(1.0, g("Cooldown", 10) * (g("EvoCooldown", 1) if evo else 1) * b.cooldown_mult ** d.t("AbilityHasteExp", 0.5)),
             dur=g("Duration") * (g("EvoDuration", 1) if evo else 1) * b.duration, tick=max(0.1, g("Tick", 0.5)),
             ev=g("EffectValue") * (g("EvoDamage", 1) if evo else 1), et=g("EffectTime") * b.duration,
             evo_effect=(k.get("%s_EvoEffect" % slot) or "") if evo else "", evo_value=g("EvoValue"), evo_time=g("EvoTime") * b.duration,
             mod=(k.get("%s_Mod" % slot) or ""), mod_value=g("ModValue"), mod_time=g("ModTime"))
    return a


def ability_hits(a, slot):
    """(horde targets per cast, boss hits per cast, enemies affected for control) for one cast"""
    cap = TARGET_CAP[slot]
    pull = PULL_DENSITY if "Pull" in a["effects"] else 1.0
    circ = lambda r: min(cap, 3.14159 * r * r * DENSITY)
    sh = a["shape"]
    if sh in ("Circle", "Nova", "Global"):
        n = circ(a["radius"]) * (pull if sh != "Global" else 1.0)
        dmg_hits, boss = n, 1.0
        if sh == "Global":
            dmg_hits *= 1.3     # blasts around every reaper (2-crew average, overlapping)
    elif sh == "Cone":
        n = min(cap, 3.14159 * a["rng"] ** 2 * a["width"] / 360.0 * DENSITY)
        dmg_hits, boss = n, 0.8
    elif sh == "Line":
        ticks = max(1.0, a["dur"] / a["tick"]) if a["dur"] > 0 else 1.0
        n = min(cap, a["rng"] * 2 * a["width"] * DENSITY)
        dmg_hits, boss = n * ticks, 0.8 * ticks
    elif sh == "Dash":
        n = min(cap, a["rng"] * 2 * a["width"] * DENSITY)
        end = circ(a["radius"])
        dmg_hits, boss = n, 1.0
        a["_final_hits"] = end
        n += end
    elif sh == "Blink":
        n = 2 * circ(a["radius"])
        dmg_hits, boss = n, 0.6
    elif sh == "Zone":
        ticks = max(1.0, a["dur"] / a["tick"])
        n = circ(a["radius"]) * pull
        dmg_hits, boss = n * ticks, 0.7 * ticks
        a["_final_hits"] = n
    elif sh == "Volley":
        waves = max(1.0, a["dur"] / a["tick"] + 1) if a["dur"] > 0 else 1.0
        n = min(cap * 2, a["count"] * min(3.0, a["pierce"] + 1) * 0.7) * waves
        dmg_hits, boss = n, (1 + 0.25 * (a["count"] - 1) * min(1.0, a["width"] and 60.0 / a["width"])) * waves
    elif sh == "Barrage":
        n = a["count"] * min(8.0, circ(a["radius"])) * 0.6
        dmg_hits, boss = n, 0.35 * a["count"]
    elif sh == "Chain":
        n = a["count"]
        dmg_hits, boss = n, 1.0
    elif sh == "Totem":
        ticks = max(1.0, a["dur"] / a["tick"])
        n = a["count"] * ticks
        dmg_hits, boss = n, ticks
    elif sh == "Self":
        ticks = max(1.0, a["dur"] / a["tick"])
        n = circ(a["radius"])
        dmg_hits, boss = n * ticks, ticks
    else:
        n = dmg_hits = boss = 1.0
    return dmg_hits, boss, n


def ability_power(d, k, slot, b, evolved, passive_cd=1.0, echo=1.0):
    """{'horde','boss' (DPS), 'util' (% power), 'team_mult' (damage share given to the crew)} of one ability at build b"""
    unlock = d.t("AbilityUnlockE", 3) if slot == "E" else d.t("AbilityUnlockR", 6)
    out = dict(horde=0.0, boss=0.0, util=0.0, self_mult=1.0, team=0.0)
    if b.level < unlock:
        return out
    a = ability_numbers(d, k, slot, b, evolved)
    scale = 1 + d.t("AbilityDamagePerLevel", 0.05) * (b.level - 1)
    hit = scale * b.damage_mult * b.crit_avg()
    cd = max(0.35, a["cd"] * passive_cd)   # refunds can push a cast below the 1 s cooldown floor, not below the cast gap
    if slot == "R" and getattr(b, "ult_period", None):
        cd = max(0.35, b.ult_period * passive_cd)   # reap core: R charges from reaped souls + kills (USQAbilityComponent::AddUltCharge)
    up = ABILITY_UPTIME[slot] * echo
    horde_hits, boss_hits, affected = ability_hits(a, slot)
    out["horde"] = (a["dmg"] * hit * horde_hits + a["final"] * hit * a.get("_final_hits", affected if a["final"] else 0)) / cd * up
    out["boss"] = (a["dmg"] * hit * boss_hits + a["final"] * hit * (1.0 if a["final"] else 0)) / cd * up
    # Character pass (Nov 25) ability mods (USQAbilityComponent::QueueModFollowUps / CommitCast)
    mod, mv = a.get("mod", ""), a.get("mod_value", 0.0)
    if mod == "Detonate":
        # ModValue extra bursts of FinalDamage (or Damage) at the target, Radius (or 250)
        burst = a["final"] if a["final"] else a["dmg"]
        rr = a["radius"] if a["radius"] > 0 else 250.0
        nb = min(TARGET_CAP[slot], 3.14159 * rr * rr * DENSITY)
        out["horde"] += burst * hit * nb * mv / cd * up
        out["boss"] += burst * hit * mv * 0.8 / cd * up
    elif mod == "Recast":
        # the snap back hits again for ModValue x Damage in max(Radius, 220) and half damage along the way (used ~70% of casts)
        rr = max(a["radius"], 220.0)
        nb = min(TARGET_CAP[slot], 3.14159 * rr * rr * DENSITY)
        out["horde"] += 0.7 * a["dmg"] * mv * hit * nb * 1.3 / cd * up
        out["boss"] += 0.7 * a["dmg"] * mv * hit * 1.5 / cd * up
    elif mod == "Stacks":
        # chained casts: average stack about 1.5 of +ModValue each (needs targets every few seconds)
        out["horde"] *= 1 + 1.5 * mv * 0.8
        out["boss"] *= 1 + 1.5 * mv * 0.8
    elif mod == "Trap":
        # waits for a target: some traps go off late or on few enemies
        out["horde"] *= 0.85
        out["boss"] *= 0.6
    util = 0.0
    base_dur = a["dur"] if a["dur"] > 0 else a["et"]
    util += effect_util(a, slot, a["effects"], a["ev"], a["et"], base_dur, False, cd, up, affected, horde_hits, out)
    if a["evo_effect"]:
        util += effect_util(a, slot, [a["evo_effect"]], a["evo_value"], a["evo_time"], a["evo_time"] if a["evo_time"] > 0 else base_dur,
                            True, cd, up, affected, horde_hits, out)
    out["util"] = util
    return out


def effect_util(a, slot, eff, ev, et, dur, is_evo, cd, up, affected, horde_hits, out):
    """Utility % of one ability's effects (control, crew buffs, healing); damage-share buffs go into out['team'] / out['self_mult']"""
    ctrl = 0.0
    if "Slow" in eff:
        ctrl += 0.4 * et
    if "Freeze" in eff:
        ctrl += 1.0 * et
    if "Knockback" in eff:
        ctrl += 0.6
    if "Pull" in eff:
        ctrl += 0.8 * max(1.0, a["dur"])
    util = 100 * CONTROL_K * min(TARGET_CAP[slot], affected) * ctrl / cd * up
    crew = (1 + 0.8 * (VALUE_CREW - 1)) / VALUE_CREW   # buffs that reach allies: your share + a nearby ally's, averaged
    # Self shape: the caster's own Empower / Fortify are personal; Haste and evolved effects also reach reapers within Radius
    ally = a["shape"] != "Self" or "Haste" in eff or is_evo
    if "Empower" in eff:
        share = ev * min(1.0, dur / cd) * up
        if ally:
            out["team"] += share
            util += 100 * share * crew
        else:
            out["self_mult"] *= 1 + share
    if "Haste" in eff:
        util += 100 * ev * min(1.0, dur / cd) * up * 0.5 * (crew if ally else 1.0)
    if "Fortify" in eff:
        util += EHP_WEIGHT * 100 * (1 / max(0.2, 1 - ev) - 1) * min(1.0, dur / cd) * up * (crew if ally else 1.0)
    if "Heal" in eff:
        util += EHP_WEIGHT * 100 * ev * min(1.0, REGEN_WINDOW / cd) * crew
    if "Shield" in eff:
        util += EHP_WEIGHT * 100 * 0.7 * ev * min(1.0, REGEN_WINDOW / cd) * crew
    if "Lifesteal" in eff:
        util += EHP_WEIGHT * 100 * ev * horde_hits * 0.5 * min(1.0, REGEN_WINDOW / cd)
    if "Revive" in eff:
        util += REVIVE_VALUE
    if "Mark" in eff:
        util += 100 * ev * min(1.0, et / cd) * up * 0.5    # single target: half credit (boss fights)
        out["mark"] = out.get("mark", 0.0) + ev * min(1.0, et / cd) * up
    return util


def passive_power(d, k, b, kr):
    """Passive: horde / boss DPS added, self damage multiplier, E cooldown factor, echo factor, utility %"""
    pid = k["PassiveId"]
    p1, p2, p3, p4 = (_f(k, "PassiveP%d" % i) for i in (1, 2, 3, 4))
    hit = b.damage_mult * b.crit_avg()
    circ = lambda r: min(16.0, 3.14159 * r * r * DENSITY)
    out = dict(horde=0.0, boss=0.0, self_mult=1.0, e_cd=1.0, echo=1.0, util=0.0, boss_mult=1.0)
    if pid == "TeamDamage":
        upt = min(1.0, p3 * kr / max(1.0, p1))
        out["self_mult"] = 1 + p2 * upt
        out["util"] = 100 * p2 * upt * 0.5
    elif pid == "HitAndRun":
        speed = min(1 / max(0.1, 1 - p3), 1 + p1 * kr) if p3 > 0 else 1 + p1 * kr
        out["e_cd"] = 1 / speed
        out["util"] = 3.0
    elif pid == "SlowSouls":
        out["util"] = min(ECON_CAP, 100 * p1 * 0.4 / 1.3 * ECON_WEIGHT)
    elif pid == "Builder":
        alive = min(p3, p2 * kr / max(1.0, p1))
        dps = alive * p4 * hit / (0.6 * b.cooldown_mult)
        out["horde"], out["boss"] = dps, dps
    elif pid == "Lamplight":
        out["util"] = EHP_WEIGHT * 100 * p2 * REGEN_WINDOW / max(1.0, b.max_hp) * 0.9 + 100 * CONTROL_K * circ(p1) * 0.4
    elif pid == "DashStrike":
        dash_every = getattr(b, "dash_every", DASH_EVERY)
        hits = circ(180 * b.area ** 0.5) * 2
        out["horde"] = p1 * hit * hits / dash_every
        out["boss"] = p1 * hit / dash_every
        out["e_cd"] = 1 / (1 + min(p3, p2 * hits) / dash_every)
    elif pid == "OnTheJob":
        out["echo"] = 1 + p3 / max(1.0, p1)
    elif pid == "Fetch":
        out["util"] = min(ECON_CAP, 100 * 0.3 * (p1 / 250.0 - 1) * ECON_WEIGHT)
    elif pid == "Bulwark":
        out["util"] = EHP_WEIGHT * 100 * (1 / (1 - p2) - 1)
    elif pid == "Collections":
        out["self_mult"] = 1 + min(p2, p1 * 50 * CARRY_SHARE)
    elif pid == "Triage":
        out["util"] = EHP_WEIGHT * 100 * 0.35 * (p3 + 8 / max(1.0, b.max_hp)) * REGEN_WINDOW / max(0.5, p1) * 0.5
    elif pid == "Scorch":
        dash_every = getattr(b, "dash_every", DASH_EVERY)
        area = (1800 * p4 * 2 * p2 * b.area ** 0.5 + 3.14159 * p2 * p2) * DENSITY
        out["horde"] = p1 * hit * min(8.0, area) * p3 * b.duration / max(1.0, dash_every * 0.6)
        out["boss"] = p1 * hit * 0.4 * p3 / max(1.0, dash_every * 0.6)
    elif pid == "Lookout":
        upt = min(1.0, p4 / max(0.5, p1))
        out["boss_mult"] = 1 + p3 * upt
        out["util"] = 100 * p3 * upt * 0.5 * 0.5 + min(ECON_CAP, 2.0)
    elif pid == "Spite":
        out["self_mult"] = 1 + p1 * MISSING_HP
    elif pid == "Audit":
        out["horde"] = kr / max(1.0, p1) * p4 * hit * circ(p3 * b.area ** 0.5)
        out["util"] = min(ECON_CAP, 100 * p2 * kr / max(1.0, p1) / max(1.0, kr * 1.3) * ECON_WEIGHT)
    elif pid == "Flock":
        stacks = min(p1, kr * p2)
        out["horde"] = p4 * stacks * hit * 2 * circ(p3 * b.area ** 0.5)
        out["boss"] = p4 * stacks * hit * 2
    elif pid == "LastRites":
        out["util"] = REVIVE_VALUE
    elif pid == "MobileBank":
        out["util"] = min(ECON_CAP, 10.0)
    elif pid == "Seance":
        alive = min(p3, (p1 * kr + 0.05) * p2)
        dps = alive * p4 * hit / (0.6 * b.cooldown_mult)
        out["horde"], out["boss"] = dps, dps
    elif pid == "Stampede":
        out["horde"] = p1 * hit * circ(p2 * b.area ** 0.5) / max(0.05, p3) * GALLOP_SHARE
        out["boss"] = p1 * hit / max(0.05, p3) * GALLOP_SHARE * 0.3
    elif pid == "Stopwatch":
        out["util"] = 100 * CONTROL_K * circ(p2 * b.area ** 0.5) * p3 * b.duration / max(1.0, p1)
    return out


def kit_evolved(d, k, b):
    # E / R evolve when the signature's paired tome is at MAX level (USQAbilityComponent::IsEvolved); the weapon itself
    # evolves as soon as it is maxed and the tome is owned at any level. Run-sim builds track the tome level (kit_evo).
    return getattr(b, "kit_evo", b.evolved)


def kit_power(d, k, b, horde_base, boss_base, vs_boss=False):
    """Everything the kit adds to build b. horde_base / boss_base: the weapons' DPS (for multipliers).
    vs_boss: marks (Lookout passive, Flare / Bankruptcy / Spotting Shot) only land on horde enemies (ASQHordeManager::
    MarkToughestEnemy never marks the boss), so they add nothing to a boss fight."""
    kr = getattr(b, "kr", None) or kill_rate(b.level)
    pas = passive_power(d, k, b, kr)
    evo = kit_evolved(d, k, b)
    cd_speed = getattr(b, "ability_speed", 1.0)          # augment cooldown refunds (Ruthless Rhythm): casts per cooldown
    e = ability_power(d, k, "E", b, evo, pas["e_cd"] / cd_speed, pas["echo"])
    r = ability_power(d, k, "R", b, evo, 1.0 / cd_speed, pas["echo"])
    team = e["team"] + r["team"]
    mark = e.get("mark", 0.0) + r.get("mark", 0.0)
    boss_mult = 1.0 if vs_boss else pas["boss_mult"] * (1 + mark)
    return dict(horde_add=pas["horde"] + e["horde"] + r["horde"], boss_add=pas["boss"] + e["boss"] + r["boss"],
                self_mult=pas["self_mult"] * e["self_mult"] * r["self_mult"] * (1 + team),
                boss_mult=boss_mult, util=pas["util"] + e["util"] + r["util"],
                e=e, r=r, pas=pas)


def kits_section(d, night_s):
    if not d.kits:
        return
    generic = shared_weapons(d)
    print("\n" + "=" * 100)
    print("REAPER KITS (Docs/Kits.md, Kits.csv): signature weapon + passive + E (team level %d) + R (team level %d), solo, typical plan,"
          " end of each night" % (d.t("AbilityUnlockE", 3), d.t("AbilityUnlockR", 6)))
    print("   Clear = %.1f x horde + %.1f x single-target DPS (kit included), each vs the median reaper; Power = clear x (1 + utility%%);"
          " role target = ROLE_CLEAR; band +-%.0f%%" % (CLEAR_W_HORDE, 1 - CLEAR_W_HORDE, 100 * CLEAR_BAND))
    rows = []
    timeline = simulate(d, night_s, 1)
    ends = [row for row in timeline if abs(row[1] - night_s / 60) < 1e-6]
    cps = chest_picks_before_bosses(d, night_s, 1)
    for k in d.kits.values():
        rid = k["ReaperId"]
        reaper = next((r for r in d.reapers if r["Id"] == rid), None)
        role = reaper["Role"] if reaper else "All-rounder"
        per_night = []
        for night in range(3):
            b = Build(d, k["SignatureWeaponId"], ends[night][2] - 1, cps[night])
            base_h = b.horde_dps(generic, kit=False)
            base_b = b.single_target_dps(generic, kit=False)
            kp = kit_power(d, k, b, base_h, base_b)
            horde = (base_h + kp["horde_add"]) * kp["self_mult"]
            boss = (base_b + kp["boss_add"]) * kp["self_mult"] * kp["boss_mult"]
            ab_share = (kp["e"]["horde"] + kp["r"]["horde"]) * kp["self_mult"] / max(1.0, horde)
            per_night.append(dict(horde=horde, boss=boss, util=kp["util"], ab=ab_share, base_h=base_h, kp=kp, lvl=ends[night][2]))
        rows.append(dict(id=rid, role=role, n=per_night))
    for night in range(3):
        med_c = statistics.median(r["n"][night]["horde"] for r in rows)
        med_b = statistics.median(r["n"][night]["boss"] for r in rows)
        med_p = statistics.median(r["n"][night]["horde"] * (1 + r["n"][night]["util"] / 100) for r in rows)
        for r in rows:
            x = r["n"][night]
            x["hordeidx"] = x["horde"] / med_c
            x["single"] = x["boss"] / med_b
            x["clear"] = CLEAR_W_HORDE * x["hordeidx"] + (1 - CLEAR_W_HORDE) * x["single"]
        med_p = statistics.median(r["n"][night]["clear"] * (1 + r["n"][night]["util"] / 100) for r in rows)
        for r in rows:
            x = r["n"][night]
            x["power"] = x["clear"] * (1 + x["util"] / 100) / med_p
    print("\n-- 12. Kits: clear speed and power vs the median reaper (night 1 / 2 / 3); Abil = E+R share of horde DPS at night 3")
    print("  %-12s %-12s %-6s %-17s %-17s %-17s %-6s %-6s %-6s %s" % ("Reaper", "Role", "Target", "Clear n1/n2/n3", "Horde n1/n2/n3",
                                                                      "Power n1/n2/n3", "Util3", "Abil3", "Dev3", "Flag"))
    flags = 0
    for r in sorted(rows, key=lambda x: -x["n"][2]["clear"]):
        tgt = ROLE_CLEAR.get(r["role"], 1.0)
        dev = r["n"][2]["clear"] / tgt - 1
        dev2 = r["n"][1]["clear"] / tgt - 1
        flag = "" if abs(dev) <= CLEAR_BAND and abs(dev2) <= CLEAR_BAND else ("STRONG" if max(dev, dev2) > CLEAR_BAND else "WEAK")
        flags += 1 if flag else 0
        n = r["n"]
        print("  %-12s %-12s %-6.2f %4.2f/%4.2f/%4.2f    %4.2f/%4.2f/%4.2f    %4.2f/%4.2f/%4.2f    %5.0f%% %4.0f%%  %+5.0f%% %s"
              % (r["id"], r["role"], tgt, n[0]["clear"], n[1]["clear"], n[2]["clear"], n[0]["hordeidx"], n[1]["hordeidx"], n[2]["hordeidx"],
                 n[0]["power"], n[1]["power"], n[2]["power"], n[2]["util"], 100 * n[2]["ab"], 100 * dev, flag))
    print("  %d / %d reapers outside +-%.0f%% of their role target at night 2 or 3" % (flags, len(rows), 100 * CLEAR_BAND))
    print("\n  Ability detail at night 3 (horde DPS / boss DPS / utility %; cooldowns after haste):")
    for r in rows:
        kp = r["n"][2]["kp"]
        print("  %-12s E %6.0f / %5.0f / %4.1f%%   R %6.0f / %5.0f / %4.1f%%   passive %5.0f / %5.0f / %4.1f%% x%.2f"
              % (r["id"], kp["e"]["horde"], kp["e"]["boss"], kp["e"]["util"], kp["r"]["horde"], kp["r"]["boss"], kp["r"]["util"],
                 kp["pas"]["horde"], kp["pas"]["boss"], kp["pas"]["util"], kp["pas"]["self_mult"]))
    return rows


# ================================================================ MISSIONS (Nov 9 2026 missions rebuild; Docs/Balance.md section 10)
# Mirrors: ASQGameMode::StartStage / Tick (3 acts per stage, act = the old night index), FSQMissionDirector (objective schedule,
# minibosses), ASQGameState::EconomyHealthMult (mission HP x act smoothing), ASQGameMode::EndRun (gold banking), USQMissions (meta upgrade
# costs), SQQuests (quests). Everything is at RELEASE length (Missions.csv Duration; Tuning NightSeconds 360 = BalanceNightSeconds).
# ---------------------------------------------------------------- ASSUMPTIONS (missions; replace with playtest numbers)
OBJ_SECONDS = {"BankSouls": 95.0, "DestroySpawners": 85.0, "Escort": 105.0, "CollectItems": 70.0, "KillElite": 55.0}
OBJ_FILL_SLOWDOWN = 1.3     # DefendPoint takes Count x this (the zone gets pushed); SurviveWave takes exactly Count
BELL_WALK = 25.0            # last objective -> bell ringing (walk to the boss spot, open a chest or two)
AUGS_PER_WIN = 5.0          # augments by the boss: 2 minibosses + level 10 + level 20 + ~1-2 objective rewards (Augment)
AUG_DPS = 0.06              # boss DPS per augment (KillBlast / orbit stones barely touch a boss; stat augments do)
SKILL_START = 0.74          # a brand-new player's effective power vs an on-curve player (dodging, routing, build choices)
SKILL_RUNS = 6.0            # skill gap closes as exp(-runs / this)
SKILL_MAX = 1.04            # practised players slightly beat the "on-curve" model
WIN_K = 4.0                 # P(win) = 1 / (1 + (need / power)^WIN_K): power / need 1.2 -> 67%, 0.8 -> 29%
NEED_W = {"HealthMult": 0.45, "SpawnMult": 0.3, "DamageMult": 0.45, "BossHealthMult": 0.25}   # how much each mission multiplier raises the bar
OFF_W, DEF_W = 0.6, 0.4     # power = skill x offence^0.6 x defence^0.4 (meta upgrades move these)
PARDON_DEF = 1.3            # a free revive (solo) is worth ~30% more defence
LOSS_AT = 0.7               # a lost mission ends this far into what a win would have taken
PERFECT_SHARE = 0.55        # wins with every objective done (no Defiance)
LOBBY_S, ENDSCREEN_S = 25.0, 20.0
MISSION_SEEDS = 25          # meta pacing runs (median reported)
MISSION_CHECKPOINTS_H = [1, 2, 5, 10, 20, 30, 40, 50]
FARM_IF_BELOW = 0.28        # a new mission with P(win) below this: farm the best cleared one for gold instead


def mission_rows(d):
    if not hasattr(d, "missions"):
        d.missions = read_csv("Missions.csv")
        d.meta = sorted(read_csv("MetaUpgrades.csv"), key=lambda r: num(r.get("Order")))
        d.augments = read_csv("Augments.csv")
    return d.missions


def mission_objectives(m):
    out = []
    for part in (m["Objectives"] or "").split("|"):
        f = [x.strip() for x in part.split(":")]
        if f and f[0]:
            out.append((f[0], int(num(f[1], 1)) if len(f) > 1 else 1, f[2] if len(f) > 2 else "Gold"))
    return out


def mission_mult(m, key, hard):
    v = num(m.get(key), 1.0)
    return v * (num(m.get("Hard" + key), 1.0) if hard else 1.0)


def meta_cost(row, rank):                      # FSQMetaUpgradeDef::CostForRank
    if rank >= int(num(row["MaxRank"], 1)):
        return None
    return max(5, int(round(num(row["BaseCost"]) * num(row["CostGrowth"], 1.5) ** rank / 5.0)) * 5)


def meta_value(d, row, rank):                  # USQMissions::MetaValueAt
    if rank <= 0:
        return 0.0
    if row["Stat"] == "ReviveSeconds":
        return max(d.t("MetaReviveMinSeconds", 3), num(row["PerRank"]) - (rank - 1))
    return num(row["PerRank"]) * rank


def meta_extra(d, ranks):
    """Death's Office ranks -> (raw stats for Build.recompute, special lines)"""
    raw, special = {}, {}
    for row in d.meta:
        r = ranks.get(row["Id"], 0)
        if r <= 0:
            continue
        v = meta_value(d, row, r)
        st = row["Stat"]
        if st in ("XPGain", "GoldGain", "ReviveSeconds", "Rerolls", "StartLevels", "Pardon"):
            special[st] = special.get(st, 0.0) + v
        else:
            raw[st] = raw.get(st, 0.0) + (-v if st == "DashCooldown" else v)
    return raw, special


def bell_time(d, m):
    """Seconds into the stage when the bell is rung (objective schedule: FSQMissionDirector::Tick), capped at the stage end"""
    dur = num(m["Duration"])
    objs = mission_objectives(m)
    n = max(1, len(objs))
    first, gap = d.t("ObjectiveFirstDelay", 20), d.t("ObjectiveGap", 15)
    spread, early = d.t("ObjectiveSpread", 0.8), d.t("ObjectiveEarlyFactor", 0.6)
    end = 0.0
    for k, (typ, count, _r) in enumerate(objs):
        sched = first + dur * spread * k / n
        start = sched if k == 0 else max(sched * early, end + gap)
        if typ == "DefendPoint":
            took = count * OBJ_FILL_SLOWDOWN
        elif typ == "SurviveWave":
            took = count
        else:
            took = OBJ_SECONDS.get(typ, 80.0)
        end = start + min(took, d.t("ObjectiveTime" + typ, 150) or 1e9)
    return min(dur, end + BELL_WALK), end + BELL_WALK > dur


class _Tune:
    """Temporarily override Tuning values (spawn multiplier, BossXP 0 for acts)"""
    def __init__(self, d, **kv):
        self.d, self.kv, self.old = d, kv, {}

    def __enter__(self):
        for k, v in self.kv.items():
            self.old[k] = self.d.tune.get(k)
            self.d.tune[k] = v
        return self.d

    def __exit__(self, *a):
        for k, v in self.old.items():
            if v is None:
                self.d.tune.pop(k, None)
            else:
                self.d.tune[k] = v


_MIS_CACHE = {}


def mission_timeline(d, m, players, hard, xp_bonus=0.0):
    """[(seconds into the stage, level, kills per player, souls collected this act)] at 60 s steps over the 3 acts"""
    key = ("tl", m["Id"], players, hard, round(xp_bonus, 3))
    if key in _MIS_CACHE:
        return _MIS_CACHE[key]
    act_s = num(m["Duration"]) / 3.0
    sm = mission_mult(m, "SpawnMult", hard)
    with _Tune(d, SpawnRateStart=d.t("SpawnRateStart", 4) * sm, SpawnRatePeak=d.t("SpawnRatePeak", 22) * sm, BossXP=0.0,
               XPBase=d.t("XPBase", 8) / (1 + xp_bonus), XPGrowth=d.t("XPGrowth", 15) / (1 + xp_bonus)):
        rows = simulate(d, act_s, players)
    out = [(night * act_s + minute * 60.0, level, kills, souls) for night, minute, level, kills, souls in rows]
    _MIS_CACHE[key] = out
    return out


def at_time(tl, t):
    best = tl[0]
    for row in tl:
        if row[0] <= t + 1e-6:
            best = row
    return best


def mission_gold(d, m, players, hard, t_end, won, gold_gain=0.0):
    """Gold one reaper EARNS up to t_end seconds (ASQPlayerState::RunGoldEarned), and the objectives done by then"""
    act_s = num(m["Duration"]) / 3.0
    tl = mission_timeline(d, m, players, hard)
    ups = {e["Id"]: e for e in d.enemies}
    share = 1.0 / (1 + d.t("PlayerSpawnRateBonus", 0.35) * (players - 1)) if d.t("GoldShareCoop", 1) > 0.5 else 1.0 / players
    gold, prev_kills, souls_total = 0.0, 0.0, 0.0
    for act in range(3):
        a0, a1 = act * act_s, min(t_end, (act + 1) * act_s)
        if a1 <= a0:
            break
        kills = at_time(tl, a1)[2] - prev_kills
        prev_kills = at_time(tl, a1)[2]
        shares = night_mix_shares(d, act)
        elite = sum(v for k, v in shares.items() if ups[k]["Behaviour"] == "Elite")
        avg_xp = sum(num(ups[k]["XP"]) * v for k, v in shares.items() if ups[k]["Behaviour"] != "Elite") / max(1e-6, 1 - elite)
        growth = 1 + d.t("GoldPerNight", 1.5) * act
        team_kills = kills * players
        coins = team_kills * (1 - elite) * d.t("GoldDropChance", 0.04) * avg_xp * d.t("GoldPerXP", 1) * growth * GOLD_PICKUP
        el = team_kills * elite
        cr = el * d.t("CrownedChance", 0.3)
        el_gold = ((el - cr) + cr * d.t("CrownedGoldMult", 3)) * d.t("EliteGold", 4) * growth * GOLD_PICKUP
        gold += (coins + el_gold) * share
        souls_total += max(0.0, at_time(tl, a1)[3]) * (a1 - a0) / act_s
    gold += souls_total * (1 - SOUL_LOSS) * d.t("SoulGold", 0.25)
    # objectives (schedule), nests, minibosses, boss
    dur = num(m["Duration"])
    objs = mission_objectives(m)
    n = max(1, len(objs))
    end, done = 0.0, 0
    for k, (typ, count, reward) in enumerate(objs):
        sched = d.t("ObjectiveFirstDelay", 20) + dur * d.t("ObjectiveSpread", 0.8) * k / n
        start = sched if k == 0 else max(sched * d.t("ObjectiveEarlyFactor", 0.6), end + d.t("ObjectiveGap", 15))
        took = count * OBJ_FILL_SLOWDOWN if typ == "DefendPoint" else (count if typ == "SurviveWave" else OBJ_SECONDS.get(typ, 80.0))
        end = start + took
        if end > t_end:
            break
        done += 1
        growth = 1 + d.t("GoldPerNight", 1.5) * min(2, int(end // (dur / 3)))
        if reward == "Gold":
            gold += d.t("ObjectiveGold", 40) * growth
        if typ == "DestroySpawners":
            gold += count * d.t("EliteGold", 4) * 2 * growth * share
    for at in [num(x) for x in (m.get("MinibossAt") or "").split("|") if x.strip()]:
        if at * dur <= t_end:
            gold += d.t("MinibossGold", 20) * (1 + d.t("GoldPerNight", 1.5) * min(2, int(at * 3)))
    if won:
        gold += d.t("BossGold", 30) * (1 + d.t("GoldPerNight", 1.5) * 2)
    return gold * mission_mult(m, "GoldMult", hard) * (1 + gold_gain), done


def mission_build(d, m, players, hard, t, ranks=None, start=None):
    """On-curve build t seconds into the mission (level-ups, chest items, Death's Office ranks)"""
    ranks = ranks or {}
    raw, special = meta_extra(d, ranks)
    tl = mission_timeline(d, m, players, hard, special.get("XPGain", 0.0))
    level = at_time(tl, t)[1] + int(special.get("StartLevels", 0))
    act_s = num(m["Duration"]) / 3.0
    picks = chest_picks_before_bosses(d, act_s, players)
    act = min(2, int(t // act_s))
    frac = max(0.0, min(1.0, (t - act * act_s) / act_s))
    chest = picks[0] * frac if act == 0 else picks[act - 1] + (picks[act] - picks[act - 1]) * frac
    b = Build(d, start or default_start(d), level - 1, chest)
    b.recompute(raw)
    return b, level, special


def mission_power(d, m, players, hard, ranks, start=None):
    """(offence, defence) of an on-curve reaper with these ranks vs none, at the bell"""
    key = ("pw", m["Id"], players, hard, tuple(sorted(ranks.items())))
    if key in _MIS_CACHE:
        return _MIS_CACHE[key]
    generic = [u for u in d.ups.values() if u["Kind"] == "Weapon"]
    t_bell, _ot = bell_time(d, m)
    base, _l0, _s0 = mission_build(d, m, players, hard, t_bell, {}, start)
    with_meta, _l1, special = mission_build(d, m, players, hard, t_bell, ranks, start)
    ehp = lambda b: (b.max_hp + b.stats.get("Regen", 0.0) * REGEN_WINDOW) / max(0.05, 1 - b.armor) * (1 + 0.3 * (b.stats.get("MoveSpeed", 650) - 650) / 650)
    off = (0.5 * with_meta.single_target_dps(generic) / max(1e-6, base.single_target_dps(generic))
           + 0.5 * with_meta.horde_dps(generic) / max(1e-6, base.horde_dps(generic)))
    dfn = ehp(with_meta) / max(1e-6, ehp(base)) * (PARDON_DEF if special.get("Pardon", 0) > 0 and players == 1 else 1.0)
    _MIS_CACHE[key] = (off, dfn)
    return off, dfn


def mission_need(m, hard):
    need = 1.0
    for k, w in NEED_W.items():
        need *= mission_mult(m, k, hard) ** w
    return need


def skill_at(runs):
    return SKILL_MAX - (SKILL_MAX - SKILL_START) * 2.718281828 ** (-runs / SKILL_RUNS)


def p_win(d, m, hard, ranks, runs, players=1):
    off, dfn = mission_power(d, m, players, hard, ranks)
    power = skill_at(runs) * off ** OFF_W * dfn ** DEF_W
    return 1.0 / (1.0 + (mission_need(m, hard) / max(1e-6, power)) ** WIN_K)


def mission_boss(d, m, players, hard, ranks=None):
    """(boss id, level at the bell, HP, TTK seconds, target seconds)"""
    generic = [u for u in d.ups.values() if u["Kind"] == "Weapon"]
    t_bell, _ot = bell_time(d, m)
    b, level, _sp = mission_build(d, m, players, hard, t_bell, ranks or {})
    boss = next((x for x in d.bosses if x["Id"] == m["Boss"]), d.bosses[-1])
    hp = num(boss["Health"]) * (1 + d.t("BossHealthPerPlayer", 0.75) * (players - 1)) * econ_mult(d, "EconomyBossHealth", 2) \
        * mission_mult(m, "BossHealthMult", hard)
    dps = b.single_target_dps(generic) * players * BOSS_UPTIME * (1 + AUG_DPS * AUGS_PER_WIN)
    return boss["Id"], level, hp, hp / max(0.01, dps), num(boss["TargetSeconds"])


def mission_minutes(d, m, players, hard, won, ranks=None):
    t_bell, _ot = bell_time(d, m)
    ttk = mission_boss(d, m, players, hard, ranks)[3]
    play = t_bell + ttk if won else LOSS_AT * (t_bell + ttk)
    return (LOBBY_S + play + ENDSCREEN_S) / 60.0, play


def bank_for(d, m, players, hard, won, ranks, first_clear, first_hard, coin_mult=1.0):
    """Gold banked at the end (ASQGameMode::EndRun + ASQPlayerState::ClientMissionEnd)"""
    _raw, special = meta_extra(d, ranks)
    _mins, play = mission_minutes(d, m, players, hard, won, ranks)
    earned, done = mission_gold(d, m, players, hard, play if not won else 1e9, won, special.get("GoldGain", 0.0))
    dur = num(m["Duration"])
    share = d.t("BankShareWin", 0.35) if won else d.t("BankShareLoss", 0.2) * max(0.5, min(1.0, play / dur))
    bank = earned * share * coin_mult
    if won:
        bank += num(m["ClearGold"]) * (mission_mult(m, "GoldMult", False) and (num(m.get("HardGoldMult"), 1.0) if hard else 1.0))
        if first_clear:
            bank += num(m["FirstClearGold"])
        if first_hard:
            bank += num(m["HardFirstClearGold"])
    return bank, earned, done


LANTERN_LIT_SHARE = 0.8     # share of a run's lanterns a solo player fills in time (assumption)
EVENT_DONE_SHARE = 0.6      # share of a run's random events the player answers (assumption)
LOOP_EVERY = 4              # a win in every 4th run is followed by one haunt (assumption: most runs cash out)
LOOP_CLEAR_SHARE = 0.6      # chance that haunt's boss falls


def mission_pacing(d, seed):
    """A new SOLO player plays missions back to back. Returns (log, state)."""
    import random
    rng = random.Random(seed)
    missions = mission_rows(d)
    quests = [q for q in d.quests if q.get("Id")]
    reapers = {r["Id"]: r for r in d.reapers}
    hired = {rid for rid, r in reapers.items() if num(r["UnlockCost"]) <= 0}
    ranks = {}
    cleared, hard_cleared = set(), set()
    done, won_with, items = set(), set(), set()
    totals, bests = {}, {}
    bank = 0.0
    clock = 0.0
    runs = 0
    log = []
    ups = {e["Id"]: e for e in d.enemies}
    snaps = []
    ledger = [0.0, 0]

    def derived(stat):
        return {"QuestsDone": len(done), "ReapersHired": len(hired), "Contracts": len(cleared), "ReaperWins": len(won_with),
                "MetaRanks": sum(ranks.values()), "MissionsCleared": len(cleared), "HardCleared": len(hard_cleared)}.get(stat)

    def check(run_stats, minute):
        nonlocal bank
        changed = True
        while changed:
            changed = False
            for q in quests:
                if q["Id"] in done:
                    continue
                v = derived(q["Stat"])
                if v is None:
                    v = totals.get(q["Stat"], 0) if q["Mode"] == "Total" else max(bests.get(q["Stat"], 0), run_stats.get(q["Stat"], 0))
                if v >= num(q["Target"]):
                    done.add(q["Id"])
                    changed = True
                    what = []
                    for k, val in parse_reward(q["Reward"]):
                        if k == "Coins":
                            bank += num(val); what.append("%s gold" % val)
                        elif k == "Reaper" and val not in hired:
                            hired.add(val); what.append("reaper " + val)
                        elif k == "Item":
                            items.add(val); what.append("item " + val)
                    log.append((minute, "quest", q["Name"], ", ".join(what)))

    def unlocked(m):
        u = (m.get("Unlock") or "").strip()
        return not u or u.split(":", 1)[-1].strip() in cleared

    def spend(minute):
        nonlocal bank
        while True:
            options = []
            for row in d.meta:
                c = meta_cost(row, ranks.get(row["Id"], 0))
                if c is not None:
                    options.append((c, "meta", row["Id"]))
            for rid, r in reapers.items():
                if rid not in hired:
                    options.append((num(r["UnlockCost"]), "hire", rid))
            if not options:
                return
            # Greedy: the cheapest upgrade or hire first (players who save up for one big thing are slower, not faster)
            cost, kind, what = min(options)
            if cost > bank:
                return
            bank -= cost
            if kind == "meta":
                ranks[what] = ranks.get(what, 0) + 1
                log.append((minute, "meta", what, "%d gold (rank %d)" % (cost, ranks[what])))
            else:
                hired.add(what)
                log.append((minute, "hire", reapers[what]["Name"], "%d gold" % cost))
            check({}, minute)

    max_meta = sum(int(num(r["MaxRank"], 1)) for r in d.meta)
    while clock < 120 * 60:
        if len(hard_cleared) == len(missions) and sum(ranks.values()) >= max_meta and len(hired) == len(reapers):
            break
        # ---- which mission: the newest unbeaten one, else the Hard modes, else farm the richest
        open_new = [m for m in missions if unlocked(m) and m["Id"] not in cleared]
        open_hard = [m for m in missions if m["Id"] in cleared and m["Id"] not in hard_cleared]
        pick, hard = None, False
        if open_new:
            pick = open_new[0]
            if p_win(d, pick, False, ranks, runs) < FARM_IF_BELOW and cleared:
                pick = None
        if pick is None and open_hard and not open_new:
            best = max(open_hard, key=lambda m: p_win(d, m, True, ranks, runs))
            if p_win(d, best, True, ranks, runs) >= FARM_IF_BELOW * 0.8:
                pick, hard = best, True
        if pick is None:
            # farm: best expected bank per minute among cleared missions (normal and Hard)
            choices = [(m, False) for m in missions if m["Id"] in cleared] + [(m, True) for m in missions if m["Id"] in hard_cleared]
            if not choices:
                pick = open_new[0]
            else:
                def rate(c):
                    mm, hh = c
                    pw = p_win(d, mm, hh, ranks, runs)
                    bw = bank_for(d, mm, 1, hh, True, ranks, False, False)[0]
                    bl = bank_for(d, mm, 1, hh, False, ranks, False, False)[0]
                    tw = mission_minutes(d, mm, 1, hh, True, ranks)[0]
                    tl_ = mission_minutes(d, mm, 1, hh, False, ranks)[0]
                    return (pw * bw + (1 - pw) * bl) / (pw * tw + (1 - pw) * tl_)
                pick, hard = max(choices, key=rate)
        pw = p_win(d, pick, hard, ranks, runs)
        won = rng.random() < pw
        mins, play = mission_minutes(d, pick, 1, hard, won, ranks)
        first_clear = won and pick["Id"] not in cleared
        first_hard = won and hard and pick["Id"] not in hard_cleared
        mine = sorted(hired, key=lambda r: (r in won_with, r))[0]
        banked, earned, objs_done = bank_for(d, pick, 1, hard, won, ranks, first_clear, first_hard)
        # ---- quest stats of the run (the server's CreditQuest events, roughly)
        run_stats = {}

        def add(st, v):
            run_stats[st] = run_stats.get(st, 0) + v
            totals[st] = totals.get(st, 0) + v

        tl = mission_timeline(d, pick, 1, hard)
        kills = at_time(tl, play)[2]
        add("Kills", kills)
        shares = night_mix_shares(d, 1)
        for eid, sh in shares.items():
            add("Kill_" + eid, kills * sh)
        elite = kills * sum(v for k, v in shares.items() if ups[k]["Behaviour"] == "Elite")
        add("EliteKills", elite)
        add("CrownedKills", elite * d.t("CrownedChance", 0.3))
        objs = mission_objectives(pick)
        add("ObjectivesDone", objs_done)
        for typ, count, _r in objs[:objs_done]:
            add("Obj_" + typ, 1)
            if typ == "DestroySpawners":
                add("NestsDestroyed", count)
            if typ == "CollectItems":
                add("UrnsCollected", count)
        minis = sum(1 for x in (pick.get("MinibossAt") or "").split("|") if x.strip() and num(x) * num(pick["Duration"]) <= play)
        add("MinibossKills", minis)
        augs = min(AUGS_PER_WIN, minis + (1 if at_time(tl, play)[1] >= 10 else 0) + (1 if at_time(tl, play)[1] >= 20 else 0)
                   + sum(1 for typ, c, r in objs[:objs_done] if r == "Augment"))
        add("AugmentsTaken", augs)
        econ = run_economy(d, num(pick["Duration"]) / 3.0, 1)
        frac = min(1.0, play / num(pick["Duration"]))
        chests = sum(e["paid"] + e["free"] for e in econ) * frac
        add("ChestsOpened", chests)
        add("FreeChests", sum(e["free"] for e in econ) * frac)
        add("LegendaryChest", chests * rarity_odds(d, d.t("ChestLuckBonus", 0.25))[3])
        add("GoldEarned", earned)
        add("TeamLevel", at_time(tl, play)[1])
        add("DamageTaken", DAMAGE_PER_NIGHT * 3 * frac)
        add("Dashes", DASHES_PER_NIGHT * 3 * frac)
        add("SoulsBanked", 250 * frac)
        add("RunsPlayed", 1)
        # Content pass (Nov 24 2026): lanterns lit credit Obj_BankSouls (one per lit lantern); events answered; loops after a win
        add("Obj_BankSouls", int(d.t("LanternStages", 4)) * frac * LANTERN_LIT_SHARE)
        add("EventsDone", d.t("EventsPerRun", 3) * frac * EVENT_DONE_SHARE)
        if won and len(cleared) >= 1 and runs % LOOP_EVERY == LOOP_EVERY - 1:
            add("LoopsBegun", 1)
            if rng.random() < LOOP_CLEAR_SHARE:
                add("LoopsCleared", 1)
        add("PlateausClimbed", PLATEAUS_PER_RUN)
        add("ShrinesUsed", SHRINES_PER_RUN)
        add("SlideKills", SLIDE_KILLS_PER_RUN)
        add("NoHitNights", P_NO_HIT_NIGHT * objs_done)
        run_stats["MaxCarried"] = 160 if mine == "Collector" else 50 + 12 * min(5, runs // 3)
        if runs >= 4 and runs % 5 == 0:
            add("EarlyBell", 1)
        if won:
            add("BossKills", 1)
            add("RunsWon", 1)
            add("Win_" + mine, 1)
            won_with.add(mine)
            add("LowHPNight", P_LOW_HP_NIGHT)
            add("BossNoDefiance", PERFECT_SHARE)
            add("PerfectRun", PERFECT_SHARE * 0.4)
            if runs >= 6:
                add("BossFast", P_FAST_BOSS); add("BossBlitz", P_BLITZ_BOSS); add("SwarmBoss", P_SWARM_BOSS)
                add("SwarmKills", P_SWARM_BOSS * 150)
                run_stats["SwarmSeconds"] = SWARM_SECONDS * min(1.0, P_SWARM_BOSS * 5) * (1 + runs / 40.0)
            if hard:
                add("HardWins", 1)
            if runs >= 20 and runs % 12 == 0:
                add("FastWin", 1)
            if runs >= 15:
                nm = 1 if runs < 30 else (2 if runs < 60 else 3)
                mods = [x for x in d.modifiers if x.get("Id")]
                for k in range(nm):
                    add("ModWin_" + mods[(runs + k) % len(mods)]["Id"], 1)
                run_stats["ModsInWin"] = nm
        run_stats["DifficultyPct"] = 20 + (40 if runs >= 30 and runs % 5 == 0 else 0)
        # Death's Ledger (ASQPlayerState::ClientMissionEnd): a rank or more every finished run
        if d.t("LedgerRanks", 1) >= 0.5 and ledger[1] < d.t("LedgerMaxRank", 300):
            ledger[0] += max(d.t("LedgerMinXP", 100), play / 60.0 * d.t("LedgerXPPerMinute", 10) + (d.t("LedgerXPWin", 40) if won else 0))
            lgold = 0
            while ledger[0] >= d.t("LedgerXPPerRank", 100) and ledger[1] < d.t("LedgerMaxRank", 300):
                ledger[0] -= d.t("LedgerXPPerRank", 100); ledger[1] += 1
                lgold += d.t("LedgerGoldPerRank", 25) + (d.t("LedgerGoldEvery5", 100) if ledger[1] % 5 == 0 else 0)
            if lgold:
                banked += lgold
                log.append((clock + mins, "ledger", "Ledger rank %d" % ledger[1], "%d gold" % lgold))
        bank += banked
        totals["GoldBanked"] = totals.get("GoldBanked", 0) + banked
        if won:
            cleared.add(pick["Id"])
            if hard:
                hard_cleared.add(pick["Id"])
        clock += mins
        for k, v in run_stats.items():
            bests[k] = max(bests.get(k, 0), v)
        check(run_stats, clock)
        log.append((clock, "run", "%s%s %s" % (pick["Id"], " HARD" if hard else "", "WIN" if won else "loss"),
                    "P(win) %.0f%%, +%d gold banked (%.1f min)" % (pw * 100, banked, mins)))
        spend(clock)
        runs += 1
        snaps.append((clock, dict(totals), dict(bests)))
    return log, dict(cleared=cleared, hard=hard_cleared, ranks=ranks, hired=hired, done=done, quests=quests, runs=runs,
                     max_meta=max_meta, clock=clock, snaps=snaps)


def missions_sections(d):
    missions = mission_rows(d)
    if not missions:
        return
    print("\n" + "=" * 100)
    print("MISSIONS (Nov 9 2026 missions rebuild; release lengths; Docs/Balance.md section 10)")
    # ---------------- 13. stage shape
    print("\n-- 13. Missions: stage, bell, levels, boss TTK (solo / 4 players), hits-to-kill at the end, normal vs Hard --")
    print("  %-16s %-4s %-6s %-9s %-8s %-6s %-22s %-22s %-10s %-8s %-7s"
          % ("Mission", "Mode", "Stage", "Bell(min)", "Lvl@bell", "Need", "Boss TTK P1 (target)", "Boss TTK P4", "Hits@end", "Earned", "Banked"))
    generic = [u for u in d.ups.values() if u["Kind"] == "Weapon"]
    for m in missions:
        for hard in (False, True):
            t_bell, over = bell_time(d, m)
            b1 = mission_boss(d, m, 1, hard)
            b4 = mission_boss(d, m, 4, hard)
            dur = num(m["Duration"])
            bd, lvl_end, _s = mission_build(d, m, 1, hard, dur - 1)
            # average horde HP at the end of act 3 (the act smoothing anchors there) x the mission's HealthMult
            mix = enemy_mix(d, 2, 1.0)
            avg = sum(enemy_hp(d, e, 2, dur / 3.0 / 60.0, 1) * w for e, w in mix if e["Behaviour"] != "Elite") / max(1e-6, sum(w for e, w in mix if e["Behaviour"] != "Elite"))
            avg *= mission_mult(m, "HealthMult", hard)
            hits = avg / max(0.01, bd.main_hit())
            earned = mission_gold(d, m, 1, hard, 1e9, True)[0]
            banked = bank_for(d, m, 1, hard, True, {}, False, False)[0]
            print("  %-16s %-4s %4.1fm  %5.1f%s    %-8d %-6.2f %5.0f s (%3.0f)          %5.0f s              %-10.1f %-8.0f %-7.0f"
                  % (m["Id"], "HARD" if hard else "norm", dur / 60, t_bell / 60, "*" if over else " ", b1[1], mission_need(m, hard),
                     b1[3], b1[4], b4[3], hits, earned, banked))
    print("   * = the objectives run past the stage clock (overtime). Need = the mission's difficulty bar (1.0 = the old 3-night run's"
          " on-curve tuning); P(win) = 1 / (1 + (need / power)^%.0f), power = skill x offence^%.1f x defence^%.1f." % (WIN_K, OFF_W, DEF_W))
    # ---------------- 14. Death's Office
    print("\n-- 14. Death's Office (MetaUpgrades.csv): cost of every rank, and what all of them add at the end of the Clocktower stage --")
    total = 0
    for row in d.meta:
        costs = [meta_cost(row, r) for r in range(int(num(row["MaxRank"], 1)))]
        total += sum(costs)
        print("  %-12s %-17s max %2d  %s  (sum %d)" % (row["Id"], row["Name"], int(num(row["MaxRank"], 1)), costs if len(costs) <= 10 else costs[:10], sum(costs)))
    full = {row["Id"]: int(num(row["MaxRank"], 1)) for row in d.meta}
    locked = sum(int(num(r["UnlockCost"])) for r in d.reapers if num(r["UnlockCost"]) > 0)
    last = missions[-1]
    off, dfn = mission_power(d, last, 1, False, full)
    print("   Every rank: %d gold (+ reapers %d = %d). Maxed meta at the Clocktower bell: offence x%.2f, defence x%.2f, P(win) for a"
          " practised player %.0f%% -> %.0f%% (Hard %.0f%% -> %.0f%%)"
          % (total, locked, total + locked, off, dfn, 100 * p_win(d, last, False, {}, 99), 100 * p_win(d, last, False, full, 99),
             100 * p_win(d, last, True, {}, 99), 100 * p_win(d, last, True, full, 99)))
    # ---------------- 15. pacing
    print("\n-- 15. META PACING: a new SOLO player (median of %d seeds; skill %.2f -> %.2f over ~%d runs; farms when P(win) < %.0f%%) --"
          % (MISSION_SEEDS, SKILL_START, SKILL_MAX, int(SKILL_RUNS * 3), 100 * FARM_IF_BELOW))
    sims = [mission_pacing(d, seed) for seed in range(MISSION_SEEDS)]

    def med(vals):
        return statistics.median(vals) if vals else 0

    first_clear_runs, first_clear_min = [], []
    for log, st in sims:
        n = 0
        for e in log:
            if e[1] == "run":
                n += 1
                if e[2].startswith(missions[0]["Id"]) and "WIN" in e[2] and "HARD" not in e[2]:
                    first_clear_runs.append(n)
                    first_clear_min.append(e[0])
                    break
    print("   Map 1 (%s) first cleared on attempt %s (median %d), at %.0f min (median)"
          % (missions[0]["Id"], sorted(first_clear_runs)[:: max(1, len(first_clear_runs) // 6)], med(first_clear_runs), med(first_clear_min)))
    print("  %5s %5s %8s %8s %10s %9s %8s" % ("Hours", "Runs", "Cleared", "Hard", "MetaRanks", "Hires", "Quests"))
    for h in MISSION_CHECKPOINTS_H:
        rows_h = []
        for log, st in sims:
            evts = [e for e in log if e[0] <= h * 60]
            runs_n = sum(1 for e in evts if e[1] == "run")
            cleared = len({e[2].split()[0] for e in evts if e[1] == "run" and "WIN" in e[2] and "HARD" not in e[2]})
            hard_n = len({e[2].split()[0] for e in evts if e[1] == "run" and "WIN" in e[2] and "HARD" in e[2]})
            meta_n = sum(1 for e in evts if e[1] == "meta")
            hires = sum(1 for e in evts if e[1] == "hire") + sum(1 for e in evts if e[1] == "quest" and "reaper" in e[3])
            quests_n = sum(1 for e in evts if e[1] == "quest")
            rows_h.append((runs_n, cleared, hard_n, meta_n, hires, quests_n))
        print("  %5d %5.0f %8.0f %8.0f %5.0f/%-4d %9.0f %8.0f" % (h, med([r[0] for r in rows_h]), med([r[1] for r in rows_h]), med([r[2] for r in rows_h]),
                                                             med([r[3] for r in rows_h]), sims[0][1]["max_meta"], med([r[4] for r in rows_h]), med([r[5] for r in rows_h])))
    # completion and the first hour in detail (seed 0)
    ends = []
    for log, st in sims:
        t_all = None
        if len(st["hard"]) == len(missions) and sum(st["ranks"].values()) >= st["max_meta"] and len(st["hired"]) == len(d.reapers):
            t_all = st["clock"] / 60
        t_maps = next((e[0] / 60 for e in log if e[1] == "run" and "WIN" in e[2] and "HARD" not in e[2] and e[2].startswith(missions[-1]["Id"])), None)
        t_hard = None
        seen = set()
        for e in log:
            if e[1] == "run" and "WIN" in e[2] and "HARD" in e[2]:
                seen.add(e[2].split()[0])
                if len(seen) == len(missions):
                    t_hard = e[0] / 60
                    break
        ends.append((t_maps, t_hard, t_all))
    pick = lambda i: med([x[i] for x in ends if x[i] is not None])
    miss = lambda i: sum(1 for x in ends if x[i] is None)
    print("   All 5 maps cleared: %.1f h | all Hard modes: %.1f h | everything (Hard + every rank + every reaper): %.1f h  (seeds not done in 120 h: %d / %d / %d)"
          % (pick(0), pick(1), pick(2), miss(0), miss(1), miss(2)))
    log0, st0 = sims[0]
    print("   First hour, seed 0:")
    for e in log0:
        if e[0] > 75:
            break
        print("     %5.1f min  %-5s %-34s %s" % (e[0], e[1], e[2], e[3]))
    gaps = [0.0] + [e[0] for e in log0 if e[1] in ("quest", "hire", "meta") or (e[1] == "run" and "WIN" in e[2])] + [600.0]
    gaps = [b - a for a, b in zip(gaps, gaps[1:]) if b <= 600.0]
    print("   Seed 0: longest gap between rewards (quest / hire / upgrade / win) in the first 10 h: %.0f min" % max(gaps))



# ================================================================ RUN SIMULATION (Nov 9 2026 remake balance pass; Docs/Balance.md section 11)
# ONE coherent simulation of a mission run, used by every section below. Mirrors, step by step:
#   ASQGameMode::StartStage / Tick      stage clock, 3 acts (Duration / 3), Final Swarm when the clock runs out
#   ASQHordeManager::SimSpawning        spawn rate lerp(SpawnRateStart, SpawnRatePeak, act progress) x crew x mission SpawnMult
#   ASQHordeManager::SpawnEnemyEx       HP = (Health + HealthPerNight x act) x (1 + r x act minutes) x crew x EconomyEnemyHealth<act>
#                                       x mission HealthMult (x Hard) x ASQGameState::StageHealthSmoothing
#   ASQHordeManager::SimEnemies         enemy hit x (1 + EnemyDamagePerNight x act) x (1 + EnemyDamagePerMinute x act minutes)
#                                       x mission DamageMult (x Hard) x the act-damage smoothing (remake pass, Tuning StageDamageSmoothing)
#   ASQGameState::AddTeamXP             XP to next = (XPBase + XPGrowth x level) x (1 + XPPerPlayer x extra), Night School / Study Group
#   ASQPlayerState::RollOffers          3 cards from CanTakeUpgrade (signature + WeaponSlots shared weapons, TomeSlots tomes,
#                                       evolutions = max weapon + its tome owned), card Weight, rarity by Luck; filler when empty
#   ASQChest / ASQGameState::ChestPriceFor  per-reaper chest prices, items by Weight at Luck + ChestLuckBonus, MaxStacks
#   FSQMissionDirector                  objective schedule (FirstDelay + Duration x Spread x k / n, EarlyFactor, Gap), limits, rewards,
#                                       minibosses at MinibossAt (augment at the 1st / 2nd kill, chest at the last), bell
#   USQAugments                         offers (tier odds per source), every effect (see aug_effects), slot / Pardon / chain caps
#   USQAbilityComponent                 E / R / passive through kit_power (cooldowns, haste, evolution at max tome)
#   ASQBoss::InitBoss                   HP = Health x (1 + BossHealthPerPlayer x extra) x (1 + 0.25 Defiance) x EconomyBossHealth3 x
#                                       mission BossHealthMult (x Hard); OnHit augments never touch the boss (TakeBossDamage)
#   USQAugments::ModifyStats            Death's Office ranks (MetaUpgrades.csv) and stat augments -> SQ::FinalizeStats
# Every reaper in the crew has its own build (own cards, own purse share, own chests); the team shares XP / level.
# ---------------------------------------------------------------- ASSUMPTIONS (run simulation; replace with playtest numbers)
SIM_DT = 5.0                    # seconds per step
SIM_SEEDS = 12                  # runs per cell (the reaper / crew rotates through SIM_POOL with the seed)
SIM_POOL = ["Grim", "Widow", "Mort", "Digby", "Bonesmith", "Keeper", "Collector", "Tallow", "Monk", "Beak", "Lookout", "Vexa"]
# solo seed s plays SIM_POOL[s]; a crew of n on seed s is n consecutive pool reapers from s x n (same reapers on average)
SIM_KILL_SHARE = 0.75           # share of spawns killed by an on-curve team (clear ratio 1); x clear^0.3, clamped 0.55-0.9
SHRINE_RAISES_STAGE = 3.5       # Performance Review / golden shrine raises one reaper takes in a stage (3 shrines + a golden one)
LOST_PROPERTY_STAGE = 2.0       # Lost Property items per stage for the whole crew (2 desks, first come)
FREE_SHRINE_CHESTS = 1.0        # free chests per reaper from Complaints Bell / Sealed Crypt / Contract Extension per stage
POT_GOLD_STAGE = 40.0           # gold from smashed pots per reaper per stage
OBJ_TRAVEL = 12.0               # seconds of walking between nests / urns / elites
ESCORT_SECONDS = 95.0           # hearse road ~85 m at EscortSpeed 160 + ambush stops
URN_SECONDS = 11.0              # per urn, solo (two reapers split the map)
BANK_TRIP = 20.0                # last trip to the lantern for a Bank Souls objective
LANTERN_TRAVEL = 22.0           # lantern trail: seconds crossing to the next lit lantern (no banking meanwhile)
SOULS_PER_KILL_PICKED = 0.55    # souls a reaper actually banks per kill (drop 1-3 x pickup 70% x 30% lost to hits / wisps)
BELL_WALK_SIM = 25.0            # last objective -> bell rung
MINI_ENGAGE = 12.0              # seconds to reach a miniboss
SKILL_NEW, SKILL_PRACTISED = 0.74, 1.04
WINPROXY_K = 4.0                # P(win) = 1 / (1 + (1 / power)^K)
WIN_OFF_W, WIN_DEF_W = 0.6, 0.4
HTD_REF = 16.0                  # hits-to-down at the bell an on-curve player "needs" (the defence reference)
COOP_REVIVE_DEF = 0.3           # co-op: each extra reaper adds this much defence (skull carry, field revives)
PARDON_DEF_SIM = 0.35           # each Death's Pardon adds this much defence
BOSS_HIT_SHARE = 0.35           # share of a boss's slam / ring damage a reaper actually eats
NEED_SCALE = 1.0                # calibrated in sim_calibrate(): power of 1.0 = 50% win
AUG_ELITE_HP_SHARE = None       # computed from Enemies.csv (elites' share of the horde's HP)


def sim_tune(d, key, dflt):
    return d.t(key, dflt)


def mission_by_id(d, mid):
    return next(m for m in mission_rows(d) if m["Id"] == mid)


def sim_act_len(m):
    return num(m["Duration"]) / 3.0


def stage_smoothing(d, act, into, act_min, ratio, per_min):
    """ASQGameState::StageHealthSmoothing (and its damage twin): ease the act step log-linearly between act starts"""
    now = (1 + ratio * act) * (1 + per_min * into * act_min)
    anchor = lambda i: (1 + ratio * 2) * (1 + per_min * act_min) if i >= 3 else 1 + ratio * i
    import math
    target = math.exp(math.log(max(0.01, anchor(act))) + (math.log(max(0.01, anchor(act + 1))) - math.log(max(0.01, anchor(act)))) * into)
    return min(5.0, max(0.2, target / max(0.01, now)))


# ---------------------------------------------------------------- reap system + early curve (Nov 9 reap core; Docs/Balance.md section 12)
# ASSUMPTIONS (replace with playtest numbers):
REAP_USE = 0.7                  # share of reap cooldowns a player actually swings (aiming, kiting, bots ~ the same)
REAP_AIM = 1.5                  # the player aims the cone at the crowd: x the enemies a random cone would hold
RIPE_K = 0.3                    # share of an enemy's life spent ripe when it takes many hits (x (1 - 1 / hits-to-kill))
REAP_KILL_CAP = 0.6             # reaps are at most this share of a reaper's kills (the auto weapons kill the rest)
REAP_BOSS_UPTIME = 0.5          # share of reap swings that reach the boss (380 cm cone; its slams punish standing close)
ULT_SHARE_NEAR = 0.6            # share of a teammate's reaps that happen within UltShareRange of you
CATCHUP_REF_CLEAR = 1.0 / 0.75  # the curve's intent: spawns at ~75% of kill capacity (clear 1.33); the catch-up drifts HP toward it


def spawn_curve(d, t, act, into, act_s):
    """ASQHordeManager::SpawnCurveRate (solo, before crew / mission multipliers). Act 1: SpawnRateStart -> SpawnRateEarly over the first
    SpawnEarlyMinutes (eased), then linear to SpawnRatePeak at the act end. Acts 2-3: lerp(SpawnRateActStart, SpawnRatePeak, act progress)."""
    start, early = d.t("SpawnRateStart", 0.6), d.t("SpawnRateEarly", 2.0)
    early_min, peak = max(0.01, d.t("SpawnEarlyMinutes", 2.0)), d.t("SpawnRatePeak", 8.0)
    if act > 0:
        return d.t("SpawnRateActStart", 5.0) + (peak - d.t("SpawnRateActStart", 5.0)) * into
    minutes, act_min = t / 60.0, act_s / 60.0
    if minutes < early_min:
        return start + (early - start) * (minutes / early_min) ** d.t("SpawnEarlyEaseExp", 1.5)
    return early + (peak - early) * min(1.0, max(0.0, (minutes - early_min) / max(0.01, act_min - early_min)))


def early_hp_mult(d, t):
    """ASQHordeManager::EarlyHealthMult: new enemies' HP x EarlyHealthStart at the stage start, easing to 1 at EarlyHealthMinutes"""
    k = min(1.0, max(0.0, t / 60.0 / max(0.01, d.t("EarlyHealthMinutes", 4.0))))
    return d.t("EarlyHealthStart", 0.4) + (1 - d.t("EarlyHealthStart", 0.4)) * k


def on_screen_cap(d, t, players, spawn_mult=1.0):
    """ASQHordeManager::OnScreenCap: OnScreenCapStart up to OnScreenCapFlatMinutes, + OnScreenCapPerMinute after, x crew x spawn mult"""
    m = t / 60.0
    cap = d.t("OnScreenCapStart", 30) + d.t("OnScreenCapPerMinute", 25) * max(0.0, m - d.t("OnScreenCapFlatMinutes", 1))
    return min(d.t("MaxEnemies", 800), cap * (1 + d.t("PlayerSpawnRateBonus", 0.5) * (players - 1)) * spawn_mult)


def streak_mult(p, smax):
    """Average streak value of a reap when each reap swing connects with probability p (a miss breaks the chain)"""
    p = min(0.999, max(0.0, p))
    return sum(k * (1 - p) * p ** (k - 1) for k in range(1, smax)) + smax * p ** (smax - 1)


def reap_numbers(d, b, avg_hp, hits, kr_each):
    """USQReapComponent for one reaper: swings / s, reaped souls / s (executions + scythe kills), average streak, the scythe's horde
    DPS (swing damage on non-ripe + the ripe HP skipped by executions) and boss DPS (ReapBossBonus x damage)"""
    import math
    swings = REAP_USE / max(0.2, d.t("ReapCooldown", 1.2))
    rng_, half = d.t("ReapRange", 380), d.t("ReapHalfAngle", 35)
    n = min(12.0, 3.14159 * rng_ * rng_ * (2 * half) / 360.0 * DENSITY * REAP_AIM)
    dmg = d.t("ReapDamage", 8) * (1 + d.t("ReapDamagePerLevel", 0.04) * (b.level - 1)) * b.damage_mult * b.crit_avg()
    ripe = RIPE_K * (d.t("ReapRipeFraction", 0.25) / 0.25) * max(0.0, 1 - 1 / max(1.0, hits))
    left = max(1.0, avg_hp * (1 - d.t("ReapRipeFraction", 0.25)) / 2 + avg_hp * d.t("ReapRipeFraction", 0.25))   # a non-ripe enemy's HP
    p_kill = min(1.0, dmg / left)
    per_swing = n * (ripe + (1 - ripe) * p_kill)
    reaps = min(swings * per_swing, REAP_KILL_CAP * max(0.0, kr_each))
    lam = reaps / max(1e-6, swings)
    mult = streak_mult(1 - math.exp(-lam), int(d.t("StreakMax", 5)))
    horde = swings * n * ((1 - ripe) * min(dmg, left) + ripe * d.t("ReapRipeFraction", 0.25) / 2 * avg_hp)
    boss = swings * REAP_BOSS_UPTIME * dmg * d.t("ReapBossBonus", 3)
    return dict(swings=swings, reaps=reaps, mult=mult, horde=horde, boss=boss, dmg=dmg, ripe=ripe, per_swing=per_swing)


def ult_period(d, b, reaps_self, reaps_mates, kr_each, players):
    """USQAbilityComponent::AddUltCharge: R charges UltChargePerReap per reaped soul (+ UltShareFraction of a nearby teammate's) and
    UltChargePerKill per kill, x UltRefCooldown / the kit's R cooldown (evolution and haste included). Returns seconds per full charge."""
    if not b.kit:
        return None
    cd = ability_numbers(d, b.kit, "R", b, getattr(b, "kit_evo", False))["cd"]
    per_s = reaps_self * d.t("UltChargePerReap", 0.03) + reaps_mates * d.t("UltChargePerReap", 0.03) * d.t("UltShareFraction", 0.5) * ULT_SHARE_NEAR \
        + kr_each * d.t("UltChargePerKill", 0.0015)
    per_s *= d.t("UltRefCooldown", 70) / max(1.0, cd)
    return min(900.0, 1.0 / max(1e-4, per_s))


def sim_state_at(d, m, hard, players, t, boss=False, into_override=None):
    """Horde numbers t seconds into the stage: act, progress in act, spawn rate, mix, avg normal HP, elite HP share,
    avg XP / souls per kill, average enemy hit, Brute HP"""
    act_s = sim_act_len(m)
    act = min(2, int(t // act_s))
    into = min(1.0, (t - act * act_s) / act_s) if into_override is None else into_override
    act_min = act_s / 60.0
    sm = mission_mult(m, "SpawnMult", hard)
    rate = spawn_curve(d, t, act, into, act_s) * (1 + d.t("PlayerSpawnRateBonus", 0.35) * (players - 1)) * sm
    if boss:
        rate *= d.t("BossSpawnRateMult", 0.35)
    mix = enemy_mix(d, act, into)
    hp_smooth = stage_smoothing(d, act, into, act_min, d.t("StageHpRefRatio", 3), d.t("EnemyHealthPerMinute", 0.1))
    hp_mult = (1 + d.t("EnemyHealthPerMinute", 0.1) * into * act_min) * (1 + d.t("EnemyHealthPerPlayer", 0.35) * (players - 1)) \
        * max(0.1, d.t("EconomyEnemyHealth%d" % (act + 1), 1.0)) * mission_mult(m, "HealthMult", hard) * hp_smooth \
        * (early_hp_mult(d, t) if act == 0 else 1.0)
    tot_w = sum(w for e, w in mix) or 1.0
    norm = [(e, w) for e, w in mix if e["Behaviour"] != "Elite"]
    nw = sum(w for e, w in norm) or 1.0
    hp_of = lambda e: (num(e["Health"]) + num(e["HealthPerNight"]) * act) * hp_mult
    avg_hp = sum(hp_of(e) * w for e, w in norm) / nw
    elite_w = (tot_w - nw) / tot_w
    brute = next(e for e in d.enemies if e["Id"] == "Brute")
    brute_hp = hp_of(brute)
    elite_hp_share = elite_w * brute_hp * 1.15 / max(1e-6, elite_w * brute_hp * 1.15 + (1 - elite_w) * avg_hp)
    xp = sum(num(e["XP"]) * w for e, w in mix) / tot_w
    xp_norm = sum(num(e["XP"]) * w for e, w in norm) / nw
    souls = sum(num(e["Souls"]) * w for e, w in mix) / tot_w
    melee = [(e, w) for e, w in mix if e["Behaviour"] in ("Chase", "Hound", "Splitter", "Spitter", "Elite")]
    mw = sum(w for e, w in melee) or 1.0
    dsm = 1.0
    if d.t("StageDamageSmoothing", 0) > 0.5:
        dsm = stage_smoothing(d, act, into, act_min, d.t("EnemyDamagePerNight", 0.3), d.t("EnemyDamagePerMinute", 0.03))
    dmg_scale = (1 + d.t("EnemyDamagePerNight", 0.3) * act) * (1 + d.t("EnemyDamagePerMinute", 0.03) * into * act_min) * dsm \
        * mission_mult(m, "DamageMult", hard)
    hit = sum(num(e["Damage"]) * w for e, w in melee) / mw * dmg_scale
    return dict(act=act, into=into, rate=rate, avg_hp=avg_hp, brute_hp=brute_hp, elite_share=elite_w, elite_hp_share=elite_hp_share,
                xp=xp, xp_norm=xp_norm, souls=souls, hit=hit, dmg_scale=dmg_scale, hp_mult=hp_mult)


# ---------------------------------------------------------------- one reaper's build (cards, items, raises, augments, meta)
class SimBuild(Build):
    def __init__(self, d, reaper_id, meta_raw=None):
        self.d = d
        self.rules = "new"
        self.plan = dict(PLANS["typical"])
        self.reaper = next(r for r in d.reapers if r["Id"] == reaper_id)
        self.rid = reaper_id
        self.kit = d.kits.get(reaper_id)
        sig = self.kit["SignatureWeaponId"]
        self.sig = sig
        self.evo = next((u for u in d.ups.values() if u["Kind"] == "Evolution" and u["EvolvesFrom"] == sig), None)
        self.signature = True
        self.main = {"id": sig, "level": 1, "power": 1.0}
        self.weapons = []                 # shared weapons: dict(id, level, power)
        self.others = []
        self.tomes = {}                   # id -> [level, power]
        self.items = {}                   # id -> power (sum of stack rarity multipliers)
        self.item_stacks = {}
        self.raise_stats = {}
        self.augs = []
        self.meta_raw = dict(meta_raw or {})
        self.level = 1
        self.evolved = False
        self.kit_evo = False
        self.raises = 0
        self.fallback_picks = 0
        self.relics = {}
        self.ctx = dict(kr=2.0, avg_hp=40.0, hit_rate=1.0, crew=1, aura_share=0.75, elite_hp_share=0.3)
        self.kr = 2.0
        self.ability_speed = 1.0
        self.dash_every = DASH_EVERY
        self.recompute()

    def clone(self):
        import copy
        c = copy.copy(self)
        c.main = dict(self.main)
        c.weapons = [dict(w) for w in self.weapons]
        c.tomes = {k: list(v) for k, v in self.tomes.items()}
        c.items = dict(self.items)
        c.item_stacks = dict(self.item_stacks)
        c.raise_stats = dict(self.raise_stats)
        c.augs = list(self.augs)
        c.meta_raw = dict(self.meta_raw)
        c.ctx = dict(self.ctx)
        c.ctx["ally_auras"] = dict(self.ctx.get("ally_auras", {}))
        return c.recompute()

    # -- slots (ASQCharacter::GetWeaponSlots / GetTomeSlots + USQAugments::ExtraSlots, clamp 4)
    def aug_count(self, effect):
        return sum(1 for a in self.augs if self.d.aug_by_id[a]["Effect"] == effect)

    def weapon_slots(self):
        extra = min(4, sum(int(num(self.d.aug_by_id[a]["Value"])) for a in self.augs if self.d.aug_by_id[a]["Effect"] == "WeaponSlot"))
        return int(self.d.t("WeaponSlots", 4)) + extra

    def tome_slots(self):
        extra = min(4, sum(int(num(self.d.aug_by_id[a]["Value"])) for a in self.augs if self.d.aug_by_id[a]["Effect"] == "TomeSlot"))
        return int(self.d.t("TomeSlots", 4)) + extra

    def level_of(self, uid):
        if uid == self.main["id"]:
            return self.main["level"]
        for w in self.weapons:
            if w["id"] == uid:
                return w["level"]
        if uid in self.tomes:
            return self.tomes[uid][0]
        return 0

    def can_take(self, u):
        """ASQCharacter::CanTakeUpgrade"""
        cat = category(u)
        lvl = self.level_of(u["Id"])
        if cat == "Weapon":
            if is_signature(u):
                return u["Id"] == self.sig and not self.evolved and lvl < int(num(u["MaxLevel"], 8))
            if lvl > 0:
                return lvl < int(num(u["MaxLevel"], 8))
            if len(self.weapons) >= self.weapon_slots():
                return False
            if any(w.get("from") == u["Id"] for w in self.weapons):
                return False
            return True
        if cat == "Tome":
            return lvl < int(num(u["MaxLevel"], 5)) if lvl > 0 else len(self.tomes) < self.tome_slots()
        if cat == "Evolution":
            if is_signature(u):
                return (u["EvolvesFrom"] == self.sig and not self.evolved and self.main["level"] >= 8
                        and self.tomes.get(u["RequiresRelic"], [0])[0] > 0)
            base = next((w for w in self.weapons if w["id"] == u["EvolvesFrom"]), None)
            return bool(base) and base["level"] >= 8 and self.tomes.get(u["RequiresRelic"], [0])[0] > 0
        return False

    def take(self, u, rarity):
        """ASQCharacter::ApplyUpgrade"""
        cat = category(u)
        if cat == "Weapon":
            bonus = WEAPON_POWER[rarity]
            if u["Id"] == self.main["id"]:
                self.main["level"] += 1
                self.main["power"] += bonus
            else:
                w = next((w for w in self.weapons if w["id"] == u["Id"]), None)
                if w:
                    w["level"] += 1
                    w["power"] += bonus
                else:
                    self.weapons.append(dict(id=u["Id"], level=1, power=1.0 + bonus))
        elif cat == "Tome":
            t = self.tomes.setdefault(u["Id"], [0, 0.0])
            t[0] += 1
            t[1] += STAT_MULT[rarity]
        elif cat == "Evolution":
            if is_signature(u):
                self.main = {"id": u["Id"], "level": 1, "power": self.main["power"]}
                self.evolved = True
            else:
                w = next(w for w in self.weapons if w["id"] == u["EvolvesFrom"])
                w["from"] = w["id"]
                w["id"] = u["Id"]
                w["level"] = 1
        self.kit_evo = self.tomes.get(self.kit["EvoTome"], [0])[0] >= int(num(self.d.ups.get(self.kit["EvoTome"], {}).get("MaxLevel"), 5))

    def add_item(self, u, rarity):
        cap = int(num(u.get("MaxStacks"), 0))
        if cap > 0 and self.item_stacks.get(u["Id"], 0) >= cap:
            return False
        self.item_stacks[u["Id"]] = self.item_stacks.get(u["Id"], 0) + 1
        self.items[u["Id"]] = self.items.get(u["Id"], 0.0) + STAT_MULT[rarity]
        return True

    def refresh(self):
        """Kill-rate dependent bits: ability / dash cooldown refunds (Ruthless Rhythm, capped by AugmentRefundPerSecond) and
        casts per second (Spell Echo / Tailwind)"""
        d = self.d
        self.recompute()
        refund = sum(num(d.aug_by_id[a]["Value"]) for a in self.augs if d.aug_by_id[a]["Effect"] == "KillRefund")
        cap = d.t("AugmentRefundPerSecond", -1)
        rate = self.kr * refund
        if cap >= 0:
            rate = min(rate, cap)
        self.ability_speed = 1.0 + rate
        dash_cd = max(0.4, self.stats.get("DashCooldown", 2.5))
        self.dash_every = max(0.7, min(DASH_EVERY, dash_cd / (1.0 + rate)))
        casts = 0.0
        if self.kit and self.level >= d.t("AbilityUnlockE", 3):
            ae = ability_numbers(d, self.kit, "E", self, self.kit_evo)
            casts += ABILITY_UPTIME["E"] / max(0.35, ae["cd"] / self.ability_speed)
            if self.level >= d.t("AbilityUnlockR", 6):
                ar = ability_numbers(d, self.kit, "R", self, self.kit_evo)
                r_cd = getattr(self, "ult_period", None) or ar["cd"]
                casts += ABILITY_UPTIME["R"] / max(0.35, r_cd / self.ability_speed)
        self.ctx["casts_per_s"] = casts
        return self

    def raw_stats(self, extra=None):
        d, r = self.d, self.reaper
        raw = {"MaxHealth": num(r["MaxHealth"], 120), "MoveSpeed": num(r["MoveSpeed"], 650), "CarryCap": num(r["CarryCap"], 50),
               "Magnet": num(r["Magnet"], 300), "DashCooldown": num(r["DashCooldown"], 2.5), "CritChance": 0.05, "CritDamage": 0.5}
        add_stats(d, raw, {t: v[1] for t, v in self.tomes.items()})
        add_stats(d, raw, self.items)
        for k, v in self.raise_stats.items():
            raw[k] = raw.get(k, 0.0) + v
        for k, v in self.meta_raw.items():
            raw[k] = raw.get(k, 0.0) + v
        for a in self.augs:
            g = d.aug_by_id[a]
            if g["Effect"] == "Stat" or (g["Effect"] == "AuraStat"):
                share = 1.0 if g["Effect"] == "Stat" else 1.0
                for st, val in ((g["Stat"], num(g["Value"])), (g.get("Stat2", ""), num(g["Value2"]) if g["Effect"] == "Stat" else 0.0)):
                    if st:
                        raw[st] = raw.get(st, 0.0) + (-val if st == "DashCooldown" else val) * share
        for k, v in self.ctx.get("ally_auras", {}).items():
            raw[k] = raw.get(k, 0.0) + v
        for k, v in (extra or {}).items():
            raw[k] = raw.get(k, 0.0) + v
        return raw

    def recompute(self, extra=None):
        s = finalize_stats(self.d, self.raw_stats(extra), "new")
        self.stats = s
        self.damage_mult = s["DamageMult"]
        self.cooldown_mult = s["CooldownMult"]
        self.crit_chance = s["CritChance"]
        self.crit_damage = s["CritDamage"]
        self.area = s["Area"]
        self.projectiles = s["Projectiles"]
        self.duration = s["Duration"]
        self.max_hp = s.get("MaxHealth", 120)
        self.armor = s["Armor"]
        self.evasion = s["Evasion"]
        self.relics = {t: v[1] for t, v in self.tomes.items()}
        self.relics.update(self.items)
        self.others = [[w["level"], w["power"]] for w in self.weapons]
        return self

    def all_weapons(self):
        return [(self.d.ups[self.main["id"]], self.main["level"], self.main["power"])] + \
            [(self.d.ups[w["id"]], w["level"], w["power"]) for w in self.weapons]

    # -- DPS (weapons + kit + augments); generic is ignored: a sim build knows its real weapons
    def weapon_horde(self):
        return sum(self.aoe_dps(u, lv, pw) for u, lv, pw in self.all_weapons())

    def weapon_boss(self):
        return sum(self.boss_dps(u, lv, pw) for u, lv, pw in self.all_weapons())

    def horde_dps(self, generic=None, kit=True, procs=True):
        base = self.weapon_horde()
        total = base
        if kit and self.kit:
            k = kit_power(self.d, self.kit, self, base, None)
            total = (base + k["horde_add"]) * k["self_mult"]
        fx = aug_effects(self, total)
        return total * fx["horde_mult"] + (fx["horde_add"] if procs else 0.0)

    def single_target_dps(self, generic=None, kit=True, vs_boss=False):
        base = self.weapon_boss()
        total = base
        if kit and self.kit:
            k = kit_power(self.d, self.kit, self, None, base, vs_boss=vs_boss)
            total = (base + k["boss_add"]) * k["self_mult"] * k["boss_mult"]
        fx = aug_effects(self, total)
        if vs_boss:
            return total * fx["boss_mult"] + fx["boss_add"]
        return total * fx["single_mult"] + fx["boss_add"]

    def ehp(self):
        s = self.stats
        fx = aug_effects(self, 0.0)
        heal = (s.get("Regen", 0.0) + fx["regen_add"] + s.get("PickupHeal", 0.0) * SOULS_PER_SECOND) * REGEN_WINDOW
        return (self.max_hp + heal) / max(0.05, (1 - self.armor) * (1 - self.evasion))

    def kit_util(self):
        if not self.kit:
            return 0.0
        return kit_power(self.d, self.kit, self, 1.0, 1.0)["util"]

    def hit_metric(self):
        """The hit a player reads as 'my hit': the biggest non-tick weapon hit (aura / orbit ticks excluded)"""
        hits = [self.hit(u, lv, pw) for u, lv, pw in self.all_weapons() if u["Behaviour"] not in ("Aura", "Orbit")]
        return max(hits) if hits else self.main_hit()

    def max_numbers(self):
        """(biggest weapon crit, biggest ability crit) this build can show"""
        w = max(self.hit(u, lv, pw) for u, lv, pw in self.all_weapons()) * (1 + self.crit_damage)
        ab = 0.0
        if self.kit and self.level >= self.d.t("AbilityUnlockE", 3):
            scale = 1 + self.d.t("AbilityDamagePerLevel", 0.05) * (self.level - 1)
            for slot in ("E", "R"):
                if slot == "R" and self.level < self.d.t("AbilityUnlockR", 6):
                    continue
                a = ability_numbers(self.d, self.kit, slot, self, self.kit_evo)
                ab = max(ab, max(a["dmg"], a["final"]) * scale * self.damage_mult * (1 + self.crit_damage))
        return w, ab


# ---------------------------------------------------------------- augments (USQAugments effects -> DPS / EHP / utility)
def aug_targets(d, radius, density_mult=1.0, cap=16.0):
    """enemies inside an augment blast (QueueBlast radius is NOT scaled by Area) incl. their body radius"""
    return min(cap, 3.14159 * (radius + d.enemy_radius) ** 2 * DENSITY * density_mult)


def chain_generations(branch, depth_cap):
    """sum of branch^g for g = 0..depth_cap (depth_cap None = only the queue caps: a supercritical chain wipes the area)"""
    if depth_cap is None:
        if branch >= 1.0:
            return float("inf")
        return 1.0 / (1.0 - branch)
    return sum(branch ** g for g in range(int(depth_cap) + 1))


def aug_effects(b, base_dps):
    """What b's augments add. horde_mult / horde_add (DPS on the horde), single_mult (elites / minibosses / nests),
    boss_mult / boss_add (the boss: only stat augments and area blasts reach it), regen_add, util %."""
    d = b.d
    out = dict(horde_mult=1.0, horde_add=0.0, single_mult=1.0, boss_mult=1.0, boss_add=0.0, regen_add=0.0, util=0.0, chain=1.0)
    if not b.augs:
        return out
    L = b.level
    scale = (1 + d.t("AugmentDamagePerLevel", 0.06) * (L - 1)) * b.damage_mult
    kr = b.ctx.get("kr", 2.0)
    avg_hp = max(1.0, b.ctx.get("avg_hp", 40.0))
    depth_cap = d.t("AugmentChainMaxDepth", -1)
    depth_cap = None if depth_cap < 0 else depth_cap
    kill_frac = lambda dmg: min(1.0, dmg / (0.6 * avg_hp)) * 0.8
    blast_p, blast_rows = 0.0, []
    for a in b.augs:
        g = d.aug_by_id[a]
        eff, v1, v2, v3 = g["Effect"], num(g["Value"]), num(g["Value2"]), num(g["Value3"])
        if eff == "KillBlast":
            blast_rows.append((v1, v2, v3 * scale))
            blast_p += v1
        elif eff == "HitSplit":
            e = v1 * v3 * v2 * 0.55
            out["horde_mult"] *= 1 + (e / (1 - e) if e < 1 else 50.0)
        elif eff == "Overkill":
            main_hit = max(1.0, b.main_hit())
            h = avg_hp / main_hit
            share = 0.5 / max(1.0, h) if h >= 1 else min(2.0, (1 - h) / max(0.2, h))
            out["horde_mult"] *= 1 + share * v1 * min(6.0, aug_targets(d, v2)) * 0.35
        elif eff == "Execute":
            out["horde_mult"] *= 1 + 0.7 * v1 / (1 - v1)
            out["single_mult"] *= 1 + 0.5 * v1 / (1 - v1)
        elif eff == "Gravestones":
            stones = min(12, int(v1))
            tick = max(0.1, d.t("AugmentOrbitTick", 0.4))
            out["horde_add"] += stones * min(v3 * scale, avg_hp) * aug_targets(d, 90) / tick * 0.8
            out["boss_add"] += stones * v3 * scale / tick * 0.25
        elif eff == "CastNova":
            cps = b.ctx.get("casts_per_s", 0.15)
            out["horde_add"] += cps * min(v3 * scale, avg_hp) * aug_targets(d, v2)
            out["boss_add"] += cps * v3 * scale * 0.6
        elif eff == "HurtNova":
            rate = min(1.0 / max(0.25, v2), CONTACTS_PER_SECOND * (1 - b.evasion) * 0.5)
            out["horde_add"] += rate * min(v3 * scale, avg_hp) * aug_targets(d, v1)
            out["boss_add"] += rate * v3 * scale * 0.3
        elif eff == "EliteHunter":
            out["single_mult"] *= 1 + v1
            out["horde_mult"] *= 1 + v1 * b.ctx.get("elite_hp_share", 0.3)
        elif eff == "LowHPDamage":
            out["horde_mult"] *= 1 + v1 * 0.35 * v2       # judgement: time spent below the threshold ~ 0.35 x threshold
        elif eff == "KillHeal":
            out["regen_add"] += kr * v1 * v2
        elif eff == "CastHaste":
            # movement while it lasts (move speed is worth ELASTICITY 1.0 per %): +v1 speed for v2 s after every cast
            cps = b.ctx.get("casts_per_s", 0.15)
            out["util"] += 100 * (v1 / max(200.0, b.stats.get("MoveSpeed", 650))) * min(1.0, cps * v2) * 0.5
        elif eff == "QuickRevive":
            out["util"] += REVIVE_VALUE * (1 if b.ctx.get("crew", 1) > 1 else 0)
    if blast_rows:
        # one kill can fire several KillBlast augments; a blast's kills fire them again (chains), up to the depth cap
        n_avg = sum(p * aug_targets(d, r) for p, r, _x in blast_rows) / max(1e-6, blast_p)
        dmg_avg = sum(p * x for p, _r, x in blast_rows) / max(1e-6, blast_p)
        branch = blast_p * n_avg * kill_frac(dmg_avg)
        gens = chain_generations(branch, depth_cap)
        out["chain"] = gens
        gens_used = min(gens, 6.0)        # damage past ~6 generations has no horde left to land on (the area is cleared)
        # useful damage only: an enemy in the blast takes at most what it has left (~0.8 x the average HP)
        out["horde_add"] += kr * sum(p * min(x, 0.8 * avg_hp) * aug_targets(d, r) for p, r, x in blast_rows) * 0.7 * gens_used
        out["boss_add"] += 0.08 * kr * sum(p * x for p, r, x in blast_rows)
    return out


# ---------------------------------------------------------------- Death's Office (greedy order: cheapest rank first)
def meta_greedy_ranks(d, n):
    mission_rows(d)
    ranks = {}
    for _ in range(n):
        best = None
        for row in d.meta:
            c = meta_cost(row, ranks.get(row["Id"], 0))
            if c is not None and (best is None or c < best[0]):
                best = (c, row["Id"])
        if not best:
            break
        ranks[best[1]] = ranks.get(best[1], 0) + 1
    return ranks


def meta_total_ranks(d):
    mission_rows(d)
    return sum(int(num(r["MaxRank"], 1)) for r in d.meta)


# ---------------------------------------------------------------- card / augment pick heuristics (the "AI player")
OFFENCE_TOMES = ("Whetstone", "Hourglass", "Monocle", "RedInk", "GraveDust", "SplitSoul", "EmbalmerPatience")
TOME_PREF = {"Whetstone": 35.0, "Hourglass": 34.0, "Monocle": 31.0, "GraveDust": 30.0, "RedInk": 29.0, "SplitSoul": 29.0,
             "EmbalmerPatience": 22.0}       # a sensible player: damage and attack speed first, crit next, duration last
DEFENCE_TOMES = ("ExtraRibs", "BonePlating", "Embalming", "TeaUrn")


def card_score(b, u, rarity):
    cat = category(u)
    if cat == "Evolution":
        return 100.0
    if cat == "Weapon":
        if u["Id"] == b.main["id"]:
            return 60.0 + 4 * rarity
        if b.level_of(u["Id"]) > 0:
            return 40.0 + 4 * rarity
        return (46.0 if len(b.weapons) < 2 else 34.0) + 4 * rarity
    if cat == "Tome":
        tid = u["Id"]
        if tid == b.kit["EvoTome"] or (b.evo and tid == b.evo["RequiresRelic"]):
            return 55.0 + 4 * rarity
        evo_tome = b.evo["RequiresRelic"] if b.evo else b.kit["EvoTome"]
        if tid not in b.tomes and evo_tome not in b.tomes and b.tome_slots() - len(b.tomes) <= 1:
            return 5.0          # keep the last tome slot for the signature's evolution tome (players learn the recipes)
        evo_for = [w for w in b.weapons if any(e["EvolvesFrom"] == w["id"] and e["RequiresRelic"] == tid for e in b.d.ups.values() if e["Kind"] == "Evolution")]
        if evo_for:
            return 38.0 + 4 * rarity
        if tid in TOME_PREF:
            return TOME_PREF[tid] + 4 * rarity
        if tid in DEFENCE_TOMES:
            have_def = any(t in DEFENCE_TOMES for t in b.tomes)
            return (33.0 if not have_def else 24.0) + 4 * rarity
        return 10.0 + 4 * rarity
    return 0.0


AUG_PRIOR = {"Stat": 1.0, "KillBlast": 1.3, "HitSplit": 1.0, "KillRefund": 1.1, "WeaponSlot": 1.2, "TomeSlot": 1.1, "Gravestones": 1.15,
             "QuickRevive": 0.9, "AuraStat": 1.2, "Execute": 1.1, "Overkill": 1.0, "KillHeal": 0.6, "KillGold": 0.5, "EliteHunter": 0.9,
             "LowHPDamage": 0.8, "CastNova": 1.0, "CastHaste": 0.6, "HurtNova": 0.9, "Pardon": 1.0, "LevelUps": 0.9, "Rerolls": 0.4,
             "ChestNow": 0.9, "XPBoost": 0.8}
AUG_TIER_ODDS = {"Miniboss1": (35, 50, 15), "Miniboss2": (15, 50, 35), "Objective": (40, 45, 15), "Level": (60, 35, 5)}


def roll_augment_offer(d, rng, b, source, crew):
    w = AUG_TIER_ODDS[source]
    pool = [g for g in d.augments if num(g["Weight"]) > 0 and not (num(g["CoopOnly"]) > 0.5 and crew < 2)
            and not (int(num(g["MaxStacks"], 1)) > 0 and b.augs.count(g["Id"]) >= int(num(g["MaxStacks"], 1)))]
    out = []
    for _ in range(3):
        if not pool:
            break
        live = [w[t] if any(int(num(g["Tier"])) == t for g in pool) else 0 for t in range(3)]
        roll = rng.random() * sum(live)
        tier = 0
        for t in range(3):
            roll -= live[t]
            if roll <= 0 and live[t] > 0:
                tier = t
                break
        cands = [g for g in pool if int(num(g["Tier"])) == tier]
        tot = sum(num(g["Weight"]) for g in cands)
        roll = rng.random() * tot
        pick = cands[-1]
        for g in cands:
            roll -= num(g["Weight"])
            if roll <= 0:
                pick = g
                break
        out.append(pick)
        pool.remove(pick)
    return out


def choose_augment(d, offer, b):
    vals = getattr(d, "aug_value", None)
    def score(g):
        if vals and g["Id"] in vals:
            return vals[g["Id"]]
        return (1 + int(num(g["Tier"]))) * AUG_PRIOR.get(g["Effect"], 1.0)
    return max(offer, key=score)


# ---------------------------------------------------------------- the run
def objective_seconds(d, m, hard, players, typ, count, team, st, level):
    """How long an objective takes this crew (FSQMissionDirector + ASQObjective::ServerTickPart), before its time limit"""
    if typ == "BankSouls":
        solo = d.t("SoloQuotaMult", 0.5)
        need = count * (solo + (1 - solo) * min(1.0, (players - 1) / 3.0))
        souls_per_s = st["rate"] * team["kill_share"] * SOULS_PER_KILL_PICKED * max(0.5, st["souls"]) \
            + team.get("reaps", 0.0) * d.t("ReapSoulBonus", 1) * (1 - SOUL_LOSS)     # reap core: reaped souls go straight into the sack
        return need / max(0.1, souls_per_s) + BANK_TRIP
    if typ == "DestroySpawners":
        act = st["act"]
        nest_hp = d.t("NestHealth", 1500) * (1 + d.t("NestHealthPerAct", 1) * act) * (1 + d.t("EnemyHealthPerPlayer", 0.35) * (players - 1)) \
            * mission_mult(m, "HealthMult", hard)
        siege = d.t("NestSiegeDPS", 30) * (1 + d.t("AugmentDamagePerLevel", 0.06) * (level - 1)) * team["dmg_mult_sum"]
        dps = siege + d.t("NestHitShare", 0.5) * 0.3 * team["horde"] + 0.5 * team["single"]
        return count * (nest_hp / max(1.0, dps) + OBJ_TRAVEL) / max(1.0, min(players, 2) * 0.75 + 0.25)
    if typ == "DefendPoint":
        fill = 1 + 0.25 * (players - 1)
        return count * OBJ_FILL_SLOWDOWN / fill
    if typ == "Escort":
        return ESCORT_SECONDS
    if typ == "CollectItems":
        return count * URN_SECONDS / min(2.0, 1.0 + 0.5 * (players - 1))
    if typ == "SurviveWave":
        return float(count)
    if typ == "KillElite":
        # the hunted elites spawn at a landmark (KillEliteHealthMult x a Brute), but ANY elite / crowned kill counts
        hp = st["brute_hp"] * d.t("KillEliteHealthMult", 4) * (1 + 0.3 * (d.t("CrownedHealthMult", 1.5) - 1))
        hunt = 1.0 / (hp / max(1.0, team["single"] * 0.8) + OBJ_TRAVEL)
        horde = st["rate"] * team["kill_share"] * st["elite_share"]
        return count / max(1e-3, hunt + horde)
    return 80.0


def sim_run(d, m, hard, players, meta_n=0, seed=0, forced_aug=None, reapers=None, record=False, skill=None, stop_at=None):
    """Simulate one mission run. Returns a dict of results (and per-minute curves when record=True)."""
    import random
    rng = random.Random(seed * 7919 + players * 31 + (17 if hard else 0) + sum(ord(c) for c in m["Id"]))
    mission_rows(d)
    ranks = meta_greedy_ranks(d, meta_n) if isinstance(meta_n, int) else dict(meta_n)
    raw, special = meta_extra(d, ranks)
    if reapers is None:
        reapers = [SIM_POOL[(seed * players + j) % len(SIM_POOL)] for j in range(players)]
    crew = [SimBuild(d, rid, raw) for rid in reapers]
    base_seed = seed * 7919 + players * 31 + (17 if hard else 0) + sum(ord(c) for c in m["Id"])
    rsalt = [sum(ord(c) * (k + 1) for k, c in enumerate(rid)) for rid in reapers]
    rng_cards = [random.Random(base_seed * 3 + 1 + 101 * i + rsalt[i]) for i in range(players)]
    rng_items = [random.Random(base_seed * 5 + 2 + 103 * i + rsalt[i]) for i in range(players)]
    rng_aug = [random.Random(base_seed * 7 + 3 + 107 * i + rsalt[i]) for i in range(players)]
    for b in crew:
        b.ctx["crew"] = players
    rerolls = [int(special.get("Rerolls", 0))] * players
    pardons = [int(special.get("Pardon", 0))] * players
    dur = num(m["Duration"])
    act_s = sim_act_len(m)
    objs = mission_objectives(m)
    n_obj = max(1, len(objs))
    first, gap = d.t("ObjectiveFirstDelay", 20), d.t("ObjectiveGap", 15)
    spread, early = d.t("ObjectiveSpread", 1.0), d.t("ObjectiveEarlyFactor", 0.88)
    sched = [first + dur * spread * k / n_obj for k in range(len(objs))]
    minis = [(num(x), y) for x, y in zip((m.get("MinibossAt") or "").split("|"), (m.get("Minibosses") or "").split("|")) if x.strip()]
    xp, level, need = 0.0, 1, xp_to_next(d, 1, players)
    gold = [0.0] * players
    earned = [0.0] * players
    opened = [0] * players
    chests_free = [0.0] * players
    raises_done = [0.0] * players
    xp_bonus = special.get("XPGain", 0.0)
    gold_gain = special.get("GoldGain", 0.0)
    goldm = mission_mult(m, "GoldMult", hard) * (1 + gold_gain)
    share = 1.0 / (1 + d.t("PlayerSpawnRateBonus", 0.35) * (players - 1))
    item_pool_ = item_pool(d)
    level_ups_pending = [int(special.get("StartLevels", 0))] * players
    aug_pending = [[] for _ in range(players)]
    if forced_aug:
        for i in range(players):
            aug_pending[i].append(("forced", forced_aug))
    curves = []
    obj_log = []
    defiance = 0
    t = 0.0
    obj_idx, obj_start, obj_end_last = 0, None, 0.0
    obj_took = None
    minis_done = 0
    mini_pending = []          # (kill time, index)
    bell_ready = None
    # Lantern trail (Nov 12 2026; FSQMissionDirector::TickTrail): lanterns light one after another across the map, each wants a
    # soul quota before its share of the clock runs out; the bell waits for the trail as well as the objectives
    trail_n = int(d.t("LanternStages", 4)) if d.t("LanternTrail", 1) >= 0.5 else 0
    trail_stage, trail_banked, trail_wait, trail_lit = 0, 0.0, 0.0, 0
    trail_log = []             # (stage, time it ended, banked in full?)
    team = dict(horde=1.0, single=1.0, kill_share=SIM_KILL_SHARE, dmg_mult_sum=float(players))
    dirty = True
    kills_total = 0.0
    min_clear, peak_hits, min_htd = 9.0, 0.0, 999.0
    early_hits = None
    early_t = dur / 6.0
    max_w, max_ab = 0.0, 0.0
    augs_taken = 0
    elite_kills = 0.0
    overtime = 0.0
    lp_done, shrine_done = False, False
    clear80 = None
    horde80, inflow80 = 0.0, 1.0
    # reap core (Nov 9): catch-up HP drift, last step's reap numbers per reaper, run totals for the reports
    catch = 1.0
    reap_now = [None] * players
    reap_tot, reap_xp_tot, reap_gold_tot, xp_tot, mult_w = 0.0, 0.0, 0.0, 0.0, 0.0
    ult_log = []
    level2_kills = None

    def grant_item(i, chest_luck=True):
        b = crew[i]
        luck = b.stats["Luck"] + (d.t("ChestLuckBonus", 0.25) if chest_luck else 0.0)
        odds = rarity_odds(d, luck)
        r = rng_items[i].random()
        rarity = 0
        for k, p in enumerate(odds):
            r -= p
            if r <= 0:
                rarity = k
                break
        pool = [u for u in item_pool_ if not (int(num(u.get("MaxStacks"), 0)) > 0 and b.item_stacks.get(u["Id"], 0) >= int(num(u.get("MaxStacks"), 0)))]
        if not pool:
            return
        tot = sum(num(u["Weight"]) for u in pool)
        roll = rng_items[i].random() * tot
        for u in pool:
            roll -= num(u["Weight"])
            if roll <= 0:
                b.add_item(u, rarity)
                return
        b.add_item(pool[-1], rarity)

    def do_level_up(i):
        nonlocal dirty
        b = crew[i]
        for attempt in range(1 + rerolls[i]):
            pool = [u for u in d.ups.values() if category(u) in ("Weapon", "Tome", "Evolution") and b.can_take(u)]
            offer = []
            for _ in range(3):
                if not pool:
                    break
                tot = sum(max(0.1, num(u["Weight"])) for u in pool)
                roll = rng_cards[i].random() * tot
                pick = pool[-1]
                for u in pool:
                    roll -= max(0.1, num(u["Weight"]))
                    if roll <= 0:
                        pick = u
                        break
                pool.remove(pick)
                odds = rarity_odds(d, b.stats["Luck"])
                r = rng_cards[i].random()
                rar = 0
                for k, p in enumerate(odds):
                    r -= p
                    if r <= 0:
                        rar = k
                        break
                offer.append((pick, 3 if category(pick) == "Evolution" else rar))
            if not offer:
                b.fallback_picks += 1
                return
            best = max(offer, key=lambda x: card_score(b, x[0], x[1]))
            sc = card_score(b, best[0], best[1])
            if sc < 20 and rerolls[i] > 0:
                rerolls[i] -= 1
                continue
            if sc < 12 and category(best[0]) == "Tome" and best[0]["Id"] not in b.tomes:
                b.fallback_picks += 1      # skip: a dead tome would take a slot for good
                return
            b.take(best[0], best[1])
            dirty = True
            return

    def apply_augment(i, g):
        nonlocal dirty, augs_taken
        b = crew[i]
        b.augs.append(g["Id"])
        augs_taken += 1 if i == 0 else 0
        eff = g["Effect"]
        if eff == "Pardon":
            pardons[i] = min(pardons[i] + max(1, int(num(g["Value"]))), int(d.t("MaxPardons", 9)))
        elif eff == "LevelUps":
            for _ in range(min(5, int(num(g["Value"])))):
                do_level_up(i)
        elif eff in ("WeaponSlot", "TomeSlot"):
            for _ in range(min(5, int(num(g["Value2"])))):
                do_level_up(i)
        elif eff == "Rerolls":
            rerolls[i] += int(num(g["Value"]))
        elif eff == "ChestNow":
            for _ in range(min(5, int(num(g["Value"])))):
                grant_item(i)
        elif eff == "AuraStat":
            for j, ob in enumerate(crew):
                if j != i:
                    ob.ctx.setdefault("ally_auras", {})
                    ob.ctx["ally_auras"][g["Stat"]] = ob.ctx["ally_auras"].get(g["Stat"], 0.0) + num(g["Value"]) * 0.75
        dirty = True

    def offer_augment(i, source):
        if forced_aug:
            return
        g = choose_augment(d, roll_augment_offer(d, rng_aug[i], crew[i], source, players), crew[i])
        apply_augment(i, g)

    # Head Start: free level-up picks at the start
    for i in range(players):
        for _ in range(level_ups_pending[i]):
            do_level_up(i)

    while True:
        if stop_at is not None and t >= stop_at:
            for b in crew:
                b.level = level
                b.recompute()
            return dict(crew=crew, level=level, t=t)
        st = sim_state_at(d, m, hard, players, min(t, dur - 1e-6))
        st["avg_hp"] *= catch
        st["brute_hp"] *= catch
        if dirty:
            casts = 0.0
            for bi, b in enumerate(crew):
                b.level = level
                b.recompute()
                b.ctx.update(kr=team.get("kr_each", 2.0), avg_hp=st["avg_hp"], elite_hp_share=st["elite_hp_share"])
                b.kr = team.get("kr_each", 2.0)
                rs = reap_now[bi]["reaps"] if reap_now[bi] else 0.0
                rm = sum(r["reaps"] for j, r in enumerate(reap_now) if r and j != bi)
                b.ult_period = ult_period(d, b, rs, rm, b.kr, players)
                b.refresh()
            team["horde"] = sum(b.horde_dps() for b in crew)
            team["single"] = sum(b.single_target_dps() for b in crew)
            team["dmg_mult_sum"] = sum(b.damage_mult for b in crew)
            for b in crew:
                w, ab = b.max_numbers()
                max_w, max_ab = max(max_w, w), max(max_ab, ab)
            dirty = False
        # ---- horde, kills, XP, gold
        inflow = st["rate"] * st["avg_hp"] * (1 - st["elite_share"]) + st["rate"] * st["elite_share"] * st["brute_hp"]
        reap_horde = sum(r["horde"] for r in reap_now if r)
        clear = (team["horde"] + reap_horde) / max(1e-6, inflow)
        # kill capacity the way the early-curve proposal counts it: one target per hit (single-target DPS + the scythe) / average HP.
        # The horde DPS above assumes a full crowd (DENSITY) around every reaper, which a 0.6-8 /s trickle does not provide.
        hp_in = inflow / max(1e-6, st["rate"])
        cap_single = (team["single"] + reap_horde) / max(1e-6, hp_in)
        cap_kps = (cap_single * st["rate"] * clear) ** 0.5     # KILL CAPACITY (assumption): geometric mean of one-target and full-crowd
        team["kill_share"] = min(0.9, max(0.55, SIM_KILL_SHARE * clear ** 0.3))
        kps = st["rate"] * team["kill_share"]
        team["kr_each"] = kps / players
        # catch-up (ASQHordeManager::TickCatchUp): new enemies' HP drifts toward the crew's kill capacity, at most +-CatchUpMax
        cmax = d.t("CatchUpMax", 0.25)
        if kps >= d.t("CatchUpMinKillRate", 0.5):
            ctarget = min(1 + cmax, max(1 - cmax, (cap_kps / max(1e-6, st["rate"]) / CATCHUP_REF_CLEAR) ** d.t("CatchUpExp", 0.5)))
            step = d.t("CatchUpDriftPerSecond", 0.01) * SIM_DT
            catch += max(-step, min(step, ctarget - catch))
        for bi, b in enumerate(crew):
            reap_now[bi] = reap_numbers(d, b, st["avg_hp"], st["avg_hp"] / max(0.01, b.hit_metric()), team["kr_each"])
        team["reaps"] = sum(r["reaps"] for r in reap_now)
        if t >= 60:
            min_clear = min(min_clear, clear)
        if clear80 is None and t >= 0.6 * dur:
            clear80 = clear
            horde80, inflow80 = sum(b.horde_dps(procs=False) for b in crew), inflow
        main_hits = st["avg_hp"] / max(0.01, statistics.median(b.hit_metric() for b in crew))
        if early_hits is None and t >= early_t:
            early_hits = main_hits
        htd = statistics.median(b.ehp() / max(0.1, st["hit"]) for b in crew)
        if record and abs(t / 60.0 - round(t / 60.0)) < 1e-6:
            curves.append(dict(t=t, level=level, avg_hp=st["avg_hp"], hits=main_hits, hit=st["hit"], htd=htd, clear=clear, rate=st["rate"],
                               cap_kps=cap_kps, cap1=cap_single, catch=catch, reaps=sum(r["reaps"] for r in reap_now if r),
                               streak=statistics.mean(r["mult"] for r in reap_now if r) if any(reap_now) else 1.0,
                               onscreen=on_screen_cap(d, t, players, mission_mult(m, "SpawnMult", hard)),
                               horde=team["horde"], single=team["single"], gold=gold[0], items=sum(crew[0].item_stacks.values()),
                               augs=len(crew[0].augs)))
        dt = SIM_DT
        k = kps * dt
        kills_total += k
        elite_kills += k * st["elite_share"]
        xp_mult = 1 + min(5.0, xp_bonus + sum(num(d.aug_by_id[a]["Value"]) for b in crew for a in b.augs if d.aug_by_id[a]["Effect"] == "XPBoost"))
        gain = k * st["xp"] * XP_PICKUP * xp_mult
        # reap core: every reaped soul pays ReapXP x the streak straight to the team, ReapSoulBonus souls into the sack, streak gold
        reaps_step = sum(r["reaps"] for r in reap_now) * dt
        reap_xp = sum(r["reaps"] * r["mult"] for r in reap_now) * dt * d.t("ReapXP", 1) * xp_mult
        gain += reap_xp
        reap_tot += reaps_step
        reap_xp_tot += reap_xp
        xp_tot += gain
        mult_w += sum(r["reaps"] * r["mult"] for r in reap_now) * dt
        growth = 1 + d.t("GoldPerNight", 1.5) * st["act"]
        coins = k * (1 - st["elite_share"]) * d.t("GoldDropChance", 0.04) * st["xp_norm"] * d.t("GoldPerXP", 1) * growth * GOLD_PICKUP
        el = k * st["elite_share"]
        el_gold = el * (1 + d.t("CrownedChance", 0.3) * (d.t("CrownedGoldMult", 3) - 1)) * d.t("EliteGold", 4) * growth * GOLD_PICKUP
        soul_gold = (k * SOULS_PER_KILL_PICKED * max(0.5, st["souls"]) + reaps_step * d.t("ReapSoulBonus", 1) * (1 - SOUL_LOSS)) * d.t("SoulGold", 0.25)
        streak_gold = sum(r["reaps"] * max(0.0, r["mult"] - 1) for r in reap_now) * dt * d.t("ReapStreakGold", 0.25) * growth
        reap_gold_tot += streak_gold * share * goldm
        per = (coins + el_gold + soul_gold + streak_gold) * share * goldm + POT_GOLD_STAGE * dt / dur * goldm
        for i, b in enumerate(crew):
            g_aug = sum(num(d.aug_by_id[a]["Value"]) * num(d.aug_by_id[a]["Value2"]) for a in b.augs if d.aug_by_id[a]["Effect"] == "KillGold")
            add = per + team["kr_each"] * dt * g_aug * growth
            gold[i] += add
            earned[i] += add
            chests_free[i] += el * d.t("CrownedChance", 0.3) * d.t("CrownedChestChance", 0.15) / players
        xp += gain
        if level2_kills is None and level == 1 and xp + 0 >= need:
            level2_kills = (kills_total) * (need / max(1e-6, xp))
        if level >= d.t("AbilityUnlockR", 6) and crew[0].kit and t < dur:
            ult_log.append((t, getattr(crew[0], "ult_period", None) or 0.0,
                            ability_numbers(d, crew[0].kit, "R", crew[0], crew[0].kit_evo)["cd"]))
        while xp >= need:
            xp -= need
            level += 1
            need = xp_to_next(d, level, players)
            for i in range(players):
                do_level_up(i)
            if level in (int(d.t("AugmentLevel1", 10)), int(d.t("AugmentLevel2", 20))):
                for i in range(players):
                    offer_augment(i, "Level")
        # ---- chests: buy when affordable (ChestsPerNight per act on the map), free ones as they come, raises, Lost Property
        for i, b in enumerate(crew):
            avail = (st["act"] + 1) * (d.t("ChestsPerNight", 6) + d.t("ChestsPerExtraPlayer", 4) * (players - 1)) / players
            while opened[i] < avail:
                price = min(d.t("ChestBasePrice", 30) * (1 + d.t("ChestPriceK", 1) * opened[i]) ** d.t("ChestPriceExp", 1.4), d.t("ChestPriceCap", 1500))
                if gold[i] < price:
                    break
                gold[i] -= price
                opened[i] += 1
                key = sum(num(d.ups[x]["StatPerLevel"]) * p for x, p in b.items.items() if d.ups[x]["Stat"] == "ChestKey")
                if rng.random() < key / (key + 1):
                    gold[i] += price
                grant_item(i)
                dirty = True
            while chests_free[i] >= 1.0:
                chests_free[i] -= 1.0
                grant_item(i)
                dirty = True
            due_r = SHRINE_RAISES_STAGE * min(1.0, t / (dur * 0.85))
            while raises_done[i] + 1 <= due_r:
                raises_done[i] += 1
                for stat, amount in raise_table(d, b.plan["raise_pref"]).items():
                    b.raise_stats[stat] = b.raise_stats.get(stat, 0.0) + amount
                dirty = True
        if not lp_done and t >= dur * 0.5:
            lp_done = True
            for i in range(players):
                if rng.random() < min(1.0, LOST_PROPERTY_STAGE / players):
                    grant_item(i, chest_luck=False)
            dirty = True
        if not shrine_done and t >= dur * 0.4:
            shrine_done = True
            for i in range(players):
                for _ in range(int(FREE_SHRINE_CHESTS + rng.random() * (FREE_SHRINE_CHESTS % 1.0 > 0))):
                    grant_item(i)
            dirty = True
        # ---- minibosses
        for idx, (at, spec) in enumerate(minis):
            if idx >= minis_done + len(mini_pending) and t >= at * dur:
                parts = spec.split(":")
                hpm = num(parts[1], 5) if len(parts) > 1 else 5
                eid = parts[0]
                e = next((x for x in d.enemies if x["Id"] == eid), None)
                base_hp = (num(e["Health"]) + num(e["HealthPerNight"]) * st["act"]) * st["hp_mult"] if e else st["brute_hp"]
                kill_t = t + MINI_ENGAGE + base_hp * hpm / max(1.0, team["single"] * 0.8)
                mini_pending.append((kill_t, idx))
        for kt, idx in list(mini_pending):
            if t >= kt:
                mini_pending.remove((kt, idx))
                minis_done += 1
                for i in range(players):
                    g = d.t("MinibossGold", 20) * growth * goldm
                    gold[i] += g
                    earned[i] += g
                if idx == 0:
                    for i in range(players):
                        offer_augment(i, "Miniboss1")
                elif idx == 1:
                    for i in range(players):
                        offer_augment(i, "Miniboss2")
                if idx == len(minis) - 1:
                    for i in range(players):
                        grant_item(i)
                dirty = True
        # ---- objectives (one at a time)
        if obj_idx < len(objs):
            typ, count, reward = objs[obj_idx]
            if obj_start is None:
                ready = sched[obj_idx] if obj_idx == 0 else max(sched[obj_idx] * early, obj_end_last + gap)
                if t >= ready:
                    obj_start = t
                    obj_took = objective_seconds(d, m, hard, players, typ, count, team, st, level)
            if obj_start is not None:
                limit = d.t("ObjectiveTime" + typ, 150) or 1e9
                done_at = obj_start + obj_took
                fail = obj_took > limit
                end_at = obj_start + (limit if fail else obj_took)
                if t >= end_at or t >= dur:
                    ok = (not fail) and t < dur + 1e-6 and done_at <= dur
                    obj_log.append((typ, obj_start, min(end_at, dur), ok, obj_took))
                    if ok:
                        if reward == "Gold":
                            for i in range(players):
                                g = d.t("ObjectiveGold", 40) * growth * goldm
                                gold[i] += g
                                earned[i] += g
                        elif reward == "Chest":
                            for i in range(players):
                                grant_item(i)
                        elif reward == "Augment":
                            for i in range(players):
                                offer_augment(i, "Objective")
                        elif reward == "XP":
                            xp += need * d.t("ObjectiveXPLevels", 0.6)
                        if typ == "DestroySpawners":
                            for i in range(players):
                                g = count * d.t("EliteGold", 4) * 2 * growth * goldm * share
                                gold[i] += g
                                earned[i] += g
                    else:
                        defiance += 1
                    dirty = True
                    obj_end_last = min(end_at, dur)
                    obj_idx += 1
                    obj_start = None
        # ---- lantern trail (runs beside the objectives)
        if trail_stage < trail_n:
            solo_q = d.t("SoloQuotaMult", 0.5)
            crew_q = solo_q + (1 - solo_q) * min(1.0, (players - 1) / 3.0)

            def trail_quota(k):
                return max(5.0, d.t("LanternQuota", 40) * (1 + d.t("LanternQuotaGrowth", 0.5) * k) * crew_q * (dur / max(1.0, d.t("LanternQuotaRefSeconds", 600))))
            quota = trail_quota(trail_stage)
            deadline = dur * d.t("LanternDeadlineShare", 0.94) * (trail_stage + 1) / trail_n
            if trail_wait > 0:
                trail_wait -= dt           # walking to the newly lit lantern
            else:
                trail_banked += dt * (st["rate"] * team["kill_share"] * SOULS_PER_KILL_PICKED * max(0.5, st["souls"])
                                      + team.get("reaps", 0.0) * d.t("ReapSoulBonus", 1) * (1 - SOUL_LOSS))
            banked_ok = trail_banked >= quota
            if banked_ok or t >= deadline:
                trail_log.append((trail_stage, t, banked_ok))
                if banked_ok:
                    trail_lit += 1
                    last = trail_stage >= trail_n - 1
                    for i in range(players):
                        g = d.t("LanternGold", 20) * growth * goldm
                        gold[i] += g
                        earned[i] += g
                        if last and d.t("LanternFinalAugment", 1) >= 0.5:
                            offer_augment(i, "Objective")
                        else:
                            grant_item(i)
                    dirty = True
                spare = max(0.0, trail_banked - quota)
                trail_stage += 1
                trail_banked = min(spare, trail_quota(trail_stage) / 2) if banked_ok else trail_banked / 2
                trail_wait = LANTERN_TRAVEL
        if obj_idx >= len(objs) and trail_stage >= trail_n and bell_ready is None:
            bell_ready = t
        if forced_aug and t >= dur * 0.35 and aug_pending[0]:
            for i in range(players):
                if forced_aug in d.aug_by_id:
                    apply_augment(i, d.aug_by_id[forced_aug])
                aug_pending[i] = []
        # ---- bell (rung BELL_WALK after the last objective) or the Final Swarm when the clock runs out
        min_htd = min(min_htd, htd) if t > 30 else min_htd
        t += dt
        if bell_ready is not None and t >= bell_ready + BELL_WALK_SIM:
            break
        if t >= dur:
            # FailRemaining: unfinished objectives fail, the bell rises; the crew walks to it inside the Final Swarm
            while obj_idx < len(objs):
                defiance += 1
                obj_log.append((objs[obj_idx][0], obj_start or dur, dur, False, 0.0))
                obj_idx += 1
                obj_start = None
            overtime = BELL_WALK_SIM
            t = dur + overtime
            break
    t_bell = t
    # ---- the boss (stats at the bell; the horde's ramps hold)
    st = sim_state_at(d, m, hard, players, min(t_bell, dur - 1e-6), boss=True)
    st["avg_hp"] *= catch
    st["brute_hp"] *= catch
    # boss fight: R charges from the summons / thinned horde (BossSpawnRateMult) and reaps on them
    kr_boss = st["rate"] * team["kill_share"] / players
    reap_boss = []
    for b in crew:
        b.level = level
        b.recompute()
        reap_boss.append(reap_numbers(d, b, st["avg_hp"], st["avg_hp"] / max(0.01, b.hit_metric()), kr_boss))
    stage_periods = [getattr(b, "ult_period", None) for b in crew]
    for bi, b in enumerate(crew):
        b.ult_period = ult_period(d, b, reap_boss[bi]["reaps"], sum(r["reaps"] for j, r in enumerate(reap_boss) if j != bi), kr_boss, players)
    boss = next((x for x in d.bosses if x["Id"] == m["Boss"]), d.bosses[-1])
    hp = num(boss["Health"]) * (1 + d.t("BossHealthPerPlayer", 0.75) * (players - 1)) * (1 + 0.25 * defiance) \
        * max(0.1, d.t("EconomyBossHealth3", 1.0)) * mission_mult(m, "BossHealthMult", hard)
    boss_dps = sum(b.single_target_dps(vs_boss=True) for b in crew) * BOSS_UPTIME + sum(r["boss"] for r in reap_boss)
    ttk = hp / max(0.01, boss_dps)
    boss_hit = (num(boss["SlamDamage"]) + 2 * num(boss["RingDamage"])) / 3.0 * (1 + 0.15 * defiance) \
        * mission_mult(m, "DamageMult", hard) ** d.t("BossMissionDamageShare", 0.0)
    ehp = statistics.median(b.ehp() for b in crew)
    boss_htd = ehp / max(1.0, boss_hit)
    end_hits = st["avg_hp"] / max(0.01, statistics.median(b.hit_metric() for b in crew))
    end_med_hits = st["avg_hp"] / max(0.01, statistics.median(statistics.median(b.hit(u, lv, pw) for u, lv, pw in b.all_weapons()) for b in crew))
    htd_bell = statistics.median(b.ehp() / max(0.1, st["hit"]) for b in crew)
    inflow = st["rate"] / d.t("BossSpawnRateMult", 0.35) * (st["avg_hp"] * (1 - st["elite_share"]) + st["elite_share"] * st["brute_hp"])
    clear_bell = sum(b.horde_dps() for b in crew) / max(1e-6, inflow)
    # reap core: the win proxy reads the crowd-limited capacity (geometric mean of one-target and full-crowd clear, as the early-curve
    # model counts it) - with 0.6-8 spawns /s the full-crowd ratio alone says every build is far ahead of the horde
    hp_bell = inflow / max(1e-6, st["rate"] / d.t("BossSpawnRateMult", 0.35))
    clear_one = (sum(b.single_target_dps() for b in crew) + sum(r["horde"] for r in reap_boss)) / max(1e-6, hp_bell) / max(1e-6, st["rate"] / d.t("BossSpawnRateMult", 0.35))
    clear_win = (clear_bell * clear_one) ** 0.5
    target = num(boss["TargetSeconds"])
    # win proxy: offence (clear ratio at the bell, boss TTK vs target) and defence (hits to down, lives)
    lives = 1 + PARDON_DEF_SIM * statistics.mean(pardons) + COOP_REVIVE_DEF * (players - 1)
    off = (min(2.0, clear_win) ** 0.5) * (min(2.0, target / max(1.0, ttk)) ** 0.5)
    dfn = (htd_bell / HTD_REF) * lives
    sk = SKILL_PRACTISED if skill is None else skill
    power = sk * off ** WIN_OFF_W * dfn ** WIN_DEF_W / NEED_SCALE
    p_win = 1.0 / (1.0 + (1.0 / max(1e-6, power)) ** WINPROXY_K)
    for i in range(players):
        g = d.t("BossGold", 30) * (1 + d.t("GoldPerNight", 1.5) * 2) * goldm
        earned[i] += g
    single_team_boss = sum(b.single_target_dps(vs_boss=True) for b in crew)
    ult_boss0 = crew[0].ult_period or 0.0
    for bi, b in enumerate(crew):
        b.ult_period = stage_periods[bi]      # the builds handed back (kit / augment reports) use the late-stage charge pace
    b0 = crew[0]
    return dict(mission=m["Id"], hard=hard, players=players, meta=sum(ranks.values()), t_bell=t_bell, level=level, ttk=ttk, target=target,
                hp=hp, boss_dps=boss_dps, defiance=defiance, early_hits=early_hits or main_hits, end_hits=end_hits, end_med_hits=end_med_hits,
                htd=htd_bell, min_htd=min_htd, clear80=clear80 if clear80 is not None else clear_bell, horde80=horde80, inflow80=inflow80, boss_htd=boss_htd, ehp=ehp, clear=clear_bell, min_clear=min_clear, max_w=max_w, max_ab=max_ab,
                earned=statistics.mean(earned), p_win=p_win, power=power, off=off, dfn=dfn, objs=obj_log, trail=trail_log, trail_lit=trail_lit, curves=curves,
                minutes=(LOBBY_S + t_bell + ttk + ENDSCREEN_S) / 60.0, play=t_bell + ttk, overtime=overtime, crew=crew,
                kills=kills_total / players, elites=elite_kills / players, augs=[list(b.augs) for b in crew], chests=opened[0],
                items=sum(b0.item_stacks.values()), evolved=b0.evolved, reapers=reapers,
                horde_team=sum(b.horde_dps() for b in crew), single_team=single_team_boss,
                reaps=reap_tot / players, reap_xp_share=reap_xp_tot / max(1e-6, xp_tot), reap_gold=reap_gold_tot / players,
                streak_avg=mult_w / max(1e-6, reap_tot), level2_kills=level2_kills or 0.0, catch_end=catch,
                ult_ratio=(statistics.mean(p / max(1.0, c) for _, p, c in ult_log) if ult_log else 0.0),
                ult_boss=ult_boss0, reap_boss_share=sum(r["boss"] for r in reap_boss) / max(1e-6, boss_dps))


def sim_cell(d, m, hard, players, meta_n, seeds=None, **kw):
    seeds = SIM_SEEDS if seeds is None else seeds
    key = ("cell", m["Id"], hard, players, meta_n if isinstance(meta_n, int) else tuple(sorted(meta_n.items())), seeds,
           tuple(sorted(kw.items())) if kw else ())
    cache = d.__dict__.setdefault("_sim_cache", {})
    if key in cache:
        return cache[key]
    runs = [sim_run(d, m, hard, players, meta_n, seed=s, **kw) for s in range(seeds)]
    med = lambda f: statistics.median(r[f] for r in runs)
    out = dict(runs=runs, **{f: med(f) for f in ("t_bell", "level", "ttk", "target", "early_hits", "end_hits", "end_med_hits", "htd",
                                                    "boss_htd", "ehp", "clear", "min_clear", "max_w", "max_ab", "earned", "p_win", "power",
                                                    "minutes", "play", "defiance", "kills", "chests", "items", "horde_team", "single_team", "clear80")})
    out["p_win_mean"] = statistics.mean(r["p_win"] for r in runs)
    cache[key] = out
    return out


def prepare_sim(d):
    mission_rows(d)
    d.aug_by_id = {g["Id"]: g for g in d.augments}
    return d


# ---------------------------------------------------------------- calibration, journey (meta at each mission), pacing on the sim
META_GRID = [0, 4, 8, 14, 20, 28, 36, 46, 56, 68, 84]
TABLE_SEEDS = 3


def sim_calibrate(d):
    """NEED_SCALE: a brand-new player (skill SKILL_NEW) on the first mission with no upgrades wins ~35% (first clear after
    1-3 attempts). Every other win chance in the model is relative to this one judgement."""
    global NEED_SCALE
    NEED_SCALE = 1.0
    d.__dict__.pop("_sim_cache", None)
    m1 = mission_rows(d)[0]
    c = sim_cell(d, m1, False, 1, 0)
    pw_new = c["power"] * SKILL_NEW / SKILL_PRACTISED
    target_p = 0.35
    NEED_SCALE = pw_new * ((1 - target_p) / target_p) ** (1.0 / WINPROXY_K)
    d.__dict__.pop("_sim_cache", None)
    return NEED_SCALE


def sim_table(d):
    """(mission, hard) -> list over META_GRID of (power at practised skill, minutes won, play seconds, earned gold)"""
    if hasattr(d, "_sim_table"):
        return d._sim_table
    total = meta_total_ranks(d)
    grid = sorted(set(min(total, g) for g in META_GRID + [total]))
    tab = {}
    for m in mission_rows(d):
        for hard in (False, True):
            rows = []
            for n in grid:
                c = sim_cell(d, m, hard, 1, n, seeds=TABLE_SEEDS)
                rows.append((n, c["power"], c["minutes"], c["play"], c["earned"]))
            tab[(m["Id"], hard)] = rows
    d._sim_table = tab
    return tab


def _interp(rows, n, idx):
    if n <= rows[0][0]:
        return rows[0][idx]
    for a, b in zip(rows, rows[1:]):
        if n <= b[0]:
            f = (n - a[0]) / max(1e-9, b[0] - a[0])
            return a[idx] + (b[idx] - a[idx]) * f
    return rows[-1][idx]


def sim_p_win(d, m, hard, ranks, runs, players=1):
    rows = sim_table(d)[(m["Id"], hard)]
    pw = _interp(rows, sum(ranks.values()), 1) * skill_at(runs) / SKILL_PRACTISED
    return 1.0 / (1.0 + (1.0 / max(1e-6, pw)) ** WINPROXY_K)


def sim_minutes(d, m, players, hard, won, ranks=None):
    rows = sim_table(d)[(m["Id"], hard)]
    n = sum((ranks or {}).values())
    play = _interp(rows, n, 3)
    play = play if won else LOSS_AT * play
    return (LOBBY_S + play + ENDSCREEN_S) / 60.0, play


def sim_bank_for(d, m, players, hard, won, ranks, first_clear, first_hard, coin_mult=1.0):
    rows = sim_table(d)[(m["Id"], hard)]
    n = sum((ranks or {}).values())
    earned = _interp(rows, n, 4)
    _mins, play = sim_minutes(d, m, players, hard, won, ranks)
    if not won:
        earned *= LOSS_AT ** 1.4          # a loss ends before the richest minutes (act 3, boss purse)
    dur = num(m["Duration"])
    share = d.t("BankShareWin", 0.12) if won else d.t("BankShareLoss", 0.1) * max(0.5, min(1.0, play / dur))
    bank = earned * share * coin_mult
    if won:
        bank += num(m["ClearGold"]) * (num(m.get("HardGoldMult"), 1.0) if hard else 1.0)
        if first_clear:
            bank += num(m["FirstClearGold"])
        if first_hard:
            bank += num(m["HardFirstClearGold"])
    objs = len(mission_objectives(m)) if won else int(len(mission_objectives(m)) * LOSS_AT)
    return bank, earned, objs


def use_sim_for_pacing():
    g = globals()
    g["p_win"], g["mission_minutes"], g["bank_for"] = sim_p_win, sim_minutes, sim_bank_for


def journey(d, seeds=None):
    """Median Death's Office ranks a solo player owns when they first clear each mission (normal / Hard), and pacing stats"""
    use_sim_for_pacing()
    seeds = MISSION_SEEDS if seeds is None else seeds
    sims = [mission_pacing(d, s) for s in range(seeds)]
    missions = mission_rows(d)
    out = {}
    for m in missions:
        for hard in (False, True):
            vals = []
            for log, st in sims:
                ranks_n = 0
                for e in log:
                    if e[1] == "meta":
                        ranks_n += 1
                    if e[1] == "run" and e[2].startswith(m["Id"] + (" HARD" if hard else " ")) and "WIN" in e[2]:
                        vals.append(ranks_n)
                        break
            out[(m["Id"], hard)] = int(statistics.median(vals)) if vals else meta_total_ranks(d)
    d._journey = out
    d._pacing = sims
    return out, sims


# ---------------------------------------------------------------- reports
def _flag(ok):
    return "" if ok else "  <-- OFF"


def report_runs(d):
    missions = mission_rows(d)
    jm = d._journey
    print("\n-- 16. RUN SIMULATION per mission x mode x crew, at the Death's Office ranks a player typically owns when they first beat it")
    print("   (journey medians from section 23; %d seeds per cell; reaper rotates with the seed). Hits = avg normal enemy HP / signature hit"
          " (median-weapon hit in brackets); HTD = hits a reaper can take (EHP / avg enemy hit); Clear = team horde DPS / HP arriving per s"
          % SIM_SEEDS)
    print("  %-15s %-4s %-2s %-4s %-5s %-4s %-9s %-11s %-12s %-6s %-5s %-5s %-13s %-11s %-6s %-5s"
          % ("Mission", "Mode", "P", "Meta", "Bell", "Lvl", "Objs ok", "Hits early", "Hits late", "Clear", "HTD", "BHTD", "Boss TTK/tgt", "MaxNum W/A",
             "RunMin", "Pwin"))
    rows = []
    for m in missions:
        for hard in (False, True):
            meta = jm[(m["Id"], hard)]
            for players in (1, 2, 3, 4):
                c = sim_cell(d, m, hard, players, meta)
                ok = statistics.mean(sum(1 for o in r["objs"] if o[3]) for r in c["runs"])
                nobj = len(mission_objectives(m))
                ratio = c["ttk"] / c["target"]
                rows.append((m["Id"], hard, players, c, ratio))
                print("  %-15s %-4s %-2d %-4d %4.1fm %-4.0f %3.1f/%-5d %4.1f        %4.1f (%4.1f)  %-6.2f %-5.1f %-5.1f %4.0f s %4.2f   %5.0f/%-5.0f %-6.1f %3.0f%%"
                      % (m["Id"], "HARD" if hard else "norm", players, meta, c["t_bell"] / 60, c["level"], ok, nobj, c["early_hits"],
                         c["end_hits"], c["end_med_hits"], c["clear"], c["htd"], c["boss_htd"], c["ttk"], ratio, c["max_w"], c["max_ab"],
                         c["minutes"], 100 * c["p_win"]))
    d._run_rows = rows
    return rows


def report_meta_tiers(d):
    print("\n-- 17. Meta tiers (solo): Death's Office ranks 0 / half / all (greedy cheapest-first order). Boss TTK / target and P(win) for a practised player")
    total = meta_total_ranks(d)
    print("  %-15s %-4s %-26s %-26s %-26s" % ("Mission", "Mode", "0 ranks: TTK  Pwin  Hits", "%d ranks" % (total // 2), "%d ranks (max)" % total))
    for m in mission_rows(d):
        for hard in (False, True):
            parts = []
            for n in (0, total // 2, total):
                c = sim_cell(d, m, hard, 1, n, seeds=3)
                parts.append("%4.2f  %3.0f%%  %4.1f" % (c["ttk"] / c["target"], 100 * c["p_win"], c["end_hits"]))
            print("  %-15s %-4s %-26s %-26s %-26s" % (m["Id"], "HARD" if hard else "norm", parts[0], parts[1], parts[2]))
    # meta power: offence / defence of maxed meta vs none (the "+60% at max" contract)
    m3 = mission_rows(d)[2]
    c0, c1 = sim_cell(d, m3, False, 1, 0, seeds=3), sim_cell(d, m3, False, 1, total, seeds=3)
    off = 0.5 * c1["horde_team"] / c0["horde_team"] + 0.5 * c1["single_team"] / c0["single_team"]
    dfn = c1["ehp"] / c0["ehp"]
    early = sim_cell(d, m3, False, 1, 4, seeds=3)
    off4 = 0.5 * early["horde_team"] / c0["horde_team"] + 0.5 * early["single_team"] / c0["single_team"]
    print("   Maxed meta vs none (%s bell): offence x%.2f, defence (EHP) x%.2f, combined power x%.2f (contract <= ~1.6).  First 4 ranks: offence x%.2f, EHP x%.2f"
          % (m3["Id"], off, dfn, off ** WIN_OFF_W * dfn ** WIN_DEF_W, off4, early["ehp"] / c0["ehp"]))
    d._meta_power = (off, dfn, off ** WIN_OFF_W * dfn ** WIN_DEF_W)


def report_curves(d):
    m = mission_by_id(d, "PlagueCarnival") if any(x["Id"] == "PlagueCarnival" for x in mission_rows(d)) else mission_rows(d)[2]
    print("\n-- 18. Curves through one run (%s, solo, journey meta; seed 0, Grim): every 2 minutes" % m["Id"])
    for hard in (False, True):
        r = sim_run(d, m, hard, 1, d._journey[(m["Id"], hard)], seed=0, record=True, reapers=["Grim"])
        print("  %s  min: lvl / avg enemy HP / hits-to-kill / enemy hit / hits-to-down / clear / items / augments" % ("HARD" if hard else "norm"))
        line = []
        for c in r["curves"]:
            if int(round(c["t"] / 60)) % 2 == 0:
                line.append("  %4.0f: %3d %6.0f %4.1f %5.1f %5.1f %4.2f %2d %d" % (c["t"] / 60, c["level"], c["avg_hp"], c["hits"], c["hit"], c["htd"], c["clear"], c["items"], c["augs"]))
        for chunk in range(0, len(line), 1):
            print("    " + line[chunk])
        print("    objectives: " + ", ".join("%s %s %.0fs" % (o[0], "ok" if o[3] else "FAIL", o[4]) for o in r["objs"]))
        print("    lantern trail: " + (", ".join("#%d %s at %.1f min" % (k + 1, "banked" if ok else "BURNT OUT", at / 60) for k, at, ok in r["trail"]) or "off"))
        print("    boss %.0f s (target %.0f), bell at %.1f min, defiance %d, augments %s" % (r["ttk"], r["target"], r["t_bell"] / 60, r["defiance"], r["augs"][0]))


def report_trail(d):
    print("\n-- 18b. Lantern trail (normal, journey meta, seed 0): when each lantern is banked (min), solo / 4 reapers; bell = objectives AND trail done")
    for m in mission_rows(d):
        cells = []
        for players in (1, 4):
            r = sim_run(d, m, False, players, d._journey[(m["Id"], False)], seed=0, reapers=None)
            cells.append("P%d: %s | bell %.1f" % (players, " ".join(("%.1f" % (at / 60)) + ("" if ok else "x") for k, at, ok in r["trail"]) or "off", r["t_bell"] / 60))
        print("  %-15s %s" % (m["Id"], "    ".join(cells)))


def report_run_chain(d):
    """Section 27 (Nov 25 2026, Docs/RunStructure.md): the 3-stage run. Each stage is simulated fresh for its lantern trail
    (when the last lantern fills = the boss rises) and the souls banked; the boss fight reads a sim of the same map run to
    the cumulative clock (the crew's level, weapons and items carry over between stages, so its power is the run's, not the
    stage's). Souls per player feed the SKINS prices (Cosmetics.csv)."""
    print("\n-- 27. RUN CHAIN (3 stages, boss from the last lantern, carried power; normal, no meta, seed 0)")
    rows = mission_rows(d)[:int(d.t("RunStages", 3))]
    for players in (1, 2, 4):
        cum = 0.0
        souls_total = 0.0
        cells = []
        for m in rows:
            dur = num(m["Duration"])
            r = sim_run(d, m, False, players, 0, seed=0)
            done = max([at for k, at, ok in r["trail"]] or [r["t_bell"]])
            lit = sum(1 for k, at, ok in r["trail"] if ok)
            m_cum = dict(m)
            m_cum["Duration"] = str(cum + dur)
            r2 = sim_run(d, m_cum, False, players, 0, seed=0)
            solo_q = d.t("SoloQuotaMult", 0.5)
            crew_q = solo_q + (1 - solo_q) * min(1.0, (players - 1) / 3.0)
            souls = sum(max(5.0, d.t("LanternQuota", 40) * (1 + d.t("LanternQuotaGrowth", 0.5) * k) * crew_q * (dur / max(1.0, d.t("LanternQuotaRefSeconds", 600))))
                        for k, at, ok in r["trail"] if ok)
            souls_total += souls
            cells.append("%s: boss up %.1f min (%d/%d lit), TTK %.0f s / %.0f" % (m["Id"][:9], done / 60.0, lit, len(r["trail"]), r2["ttk"], r2["target"]))
            cum += done + r2["ttk"] + 30.0   # + the soul rift / passage
        print("  P%d  run %.1f min to the endless horde | souls banked %.0f team (%.0f each)" % (players, cum / 60.0, souls_total, souls_total / players))
        for c in cells:
            print("       " + c)


def report_coop(d):
    print("\n-- 19. Co-op scaling (normal, journey meta): per-person pace = team clear ratio at 60% of the stage vs solo's (1.00 = same pace per person)")
    print("  %-15s %-28s %-28s %-24s" % ("Mission", "Clear per person P2/P3/P4", "Boss TTK/tgt P1/P2/P3/P4", "Objectives time P1/P4"))
    out = []
    for m in mission_rows(d):
        meta = d._journey[(m["Id"], False)]
        cs = [sim_cell(d, m, False, p, meta) for p in (1, 2, 3, 4)]
        pace = lambda c: statistics.mean(r["horde80"] for r in c["runs"]) / statistics.mean(r["inflow80"] for r in c["runs"])
        per = [pace(c) / max(1e-6, pace(cs[0])) for c in cs[1:]]
        ot = [statistics.mean(sum(o[2] - o[1] for o in r["objs"]) for r in c["runs"]) for c in (cs[0], cs[3])]
        out.append((m["Id"], per, [c["ttk"] / c["target"] for c in cs]))
        print("  %-15s %-28s %-28s %4.0f / %4.0f s" % (m["Id"], " / ".join("%.2f" % x for x in per), " / ".join("%.2f" % (c["ttk"] / c["target"]) for c in cs), ot[0], ot[1]))
    d._coop = out


def aug_reference(d):
    m = mission_rows(d)[2]
    return m, d._journey.get((m["Id"], False), 20)


RUN_LEVEL_AUGS = ("WeaponSlot", "TomeSlot", "LevelUps", "ChestNow", "XPBoost", "Rerolls", "KillGold")


def _aug_metric(b):
    b.refresh()
    return dict(h=b.horde_dps(), s=0.5 * b.single_target_dps() + 0.5 * b.single_target_dps(vs_boss=True), e=b.ehp(), gold=0.0)


def aug_value_table(d, seeds=6):
    """Value of each augment per pick, % power. Builds-in-place augments (stats, procs, auras, Pardon) are added to the
    reference builds at the bell (like a tome card: 0.5 x horde DPS gain + 0.5 x single-target gain (elites and boss) +
    0.75 x EHP gain + utility); run-level ones (slots, level-ups, chests, XP, rerolls, gold) are measured as the difference
    between two full runs with the same random streams (forced at 35% of the stage)."""
    m, meta = aug_reference(d)
    generic = shared_weapons(d)
    base_runs = {p: [sim_run(d, m, False, p, meta, seed=s, forced_aug="__none__") for s in range(seeds)] for p in (1, 2)}
    res = {}
    for g in d.augments:
        players = 2 if num(g["CoopOnly"]) > 0.5 else 1
        vals = []
        for s in range(seeds):
            r0 = base_runs[players][s]
            if g["Effect"] in RUN_LEVEL_AUGS:
                r1 = sim_run(d, m, False, players, meta, seed=s, forced_aug=g["Id"])
                v = 100 * (0.5 * (r1["horde_team"] / r0["horde_team"] - 1) + 0.5 * (r1["single_team"] / r0["single_team"] - 1)
                           + EHP_WEIGHT * (r1["ehp"] / r0["ehp"] - 1))
                if g["Effect"] == "Rerolls":
                    v += 1.5 * num(g["Value"])          # judgement: a reroll ~ 1.5% power (the AI rarely needs one)
                vals.append(v)
                continue
            team = [b.clone() for b in r0["crew"]]
            before = [_aug_metric(b) for b in team]
            if g["Effect"] == "Stat":
                fake = dict(Stat=g["Stat"], StatPerLevel=g["Value"], Stat2=g.get("Stat2", ""), Stat2PerLevel=g["Value2"])
                vals.append(pick_value(d, team[0], generic, fake)[3])
                continue
            if g["Effect"] == "AuraStat":
                for j, b in enumerate(team):
                    if j == 0:
                        b.augs.append(g["Id"])
                    else:
                        b.ctx.setdefault("ally_auras", {})[g["Stat"]] = b.ctx.get("ally_auras", {}).get(g["Stat"], 0.0) + num(g["Value"]) * 0.75
            else:
                team[0].augs.append(g["Id"])
            after = [_aug_metric(b) for b in team]
            gain = []
            for b0, b1, b in zip(before, after, team):
                util = aug_effects(b, 0.0)["util"] if b is team[0] else 0.0
                gain.append(100 * (0.5 * (b1["h"] / b0["h"] - 1) + 0.5 * (b1["s"] / b0["s"] - 1) + EHP_WEIGHT * (b1["e"] / b0["e"] - 1)) + util)
            v = gain[0] if g["Effect"] != "AuraStat" else gain[0] + sum(gain[1:])      # an aura counts for the whole crew
            if g["Effect"] == "Pardon":
                v += 100 * EHP_WEIGHT * PARDON_DEF_SIM * num(g["Value"])
            if g["Effect"] == "QuickRevive":
                v += 100 * EHP_WEIGHT * 0.5 * COOP_REVIVE_DEF
            vals.append(v)
        res[g["Id"]] = statistics.mean(vals)
    return res, m, meta


def report_augments(d):
    print("\n-- 20. AUGMENTS: value per pick (marginal run sim, reference %s normal, journey meta; co-op-only ones in a 2-reaper crew)" % aug_reference(d)[0]["Id"])
    vals, m, meta = aug_value_table(d)
    d.aug_value = {k: v for k, v in vals.items()}
    silver = statistics.median(v for k, v in vals.items() if int(num(d.aug_by_id[k]["Tier"])) == 0)
    target = {0: 1.0, 1: 1.5, 2: 2.2}
    print("   Median Silver = %.1f%% power. Tier targets: Silver 1.0x, Gold 1.5x, Prism 2.2x the median Silver; band +-30%%" % silver)
    print("  %-15s %-6s %-12s %-8s %-6s %s" % ("Augment", "Tier", "Effect", "Value%", "xSilv", "Flag"))
    flags = 0
    for g in sorted(d.augments, key=lambda x: (int(num(x["Tier"])), -vals[x["Id"]])):
        tier = int(num(g["Tier"]))
        x = vals[g["Id"]] / max(0.01, silver)
        ok = target[tier] * 0.7 <= x <= target[tier] * 1.3
        flags += 0 if ok else 1
        print("  %-15s %-6s %-12s %6.1f   %4.2f  %s" % (g["Id"], ["Silver", "Gold", "Prism"][tier], g["Effect"], vals[g["Id"]], x,
                                                    "" if ok else ("STRONG" if x > target[tier] * 1.3 else "weak")))
    print("   %d / %d augments outside their tier band" % (flags, len(d.augments)))
    d._aug_flags = flags
    d._aug_silver = silver
    # ---- runaway loops
    print("\n   Runaway checks (late game, level 45, horde HP of the Clocktower end; branching >= 1 = an unbounded chain):")
    m5 = mission_rows(d)[-1]
    st = sim_state_at(d, m5, False, 1, num(m5["Duration"]) - 1)
    b = SimBuild(d, "Grim")
    b.level = 45
    b.raise_stats = {"Damage": 1.0}
    b.recompute()
    b.ctx.update(kr=8.0, avg_hp=st["avg_hp"])
    scale = (1 + d.t("AugmentDamagePerLevel", 0.06) * 44) * b.damage_mult
    kf = lambda dmg: min(1.0, dmg / (0.6 * st["avg_hp"])) * 0.8
    for ids in (["ChainReaction"], ["PowderKeg"], ["ChainReaction", "PowderKeg"]):
        p = sum(num(d.aug_by_id[i]["Value"]) for i in ids)
        n = sum(num(d.aug_by_id[i]["Value"]) * aug_targets(d, num(d.aug_by_id[i]["Value2"])) for i in ids) / p
        dmg = sum(num(d.aug_by_id[i]["Value"]) * num(d.aug_by_id[i]["Value3"]) * scale for i in ids) / p
        br = p * n * kf(dmg)
        dense = p * min(16, n * 2.5) * kf(dmg)      # a pulled / packed crowd (vortex Rs): 2.5x the density
        cap = d.t("AugmentChainMaxDepth", -1)
        print("   %-26s blast %4.0f dmg, %4.1f targets: branching %.2f (packed crowd %.2f) -> blasts per kill %s uncapped, %.2f with depth cap %s"
              % ("+".join(ids), dmg, n, br, dense, "INF" if dense >= 1 else "%.2f" % (p / (1 - dense)),
                 p * chain_generations(dense, cap if cap >= 0 else 2), "%d" % cap if cap >= 0 else "(none)"))
    e = 0.12 * 2 * 0.4 * 0.55
    print("   %-26s bolts per hit %.3f, damage recursion %.3f (converges: each generation is 0.4x the last)" % ("Shrapnel (HitSplit)", 0.12 * 2, e))
    for kr in (4.0, 10.0, 20.0):
        cap = d.t("AugmentRefundPerSecond", -1)
        rate = kr * 0.25
        print("   %-26s %4.0f kills/s: cooldowns tick x%.1f faster uncapped%s" % ("Ruthless Rhythm (refund)", kr, 1 + rate,
                                                                                ", x%.1f with the cap" % (1 + min(rate, cap)) if cap >= 0 else ""))
    print("   %-26s MaxStacks %s, character cap %d  |  %-12s slots clamp +4 (USQAugments::ExtraSlots), MaxStacks 1 each"
          % ("Death's Pardon", d.aug_by_id["PardonSlip"]["MaxStacks"], int(d.t("MaxPardons", 9)), "Holster/Shelf"))


def report_components(d):
    print("\n-- 21. WEAPONS, EVOLUTIONS, TOMES, ITEMS on the sim's reference builds (Grim, %s normal, journey meta; end of act 1 / 2 / bell)"
          % mission_rows(d)[2]["Id"])
    generic = shared_weapons(d)
    m = mission_rows(d)[2]
    meta = d._journey.get((m["Id"], False), 20)
    dur = num(m["Duration"])
    refs, refsets = [], []
    for seed in (1, 2, 3):                     # three Grim runs: a pick's value is averaged over three real builds
        row = []
        for frac in (1 / 3.0, 2 / 3.0, 0.85):
            r = sim_run(d, m, False, 1, meta, seed=seed, reapers=["Grim"], stop_at=frac * dur)
            row.append((r["crew"][0], r["crew"][0].level))
        refsets.append(row)
    refs = refsets[0]
    tomes = [u for u in d.ups.values() if category(u) == "Tome"]
    items = item_pool(d)
    summary = {}

    def avg_value(u, n):
        vs = [pick_value(d, rs[n][0], generic, u) for rs in refsets]
        return tuple(statistics.mean(v[i] for v in vs) for i in range(4))
    for label, group in (("TOMES", tomes), ("ITEMS", items)):
        vals = {u["Id"]: [avg_value(u, n) for n in range(3)] for u in group}
        med = [statistics.median(v[n][3] for v in vals.values()) for n in range(3)]
        print("\n  %s (value of one Common pick, %% power; median %s); contract: none above 1.3x the median of its rarity" % (label, " / ".join("%.1f" % x for x in med)))
        print("  %-17s %-11s %-18s %-18s %-6s %s" % ("Id", "Stat", "Act1 D/E/U", "Act2 D/E/U", "xMed", "Flag"))
        for u in sorted(group, key=lambda x: -vals[x["Id"]][1][3]):
            v = vals[u["Id"]]
            ratio = statistics.mean(v[n][3] / max(1e-6, med[n]) for n in range(3))
            flag = "DOMINANT" if ratio > 1.3 else ("weak" if ratio < GOOD_BAND[0] else "")
            summary[u["Id"]] = (label, ratio)
            print("  %-17s %-11s %4.1f/%4.1f/%4.1f    %4.1f/%4.1f/%4.1f    %-6.2f %s" % (u["Id"], u["Stat"], v[0][0], v[0][1], v[0][2], v[1][0], v[1][1], v[1][2], ratio, flag))
    rb, lv = refs[2]
    rows, evos = weapon_table(d, rb, generic)
    print("\n  Weapons at max level with the late build (level %d); score = role-weighted DPS vs the median (contract 0.85-1.15)" % lv)
    for r in sorted(rows, key=lambda x: (x["role"], -x["score"])):
        flag = "" if 0.85 <= r["score"] <= 1.15 else ("STRONG" if r["score"] > 1.15 else "WEAK")
        print("  %-15s %-10s %-6s %-8.0f %-8.0f %-6.2f %s" % (r["id"], r["beh"], r["role"], r["horde"], r["boss"], r["score"], flag))
    print("\n  Evolutions: spike vs the maxed base weapon (contract +45-65%)")
    for e in sorted(evos, key=lambda x: -x["spike"]):
        flag = "" if 1.45 <= e["spike"] <= 1.65 else ("TOO BIG" if e["spike"] > 1.65 else "TOO SMALL")
        print("  %-19s %-15s %-17s +%-4.0f%% %s" % (e["id"], e["base"], e["tome"], 100 * (e["spike"] - 1), flag))
    # signature weapons + their evolutions
    sig_rows = []
    for k in d.kits.values():
        u = d.ups[k["SignatureWeaponId"]]
        p = 1 + sum(pp * w for pp, w in zip(rarity_odds(d), WEAPON_POWER)) * 8
        h, bo = rb.aoe_dps(u, 8, p), rb.boss_dps(u, 8, p)
        ev = next((x for x in d.ups.values() if x["Kind"] == "Evolution" and x["EvolvesFrom"] == u["Id"]), None)
        role = "area" if u["Behaviour"] in AREA_BEHAVIOURS else "single"
        w = ROLE_WEIGHT[role]
        spike = (w * rb.aoe_dps(ev, 1, p) / h + (1 - w) * rb.boss_dps(ev, 1, p) / bo) if ev else 1.0
        sig_rows.append((k["ReaperId"], u["Id"], u["Behaviour"], h, bo, spike))
    print("\n  Signature evolutions (spike, contract +45-65%): " + ", ".join("%s %+.0f%%%s" % (s[1], 100 * (s[5] - 1), "" if 1.45 <= s[5] <= 1.65 else "!") for s in sig_rows))
    d._components = (summary, rows, evos, sig_rows)
    return summary, rows, evos


KIT_SEEDS = 10


def report_kits(d):
    print("\n-- 22. KITS: clear speed (0.6 horde + 0.4 single-target incl. kit and passive) vs the median reaper, from full run sims")
    print("   (solo, %s normal, journey meta, %d seeds per reaper, at the bell). Contract: within +-12%% of the role target (ROLE_CLEAR)"
          % (mission_rows(d)[2]["Id"], KIT_SEEDS))
    m = mission_rows(d)[2]
    meta = d._journey.get((m["Id"], False), 20)
    rows = []
    def bare(b):
        """the reaper's own power: same build without its augments (augment luck is not the kit's)"""
        c = b.clone()
        c.augs = []
        return c.refresh()
    for k in d.kits.values():
        rid = k["ReaperId"]
        runs = [sim_run(d, m, False, 1, meta, seed=s, reapers=[rid]) for s in range(KIT_SEEDS)]
        bs = [bare(r["crew"][0]) for r in runs]
        h = statistics.mean(b.horde_dps() for b in bs)
        sgl = statistics.mean(b.single_target_dps() for b in bs)
        boss = statistics.mean(b.single_target_dps(vs_boss=True) for b in bs)
        util = statistics.mean(b.kit_util() for b in bs)
        role = next(r["Role"] for r in d.reapers if r["Id"] == rid)
        rows.append(dict(id=rid, role=role, h=h, s=sgl, boss=boss, util=util, ttk=statistics.mean(r["ttk"] / r["target"] for r in runs),
                         max_ab=max(b.max_numbers()[1] for b in bs)))
    mh = statistics.median(r["h"] for r in rows)
    ms = statistics.median(r["s"] for r in rows)
    for r in rows:
        r["clear"] = CLEAR_W_HORDE * r["h"] / mh + (1 - CLEAR_W_HORDE) * r["s"] / ms
    mp = statistics.median(r["clear"] * (1 + r["util"] / 100) for r in rows)
    flags = 0
    print("  %-12s %-12s %-6s %-6s %-6s %-6s %-6s %-6s %-7s %-6s %s" % ("Reaper", "Role", "Target", "Clear", "Horde", "Single", "Util%", "Power", "TTK/tgt", "MaxAb", "Flag"))
    for r in sorted(rows, key=lambda x: -x["clear"]):
        r["power"] = r["clear"] * (1 + r["util"] / 100) / mp
        tgt = ROLE_CLEAR.get(r["role"], 1.0)
        dev = r["clear"] / tgt - 1
        bad = abs(dev) > 0.12 or r["power"] > 1.15 or r["power"] < 0.85
        flags += 1 if bad else 0
        print("  %-12s %-12s %-6.2f %-6.2f %-6.2f %-6.2f %-6.0f %-6.2f %-7.2f %-6.0f %s" % (r["id"], r["role"], tgt, r["clear"], r["h"] / mh, r["s"] / ms, r["util"], r["power"], r["ttk"], r["max_ab"],
                                                                         ("STRONG" if dev > 0.12 or r["power"] > 1.15 else "WEAK") if bad else ""))
    print("  %d / %d reapers outside +-12%% of their role target (or power outside 0.85-1.15)" % (flags, len(rows)))
    d._kits = rows
    return rows


def report_pacing(d):
    sims = d._pacing
    missions = mission_rows(d)
    print("\n-- 23. JOURNEY / META PACING on the run sim (a new solo player, %d seeds; greedy buyer; P(win) from the sim's power, skill %.2f -> %.2f)"
          % (len(sims), SKILL_START, SKILL_MAX))
    med = lambda v: statistics.median(v) if v else 0
    fc_runs, fc_min = [], []
    for log, st in sims:
        n = 0
        for e in log:
            if e[1] == "run":
                n += 1
                if e[2].startswith(missions[0]["Id"] + " ") and "WIN" in e[2]:
                    fc_runs.append(n)
                    fc_min.append(e[0])
                    break
    print("   Map 1 first cleared on attempt %d (median; 10th-90th pct %d-%d) at %.0f min" % (med(fc_runs), sorted(fc_runs)[len(fc_runs) // 10], sorted(fc_runs)[min(len(fc_runs) - 1, 9 * len(fc_runs) // 10)], med(fc_min)))
    print("  %5s %5s %8s %6s %10s %6s %7s" % ("Hours", "Runs", "Cleared", "Hard", "MetaRanks", "Hires", "Quests"))
    hour1 = None
    for h in MISSION_CHECKPOINTS_H:
        rows_h = []
        for log, st in sims:
            ev = [e for e in log if e[0] <= h * 60]
            rows_h.append((sum(1 for e in ev if e[1] == "run"),
                           len({e[2].split()[0] for e in ev if e[1] == "run" and "WIN" in e[2] and "HARD" not in e[2]}),
                           len({e[2].split()[0] for e in ev if e[1] == "run" and "WIN" in e[2] and "HARD" in e[2]}),
                           sum(1 for e in ev if e[1] == "meta"),
                           sum(1 for e in ev if e[1] == "hire") + sum(1 for e in ev if e[1] == "quest" and "reaper" in e[3]),
                           sum(1 for e in ev if e[1] == "quest")))
        r = [med([x[i] for x in rows_h]) for i in range(6)]
        if h == 1:
            hour1 = r
        print("  %5d %5.0f %8.0f %6.0f %6.0f/%-3d %6.0f %7.0f" % (h, r[0], r[1], r[2], r[3], meta_total_ranks(d), r[4], r[5]))
    ends = []
    for log, st in sims:
        t_maps = next((e[0] / 60 for e in log if e[1] == "run" and "WIN" in e[2] and "HARD" not in e[2] and e[2].startswith(missions[-1]["Id"])), None)
        seen, t_hard = set(), None
        for e in log:
            if e[1] == "run" and "WIN" in e[2] and "HARD" in e[2]:
                seen.add(e[2].split()[0])
                if len(seen) == len(missions):
                    t_hard = e[0] / 60
                    break
        t_all = st["clock"] / 60 if (len(st["hard"]) == len(missions) and sum(st["ranks"].values()) >= st["max_meta"] and len(st["hired"]) == len(d.reapers)) else None
        ends.append((t_maps, t_hard, t_all))
    pk = lambda i: med([x[i] for x in ends if x[i] is not None])
    print("   All maps: %.1f h | all Hard: %.1f h | everything: %.1f h" % (pk(0), pk(1), pk(2)))
    print("   Death's Office ranks owned at each first clear (journey): " + ", ".join("%s %d/%d" % (m["Id"][:9], d._journey[(m["Id"], False)], d._journey[(m["Id"], True)]) for m in missions))
    log0 = sims[0][0]
    print("   First 75 minutes, seed 0:")
    for e in log0:
        if e[0] > 75:
            break
        if e[1] != "quest":
            print("     %5.1f min  %-5s %-30s %s" % (e[0], e[1], e[2], e[3]))
    gaps = [0.0] + [e[0] for e in log0 if e[1] in ("quest", "hire", "meta") or (e[1] == "run" and "WIN" in e[2])] + [600.0]
    gaps = [b - a for a, b in zip(gaps, gaps[1:]) if b <= 600.0]
    print("   Longest gap between rewards in the first 10 h (seed 0): %.0f min" % max(gaps))
    d._pace = dict(first_clear=med(fc_runs), first_min=med(fc_min), hour1=hour1, maps=pk(0), hard=pk(1), all=pk(2), gap=max(gaps))



def report_events(d):
    """Content item 12 (Nov 24 2026): what the random events add to a run. Reads Events.csv + Missions.csv; no simulation."""
    try:
        events = read_csv("Events.csv")
        missions = read_csv("Missions.csv")
    except Exception as err:  # the file is optional in the game too
        print("\n-- 28. RUN EVENTS: Events.csv not readable (%s)" % err)
        return
    num = lambda row, key, default=1.0: float(row.get(key) or default)
    per_run = int(d.t("EventsPerRun", 3))
    print("\n-- 28. RUN EVENTS (Events.csv; %d per mission, +%d per haunt loop; shares assume every event rolls by weight)" % (per_run, int(d.t("EventLoopExtra", 1))))
    print("  map             pool  event min/run  horde  XP     souls  gold   (run-average multipliers from Ambient / Toll events)")
    for m in missions:
        biome = m.get("Biome") or m.get("Id")
        pool = [e for e in events if e.get("Kind") != "Condition" and num(e, "Weight") > 0 and (not e.get("Biome") or e.get("Biome") == biome)]
        total_w = sum(num(e, "Weight") for e in pool) or 1.0
        dur = float(m.get("Duration") or 600)
        active, up = 0.0, {"SpawnMult": 0.0, "XPMult": 0.0, "SoulMult": 0.0, "GoldMult": 0.0}
        for e in pool:
            share = num(e, "Weight") / total_w * per_run
            secs = num(e, "Seconds", 45)
            if e.get("Kind") == "Site":
                secs *= 0.5  # assumption: a site is answered about half way through its timer
            active += share * secs
            for key in up:
                up[key] += share * secs * (num(e, key) - 1.0)
        print("  %-15s %4d  %8.1f       x%.3f x%.3f x%.3f x%.3f" % (m.get("Id"), len(pool), active / 60.0,
              1 + up["SpawnMult"] / dur, 1 + up["XPMult"] / dur, 1 + up["SoulMult"] / dur, 1 + up["GoldMult"] / dur))
    conds = [e for e in events if e.get("Kind") == "Condition"]
    print("  District conditions: %d in the file, %d rolled per run (drops inside that district only)." % (len(conds), int(d.t("DistrictConditions", 2))))

def report_unlock_audit(d):
    """Content pass (Nov 24 2026, Docs/Content.md item 11): is there something new often enough? Uses the journey sims."""
    sims = d._pacing
    med = lambda v: statistics.median(v) if v else 0
    kinds = ("quest", "hire", "meta")
    any_kinds = kinds + ("ledger",)
    print("\n-- 27. UNLOCK PACING AUDIT (targets: a reward after every one of the first 10 runs; no gap over 20 min in the first 10 h)")
    dry_runs, worst_gaps, over20, where = [], [], [], {}
    for log, st in sims:
        run_ends = [e[0] for e in log if e[1] == "run"][:10]
        dry = 0
        prev = 0.0
        for t in run_ends:
            got = any(e[1] in kinds and prev < e[0] <= t + 1e-6 for e in log) or any(e[1] == "run" and "WIN" in e[2] and abs(e[0] - t) < 1e-6 and "first" in e[3] for e in log)
            dry += 0 if got else 1
            prev = t
        dry_runs.append(dry)
        times = [0.0] + sorted(e[0] for e in log if e[1] in kinds and e[0] <= 600.0) + [600.0]
        gaps = [(b - a, a) for a, b in zip(times, times[1:])]
        worst_gaps.append(max(g[0] for g in gaps))
        over20.append(sum(1 for g in gaps if g[0] > 20.0))
        for g, a in gaps:
            if g > 20.0:
                where[int(a // 60)] = where.get(int(a // 60), 0) + 1
    print("   First 10 runs with nothing new (quest / hire / upgrade): median %d, worst seed %d" % (med(dry_runs), max(dry_runs)))
    any_gap = []
    for log, st in sims:
        times = [0.0] + sorted(e[0] for e in log if e[1] in any_kinds and e[0] <= 600.0) + [600.0]
        any_gap.append(max(b - a for a, b in zip(times, times[1:])))
    print("   Counting Death's Ledger ranks as a reward: longest gap in the first 10 h median %.0f min, worst seed %.0f min" % (med(any_gap), max(any_gap)))
    print("   Longest gap in the first 10 h: median %.0f min, worst seed %.0f min; gaps over 20 min per seed: median %d" % (med(worst_gaps), max(worst_gaps), med(over20)))
    print("   Hour in which the over-20-min gaps start (all seeds): " + (", ".join("h%d x%d" % (h + 1, n) for h, n in sorted(where.items())) or "none"))
    quests = [q for q in d.quests if q.get("Id")]
    done_10h = med([sum(1 for e in log if e[1] == "quest" and e[0] <= 600.0) for log, st in sims])
    print("   Quests: %d in the file, %.0f done by hour 10 (median). Meta ranks %d, reapers to hire %d." % (len(quests), done_10h, meta_total_ranks(d), sum(1 for r in d.reapers if num(r["UnlockCost"]) > 0)))
    d._audit = dict(dry=med(dry_runs), dry_worst=max(dry_runs), gap=med(worst_gaps), gap_worst=max(worst_gaps), over20=med(over20))


SENS_PARAMS = [
    ("Tuning", "SpawnRatePeak"), ("Tuning", "XPGrowth"), ("Tuning", "EnemyHealthPerMinute"), ("Tuning", "StageHpRefRatio"),
    ("Tuning", "EconomyBossHealth3"), ("Tuning", "AbilityDamagePerLevel"), ("Tuning", "EnemyDamagePerNight"), ("Tuning", "RarityRare"),
    ("Tuning", "ChestPriceExp"), ("Tuning", "GoldDropChance"), ("Enemies", "Health*"), ("Upgrades", "Signature Damage*"),
    ("Kits", "E/R Damage*"), ("Missions", "HealthMult*"), ("Meta", "Damage PerRank"), ("Assume", "BOSS_UPTIME"), ("Assume", "SIM_KILL_SHARE"),
    ("Assume", "DENSITY"), ("Assume", "XP_PICKUP"), ("Assume", "SHRINE_RAISES_STAGE"),
]


def _perturb(d, kind, key, f):
    """Scale one number (or a family) by f in memory. Returns an undo function."""
    g = globals()
    if kind == "Tuning":
        old = d.tune.get(key, 0.0)
        d.tune[key] = old * f
        return lambda: d.tune.__setitem__(key, old)
    if kind == "Assume":
        old = g[key]
        g[key] = old * f
        return lambda: g.__setitem__(key, old)
    saved = []
    if kind == "Enemies":
        for e in d.enemies:
            saved.append((e, "Health", e["Health"]))
            saved.append((e, "HealthPerNight", e["HealthPerNight"]))
            e["Health"] = str(num(e["Health"]) * f)
            e["HealthPerNight"] = str(num(e["HealthPerNight"]) * f)
    elif kind == "Upgrades":
        for u in d.ups.values():
            if is_signature(u) and u["Kind"] in ("Weapon", "Evolution"):
                for c in ("Damage", "DamagePerLevel"):
                    saved.append((u, c, u[c]))
                    u[c] = str(num(u[c]) * f)
    elif kind == "Kits":
        for k in d.kits.values():
            for c in ("E_Damage", "E_FinalDamage", "R_Damage", "R_FinalDamage"):
                saved.append((k, c, k[c]))
                k[c] = str(num(k[c]) * f)
    elif kind == "Missions":
        for m in mission_rows(d):
            saved.append((m, "HealthMult", m["HealthMult"]))
            m["HealthMult"] = str(num(m["HealthMult"]) * f)
    elif kind == "Meta":
        for row in d.meta:
            if row["Id"] == "Damage":
                saved.append((row, "PerRank", row["PerRank"]))
                row["PerRank"] = str(num(row["PerRank"]) * f)

    def undo():
        for obj, c, v in saved:
            obj[c] = v
    return undo


def _sens_outputs(d):
    d.__dict__.pop("_sim_cache", None)
    missions = mission_rows(d)
    cells = [(missions[0], False), (missions[-1], False), (missions[2], True)]
    ttk, hits, pw, htd = [], [], [], []
    for m, hard in cells:
        c = sim_cell(d, m, hard, 1, d._journey[(m["Id"], hard)], seeds=2)
        ttk.append(c["ttk"] / c["target"])
        hits.append(c["end_hits"])
        pw.append(c["power"])
        htd.append(c["htd"])
    return dict(ttk=statistics.mean(ttk), hits=statistics.mean(hits), power=statistics.mean(pw), htd=statistics.mean(htd))


def report_sensitivity(d):
    print("\n-- 24. SENSITIVITY: +10% on one number at a time; elasticity = % change of the output / 10% (2 seeds; missions 1 + 5 normal, 3 Hard)")
    base = _sens_outputs(d)
    rows = []
    for kind, key in SENS_PARAMS:
        k2 = key.split()[0].rstrip("*")
        undo = _perturb(d, kind, k2 if kind != "Upgrades" else key, 1.1)
        try:
            out = _sens_outputs(d)
        finally:
            undo()
        el = {o: (out[o] / base[o] - 1) / 0.1 for o in base}
        rows.append((kind, key, el, max(abs(v) for v in el.values())))
    d.__dict__.pop("_sim_cache", None)
    rows.sort(key=lambda r: -r[3])
    print("  %-9s %-22s %-10s %-10s %-10s %-10s" % ("Source", "Number", "BossTTK", "HitsLate", "Power", "HitsToDown"))
    for kind, key, el, mx in rows:
        print("  %-9s %-22s %+-10.2f %+-10.2f %+-10.2f %+-10.2f" % (kind, key, el["ttk"], el["hits"], el["power"], el["htd"]))
    d._sens = rows


def report_reap(d):
    """Section 26: the reap system + early curve (Nov 9 reap core, Docs/Balance.md section 12)"""
    print("\n-- 26. REAP SYSTEM + EARLY CURVE (Docs/Balance.md section 12): first minutes of PlagueCarnival solo (Grim, journey meta, seed 0)")
    print("   Kill capacity = geometric mean of one-target and full-crowd kills/s (assumption); target spawn ~75%% of it. XP to level 2 = %d"
          % xp_to_next(d, 1, 1))
    m = mission_by_id(d, "PlagueCarnival") if any(x["Id"] == "PlagueCarnival" for x in mission_rows(d)) else mission_rows(d)[2]
    r = sim_run(d, m, False, 1, d._journey[(m["Id"], False)], seed=0, record=True, reapers=["Grim"])
    print("  min  spawn/s  capacity/s  spawn/cap  on-screen cap  level  avg HP  hits  catch-up  reaps/s  streak")
    for c in r["curves"]:
        mn = c["t"] / 60
        if mn <= 6 or int(round(mn)) % 3 == 0:
            print("  %3.0f  %6.2f  %9.2f  %9.2f  %12.0f  %5d  %6.0f  %4.1f  %8.2f  %7.2f  %6.2f" % (mn, c["rate"], c["cap_kps"], c["rate"] / max(1e-6, c["cap_kps"]),
                  c["onscreen"], c["level"], c["avg_hp"], c["hits"], c["catch"], c["reaps"], c["streak"]))
    print("  Per mission (normal, 1 / 4 players, journey meta): reaps per reaper per run, reap share of XP, average streak, R charge time / old R cooldown"
          " (stage), boss-fight R charge s, scythe share of boss DPS, kills to level 2")
    for mm in mission_rows(d):
        for p in (1, 4):
            c = sim_cell(d, mm, False, p, d._journey[(mm["Id"], False)])
            mean = lambda f: statistics.mean(x[f] for x in c["runs"])
            print("  %-15s P%d  reaps %4.0f  xp %3.0f%%  streak x%.2f  R pace %.2f  boss R %3.0f s  scythe %2.0f%% boss  L2 kills %4.1f  catch-up end %.2f"
                  % (mm["Id"], p, mean("reaps"), 100 * mean("reap_xp_share"), mean("streak_avg"), mean("ult_ratio"), mean("ult_boss"),
                     100 * mean("reap_boss_share"), mean("level2_kills"), mean("catch_end")))


def report_verdict(d):
    print("\n-- 25. VERDICT vs the contract (Docs/Balance.md section 11)")
    rows = getattr(d, "_run_rows", None)
    if not rows:
        rows = [(m["Id"], h, 1, sim_cell(d, m, h, 1, d._journey[(m["Id"], h)]), 0) for m in mission_rows(d) for h in (False, True)]
        rows = [(a, h, p, c, c["ttk"] / c["target"]) for (a, h, p, c, _r) in rows]
    early = [c["early_hits"] for (_m, h, p, c, _r) in rows if not h]
    late_n = [c["end_hits"] for (_m, h, p, c, _r) in rows if not h]
    late_h = [c["end_hits"] for (_m, h, p, c, _r) in rows if h]
    ttk = [r for (_m, h, p, c, r) in rows]
    print("   Hits-to-kill early (normal, all crews): %.1f-%.1f (1-3)%s" % (min(early), max(early), _flag(min(early) >= 0.8 and max(early) <= 3.2)))
    print("   Hits-to-kill late normal: %.1f-%.1f (2-5)%s   Hard: %.1f-%.1f (3-6)%s" % (min(late_n), max(late_n), _flag(min(late_n) >= 1.8 and max(late_n) <= 5.2),
                                                                                  min(late_h), max(late_h), _flag(min(late_h) >= 2.8 and max(late_h) <= 6.2)))
    print("   Boss TTK / target, every mission x mode x crew: %.2f-%.2f (0.85-1.15)%s" % (min(ttk), max(ttk), _flag(min(ttk) >= 0.85 and max(ttk) <= 1.15)))
    mw = max(c["max_w"] for (_m, h, p, c, _r) in rows)
    ma = max(c["max_ab"] for (_m, h, p, c, _r) in rows)
    print("   Biggest numbers: weapon crit %.0f, ability crit %.0f (tens to low thousands)%s" % (mw, ma, _flag(ma <= 4000 and mw <= 3000)))
    if hasattr(d, "_coop"):
        per = [x for (_i, p, _t) in d._coop for x in p]
        print("   Co-op clear per person vs solo: %.2f-%.2f (about 1.0)%s" % (min(per), max(per), _flag(min(per) >= 0.85 and max(per) <= 1.2)))
    if hasattr(d, "_meta_power"):
        print("   Maxed meta power x%.2f (<= ~1.6)%s" % (d._meta_power[2], _flag(d._meta_power[2] <= 1.7)))
    if hasattr(d, "_kits"):
        bad = [r["id"] for r in d._kits if abs(r["clear"] / ROLE_CLEAR.get(r["role"], 1) - 1) > 0.12 or not (0.85 <= r["power"] <= 1.15)]
        print("   Kits outside +-12%% / power 0.85-1.15: %s%s" % (", ".join(bad) or "none", _flag(not bad)))
    if hasattr(d, "_components"):
        summary, wrows, evos, sig = d._components
        wbad = [r["id"] for r in wrows if not 0.85 <= r["score"] <= 1.15]
        ebad = [e["id"] for e in evos if not 1.45 <= e["spike"] <= 1.65] + [s[1] for s in sig if not 1.45 <= s[5] <= 1.65]
        tbad = [k for k, (lab, ratio) in summary.items() if ratio > 1.3]
        print("   Weapons outside +-15%%: %s%s" % (", ".join(wbad) or "none", _flag(not wbad)))
        print("   Evolutions outside +45-65%%: %s%s" % (", ".join(ebad) or "none", _flag(not ebad)))
        print("   Tomes / items above 1.3x median: %s%s" % (", ".join(tbad) or "none", _flag(not tbad)))
    if hasattr(d, "_aug_flags"):
        print("   Augments outside their tier band: %d%s" % (d._aug_flags, _flag(d._aug_flags <= 3)))
    if hasattr(d, "_pace"):
        p = d._pace
        print("   First clear: attempt %d at %.0f min (1-3 attempts, 30-60 min)%s" % (p["first_clear"], p["first_min"], _flag(1 <= p["first_clear"] <= 3 and p["first_min"] <= 65)))
        h1 = p["hour1"]
        print("   Hour 1: %d upgrades, %d reapers (2-4 and 1-2)%s" % (h1[3], h1[4], _flag(2 <= h1[3] <= 4 and 1 <= h1[4] <= 2)))
        print("   All Hard modes %.0f h, everything %.0f h (30-50 h)%s" % (p["hard"], p["all"], _flag(25 <= p["hard"] <= 50 and p["all"] <= 55)))


def unified_report(d, quick=False):
    prepare_sim(d)
    print("=" * 110)
    print("SOUL QUOTA - UNIFIED RUN SIMULATION (remake balance pass, Nov 9 2026). Release lengths (Missions.csv Duration; Tuning NightSeconds"
          " is the test knob and is ignored here).")
    ns = sim_calibrate(d)
    print("   Win-proxy calibration: NEED_SCALE %.3f (a new player's first Graveyard attempt with no upgrades wins ~35%%)" % ns)
    journey(d, seeds=12 if quick else MISSION_SEEDS)
    report_runs(d)
    report_meta_tiers(d)
    report_curves(d)
    report_trail(d)
    report_run_chain(d)
    report_coop(d)
    report_augments(d)
    report_components(d)
    report_kits(d)
    report_pacing(d)
    report_unlock_audit(d)
    report_events(d)
    report_reap(d)
    if not quick:
        report_sensitivity(d)
    report_verdict(d)


if __name__ == "__main__":
    main()
