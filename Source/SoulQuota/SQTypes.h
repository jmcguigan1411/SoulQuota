// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "SQTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSQ, Log, All);

UENUM(BlueprintType)
enum class ESQPhase : uint8
{
	Waiting,
	Night,
	Boss,
	Shop,
	Victory,
	Defeat
};

// Plain enums (filled from CSV text, not exposed to Blueprint)
enum class ESQUpgradeKind : uint8 { Weapon, Relic, Evolution };
// Chain and Beam were added in the items remaster: keep them at the end so older values stay stable
enum class ESQWeaponBehaviour : uint8 { None, Arc, Projectile, Aura, Orbit, Nova, Drop, Turret, Chain, Beam };
enum class ESQEnemyBehaviour : uint8 { Chase, Wisp, Digger, Hound, Elite, Spitter, Bomber, Splitter };
enum class ESQGimmick : uint8 { None, Tide, Spotlight, Ice, TimeStop };

/** One upgrade a reaper owns (weapon, relic or evolution) */
USTRUCT(BlueprintType)
struct FSQOwned
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	UPROPERTY()
	int32 Level = 1;

	/**
	 * Rarity power (rolled on the level-up card).
	 * Weapons / evolutions: damage multiplier, starts at 1 and gains +0.10 / +0.22 / +0.40 per Rare / Epic / Legendary pick.
	 * Relics: total stat "picks", each pick adds 1.0 / 1.4 / 1.9 / 2.6 (Common..Legendary), so stat = StatPerLevel * Power.
	 */
	UPROPERTY()
	float Power = 1.f;

	/** Highest rarity ever picked for this item (0 Common .. 3 Legendary), for the HUD chip border. */
	UPROPERTY()
	uint8 BestRarity = 0;
};

/** Derived stats, computed on the server from reaper + relics, replicated to everyone */
USTRUCT(BlueprintType)
struct FSQStats
{
	GENERATED_BODY()

	UPROPERTY() float MaxHealth = 100.f;
	UPROPERTY() float MoveSpeed = 650.f;
	UPROPERTY() float DamageMult = 1.f;
	UPROPERTY() float CooldownMult = 1.f;
	UPROPERTY() float AreaMult = 1.f;
	UPROPERTY() int32 CarryCap = 50;
	UPROPERTY() float Magnet = 300.f;
	UPROPERTY() float Armor = 0.f;
	UPROPERTY() int32 ProjectileBonus = 0;
	UPROPERTY() float DashCooldown = 2.5f;
	UPROPERTY() float Regen = 0.f;
	UPROPERTY() int32 TurretBonus = 0;
	UPROPERTY() float SoulBonus = 0.f;   // chance per kill of +1 soul
	UPROPERTY() float BankSpeed = 0.f;   // +x faster banking
	UPROPERTY() float Thorns = 0.f;      // damage dealt to enemies that touch you
	UPROPERTY() float PickupHeal = 0.f;  // health per soul picked up
	// --- items remaster ---
	UPROPERTY() float Luck = 0.f;        // raises the chance of rarer level-up cards (1.0 = noticeably more rares)
	UPROPERTY() float CritChance = 0.05f; // chance per attack to crit
	UPROPERTY() float CritDamage = 0.5f;  // crits deal (1 + CritDamage) x damage
	UPROPERTY() float Duration = 1.f;    // multiplier: turret life, slows, projectile flight time
	UPROPERTY() int32 ExtraJumps = 0;    // extra mid-air jumps
	UPROPERTY() float Evasion = 0.f;     // chance to ignore a hit completely (capped at 0.5)
	UPROPERTY() float AllyRegen = 0.f;   // health per second given to every teammate within BondRange
};

/** Compact network snapshots (positions quantised to 4cm, flat arena) */
USTRUCT()
struct FSQEnemyNet
{
	GENERATED_BODY()

	UPROPERTY() uint16 Id = 0;
	UPROPERTY() int16 X = 0;
	UPROPERTY() int16 Y = 0;
	UPROPERTY() uint8 Type = 0;
	/** Animation state for clients: low 3 bits = ESQEnemyAnimState (SQHordeManager.h), bit 3 = burst-spawned (split / summon) */
	UPROPERTY() uint8 State = 0;
};

USTRUCT()
struct FSQPickupNet
{
	GENERATED_BODY()

	UPROPERTY() uint16 Id = 0;
	UPROPERTY() int16 X = 0;
	UPROPERTY() int16 Y = 0;
	UPROPERTY() uint8 Kind = 0; // 0 = XP, 1 = Soul
};

USTRUCT()
struct FSQProjNet
{
	GENERATED_BODY()

	UPROPERTY() uint16 Id = 0;
	UPROPERTY() int16 X = 0;
	UPROPERTY() int16 Y = 0;
	UPROPERTY() uint8 Hostile = 0;
};
