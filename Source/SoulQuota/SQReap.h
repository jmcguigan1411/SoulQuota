// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "SQReap.generated.h"

class ASQCharacter;
class ASQGameState;
class ASQHordeManager;
struct FSQReapHits;

/**
 * Reap core (Nov 9 2026; Docs/Balance.md section 12): "Sow, Soften, Reap".
 * The auto weapons soften the horde; enemies under Tuning ReapRipeFraction of their HP turn ripe (teal ring + skull tag, drawn by
 * ASQHordeManager from FSQEnemyNet::State bit 6). Left click swings a ghost scythe toward the cursor (TryReap): every ripe enemy in
 * the cone (ReapRange, ReapHalfAngle) is executed, the rest take ReapDamage; bosses and minibosses are never executed and take
 * ReapBossBonus x the damage instead. Each reaped soul pays +ReapSoulBonus souls, ReapXP x the streak team XP, R charge
 * (USQAbilityComponent::AddUltCharge, shared with teammates nearby) and counts toward a downed teammate's harvest revive.
 * Reaps within StreakWindow chain the Reap Streak (x2 .. xStreakMax: bonus XP and gold). During the dash's pass-through window
 * ripe enemies the reaper passes through are reaped too (Reaper's Passage). Bots reap by themselves.
 *
 * Server-authoritative: the owner sends ServerReap with a quantized aim point; the server checks the phase and the cooldown.
 * Cooldown and streak replicate to the owner only; the swing cosmetic is an unreliable multicast.
 */
UCLASS(ClassGroup = (SoulQuota))
class SOULQUOTA_API USQReapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USQReapComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- API for the UI / input agent (owning machine) ---
	/** Seconds until the scythe can swing again (owner) */
	float GetCooldownRemaining() const;
	/** Full reap cooldown (Tuning ReapCooldown) */
	float GetCooldownMax() const;
	/** Cone length in cm (Tuning ReapRange) */
	float GetRange() const;
	/** Cone half angle in degrees (Tuning ReapHalfAngle) */
	float GetHalfAngleDeg() const;
	/** Current Reap Streak: 0 = none, 1 = one reap, 2..StreakMax = the xN multiplier on reap XP / gold (owner) */
	int32 GetStreak() const;
	/** Seconds before the streak breaks (0 when there is none; owner) */
	float GetStreakTimeLeft() const;
	/** Left mouse: swing toward AimPoint (ground point under the cursor). Ignored while cooling down, downed, carried or paused. */
	void TryReap(const FVector& AimPoint);

	// --- Gameplay hooks (server) ---
	void ResetForRun();
	/**
	 * Pays out what one swing or dash passage reaped (Hits.Reaped > 0 = one streak step): souls, streak XP / gold, R charge (shared
	 * with teammates nearby) and the harvest revive of downed teammates within HarvestReviveRange of the reaped spots.
	 */
	void CreditReaps(const FSQReapHits& Hits);

	// --- Network ---
	UFUNCTION(Server, Reliable) void ServerReap(FVector_NetQuantize AimPoint);
	/** Flags: 1 = reaped something, 2 = hit the boss, 4 = dash passage (no swing). Spots = up to 12 reaped souls (soul pops). */
	UFUNCTION(NetMulticast, Unreliable) void MulticastReapSwing(FVector_NetQuantize Origin, FVector_NetQuantize Target, uint8 Flags, const TArray<FVector_NetQuantize>& Spots);

	/** Server time the scythe is ready again (owner only) */
	UPROPERTY(Replicated) float ReadyAt = 0.f;
	/** Reap Streak count and the server time it breaks (owner only) */
	UPROPERTY(Replicated) uint8 Streak = 0;
	UPROPERTY(Replicated) float StreakEndsAt = 0.f;

private:
	ASQCharacter* GetReaper() const;
	ASQGameState* GetGS() const;
	ASQHordeManager* GetHorde() const;
	float ServerNow() const;
	bool CanReapNow() const;
	/** Server: one swing toward AimPoint (cooldown already checked) */
	void DoReap(const FVector& AimPoint);
	/** Server: Reaper's Passage while the dash passes through bodies */
	void TickPassage();
	/** Server: AI-controlled reapers swing at ripe enemies in reach */
	void TickBot(float DeltaTime);
	/** Every machine: the swing effect, the attack clip and soul pops */
	void PlaySwingCosmetics(const FVector& Origin, const FVector& Target, uint8 Flags, const TArray<FVector_NetQuantize>& Spots);

	FVector2D LastPassagePos = FVector2D::ZeroVector;
	bool bPassageActive = false;
	float LocalGate = 0.f;
	float BotTimer = 0.f;
};
