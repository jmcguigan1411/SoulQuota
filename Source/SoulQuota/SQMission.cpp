// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQMission.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "SQQuests.h"
#include "SQSave.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQGameMode.h"
#include "SQPlayerState.h"
#include "SQCharacter.h"
#include "SQHordeManager.h"
#include "SQArena.h"
#include "SQLantern.h"
#include "SQChest.h"
#include "SQAugments.h"
#include "SQInteractCue.h"

// Unity build: every file-scope name here carries an SQMis prefix.
namespace SQMisData
{
	struct FSQMisTables
	{
		TArray<FSQMissionDef> Missions;
		TArray<FSQMetaUpgradeDef> Meta;
		FString Errors;
		bool bLoaded = false;
	};

	FSQMisTables& SQMisTables()
	{
		static FSQMisTables Tables;
		return Tables;
	}

	TArray<FString> SQMisSplit(const FString& Text, const TCHAR* Sep)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, Sep, false);
		for (FString& Each : Parts) Each.TrimStartAndEndInline();
		return Parts;
	}

	void SQMisLoad(FSQMisTables& Tables)
	{
		Tables.Missions.Reset();
		Tables.Meta.Reset();
		Tables.Errors.Reset();
		Tables.bLoaded = true;

		TArray<TMap<FString, FString>> Rows;
		if (SQQuests::ReadCsvRows(TEXT("Missions.csv"), Rows, Tables.Errors))
		{
			for (const TMap<FString, FString>& Row : Rows)
			{
				FSQMissionDef Def;
				Def.Id = FName(*SQQuests::CsvCell(Row, TEXT("Id")));
				if (Def.Id.IsNone()) continue;
				Def.Name = SQQuests::CsvCell(Row, TEXT("Name"));
				Def.Description = SQQuests::CsvCell(Row, TEXT("Description"));
				Def.Biome = FName(*SQQuests::CsvCell(Row, TEXT("Biome")));
				Def.Duration = FMath::Clamp(SQQuests::CsvNum(Row, TEXT("Duration"), 1080.f), 60.f, 7200.f);
				for (const FString& ObjText : SQMisSplit(SQQuests::CsvCell(Row, TEXT("Objectives")), TEXT("|")))
				{
					if (ObjText.IsEmpty()) continue;
					const TArray<FString> Fields = SQMisSplit(ObjText, TEXT(":"));
					FSQObjectiveSpec Spec;
					Spec.Type = USQMissions::ParseObjectiveType(Fields.IsValidIndex(0) ? Fields[0] : FString());
					if (Spec.Type == ESQObjectiveType::None)
					{
						Tables.Errors += FString::Printf(TEXT("Mission %s: unknown objective %s. "), *Def.Id.ToString(), *ObjText);
						continue;
					}
					Spec.TypeName = USQMissions::ObjectiveTypeName(Spec.Type);
					Spec.Count = Fields.IsValidIndex(1) ? FMath::Clamp(FCString::Atoi(*Fields[1]), 1, 2000) : 1;
					const FString RewardText = Fields.IsValidIndex(2) ? Fields[2] : FString(TEXT("Gold"));
					if (RewardText.Equals(TEXT("Chest"), ESearchCase::IgnoreCase)) Spec.Reward = ESQObjectiveReward::Chest;
					else if (RewardText.Equals(TEXT("Augment"), ESearchCase::IgnoreCase)) Spec.Reward = ESQObjectiveReward::Augment;
					else if (RewardText.Equals(TEXT("XP"), ESearchCase::IgnoreCase)) Spec.Reward = ESQObjectiveReward::XP;
					else if (RewardText.Equals(TEXT("None"), ESearchCase::IgnoreCase)) Spec.Reward = ESQObjectiveReward::None;
					else Spec.Reward = ESQObjectiveReward::Gold;
					Spec.RewardName = Spec.Reward == ESQObjectiveReward::None ? FName() : FName(*RewardText);
					Spec.Enemy = (Fields.IsValidIndex(3) && !Fields[3].IsEmpty()) ? FName(*Fields[3]) : USQMissions::DefaultEnemyFor(Spec.Type);
					Def.Objectives.Add(Spec);
				}
				Def.Boss = FName(*SQQuests::CsvCell(Row, TEXT("Boss")));
				for (const FString& MiniText : SQMisSplit(SQQuests::CsvCell(Row, TEXT("Minibosses")), TEXT("|")))
				{
					if (MiniText.IsEmpty()) continue;
					const TArray<FString> Fields = SQMisSplit(MiniText, TEXT(":"));
					FSQMissionMiniboss Mini;
					Mini.Enemy = FName(*(Fields.IsValidIndex(0) ? Fields[0] : FString(TEXT("Brute"))));
					Mini.HealthMult = Fields.IsValidIndex(1) ? FMath::Clamp(FCString::Atof(*Fields[1]), 0.1f, 200.f) : 5.f;
					Mini.DamageMult = Fields.IsValidIndex(2) ? FMath::Clamp(FCString::Atof(*Fields[2]), 0.1f, 20.f) : 1.3f;
					Mini.Name = Fields.IsValidIndex(3) ? Fields[3] : Mini.Enemy.ToString();
					Def.Minibosses.Add(Mini);
				}
				for (const FString& AtText : SQMisSplit(SQQuests::CsvCell(Row, TEXT("MinibossAt")), TEXT("|")))
				{
					if (!AtText.IsEmpty()) Def.MinibossAt.Add(FMath::Clamp(FCString::Atof(*AtText), 0.f, 1.f));
				}
				while (Def.MinibossAt.Num() < Def.Minibosses.Num())
				{
					Def.MinibossAt.Add(FMath::Clamp(0.35f + 0.33f * static_cast<float>(Def.MinibossAt.Num()), 0.f, 0.95f));
				}
				auto Mul = [&Row](const TCHAR* Key, float Default) { return FMath::Clamp(SQQuests::CsvNum(Row, Key, Default), 0.05f, 50.f); };
				Def.HealthMult = Mul(TEXT("HealthMult"), 1.f);
				Def.DamageMult = Mul(TEXT("DamageMult"), 1.f);
				Def.SpawnMult = Mul(TEXT("SpawnMult"), 1.f);
				Def.BossHealthMult = Mul(TEXT("BossHealthMult"), 1.f);
				Def.GoldMult = Mul(TEXT("GoldMult"), 1.f);
				Def.HardHealthMult = Mul(TEXT("HardHealthMult"), 1.5f);
				Def.HardDamageMult = Mul(TEXT("HardDamageMult"), 1.35f);
				Def.HardSpawnMult = Mul(TEXT("HardSpawnMult"), 1.2f);
				Def.HardBossHealthMult = Mul(TEXT("HardBossHealthMult"), 1.6f);
				Def.HardGoldMult = Mul(TEXT("HardGoldMult"), 1.5f);
				const FString UnlockText = SQQuests::CsvCell(Row, TEXT("Unlock"));
				FString UnlockKind, UnlockValue;
				if (UnlockText.Split(TEXT(":"), &UnlockKind, &UnlockValue)) Def.UnlockMission = FName(*UnlockValue.TrimStartAndEnd());
				Def.ClearGold = FMath::Max(0, FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("ClearGold"), 0.f)));
				Def.FirstClearGold = FMath::Max(0, FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("FirstClearGold"), 0.f)));
				Def.HardFirstClearGold = FMath::Max(0, FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("HardFirstClearGold"), 0.f)));
				Def.Difficulty = FMath::Clamp(FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("Difficulty"), 1.f)), 1, 5);
				if (Def.Objectives.Num() == 0) Tables.Errors += FString::Printf(TEXT("Mission %s: no objectives. "), *Def.Id.ToString());
				Tables.Missions.Add(Def);
			}
		}

		Rows.Reset();
		if (SQQuests::ReadCsvRows(TEXT("MetaUpgrades.csv"), Rows, Tables.Errors))
		{
			for (const TMap<FString, FString>& Row : Rows)
			{
				FSQMetaUpgradeDef Def;
				Def.Id = FName(*SQQuests::CsvCell(Row, TEXT("Id")));
				if (Def.Id.IsNone()) continue;
				Def.Name = SQQuests::CsvCell(Row, TEXT("Name"));
				Def.Description = SQQuests::CsvCell(Row, TEXT("Description"));
				Def.Stat = FName(*SQQuests::CsvCell(Row, TEXT("Stat")));
				Def.PerRank = SQQuests::CsvNum(Row, TEXT("PerRank"), 0.f);
				Def.MaxRank = FMath::Clamp(FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("MaxRank"), 1.f)), 1, 50);
				Def.BaseCost = FMath::Clamp(FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("BaseCost"), 100.f)), 1, 10000000);
				Def.CostGrowth = FMath::Clamp(SQQuests::CsvNum(Row, TEXT("CostGrowth"), 1.5f), 1.f, 10.f);
				Def.Order = FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("Order"), static_cast<float>(Tables.Meta.Num())));
				Tables.Meta.Add(Def);
			}
			Tables.Meta.StableSort([](const FSQMetaUpgradeDef& A, const FSQMetaUpgradeDef& B) { return A.Order < B.Order; });
		}
	}

	FSQMisTables& SQMisGet()
	{
		FSQMisTables& Tables = SQMisTables();
		if (!Tables.bLoaded) SQMisLoad(Tables);
		return Tables;
	}

	bool SQMisIsSpecialMetaStat(FName Stat)
	{
		return Stat == TEXT("XPGain") || Stat == TEXT("GoldGain") || Stat == TEXT("ReviveSeconds") || Stat == TEXT("Rerolls")
			|| Stat == TEXT("StartLevels") || Stat == TEXT("Pardon");
	}

	bool SQMisUnlockAll(const USQData* D)
	{
		return D && D->Tune(TEXT("UnlockAllBiomes"), 0.f) > 0.5f;
	}

	/** Tuning NightSeconds / BalanceNightSeconds (test lengths compress the stage and the objectives with it) */
	float SQMisTimeScale(const USQData* D)
	{
		if (!D) return 1.f;
		return FMath::Clamp(D->Tune(TEXT("NightSeconds"), 360.f) / FMath::Max(1.f, D->Tune(TEXT("BalanceNightSeconds"), 360.f)), 0.1f, 4.f);
	}
}

// ================================================================ meta upgrade rows

int32 FSQMetaUpgradeDef::CostForRank(int32 CurrentRank) const
{
	if (CurrentRank >= MaxRank) return -1;
	const double Raw = static_cast<double>(BaseCost) * FMath::Pow(static_cast<double>(CostGrowth), static_cast<double>(FMath::Max(0, CurrentRank)));
	const double Rounded = FMath::Max(5.0, FMath::RoundToDouble(Raw / 5.0) * 5.0);
	return static_cast<int32>(FMath::Min(Rounded, 100000000.0));
}

FString FSQMetaUpgradeDef::ValueText(int32 Rank) const
{
	if (Rank <= 0) return FString();
	const float Total = PerRank * static_cast<float>(Rank);
	if (Stat == TEXT("XPGain")) return FString::Printf(TEXT("+%d%% team XP"), FMath::RoundToInt(Total * 100.f));
	if (Stat == TEXT("GoldGain")) return FString::Printf(TEXT("+%d%% gold"), FMath::RoundToInt(Total * 100.f));
	if (Stat == TEXT("ReviveSeconds")) return FString::Printf(TEXT("Field revive in %d s"), FMath::Max(1, FMath::RoundToInt(FMath::Max(3.f, PerRank - static_cast<float>(Rank - 1)))));
	if (Stat == TEXT("Rerolls")) return FString::Printf(TEXT("+%d rerolls per run"), FMath::RoundToInt(Total));
	if (Stat == TEXT("StartLevels")) return FString::Printf(TEXT("+%d free level-up picks"), FMath::RoundToInt(Total));
	if (Stat == TEXT("Pardon")) return FString(TEXT("A free revive every mission"));
	return SQ::StatText(Stat, Total);
}

// ================================================================ USQMissions (static)

const TArray<FSQMissionDef>& USQMissions::Missions() { return SQMisData::SQMisGet().Missions; }
const TArray<FSQMetaUpgradeDef>& USQMissions::MetaUpgrades() { return SQMisData::SQMisGet().Meta; }
const FString& USQMissions::LoadErrors() { return SQMisData::SQMisGet().Errors; }
void USQMissions::Reload() { SQMisData::SQMisLoad(SQMisData::SQMisTables()); }

const FSQMissionDef* USQMissions::FindMission(FName Id)
{
	return Missions().FindByPredicate([Id](const FSQMissionDef& Each) { return Each.Id == Id; });
}

const FSQMetaUpgradeDef* USQMissions::FindMetaUpgrade(FName Id)
{
	return MetaUpgrades().FindByPredicate([Id](const FSQMetaUpgradeDef& Each) { return Each.Id == Id; });
}

ESQObjectiveType USQMissions::ParseObjectiveType(const FString& Text)
{
	const FString Clean = Text.TrimStartAndEnd();
	if (Clean.Equals(TEXT("BankSouls"), ESearchCase::IgnoreCase)) return ESQObjectiveType::BankSouls;
	if (Clean.Equals(TEXT("DestroySpawners"), ESearchCase::IgnoreCase)) return ESQObjectiveType::DestroySpawners;
	if (Clean.Equals(TEXT("DefendPoint"), ESearchCase::IgnoreCase)) return ESQObjectiveType::DefendPoint;
	if (Clean.Equals(TEXT("Escort"), ESearchCase::IgnoreCase)) return ESQObjectiveType::Escort;
	if (Clean.Equals(TEXT("CollectItems"), ESearchCase::IgnoreCase)) return ESQObjectiveType::CollectItems;
	if (Clean.Equals(TEXT("SurviveWave"), ESearchCase::IgnoreCase)) return ESQObjectiveType::SurviveWave;
	if (Clean.Equals(TEXT("KillElite"), ESearchCase::IgnoreCase)) return ESQObjectiveType::KillElite;
	return ESQObjectiveType::None;
}

FName USQMissions::ObjectiveTypeName(ESQObjectiveType Type)
{
	switch (Type)
	{
	case ESQObjectiveType::BankSouls: return TEXT("BankSouls");
	case ESQObjectiveType::DestroySpawners: return TEXT("DestroySpawners");
	case ESQObjectiveType::DefendPoint: return TEXT("DefendPoint");
	case ESQObjectiveType::Escort: return TEXT("Escort");
	case ESQObjectiveType::CollectItems: return TEXT("CollectItems");
	case ESQObjectiveType::SurviveWave: return TEXT("SurviveWave");
	case ESQObjectiveType::KillElite: return TEXT("KillElite");
	default: return NAME_None;
	}
}

FName USQMissions::DefaultEnemyFor(ESQObjectiveType Type)
{
	switch (Type)
	{
	case ESQObjectiveType::DestroySpawners: return TEXT("Shambler");
	case ESQObjectiveType::DefendPoint: return TEXT("Rotter");
	case ESQObjectiveType::Escort: return TEXT("Lurcher");
	case ESQObjectiveType::SurviveWave: return TEXT("BoneHound");
	case ESQObjectiveType::KillElite: return TEXT("Brute");
	default: return NAME_None;
	}
}

bool USQMissions::IsMissionUnlocked(const USQData* D, const USQSaveGame* Save, FName Id)
{
	const FSQMissionDef* Def = FindMission(Id);
	if (!Def) return false;
	if (SQMisData::SQMisUnlockAll(D) || Def->UnlockMission.IsNone()) return true;
	return Save && Save->HasClearedMission(Def->UnlockMission);
}

bool USQMissions::IsHardUnlocked(const USQData* D, const USQSaveGame* Save, FName Id)
{
	if (!FindMission(Id)) return false;
	return SQMisData::SQMisUnlockAll(D) || (Save && Save->HasClearedMission(Id));
}

int32 USQMissions::GetMetaRank(const USQSaveGame* Save, FName Id)
{
	return Save ? Save->MetaRank(Id) : 0;
}

int32 USQMissions::CountMetaRanks(const USQSaveGame* Save)
{
	int32 Total = 0;
	if (Save)
	{
		for (const TPair<FName, int32>& Each : Save->MetaRanks) Total += FMath::Max(0, Each.Value);
	}
	return Total;
}

float USQMissions::MetaValueAt(const FSQMetaUpgradeDef& Def, int32 Rank, const USQData* D)
{
	if (Rank <= 0) return 0.f;
	if (Def.Stat == TEXT("ReviveSeconds"))
	{
		const float Fastest = D ? FMath::Max(0.5f, D->Tune(TEXT("MetaReviveMinSeconds"), 3.f)) : 3.f;
		return FMath::Max(Fastest, Def.PerRank - static_cast<float>(Rank - 1));
	}
	return Def.PerRank * static_cast<float>(Rank);
}

void USQMissions::GetMissionList(const USQData* D, const USQSaveGame* Save, TArray<FSQMissionView>& Out)
{
	Out.Reset();
	for (const FSQMissionDef& Def : Missions())
	{
		FSQMissionView& View = Out.AddDefaulted_GetRef();
		View.Id = Def.Id;
		View.Name = FText::FromString(Def.Name);
		View.Description = FText::FromString(Def.Description);
		View.Biome = Def.Biome;
		View.DurationSeconds = Def.Duration;
		const FSQBossDef* BossDef = D ? D->FindBoss(Def.Boss) : nullptr;
		View.BossName = FText::FromString(BossDef ? BossDef->Name : Def.Boss.ToString());
		View.Difficulty = Def.Difficulty;
		View.ObjectiveCount = Def.Objectives.Num();
		View.bUnlocked = IsMissionUnlocked(D, Save, Def.Id);
		View.bCleared = Save && Save->HasClearedMission(Def.Id);
		View.bHardUnlocked = IsHardUnlocked(D, Save, Def.Id);
		View.bHardCleared = Save && Save->HasClearedHard(Def.Id);
		if (!View.bUnlocked)
		{
			const FSQMissionDef* Prev = FindMission(Def.UnlockMission);
			View.UnlockText = FText::FromString(FString::Printf(TEXT("Beat %s to unlock"), Prev ? *Prev->Name : *Def.UnlockMission.ToString()));
		}
		FString Reward = FString::Printf(TEXT("Clear: %d gold"), Def.ClearGold);
		if (!View.bCleared && Def.FirstClearGold > 0) Reward += FString::Printf(TEXT(" (first clear +%d)"), Def.FirstClearGold);
		else if (View.bCleared && !View.bHardCleared && Def.HardFirstClearGold > 0) Reward += FString::Printf(TEXT(" (first Hard clear +%d)"), Def.HardFirstClearGold);
		View.RewardText = FText::FromString(Reward);
		const float* Best = Save ? Save->MissionBestSeconds.Find(Def.Id) : nullptr;
		View.BestSeconds = Best ? *Best : 0.f;
	}
}

void USQMissions::GetMetaUpgradeList(const USQData* D, const USQSaveGame* Save, TArray<FSQMetaUpgradeView>& Out)
{
	Out.Reset();
	const int32 Bank = Save ? Save->OvertimePay : 0;
	for (const FSQMetaUpgradeDef& Def : MetaUpgrades())
	{
		FSQMetaUpgradeView& View = Out.AddDefaulted_GetRef();
		View.Id = Def.Id;
		View.Name = FText::FromString(Def.Name);
		View.Description = FText::FromString(Def.Description);
		View.Stat = Def.Stat;
		View.Rank = FMath::Min(GetMetaRank(Save, Def.Id), Def.MaxRank);
		View.MaxRank = Def.MaxRank;
		View.NextCost = Def.CostForRank(View.Rank);
		View.bCanAfford = View.NextCost >= 0 && Bank >= View.NextCost;
		View.CurrentText = FText::FromString(View.Rank > 0 ? Def.ValueText(View.Rank) : FString(TEXT("-")));
		View.NextText = FText::FromString(View.NextCost >= 0 ? Def.ValueText(View.Rank + 1) : FString(TEXT("MAX")));
	}
}

bool USQMissions::BuyMetaUpgrade(USQSaveGame* Save, const USQData* D, FName Id)
{
	const FSQMetaUpgradeDef* Def = FindMetaUpgrade(Id);
	if (!Save || !Def) return false;
	const int32 Rank = FMath::Min(Save->MetaRank(Id), Def->MaxRank);
	const int32 Cost = Def->CostForRank(Rank);
	if (Cost < 0 || Save->OvertimePay < Cost) return false;
	Save->OvertimePay -= Cost;
	Save->MetaRanks.FindOrAdd(Id) = Rank + 1;
	USQSaveGame::Store(Save);
	return true;
}

void USQMissions::SetPendingMission(UObject* WorldContext, FName MissionId, bool bHard)
{
	USQSaveGame* Save = USQSaveGame::Load();
	if (!Save || !FindMission(MissionId)) return;
	const USQData* D = WorldContext ? USQData::Get(WorldContext) : nullptr;
	Save->PendingMission = MissionId;
	Save->bPendingHard = bHard && IsHardUnlocked(D, Save, MissionId);
	USQSaveGame::Store(Save);
}

FName USQMissions::GetPendingMission(const UObject* WorldContext, bool& bOutHard)
{
	bOutHard = false;
	const USQSaveGame* Save = USQSaveGame::Load();
	const USQData* D = WorldContext ? USQData::Get(WorldContext) : nullptr;
	const TArray<FSQMissionDef>& All = Missions();
	if (All.Num() == 0) return NAME_None;
	if (Save && !Save->PendingMission.IsNone() && IsMissionUnlocked(D, Save, Save->PendingMission))
	{
		bOutHard = Save->bPendingHard && IsHardUnlocked(D, Save, Save->PendingMission);
		return Save->PendingMission;
	}
	// No choice yet: the first unlocked mission that has not been beaten (a new player lands on map 1)
	for (const FSQMissionDef& Def : All)
	{
		if (IsMissionUnlocked(D, Save, Def.Id) && !(Save && Save->HasClearedMission(Def.Id))) return Def.Id;
	}
	return All[0].Id;
}

// ================================================================ ASQObjective (world parts)

ASQObjective::ASQObjective()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(10.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	auto MakePiece = [this](const TCHAR* PieceName)
	{
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(PieceName);
		Piece->SetupAttachment(Root);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCanEverAffectNavigation(false);
		return Piece;
	};
	Body = MakePiece(TEXT("Body"));
	Top = MakePiece(TEXT("Top"));
	Ring = MakePiece(TEXT("Ring"));
	Ring->SetCastShadow(false);
	Cue = CreateDefaultSubobject<USQInteractCue>(TEXT("Cue"));
	Cue->SetupAttachment(Root);
}

void ASQObjective::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQObjective, Part);
	DOREPLIFETIME(ASQObjective, Radius);
	DOREPLIFETIME(ASQObjective, Health);
	DOREPLIFETIME(ASQObjective, MaxHealth);
	DOREPLIFETIME(ASQObjective, Progress);
	DOREPLIFETIME(ASQObjective, bDone);
	DOREPLIFETIME(ASQObjective, bStalled);
}

ASQObjective* ASQObjective::SpawnPart(UWorld* InWorld, ESQObjectivePart InPart, const FVector& Location, float InRadius)
{
	if (!InWorld || InWorld->GetNetMode() == NM_Client) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const float GroundZ = ASQArena::GroundZAt(InWorld, static_cast<float>(Location.X), static_cast<float>(Location.Y));
	ASQObjective* Spawned = InWorld->SpawnActor<ASQObjective>(ASQObjective::StaticClass(), FVector(Location.X, Location.Y, GroundZ), FRotator::ZeroRotator, Params);
	if (!Spawned) return nullptr;
	Spawned->Part = static_cast<uint8>(InPart);
	Spawned->Radius = FMath::Max(40.f, InRadius);
	Spawned->ApplyLook();
	return Spawned;
}

void ASQObjective::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();
}

void ASQObjective::OnRep_Look()
{
	ApplyLook();
}

void ASQObjective::ApplyLook()
{
	if (bLookApplied && LookPart == Part && bLookDone == bDone) return;
	bLookApplied = true;
	LookPart = Part;
	bLookDone = bDone;
	const ESQObjectivePart PartKind = static_cast<ESQObjectivePart>(Part);
	FLinearColor BodyColor(0.3f, 0.3f, 0.3f);
	FLinearColor RingColor(1.f, 0.85f, 0.4f);
	Top->SetVisibility(true);
	Body->SetVisibility(true);
	Ring->SetVisibility(true);
	const float RingScale = Radius / 50.f;
	switch (PartKind)
	{
	case ESQObjectivePart::Nest:
		Body->SetStaticMesh(SQ::Mesh(TEXT("Cone")));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
		Body->SetRelativeScale3D(FVector(2.4f, 2.4f, 2.4f));
		Top->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
		Top->SetRelativeLocation(FVector(0.f, 0.f, 250.f));
		Top->SetRelativeScale3D(FVector(1.1f));
		BodyColor = FLinearColor(0.35f, 0.08f, 0.1f);
		RingColor = FLinearColor(1.f, 0.2f, 0.15f);
		break;
	case ESQObjectivePart::Zone:
		Body->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
		Body->SetRelativeScale3D(FVector(0.15f, 0.15f, 4.4f));
		Top->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
		Top->SetRelativeLocation(FVector(0.f, 45.f, 400.f));
		Top->SetRelativeScale3D(FVector(0.05f, 0.9f, 0.55f));
		BodyColor = FLinearColor(0.25f, 0.2f, 0.15f);
		RingColor = FLinearColor(0.35f, 0.9f, 1.f);
		break;
	case ESQObjectivePart::Cart:
		Body->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
		Body->SetRelativeScale3D(FVector(2.6f, 1.4f, 0.9f));
		Top->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
		Top->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
		Top->SetRelativeScale3D(FVector(2.0f, 0.8f, 0.5f));
		BodyColor = FLinearColor(0.12f, 0.09f, 0.08f);
		RingColor = FLinearColor(0.7f, 0.6f, 1.f);
		break;
	case ESQObjectivePart::Urn:
		Body->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 45.f));
		Body->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.9f));
		Top->SetStaticMesh(SQ::Mesh(TEXT("Sphere")));
		Top->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
		Top->SetRelativeScale3D(FVector(0.5f));
		BodyColor = FLinearColor(0.85f, 0.65f, 0.2f);
		RingColor = FLinearColor(1.f, 0.85f, 0.3f);
		break;
	default: // Beacon
		Body->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 600.f));
		Body->SetRelativeScale3D(FVector(0.35f, 0.35f, 12.f));
		Top->SetVisibility(false);
		BodyColor = FLinearColor(1.f, 0.9f, 0.5f);
		RingColor = FLinearColor(1.f, 0.9f, 0.5f);
		break;
	}
	Ring->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	Ring->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
	Ring->SetRelativeScale3D(FVector(RingScale, RingScale, 0.02f));
	BodyMID = SQ::ColorMID(this, BodyColor);
	RingMID = SQ::ColorMID(this, RingColor);
	if (BodyMID)
	{
		Body->SetMaterial(0, BodyMID);
		Top->SetMaterial(0, BodyMID);
	}
	if (RingMID) Ring->SetMaterial(0, RingMID);
	if (Cue)
	{
		// One look for everything you can use: urns / zones / carts / beacons are green objectives, nests red-orange
		const TCHAR* CueLabel = TEXT("OBJECTIVE");
		ESQCueKind CueKind = ESQCueKind::Collect;
		float CueHeight = 260.f;
		switch (PartKind)
		{
		case ESQObjectivePart::Nest: CueKind = ESQCueKind::Hazard; CueLabel = TEXT("DESTROY THE NEST"); CueHeight = 380.f; break;
		case ESQObjectivePart::Zone: CueLabel = TEXT("HOLD THE ZONE"); CueHeight = 560.f; break;
		case ESQObjectivePart::Cart: CueLabel = TEXT("ESCORT THE CART"); CueHeight = 300.f; break;
		case ESQObjectivePart::Urn: CueLabel = TEXT("COLLECT THE URN"); CueHeight = 200.f; break;
		default: CueHeight = 1300.f; break;
		}
		Cue->Setup(CueKind, FMath::Min(Radius, 700.f), CueHeight, CueLabel);
		Cue->SetUsed(bDone);
	}
	if (bDone)
	{
		// Collected urns vanish; finished nests / zones / carts go dark (they are destroyed with the objective)
		if (PartKind == ESQObjectivePart::Urn)
		{
			Body->SetVisibility(false);
			Top->SetVisibility(false);
		}
		Ring->SetVisibility(false);
	}
}

bool ASQObjective::AnyReaperWithin(float Range, int32* OutCount) const
{
	int32 Count = 0;
	const FVector Here = GetActorLocation();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		const ASQCharacter* Reaper = *It;
		if (!Reaper || Reaper->bDowned) continue;
		if (FVector::Dist2D(Reaper->GetActorLocation(), Here) <= Range) Count++;
	}
	if (OutCount) *OutCount = Count;
	return Count > 0;
}

void ASQObjective::DamageNest(float Amount)
{
	if (!HasAuthority() || bDone || static_cast<ESQObjectivePart>(Part) != ESQObjectivePart::Nest || !(Amount > 0.f)) return;
	Health = FMath::Max(0.f, Health - Amount);
	if (Health <= 0.f)
	{
		bDone = true;
		ApplyLook();
		if (ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>())
		{
			for (APlayerState* EachPS : GS->PlayerArray)
			{
				if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS)) EachSQ->CreditQuest(TEXT("NestsDestroyed"));
			}
			if (GS->Horde)
			{
				const USQData* D = USQData::Get(this);
				const float Pile = (D ? D->Tune(TEXT("EliteGold"), 4.f) : 4.f) * 2.f * GS->GoldNightGrowth() * GS->GoldMultiplier();
				const FVector Here = GetActorLocation();
				GS->Horde->DropGold(FVector2D(Here.X, Here.Y), FMath::Max(1, FMath::RoundToInt(Pile)));
			}
		}
	}
}

void ASQObjective::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LookTime += DeltaSeconds;
	if (HasAuthority() && !bDone) ServerTickPart(DeltaSeconds);

	if (GetNetMode() == NM_DedicatedServer) return;
	const ESQObjectivePart PartKind = static_cast<ESQObjectivePart>(Part);
	const float Pulse = 0.5f + 0.5f * FMath::Sin(LookTime * 3.f);
	if (RingMID)
	{
		float Glow = 0.5f + Pulse;
		if (PartKind == ESQObjectivePart::Zone) Glow = (bStalled ? 0.3f + 1.5f * Pulse : 0.8f) + 2.f * Progress;
		else if (PartKind == ESQObjectivePart::Cart) Glow = bStalled ? 0.2f + 1.2f * Pulse : 0.6f;
		RingMID->SetScalarParameterValue(TEXT("Glow"), bDone ? 0.f : Glow);
	}
	if (BodyMID)
	{
		float BodyGlow = 0.f;
		if (PartKind == ESQObjectivePart::Nest) BodyGlow = MaxHealth > 0.f ? 0.4f + 1.6f * (1.f - Health / MaxHealth) * Pulse : 0.4f;
		else if (PartKind == ESQObjectivePart::Urn || PartKind == ESQObjectivePart::Beacon) BodyGlow = 0.8f + Pulse;
		BodyMID->SetScalarParameterValue(TEXT("Glow"), bDone ? 0.f : BodyGlow);
	}
	if (PartKind == ESQObjectivePart::Nest && !bDone)
	{
		// The nest's heart throbs faster as it cracks
		const float Hurt = MaxHealth > 0.f ? 1.f - Health / MaxHealth : 0.f;
		Top->SetRelativeScale3D(FVector(1.1f + 0.15f * FMath::Sin(LookTime * (4.f + 8.f * Hurt))));
	}
	else if (PartKind == ESQObjectivePart::Urn && !bDone)
	{
		Top->SetRelativeLocation(FVector(0.f, 0.f, 110.f + 12.f * FMath::Sin(LookTime * 2.5f)));
	}
	else if (PartKind == ESQObjectivePart::Zone)
	{
		// The flag climbs its pole with the meter
		Top->SetRelativeLocation(FVector(0.f, 45.f, 60.f + 340.f * FMath::Clamp(Progress, 0.f, 1.f)));
	}
}

void ASQObjective::ServerTickPart(float DeltaSeconds)
{
	const USQData* D = USQData::Get(this);
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (!D || !GS || (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss)) return;
	const FVector Here = GetActorLocation();
	const FVector2D Here2D(Here.X, Here.Y);
	const float CrewScale = 1.f + FMath::Max(0.f, D->Tune(TEXT("PlayerSpawnRateBonus"), 0.35f)) * static_cast<float>(FMath::Max(0, GS->PlayerArray.Num() - 1));

	switch (static_cast<ESQObjectivePart>(Part))
	{
	case ESQObjectivePart::Nest:
	{
		PressureTimer -= DeltaSeconds;
		if (PressureTimer <= 0.f)
		{
			PressureTimer = FMath::Max(1.f, D->Tune(TEXT("NestSpawnInterval"), 5.f));
			const int32 Count = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("NestSpawnCount"), 4.f) * CrewScale), 0, 40);
			if (GS->Horde && Count > 0 && !SpawnEnemy.IsNone()) GS->Horde->SpawnEnemyNear(SpawnEnemy, Here2D, Count, Radius + 220.f);
		}
		// Siege: every reaper standing by the nest chips it (so a nest with no horde around it still falls)
		const float Siege = FMath::Max(0.f, D->Tune(TEXT("NestSiegeDPS"), 30.f));
		const float PerLevel = FMath::Max(0.f, D->Tune(TEXT("AugmentDamagePerLevel"), 0.06f));
		const float LevelScale = 1.f + PerLevel * static_cast<float>(FMath::Max(0, GS->TeamLevel - 1));
		float SiegeDamage = 0.f;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			const ASQCharacter* Reaper = *It;
			if (!Reaper || Reaper->bDowned) continue;
			if (FVector::Dist2D(Reaper->GetActorLocation(), Here) <= Radius + 180.f) SiegeDamage += Siege * LevelScale * FMath::Max(0.1f, Reaper->Stats.DamageMult) * DeltaSeconds;
		}
		if (SiegeDamage > 0.f) DamageNest(SiegeDamage);
		break;
	}
	case ESQObjectivePart::Zone:
	{
		int32 Inside = 0;
		AnyReaperWithin(Radius, &Inside);
		bStalled = Inside == 0;
		if (Inside > 0) Progress += DeltaSeconds / FMath::Max(1.f, FillSeconds) * (1.f + 0.25f * static_cast<float>(Inside - 1));
		else Progress -= FMath::Max(0.f, D->Tune(TEXT("DefendDecayPerSecond"), 0.02f)) * DeltaSeconds;
		Progress = FMath::Clamp(Progress, 0.f, 1.f);
		if (Progress >= 1.f)
		{
			bDone = true;
			ApplyLook();
		}
		PressureTimer -= DeltaSeconds;
		if (PressureTimer <= 0.f)
		{
			PressureTimer = FMath::Max(1.f, D->Tune(TEXT("DefendPressureInterval"), 7.f));
			const int32 Count = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("DefendPressureCount"), 10.f) * CrewScale), 0, 60);
			if (GS->Horde && Count > 0 && !SpawnEnemy.IsNone()) GS->Horde->SpawnEnemyNear(SpawnEnemy, Here2D, Count, Radius + 900.f);
		}
		break;
	}
	case ESQObjectivePart::Cart:
	{
		const bool bEscorted = AnyReaperWithin(D->Tune(TEXT("EscortRadius"), 700.f));
		bStalled = !bEscorted;
		if (bEscorted && Path.Num() >= 2 && PathLength > 1.f)
		{
			const float TimeScale = SQMisData::SQMisTimeScale(D);
			PathDone = FMath::Min(PathLength, PathDone + FMath::Max(10.f, D->Tune(TEXT("EscortSpeed"), 160.f)) / FMath::Clamp(TimeScale, 0.5f, 1.f) * DeltaSeconds);
			// Walk the road to PathDone
			float Left = PathDone;
			FVector At = Path[0];
			FVector Heading = FVector::ForwardVector;
			for (int32 Seg = 0; Seg + 1 < Path.Num(); ++Seg)
			{
				const FVector SegDelta = Path[Seg + 1] - Path[Seg];
				const float SegLen = static_cast<float>(SegDelta.Size2D());
				Heading = SegDelta.GetSafeNormal2D();
				if (Left <= SegLen || Seg + 2 == Path.Num())
				{
					const float Alpha = SegLen > 1.f ? FMath::Clamp(Left / SegLen, 0.f, 1.f) : 1.f;
					At = Path[Seg] + SegDelta * Alpha;
					break;
				}
				Left -= SegLen;
			}
			At.Z = ASQArena::GroundZAt(this, static_cast<float>(At.X), static_cast<float>(At.Y));
			SetActorLocationAndRotation(At, Heading.IsNearlyZero() ? GetActorRotation() : Heading.Rotation());
			Progress = FMath::Clamp(PathDone / PathLength, 0.f, 1.f);
			if (Progress >= 1.f)
			{
				bDone = true;
				ApplyLook();
			}
		}
		PressureTimer -= DeltaSeconds;
		if (PressureTimer <= 0.f)
		{
			PressureTimer = FMath::Max(1.f, D->Tune(TEXT("EscortAmbushInterval"), 9.f));
			const int32 Count = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("EscortAmbushCount"), 8.f) * CrewScale), 0, 60);
			if (GS->Horde && Count > 0 && !SpawnEnemy.IsNone()) GS->Horde->SpawnEnemyNear(SpawnEnemy, Here2D, Count, 1300.f);
		}
		break;
	}
	case ESQObjectivePart::Urn:
	{
		const float Reach = FMath::Max(40.f, D->Tune(TEXT("UrnPickupRadius"), 160.f));
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Reaper = *It;
			if (!Reaper || Reaper->bDowned || FVector::Dist2D(Reaper->GetActorLocation(), Here) > Reach) continue;
			bDone = true;
			ApplyLook();
			if (ASQPlayerState* FinderPS = Reaper->GetPlayerState<ASQPlayerState>()) FinderPS->CreditQuest(TEXT("UrnsCollected"));
			Reaper->MulticastFX(0, Here, 160.f, FColor(255, 214, 90), 0.5f);
			break;
		}
		break;
	}
	default:
		break;
	}
}

// ================================================================ FSQMissionDirector (server)

ASQGameState* FSQMissionDirector::GetGS() const
{
	UWorld* W = World.Get();
	return W ? W->GetGameState<ASQGameState>() : nullptr;
}

int32 FSQMissionDirector::CrewSize() const
{
	const ASQGameState* GS = GetGS();
	return GS ? FMath::Max(1, GS->PlayerArray.Num()) : 1;
}

float FSQMissionDirector::CrewScale() const
{
	UWorld* W = World.Get();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	return 1.f + FMath::Max(0.f, D ? D->Tune(TEXT("PlayerSpawnRateBonus"), 0.35f) : 0.35f) * static_cast<float>(CrewSize() - 1);
}

void FSQMissionDirector::Begin(UWorld* InWorld, const FSQMissionDef* InMission, bool bInHard, float InStageDuration)
{
	End();
	World = InWorld;
	MissionId = InMission ? InMission->Id : NAME_None;
	const FSQMissionDef* Mission = InMission;
	bHard = bInHard;
	StageDuration = FMath::Max(10.f, InStageDuration);
	MinibossKills = 0;
	ViewTimer = 0.f;
	LastEndedAt = 0.f;
	Runs.Reset();
	MinibossFired.Reset();
	if (!Mission || !InWorld) return;
	const USQData* D = USQData::Get(InWorld);
	const float TimeScale = SQMisData::SQMisTimeScale(D);
	const float FirstDelay = (D ? D->Tune(TEXT("ObjectiveFirstDelay"), 20.f) : 20.f) * FMath::Min(1.f, TimeScale);
	const float Spread = FMath::Clamp(D ? D->Tune(TEXT("ObjectiveSpread"), 0.8f) : 0.8f, 0.1f, 1.f);
	const int32 Count = Mission->Objectives.Num();
	for (int32 ObjIdx = 0; ObjIdx < Count; ++ObjIdx)
	{
		FObjectiveRun& Run = Runs.AddDefaulted_GetRef();
		Run.Spec = Mission->Objectives[ObjIdx];
		Run.ScheduledAt = FirstDelay + StageDuration * Spread * static_cast<float>(ObjIdx) / static_cast<float>(FMath::Max(1, Count));
	}
	MinibossFired.Init(false, Mission->Minibosses.Num());
	BeginTrail();
	WriteViews(0.f);
}

// ---------------------------------------------------------------- lantern trail

int32 FSQMissionDirector::TrailQuotaFor(int32 Stage) const
{
	UWorld* W = World.Get();
	ASQGameState* GS = GetGS();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!D || !GS) return 20;
	// Souls at 4 reapers (solo SoloQuotaMult), growing per lantern; Quota Crunch and the test length scale it like the old objective
	const float Base = FMath::Max(1.f, D->Tune(TEXT("LanternQuota"), 40.f));
	const float Growth = FMath::Max(0.f, D->Tune(TEXT("LanternQuotaGrowth"), 0.5f));
	const float SoloMult = D->Tune(TEXT("SoloQuotaMult"), 0.5f);
	const float CrewMult = FMath::Lerp(SoloMult, 1.f, FMath::Clamp(static_cast<float>(CrewSize() - 1) / 3.f, 0.f, 1.f));
	const float TimeScale = SQMisData::SQMisTimeScale(D);
	// Longer maps ask for more (the same share of the stage's kills): Missions.csv Duration against LanternQuotaRefSeconds
	const FSQMissionDef* Mission = GetMission();
	const float LengthMult = Mission ? FMath::Clamp(Mission->Duration / FMath::Max(1.f, D->Tune(TEXT("LanternQuotaRefSeconds"), 600.f)), 0.25f, 4.f) : 1.f;
	return FMath::Max(5, FMath::RoundToInt(Base * (1.f + Growth * static_cast<float>(Stage)) * CrewMult * LengthMult * GS->ModProduct(&FSQModifierDef::QuotaMult) * FMath::Min(1.f, TimeScale)));
}

void FSQMissionDirector::BeginTrail()
{
	Trail = FTrail();
	UWorld* W = World.Get();
	ASQGameState* GS = GetGS();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!W || !GS || !D) return;
	GS->TrailSites.Reset();
	GS->TrailStage = 0;
	GS->TrailMissedMask = 0;
	GS->TrailBanked = 0;
	GS->TrailQuota = 0;
	const int32 Stages = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("LanternStages"), 4.f)), 1, 8);
	if (D->Tune(TEXT("LanternTrail"), 1.f) < 0.5f || !GS->Arena || !GS->Lantern) return;
	GS->Arena->GetLanternTrail(Stages, GS->Arena->RunSeed + TrailSeedSalt, Trail.Sites, Trail.Names);
	if (Trail.Sites.Num() == 0) return;
	Trail.bOn = true;
	Trail.LastGSBanked = GS->Banked;
	GS->TrailSites = Trail.Sites;
	LightTrailStage(0.f);
}

void FSQMissionDirector::LightTrailStage(float StageTime)
{
	ASQGameState* GS = GetGS();
	UWorld* W = World.Get();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!GS || !D || !Trail.Sites.IsValidIndex(Trail.Stage)) return;
	Trail.Quota = TrailQuotaFor(Trail.Stage);
	// Each lantern has its share of the stage clock; the last one burns until just before the stage ends
	const float Share = FMath::Clamp(D->Tune(TEXT("LanternDeadlineShare"), 0.94f), 0.3f, 1.f);
	Trail.Deadline = StageDuration * Share * static_cast<float>(Trail.Stage + 1) / static_cast<float>(Trail.Sites.Num());
	// Never less than a fair walk + fill from now (a lantern lit late still gets its minimum)
	Trail.Deadline = FMath::Max(Trail.Deadline, StageTime + FMath::Max(10.f, D->Tune(TEXT("LanternMinSeconds"), 45.f)) * FMath::Clamp(SQMisData::SQMisTimeScale(D), 0.5f, 1.f));
	if (GS->Lantern) GS->Lantern->SetActorLocation(Trail.Sites[Trail.Stage]);
	GS->TrailStage = Trail.Stage;
	// Every lantern banked or burnt out makes the sacks bigger (ASQCharacter::RecomputeStats reads TrailStage)
	for (TActorIterator<ASQCharacter> It(W); It; ++It) It->RecomputeStats();
	GS->TrailQuota = Trail.Quota;
	GS->TrailBanked = FMath::Min(Trail.Banked, Trail.Quota);
}

void FSQMissionDirector::TickTrail(float StageTime)
{
	ASQGameState* GS = GetGS();
	if (!GS || !Trail.bOn) return;
	// Everything banked at the lit lantern counts (the game mode turns the same souls into gold)
	const int32 Now = GS->Banked;
	if (Now > Trail.LastGSBanked && !Trail.bDone) Trail.Banked += Now - Trail.LastGSBanked;
	Trail.LastGSBanked = Now;
	if (Trail.bDone) return;
	GS->TrailBanked = FMath::Min(Trail.Banked, Trail.Quota);
	if (GS->Phase != ESQPhase::Night) return;
	if (Trail.Banked >= Trail.Quota) FinishTrailStage(true, StageTime);
	else if (StageTime >= Trail.Deadline) FinishTrailStage(false, StageTime);
}

void FSQMissionDirector::FinishTrailStage(bool bBanked, float StageTime)
{
	UWorld* W = World.Get();
	ASQGameState* GS = GetGS();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!W || !GS || !D || !Trail.bOn || Trail.bDone) return;
	const int32 Done = Trail.Stage;
	const FVector At = Trail.Sites.IsValidIndex(Done) ? Trail.Sites[Done] : FVector::ZeroVector;
	const bool bLast = Done >= Trail.Sites.Num() - 1;
	FString RewardText;
	if (bBanked)
	{
		++Trail.Lit;
		// Reward, quick: a free chest each beside the spent lantern; the last lantern gives everyone an augment pick instead
		if (bLast && D->Tune(TEXT("LanternFinalAugment"), 1.f) > 0.5f)
		{
			USQAugments::GrantAugmentPickAll(W, ESQAugmentSource::Objective);
			RewardText = TEXT(" Pick an AUGMENT!");
		}
		else
		{
			const int32 Crew = FMath::Clamp(CrewSize(), 1, 8);
			for (int32 ChestIdx = 0; ChestIdx < Crew; ++ChestIdx)
			{
				const float Angle = 2.f * PI * static_cast<float>(ChestIdx) / static_cast<float>(Crew) + 0.6f;
				FVector ChestAt(At.X + FMath::Cos(Angle) * 560.f, At.Y + FMath::Sin(Angle) * 560.f, At.Z);
				if (GS->Arena) ChestAt = GS->Arena->FindLanternSpot(static_cast<float>(ChestAt.X), static_cast<float>(ChestAt.Y), 100.f);
				ASQChest::SpawnChest(W, ChestAt, true);
			}
			RewardText = TEXT(" Free chests!");
		}
		const int32 Gold = FMath::Max(0, FMath::RoundToInt(FMath::Max(0.f, D->Tune(TEXT("LanternGold"), 20.f)) * GS->GoldNightGrowth() * GS->GoldMultiplier()));
		for (APlayerState* EachPS : GS->PlayerArray)
		{
			ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS);
			if (!EachSQ) continue;
			if (Gold > 0) EachSQ->AddGold(Gold);
			// The old Bank Souls objective quests count lanterns now
			EachSQ->CreditQuest(TEXT("Obj_BankSouls"));
			EachSQ->CreditQuest(TEXT("ObjectivesDone"));
		}
	}
	else
	{
		GS->TrailMissedMask |= (1 << FMath::Clamp(Done, 0, 30));
	}
	// The dead answer every lantern: the horde steps up for the rest of the run, lit or burnt out
	GS->AddDifficulty(FMath::Max(0.f, D->Tune(TEXT("LanternDifficulty"), 0.06f)));

	// Souls over the quota are not wasted: up to half the next quota carries on
	const int32 Spare = FMath::Max(0, Trail.Banked - Trail.Quota);
	++Trail.Stage;
	if (Trail.Stage >= Trail.Sites.Num())
	{
		Trail.bDone = true;
		Trail.Banked = 0;
		GS->TrailStage = Trail.Sites.Num();
		GS->TrailBanked = 0;
		GS->TrailQuota = 0;
		GS->PushBeat(bBanked ? FString::Printf(TEXT("THE LAST LANTERN IS BANKED!%s The trail is walked: finish the objectives and ring the bell."), *RewardText)
			: FString(TEXT("The last lantern burnt out. The trail ends here: finish the objectives and ring the bell.")), bBanked ? 6 : 7);
		return;
	}
	Trail.Banked = bBanked ? FMath::Min(Spare, TrailQuotaFor(Trail.Stage) / 2) : Trail.Banked / 2;
	LightTrailStage(StageTime);
	const FString Where = Trail.Names.IsValidIndex(Trail.Stage) ? Trail.Names[Trail.Stage] : FString(TEXT("the next district"));
	GS->PushBeat(bBanked ? FString::Printf(TEXT("LANTERN BANKED!%s The next lantern is lit in %s - follow the beam."), *RewardText, *Where)
		: FString::Printf(TEXT("The lantern burnt out (no reward). The next one is lit in %s - follow the beam."), *Where), bBanked ? 6 : 7);
}

void FSQMissionDirector::End()
{
	for (FObjectiveRun& Run : Runs)
	{
		for (const TWeakObjectPtr<ASQObjective>& Piece : Run.Parts)
		{
			if (Piece.IsValid()) Piece->Destroy();
		}
		Run.Parts.Reset();
	}
	Trail = FTrail();
	MissionId = NAME_None;
}

bool FSQMissionDirector::AllResolved() const
{
	for (const FObjectiveRun& Run : Runs)
	{
		if (Run.State < 2) return false;
	}
	return !Trail.bOn || Trail.bDone;
}

int32 FSQMissionDirector::NumDone() const
{
	int32 Done = 0;
	for (const FObjectiveRun& Run : Runs) Done += Run.State == 2 ? 1 : 0;
	return Done;
}

int32 FSQMissionDirector::NumFailed() const
{
	int32 Failed = 0;
	for (const FObjectiveRun& Run : Runs) Failed += Run.State == 3 ? 1 : 0;
	return Failed;
}

void FSQMissionDirector::FailRemaining()
{
	ASQGameState* GS = GetGS();
	const float Now = GS ? GS->StageTime : 0.f;
	for (int32 ObjIdx = 0; ObjIdx < Runs.Num(); ++ObjIdx)
	{
		if (Runs[ObjIdx].State < 2) FinishObjective(ObjIdx, false, Now);
	}
	// The clock ran out: lanterns still unlit stay dark (the lit one stays where it is for spare souls)
	if (Trail.bOn && !Trail.bDone && GS)
	{
		for (int32 Left = Trail.Stage; Left < Trail.Sites.Num(); ++Left) GS->TrailMissedMask |= (1 << FMath::Clamp(Left, 0, 30));
		Trail.bDone = true;
		GS->TrailQuota = 0;
		GS->TrailBanked = 0;
	}
	WriteViews(Now);
}

void FSQMissionDirector::Tick(float DeltaSeconds, float StageTime)
{
	if (!GetMission()) return;
	UWorld* W = World.Get();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!W || !D) return;

	// The active objective (one at a time), then the next one when its time comes
	int32 Active = INDEX_NONE;
	int32 NextPending = INDEX_NONE;
	for (int32 ObjIdx = 0; ObjIdx < Runs.Num(); ++ObjIdx)
	{
		if (Runs[ObjIdx].State == 1) { Active = ObjIdx; break; }
		if (Runs[ObjIdx].State == 0) { NextPending = ObjIdx; break; }
	}
	if (Active != INDEX_NONE)
	{
		TickObjective(Active, DeltaSeconds, StageTime);
	}
	else if (NextPending != INDEX_NONE)
	{
		const float Gap = D->Tune(TEXT("ObjectiveGap"), 15.f) * FMath::Min(1.f, SQMisData::SQMisTimeScale(D));
		const float Early = FMath::Clamp(D->Tune(TEXT("ObjectiveEarlyFactor"), 0.6f), 0.f, 1.f);
		// Finishing fast pulls the next objective forward (to Early x its schedule); the first one keeps its delay
		const float Scheduled = Runs[NextPending].ScheduledAt * (NextPending == 0 ? 1.f : Early);
		const float ReadyAt = FMath::Max(Scheduled, NextPending == 0 ? 0.f : LastEndedAt + Gap);
		if (StageTime >= ReadyAt) StartObjective(NextPending, StageTime);
	}

	TickTrail(StageTime);
	TickMinibosses(StageTime);

	ViewTimer -= DeltaSeconds;
	if (ViewTimer <= 0.f)
	{
		ViewTimer = 0.25f;
		WriteViews(StageTime);
	}
}

void FSQMissionDirector::TickMinibosses(float StageTime)
{
	ASQGameState* GS = GetGS();
	const FSQMissionDef* Mission = GetMission();
	if (!Mission || !GS || !GS->Horde || GS->Phase != ESQPhase::Night) return;
	for (int32 MiniIdx = 0; MiniIdx < Mission->Minibosses.Num() && MiniIdx < MinibossFired.Num(); ++MiniIdx)
	{
		if (MinibossFired[MiniIdx]) continue;
		const float At = Mission->MinibossAt.IsValidIndex(MiniIdx) ? Mission->MinibossAt[MiniIdx] : 0.5f;
		if (StageTime < At * StageDuration) continue;
		MinibossFired[MiniIdx] = true;
		const FSQMissionMiniboss& Mini = Mission->Minibosses[MiniIdx];
		const bool bLast = MiniIdx == Mission->Minibosses.Num() - 1;
		GS->Horde->SpawnMiniboss(Mini.Enemy, Mini.HealthMult, Mini.DamageMult, Mini.Name, bLast);
		GS->PushBeat(FString::Printf(TEXT("MINIBOSS: %s! Beat it for an AUGMENT."), *Mini.Name), 1);
	}
}

FVector FSQMissionDirector::RandomGround(float MinDistFromCentre) const
{
	UWorld* W = World.Get();
	const ASQGameState* GS = GetGS();
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	const USQData* D = W ? USQData::Get(W) : nullptr;
	const float Half = Arena ? Arena->GetArenaHalf() : (D ? D->Tune(TEXT("ArenaHalfSize"), 6000.f) : 6000.f);
	// FindLanternSpot searches outwards from the random point for clear ground (no retry loop needed)
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Dist = FMath::FRandRange(FMath::Min(MinDistFromCentre, Half * 0.7f), Half * 0.75f);
	const float CandX = FMath::Cos(Angle) * Dist;
	const float CandY = FMath::Sin(Angle) * Dist;
	if (Arena) return Arena->FindLanternSpot(CandX, CandY, 200.f);
	return FVector(CandX, CandY, ASQArena::GroundZAt(W, CandX, CandY));
}

void FSQMissionDirector::FindSpots(FName Type, int32 Count, TArray<FVector>& Out) const
{
	Out.Reset();
	const ASQGameState* GS = GetGS();
	const ASQArena* Arena = GS ? GS->Arena.Get() : nullptr;
	// Three-map remaster: nests, elites and urns gather round the LIT lantern (they used to be anywhere on the map and
	// pulled the squad away from the trail). Ask for the whole pool, keep the Count nearest the lit lantern.
	const bool bNearTrail = Arena && GS->TrailSites.IsValidIndex(GS->TrailStage)
		&& (Type == FName(TEXT("DestroySpawners")) || Type == FName(TEXT("KillElite")) || Type == FName(TEXT("CollectItems")));
	if (bNearTrail)
	{
		Arena->GetObjectiveSpots(Type, 64, Out);
		const FVector2D Lit(GS->TrailSites[GS->TrailStage].X, GS->TrailSites[GS->TrailStage].Y);
		Out.Sort([&Lit](const FVector& A, const FVector& B)
		{
			return FVector2D::DistSquared(FVector2D(A.X, A.Y), Lit) < FVector2D::DistSquared(FVector2D(B.X, B.Y), Lit);
		});
	}
	else if (Arena) Arena->GetObjectiveSpots(Type, Count, Out); // false = fewer than Count found: topped up below
	while (Out.Num() < Count) Out.Add(RandomGround(1800.f));
	if (Out.Num() > Count) Out.SetNum(Count);
}

void FSQMissionDirector::StartObjective(int32 Index, float StageTime)
{
	UWorld* W = World.Get();
	ASQGameState* GS = GetGS();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!W || !GS || !D || !Runs.IsValidIndex(Index)) return;
	FObjectiveRun& Run = Runs[Index];
	Run.State = 1;
	Run.StartedAt = StageTime;
	Run.Value = 0.f;
	Run.Timer = 0.f;
	Run.Parts.Reset();
	const float TimeScale = SQMisData::SQMisTimeScale(D);
	const FString LimitKey = FString::Printf(TEXT("ObjectiveTime%s"), *Run.Spec.TypeName.ToString());
	// Test lengths compress the objectives too, but never their time limits below half (walking across the map takes what it takes)
	Run.TimeLimit = FMath::Max(0.f, D->Tune(FName(*LimitKey), 150.f)) * FMath::Clamp(TimeScale, 0.5f, 1.f);
	const int32 Act = FMath::Clamp(GS->NightIndex, 0, 2);
	const FSQMissionDef* Mission = GetMission();
	const float MissionHP = Mission ? Mission->Mult(Mission->HealthMult, Mission->HardHealthMult, bHard) : 1.f;

	switch (Run.Spec.Type)
	{
	case ESQObjectiveType::BankSouls:
	{
		// The old night quota, as one objective: bank N souls at the lantern (solo 50% -> 100% at 4 reapers, Quota Crunch, test length)
		const float SoloMult = D->Tune(TEXT("SoloQuotaMult"), 0.5f);
		const float CrewMult = FMath::Lerp(SoloMult, 1.f, FMath::Clamp(static_cast<float>(CrewSize() - 1) / 3.f, 0.f, 1.f));
		Run.Target = FMath::Max(10, FMath::RoundToInt(static_cast<float>(Run.Spec.Count) * CrewMult * GS->ModProduct(&FSQModifierDef::QuotaMult) * FMath::Min(1.f, TimeScale)));
		GS->Quota = Run.Target;
		GS->Banked = 0;
		Run.Anchor = GS->Lantern ? GS->Lantern->GetActorLocation() : FVector::ZeroVector;
		if (ASQObjective* Beacon = ASQObjective::SpawnPart(W, ESQObjectivePart::Beacon, Run.Anchor, 420.f)) Run.Parts.Add(Beacon);
		break;
	}
	case ESQObjectiveType::DestroySpawners:
	{
		TArray<FVector> Spots;
		FindSpots(Run.Spec.TypeName, Run.Spec.Count, Spots);
		const float NestHP = FMath::Max(50.f, D->Tune(TEXT("NestHealth"), 1500.f)) * (1.f + FMath::Max(0.f, D->Tune(TEXT("NestHealthPerAct"), 1.f)) * static_cast<float>(Act))
			* (1.f + FMath::Max(0.f, D->Tune(TEXT("EnemyHealthPerPlayer"), 0.35f)) * static_cast<float>(CrewSize() - 1)) * MissionHP * FMath::Min(1.f, TimeScale);
		for (const FVector& Spot : Spots)
		{
			ASQObjective* Nest = ASQObjective::SpawnPart(W, ESQObjectivePart::Nest, Spot, FMath::Max(80.f, D->Tune(TEXT("NestRadius"), 260.f)));
			if (!Nest) continue;
			Nest->MaxHealth = NestHP;
			Nest->Health = NestHP;
			Nest->SpawnEnemy = Run.Spec.Enemy;
			Nest->PressureTimer = FMath::FRandRange(0.5f, 3.f);
			Run.Parts.Add(Nest);
		}
		Run.Target = FMath::Max(1, Run.Parts.Num());
		Run.Anchor = Spots.Num() > 0 ? Spots[0] : FVector::ZeroVector;
		break;
	}
	case ESQObjectiveType::DefendPoint:
	{
		TArray<FVector> Spots;
		FindSpots(Run.Spec.TypeName, 1, Spots);
		Run.Anchor = Spots.Num() > 0 ? Spots[0] : FVector::ZeroVector;
		if (ASQObjective* Zone = ASQObjective::SpawnPart(W, ESQObjectivePart::Zone, Run.Anchor, FMath::Max(150.f, D->Tune(TEXT("DefendRadius"), 600.f))))
		{
			Zone->FillSeconds = FMath::Max(3.f, static_cast<float>(Run.Spec.Count) * FMath::Min(1.f, TimeScale));
			Zone->SpawnEnemy = Run.Spec.Enemy;
			Zone->PressureTimer = 2.f;
			Run.Parts.Add(Zone);
		}
		Run.Target = 1;
		break;
	}
	case ESQObjectiveType::Escort:
	{
		TArray<FVector> Road;
		const bool bRoad = GS->Arena && GS->Arena->GetEscortPath(Road) && Road.Num() >= 2;
		if (!bRoad)
		{
			// No road from the map: lantern -> four clear spots round the ring
			Road.Reset();
			Road.Add(GS->Lantern ? GS->Lantern->GetActorLocation() : FVector::ZeroVector);
			const float StartAngle = FMath::FRandRange(0.f, 2.f * PI);
			const float Half = GS->Arena ? GS->Arena->GetArenaHalf() : D->Tune(TEXT("ArenaHalfSize"), 6000.f);
			for (int32 Step = 1; Step <= 4; ++Step)
			{
				const float Angle = StartAngle + 0.9f * static_cast<float>(Step);
				const float CandX = FMath::Cos(Angle) * Half * 0.5f;
				const float CandY = FMath::Sin(Angle) * Half * 0.5f;
				Road.Add(GS->Arena ? GS->Arena->FindLanternSpot(CandX, CandY, 200.f) : FVector(CandX, CandY, 0.f));
			}
		}
		float Length = 0.f;
		for (int32 Seg = 0; Seg + 1 < Road.Num(); ++Seg) Length += static_cast<float>(FVector::Dist2D(Road[Seg], Road[Seg + 1]));
		Run.Anchor = Road[0];
		if (ASQObjective* Cart = ASQObjective::SpawnPart(W, ESQObjectivePart::Cart, Road[0], FMath::Max(150.f, D->Tune(TEXT("EscortRadius"), 700.f))))
		{
			Cart->Path = Road;
			Cart->PathLength = FMath::Max(1.f, Length);
			Cart->PathDone = 0.f;
			Cart->SpawnEnemy = Run.Spec.Enemy;
			Cart->PressureTimer = 4.f;
			Run.Parts.Add(Cart);
		}
		Run.Target = 1;
		break;
	}
	case ESQObjectiveType::CollectItems:
	{
		TArray<FVector> Spots;
		FindSpots(Run.Spec.TypeName, Run.Spec.Count, Spots);
		for (const FVector& Spot : Spots)
		{
			if (ASQObjective* Urn = ASQObjective::SpawnPart(W, ESQObjectivePart::Urn, Spot, 120.f)) Run.Parts.Add(Urn);
		}
		Run.Target = FMath::Max(1, Run.Parts.Num());
		Run.Anchor = Spots.Num() > 0 ? Spots[0] : FVector::ZeroVector;
		break;
	}
	case ESQObjectiveType::SurviveWave:
	{
		TArray<FVector> Spots;
		FindSpots(Run.Spec.TypeName, 1, Spots);
		Run.Anchor = Spots.Num() > 0 ? Spots[0] : FVector::ZeroVector;
		if (ASQObjective* Beacon = ASQObjective::SpawnPart(W, ESQObjectivePart::Beacon, Run.Anchor, 500.f)) Run.Parts.Add(Beacon);
		Run.Target = FMath::Max(5, FMath::RoundToInt(static_cast<float>(Run.Spec.Count) * FMath::Min(1.f, TimeScale)));
		Run.Timer = 0.f; // first burst at once
		break;
	}
	case ESQObjectiveType::KillElite:
	{
		TArray<FVector> Spots;
		FindSpots(Run.Spec.TypeName, 1, Spots);
		Run.Anchor = Spots.Num() > 0 ? Spots[0] : FVector::ZeroVector;
		if (ASQObjective* Beacon = ASQObjective::SpawnPart(W, ESQObjectivePart::Beacon, Run.Anchor, 600.f)) Run.Parts.Add(Beacon);
		const int32 EliteType = D->FindEnemyIndex(Run.Spec.Enemy);
		if (GS->Horde && EliteType != INDEX_NONE)
		{
			for (int32 EliteIdx = 0; EliteIdx < Run.Spec.Count; ++EliteIdx)
			{
				const float Angle = 2.f * PI * static_cast<float>(EliteIdx) / static_cast<float>(FMath::Max(1, Run.Spec.Count));
				const FVector2D At(Run.Anchor.X + FMath::Cos(Angle) * 350.f, Run.Anchor.Y + FMath::Sin(Angle) * 350.f);
				GS->Horde->SpawnEnemyEx(EliteType, At, false, FMath::Max(0.1f, D->Tune(TEXT("KillEliteHealthMult"), 4.f)),
					FMath::Max(0.1f, D->Tune(TEXT("KillEliteDamageMult"), 1.3f)), ASQHordeManager::EcoCrowned);
			}
		}
		Run.Target = FMath::Max(1, Run.Spec.Count);
		break;
	}
	default:
		Run.Target = 1;
		break;
	}

	// Quest NoHitNights now means "an objective without taking a hit"
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS)) EachSQ->bTrackHitThisNight = false;
	}
	WriteViews(StageTime);
	const FString Title = GS->Objectives.IsValidIndex(Index) ? GS->Objectives[Index].Title.ToString() : Run.Spec.TypeName.ToString();
	GS->PushBeat(FString::Printf(TEXT("NEW OBJECTIVE: %s"), *Title), 5);
}

void FSQMissionDirector::TickObjective(int32 Index, float DeltaSeconds, float StageTime)
{
	UWorld* W = World.Get();
	ASQGameState* GS = GetGS();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!GS || !D || !Runs.IsValidIndex(Index)) return;
	FObjectiveRun& Run = Runs[Index];
	bool bSuccess = false;

	auto CountDone = [&Run]()
	{
		int32 Done = 0;
		for (const TWeakObjectPtr<ASQObjective>& Piece : Run.Parts) Done += (!Piece.IsValid() || Piece->bDone) ? 1 : 0;
		return Done;
	};

	switch (Run.Spec.Type)
	{
	case ESQObjectiveType::BankSouls:
		Run.Value = static_cast<float>(FMath::Max(0, GS->Banked));
		bSuccess = GS->Banked >= Run.Target;
		break;
	case ESQObjectiveType::DestroySpawners:
	case ESQObjectiveType::CollectItems:
		Run.Value = static_cast<float>(CountDone());
		bSuccess = Run.Value >= static_cast<float>(Run.Target);
		break;
	case ESQObjectiveType::DefendPoint:
	case ESQObjectiveType::Escort:
	{
		const ASQObjective* Piece = Run.Parts.Num() > 0 ? Run.Parts[0].Get() : nullptr;
		Run.Value = Piece ? Piece->Progress : 1.f;
		bSuccess = !Piece || Piece->bDone;
		if (Piece && Run.Spec.Type == ESQObjectiveType::Escort) Run.Anchor = Piece->GetActorLocation();
		break;
	}
	case ESQObjectiveType::SurviveWave:
	{
		Run.Value = StageTime - Run.StartedAt;
		Run.Timer -= DeltaSeconds;
		if (Run.Timer <= 0.f && GS->Horde && GS->Phase == ESQPhase::Night)
		{
			Run.Timer = FMath::Max(1.f, D->Tune(TEXT("SurviveWaveInterval"), 8.f));
			GS->Horde->SpawnSwarm(Run.Spec.Enemy, FMath::Max(1, FMath::RoundToInt(D->Tune(TEXT("SurviveWaveCount"), 18.f))));
		}
		bSuccess = Run.Value >= static_cast<float>(Run.Target);
		break;
	}
	case ESQObjectiveType::KillElite:
		bSuccess = Run.Value >= static_cast<float>(Run.Target); // NotifyKill counts
		break;
	default:
		bSuccess = true;
		break;
	}

	// The arrow follows the nearest unfinished part (nest / urn)
	if (Run.Spec.Type == ESQObjectiveType::DestroySpawners || Run.Spec.Type == ESQObjectiveType::CollectItems)
	{
		for (const TWeakObjectPtr<ASQObjective>& Piece : Run.Parts)
		{
			if (Piece.IsValid() && !Piece->bDone) { Run.Anchor = Piece->GetActorLocation(); break; }
		}
	}

	if (bSuccess) FinishObjective(Index, true, StageTime);
	else if (Run.TimeLimit > 0.f && StageTime - Run.StartedAt >= Run.TimeLimit) FinishObjective(Index, false, StageTime);
}

void FSQMissionDirector::FinishObjective(int32 Index, bool bSuccess, float StageTime)
{
	ASQGameState* GS = GetGS();
	if (!GS || !Runs.IsValidIndex(Index)) return;
	FObjectiveRun& Run = Runs[Index];
	if (Run.State >= 2) return;
	Run.State = bSuccess ? 2 : 3;
	LastEndedAt = StageTime;
	if (Run.Spec.Type == ESQObjectiveType::BankSouls) GS->Quota = 0;
	for (const TWeakObjectPtr<ASQObjective>& Piece : Run.Parts)
	{
		if (Piece.IsValid()) Piece->Destroy();
	}
	Run.Parts.Reset();
	WriteViews(StageTime);
	const FString Title = GS->Objectives.IsValidIndex(Index) ? GS->Objectives[Index].Title.ToString() : Run.Spec.TypeName.ToString();

	if (bSuccess)
	{
		GrantReward(Run);
		const FName TypeStat(*FString::Printf(TEXT("Obj_%s"), *Run.Spec.TypeName.ToString()));
		for (APlayerState* EachPS : GS->PlayerArray)
		{
			ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS);
			if (!EachSQ) continue;
			EachSQ->CreditQuest(TEXT("ObjectivesDone"));
			EachSQ->CreditQuest(TypeStat);
			if (!EachSQ->bTrackHitThisNight && EachSQ->GetPawn()) EachSQ->CreditQuest(TEXT("NoHitNights"));
		}
		const TCHAR* RewardText = TEXT("");
		switch (Run.Spec.Reward)
		{
		case ESQObjectiveReward::Gold: RewardText = TEXT(" +GOLD for everyone"); break;
		case ESQObjectiveReward::Chest: RewardText = TEXT(" Free chests!"); break;
		case ESQObjectiveReward::Augment: RewardText = TEXT(" Pick an AUGMENT!"); break;
		case ESQObjectiveReward::XP: RewardText = TEXT(" +XP"); break;
		default: break;
		}
		GS->PushBeat(FString::Printf(TEXT("OBJECTIVE COMPLETE: %s.%s"), *Title, RewardText), 6);
	}
	else
	{
		// Failing costs the boss fight: every failed objective is a Defiance stack (the old missed-quota rule)
		GS->Defiance++;
		GS->PushBeat(FString::Printf(TEXT("OBJECTIVE FAILED: %s. The boss gains DEFIANCE (x%d)."), *Title, GS->Defiance), 7);
	}
}

void FSQMissionDirector::GrantReward(const FObjectiveRun& Run)
{
	UWorld* W = World.Get();
	ASQGameState* GS = GetGS();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	if (!W || !GS || !D) return;
	switch (Run.Spec.Reward)
	{
	case ESQObjectiveReward::Gold:
	{
		const int32 Gold = FMath::Max(1, FMath::RoundToInt(FMath::Max(0.f, D->Tune(TEXT("ObjectiveGold"), 40.f)) * GS->GoldNightGrowth() * GS->GoldMultiplier()));
		for (APlayerState* EachPS : GS->PlayerArray)
		{
			if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS)) EachSQ->AddGold(Gold);
		}
		break;
	}
	case ESQObjectiveReward::Chest:
	{
		const int32 Crew = FMath::Clamp(CrewSize(), 1, 8);
		for (int32 ChestIdx = 0; ChestIdx < Crew; ++ChestIdx)
		{
			const float Angle = 2.f * PI * static_cast<float>(ChestIdx) / static_cast<float>(Crew);
			const float Spread = Crew > 1 ? 180.f : 0.f;
			FVector At(Run.Anchor.X + FMath::Cos(Angle) * Spread, Run.Anchor.Y + FMath::Sin(Angle) * Spread, Run.Anchor.Z);
			if (GS->Arena) At = GS->Arena->FindLanternSpot(static_cast<float>(At.X), static_cast<float>(At.Y), 100.f);
			ASQChest::SpawnChest(W, At, true);
		}
		break;
	}
	case ESQObjectiveReward::Augment:
		USQAugments::GrantAugmentPickAll(W, ESQAugmentSource::Objective);
		break;
	case ESQObjectiveReward::XP:
		GS->AddTeamXP(FMath::Max(1, FMath::RoundToInt(static_cast<float>(GS->XPToNext) * FMath::Clamp(D->Tune(TEXT("ObjectiveXPLevels"), 0.6f), 0.f, 5.f))));
		break;
	default:
		break;
	}
}

void FSQMissionDirector::NotifyKill(int32 EnemyType, uint8 EcoFlags)
{
	UWorld* W = World.Get();
	if (!GetMission() || !W) return;
	const USQData* D = USQData::Get(W);
	if ((EcoFlags & ASQHordeManager::EcoMiniboss) != 0)
	{
		MinibossKills++;
		if (MinibossKills == 1) USQAugments::GrantAugmentPickAll(W, ESQAugmentSource::Miniboss1);
		else if (MinibossKills == 2) USQAugments::GrantAugmentPickAll(W, ESQAugmentSource::Miniboss2);
	}
	const bool bElite = (EcoFlags & ASQHordeManager::EcoCrowned) != 0
		|| (D && D->Enemies.IsValidIndex(EnemyType) && D->Enemies[EnemyType].Behaviour == ESQEnemyBehaviour::Elite);
	if (!bElite) return;
	for (FObjectiveRun& Run : Runs)
	{
		if (Run.State == 1 && Run.Spec.Type == ESQObjectiveType::KillElite) Run.Value += 1.f;
	}
}

void FSQMissionDirector::NotifyHitAt(const FVector2D& Pos, float Damage)
{
	if (MissionId.IsNone() || !(Damage > 0.f)) return;
	UWorld* W = World.Get();
	const USQData* D = W ? USQData::Get(W) : nullptr;
	const float Share = D ? FMath::Max(0.f, D->Tune(TEXT("NestHitShare"), 0.5f)) : 0.5f;
	if (Share <= 0.f) return;
	for (FObjectiveRun& Run : Runs)
	{
		if (Run.State != 1 || Run.Spec.Type != ESQObjectiveType::DestroySpawners) continue;
		for (const TWeakObjectPtr<ASQObjective>& Piece : Run.Parts)
		{
			ASQObjective* Nest = Piece.Get();
			if (!Nest || Nest->bDone) continue;
			const FVector NestAt = Nest->GetActorLocation();
			if (FVector2D::DistSquared(Pos, FVector2D(NestAt.X, NestAt.Y)) <= FMath::Square(static_cast<double>(Nest->Radius) + 120.0)) Nest->DamageNest(Damage * Share);
		}
	}
}

void FSQMissionDirector::WriteViews(float StageTime)
{
	ASQGameState* GS = GetGS();
	if (!GS) return;
	TArray<FSQObjectiveView> Views;
	Views.Reserve(Runs.Num());
	for (const FObjectiveRun& Run : Runs)
	{
		FSQObjectiveView& View = Views.AddDefaulted_GetRef();
		View.Type = Run.Spec.TypeName;
		View.Reward = Run.Spec.RewardName;
		View.bActive = Run.State == 1;
		View.bDone = Run.State == 2;
		View.bFailed = Run.State == 3;
		View.WorldLocation = Run.Anchor;
		const int32 Target = Run.State == 0 ? Run.Spec.Count : Run.Target;
		const int32 ValueInt = FMath::FloorToInt(Run.Value + 0.001f);
		FString Title;
		FString Detail;
		float Prog = 0.f;
		switch (Run.Spec.Type)
		{
		case ESQObjectiveType::BankSouls:
			Title = FString::Printf(TEXT("Bank %d souls at the lantern"), Target);
			Detail = FString::Printf(TEXT("%d / %d banked"), FMath::Min(ValueInt, Target), Target);
			Prog = Target > 0 ? Run.Value / static_cast<float>(Target) : 0.f;
			break;
		case ESQObjectiveType::DestroySpawners:
			Title = FString::Printf(TEXT("Destroy the %d spawner nests"), Target);
			Detail = FString::Printf(TEXT("%d / %d destroyed"), ValueInt, Target);
			Prog = Target > 0 ? Run.Value / static_cast<float>(Target) : 0.f;
			break;
		case ESQObjectiveType::DefendPoint:
		{
			Title = TEXT("Defend the point");
			const ASQObjective* Zone = Run.Parts.Num() > 0 ? Run.Parts[0].Get() : nullptr;
			Detail = (Zone && Zone->bStalled) ? FString::Printf(TEXT("Zone empty! %d%% held"), FMath::RoundToInt(Run.Value * 100.f))
				: FString::Printf(TEXT("Hold the zone: %d%%"), FMath::RoundToInt(Run.Value * 100.f));
			Prog = Run.Value;
			break;
		}
		case ESQObjectiveType::Escort:
		{
			Title = TEXT("Escort the hearse");
			const ASQObjective* Cart = Run.Parts.Num() > 0 ? Run.Parts[0].Get() : nullptr;
			Detail = (Cart && Cart->bStalled) ? FString::Printf(TEXT("The hearse waits for you (%d%%)"), FMath::RoundToInt(Run.Value * 100.f))
				: FString::Printf(TEXT("%d%% of the road"), FMath::RoundToInt(Run.Value * 100.f));
			Prog = Run.Value;
			break;
		}
		case ESQObjectiveType::CollectItems:
			Title = FString::Printf(TEXT("Collect %d soul urns"), Target);
			Detail = FString::Printf(TEXT("%d / %d collected"), ValueInt, Target);
			Prog = Target > 0 ? Run.Value / static_cast<float>(Target) : 0.f;
			break;
		case ESQObjectiveType::SurviveWave:
			Title = TEXT("Survive the wave");
			Detail = FString::Printf(TEXT("%d s left"), FMath::Max(0, FMath::CeilToInt(static_cast<float>(Target) - Run.Value)));
			Prog = Target > 0 ? Run.Value / static_cast<float>(Target) : 0.f;
			break;
		case ESQObjectiveType::KillElite:
			Title = FString::Printf(TEXT("Hunt %d crowned elites"), Target);
			Detail = FString::Printf(TEXT("%d / %d slain"), FMath::Min(ValueInt, Target), Target);
			Prog = Target > 0 ? Run.Value / static_cast<float>(Target) : 0.f;
			break;
		default:
			Title = Run.Spec.TypeName.ToString();
			break;
		}
		if (Run.State == 0) Detail = TEXT("Coming up");
		else if (Run.State == 2) { Detail = TEXT("Complete"); Prog = 1.f; }
		else if (Run.State == 3) Detail = TEXT("Failed");
		View.Title = FText::FromString(Title);
		View.Detail = FText::FromString(Detail);
		View.Progress = FMath::Clamp(Prog, 0.f, 1.f);
		View.TimeLeft = (Run.State == 1 && Run.TimeLimit > 0.f) ? FMath::Max(0.f, Run.TimeLimit - (StageTime - Run.StartedAt)) : -1.f;
	}
	// The lantern trail is the last row (objective indices stay the runs' indices); the HUD draws it first
	if (Trail.bOn)
	{
		FSQObjectiveView& View = Views.AddDefaulted_GetRef();
		View.Type = FName(TEXT("LanternTrail"));
		const int32 Total = Trail.Sites.Num();
		if (Trail.bDone)
		{
			View.bDone = true;
			View.Progress = 1.f;
			View.Title = FText::FromString(FString::Printf(TEXT("Lantern trail: %d of %d banked"), Trail.Lit, Total));
			View.Detail = FText::FromString(TEXT("Complete"));
		}
		else
		{
			const bool bLast = Trail.Stage >= Total - 1;
			View.bActive = true;
			View.Reward = FName(bLast ? TEXT("Augment") : TEXT("Chest"));
			View.WorldLocation = Trail.Sites.IsValidIndex(Trail.Stage) ? Trail.Sites[Trail.Stage] : FVector::ZeroVector;
			const FString Where = Trail.Names.IsValidIndex(Trail.Stage) ? Trail.Names[Trail.Stage] : FString(TEXT("the district"));
			View.Title = FText::FromString(FString::Printf(TEXT("Lantern %d of %d: bank %d souls in %s"), Trail.Stage + 1, Total, Trail.Quota, *Where));
			View.Detail = FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::Min(Trail.Banked, Trail.Quota), Trail.Quota));
			View.Progress = Trail.Quota > 0 ? FMath::Clamp(static_cast<float>(Trail.Banked) / static_cast<float>(Trail.Quota), 0.f, 1.f) : 0.f;
			View.TimeLeft = FMath::Max(0.f, Trail.Deadline - StageTime);
		}
	}
	GS->Objectives = MoveTemp(Views);
}
