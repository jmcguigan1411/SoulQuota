// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQPot.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQArena.h"
#include "SQArt.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQFx.h"
#include "SQGameState.h"
#include "SQHordeManager.h"
#include "SQInteractCue.h"
#include "SQPlayerController.h"
#include "SQPlayerState.h"
#include "SQSave.h"

namespace
{
	FLinearColor PotLootColor(uint8 Kind)
	{
		switch (Kind)
		{
		case 1: return FLinearColor(0.35f, 0.95f, 0.85f);  // XP
		case 2: return FLinearColor(0.4f, 1.f, 0.5f);      // heal
		case 3: return FLinearColor(0.85f, 0.9f, 1.f);     // banked gold
		default: return FLinearColor(1.f, 0.75f, 0.2f);    // gold
		}
	}
}

ASQPot::ASQPot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	PotRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PotRoot"));
	RootComponent = PotRoot;

	PotBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PotBody"));
	PotBody->SetupAttachment(PotRoot);
	PotBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PotBody->SetCanEverAffectNavigation(false);

	PotNeck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PotNeck"));
	PotNeck->SetupAttachment(PotRoot);
	PotNeck->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PotNeck->SetCanEverAffectNavigation(false);

	Cue = CreateDefaultSubobject<USQInteractCue>(TEXT("Cue"));
	Cue->SetupAttachment(PotRoot);
}

void ASQPot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQPot, DefId);
	DOREPLIFETIME(ASQPot, bSmashed);
	DOREPLIFETIME(ASQPot, LootKind);
}

const FSQShrineDef* ASQPot::GetDef() const
{
	const USQData* D = USQData::Get(this);
	return D ? D->FindShrine(DefId) : nullptr;
}

void ASQPot::BeginPlay()
{
	Super::BeginPlay();
	// Only the server checks for smashing; clients just show the pot (and the burst via OnRep_Smashed)
	if (!HasAuthority()) SetActorTickEnabled(false);
	if (!DefId.IsNone()) ApplyDefVisuals();
}

void ASQPot::InitPot(FName InDefId)
{
	if (!HasAuthority()) return;
	DefId = InDefId;
	ApplyDefVisuals();
	ForceNetUpdate();
}

void ASQPot::OnRep_Def()
{
	if (HasActorBegunPlay()) ApplyDefVisuals();
}

void ASQPot::ApplyDefVisuals()
{
	if (bVisualsBuilt || GetNetMode() == NM_DedicatedServer) return;
	const FSQShrineDef* Def = GetDef();
	if (!Def) return;
	bVisualsBuilt = true;
	const float H = Def->Height;
	const FVector Here = GetActorLocation();
	const float Yaw = FMath::Fmod(static_cast<float>(FMath::Abs(Here.X) * 0.31 + FMath::Abs(Here.Y) * 0.17), 360.f);
	if (UStaticMesh* Art = SQArt::Prop(Def->Model))
	{
		const float S = ASQArena::FitMeshHeight(Art, H);
		const FBoxSphereBounds Bo = Art->GetBounds();
		PotBody->SetStaticMesh(Art);
		PotBody->SetRelativeScale3D(FVector(S));
		PotBody->SetRelativeLocation(FVector(-Bo.Origin.X * S, -Bo.Origin.Y * S, -(Bo.Origin.Z - Bo.BoxExtent.Z) * S));
		PotBody->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		PotMID = SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Def->Color, 0.25f));
		if (PotMID) PotMID->SetScalarParameterValue(TEXT("Glow"), Def->Glow);
		SQArt::SetAllMaterials(PotBody, PotMID);
		PotNeck->SetVisibility(false);
	}
	else
	{
		// Primitive urn: a squat clay belly and a neck
		PotMID = ASQArena::SurfaceMID(this, Def->Color, Def->Glow);
		PotBody->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
		PotBody->SetRelativeScale3D(FVector(H * 0.0085f, H * 0.0085f, H * 0.0075f));
		PotBody->SetRelativeLocation(FVector(0.f, 0.f, H * 0.38f));
		PotBody->SetMaterial(0, PotMID);
		PotNeck->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		PotNeck->SetRelativeScale3D(FVector(H * 0.0045f, H * 0.0045f, H * 0.0035f));
		PotNeck->SetRelativeLocation(FVector(0.f, 0.f, H * 0.82f));
		PotNeck->SetMaterial(0, PotMID);
		PotNeck->SetVisibility(true);
	}
	if (Cue)
	{
		Cue->Setup(ESQCueKind::Pot, FMath::Max(70.f, H * 0.75f), H + 70.f, TEXT("SMASH"));
		Cue->SetUsed(bSmashed);
	}
	if (bSmashed)
	{
		PotBody->SetVisibility(false);
		PotNeck->SetVisibility(false);
	}
}

void ASQPot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bSmashed) return;
	UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const USQData* D = USQData::Get(this);
	const FSQShrineDef* Def = GetDef();
	if (!GS || !D || !Def || GS->Phase == ESQPhase::Waiting) return;

	if (PendingSmashTime >= 0.f)
	{
		PendingSmashTime -= DeltaSeconds;
		if (PendingSmashTime < 0.f) Smash(PendingBy.Get());
		return;
	}

	const bool bFighting = GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;
	const float Touch = Def->Radius + D->Tune(TEXT("PotTouchRadius"), 50.f);
	const float Reach = D->Tune(TEXT("PotAttackRange"), 280.f);
	const FVector Here = GetActorLocation();
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		ASQCharacter* C = *It;
		if (!IsValid(C) || C->bDowned || !C->GetPlayerState()) continue;
		const FVector L = C->GetActorLocation();
		if (FMath::Abs(static_cast<float>(L.Z - Here.Z)) > 300.f) continue; // a plateau above / below
		const float DistSq = static_cast<float>(FVector::DistSquared2D(L, Here));
		if (DistSq <= Touch * Touch)
		{
			Smash(C);
			return;
		}
		// Auto-attacks reach it: it breaks a beat later (reads as the swing landing)
		if (bFighting && DistSq <= Reach * Reach)
		{
			PendingBy = C;
			PendingSmashTime = FMath::FRandRange(0.1f, 0.35f);
			return;
		}
	}
}

void ASQPot::Smash(ASQCharacter* By)
{
	if (!HasAuthority() || bSmashed) return;
	const FSQShrineDef* Def = GetDef();
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!Def || !GS) return;
	const FVector Here = GetActorLocation();

	// No smasher given (weapon hook without an owner): the nearest standing reaper gets the personal loot
	if (!By || By->bDowned)
	{
		float BestSq = TNumericLimits<float>::Max();
		for (TActorIterator<ASQCharacter> It(World); It; ++It)
		{
			ASQCharacter* C = *It;
			if (!IsValid(C) || C->bDowned || !C->GetPlayerState()) continue;
			const float DSq = static_cast<float>(FVector::DistSquared2D(C->GetActorLocation(), Here));
			if (DSq < BestSq)
			{
				BestSq = DSq;
				By = C;
			}
		}
	}

	// Loot table: "Kind:Weight:Amount|..."
	TArray<FString> Rows;
	Def->Boosts.ParseIntoArray(Rows, TEXT("|"), true);
	TArray<FString> Kinds;
	TArray<float> Weights;
	TArray<float> Amounts;
	float Total = 0.f;
	for (const FString& Row : Rows)
	{
		TArray<FString> Cells;
		Row.ParseIntoArray(Cells, TEXT(":"), true);
		if (Cells.Num() < 3) continue;
		const float W = FMath::Max(0.f, FCString::Atof(*Cells[1]));
		Kinds.Add(Cells[0].TrimStartAndEnd());
		Weights.Add(W);
		Amounts.Add(FCString::Atof(*Cells[2]));
		Total += W;
	}
	int32 Pick = INDEX_NONE;
	if (Total > 0.f)
	{
		float Roll = FMath::FRandRange(0.f, Total);
		for (int32 i = 0; i < Kinds.Num(); ++i)
		{
			Roll -= Weights[i];
			if (Roll <= 0.f) { Pick = i; break; }
		}
		if (Pick == INDEX_NONE) Pick = Kinds.Num() - 1;
	}

	const float NightMult = 1.f + 0.5f * static_cast<float>(FMath::Max(0, GS->NightIndex));
	LootKind = 0;
	if (Pick != INDEX_NONE)
	{
		const FString& Kind = Kinds[Pick];
		const float Amount = Amounts[Pick];
		if (Kind.Equals(TEXT("Gold"), ESearchCase::IgnoreCase))
		{
			LootKind = 0;
			if (ASQPlayerState* PS = By ? Cast<ASQPlayerState>(By->GetPlayerState()) : nullptr) PS->AddGold(FMath::Max(1, FMath::RoundToInt(Amount * NightMult)));
		}
		else if (Kind.Equals(TEXT("XP"), ESearchCase::IgnoreCase))
		{
			LootKind = 1;
			if (GS->Horde) GS->Horde->DropXP(FVector2D(Here.X, Here.Y), FMath::Max(1, FMath::RoundToInt(Amount * NightMult)));
		}
		else if (Kind.Equals(TEXT("Heal"), ESearchCase::IgnoreCase))
		{
			LootKind = 2;
			if (By && !By->bDowned) By->Health = FMath::Min(By->Stats.MaxHealth, By->Health + By->Stats.MaxHealth * FMath::Clamp(Amount, 0.f, 1.f));
		}
		else if (Kind.Equals(TEXT("Coins"), ESearchCase::IgnoreCase))
		{
			LootKind = 3;
			if (APlayerState* PS = By ? By->GetPlayerState() : nullptr) MulticastSoulCoins(PS, FMath::Max(1, FMath::RoundToInt(Amount)));
		}
	}

	// Quest stat PotsBroken (Quests.csv): the smasher (or the nearest reaper picked above)
	if (ASQPlayerState* SmashPS = By ? By->GetPlayerState<ASQPlayerState>() : nullptr) SmashPS->CreditQuest(TEXT("PotsBroken"));

	bSmashed = true;
	OnRep_Smashed();
	ForceNetUpdate();
	SetActorTickEnabled(false);
	SetLifeSpan(3.f); // long enough for the replicated smash (and the coins RPC) to reach everyone
}

void ASQPot::OnRep_Smashed()
{
	if (!bSmashed) return;
	PotBody->SetVisibility(false);
	PotNeck->SetVisibility(false);
	if (Cue) Cue->SetUsed(true);
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* World = GetWorld();
	if (!World) return;
	const FSQShrineDef* Def = GetDef();
	const FVector Here = GetActorLocation() + FVector(0.f, 0.f, 40.f);
	const FLinearColor ShardColor = Def ? Def->Color : FLinearColor(0.55f, 0.48f, 0.42f);
	// Clay shards fly, then the loot colour pops
	for (int32 i = 0; i < 8; ++i)
	{
		FSQFxParticle Shard;
		Shard.Shape = ESQFxShape::Cube;
		Shard.Pos = Here;
		const float Ang = FMath::FRandRange(0.f, 2.f * PI);
		const float Speed = FMath::FRandRange(250.f, 520.f);
		Shard.Vel = FVector(FMath::Cos(Ang) * Speed, FMath::Sin(Ang) * Speed, FMath::FRandRange(300.f, 650.f));
		Shard.Color = ShardColor;
		Shard.Life = FMath::FRandRange(0.6f, 0.9f);
		Shard.SizeStart = FVector(14.f, 10.f, 6.f);
		Shard.SizeEnd = FVector(10.f, 7.f, 4.f);
		Shard.Gravity = -1800.f;
		Shard.SpinAxis = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 1.f).GetSafeNormal();
		Shard.SpinRate = FMath::FRandRange(6.f, 14.f);
		Shard.FloorZ = static_cast<float>(GetActorLocation().Z) + 3.f;
		SQFx::Emit(World, Shard);
	}
	const FLinearColor Loot = PotLootColor(LootKind);
	SQFx::Burst(World, Here, Loot, LootKind == 3 ? 24 : 12, 320.f, 0.8f, 10.f, LootKind == 2 ? 500.f : -400.f);
	SQFx::Flash(World, Here, Loot, 90.f, 0.12f);
	if (USoundBase* Sound = SQ::Sound(TEXT("Squelch"))) UGameplayStatics::PlaySoundAtLocation(this, Sound, Here, 0.6f, FMath::FRandRange(1.2f, 1.5f));
}

void ASQPot::MulticastSoulCoins_Implementation(APlayerState* To, int32 Coins)
{
	UWorld* World = GetWorld();
	if (!World || !To || Coins <= 0) return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ASQPlayerController* PC = Cast<ASQPlayerController>(It->Get());
		if (!PC || !PC->IsLocalController() || PC->PlayerState != To) continue;
		if (USQSaveGame* Save = PC->GetProfile())
		{
			Save->OvertimePay += Coins;
			Save->LifetimePay += Coins;
			USQSaveGame::Store(Save);
			PC->ShowLocalMessage(FString::Printf(TEXT("Silver urn: +%d banked gold (total %d)"), Coins, Save->OvertimePay));
		}
	}
}

int32 ASQPot::SmashInRadius(UWorld* World, const FVector2D& Center, float Radius, ASQCharacter* By)
{
	if (!World || World->GetNetMode() == NM_Client) return 0;
	int32 Broke = 0;
	const float RSq = Radius * Radius;
	for (TActorIterator<ASQPot> It(World); It; ++It)
	{
		ASQPot* Pot = *It;
		if (!IsValid(Pot) || Pot->bSmashed) continue;
		const FVector L = Pot->GetActorLocation();
		if (FVector2D::DistSquared(FVector2D(L.X, L.Y), Center) > RSq) continue;
		Pot->Smash(By);
		++Broke;
	}
	return Broke;
}
