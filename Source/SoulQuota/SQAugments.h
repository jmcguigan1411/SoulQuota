// Copyright 2026 Jack McGuigan. All rights reserved.
// Augments (Nov 9 2026): rare, powerful run upgrades separate from weapon / tome / item picks (Augments.csv, data-driven: an effect enum +
// Value / Value2 / Value3 / Stat params). Offered 1 of 3 on the level-up card path with the AUGMENT style (ASQPlayerState::bAugmentOffer)
// at the 1st / 2nd miniboss kill, on objectives with an Augment reward, and at team levels AugmentLevel1 / AugmentLevel2.
// Every effect runs on the server through a few central hooks:
//   OnKill / OnHit      - ASQHordeManager::KillEnemy / DamageEnemy (one line each)
//   NotifyCast          - the ability agent's cast function (USQAbilityComponent) should call it
//   ModifyStats         - ASQCharacter::RecomputeStats, on the raw stats before SQ::FinalizeStats (also applies the META upgrades)
//   ExtraSlots          - ASQCharacter::GetWeaponSlots / GetTomeSlots
//   Tick (this subsystem) - deferred explosions / bolts, orbiting gravestones, team auras, field revives, hurt novas
// Damage effects are QUEUED and applied next frame: the hooks run inside the horde's own loops, which must not be re-entered.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SQTypes.h"
#include "SQAugments.generated.h"

class ASQCharacter;
class ASQHordeManager;
class ASQPlayerState;

enum class ESQAugmentEffect : uint8
{
	None, Stat, KillBlast, HitSplit, KillRefund, WeaponSlot, TomeSlot, Gravestones, QuickRevive, AuraStat, Execute, Overkill,
	KillHeal, KillGold, EliteHunter, LowHPDamage, CastNova, CastHaste, HurtNova, Pardon, LevelUps, Rerolls, ChestNow, XPBoost
};

/** Where an augment offer came from (sets the Silver / Gold / Prism odds) */
enum class ESQAugmentSource : uint8 { Level, Miniboss1, Miniboss2, Objective };

struct FSQAugmentDef
{
	FName Id;
	FString Name;
	FString Description;
	/** 0 Silver, 1 Gold, 2 Prism */
	uint8 Tier = 0;
	ESQAugmentEffect Effect = ESQAugmentEffect::None;
	float Value = 0.f;
	float Value2 = 0.f;
	float Value3 = 0.f;
	FName Stat;
	FName Stat2;
	bool bCoopOnly = false;
	/** 0 = any number per run */
	int32 MaxStacks = 1;
	float Weight = 1.f;
};

UCLASS()
class SOULQUOTA_API USQAugments : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USQAugments* Get(const UObject* WorldContext);

	// ---- Data (Augments.csv)
	static const TArray<FSQAugmentDef>& Augments();
	static const FSQAugmentDef* FindAugment(FName Id);
	static void Reload();
	static const FString& LoadErrors();
	/** "SILVER" / "GOLD" / "PRISM" */
	static FString TierName(uint8 Tier);

	// ---- Hooks (server; null-safe; cheap when nobody owns an augment)
	/** Horde kill: kill effects of By's augments + the mission director (minibosses, hunts). By may be null. */
	static void OnKill(ASQHordeManager* Horde, ASQCharacter* By, const FVector2D& Pos, int32 EnemyType, uint8 EcoFlags);
	/** Horde hit (before it lands): returns the damage to apply. Nests near Pos take a share. By may be null. */
	static float OnHit(ASQCharacter* By, float Damage, int32 EnemyType, float EnemyHealth, float EnemyMaxHealth, const FVector2D& Pos, uint8 EcoFlags);
	/** Ability cast (ability agent: call this from the cast function). Slot = the ability slot. */
	static void NotifyCast(ASQCharacter* Caster, uint8 Slot);
	/** Adds meta upgrades + stat augments + team auras to C's RAW stats (call inside RecomputeStats, before SQ::FinalizeStats) */
	static void ModifyStats(const ASQCharacter* C, FSQStats& Raw);
	/** Extra weapon (bWeapon) or tome slots from augments */
	static int32 ExtraSlots(const ASQCharacter* C, bool bWeapon);
	/** Seconds a kill takes off C's ability cooldowns (Ruthless Rhythm). The ability agent applies it; the dash is done here. */
	static float GetKillCooldownRefund(const ASQCharacter* C);
	/** Team XP multiplier bonus: crew average of the Night School meta upgrade + every Study Group augment on the crew */
	static float TeamXPBonus(const UWorld* World);

	// ---- Offers (server)
	/** Every reaper in the run gets one augment pick */
	static void GrantAugmentPickAll(UWorld* World, ESQAugmentSource Source);
	/** 3 distinct augments PS may take (Avoid = the set just rerolled), with their tiers */
	static void RollOffer(const ASQPlayerState* PS, ESQAugmentSource Source, const TArray<FName>& Avoid, TArray<FName>& OutIds, TArray<uint8>& OutTiers);
	/** PS just took Id (already in OwnedAugments): one-shot effects (pardon, level-ups, rerolls, chests) and a stat refresh */
	static void ApplyPicked(ASQPlayerState* PS, FName Id);
	static int32 CountOwned(const ASQPlayerState* PS, FName Id);
	/** "GOLD - Orbiting Gravestones: 3 gravestones orbit you..." */
	static FString Describe(FName Id);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FPendingBlast
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float Radius = 200.f;
		float Damage = 0.f;
		/** Chain generation: 0 = queued outside a blast; a blast's own kills queue Depth + 1 (cap AugmentChainMaxDepth) */
		int32 Depth = 0;
		TWeakObjectPtr<ASQCharacter> By;
	};
	struct FPendingBolt
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float Damage = 0.f;
		TWeakObjectPtr<ASQCharacter> By;
	};
	struct FReaperState
	{
		float OrbitAngle = 0.f;
		float OrbitTimer = 0.f;
		float OrbitFxTimer = 0.f;
		float LastHealth = -1.f;
		float HurtCooldown = 0.f;
		float ReviveProgress = 0.f;
		/** Kill cooldown refunds left this second (Tuning AugmentRefundPerSecond): abilities and dash have their own budget */
		float AbilityRefundBudget = 0.f;
		float DashRefundBudget = 0.f;
		bool bWasDowned = false;
		/** Team auras on this reaper right now (augment ids, one per owner) */
		TArray<FName> Auras;
	};

	void QueueBlast(const FVector2D& Pos, float Radius, float Damage, ASQCharacter* By);
	void QueueBolts(const FVector2D& Pos, float Damage, int32 Count, ASQCharacter* By);
	void ProcessQueues();
	void TickReapers(float DeltaTime);
	/** Base x (1 + AugmentDamagePerLevel x (team level - 1)) x By's damage multiplier */
	static float AugmentDamage(const ASQCharacter* By, float Base);

	TArray<FPendingBlast> Blasts;
	TArray<FPendingBolt> Bolts;
	TMap<TWeakObjectPtr<ASQCharacter>, FReaperState> States;
	float AuraTimer = 0.f;
	int32 FxThisFrame = 0;
	/** Chain generation of the blast being applied right now (QueueBlast tags new blasts with it) */
	int32 ActiveBlastDepth = 0;
};
