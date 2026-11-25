// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQAbility.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "SQCharacter.h"
#include "SQPlayerController.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQHordeManager.h"
#include "SQBoss.h"
#include "SQFx.h"
#include "SQFxTheme.h"
#include "SQArena.h"
#include "SQAnimInstance.h"
#include "SQAugments.h"
#include "SQLantern.h"
#include "GameFramework/PlayerState.h"
#include "SQPlayerState.h"

// ---------------------------------------------------------------- helpers (prefixed: unity build)
namespace
{
	constexpr uint8 SQAbSlotE = 2;
	constexpr float SQAbUltReady = 0.999f;   // reap core: R charge that counts as full
	constexpr float SQAbUltScale = 100.f;    // the R's "cooldown" in the view = missing charge in percent
	constexpr uint32 SQAbAllyBits = SQAbilityFx::Heal | SQAbilityFx::Shield | SQAbilityFx::Haste | SQAbilityFx::Empower | SQAbilityFx::Fortify | SQAbilityFx::Revive;

	float SQAbTune(const USQData* D, const TCHAR* Key, float Default)
	{
		return D ? D->Tune(FName(Key), Default) : Default;
	}

	FVector SQAbGround(const UObject* Ctx, const FVector2D& P, float Lift = 5.f)
	{
		const float X = static_cast<float>(P.X);
		const float Y = static_cast<float>(P.Y);
		return FVector(X, Y, ASQArena::GroundZAt(Ctx, X, Y) + Lift);
	}

	FVector2D SQAbFlat(const FVector& V)
	{
		return FVector2D(V.X, V.Y);
	}

	float SQAbAreaScale(const USQData* D, float AreaMult)
	{
		return FMath::Pow(FMath::Max(0.1f, AreaMult), SQAbTune(D, TEXT("AbilityAreaExp"), 0.5f));
	}

	FText SQAbText(const FString& S)
	{
		return FText::FromString(S);
	}

	/** Static part of a kit slot view (0 weapon, 1 passive, 2 E, 3 R) */
	bool SQAbFillView(const USQData* D, const FSQKitDef& Kit, uint8 Slot, FSQAbilityView& Out)
	{
		Out = FSQAbilityView();
		const FSQUpgradeDef* Tome = D ? D->FindUpgrade(Kit.EvoTome) : nullptr;
		const FString TomeName = Tome ? Tome->Name : Kit.EvoTome.ToString();
		if (Slot == 0)
		{
			const FSQUpgradeDef* Weapon = D ? D->FindUpgrade(Kit.SignatureWeapon) : nullptr;
			if (!Weapon) return false;
			Out.Name = SQAbText(Weapon->Name);
			Out.Glyph = Weapon->IconGlyph;
			Out.Description = SQAbText(Weapon->Description);
			Out.Color = Weapon->Color;
			Out.bUnlocked = true;
			FString EvoName;
			for (const FSQUpgradeDef& Evo : D->Upgrades)
			{
				if (Evo.Category == ESQUpgradeCategory::Evolution && Evo.EvolvesFrom == Weapon->Id) { EvoName = Evo.Name; break; }
			}
			Out.EvolvedText = SQAbText(FString::Printf(TEXT("Signature weapon. Max level + %s (max) evolves it into %s."), *TomeName, *EvoName));
			return true;
		}
		if (Slot == 1)
		{
			Out.Name = SQAbText(Kit.PassiveName);
			Out.Glyph = Kit.PassiveGlyph;
			Out.Description = SQAbText(Kit.PassiveDesc);
			Out.bUnlocked = true;
			if (const FSQReaperDef* R = D ? D->FindReaper(Kit.ReaperId) : nullptr) Out.Color = R->Color;
			Out.EvolvedText = SQAbText(FString::Printf(TEXT("Passive. %s at max level also evolves E and R."), *TomeName));
			return true;
		}
		if (Slot != 2 && Slot != 3) return false;
		const FSQAbilityDef& A = Kit.Abilities[Slot - SQAbSlotE];
		Out.Name = SQAbText(A.Name);
		Out.Glyph = A.Glyph;
		Out.Description = SQAbText(A.Description);
		Out.Color = A.Color;
		Out.CooldownMax = A.Cooldown;
		Out.UnlockLevel = FMath::RoundToInt(Slot == SQAbSlotE ? SQAbTune(D, TEXT("AbilityUnlockE"), 3.f) : SQAbTune(D, TEXT("AbilityUnlockR"), 6.f));
		Out.EvolvedText = SQAbText(FString::Printf(TEXT("Evolves with %s (max): %s"), *TomeName, *A.EvoText));
		return true;
	}

	/** Fx flavour groups (Kits.csv <Slot>_Fx) */
	int32 SQAbFlavourGroup(FName Fx)
	{
		static const FName Rising[] = { TEXT("Pillar"), TEXT("Embers"), TEXT("Holy"), TEXT("Time") };
		static const FName Falling[] = { TEXT("Shards"), TEXT("Stamp"), TEXT("Dust"), TEXT("Paper") };
		static const FName Smoke[] = { TEXT("Smoke"), TEXT("Ghost"), TEXT("Blood"), TEXT("Crows") };
		for (const FName& N : Rising) { if (Fx == N) return 0; }
		for (const FName& N : Falling) { if (Fx == N) return 1; }
		for (const FName& N : Smoke) { if (Fx == N) return 2; }
		return 3; // Sparks, Slash, Streaks, Lightning
	}
}

// ---------------------------------------------------------------- setup

USQAbilityComponent::USQAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void USQAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, ReadyAtE, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, ReadyAtR, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, CooldownMaxE, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, CooldownMaxR, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, UltCharge, COND_OwnerOnly);
	DOREPLIFETIME(USQAbilityComponent, BuffSpeedMult);
	DOREPLIFETIME(USQAbilityComponent, Totems);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, ECounterNet, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQAbilityComponent, bRecastOpen, COND_OwnerOnly);
}

ASQCharacter* USQAbilityComponent::GetReaper() const
{
	return Cast<ASQCharacter>(GetOwner());
}

ASQGameState* USQAbilityComponent::GetGS() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGameState<ASQGameState>() : nullptr;
}

ASQHordeManager* USQAbilityComponent::GetHorde() const
{
	ASQGameState* GS = GetGS();
	return GS ? GS->Horde.Get() : nullptr;
}

float USQAbilityComponent::Now() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
}

float USQAbilityComponent::ServerNow() const
{
	const ASQGameState* GS = GetGS();
	return GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : Now();
}

const FSQKitDef* USQAbilityComponent::GetKit() const
{
	const ASQCharacter* R = GetReaper();
	const USQData* D = USQData::Get(this);
	return (R && D) ? D->FindKit(R->ReaperId) : nullptr;
}

const FSQAbilityDef* USQAbilityComponent::GetAbility(int32 Idx) const
{
	const FSQKitDef* Kit = GetKit();
	return (Kit && (Idx == 0 || Idx == 1)) ? &Kit->Abilities[Idx] : nullptr;
}

int32 USQAbilityComponent::UnlockLevelFor(int32 Idx) const
{
	const USQData* D = USQData::Get(this);
	return FMath::RoundToInt(Idx == 0 ? SQAbTune(D, TEXT("AbilityUnlockE"), 3.f) : SQAbTune(D, TEXT("AbilityUnlockR"), 6.f));
}

bool USQAbilityComponent::IsUnlocked(int32 Idx) const
{
	const ASQGameState* GS = GetGS();
	return GS && GetAbility(Idx) && GS->TeamLevel >= UnlockLevelFor(Idx);
}

float USQAbilityComponent::CooldownLeft(int32 Idx) const
{
	// Reap core: the R has no cooldown any more. Its "time left" is the missing charge in percent (0 = ready, 100 = empty), which keeps
	// every caller (TryCast, bots, the HUD sweep) working; GetUltCharge01 is the explicit 0..1 value.
	if (Idx == 1) return UltCharge >= SQAbUltReady ? 0.f : FMath::Clamp(1.f - UltCharge, 0.f, 1.f) * SQAbUltScale;
	return FMath::Max(0.f, ReadyAtE - ServerNow());
}

float USQAbilityComponent::GetUltCharge01() const
{
	return FMath::Clamp(UltCharge, 0.f, 1.f);
}

void USQAbilityComponent::AddUltCharge(float RawAmount, bool bShare)
{
	ASQCharacter* R = GetReaper();
	if (!R || !R->HasAuthority() || !(RawAmount > 0.f) || R->bDowned) return;
	const USQData* D = USQData::Get(this);
	// Every kit fills at its own R's pace: a 55 s R charges faster than a 90 s one (evolution and haste shorten it too)
	const float KitPace = SQAbTune(D, TEXT("UltRefCooldown"), 70.f) / FMath::Max(1.f, ComputeCooldown(1));
	UltCharge = FMath::Clamp(UltCharge + RawAmount * KitPace, 0.f, 1.f);
	if (!bShare) return;
	// Co-op: reaping near a teammate shares part of the charge with them
	const float ShareFrac = FMath::Clamp(SQAbTune(D, TEXT("UltShareFraction"), 0.5f), 0.f, 1.f);
	const float ShareRange = SQAbTune(D, TEXT("UltShareRange"), 600.f);
	if (ShareFrac <= 0.f || ShareRange <= 0.f) return;
	for (TActorIterator<ASQCharacter> MateIt(GetWorld()); MateIt; ++MateIt)
	{
		ASQCharacter* Mate = *MateIt;
		if (Mate == R || Mate->bDowned || !Mate->Abilities) continue;
		if (FVector::Dist2D(Mate->GetActorLocation(), R->GetActorLocation()) > ShareRange) continue;
		Mate->Abilities->AddUltCharge(RawAmount * ShareFrac, false);
	}
}

bool USQAbilityComponent::IsEvolved() const
{
	const FSQKitDef* Kit = GetKit();
	const ASQCharacter* R = GetReaper();
	const USQData* D = USQData::Get(this);
	const FSQUpgradeDef* Tome = (Kit && D) ? D->FindUpgrade(Kit->EvoTome) : nullptr;
	return Tome && R && R->GetUpgradeLevel(Kit->EvoTome) >= Tome->MaxLevel;
}

float USQAbilityComponent::ComputeCooldown(int32 Idx) const
{
	const FSQAbilityDef* A = GetAbility(Idx);
	const ASQCharacter* R = GetReaper();
	if (!A || !R) return 10.f;
	const USQData* D = USQData::Get(this);
	const float Evo = IsEvolved() ? A->EvoCooldown : 1.f;
	const float Haste = FMath::Pow(FMath::Clamp(R->Stats.CooldownMult, 0.1f, 4.f), SQAbTune(D, TEXT("AbilityHasteExp"), 0.5f));
	return FMath::Max(1.f, A->Cooldown * Evo * Haste);
}

bool USQAbilityComponent::CanFightNow() const
{
	const ASQCharacter* R = GetReaper();
	const ASQGameState* GS = GetGS();
	if (!R || !GS || R->bDowned || R->CarriedBy || GS->bPickPause || R->IsFrozenForLobby()) return false;
	return GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;
}

FVector USQAbilityComponent::ClampAim(int32 Idx, const FVector& AimPoint) const
{
	const ASQCharacter* R = GetReaper();
	const FSQAbilityDef* A = GetAbility(Idx);
	if (!R || !A) return AimPoint;
	const FVector2D Me = R->Pos2D();
	FVector2D Aim = SQAbFlat(AimPoint);
	if (Aim.ContainsNaN()) Aim = Me;
	const bool bSelfShape = A->Shape == ESQAbilityShape::Nova || A->Shape == ESQAbilityShape::Self || A->Shape == ESQAbilityShape::Global;
	if (bSelfShape) return SQAbGround(this, Me + FVector2D(R->GetActorForwardVector().X, R->GetActorForwardVector().Y) * 100.f);
	const float Range = FMath::Max(100.f, A->Range);
	const FVector2D Delta = Aim - Me;
	const double Dist = Delta.Size();
	// Directional shapes keep the direction even with the cursor on the reaper
	const bool bDirectional = A->Shape == ESQAbilityShape::Cone || A->Shape == ESQAbilityShape::Line || A->Shape == ESQAbilityShape::Volley;
	if (bDirectional && Dist < 1.0) Aim = Me + FVector2D(R->GetActorForwardVector().X, R->GetActorForwardVector().Y) * 100.f;
	else if (Dist > Range) Aim = Me + Delta / Dist * Range;
	return SQAbGround(this, Aim);
}

void USQAbilityComponent::ResetForRun()
{
	Pending.Reset();
	PendingAdds.Reset();
	Buffs.Reset();
	Totems.Reset();
	PendingPops.Reset();
	ReadyAtE = 0.f;
	ReadyAtR = 0.f;
	CooldownMaxE = 0.f;
	CooldownMaxR = 0.f;
	UltCharge = 0.f;
	BuffSpeedMult = 1.f;
	KillCounter = 0;
	CastCounter = 0;
	PassiveTimer = 0.f;
	PassiveFxTimer = 0.f;
	PassiveAnimTimer = 0.f;
	PassiveStep = 0;
	CarriedSeen = 0;
	MarkedEnemyId = 0;
	CooldownCutThisCycle = 0.f;
	FlockCrows = 0.f;
	FlockDecay = 0.f;
	FlockTick = 0.f;
	ScorchDropTime = 0.f;
	ScorchDropTimer = 0.f;
	ScorchDamageTimer = 0.f;
	ScorchPatches.Reset();
	ReviveTarget.Reset();
	ReviveChannel = 0.f;
	AimingSlot = 255;
	ECharges = -1;
	NextChargeAt = 0.f;
	RecastUntil = 0.f;
	RecastCooldownAt = 0.f;
	EStacks = 0;
	EStackUntil = 0.f;
	ECounterNet = 0;
	bRecastOpen = false;
}

// ---------------------------------------------------------------- tick

void USQAbilityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ASQCharacter* R = GetReaper();
	if (!R) return;
	LocalCastGate = FMath::Max(0.f, LocalCastGate - DeltaTime);
	TickTrait(DeltaTime);

	if (R->HasAuthority())
	{
		TickBuffs();
		TickEMods();
		const ASQGameState* GS = GetGS();
		const bool bFightPhase = GS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss);
		if (!bFightPhase)
		{
			if (Pending.Num() > 0 || PendingAdds.Num() > 0) { Pending.Reset(); PendingAdds.Reset(); RebuildTotemNet(); }
		}
		else if (!GS->bPickPause)
		{
			if (CanFightNow())
			{
				TickPassive(DeltaTime);
				TickBot(DeltaTime);
			}
			TickPending(DeltaTime);
		}
		FlushPops(DeltaTime);
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		TickIndicator(DeltaTime);
		TickLocalFx(DeltaTime);
	}
}

// ---------------------------------------------------------------- input + casting

void USQAbilityComponent::TryCast(uint8 Slot, const FVector& AimPoint)
{
	ASQCharacter* R = GetReaper();
	if (!R || (Slot != 2 && Slot != 3) || AimPoint.ContainsNaN()) return;
	const int32 Idx = Slot - SQAbSlotE;
	const USQData* D = USQData::Get(this);
	const bool bQuick = SQAbTune(D, TEXT("QuickCast"), 1.f) >= 0.5f;
	if (!bQuick)
	{
		if (AimingSlot != Slot)
		{
			AimingSlot = Slot; // press: start aiming (the indicator follows the cursor)
			LastAim = AimPoint;
			return;
		}
		AimingSlot = 255;      // release / second press: cast
	}
	if (!CanFightNow() || !IsUnlocked(Idx) || CooldownLeft(Idx) > 0.1f || LocalCastGate > 0.f) return;
	LocalCastGate = SQAbTune(D, TEXT("AbilityMinCastGap"), 0.25f);
	const FVector Clamped = ClampAim(Idx, AimPoint);
	QuickFlash = 0.3f;
	QuickFlashSlot = Slot;
	QuickFlashAim = Clamped;
	ServerCast(Slot, FVector_NetQuantize(Clamped));
}

void USQAbilityComponent::CancelAim()
{
	AimingSlot = 255;
}

void USQAbilityComponent::ServerCast_Implementation(uint8 Slot, FVector_NetQuantize AimPoint)
{
	if (Slot != 2 && Slot != 3) return;
	const FVector Aim(AimPoint.X, AimPoint.Y, AimPoint.Z);
	if (Aim.ContainsNaN()) return;
	CommitCast(Slot - SQAbSlotE, Aim);
}

bool USQAbilityComponent::CommitCast(int32 Idx, const FVector& AimPoint)
{
	ASQCharacter* R = GetReaper();
	if (!R || !R->HasAuthority() || !CanFightNow() || !IsUnlocked(Idx) || !GetHorde()) return false;
	const USQData* D = USQData::Get(this);
	// Character pass: E mods (Charges / Recast / Stacks)
	const FSQAbilityDef* ModDef = GetAbility(Idx);
	const FName EMod = (Idx == 0 && ModDef) ? ModDef->Mod : NAME_None;
	if (EMod == TEXT("Recast") && RecastUntil > ServerNow())
	{
		DoRecast();
		return true;
	}
	TickEMods();
	const float Slack = SQAbTune(D, TEXT("AbilityCooldownSlack"), 0.2f);
	if (EMod == TEXT("Charges"))
	{
		if (ECharges <= 0 || CooldownLeft(Idx) > Slack) return false;
	}
	// A little slack for the owner's view of the replicated cooldown. The R (reap core) needs a full charge instead.
	else if (Idx == 0 && CooldownLeft(Idx) > Slack) return false;
	if (Idx == 1 && UltCharge < SQAbUltReady) return false;
	const float Cd = ComputeCooldown(Idx);
	float Strength = 1.f;
	if (Idx == 0)
	{
		const float NowS = ServerNow();
		ReadyAtE = NowS + Cd;
		CooldownMaxE = Cd;
		CooldownCutThisCycle = 0.f;
		if (EMod == TEXT("Charges"))
		{
			if (ECharges >= FMath::Max(1, FMath::RoundToInt(ModDef->ModValue))) NextChargeAt = NowS + Cd; // the first spent charge starts recharging
			ECharges--;
			// Charges left: only a short lockout between casts
			ReadyAtE = ECharges > 0 ? NowS + FMath::Max(0.2f, SQAbTune(D, TEXT("ChargeLockout"), 0.35f)) : NextChargeAt;
			ECounterNet = static_cast<uint8>(FMath::Max(0, ECharges));
		}
		else if (EMod == TEXT("Recast"))
		{
			ASQCharacter* Self = GetReaper();
			RecastPos = Self ? Self->Pos2D() : FVector2D::ZeroVector;
			RecastAim = AimPoint;
			RecastUntil = NowS + FMath::Max(0.5f, ModDef->ModTime);
			RecastCooldownAt = NowS + Cd;
			ReadyAtE = NowS + FMath::Max(0.2f, SQAbTune(D, TEXT("RecastLockout"), 0.3f));
			bRecastOpen = true;
		}
		else if (EMod == TEXT("Stacks"))
		{
			EStacks = NowS <= EStackUntil ? FMath::Min(EStacks + 1, 3) : 0;
			EStackUntil = NowS + FMath::Max(0.5f, ModDef->ModTime);
			Strength = 1.f + ModDef->ModValue * static_cast<float>(EStacks);
			ECounterNet = static_cast<uint8>(EStacks);
		}
	}
	else
	{
		UltCharge = 0.f;
		CooldownMaxR = SQAbUltScale;
	}
	ExecuteCast(Idx, ClampAim(Idx, AimPoint), false, Strength);
	USQAugments::NotifyCast(R, static_cast<uint8>(Idx + SQAbSlotE));

	// The Intern (Overeager): every Nth cast echoes
	const FSQKitDef* Kit = GetKit();
	if (Kit && Kit->PassiveId == TEXT("OnTheJob"))
	{
		CastCounter++;
		const int32 EchoEvery = FMath::Max(1, FMath::RoundToInt(Kit->P[0]));
		if (CastCounter % EchoEvery == 0)
		{
			FPending Echo;
			Echo.Kind = EPendingKind::Echo;
			Echo.Cast.Idx = Idx;
			Echo.Pos = SQAbFlat(ClampAim(Idx, AimPoint));
			Echo.TimeLeft = FMath::Max(0.1f, Kit->P[1]);
			Echo.TotemDamage = FMath::Clamp(Kit->P[2], 0.1f, 1.f); // echo strength
			Pending.Add(Echo);
			QueuePassiveAnim(0);
		}
	}
	return true;
}

bool USQAbilityComponent::BuildCast(int32 Idx, const FVector& AimPoint, bool bEcho, float Strength, FCast& Out) const
{
	const ASQCharacter* R = GetReaper();
	const FSQAbilityDef* A = GetAbility(Idx);
	const ASQGameState* GS = GetGS();
	const USQData* D = USQData::Get(this);
	if (!R || !A || !GS) return false;

	Out = FCast();
	Out.Idx = Idx;
	Out.bEcho = bEcho;
	Out.bEvo = IsEvolved();
	Out.Origin = R->Pos2D();
	Out.Target = SQAbFlat(AimPoint);
	Out.Dir = (Out.Target - Out.Origin).GetSafeNormal();
	if (Out.Dir.IsNearlyZero()) Out.Dir = FVector2D(R->GetActorForwardVector().X, R->GetActorForwardVector().Y).GetSafeNormal();
	if (Out.Dir.IsNearlyZero()) Out.Dir = FVector2D(1.f, 0.f);

	const float LevelScale = 1.f + SQAbTune(D, TEXT("AbilityDamagePerLevel"), 0.05f) * static_cast<float>(FMath::Max(0, GS->TeamLevel - 1));
	Out.Crit = FMath::FRand() < R->Stats.CritChance ? 1.f + FMath::Max(0.f, R->Stats.CritDamage) : 1.f;
	const float DamageMult = R->Stats.DamageMult * GetOutgoingDamageMult();
	const float EvoDmg = Out.bEvo ? A->EvoDamage : 1.f;
	Out.Damage = A->Damage * EvoDmg * LevelScale * DamageMult * Out.Crit * Strength;
	Out.Final = A->FinalDamage * EvoDmg * LevelScale * DamageMult * Out.Crit * Strength;
	const float EvoRad = Out.bEvo ? A->EvoRadius : 1.f;
	Out.Radius = A->Radius * EvoRad * SQAbAreaScale(D, R->Stats.AreaMult);
	const bool bWidthScales = A->Shape == ESQAbilityShape::Line || A->Shape == ESQAbilityShape::Dash || A->Shape == ESQAbilityShape::Cone;
	Out.Width = A->Width * (bWidthScales ? EvoRad : 1.f);
	if (A->Shape == ESQAbilityShape::Cone) Out.Width = FMath::Clamp(Out.Width, 5.f, 340.f);
	Out.Range = A->Range;
	Out.Count = FMath::Clamp(A->Count + (Out.bEvo ? A->EvoCount : 0), 0, 64);
	Out.Pierce = A->Pierce;
	Out.Duration = A->Duration * (Out.bEvo ? A->EvoDuration : 1.f) * R->Stats.Duration;
	Out.Tick = FMath::Max(0.1f, A->Tick);
	Out.Effects = A->Effects;
	Out.EffectValue = A->EffectValue * EvoDmg;
	Out.EffectTime = A->EffectTime * R->Stats.Duration;
	Out.EvoEffects = Out.bEvo ? A->EvoEffects : 0u;
	Out.EvoValue = A->EvoValue;
	Out.EvoTime = A->EvoTime * R->Stats.Duration;
	Out.Delay = A->Delay;
	Out.Color = A->Color.ToFColor(true);
	return true;
}

void USQAbilityComponent::ExecuteCast(int32 Idx, const FVector& AimPoint, bool bEcho, float Strength)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	const FSQAbilityDef* A = GetAbility(Idx);
	FCast C;
	if (!R || !Horde || !A || !BuildCast(Idx, AimPoint, bEcho, Strength, C)) return;

	// Face the aim and tell every machine (anim + effect)
	if (!bEcho && A->Shape != ESQAbilityShape::Self && A->Shape != ESQAbilityShape::Global && A->Shape != ESQAbilityShape::Nova)
	{
		const float Yaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(C.Dir.Y, C.Dir.X)));
		R->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}
	uint8 Flags = 0;
	if (C.bEvo) Flags |= 1;
	if (bEcho) Flags |= 2;
	if (Idx == 1) Flags |= 4;
	MulticastCast(static_cast<uint8>(Idx + SQAbSlotE), FVector_NetQuantize(SQAbGround(this, C.Origin)), FVector_NetQuantize(SQAbGround(this, C.Target)), Flags);

	const float SlowTime = (C.Effects & SQAbilityFx::Slow) ? C.EffectTime : 0.f;
	const USQData* D = USQData::Get(this);
	switch (A->Shape)
	{
	case ESQAbilityShape::Circle:
	case ESQAbilityShape::Nova:
	case ESQAbilityShape::Global:
		if (A->Mod == TEXT("Trap") && A->Shape == ESQAbilityShape::Circle)
		{
			// Character pass: armed at the target, goes off when an enemy comes near (QueueModFollowUps arms it)
			break;
		}
		if (C.Delay > 0.f)
		{
			// Pulls start at once: the crowd gathers during the wind-up
			if (C.Effects & SQAbilityFx::Pull)
			{
				const FVector2D PullAt = A->Shape == ESQAbilityShape::Circle ? C.Target : C.Origin;
				Horde->ApplyPull(PullAt, C.Radius * 1.2f, SQAbTune(D, TEXT("AbilityPullStrength"), 1100.f), SQAbTune(D, TEXT("AbilityKnockMaxRadius"), 75.f));
			}
			FPending Wait;
			Wait.Kind = EPendingKind::Impact;
			Wait.Cast = C;
			Wait.TimeLeft = C.Delay;
			Pending.Add(Wait);
		}
		else
		{
			Impact(C);
		}
		break;

	case ESQAbilityShape::Cone:
	{
		const int32 Hits = Horde->ApplyConeDamage(C.Origin, C.Dir, C.Range, C.Width * 0.5f, C.Damage, R, SlowTime);
		const FVector2D Mid = C.Origin + C.Dir * (C.Range * 0.5f);
		ApplyControl(C, Mid, C.Range * 0.55f, C.Effects, C.EffectTime, C.EffectValue, C.Dir);
		ApplyEvo(C, Mid, C.Range * 0.55f, C.Dir);
		if (Hits > 0) AddAreaPops(Mid, C.Range * 0.5f, C.Damage, C.Crit, 6);
		break;
	}

	case ESQAbilityShape::Line:
		if (C.Duration > 0.f)
		{
			FPending Beam;
			Beam.Kind = EPendingKind::Beam;
			Beam.Cast = C;
			Beam.Life = C.Duration;
			Beam.TickLeft = 0.f;
			Pending.Add(Beam);
		}
		else
		{
			LineHit(C, C.Origin);
		}
		break;

	case ESQAbilityShape::Dash:
	{
		const FVector2D From = C.Origin;
		const FVector2D To = C.Target;
		if (!bEcho) MoveOwner(To);
		const int32 Hits = Horde->ApplyLineDamage(From, To, C.Width, C.Damage, R, SlowTime);
		if (Hits > 0) AddAreaPops((From + To) * 0.5f, static_cast<float>(FVector2D::Distance(From, To)) * 0.5f, C.Damage, C.Crit, 5);
		if ((C.Effects & SQAbilityFx::Lifesteal) && Hits > 0)
		{
			const float Heal = FMath::Min(0.25f, C.EffectValue * static_cast<float>(Hits)) * R->Stats.MaxHealth;
			R->Health = FMath::Min(R->Stats.MaxHealth, R->Health + Heal);
			MulticastAbilityHit(2, FVector_NetQuantize(R->GetActorLocation()), 120.f, FColor(255, 120, 150), 0.3f);
		}
		const float EndRadius = C.Radius > 0.f ? C.Radius : C.Width * 1.5f;
		HitCircle(C, To, EndRadius, C.Final, C.Effects & ~(SQAbilityFx::Lifesteal | SQAbAllyBits), C.EffectTime, C.EffectValue, C.Dir);
		ApplyEvo(C, To, EndRadius, C.Dir);
		break;
	}

	case ESQAbilityShape::Blink:
	{
		const FVector2D From = C.Origin;
		if (!bEcho) MoveOwner(C.Target);
		HitCircle(C, From, C.Radius, C.Damage, C.Effects, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
		HitCircle(C, C.Target, C.Radius, C.Damage, C.Effects, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
		ApplyEvo(C, C.Target, C.Radius, FVector2D::ZeroVector);
		break;
	}

	case ESQAbilityShape::Zone:
	{
		FPending Zone;
		Zone.Kind = EPendingKind::Zone;
		Zone.Cast = C;
		Zone.Pos = C.Target;
		Zone.Life = FMath::Max(0.2f, C.Duration);
		Zone.TickLeft = 0.f;
		Pending.Add(Zone);
		break;
	}

	case ESQAbilityShape::Volley:
		FireVolley(C, C.Origin);
		if (C.Duration > 0.f)
		{
			FPending Wave;
			Wave.Kind = EPendingKind::Wave;
			Wave.Cast = C;
			Wave.Life = C.Duration;
			Wave.TickLeft = C.Tick;
			Pending.Add(Wave);
		}
		break;

	case ESQAbilityShape::Barrage:
	{
		const int32 Strikes = FMath::Clamp(C.Count, 1, 40);
		for (int32 StrikeIdx = 0; StrikeIdx < Strikes; ++StrikeIdx)
		{
			FPending Drop;
			Drop.Kind = EPendingKind::Drop;
			Drop.Cast = C;
			Drop.Pos = C.Target + FMath::RandPointInCircle(FMath::Max(10.f, C.Width));
			Drop.TimeLeft = C.Duration * static_cast<float>(StrikeIdx) / static_cast<float>(Strikes) + FMath::Max(0.15f, C.Delay);
			MulticastAbilityHit(1, FVector_NetQuantize(SQAbGround(this, Drop.Pos)), C.Radius, C.Color, Drop.TimeLeft);
			Pending.Add(Drop);
		}
		break;
	}

	case ESQAbilityShape::Chain:
	{
		TArray<FVector2D> Links;
		Horde->ApplyChainDamage(C.Target, FMath::Max(1, C.Count), FMath::Max(100.f, C.Radius), FMath::Max(100.f, C.Width), C.Damage, R, SlowTime, Links);
		if (Links.Num() >= 2)
		{
			TArray<FVector_NetQuantize> Points;
			for (int32 LinkIdx = 0; LinkIdx < Links.Num() && Points.Num() < 24; ++LinkIdx)
			{
				Points.Add(FVector_NetQuantize(SQAbGround(this, Links[LinkIdx], 60.f)));
				if (LinkIdx > 0) AddPop(Links[LinkIdx], C.Damage, C.Crit);
			}
			MulticastAbilityLines(Points, C.Color);
			ApplyControl(C, Links.Last(), 150.f, C.Effects & ~SQAbilityFx::Slow, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
		}
		break;
	}

	case ESQAbilityShape::Self:
	{
		// Personal Empower / Fortify; Haste and evolved effects also reach reapers within Radius
		const float BuffTime = C.Duration > 0.f ? C.Duration : C.EffectTime;
		if (C.Effects & SQAbilityFx::Empower) ReceiveBuff(SQAbilityFx::Empower, C.EffectValue, BuffTime);
		if (C.Effects & SQAbilityFx::Fortify) ReceiveBuff(SQAbilityFx::Fortify, C.EffectValue, BuffTime);
		if (C.Effects & SQAbilityFx::Haste) ApplyAllyEffects(C.Origin, C.Radius, SQAbilityFx::Haste, C.EffectValue, BuffTime, true);
		// Character pass: a shield for the crew around him (Closed Casket)
		if (C.Effects & SQAbilityFx::Shield) ApplyAllyEffects(C.Origin, C.Radius, SQAbilityFx::Shield, C.EffectValue, BuffTime, true);
		if (C.EvoEffects) ApplyAllyEffects(C.Origin, C.Radius, C.EvoEffects & SQAbAllyBits, C.EvoValue, C.EvoTime > 0.f ? C.EvoTime : BuffTime, true);
		FPending Aura;
		Aura.Kind = EPendingKind::Aura;
		Aura.Cast = C;
		Aura.Life = FMath::Max(0.2f, C.Duration);
		Aura.TickLeft = 0.f;
		Pending.Add(Aura);
		break;
	}

	case ESQAbilityShape::Totem:
		SpawnTotem(C.Target, 0, FMath::Max(1.f, C.Duration), C.Damage, C.Tick, FMath::Max(300.f, C.Width), FMath::Max(1, C.Count), 1, &C, C.Color);
		break;

	default:
		break;
	}
	if (!bEcho && !A->Mod.IsNone()) QueueModFollowUps(C, *A);
}

// ---------------------------------------------------------------- character pass: ability mods (server)

void USQAbilityComponent::TickEMods()
{
	const FSQAbilityDef* A = GetAbility(0);
	const FName Mod = A ? A->Mod : NAME_None;
	const float NowS = ServerNow();
	if (Mod == TEXT("Charges"))
	{
		const int32 MaxCharges = FMath::Max(1, FMath::RoundToInt(A->ModValue));
		if (ECharges < 0 || ECharges > MaxCharges) { ECharges = MaxCharges; NextChargeAt = 0.f; }
		while (ECharges < MaxCharges && NextChargeAt > 0.f && NowS >= NextChargeAt)
		{
			ECharges++;
			const float Cd = ComputeCooldown(0);
			NextChargeAt = ECharges < MaxCharges ? NextChargeAt + Cd : 0.f;
			if (ECharges == 1) ReadyAtE = FMath::Min(ReadyAtE, NowS); // back from empty: castable again
			if (ECharges < MaxCharges) CooldownMaxE = Cd;
		}
		ECounterNet = static_cast<uint8>(FMath::Max(0, ECharges));
	}
	else if (Mod == TEXT("Recast"))
	{
		if (RecastUntil > 0.f && NowS > RecastUntil)
		{
			// Window missed: the cooldown that started with the first cast carries on
			RecastUntil = 0.f;
			ReadyAtE = FMath::Max(ReadyAtE, RecastCooldownAt);
			bRecastOpen = false;
		}
	}
	else if (Mod == TEXT("Stacks"))
	{
		if (EStacks > 0 && NowS > EStackUntil) { EStacks = 0; ECounterNet = 0; }
	}
}

void USQAbilityComponent::DoRecast()
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	const FSQAbilityDef* A = GetAbility(0);
	if (!R || !Horde || !A) return;
	const FVector2D From = R->Pos2D();
	RecastUntil = 0.f;
	bRecastOpen = false;
	ReadyAtE = RecastCooldownAt;
	FCast C;
	if (!BuildCast(0, SQAbGround(this, RecastPos), false, FMath::Max(0.1f, A->ModValue), C)) return;
	C.Origin = From;
	C.Target = RecastPos;
	MoveOwner(RecastPos);
	MulticastCast(SQAbSlotE, FVector_NetQuantize(SQAbGround(this, From)), FVector_NetQuantize(SQAbGround(this, RecastPos)), C.bEvo ? 1 : 0);
	const float Radius = FMath::Max(C.Radius, 220.f);
	const int32 Hits = HitCircle(C, RecastPos, Radius, C.Damage, C.Effects & ~SQAbAllyBits, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
	// the way back hurts too
	Horde->ApplyLineDamage(From, RecastPos, FMath::Max(60.f, C.Width), C.Damage * 0.5f, R, (C.Effects & SQAbilityFx::Slow) ? C.EffectTime : 0.f);
	if (Hits > 0) AddAreaPops(RecastPos, Radius, C.Damage, C.Crit, 5);
	MulticastAbilityHit(3, FVector_NetQuantize(SQAbGround(this, RecastPos)), Radius, C.Color, 0.4f);
}

void USQAbilityComponent::QueueModFollowUps(const FCast& C, const FSQAbilityDef& A)
{
	if (A.Mod == TEXT("Trap") && A.Shape == ESQAbilityShape::Circle)
	{
		const int32 MaxArmed = FMath::Max(1, FMath::RoundToInt(A.ModValue));
		int32 Armed = 0;
		int32 Oldest = INDEX_NONE;
		for (int32 PendIdx = 0; PendIdx < Pending.Num(); ++PendIdx)
		{
			if (Pending[PendIdx].Kind == EPendingKind::Trap && Pending[PendIdx].Cast.Idx == C.Idx)
			{
				if (Oldest == INDEX_NONE) Oldest = PendIdx;
				Armed++;
			}
		}
		if (Armed >= MaxArmed && Oldest != INDEX_NONE) Pending[Oldest].Life = 0.f; // the oldest goes off now
		FPending Trap;
		Trap.Kind = EPendingKind::Trap;
		Trap.Cast = C;
		Trap.Pos = C.Target;
		Trap.Life = FMath::Max(1.f, A.ModTime);
		Trap.TimeLeft = 0.4f; // arming time
		Pending.Add(Trap);
		MulticastAbilityHit(1, FVector_NetQuantize(SQAbGround(this, C.Target)), C.Radius * 0.55f, C.Color, Trap.Life);
	}
	else if (A.Mod == TEXT("Detonate"))
	{
		const int32 Bursts = FMath::Clamp(FMath::RoundToInt(A.ModValue), 1, 6);
		const float Gap = FMath::Max(0.2f, A.ModTime);
		for (int32 BurstIdx = 0; BurstIdx < Bursts; ++BurstIdx)
		{
			FPending Burst;
			Burst.Kind = EPendingKind::Detonate;
			Burst.Cast = C;
			Burst.Pos = C.Target;
			Burst.TimeLeft = Gap * static_cast<float>(BurstIdx + 1);
			Pending.Add(Burst);
		}
		MulticastAbilityHit(1, FVector_NetQuantize(SQAbGround(this, C.Target)), C.Radius > 0.f ? C.Radius : 250.f, C.Color, Gap);
	}
}

void USQAbilityComponent::Impact(const FCast& C)
{
	ASQCharacter* R = GetReaper();
	if (!R) return;
	const FSQAbilityDef* A = GetAbility(C.Idx);
	if (!A) return;
	const float BuffTime = C.Duration > 0.f ? C.Duration : C.EffectTime;
	if (A->Shape == ESQAbilityShape::Global)
	{
		// Every reaper on the map: revive / heal, and a blast around each one standing
		ApplyAllyEffects(C.Origin, -1.f, C.Effects, C.EffectValue, BuffTime, true);
		ApplyAllyEffects(C.Origin, -1.f, C.EvoEffects, C.EvoValue, C.EvoTime > 0.f ? C.EvoTime : BuffTime, true);
		const uint32 EnemyBits = C.Effects & ~SQAbAllyBits;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Ally = *It;
			if (!Ally || Ally->bDowned) continue;
			HitCircle(C, Ally->Pos2D(), C.Radius, C.Damage, EnemyBits, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
			MulticastAbilityHit(3, FVector_NetQuantize(SQAbGround(this, Ally->Pos2D())), C.Radius, C.Color, 0.5f);
		}
		return;
	}
	const FVector2D Center = A->Shape == ESQAbilityShape::Circle ? C.Target : R->Pos2D();
	// The pull already ran at the cast for delayed impacts
	const uint32 EnemyBits = C.Effects & ~SQAbAllyBits & ~(C.Delay > 0.f ? SQAbilityFx::Pull : 0u);
	HitCircle(C, Center, C.Radius, C.Damage, EnemyBits, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
	ApplyAllyEffects(Center, C.Radius, C.Effects, C.EffectValue, BuffTime, true);
	ApplyEvo(C, Center, C.Radius, FVector2D::ZeroVector);
}

int32 USQAbilityComponent::HitCircle(const FCast& C, const FVector2D& Center, float Radius, float Damage, uint32 Effects, float EffTime, float EffValue, const FVector2D& KnockDir)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !Horde || Radius <= 0.f) return 0;
	const float Slow = (Effects & SQAbilityFx::Slow) ? EffTime : 0.f;
	int32 Hits = 0;
	if (Damage > 0.f || Slow > 0.f)
	{
		const FSQPrimeScope PrimeScope(Horde); // co-op combos: what an E / R hits is primed for a teammate to burst
		Hits = Horde->ApplyAreaDamage(Center, Radius, Damage, R, Slow);
	}
	ApplyControl(C, Center, Radius, Effects, EffTime, EffValue, KnockDir);
	if (Damage > 0.f && Hits > 0) AddAreaPops(Center, Radius, Damage, C.Crit, 6);
	if ((Effects & SQAbilityFx::Lifesteal) && Hits > 0)
	{
		R->Health = FMath::Min(R->Stats.MaxHealth, R->Health + FMath::Min(0.25f, EffValue * static_cast<float>(Hits)) * R->Stats.MaxHealth);
	}
	return Hits;
}

void USQAbilityComponent::ApplyControl(const FCast& C, const FVector2D& Center, float Radius, uint32 Effects, float EffTime, float EffValue, const FVector2D& KnockDir)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !Horde || Radius <= 0.f) return;
	const USQData* D = USQData::Get(this);
	const float MaxBody = SQAbTune(D, TEXT("AbilityKnockMaxRadius"), 75.f);
	if (Effects & SQAbilityFx::Pull) Horde->ApplyPull(Center, Radius, SQAbTune(D, TEXT("AbilityPullStrength"), 1100.f), MaxBody);
	if (Effects & SQAbilityFx::Knockback) Horde->ApplyKnockback(Center, Radius, KnockDir, SQAbTune(D, TEXT("AbilityKnockback"), 1600.f), MaxBody);
	if ((Effects & SQAbilityFx::Freeze) && EffTime > 0.f) Horde->FreezeArea(Center, Radius, EffTime);
	if ((Effects & SQAbilityFx::Mark) && EffValue > 0.f)
	{
		uint16 MarkId = 0;
		FVector2D MarkPos = FVector2D::ZeroVector;
		if (Horde->MarkToughestEnemy(Center, Radius, EffValue, FMath::Max(0.5f, EffTime), MarkId, MarkPos))
		{
			R->MulticastFX(1, SQAbGround(this, MarkPos), 140.f, C.Color, FMath::Max(0.5f, EffTime));
		}
	}
}

void USQAbilityComponent::ApplyEvo(const FCast& C, const FVector2D& Center, float Radius, const FVector2D& KnockDir)
{
	if (!C.EvoEffects || Radius <= 0.f) return;
	const uint32 EnemyBits = C.EvoEffects & ~SQAbAllyBits;
	if (EnemyBits) HitCircle(C, Center, Radius, 0.f, EnemyBits, C.EvoTime, C.EvoValue, KnockDir);
	if (C.EvoEffects & SQAbAllyBits) ApplyAllyEffects(Center, FMath::Max(Radius, 600.f), C.EvoEffects, C.EvoValue, C.EvoTime > 0.f ? C.EvoTime : 5.f, true);
}

void USQAbilityComponent::ApplyAllyEffects(const FVector2D& Center, float Radius, uint32 Effects, float Value, float Time, bool bIncludeSelf)
{
	const uint32 Bits = Effects & SQAbAllyBits;
	ASQCharacter* Me = GetReaper();
	if (!Bits || !Me) return;
	const float BuffTime = Time > 0.f ? Time : 4.f;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* Ally = *It;
		if (!Ally || (!bIncludeSelf && Ally == Me)) continue;
		if (Radius >= 0.f && FVector2D::Distance(Ally->Pos2D(), Center) > Radius) continue;
		USQAbilityComponent* AllyKit = Ally->Abilities;
		if (Ally->bDowned)
		{
			if (Bits & SQAbilityFx::Revive)
			{
				Ally->Revive(FMath::Clamp(Value, 0.1f, 1.f));
				MulticastAbilityHit(3, FVector_NetQuantize(Ally->GetActorLocation() - FVector(0.f, 0.f, 85.f)), 260.f, FColor(255, 245, 200), 0.6f);
			}
			continue;
		}
		const float AllyMax = FMath::Max(1.f, Ally->Stats.MaxHealth);
		if (Bits & SQAbilityFx::Heal) Ally->Health = FMath::Min(AllyMax, Ally->Health + Value * AllyMax);
		if ((Bits & SQAbilityFx::Shield) && AllyKit)
		{
			const float Amount = FMath::Max(0.f, Value) * AllyMax;
			const float Before = Ally->Shield;
			Ally->Shield = FMath::Min(AllyMax * 0.6f, Ally->Shield + Amount);
			AllyKit->ReceiveBuff(SQAbilityFx::Shield, Value, BuffTime, Ally->Shield - Before);
		}
		if (AllyKit)
		{
			if (Bits & SQAbilityFx::Haste) AllyKit->ReceiveBuff(SQAbilityFx::Haste, Value, BuffTime);
			if (Bits & SQAbilityFx::Empower) AllyKit->ReceiveBuff(SQAbilityFx::Empower, Value, BuffTime);
			if (Bits & SQAbilityFx::Fortify) AllyKit->ReceiveBuff(SQAbilityFx::Fortify, Value, BuffTime);
		}
		if (Bits & (SQAbilityFx::Heal | SQAbilityFx::Shield | SQAbilityFx::Empower | SQAbilityFx::Fortify | SQAbilityFx::Haste))
		{
			const FColor BuffColor = (Bits & (SQAbilityFx::Heal | SQAbilityFx::Shield)) ? FColor(150, 255, 170) : FColor(255, 220, 120);
			MulticastAbilityHit(2, FVector_NetQuantize(Ally->GetActorLocation()), 120.f, BuffColor, 0.4f);
		}
	}
}

void USQAbilityComponent::LineHit(const FCast& C, const FVector2D& From)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !Horde) return;
	const USQData* D = USQData::Get(this);
	const FVector2D End = From + C.Dir * C.Range;
	const float Slow = (C.Effects & SQAbilityFx::Slow) ? C.EffectTime : 0.f;
	int32 Hits = 0;
	{
		const FSQPrimeScope PrimeScope(Horde); // co-op combos
		Hits = Horde->ApplyLineDamage(From, End, FMath::Max(20.f, C.Width), C.Damage, R, Slow);
	}
	const float MaxBody = SQAbTune(D, TEXT("AbilityKnockMaxRadius"), 75.f);
	// Circles along the line for the control effects (Pull drags back toward the caster)
	for (int32 SegIdx = 1; SegIdx <= 3; ++SegIdx)
	{
		const FVector2D At = From + C.Dir * (C.Range * static_cast<float>(SegIdx) / 4.f);
		const float SegRadius = C.Range / 6.f + C.Width;
		if (C.Effects & SQAbilityFx::Pull) Horde->ApplyKnockback(At, SegRadius, -C.Dir, SQAbTune(D, TEXT("AbilityPullStrength"), 1100.f), MaxBody);
		if (C.Effects & SQAbilityFx::Knockback) Horde->ApplyKnockback(At, SegRadius, C.Dir, SQAbTune(D, TEXT("AbilityKnockback"), 1600.f), MaxBody);
		if ((C.Effects & SQAbilityFx::Freeze) && C.EffectTime > 0.f) Horde->FreezeArea(At, SegRadius, C.EffectTime);
		if (C.EvoEffects) ApplyEvo(C, At, SegRadius, C.Dir);
	}
	if (C.Effects & SQAbilityFx::Mark)
	{
		ApplyControl(C, From + C.Dir * (C.Range * 0.5f), C.Range * 0.5f, SQAbilityFx::Mark, C.EffectTime, C.EffectValue, C.Dir);
	}
	if (Hits > 0)
	{
		TArray<FVector2D> Near;
		Horde->FindNearestTargets(From + C.Dir * (C.Range * 0.5f), 12, C.Range * 0.5f, Near);
		int32 Popped = 0;
		for (const FVector2D& Spot : Near)
		{
			const FVector2D Rel = Spot - From;
			const double Along = FVector2D::DotProduct(Rel, C.Dir);
			const double Across = FMath::Abs(Rel.X * C.Dir.Y - Rel.Y * C.Dir.X);
			if (Along < 0.0 || Across > C.Width + 60.f) continue;
			AddPop(Spot, C.Damage, C.Crit);
			if (++Popped >= 6) break;
		}
	}
}

void USQAbilityComponent::FireVolley(const FCast& C, const FVector2D& From)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !Horde) return;
	const USQData* D = USQData::Get(this);
	const int32 Shots = FMath::Clamp(C.Count, 1, 48);
	const float Speed = FMath::Max(400.f, SQAbTune(D, TEXT("AbilityVolleySpeed"), 1900.f));
	const float Life = FMath::Max(0.2f, FMath::Max(300.f, C.Range) / Speed);
	const bool bRing = C.Width >= 359.f;
	const float BaseYaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(C.Dir.Y, C.Dir.X))) + (bRing ? FMath::FRandRange(0.f, 360.f / Shots) : 0.f);
	for (int32 ShotIdx = 0; ShotIdx < Shots; ++ShotIdx)
	{
		float Offset = 0.f;
		if (bRing) Offset = 360.f * static_cast<float>(ShotIdx) / static_cast<float>(Shots);
		else if (Shots > 1) Offset = -C.Width * 0.5f + C.Width * static_cast<float>(ShotIdx) / static_cast<float>(Shots - 1);
		const float Rad = FMath::DegreesToRadians(BaseYaw + Offset);
		const FVector2D ShotDir(FMath::Cos(Rad), FMath::Sin(Rad));
		Horde->SpawnProjectile(From + ShotDir * 50.f, ShotDir * Speed, C.Damage, FMath::Max(20.f, C.Radius), Life, FMath::Max(1, C.Pierce), false, R);
	}
	// Damage numbers on the first targets in the fan (cosmetic estimate, like the weapon projectiles)
	TArray<FVector2D> Near;
	Horde->FindNearestTargets(From, Shots, FMath::Max(300.f, C.Range), Near);
	const double MinCos = bRing ? -1.0 : FMath::Cos(FMath::DegreesToRadians(C.Width * 0.5f + 10.f));
	int32 Popped = 0;
	for (const FVector2D& Spot : Near)
	{
		const FVector2D ToSpot = (Spot - From).GetSafeNormal();
		if (FVector2D::DotProduct(ToSpot, C.Dir) < MinCos) continue;
		AddPop(Spot, C.Damage, C.Crit);
		if (++Popped >= 8) break;
	}
}

bool USQAbilityComponent::MoveOwner(const FVector2D& To)
{
	ASQCharacter* R = GetReaper();
	if (!R) return false;
	const float HalfHeight = R->GetCapsuleComponent() ? R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Dest = SQAbGround(this, To, HalfHeight + 2.f);
	return R->TeleportTo(Dest, R->GetActorRotation(), false, false);
}

// ---------------------------------------------------------------- pending effects (server)

void USQAbilityComponent::SpawnTotem(const FVector2D& Where, uint8 Style, float Life, float ShotDamage, float Interval, float Range, int32 Targets, int32 MaxOfStyle, const FCast* FromCast, FColor Color)
{
	FPending Totem;
	Totem.Kind = EPendingKind::Totem;
	if (FromCast) Totem.Cast = *FromCast;
	Totem.Cast.Color = Color;
	Totem.Pos = Where;
	Totem.Life = Life;
	Totem.TickLeft = 0.3f;
	Totem.TotemDamage = ShotDamage;
	Totem.TotemInterval = FMath::Max(0.1f, Interval);
	Totem.TotemRange = Range;
	Totem.TotemTargets = FMath::Clamp(Targets, 1, 8);
	Totem.bPassiveTotem = FromCast == nullptr;
	Totem.Style = Style;
	Totem.TimeLeft = static_cast<float>(FMath::Max(1, MaxOfStyle)); // the per-style cap travels with it (InsertTotem)
	if (ASQCharacter* R = GetReaper()) R->MulticastFX(0, SQAbGround(this, Where), 180.f, Color, 0.4f);
	if (bInPendingTick)
	{
		PendingAdds.Add(Totem);
		return;
	}
	InsertTotem(Totem);
}

void USQAbilityComponent::InsertTotem(const FPending& Totem)
{
	const USQData* D = USQData::Get(this);
	const int32 MaxOfStyle = FMath::Max(1, FMath::RoundToInt(Totem.TimeLeft));
	int32 Mine = 0;
	int32 Oldest = INDEX_NONE;
	int32 AllTotems = 0;
	for (int32 PendIdx = 0; PendIdx < Pending.Num(); ++PendIdx)
	{
		if (Pending[PendIdx].Kind != EPendingKind::Totem) continue;
		AllTotems++;
		if (Pending[PendIdx].Style != Totem.Style) continue;
		if (Oldest == INDEX_NONE) Oldest = PendIdx;
		Mine++;
	}
	if ((Mine >= MaxOfStyle || AllTotems >= FMath::RoundToInt(SQAbTune(D, TEXT("AbilityTotemMax"), 8.f))) && Oldest != INDEX_NONE)
	{
		Pending.RemoveAt(Oldest);
	}
	Pending.Add(Totem);
	RebuildTotemNet();
}

void USQAbilityComponent::RebuildTotemNet()
{
	Totems.Reset();
	for (const FPending& P : Pending)
	{
		if (P.Kind != EPendingKind::Totem) continue;
		FSQAbilityTotemNet Net;
		Net.Pos = FVector_NetQuantize(SQAbGround(this, P.Pos, 0.f));
		Net.Style = P.Style;
		Net.Color = P.Cast.Color;
		Net.Radius = P.Style == 0 ? FMath::Max(80.f, P.Cast.Radius) : 60.f;
		Totems.Add(Net);
	}
}

void USQAbilityComponent::TickPending(float DeltaTime)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !Horde || Pending.Num() == 0) return;
	const USQData* D = USQData::Get(this);
	TArray<int32> Done;
	TArray<FPending> Echoes;
	bool bTotemsChanged = false;
	bInPendingTick = true;

	for (int32 PendIdx = 0; PendIdx < Pending.Num(); ++PendIdx)
	{
		FPending& P = Pending[PendIdx];
		FCast& C = P.Cast;
		switch (P.Kind)
		{
		case EPendingKind::Impact:
			P.TimeLeft -= DeltaTime;
			if (P.TimeLeft <= 0.f) { Impact(C); Done.Add(PendIdx); }
			break;

		case EPendingKind::Drop:
			P.TimeLeft -= DeltaTime;
			if (P.TimeLeft <= 0.f)
			{
				HitCircle(C, P.Pos, C.Radius, C.Damage, C.Effects & ~SQAbAllyBits, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
				ApplyEvo(C, P.Pos, C.Radius, FVector2D::ZeroVector);
				MulticastAbilityHit(0, FVector_NetQuantize(SQAbGround(this, P.Pos)), C.Radius, C.Color, 0.3f);
				Done.Add(PendIdx);
			}
			break;

		case EPendingKind::Zone:
			P.TickLeft -= DeltaTime;
			P.Life -= DeltaTime;
			if (P.TickLeft <= 0.f && P.Life > 0.f)
			{
				P.TickLeft += C.Tick;
				HitCircle(C, P.Pos, C.Radius, C.Damage, C.Effects & ~SQAbAllyBits, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
				// Character pass: a zone can tend reapers standing in it (heal per tick, shields / buffs refreshed)
				if (C.Effects & SQAbAllyBits) ApplyAllyEffects(P.Pos, C.Radius, C.Effects & SQAbAllyBits & ~SQAbilityFx::Revive, C.EffectValue, C.Tick * 2.f + 0.1f, true);
			}
			if (P.Life <= 0.f)
			{
				// Collapse: the final burst and the evolved effect
				HitCircle(C, P.Pos, C.Radius, C.Final, C.Effects & ~(SQAbAllyBits | SQAbilityFx::Pull), C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
				ApplyEvo(C, P.Pos, C.Radius, FVector2D::ZeroVector);
				MulticastAbilityHit(3, FVector_NetQuantize(SQAbGround(this, P.Pos)), C.Radius, C.Color, 0.5f);
				Done.Add(PendIdx);
			}
			break;

		case EPendingKind::Wave:
			P.TickLeft -= DeltaTime;
			P.Life -= DeltaTime;
			if (P.TickLeft <= 0.f && !R->bDowned)
			{
				P.TickLeft += C.Tick;
				FireVolley(C, R->Pos2D());
			}
			if (P.Life <= 0.f) Done.Add(PendIdx);
			break;

		case EPendingKind::Aura:
			P.TickLeft -= DeltaTime;
			P.Life -= DeltaTime;
			if (R->bDowned) { Done.Add(PendIdx); break; }
			if (P.TickLeft <= 0.f)
			{
				P.TickLeft += C.Tick;
				HitCircle(C, R->Pos2D(), C.Radius, C.Damage, C.Effects & ~(SQAbAllyBits | SQAbilityFx::Mark), C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
			}
			if (P.Life <= 0.f) Done.Add(PendIdx);
			break;

		case EPendingKind::Beam:
			P.TickLeft -= DeltaTime;
			P.Life -= DeltaTime;
			if (R->bDowned) { Done.Add(PendIdx); break; }
			if (P.TickLeft <= 0.f)
			{
				P.TickLeft += C.Tick;
				LineHit(C, R->Pos2D());
			}
			if (P.Life <= 0.f) Done.Add(PendIdx);
			break;

		case EPendingKind::Echo:
			P.TimeLeft -= DeltaTime;
			if (P.TimeLeft <= 0.f) { Echoes.Add(P); Done.Add(PendIdx); }
			break;

		case EPendingKind::Totem:
		{
			P.Life -= DeltaTime;
			if (P.Life <= 0.f) { Done.Add(PendIdx); bTotemsChanged = true; break; }
			P.TickLeft -= DeltaTime;
			if (P.TickLeft > 0.f) break;
			P.TickLeft = P.bPassiveTotem ? P.TotemInterval * R->Stats.CooldownMult : P.TotemInterval;
			TArray<FVector2D> Targets;
			Horde->FindNearestTargets(P.Pos, P.TotemTargets, P.TotemRange, Targets);
			if (Targets.Num() > 0)
			{
				const float ShotCrit = FMath::FRand() < R->Stats.CritChance ? 1.f + FMath::Max(0.f, R->Stats.CritDamage) : 1.f;
				const float ShotDamage = P.bPassiveTotem ? P.TotemDamage * ShotCrit : P.TotemDamage;
				const float CritShown = P.bPassiveTotem ? ShotCrit : C.Crit;
				const float Slow = (!P.bPassiveTotem && (C.Effects & SQAbilityFx::Slow)) ? FMath::Max(0.5f, C.EffectTime) : 0.f;
				TArray<FVector_NetQuantize> Bolt;
				const FVector_NetQuantize TotemTop(SQAbGround(this, P.Pos, 140.f));
				for (const FVector2D& Target : Targets)
				{
					Horde->ApplyLineDamage(P.Pos, Target, 45.f, ShotDamage, R, Slow);
					AddPop(Target, ShotDamage, CritShown);
					if (Bolt.Num() < 16)
					{
						Bolt.Add(TotemTop);
						Bolt.Add(FVector_NetQuantize(SQAbGround(this, Target, 50.f)));
					}
				}
				MulticastAbilityLines(Bolt, C.Color);
			}
			if (!P.bPassiveTotem)
			{
				// R totems: drag the horde in (taunt) every 2s; buff reapers close to it
				P.PullTimer -= P.TotemInterval;
				if ((C.Effects & SQAbilityFx::Pull) && P.PullTimer <= 0.f)
				{
					P.PullTimer = 2.f;
					Horde->ApplyPull(P.Pos, P.TotemRange * 0.7f, SQAbTune(D, TEXT("AbilityPullStrength"), 1100.f), SQAbTune(D, TEXT("AbilityKnockMaxRadius"), 75.f));
					MulticastAbilityHit(0, FVector_NetQuantize(SQAbGround(this, P.Pos)), P.TotemRange * 0.7f, C.Color, 0.4f);
				}
				if (C.Effects & SQAbAllyBits)
				{
					ApplyAllyEffects(P.Pos, 600.f, C.Effects & (SQAbilityFx::Empower | SQAbilityFx::Haste | SQAbilityFx::Fortify), C.EffectValue, P.TotemInterval * 2.5f + 0.2f, true);
				}
			}
			break;
		}
		case EPendingKind::Trap:
		{
			P.Life -= DeltaTime;
			P.TimeLeft -= DeltaTime;
			FVector2D Near;
			const bool bTriggered = P.TimeLeft <= 0.f && Horde->FindNearestEnemyAnyMachine(P.Pos, C.Radius * 0.55f, Near);
			if (bTriggered || P.Life <= 0.f)
			{
				const uint32 EnemyBits = C.Effects & ~SQAbAllyBits;
				const int32 Hits = HitCircle(C, P.Pos, C.Radius, C.Damage, EnemyBits, C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
				ApplyAllyEffects(P.Pos, C.Radius, C.Effects & SQAbAllyBits, C.EffectValue, C.EffectTime, true);
				ApplyEvo(C, P.Pos, C.Radius, FVector2D::ZeroVector);
				if (Hits > 0) AddAreaPops(P.Pos, C.Radius, C.Damage, C.Crit, 6);
				MulticastAbilityHit(3, FVector_NetQuantize(SQAbGround(this, P.Pos)), C.Radius, C.Color, 0.45f);
				Done.Add(PendIdx);
			}
			break;
		}

		case EPendingKind::Detonate:
			P.TimeLeft -= DeltaTime;
			if (P.TimeLeft <= 0.f)
			{
				const float BurstRadius = C.Radius > 0.f ? C.Radius : 250.f;
				const float BurstDamage = C.Final > 0.f ? C.Final : C.Damage;
				const int32 Hits = HitCircle(C, P.Pos, BurstRadius, BurstDamage, C.Effects & ~(SQAbAllyBits | SQAbilityFx::Pull), C.EffectTime, C.EffectValue, FVector2D::ZeroVector);
				if (Hits > 0) AddAreaPops(P.Pos, BurstRadius, BurstDamage, C.Crit, 6);
				MulticastAbilityHit(3, FVector_NetQuantize(SQAbGround(this, P.Pos)), BurstRadius, C.Color, 0.4f);
				Done.Add(PendIdx);
			}
			break;

		default:
			Done.Add(PendIdx);
			break;
		}
	}
	bInPendingTick = false;
	for (int32 DoneIdx = Done.Num() - 1; DoneIdx >= 0; --DoneIdx)
	{
		if (Pending.IsValidIndex(Done[DoneIdx])) Pending.RemoveAt(Done[DoneIdx]);
	}
	if (bTotemsChanged) RebuildTotemNet();
	if (PendingAdds.Num() > 0)
	{
		const TArray<FPending> Adds = MoveTemp(PendingAdds);
		PendingAdds.Reset();
		for (const FPending& Add : Adds) InsertTotem(Add);
	}
	for (const FPending& Echo : Echoes)
	{
		if (CanFightNow()) ExecuteCast(Echo.Cast.Idx, SQAbGround(this, Echo.Pos), true, Echo.TotemDamage);
	}
}

// ---------------------------------------------------------------- buffs (server)

void USQAbilityComponent::ReceiveBuff(uint32 Type, float Value, float Seconds, float ShieldAmount)
{
	if (Seconds <= 0.f) return;
	const float EndTime = Now() + Seconds;
	for (FBuff& Existing : Buffs)
	{
		if (Existing.Type != Type) continue;
		Existing.Value = FMath::Max(Existing.Value, Value);
		Existing.EndTime = FMath::Max(Existing.EndTime, EndTime);
		Existing.ShieldGiven += ShieldAmount;
		TickBuffs();
		return;
	}
	FBuff New;
	New.Type = Type;
	New.Value = Value;
	New.EndTime = EndTime;
	New.ShieldGiven = ShieldAmount;
	Buffs.Add(New);
	TickBuffs();
}

void USQAbilityComponent::TickBuffs()
{
	ASQCharacter* R = GetReaper();
	if (!R) return;
	const float T = Now();
	float Haste = 0.f;
	for (int32 BuffIdx = Buffs.Num() - 1; BuffIdx >= 0; --BuffIdx)
	{
		FBuff& B = Buffs[BuffIdx];
		if (T >= B.EndTime || R->bDowned)
		{
			if (B.Type == SQAbilityFx::Shield) R->Shield = FMath::Max(0.f, R->Shield - FMath::Min(R->Shield, B.ShieldGiven));
			Buffs.RemoveAt(BuffIdx);
			continue;
		}
		if (B.Type == SQAbilityFx::Haste) Haste = FMath::Max(Haste, B.Value);
	}
	const float WantSpeed = 1.f + FMath::Clamp(Haste, 0.f, 1.5f);
	if (!FMath::IsNearlyEqual(BuffSpeedMult, WantSpeed, 0.001f)) BuffSpeedMult = WantSpeed;
}

float USQAbilityComponent::GetOutgoingDamageMult() const
{
	float Mult = 1.f;
	float Empower = 0.f;
	for (const FBuff& B : Buffs)
	{
		if (B.Type == SQAbilityFx::Empower) Empower = FMath::Max(Empower, B.Value);
	}
	Mult *= 1.f + Empower;
	const FSQKitDef* Kit = GetKit();
	const ASQCharacter* R = GetReaper();
	// Close Ranks (Nov 23 2026): reapers fighting side by side hit a little harder (solo counts as one ally)
	if (R && R->SquadNear > 0) Mult *= 1.f + SQAbTune(USQData::Get(this), TEXT("SquadAuraDamage"), 0.04f) * static_cast<float>(R->SquadNear);
	if (Kit && R)
	{
		if (Kit->PassiveId == TEXT("Collections"))
		{
			Mult *= 1.f + FMath::Min(FMath::Max(0.f, Kit->P[1]), FMath::Max(0.f, Kit->P[0]) * static_cast<float>(R->Carried));
		}
		else if (Kit->PassiveId == TEXT("Spite"))
		{
			const float Missing = 1.f - FMath::Clamp(R->Health / FMath::Max(1.f, R->Stats.MaxHealth), 0.f, 1.f);
			Mult *= 1.f + FMath::Max(0.f, Kit->P[0]) * Missing;
		}
	}
	if (const FSQTraitDef* T = GetTrait())
	{
		if (T->Effect == TEXT("Damage")) Mult *= 1.f + T->Value * TraitAlpha;
	}
	return Mult;
}

// ---------------------------------------------------------------- character pass: traits (every machine)

const FSQTraitDef* USQAbilityComponent::GetTrait() const
{
	const ASQCharacter* R = GetReaper();
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* Def = (R && D) ? D->FindReaper(R->ReaperId) : nullptr;
	return (Def && Def->Trait.IsSet()) ? &Def->Trait : nullptr;
}

bool USQAbilityComponent::IsPhasing() const
{
	const FSQTraitDef* T = GetTrait();
	return T && T->Effect == TEXT("Phase") && TraitAlpha > 0.5f;
}

void USQAbilityComponent::NoteDash()
{
	TraitLastDashAt = Now();
}

bool USQAbilityComponent::EvalTraitCond(const FSQTraitDef& T, float NowLocal)
{
	const ASQCharacter* R = GetReaper();
	if (!R || R->bDowned) return false;
	const FName Cond = T.Cond;
	if (Cond == TEXT("Always")) return true;
	if (Cond == TEXT("AfterDash")) return NowLocal - TraitLastDashAt <= FMath::Max(0.1f, T.Time);
	if (Cond == TEXT("AfterCast")) return NowLocal - TraitLastCastAt <= FMath::Max(0.1f, T.Time);
	if (Cond == TEXT("Moving")) return TraitMoveHeld >= T.Time;
	if (Cond == TEXT("Still")) return TraitStillHeld >= T.Time;
	if (Cond == TEXT("Carrying"))
	{
		return R->Carried > 0; // strength scales with the sack below
	}
	if (Cond == TEXT("LowHealth")) return R->Health < R->Stats.MaxHealth * 0.4f;
	if (Cond == TEXT("NearAlly") || Cond == TEXT("Alone") || Cond == TEXT("AllyDowned"))
	{
		const float Range = Cond == TEXT("Alone") ? 1200.f : 700.f;
		bool bNear = false, bDown = false;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			const ASQCharacter* Other = *It;
			if (!Other || Other == R) continue;
			if (Other->bDowned) bDown = true;
			else if (FVector::Dist2D(Other->GetActorLocation(), R->GetActorLocation()) <= Range) bNear = true;
		}
		if (Cond == TEXT("AllyDowned")) return bDown;
		return Cond == TEXT("NearAlly") ? bNear : !bNear;
	}
	if (Cond == TEXT("Crowd"))
	{
		const ASQHordeManager* Horde = GetHorde();
		if (!Horde) return false;
		FSQBodyContacts Contacts;
		return Horde->GatherBodyContacts(R->Pos2D(), 260.f, 40.f, 0.f, Contacts) >= FMath::Max(1, FMath::RoundToInt(T.Time));
	}
	if (Cond == TEXT("NearLantern"))
	{
		const USQData* D = USQData::Get(this);
		const float Range = SQAbTune(D, TEXT("TraitLanternRange"), 1600.f);
		for (TActorIterator<ASQLantern> It(GetWorld()); It; ++It)
		{
			if (*It && FVector::Dist2D(It->GetActorLocation(), R->GetActorLocation()) <= Range) return true;
		}
		return false;
	}
	return false;
}

void USQAbilityComponent::TickTrait(float DeltaTime)
{
	ASQCharacter* R = GetReaper();
	const FSQTraitDef* T = GetTrait();
	if (!R || !T) { TraitAlpha = 0.f; return; }
	const float Speed2D = static_cast<float>(R->GetVelocity().Size2D());
	if (Speed2D > 120.f) { TraitMoveHeld += DeltaTime; TraitStillHeld = 0.f; }
	else { TraitStillHeld += DeltaTime; TraitMoveHeld = FMath::Max(0.f, TraitMoveHeld - DeltaTime * 3.f); }
	// Area checks (allies, crowd, lantern) every 0.2 s; timers every frame
	TraitCheckTimer -= DeltaTime;
	const bool bAreaCond = T->Cond == TEXT("NearAlly") || T->Cond == TEXT("Alone") || T->Cond == TEXT("AllyDowned") || T->Cond == TEXT("Crowd") || T->Cond == TEXT("NearLantern");
	if (!bAreaCond || TraitCheckTimer <= 0.f)
	{
		TraitCheckTimer = 0.2f;
		bTraitCondCached = EvalTraitCond(*T, Now());
	}
	float Want = bTraitCondCached ? 1.f : 0.f;
	if (T->Cond == TEXT("Carrying") && bTraitCondCached)
	{
		Want = FMath::Clamp(static_cast<float>(R->Carried) / static_cast<float>(FMath::Max(1, R->Stats.CarryCap)), 0.f, 1.f);
	}
	TraitAlpha = FMath::FInterpTo(TraitAlpha, Want, DeltaTime, Want > TraitAlpha ? 6.f : 3.f);
	if (TraitAlpha < 0.002f) TraitAlpha = Want > 0.f ? TraitAlpha : 0.f;

	// Shove: the server pushes the horde aside in front of a moving reaper
	if (T->Effect == TEXT("Shove") && R->HasAuthority() && TraitAlpha > 0.3f && Speed2D > 120.f)
	{
		TraitShoveTimer -= DeltaTime;
		if (TraitShoveTimer <= 0.f)
		{
			TraitShoveTimer = 0.2f;
			if (ASQHordeManager* Horde = GetHorde())
			{
				const FVector2D Dir = FVector2D(R->GetVelocity().X, R->GetVelocity().Y).GetSafeNormal();
				const float Radius = 120.f + 160.f * T->Value;
				Horde->ApplyKnockback(R->Pos2D() + Dir * (Radius * 0.6f), Radius, Dir, 600.f * TraitAlpha, 75.f);
			}
		}
	}
}

float USQAbilityComponent::GetIncomingDamageMult() const
{
	const ASQCharacter* Me = GetReaper();
	if (!Me) return 1.f;
	float BestBulwark = 0.f;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		const ASQCharacter* Guard = *It;
		if (!Guard || Guard->bDowned || !Guard->Abilities) continue;
		const FSQKitDef* GuardKit = Guard->Abilities->GetKit();
		if (!GuardKit || GuardKit->PassiveId != TEXT("Bulwark")) continue;
		if (FVector::Dist2D(Guard->GetActorLocation(), Me->GetActorLocation()) > GuardKit->P[0]) continue;
		BestBulwark = FMath::Max(BestBulwark, GuardKit->P[1]);
	}
	float Fortify = 0.f;
	for (const FBuff& B : Buffs)
	{
		if (B.Type == SQAbilityFx::Fortify) Fortify = FMath::Max(Fortify, B.Value);
	}
	// Close Ranks: and take a little less
	const float RanksArmor = FMath::Clamp(SQAbTune(USQData::Get(this), TEXT("SquadAuraArmor"), 0.03f) * static_cast<float>(Me->SquadNear), 0.f, 0.3f);
	// Character pass: a Fortify trait
	float TraitArmor = 0.f;
	if (const FSQTraitDef* T = GetTrait())
	{
		if (T->Effect == TEXT("Fortify")) TraitArmor = FMath::Clamp(T->Value * TraitAlpha, 0.f, 0.5f);
	}
	return FMath::Max(0.2f, (1.f - FMath::Clamp(BestBulwark, 0.f, 0.6f)) * (1.f - FMath::Clamp(Fortify, 0.f, 0.6f)) * (1.f - RanksArmor) * (1.f - TraitArmor));
}

float USQAbilityComponent::GetMoveSpeedMult() const
{
	float Mult = FMath::Max(0.1f, BuffSpeedMult);
	const ASQCharacter* R = GetReaper();
	const FSQKitDef* Kit = GetKit();
	if (R && Kit && R->CarriedSkull && Kit->PassiveId == TEXT("Fetch")) Mult *= FMath::Max(1.f, Kit->P[1]);
	if (const FSQTraitDef* T = GetTrait())
	{
		if (T->Effect == TEXT("Speed")) Mult *= 1.f + T->Value * TraitAlpha;
	}
	return Mult;
}

float USQAbilityComponent::GetSoulMagnet(float NormalRange) const
{
	const FSQKitDef* Kit = GetKit();
	if (Kit && Kit->PassiveId == TEXT("Fetch")) return FMath::Max(NormalRange, Kit->P[0]);
	return NormalRange;
}

FName USQAbilityComponent::GetPassiveId() const
{
	const FSQKitDef* Kit = GetKit();
	return Kit ? Kit->PassiveId : FName(NAME_None);
}

float USQAbilityComponent::GetPassiveValue() const
{
	const FSQKitDef* Kit = GetKit();
	return Kit ? Kit->P[0] : 0.f;
}

// ---------------------------------------------------------------- passive (server)

void USQAbilityComponent::QueuePassiveAnim(uint8 Variant)
{
	ASQCharacter* R = GetReaper();
	if (!R || !R->HasAuthority() || R->bDowned) return;
	const float MinGap = SQAbTune(USQData::Get(this), TEXT("AbilityAnimMinInterval"), 1.5f);
	if (Now() - LastPassiveAnim < MinGap) return;
	LastPassiveAnim = Now();
	R->MulticastAbilityAnim(Variant);
}

void USQAbilityComponent::ReduceCooldown(int32 Idx, float Seconds)
{
	if (Seconds <= 0.f) return;
	const FSQKitDef* Kit = GetKit();
	float Cut = Seconds;
	if (Idx == 0 && Kit && Kit->PassiveId == TEXT("HitAndRun") && Kit->P[2] > 0.f)
	{
		Cut = FMath::Min(Cut, FMath::Max(0.f, Kit->P[2] * CooldownMaxE - CooldownCutThisCycle));
		CooldownCutThisCycle += Cut;
	}
	if (Idx == 1)
	{
		// Reap core: refunds on the R (Ruthless Rhythm) become charge - Seconds out of the R's would-be cooldown
		UltCharge = FMath::Clamp(UltCharge + Cut / FMath::Max(1.f, ComputeCooldown(1)), 0.f, 1.f);
		return;
	}
	ReadyAtE = FMath::Max(ServerNow(), ReadyAtE - Cut);
}

void USQAbilityComponent::OnOwnerKill(const FVector2D& Where, bool bElite)
{
	ASQCharacter* R = GetReaper();
	const FSQKitDef* Kit = GetKit();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !R->HasAuthority() || R->bDowned) return;
	// Reap core: every kill trickles a little R charge (reaped souls give much more, USQReapComponent)
	AddUltCharge(SQAbTune(USQData::Get(this), TEXT("UltChargePerKill"), 0.00075f), false);
	// Augments (Ruthless Rhythm): kills take seconds off both ability cooldowns
	const float AugRefund = USQAugments::GetKillCooldownRefund(R);
	if (AugRefund > 0.f)
	{
		ReduceCooldown(0, AugRefund);
		ReduceCooldown(1, AugRefund);
	}
	if (!Kit) return;
	const USQData* D = USQData::Get(this);
	const FName Id = Kit->PassiveId;
	const float DamageMult = R->Stats.DamageMult * GetOutgoingDamageMult();

	if (Id == TEXT("TeamDamage"))
	{
		// Grim: every P1 kills, he and every reaper within P4 deal +P2 damage for P3 seconds
		KillCounter++;
		if (KillCounter >= FMath::Max(1, FMath::RoundToInt(Kit->P[0])))
		{
			KillCounter = 0;
			ApplyAllyEffects(R->Pos2D(), Kit->P[3], SQAbilityFx::Empower, Kit->P[1], Kit->P[2] * R->Stats.Duration, true);
			R->MulticastFX(0, SQAbGround(this, R->Pos2D()), Kit->P[3], FColor(255, 243, 196), 0.5f);
			QueuePassiveAnim(1);
		}
		R->PassiveCharge = static_cast<float>(KillCounter) / FMath::Max(1.f, Kit->P[0]);
	}
	else if (Id == TEXT("HitAndRun"))
	{
		// Swift: kills cut his E cooldown (capped per cooldown) and give a short burst of speed
		ReduceCooldown(0, Kit->P[0]);
		ReceiveBuff(SQAbilityFx::Haste, Kit->P[1], 1.5f);
	}
	else if (Id == TEXT("Builder"))
	{
		KillCounter++;
		if (KillCounter >= FMath::Max(1, FMath::RoundToInt(Kit->P[0])))
		{
			KillCounter = 0;
			SpawnTotem(R->Pos2D() + FMath::RandPointInCircle(90.f), 1, Kit->P[1] * R->Stats.Duration, Kit->P[3] * DamageMult, 0.6f, 1300.f, 1,
				FMath::Max(1, FMath::RoundToInt(Kit->P[2])) + R->Stats.TurretBonus, nullptr, FColor(242, 177, 52));
			QueuePassiveAnim(0);
		}
		R->PassiveCharge = static_cast<float>(KillCounter) / FMath::Max(1.f, Kit->P[0]);
	}
	else if (Id == TEXT("Audit"))
	{
		// The stamp blast's own kills come back into OnOwnerKill synchronously: cap the nesting (remake balance pass)
		static int32 SQAuditBlastDepth = 0;
		KillCounter++;
		if (KillCounter >= FMath::Max(1, FMath::RoundToInt(Kit->P[0])) && Horde && SQAuditBlastDepth < 2)
		{
			++SQAuditBlastDepth;
			KillCounter = 0;
			Horde->DropSouls(Where, FMath::Max(1, FMath::RoundToInt(Kit->P[1])));
			const float Radius = Kit->P[2] * SQAbAreaScale(D, R->Stats.AreaMult);
			const float Crit = FMath::FRand() < R->Stats.CritChance ? 1.f + R->Stats.CritDamage : 1.f;
			if (Horde->ApplyAreaDamage(Where, Radius, Kit->P[3] * DamageMult * Crit, R) > 0) AddAreaPops(Where, Radius, Kit->P[3] * DamageMult * Crit, Crit, 3);
			R->MulticastFX(0, SQAbGround(this, Where), Radius, FColor(210, 66, 60), 0.3f);
			QueuePassiveAnim(0);
			--SQAuditBlastDepth;
		}
		R->PassiveCharge = static_cast<float>(KillCounter) / FMath::Max(1.f, Kit->P[0]);
	}
	else if (Id == TEXT("Flock"))
	{
		FlockCrows = FMath::Min(FMath::Max(1.f, Kit->P[0]), FlockCrows + 1.f);
		FlockDecay = FMath::Max(0.2f, Kit->P[1]);
	}
	else if (Id == TEXT("Seance"))
	{
		// Madame Ectoplasm: kills may raise a ghost where the enemy fell (elites always do)
		if (bElite || FMath::FRand() < Kit->P[0])
		{
			SpawnTotem(Where, 2, Kit->P[1] * R->Stats.Duration, Kit->P[3] * DamageMult, 0.6f, 1300.f, 1, FMath::Max(1, FMath::RoundToInt(Kit->P[2])), nullptr, FColor(124, 240, 184));
			QueuePassiveAnim(static_cast<uint8>(bElite ? 1 : 0));
		}
	}
}

void USQAbilityComponent::OnOwnerDash(const FVector& Direction)
{
	ASQCharacter* R = GetReaper();
	const FSQKitDef* Kit = GetKit();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !R->HasAuthority() || !Kit || !Horde || !CanFightNow()) return;
	const USQData* D = USQData::Get(this);
	if (Kit->PassiveId == TEXT("DashStrike"))
	{
		// Hollow Monk: the dash hurts everything along its path; every enemy hit cuts the E cooldown
		const FVector2D From = R->Pos2D();
		const FVector2D Dir = FVector2D(Direction.X, Direction.Y).GetSafeNormal();
		const float Radius = 180.f * SQAbAreaScale(D, R->Stats.AreaMult);
		const float Damage = Kit->P[0] * R->Stats.DamageMult * GetOutgoingDamageMult();
		int32 Hits = 0;
		for (int32 StepIdx = 0; StepIdx <= 3; ++StepIdx)
		{
			Hits += Horde->ApplyAreaDamage(From + Dir * (130.f * StepIdx), Radius, Damage, R);
		}
		if (Hits > 0)
		{
			AddAreaPops(From + Dir * 200.f, 300.f, Damage, 1.f, 4);
			ReduceCooldown(0, FMath::Min(Kit->P[2], Kit->P[1] * static_cast<float>(Hits)));
		}
		R->MulticastFX(0, R->GetActorLocation() + Direction * 200.f - FVector(0.f, 0.f, 85.f), 300.f, FColor(230, 230, 255), 0.2f);
	}
	else if (Kit->PassiveId == TEXT("Scorch"))
	{
		ScorchDropTime = FMath::Max(0.05f, Kit->P[3]);
		ScorchDropTimer = 0.f;
	}
}

void USQAbilityComponent::TickPassive(float DeltaTime)
{
	ASQCharacter* R = GetReaper();
	const FSQKitDef* Kit = GetKit();
	ASQGameState* GS = GetGS();
	if (!R || !Kit || !GS) return;
	const USQData* D = USQData::Get(this);
	ASQHordeManager* Horde = GS->Horde.Get();
	const FName Id = Kit->PassiveId;
	const float* P = Kit->P;
	const FVector L = R->GetActorLocation();
	const FVector2D Me(L.X, L.Y);
	const float DamageMult = R->Stats.DamageMult * GetOutgoingDamageMult();
	const float AreaScale = SQAbAreaScale(D, R->Stats.AreaMult);
	float Charge = -1.f; // < 0: leave PassiveCharge alone

	if (Id == TEXT("Triage"))
	{
		const float Interval = FMath::Max(0.5f, P[0]);
		PassiveTimer = FMath::Min(Interval, PassiveTimer + DeltaTime);
		if (PassiveTimer >= Interval)
		{
			const float ShieldCap = P[3];
			ASQCharacter* Patient = nullptr;
			float PatientScore = TNumericLimits<float>::Max();
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				ASQCharacter* Cand = *It;
				if (!Cand || Cand->bDowned || FVector::Dist2D(Cand->GetActorLocation(), L) > P[1]) continue;
				const float Score = (Cand->Health + Cand->Shield) / FMath::Max(1.f, Cand->Stats.MaxHealth);
				if (Score >= 1.f + ShieldCap - 0.001f) continue;
				if (Score < PatientScore) { PatientScore = Score; Patient = Cand; }
			}
			if (Patient)
			{
				PassiveTimer = 0.f;
				const float Amount = P[2] * Patient->Stats.MaxHealth + 8.f;
				const float Healed = FMath::Clamp(Patient->Stats.MaxHealth - Patient->Health, 0.f, Amount);
				Patient->Health += Healed;
				Patient->Shield = FMath::Min(Patient->Stats.MaxHealth * ShieldCap, Patient->Shield + FMath::Max(0.f, Amount - Healed));
				R->MulticastFX(0, Patient->GetActorLocation() - FVector(0.f, 0.f, 85.f), 150.f, FColor(150, 255, 170), 0.35f);
				QueuePassiveAnim(static_cast<uint8>(Patient != R ? 0 : 1));
			}
		}
		Charge = PassiveTimer / Interval;
	}
	else if (Id == TEXT("Lookout"))
	{
		const float Interval = FMath::Max(0.5f, P[0]);
		PassiveTimer = FMath::Min(Interval, PassiveTimer + DeltaTime);
		if (Horde && PassiveTimer >= Interval)
		{
			FVector2D MarkPos = FVector2D::ZeroVector;
			uint16 NewMark = 0;
			if (Horde->MarkToughestEnemy(Me, P[1], P[2], P[3], NewMark, MarkPos))
			{
				PassiveTimer = 0.f;
				MarkedEnemyId = NewMark;
				PassiveFxTimer = 0.f;
				R->MulticastFX(0, SQAbGround(this, MarkPos), 260.f, FColor(110, 160, 240), 0.4f);
				QueuePassiveAnim(0);
			}
		}
		PassiveFxTimer += DeltaTime;
		if (Horde && MarkedEnemyId != 0 && PassiveFxTimer >= 0.6f)
		{
			PassiveFxTimer = 0.f;
			FVector2D MarkPos = FVector2D::ZeroVector;
			if (Horde->FindEnemyPos(MarkedEnemyId, MarkPos)) R->MulticastFX(1, SQAbGround(this, MarkPos), 120.f, FColor(110, 160, 240), 0.6f);
			else MarkedEnemyId = 0;
		}
		Charge = PassiveTimer / Interval;
	}
	else if (Id == TEXT("MobileBank"))
	{
		const float Interval = FMath::Max(0.5f, P[0]);
		PassiveTimer = FMath::Min(Interval, PassiveTimer + DeltaTime);
		if (GS->Phase == ESQPhase::Night && PassiveTimer >= Interval)
		{
			PassiveTimer = 0.f;
			int32 Total = 0;
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				ASQCharacter* Cand = *It;
				if (!Cand || Cand->bDowned || Cand->Carried <= 0 || FVector::Dist2D(Cand->GetActorLocation(), L) > P[1]) continue;
				const int32 Amount = FMath::Clamp(FMath::CeilToInt(static_cast<float>(Cand->Carried) * FMath::Clamp(P[2], 0.f, 1.f)), 1, Cand->Carried);
				Cand->Carried -= Amount;
				Total += Amount;
				if (ASQPlayerState* CandPS = Cand->GetPlayerState<ASQPlayerState>()) CandPS->RunSoulsBanked += Amount; // their souls, their bank
			}
			if (Total > 0)
			{
				GS->Banked += Total;
				R->MulticastFX(0, SQAbGround(this, Me), P[1], FColor(255, 215, 120), 0.4f);
				QueuePassiveAnim(static_cast<uint8>(Total >= 20 ? 1 : 0));
			}
		}
		Charge = PassiveTimer / Interval;
	}
	else if (Id == TEXT("Stopwatch"))
	{
		const float Interval = FMath::Max(1.f, P[0]);
		PassiveTimer = FMath::Min(Interval, PassiveTimer + DeltaTime);
		if (Horde && PassiveTimer >= Interval)
		{
			PassiveTimer = 0.f;
			const float Radius = P[1] * AreaScale;
			const float Seconds = FMath::Max(0.1f, P[2]) * R->Stats.Duration;
			Horde->FreezeArea(Me, Radius, Seconds);
			R->MulticastFX(1, SQAbGround(this, Me), Radius, FColor(176, 141, 87), Seconds);
			R->MulticastFX(0, SQAbGround(this, Me), Radius, FColor(255, 240, 200), 0.4f);
			QueuePassiveAnim(0);
		}
		Charge = PassiveTimer / Interval;
	}
	else if (Id == TEXT("Stampede"))
	{
		PassiveTimer -= DeltaTime;
		const UCharacterMovementComponent* Move = R->GetCharacterMovement();
		const float Gallop = (Move ? Move->MaxWalkSpeed : R->Stats.MoveSpeed) * FMath::Clamp(P[3], 0.1f, 1.f);
		const float Speed2D = static_cast<float>(R->GetVelocity().Size2D());
		if (Speed2D >= Gallop && PassiveStep == 0) { PassiveStep = 1; QueuePassiveAnim(0); }
		else if (Speed2D < Gallop * 0.6f) PassiveStep = 0;
		if (Horde && PassiveTimer <= 0.f && Speed2D >= Gallop)
		{
			PassiveTimer = FMath::Max(0.05f, P[2]);
			const float Radius = P[1] * AreaScale;
			if (Horde->ApplyAreaDamage(Me, Radius, FMath::Max(0.f, P[0]) * DamageMult, R, 0.25f) > 0) R->MulticastFX(0, SQAbGround(this, Me), Radius, FColor(224, 122, 31), 0.15f);
		}
	}
	else if (Id == TEXT("Scorch"))
	{
		const float BurnRadius = P[1] * AreaScale;
		if (ScorchDropTime > 0.f)
		{
			ScorchDropTime -= DeltaTime;
			ScorchDropTimer -= DeltaTime;
			if (ScorchDropTimer <= 0.f)
			{
				ScorchDropTimer = 0.07f;
				FScorchPatch Patch;
				Patch.Pos = Me;
				Patch.Life = P[2] * R->Stats.Duration;
				if (ScorchPatches.Num() >= 24) ScorchPatches.RemoveAt(0);
				ScorchPatches.Add(Patch);
				R->MulticastFX(1, SQAbGround(this, Me), BurnRadius, FColor(255, 150, 50), Patch.Life);
			}
		}
		for (int32 PatchIdx = ScorchPatches.Num() - 1; PatchIdx >= 0; --PatchIdx)
		{
			ScorchPatches[PatchIdx].Life -= DeltaTime;
			if (ScorchPatches[PatchIdx].Life <= 0.f) ScorchPatches.RemoveAt(PatchIdx);
		}
		ScorchDamageTimer -= DeltaTime;
		if (ScorchPatches.Num() > 0 && ScorchDamageTimer <= 0.f && Horde)
		{
			ScorchDamageTimer = 0.25f;
			const float BurnDamage = FMath::Max(0.f, P[0]) * 0.25f * DamageMult;
			for (const FScorchPatch& Patch : ScorchPatches) Horde->ApplyAreaDamage(Patch.Pos, BurnRadius, BurnDamage, R);
		}
	}
	else if (Id == TEXT("LastRites"))
	{
		const float Range = P[1];
		ASQCharacter* Target = ReviveTarget.Get();
		if (!Target || !Target->bDowned || Target->CarriedBy || FVector::Dist2D(Target->GetActorLocation(), L) > Range)
		{
			Target = nullptr;
			float BestDist = Range;
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				ASQCharacter* Cand = *It;
				if (!Cand || Cand == R || !Cand->bDowned || Cand->CarriedBy) continue;
				const float CandDist = static_cast<float>(FVector::Dist2D(Cand->GetActorLocation(), L));
				if (CandDist <= BestDist) { BestDist = CandDist; Target = Cand; }
			}
		}
		if (Target != ReviveTarget.Get())
		{
			ReviveTarget = Target;
			ReviveChannel = 0.f;
			if (Target) QueuePassiveAnim(0);
		}
		Charge = 0.f;
		if (Target)
		{
			const float Need = FMath::Max(0.5f, P[0]);
			ReviveChannel += DeltaTime;
			PassiveFxTimer += DeltaTime;
			if (PassiveFxTimer >= 0.5f)
			{
				PassiveFxTimer = 0.f;
				R->MulticastFX(0, Target->GetActorLocation() - FVector(0.f, 0.f, 85.f), 140.f, FColor(220, 200, 240), 0.3f);
			}
			Charge = FMath::Clamp(ReviveChannel / Need, 0.f, 1.f);
			if (ReviveChannel >= Need)
			{
				Target->Revive(FMath::Clamp(P[2], 0.1f, 1.f));
				const APlayerState* TargetPS = Target->GetPlayerState();
				const FSQReaperDef* MyDef = D ? D->FindReaper(R->ReaperId) : nullptr;
				GS->PushAlert(FString::Printf(TEXT("%s was revived on the spot by %s!"), TargetPS ? *TargetPS->GetPlayerName() : TEXT("A reaper"), MyDef ? *MyDef->Name : TEXT("a reaper")));
				ReviveTarget.Reset();
				ReviveChannel = 0.f;
				Charge = 0.f;
			}
		}
	}
	else if (Id == TEXT("Lamplight"))
	{
		PassiveTimer += DeltaTime;
		if (PassiveTimer >= 0.5f)
		{
			PassiveTimer = 0.f;
			const float Radius = P[0] * AreaScale;
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				ASQCharacter* Ally = *It;
				if (!Ally || Ally->bDowned || FVector::Dist2D(Ally->GetActorLocation(), L) > Radius) continue;
				Ally->Health = FMath::Min(Ally->Stats.MaxHealth, Ally->Health + P[1] * 0.5f);
			}
			if (Horde) Horde->ApplyAreaDamage(Me, Radius, 0.f, R, FMath::Max(0.1f, P[2]));
		}
	}
	else if (Id == TEXT("Flock"))
	{
		if (FlockCrows > 0.f)
		{
			FlockDecay -= DeltaTime;
			if (FlockDecay <= 0.f)
			{
				FlockCrows = FMath::Max(0.f, FlockCrows - 1.f);
				FlockDecay = FMath::Max(0.2f, P[1]);
			}
			FlockTick -= DeltaTime;
			if (FlockTick <= 0.f && Horde)
			{
				FlockTick = 0.5f;
				const float Radius = P[2] * AreaScale;
				const float Damage = P[3] * FlockCrows * DamageMult;
				if (Horde->ApplyAreaDamage(Me, Radius, Damage, R) > 0) AddAreaPops(Me, Radius, Damage, 1.f, 2);
			}
			if (FlockCrows >= P[0] && PassiveStep == 0) { PassiveStep = 1; QueuePassiveAnim(1); }
		}
		if (FlockCrows < P[0] * 0.5f) PassiveStep = 0;
		Charge = FlockCrows / FMath::Max(1.f, P[0]);
	}
	else if (Id == TEXT("Bulwark"))
	{
		PassiveFxTimer += DeltaTime;
		if (PassiveFxTimer >= 4.f)
		{
			PassiveFxTimer = 0.f;
			R->MulticastFX(0, SQAbGround(this, Me), P[0], FColor(138, 125, 107), 0.5f);
		}
		PassiveAnimTimer -= DeltaTime;
		if (PassiveAnimTimer <= 0.f)
		{
			PassiveAnimTimer = 1.f;
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				if (*It != R && !It->bDowned && FVector::Dist2D(It->GetActorLocation(), L) <= P[0])
				{
					QueuePassiveAnim(0);
					PassiveAnimTimer = 10.f;
					break;
				}
			}
		}
	}
	else if (Id == TEXT("Fetch"))
	{
		if (R->Carried > CarriedSeen && Now() >= PassiveAnimTimer)
		{
			QueuePassiveAnim(0);
			PassiveAnimTimer = Now() + 6.f;
		}
		CarriedSeen = R->Carried;
	}
	else if (Id == TEXT("Collections"))
	{
		const int32 Step = R->Carried / 25;
		if (Step > PassiveStep) QueuePassiveAnim(static_cast<uint8>(Step >= 4 ? 1 : 0));
		PassiveStep = Step;
	}
	else if (Id == TEXT("Spite"))
	{
		const float HealthFrac = R->Health / FMath::Max(1.f, R->Stats.MaxHealth);
		const int32 Step = HealthFrac < 0.25f ? 2 : (HealthFrac < 0.5f ? 1 : 0);
		if (Step > PassiveStep) QueuePassiveAnim(static_cast<uint8>(Step >= 2 ? 1 : 0));
		PassiveStep = Step;
	}

	if (Charge >= 0.f)
	{
		Charge = FMath::Clamp(Charge, 0.f, 1.f);
		if (FMath::Abs(R->PassiveCharge - Charge) >= 0.02f || (Charge <= 0.f && R->PassiveCharge > 0.f) || (Charge >= 1.f && R->PassiveCharge < 1.f))
		{
			R->PassiveCharge = Charge;
		}
	}
}

// ---------------------------------------------------------------- damage numbers (server)

void USQAbilityComponent::AddPop(const FVector2D& Where, float Amount, float CritMult)
{
	const ASQCharacter* R = GetReaper();
	if (!R || !R->HasAuthority() || Amount < 0.5f) return;
	const USQData* D = USQData::Get(this);
	if (SQAbTune(D, TEXT("DamageNumbersNet"), 1.f) <= 0.f) return;
	const float Huge = SQAbTune(D, TEXT("DamageNumberHuge"), 250.f);
	uint8 Tier = 0;
	if (CritMult > 1.f) Tier = CritMult >= 2.25f ? 2 : 1;
	if (Amount >= Huge) Tier = 2;
	const int16 PopX = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(Where.X)), -32000, 32000));
	const int16 PopY = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(Where.Y)), -32000, 32000));
	for (FPop& Queued : PendingPops)
	{
		if (FMath::Abs(Queued.X - PopX) > 60 || FMath::Abs(Queued.Y - PopY) > 60) continue;
		Queued.Amount = static_cast<uint16>(FMath::Min(65535, static_cast<int32>(Queued.Amount) + FMath::RoundToInt(Amount)));
		Queued.Info = static_cast<uint8>(FMath::Max(static_cast<int32>(Queued.Info & 3), static_cast<int32>(Tier)));
		return;
	}
	if (PendingPops.Num() >= 32) return;
	FPop& Pop = PendingPops.AddDefaulted_GetRef();
	Pop.X = PopX;
	Pop.Y = PopY;
	Pop.Amount = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Amount), 1, 65535));
	Pop.Info = Tier;
}

void USQAbilityComponent::AddAreaPops(const FVector2D& Center, float Radius, float Amount, float CritMult, int32 MaxPops)
{
	const ASQHordeManager* Horde = GetHorde();
	if (!Horde || MaxPops <= 0) return;
	TArray<FVector2D> Spots;
	Horde->FindNearestTargets(Center, MaxPops, Radius, Spots);
	for (const FVector2D& Spot : Spots) AddPop(Spot, Amount, CritMult);
}

void USQAbilityComponent::FlushPops(float DeltaTime)
{
	PopTimer += DeltaTime;
	if (PopTimer < 0.1f || PendingPops.Num() == 0) return;
	PopTimer = 0.f;
	ASQCharacter* R = GetReaper();
	if (!R) return;
	TArray<FSQDamagePop> Out;
	Out.Reserve(PendingPops.Num());
	for (const FPop& Pop : PendingPops)
	{
		FSQDamagePop& Net = Out.AddDefaulted_GetRef();
		Net.X = Pop.X;
		Net.Y = Pop.Y;
		Net.Amount = Pop.Amount;
		Net.Info = Pop.Info;
	}
	PendingPops.Reset();
	R->MulticastDamagePops(Out);
}

// ---------------------------------------------------------------- bots (server)

bool USQAbilityComponent::FindBestTarget(float Range, float ClusterRadius, FVector2D& OutPos, int32& OutCount) const
{
	const ASQCharacter* R = GetReaper();
	const ASQHordeManager* Horde = GetHorde();
	OutCount = 0;
	if (!R || !Horde) return false;
	TArray<FVector2D> Near;
	Horde->FindNearestTargets(R->Pos2D(), 40, Range, Near);
	const double ClusterSq = static_cast<double>(ClusterRadius) * ClusterRadius;
	for (const FVector2D& Cand : Near)
	{
		int32 Count = 0;
		for (const FVector2D& Other : Near)
		{
			if (FVector2D::DistSquared(Cand, Other) <= ClusterSq) Count++;
		}
		if (Count > OutCount) { OutCount = Count; OutPos = Cand; }
	}
	return OutCount > 0;
}

void USQAbilityComponent::TickBot(float DeltaTime)
{
	ASQCharacter* R = GetReaper();
	const AController* Ctrl = R ? R->GetController() : nullptr;
	if (!Ctrl || Ctrl->IsPlayerController()) return;
	BotTimer -= DeltaTime;
	if (BotTimer > 0.f) return;
	const USQData* D = USQData::Get(this);
	BotTimer = SQAbTune(D, TEXT("AbilityBotInterval"), 0.5f);
	const ASQGameState* GS = GetGS();
	const ASQHordeManager* Horde = GetHorde();
	if (!GS || !Horde) return;

	for (int32 Idx = 1; Idx >= 0; --Idx)
	{
		const FSQAbilityDef* A = GetAbility(Idx);
		if (!A || !IsUnlocked(Idx) || CooldownLeft(Idx) > 0.f) continue;
		const int32 Need = FMath::RoundToInt(Idx == 0 ? SQAbTune(D, TEXT("AbilityBotDensityE"), 6.f) : SQAbTune(D, TEXT("AbilityBotDensityR"), 18.f));
		const bool bSelfShape = A->Shape == ESQAbilityShape::Nova || A->Shape == ESQAbilityShape::Self || A->Shape == ESQAbilityShape::Global;
		bool bAllyDown = false;
		if (A->Effects & SQAbilityFx::Revive)
		{
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				if (*It != R && It->bDowned) { bAllyDown = true; break; }
			}
		}
		FVector2D Aim = R->Pos2D();
		int32 Count = 0;
		if (bSelfShape)
		{
			TArray<FVector2D> Near;
			Horde->FindNearestTargets(R->Pos2D(), 40, FMath::Max(300.f, A->Radius), Near);
			Count = Near.Num();
		}
		else
		{
			FindBestTarget(FMath::Max(600.f, A->Range), FMath::Max(250.f, A->Radius), Aim, Count);
		}
		bool bBossClose = false;
		if (const AActor* Boss = GS->Boss.Get())
		{
			const FVector2D BossPos(Boss->GetActorLocation().X, Boss->GetActorLocation().Y);
			if (FVector2D::Distance(BossPos, R->Pos2D()) <= FMath::Max(600.f, A->Range))
			{
				bBossClose = true;
				if (!bSelfShape) Aim = BossPos;
			}
		}
		if (Count >= Need || bBossClose || bAllyDown)
		{
			if (CommitCast(Idx, SQAbGround(this, Aim))) return; // one cast per think
		}
	}
}

// ---------------------------------------------------------------- view API

bool USQAbilityComponent::DescribeKit(const USQData* D, FName ReaperId, TArray<FSQAbilityView>& OutSlots)
{
	OutSlots.Reset();
	const FSQKitDef* Kit = D ? D->FindKit(ReaperId) : nullptr;
	if (!Kit) return false;
	for (uint8 Slot = 0; Slot < 4; ++Slot)
	{
		FSQAbilityView View;
		SQAbFillView(D, *Kit, Slot, View);
		OutSlots.Add(View);
	}
	return true;
}

bool USQAbilityComponent::GetAbilityView(uint8 Slot, FSQAbilityView& Out) const
{
	const USQData* D = USQData::Get(this);
	const FSQKitDef* Kit = GetKit();
	const ASQCharacter* R = GetReaper();
	if (!D || !Kit || !R || !SQAbFillView(D, *Kit, Slot, Out)) return false;
	if (Slot == 0)
	{
		for (const FSQUpgradeDef& Evo : D->Upgrades)
		{
			if (Evo.Category == ESQUpgradeCategory::Evolution && Evo.EvolvesFrom == Kit->SignatureWeapon && R->GetUpgradeLevel(Evo.Id) > 0)
			{
				Out.bEvolved = true;
				Out.Name = SQAbText(Evo.Name);
				Out.Glyph = Evo.IconGlyph;
				Out.Description = SQAbText(Evo.Description);
				break;
			}
		}
		return true;
	}
	if (Slot == 1)
	{
		Out.bEvolved = IsEvolved();
		return true;
	}
	const int32 Idx = Slot - SQAbSlotE;
	Out.bUnlocked = IsUnlocked(Idx);
	Out.bEvolved = IsEvolved();
	Out.Cooldown = CooldownLeft(Idx);
	if (Idx == 1)
	{
		// Reap core: the sweep shows the charge (Cooldown = missing charge in percent of CooldownMax 100); GetUltCharge01 for the exact value
		Out.CooldownMax = SQAbUltScale;
		Out.bAiming = AimingSlot == Slot;
		return true;
	}
	const float LastMax = CooldownMaxE;
	Out.CooldownMax = Out.Cooldown > 0.f && LastMax > 0.f ? LastMax : ComputeCooldown(Idx);
	Out.bAiming = AimingSlot == Slot;
	// Character pass: charges / stacks on the E slot, and an open recast shows as ready
	const FSQAbilityDef* EDef = GetAbility(0);
	if (EDef && (EDef->Mod == TEXT("Charges") || EDef->Mod == TEXT("Stacks"))) Out.Charges = ECounterNet;
	if (EDef && EDef->Mod == TEXT("Recast") && bRecastOpen) Out.Cooldown = 0.f;
	return true;
}

FText USQAbilityComponent::GetPassiveName() const
{
	const FSQKitDef* Kit = GetKit();
	return Kit ? SQAbText(Kit->PassiveName) : FText::GetEmpty();
}

FText USQAbilityComponent::GetPassiveDescription() const
{
	const FSQKitDef* Kit = GetKit();
	return Kit ? SQAbText(Kit->PassiveDesc) : FText::GetEmpty();
}

// ---------------------------------------------------------------- local: aim indicator (owning machine only)

bool USQAbilityComponent::GetCursorGround(FVector& Out) const
{
	// Chase camera (Nov 12 2026): there is no cursor in a fight; the aim point is the controller's (auto-aim or the crosshair)
	const ASQCharacter* R = GetReaper();
	const ASQPlayerController* PC = R ? Cast<ASQPlayerController>(R->GetController()) : nullptr;
	return PC && PC->GetAimPoint(Out);
}

void USQAbilityComponent::TickIndicator(float DeltaTime)
{
	ASQCharacter* R = GetReaper();
	if (!R || !R->IsLocallyControlled()) return;
	QuickFlash = FMath::Max(0.f, QuickFlash - DeltaTime);
	if (AimingSlot == 2 || AimingSlot == 3)
	{
		FVector Cursor;
		if (GetCursorGround(Cursor)) LastAim = Cursor;
		if (R->bDowned) { AimingSlot = 255; return; }
		DrawIndicator(AimingSlot - SQAbSlotE, ClampAim(AimingSlot - SQAbSlotE, LastAim), 1.f);
	}
	else if (QuickFlash > 0.f && (QuickFlashSlot == 2 || QuickFlashSlot == 3))
	{
		DrawIndicator(QuickFlashSlot - SQAbSlotE, QuickFlashAim, QuickFlash / 0.3f);
	}
}

void USQAbilityComponent::DrawIndicator(int32 Idx, const FVector& Aim, float Alpha)
{
	UWorld* World = GetWorld();
	const ASQCharacter* R = GetReaper();
	const FSQAbilityDef* A = GetAbility(Idx);
	if (!World || !R || !A) return;
	const USQData* D = USQData::Get(this);
	// Off by default since the chase camera (Nov 12 2026): the range ring / aim shape were overhead-view aids. Tuning AbilityIndicators 1 brings them back.
	if (SQAbTune(D, TEXT("AbilityIndicators"), 0.f) < 0.5f) return;
	const bool bEvo = IsEvolved();
	const float Radius = A->Radius * (bEvo ? A->EvoRadius : 1.f) * SQAbAreaScale(D, R->Stats.AreaMult);
	const FLinearColor Col = FMath::Lerp(A->Color, FLinearColor::White, 0.35f) * FMath::Clamp(Alpha, 0.2f, 1.f);
	auto Dot = [World, &Col](const FVector& At, float Size)
	{
		FSQFxParticle Pt;
		Pt.Pos = At;
		Pt.Color = Col;
		Pt.Life = 0.05f;
		Pt.SizeStart = FVector(Size, Size, Size * 0.5f);
		Pt.SizeEnd = Pt.SizeStart;
		Pt.bShrinkOut = false;
		SQFx::Emit(World, Pt);
	};
	const FVector Me = SQAbGround(this, R->Pos2D(), 8.f);
	const FVector Target(Aim.X, Aim.Y, Me.Z);
	auto Circle = [&Dot](const FVector& C, float Rad, int32 Points, float Size)
	{
		for (int32 PtIdx = 0; PtIdx < Points; ++PtIdx)
		{
			const float Ang = 2.f * PI * static_cast<float>(PtIdx) / static_cast<float>(Points);
			Dot(C + FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f), Size);
		}
	};
	auto LineDots = [&Dot](const FVector& From, const FVector& To, float Step, float Size)
	{
		const float Len = static_cast<float>(FVector::Dist(From, To));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Len / Step), 1, 40);
		for (int32 StepIdx = 0; StepIdx <= Steps; ++StepIdx) Dot(FMath::Lerp(From, To, static_cast<float>(StepIdx) / Steps), Size);
	};
	if (A->Range > 0.f) Circle(Me, A->Range, 28, 6.f); // range ring
	const FVector Dir3 = (Target - Me).GetSafeNormal2D();
	switch (A->Shape)
	{
	case ESQAbilityShape::Line:
	case ESQAbilityShape::Volley:
		LineDots(Me, Me + Dir3 * A->Range, 60.f, 12.f);
		break;
	case ESQAbilityShape::Dash:
		LineDots(Me, Target, 50.f, 12.f);
		if (Radius > 0.f) Circle(Target, Radius, 20, 10.f);
		break;
	case ESQAbilityShape::Cone:
	{
		const float Half = A->Width * (bEvo ? A->EvoRadius : 1.f) * 0.5f;
		const FVector Left = Dir3.RotateAngleAxis(-Half, FVector::UpVector);
		const FVector Right = Dir3.RotateAngleAxis(Half, FVector::UpVector);
		LineDots(Me, Me + Left * A->Range, 60.f, 12.f);
		LineDots(Me, Me + Right * A->Range, 60.f, 12.f);
		break;
	}
	case ESQAbilityShape::Nova:
	case ESQAbilityShape::Self:
	case ESQAbilityShape::Global:
		Circle(Me, FMath::Max(100.f, Radius), 32, 12.f);
		break;
	case ESQAbilityShape::Blink:
		LineDots(Me, Target, 90.f, 8.f);
		Circle(Target, FMath::Max(80.f, Radius), 22, 12.f);
		break;
	case ESQAbilityShape::Barrage:
		Circle(Target, FMath::Max(80.f, A->Width), 32, 12.f);
		break;
	case ESQAbilityShape::Chain:
	case ESQAbilityShape::Totem:
		Circle(Target, 90.f, 14, 14.f);
		Circle(Target, FMath::Max(100.f, Radius), 22, 8.f);
		break;
	default:
		Circle(Target, FMath::Max(80.f, Radius), 26, 12.f);
		break;
	}
}

// ---------------------------------------------------------------- local cosmetics (every machine)

void USQAbilityComponent::PlayCastAnim(int32 Idx, bool bBig)
{
	const ASQCharacter* R = GetReaper();
	const FSQAbilityDef* A = GetAbility(Idx);
	if (!R || !A || R->bDowned || !R->SkinMesh) return;
	USQAnimInstance* Anim = Cast<USQAnimInstance>(R->SkinMesh->GetAnimInstance());
	if (!Anim) return;
	for (const FName& Clip : A->Anims)
	{
		const bool bAuthored = Clip.ToString().StartsWith(TEXT("SQ_")) || Clip.ToString().StartsWith(TEXT("Reaper_"));
		const float Rate = (bAuthored ? 1.f : 1.4f) * (bBig ? 0.9f : 1.f);
		if (Anim->PlayAction(Clip, Rate, true, 0.06f, true)) return;
	}
}

bool USQAbilityComponent::PlayPassiveAnim(USQAnimInstance* SkinAnim, bool bFullBody, uint8 Variant) const
{
	const FSQKitDef* Kit = GetKit();
	if (!SkinAnim || !Kit) return false;
	for (const FName& Clip : Kit->PassiveAnims)
	{
		const bool bAuthored = Clip.ToString().StartsWith(TEXT("SQ_"));
		const float Rate = (bAuthored ? 1.f : 1.3f) * (Variant > 0 ? 0.9f : 1.f);
		if (SkinAnim->PlayAction(Clip, Rate, bFullBody, 0.1f, true)) return true;
	}
	return false;
}

void USQAbilityComponent::DrawFlavour(const FVector& At, float Radius, const FLinearColor& Color, FName Fx, bool bBig)
{
	UWorld* World = GetWorld();
	if (!World) return;
	const int32 Count = bBig ? 26 : 12;
	switch (SQAbFlavourGroup(Fx))
	{
	case 0: // rising light: embers, holy, time, pillars
		SQFx::Burst(World, At, Color, Count, 220.f + Radius * 0.2f, 0.9f, 8.f, 500.f);
		if (bBig || Fx == TEXT("Pillar"))
		{
			SQFx::Streak(World, At, At + FVector(0.f, 0.f, bBig ? 900.f : 450.f), FMath::Lerp(Color, FLinearColor::White, 0.4f), bBig ? 110.f : 60.f, 0.55f);
			for (int32 PillarIdx = 0; PillarIdx < (bBig ? 6 : 3); ++PillarIdx)
			{
				const float Ang = 2.f * PI * PillarIdx / (bBig ? 6.f : 3.f);
				const FVector Off(FMath::Cos(Ang) * Radius * 0.55f, FMath::Sin(Ang) * Radius * 0.55f, 0.f);
				SQFx::Streak(World, At + Off, At + Off + FVector(0.f, 0.f, bBig ? 520.f : 260.f), Color, 28.f, 0.45f, 0.05f * PillarIdx);
			}
		}
		break;
	case 1: // falling debris: shards, stamps, dust, paper
		SQFx::Burst(World, At + FVector(0.f, 0.f, 40.f), Color, Count, 380.f + Radius * 0.3f, 0.7f, 10.f, -1400.f);
		break;
	case 2: // smoke: ghosts, soot, blood, crows
		SQFx::Burst(World, At, Color * 0.7f, Count, 120.f + Radius * 0.15f, 1.1f, 18.f, 140.f);
		break;
	default: // sparks
		SQFx::Ring(World, At, Color, Count, 300.f + Radius * 0.6f, 0.35f, 8.f);
		SQFx::Burst(World, At, Color, Count / 2, 420.f, 0.35f, 6.f);
		break;
	}
}

void USQAbilityComponent::DrawBurst(const FVector& At, float Radius, const FLinearColor& Color, FName Fx, bool bBig)
{
	UWorld* World = GetWorld();
	if (!World) return;
	const FLinearColor Bright = FMath::Lerp(Color, FLinearColor::White, 0.45f);
	SQFx::Shockwave(World, At, FMath::Max(60.f, Radius), Color, 0.35f, bBig ? 34.f : 18.f);
	SQFx::Ring(World, At, Color, bBig ? 28 : 14, FMath::Max(150.f, Radius * 1.5f), 0.4f, 9.f);
	SQFx::Flash(World, At + FVector(0.f, 0.f, 30.f), Bright, FMath::Clamp(Radius * (bBig ? 0.55f : 0.3f), 50.f, bBig ? 420.f : 180.f), 0.16f);
	if (bBig)
	{
		// R: staggered shockwaves + a white core flash
		SQFx::Shockwave(World, At, Radius * 1.15f, Bright, 0.5f, 22.f, 0.12f);
		SQFx::Shockwave(World, At, Radius * 0.7f, Color, 0.45f, 16.f, 0.25f);
		SQFx::Flash(World, At + FVector(0.f, 0.f, 80.f), FLinearColor::White, 140.f, 0.1f, 0.05f);
	}
	DrawFlavour(At, Radius, Color, Fx, bBig);
}

void USQAbilityComponent::MulticastCast_Implementation(uint8 Slot, FVector_NetQuantize Origin, FVector_NetQuantize Target, uint8 Flags)
{
	if (Slot == 2 || Slot == 3) TraitLastCastAt = Now(); // character pass: AfterCast traits, on every machine
	if (Slot == 3 && !(Flags & 2))
	{
		if (ASQCharacter* Caster = GetReaper()) Caster->Bark(TEXT("Combo")); // character pass: the R line
	}
	if (GetNetMode() == NM_DedicatedServer || (Slot != 2 && Slot != 3)) return;
	UWorld* World = GetWorld();
	const ASQCharacter* R = GetReaper();
	const int32 Idx = Slot - SQAbSlotE;
	const FSQAbilityDef* A = GetAbility(Idx);
	if (!World || !R || !A) return;
	const USQData* D = USQData::Get(this);
	const bool bEvo = (Flags & 1) != 0;
	const bool bEcho = (Flags & 2) != 0;
	const bool bBig = (Flags & 4) != 0;
	const FLinearColor Col = A->Color;
	const FVector From(Origin.X, Origin.Y, Origin.Z);
	const FVector To(Target.X, Target.Y, Target.Z);
	const float Radius = A->Radius * (bEvo ? A->EvoRadius : 1.f) * SQAbAreaScale(D, R->Stats.AreaMult);
	const float Duration = A->Duration * (bEvo ? A->EvoDuration : 1.f) * R->Stats.Duration;
	if (!bEcho) PlayCastAnim(Idx, bBig);
	SQFx::Flash(World, R->GetActorLocation() + FVector(0.f, 0.f, 20.f), FMath::Lerp(Col, FLinearColor::White, 0.5f), bBig ? 160.f : 90.f, 0.14f);
	const FVector Dir = (To - From).GetSafeNormal2D();

	auto AddLocal = [this, Idx, &Col, A](uint8 Kind, const FVector& Pos, float Time, float FxRadius, bool bBigFx)
	{
		FLocalFx Fx;
		Fx.Idx = Idx;
		Fx.Kind = Kind;
		Fx.Pos = Pos;
		Fx.TimeLeft = Time;
		Fx.Life = Time;
		Fx.Radius = FxRadius;
		Fx.bBig = bBigFx;
		Fx.bPull = (A->Effects & SQAbilityFx::Pull) != 0;
		Fx.Color = Col;
		Fx.Fx = A->Fx;
		if (LocalFx.Num() < 24) LocalFx.Add(Fx);
	};

	switch (A->Shape)
	{
	case ESQAbilityShape::Circle:
		if (A->Delay > 0.f)
		{
			ASQFx::Spawn(World, 1, To, Radius, Col, A->Delay);
			AddLocal(0, To, A->Delay, Radius, bBig);
		}
		else DrawBurst(To, Radius, Col, A->Fx, bBig);
		break;
	case ESQAbilityShape::Nova:
	case ESQAbilityShape::Global:
		if (A->Delay > 0.f)
		{
			ASQFx::Spawn(World, 1, From, Radius, Col, A->Delay);
			AddLocal(3, From, A->Delay, Radius, bBig);
		}
		else DrawBurst(From, Radius, Col, A->Fx, bBig);
		break;
	case ESQAbilityShape::Cone:
	{
		const float Half = A->Width * (bEvo ? A->EvoRadius : 1.f) * 0.5f;
		const float Yaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Dir.Y, Dir.X)));
		SQFx::SwingArc(World, From + FVector(0.f, 0.f, 40.f), Yaw - Half, Half * 2.f, A->Range * 0.6f, Col, 40.f, 0.3f);
		SQFx::SwingArc(World, From + FVector(0.f, 0.f, 40.f), Yaw - Half, Half * 2.f, A->Range, FMath::Lerp(Col, FLinearColor::White, 0.3f), 26.f, 0.35f);
		DrawFlavour(From + Dir * (A->Range * 0.6f), A->Range * 0.4f, Col, A->Fx, bBig);
		break;
	}
	case ESQAbilityShape::Line:
	{
		const float LineWidth = A->Width * (bEvo ? A->EvoRadius : 1.f);
		const FVector End = From + Dir * A->Range;
		SQFx::Streak(World, From + FVector(0.f, 0.f, 50.f), End + FVector(0.f, 0.f, 50.f), Col, FMath::Max(30.f, LineWidth), 0.3f);
		if (Duration > 0.f) AddLocal(4, Dir * A->Range, Duration, LineWidth, bBig);
		DrawFlavour(End, 120.f, Col, A->Fx, false);
		break;
	}
	case ESQAbilityShape::Dash:
		SQFx::Streak(World, From + FVector(0.f, 0.f, 60.f), To + FVector(0.f, 0.f, 60.f), Col, 70.f, 0.35f);
		SQFx::Flash(World, From + FVector(0.f, 0.f, 40.f), Col, 100.f, 0.12f);
		DrawBurst(To, FMath::Max(120.f, Radius), Col, A->Fx, bBig);
		break;
	case ESQAbilityShape::Blink:
		DrawBurst(From, FMath::Max(100.f, Radius), Col, A->Fx, false);
		DrawBurst(To, FMath::Max(100.f, Radius), Col, A->Fx, bBig);
		break;
	case ESQAbilityShape::Zone:
		ASQFx::Spawn(World, 1, To, Radius, Col, FMath::Max(0.3f, Duration));
		AddLocal(1, To, FMath::Max(0.3f, Duration), Radius, bBig);
		DrawBurst(To, Radius, Col, A->Fx, false);
		break;
	case ESQAbilityShape::Volley:
	{
		const bool bRing = A->Width >= 359.f;
		const int32 Rays = FMath::Clamp(A->Count, 1, 16);
		for (int32 RayIdx = 0; RayIdx < Rays; ++RayIdx)
		{
			const float Off = bRing ? 360.f * RayIdx / Rays : (Rays > 1 ? -A->Width * 0.5f + A->Width * RayIdx / (Rays - 1) : 0.f);
			const FVector RayDir = Dir.RotateAngleAxis(Off, FVector::UpVector);
			SQFx::Streak(World, From + FVector(0.f, 0.f, 60.f), From + RayDir * 320.f + FVector(0.f, 0.f, 60.f), Col, 18.f, 0.2f);
		}
		DrawFlavour(From, 150.f, Col, A->Fx, bBig);
		break;
	}
	case ESQAbilityShape::Barrage:
		ASQFx::Spawn(World, 1, To, FMath::Max(100.f, A->Width), Col, FMath::Max(0.3f, Duration + A->Delay));
		break;
	case ESQAbilityShape::Chain:
		SQFx::Flash(World, To + FVector(0.f, 0.f, 50.f), Col, 120.f, 0.15f);
		break;
	case ESQAbilityShape::Self:
		DrawBurst(R->GetActorLocation() - FVector(0.f, 0.f, 85.f), Radius, Col, A->Fx, bBig);
		AddLocal(2, FVector::ZeroVector, FMath::Max(0.3f, Duration), Radius, bBig);
		break;
	case ESQAbilityShape::Totem:
		DrawBurst(To, 200.f, Col, A->Fx, bBig);
		break;
	default:
		break;
	}
	// The monster's own effect language on top of the shape's effect (SQFxTheme, Nov 10 2026)
	SQTheme::PlayCast(World, R->ReaperId, Slot == SQAbSlotE + 1, From, To, Radius);
	if (bBig && !bEcho)
	{
		// Every R: a light pillar out of the caster
		const FVector Feet = R->GetActorLocation() - FVector(0.f, 0.f, 85.f);
		SQFx::Streak(World, Feet, Feet + FVector(0.f, 0.f, 700.f), FMath::Lerp(Col, FLinearColor::White, 0.5f), 90.f, 0.5f);
		SQFx::Shockwave(World, Feet, 260.f, Col, 0.4f, 26.f);
	}
}

void USQAbilityComponent::MulticastAbilityHit_Implementation(uint8 Kind, FVector_NetQuantize Where, float Radius, FColor Color, float Duration)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* World = GetWorld();
	if (!World) return;
	const FVector At(Where.X, Where.Y, Where.Z);
	const FLinearColor Col(Color);
	switch (Kind)
	{
	case 1:
		ASQFx::Spawn(World, 1, At, Radius, Col, FMath::Max(0.1f, Duration));
		break;
	case 2:
		SQFx::Burst(World, At, Col, 8, 160.f, 0.6f, 7.f, 400.f);
		SQFx::Ring(World, At - FVector(0.f, 0.f, 80.f), Col, 10, 160.f, 0.35f, 6.f);
		break;
	case 3:
		DrawBurst(At, Radius, Col, TEXT("Pillar"), true);
		break;
	default:
		DrawBurst(At, Radius, Col, TEXT("Shards"), false);
		SQFx::Impact(World, At + FVector(0.f, 0.f, 30.f), Col, 1.f, false);
		break;
	}
}

void USQAbilityComponent::MulticastAbilityLines_Implementation(const TArray<FVector_NetQuantize>& Points, FColor Color)
{
	if (GetNetMode() == NM_DedicatedServer || Points.Num() < 2) return;
	UWorld* World = GetWorld();
	const FLinearColor Col(Color);
	for (int32 PtIdx = 0; PtIdx + 1 < Points.Num(); ++PtIdx)
	{
		const FVector A(Points[PtIdx].X, Points[PtIdx].Y, Points[PtIdx].Z);
		const FVector B(Points[PtIdx + 1].X, Points[PtIdx + 1].Y, Points[PtIdx + 1].Z);
		SQFx::Streak(World, A, B, Col, 22.f, 0.18f);
		const int32 Sparks = FMath::Clamp(FMath::CeilToInt(static_cast<float>(FVector::Dist(A, B)) / 70.f), 1, 20);
		for (int32 SparkIdx = 0; SparkIdx <= Sparks; ++SparkIdx)
		{
			const FVector Spot = FMath::Lerp(A, B, static_cast<float>(SparkIdx) / Sparks) + FVector(FMath::FRandRange(-16.f, 16.f), FMath::FRandRange(-16.f, 16.f), FMath::FRandRange(-10.f, 10.f));
			SQFx::Burst(World, Spot, Col, 1, 80.f, 0.16f, 9.f, 0.f);
		}
		SQFx::Impact(World, B, Col, 0.8f, false);
	}
}

void USQAbilityComponent::TickLocalFx(float DeltaTime)
{
	UWorld* World = GetWorld();
	const ASQCharacter* R = GetReaper();
	if (!World || !R) return;
	const FVector Feet = R->GetActorLocation() - FVector(0.f, 0.f, 85.f);

	for (int32 FxIdx = LocalFx.Num() - 1; FxIdx >= 0; --FxIdx)
	{
		FLocalFx& Fx = LocalFx[FxIdx];
		Fx.TimeLeft -= DeltaTime;
		Fx.Emit -= DeltaTime;
		const bool bEnd = Fx.TimeLeft <= 0.f;
		switch (Fx.Kind)
		{
		case 0:
			if (bEnd) DrawBurst(Fx.Pos, Fx.Radius, Fx.Color, Fx.Fx, Fx.bBig);
			break;
		case 3:
			if (bEnd) DrawBurst(Feet, Fx.Radius, Fx.Color, Fx.Fx, Fx.bBig);
			break;
		case 1: // zone: a swirling rim, sparks spiralling in when it pulls
			if (Fx.Emit <= 0.f && !bEnd)
			{
				Fx.Emit = 0.12f;
				const float Spin = Fx.TimeLeft * 3.f;
				for (int32 ArmIdx = 0; ArmIdx < 6; ++ArmIdx)
				{
					const float Ang = Spin + 2.f * PI * ArmIdx / 6.f;
					const FVector Rim = Fx.Pos + FVector(FMath::Cos(Ang) * Fx.Radius, FMath::Sin(Ang) * Fx.Radius, 10.f);
					FSQFxParticle Pt;
					Pt.Pos = Rim;
					Pt.Vel = Fx.bPull ? (Fx.Pos - Rim) * 1.6f : FVector(0.f, 0.f, 120.f);
					Pt.Color = Fx.Color;
					Pt.Life = 0.5f;
					Pt.SizeStart = FVector(14.f);
					Pt.SizeEnd = FVector(4.f);
					SQFx::Emit(World, Pt);
				}
			}
			if (bEnd) DrawBurst(Fx.Pos, Fx.Radius, Fx.Color, Fx.Fx, Fx.bBig);
			break;
		case 2: // self aura following the reaper
			if (Fx.Emit <= 0.f && !bEnd && !R->bDowned)
			{
				Fx.Emit = 0.15f;
				SQFx::Ring(World, Feet + FVector(0.f, 0.f, 5.f), Fx.Color, 10, Fx.Radius * 1.4f, 0.25f, 7.f);
				DrawFlavour(Feet, Fx.Radius * 0.4f, Fx.Color, Fx.Fx, false);
			}
			break;
		case 4: // beam following the reaper (Pos = offset to the far end)
			if (Fx.Emit <= 0.f && !bEnd && !R->bDowned)
			{
				Fx.Emit = 0.1f;
				const FVector Start = Feet + FVector(0.f, 0.f, 135.f);
				SQFx::Streak(World, Start, Start + Fx.Pos, Fx.Color, FMath::Max(30.f, Fx.Radius) * (Fx.bBig ? 1.3f : 1.f), 0.14f);
				SQFx::Burst(World, Start + Fx.Pos * FMath::FRand(), Fx.Color, 3, 200.f, 0.3f, 8.f, 300.f);
			}
			break;
		default:
			break;
		}
		if (bEnd) LocalFx.RemoveAt(FxIdx);
	}

	// Summoned posts: glowing pillars
	LocalTotemFx -= DeltaTime;
	if (LocalTotemFx <= 0.f && Totems.Num() > 0)
	{
		LocalTotemFx = 0.12f;
		for (const FSQAbilityTotemNet& Totem : Totems)
		{
			const FLinearColor TotemCol(Totem.Color);
			const FVector Base(Totem.Pos.X, Totem.Pos.Y, Totem.Pos.Z);
			const float Height = Totem.Style == 0 ? 260.f : 140.f;
			SQFx::Streak(World, Base, Base + FVector(0.f, 0.f, Height), TotemCol, Totem.Style == 0 ? 50.f : 26.f, 0.16f);
			if (Totem.Style == 0) SQFx::Ring(World, Base + FVector(0.f, 0.f, 5.f), TotemCol, 8, Totem.Radius, 0.3f, 6.f);
		}
	}

	// Passive looks that need no RPC (decided from the kit and replicated state)
	LocalPassiveFx -= DeltaTime;
	if (LocalPassiveFx <= 0.f && !R->bDowned)
	{
		LocalPassiveFx = 0.4f;
		const FSQKitDef* Kit = GetKit();
		const USQData* D = USQData::Get(this);
		if (Kit && Kit->PassiveId == TEXT("Lamplight"))
		{
			SQFx::Ring(World, Feet + FVector(0.f, 0.f, 5.f), FLinearColor(1.f, 0.82f, 0.48f), 12, Kit->P[0] * SQAbAreaScale(D, R->Stats.AreaMult) * 0.9f, 0.4f, 5.f);
		}
		else if (Kit && Kit->PassiveId == TEXT("Flock") && R->PassiveCharge > 0.01f)
		{
			const int32 Crows = FMath::Clamp(FMath::RoundToInt(R->PassiveCharge * Kit->P[0]), 1, 12);
			const float Spin = Now() * 3.f;
			for (int32 CrowIdx = 0; CrowIdx < Crows; ++CrowIdx)
			{
				const float Ang = Spin + 2.f * PI * CrowIdx / Crows;
				const FVector At = Feet + FVector(FMath::Cos(Ang) * Kit->P[2] * 0.7f, FMath::Sin(Ang) * Kit->P[2] * 0.7f, 120.f);
				SQFx::Burst(World, At, FLinearColor(0.35f, 0.35f, 0.42f), 1, 30.f, 0.4f, 14.f, 0.f);
			}
		}
	}
}
