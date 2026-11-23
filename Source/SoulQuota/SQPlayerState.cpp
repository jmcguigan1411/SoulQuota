// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQGameMode.h"
#include "SQGameState.h"
#include "SQSave.h"
#include "SQQuests.h"
#include "SQMission.h"
#include "SQAugments.h"
#include "GameFramework/PlayerController.h"

void ASQPlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		const USQData* D = USQData::Get(this);
		RerollsLeft = FMath::Max(0, FMath::RoundToInt(D ? D->Tune(TEXT("RerollsPerRun"), 2.f) : 2.f));
	}
}

void ASQPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQPlayerState, ReaperId);
	DOREPLIFETIME(ASQPlayerState, Offers);
	DOREPLIFETIME(ASQPlayerState, OfferRarity);
	DOREPLIFETIME(ASQPlayerState, OfferSerial);
	DOREPLIFETIME(ASQPlayerState, PendingLevelUps);
	DOREPLIFETIME(ASQPlayerState, RerollsLeft);
	DOREPLIFETIME(ASQPlayerState, bInPickPause);
	DOREPLIFETIME(ASQPlayerState, bShopReady);
	DOREPLIFETIME(ASQPlayerState, Kills);
	DOREPLIFETIME(ASQPlayerState, Gold);
	DOREPLIFETIME(ASQPlayerState, ChestsOpened);
	DOREPLIFETIME(ASQPlayerState, AugmentOffers);
	DOREPLIFETIME(ASQPlayerState, OwnedAugments);
	DOREPLIFETIME(ASQPlayerState, bAugmentOffer);
	DOREPLIFETIME(ASQPlayerState, PendingAugments);
}

void ASQPlayerState::ClientInitialize(AController* C)
{
	Super::ClientInitialize(C);
	// A remote client's own state: tell the server our Death's Office ranks (the host sends from ASQGameMode::PostLogin)
	const APlayerController* OwnerPC = Cast<APlayerController>(C);
	if (OwnerPC && OwnerPC->IsLocalController() && !HasAuthority()) SendMetaRanks();
}

void ASQPlayerState::GrantLevelUp()
{
	if (!HasAuthority()) return;
	PendingLevelUps++;
	if (Offers.Num() == 0)
	{
		GenerateOffers();
	}
}

void ASQPlayerState::GenerateOffers()
{
	// Augment sets first: they are rarer and bigger than a level-up card
	if (PendingAugments > 0) RollAugmentOffers(TArray<FName>());
	else RollOffers(TArray<FName>());
}

uint8 ASQPlayerState::RollRarity(float Luck) const
{
	// Base odds (percent) from Tuning.csv, luck pushes weight from Common up the ladder
	const USQData* D = USQData::Get(this);
	const float LuckAmount = FMath::Max(0.f, Luck);
	const float RareBase = D ? D->Tune(TEXT("RarityRare"), 27.f) : 27.f;
	const float EpicBase = D ? D->Tune(TEXT("RarityEpic"), 10.f) : 10.f;
	const float LegendBase = D ? D->Tune(TEXT("RarityLegendary"), 3.f) : 3.f;
	float RarityWeights[4];
	RarityWeights[0] = FMath::Max(0.f, 100.f - RareBase - EpicBase - LegendBase);
	RarityWeights[1] = FMath::Max(0.f, RareBase) * (1.f + 0.6f * LuckAmount);
	RarityWeights[2] = FMath::Max(0.f, EpicBase) * (1.f + 1.0f * LuckAmount);
	RarityWeights[3] = FMath::Max(0.f, LegendBase) * (1.f + 1.5f * LuckAmount);

	float RarityTotal = 0.f;
	for (const float Each : RarityWeights) RarityTotal += Each;
	if (RarityTotal <= 0.f) return 0;

	float RarityRoll = FMath::FRandRange(0.f, RarityTotal);
	for (uint8 Tier = 0; Tier < 4; ++Tier)
	{
		RarityRoll -= RarityWeights[Tier];
		if (RarityRoll <= 0.f) return Tier;
	}
	return 3;
}

int32 ASQPlayerState::BestOfferIndex() const
{
	// Rarest real card first; filler cards (gold / XP / heal) only when nothing else is on the table
	const USQData* BestData = USQData::Get(this);
	auto CardScore = [this, BestData](int32 CardIdx)
	{
		const FSQUpgradeDef* CardDef = BestData ? BestData->FindUpgrade(Offers[CardIdx]) : nullptr;
		const int32 Tier = OfferRarity.IsValidIndex(CardIdx) ? static_cast<int32>(OfferRarity[CardIdx]) : 0;
		return (!CardDef || CardDef->IsFallback()) ? Tier - 10 : Tier;
	};
	int32 Best = INDEX_NONE;
	for (int32 OfferIdx = 0; OfferIdx < Offers.Num(); ++OfferIdx)
	{
		if (Best == INDEX_NONE || CardScore(OfferIdx) > CardScore(Best)) Best = OfferIdx;
	}
	return Best;
}

void ASQPlayerState::RollOffers(const TArray<FName>& Avoid)
{
	Offers.Reset();
	OfferRarity.Reset();
	AugmentOffers.Reset();
	bAugmentOffer = false;
	OfferSerial++;
	OfferIdleSeconds = 0.f;
	ASQCharacter* C = GetPawn<ASQCharacter>();
	const USQData* D = USQData::Get(this);
	if (!C || !D || PendingLevelUps <= 0) return;

	// Level-up cards: weapons, tomes and evolutions only (items come from chests / shrines). CanTakeUpgrade applies the
	// slot rules, so once both rows of slots are full only owned weapons / tomes (and ready evolutions) are offered.
	TArray<FName> Pool;
	TArray<float> Weights;
	for (const FSQUpgradeDef& U : D->Upgrades)
	{
		if (U.IsItem() || U.IsFallback()) continue;
		if (C->CanTakeUpgrade(U) && !IsItemLocked(U.Id))
		{
			Pool.Add(U.Id);
			// a reroll strongly prefers cards that weren't just on the table
			Weights.Add(FMath::Max(0.1f, U.Weight) * (Avoid.Contains(U.Id) ? 0.15f : 1.f));
		}
	}

	const float Luck = C->GetLuck();
	for (int32 Pick = 0; Pick < 3 && Pool.Num() > 0; ++Pick)
	{
		float Total = 0.f;
		for (float W : Weights) Total += W;
		float Roll = FMath::FRandRange(0.f, Total);
		int32 Chosen = Pool.Num() - 1;
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			Roll -= Weights[i];
			if (Roll <= 0.f) { Chosen = i; break; }
		}
		const FSQUpgradeDef* Def = D->FindUpgrade(Pool[Chosen]);
		const bool bEvolution = Def && Def->Kind == ESQUpgradeKind::Evolution;
		Offers.Add(Pool[Chosen]);
		OfferRarity.Add(bEvolution ? static_cast<uint8>(3) : RollRarity(Luck));
		Pool.RemoveAt(Chosen);
		Weights.RemoveAt(Chosen);
	}

	// Nearly everything maxed: fill the empty card spots with filler cards (Upgrades.csv Category Fallback rows:
	// gold, team XP, a heal), each at a normal rarity roll. Bots skip the XP card (no nested level-up chains).
	for (const FSQUpgradeDef& Filler : D->Upgrades)
	{
		if (Offers.Num() >= 3) break;
		if (!Filler.IsFallback() || Offers.Contains(Filler.Id)) continue;
		if (IsABot() && Filler.Id == TEXT("SQ_XP")) continue;
		Offers.Add(Filler.Id);
		OfferRarity.Add(RollRarity(Luck));
	}
	if (Offers.Num() == 0)
	{
		Offers.Add(TEXT("SQ_Heal"));
		OfferRarity.Add(0);
	}

	if (IsABot())
	{
		ChooseOffer(FMath::RandRange(0, Offers.Num() - 1));
	}
	else if (bAutoPick)
	{
		// "Auto-pick level-ups" setting: the rarest card, straight away (no pick screen, no strip)
		ChooseOffer(FMath::Max(0, BestOfferIndex()));
	}
}

void ASQPlayerState::AfterOfferResolved()
{
	PendingLevelUps = FMath::Max(0, PendingLevelUps - 1);
	Offers.Reset();
	OfferRarity.Reset();
	OfferSerial++;
	if (PendingLevelUps > 0 || PendingAugments > 0)
	{
		GenerateOffers(); // the next set appears straight away (the pause holds)
	}
}

// ============================================================ augments (server)

void ASQPlayerState::GrantAugmentPick(uint8 Source)
{
	if (!HasAuthority()) return;
	PendingAugments = FMath::Min(PendingAugments + 1, 20);
	PendingAugmentSources.Add(Source);
	if (Offers.Num() == 0 && GetPawn()) GenerateOffers();
}

void ASQPlayerState::RollAugmentOffers(const TArray<FName>& Avoid)
{
	Offers.Reset();
	OfferRarity.Reset();
	AugmentOffers.Reset();
	OfferSerial++;
	OfferIdleSeconds = 0.f;
	bAugmentOffer = false;
	if (PendingAugments <= 0 || !GetPawn()) return;
	const ESQAugmentSource Source = PendingAugmentSources.Num() > 0 ? static_cast<ESQAugmentSource>(PendingAugmentSources[0]) : ESQAugmentSource::Level;
	TArray<FName> Ids;
	TArray<uint8> Tiers;
	USQAugments::RollOffer(this, Source, Avoid, Ids, Tiers);
	if (Ids.Num() == 0)
	{
		// Nothing left to offer (every augment owned): the pick is lost, carry on with the level-up cards
		PendingAugments = 0;
		PendingAugmentSources.Reset();
		if (PendingLevelUps > 0) RollOffers(TArray<FName>());
		return;
	}
	AugmentOffers = Ids;
	Offers = Ids;          // the level-up card path draws these (AUGMENT style while bAugmentOffer)
	OfferRarity = Tiers;
	bAugmentOffer = true;
	if (IsABot()) ChooseAugment(FMath::RandRange(0, Offers.Num() - 1));
	else if (bAutoPick) ChooseAugment(0);
}

void ASQPlayerState::ChooseAugment(int32 Index)
{
	if (!HasAuthority() || !bAugmentOffer || !AugmentOffers.IsValidIndex(Index) || PendingAugments <= 0) return;
	const FName Picked = AugmentOffers[Index];
	OwnedAugments.Add(Picked);
	CreditQuest(TEXT("AugmentsTaken"));
	USQAugments::ApplyPicked(this, Picked);
	if (const FSQAugmentDef* Def = USQAugments::FindAugment(Picked))
	{
		if (!IsABot()) ClientNotice(FString::Printf(TEXT("AUGMENT: %s"), *Def->Name.ToUpper()), Def->Description, 2, Def->Tier);
	}
	AfterAugmentResolved();
}

void ASQPlayerState::AfterAugmentResolved()
{
	PendingAugments = FMath::Max(0, PendingAugments - 1);
	if (PendingAugmentSources.Num() > 0) PendingAugmentSources.RemoveAt(0);
	Offers.Reset();
	OfferRarity.Reset();
	AugmentOffers.Reset();
	bAugmentOffer = false;
	OfferSerial++;
	if (PendingAugments > 0 || PendingLevelUps > 0) GenerateOffers();
}

void ASQPlayerState::ServerChooseAugment_Implementation(int32 Index)
{
	ChooseAugment(Index);
}

void ASQPlayerState::ChooseOffer(int32 Index)
{
	if (bAugmentOffer)
	{
		ChooseAugment(Index);
		return;
	}
	if (!HasAuthority() || !Offers.IsValidIndex(Index) || PendingLevelUps <= 0) return;
	if (ASQCharacter* C = GetPawn<ASQCharacter>())
	{
		const uint8 Rarity = OfferRarity.IsValidIndex(Index) ? OfferRarity[Index] : 0;
		C->ApplyUpgrade(Offers[Index], Rarity);
	}
	AfterOfferResolved();
}

void ASQPlayerState::RerollOffers()
{
	if (!HasAuthority() || !IsChoosing() || RerollsLeft <= 0) return;
	RerollsLeft--;
	const TArray<FName> Previous = Offers;
	if (bAugmentOffer) RollAugmentOffers(Previous);
	else RollOffers(Previous);
}

void ASQPlayerState::SkipOffer()
{
	if (!HasAuthority() || !IsChoosing()) return;
	if (bAugmentOffer) return; // augments can't be skipped (pick one; the co-op idle timer takes the first)
	ASQCharacter* C = GetPawn<ASQCharacter>();
	if (C && !C->bDowned)
	{
		// small consolation: a sip of health
		const USQData* D = USQData::Get(this);
		const float HealFraction = D ? D->Tune(TEXT("SkipHealFraction"), 0.15f) : 0.15f;
		C->Health = FMath::Min(C->Stats.MaxHealth, C->Health + C->Stats.MaxHealth * HealFraction);
	}
	AfterOfferResolved();
}

void ASQPlayerState::ResetForRun()
{
	const USQData* D = USQData::Get(this);
	Offers.Reset();
	OfferRarity.Reset();
	OfferSerial++;
	PendingLevelUps = 0;
	RerollsLeft = FMath::Max(0, FMath::RoundToInt(D ? D->Tune(TEXT("RerollsPerRun"), 2.f) : 2.f));
	bInPickPause = false;
	bShopReady = false;
	bPickStripOpen = false;
	OfferIdleSeconds = 0.f;
	Kills = 0;
	RunJoinNight = 0;
	MidRunPickDeadline = -1.0;
	Gold = 0;
	ChestsOpened = 0;
	GoldRemainder = 0.f;
	PendingQuest.Reset();
	AugmentOffers.Reset();
	OwnedAugments.Reset();
	bAugmentOffer = false;
	PendingAugments = 0;
	PendingAugmentSources.Reset();
	RunGoldEarned = 0;
	TrackLastHealth = -1.f;
	TrackLastDashCd = 0.f;
	TrackLastCarried = 0;
	TrackRunMaxCarried = 0;
	TrackPlateau = INDEX_NONE;
	bTrackHitThisNight = false;
	bTrackWasDowned = false;
	TrackDamageRemainder = 0.f;
}

// ============================================================ gold (server)

void ASQPlayerState::AddGold(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return;
	// Golden Touch (meta): x(1 + bonus); the fraction waits in GoldRemainder
	const float GoldBonus = FMath::Clamp(GetMetaValue(TEXT("GoldGain")), 0.f, 5.f);
	if (GoldBonus <= 0.f)
	{
		AddGoldRaw(Amount);
		return;
	}
	GoldRemainder += FMath::Min(static_cast<float>(Amount) * (1.f + GoldBonus), 1.0e7f);
	const int32 Whole = FMath::FloorToInt(GoldRemainder);
	if (Whole > 0)
	{
		GoldRemainder -= static_cast<float>(Whole);
		AddGoldRaw(Whole);
	}
}

void ASQPlayerState::AddGoldRaw(int32 Amount)
{
	if (Amount <= 0) return;
	Gold = static_cast<int32>(FMath::Min(static_cast<int64>(Gold) + Amount, static_cast<int64>(100000000)));
	RunGoldEarned = static_cast<int32>(FMath::Min(static_cast<int64>(RunGoldEarned) + Amount, static_cast<int64>(100000000)));
	CreditQuest(TEXT("GoldEarned"), Amount);
}

bool ASQPlayerState::SpendGold(int32 Amount)
{
	if (!HasAuthority() || Amount < 0 || Gold < Amount) return false;
	Gold -= Amount;
	if (Amount > 0) CreditQuest(TEXT("GoldSpent"), Amount);
	return true;
}

void ASQPlayerState::AddGoldExact(float Amount)
{
	if (!HasAuthority() || !(Amount > 0.f)) return;
	GoldRemainder += FMath::Min(Amount * (1.f + FMath::Clamp(GetMetaValue(TEXT("GoldGain")), 0.f, 5.f)), 1.0e7f);
	const int32 Whole = FMath::FloorToInt(GoldRemainder);
	if (Whole > 0)
	{
		GoldRemainder -= static_cast<float>(Whole);
		AddGoldRaw(Whole);
	}
}

// ============================================================ meta upgrades

float ASQPlayerState::GetMetaValue(FName Stat) const
{
	if (MetaRanks.Num() == 0) return 0.f;
	const USQData* D = USQData::Get(this);
	float Total = 0.f;
	for (const FSQMetaUpgradeDef& Def : USQMissions::MetaUpgrades())
	{
		if (Def.Stat == Stat) Total += USQMissions::MetaValueAt(Def, GetMetaRank(Def.Id), D);
	}
	return Total;
}

void ASQPlayerState::ServerSetMetaRanks_Implementation(const TArray<FName>& Ids, const TArray<uint8>& Ranks)
{
	MetaRanks.Reset();
	for (int32 RankIdx = 0; RankIdx < Ids.Num() && RankIdx < Ranks.Num() && RankIdx < 64; ++RankIdx)
	{
		// Only real lines, never past their MaxRank (a client can't send itself rank 99)
		const FSQMetaUpgradeDef* Def = USQMissions::FindMetaUpgrade(Ids[RankIdx]);
		if (Def && Ranks[RankIdx] > 0) MetaRanks.Add(Def->Id, FMath::Min(static_cast<int32>(Ranks[RankIdx]), Def->MaxRank));
	}
	ASQCharacter* C = GetPawn<ASQCharacter>();
	const ASQGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	if (C)
	{
		C->RecomputeStats();
		if (GS && GS->Phase == ESQPhase::Waiting && !C->bDowned) C->Health = C->Stats.MaxHealth;
	}
}

void ASQPlayerState::SendMetaRanks()
{
	const USQSaveGame* Save = USQSaveGame::Load();
	TArray<FName> Ids;
	TArray<uint8> Ranks;
	if (Save)
	{
		for (const TPair<FName, int32>& Each : Save->MetaRanks)
		{
			if (Each.Value <= 0) continue;
			Ids.Add(Each.Key);
			Ranks.Add(static_cast<uint8>(FMath::Clamp(Each.Value, 0, 255)));
		}
	}
	ServerSetMetaRanks(Ids, Ranks);
}

void ASQPlayerState::ClientMissionEnd_Implementation(FName MissionId, bool bHard, bool bWon, int32 GoldBanked, float StageSeconds)
{
	USQSaveGame* Save = USQSaveGame::Load();
	if (!Save) return;
	const FSQMissionDef* Mission = USQMissions::FindMission(MissionId);
	int32 Banked = FMath::Clamp(GoldBanked, 0, 10000000);
	FString Sub = FString::Printf(TEXT("%d gold banked for Death's Office"), Banked);
	Save->RunsPlayed++;
	if (bWon && Mission)
	{
		Save->RunsWon++;
		// First clears pay once per profile (every PC has its own)
		if (!Save->HasClearedMission(MissionId))
		{
			Banked += Mission->FirstClearGold;
			Save->MissionsCleared.Add(MissionId);
			Sub = FString::Printf(TEXT("%d gold banked (first clear +%d). A new mission and Hard mode may be open."), Banked, Mission->FirstClearGold);
		}
		if (bHard && !Save->HasClearedHard(MissionId))
		{
			Banked += Mission->HardFirstClearGold;
			Save->HardCleared.Add(MissionId);
			Sub = FString::Printf(TEXT("%d gold banked (first Hard clear +%d)."), Banked, Mission->HardFirstClearGold);
		}
		// The biome's contract (old quests and lore pages count contracts)
		Save->Contracts.AddUnique(Mission->Biome);
		float& Best = Save->MissionBestSeconds.FindOrAdd(MissionId);
		if (Best <= 0.f || StageSeconds < Best) Best = FMath::Max(1.f, StageSeconds);
	}
	Save->OvertimePay = FMath::Clamp(Save->OvertimePay + Banked, 0, 100000000);
	Save->LifetimePay = FMath::Clamp(Save->LifetimePay + Banked, 0, 100000000);
	int32& BankedTotal = Save->QuestTotals.FindOrAdd(TEXT("GoldBanked"));
	BankedTotal = static_cast<int32>(FMath::Min(static_cast<int64>(BankedTotal) + Banked, static_cast<int64>(2000000000)));
	AddLocalToast(bWon ? FString(TEXT("MISSION COMPLETE")) : FString(TEXT("MISSION FAILED")), Sub, 3);
	USQSaveGame::Store(Save);
}

void ASQPlayerState::ServerUseNearby_Implementation()
{
	if (ASQGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ASQGameMode>() : nullptr)
	{
		GM->HandleUse(this);
	}
}

void ASQPlayerState::ServerToggleModifier_Implementation(int32 Index)
{
	ASQGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	const APlayerController* HostPC = Cast<APlayerController>(GetOwner());
	// Host only (the listen-server's own controller), and only before the run starts
	if (!GS || !HostPC || !HostPC->IsLocalController() || GS->Phase != ESQPhase::Waiting) return;
	const TArray<FSQModifierDef>& Mods = SQQuests::Modifiers();
	if (!Mods.IsValidIndex(Index) || Index >= 31) return;
	GS->ModifierMask ^= (1 << Index);
	const bool bOn = GS->HasModifier(Index);
	GS->PushAlert(FString::Printf(TEXT("Contract clause %s: %s (banked gold x%.2f)"), bOn ? TEXT("ON") : TEXT("OFF"), *Mods[Index].Name, GS->CoinMultiplier()));
}

// ============================================================ quests

void ASQPlayerState::CreditQuest(FName Stat, int32 Amount)
{
	// Bots have no profile; and a client RPC on a bot's state would run on the host and credit the HOST's profile
	if (!HasAuthority() || IsABot() || Stat.IsNone() || Amount <= 0) return;
	int32& Pending = PendingQuest.FindOrAdd(Stat);
	Pending = static_cast<int32>(FMath::Min(static_cast<int64>(Pending) + Amount, static_cast<int64>(1000000000)));
}

void ASQPlayerState::FlushQuests()
{
	if (!HasAuthority() || PendingQuest.Num() == 0) return;
	if (IsABot() || !Cast<APlayerController>(GetOwner()))
	{
		PendingQuest.Reset();
		return;
	}
	TArray<FName> Stats;
	TArray<int32> Amounts;
	for (const TPair<FName, int32>& Each : PendingQuest)
	{
		Stats.Add(Each.Key);
		Amounts.Add(Each.Value);
	}
	PendingQuest.Reset();
	ClientQuestProgress(Stats, Amounts);
}

void ASQPlayerState::ClientQuestProgress_Implementation(const TArray<FName>& Stats, const TArray<int32>& Amounts)
{
	USQSaveGame* Save = USQSaveGame::Load();
	if (!Save) return;
	for (int32 StatIdx = 0; StatIdx < Stats.Num() && StatIdx < Amounts.Num(); ++StatIdx)
	{
		const int32 Amount = FMath::Clamp(Amounts[StatIdx], 0, 1000000000);
		int32& Total = Save->QuestTotals.FindOrAdd(Stats[StatIdx]);
		Total = static_cast<int32>(FMath::Min(static_cast<int64>(Total) + Amount, static_cast<int64>(2000000000)));
		int32& RunValue = LocalRunStats.FindOrAdd(Stats[StatIdx]);
		RunValue = static_cast<int32>(FMath::Min(static_cast<int64>(RunValue) + Amount, static_cast<int64>(2000000000)));
	}
	TArray<const FSQQuestDef*> Finished;
	SQQuests::CompleteFinished(Save, &LocalRunStats, Finished);
	const USQData* D = USQData::Get(this);
	for (const FSQQuestDef* Quest : Finished)
	{
		AddLocalToast(FString::Printf(TEXT("REVIEW PASSED: %s"), *Quest->Name.ToUpper()), SQQuests::DescribeRewards(*Quest, D), 1);
		for (const FSQQuestReward& Reward : Quest->Rewards)
		{
			if (Reward.Type == TEXT("Item")) bLocksSent = false; // the HUD re-sends the (shorter) locked list
		}
	}
	if (Finished.Num() > 0) USQSaveGame::Store(Save);
}

void ASQPlayerState::ClientQuestRunStart_Implementation()
{
	LocalRunStats.Reset();
	SendMetaRanks(); // a mission is starting: make sure the server has this profile's latest Death's Office ranks
}

void ASQPlayerState::ClientQuestRunEnd_Implementation(bool bWon, FName PlayedReaper, int32 BonusCoins)
{
	USQSaveGame* Save = USQSaveGame::Load();
	if (!Save) return;
	for (const TPair<FName, int32>& RunValue : LocalRunStats)
	{
		int32& Best = Save->QuestBests.FindOrAdd(RunValue.Key);
		Best = FMath::Max(Best, RunValue.Value);
	}
	if (bWon && !PlayedReaper.IsNone()) Save->ReapersWonWith.AddUnique(PlayedReaper);
	const int32 Bonus = FMath::Clamp(BonusCoins, 0, 1000000);
	if (Bonus > 0)
	{
		Save->OvertimePay = FMath::Clamp(Save->OvertimePay + Bonus, 0, 100000000);
		Save->LifetimePay = FMath::Clamp(Save->LifetimePay + Bonus, 0, 100000000);
		AddLocalToast(FString::Printf(TEXT("CONTRACT CLAUSES: +%d BANKED GOLD"), Bonus), TEXT("Bonus for working under harder terms"), 3);
	}
	TArray<const FSQQuestDef*> Finished;
	SQQuests::CompleteFinished(Save, &LocalRunStats, Finished);
	const USQData* D = USQData::Get(this);
	for (const FSQQuestDef* Quest : Finished)
	{
		AddLocalToast(FString::Printf(TEXT("REVIEW PASSED: %s"), *Quest->Name.ToUpper()), SQQuests::DescribeRewards(*Quest, D), 1);
	}
	bLocksSent = false;
	USQSaveGame::Store(Save);
}

void ASQPlayerState::ClientNotice_Implementation(const FString& Text, const FString& Sub, uint8 Kind, uint8 Rarity)
{
	AddLocalToast(Text, Sub, Kind, Rarity);
}

void ASQPlayerState::AddLocalToast(const FString& Text, const FString& Sub, uint8 Kind, uint8 Rarity)
{
	FSQLocalToast& Toast = LocalToasts.AddDefaulted_GetRef();
	Toast.Text = Text.Left(120);
	Toast.Sub = Sub.Left(160);
	Toast.Kind = Kind;
	Toast.Rarity = Rarity;
	Toast.Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	while (LocalToasts.Num() > 12) LocalToasts.RemoveAt(0);
}

// ============================================================ quest-locked items

bool ASQPlayerState::IsItemLocked(FName Id) const
{
	if (LockedItems.Num() == 0) return false;
	const USQData* D = USQData::Get(this);
	if (D && D->Tune(TEXT("UnlockAllItems"), 0.f) > 0.5f) return false;
	return LockedItems.Contains(Id);
}

void ASQPlayerState::ServerSetLockedItems_Implementation(const TArray<FName>& Locked)
{
	LockedItems.Reset();
	for (const FName ItemId : Locked)
	{
		// Only real quest items can be locked (a client can't lock the whole pool and break offers)
		if (LockedItems.Num() < 128 && SQQuests::IsQuestItem(ItemId)) LockedItems.AddUnique(ItemId);
	}
}

void ASQPlayerState::SendLockedItems()
{
	bLocksSent = true;
	TArray<FName> Locked;
	SQQuests::GetLockedItems(USQSaveGame::Load(), Locked);
	ServerSetLockedItems(Locked);
}
