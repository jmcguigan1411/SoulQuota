// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQQuests.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SQSave.h"
#include "SQData.h"
#include "SQMission.h"
#include "SQAugments.h"

// Unity build: every file-scope name here carries an SQQuest prefix (SQData.cpp has its own anonymous CSV reader).
namespace SQQuestData
{
	struct FSQQuestTables
	{
		TArray<FSQQuestDef> Quests;
		TArray<FSQModifierDef> Modifiers;
		TArray<FSQCosmeticDef> Cosmetics;
		TArray<FSQEventDef> Events;
		TArray<FSQTimelineBeat> Timeline;
		TSet<FName> QuestItems;
		FString Errors;
		bool bLoaded = false;
	};

	FSQQuestTables& SQQuestTables()
	{
		static FSQQuestTables Tables;
		return Tables;
	}

	/** Header-keyed rows of Content/Data/<File> (quoted cells allowed, # lines skipped) */
	bool SQQuestReadCsv(const TCHAR* File, TArray<TMap<FString, FString>>& OutRows, FString& Errors)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / File;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Errors += FString::Printf(TEXT("Missing %s. "), File);
			return false;
		}
		TArray<TArray<FString>> Lines;
		TArray<FString> Row;
		FString Cell;
		bool bQuoted = false;
		for (int32 CharIdx = 0; CharIdx < Text.Len(); ++CharIdx)
		{
			const TCHAR Ch = Text[CharIdx];
			if (bQuoted)
			{
				if (Ch == TEXT('"'))
				{
					if (CharIdx + 1 < Text.Len() && Text[CharIdx + 1] == TEXT('"')) { Cell.AppendChar(TEXT('"')); ++CharIdx; }
					else { bQuoted = false; }
				}
				else { Cell.AppendChar(Ch); }
			}
			else if (Ch == TEXT('"')) { bQuoted = true; }
			else if (Ch == TEXT(',')) { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); }
			else if (Ch == TEXT('\n')) { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); Lines.Add(Row); Row.Reset(); }
			else if (Ch != TEXT('\r')) { Cell.AppendChar(Ch); }
		}
		if (!Cell.IsEmpty() || Row.Num() > 0) { Row.Add(Cell.TrimStartAndEnd()); Lines.Add(Row); }

		TArray<FString> Header;
		for (const TArray<FString>& Line : Lines)
		{
			if (Line.Num() == 0 || Line[0].IsEmpty() || Line[0].StartsWith(TEXT("#"))) continue;
			if (Header.Num() == 0)
			{
				for (const FString& HeaderCell : Line) Header.Add(HeaderCell.ToLower());
				continue;
			}
			TMap<FString, FString>& Out = OutRows.AddDefaulted_GetRef();
			for (int32 Col = 0; Col < Header.Num() && Col < Line.Num(); ++Col)
			{
				if (!Header[Col].IsEmpty()) Out.Add(Header[Col], Line[Col]);
			}
		}
		return true;
	}

	FString SQQuestCell(const TMap<FString, FString>& Row, const TCHAR* Key)
	{
		const FString* Found = Row.Find(FString(Key).ToLower());
		return Found ? *Found : FString();
	}

	float SQQuestNum(const TMap<FString, FString>& Row, const TCHAR* Key, float Default)
	{
		const FString Value = SQQuestCell(Row, Key);
		return Value.IsEmpty() ? Default : FCString::Atof(*Value);
	}

	void SQQuestLoad(FSQQuestTables& Tables)
	{
		Tables.Quests.Reset();
		Tables.Modifiers.Reset();
		Tables.Cosmetics.Reset();
		{
			// Cosmetics.csv is optional: a missing file just means no cosmetic rewards
			TArray<TMap<FString, FString>> CosRows;
			FString CosErrors;
			if (SQQuestReadCsv(TEXT("Cosmetics.csv"), CosRows, CosErrors))
			{
				for (const TMap<FString, FString>& CosRow : CosRows)
				{
					FSQCosmeticDef Cos;
					Cos.Id = FName(*SQQuestCell(CosRow, TEXT("Id")));
					if (Cos.Id.IsNone()) continue;
					Cos.Kind = FName(*SQQuestCell(CosRow, TEXT("Kind")));
					Cos.Name = SQQuestCell(CosRow, TEXT("Name"));
					FString HexText = SQQuestCell(CosRow, TEXT("Color"));
					HexText.RemoveFromStart(TEXT("#"));
					if (HexText.Len() == 6) Cos.Color = FLinearColor::FromSRGBColor(FColor::FromHex(HexText));
					Tables.Cosmetics.Add(Cos);
				}
			}
		}
		Tables.Events.Reset();
		{
			// Events.csv is optional: a missing file just means no run events or district conditions
			TArray<TMap<FString, FString>> EventRows;
			FString EventErrors;
			if (SQQuestReadCsv(TEXT("Events.csv"), EventRows, EventErrors))
			{
				for (const TMap<FString, FString>& EventRow : EventRows)
				{
					FSQEventDef Ev;
					Ev.Id = FName(*SQQuestCell(EventRow, TEXT("Id")));
					if (Ev.Id.IsNone()) continue;
					Ev.Name = SQQuestCell(EventRow, TEXT("Name"));
					const FString KindText = SQQuestCell(EventRow, TEXT("Kind"));
					Ev.Kind = KindText == TEXT("Hunt") ? ESQEventKind::Hunt : (KindText == TEXT("Site") ? ESQEventKind::Site : (KindText == TEXT("Condition") ? ESQEventKind::Condition : ESQEventKind::Ambient));
					const FString SpecialText = SQQuestCell(EventRow, TEXT("Special"));
					Ev.Special = SpecialText == TEXT("Fog") ? ESQEventSpecial::Fog : (SpecialText == TEXT("KingTide") ? ESQEventSpecial::KingTide
						: (SpecialText == TEXT("ClockFast") ? ESQEventSpecial::ClockFast : (SpecialText == TEXT("ClockReverse") ? ESQEventSpecial::ClockReverse
						: (SpecialText == TEXT("Toll") ? ESQEventSpecial::Toll : ESQEventSpecial::None))));
					Ev.Biome = FName(*SQQuestCell(EventRow, TEXT("Biome")));
					Ev.Weight = FMath::Max(0.f, SQQuestNum(EventRow, TEXT("Weight"), 1.f));
					Ev.Seconds = FMath::Clamp(SQQuestNum(EventRow, TEXT("Seconds"), 45.f), 5.f, 600.f);
					Ev.Hold = FMath::Clamp(SQQuestNum(EventRow, TEXT("Hold"), 3.f), 0.2f, 60.f);
					Ev.Banner = SQQuestCell(EventRow, TEXT("Banner"));
					Ev.Hint = SQQuestCell(EventRow, TEXT("Hint"));
					Ev.SpawnMult = FMath::Clamp(SQQuestNum(EventRow, TEXT("SpawnMult"), 1.f), 0.1f, 4.f);
					Ev.EliteMult = FMath::Clamp(SQQuestNum(EventRow, TEXT("EliteMult"), 1.f), 0.f, 10.f);
					Ev.XPMult = FMath::Clamp(SQQuestNum(EventRow, TEXT("XPMult"), 1.f), 0.1f, 5.f);
					Ev.SoulMult = FMath::Clamp(SQQuestNum(EventRow, TEXT("SoulMult"), 1.f), 0.1f, 5.f);
					Ev.GoldMult = FMath::Clamp(SQQuestNum(EventRow, TEXT("GoldMult"), 1.f), 0.1f, 5.f);
					Ev.Enemy = FName(*SQQuestCell(EventRow, TEXT("Enemy")));
					Ev.HealthMult = FMath::Clamp(SQQuestNum(EventRow, TEXT("HealthMult"), 1.f), 0.1f, 100.f);
					Ev.Escort = FName(*SQQuestCell(EventRow, TEXT("Escort")));
					Ev.Count = FMath::Clamp(FMath::RoundToInt(SQQuestNum(EventRow, TEXT("Count"), 0.f)), 0, 60);
					Ev.Reward = SQQuestCell(EventRow, TEXT("Reward"));
					FString EventHex = SQQuestCell(EventRow, TEXT("Color"));
					EventHex.RemoveFromStart(TEXT("#"));
					if (EventHex.Len() == 6) Ev.Color = FLinearColor::FromSRGBColor(FColor::FromHex(EventHex));
					Ev.Model = FName(*SQQuestCell(EventRow, TEXT("Model")));
					Ev.Height = FMath::Clamp(SQQuestNum(EventRow, TEXT("Height"), 140.f), 40.f, 400.f);
					Tables.Events.Add(Ev);
				}
			}
		}
		Tables.Timeline.Reset();
		Tables.QuestItems.Reset();
		Tables.Errors.Reset();
		Tables.bLoaded = true;

		TArray<TMap<FString, FString>> Rows;
		if (SQQuestReadCsv(TEXT("Quests.csv"), Rows, Tables.Errors))
		{
			for (const TMap<FString, FString>& Row : Rows)
			{
				FSQQuestDef Quest;
				Quest.Id = FName(*SQQuestCell(Row, TEXT("Id")));
				if (Quest.Id.IsNone()) continue;
				Quest.Name = SQQuestCell(Row, TEXT("Name"));
				Quest.Category = FName(*SQQuestCell(Row, TEXT("Category")));
				Quest.Description = SQQuestCell(Row, TEXT("Description"));
				Quest.Stat = FName(*SQQuestCell(Row, TEXT("Stat")));
				Quest.Target = FMath::Max(1, FMath::RoundToInt(SQQuestNum(Row, TEXT("Target"), 1.f)));
				Quest.Mode = SQQuestCell(Row, TEXT("Mode")).Equals(TEXT("Best"), ESearchCase::IgnoreCase) ? ESQQuestMode::Best : ESQQuestMode::Total;
				TArray<FString> RewardParts;
				SQQuestCell(Row, TEXT("Reward")).ParseIntoArray(RewardParts, TEXT("|"), true);
				for (const FString& Part : RewardParts)
				{
					FString Kind, Value;
					if (!Part.Split(TEXT(":"), &Kind, &Value)) continue;
					FSQQuestReward Reward;
					Reward.Type = FName(*Kind.TrimStartAndEnd());
					if (Reward.Type == TEXT("Coins")) Reward.Amount = FMath::Max(0, FCString::Atoi(*Value.TrimStartAndEnd()));
					else Reward.Id = FName(*Value.TrimStartAndEnd());
					if (Reward.Type != TEXT("Coins") && Reward.Type != TEXT("Reaper") && Reward.Type != TEXT("Item") && Reward.Type != TEXT("Cosmetic"))
					{
						Tables.Errors += FString::Printf(TEXT("Quest %s: unknown reward %s. "), *Quest.Id.ToString(), *Kind);
						continue;
					}
					if (Reward.Type == TEXT("Item")) Tables.QuestItems.Add(Reward.Id);
					Quest.Rewards.Add(Reward);
				}
				if (Quest.Stat.IsNone()) Tables.Errors += FString::Printf(TEXT("Quest %s: no Stat. "), *Quest.Id.ToString());
				Tables.Quests.Add(Quest);
			}
		}

		Rows.Reset();
		if (SQQuestReadCsv(TEXT("Modifiers.csv"), Rows, Tables.Errors))
		{
			for (const TMap<FString, FString>& Row : Rows)
			{
				FSQModifierDef Mod;
				Mod.Id = FName(*SQQuestCell(Row, TEXT("Id")));
				if (Mod.Id.IsNone() || Tables.Modifiers.Num() >= 30) continue; // a 32-bit mask (sign bit unused)
				Mod.Name = SQQuestCell(Row, TEXT("Name"));
				Mod.Description = SQQuestCell(Row, TEXT("Description"));
				Mod.CoinMult = FMath::Max(1.f, SQQuestNum(Row, TEXT("CoinMult"), 1.f));
				Mod.SpawnMult = FMath::Clamp(SQQuestNum(Row, TEXT("SpawnMult"), 1.f), 0.1f, 5.f);
				Mod.EnemyDamageMult = FMath::Clamp(SQQuestNum(Row, TEXT("EnemyDamageMult"), 1.f), 0.1f, 10.f);
				Mod.HealthRampMult = FMath::Clamp(SQQuestNum(Row, TEXT("HealthRampMult"), 1.f), 0.f, 10.f);
				Mod.NightMult = FMath::Clamp(SQQuestNum(Row, TEXT("NightMult"), 1.f), 0.25f, 4.f);
				Mod.ChestPriceMult = FMath::Clamp(SQQuestNum(Row, TEXT("ChestPriceMult"), 1.f), 0.1f, 10.f);
				Mod.GoldMult = FMath::Clamp(SQQuestNum(Row, TEXT("GoldMult"), 1.f), 0.f, 10.f);
				Mod.EliteMult = FMath::Clamp(SQQuestNum(Row, TEXT("EliteMult"), 1.f), 0.f, 20.f);
				Mod.QuotaMult = FMath::Clamp(SQQuestNum(Row, TEXT("QuotaMult"), 1.f), 0.1f, 10.f);
				Mod.HoldXP = FMath::Clamp(SQQuestNum(Row, TEXT("HoldXP"), 0.f), 0.f, 0.95f);
				Mod.bCrownAll = SQQuestNum(Row, TEXT("CrownAll"), 0.f) > 0.5f;
				Mod.bNoHealing = SQQuestNum(Row, TEXT("NoHealing"), 0.f) > 0.5f;
				{
					FString UnlockStatText, UnlockTargetText;
					if (SQQuestCell(Row, TEXT("Unlock")).Split(TEXT(":"), &UnlockStatText, &UnlockTargetText))
					{
						Mod.UnlockStat = FName(*UnlockStatText.TrimStartAndEnd());
						Mod.UnlockTarget = FMath::Max(0, FCString::Atoi(*UnlockTargetText));
					}
				}
				Tables.Modifiers.Add(Mod);
			}
		}

		Rows.Reset();
		if (SQQuestReadCsv(TEXT("Timeline.csv"), Rows, Tables.Errors))
		{
			for (const TMap<FString, FString>& Row : Rows)
			{
				FSQTimelineBeat Beat;
				Beat.Id = FName(*SQQuestCell(Row, TEXT("Id")));
				if (Beat.Id.IsNone()) continue;
				const FString KindText = SQQuestCell(Row, TEXT("Kind"));
				if (KindText.Equals(TEXT("Miniboss"), ESearchCase::IgnoreCase)) Beat.Kind = ESQBeatKind::Miniboss;
				else if (KindText.Equals(TEXT("FinalSwarm"), ESearchCase::IgnoreCase)) Beat.Kind = ESQBeatKind::FinalSwarm;
				else if (KindText.Equals(TEXT("Swarm"), ESearchCase::IgnoreCase)) Beat.Kind = ESQBeatKind::Swarm;
				else
				{
					Tables.Errors += FString::Printf(TEXT("Timeline %s: unknown Kind %s. "), *Beat.Id.ToString(), *KindText);
					continue;
				}
				Beat.At = FMath::Max(0.f, SQQuestNum(Row, TEXT("At"), 0.5f));
				Beat.Nights = SQQuestCell(Row, TEXT("Nights"));
				{
					TArray<FString> MissionParts;
					SQQuestCell(Row, TEXT("Missions")).ParseIntoArray(MissionParts, TEXT("|"), true);
					for (const FString& MissionPart : MissionParts)
					{
						const FString CleanMission = MissionPart.TrimStartAndEnd();
						if (!CleanMission.IsEmpty() && CleanMission != TEXT("*")) Beat.Missions.Add(FName(*CleanMission));
					}
					const FString HardText = SQQuestCell(Row, TEXT("Hard")).TrimStartAndEnd();
					Beat.HardOnly = HardText.IsEmpty() ? static_cast<int8>(-1) : (FCString::Atoi(*HardText) > 0 ? static_cast<int8>(1) : static_cast<int8>(0));
				}
				Beat.Enemy = FName(*SQQuestCell(Row, TEXT("Enemy")));
				Beat.Count = FMath::Clamp(SQQuestNum(Row, TEXT("Count"), 1.f), 0.f, 400.f);
				Beat.HealthMult = FMath::Clamp(SQQuestNum(Row, TEXT("HealthMult"), 1.f), 0.1f, 200.f);
				Beat.DamageMult = FMath::Clamp(SQQuestNum(Row, TEXT("DamageMult"), 1.f), 0.1f, 20.f);
				Beat.Name = SQQuestCell(Row, TEXT("Name"));
				Beat.Announce = SQQuestCell(Row, TEXT("Announce"));
				Beat.bFreeChest = SQQuestNum(Row, TEXT("FreeChest"), 0.f) > 0.5f;
				Tables.Timeline.Add(Beat);
			}
		}
	}

	FSQQuestTables& SQQuestGet()
	{
		FSQQuestTables& Tables = SQQuestTables();
		if (!Tables.bLoaded) SQQuestLoad(Tables);
		return Tables;
	}

	/** Stats worked out from the profile instead of counted */
	bool SQQuestDerived(FName Stat, const USQSaveGame* Save, int32& OutValue)
	{
		if (Stat == TEXT("QuestsDone")) { OutValue = Save->QuestsDone.Num(); return true; }
		if (Stat == TEXT("ReapersHired")) { OutValue = Save->UnlockedReapers.Num(); return true; }
		if (Stat == TEXT("Contracts")) { OutValue = Save->Contracts.Num(); return true; }
		if (Stat == TEXT("ReaperWins")) { OutValue = Save->ReapersWonWith.Num(); return true; }
		// Nov 9 missions
		if (Stat == TEXT("MissionsCleared")) { OutValue = Save->MissionsCleared.Num(); return true; }
		if (Stat == TEXT("HardCleared")) { OutValue = Save->HardCleared.Num(); return true; }
		if (Stat == TEXT("MetaRanks"))
		{
			int32 Ranks = 0;
			for (const TPair<FName, int32>& Each : Save->MetaRanks) Ranks += FMath::Max(0, Each.Value);
			OutValue = Ranks;
			return true;
		}
		return false;
	}
}

bool FSQTimelineBeat::AppliesToNight(int32 NightIndex) const
{
	const FString Clean = Nights.TrimStartAndEnd();
	if (Clean.IsEmpty() || Clean == TEXT("*")) return true;
	const int32 NightNumber = NightIndex + 1;
	if (Clean.EndsWith(TEXT("+"))) return NightNumber >= FCString::Atoi(*Clean.LeftChop(1));
	return NightNumber == FCString::Atoi(*Clean);
}

bool FSQTimelineBeat::AppliesToMission(FName MissionId, bool bHard) const
{
	if (HardOnly >= 0 && (HardOnly > 0) != bHard) return false;
	return Missions.Num() == 0 || Missions.Contains(MissionId);
}

namespace SQQuests
{
	bool ReadCsvRows(const TCHAR* File, TArray<TMap<FString, FString>>& OutRows, FString& Errors)
	{
		return SQQuestData::SQQuestReadCsv(File, OutRows, Errors);
	}

	FString CsvCell(const TMap<FString, FString>& Row, const TCHAR* Key)
	{
		return SQQuestData::SQQuestCell(Row, Key);
	}

	float CsvNum(const TMap<FString, FString>& Row, const TCHAR* Key, float Default)
	{
		return SQQuestData::SQQuestNum(Row, Key, Default);
	}

	const TArray<FSQQuestDef>& Quests() { return SQQuestData::SQQuestGet().Quests; }
	const TArray<FSQModifierDef>& Modifiers() { return SQQuestData::SQQuestGet().Modifiers; }
	const TArray<FSQTimelineBeat>& Timeline() { return SQQuestData::SQQuestGet().Timeline; }
	const TArray<FSQCosmeticDef>& Cosmetics() { return SQQuestData::SQQuestGet().Cosmetics; }
	const TArray<FSQEventDef>& Events() { return SQQuestData::SQQuestGet().Events; }
	const FSQEventDef* FindEvent(FName Id)
	{
		if (Id.IsNone()) return nullptr;
		for (const FSQEventDef& Ev : Events())
		{
			if (Ev.Id == Id) return &Ev;
		}
		return nullptr;
	}
	bool ModifierUnlocked(const FSQModifierDef& Mod, const USQSaveGame* Save)
	{
		if (Mod.UnlockStat.IsNone() || Mod.UnlockTarget <= 0) return true;
		return Save && Save->QuestTotal(Mod.UnlockStat) >= Mod.UnlockTarget;
	}
	const FSQCosmeticDef* FindCosmetic(FName Id)
	{
		for (const FSQCosmeticDef& Cos : Cosmetics())
		{
			if (Cos.Id == Id) return &Cos;
		}
		return nullptr;
	}

	FLinearColor EquippedColor(const USQSaveGame* Save, FName Kind, const FLinearColor& Default)
	{
		if (!Save) return Default;
		const FName* Equipped = Save->CosmeticsEquipped.Find(Kind);
		if (!Equipped || Equipped->IsNone() || !Save->CosmeticsOwned.Contains(*Equipped)) return Default;
		const FSQCosmeticDef* Cos = FindCosmetic(*Equipped);
		return (Cos && Cos->Kind == Kind) ? Cos->Color : Default;
	}

	FString CycleCosmetic(USQSaveGame* Save, FName Kind)
	{
		if (!Save) return FString(TEXT("STANDARD"));
		// The cycle: standard (none), then every owned cosmetic of this kind in file order
		TArray<FName> Ring;
		Ring.Add(NAME_None);
		for (const FSQCosmeticDef& Cos : Cosmetics())
		{
			if (Cos.Kind == Kind && Save->CosmeticsOwned.Contains(Cos.Id)) Ring.Add(Cos.Id);
		}
		const FName* Current = Save->CosmeticsEquipped.Find(Kind);
		const int32 At = FMath::Max(0, Ring.IndexOfByKey(Current ? *Current : NAME_None));
		const FName Next = Ring[(At + 1) % Ring.Num()];
		Save->CosmeticsEquipped.Add(Kind, Next);
		const FSQCosmeticDef* NextDef = Next.IsNone() ? nullptr : FindCosmetic(Next);
		return NextDef ? NextDef->Name.ToUpper() : FString(TEXT("STANDARD"));
	}

	bool NearestGoal(const USQSaveGame* Save, const USQData* D, FString& OutName, FString& OutReward, float& OutFrac)
	{
		if (!Save) return false;
		const FSQQuestDef* Best = nullptr;
		float BestFrac = -1.f;
		for (const FSQQuestDef& Quest : Quests())
		{
			if (Save->HasQuest(Quest.Id)) continue;
			const float Frac = static_cast<float>(Progress(Quest, Save, nullptr)) / static_cast<float>(FMath::Max(1, Quest.Target));
			if (Frac > BestFrac)
			{
				BestFrac = Frac;
				Best = &Quest;
			}
		}
		if (!Best) return false;
		OutName = Best->Name;
		OutReward = DescribeRewards(*Best, D);
		OutFrac = FMath::Clamp(BestFrac, 0.f, 1.f);
		return true;
	}
	const FString& LoadErrors()
	{
		// Missions.csv / MetaUpgrades.csv / Augments.csv problems ride along with the quest tables' (HUD data errors)
		static FString SQQuestCombinedErrors;
		SQQuestCombinedErrors = SQQuestData::SQQuestGet().Errors + USQMissions::LoadErrors() + USQAugments::LoadErrors();
		return SQQuestCombinedErrors;
	}

	const FSQQuestDef* FindQuest(FName Id)
	{
		return Quests().FindByPredicate([Id](const FSQQuestDef& Each) { return Each.Id == Id; });
	}

	void Reload()
	{
		SQQuestData::SQQuestLoad(SQQuestData::SQQuestTables());
		USQMissions::Reload();
		USQAugments::Reload();
	}

	bool IsQuestItem(FName ItemId)
	{
		return SQQuestData::SQQuestGet().QuestItems.Contains(ItemId);
	}

	void GetLockedItems(const USQSaveGame* Save, TArray<FName>& Out)
	{
		Out.Reset();
		for (const FName ItemId : SQQuestData::SQQuestGet().QuestItems)
		{
			if (!Save || !Save->HasItem(ItemId)) Out.Add(ItemId);
		}
	}

	int32 Progress(const FSQQuestDef& Quest, const USQSaveGame* Save, const TMap<FName, int32>* RunStats)
	{
		if (!Save) return 0;
		int32 Derived = 0;
		if (SQQuestData::SQQuestDerived(Quest.Stat, Save, Derived)) return Derived;
		if (Quest.Mode == ESQQuestMode::Total) return Save->QuestTotal(Quest.Stat);
		const int32* RunValue = RunStats ? RunStats->Find(Quest.Stat) : nullptr;
		return FMath::Max(Save->QuestBest(Quest.Stat), RunValue ? *RunValue : 0);
	}

	void CompleteFinished(USQSaveGame* Save, const TMap<FName, int32>* RunStats, TArray<const FSQQuestDef*>& OutDone)
	{
		OutDone.Reset();
		if (!Save) return;
		// Loop: finishing a quest moves QuestsDone (and hires move ReapersHired), which can finish another
		for (int32 Pass = 0; Pass < 8; ++Pass)
		{
			bool bAny = false;
			for (const FSQQuestDef& Quest : Quests())
			{
				if (Save->HasQuest(Quest.Id) || Progress(Quest, Save, RunStats) < Quest.Target) continue;
				Save->QuestsDone.Add(Quest.Id);
				Save->QuestsNew.AddUnique(Quest.Id);
				for (const FSQQuestReward& Reward : Quest.Rewards)
				{
					if (Reward.Type == TEXT("Coins"))
					{
						Save->OvertimePay = FMath::Clamp(Save->OvertimePay + Reward.Amount, 0, 100000000);
						Save->LifetimePay = FMath::Clamp(Save->LifetimePay + Reward.Amount, 0, 100000000);
					}
					else if (Reward.Type == TEXT("Reaper"))
					{
						Save->UnlockedReapers.AddUnique(Reward.Id);
					}
					else if (Reward.Type == TEXT("Item"))
					{
						Save->UnlockedItems.AddUnique(Reward.Id);
					}
					else if (Reward.Type == TEXT("Cosmetic"))
					{
						Save->CosmeticsOwned.AddUnique(Reward.Id);
					}
				}
				OutDone.Add(&Quest);
				bAny = true;
			}
			if (!bAny) break;
		}
	}

	FString DescribeRewards(const FSQQuestDef& Quest, const USQData* D)
	{
		FString Out;
		for (const FSQQuestReward& Reward : Quest.Rewards)
		{
			FString Part;
			if (Reward.Type == TEXT("Coins"))
			{
				Part = FString::Printf(TEXT("%d banked gold"), Reward.Amount);
			}
			else if (Reward.Type == TEXT("Reaper"))
			{
				const FSQReaperDef* ReaperDef = D ? D->FindReaper(Reward.Id) : nullptr;
				Part = FString::Printf(TEXT("hires %s"), ReaperDef ? *ReaperDef->Name : *Reward.Id.ToString());
			}
			else if (Reward.Type == TEXT("Cosmetic"))
			{
				const FSQCosmeticDef* CosDef = FindCosmetic(Reward.Id);
				Part = FString::Printf(TEXT("%s (lantern trail colour)"), CosDef && !CosDef->Name.IsEmpty() ? *CosDef->Name : *Reward.Id.ToString());
			}
			else
			{
				const FSQUpgradeDef* ItemDef = D ? D->FindUpgrade(Reward.Id) : nullptr;
				Part = FString::Printf(TEXT("unlocks %s"), ItemDef ? *ItemDef->Name : *Reward.Id.ToString());
			}
			Out += Out.IsEmpty() ? Part : TEXT(" + ") + Part;
		}
		return Out;
	}

	float CoinMultiplier(int32 Mask, float Cap)
	{
		float Mult = 1.f;
		const TArray<FSQModifierDef>& Mods = Modifiers();
		for (int32 ModIdx = 0; ModIdx < Mods.Num(); ++ModIdx)
		{
			if (Mask & (1 << ModIdx)) Mult += Mods[ModIdx].CoinMult - 1.f;
		}
		return FMath::Clamp(Mult, 1.f, FMath::Max(1.f, Cap));
	}

	int32 CountModifiers(int32 Mask)
	{
		int32 Count = 0;
		for (int32 ModIdx = 0; ModIdx < Modifiers().Num(); ++ModIdx)
		{
			if (Mask & (1 << ModIdx)) Count++;
		}
		return Count;
	}
}
