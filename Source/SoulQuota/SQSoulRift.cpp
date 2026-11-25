// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQSoulRift.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQData.h"
#include "SQArt.h"
#include "SQFx.h"

namespace
{
	const FLinearColor RiftTeal(0.36f, 0.95f, 0.84f);
	const FLinearColor RiftCore(0.85f, 1.f, 0.97f);
	constexpr int32 RiftRings = 4;
	constexpr int32 RiftWisps = 10;
	constexpr int32 TunnelRings = 14;
	constexpr float TunnelSpacing = 420.f;

	UStaticMesh* RiftKitMesh(const TCHAR* Name, const TCHAR* Fallback)
	{
		const FString Path = FString::Printf(TEXT("/Game/Art/FX/SM_FX_%s.SM_FX_%s"), Name, Name);
		if (UStaticMesh* Kit = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) return Kit;
		return SQ::Mesh(FName(Fallback));
	}
}

ASQSoulRift::ASQSoulRift()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	Core->SetupAttachment(Root);
	Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Core->SetCastShadow(false);

	// The passage floor: invisible, solid (the crew stands on it while the tunnel streams past)
	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(Root);
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetCastShadow(false);
	Floor->SetVisibility(false);
	Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 300.f));
	Light->SetIntensity(60000.f);
	Light->SetAttenuationRadius(2600.f);
	Light->SetLightColor(RiftTeal);
	Light->SetCastShadows(false);
}

void ASQSoulRift::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQSoulRift, Mode);
	DOREPLIFETIME(ASQSoulRift, Radius);
}

ASQSoulRift* ASQSoulRift::SpawnRift(UWorld* World, const FVector& Location, uint8 InMode, float InRadius)
{
	if (!World || World->GetNetMode() == NM_Client) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform Xf(FRotator::ZeroRotator, Location);
	ASQSoulRift* Rift = World->SpawnActorDeferred<ASQSoulRift>(ASQSoulRift::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Rift) return nullptr;
	Rift->Mode = InMode;
	Rift->Radius = FMath::Max(100.f, InRadius);
	Rift->FinishSpawning(Xf);
	return Rift;
}

void ASQSoulRift::BeginPlay()
{
	Super::BeginPlay();
	// The floor exists on every machine (the server simulates the crew standing on it; clients predict their own reaper)
	Floor->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	ApplyLook();
}

void ASQSoulRift::OnRep_Look()
{
	ApplyLook();
}

void ASQSoulRift::ApplyLook()
{
	const bool bPassage = Mode == 1;
	// Floor: a 40 cm slab under the passage, its top at the actor's origin
	Floor->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 0.4f));
	Floor->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
	Floor->SetCollisionEnabled(bPassage ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (GetNetMode() == NM_DedicatedServer || LookMode == Mode) return;
	LookMode = Mode;

	if (!GlowMat) GlowMat = SQArt::GlowMID(this, RiftTeal, 3.f, 0.f);
	if (!DarkMat) DarkMat = SQ::ColorMID(this, FLinearColor(0.02f, 0.03f, 0.06f));
	UMaterialInterface* Glow = GlowMat ? static_cast<UMaterialInterface*>(GlowMat.Get()) : static_cast<UMaterialInterface*>(SQ::ColorMID(this, RiftTeal));

	Core->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	if (UMaterialInstanceDynamic* CoreMat = SQArt::GlowMID(this, RiftCore, 5.f, 0.f)) Core->SetMaterial(0, CoreMat);
	else Core->SetMaterial(0, Glow);
	Core->SetVisibility(!bPassage);

	if (!Rings)
	{
		Rings = NewObject<UInstancedStaticMeshComponent>(this, TEXT("RiftRings"));
		Rings->SetupAttachment(Root);
		Rings->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rings->SetCastShadow(false);
		Rings->RegisterComponent();
	}
	if (!Wisps)
	{
		Wisps = NewObject<UInstancedStaticMeshComponent>(this, TEXT("RiftWisps"));
		Wisps->SetupAttachment(Root);
		Wisps->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wisps->SetCastShadow(false);
		Wisps->RegisterComponent();
	}
	Rings->ClearInstances();
	Wisps->ClearInstances();
	Rings->SetStaticMesh(RiftKitMesh(TEXT("Ring"), TEXT("Cylinder")));
	Rings->SetMaterial(0, Glow);
	Wisps->SetStaticMesh(RiftKitMesh(TEXT("Flame"), TEXT("Sphere")));
	Wisps->SetMaterial(0, Glow);
	const int32 NumRings = bPassage ? TunnelRings : RiftRings;
	for (int32 RingIdx = 0; RingIdx < NumRings; ++RingIdx) Rings->AddInstance(FTransform::Identity);
	for (int32 WispIdx = 0; WispIdx < RiftWisps; ++WispIdx) Wisps->AddInstance(FTransform::Identity);
	Light->SetIntensity(bPassage ? 25000.f : 60000.f);
	Light->SetRelativeLocation(FVector(0.f, 0.f, bPassage ? 600.f : 300.f));
}

void ASQSoulRift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer || !Rings || !Wisps) return;
	Time += DeltaSeconds;
	const bool bPassage = Mode == 1;
	const FVector Here = GetActorLocation();

	if (!bPassage)
	{
		// The torn soul: a bright core, rings spinning on tilted axes, flame wisps orbiting and climbing, a steady upward stream
		const float Pulse = 1.f + 0.08f * FMath::Sin(Time * 5.f);
		Core->SetRelativeLocation(FVector(0.f, 0.f, 260.f));
		Core->SetRelativeScale3D(FVector(1.6f * Pulse));
		for (int32 RingIdx = 0; RingIdx < Rings->GetInstanceCount(); ++RingIdx)
		{
			const float Spin = Time * (90.f + 35.f * RingIdx) * (RingIdx % 2 ? -1.f : 1.f);
			const FRotator Tilt(25.f + 18.f * RingIdx, Spin, 10.f * RingIdx);
			const float Size = Radius / 50.f * (0.55f + 0.18f * RingIdx);
			Rings->UpdateInstanceTransform(RingIdx, FTransform(Tilt, FVector(0.f, 0.f, 260.f), FVector(Size, Size, Size)), false, false);
		}
		for (int32 WispIdx = 0; WispIdx < Wisps->GetInstanceCount(); ++WispIdx)
		{
			const float Phase = 2.f * PI * static_cast<float>(WispIdx) / Wisps->GetInstanceCount();
			const float Climb = FMath::Fmod(Time * 0.5f + WispIdx * 0.1f, 1.f);
			const float R = Radius * (0.95f - 0.5f * Climb);
			const FVector P(FMath::Cos(Phase + Time * 2.2f) * R, FMath::Sin(Phase + Time * 2.2f) * R, 40.f + Climb * 620.f);
			const float S = 0.9f * (1.f - Climb) + 0.2f;
			Wisps->UpdateInstanceTransform(WispIdx, FTransform(FRotator::ZeroRotator, P, FVector(S)), false, false);
		}
		Rings->MarkRenderStateDirty();
		Wisps->MarkRenderStateDirty();
		FxTimer -= DeltaSeconds;
		if (FxTimer <= 0.f)
		{
			FxTimer = 0.12f;
			FSQFxParticle P;
			const float A = FMath::FRandRange(0.f, 2.f * PI);
			P.Pos = Here + FVector(FMath::Cos(A) * Radius * 0.7f, FMath::Sin(A) * Radius * 0.7f, 30.f);
			P.Vel = FVector(-FMath::Cos(A) * 120.f, -FMath::Sin(A) * 120.f, 520.f);
			P.Color = RiftTeal;
			P.Shape = ESQFxShape::Flame;
			P.Life = 1.4f;
			P.SizeStart = FVector(40.f);
			P.SizeEnd = FVector(12.f);
			P.Bright = 2.f;
			SQFx::Emit(GetWorld(), P);
		}
		return;
	}

	// The passage: rings stream upward past the floor (the crew seems to fall), wisps drift up beside them
	const float Speed = 1100.f;
	const float Span = TunnelSpacing * TunnelRings;
	for (int32 RingIdx = 0; RingIdx < Rings->GetInstanceCount(); ++RingIdx)
	{
		const float Z = FMath::Fmod(Time * Speed + RingIdx * TunnelSpacing, Span) - Span * 0.55f;
		const float Size = Radius * 2.6f / 50.f;
		Rings->UpdateInstanceTransform(RingIdx, FTransform(FRotator(90.f, 0.f, Time * 20.f + RingIdx * 23.f), FVector(0.f, 0.f, Z), FVector(Size, Size, Size)), false, false);
	}
	for (int32 WispIdx = 0; WispIdx < Wisps->GetInstanceCount(); ++WispIdx)
	{
		const float Phase = 2.f * PI * static_cast<float>(WispIdx) / Wisps->GetInstanceCount();
		const float Z = FMath::Fmod(Time * Speed * 1.3f + WispIdx * 700.f, Span) - Span * 0.55f;
		const FVector P(FMath::Cos(Phase) * Radius * 2.2f, FMath::Sin(Phase) * Radius * 2.2f, Z);
		Wisps->UpdateInstanceTransform(WispIdx, FTransform(FRotator::ZeroRotator, P, FVector(1.4f)), false, false);
	}
	Rings->MarkRenderStateDirty();
	Wisps->MarkRenderStateDirty();
	FxTimer -= DeltaSeconds;
	if (FxTimer <= 0.f)
	{
		FxTimer = 0.05f;
		FSQFxParticle P;
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		const float R = FMath::FRandRange(Radius * 1.3f, Radius * 2.8f);
		P.Pos = Here + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, -1500.f);
		P.Vel = FVector(0.f, 0.f, 2600.f);
		P.Color = FMath::Lerp(RiftTeal, RiftCore, FMath::FRand());
		P.Shape = ESQFxShape::Flame;
		P.Life = 1.3f;
		P.SizeStart = P.SizeEnd = FVector(FMath::FRandRange(30.f, 70.f));
		P.Bright = 2.f;
		SQFx::Emit(GetWorld(), P);
	}
}
