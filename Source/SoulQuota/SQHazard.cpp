// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQHazard.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "SQArena.h"
#include "SQArt.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQFx.h"
#include "SQGameState.h"
#include "SQInteractCue.h"
#include "SQLantern.h"

namespace
{
	/** Transform that sits a mesh's bottom-centre on BaseCentre (same idea as the arena's prop placement). */
	FTransform HazardSitTransform(const UStaticMesh* InMesh, const FVector& BaseCentre, const FQuat& Rot, float UniformScale)
	{
		const FBoxSphereBounds Bo = InMesh->GetBounds();
		const FVector LocalBottom(Bo.Origin.X, Bo.Origin.Y, Bo.Origin.Z - Bo.BoxExtent.Z);
		return FTransform(Rot, BaseCentre - Rot.RotateVector(LocalBottom * UniformScale), FVector(UniformScale));
	}

	const int32 HazardRingSegments = 72;
	const int32 HazardGloomSegments = 48;
	const int32 HazardFogPuffs = 14;
	const int32 HazardFogInnerPuffs = 7;
}

ASQHazardField::ASQHazardField()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // every machine builds its own copy from the biome seed
	FieldRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FieldRoot"));
	RootComponent = FieldRoot;
}

ESQHazardKind ASQHazardField::ParseKind(const FString& KindName)
{
	if (KindName.Equals(TEXT("Gloom"), ESearchCase::IgnoreCase)) return ESQHazardKind::Gloom;
	if (KindName.Equals(TEXT("Graves"), ESearchCase::IgnoreCase)) return ESQHazardKind::Graves;
	if (KindName.Equals(TEXT("Fog"), ESearchCase::IgnoreCase)) return ESQHazardKind::Fog;
	if (KindName.Equals(TEXT("Gusts"), ESearchCase::IgnoreCase)) return ESQHazardKind::Gusts;
	if (KindName.Equals(TEXT("Bells"), ESearchCase::IgnoreCase)) return ESQHazardKind::Bells;
	return ESQHazardKind::None;
}

// ---------------------------------------------------------------- setup

void ASQHazardField::Setup(ASQArena* InArena, const FString& HazardCell, const FString& CountCell, int32 Seed, const FLinearColor& InFogColor, const FLinearColor& InLampColor)
{
	ArenaRef = InArena;
	FogTint = InFogColor;
	LampTint = InLampColor;
	const USQData* D = USQData::Get(this);
	if (!ArenaRef || !D) return;
	ArenaHalfSize = ArenaRef->GetArenaHalf();

	// Numbers (Tuning.csv Hazard*; the *Ring* / Hop keys are the top-down pass)
	GloomWarn = FMath::Max(1.f, D->Tune(TEXT("HazardGloomWarn"), 6.f));
	GloomDark = FMath::Max(1.f, D->Tune(TEXT("HazardGloomDark"), 12.f));
	GloomPeriod = FMath::Max(GloomWarn + GloomDark + 10.f, D->Tune(TEXT("HazardGloomPeriod"), 55.f));
	GloomDps = FMath::Max(0.f, D->Tune(TEXT("HazardGloomDps"), 5.f));
	GloomRadius = FMath::Clamp(D->Tune(TEXT("HazardGloomRingRadius"), 900.f), 300.f, 2500.f);
	GloomSpeed = FMath::Clamp(D->Tune(TEXT("HazardGloomRingSpeed"), 0.045f), 0.f, 0.3f);
	GraveRadius = FMath::Clamp(D->Tune(TEXT("HazardGraveRadius"), 160.f), 60.f, 500.f);
	GravePlayerMult = FMath::Clamp(D->Tune(TEXT("HazardGravePlayerSpeed"), 0.35f), 0.1f, 1.f);
	GraveEnemyMult = FMath::Clamp(D->Tune(TEXT("HazardGraveEnemySpeed"), 0.45f), 0.f, 1.f);
	FogRadius = FMath::Clamp(D->Tune(TEXT("HazardFogRadius"), 1300.f), 300.f, 4000.f);
	FogDrift = FMath::Max(0.f, D->Tune(TEXT("HazardFogDrift"), 1500.f));
	GustWarn = FMath::Max(0.2f, D->Tune(TEXT("HazardGustWarn"), 1.2f));
	GustActive = FMath::Max(0.2f, D->Tune(TEXT("HazardGustActive"), 0.8f));
	GustPeriod = FMath::Max(GustWarn + GustActive + 1.f, D->Tune(TEXT("HazardGustPeriod"), 7.f));
	GustRadius = FMath::Clamp(D->Tune(TEXT("HazardGustRadius"), 200.f), 80.f, 600.f);
	GustLaunch = FMath::Max(300.f, D->Tune(TEXT("HazardGustHop"), 950.f));
	BellWarn = FMath::Max(0.5f, D->Tune(TEXT("HazardBellWarn"), 2.5f));
	BellSpeed = FMath::Max(100.f, D->Tune(TEXT("HazardBellRingSpeed"), 700.f));
	BellMaxRadius = FMath::Max(500.f, D->Tune(TEXT("HazardBellRingRadius"), 3200.f));
	BellPeriod = FMath::Max(BellWarn + BellMaxRadius / BellSpeed + 2.f, D->Tune(TEXT("HazardBellPeriod"), 22.f));
	BellBand = FMath::Clamp(D->Tune(TEXT("HazardBellRingBand"), 160.f), 20.f, 400.f);
	BellDamage = FMath::Max(0.f, D->Tune(TEXT("HazardBellDamage"), 18.f));
	BellClear = FMath::Max(0.f, D->Tune(TEXT("HazardBellJumpClear"), 50.f));
	DamagePerNight = FMath::Max(0.f, D->Tune(TEXT("HazardDamagePerNight"), 0.3f));

	TArray<FString> KindParts;
	HazardCell.ParseIntoArray(KindParts, TEXT("|"), true);
	TArray<FString> CountParts;
	CountCell.ParseIntoArray(CountParts, TEXT("|"), true);

	// Own stream: every machine lays the hazards out identically (the arena's props are already identical)
	FRandomStream Rand(Seed);
	const TArray<FSQTerrainPlateau>& Tiers = ArenaRef->GetPlateaus();
	for (int32 k = 0; k < KindParts.Num(); ++k)
	{
		const ESQHazardKind Kind = ParseKind(KindParts[k].TrimStartAndEnd());
		if (Kind == ESQHazardKind::None || Kinds.Contains(Kind)) continue;
		const FString CountText = CountParts.IsValidIndex(k) ? CountParts[k].TrimStartAndEnd() : FString(TEXT("3"));
		const int32 Count = FMath::Clamp(FCString::Atoi(*CountText), 0, 16);
		if (Count <= 0) continue;
		Kinds.Add(Kind);

		switch (Kind)
		{
		case ESQHazardKind::Gloom:
			// A few roaming rings (the old count was will-o-lamps; more than 3 rings would carpet a 112 m map)
			NumGloom = FMath::Clamp((Count + 1) / 2, 1, 3);
			GloomOffset = Rand.FRandRange(0.f, GloomPeriod);
			break;
		case ESQHazardKind::Graves:
			for (int32 i = 0; i < Count; ++i)
			{
				FVector2D GraveSpot;
				if (FindSpot(Rand, GraveRadius, GraveSpot)) GravePos.Add(GraveSpot);
			}
			break;
		case ESQHazardKind::Fog:
			for (int32 i = 0; i < Count; ++i)
			{
				const float FX = Rand.FRandRange(-ArenaHalfSize * 0.5f, ArenaHalfSize * 0.5f);
				const float FY = Rand.FRandRange(-ArenaHalfSize * 0.5f, ArenaHalfSize * 0.5f);
				FogAnchor.Add(FVector2D(FX, FY));
			}
			break;
		case ESQHazardKind::Gusts:
			for (int32 i = 0; i < Count; ++i)
			{
				bool bPlaced = false;
				// The first vents sit at a tier's foot and hop reapers onto its top (a shortcut up)
				if (Tiers.IsValidIndex(i))
				{
					const FSQTerrainPlateau& Tier = Tiers[i];
					for (int32 Try = 0; Try < 12 && !bPlaced; ++Try)
					{
						const float Ang = Rand.FRandRange(0.f, 2.f * PI);
						const float FootDist = Tier.RadiusAt(Ang) + Tier.Ramp + 260.f;
						const float VX = static_cast<float>(Tier.Centre.X) + FMath::Cos(Ang) * FootDist;
						const float VY = static_cast<float>(Tier.Centre.Y) + FMath::Sin(Ang) * FootDist;
						if (FMath::Abs(VX) > ArenaHalfSize * 0.9f || FMath::Abs(VY) > ArenaHalfSize * 0.9f) continue;
						if (ArenaRef->GroundSlopeDeg(VX, VY) > 12.f || ArenaRef->GroundRoughness(VX, VY, GustRadius) > 50.f) continue;
						if (!ArenaRef->IsPropFootprintFree(VX, VY, GustRadius)) continue;
						if (ArenaRef->IsReservedSpot(VX, VY, GustRadius)) continue;
						if (IsSpotBlocked(VX, VY, GustRadius)) continue;
						const float TX = static_cast<float>(Tier.Centre.X) + FMath::Cos(Ang) * Tier.RadiusAt(Ang) * 0.45f;
						const float TY = static_cast<float>(Tier.Centre.Y) + FMath::Sin(Ang) * Tier.RadiusAt(Ang) * 0.45f;
						VentPos.Add(FVector2D(VX, VY));
						VentTarget.Add(FVector(TX, TY, ArenaRef->GroundZ(TX, TY)));
						VentHasTarget.Add(true);
						bPlaced = true;
					}
				}
				if (!bPlaced)
				{
					FVector2D VentSpot;
					if (FindSpot(Rand, GustRadius, VentSpot))
					{
						VentPos.Add(VentSpot);
						VentTarget.Add(FVector::ZeroVector);
						VentHasTarget.Add(false);
						bPlaced = true;
					}
				}
				if (bPlaced) VentPhase.Add(Rand.FRandRange(0.f, GustPeriod));
			}
			break;
		case ESQHazardKind::Bells:
			for (int32 i = 0; i < Count; ++i)
			{
				FVector2D BellSpot;
				if (FindSpot(Rand, 260.f, BellSpot))
				{
					BellPos.Add(BellSpot);
					BellPhase.Add(static_cast<float>(i) * BellPeriod / static_cast<float>(Count) + Rand.FRandRange(0.f, 3.f));
				}
			}
			break;
		default:
			break;
		}
	}
	BellHitCycle.SetNum(BellPos.Num());
	BellCycleSeen.Init(-1, BellPos.Num());

	if (GetNetMode() != NM_DedicatedServer) BuildVisuals();

	UE_LOG(LogSQ, Log, TEXT("Hazards: %d kinds | gloom rings %d, graves %d, fog banks %d, vents %d, bells %d"),
		Kinds.Num(), HasKind(ESQHazardKind::Gloom) ? NumGloom : 0, GravePos.Num(), FogAnchor.Num(), VentPos.Num(), BellPos.Num());
}

bool ASQHazardField::FindSpot(FRandomStream& Rand, float Radius, FVector2D& Out) const
{
	for (int32 Try = 0; Try < 80; ++Try)
	{
		// Always the same two rolls per try (deterministic on every machine)
		const float SX = Rand.FRandRange(-ArenaHalfSize * 0.86f, ArenaHalfSize * 0.86f);
		const float SY = Rand.FRandRange(-ArenaHalfSize * 0.86f, ArenaHalfSize * 0.86f);
		// Off the mission areas (hub, landmark floors, boss arena, escort road, spawner pads) and the walls
		if (ArenaRef->IsReservedSpot(SX, SY, Radius)) continue;
		if (ArenaRef->GroundSlopeDeg(SX, SY) > 12.f) continue;
		if (ArenaRef->GroundRoughness(SX, SY, Radius) > 50.f) continue;
		if (ArenaRef->GroundZ(SX, SY) < -15.f) continue;
		if (!ArenaRef->IsPropFootprintFree(SX, SY, Radius)) continue;
		if (IsSpotBlocked(SX, SY, Radius)) continue;
		Out = FVector2D(SX, SY);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- queries

float ASQHazardField::ClockNow() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0.f;
	const AGameStateBase* GS = World->GetGameState();
	return GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : static_cast<float>(World->GetTimeSeconds());
}

bool ASQHazardField::IsHazardActive() const
{
	return ArenaRef && ArenaRef->IsGimmickActive();
}

float ASQHazardField::NightDamageMult() const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	return 1.f + DamagePerNight * static_cast<float>(GS ? FMath::Max(0, GS->NightIndex) : 0);
}

ASQCharacter* ASQHazardField::LocalReaper() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return (PC && PC->IsLocalController()) ? Cast<ASQCharacter>(PC->GetPawn()) : nullptr;
}

bool ASQHazardField::GloomState(bool& bWarning, float& SecondsLeft, float& Fill) const
{
	bWarning = false;
	SecondsLeft = 0.f;
	Fill = 0.f;
	if (!HasKind(ESQHazardKind::Gloom)) return false;
	const float Cycle = FMath::Fmod(ClockNow() + GloomOffset, GloomPeriod);
	const float DarkStart = GloomPeriod - GloomDark;
	const float WarnStart = DarkStart - GloomWarn;
	if (Cycle >= DarkStart)
	{
		SecondsLeft = GloomPeriod - Cycle;
		Fill = 1.f;
		return true;
	}
	bWarning = Cycle >= WarnStart;
	SecondsLeft = DarkStart - Cycle;
	if (bWarning) Fill = FMath::Clamp((Cycle - WarnStart) / GloomWarn, 0.f, 1.f);
	return false;
}

FVector2D ASQHazardField::GloomPos(int32 Index) const
{
	const float T = ClockNow();
	const float Fi = static_cast<float>(Index);
	return FVector2D(FMath::Sin(T * GloomSpeed + Fi * 1.9f), FMath::Cos(T * GloomSpeed * 1.3f + Fi * 1.3f)) * (ArenaHalfSize * 0.55f);
}

bool ASQHazardField::IsInDarkGloom(const FVector2D& P) const
{
	bool bWarn = false;
	float Left = 0.f;
	float Fill = 0.f;
	if (!GloomState(bWarn, Left, Fill)) return false;
	// The lantern's ring is always a safe light
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (GS && GS->Lantern)
	{
		const FVector LL = GS->Lantern->GetActorLocation();
		const float LanternLight = GS->Lantern->Radius;
		if (FVector2D::DistSquared(P, FVector2D(LL.X, LL.Y)) <= LanternLight * LanternLight) return false;
	}
	const float RSq = GloomRadius * GloomRadius;
	for (int32 i = 0; i < NumGloom; ++i)
	{
		if (FVector2D::DistSquared(P, GloomPos(i)) <= RSq) return true;
	}
	return false;
}

FVector2D ASQHazardField::FogPos(int32 Index) const
{
	if (!FogAnchor.IsValidIndex(Index)) return FVector2D::ZeroVector;
	const float T = ClockNow();
	const float Fi = static_cast<float>(Index);
	const FVector2D Drift(FMath::Sin(T * 0.021f + Fi * 2.3f), FMath::Cos(T * 0.017f + Fi * 1.7f));
	const FVector2D Raw = FogAnchor[Index] + Drift * FogDrift;
	const float Lim = ArenaHalfSize * 0.8f;
	return FVector2D(FMath::Clamp(static_cast<float>(Raw.X), -Lim, Lim), FMath::Clamp(static_cast<float>(Raw.Y), -Lim, Lim));
}

float ASQHazardField::FogDepthAt(const FVector2D& P) const
{
	float Best = 0.f;
	for (int32 i = 0; i < FogAnchor.Num(); ++i)
	{
		const float Dist = static_cast<float>(FVector2D::Distance(P, FogPos(i)));
		const float Depth = 1.f - FMath::SmoothStep(0.6f, 1.f, Dist / FogRadius);
		Best = FMath::Max(Best, Depth);
	}
	return Best;
}

bool ASQHazardField::GustState(int32 Index, bool& bWarning) const
{
	bWarning = false;
	if (!VentPhase.IsValidIndex(Index)) return false;
	const float Cycle = FMath::Fmod(ClockNow() + VentPhase[Index], GustPeriod);
	const float ActiveStart = GustPeriod - GustActive;
	bWarning = Cycle >= ActiveStart - GustWarn && Cycle < ActiveStart;
	return Cycle >= ActiveStart;
}

float ASQHazardField::BellRing(int32 Index, bool& bWarning, int32& OutCycle) const
{
	bWarning = false;
	OutCycle = -1;
	if (!BellPhase.IsValidIndex(Index)) return -1.f;
	const float T = ClockNow() + BellPhase[Index];
	OutCycle = FMath::FloorToInt(T / BellPeriod);
	const float Cycle = T - static_cast<float>(OutCycle) * BellPeriod;
	if (Cycle < BellWarn)
	{
		bWarning = true;
		return -1.f;
	}
	const float RingR = (Cycle - BellWarn) * BellSpeed;
	return RingR <= BellMaxRadius ? RingR : -1.f;
}

float ASQHazardField::PlayerSpeedMult(const FVector2D& P) const
{
	if (GravePos.Num() == 0 || !IsHazardActive()) return 1.f;
	const float RSq = GraveRadius * GraveRadius;
	for (const FVector2D& G : GravePos)
	{
		if (FVector2D::DistSquared(P, G) <= RSq) return GravePlayerMult;
	}
	return 1.f;
}

float ASQHazardField::EnemySpeedMult(const FVector2D& P) const
{
	if (GravePos.Num() == 0 || !IsHazardActive()) return 1.f;
	const float RSq = GraveRadius * GraveRadius;
	for (const FVector2D& G : GravePos)
	{
		if (FVector2D::DistSquared(P, G) <= RSq) return GraveEnemyMult;
	}
	return 1.f;
}

bool ASQHazardField::IsSpotBlocked(float X, float Y, float R) const
{
	const FVector2D P(X, Y);
	for (const FVector2D& G : GravePos)
	{
		if (FVector2D::DistSquared(P, G) < FMath::Square(GraveRadius + R + 100.f)) return true;
	}
	for (const FVector2D& V : VentPos)
	{
		if (FVector2D::DistSquared(P, V) < FMath::Square(GustRadius + R + 100.f)) return true;
	}
	for (const FVector2D& B : BellPos)
	{
		if (FVector2D::DistSquared(P, B) < FMath::Square(260.f + R + 100.f)) return true;
	}
	return false;
}

FString ASQHazardField::StatusText() const
{
	if (!IsHazardActive()) return FString();
	const ASQCharacter* Me = LocalReaper();
	if (!Me) return FString();
	const FVector2D P = Me->Pos2D();
	if (HasKind(ESQHazardKind::Gloom))
	{
		bool bWarn = false;
		float Left = 0.f;
		float Fill = 0.f;
		const bool bDark = GloomState(bWarn, Left, Fill);
		float Nearest = TNumericLimits<float>::Max();
		for (int32 i = 0; i < NumGloom; ++i) Nearest = FMath::Min(Nearest, static_cast<float>(FVector2D::Distance(P, GloomPos(i))));
		if (bDark && IsInDarkGloom(P)) return FString::Printf(TEXT("GLOOM (%.0fs): get out of the dark ring!"), FMath::CeilToFloat(Left));
		if (bWarn && Nearest < GloomRadius + 400.f) return FString::Printf(TEXT("A gloom ring is filling in - step out (%.0f)"), FMath::CeilToFloat(Left));
	}
	for (int32 b = 0; b < BellPos.Num(); ++b)
	{
		bool bWarn = false;
		int32 Cycle = 0;
		const float RingR = BellRing(b, bWarn, Cycle);
		const float Dist = static_cast<float>(FVector2D::Distance(P, BellPos[b]));
		if (Dist > BellMaxRadius) continue;
		if (bWarn) return TEXT("A bell is about to toll - DASH through its ring");
		if (RingR >= 0.f && RingR < Dist + 50.f && RingR > Dist - 900.f) return TEXT("BELL RING - DASH THROUGH IT!");
	}
	if (PlayerSpeedMult(P) < 0.99f) return TEXT("Stuck in an open grave - dash out!");
	for (int32 v = 0; v < VentPos.Num(); ++v)
	{
		bool bWarn = false;
		GustState(v, bWarn);
		if (bWarn && FVector2D::DistSquared(P, VentPos[v]) < FMath::Square(GustRadius * 3.f)) return TEXT("Gust vent about to blow - stand on it for a hop");
	}
	if (FogDepthAt(P) > 0.5f) return TEXT("Fog bank: you can't see what's inside");
	return FString();
}

// ---------------------------------------------------------------- server effects

void ASQHazardField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!ArenaRef) return;
	if (HasAuthority()) ServerTickHazards(DeltaSeconds);
	if (VentPos.Num() > 0 && IsHazardActive()) TickGusts();
	if (GetNetMode() != NM_DedicatedServer) TickVisuals(DeltaSeconds);
}

void ASQHazardField::ServerTickHazards(float DeltaSeconds)
{
	if (!IsHazardActive()) return;
	UWorld* World = GetWorld();
	if (!World) return;
	TArray<ASQCharacter*, TInlineAllocator<8>> Standing;
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		ASQCharacter* C = *It;
		if (IsValid(C) && !C->bDowned && C->GetPlayerState()) Standing.Add(C);
	}
	if (Standing.Num() == 0) return;

	// Gloom rings: inside a dark ring health drains (no soul drop); going to 0 downs the reaper through the normal hit path
	if (HasKind(ESQHazardKind::Gloom))
	{
		const float Step = 0.25f;
		GloomTickTimer -= DeltaSeconds;
		if (GloomTickTimer <= 0.f)
		{
			GloomTickTimer = FMath::Max(0.f, GloomTickTimer + Step);
			const float Drain = GloomDps * NightDamageMult() * Step;
			for (ASQCharacter* C : Standing)
			{
				if (C->bGodMode || C->bPickBubble || !IsInDarkGloom(C->Pos2D())) continue;
				const float Taken = Drain * (1.f - FMath::Clamp(C->Stats.Armor, 0.f, 0.8f));
				if (C->Health - Taken > 1.f) C->Health -= Taken;
				else C->ApplyHit(C->Health + 10.f);
			}
		}
	}

	// Bell rings: a reaper inside the band takes one hit per toll, unless it is dashing through (dash i-frames) or hopping
	for (int32 b = 0; b < BellPos.Num(); ++b)
	{
		bool bWarn = false;
		int32 Cycle = 0;
		const float RingR = BellRing(b, bWarn, Cycle);
		if (RingR < 0.f) continue;
		for (ASQCharacter* C : Standing)
		{
			const float Dist = static_cast<float>(FVector2D::Distance(C->Pos2D(), BellPos[b]));
			if (FMath::Abs(Dist - RingR) > BellBand * 0.5f + 30.f) continue;
			if (C->IsDashPassing() || C->GetFeetAboveGround() > BellClear) continue;
			const TWeakObjectPtr<AActor> Key(C);
			const int32* Last = BellHitCycle[b].Find(Key);
			if (Last && *Last == Cycle) continue;
			BellHitCycle[b].Add(Key, Cycle);
			C->ApplyHit(BellDamage * NightDamageMult());
		}
		if (BellHitCycle[b].Num() > 32)
		{
			for (auto It = BellHitCycle[b].CreateIterator(); It; ++It)
			{
				if (!It.Key().IsValid()) It.RemoveCurrent();
			}
		}
	}
}

void ASQHazardField::TickGusts()
{
	UWorld* World = GetWorld();
	if (!World) return;
	const bool bAuthority = HasAuthority();
	const float TimeNow = static_cast<float>(World->GetTimeSeconds());
	const float Grav = FMath::Max(100.f, static_cast<float>(-World->GetGravityZ()));
	const float GustRSq = GustRadius * GustRadius;
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		ASQCharacter* C = *It;
		if (!IsValid(C) || C->bDowned || !C->GetPlayerState()) continue;
		// The server launches everyone; the owning client launches itself too (instant lift, no correction pop)
		if (!bAuthority && !C->IsLocallyControlled()) continue;
		UCharacterMovementComponent* GustMove = C->GetCharacterMovement();
		if (!GustMove || GustMove->MovementMode == MOVE_None) continue;
		const FVector2D P = C->Pos2D();
		for (int32 v = 0; v < VentPos.Num(); ++v)
		{
			bool bWarn = false;
			if (!GustState(v, bWarn)) continue;
			if (FVector2D::DistSquared(P, VentPos[v]) > GustRSq) continue;
			const float VentX = static_cast<float>(VentPos[v].X);
			const float VentY = static_cast<float>(VentPos[v].Y);
			const float VentGround = ArenaRef->GroundZ(VentX, VentY);
			const UCapsuleComponent* Capsule = C->GetCapsuleComponent();
			const float HalfH = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
			const FVector From = C->GetActorLocation();
			if (static_cast<float>(From.Z) - HalfH - VentGround > 200.f) continue; // already up in the air
			const TWeakObjectPtr<AActor> Key(C);
			if (const float* Until = GustCooldownUntil.Find(Key))
			{
				if (TimeNow < *Until) continue;
			}
			if (GustCooldownUntil.Num() > 64)
			{
				for (auto CdIt = GustCooldownUntil.CreateIterator(); CdIt; ++CdIt)
				{
					if (!CdIt.Key().IsValid() || CdIt.Value() < TimeNow) CdIt.RemoveCurrent();
				}
			}
			GustCooldownUntil.Add(Key, TimeNow + GustActive + 0.5f);

			// A harmless hop (>= JumpPadSafeLaunchZ by default, so the landing is safe): onto the tier top, or onward
			FVector Launch(0.f, 0.f, GustLaunch);
			if (VentHasTarget.IsValidIndex(v) && VentHasTarget[v])
			{
				const FVector& Target = VentTarget[v];
				const float Rise = static_cast<float>(Target.Z) - VentGround;
				const float Disc = GustLaunch * GustLaunch - 2.f * Grav * Rise;
				const float FlightT = Disc > 0.f ? (GustLaunch + FMath::Sqrt(Disc)) / Grav : GustLaunch / Grav;
				const float ToX = static_cast<float>(Target.X - From.X);
				const float ToY = static_cast<float>(Target.Y - From.Y);
				const float Dist = FMath::Sqrt(ToX * ToX + ToY * ToY);
				if (Dist > 1.f)
				{
					const float Flat = FMath::Clamp(Dist / FMath::Max(0.2f, FlightT), 0.f, 1200.f);
					Launch.X = ToX / Dist * Flat;
					Launch.Y = ToY / Dist * Flat;
				}
			}
			else
			{
				// Free vent: carries the reaper on the way it was going (an escape hop over the horde)
				const FVector2D Run(GustMove->Velocity.X, GustMove->Velocity.Y);
				const FVector2D Onward = Run.SizeSquared() > 100.0 ? Run.GetSafeNormal() : (P - VentPos[v]).GetSafeNormal();
				Launch.X = Onward.X * 350.f;
				Launch.Y = Onward.Y * 350.f;
			}
			C->LaunchCharacter(Launch, true, true);
			break;
		}
	}
}

// ---------------------------------------------------------------- visuals

UStaticMeshComponent* ASQHazardField::AddPart(FName Shape, const FVector& Loc, const FRotator& Rot, const FVector& Scale, UMaterialInstanceDynamic* InMID)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(SQ::Mesh(Shape));
	if (InMID) Part->SetMaterial(0, InMID);
	Part->SetupAttachment(FieldRoot);
	Part->SetRelativeLocation(Loc);
	Part->SetRelativeRotation(Rot);
	Part->SetRelativeScale3D(Scale);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(false);
	Part->SetCanEverAffectNavigation(false);
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

UInstancedStaticMeshComponent* ASQHazardField::AddISM(FName Shape, UMaterialInstanceDynamic* InMID)
{
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetStaticMesh(SQ::Mesh(Shape));
	if (InMID) ISM->SetMaterial(0, InMID);
	ISM->SetupAttachment(FieldRoot);
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(false);
	ISM->SetCanEverAffectNavigation(false);
	ISM->RegisterComponent();
	PartISMs.Add(ISM);
	return ISM;
}

void ASQHazardField::AddGroundRing(TArray<FTransform>& Out, const FVector2D& C, float R, float Width, float Lift, int32 Segments) const
{
	if (!ArenaRef || R < 1.f || Segments < 3) return;
	const float SegLen = 2.f * PI * R / static_cast<float>(Segments);
	for (int32 s = 0; s < Segments; ++s)
	{
		const float Ang = static_cast<float>(s) * 2.f * PI / static_cast<float>(Segments);
		const float SX = static_cast<float>(C.X) + FMath::Cos(Ang) * R;
		const float SY = static_cast<float>(C.Y) + FMath::Sin(Ang) * R;
		if (FMath::Abs(SX) > ArenaHalfSize || FMath::Abs(SY) > ArenaHalfSize) continue;
		const float SZ = ArenaRef->GroundZ(SX, SY) + Lift;
		Out.Add(FTransform(FRotator(0.f, FMath::RadiansToDegrees(Ang) + 90.f, 0.f), FVector(SX, SY, SZ), FVector(SegLen / 100.f * 1.1f, Width / 100.f, 0.04f)));
	}
}

void ASQHazardField::BuildVisuals()
{
	UWorld* World = GetWorld();
	if (!World || !ArenaRef) return;
	UMaterialInstanceDynamic* PropArtMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor(0.85f, 0.8f, 0.75f));
	const FLinearColor RimPale(0.75f, 0.72f, 0.62f);

	// --- Gloom rings: a glowing violet outline (always), a dark disc that fills in, and a little orb at the centre ---
	if (HasKind(ESQHazardKind::Gloom))
	{
		GloomEdgeMID = ASQArena::SurfaceMID(this, FLinearColor(0.55f, 0.25f, 1.f), 1.6f);
		GloomEdgeISM = AddISM(TEXT("Cube"), GloomEdgeMID);
		GloomDiscISM = AddISM(TEXT("Cylinder"), ASQArena::SurfaceMID(this, FLinearColor(0.025f, 0.01f, 0.05f), 0.f));
		UMaterialInstanceDynamic* OrbMID = ASQArena::SurfaceMID(this, FLinearColor(0.6f, 0.3f, 1.f), 5.f);
		for (int32 i = 0; i < NumGloom; ++i) GloomOrbs.Add(AddPart(TEXT("Sphere"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.7f), OrbMID));
	}

	// --- Graves: a pale rim, a dark pit, a dirt mound and a marker ---
	if (GravePos.Num() > 0)
	{
		UMaterialInstanceDynamic* RimMID = ASQArena::SurfaceMID(this, RimPale, 0.f);
		UMaterialInstanceDynamic* PitMID = ASQArena::SurfaceMID(this, FLinearColor(0.02f, 0.015f, 0.03f), 0.f);
		UMaterialInstanceDynamic* DirtMID = ASQArena::SurfaceMID(this, FLinearColor(0.16f, 0.1f, 0.06f), 0.f);
		UStaticMesh* MoundArt = SQArt::Prop(TEXT("grave_A_destroyed"));
		UStaticMesh* MarkerArt = SQArt::Prop(TEXT("gravemarker_A"));
		for (const FVector2D& G : GravePos)
		{
			const float GX = static_cast<float>(G.X);
			const float GY = static_cast<float>(G.Y);
			const FVector N = ArenaRef->GroundNormal(GX, GY);
			const FRotator Tilt = FRotationMatrix::MakeFromZ(N).Rotator();
			AddPart(TEXT("Cylinder"), FVector(GX, GY, ArenaRef->GroundZ(GX, GY) + 2.f), Tilt, FVector((GraveRadius + 28.f) / 50.f, (GraveRadius + 28.f) / 50.f, 0.02f), RimMID);
			AddPart(TEXT("Cylinder"), FVector(GX, GY, ArenaRef->GroundZ(GX, GY) + 4.f), Tilt, FVector(GraveRadius / 50.f, GraveRadius / 50.f, 0.02f), PitMID);
			const float MoundX = GX + GraveRadius * 0.95f;
			const float MoundZ = ArenaRef->GroundZ(MoundX, GY);
			if (MoundArt && PropArtMID)
			{
				UStaticMeshComponent* Mound = NewObject<UStaticMeshComponent>(this);
				Mound->SetStaticMesh(MoundArt);
				SQArt::SetAllMaterials(Mound, PropArtMID);
				Mound->SetupAttachment(FieldRoot);
				const float MS = ASQArena::FitMeshHeight(MoundArt, 70.f);
				Mound->SetRelativeTransform(HazardSitTransform(MoundArt, FVector(MoundX, GY, MoundZ - 4.f), FQuat(FRotator(0.f, 90.f, 0.f)), MS));
				Mound->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mound->SetCastShadow(false);
				Mound->RegisterComponent();
				Parts.Add(Mound);
			}
			else
			{
				AddPart(TEXT("Sphere"), FVector(MoundX, GY, MoundZ + 5.f), FRotator::ZeroRotator, FVector(0.8f, 1.6f, 0.45f), DirtMID);
			}
			const float MarkX = GX - GraveRadius * 1.1f;
			const float MarkZ = ArenaRef->GroundZ(MarkX, GY);
			if (MarkerArt && PropArtMID)
			{
				UStaticMeshComponent* Marker = NewObject<UStaticMeshComponent>(this);
				Marker->SetStaticMesh(MarkerArt);
				SQArt::SetAllMaterials(Marker, PropArtMID);
				Marker->SetupAttachment(FieldRoot);
				const float MkS = ASQArena::FitMeshHeight(MarkerArt, 90.f);
				Marker->SetRelativeTransform(HazardSitTransform(MarkerArt, FVector(MarkX, GY, MarkZ - 4.f), FQuat(FRotator(0.f, 90.f, 0.f)), MkS));
				Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Marker->SetCastShadow(false);
				Marker->RegisterComponent();
				Parts.Add(Marker);
			}
			else
			{
				AddPart(TEXT("Cube"), FVector(MarkX, GY, MarkZ + 45.f), FRotator(0.f, 0.f, 6.f), FVector(0.15f, 0.6f, 0.9f), DirtMID);
			}
		}
	}

	// --- Fog banks: a low cloud layer of clay puffs drifting with each bank, outlined on the ground ---
	if (FogAnchor.Num() > 0)
	{
		const FLinearColor PuffColor(FMath::Min(1.f, FogTint.R * 2.2f + 0.1f), FMath::Min(1.f, FogTint.G * 2.2f + 0.1f), FMath::Min(1.f, FogTint.B * 2.2f + 0.12f), 1.f);
		FogISM = AddISM(TEXT("Sphere"), ASQArena::SurfaceMID(this, PuffColor, 0.25f));
		FogEdgeISM = AddISM(TEXT("Cube"), ASQArena::SurfaceMID(this, PuffColor * 1.3f, 0.4f));
	}

	// --- Gust vents: a pale rim, an iron grate ring and a cap that glows before it blows ---
	if (VentPos.Num() > 0)
	{
		UMaterialInstanceDynamic* RimMID = ASQArena::SurfaceMID(this, RimPale, 0.f);
		UMaterialInstanceDynamic* IronMID = ASQArena::SurfaceMID(this, FLinearColor(0.08f, 0.08f, 0.09f), 0.f);
		for (int32 v = 0; v < VentPos.Num(); ++v)
		{
			const float VX = static_cast<float>(VentPos[v].X);
			const float VY = static_cast<float>(VentPos[v].Y);
			const float VZ = ArenaRef->GroundZ(VX, VY);
			const FRotator Tilt = FRotationMatrix::MakeFromZ(ArenaRef->GroundNormal(VX, VY)).Rotator();
			AddPart(TEXT("Cylinder"), FVector(VX, VY, VZ + 2.f), Tilt, FVector((GustRadius + 25.f) / 50.f, (GustRadius + 25.f) / 50.f, 0.06f), RimMID);
			AddPart(TEXT("Cylinder"), FVector(VX, VY, VZ + 4.f), Tilt, FVector(GustRadius / 50.f, GustRadius / 50.f, 0.1f), IronMID);
			UMaterialInstanceDynamic* CapMID = ASQArena::SurfaceMID(this, FLinearColor(0.35f, 0.85f, 1.f), 0.3f);
			VentMIDs.Add(CapMID);
			VentCaps.Add(AddPart(TEXT("Cylinder"), FVector(VX, VY, VZ + 8.f), Tilt, FVector(GustRadius * 0.7f / 50.f, GustRadius * 0.7f / 50.f, 0.1f), CapMID));
		}
	}

	// --- Bells: a frame (art arch or two posts) with a glowing bell, the ring band and a reach outline ---
	if (BellPos.Num() > 0)
	{
		UMaterialInstanceDynamic* WoodMID = ASQArena::SurfaceMID(this, FLinearColor(0.2f, 0.12f, 0.07f), 0.f);
		UStaticMesh* ArchArt = SQArt::Prop(TEXT("arch"));
		for (int32 b = 0; b < BellPos.Num(); ++b)
		{
			const float BX = static_cast<float>(BellPos[b].X);
			const float BY = static_cast<float>(BellPos[b].Y);
			const float BZ = ArenaRef->GroundZ(BX, BY);
			if (ArchArt && PropArtMID)
			{
				UStaticMeshComponent* Arch = NewObject<UStaticMeshComponent>(this);
				Arch->SetStaticMesh(ArchArt);
				SQArt::SetAllMaterials(Arch, PropArtMID);
				Arch->SetupAttachment(FieldRoot);
				const float AS = ASQArena::FitMeshHeight(ArchArt, 300.f);
				Arch->SetRelativeTransform(HazardSitTransform(ArchArt, FVector(BX, BY, BZ - 4.f), FQuat::Identity, AS));
				Arch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				// Taller than 2 m: the top-down camera may fade it
				Arch->ComponentTags.AddUnique(FName(TEXT("SQFadeable")));
				Arch->RegisterComponent();
				Parts.Add(Arch);
			}
			else
			{
				UStaticMeshComponent* PostL = AddPart(TEXT("Cylinder"), FVector(BX - 120.f, BY, BZ + 140.f), FRotator::ZeroRotator, FVector(0.25f, 0.25f, 2.8f), WoodMID);
				UStaticMeshComponent* PostR = AddPart(TEXT("Cylinder"), FVector(BX + 120.f, BY, BZ + 140.f), FRotator::ZeroRotator, FVector(0.25f, 0.25f, 2.8f), WoodMID);
				UStaticMeshComponent* Beam = AddPart(TEXT("Cube"), FVector(BX, BY, BZ + 275.f), FRotator::ZeroRotator, FVector(2.8f, 0.3f, 0.3f), WoodMID);
				PostL->ComponentTags.AddUnique(FName(TEXT("SQFadeable")));
				PostR->ComponentTags.AddUnique(FName(TEXT("SQFadeable")));
				Beam->ComponentTags.AddUnique(FName(TEXT("SQFadeable")));
			}
			UMaterialInstanceDynamic* BellMID = ASQArena::SurfaceMID(this, FLinearColor(1.f, 0.68f, 0.22f), 0.6f);
			BellMIDs.Add(BellMID);
			UStaticMeshComponent* BellBody = AddPart(TEXT("Cone"), FVector(BX, BY, BZ + 200.f), FRotator::ZeroRotator, FVector(1.f, 1.f, 1.1f), BellMID);
			BellBody->SetCastShadow(true);
			BellBody->ComponentTags.AddUnique(FName(TEXT("SQFadeable")));
			BellBodies.Add(BellBody);
		}
		RingISM = AddISM(TEXT("Cube"), ASQArena::SurfaceMID(this, FLinearColor(1.f, 0.7f, 0.25f), 3.f));
		BellReachISM = AddISM(TEXT("Cube"), ASQArena::SurfaceMID(this, FLinearColor(1.f, 0.55f, 0.2f), 0.8f));
	}
}

void ASQHazardField::TickVisuals(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !ArenaRef) return;
	VisualClock += DeltaSeconds;
	const bool bActive = IsHazardActive();
	ASQCharacter* Me = LocalReaper();

	// --- Gloom rings (refreshed 30x a second) ---
	if (HasKind(ESQHazardKind::Gloom) && GloomEdgeISM && GloomDiscISM)
	{
		bool bWarn = false;
		float Left = 0.f;
		float Fill = 0.f;
		const bool bDark = bActive && GloomState(bWarn, Left, Fill);
		bWarn = bWarn && bActive;
		const uint8 EdgeTone = bDark ? 2 : (bWarn ? 1 : 0);
		if (EdgeTone != GloomEdgeTone && GloomEdgeMID)
		{
			GloomEdgeTone = EdgeTone;
			GloomEdgeMID->SetVectorParameterValue(TEXT("Color"), EdgeTone == 0 ? FLinearColor(0.55f, 0.25f, 1.f) : USQInteractCue::KindColor(ESQCueKind::Hazard));
			GloomEdgeMID->SetScalarParameterValue(TEXT("Glow"), EdgeTone == 0 ? 1.6f : (EdgeTone == 1 ? 2.6f : 3.2f));
		}
		for (int32 i = 0; i < GloomOrbs.Num(); ++i)
		{
			const FVector2D GP = GloomPos(i);
			const float GX = static_cast<float>(GP.X);
			const float GY = static_cast<float>(GP.Y);
			if (GloomOrbs[i])
			{
				GloomOrbs[i]->SetVisibility(bActive);
				GloomOrbs[i]->SetRelativeLocation(FVector(GX, GY, ArenaRef->GroundZ(GX, GY) + 60.f + 15.f * FMath::Sin(VisualClock * 2.f + static_cast<float>(i))));
			}
		}
		GloomRefreshTimer -= DeltaSeconds;
		if (GloomRefreshTimer <= 0.f)
		{
			GloomRefreshTimer = 1.f / 30.f;
			ScratchGloomDisc.Reset();
			ScratchGloomEdge.Reset();
			if (bActive)
			{
				for (int32 i = 0; i < NumGloom; ++i)
				{
					const FVector2D GP = GloomPos(i);
					// The outline pulses faster as it fills; dark rings get a thicker edge
					const float EdgeW = bDark ? 70.f : (bWarn ? 45.f + 20.f * FMath::Sin(VisualClock * 12.f) : 30.f);
					AddGroundRing(ScratchGloomEdge, GP, GloomRadius, EdgeW, 9.f, HazardGloomSegments);
					const float DiscR = bDark ? GloomRadius : GloomRadius * Fill;
					if (DiscR > 20.f)
					{
						const float GX = static_cast<float>(GP.X);
						const float GY = static_cast<float>(GP.Y);
						const float Probe = FMath::Min(DiscR, 400.f);
						const float ZC = ArenaRef->GroundZ(GX, GY);
						const float ZXP = ArenaRef->GroundZ(GX + Probe, GY);
						const float ZXM = ArenaRef->GroundZ(GX - Probe, GY);
						const float ZYP = ArenaRef->GroundZ(GX, GY + Probe);
						const float ZYM = ArenaRef->GroundZ(GX, GY - Probe);
						const FVector DiscN = FVector(-(ZXP - ZXM) / (2.f * Probe), -(ZYP - ZYM) / (2.f * Probe), 1.f).GetSafeNormal();
						const float DZ = FMath::Max(ZC, 0.25f * (ZXP + ZXM + ZYP + ZYM)) + 5.f;
						ScratchGloomDisc.Add(FTransform(FRotationMatrix::MakeFromZ(DiscN).Rotator(), FVector(GX, GY, DZ), FVector(DiscR / 50.f, DiscR / 50.f, 0.01f)));
					}
				}
			}
			if (ScratchGloomEdge.Num() > 0 || bGloomShown)
			{
				GloomEdgeISM->ClearInstances();
				GloomDiscISM->ClearInstances();
				if (ScratchGloomEdge.Num() > 0) GloomEdgeISM->AddInstances(ScratchGloomEdge, false, true);
				if (ScratchGloomDisc.Num() > 0) GloomDiscISM->AddInstances(ScratchGloomDisc, false, true);
				bGloomShown = ScratchGloomEdge.Num() > 0;
			}
		}
		// The dark is gnawing at you: little violet wisps peel off the local reaper
		if (bDark && Me && !Me->bDowned && IsInDarkGloom(Me->Pos2D()))
		{
			LocalFxTimer -= DeltaSeconds;
			if (LocalFxTimer <= 0.f)
			{
				LocalFxTimer = 0.35f;
				SQFx::Burst(World, Me->GetActorLocation(), FLinearColor(0.45f, 0.2f, 0.8f), 5, 160.f, 0.5f, 8.f, 300.f);
			}
		}
	}

	// --- Fog banks: a low cloud layer that hides what is under it (15x a second), outlined on the ground ---
	if (FogAnchor.Num() > 0 && FogISM)
	{
		FogRefreshTimer -= DeltaSeconds;
		if (FogRefreshTimer <= 0.f)
		{
			FogRefreshTimer = 1.f / 15.f;
			ScratchFog.Reset();
			ScratchFogEdge.Reset();
			for (int32 i = 0; i < FogAnchor.Num(); ++i)
			{
				const FVector2D C = FogPos(i);
				const float Spin = VisualClock * 0.05f * ((i % 2) == 0 ? 1.f : -1.f);
				const int32 TotalPuffs = HazardFogPuffs + HazardFogInnerPuffs + 1;
				for (int32 j = 0; j < TotalPuffs; ++j)
				{
					const float Fj = static_cast<float>(j);
					const bool bOuter = j < HazardFogPuffs;
					const bool bCentre = j == TotalPuffs - 1;
					const int32 RingCount = bOuter ? HazardFogPuffs : HazardFogInnerPuffs;
					const float Ang = Fj * 2.f * PI / static_cast<float>(RingCount) + Spin * (bOuter ? 1.f : -1.4f);
					const float Ring = bCentre ? 0.f : FogRadius * (bOuter ? 0.74f + 0.1f * FMath::Sin(Fj * 1.7f + static_cast<float>(i)) : 0.38f);
					const float PX = static_cast<float>(C.X) + FMath::Cos(Ang) * Ring;
					const float PY = static_cast<float>(C.Y) + FMath::Sin(Ang) * Ring;
					const float Size = (bOuter ? 300.f : 380.f) + 100.f * (0.5f + 0.5f * FMath::Sin(Fj * 2.9f + static_cast<float>(i) * 0.7f));
					const float PZ = ArenaRef->GroundZ(PX, PY) + 120.f + 20.f * FMath::Sin(VisualClock * 0.6f + Fj);
					ScratchFog.Add(FTransform(FRotator::ZeroRotator, FVector(PX, PY, PZ), FVector(Size / 50.f, Size / 50.f, Size * 0.3f / 50.f)));
				}
				AddGroundRing(ScratchFogEdge, C, FogRadius, 35.f, 7.f, 40);
			}
			FogISM->ClearInstances();
			FogISM->AddInstances(ScratchFog, false, true);
			if (FogEdgeISM)
			{
				FogEdgeISM->ClearInstances();
				if (bActive) FogEdgeISM->AddInstances(ScratchFogEdge, false, true);
			}
		}
	}

	// --- Gust vents: glow up before they blow, a column of sparks while they blow ---
	FxTimer -= DeltaSeconds;
	const bool bFxFrame = FxTimer <= 0.f;
	if (bFxFrame) FxTimer = 0.05f;
	for (int32 v = 0; v < VentPos.Num() && v < VentMIDs.Num(); ++v)
	{
		bool bWarn = false;
		const bool bBlowing = bActive && GustState(v, bWarn);
		bWarn = bWarn && bActive;
		const float Glow = bBlowing ? 7.f : (bWarn ? 2.f + 2.f * FMath::Sin(VisualClock * 20.f) : 0.3f);
		if (VentMIDs[v]) VentMIDs[v]->SetScalarParameterValue(TEXT("Glow"), Glow);
		if (bBlowing && bFxFrame)
		{
			const float VX = static_cast<float>(VentPos[v].X);
			const float VY = static_cast<float>(VentPos[v].Y);
			const float VZ = ArenaRef->GroundZ(VX, VY);
			for (int32 k = 0; k < 3; ++k)
			{
				FSQFxParticle Puff;
				const float Ang = FMath::FRandRange(0.f, 2.f * PI);
				const float Rad = FMath::FRandRange(0.f, GustRadius * 0.8f);
				Puff.Pos = FVector(VX + FMath::Cos(Ang) * Rad, VY + FMath::Sin(Ang) * Rad, VZ + 20.f);
				Puff.Vel = FVector(0.f, 0.f, FMath::FRandRange(900.f, 1300.f));
				Puff.Color = FLinearColor(0.55f, 0.95f, 1.f);
				Puff.Life = 0.6f;
				Puff.SizeStart = FVector(18.f);
				Puff.SizeEnd = FVector(6.f);
				Puff.Drag = 1.f;
				SQFx::Emit(World, Puff);
			}
		}
	}

	// --- Bells: sway + glow while about to toll (its reach outlined on the ground), the toll, and the ring racing out ---
	ScratchRing.Reset();
	ScratchReach.Reset();
	for (int32 b = 0; b < BellPos.Num(); ++b)
	{
		bool bWarn = false;
		int32 Cycle = 0;
		const float RingR = bActive ? BellRing(b, bWarn, Cycle) : -1.f;
		bWarn = bWarn && bActive;
		const float BX = static_cast<float>(BellPos[b].X);
		const float BY = static_cast<float>(BellPos[b].Y);
		const float BZ = ArenaRef->GroundZ(BX, BY);
		if (BellBodies.IsValidIndex(b) && BellBodies[b])
		{
			const float Sway = bWarn ? 18.f * FMath::Sin(VisualClock * 9.f) : (RingR >= 0.f && RingR < 600.f ? 8.f * FMath::Sin(VisualClock * 6.f) : 0.f);
			BellBodies[b]->SetRelativeRotation(FRotator(0.f, 0.f, Sway));
		}
		if (BellMIDs.IsValidIndex(b) && BellMIDs[b])
		{
			BellMIDs[b]->SetScalarParameterValue(TEXT("Glow"), bWarn ? 2.5f + 1.5f * FMath::Sin(VisualClock * 14.f) : (RingR >= 0.f ? 1.5f : 0.6f));
		}
		if (bWarn && FMath::Fmod(VisualClock, 0.5f) < 0.32f) AddGroundRing(ScratchReach, BellPos[b], BellMaxRadius, 40.f, 6.f, HazardRingSegments);
		if (RingR >= 0.f && BellCycleSeen.IsValidIndex(b) && BellCycleSeen[b] != Cycle)
		{
			BellCycleSeen[b] = Cycle;
			const FVector BellTop(BX, BY, BZ + 200.f);
			SQFx::Flash(World, BellTop, FLinearColor(1.f, 0.75f, 0.3f), 160.f, 0.25f);
			SQFx::Ring(World, FVector(BX, BY, BZ + 20.f), FLinearColor(1.f, 0.7f, 0.25f), 24, 900.f, 0.6f, 14.f);
			if (USoundBase* BellSound = SQ::Sound(TEXT("Bell"))) UGameplayStatics::PlaySoundAtLocation(this, BellSound, BellTop, 1.f, 0.8f);
		}
		if (RingR >= 0.f) AddGroundRing(ScratchRing, BellPos[b], FMath::Max(RingR, 30.f), BellBand, 12.f, HazardRingSegments);
	}
	if (RingISM && (ScratchRing.Num() > 0 || bRingShown))
	{
		RingISM->ClearInstances();
		if (ScratchRing.Num() > 0) RingISM->AddInstances(ScratchRing, false, true);
		bRingShown = ScratchRing.Num() > 0;
	}
	if (BellReachISM && (ScratchReach.Num() > 0 || bReachShown))
	{
		BellReachISM->ClearInstances();
		if (ScratchReach.Num() > 0) BellReachISM->AddInstances(ScratchReach, false, true);
		bReachShown = ScratchReach.Num() > 0;
	}
}
