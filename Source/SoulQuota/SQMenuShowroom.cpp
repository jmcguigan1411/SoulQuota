// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQMenuShowroom.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "SQArt.h"
#include "SQData.h"
#include "SQUIStyle.h"
#include "SQFx.h"
#include "SQFxTheme.h"

namespace
{
	/** Ease out with a little overshoot (0..1 -> 0..1). */
	float ShowroomBackOut(float X)
	{
		const float C1 = 1.70158f;
		const float C3 = C1 + 1.f;
		const float M = X - 1.f;
		return 1.f + C3 * M * M * M + C1 * M * M;
	}
}

ASQMenuShowroom::ASQMenuShowroom()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SceneRoot);
	Camera->bConstrainAspectRatio = false;

	ReaperRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ReaperRoot"));
	ReaperRoot->SetupAttachment(SceneRoot);

	ReaperMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ReaperMesh"));
	ReaperMesh->SetupAttachment(ReaperRoot);
	ReaperMesh->SetMobility(EComponentMobility::Movable);
	ReaperMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReaperMesh->SetVisibility(false);

	ReaperWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ReaperWeapon"));
	ReaperWeapon->SetupAttachment(ReaperMesh);
	ReaperWeapon->SetMobility(EComponentMobility::Movable);
	ReaperWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReaperWeapon->SetVisibility(false);

	FallbackRobe = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallbackRobe"));
	FallbackRobe->SetupAttachment(ReaperRoot);
	FallbackRobe->SetMobility(EComponentMobility::Movable);
	FallbackRobe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackRobe->SetRelativeLocation(FVector(0.0, 0.0, 75.0));
	FallbackRobe->SetRelativeScale3D(FVector(0.9, 0.9, 1.5));
	FallbackRobe->SetVisibility(false);

	FallbackSkull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallbackSkull"));
	FallbackSkull->SetupAttachment(ReaperRoot);
	FallbackSkull->SetMobility(EComponentMobility::Movable);
	FallbackSkull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackSkull->SetRelativeLocation(FVector(0.0, 0.0, 165.0));
	FallbackSkull->SetRelativeScale3D(FVector(0.5));
	FallbackSkull->SetVisibility(false);
}

void ASQMenuShowroom::BeginPlay()
{
	Super::BeginPlay();

	// soft film look: vignette, bloom on the lantern and the wisps, fixed exposure like the Graveyard map
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = ExposureBias;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.75f;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 1.2f;
	Camera->SetFieldOfView(CameraFov);

	FallbackRobe->SetStaticMesh(SQ::Mesh(TEXT("Cone")));
	FallbackSkull->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	FallbackSkull->SetMaterial(0, SQ::ColorMID(this, SQUI::BoneWhite));

	// the reaper stands at the origin, turned a little from facing the camera
	const float CamYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(CameraSpot.Y, CameraSpot.X)));
	RootYaw = CamYaw + ReaperTurn;
	ReaperRoot->SetRelativeRotation(FRotator(0.f, RootYaw, 0.f));

	BuildSet();
	BuildLights();
	BuildWisps();
	if (bStudio)
	{
		BuildStudio();
	}
	Tick(0.f); // camera in place before the first frame
}

UMaterialInstanceDynamic* ASQMenuShowroom::GlowMID(const FLinearColor& Colour, float Glow)
{
	UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInstanceDynamic* MID = SQArt::TintMID(this, White, Colour);
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
	return MID;
}

UStaticMeshComponent* ASQMenuShowroom::AddMesh(UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, UMaterialInterface* Mat)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(SceneRoot);
	C->SetRelativeTransform(FTransform(Rot, Loc, Scale));
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	C->RegisterComponent();
	return C;
}

UStaticMeshComponent* ASQMenuShowroom::AddProp(FName PropName, const FVector& Loc, float Yaw, float Height, FName Shape, const FVector& ShapeScale,
	const FLinearColor& ShapeColour)
{
	const FRotator Rot(0.f, Yaw, 0.f);
	if (UStaticMesh* Art = SQArt::Prop(PropName))
	{
		// feet on the ground, centred on Loc (the KayKit pivots vary)
		const float S = SQArt::FitScale(Art, Height);
		const FBoxSphereBounds B = Art->GetBounds();
		const FVector Offset(-B.Origin.X * S, -B.Origin.Y * S, -(B.Origin.Z - B.BoxExtent.Z) * S);
		UStaticMeshComponent* C = AddMesh(Art, Loc + Rot.RotateVector(Offset), Rot, FVector(S), nullptr);
		if (C)
		{
			SQArt::SetAllMaterials(C, SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White));
		}
		return C;
	}
	if (Shape.IsNone())
	{
		return nullptr; // decoration only when the art is there
	}
	// engine shapes are 100 cm and centred: lift half the height
	return AddMesh(SQ::Mesh(Shape), Loc + FVector(0.0, 0.0, ShapeScale.Z * 50.0), Rot, ShapeScale, SQ::ColorMID(this, ShapeColour));
}

void ASQMenuShowroom::BuildSet()
{
	const FLinearColor Stone = SQUI::Hex(0x4A, 0x48, 0x52);
	const FLinearColor Wood = SQUI::Hex(0x3A, 0x2A, 0x1E);
	const FLinearColor Pumpkin = SQUI::Hex(0xE8, 0x7A, 0x1C);

	// ground: a mossy disc fading into the fog, a dirt patch under the reaper
	AddMesh(SQ::Mesh(TEXT("Cylinder")), FVector(0.0, 0.0, -5.0), FRotator::ZeroRotator, FVector(26.0, 26.0, 0.1), SQ::ColorMID(this, SQUI::Hex(0x1C, 0x23, 0x18)));
	AddMesh(SQ::Mesh(TEXT("Cylinder")), FVector(0.0, 0.0, 0.3), FRotator::ZeroRotator, FVector(3.6, 3.6, 0.02), SQ::ColorMID(this, SQUI::Hex(0x2B, 0x22, 0x19)));

	// the big lantern behind the reaper's left shoulder, with its flame
	const float LanternH = 300.f;
	const FVector LanternAt(-150.0, -260.0, 0.0);
	UStaticMeshComponent* Lantern = AddProp(TEXT("lantern_standing"), LanternAt, 200.f, LanternH, TEXT("Cylinder"), FVector(0.22, 0.22, 3.0), Wood);
	if (Lantern && SQArt::Prop(TEXT("lantern_standing")))
	{
		if (UMaterialInstanceDynamic* LanternMID = Cast<UMaterialInstanceDynamic>(Lantern->GetMaterial(0)))
		{
			LanternMID->SetScalarParameterValue(TEXT("Glow"), 0.4f);
		}
	}
	FlameSpot = LanternAt + FVector(0.0, 0.0, LanternH * 0.8f);
	FlameMID = GlowMID(SQUI::Amber, 10.f);
	CandleFlames.Add(AddMesh(SQ::Mesh(TEXT("Sphere")), FlameSpot, FRotator::ZeroRotator, FVector(0.22), FlameMID));

	// graves, a dead tree, candles and a jack-o'-lantern
	AddProp(TEXT("gravestone"), FVector(-270.0, 230.0, 0.0), 12.f, 125.f, TEXT("Cube"), FVector(0.65, 0.2, 1.1), Stone);
	AddProp(TEXT("grave_A"), FVector(-110.0, 330.0, 0.0), -18.f, 95.f, TEXT("Cube"), FVector(0.55, 0.18, 0.85), Stone);
	AddProp(TEXT("gravestone"), FVector(-480.0, -470.0, 0.0), -24.f, 135.f, TEXT("Cube"), FVector(0.65, 0.2, 1.2), Stone);
	AddProp(TEXT("grave_A"), FVector(-620.0, 60.0, 0.0), 6.f, 100.f, TEXT("Cube"), FVector(0.55, 0.18, 0.9), Stone);
	AddProp(TEXT("tree_dead_large"), FVector(-560.0, 470.0, 0.0), 35.f, 640.f, TEXT("Cylinder"), FVector(0.45, 0.45, 5.5), Wood);
	UStaticMeshComponent* Candles = AddProp(TEXT("candle_triple"), FVector(120.0, -175.0, 0.0), 30.f, 46.f, TEXT("Cylinder"), FVector(0.12, 0.12, 0.35), SQUI::BoneWhite);
	if (Candles && SQArt::Prop(TEXT("candle_triple")))
	{
		if (UMaterialInstanceDynamic* CandleMID = Cast<UMaterialInstanceDynamic>(Candles->GetMaterial(0)))
		{
			CandleMID->SetScalarParameterValue(TEXT("Glow"), 0.6f);
		}
	}
	UStaticMeshComponent* Jack = AddProp(TEXT("pumpkin_orange_jackolantern"), FVector(150.0, 210.0, 0.0), 195.f, 58.f, TEXT("Sphere"), FVector(0.6, 0.6, 0.5), Pumpkin);
	if (Jack && SQArt::Prop(TEXT("pumpkin_orange_jackolantern")))
	{
		if (UMaterialInstanceDynamic* JackMID = Cast<UMaterialInstanceDynamic>(Jack->GetMaterial(0)))
		{
			JackMID->SetScalarParameterValue(TEXT("Glow"), 0.35f);
		}
	}
	// small bits (art only)
	AddProp(TEXT("skull"), FVector(95.0, 95.0, 0.0), 150.f, 22.f, NAME_None, FVector::OneVector, Stone);
	AddProp(TEXT("bone_A"), FVector(70.0, -60.0, 0.0), 70.f, 10.f, NAME_None, FVector::OneVector, Stone);
	AddProp(TEXT("candle_triple"), FVector(-250.0, 160.0, 0.0), -40.f, 34.f, NAME_None, FVector::OneVector, Stone);

	// a crooked fence across the back
	FRandomStream Rng(13);
	if (UStaticMesh* FenceArt = SQArt::Prop(TEXT("fence")))
	{
		const FVector E = FenceArt->GetBounds().BoxExtent;
		const bool bAlongX = E.X >= E.Y;
		const float SegW = 210.f;
		const float S = SQArt::FitWidth(FenceArt, SegW);
		const FBoxSphereBounds B = FenceArt->GetBounds();
		for (int32 k = -5; k <= 5; ++k)
		{
			const FRotator Rot(Rng.FRandRange(-3.f, 3.f), (bAlongX ? 90.f : 0.f) + Rng.FRandRange(-4.f, 4.f), Rng.FRandRange(-4.f, 4.f));
			const FVector Offset(-B.Origin.X * S, -B.Origin.Y * S, -(B.Origin.Z - B.BoxExtent.Z) * S);
			UStaticMeshComponent* Seg = AddMesh(FenceArt, FVector(-720.0, k * SegW, 0.0) + Rot.RotateVector(Offset), Rot, FVector(S), nullptr);
			if (Seg)
			{
				SQArt::SetAllMaterials(Seg, SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White));
			}
		}
	}
	else
	{
		UMaterialInstanceDynamic* FenceMID = SQ::ColorMID(this, Wood);
		for (int32 k = -11; k <= 11; ++k)
		{
			AddMesh(SQ::Mesh(TEXT("Cube")), FVector(-720.0, k * 100.0, 60.0), FRotator(0.f, 0.f, Rng.FRandRange(-5.f, 5.f)), FVector(0.1, 0.12, 1.2), FenceMID);
		}
		AddMesh(SQ::Mesh(TEXT("Cube")), FVector(-720.0, 0.0, 95.0), FRotator::ZeroRotator, FVector(0.06, 23.0, 0.08), FenceMID);
		AddMesh(SQ::Mesh(TEXT("Cube")), FVector(-720.0, 0.0, 40.0), FRotator::ZeroRotator, FVector(0.06, 23.0, 0.08), FenceMID);
	}

	// graves and trees further back, fading into the fog
	for (int32 i = 0; i < 12; ++i)
	{
		const FVector At(Rng.FRandRange(-1800.f, -1050.f), Rng.FRandRange(-1600.f, 1600.f), 0.0);
		const float H = Rng.FRandRange(90.f, 150.f);
		AddProp(Rng.FRand() < 0.5f ? FName(TEXT("gravestone")) : FName(TEXT("grave_A")), At, Rng.FRandRange(-30.f, 30.f), H, TEXT("Cube"),
			FVector(0.6, 0.2, H / 100.f), Stone);
	}
	AddProp(TEXT("tree_dead_large"), FVector(-1500.0, -900.0, 0.0), -60.f, 820.f, TEXT("Cylinder"), FVector(0.5, 0.5, 7.0), Wood);
	AddProp(TEXT("tree_dead_large"), FVector(-1250.0, 1250.0, 0.0), 120.f, 720.f, TEXT("Cylinder"), FVector(0.5, 0.5, 6.0), Wood);

	// the moon, low and far behind the scene (not in the lobby studio: its backdrop wall hides it)
	if (bStudio)
	{
		// nothing
	}
	else if (UStaticMeshComponent* Moon = AddMesh(SQ::Mesh(TEXT("Sphere")), FVector(-5200.0, -2300.0, 2100.0), FRotator::ZeroRotator, FVector(6.0),
		GlowMID(SQUI::Hex(0xCF, 0xE0, 0xFF), 30.f)))
	{
		Moon->SetCastShadow(false);
	}
	for (UStaticMeshComponent* Flame : CandleFlames)
	{
		if (Flame)
		{
			Flame->SetCastShadow(false);
		}
	}
}

void ASQMenuShowroom::BuildLights()
{
	auto MakePoint = [this](const FVector& At, float Brightness, const FLinearColor& Colour, float Radius, bool bShadows)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetMobility(EComponentMobility::Movable);
		L->SetupAttachment(SceneRoot);
		L->SetRelativeLocation(At);
		L->SetIntensity(Brightness);
		L->SetLightColor(Colour);
		L->SetAttenuationRadius(Radius);
		L->SetCastShadows(bShadows);
		L->RegisterComponent();
		return L;
	};

	// cold moonlight from behind the scene: rim on the reaper and long shadows towards the camera
	// (not in the lobby studio: a directional light would light the whole arena too)
	if (!bStudio)
	{
		MoonLight = NewObject<UDirectionalLightComponent>(this);
		MoonLight->SetMobility(EComponentMobility::Movable);
		MoonLight->SetupAttachment(SceneRoot);
		MoonLight->SetRelativeRotation(FRotator(-34.f, 28.f, 0.f));
		MoonLight->SetIntensity(2.2f);
		MoonLight->SetLightColor(SQUI::Hex(0x9A, 0xB4, 0xFF));
		MoonLight->SetCastShadows(true);
		MoonLight->RegisterComponent();
	}

	// the lantern (flickers in Tick), a cool fill from the camera side, a ghostly soul glow at the reaper's feet
	LanternLight = MakePoint(FlameSpot, LanternBrightness, FLinearColor(1.f, 0.7f, 0.35f), 1600.f, true);
	LanternLight->SetSourceRadius(8.f);
	MakePoint(FVector(560.0, 300.0, 330.0), 9000.f, FLinearColor(0.55f, 0.62f, 1.f), 2400.f, false);
	SoulLight = MakePoint(FVector(45.0, 0.0, 25.0), 2600.f, SQUI::Soul, 380.f, false);
	// candles and the jack-o'-lantern
	MakePoint(FVector(120.0, -175.0, 55.0), 2200.f, FLinearColor(1.f, 0.62f, 0.3f), 320.f, false);
	MakePoint(FVector(150.0, 210.0, 35.0), 1800.f, FLinearColor(1.f, 0.45f, 0.1f), 260.f, false);
}

void ASQMenuShowroom::BuildWisps()
{
	FRandomStream Rng(7);
	UMaterialInstanceDynamic* WispMID = GlowMID(SQUI::Soul, 12.f);
	for (int32 i = 0; i < 9; ++i)
	{
		const float Ang = (i + Rng.FRandRange(-0.3f, 0.3f)) * UE_TWO_PI / 9.f;
		const float Dist = Rng.FRandRange(170.f, 520.f);
		const FVector Home(FMath::Cos(Ang) * Dist - 120.f, FMath::Sin(Ang) * Dist, Rng.FRandRange(60.f, 260.f));
		UStaticMeshComponent* Wisp = AddMesh(SQ::Mesh(TEXT("Sphere")), Home, FRotator::ZeroRotator, FVector(Rng.FRandRange(0.06f, 0.11f)), WispMID);
		if (!Wisp)
		{
			continue;
		}
		Wisp->SetCastShadow(false);
		Wisps.Add(Wisp);
		WispHome.Add(Home);
		WispPhase.Add(Rng.FRandRange(0.f, UE_TWO_PI));
	}
	// two of them light the ground a little
	for (int32 i = 0; i < FMath::Min(2, Wisps.Num()); ++i)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetMobility(EComponentMobility::Movable);
		L->SetupAttachment(SceneRoot);
		L->SetIntensity(1300.f);
		L->SetLightColor(SQUI::Soul);
		L->SetAttenuationRadius(260.f);
		L->SetCastShadows(false);
		L->RegisterComponent();
		WispLights.Add(L);
	}
}

void ASQMenuShowroom::SetupStudio(UTextureRenderTarget2D* Target, bool bPortrait)
{
	bStudio = true;
	bStudioPortrait = bPortrait;
	StudioTarget = Target;
}

void ASQMenuShowroom::BuildStudio()
{
	const USQData* D = USQData::Get(this);
	const float WallGlow = D ? D->Tune(TEXT("LobbyStudioWallGlow"), 2.f) : 2.f;

	// a dusk-violet wall far behind the set: the capture shows only this actor, so there is no sky
	if (UStaticMeshComponent* Wall = AddMesh(SQ::Mesh(TEXT("Cube")), FVector(-2300.0, 0.0, 900.0), FRotator::ZeroRotator, FVector(0.2, 90.0, 40.0),
		GlowMID(SQUI::Hex(0x2A, 0x1F, 0x45), WallGlow)))
	{
		Wall->SetCastShadow(false);
	}

	// key light from the camera side (no moonlight here) and an accent rim light behind the reaper
	UPointLightComponent* Key = NewObject<UPointLightComponent>(this);
	Key->SetMobility(EComponentMobility::Movable);
	Key->SetupAttachment(SceneRoot);
	Key->SetRelativeLocation(FVector(420.0, -280.0, 360.0));
	Key->SetIntensity(D ? D->Tune(TEXT("LobbyStudioKeyLight"), 9000.f) : 9000.f);
	Key->SetLightColor(FLinearColor(1.f, 0.9f, 0.8f));
	Key->SetAttenuationRadius(1600.f);
	Key->SetCastShadows(false);
	Key->RegisterComponent();

	RimLight = NewObject<UPointLightComponent>(this);
	RimLight->SetMobility(EComponentMobility::Movable);
	RimLight->SetupAttachment(SceneRoot);
	RimLight->SetRelativeLocation(FVector(-240.0, 80.0, 240.0));
	RimLight->SetIntensity(D ? D->Tune(TEXT("LobbyStudioRimLight"), 16000.f) : 16000.f);
	RimLight->SetLightColor(StudioAccent);
	RimLight->SetAttenuationRadius(900.f);
	RimLight->SetCastShadows(false);
	RimLight->RegisterComponent();

	// only ever seen by the scene capture: keep the pose ticking anyway
	ReaperMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	StudioCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("StudioCapture"));
	StudioCapture->SetupAttachment(Camera);
	StudioCapture->bCaptureEveryFrame = !bStudioPortrait;
	StudioCapture->bCaptureOnMovement = false;
	StudioCapture->bAlwaysPersistRenderingState = true;
	StudioCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	StudioCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	StudioCapture->ShowOnlyActors.Add(this);
	StudioCapture->FOVAngle = bStudioPortrait ? PortraitFov : CameraFov;
	StudioCapture->PostProcessSettings = Camera->PostProcessSettings;
	StudioCapture->PostProcessBlendWeight = 1.f;
	StudioCapture->ShowFlags.SetFog(false);
	StudioCapture->ShowFlags.SetVolumetricFog(false);
	StudioCapture->ShowFlags.SetAtmosphere(false);
	StudioCapture->TextureTarget = StudioTarget;
	StudioCapture->RegisterComponent();
}

void ASQMenuShowroom::SetAccent(const FLinearColor& Colour)
{
	StudioAccent = Colour;
	if (RimLight)
	{
		RimLight->SetLightColor(Colour);
	}
}

void ASQMenuShowroom::ShowReaperNow(FName ReaperId)
{
	ShownReaper = NAME_None; // "first show": no rise out of the ground
	ShowReaper(ReaperId);
	PopAt = -100.f;
	CheerAt = -100.f;
	PlayIdle();
}

void ASQMenuShowroom::CaptureNow(UTextureRenderTarget2D* NewTarget)
{
	if (!StudioCapture)
	{
		return;
	}
	if (NewTarget)
	{
		StudioTarget = NewTarget;
		StudioCapture->TextureTarget = NewTarget;
	}
	if (StudioCapture->TextureTarget)
	{
		StudioCapture->CaptureScene();
	}
}

void ASQMenuShowroom::SetCapturing(bool bOn)
{
	if (StudioCapture)
	{
		StudioCapture->bCaptureEveryFrame = bOn && !bStudioPortrait;
	}
}

FName ASQMenuShowroom::ReaperModel() const
{
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* Def = D ? D->FindReaper(ShownReaper) : nullptr;
	return Def ? Def->Model : NAME_None;
}

void ASQMenuShowroom::PlayIdle()
{
	OneShotLeft = 0.f;
	if (bHasArt)
	{
		SQArt::Play(ReaperMesh, SQArt::Anim(ReaperModel(), TEXT("Idle")), true);
	}
}

bool ASQMenuShowroom::PlayOnce(UAnimSequence* AnimSeq)
{
	if (!AnimSeq || !bHasArt)
	{
		return false;
	}
	// straight to PlayAnimation (SQArt::Play skips an animation that is already set, and a one-shot may repeat)
	ReaperMesh->PlayAnimation(AnimSeq, false);
	ReaperMesh->SetPlayRate(1.f);
	OneShotLeft = FMath::Max(0.25f, static_cast<float>(AnimSeq->GetPlayLength()) - 0.08f);
	return true;
}

void ASQMenuShowroom::ShowReaper(FName ReaperId)
{
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* Def = D ? D->FindReaper(ReaperId) : nullptr;
	if (!Def && D && D->Reapers.Num() > 0)
	{
		Def = &D->Reapers[0];
	}
	const FName NewId = Def ? Def->Id : ReaperId;
	const bool bFirst = ShownReaper.IsNone();
	if (!bFirst && NewId == ShownReaper)
	{
		return;
	}
	ShownReaper = NewId.IsNone() ? FName(TEXT("Grim")) : NewId;
	PopAt = Clock;
	NextFidget = Clock + 10.f;

	const FLinearColor Colour = Def ? Def->Color : SQUI::BoneWhite;
	const FName Model = SQArt::ResolveModel(Def ? Def->Model : NAME_None);
	USkeletalMesh* Skel = SQArt::Character(Model);
	bHasArt = Skel != nullptr;
	if (SoulLight)
	{
		SoulLight->SetLightColor(FMath::Lerp(SQUI::Soul, Colour, 0.45f));
	}

	if (!bHasArt)
	{
		// primitive reaper: a robe in the reaper's colour and a bone-white skull
		ReaperMesh->SetVisibility(false);
		ReaperWeapon->SetVisibility(false);
		if (!RobeMID)
		{
			RobeMID = SQ::ColorMID(this, Colour);
			FallbackRobe->SetMaterial(0, RobeMID);
		}
		else
		{
			RobeMID->SetVectorParameterValue(TEXT("Color"), Colour);
		}
		FallbackRobe->SetVisibility(true);
		FallbackSkull->SetVisibility(true);
		return;
	}

	FallbackRobe->SetVisibility(false);
	FallbackSkull->SetVisibility(false);
	if (Model != ShownModel)
	{
		ShownModel = Model;
		ReaperMesh->SetSkeletalMeshAsset(Skel);
		BaseScale = SQArt::FitScale(Skel, ReaperHeight);
		ReaperMesh->SetRelativeScale3D(FVector(BaseScale));
		ReaperMesh->SetRelativeRotation(FRotator(0.f, SQArt::YawOffset(this), 0.f));
	}
	const FLinearColor Tint = SQArt::SoftTint(Colour, 0.7f);
	if (!ReaperMID)
	{
		ReaperMID = SQArt::TintMID(this, SQArt::CharacterTexture(Model), Tint);
	}
	else
	{
		ReaperMID->SetVectorParameterValue(TEXT("Color"), Tint);
		if (UTexture* ShowTex = SQArt::CharacterTexture(Model)) ReaperMID->SetTextureParameterValue(TEXT("Tex"), ShowTex);
	}
	SQArt::SetAllMaterials(ReaperMesh, ReaperMID);
	SQArt::ApplyGlowSlots(ReaperMesh, this, SQArt::CharacterTexture(Model));
	ReaperMesh->SetVisibility(true);

	// the reaper's weapon in the right hand
	UStaticMesh* WeaponArt = SQArt::Weapon(Def ? Def->Weapon : NAME_None);
	const FName Socket = SQArt::HandSocket(ReaperMesh);
	if (WeaponArt && !Socket.IsNone())
	{
		ReaperWeapon->SetStaticMesh(WeaponArt);
		ReaperWeapon->AttachToComponent(ReaperMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		ReaperWeapon->SetRelativeTransform(FTransform::Identity);
		ReaperWeapon->SetRelativeScale3D(FVector(SQArt::FitLongest(WeaponArt, 120.f) / FMath::Max(0.0001f, BaseScale)));
		SQArt::SetAllMaterials(ReaperWeapon, ReaperMID);
		ReaperWeapon->SetVisibility(true);
	}
	else
	{
		ReaperWeapon->SetVisibility(false);
	}

	// A newly picked monster makes its entrance (the first one shown is already standing there):
	// its own SQ_Intro_<Id> take + the themed arrival (rise / poof / drop / materialise / grow / jolt) and effects
	if (bFirst)
	{
		PlayIdle();
		return;
	}
	StartEntrance(ShownReaper);
	const FName IntroTake(*FString::Printf(TEXT("SQ_Intro_%s"), *ShownReaper.ToString()));
	if (!PlayOnce(SQArt::Anim(Model, IntroTake)) && !PlayOnce(SQArt::Anim(Model, TEXT("Spawn_Ground_Skeletons"))))
	{
		PlayIdle();
	}
}

void ASQMenuShowroom::StartEntrance(FName ReaperId)
{
	const SQTheme::FEntranceSpec Spec = SQTheme::Entrance(ReaperId);
	EntranceAt = Clock;
	EntranceMode = static_cast<uint8>(Spec.Mode);
	EntranceHide = Spec.HideFor;
	EntranceMove = FMath::Max(0.05f, Spec.MoveFor);
	EntranceDrop = Spec.DropHeight;
	bEntranceHidden = Spec.Mode == SQTheme::EEntrance::Poof && Spec.HideFor > 0.f;
	if (bEntranceHidden)
	{
		ReaperMesh->SetVisibility(false);
		ReaperWeapon->SetVisibility(false);
	}
	UWorld* World = GetWorld();
	if (!World) return;
	// the lobby studio camera only renders this actor: let it see the effect particles too
	if (StudioCapture)
	{
		if (ASQSparks* Sparks = ASQSparks::Get(World))
		{
			StudioCapture->ShowOnlyActors.AddUnique(Sparks);
		}
	}
	const FVector Feet = ReaperRoot->GetComponentLocation();
	FVector ToCamera = Camera->GetComponentLocation() - Feet;
	ToCamera.Z = 0.0;
	SQTheme::PlayEntrance(World, ReaperId, Feet, ToCamera.GetSafeNormal(), FMath::Clamp(ReaperHeight / 190.f, 0.4f, 2.f));
}

void ASQMenuShowroom::EntranceTransform(FVector& OutScale, FVector& OutOffset, float& OutYaw)
{
	OutScale = FVector::OneVector;
	OutOffset = FVector::ZeroVector;
	OutYaw = 0.f;
	const float T = Clock - EntranceAt;
	if (T < 0.f || T > EntranceMove + EntranceHide + 0.6f) return;
	const float U = FMath::Clamp(T / EntranceMove, 0.f, 1.f);
	const float EaseOut = 1.f - FMath::Pow(1.f - U, 3.f);
	switch (static_cast<SQTheme::EEntrance>(EntranceMode))
	{
	case SQTheme::EEntrance::Rise:
		// climbs out of the ground, a small overshoot as it plants its feet
		OutOffset.Z = -ReaperHeight * 1.05f * (1.f - EaseOut);
		break;
	case SQTheme::EEntrance::Poof:
	{
		const float V = FMath::Clamp((T - EntranceHide) / 0.35f, 0.f, 1.f);
		const float C1 = 1.70158f, C3 = C1 + 1.f;
		const float Back = V >= 1.f ? 1.f : 1.f + C3 * FMath::Pow(V - 1.f, 3.f) + C1 * FMath::Pow(V - 1.f, 2.f);
		OutScale = FVector(FMath::Lerp(0.2f, 1.f, Back));
		break;
	}
	case SQTheme::EEntrance::Drop:
		if (U < 1.f)
		{
			OutOffset.Z = EntranceDrop * (1.f - U * U);           // falls, accelerating
		}
		else
		{
			const float L = FMath::Clamp((T - EntranceMove) / 0.22f, 0.f, 1.f);
			const float Squash = FMath::Sin(L * UE_PI) * (1.f - L);    // lands: squash, then stretch back
			OutScale = FVector(1.f + 0.12f * Squash, 1.f + 0.12f * Squash, 1.f - 0.18f * Squash);
		}
		break;
	case SQTheme::EEntrance::Materialize:
		OutScale = FVector(FMath::Lerp(0.05f, 1.f, EaseOut), FMath::Lerp(0.05f, 1.f, EaseOut), FMath::Lerp(0.4f, 1.f, EaseOut));
		OutYaw = 540.f * (1.f - EaseOut);
		break;
	case SQTheme::EEntrance::Grow:
	{
		const float C1 = 2.2f, C3 = C1 + 1.f;
		const float Back = U >= 1.f ? 1.f : 1.f + C3 * FMath::Pow(U - 1.f, 3.f) + C1 * FMath::Pow(U - 1.f, 2.f);
		OutScale = FVector(FMath::Lerp(0.3f, 1.f, EaseOut), FMath::Lerp(0.3f, 1.f, EaseOut), FMath::Max(0.05f, Back));
		break;
	}
	case SQTheme::EEntrance::Jolt:
		if (T > EntranceMove && T < EntranceMove + 0.35f)
		{
			const float J = 1.f - (T - EntranceMove) / 0.35f;
			OutOffset = FVector(FMath::Sin(T * 90.f) * 5.f * J, FMath::Cos(T * 77.f) * 4.f * J, 0.f);
		}
		break;
	default:
		break;
	}
}

void ASQMenuShowroom::Celebrate()
{
	CheerAt = Clock;
	NextFidget = Clock + 10.f;
	if (bHasArt)
	{
		PlayOnce(SQArt::Anim(ReaperModel(), TEXT("Cheer")));
	}
}

void ASQMenuShowroom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Step = FMath::Clamp(DeltaSeconds, 0.f, 0.1f);
	Clock += Step;

	// one-shots go back to Idle; now and then a little fidget
	if (OneShotLeft > 0.f)
	{
		OneShotLeft -= Step;
		if (OneShotLeft <= 0.f)
		{
			PlayIdle();
		}
	}
	else if (bHasArt && Clock > NextFidget)
	{
		NextFidget = Clock + FMath::FRandRange(10.f, 15.f);
		PlayOnce(SQArt::Anim(ReaperModel(), FMath::RandBool() ? FName(TEXT("Idle_B")) : FName(TEXT("Taunt"))));
	}

	// pop when the reaper changes; the primitive reaper bobs, and hops when it cheers
	const float PopT = FMath::Clamp((Clock - PopAt) / 0.55f, 0.f, 1.f);
	const float Pop = PopT >= 1.f ? 1.f : 0.88f + 0.12f * ShowroomBackOut(PopT);
	float Lift = 0.f;
	if (!bHasArt)
	{
		Lift = 4.f * FMath::Sin(Clock * 2.1f);
		const float CheerT = (Clock - CheerAt) / 0.4f;
		if (CheerT >= 0.f && CheerT < 2.f)
		{
			Lift += 34.f * FMath::Abs(FMath::Sin(CheerT * UE_PI));
		}
	}
	// the entrance (if one is playing) takes over the pop
	FVector EntScale;
	FVector EntOffset;
	float EntYaw = 0.f;
	EntranceTransform(EntScale, EntOffset, EntYaw);
	if (bEntranceHidden && Clock - EntranceAt >= EntranceHide)
	{
		bEntranceHidden = false;
		ReaperMesh->SetVisibility(bHasArt);
		ReaperWeapon->SetVisibility(bHasArt && ReaperWeapon->GetStaticMesh() != nullptr);
	}
	const bool bEntering = Clock - EntranceAt < EntranceMove + EntranceHide + 0.6f;
	ReaperRoot->SetRelativeScale3D((bEntering ? FVector::OneVector : FVector(Pop)) * EntScale);
	ReaperRoot->SetRelativeLocation(FVector(0.0, 0.0, Lift) + EntOffset);
	ReaperRoot->SetRelativeRotation(FRotator(0.f, RootYaw + EntYaw, 0.f));

	// camera: eases in for the REAPERS page, sways gently
	CloseBlend = FMath::FInterpTo(CloseBlend, bCloseUp ? 1.f : 0.f, Step, 2.6f);
	const float Ease = FMath::SmoothStep(0.f, 1.f, CloseBlend);
	FVector Spot = CameraSpot + (CloseSpot - CameraSpot) * Ease;
	FVector Aim = CameraAim + (CloseAim - CameraAim) * Ease;
	const float Phase = DriftSeconds > 0.f ? Clock * UE_TWO_PI / DriftSeconds : 0.f;
	Spot += FVector(0.0, DriftCm * FMath::Sin(Phase), DriftCm * 0.35f * FMath::Sin(Phase * 0.5f + 1.f));
	Aim += FVector(0.0, DriftCm * 0.2f * FMath::Sin(Phase + 0.6f), 0.0);
	if (bStudioPortrait)
	{
		// portrait captures: a still bust shot
		Spot = PortraitSpot;
		Aim = PortraitAim;
	}
	Camera->SetRelativeLocationAndRotation(Spot, (Aim - Spot).Rotation());

	// lantern flicker
	const float Flicker = 0.84f + 0.07f * FMath::Sin(Clock * 13.1f) + 0.05f * FMath::Sin(Clock * 27.3f + 1.3f) + 0.08f * FMath::PerlinNoise1D(Clock * 2.7f);
	if (LanternLight)
	{
		LanternLight->SetIntensity(LanternBrightness * Flicker);
	}
	if (FlameMID)
	{
		FlameMID->SetScalarParameterValue(TEXT("Glow"), 10.f * Flicker);
	}

	// soul wisps bob about; when the reaper changes or cheers they swirl round it for a moment
	const float Swirl = FMath::Clamp(1.f - (Clock - FMath::Max(PopAt, CheerAt)) / 1.6f, 0.f, 1.f);
	const float SwirlEase = Swirl * Swirl * (3.f - 2.f * Swirl);
	for (int32 i = 0; i < Wisps.Num(); ++i)
	{
		const float P = WispPhase[i];
		const FVector Bob(40.f * FMath::Sin(Clock * 0.6f + P), 40.f * FMath::Cos(Clock * 0.5f + P * 1.3f), 25.f * FMath::Sin(Clock * 1.3f + P * 2.f));
		const float OrbitAng = Clock * 4.f + P;
		const FVector Orbit(FMath::Cos(OrbitAng) * 85.f, FMath::Sin(OrbitAng) * 85.f, 40.f + 60.f * (P / UE_TWO_PI));
		const FVector Home = WispHome[i] + Bob;
		const FVector At = Home + (Orbit - Home) * (SwirlEase * 0.75f);
		Wisps[i]->SetRelativeLocation(At);
		if (WispLights.IsValidIndex(i) && WispLights[i])
		{
			WispLights[i]->SetRelativeLocation(At);
		}
	}
}
