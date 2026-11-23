// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQInteractCue.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "SQArena.h"
#include "SQData.h"
#include "SQGameState.h"

namespace
{
	constexpr int32 CueRingSegments = 28;
	const FLinearColor CueSpentGrey(0.32f, 0.33f, 0.36f);
}

USQInteractCue::USQInteractCue()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // Setup turns it on (clients only)
	PrimaryComponentTick.TickInterval = 1.f / 30.f;
}

FLinearColor USQInteractCue::KindColor(ESQCueKind InKind)
{
	switch (InKind)
	{
	case ESQCueKind::Shrine: return FLinearColor(0.25f, 0.9f, 1.f);
	case ESQCueKind::Chest: return FLinearColor(1.f, 0.72f, 0.2f);
	case ESQCueKind::FreeChest: return FLinearColor(1.f, 0.88f, 0.35f);
	case ESQCueKind::Pot: return FLinearColor(0.92f, 0.9f, 0.85f);
	case ESQCueKind::Portal: return FLinearColor(1.f, 0.22f, 0.3f);
	case ESQCueKind::Hazard: return FLinearColor(1.f, 0.38f, 0.12f);
	case ESQCueKind::Collect: return FLinearColor(0.35f, 1.f, 0.45f);
	default: return FLinearColor::White;
	}
}

void USQInteractCue::Setup(ESQCueKind InKind, float InRadius, float InIconHeight, const FString& InLabel)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	Kind = InKind;
	Radius = FMath::Max(30.f, InRadius);
	IconHeight = InIconHeight;
	LabelText = InLabel;
	if (const USQData* D = USQData::Get(this))
	{
		LabelRange = D->Tune(TEXT("CueLabelRange"), 750.f);
		Brightness = D->Tune(TEXT("CueBrightness"), 1.f);
	}
	// Neighbouring pots must not pulse in lockstep
	const FVector Here = GetComponentLocation();
	PhaseOffset = FMath::Fmod(static_cast<float>(FMath::Abs(Here.X) * 0.013 + FMath::Abs(Here.Y) * 0.007), 2.f * PI);
	BuildParts();
	ApplyColors();
	SetComponentTickEnabled(!bUsed);
}

void USQInteractCue::BuildParts()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return;
	if (!bBuilt)
	{
		bBuilt = true;
		RingISM = NewObject<UInstancedStaticMeshComponent>(OwnerActor);
		RingISM->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
		Pad = NewObject<UStaticMeshComponent>(OwnerActor);
		Pad->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		Gem = NewObject<UStaticMeshComponent>(OwnerActor);
		Gem->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
		Label = NewObject<UTextRenderComponent>(OwnerActor);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextBottom);
		Label->SetWorldSize(36.f);
		// Readability: a dark twin a hair behind the label (world text has no outline of its own)
		LabelShadow = NewObject<UTextRenderComponent>(OwnerActor);
		LabelShadow->SetHorizontalAlignment(EHTA_Center);
		LabelShadow->SetVerticalAlignment(EVRTA_TextBottom);
		LabelShadow->SetWorldSize(36.f);
		LabelShadow->SetTextRenderColor(FColor(8, 5, 14, 255));
		UPrimitiveComponent* Parts[5] = { RingISM, Pad, Gem, Label, LabelShadow };
		for (UPrimitiveComponent* Part : Parts)
		{
			Part->SetupAttachment(this);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			Part->SetCastShadow(false);
			Part->RegisterComponent();
		}
		RingMID = ASQArena::SurfaceMID(OwnerActor, FLinearColor::White, 2.f);
		PadMID = ASQArena::SurfaceMID(OwnerActor, FLinearColor::White, 0.15f);
		GemMID = ASQArena::SurfaceMID(OwnerActor, FLinearColor::White, 4.f);
		RingISM->SetMaterial(0, RingMID);
		Pad->SetMaterial(0, PadMID);
		Gem->SetMaterial(0, GemMID);
	}
	// A faint pad just inside the ring reads as a painted use-area from the top-down camera
	Pad->SetRelativeScale3D(FVector(Radius / 50.f * 0.96f, Radius / 50.f * 0.96f, 0.012f));
	Pad->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
	Gem->SetRelativeScale3D(FVector(0.22f));
	Label->SetText(FText::FromString(LabelText));
	Label->SetRelativeLocation(FVector(0.f, 0.f, IconHeight + 45.f));
	Label->SetVisibility(false);
	if (LabelShadow)
	{
		LabelShadow->SetText(FText::FromString(LabelText));
		LabelShadow->SetVisibility(false);
	}
	bGrounded = false;
	GroundRing();
}

void USQInteractCue::GroundRing()
{
	if (!RingISM) return;
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	bGrounded = Arena && Arena->HasTerrain();
	const FVector Here = GetComponentLocation();
	const float SegLen = 2.f * PI * Radius / CueRingSegments;
	RingISM->ClearInstances();
	for (int32 i = 0; i < CueRingSegments; ++i)
	{
		const float Ang = 2.f * PI * static_cast<float>(i) / CueRingSegments;
		const float RX = FMath::Cos(Ang) * Radius;
		const float RY = FMath::Sin(Ang) * Radius;
		const float RZ = bGrounded ? Arena->GroundZ(static_cast<float>(Here.X) + RX, static_cast<float>(Here.Y) + RY) - static_cast<float>(Here.Z) + 5.f : 5.f;
		// Every seventh segment is a longer, wider tick: reads as a designed marker, not a hoop
		const bool bTick = (i % 7) == 0;
		RingISM->AddInstance(FTransform(FRotator(0.f, FMath::RadiansToDegrees(Ang) + 90.f, 0.f), FVector(RX, RY, RZ),
			FVector(SegLen / 100.f * (bTick ? 0.95f : 0.62f), bTick ? 0.2f : 0.1f, 0.04f)), false);
	}
}

void USQInteractCue::ApplyColors()
{
	const FLinearColor Base = KindColor(Kind);
	const FLinearColor RingColor = bUsed ? CueSpentGrey * 0.6f : Base;
	if (RingMID)
	{
		RingMID->SetVectorParameterValue(TEXT("Color"), RingColor);
		RingMID->SetScalarParameterValue(TEXT("Glow"), bUsed ? 0.f : 2.f * Brightness);
	}
	if (PadMID)
	{
		PadMID->SetVectorParameterValue(TEXT("Color"), Base * 0.22f);
		PadMID->SetScalarParameterValue(TEXT("Glow"), 0.12f * Brightness);
	}
	if (GemMID)
	{
		GemMID->SetVectorParameterValue(TEXT("Color"), Base);
		GemMID->SetScalarParameterValue(TEXT("Glow"), 4.f * Brightness);
	}
	if (Pad) Pad->SetVisibility(!bUsed);
	if (Gem) Gem->SetVisibility(!bUsed);
	if (Label)
	{
		Label->SetTextRenderColor(Base.ToFColor(true));
		if (bUsed) Label->SetVisibility(false);
	}
	if (LabelShadow && bUsed) LabelShadow->SetVisibility(false);
}

void USQInteractCue::SetLabel(const FString& InLabel)
{
	if (InLabel == LabelText) return;
	LabelText = InLabel;
	if (Label) Label->SetText(FText::FromString(LabelText));
	if (LabelShadow) LabelShadow->SetText(FText::FromString(LabelText));
}

void USQInteractCue::SetUsed(bool bInUsed)
{
	if (bUsed == bInUsed) return;
	bUsed = bInUsed;
	if (!bBuilt) return;
	ApplyColors();
	if (RingISM) RingISM->SetRelativeScale3D(FVector::OneVector);
	SetComponentTickEnabled(!bUsed);
}

void USQInteractCue::SetUrgent(bool bInUrgent)
{
	bUrgent = bInUrgent;
}

void USQInteractCue::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBuilt || bUsed) return;
	UWorld* World = GetWorld();
	if (!World) return;
	// Spawned before the terrain replicated in: drop the ring onto the ground once it exists
	if (!bGrounded)
	{
		GroundRetry -= DeltaTime;
		if (GroundRetry <= 0.f)
		{
			GroundRetry = 0.5f;
			GroundRing();
		}
	}

	Clock += DeltaTime;
	const bool bFast = bUrgent || Kind == ESQCueKind::FreeChest;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * (bFast ? 5.5f : 2.4f) + PhaseOffset);

	// Close to the local reaper: brighter, label on
	const FVector Here = GetComponentLocation();
	const APawn* Me = UGameplayStatics::GetPlayerPawn(World, 0);
	const float Dist = Me ? static_cast<float>(FVector::Dist2D(Me->GetActorLocation(), Here)) : 1.0e9f;
	const bool bNear = Dist < FMath::Max(LabelRange, Radius + 150.f);
	NearBlend = FMath::FInterpTo(NearBlend, bNear ? 1.f : 0.f, DeltaTime, 8.f);

	if (RingMID) RingMID->SetScalarParameterValue(TEXT("Glow"), Brightness * (1.f + 1.6f * Pulse) * (1.f + 0.8f * NearBlend) * (bUrgent ? 1.6f : 1.f));
	if (RingISM) RingISM->SetRelativeScale3D(FVector(1.f + 0.035f * Pulse, 1.f + 0.035f * Pulse, 1.f));
	if (GemMID) GemMID->SetScalarParameterValue(TEXT("Glow"), Brightness * (3.f + 3.f * Pulse));
	if (Gem)
	{
		const float Bob = 14.f * FMath::Sin(Clock * 2.f + PhaseOffset);
		Gem->SetRelativeLocationAndRotation(FVector(0.f, 0.f, IconHeight + Bob),
			FRotator(45.f, FMath::Fmod(Clock * 70.f + FMath::RadiansToDegrees(PhaseOffset), 360.f), 45.f));
		Gem->SetRelativeScale3D(FVector(0.2f + 0.05f * NearBlend));
	}
	if (Label)
	{
		const bool bShowLabel = NearBlend > 0.35f && !LabelText.IsEmpty();
		if (Label->IsVisible() != bShowLabel) Label->SetVisibility(bShowLabel);
		if (LabelShadow && LabelShadow->IsVisible() != bShowLabel) LabelShadow->SetVisibility(bShowLabel);
		if (bShowLabel)
		{
			if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0))
			{
				const FRotator Look = (Cam->GetCameraLocation() - Label->GetComponentLocation()).Rotation();
				const FRotator Facing(FMath::Clamp(static_cast<float>(Look.Pitch), -60.f, 60.f), static_cast<float>(Look.Yaw), 0.f);
				Label->SetWorldRotation(Facing);
				if (LabelShadow)
				{
					// 3 cm further from the camera and 2.5 cm down-right on screen: a drop shadow
					const FVector Away = -Facing.Vector();
					LabelShadow->SetWorldLocationAndRotation(Label->GetComponentLocation() + Away * 3.f
						- Label->GetUpVector() * 2.5f + Label->GetRightVector() * 2.5f, Facing);
				}
			}
		}
	}
}
