// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SQTypes.h"
#include "SQData.generated.h"

class UStaticMesh;
class UMaterialInstanceDynamic;
class USoundBase;
class USQData;

/**
 * Upgrades.csv Category column (items/tomes pass, Nov 2026; Docs/ItemsAndTomes.md). A missing cell falls back to Kind:
 * Weapon -> Weapon, Relic -> Tome, Evolution -> Evolution.
 *   Weapon    - level-up cards, Tuning WeaponSlots slots, levels 1..MaxLevel
 *   Tome      - level-up cards, Tuning TomeSlots slots, a fixed stat gain per level x the card's rarity multiplier
 *   Item      - chests / Lost Property / altars only, no slots, stacks (Level = stacks held, MaxStacks 0 = no limit)
 *   Evolution - a maxed weapon + its paired tome (RequiresRelic); replaces the weapon in its slot
 *   Fallback  - SQ_Heal / SQ_Gold / SQ_XP filler cards once a reaper has nothing left to take (never pooled)
 * Tomes, items and fallbacks all use Kind = Relic, so every "Kind == Relic" stat loop keeps working.
 */
enum class ESQUpgradeCategory : uint8 { Weapon, Tome, Item, Evolution, Fallback };

struct FSQUpgradeDef
{
	FName Id;
	FString Name;
	ESQUpgradeKind Kind = ESQUpgradeKind::Weapon;
	ESQUpgradeCategory Category = ESQUpgradeCategory::Weapon;
	/** Items only: most stacks one reaper can hold (0 = no limit). Upgrades.csv MaxStacks. */
	int32 MaxStacks = 0;
	ESQWeaponBehaviour Behaviour = ESQWeaponBehaviour::None;
	int32 MaxLevel = 1;
	float Damage = 0.f, DamagePerLevel = 0.f;
	float Cooldown = 1.f, CooldownPerLevel = 0.f;
	float Radius = 0.f, RadiusPerLevel = 0.f;
	float Count = 1.f, CountPerLevel = 0.f;
	float Speed = 0.f;
	float Slow = 0.f;
	FName Stat;
	float StatPerLevel = 0.f;
	/** Optional second stat (relics): applied exactly like Stat */
	FName Stat2;
	float Stat2PerLevel = 0.f;
	FName EvolvesFrom;
	FName RequiresRelic;
	float Weight = 10.f;
	FLinearColor Color = FLinearColor::White;
	FString Description;
	/** 1-2 ASCII characters the HUD draws in the card icon (e.g. "SC" for Scythe Sweep) */
	FString IconGlyph;
	/** Category shown on cards: Melee, Ranged, Area, Summon, Offence, Defence, Economy, Mobility, Co-op */
	FName Tag;
	/** Attack presentation (Upgrades.csv, all optional): AttackStyle = Slash/Chop/Spin/Stab/Cast/Raise/Throw/Shoot/Slam/Summon,
	 *  AttackAnim = '|'-separated clips tried first, FxStyle = effect look (Sweep, Sickle, Bolt, Bells...; see SQCharacter.cpp) */
	FName AttackStyle;
	TArray<FName> AttackAnims;
	FName FxStyle;

	/** Kits pass (Upgrades.csv Signature 1): a reaper's own weapon / evolution. Only SignatureOwner is ever offered it; it does
	 *  not use one of the Tuning WeaponSlots shared slots. SignatureOwner is filled from Kits.csv by the loader. */
	bool bSignature = false;
	FName SignatureOwner;

	bool IsSignature() const { return bSignature; }
	bool IsTome() const { return Category == ESQUpgradeCategory::Tome; }
	bool IsItem() const { return Category == ESQUpgradeCategory::Item; }
	bool IsFallback() const { return Category == ESQUpgradeCategory::Fallback; }
	/** Highest Level an owner can reach: MaxLevel, or MaxStacks for items (no limit = 999) */
	int32 LevelCap() const { return Category == ESQUpgradeCategory::Item ? (MaxStacks > 0 ? MaxStacks : 999) : MaxLevel; }
};

/**
 * Procedural walk personality layered on top of the locomotion clips (Reapers.csv Gait* columns).
 * Every default is neutral: 0 = no effect, except ArmSwing and Stride (1 = the clip as authored).
 */
struct FSQGaitProfile
{
	float Bounce = 0.f;   // cm of extra hop per step (hips rise between footfalls)
	float Sway = 0.f;     // degrees of side-to-side hip roll per stride
	float Hunch = 0.f;    // degrees of forward spine + head bend (negative = chest out, proud)
	float Lean = 0.f;     // degrees of extra forward lean, faded in with running
	float ArmSwing = 1.f; // upper-arm swing scale: 0 = arms still, 1 = clip, 2 = exaggerated
	float Stride = 1.f;   // walk/run cycle rate at the same speed: >1 = short quick steps, <1 = long strides
	float HeadBob = 0.f;  // degrees of head nod per step
	float Limp = 0.f;     // 0..1 asymmetry: one leg's step is slower and the hip drops on that side
	float Hover = 0.f;    // cm the whole body floats off the ground (ghosts), with a gentle bob
};

/**
 * Character pass (Nov 25): per-reaper body proportions on the shared rig (Reapers.csv Body* columns), applied by
 * USQAnimInstance as uniform bone scales after the pose is built. Values are the EFFECTIVE size of each part
 * (1 = as modelled): the anim instance divides out the parent scales, and lifts the root so longer / shorter legs
 * still stand on the ground. Uniform scales only, so the result is the same whatever the bone axes are after import.
 */
struct FSQBodyProfile
{
	float Legs = 1.f;   // upper leg chain (leg length and foot size)
	float Torso = 1.f;  // spine: everything above the hips
	float Chest = 1.f;  // chest on top of Torso (shoulder width; arms and head are corrected back)
	float Head = 1.f;   // final head size
	float Arms = 1.f;   // final arm size (length and girth)
	float Hands = 1.f;  // hands on top of Arms
	bool IsNeutral() const
	{
		return FMath::IsNearlyEqual(Legs, 1.f) && FMath::IsNearlyEqual(Torso, 1.f) && FMath::IsNearlyEqual(Chest, 1.f)
			&& FMath::IsNearlyEqual(Head, 1.f) && FMath::IsNearlyEqual(Arms, 1.f) && FMath::IsNearlyEqual(Hands, 1.f);
	}
	/** World size multiplier of something held in the hand (weapons attached to handslot inherit the bone scale) */
	float HandWorldScale() const { return Arms * Hands; }
};

/**
 * Character pass (Nov 25): every reaper's movement / stance trait (Reapers.csv Trait* columns). When Cond holds, Effect
 * applies at Value (eased in and out). Cond: Always, AfterDash, AfterCast, Moving, Still, Carrying, LowHealth, NearAlly,
 * Alone, Crowd, AllyDowned, NearLantern. Effect: Speed (+Value move speed), Fortify (-Value damage taken), Damage
 * (+Value damage), Phase (walks through the horde), Shove (pushes the horde aside while moving).
 * Time: seconds the condition must hold first (Moving / Still), or how long it lasts after the event (AfterDash / AfterCast),
 * or the enemy count for Crowd. Evaluated on every machine from replicated state, so the owner predicts its speed.
 */
struct FSQTraitDef
{
	FString Name;
	FName Cond;
	FName Effect;
	float Value = 0.f;
	float Time = 0.f;
	FString Text;
	bool IsSet() const { return !Cond.IsNone() && !Effect.IsNone(); }
};

struct FSQReaperDef
{
	FName Id;
	FString Name;
	float MaxHealth = 100.f;
	float MoveSpeed = 650.f;
	int32 CarryCap = 50;
	float DashCooldown = 2.5f;
	float Magnet = 300.f;
	/** Kits pass: the kit's signature weapon (Kits.csv SignatureWeaponId overrides Reapers.csv StartWeapon) */
	FName StartWeapon;
	int32 UnlockCost = 0;
	/** Kits pass: copied from Kits.csv (PassiveId / PassiveDesc). Reapers.csv Passive* columns are no longer read. */
	FName Passive;
	FString PassiveText;
	/** Co-op role shown in the reaper select (Healer, Hauler, Banker, Defender...) */
	FName Role;
	/** Short text for a future quest unlock, e.g. "Revive 25 teammates" */
	FString UnlockHint;
	FLinearColor Color = FLinearColor::White;
	FName Model;   // imported character (Art/Characters/<Model>)
	FName Weapon;  // imported hand-held mesh (Art/Weapons/SM_<Weapon>); primitive stand-in when the art is missing
	float WeaponSize = 0.f; // held weapon length in cm (Reapers.csv WeaponSize; 0 = by weapon type)
	FName Accessory; // fun prop on the reaper (Art/Props/SM_<Accessory>)
	FName AccessorySlot; // Hand (default) or Head
	float AccessorySize = 50.f; // prop size in cm
	float ArtScale = 1.f;       // model size multiplier on top of Tuning ReaperArtHeight
	/** Locomotion style clips (A_<name>), tried in order: CSV cells are '|'-separated fallback lists
	 *  ("SQ_Walk_Swagger|Walking_C"). Empty = the model's default idle / walk / run. */
	TArray<FName> IdleAnims, WalkAnims, RunAnims;
	FSQGaitProfile Gait;
	FSQBodyProfile Body;
	/** Collision radius in cm (Reapers.csv HitRadius, default 40): small reapers are harder to hit, giants easier */
	float HitRadius = 40.f;
	FSQTraitDef Trait;
	/** Character select copy: one-line bio, a play tip, a difficulty 1..3 and a playstyle tag */
	FString Bio;
	FString Tip;
	int32 Difficulty = 1;
	FString Playstyle;
	/** Text barks (Reapers.csv Bark<Event>, '|'-separated variants): Level, LowHealth, Revive, Combo, Boss, Victory, Pickup */
	TMap<FName, TArray<FString>> Barks;
};

/** Ability shapes (Kits.csv <Slot>_Shape; Docs/Kits.md). Keep new values at the end. */
enum class ESQAbilityShape : uint8 { None, Circle, Nova, Cone, Line, Dash, Blink, Zone, Volley, Barrage, Chain, Self, Totem, Global };

/** Ability effect bits (Kits.csv <Slot>_Effect / EvoEffect: "Slow|Knockback"). Magnitudes: EffectValue (or EvoValue). */
namespace SQAbilityFx
{
	constexpr uint32 Slow = 1u << 0;       // EffectTime seconds
	constexpr uint32 Freeze = 1u << 1;     // EffectTime seconds (enemies stop moving and attacking)
	constexpr uint32 Knockback = 1u << 2;  // Tuning AbilityKnockback
	constexpr uint32 Pull = 1u << 3;       // Tuning AbilityPullStrength (toward the centre)
	constexpr uint32 Mark = 1u << 4;       // toughest enemy: +EffectValue damage taken for EffectTime
	constexpr uint32 Heal = 1u << 5;       // reapers in the area: +EffectValue x max HP
	constexpr uint32 Shield = 1u << 6;     // reapers in the area: shield EffectValue x max HP for the buff time
	constexpr uint32 Haste = 1u << 7;      // reapers: +EffectValue move speed for the buff time
	constexpr uint32 Empower = 1u << 8;    // reapers: +EffectValue damage for the buff time
	constexpr uint32 Fortify = 1u << 9;    // reapers: -EffectValue damage taken for the buff time
	constexpr uint32 Revive = 1u << 10;    // downed reapers in the area stand up at EffectValue health
	constexpr uint32 Lifesteal = 1u << 11; // caster heals EffectValue x max HP per enemy hit (max 25% a cast)
}

/** One E or R ability of a kit (Kits.csv E_* / R_* columns) */
struct FSQAbilityDef
{
	FString Name;
	FString Glyph;
	ESQAbilityShape Shape = ESQAbilityShape::None;
	uint32 Effects = 0;
	float Damage = 0.f;
	float Radius = 0.f;
	float Range = 0.f;
	float Width = 0.f;       // Line / Dash half width, Cone degrees, Volley fan degrees, Barrage scatter radius, Chain jump range, Totem shot range
	int32 Count = 0;         // Volley projectiles, Barrage strikes, Chain hits, Totem targets per shot
	int32 Pierce = 0;        // Volley
	float Cooldown = 10.f;
	float Duration = 0.f;    // Zone / Self / Totem life, Line beam time, Volley waves, Barrage spread
	float Tick = 0.5f;
	float EffectValue = 0.f;
	float EffectTime = 0.f;
	float FinalDamage = 0.f; // Dash end blast, Zone collapse
	float Delay = 0.f;       // telegraph before a Circle / Nova / Global lands (Barrage: per strike)
	// Evolved (the signature's paired tome at max level)
	float EvoDamage = 1.f;   // x Damage, FinalDamage and EffectValue
	float EvoRadius = 1.f;   // x Radius (and Width for Line / Dash / Cone)
	int32 EvoCount = 0;
	float EvoCooldown = 1.f;
	float EvoDuration = 1.f;
	uint32 EvoEffects = 0;   // added effect, using EvoValue / EvoTime
	float EvoValue = 0.f;
	float EvoTime = 0.f;
	FString EvoText;
	/**
	 * Character pass (Nov 25): how this ability plays beyond its shape (Kits.csv <Slot>_Mod / ModValue / ModTime). None by default.
	 *   Charges  (E): ModValue charges; each recharges in one cooldown, one at a time
	 *   Recast   (E): pressing again within ModTime snaps the reaper back to where it cast from, hitting there for ModValue x Damage
	 *   Stacks   (E): each cast within ModTime of the last adds a stack (max 3) of +ModValue damage
	 *   Trap     (Circle): arms at the target for ModTime and goes off when an enemy comes near; ModValue traps armed at most
	 *   Detonate (any): ModValue extra bursts (FinalDamage, or Damage) at the target, ModTime apart
	 */
	FName Mod;
	float ModValue = 0.f;
	float ModTime = 0.f;
	TArray<FName> Anims;     // cast take fallback list
	FName Fx;                // flavour particles (Pillar, Embers, Shards, Smoke, Sparks, Ghost, Lightning, Stamp, Slash, Dust, Paper, Holy, Time, Blood, Streaks, Crows)
	FLinearColor Color = FLinearColor::White;
	FString Description;
};

/** Kits.csv row: one reaper's whole kit (Docs/Kits.md) */
struct FSQKitDef
{
	FName ReaperId;
	FName SignatureWeapon;
	FName EvoTome;
	FName PassiveId;
	FString PassiveName;
	FString PassiveGlyph;
	FString PassiveDesc;
	float P[4] = { 0.f, 0.f, 0.f, 0.f };
	TArray<FName> PassiveAnims;
	/** [0] = E, [1] = R */
	FSQAbilityDef Abilities[2];
};

/** How an enemy moves (server steering + client body motion). Enemies.csv Gait column. */
enum class ESQEnemyGait : uint8 { Shamble, Lurch, Scuttle, Hop, ZigZag, Drift, Charge, Crawl, Stomp, Circle, Sprint };
/** How it enters the world (client look; the server keeps it inert for SQEnemySpawnTime). */
enum class ESQEnemySpawn : uint8 { Rise, Crawl, Drop, Burst, Fade };
/** Default death (a big killing hit always launches the body instead). */
enum class ESQEnemyDeath : uint8 { Tumble, Bones, Launch, Pop, Fade };
/**
 * Flying enemies (Enemies.csv Fly column, appended Nov 2026). None = walks on the ground.
 * Swoop = bat swarm: zig-zags at altitude, telegraphs (shadow + red marker), dives at a reaper, climbs back up.
 * Phase = floating ghost: low hover, drifts through the crowd, phases out and blinks closer.
 * Flyers never body-block reapers, ignore the terrain / hazards, and are hit by every weapon (the sim is 2D).
 */
enum class ESQEnemyFly : uint8 { None, Swoop, Phase };

/** Boss abilities (Bosses.csv Abilities / Phase2 / Phase3 columns: "Name:Weight|Name:Weight") */
enum class ESQBossAbility : uint8 { None, Slam, Shockwave, Spiral, Fan, Charge, Summon, Pools, Vortex, Mark, Pylons, Tether, Ring,
	// Nov 25 2026 run structure: map bosses built on their map's mechanic (Docs/RunStructure.md)
	TombWall, Burrow, Raise, Cracks,              // The Gravekeeper (Graveyard terraces)
	Flood, Tentacles, GhostShip, KingTide,        // The Drowned King (Harbour tide)
	TimeZones, ClockSweep, Rewind,                // The Merchant of Hours (Clocktower)
	Count };
struct FSQBossAbilityPick
{
	ESQBossAbility Ability = ESQBossAbility::None;
	float Weight = 1.f;
};

struct FSQEnemyDef
{
	FName Id;
	FString Name;
	ESQEnemyBehaviour Behaviour = ESQEnemyBehaviour::Chase;
	float Health = 10.f, HealthPerNight = 0.f;
	float Speed = 200.f;
	float Damage = 5.f;
	float Radius = 40.f;
	int32 Souls = 1;
	int32 XP = 1;
	float Weight[3] = { 0.f, 0.f, 0.f };
	float MinProgress = 0.f;
	FName Shape;
	float Scale = 1.f;
	FLinearColor Color = FLinearColor::White;
	FName SplitInto;
	int32 SplitCount = 0;
	float Range = 0.f;
	FName Model;    // imported character, empty = primitive shape
	FName Anim;     // looping move animation (A_<Anim>): first entry of MoveAnims
	float Height = 150.f; // art height in cm before Scale

	// --- Look + animation personality (Enemies.csv columns after Height; every one optional) ---
	TArray<FName> MoveAnims;     // Anim column as a '|' fallback list ("SQ_Enemy_Shamble|Walking_D_Skeletons")
	float AnimRate = 1.f;        // move cycle play rate
	ESQEnemyGait Gait = ESQEnemyGait::Shamble;
	ESQEnemySpawn Spawn = ESQEnemySpawn::Rise;
	ESQEnemyDeath Death = ESQEnemyDeath::Tumble;
	float Windup = 0.f;          // seconds of telegraph before a melee hit / shot / blast lands (0 = instant)
	TArray<FName> AttackAnims;   // wind-up + strike take (fallback list)
	TArray<FName> HitAnims;      // hit react take (fallback list)
	FName Prop;                  // Halloween Bits prop (Art/Props/SM_<Prop>); with no Model it IS the body
	FName PropBone;              // bone or socket (head, chest, hand.r, hand.l, hips); default head
	float PropSize = 40.f;       // prop's longest side in cm (before Scale)
	FVector PropOffset = FVector::ZeroVector;     // cm, in the bone's space
	FRotator PropRot = FRotator::ZeroRotator;     // pitch|yaw|roll
	FLinearColor Accent = FLinearColor::White;    // prop tint + glow colour
	float Glow = 0.f;            // emissive strength (elites ~1, glowing heads ~0.4)
	FVector2D Stretch = FVector2D(1.f, 1.f);      // silhouette: width x height multiplier
	float TintStrength = 0.55f;  // how strongly Color tints the skeleton texture
	int32 Stack = 0;             // extra skeletons stacked on its shoulders (tower gag)
	TArray<FName> StackAnims;    // pose of the stacked riders (fallback list)

	// --- Flying (Enemies.csv columns appended after StackAnim; every one optional) ---
	ESQEnemyFly Fly = ESQEnemyFly::None;
	float Altitude = 0.f;        // cm above the ground while cruising (client render; derived from type + state)
	float FlyBob = 0.f;          // cm of up-down bob while cruising
	bool IsFlyer() const { return Fly != ESQEnemyFly::None; }

	// --- Collision (Enemies.csv Reach, appended; Nov 7 collision pass) ---
	/** Weapon / claw reach in cm past touching: a hit lands when centre distance <= body radius + reaper radius + Reach */
	float Reach = 12.f;
};

struct FSQBossDef
{
	FName Id;
	FString Name;
	FString Title;
	int32 Night = 1;
	float Health = 1000.f;
	float Speed = 250.f;
	float SlamDamage = 20.f, SlamRadius = 350.f, SlamCooldown = 6.f;
	int32 RingCount = 8;
	float RingDamage = 10.f, RingCooldown = 5.f;
	FName SummonId;
	int32 SummonCount = 8;
	float SummonCooldown = 9.f;
	float Scale = 3.f;
	FLinearColor Color = FLinearColor::White;
	FString IntroLine;
	FString ContractText;
	FName Model;
	FName Weapon;

	// --- Ability rotation + style (Bosses.csv columns after Weapon; every one optional) ---
	TArray<FSQBossAbilityPick> Abilities;   // phase 1 pool (empty = Slam, Ring, Summon)
	TArray<FSQBossAbilityPick> Phase2;      // added at 66% HP
	TArray<FSQBossAbilityPick> Phase3;      // added at 33% HP
	TArray<FName> Elements;                 // element cycle (Fire|Ice|Lightning), empty = none
	float TargetSeconds = 180.f;            // intended time-to-kill (logged against the real one)
	FName Prop;                             // Halloween Bits prop on a bone
	FName PropBone;
	float PropSize = 80.f;
	FLinearColor Accent = FLinearColor::White;
	float Glow = 0.4f;
	TArray<FName> MoveAnims;                // walk / run fallback list
	TArray<FName> RoarAnims;                // phase-change take fallback list
	FName SummonFlyer;                      // Bosses.csv SummonFlyer (appended): flyers added to every Summon (none = ground only)
	int32 SummonFlyerCount = 0;             // Bosses.csv SummonFlyerCount: flyers per Summon (+1 per phase)
};

struct FSQShopDef
{
	FName Id;
	FString Name;
	int32 Cost = 50;
	FName Effect;
	float Value = 1.f;
	FString Description;
};

struct FSQBiomeDef
{
	FName Id;
	FString Name;
	FString Description;
	ESQGimmick Gimmick = ESQGimmick::None;
	FString GimmickText;
	FLinearColor GroundColor = FLinearColor(0.16f, 0.22f, 0.12f);
	FLinearColor PropColor = FLinearColor(0.45f, 0.45f, 0.47f);
	FLinearColor FogColor = FLinearColor(0.12f, 0.08f, 0.2f);
	FName PropShape;
	int32 PropCount = 70;
	TArray<FName> Props;  // imported blocking props (SM_<name>), picked at random
	TArray<FName> Decor;  // imported small non-blocking scatter
	int32 DecorCount = 120;
	FName BossId;
	FName RequiresContract;
	bool bRequiresAllContracts = false;
};

struct FSQBondDef
{
	FName Id;
	FString Name;
	FName RelicA;
	FName RelicB;
	FName Stat;
	float Value = 0.f;
	FString Description;
};

struct FSQStoryDef
{
	FName Id;
	int32 Night = 0;
	FString Speaker;
	FString Text;
};

/**
 * One map interactable (Shrines.csv): shrines, altars, pots and secrets. Type picks the behaviour (ASQShrine / ASQPot);
 * the meaning of Value / Value2 / Enemy / Count / Duration / Loot depends on the type (see the CSV header comments).
 */
struct FSQShrineDef
{
	FName Id;
	FString Name;
	FName Type;                 // Charge, Relic, Challenge, Greed, Magnet, Cursed, Key, Crypt, Pot
	TArray<FName> Biomes;       // empty = every biome
	int32 PerBiome = 1;         // how many are scattered per night
	float Chance = 1.f;         // per-copy spawn chance
	float HighGround = 0.f;     // share placed on plateau tops (0..1)
	bool bSecret = false;       // only placed in biomes that list it in Biomes.csv Secrets
	bool bHidden = false;       // buried: dig (stand on it) to reveal
	float Radius = 300.f;
	float ChargeSeconds = 6.f;
	float CoopBonus = 0.5f;     // charge speed per extra reaper in the ring
	int32 Choices = 3;
	int32 Rarity = 0;           // minimum rarity of the offers (3 = Legendary only)
	float Value = 0.f;
	float Value2 = 0.f;
	FName Enemy;
	int32 Count = 0;
	float Duration = 0.f;
	FName Model;                // Art/Props/SM_<Model>; primitive fallback when missing
	float Height = 150.f;       // art height in cm
	FLinearColor Color = FLinearColor::White;
	float Glow = 1.f;
	FString Prompt;
	FString Boosts;             // Charge: "Stat:Amount|..."; Pot: "Kind:Weight:Amount|..."
	FString Description;
};

/**
 * Loads every game number from Content/Data/*.csv when the game starts.
 * Edit a CSV, press Play again: no recompile needed. Console: SQReload.
 */
UCLASS()
class SOULQUOTA_API USQData : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static USQData* Get(const UObject* WorldContext);

	void Reload();

	float Tune(FName Key, float Default) const;
	/** The CSV value, ignoring this machine's graphics overrides */
	float TuneBase(FName Key, float Default) const;
	/**
	 * Platform pass (Nov 23 2026): a LOCAL override of a cosmetic Tuning key (SETTINGS > GRAPHICS, see SQPerf.h). Never use it
	 * for anything that changes collision, spawning or damage: co-op machines may hold different values.
	 */
	void SetLocalTune(FName Key, float Value) { LocalTuning.Add(Key, Value); }
	void ClearLocalTunes() { LocalTuning.Reset(); }
	const FSQUpgradeDef* FindUpgrade(FName Id) const;
	const FSQReaperDef* FindReaper(FName Id) const;
	/** Kits.csv row of a reaper (nullptr when it has no kit) */
	const FSQKitDef* FindKit(FName ReaperId) const;
	const FSQBossDef* FindBoss(FName Id) const;
	const FSQBossDef* FindBossForNight(int32 Night) const;
	int32 FindEnemyIndex(FName Id) const;
	const FSQBiomeDef* FindBiome(FName Id) const;
	/** Shrines.csv row by Id (nullptr when missing) */
	const FSQShrineDef* FindShrine(FName Id) const;

	TArray<FSQUpgradeDef> Upgrades;
	TArray<FSQReaperDef> Reapers;
	TArray<FSQEnemyDef> Enemies;
	TArray<FSQBossDef> Bosses;
	TArray<FSQShopDef> Shop;
	TArray<FSQStoryDef> Story;
	TArray<FSQBiomeDef> Biomes;
	TArray<FSQBondDef> Bonds;
	/** Reaper kits (Kits.csv; Docs/Kits.md) */
	TArray<FSQKitDef> Kits;
	/** Map interactables (Shrines.csv) */
	TArray<FSQShrineDef> Shrines;
	TMap<FName, float> Tuning;
	/** This machine's graphics overrides (SetLocalTune); checked before Tuning */
	TMap<FName, float> LocalTuning;

	FString LoadErrors;
};

namespace SQ
{
	SOULQUOTA_API UStaticMesh* Mesh(FName Shape);
	SOULQUOTA_API UMaterialInstanceDynamic* ColorMID(UObject* Outer, const FLinearColor& Color);

	/** Weapon numbers at a level. Power = FSQOwned::Power (rarity bonus, 1 = Common only). */
	SOULQUOTA_API float UpgradeDamage(const FSQUpgradeDef& Def, int32 Level, float Power = 1.f);
	SOULQUOTA_API float UpgradeCooldown(const FSQUpgradeDef& Def, int32 Level);
	/** Power widens areas a little: +25% of the power bonus */
	SOULQUOTA_API float UpgradeRadius(const FSQUpgradeDef& Def, int32 Level, float Power = 1.f);
	/** Multi-shot weapons get +1 count for every 0.4 of power bonus */
	SOULQUOTA_API int32 UpgradeCount(const FSQUpgradeDef& Def, int32 Level, float Power = 1.f);
	SOULQUOTA_API FString KindName(ESQUpgradeKind Kind);
	/** Card chip text: WEAPON / TOME / ITEM / EVOLUTION / BONUS */
	SOULQUOTA_API FString CategoryName(const FSQUpgradeDef& Def);

	// --- Rarity (0 Common, 1 Rare, 2 Epic, 3 Legendary) ---
	SOULQUOTA_API FString RarityName(uint8 R);
	/** Common bone-white #E8E2D0, Rare teal #5CF2D6, Epic violet #B47CFF, Legendary amber #FFB547 */
	SOULQUOTA_API FLinearColor RarityColor(uint8 R);
	/** Weapon power added by one pick: 0 / 0.10 / 0.22 / 0.40 */
	SOULQUOTA_API float RarityWeaponPower(uint8 R);
	/** Relic stat gain multiplier for one pick: 1.0 / 1.4 / 1.9 / 2.6 */
	SOULQUOTA_API float RarityStatMult(uint8 R);
	/** Short concrete line of what this card gives, e.g. "+18% damage, +1 projectile" or "+12% move speed" */
	SOULQUOTA_API FString DescribeUpgradeGain(const FSQUpgradeDef& Def, int32 CurrentLevel, uint8 Rarity);

	// --- Stats (relics, bonds) ---
	/** Adds Value of a named stat (CSV Stat column) to S. Returns false for an unknown stat name. */
	SOULQUOTA_API bool ApplyStat(FSQStats& S, FName Stat, float Value);
	/**
	 * Raw summed stats -> the stats the game uses (items/tomes pass). Every source (reaper, tomes, items, bonds, shrine
	 * raises, the Intern) adds raw values with ApplyStat; this runs ONCE on the total (never twice: the curves are not
	 * idempotent). Tuning keys (defaults in brackets; Docs/ItemsAndTomes.md "Stat caps"):
	 *   Cooldown: raw "Cooldown" values are attack speed: CooldownMult = 1 / (1 + sum), floored at CooldownFloor [0.4]
	 *   CritChance capped at CritChanceMax [1]; the overflow becomes crit damage x CritOverflowToDamage [1]
	 *   AreaMult capped at AreaMax [3]; Duration capped at DurationMax [3]
	 *   Armor: damage taken x ArmorK / (ArmorK + armor) [1] (armor 0.25 = +25% effective HP), at most ArmorMax [0.75]
	 *   Evasion: x / (x + EvasionK) [1], at most EvasionMax [0.4]
	 *   Luck: LuckSoftCap x L / (L + LuckSoftCap) [2] (small luck ~linear, never above 2)
	 *   ProjectileBonus <= ProjectileBonusMax [4], TurretBonus <= TurretBonusMax [6], MoveSpeed <= MoveSpeedMax [1100],
	 *   SoulBonus <= 1, ExtraJumps <= 4, DashCooldown >= 0.4
	 * D may be null (defaults).
	 */
	SOULQUOTA_API void FinalizeStats(FSQStats& S, const USQData* D);
	/** "+10% damage", "+60 pickup range", "-0.2s dash cooldown"... */
	SOULQUOTA_API FString StatText(FName Stat, float Value);
	/** /Game/Audio/SFX_<Name> made by Scripts/create_audio.py (nullptr if not generated yet) */
	SOULQUOTA_API USoundBase* Sound(const TCHAR* Name);
}
