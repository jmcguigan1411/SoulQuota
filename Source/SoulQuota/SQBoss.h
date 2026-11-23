// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SQData.h"
#include "SQBoss.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UAnimSequence;
class ASQCharacter;

/**
 * A legend who signed one of the Merchant's contracts. Rises at midnight.
 * Ability rotation (Bosses.csv Abilities / Phase2 / Phase3): weighted picks with per-ability cooldowns, every one
 * telegraphed (wind-up anim + ground marker in the ability's colour) 0.8-1.5 s before damage. Phases at 66% / 33%:
 * an invulnerable roar, new abilities, faster cadence. Co-op mechanics (pylons, tether, shared mark) with solo fallbacks.
 * Defiance stacks (missed quota) make it stronger.
 */
UCLASS()
class SOULQUOTA_API ASQBoss : public ACharacter
{
	GENERATED_BODY()

public:
	ASQBoss();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: set up from Bosses.csv */
	void InitBoss(FName InBossId, int32 InDefiance, int32 NumPlayers, float ExtraHealthMult = 1.f);
	void TakeBossDamage(float Damage, ASQCharacter* By);
	float HitRadius() const;

	// --- HUD hooks (every machine, from replicated state) ---
	int32 GetPhaseIndex() const { return Phase; }
	static constexpr int32 NumPhases = 3;
	bool IsShielded() const { return bShielded; }
	/** One short line for the boss bar: the mechanic the team must answer right now (empty = none) */
	FString GetMechanicHint() const;

	UPROPERTY(ReplicatedUsing = OnRep_BossId) FName BossId;
	UPROPERTY(Replicated) float Health = 1000.f;
	UPROPERTY(Replicated) float MaxHealth = 1000.f;
	UPROPERTY(Replicated) int32 DefianceStacks = 0;

	/** 0..2 (66% / 33% thresholds) */
	UPROPERTY(ReplicatedUsing = OnRep_Phase) uint8 Phase = 0;
	/** ESQBossAbility being telegraphed / performed */
	UPROPERTY(Replicated) uint8 ActiveAbility = 0;
	/** Element phase (Merchant): 0 none, 1 fire, 2 ice, 3 lightning */
	UPROPERTY(ReplicatedUsing = OnRep_Element) uint8 Element = 0;
	/** Phase-change roar: no damage taken */
	UPROPERTY(Replicated) bool bInvulnerable = false;
	/** Pylon shield: 90% damage reduction until every pylon is held at once (solo: lit one by one) */
	UPROPERTY(Replicated) bool bShielded = false;
	UPROPERTY(Replicated) float ShieldProgress = 0.f;
	UPROPERTY(Replicated) TArray<FVector_NetQuantize> Pylons;
	UPROPERTY(Replicated) uint8 PylonHeldMask = 0;
	UPROPERTY(Replicated) uint8 PylonLitMask = 0;
	/** Shared mark: a circle chasing one reaper; it detonates and the damage is split between everyone inside */
	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> MarkTarget;
	UPROPERTY(Replicated) FVector_NetQuantize MarkLoc = FVector_NetQuantize(FVector::ZeroVector);
	UPROPERTY(Replicated) float MarkLeft = 0.f;
	/** Tether: two reapers (solo: reaper + boss) must stay between TetherMin and TetherMax apart */
	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> TetherA;
	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> TetherB;
	/** Stunned after its shield breaks: takes extra damage */
	UPROPERTY(Replicated) bool bStaggered = false;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Crown;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> EyeL;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> EyeR;
	/** Imported character (hidden until the art loads; primitive body is the fallback). */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Skin;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> WeaponMesh;
	/** Bosses.csv Prop on PropBone */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PropMesh;
	/** Mechanic visuals (world space, every machine) */
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> PylonMeshes;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> MarkDisc;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> TetherBeam;

	UFUNCTION(NetMulticast, Unreliable) void MulticastFX(uint8 Kind, FVector_NetQuantize Location, float Radius, FColor Color, float Duration);
	/**
	 * One-shot art animation on every machine: 0 slam chop, 1 summon, 2 shoot, 3 death, 4 roar (phase change),
	 * 5 charge wind-up, 6 channel (vortex / spiral), 7 point (mark / tether), 8 shield raise.
	 */
	UFUNCTION(NetMulticast, Unreliable) void MulticastAnim(uint8 Which);

protected:
	UFUNCTION() void OnRep_BossId();
	UFUNCTION() void OnRep_Phase();
	UFUNCTION() void OnRep_Element();
	void ApplyVisuals();
	void ApplyGlow();
	ASQCharacter* FindTarget() const;
	void PlayOneShot(uint8 Which);
	void UpdateArtAnim(float DeltaSeconds);
	void UpdateMechanicVisuals(float DeltaSeconds);

	// --- Server: ability rotation ---
	enum class EStage : uint8 { Gap, Windup, Active };
	struct FBossWave   // expanding shockwave ring: jump it or be outside it
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 0.f;
		float MaxRadius = 1200.f;
		float Speed = 900.f;
		float Damage = 10.f;
		float Delay = 0.f;
		TArray<TWeakObjectPtr<ASQCharacter>> Hit;
	};
	struct FBossStrike // telegraphed circle that lands after Delay
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 200.f;
		float Delay = 1.f;
		float Damage = 10.f;
		FColor Colour = FColor::Red;
	};
	struct FBossHazard // lingering pool
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 250.f;
		float TimeLeft = 6.f;
		float Dps = 8.f;
	};
	void ServerAbilities(float DeltaSeconds, const FSQBossDef& Def);
	void ServerPhase(const FSQBossDef& Def);
	void ServerHazards(float DeltaSeconds);
	ESQBossAbility PickAbility(const FSQBossDef& Def) const;
	void BeginAbility(ESQBossAbility Ability, const FSQBossDef& Def);
	void ResolveAbility(const FSQBossDef& Def);
	/** false = keep going */
	bool TickActiveAbility(float DeltaSeconds, const FSQBossDef& Def);
	void EndAbility();
	void ClearMechanics();
	void GatherPlayers(TArray<ASQCharacter*>& Out) const;
	/** A standing reaper, preferring one the last ability did not target (so abilities spread over the team) */
	ASQCharacter* PickVictim(bool bFurthest);
	float GroundAtXY(float X, float Y) const;
	float CadenceMult() const;
	float RoarSeconds() const;
	float DamageMult() const;
	FColor AbilityColour(ESQBossAbility Ability) const;

	float SlamCd = 3.f;
	float ContactCd = 0.f;
	float VisualTime = 0.f;
	bool bDead = false;
	/** Server: seconds spent pushing toward a far target without moving (cliff / prop) -> hop */
	float BossStuckTime = 0.f;

	EStage Stage = EStage::Gap;
	float StageTime = 2.5f;
	float ActiveTime = 0.f;
	float PulseTimer = 0.f;
	int32 PulseCount = 0;
	float AbilityCd[16] = { 0.f };
	ESQBossAbility LastAbility = ESQBossAbility::None;
	TWeakObjectPtr<ASQCharacter> Victim;
	TWeakObjectPtr<ASQCharacter> LastVictim;
	FVector AbilityPoint = FVector::ZeroVector;
	FVector AbilityDir = FVector::ForwardVector;
	float SpiralAngle = 0.f;
	TArray<TWeakObjectPtr<ASQCharacter>> ChargeHit;
	TArray<FBossWave> Waves;
	TArray<FBossStrike> Strikes;
	TArray<FBossHazard> Hazards;
	float HazardTick = 0.f;
	float TetherTick = 0.f;
	float ShieldTimeLeft = 0.f;
	float PylonHold[4] = { 0.f, 0.f, 0.f, 0.f };
	float RoarLeft = 0.f;
	float StaggerLeft = 0.f;
	float ElementTime = 0.f;
	float ElementTick = 0.f;
	int32 ElementIdx = -1;
	float FightTime = 0.f;
	int32 TeamSize = 1;
	float BaseWalkSpeed = 250.f;
	float BaseAccel = 2048.f;
	bool bEnragedAnnounced = false;

	// Imported-art animation state (cosmetic, every machine)
	bool bArtVisuals = false;
	FName ArtModel;
	float OneShotLeft = 0.f;
	FVector LastArtLocation = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ArtMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PropMID;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PylonMIDs;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MarkMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TetherMID;
	float CurGlow = -1.f;
	float FxTimer = 0.f;
};
