// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQChest.h"
#include "SQInput.h"
#include "SQFxRecipe.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "SQArena.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQInteractCue.h"
#include "SQPlayerState.h"

ASQChest::ASQChest()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true; // a couple of dozen at most; the HUD shows prices for the ones near you
	SetNetUpdateFrequency(4.f);

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
	// Engine basic shapes are 100 cm: a chunky 90 x 60 x 50 cm chest, its lid hinged at the back edge, a glowing ring under it
	Body = MakePart(TEXT("Body"), TEXT("Cube"), FVector(0.f, 0.f, 25.f), FVector(0.9f, 0.6f, 0.5f));
	Band = MakePart(TEXT("Band"), TEXT("Cube"), FVector(0.f, 0.f, 30.f), FVector(0.94f, 0.12f, 0.54f));
	Lid = MakePart(TEXT("Lid"), TEXT("Cube"), FVector(0.f, 0.f, 58.f), FVector(0.95f, 0.65f, 0.16f));
	Halo = MakePart(TEXT("Halo"), TEXT("Cylinder"), FVector(0.f, 0.f, 2.f), FVector(1.5f, 1.5f, 0.02f));
	Halo->SetCastShadow(false);
	Cue = CreateDefaultSubobject<USQInteractCue>(TEXT("Cue"));
	Cue->SetupAttachment(Root);
}

void ASQChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQChest, bFree);
	DOREPLIFETIME(ASQChest, bOpened);
}

ASQChest* ASQChest::SpawnChest(UWorld* World, const FVector& Location, bool bInFree)
{
	if (!World || World->GetNetMode() == NM_Client) return nullptr;
	const float GroundZ = ASQArena::GroundZAt(World, static_cast<float>(Location.X), static_cast<float>(Location.Y));
	const FVector OnGround(Location.X, Location.Y, World->GetGameState<ASQGameState>() && World->GetGameState<ASQGameState>()->Arena ? GroundZ : Location.Z);
	const FTransform SpawnXform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), OnGround);
	ASQChest* Chest = World->SpawnActorDeferred<ASQChest>(ASQChest::StaticClass(), SpawnXform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Chest) return nullptr;
	Chest->bFree = bInFree;
	Chest->FinishSpawning(SpawnXform);
	return Chest;
}

void ASQChest::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();
}

void ASQChest::OnRep_Look()
{
	ApplyLook();
}

void ASQChest::ApplyLook()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (bLookApplied && bLookFree == bFree) return;
	bLookApplied = true;
	bLookFree = bFree;
	// Paid: dark coffin-wood with brass bands. Free: gilded, glowing.
	const FLinearColor Wood = bFree ? FLinearColor(0.85f, 0.62f, 0.12f) : FLinearColor(0.16f, 0.09f, 0.05f);
	const FLinearColor Trim = bFree ? FLinearColor(1.f, 0.9f, 0.45f) : FLinearColor(0.75f, 0.55f, 0.2f);
	Body->SetMaterial(0, SQ::ColorMID(this, Wood));
	Lid->SetMaterial(0, SQ::ColorMID(this, Wood * 1.15f));
	BandMID = SQ::ColorMID(this, Trim);
	Band->SetMaterial(0, BandMID);
	HaloMID = SQ::ColorMID(this, bFree ? FLinearColor(1.f, 0.8f, 0.25f) : FLinearColor(1.f, 0.6f, 0.15f));
	Halo->SetMaterial(0, HaloMID);
	if (BandMID) BandMID->SetScalarParameterValue(TEXT("Glow"), bFree ? 1.2f : 0.3f);
	// The shared cue replaces the old flat halo disc (kept as a faint shadow-catcher under the chest)
	Halo->SetRelativeScale3D(FVector(1.05f, 1.05f, 0.02f));
	if (Cue)
	{
		Cue->Setup(bFree ? ESQCueKind::FreeChest : ESQCueKind::Chest, 115.f, 125.f, *SQInput::TranslateInline(bFree ? TEXT("FREE CHEST  [F]") : TEXT("CHEST  [F]")));
		Cue->SetUsed(bOpened);
	}
}

void ASQChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) return;
	LookTime += DeltaSeconds;
	if (!bOpened) bOpenFxArmed = true;
	else if (bOpenFxArmed)
	{
		// Nov 19 2026: light pillar, coins of light and a ground flash as the lid goes up (FxRecipes.csv ChestOpen)
		bOpenFxArmed = false;
		SQFxRecipe::PlayShared(GetWorld(), FName(TEXT("ChestOpen")), GetActorLocation(), bFree ? FLinearColor(1.f, 0.85f, 0.35f) : FLinearColor(1.f, 0.78f, 0.23f), 220.f);
	}
	// The lid swings open once opened; the halo breathes until then
	LidOpen = FMath::FInterpTo(LidOpen, bOpened ? 1.f : 0.f, DeltaSeconds, 6.f);
	Lid->SetRelativeLocation(FVector(-30.f * LidOpen, 0.f, 58.f + 25.f * LidOpen));
	Lid->SetRelativeRotation(FRotator(-100.f * LidOpen, 0.f, 0.f));
	if (HaloMID)
	{
		const float Breath = 0.5f + 0.5f * FMath::Sin(LookTime * (bFree ? 4.f : 2.f));
		HaloMID->SetScalarParameterValue(TEXT("Glow"), bOpened ? 0.f : (bFree ? 0.5f : 0.2f) * (0.5f + 0.5f * Breath));
	}
	Halo->SetVisibility(!bOpened);
	if (Cue) Cue->SetUsed(bOpened);
}

float ASQChest::KeyChance(const ASQCharacter* Reaper)
{
	const USQData* D = Reaper ? USQData::Get(Reaper) : nullptr;
	if (!D) return 0.f;
	float KeyStat = 0.f;
	static const FName ChestKeyStat(TEXT("ChestKey"));
	for (const FSQOwned& Item : Reaper->Owned)
	{
		const FSQUpgradeDef* Def = D->FindUpgrade(Item.Id);
		if (!Def || Def->Kind != ESQUpgradeKind::Relic) continue;
		// Relic Power = total picks x rarity multiplier (FSQOwned), like every other relic stat
		if (Def->Stat == ChestKeyStat) KeyStat += Def->StatPerLevel * Item.Power;
		if (Def->Stat2 == ChestKeyStat) KeyStat += Def->Stat2PerLevel * Item.Power;
	}
	KeyStat = FMath::Max(0.f, KeyStat);
	return KeyStat / (KeyStat + 1.f); // Megabonk's Key: diminishing, never 100%
}

bool ASQChest::TryOpen(ASQPlayerState* Opener)
{
	if (!HasAuthority() || bOpened || !Opener) return false;
	ASQCharacter* Reaper = Opener->GetPawn<ASQCharacter>();
	const ASQGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	if (!Reaper || Reaper->bDowned || !GS) return false;

	bool bKeyOpen = false;
	if (!bFree)
	{
		const int32 Price = GS->ChestPriceFor(Opener);
		if (Opener->Gold < Price)
		{
			if (!Opener->IsABot())
			{
				Opener->ClientNotice(FString::Printf(TEXT("NOT ENOUGH GOLD: %d NEEDED"), Price), FString::Printf(TEXT("You have %d. Reap more - coins drop from the horde, elites and minibosses."), Opener->Gold), 0, 0);
			}
			return false;
		}
		// Skeleton Key: a free open (no gold, no price rise). Rolled only when you could pay, so spamming E can't fish for it.
		bKeyOpen = FMath::FRand() < KeyChance(Reaper);
		if (!bKeyOpen)
		{
			if (!Opener->SpendGold(Price)) return false;
			Opener->ChestsOpened++;
		}
	}
	bOpened = true;
	ApplyLook();
	SetLifeSpan(3.f);
	GrantChestRoll(Opener, bFree, bKeyOpen);
	// Personal loot (Nov 23 2026): a FREE chest pays every reaper on the crew a roll of their own, so nobody races for it.
	// Paid chests stay personal (your purse, your price; the map already places more of them per extra reaper).
	const USQData* ShareData = USQData::Get(this);
	if (bFree && GS && (!ShareData || ShareData->Tune(TEXT("ChestFreeForAll"), 1.f) > 0.5f))
	{
		for (APlayerState* EachPS : GS->PlayerArray)
		{
			ASQPlayerState* Mate = Cast<ASQPlayerState>(EachPS);
			if (Mate && Mate != Opener && Mate->GetPawn<ASQCharacter>()) GrantChestRoll(Mate, true, false);
		}
	}
	return true;
}

void ASQChest::GrantChestRoll(ASQPlayerState* PS, bool bFreeOpen, bool bKeyOpen)
{
	if (!PS || !PS->HasAuthority()) return;
	ASQCharacter* Reaper = PS->GetPawn<ASQCharacter>();
	const USQData* D = USQData::Get(PS);
	if (!Reaper || !D) return;

	// One ITEM (items/tomes pass: chests never give weapons or tomes). Items have no slots and stack, so the pool is only
	// empty if every item is at its MaxStacks or quest-locked: then gold + a heal instead.
	TArray<const FSQUpgradeDef*> Pool;
	TArray<float> Weights;
	for (const FSQUpgradeDef& Def : D->Upgrades)
	{
		if (!Def.IsItem() || !Reaper->CanTakeUpgrade(Def) || PS->IsItemLocked(Def.Id)) continue;
		Pool.Add(&Def);
		Weights.Add(FMath::Max(0.1f, Def.Weight));
	}

	const FString Header = bKeyOpen ? FString(TEXT("SKELETON KEY! FREE CHEST")) : (bFreeOpen ? FString(TEXT("FREE CHEST")) : FString(TEXT("CHEST OPENED")));
	FString Sub;
	uint8 Rarity = 0;
	if (Pool.Num() > 0)
	{
		float Total = 0.f;
		for (const float Each : Weights) Total += Each;
		float Roll = FMath::FRandRange(0.f, Total);
		int32 Chosen = Pool.Num() - 1;
		for (int32 PoolIdx = 0; PoolIdx < Pool.Num(); ++PoolIdx)
		{
			Roll -= Weights[PoolIdx];
			if (Roll <= 0.f) { Chosen = PoolIdx; break; }
		}
		const FSQUpgradeDef* Picked = Pool[Chosen];
		Rarity = PS->RollRarity(Reaper->GetLuck() + FMath::Max(0.f, D->Tune(TEXT("ChestLuckBonus"), 0.25f)));
		const int32 StacksBefore = Reaper->GetUpgradeLevel(Picked->Id);
		const FString StackNote = StacksBefore > 0 ? FString::Printf(TEXT("  [x%d]"), StacksBefore + 1) : FString();
		Sub = FString::Printf(TEXT("%s (%s): %s%s"), *Picked->Name, *SQ::RarityName(Rarity), *SQ::DescribeUpgradeGain(*Picked, StacksBefore, Rarity), *StackNote);
		Reaper->ApplyUpgrade(Picked->Id, Rarity);
	}
	else
	{
		// Every item at its stack limit: the SQ_Gold filler amount + a full heal instead
		const FSQUpgradeDef* GoldCard = D->FindUpgrade(TEXT("SQ_Gold"));
		if (GoldCard) Reaper->ApplyUpgrade(GoldCard->Id, 0);
		if (!Reaper->bDowned) Reaper->Health = Reaper->Stats.MaxHealth;
		Sub = TEXT("Every item is stacked to its limit: gold and a full heal instead.");
	}

	PS->CreditQuest(TEXT("ChestsOpened"), 1);
	if (bFreeOpen || bKeyOpen) PS->CreditQuest(TEXT("FreeChests"), 1);
	if (Rarity >= 3) PS->CreditQuest(TEXT("LegendaryChest"), 1);
	if (!PS->IsABot()) PS->ClientNotice(Header, Sub, 2, Rarity);
}
