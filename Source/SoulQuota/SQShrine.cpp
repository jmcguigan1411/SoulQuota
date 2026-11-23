// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQShrine.h"
#include "SQFxRecipe.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQArena.h"
#include "SQArt.h"
#include "SQBoss.h"
#include "SQCharacter.h"
#include "SQChest.h"
#include "SQData.h"
#include "SQFx.h"
#include "SQGameState.h"
#include "SQHordeManager.h"
#include "SQPlayerState.h"

namespace
{
	const int32 ShrineRingSegments = 40;
	const float ShrinePedestalRadius = 100.f;
	const float ShrinePickGrace = 0.8f;

	bool SQShrineStatsEqual(const FSQStats& A, const FSQStats& B)
	{
		return A.MaxHealth == B.MaxHealth && A.MoveSpeed == B.MoveSpeed && A.DamageMult == B.DamageMult
			&& A.CooldownMult == B.CooldownMult && A.AreaMult == B.AreaMult && A.CarryCap == B.CarryCap
			&& A.Magnet == B.Magnet && A.Armor == B.Armor && A.ProjectileBonus == B.ProjectileBonus
			&& A.DashCooldown == B.DashCooldown && A.Regen == B.Regen && A.TurretBonus == B.TurretBonus
			&& A.SoulBonus == B.SoulBonus && A.BankSpeed == B.BankSpeed && A.Thorns == B.Thorns
			&& A.PickupHeal == B.PickupHeal && A.Luck == B.Luck && A.CritChance == B.CritChance
			&& A.CritDamage == B.CritDamage && A.Duration == B.Duration && A.ExtraJumps == B.ExtraJumps
			&& A.Evasion == B.Evasion && A.AllyRegen == B.AllyRegen;
	}


	/** Feet height of a reaper (capsule bottom) */
	float SQShrineFeetZ(const ASQCharacter* C)
	{
		const UCapsuleComponent* Capsule = C->GetCapsuleComponent();
		const float HalfH = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
		return static_cast<float>(C->GetActorLocation().Z) - HalfH;
	}

	/** Local camera (for facing text and distance culling); false on a dedicated server */
	bool SQShrineViewLocation(const UWorld* World, FVector& Out)
	{
		const APlayerCameraManager* Cam = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
		if (!Cam) return false;
		Out = Cam->GetCameraLocation();
		return true;
	}

	void SQShrineFaceCamera(UTextRenderComponent* Text, const FVector& ViewLoc)
	{
		if (!Text) return;
		const FRotator Look = (ViewLoc - Text->GetComponentLocation()).Rotation();
		Text->SetWorldRotation(FRotator(FMath::Clamp(static_cast<float>(Look.Pitch), -60.f, 60.f), static_cast<float>(Look.Yaw), 0.f));
	}

	/** Quest stat ShrinesUsed (Quests.csv): one per reaper who uses / completes a shrine (server) */
	void SQShrineCreditUse(const ASQCharacter* Reaper)
	{
		if (ASQPlayerState* UsePS = Reaper ? Reaper->GetPlayerState<ASQPlayerState>() : nullptr) UsePS->CreditQuest(TEXT("ShrinesUsed"));
	}
}

// ================================================================ boost ledger

USQBoostLedger* USQBoostLedger::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<USQBoostLedger>() : nullptr;
}

bool USQBoostLedger::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USQBoostLedger::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USQBoostLedger, STATGROUP_Tickables);
}

void USQBoostLedger::AddBoost(ASQCharacter* Reaper, FName Stat, float Value, float Seconds)
{
	if (!Reaper || !Reaper->HasAuthority() || Stat.IsNone()) return;
	FSQStats Probe;
	if (!SQ::ApplyStat(Probe, Stat, 0.f)) return; // unknown stat name (CSV typo)
	const TWeakObjectPtr<ASQCharacter> Key(Reaper);
	FLedgerEntry& Entry = Entries.FindOrAdd(Key);
	FLedgerBoost Boost;
	Boost.Stat = Stat;
	Boost.Value = Value;
	Boost.TimeLeft = Seconds > 0.f ? Seconds : -1.f;
	Entry.Boosts.Add(Boost);
	Entry.bDirty = true;
}

void USQBoostLedger::ClearAll()
{
	// Hand every reaper back its own stats (no-op when the character already recomputed them)
	for (TPair<TWeakObjectPtr<ASQCharacter>, FLedgerEntry>& Pair : Entries)
	{
		ASQCharacter* C = Pair.Key.Get();
		if (C && Pair.Value.bApplied && SQShrineStatsEqual(C->Stats, Pair.Value.AppliedStats))
		{
			FSQStats Restored = Pair.Value.BaseStats; // raw (items/tomes pass): run the caps + curves once
			SQ::FinalizeStats(Restored, USQData::Get(C));
			C->Stats = Restored;
			C->Health = FMath::Min(C->Health, C->Stats.MaxHealth);
		}
	}
	Entries.Reset();
}

void USQBoostLedger::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || Entries.Num() == 0) return;
	const ASQGameState* GS = World->GetGameState<ASQGameState>();
	if (GS && GS->Phase == ESQPhase::Waiting)
	{
		ClearAll(); // a new run: shrine boosts last one run
		return;
	}

	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		ASQCharacter* C = It.Key().Get();
		if (!C)
		{
			It.RemoveCurrent();
			continue;
		}
		FLedgerEntry& Entry = It.Value();
		for (int32 b = Entry.Boosts.Num() - 1; b >= 0; --b)
		{
			FLedgerBoost& Boost = Entry.Boosts[b];
			if (Boost.TimeLeft < 0.f) continue;
			Boost.TimeLeft -= DeltaTime;
			if (Boost.TimeLeft <= 0.f)
			{
				Entry.Boosts.RemoveAt(b);
				Entry.bDirty = true;
			}
		}

		// The character recomputed its stats since we last wrote them (relic pick, bond, level passive...): new base
		const bool bRecomputed = !Entry.bApplied || !SQShrineStatsEqual(C->Stats, Entry.AppliedStats);
		if (!bRecomputed && !Entry.bDirty)
		{
			Entry.LastHealth = C->Health;
			continue;
		}
		// Raises add to the character's RAW stats, then the caps + diminishing curves run once (SQ::FinalizeStats)
		if (bRecomputed) Entry.BaseStats = C->RawStats;

		FSQStats Boosted = Entry.BaseStats;
		for (const FLedgerBoost& Boost : Entry.Boosts) SQ::ApplyStat(Boosted, Boost.Stat, Boost.Value);
		SQ::FinalizeStats(Boosted, USQData::Get(C));

		if (!C->bDowned)
		{
			if (!Entry.bApplied)
			{
				// First raise: max HP gained comes with the health, like a relic pick
				if (Boosted.MaxHealth > Entry.BaseStats.MaxHealth) C->Health += Boosted.MaxHealth - Entry.BaseStats.MaxHealth;
			}
			else if (bRecomputed)
			{
				// RecomputeStats clamped health to the un-boosted max: give back what it cut off
				if (Entry.LastHealth > C->Health && FMath::IsNearlyEqual(C->Health, Entry.BaseStats.MaxHealth, 0.5f))
				{
					C->Health = FMath::Min(Entry.LastHealth, Boosted.MaxHealth);
				}
			}
			else if (Boosted.MaxHealth > Entry.AppliedStats.MaxHealth)
			{
				C->Health += Boosted.MaxHealth - Entry.AppliedStats.MaxHealth;
			}
		}
		C->Stats = Boosted;
		C->Health = FMath::Min(C->Health, C->Stats.MaxHealth);
		Entry.AppliedStats = Boosted;
		Entry.bApplied = true;
		Entry.bDirty = false;
		Entry.LastHealth = C->Health;

		if (Entry.Boosts.Num() == 0) It.RemoveCurrent(); // back to the character's own stats: stop tracking
	}
}

// ================================================================ shrine

ASQShrine::ASQShrine()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);

	ShrineRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShrineRoot"));
	RootComponent = ShrineRoot;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(ShrineRoot);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCanEverAffectNavigation(false);

	OrbMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OrbMesh"));
	OrbMesh->SetupAttachment(ShrineRoot);
	OrbMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OrbMesh->SetCastShadow(false);
	OrbMesh->SetCanEverAffectNavigation(false);

	BeamMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeamMesh"));
	BeamMesh->SetupAttachment(ShrineRoot);
	BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamMesh->SetCastShadow(false);
	BeamMesh->SetCanEverAffectNavigation(false);
	BeamMesh->SetVisibility(false);

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(ShrineRoot);
	GlowLight->SetCastShadows(false);
	GlowLight->SetIntensity(0.f);

	PromptText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PromptText"));
	PromptText->SetupAttachment(ShrineRoot);
	PromptText->SetHorizontalAlignment(EHTA_Center);
	PromptText->SetVerticalAlignment(EVRTA_TextBottom);
	PromptText->SetWorldSize(34.f);
	PromptText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PromptText->SetCastShadow(false);

	RingLit = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RingLit"));
	RingLit->SetupAttachment(ShrineRoot);
	RingLit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RingLit->SetCastShadow(false);
	RingLit->SetCanEverAffectNavigation(false);

	RingDim = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RingDim"));
	RingDim->SetupAttachment(ShrineRoot);
	RingDim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RingDim->SetCastShadow(false);
	RingDim->SetCanEverAffectNavigation(false);

	for (int32 i = 0; i < 3; ++i)
	{
		UStaticMeshComponent* PedBase = CreateDefaultSubobject<UStaticMeshComponent>(FName(*FString::Printf(TEXT("PedestalBase%d"), i)));
		PedBase->SetupAttachment(ShrineRoot);
		PedBase->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PedBase->SetCanEverAffectNavigation(false);
		PedBase->SetVisibility(false);
		PedestalBases.Add(PedBase);

		UStaticMeshComponent* PedOrb = CreateDefaultSubobject<UStaticMeshComponent>(FName(*FString::Printf(TEXT("PedestalOrb%d"), i)));
		PedOrb->SetupAttachment(ShrineRoot);
		PedOrb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PedOrb->SetCastShadow(false);
		PedOrb->SetCanEverAffectNavigation(false);
		PedOrb->SetVisibility(false);
		PedestalOrbs.Add(PedOrb);

		UTextRenderComponent* PedText = CreateDefaultSubobject<UTextRenderComponent>(FName(*FString::Printf(TEXT("PedestalText%d"), i)));
		PedText->SetupAttachment(ShrineRoot);
		PedText->SetHorizontalAlignment(EHTA_Center);
		PedText->SetVerticalAlignment(EVRTA_TextBottom);
		PedText->SetWorldSize(26.f);
		PedText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PedText->SetCastShadow(false);
		PedText->SetVisibility(false);
		PedestalTexts.Add(PedText);
	}
}

void ASQShrine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQShrine, DefId);
	DOREPLIFETIME(ASQShrine, State);
	DOREPLIFETIME(ASQShrine, Progress);
	DOREPLIFETIME(ASQShrine, OfferIds);
	DOREPLIFETIME(ASQShrine, OfferValues);
	DOREPLIFETIME(ASQShrine, OfferRarity);
	DOREPLIFETIME(ASQShrine, PicksLeft);
}

ESQShrineType ASQShrine::ParseType(FName TypeName)
{
	const FString T = TypeName.ToString();
	if (T.Equals(TEXT("Relic"), ESearchCase::IgnoreCase)) return ESQShrineType::Relic;
	if (T.Equals(TEXT("Challenge"), ESearchCase::IgnoreCase)) return ESQShrineType::Challenge;
	if (T.Equals(TEXT("Greed"), ESearchCase::IgnoreCase)) return ESQShrineType::Greed;
	if (T.Equals(TEXT("Magnet"), ESearchCase::IgnoreCase)) return ESQShrineType::Magnet;
	if (T.Equals(TEXT("Cursed"), ESearchCase::IgnoreCase)) return ESQShrineType::Cursed;
	if (T.Equals(TEXT("Key"), ESearchCase::IgnoreCase)) return ESQShrineType::Key;
	if (T.Equals(TEXT("Crypt"), ESearchCase::IgnoreCase)) return ESQShrineType::Crypt;
	if (T.Equals(TEXT("Pot"), ESearchCase::IgnoreCase)) return ESQShrineType::Pot;
	return ESQShrineType::Charge;
}

const FSQShrineDef* ASQShrine::GetDef() const
{
	const USQData* D = USQData::Get(this);
	return D ? D->FindShrine(DefId) : nullptr;
}

ESQShrineType ASQShrine::GetShrineType() const
{
	const FSQShrineDef* Def = GetDef();
	return Def ? ParseType(Def->Type) : ESQShrineType::Charge;
}

void ASQShrine::BeginPlay()
{
	Super::BeginPlay();
	if (!DefId.IsNone()) ApplyDefVisuals();
}

void ASQShrine::InitShrine(FName InDefId)
{
	if (!HasAuthority()) return;
	DefId = InDefId;
	const FSQShrineDef* Def = GetDef();
	const ESQShrineType Type = GetShrineType();
	ESQShrineState Start = ESQShrineState::Idle;
	if (Def && Def->bHidden) Start = ESQShrineState::Buried;
	else if (Type == ESQShrineType::Crypt) Start = ESQShrineState::Locked;
	State = static_cast<uint8>(Start);
	Progress = 0.f;
	ApplyDefVisuals();
	ForceNetUpdate();
}

void ASQShrine::OnRep_Def()
{
	if (HasActorBegunPlay()) ApplyDefVisuals();
}

void ASQShrine::OnRep_State()
{
	RingLitShown = -1; // refresh the ring on the next visual tick
}

void ASQShrine::SetShrineState(ESQShrineState NewState)
{
	State = static_cast<uint8>(NewState);
	StateTime = 0.f;
	OnRep_State();
	ForceNetUpdate();
}

void ASQShrine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && EndPlayReason == EEndPlayReason::Destroyed) DismissCopy();
	Super::EndPlay(EndPlayReason);
}

void ASQShrine::Alert(const FString& Text) const
{
	UWorld* World = GetWorld();
	if (ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr) GS->PushAlert(Text);
}

float ASQShrine::NightGoldMult() const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	return 1.f + 0.5f * static_cast<float>(GS ? FMath::Max(0, GS->NightIndex) : 0);
}

void ASQShrine::PayEveryone(int32 GoldEach) const
{
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!GS || GoldEach <= 0) return;
	for (const TObjectPtr<APlayerState>& PSPtr : GS->PlayerArray)
	{
		if (ASQPlayerState* SPS = Cast<ASQPlayerState>(PSPtr.Get())) SPS->AddGold(GoldEach);
	}
}

void ASQShrine::SpawnFreeChest(const FVector& Near) const
{
	UWorld* World = GetWorld();
	if (!World) return;
	const float CX = static_cast<float>(Near.X);
	const float CY = static_cast<float>(Near.Y);
	ASQChest::SpawnChest(World, FVector(CX, CY, ASQArena::GroundZAt(this, CX, CY)), true);
}

void ASQShrine::GatherStanding(TArray<ASQCharacter*>& Out) const
{
	Out.Reset();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* C = *It;
		if (IsValid(C) && !C->bDowned && C->GetPlayerState()) Out.Add(C);
	}
}

int32 ASQShrine::CountNear(const TArray<ASQCharacter*>& Reapers, float Radius, ASQCharacter** OutFirst) const
{
	const FVector Here = GetActorLocation();
	const float RSq = Radius * Radius;
	int32 Count = 0;
	for (ASQCharacter* C : Reapers)
	{
		const FVector L = C->GetActorLocation();
		if (FMath::Square(static_cast<float>(L.X - Here.X)) + FMath::Square(static_cast<float>(L.Y - Here.Y)) > RSq) continue;
		if (FMath::Abs(SQShrineFeetZ(C) - static_cast<float>(Here.Z)) > 250.f) continue; // on a plateau above / in a basin below
		if (Count == 0 && OutFirst) *OutFirst = C;
		++Count;
	}
	return Count;
}

float ASQShrine::PedestalDistance() const
{
	const FSQShrineDef* Def = GetDef();
	const float R = Def ? Def->Radius : 300.f;
	return GetShrineType() == ESQShrineType::Relic ? FMath::Max(220.f, R) : FMath::Max(200.f, R * 0.62f);
}

FVector ASQShrine::PedestalLocation(int32 Index) const
{
	const int32 Num = FMath::Max(1, OfferIds.Num());
	const float Ang = 2.f * PI * static_cast<float>(Index) / static_cast<float>(Num) + PI * 0.5f;
	const float Dist = PedestalDistance();
	const FVector Here = GetActorLocation();
	const float PX = static_cast<float>(Here.X) + FMath::Cos(Ang) * Dist;
	const float PY = static_cast<float>(Here.Y) + FMath::Sin(Ang) * Dist;
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	const float PZ = (Arena && Arena->HasTerrain()) ? Arena->GroundZ(PX, PY) : static_cast<float>(Here.Z);
	return FVector(PX, PY, PZ);
}

// ---------------------------------------------------------------- server

void ASQShrine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority()) ServerTick(DeltaSeconds);
	if (GetNetMode() != NM_DedicatedServer) TickVisuals(DeltaSeconds);
}

void ASQShrine::ServerTick(float DeltaSeconds)
{
	const FSQShrineDef* Def = GetDef();
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const USQData* D = USQData::Get(this);
	if (!Def || !GS || !D) return;
	StateTime += DeltaSeconds;
	const bool bLive = GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;
	const ESQShrineType Type = ParseType(Def->Type);

	TArray<ASQCharacter*> Reapers;
	GatherStanding(Reapers);

	switch (GetShrineState())
	{
	case ESQShrineState::Buried:
	{
		if (!bLive) break;
		const bool bDigging = CountNear(Reapers, 170.f) > 0;
		const float DigSeconds = FMath::Max(0.5f, Def->Value);
		Progress = FMath::Clamp(Progress + (bDigging ? DeltaSeconds / DigSeconds : -DeltaSeconds * 0.2f), 0.f, 1.f);
		if (Progress >= 1.f)
		{
			Progress = 0.f;
			SetShrineState(ESQShrineState::Idle);
			MulticastShrineFx(GetActorLocation(), Def->Color.ToFColor(true), 1);
			Alert(FString::Printf(TEXT("Someone dug up the %s! Stand in its ring to charge it."), *Def->Name));
		}
		break;
	}
	case ESQShrineState::Idle:
	case ESQShrineState::Charging:
	{
		if (!bLive) break;
		ASQCharacter* First = nullptr;
		const int32 Inside = CountNear(Reapers, Def->Radius, &First);
		switch (Type)
		{
		case ESQShrineType::Charge:
		{
			if (Inside > 0)
			{
				if (GetShrineState() == ESQShrineState::Idle) SetShrineState(ESQShrineState::Charging);
				// Co-op bonus: every extra reaper in the ring adds CoopBonus to the charge rate
				Progress += DeltaSeconds / Def->ChargeSeconds * (1.f + Def->CoopBonus * static_cast<float>(Inside - 1));
			}
			else if (Progress > 0.f)
			{
				Progress = FMath::Max(0.f, Progress - DeltaSeconds / Def->ChargeSeconds * 0.25f);
				if (Progress <= 0.f) SetShrineState(ESQShrineState::Idle);
			}
			if (Progress >= 1.f)
			{
				Progress = 1.f;
				TArray<ASQCharacter*> Pickers;
				for (ASQCharacter* C : Reapers)
				{
					TArray<ASQCharacter*> One;
					One.Add(C);
					if (CountNear(One, Def->Radius * 1.5f) > 0) Pickers.Add(C);
				}
				BeginChoosing(Pickers, false);
			}
			break;
		}
		case ESQShrineType::Relic:
			if (First)
			{
				TArray<ASQCharacter*> Pickers;
				Pickers.Add(First);
				for (ASQCharacter* C : Reapers)
				{
					if (C != First) Pickers.Add(C);
				}
				BeginChoosing(Pickers, true);
			}
			break;
		case ESQShrineType::Challenge:
			if (First)
			{
				int32 Team = 0;
				for (TActorIterator<ASQCharacter> It(World); It; ++It)
				{
					if (It->GetPlayerState()) ++Team;
				}
				StartChallenge(FMath::Max(1, Team));
			}
			break;
		case ESQShrineType::Greed:
			if (First)
			{
				const int32 GoldEach = FMath::Max(1, FMath::RoundToInt(Def->Value * NightGoldMult()));
				PayEveryone(GoldEach);
				SQShrineCreditUse(First);
				GS->AddDifficulty(Def->Value2);
				MulticastShrineFx(GetActorLocation(), FColor(255, 210, 90), 2);
				Alert(FString::Printf(TEXT("PETTY CASH: +%d gold each. The horde is %d%% meaner for the rest of the run."), GoldEach, FMath::RoundToInt(Def->Value2 * 100.f)));
				SetShrineState(ESQShrineState::Spent);
			}
			break;
		case ESQShrineType::Magnet:
			if (First)
			{
				if (USQBoostLedger* Ledger = USQBoostLedger::Get(this)) Ledger->AddBoost(First, TEXT("Magnet"), Def->Value, FMath::Max(1.f, Def->Duration));
				SQShrineCreditUse(First);
				const APlayerState* PS = First->GetPlayerState();
				Alert(FString::Printf(TEXT("COLLECTIONS: every soul and spark on the map flies to %s!"), PS ? *PS->GetPlayerName() : TEXT("a reaper")));
				MulticastShrineFx(GetActorLocation(), Def->Color.ToFColor(true), 1);
				Progress = 0.f;
				SetShrineState(ESQShrineState::Active);
			}
			break;
		case ESQShrineType::Cursed:
		{
			const float MinLeft = D->Tune(TEXT("ShrineCursedMinSeconds"), 45.f);
			if (First && GS->Phase == ESQPhase::Night && GS->PhaseTimeLeft > MinLeft)
			{
				CopiesLeft = FMath::Max(1, Def->Count);
				SQShrineCreditUse(First);
				SetShrineState(ESQShrineState::Active);
				SpawnCursedCopy();
			}
			break;
		}
		case ESQShrineType::Key:
			if (First)
			{
				for (TActorIterator<ASQShrine> It(World); It; ++It)
				{
					ASQShrine* Other = *It;
					if (Other && Other->GetShrineType() == ESQShrineType::Crypt && Other->GetShrineState() == ESQShrineState::Locked)
					{
						Other->SetShrineState(ESQShrineState::Idle);
						Other->MulticastShrineFx(Other->GetActorLocation(), FColor(120, 255, 160), 1);
					}
				}
				MulticastShrineFx(GetActorLocation(), Def->Color.ToFColor(true), 2);
				SQShrineCreditUse(First);
				Alert(TEXT("GROUNDSKEEPER'S KEY FOUND: the Sealed Crypt is open!"));
				SetShrineState(ESQShrineState::Spent);
			}
			break;
		case ESQShrineType::Crypt:
			if (First)
			{
				const FVector Here = GetActorLocation();
				SpawnFreeChest(Here + FVector(Def->Radius + 120.f, 0.f, 0.f));
				const int32 GoldEach = FMath::RoundToInt(Def->Value * NightGoldMult());
				PayEveryone(GoldEach);
				SQShrineCreditUse(First);
				MulticastShrineFx(Here, FColor(255, 210, 90), 2);
				Alert(FString::Printf(TEXT("SEALED CRYPT OPENED: a free chest and +%d gold each!"), GoldEach));
				SetShrineState(ESQShrineState::Spent);
			}
			break;
		default:
			break;
		}
		break;
	}
	case ESQShrineState::Choosing:
		TickChoosing(DeltaSeconds, Reapers);
		break;
	case ESQShrineState::Active:
		if (Type == ESQShrineType::Challenge) TickChallenge(DeltaSeconds, Reapers);
		else if (Type == ESQShrineType::Cursed) TickCursed();
		else if (Type == ESQShrineType::Magnet)
		{
			const float Dur = FMath::Max(1.f, Def->Duration);
			Progress = FMath::Clamp(StateTime / Dur, 0.f, 1.f);
			if (StateTime >= Dur) SetShrineState(ESQShrineState::Spent);
		}
		break;
	default:
		break;
	}
}

uint8 ASQShrine::RollRarity(int32 MinRarity) const
{
	if (MinRarity >= 3) return 3;
	// The level-up card odds (Tuning RarityRare / Epic / Legendary), with tiers below the shrine's minimum removed
	const USQData* D = USQData::Get(this);
	const float RareW = D ? D->Tune(TEXT("RarityRare"), 27.f) : 27.f;
	const float EpicW = D ? D->Tune(TEXT("RarityEpic"), 10.f) : 10.f;
	const float LegendW = D ? D->Tune(TEXT("RarityLegendary"), 3.f) : 3.f;
	float Weights[4] = { FMath::Max(0.f, 100.f - RareW - EpicW - LegendW), FMath::Max(0.f, RareW), FMath::Max(0.f, EpicW), FMath::Max(0.f, LegendW) };
	float Total = 0.f;
	for (int32 Tier = 0; Tier < 4; ++Tier)
	{
		if (Tier < MinRarity) Weights[Tier] = 0.f;
		Total += Weights[Tier];
	}
	if (Total <= 0.f) return static_cast<uint8>(FMath::Clamp(MinRarity, 0, 3));
	float Roll = FMath::FRandRange(0.f, Total);
	for (int32 Tier = 0; Tier < 4; ++Tier)
	{
		Roll -= Weights[Tier];
		if (Roll <= 0.f) return static_cast<uint8>(Tier);
	}
	return 3;
}

bool ASQShrine::RollOffers(const TArray<ASQCharacter*>& Pickers)
{
	OfferIds.Reset();
	OfferValues.Reset();
	OfferRarity.Reset();
	const FSQShrineDef* Def = GetDef();
	const USQData* D = USQData::Get(this);
	if (!Def || !D) return false;
	const int32 Wanted = FMath::Clamp(Def->Choices, 1, 3);

	if (GetShrineType() == ESQShrineType::Relic)
	{
		// Items at least one reaper here can still take (items/tomes pass: Lost Property gives items, never tomes)
		TArray<FName> Pool;
		TArray<float> Weights;
		for (const FSQUpgradeDef& U : D->Upgrades)
		{
			if (!U.IsItem()) continue;
			bool bAnyCan = false;
			for (const ASQCharacter* C : Pickers)
			{
				if (C && C->CanTakeUpgrade(U)) { bAnyCan = true; break; }
			}
			if (!bAnyCan) continue;
			Pool.Add(U.Id);
			Weights.Add(FMath::Max(0.1f, U.Weight));
		}
		while (OfferIds.Num() < Wanted && Pool.Num() > 0)
		{
			float Total = 0.f;
			for (const float W : Weights) Total += W;
			float Roll = FMath::FRandRange(0.f, Total);
			int32 Chosen = Pool.Num() - 1;
			for (int32 i = 0; i < Pool.Num(); ++i)
			{
				Roll -= Weights[i];
				if (Roll <= 0.f) { Chosen = i; break; }
			}
			OfferIds.Add(Pool[Chosen]);
			OfferValues.Add(0.f);
			OfferRarity.Add(RollRarity(Def->Rarity));
			Pool.RemoveAt(Chosen);
			Weights.RemoveAt(Chosen);
		}
	}
	else
	{
		// Charge: "Stat:Amount|..." -> Choices different stats, each at a rolled rarity (x1.0 / 1.4 / 1.9 / 2.6)
		TArray<FName> Stats;
		TArray<float> Amounts;
		TArray<FString> Entries;
		Def->Boosts.ParseIntoArray(Entries, TEXT("|"), true);
		for (const FString& Entry : Entries)
		{
			FString StatName;
			FString AmountText;
			if (!Entry.Split(TEXT(":"), &StatName, &AmountText)) continue;
			const FName StatId(*StatName.TrimStartAndEnd());
			FSQStats Probe;
			if (StatId.IsNone() || !SQ::ApplyStat(Probe, StatId, 0.f)) continue;
			Stats.Add(StatId);
			Amounts.Add(FCString::Atof(*AmountText.TrimStartAndEnd()));
		}
		while (OfferIds.Num() < Wanted && Stats.Num() > 0)
		{
			const int32 Pick = FMath::RandRange(0, Stats.Num() - 1);
			const uint8 Rarity = RollRarity(Def->Rarity);
			OfferIds.Add(Stats[Pick]);
			OfferValues.Add(Amounts[Pick] * SQ::RarityStatMult(Rarity));
			OfferRarity.Add(Rarity);
			Stats.RemoveAt(Pick);
			Amounts.RemoveAt(Pick);
		}
	}
	return OfferIds.Num() > 0;
}

void ASQShrine::BeginChoosing(const TArray<ASQCharacter*>& Pickers, bool bFirstCome)
{
	const FSQShrineDef* Def = GetDef();
	if (!Def || Pickers.Num() == 0 || !RollOffers(Pickers))
	{
		if (Def && GetShrineType() == ESQShrineType::Relic) return; // nobody can take a relic right now: stay open
		SetShrineState(ESQShrineState::Spent);
		return;
	}
	bFirstComeOnly = bFirstCome;
	Eligible.Reset();
	for (ASQCharacter* C : Pickers) Eligible.Add(C);
	PicksLeft = static_cast<uint8>(FMath::Clamp(bFirstCome ? 1 : Eligible.Num(), 0, 255));
	for (int32 i = 0; i < 3; ++i)
	{
		DwellWho[i] = nullptr;
		DwellTime[i] = 0.f;
	}
	Progress = 1.f;
	SetShrineState(ESQShrineState::Choosing);
	MulticastShrineFx(GetActorLocation(), Def->Color.ToFColor(true), 1);
	if (!bFirstCome) Alert(FString::Printf(TEXT("%s complete: everyone who was in range, stand on a pedestal to pick a raise!"), *Def->Name));

	// Bots pick straight away (a random card, like their level-ups)
	for (int32 e = Eligible.Num() - 1; e >= 0 && GetShrineState() == ESQShrineState::Choosing; --e)
	{
		ASQCharacter* C = Eligible[e].Get();
		const APlayerState* PS = C ? C->GetPlayerState() : nullptr;
		if (!PS || !PS->IsABot()) continue;
		if (bFirstComeOnly && C != Pickers[0]) continue; // only the bot that opened the desk takes from it
		const int32 Pick = FMath::RandRange(0, OfferIds.Num() - 1);
		if (bFirstComeOnly)
		{
			const USQData* D = USQData::Get(this);
			const FSQUpgradeDef* U = D ? D->FindUpgrade(OfferIds[Pick]) : nullptr;
			if (!U || !C->CanTakeUpgrade(*U)) continue;
		}
		GrantOffer(C, Pick);
		Eligible.RemoveAt(e);
		PicksLeft = PicksLeft > 0 ? static_cast<uint8>(PicksLeft - 1) : static_cast<uint8>(0);
		if (bFirstComeOnly) SetShrineState(ESQShrineState::Spent);
	}
	if (!bFirstComeOnly && Eligible.Num() == 0 && GetShrineState() == ESQShrineState::Choosing) SetShrineState(ESQShrineState::Spent);
}

void ASQShrine::GrantOffer(ASQCharacter* Reaper, int32 Index)
{
	if (!Reaper || !OfferIds.IsValidIndex(Index)) return;
	const uint8 Rarity = OfferRarity.IsValidIndex(Index) ? OfferRarity[Index] : 0;
	const APlayerState* PS = Reaper->GetPlayerState();
	const FString Who = PS ? PS->GetPlayerName() : FString(TEXT("A reaper"));
	FString What;
	if (GetShrineType() == ESQShrineType::Relic)
	{
		Reaper->ApplyUpgrade(OfferIds[Index], Rarity);
		const USQData* D = USQData::Get(this);
		const FSQUpgradeDef* U = D ? D->FindUpgrade(OfferIds[Index]) : nullptr;
		What = FString::Printf(TEXT("%s (%s)"), U ? *U->Name : *OfferIds[Index].ToString(), *SQ::RarityName(Rarity));
	}
	else
	{
		const float Value = OfferValues.IsValidIndex(Index) ? OfferValues[Index] : 0.f;
		if (USQBoostLedger* Ledger = USQBoostLedger::Get(this)) Ledger->AddBoost(Reaper, OfferIds[Index], Value);
		What = FString::Printf(TEXT("%s (%s raise)"), *SQ::StatText(OfferIds[Index], Value), *SQ::RarityName(Rarity));
	}
	SQShrineCreditUse(Reaper);
	Alert(FString::Printf(TEXT("%s took %s"), *Who, *What));
	MulticastShrineFx(Reaper->GetActorLocation(), SQ::RarityColor(Rarity).ToFColor(true), 0);
}

void ASQShrine::TickChoosing(float DeltaSeconds, const TArray<ASQCharacter*>& Reapers)
{
	const USQData* D = USQData::Get(this);
	const FSQShrineDef* Def = GetDef();
	if (!D || !Def) return;
	const float DwellNeed = FMath::Max(0.1f, D->Tune(TEXT("ShrineDwellSeconds"), 0.5f));
	const float PickSeconds = FMath::Max(3.f, D->Tune(TEXT("ShrinePickSeconds"), 25.f));
	const bool bRelic = GetShrineType() == ESQShrineType::Relic;

	for (int32 e = Eligible.Num() - 1; e >= 0; --e)
	{
		if (!Eligible[e].IsValid()) Eligible.RemoveAt(e);
	}

	if (StateTime >= ShrinePickGrace)
	{
		for (int32 i = 0; i < OfferIds.Num() && i < 3; ++i)
		{
			const FVector Ped = PedestalLocation(i);
			ASQCharacter* OnIt = nullptr;
			for (ASQCharacter* C : Reapers)
			{
				const FVector L = C->GetActorLocation();
				if (FMath::Square(static_cast<float>(L.X - Ped.X)) + FMath::Square(static_cast<float>(L.Y - Ped.Y)) > ShrinePedestalRadius * ShrinePedestalRadius) continue;
				if (FMath::Abs(SQShrineFeetZ(C) - static_cast<float>(Ped.Z)) > 200.f) continue;
				if (!bFirstComeOnly && !Eligible.Contains(TWeakObjectPtr<ASQCharacter>(C))) continue;
				if (bRelic)
				{
					const FSQUpgradeDef* U = D->FindUpgrade(OfferIds[i]);
					if (!U || !C->CanTakeUpgrade(*U)) continue;
				}
				OnIt = C;
				break;
			}
			if (!OnIt)
			{
				DwellWho[i] = nullptr;
				DwellTime[i] = 0.f;
				continue;
			}
			if (DwellWho[i].Get() == OnIt) DwellTime[i] += DeltaSeconds;
			else
			{
				DwellWho[i] = OnIt;
				DwellTime[i] = DeltaSeconds;
			}
			if (DwellTime[i] < DwellNeed) continue;

			GrantOffer(OnIt, i);
			Eligible.Remove(TWeakObjectPtr<ASQCharacter>(OnIt));
			PicksLeft = PicksLeft > 0 ? static_cast<uint8>(PicksLeft - 1) : static_cast<uint8>(0);
			for (int32 k = 0; k < 3; ++k)
			{
				if (DwellWho[k].Get() == OnIt)
				{
					DwellWho[k] = nullptr;
					DwellTime[k] = 0.f;
				}
			}
			if (bFirstComeOnly)
			{
				SetShrineState(ESQShrineState::Spent);
				return;
			}
		}
	}

	if (bFirstComeOnly)
	{
		Progress = 1.f;
		return;
	}
	// Charge: whoever hasn't picked when the time runs out gets a random raise
	Progress = FMath::Clamp(1.f - StateTime / PickSeconds, 0.f, 1.f);
	if (StateTime >= PickSeconds)
	{
		for (const TWeakObjectPtr<ASQCharacter>& Left : Eligible)
		{
			if (ASQCharacter* C = Left.Get()) GrantOffer(C, FMath::RandRange(0, OfferIds.Num() - 1));
		}
		Eligible.Reset();
	}
	if (Eligible.Num() == 0)
	{
		PicksLeft = 0;
		SetShrineState(ESQShrineState::Spent);
	}
}

void ASQShrine::StartChallenge(int32 TeamSize)
{
	const FSQShrineDef* Def = GetDef();
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!Def || !GS) return;
	const FVector Here = GetActorLocation();
	if (GS->Horde && !Def->Enemy.IsNone())
	{
		GS->Horde->SpawnEnemyNear(Def->Enemy, FVector2D(Here.X, Here.Y), FMath::Max(1, Def->Count + TeamSize - 1), 900.f);
	}
	bSecondWave = false;
	FailTimer = 0.f;
	Progress = 0.f;
	SetShrineState(ESQShrineState::Active);
	MulticastShrineFx(Here, Def->Color.ToFColor(true), 1);
	Alert(FString::Printf(TEXT("COMPLAINTS! Survive %.0f s near the %s for a free chest."), FMath::Max(1.f, Def->Duration), *Def->Name));
}

void ASQShrine::TickChallenge(float DeltaSeconds, const TArray<ASQCharacter*>& Reapers)
{
	const FSQShrineDef* Def = GetDef();
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!Def || !GS) return;
	const FVector Here = GetActorLocation();
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss)
	{
		SetShrineState(ESQShrineState::Spent); // the night ended (office / run over): no reward
		return;
	}
	const float Dur = FMath::Max(1.f, Def->Duration);
	Progress = FMath::Clamp(StateTime / Dur, 0.f, 1.f);
	if (!bSecondWave && StateTime >= Dur * 0.5f)
	{
		bSecondWave = true;
		int32 Team = 0;
		for (TActorIterator<ASQCharacter> It(World); It; ++It)
		{
			if (It->GetPlayerState()) ++Team;
		}
		if (GS->Horde && !Def->Enemy.IsNone())
		{
			GS->Horde->SpawnEnemyNear(Def->Enemy, FVector2D(Here.X, Here.Y), FMath::Max(1, Def->Count + Team - 1), 900.f);
		}
		Alert(TEXT("More complaints! Hold on near the bell."));
	}

	// Nobody standing near the bell for a while: the complaint is dismissed (no reward)
	const float Zone = Def->Value2 > 0.f ? Def->Value2 : 1600.f;
	const float ZoneSq = Zone * Zone;
	bool bAnyNear = false;
	for (const ASQCharacter* C : Reapers)
	{
		if (FVector::DistSquared2D(C->GetActorLocation(), Here) <= ZoneSq)
		{
			bAnyNear = true;
			break;
		}
	}
	FailTimer = bAnyNear ? 0.f : FailTimer + DeltaSeconds;
	if (FailTimer > 6.f)
	{
		MulticastShrineFx(Here, FColor(120, 120, 130), 3);
		Alert(TEXT("Complaint dismissed: nobody stayed near the bell."));
		SetShrineState(ESQShrineState::Spent);
		return;
	}

	if (StateTime >= Dur)
	{
		SpawnFreeChest(Here + FVector(Def->Radius + 150.f, Def->Radius + 150.f, 0.f) * 0.7071f);
		const int32 GoldEach = FMath::RoundToInt(Def->Value * NightGoldMult());
		PayEveryone(GoldEach);
		for (const ASQCharacter* Holder : Reapers)
		{
			if (FVector::DistSquared2D(Holder->GetActorLocation(), Here) <= ZoneSq) SQShrineCreditUse(Holder);
		}
		MulticastShrineFx(Here, FColor(255, 210, 90), 2);
		Alert(FString::Printf(TEXT("COMPLAINTS HANDLED: a free chest and +%d gold each!"), GoldEach));
		SetShrineState(ESQShrineState::Spent);
	}
}

void ASQShrine::SpawnCursedCopy()
{
	const FSQShrineDef* Def = GetDef();
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const USQData* D = USQData::Get(this);
	if (!Def || !GS || !D) return;
	// Weapons only hit ASQGameState::Boss, so only one copy can be up at a time (another lectern's copy: wait)
	if (IsValid(GS->Boss))
	{
		bWaitingForCopy = true;
		return;
	}
	bWaitingForCopy = false;

	// Tonight's legend (same rule as ASQGameMode::PickBossForNight: nights 1-2 by Bosses.csv Night, night 3 = the biome's legend)
	const FSQBossDef* BossDef = nullptr;
	if (GS->NightIndex >= 2 && GS->Arena)
	{
		const FSQBiomeDef* B = D->FindBiome(GS->Arena->BiomeId);
		if (B && !B->BossId.IsNone()) BossDef = D->FindBoss(B->BossId);
	}
	if (!BossDef) BossDef = D->FindBossForNight(FMath::Clamp(GS->NightIndex + 1, 1, 3));
	if (!BossDef)
	{
		SetShrineState(ESQShrineState::Spent);
		return;
	}

	const FVector Here = GetActorLocation();
	const float BX = static_cast<float>(Here.X) + 700.f;
	const float BY = static_cast<float>(Here.Y);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASQBoss* Copy = World->SpawnActor<ASQBoss>(ASQBoss::StaticClass(), FVector(BX, BY, ASQArena::GroundZAt(this, BX, BY) + 300.f), FRotator::ZeroRotator, Params);
	if (!Copy)
	{
		SetShrineState(ESQShrineState::Spent);
		return;
	}
	int32 Team = 0;
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		if (It->GetPlayerState()) ++Team;
	}
	// Same night-length scale the game mode uses for the real boss (Docs/Balance.md), times the copy's share
	const float NightScale = FMath::Clamp(D->Tune(TEXT("NightSeconds"), 360.f) / FMath::Max(1.f, D->Tune(TEXT("BalanceNightSeconds"), 360.f)), 0.1f, 4.f);
	const float Share = FMath::Clamp(Def->Value > 0.f ? Def->Value : 0.3f, 0.05f, 2.f);
	Copy->InitBoss(BossDef->Id, 0, FMath::Max(1, Team), Share * NightScale);
	GS->Boss = Copy; // weapons and the boss bar follow ASQGameState::Boss (cleared again when the copy dies or at midnight)
	CopyBoss = Copy;
	bCopyAlive = true;
	LastCopyLoc = Copy->GetActorLocation();
	MulticastShrineFx(Here, Def->Color.ToFColor(true), 1);
	Alert(FString::Printf(TEXT("A temp copy of %s rises! Beat it before midnight for a free chest."), *BossDef->Name));
}

void ASQShrine::TickCursed()
{
	const FSQShrineDef* Def = GetDef();
	UWorld* World = GetWorld();
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!Def || !GS) return;
	if (GS->Phase != ESQPhase::Night)
	{
		const bool bHadCopy = CopyBoss.IsValid();
		DismissCopy();
		if (bHadCopy) Alert(TEXT("Midnight: the temp legend's contract ran out."));
		SetShrineState(ESQShrineState::Spent);
		return;
	}
	if (bWaitingForCopy)
	{
		if (!IsValid(GS->Boss)) SpawnCursedCopy();
		return;
	}
	ASQBoss* Copy = CopyBoss.Get();
	if (Copy && Copy->Health > 0.f)
	{
		LastCopyLoc = Copy->GetActorLocation();
		Progress = Copy->MaxHealth > 0.f ? FMath::Clamp(Copy->Health / Copy->MaxHealth, 0.f, 1.f) : 0.f;
		return;
	}
	// The copy died (Health 0, about to vanish) or is gone
	if (Copy && GS->Boss.Get() == Copy) GS->Boss = nullptr;
	if (!bCopyAlive) return;
	bCopyAlive = false;
	if (Copy)
	{
		SpawnFreeChest(LastCopyLoc);
		const int32 GoldEach = FMath::RoundToInt(Def->Value2 * NightGoldMult());
		PayEveryone(GoldEach);
		MulticastShrineFx(LastCopyLoc, FColor(255, 210, 90), 2);
		Alert(FString::Printf(TEXT("Temp legend dismissed: a free chest and +%d gold each!"), GoldEach));
	}
	CopiesLeft--;
	if (CopiesLeft > 0) SpawnCursedCopy();
	else SetShrineState(ESQShrineState::Spent);
}

void ASQShrine::DismissCopy()
{
	ASQBoss* Copy = CopyBoss.Get();
	CopyBoss = nullptr;
	bCopyAlive = false;
	bWaitingForCopy = false;
	if (!Copy) return;
	UWorld* World = GetWorld();
	if (ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr)
	{
		if (GS->Boss.Get() == Copy) GS->Boss = nullptr; // never touches the real boss
	}
	MulticastShrineFx(Copy->GetActorLocation(), FColor(120, 120, 130), 3);
	Copy->Destroy();
}

void ASQShrine::MulticastShrineFx_Implementation(FVector_NetQuantize At, FColor Color, uint8 Kind)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UWorld* World = GetWorld();
	if (!World) return;
	const FLinearColor Lin(Color);
	switch (Kind)
	{
	case 0:
		if (SQFxRecipe::PlayShared(World, FName(TEXT("Collect")), At, Lin, 80.f, 1.6f)) break;
		SQFx::Burst(World, At, Lin, 14, 380.f, 0.7f, 12.f, 500.f);
		SQFx::Ring(World, At, Lin, 18, 420.f, 0.45f, 9.f);
		break;
	case 1:
		if (USoundBase* Sound = SQ::Sound(TEXT("LevelUp"))) UGameplayStatics::PlaySoundAtLocation(this, Sound, At, 0.8f);
		// Nov 19 2026: sigil, rising stars and a light column (FxRecipes.csv ShrinePulse); the built-in burst below when the motion kit is not imported
		if (SQFxRecipe::PlayShared(World, FName(TEXT("ShrinePulse")), At, Lin, 300.f)) break;
		SQFx::Flash(World, At + FVector(0.f, 0.f, 120.f), Lin, 220.f, 0.2f);
		SQFx::Burst(World, At + FVector(0.f, 0.f, 60.f), Lin, 30, 650.f, 1.f, 16.f, 400.f);
		SQFx::Shockwave(World, At + FVector(0.f, 0.f, 8.f), 600.f, Lin, 0.5f, 30.f);
		break;
	case 2:
		if (USoundBase* Sound = SQ::Sound(TEXT("Bank"))) UGameplayStatics::PlaySoundAtLocation(this, Sound, At, 1.f);
		if (SQFxRecipe::PlayShared(World, FName(TEXT("ChestOpen")), At, Lin, 260.f)) break;
		SQFx::Flash(World, At + FVector(0.f, 0.f, 80.f), Lin, 260.f, 0.25f);
		SQFx::Burst(World, At + FVector(0.f, 0.f, 40.f), Lin, 40, 700.f, 1.2f, 14.f, -700.f);
		SQFx::Ring(World, At + FVector(0.f, 0.f, 10.f), Lin, 28, 600.f, 0.6f, 12.f);
		break;
	default:
		SQFx::Burst(World, At + FVector(0.f, 0.f, 60.f), Lin, 16, 260.f, 0.8f, 14.f, 200.f);
		break;
	}
}

// ---------------------------------------------------------------- visuals

void ASQShrine::ApplyDefVisuals()
{
	if (bVisualsBuilt || GetNetMode() == NM_DedicatedServer) return;
	const FSQShrineDef* Def = GetDef();
	if (!Def) return;
	bVisualsBuilt = true;
	const ESQShrineType Type = ParseType(Def->Type);
	const FLinearColor Accent = Def->Color;
	const float H = Def->Height;
	const FVector Here = GetActorLocation();
	const float BodyYaw = FMath::Fmod(static_cast<float>(FMath::Abs(Here.X) * 0.13 + FMath::Abs(Here.Y) * 0.07), 360.f);

	if (UStaticMesh* Art = SQArt::Prop(Def->Model))
	{
		const float S = ASQArena::FitMeshHeight(Art, H);
		const FBoxSphereBounds Bo = Art->GetBounds();
		BodyMesh->SetStaticMesh(Art);
		BodyMesh->SetRelativeScale3D(FVector(S));
		BodyBaseZ = static_cast<float>(-(Bo.Origin.Z - Bo.BoxExtent.Z)) * S;
		BodyMesh->SetRelativeLocation(FVector(-Bo.Origin.X * S, -Bo.Origin.Y * S, BodyBaseZ));
		BodyLiveColor = SQArt::SoftTint(Accent, 0.35f);
		BodyMID = SQArt::TintMID(this, SQArt::PropTexture(), BodyLiveColor);
		SQArt::SetAllMaterials(BodyMesh, BodyMID);
	}
	else
	{
		// Primitive stand-ins (no art imported yet)
		FName Shape = TEXT("Cone");
		FVector Scale(0.9f, 0.9f, H / 100.f);
		switch (Type)
		{
		case ESQShrineType::Relic: Shape = TEXT("Cube"); Scale = FVector(1.4f, 0.8f, H / 100.f); break;
		case ESQShrineType::Greed: Shape = TEXT("Cube"); Scale = FVector(1.6f, 0.7f, H / 100.f); break;
		case ESQShrineType::Magnet: Shape = TEXT("Cylinder"); Scale = FVector(0.9f, 0.9f, H / 100.f); break;
		case ESQShrineType::Cursed: Shape = TEXT("Cube"); Scale = FVector(0.3f, 0.9f, H / 100.f); break;
		case ESQShrineType::Key: Shape = TEXT("Cylinder"); Scale = FVector(0.15f, 0.15f, H / 100.f); break;
		case ESQShrineType::Crypt: Shape = TEXT("Cube"); Scale = FVector(3.f, 2.4f, H / 100.f); break;
		default: break;
		}
		BodyMesh->SetStaticMesh(SQ::Mesh(Shape));
		BodyMesh->SetRelativeScale3D(Scale);
		BodyBaseZ = static_cast<float>(Scale.Z) * 50.f;
		BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, BodyBaseZ));
		BodyLiveColor = Accent * 0.55f;
		BodyMID = ASQArena::SurfaceMID(this, BodyLiveColor, Def->Glow * 0.3f);
		BodyMesh->SetMaterial(0, BodyMID);
	}
	BodyMesh->SetRelativeRotation(FRotator(0.f, BodyYaw, 0.f));
	if (GetShrineState() == ESQShrineState::Buried)
	{
		BodySink = -H * 0.92f; // starts underground (no sinking animation on spawn)
		BodyMesh->SetRelativeLocation(FVector(BodyMesh->GetRelativeLocation().X, BodyMesh->GetRelativeLocation().Y, BodyBaseZ + BodySink));
	}

	OrbMesh->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
	OrbMesh->SetRelativeScale3D(FVector(Type == ESQShrineType::Key ? 0.2f : 0.35f));
	OrbMesh->SetRelativeLocation(FVector(0.f, 0.f, H + 70.f));
	OrbMID = ASQArena::SurfaceMID(this, Accent, Def->Glow * 3.f);
	OrbMesh->SetMaterial(0, OrbMID);

	BeamMesh->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	BeamMesh->SetRelativeScale3D(FVector(0.18f, 0.18f, 30.f));
	BeamMesh->SetRelativeLocation(FVector(0.f, 0.f, 1500.f));
	BeamMID = ASQArena::SurfaceMID(this, Accent, 4.f);
	BeamMesh->SetMaterial(0, BeamMID);

	GlowLight->SetRelativeLocation(FVector(0.f, 0.f, H + 80.f));
	GlowLight->SetLightColor(Accent);
	GlowLight->SetAttenuationRadius(600.f + Def->Radius);

	PromptText->SetRelativeLocation(FVector(0.f, 0.f, H + 140.f));
	PromptText->SetTextRenderColor(Accent.ToFColor(true));

	RingLit->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
	RingLit->SetMaterial(0, ASQArena::SurfaceMID(this, Accent, 2.5f));
	RingDim->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
	RingDim->SetMaterial(0, ASQArena::SurfaceMID(this, Accent * 0.3f, 0.2f));
	BuildRing();

	UMaterialInstanceDynamic* StoneMID = ASQArena::SurfaceMID(this, FLinearColor(0.3f, 0.29f, 0.33f), 0.f);
	PedestalMIDs.Reset();
	for (int32 i = 0; i < 3; ++i)
	{
		if (PedestalBases.IsValidIndex(i) && PedestalBases[i])
		{
			PedestalBases[i]->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
			PedestalBases[i]->SetRelativeScale3D(FVector(0.55f, 0.55f, 0.7f));
			PedestalBases[i]->SetMaterial(0, StoneMID);
		}
		UMaterialInstanceDynamic* PedMID = ASQArena::SurfaceMID(this, FLinearColor::White, 3.f);
		PedestalMIDs.Add(PedMID);
		if (PedestalOrbs.IsValidIndex(i) && PedestalOrbs[i])
		{
			PedestalOrbs[i]->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
			PedestalOrbs[i]->SetRelativeScale3D(FVector(0.4f));
			PedestalOrbs[i]->SetMaterial(0, PedMID);
		}
	}
	LastPedestalText.Init(FString(), 3);
	RingLitShown = -1;
}

void ASQShrine::BuildRing()
{
	RingSegments.Reset();
	const FSQShrineDef* Def = GetDef();
	if (!Def) return;
	const float R = Def->Radius;
	const FVector Here = GetActorLocation();
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	bRingGrounded = Arena && Arena->HasTerrain();
	const float SegLen = 2.f * PI * R / ShrineRingSegments;
	for (int32 i = 0; i < ShrineRingSegments; ++i)
	{
		const float Ang = 2.f * PI * static_cast<float>(i) / ShrineRingSegments;
		const float RX = FMath::Cos(Ang) * R;
		const float RY = FMath::Sin(Ang) * R;
		const float RZ = bRingGrounded ? Arena->GroundZ(static_cast<float>(Here.X) + RX, static_cast<float>(Here.Y) + RY) - static_cast<float>(Here.Z) + 6.f : 6.f;
		RingSegments.Add(FTransform(FRotator(0.f, FMath::RadiansToDegrees(Ang) + 90.f, 0.f), FVector(RX, RY, RZ), FVector(SegLen / 100.f * 0.8f, 0.12f, 0.05f)));
	}
	RingLitShown = -1;
}

FString ASQShrine::BuildPromptText(const FSQShrineDef& Def) const
{
	const ESQShrineType Type = ParseType(Def.Type);
	const int32 Pct = FMath::Clamp(FMath::RoundToInt(Progress * 100.f), 0, 100);
	switch (GetShrineState())
	{
	case ESQShrineState::Buried:
		return Progress > 0.f ? FString::Printf(TEXT("%s\nDIGGING %d%%"), *Def.Prompt, Pct) : Def.Prompt;
	case ESQShrineState::Locked:
		return Def.Prompt;
	case ESQShrineState::Idle:
	{
		if (Type == ESQShrineType::Crypt) return FString::Printf(TEXT("%s\nUNLOCKED: WALK IN"), *Def.Name);
		if (Type == ESQShrineType::Cursed)
		{
			const UWorld* World = GetWorld();
			const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
			const USQData* D = USQData::Get(this);
			const float MinLeft = D ? D->Tune(TEXT("ShrineCursedMinSeconds"), 45.f) : 45.f;
			if (GS && (GS->Phase != ESQPhase::Night || GS->PhaseTimeLeft <= MinLeft)) return FString::Printf(TEXT("%s\n(closed until the next night)"), *Def.Name);
		}
		if (Type == ESQShrineType::Key) return Def.Prompt;
		return FString::Printf(TEXT("%s\n%s"), *Def.Name, *Def.Prompt);
	}
	case ESQShrineState::Charging:
		return FString::Printf(TEXT("%s\n%d%%  (more reapers = faster)"), *Def.Name, Pct);
	case ESQShrineState::Choosing:
	{
		if (Type == ESQShrineType::Relic) return TEXT("TAKE ONE ITEM: STAND ON IT");
		const USQData* D = USQData::Get(this);
		const float PickSeconds = D ? D->Tune(TEXT("ShrinePickSeconds"), 25.f) : 25.f;
		return FString::Printf(TEXT("PICK A RAISE: STAND ON A PEDESTAL\n%d to pick, %.0f s"), static_cast<int32>(PicksLeft), FMath::CeilToFloat(Progress * PickSeconds));
	}
	case ESQShrineState::Active:
		if (Type == ESQShrineType::Challenge)
		{
			return FString::Printf(TEXT("SURVIVE NEAR THE BELL: %.0f s"), FMath::CeilToFloat(FMath::Max(1.f, Def.Duration) * (1.f - Progress)));
		}
		if (Type == ESQShrineType::Cursed) return FString::Printf(TEXT("BEAT THE TEMP LEGEND (%d%% left)"), Pct);
		return TEXT("COLLECTING...");
	default:
		return FString();
	}
}

void ASQShrine::TickVisuals(float DeltaSeconds)
{
	const FSQShrineDef* Def = GetDef();
	if (!Def) return;
	if (!bVisualsBuilt) ApplyDefVisuals();
	if (!bVisualsBuilt) return;
	UWorld* World = GetWorld();
	VisualClock += DeltaSeconds;
	const ESQShrineState S = GetShrineState();
	const ESQShrineType Type = ParseType(Def->Type);
	const bool bWorking = S == ESQShrineState::Charging || S == ESQShrineState::Choosing || S == ESQShrineState::Active;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(VisualClock * (bWorking ? 6.f : 2.f));
	const FVector Here = GetActorLocation();

	// The terrain may not have been built yet when this shrine replicated in: ground the ring once it exists
	if (!bRingGrounded)
	{
		const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
		if (GS && GS->Arena && GS->Arena->HasTerrain()) BuildRing();
	}

	// Buried shrines rise out of the ground once dug up; the key bobs and spins
	const float SinkTarget = S == ESQShrineState::Buried ? -Def->Height * 0.92f : 0.f;
	if (!FMath::IsNearlyEqual(BodySink, SinkTarget, 0.5f) || Type == ESQShrineType::Key)
	{
		BodySink = FMath::FInterpTo(BodySink, SinkTarget, DeltaSeconds, 2.5f);
		const FVector Rel = BodyMesh->GetRelativeLocation();
		const float Bob = (Type == ESQShrineType::Key && S != ESQShrineState::Spent) ? 25.f + 12.f * FMath::Sin(VisualClock * 2.5f) : 0.f;
		BodyMesh->SetRelativeLocation(FVector(Rel.X, Rel.Y, BodyBaseZ + BodySink + Bob));
		if (Type == ESQShrineType::Key) BodyMesh->SetRelativeRotation(FRotator(0.f, FMath::Fmod(VisualClock * 90.f, 360.f), 20.f));
	}
	if (Type == ESQShrineType::Key) BodyMesh->SetVisibility(S != ESQShrineState::Spent);

	// Glow, orb, beam, light
	float GlowNow = Def->Glow * (0.6f + 0.4f * Pulse);
	if (bWorking) GlowNow = Def->Glow * (1.5f + Pulse);
	if (S == ESQShrineState::Spent || S == ESQShrineState::Buried) GlowNow = 0.f;
	if (S == ESQShrineState::Locked) GlowNow = 0.25f;
	if (BodyMID) BodyMID->SetScalarParameterValue(TEXT("Glow"), GlowNow * 0.5f);
	const bool bSpentNow = S == ESQShrineState::Spent;
	if (bSpentNow != bSpentLookShown && BodyMID)
	{
		bSpentLookShown = bSpentNow;
		BodyMID->SetVectorParameterValue(TEXT("Color"), bSpentNow ? FLinearColor(0.3f, 0.3f, 0.32f) : BodyLiveColor);
	}
	if (OrbMID) OrbMID->SetScalarParameterValue(TEXT("Glow"), GlowNow * 3.f);
	const bool bShowOrb = S != ESQShrineState::Spent && S != ESQShrineState::Buried && Type != ESQShrineType::Key;
	OrbMesh->SetVisibility(bShowOrb);
	if (bShowOrb) OrbMesh->SetRelativeLocation(FVector(0.f, 0.f, Def->Height + 70.f + 15.f * FMath::Sin(VisualClock * 1.7f)));
	BeamMesh->SetVisibility(bWorking);
	if (bWorking && BeamMID) BeamMID->SetScalarParameterValue(TEXT("Glow"), 2.f + 3.f * Pulse);
	const bool bLight = GlowNow > 0.01f;
	GlowLight->SetVisibility(bLight);
	if (bLight) GlowLight->SetIntensity(2500.f * (GlowNow + 0.5f));

	// Ring: the trigger area, lit to show progress
	const bool bRingType = Type != ESQShrineType::Key && Type != ESQShrineType::Crypt;
	const bool bShowRing = bRingType && (S == ESQShrineState::Idle || bWorking);
	RingLit->SetVisibility(bShowRing);
	RingDim->SetVisibility(bShowRing);
	if (bShowRing)
	{
		float Lit = 0.f;
		if (S == ESQShrineState::Charging || S == ESQShrineState::Choosing) Lit = Progress;
		else if (S == ESQShrineState::Active) Lit = (Type == ESQShrineType::Cursed) ? Progress : 1.f - Progress;
		const int32 LitCount = FMath::Clamp(FMath::RoundToInt(Lit * ShrineRingSegments), 0, RingSegments.Num());
		if (LitCount != RingLitShown)
		{
			RingLitShown = LitCount;
			ScratchLit.Reset();
			ScratchDim.Reset();
			for (int32 i = 0; i < RingSegments.Num(); ++i)
			{
				if (i < LitCount) ScratchLit.Add(RingSegments[i]);
				else ScratchDim.Add(RingSegments[i]);
			}
			RingLit->ClearInstances();
			if (ScratchLit.Num() > 0) RingLit->AddInstances(ScratchLit, false, false);
			RingDim->ClearInstances();
			if (ScratchDim.Num() > 0) RingDim->AddInstances(ScratchDim, false, false);
		}
	}

	// Local camera: text faces it, and fades out with distance (secrets only show up close)
	FVector ViewLoc = FVector::ZeroVector;
	const bool bHaveView = SQShrineViewLocation(World, ViewLoc);
	const float ViewDist = bHaveView ? static_cast<float>(FVector::Dist(ViewLoc, Here)) : 0.f;
	const bool bSecretish = S == ESQShrineState::Buried || Type == ESQShrineType::Key;
	const float TextRange = bSecretish ? 1600.f : 3200.f;

	// Pedestals (Choosing)
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bShowPed = S == ESQShrineState::Choosing && i < OfferIds.Num();
		UStaticMeshComponent* PedBase = PedestalBases.IsValidIndex(i) ? PedestalBases[i].Get() : nullptr;
		UStaticMeshComponent* PedOrb = PedestalOrbs.IsValidIndex(i) ? PedestalOrbs[i].Get() : nullptr;
		UTextRenderComponent* PedText = PedestalTexts.IsValidIndex(i) ? PedestalTexts[i].Get() : nullptr;
		if (PedBase) PedBase->SetVisibility(bShowPed);
		if (PedOrb) PedOrb->SetVisibility(bShowPed);
		if (PedText) PedText->SetVisibility(bShowPed && ViewDist < TextRange);
		if (!bShowPed) continue;
		const FVector Ped = PedestalLocation(i);
		const uint8 Rarity = OfferRarity.IsValidIndex(i) ? OfferRarity[i] : 0;
		const FLinearColor RarColor = SQ::RarityColor(Rarity);
		if (PedBase) PedBase->SetWorldLocation(Ped + FVector(0.f, 0.f, 35.f));
		if (PedOrb) PedOrb->SetWorldLocation(Ped + FVector(0.f, 0.f, 105.f + 10.f * FMath::Sin(VisualClock * 2.2f + static_cast<float>(i))));
		if (PedestalMIDs.IsValidIndex(i) && PedestalMIDs[i])
		{
			PedestalMIDs[i]->SetVectorParameterValue(TEXT("Color"), RarColor);
			PedestalMIDs[i]->SetScalarParameterValue(TEXT("Glow"), 2.5f + 1.5f * Pulse);
		}
		if (PedText)
		{
			FString Label;
			if (Type == ESQShrineType::Relic)
			{
				const USQData* D = USQData::Get(this);
				const FSQUpgradeDef* U = D ? D->FindUpgrade(OfferIds[i]) : nullptr;
				Label = FString::Printf(TEXT("%s\n%s"), U ? *U->Name : *OfferIds[i].ToString(), *SQ::RarityName(Rarity));
			}
			else
			{
				const float Value = OfferValues.IsValidIndex(i) ? OfferValues[i] : 0.f;
				Label = FString::Printf(TEXT("%s\n%s"), *SQ::StatText(OfferIds[i], Value), *SQ::RarityName(Rarity));
			}
			if (LastPedestalText.IsValidIndex(i) && LastPedestalText[i] != Label)
			{
				LastPedestalText[i] = Label;
				PedText->SetText(FText::FromString(Label));
				PedText->SetTextRenderColor(RarColor.ToFColor(true));
			}
			PedText->SetWorldLocation(Ped + FVector(0.f, 0.f, 170.f));
			if (bHaveView) SQShrineFaceCamera(PedText, ViewLoc);
		}
	}

	// Prompt
	const FString Prompt = BuildPromptText(*Def);
	const bool bShowPrompt = !Prompt.IsEmpty() && bHaveView && ViewDist < TextRange;
	PromptText->SetVisibility(bShowPrompt);
	if (bShowPrompt)
	{
		if (Prompt != LastPrompt)
		{
			LastPrompt = Prompt;
			PromptText->SetText(FText::FromString(Prompt));
		}
		const float Lift = S == ESQShrineState::Buried ? 60.f : Def->Height + 140.f;
		PromptText->SetRelativeLocation(FVector(0.f, 0.f, Lift));
		SQShrineFaceCamera(PromptText, ViewLoc);
	}

	// Sparkle: charging motes rising from the ring, gold glints over a buried shrine
	FxClock -= DeltaSeconds;
	if (FxClock <= 0.f && World && bHaveView && ViewDist < 5000.f)
	{
		if (S == ESQShrineState::Charging)
		{
			FxClock = 0.15f;
			const float Ang = FMath::FRandRange(0.f, 2.f * PI);
			const FVector At = Here + FVector(FMath::Cos(Ang) * Def->Radius, FMath::Sin(Ang) * Def->Radius, 10.f);
			SQFx::Burst(World, At, Def->Color, 3, 120.f, 0.8f, 8.f, 600.f);
		}
		else if (S == ESQShrineState::Buried)
		{
			FxClock = 1.4f;
			SQFx::Burst(World, Here + FVector(0.f, 0.f, 15.f), FLinearColor(1.f, 0.8f, 0.3f), 4, 90.f, 0.6f, 6.f, 250.f);
		}
		else if (S == ESQShrineState::Locked)
		{
			FxClock = 2.f;
			SQFx::Burst(World, Here + FVector(0.f, 0.f, 60.f), FLinearColor(0.6f, 0.2f, 0.25f), 3, 60.f, 0.8f, 6.f, 100.f);
		}
		else
		{
			FxClock = 0.5f;
		}
	}
}
