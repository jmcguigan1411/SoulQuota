// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "SQAbility.generated.h"

class ASQCharacter;
class ASQHordeManager;
class ASQGameState;
class USQData;
class USQAnimInstance;
struct FSQKitDef;
struct FSQAbilityDef;

/**
 * What the HUD / lobby draws for one kit slot (USQAbilityComponent::GetAbilityView, DescribeKit).
 * Slots: 0 = signature weapon, 1 = passive, 2 = E, 3 = R.
 */
USTRUCT(BlueprintType)
struct FSQAbilityView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FText Name;
	/** 1-2 ASCII characters for the icon */
	UPROPERTY(BlueprintReadOnly) FString Glyph;
	UPROPERTY(BlueprintReadOnly) FText Description;
	/** Seconds left (owner only: cooldowns replicate to the owning client) */
	UPROPERTY(BlueprintReadOnly) float Cooldown = 0.f;
	/** Full cooldown of the last cast (or the current one, with stats, before the first cast) */
	UPROPERTY(BlueprintReadOnly) float CooldownMax = 0.f;
	UPROPERTY(BlueprintReadOnly) bool bUnlocked = false;
	/** Team level that unlocks it (Tuning AbilityUnlockE / AbilityUnlockR; 0 for the weapon and passive) */
	UPROPERTY(BlueprintReadOnly) int32 UnlockLevel = 0;
	/** The signature's paired tome is maxed (E / R / passive), or the signature itself evolved (weapon) */
	UPROPERTY(BlueprintReadOnly) bool bEvolved = false;
	/** Not used by any kit yet (0) */
	UPROPERTY(BlueprintReadOnly) int32 Charges = 0;
	/** Extras for the UI */
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
	/** "Evolves with <tome>: <what changes>" */
	UPROPERTY(BlueprintReadOnly) FText EvolvedText;
	/** True while the owner holds the key to aim this ability (aim-hold mode) */
	UPROPERTY(BlueprintReadOnly) bool bAiming = false;
};

/** A summoned post (R totems, Bonesmith sentries, Ectoplasm ghosts) - replicated for the visuals only */
USTRUCT()
struct FSQAbilityTotemNet
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize Pos;
	/** 0 = R totem, 1 = passive sentry, 2 = ghost */
	UPROPERTY() uint8 Style = 0;
	UPROPERTY() FColor Color = FColor::White;
	UPROPERTY() float Radius = 100.f;
};

/**
 * Reaper kit runtime (Nov 9 kits pass; Docs/Kits.md; Content/Data/Kits.csv): the passive, the E (slot 2) and R (slot 3)
 * abilities, their evolution, bot casting and every cosmetic. Server-authoritative: the owner sends ServerCast with a quantized
 * aim point; the server checks unlock level (team level), cooldown and range and applies the effects through ASQHordeManager.
 * Cosmetics go out as unreliable multicasts and run locally from Kits.csv. Cooldowns replicate to the owner only.
 */
UCLASS(ClassGroup = (SoulQuota))
class SOULQUOTA_API USQAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USQAbilityComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Input (owning machine; ASQCharacter::TryCastAbility forwards here) ---
	/**
	 * Slot 2 = E, 3 = R. AimPoint = unclamped ground point under the cursor. Tuning QuickCast 1 (default): casts at once.
	 * QuickCast 0: the first call starts aiming (local indicator follows the cursor), the next call for the same slot casts
	 * (call it on press and again on release). The range is clamped here and again on the server.
	 */
	void TryCast(uint8 Slot, const FVector& AimPoint);
	/** Aim-hold mode: stop aiming without casting */
	void CancelAim();

	// --- View API (UI) ---
	/** Slot 0 = signature weapon, 1 = passive, 2 = E, 3 = R. False when the reaper has no kit. */
	bool GetAbilityView(uint8 Slot, FSQAbilityView& Out) const;
	FText GetPassiveName() const;
	FText GetPassiveDescription() const;
	/** The kit of a reaper before it is spawned (lobby / roster). OutSlots = Weapon, Passive, E, R. */
	static bool DescribeKit(const USQData* D, FName ReaperId, TArray<FSQAbilityView>& OutSlots /*Weapon,Passive,E,R*/);

	// --- Hooks from ASQCharacter ---
	void ResetForRun();
	/** Server: a kill credited to the owner */
	void OnOwnerKill(const FVector2D& Where, bool bElite);
	/** Server: the owner dashed */
	void OnOwnerDash(const FVector& Direction);
	// --- Ultimate charge (reap core, Nov 9): R (slot 3) fills from reaped souls + a kill trickle instead of a cooldown ---
	/** 0..1 charge of the R (owner: replicated; 1 = ready). The HUD's slot-3 sweep shows it through GetAbilityView too. */
	float GetUltCharge01() const;
	/**
	 * Server: add raw charge (Tuning UltChargePerReap / UltChargePerKill units). Scaled by UltRefCooldown / this kit's R cooldown
	 * (evolution and haste included), so every kit keeps its R's relative pace. bShare: reaped souls also give UltShareFraction of
	 * the raw amount to every other standing reaper within UltShareRange.
	 */
	void AddUltCharge(float RawAmount, bool bShare);
	/** Server, co-op pass (Nov 23 2026): a timed buff from outside the kit (revive reward haste). Type = one SQAbilityFx ally bit. */
	void GiveBuff(uint32 Type, float Value, float Seconds) { ReceiveBuff(Type, Value, Seconds); }
	/** Damage multiplier on top of Stats.DamageMult (passive + buffs). Server (and anywhere for display). */
	float GetOutgoingDamageMult() const;
	/** Damage-taken multiplier (Bulwark reapers nearby, Fortify buffs) */
	float GetIncomingDamageMult() const;
	/** Move speed multiplier (Haste buffs replicate; Fetch with a skull). Server and owning client agree. */
	float GetMoveSpeedMult() const;
	/** Soul pickup pull range (Fetch makes it huge) */
	float GetSoulMagnet(float NormalRange) const;
	FName GetPassiveId() const;
	/** The passive's main number (P1): the horde reads it for Grief Tax (SlowSouls) */
	float GetPassiveValue() const;
	/** Local: the passive's ability moment (PassiveAnim list) */
	bool PlayPassiveAnim(USQAnimInstance* SkinAnim, bool bFullBody, uint8 Variant) const;
	/** The signature's paired tome is at max level: E and R are evolved */
	bool IsEvolved() const;
	const FSQKitDef* GetKit() const;

	// --- Network ---
	UFUNCTION(Server, Reliable) void ServerCast(uint8 Slot, FVector_NetQuantize AimPoint);
	/** Flags: 1 = evolved, 2 = echo (Overeager), 4 = big (R) */
	UFUNCTION(NetMulticast, Unreliable) void MulticastCast(uint8 Slot, FVector_NetQuantize Origin, FVector_NetQuantize Target, uint8 Flags);
	/** Kind: 0 impact, 1 telegraph disc (Duration = warning time), 2 heal / buff burst on a reaper, 3 big impact */
	UFUNCTION(NetMulticast, Unreliable) void MulticastAbilityHit(uint8 Kind, FVector_NetQuantize Where, float Radius, FColor Color, float Duration);
	/** Bolts / chains: a jagged line through Points */
	UFUNCTION(NetMulticast, Unreliable) void MulticastAbilityLines(const TArray<FVector_NetQuantize>& Points, FColor Color);

	/** Server time each ability is ready again ([0] = E, [1] = R); owner only */
	UPROPERTY(Replicated) float ReadyAtE = 0.f;
	UPROPERTY(Replicated) float ReadyAtR = 0.f;
	UPROPERTY(Replicated) float CooldownMaxE = 0.f;
	UPROPERTY(Replicated) float CooldownMaxR = 0.f;
	/** Reap core: R charge 0..1 (owner only). R casts at 1 and resets to 0. */
	UPROPERTY(Replicated) float UltCharge = 0.f;
	/** Haste buffs (move speed multiplier), everyone: the owning client predicts movement with it */
	UPROPERTY(Replicated) float BuffSpeedMult = 1.f;
	/** Summoned posts for the visuals */
	UPROPERTY(Replicated) TArray<FSQAbilityTotemNet> Totems;

private:
	ASQCharacter* GetReaper() const;
	ASQGameState* GetGS() const;
	ASQHordeManager* GetHorde() const;
	float Now() const;
	float ServerNow() const;
	const FSQAbilityDef* GetAbility(int32 Idx) const;
	int32 UnlockLevelFor(int32 Idx) const;
	bool IsUnlocked(int32 Idx) const;
	float CooldownLeft(int32 Idx) const;
	/** Cooldown with stats: Cooldown x EvoCooldown x CooldownMult ^ AbilityHasteExp */
	float ComputeCooldown(int32 Idx) const;
	bool CanFightNow() const;
	FVector ClampAim(int32 Idx, const FVector& AimPoint) const;

	// --- Casting (server) ---
	struct FCast
	{
		int32 Idx = 0;
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D Target = FVector2D::ZeroVector;
		FVector2D Dir = FVector2D(1.f, 0.f);
		bool bEvo = false;
		bool bEcho = false;
		float Damage = 0.f;      // per hit, crit included
		float Final = 0.f;
		float Crit = 1.f;
		float Radius = 0.f;
		float Width = 0.f;
		float Range = 0.f;
		int32 Count = 0;
		int32 Pierce = 0;
		float Duration = 0.f;
		float Tick = 0.5f;
		uint32 Effects = 0;
		float EffectValue = 0.f;
		float EffectTime = 0.f;
		uint32 EvoEffects = 0;
		float EvoValue = 0.f;
		float EvoTime = 0.f;
		float Delay = 0.f;
		FColor Color = FColor::White;
	};
	bool BuildCast(int32 Idx, const FVector& AimPoint, bool bEcho, float Strength, FCast& Out) const;
	void ExecuteCast(int32 Idx, const FVector& AimPoint, bool bEcho, float Strength);
	/** Damage + enemy effects in a circle; returns enemies hit */
	int32 HitCircle(const FCast& C, const FVector2D& Center, float Radius, float Damage, uint32 Effects, float EffTime, float EffValue, const FVector2D& KnockDir);
	/** Enemy control effects (slow is applied with the damage) */
	void ApplyControl(const FCast& C, const FVector2D& Center, float Radius, uint32 Effects, float EffTime, float EffValue, const FVector2D& KnockDir);
	/** Reaper effects (heal, shield, haste, empower, fortify, revive) to every reaper within Radius of Center (Radius < 0 = everyone) */
	void ApplyAllyEffects(const FVector2D& Center, float Radius, uint32 Effects, float Value, float Time, bool bIncludeSelf);
	void Impact(const FCast& C);
	/** The evolved effect (EvoEffects with EvoValue / EvoTime) on enemies and reapers around Center */
	void ApplyEvo(const FCast& C, const FVector2D& Center, float Radius, const FVector2D& KnockDir);
	void FireVolley(const FCast& C, const FVector2D& From);
	void LineHit(const FCast& C, const FVector2D& From);
	/** Validates (fighting, unlocked, cooldown), casts and starts the cooldown. Server. */
	bool CommitCast(int32 Idx, const FVector& AimPoint);
	bool MoveOwner(const FVector2D& To);

	enum class EPendingKind : uint8 { Impact, Zone, Drop, Wave, Aura, Beam, Echo, Totem };
	struct FPending
	{
		EPendingKind Kind = EPendingKind::Impact;
		FCast Cast;
		FVector2D Pos = FVector2D::ZeroVector;
		float TimeLeft = 0.f;
		float TickLeft = 0.f;
		float Life = 0.f;
		float TotemDamage = 0.f;      // passive sentries / ghosts: shot damage
		float TotemInterval = 0.6f;
		float TotemRange = 1200.f;
		int32 TotemTargets = 1;
		float PullTimer = 0.f;
		bool bPassiveTotem = false;
		uint8 Style = 0;
		FName Source;
	};
	TArray<FPending> Pending;
	void TickPending(float DeltaTime);
	void SpawnTotem(const FVector2D& Where, uint8 Style, float Life, float ShotDamage, float Interval, float Range, int32 Targets, int32 MaxOfStyle, const FCast* FromCast, FColor Color);
	void RebuildTotemNet();
	/** Adds a built totem, enforcing the per-style and total caps */
	void InsertTotem(const FPending& Totem);
	/** Kills inside TickPending can spawn sentries / ghosts: those wait here until the loop ends */
	bool bInPendingTick = false;
	TArray<FPending> PendingAdds;

	// --- Buffs received (server) ---
	struct FBuff
	{
		uint32 Type = 0;     // one SQAbilityFx bit (Haste, Empower, Fortify, Shield)
		float Value = 0.f;
		float EndTime = 0.f;
		float ShieldGiven = 0.f;
	};
	TArray<FBuff> Buffs;
	void ReceiveBuff(uint32 Type, float Value, float Seconds, float ShieldAmount = 0.f);
	void TickBuffs();

	// --- Passive (server) ---
	void TickPassive(float DeltaTime);
	void QueuePassiveAnim(uint8 Variant);
	void ReduceCooldown(int32 Idx, float Seconds);
	int32 KillCounter = 0;
	int32 CastCounter = 0;
	float PassiveTimer = 0.f;
	float PassiveFxTimer = 0.f;
	float PassiveAnimTimer = 0.f;
	float LastPassiveAnim = -100.f;
	int32 PassiveStep = 0;
	int32 CarriedSeen = 0;
	uint16 MarkedEnemyId = 0;
	float CooldownCutThisCycle = 0.f;
	float FlockCrows = 0.f;
	float FlockDecay = 0.f;
	float FlockTick = 0.f;
	float ScorchDropTime = 0.f;
	float ScorchDropTimer = 0.f;
	float ScorchDamageTimer = 0.f;
	struct FScorchPatch
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float Life = 0.f;
	};
	TArray<FScorchPatch> ScorchPatches;
	TWeakObjectPtr<ASQCharacter> ReviveTarget;
	float ReviveChannel = 0.f;

	// --- Damage numbers (server; flushed through ASQCharacter::MulticastDamagePops) ---
	void AddPop(const FVector2D& Where, float Amount, float CritMult);
	void AddAreaPops(const FVector2D& Center, float Radius, float Amount, float CritMult, int32 MaxPops);
	void FlushPops(float DeltaTime);
	struct FPop
	{
		int16 X = 0;
		int16 Y = 0;
		uint16 Amount = 0;
		uint8 Info = 0;
	};
	TArray<FPop> PendingPops;
	float PopTimer = 0.f;

	// --- Bots (server, AI-controlled owners) ---
	void TickBot(float DeltaTime);
	bool FindBestTarget(float Range, float ClusterRadius, FVector2D& OutPos, int32& OutCount) const;
	float BotTimer = 0.f;

	// --- Local (owning client): aim-hold indicator ---
	void TickIndicator(float DeltaTime);
	bool GetCursorGround(FVector& Out) const;
	void DrawIndicator(int32 Idx, const FVector& Aim, float Alpha);
	uint8 AimingSlot = 255;
	FVector LastAim = FVector::ZeroVector;
	float LocalCastGate = 0.f;
	float QuickFlash = 0.f;
	uint8 QuickFlashSlot = 255;
	FVector QuickFlashAim = FVector::ZeroVector;

	// --- Local cosmetics (every machine) ---
	struct FLocalFx
	{
		int32 Idx = 0;
		uint8 Kind = 0;          // 0 delayed burst at Pos, 1 zone, 2 self aura (follows the reaper), 3 delayed burst at the reaper, 4 beam (Pos = offset)
		FVector Pos = FVector::ZeroVector;
		float TimeLeft = 0.f;
		float Life = 0.f;
		float Radius = 0.f;
		float Emit = 0.f;
		bool bBig = false;
		bool bPull = false;
		FLinearColor Color = FLinearColor::White;
		FName Fx;
	};
	TArray<FLocalFx> LocalFx;
	void TickLocalFx(float DeltaTime);
	void PlayCastAnim(int32 Idx, bool bBig);
	void DrawBurst(const FVector& At, float Radius, const FLinearColor& Color, FName Fx, bool bBig);
	void DrawFlavour(const FVector& At, float Radius, const FLinearColor& Color, FName Fx, bool bBig);
	float LocalTotemFx = 0.f;
	float LocalPassiveFx = 0.f;
};
