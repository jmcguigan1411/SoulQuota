// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQBoss.h"
#include "Net/UnrealNetwork.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "SQArt.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "SQData.h"
#include "SQGameMode.h"
#include "SQGameState.h"
#include "SQCharacter.h"
#include "SQHordeManager.h"
#include "SQFx.h"
#include "SQArena.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Fixed per-ability numbers (cooldown s, wind-up s, anim, colour). Damage scales from the boss's CSV columns. */
	struct FSQBossAbilityInfo
	{
		float Cooldown = 6.f;
		float Windup = 1.f;
		uint8 Anim = 0;
		FColor Colour = FColor(220, 40, 40);
	};

	FSQBossAbilityInfo SQBossAbilityInfo(ESQBossAbility Ability)
	{
		FSQBossAbilityInfo Out;
		switch (Ability)
		{
		case ESQBossAbility::Slam:      Out = { 5.f, 1.1f, 0, FColor(220, 40, 40) }; break;
		case ESQBossAbility::Shockwave: Out = { 7.f, 1.0f, 0, FColor(255, 140, 40) }; break;
		case ESQBossAbility::Spiral:    Out = { 9.f, 0.9f, 6, FColor(180, 80, 255) }; break;
		case ESQBossAbility::Fan:       Out = { 4.5f, 0.8f, 2, FColor(255, 60, 160) }; break;
		case ESQBossAbility::Charge:    Out = { 8.f, 1.3f, 5, FColor(255, 50, 30) }; break;
		case ESQBossAbility::Summon:    Out = { 9.f, 1.0f, 1, FColor(120, 220, 90) }; break;
		case ESQBossAbility::Pools:     Out = { 10.f, 1.2f, 1, FColor(140, 255, 60) }; break;
		case ESQBossAbility::Vortex:    Out = { 14.f, 1.2f, 6, FColor(80, 140, 255) }; break;
		case ESQBossAbility::Mark:      Out = { 13.f, 0.8f, 7, FColor(255, 30, 60) }; break;
		case ESQBossAbility::Pylons:    Out = { 35.f, 1.2f, 8, FColor(255, 210, 80) }; break;
		case ESQBossAbility::Tether:    Out = { 22.f, 1.2f, 7, FColor(80, 255, 230) }; break;
		case ESQBossAbility::Ring:      Out = { 5.f, 0.8f, 2, FColor(255, 100, 60) }; break;
		// Nov 25 2026 map bosses (SQBossMap.cpp)
		case ESQBossAbility::TombWall:  Out = { 9.f, 1.1f, 0, FColor(205, 200, 220) }; break;
		case ESQBossAbility::Burrow:    Out = { 11.f, 0.6f, 6, FColor(170, 110, 60) }; break;
		case ESQBossAbility::Raise:     Out = { 12.f, 1.2f, 1, FColor(120, 220, 90) }; break;
		case ESQBossAbility::Cracks:    Out = { 10.f, 0.9f, 0, FColor(255, 110, 40) }; break;
		case ESQBossAbility::Flood:     Out = { 16.f, 1.0f, 6, FColor(60, 150, 255) }; break;
		case ESQBossAbility::Tentacles: Out = { 7.f, 1.0f, 2, FColor(40, 210, 200) }; break;
		case ESQBossAbility::GhostShip: Out = { 14.f, 2.0f, 7, FColor(140, 230, 255) }; break;
		case ESQBossAbility::KingTide:  Out = { 28.f, 1.5f, 4, FColor(30, 110, 255) }; break;
		case ESQBossAbility::TimeZones: Out = { 12.f, 0.8f, 7, FColor(255, 215, 90) }; break;
		case ESQBossAbility::ClockSweep:Out = { 10.f, 1.4f, 6, FColor(255, 185, 60) }; break;
		case ESQBossAbility::Rewind:    Out = { 25.f, 0.8f, 8, FColor(200, 160, 255) }; break;
		default: break;
		}
		return Out;
	}

	const TCHAR* SQBossAbilityLabel(ESQBossAbility Ability)
	{
		switch (Ability)
		{
		case ESQBossAbility::Slam: return TEXT("SLAM");
		case ESQBossAbility::Shockwave: return TEXT("SHOCKWAVE - jump the rings");
		case ESQBossAbility::Spiral: return TEXT("SPIRAL");
		case ESQBossAbility::Fan: return TEXT("VOLLEY");
		case ESQBossAbility::Charge: return TEXT("CHARGE");
		case ESQBossAbility::Summon: return TEXT("SUMMON");
		case ESQBossAbility::Pools: return TEXT("POOLS");
		case ESQBossAbility::Vortex: return TEXT("VORTEX - run out!");
		case ESQBossAbility::Ring: return TEXT("RING");
		case ESQBossAbility::TombWall: return TEXT("TOMBSTONE WALL - go round");
		case ESQBossAbility::Burrow: return TEXT("BURROWING - keep moving!");
		case ESQBossAbility::Raise: return TEXT("RAISING THE DEAD");
		case ESQBossAbility::Cracks: return TEXT("THE GROUND CRACKS - step off the lines");
		case ESQBossAbility::Flood: return TEXT("FLOOD - keep to the causeways");
		case ESQBossAbility::Tentacles: return TEXT("TENTACLES - change direction!");
		case ESQBossAbility::GhostShip: return TEXT("GHOST SHIP - leave its lane");
		case ESQBossAbility::KingTide: return TEXT("KING TIDE - get to high ground!");
		case ESQBossAbility::TimeZones: return TEXT("TIME ZONES - blue fast, orange slow, white frozen");
		case ESQBossAbility::ClockSweep: return TEXT("CLOCK HANDS - JUMP!");
		case ESQBossAbility::Rewind: return TEXT("REWIND - stand on the GEAR!");
		default: return TEXT("");
		}
	}

	FLinearColor SQBossElementColour(uint8 InElement)
	{
		switch (InElement)
		{
		case 1: return FLinearColor(1.f, 0.35f, 0.08f);  // fire
		case 2: return FLinearColor(0.45f, 0.8f, 1.f);   // ice
		case 3: return FLinearColor(0.85f, 0.7f, 1.f);   // lightning
		default: return FLinearColor::White;
		}
	}

	uint8 SQBossElementFromName(FName InName)
	{
		const FString Str = InName.ToString();
		if (Str.Equals(TEXT("Fire"), ESearchCase::IgnoreCase)) return 1;
		if (Str.Equals(TEXT("Ice"), ESearchCase::IgnoreCase)) return 2;
		if (Str.Equals(TEXT("Lightning"), ESearchCase::IgnoreCase)) return 3;
		return 0;
	}

	/** Element bias on ability weights: fire likes pools/spirals, ice likes shockwaves/vortex, lightning likes marks/volleys */
	float SQBossElementBias(uint8 InElement, ESQBossAbility Ability)
	{
		switch (InElement)
		{
		case 1: return (Ability == ESQBossAbility::Pools) ? 2.f : (Ability == ESQBossAbility::Spiral ? 1.5f : 1.f);
		case 2: return (Ability == ESQBossAbility::Shockwave) ? 2.f : (Ability == ESQBossAbility::Vortex ? 1.5f : 1.f);
		case 3: return (Ability == ESQBossAbility::Mark) ? 2.f : (Ability == ESQBossAbility::Fan ? 1.5f : 1.f);
		default: return 1.f;
		}
	}

	/** One-shot take names per MulticastAnim index (first that exists wins) */
	FName SQBossAnimFor(FName Model, uint8 Which, const FSQBossDef* Def)
	{
		if (Which == 4 && Def)
		{
			for (const FName& RoarName : Def->RoarAnims) if (SQArt::Anim(Model, RoarName)) return RoarName;
		}
		static const TCHAR* const Lists[9][3] = {
			{ TEXT("2H_Melee_Attack_Chop"), TEXT("1H_Melee_Attack_Chop"), nullptr },
			{ TEXT("Spellcast_Summon"), TEXT("Spellcast_Raise"), nullptr },
			{ TEXT("Spellcast_Shoot"), TEXT("Throw"), nullptr },
			{ TEXT("Death_A"), TEXT("Death_B"), nullptr },
			{ TEXT("SQ_Enemy_Roar"), TEXT("Taunt_Longer"), TEXT("Cheer") },
			{ TEXT("SQ_Ability_Stampede"), TEXT("2H_Melee_Idle"), nullptr },
			{ TEXT("Spellcast_Long"), TEXT("Spellcasting"), nullptr },
			{ TEXT("Spellcast_Raise"), TEXT("SQ_Ability_Lookout"), nullptr },
			{ TEXT("Blocking"), TEXT("Spellcast_Raise"), nullptr },
		};
		if (Which >= 9) return NAME_None;
		for (int32 NameIdx = 0; NameIdx < 3; ++NameIdx)
		{
			if (!Lists[Which][NameIdx]) break;
			const FName Candidate(Lists[Which][NameIdx]);
			if (SQArt::Anim(Model, Candidate)) return Candidate;
		}
		return NAME_None;
	}
}

ASQBoss::ASQBoss()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	AutoPossessAI = EAutoPossessAI::Spawned;
	AIControllerClass = AAIController::StaticClass();

	GetCapsuleComponent()->InitCapsuleSize(110.f, 160.f);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 240.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 250.f;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Crown = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Crown"));
	Crown->SetupAttachment(RootComponent);
	Crown->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	EyeL = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EyeL"));
	EyeL->SetupAttachment(RootComponent);
	EyeL->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	EyeR = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EyeR"));
	EyeR->SetupAttachment(RootComponent);
	EyeR->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Skin = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Skin"));
	Skin->SetupAttachment(RootComponent);
	Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Skin->SetGenerateOverlapEvents(false);
	Skin->SetVisibility(false);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetMobility(EComponentMobility::Movable);
	WeaponMesh->SetupAttachment(Skin);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetVisibility(false);

	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	PropMesh->SetMobility(EComponentMobility::Movable);
	PropMesh->SetupAttachment(Skin);
	PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PropMesh->SetVisibility(false);

	// Mechanic visuals live in world space (absolute transforms), hidden until used
	for (int32 PylonIdx = 0; PylonIdx < 3; ++PylonIdx)
	{
		UStaticMeshComponent* PylonComp = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Pylon%d"), PylonIdx));
		PylonComp->SetupAttachment(RootComponent);
		PylonComp->SetUsingAbsoluteLocation(true);
		PylonComp->SetUsingAbsoluteRotation(true);
		PylonComp->SetUsingAbsoluteScale(true);
		PylonComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PylonComp->SetVisibility(false);
		PylonMeshes.Add(PylonComp);
	}
	MarkDisc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkDisc"));
	MarkDisc->SetupAttachment(RootComponent);
	MarkDisc->SetUsingAbsoluteLocation(true);
	MarkDisc->SetUsingAbsoluteRotation(true);
	MarkDisc->SetUsingAbsoluteScale(true);
	MarkDisc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkDisc->SetCastShadow(false);
	MarkDisc->SetVisibility(false);

	TetherBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TetherBeam"));
	TetherBeam->SetupAttachment(RootComponent);
	TetherBeam->SetUsingAbsoluteLocation(true);
	TetherBeam->SetUsingAbsoluteRotation(true);
	TetherBeam->SetUsingAbsoluteScale(true);
	TetherBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TetherBeam->SetCastShadow(false);
	TetherBeam->SetVisibility(false);

	// Nov 25 2026: Merchant clock hands (world space, hidden until a sweep)
	HandBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandBeam"));
	HandBeamTwin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandBeamTwin"));
	for (UStaticMeshComponent* HandComp : { HandBeam.Get(), HandBeamTwin.Get() })
	{
		HandComp->SetupAttachment(RootComponent);
		HandComp->SetUsingAbsoluteLocation(true);
		HandComp->SetUsingAbsoluteRotation(true);
		HandComp->SetUsingAbsoluteScale(true);
		HandComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HandComp->SetCastShadow(false);
		HandComp->SetVisibility(false);
	}
}

void ASQBoss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQBoss, BossId);
	DOREPLIFETIME(ASQBoss, Health);
	DOREPLIFETIME(ASQBoss, MaxHealth);
	DOREPLIFETIME(ASQBoss, DefianceStacks);
	DOREPLIFETIME(ASQBoss, Phase);
	DOREPLIFETIME(ASQBoss, ActiveAbility);
	DOREPLIFETIME(ASQBoss, Element);
	DOREPLIFETIME(ASQBoss, bInvulnerable);
	DOREPLIFETIME(ASQBoss, bShielded);
	DOREPLIFETIME(ASQBoss, ShieldProgress);
	DOREPLIFETIME(ASQBoss, Pylons);
	DOREPLIFETIME(ASQBoss, PylonHeldMask);
	DOREPLIFETIME(ASQBoss, PylonLitMask);
	DOREPLIFETIME(ASQBoss, MarkTarget);
	DOREPLIFETIME(ASQBoss, MarkLoc);
	DOREPLIFETIME(ASQBoss, MarkLeft);
	DOREPLIFETIME(ASQBoss, TetherA);
	DOREPLIFETIME(ASQBoss, TetherB);
	DOREPLIFETIME(ASQBoss, bStaggered);
	DOREPLIFETIME(ASQBoss, bBurrowed);
	DOREPLIFETIME(ASQBoss, ZoneCenters);
	DOREPLIFETIME(ASQBoss, ZoneKinds);
	DOREPLIFETIME(ASQBoss, ZonesEndTime);
	DOREPLIFETIME(ASQBoss, ZoneRadius);
	DOREPLIFETIME(ASQBoss, bSweep);
	DOREPLIFETIME(ASQBoss, bSweepTwin);
	DOREPLIFETIME(ASQBoss, SweepCenter);
	DOREPLIFETIME(ASQBoss, SweepStartYaw);
	DOREPLIFETIME(ASQBoss, SweepStartTime);
	DOREPLIFETIME(ASQBoss, SweepRate);
	DOREPLIFETIME(ASQBoss, SweepLength);
	DOREPLIFETIME(ASQBoss, EntranceLeft);
	DOREPLIFETIME(ASQBoss, EntranceTotal);
}

void ASQBoss::BeginPlay()
{
	Super::BeginPlay();
	Body->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	Crown->SetStaticMesh(SQ::Mesh(TEXT("Cone")));
	EyeL->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	EyeR->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	EyeL->SetMaterial(0, SQ::ColorMID(this, FLinearColor(1.f, 0.3f, 0.1f)));
	EyeR->SetMaterial(0, SQ::ColorMID(this, FLinearColor(1.f, 0.3f, 0.1f)));
	Crown->SetMaterial(0, SQ::ColorMID(this, FLinearColor(0.9f, 0.75f, 0.2f)));

	if (GetNetMode() != NM_DedicatedServer)
	{
		// Pylons: a candle shrine (falls back to a pillar shape), glowing while held
		UStaticMesh* PylonAsset = SQArt::Prop(TEXT("shrine_candles"));
		const bool bPylonArt = PylonAsset != nullptr;
		if (!PylonAsset) PylonAsset = SQ::Mesh(TEXT("Cylinder"));
		for (UStaticMeshComponent* PylonComp : PylonMeshes)
		{
			if (!PylonComp) continue;
			PylonComp->SetStaticMesh(PylonAsset);
			UMaterialInstanceDynamic* PylonMat = bPylonArt ? SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor(1.f, 0.9f, 0.6f)) : SQ::ColorMID(this, FLinearColor(1.f, 0.85f, 0.4f));
			if (PylonMat) PylonComp->SetMaterial(0, PylonMat);
			PylonMIDs.Add(PylonMat);
			PylonComp->SetWorldScale3D(bPylonArt ? FVector(SQArt::FitScale(PylonAsset, 220.f)) : FVector(1.2f, 1.2f, 2.2f));
		}
		MarkDisc->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		MarkMID = SQ::ColorMID(this, FLinearColor(1.f, 0.1f, 0.2f));
		if (MarkMID) { MarkMID->SetScalarParameterValue(TEXT("Glow"), 1.5f); MarkDisc->SetMaterial(0, MarkMID); }
		TetherBeam->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		TetherMID = SQ::ColorMID(this, FLinearColor(0.3f, 1.f, 0.9f));
		if (TetherMID) { TetherMID->SetScalarParameterValue(TEXT("Glow"), 2.f); TetherBeam->SetMaterial(0, TetherMID); }
		HandMID = SQ::ColorMID(this, FLinearColor(1.f, 0.72f, 0.25f));
		if (HandMID) HandMID->SetScalarParameterValue(TEXT("Glow"), 3.f);
		for (UStaticMeshComponent* HandComp : { HandBeam.Get(), HandBeamTwin.Get() })
		{
			if (!HandComp) continue;
			HandComp->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
			if (HandMID) HandComp->SetMaterial(0, HandMID);
		}
	}
	ApplyVisuals();
}

void ASQBoss::InitBoss(FName InBossId, int32 InDefiance, int32 NumPlayers, float ExtraHealthMult)
{
	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(InBossId) : nullptr;
	BossId = InBossId;
	DefianceStacks = InDefiance;
	TeamSize = FMath::Max(1, NumPlayers);
	const float Base = Def ? Def->Health : 1000.f;
	// Co-op: HP x (1 + 0.75 x (players - 1)); abilities spread over the team instead of more HP sponge
	const float PerPlayer = D ? D->Tune(TEXT("BossHealthPerPlayer"), 0.75f) : 0.75f;
	MaxHealth = Base * (1.f + PerPlayer * (TeamSize - 1)) * (1.f + 0.25f * DefianceStacks) * ExtraHealthMult;
	Health = MaxHealth;
	BaseWalkSpeed = (Def ? Def->Speed : 250.f) * (1.f + 0.05f * DefianceStacks);
	BaseAccel = GetCharacterMovement()->MaxAcceleration;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	Stage = EStage::Gap;
	StageTime = 2.5f; // a breath after the intro line
	FightTime = 0.f;
	// Nov 25 2026: the boss climbs out of the lantern / the ground first (invulnerable, no abilities while it rises)
	EntranceTotal = D ? FMath::Clamp(D->Tune(TEXT("BossEntranceSeconds"), 3.f), 0.f, 10.f) : 3.f;
	EntranceLeft = EntranceTotal;
	if (EntranceLeft > 0.f)
	{
		bInvulnerable = true;
		MulticastAnim(4);
	}
	if (Def && Def->Elements.Num() > 0)
	{
		ElementIdx = 0;
		Element = SQBossElementFromName(Def->Elements[0]);
	}
	ApplyVisuals();
}

float ASQBoss::HitRadius() const
{
	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
	return 50.f * (Def ? Def->Scale : 3.f);
}

void ASQBoss::OnRep_BossId()
{
	ApplyVisuals();
}

void ASQBoss::OnRep_Phase()
{
	// Clients: the roar animation + burst (the multicast may be dropped; the phase never is)
	if (GetNetMode() == NM_DedicatedServer) return;
	PlayOneShot(4);
	const FVector L = GetActorLocation();
	SQFx::Ring(GetWorld(), FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 10.f), FLinearColor(1.f, 0.3f, 0.2f), 24, 900.f, 0.7f, 12.f);
	ApplyGlow();
}

void ASQBoss::OnRep_Element()
{
	ApplyGlow();
}

void ASQBoss::ApplyGlow()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
	if (!Def) return;
	// Brighter each phase; element phases recolour the body
	const float Glow = Def->Glow * (1.f + 0.6f * Phase) + (bStaggered ? 1.5f : 0.f);
	const FLinearColor Tint = SQArt::SoftTint(Element > 0 ? SQBossElementColour(Element) : Def->Color, 0.6f);
	if (ArtMID)
	{
		ArtMID->SetVectorParameterValue(TEXT("Color"), Tint);
		ArtMID->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
	CurGlow = Glow;
}

void ASQBoss::ApplyVisuals()
{
	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
	const float S = Def ? Def->Scale : 3.f;
	Body->SetRelativeLocation(FVector(0.f, 0.f, -160.f + 50.f * S));
	Body->SetRelativeScale3D(FVector(S));
	Crown->SetRelativeLocation(FVector(0.f, 0.f, -160.f + 100.f * S + 20.f));
	Crown->SetRelativeScale3D(FVector(S * 0.5f));
	EyeL->SetRelativeLocation(FVector(40.f * S, -15.f * S, -160.f + 60.f * S));
	EyeR->SetRelativeLocation(FVector(40.f * S, 15.f * S, -160.f + 60.f * S));
	EyeL->SetRelativeScale3D(FVector(0.18f * S));
	EyeR->SetRelativeScale3D(FVector(0.18f * S));
	if (Def) Body->SetMaterial(0, SQ::ColorMID(this, Def->Color));

	// Imported character art (falls back to the primitive body when missing)
	USkeletalMesh* ArtMesh = Def ? SQArt::Character(Def->Model) : nullptr;
	bArtVisuals = ArtMesh != nullptr;
	Body->SetVisibility(!bArtVisuals);
	Crown->SetVisibility(!bArtVisuals);
	EyeL->SetVisibility(!bArtVisuals);
	EyeR->SetVisibility(!bArtVisuals);
	Skin->SetVisibility(bArtVisuals);
	if (!bArtVisuals)
	{
		WeaponMesh->SetVisibility(false);
		PropMesh->SetVisibility(false);
		return;
	}

	ArtModel = Def->Model;
	if (Skin->GetSkeletalMeshAsset() != ArtMesh)
	{
		Skin->SetSkeletalMeshAsset(ArtMesh);
		OneShotLeft = 0.f;
	}
	const float ArtScale = SQArt::FitScale(ArtMesh, S * 100.f);
	const FBoxSphereBounds ArtBounds = ArtMesh->GetImportedBounds();
	const float ArtBottom = static_cast<float>(ArtBounds.Origin.Z - ArtBounds.BoxExtent.Z);
	// Root is the capsule centre; the floor is HalfHeight below it, so put the feet there
	const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	Skin->SetRelativeScale3D(FVector(ArtScale));
	Skin->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight - ArtBottom * ArtScale));
	SkinRestLoc = Skin->GetRelativeLocation();
	bSkinRestKnown = true;
	Skin->SetRelativeRotation(FRotator(0.f, SQArt::YawOffset(this), 0.f));

	ArtMID = SQArt::TintMID(this, SQArt::CharacterTexture(), SQArt::SoftTint(Def->Color, 0.6f));
	SQArt::SetAllMaterials(Skin, ArtMID);
	CurGlow = -1.f;
	ApplyGlow();

	UStaticMesh* WeaponAsset = SQArt::Weapon(Def->Weapon);
	const FName Hand = SQArt::HandSocket(Skin);
	if (WeaponAsset && !Hand.IsNone())
	{
		WeaponMesh->SetStaticMesh(WeaponAsset);
		SQArt::SetAllMaterials(WeaponMesh, ArtMID);
		WeaponMesh->AttachToComponent(Skin, FAttachmentTransformRules::KeepRelativeTransform, Hand);
		WeaponMesh->SetRelativeTransform(FTransform::Identity);
		{
			const float BossParentScale = FMath::Max(0.0001f, static_cast<float>(Skin->GetRelativeScale3D().X));
			const float BossHeight = Def ? Def->Scale * 100.f : 300.f;
			WeaponMesh->SetRelativeScale3D(FVector(SQArt::FitLongest(WeaponAsset, BossHeight * 0.65f) / BossParentScale));
		}
		WeaponMesh->SetVisibility(true);
	}
	else
	{
		WeaponMesh->SetVisibility(false);
	}

	// Signature prop (crown of candles, pumpkin, lantern...) on its bone
	UStaticMesh* PropAsset = SQArt::Prop(Def->Prop);
	FName PropBoneName = Def->PropBone.IsNone() ? FName(TEXT("head")) : Def->PropBone;
	if (PropAsset && !Skin->DoesSocketExist(PropBoneName)) PropBoneName = FName(*PropBoneName.ToString().Replace(TEXT("."), TEXT("_")));
	if (PropAsset && Skin->DoesSocketExist(PropBoneName))
	{
		PropMesh->SetStaticMesh(PropAsset);
		PropMID = SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Def->Accent, 0.35f));
		if (PropMID) { PropMID->SetScalarParameterValue(TEXT("Glow"), Def->Glow * 2.f); SQArt::SetAllMaterials(PropMesh, PropMID); }
		PropMesh->AttachToComponent(Skin, FAttachmentTransformRules::KeepRelativeTransform, PropBoneName);
		const float BossParentScale = FMath::Max(0.0001f, static_cast<float>(Skin->GetRelativeScale3D().X));
		PropMesh->SetRelativeTransform(FTransform::Identity);
		// Upright in world space whatever the bone's axes are (the bone only carries it)
		PropMesh->SetUsingAbsoluteRotation(true);
		PropMesh->SetRelativeScale3D(FVector(SQArt::FitLongest(PropAsset, Def->PropSize) / BossParentScale));
		PropMesh->SetVisibility(true);
	}
	else
	{
		PropMesh->SetVisibility(false);
	}

	LastArtLocation = GetActorLocation();
	if (OneShotLeft <= 0.f) UpdateArtAnim(0.f);
}

void ASQBoss::UpdateArtAnim(float DeltaSeconds)
{
	const FVector Loc = GetActorLocation();
	const float Moved = DeltaSeconds > UE_KINDA_SMALL_NUMBER ? static_cast<float>(FVector::Dist2D(Loc, LastArtLocation)) / DeltaSeconds : 0.f;
	LastArtLocation = Loc;
	if (PropMesh && PropMesh->IsVisible())
	{
		PropMesh->SetWorldRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
	}

	if (OneShotLeft > 0.f)
	{
		OneShotLeft -= DeltaSeconds;
		if (OneShotLeft > 0.f) return;
	}

	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
	const bool bMoving = FMath::Max(Moved, static_cast<float>(GetVelocity().Size2D())) > 25.f;
	UAnimSequence* Loop = nullptr;
	if (bMoving && Def)
	{
		for (const FName& MoveName : Def->MoveAnims)
		{
			Loop = SQArt::Anim(ArtModel, MoveName);
			if (Loop) break;
		}
	}
	if (bMoving && !Loop) Loop = SQArt::Anim(ArtModel, TEXT("Running_B"));
	if (!Loop && bStaggered) Loop = SQArt::Anim(ArtModel, TEXT("SQ_Enemy_Dizzy"));
	if (!Loop) Loop = SQArt::Anim(ArtModel, TEXT("Idle_Combat"));
	if (!Loop) Loop = SQArt::Anim(ArtModel, TEXT("Idle"));
	// Later phases move with more urgency
	SQArt::Play(Skin, Loop, true, 1.f + 0.12f * Phase);
}

void ASQBoss::PlayOneShot(uint8 Which)
{
	if (!bArtVisuals) return;
	const USQData* D = USQData::Get(this);
	const FName TakeName = SQBossAnimFor(ArtModel, Which, D ? D->FindBoss(BossId) : nullptr);
	UAnimSequence* OneShot = SQArt::Anim(ArtModel, TakeName);
	if (!OneShot) return;
	Skin->PlayAnimation(OneShot, false);
	Skin->SetPlayRate(1.f);
	// Death holds its last pose (the actor is destroyed shortly after)
	OneShotLeft = Which == 3 ? 1000.f : FMath::Max(0.1f, static_cast<float>(OneShot->GetPlayLength()));
}

void ASQBoss::MulticastAnim_Implementation(uint8 Which)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	PlayOneShot(Which);
}

ASQCharacter* ASQBoss::FindTarget() const
{
	ASQCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		if (It->bDowned) continue;
		const float Dist = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (Dist < BestDist) { BestDist = Dist; Best = *It; }
	}
	return Best;
}

void ASQBoss::GatherPlayers(TArray<ASQCharacter*>& Out) const
{
	Out.Reset();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->bDowned) Out.Add(*It);
	}
}

ASQCharacter* ASQBoss::PickVictim(bool bFurthest)
{
	TArray<ASQCharacter*> Standing;
	GatherPlayers(Standing);
	if (Standing.Num() == 0) return nullptr;
	const FVector L = GetActorLocation();
	ASQCharacter* Best = nullptr;
	float BestScore = -1.f;
	for (ASQCharacter* Cand : Standing)
	{
		// Furthest (charges) or random, with a big penalty for whoever the last ability picked
		float Score = bFurthest ? static_cast<float>(FVector::Dist2D(Cand->GetActorLocation(), L)) : FMath::FRandRange(0.f, 1000.f);
		if (Standing.Num() > 1 && Cand == LastVictim.Get()) Score *= 0.2f;
		if (Score > BestScore) { BestScore = Score; Best = Cand; }
	}
	LastVictim = Best;
	return Best;
}

float ASQBoss::GroundAtXY(float X, float Y) const
{
	return ASQArena::GroundZAt(this, X, Y);
}

float ASQBoss::CadenceMult() const
{
	// Defiance and later phases speed the rotation up; low HP enrages
	const float PhaseMult = Phase == 0 ? 1.f : (Phase == 1 ? 0.82f : 0.68f);
	const float Enrage = Health < MaxHealth * 0.2f ? 0.75f : 1.f;
	return FMath::Max(0.35f, 1.f - 0.08f * DefianceStacks) * PhaseMult * Enrage;
}

float ASQBoss::DamageMult() const
{
	// Remake balance pass: the mission's (and Hard mode's / Glass Reaper's) enemy damage multiplier reaches the boss too,
	// at BossMissionDamageShare strength (0.5 = its square root), so a Hard boss is not the normal boss with more HP
	const USQData* BossData = USQData::Get(this);
	const ASQGameState* BossGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	const float MissionShare = BossData ? FMath::Clamp(BossData->Tune(TEXT("BossMissionDamageShare"), 0.5f), 0.f, 1.f) : 0.f;
	const float MissionMult = BossGS ? FMath::Pow(FMath::Clamp(BossGS->ModProduct(&FSQModifierDef::EnemyDamageMult), 0.1f, 10.f), MissionShare) : 1.f;
	return (1.f + 0.15f * DefianceStacks) * (Health < MaxHealth * 0.2f ? 1.15f : 1.f) * MissionMult;
}

FColor ASQBoss::AbilityColour(ESQBossAbility Ability) const
{
	if (Element > 0 && Ability != ESQBossAbility::Pylons && Ability != ESQBossAbility::Tether) return SQBossElementColour(Element).ToFColor(true);
	return SQBossAbilityInfo(Ability).Colour;
}

FString ASQBoss::GetMechanicHint() const
{
	if (EntranceLeft > 0.f) return TEXT("IT RISES...");
	if (bBurrowed) return TEXT("BURROWING - keep moving, it erupts under you!");
	if (bSweep) return TEXT("CLOCK HANDS - JUMP them!");
	if (bInvulnerable) return FString::Printf(TEXT("PHASE %d - it roars (invulnerable)"), Phase + 1);
	if (bShielded)
	{
		const int32 NumPylons = Pylons.Num();
		int32 Held = 0;
		for (int32 PylonIdx = 0; PylonIdx < NumPylons; ++PylonIdx) if (PylonHeldMask & (1 << PylonIdx)) Held++;
		return FString::Printf(TEXT("SHIELDED - hold every pylon at once (%d/%d)  %d%%"), Held, NumPylons, FMath::RoundToInt(ShieldProgress * 100.f));
	}
	if (bStaggered) return TEXT("SHIELD BROKEN - hit it now!");
	if (MarkTarget) return TEXT("MARKED - stand in the red circle together to split the blast");
	if (TetherA) return TetherB ? TEXT("TETHERED - stay between close and far") : TEXT("LEASHED - stay near the boss, not on top of it");
	const FString Label = SQBossAbilityLabel(static_cast<ESQBossAbility>(ActiveAbility));
	if (Element > 0)
	{
		static const TCHAR* const ElementNames[4] = { TEXT(""), TEXT("FIRE"), TEXT("ICE"), TEXT("LIGHTNING") };
		return Label.IsEmpty() ? FString(ElementNames[Element & 3]) : FString::Printf(TEXT("%s  -  %s"), ElementNames[Element & 3], *Label);
	}
	return Label;
}

void ASQBoss::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Clay breathing (stepped like stop-motion)
	VisualTime += DeltaSeconds;
	const float Stepped = FMath::FloorToFloat(VisualTime * 12.f) / 12.f;
	const float Breath = 1.f + 0.04f * FMath::Sin(Stepped * 4.f);
	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
	const float S = Def ? Def->Scale : 3.f;
	Body->SetRelativeScale3D(FVector(S * Breath, S * Breath, S / Breath));
	if (EntranceLeft > 0.f) EntranceLeft = FMath::Max(0.f, EntranceLeft - DeltaSeconds); // every machine eases the rise between updates
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (bArtVisuals) UpdateArtAnim(DeltaSeconds);
		UpdateMechanicVisuals(DeltaSeconds);
		UpdateMapVisuals(DeltaSeconds);
	}

	if (!HasAuthority() || bDead || !Def) return;
	// Entrance: rising, invulnerable, nothing else yet
	if (EntranceTotal > 0.f)
	{
		if (EntranceLeft > 0.f) return;
		EntranceTotal = 0.f;
		bInvulnerable = false;
	}
	FightTime += DeltaSeconds;

	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const FVector L = GetActorLocation();

	// Safety: fell through the terrain or got outside the arena -> back on the ground near the lantern side of the map
	{
		const ASQArena* BossArena = GS ? GS->Arena.Get() : nullptr;
		const float BossHalf = D->Tune(TEXT("ArenaHalfSize"), 6000.f);
		const bool bBossOut = FMath::Abs(L.X) > BossHalf || FMath::Abs(L.Y) > BossHalf;
		const bool bBossBelow = BossArena && BossArena->HasTerrain() && L.Z < BossArena->MinGroundZ() - 60.f /* capsule centre >= MinZ+160 on real ground; safety floor top is MinZ-300 */;
		if (bBossOut || bBossBelow)
		{
			const float BossInner = BossHalf - 400.f;
			const float SafeBossX = FMath::Clamp(static_cast<float>(L.X), -BossInner, BossInner);
			const float SafeBossY = FMath::Clamp(static_cast<float>(L.Y), -BossInner, BossInner);
			const float BossHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			SetActorLocation(FVector(SafeBossX, SafeBossY, ASQArena::GroundZAt(this, SafeBossX, SafeBossY) + BossHalfHeight + 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			GetCharacterMovement()->Velocity = FVector::ZeroVector;
			BossStuckTime = 0.f;
			return;
		}
	}

	ServerPhase(*Def);
	ServerHazards(DeltaSeconds);
	TickMapPassives(DeltaSeconds, *Def);

	// Roar (phase change) and stagger (shield broken): no movement, no abilities
	if (RoarLeft > 0.f)
	{
		RoarLeft -= DeltaSeconds;
		if (RoarLeft <= 0.f) bInvulnerable = false;
		return;
	}
	if (StaggerLeft > 0.f)
	{
		StaggerLeft -= DeltaSeconds;
		if (StaggerLeft <= 0.f) bStaggered = false;
		return;
	}

	// Walk toward the nearest reaper between abilities (planted while winding up; charges move on their own)
	ASQCharacter* Target = FindTarget();
	const bool bPlanted = Stage == EStage::Windup || (Stage == EStage::Active && ActiveAbility != static_cast<uint8>(ESQBossAbility::Charge));
	if (Target && !bPlanted)
	{
		const FVector To = Target->GetActorLocation() - L;
		if (To.Size2D() > 350.f)
		{
			// Top-down maps: layout walls block the boss, so bend round the wall ahead instead of pressing into it
			FVector2D ChaseDir = FVector2D(To.X, To.Y).GetSafeNormal();
			const ASQArena* ChaseArena = GS ? GS->Arena.Get() : nullptr;
			const float ChaseRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
			if (ChaseArena) ChaseDir = ChaseArena->SteerAroundWalls(FVector2D(L.X, L.Y), ChaseDir, ChaseRadius, 300.f + ChaseRadius);
			AddMovementInput(FVector(ChaseDir.X, ChaseDir.Y, 0.f), 1.f);
			// Stuck against a plateau cliff / prop while the target is out of reach: hop toward the target after a while
			const bool bBarelyMoving = GetVelocity().Size2D() < 30.f;
			BossStuckTime = bBarelyMoving ? BossStuckTime + DeltaSeconds : 0.f;
			if (BossStuckTime > 3.f)
			{
				BossStuckTime = 0.f;
				FVector2D Hop = FVector2D(L.X, L.Y) + FVector2D(To.X, To.Y).GetSafeNormal() * FMath::Min(600.f, static_cast<float>(To.Size2D()) - 300.f);
				if (ChaseArena) ChaseArena->ResolveWallOverlap(Hop, ChaseRadius); // never hop into a layout wall
				const float BossHopHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const FVector HopTo(Hop.X, Hop.Y, ASQArena::GroundZAt(this, static_cast<float>(Hop.X), static_cast<float>(Hop.Y)) + BossHopHalfHeight + 20.f);
				TeleportTo(HopTo, GetActorRotation(), false, false); // finds a free spot (no overlap with props)
				MulticastFX(0, FVector(Hop.X, Hop.Y, HopTo.Z - BossHopHalfHeight), HitRadius() * 1.5f, FColor(120, 80, 160), 0.35f);
			}
		}
		else
		{
			BossStuckTime = 0.f;
			const FRotator Face(0.f, To.Rotation().Yaw, 0.f);
			SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Face, DeltaSeconds, 240.f));
		}
	}
	else if (Target && Stage == EStage::Windup)
	{
		// Face the victim (or the target) while winding up so the tell reads
		const ASQCharacter* FaceAt = Victim.IsValid() ? Victim.Get() : Target;
		const FVector To = FaceAt->GetActorLocation() - L;
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, To.Rotation().Yaw, 0.f), DeltaSeconds, 360.f));
	}

	ServerAbilities(DeltaSeconds, *Def);

	// Contact damage
	ContactCd -= DeltaSeconds;
	if (ContactCd <= 0.f)
	{
		ContactCd = 1.f;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= HitRadius() + 60.f) It->ApplyHit(15.f * DamageMult());
		}
	}
}

// ================================================================ server: phases + abilities

void ASQBoss::ServerPhase(const FSQBossDef& Def)
{
	const float Frac = MaxHealth > 0.f ? Health / MaxHealth : 1.f;
	const uint8 Wanted = Frac <= 0.33f ? 2 : (Frac <= 0.66f ? 1 : 0);
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (Wanted > Phase)
	{
		// Phase change: cancel what it was doing, roar (invulnerable), unlock the next ability set, speed up
		EndAbility();
		ClearMechanics();
		Phase = Wanted;
		bInvulnerable = true;
		RoarLeft = RoarSeconds();
		Stage = EStage::Gap;
		StageTime = 0.8f;
		MulticastAnim(4);
		const FVector L = GetActorLocation();
		MulticastFX(0, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 900.f, FColor(255, 80, 60), 0.6f);
		if (GS) GS->PushAlert(FString::Printf(TEXT("%s enters PHASE %d!"), *Def.Name, Phase + 1));
		if (HasAuthority() && GetNetMode() != NM_DedicatedServer) OnRep_Phase(); // listen-server host
	}
	if (!bEnragedAnnounced && Frac <= 0.2f)
	{
		bEnragedAnnounced = true;
		if (GS) GS->PushAlert(FString::Printf(TEXT("%s is ENRAGED!"), *Def.Name));
	}

	// Element phases (~30 s each, cycling)
	if (Def.Elements.Num() > 0)
	{
		ElementTime += GetWorld()->GetDeltaSeconds();
		const USQData* D = USQData::Get(this);
		const float ElementSeconds = D ? D->Tune(TEXT("BossElementSeconds"), 30.f) : 30.f;
		if (ElementTime >= ElementSeconds)
		{
			ElementTime = 0.f;
			ElementIdx = (ElementIdx + 1) % Def.Elements.Num();
			Element = SQBossElementFromName(Def.Elements[ElementIdx]);
			if (GS)
			{
				static const TCHAR* const ElementWords[4] = { TEXT("shadow"), TEXT("FIRE"), TEXT("ICE"), TEXT("LIGHTNING") };
				GS->PushAlert(FString::Printf(TEXT("%s turns to %s!"), *Def.Name, ElementWords[Element & 3]));
			}
			if (GetNetMode() != NM_DedicatedServer) OnRep_Element();
		}
	}
}

ESQBossAbility ASQBoss::PickAbility(const FSQBossDef& Def) const
{
	TArray<FSQBossAbilityPick, TInlineAllocator<16>> Pool;
	if (Def.Abilities.Num() > 0) Pool.Append(Def.Abilities);
	else
	{
		// Old bosses with no Abilities column: the original slam / ring / summon
		FSQBossAbilityPick Pick;
		Pick.Ability = ESQBossAbility::Slam; Pool.Add(Pick);
		Pick.Ability = ESQBossAbility::Ring; Pool.Add(Pick);
		Pick.Ability = ESQBossAbility::Summon; Pool.Add(Pick);
	}
	if (Phase >= 1) Pool.Append(Def.Phase2);
	if (Phase >= 2) Pool.Append(Def.Phase3);

	float Total = 0.f;
	TArray<float, TInlineAllocator<16>> Weights;
	for (const FSQBossAbilityPick& Pick : Pool)
	{
		const int32 Slot = static_cast<int32>(Pick.Ability);
		float W = Pick.Weight * SQBossElementBias(Element, Pick.Ability);
		if (Slot >= 0 && Slot < MaxAbilitySlots && AbilityCd[Slot] > 0.f) W = 0.f;
		if (Pick.Ability == LastAbility && Pool.Num() > 1) W *= 0.15f;
		if (Pick.Ability == ESQBossAbility::Summon && Def.SummonId.IsNone()) W = 0.f;
		Weights.Add(W);
		Total += W;
	}
	if (Total <= 0.f) return ESQBossAbility::None;
	float Roll = FMath::FRandRange(0.f, Total);
	for (int32 PickIdx = 0; PickIdx < Pool.Num(); ++PickIdx)
	{
		Roll -= Weights[PickIdx];
		if (Roll <= 0.f && Weights[PickIdx] > 0.f) return Pool[PickIdx].Ability;
	}
	return Pool.Last().Ability;
}

void ASQBoss::ServerAbilities(float DeltaSeconds, const FSQBossDef& Def)
{
	for (float& Cd : AbilityCd) Cd -= DeltaSeconds;

	switch (Stage)
	{
	case EStage::Gap:
	{
		StageTime -= DeltaSeconds;
		if (StageTime > 0.f) break;
		const ESQBossAbility Next = PickAbility(Def);
		if (Next == ESQBossAbility::None) { StageTime = 0.5f; break; }
		BeginAbility(Next, Def);
		break;
	}
	case EStage::Windup:
		StageTime -= DeltaSeconds;
		if (StageTime <= 0.f)
		{
			ResolveAbility(Def);
		}
		break;
	case EStage::Active:
		ActiveTime += DeltaSeconds;
		if (TickActiveAbility(DeltaSeconds, Def)) EndAbility();
		break;
	default:
		break;
	}
}

void ASQBoss::BeginAbility(ESQBossAbility Ability, const FSQBossDef& Def)
{
	const FSQBossAbilityInfo Info = SQBossAbilityInfo(Ability);
	const int32 Slot = static_cast<int32>(Ability);
	float Cooldown = Info.Cooldown;
	if (Ability == ESQBossAbility::Summon) Cooldown = Def.SummonCooldown;
	if (Ability == ESQBossAbility::Ring) Cooldown = Def.RingCooldown;
	if (Ability == ESQBossAbility::Slam) Cooldown = Def.SlamCooldown;
	if (Slot >= 0 && Slot < MaxAbilitySlots) AbilityCd[Slot] = Cooldown * CadenceMult();
	LastAbility = Ability;
	ActiveAbility = static_cast<uint8>(Ability);
	Stage = EStage::Windup;
	StageTime = Info.Windup;
	ActiveTime = 0.f;
	PulseTimer = 0.f;
	PulseCount = 0;
	MulticastAnim(Info.Anim);

	const FVector L = GetActorLocation();
	const FColor Colour = AbilityColour(Ability);
	ASQCharacter* Picked = nullptr;
	switch (Ability)
	{
	case ESQBossAbility::Slam:
	{
		Picked = PickVictim(false);
		AbilityPoint = Picked ? Picked->GetActorLocation() : L;
		AbilityPoint.Z = GroundAtXY(static_cast<float>(AbilityPoint.X), static_cast<float>(AbilityPoint.Y)) + 5.f;
		MulticastFX(1, AbilityPoint, Def.SlamRadius, Colour, Info.Windup);
		break;
	}
	case ESQBossAbility::Shockwave:
	case ESQBossAbility::Vortex:
	case ESQBossAbility::Spiral:
	case ESQBossAbility::Ring:
		AbilityPoint = FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f);
		MulticastFX(1, AbilityPoint, Ability == ESQBossAbility::Vortex ? 1800.f : HitRadius() * 2.2f, Colour, Info.Windup);
		break;
	case ESQBossAbility::Fan:
		Picked = PickVictim(false);
		AbilityPoint = Picked ? Picked->GetActorLocation() : L + GetActorForwardVector() * 600.f;
		MulticastFX(1, FVector(AbilityPoint.X, AbilityPoint.Y, GroundAtXY(static_cast<float>(AbilityPoint.X), static_cast<float>(AbilityPoint.Y)) + 5.f), 160.f, Colour, Info.Windup);
		break;
	case ESQBossAbility::Charge:
	{
		// Charges the reaper furthest away (punishes the one who stayed back); the lane is marked
		Picked = PickVictim(true);
		const FVector Goal = Picked ? Picked->GetActorLocation() : L + GetActorForwardVector() * 1000.f;
		AbilityDir = (Goal - L).GetSafeNormal2D();
		if (AbilityDir.IsNearlyZero()) AbilityDir = GetActorForwardVector();
		const float LaneLen = FMath::Clamp(static_cast<float>(FVector::Dist2D(Goal, L)) + 300.f, 600.f, 2400.f);
		for (int32 DiscIdx = 1; DiscIdx <= 6; ++DiscIdx)
		{
			const FVector At = L + AbilityDir * (LaneLen * DiscIdx / 6.f);
			MulticastFX(1, FVector(At.X, At.Y, GroundAtXY(static_cast<float>(At.X), static_cast<float>(At.Y)) + 5.f), HitRadius() + 60.f, Colour, Info.Windup);
		}
		AbilityPoint = L + AbilityDir * LaneLen;
		break;
	}
	case ESQBossAbility::Summon:
		AbilityPoint = L;
		MulticastFX(1, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 600.f, Colour, Info.Windup);
		break;
	case ESQBossAbility::Pools:
	{
		// A pool under every standing reaper (marked now, poured on resolve)
		TArray<ASQCharacter*> Standing;
		GatherPlayers(Standing);
		for (ASQCharacter* Reaper : Standing)
		{
			FVector At = Reaper->GetActorLocation();
			At.Z = GroundAtXY(static_cast<float>(At.X), static_cast<float>(At.Y)) + 5.f;
			FBossStrike Strike;
			Strike.Center = At;
			Strike.Radius = 260.f;
			Strike.Delay = Info.Windup;
			Strike.Damage = 0.f; // the pool does the damage
			Strike.Colour = Colour;
			Strikes.Add(Strike);
			MulticastFX(1, At, 260.f, Colour, Info.Windup);
		}
		break;
	}
	case ESQBossAbility::Mark:
	{
		Picked = PickVictim(false);
		MarkTarget = Picked;
		MarkLoc = Picked ? Picked->GetActorLocation() : L;
		MarkLeft = 6.f + Info.Windup;
		break;
	}
	case ESQBossAbility::Pylons:
	{
		// Shield + pylons around the arena centre side of the boss; co-op: one per reaper (2-3), held at the same time
		TArray<ASQCharacter*> Standing;
		GatherPlayers(Standing);
		const int32 NumPylons = FMath::Clamp(Standing.Num(), 2, 3);
		const USQData* D = USQData::Get(this);
		const float ArenaHalf = D ? D->Tune(TEXT("ArenaHalfSize"), 6000.f) - 500.f : 5500.f;
		const float Ring = D ? D->Tune(TEXT("BossPylonRing"), 1300.f) : 1300.f;
		const float StartAngle = FMath::FRandRange(0.f, 2.f * PI);
		Pylons.Reset();
		for (int32 PylonIdx = 0; PylonIdx < NumPylons; ++PylonIdx)
		{
			const float A = StartAngle + PylonIdx * 2.f * PI / NumPylons;
			const float PX = FMath::Clamp(static_cast<float>(L.X) + FMath::Cos(A) * Ring, -ArenaHalf, ArenaHalf);
			const float PY = FMath::Clamp(static_cast<float>(L.Y) + FMath::Sin(A) * Ring, -ArenaHalf, ArenaHalf);
			Pylons.Add(FVector(PX, PY, GroundAtXY(PX, PY)));
			MulticastFX(1, FVector(PX, PY, GroundAtXY(PX, PY) + 5.f), 260.f, Colour, Info.Windup + 0.5f);
		}
		PylonHeldMask = 0;
		PylonLitMask = 0;
		ShieldProgress = 0.f;
		for (float& Hold : PylonHold) Hold = 0.f;
		break;
	}
	case ESQBossAbility::Tether:
	{
		TArray<ASQCharacter*> Standing;
		GatherPlayers(Standing);
		if (Standing.Num() >= 2)
		{
			const int32 First = FMath::RandRange(0, Standing.Num() - 1);
			int32 Second = FMath::RandRange(0, Standing.Num() - 2);
			if (Second >= First) Second++;
			TetherA = Standing[First];
			TetherB = Standing[Second];
		}
		else
		{
			// Solo fallback: a leash to the boss itself
			TetherA = Standing.Num() > 0 ? Standing[0] : nullptr;
			TetherB = nullptr;
		}
		break;
	}
	default:
		BeginMapAbility(Ability, Def, Info.Windup);
		break;
	}
	if (Picked) Victim = Picked;
}

void ASQBoss::ResolveAbility(const FSQBossDef& Def)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQHordeManager* Horde = GS ? GS->Horde.Get() : nullptr;
	const ESQBossAbility Ability = static_cast<ESQBossAbility>(ActiveAbility);
	const float Dmg = DamageMult();
	const FVector L = GetActorLocation();
	const FVector2D L2(L.X, L.Y);
	Stage = EStage::Active;
	ActiveTime = 0.f;
	PulseTimer = 0.f;
	PulseCount = 0;

	switch (Ability)
	{
	case ESQBossAbility::Slam:
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), AbilityPoint) <= Def.SlamRadius) It->ApplyHit(Def.SlamDamage * Dmg);
		}
		MulticastFX(0, AbilityPoint, Def.SlamRadius, FColor(255, 120, 40), 0.3f);
		break;
	case ESQBossAbility::Ring:
		if (Horde)
		{
			const int32 Count = Def.RingCount + 2 * Phase;
			const float Offset = FMath::FRandRange(0.f, PI);
			for (int32 ShotIdx = 0; ShotIdx < Count; ++ShotIdx)
			{
				const float A = Offset + ShotIdx * 2.f * PI / FMath::Max(1, Count);
				const FVector2D ShotDir(FMath::Cos(A), FMath::Sin(A));
				Horde->SpawnProjectile(L2 + ShotDir * HitRadius(), ShotDir * 650.f, Def.RingDamage * Dmg, 35.f, 4.f, 1, true, nullptr);
			}
		}
		break;
	case ESQBossAbility::Shockwave:
	{
		// 2-3 rings rolling outward along the ground: jump them (or stand far away)
		const int32 NumWaves = 2 + (Phase >= 1 ? 1 : 0);
		for (int32 WaveIdx = 0; WaveIdx < NumWaves; ++WaveIdx)
		{
			FBossWave Wave;
			Wave.Center = AbilityPoint;
			Wave.MaxRadius = 1400.f + 200.f * Phase;
			Wave.Speed = Element == 2 ? 650.f : 850.f;
			Wave.Damage = Def.SlamDamage * 0.7f * Dmg;
			Wave.Delay = WaveIdx * 0.55f;
			Waves.Add(Wave);
		}
		break;
	}
	case ESQBossAbility::Summon:
		if (Horde)
		{
			Horde->SpawnEnemyNear(Def.SummonId, L2, Def.SummonCount + 2 * Phase, 600.f);
			// Bosses.csv SummonFlyer / SummonFlyerCount: a few flyers join every summon (+1 per phase)
			if (!Def.SummonFlyer.IsNone() && Def.SummonFlyerCount > 0) Horde->SpawnEnemyNear(Def.SummonFlyer, L2, Def.SummonFlyerCount + Phase, 700.f);
		}
		break;
	case ESQBossAbility::Pools:
		break; // strikes queued at the wind-up become pools when they land (ServerHazards)
	case ESQBossAbility::Charge:
		ChargeHit.Reset();
		GetCharacterMovement()->MaxWalkSpeed = FMath::Min(2400.f, BaseWalkSpeed * 5.f);
		GetCharacterMovement()->MaxAcceleration = 20000.f;
		break;
	default:
		ResolveMapAbility(Ability, Def);
		break;
	}
}

bool ASQBoss::TickActiveAbility(float DeltaSeconds, const FSQBossDef& Def)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQHordeManager* Horde = GS ? GS->Horde.Get() : nullptr;
	const ESQBossAbility Ability = static_cast<ESQBossAbility>(ActiveAbility);
	const float Dmg = DamageMult();
	const FVector L = GetActorLocation();
	const FVector2D L2(L.X, L.Y);
	const USQData* D = USQData::Get(this);

	switch (Ability)
	{
	case ESQBossAbility::Spiral:
	{
		// Two arms of shots sweeping round for 2.4 s
		if (!Horde) return true;
		PulseTimer -= DeltaSeconds;
		while (PulseTimer <= 0.f && ActiveTime < 2.4f)
		{
			PulseTimer += 0.09f;
			SpiralAngle += 0.33f;
			const int32 Arms = 2 + (Phase >= 2 ? 1 : 0);
			for (int32 ArmIdx = 0; ArmIdx < Arms; ++ArmIdx)
			{
				const float A = SpiralAngle + ArmIdx * 2.f * PI / Arms;
				const FVector2D ShotDir(FMath::Cos(A), FMath::Sin(A));
				Horde->SpawnProjectile(L2 + ShotDir * HitRadius(), ShotDir * 560.f, Def.RingDamage * 0.8f * Dmg, 32.f, 4.f, 1, true, nullptr);
			}
		}
		return ActiveTime >= 2.4f;
	}
	case ESQBossAbility::Fan:
	{
		// Three aimed volleys of 7 at the victim
		if (!Horde) return true;
		PulseTimer -= DeltaSeconds;
		if (PulseTimer <= 0.f && PulseCount < 3)
		{
			PulseTimer = 0.35f;
			PulseCount++;
			const FVector Aim = Victim.IsValid() ? Victim->GetActorLocation() : AbilityPoint;
			const FVector2D AimDir = FVector2D(Aim.X - L.X, Aim.Y - L.Y).GetSafeNormal();
			for (int32 ShotIdx = -3; ShotIdx <= 3; ++ShotIdx)
			{
				const FVector2D ShotDir = AimDir.GetRotated(ShotIdx * 9.f);
				Horde->SpawnProjectile(L2 + ShotDir * HitRadius(), ShotDir * 760.f, Def.RingDamage * Dmg, 30.f, 3.f, 1, true, nullptr);
			}
		}
		return PulseCount >= 3 && PulseTimer <= 0.f;
	}
	case ESQBossAbility::Charge:
	{
		AddMovementInput(AbilityDir, 1.f);
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Reaper = *It;
			if (Reaper->bDowned || ChargeHit.Contains(Reaper)) continue;
			if (FVector::Dist2D(Reaper->GetActorLocation(), L) <= HitRadius() + 70.f)
			{
				ChargeHit.Add(Reaper);
				Reaper->ApplyHit(Def.SlamDamage * 1.2f * Dmg);
				MulticastFX(0, Reaper->GetActorLocation(), 200.f, FColor(255, 60, 40), 0.25f);
			}
		}
		const bool bArrived = FVector::Dist2D(L, AbilityPoint) < 150.f;
		if (ActiveTime >= 0.9f || bArrived)
		{
			GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
			GetCharacterMovement()->MaxAcceleration = BaseAccel;
			MulticastFX(0, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), HitRadius() * 2.f, FColor(200, 160, 120), 0.3f);
			return true;
		}
		return false;
	}
	case ESQBossAbility::Vortex:
	{
		// 3.5 s pull toward the boss with a damaging core: run outward
		PulseTimer -= DeltaSeconds;
		if (PulseTimer <= 0.f)
		{
			PulseTimer = 0.2f;
			const float Pull = D ? D->Tune(TEXT("BossVortexPull"), 260.f) : 260.f;
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				ASQCharacter* Reaper = *It;
				if (Reaper->bDowned) continue;
				const FVector To = L - Reaper->GetActorLocation();
				const float Dist = static_cast<float>(To.Size2D());
				if (Dist > 1800.f) continue;
				Reaper->LaunchCharacter(To.GetSafeNormal2D() * Pull * (1.f - 0.4f * Dist / 1800.f), false, false);
				if (Dist < HitRadius() + 250.f && (PulseCount % 3) == 0) Reaper->ApplyHit(Def.SlamDamage * 0.35f * Dmg);
			}
			PulseCount++;
			if (PulseCount % 3 == 0) MulticastFX(0, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 1800.f, AbilityColour(Ability), 0.5f);
		}
		return ActiveTime >= 3.5f;
	}
	case ESQBossAbility::Mark:
	{
		// The circle chases its reaper (slower than a run); when time is up everyone inside shares the blast
		ASQCharacter* Marked = MarkTarget.Get();
		MarkLeft -= DeltaSeconds;
		if (Marked && !Marked->bDowned)
		{
			const FVector To = Marked->GetActorLocation() - FVector(MarkLoc);
			const float Step = (D ? D->Tune(TEXT("BossMarkSpeed"), 480.f) : 480.f) * DeltaSeconds;
			MarkLoc = FVector(MarkLoc) + To.GetSafeNormal2D() * FMath::Min(Step, static_cast<float>(To.Size2D()));
		}
		if (MarkLeft > 0.f) return false;
		const float MarkRadius = D ? D->Tune(TEXT("BossMarkRadius"), 300.f) : 300.f;
		TArray<ASQCharacter*> Inside;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->bDowned && FVector::Dist2D(It->GetActorLocation(), MarkLoc) <= MarkRadius) Inside.Add(*It);
		}
		const float Blast = Def.SlamDamage * 2.2f * Dmg;
		// Co-op: split between everyone inside; alone in a team = 1.5x (help them!); solo = 1x (outrun it)
		const float Each = Inside.Num() >= 2 ? Blast / Inside.Num() : (TeamSize >= 2 ? Blast * 1.5f : Blast);
		for (ASQCharacter* Reaper : Inside) Reaper->ApplyHit(Each);
		const FVector MarkAt(MarkLoc.X, MarkLoc.Y, GroundAtXY(static_cast<float>(MarkLoc.X), static_cast<float>(MarkLoc.Y)) + 5.f);
		MulticastFX(0, MarkAt, MarkRadius, FColor(255, 40, 70), 0.4f);
		MarkTarget = nullptr;
		MarkLeft = 0.f;
		return true;
	}
	case ESQBossAbility::Pylons:
	{
		if (!bShielded && ActiveTime < 0.1f)
		{
			bShielded = true;
			ShieldTimeLeft = D ? D->Tune(TEXT("BossShieldSeconds"), 25.f) : 25.f;
			if (GS) GS->PushAlert(TEXT("The boss raises a shield! Hold every pylon AT THE SAME TIME to break it!"));
		}
		ShieldTimeLeft -= DeltaSeconds;
		const float HoldRange = D ? D->Tune(TEXT("BossPylonRange"), 260.f) : 260.f;
		TArray<ASQCharacter*> Standing;
		GatherPlayers(Standing);
		uint8 Held = 0;
		for (int32 PylonIdx = 0; PylonIdx < Pylons.Num() && PylonIdx < 4; ++PylonIdx)
		{
			for (ASQCharacter* Reaper : Standing)
			{
				if (FVector::Dist2D(Reaper->GetActorLocation(), Pylons[PylonIdx]) <= HoldRange) { Held |= static_cast<uint8>(1 << PylonIdx); break; }
			}
		}
		PylonHeldMask = Held;
		const uint8 AllMask = static_cast<uint8>((1 << Pylons.Num()) - 1);
		if (Standing.Num() >= Pylons.Num())
		{
			// Co-op: everyone on a pylon at once fills the bar; letting go drains it
			const float Need = D ? D->Tune(TEXT("BossPylonHoldSeconds"), 2.5f) : 2.5f;
			ShieldProgress = Held == AllMask ? ShieldProgress + DeltaSeconds / Need : FMath::Max(0.f, ShieldProgress - DeltaSeconds * 0.25f);
		}
		else
		{
			// Solo / short-handed fallback: light the pylons one after another (each 1.2 s), lit ones stay lit
			for (int32 PylonIdx = 0; PylonIdx < Pylons.Num() && PylonIdx < 4; ++PylonIdx)
			{
				if (Held & (1 << PylonIdx)) PylonHold[PylonIdx] += DeltaSeconds;
				if (PylonHold[PylonIdx] >= 1.2f) PylonLitMask |= static_cast<uint8>(1 << PylonIdx);
			}
			int32 Lit = 0;
			for (int32 PylonIdx = 0; PylonIdx < Pylons.Num(); ++PylonIdx) if (PylonLitMask & (1 << PylonIdx)) Lit++;
			ShieldProgress = Pylons.Num() > 0 ? static_cast<float>(Lit) / Pylons.Num() : 1.f;
		}
		if (ShieldProgress >= 1.f)
		{
			// Broken: stunned and vulnerable
			bShielded = false;
			bStaggered = true;
			StaggerLeft = D ? D->Tune(TEXT("BossStaggerSeconds"), 4.f) : 4.f;
			MulticastAnim(4);
			MulticastFX(0, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 700.f, FColor(255, 230, 120), 0.5f);
			if (GS) GS->PushAlert(TEXT("SHIELD BROKEN! It's stunned - hit it!"));
			Pylons.Reset();
			if (GetNetMode() != NM_DedicatedServer) ApplyGlow();
			return true;
		}
		if (ShieldTimeLeft <= 0.f)
		{
			// Failed: the shield drops on its own, but the boss heals
			bShielded = false;
			Health = FMath::Min(MaxHealth, Health + MaxHealth * 0.08f);
			if (GS) GS->PushAlert(TEXT("The pylons went dark... the boss drank their power."));
			Pylons.Reset();
			return true;
		}
		return false;
	}
	case ESQBossAbility::Tether:
	{
		// 8 s: tethered reapers (solo: reaper and boss) take damage while too close or too far apart
		ASQCharacter* EndA = TetherA.Get();
		if (!EndA || EndA->bDowned || ActiveTime >= 8.f)
		{
			TetherA = nullptr;
			TetherB = nullptr;
			return true;
		}
		const FVector OtherEnd = TetherB ? TetherB->GetActorLocation() : L;
		const float Dist = static_cast<float>(FVector::Dist2D(EndA->GetActorLocation(), OtherEnd));
		const float MinD = TetherB ? 250.f : HitRadius() + 200.f;
		const float MaxD = TetherB ? 1400.f : 1300.f;
		TetherTick -= DeltaSeconds;
		if (TetherTick <= 0.f)
		{
			TetherTick = 0.5f;
			if (Dist < MinD || Dist > MaxD)
			{
				const float Hurt = Def.RingDamage * 0.6f * Dmg;
				EndA->ApplyHit(Hurt);
				if (TetherB && !TetherB->bDowned) TetherB->ApplyHit(Hurt);
			}
		}
		return false;
	}
	default:
	{
		// Nov 25 map abilities run in SQBossMap.cpp; Slam, Ring, Shockwave, Summon, Pools: done when resolved (ServerHazards)
		bool bMapHandled = false;
		const bool bMapDone = TickMapAbility(Ability, DeltaSeconds, Def, bMapHandled);
		if (bMapHandled) return bMapDone;
		return ActiveTime >= 0.4f;
	}
	}
}

void ASQBoss::EndAbility()
{
	if (Stage == EStage::Active && ActiveAbility == static_cast<uint8>(ESQBossAbility::Charge))
	{
		GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
		GetCharacterMovement()->MaxAcceleration = BaseAccel;
	}
	Stage = EStage::Gap;
	const USQData* D = USQData::Get(this);
	StageTime = (D ? D->Tune(TEXT("BossAbilityGap"), 1.4f) : 1.4f) * CadenceMult();
	ActiveAbility = 0;
	Victim = nullptr;
}

void ASQBoss::ClearMechanics()
{
	MarkTarget = nullptr;
	MarkLeft = 0.f;
	TetherA = nullptr;
	TetherB = nullptr;
	if (bShielded) bShielded = false;
	Pylons.Reset();
	PylonHeldMask = 0;
	PylonLitMask = 0;
	ShieldProgress = 0.f;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	GetCharacterMovement()->MaxAcceleration = BaseAccel;
	// Map mechanics: back above ground, zones / hands / ships gone
	if (bBurrowed)
	{
		bBurrowed = false;
		SetActorEnableCollision(true);
		if (EntranceLeft <= 0.f) bInvulnerable = false;
	}
	bSweep = false;
	bSweepTwin = false;
	ZoneCenters.Reset();
	ZoneKinds.Reset();
	Ships.Reset();
	RaiseSpots.Reset();
}

void ASQBoss::ServerHazards(float DeltaSeconds)
{
	const USQData* D = USQData::Get(this);
	const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
	if (!Def) return;
	const float Dmg = DamageMult();

	// Shockwaves: a ring hits each reaper once as it passes, unless they are in the air
	for (int32 WaveIdx = Waves.Num() - 1; WaveIdx >= 0; --WaveIdx)
	{
		FBossWave& Wave = Waves[WaveIdx];
		if (Wave.Delay > 0.f)
		{
			Wave.Delay -= DeltaSeconds;
			if (Wave.Delay <= 0.f) MulticastFX(0, Wave.Center, Wave.MaxRadius, AbilityColour(ESQBossAbility::Shockwave), Wave.MaxRadius / Wave.Speed);
			continue;
		}
		Wave.Radius += Wave.Speed * DeltaSeconds;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Reaper = *It;
			if (Reaper->bDowned || Wave.Hit.Contains(Reaper)) continue;
			const float Dist = static_cast<float>(FVector::Dist2D(Reaper->GetActorLocation(), Wave.Center));
			if (FMath::Abs(Dist - Wave.Radius) > 70.f) continue;
			const FVector RL = Reaper->GetActorLocation();
			const float Airborne = static_cast<float>(RL.Z) - GroundAtXY(static_cast<float>(RL.X), static_cast<float>(RL.Y));
			Wave.Hit.Add(Reaper);
			if (Airborne < 150.f) Reaper->ApplyHit(Wave.Damage); // capsule centre ~90 above ground when standing
		}
		if (Wave.Radius >= Wave.MaxRadius) Waves.RemoveAt(WaveIdx);
	}

	// Telegraphed strikes (pool spots, lightning)
	for (int32 StrikeIdx = Strikes.Num() - 1; StrikeIdx >= 0; --StrikeIdx)
	{
		FBossStrike& Strike = Strikes[StrikeIdx];
		Strike.Delay -= DeltaSeconds;
		if (Strike.Delay > 0.f) continue;
		if (Strike.Damage > 0.f)
		{
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				if (It->bDowned || FVector::Dist2D(It->GetActorLocation(), Strike.Center) > Strike.Radius) continue;
				It->ApplyHit(Strike.Damage);
				if (Strike.Knock > 0.f)
				{
					FVector Out = (It->GetActorLocation() - Strike.Center).GetSafeNormal2D();
					if (Out.IsNearlyZero()) Out = FVector(1.f, 0.f, 0.f);
					It->LaunchCharacter(Out * Strike.Knock + FVector(0.f, 0.f, Strike.Knock * 0.5f), true, true);
				}
			}
			// Map bosses: a tall burst column (reads from the chase camera); the old flat burst otherwise
			MulticastFX(Strike.PitSeconds > 0.f || Strike.Knock > 0.f ? 11 : 0, Strike.Center, Strike.Radius, Strike.Colour, 0.25f);
			if (Strike.PitSeconds > 0.f)
			{
				FBossHazard Pit;
				Pit.Center = Strike.Center;
				Pit.Radius = Strike.Radius;
				Pit.TimeLeft = Strike.PitSeconds;
				Pit.Dps = Def->RingDamage * 0.7f * Dmg;
				Pit.bWall = Strike.bWallPost;
				Hazards.Add(Pit);
				MulticastFX(Strike.bWallPost ? 13 : 1, Pit.Center, Pit.Radius, Strike.Colour, Pit.TimeLeft);
			}
		}
		else
		{
			// Becomes a lingering pool
			FBossHazard Pool;
			Pool.Center = Strike.Center;
			Pool.Radius = Strike.Radius;
			Pool.TimeLeft = 6.f + 2.f * Phase;
			Pool.Dps = Def->RingDamage * 0.8f * Dmg;
			Hazards.Add(Pool);
			MulticastFX(1, Pool.Center, Pool.Radius, Strike.Colour, Pool.TimeLeft);
		}
		Strikes.RemoveAt(StrikeIdx);
	}

	// Pools burn whoever stands in them (ticks every 0.5 s)
	HazardTick -= DeltaSeconds;
	const bool bHazardTick = HazardTick <= 0.f;
	if (bHazardTick) HazardTick = 0.5f;
	for (int32 HazardIdx = Hazards.Num() - 1; HazardIdx >= 0; --HazardIdx)
	{
		FBossHazard& Pool = Hazards[HazardIdx];
		Pool.TimeLeft -= DeltaSeconds;
		if (Pool.TimeLeft <= 0.f) { Hazards.RemoveAt(HazardIdx); continue; }
		if (!bHazardTick) continue;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			if (It->bDowned || FVector::Dist2D(It->GetActorLocation(), Pool.Center) > Pool.Radius) continue;
			if (Pool.bWall)
			{
				// A tombstone post: shoved back out (a wall to go round), a light knock rather than a burn
				FVector Out = (It->GetActorLocation() - Pool.Center).GetSafeNormal2D();
				if (Out.IsNearlyZero()) Out = FVector(1.f, 0.f, 0.f);
				It->LaunchCharacter(Out * 700.f, true, false);
				It->ApplyHit(Pool.Dps * 0.25f);
				continue;
			}
			It->ApplyHit(Pool.Dps * 0.5f);
		}
	}

	// Element passives (Merchant): fire drops a pool on someone, ice rolls a shockwave, lightning strikes everyone's feet
	if (Element > 0 && RoarLeft <= 0.f)
	{
		ElementTick -= DeltaSeconds;
		if (ElementTick <= 0.f)
		{
			ElementTick = 4.f * CadenceMult();
			const FColor Colour = SQBossElementColour(Element).ToFColor(true);
			TArray<ASQCharacter*> Standing;
			GatherPlayers(Standing);
			if (Element == 2)
			{
				const FVector L = GetActorLocation();
				FBossWave Wave;
				Wave.Center = FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f);
				Wave.MaxRadius = 1200.f;
				Wave.Speed = 600.f;
				Wave.Damage = Def->SlamDamage * 0.5f * Dmg;
				Wave.Delay = 0.8f;
				MulticastFX(1, Wave.Center, HitRadius() * 2.f, Colour, 0.8f);
				Waves.Add(Wave);
			}
			else
			{
				for (int32 ReaperIdx = 0; ReaperIdx < Standing.Num(); ++ReaperIdx)
				{
					if (Element == 1 && ReaperIdx > 0) break; // fire: one pool per tick
					FVector At = Standing[ReaperIdx]->GetActorLocation();
					At.Z = GroundAtXY(static_cast<float>(At.X), static_cast<float>(At.Y)) + 5.f;
					FBossStrike Strike;
					Strike.Center = At;
					Strike.Radius = Element == 1 ? 240.f : 200.f;
					Strike.Delay = 1.1f;
					Strike.Damage = Element == 1 ? 0.f : Def->RingDamage * 1.2f * Dmg;
					Strike.Colour = Colour;
					Strikes.Add(Strike);
					MulticastFX(1, At, Strike.Radius, Colour, Strike.Delay);
				}
			}
		}
	}
}

float ASQBoss::RoarSeconds() const
{
	const USQData* D = USQData::Get(this);
	return D ? D->Tune(TEXT("BossRoarSeconds"), 2.f) : 2.f;
}

// ================================================================ cosmetic (every machine)

void ASQBoss::UpdateMechanicVisuals(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World) return;
	const float Now = World->GetTimeSeconds();

	// Pylons: candle shrines, glowing bright while someone holds them (lit ones stay warm)
	for (int32 PylonIdx = 0; PylonIdx < PylonMeshes.Num(); ++PylonIdx)
	{
		UStaticMeshComponent* PylonComp = PylonMeshes[PylonIdx];
		if (!PylonComp) continue;
		const bool bUsed = Pylons.IsValidIndex(PylonIdx);
		if (PylonComp->IsVisible() != bUsed) PylonComp->SetVisibility(bUsed);
		if (!bUsed) continue;
		PylonComp->SetWorldLocation(FVector(Pylons[PylonIdx]));
		const bool bHeld = (PylonHeldMask & (1 << PylonIdx)) != 0;
		const bool bLit = (PylonLitMask & (1 << PylonIdx)) != 0;
		if (PylonMIDs.IsValidIndex(PylonIdx) && PylonMIDs[PylonIdx])
		{
			PylonMIDs[PylonIdx]->SetScalarParameterValue(TEXT("Glow"), bHeld ? 3.f + FMath::Sin(Now * 10.f) : (bLit ? 1.5f : 0.3f + 0.3f * FMath::Sin(Now * 3.f)));
		}
	}

	// Shield: a ring of sparks around the boss
	FxTimer -= DeltaSeconds;
	const FVector L = GetActorLocation();
	if (FxTimer <= 0.f)
	{
		FxTimer = 0.25f;
		if (bShielded) SQFx::Ring(World, FVector(L.X, L.Y, L.Z), FLinearColor(1.f, 0.85f, 0.3f), 16, 60.f, 0.3f, 10.f);
		if (bStaggered) SQFx::Ring(World, FVector(L.X, L.Y, L.Z + HitRadius() * 2.f), FLinearColor(1.f, 0.95f, 0.4f), 5, 80.f, 0.3f, 8.f);
	}

	// Mark: a red disc chasing its reaper, flashing faster near the end
	if (MarkTarget)
	{
		const USQData* D = USQData::Get(this);
		const float MarkRadius = D ? D->Tune(TEXT("BossMarkRadius"), 300.f) : 300.f;
		const float Ground = GroundAtXY(static_cast<float>(MarkLoc.X), static_cast<float>(MarkLoc.Y));
		MarkDisc->SetWorldLocation(FVector(MarkLoc.X, MarkLoc.Y, Ground + 4.f));
		MarkDisc->SetWorldScale3D(FVector(MarkRadius / 50.f, MarkRadius / 50.f, 0.02f));
		if (MarkMID)
		{
			const float Rate = MarkLeft < 1.5f ? 18.f : 6.f;
			MarkMID->SetScalarParameterValue(TEXT("Glow"), 1.f + FMath::Abs(FMath::Sin(Now * Rate)) * 2.f);
		}
		if (!MarkDisc->IsVisible()) MarkDisc->SetVisibility(true);
	}
	else if (MarkDisc->IsVisible())
	{
		MarkDisc->SetVisibility(false);
	}

	// Tether: a thin beam between the two ends, green when the distance is safe, red when it hurts
	if (TetherA)
	{
		const FVector EndA = TetherA->GetActorLocation();
		const FVector EndB = TetherB ? TetherB->GetActorLocation() : L;
		const FVector Mid = (EndA + EndB) * 0.5f;
		const FVector Span = EndB - EndA;
		const float Len = static_cast<float>(Span.Size());
		const float Dist2D = static_cast<float>(Span.Size2D());
		const bool bSafe = TetherB ? (Dist2D >= 250.f && Dist2D <= 1400.f) : (Dist2D >= HitRadius() + 200.f && Dist2D <= 1300.f);
		if (Len > 1.f)
		{
			TetherBeam->SetWorldLocationAndRotation(Mid, FRotationMatrix::MakeFromZ(Span / Len).Rotator());
			TetherBeam->SetWorldScale3D(FVector(0.12f, 0.12f, Len / 100.f));
		}
		if (TetherMID) TetherMID->SetVectorParameterValue(TEXT("Color"), bSafe ? FLinearColor(0.3f, 1.f, 0.6f) : FLinearColor(1.f, 0.15f, 0.1f));
		if (!TetherBeam->IsVisible()) TetherBeam->SetVisibility(true);
	}
	else if (TetherBeam->IsVisible())
	{
		TetherBeam->SetVisibility(false);
	}

	// Glow follows phase / stagger / element
	if (ArtMID)
	{
		const USQData* D = USQData::Get(this);
		const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr;
		const float Want = Def ? Def->Glow * (1.f + 0.6f * Phase) + (bStaggered ? 1.5f : 0.f) + (bInvulnerable ? 2.f * FMath::Abs(FMath::Sin(Now * 12.f)) : 0.f) : 0.f;
		if (FMath::Abs(Want - CurGlow) > 0.02f)
		{
			ArtMID->SetScalarParameterValue(TEXT("Glow"), Want);
			CurGlow = Want;
		}
	}
}

void ASQBoss::TakeBossDamage(float Damage, ASQCharacter* By)
{
	if (!HasAuthority() || bDead || bInvulnerable || bBurrowed) return;
	if (bShielded) Damage *= 0.1f;
	if (bStaggered) Damage *= 1.5f;
	Health -= Damage;
	if (Health <= 0.f)
	{
		Health = 0.f;
		bDead = true;
		ClearMechanics();
		const FVector L = GetActorLocation();
		MulticastFX(15, FVector(L.X, L.Y, ASQArena::GroundZAt(this, static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 900.f, FColor(255, 230, 150), 0.8f);
		MulticastAnim(3);

		// Tuning log: time-to-kill against the boss's target (aim for 2-4 minutes, not a bullet sponge)
		const USQData* D = USQData::Get(this);
		if (const FSQBossDef* Def = D ? D->FindBoss(BossId) : nullptr)
		{
			const float Fight = FMath::Max(1.f, FightTime);
			UE_LOG(LogSQ, Log, TEXT("[BossTTK] %s died in %.1f s (target %.0f s) | team %d, defiance %d, MaxHealth %.0f, team DPS %.1f | CSV Health for the target: %.0f"),
				*Def->Name, Fight, Def->TargetSeconds, TeamSize, DefianceStacks, MaxHealth, MaxHealth / Fight, Def->Health * Def->TargetSeconds / Fight);
		}

		if (ASQGameMode* GM = GetWorld()->GetAuthGameMode<ASQGameMode>())
		{
			GM->OnBossDefeated(this);
		}
		SetLifeSpan(0.2f);
	}
}

void ASQBoss::MulticastFX_Implementation(uint8 Kind, FVector_NetQuantize Location, float Radius, FColor Color, float Duration)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* FxWorld = GetWorld();
	const FLinearColor Lin(Color);
	if (Kind >= 10)
	{
		// Nov 25 2026 map-boss kinds: vertical pieces that read from the chase camera (FX lesson: flat ground lines vanish)
		const FVector Up(0.f, 0.f, 1.f);
		switch (Kind)
		{
		case 10: // telegraph disc + a warning pillar that stands until it lands
			ASQFx::Spawn(FxWorld, 1, Location, Radius, Lin, Duration);
			SQFx::Streak(FxWorld, Location, Location + Up * FMath::Clamp(Radius * 3.f, 250.f, 900.f), Lin, FMath::Clamp(Radius * 0.18f, 18.f, 70.f), Duration);
			break;
		case 11: // impact column
			ASQFx::Spawn(FxWorld, 0, Location, Radius, Lin, Duration);
			SQFx::Streak(FxWorld, Location, Location + Up * FMath::Clamp(Radius * 4.f, 400.f, 1400.f), FMath::Lerp(Lin, FLinearColor::White, 0.4f), FMath::Clamp(Radius * 0.35f, 30.f, 160.f), 0.35f);
			SQFx::Burst(FxWorld, Location + Up * 60.f, Lin, 10, 500.f, 0.6f, 14.f);
			break;
		case 12: // ghost ship: glowing hull and mast, sails, spray
			SQFx::Flash(FxWorld, Location + Up * 120.f, Lin, Radius * 1.6f, 0.2f);
			SQFx::Streak(FxWorld, Location, Location + Up * 700.f, Lin, 40.f, 0.22f);
			SQFx::Burst(FxWorld, Location + Up * 40.f, FLinearColor(0.7f, 0.95f, 1.f), 4, 300.f, 0.5f, 10.f);
			break;
		case 13: // tombstone post standing for Duration
			ASQFx::Spawn(FxWorld, 1, Location, Radius, Lin, Duration);
			SQFx::Streak(FxWorld, Location, Location + Up * 260.f, FLinearColor(0.75f, 0.74f, 0.8f), Radius * 0.9f, Duration);
			break;
		case 14: // a warning line: telegraph discs along it are sent separately; this is the glowing spine
			SQFx::Flash(FxWorld, Location, Lin, 160.f, Duration);
			break;
		case 15: // boss death: a huge column of light, rings and a local camera beat (no slow motion: co-op)
		{
			ASQFx::Spawn(FxWorld, 0, Location, Radius, Lin, Duration);
			SQFx::Streak(FxWorld, Location, Location + Up * 3000.f, FLinearColor(0.6f, 1.f, 0.95f), 260.f, 1.2f);
			SQFx::Shockwave(FxWorld, Location + Up * 20.f, Radius * 2.5f, FLinearColor(0.45f, 1.f, 0.9f), 1.2f, 80.f);
			SQFx::Burst(FxWorld, Location + Up * 200.f, FLinearColor(0.45f, 1.f, 0.9f), 40, 1200.f, 1.4f, 26.f);
			APlayerController* LocalPC = FxWorld ? FxWorld->GetFirstPlayerController() : nullptr;
			if (ASQCharacter* Mine = LocalPC ? Cast<ASQCharacter>(LocalPC->GetPawn()) : nullptr) Mine->NudgeCamera(0.5f);
			if (USoundBase* S = SQ::Sound(TEXT("Slam"))) UGameplayStatics::PlaySoundAtLocation(this, S, Location, 1.f, 0.6f);
			break;
		}
		default:
			break;
		}
		if (Kind == 11)
		{
			if (USoundBase* S = SQ::Sound(TEXT("Slam"))) UGameplayStatics::PlaySoundAtLocation(this, S, Location, 0.8f, FMath::FRandRange(0.9f, 1.1f));
		}
		return;
	}
	ASQFx::Spawn(FxWorld, Kind, Location, Radius, Lin, Duration);
	if (Kind == 0)
	{
		if (USoundBase* S = SQ::Sound(TEXT("Slam"))) UGameplayStatics::PlaySoundAtLocation(this, S, Location, 1.f, FMath::FRandRange(0.9f, 1.1f));
	}
}
