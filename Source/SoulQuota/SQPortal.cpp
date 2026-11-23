// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQPortal.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "SQArena.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQGameMode.h"
#include "SQInteractCue.h"

ASQPortal::ASQPortal()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	auto MakePart = [this](const TCHAR* PartName, const TCHAR* Shape, const FVector& Loc, const FVector& Scale)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(Root);
		Part->SetStaticMesh(SQ::Mesh(Shape));
		Part->SetRelativeLocation(Loc);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		return Part;
	};
	// A gallows-style bell frame on a round stone plinth (~4 m tall, readable from across the arena)
	Plinth = MakePart(TEXT("Plinth"), TEXT("Cylinder"), FVector(0.f, 0.f, 20.f), FVector(2.2f, 2.2f, 0.4f));
	PostLeft = MakePart(TEXT("PostLeft"), TEXT("Cube"), FVector(0.f, -110.f, 200.f), FVector(0.25f, 0.25f, 3.4f));
	PostRight = MakePart(TEXT("PostRight"), TEXT("Cube"), FVector(0.f, 110.f, 200.f), FVector(0.25f, 0.25f, 3.4f));
	Beam = MakePart(TEXT("Beam"), TEXT("Cube"), FVector(0.f, 0.f, 380.f), FVector(0.3f, 2.6f, 0.3f));
	Bell = MakePart(TEXT("Bell"), TEXT("Cone"), FVector(0.f, 0.f, 300.f), FVector(1.1f, 1.1f, 1.2f));
	Circle = MakePart(TEXT("Circle"), TEXT("Cylinder"), FVector(0.f, 0.f, 3.f), FVector(8.f, 8.f, 0.02f));
	Circle->SetCastShadow(false);
	Cue = CreateDefaultSubobject<USQInteractCue>(TEXT("Cue"));
	Cue->SetupAttachment(Root);
}

void ASQPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQPortal, bArmed);
	DOREPLIFETIME(ASQPortal, bRung);
	DOREPLIFETIME(ASQPortal, RingProgress);
	DOREPLIFETIME(ASQPortal, Ringer);
}

ASQPortal* ASQPortal::SpawnPortal(UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_Client) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ASQPortal>(ASQPortal::StaticClass(), Location, FRotator::ZeroRotator, Params);
}

void ASQPortal::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) return;
	const FLinearColor Stone(0.22f, 0.21f, 0.25f);
	const FLinearColor Timber(0.18f, 0.11f, 0.07f);
	Plinth->SetMaterial(0, SQ::ColorMID(this, Stone));
	PostLeft->SetMaterial(0, SQ::ColorMID(this, Timber));
	PostRight->SetMaterial(0, SQ::ColorMID(this, Timber));
	Beam->SetMaterial(0, SQ::ColorMID(this, Timber));
	BellMID = SQ::ColorMID(this, FLinearColor(0.75f, 0.55f, 0.2f));
	Bell->SetMaterial(0, BellMID);
	CircleMID = SQ::ColorMID(this, FLinearColor(0.85f, 0.15f, 0.2f));
	Circle->SetMaterial(0, CircleMID);
	// The cylinder is 100 cm wide: scale the circle to the real use range
	const float UseDiameter = GetUseRange() * 2.f / 100.f;
	Circle->SetRelativeScale3D(FVector(UseDiameter, UseDiameter, 0.02f));
	if (Cue) Cue->Setup(ESQCueKind::Portal, GetUseRange(), 470.f, TEXT("THE BELL: FINISH THE OBJECTIVES"));
}

float ASQPortal::GetUseRange() const
{
	const USQData* D = USQData::Get(this);
	return FMath::Clamp(D ? D->Tune(TEXT("PortalUseRange"), 420.f) : 420.f, 150.f, 2000.f);
}

bool ASQPortal::StartRing(ASQCharacter* By)
{
	if (!HasAuthority() || !By || !bArmed || bRung || By->bDowned) return false;
	if (Ringer == By) return true;
	Ringer = By;
	RingProgress = 0.f;
	return true;
}

void ASQPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LookTime += DeltaSeconds;

	if (HasAuthority() && Ringer)
	{
		// Hold position: leaving the circle, going down or the bell being disarmed cancels the ring
		const bool bStillHere = IsValid(Ringer) && !Ringer->bDowned && bArmed && !bRung
			&& FVector::Dist2D(Ringer->GetActorLocation(), GetActorLocation()) <= GetUseRange() + 50.f;
		if (!bStillHere)
		{
			Ringer = nullptr;
			RingProgress = 0.f;
		}
		else
		{
			const USQData* D = USQData::Get(this);
			const float RingSeconds = FMath::Max(0.1f, D ? D->Tune(TEXT("PortalRingSeconds"), 2.f) : 2.f);
			RingProgress = FMath::Min(1.f, RingProgress + DeltaSeconds / RingSeconds);
			if (RingProgress >= 1.f)
			{
				ASQCharacter* RungBy = Ringer;
				Ringer = nullptr;
				RingProgress = 0.f;
				if (ASQGameMode* GM = GetWorld()->GetAuthGameMode<ASQGameMode>()) GM->RingBell(RungBy);
			}
		}
	}

	if (GetNetMode() == NM_DedicatedServer) return;
	// The bell swings while it is rung and glows when it can be; the circle pulses red when armed, dim otherwise
	const float SwingTarget = (Ringer != nullptr) ? 22.f * FMath::Sin(LookTime * 9.f) : 0.f;
	Swing = FMath::FInterpTo(Swing, SwingTarget, DeltaSeconds, 8.f);
	Bell->SetRelativeRotation(FRotator(0.f, 0.f, Swing));
	const float Pulse = 0.5f + 0.5f * FMath::Sin(LookTime * 3.f);
	if (BellMID) BellMID->SetScalarParameterValue(TEXT("Glow"), bRung ? 0.f : (bArmed ? 0.8f + 1.2f * Pulse : 0.1f));
	if (CircleMID) CircleMID->SetScalarParameterValue(TEXT("Glow"), bRung ? 0.f : (bArmed ? 0.6f + 1.4f * Pulse + 2.f * RingProgress : 0.05f));
	Circle->SetVisibility(!bRung);
	if (Cue)
	{
		Cue->SetUsed(bRung);
		Cue->SetUrgent(bArmed);
		const int32 LabelKey = Ringer != nullptr ? 100 + FMath::RoundToInt(RingProgress * 20.f) * 5 : (bArmed ? 1 : 0);
		if (LabelKey != CueLabelKey)
		{
			CueLabelKey = LabelKey;
			Cue->SetLabel(LabelKey >= 100 ? FString::Printf(TEXT("RINGING  %d%%"), LabelKey - 100)
				: (LabelKey == 1 ? FString(TEXT("RING THE BELL  [F]")) : FString(TEXT("THE BELL: FINISH THE OBJECTIVES"))));
		}
	}
}
