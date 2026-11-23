// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQReap.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQPlayerState.h"
#include "SQHordeManager.h"
#include "SQAbility.h"
#include "SQArena.h"
#include "SQFx.h"

// ---------------------------------------------------------------- helpers (prefixed: unity build)
namespace
{
	const FLinearColor SQReapTeal(0.36f, 0.95f, 0.84f);
	constexpr int32 SQReapMaxSpotsSent = 12;

	float SQReapTune(const USQData* D, const TCHAR* Key, float Default)
	{
		return D ? D->Tune(FName(Key), Default) : Default;
	}

	FVector SQReapGround(const UObject* Ctx, const FVector2D& P, float Lift)
	{
		const float X = static_cast<float>(P.X);
		const float Y = static_cast<float>(P.Y);
		return FVector(X, Y, ASQArena::GroundZAt(Ctx, X, Y) + Lift);
	}
}

// ---------------------------------------------------------------- setup

USQReapComponent::USQReapComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void USQReapComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(USQReapComponent, ReadyAt, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQReapComponent, Streak, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USQReapComponent, StreakEndsAt, COND_OwnerOnly);
}

ASQCharacter* USQReapComponent::GetReaper() const
{
	return Cast<ASQCharacter>(GetOwner());
}

ASQGameState* USQReapComponent::GetGS() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGameState<ASQGameState>() : nullptr;
}

ASQHordeManager* USQReapComponent::GetHorde() const
{
	ASQGameState* GS = GetGS();
	return GS ? GS->Horde.Get() : nullptr;
}

float USQReapComponent::ServerNow() const
{
	const ASQGameState* GS = GetGS();
	if (GS) return static_cast<float>(GS->GetServerWorldTimeSeconds());
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
}

bool USQReapComponent::CanReapNow() const
{
	const ASQCharacter* R = GetReaper();
	const ASQGameState* GS = GetGS();
	if (!R || !GS || R->bDowned || R->CarriedBy || GS->bPickPause || R->IsFrozenForLobby()) return false;
	return GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;
}

void USQReapComponent::ResetForRun()
{
	ReadyAt = 0.f;
	Streak = 0;
	StreakEndsAt = 0.f;
	bPassageActive = false;
	LocalGate = 0.f;
	BotTimer = 0.f;
}

// ---------------------------------------------------------------- UI API

float USQReapComponent::GetCooldownRemaining() const
{
	return FMath::Max(0.f, ReadyAt - ServerNow());
}

float USQReapComponent::GetCooldownMax() const
{
	return FMath::Max(0.05f, SQReapTune(USQData::Get(this), TEXT("ReapCooldown"), 1.2f));
}

float USQReapComponent::GetRange() const
{
	return FMath::Max(50.f, SQReapTune(USQData::Get(this), TEXT("ReapRange"), 380.f));
}

float USQReapComponent::GetHalfAngleDeg() const
{
	return FMath::Clamp(SQReapTune(USQData::Get(this), TEXT("ReapHalfAngle"), 35.f), 1.f, 180.f);
}

int32 USQReapComponent::GetStreak() const
{
	return GetStreakTimeLeft() > 0.f ? static_cast<int32>(Streak) : 0;
}

float USQReapComponent::GetStreakTimeLeft() const
{
	return Streak > 0 ? FMath::Max(0.f, StreakEndsAt - ServerNow()) : 0.f;
}

// ---------------------------------------------------------------- input + server swing

void USQReapComponent::TryReap(const FVector& AimPoint)
{
	ASQCharacter* R = GetReaper();
	if (!R || AimPoint.ContainsNaN() || LocalGate > 0.f || !CanReapNow()) return;
	if (GetCooldownRemaining() > 0.1f) return;
	LocalGate = 0.15f;
	R->StartAimLock(0.35f); // face the swing
	ServerReap(FVector_NetQuantize(AimPoint));
}

void USQReapComponent::ServerReap_Implementation(FVector_NetQuantize AimPoint)
{
	const FVector Aim(AimPoint.X, AimPoint.Y, AimPoint.Z);
	if (Aim.ContainsNaN() || !CanReapNow()) return;
	// A little slack for the owner's view of the replicated cooldown
	if (GetCooldownRemaining() > 0.15f) return;
	DoReap(Aim);
}

void USQReapComponent::DoReap(const FVector& AimPoint)
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	const ASQGameState* GS = GetGS();
	if (!R || !R->HasAuthority() || !Horde || !GS) return;
	const USQData* D = USQData::Get(this);
	ReadyAt = ServerNow() + GetCooldownMax();

	const FVector2D Me = R->Pos2D();
	FVector2D Dir = FVector2D(AimPoint.X, AimPoint.Y) - Me;
	if (Dir.SizeSquared() < 1.0) Dir = FVector2D(R->GetActorForwardVector().X, R->GetActorForwardVector().Y);
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero()) Dir = FVector2D(1.f, 0.f);

	// Scythe damage: ReapDamage x (1 + ReapDamagePerLevel x (team level - 1)) x the damage stat (kit buffs included) x crit
	const float LevelScale = 1.f + FMath::Max(0.f, SQReapTune(D, TEXT("ReapDamagePerLevel"), 0.04f)) * static_cast<float>(FMath::Max(0, GS->TeamLevel - 1));
	const float KitMult = R->Abilities ? R->Abilities->GetOutgoingDamageMult() : 1.f;
	const float Crit = FMath::FRand() < R->Stats.CritChance ? 1.f + FMath::Max(0.f, R->Stats.CritDamage) : 1.f;
	const float Damage = FMath::Max(0.f, SQReapTune(D, TEXT("ReapDamage"), 8.f)) * LevelScale * R->Stats.DamageMult * KitMult * Crit;

	FSQReapHits Hits;
	Horde->ApplyReap(Me, Dir, GetRange(), GetHalfAngleDeg(), Damage, SQReapTune(D, TEXT("ReapBossBonus"), 3.f), R, Hits);
	CreditReaps(Hits);

	TArray<FVector_NetQuantize> Spots;
	for (int32 SpotIdx = 0; SpotIdx < Hits.Spots.Num() && SpotIdx < SQReapMaxSpotsSent; ++SpotIdx)
	{
		Spots.Add(FVector_NetQuantize(SQReapGround(this, Hits.Spots[SpotIdx], 40.f)));
	}
	const uint8 Flags = static_cast<uint8>((Hits.Reaped > 0 ? 1 : 0) | (Hits.bHitBoss ? 2 : 0));
	const FVector Origin = R->GetActorLocation();
	const FVector Target = Origin + FVector(Dir.X, Dir.Y, 0.f) * GetRange();
	MulticastReapSwing(FVector_NetQuantize(Origin), FVector_NetQuantize(Target), Flags, Spots);
}

// ---------------------------------------------------------------- payouts (server)

void USQReapComponent::CreditReaps(const FSQReapHits& Hits)
{
	ASQCharacter* R = GetReaper();
	ASQGameState* GS = GetGS();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !R->HasAuthority() || !GS || Hits.Reaped <= 0) return;
	const USQData* D = USQData::Get(this);
	const int32 Count = Hits.Reaped;

	// Reap Streak: a reap within StreakWindow of the last one raises it (up to StreakMax); otherwise it starts again at 1
	const float Now = ServerNow();
	const int32 StreakCap = FMath::Clamp(FMath::RoundToInt(SQReapTune(D, TEXT("StreakMax"), 5.f)), 1, 20);
	Streak = static_cast<uint8>(Now <= StreakEndsAt && Streak > 0 ? FMath::Min(StreakCap, static_cast<int32>(Streak) + 1) : 1);
	StreakEndsAt = Now + FMath::Max(0.1f, SQReapTune(D, TEXT("StreakWindow"), 2.f));
	const int32 Mult = FMath::Max(1, static_cast<int32>(Streak));

	// Souls straight into the sack, team XP x the streak, streak gold from x2 up (shared purse in co-op like any coin)
	const int32 SoulsEach = FMath::Max(0, FMath::RoundToInt(SQReapTune(D, TEXT("ReapSoulBonus"), 1.f)));
	if (SoulsEach > 0) R->AddCarried(SoulsEach * Count);
	const int32 BonusXP = FMath::RoundToInt(FMath::Max(0.f, SQReapTune(D, TEXT("ReapXP"), 1.f)) * static_cast<float>(Count * Mult));
	if (BonusXP > 0) GS->AddTeamXP(BonusXP);
	if (Mult > 1 && Horde)
	{
		const float Gold = FMath::Max(0.f, SQReapTune(D, TEXT("ReapStreakGold"), 0.25f)) * static_cast<float>(Count * (Mult - 1))
			* GS->GoldMultiplier() * GS->GoldNightGrowth();
		if (Gold > 0.f) Horde->GiveGold(R, Gold);
	}

	// The Ultimate fills from reaped souls (part of it shared with teammates nearby)
	if (R->Abilities) R->Abilities->AddUltCharge(SQReapTune(D, TEXT("UltChargePerReap"), 0.015f) * static_cast<float>(Count), true);

	// Co-op Harvest Revive: souls reaped near a downed teammate count toward bringing them back
	const float HarvestRange = SQReapTune(D, TEXT("HarvestReviveRange"), 700.f);
	if (HarvestRange > 0.f)
	{
		const double HarvestSq = static_cast<double>(HarvestRange) * HarvestRange;
		for (TActorIterator<ASQCharacter> DownIt(GetWorld()); DownIt; ++DownIt)
		{
			ASQCharacter* Downed = *DownIt;
			if (Downed == R || !Downed->bDowned || Downed->IsPardonPending()) continue;
			const FVector2D DownPos = Downed->Pos2D();
			int32 Near = 0;
			for (const FVector2D& Spot : Hits.Spots)
			{
				if (FVector2D::DistSquared(Spot, DownPos) <= HarvestSq) Near++;
			}
			if (Near > 0) Downed->AddHarvestSouls(Near, R);
		}
	}

	if (ASQPlayerState* PS = R->GetPlayerState<ASQPlayerState>())
	{
		PS->CreditQuest(TEXT("Reaps"), Count);
		if (Hits.Executed > 0) PS->CreditQuest(TEXT("Executions"), Hits.Executed);
	}
}

// ---------------------------------------------------------------- tick (server: passage, bots, streak expiry; local: gate)

void USQReapComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	LocalGate = FMath::Max(0.f, LocalGate - DeltaTime);
	const ASQCharacter* R = GetReaper();
	if (!R || !R->HasAuthority()) return;
	if (Streak > 0 && ServerNow() > StreakEndsAt)
	{
		Streak = 0;
	}
	if (!CanReapNow())
	{
		bPassageActive = false;
		return;
	}
	TickPassage();
	TickBot(DeltaTime);
}

void USQReapComponent::TickPassage()
{
	ASQCharacter* R = GetReaper();
	ASQHordeManager* Horde = GetHorde();
	if (!R || !Horde) return;
	const FVector2D Here = R->Pos2D();
	if (!R->IsDashPassing())
	{
		bPassageActive = false;
		return;
	}
	if (!bPassageActive)
	{
		// The dash just started: sweep from where it began (this frame's position) on the next ticks
		bPassageActive = true;
		LastPassagePos = Here;
	}
	const USQData* D = USQData::Get(this);
	const float Radius = ASQHordeManager::ReaperBodyRadius(R, D) + FMath::Max(0.f, SQReapTune(D, TEXT("ReapPassageRadius"), 40.f));
	FSQReapHits Hits;
	Horde->ReapAlongPath(LastPassagePos, Here, Radius, R, Hits);
	LastPassagePos = Here;
	if (Hits.Reaped <= 0) return;
	CreditReaps(Hits);
	TArray<FVector_NetQuantize> Spots;
	for (int32 SpotIdx = 0; SpotIdx < Hits.Spots.Num() && SpotIdx < SQReapMaxSpotsSent; ++SpotIdx)
	{
		Spots.Add(FVector_NetQuantize(SQReapGround(this, Hits.Spots[SpotIdx], 40.f)));
	}
	const FVector Origin = R->GetActorLocation();
	MulticastReapSwing(FVector_NetQuantize(Origin), FVector_NetQuantize(Origin + R->GetActorForwardVector() * 100.f), 1 | 4, Spots);
}

void USQReapComponent::TickBot(float DeltaTime)
{
	const ASQCharacter* R = GetReaper();
	const AController* Ctrl = R ? R->GetController() : nullptr;
	const ASQHordeManager* Horde = GetHorde();
	if (!Ctrl || Ctrl->IsPlayerController() || !Horde) return;
	BotTimer -= DeltaTime;
	if (BotTimer > 0.f || GetCooldownRemaining() > 0.f) return;
	BotTimer = 0.25f;
	FVector2D Target;
	if (Horde->FindNearestRipe(R->Pos2D(), GetRange() + 40.f, Target))
	{
		DoReap(FVector(Target.X, Target.Y, R->GetActorLocation().Z));
	}
}

// ---------------------------------------------------------------- cosmetics (every machine)

void USQReapComponent::MulticastReapSwing_Implementation(FVector_NetQuantize Origin, FVector_NetQuantize Target, uint8 Flags, const TArray<FVector_NetQuantize>& Spots)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	PlaySwingCosmetics(FVector(Origin.X, Origin.Y, Origin.Z), FVector(Target.X, Target.Y, Target.Z), Flags, Spots);
}

void USQReapComponent::PlaySwingCosmetics(const FVector& Origin, const FVector& Target, uint8 Flags, const TArray<FVector_NetQuantize>& Spots)
{
	UWorld* World = GetWorld();
	ASQCharacter* R = GetReaper();
	if (!World || !R) return;
	const bool bPassage = (Flags & 4) != 0;
	FVector Dir = (Target - Origin).GetSafeNormal2D();
	if (Dir.IsNearlyZero()) Dir = R->GetActorForwardVector().GetSafeNormal2D();
	const FVector Feet = Origin - FVector(0.f, 0.f, 85.f);
	if (!bPassage)
	{
		// The ghost scythe sweep and the reaper's swing clip + hit-stop (VFX agent: SQFx::ReapSwing / PlayReapAnim)
		SQFx::ReapSwing(World, Feet, Dir, GetRange(), GetHalfAngleDeg(), SQReapTeal);
		if (!R->bDowned) SQFx::PlayReapAnim(R);
	}
	// Reaped souls: each one flies home to the reaper
	if ((Flags & 1) != 0)
	{
		for (const FVector_NetQuantize& Spot : Spots)
		{
			SQFx::ReapSoulTrail(World, FVector(Spot.X, Spot.Y, Spot.Z), R);
		}
	}
	if ((Flags & 2) != 0)
	{
		SQFx::Flash(World, Origin + Dir * (GetRange() * 0.6f), SQReapTeal, 140.f, 0.14f);
	}
}
