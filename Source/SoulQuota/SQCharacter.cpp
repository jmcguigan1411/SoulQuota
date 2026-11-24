// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQPlayerState.h"
#include "SQHordeManager.h"
#include "SQLantern.h"
#include "SQFx.h"
#include "SQFxTheme.h"
#include "SQFxRecipe.h"
#include "SQArena.h"
#include "SQArt.h"
#include "SQAnimInstance.h"
#include "SQAbility.h"
#include "SQReap.h"
#include "SQPlayerController.h"
#include "SQAugments.h"
#include "SQSave.h"

// ---------------------------------------------------------------- movement (body blocking + movement tech)

/** One client move plus the reaper's movement-tech state at its start (see USQMovementComponent). */
class FSQSavedMove : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	bool bSavedSlideHeld = false;
	bool bSavedSlideActive = false;
	float SavedSlideBoostCooldown = 0.f;
	float SavedHopWindow = 0.f;
	float SavedHopKeepSpeed = 0.f;

	virtual void Clear() override
	{
		Super::Clear();
		bSavedSlideHeld = false;
		bSavedSlideActive = false;
		SavedSlideBoostCooldown = 0.f;
		SavedHopWindow = 0.f;
		SavedHopKeepSpeed = 0.f;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 MoveFlags = Super::GetCompressedFlags();
		if (bSavedSlideHeld) MoveFlags |= FLAG_Custom_0;
		return MoveFlags;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		const FSQSavedMove* Later = static_cast<const FSQSavedMove*>(NewMove.Get());
		if (!Later) return false;
		// Only merge plain moves: any slide, hop window or boost timer makes the state time-dependent
		if (bSavedSlideHeld != Later->bSavedSlideHeld || bSavedSlideActive || Later->bSavedSlideActive) return false;
		if (SavedHopWindow > 0.f || Later->SavedHopWindow > 0.f) return false;
		if (SavedSlideBoostCooldown > 0.f || Later->SavedSlideBoostCooldown > 0.f) return false;
		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
		if (const USQMovementComponent* SQMove = C ? Cast<USQMovementComponent>(C->GetCharacterMovement()) : nullptr)
		{
			bSavedSlideHeld = SQMove->bSlideHeld;
			bSavedSlideActive = SQMove->bSlideActive;
			SavedSlideBoostCooldown = SQMove->SlideBoostCooldownLeft;
			SavedHopWindow = SQMove->HopWindowLeft;
			SavedHopKeepSpeed = SQMove->HopKeepSpeed;
		}
	}

	virtual void PrepMoveFor(ACharacter* C) override
	{
		Super::PrepMoveFor(C);
		if (USQMovementComponent* SQMove = C ? Cast<USQMovementComponent>(C->GetCharacterMovement()) : nullptr)
		{
			// bSlideHeld itself comes back through the compressed flags (MoveAutonomous -> UpdateFromCompressedFlags)
			SQMove->bSlideActive = bSavedSlideActive;
			SQMove->SlideBoostCooldownLeft = SavedSlideBoostCooldown;
			SQMove->HopWindowLeft = SavedHopWindow;
			SQMove->HopKeepSpeed = SavedHopKeepSpeed;
		}
	}
};

class FSQNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FSQNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSQSavedMove());
	}
};

USQMovementComponent::USQMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The slide lowers the capsule through the engine's (predicted, replicated) crouch
	GetNavAgentPropertiesRef().bCanCrouch = true;
	bCanWalkOffLedgesWhenCrouching = true; // a slide runs off ledges like a run does
	SetCrouchedHalfHeight(55.f);
}

FNetworkPredictionData_Client* USQMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		USQMovementComponent* MutableThis = const_cast<USQMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FSQNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}

void USQMovementComponent::RefreshMoveTuning()
{
	const USQData* TuneData = USQData::Get(this);
	if (!TuneData) return;
	FSQMoveTune& T = MoveTune;
	T.SlideEnterSpeed = FMath::Max(0.f, TuneData->Tune(TEXT("SlideEnterSpeed"), 380.f));
	T.SlideExitSpeed = FMath::Clamp(TuneData->Tune(TEXT("SlideExitSpeed"), 300.f), 0.f, T.SlideEnterSpeed);
	T.SlideBoost = FMath::Max(0.f, TuneData->Tune(TEXT("SlideBoost"), 400.f));
	T.SlideBoostCooldown = FMath::Max(0.f, TuneData->Tune(TEXT("SlideBoostCooldown"), 0.8f));
	T.SlideMaxSpeed = FMath::Max(100.f, TuneData->Tune(TEXT("SlideMaxSpeed"), 2000.f));
	T.SlideFriction = FMath::Max(0.f, TuneData->Tune(TEXT("SlideFriction"), 500.f));
	T.SlideSlopeGravity = FMath::Max(0.f, TuneData->Tune(TEXT("SlideSlopeGravity"), 2.2f));
	T.SlideSteerDegrees = FMath::Max(0.f, TuneData->Tune(TEXT("SlideSteerDegrees"), 90.f));
	T.HopWindow = FMath::Clamp(TuneData->Tune(TEXT("HopWindow"), 0.15f), 0.f, 1.f);
	T.HopMaxSpeed = FMath::Max(0.f, TuneData->Tune(TEXT("HopMaxSpeed"), 1500.f));
	T.HopSpeedGain = FMath::Clamp(TuneData->Tune(TEXT("HopSpeedGain"), 1.03f), 0.5f, 2.f);
	T.FastFallGravity = FMath::Clamp(TuneData->Tune(TEXT("FastFallGravity"), 2.5f), 1.f, 10.f);

	AirControl = FMath::Clamp(TuneData->Tune(TEXT("AirControl"), 0.6f), 0.f, 1.f);
	AirControlBoostMultiplier = FMath::Max(0.f, TuneData->Tune(TEXT("AirControlBoost"), 2.f));
	AirControlBoostVelocityThreshold = FMath::Max(0.f, TuneData->Tune(TEXT("AirControlBoostThreshold"), 25.f));
	// Only when not crouched: changing the crouched height mid-slide would desync the capsule
	if (!IsCrouching()) SetCrouchedHalfHeight(FMath::Clamp(TuneData->Tune(TEXT("SlideCapsuleHalfHeight"), 55.f), 40.f, 90.f));
}

bool USQMovementComponent::ReaperCanSlide() const
{
	const ASQCharacter* Reaper = Cast<ASQCharacter>(CharacterOwner);
	return Reaper && Reaper->CanSlideNow();
}

void USQMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bSlideHeld = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

bool USQMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// Replays set bSlideHeld from each saved move's flags: keep the key's real state for the next new move
	const bool bRealSlideHeld = bSlideHeld;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	bSlideHeld = bRealSlideHeld;
	return bReplayed;
}

void USQMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		const float StepSeconds = FMath::Max(0.f, DeltaSeconds);
		SlideBoostCooldownLeft = FMath::Max(0.f, SlideBoostCooldownLeft - StepSeconds);
		HopWindowLeft = FMath::Max(0.f, HopWindowLeft - StepSeconds);
		if (HopWindowLeft <= 0.f) HopKeepSpeed = 0.f;

		const bool bCanSlide = ReaperCanSlide();
		const float FlatSpeed = static_cast<float>(Velocity.Size2D());
		if (bSlideActive)
		{
			if (!bSlideHeld || !bCanSlide || !IsMovingOnGround() || FlatSpeed < MoveTune.SlideExitSpeed) bSlideActive = false;
		}
		else if (bSlideHeld && bCanSlide && IsMovingOnGround() && FlatSpeed >= MoveTune.SlideEnterSpeed)
		{
			StartSlide();
		}
		// The slide owns the crouch: the engine shrinks / restores the capsule (and replicates bIsCrouched) below
		bWantsToCrouch = bSlideActive;
	}
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void USQMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	if (IsFalling() && UpdatedComponent)
	{
		FallPeakZ = FMath::Max(FallPeakZ, static_cast<float>(UpdatedComponent->GetComponentLocation().Z));
	}
}

void USQMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (!IsMovingOnGround()) bSlideActive = false;
	if (IsFalling() && PreviousMovementMode != MOVE_Falling)
	{
		if (UpdatedComponent) FallPeakZ = static_cast<float>(UpdatedComponent->GetComponentLocation().Z);
		// Bunny hop: a jump inside the window after a landing keeps the landing speed (+ HopSpeedGain, capped)
		if (HopWindowLeft > 0.f && CharacterOwner && CharacterOwner->bPressedJump && Velocity.Z > 0.0)
		{
			FVector HopFlat(Velocity.X, Velocity.Y, 0.0);
			const float HopNow = static_cast<float>(HopFlat.Size());
			const float HopWant = FMath::Max(HopNow, FMath::Min(HopKeepSpeed * MoveTune.HopSpeedGain, MoveTune.HopMaxSpeed));
			if (HopNow > 1.f && HopWant > HopNow)
			{
				HopFlat *= HopWant / HopNow;
				Velocity.X = HopFlat.X;
				Velocity.Y = HopFlat.Y;
			}
		}
		HopWindowLeft = 0.f;
		HopKeepSpeed = 0.f;
	}
}

bool USQMovementComponent::CanAttemptJump() const
{
	// Stock rule: no jump while crouch is wanted. The slide crouches, and slide-jumping is the point.
	if (bSlideActive) return IsJumpAllowed() && IsMovingOnGround();
	return Super::CanAttemptJump();
}

float USQMovementComponent::GetGravityZ() const
{
	const float BaseGravityZ = Super::GetGravityZ();
	// Fast-fall: slide held in the air pulls the reaper down hard once past the apex
	if (bSlideHeld && IsFalling() && Velocity.Z <= 0.0 && ReaperCanSlide()) return BaseGravityZ * MoveTune.FastFallGravity;
	return BaseGravityZ;
}

void USQMovementComponent::StartSlide()
{
	bSlideActive = true;
	FVector SlideFlat(Velocity.X, Velocity.Y, 0.0);
	float SlideSpeed = static_cast<float>(SlideFlat.Size());
	if (SlideSpeed < 1.f) return;
	if (SlideBoostCooldownLeft <= 0.f && MoveTune.SlideBoost > 0.f)
	{
		// The burst never takes a fast slide (downhill, hop) past the cap, and never slows it down either
		SlideSpeed = FMath::Max(SlideSpeed, FMath::Min(SlideSpeed + MoveTune.SlideBoost, MoveTune.SlideMaxSpeed));
		SlideBoostCooldownLeft = MoveTune.SlideBoostCooldown;
	}
	SlideFlat = SlideFlat.GetSafeNormal() * SlideSpeed;
	Velocity.X = SlideFlat.X;
	Velocity.Y = SlideFlat.Y;
}

void USQMovementComponent::CalcSlideVelocity(float DeltaTime)
{
	FVector SlideFlat(Velocity.X, Velocity.Y, 0.0);

	// 1. Slope: gravity along the floor. The floor normal's horizontal part points downhill with length sin(slope);
	//    x cos(slope) gives the horizontal share of the pull. Downhill builds speed, uphill bleeds it.
	if (CurrentFloor.IsWalkableFloor())
	{
		const FVector FloorNormal = CurrentFloor.HitResult.ImpactNormal;
		const FVector Downhill(FloorNormal.X, FloorNormal.Y, 0.0);
		const float SlopePull = -Super::GetGravityZ() * static_cast<float>(FloorNormal.Z) * MoveTune.SlideSlopeGravity;
		SlideFlat += Downhill * (SlopePull * DeltaTime);
	}

	// 2. Steering: turn toward the stick / WASD at a limited rate (a slide commits you to a line)
	float SlideSpeed = static_cast<float>(SlideFlat.Size());
	const FVector SteerTo = Acceleration.GetSafeNormal2D();
	if (SlideSpeed > 1.f && !SteerTo.IsNearlyZero())
	{
		const FVector Heading = SlideFlat / SlideSpeed;
		const float CosTurn = FMath::Clamp(static_cast<float>(FVector::DotProduct(Heading, SteerTo)), -1.f, 1.f);
		const float TurnLeft = FMath::Acos(CosTurn);
		const float TurnStep = FMath::Min(TurnLeft, FMath::DegreesToRadians(MoveTune.SlideSteerDegrees) * DeltaTime);
		const float TurnSign = (Heading.X * SteerTo.Y - Heading.Y * SteerTo.X) >= 0.0 ? 1.f : -1.f;
		SlideFlat = Heading.RotateAngleAxis(FMath::RadiansToDegrees(TurnStep * TurnSign), FVector::UpVector) * SlideSpeed;
	}

	// 3. Friction: a constant decay toward a stop (UpdateCharacterStateBeforeMovement ends the slide below SlideExitSpeed)
	SlideSpeed = static_cast<float>(SlideFlat.Size());
	if (SlideSpeed > 1.f)
	{
		const float NewSpeed = FMath::Min(FMath::Max(0.f, SlideSpeed - MoveTune.SlideFriction * DeltaTime), MoveTune.SlideMaxSpeed);
		SlideFlat *= NewSpeed / SlideSpeed;
	}
	Velocity.X = SlideFlat.X;
	Velocity.Y = SlideFlat.Y;
}

void USQMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (bSlideActive && IsMovingOnGround() && DeltaTime > 0.f)
	{
		CalcSlideVelocity(DeltaTime);
	}
	else
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		// Bunny-hop window: the walking brakes don't eat the landing speed yet (only while still pushing along the run:
		// letting go or turning back stops as usual)
		if (HopWindowLeft > 0.f && HopKeepSpeed > 0.f && IsMovingOnGround())
		{
			FVector HopFlat(Velocity.X, Velocity.Y, 0.0);
			const float HopNow = static_cast<float>(HopFlat.Size());
			const bool bPushingOn = HopNow > 1.f && FVector::DotProduct(Acceleration.GetSafeNormal2D(), HopFlat / HopNow) > 0.3;
			if (bPushingOn && HopNow < HopKeepSpeed)
			{
				HopFlat *= HopKeepSpeed / HopNow;
				Velocity.X = HopFlat.X;
				Velocity.Y = HopFlat.Y;
			}
		}
	}
	// Body blocking applies to every rule (a slide does not pass through the horde; only the dash does)
	if (const ASQCharacter* Reaper = Cast<ASQCharacter>(CharacterOwner)) Reaper->ApplyBodyBlock(Velocity, DeltaTime);
}

bool USQMovementComponent::HandleReaperLanded(float& OutDrop, bool& bOutSafe)
{
	OutDrop = 0.f;
	bOutSafe = bFallDamageSafe;
	bFallDamageSafe = false;
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || !UpdatedComponent) return false;

	OutDrop = FMath::Max(0.f, FallPeakZ - static_cast<float>(UpdatedComponent->GetComponentLocation().Z));
	FallPeakZ = static_cast<float>(UpdatedComponent->GetComponentLocation().Z);

	const float LandSpeed = static_cast<float>(Velocity.Size2D());
	HopWindowLeft = MoveTune.HopWindow;
	HopKeepSpeed = FMath::Min(LandSpeed, MoveTune.HopMaxSpeed);

	// Slide held on touchdown: a roll. No fall damage; the slide carries on if there is speed to slide with.
	const bool bRoll = bSlideHeld && ReaperCanSlide();
	if (bRoll)
	{
		bOutSafe = true;
		if (!bSlideActive && LandSpeed >= MoveTune.SlideExitSpeed) StartSlide();
	}
	return bRoll;
}

void USQMovementComponent::ResetFallPeak()
{
	if (UpdatedComponent) FallPeakZ = static_cast<float>(UpdatedComponent->GetComponentLocation().Z);
	bFallDamageSafe = false;
}

ASQCharacter::ASQCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USQMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 900.f, 0.f);
	Move->MaxWalkSpeed = 650.f;
	Move->JumpZVelocity = 720.f;
	Move->AirControl = 0.6f;
	Move->BrakingDecelerationWalking = 2500.f;
	Move->GroundFriction = 10.f;

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(RootComponent);
	Boom->TargetArmLength = 1400.f;
	Boom->bUsePawnControlRotation = true;
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = 12.f;
	Boom->bDoCollisionTest = false;
	Boom->SocketOffset = FVector(0.f, 0.f, 120.f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Boom, USpringArmComponent::SocketName);
	Camera->FieldOfView = 75.f;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -35.f));
	BodyMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.05f));

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	HeadMesh->SetupAttachment(RootComponent);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
	HeadMesh->SetRelativeScale3D(FVector(0.6f));

	ScytheMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Scythe"));
	ScytheMesh->SetupAttachment(RootComponent);
	ScytheMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScytheMesh->SetRelativeLocation(FVector(25.f, 45.f, 10.f));
	ScytheMesh->SetRelativeRotation(FRotator(0.f, 0.f, 15.f));
	ScytheMesh->SetRelativeScale3D(FVector(0.06f, 0.06f, 1.7f));

	WeaponHeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponHead"));
	WeaponHeadMesh->SetupAttachment(RootComponent);
	WeaponHeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponHeadMesh->SetCastShadow(false);
	WeaponHeadMesh->SetVisibility(false);

	AuraMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Aura"));
	AuraMesh->SetupAttachment(RootComponent);
	AuraMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AuraMesh->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	AuraMesh->SetCastShadow(false);
	AuraMesh->SetVisibility(false);

	SkinMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Skin"));
	// Under the character's (empty) Mesh: remote reapers then get CharacterMovement's network smoothing (no jitter)
	if (GetMesh()) SkinMesh->SetupAttachment(GetMesh());
	else SkinMesh->SetupAttachment(RootComponent);
	SkinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkinMesh->SetRelativeLocation(FVector(0.f, 0.f, -90.f)); // capsule half height: feet on the floor
	SkinMesh->SetVisibility(false);

	AccessoryMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Accessory"));
	AccessoryMesh->SetupAttachment(SkinMesh);
	AccessoryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AccessoryMesh->SetVisibility(false);

	OrbISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Orbs"));
	OrbISM->SetupAttachment(RootComponent);
	OrbISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OrbISM->SetUsingAbsoluteLocation(true);
	OrbISM->SetUsingAbsoluteRotation(true);
	OrbISM->SetUsingAbsoluteScale(true);

	SoulStackISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SoulStack"));
	SoulStackISM->SetupAttachment(RootComponent);
	SoulStackISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Abilities = CreateDefaultSubobject<USQAbilityComponent>(TEXT("Abilities"));
	Reap = CreateDefaultSubobject<USQReapComponent>(TEXT("Reap"));

	TurretISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Turrets"));
	TurretISM->SetupAttachment(RootComponent);
	TurretISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretISM->SetUsingAbsoluteLocation(true);
	TurretISM->SetUsingAbsoluteRotation(true);
	TurretISM->SetUsingAbsoluteScale(true);
}

void ASQCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQCharacter, ReaperId);
	DOREPLIFETIME(ASQCharacter, Stats);
	DOREPLIFETIME(ASQCharacter, Health);
	DOREPLIFETIME(ASQCharacter, Carried);
	DOREPLIFETIME(ASQCharacter, bDowned);
	DOREPLIFETIME(ASQCharacter, Pardons);
	DOREPLIFETIME(ASQCharacter, BankProgress);
	DOREPLIFETIME(ASQCharacter, Owned);
	DOREPLIFETIME(ASQCharacter, Turrets);
	DOREPLIFETIME(ASQCharacter, CarriedSkull);
	DOREPLIFETIME(ASQCharacter, CarriedBy);
	DOREPLIFETIME(ASQCharacter, bGodMode);
	DOREPLIFETIME(ASQCharacter, ShopBonusCarry);
	DOREPLIFETIME(ASQCharacter, ActiveBonds);
	DOREPLIFETIME(ASQCharacter, Shield);
	DOREPLIFETIME(ASQCharacter, PassiveCharge);
	DOREPLIFETIME(ASQCharacter, bPickBubble);
	DOREPLIFETIME(ASQCharacter, HarvestSouls);
	DOREPLIFETIME(ASQCharacter, RiseLockUntil);
	DOREPLIFETIME(ASQCharacter, SquadNear);
	DOREPLIFETIME(ASQCharacter, GhostHauntCharge);
}

void ASQCharacter::BeginPlay()
{
	Super::BeginPlay();

	BodyMesh->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	HeadMesh->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	ScytheMesh->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
	AuraMesh->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	OrbISM->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	SoulStackISM->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	TurretISM->SetStaticMesh(SQ::Mesh(TEXT("Cone")));

	OrbISM->SetMaterial(0, SQ::ColorMID(this, FLinearColor(0.05f, 0.05f, 0.07f)));
	SoulStackISM->SetMaterial(0, SQ::ColorMID(this, FLinearColor(1.f, 0.85f, 0.45f)));
	TurretISM->SetMaterial(0, SQ::ColorMID(this, FLinearColor(0.95f, 0.75f, 0.3f)));
	AuraMesh->SetMaterial(0, SQ::ColorMID(this, FLinearColor(0.55f, 0.3f, 0.8f)));
	ScytheMesh->SetMaterial(0, SQ::ColorMID(this, FLinearColor(0.35f, 0.25f, 0.15f)));

	OrbISM->SetWorldTransform(FTransform::Identity);
	TurretISM->SetWorldTransform(FTransform::Identity);

	ApplyCameraMode(); // before ApplySpeed (it reads bTopDownCamera for the turn rate)
	ApplyVisuals();
	ApplySpeed();
	LastYawVisual = static_cast<float>(GetActorRotation().Yaw);
}

void ASQCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreAllOccluders();
	Super::EndPlay(EndPlayReason);
}

void ASQCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (HasAuthority())
	{
		// Only (re)initialise a fresh reaper or in the lobby: a mid-run re-possess must not wipe weapons, souls and health
		const UWorld* PossessWorld = GetWorld();
		const ASQGameState* PossessGS = PossessWorld ? PossessWorld->GetGameState<ASQGameState>() : nullptr;
		if (Owned.Num() == 0 || !PossessGS || PossessGS->Phase == ESQPhase::Waiting)
		{
			const ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
			InitReaper(PS ? PS->ReaperId : FName(TEXT("Grim")));
		}
	}
}

void ASQCharacter::InitReaper(FName NewReaper)
{
	if (!HasAuthority()) return;
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* R = D ? D->FindReaper(NewReaper) : nullptr;

	ReaperId = R ? R->Id : NewReaper;
	Owned.Reset();
	WeaponCooldowns.Reset();
	PendingDrops.Reset();
	Turrets.Reset();
	TurretInfos.Reset();
	Carried = 0;
	BankProgress = 0.f;
	ShopBonusCarry = 0;
	PardonTimer = -1.f;
	ReleaseSkull();
	CarriedBy = nullptr;
	bDowned = false;
	PickBubbleLeft = 0.f;
	PickBubbleCooldownLeft = 0.f;
	PickBubbleSerial = -1;
	bPickBubble = false;

	// Kit state (passive, E / R cooldowns, buffs, summons) starts fresh every run
	Shield = 0.f;
	PassiveCharge = 0.f;
	if (Abilities) Abilities->ResetForRun();
	if (Reap) Reap->ResetForRun();
	HarvestSouls = 0;
	RiseLockUntil = -1.f;
	// Last run's bonds / invulnerability / dash must not leak into the new run
	ActiveBonds.Reset();
	BondTimer = 0.f;
	HitInvuln = 0.f;
	DashCooldownLeft = 0.f;
	DashPassLeft = 0.f;

	// Kits pass: every reaper starts with its signature weapon (Kits.csv SignatureWeaponId, copied into StartWeapon by SQData)
	const FName StartWeaponId = R ? R->StartWeapon : NAME_None;

	if (!StartWeaponId.IsNone())
	{
		FSQOwned Start;
		Start.Id = StartWeaponId;
		Start.Level = 1;
		Start.Power = 1.f;
		Owned.Add(Start);
	}

	RecomputeStats();
	Health = Stats.MaxHealth;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	OnRep_Reaper();
	OnRep_Downed();
}

void ASQCharacter::RecomputeStats()
{
	const USQData* D = USQData::Get(this);
	if (!D) return;

	FSQStats S;
	if (const FSQReaperDef* R = D->FindReaper(ReaperId))
	{
		S.MaxHealth = R->MaxHealth;
		S.MoveSpeed = R->MoveSpeed;
		S.CarryCap = R->CarryCap;
		S.DashCooldown = R->DashCooldown;
		S.Magnet = R->Magnet;
	}

	// Tomes + items (Kind Relic): every level / stack adds StatPerLevel x its rarity multiplier (accumulated in Power)
	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* U = D->FindUpgrade(O.Id);
		if (!U || U->Kind != ESQUpgradeKind::Relic) continue;
		const float Picks = FMath::Max(0.f, O.Power);
		if (!U->Stat.IsNone()) SQ::ApplyStat(S, U->Stat, U->StatPerLevel * Picks);
		if (!U->Stat2.IsNone()) SQ::ApplyStat(S, U->Stat2, U->Stat2PerLevel * Picks);
	}

	// Co-op Bonds currently active (worked out by ServerTickBonds)
	for (const FName& BondId : ActiveBonds)
	{
		const FSQBondDef* Bond = D->Bonds.FindByPredicate([BondId](const FSQBondDef& X) { return X.Id == BondId; });
		if (Bond) SQ::ApplyStat(S, Bond->Stat, Bond->Value);
	}

	S.CarryCap += ShopBonusCarry;
	// Lantern trail (Nov 12 2026): the sack grows with every lantern behind you, in step with the quotas (one or two sacks a lantern)
	if (const ASQGameState* TrailGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr)
	{
		if (TrailGS->TrailSites.Num() > 0 && TrailGS->TrailStage > 0)
		{
			const int32 Steps = FMath::Min(TrailGS->TrailStage, TrailGS->TrailSites.Num() - 1);
			S.CarryCap += FMath::RoundToInt(static_cast<float>(S.CarryCap) * FMath::Max(0.f, D->Tune(TEXT("LanternSackGrowth"), 1.5f)) * static_cast<float>(Steps));
		}
	}
	USQAugments::ModifyStats(this, S); // Death's Office meta upgrades + stat augments + team auras (Nov 9 missions)
	// Caps + diminishing curves run once on the summed raw stats (shrine raises are re-added to RawStats by USQBoostLedger)
	RawStats = S;
	SQ::FinalizeStats(S, D);

	const float OldMax = Stats.MaxHealth;
	Stats = S;
	if (HasAuthority() && !bDowned && Stats.MaxHealth > OldMax) // a downed reaper stays at 0 until revived
	{
		Health += Stats.MaxHealth - OldMax;
	}
	Health = FMath::Min(Health, Stats.MaxHealth);
	ApplySpeed();
}

void ASQCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	DashCooldownLeft = FMath::Max(0.f, DashCooldownLeft - DeltaSeconds);
	DashPassLeft = FMath::Max(0.f, DashPassLeft - DeltaSeconds);
	AimLockLeft = FMath::Max(0.f, AimLockLeft - DeltaSeconds);
	UpdateFacingMode();

	if (HasAuthority())
	{
		HitInvuln = FMath::Max(-1.f, HitInvuln - DeltaSeconds);
		ServerKeepInArena();
		ServerUpdateMoveTolerance(DeltaSeconds);

		if (bDowned)
		{
			if (PardonTimer > 0.f)
			{
				PardonTimer -= DeltaSeconds;
				if (PardonTimer <= 0.f)
				{
					PardonTimer = -1.f;
					const USQData* D = USQData::Get(this);
					Revive(D ? D->Tune(TEXT("ReviveFraction"), 0.5f) : 0.5f);
				}
			}
			if (bDowned) ServerTickGhost(DeltaSeconds);
		}
		else
		{
			GhostTrailStageSeen = -1;
			if (Stats.Regen > 0.f && Health < Stats.MaxHealth)
			{
				Health = FMath::Min(Stats.MaxHealth, Health + Stats.Regen * DeltaSeconds);
			}
			ServerTickWeapons(DeltaSeconds);
			ServerTickBanking(DeltaSeconds);
			ServerTickBonds(DeltaSeconds);
			ServerTickSlideKnock(DeltaSeconds);
		}
		ServerTickSkull(DeltaSeconds);
		FlushDamagePops(DeltaSeconds);
	}

	UpdateZoneMovement();
	UpdateLocalVisuals(DeltaSeconds);
}

void ASQCharacter::ServerKeepInArena()
{
	// Map-exploit guard: multi-jumps + glide + dash (or a jump pad) can carry a reaper over the collision walls, and
	// outside them there is no floor. Anything past the wall face or below the terrain is put back on the ground inside.
	if (CarriedBy) return; // a carried skull is positioned by its carrier
	const UWorld* KeepWorld = GetWorld();
	const ASQGameState* KeepGS = KeepWorld ? KeepWorld->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* KeepArena = KeepGS ? KeepGS->Arena.Get() : nullptr;
	if (!KeepArena) return;
	const USQData* KeepData = USQData::Get(this);
	const float ArenaHalfSize = KeepData ? KeepData->Tune(TEXT("ArenaHalfSize"), 6000.f) : 6000.f;
	const FVector Here = GetActorLocation();
	const bool bOutside = FMath::Abs(Here.X) > ArenaHalfSize + 10.f || FMath::Abs(Here.Y) > ArenaHalfSize + 10.f;
	const bool bBelow = KeepArena->HasTerrain() && Here.Z < KeepArena->MinGroundZ() - 100.f /* capsule centre is >= MinZ+90 on any real ground; the hidden safety floor sits at MinZ-300 */;
	if (!bOutside && !bBelow) return;

	const float Inner = ArenaHalfSize - 120.f;
	const float SafeX = FMath::Clamp(static_cast<float>(Here.X), -Inner, Inner);
	const float SafeY = FMath::Clamp(static_cast<float>(Here.Y), -Inner, Inner);
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const float SafeZ = ASQArena::GroundZAt(this, SafeX, SafeY) + HalfHeight + 10.f;
	SetActorLocation(FVector(SafeX, SafeY, SafeZ), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* KeepMove = GetCharacterMovement())
	{
		KeepMove->Velocity = FVector::ZeroVector;
		KeepMove->SetMovementMode(MOVE_Falling);
	}
	if (USQMovementComponent* KeepSQMove = GetSQMovement()) KeepSQMove->ResetFallPeak(); // the teleport is not a fall
}

void ASQCharacter::ServerTickBonds(float DeltaSeconds)
{
	BondTimer -= DeltaSeconds;
	if (BondTimer > 0.f) return;
	BondTimer = 0.5f;
	ServerShareRegen(0.5f);

	const USQData* D = USQData::Get(this);
	if (!D) return;
	const float Range = D->Tune(TEXT("BondRange"), 900.f);

	// Close Ranks (Nov 23 2026): count the standing allies fighting beside this reaper. A crew of one counts
	// SquadAuraSolo allies, so playing alone is never weaker for it.
	{
		const float AuraRange = D->Tune(TEXT("SquadAuraRange"), 900.f);
		const int32 AuraMax = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("SquadAuraMax"), 2.f)), 0, 3);
		int32 Crew = 0;
		int32 Near = 0;
		for (TActorIterator<ASQCharacter> AuraIt(GetWorld()); AuraIt; ++AuraIt)
		{
			const ASQCharacter* Mate = *AuraIt;
			if (Mate == this) continue;
			Crew++;
			if (!Mate->bDowned && FVector::Dist2D(Mate->GetActorLocation(), GetActorLocation()) <= AuraRange) Near++;
		}
		if (Crew == 0) Near = FMath::RoundToInt(D->Tune(TEXT("SquadAuraSolo"), 1.f));
		SquadNear = static_cast<uint8>(FMath::Clamp(Near, 0, AuraMax));
	}

	TArray<FName> NewBonds;
	for (const FSQBondDef& Bond : D->Bonds)
	{
		const bool bHaveA = GetUpgradeLevel(Bond.RelicA) > 0;
		const bool bHaveB = GetUpgradeLevel(Bond.RelicB) > 0;
		if (!bHaveA && !bHaveB) continue;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Ally = *It;
			if (Ally == this || Ally->bDowned) continue;
			if (FVector::Dist2D(Ally->GetActorLocation(), GetActorLocation()) > Range) continue;
			if ((bHaveA && Ally->GetUpgradeLevel(Bond.RelicB) > 0) || (bHaveB && Ally->GetUpgradeLevel(Bond.RelicA) > 0))
			{
				NewBonds.Add(Bond.Id);
				break;
			}
		}
	}

	if (NewBonds != ActiveBonds)
	{
		for (const FName& Id : NewBonds)
		{
			if (!ActiveBonds.Contains(Id))
			{
				const FSQBondDef* Bond = D->Bonds.FindByPredicate([Id](const FSQBondDef& X) { return X.Id == Id; });
				if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
				{
					if (Bond) GS->PushAlert(FString::Printf(TEXT("BOND: %s (%s)"), *Bond->Name, *Bond->Description));
				}
			}
		}
		ActiveBonds = NewBonds;
		RecomputeStats();
	}
}

void ASQCharacter::ServerShareRegen(float Seconds)
{
	if (Stats.AllyRegen <= 0.f || bDowned) return;
	const USQData* D = USQData::Get(this);
	const float ShareRange = D ? D->Tune(TEXT("BondRange"), 900.f) : 900.f;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* Ally = *It;
		if (Ally == this || Ally->bDowned || Ally->Health >= Ally->Stats.MaxHealth) continue;
		if (FVector::Dist2D(Ally->GetActorLocation(), GetActorLocation()) > ShareRange) continue;
		Ally->Health = FMath::Min(Ally->Stats.MaxHealth, Ally->Health + Stats.AllyRegen * Seconds);
	}
}

void ASQCharacter::UpdateZoneMovement()
{
	// Runs on server and owning client so movement prediction agrees
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (!Move || bDowned) return;
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	const FVector2D P = Pos2D();
	float Mult = Arena ? Arena->PlayerSpeedMult(P) : 1.f;
	// Kit: Haste buffs (replicated) and Femur's Fetch with a skull (CarriedSkull replicates): server and owner agree
	if (Abilities) Mult *= Abilities->GetMoveSpeedMult();
	const bool bIce = Arena && Arena->IsOnIce(P);
	Move->MaxWalkSpeed = Stats.MoveSpeed * Mult;
	Move->GroundFriction = bIce ? 0.8f : 10.f;
	Move->BrakingDecelerationWalking = bIce ? 250.f : 2500.f;
}

// ---------------------------------------------------------------- weapons

float ASQCharacter::GetDamageMult() const
{
	// Kit passive (Collections, Spite) and Empower buffs live in the ability component
	return Stats.DamageMult * (Abilities ? Abilities->GetOutgoingDamageMult() : 1.f);
}

float ASQCharacter::RollCritMult() const
{
	return FMath::FRand() < Stats.CritChance ? 1.f + FMath::Max(0.f, Stats.CritDamage) : 1.f;
}

bool ASQCharacter::AllowWeaponFx()
{
	// Refills WeaponFxPerSecond tokens a second (burst of the same size): six fast weapons x four reapers can't flood the net
	const UWorld* FxWorld = GetWorld();
	if (!FxWorld) return false;
	const USQData* FxData = USQData::Get(this);
	const float PerSecond = FMath::Max(1.f, FxData ? FxData->Tune(TEXT("WeaponFxPerSecond"), 24.f) : 24.f);
	const float FxNow = static_cast<float>(FxWorld->GetTimeSeconds());
	WeaponFxTokens = FMath::Min(PerSecond, WeaponFxTokens + (FxNow - WeaponFxStamp) * PerSecond);
	WeaponFxStamp = FxNow;
	if (WeaponFxTokens < 1.f) return false;
	WeaponFxTokens -= 1.f;
	return true;
}

void ASQCharacter::RemoveTurretAt(int32 Index)
{
	if (Turrets.IsValidIndex(Index)) Turrets.RemoveAt(Index);
	if (TurretInfos.IsValidIndex(Index)) TurretInfos.RemoveAt(Index);
}

void ASQCharacter::ServerTickWeapons(float DeltaSeconds)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D || !GS->Horde) return;
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) return;

	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* Def = D->FindUpgrade(O.Id);
		if (!Def || Def->Kind == ESQUpgradeKind::Relic || Def->Behaviour == ESQWeaponBehaviour::None) continue;
		float& Cd = WeaponCooldowns.FindOrAdd(O.Id);
		Cd -= DeltaSeconds;
		if (Cd <= 0.f)
		{
			FireWeapon(*Def, O.Level, O.Power);
			Cd = FMath::Max(0.08f, SQ::UpgradeCooldown(*Def, O.Level) * Stats.CooldownMult);
		}
	}

	// Tombstones landing
	for (int32 i = PendingDrops.Num() - 1; i >= 0; --i)
	{
		PendingDrops[i].TimeLeft -= DeltaSeconds;
		if (PendingDrops[i].TimeLeft <= 0.f)
		{
			const FPendingDrop& Drop = PendingDrops[i];
			const float DropCrit = RollCritMult();
			const FVector2D DropAt(Drop.Location.X, Drop.Location.Y);
			GS->Horde->ApplyAreaDamage(DropAt, Drop.Radius, Drop.Damage * DropCrit, this);
			AddAreaPops(DropAt, Drop.Radius, Drop.Damage * DropCrit, DropCrit, 6); // the landing itself is drawn by every machine
			PendingDrops.RemoveAtSwap(i);
		}
	}

	// Turrets (each remembers the weapon that placed it)
	if (TurretInfos.Num() != Turrets.Num())
	{
		Turrets.SetNum(FMath::Min(Turrets.Num(), TurretInfos.Num()));
		TurretInfos.SetNum(Turrets.Num());
	}
	for (int32 i = Turrets.Num() - 1; i >= 0; --i)
	{
		FTurretInfo& Info = TurretInfos[i];
		Info.Life -= DeltaSeconds;
		if (Info.Life <= 0.f)
		{
			RemoveTurretAt(i);
			continue;
		}
		Info.ShotCooldown -= DeltaSeconds;
		if (Info.ShotCooldown <= 0.f)
		{
			Info.ShotCooldown = 0.6f * Stats.CooldownMult;
			const FVector2D From(Turrets[i].X, Turrets[i].Y);
			TArray<FVector2D> Targets;
			GS->Horde->FindNearestTargets(From, 1, 1600.f, Targets);
			if (Targets.Num() > 0)
			{
				const FVector2D Dir = (Targets[0] - From).GetSafeNormal();
				const float ShotCrit = RollCritMult();
				GS->Horde->SpawnProjectile(From + Dir * 40.f, Dir * Info.ShotSpeed, Info.ShotDamage * ShotCrit, 30.f * Stats.AreaMult, 1.4f * Stats.Duration, 1, false, this);
				const float ShotDist = static_cast<float>(FVector2D::Distance(From, Targets[0]));
				if (ShotDist <= Info.ShotSpeed * 1.4f * Stats.Duration) AddDamagePop(Targets[0], Info.ShotDamage * ShotCrit, ShotCrit, ShotDist / FMath::Max(1.f, Info.ShotSpeed));
			}
		}
	}
}

void ASQCharacter::FireWeapon(const FSQUpgradeDef& Def, int32 Level, float Power)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQHordeManager* Horde = GS ? GS->Horde.Get() : nullptr;
	if (!Horde) return;

	const FVector L = GetActorLocation();
	const FVector2D P(L.X, L.Y);
	const FVector Ground(L.X, L.Y, ASQArena::GroundZAt(this, static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f); // terrain has plateaus: not Z=5
	const float Crit = RollCritMult();
	const bool bCrit = Crit > 1.f;
	const float Damage = SQ::UpgradeDamage(Def, Level, Power) * GetDamageMult() * Crit;
	const float Radius = SQ::UpgradeRadius(Def, Level, Power) * Stats.AreaMult;
	const float SlowTime = Def.Slow * Stats.Duration;
	int32 Count = SQ::UpgradeCount(Def, Level, Power);
	const bool bEvolved = Def.Kind == ESQUpgradeKind::Evolution;
	const bool bAnim = AllowAttackAnim(Def);
	// Effect points for MulticastWeaponFire (see its comment); every machine draws the effect from these
	TArray<FVector_NetQuantize> FxPoints;
	static constexpr int32 MaxFxPoints = 17;
	auto AddFxPoint = [&FxPoints](const FVector2D& Spot, double Z)
	{
		if (FxPoints.Num() < MaxFxPoints) FxPoints.Add(FVector_NetQuantize(Spot.X, Spot.Y, Z));
	};

	switch (Def.Behaviour)
	{
	case ESQWeaponBehaviour::Arc:
	case ESQWeaponBehaviour::Nova:
	{
		Horde->ApplyAreaDamage(P, Radius, Damage, this, SlowTime);
		const USQData* JuiceData = USQData::Get(this);
		if (Def.Behaviour == ESQWeaponBehaviour::Arc)
		{
			// Attack juice follow-through: melee swings shove the crowd back a little (Tuning WeaponKnockback, 0 = off)
			const float Knock = JuiceData ? JuiceData->Tune(TEXT("WeaponKnockback"), 220.f) : 220.f;
			if (Knock > 0.f) Horde->ApplyKnockback(P, Radius, FVector2D::ZeroVector, Knock, JuiceData ? JuiceData->Tune(TEXT("WeaponKnockbackMaxEnemyRadius"), 62.f) : 62.f);
		}
		// With a swing animation the numbers wait for its blow (Tuning MeleePopDelay)
		const float PopDelay = bAnim ? (JuiceData ? JuiceData->Tune(TEXT("MeleePopDelay"), 0.15f) : 0.15f) : 0.f;
		TArray<FVector2D> HitSpots;
		Horde->FindNearestTargets(P, 8, Radius, HitSpots);
		AddFxPoint(P, Ground.Z);
		for (int32 HitIdx = 0; HitIdx < HitSpots.Num(); ++HitIdx)
		{
			AddDamagePop(HitSpots[HitIdx], Damage, Crit, PopDelay);
			if (HitIdx < 6) AddFxPoint(HitSpots[HitIdx], Ground.Z);
		}
		SendWeaponFire(Def, bCrit, bAnim, Radius, FxPoints);
		break;
	}

	case ESQWeaponBehaviour::Aura:
		Horde->ApplyAreaDamage(P, Radius, Damage, this, SlowTime);
		AddAreaPops(P, Radius, Damage, Crit, 5);
		SendWeaponFire(Def, bCrit, bAnim, Radius, FxPoints); // the aura itself is drawn locally: anim gesture only
		break;

	case ESQWeaponBehaviour::Projectile:
	{
		Count += Stats.ProjectileBonus;
		TArray<FVector2D> Targets;
		Horde->FindNearestTargets(P, Count, 2500.f, Targets);
		const int32 Pierce = 1 + Level / 3 + (bEvolved ? 3 : 0);
		const float ShotSpeed = FMath::Max(400.f, Def.Speed);
		const float ShotLife = 1.6f * Stats.Duration;
		AddFxPoint(P, L.Z);
		for (int32 i = 0; i < Count; ++i)
		{
			FVector2D Dir;
			bool bAimed = false;
			if (Targets.Num() > 0)
			{
				Dir = (Targets[i % Targets.Num()] - P).GetSafeNormal();
				bAimed = i < Targets.Num();
				if (i >= Targets.Num())
				{
					const float Spread = FMath::DegreesToRadians(FMath::FRandRange(-20.f, 20.f));
					Dir = FVector2D(Dir.X * FMath::Cos(Spread) - Dir.Y * FMath::Sin(Spread), Dir.X * FMath::Sin(Spread) + Dir.Y * FMath::Cos(Spread));
				}
			}
			else
			{
				const float A = FMath::FRandRange(0.f, 2.f * PI);
				Dir = FVector2D(FMath::Cos(A), FMath::Sin(A));
			}
			Horde->SpawnProjectile(P + Dir * 50.f, Dir * ShotSpeed, Damage, FMath::Max(20.f, Radius), ShotLife, Pierce, false, this);

			// Cosmetic: where this shot is drawn to (its first target, or the end of its flight) + its damage number
			const float TargetDist = bAimed ? static_cast<float>(FVector2D::Distance(P, Targets[i])) : ShotSpeed * ShotLife;
			const float DrawDist = FMath::Min(TargetDist, ShotSpeed * ShotLife);
			AddFxPoint(P + Dir * DrawDist, L.Z);
			if (bAimed && TargetDist <= ShotSpeed * ShotLife) AddDamagePop(Targets[i], Damage, Crit, TargetDist / ShotSpeed);
		}
		SendWeaponFire(Def, bCrit, bAnim, Radius, FxPoints);
		break;
	}

	case ESQWeaponBehaviour::Orbit:
	{
		const float T = GS->GetServerWorldTimeSeconds();
		const float OrbRadius = 70.f * Stats.AreaMult;
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = T * Def.Speed + i * 2.f * PI / Count;
			const FVector2D OrbPos = P + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
			if (Horde->ApplyAreaDamage(OrbPos, OrbRadius, Damage, this, SlowTime) > 0) AddAreaPops(OrbPos, OrbRadius, Damage, Crit, 1);
		}
		SendWeaponFire(Def, bCrit, bAnim, Radius, FxPoints); // orbits are drawn locally: anim gesture only
		break;
	}

	case ESQWeaponBehaviour::Drop:
	{
		TArray<FVector2D> Targets;
		Horde->FindNearestTargets(P, Count + 3, 1800.f, Targets);
		AddFxPoint(P, Ground.Z);
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D Target = Targets.Num() > 0 ? Targets[FMath::RandRange(0, Targets.Num() - 1)] : P + FMath::RandPointInCircle(800.f);
			FPendingDrop Drop;
			Drop.Location = FVector(Target.X, Target.Y, ASQArena::GroundZAt(this, static_cast<float>(Target.X), static_cast<float>(Target.Y)) + 5.f);
			Drop.TimeLeft = 0.6f;
			Drop.Damage = Damage / Crit; // drops roll their own crit when they land
			Drop.Radius = Radius;
			if (PendingDrops.Num() >= 64) break; // bounded even with huge Count / tiny cooldowns
			PendingDrops.Add(Drop);
			AddFxPoint(Target, Drop.Location.Z); // telegraph + falling slab + landing, drawn on every machine
		}
		SendWeaponFire(Def, false, bAnim, Radius, FxPoints);
		break;
	}

	case ESQWeaponBehaviour::Turret:
	{
		// Each turret weapon has its own limit: Count + TurretBonus of its own turrets alive at once
		const int32 Max = FMath::Max(1, Count + Stats.TurretBonus);
		int32 Mine = 0;
		int32 Oldest = INDEX_NONE;
		for (int32 i = 0; i < TurretInfos.Num(); ++i)
		{
			if (TurretInfos[i].Source != Def.Id) continue;
			if (Oldest == INDEX_NONE) Oldest = i;
			Mine++;
		}
		if (Mine >= Max && Oldest != INDEX_NONE) RemoveTurretAt(Oldest);

		FTurretInfo Info;
		Info.Source = Def.Id;
		Info.Life = 10.f * Stats.Duration;
		Info.ShotCooldown = 0.2f;
		Info.ShotDamage = Damage / Crit; // shots roll their own crit
		Info.ShotSpeed = FMath::Max(400.f, Def.Speed);
		{
			const float TurretX = static_cast<float>(L.X) + FMath::FRandRange(-80.f, 80.f);
			const float TurretY = static_cast<float>(L.Y) + FMath::FRandRange(-80.f, 80.f);
			const float TurretZ = ASQArena::GroundZAt(this, TurretX, TurretY);
			Turrets.Add(FVector_NetQuantize(TurretX, TurretY, TurretZ)); // Z = ground (drawn on plateaus too)
			AddFxPoint(FVector2D(TurretX, TurretY), TurretZ);
		}
		TurretInfos.Add(Info);
		SendWeaponFire(Def, false, bAnim, Radius, FxPoints);
		break;
	}

	case ESQWeaponBehaviour::Chain:
	{
		// Radius = jump range; Count (+ projectile bonus) = enemies hit per chain
		const int32 ChainHits = Count + Stats.ProjectileBonus;
		TArray<FVector2D> ChainPoints;
		Horde->ApplyChainDamage(P, ChainHits, FMath::Max(Radius * 2.f, 900.f), Radius, Damage, this, SlowTime, ChainPoints);
		for (int32 LinkIdx = 0; LinkIdx < ChainPoints.Num(); ++LinkIdx)
		{
			AddFxPoint(ChainPoints[LinkIdx], L.Z);
			if (LinkIdx > 0) AddDamagePop(ChainPoints[LinkIdx], Damage, Crit, 0.04f * LinkIdx);
		}
		if (ChainPoints.Num() < 2) FxPoints.Reset(); // nothing in range: no lightning, maybe still the gesture
		SendWeaponFire(Def, bCrit, bAnim, Radius, FxPoints);
		break;
	}

	case ESQWeaponBehaviour::Beam:
	{
		// Radius = beam length, Speed = beam width, Count (+ projectile bonus) = beams at different targets
		const int32 Beams = Count + Stats.ProjectileBonus;
		const float HalfWidth = FMath::Max(30.f, Def.Speed * 0.5f) * Stats.AreaMult;
		TArray<FVector2D> Targets;
		Horde->FindNearestTargets(P, Beams, FMath::Max(Radius, 600.f), Targets);
		AddFxPoint(P, L.Z);
		for (int32 i = 0; i < Beams; ++i)
		{
			FVector2D Dir;
			if (Targets.Num() > 0)
			{
				Dir = (Targets[i % Targets.Num()] - P).GetSafeNormal();
				if (i >= Targets.Num()) Dir = Dir.GetRotated(360.f * i / Beams);
			}
			else
			{
				const float A = FMath::FRandRange(0.f, 2.f * PI);
				Dir = FVector2D(FMath::Cos(A), FMath::Sin(A));
			}
			if (Dir.IsNearlyZero()) Dir = FVector2D(1.f, 0.f);
			const FVector2D BeamEnd = P + Dir * Radius;
			Horde->ApplyLineDamage(P, BeamEnd, HalfWidth, Damage, this, SlowTime);
			AddFxPoint(BeamEnd, L.Z);
			if (i < Targets.Num() && FVector2D::Distance(P, Targets[i]) <= Radius) AddDamagePop(Targets[i], Damage, Crit);
		}
		SendWeaponFire(Def, bCrit, bAnim, Radius, FxPoints);
		break;
	}

	default:
		break;
	}
}

bool ASQCharacter::AllowAttackAnim(const FSQUpgradeDef& Def)
{
	// Server only. Weapons can fire many times a second: at most AnimActionRateLimit attack animations per second,
	// and the always-on weapons (auras, orbits) only throw a gesture every AuraGestureInterval seconds
	if (!HasAuthority() || bDowned || Def.Behaviour == ESQWeaponBehaviour::None) return false;
	const UWorld* AnimWorld = GetWorld();
	if (!AnimWorld) return false;
	const USQData* D = USQData::Get(this);
	const float Limit = D ? D->Tune(TEXT("AnimActionRateLimit"), 3.f) : 3.f;
	if (Limit <= 0.f) return false;
	const float AnimNow = AnimWorld->GetTimeSeconds();
	if (AnimNow - LastAttackAnimTime < 1.f / Limit) return false;
	if (Def.Behaviour == ESQWeaponBehaviour::Aura || Def.Behaviour == ESQWeaponBehaviour::Orbit)
	{
		const float Gap = D ? D->Tune(TEXT("AuraGestureInterval"), 3.5f) : 3.5f;
		if (AnimNow - LastAuraGestureTime < Gap) return false;
		LastAuraGestureTime = AnimNow;
	}
	LastAttackAnimTime = AnimNow;
	return true;
}

void ASQCharacter::SendWeaponFire(const FSQUpgradeDef& Def, bool bCrit, bool bAnim, float Radius, const TArray<FVector_NetQuantize>& Points)
{
	const bool bFx = Points.Num() > 0 && AllowWeaponFx();
	if (!bFx && !bAnim) return;
	uint8 Flags = 0;
	if (bCrit) Flags |= 1;
	if (bAnim) Flags |= 2;
	if (bFx) Flags |= 4;
	if (bFx)
	{
		MulticastWeaponFire(Def.Id, Flags, Radius, Points);
	}
	else
	{
		const TArray<FVector_NetQuantize> NoPoints;
		MulticastWeaponFire(Def.Id, Flags, Radius, NoPoints);
	}
}

void ASQCharacter::AddDamagePop(const FVector2D& Where, float Amount, float CritMult, float Delay)
{
	if (!HasAuthority() || Amount < 0.5f) return;
	const USQData* D = USQData::Get(this);
	if (D && D->Tune(TEXT("DamageNumbersNet"), 1.f) <= 0.f) return;
	const float Huge = D ? D->Tune(TEXT("DamageNumberHuge"), 250.f) : 250.f;
	uint8 Tier = 0;
	if (CritMult > 1.f) Tier = CritMult >= 2.25f ? 2 : 1;
	if (Amount >= Huge) Tier = 2;
	const int32 DelaySteps = FMath::Clamp(FMath::RoundToInt(Delay * 20.f), 0, 63);
	const int16 PopX = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(Where.X)), -32000, 32000));
	const int16 PopY = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(Where.Y)), -32000, 32000));

	// Merge with a queued pop on the same spot and timing (aura ticks, several hits on one enemy)
	for (FSQDamagePop& Queued : PendingPops)
	{
		if ((Queued.Info >> 2) != DelaySteps) continue;
		if (FMath::Abs(Queued.X - PopX) > 60 || FMath::Abs(Queued.Y - PopY) > 60) continue;
		Queued.Amount = static_cast<uint16>(FMath::Min(65535, static_cast<int32>(Queued.Amount) + FMath::RoundToInt(Amount)));
		Queued.Info = static_cast<uint8>((DelaySteps << 2) | FMath::Max(static_cast<int32>(Queued.Info & 3), static_cast<int32>(Tier)));
		return;
	}
	if (PendingPops.Num() >= 32) return; // the batch is full until the next flush
	FSQDamagePop& Pop = PendingPops.AddDefaulted_GetRef();
	Pop.X = PopX;
	Pop.Y = PopY;
	Pop.Amount = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Amount), 1, 65535));
	Pop.Info = static_cast<uint8>((DelaySteps << 2) | Tier);
}

void ASQCharacter::AddAreaPops(const FVector2D& Center, float Radius, float Amount, float CritMult, int32 MaxPops)
{
	ASQGameState* PopGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	if (!PopGS || !PopGS->Horde || MaxPops <= 0) return;
	TArray<FVector2D> PopSpots;
	PopGS->Horde->FindNearestTargets(Center, MaxPops, Radius, PopSpots);
	for (const FVector2D& Spot : PopSpots) AddDamagePop(Spot, Amount, CritMult);
}

void ASQCharacter::FlushDamagePops(float DeltaSeconds)
{
	PopFlushTimer += DeltaSeconds;
	if (PopFlushTimer < 0.1f || PendingPops.Num() == 0) return;
	PopFlushTimer = 0.f;
	MulticastDamagePops(PendingPops);
	PendingPops.Reset();
}

void ASQCharacter::MulticastDamagePops_Implementation(const TArray<FSQDamagePop>& Pops)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* PopWorld = GetWorld();
	for (const FSQDamagePop& Pop : Pops)
	{
		const float PopX = Pop.X;
		const float PopY = Pop.Y;
		const float PopZ = ASQArena::GroundZAt(this, PopX, PopY) + 120.f;
		SQFx::DamageNumber(PopWorld, FVector(PopX, PopY, PopZ), static_cast<float>(Pop.Amount), static_cast<uint8>(Pop.Info & 3), static_cast<float>(Pop.Info >> 2) / 20.f);
	}
}

// ---------------------------------------------------------------- souls & banking

int32 ASQCharacter::AddCarried(int32 Amount)
{
	if (!HasAuthority() || bDowned || Amount <= 0) return 0;
	const int32 Space = FMath::Max(0, Stats.CarryCap - Carried);
	const int32 Accepted = FMath::Min(Space, Amount);
	Carried += Accepted;
	if (Stats.PickupHeal > 0.f) Health = FMath::Min(Stats.MaxHealth, Health + Stats.PickupHeal * Accepted);
	return Accepted;
}

void ASQCharacter::LoseCarried(int32 Amount, bool bDropAsPickups)
{
	if (!HasAuthority()) return;
	Amount = FMath::Clamp(Amount, 0, Carried);
	if (Amount <= 0) return;
	Carried -= Amount;
	if (bDropAsPickups)
	{
		if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
		{
			if (GS->Horde) GS->Horde->DropSouls(Pos2D(), Amount);
		}
	}
}

void ASQCharacter::ServerTickBanking(float DeltaSeconds)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D || !GS->Lantern || GS->Phase != ESQPhase::Night || Carried <= 0 || CarriedSkull)
	{
		BankProgress = 0.f;
		return;
	}

	const float Dist = FVector::Dist2D(GetActorLocation(), GS->Lantern->GetActorLocation());
	if (Dist > GS->Lantern->Radius)
	{
		BankProgress = 0.f;
		return;
	}

	const float BankTime = D->Tune(TEXT("BankSeconds"), 1.5f) / (1.f + Stats.BankSpeed);
	BankProgress += DeltaSeconds / FMath::Max(0.1f, BankTime);
	if (BankProgress >= 1.f)
	{
		const float Mult = GS->bFinalCall ? D->Tune(TEXT("FinalCallMultiplier"), 2.f) : 1.f;
		GS->Banked += FMath::RoundToInt(Carried * Mult);
		Carried = 0;
		BankProgress = 0.f;
		const FVector LL = GS->Lantern->GetActorLocation();
		MulticastFX(0, FVector(LL.X, LL.Y, LL.Z + 5.f), GS->Lantern->Radius, FColor(255, 215, 120), 0.4f);
	}
}

// ---------------------------------------------------------------- damage, downed, skull carry

void ASQCharacter::ApplyHit(float Damage)
{
	if (!HasAuthority() || bDowned || bGodMode || HitInvuln > 0.f || PickBubbleLeft > 0.f || Damage <= 0.f || IsRising()) return;
	const USQData* D = USQData::Get(this);

	// Evasion: the hit simply misses (short invulnerability so a crowd can't re-roll it every frame)
	if (Stats.Evasion > 0.f && FMath::FRand() < Stats.Evasion)
	{
		HitInvuln = 0.25f;
		MulticastFX(0, GetActorLocation() - FVector(0.f, 0.f, 85.f), 120.f, FColor(200, 230, 255), 0.15f);
		return;
	}

	float Taken = Damage * (1.f - Stats.Armor) * IncomingDamageMult();
	HitInvuln = 0.25f;
	if (Shield > 0.f)
	{
		// Doctor Beak's overheal shield soaks the hit first; a fully soaked hit keeps your souls too
		const float Absorbed = FMath::Min(Shield, Taken);
		Shield -= Absorbed;
		Taken -= Absorbed;
		if (Taken <= 0.f)
		{
			MulticastFX(0, GetActorLocation() - FVector(0.f, 0.f, 85.f), 110.f, FColor(150, 255, 170), 0.15f);
			return;
		}
	}
	Health -= Taken;
	if (IsLocallyControlled()) HitFlash = 0.35f; // listen-server host doesn't get OnRep

	const float DropFrac = D ? D->Tune(TEXT("HitDropFraction"), 0.25f) : 0.25f;
	const int32 Drop = FMath::CeilToInt(Carried * DropFrac);
	if (Drop > 0) LoseCarried(Drop, true);

	if (Health <= 0.f)
	{
		Health = 0.f;
		bDowned = true;
		HarvestSouls = 0;
		Shield = 0.f;
		PassiveCharge = 0.f;
		LoseCarried(Carried, true);
		BankProgress = 0.f;
		ReleaseSkull();
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);

		if (Pardons > 0)
		{
			Pardons--;
			PardonTimer = D ? D->Tune(TEXT("PardonSeconds"), 3.f) : 3.f;
		}

		// Solo vs co-op (ASQGameMode::CheckWipe ends the run when no reaper is left standing):
		// alone (no other reaper, human or bot, still up), going down without a Pardon is death - no skull to carry.
		int32 StillStanding = 0;
		for (TActorIterator<ASQCharacter> MateIt(GetWorld()); MateIt; ++MateIt)
		{
			if (*MateIt != this && (!MateIt->bDowned || MateIt->IsPardonPending())) StillStanding++;
		}
		if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
		{
			const APlayerState* PS = GetPlayerState();
			const FString Who = PS ? PS->GetPlayerName() : FString(TEXT("A reaper"));
			if (PardonTimer > 0.f) GS->PushAlert(FString::Printf(TEXT("%s is down - Death's Pardon in a moment..."), *Who));
			else if (StillStanding > 0) GS->PushAlert(FString::Printf(TEXT("%s is down! Reap %d souls near them, or carry their skull to the lantern (F)"), *Who, GetHarvestReviveNeed()));
			else GS->PushAlert(FString::Printf(TEXT("%s has been reaped. Nobody left to carry the skull."), *Who));
		}
		OnRep_Downed();
	}
}

void ASQCharacter::ServerTickPickBubble(bool bStripOpen, uint8 SerialNow, float DeltaSeconds, float BubbleSeconds, float BubbleCooldown)
{
	if (!HasAuthority()) return;
	PickBubbleCooldownLeft = FMath::Max(0.f, PickBubbleCooldownLeft - DeltaSeconds);
	if (!bStripOpen || bDowned)
	{
		PickBubbleLeft = 0.f;
		PickBubbleSerial = -1; // re-opening the strip counts as a fresh pick
	}
	else
	{
		if (static_cast<int32>(SerialNow) != PickBubbleSerial)
		{
			PickBubbleSerial = SerialNow;
			if (PickBubbleCooldownLeft <= 0.f && BubbleSeconds > 0.f)
			{
				PickBubbleLeft = BubbleSeconds;
				PickBubbleCooldownLeft = BubbleCooldown;
			}
		}
		PickBubbleLeft = FMath::Max(0.f, PickBubbleLeft - DeltaSeconds);
	}
	const bool bBubbleNow = PickBubbleLeft > 0.f;
	if (bPickBubble != bBubbleNow) bPickBubble = bBubbleNow;
}

void ASQCharacter::Revive(float HealthFraction)
{
	if (!HasAuthority()) return;
	if (CarriedBy)
	{
		CarriedBy->CarriedSkull = nullptr;
		CarriedBy = nullptr;
	}
	bDowned = false;
	PardonTimer = -1.f;
	HarvestSouls = 0;
	Health = FMath::Max(1.f, Stats.MaxHealth * HealthFraction);
	HitInvuln = 1.5f;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	OnRep_Downed();
}

int32 ASQCharacter::GetHarvestReviveNeed() const
{
	const USQData* D = USQData::Get(this);
	return FMath::Max(1, FMath::RoundToInt(D ? D->Tune(TEXT("HarvestReviveSouls"), 10.f) : 10.f));
}

void ASQCharacter::AddHarvestSouls(int32 Count, ASQCharacter* By)
{
	if (!HasAuthority() || !bDowned || Count <= 0 || IsPardonPending()) return;
	HarvestSouls = FMath::Min(HarvestSouls + Count, 10000);
	if (HarvestSouls < GetHarvestReviveNeed()) return;
	// Co-op Harvest Revive: the crew reaped enough souls around the skull - it stands up where it lies (a carrier lets go first)
	const USQData* D = USQData::Get(this);
	Revive(D ? D->Tune(TEXT("ReviveFraction"), 0.5f) : 0.5f);
	if (By) By->RewardRevive(this);
	MulticastFX(0, GetActorLocation() - FVector(0.f, 0.f, 85.f), 420.f, FColor(92, 242, 214), 0.6f);
	if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
	{
		const APlayerState* PS = GetPlayerState();
		const APlayerState* ByPS = By ? By->GetPlayerState() : nullptr;
		GS->PushAlert(FString::Printf(TEXT("%s was reaped back to work%s%s!"), PS ? *PS->GetPlayerName() : TEXT("A reaper"),
			ByPS ? TEXT(" by ") : TEXT(""), ByPS ? *ByPS->GetPlayerName() : TEXT("")));
	}
}

void ASQCharacter::StartRiseLock(float Seconds)
{
	if (!HasAuthority()) return;
	const UWorld* RiseWorld = GetWorld();
	const ASQGameState* GS = RiseWorld ? RiseWorld->GetGameState<ASQGameState>() : nullptr;
	const float Lock = FMath::Clamp(Seconds, 0.f, 10.f);
	RiseLockUntil = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) + Lock : -1.f;
	HitInvuln = FMath::Max(HitInvuln, Lock);
	if (UCharacterMovementComponent* RiseMove = GetCharacterMovement()) RiseMove->StopMovementImmediately();
}

bool ASQCharacter::IsRising() const
{
	if (RiseLockUntil <= 0.f) return false;
	const UWorld* RiseWorld = GetWorld();
	const ASQGameState* GS = RiseWorld ? RiseWorld->GetGameState<ASQGameState>() : nullptr;
	return GS && static_cast<float>(GS->GetServerWorldTimeSeconds()) < RiseLockUntil;
}

void ASQCharacter::ReleaseSkull()
{
	if (CarriedSkull)
	{
		CarriedSkull->CarriedBy = nullptr;
		if (CarriedSkull->GetCharacterMovement()) CarriedSkull->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		CarriedSkull = nullptr;
	}
}

void ASQCharacter::ServerInteract_Implementation()
{
	if (bDowned)
	{
		TryGhostHaunt(); // ghost shift: F haunts the toughest enemy nearby
		return;
	}
	if (CarriedSkull)
	{
		ReleaseSkull();
		return;
	}

	ASQCharacter* Best = nullptr;
	float BestDist = 300.f;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* Other = *It;
		if (Other == this || !Other->bDowned || Other->CarriedBy) continue;
		const float Dist = FVector::Dist2D(Other->GetActorLocation(), GetActorLocation());
		if (Dist < BestDist) { BestDist = Dist; Best = Other; }
	}

	if (Best)
	{
		CarriedSkull = Best;
		Best->CarriedBy = this;
		Best->GetCharacterMovement()->DisableMovement();
	}
}

void ASQCharacter::ServerTickSkull(float DeltaSeconds)
{
	// Our carrier left the game / was destroyed / no longer holds us: don't stay frozen (MOVE_None) and unpickable forever
	if (CarriedBy && (!IsValid(CarriedBy) || CarriedBy->CarriedSkull != this))
	{
		CarriedBy = nullptr;
	}
	if (!CarriedBy && bDowned)
	{
		UCharacterMovementComponent* SkullMove = GetCharacterMovement();
		if (SkullMove && SkullMove->MovementMode == MOVE_None) SkullMove->SetMovementMode(MOVE_Walking);
	}

	if (!CarriedSkull) return;
	if (!IsValid(CarriedSkull) || !CarriedSkull->bDowned || bDowned)
	{
		ReleaseSkull();
		return;
	}

	CarriedSkull->SetActorLocation(GetActorLocation() + FVector(0.f, 0.f, 150.f), false, nullptr, ETeleportType::TeleportPhysics);

	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (GS && GS->Lantern && FVector::Dist2D(GetActorLocation(), GS->Lantern->GetActorLocation()) <= GS->Lantern->Radius)
	{
		const USQData* D = USQData::Get(this);
		ASQCharacter* Skull = CarriedSkull;
		ReleaseSkull();
		Skull->Revive(D ? D->Tune(TEXT("ReviveFraction"), 0.5f) : 0.5f);
		RewardRevive(Skull);
		Skull->SetActorLocation(GetActorLocation() + FVector(120.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		GS->PushAlert(TEXT("Reaper revived at the lantern!"));
	}
}

// ---------------------------------------------------------------- upgrades

int32 ASQCharacter::GetUpgradeLevel(FName Id) const
{
	const FSQOwned* Found = Owned.FindByPredicate([Id](const FSQOwned& O) { return O.Id == Id; });
	return Found ? Found->Level : 0;
}

int32 ASQCharacter::CountWeapons() const
{
	const USQData* D = USQData::Get(this);
	int32 N = 0;
	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* U = D ? D->FindUpgrade(O.Id) : nullptr;
		if (U && U->Kind != ESQUpgradeKind::Relic && !U->IsSignature()) N++; // kits pass: the signature has its own slot
	}
	return N;
}

int32 ASQCharacter::CountRelics() const
{
	const USQData* D = USQData::Get(this);
	int32 N = 0;
	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* U = D ? D->FindUpgrade(O.Id) : nullptr;
		if (U && U->Kind == ESQUpgradeKind::Relic) N++;
	}
	return N;
}

int32 ASQCharacter::CountTomes() const
{
	const USQData* D = USQData::Get(this);
	int32 N = 0;
	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* U = D ? D->FindUpgrade(O.Id) : nullptr;
		if (U && U->IsTome()) N++;
	}
	return N;
}

int32 ASQCharacter::GetWeaponSlots() const
{
	const USQData* D = USQData::Get(this);
	return FMath::Clamp(FMath::RoundToInt(D ? D->Tune(TEXT("WeaponSlots"), 4.f) : 4.f) + USQAugments::ExtraSlots(this, true), 1, 8);
}

int32 ASQCharacter::GetTomeSlots() const
{
	const USQData* D = USQData::Get(this);
	return FMath::Clamp(FMath::RoundToInt(D ? D->Tune(TEXT("TomeSlots"), 4.f) : 4.f) + USQAugments::ExtraSlots(this, false), 0, 8);
}

FString ASQCharacter::SlotText(const FSQUpgradeDef& Def) const
{
	if (Def.IsSignature()) return TEXT("SIGNATURE");
	if (Def.Category == ESQUpgradeCategory::Weapon) return FString::Printf(TEXT("WEAPONS %d/%d"), CountWeapons(), GetWeaponSlots());
	if (Def.IsTome()) return FString::Printf(TEXT("TOMES %d/%d"), CountTomes(), GetTomeSlots());
	return FString();
}

bool ASQCharacter::CanTakeUpgrade(const FSQUpgradeDef& Def) const
{
	// Slot rules (Docs/ItemsAndTomes.md): weapons and tomes fill Tuning WeaponSlots / TomeSlots and level up to MaxLevel;
	// items have no slots and stack up to MaxStacks (0 = no limit); evolutions replace their maxed weapon in its slot.
	const int32 Level = GetUpgradeLevel(Def.Id);
	const USQData* D = USQData::Get(this);
	switch (Def.Category)
	{
	case ESQUpgradeCategory::Weapon:
	{
		// Kits pass: a signature weapon is only ever offered to its owner, and never uses a shared slot
		if (Def.IsSignature() && Def.SignatureOwner != ReaperId) return false;
		if (Level > 0) return Level < Def.MaxLevel;
		if (!Def.IsSignature() && CountWeapons() >= GetWeaponSlots()) return false;
		// Already evolved from this weapon: the base weapon can't be taken a second time
		for (const FSQOwned& O : Owned)
		{
			const FSQUpgradeDef* OwnedDef = D ? D->FindUpgrade(O.Id) : nullptr;
			if (OwnedDef && OwnedDef->Category == ESQUpgradeCategory::Evolution && OwnedDef->EvolvesFrom == Def.Id) return false;
		}
		return true;
	}
	case ESQUpgradeCategory::Tome:
		return Level > 0 ? Level < Def.MaxLevel : CountTomes() < GetTomeSlots();
	case ESQUpgradeCategory::Item:
		return Level < Def.LevelCap();
	case ESQUpgradeCategory::Evolution:
	{
		if (Level > 0) return false;
		if (Def.IsSignature() && Def.SignatureOwner != ReaperId) return false;
		const FSQUpgradeDef* Base = D ? D->FindUpgrade(Def.EvolvesFrom) : nullptr;
		return Base && GetUpgradeLevel(Def.EvolvesFrom) >= Base->MaxLevel && GetUpgradeLevel(Def.RequiresRelic) > 0;
	}
	default:
		return false; // Fallback cards are added by ASQPlayerState::RollOffers, never pooled
	}
}

float ASQCharacter::GetLuck() const
{
	return Stats.Luck;
}

void ASQCharacter::ApplyUpgrade(FName Id, uint8 Rarity)
{
	if (!HasAuthority()) return;
	if (Id == TEXT("SQ_Heal"))
	{
		if (!bDowned) Health = Stats.MaxHealth; // a downed reaper is revived, not healed (no full-HP downed state)
		return;
	}

	const USQData* D = USQData::Get(this);
	const FSQUpgradeDef* Def = D ? D->FindUpgrade(Id) : nullptr;
	if (!Def) return;
	const uint8 Tier = static_cast<uint8>(FMath::Min<int32>(Rarity, 3));

	if (Def->IsFallback())
	{
		// Filler cards once nothing is left to take (Upgrades.csv rows SQ_Gold / SQ_XP; amount = StatPerLevel x rarity)
		ASQGameState* FallbackGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
		const float FallbackAmount = FMath::Max(0.f, Def->StatPerLevel) * SQ::RarityStatMult(Tier);
		if (Id == TEXT("SQ_Gold"))
		{
			if (ASQPlayerState* GoldPS = GetPlayerState<ASQPlayerState>())
			{
				GoldPS->AddGold(FMath::RoundToInt(FallbackAmount * (FallbackGS ? FallbackGS->GoldNightGrowth() : 1.f)));
			}
		}
		else if (Id == TEXT("SQ_XP") && FallbackGS)
		{
			// A share of the current level, split by crew size: 4 maxed reapers picking it can't chain level-ups forever
			const int32 Crew = FMath::Max(1, FallbackGS->PlayerArray.Num());
			const float XPGain = FMath::Min(FallbackAmount, 0.5f) * static_cast<float>(FallbackGS->XPToNext) / static_cast<float>(Crew);
			FallbackGS->AddTeamXP(FMath::Max(1, FMath::RoundToInt(XPGain)));
		}
		else if (!bDowned)
		{
			Health = Stats.MaxHealth;
		}
		return;
	}

	if (Def->Kind == ESQUpgradeKind::Evolution)
	{
		// Evolutions are always Legendary cards. The evolved weapon keeps the power the base weapon earned
		// from its rarer picks (recipes are deterministic, so rarity luck is never thrown away).
		for (FSQOwned& O : Owned)
		{
			if (O.Id == Def->EvolvesFrom)
			{
				WeaponCooldowns.Remove(O.Id);
				O.Id = Def->Id;
				O.Level = 1;
				O.Power = FMath::Max(1.f, O.Power);
				O.BestRarity = 3;
				break;
			}
		}
		if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
		{
			GS->PushAlert(FString::Printf(TEXT("EVOLUTION: %s"), *Def->Name));
		}
		const FVector L = GetActorLocation();
		MulticastFX(0, FVector(L.X, L.Y, 5.f), 600.f, SQ::RarityColor(3).ToFColor(true), 0.5f);
	}
	else if (FSQOwned* Found = Owned.FindByPredicate([Id](const FSQOwned& O) { return O.Id == Id; }))
	{
		if (Found->Level >= Def->LevelCap()) return; // nothing left to give (the offer list should not show it); items: Level = stacks
		Found->Level = Found->Level + 1;
		Found->Power += Def->Kind == ESQUpgradeKind::Relic ? SQ::RarityStatMult(Tier) : SQ::RarityWeaponPower(Tier);
		Found->BestRarity = FMath::Max(Found->BestRarity, Tier);
	}
	else
	{
		FSQOwned New;
		New.Id = Id;
		New.Level = 1;
		New.Power = Def->Kind == ESQUpgradeKind::Relic ? SQ::RarityStatMult(Tier) : 1.f + SQ::RarityWeaponPower(Tier);
		New.BestRarity = Tier;
		Owned.Add(New);
	}
	RecomputeStats();
}

// ---------------------------------------------------------------- dash

bool ASQCharacter::IsFrozenForLobby() const
{
	const UWorld* W = GetWorld();
	const ASQGameState* GS = W ? W->GetGameState<ASQGameState>() : nullptr;
	return GS && (GS->Phase == ESQPhase::Waiting || IsRising()); // reap core: the rise from the grave blocks input like the lobby
}

void ASQCharacter::MulticastRunStart_Implementation()
{
	bPlayedSpawn = false; // the anim pass plays the rise-from-the-ground again (the teal ring is gone: ASQRiseFx draws the grave cracks)
}

void ASQCharacter::TryDash(const FVector& PreferredDir)
{
	if (bDowned || DashCooldownLeft > 0.f || IsFrozenForLobby()) return;
	FVector Dir = PreferredDir.GetSafeNormal2D();
	if (Dir.IsNearlyZero()) Dir = GetLastMovementInputVector();
	if (Dir.IsNearlyZero()) Dir = GetActorForwardVector();
	Dir.Z = 0.f;
	Dir.Normalize();

	if (HasAuthority())
	{
		DoDash(Dir);
	}
	else
	{
		DoDash(Dir);
		ServerDash(Dir);
	}
}

void ASQCharacter::ServerDash_Implementation(FVector Direction)
{
	if (bDowned || IsFrozenForLobby() || Direction.ContainsNaN()) return;
	// Allow small timing slack between client and server cooldowns, but at most 30% of the cooldown (dash spam guard)
	const float DashSlack = FMath::Min(0.25f, Stats.DashCooldown * 0.3f);
	if (DashCooldownLeft > DashSlack) return;
	// Never trust the client's vector: flat unit direction only (zero -> facing)
	FVector DashDir = Direction.GetSafeNormal2D();
	if (DashDir.IsNearlyZero()) DashDir = GetActorForwardVector().GetSafeNormal2D();
	if (DashDir.IsNearlyZero()) return;
	DoDash(DashDir);
}

void ASQCharacter::DoDash(const FVector& Direction)
{
	const USQData* D = USQData::Get(this);
	const float Speed = D ? D->Tune(TEXT("DashSpeed"), 1800.f) : 1800.f;
	if (HasAuthority() && Abilities)
	{
		// Kit dash passives (Hollow Monk's strike, Tallow's wax trail) run in the ability component
		Abilities->OnOwnerDash(Direction);
		// Hollow Monk: brief invulnerability, at most half the dash cooldown so dash spam can't chain it into permanent immunity
		if (Abilities->GetPassiveId() == TEXT("DashStrike")) HitInvuln = FMath::Max(HitInvuln, FMath::Min(0.45f, Stats.DashCooldown * 0.5f));
	}
	LaunchCharacter(Direction * Speed + FVector(0.f, 0.f, 120.f), true, false);
	DashCooldownLeft = Stats.DashCooldown;
	// (the dive pose itself plays from the dash detection in UpdateSkinAnimation, which runs on every machine)
	// The dash slips through enemy bodies (never stuck in a crowd for good). Runs on the owner and the server alike.
	DashPassLeft = D ? FMath::Max(0.f, D->Tune(TEXT("DashPassSeconds"), 0.3f)) : 0.3f;
	// Kick-off dust, speed streaks and a flick in the reaper's colour (FxRecipes.csv */Dash; drawn where this runs: owner and host)
	if (UWorld* DashWorld = GetWorld())
	{
		SQFxRecipe::FContext DashFx;
		DashFx.Origin = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		DashFx.Dir = -Direction;
		DashFx.SetColor(SQTheme::Has(ReaperId) ? SQTheme::Colour(ReaperId, 1) : FLinearColor(0.36f, 0.95f, 0.84f));
		SQFxRecipe::Play(DashWorld, FName(TEXT("*")), FName(TEXT("Dash")), DashFx);
	}
}

// ---------------------------------------------------------------- movement tech (slide, hop, fast-fall, fall damage)

USQMovementComponent* ASQCharacter::GetSQMovement() const
{
	return Cast<USQMovementComponent>(GetCharacterMovement());
}

void ASQCharacter::SetSlideInput(bool bHeld)
{
	if (USQMovementComponent* SlideMove = GetSQMovement()) SlideMove->SetSlideHeld(bHeld);
}

bool ASQCharacter::CanSlideNow() const
{
	return !bDowned && !CarriedBy && !IsFrozenForLobby();
}

bool ASQCharacter::IsSliding() const
{
	if (bDowned) return false;
	// Remote views only get the replicated crouch, and the slide is the only thing that crouches a reaper
	if (GetLocalRole() == ROLE_SimulatedProxy) return bIsCrouched != 0;
	const USQMovementComponent* SlideMove = GetSQMovement();
	return SlideMove && SlideMove->IsSliding();
}

bool ASQCharacter::CanJumpInternal_Implementation() const
{
	const USQMovementComponent* SlideMove = GetSQMovement();
	if (bIsCrouched && SlideMove && SlideMove->IsSliding()) return JumpIsAllowedInternal();
	return Super::CanJumpInternal_Implementation();
}

void ASQCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	USQMovementComponent* LandMove = GetSQMovement();
	if (!LandMove) return;
	float DropHeight = 0.f;
	bool bSafeLanding = false;
	LandMove->HandleReaperLanded(DropHeight, bSafeLanding);
	// Landing dust and a ground ring, bigger from higher (FxRecipes.csv */Land)
	if (DropHeight > 60.f)
	{
		if (UWorld* LandWorld = GetWorld())
		{
			SQFxRecipe::PlayShared(LandWorld, FName(TEXT("Land")), GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight()), FLinearColor(0.72f, 0.69f, 0.63f), 100.f,
				FMath::Clamp(DropHeight / 300.f, 0.7f, 1.8f));
		}
	}
	// Only the server's own simulation applies damage (the owning client replays landings during corrections)
	if (HasAuthority() && !bSafeLanding) ServerApplyFallDamage(DropHeight);
}

void ASQCharacter::LaunchCharacter(FVector LaunchVelocity, bool bXYOverride, bool bZOverride)
{
	Super::LaunchCharacter(LaunchVelocity, bXYOverride, bZOverride);
	// Jump pads launch straight up hard (JumpPadLaunch); the dash only hops (Z 120). Big launches land safely.
	const USQData* LaunchData = USQData::Get(this);
	const float SafeLaunchZ = LaunchData ? LaunchData->Tune(TEXT("JumpPadSafeLaunchZ"), 900.f) : 900.f;
	if (LaunchVelocity.Z >= SafeLaunchZ)
	{
		if (USQMovementComponent* LaunchMove = GetSQMovement()) LaunchMove->MarkFallSafe();
	}
}

void ASQCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	// The capsule centre dropped: keep the ground disc on the ground
	if (AuraMesh) AuraMesh->SetRelativeLocation(FVector(0.f, 0.f, -88.f + ScaledHalfHeightAdjust));
}

void ASQCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	if (AuraMesh) AuraMesh->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
}

void ASQCharacter::ServerApplyFallDamage(float DropHeight)
{
	if (!HasAuthority() || bDowned || bGodMode || CarriedBy || PickBubbleLeft > 0.f) return;
	const USQData* FallData = USQData::Get(this);
	const float MinHeight = FallData ? FallData->Tune(TEXT("FallDamageMinHeight"), 900.f) : 900.f;
	if (DropHeight <= MinHeight) return;
	const float PerMeter = FallData ? FMath::Max(0.f, FallData->Tune(TEXT("FallDamagePerMeter"), 0.04f)) : 0.04f;
	const float MaxFraction = FallData ? FMath::Clamp(FallData->Tune(TEXT("FallDamageMaxFraction"), 0.3f), 0.f, 1.f) : 0.3f;
	const float Fraction = FMath::Min(MaxFraction, (DropHeight - MinHeight) / 100.f * PerMeter);
	// A hazard, not a death trap: a fall alone never downs a reaper
	const float Taken = FMath::Min(Fraction * Stats.MaxHealth, Health - 1.f);
	if (Taken < 1.f) return;
	Health -= Taken;
	if (IsLocallyControlled()) HitFlash = 0.35f; // listen-server host doesn't get OnRep
	MulticastFX(0, GetActorLocation() - FVector(0.f, 0.f, 85.f), 200.f + DropHeight * 0.1f, FColor(140, 120, 100), 0.3f);
}

void ASQCharacter::ServerTickSlideKnock(float DeltaSeconds)
{
	SlideKnockTimer = FMath::Max(0.f, SlideKnockTimer - DeltaSeconds);
	if (SlideKnockTimer > 0.f || !IsSliding()) return;
	const UWorld* KnockWorld = GetWorld();
	const ASQGameState* KnockGS = KnockWorld ? KnockWorld->GetGameState<ASQGameState>() : nullptr;
	ASQHordeManager* KnockHorde = KnockGS ? KnockGS->Horde.Get() : nullptr;
	if (!KnockHorde) return;
	const FVector KnockVelocity = GetVelocity();
	const FVector2D KnockDir(KnockVelocity.X, KnockVelocity.Y);
	if (KnockDir.IsNearlyZero(1.f)) return;
	const USQData* KnockData = USQData::Get(this);
	const float KnockRadius = KnockData ? KnockData->Tune(TEXT("SlideKnockRadius"), 140.f) : 140.f;
	const float KnockStrength = KnockData ? KnockData->Tune(TEXT("SlideKnockStrength"), 450.f) : 450.f;
	const float KnockMaxEnemyRadius = KnockData ? KnockData->Tune(TEXT("SlideKnockMaxEnemyRadius"), 62.f) : 62.f;
	SlideKnockTimer = 0.1f; // about 10 Hz while the slide lasts
	KnockHorde->ApplyKnockback(Pos2D(), KnockRadius, KnockDir.GetSafeNormal(), KnockStrength, KnockMaxEnemyRadius);
}

void ASQCharacter::UpdateMovementFx(float DeltaSeconds)
{
	UWorld* FxWorld = GetWorld();
	const USQData* FxData = USQData::Get(this);
	if (!FxWorld || !FxData || GetNetMode() == NM_DedicatedServer) return;

	// Nov 19 2026 (effects pass): the team gaining a level bursts on every living reaper (FxRecipes.csv LevelUp). Seen-level starts unset so joining late does not fire it.
	if (const ASQGameState* FxGS = FxWorld->GetGameState<ASQGameState>())
	{
		if (FxSeenTeamLevel >= 0 && FxGS->TeamLevel > FxSeenTeamLevel && !bDowned)
		{
			SQFxRecipe::PlayShared(FxWorld, FName(TEXT("LevelUp")), GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f), FLinearColor(0.4f, 0.88f, 0.35f), 260.f);
		}
		FxSeenTeamLevel = FxGS->TeamLevel;
	}

	const bool bSlidingNow = IsSliding();
	const UCharacterMovementComponent* FxMove = GetCharacterMovement();
	const bool bFallingNow = FxMove && FxMove->IsFalling();
	const FVector FxVelocity = GetVelocity();
	const float FxSpeed = static_cast<float>(FxVelocity.Size2D());
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight - 5.f);
	const FLinearColor DustColour(0.42f, 0.36f, 0.3f);

	// Slide momentum (and the jumps out of it) must not read as a dash in the dodge-anim detector
	if (bSlidingNow) SlideMomentumGrace = 0.6f;
	else if (!bFallingNow) SlideMomentumGrace = FMath::Max(0.f, SlideMomentumGrace - DeltaSeconds);

	// Slide start: a dust ring (and the roll if this slide comes straight out of a long fall); dust trail while sliding
	LandRollWindow = FMath::Max(0.f, LandRollWindow - DeltaSeconds);
	if (bSlidingNow && !bWasSlidingFx)
	{
		SQFx::Ring(FxWorld, Feet, DustColour, 10, 260.f, 0.4f, 7.f);
		if (LandRollWindow > 0.f)
		{
			LandRollWindow = 0.f;
			PlayOneShot(TEXT("Dodge_Forward"), 1.7f, true, 0.05f);
			SQFx::Burst(FxWorld, Feet, DustColour, 8, 300.f, 0.5f, 9.f, -600.f);
		}
	}
	bWasSlidingFx = bSlidingNow;
	if (bSlidingNow && !bDowned)
	{
		SlideDustTimer -= DeltaSeconds;
		if (SlideDustTimer <= 0.f)
		{
			SlideDustTimer = 0.06f;
			const int32 Puffs = FxSpeed > 1200.f ? 3 : 2;
			SQFx::Burst(FxWorld, Feet - FxVelocity.GetSafeNormal2D() * 30.f, DustColour, Puffs, 140.f, 0.35f, 7.f, -300.f);
		}
	}

	// Camera FOV kick + speed lines: the local player's own view only
	if (!IsLocallyControlled() || !Camera) return;
	const float FxMinSpeed = FxData->Tune(TEXT("SpeedFxMinSpeed"), 1000.f);
	const float FxFullSpeed = FMath::Max(FxMinSpeed + 1.f, FxData->Tune(TEXT("SpeedFxFullSpeed"), 1800.f));
	const float SpeedAlpha = bDowned ? 0.f : FMath::Clamp((FxSpeed - FxMinSpeed) / (FxFullSpeed - FxMinSpeed), 0.f, 1.f);
	const float BaseFov = bTopDownCamera ? FMath::Clamp(FxData->Tune(TEXT("ChaseFov"), 90.f), 60.f, 110.f) : 75.f;
	const float WantFov = BaseFov + (bSlidingNow ? FxData->Tune(TEXT("SlideFovKick"), 8.f) : 0.f) + SpeedAlpha * FxData->Tune(TEXT("SpeedFovKick"), 6.f) * (bTopDownCamera ? 0.5f : 1.f);
	CameraFovNow = FMath::FInterpTo(CameraFovNow, FMath::Clamp(WantFov, 30.f, 120.f), DeltaSeconds, WantFov > CameraFovNow ? 10.f : 4.f);
	Camera->SetFieldOfView(CameraFovNow);

	if (SpeedAlpha <= 0.f)
	{
		SpeedLineTimer = 0.f;
		return;
	}
	SpeedLineTimer -= DeltaSeconds;
	if (SpeedLineTimer > 0.f) return;
	SpeedLineTimer = FMath::Lerp(0.08f, 0.03f, SpeedAlpha);
	const FVector RunDir = FxVelocity.GetSafeNormal2D();
	const FVector SideDir = FVector::CrossProduct(FVector::UpVector, RunDir);
	const FVector Centre = GetActorLocation();
	for (int32 LineIdx = 0; LineIdx < 2; ++LineIdx)
	{
		// Streaks just ahead of and around the reaper, pointing back along the run: the world rushing past
		const float Side = FMath::FRandRange(140.f, 380.f) * (FMath::RandBool() ? 1.f : -1.f);
		const FVector LineStart = Centre + RunDir * FMath::FRandRange(80.f, 420.f) + SideDir * Side + FVector(0.f, 0.f, FMath::FRandRange(-60.f, 200.f));
		const FVector LineEnd = LineStart - RunDir * FMath::FRandRange(160.f, 320.f) * (0.6f + 0.4f * SpeedAlpha);
		SQFx::Streak(FxWorld, LineStart, LineEnd, FLinearColor(0.95f, 0.97f, 1.f), FMath::FRandRange(3.f, 6.f), 0.14f);
	}
}

// ---------------------------------------------------------------- body blocking

float ASQCharacter::GetFeetAboveGround() const
{
	const UCharacterMovementComponent* FeetMove = GetCharacterMovement();
	if (!FeetMove || FeetMove->IsMovingOnGround()) return 0.f;
	const FVector Here = GetActorLocation();
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const float TerrainZ = ASQArena::GroundZAt(this, static_cast<float>(Here.X), static_cast<float>(Here.Y));
	return FMath::Max(0.f, static_cast<float>(Here.Z) - HalfHeight - TerrainZ);
}

void ASQCharacter::ApplyBodyBlock(FVector& InOutVelocity, float DeltaTime) const
{
	// Simulated proxies just follow the server; skulls roll free; a dash slips through
	if (GetLocalRole() == ROLE_SimulatedProxy || bDowned || CarriedBy || DashPassLeft > 0.f || DeltaTime <= 0.f) return;
	const UWorld* BlockWorld = GetWorld();
	const ASQGameState* BlockGS = BlockWorld ? BlockWorld->GetGameState<ASQGameState>() : nullptr;
	const ASQHordeManager* BlockHorde = BlockGS ? BlockGS->Horde.Get() : nullptr;
	const USQData* BlockData = USQData::Get(this);
	if (!BlockHorde || !BlockData || BlockData->Tune(TEXT("BodyBlockEnabled"), 1.f) < 0.5f) return;

	const float BodyRadius = ASQHordeManager::ReaperBodyRadius(this, BlockData); // collision pass: the capsule radius
	const float ContactSkin = FMath::Max(0.f, BlockData->Tune(TEXT("BodyBlockSkin"), 6.f));
	FSQBodyContacts Contacts;
	if (BlockHorde->GatherBodyContacts(Pos2D(), BodyRadius, ContactSkin, GetFeetAboveGround(), Contacts) == 0) return;

	FVector2D Flat(InOutVelocity.X, InOutVelocity.Y);
	// 1. No walking into a body: remove the inward part of the velocity (twice, so a wedge between two holds firm)
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const FSQBodyContact& Contact : Contacts)
		{
			const float Into = -static_cast<float>(FVector2D::DotProduct(Flat, Contact.Normal));
			if (Into > 0.f) Flat += Contact.Normal * Into;
		}
	}
	// 2. Ease out of any overlap with a capped outward speed (a velocity, never a teleport: a small client/server
	//    disagreement about where an enemy is costs a nudge, not a correction)
	FVector2D Overlap = FVector2D::ZeroVector;
	for (const FSQBodyContact& Contact : Contacts)
	{
		if (Contact.Depth > 0.f) Overlap += Contact.Normal * Contact.Depth;
	}
	const double OverlapLen = Overlap.Size();
	if (OverlapLen > 0.5)
	{
		const FVector2D OutDir = Overlap / OverlapLen;
		const float WantOut = FMath::Min(FMath::Max(0.f, BlockData->Tune(TEXT("BodyBlockMaxPush"), 350.f)),
			static_cast<float>(OverlapLen) * FMath::Max(0.f, BlockData->Tune(TEXT("BodyBlockPushStiffness"), 10.f)));
		const float OutNow = static_cast<float>(FVector2D::DotProduct(Flat, OutDir));
		if (OutNow < WantOut) Flat += OutDir * (WantOut - OutNow);
	}
	InOutVelocity.X = Flat.X;
	InOutVelocity.Y = Flat.Y;
}

void ASQCharacter::ServerUpdateMoveTolerance(float DeltaSeconds)
{
	// Only remote human reapers send client moves (the host and bots move on the server itself)
	UCharacterMovementComponent* TolMove = GetCharacterMovement();
	const AController* TolController = GetController();
	const bool bRemoteHuman = TolController && TolController->IsPlayerController() && !IsLocallyControlled();
	bool bWant = false;
	if (TolMove && bRemoteHuman)
	{
		const USQData* TolData = USQData::Get(this);
		const UWorld* TolWorld = GetWorld();
		const ASQGameState* TolGS = TolWorld ? TolWorld->GetGameState<ASQGameState>() : nullptr;
		const ASQHordeManager* TolHorde = TolGS ? TolGS->Horde.Get() : nullptr;
		bool bTouching = DashPassLeft > 0.f;
		if (!bTouching && TolHorde && TolData && TolData->Tune(TEXT("BodyBlockEnabled"), 1.f) > 0.5f && !bDowned)
		{
			// Anything near enough that the owner's (interpolated, slightly older) view of the horde could disagree
			FSQBodyContacts NearBodies;
			const float NearRadius = ASQHordeManager::ReaperBodyRadius(this, TolData) + TolData->Tune(TEXT("BodyBlockGraceRange"), 120.f);
			bTouching = TolHorde->GatherBodyContacts(Pos2D(), NearRadius, 0.f, 0.f, NearBodies) > 0;
		}
		if (bTouching) BodyContactGrace = TolData ? TolData->Tune(TEXT("BodyBlockGraceSeconds"), 0.5f) : 0.5f;
		else BodyContactGrace = FMath::Max(0.f, BodyContactGrace - DeltaSeconds);
		bWant = BodyContactGrace > 0.f;
	}
	if (TolMove && bWant != bBodyTolerance)
	{
		// UE's documented pair for short bursts that are hard to sync: skip the error check, keep the client's position
		bBodyTolerance = bWant;
		TolMove->bIgnoreClientMovementErrorChecksAndCorrection = bWant;
		TolMove->bServerAcceptClientAuthoritativePosition = bWant;
	}
}

// ---------------------------------------------------------------- visuals

namespace
{
	/**
	 * Kit passives (Kits.csv PassiveId) whose ability moment plays full body even while moving, and those that already draw
	 * their own effect at the reaper. The clips themselves come from Kits.csv PassiveAnim (USQAbilityComponent::PlayPassiveAnim).
	 */
	struct FSQAbilityAnimRow
	{
		const TCHAR* Passive;
		bool bAlwaysFullBody;
		bool bOwnFx;
	};

	const FSQAbilityAnimRow SQAbilityAnimRows[] = {
		{ TEXT("DashStrike"), true, true },  { TEXT("Scorch"), true, true },     { TEXT("LastRites"), true, true },
		{ TEXT("Triage"), false, true },     { TEXT("OnTheJob"), false, true },  { TEXT("MobileBank"), false, true },
		{ TEXT("Stopwatch"), false, true },  { TEXT("Builder"), false, true },   { TEXT("Seance"), false, true },
		{ TEXT("Audit"), false, true },      { TEXT("TeamDamage"), false, true }, { TEXT("Lookout"), false, true },
	};

	const FSQAbilityAnimRow* FindSQAbilityAnimRow(FName PassiveName)
	{
		if (PassiveName.IsNone()) return nullptr;
		for (const FSQAbilityAnimRow& AbilityRow : SQAbilityAnimRows)
		{
			if (PassiveName == AbilityRow.Passive) return &AbilityRow;
		}
		return nullptr;
	}
}

void ASQCharacter::MulticastFX_Implementation(uint8 Kind, FVector_NetQuantize Location, float Radius, FColor Color, float Duration)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (Kind == SQFxKindCombo)
	{
		// Squad combo burst: a layered impact, and the COMBO callout for the local reaper when it went off near them
		UWorld* ComboWorld = GetWorld();
		ASQFx::Spawn(ComboWorld, 0, Location, Radius, FLinearColor(Color), Duration);
		SQFx::Burst(ComboWorld, FVector(Location) + FVector(0.f, 0.f, 60.f), FLinearColor(Color), 14, 620.f, 0.45f, 12.f, -500.f);
		const APlayerController* LocalPC = ComboWorld ? ComboWorld->GetFirstPlayerController() : nullptr;
		ASQCharacter* LocalReaper = LocalPC ? Cast<ASQCharacter>(LocalPC->GetPawn()) : nullptr;
		if (LocalReaper && FVector::Dist2D(LocalReaper->GetActorLocation(), FVector(Location)) <= 2200.f)
		{
			const float ComboNow = ComboWorld->GetTimeSeconds();
			LocalReaper->ComboFlashCount = ComboNow - LocalReaper->ComboFlashAt <= 2.5f ? LocalReaper->ComboFlashCount + 1 : 1;
			LocalReaper->ComboFlashAt = ComboNow;
			if (LocalReaper == this) NudgeCamera(0.12f);
		}
		return;
	}
	ASQFx::Spawn(GetWorld(), Kind, Location, Radius, FLinearColor(Color), Duration);
}

// ---------------------------------------------------------------- co-op pass (Nov 23 2026, Docs/CoopFeatures.md)

void ASQCharacter::OnSquadCombo(const FVector2D& At, float Radius, float Damage)
{
	if (!HasAuthority()) return;
	const USQData* D = USQData::Get(this);
	const float AtX = static_cast<float>(At.X);
	const float AtY = static_cast<float>(At.Y);
	MulticastFX(SQFxKindCombo, FVector(AtX, AtY, ASQArena::GroundZAt(this, AtX, AtY) + 20.f), Radius, FColor(255, 214, 92), 0.4f);
	AddDamagePop(At, Damage, 2.f);
	if (Abilities) Abilities->AddUltCharge(D ? D->Tune(TEXT("ComboUltCharge"), 0.008f) : 0.008f, true);
	if (ASQPlayerState* ComboPS = GetPlayerState<ASQPlayerState>()) ComboPS->CreditQuest(TEXT("SquadCombos"));
}

void ASQCharacter::RewardRevive(ASQCharacter* Revived)
{
	if (!HasAuthority() || bDowned || !Revived || Revived == this) return;
	const USQData* D = USQData::Get(this);
	const float HealFrac = D ? FMath::Clamp(D->Tune(TEXT("ReviveRewardHeal"), 0.15f), 0.f, 1.f) : 0.15f;
	const float Haste = D ? FMath::Clamp(D->Tune(TEXT("ReviveRewardHaste"), 0.2f), 0.f, 1.f) : 0.2f;
	const float HasteTime = D ? D->Tune(TEXT("ReviveRewardHasteTime"), 5.f) : 5.f;
	Health = FMath::Min(Stats.MaxHealth, Health + Stats.MaxHealth * HealFrac);
	if (Abilities)
	{
		Abilities->AddUltCharge(D ? D->Tune(TEXT("ReviveRewardUlt"), 0.1f) : 0.1f, false);
		if (Haste > 0.f && HasteTime > 0.f) Abilities->GiveBuff(SQAbilityFx::Haste, Haste, HasteTime);
	}
	if (Revived->Abilities && Haste > 0.f && HasteTime > 0.f) Revived->Abilities->GiveBuff(SQAbilityFx::Haste, Haste, HasteTime);
	MulticastFX(0, GetActorLocation() - FVector(0.f, 0.f, 85.f), 240.f, FColor(150, 255, 170), 0.45f);
	if (ASQPlayerState* SaviourPS = GetPlayerState<ASQPlayerState>()) SaviourPS->CreditQuest(TEXT("Revives"));
}

void ASQCharacter::ServerTickGhost(float DeltaSeconds)
{
	// Ghost shift: something to do while down. The spirit moves at DownedSpeed, gathers XP and gold (horde SimPickups),
	// recharges Haunt, and stands back up when the next lantern lights.
	const USQData* D = USQData::Get(this);
	const ASQGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	if (!D || !GS) return;
	const float Cooldown = FMath::Max(0.5f, D->Tune(TEXT("GhostHauntCooldown"), 5.f));
	GhostHauntLeft = FMath::Max(0.f, GhostHauntLeft - DeltaSeconds);
	GhostHauntCharge = 1.f - FMath::Clamp(GhostHauntLeft / Cooldown, 0.f, 1.f);
	if (GhostTrailStageSeen < 0) GhostTrailStageSeen = GS->TrailStage;
	else if (GS->TrailStage != GhostTrailStageSeen)
	{
		GhostTrailStageSeen = GS->TrailStage;
		if (D->Tune(TEXT("GhostLanternRevive"), 1.f) > 0.5f && !IsPardonPending())
		{
			Revive(D->Tune(TEXT("ReviveFraction"), 0.5f));
			MulticastFX(0, GetActorLocation() - FVector(0.f, 0.f, 85.f), 420.f, FColor(255, 214, 120), 0.6f);
			if (ASQGameState* AlertGS = GetWorld()->GetGameState<ASQGameState>())
			{
				const APlayerState* GhostPS = GetPlayerState();
				AlertGS->PushAlert(FString::Printf(TEXT("%s rises with the new lantern!"), GhostPS ? *GhostPS->GetPlayerName() : TEXT("A reaper")));
			}
			return;
		}
	}
	// Bots haunt whenever it is ready
	const ASQPlayerState* GhostState = GetPlayerState<ASQPlayerState>();
	if (GhostState && GhostState->IsABot() && GhostHauntLeft <= 0.f && !CarriedBy) TryGhostHaunt();
}

bool ASQCharacter::TryGhostHaunt()
{
	if (!HasAuthority() || !bDowned || GhostHauntLeft > 0.f) return false;
	const USQData* D = USQData::Get(this);
	const ASQGameState* HauntGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	ASQHordeManager* Horde = HauntGS ? HauntGS->Horde.Get() : nullptr;
	if (!D || !Horde || D->Tune(TEXT("GhostHaunt"), 1.f) <= 0.5f) return false;
	uint16 MarkedId = 0;
	FVector2D MarkedPos = FVector2D::ZeroVector;
	if (!Horde->MarkToughestEnemy(Pos2D(), D->Tune(TEXT("GhostHauntRange"), 1200.f), D->Tune(TEXT("GhostHauntBonus"), 0.2f), D->Tune(TEXT("GhostHauntSeconds"), 5.f), MarkedId, MarkedPos))
	{
		return false;
	}
	GhostHauntLeft = FMath::Max(0.5f, D->Tune(TEXT("GhostHauntCooldown"), 5.f));
	GhostHauntCharge = 0.f;
	const float MarkX = static_cast<float>(MarkedPos.X);
	const float MarkY = static_cast<float>(MarkedPos.Y);
	TArray<FVector_NetQuantize> Line;
	Line.Add(FVector_NetQuantize(GetActorLocation()));
	Line.Add(FVector_NetQuantize(FVector(MarkX, MarkY, ASQArena::GroundZAt(this, MarkX, MarkY) + 110.f)));
	MulticastLineFX(Line, FColor(170, 230, 255), 0);
	return true;
}

void ASQCharacter::ServerPing_Implementation(uint8 Kind, FVector_NetQuantize Where)
{
	// A burst of 3, then one every PingCooldown seconds
	const USQData* D = USQData::Get(this);
	const UWorld* PingWorld = GetWorld();
	if (!PingWorld || Kind > 5 || FVector(Where).ContainsNaN()) return;
	const float Cooldown = D ? FMath::Max(0.2f, D->Tune(TEXT("PingCooldown"), 1.5f)) : 1.5f;
	const float PingNow = PingWorld->GetTimeSeconds();
	PingBudget = FMath::Min(3.f, PingBudget + (PingNow - LastPingTime) / Cooldown);
	LastPingTime = PingNow;
	if (PingBudget < 1.f) return;
	PingBudget -= 1.f;
	uint8 Slot = 0;
	const AGameStateBase* PingGS = PingWorld->GetGameState();
	const APlayerState* MyPS = GetPlayerState();
	if (PingGS && MyPS) Slot = static_cast<uint8>(FMath::Clamp(PingGS->PlayerArray.IndexOfByKey(MyPS), 0, 7));
	MulticastPing(Kind, Where, Slot);
}

void ASQCharacter::MulticastPing_Implementation(uint8 Kind, FVector_NetQuantize Where, uint8 Slot)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* PingWorld = GetWorld();
	ASQGameState* PingGS = PingWorld ? PingWorld->GetGameState<ASQGameState>() : nullptr;
	if (!PingGS) return;
	const APlayerState* PingerPS = GetPlayerState();
	PingGS->AddLocalPing(Kind, FVector(Where), Slot, PingerPS ? PingerPS->GetPlayerName() : FString());
	const FLinearColor PingColor = ASQGameState::PingColor(Slot);
	if (USoundBase* PingSound = SQ::Sound(TEXT("Soul")))
	{
		UGameplayStatics::PlaySoundAtLocation(this, PingSound, FVector(Where), 0.8f, Kind == 4 ? 0.6f : (Kind == 1 ? 0.8f : 1.25f));
	}
	ASQFx::Spawn(PingWorld, 0, FVector(Where), 180.f, PingColor, 0.5f);
	SQFx::Burst(PingWorld, FVector(Where) + FVector(0.f, 0.f, 80.f), PingColor, 8, 380.f, 0.5f, 10.f, -300.f);
}

void ASQCharacter::MulticastLineFX_Implementation(const TArray<FVector_NetQuantize>& Points, FColor Color, uint8 Style)
{
	if (GetNetMode() == NM_DedicatedServer || Points.Num() < 2) return;
	UWorld* World = GetWorld();
	const FLinearColor LineCol(Color);
	const float Step = Style == 1 ? 70.f : 55.f;
	for (int32 i = 0; i + 1 < Points.Num(); ++i)
	{
		const FVector A(Points[i].X, Points[i].Y, Points[i].Z - 30.f);
		const FVector B(Points[i + 1].X, Points[i + 1].Y, Points[i + 1].Z - 30.f);
		const float Len = static_cast<float>(FVector::Dist(A, B));
		const int32 Sparks = FMath::Clamp(FMath::CeilToInt(Len / Step), 1, 40);
		for (int32 k = 0; k <= Sparks; ++k)
		{
			FVector Spot = FMath::Lerp(A, B, static_cast<float>(k) / Sparks);
			if (Style == 0) Spot += FVector(FMath::FRandRange(-18.f, 18.f), FMath::FRandRange(-18.f, 18.f), FMath::FRandRange(-12.f, 12.f)); // jagged ghost-lightning
			SQFx::Burst(World, Spot, LineCol, 1, Style == 1 ? 40.f : 90.f, Style == 1 ? 0.22f : 0.18f, Style == 1 ? 14.f : 10.f, 0.f);
		}
		if (Style == 0) SQFx::Burst(World, B, LineCol, 6, 260.f, 0.3f, 9.f, -400.f); // zap at each target
	}
}

namespace
{
	/** Attack juice variants (Attack storyboard): Slash crescents, Slam ground hits, Thrust lunges, Cast charge + burst, Throw release */
	namespace SQSwingFamily
	{
		constexpr uint8 Slash = 0;
		constexpr uint8 Slam = 1;
		constexpr uint8 Thrust = 2;
		constexpr uint8 Cast = 3;
		constexpr uint8 Throw = 4;
	}

	/** How each AttackStyle (Upgrades.csv) animates: clips tried after the weapon's own AttackAnim list */
	struct FSQAttackStyleRow
	{
		const TCHAR* Style;
		const TCHAR* Anims[4];   // remastered (R2) takes first, the KayKit originals after them as fallbacks
		const TCHAR* Finisher[2]; // every third attack (combo ender), first clip the model has
		float Rate;              // clip play rate
		float ImpactFrac;        // fraction of the clip where the blow lands (hit-stop + squash)
		bool bTrail;             // draws a trail from the weapon tip during the swing
		float Squash;            // squash (-) or stretch (+) at the impact
		uint8 Family;            // attack juice variant (Attack storyboard): SQSwingFamily
	};

	const FSQAttackStyleRow SQAttackStyleRows[] = {
		{ TEXT("Slash"),  { TEXT("Reaper_Scythe_Swing"), TEXT("SQ_Attack_Slash_R2"), TEXT("1H_Melee_Attack_Slice_Horizontal"), TEXT("2H_Melee_Attack_Slice") }, { TEXT("Reaper_Scythe_Slam"), TEXT("2H_Melee_Attack_Spin") }, 1.6f, 0.35f, true, -0.10f, SQSwingFamily::Slash },
		{ TEXT("Chop"),   { TEXT("SQ_Attack_Chop_R2"), TEXT("2H_Melee_Attack_Chop"), TEXT("1H_Melee_Attack_Chop"), nullptr }, { TEXT("1H_Melee_Attack_Jump_Chop"), nullptr }, 1.5f, 0.42f, true, -0.16f, SQSwingFamily::Slam },
		{ TEXT("Spin"),   { TEXT("2H_Melee_Attack_Spin"), TEXT("2H_Melee_Attack_Spinning"), nullptr }, { nullptr, nullptr }, 1.5f, 0.40f, true, 0.08f, SQSwingFamily::Slash },
		{ TEXT("Stab"),   { TEXT("SQ_Attack_Thrust_R3"), TEXT("1H_Melee_Attack_Stab"), TEXT("2H_Melee_Attack_Stab"), nullptr }, { nullptr, nullptr }, 1.7f, 0.35f, true, 0.08f, SQSwingFamily::Thrust },
		{ TEXT("Cast"),   { TEXT("SQ_Cast_Shoot_R2"), TEXT("Spellcast_Shoot"), TEXT("Spellcasting"), TEXT("Spellcast_Raise") }, { nullptr, nullptr }, 1.7f, 0.30f, false, 0.06f, SQSwingFamily::Cast },
		{ TEXT("Raise"),  { TEXT("SQ_Cast_Raise_R2"), TEXT("Spellcast_Raise"), TEXT("Spellcast_Long"), nullptr }, { nullptr, nullptr }, 1.5f, 0.45f, false, -0.12f, SQSwingFamily::Cast },
		{ TEXT("Throw"),  { TEXT("SQ_Throw_R2"), TEXT("Throw"), TEXT("1H_Melee_Attack_Slice_Diagonal"), nullptr }, { nullptr, nullptr }, 1.8f, 0.30f, true, 0.06f, SQSwingFamily::Throw },
		{ TEXT("Shoot"),  { TEXT("1H_Ranged_Shoot"), TEXT("2H_Ranged_Shoot"), TEXT("SQ_Cast_Shoot_R2"), TEXT("Spellcast_Shoot") }, { nullptr, nullptr }, 1.8f, 0.20f, false, -0.05f, SQSwingFamily::Throw },
		{ TEXT("Slam"),   { TEXT("SQ_Attack_Slam_R3"), TEXT("Reaper_Scythe_Slam"), TEXT("SQ_Attack_Chop_R2"), TEXT("2H_Melee_Attack_Chop") }, { nullptr, nullptr }, 1.4f, 0.50f, true, -0.20f, SQSwingFamily::Slam },
		{ TEXT("Summon"), { TEXT("SQ_Cast_Summon_R2"), TEXT("Spellcast_Summon"), TEXT("Interact"), TEXT("Use_Item") }, { nullptr, nullptr }, 1.5f, 0.40f, false, -0.08f, SQSwingFamily::Cast },
	};

	const FSQAttackStyleRow& FindSQAttackStyle(const FSQUpgradeDef& Def)
	{
		FName StyleName = Def.AttackStyle;
		if (StyleName.IsNone())
		{
			// No AttackStyle column value: pick one from the behaviour
			switch (Def.Behaviour)
			{
			case ESQWeaponBehaviour::Arc: StyleName = TEXT("Slash"); break;
			case ESQWeaponBehaviour::Projectile: StyleName = TEXT("Throw"); break;
			case ESQWeaponBehaviour::Nova: StyleName = TEXT("Raise"); break;
			case ESQWeaponBehaviour::Drop: StyleName = TEXT("Slam"); break;
			case ESQWeaponBehaviour::Turret: StyleName = TEXT("Summon"); break;
			case ESQWeaponBehaviour::Beam: StyleName = TEXT("Shoot"); break;
			case ESQWeaponBehaviour::Orbit: StyleName = TEXT("Spin"); break;
			default: StyleName = TEXT("Cast"); break;
			}
		}
		for (const FSQAttackStyleRow& StyleRow : SQAttackStyleRows)
		{
			if (StyleName == StyleRow.Style) return StyleRow;
		}
		return SQAttackStyleRows[4]; // Cast
	}

	/** Dark weapon colours (crows, black death) still need to read as light: lift them toward white */
	FLinearColor SQFxBright(const FLinearColor& In)
	{
		const float MaxC = FMath::Max3(In.R, In.G, In.B);
		return MaxC < 0.35f ? FMath::Lerp(In, FLinearColor(0.75f, 0.7f, 0.85f), 0.55f) : In;
	}

	/** Point on a circle of radius R around C (2D, Z kept) */
	FVector SQFxOnCircle(const FVector& C, float R, float Deg)
	{
		const float Rad = FMath::DegreesToRadians(Deg);
		return C + FVector(FMath::Cos(Rad) * R, FMath::Sin(Rad) * R, 0.f);
	}
}

void ASQCharacter::MulticastWeaponFire_Implementation(FName WeaponId, uint8 Flags, float Radius, const TArray<FVector_NetQuantize>& Points)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	const USQData* D = USQData::Get(this);
	const FSQUpgradeDef* Def = D ? D->FindUpgrade(WeaponId) : nullptr;
	if (!Def) return;
	const bool bCrit = (Flags & 1) != 0;
	if ((Flags & 2) != 0) PlayAttack(*Def, bCrit);
	if ((Flags & 4) != 0) PlayWeaponFx(*Def, bCrit, Radius, Points);
}

void ASQCharacter::PlayAttack(const FSQUpgradeDef& Def, bool bCrit)
{
	if (bDowned) return;
	const FSQAttackStyleRow& Style = FindSQAttackStyle(Def);
	const float Rate = Style.Rate * (bCrit ? 0.9f : 1.f); // crits wind up a touch heavier
	AttackComboCount++;

	// Which clip: the combo finisher every third swing, then the weapon's own list, then the style's stand-ins
	TArray<FName, TInlineAllocator<8>> Tries;
	if (AttackComboCount % 3 == 0)
	{
		for (const TCHAR* FinisherAnim : Style.Finisher)
		{
			if (FinisherAnim) Tries.Add(FName(FinisherAnim));
		}
	}
	for (const FName& Listed : Def.AttackAnims) Tries.Add(Listed);
	for (const TCHAR* StyleAnim : Style.Anims)
	{
		if (StyleAnim) Tries.Add(FName(StyleAnim));
	}

	float ClipSeconds = 0.f;
	if (bHasSkin)
	{
		if (USQAnimInstance* SkinAnim = GetSkinAnim())
		{
			for (const FName& Try : Tries)
			{
				if (SkinAnim->PlayAction(Try, Rate, false, 0.06f))
				{
					const UAnimSequence* Played = SQArt::Anim(SkinModel, Try);
					ClipSeconds = Played ? static_cast<float>(Played->GetPlayLength()) / Rate : 0.6f;
					break;
				}
			}
		}
	}
	if (ClipSeconds <= 0.f && bHasSkin && GetSkinAnim() && GetSkinAnim()->IsActionPlaying()) return; // a bigger action owns the body

	// Timing on every machine is the same: the multicast arrives, the clip starts and coils slowly for its first frames
	// (anticipation, USQAnimInstance::WindUp), the blow lands at ImpactFrac of the clip
	const USQData* JuiceData = USQData::Get(this);
	const float WindSeconds = JuiceData ? JuiceData->Tune(TEXT("AttackWindUpSeconds"), 0.133f) : 0.133f;
	const float WindRate = JuiceData ? JuiceData->Tune(TEXT("AttackWindUpRate"), 0.5f) : 0.5f;
	const float Swing = ClipSeconds > 0.f ? ClipSeconds : 0.35f;
	float ImpactIn = Swing * Style.ImpactFrac;
	USQAnimInstance* WindAnim = ClipSeconds > 0.f ? GetSkinAnim() : nullptr;
	if (WindAnim && WindSeconds > 0.f)
	{
		WindAnim->WindUp(WindSeconds, WindRate);
		ImpactIn = USQAnimInstance::TimeToClipFraction(Swing, Style.ImpactFrac, WindSeconds, WindRate);
	}
	else
	{
		ImpactIn = FMath::Max(ImpactIn, WindSeconds); // no clip: still give the eye ~4 frames of wind-up
	}
	ImpactIn = FMath::Clamp(ImpactIn, 0.08f, 0.55f);
	PendingSwingSquash = Style.Squash * (bCrit ? 1.5f : 1.f);
	PendingHitStopSeconds = bCrit ? (JuiceData ? JuiceData->Tune(TEXT("AttackHitStopCritSeconds"), 0.09f) : 0.09f)
		: (JuiceData ? JuiceData->Tune(TEXT("AttackHitStopSeconds"), 0.06f) : 0.06f);
	WeaponTrailTime = Style.bTrail ? FMath::Clamp(ImpactIn + 0.25f, 0.2f, 0.7f) : 0.f;
	WeaponTrailColor = SQFxBright(bCrit ? FMath::Lerp(Def.Color, FLinearColor(1.f, 0.71f, 0.28f), 0.5f) : Def.Color);
	bLastTipValid = false;
	WeaponGlow = bCrit ? 3.f : 1.6f;
	// The five beats (anticipation now; smear, impact, follow-through and settle in TickSwingJuice)
	StartSwingJuice(Style.Family, ImpactIn, bCrit, false);
}

void ASQCharacter::PlayWeaponFx(const FSQUpgradeDef& Def, bool bCrit, float Radius, const TArray<FVector_NetQuantize>& Points)
{
	UWorld* World = GetWorld();
	if (!World) return;
	// Nov 19 2026: a reaper's signature weapon (and its evolution) swings in the reaper's own effect colour, not the weapon's
	// card colour (several are near-white, which is why swings read as plain white arcs). StyleFxSignatureColour 0 = old.
	FLinearColor Col = SQFxBright(Def.Color);
	{
		const USQData* ColData = USQData::Get(this);
		const FSQKitDef* ColKit = ColData ? ColData->FindKit(ReaperId) : nullptr;
		if (ColKit && !ColKit->SignatureWeapon.IsNone() && (Def.Id == ColKit->SignatureWeapon || Def.EvolvesFrom == ColKit->SignatureWeapon)
			&& ColData->Tune(TEXT("StyleFxSignatureColour"), 1.f) > 0.f && SQTheme::Has(ReaperId))
		{
			Col = SQTheme::Colour(ReaperId, 1);
		}
	}
	const FLinearColor CritCol(1.f, 0.71f, 0.28f);
	const FLinearColor HitCol = bCrit ? FMath::Lerp(Col, CritCol, 0.6f) : Col;
	const FLinearColor Hot = FMath::Lerp(Col, FLinearColor::White, 0.55f);
	const bool bEvolved = Def.Kind == ESQUpgradeKind::Evolution;
	const float Big = bEvolved ? 1.4f : 1.f;
	const FName Fx = Def.FxStyle;
	const FVector Here = GetActorLocation();
	const float Yaw = static_cast<float>(GetActorRotation().Yaw);
	const FVector GroundO(Here.X, Here.Y, ASQArena::GroundZAt(this, static_cast<float>(Here.X), static_cast<float>(Here.Y)) + 8.f);
	auto GroundAt = [this](const FVector& Spot) -> FVector
	{
		return FVector(Spot.X, Spot.Y, ASQArena::GroundZAt(this, static_cast<float>(Spot.X), static_cast<float>(Spot.Y)) + 8.f);
	};
	// The muzzle: the held weapon's tip if one is shown, else in front of the chest
	FVector Muzzle;
	if (!GetWeaponTip(Muzzle)) Muzzle = Here + GetActorForwardVector() * 50.f + FVector(0.f, 0.f, 25.f);
	// Attack juice timing (PlayAttack ran first in the same multicast): the smear leads the blow, hits land on the blow,
	// ranged releases wait a little for the cast / throw gesture (capped so the damage numbers still line up)
	const bool bTimedSwing = HitStopAt >= 0.f;
	const USQData* FxData = USQData::Get(this);
	const float SmearLead = FxData ? FxData->Tune(TEXT("AttackSmearSeconds"), 0.067f) : 0.067f;
	const float HitDelay = bTimedSwing ? HitStopAt : 0.08f;
	const float SmearDelay = bTimedSwing ? FMath::Max(0.f, HitStopAt - SmearLead) : 0.f;
	const float ReleaseDelay = bTimedSwing ? FMath::Min(HitStopAt, FxData ? FxData->Tune(TEXT("AttackReleaseMaxDelay"), 0.15f) : 0.15f) : 0.f;
	// The monster's own accent on its signature weapon (and that weapon's evolution), landing on the blow (SQFxTheme, Nov 10 2026)
	if (const FSQKitDef* ThemeKit = FxData ? FxData->FindKit(ReaperId) : nullptr)
	{
		if (!ThemeKit->SignatureWeapon.IsNone() && (Def.Id == ThemeKit->SignatureWeapon || Def.EvolvesFrom == ThemeKit->SignatureWeapon))
		{
			SQTheme::PlayAttack(World, ReaperId, GroundO, GetActorForwardVector(), Radius > 1.f ? Radius : 300.f, bCrit, bEvolved, HitDelay);
		}
	}
	// Hit sparks at the enemies the server reported (Points[1..] for area weapons): the target flashes white, sparks fly,
	// and a skid puff is shoved away from the reaper (the knockback, Tuning WeaponKnockback)
	auto AreaImpacts = [&](float Delay)
	{
		for (int32 HitIdx = 1; HitIdx < Points.Num(); ++HitIdx)
		{
			const FVector HitSpot = GroundAt(Points[HitIdx]);
			const FVector HitAt = HitSpot + FVector(0.f, 0.f, 80.f);
			const float HitAtDelay = Delay + 0.02f * HitIdx;
			SQFx::Flash(World, HitAt, FLinearColor::White, 85.f * Big, 0.07f, HitAtDelay);
			SQFx::Impact(World, HitAt, HitCol, Big, bCrit, HitAtDelay);
			FVector Away = HitSpot - Here;
			Away.Z = 0.f;
			Away = Away.GetSafeNormal();
			FSQFxParticle Skid;
			Skid.Pos = HitSpot + FVector(0.f, 0.f, 12.f);
			Skid.Vel = Away * 420.f + FVector(0.f, 0.f, 50.f);
			Skid.Drag = 5.f;
			Skid.Color = FLinearColor(0.55f, 0.5f, 0.47f);
			Skid.Delay = HitAtDelay + 0.06f;
			Skid.Life = 0.35f;
			Skid.SizeStart = FVector(16.f);
			Skid.SizeEnd = FVector(30.f);
			SQFx::Emit(World, Skid);
		}
	};

	switch (Def.Behaviour)
	{
	case ESQWeaponBehaviour::Arc:
	{
		const float R = FMath::Max(60.f, Radius);
		const float Sweep = (ArcSwingCount++ % 2 == 0) ? 360.f : -360.f;
		bSwingFxDrawn = true; // this effect is the swing's smear + impact: the attack juice skips its own
		if (Fx == TEXT("Hymn"))
		{
			// Three mournful ripples + rising notes
			for (int32 Ripple = 0; Ripple < 3; ++Ripple) SQFx::Shockwave(World, GroundO + FVector(0.f, 0.f, 20.f * Ripple), R, Col, 0.45f, 26.f * Big, HitDelay + 0.13f * Ripple);
			for (int32 Note = 0; Note < 8; ++Note)
			{
				FSQFxParticle P;
				P.Pos = SQFxOnCircle(GroundO, FMath::FRandRange(0.2f, 0.9f) * R, FMath::FRandRange(0.f, 360.f)) + FVector(0.f, 0.f, 40.f);
				P.Vel = FVector(0.f, 0.f, 160.f);
				P.Color = Hot;
				P.Life = 0.7f;
				P.Delay = HitDelay + 0.05f * Note;
				P.SizeStart = P.SizeEnd = FVector(16.f, 16.f, 22.f);
				P.TrailEvery = 0.05f;
				P.TrailSize = 4.f;
				P.TrailColor = Col;
				SQFx::Emit(World, P);
			}
		}
		else if (Fx == TEXT("Smack"))
		{
			// A brutal ground thump: short front arc, shockwave and flung clods
			SQFx::SwingArc(World, GroundO + FVector(0.f, 0.f, 50.f), Yaw - 80.f, 160.f, R * 0.7f, Col, 46.f * Big, 0.22f, SmearDelay);
			SQFx::Shockwave(World, GroundO, R, Col, 0.3f, 48.f * Big, HitDelay);
			SQFx::Flash(World, GroundO + GetActorForwardVector() * 60.f, Hot, 90.f * Big, 0.12f, HitDelay);
			SQFx::GroundCracks(World, GroundO + GetActorForwardVector() * 60.f, R * 0.45f, Col, 6, HitDelay + 0.8f, 6.f * Big, HitDelay);
			for (int32 Clod = 0; Clod < 8; ++Clod)
			{
				FSQFxParticle P;
				const float Ang = FMath::FRandRange(0.f, 360.f);
				P.Pos = SQFxOnCircle(GroundO, R * 0.3f, Ang);
				P.Vel = SQFxOnCircle(FVector::ZeroVector, FMath::FRandRange(200.f, 420.f), Ang) + FVector(0.f, 0.f, FMath::FRandRange(450.f, 700.f));
				P.Gravity = -1800.f;
				P.FloorZ = GroundO.Z;
				P.Shape = ESQFxShape::Cube;
				P.Color = FMath::Lerp(Col, FLinearColor(0.35f, 0.25f, 0.15f), 0.5f);
				P.Life = 0.7f;
				P.Delay = HitDelay;
				P.SizeStart = P.SizeEnd = FVector(FMath::FRandRange(12.f, 22.f)) * Big;
				P.SpinAxis = FMath::VRand();
				P.SpinRate = 10.f;
				SQFx::Emit(World, P);
			}
		}
		else
		{
			// Sweep: a full crescent around the reaper at the blade's reach, plus a thinner inner echo
			// (the ghostly smear: wide and soft, the hot edge thin; both lead the blow by the smear beat)
			SQFx::SwingArc(World, GroundO + FVector(0.f, 0.f, 55.f), Yaw + 180.f, Sweep, R * 0.85f, FMath::Lerp(Col, FLinearColor::White, 0.2f), 58.f * Big, 0.3f, SmearDelay);
			SQFx::SwingArc(World, GroundO + FVector(0.f, 0.f, 45.f), Yaw + 200.f, Sweep * 0.9f, R * 0.5f, Hot, 20.f * Big, 0.26f, SmearDelay);
			SQFx::Shockwave(World, GroundO, R, Col, 0.22f, 16.f, HitDelay);
		}
		AreaImpacts(HitDelay);
		break;
	}

	case ESQWeaponBehaviour::Nova:
	{
		const float R = FMath::Max(60.f, Radius);
		if (Fx == TEXT("Frost"))
		{
			SQFx::Shockwave(World, GroundO, R, Col, 0.4f, 40.f * Big, ReleaseDelay);
			SQFx::Flash(World, Here, Hot, 110.f * Big, 0.14f, ReleaseDelay);
			for (int32 Shard = 0; Shard < 14; ++Shard)
			{
				const float Ang = 360.f * Shard / 14.f + FMath::FRandRange(-8.f, 8.f);
				FSQFxParticle P;
				P.Pos = GroundO + FVector(0.f, 0.f, 30.f);
				P.Vel = SQFxOnCircle(FVector::ZeroVector, R / 0.35f, Ang) + FVector(0.f, 0.f, 60.f);
				P.Drag = 1.5f;
				P.Shape = ESQFxShape::Cube;
				P.bAlignToVelocity = true;
				P.Color = Hot;
				P.Delay = ReleaseDelay;
				P.Life = 0.38f;
				P.SizeStart = FVector(46.f, 10.f, 10.f) * Big;
				P.SizeEnd = FVector(30.f, 6.f, 6.f) * Big;
				P.SpinRate = 6.f;
				SQFx::Emit(World, P);
			}
		}
		else if (Fx == TEXT("Flare"))
		{
			SQFx::Flash(World, Here + FVector(0.f, 0.f, 30.f), Hot, 190.f * Big, 0.18f, ReleaseDelay);
			SQFx::Shockwave(World, GroundO, R, Col, 0.3f, 34.f * Big, ReleaseDelay);
			SQFx::Burst(World, Here + FVector(0.f, 0.f, 40.f), Col, 14, 520.f, 0.4f, 9.f, 200.f, ReleaseDelay);
		}
		else
		{
			// Bells: a tolling double shockwave
			SQFx::Shockwave(World, GroundO, R, Col, 0.35f, 52.f * Big, ReleaseDelay);
			SQFx::Shockwave(World, GroundO + FVector(0.f, 0.f, 15.f), R * 0.8f, Hot, 0.35f, 24.f * Big, ReleaseDelay + 0.15f);
			SQFx::Flash(World, Here + FVector(0.f, 0.f, 60.f), Hot, 120.f * Big, 0.14f, ReleaseDelay);
			SQFx::Ring(World, GroundO + FVector(0.f, 0.f, 10.f), Col, 16, R / 0.4f, 0.4f, 9.f, ReleaseDelay);
		}
		AreaImpacts(ReleaseDelay + 0.1f);
		break;
	}

	case ESQWeaponBehaviour::Projectile:
	{
		const float ShotSpeed = FMath::Max(400.f, Def.Speed);
		SQFx::Flash(World, Muzzle, Hot, 55.f * Big, 0.1f, ReleaseDelay);
		SQFx::Burst(World, Muzzle, Col, 4, 260.f, 0.2f, 6.f, -300.f, ReleaseDelay);
		for (int32 ShotIdx = 1; ShotIdx < Points.Num(); ++ShotIdx)
		{
			FVector End(Points[ShotIdx].X, Points[ShotIdx].Y, Muzzle.Z);
			FVector Span = End - Muzzle;
			Span.Z = 0.f;
			const float Dist = static_cast<float>(Span.Size());
			if (Dist < 10.f) continue;
			const float Flight = FMath::Clamp(Dist / ShotSpeed, 0.05f, 3.f);
			FSQFxParticle P;
			P.Pos = Muzzle;
			P.Vel = Span / Flight;
			P.Color = Col;
			P.Delay = ReleaseDelay;
			P.Life = Flight;
			P.bShrinkOut = false;
			P.TrailEvery = 0.03f;
			P.TrailSize = 6.f * Big;
			P.TrailColor = Col;
			P.DeathFx = bCrit ? 2 : 1;
			P.DeathCount = bCrit ? 10 : 6;
			P.DeathSize = 6.f * Big;
			P.DeathColor = HitCol;
			if (Fx == TEXT("Sickle"))
			{
				P.Shape = ESQFxShape::Cube;
				P.SizeStart = P.SizeEnd = FVector(46.f, 14.f, 4.f) * Big;
				P.SpinAxis = FVector::UpVector;
				P.SpinRate = 26.f;
			}
			else if (Fx == TEXT("Nail"))
			{
				P.Shape = ESQFxShape::Cube;
				P.bAlignToVelocity = true;
				P.SizeStart = P.SizeEnd = FVector(80.f, 11.f, 11.f) * Big;
				P.TrailEvery = 0.05f;
				P.DeathCount = 10;
				P.DeathSize = 9.f * Big;
			}
			else if (Fx == TEXT("Paper"))
			{
				P.Shape = ESQFxShape::Cube;
				P.SizeStart = P.SizeEnd = FVector(26.f, 20.f, 2.f) * Big;
				P.SpinAxis = FMath::VRand();
				P.SpinRate = 14.f;
				P.TrailEvery = 0.07f;
				P.TrailSize = 4.f;
			}
			else
			{
				// Bolt: a stretched glowing streak
				P.bAlignToVelocity = true;
				P.Color = Hot;
				P.SizeStart = P.SizeEnd = FVector(70.f, 15.f, 15.f) * Big;
				P.TrailEvery = 0.02f;
			}
			SQFx::Emit(World, P);
			SQFx::Flash(World, End, FMath::Lerp(HitCol, FLinearColor::White, 0.5f), (bCrit ? 60.f : 36.f) * Big, 0.1f, Flight + ReleaseDelay);
		}
		break;
	}

	case ESQWeaponBehaviour::Beam:
	{
		const float Width = FMath::Max(30.f, Def.Speed) * 0.6f * Stats.AreaMult;
		SQFx::Flash(World, Muzzle, Hot, 80.f * Big, 0.14f, ReleaseDelay);
		for (int32 BeamIdx = 1; BeamIdx < Points.Num(); ++BeamIdx)
		{
			const FVector End(Points[BeamIdx].X, Points[BeamIdx].Y, Muzzle.Z);
			SQFx::Streak(World, Muzzle, End, Col, Width, 0.28f, ReleaseDelay);
			SQFx::Streak(World, Muzzle, End, FLinearColor::White, Width * 0.3f, 0.18f, ReleaseDelay);
			const float Len = static_cast<float>(FVector::Dist(Muzzle, End));
			const int32 Motes = FMath::Clamp(FMath::RoundToInt(Len / 140.f), 2, 14);
			for (int32 Mote = 1; Mote <= Motes; ++Mote)
			{
				SQFx::Burst(World, FMath::Lerp(Muzzle, End, static_cast<float>(Mote) / Motes), Col, 1, 120.f, 0.3f, 9.f, 150.f, ReleaseDelay);
			}
			SQFx::Impact(World, End, HitCol, Big, bCrit, ReleaseDelay);
		}
		break;
	}

	case ESQWeaponBehaviour::Chain:
	{
		// Jagged ghost-lightning: each link split into kinked segments with a white core, a zap at every target
		FVector Prev = Muzzle;
		for (int32 LinkIdx = 1; LinkIdx < Points.Num(); ++LinkIdx)
		{
			const FVector Next = GroundAt(Points[LinkIdx]) + FVector(0.f, 0.f, 80.f);
			const float LinkDelay = ReleaseDelay + 0.04f * (LinkIdx - 1);
			FVector SegA = Prev;
			constexpr int32 Kinks = 4;
			for (int32 Kink = 1; Kink <= Kinks; ++Kink)
			{
				FVector SegB = FMath::Lerp(Prev, Next, static_cast<float>(Kink) / Kinks);
				if (Kink < Kinks) SegB += FVector(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-20.f, 20.f));
				SQFx::Streak(World, SegA, SegB, Col, 16.f * Big, 0.22f, LinkDelay);
				SQFx::Streak(World, SegA, SegB, FLinearColor::White, 6.f, 0.16f, LinkDelay);
				SegA = SegB;
			}
			SQFx::Impact(World, Next, HitCol, 0.9f * Big, bCrit, LinkDelay);
			Prev = Next;
		}
		SQFx::Flash(World, Muzzle, Hot, 50.f, 0.1f, ReleaseDelay);
		break;
	}

	case ESQWeaponBehaviour::Drop:
	{
		const float R = FMath::Max(60.f, Radius);
		constexpr float FallTime = 0.6f;
		for (int32 DropIdx = 1; DropIdx < Points.Num(); ++DropIdx)
		{
			const FVector Spot(Points[DropIdx]);
			ASQFx::Spawn(World, 1, Spot, R, FLinearColor(FColor(200, 60, 60)), FallTime); // telegraph (as before)
			if (Fx == TEXT("Quake"))
			{
				// The ground erupts: spikes punch up through the floor when the drop lands
				for (int32 Spike = 0; Spike < 9; ++Spike)
				{
					FSQFxParticle P;
					P.Pos = SQFxOnCircle(Spot, FMath::FRandRange(0.f, 0.85f) * R, FMath::FRandRange(0.f, 360.f)) - FVector(0.f, 0.f, 30.f);
					P.Vel = FVector(0.f, 0.f, 520.f);
					P.Drag = 5.f;
					P.Shape = ESQFxShape::Cone;
					P.Color = FMath::Lerp(Col, FLinearColor(0.45f, 0.32f, 0.2f), 0.3f);
					P.Delay = FallTime;
					P.Life = 0.55f;
					P.SizeStart = FVector(40.f, 40.f, 30.f) * Big;
					P.SizeEnd = FVector(30.f, 30.f, 110.f) * Big;
					P.bEaseOut = true;
					SQFx::Emit(World, P);
				}
			}
			else
			{
				// Something heavy falls out of the sky and lands on the telegraph: a tombstone or a giant stamp
				const bool bStamp = Fx == TEXT("Stamp");
				const FVector Size = bStamp ? FVector(R * 1.1f, R * 1.1f, 28.f) : FVector(26.f, 70.f, 100.f) * FMath::Clamp(R / 200.f, 0.7f, 2.f) * Big;
				const float Height = 900.f;
				FSQFxParticle P;
				P.Pos = Spot + FVector(0.f, 0.f, Height);
				P.Gravity = -2.f * Height / (FallTime * FallTime);
				P.FloorZ = Spot.Z + Size.Z * 0.5f;
				P.Shape = ESQFxShape::Cube;
				P.Color = Col;
				P.Life = FallTime + 0.45f;
				P.Rot = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion();
				P.SizeStart = P.SizeEnd = Size;
				SQFx::Emit(World, P);
			}
			// Landing: shockwave, dust and a weapon-coloured burst
			SQFx::Shockwave(World, Spot, R, Col, 0.3f, 40.f * Big, FallTime);
			SQFx::Impact(World, Spot + FVector(0.f, 0.f, 40.f), Col, 1.6f * Big, false, FallTime);
		}
		break;
	}

	case ESQWeaponBehaviour::Turret:
	{
		// A turret assembles: a pillar of light, a ring and rising motes at the new turret
		const FVector Spot = Points.Num() > 0 ? FVector(Points[0]) : GroundO;
		SQFx::Streak(World, Spot, Spot + FVector(0.f, 0.f, 220.f), Col, 40.f, 0.35f);
		SQFx::Shockwave(World, Spot + FVector(0.f, 0.f, 5.f), 160.f, Col, 0.35f, 24.f);
		SQFx::Burst(World, Spot + FVector(0.f, 0.f, 30.f), Hot, 10, 220.f, 0.6f, 7.f, 400.f);
		SQFx::Flash(World, Spot + FVector(0.f, 0.f, 90.f), Hot, 80.f, 0.14f);
		break;
	}

	default:
		break;
	}
}

// ---------------------------------------------------------------- attack juice (Attack storyboard, local, every machine)

void ASQCharacter::NudgeCamera(float Amount, float Delay)
{
	if (Amount <= 0.f || !IsLocallyControlled() || !IsPlayerControlled()) return;
	if (Delay > 0.f)
	{
		CamNudgeIn = Delay;
		CamNudgeAmount = FMath::Max(CamNudgeAmount, Amount);
		return;
	}
	// A short local shake of the boom (UpdateLocalVisuals); never time dilation (co-op)
	AttackShake = FMath::Max(AttackShake, FMath::Clamp(Amount, 0.f, 0.5f));
}

void ASQCharacter::PlayReapJuice(float ImpactIn)
{
	if (GetNetMode() == NM_DedicatedServer || bDowned) return;
	const USQData* D = USQData::Get(this);
	WeaponTrailColor = FLinearColor(0.36f, 0.95f, 0.84f);
	WeaponTrailTime = FMath::Clamp(ImpactIn + 0.25f, 0.2f, 0.7f);
	bLastTipValid = false;
	WeaponGlow = 3.f;
	PendingSwingSquash = -0.18f;
	PendingHitStopSeconds = D ? D->Tune(TEXT("ReapHitStopSeconds"), 0.07f) : 0.07f;
	StartSwingJuice(SQSwingFamily::Slash, ImpactIn, false, true);
}

void ASQCharacter::StartSwingJuice(uint8 Family, float ImpactIn, bool bCrit, bool bReap)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* World = GetWorld();
	const USQData* D = USQData::Get(this);
	const float SmearSeconds = D ? D->Tune(TEXT("AttackSmearSeconds"), 0.067f) : 0.067f;
	const float FollowSeconds = D ? D->Tune(TEXT("AttackFollowSeconds"), 0.167f) : 0.167f;
	const float SettleSeconds = D ? D->Tune(TEXT("AttackSettleSeconds"), 0.2f) : 0.2f;
	const float CoilSquash = D ? D->Tune(TEXT("AttackAnticipationSquash"), 0.06f) : 0.06f;

	SwingFamily = Family <= SQSwingFamily::Throw ? Family : SQSwingFamily::Cast;
	bSwingCrit = bCrit;
	bSwingReap = bReap;
	bSwingFxDrawn = false;
	SwingSign = -SwingSign;
	SwingBeats = 0;
	SwingClock = 0.f;
	SwingImpactAt = FMath::Max(0.03f, ImpactIn);
	SwingSmearAt = FMath::Max(0.f, SwingImpactAt - SmearSeconds);
	SwingFollowAt = SwingImpactAt + PendingHitStopSeconds;
	SwingEndAt = SwingFollowAt + FollowSeconds + SettleSeconds;
	HitStopAt = SwingImpactAt;

	// 1. Anticipation: the body coils (squash), a glint runs along the raised edge
	const float Coil = CoilSquash * (SwingFamily == SQSwingFamily::Slam ? 1.4f : 1.f) * (bReap ? 1.3f : 1.f);
	SquashValue = -Coil;
	SquashVelocity = 0.f;
	WeaponGlow = FMath::Max(WeaponGlow, 1.2f);
	FVector Tip;
	if (!GetWeaponTip(Tip)) Tip = GetActorLocation() + GetActorForwardVector() * 40.f + FVector(0.f, 0.f, 60.f);
	SQFx::Glint(World, Tip, WeaponTrailColor, (bCrit || bReap) ? 80.f : 55.f, FMath::Min(0.05f, SwingSmearAt * 0.5f));
	if (SwingFamily == SQSwingFamily::Cast)
	{
		// Charge: motes converge on the tip until the release
		WeaponGlow = FMath::Max(WeaponGlow, 2.5f);
		const float Gather = FMath::Max(0.08f, SwingImpactAt);
		for (int32 Mote = 0; Mote < 6; ++Mote)
		{
			const float Ang = FMath::DegreesToRadians(60.f * Mote + FMath::FRandRange(-15.f, 15.f));
			FSQFxParticle P;
			P.Pos = Tip + FVector(FMath::Cos(Ang), FMath::Sin(Ang), FMath::FRandRange(-0.4f, 0.6f)) * 75.f;
			P.Vel = (Tip - P.Pos) / Gather;
			P.Color = FMath::Lerp(WeaponTrailColor, FLinearColor::White, 0.3f);
			P.Life = Gather;
			P.SizeStart = FVector(5.f);
			P.SizeEnd = FVector(10.f);
			P.bShrinkOut = false;
			SQFx::Emit(World, P);
		}
	}
}

void ASQCharacter::TickSwingJuice(float DeltaSeconds)
{
	if (SwingClock < 0.f) return;
	SwingClock += DeltaSeconds;
	UWorld* World = GetWorld();
	const USQData* D = USQData::Get(this);
	const FVector Here = GetActorLocation();
	FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	if (Fwd.IsNearlyZero()) Fwd = FVector::ForwardVector;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
	const float Yaw = static_cast<float>(GetActorRotation().Yaw);
	const float FloorZ = ASQArena::GroundZAt(this, static_cast<float>(Here.X), static_cast<float>(Here.Y));
	const FVector Feet(Here.X, Here.Y, FloorZ + 4.f);
	const FVector Chest(Here.X, Here.Y, FloorZ + 80.f);
	const float Reach = FMath::Clamp(HeldWeaponLength * 0.9f + 40.f, 100.f, 240.f);
	const float Big = bSwingCrit ? 1.3f : 1.f;
	const FLinearColor Col = WeaponTrailColor;
	const FLinearColor Ghost = FMath::Lerp(Col, FLinearColor::White, 0.3f);
	const FLinearColor Hot = FMath::Lerp(Col, FLinearColor::White, 0.65f);
	FVector Tip;
	if (!GetWeaponTip(Tip)) Tip = Chest + Fwd * 50.f;
	// The reap draws its own smear / impact (SQFx::ReapSwing); Arc weapons drew theirs in PlayWeaponFx
	const bool bOwnFx = !bSwingReap && !bSwingFxDrawn;

	// 2. Smear (about 2 frames before the blow): the body stretches into the swing, a ghostly trail along the arc
	if ((SwingBeats & 1) == 0 && SwingClock >= SwingSmearAt)
	{
		SwingBeats |= 1;
		static const float StretchByFamily[] = { 0.08f, 0.12f, 0.10f, 0.05f, 0.06f };
		SquashValue = StretchByFamily[SwingFamily] * (bSwingReap ? 1.4f : 1.f);
		SquashVelocity = 0.f;
		const float SmearLen = FMath::Max(0.04f, SwingImpactAt - SwingSmearAt);
		if (bOwnFx)
		{
			switch (SwingFamily)
			{
			case SQSwingFamily::Slash:
			{
				const float ArcDur = FMath::Max(0.18f, SmearLen / 0.45f);
				SQFx::SwingArc(World, Chest, Yaw + 80.f * SwingSign, -160.f * SwingSign, Reach, Ghost, Reach * 0.45f * Big, ArcDur);
				SQFx::SwingArc(World, Chest + FVector(0.f, 0.f, 8.f), Yaw + 80.f * SwingSign, -160.f * SwingSign, Reach * 0.7f, Hot, 14.f * Big, ArcDur * 0.9f);
				break;
			}
			case SQSwingFamily::Slam:
			{
				// Overhead: a vertical crescent from behind the head down to the ground in front
				const FVector Pivot = Chest + FVector(0.f, 0.f, 30.f);
				constexpr int32 Segs = 9;
				for (int32 Seg = 0; Seg < Segs; ++Seg)
				{
					const float U = static_cast<float>(Seg) / (Segs - 1);
					const float Ang = FMath::DegreesToRadians(FMath::Lerp(115.f, -35.f, U));
					const FVector Radial = Fwd * FMath::Cos(Ang) + FVector::UpVector * FMath::Sin(Ang);
					const FVector Tangent = Fwd * FMath::Sin(Ang) - FVector::UpVector * FMath::Cos(Ang);
					FSQFxParticle P;
					P.Pos = Pivot + Radial * (Reach * 0.9f);
					P.Vel = Radial * 120.f;
					P.Drag = 4.f;
					P.Color = Seg == Segs - 1 ? Hot : Ghost;
					P.Shape = ESQFxShape::Cube;
					P.Delay = SmearLen * U;
					P.Life = 0.16f;
					P.Rot = FRotationMatrix::MakeFromXZ(Tangent, Side).ToQuat();
					const float W = Reach * 0.32f * Big * (0.55f + 0.45f * U);
					P.SizeStart = FVector(Reach * 0.36f, W * 0.3f, W);
					P.SizeEnd = FVector(Reach * 0.3f, W * 0.1f, W * 0.2f);
					P.bShrinkOut = false;
					SQFx::Emit(World, P);
				}
				break;
			}
			case SQSwingFamily::Thrust:
			{
				// Lunge: three speed lines along the thrust and a hot point at the reach
				for (int32 LineIdx = -1; LineIdx <= 1; ++LineIdx)
				{
					const FVector Off = Side * (18.f * LineIdx) + FVector(0.f, 0.f, 6.f * FMath::Abs(LineIdx));
					SQFx::Streak(World, Chest - Fwd * 60.f + Off, Chest + Fwd * (Reach * 1.3f) + Off, LineIdx == 0 ? Hot : Ghost, (LineIdx == 0 ? 14.f : 8.f) * Big, 0.14f);
				}
				SQFx::Flash(World, Chest + Fwd * Reach, Hot, 40.f * Big, 0.08f, SmearLen * 0.5f);
				break;
			}
			case SQSwingFamily::Cast:
				// The charge peaks: a glow swelling on the tip until the release
				WeaponGlow = FMath::Max(WeaponGlow, 3.f);
				SQFx::Flash(World, Tip, Hot, 70.f * Big, SmearLen + 0.05f);
				break;
			default:
				// Throw: the arm whips over, a short streak at the hand
				SQFx::Streak(World, Chest - Fwd * 40.f + FVector(0.f, 0.f, 50.f), Chest + Fwd * 70.f + FVector(0.f, 0.f, 10.f), Ghost, 18.f * Big, 0.12f);
				break;
			}
		}
	}

	// 3. Impact (hit-stop, squash and the weapon pop run in UpdateWeaponVisuals' HitStopAt block; targets in PlayWeaponFx)
	if ((SwingBeats & 2) == 0 && SwingClock >= SwingImpactAt)
	{
		SwingBeats |= 2;
		float Nudge = bSwingCrit ? (D ? D->Tune(TEXT("AttackCamNudgeCrit"), 0.25f) : 0.25f) : (D ? D->Tune(TEXT("AttackCamNudge"), 0.08f) : 0.08f);
		if (SwingFamily == SQSwingFamily::Slam) Nudge *= 1.6f;
		if (bSwingReap) Nudge = D ? D->Tune(TEXT("ReapCamNudge"), 0.3f) : 0.3f;
		NudgeCamera(Nudge);
		if (bOwnFx)
		{
			switch (SwingFamily)
			{
			case SQSwingFamily::Slash:
			case SQSwingFamily::Thrust:
			{
				const FVector Contact = Chest + Fwd * (Reach * 0.85f);
				SQFx::Glint(World, Contact, FLinearColor::White, 60.f * Big);
				SQFx::Burst(World, Contact, Hot, 5, 380.f, 0.25f, 5.f * Big, -700.f);
				break;
			}
			case SQSwingFamily::Slam:
			{
				const FVector Hit = Feet + Fwd * (Reach * 0.7f);
				SQFx::GroundCracks(World, Hit, 110.f * Big, Col, 6, 0.9f, 6.f);
				SQFx::Shockwave(World, Hit, 160.f * Big, Col, 0.25f, 26.f);
				SQFx::DirtClods(World, Hit, 6, 520.f, 14.f);
				break;
			}
			case SQSwingFamily::Cast:
				SQFx::Flash(World, Tip, Hot, 110.f * Big, 0.12f);
				SQFx::Ring(World, Tip, Col, 10, 360.f, 0.25f, 6.f);
				break;
			default:
				// Throw: release recoil, a puff behind the hand
				SQFx::Burst(World, Chest - Fwd * 20.f, Ghost, 4, 200.f, 0.25f, 6.f, 0.f);
				break;
			}
		}
	}

	// 4. Follow-through: the blade overshoots past the target (the knockback itself: server, Tuning WeaponKnockback)
	if ((SwingBeats & 4) == 0 && SwingClock >= SwingFollowAt)
	{
		SwingBeats |= 4;
		if (!bSwingReap)
		{
			switch (SwingFamily)
			{
			case SQSwingFamily::Slash:
				SQFx::SwingArc(World, Chest - FVector(0.f, 0.f, 10.f), Yaw - 80.f * SwingSign, -50.f * SwingSign, Reach * 0.9f, Hot, 10.f, 0.2f);
				break;
			case SQSwingFamily::Slam:
				SQFx::Ring(World, Feet + Fwd * (Reach * 0.7f), FLinearColor(0.5f, 0.42f, 0.33f), 10, 300.f, 0.35f, 9.f);
				break;
			case SQSwingFamily::Thrust:
				SQFx::Streak(World, Chest + Fwd * (Reach * 0.9f), Chest + Fwd * (Reach * 1.5f), Ghost, 6.f, 0.12f);
				break;
			case SQSwingFamily::Cast:
				SQFx::Burst(World, Tip, Ghost, 4, 120.f, 0.5f, 6.f, 300.f);
				break;
			default:
				break;
			}
		}
	}

	// 5. Settle: the spring (UpdateSquash) eases the body back, the trail fades, the next swing may start
	if (SwingClock >= SwingEndAt) SwingClock = -1.f;
}

bool ASQCharacter::QueueAbilityAnim(uint8 Variant)
{
	// Server only. One unreliable multicast per trigger, at most one every AbilityAnimMinInterval seconds
	if (!HasAuthority() || bDowned) return false;
	const UWorld* AbilityWorld = GetWorld();
	if (!AbilityWorld) return false;
	const USQData* D = USQData::Get(this);
	const float MinGap = D ? D->Tune(TEXT("AbilityAnimMinInterval"), 1.5f) : 1.5f;
	const float AbilityNow = AbilityWorld->GetTimeSeconds();
	if (AbilityNow - LastAbilityAnimTime < MinGap) return false;
	LastAbilityAnimTime = AbilityNow;
	MulticastAbilityAnim(Variant);
	return true;
}

void ASQCharacter::MulticastAbilityAnim_Implementation(uint8 Variant)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	PlayAbilityAnim(Variant);

	// A burst in the reaper's colour, unless the passive already shows its own effect at the reaper right now
	const FSQAbilityAnimRow* Row = FindSQAbilityAnimRow(GetPassiveId());
	if (Row && Row->bOwnFx && Variant == 0) return;
	UWorld* World = GetWorld();
	const USQData* FxData = USQData::Get(this);
	const FSQReaperDef* FxDef = FxData ? FxData->FindReaper(ReaperId) : nullptr;
	const FLinearColor FxColor = FxDef ? FxDef->Color : FLinearColor::White;
	const bool bBig = Variant > 0;
	const FVector Here = GetActorLocation();
	SQFx::Ring(World, Here - FVector(0.f, 0.f, 85.f), FxColor, bBig ? 20 : 12, bBig ? 420.f : 280.f, 0.45f, bBig ? 10.f : 8.f);
	SQFx::Burst(World, Here + FVector(0.f, 0.f, 30.f), FxColor, bBig ? 12 : 7, 240.f, 0.5f, 9.f, 300.f);
	// Readable "ability moment": a flash at the chest and a ground shockwave (a pillar of light for big moments)
	SQFx::Flash(World, Here + FVector(0.f, 0.f, 20.f), FMath::Lerp(FxColor, FLinearColor::White, 0.5f), bBig ? 150.f : 90.f, 0.16f);
	SQFx::Shockwave(World, Here - FVector(0.f, 0.f, 82.f), bBig ? 380.f : 240.f, FxColor, 0.4f, bBig ? 34.f : 22.f);
	if (bBig) SQFx::Streak(World, Here - FVector(0.f, 0.f, 85.f), Here + FVector(0.f, 0.f, 260.f), FxColor, 60.f, 0.4f);
}

bool ASQCharacter::PlayAbilityAnim(uint8 Variant)
{
	if (!bHasSkin || bDowned || !Abilities) return false;
	USQAnimInstance* SkinAnim = GetSkinAnim();
	if (!SkinAnim) return false;
	const FSQAbilityAnimRow* Row = FindSQAbilityAnimRow(GetPassiveId());
	// Upper body while moving; full body for a standing moment or an in-place passive (revive, dash)
	const bool bFull = (Row && Row->bAlwaysFullBody) || static_cast<float>(GetVelocity().Size2D()) < 40.f;
	return Abilities->PlayPassiveAnim(SkinAnim, bFull, Variant);
}

void ASQCharacter::OnRep_Health()
{
	if (IsLocallyControlled() && Health < LastHealthSeen - 0.5f) HitFlash = 0.35f;
	LastHealthSeen = Health;
}

void ASQCharacter::OnRep_Reaper()
{
	ApplyVisuals();
}

void ASQCharacter::OnRep_Stats()
{
	ApplySpeed();
}

void ASQCharacter::OnRep_Downed()
{
	ApplySpeed();
	ApplyVisuals();
}

void ASQCharacter::ApplySpeed()
{
	const USQData* D = USQData::Get(this);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = bDowned ? (D ? D->Tune(TEXT("DownedSpeed"), 160.f) : 160.f) : Stats.MoveSpeed;
	}
	JumpMaxCount = 1 + FMath::Max(0, Stats.ExtraJumps); // server and owning client agree (Stats replicate)
	if (UCharacterMovementComponent* TurnMove = GetCharacterMovement())
	{
		// Top-down: a quick turn (facing snaps toward the cursor on a cast, ~0.15 s for a half turn); the anim adds a short blend on top
		const float TurnRate = bTopDownCamera ? (D ? FMath::Clamp(D->Tune(TEXT("TopDownTurnRate"), 1080.f), 180.f, 5000.f) : 1080.f) : 900.f;
		TurnMove->RotationRate = FRotator(0.f, TurnRate, 0.f);
	}
	if (USQMovementComponent* TuneMove = GetSQMovement()) TuneMove->RefreshMoveTuning(); // slide / hop / air control keys
}

void ASQCharacter::ApplyVisuals()
{
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* R = D ? D->FindReaper(ReaperId) : nullptr;
	const FLinearColor Robe = R ? R->Color : FLinearColor::White;

	if (!BodyMID) BodyMID = SQ::ColorMID(this, Robe);
	if (!HeadMID) HeadMID = SQ::ColorMID(this, FLinearColor(0.95f, 0.93f, 0.85f));
	if (BodyMID) { BodyMID->SetVectorParameterValue(TEXT("Color"), Robe); BodyMesh->SetMaterial(0, BodyMID); }
	if (HeadMID) HeadMesh->SetMaterial(0, HeadMID);

	// Imported animated skeleton when available
	FName Model = R ? R->Model : NAME_None;
	USkeletalMesh* Skel = SQArt::Character(Model);
	if (!Skel && !Model.IsNone())
	{
		// Custom model not imported yet: fall back to a KayKit skeleton on the same rig
		Model = TEXT("Mage");
		Skel = SQArt::Character(Model);
	}
	bHasSkin = Skel != nullptr;
	if (bHasSkin)
	{
		// Per-reaper size (Reapers.csv ArtScale): two reapers can share a model and still differ in silhouette
		const float Height = (D ? D->Tune(TEXT("ReaperArtHeight"), 190.f) : 190.f) * (R ? R->ArtScale : 1.f);
		const float WantScale = SQArt::FitScale(Skel, Height);
		if (SkinModel != Model || !FMath::IsNearlyEqual(SkinBaseScale, WantScale, 0.0001f))
		{
			if (SkinModel != Model) SkinMesh->SetSkeletalMeshAsset(Skel);
			SkinModel = Model;
			SkinBaseScale = WantScale;
			SkinMesh->SetRelativeScale3D(FVector(SkinBaseScale));
			SkinMesh->SetRelativeRotation(FRotator(0.f, SQArt::YawOffset(this), 0.f));
			AccessoryReaper = NAME_None; // new skeleton or size: re-attach the accessory
		}
		// Native C++ anim instance (no AnimBlueprint asset): blended locomotion, layered attacks, procedural lean
		SkinMesh->SetAnimInstanceClass(USQAnimInstance::StaticClass());
		if (USQAnimInstance* SkinAnim = GetSkinAnim()) SkinAnim->SetAnimSet(SkinModel);
		// The R_<Id> reaper bodies carry their own painted palette: keep it untinted (KayKit bodies take the robe tint)
		const bool bOwnPalette = SkinModel.ToString().StartsWith(TEXT("R_"));
		const FLinearColor SkinTint = bOwnPalette ? FLinearColor::White : SQArt::SoftTint(Robe, 0.7f);
		if (!SkinMID) SkinMID = SQArt::TintMID(this, SQArt::CharacterTexture(SkinModel), SkinTint);
		else
		{
			SkinMID->SetVectorParameterValue(TEXT("Color"), SkinTint);
			if (UTexture* SkinTex = SQArt::CharacterTexture(SkinModel)) SkinMID->SetTextureParameterValue(TEXT("Tex"), SkinTex);
		}
		SQArt::SetAllMaterials(SkinMesh, SkinMID);
		SQArt::ApplyGlowSlots(SkinMesh, this, SQArt::CharacterTexture(SkinModel)); // eyes + lantern glow on our own reaper

		// Weapon in the right hand (reuses the scythe component; a primitive stand-in when the art is missing)
		if (AccessoryReaper != ReaperId || !bHasWeaponArt) ApplyHeldWeapon(R); // (re)built with a new reaper, skeleton or size
		if (AccessoryReaper != ReaperId) ApplyAccessory(ReaperId);
		// The skull shown while a downed reaper is carried home
		if (UStaticMesh* Skull = SQArt::Prop(TEXT("skull")))
		{
			if (HeadMesh->GetStaticMesh() != Skull)
			{
				HeadMesh->SetStaticMesh(Skull);
				SQArt::SetAllMaterials(HeadMesh, SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White));
				HeadMesh->SetRelativeScale3D(FVector(SQArt::FitScale(Skull, 55.f)));
			}
			HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, -60.f));
		}
		const bool bAsSkull = bDowned && CarriedBy != nullptr;
		SkinMesh->SetVisibility(!bAsSkull);
		BodyMesh->SetVisibility(false);
		HeadMesh->SetVisibility(bAsSkull);
		return;
	}

	SkinMesh->SetVisibility(false);
	AccessoryMesh->SetVisibility(false);
	WeaponHeadMesh->SetVisibility(false);
	HeadMesh->SetVisibility(true);
	BodyMesh->SetVisibility(!bDowned);
	ScytheMesh->SetVisibility(!bDowned);
	HeadMesh->SetRelativeLocation(bDowned ? FVector(0.f, 0.f, -55.f) : FVector(0.f, 0.f, 50.f));
	HeadMesh->SetRelativeScale3D(bDowned ? FVector(0.75f) : FVector(0.6f));
}

namespace
{
	/** Reaper personality prop from Reapers.csv (Accessory, AccessorySlot Hand/Head, AccessorySize in cm). */
	bool ReaperAccessory(const FSQReaperDef* Def, FName& OutProp, bool& bOutHead, float& OutSize)
	{
		bOutHead = false;
		OutSize = 50.f;
		if (!Def || Def->Accessory.IsNone()) return false;
		OutProp = Def->Accessory;
		OutSize = Def->AccessorySize;
		bOutHead = Def->AccessorySlot == TEXT("Head");
		return true;
	}

	/** Head bone, or the left hand slot/bone, on the KayKit rig (names may have dots converted on import). */
	FName FindAccessoryBone(const USkeletalMeshComponent* Comp, bool bHead)
	{
		if (!Comp) return NAME_None;
		if (!bHead)
		{
			static const FName HandNames[] = { FName(TEXT("handslot.l")), FName(TEXT("handslot_l")), FName(TEXT("hand.l")), FName(TEXT("hand_l")) };
			for (const FName& N : HandNames)
			{
				if (Comp->DoesSocketExist(N)) return N;
			}
		}
		const int32 NumBones = Comp->GetNumBones();
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FName BoneName = Comp->GetBoneName(i);
			const FString Lower = BoneName.ToString().ToLower();
			const bool bMatch = bHead
				? Lower.Contains(TEXT("head"))
				: (Lower.Contains(TEXT("hand")) && (Lower.EndsWith(TEXT(".l")) || Lower.EndsWith(TEXT("_l")) || Lower.Contains(TEXT("left"))));
			if (bMatch) return BoneName;
		}
		return NAME_None;
	}
}

namespace
{
	/** Right hand slot/bone on the KayKit rig when SQArt::HandSocket finds no socket (names may have dots converted) */
	FName FindRightHandBone(const USkeletalMeshComponent* Comp)
	{
		if (!Comp) return NAME_None;
		const FName Socket = SQArt::HandSocket(Comp);
		if (!Socket.IsNone()) return Socket;
		const int32 NumBones = Comp->GetNumBones();
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FName BoneName = Comp->GetBoneName(i);
			const FString Lower = BoneName.ToString().ToLower();
			if (Lower.Contains(TEXT("hand")) && (Lower.EndsWith(TEXT(".r")) || Lower.EndsWith(TEXT("_r")) || Lower.Contains(TEXT("right")))) return BoneName;
		}
		return NAME_None;
	}

	/** Default held length (cm) per weapon type when Reapers.csv WeaponSize is empty */
	float SQHeldWeaponLength(FName WeaponName)
	{
		if (WeaponName == TEXT("Scythe")) return 150.f;
		if (WeaponName == TEXT("Staff")) return 135.f;
		if (WeaponName == TEXT("Axe")) return 95.f;
		if (WeaponName == TEXT("Blade")) return 85.f;
		return 110.f;
	}
}

void ASQCharacter::ApplyHeldWeapon(const FSQReaperDef* R)
{
	const FName WeaponName = R ? R->Weapon : NAME_None;
	const FName Socket = FindRightHandBone(SkinMesh);
	if (WeaponName.IsNone() || Socket.IsNone())
	{
		if (!WeaponName.IsNone() && Socket.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("SQCharacter: %s has no right-hand socket/bone on model %s: weapon hidden."), *ReaperId.ToString(), *SkinModel.ToString());
		}
		ScytheMesh->SetVisibility(false);
		WeaponHeadMesh->SetVisibility(false);
		bHasWeaponArt = false;
		return;
	}

	const float Len = (R && R->WeaponSize > 0.f) ? R->WeaponSize : SQHeldWeaponLength(WeaponName);
	HeldWeaponLength = Len;
	// Size in world cm: undo the mesh scale and the hand bone's own scale
	const FTransform BoneCS = SkinMesh->GetSocketTransform(Socket, RTS_Component);
	const float BoneScale = FMath::Max(0.0001f, static_cast<float>(BoneCS.GetScale3D().X));
	const float ParentScale = FMath::Max(0.0001f, SkinBaseScale) * BoneScale;
	const FLinearColor Robe = R ? R->Color : FLinearColor::White;

	ScytheMesh->AttachToComponent(SkinMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	WeaponHeadMesh->AttachToComponent(SkinMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	ScytheMesh->SetRelativeRotation(FRotator::ZeroRotator);
	WeaponHeadMesh->SetRelativeRotation(FRotator::ZeroRotator);

	if (UStaticMesh* WeaponMesh = SQArt::Weapon(WeaponName))
	{
		// Imported weapon: pivot at the grip, pole along +Z (README_Reaper.md), sized by its longest side
		bPrimitiveWeapon = false;
		// Weapons are UV'd to their source atlas: the scythe to the Reaper palette, KayKit weapons to the skeleton atlas.
		// The R_<Id> bodies have their own palettes, so they must not lend theirs to the weapon.
		// Monster roster weapons (W_<Id>, Nov 9 2026) are UV'd to their owner's R_<Id> palette.
		const bool bBodyOwnPalette = SkinModel.ToString().StartsWith(TEXT("R_"));
		const FString WeaponStr = WeaponName.ToString();
		const FName WeaponTexModel = WeaponStr.StartsWith(TEXT("W_")) ? FName(*(TEXT("R_") + WeaponStr.RightChop(2)))
			: !bBodyOwnPalette ? SkinModel
			: (WeaponStr.Contains(TEXT("Scythe")) ? FName(TEXT("Reaper")) : FName(TEXT("Mage")));
		const FLinearColor WeaponTint = WeaponStr.StartsWith(TEXT("W_")) ? FLinearColor::White : SQArt::SoftTint(Robe, 0.35f); // own palette: untinted
		WeaponMID = SQArt::TintMID(this, SQArt::CharacterTexture(WeaponTexModel), WeaponTint);
		ScytheMesh->SetStaticMesh(WeaponMesh);
		ScytheMesh->SetRelativeLocation(FVector::ZeroVector);
		HeldWeaponBaseScale = FVector(SQArt::FitLongest(WeaponMesh, Len) / ParentScale);
		ScytheMesh->SetRelativeScale3D(HeldWeaponBaseScale);
		SQArt::SetAllMaterials(ScytheMesh, WeaponMID);
		WeaponHeadMesh->SetVisibility(false);
	}
	else
	{
		// Primitive stand-in so the weapon is never invisible: a shaft along +Z plus a head per weapon type
		bPrimitiveWeapon = true;
		UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		const bool bBlade = WeaponName == TEXT("Blade");
		const bool bStaff = WeaponName == TEXT("Staff") || (!bBlade && WeaponName != TEXT("Scythe") && WeaponName != TEXT("Axe"));
		const FLinearColor ShaftCol = bBlade ? FLinearColor(0.75f, 0.78f, 0.85f) : FLinearColor(0.38f, 0.27f, 0.16f);
		WeaponMID = SQArt::TintMID(this, White, ShaftCol);
		UMaterialInstanceDynamic* WeaponHeadMID = SQArt::TintMID(this, White, bStaff ? SQArt::SoftTint(Robe, 0.8f) : FLinearColor(0.85f, 0.87f, 0.92f));
		if (WeaponHeadMID && bStaff) WeaponHeadMID->SetScalarParameterValue(TEXT("Glow"), 2.5f);

		ScytheMesh->SetStaticMesh(SQ::Mesh(bBlade ? TEXT("Cube") : TEXT("Cylinder")));
		const FVector ShaftCm = bBlade ? FVector(4.f, 11.f, Len * 0.8f) : FVector(6.f, 6.f, Len);
		HeldWeaponBaseScale = ShaftCm / 100.f / ParentScale;
		ScytheMesh->SetRelativeScale3D(HeldWeaponBaseScale);
		ScytheMesh->SetRelativeLocation(FVector(0.f, 0.f, (bBlade ? Len * 0.5f : Len * 0.45f)) / ParentScale);
		SQArt::SetAllMaterials(ScytheMesh, WeaponMID);

		FVector HeadCm;
		FVector HeadAt;
		const TCHAR* HeadShape = TEXT("Cube");
		if (bBlade) { HeadCm = FVector(6.f, 24.f, 5.f); HeadAt = FVector(0.f, 0.f, Len * 0.1f); }                   // crossguard
		else if (WeaponName == TEXT("Scythe")) { HeadCm = FVector(4.f, Len * 0.45f, 16.f); HeadAt = FVector(0.f, Len * 0.2f, Len * 0.92f); }
		else if (WeaponName == TEXT("Axe")) { HeadCm = FVector(6.f, 28.f, 32.f); HeadAt = FVector(0.f, 12.f, Len * 0.82f); }
		else { HeadCm = FVector(24.f); HeadAt = FVector(0.f, 0.f, Len * 0.95f); HeadShape = TEXT("Sphere"); }       // staff orb
		WeaponHeadMesh->SetStaticMesh(SQ::Mesh(HeadShape));
		HeldHeadBaseScale = HeadCm / 100.f / ParentScale;
		WeaponHeadMesh->SetRelativeScale3D(HeldHeadBaseScale);
		WeaponHeadMesh->SetRelativeLocation(HeadAt / ParentScale);
		SQArt::SetAllMaterials(WeaponHeadMesh, WeaponHeadMID);
		WeaponHeadMesh->SetVisibility(SkinMesh->IsVisible());
	}
	ScytheMesh->SetVisibility(SkinMesh->IsVisible());
	bHasWeaponArt = true;
	bLastTipValid = false;
}

bool ASQCharacter::GetWeaponTip(FVector& OutTip) const
{
	if (!bHasSkin || !bHasWeaponArt || !ScytheMesh || !ScytheMesh->IsVisible()) return false;
	const FVector Up = ScytheMesh->GetUpVector();
	// Art pivots at the grip; the primitive shaft is centred (0.45 of its length up the pole)
	const FVector Grip = bPrimitiveWeapon ? ScytheMesh->GetComponentLocation() - Up * (HeldWeaponLength * 0.45f) : ScytheMesh->GetComponentLocation();
	OutTip = Grip + Up * HeldWeaponLength * 0.95f;
	return true;
}

void ASQCharacter::UpdateWeaponVisuals(float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* World = GetWorld();

	// The blow lands: hit-stop on the attack clip, squash the body, punch the weapon
	if (HitStopAt >= 0.f)
	{
		HitStopAt -= DeltaSeconds;
		if (HitStopAt < 0.f)
		{
			HitStopAt = -1.f;
			if (USQAnimInstance* SkinAnim = GetSkinAnim()) SkinAnim->HitStop(PendingHitStopSeconds);
			SquashValue = PendingSwingSquash;
			SquashVelocity = 0.f;
			WeaponPop = 1.f;
		}
	}
	TickSwingJuice(DeltaSeconds);
	if (CamNudgeIn >= 0.f)
	{
		CamNudgeIn -= DeltaSeconds;
		if (CamNudgeIn < 0.f)
		{
			CamNudgeIn = -1.f;
			NudgeCamera(CamNudgeAmount);
			CamNudgeAmount = 0.f;
		}
	}

	// Weapon trail: a glowing ribbon following the real tip of the held weapon through the swing. From the smear beat
	// through the follow-through it widens into a ghostly smear
	if (WeaponTrailTime > 0.f)
	{
		WeaponTrailTime -= DeltaSeconds;
		FVector Tip;
		if (GetWeaponTip(Tip))
		{
			if (bLastTipValid && FVector::DistSquared(Tip, LastTipPos) > 16.f)
			{
				const bool bSmearing = SwingClock >= 0.f && SwingClock >= SwingSmearAt && SwingClock <= SwingFollowAt + 0.08f;
				const float TrailWidth = bSmearing ? (bSwingReap ? 84.f : 62.f) : 26.f;
				const FLinearColor TrailCol = bSmearing ? FMath::Lerp(WeaponTrailColor, FLinearColor::White, 0.2f) : WeaponTrailColor;
				SQFx::Streak(World, LastTipPos, Tip, TrailCol, TrailWidth, bSmearing ? 0.22f : 0.18f);
				SQFx::Streak(World, LastTipPos, Tip, FLinearColor::White, bSmearing ? 12.f : 8.f, 0.1f);
			}
			LastTipPos = Tip;
			bLastTipValid = true;
		}
		else
		{
			bLastTipValid = false;
		}
	}
	else
	{
		bLastTipValid = false;
	}

	// Punch (scale) and glow on the weapon
	WeaponPop = FMath::Max(0.f, WeaponPop - DeltaSeconds * 6.f);
	WeaponGlow = FMath::Max(0.f, WeaponGlow - DeltaSeconds * 5.f);
	if (bHasWeaponArt)
	{
		const float Pop = 1.f + 0.35f * WeaponPop;
		ScytheMesh->SetRelativeScale3D(HeldWeaponBaseScale * Pop);
		if (bPrimitiveWeapon) WeaponHeadMesh->SetRelativeScale3D(HeldHeadBaseScale * Pop);
		if (WeaponMID) WeaponMID->SetScalarParameterValue(TEXT("Glow"), WeaponGlow);
	}
	AttackShake = FMath::Max(0.f, AttackShake - DeltaSeconds);
}

void ASQCharacter::UpdatePassiveWeaponFx(float DeltaSeconds)
{
	// Always-on weapons (auras, orbits) are drawn here on every machine from replicated Owned + Stats: no network traffic
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* World = GetWorld();
	const USQData* D = USQData::Get(this);
	if (!World || !D) return;
	const ASQGameState* GS = World->GetGameState<ASQGameState>();
	const float ServerTime = GS ? GS->GetServerWorldTimeSeconds() : VisualTime;
	const FVector L = GetActorLocation();
	const FVector GroundL(L.X, L.Y, ASQArena::GroundZAt(this, static_cast<float>(L.X), static_cast<float>(L.Y)) + 8.f);
	const bool bShowAura = !bDowned && GS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss);

	AuraFxTimer -= DeltaSeconds;
	OrbTrailTimer -= DeltaSeconds;
	const bool bAuraPulse = AuraFxTimer <= 0.f;
	if (bAuraPulse) AuraFxTimer = 0.45f;
	const bool bOrbTrail = OrbTrailTimer <= 0.f;
	if (bOrbTrail) OrbTrailTimer = 0.05f;

	int32 OrbitSlot = 0;
	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* Def = D->FindUpgrade(O.Id);
		if (!Def) continue;
		const float R = SQ::UpgradeRadius(*Def, O.Level, O.Power) * Stats.AreaMult;
		const FLinearColor Col = SQFxBright(Def->Color);
		const bool bEvolved = Def->Kind == ESQUpgradeKind::Evolution;

		if (Def->Behaviour == ESQWeaponBehaviour::Aura && bShowAura && bAuraPulse)
		{
			if (Def->FxStyle == TEXT("Plague"))
			{
				for (int32 Puff = 0; Puff < (bEvolved ? 6 : 4); ++Puff)
				{
					FSQFxParticle P;
					P.Pos = SQFxOnCircle(GroundL, FMath::FRandRange(0.15f, 0.95f) * R, FMath::FRandRange(0.f, 360.f)) + FVector(0.f, 0.f, 20.f);
					P.Vel = FVector(FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-20.f, 20.f), 60.f);
					P.Color = Col;
					P.Life = 1.1f;
					P.SizeStart = FVector(30.f);
					P.SizeEnd = FVector(90.f, 90.f, 60.f);
					P.bEaseOut = true;
					SQFx::Emit(World, P);
				}
			}
			else if (Def->FxStyle == TEXT("Goo"))
			{
				for (int32 Bubble = 0; Bubble < (bEvolved ? 7 : 4); ++Bubble)
				{
					FSQFxParticle P;
					P.Pos = SQFxOnCircle(GroundL, FMath::FRandRange(0.f, 0.9f) * R, FMath::FRandRange(0.f, 360.f));
					P.Color = Col;
					P.Life = 0.5f;
					P.Delay = FMath::FRandRange(0.f, 0.3f);
					P.SizeStart = FVector(8.f);
					P.SizeEnd = FVector(36.f, 36.f, 26.f);
					P.bShrinkOut = false;
					P.DeathFx = 1;
					P.DeathCount = 3;
					P.DeathSize = 4.f;
					P.DeathColor = Col;
					SQFx::Emit(World, P);
				}
			}
			else
			{
				// Wail (default): ripples racing out to the edge of the aura
				SQFx::Shockwave(World, GroundL + FVector(0.f, 0.f, 30.f), R, Col, 0.45f, bEvolved ? 26.f : 18.f);
			}
		}
		else if (Def->Behaviour == ESQWeaponBehaviour::Orbit && OrbitSlot < 6)
		{
			// One instanced mesh per orbit weapon, shaped and coloured by its FxStyle
			const int32 Slot = OrbitSlot++;
			if (!OrbitISMs.IsValidIndex(Slot) || !OrbitISMs[Slot] || OrbitISMWeapons[Slot] != Def->Id)
			{
				if (!OrbitISMs.IsValidIndex(Slot))
				{
					UInstancedStaticMeshComponent* Made = NewObject<UInstancedStaticMeshComponent>(this);
					Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Made->SetCastShadow(false);
					Made->SetUsingAbsoluteLocation(true);
					Made->SetUsingAbsoluteRotation(true);
					Made->SetUsingAbsoluteScale(true);
					Made->SetupAttachment(RootComponent);
					Made->RegisterComponent();
					Made->SetWorldTransform(FTransform::Identity);
					OrbitISMs.Add(Made);
					OrbitISMWeapons.Add(NAME_None);
				}
				UInstancedStaticMeshComponent* NewOrbs = OrbitISMs[Slot];
				if (!NewOrbs) continue;
				OrbitISMWeapons[Slot] = Def->Id;
				const TCHAR* OrbShape = Def->FxStyle == TEXT("Thorns") ? TEXT("Cone") : (Def->FxStyle == TEXT("Crows") ? TEXT("Cube") : TEXT("Sphere"));
				NewOrbs->SetStaticMesh(SQ::Mesh(OrbShape));
				UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), nullptr, LOAD_NoWarn | LOAD_Quiet);
				UMaterialInstanceDynamic* OrbMID = SQArt::TintMID(this, White, Def->Color);
				if (OrbMID) OrbMID->SetScalarParameterValue(TEXT("Glow"), Def->FxStyle == TEXT("Crows") ? 0.4f : 2.5f);
				NewOrbs->SetMaterial(0, OrbMID);
				NewOrbs->ClearInstances();
			}
			UInstancedStaticMeshComponent* Orbs = OrbitISMs[Slot];
			if (!Orbs) continue;
			TArray<FTransform> OrbXforms;
			if (!bDowned)
			{
				const int32 Count = SQ::UpgradeCount(*Def, O.Level, O.Power);
				const float Stepped = FMath::FloorToFloat(VisualTime * 12.f) / 12.f;
				for (int32 i = 0; i < Count; ++i)
				{
					const float A = ServerTime * Def->Speed + i * 2.f * PI / Count;
					const FVector OrbPos = L + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 10.f);
					const float FacingYaw = FMath::RadiansToDegrees(A) + 90.f; // along the orbit
					FTransform Xf;
					if (Def->FxStyle == TEXT("Crows"))
					{
						// Flapping: wings (flat box) beat at stop-motion rate
						const float Flap = 0.5f + 0.5f * FMath::Abs(FMath::Sin(Stepped * 18.f + i));
						Xf = FTransform(FRotator(0.f, FacingYaw, 0.f), OrbPos + FVector(0.f, 0.f, 25.f), FVector(0.22f, 0.5f * Flap, 0.08f) * (bEvolved ? 1.3f : 1.f));
					}
					else if (Def->FxStyle == TEXT("Thorns"))
					{
						Xf = FTransform(FRotator(-90.f, FacingYaw, ServerTime * 600.f), OrbPos, FVector(0.25f, 0.25f, 0.45f) * (bEvolved ? 1.3f : 1.f));
					}
					else
					{
						Xf = FTransform(FRotator::ZeroRotator, OrbPos, FVector(0.3f) * (bEvolved ? 1.3f : 1.f));
					}
					OrbXforms.Add(Xf);
					if (bOrbTrail) SQFx::Burst(World, OrbPos + FVector(0.f, 0.f, 10.f), Col, 1, 30.f, 0.25f, 6.f, 0.f);
				}
			}
			if (Orbs->GetInstanceCount() != OrbXforms.Num())
			{
				Orbs->ClearInstances();
				if (OrbXforms.Num() > 0) Orbs->AddInstances(OrbXforms, false, true);
			}
			else if (OrbXforms.Num() > 0)
			{
				Orbs->BatchUpdateInstancesTransforms(0, OrbXforms, true, true, true);
			}
		}
	}
	// Orbit weapons that are gone (or slots beyond the current count) are emptied
	for (int32 Slot = OrbitSlot; Slot < OrbitISMs.Num(); ++Slot)
	{
		if (OrbitISMs[Slot] && OrbitISMs[Slot]->GetInstanceCount() > 0) OrbitISMs[Slot]->ClearInstances();
		if (OrbitISMWeapons.IsValidIndex(Slot)) OrbitISMWeapons[Slot] = NAME_None;
	}

	// Green numbers when this reaper is healed (regen, Triage, pickups, revives) on every machine
	const bool bFighting = GS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss);
	if (LastHealthForPops >= 0.f && Health > LastHealthForPops + 0.01f && bFighting && !bDowned) HealPopAccum += Health - LastHealthForPops;
	LastHealthForPops = Health;
	HealPopTimer -= DeltaSeconds;
	if (HealPopAccum >= 1.f && HealPopTimer <= 0.f)
	{
		SQFx::DamageNumber(World, L + FVector(0.f, 0.f, 110.f), HealPopAccum, 3);
		HealPopAccum = 0.f;
		HealPopTimer = 0.45f;
	}
}

USQAnimInstance* ASQCharacter::GetSkinAnim() const
{
	return SkinMesh ? Cast<USQAnimInstance>(SkinMesh->GetAnimInstance()) : nullptr;
}

bool ASQCharacter::PlayOneShot(FName AnimName, float Rate, bool bFullBody, float BlendIn)
{
	if (!bHasSkin) return false;
	USQAnimInstance* SkinAnim = GetSkinAnim();
	return SkinAnim && SkinAnim->PlayAction(AnimName, Rate, bFullBody, BlendIn);
}

void ASQCharacter::ApplyAccessory(FName ForReaper)
{
	AccessoryReaper = ForReaper;
	FName PropName;
	bool bHead = false;
	float Size = 50.f;
	const USQData* PropData = USQData::Get(this);
	const FSQReaperDef* PropDef = PropData ? PropData->FindReaper(ForReaper) : nullptr;
	UStaticMesh* PropMesh = ReaperAccessory(PropDef, PropName, bHead, Size) ? SQArt::Prop(PropName) : nullptr;
	const FName AttachBone = PropMesh ? FindAccessoryBone(SkinMesh, bHead) : NAME_None;
	if (!PropMesh || AttachBone.IsNone())
	{
		AccessoryMesh->SetStaticMesh(nullptr);
		AccessoryMesh->SetVisibility(false);
		return;
	}

	AccessoryMesh->SetStaticMesh(PropMesh);
	AccessoryMesh->AttachToComponent(SkinMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachBone);

	// Bone transform in mesh component space (no component scale): keep the prop upright and size it in world cm
	const FTransform BoneCS = SkinMesh->GetSocketTransform(AttachBone, RTS_Component);
	const float BoneScale = FMath::Max(0.0001f, static_cast<float>(BoneCS.GetScale3D().X));
	const float ParentScale = FMath::Max(0.0001f, SkinBaseScale) * BoneScale;

	FVector RelLocation = FVector::ZeroVector;
	if (bHead)
	{
		// Sit on top of the skull: lift from the head bone to the top of the mesh bounds (component units)
		const USkeletalMesh* SkelAsset = SkinMesh->GetSkeletalMeshAsset();
		const FBoxSphereBounds MeshBounds = SkelAsset ? SkelAsset->GetImportedBounds() : FBoxSphereBounds(ForceInit);
		const float TopZ = static_cast<float>(MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z);
		const float Lift = FMath::Max(0.f, TopZ - static_cast<float>(BoneCS.GetLocation().Z)) * 0.95f;
		RelLocation = BoneCS.InverseTransformVector(FVector(0.f, 0.f, Lift));
	}
	AccessoryMesh->SetRelativeLocation(RelLocation);
	AccessoryMesh->SetRelativeRotation(BoneCS.GetRotation().Inverse());
	AccessoryMesh->SetRelativeScale3D(FVector(SQArt::FitScale(PropMesh, Size) / ParentScale));
	SQArt::SetAllMaterials(AccessoryMesh, SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White));
	AccessoryMesh->SetVisibility(SkinMesh->IsVisible());
}

void ASQCharacter::UpdateSquash(float DeltaSeconds)
{
	if (!bHasSkin) return;

	// Landing: squash in proportion to how long we were airborne
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	const bool bFalling = Move && Move->IsFalling() && !bDowned;
	const float VisualZ = static_cast<float>(GetActorLocation().Z);
	if (bFalling)
	{
		FallTimeVisual += DeltaSeconds;
		FallPeakVisualZ = bWasFallingVisual ? FMath::Max(FallPeakVisualZ, VisualZ) : VisualZ;
	}
	else
	{
		// A big drop that ends in a slide (slide held on touchdown) plays the roll (UpdateMovementFx)
		const USQData* RollData = USQData::Get(this);
		const float RollDrop = RollData ? RollData->Tune(TEXT("SlideRollAnimDrop"), 400.f) : 400.f;
		if (bWasFallingVisual && FallPeakVisualZ - VisualZ > RollDrop) LandRollWindow = 0.2f;
		if (bWasFallingVisual && FallTimeVisual > 0.12f)
		{
			// Landing: squash by how long (and so how hard) we fell, with a little downward kick so it rings out
			const float Strength = FMath::Clamp(FallTimeVisual / 0.6f, 0.4f, 1.3f);
			SquashValue = -0.15f * Strength;
			SquashVelocity = -0.6f * Strength;
			// A real drop also plays the landing crouch (never over an ability or attack in progress)
			if (FallTimeVisual > 0.3f)
			{
				if (USQAnimInstance* LandAnim = GetSkinAnim()) LandAnim->PlayAction(FName(TEXT("SQ_Jump_Land")), 1.5f, false, 0.03f, false);
			}
		}
		FallTimeVisual = 0.f;
	}
	// Take-off: a quick stretch as the feet leave the ground on a jump
	if (bFalling && !bWasFallingVisual && Move && Move->Velocity.Z > 200.f)
	{
		SquashValue = FMath::Max(SquashValue, 0.1f);
		SquashVelocity = 0.f;
	}
	bWasFallingVisual = bFalling;

	// Damped spring back to rest (slightly stretched while airborne): one bouncy overshoot
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const float RestValue = bFalling ? 0.05f : 0.f;
	const float Stiffness = 220.f;
	const float Damping = 14.f;
	SquashVelocity += (-(SquashValue - RestValue) * Stiffness - SquashVelocity * Damping) * Dt;
	SquashValue = FMath::Clamp(SquashValue + SquashVelocity * Dt, -0.3f, 0.3f);

	const float ScaleZ = 1.f + SquashValue;
	const float ScaleXY = 1.f - SquashValue * 0.5f;
	SkinMesh->SetRelativeScale3D(FVector(SkinBaseScale * ScaleXY, SkinBaseScale * ScaleXY, SkinBaseScale * ScaleZ));
}

void ASQCharacter::UpdateSkinAnimation(float DeltaSeconds)
{
	if (!bHasSkin) return;

	const bool bAsSkull = bDowned && CarriedBy != nullptr;
	SkinMesh->SetVisibility(!bAsSkull);
	HeadMesh->SetVisibility(bAsSkull);
	ScytheMesh->SetVisibility(!bAsSkull && bHasWeaponArt);
	WeaponHeadMesh->SetVisibility(!bAsSkull && bHasWeaponArt && bPrimitiveWeapon);
	AccessoryMesh->SetVisibility(!bAsSkull && AccessoryMesh->GetStaticMesh() != nullptr);

	// The mesh component may recreate its anim instance (e.g. new skeletal mesh): make sure it has our clips
	USQAnimInstance* SkinAnim = GetSkinAnim();
	if (SkinAnim && SkinAnim->GetAnimSetModel() != SkinModel) { SkinAnim->SetAnimSet(SkinModel); StyleAppliedFor = NAME_None; }
	if (SkinAnim && StyleAppliedFor != ReaperId) ApplyAnimStyle(SkinAnim);

	// Turn rate (deg/s, + = turning right) for the sideways lean
	const float Yaw = static_cast<float>(GetActorRotation().Yaw);
	const float YawRate = DeltaSeconds > UE_KINDA_SMALL_NUMBER ? FMath::FindDeltaAngleDegrees(LastYawVisual, Yaw) / DeltaSeconds : 0.f;
	LastYawVisual = Yaw;
	TurnRateVisual = FMath::FInterpTo(TurnRateVisual, FMath::Clamp(YawRate, -1080.f, 1080.f), DeltaSeconds, 12.f);

	// Velocity that works on every machine: the movement component's (replicated for simulated proxies), with a
	// position-delta fallback so remote reapers never stand still while their capsule glides
	const FVector NowLocation = GetActorLocation();
	if (bHasLastVisualLocation && DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		FVector PosVel = (NowLocation - LastVisualLocation) / DeltaSeconds;
		if (PosVel.SizeSquared2D() > 3000.f * 3000.f) PosVel = FVector::ZeroVector; // teleport, not movement
		VisualVelocity = FMath::VInterpTo(VisualVelocity, PosVel, DeltaSeconds, 12.f);
	}
	LastVisualLocation = NowLocation;
	bHasLastVisualLocation = true;
	FVector Vel = GetVelocity();
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		const UCharacterMovementComponent* SimMove = GetCharacterMovement();
		const FVector MoveVel = SimMove ? SimMove->Velocity : FVector::ZeroVector;
		Vel = MoveVel.SizeSquared2D() > 25.f ? MoveVel : VisualVelocity;
	}
	const float Speed = static_cast<float>(Vel.Size2D());
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	uint8 Mood = 0;
	if (GS && GS->Phase == ESQPhase::Victory) Mood = 1;
	else if (GS && GS->Phase == ESQPhase::Defeat) Mood = 2;

	if (SkinAnim) SkinAnim->SetLocomotion(Speed, Vel, TurnRateVisual, bDowned, CarriedSkull != nullptr, Mood);
	if (SkinAnim) SkinAnim->SetSliding(IsSliding() && Mood == 0);
	if (SkinAnim)
	{
		const UCharacterMovementComponent* AirMove = GetCharacterMovement();
		SkinAnim->SetAirborne(AirMove && AirMove->IsFalling() && !bDowned && Mood == 0, static_cast<float>(Vel.Z));
	}
	if (SkinAnim)
	{
		// Facing snapped away from the run (a cast toward the cursor): the anim softens the turn and backpedals.
		// Owner: the aim-lock itself; remote views: facing well away from the velocity.
		float FacingSnap = AimLockLeft > 0.f ? 1.f : 0.f;
		if (bTopDownCamera && Speed > 60.f && !IsLocallyControlled())
		{
			const float FaceDot = static_cast<float>(FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), Vel.GetSafeNormal2D()));
			if (FaceDot < -0.25f) FacingSnap = 1.f;
		}
		SkinAnim->SetFacingSnap(FacingSnap);
	}
	if (SkinAnim)
	{
		// Look where the camera points (your own reaper only: remote views have no control rotation). Tuning AnimLookScale 0 = off
		float LookYaw = 0.f;
		float LookPitch = 0.f;
		const USQData* LookData = USQData::Get(this);
		const float LookScale = LookData ? FMath::Clamp(LookData->Tune(TEXT("AnimLookScale"), 1.f), 0.f, 1.5f) : 1.f;
		if (IsLocallyControlled() && !bDowned && Mood == 0 && LookScale > 0.f)
		{
			const FRotator ControlRot = GetControlRotation();
			LookYaw = static_cast<float>(FRotator::NormalizeAxis(ControlRot.Yaw - GetActorRotation().Yaw)) * LookScale;
			if (FMath::Abs(LookYaw) > 110.f) LookYaw = 0.f; // camera in front of the reaper: don't wring its neck
			LookPitch = static_cast<float>(FRotator::NormalizeAxis(ControlRot.Pitch) + 15.0) * 0.4f * LookScale;
		}
		SkinAnim->SetLook(LookYaw, LookPitch);
	}

	if (!bPlayedSpawn && SkinAnim)
	{
		bPlayedSpawn = true;
		PlayOneShot(TEXT("Spawn_Ground_Skeletons"), 1.3f, true, 0.f);
	}

	// Took damage -> flinch + flash (works on host and clients, unlike OnRep). Full body only when standing still
	if (LastVisualHealth >= 0.f && Health < LastVisualHealth - 0.5f && !bDowned)
	{
		SkinGlow = 1.5f;
		// Remaster 8: the reaper's own flinch (SQ_Hit_<Id>, built on the hand-animated hits), else the shared one
		const FName HitTake(*(FString(TEXT("SQ_Hit_")) + ReaperId.ToString()));
		if (!PlayOneShot(HitTake, 1.f, Speed < 40.f)) PlayOneShot(TEXT("Hit_A"), 1.6f, Speed < 40.f);
		// Procedural wobble on top (bigger hits ring out more); a little squash too
		if (SkinAnim) SkinAnim->HitReact(FMath::Clamp((LastVisualHealth - Health) / FMath::Max(1.f, Stats.MaxHealth) * 6.f, 0.5f, 1.5f));
		SquashValue = FMath::Min(SquashValue, -0.07f);
	}
	LastVisualHealth = Health;

	// Dash started: cooldown jumped up (server/owner) or a speed burst (everyone else sees replicated velocity)
	DashAnimLockout = FMath::Max(0.f, DashAnimLockout - DeltaSeconds);
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const float WalkMax = MoveComp ? MoveComp->MaxWalkSpeed : Stats.MoveSpeed;
	const bool bDashSpeed = Speed > FMath::Max(300.f, WalkMax) * 1.35f;
	// (a slide burst, or a jump out of a slide, is fast too: not a dash - SlideMomentumGrace, see UpdateMovementFx)
	const bool bDashStarted = DashCooldownLeft > LastDashCooldownSeen + 0.2f || (bDashSpeed && !bWasDashSpeed && SlideMomentumGrace <= 0.f && !IsSliding());
	bWasDashSpeed = bDashSpeed;
	LastDashCooldownSeen = DashCooldownLeft;
	if (bDashStarted && !bDowned && DashAnimLockout <= 0.f)
	{
		DashAnimLockout = 0.4f;
		// Monk / Tallow: the dash IS their ability (dash detection already runs on every machine: no extra RPC)
		const FName DashPassive = GetPassiveId();
		const bool bDashAbility = (DashPassive == TEXT("DashStrike") || DashPassive == TEXT("Scorch")) && PlayAbilityAnim(0);
		// The low forward dive (Remaster 7: on every machine, so squadmates see it too); the roll is the stand-in
		if (!bDashAbility && !PlayOneShot(TEXT("SQ_Dash"), 1.35f, true, 0.04f)) PlayOneShot(TEXT("Dodge_Forward"), 1.6f, true);
		SquashValue = 0.16f; // stretch, then the spring wobbles it back
		SquashVelocity = 0.f;
	}

	// Fall down / get back up / lose the run
	// Remaster 8: the downed loop sits on the ground (hand-animated), so drop into a sit instead of falling flat
	if (bDowned && !bWasDownedVisual)
	{
		if (!PlayOneShot(TEXT("Sit_Floor_Down"), 1.8f, true, 0.05f)) PlayOneShot(TEXT("Death_A"), 1.4f, true, 0.05f);
	}
	else if (!bDowned && bWasDownedVisual)
	{
		// Up off the knees into a tall stretch (the reaper's own take), else the shared stand-up
		const FName ReviveTake(*(FString(TEXT("SQ_Revive_")) + ReaperId.ToString()));
		if (!PlayOneShot(ReviveTake, 1.f, true, 0.05f)) PlayOneShot(TEXT("Lie_StandUp"), 1.5f, true, 0.f);
	}
	bWasDownedVisual = bDowned;
	if (Mood == 2 && LastMoodVisual != 2 && !bDowned) PlayOneShot(TEXT("Death_A"), 1.2f, true, 0.1f);
	LastMoodVisual = Mood;

	// Idle fidgets: stand still a few seconds and the reaper does something
	const bool bCanFidget = !bDowned && Mood == 0 && Speed < 40.f && BankProgress <= 0.f;
	IdleFidgetTime = bCanFidget ? IdleFidgetTime + DeltaSeconds : 0.f;
	if (IdleFidgetTime > NextFidgetAt && SkinAnim && !SkinAnim->IsActionPlaying())
	{
		static const TCHAR* const Fidgets[] = { TEXT("Reaper_Taunt_Clipboard"), TEXT("Taunt"), TEXT("Interact"), TEXT("Idle_B"), TEXT("Cheer"), TEXT("PickUp") };
		const int32 NumFidgets = static_cast<int32>(UE_ARRAY_COUNT(Fidgets));
		const int32 FidgetStart = FMath::RandRange(0, NumFidgets - 1);
		for (int32 FidgetTry = 0; FidgetTry < NumFidgets; ++FidgetTry)
		{
			if (PlayOneShot(Fidgets[(FidgetStart + FidgetTry) % NumFidgets], 1.f, true, 0.2f)) break;
		}
		IdleFidgetTime = 0.f;
		NextFidgetAt = FMath::FRandRange(5.f, 9.f);
	}

	// Banking at the lantern: pour the souls in
	BankAnimCooldown = FMath::Max(0.f, BankAnimCooldown - DeltaSeconds);
	if (BankProgress > 0.f && !bDowned && BankAnimCooldown <= 0.f)
	{
		BankAnimCooldown = 1.1f;
		const FName BankTake(*(FString(TEXT("SQ_Bank_")) + ReaperId.ToString()));
		if (!PlayOneShot(BankTake, 1.f, Speed < 40.f, 0.1f) && !PlayOneShot(TEXT("Reaper_Bank_Channel"), 1.f, Speed < 40.f, 0.1f))
		{
			PlayOneShot(TEXT("Interact"), 1.f, Speed < 40.f, 0.1f);
		}
	}

	// Team levelled up: everyone throws a little cheer (upper body so running isn't interrupted)
	if (GS)
	{
		if (LastTeamLevelSeen >= 0 && GS->TeamLevel > LastTeamLevelSeen && !bDowned)
		{
			PlayOneShot(TEXT("Cheer"), 1.6f, Speed < 40.f, 0.1f);
			SkinGlow = FMath::Max(SkinGlow, 1.2f);
		}
		LastTeamLevelSeen = GS->TeamLevel;
	}

	SkinGlow = FMath::Max(0.f, SkinGlow - DeltaSeconds * 6.f);
	if (SkinMID) SkinMID->SetScalarParameterValue(TEXT("Glow"), SkinGlow);

	UpdateSquash(DeltaSeconds);
}

void ASQCharacter::ApplyAnimStyle(USQAnimInstance* SkinAnim)
{
	StyleAppliedFor = ReaperId;
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* R = D ? D->FindReaper(ReaperId) : nullptr;
	if (!SkinAnim) return;
	if (R)
	{
		SkinAnim->SetStyle(R->IdleAnims, R->WalkAnims, R->RunAnims, ReaperId);
		SkinAnim->SetGait(R->Gait);
		SkinAnim->SetGroundScale(R->ArtScale);
	}
	else
	{
		SkinAnim->SetStyle(TArray<FName>(), TArray<FName>(), TArray<FName>(), ReaperId);
		SkinAnim->SetGait(FSQGaitProfile());
		SkinAnim->SetGroundScale(1.f);
	}
}

void ASQCharacter::UpdateLocalVisuals(float DeltaSeconds)
{
	VisualTime += DeltaSeconds;

	// Hit shake on the camera
	HitFlash = FMath::Max(0.f, HitFlash - DeltaSeconds);
	FVector Shake = HitFlash > 0.f ? FVector(0.f, FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * HitFlash * 60.f : FVector::ZeroVector;
	if (AttackShake > 0.f) Shake += FVector(0.f, FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * AttackShake * 30.f; // crit kick
	Shake *= SQSettings::ShakeScale(USQSaveGame::Load()); // SETTINGS > CAMERA > Screen shake (0.5 = the original strength)
	Boom->SocketOffset = (bTopDownCamera ? FVector::ZeroVector : FVector(0.f, 0.f, 120.f)) + Shake;
	UpdateTopDownCamera(DeltaSeconds);
	UpdateCameraOcclusion(DeltaSeconds);

	const USQData* D = USQData::Get(this);
	if (!D) return;

	// Clay squash while moving (stop-motion feel: stepped at ~12 fps)
	const float Stepped = FMath::FloorToFloat(VisualTime * 12.f) / 12.f;
	const float Speed = GetVelocity().Size2D();
	const float Squash = bDowned ? 0.f : FMath::Sin(Stepped * 14.f) * 0.06f * FMath::Clamp(Speed / 600.f, 0.f, 1.f);
	BodyMesh->SetRelativeScale3D(FVector(0.65f * (1.f + Squash), 0.65f * (1.f + Squash), 1.05f * (1.f - Squash)));
	UpdateSkinAnimation(DeltaSeconds);
	UpdateMovementFx(DeltaSeconds);
	UpdateWeaponVisuals(DeltaSeconds);
	UpdatePassiveWeaponFx(DeltaSeconds);
	// Co-op pick bubble: a soul-gold shimmer around the reaper while it is protected (so nobody mistakes it for god mode)
	if (bPickBubble && !bDowned)
	{
		PickShimmerTimer -= DeltaSeconds;
		if (PickShimmerTimer <= 0.f)
		{
			PickShimmerTimer = 0.12f;
			const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, 85.f);
			SQFx::Ring(GetWorld(), Feet + FVector(0.f, 0.f, FMath::FRandRange(10.f, 150.f)), FLinearColor(1.f, 0.85f, 0.45f), 10, 140.f, 0.25f, 5.f);
			SQFx::Burst(GetWorld(), Feet + FVector(0.f, 0.f, 90.f), FLinearColor(1.f, 0.92f, 0.65f), 2, 120.f, 0.4f, 5.f, 250.f);
		}
		SkinGlow = FMath::Max(SkinGlow, 0.6f + 0.4f * FMath::Sin(VisualTime * 18.f));
	}
	else
	{
		PickShimmerTimer = 0.f;
	}
	if (bDowned && !bHasSkin)
	{
		HeadMesh->SetRelativeRotation(FRotator(Stepped * 220.f, 0.f, 0.f));
	}

	// Aura disc from owned weapons, tinted like the biggest aura (orbits + aura effects: UpdatePassiveWeaponFx)
	float AuraRadius = 0.f;
	FLinearColor AuraColour = FLinearColor(0.55f, 0.3f, 0.8f);
	for (const FSQOwned& O : Owned)
	{
		const FSQUpgradeDef* Def = D->FindUpgrade(O.Id);
		if (!Def || Def->Behaviour != ESQWeaponBehaviour::Aura) continue;
		const float R = SQ::UpgradeRadius(*Def, O.Level, O.Power) * Stats.AreaMult;
		if (R > AuraRadius)
		{
			AuraRadius = R;
			AuraColour = Def->Color;
		}
	}

	AuraMesh->SetVisibility(AuraRadius > 0.f && !bDowned);
	if (AuraRadius > 0.f)
	{
		const float Pulse = 1.f + 0.04f * FMath::Sin(Stepped * 10.f);
		AuraMesh->SetRelativeScale3D(FVector(AuraRadius / 50.f * Pulse, AuraRadius / 50.f * Pulse, 0.01f));
		if (UMaterialInstanceDynamic* AuraMID = Cast<UMaterialInstanceDynamic>(AuraMesh->GetMaterial(0))) AuraMID->SetVectorParameterValue(TEXT("Color"), AuraColour);
	}
	if (OrbISM->GetInstanceCount() > 0) OrbISM->ClearInstances(); // superseded by the per-weapon orbit meshes

	// Souls stacked on your back: the greed meter everyone can see
	const int32 StackCount = FMath::Min(12, FMath::CeilToInt(Carried / 5.f));
	if (SoulStackISM->GetInstanceCount() != StackCount)
	{
		SoulStackISM->ClearInstances();
		TArray<FTransform> Stack;
		for (int32 i = 0; i < StackCount; ++i)
		{
			Stack.Add(FTransform(FRotator::ZeroRotator, FVector(-45.f, (i % 2) * 14.f - 7.f, 10.f + i * 16.f), FVector(0.22f)));
		}
		if (Stack.Num() > 0) SoulStackISM->AddInstances(Stack, false, false);
	}

	// Bone turrets
	TArray<FTransform> TurretXforms;
	for (const FVector_NetQuantize& T : Turrets)
	{
		TurretXforms.Add(FTransform(FRotator::ZeroRotator, FVector(T.X, T.Y, T.Z + 45.f), FVector(0.5f, 0.5f, 0.9f)));
	}
	if (TurretISM->GetInstanceCount() != TurretXforms.Num())
	{
		TurretISM->ClearInstances();
		if (TurretXforms.Num() > 0) TurretISM->AddInstances(TurretXforms, false, true);
	}
	else if (TurretXforms.Num() > 0)
	{
		// Same count but the oldest turret may have been replaced by a new one elsewhere
		TurretISM->BatchUpdateInstancesTransforms(0, TurretXforms, true, true, true);
	}
}

// ---------------------------------------------------------------- reaper passive (kits pass: USQAbilityComponent, Kits.csv)

FName ASQCharacter::GetPassiveId() const
{
	return Abilities ? Abilities->GetPassiveId() : FName(NAME_None);
}

float ASQCharacter::GetPassiveValue() const
{
	return Abilities ? Abilities->GetPassiveValue() : 0.f;
}

float ASQCharacter::GetSoulMagnet() const
{
	const float NormalRange = Stats.Magnet * 0.6f;
	return Abilities ? Abilities->GetSoulMagnet(NormalRange) : NormalRange; // Femur the Hound (Fetch)
}

void ASQCharacter::OnPassiveKill(const FVector2D& Where, bool bElite)
{
	if (HasAuthority() && !bDowned && Abilities) Abilities->OnOwnerKill(Where, bElite);
}

float ASQCharacter::IncomingDamageMult() const
{
	return Abilities ? Abilities->GetIncomingDamageMult() : 1.f;
}

void ASQCharacter::TryCastAbility(uint8 Slot, const FVector& AimPoint)
{
	if (Abilities) Abilities->TryCast(Slot, AimPoint);
}

// ---------------------------------------------------------------- top-down camera, facing, aim reticle, occlusion

namespace SQTopDownCam
{
	/** The chase camera's starting tilt (deg below the horizon) */
	static float StartTilt(const USQSaveGame* Save)
	{
		return FMath::Clamp(Save ? Save->CameraTilt : SQSettings::TiltDefault, SQSettings::TiltMin, SQSettings::TiltMax);
	}

	/** SETTINGS zoom (boom cm), clamped to the wheel range */
	static float Zoom(const USQSaveGame* Save)
	{
		return FMath::Clamp(Save ? Save->CameraZoom : SQSettings::ZoomDefault, SQSettings::ZoomMin, SQSettings::ZoomMax);
	}

	/** Puts a faded occluder back: un-hides a whole component, or restores an ISM instance's transform */
	static void RestoreOccluder(UStaticMeshComponent* Comp, int32 Instance, const FTransform& Original)
	{
		if (!Comp) return;
		if (Instance == INDEX_NONE)
		{
			Comp->SetHiddenInGame(false);
		}
		else if (UInstancedStaticMeshComponent* RestoreISM = Cast<UInstancedStaticMeshComponent>(Comp))
		{
			if (RestoreISM->IsValidInstance(Instance)) RestoreISM->UpdateInstanceTransform(Instance, Original, true, true, true);
		}
	}
}

bool ASQCharacter::IsTopDownMode(const UObject* WorldContext)
{
	const USQData* ModeData = WorldContext ? USQData::Get(WorldContext) : nullptr;
	return !ModeData || ModeData->Tune(TEXT("TopDownMode"), 1.f) >= 0.5f;
}

float ASQCharacter::GetCameraYaw(const UObject* WorldContext)
{
	const USQData* YawData = WorldContext ? USQData::Get(WorldContext) : nullptr;
	return YawData ? YawData->Tune(TEXT("CameraYaw"), 0.f) : 0.f;
}

void ASQCharacter::ApplyCameraMode()
{
	bTopDownCamera = IsTopDownMode(this);
	if (!bTopDownCamera || !Boom || !Camera) return; // TopDownMode 0: the constructor's 3rd-person boom stays

	const USQData* CamData = USQData::Get(this);
	const USQSaveGame* CamSave = USQSaveGame::Load();
	// Chase camera (Nov 12 2026): behind and above the reaper. The boom ignores the control rotation (that is the reaper's
	// wanted facing) and the reaper's turning; UpdateTopDownCamera sets its rotation from the mouse / right-stick orbit.
	Boom->bUsePawnControlRotation = false;
	Boom->bInheritPitch = false;
	Boom->bInheritYaw = false;
	Boom->bInheritRoll = false;
	Boom->SetUsingAbsoluteRotation(true);
	CameraOrbitYaw = static_cast<float>(GetActorRotation().Yaw);
	CameraTiltNow = SQTopDownCam::StartTilt(CamSave);
	Boom->SetWorldRotation(FRotator(-CameraTiltNow, CameraOrbitYaw, 0.f));
	// Arena meshes ignore the camera channel (occluders hide instead, UpdateCameraOcclusion) and UpdateTopDownCamera keeps
	// the camera above the terrain itself, so the arm needs no collision sweep. The arm's location lag is the smooth follow.
	Boom->bDoCollisionTest = false;
	Boom->bEnableCameraLag = true;
	Boom->bEnableCameraRotationLag = false;
	Boom->CameraLagSpeed = SQSettings::FollowLagSpeed(CamSave);
	Boom->SetTickableWhenPaused(true); // solo SETTINGS pauses the game: the camera still shows changes live
	Boom->CameraLagMaxDistance = CamData ? FMath::Max(0.f, CamData->Tune(TEXT("ChaseLagMaxDistance"), 220.f)) : 220.f;
	Boom->TargetOffset = FVector(0.f, 0.f, CamData ? CamData->Tune(TEXT("ChaseHeight"), 90.f) : 90.f);
	Boom->SocketOffset = FVector::ZeroVector;

	CameraZoomTarget = SQTopDownCam::Zoom(CamSave);
	CameraZoomNow = CameraZoomTarget;
	CameraArmNow = CameraZoomNow;
	Boom->TargetArmLength = CameraArmNow;

	CameraFovNow = CamData ? FMath::Clamp(CamData->Tune(TEXT("ChaseFov"), 90.f), 60.f, 110.f) : 90.f;
	Camera->SetFieldOfView(CameraFovNow);
}

void ASQCharacter::AddCameraZoom(float Steps)
{
	if (!bTopDownCamera) return;
	const USQData* ZoomData = USQData::Get(this);
	USQSaveGame* ZoomSave = USQSaveGame::Load();
	const float StepCm = ZoomData ? FMath::Max(0.f, ZoomData->Tune(TEXT("ChaseZoomStep"), 80.f)) : 80.f;
	CameraZoomTarget = FMath::Clamp(CameraZoomTarget - Steps * StepCm, SQSettings::ZoomMin, SQSettings::ZoomMax);
	if (ZoomSave)
	{
		// the wheel is the SETTINGS zoom too (the slider follows it); written to disk a moment after the last notch
		ZoomSave->CameraZoom = CameraZoomTarget;
		const UWorld* ZoomWorld = GetWorld();
		CameraZoomSaveAt = ZoomWorld ? static_cast<float>(ZoomWorld->GetRealTimeSeconds()) : 0.f;
	}
}

bool ASQCharacter::IsManualAim()
{
	const USQSaveGame* AimSave = USQSaveGame::Load();
	return AimSave && AimSave->AimMode == 1;
}

float ASQCharacter::GetViewYaw() const
{
	if (!bTopDownCamera || !Boom) return GetCameraYaw(this);
	return static_cast<float>(Boom->GetComponentRotation().Yaw);
}

void ASQCharacter::AddCameraOrbit(float YawDeg, float PitchDeg)
{
	if (!bTopDownCamera) return;
	CameraOrbitYaw = FRotator::NormalizeAxis(CameraOrbitYaw + YawDeg);
	CameraTiltNow = FMath::Clamp(CameraTiltNow + PitchDeg, SQSettings::TiltMin, SQSettings::TiltMax);
}

void ASQCharacter::ResetCamera()
{
	if (!bTopDownCamera) return;
	USQSaveGame* ResetSave = USQSaveGame::Load();
	// behind the reaper: the boom points the way the reaper faces
	CameraOrbitYaw = static_cast<float>(GetActorRotation().Yaw);
	CameraTiltNow = SQSettings::TiltDefault;
	CameraZoomTarget = SQSettings::ZoomDefault;
	if (ResetSave)
	{
		ResetSave->CameraZoom = CameraZoomTarget;
		USQSaveGame::Store(ResetSave);
	}
}

void ASQCharacter::StartAimLock(float Seconds)
{
	AimLockLeft = FMath::Max(AimLockLeft, FMath::Clamp(Seconds, 0.f, 2.f));
}

void ASQCharacter::UpdateFacingMode()
{
	UCharacterMovementComponent* FaceMove = GetCharacterMovement();
	if (!FaceMove) return;
	// Human reapers in top-down turn toward the control rotation, which the controller sets to the move direction (or the
	// cursor while aim-locked). The control rotation travels with every saved move, so the server and the owning client turn
	// alike and remote views get the replicated rotation. Bots (AI control rotation follows the pawn) and TopDownMode 0
	// keep the stock orient-to-movement. Simulated proxies have no controller and don't simulate rotation.
	const bool bFaceControl = bTopDownCamera && Controller && Controller->IsPlayerController();
	if ((FaceMove->bUseControllerDesiredRotation != 0) != bFaceControl)
	{
		FaceMove->bUseControllerDesiredRotation = bFaceControl;
		FaceMove->bOrientRotationToMovement = !bFaceControl;
	}
}

void ASQCharacter::UpdateTopDownCamera(float DeltaSeconds)
{
	if (!bTopDownCamera || !Boom || !IsLocallyControlled()) return;
	const ASQPlayerController* CamPC = Cast<ASQPlayerController>(Controller);
	if (!CamPC) return; // bots are "locally controlled" on the host too

	const USQData* CamData = USQData::Get(this);
	USQSaveGame* CamSave = USQSaveGame::Load();
	const UWorld* CamWorld = GetWorld();
	const float RealNow = CamWorld ? static_cast<float>(CamWorld->GetRealTimeSeconds()) : 0.f;
	// real (undilated) frame time: the camera keeps moving while the level-up pause slows the world
	const float CamDelta = CamWorld && CamWorld->GetWorldSettings() ? FMath::Clamp(DeltaSeconds / FMath::Max(0.0001f, CamWorld->GetWorldSettings()->GetEffectiveTimeDilation()), 0.f, 0.1f) : DeltaSeconds;

	// ---- distance: the SETTINGS camera distance is the target (the wheel writes it); write the profile a second after the last notch
	CameraZoomTarget = SQTopDownCam::Zoom(CamSave);
	CameraZoomNow = FMath::FInterpTo(CameraZoomNow, CameraZoomTarget, CamDelta, 10.f);
	if (CameraZoomSaveAt >= 0.f && RealNow - CameraZoomSaveAt > 1.f)
	{
		CameraZoomSaveAt = -1.f;
		if (CamSave) USQSaveGame::Store(CamSave);
	}

	// ---- rotation: the orbit (mouse / right stick through AddCameraOrbit). Set directly: mouse look must not lag.
	const FRotator WantRot(-CameraTiltNow, CameraOrbitYaw, 0.f);
	Boom->SetWorldRotation(WantRot);

	// ---- follow smoothing (spring arm lag)
	Boom->CameraLagSpeed = SQSettings::FollowLagSpeed(CamSave);

	// ---- pivot: a little above the reaper. Downed with a teammate still up: drift over to them (spectate), back on revive.
	const float PivotUp = CamData ? CamData->Tune(TEXT("ChaseHeight"), 90.f) : 90.f;
	FVector WantOffset(0.f, 0.f, PivotUp);
	if (bDowned && (!CamData || CamData->Tune(TEXT("ChaseSpectate"), 1.f) >= 0.5f))
	{
		DownedCamSeconds += CamDelta;
		const ASQCharacter* Watch = nullptr;
		double WatchSq = TNumericLimits<double>::Max();
		for (TActorIterator<ASQCharacter> It(CamWorld); It; ++It)
		{
			const ASQCharacter* Mate = *It;
			if (!Mate || Mate == this || Mate->bDowned) continue;
			const double MateSq = FVector::DistSquared(Mate->GetActorLocation(), GetActorLocation());
			if (MateSq < WatchSq) { WatchSq = MateSq; Watch = Mate; }
		}
		if (Watch && DownedCamSeconds > 1.5f) WantOffset += Watch->GetActorLocation() - GetActorLocation();
	}
	else
	{
		DownedCamSeconds = 0.f;
	}
	CameraLookAheadNow = FMath::VInterpTo(CameraLookAheadNow, WantOffset, CamDelta, bDowned ? 2.5f : 12.f);
	Boom->TargetOffset = CameraLookAheadNow;

	// ---- keep the camera out of the ground: shorten the arm where the terrain (banks, bowls, ramps) rises behind the reaper.
	// Arena meshes ignore the camera channel, so this reads the height field: pull in fast, ease back out.
	float WantArm = CameraZoomNow;
	const ASQGameState* CamGS = CamWorld ? CamWorld->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* CamArena = CamGS ? CamGS->Arena.Get() : nullptr;
	if (CamArena && CamArena->HasTerrain())
	{
		const float Clearance = CamData ? FMath::Max(10.f, CamData->Tune(TEXT("ChaseGroundClearance"), 55.f)) : 55.f;
		const FVector Pivot = GetActorLocation() + CameraLookAheadNow;
		const FVector Back = -WantRot.Vector();
		constexpr int32 Probes = 8;
		for (int32 ProbeIdx = Probes; ProbeIdx >= 1; --ProbeIdx)
		{
			const float Dist = CameraZoomNow * static_cast<float>(ProbeIdx) / static_cast<float>(Probes);
			const FVector At = Pivot + Back * Dist;
			const float Ground = CamArena->GroundZ(static_cast<float>(At.X), static_cast<float>(At.Y));
			if (At.Z < Ground + Clearance)
			{
				// this far out the arm is under the ground: stop just short of the previous (clear) probe
				WantArm = FMath::Min(WantArm, CameraZoomNow * static_cast<float>(ProbeIdx - 1) / static_cast<float>(Probes));
			}
		}
		WantArm = FMath::Max(WantArm, FMath::Min(CameraZoomNow, 160.f));
	}
	CameraArmNow = FMath::FInterpTo(CameraArmNow, WantArm, CamDelta, WantArm < CameraArmNow ? 18.f : 4.f);
	Boom->TargetArmLength = CameraArmNow;
}

void ASQCharacter::RebuildOccluderCache(const ASQArena* ForArena)
{
	OccluderCache.Reset();
	OccluderCacheArena = ForArena;
	OccluderCacheSerial = ForArena ? ForArena->GetBuildSerial() : -1;
	if (!ForArena) return;

	const USQData* CacheData = USQData::Get(this);
	const float MinHeight = CacheData ? CacheData->Tune(TEXT("OcclusionMinHeight"), 170.f) : 170.f;
	const int32 MaxColliderInstances = CacheData ? FMath::RoundToInt(CacheData->Tune(TEXT("OcclusionMaxColliderInstances"), 6.f)) : 6;
	auto IsTall = [ForArena, MinHeight](const FBox& TestBox)
	{
		constexpr double MaxSpan = 3000.0; // huge pieces (safety floor, backdrop bands, moon) never fade
		if (!TestBox.IsValid) return false;
		const FVector BoxSize = TestBox.GetSize();
		if (BoxSize.X > MaxSpan || BoxSize.Y > MaxSpan) return false;
		const FVector BoxMid = TestBox.GetCenter();
		return TestBox.Max.Z - ForArena->GroundZ(static_cast<float>(BoxMid.X), static_cast<float>(BoxMid.Y)) >= MinHeight;
	};

	TInlineComponentArray<UStaticMeshComponent*> ArenaMeshes(ForArena);
	for (UStaticMeshComponent* EachMesh : ArenaMeshes)
	{
		if (!EachMesh || !EachMesh->IsRegistered() || !EachMesh->GetStaticMesh()) continue;
		if (UInstancedStaticMeshComponent* EachISM = Cast<UInstancedStaticMeshComponent>(EachMesh))
		{
			const int32 NumInst = EachISM->GetInstanceCount();
			if (NumInst <= 0) continue;
			// Colliding ISMs (props, park pieces): shrinking an instance would move its collision too, so only small ones
			// fade, and as a whole component. Non-colliding ISMs (walls, lamps, rocks, trees) fade per instance.
			const bool bColliding = EachISM->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
			if (bColliding && NumInst > MaxColliderInstances) continue;
			const FBox MeshBox = EachISM->GetStaticMesh()->GetBoundingBox();
			for (int32 InstIdx = 0; InstIdx < NumInst; ++InstIdx)
			{
				FTransform InstXf;
				if (!EachISM->GetInstanceTransform(InstIdx, InstXf, true)) continue;
				const FBox InstBox = MeshBox.TransformBy(InstXf);
				if (!IsTall(InstBox)) continue;
				FSQOccluder InstOcc;
				InstOcc.Comp = EachISM;
				InstOcc.Instance = bColliding ? INDEX_NONE : InstIdx;
				InstOcc.Box = InstBox;
				OccluderCache.Add(InstOcc);
			}
			continue;
		}
		const FBox CompBox = EachMesh->Bounds.GetBox();
		if (!IsTall(CompBox)) continue;
		FSQOccluder CompOcc;
		CompOcc.Comp = EachMesh;
		CompOcc.Box = CompBox;
		OccluderCache.Add(CompOcc);
	}
}

void ASQCharacter::RestoreAllOccluders()
{
	for (const FSQFadedOccluder& Fade : FadedOccluders)
	{
		SQTopDownCam::RestoreOccluder(Fade.Comp.Get(), Fade.Instance, Fade.Original);
	}
	FadedOccluders.Reset();
}

void ASQCharacter::UpdateCameraOcclusion(float DeltaSeconds)
{
	const USQData* OccData = USQData::Get(this);
	const bool bLocalHuman = bTopDownCamera && IsLocallyControlled() && Cast<ASQPlayerController>(Controller) != nullptr;
	if (!bLocalHuman || !Camera || (OccData && OccData->Tune(TEXT("CameraOcclusion"), 1.f) < 0.5f))
	{
		if (FadedOccluders.Num() > 0) RestoreAllOccluders();
		return;
	}
	// ~20 Hz is plenty (the hold below hides the gaps)
	OcclusionTimer -= DeltaSeconds;
	if (OcclusionTimer > 0.f) return;
	const float StepSeconds = 0.05f;
	OcclusionTimer = StepSeconds;

	const UWorld* OccWorld = GetWorld();
	const ASQGameState* OccGS = OccWorld ? OccWorld->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* OccArena = OccGS ? OccGS->Arena.Get() : nullptr;
	if (!OccArena)
	{
		RestoreAllOccluders();
		return;
	}
	if (OccArena != OccluderCacheArena.Get() || OccArena->GetBuildSerial() != OccluderCacheSerial)
	{
		RestoreAllOccluders();
		RebuildOccluderCache(OccArena);
	}

	const float Pad = OccData ? FMath::Max(0.f, OccData->Tune(TEXT("OcclusionPadding"), 50.f)) : 50.f;
	const float HoldSeconds = OccData ? FMath::Max(StepSeconds, OccData->Tune(TEXT("OcclusionHoldSeconds"), 0.35f)) : 0.35f;
	const FVector CamLoc = Camera->GetComponentLocation();

	// Chase camera: only what sits between the camera and this reaper (head and knees) is hidden. With the horizon in view a
	// line to every nearby teammate would strip half the scenery.
	TArray<FVector, TInlineAllocator<4>> Targets;
	{
		const FVector SeenLoc = GetActorLocation() + (bDowned ? CameraLookAheadNow : FVector::ZeroVector);
		Targets.Add(SeenLoc + FVector(0.f, 0.f, 60.f));
		Targets.Add(SeenLoc - FVector(0.f, 0.f, 50.f));
	}

	for (FSQFadedOccluder& Fade : FadedOccluders) Fade.HoldLeft -= StepSeconds;

	for (const FSQOccluder& Occ : OccluderCache)
	{
		UStaticMeshComponent* OccComp = Occ.Comp.Get();
		if (!OccComp) continue;
		const FBox Padded = Occ.Box.ExpandBy(static_cast<double>(Pad));
		bool bBlocks = false;
		for (const FVector& Target : Targets)
		{
			FBox LineBox(ForceInit);
			LineBox += CamLoc;
			LineBox += Target;
			if (!LineBox.Intersect(Padded)) continue;
			if (FMath::LineBoxIntersection(Padded, CamLoc, Target, Target - CamLoc))
			{
				bBlocks = true;
				break;
			}
		}
		if (!bBlocks) continue;

		const int32 OccInstance = Occ.Instance;
		FSQFadedOccluder* Already = FadedOccluders.FindByPredicate([OccComp, OccInstance](const FSQFadedOccluder& Each)
		{
			return Each.Comp.Get() == OccComp && Each.Instance == OccInstance;
		});
		if (Already)
		{
			Already->HoldLeft = HoldSeconds;
			continue;
		}
		if (!OccComp->IsVisible()) continue; // hidden on purpose by its owner (colliders, art swaps): leave it alone

		FSQFadedOccluder NewFade;
		NewFade.Comp = OccComp;
		NewFade.Instance = OccInstance;
		NewFade.HoldLeft = HoldSeconds;
		if (OccInstance == INDEX_NONE)
		{
			OccComp->SetHiddenInGame(true); // local only: arena meshes are built on every machine, never replicated
		}
		else
		{
			UInstancedStaticMeshComponent* OccISM = Cast<UInstancedStaticMeshComponent>(OccComp);
			if (!OccISM || !OccISM->IsValidInstance(OccInstance)) continue;
			if (!OccISM->GetInstanceTransform(OccInstance, NewFade.Original, true)) continue;
			FTransform Shrunk = NewFade.Original;
			Shrunk.SetScale3D(NewFade.Original.GetScale3D() * 0.001);
			OccISM->UpdateInstanceTransform(OccInstance, Shrunk, true, true, true);
		}
		FadedOccluders.Add(NewFade);
	}

	// Put back whatever has been out of the way for the hold time (no flicker at the edges)
	for (int32 FadeIdx = FadedOccluders.Num() - 1; FadeIdx >= 0; --FadeIdx)
	{
		const FSQFadedOccluder& Fade = FadedOccluders[FadeIdx];
		if (Fade.HoldLeft > 0.f && Fade.Comp.IsValid()) continue;
		SQTopDownCam::RestoreOccluder(Fade.Comp.Get(), Fade.Instance, Fade.Original);
		FadedOccluders.RemoveAtSwap(FadeIdx);
	}
}
