// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQHordeManager.h"
#include "Net/UnrealNetwork.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "CoreGlobals.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "SQArt.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQPlayerState.h"
#include "SQCharacter.h"
#include "SQLantern.h"
#include "SQBoss.h"
#include "SQArena.h"
#include "SQFx.h"
#include "SQFxRecipe.h"
#include "SQPerf.h"
#include "SQChest.h"
#include "SQQuests.h"
#include "SQAugments.h"
#include "SQTutorial.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "DrawDebugHelpers.h"

namespace
{
	int16 Quantize(double V)
	{
		return (int16)FMath::Clamp(FMath::RoundToInt(V / 4.0), -32767, 32767);
	}

	/** Stable pseudo-random 0..1 per enemy Id (scale jitter etc.) */
	float IdHash01(uint32 InId, uint32 Salt)
	{
		uint32 H = InId * 2654435761u + Salt * 40503u;
		H ^= H >> 15;
		H *= 2246822519u;
		H ^= H >> 13;
		return (float)(H & 0xFFFFu) / 65535.f;
	}

	constexpr int32 MaxHitIdsPerSnapshot = 128;
	constexpr int32 MaxNetEntries = 2000;     // per replicated snapshot array (engine default limit is 2048 elements)
	constexpr float HitFlashTime = 0.12f;
	constexpr float HitSquashTime = 0.1f;
	constexpr float HitJoltTime = 0.15f;
	constexpr float HitJoltDist = 22.f;
	constexpr float RiseTime = 0.5f;
	constexpr uint8 EnemyStateMask = 0x07;
	constexpr uint8 EnemyBurstBit = 0x08;
	// Economy pass: look flags in the replicated state byte (bits 4-5; every reader masks the low bits)
	constexpr uint8 EnemyCrownBit = 0x10;
	constexpr uint8 EnemyMinibossBit = 0x20;
	// Reap core (Nov 9): ripe for reaping (below ReapRipeFraction of max HP), bit 6. Bit 7 (0x80) is still free.
	constexpr uint8 EnemyRipeBit = 0x40;
	constexpr int32 MaxRipeMarkers = 256;  // ripe ring / skull instances drawn at most (the pools grow in steps of 32 and never rebuild per frame)
	const FLinearColor SQHMSoulTeal(0.36f, 0.95f, 0.84f);
	constexpr int32 MaxLaunchIdsPerSnapshot = 64;
	constexpr int32 MaxDebris = 140;
	constexpr float DebrisGravity = -2200.f;

	/** Seconds an enemy stays inert while it enters the world (server) = length of its spawn animation (client) */
	float SQEnemySpawnTime(ESQEnemySpawn Style)
	{
		switch (Style)
		{
		case ESQEnemySpawn::Crawl: return 0.9f;
		case ESQEnemySpawn::Drop: return 0.6f;
		case ESQEnemySpawn::Burst: return 0.3f;
		case ESQEnemySpawn::Fade: return 0.6f;
		default: return RiseTime;
		}
	}

	ESQEnemySpawn SQEnemySpawnStyle(const FSQEnemyDef& Def, uint8 NetState)
	{
		return (NetState & EnemyBurstBit) ? ESQEnemySpawn::Burst : Def.Spawn;
	}

	ESQEnemyAnimState SQEnemyAnim(uint8 NetState)
	{
		return static_cast<ESQEnemyAnimState>(NetState & EnemyStateMask);
	}

	/** The state byte without the ripe flag: turning ripe mid-windup must not restart the pose clock or the attack clip */
	uint8 SQEnemyPoseState(uint8 NetState)
	{
		return static_cast<uint8>(NetState & static_cast<uint8>(~EnemyRipeBit));
	}

	/** Procedural body motion layered on top of the walk clip / instanced body (cm and degrees) */
	struct FSQGaitPose
	{
		float Bob = 0.f;
		float Pitch = 0.f;   // + leans forward
		float Roll = 0.f;
		float YawJitter = 0.f;
		float SquashXY = 1.f;
		float SquashZ = 1.f;
	};

	/** T: seconds (stepped for clay instancing), Phase: per-enemy offset, MoveAmt: observed speed / type speed */
	FSQGaitPose SQGaitPose(ESQEnemyGait Gait, float T, float Phase, float MoveAmt, bool bSkinned)
	{
		FSQGaitPose G;
		const float Mv = FMath::Clamp(MoveAmt, 0.f, 1.5f);
		const float Walk = FMath::Min(Mv, 1.f);
		switch (Gait)
		{
		case ESQEnemyGait::Shamble:
			G.Roll = 6.f * FMath::Sin(T * 3.f + Phase) * Walk;
			G.Pitch = 6.f * Walk;
			G.Bob = 3.f * FMath::Abs(FMath::Sin(T * 6.f + Phase)) * Walk;
			break;
		case ESQEnemyGait::Lurch:
			// leans into each surge (the server pulses its speed), rocks over the bad leg
			G.Pitch = 4.f + 16.f * Mv;
			G.Roll = 9.f * FMath::Sin(T * 3.2f + Phase);
			G.Bob = -4.f * FMath::Abs(FMath::Sin(T * 3.2f + Phase));
			break;
		case ESQEnemyGait::Scuttle:
			G.SquashZ = bSkinned ? 1.f : 0.82f;
			G.SquashXY = bSkinned ? 1.f : 1.12f;
			G.YawJitter = 9.f * FMath::Sin(T * 23.f + Phase) * Walk;
			G.Bob = 3.f * FMath::Abs(FMath::Sin(T * 20.f + Phase)) * Walk;
			G.Pitch = 10.f * Walk;
			break;
		case ESQEnemyGait::Hop:
		{
			// bunny hops: up in an arc, squash on landing, stretch in the air
			const float HopT = FMath::Fmod(T * 1.6f + Phase * 0.16f, 1.f);
			const float Air = FMath::Sin(HopT * PI);
			const float Amp = 0.35f + 0.65f * Walk;
			G.Bob = 70.f * Air * Amp;
			G.SquashZ = HopT < 0.12f ? 0.72f + 2.3f * HopT : 1.f + 0.12f * Air * Amp;
			G.SquashXY = HopT < 0.12f ? 1.25f - 2.f * HopT : 1.f - 0.05f * Air;
			G.Pitch = -8.f * FMath::Cos(HopT * PI) * Amp;
			break;
		}
		case ESQEnemyGait::ZigZag:
			G.Roll = 28.f * FMath::Sin(T * 5.f + Phase);
			G.Bob = 25.f + 15.f * FMath::Sin(T * 3.f + Phase);
			G.Pitch = 12.f * Walk;
			break;
		case ESQEnemyGait::Drift:
			G.Bob = 30.f + 12.f * FMath::Sin(T * 2.f + Phase);
			G.Roll = 8.f * FMath::Sin(T * 1.3f + Phase);
			G.Pitch = 6.f * Walk;
			break;
		case ESQEnemyGait::Charge:
			G.Pitch = 8.f + 10.f * Mv;
			G.Roll = 5.f * FMath::Sin(T * 5.f + Phase) * Walk;
			break;
		case ESQEnemyGait::Crawl:
			G.SquashZ = bSkinned ? 1.f : 0.45f;
			G.SquashXY = bSkinned ? 1.f : 1.3f;
			G.Roll = 7.f * FMath::Sin(T * 4.f + Phase) * Walk;
			break;
		case ESQEnemyGait::Stomp:
		{
			const float Step = FMath::Abs(FMath::Sin(T * 2.6f + Phase));
			G.Roll = 11.f * FMath::Sin(T * 2.6f + Phase) * Walk;
			G.Bob = -5.f * (1.f - Step) * Walk;
			G.SquashZ = 1.f - 0.06f * (1.f - Step) * Walk;
			G.SquashXY = 1.f + 0.05f * (1.f - Step) * Walk;
			break;
		}
		case ESQEnemyGait::Circle:
			G.Roll = -16.f * Walk;   // banks into the orbit
			G.Pitch = 10.f * Walk;
			G.Bob = 5.f * FMath::Abs(FMath::Sin(T * 12.f + Phase)) * Walk;
			break;
		case ESQEnemyGait::Sprint:
			G.Pitch = 16.f * Mv;
			G.Bob = 6.f * FMath::Abs(FMath::Sin(T * 14.f + Phase)) * Walk;
			break;
		default:
			break;
		}
		return G;
	}

	/** Bone or socket on an imported KayKit mesh ("hand.r" may have become "hand_r" on import). NAME_None when missing. */
	FName SQResolveEnemyBone(const USkeletalMesh* Mesh, FName Bone, int32& OutIndex)
	{
		OutIndex = INDEX_NONE;
		if (!Mesh || Bone.IsNone()) return NAME_None;
		const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
		OutIndex = RefSkel.FindBoneIndex(Bone);
		if (OutIndex != INDEX_NONE) return Bone;
		const FName Underscored(*Bone.ToString().Replace(TEXT("."), TEXT("_")));
		OutIndex = RefSkel.FindBoneIndex(Underscored);
		return OutIndex != INDEX_NONE ? Underscored : NAME_None;
	}

	/** Component-space reference pose of a bone */
	FTransform SQRefBoneComponentSpace(const USkeletalMesh* Mesh, int32 BoneIndex)
	{
		FTransform Acc = FTransform::Identity;
		if (!Mesh) return Acc;
		const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
		const TArray<FTransform>& Locals = RefSkel.GetRefBonePose();
		int32 Walk = BoneIndex;
		while (Walk != INDEX_NONE && Locals.IsValidIndex(Walk))
		{
			Acc = Acc * Locals[Walk];
			Walk = RefSkel.GetParentIndex(Walk);
		}
		return Acc;
	}

	/** First take of a '|' fallback list that exists for this model */
	UAnimSequence* SQFirstAnim(FName Model, const TArray<FName>& Names, FName* OutName = nullptr)
	{
		for (const FName& Candidate : Names)
		{
			if (UAnimSequence* Found = SQArt::Anim(Model, Candidate))
			{
				if (OutName) *OutName = Candidate;
				return Found;
			}
		}
		return nullptr;
	}

	bool SQIsHeadBone(FName Bone)
	{
		return Bone.IsNone() || Bone == FName(TEXT("head"));
	}

	/** World Z a flyer's body is drawn at: smoothed ground + smoothed altitude, never into the terrain under it */
	float SQFlyerBaseZ(float SmoothedGround, float FlyHeight, float GroundHere)
	{
		return FMath::Max(SmoothedGround + FlyHeight, GroundHere + 25.f);
	}

	/**
	 * Spawn "pop" right after an enemy finishes entering the world: a quick stretch up, then a squash, settling at 1.
	 * K = seconds since the spawn ended. Returns the Z scale multiplier (XY gets the inverse half).
	 */
	float SQSpawnPopZ(float K)
	{
		constexpr float PopTime = 0.28f;
		if (K < 0.f || K >= PopTime) return 1.f;
		const float T = K / PopTime;
		return 1.f + 0.2f * FMath::Sin(2.f * PI * T) * (1.f - T);
	}

	/** Far "flipbook impostor" frame: /Game/Art/Impostors/SM_<Model>_Walk_<Frame> (nullptr when the art isn't imported yet) */
	UStaticMesh* SQHMLoadImpostorFrame(FName Model, int32 Frame)
	{
		if (Model.IsNone()) return nullptr;
		const FString ImpName = FString::Printf(TEXT("SM_%s_Walk_%d"), *Model.ToString(), Frame);
		const FString ImpPath = FString::Printf(TEXT("/Game/Art/Impostors/%s.%s"), *ImpName, *ImpName);
		return LoadObject<UStaticMesh>(nullptr, *ImpPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

ASQHordeManager::ASQHordeManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(15.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void ASQHordeManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQHordeManager, EnemyNet);
	DOREPLIFETIME(ASQHordeManager, PickupNet);
	DOREPLIFETIME(ASQHordeManager, ProjNet);
	DOREPLIFETIME(ASQHordeManager, HitNet);
}

void ASQHordeManager::BeginPlay()
{
	Super::BeginPlay();
	if (const USQData* D = USQData::Get(this))
	{
		ArenaHalf = D->Tune(TEXT("ArenaHalfSize"), 6000.f);
		SetNetUpdateFrequency(D->Tune(TEXT("NetRate"), 15.f));
	}
	SetActorLocation(FVector::ZeroVector);
	InitTypeShapes();
	EnsureRenderComponents();
}

void ASQHordeManager::EnsureRenderComponents()
{
	const USQData* D = USQData::Get(this);
	if (!D || GetNetMode() == NM_DedicatedServer) return;

	auto MakeISM = [this](FName Shape, const FLinearColor& Color, bool bShadows)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetStaticMesh(SQ::Mesh(Shape));
		ISM->SetMaterial(0, SQ::ColorMID(this, Color));
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(bShadows);
		ISM->SetupAttachment(Root);
		ISM->RegisterComponent();
		return ISM;
	};

	if (EnemyISMs.Num() == 0)
	{
		const FName BodyBone(TEXT("Body"));
		for (const FSQEnemyDef& E : D->Enemies)
		{
			// Prop accessory material (Halloween Bits palette, tinted toward Accent, glowing for candles / lanterns / elites)
			UStaticMesh* PropMesh = SQArt::Prop(E.Prop);
			UMaterialInstanceDynamic* PropMID = nullptr;
			if (PropMesh)
			{
				PropMID = SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(E.Accent, 0.35f));
				if (PropMID) PropMID->SetScalarParameterValue(TEXT("Glow"), E.Glow);
			}
			PropMIDs.Add(PropMID);
			const bool bPropBody = PropMesh && (E.PropBone == BodyBone || E.Model.IsNone());
			TypePropIsBody.Add(bPropBody);
			TypePropFit.Add(PropMesh ? SQArt::FitLongest(PropMesh, E.PropSize * E.Scale) : 1.f);
			if (bPropBody)
			{
				// The prop IS the enemy (e.g. a floating candle skull): instanced prop at every distance
				UInstancedStaticMeshComponent* BodyISM = MakeISM(E.Shape, E.Color, true);
				BodyISM->SetStaticMesh(PropMesh);
				if (PropMID) BodyISM->SetMaterial(0, PropMID);
				EnemyISMs.Add(BodyISM);
				PropISMs.Add(nullptr);
			}
			else
			{
				UInstancedStaticMeshComponent* BodyISM = MakeISM(E.Shape, E.Color, true);
				if (E.Behaviour == ESQEnemyBehaviour::Elite && E.Glow > 0.f)
				{
					if (UMaterialInstanceDynamic* BodyMID = Cast<UMaterialInstanceDynamic>(BodyISM->GetMaterial(0))) BodyMID->SetScalarParameterValue(TEXT("Glow"), E.Glow);
				}
				EnemyISMs.Add(BodyISM);
				UInstancedStaticMeshComponent* FarPropISM = nullptr;
				if (PropMesh)
				{
					FarPropISM = MakeISM(TEXT("Sphere"), E.Accent, false);
					FarPropISM->SetStaticMesh(PropMesh);
					if (PropMID) FarPropISM->SetMaterial(0, PropMID);
				}
				PropISMs.Add(FarPropISM);
			}
			SplatISMs.Add(MakeISM(TEXT("Sphere"), E.Color * 0.7f, false));
		}
		// Attack telegraphs: dark outer disc (where it will hit) + bright fill that grows until the hit lands
		TeleOuterISM = MakeISM(TEXT("Cylinder"), FLinearColor(0.35f, 0.02f, 0.03f), false);
		TeleFillISM = MakeISM(TEXT("Cylinder"), FLinearColor(1.f, 0.22f, 0.08f), false);
		// Flyers' ground shadows (soft dark discs; they shrink as the flyer climbs)
		ShadowISM = MakeISM(TEXT("Cylinder"), FLinearColor(0.015f, 0.012f, 0.02f), false);
		if (UMaterialInstanceDynamic* FillMID = Cast<UMaterialInstanceDynamic>(TeleFillISM->GetMaterial(0))) FillMID->SetScalarParameterValue(TEXT("Glow"), 1.5f);
		XPISM = MakeISM(TEXT("Sphere"), FLinearColor(0.10f, 0.78f, 0.22f), false); // fallback only (crystal ladder: PickupTierISMs)
		// Economy pass: spinning gold coins, gold crowns over crowned elites / minibosses, a red ring under a miniboss
		GoldISM = MakeISM(TEXT("Cylinder"), FLinearColor(1.f, 0.78f, 0.15f), false);
		CrownISM = MakeISM(TEXT("Cone"), FLinearColor(1.f, 0.8f, 0.2f), false);
		MiniRingISM = MakeISM(TEXT("Cylinder"), FLinearColor(0.9f, 0.1f, 0.12f), false);
		for (UInstancedStaticMeshComponent* GlowISM : { GoldISM.Get(), CrownISM.Get(), MiniRingISM.Get() })
		{
			if (UMaterialInstanceDynamic* GlowMID = GlowISM ? Cast<UMaterialInstanceDynamic>(GlowISM->GetMaterial(0)) : nullptr) GlowMID->SetScalarParameterValue(TEXT("Glow"), 1.2f);
		}
		// Reap core: a glowing soul-teal disc under every ripe enemy (the body hides its middle from the top-down camera, so it reads
		// as an outline ring) and a tiny skull tag over its head. One ISM each for the whole horde, skinned and impostor alike.
		RipeRingISM = MakeISM(TEXT("Cylinder"), SQHMSoulTeal, false);
		RipeSkullISM = MakeISM(TEXT("Sphere"), SQHMSoulTeal, false);
		if (UStaticMesh* SkullMesh = SQArt::Prop(TEXT("skull")))
		{
			RipeSkullISM->SetStaticMesh(SkullMesh);
			RipeSkullFit = SQArt::FitLongest(SkullMesh, 30.f);
		}
		for (UInstancedStaticMeshComponent* RipeISM : { RipeRingISM.Get(), RipeSkullISM.Get() })
		{
			if (UMaterialInstanceDynamic* RipeMID = RipeISM ? Cast<UMaterialInstanceDynamic>(RipeISM->GetMaterial(0)) : nullptr) RipeMID->SetScalarParameterValue(TEXT("Glow"), 2.f);
		}
		// Souls are soul-teal (gold is yellow coins, XP is green crystals: each reads at a glance)
		// Souls (Nov 10 2026, our own spectral look): a white-hot core orb inside a teal teardrop wisp that trails upward,
		// three motes circling it and a soft glow pool on the ground. Grey core + dim pool when your sack is full.
		SoulISM = MakeISM(TEXT("Sphere"), FLinearColor(0.80f, 1.00f, 0.96f), false);      // core
		SoulTailISM = MakeISM(TEXT("Sphere"), SQHMSoulTeal, false);                          // wisp body / tail
		SoulMoteISM = MakeISM(TEXT("Sphere"), FLinearColor(0.55f, 1.00f, 0.90f), false);   // orbiting motes
		SoulPoolISM = MakeISM(TEXT("Cylinder"), SQHMSoulTeal, false);                        // ground glow
		SoulFullISM = MakeISM(TEXT("Sphere"), FLinearColor(0.42f, 0.46f, 0.48f), false);   // full sack: grey core
		if (UStaticMesh* WispMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/FX/SM_FX_Flame.SM_FX_Flame"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			SoulTailISM->SetStaticMesh(WispMesh);
			SoulNorm = 100.f / FMath::Max(1.f, static_cast<float>(WispMesh->GetBoundingBox().GetSize().GetMax()));
		}
		const TPair<UInstancedStaticMeshComponent*, float> SoulGlows[] = {
			{ SoulISM.Get(), 5.f }, { SoulTailISM.Get(), 2.4f }, { SoulMoteISM.Get(), 3.5f }, { SoulPoolISM.Get(), 1.2f }, { SoulFullISM.Get(), 0.f } };
		for (const TPair<UInstancedStaticMeshComponent*, float>& SoulGlow : SoulGlows)
		{
			if (UMaterialInstanceDynamic* SoulMID = SoulGlow.Key ? Cast<UMaterialInstanceDynamic>(SoulGlow.Key->GetMaterial(0)) : nullptr)
			{
				SoulMID->SetScalarParameterValue(TEXT("Glow"), SoulGlow.Value);
			}
		}
		// Nov 12 2026 (from the first real screenshots: the opaque cone + white ball read as pale chess pawns, and a full
		// sack left grey balls all over the floor). Souls are now LIGHT, not solids: every piece uses the additive glow
		// material (M_SQGlow) - a small bright teal core, a soft halo round it, a see-through flame tail and a faint pool on
		// the ground. A full sack only dims them (still teal). Without the material the Nov 10 look above stays.
		bSoulGlow = false;
		SoulHaloISM = MakeISM(TEXT("Sphere"), SQHMSoulTeal, false);
		{
			const FLinearColor Teal(0.10f, 0.95f, 0.80f);
			UMaterialInstanceDynamic* CoreGlow = SQArt::GlowMID(this, FLinearColor(0.35f, 1.00f, 0.88f), 3.2f, 1.f);
			UMaterialInstanceDynamic* HaloGlow = SQArt::GlowMID(this, Teal, 0.55f, 1.f);
			UMaterialInstanceDynamic* TailGlow = SQArt::GlowMID(this, Teal, 0.9f, 0.3f);
			UMaterialInstanceDynamic* MoteGlow = SQArt::GlowMID(this, FLinearColor(0.45f, 1.00f, 0.90f), 2.4f, 1.f);
			UMaterialInstanceDynamic* PoolGlow = SQArt::GlowMID(this, Teal, 0.22f, 0.f);
			UMaterialInstanceDynamic* FullGlow = SQArt::GlowMID(this, FLinearColor(0.08f, 0.55f, 0.50f), 0.45f, 1.f);
			if (CoreGlow && HaloGlow && TailGlow && MoteGlow && PoolGlow && FullGlow)
			{
				SoulISM->SetMaterial(0, CoreGlow);
				SoulHaloISM->SetMaterial(0, HaloGlow);
				SoulTailISM->SetMaterial(0, TailGlow);
				SoulMoteISM->SetMaterial(0, MoteGlow);
				SoulPoolISM->SetMaterial(0, PoolGlow);
				SoulFullISM->SetMaterial(0, FullGlow);
				bSoulGlow = true;
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("SQHorde: M_SQGlow not found (run Scripts/setup_project.py) - souls use the solid look this run"));
			}
		}
		// Pickup ladders (Nov 10 2026): XP crystal 1 / 2 / cluster / white prism; gold coin / coin stack / purse / gold bar (effect-kit meshes)
		{
			struct FTierDef { const TCHAR* Kit; const TCHAR* Fallback; FLinearColor Color; float Glow; };
			const FTierDef Tiers[8] = {
				{ TEXT("Crystal1"), TEXT("Sphere"), FLinearColor(0.06f, 0.80f, 0.18f), 2.0f },
				{ TEXT("Crystal2"), TEXT("Sphere"), FLinearColor(0.10f, 0.90f, 0.24f), 2.3f },
				{ TEXT("Crystal3"), TEXT("Sphere"), FLinearColor(0.42f, 1.00f, 0.55f), 2.4f },
				{ TEXT("Crystal4"), TEXT("Sphere"), FLinearColor(0.95f, 1.00f, 0.97f), 4.0f },
				{ TEXT("Coin"), TEXT("Cylinder"), FLinearColor(1.00f, 0.74f, 0.10f), 1.2f },
				{ TEXT("CoinStack"), TEXT("Cylinder"), FLinearColor(1.00f, 0.70f, 0.08f), 1.4f },
				{ TEXT("Purse"), TEXT("Sphere"), FLinearColor(0.60f, 0.36f, 0.12f), 0.5f },
				{ TEXT("GoldBar"), TEXT("Cube"), FLinearColor(1.00f, 0.82f, 0.25f), 2.2f },
			};
			for (const FTierDef& Tier : Tiers)
			{
				UInstancedStaticMeshComponent* TierISM = MakeISM(Tier.Fallback, Tier.Color, false);
				float Norm = 1.f;
				const FString KitPath = FString::Printf(TEXT("/Game/Art/FX/SM_FX_%s.SM_FX_%s"), Tier.Kit, Tier.Kit);
				if (UStaticMesh* KitMesh = LoadObject<UStaticMesh>(nullptr, *KitPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					TierISM->SetStaticMesh(KitMesh);
					Norm = 100.f / FMath::Max(1.f, static_cast<float>(KitMesh->GetBoundingBox().GetSize().GetMax()));
				}
				if (UMaterialInstanceDynamic* TierMID = Cast<UMaterialInstanceDynamic>(TierISM->GetMaterial(0))) TierMID->SetScalarParameterValue(TEXT("Glow"), Tier.Glow);
				PickupTierISMs.Add(TierISM);
				PickupTierNorm.Add(Norm);
			}
			ScratchPickupTier.SetNum(PickupTierISMs.Num());
		}
		ProjISM = MakeISM(TEXT("Cube"), FLinearColor(0.75f, 0.8f, 0.9f), false);
		HostileProjISM = MakeISM(TEXT("Sphere"), FLinearColor(0.9f, 0.15f, 0.2f), false);
	}
}

void ASQHordeManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const USQData* TickData = USQData::Get(this))
	{
		BlockHeightMult = FMath::Clamp(TickData->Tune(TEXT("BodyBlockHeightMult"), 0.8f), 0.1f, 2.f);
		RipeFraction = FMath::Clamp(TickData->Tune(TEXT("ReapRipeFraction"), 0.25f), 0.f, 0.95f);
		if (!bTypeShapesDone || TypeRadius.Num() != TickData->Enemies.Num()) InitTypeShapes(); // late data / SQReload
	}
	if (HasAuthority())
	{
		ServerSim(DeltaSeconds);
	}
	RenderAll(DeltaSeconds);
}

// ================================================================ server simulation

FIntPoint ASQHordeManager::Cell(const FVector2D& P) const
{
	return FIntPoint(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize));
}

FVector2D ASQHordeManager::ClampToArena(const FVector2D& P) const
{
	const float H = ArenaHalf - 60.f;
	return FVector2D(FMath::Clamp(P.X, -H, H), FMath::Clamp(P.Y, -H, H));
}

void ASQHordeManager::RebuildGrid()
{
	Grid.Reset();
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		Grid.FindOrAdd(Cell(Enemies[i].Pos)).Add(i);
	}
}

void ASQHordeManager::ServerSim(float DeltaSeconds)
{
	SimClock += DeltaSeconds;
	// Compact dead entries first so grid indices stay valid for the whole frame
	Enemies.RemoveAll([](const FEnemy& E) { return !E.bAlive; });
	Pickups.RemoveAll([](const FPickup& P) { return !P.bAlive; });
	Projectiles.RemoveAll([](const FProjectile& P) { return !P.bAlive; });
	if (PendingSpawns.Num() > 0)
	{
		const TArray<FPendingSpawn> ToSpawn = MoveTemp(PendingSpawns);
		PendingSpawns.Reset();
		for (const FPendingSpawn& S : ToSpawn) SpawnEnemy(S.Type, S.Pos, S.bBurst);
	}
	RebuildGrid();

	TArray<ASQCharacter*> Players;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		Players.Add(*It);
	}

	RunCombos(Players);
	SimSpawning(DeltaSeconds, Players);
	TickCatchUp(DeltaSeconds, Players);
	SimEnemies(DeltaSeconds, Players);
	SimPickups(DeltaSeconds, Players);
	MergePickups(DeltaSeconds, Players);
	SimProjectiles(DeltaSeconds, Players);
	UpdateMinibossTracker();

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextNetTime)
	{
		NextNetTime = Now + 1.f / FMath::Max(1.f, GetNetUpdateFrequency());
		BuildSnapshots();
	}
}

void ASQHordeManager::RunCombos(const TArray<ASQCharacter*>& Players)
{
	// Co-op combos (Nov 23 2026): read the dials once a step, then fire the bursts DamageEnemy queued last step
	if (const USQData* ComboData = USQData::Get(this))
	{
		ComboWindow = ComboData->Tune(TEXT("ComboEnabled"), 1.f) > 0.5f ? FMath::Clamp(ComboData->Tune(TEXT("ComboWindow"), 4.f), 0.f, 30.f) : 0.f;
		ComboBurst = FMath::Clamp(ComboData->Tune(TEXT("ComboBurst"), 0.6f), 0.f, 5.f);
		ComboRadius = FMath::Clamp(ComboData->Tune(TEXT("ComboRadius"), 260.f), 50.f, 1200.f);
		ComboCooldown = FMath::Clamp(ComboData->Tune(TEXT("ComboCooldown"), 0.35f), 0.05f, 10.f);
		bComboSolo = Players.Num() <= 1 && ComboData->Tune(TEXT("ComboSolo"), 1.f) > 0.5f;
	}
	if (PendingCombos.Num() == 0) return;
	const TArray<FPendingCombo> ToFire = MoveTemp(PendingCombos);
	PendingCombos.Reset();
	bInCombo = true;
	for (const FPendingCombo& Combo : ToFire)
	{
		ASQCharacter* By = Combo.By.Get();
		if (!By || Combo.Damage <= 0.f) continue;
		ApplyAreaDamage(Combo.At, ComboRadius, Combo.Damage, By, 0.f);
		By->OnSquadCombo(Combo.At, ComboRadius, Combo.Damage);
	}
	bInCombo = false;
}

int32 ASQHordeManager::PickEnemyType(int32 Night, float Progress) const
{
	const USQData* D = USQData::Get(this);
	if (!D || D->Enemies.Num() == 0) return -1;
	const int32 Col = FMath::Clamp(Night, 0, 2);
	// Management Visit clause: elites weigh EliteMult x more
	const UWorld* PickWorld = GetWorld();
	const ASQGameState* PickGS = PickWorld ? PickWorld->GetGameState<ASQGameState>() : nullptr;
	const float EliteMult = PickGS ? FMath::Max(0.f, PickGS->ModProduct(&FSQModifierDef::EliteMult) * PickGS->EventMult(&FSQEventDef::EliteMult)) : 1.f;
	auto TypeWeight = [Col, EliteMult](const FSQEnemyDef& Each) { return Each.Weight[Col] * (Each.Behaviour == ESQEnemyBehaviour::Elite ? EliteMult : 1.f); };
	float Total = 0.f;
	for (const FSQEnemyDef& E : D->Enemies)
	{
		if (Progress >= E.MinProgress) Total += TypeWeight(E);
	}
	if (Total <= 0.f) return 0;
	float Roll = FMath::FRandRange(0.f, Total);
	for (int32 i = 0; i < D->Enemies.Num(); ++i)
	{
		const FSQEnemyDef& E = D->Enemies[i];
		if (Progress < E.MinProgress) continue;
		Roll -= TypeWeight(E);
		if (Roll <= 0.f) return i;
	}
	return 0;
}

void ASQHordeManager::SimSpawning(float DeltaSeconds, const TArray<ASQCharacter*>& Players)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D || Players.Num() == 0) return;
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) return;
	// The Long Haunt: nothing spawns while the host decides between cashing out and another loop
	if (GS->LoopChoiceTimeLeft > 0.f) return;

	// Solo Training Shift (UI agent's tutorial): the horde holds back until the Reap tip is done; a few slow ones wait near the reaper
	if (GS->Phase == ESQPhase::Night && USQTutorial::ShouldHoldSpawns(this))
	{
		SimTutorialHold(Players, D);
		SpawnAccum = 0.f;
		return;
	}

	// Early-curve pass (Nov 9 reap core; Docs/Balance.md section 12): a time curve instead of a flat act lerp (SpawnCurveRate)
	float Rate = SpawnCurveRate(GS, D);
	Rate *= 1.f + D->Tune(TEXT("PlayerSpawnRateBonus"), 0.5f) * (GS->PlayerArray.Num() - 1);
	if (GS->Phase == ESQPhase::Boss) Rate *= D->Tune(TEXT("BossSpawnRateMult"), 0.35f);
	// Economy pass: Difficulty and the Double Booking clause raise the horde; the Final Swarm adds its tiers on top
	Rate *= (1.f + FMath::Max(0.f, GS->Difficulty)) * GS->ModProduct(&FSQModifierDef::SpawnMult);
	// Run events (Blood Moon, the Tolling Totem): the on-screen cap below still bounds the horde
	Rate *= GS->EventMult(&FSQEventDef::SpawnMult);
	if (GS->bFinalSwarm) SimFinalSwarm(DeltaSeconds, GS, D);

	// The regular loop also respects the on-screen cap curve (30 at minute 1, rising); swarms, nests and summons don't (HardCap only)
	const int32 MaxAlive = GetOnScreenCap();
	const float RMin = D->Tune(TEXT("SpawnRadiusMin"), 2200.f);
	const float RMax = FMath::Max(RMin, D->Tune(TEXT("SpawnRadiusMax"), 3000.f));
	// Never spawn on top of anyone: ClampToArena can pull a spot close to the anchor, or next to another reaper
	const float MinPlayerDist = FMath::Min(RMin, D->Tune(TEXT("SpawnMinPlayerDistance"), 1500.f));

	SpawnSpots.Reset();
	for (const ASQCharacter* SpotChar : Players)
	{
		if (SpotChar) SpawnSpots.Add(SpotChar->Pos2D());
	}
	if (SpawnSpots.Num() == 0) return;

	SpawnAccum += Rate * DeltaSeconds;
	while (SpawnAccum >= 1.f)
	{
		SpawnAccum -= 1.f;
		if (Enemies.Num() >= MaxAlive) { SpawnAccum = 0.f; break; }
		const int32 Type = PickEnemyType(GS->NightIndex, GS->NightProgress());
		if (Type < 0) break;
		for (int32 SpawnTry = 0; SpawnTry < 8; ++SpawnTry)
		{
			const FVector2D& AnchorPos = SpawnSpots[FMath::RandRange(0, SpawnSpots.Num() - 1)];
			const float A = FMath::FRandRange(0.f, 2.f * PI);
			const float R = FMath::FRandRange(RMin, RMax);
			const FVector2D Cand = ClampToArena(AnchorPos + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
			if (!SpawnSpotOk(Cand, MinPlayerDist, D->Enemies.IsValidIndex(Type) && D->Enemies[Type].IsFlyer())) continue;
			SpawnEnemy(Type, Cand);
			break;
		}
		// No good spot this time (every try was near a reaper / on a cliff): skip, the next one comes soon
	}
}

// ================================================================ early curve + catch-up (Nov 9 reap core, server)

float ASQHordeManager::StageReleaseMinutes(const ASQGameState* GS) const
{
	if (!GS) return 0.f;
	return FMath::Max(0.f, GS->StageTime) / FMath::Max(0.1f, GS->NightScale()) / 60.f;
}

float ASQHordeManager::SpawnCurveRate(const ASQGameState* GS, const USQData* D) const
{
	if (!GS || !D) return 0.f;
	const float Peak = FMath::Max(0.f, D->Tune(TEXT("SpawnRatePeak"), 8.f));
	if (GS->NightIndex > 0 || GS->bFinalSwarm)
	{
		// Acts 2-3: from SpawnRateActStart to the peak through the act
		return FMath::Lerp(FMath::Max(0.f, D->Tune(TEXT("SpawnRateActStart"), 6.f)), Peak, GS->NightProgress());
	}
	// Act 1: SpawnRateStart -> SpawnRateEarly over SpawnEarlyMinutes (eased: slow for the first level-ups), then linear to the peak
	const float StartRate = FMath::Max(0.f, D->Tune(TEXT("SpawnRateStart"), 0.6f));
	const float EarlyRate = FMath::Max(0.f, D->Tune(TEXT("SpawnRateEarly"), 2.f));
	const float EarlyMinutes = FMath::Max(0.01f, D->Tune(TEXT("SpawnEarlyMinutes"), 2.f));
	const float Minutes = StageReleaseMinutes(GS);
	if (Minutes < EarlyMinutes)
	{
		const float Ease = FMath::Pow(Minutes / EarlyMinutes, FMath::Clamp(D->Tune(TEXT("SpawnEarlyEaseExp"), 1.5f), 0.2f, 4.f));
		return FMath::Lerp(StartRate, EarlyRate, Ease);
	}
	const float ActMinutes = FMath::Max(1.f, GS->PhaseDuration) / FMath::Max(0.1f, GS->NightScale()) / 60.f;
	return FMath::Lerp(EarlyRate, Peak, FMath::Clamp((Minutes - EarlyMinutes) / FMath::Max(0.01f, ActMinutes - EarlyMinutes), 0.f, 1.f));
}

int32 ASQHordeManager::GetOnScreenCap() const
{
	const USQData* D = USQData::Get(this);
	const UWorld* CapWorld = GetWorld();
	const ASQGameState* GS = CapWorld ? CapWorld->GetGameState<ASQGameState>() : nullptr;
	const int32 MaxEnemies = D ? FMath::RoundToInt(D->Tune(TEXT("MaxEnemies"), 800.f)) : 800;
	if (!D || !GS) return MaxEnemies;
	const float Minutes = StageReleaseMinutes(GS);
	const float FlatMinutes = FMath::Max(0.f, D->Tune(TEXT("OnScreenCapFlatMinutes"), 1.f));
	float Cap = FMath::Max(1.f, D->Tune(TEXT("OnScreenCapStart"), 30.f)) + FMath::Max(0.f, D->Tune(TEXT("OnScreenCapPerMinute"), 25.f)) * FMath::Max(0.f, Minutes - FlatMinutes);
	Cap *= 1.f + FMath::Max(0.f, D->Tune(TEXT("PlayerSpawnRateBonus"), 0.5f)) * static_cast<float>(FMath::Max(0, GS->PlayerArray.Num() - 1));
	Cap *= (1.f + FMath::Max(0.f, GS->Difficulty)) * FMath::Max(0.f, GS->ModProduct(&FSQModifierDef::SpawnMult));
	if (GS->bFinalSwarm) return MaxEnemies;
	return FMath::Clamp(FMath::RoundToInt(FMath::Min(Cap, 1.0e6f)), 1, MaxEnemies);
}

float ASQHordeManager::EarlyHealthMult(const ASQGameState* GS, const USQData* D) const
{
	if (!GS || !D || GS->NightIndex > 0 || GS->bFinalSwarm || (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss)) return 1.f;
	const float StartMult = FMath::Clamp(D->Tune(TEXT("EarlyHealthStart"), 0.4f), 0.05f, 1.f);
	const float EaseMinutes = FMath::Max(0.01f, D->Tune(TEXT("EarlyHealthMinutes"), 6.f));
	return FMath::Lerp(StartMult, 1.f, FMath::Clamp(StageReleaseMinutes(GS) / EaseMinutes, 0.f, 1.f));
}

void ASQHordeManager::TickCatchUp(float DeltaSeconds, const TArray<ASQCharacter*>& Players)
{
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss)
	{
		CatchUpMult = 1.f;
		CatchUpKillRate = 0.f;
		CatchUpNear = 0.f;
		CatchUpKills = 0;
		return;
	}
	CatchUpTimer += DeltaSeconds;
	if (CatchUpTimer < 0.25f) return;
	const float Step = CatchUpTimer;
	CatchUpTimer = 0.f;
	const float MaxShift = FMath::Clamp(D->Tune(TEXT("CatchUpMax"), 0.25f), 0.f, 0.9f);
	// Little's law: (enemies near the crew) / (kills per second) = seconds an enemy survives once it reaches the crew.
	// Short = the crew out-damages the curve (HP drifts up); long = the horde piles up (HP drifts down). Both capped at CatchUpMax.
	const float NearRadius = FMath::Max(100.f, D->Tune(TEXT("CatchUpNearRadius"), 800.f));
	const double NearSq = static_cast<double>(NearRadius) * NearRadius;
	int32 Near = 0;
	for (const FEnemy& E : Enemies)
	{
		if (!E.bAlive || (E.EcoFlags & EcoMiniboss)) continue;
		for (const ASQCharacter* NearChar : Players)
		{
			if (NearChar && !NearChar->bDowned && FVector2D::DistSquared(NearChar->Pos2D(), E.Pos) <= NearSq) { Near++; break; }
		}
	}
	const float Window = FMath::Max(1.f, D->Tune(TEXT("CatchUpWindow"), 20.f));
	const float Blend = FMath::Clamp(Step / Window, 0.f, 1.f);
	CatchUpKillRate = FMath::Lerp(CatchUpKillRate, static_cast<float>(CatchUpKills) / Step, Blend);
	CatchUpNear = FMath::Lerp(CatchUpNear, static_cast<float>(Near), Blend);
	CatchUpKills = 0;
	if (MaxShift <= 0.f) { CatchUpMult = 1.f; return; }
	float Desired = 1.f;
	if (GS->Phase == ESQPhase::Boss) Desired = CatchUpMult;   // the boss fight holds the last value (few spawns to judge by)
	else if (CatchUpKillRate >= FMath::Max(0.01f, D->Tune(TEXT("CatchUpMinKillRate"), 0.5f)))
	{
		const float Engaged = CatchUpNear / CatchUpKillRate;
		const float Target = FMath::Max(0.1f, D->Tune(TEXT("CatchUpTargetSeconds"), 3.f));
		Desired = FMath::Pow(Target / FMath::Max(0.05f, Engaged), FMath::Clamp(D->Tune(TEXT("CatchUpExp"), 0.5f), 0.f, 2.f));
	}
	Desired = FMath::Clamp(Desired, 1.f - MaxShift, 1.f + MaxShift);
	const float MaxStep = FMath::Max(0.f, D->Tune(TEXT("CatchUpDriftPerSecond"), 0.01f)) * Step;
	CatchUpMult = FMath::Clamp(CatchUpMult + FMath::Clamp(Desired - CatchUpMult, -MaxStep, MaxStep), 1.f - MaxShift, 1.f + MaxShift);
}

void ASQHordeManager::SimTutorialHold(const TArray<ASQCharacter*>& Players, const USQData* D)
{
	if (!D || Players.Num() == 0 || D->Enemies.Num() == 0) return;
	const int32 HoldMax = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("TutorialHoldEnemies"), 6.f)), 0, 32);
	if (Enemies.Num() >= HoldMax) return;
	// One at a time, so they trickle in instead of popping up as a ring
	TutorialHoldTimer -= GetWorld()->GetDeltaSeconds();
	if (TutorialHoldTimer > 0.f) return;
	TutorialHoldTimer = 0.8f;
	// The slowest plain walker that may appear at the very start (crawler / shambler): something to soften and reap
	int32 SlowType = INDEX_NONE;
	for (int32 TypeIdx = 0; TypeIdx < D->Enemies.Num(); ++TypeIdx)
	{
		const FSQEnemyDef& Def = D->Enemies[TypeIdx];
		if (Def.MinProgress > 0.f || Def.IsFlyer() || Def.Behaviour != ESQEnemyBehaviour::Chase || Def.Weight[0] <= 0.f) continue;
		if (SlowType == INDEX_NONE || Def.Speed < D->Enemies[SlowType].Speed) SlowType = TypeIdx;
	}
	if (SlowType == INDEX_NONE) SlowType = 0;
	const ASQCharacter* Anchor = Players[0];
	if (!Anchor) return;
	const float HoldMin = FMath::Max(300.f, D->Tune(TEXT("TutorialHoldRadiusMin"), 900.f));
	const float HoldMaxR = FMath::Max(HoldMin, D->Tune(TEXT("TutorialHoldRadiusMax"), 1300.f));
	SpawnSpots.Reset();
	SpawnSpots.Add(Anchor->Pos2D());
	for (int32 SpawnTry = 0; SpawnTry < 8; ++SpawnTry)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		const FVector2D Cand = ClampToArena(Anchor->Pos2D() + FVector2D(FMath::Cos(A), FMath::Sin(A)) * FMath::FRandRange(HoldMin, HoldMaxR));
		if (!SpawnSpotOk(Cand, HoldMin * 0.8f, false)) continue;
		const int32 Added = SpawnEnemyEx(SlowType, Cand, false, 1.f, 1.f, EcoSwarm);
		if (Enemies.IsValidIndex(Added)) Enemies[Added].SlowTime = 3600.f;   // slowed for the whole lesson
		break;
	}
}

bool ASQHordeManager::SpawnSpotOk(const FVector2D& Spot, float MinPlayerDist, bool bFlyer) const
{
	const double MinSq = static_cast<double>(MinPlayerDist) * MinPlayerDist;
	for (const FVector2D& ReaperPos : SpawnSpots)
	{
		if (FVector2D::DistSquared(ReaperPos, Spot) < MinSq) return false;
	}
	// Not on a plateau cliff face (the 2D sim ignores terrain: a spawn there looks like it is inside the rock). Flyers hover over it.
	if (bFlyer) return true;
	const UWorld* SpotWorld = GetWorld();
	const ASQGameState* SpotGS = SpotWorld ? SpotWorld->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* SpotArena = SpotGS ? SpotGS->Arena.Get() : nullptr;
	if (SpotArena && SpotArena->HasTerrain() && SpotArena->GroundSlopeDeg(static_cast<float>(Spot.X), static_cast<float>(Spot.Y)) > 35.f) return false;
	return true;
}

void ASQHordeManager::SpawnEnemy(int32 Type, const FVector2D& Pos, bool bBurstSpawn)
{
	SpawnEnemyEx(Type, Pos, bBurstSpawn, 1.f, 1.f, 0);
}

int32 ASQHordeManager::SpawnEnemyEx(int32 Type, const FVector2D& Pos, bool bBurstSpawn, float HealthMult, float DamageMult, uint8 Flags)
{
	const USQData* D = USQData::Get(this);
	if (!D || !D->Enemies.IsValidIndex(Type)) return INDEX_NONE;
	// Hard cap for every source (splits, boss summons, debug): keeps the sim bounded and EnemyNet under the replication array limit
	const int32 HardCap = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MaxEnemies"), 800.f) * 1.25f), 1, 2000);
	if (Enemies.Num() >= HardCap) return INDEX_NONE;
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const FSQEnemyDef& Def = D->Enemies[Type];

	FEnemy E;
	E.Id = TakeNextId();
	E.Type = (uint8)Type;
	E.Pos = ClampToArena(Pos);
	// Balance pass (Docs/Balance.md): per-night step x per-minute ramp inside the night x co-op toughness per extra reaper
	const float HpMinutes = GS ? GS->NightProgress() * FMath::Max(1.f, GS->PhaseDuration) / 60.f : 0.f;
	const int32 HpExtraPlayers = GS ? FMath::Max(0, GS->PlayerArray.Num() - 1) : 0;
	const float HpRampMult = GS ? FMath::Max(0.f, GS->ModProduct(&FSQModifierDef::HealthRampMult)) : 1.f; // Rush Hour clause
	const float HpRamp = (1.f + FMath::Max(0.f, D->Tune(TEXT("EnemyHealthPerMinute"), 0.1f)) * HpRampMult * HpMinutes)
		* (1.f + FMath::Max(0.f, D->Tune(TEXT("EnemyHealthPerPlayer"), 0.35f)) * static_cast<float>(HpExtraPlayers));
	// HealthPerNight was tuned for BalanceNightSeconds nights: shorter (test) nights level reapers less, so the step shrinks with them
	const float HpNightScale = FMath::Clamp(D->Tune(TEXT("NightSeconds"), 360.f) / FMath::Max(1.f, D->Tune(TEXT("BalanceNightSeconds"), 360.f)), 0.1f, 4.f);
	E.Health = (Def.Health + Def.HealthPerNight * HpNightScale * static_cast<float>(GS ? GS->NightIndex : 0)) * HpRamp;
	// Economy pass: chest relics (EconomyEnemyHealth<N>), Difficulty, and the caller's multiplier (miniboss / Final Swarm tier)
	if (GS)
	{
		E.Health *= GS->EconomyHealthMult(false) * (1.f + FMath::Max(0.f, D->Tune(TEXT("DifficultyHealthShare"), 0.5f)) * FMath::Max(0.f, GS->Difficulty));
	}
	E.Health *= FMath::Clamp(HealthMult, 0.01f, 1000.f);
	// Early-curve pass: act 1 starts soft (one scythe hit until the first level-ups) and the catch-up drifts HP toward the crew's damage
	E.Health *= EarlyHealthMult(GS, D);
	if (!(Flags & EcoMiniboss)) E.Health *= CatchUpMult;
	E.DamageMult = FMath::Clamp(DamageMult, 0.f, 100.f);
	E.EcoFlags = Flags;
	// Crowned elites (Megabonk): more HP, a bigger gold pile and a free-chest chance. Never on swarm / miniboss spawns.
	if (Def.Behaviour == ESQEnemyBehaviour::Elite && !(Flags & (EcoSwarm | EcoMiniboss)))
	{
		const bool bCrownAll = GS && GS->ModAny(&FSQModifierDef::bCrownAll);
		if (bCrownAll || FMath::FRand() < D->Tune(TEXT("CrownedChance"), 0.3f))
		{
			E.EcoFlags |= EcoCrowned;
			E.Health *= FMath::Max(0.1f, D->Tune(TEXT("CrownedHealthMult"), 1.5f));
		}
	}
	E.Health = FMath::Min(E.Health, 1.0e9f);
	E.MaxHealth = FMath::Max(1.f, E.Health);
	E.bBurstSpawn = bBurstSpawn;
	E.PhaseCd = FMath::FRandRange(1.f, 3.f); // Wraiths don't all blink in step
	E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Spawning);
	return Enemies.Add(E);
}

void ASQHordeManager::SpawnEnemyNear(FName EnemyId, const FVector2D& Center, int32 Count, float Radius)
{
	const USQData* D = USQData::Get(this);
	const int32 Type = D ? D->FindEnemyIndex(EnemyId) : INDEX_NONE;
	if (Type == INDEX_NONE) return;
	// Boss summons: keep a small gap from every reaper (no summons straight on top of someone)
	SpawnSpots.Reset();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It) SpawnSpots.Add(It->Pos2D());
	const float NearGap = D->Tune(TEXT("SummonMinPlayerDistance"), 350.f);
	const bool bSummonFlyer = D->Enemies.IsValidIndex(Type) && D->Enemies[Type].IsFlyer();
	for (int32 i = 0; i < Count; ++i)
	{
		FVector2D Spot = ClampToArena(Center + FMath::RandPointInCircle(Radius));
		for (int32 SpawnTry = 0; SpawnTry < 6 && !SpawnSpotOk(Spot, NearGap, bSummonFlyer); ++SpawnTry)
		{
			Spot = ClampToArena(Center + FMath::RandPointInCircle(FMath::Max(Radius, NearGap * 2.f)));
		}
		SpawnEnemy(Type, Spot);
	}
}

uint16 ASQHordeManager::TakeNextId()
{
	if (NextId == 0) NextId = 1;
	return NextId++;
}

float ASQHordeManager::GetWidowBest()
{
	// Widow passive lookup: once per frame (it used to iterate every character on every slowed kill)
	if (WidowBestFrame == GFrameCounter) return WidowBestCache;
	WidowBestFrame = GFrameCounter;
	WidowBestCache = 0.f;
	static const FName SlowSoulsId(TEXT("SlowSouls"));
	for (TActorIterator<ASQCharacter> CrewIt(GetWorld()); CrewIt; ++CrewIt)
	{
		if (CrewIt->GetPassiveId() == SlowSoulsId) WidowBestCache = FMath::Max(WidowBestCache, CrewIt->GetPassiveValue());
	}
	return WidowBestCache;
}

void ASQHordeManager::DebugSpawn(int32 Count)
{
	TArray<ASQCharacter*> Players;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It) Players.Add(*It);
	const FVector2D Center = Players.Num() > 0 ? Players[0]->Pos2D() : FVector2D::ZeroVector;
	for (int32 i = 0; i < Count; ++i)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		SpawnEnemy(PickEnemyType(1, 1.f), Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * FMath::FRandRange(800.f, 2500.f));
	}
}

/** Remake balance pass: enemy hits ease from one act start to the next like the horde's HP (ASQGameState::StageHealthSmoothing),
 *  instead of jumping +EnemyDamagePerNight at every act change. Returns the multiplier on the act formula (1 at act starts
 *  and at the stage end). Tuning StageDamageSmoothing 0 switches it off. */
static float SQHordeStageDamageSmoothing(const ASQGameState* GS, const USQData* D)
{
	if (!GS || !D || D->Tune(TEXT("StageDamageSmoothing"), 1.f) < 0.5f) return 1.f;
	if ((GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) || GS->StageDuration <= 0.f) return 1.f;
	const float StepRatio = FMath::Max(0.f, D->Tune(TEXT("EnemyDamagePerNight"), 0.3f));
	const float StepPerMinute = FMath::Max(0.f, D->Tune(TEXT("EnemyDamagePerMinute"), 0.03f)) * FMath::Max(0.f, GS->ModProduct(&FSQModifierDef::HealthRampMult));
	const float StepActMinutes = FMath::Max(1.f, GS->PhaseDuration) / 60.f;
	const int32 StepAct = FMath::Clamp(GS->NightIndex, 0, 2);
	const float StepInto = FMath::Clamp(GS->NightProgress(), 0.f, 1.f);
	const float DamageNow = (1.f + StepRatio * static_cast<float>(StepAct)) * (1.f + StepPerMinute * StepInto * StepActMinutes);
	const float FromAnchor = 1.f + StepRatio * static_cast<float>(StepAct);
	const float ToAnchor = StepAct >= 2 ? (1.f + StepRatio * 2.f) * (1.f + StepPerMinute * StepActMinutes) : 1.f + StepRatio * static_cast<float>(StepAct + 1);
	const float Target = FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(0.01f, FromAnchor)), FMath::Loge(FMath::Max(0.01f, ToAnchor)), StepInto));
	return FMath::Clamp(Target / FMath::Max(0.01f, DamageNow), 0.5f, 2.f);
}

void ASQHordeManager::SimEnemies(float DeltaSeconds, const TArray<ASQCharacter*>& Players)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;

	const bool bHasLantern = GS->Lantern != nullptr;
	const FVector2D LanternPos = bHasLantern ? FVector2D(GS->Lantern->GetActorLocation().X, GS->Lantern->GetActorLocation().Y) : FVector2D::ZeroVector;
	const float LanternRadius = bHasLantern ? GS->Lantern->Radius : 350.f;
	const float Blend = FMath::Min(1.f, DeltaSeconds * 6.f);
	const float DriftBlend = FMath::Min(1.f, DeltaSeconds * 1.8f);
	const ASQArena* Arena = GS->Arena.Get();
	// Telegraph tuning: how far past its reach a wind-up still connects, and the charger's dash
	const float WindupSlack = D->Tune(TEXT("EnemyWindupSlack"), 40.f);
	const float ChargeRange = D->Tune(TEXT("EnemyChargeRange"), 750.f);
	const float ChargeSpeedMult = D->Tune(TEXT("EnemyChargeSpeedMult"), 3.f);
	const float ChargeTime = D->Tune(TEXT("EnemyChargeTime"), 0.6f);
	const float RecoverTime = D->Tune(TEXT("EnemyChargeRecover"), 0.8f);
	const float BomberRadius = D->Tune(TEXT("BomberRadius"), 240.f);
	// Balance pass (Docs/Balance.md): enemy hits grow per night and per minute into the night
	const float DmgMinutes = GS->NightProgress() * FMath::Max(1.f, GS->PhaseDuration) / 60.f;
	// Economy pass: Glass Reaper (EnemyDamageMult) and Rush Hour (HealthRampMult also speeds the damage ramp) clauses
	const float DmgScale = (1.f + FMath::Max(0.f, D->Tune(TEXT("EnemyDamagePerNight"), 0.3f)) * static_cast<float>(GS->NightIndex))
		* (1.f + FMath::Max(0.f, D->Tune(TEXT("EnemyDamagePerMinute"), 0.03f)) * FMath::Max(0.f, GS->ModProduct(&FSQModifierDef::HealthRampMult)) * DmgMinutes)
		* FMath::Max(0.f, GS->ModProduct(&FSQModifierDef::EnemyDamageMult))
		* SQHordeStageDamageSmoothing(GS, D);
	// Flyers (Gravebat swoops, Wraith phase blinks)
	const float PhaseInterval = FMath::Max(0.5f, D->Tune(TEXT("WraithPhaseInterval"), 3.5f));
	const float SwoopCooldown = FMath::Max(0.5f, D->Tune(TEXT("GravebatSwoopCooldown"), 2.8f));
	// Top-down maps (Nov 9): walkers steer round layout walls / blocking props this far ahead and never pass through them
	const float WallLook = FMath::Max(0.f, D->Tune(TEXT("HordeWallLook"), 260.f));

	// Body blocking: the horde crowds around reapers instead of overlapping them (each reaper is a solid circle;
	// enemies shorter than a jumping reaper's feet pass underneath). Mirrors ASQCharacter::ApplyBodyBlock.
	const bool bBodyBlock = D->Tune(TEXT("BodyBlockEnabled"), 1.f) > 0.5f;
	// Collision pass: a reaper's body circle is its capsule (ReaperBodyRadius), an enemy's is EnemyRadius (CSV blended to its art)
	TArray<float, TInlineAllocator<8>> PlayerRadii;
	for (const ASQCharacter* RadiusChar : Players) PlayerRadii.Add(ReaperBodyRadius(RadiusChar, D));
	const float DefaultReaperR = ReaperBodyRadius(nullptr, D);
	struct FReaperBody
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float FeetAbove = 0.f;
		float Radius = 40.f;
	};
	TArray<FReaperBody, TInlineAllocator<8>> ReaperBodies;
	if (bBodyBlock)
	{
		for (int32 BodyIdx = 0; BodyIdx < Players.Num(); ++BodyIdx)
		{
			const ASQCharacter* BodyChar = Players[BodyIdx];
			if (!BodyChar || BodyChar->bDowned || BodyChar->CarriedBy || BodyChar->IsDashPassing()) continue;
			FReaperBody Body;
			Body.Pos = BodyChar->Pos2D();
			Body.FeetAbove = BodyChar->GetFeetAboveGround();
			Body.Radius = PlayerRadii[BodyIdx];
			ReaperBodies.Add(Body);
		}
	}

	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		FEnemy& E = Enemies[i];
		if (!E.bAlive || !D->Enemies.IsValidIndex(E.Type)) continue;
		const FSQEnemyDef& Def = D->Enemies[E.Type];
		E.AttackCd -= DeltaSeconds;
		E.SlowTime -= DeltaSeconds;
		E.MarkTime -= DeltaSeconds;
		if (E.PrimeTime > 0.f)
		{
			E.PrimeTime -= DeltaSeconds;
			if (E.PrimeTime <= 0.f) E.PrimeDamage = 0.f;
		}
		E.Age += DeltaSeconds;
		// ApplyKnockback (slide): a short shove that decays; steering takes over again once it fades
		if (!E.Knock.IsNearlyZero(5.f))
		{
			E.Pos = ClampToArena(E.Pos + E.Knock * DeltaSeconds);
			E.Knock *= FMath::Max(0.f, 1.f - 9.f * DeltaSeconds);
		}
		else
		{
			E.Knock = FVector2D::ZeroVector;
		}
		if (E.FrozenTime > 0.f)
		{
			// Stopwatch passive: stopped in time (no movement, no attacks; a wind-up in progress is cancelled)
			E.FrozenTime -= DeltaSeconds;
			E.Vel = FVector2D::ZeroVector;
			E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Frozen);
			E.bChargeWindup = false;
			E.bPhaseWindup = false;
			continue;
		}
		if (E.AnimState == static_cast<uint8>(ESQEnemyAnimState::Frozen)) E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Move);
		// Entering the world (rising / dropping / bursting in): inert, so the spawn animation is honest
		if (E.AnimState == static_cast<uint8>(ESQEnemyAnimState::Spawning))
		{
			if (E.Age < SQEnemySpawnTime(E.bBurstSpawn ? ESQEnemySpawn::Burst : Def.Spawn)) continue;
			E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Move);
		}
		const ESQEnemyAnimState State = static_cast<ESQEnemyAnimState>(E.AnimState);
		if (State != ESQEnemyAnimState::Move) E.StateTime -= DeltaSeconds;
		const bool bFlyer = Def.IsFlyer();
		const bool bSwoop = Def.Fly == ESQEnemyFly::Swoop;
		const bool bPhaser = Def.Fly == ESQEnemyFly::Phase;
		E.PhaseCd -= DeltaSeconds;

		// Nearest standing reaper
		ASQCharacter* Nearest = nullptr;
		float NearestDistSq = TNumericLimits<float>::Max();
		float NearestR = DefaultReaperR;
		ASQCharacter* Richest = nullptr;
		for (int32 PlayerIdx = 0; PlayerIdx < Players.Num(); ++PlayerIdx)
		{
			ASQCharacter* C = Players[PlayerIdx];
			if (!C || C->bDowned) continue;
			const float DSq = FVector2D::DistSquared(C->Pos2D(), E.Pos);
			if (DSq < NearestDistSq) { NearestDistSq = DSq; Nearest = C; NearestR = PlayerRadii[PlayerIdx]; }
			if (C->Carried > 0 && (!Richest || C->Carried > Richest->Carried)) Richest = C;
		}
		// Collision pass: the body is held at MyRadius + reaper radius from a reaper's centre (body blocking), so a hit lands
		// when the two circles touch plus this type's weapon reach (Enemies.csv Reach; was a flat Radius + 48 for every type)
		const float MyRadius = EnemyRadius(E.Type);
		const float Reach = MyRadius + NearestR + (TypeReach.IsValidIndex(E.Type) ? TypeReach[E.Type] : Def.Reach);
		const float Phase = IdHash01(E.Id, 5u) * 2.f * PI;

		// --- Charger dash: fixed direction, fast, hits the first reaper it meets, then dizzy ---
		if (State == ESQEnemyAnimState::Charge)
		{
			E.Vel = E.ChargeDir * Def.Speed * ChargeSpeedMult;
			E.Pos = ClampToArena(E.Pos + E.Vel * DeltaSeconds);
			if (Arena && !bFlyer) Arena->ResolveWallOverlap(E.Pos, MyRadius);
			bool bHitSomeone = false;
			for (ASQCharacter* C : Players)
			{
				if (!C || C->bDowned || FVector2D::DistSquared(C->Pos2D(), E.Pos) > FMath::Square(Reach + 20.f)) continue;
				C->ApplyHit(Def.Damage * DmgScale * E.DamageMult);
				if (C->Stats.Thorns > 0.f) DamageEnemy(i, C->Stats.Thorns, C, 0.f);
				bHitSomeone = true;
				break;
			}
			if (!E.bAlive) continue;
			if (bHitSomeone || E.StateTime <= 0.f)
			{
				// Gravebat: the dive ends and it climbs back up (Recover = climb on clients)
				E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Recover);
				E.StateTime = RecoverTime;
				E.AttackCd = bSwoop ? SwoopCooldown * FMath::FRandRange(0.8f, 1.2f) : 2.5f;
			}
			continue;
		}
		if ((State == ESQEnemyAnimState::Strike || State == ESQEnemyAnimState::Recover) && E.StateTime <= 0.f)
		{
			E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Move);
		}
		const bool bRecovering = State == ESQEnemyAnimState::Recover && E.StateTime > 0.f;
		const bool bWindingUp = State == ESQEnemyAnimState::Windup;

		FVector2D Target = LanternPos;
		bool bFlee = false;
		switch (Def.Behaviour)
		{
		case ESQEnemyBehaviour::Digger:
			Target = LanternPos;
			break;
		case ESQEnemyBehaviour::Spitter:
			if (Nearest)
			{
				Target = Nearest->Pos2D();
				// Keep at range: back off when too close, hold position in the band
				const float Range = Def.Range > 0.f ? Def.Range : 900.f;
				const float DistNow = FMath::Sqrt(NearestDistSq);
				if (DistNow < Range * 0.8f) bFlee = true;
				else if (DistNow < Range) Target = E.Pos;
			}
			break;
		case ESQEnemyBehaviour::Wisp:
			if (E.Stolen > 0 && Nearest) { Target = Nearest->Pos2D(); bFlee = true; }
			else if (Richest) Target = Richest->Pos2D();
			else if (Nearest) Target = Nearest->Pos2D();
			break;
		default:
			if (Nearest) Target = Nearest->Pos2D();
			break;
		}

		// Gravebat climbing back after a dive: pulls away from the reaper while it regains height
		if (bSwoop && bRecovering && Nearest) { Target = Nearest->Pos2D(); bFlee = true; }

		FVector2D To = Target - E.Pos;
		const float Dist = To.Size();
		FVector2D Dir = Dist > 1.f ? To / Dist : FVector2D::ZeroVector;
		if (bFlee) Dir = -Dir;
		// Gravebat swarm: circles the reaper inside swoop range while its swoop recharges
		if (bSwoop && !bFlee && E.AttackCd > 0.f)
		{
			const float SwoopRange = Def.Range > 0.f ? Def.Range : ChargeRange;
			if (Dist < SwoopRange * 0.85f)
			{
				const FVector2D Tangent = (E.Id & 1) ? FVector2D(-Dir.Y, Dir.X) : FVector2D(Dir.Y, -Dir.X);
				Dir = (Tangent * 0.9f + Dir * (Dist < SwoopRange * 0.5f ? -0.4f : 0.15f)).GetSafeNormal();
			}
		}

		// Movement personality (cheap: one sin per enemy)
		float GaitSpeed = 1.f;
		switch (Def.Gait)
		{
		case ESQEnemyGait::Lurch:
			GaitSpeed = 0.55f + 1.2f * FMath::Square(FMath::Max(0.f, FMath::Sin(SimClock * 3.2f + Phase)));
			break;
		case ESQEnemyGait::Scuttle:
			GaitSpeed = FMath::Frac(SimClock * 1.8f + Phase) < 0.65f ? 1.35f : 0.15f;
			break;
		case ESQEnemyGait::ZigZag:
			Dir = Dir.GetRotated(50.f * FMath::Sin(SimClock * 5.f + Phase));
			break;
		case ESQEnemyGait::Circle:
			if (!bFlee && Dist > Reach * 1.5f && Dist < 560.f && E.AttackCd > 0.f)
			{
				// orbit the reaper (direction by Id), drifting slowly inward, until the next lunge
				const FVector2D Tangent = (E.Id & 1) ? FVector2D(-Dir.Y, Dir.X) : FVector2D(Dir.Y, -Dir.X);
				Dir = (Tangent * 0.9f + Dir * 0.3f).GetSafeNormal();
			}
			else if (Dist < 560.f)
			{
				GaitSpeed = 1.3f; // lunge
			}
			break;
		default:
			break;
		}

		// Separation from neighbours (keeps the horde a crowd, not a blob). Drifting ghosts pass through each other.
		FVector2D Sep = FVector2D::ZeroVector;
		if (Def.Gait != ESQEnemyGait::Drift)
		{
			const FIntPoint C0 = Cell(E.Pos);
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					const TArray<int32>* Bucket = Grid.Find(FIntPoint(C0.X + dx, C0.Y + dy));
					if (!Bucket) continue;
					for (int32 j : *Bucket)
					{
						if (j == i || !Enemies.IsValidIndex(j) || !D->Enemies.IsValidIndex(Enemies[j].Type)) continue;
						const FEnemy& O = Enemies[j];
						if (D->Enemies[O.Type].IsFlyer() != bFlyer) continue; // flyers and walkers don't push each other
						const FVector2D Delta = E.Pos - O.Pos;
						const float DSq = Delta.SizeSquared();
						const float Min = (MyRadius + EnemyRadius(O.Type)) * 0.9f;
						if (DSq > 0.01f && DSq < Min * Min)
						{
							const float DD = FMath::Sqrt(DSq);
							Sep += Delta / DD * ((Min - DD) / Min);
						}
					}
				}
			}
		}

		const float ZoneMult = Arena ? Arena->EnemySpeedMult(E.Pos) : 1.f;
		// Flyers are above the tide / ice / mud: only a full time stop (0) holds them
		const float MoveZone = bFlyer ? (ZoneMult <= 0.f ? 0.f : 1.f) : ZoneMult;
		const float Speed = Def.Speed * GaitSpeed * (E.SlowTime > 0.f ? 0.5f : 1.f) * MoveZone;
		// Planted while winding up or dizzy (that is the dodge window); still pushed apart by the crowd.
		// Gravebats keep flapping away while they climb; a Wraith drifts slowly while it phases out.
		float MoveScale = (bWindingUp || bRecovering) ? 0.f : 1.f;
		if (bSwoop && bRecovering) MoveScale = 0.6f;
		if (bPhaser && bWindingUp) MoveScale = 0.25f;
		if (Arena && !bFlyer && WallLook > 0.f && MoveScale > 0.f) Dir = Arena->SteerAroundWalls(E.Pos, Dir, MyRadius, WallLook);
		const FVector2D Desired = Dir * Speed * MoveScale + Sep * Def.Speed * MoveZone * 1.5f;
		E.Vel = FMath::Lerp(E.Vel, Desired, Def.Gait == ESQEnemyGait::Drift ? DriftBlend : Blend);
		E.Pos = ClampToArena(E.Pos + E.Vel * DeltaSeconds);

		// Body blocking: pushed out of every reaper it walked into (they crowd and pin; jumpers pass over)
		if (ReaperBodies.Num() > 0 && TypeBlocks(Def))
		{
			const float EnemyTop = TypeBlockHeight(Def);
			for (const FReaperBody& Body : ReaperBodies)
			{
				if (Body.FeetAbove > EnemyTop) continue;
				const float MinDist = MyRadius + Body.Radius;
				const FVector2D Away = E.Pos - Body.Pos;
				const double AwaySq = Away.SizeSquared();
				if (AwaySq >= static_cast<double>(MinDist) * MinDist) continue;
				const float AwayLen = static_cast<float>(FMath::Sqrt(AwaySq));
				const FVector2D OutDir = AwayLen > 0.5f ? Away / AwayLen : FVector2D(FMath::Cos(static_cast<float>(E.Id)), FMath::Sin(static_cast<float>(E.Id)));
				E.Pos = ClampToArena(Body.Pos + OutDir * MinDist);
				const float IntoReaper = -static_cast<float>(FVector2D::DotProduct(E.Vel, OutDir));
				if (IntoReaper > 0.f) E.Vel += OutDir * IntoReaper;
			}
		}

		// Layout walls / blocking props (top-down maps): walkers are pushed back out (flyers pass over)
		if (Arena && !bFlyer) Arena->ResolveWallOverlap(E.Pos, MyRadius);

		// Biome hazards (the tide drowns the horde; flyers are above it)
		if (Arena && !bFlyer)
		{
			const float Dps = Arena->EnemyDamagePerSecond(E.Pos);
			if (Dps > 0.f)
			{
				DamageEnemy(i, Dps * DeltaSeconds, nullptr, 0.f, false); // no hit flash for hazard ticks
				if (!E.bAlive) continue;
			}
		}
		if (ZoneMult <= 0.f) continue; // frozen in time: no attacks either

		// --- A wind-up finishing: the telegraphed attack lands now ---
		if (bWindingUp)
		{
			if (E.StateTime > 0.f) continue;
			E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Strike);
			E.StateTime = 0.3f;
			if (E.bPhaseWindup)
			{
				// Wraith: faded out completely - blink closer to its reaper, then fade back in (Strike = phase-in on clients)
				E.bPhaseWindup = false;
				E.StateTime = 0.35f;
				E.PhaseCd = PhaseInterval * FMath::FRandRange(0.8f, 1.25f);
				if (Nearest)
				{
					const FVector2D ToReaper = Nearest->Pos2D() - E.Pos;
					const float ReaperDist = static_cast<float>(ToReaper.Size());
					const float Hop = FMath::Min(Def.Range > 0.f ? Def.Range : 400.f, FMath::Max(0.f, ReaperDist - Reach * 1.6f));
					if (ReaperDist > 1.f && Hop > 0.f) E.Pos = ClampToArena(E.Pos + ToReaper / ReaperDist * Hop);
				}
				continue;
			}
			if (E.bChargeWindup)
			{
				E.bChargeWindup = false;
				E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Charge);
				E.StateTime = ChargeTime;
				continue;
			}
			if (Def.Behaviour == ESQEnemyBehaviour::Bomber)
			{
				// The fuse ran out: blast everyone in the marked circle (stepping out of it is the counterplay)
				for (ASQCharacter* C : Players)
				{
					if (C && !C->bDowned && FVector2D::Distance(C->Pos2D(), E.Pos) <= BomberRadius) C->ApplyHit(Def.Damage * DmgScale * E.DamageMult);
				}
				if (Nearest) Nearest->MulticastFX(0, FVector(E.Pos.X, E.Pos.Y, 5.f), BomberRadius, FColor(255, 140, 40), 0.3f);
				KillEnemy(i, nullptr);
				continue;
			}
			if (Def.Behaviour == ESQEnemyBehaviour::Spitter)
			{
				E.AttackCd = 2.5f;
				if (Nearest)
				{
					const FVector2D ShotDir = (Nearest->Pos2D() - E.Pos).GetSafeNormal();
					SpawnProjectile(E.Pos + ShotDir * (MyRadius + 20.f), ShotDir * 700.f, Def.Damage * DmgScale * E.DamageMult, 30.f, 3.f, 1, true, nullptr);
				}
				continue;
			}
			E.AttackCd = 0.8f;
			if (Nearest && NearestDistSq <= FMath::Square(Reach + WindupSlack))
			{
				Nearest->ApplyHit(Def.Damage * DmgScale * E.DamageMult);
				if (Nearest->Stats.Thorns > 0.f) DamageEnemy(i, Nearest->Stats.Thorns, Nearest, 0.f);
			}
			continue;
		}
		if (bRecovering) continue;

		// Spitters lob hostile projectiles from range (after a short cast telegraph)
		if (Def.Behaviour == ESQEnemyBehaviour::Spitter)
		{
			if (Nearest && E.AttackCd <= 0.f && NearestDistSq <= FMath::Square((Def.Range > 0.f ? Def.Range : 900.f) * 1.3f))
			{
				if (Def.Windup > 0.f)
				{
					E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Windup);
					E.StateTime = Def.Windup;
				}
				else
				{
					E.AttackCd = 2.5f;
					const FVector2D ShotDir = (Nearest->Pos2D() - E.Pos).GetSafeNormal();
					SpawnProjectile(E.Pos + ShotDir * (MyRadius + 20.f), ShotDir * 700.f, Def.Damage * DmgScale * E.DamageMult, 30.f, 3.f, 1, true, nullptr);
				}
			}
			continue;
		}

		// Gravediggers rob the lantern
		if (Def.Behaviour == ESQEnemyBehaviour::Digger)
		{
			if (bHasLantern && FVector2D::Distance(E.Pos, LanternPos) < LanternRadius * 0.6f)
			{
				const int32 Steal = FMath::RoundToInt(D->Tune(TEXT("DiggerSteal"), 15.f));
				if (GS->Phase == ESQPhase::Night && GS->Banked > 0)
				{
					GS->Banked = FMath::Max(0, GS->Banked - Steal);
					GS->PushAlert(FString::Printf(TEXT("A Gravedigger stole %d banked souls! Guard the lantern!"), Steal));
				}
				E.bAlive = false;
			}
			continue;
		}

		// Wraith: every few seconds it phases out (Windup) and blinks closer; it never telegraphs its touch
		if (bPhaser && Nearest && E.PhaseCd <= 0.f && NearestDistSq > FMath::Square(Reach * 2.5f) && NearestDistSq < FMath::Square(2200.f))
		{
			E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Windup);
			E.StateTime = FMath::Max(0.2f, Def.Windup);
			E.bPhaseWindup = true;
			E.bChargeWindup = false;
			continue;
		}

		// Chargers: rear up (telegraph) when a reaper is in charge range, then dash at where they stood.
		// Gravebats use the same machine: rise + red marker (Windup), dive (Charge), climb back (Recover).
		const float ChargeStartRange = bSwoop ? (Def.Range > 0.f ? Def.Range : ChargeRange) : ChargeRange;
		if ((Def.Gait == ESQEnemyGait::Charge || bSwoop) && Nearest && E.AttackCd <= 0.f
			&& (bSwoop || NearestDistSq > FMath::Square(Reach)) && NearestDistSq <= FMath::Square(ChargeStartRange))
		{
			E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Windup);
			E.StateTime = FMath::Max(0.3f, Def.Windup);
			E.bChargeWindup = true;
			E.ChargeDir = (Nearest->Pos2D() - E.Pos).GetSafeNormal();
			continue;
		}

		// Gravebats only hurt by diving
		if (bSwoop) continue;

		// Contact with the nearest reaper
		if (Nearest && E.AttackCd <= 0.f && NearestDistSq <= FMath::Square(Reach))
		{
			if (Def.Behaviour == ESQEnemyBehaviour::Wisp)
			{
				E.AttackCd = 0.8f;
				if (E.Stolen == 0 && Nearest->Carried > 0)
				{
					const int32 Steal = FMath::Min(FMath::Max(1, FMath::RoundToInt(Def.Damage)), Nearest->Carried);
					Nearest->LoseCarried(Steal, false);
					E.Stolen += Steal;
				}
				continue;
			}
			if (Def.Windup > 0.f && !bPhaser)
			{
				// Telegraph first: the hit lands when the wind-up ends, if the reaper is still in reach
				E.AnimState = static_cast<uint8>(ESQEnemyAnimState::Windup);
				E.StateTime = Def.Windup;
				E.bChargeWindup = false;
				continue;
			}
			E.AttackCd = 0.8f;
			if (Def.Behaviour == ESQEnemyBehaviour::Bomber)
			{
				// Explodes on contact, hurting every reaper nearby
				for (ASQCharacter* C : Players)
				{
					if (C && !C->bDowned && FVector2D::Distance(C->Pos2D(), E.Pos) <= BomberRadius) C->ApplyHit(Def.Damage * DmgScale * E.DamageMult);
				}
				Nearest->MulticastFX(0, FVector(E.Pos.X, E.Pos.Y, 5.f), BomberRadius, FColor(255, 140, 40), 0.3f);
				KillEnemy(i, nullptr);
				continue;
			}
			Nearest->ApplyHit(Def.Damage * DmgScale * E.DamageMult);
			if (Nearest->Stats.Thorns > 0.f)
			{
				DamageEnemy(i, Nearest->Stats.Thorns, Nearest, 0.f);
			}
		}
	}
}

void ASQHordeManager::SimPickups(float DeltaSeconds, const TArray<ASQCharacter*>& Players)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;

	const float Lifetime = D->Tune(TEXT("SoulLifetime"), 90.f);
	const float GoldLifetime = FMath::Max(1.f, D->Tune(TEXT("GoldLifetime"), 60.f));
	const float PickupRadius = D->Tune(TEXT("PickupRadius"), 90.f);
	const float PullSpeed = D->Tune(TEXT("PullSpeed"), 1300.f);

	// Soul pull range per player, worked out once per frame (Femur's Fetch passive widens it)
	TArray<float, TInlineAllocator<8>> SoulRanges;
	for (ASQCharacter* C : Players) SoulRanges.Add(C ? C->GetSoulMagnet() : 0.f);

	// Ghost shift (Nov 23 2026): a downed reaper's spirit still gathers XP and gold for the crew (never souls)
	const bool bGhostCollect = D->Tune(TEXT("GhostCollect"), 1.f) > 0.5f;
	for (FPickup& P : Pickups)
	{
		if (!P.bAlive) continue;
		P.Age += DeltaSeconds;
		if (P.Kind == 1 && P.Age > Lifetime) { P.bAlive = false; continue; }
		if (P.Kind == 2 && P.Age > GoldLifetime) { P.bAlive = false; continue; }

		ASQCharacter* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (int32 PlayerIdx = 0; PlayerIdx < Players.Num(); ++PlayerIdx)
		{
			ASQCharacter* C = Players[PlayerIdx];
			if (!C) continue;
			if (C->bDowned && (P.Kind == 1 || !bGhostCollect || C->CarriedBy)) continue;
			const float Range = P.Kind == 1 ? SoulRanges[PlayerIdx] : C->Stats.Magnet; // XP and gold use the normal magnet
			const float Dist = FVector2D::Distance(C->Pos2D(), P.Pos);
			if (Dist > Range || Dist >= BestDist) continue;
			if (P.Kind == 1 && C->Carried >= C->Stats.CarryCap) continue;
			Best = C;
			BestDist = Dist;
		}
		if (!Best) continue;

		if (BestDist <= PickupRadius)
		{
			if (P.Kind == 0)
			{
				GS->AddTeamXP(P.Value);
				P.bAlive = false;
			}
			else if (P.Kind == 2)
			{
				GiveGold(Best, static_cast<float>(P.Value));
				P.bAlive = false;
			}
			else
			{
				const int32 Took = Best->AddCarried(P.Value);
				P.Value -= Took;
				if (P.Value <= 0) P.bAlive = false;
			}
		}
		else
		{
			const FVector2D Dir = (Best->Pos2D() - P.Pos).GetSafeNormal();
			P.Pos += Dir * FMath::Min(PullSpeed * DeltaSeconds, BestDist);
		}
	}
}

void ASQHordeManager::SimProjectiles(float DeltaSeconds, const TArray<ASQCharacter*>& Players)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;
	ASQBoss* Boss = GS->Boss;

	for (FProjectile& Pr : Projectiles)
	{
		if (!Pr.bAlive) continue;
		Pr.Life -= DeltaSeconds;
		Pr.Pos += Pr.Vel * DeltaSeconds;
		if (Pr.Life <= 0.f || FMath::Abs(Pr.Pos.X) > ArenaHalf || FMath::Abs(Pr.Pos.Y) > ArenaHalf)
		{
			Pr.bAlive = false;
			continue;
		}

		if (Pr.bHostile)
		{
			for (ASQCharacter* C : Players)
			{
				if (C && !C->bDowned && FVector2D::Distance(C->Pos2D(), Pr.Pos) < Pr.Radius + ReaperBodyRadius(C, D))
				{
					C->ApplyHit(Pr.Damage);
					Pr.bAlive = false;
					break;
				}
			}
			continue;
		}

		ASQCharacter* Shooter = Pr.Owner.Get();
		const FIntPoint ProjMinC = Cell(Pr.Pos - FVector2D(Pr.Radius + MaxTypeRadius));
		const FIntPoint ProjMaxC = Cell(Pr.Pos + FVector2D(Pr.Radius + MaxTypeRadius));
		for (int32 CellX = ProjMinC.X; CellX <= ProjMaxC.X && Pr.bAlive; ++CellX)
		{
			for (int32 CellY = ProjMinC.Y; CellY <= ProjMaxC.Y && Pr.bAlive; ++CellY)
			{
				const TArray<int32>* Bucket = Grid.Find(FIntPoint(CellX, CellY));
				if (!Bucket) continue;
				for (int32 j : *Bucket)
				{
					if (!Enemies.IsValidIndex(j)) continue;
					FEnemy& E = Enemies[j];
					if (!E.bAlive || Pr.HitIds.Contains(E.Id) || !D->Enemies.IsValidIndex(E.Type)) continue;
					const float R = Pr.Radius + EnemyRadius(E.Type);
					if (FVector2D::DistSquared(E.Pos, Pr.Pos) <= R * R)
					{
						Pr.HitIds.Add(E.Id);
						DamageEnemy(j, Pr.Damage, Shooter, 0.f);
						if (--Pr.Pierce <= 0) { Pr.bAlive = false; break; }
					}
				}
			}
		}

		if (Pr.bAlive && Boss && !Pr.bHitBoss)
		{
			const FVector BL = Boss->GetActorLocation();
			if (FVector2D::Distance(FVector2D(BL.X, BL.Y), Pr.Pos) <= Pr.Radius + Boss->HitRadius())
			{
				Pr.bHitBoss = true;
				Boss->TakeBossDamage(Pr.Damage, Shooter);
				if (--Pr.Pierce <= 0) Pr.bAlive = false;
			}
		}
	}
}

// ================================================================ server API

int32 ASQHordeManager::ApplyAreaDamage(const FVector2D& Center, float Radius, float Damage, ASQCharacter* By, float Slow)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D) return 0;

	int32 Hits = 0;
	const float MaxEnemyRadius = MaxTypeRadius; // the biggest body circle (cells far enough out to catch its edge)
	const FIntPoint MinC = Cell(Center - FVector2D(Radius + MaxEnemyRadius));
	const FIntPoint MaxC = Cell(Center + FVector2D(Radius + MaxEnemyRadius));
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive || !D->Enemies.IsValidIndex(Enemies[j].Type)) continue;
				const float R = Radius + EnemyRadius(Enemies[j].Type);
				if (FVector2D::DistSquared(Enemies[j].Pos, Center) <= R * R)
				{
					DamageEnemy(j, Damage, By, Slow);
					Hits++;
				}
			}
		}
	}

	if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		if (ASQBoss* Boss = GS->Boss)
		{
			const FVector BL = Boss->GetActorLocation();
			if (FVector2D::Distance(FVector2D(BL.X, BL.Y), Center) <= Radius + Boss->HitRadius())
			{
				Boss->TakeBossDamage(Damage, By);
				Hits++;
			}
		}
	}
	return Hits;
}

int32 ASQHordeManager::ApplyLineDamage(const FVector2D& From, const FVector2D& To, float HalfWidth, float Damage, ASQCharacter* By, float Slow)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D) return 0;

	const FVector2D Seg = To - From;
	const double SegLenSq = FMath::Max(1.0, Seg.SizeSquared());
	// Squared distance from a point to the segment
	auto LineDistSq = [&From, &Seg, SegLenSq](const FVector2D& Q) -> double
	{
		const double T = FMath::Clamp(FVector2D::DotProduct(Q - From, Seg) / SegLenSq, 0.0, 1.0);
		return FVector2D::DistSquared(Q, From + Seg * T);
	};

	int32 Hits = 0;
	const float MaxEnemyRadius = MaxTypeRadius;
	const double Pad = HalfWidth + MaxEnemyRadius;
	const FVector2D BoxMin(FMath::Min(From.X, To.X) - Pad, FMath::Min(From.Y, To.Y) - Pad);
	const FVector2D BoxMax(FMath::Max(From.X, To.X) + Pad, FMath::Max(From.Y, To.Y) + Pad);
	const FIntPoint MinC = Cell(BoxMin);
	const FIntPoint MaxC = Cell(BoxMax);
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive || !D->Enemies.IsValidIndex(Enemies[j].Type)) continue;
				const double Reach = HalfWidth + EnemyRadius(Enemies[j].Type);
				if (LineDistSq(Enemies[j].Pos) <= Reach * Reach)
				{
					DamageEnemy(j, Damage, By, Slow);
					Hits++;
				}
			}
		}
	}

	if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		if (ASQBoss* Boss = GS->Boss)
		{
			const FVector BL = Boss->GetActorLocation();
			const double Reach = HalfWidth + Boss->HitRadius();
			if (LineDistSq(FVector2D(BL.X, BL.Y)) <= Reach * Reach)
			{
				Boss->TakeBossDamage(Damage, By);
				Hits++;
			}
		}
	}
	return Hits;
}

int32 ASQHordeManager::ApplyChainDamage(const FVector2D& From, int32 Hits, float FirstRange, float JumpRange, float Damage, ASQCharacter* By, float Slow, TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();
	OutPoints.Add(From);
	if (!HasAuthority() || Hits <= 0) return 0;

	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQBoss* Boss = GS ? GS->Boss.Get() : nullptr;
	bool bBossDone = false;
	TArray<uint16, TInlineAllocator<16>> ChainHitIds;
	FVector2D Current = From;
	int32 Done = 0;

	for (int32 Jump = 0; Jump < Hits; ++Jump)
	{
		const float Range = Jump == 0 ? FirstRange : JumpRange;
		double BestSq = static_cast<double>(Range) * Range;
		int32 BestIdx = INDEX_NONE;
		bool bBestIsBoss = false;

		// Collision pass: "within range" means the body's edge (EnemyRadius) is, like every other weapon hit test
		const FIntPoint MinC = Cell(Current - FVector2D(Range + MaxTypeRadius));
		const FIntPoint MaxC = Cell(Current + FVector2D(Range + MaxTypeRadius));
		for (int32 x = MinC.X; x <= MaxC.X; ++x)
		{
			for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
			{
				const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
				if (!Bucket) continue;
				for (int32 j : *Bucket)
				{
					if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive || ChainHitIds.Contains(Enemies[j].Id)) continue;
					const double EdgeDist = FMath::Max(0.0, FVector2D::Distance(Enemies[j].Pos, Current) - static_cast<double>(EnemyRadius(Enemies[j].Type)));
					const double EdgeSq = EdgeDist * EdgeDist;
					if (EdgeSq < BestSq) { BestSq = EdgeSq; BestIdx = j; }
				}
			}
		}
		if (Boss && !bBossDone)
		{
			const FVector BL = Boss->GetActorLocation();
			const double DSq = FVector2D::DistSquared(FVector2D(BL.X, BL.Y), Current);
			if (DSq < BestSq) { BestSq = DSq; bBestIsBoss = true; }
		}

		if (bBestIsBoss)
		{
			const FVector BL = Boss->GetActorLocation();
			Current = FVector2D(BL.X, BL.Y);
			bBossDone = true;
			Boss->TakeBossDamage(Damage, By);
		}
		else if (BestIdx != INDEX_NONE)
		{
			Current = Enemies[BestIdx].Pos;
			ChainHitIds.Add(Enemies[BestIdx].Id);
			DamageEnemy(BestIdx, Damage, By, Slow);
		}
		else
		{
			break; // nothing left in reach
		}
		OutPoints.Add(Current);
		Done++;
	}
	return Done;
}

void ASQHordeManager::DamageEnemy(int32 Index, float Damage, ASQCharacter* By, float Slow, bool bFlash)
{
	if (!Enemies.IsValidIndex(Index)) return;
	FEnemy& E = Enemies[Index];
	if (!E.bAlive) return;
	if (E.MarkTime > 0.f) Damage *= 1.f + E.MarkBonus; // Lookout mark
	// Co-op combos (Nov 23 2026): an E / R primes what it hits; a hit from ANOTHER reaper (solo: your own weapons) bursts it.
	// The burst is queued and runs next sim step, so nothing here re-enters the damage loops.
	if (By && Damage > 0.f && !bInCombo && ComboWindow > 0.f)
	{
		const uint32 ById = By->GetUniqueID();
		const bool bPrimedByOther = E.PrimeTime > 0.f && (E.PrimeBy != ById || (bComboSolo && !bPrimeHits));
		if (bPrimedByOther)
		{
			float& ReadyAt = ComboReadyAt.FindOrAdd(ById);
			if (SimClock >= ReadyAt && PendingCombos.Num() < 24)
			{
				ReadyAt = SimClock + ComboCooldown;
				FPendingCombo& Combo = PendingCombos.AddDefaulted_GetRef();
				Combo.At = E.Pos;
				Combo.Damage = E.PrimeDamage * ComboBurst;
				Combo.By = By;
				E.PrimeTime = 0.f;
				E.PrimeDamage = 0.f;
			}
		}
		else if (bPrimeHits)
		{
			E.PrimeTime = ComboWindow;
			E.PrimeDamage = FMath::Max(E.PrimeDamage, Damage);
			E.PrimeBy = ById;
		}
	}
	Damage = USQAugments::OnHit(By, Damage, E.Type, E.Health, E.MaxHealth, E.Pos, E.EcoFlags); // augments + mission nests (Nov 9 missions)
	const float HealthBefore = E.Health;
	E.Health -= Damage;
	if (bFlash && Damage > 0.f)
	{
		E.bHitNet = true;
		E.bHitView = true;
	}
	if (Slow > 0.f) E.SlowTime = FMath::Max(E.SlowTime, Slow);
	if (E.Health <= 0.f)
	{
		// A big killing blow (3x the health it had left, and a real hit) launches the body away on every machine
		if (bFlash && Damage >= FMath::Max(25.f, HealthBefore * 3.f) && PendingLaunchIds.Num() < MaxLaunchIdsPerSnapshot)
		{
			PendingLaunchIds.Add(E.Id);
			if (LaunchIds.Num() < 256) LaunchIds.Add(E.Id); // listen-server host view
		}
		KillEnemy(Index, By);
	}
}

void ASQHordeManager::KillEnemy(int32 Index, ASQCharacter* By)
{
	const USQData* D = USQData::Get(this);
	if (!D || !Enemies.IsValidIndex(Index)) return;
	FEnemy& E = Enemies[Index];
	if (!E.bAlive) return;
	E.bAlive = false;
	CatchUpKills++;
	if (!D->Enemies.IsValidIndex(E.Type)) return; // data reloaded with fewer enemy types

	const FSQEnemyDef& Def = D->Enemies[E.Type];
	int32 Souls = Def.Souls;
	if (E.SlowTime > 0.f)
	{
		// Widow passive (team-wide): slowed enemies give extra souls, only if a Widow is on the crew.
		// Strength V (scales with team level): floor(V) souls + a (V - floor(V)) chance of one more. Best Widow counts.
		const float WidowBest = GetWidowBest();
		if (WidowBest > 0.f)
		{
			const int32 WidowWhole = FMath::FloorToInt(WidowBest);
			Souls += WidowWhole + (FMath::FRand() < WidowBest - static_cast<float>(WidowWhole) ? 1 : 0);
		}
	}
	if (E.MarkTime > 0.f) Souls += FMath::RoundToInt(D->Tune(TEXT("LookoutMarkSouls"), 2.f)); // Lookout mark bounty
	if (By && By->Stats.SoulBonus > 0.f && FMath::FRand() < By->Stats.SoulBonus) Souls += 1;
	if (const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		if (const ASQArena* Arena = GS->Arena.Get()) Souls = FMath::RoundToInt(Souls * Arena->SoulMult(E.Pos));
	}
	// Souls a Wisp stole come back exactly once: never multiplied by a bonus zone (feed a Wisp, kill it in the zone = duplication)
	Souls += E.Stolen;
	if (Def.Behaviour == ESQEnemyBehaviour::Splitter && !Def.SplitInto.IsNone())
	{
		const int32 SplitType = D->FindEnemyIndex(Def.SplitInto);
		for (int32 s = 0; s < Def.SplitCount && SplitType != INDEX_NONE; ++s)
		{
			FPendingSpawn Spawn;
			Spawn.Type = SplitType;
			Spawn.Pos = E.Pos + FMath::RandPointInCircle(80.f);
			Spawn.bBurst = true; // pops out of the body instead of rising from the ground
			PendingSpawns.Add(Spawn);
		}
	}
	// ---- Economy pass: gold, crowned / miniboss chests, quest stats
	ASQGameState* KillGS = GetWorld()->GetGameState<ASQGameState>();
	// Run events and district conditions (content item 12) scale what a kill sheds; souls a Wisp stole are never multiplied
	const float EventSoulMult = KillGS ? KillGS->DropMult(E.Pos, &FSQEventDef::SoulMult) : 1.f;
	const float EventXPMult = KillGS ? KillGS->DropMult(E.Pos, &FSQEventDef::XPMult) : 1.f;
	if (EventSoulMult != 1.f) Souls = E.Stolen + FMath::RoundToInt(static_cast<float>(FMath::Max(0, Souls - E.Stolen)) * EventSoulMult);
	if (Souls > 0) DropSouls(E.Pos, Souls);
	if (Def.XP > 0) DropXP(E.Pos, FMath::Max(1, FMath::RoundToInt(static_cast<float>(Def.XP) * EventXPMult)));

	const bool bElite = Def.Behaviour == ESQEnemyBehaviour::Elite;
	const bool bCrowned = (E.EcoFlags & EcoCrowned) != 0;
	const bool bMiniboss = (E.EcoFlags & EcoMiniboss) != 0;
	const FVector2D DeathPos = E.Pos;
	if (KillGS)
	{
		const float GoldScale = KillGS->GoldMultiplier() * KillGS->GoldNightGrowth() * KillGS->DropMult(DeathPos, &FSQEventDef::GoldMult);
		if (bMiniboss)
		{
			// Every reaper gets the miniboss's purse; a FreeChest beat drops one free chest per reaper
			const int32 MiniGold = FMath::RoundToInt(FMath::Max(0.f, D->Tune(TEXT("MinibossGold"), 20.f)) * GoldScale);
			int32 Crew = 0;
			for (APlayerState* EachPS : KillGS->PlayerArray)
			{
				if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS))
				{
					EachSQ->AddGold(MiniGold);
					EachSQ->CreditQuest(TEXT("MinibossKills"), 1);
					Crew++;
				}
			}
			const bool bChest = E.Id == MinibossId ? bMinibossChest : false;
			if (bChest)
			{
				for (int32 ChestIdx = 0; ChestIdx < FMath::Clamp(Crew, 1, 8); ++ChestIdx)
				{
					const float ChestAngle = 2.f * PI * static_cast<float>(ChestIdx) / static_cast<float>(FMath::Max(1, Crew));
					const FVector2D ChestAt = ClampToArena(DeathPos + FVector2D(FMath::Cos(ChestAngle), FMath::Sin(ChestAngle)) * (Crew > 1 ? 160.f : 0.f));
					ASQChest::SpawnChest(GetWorld(), FVector(ChestAt.X, ChestAt.Y, 0.f), true);
				}
			}
			if (E.Id == MinibossId)
			{
				MinibossId = 0;
				KillGS->MinibossName.Reset();
				KillGS->MinibossHealthFrac = 0.f;
			}
		}
		else if (bElite)
		{
			const float Pile = FMath::Max(0.f, D->Tune(TEXT("EliteGold"), 4.f)) * (bCrowned ? FMath::Max(1.f, D->Tune(TEXT("CrownedGoldMult"), 3.f)) : 1.f);
			DropGold(DeathPos, FMath::Max(1, FMath::RoundToInt(Pile * GoldScale)));
			if (bCrowned && FMath::FRand() < D->Tune(TEXT("CrownedChestChance"), 0.15f))
			{
				ASQChest::SpawnChest(GetWorld(), FVector(DeathPos.X, DeathPos.Y, 0.f), true);
			}
		}
		else if (FMath::FRand() < D->Tune(TEXT("GoldDropChance"), 0.04f))
		{
			DropGold(DeathPos, FMath::Max(1, FMath::RoundToInt(static_cast<float>(FMath::Max(1, Def.XP)) * D->Tune(TEXT("GoldPerXP"), 1.f) * GoldScale)));
		}
	}

	if (By)
	{
		if (ASQPlayerState* PS = By->GetPlayerState<ASQPlayerState>())
		{
			PS->Kills++;
			PS->CreditQuest(TEXT("Kills"), 1);
			if (KillStatByType.Num() != D->Enemies.Num())
			{
				KillStatByType.Reset();
				for (const FSQEnemyDef& StatDef : D->Enemies) KillStatByType.Add(FName(*FString::Printf(TEXT("Kill_%s"), *StatDef.Id.ToString())));
			}
			if (KillStatByType.IsValidIndex(E.Type)) PS->CreditQuest(KillStatByType[E.Type], 1);
			if (bElite) PS->CreditQuest(TEXT("EliteKills"), 1);
			if (bCrowned) PS->CreditQuest(TEXT("CrownedKills"), 1);
			if (KillGS && KillGS->bFinalSwarm) PS->CreditQuest(TEXT("SwarmKills"), 1);
			if (By->IsSliding()) PS->CreditQuest(TEXT("SlideKills"), 1); // the killer's weapon landed while it slid
		}
		By->OnPassiveKill(E.Pos, Def.Behaviour == ESQEnemyBehaviour::Elite);
	}
	USQAugments::OnKill(this, By, DeathPos, E.Type, E.EcoFlags); // augment kill effects + mission director (Nov 9 missions)
}

// ================================================================ economy pass (server)

void ASQHordeManager::DropGold(const FVector2D& Pos, int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return;
	const int32 Coins = FMath::Clamp(Amount / 8 + 1, 1, 3);
	const int32 Per = Amount / Coins;
	int32 Rest = Amount - Per * Coins;
	for (int32 CoinIdx = 0; CoinIdx < Coins; ++CoinIdx)
	{
		const int32 CoinValue = Per + (Rest-- > 0 ? 1 : 0);
		if (CoinValue > 0) SpawnPickup(2, Pos + FMath::RandPointInCircle(40.f + 20.f * Coins), CoinValue);
	}
}

void ASQHordeManager::GiveGold(ASQCharacter* Finder, float Amount)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D || !(Amount > 0.f)) return;
	const bool bShared = D->Tune(TEXT("GoldShareCoop"), 1.f) > 0.5f;
	ASQPlayerState* FinderPS = Finder ? Finder->GetPlayerState<ASQPlayerState>() : nullptr;
	if (!bShared || GS->PlayerArray.Num() <= 1)
	{
		if (FinderPS) FinderPS->AddGoldExact(Amount);
		else if (GS->PlayerArray.Num() > 0)
		{
			if (ASQPlayerState* FirstPS = Cast<ASQPlayerState>(GS->PlayerArray[0])) FirstPS->AddGoldExact(Amount);
		}
		return;
	}
	// Shared purse: the team picks up about (1 + Bonus x extra reapers) x a solo crew's coins, so each reaper gets 1 / that,
	// i.e. every reaper earns about a solo purse and pays solo prices (Docs/Balance.md "Economy")
	const float Share = 1.f / (1.f + FMath::Max(0.f, D->Tune(TEXT("PlayerSpawnRateBonus"), 0.35f)) * static_cast<float>(GS->PlayerArray.Num() - 1));
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS)) EachSQ->AddGoldExact(Amount * Share);
	}
}

void ASQHordeManager::VacuumPickups()
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (!HasAuthority() || !GS) return;
	int64 XPTotal = 0;
	int64 GoldTotal = 0;
	int64 SoulTotal = 0;
	for (const FPickup& P : Pickups)
	{
		if (!P.bAlive) continue;
		if (P.Kind == 0) XPTotal += P.Value;
		else if (P.Kind == 2) GoldTotal += P.Value;
		else SoulTotal += P.Value;
	}
	Pickups.Reset();
	PickupNet.Reset();
	if (XPTotal > 0) GS->AddTeamXP(static_cast<int32>(FMath::Min<int64>(XPTotal, 100000000)));
	if (GoldTotal > 0) GiveGold(nullptr, static_cast<float>(FMath::Min<int64>(GoldTotal, 100000000)));
	if (SoulTotal > 0) GS->ShopSouls = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(GS->ShopSouls) + SoulTotal, 100000000));
}

bool ASQHordeManager::FindBeatSpot(float MinR, float MaxR, float MinGap, bool bFlyer, FVector2D& Out)
{
	SpawnSpots.Reset();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->bDowned) SpawnSpots.Add(It->Pos2D());
	}
	if (SpawnSpots.Num() == 0) return false;
	for (int32 SpotTry = 0; SpotTry < 12; ++SpotTry)
	{
		const FVector2D& AnchorPos = SpawnSpots[FMath::RandRange(0, SpawnSpots.Num() - 1)];
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const FVector2D Cand = ClampToArena(AnchorPos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * FMath::FRandRange(MinR, MaxR));
		if (SpawnSpotOk(Cand, MinGap, bFlyer))
		{
			Out = Cand;
			return true;
		}
	}
	return false;
}

void ASQHordeManager::SpawnMiniboss(FName EnemyId, float HealthMult, float DamageMult, const FString& Name, bool bFreeChest)
{
	const USQData* D = USQData::Get(this);
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const int32 Type = D ? D->FindEnemyIndex(EnemyId) : INDEX_NONE;
	if (!HasAuthority() || !GS || Type == INDEX_NONE) return;
	FVector2D Spot;
	if (!FindBeatSpot(1400.f, 1900.f, 1100.f, D->Enemies[Type].IsFlyer(), Spot)) return;
	const int32 Index = SpawnEnemyEx(Type, Spot, false, HealthMult, DamageMult, EcoMiniboss);
	if (Index == INDEX_NONE) return;
	MinibossId = Enemies[Index].Id;
	bMinibossChest = bFreeChest;
	GS->MinibossName = Name;
	GS->MinibossHealthFrac = 1.f;
}

void ASQHordeManager::SpawnSwarm(FName EnemyId, int32 Count)
{
	const USQData* D = USQData::Get(this);
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const int32 Type = D ? D->FindEnemyIndex(EnemyId) : INDEX_NONE;
	if (!HasAuthority() || !GS || Type == INDEX_NONE || Count <= 0) return;
	const float Scale = (1.f + FMath::Max(0.f, D->Tune(TEXT("PlayerSpawnRateBonus"), 0.35f)) * static_cast<float>(FMath::Max(0, GS->PlayerArray.Num() - 1)))
		* (1.f + FMath::Max(0.f, GS->Difficulty)) * GS->ModProduct(&FSQModifierDef::SpawnMult);
	const int32 Total = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Count) * Scale), 1, 400);
	const bool bFlyer = D->Enemies[Type].IsFlyer();
	// One packed arc per wave: the swarm comes from one side (readable, dodgeable), at the usual safe spawn distance
	FVector2D Centre;
	if (!FindBeatSpot(1700.f, 2100.f, 1300.f, bFlyer, Centre)) return;
	for (int32 SwarmIdx = 0; SwarmIdx < Total; ++SwarmIdx)
	{
		FVector2D Spot = ClampToArena(Centre + FMath::RandPointInCircle(450.f));
		if (!SpawnSpotOk(Spot, 1100.f, bFlyer)) Spot = Centre;
		if (SpawnEnemyEx(Type, Spot, false, 1.f, 1.f, EcoSwarm) == INDEX_NONE) break;
	}
}

void ASQHordeManager::SimFinalSwarm(float DeltaSeconds, const ASQGameState* GS, const USQData* D)
{
	// Endless, escalating tiers from Timeline.csv (Kind FinalSwarm, At = minutes after midnight). Normal spawning carries on too.
	const TArray<FSQTimelineBeat>& Beats = SQQuests::Timeline();
	if (FinalSwarmAccum.Num() != Beats.Num()) FinalSwarmAccum.SetNumZeroed(Beats.Num());
	const float Minutes = FMath::Max(0.f, GS->FinalSwarmSeconds) / 60.f;
	const float Crew = 1.f + FMath::Max(0.f, D->Tune(TEXT("PlayerSpawnRateBonus"), 0.35f)) * static_cast<float>(FMath::Max(0, GS->PlayerArray.Num() - 1));
	const float Ramp = 1.f + FMath::Max(0.f, D->Tune(TEXT("FinalSwarmRampPerMinute"), 0.5f)) * Minutes;
	const float HealthRamp = 1.f + FMath::Max(0.f, D->Tune(TEXT("FinalSwarmHealthPerMinute"), 0.3f)) * Minutes;
	const float DamageRamp = 1.f + FMath::Max(0.f, D->Tune(TEXT("FinalSwarmDamagePerMinute"), 0.15f)) * Minutes;
	const float Extra = (1.f + FMath::Max(0.f, GS->Difficulty)) * GS->ModProduct(&FSQModifierDef::SpawnMult);
	const int32 HardCap = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MaxEnemies"), 800.f) * 1.25f), 1, 2000);
	const float MinGap = FMath::Min(D->Tune(TEXT("SpawnRadiusMin"), 2200.f), D->Tune(TEXT("SpawnMinPlayerDistance"), 1500.f));
	for (int32 BeatIdx = 0; BeatIdx < Beats.Num(); ++BeatIdx)
	{
		const FSQTimelineBeat& Beat = Beats[BeatIdx];
		if (Beat.Kind != ESQBeatKind::FinalSwarm || Minutes < Beat.At || !Beat.AppliesToNight(GS->NightIndex)) continue;
		const int32 Type = D->FindEnemyIndex(Beat.Enemy);
		if (Type == INDEX_NONE) continue;
		FinalSwarmAccum[BeatIdx] += Beat.Count * Crew * Ramp * Extra * DeltaSeconds;
		int32 Guard = 0;
		while (FinalSwarmAccum[BeatIdx] >= 1.f && Guard++ < 32)
		{
			FinalSwarmAccum[BeatIdx] -= 1.f;
			if (Enemies.Num() >= HardCap) { FinalSwarmAccum[BeatIdx] = 0.f; break; }
			FVector2D Spot;
			if (!FindBeatSpot(MinGap + 200.f, MinGap + 900.f, MinGap, D->Enemies[Type].IsFlyer(), Spot)) break;
			SpawnEnemyEx(Type, Spot, false, Beat.HealthMult * HealthRamp, Beat.DamageMult * DamageRamp, EcoSwarm);
		}
	}
}

void ASQHordeManager::UpdateMinibossTracker()
{
	if (MinibossId == 0) return;
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	for (const FEnemy& Each : Enemies)
	{
		if (Each.Id == MinibossId && Each.bAlive)
		{
			if (GS) GS->MinibossHealthFrac = FMath::Clamp(Each.Health / FMath::Max(1.f, Each.MaxHealth), 0.f, 1.f);
			return;
		}
	}
	// Gone without a kill (cleared, fell off the map): drop the bar
	MinibossId = 0;
	if (GS)
	{
		GS->MinibossName.Reset();
		GS->MinibossHealthFrac = 0.f;
	}
}

void ASQHordeManager::ApplyKnockback(const FVector2D& Center, float Radius, const FVector2D& Dir, float Strength, float MaxEnemyRadius)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D || Radius <= 0.f || Strength <= 0.f) return;
	const double RadiusSq = static_cast<double>(Radius) * Radius;
	const FVector2D Push = Dir.GetSafeNormal();
	const float Speed = FMath::Min(Strength, 5000.f);
	for (FEnemy& Each : Enemies)
	{
		if (!Each.bAlive || !D->Enemies.IsValidIndex(Each.Type)) continue;
		const FSQEnemyDef& KnockDef = D->Enemies[Each.Type];
		// "Small" = the runtime body radius (EnemyRadius: CSV blended to the art), the same circle body blocking uses
		if (KnockDef.IsFlyer() || EnemyRadius(Each.Type) > MaxEnemyRadius) continue;
		if (Each.AnimState == static_cast<uint8>(ESQEnemyAnimState::Spawning)) continue;
		if (FVector2D::DistSquared(Each.Pos, Center) > RadiusSq) continue;
		const FVector2D Away = Push.IsNearlyZero() ? (Each.Pos - Center).GetSafeNormal() : Push;
		Each.Knock = Away * Speed;
	}
}

void ASQHordeManager::ApplyPull(const FVector2D& Center, float Radius, float Strength, float MaxEnemyRadius)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D || Radius <= 0.f || Strength <= 0.f) return;
	const double PullRadiusSq = static_cast<double>(Radius) * Radius;
	const float PullSpeed = FMath::Min(Strength, 5000.f);
	for (FEnemy& Each : Enemies)
	{
		if (!Each.bAlive || !D->Enemies.IsValidIndex(Each.Type)) continue;
		if (D->Enemies[Each.Type].IsFlyer() || EnemyRadius(Each.Type) > MaxEnemyRadius) continue;
		if (Each.AnimState == static_cast<uint8>(ESQEnemyAnimState::Spawning)) continue;
		const double PullDistSq = FVector2D::DistSquared(Each.Pos, Center);
		if (PullDistSq > PullRadiusSq || PullDistSq < 1600.0) continue;
		Each.Knock = (Center - Each.Pos).GetSafeNormal() * PullSpeed;
	}
}

int32 ASQHordeManager::ApplyConeDamage(const FVector2D& Origin, const FVector2D& Dir, float Range, float HalfAngleDeg, float Damage, ASQCharacter* By, float Slow)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D || Range <= 0.f) return 0;
	const FVector2D Facing = Dir.IsNearlyZero() ? FVector2D(1.f, 0.f) : Dir.GetSafeNormal();
	const double MinCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDeg, 1.f, 180.f)));
	auto InCone = [&Origin, &Facing, MinCos, Range](const FVector2D& Q, float BodyRadius) -> bool
	{
		const FVector2D Delta = Q - Origin;
		const double Dist = Delta.Size();
		if (Dist > static_cast<double>(Range) + BodyRadius) return false;
		if (Dist <= static_cast<double>(BodyRadius) + 1.0) return true; // standing on the caster
		return FVector2D::DotProduct(Delta / Dist, Facing) >= MinCos;
	};
	int32 Hits = 0;
	const FIntPoint MinC = Cell(Origin - FVector2D(Range + MaxTypeRadius));
	const FIntPoint MaxC = Cell(Origin + FVector2D(Range + MaxTypeRadius));
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive || !D->Enemies.IsValidIndex(Enemies[j].Type)) continue;
				if (InCone(Enemies[j].Pos, EnemyRadius(Enemies[j].Type)))
				{
					DamageEnemy(j, Damage, By, Slow);
					Hits++;
				}
			}
		}
	}
	if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		if (ASQBoss* Boss = GS->Boss)
		{
			const FVector BL = Boss->GetActorLocation();
			if (InCone(FVector2D(BL.X, BL.Y), Boss->HitRadius()))
			{
				Boss->TakeBossDamage(Damage, By);
				Hits++;
			}
		}
	}
	return Hits;
}

// ================================================================ reap core (Nov 9 2026, server; USQReapComponent)

bool ASQHordeManager::IsEnemyRipe(int32 Index) const
{
	if (!Enemies.IsValidIndex(Index)) return false;
	const FEnemy& E = Enemies[Index];
	if (!E.bAlive || (E.EcoFlags & EcoMiniboss) || E.AnimState == static_cast<uint8>(ESQEnemyAnimState::Spawning)) return false;
	return RipeFraction > 0.f && E.Health < E.MaxHealth * RipeFraction;
}

uint8 ASQHordeManager::EnemyNetState(const FEnemy& E) const
{
	const bool bRipe = E.bAlive && !(E.EcoFlags & EcoMiniboss) && RipeFraction > 0.f && E.Health < E.MaxHealth * RipeFraction
		&& E.AnimState != static_cast<uint8>(ESQEnemyAnimState::Spawning);
	return static_cast<uint8>((E.AnimState & EnemyStateMask) | (E.bBurstSpawn ? EnemyBurstBit : 0)
		| ((E.EcoFlags & EcoCrowned) ? EnemyCrownBit : 0) | ((E.EcoFlags & EcoMiniboss) ? EnemyMinibossBit : 0)
		| (bRipe ? EnemyRipeBit : 0));
}

void ASQHordeManager::ExecuteEnemy(int32 Index, ASQCharacter* By)
{
	if (!Enemies.IsValidIndex(Index) || !Enemies[Index].bAlive) return;
	FEnemy& E = Enemies[Index];
	E.bHitNet = true;
	E.bHitView = true;
	// A reaped body is flung away on every machine (the big-hit launch path)
	if (PendingLaunchIds.Num() < MaxLaunchIdsPerSnapshot)
	{
		PendingLaunchIds.Add(E.Id);
		if (LaunchIds.Num() < 256) LaunchIds.Add(E.Id);
	}
	E.Health = 0.f;
	KillEnemy(Index, By);
}

void ASQHordeManager::ApplyReap(const FVector2D& Origin, const FVector2D& Dir, float Range, float HalfAngleDeg, float Damage, float BossBonus, ASQCharacter* By, FSQReapHits& Out)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D || Range <= 0.f) return;
	const FVector2D Facing = Dir.IsNearlyZero() ? FVector2D(1.f, 0.f) : Dir.GetSafeNormal();
	const double MinCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDeg, 1.f, 180.f)));
	auto InReapCone = [&Origin, &Facing, MinCos, Range](const FVector2D& Q, float BodyRadius) -> bool
	{
		const FVector2D Delta = Q - Origin;
		const double Dist = Delta.Size();
		if (Dist > static_cast<double>(Range) + BodyRadius) return false;
		if (Dist <= static_cast<double>(BodyRadius) + 1.0) return true;
		return FVector2D::DotProduct(Delta / Dist, Facing) >= MinCos;
	};
	const float Bonus = FMath::Max(0.f, BossBonus);
	// Gather first: kills inside the loop can queue blasts / splits that change the horde under us
	TArray<int32, TInlineAllocator<64>> InCone;
	const FIntPoint MinC = Cell(Origin - FVector2D(Range + MaxTypeRadius));
	const FIntPoint MaxC = Cell(Origin + FVector2D(Range + MaxTypeRadius));
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive || !D->Enemies.IsValidIndex(Enemies[j].Type)) continue;
				if (Enemies[j].AnimState == static_cast<uint8>(ESQEnemyAnimState::Spawning)) continue;
				if (InReapCone(Enemies[j].Pos, EnemyRadius(Enemies[j].Type))) InCone.Add(j);
			}
		}
	}
	for (const int32 j : InCone)
	{
		if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive) continue;
		const FVector2D At = Enemies[j].Pos;
		Out.Hit++;
		if (IsEnemyRipe(j))
		{
			ExecuteEnemy(j, By);
			Out.Executed++;
			Out.Reaped++;
			Out.Spots.Add(At);
			continue;
		}
		const bool bMini = (Enemies[j].EcoFlags & EcoMiniboss) != 0;
		DamageEnemy(j, Damage * (bMini ? Bonus : 1.f), By, 0.f);
		if (Enemies.IsValidIndex(j) && !Enemies[j].bAlive)
		{
			Out.Reaped++;
			Out.Spots.Add(At);
		}
	}
	if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		if (ASQBoss* Boss = GS->Boss)
		{
			const FVector BL = Boss->GetActorLocation();
			if (InReapCone(FVector2D(BL.X, BL.Y), Boss->HitRadius()))
			{
				// Bosses are never executed: the scythe bites harder instead
				Boss->TakeBossDamage(Damage * Bonus, By);
				Out.Hit++;
				Out.bHitBoss = true;
			}
		}
	}
}

void ASQHordeManager::ReapAlongPath(const FVector2D& From, const FVector2D& To, float Radius, ASQCharacter* By, FSQReapHits& Out)
{
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !D) return;
	const FVector2D Seg = To - From;
	const double SegLenSq = Seg.SizeSquared();
	const float Reach = FMath::Max(0.f, Radius) + MaxTypeRadius;
	const FIntPoint MinC = Cell(FVector2D(FMath::Min(From.X, To.X), FMath::Min(From.Y, To.Y)) - FVector2D(Reach));
	const FIntPoint MaxC = Cell(FVector2D(FMath::Max(From.X, To.X), FMath::Max(From.Y, To.Y)) + FVector2D(Reach));
	TArray<int32, TInlineAllocator<32>> Touched;
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!IsEnemyRipe(j) || !D->Enemies.IsValidIndex(Enemies[j].Type)) continue;
				const double Along = SegLenSq > 1.0 ? FMath::Clamp(FVector2D::DotProduct(Enemies[j].Pos - From, Seg) / SegLenSq, 0.0, 1.0) : 0.0;
				const FVector2D Closest = From + Seg * Along;
				const float Touch = Radius + EnemyRadius(Enemies[j].Type);
				if (FVector2D::DistSquared(Closest, Enemies[j].Pos) <= static_cast<double>(Touch) * Touch) Touched.Add(j);
			}
		}
	}
	for (const int32 j : Touched)
	{
		if (!IsEnemyRipe(j)) continue;
		const FVector2D At = Enemies[j].Pos;
		ExecuteEnemy(j, By);
		Out.Hit++;
		Out.Executed++;
		Out.Reaped++;
		Out.Spots.Add(At);
	}
}

bool ASQHordeManager::FindNearestRipe(const FVector2D& From, float Range, FVector2D& OutPos) const
{
	const USQData* D = USQData::Get(this);
	if (!D) return false;
	double BestSq = static_cast<double>(Range) * Range;
	bool bFound = false;
	const FIntPoint MinC = Cell(From - FVector2D(Range));
	const FIntPoint MaxC = Cell(From + FVector2D(Range));
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!IsEnemyRipe(j)) continue;
				const double DistSq = FVector2D::DistSquared(Enemies[j].Pos, From);
				if (DistSq < BestSq) { BestSq = DistSq; OutPos = Enemies[j].Pos; bFound = true; }
			}
		}
	}
	return bFound;
}

bool ASQHordeManager::MarkToughestEnemy(const FVector2D& From, float Range, float Bonus, float Seconds, uint16& OutId, FVector2D& OutPos)
{
	OutId = 0;
	if (!HasAuthority()) return false;
	const double RangeSq = static_cast<double>(Range) * Range;
	int32 BestIdx = INDEX_NONE;
	float BestHealth = 0.f;
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		const FEnemy& Cand = Enemies[i];
		if (!Cand.bAlive || FVector2D::DistSquared(Cand.Pos, From) > RangeSq) continue;
		if (BestIdx == INDEX_NONE || Cand.Health > BestHealth)
		{
			BestIdx = i;
			BestHealth = Cand.Health;
		}
	}
	if (BestIdx == INDEX_NONE) return false;
	FEnemy& Marked = Enemies[BestIdx];
	Marked.MarkBonus = Marked.MarkTime > 0.f ? FMath::Max(Marked.MarkBonus, Bonus) : Bonus;
	Marked.MarkTime = FMath::Max(Marked.MarkTime, Seconds);
	OutId = Marked.Id;
	OutPos = Marked.Pos;
	return true;
}

bool ASQHordeManager::FindEnemyPos(uint16 EnemyId, FVector2D& OutPos) const
{
	if (EnemyId == 0) return false;
	for (const FEnemy& Cand : Enemies)
	{
		if (Cand.Id == EnemyId && Cand.bAlive)
		{
			OutPos = Cand.Pos;
			return true;
		}
	}
	return false;
}

int32 ASQHordeManager::FreezeArea(const FVector2D& Center, float Radius, float Seconds)
{
	if (!HasAuthority() || Seconds <= 0.f) return 0;
	const double RadiusSq = static_cast<double>(Radius) * Radius;
	int32 Frozen = 0;
	for (FEnemy& Cand : Enemies)
	{
		if (!Cand.bAlive || FVector2D::DistSquared(Cand.Pos, Center) > RadiusSq) continue;
		Cand.FrozenTime = FMath::Max(Cand.FrozenTime, Seconds);
		Frozen++;
	}
	return Frozen;
}

void ASQHordeManager::SpawnPickup(uint8 Kind, const FVector2D& Pos, int32 Value)
{
	const USQData* D = USQData::Get(this);
	const int32 MaxPickups = D ? FMath::RoundToInt(D->Tune(TEXT("MaxPickups"), 700.f)) : 700;

	// Over the cap: retire the oldest pickup of the SAME kind and merge its value into this one (nothing is lost:
	// the old code retired XP even for a new soul and threw that XP away, and never capped souls at all)
	if (Pickups.Num() >= MaxPickups)
	{
		bool bMerged = false;
		for (FPickup& Old : Pickups)
		{
			if (Old.bAlive && Old.Kind == Kind)
			{
				Old.bAlive = false;
				Value += Old.Value;
				bMerged = true;
				break;
			}
		}
		// The cap is full of the other kind: fold this one into the newest pickup of its kind, else let it through
		if (!bMerged)
		{
			for (int32 PickIdx = Pickups.Num() - 1; PickIdx >= 0; --PickIdx)
			{
				FPickup& Newest = Pickups[PickIdx];
				if (Newest.bAlive && Newest.Kind == Kind) { Newest.Value += Value; return; }
			}
		}
	}

	FPickup P;
	P.Id = TakeNextId();
	P.Kind = Kind;
	P.Pos = ClampToArena(Pos);
	P.Value = Value;
	Pickups.Add(P);
}

uint8 ASQHordeManager::PickupTier(const FPickup& P) const
{
	const USQData* D = USQData::Get(this);
	if (P.Kind == 1)
	{
		// Souls: merged wisps grow (Nov 12 2026)
		const float SV = static_cast<float>(P.Value);
		const float S1 = D ? D->Tune(TEXT("SoulTier1"), 3.f) : 3.f;
		const float S2 = D ? D->Tune(TEXT("SoulTier2"), 8.f) : 8.f;
		const float S3 = D ? D->Tune(TEXT("SoulTier3"), 20.f) : 20.f;
		return static_cast<uint8>(SV >= S3 ? 3 : (SV >= S2 ? 2 : (SV >= S1 ? 1 : 0)));
	}
	const bool bGold = P.Kind == 2;
	const float V = static_cast<float>(P.Value);
	const float T1 = D ? D->Tune(bGold ? TEXT("GoldTier1") : TEXT("XpTier1"), bGold ? 5.f : 4.f) : 4.f;
	const float T2 = D ? D->Tune(bGold ? TEXT("GoldTier2") : TEXT("XpTier2"), bGold ? 15.f : 12.f) : 12.f;
	const float T3 = D ? D->Tune(bGold ? TEXT("GoldTier3") : TEXT("XpTier3"), 40.f) : 40.f;
	return static_cast<uint8>(V >= T3 ? 3 : (V >= T2 ? 2 : (V >= T1 ? 1 : 0)));
}

void ASQHordeManager::MergePickups(float DeltaSeconds, const TArray<ASQCharacter*>& Players)
{
	const USQData* D = USQData::Get(this);
	PickupMergeClock -= DeltaSeconds;
	if (!D || PickupMergeClock > 0.f) return;
	PickupMergeClock = FMath::Max(0.1f, D->Tune(TEXT("PickupMergeEvery"), 0.35f));
	const int32 MergeAt = FMath::Max(2, FMath::RoundToInt(D->Tune(TEXT("PickupMergeAt"), 60.f)));
	const double Cell = FMath::Max(20.f, D->Tune(TEXT("PickupMergeRadius"), 200.f));
	const float MinAge = D->Tune(TEXT("PickupMergeMinAge"), 0.8f);
	const double KeepClearSq = FMath::Square(static_cast<double>(D->Tune(TEXT("PickupMergeKeepClear"), 450.f)));
	// Souls merge much sooner than XP / gold (Nov 12 2026): a carpet of small wisps was the clutter in the screenshots
	const int32 SoulMergeAt = FMath::Max(2, FMath::RoundToInt(D->Tune(TEXT("SoulMergeAt"), 8.f)));
	const uint8 Kinds[3] = { 0, 2, 1 };
	for (const uint8 Kind : Kinds)
	{
		int32 Count = 0;
		for (const FPickup& P : Pickups) if (P.bAlive && P.Kind == Kind) ++Count;
		if (Count < (Kind == 1 ? SoulMergeAt : MergeAt)) continue;
		// One host per grid cell: everything else of this kind in the cell folds into it (value-weighted position, so it slides together)
		TMap<FIntPoint, int32> Hosts;
		for (int32 PickIdx = 0; PickIdx < Pickups.Num(); ++PickIdx)
		{
			FPickup& P = Pickups[PickIdx];
			if (!P.bAlive || P.Kind != Kind || P.Age < MinAge) continue;
			bool bNearPlayer = false;
			for (const ASQCharacter* C : Players)
			{
				if (C && FVector2D::DistSquared(C->Pos2D(), P.Pos) < KeepClearSq) { bNearPlayer = true; break; }
			}
			if (bNearPlayer) continue;   // being collected: leave it alone
			const FIntPoint Key(FMath::FloorToInt(static_cast<float>(P.Pos.X / Cell)), FMath::FloorToInt(static_cast<float>(P.Pos.Y / Cell)));
			if (const int32* HostIdx = Hosts.Find(Key))
			{
				FPickup& Host = Pickups[*HostIdx];
				const double Total = static_cast<double>(Host.Value + P.Value);
				if (Total > 0.0) Host.Pos = (Host.Pos * static_cast<double>(Host.Value) + P.Pos * static_cast<double>(P.Value)) / Total;
				Host.Value += P.Value;
				P.bAlive = false;
			}
			else
			{
				Hosts.Add(Key, PickIdx);
			}
		}
	}
}

void ASQHordeManager::DropSouls(const FVector2D& Pos, int32 Amount)
{
	if (Amount <= 0) return;
	// Nov 12 2026: one or two wisps per kill, not six (same soul total; SoulChunks in Tuning.csv)
	const USQData* ChunkData = USQData::Get(this);
	const int32 MaxChunks = ChunkData ? FMath::Clamp(FMath::RoundToInt(ChunkData->Tune(TEXT("SoulChunks"), 2.f)), 1, 6) : 2;
	const int32 Chunks = FMath::Min(Amount, MaxChunks);
	const int32 Per = Amount / Chunks;
	int32 Rest = Amount - Per * Chunks;
	for (int32 i = 0; i < Chunks; ++i)
	{
		const int32 V = Per + (Rest-- > 0 ? 1 : 0);
		SpawnPickup(1, Pos + FMath::RandPointInCircle(70.f + Chunks * 15.f), V);
	}
}

void ASQHordeManager::DropXP(const FVector2D& Pos, int32 Amount)
{
	if (Amount > 0) SpawnPickup(0, Pos + FMath::RandPointInCircle(25.f), Amount);
}

void ASQHordeManager::SpawnProjectile(const FVector2D& Pos, const FVector2D& Vel, float Damage, float Radius, float Life, int32 Pierce, bool bHostile, ASQCharacter* Shooter)
{
	// Bounded: a huge build (projectile bonus x turrets x 4 reapers) must not grow the sim / ProjNet without limit
	const USQData* ProjData = USQData::Get(this);
	const int32 MaxProjectiles = ProjData ? FMath::Clamp(FMath::RoundToInt(ProjData->Tune(TEXT("MaxProjectiles"), 1200.f)), 64, 1800) : 1200;
	if (Projectiles.Num() >= MaxProjectiles) return;

	FProjectile P;
	P.Id = TakeNextId();
	P.Pos = Pos;
	P.Vel = Vel;
	P.Damage = Damage;
	P.Radius = Radius;
	P.Life = Life;
	P.Pierce = FMath::Max(1, Pierce);
	P.bHostile = bHostile;
	P.Owner = Shooter;
	Projectiles.Add(P);
}

void ASQHordeManager::FindNearestTargets(const FVector2D& From, int32 Count, float MaxRange, TArray<FVector2D>& Out) const
{
	Out.Reset();
	TArray<TPair<float, FVector2D>> Candidates;
	const float MaxSq = MaxRange * MaxRange;
	for (const FEnemy& E : Enemies)
	{
		if (!E.bAlive) continue;
		const float DSq = FVector2D::DistSquared(E.Pos, From);
		if (DSq <= MaxSq) Candidates.Emplace(DSq, E.Pos);
	}
	if (const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		if (const ASQBoss* Boss = GS->Boss)
		{
			const FVector BL = Boss->GetActorLocation();
			const FVector2D BP(BL.X, BL.Y);
			const float DSq = FVector2D::DistSquared(BP, From);
			if (DSq <= MaxSq) Candidates.Emplace(0.f, BP); // always prioritise the boss
		}
	}
	Candidates.Sort([](const TPair<float, FVector2D>& A, const TPair<float, FVector2D>& B) { return A.Key < B.Key; });
	for (int32 i = 0; i < Candidates.Num() && Out.Num() < Count; ++i)
	{
		Out.Add(Candidates[i].Value);
	}
}

bool ASQHordeManager::FindNearestPickup(const FVector2D& From, uint8 Kind, float MaxRange, FVector2D& Out) const
{
	float BestSq = MaxRange * MaxRange;
	bool bFound = false;
	for (const FPickup& P : Pickups)
	{
		if (!P.bAlive || P.Kind != Kind) continue;
		const float DSq = FVector2D::DistSquared(P.Pos, From);
		if (DSq < BestSq) { BestSq = DSq; Out = P.Pos; bFound = true; }
	}
	return bFound;
}

FVector2D ASQHordeManager::ComputeThreat(const FVector2D& From, float Range) const
{
	FVector2D Push = FVector2D::ZeroVector;
	const FIntPoint MinC = Cell(From - FVector2D(Range));
	const FIntPoint MaxC = Cell(From + FVector2D(Range));
	for (int32 x = MinC.X; x <= MaxC.X; ++x)
	{
		for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
		{
			const TArray<int32>* Bucket = Grid.Find(FIntPoint(x, y));
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (!Enemies.IsValidIndex(j) || !Enemies[j].bAlive) continue;
				const FVector2D Delta = From - Enemies[j].Pos;
				const float Dist = Delta.Size();
				if (Dist > 1.f && Dist < Range) Push += Delta / Dist * (1.f - Dist / Range);
			}
		}
	}
	return Push;
}

void ASQHordeManager::ClearSoulPickups()
{
	for (FPickup& P : Pickups)
	{
		if (P.Kind == 1) P.bAlive = false;
	}
}

void ASQHordeManager::ClearAll()
{
	PendingSpawns.Reset();
	Enemies.Reset();
	Pickups.Reset();
	Projectiles.Reset();
	Grid.Reset();
	EnemyNet.Reset();
	PickupNet.Reset();
	ProjNet.Reset();
	HitNet.Reset();
	PendingLaunchIds.Reset();
	LaunchIds.Reset();
	SpawnAccum = 0.f;
	MinibossId = 0;
	bMinibossChest = false;
	FinalSwarmAccum.Reset();
	CatchUpMult = 1.f;
	CatchUpKillRate = 0.f;
	CatchUpNear = 0.f;
	CatchUpKills = 0;
	TutorialHoldTimer = 0.f;
}

int32 ASQHordeManager::NumAlive() const
{
	int32 N = 0;
	for (const FEnemy& E : Enemies) if (E.bAlive) N++;
	return N;
}

// ================================================================ body blocking (every machine)

bool ASQHordeManager::TypeBlocks(const FSQEnemyDef& Def)
{
	// Flyers pass over reapers and wisps are loose souls: only ground bodies block
	return !Def.IsFlyer() && Def.Behaviour != ESQEnemyBehaviour::Wisp;
}

float ASQHordeManager::TypeBlockHeight(const FSQEnemyDef& Def) const
{
	return Def.Height * Def.Scale * Def.Stretch.Y * BlockHeightMult;
}

int32 ASQHordeManager::GatherBodyContacts(const FVector2D& At, float Radius, float Skin, float FeetAboveGround, FSQBodyContacts& Out) const
{
	Out.Reset();
	const USQData* D = USQData::Get(this);
	if (!D) return 0;
	const float SafeSkin = FMath::Max(0.f, Skin);
	auto Consider = [&Out, &At, Radius, SafeSkin, FeetAboveGround](const FVector2D& BodyPos, float BodyRadius, float BodyHeight)
	{
		if (FeetAboveGround > BodyHeight || Out.Num() >= 16) return; // jumped over it (or plenty of contacts already)
		const FVector2D FromBody = At - BodyPos;
		const float TouchDist = Radius + BodyRadius;
		const double FromSq = FromBody.SizeSquared();
		if (FromSq >= FMath::Square(static_cast<double>(TouchDist + SafeSkin))) return;
		const float FromLen = static_cast<float>(FMath::Sqrt(FromSq));
		FSQBodyContact Contact;
		Contact.Normal = FromLen > 0.5f ? FromBody / FromLen : FVector2D(1.f, 0.f);
		Contact.Depth = TouchDist - FromLen;
		Out.Add(Contact);
	};
	const FIntPoint Center = Cell(At);
	const bool bSim = HasAuthority();
	for (int32 CellX = -1; CellX <= 1; ++CellX)
	{
		for (int32 CellY = -1; CellY <= 1; ++CellY)
		{
			const FIntPoint Key(Center.X + CellX, Center.Y + CellY);
			if (bSim)
			{
				// Server: live sim positions (Grid is rebuilt each sim step; indices stay valid until the next one)
				const TArray<int32>* SimBucket = Grid.Find(Key);
				if (!SimBucket) continue;
				for (const int32 SimIdx : *SimBucket)
				{
					if (!Enemies.IsValidIndex(SimIdx)) continue;
					const FEnemy& Body = Enemies[SimIdx];
					if (!Body.bAlive || Body.AnimState == static_cast<uint8>(ESQEnemyAnimState::Spawning) || !D->Enemies.IsValidIndex(Body.Type)) continue;
					const FSQEnemyDef& BodyDef = D->Enemies[Body.Type];
					if (!TypeBlocks(BodyDef)) continue;
					Consider(Body.Pos, EnemyRadius(Body.Type), TypeBlockHeight(BodyDef));
				}
			}
			else
			{
				// Owning client: the same horde at its interpolated positions (BuildClientBlockers)
				const TArray<int32>* ViewBucket = ClientBlockGrid.Find(Key);
				if (!ViewBucket) continue;
				for (const int32 BlockIdx : *ViewBucket)
				{
					if (!ClientBlockers.IsValidIndex(BlockIdx)) continue;
					const FBlocker& Blocker = ClientBlockers[BlockIdx];
					Consider(Blocker.Pos, Blocker.Radius, Blocker.Height);
				}
			}
		}
	}
	return Out.Num();
}

bool ASQHordeManager::AnyEnemyWithin(const FVector2D& At, float Range) const
{
	const double RangeSq = static_cast<double>(Range) * Range;
	if (HasAuthority())
	{
		for (const FEnemy& Each : Enemies)
		{
			if (Each.bAlive && FVector2D::DistSquared(Each.Pos, At) <= RangeSq) return true;
		}
		return false;
	}
	for (const TPair<uint16, FVector2D>& Shown : EnemyDisplay)
	{
		if (FVector2D::DistSquared(Shown.Value, At) <= RangeSq) return true;
	}
	return false;
}

bool ASQHordeManager::FindNearestEnemyAnyMachine(const FVector2D& From, float MaxRange, FVector2D& OutPos) const
{
	double BestSq = static_cast<double>(MaxRange) * MaxRange;
	bool bFoundAny = false;
	if (HasAuthority())
	{
		for (const FEnemy& Each : Enemies)
		{
			if (!Each.bAlive) continue;
			const double EachSq = FVector2D::DistSquared(Each.Pos, From);
			if (EachSq < BestSq) { BestSq = EachSq; OutPos = Each.Pos; bFoundAny = true; }
		}
	}
	else
	{
		for (const TPair<uint16, FVector2D>& Shown : EnemyDisplay)
		{
			const double ShownSq = FVector2D::DistSquared(Shown.Value, From);
			if (ShownSq < BestSq) { BestSq = ShownSq; OutPos = Shown.Value; bFoundAny = true; }
		}
	}
	const UWorld* AimWorld = GetWorld();
	const ASQGameState* AimGS = AimWorld ? AimWorld->GetGameState<ASQGameState>() : nullptr;
	if (const ASQBoss* AimBoss = AimGS ? AimGS->Boss.Get() : nullptr)
	{
		const FVector BossLoc = AimBoss->GetActorLocation();
		const FVector2D BossPos(BossLoc.X, BossLoc.Y);
		if (FVector2D::DistSquared(BossPos, From) < BestSq) { OutPos = BossPos; bFoundAny = true; }
	}
	return bFoundAny;
}

void ASQHordeManager::BuildClientBlockers()
{
	ClientBlockers.Reset();
	ClientBlockGrid.Reset();
	const USQData* D = USQData::Get(this);
	const UWorld* World = GetWorld();
	const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
	if (!D || !LocalPawn) return;
	const FVector PawnLoc = LocalPawn->GetActorLocation();
	const FVector2D Around(PawnLoc.X, PawnLoc.Y);
	const double CullSq = FMath::Square(1200.0); // only bodies the local reaper could touch this frame or the next
	for (const FViewItem& V : ViewEnemies)
	{
		if (!D->Enemies.IsValidIndex(V.Type) || SQEnemyAnim(V.State) == ESQEnemyAnimState::Spawning) continue;
		const FSQEnemyDef& Def = D->Enemies[V.Type];
		if (!TypeBlocks(Def) || FVector2D::DistSquared(V.Pos, Around) > CullSq) continue;
		FBlocker Blocker;
		Blocker.Pos = V.Pos;
		Blocker.Radius = EnemyRadius(V.Type);
		Blocker.Height = TypeBlockHeight(Def);
		const int32 BlockIdx = ClientBlockers.Add(Blocker);
		ClientBlockGrid.FindOrAdd(Cell(V.Pos)).Add(BlockIdx);
	}
}

float ASQHordeManager::FlyTargetZ(const FSQEnemyDef& Def, const FViewItem& V, float Now) const
{
	if (!Def.IsFlyer()) return 0.f;
	const float BobPhase = IdHash01(V.Id, 5u) * 2.f * PI;
	const float Bob = Def.FlyBob * FMath::Sin(Now * 2.2f + BobPhase);
	const float StateAge = Now - V.StateAt;
	const bool bSwoopType = Def.Fly == ESQEnemyFly::Swoop;
	switch (SQEnemyAnim(V.State))
	{
	case ESQEnemyAnimState::Windup:
		// Gravebat rears up before the dive; a Wraith lifts a little as it fades out
		return bSwoopType ? Def.Altitude + 60.f * FMath::Clamp(StateAge / FMath::Max(0.3f, Def.Windup), 0.f, 1.f) : Def.Altitude + 25.f;
	case ESQEnemyAnimState::Charge:
		// The dive: down to reaper chest height
		return bSwoopType ? FMath::Min(Def.Altitude, 55.f) : Def.Altitude + Bob;
	case ESQEnemyAnimState::Recover:
		return Def.Altitude + 0.5f * Bob; // climbing back (the smoothing in CarryViewState makes it a climb)
	case ESQEnemyAnimState::Frozen:
		return Def.Altitude;
	default:
		return Def.Altitude + Bob;
	}
}

int32 ASQHordeManager::NumPickups() const
{
	return HasAuthority() ? Pickups.Num() : PickupNet.Num();
}

void ASQHordeManager::BuildSnapshots()
{
	EnemyNet.Reset(Enemies.Num());
	// Hit signal: sequence number first so the array always changes (OnRep fires every snapshot), then hit Ids
	HitNet.Reset();
	HitNet.Add(++HitSeq);
	for (FEnemy& E : Enemies)
	{
		if (!E.bAlive) continue;
		if (E.bHitNet)
		{
			E.bHitNet = false;
			if (HitNet.Num() <= MaxHitIdsPerSnapshot) HitNet.Add(E.Id);
		}
		if (EnemyNet.Num() >= MaxNetEntries) continue; // stay under the replicated array limit (net.MaxRepArraySize 2048)
		FSQEnemyNet N;
		N.Id = E.Id; N.X = Quantize(E.Pos.X); N.Y = Quantize(E.Pos.Y); N.Type = E.Type;
		N.State = EnemyNetState(E);
		EnemyNet.Add(N);
	}
	if (PendingLaunchIds.Num() > 0)
	{
		HitNet.Add(0); // marker: the rest are launch deaths
		for (const uint16 LaunchedId : PendingLaunchIds) HitNet.Add(LaunchedId);
		PendingLaunchIds.Reset();
	}
	PickupNet.Reset(Pickups.Num());
	for (const FPickup& P : Pickups)
	{
		if (!P.bAlive || PickupNet.Num() >= MaxNetEntries) continue;
		FSQPickupNet N;
		N.Id = P.Id; N.X = Quantize(P.Pos.X); N.Y = Quantize(P.Pos.Y); N.Kind = PackedPickupKind(P);
		PickupNet.Add(N);
	}
	ProjNet.Reset(Projectiles.Num());
	for (const FProjectile& P : Projectiles)
	{
		if (!P.bAlive || ProjNet.Num() >= MaxNetEntries) continue;
		FSQProjNet N;
		N.Id = P.Id; N.X = Quantize(P.Pos.X); N.Y = Quantize(P.Pos.Y); N.Hostile = P.bHostile ? 1 : 0;
		ProjNet.Add(N);
	}
}

// ================================================================ rendering (all machines)

float ASQHordeManager::GroundAt(const FVector2D& P) const
{
	return FrameArena ? FrameArena->GroundZ((float)P.X, (float)P.Y) : 0.f;
}

bool ASQHordeManager::NearestPlayer(const FVector2D& From, FVector2D& OutPos, float& OutDistSq) const
{
	bool bFound = false;
	float BestSq = TNumericLimits<float>::Max();
	for (const FVector2D& PlayerPos : FramePlayers)
	{
		const float DSq = (float)FVector2D::DistSquared(PlayerPos, From);
		if (DSq < BestSq)
		{
			BestSq = DSq;
			OutPos = PlayerPos;
			bFound = true;
		}
	}
	OutDistSq = BestSq;
	return bFound;
}

void ASQHordeManager::OnRep_HitNet()
{
	// [0] is the snapshot sequence; then enemies damaged since the previous snapshot; after a 0 marker, launch deaths
	bool bLaunchPart = false;
	for (int32 i = 1; i < HitNet.Num(); ++i)
	{
		const uint16 NetId = HitNet[i];
		if (NetId == 0) { bLaunchPart = true; continue; }
		if (bLaunchPart)
		{
			if (LaunchIds.Num() < 256) LaunchIds.Add(NetId);
		}
		else if (PendingHits.Num() < 512)
		{
			PendingHits.Add(NetId);
		}
	}
}

void ASQHordeManager::UpdateISM(UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xforms)
{
	if (!ISM) return;
	if (ISM->GetInstanceCount() != Xforms.Num())
	{
		ISM->ClearInstances();
		if (Xforms.Num() > 0) ISM->AddInstances(Xforms, false, true);
	}
	else if (Xforms.Num() > 0)
	{
		ISM->BatchUpdateInstancesTransforms(0, Xforms, true, true, true);
	}
}

void ASQHordeManager::RenderAll(float DeltaSeconds)
{
	FrameArena = nullptr;
	if (GetNetMode() == NM_DedicatedServer) return;
	const USQData* D = USQData::Get(this);
	UWorld* World = GetWorld();
	if (!D || !World) return;
	EnsureRenderComponents();

	// Per-frame caches: arena (ground height) and standing player positions (known on every machine)
	const ASQGameState* GS = World->GetGameState<ASQGameState>();
	FrameArena = GS ? GS->Arena.Get() : nullptr;
	FramePlayers.Reset();
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		const ASQCharacter* PlayerChar = *It;
		if (PlayerChar && !PlayerChar->bDowned) FramePlayers.Add(PlayerChar->Pos2D());
	}

	// Gather view items: server reads the simulation, clients interpolate snapshots
	ViewEnemies.Reset();
	ViewPickups.Reset();
	ViewProjs.Reset();
	if (HasAuthority())
	{
		for (FEnemy& E : Enemies)
		{
			if (!E.bAlive) continue;
			ViewEnemies.Add({ E.Id, E.Type, E.Pos, E.bHitView, EnemyNetState(E) });
			E.bHitView = false;
		}
		for (const FPickup& P : Pickups) if (P.bAlive) ViewPickups.Add({ P.Id, PackedPickupKind(P), P.Pos, false });
		for (const FProjectile& P : Projectiles) if (P.bAlive) ViewProjs.Add({ P.Id, (uint8)(P.bHostile ? 1 : 0), P.Pos, false });
	}
	else
	{
		auto Interp = [DeltaSeconds](const TMap<uint16, FVector2D>& Display, uint16 Id, const FVector2D& Target, float Speed)
		{
			const FVector2D* Prev = Display.Find(Id);
			if (!Prev) return Target;
			// A real jump (a Wraith's phase-blink) snaps instead of sliding across the floor
			if (FVector2D::DistSquared(*Prev, Target) > FMath::Square(350.0)) return Target;
			return FMath::Vector2DInterpTo(*Prev, Target, DeltaSeconds, Speed);
		};

		const bool bAnyHits = PendingHits.Num() > 0;
		ScratchDisplay.Reset();
		for (const FSQEnemyNet& N : EnemyNet)
		{
			const FVector2D P = Interp(EnemyDisplay, N.Id, FVector2D(N.X * 4.0, N.Y * 4.0), 10.f);
			ScratchDisplay.Add(N.Id, P);
			ViewEnemies.Add({ N.Id, N.Type, P, bAnyHits && PendingHits.Contains(N.Id), N.State });
		}
		Swap(EnemyDisplay, ScratchDisplay);
		PendingHits.Reset();

		ScratchDisplay.Reset();
		for (const FSQPickupNet& N : PickupNet)
		{
			const FVector2D P = Interp(PickupDisplay, N.Id, FVector2D(N.X * 4.0, N.Y * 4.0), 14.f);
			ScratchDisplay.Add(N.Id, P);
			ViewPickups.Add({ N.Id, N.Kind, P, false });
		}
		Swap(PickupDisplay, ScratchDisplay);

		ScratchDisplay.Reset();
		for (const FSQProjNet& N : ProjNet)
		{
			const FVector2D P = Interp(ProjDisplay, N.Id, FVector2D(N.X * 4.0, N.Y * 4.0), 20.f);
			ScratchDisplay.Add(N.Id, P);
			ViewProjs.Add({ N.Id, N.Hostile, P, false });
		}
		Swap(ProjDisplay, ScratchDisplay);
	}

	const float T = World->GetTimeSeconds();
	CarryViewState(T, DeltaSeconds);
	// Owning clients block their reaper against these interpolated bodies (the server uses the sim)
	if (!HasAuthority()) BuildClientBlockers();

	// Nearest enemies with imported art become animated skeletons; the rest stay instanced primitives
	RenderSkinned(ViewEnemies, DeltaSeconds, ScratchSkinnedMask);
	TickDeathPuppets(DeltaSeconds);
	TickDebris(DeltaSeconds);

	// Hit sparks near the local player (skinned and primitive enemies alike)
	{
		const float SparkRadiusSq = SkinRadius * SkinRadius;
		int32 HitSparks = 0;
		const bool bMotionKit = SQFx::HasMotionKit(World);
		for (const FViewItem& V : ViewEnemies)
		{
			if (!V.bHit || HitSparks >= 24) continue;
			if (!D->Enemies.IsValidIndex(V.Type)) continue;
			if (FVector2D::DistSquared(V.Pos, SkinViewCenter) > SparkRadiusSq) continue;
			const FSQEnemyDef& Def = D->Enemies[V.Type];
			const bool bArt = SkinTypes.IsValidIndex(V.Type) && SkinTypes[V.Type].Mesh != nullptr;
			const float SparkZ = GroundAt(V.Pos) + (bArt ? SkinTypes[V.Type].ArtHeight * 0.55f : 60.f * Def.Scale);
			const FLinearColor SparkColor = Def.Color * 0.4f + FLinearColor::White * 0.6f;
			// Nov 19 2026: with the motion kit in, a hit is a starburst with needle sparks (a shape) rather than four balls; costs ~8 particles, so the frame cap drops to 14
			if (bMotionKit)
			{
				if (HitSparks >= 14) break;
				SQFx::Impact(World, FVector(V.Pos.X, V.Pos.Y, SparkZ), SparkColor, FMath::Clamp(0.55f + 0.25f * Def.Scale, 0.6f, 1.4f), false);
			}
			else SQFx::Burst(World, FVector(V.Pos.X, V.Pos.Y, SparkZ), SparkColor, 4, 300.f, 0.3f, 5.f, -900.f);
			HitSparks++;
		}
	}

	RenderEnemyISMs(T, DeltaSeconds);
	RenderTelegraphs(T, DeltaSeconds);
#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
	if (bShowCollision && HasAuthority()) DrawCollisionDebug();
#endif

	// Ground shadows under flyers (smaller and fainter-looking as they climb)
	ScratchShadow.Reset();
	for (const FViewItem& V : ViewEnemies)
	{
		if (!D->Enemies.IsValidIndex(V.Type) || !D->Enemies[V.Type].IsFlyer()) continue;
		const float ShadowGround = GroundAt(V.Pos);
		const float Height = FMath::Max(0.f, SQFlyerBaseZ(V.FlyGround, V.FlyZ, ShadowGround) - ShadowGround);
		const float Shrink = FMath::Clamp(1.f - Height / 900.f, 0.45f, 1.f);
		const float ShadowR = FMath::Max(15.f, EnemyRadius(V.Type) * 1.25f * Shrink);
		ScratchShadow.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, ShadowGround + 1.5f), FVector(ShadowR / 50.f, ShadowR / 50.f, 0.01f)));
	}
	UpdateISM(ShadowISM, ScratchShadow);
	UpdateSplats(ViewEnemies);

	ScratchXP.Reset();
	ScratchSoul.Reset();
	ScratchSoulFull.Reset();
	ScratchSoulTail.Reset();
	ScratchSoulMote.Reset();
	ScratchSoulPool.Reset();
	ScratchSoulHalo.Reset();
	SoulSeenNow.Reset();
	ScratchGold.Reset();
	for (TArray<FTransform>& TierXforms : ScratchPickupTier) TierXforms.Reset();
	// Your own sack full: souls on the ground go grey (you'd run over them and nothing happens otherwise)
	bool bLocalSackFull = false;
	{
		const APlayerController* SackPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (const ASQCharacter* SackPawn = SackPC ? Cast<ASQCharacter>(SackPC->GetPawn()) : nullptr)
		{
			bLocalSackFull = SackPawn->Stats.CarryCap > 0 && SackPawn->Carried >= SackPawn->Stats.CarryCap;
		}
	}
	for (const FViewItem& V : ViewPickups)
	{
		// FSQPickupNet::Kind: low nibble = kind (0 XP, 1 soul, 2 gold), high nibble = tier (PickupTier)
		const uint8 PickKind = V.Type & 0x0F;
		const int32 Tier = FMath::Clamp(static_cast<int32>(V.Type >> 4), 0, 3);
		const float Bob = GroundAt(V.Pos) + 25.f + 8.f * FMath::Sin(T * 4.f + V.Id);
		const int32 Slot = PickKind == 0 ? Tier : (PickKind == 2 ? 4 + Tier : INDEX_NONE);
		if (ScratchPickupTier.IsValidIndex(Slot) && PickupTierNorm.IsValidIndex(Slot))
		{
			// XP crystals turn slowly and grow per tier; coins spin on edge, stacks / purses / bars sit and turn
			// Read from the top-down camera: a 30 cm upright shard looked like a dot, so crystals are bigger and tilted
			// Nov 12 2026 (chase camera): crystals a size up again so they read against the horde's feet
			static const float SizeCm[8] = { 66.f, 80.f, 98.f, 120.f, 48.f, 58.f, 62.f, 66.f };
			const float Scale = SizeCm[Slot] / 100.f * PickupTierNorm[Slot];
			const float Spin = PickKind == 2 && Tier == 0 ? 220.f : (PickKind == 0 ? 60.f + 15.f * Tier : 45.f);
			const float Lift = PickKind == 0 ? 6.f * Tier : (Tier == 0 ? 15.f : 4.f);
			const float Tilt = PickKind == 0 ? 28.f : 0.f; // leaning crystals show their facets from above
			ScratchPickupTier[Slot].Add(FTransform(FRotator(Tilt, T * Spin + V.Id * 37.f, 0.f), FVector(V.Pos.X, V.Pos.Y, Bob + Lift), FVector(Scale)));
		}
		else if (PickKind == 0) ScratchXP.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Bob), FVector(0.18f)));
		else if (PickKind == 2) ScratchGold.Add(FTransform(FRotator(90.f, T * 220.f + V.Id * 37.f, 0.f), FVector(V.Pos.X, V.Pos.Y, Bob + 15.f), FVector(0.32f, 0.32f, 0.06f)));
		else
		{
			SoulSeenNow.Add(V.Id, FVector(V.Pos.X, V.Pos.Y, GroundAt(V.Pos) + 55.f));
			const float Phase = T * 2.6f + V.Id * 0.91f;
			const float Ground = GroundAt(V.Pos);
			const float CoreZ = Ground + 55.f + 10.f * FMath::Sin(Phase);
			const FVector Core(V.Pos.X, V.Pos.Y, CoreZ);
			const float Pulse = 1.f + 0.10f * FMath::Sin(T * 7.f + V.Id);
			if (bSoulGlow)
			{
				// Light wisp (additive): bigger for merged souls (tier from SoulTier1-3), dim teal when the sack is full
				static const float TierScale[4] = { 1.f, 1.3f, 1.65f, 2.1f };
				const float S = TierScale[Tier];
				const FVector Wisp(V.Pos.X, V.Pos.Y, Ground + 42.f + 12.f * S + 8.f * FMath::Sin(Phase));
				if (bLocalSackFull)
				{
					ScratchSoulFull.Add(FTransform(FRotator::ZeroRotator, Wisp, FVector(0.30f * S)));
					ScratchSoulFull.Add(FTransform(FRotator::ZeroRotator, Wisp, FVector(0.09f * S)));
					continue;
				}
				ScratchSoul.Add(FTransform(FRotator::ZeroRotator, Wisp, FVector(0.11f * S * Pulse)));
				ScratchSoulHalo.Add(FTransform(FRotator::ZeroRotator, Wisp, FVector(0.44f * S * (2.f - Pulse))));
				const float WispSway = 7.f * FMath::Sin(Phase * 1.3f);
				const float TailW = 0.24f * S * SoulNorm;
				ScratchSoulTail.Add(FTransform(FRotator(WispSway, V.Id * 53.f + T * 30.f, 0.f), Wisp - FVector(0.f, 0.f, 8.f * S),
					FVector(TailW, TailW, 0.62f * S * SoulNorm * (1.f + 0.12f * FMath::Sin(T * 9.f + V.Id)))));
				ScratchSoulPool.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Ground + 1.5f), FVector(0.55f * S, 0.55f * S, 0.004f)));
				if (Tier >= 2)
				{
					for (int32 MoteIdx = 0; MoteIdx < 2; ++MoteIdx)
					{
						const float Ang = T * 2.8f + V.Id + MoteIdx * PI;
						ScratchSoulMote.Add(FTransform(FRotator::ZeroRotator,
							Wisp + FVector(FMath::Cos(Ang) * 26.f * S, FMath::Sin(Ang) * 26.f * S, 10.f * FMath::Sin(Ang * 1.7f)), FVector(0.05f * S)));
					}
				}
				continue;
			}
			if (bLocalSackFull)
			{
				ScratchSoulFull.Add(FTransform(FRotator::ZeroRotator, Core, FVector(0.22f)));
				ScratchSoulPool.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Ground + 1.f), FVector(0.25f, 0.25f, 0.004f)));
				continue;
			}
			ScratchSoul.Add(FTransform(FRotator::ZeroRotator, Core, FVector(0.20f * Pulse)));
			// Wisp body: the flame mesh hangs round the core with its tip trailing up and swaying
			const float Sway = 9.f * FMath::Sin(Phase * 1.3f);
			const float TailScale = 0.62f * SoulNorm;
			ScratchSoulTail.Add(FTransform(FRotator(Sway, V.Id * 53.f + T * 40.f, 0.f), Core - FVector(0.f, 0.f, 14.f),
				FVector(TailScale, TailScale, TailScale * (1.15f + 0.1f * FMath::Sin(T * 9.f + V.Id)))));
			for (int32 MoteIdx = 0; MoteIdx < 3; ++MoteIdx)
			{
				const float Ang = T * 3.2f + V.Id + MoteIdx * 2.094f;
				const FVector MoteAt = Core + FVector(FMath::Cos(Ang) * 30.f, FMath::Sin(Ang) * 30.f, 12.f * FMath::Sin(Ang * 1.7f));
				ScratchSoulMote.Add(FTransform(FRotator::ZeroRotator, MoteAt, FVector(0.05f)));
			}
			const float PoolR = 0.45f * Pulse;
			ScratchSoulPool.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Ground + 1.f), FVector(PoolR, PoolR, 0.004f)));
		}
	}
	RenderSoulAbsorb(DeltaSeconds);
	UpdateISM(XPISM, ScratchXP);
	UpdateISM(SoulISM, ScratchSoul);
	UpdateISM(SoulFullISM, ScratchSoulFull);
	UpdateISM(SoulTailISM, ScratchSoulTail);
	UpdateISM(SoulMoteISM, ScratchSoulMote);
	UpdateISM(SoulPoolISM, ScratchSoulPool);
	UpdateISM(SoulHaloISM, ScratchSoulHalo);
	UpdateISM(GoldISM, ScratchGold);
	for (int32 TierIdx = 0; TierIdx < PickupTierISMs.Num() && TierIdx < ScratchPickupTier.Num(); ++TierIdx)
	{
		UpdateISM(PickupTierISMs[TierIdx], ScratchPickupTier[TierIdx]);
	}

	// Economy pass: gold crowns over crowned elites (big ones over minibosses) and a red ground ring under minibosses
	ScratchCrown.Reset();
	ScratchMiniRing.Reset();
	for (const FViewItem& V : ViewEnemies)
	{
		if (!(V.State & (EnemyCrownBit | EnemyMinibossBit)) || !D->Enemies.IsValidIndex(V.Type)) continue;
		const FSQEnemyDef& CrownDef = D->Enemies[V.Type];
		const bool bMini = (V.State & EnemyMinibossBit) != 0;
		const float CrownGround = GroundAt(V.Pos);
		const bool bArtBody = SkinTypes.IsValidIndex(V.Type) && SkinTypes[V.Type].Mesh != nullptr;
		const float BodyTop = bArtBody ? SkinTypes[V.Type].ArtHeight : 100.f * CrownDef.Scale * CrownDef.Stretch.Y;
		const float CrownSize = bMini ? 0.9f : 0.5f;
		const float CrownBob = 10.f * FMath::Sin(T * 3.f + V.Id);
		ScratchCrown.Add(FTransform(FRotator(0.f, T * 90.f, 0.f), FVector(V.Pos.X, V.Pos.Y, CrownGround + BodyTop + 40.f + CrownBob), FVector(CrownSize, CrownSize, CrownSize * 0.6f)));
		if (bMini)
		{
			const float RingR = (EnemyRadius(V.Type) + 60.f) / 50.f;
			ScratchMiniRing.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, CrownGround + 2.f), FVector(RingR, RingR, 0.015f)));
		}
	}
	UpdateISM(CrownISM, ScratchCrown);
	UpdateISM(MiniRingISM, ScratchMiniRing);

	// Reap core: ripe enemies (State bit 6) get a pulsing soul-teal ring at their feet and a small skull tag over the head
	ScratchRipeRing.Reset();
	ScratchRipeSkull.Reset();
	for (const FViewItem& V : ViewEnemies)
	{
		if (ScratchRipeRing.Num() >= MaxRipeMarkers) break;
		if (!(V.State & EnemyRipeBit) || !D->Enemies.IsValidIndex(V.Type) || SQEnemyAnim(V.State) == ESQEnemyAnimState::Spawning) continue;
		const FSQEnemyDef& RipeDef = D->Enemies[V.Type];
		const float RipeGround = GroundAt(V.Pos);
		const float RipePulse = 1.f + 0.07f * FMath::Sin(T * 7.f + V.Id * 0.37f);
		const float RingR = (EnemyRadius(V.Type) + 22.f) * RipePulse / 50.f;
		ScratchRipeRing.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, RipeGround + 3.f), FVector(RingR, RingR, 0.012f)));
		const bool bRipeArt = SkinTypes.IsValidIndex(V.Type) && SkinTypes[V.Type].Mesh != nullptr;
		const float RipeTop = bRipeArt ? SkinTypes[V.Type].ArtHeight : 100.f * RipeDef.Scale * RipeDef.Stretch.Y;
		const float RipeFly = RipeDef.IsFlyer() ? RipeDef.Altitude : 0.f;
		const float SkullZ = RipeGround + RipeFly + RipeTop + ((V.State & EnemyCrownBit) ? 95.f : 30.f) + 5.f * FMath::Sin(T * 4.f + V.Id);
		ScratchRipeSkull.Add(FTransform(FRotator(0.f, T * 60.f + V.Id * 23.f, 0.f), FVector(V.Pos.X, V.Pos.Y, SkullZ), FVector(RipeSkullFit)));
	}
	UpdateISMPadded(RipeRingISM, ScratchRipeRing, RipeRingCapacity);
	UpdateISMPadded(RipeSkullISM, ScratchRipeSkull, RipeSkullCapacity);

	ScratchProj.Reset();
	ScratchHostile.Reset();
	for (const FViewItem& V : ViewProjs)
	{
		const float Ground = GroundAt(V.Pos);
		if (V.Type == 0) ScratchProj.Add(FTransform(FRotator(0.f, T * 900.f + V.Id * 20.f, 0.f), FVector(V.Pos.X, V.Pos.Y, Ground + 60.f), FVector(0.45f, 0.08f, 0.04f)));
		else ScratchHostile.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Ground + 70.f), FVector(0.35f)));
	}
	UpdateISM(ProjISM, ScratchProj);
	UpdateISM(HostileProjISM, ScratchHostile);

	FrameArena = nullptr; // only valid during this render
}

void ASQHordeManager::RenderSoulAbsorb(float DeltaSeconds)
{
	// Collection flourish (cosmetic, every machine): a soul that vanished next to a reaper was lifted by them, so it
	// arcs up and into their chest, shrinking, with its motes streaming behind, then a ring flashes round the body.
	// Works on clients too: it only compares which soul ids were drawn last frame with this frame.
	TArray<TPair<FVector, TWeakObjectPtr<ASQCharacter>>, TInlineAllocator<8>> Reapers;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->bDowned) Reapers.Emplace(It->GetActorLocation(), *It);
	}
	const double GrabSq = FMath::Square(320.0);
	for (const TPair<int32, FVector>& Prev : SoulSeenPrev)
	{
		if (SoulSeenNow.Contains(Prev.Key)) continue;
		int32 BestIdx = INDEX_NONE;
		double BestSq = GrabSq;
		for (int32 ReaperIdx = 0; ReaperIdx < Reapers.Num(); ++ReaperIdx)
		{
			const double DSq = FVector2D::DistSquared(FVector2D(Reapers[ReaperIdx].Key), FVector2D(Prev.Value));
			if (DSq < BestSq) { BestSq = DSq; BestIdx = ReaperIdx; }
		}
		if (BestIdx != INDEX_NONE && SoulFlies.Num() < 96)
		{
			FSoulFly Fly;
			Fly.From = Prev.Value;
			Fly.To = Reapers[BestIdx].Value;
			Fly.Seed = static_cast<float>(Prev.Key % 97);
			SoulFlies.Add(Fly);
		}
	}
	Swap(SoulSeenPrev, SoulSeenNow);

	const float FlyTime = 0.42f;
	const float BurstTime = 0.28f;
	for (int32 FlyIdx = SoulFlies.Num() - 1; FlyIdx >= 0; --FlyIdx)
	{
		FSoulFly& Fly = SoulFlies[FlyIdx];
		Fly.Age += DeltaSeconds;
		const ASQCharacter* Target = Fly.To.Get();
		if (Target) Fly.LastTarget = Target->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		if (Fly.Age >= FlyTime + BurstTime || (Fly.LastTarget.IsZero() && !Target)) { SoulFlies.RemoveAtSwap(FlyIdx); continue; }
		if (Fly.Age < FlyTime)
		{
			// Ease-in (slow lift, fast snap into the body) on a sideways-curling arc
			const float Alpha = Fly.Age / FlyTime;
			const float Ease = Alpha * Alpha * (3.f - 2.f * Alpha) * 0.35f + Alpha * Alpha * 0.65f;
			const FVector Path = Fly.LastTarget - Fly.From;
			const FVector Side = FVector::CrossProduct(Path.GetSafeNormal(), FVector::UpVector) * (FMath::Sin(Fly.Seed) * 60.f);
			auto At = [&](float A) -> FVector
			{
				const float E = A * A * (3.f - 2.f * A) * 0.35f + A * A * 0.65f;
				return Fly.From + Path * E + FVector(0.f, 0.f, 110.f * FMath::Sin(PI * A)) + Side * FMath::Sin(PI * A);
			};
			const FVector Here = At(Alpha);
			const float Shrink = FMath::Lerp(0.24f, 0.08f, Ease);
			ScratchSoul.Add(FTransform(FRotator::ZeroRotator, Here, FVector(Shrink)));
			if (bSoulGlow) ScratchSoulHalo.Add(FTransform(FRotator::ZeroRotator, Here, FVector(Shrink * 3.2f)));
			const float TailScale = 0.5f * SoulNorm * (1.f - 0.6f * Ease);
			const FRotator Facing = (Here - At(FMath::Max(0.f, Alpha - 0.08f))).Rotation() + FRotator(-90.f, 0.f, 0.f);
			ScratchSoulTail.Add(FTransform(Facing, Here, FVector(TailScale, TailScale, TailScale * 1.6f)));
			for (int32 Trail = 1; Trail <= 4; ++Trail)
			{
				ScratchSoulMote.Add(FTransform(FRotator::ZeroRotator, At(FMath::Max(0.f, Alpha - Trail * 0.07f)), FVector(0.06f - Trail * 0.01f)));
			}
		}
		else
		{
			// Arrival: a teal ring blooms round the reaper's feet and a few motes pop upward
			const float B = (Fly.Age - FlyTime) / BurstTime;
			const FVector Feet = Fly.LastTarget - FVector(0.f, 0.f, 40.f) + FVector(0.f, 0.f, 4.f);
			// Nov 19 2026: the frame the soul lands, a wisp lifts off the reaper with a starburst and four orbiting motes (FxRecipes.csv SoulCollect)
			if (Fly.Age - DeltaSeconds < FlyTime) SQFxRecipe::PlayShared(GetWorld(), FName(TEXT("SoulCollect")), Fly.LastTarget - FVector(0.f, 0.f, 40.f), FLinearColor(0.5f, 0.91f, 1.f), 60.f);
			const float RingR = FMath::Lerp(0.3f, 1.3f, B);
			ScratchSoulPool.Add(FTransform(FRotator::ZeroRotator, Feet, FVector(RingR, RingR, 0.004f * (1.f - B) + 0.001f)));
			// glow build: a soft bloom of light at the chest as the soul lands
			if (bSoulGlow) ScratchSoulHalo.Add(FTransform(FRotator::ZeroRotator, Fly.LastTarget, FVector(FMath::Lerp(0.9f, 0.2f, B))));
			for (int32 Pop = 0; Pop < 4; ++Pop)
			{
				const float Ang = Fly.Seed + Pop * 1.571f;
				ScratchSoulMote.Add(FTransform(FRotator::ZeroRotator,
					Fly.LastTarget + FVector(FMath::Cos(Ang) * 45.f * B, FMath::Sin(Ang) * 45.f * B, 70.f * B), FVector(0.05f * (1.f - B))));
			}
		}
	}
}

void ASQHordeManager::UpdateSplats(const TArray<FViewItem>& EnemyView)
{
	const USQData* D = USQData::Get(this);
	UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!D) return;

	// Enemies that vanished since last frame died -> leave a clay splat (works the same on server and clients)
	ScratchCurrent.Reset();
	for (const FViewItem& V : EnemyView) ScratchCurrent.Add(V.Id, V);

	ScratchVanished.Reset();
	for (const TPair<uint16, FViewItem>& Pair : LastEnemyView)
	{
		if (!ScratchCurrent.Contains(Pair.Key)) ScratchVanished.Add(Pair.Value);
	}
	Swap(LastEnemyView, ScratchCurrent);

	const bool bCalmPhase = GS && (GS->Phase == ESQPhase::Shop || GS->Phase == ESQPhase::Waiting);
	if (bCalmPhase || ScratchVanished.Num() > 150) // phase reset, not real deaths: wipe the floor clean
	{
		if (Splats.Num() > 0) { Splats.Reset(); SplatHead = 0; bSplatsDirty = true; }
		LaunchIds.Reset();
	}
	else
	{
		LocalDeathCount += ScratchVanished.Num();
		const int32 MaxSplats = 600;
		const float BurstRadiusSq = SkinRadius * SkinRadius;
		const FLinearColor SoulColor(0.55f, 0.95f, 1.f);
		int32 SoulBursts = 0;
		for (const FViewItem& V : ScratchVanished)
		{
			if (!D->Enemies.IsValidIndex(V.Type)) continue;
			const FSQEnemyDef& Def = D->Enemies[V.Type];
			FSplat S;
			S.Type = V.Type;
			S.Pos = V.Pos;
			S.Scale = Def.Scale * FMath::FRandRange(1.1f, 1.8f);
			S.Yaw = FMath::FRandRange(0.f, 360.f);
			S.Z = GroundAt(V.Pos);
			// Nov 10 2026: enemies leave nothing behind but their XP (Tuning DeathSplats 1 brings the clay splats back)
			if (D->Tune(TEXT("DeathSplats"), 0.f) >= 0.5f)
			{
				if (Splats.Num() < MaxSplats) Splats.Add(S);
				else { Splats[SplatHead] = S; SplatHead = (SplatHead + 1) % MaxSplats; }
				bSplatsDirty = true;
			}
			const bool bLaunched = LaunchIds.Remove(V.Id) > 0;
			SpawnDeathPuppet(V, bLaunched);

			// Soul burst: a few glowing spheres rise out of the body
			if (SoulBursts < 20 && FVector2D::DistSquared(V.Pos, SkinViewCenter) <= BurstRadiusSq)
			{
				const bool bArt = SkinTypes.IsValidIndex(V.Type) && SkinTypes[V.Type].Mesh != nullptr;
				const float BodyZ = S.Z + (bArt ? SkinTypes[V.Type].ArtHeight * 0.45f : 50.f * Def.Scale);
				SQFx::Burst(World, FVector(V.Pos.X, V.Pos.Y, BodyZ), SoulColor, 5, 170.f, 1.0f, 8.f, 380.f);
				SoulBursts++;
			}
		}
	}

	if (!bSplatsDirty) return;
	bSplatsDirty = false;
	ScratchSplatPerType.SetNum(SplatISMs.Num());
	for (TArray<FTransform>& TypeXforms : ScratchSplatPerType) TypeXforms.Reset();
	for (const FSplat& S : Splats)
	{
		if (!ScratchSplatPerType.IsValidIndex(S.Type)) continue;
		ScratchSplatPerType[S.Type].Add(FTransform(FRotator(0.f, S.Yaw, 0.f), FVector(S.Pos.X, S.Pos.Y, S.Z + 2.f), FVector(S.Scale, S.Scale * 0.75f, 0.02f)));
	}
	for (int32 i = 0; i < SplatISMs.Num(); ++i)
	{
		UpdateISM(SplatISMs[i], ScratchSplatPerType[i]);
	}
}

// ================================================================ enemy personality (all machines, cosmetic)

void ASQHordeManager::CarryViewState(float Now, float DeltaSeconds)
{
	const USQData* D = USQData::Get(this);
	// A late joiner sees a full horde at once: none of it is "just spawned"
	const bool bFreshView = !bViewPrimed;
	if (ViewEnemies.Num() > 0) bViewPrimed = true;
	const float TurnAlpha = FMath::Min(1.f, DeltaSeconds * 8.f);
	const float SpeedAlpha = FMath::Min(1.f, DeltaSeconds * 6.f);
	const float InvDt = DeltaSeconds > UE_KINDA_SMALL_NUMBER ? 1.f / DeltaSeconds : 0.f;
	for (FViewItem& V : ViewEnemies)
	{
		const FViewItem* Prev = LastEnemyView.Find(V.Id);
		const FSQEnemyDef* FlyDef = (D && D->Enemies.IsValidIndex(V.Type) && D->Enemies[V.Type].IsFlyer()) ? &D->Enemies[V.Type] : nullptr;
		if (!Prev)
		{
			V.Born = bFreshView ? Now - 100.f : Now;
			V.StateAt = V.Born;
			V.Yaw = static_cast<float>((V.Id * 37) % 360);
			V.MoveAmt = 0.f;
			V.WalkPhase = IdHash01(V.Id, 9u); // the crowd doesn't step in lockstep
			V.SlopePitch = 0.f;
			if (FlyDef)
			{
				V.FlyGround = GroundAt(V.Pos);
				V.FlyZ = FlyTargetZ(*FlyDef, V, Now);
			}
			continue;
		}
		V.Born = Prev->Born;
		V.StateAt = SQEnemyPoseState(Prev->State) == SQEnemyPoseState(V.State) ? Prev->StateAt : Now;
		V.WalkPhase = Prev->WalkPhase;
		V.SlopePitch = Prev->SlopePitch;
		if (FlyDef)
		{
			// Altitude from type + state (no replicated bytes): a fast dive, a slow climb, a gentle cruise
			const ESQEnemyAnimState FlySt = SQEnemyAnim(V.State);
			const float FlyRate = FlySt == ESQEnemyAnimState::Charge ? 9.f : (FlySt == ESQEnemyAnimState::Recover ? 2.2f : 4.f);
			V.FlyGround = FMath::FInterpTo(Prev->FlyGround, GroundAt(V.Pos), DeltaSeconds, 3.f);
			V.FlyZ = FMath::FInterpTo(Prev->FlyZ, FlyTargetZ(*FlyDef, V, Now), DeltaSeconds, FlyRate);
		}
		const FVector2D Moved = V.Pos - Prev->Pos;
		const float TypeSpeed = (D && D->Enemies.IsValidIndex(V.Type)) ? FMath::Max(1.f, D->Enemies[V.Type].Speed) : 200.f;
		const float Observed = static_cast<float>(Moved.Size()) * InvDt;
		V.MoveAmt = FMath::Lerp(Prev->MoveAmt, Observed / TypeSpeed, SpeedAlpha);
		V.Yaw = Prev->Yaw;
		if (Moved.SizeSquared() > 0.25)
		{
			const float MoveYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Moved.Y), static_cast<float>(Moved.X)));
			V.Yaw = FMath::UnwindDegrees(V.Yaw + FMath::FindDeltaAngleDegrees(V.Yaw, MoveYaw) * TurnAlpha);
		}
	}
}

void ASQHordeManager::RenderEnemyISMs(float Now, float DeltaSeconds)
{
	const USQData* D = USQData::Get(this);
	if (!D) return;
	EnsureImpostorComponents();
	const float Stepped = FMath::FloorToFloat(Now * 12.f) / 12.f; // stop-motion stepping (clay)
	ScratchPerType.SetNum(EnemyISMs.Num());
	for (TArray<FTransform>& TypeXforms : ScratchPerType) TypeXforms.Reset();
	ScratchPropPerType.SetNum(PropISMs.Num());
	for (TArray<FTransform>& PropXforms : ScratchPropPerType) PropXforms.Reset();
	ScratchImpostor.SetNum(ImpostorISMs.Num());
	for (TArray<FTransform>& FrameXforms : ScratchImpostor) FrameXforms.Reset();
	const FName HipsBone(TEXT("hips"));
	const FName ChestBone(TEXT("chest"));
	const FQuat ArtQuat(FRotator(0.f, SQArt::YawOffset(this), 0.f));

	// Where each skinned walk leader is in its loop this frame (the impostors near the ring lock onto it: a seamless hand-off)
	const int32 NumTypes = D->Enemies.Num();
	ScratchLeaderPhase.Init(-1.f, NumTypes * 3);
	for (int32 TypeIdx = 0; TypeIdx < NumTypes && TypeIdx < SkinTypes.Num(); ++TypeIdx)
	{
		const FSkinType& LeadInfo = SkinTypes[TypeIdx];
		if (!LeadInfo.Mesh) continue;
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			const int32 LeaderIdx = LeadInfo.Leaders[Variant];
			if (!SkinLeaders.IsValidIndex(LeaderIdx) || !SkinLeaders[LeaderIdx]) continue;
			ScratchLeaderPhase[TypeIdx * 3 + Variant] = FMath::Frac(SkinLeaders[LeaderIdx]->GetPosition() / FMath::Max(0.05f, LeadInfo.MoveLen));
		}
	}

	for (int32 ViewIdx = 0; ViewIdx < ViewEnemies.Num(); ++ViewIdx)
	{
		if (ScratchSkinnedMask.IsValidIndex(ViewIdx) && ScratchSkinnedMask[ViewIdx]) continue;
		FViewItem& V = ViewEnemies[ViewIdx];
		if (!ScratchPerType.IsValidIndex(V.Type) || !D->Enemies.IsValidIndex(V.Type)) continue;
		const FSQEnemyDef& Def = D->Enemies[V.Type];
		// The character itself (flipbook impostor), whenever its art exists; the primitive shape is the last fallback
		if (ImpostorSets.IsValidIndex(V.Type) && ImpostorSets[V.Type].NumValid > 0 && ImpostorISMs.IsValidIndex(V.Type * ImpostorFrames)
			&& ImpostorISMs[V.Type * ImpostorFrames])
		{
			AddImpostorInstance(V, Def, ImpostorSets[V.Type], Now, DeltaSeconds, ArtQuat);
			continue;
		}
		const ESQEnemyAnimState AnimSt = SQEnemyAnim(V.State);
		const float Phase = IdHash01(V.Id, 5u) * 2.f * PI;
		const bool bFrozen = AnimSt == ESQEnemyAnimState::Frozen;
		const FSQGaitPose G = SQGaitPose(Def.Gait, bFrozen ? 0.f : Stepped + Phase * 0.05f, Phase, bFrozen ? 0.f : V.MoveAmt, false);
		float BodyYaw = V.Yaw + G.YawJitter;
		float BodyPitch = G.Pitch;
		float BodyRoll = G.Roll;
		float Swell = 1.f;
		float SquashXY = G.SquashXY;
		float SquashZ = G.SquashZ;
		float Lunge = 0.f;
		const float StateAge = Now - V.StateAt;
		const bool bFlyerBody = Def.IsFlyer();
		const bool bPhaserBody = Def.Fly == ESQEnemyFly::Phase;
		switch (AnimSt)
		{
		case ESQEnemyAnimState::Windup:
		{
			if (bPhaserBody)
			{
				// Wraith phasing out: thins to a sliver and stretches up before it blinks
				const float FadeK = FMath::Clamp(StateAge / FMath::Max(0.2f, Def.Windup), 0.f, 1.f);
				SquashXY *= FMath::Lerp(1.f, 0.25f, FadeK);
				SquashZ *= 1.f + 0.3f * FadeK;
				BodyYaw += 40.f * FadeK * FMath::Sin(Now * 30.f);
				break;
			}
			// Brotato-style pulse: swells and shakes faster as the hit gets close
			const float K = FMath::Clamp(StateAge / FMath::Max(0.05f, Def.Windup), 0.f, 1.f);
			Swell = 1.f + 0.18f * K * (0.5f + 0.5f * FMath::Sin(Now * (20.f + 30.f * K)));
			SquashZ *= 1.f + 0.1f * K;
			BodyPitch = -14.f * K;
			BodyYaw += 6.f * FMath::Sin(Now * 45.f) * K;
			break;
		}
		case ESQEnemyAnimState::Strike:
			if (bPhaserBody)
			{
				// Wraith phasing back in: pops from a sliver to slightly too big, then settles
				const float InK = FMath::Clamp(StateAge / 0.35f, 0.f, 1.f);
				SquashXY *= InK < 0.6f ? FMath::Lerp(0.25f, 1.15f, InK / 0.6f) : FMath::Lerp(1.15f, 1.f, (InK - 0.6f) / 0.4f);
				break;
			}
			Lunge = 30.f * (1.f - FMath::Clamp(StateAge / 0.3f, 0.f, 1.f));
			BodyPitch = 18.f;
			break;
		case ESQEnemyAnimState::Charge:
			BodyPitch = bFlyerBody ? 45.f : 28.f; // a Gravebat dives nose-first
			SquashXY *= 0.9f;
			SquashZ *= 1.06f;
			break;
		case ESQEnemyAnimState::Recover:
			if (bFlyerBody)
			{
				BodyPitch = -20.f; // climbing back up
				break;
			}
			BodyRoll = 18.f * FMath::Sin(Now * 9.f);
			BodyYaw += 25.f * FMath::Sin(Now * 4.f);
			BodyPitch = -8.f;
			break;
		default:
			break;
		}

		// Spawn styles (the server keeps it inert for exactly this long)
		const ESQEnemySpawn SpawnStyle = SQEnemySpawnStyle(Def, V.State);
		const float SpawnT = SQEnemySpawnTime(SpawnStyle);
		const float Age = Now - V.Born;
		float SpawnZ = 0.f;
		float SpawnScale = 1.f;
		const float BodyH = 100.f * Def.Scale * Def.Stretch.Y;
		if (Age < SpawnT)
		{
			const float P = FMath::Clamp(Age / SpawnT, 0.f, 1.f);
			switch (SpawnStyle)
			{
			case ESQEnemySpawn::Crawl: SpawnZ = -BodyH * (1.f - P); BodyPitch += 60.f * (1.f - P); break;
			case ESQEnemySpawn::Drop: SpawnZ = 700.f * (1.f - P * P); SquashZ *= 1.f + 0.25f * (1.f - P); break;
			case ESQEnemySpawn::Burst: SpawnScale = P < 0.7f ? 1.25f * P / 0.7f : 1.25f - 0.25f * (P - 0.7f) / 0.3f; break;
			case ESQEnemySpawn::Fade: SpawnScale = P; break;
			default: SpawnZ = -BodyH * (1.f - FMath::InterpEaseOut(0.f, 1.f, P, 2.f)); break;
			}
		}
		else if (SpawnStyle != ESQEnemySpawn::Fade)
		{
			// Spawn pop: stretch up out of the ground / landing, then a squash, then settle
			const float PopZ = SQSpawnPopZ(Age - SpawnT);
			SquashZ *= PopZ;
			SquashXY *= 1.f - 0.5f * (PopZ - 1.f);
		}

		const float YawRad = FMath::DegreesToRadians(BodyYaw);
		const FVector2D Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad));
		const FVector2D Right(-Fwd.Y, Fwd.X);
		const FVector2D BodyPos = V.Pos + Fwd * Lunge;
		const float Ground = bFlyerBody ? SQFlyerBaseZ(V.FlyGround, V.FlyZ, GroundAt(V.Pos)) : GroundAt(V.Pos);
		const FRotator BodyRot(BodyPitch, BodyYaw, BodyRoll);
		const bool bPropBody = TypePropIsBody.IsValidIndex(V.Type) && TypePropIsBody[V.Type];
		if (bPropBody)
		{
			const float Fit = TypePropFit[V.Type] * SpawnScale * Swell;
			const FVector Loc(BodyPos.X, BodyPos.Y, Ground + G.Bob + SpawnZ + Def.PropOffset.Z);
			ScratchPerType[V.Type].Add(FTransform(BodyRot + Def.PropRot, Loc, FVector(Fit * SquashXY, Fit * SquashXY, Fit * SquashZ)));
			continue;
		}

		const float S = Def.Scale * SpawnScale * Swell;
		const FVector Scale3(S * Def.Stretch.X * SquashXY, S * Def.Stretch.X * SquashXY, S * Def.Stretch.Y * SquashZ);
		const FVector Loc(BodyPos.X, BodyPos.Y, Ground + 50.f * Scale3.Z + G.Bob + SpawnZ);
		ScratchPerType[V.Type].Add(FTransform(BodyRot, Loc, Scale3));

		// Tower gag: smaller copies stacked on top, swaying more the higher they are
		float TopZ = Loc.Z + 50.f * Scale3.Z;
		for (int32 Rider = 1; Rider <= Def.Stack; ++Rider)
		{
			const float RiderScale = FMath::Pow(0.85f, static_cast<float>(Rider));
			const FVector RiderScale3 = Scale3 * RiderScale;
			const float RiderSway = 6.f * Rider * FMath::Sin(Stepped * 2.5f + Phase + Rider);
			const FVector RiderLoc(BodyPos.X + Right.X * RiderSway, BodyPos.Y + Right.Y * RiderSway, TopZ + 50.f * RiderScale3.Z);
			ScratchPerType[V.Type].Add(FTransform(FRotator(BodyPitch, BodyYaw, BodyRoll + RiderSway * 1.5f), RiderLoc, RiderScale3));
			TopZ = RiderLoc.Z + 50.f * RiderScale3.Z;
		}

		// Far prop: same accessory, placed by bone class (head on top, hands at the side, hips / chest in the middle)
		if (PropISMs.IsValidIndex(V.Type) && PropISMs[V.Type] && TypePropFit.IsValidIndex(V.Type))
		{
			FVector PropLoc;
			if (SQIsHeadBone(Def.PropBone))
			{
				PropLoc = FVector(BodyPos.X, BodyPos.Y, TopZ + Def.PropOffset.Z * 0.3f);
			}
			else if (Def.PropBone == HipsBone || Def.PropBone == ChestBone)
			{
				PropLoc = FVector(BodyPos.X, BodyPos.Y, Loc.Z - 50.f * Scale3.Z + Def.PropSize * Def.Scale * 0.5f);
			}
			else
			{
				const float Side = Def.PropBone.ToString().EndsWith(TEXT("l")) ? -1.f : 1.f;
				const float SideDist = 55.f * Scale3.X;
				PropLoc = FVector(BodyPos.X + Right.X * SideDist * Side, BodyPos.Y + Right.Y * SideDist * Side, Loc.Z - 10.f * Scale3.Z);
			}
			const float PropFit = TypePropFit[V.Type] * SpawnScale;
			ScratchPropPerType[V.Type].Add(FTransform(BodyRot + Def.PropRot, PropLoc, FVector(PropFit)));
		}
	}
	for (int32 i = 0; i < EnemyISMs.Num(); ++i)
	{
		UpdateISM(EnemyISMs[i], ScratchPerType[i]);
	}
	for (int32 i = 0; i < PropISMs.Num(); ++i)
	{
		if (PropISMs[i]) UpdateISM(PropISMs[i], ScratchPropPerType[i]);
	}
	for (int32 i = 0; i < ImpostorISMs.Num() && i < ScratchImpostor.Num() && i < ImpostorCapacity.Num(); ++i)
	{
		if (ImpostorISMs[i]) UpdateISMPadded(ImpostorISMs[i], ScratchImpostor[i], ImpostorCapacity[i]);
	}
}

void ASQHordeManager::RenderTelegraphs(float Now, float DeltaSeconds)
{
	const USQData* D = USQData::Get(this);
	UWorld* World = GetWorld();
	if (!D || !World) return;
	ScratchTeleOuter.Reset();
	ScratchTeleFill.Reset();
	const float NearSq = FMath::Square(SkinRadius * 1.3f);
	const float WindupSlack = D->Tune(TEXT("EnemyWindupSlack"), 40.f);
	const float ChargeLen = D->Tune(TEXT("EnemyChargeSpeedMult"), 3.f) * D->Tune(TEXT("EnemyChargeTime"), 0.6f);
	const float BomberRadius = D->Tune(TEXT("BomberRadius"), 240.f);
	int32 FxBudget = 10;
	int32 Telegraphs = 0;
	for (const FViewItem& V : ViewEnemies)
	{
		if (!D->Enemies.IsValidIndex(V.Type)) continue;
		if (FVector2D::DistSquared(V.Pos, SkinViewCenter) > NearSq) continue;
		const FSQEnemyDef& Def = D->Enemies[V.Type];
		const ESQEnemyAnimState AnimSt = SQEnemyAnim(V.State);
		const float Ground = GroundAt(V.Pos);

		// Spawn effects (dust, dirt, landing thud, pop)
		const float Age = Now - V.Born;
		if (Age < SQEnemySpawnTime(SQEnemySpawnStyle(Def, V.State)) + DeltaSeconds) EmitSpawnFx(V, Age, DeltaSeconds, FxBudget);

		// Elites smoulder: the odd ember rising off them
		if (Def.Behaviour == ESQEnemyBehaviour::Elite && FxBudget > 0 && FMath::FRand() < DeltaSeconds * 4.f)
		{
			SQFx::Burst(World, FVector(V.Pos.X, V.Pos.Y, Ground + 120.f * Def.Scale * FMath::FRand()), Def.Accent, 1, 60.f, 0.8f, 7.f, 250.f);
			FxBudget--;
		}
		// Dizzy chargers: little stars over the head (a climbing Gravebat isn't dizzy)
		if (AnimSt == ESQEnemyAnimState::Recover && !Def.IsFlyer() && FxBudget > 0 && FMath::FRand() < DeltaSeconds * 6.f)
		{
			SQFx::Ring(World, FVector(V.Pos.X, V.Pos.Y, Ground + 170.f * Def.Scale), FLinearColor(1.f, 0.95f, 0.4f), 3, 60.f, 0.4f, 6.f);
			FxBudget--;
		}
		if (AnimSt != ESQEnemyAnimState::Windup || Telegraphs >= 80) continue;
		Telegraphs++;

		const float WindupDur = (Def.Gait == ESQEnemyGait::Charge || Def.Fly == ESQEnemyFly::Swoop) ? FMath::Max(0.3f, Def.Windup) : FMath::Max(0.05f, Def.Windup);
		const float K = FMath::Clamp((Now - V.StateAt) / WindupDur, 0.f, 1.f);
		// The flash: a spark pop the moment it starts winding up (a Wraith gives off a cold wisp as it starts to phase)
		const bool bPhasing = Def.Fly == ESQEnemyFly::Phase;
		const float FlashZ = Def.IsFlyer() ? SQFlyerBaseZ(V.FlyGround, V.FlyZ, Ground) + 40.f * Def.Scale : Ground + 110.f * Def.Scale;
		if (Now - V.StateAt < DeltaSeconds * 1.5f && FxBudget > 0)
		{
			SQFx::Burst(World, FVector(V.Pos.X, V.Pos.Y, FlashZ), bPhasing ? Def.Accent : FLinearColor(1.f, 0.35f, 0.15f), bPhasing ? 8 : 4, 220.f, bPhasing ? 0.5f : 0.25f, 6.f, bPhasing ? 200.f : -300.f);
			FxBudget--;
		}
		if (Def.Behaviour == ESQEnemyBehaviour::Spitter || bPhasing) continue; // spitter: the cast + glow is the tell; Wraith: phasing, not attacking

		FVector2D PlayerPos = FVector2D::ZeroVector;
		float PlayerDistSq = 0.f;
		const bool bHasPlayer = NearestPlayer(V.Pos, PlayerPos, PlayerDistSq);
		// Collision pass: every telegraph is drawn at the real hit distance. A hit lands when the centre distance <= EnemyRadius +
		// reaper radius + Reach (+ WindupSlack), i.e. when the reaper's body overlaps a disc of EnemyRadius + Reach + WindupSlack.
		const float TeleReach = EnemyRadius(V.Type) + (TypeReach.IsValidIndex(V.Type) ? TypeReach[V.Type] : Def.Reach);
		if (Def.Fly == ESQEnemyFly::Swoop)
		{
			// Gravebat swoop: a red target marker on the reaper it is about to dive at (its shadow is the other half of the tell);
			// the dive hits a reaper whose body overlaps this circle as the bat passes (Charge: attack reach + 20)
			if (!bHasPlayer) continue;
			const float MarkR = TeleReach + 20.f;
			const float MarkZ = GroundAt(PlayerPos);
			ScratchTeleOuter.Add(FTransform(FRotator::ZeroRotator, FVector(PlayerPos.X, PlayerPos.Y, MarkZ + 2.f), FVector(MarkR / 50.f, MarkR / 50.f, 0.015f)));
			const float MarkFill = FMath::Max(1.f, MarkR * K);
			ScratchTeleFill.Add(FTransform(FRotator::ZeroRotator, FVector(PlayerPos.X, PlayerPos.Y, MarkZ + 3.f), FVector(MarkFill / 50.f, MarkFill / 50.f, 0.015f)));
			continue;
		}
		if (Def.Gait == ESQEnemyGait::Charge && bHasPlayer && PlayerDistSq > FMath::Square(TeleReach + 40.f))
		{
			// Charge lane: a row of discs toward the reaper it locked onto, filling in as the dash gets close (lane half-width =
			// the dash's hit distance minus the reaper's radius: attack reach + 20)
			const FVector2D LaneDir = (PlayerPos - V.Pos).GetSafeNormal();
			const float LaneLen = Def.Speed * ChargeLen;
			const float DiscR = TeleReach + 20.f;
			const int32 Discs = FMath::Clamp(FMath::CeilToInt(LaneLen / (DiscR * 1.6f)), 1, 12);
			for (int32 DiscIdx = 0; DiscIdx < Discs; ++DiscIdx)
			{
				const FVector2D At = V.Pos + LaneDir * (DiscR + DiscIdx * DiscR * 1.6f);
				const float AtZ = GroundAt(At);
				ScratchTeleOuter.Add(FTransform(FRotator::ZeroRotator, FVector(At.X, At.Y, AtZ + 2.f), FVector(DiscR / 50.f, DiscR / 50.f, 0.015f)));
				if (static_cast<float>(DiscIdx) < K * Discs)
				{
					ScratchTeleFill.Add(FTransform(FRotator::ZeroRotator, FVector(At.X, At.Y, AtZ + 3.f), FVector(DiscR * 0.8f / 50.f, DiscR * 0.8f / 50.f, 0.015f)));
				}
			}
			continue;
		}
		// (a Bomber's blast reaches any reaper whose centre is within BomberRadius)
		const float R = Def.Behaviour == ESQEnemyBehaviour::Bomber ? BomberRadius : TeleReach + WindupSlack;
		ScratchTeleOuter.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Ground + 2.f), FVector(R / 50.f, R / 50.f, 0.015f)));
		const float FillR = FMath::Max(1.f, R * K);
		ScratchTeleFill.Add(FTransform(FRotator::ZeroRotator, FVector(V.Pos.X, V.Pos.Y, Ground + 3.f), FVector(FillR / 50.f, FillR / 50.f, 0.015f)));
	}
	UpdateISM(TeleOuterISM, ScratchTeleOuter);
	UpdateISM(TeleFillISM, ScratchTeleFill);
}

void ASQHordeManager::EmitSpawnFx(const FViewItem& V, float Age, float DeltaSeconds, int32& Budget)
{
	const USQData* D = USQData::Get(this);
	UWorld* World = GetWorld();
	if (Budget <= 0 || !D || !World || !D->Enemies.IsValidIndex(V.Type)) return;
	const FSQEnemyDef& Def = D->Enemies[V.Type];
	const ESQEnemySpawn SpawnStyle = SQEnemySpawnStyle(Def, V.State);
	const float SpawnT = SQEnemySpawnTime(SpawnStyle);
	const bool bStart = Age < DeltaSeconds;
	const bool bLand = Age >= SpawnT && Age - DeltaSeconds < SpawnT;
	const bool bMid = Age >= SpawnT * 0.5f && Age - DeltaSeconds < SpawnT * 0.5f;
	const FVector Base(V.Pos.X, V.Pos.Y, GroundAt(V.Pos) + 5.f);
	const FLinearColor Dust(0.42f, 0.36f, 0.3f);
	const FLinearColor Dirt(0.3f, 0.22f, 0.15f);
	switch (SpawnStyle)
	{
	case ESQEnemySpawn::Rise:
		if (bStart) { SQFx::Ring(World, Base, Dust, 10, 260.f, 0.45f, 7.f); Budget--; }
		break;
	case ESQEnemySpawn::Crawl:
		if (bStart) { SQFx::Burst(World, Base, Dirt, 8, 260.f, 0.6f, 9.f); Budget--; }
		else if (bMid) { SQFx::Ring(World, Base, Dust, 8, 200.f, 0.4f, 7.f); Budget--; }
		break;
	case ESQEnemySpawn::Drop:
		// a shadow ring marks where it will land, then a thud
		if (bStart) { SQFx::Ring(World, Base, FLinearColor(0.1f, 0.08f, 0.1f), 8, 40.f, SpawnT, 8.f); Budget--; }
		else if (bLand && !Def.IsFlyer()) { SQFx::Ring(World, Base, Dust, 14, 380.f, 0.5f, 9.f); SQFx::Burst(World, Base, Dust, 6, 250.f, 0.5f, 8.f); Budget--; }
		break;
	case ESQEnemySpawn::Burst:
		if (bStart) { SQFx::Burst(World, Base + FVector(0.f, 0.f, 50.f * Def.Scale), Def.Accent, 10, 380.f, 0.4f, 7.f); Budget--; }
		break;
	case ESQEnemySpawn::Fade:
		if (bStart) { SQFx::Burst(World, Base + FVector(0.f, 0.f, 40.f), Def.Accent, 8, 120.f, 0.8f, 7.f, 300.f); Budget--; }
		break;
	default:
		break;
	}
}

UInstancedStaticMeshComponent* ASQHordeManager::DebrisISMFor(UStaticMesh* Mesh, UMaterialInstanceDynamic* MID)
{
	if (!Mesh || !Root) return nullptr;
	for (int32 i = 0; i < DebrisISMs.Num() && i < DebrisMeshes.Num(); ++i)
	{
		if (DebrisMeshes[i] == Mesh && DebrisISMs[i] && (!MID || DebrisISMs[i]->GetMaterial(0) == MID)) return DebrisISMs[i];
	}
	UInstancedStaticMeshComponent* PieceISM = NewObject<UInstancedStaticMeshComponent>(this);
	PieceISM->SetStaticMesh(Mesh);
	if (MID) PieceISM->SetMaterial(0, MID);
	PieceISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PieceISM->SetCastShadow(false);
	PieceISM->SetupAttachment(Root);
	PieceISM->RegisterComponent();
	DebrisISMs.Add(PieceISM);
	DebrisMeshes.Add(Mesh);
	return PieceISM;
}

void ASQHordeManager::SpawnDebris(const FVector& At, const FVector& Away, UStaticMesh* Mesh, UMaterialInstanceDynamic* MID, float Size, int32 Count, float Power)
{
	UInstancedStaticMeshComponent* PieceISM = DebrisISMFor(Mesh, MID);
	if (!PieceISM || Count <= 0) return;
	int32 PieceIdx = INDEX_NONE;
	for (int32 i = 0; i < DebrisISMs.Num(); ++i)
	{
		if (DebrisISMs[i].Get() == PieceISM) { PieceIdx = i; break; }
	}
	if (PieceIdx == INDEX_NONE) return;
	if (Debris.Num() < MaxDebris) Debris.SetNum(MaxDebris);
	const float Fit = SQArt::FitLongest(Mesh, Size);
	for (int32 c = 0; c < Count; ++c)
	{
		FDebris& Piece = Debris[DebrisHead];
		DebrisHead = (DebrisHead + 1) % MaxDebris;
		Piece.bActive = true;
		Piece.Age = 0.f;
		Piece.Life = FMath::FRandRange(1.3f, 1.9f);
		Piece.ISMIdx = PieceIdx;
		Piece.Scale = Fit * FMath::FRandRange(0.85f, 1.15f);
		Piece.Pos = At + FVector(FMath::FRandRange(-15.f, 15.f), FMath::FRandRange(-15.f, 15.f), FMath::FRandRange(-10.f, 20.f));
		const FVector2D Scatter = FMath::RandPointInCircle(1.f);
		Piece.Vel = Away * Power * FMath::FRandRange(0.4f, 1.f) + FVector(Scatter.X, Scatter.Y, 0.f) * Power * 0.6f
			+ FVector(0.f, 0.f, FMath::FRandRange(0.8f, 1.4f) * Power);
		Piece.Rot = FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f));
		Piece.Spin = FRotator(FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-360.f, 360.f), FMath::FRandRange(-720.f, 720.f));
	}
}

void ASQHordeManager::TickDebris(float DeltaSeconds)
{
	if (DebrisISMs.Num() == 0) return;
	ScratchDebris.SetNum(DebrisISMs.Num());
	for (TArray<FTransform>& PieceXforms : ScratchDebris) PieceXforms.Reset();
	for (FDebris& Piece : Debris)
	{
		if (!Piece.bActive) continue;
		Piece.Age += DeltaSeconds;
		if (Piece.Age >= Piece.Life || !ScratchDebris.IsValidIndex(Piece.ISMIdx)) { Piece.bActive = false; continue; }
		Piece.Vel.Z += DebrisGravity * DeltaSeconds;
		Piece.Pos += Piece.Vel * DeltaSeconds;
		Piece.Rot += Piece.Spin * DeltaSeconds;
		const float Floor = GroundAt(FVector2D(Piece.Pos.X, Piece.Pos.Y)) + 4.f;
		if (Piece.Pos.Z < Floor)
		{
			Piece.Pos.Z = Floor;
			if (Piece.Vel.Z < 0.f)
			{
				Piece.Vel.Z *= -0.35f;
				Piece.Vel.X *= 0.55f;
				Piece.Vel.Y *= 0.55f;
				Piece.Spin *= 0.5f;
			}
		}
		const float Fade = FMath::Clamp((Piece.Life - Piece.Age) / 0.35f, 0.f, 1.f);
		ScratchDebris[Piece.ISMIdx].Add(FTransform(Piece.Rot, Piece.Pos, FVector(Piece.Scale * Fade)));
	}
	for (int32 i = 0; i < DebrisISMs.Num(); ++i)
	{
		UpdateISM(DebrisISMs[i], ScratchDebris[i]);
	}
}

// ================================================================ skinned horde (imported art)

int32 ASQHordeManager::FindOrMakeLeader(USkeletalMesh* ArtMesh, FName Model, FName AnimName, UAnimSequence* AnimSeq, int32 Variant, float Rate)
{
	for (int32 k = 0; k < SkinLeaderKeys.Num(); ++k)
	{
		const FSkinLeaderKey& Key = SkinLeaderKeys[k];
		if (Key.Model == Model && Key.Anim == AnimName && Key.Variant == Variant && FMath::IsNearlyEqual(Key.Rate, Rate)) return k;
	}
	if (!ArtMesh || !AnimSeq || !Root) return INDEX_NONE;

	USkeletalMeshComponent* Leader = NewObject<USkeletalMeshComponent>(this);
	Leader->SetSkeletalMeshAsset(ArtMesh);
	Leader->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Leader->SetCastShadow(false);
	Leader->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Leader->SetVisibility(false);
	Leader->SetupAttachment(Root);
	Leader->RegisterComponent();
	SQArt::Play(Leader, AnimSeq, true, Rate * (0.9f + 0.1f * Variant));
	Leader->SetPosition(static_cast<float>(AnimSeq->GetPlayLength() * Variant / 3.f), false);

	FSkinLeaderKey NewKey;
	NewKey.Model = Model;
	NewKey.Anim = AnimName;
	NewKey.Variant = Variant;
	NewKey.Rate = Rate;
	SkinLeaderKeys.Add(NewKey);
	return SkinLeaders.Add(Leader);
}

void ASQHordeManager::EnsureSkinnedComponents()
{
	if (bSkinInitDone) return;
	const USQData* D = USQData::Get(this);
	if (!D || GetNetMode() == NM_DedicatedServer || D->Enemies.Num() == 0 || !Root) return;
	bSkinInitDone = true;

	// Nov 7: 4200 / 220 (was 3200 / 160); past the ring or the pool, enemies draw as flipbook impostors of the same art
	SkinRadius = FMath::Max(500.f, D->Tune(TEXT("SkinnedEnemyRadius"), 4200.f));
	AttackAnimRange = D->Tune(TEXT("EnemyAttackAnimRange"), 180.f);
	HitFlashGlow = D->Tune(TEXT("EnemyHitFlash"), 3.f);
	OneShotCap = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("EnemyOneShotCap"), 24.f)), 0, 256);
	const int32 NumTypes = D->Enemies.Num();
	SkinTypes.SetNum(NumTypes);
	SkinMIDs.SetNum(NumTypes);

	// Spawn takes per style (Burst / Fade are procedural only)
	static const FName RiseTakes[] = { FName(TEXT("Spawn_Ground_Skeletons")), FName(TEXT("Spawn_Ground")) };
	static const FName CrawlTakes[] = { FName(TEXT("Skeletons_Awaken_Floor")), FName(TEXT("Lie_StandUp")) };
	static const FName DropTakes[] = { FName(TEXT("Spawn_Air")), FName(TEXT("Jump_Land")) };

	for (int32 TypeIdx = 0; TypeIdx < NumTypes; ++TypeIdx)
	{
		const FSQEnemyDef& Def = D->Enemies[TypeIdx];
		USkeletalMesh* ArtMesh = SQArt::Character(Def.Model);
		FName MoveName = NAME_None;
		UAnimSequence* MoveAnim = SQFirstAnim(Def.Model, Def.MoveAnims, &MoveName);
		if (!ArtMesh || !MoveAnim) continue; // no art: this type stays a primitive

		FSkinType& Info = SkinTypes[TypeIdx];
		Info.Mesh = ArtMesh;
		Info.DeathA = SQArt::Anim(Def.Model, FName(TEXT("Death_A")));
		Info.DeathB = SQArt::Anim(Def.Model, FName(TEXT("Death_B")));
		if (!Info.DeathA && !Info.DeathB) Info.DeathA = SQArt::Anim(Def.Model, FName(TEXT("Death"))); // own-rig models (Gravebat)
		Info.ArtHeight = Def.Height * Def.Scale * Def.Stretch.Y;
		Info.MeshScale = SQArt::FitScale(ArtMesh, Def.Height * Def.Scale);
		Info.Tint = SQArt::SoftTint(Def.Color, Def.TintStrength);
		Info.MoveRate = Def.AnimRate;
		Info.MoveLen = FMath::Max(0.05f, static_cast<float>(MoveAnim->GetPlayLength()));
		Info.BodyGlow = Def.Behaviour == ESQEnemyBehaviour::Elite ? Def.Glow : 0.f;
		// Own palette when the model has one (T_Zombie, T_Gravebat...), else the shared skeleton texture
		Info.Tex = SQArt::CharacterTexture(Def.Model);
		if (Info.Tex) SkinAssetRefs.Add(Info.Tex);
		SkinAssetRefs.Add(ArtMesh);
		SkinAssetRefs.Add(MoveAnim);
		if (Info.DeathA) SkinAssetRefs.Add(Info.DeathA);
		if (Info.DeathB) SkinAssetRefs.Add(Info.DeathB);
		SkinMIDs[TypeIdx] = SQArt::TintMID(this, Info.Tex, Info.Tint);
		if (SkinMIDs[TypeIdx] && Info.BodyGlow > 0.f) SkinMIDs[TypeIdx]->SetScalarParameterValue(TEXT("Glow"), Info.BodyGlow);

		// Hidden leaders drive the pose of every follower (3 phase variants so the crowd is not in lockstep)
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			Info.Leaders[Variant] = FindOrMakeLeader(ArtMesh, Def.Model, MoveName, MoveAnim, Variant, Def.AnimRate);
		}

		// Wind-up + strike: played per follower as a one-shot timed to the server's wind-up (leader loop = fallback)
		FName AttackName = NAME_None;
		Info.AttackSeq = SQFirstAnim(Def.Model, Def.AttackAnims, &AttackName);
		Info.AttackRange = 0.f;
		if (Info.AttackSeq)
		{
			SkinAssetRefs.Add(Info.AttackSeq);
			// (a reaper this close sees the swing loop: the real hit distance + a little lead-in)
			Info.AttackRange = Def.Behaviour == ESQEnemyBehaviour::Spitter ? (Def.Range > 0.f ? Def.Range : 900.f) : FMath::Max(AttackAnimRange, EnemyAttackReach(TypeIdx, ReaperBodyRadius(nullptr, D)) + 20.f);
			for (int32 Variant = 0; Variant < 3; ++Variant)
			{
				Info.AttackLeaders[Variant] = FindOrMakeLeader(ArtMesh, Def.Model, AttackName, Info.AttackSeq, Variant);
			}
		}
		Info.HitSeq = SQFirstAnim(Def.Model, Def.HitAnims);
		if (Info.HitSeq) SkinAssetRefs.Add(Info.HitSeq);
		// Own-rig flyers (Gravebat) have a "Spawn" take of their own
		if (Def.IsFlyer())
		{
			Info.SpawnSeq = SQArt::Anim(Def.Model, FName(TEXT("Spawn")));
			if (Info.SpawnSeq) SkinAssetRefs.Add(Info.SpawnSeq);
		}
		// Crawlers already crawl out on their own loop; everyone else plays the matching spawn take when it exists
		if (Def.Gait != ESQEnemyGait::Crawl && !Info.SpawnSeq)
		{
			const FName* Takes = nullptr;
			if (Def.Spawn == ESQEnemySpawn::Rise) Takes = RiseTakes;
			else if (Def.Spawn == ESQEnemySpawn::Crawl) Takes = CrawlTakes;
			else if (Def.Spawn == ESQEnemySpawn::Drop) Takes = DropTakes;
			for (int32 TakeIdx = 0; Takes && TakeIdx < 2 && !Info.SpawnSeq; ++TakeIdx) Info.SpawnSeq = SQArt::Anim(Def.Model, Takes[TakeIdx]);
			if (Info.SpawnSeq) SkinAssetRefs.Add(Info.SpawnSeq);
		}

		// Tower riders sit on the shoulders with their own pose
		if (Def.Stack > 0)
		{
			TArray<FName> RiderTakes = Def.StackAnims;
			RiderTakes.Add(FName(TEXT("Sit_Floor_Idle")));
			RiderTakes.Add(FName(TEXT("Cheer")));
			FName RiderName = NAME_None;
			UAnimSequence* RiderSeq = SQFirstAnim(Def.Model, RiderTakes, &RiderName);
			for (int32 Variant = 0; Variant < 3; ++Variant)
			{
				Info.StackLeaders[Variant] = RiderSeq ? FindOrMakeLeader(ArtMesh, Def.Model, RiderName, RiderSeq, Variant) : Info.Leaders[Variant];
			}
			if (RiderSeq) SkinAssetRefs.Add(RiderSeq);
		}

		// Prop accessory: placed in component space at the reference pose, then carried by the bone
		UStaticMesh* PropMesh = SQArt::Prop(Def.Prop);
		const bool bPropBody = TypePropIsBody.IsValidIndex(TypeIdx) && TypePropIsBody[TypeIdx];
		if (PropMesh && !bPropBody)
		{
			int32 BoneIndex = INDEX_NONE;
			const FName WantBone = Def.PropBone.IsNone() ? FName(TEXT("head")) : Def.PropBone;
			Info.PropSocket = SQResolveEnemyBone(ArtMesh, WantBone, BoneIndex);
			if (!Info.PropSocket.IsNone())
			{
				Info.PropMesh = PropMesh;
				SkinAssetRefs.Add(PropMesh);
				const float SafeMeshScale = FMath::Max(0.0001f, Info.MeshScale);
				const FTransform BoneCS = SQRefBoneComponentSpace(ArtMesh, BoneIndex);
				const float PropScale = SQArt::FitLongest(PropMesh, Def.PropSize * Def.Scale) / SafeMeshScale;
				// Component space of the KayKit mesh is the art's own frame (the follower adds the yaw offset)
				const FTransform Wanted(FQuat(Def.PropRot), BoneCS.GetLocation() + Def.PropOffset / SafeMeshScale, FVector(PropScale));
				Info.PropRel = Wanted.GetRelativeTransform(BoneCS);
				// Far impostors carry the same prop at the reference pose. KayKit rest poses hold the arms out (T-pose, arms along
				// the mesh X axis), so a hand prop is pulled in to hang beside the hip as it does mid-walk (approximate; far away).
				FTransform FarCS = Wanted;
				if (Info.PropSocket.ToString().Contains(TEXT("hand")))
				{
					const float RestH = FMath::Max(1.f, static_cast<float>(ArtMesh->GetImportedBounds().BoxExtent.Z) * 2.f);
					FVector HandLoc = FarCS.GetLocation();
					HandLoc.X = FMath::Sign(HandLoc.X) * FMath::Min(FMath::Abs(static_cast<float>(HandLoc.X)), 0.22f * RestH);
					HandLoc.Z = FMath::Min(static_cast<float>(HandLoc.Z), 0.45f * RestH);
					FarCS.SetLocation(HandLoc);
				}
				Info.FarPropCS = FarCS;
				Info.bFarProp = true;
			}
		}
		bAnySkinArt = true;
	}
	if (!bAnySkinArt) return;

	// Follower pool (copies the leader pose, so near-zero animation cost each). Each slot owns a MID for its hit flash
	// and a prop component for its accessory.
	const int32 PoolSize = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("SkinnedEnemyPool"), 220.f)), 0, 1024);
	SkinFollowers.Reserve(PoolSize);
	SlotMIDs.Reserve(PoolSize);
	SlotProps.Reserve(PoolSize);
	SkinSlots.SetNum(PoolSize);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		USkeletalMeshComponent* Follower = NewObject<USkeletalMeshComponent>(this);
		Follower->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Follower->SetCastShadow(true);
		Follower->SetVisibility(false);
		Follower->SetupAttachment(Root);
		Follower->RegisterComponent();
		SkinFollowers.Add(Follower);
		SlotMIDs.Add(SQArt::TintMID(this, SQArt::CharacterTexture(), FLinearColor::White));

		UStaticMeshComponent* SlotProp = NewObject<UStaticMeshComponent>(this);
		SlotProp->SetMobility(EComponentMobility::Movable);
		SlotProp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SlotProp->SetCastShadow(false);
		SlotProp->SetVisibility(false);
		SlotProp->SetupAttachment(Follower);
		SlotProp->RegisterComponent();
		SlotProps.Add(SlotProp);
	}

	// Tower riders (leader-pose copies stacked on a base follower)
	const int32 NumRiders = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("SkinnedRiderPool"), 24.f)), 0, 128);
	StackFollowers.Reserve(NumRiders);
	StackLeaderOf.Init(INDEX_NONE, NumRiders);
	StackTypeOf.Init(INDEX_NONE, NumRiders);
	for (int32 i = 0; i < NumRiders; ++i)
	{
		USkeletalMeshComponent* Rider = NewObject<USkeletalMeshComponent>(this);
		Rider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rider->SetCastShadow(true);
		Rider->SetVisibility(false);
		Rider->SetupAttachment(Root);
		Rider->RegisterComponent();
		StackFollowers.Add(Rider);
	}

	// Death puppets: a ring of one-shot death animations
	const int32 NumPuppets = 40;
	DeathPuppets.Reserve(NumPuppets);
	PuppetStates.SetNum(NumPuppets);
	for (int32 i = 0; i < NumPuppets; ++i)
	{
		USkeletalMeshComponent* Puppet = NewObject<USkeletalMeshComponent>(this);
		Puppet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Puppet->SetCastShadow(true);
		Puppet->SetVisibility(false);
		Puppet->SetupAttachment(Root);
		Puppet->RegisterComponent();
		DeathPuppets.Add(Puppet);
	}
}

FVector2D ASQHordeManager::LocalViewCenter() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) return FVector2D::ZeroVector;
	if (const APawn* ViewPawn = PC->GetPawn())
	{
		const FVector PawnLoc = ViewPawn->GetActorLocation();
		return FVector2D(PawnLoc.X, PawnLoc.Y);
	}
	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	return FVector2D(ViewLoc.X, ViewLoc.Y);
}

void ASQHordeManager::RenderSkinned(TArray<FViewItem>& EnemyView, float DeltaSeconds, TArray<bool>& OutSkinned)
{
	OutSkinned.Reset();
	FreedSkinYaw.Reset();
	SkinViewCenter = LocalViewCenter(); // also used by sparks / soul bursts when there is no art
	EnsureSkinnedComponents();
	if (!bAnySkinArt || SkinFollowers.Num() == 0) return;
	OutSkinned.Init(false, EnemyView.Num());
	const USQData* D = USQData::Get(this);
	if (!D) return;

	const int32 PoolSize = FMath::Min3(SkinFollowers.Num(), SkinSlots.Num(), SlotProps.Num());
	// Quality governor (SQPerf): fewer animated bodies near you while the frame time is over budget (cosmetic, per machine)
	const float RadiusSq = FMath::Square(SkinRadius * SQPerf::HordeScale());

	// Ends a follower's own one-shot and hands it back to a leader on the next pose
	auto EndOneShot = [this](USkeletalMeshComponent* OneShotComp, FSkinSlot& OneShotSlot)
	{
		if (!OneShotSlot.bOneShot) return;
		OneShotSlot.bOneShot = false;
		OneShotSlot.OneShotKind = 0;
		OneShotSlot.OneShotLeft = 0.f;
		OneShotSlot.LeaderIdx = INDEX_NONE;
		ActiveOneShots = FMath::Max(0, ActiveOneShots - 1);
		if (OneShotComp) OneShotComp->Stop();
	};
	// Plays Seq on this follower only (wind-up synced to the server, hit react, spawn). Budgeted by OneShotCap.
	auto StartOneShot = [this](USkeletalMeshComponent* OneShotComp, FSkinSlot& OneShotSlot, UAnimSequence* Seq, float Rate, uint8 Kind) -> bool
	{
		if (!OneShotComp || !Seq) return false;
		if (!OneShotSlot.bOneShot && ActiveOneShots >= OneShotCap) return false;
		if (!OneShotSlot.bOneShot) ActiveOneShots++;
		const float SafeRate = FMath::Clamp(Rate, 0.3f, 3.f);
		OneShotComp->SetLeaderPoseComponent(nullptr);
		OneShotComp->PlayAnimation(Seq, false);
		OneShotComp->SetPlayRate(SafeRate);
		OneShotSlot.bOneShot = true;
		OneShotSlot.OneShotKind = Kind;
		OneShotSlot.OneShotLeft = static_cast<float>(Seq->GetPlayLength()) / SafeRate;
		OneShotSlot.LeaderIdx = INDEX_NONE;
		return true;
	};

	// 1. Candidates within radius. Already-assigned enemies get a little hysteresis so slots do not thrash at the edge.
	TArray<TPair<float, int32>>& Candidates = ScratchCandidates;
	Candidates.Reset();
	for (int32 ViewIdx = 0; ViewIdx < EnemyView.Num(); ++ViewIdx)
	{
		const FViewItem& V = EnemyView[ViewIdx];
		if (!SkinTypes.IsValidIndex(V.Type) || !SkinTypes[V.Type].Mesh) continue;
		float DistSq = (float)FVector2D::DistSquared(V.Pos, SkinViewCenter);
		const bool bAssigned = SkinSlotOfId.Contains(V.Id);
		if (DistSq > (bAssigned ? RadiusSq * 1.21f : RadiusSq)) continue;
		if (bAssigned) DistSq *= 0.81f;
		Candidates.Emplace(DistSq, ViewIdx);
	}
	if (Candidates.Num() > PoolSize)
	{
		Candidates.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		Candidates.SetNum(PoolSize, EAllowShrinking::No);
	}

	// 2. Keep slots whose enemy is still wanted; queue new enemies
	TArray<int32>& SlotViewIdx = ScratchSlotView;
	SlotViewIdx.Init(INDEX_NONE, PoolSize);
	TArray<int32>& NewViewIdx = ScratchNewView;
	NewViewIdx.Reset();
	for (const TPair<float, int32>& Cand : Candidates)
	{
		const FViewItem& V = EnemyView[Cand.Value];
		const int32* SlotPtr = SkinSlotOfId.Find(V.Id);
		if (SlotPtr && SlotViewIdx.IsValidIndex(*SlotPtr) && SkinSlots[*SlotPtr].Type == (int32)V.Type)
		{
			SlotViewIdx[*SlotPtr] = Cand.Value;
		}
		else
		{
			NewViewIdx.Add(Cand.Value);
		}
	}

	// 3. Free slots whose enemy left the near set (or died); remember their facing for death puppets
	for (int32 s = 0; s < PoolSize; ++s)
	{
		FSkinSlot& Slot = SkinSlots[s];
		if (SlotViewIdx[s] != INDEX_NONE || Slot.EnemyId == INDEX_NONE) continue;
		const uint16 OldId = (uint16)Slot.EnemyId;
		const int32* MappedSlot = SkinSlotOfId.Find(OldId);
		if (MappedSlot && *MappedSlot == s) SkinSlotOfId.Remove(OldId);
		if (Slot.bHasPos) FreedSkinYaw.Add(OldId, Slot.Yaw);
		EndOneShot(SkinFollowers[s], Slot);
		Slot.EnemyId = INDEX_NONE;
		Slot.bHasPos = false;
	}

	// 4. Hand free slots to newcomers (their spawn age comes from the view: Born)
	UWorld* World = GetWorld();
	const float T = World ? World->GetTimeSeconds() : 0.f;
	int32 FreeCursor = 0;
	for (const int32 ViewIdx : NewViewIdx)
	{
		while (FreeCursor < PoolSize && (SlotViewIdx[FreeCursor] != INDEX_NONE || SkinSlots[FreeCursor].EnemyId != INDEX_NONE)) ++FreeCursor;
		if (FreeCursor >= PoolSize) break;
		const FViewItem& V = EnemyView[ViewIdx];
		FSkinSlot& Slot = SkinSlots[FreeCursor];
		Slot.EnemyId = V.Id;
		Slot.bHasPos = false;
		Slot.bAttacking = false;
		Slot.HitAge = 10.f;
		Slot.Pitch = V.SlopePitch; // the impostor's lean (seamless hand-off; the facing comes from V.Yaw below)
		Slot.MoveHold = 0.f;
		Slot.JoltDir = FVector2D::ZeroVector;
		Slot.SpawnAge = FMath::Min(T - V.Born, 10.f);
		Slot.LastState = 0xFF; // first pose counts as a state change
		SkinSlotOfId.Add(V.Id, FreeCursor);
		SlotViewIdx[FreeCursor] = ViewIdx;
		++FreeCursor;
	}

	// 5. Pose followers
	static const FName GlowParam(TEXT("Glow"));
	static const FName ColorParam(TEXT("Color"));
	static const FName TexParam(TEXT("Tex"));
	const FQuat ArtQuat(FRotator(0.f, SQArt::YawOffset(this), 0.f));
	const float TurnAlpha = FMath::Min(1.f, DeltaSeconds * 10.f);
	const float PitchAlpha = FMath::Min(1.f, DeltaSeconds * 6.f);
	const FLinearColor WarnTint(1.f, 0.3f, 0.15f);
	int32 RiderCursor = 0;
	for (int32 s = 0; s < PoolSize; ++s)
	{
		USkeletalMeshComponent* Follower = SkinFollowers[s];
		FSkinSlot& Slot = SkinSlots[s];
		if (!Follower) continue;
		const int32 ViewIdx = SlotViewIdx[s];
		if (ViewIdx == INDEX_NONE)
		{
			if (Slot.bShown) { Follower->SetVisibility(false, true); Slot.bShown = false; }
			continue;
		}

		const FViewItem& V = EnemyView[ViewIdx];
		const FSkinType& Info = SkinTypes[V.Type];
		const FSQEnemyDef& Def = D->Enemies[V.Type];
		const int32 Variant = V.Id % 3;
		UMaterialInstanceDynamic* SlotMID = SlotMIDs.IsValidIndex(s) ? SlotMIDs[s].Get() : nullptr;
		UStaticMeshComponent* SlotProp = SlotProps[s];

		if (Slot.Type != (int32)V.Type)
		{
			EndOneShot(Follower, Slot);
			Follower->SetSkeletalMeshAsset(Info.Mesh);
			if (SlotMID)
			{
				SlotMID->SetVectorParameterValue(ColorParam, Info.Tint);
				if (Info.Tex) SlotMID->SetTextureParameterValue(TexParam, Info.Tex);
				SQArt::SetAllMaterials(Follower, SlotMID);
			}
			else
			{
				SQArt::SetAllMaterials(Follower, SkinMIDs.IsValidIndex(V.Type) ? SkinMIDs[V.Type].Get() : nullptr);
			}
			ApplyTypeGlowSlots(Follower, V.Type); // eyes / ghost light glow (the far impostors do the same)
			Slot.CurWarm = 0.f;
			Slot.CurGlow = -1.f;
			// Prop accessory on its bone
			if (SlotProp)
			{
				SlotProp->SetStaticMesh(Info.PropMesh);
				if (Info.PropMesh)
				{
					SQArt::SetAllMaterials(SlotProp, PropMIDs.IsValidIndex(V.Type) ? PropMIDs[V.Type].Get() : nullptr);
					SlotProp->AttachToComponent(Follower, FAttachmentTransformRules::KeepRelativeTransform, Info.PropSocket);
					SlotProp->SetRelativeTransform(Info.PropRel);
				}
			}
			Slot.Type = V.Type;
			Slot.LeaderIdx = INDEX_NONE;
		}

		const ESQEnemyAnimState AnimSt = SQEnemyAnim(V.State);
		const bool bStateChanged = Slot.LastState != SQEnemyPoseState(V.State);
		Slot.LastState = SQEnemyPoseState(V.State);
		const float StateAge = T - V.StateAt;
		const bool bWindup = AnimSt == ESQEnemyAnimState::Windup;
		const float WindupDur = (Def.Gait == ESQEnemyGait::Charge || Def.Fly == ESQEnemyFly::Swoop) ? FMath::Max(0.3f, Def.Windup) : FMath::Max(0.05f, Def.Windup);
		const float WindupK = bWindup ? FMath::Clamp(StateAge / WindupDur, 0.f, 1.f) : 0.f;

		FVector2D PlayerPos = FVector2D::ZeroVector;
		float PlayerDistSq = 0.f;
		const bool bHasPlayer = NearestPlayer(V.Pos, PlayerPos, PlayerDistSq);
		const bool bFlyerSlot = Def.IsFlyer();
		const bool bPhaserSlot = Def.Fly == ESQEnemyFly::Phase;
		// A Gravebat keeps its swoop take through the dive
		Slot.bAttacking = bWindup || AnimSt == ESQEnemyAnimState::Strike || (Def.Fly == ESQEnemyFly::Swoop && AnimSt == ESQEnemyAnimState::Charge);

		// Hit reaction timers (jolt away from the nearest reaper: the likely hitter)
		if (V.bHit)
		{
			Slot.HitAge = 0.f;
			const FVector2D Away = bHasPlayer ? (V.Pos - PlayerPos) : FVector2D(FMath::Cos(V.Id * 1.7f), FMath::Sin(V.Id * 1.7f));
			Slot.JoltDir = Away.GetSafeNormal();
		}
		else
		{
			Slot.HitAge = FMath::Min(Slot.HitAge + DeltaSeconds, 10.f);
		}
		Slot.SpawnAge = FMath::Min(Slot.SpawnAge + DeltaSeconds, 10.f);

		// --- one-shots: wind-up synced to the server, spawn take, hit react ---
		if (Slot.bOneShot)
		{
			Slot.OneShotLeft -= DeltaSeconds;
			const bool bAttackCancelled = Slot.OneShotKind == 1 && !Slot.bAttacking && AnimSt != ESQEnemyAnimState::Charge;
			if (Slot.OneShotLeft <= 0.f || bAttackCancelled) EndOneShot(Follower, Slot);
		}
		const ESQEnemySpawn SpawnStyle = SQEnemySpawnStyle(Def, V.State);
		const float SpawnT = SQEnemySpawnTime(SpawnStyle);
		if (bStateChanged && bWindup && Info.AttackSeq)
		{
			// Time the take so its impact (about 45% in) lands when the server's wind-up ends
			const float Len = static_cast<float>(Info.AttackSeq->GetPlayLength());
			StartOneShot(Follower, Slot, Info.AttackSeq, Len * 0.45f / WindupDur, 1);
		}
		else if (!Slot.bOneShot && Slot.SpawnAge < SpawnT * 0.5f && Info.SpawnSeq && SpawnStyle != ESQEnemySpawn::Burst)
		{
			const float Len = static_cast<float>(Info.SpawnSeq->GetPlayLength());
			StartOneShot(Follower, Slot, Info.SpawnSeq, Len / (SpawnT + 0.4f), 3);
		}
		else if (V.bHit && !Slot.bOneShot && AnimSt == ESQEnemyAnimState::Move && Info.HitSeq && FMath::FRand() < 0.35f)
		{
			StartOneShot(Follower, Slot, Info.HitSeq, 1.5f, 2);
		}

		if (!Slot.bOneShot)
		{
			int32 WantLeader = (Slot.bAttacking && SkinLeaders.IsValidIndex(Info.AttackLeaders[Variant])) ? Info.AttackLeaders[Variant] : Info.Leaders[Variant];
			if (!SkinLeaders.IsValidIndex(WantLeader) || !SkinLeaders[WantLeader])
			{
				// Should not happen; fall back to the primitive for this enemy
				if (Slot.bShown) { Follower->SetVisibility(false, true); Slot.bShown = false; }
				continue;
			}
			if (Slot.LeaderIdx != WantLeader)
			{
				Follower->SetLeaderPoseComponent(SkinLeaders[WantLeader].Get());
				Slot.LeaderIdx = WantLeader;
			}
		}

		// Facing: toward the reaper while attacking, else the direction of travel (smoothed)
		bool bMovedNow = false;
		float TargetYaw = Slot.Yaw;
		if (!Slot.bHasPos)
		{
			Slot.bHasPos = true;
			Slot.LastPos = V.Pos;
			Slot.Yaw = V.Yaw;
			TargetYaw = Slot.Yaw;
		}
		else
		{
			const FVector2D Moved = V.Pos - Slot.LastPos;
			if (Moved.SizeSquared() > 1.f)
			{
				bMovedNow = true;
				TargetYaw = FMath::RadiansToDegrees(FMath::Atan2((float)Moved.Y, (float)Moved.X));
				Slot.LastPos = V.Pos;
				Slot.MoveHold = 0.25f;
			}
			else
			{
				Slot.MoveHold = FMath::Max(0.f, Slot.MoveHold - DeltaSeconds);
			}
		}
		if (Slot.bAttacking && bHasPlayer)
		{
			const FVector2D ToPlayer = PlayerPos - V.Pos;
			if (ToPlayer.SizeSquared() > 1.f)
			{
				TargetYaw = FMath::RadiansToDegrees(FMath::Atan2((float)ToPlayer.Y, (float)ToPlayer.X));
				bMovedNow = true;
			}
		}
		if (bMovedNow)
		{
			Slot.Yaw = FMath::UnwindDegrees(Slot.Yaw + FMath::FindDeltaAngleDegrees(Slot.Yaw, TargetYaw) * TurnAlpha);
		}

		// Terrain: stand on the ground and lean (max 15 deg) with the slope along the facing
		const float GroundHere = bFlyerSlot ? SQFlyerBaseZ(V.FlyGround, V.FlyZ, GroundAt(V.Pos)) : GroundAt(V.Pos);
		float TargetPitch = 0.f;
		if (FrameArena && !bFlyerSlot)
		{
			const float YawRad = FMath::DegreesToRadians(Slot.Yaw);
			const FVector2D Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad));
			const float Ahead = GroundAt(V.Pos + Fwd * 45.0);
			const float Behind = GroundAt(V.Pos - Fwd * 45.0);
			TargetPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Ahead - Behind, 90.f)), -15.f, 15.f);
		}
		Slot.Pitch = FMath::Lerp(Slot.Pitch, TargetPitch, PitchAlpha);

		// Movement personality on top of the clip (halved lean: the clip already leans)
		const float Phase = IdHash01(V.Id, 5u) * 2.f * PI;
		const bool bFrozen = AnimSt == ESQEnemyAnimState::Frozen;
		const FSQGaitPose G = bFrozen ? FSQGaitPose() : SQGaitPose(Def.Gait, T, Phase, Slot.MoveHold > 0.f ? V.MoveAmt : 0.f, true);
		const bool bClipHops = Def.Gait == ESQEnemyGait::Hop; // the hop take does its own bounce
		float ExtraPitch = G.Pitch * 0.5f;
		float ExtraRoll = G.Roll * 0.6f;
		float ExtraYaw = G.YawJitter;
		float Bob = bClipHops ? 0.f : G.Bob;
		float Swell = 1.f;
		float Lunge = 0.f;
		float PhaseThin = 1.f; // Wraith phase-out / phase-in: XY thickness
		switch (AnimSt)
		{
		case ESQEnemyAnimState::Windup:
			if (bPhaserSlot)
			{
				PhaseThin = FMath::Lerp(1.f, 0.25f, WindupK);
				ExtraYaw += 40.f * WindupK * FMath::Sin(T * 30.f);
				break;
			}
			Swell = 1.f + 0.12f * WindupK * (0.5f + 0.5f * FMath::Sin(T * (20.f + 30.f * WindupK)));
			ExtraYaw += 4.f * FMath::Sin(T * 45.f) * WindupK;
			if (Def.Gait == ESQEnemyGait::Charge || Def.Fly == ESQEnemyFly::Swoop) ExtraPitch -= 12.f * WindupK; // rears back
			break;
		case ESQEnemyAnimState::Strike:
			if (bPhaserSlot)
			{
				const float InK = FMath::Clamp(StateAge / 0.35f, 0.f, 1.f);
				PhaseThin = InK < 0.6f ? FMath::Lerp(0.25f, 1.15f, InK / 0.6f) : FMath::Lerp(1.15f, 1.f, (InK - 0.6f) / 0.4f);
				break;
			}
			Lunge = 25.f * (1.f - FMath::Clamp(StateAge / 0.3f, 0.f, 1.f));
			break;
		case ESQEnemyAnimState::Charge:
			ExtraPitch += bFlyerSlot ? 40.f : 18.f; // flyers dive nose-first
			break;
		case ESQEnemyAnimState::Recover:
			if (bFlyerSlot)
			{
				ExtraPitch -= 18.f; // climbing back up
				break;
			}
			ExtraRoll += 15.f * FMath::Sin(T * 9.f);
			ExtraYaw += 20.f * FMath::Sin(T * 4.f);
			break;
		default:
			break;
		}

		// Variety: per-Id size jitter (+-8%), silhouette stretch, gait squash
		const float Jitter = 1.f + 0.08f * (IdHash01(V.Id, 17u) * 2.f - 1.f);
		const float BaseScale = Info.MeshScale * Jitter * Swell;
		const float GaitXY = bClipHops ? 1.f : G.SquashXY;
		const float GaitZ = bClipHops ? 1.f : G.SquashZ;
		FVector ScaleVec(BaseScale * Def.Stretch.X * GaitXY * PhaseThin, BaseScale * Def.Stretch.X * GaitXY * PhaseThin, BaseScale * Def.Stretch.Y * GaitZ);
		if (Slot.HitAge < HitSquashTime)
		{
			const float K = 1.f - Slot.HitAge / HitSquashTime;
			ScaleVec.X *= 1.f + 0.2f * K;
			ScaleVec.Y *= 1.f + 0.2f * K;
			ScaleVec.Z *= 1.f - 0.15f * K;
		}

		const float FaceRad = FMath::DegreesToRadians(Slot.Yaw);
		FVector Loc(V.Pos.X + FMath::Cos(FaceRad) * Lunge, V.Pos.Y + FMath::Sin(FaceRad) * Lunge, GroundHere + Bob);
		if (Slot.HitAge < HitJoltTime)
		{
			const float K = 1.f - Slot.HitAge / HitJoltTime;
			Loc.X += Slot.JoltDir.X * HitJoltDist * K * K;
			Loc.Y += Slot.JoltDir.Y * HitJoltDist * K * K;
		}
		// Spawn styles (procedural part; the spawn take, when it played, does the rest)
		const bool bSpawnTake = Slot.bOneShot && Slot.OneShotKind == 3;
		if (Slot.SpawnAge < SpawnT)
		{
			const float P = FMath::Clamp(Slot.SpawnAge / SpawnT, 0.f, 1.f);
			switch (SpawnStyle)
			{
			case ESQEnemySpawn::Crawl:
				if (!bSpawnTake) { Loc.Z -= Info.ArtHeight * Jitter * (1.f - P); ExtraPitch += 50.f * (1.f - P); }
				break;
			case ESQEnemySpawn::Drop:
				Loc.Z += 700.f * (1.f - P * P);
				ScaleVec.Z *= 1.f + 0.2f * (1.f - P);
				break;
			case ESQEnemySpawn::Burst:
				ScaleVec *= P < 0.7f ? 1.25f * P / 0.7f : 1.25f - 0.25f * (P - 0.7f) / 0.3f;
				break;
			case ESQEnemySpawn::Fade:
				ScaleVec *= P;
				break;
			default:
				if (!bSpawnTake) Loc.Z -= Info.ArtHeight * Jitter * (1.f - FMath::InterpEaseOut(0.f, 1.f, P, 2.f));
				break;
			}
		}
		else if (SpawnStyle != ESQEnemySpawn::Fade)
		{
			// Spawn pop: a stretch up out of the ground (or off the landing), a squash, then rest
			const float PopZ = SQSpawnPopZ(Slot.SpawnAge - SpawnT);
			ScaleVec.Z *= PopZ;
			ScaleVec.X *= 1.f - 0.5f * (PopZ - 1.f);
			ScaleVec.Y *= 1.f - 0.5f * (PopZ - 1.f);
		}

		const FQuat Rot = FQuat(FRotator(Slot.Pitch + ExtraPitch, Slot.Yaw + ExtraYaw, ExtraRoll)) * ArtQuat;
		Follower->SetWorldTransform(FTransform(Rot, Loc, ScaleVec));

		// Glow: elite base + wind-up pulse (warming toward red) + white hit flash, through the slot's own MID
		if (SlotMID)
		{
			float Glow = Info.BodyGlow;
			if (bWindup) Glow += (0.6f + 2.2f * WindupK) * (0.5f + 0.5f * FMath::Sin(T * (18.f + 26.f * WindupK)));
			if (Slot.HitAge < HitFlashTime) Glow = FMath::Max(Glow, HitFlashGlow * (1.f - Slot.HitAge / HitFlashTime));
			if (FMath::Abs(Glow - Slot.CurGlow) > 0.01f)
			{
				SlotMID->SetScalarParameterValue(GlowParam, Glow);
				Slot.CurGlow = Glow;
			}
			const float Warm = (bWindup && !bPhaserSlot) ? 0.35f + 0.65f * WindupK : 0.f; // a phasing Wraith glows, it doesn't warn
			if (FMath::Abs(Warm - Slot.CurWarm) > 0.02f)
			{
				SlotMID->SetVectorParameterValue(ColorParam, FMath::Lerp(Info.Tint, WarnTint, Warm));
				Slot.CurWarm = Warm;
			}
		}

		if (!Slot.bShown) { Follower->SetVisibility(true, true); Slot.bShown = true; }
		if (SlotProp && !Info.PropMesh && SlotProp->IsVisible()) SlotProp->SetVisibility(false);
		OutSkinned[ViewIdx] = true;

		// Hand-off state for the impostor that takes over if this enemy leaves the skinned set: the same facing, slope lean and
		// walk phase (the leader's position in its loop; an attack / hit one-shot keeps the last walk phase)
		{
			FViewItem& HandOff = EnemyView[ViewIdx];
			HandOff.Yaw = Slot.Yaw;
			HandOff.SlopePitch = Slot.Pitch;
			const int32 WalkLeader = Info.Leaders[Variant];
			if (!Slot.bOneShot && Slot.LeaderIdx == WalkLeader && SkinLeaders.IsValidIndex(WalkLeader) && SkinLeaders[WalkLeader])
			{
				HandOff.WalkPhase = FMath::Frac(SkinLeaders[WalkLeader]->GetPosition() / FMath::Max(0.05f, Info.MoveLen));
			}
		}

		// Tower riders: smaller copies sitting on the shoulders, swaying more the higher they are
		float RiderZ = Loc.Z + Info.ArtHeight * Jitter * 0.62f;
		for (int32 RiderIdx = 1; RiderIdx <= Def.Stack && RiderCursor < StackFollowers.Num(); ++RiderIdx)
		{
			USkeletalMeshComponent* Rider = StackFollowers[RiderCursor];
			const int32 RiderSlot = RiderCursor++;
			if (!Rider) continue;
			if (StackTypeOf[RiderSlot] != (int32)V.Type)
			{
				Rider->SetSkeletalMeshAsset(Info.Mesh);
				SQArt::SetAllMaterials(Rider, SkinMIDs.IsValidIndex(V.Type) ? SkinMIDs[V.Type].Get() : nullptr);
				ApplyTypeGlowSlots(Rider, V.Type);
				StackTypeOf[RiderSlot] = V.Type;
				StackLeaderOf[RiderSlot] = INDEX_NONE;
			}
			const int32 RiderLeader = Info.StackLeaders[(Variant + RiderIdx) % 3];
			if (SkinLeaders.IsValidIndex(RiderLeader) && StackLeaderOf[RiderSlot] != RiderLeader)
			{
				Rider->SetLeaderPoseComponent(SkinLeaders[RiderLeader].Get());
				StackLeaderOf[RiderSlot] = RiderLeader;
			}
			const float RiderShrink = FMath::Pow(0.85f, static_cast<float>(RiderIdx));
			const float RiderSway = 7.f * RiderIdx * FMath::Sin(T * 2.5f + Phase + RiderIdx);
			const FVector RiderLoc(Loc.X - FMath::Sin(FaceRad) * RiderSway, Loc.Y + FMath::Cos(FaceRad) * RiderSway, RiderZ);
			const FQuat RiderRot = FQuat(FRotator(Slot.Pitch, Slot.Yaw + 8.f * FMath::Sin(T * 1.7f + RiderIdx), ExtraRoll + RiderSway * 1.5f)) * ArtQuat;
			Rider->SetWorldTransform(FTransform(RiderRot, RiderLoc, ScaleVec * RiderShrink));
			if (!Rider->IsVisible()) Rider->SetVisibility(true);
			RiderZ += Info.ArtHeight * Jitter * RiderShrink * 0.62f;
		}
	}
	// Hide riders nobody used this frame
	for (int32 RiderSlot = RiderCursor; RiderSlot < StackShown && RiderSlot < StackFollowers.Num(); ++RiderSlot)
	{
		if (StackFollowers[RiderSlot]) StackFollowers[RiderSlot]->SetVisibility(false);
	}
	StackShown = RiderCursor;
}

void ASQHordeManager::SpawnDeathPuppet(const FViewItem& Dead, bool bLaunched)
{
	const USQData* D = USQData::Get(this);
	UWorld* World = GetWorld();
	if (!D || !World || !D->Enemies.IsValidIndex(Dead.Type)) return;
	if (FVector2D::DistSquared(Dead.Pos, SkinViewCenter) > SkinRadius * SkinRadius) return;
	const FSQEnemyDef& Def = D->Enemies[Dead.Type];
	const bool bArt = SkinTypes.IsValidIndex(Dead.Type) && SkinTypes[Dead.Type].Mesh != nullptr;
	const float Height = bArt ? SkinTypes[Dead.Type].ArtHeight : 100.f * Def.Scale * Def.Stretch.Y;
	const float Ground = GroundAt(Dead.Pos);
	// Flyers die where they were in the air (and fall); everyone else on the ground
	const bool bFlyerDeath = Def.IsFlyer();
	const float DeathBase = bFlyerDeath ? SQFlyerBaseZ(Dead.FlyGround, Dead.FlyZ, Ground) : Ground;
	const FVector BodyAt(Dead.Pos.X, Dead.Pos.Y, DeathBase + Height * 0.5f);

	// Away from the nearest reaper (the likely killer)
	FVector2D PlayerPos = FVector2D::ZeroVector;
	float PlayerDistSq = 0.f;
	FVector2D Away2D = NearestPlayer(Dead.Pos, PlayerPos, PlayerDistSq) ? (Dead.Pos - PlayerPos).GetSafeNormal() : FVector2D(FMath::Cos(Dead.Id * 1.7f), FMath::Sin(Dead.Id * 1.7f));
	if (Away2D.IsNearlyZero()) Away2D = FVector2D(1.f, 0.f);
	const FVector Away(Away2D.X, Away2D.Y, 0.f);

	// Bones for debris (lazy shared material)
	if (!BoneDebrisMID) BoneDebrisMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White);
	static const FName BoneProps[] = { FName(TEXT("bone_A")), FName(TEXT("bone_B")), FName(TEXT("bone_C")), FName(TEXT("skull")) };
	auto ThrowBones = [&](int32 Count, float Power)
	{
		for (int32 BoneIdx = 0; BoneIdx < Count; ++BoneIdx)
		{
			UStaticMesh* BoneMesh = SQArt::Prop(BoneProps[BoneIdx % 4]);
			if (!BoneMesh) { SQFx::Burst(World, BodyAt, FLinearColor(0.9f, 0.88f, 0.8f), 3, Power * 0.5f, 0.6f, 7.f); continue; }
			SpawnDebris(BodyAt, Away, BoneMesh, BoneDebrisMID, BoneIdx % 4 == 3 ? 28.f * Def.Scale : 34.f * Def.Scale, 1, Power);
		}
	};
	// The accessory flies off on every death
	UStaticMesh* PropMesh = SQArt::Prop(Def.Prop);
	UMaterialInstanceDynamic* PropMID = PropMIDs.IsValidIndex(Dead.Type) ? PropMIDs[Dead.Type].Get() : nullptr;
	const bool bPropBody = TypePropIsBody.IsValidIndex(Dead.Type) && TypePropIsBody[Dead.Type];
	if (PropMesh && !bPropBody) SpawnDebris(BodyAt + FVector(0.f, 0.f, Height * 0.3f), Away, PropMesh, PropMID, Def.PropSize * Def.Scale, 1, 500.f);

	// Nov 19 2026: every death near the camera also lets go of a soul wisp in the enemy's colour (FxRecipes.csv "Death"), at most 10 a frame so a
	// screen-wide wipe cannot flood the particle pool. Purely visual; nothing here touches the sim.
	{
		static uint64 DeathFxFrame = 0;
		static int32 DeathFxThisFrame = 0;
		if (DeathFxFrame != GFrameCounter) { DeathFxFrame = GFrameCounter; DeathFxThisFrame = 0; }
		if (DeathFxThisFrame < 10)
		{
			DeathFxThisFrame++;
			SQFxRecipe::PlayShared(World, FName(TEXT("Death")), FVector(Dead.Pos.X, Dead.Pos.Y, DeathBase), Def.Accent, FMath::Clamp(Height * 0.5f, 30.f, 220.f));
		}
	}

	const ESQEnemyDeath Style = bLaunched ? ESQEnemyDeath::Launch : Def.Death;
	switch (Style)
	{
	case ESQEnemyDeath::Bones:
		ThrowBones(5, 520.f);
		SQFx::Burst(World, BodyAt, FLinearColor(0.9f, 0.88f, 0.8f), 6, 300.f, 0.4f, 6.f);
		return;
	case ESQEnemyDeath::Pop:
		ThrowBones(3, 650.f);
		SQFx::Burst(World, BodyAt, Def.Accent, 14, 520.f, 0.5f, 9.f);
		SQFx::Ring(World, FVector(Dead.Pos.X, Dead.Pos.Y, Ground + 5.f), Def.Accent, 12, 420.f, 0.4f, 8.f);
		return;
	case ESQEnemyDeath::Fade:
		SQFx::Burst(World, BodyAt, Def.Accent, 10, 160.f, 1.1f, 8.f, 420.f);
		return;
	default:
		break;
	}

	// Tumble / Launch: a puppet plays the death take (falls back to bones when this type has no art)
	if (!bArt || DeathPuppets.Num() == 0 || PuppetStates.Num() != DeathPuppets.Num())
	{
		ThrowBones(4, Style == ESQEnemyDeath::Launch ? 800.f : 450.f);
		return;
	}
	const FSkinType& Info = SkinTypes[Dead.Type];
	UAnimSequence* DeathAnim = (Dead.Id % 2 == 1) ? Info.DeathB : Info.DeathA;
	if (!DeathAnim) DeathAnim = Info.DeathA ? Info.DeathA : Info.DeathB;

	const int32 PuppetIdx = PuppetHead;
	PuppetHead = (PuppetHead + 1) % DeathPuppets.Num();
	USkeletalMeshComponent* Puppet = DeathPuppets[PuppetIdx];
	if (!Puppet) return;

	FPuppetState& State = PuppetStates[PuppetIdx];
	if (State.Type != (int32)Dead.Type)
	{
		Puppet->SetSkeletalMeshAsset(Info.Mesh);
		SQArt::SetAllMaterials(Puppet, SkinMIDs.IsValidIndex(Dead.Type) ? SkinMIDs[Dead.Type].Get() : nullptr);
		State.Type = Dead.Type;
	}

	const float Jitter = 1.f + 0.08f * (IdHash01(Dead.Id, 17u) * 2.f - 1.f); // same size it had alive
	const float* FreedYaw = FreedSkinYaw.Find(Dead.Id);
	const float FacingYaw = FreedYaw ? *FreedYaw : FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-Away2D.Y), static_cast<float>(-Away2D.X)));
	State.Loc = FVector(Dead.Pos.X, Dead.Pos.Y, DeathBase);
	State.Age = 0.f;
	State.SinkDepth = FMath::Max(20.f, Info.ArtHeight * Jitter);
	State.bActive = true;
	State.bLanded = false;
	State.Style = static_cast<uint8>(Style);
	State.Rot = FRotator(0.f, FacingYaw, 0.f);
	State.Scale = Info.MeshScale * Jitter;
	State.BaseScale = FVector(State.Scale * Def.Stretch.X, State.Scale * Def.Stretch.X, State.Scale * Def.Stretch.Y);
	State.RollSign = (Dead.Id & 1) ? 1.f : -1.f;
	State.LandAge = -1.f;
	if (bFlyerDeath && Style != ESQEnemyDeath::Launch)
	{
		// Shot out of the air: a limp, spinning fall (uses the launch physics, no upward throw)
		State.Style = static_cast<uint8>(ESQEnemyDeath::Launch);
		State.Vel = Away * FMath::FRandRange(150.f, 260.f) + FVector(0.f, 0.f, FMath::FRandRange(80.f, 220.f));
		State.Spin = FRotator(FMath::FRandRange(200.f, 420.f), FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-300.f, 300.f));
	}
	else if (Style == ESQEnemyDeath::Launch)
	{
		// Big hit: thrown up and away, spinning (heavier types fly less far)
		const float Heft = 1.f / FMath::Sqrt(FMath::Max(0.5f, Def.Scale));
		State.Vel = Away * FMath::FRandRange(700.f, 1000.f) * Heft + FVector(0.f, 0.f, FMath::FRandRange(750.f, 1000.f) * Heft);
		State.Spin = FRotator(FMath::FRandRange(360.f, 720.f), FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-360.f, 360.f));
	}
	else
	{
		// Ragdoll-ish topple: knocked back a step, then falls over backwards
		State.Vel = Away * 180.f;
		State.Spin = FRotator::ZeroRotator;
	}

	Puppet->SetWorldScale3D(State.BaseScale);
	Puppet->SetWorldLocationAndRotation(State.Loc, FQuat(State.Rot) * FQuat(FRotator(0.f, SQArt::YawOffset(this), 0.f)));
	Puppet->SetVisibility(true);
	if (DeathAnim)
	{
		SQArt::Play(Puppet, DeathAnim, false);
		// Restart even if this puppet last played the same death anim
		Puppet->SetPosition(0.f, false);
		Puppet->Play(false);
	}
}

void ASQHordeManager::TickDeathPuppets(float DeltaSeconds)
{
	const int32 Num = FMath::Min(DeathPuppets.Num(), PuppetStates.Num());
	if (Num == 0) return;
	const FQuat ArtQuat(FRotator(0.f, SQArt::YawOffset(this), 0.f));
	UWorld* World = GetWorld();
	for (int32 i = 0; i < Num; ++i)
	{
		FPuppetState& State = PuppetStates[i];
		if (!State.bActive) continue;
		USkeletalMeshComponent* Puppet = DeathPuppets[i];
		if (!Puppet) { State.bActive = false; continue; }
		State.Age += DeltaSeconds;
		const bool bLaunch = State.Style == static_cast<uint8>(ESQEnemyDeath::Launch);
		const float Life = bLaunch ? 1.9f : 1.3f;
		if (State.Age >= Life)
		{
			State.bActive = false;
			Puppet->Stop();
			Puppet->SetVisibility(false);
			continue;
		}
		if (bLaunch && !State.bLanded)
		{
			State.Vel.Z += -2400.f * DeltaSeconds;
			State.Loc += State.Vel * DeltaSeconds;
			State.Rot += State.Spin * DeltaSeconds;
			const float Floor = GroundAt(FVector2D(State.Loc.X, State.Loc.Y));
			if (State.Loc.Z <= Floor && State.Vel.Z < 0.f)
			{
				State.Loc.Z = Floor;
				State.Vel = FVector(State.Vel.X * 0.4f, State.Vel.Y * 0.4f, -State.Vel.Z * 0.3f);
				State.Spin *= 0.35f;
				State.LandAge = 0.f; // every bounce squashes
				if (FMath::Abs(State.Vel.Z) < 160.f)
				{
					State.bLanded = true;
					if (World) SQFx::Ring(World, State.Loc + FVector(0.f, 0.f, 5.f), FLinearColor(0.42f, 0.36f, 0.3f), 8, 220.f, 0.35f, 7.f);
				}
			}
		}
		else if (!bLaunch)
		{
			// Knocked off its feet: a little hop back, topple over (eased, with a twist to one side), thud and squash
			const float K = FMath::Clamp(State.Age / 0.35f, 0.f, 1.f);
			State.Rot.Pitch = 78.f * FMath::InterpEaseIn(0.f, 1.f, K, 2.2f);
			State.Rot.Roll = 18.f * State.RollSign * FMath::Sin(K * PI * 0.5f);
			State.Loc += State.Vel * DeltaSeconds * (1.f - K);
			State.Loc.Z = GroundAt(FVector2D(State.Loc.X, State.Loc.Y)) + 22.f * FMath::Sin(FMath::Min(State.Age / 0.22f, 1.f) * PI);
			if (K >= 1.f && State.LandAge < 0.f)
			{
				State.LandAge = 0.f;
				if (World) SQFx::Ring(World, State.Loc + FVector(0.f, 0.f, 4.f), FLinearColor(0.42f, 0.36f, 0.3f), 6, 160.f, 0.3f, 6.f);
			}
		}
		// Impact squash: flattens on the thud and springs back (a wobble, not a pop)
		FVector ShownScale = State.BaseScale;
		if (State.LandAge >= 0.f)
		{
			State.LandAge += DeltaSeconds;
			const float Wobble = FMath::Exp(-State.LandAge * 9.f) * FMath::Cos(State.LandAge * 38.f);
			ShownScale.Z *= 1.f - 0.22f * Wobble;
			ShownScale.X *= 1.f + 0.12f * Wobble;
			ShownScale.Y *= 1.f + 0.12f * Wobble;
		}
		Puppet->SetWorldScale3D(ShownScale);
		FVector Shown = State.Loc;
		if (State.Age > Life - 0.4f)
		{
			const float Sink = FMath::Clamp((State.Age - (Life - 0.4f)) / 0.4f, 0.f, 1.f);
			Shown.Z -= State.SinkDepth * Sink;
		}
		Puppet->SetWorldLocationAndRotation(Shown, FQuat(State.Rot) * ArtQuat);
	}
}

// ================================================================ collision shapes + far impostors (Nov 7)

float ASQHordeManager::EnemyRadius(int32 Type) const
{
	return TypeRadius.IsValidIndex(Type) ? TypeRadius[Type] : 40.f;
}

float ASQHordeManager::EnemyAttackReach(int32 Type, float ReaperRadius) const
{
	return EnemyRadius(Type) + ReaperRadius + (TypeReach.IsValidIndex(Type) ? TypeReach[Type] : 12.f);
}

float ASQHordeManager::ReaperBodyRadius(const ASQCharacter* Reaper, const USQData* Data)
{
	const float Fallback = Data ? FMath::Max(1.f, Data->Tune(TEXT("BodyBlockPlayerRadius"), 40.f)) : 40.f;
	if (!Reaper || (Data && Data->Tune(TEXT("BodyBlockUseCapsule"), 1.f) < 0.5f)) return Fallback;
	const UCapsuleComponent* Capsule = Reaper->GetCapsuleComponent();
	return Capsule ? FMath::Max(1.f, Capsule->GetScaledCapsuleRadius()) : Fallback;
}

void ASQHordeManager::InitTypeShapes()
{
	const USQData* D = USQData::Get(this);
	if (!D || D->Enemies.Num() == 0) return;
	if (bTypeShapesDone && TypeRadius.Num() == D->Enemies.Num()) return;
	bTypeShapesDone = true;
	const int32 NumTypes = D->Enemies.Num();
	const float ArtFit = FMath::Clamp(D->Tune(TEXT("CollisionArtFit"), 1.f), 0.f, 1.f);
	ImpostorPullRate = FMath::Max(0.f, D->Tune(TEXT("ImpostorPhasePull"), 4.f));
	TypeRadius.SetNum(NumTypes);
	TypeReach.SetNum(NumTypes);
	ImpostorSets.SetNum(NumTypes);
	TypeGlowMIDs.SetNum(NumTypes);
	MaxTypeRadius = 1.f;

	// Frames per model (several enemy types share one model)
	TMap<FName, FImpostorSet> ByModel;
	for (int32 TypeIdx = 0; TypeIdx < NumTypes; ++TypeIdx)
	{
		const FSQEnemyDef& Def = D->Enemies[TypeIdx];
		FImpostorSet Set;
		if (!Def.Model.IsNone())
		{
			if (const FImpostorSet* Known = ByModel.Find(Def.Model))
			{
				Set = *Known;
			}
			else
			{
				for (int32 FrameNo = 0; FrameNo < ImpostorFrames; ++FrameNo)
				{
					Set.Frames[FrameNo] = SQHMLoadImpostorFrame(Def.Model, FrameNo);
					if (Set.Frames[FrameNo])
					{
						Set.NumValid++;
						ImpostorAssetRefs.Add(Set.Frames[FrameNo]);
					}
				}
				// A missing frame reuses the one before it (or the first that exists)
				if (Set.NumValid > 0)
				{
					UStaticMesh* FirstValid = nullptr;
					for (UStaticMesh* Each : Set.Frames) { if (Each) { FirstValid = Each; break; } }
					UStaticMesh* PrevFrame = FirstValid;
					for (int32 FrameNo = 0; FrameNo < ImpostorFrames; ++FrameNo)
					{
						if (!Set.Frames[FrameNo]) Set.Frames[FrameNo] = PrevFrame;
						PrevFrame = Set.Frames[FrameNo];
					}
				}
				ByModel.Add(Def.Model, Set);
			}
		}

		// Art radius: the posed walk frames' width (average half extent X / Y) at the skinned mesh's cm scale x Stretch.X
		float ArtRadius = 0.f;
		if (Set.NumValid > 0)
		{
			const USkeletalMesh* SkMesh = SQArt::Character(Def.Model);
			Set.MeshScale = SkMesh ? SQArt::FitScale(SkMesh, Def.Height * Def.Scale) : SQArt::FitScale(Set.Frames[0], Def.Height * Def.Scale);
			float HalfSum = 0.f;
			for (int32 FrameNo = 0; FrameNo < ImpostorFrames; ++FrameNo)
			{
				const FVector Ext = Set.Frames[FrameNo]->GetBounds().BoxExtent;
				HalfSum += 0.5f * static_cast<float>(Ext.X + Ext.Y);
			}
			ArtRadius = HalfSum / ImpostorFrames * Set.MeshScale * Def.Stretch.X;
		}
		Set.ArtRadius = ArtRadius;
		ImpostorSets[TypeIdx] = Set;

		// Clamp: the 3x3 grid queries (CellSize 250) assume a body circle well under a cell
		const float Blended = ArtRadius > 1.f ? FMath::Lerp(Def.Radius, ArtRadius, ArtFit) : Def.Radius;
		TypeRadius[TypeIdx] = FMath::Clamp(Blended, 5.f, 200.f);
		TypeReach[TypeIdx] = Def.Reach;
		MaxTypeRadius = FMath::Max(MaxTypeRadius, TypeRadius[TypeIdx]);
		UE_LOG(LogSQ, Log, TEXT("[Collision] %s: radius %.0f (CSV %.0f, art %.0f), reach +%.0f, impostor frames %d"),
			*Def.Id.ToString(), TypeRadius[TypeIdx], Def.Radius, ArtRadius, Def.Reach, Set.NumValid);
	}
}

void ASQHordeManager::EnsureImpostorComponents()
{
	if (bImpostorInitDone) return;
	const USQData* D = USQData::Get(this);
	if (!D || GetNetMode() == NM_DedicatedServer || !Root || D->Enemies.Num() == 0) return;
	InitTypeShapes();
	bImpostorInitDone = true;
	const int32 NumTypes = D->Enemies.Num();
	ImpostorISMs.Init(nullptr, NumTypes * ImpostorFrames);
	ImpostorCapacity.Init(0, NumTypes * ImpostorFrames);
	ScratchImpostor.SetNum(NumTypes * ImpostorFrames);
	const bool bShadows = D->Tune(TEXT("ImpostorShadows"), 1.f) > 0.5f;
	for (int32 TypeIdx = 0; TypeIdx < NumTypes && TypeIdx < ImpostorSets.Num(); ++TypeIdx)
	{
		const FImpostorSet& Set = ImpostorSets[TypeIdx];
		if (Set.NumValid <= 0) continue;
		const FSQEnemyDef& Def = D->Enemies[TypeIdx];
		// The type's material: the skinned one when it exists (same tint + elite glow), else the same recipe
		UMaterialInstanceDynamic* BodyMID = SkinMIDs.IsValidIndex(TypeIdx) ? SkinMIDs[TypeIdx].Get() : nullptr;
		if (!BodyMID)
		{
			BodyMID = SQArt::TintMID(this, SQArt::CharacterTexture(Def.Model), SQArt::SoftTint(Def.Color, Def.TintStrength));
			if (BodyMID && Def.Behaviour == ESQEnemyBehaviour::Elite && Def.Glow > 0.f) BodyMID->SetScalarParameterValue(TEXT("Glow"), Def.Glow);
			if (BodyMID) ImpostorAssetRefs.Add(BodyMID);
		}
		for (int32 FrameNo = 0; FrameNo < ImpostorFrames; ++FrameNo)
		{
			UInstancedStaticMeshComponent* FrameISM = NewObject<UInstancedStaticMeshComponent>(this);
			FrameISM->SetMobility(EComponentMobility::Movable);
			FrameISM->SetStaticMesh(Set.Frames[FrameNo]);
			FrameISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			FrameISM->SetCastShadow(bShadows);
			FrameISM->SetupAttachment(Root);
			FrameISM->RegisterComponent();
			SQArt::SetAllMaterials(FrameISM, BodyMID);
			ApplyTypeGlowSlots(FrameISM, TypeIdx);
			ImpostorISMs[TypeIdx * ImpostorFrames + FrameNo] = FrameISM;
		}
	}
}

void ASQHordeManager::ApplyTypeGlowSlots(UMeshComponent* Comp, int32 Type)
{
	const USQData* D = USQData::Get(this);
	if (!Comp || !D || !D->Enemies.IsValidIndex(Type)) return;
	if (TypeGlowMIDs.Num() <= Type) TypeGlowMIDs.SetNum(D->Enemies.Num());
	const TArray<FName> GlowSlotNames = Comp->GetMaterialSlotNames();
	for (const FName& GlowSlotName : GlowSlotNames)
	{
		if (!GlowSlotName.ToString().Contains(TEXT("Glow"))) continue;
		const int32 GlowSlotIdx = Comp->GetMaterialIndex(GlowSlotName);
		if (GlowSlotIdx == INDEX_NONE) continue;
		if (!TypeGlowMIDs[Type])
		{
			TypeGlowMIDs[Type] = SQArt::TintMID(this, SQArt::CharacterTexture(D->Enemies[Type].Model), FLinearColor::White);
			if (TypeGlowMIDs[Type]) TypeGlowMIDs[Type]->SetScalarParameterValue(TEXT("Glow"), 2.5f);
		}
		if (TypeGlowMIDs[Type]) Comp->SetMaterial(GlowSlotIdx, TypeGlowMIDs[Type].Get());
	}
}

void ASQHordeManager::UpdateISMPadded(UInstancedStaticMeshComponent* ISM, TArray<FTransform>& Xforms, int32& Capacity)
{
	if (!ISM) return;
	const int32 Real = Xforms.Num();
	if (Real == 0)
	{
		if (ISM->GetInstanceCount() > 0) ISM->ClearInstances();
		Capacity = 0;
		return;
	}
	// Grow in steps of 32; shrink only below half, so a frame-ISM's count moving by a few never rebuilds the buffer
	if (Real > Capacity || Real < Capacity / 2) Capacity = ((Real + 31) / 32) * 32;
	// Spares: a near-zero-size copy of the first real instance (bounds stay tight; culled per instance)
	const FTransform Hidden(Xforms[0].GetRotation(), Xforms[0].GetLocation(), FVector(0.001f));
	while (Xforms.Num() < Capacity) Xforms.Add(Hidden);
	UpdateISM(ISM, Xforms);
}

void ASQHordeManager::AddImpostorInstance(FViewItem& V, const FSQEnemyDef& Def, const FImpostorSet& Imp, float Now, float DeltaSeconds, const FQuat& ArtQuat)
{
	// Mirrors RenderSkinned's follower pose (gait at the skinned strength, states, spawn styles, jitter, stretch, slope lean,
	// yaw offset) so the switch between the two is invisible; only the hit squash / jolt / glow are skinned-only.
	const ESQEnemyAnimState AnimSt = SQEnemyAnim(V.State);
	const bool bFrozen = AnimSt == ESQEnemyAnimState::Frozen;
	const bool bFlyerBody = Def.IsFlyer();
	const bool bPhaserBody = Def.Fly == ESQEnemyFly::Phase;
	const float StateAge = Now - V.StateAt;
	const int32 Variant = V.Id % 3;
	const FSkinType* Info = SkinTypes.IsValidIndex(V.Type) && SkinTypes[V.Type].Mesh ? &SkinTypes[V.Type] : nullptr;

	// 1. Walk phase: AnimRate (the clip rate, as the skinned leader) x how fast it moves; near the skinned ring it is pulled
	//    onto its leader's phase, and a skinned enemy writes its leader's phase back, so the hand-off keeps the same step
	const float CycleLen = Info ? FMath::Max(0.05f, Info->MoveLen) : 1.f;
	const float BaseRate = Def.AnimRate * (0.9f + 0.1f * Variant) / CycleLen;
	const float MoveFactor = bFrozen ? 0.f : FMath::Clamp(V.MoveAmt, 0.35f, 1.5f);
	float WalkPhase = FMath::Frac(V.WalkPhase + BaseRate * MoveFactor * DeltaSeconds);
	const float DistSq = static_cast<float>(FVector2D::DistSquared(V.Pos, SkinViewCenter));
	const float BandOuter = SkinRadius * 1.6f;
	const bool bNearRing = DistSq < BandOuter * BandOuter;
	const float LeaderPhase = ScratchLeaderPhase.IsValidIndex(V.Type * 3 + Variant) ? ScratchLeaderPhase[V.Type * 3 + Variant] : -1.f;
	if (LeaderPhase >= 0.f && bNearRing && !bFrozen)
	{
		const float Band = FMath::Clamp((BandOuter - FMath::Sqrt(DistSq)) / (SkinRadius * 0.5f), 0.f, 1.f); // 1 inside 1.1 x ring
		float PhaseDiff = LeaderPhase - WalkPhase;
		PhaseDiff -= FMath::RoundToFloat(PhaseDiff); // the short way round the loop
		WalkPhase = FMath::Frac(WalkPhase + PhaseDiff * FMath::Min(1.f, ImpostorPullRate * Band * DeltaSeconds));
	}
	V.WalkPhase = WalkPhase;
	const int32 Frame = FMath::Clamp(FMath::FloorToInt(WalkPhase * ImpostorFrames), 0, ImpostorFrames - 1);

	// 2. Slope lean (ground units near the ring only; far away it eases back upright)
	float TargetPitch = 0.f;
	if (FrameArena && !bFlyerBody && bNearRing)
	{
		const float LeanRad = FMath::DegreesToRadians(V.Yaw);
		const FVector2D LeanFwd(FMath::Cos(LeanRad), FMath::Sin(LeanRad));
		const float Ahead = GroundAt(V.Pos + LeanFwd * 45.0);
		const float Behind = GroundAt(V.Pos - LeanFwd * 45.0);
		TargetPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Ahead - Behind, 90.f)), -15.f, 15.f);
	}
	V.SlopePitch = FMath::Lerp(V.SlopePitch, TargetPitch, FMath::Min(1.f, DeltaSeconds * 6.f));

	// 3. Gait + state, at the skinned strength (the baked walk pose already leans)
	const float Phase = IdHash01(V.Id, 5u) * 2.f * PI;
	const FSQGaitPose G = bFrozen ? FSQGaitPose() : SQGaitPose(Def.Gait, Now, Phase, V.MoveAmt, true);
	const bool bClipHops = Def.Gait == ESQEnemyGait::Hop;
	float ExtraPitch = G.Pitch * 0.5f;
	float ExtraRoll = G.Roll * 0.6f;
	float ExtraYaw = G.YawJitter;
	const float Bob = bClipHops ? 0.f : G.Bob;
	float Swell = 1.f;
	float Lunge = 0.f;
	float PhaseThin = 1.f;
	const float WindupDur = (Def.Gait == ESQEnemyGait::Charge || Def.Fly == ESQEnemyFly::Swoop) ? FMath::Max(0.3f, Def.Windup) : FMath::Max(0.05f, Def.Windup);
	const float WindupK = AnimSt == ESQEnemyAnimState::Windup ? FMath::Clamp(StateAge / WindupDur, 0.f, 1.f) : 0.f;
	switch (AnimSt)
	{
	case ESQEnemyAnimState::Windup:
		if (bPhaserBody)
		{
			PhaseThin = FMath::Lerp(1.f, 0.25f, WindupK);
			ExtraYaw += 40.f * WindupK * FMath::Sin(Now * 30.f);
			break;
		}
		Swell = 1.f + 0.12f * WindupK * (0.5f + 0.5f * FMath::Sin(Now * (20.f + 30.f * WindupK)));
		ExtraYaw += 4.f * FMath::Sin(Now * 45.f) * WindupK;
		if (Def.Gait == ESQEnemyGait::Charge || Def.Fly == ESQEnemyFly::Swoop) ExtraPitch -= 12.f * WindupK;
		break;
	case ESQEnemyAnimState::Strike:
		if (bPhaserBody)
		{
			const float InK = FMath::Clamp(StateAge / 0.35f, 0.f, 1.f);
			PhaseThin = InK < 0.6f ? FMath::Lerp(0.25f, 1.15f, InK / 0.6f) : FMath::Lerp(1.15f, 1.f, (InK - 0.6f) / 0.4f);
			break;
		}
		Lunge = 25.f * (1.f - FMath::Clamp(StateAge / 0.3f, 0.f, 1.f));
		break;
	case ESQEnemyAnimState::Charge:
		ExtraPitch += bFlyerBody ? 40.f : 18.f;
		break;
	case ESQEnemyAnimState::Recover:
		if (bFlyerBody)
		{
			ExtraPitch -= 18.f;
			break;
		}
		ExtraRoll += 15.f * FMath::Sin(Now * 9.f);
		ExtraYaw += 20.f * FMath::Sin(Now * 4.f);
		break;
	default:
		break;
	}

	// 4. Scale: the skinned mesh's FitScale, per-Id jitter, Stretch, gait squash
	const float Jitter = 1.f + 0.08f * (IdHash01(V.Id, 17u) * 2.f - 1.f);
	const float BaseScale = Imp.MeshScale * Jitter * Swell;
	const float GaitXY = bClipHops ? 1.f : G.SquashXY;
	const float GaitZ = bClipHops ? 1.f : G.SquashZ;
	FVector ScaleVec(BaseScale * Def.Stretch.X * GaitXY * PhaseThin, BaseScale * Def.Stretch.X * GaitXY * PhaseThin, BaseScale * Def.Stretch.Y * GaitZ);

	// 5. Place it: feet on the ground (flyers at their smoothed altitude), spawn styles as the skinned path
	const float GroundHere = bFlyerBody ? SQFlyerBaseZ(V.FlyGround, V.FlyZ, GroundAt(V.Pos)) : GroundAt(V.Pos);
	const float FaceRad = FMath::DegreesToRadians(V.Yaw);
	FVector Loc(V.Pos.X + FMath::Cos(FaceRad) * Lunge, V.Pos.Y + FMath::Sin(FaceRad) * Lunge, GroundHere + Bob);
	const float ArtHeight = Def.Height * Def.Scale * Def.Stretch.Y;
	const ESQEnemySpawn SpawnStyle = SQEnemySpawnStyle(Def, V.State);
	const float SpawnT = SQEnemySpawnTime(SpawnStyle);
	const float SpawnAge = Now - V.Born;
	if (SpawnAge < SpawnT)
	{
		const float P = FMath::Clamp(SpawnAge / SpawnT, 0.f, 1.f);
		switch (SpawnStyle)
		{
		case ESQEnemySpawn::Crawl: Loc.Z -= ArtHeight * Jitter * (1.f - P); ExtraPitch += 50.f * (1.f - P); break;
		case ESQEnemySpawn::Drop: Loc.Z += 700.f * (1.f - P * P); ScaleVec.Z *= 1.f + 0.2f * (1.f - P); break;
		case ESQEnemySpawn::Burst: ScaleVec *= P < 0.7f ? 1.25f * P / 0.7f : 1.25f - 0.25f * (P - 0.7f) / 0.3f; break;
		case ESQEnemySpawn::Fade: ScaleVec *= P; break;
		default: Loc.Z -= ArtHeight * Jitter * (1.f - FMath::InterpEaseOut(0.f, 1.f, P, 2.f)); break;
		}
	}
	else if (SpawnStyle != ESQEnemySpawn::Fade)
	{
		const float PopZ = SQSpawnPopZ(SpawnAge - SpawnT);
		ScaleVec.Z *= PopZ;
		ScaleVec.X *= 1.f - 0.5f * (PopZ - 1.f);
		ScaleVec.Y *= 1.f - 0.5f * (PopZ - 1.f);
	}

	const FQuat Rot = FQuat(FRotator(V.SlopePitch + ExtraPitch, V.Yaw + ExtraYaw, ExtraRoll)) * ArtQuat;
	const FTransform BodyXform(Rot, Loc, ScaleVec);
	const int32 BaseSlot = V.Type * ImpostorFrames;
	if (ScratchImpostor.IsValidIndex(BaseSlot + Frame)) ScratchImpostor[BaseSlot + Frame].Add(BodyXform);

	// 6. Tower riders: smaller copies on the shoulders (the skinned riders' placement, a frame apart each)
	float RiderZ = Loc.Z + ArtHeight * Jitter * 0.62f;
	for (int32 RiderIdx = 1; RiderIdx <= Def.Stack; ++RiderIdx)
	{
		const float RiderShrink = FMath::Pow(0.85f, static_cast<float>(RiderIdx));
		const float RiderSway = 7.f * RiderIdx * FMath::Sin(Now * 2.5f + Phase + RiderIdx);
		const FVector RiderLoc(Loc.X - FMath::Sin(FaceRad) * RiderSway, Loc.Y + FMath::Cos(FaceRad) * RiderSway, RiderZ);
		const FQuat RiderRot = FQuat(FRotator(V.SlopePitch, V.Yaw + 8.f * FMath::Sin(Now * 1.7f + RiderIdx), ExtraRoll + RiderSway * 1.5f)) * ArtQuat;
		const int32 RiderFrame = (Frame + RiderIdx) % ImpostorFrames;
		if (ScratchImpostor.IsValidIndex(BaseSlot + RiderFrame)) ScratchImpostor[BaseSlot + RiderFrame].Add(FTransform(RiderRot, RiderLoc, ScaleVec * RiderShrink));
		RiderZ += ArtHeight * Jitter * RiderShrink * 0.62f;
	}

	// 7. The prop accessory, at its reference-pose spot on the body
	if (Info && Info->bFarProp && PropISMs.IsValidIndex(V.Type) && PropISMs[V.Type] && ScratchPropPerType.IsValidIndex(V.Type))
	{
		ScratchPropPerType[V.Type].Add(Info->FarPropCS * BodyXform);
	}
}

void ASQHordeManager::DrawCollisionDebug()
{
#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
	// Host view of the server sim: green = body circle (blocks reapers), blue = body of a flyer / wisp (never blocks),
	// red = attack reach (a reaper whose body overlaps it gets hit; telegraphs add EnemyWindupSlack), yellow = reaper body.
	UWorld* World = GetWorld();
	const USQData* D = USQData::Get(this);
	if (!World || !D) return;
	const double ShowSq = FMath::Square(2600.0);
	int32 Drawn = 0;
	for (const FEnemy& E : Enemies)
	{
		if (!E.bAlive || !D->Enemies.IsValidIndex(E.Type) || Drawn >= 400) continue;
		if (FVector2D::DistSquared(E.Pos, SkinViewCenter) > ShowSq) continue;
		const FSQEnemyDef& Def = D->Enemies[E.Type];
		const FVector At(E.Pos.X, E.Pos.Y, GroundAt(E.Pos) + 6.f);
		const float BodyR = EnemyRadius(E.Type);
		const float ReachR = BodyR + (TypeReach.IsValidIndex(E.Type) ? TypeReach[E.Type] : Def.Reach);
		DrawDebugCircle(World, At, BodyR, 24, TypeBlocks(Def) ? FColor::Green : FColor(80, 160, 255), false, -1.f, 0, 2.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		DrawDebugCircle(World, At, ReachR, 24, FColor::Red, false, -1.f, 0, 1.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		++Drawn;
	}
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		const ASQCharacter* Reaper = *It;
		if (!Reaper) continue;
		const FVector2D ReaperPos = Reaper->Pos2D();
		const FVector At(ReaperPos.X, ReaperPos.Y, GroundAt(ReaperPos) + 8.f);
		DrawDebugCircle(World, At, ReaperBodyRadius(Reaper, D), 24, FColor::Yellow, false, -1.f, 0, 3.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
	}
#endif
}

// ---- Run events (content item 12): the game mode follows a Hunt through the horde's one tracked miniboss

bool ASQHordeManager::GetMinibossPos(FVector2D& Out) const
{
	if (MinibossId == 0) return false;
	for (const FEnemy& Each : Enemies)
	{
		if (Each.Id == MinibossId)
		{
			Out = Each.Pos;
			return true;
		}
	}
	return false;
}
