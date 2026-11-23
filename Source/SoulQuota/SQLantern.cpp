// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQLantern.h"
#include "SQFxRecipe.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQArt.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQArena.h"

ASQLantern::ASQLantern()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Post = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Post"));
	Post->SetMobility(EComponentMobility::Movable);   // the lantern moves every night
	Post->SetupAttachment(Root);
	Post->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	Post->SetRelativeScale3D(FVector(0.3f, 0.3f, 2.4f));
	Post->SetCollisionProfileName(TEXT("BlockAll"));

	Flame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flame"));
	Flame->SetMobility(EComponentMobility::Movable);
	Flame->SetupAttachment(Root);
	Flame->SetCastShadow(false);
	Flame->SetRelativeLocation(FVector(0.f, 0.f, 275.f));
	Flame->SetRelativeScale3D(FVector(0.7f));
	Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetMobility(EComponentMobility::Movable);
	Ring->SetupAttachment(Root);
	Ring->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 300.f));
	Light->SetIntensity(30000.f);
	Light->SetAttenuationRadius(1800.f);
	Light->SetLightColor(FLinearColor(1.f, 0.75f, 0.35f));

	LanternMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LanternMesh"));
	LanternMesh->SetMobility(EComponentMobility::Movable);
	LanternMesh->SetupAttachment(Root);
	LanternMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LanternMesh->SetVisibility(false);
}

void ASQLantern::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQLantern, Radius);
}

void ASQLantern::BeginPlay()
{
	Super::BeginPlay();
	Post->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	Flame->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	Ring->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	Post->SetMaterial(0, ASQArena::SurfaceMID(this, FLinearColor(0.12f, 0.1f, 0.1f)));
	// Glowing flame + banking ring: the one spot every player must find at a glance (M_Ground Glow; clay fallback)
	Flame->SetMaterial(0, ASQArena::SurfaceMID(this, FLinearColor(1.f, 0.7f, 0.3f), 6.f));
	Ring->SetMaterial(0, ASQArena::SurfaceMID(this, FLinearColor(0.75f, 0.52f, 0.16f), 0.6f));
	if (const USQData* LD = USQData::Get(this))
	{
		Light->SetIntensity(LD->Tune(TEXT("LanternLightIntensity"), 30000.f));
		// Shadowed point light = 6 shadow views of the whole horde: off unless asked for
		Light->SetCastShadows(LD->Tune(TEXT("LanternShadows"), 0.f) > 0.5f);
	}

	// Imported lantern art (primitive post stays as an invisible blocker)
	if (UStaticMesh* LanternAsset = SQArt::Prop(TEXT("lantern_standing")))
	{
		const float LanternHeight = 340.f;
		const float LScale = ASQArena::FitMeshHeight(LanternAsset, LanternHeight);
		const FBoxSphereBounds LB = LanternAsset->GetBounds();
		LanternMesh->SetStaticMesh(LanternAsset);
		LanternMesh->SetRelativeScale3D(FVector(LScale));
		LanternMesh->SetRelativeLocation(FVector(-LB.Origin.X * LScale, -LB.Origin.Y * LScale, -(LB.Origin.Z - LB.BoxExtent.Z) * LScale));
		UMaterialInstanceDynamic* LanternMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White);
		if (LanternMID) LanternMID->SetScalarParameterValue(TEXT("Glow"), 0.4f);
		SQArt::SetAllMaterials(LanternMesh, LanternMID);
		LanternMesh->SetCollisionProfileName(TEXT("BlockAll"));
		LanternMesh->SetVisibility(true);
		Post->SetVisibility(false);

		FlameScale = 0.3f;
		Flame->SetRelativeLocation(FVector(0.f, 0.f, LanternHeight * 0.8f));
		Flame->SetRelativeScale3D(FVector(FlameScale));

		if (UStaticMesh* CandleAsset = SQArt::Prop(TEXT("candle_triple")))
		{
			// FitMeshHeight: SQArt::FitScale returns 1 for meshes under 1 unit tall (a 0.3-unit candle stayed 3 mm tall)
			CandleArtScale = ASQArena::FitMeshHeight(CandleAsset, 60.f);
			const FBoxSphereBounds CB = CandleAsset->GetBounds();
			CandleZ = -static_cast<float>(CB.Origin.Z - CB.BoxExtent.Z) * CandleArtScale;
			UMaterialInstanceDynamic* CandleMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White);
			if (CandleMID) CandleMID->SetScalarParameterValue(TEXT("Glow"), 0.6f);
			Candles = NewObject<UInstancedStaticMeshComponent>(this);
			Candles->SetMobility(EComponentMobility::Movable);
			Candles->SetStaticMesh(CandleAsset);
			SQArt::SetAllMaterials(Candles, CandleMID);
			Candles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Candles->SetCastShadow(false);
			Candles->SetupAttachment(Root);
			Candles->RegisterComponent();
		}
	}

	// ---- Lantern trail: the beam over the lit lantern, the other sites' posts and the footlights towards it
	{
		const USQData* TD = USQData::Get(this);
		BeamHeight = TD ? FMath::Clamp(TD->Tune(TEXT("LanternBeamHeight"), 5000.f), 0.f, 20000.f) : 5000.f;
		BeamWidth = TD ? FMath::Clamp(TD->Tune(TEXT("LanternBeamWidth"), 0.9f), 0.1f, 6.f) : 0.9f;
		const float BeamGlow = TD ? FMath::Max(0.f, TD->Tune(TEXT("LanternBeamGlow"), 3.f)) : 3.f;
		if (BeamHeight > 1.f)
		{
			Beam = NewObject<UStaticMeshComponent>(this);
			Beam->SetMobility(EComponentMobility::Movable);
			Beam->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
			Beam->SetMaterial(0, ASQArena::SurfaceMID(this, FLinearColor(1.f, 0.72f, 0.28f), BeamGlow));
			Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Beam->SetCastShadow(false);
			Beam->SetupAttachment(Root);
			Beam->SetRelativeLocation(FVector(0.f, 0.f, 380.f + BeamHeight * 0.5f));
			Beam->SetRelativeScale3D(FVector(BeamWidth, BeamWidth, BeamHeight / 100.f));
			Beam->RegisterComponent();
			Beam->SetVisibility(false);
		}
		UStaticMesh* PostAsset = SQArt::Prop(TEXT("lantern_standing"));
		UStaticMesh* PostMesh = PostAsset ? PostAsset : SQ::Mesh(TEXT("Cylinder"));
		if (PostAsset)
		{
			PostArtScale = ASQArena::FitMeshHeight(PostAsset, 340.f);
			const FBoxSphereBounds PB = PostAsset->GetBounds();
			PostArtZ = -static_cast<float>(PB.Origin.Z - PB.BoxExtent.Z) * PostArtScale;
		}
		UMaterialInterface* DormantMat = nullptr;
		UMaterialInterface* SpentMat = nullptr;
		if (PostAsset)
		{
			// Dormant = the same lantern, unlit and cold blue-grey; spent = dim warm ember
			UMaterialInstanceDynamic* DormantMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor(0.42f, 0.5f, 0.68f));
			UMaterialInstanceDynamic* SpentMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor(0.5f, 0.38f, 0.3f));
			if (SpentMID) SpentMID->SetScalarParameterValue(TEXT("Glow"), 0.12f);
			DormantMat = DormantMID;
			SpentMat = SpentMID;
		}
		if (!DormantMat) DormantMat = ASQArena::SurfaceMID(this, FLinearColor(0.1f, 0.12f, 0.18f));
		if (!SpentMat) SpentMat = ASQArena::SurfaceMID(this, FLinearColor(0.2f, 0.13f, 0.08f), 0.15f);
		DormantPosts = MakeWorldISM(PostMesh, DormantMat);
		SpentPosts = MakeWorldISM(PostMesh, SpentMat);
		Footlights = MakeWorldISM(SQ::Mesh(TEXT("Cylinder")), ASQArena::SurfaceMID(this, FLinearColor(1.f, 0.7f, 0.25f), TD ? FMath::Max(0.f, TD->Tune(TEXT("LanternFootlightGlow"), 2.5f)) : 2.5f));
	}

	if (HasAuthority())
	{
		if (const USQData* D = USQData::Get(this)) Radius = D->Tune(TEXT("LanternRadius"), 350.f);
		// Sit on the terrain (the central plaza is flat at 0, so at the origin this changes nothing)
		const FVector Here = GetActorLocation();
		const float GroundHere = ASQArena::GroundZAt(this, static_cast<float>(Here.X), static_cast<float>(Here.Y));
		if (!FMath::IsNearlyEqual(static_cast<float>(Here.Z), GroundHere, 1.f)) SetActorLocation(FVector(Here.X, Here.Y, GroundHere));
	}
	OnRep_Radius();
}

UInstancedStaticMeshComponent* ASQLantern::MakeWorldISM(UStaticMesh* InMesh, UMaterialInterface* Mat)
{
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetStaticMesh(InMesh);
	if (Mat)
	{
		const int32 Slots = FMath::Max(1, ISM->GetNumMaterials());
		for (int32 Slot = 0; Slot < Slots; ++Slot) ISM->SetMaterial(Slot, Mat);
	}
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(false);
	ISM->SetupAttachment(Root);
	// World space: the actor moves from site to site, these stay where they are put
	ISM->SetUsingAbsoluteLocation(true);
	ISM->SetUsingAbsoluteRotation(true);
	ISM->SetUsingAbsoluteScale(true);
	ISM->RegisterComponent();
	ISM->SetWorldTransform(FTransform::Identity);
	return ISM;
}

void ASQLantern::RefreshTrailLook(const ASQGameState* GS)
{
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	const int32 Sites = GS ? GS->TrailSites.Num() : 0;
	const int32 Stage = GS ? GS->TrailStage : 0;
	const int32 Missed = GS ? GS->TrailMissedMask : 0;
	const int32 Serial = Arena ? Arena->GetBuildSerial() : -1;
	const FVector Here = GetActorLocation();
	if (Sites == LookSites && Stage == LookStage && Missed == LookMissed && Serial == LookSerial && Here.Equals(LookAt, 1.0)) return;
	// Nov 19 2026: the moment the trail moves on, the lantern just banked gutters out and the new one catches (FxRecipes.csv LanternSpent / LanternLit)
	if (LookStage >= 0 && Stage == LookStage + 1 && Sites == LookSites && GetNetMode() != NM_DedicatedServer)
	{
		SQFxRecipe::PlayShared(GetWorld(), FName(TEXT("LanternSpent")), LookAt, FLinearColor(1.f, 0.48f, 0.23f), Radius);
		SQFxRecipe::PlayShared(GetWorld(), FName(TEXT("LanternLit")), Here, FLinearColor(1.f, 0.69f, 0.29f), Radius);
	}
	LookSites = Sites;
	LookStage = Stage;
	LookMissed = Missed;
	LookSerial = Serial;
	LookAt = Here;
	if (DormantPosts) DormantPosts->ClearInstances();
	if (SpentPosts) SpentPosts->ClearInstances();
	if (Footlights) Footlights->ClearInstances();
	// The beam marks a lantern that still wants souls
	if (Beam) Beam->SetVisibility(Sites > 0 && Stage < Sites);
	if (Sites <= 0 || !GS) return;

	const bool bArtPost = SQArt::Prop(TEXT("lantern_standing")) != nullptr;
	for (int32 SiteIdx = 0; SiteIdx < Sites; ++SiteIdx)
	{
		const FVector Site = GS->TrailSites[SiteIdx];
		// The lit lantern (this actor) stands on its own site; after the trail it stays on the last one
		if (FVector::DistSquared2D(Site, Here) < FMath::Square(60.0)) continue;
		const float GroundZ = ASQArena::GroundZAt(this, static_cast<float>(Site.X), static_cast<float>(Site.Y));
		const FTransform Xf = bArtPost
			? FTransform(FRotator(0.f, SiteIdx * 47.f, 0.f), FVector(Site.X, Site.Y, GroundZ + PostArtZ), FVector(PostArtScale))
			: FTransform(FRotator::ZeroRotator, FVector(Site.X, Site.Y, GroundZ + 120.f), FVector(0.3f, 0.3f, 2.4f));
		UInstancedStaticMeshComponent* Posts = SiteIdx < Stage ? SpentPosts.Get() : DormantPosts.Get();
		if (Posts) Posts->AddInstance(Xf, true);
	}

	// Footlights: from the lantern just left, down its lane to the hub and up the lit lantern's lane (straight when there is no lane)
	// Floor rule (Nov 12 2026 map remaster): nothing lies on the floor except pickups, so the footlights are off unless
	// Tuning LanternFootlights is 1. The beam, the on-screen arrow and the minimap line still point the way.
	const USQData* FloorD = USQData::Get(this);
	if (!FloorD || FloorD->Tune(TEXT("LanternFootlights"), 0.f) < 0.5f) return;
	if (!Footlights || !Arena || Stage <= 0 || Stage >= Sites) return;
	TArray<FVector2D> Path;
	const FVector From = GS->TrailSites[Stage - 1];
	const FVector To = GS->TrailSites[Stage];
	auto LaneOf = [Arena](const FVector& Site) -> const FSQLane*
	{
		const int32 DistrictIdx = Arena->FindDistrictAt(static_cast<float>(Site.X), static_cast<float>(Site.Y));
		if (DistrictIdx == INDEX_NONE) return nullptr;
		for (const FSQLane& Lane : Arena->GetLanes())
		{
			if (Lane.District == DistrictIdx && Lane.Pts.Num() >= 2) return &Lane;
		}
		return nullptr;
	};
	Path.Add(FVector2D(From.X, From.Y));
	if (const FSQLane* Out = LaneOf(From))
	{
		// Lanes run hub -> district: walk this one backwards
		for (int32 PtIdx = Out->Pts.Num() - 1; PtIdx >= 0; --PtIdx) Path.Add(Out->Pts[PtIdx]);
	}
	if (const FSQLane* In = LaneOf(To))
	{
		for (const FVector2D& Pt : In->Pts) Path.Add(Pt);
	}
	Path.Add(FVector2D(To.X, To.Y));
	const USQData* D = USQData::Get(this);
	const float Spacing = D ? FMath::Clamp(D->Tune(TEXT("LanternFootlightSpacing"), 450.f), 150.f, 3000.f) : 450.f;
	float Carry = Spacing;
	int32 Placed = 0;
	for (int32 PtIdx = 1; PtIdx < Path.Num() && Placed < 400; ++PtIdx)
	{
		const FVector2D A = Path[PtIdx - 1];
		const FVector2D B = Path[PtIdx];
		const float SegLen = static_cast<float>(FVector2D::Distance(A, B));
		if (SegLen < 1.f) continue;
		float Along = Carry;
		for (; Along < SegLen && Placed < 400; Along += Spacing)
		{
			const FVector2D P = FMath::Lerp(A, B, static_cast<double>(Along / SegLen));
			// Not inside either banking ring
			if (FVector2D::Distance(P, FVector2D(To.X, To.Y)) < Radius + 80.f || FVector2D::Distance(P, FVector2D(From.X, From.Y)) < Radius + 80.f) continue;
			const float Z = ASQArena::GroundZAt(this, static_cast<float>(P.X), static_cast<float>(P.Y));
			Footlights->AddInstance(FTransform(FRotator::ZeroRotator, FVector(P.X, P.Y, Z + 4.f), FVector(0.42f, 0.42f, 0.05f)), true);
			++Placed;
		}
		Carry = Along - SegLen;
	}
}

void ASQLantern::OnRep_Radius()
{
	Ring->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 0.01f));
	if (Candles)
	{
		Candles->ClearInstances();
		for (int32 i = 0; i < 8; ++i)
		{
			const float Angle = i * 2.f * PI / 8.f;
			const FVector CandlePos(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, CandleZ);
			Candles->AddInstance(FTransform(FRotator(0.f, FMath::RadiansToDegrees(Angle), 0.f), CandlePos, FVector(CandleArtScale)));
		}
	}
}

void ASQLantern::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	if (HasAuthority()) ServerValidateSpot();
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const bool bFinalCall = GS && GS->bFinalCall;
	RefreshTrailLook(GS);
	if (Beam && Beam->IsVisible())
	{
		// A slow breath so the eye finds it against the sky
		const float Breath = 1.f + 0.18f * FMath::Sin(Time * 2.2f);
		Beam->SetRelativeScale3D(FVector(BeamWidth * Breath, BeamWidth * Breath, BeamHeight / 100.f));
	}
	const float Flicker = 1.f + 0.08f * FMath::Sin(Time * 13.f) + 0.05f * FMath::Sin(Time * 29.f);
	Flame->SetRelativeScale3D(FVector(FlameScale * Flicker));
	Light->SetLightColor(bFinalCall && FMath::Fmod(Time, 0.5f) < 0.25f ? FLinearColor(1.f, 0.2f, 0.15f) : FLinearColor(1.f, 0.75f, 0.35f));
}

void ASQLantern::ServerValidateSpot()
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	if (!Arena || !Arena->HasTerrain()) return;
	const FVector Here = GetActorLocation();
	const FVector2D HereXY(Here.X, Here.Y);
	if (Arena->GetBuildSerial() == ValidatedSerial && FVector2D::DistSquared(HereXY, ValidatedXY) < 1.0) return;
	ValidatedSerial = Arena->GetBuildSerial();
	// The game mode only checks roughness + jump pads: it can pick a spot inside a crypt, a lamp post, on ice or by a wall
	const FVector Spot = Arena->FindLanternSpot(static_cast<float>(Here.X), static_cast<float>(Here.Y), Radius);
	ValidatedXY = FVector2D(Spot.X, Spot.Y);
	if (!Spot.Equals(Here, 1.0))
	{
		SetActorLocation(Spot);
	}
}
