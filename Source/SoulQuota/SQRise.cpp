// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQRise.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SQAnimInstance.h"
#include "SQArena.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQFx.h"
#include "SQFxTheme.h"

namespace
{
	/** Beat times of the storyboard at 1.6 s (scaled by RiseSeconds / 1.6) */
	constexpr float SQRiseBeatTimes[] = { 0.f, 0.3f, 0.6f, 0.9f, 1.2f, 1.6f };
	constexpr int32 SQRiseNumBeats = UE_ARRAY_COUNT(SQRiseBeatTimes);
	const FLinearColor SQRiseSoul(0.36f, 0.95f, 0.84f);
	const FLinearColor SQRiseDirt(0.33f, 0.24f, 0.16f);
	const FLinearColor SQRiseBone(0.91f, 0.89f, 0.82f);

	/**
	 * Reapers.csv RiseFlourish cell of one reaper ("Fx:Anim1|Anim2"), read straight from the CSV (USQData doesn't load
	 * this column). Read at every rise, so an edited CSV applies on the next run.
	 */
	FString SQRiseReadFlourish(FName ReaperId)
	{
		FString Text;
		if (ReaperId.IsNone() || !FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data") / TEXT("Reapers.csv")))) return FString();

		int32 IdCol = INDEX_NONE;
		int32 FlourishCol = INDEX_NONE;
		bool bHeader = true;
		TArray<FString> Row;
		FString Cell;
		bool bQuoted = false;
		auto EndRow = [&]() -> bool
		{
			Row.Add(Cell.TrimStartAndEnd());
			Cell.Reset();
			bool bFound = false;
			if (Row.Num() > 0 && !Row[0].IsEmpty() && !Row[0].StartsWith(TEXT("#")))
			{
				if (bHeader)
				{
					bHeader = false;
					for (int32 Col = 0; Col < Row.Num(); ++Col)
					{
						if (IdCol == INDEX_NONE && Row[Col].Equals(TEXT("Id"), ESearchCase::IgnoreCase)) IdCol = Col;
						if (FlourishCol == INDEX_NONE && Row[Col].Equals(TEXT("RiseFlourish"), ESearchCase::IgnoreCase)) FlourishCol = Col;
					}
				}
				else if (Row.IsValidIndex(IdCol) && Row.IsValidIndex(FlourishCol) && FName(*Row[IdCol]) == ReaperId)
				{
					Cell = Row[FlourishCol];
					bFound = true;
				}
			}
			Row.Reset();
			return bFound;
		};
		for (int32 Idx = 0; Idx < Text.Len(); ++Idx)
		{
			const TCHAR C = Text[Idx];
			if (bQuoted)
			{
				if (C == TEXT('"'))
				{
					if (Idx + 1 < Text.Len() && Text[Idx + 1] == TEXT('"')) { Cell.AppendChar(TEXT('"')); ++Idx; }
					else { bQuoted = false; }
				}
				else { Cell.AppendChar(C); }
			}
			else if (C == TEXT('"')) { bQuoted = true; }
			else if (C == TEXT(',')) { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); }
			else if (C == TEXT('\n')) { if (EndRow()) return Cell; }
			else if (C != TEXT('\r')) { Cell.AppendChar(C); }
		}
		if ((!Cell.IsEmpty() || Row.Num() > 0) && EndRow()) return Cell;
		return FString();
	}

	FVector SQRiseOnCircle(const FVector& Center, float Radius, float Deg)
	{
		const float Rad = FMath::DegreesToRadians(Deg);
		return Center + FVector(FMath::Cos(Rad) * Radius, FMath::Sin(Rad) * Radius, 0.f);
	}

	/** Flat spinning bits tossed up (coins, paper slips, bone shards) */
	void SQRiseToss(UWorld* World, const FVector& From, float FloorZ, const FLinearColor& Color, ESQFxShape Shape, const FVector& Size, int32 Count, float Up, float Out, float Gravity, float Drag)
	{
		for (int32 Bit = 0; Bit < Count; ++Bit)
		{
			const float Ang = FMath::FRandRange(0.f, 360.f);
			FSQFxParticle P;
			P.Pos = From;
			P.Vel = SQRiseOnCircle(FVector::ZeroVector, Out * FMath::FRandRange(0.4f, 1.f), Ang) + FVector(0.f, 0.f, Up * FMath::FRandRange(0.75f, 1.2f));
			P.Gravity = Gravity;
			P.Drag = Drag;
			P.FloorZ = FloorZ;
			P.Shape = Shape;
			P.Color = Color;
			P.Life = FMath::FRandRange(0.7f, 1.f);
			P.SizeStart = P.SizeEnd = Size;
			P.Rot = FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f).Quaternion();
			P.SpinAxis = FMath::VRand();
			P.SpinRate = FMath::FRandRange(8.f, 16.f);
			SQFx::Emit(World, P);
		}
	}

	/** Motes floating up from Center (lantern light, healing, souls) */
	void SQRiseMotes(UWorld* World, const FVector& Center, const FLinearColor& Color, int32 Count, float Radius, float Rise, float Size)
	{
		for (int32 Mote = 0; Mote < Count; ++Mote)
		{
			FSQFxParticle P;
			P.Pos = SQRiseOnCircle(Center, FMath::FRandRange(0.f, Radius), FMath::FRandRange(0.f, 360.f));
			P.Vel = FVector(0.f, 0.f, Rise * FMath::FRandRange(0.6f, 1.2f));
			P.Drag = 1.f;
			P.Color = Color;
			P.Delay = 0.03f * Mote;
			P.Life = FMath::FRandRange(0.6f, 0.9f);
			P.SizeStart = FVector(Size);
			P.SizeEnd = FVector(Size * 0.5f);
			SQFx::Emit(World, P);
		}
	}
}

ASQRiseFx::ASQRiseFx()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true; // one per reaper for ~3 s: everyone sees the whole crew rise
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(2.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	auto MakePart = [this](const TCHAR* PartName, const TCHAR* Shape, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(SQ::Mesh(Shape));
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->SetVisibility(false);
		return Part;
	};
	Mound = MakePart(TEXT("Mound"), TEXT("Sphere"), Root);

	Hand = CreateDefaultSubobject<USceneComponent>(TEXT("Hand"));
	Hand->SetupAttachment(Root);
	// Engine basic shapes are 100 cm: a 10 cm thick, 50 cm forearm with three 16 cm fingers splayed at the top
	HandArm = MakePart(TEXT("HandArm"), TEXT("Cylinder"), Hand);
	HandArm->SetRelativeLocation(FVector(0.f, 0.f, 25.f));
	HandArm->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.5f));
	HandFingerA = MakePart(TEXT("HandFingerA"), TEXT("Cylinder"), Hand);
	HandFingerB = MakePart(TEXT("HandFingerB"), TEXT("Cylinder"), Hand);
	HandFingerC = MakePart(TEXT("HandFingerC"), TEXT("Cylinder"), Hand);
	const float FingerYaw[] = { -35.f, 0.f, 35.f };
	UStaticMeshComponent* Fingers[] = { HandFingerA, HandFingerB, HandFingerC };
	for (int32 FingerIdx = 0; FingerIdx < 3; ++FingerIdx)
	{
		Fingers[FingerIdx]->SetRelativeLocation(FVector(0.f, FingerYaw[FingerIdx] * 0.12f, 56.f));
		Fingers[FingerIdx]->SetRelativeRotation(FRotator(0.f, 0.f, FingerYaw[FingerIdx]));
		Fingers[FingerIdx]->SetRelativeScale3D(FVector(0.035f, 0.035f, 0.16f));
	}

	NameTag = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameTag"));
	NameTag->SetupAttachment(Root);
	NameTag->SetHorizontalAlignment(EHTA_Center);
	NameTag->SetVerticalAlignment(EVRTA_TextCenter);
	NameTag->SetWorldSize(40.f);
	NameTag->SetCastShadow(false);
	NameTag->SetVisibility(false);
}

void ASQRiseFx::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQRiseFx, Reaper);
	DOREPLIFETIME(ASQRiseFx, StartServerTime);
	DOREPLIFETIME(ASQRiseFx, Duration);
}

float ASQRiseFx::PlayRise(ASQCharacter* InReaper)
{
	if (!InReaper) return 0.f;
	UWorld* World = InReaper->GetWorld();
	if (!World) return 0.f;
	const USQData* D = USQData::Get(InReaper);
	const float Seconds = FMath::Clamp(D ? D->Tune(TEXT("RiseSeconds"), 1.6f) : 1.6f, 0.4f, 6.f);

	// One rise per reaper: a restart replaces the old one (its EndPlay puts the mesh back)
	for (TActorIterator<ASQRiseFx> It(World); It; ++It)
	{
		if (It->Reaper == InReaper && !It->IsActorBeingDestroyed() && It->HasAuthority()) It->Destroy();
	}

	const FVector At = InReaper->GetActorLocation();
	const float FloorZ = ASQArena::GroundZAt(InReaper, static_cast<float>(At.X), static_cast<float>(At.Y));
	const FTransform Xform(FRotator(0.f, InReaper->GetActorRotation().Yaw, 0.f), FVector(At.X, At.Y, FloorZ));
	ASQRiseFx* Rise = World->SpawnActorDeferred<ASQRiseFx>(ASQRiseFx::StaticClass(), Xform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Rise) return Seconds;
	Rise->Reaper = InReaper;
	Rise->Duration = Seconds;
	const AGameStateBase* GS = World->GetGameState();
	Rise->StartServerTime = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : static_cast<float>(World->GetTimeSeconds());
	Rise->FinishSpawning(Xform);
	return Seconds;
}

void ASQRiseFx::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) SetLifeSpan(Duration + 1.6f);
	if (GetNetMode() == NM_DedicatedServer) SetActorTickEnabled(false); // nothing to draw
}

void ASQRiseFx::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreSkin();
	if (USQAnimInstance* Anim = GetReaperAnim())
	{
		if (Clock < Duration && Anim->IsActionLocked()) Anim->ClearActionLock(); // cut short: let the reaper act again
	}
	Super::EndPlay(EndPlayReason);
}

USQAnimInstance* ASQRiseFx::GetReaperAnim() const
{
	return (Reaper && Reaper->SkinMesh) ? Cast<USQAnimInstance>(Reaper->SkinMesh->GetAnimInstance()) : nullptr;
}

FVector ASQRiseFx::FeetSpot() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 3.f);
}

void ASQRiseFx::SetSkinDrop(float DownCm)
{
	if (!Reaper || !Reaper->SkinMesh) return;
	Reaper->SkinMesh->SetRelativeLocation(SkinBase - FVector(0.f, 0.f, FMath::Max(0.f, DownCm)));
	bSkinDropped = DownCm > 0.f;
}

void ASQRiseFx::RestoreSkin()
{
	if (!bSkinDropped) return;
	bSkinDropped = false;
	if (Reaper && Reaper->SkinMesh) Reaper->SkinMesh->SetRelativeLocation(SkinBase);
}

void ASQRiseFx::BeginSequence(float Elapsed)
{
	bBegun = true;
	const USQData* D = USQData::Get(this);
	Scale = FMath::Max(0.25f, Duration / 1.6f);
	Depth = D ? D->Tune(TEXT("RiseDepth"), 200.f) : 200.f;
	CrackRadius = D ? D->Tune(TEXT("RiseCrackRadius"), 170.f) : 170.f;
	const float LateSkip = D ? D->Tune(TEXT("RiseLateJoinSkip"), 0.35f) : 0.35f;
	const ASQCharacter* ReaperCDO = Reaper->GetClass()->GetDefaultObject<ASQCharacter>();
	SkinBase = (ReaperCDO && ReaperCDO->SkinMesh) ? ReaperCDO->SkinMesh->GetRelativeLocation() : FVector(0.f, 0.f, -90.f);

	if (Elapsed > LateSkip)
	{
		// Late joiner / long hitch: straight to the end state (reaper standing, cracks healed, nothing left to play)
		bSkipped = true;
		Clock = Duration + 10.f;
		NextBeat = SQRiseNumBeats;
		RestoreSkin();
		Mound->SetVisibility(false);
		Hand->SetVisibility(false, true);
		NameTag->SetVisibility(false);
		return;
	}
	Clock = FMath::Max(0.f, Elapsed);

	const FSQReaperDef* Def = D ? D->FindReaper(Reaper->ReaperId) : nullptr;
	ReaperColor = Def ? Def->Color : SQRiseSoul;

	// The rise clip: the dedicated take rises out of the ground by itself; the stand-ins play from the burst
	RiseClip = NAME_None;
	RiseClipLength = 0.f;
	if (USQAnimInstance* Anim = GetReaperAnim())
	{
		static const TCHAR* const RiseTakes[] = { TEXT("SQ_Rise_FromGrave"), TEXT("Skeletons_Awaken_Floor"), TEXT("Spawn_Ground_Skeletons"), TEXT("Lie_StandUp") };
		for (const TCHAR* Take : RiseTakes)
		{
			TArray<FName> One;
			One.Add(FName(Take));
			if (const UAnimSequence* Seq = Anim->FindFirstAnim(One))
			{
				RiseClip = FName(Take);
				RiseClipLength = static_cast<float>(Seq->GetPlayLength());
				break;
			}
		}
		// Nothing else may cut the sequence (the old spawn clip, flinches, fidgets, attacks)
		Anim->LockActions(FMath::Max(0.f, Duration - Clock));
	}
	const bool bDedicatedClip = RiseClip == FName(TEXT("SQ_Rise_FromGrave"));
	bClipFull = bDedicatedClip && (D ? D->Tune(TEXT("RiseClipFull"), 1.f) : 1.f) > 0.f;
	const bool bHasSkin = Reaper->SkinMesh && Reaper->SkinMesh->GetSkeletalMeshAsset() != nullptr;
	if (!bClipFull && bHasSkin) SetSkinDrop(Depth); // under the floor until the burst

	// The flourish (Reapers.csv RiseFlourish "Fx:Anim1|Anim2"); SQ_Flourish_<Id> is always tried first
	FlourishFx = NAME_None;
	FlourishAnims.Reset();
	FlourishAnims.Add(FName(*FString::Printf(TEXT("SQ_Flourish_%s"), *Reaper->ReaperId.ToString())));
	const FString Cell = SQRiseReadFlourish(Reaper->ReaperId);
	FString FxPart = Cell;
	FString AnimPart;
	if (Cell.Split(TEXT(":"), &FxPart, &AnimPart))
	{
		TArray<FString> Names;
		AnimPart.ParseIntoArray(Names, TEXT("|"), true);
		for (const FString& AnimName : Names)
		{
			const FString Clean = AnimName.TrimStartAndEnd();
			if (!Clean.IsEmpty()) FlourishAnims.Add(FName(*Clean));
		}
	}
	FxPart.TrimStartAndEndInline();
	if (!FxPart.IsEmpty()) FlourishFx = FName(*FxPart);
	FlourishAnims.Add(FName(TEXT("Cheer"))); // the plain hero pose

	// Name tag text: the reaper's name in capitals, in its colour
	const FString TagText = Def ? Def->Name.ToUpper() : Reaper->ReaperId.ToString().ToUpper();
	NameTag->SetText(FText::FromString(TagText));
	NameTag->SetTextRenderColor(FMath::Lerp(ReaperColor, FLinearColor::White, 0.35f).ToFColor(true));
}

void ASQRiseFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBegun)
	{
		// The reaper reference can arrive after the actor on a client: start as soon as it resolves
		if (!Reaper) return;
		const UWorld* World = GetWorld();
		const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
		const float Now = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : (World ? static_cast<float>(World->GetTimeSeconds()) : StartServerTime);
		BeginSequence(HasAuthority() ? 0.f : Now - StartServerTime);
	}
	if (bSkipped || !Reaper) return;

	Clock += DeltaSeconds;
	while (NextBeat < SQRiseNumBeats && Clock >= SQRiseBeatTimes[NextBeat] * Scale)
	{
		PlayBeat(NextBeat++);
	}
	UpdateShapes(DeltaSeconds);
}

void ASQRiseFx::PlayBeat(int32 Beat)
{
	UWorld* World = GetWorld();
	const FVector Feet = FeetSpot();
	const FLinearColor Hot = FMath::Lerp(SQRiseSoul, FLinearColor::White, 0.5f);
	USQAnimInstance* Anim = GetReaperAnim();

	switch (Beat)
	{
	case 0:
	{
		// Crack: a ring of glowing cracks races out over the dirt, a low thump
		SQFx::GroundCracks(World, Feet, CrackRadius, SQRiseSoul, 9, 1.6f * Scale + 0.45f, 8.f);
		SQFx::GroundCracks(World, Feet, CrackRadius * 0.55f, Hot, 5, 1.6f * Scale + 0.35f, 5.f, 0.05f);
		SQFx::Shockwave(World, Feet, CrackRadius * 1.1f, SQRiseSoul, 0.35f, 18.f);
		SQFx::Flash(World, Feet, SQRiseSoul, 70.f, 0.15f);
		Reaper->NudgeCamera(0.15f);
		if (Anim && bClipFull && RiseClipLength > 0.f)
		{
			// (ends a hair before the hero beat so the flourish can layer on top)
			Anim->PlayAction(RiseClip, FMath::Clamp(RiseClipLength / (Duration * 0.96f), 0.4f, 3.f), true, 0.f, true);
		}
		Mound->SetMaterial(0, SQ::ColorMID(this, SQRiseDirt));
		Mound->SetRelativeScale3D(FVector(1.3f, 1.3f, 0.06f));
		Mound->SetVisibility(true);
		break;
	}
	case 1:
		// Swell: the mound starts to bulge (UpdateShapes), light leaks from the seams
		SQFx::Burst(World, Feet + FVector(0.f, 0.f, 10.f), SQRiseSoul, 6, 160.f, 0.5f, 6.f, 300.f);
		break;
	case 2:
	{
		// The hand: punches up through the soil (the dedicated clip has its own), clods scatter
		SQFx::DirtClods(World, Feet, 8, 480.f, 12.f);
		SQFx::Flash(World, Feet + FVector(0.f, 0.f, 20.f), Hot, 50.f, 0.1f);
		Reaper->NudgeCamera(0.06f);
		if (!bClipFull)
		{
			UMaterialInstanceDynamic* BoneMID = SQ::ColorMID(this, SQRiseBone);
			HandArm->SetMaterial(0, BoneMID);
			HandFingerA->SetMaterial(0, BoneMID);
			HandFingerB->SetMaterial(0, BoneMID);
			HandFingerC->SetMaterial(0, BoneMID);
			Hand->SetRelativeLocation(FVector(25.f, 10.f, -70.f));
			Hand->SetVisibility(true, true);
		}
		break;
	}
	case 3:
	{
		// Burst: the earth blows open, a pillar of soul light, the reaper is dragged up
		SQFx::DirtClods(World, Feet, 16, 650.f, 15.f);
		SQFx::Shockwave(World, Feet, 220.f, SQRiseSoul, 0.3f, 34.f);
		SQFx::Flash(World, Feet + FVector(0.f, 0.f, 40.f), Hot, 170.f, 0.16f);
		constexpr int32 Beams = 12;
		for (int32 BeamIdx = 0; BeamIdx < Beams; ++BeamIdx)
		{
			// A cage of thin vertical beams (an opaque cylinder would hide the reaper climbing out of it)
			const FVector Base = SQRiseOnCircle(Feet, 55.f, 360.f * BeamIdx / Beams);
			SQFx::Streak(World, Base, Base + FVector(0.f, 0.f, FMath::FRandRange(420.f, 640.f)), BeamIdx % 3 == 0 ? Hot : SQRiseSoul, 12.f, 0.65f * Scale);
		}
		SQFx::Streak(World, Feet, Feet + FVector(0.f, 0.f, 260.f), FLinearColor::White, 26.f, 0.25f);
		SQFx::Burst(World, Feet + FVector(0.f, 0.f, 30.f), SQRiseSoul, 14, 260.f, 0.9f, 8.f, 650.f);
		Reaper->NudgeCamera(0.2f);
		Hand->SetVisibility(false, true);
		if (Anim && !bClipFull && RiseClipLength > 0.f)
		{
			Anim->PlayAction(RiseClip, FMath::Clamp(RiseClipLength / (0.66f * Scale), 0.6f, 3.f), true, 0.f, true);
		}
		break;
	}
	case 4:
	{
		// Climb out: dirt shaken off the body, a puff at the feet
		const FVector Body = Reaper->GetActorLocation();
		SQFx::Burst(World, Body + FVector(0.f, 0.f, 20.f), SQRiseDirt, 10, 260.f, 0.55f, 7.f, -1100.f);
		SQFx::DirtClods(World, Feet, 5, 260.f, 9.f);
		SQFx::Ring(World, Feet + FVector(0.f, 0.f, 4.f), FLinearColor(0.5f, 0.42f, 0.33f), 12, 260.f, 0.35f, 8.f);
		break;
	}
	default:
	{
		// Ready: hero pose / flourish, the name tag pops, the cracks heal (their particles close by themselves)
		SQFx::Ring(World, Feet + FVector(0.f, 0.f, 6.f), SQRiseSoul, 16, 380.f, 0.4f, 8.f);
		SQFx::Flash(World, Reaper->GetActorLocation(), Hot, 90.f, 0.14f);
		RestoreSkin();
		PlayFlourish();
		TagStart = Clock;
		NameTag->SetVisibility(true);
		break;
	}
	}
}

void ASQRiseFx::UpdateShapes(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const float T = Clock / Scale; // storyboard seconds
	const FVector Feet = FeetSpot();

	// Mound: flat -> bulging (0.3..0.9) -> burst into a low rim -> sinks away
	if (Mound->IsVisible() || T < 0.9f)
	{
		FVector MoundScale(1.3f, 1.3f, 0.06f);
		if (T >= 0.3f && T < 0.9f)
		{
			const float U = (T - 0.3f) / 0.6f;
			const float Wobble = FMath::Sin(Clock * 42.f) * 0.03f * U;
			MoundScale = FVector(1.3f + 0.25f * U + Wobble, 1.3f + 0.25f * U - Wobble, 0.06f + 0.42f * FMath::Pow(U, 1.5f));
		}
		else if (T >= 0.9f)
		{
			const float Sink = FMath::Clamp((T - 1.6f) / 0.5f, 0.f, 1.f);
			MoundScale = FVector(1.9f, 1.9f, 0.05f) * (1.f - Sink);
			if (Sink >= 1.f) Mound->SetVisibility(false);
		}
		Mound->SetRelativeScale3D(MoundScale.ComponentMax(FVector(0.001f)));
	}

	// Soul light leaking from the seams while it swells
	if (T >= 0.3f && T < 0.9f)
	{
		LeakTimer -= DeltaSeconds;
		if (LeakTimer <= 0.f)
		{
			LeakTimer = 0.06f;
			const FVector Seam = SQRiseOnCircle(Feet, FMath::FRandRange(20.f, 60.f), FMath::FRandRange(0.f, 360.f));
			SQFx::Streak(World, Seam, Seam + FVector(FMath::FRandRange(-15.f, 15.f), FMath::FRandRange(-15.f, 15.f), FMath::FRandRange(60.f, 120.f)), SQRiseSoul, 8.f, 0.2f);
		}
	}

	// The hand: punches up fast, fingers flex, gone at the burst
	if (Hand->IsVisible() && T >= 0.6f && T < 0.9f)
	{
		const float U = FMath::Clamp((T - 0.6f) / 0.08f, 0.f, 1.f);
		const float Up = 1.f - FMath::Square(1.f - U);
		Hand->SetRelativeLocation(FVector(25.f, 10.f, FMath::Lerp(-70.f, 10.f, Up)));
		const float Flex = 20.f * FMath::Sin(Clock * 30.f);
		HandFingerA->SetRelativeRotation(FRotator(Flex, 0.f, -35.f));
		HandFingerB->SetRelativeRotation(FRotator(-Flex, 0.f, 0.f));
		HandFingerC->SetRelativeRotation(FRotator(Flex, 0.f, 35.f));
	}

	// Skinned mesh under the floor until the burst, then dragged up (stand-in clips only)
	if (bSkinDropped)
	{
		if (T < 0.9f) SetSkinDrop(Depth);
		else
		{
			const float U = FMath::Clamp((T - 0.9f) / 0.35f, 0.f, 1.f);
			const float Out = 1.f - FMath::Square(1.f - U);
			if (U >= 1.f) RestoreSkin();
			else SetSkinDrop(Depth * (1.f - Out));
		}
	}

	// Name tag: pops (0 -> 1.3 -> 1), holds, shrinks away; always faces the local camera, above the reaper's head
	if (TagStart >= 0.f && NameTag->IsVisible())
	{
		const float Age = Clock - TagStart;
		float TagScale = 1.f;
		if (Age < 0.12f) TagScale = 1.3f * (Age / 0.12f);
		else if (Age < 0.25f) TagScale = FMath::Lerp(1.3f, 1.f, (Age - 0.12f) / 0.13f);
		else if (Age > 1.35f) TagScale = FMath::Max(0.f, 1.f - (Age - 1.35f) / 0.25f);
		if (Age > 1.6f)
		{
			NameTag->SetVisibility(false);
			TagStart = -1.f;
		}
		const FVector TagAt = Reaper->GetActorLocation() + FVector(0.f, 0.f, 150.f);
		NameTag->SetWorldLocation(TagAt);
		NameTag->SetWorldScale3D(FVector(FMath::Max(0.001f, TagScale)));
		if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			NameTag->SetWorldRotation((Cam->GetCameraLocation() - TagAt).Rotation());
		}
	}
}

void ASQRiseFx::PlayFlourish()
{
	UWorld* World = GetWorld();
	const FVector Feet = FeetSpot();
	const FVector Body = Reaper->GetActorLocation();
	const FVector Head = Body + FVector(0.f, 0.f, 70.f);
	FVector Fwd = Reaper->GetActorForwardVector().GetSafeNormal2D();
	if (Fwd.IsNearlyZero()) Fwd = FVector::ForwardVector;
	const FVector HandSpot = (Reaper->AccessoryMesh && Reaper->AccessoryMesh->IsVisible()) ? Reaper->AccessoryMesh->GetComponentLocation() : Body + Fwd * 30.f + FVector(0.f, 0.f, 20.f);
	const FVector WeaponTop = (Reaper->ScytheMesh && Reaper->ScytheMesh->IsVisible()) ? Reaper->ScytheMesh->GetComponentLocation() + Reaper->ScytheMesh->GetUpVector() * 100.f : Head + Fwd * 30.f;
	const FLinearColor Col = FMath::Lerp(ReaperColor, FLinearColor::White, 0.25f);
	const FLinearColor Gold(1.f, 0.8f, 0.35f);
	const FLinearColor Flame(1.f, 0.55f, 0.15f);

	// The flourish clip (upper body, so the player can start moving at once; full body while standing still)
	if (USQAnimInstance* Anim = GetReaperAnim())
	{
		bool bPlayed = false;
		for (const FName& Try : FlourishAnims)
		{
			if (Anim->PlayAction(Try, 1.2f, false, 0.1f, true)) { bPlayed = true; break; }
		}
		// The rise clip still owns the body for a frame: play the first available flourish full body instead
		for (int32 TryIdx = 0; !bPlayed && TryIdx < FlourishAnims.Num(); ++TryIdx)
		{
			bPlayed = Anim->PlayAction(FlourishAnims[TryIdx], 1.2f, true, 0.1f, true);
		}
	}

	// Monster roster (Nov 10 2026): every monster's hero beat uses its own effect theme (the old RiseFlourish styles stay as the fallback)
	if (SQTheme::Has(Reaper->ReaperId))
	{
		SQTheme::PlayFlourish(World, Reaper->ReaperId, Feet, Fwd);
		return;
	}

	const FName Fx = FlourishFx;
	if (Fx == TEXT("Flame"))
	{
		// Lights the wick: a flame bursts up from the candle
		SQFx::Flash(World, HandSpot, Flame, 90.f, 0.16f);
		SQFx::Flash(World, HandSpot, FLinearColor::White, 35.f, 0.08f);
		SQFx::Burst(World, HandSpot, Flame, 10, 220.f, 0.5f, 8.f, 520.f);
		SQFx::Burst(World, HandSpot, Gold, 6, 140.f, 0.6f, 5.f, 380.f, 0.08f);
	}
	else if (Fx == TEXT("Stumble"))
	{
		// Trips over the lanyard: a dust puff ahead and paperwork everywhere
		SQFx::Burst(World, Feet + Fwd * 50.f + FVector(0.f, 0.f, 10.f), FLinearColor(0.6f, 0.55f, 0.5f), 10, 200.f, 0.45f, 12.f, 100.f);
		SQRiseToss(World, Body + FVector(0.f, 0.f, 20.f), Feet.Z, FLinearColor(0.95f, 0.95f, 0.9f), ESQFxShape::Cube, FVector(18.f, 13.f, 1.5f), 6, 380.f, 220.f, -420.f, 2.5f);
	}
	else if (Fx == TEXT("Watch"))
	{
		// Checks the watch: twelve gold ticks around him one after another, then a chime
		for (int32 TickIdx = 0; TickIdx < 12; ++TickIdx)
		{
			SQFx::Flash(World, SQRiseOnCircle(Body, 85.f, 90.f - 30.f * TickIdx), Gold, 18.f, 0.25f, 0.03f * TickIdx);
		}
		SQFx::Glint(World, HandSpot, Gold, 70.f, 0.38f);
		SQFx::Shockwave(World, Feet, 140.f, Gold, 0.3f, 16.f, 0.38f);
	}
	else if (Fx == TEXT("Lantern"))
	{
		SQFx::Flash(World, HandSpot, Gold, 120.f, 0.2f);
		SQRiseMotes(World, HandSpot, Gold, 10, 40.f, 250.f, 7.f);
	}
	else if (Fx == TEXT("Wail"))
	{
		SQFx::Shockwave(World, Feet + FVector(0.f, 0.f, 30.f), 260.f, Col, 0.4f, 24.f);
		SQFx::Shockwave(World, Feet + FVector(0.f, 0.f, 50.f), 200.f, Col, 0.4f, 16.f, 0.15f);
		SQRiseMotes(World, Head, Col, 8, 50.f, 200.f, 7.f);
	}
	else if (Fx == TEXT("Bones"))
	{
		SQRiseToss(World, Body, Feet.Z, SQRiseBone, ESQFxShape::Cube, FVector(16.f, 6.f, 6.f), 10, 520.f, 260.f, -1500.f, 0.f);
		SQFx::Shockwave(World, Feet, 160.f, SQRiseBone, 0.25f, 20.f);
	}
	else if (Fx == TEXT("Coins"))
	{
		SQRiseToss(World, HandSpot, Feet.Z, Gold, ESQFxShape::Cylinder, FVector(14.f, 14.f, 3.f), 10, 560.f, 200.f, -1500.f, 0.f);
		SQFx::Glint(World, HandSpot, Gold, 60.f);
	}
	else if (Fx == TEXT("Ledger"))
	{
		SQRiseToss(World, HandSpot, Feet.Z, FLinearColor(0.95f, 0.93f, 0.85f), ESQFxShape::Cube, FVector(16.f, 12.f, 1.5f), 5, 300.f, 160.f, -380.f, 2.5f);
		SQFx::Glint(World, HandSpot, FLinearColor::White, 50.f, 0.2f);
	}
	else if (Fx == TEXT("Heal"))
	{
		const FLinearColor Green(0.45f, 1.f, 0.55f);
		SQRiseMotes(World, Feet + FVector(0.f, 0.f, 10.f), Green, 12, 70.f, 220.f, 7.f);
		SQFx::Shockwave(World, Feet, 150.f, Green, 0.35f, 16.f);
		SQFx::Streak(World, Head + FVector(0.f, -18.f, 40.f), Head + FVector(0.f, 18.f, 40.f), Green, 10.f, 0.45f);
		SQFx::Streak(World, Head + FVector(0.f, 0.f, 22.f), Head + FVector(0.f, 0.f, 58.f), Green, 10.f, 0.45f);
	}
	else if (Fx == TEXT("Halo"))
	{
		SQFx::Shockwave(World, Head + FVector(0.f, 0.f, 45.f), 45.f, Gold, 0.6f, 8.f);
		SQFx::Glint(World, Head + FVector(0.f, 0.f, 45.f), Gold, 80.f);
		SQRiseMotes(World, Head, Gold, 8, 40.f, 160.f, 6.f);
	}
	else if (Fx == TEXT("Thud"))
	{
		SQFx::Shockwave(World, Feet, 240.f, FLinearColor(0.55f, 0.45f, 0.35f), 0.3f, 36.f);
		SQFx::DirtClods(World, Feet, 10, 420.f, 12.f);
		Reaper->NudgeCamera(0.12f);
	}
	else if (Fx == TEXT("Ecto"))
	{
		const FLinearColor Ecto(0.45f, 1.f, 0.7f);
		SQFx::Flash(World, Head, Ecto, 90.f, 0.14f);
		SQFx::Burst(World, Head, Ecto, 12, 380.f, 0.6f, 11.f, -900.f);
	}
	else if (Fx == TEXT("Pop"))
	{
		// The pumpkin head pops off and lands back on: an orange burst with embers
		SQFx::Flash(World, Head, Flame, 120.f, 0.14f);
		SQRiseToss(World, Head, Feet.Z, Flame, ESQFxShape::Cube, FVector(9.f), 8, 420.f, 200.f, -1300.f, 0.f);
		SQFx::Burst(World, Head, Gold, 6, 160.f, 0.5f, 5.f, 400.f);
	}
	else if (Fx == TEXT("Soot"))
	{
		SQFx::Burst(World, Body + FVector(0.f, 0.f, 20.f), FLinearColor(0.18f, 0.18f, 0.2f), 12, 180.f, 0.6f, 14.f, 150.f);
		SQFx::Burst(World, Body + FVector(0.f, 0.f, 20.f), Flame, 4, 220.f, 0.5f, 4.f, 300.f);
	}
	else if (Fx == TEXT("Veil"))
	{
		for (int32 Wisp = 0; Wisp < 8; ++Wisp)
		{
			FSQFxParticle P;
			const float Ang = 45.f * Wisp;
			P.Pos = SQRiseOnCircle(Body, 30.f, Ang);
			P.Vel = SQRiseOnCircle(FVector::ZeroVector, 180.f, Ang + 70.f) + FVector(0.f, 0.f, 160.f);
			P.Drag = 1.5f;
			P.Color = Col;
			P.Life = 0.7f;
			P.SizeStart = FVector(12.f);
			P.SizeEnd = FVector(5.f);
			P.TrailEvery = 0.04f;
			P.TrailSize = 5.f;
			P.TrailColor = Col;
			SQFx::Emit(World, P);
		}
	}
	else if (Fx == TEXT("Dash"))
	{
		for (int32 LineIdx = -1; LineIdx <= 1; ++LineIdx)
		{
			const FVector Off = FVector::CrossProduct(FVector::UpVector, Fwd) * (22.f * LineIdx) + FVector(0.f, 0.f, 20.f + 25.f * (LineIdx + 1));
			SQFx::Streak(World, Body - Fwd * 160.f + Off, Body - Fwd * 30.f + Off, Col, 8.f, 0.25f);
		}
		SQFx::Ring(World, Feet + FVector(0.f, 0.f, 4.f), FLinearColor(0.55f, 0.5f, 0.45f), 12, 300.f, 0.35f, 8.f);
	}
	else if (Fx == TEXT("Calm"))
	{
		SQFx::Shockwave(World, Feet + FVector(0.f, 0.f, 10.f), 180.f, Col, 0.7f, 10.f);
		SQFx::Shockwave(World, Feet + FVector(0.f, 0.f, 40.f), 130.f, Col, 0.7f, 8.f, 0.2f);
		SQFx::Glint(World, Head + FVector(0.f, 0.f, 30.f), Col, 60.f, 0.3f);
	}
	else if (Fx == TEXT("Fetch"))
	{
		// A bone tossed up, caught on the way down
		FSQFxParticle P;
		P.Pos = Head + Fwd * 20.f;
		P.Vel = FVector(0.f, 0.f, 520.f);
		P.Gravity = -1600.f;
		P.Shape = ESQFxShape::Cube;
		P.Color = SQRiseBone;
		P.Life = 0.62f;
		P.SizeStart = P.SizeEnd = FVector(34.f, 8.f, 8.f);
		P.SpinAxis = FVector::RightVector;
		P.SpinRate = 18.f;
		SQFx::Emit(World, P);
		SQFx::Burst(World, Feet + FVector(0.f, 0.f, 10.f), FLinearColor(0.6f, 0.55f, 0.5f), 6, 160.f, 0.4f, 9.f, 50.f);
	}
	else if (Fx == TEXT("Dig"))
	{
		SQFx::DirtClods(World, Feet - Fwd * 50.f, 10, 520.f, 12.f);
	}
	else
	{
		// Scythe (default): a glint runs up the blade, a soul ring at the feet
		SQFx::Glint(World, WeaponTop, SQRiseSoul, 80.f);
		SQFx::Shockwave(World, Feet, 150.f, SQRiseSoul, 0.3f, 16.f, 0.05f);
	}
}
