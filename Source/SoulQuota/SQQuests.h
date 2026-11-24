// Copyright 2026 Jack McGuigan. All rights reserved.
// Meta goals and run beats (Nov 7 economy pass), all data-driven:
//   Quests.csv    - "Performance Reviews": stat targets with banked gold / reaper / item rewards (progress in SQSave, per PC)
//   Modifiers.csv - "Contract Clauses": challenge modifiers the host toggles in the lobby (banked gold multiplier)
//   Timeline.csv  - run beats: minibosses and swarm waves inside a night, the Final Swarm tiers after midnight
// The CSVs are read once on first use (SQReload also re-reads them through SQQuests::Reload).

#pragma once

#include "CoreMinimal.h"

class USQSaveGame;
class USQData;

enum class ESQQuestMode : uint8 { Total, Best };

/** Content pass (Nov 24 2026): Cosmetics.csv. Purely visual, local to the owner's screen, earned from quests (never bought). */
struct FSQCosmeticDef
{
	FName Id;
	/** Trail = the colour of YOUR lantern trail marker and guide line on the minimap */
	FName Kind;
	FString Name;
	FLinearColor Color = FLinearColor::White;
};

struct FSQQuestReward
{
	FName Type;        // Coins | Reaper | Item | Cosmetic
	FName Id;          // Reapers.csv / Upgrades.csv id (none for coins)
	int32 Amount = 0;  // coins
};

struct FSQQuestDef
{
	FName Id;
	FString Name;
	FName Category;
	FString Description;
	FName Stat;
	int32 Target = 1;
	ESQQuestMode Mode = ESQQuestMode::Total;
	TArray<FSQQuestReward> Rewards;
};

struct FSQModifierDef
{
	FName Id;
	FString Name;
	FString Description;
	float CoinMult = 1.f;
	float SpawnMult = 1.f;
	float EnemyDamageMult = 1.f;
	float HealthRampMult = 1.f;
	float NightMult = 1.f;
	float ChestPriceMult = 1.f;
	float GoldMult = 1.f;
	float EliteMult = 1.f;
	float QuotaMult = 1.f;
	float HoldXP = 0.f;
	bool bCrownAll = false;
	bool bNoHealing = false;
	/** Content item 12: the clause is offered once the host's lifetime QuestTotal(UnlockStat) >= UnlockTarget (none = always) */
	FName UnlockStat;
	int32 UnlockTarget = 0;
};

/** Events.csv row (content item 12): a seeded random run event or a district condition. Docs/Content.md. */
enum class ESQEventKind : uint8 { Ambient, Hunt, Site, Condition };
enum class ESQEventSpecial : uint8 { None, Fog, KingTide, ClockFast, ClockReverse, Toll };
struct FSQEventDef
{
	FName Id;
	FString Name;
	ESQEventKind Kind = ESQEventKind::Ambient;
	ESQEventSpecial Special = ESQEventSpecial::None;
	FName Biome;
	float Weight = 1.f;
	float Seconds = 45.f;
	float Hold = 3.f;
	FString Banner;
	FString Hint;
	float SpawnMult = 1.f;
	float EliteMult = 1.f;
	float XPMult = 1.f;
	float SoulMult = 1.f;
	float GoldMult = 1.f;
	FName Enemy;
	float HealthMult = 1.f;
	FName Escort;
	int32 Count = 0;
	FString Reward;
	FLinearColor Color = FLinearColor::White;
	FName Model;
	float Height = 140.f;
};

enum class ESQBeatKind : uint8 { Miniboss, Swarm, FinalSwarm };

struct FSQTimelineBeat
{
	FName Id;
	ESQBeatKind Kind = ESQBeatKind::Swarm;
	/** Share of the ACT (Miniboss / Swarm) or minutes into the Final Swarm (FinalSwarm) */
	float At = 0.5f;
	/** Acts (Nov 9 missions: an act is the old night index): * = every act, N = act N only, N+ = act N and later (1-based) */
	FString Nights;
	/** Missions.csv ids this beat runs in (empty = every mission) */
	TArray<FName> Missions;
	/** -1 both modes, 1 Hard mode only, 0 normal only */
	int8 HardOnly = -1;
	FName Enemy;
	float Count = 1.f;
	float HealthMult = 1.f;
	float DamageMult = 1.f;
	FString Name;
	FString Announce;
	bool bFreeChest = false;

	bool AppliesToNight(int32 NightIndex) const;
	bool AppliesToMission(FName MissionId, bool bHard) const;
};

namespace SQQuests
{
	SOULQUOTA_API const TArray<FSQQuestDef>& Quests();
	SOULQUOTA_API const TArray<FSQModifierDef>& Modifiers();
	SOULQUOTA_API const TArray<FSQTimelineBeat>& Timeline();
	SOULQUOTA_API const FSQQuestDef* FindQuest(FName Id);
	SOULQUOTA_API const TArray<FSQCosmeticDef>& Cosmetics();
	SOULQUOTA_API const FSQCosmeticDef* FindCosmetic(FName Id);
	SOULQUOTA_API const TArray<FSQEventDef>& Events();
	SOULQUOTA_API const FSQEventDef* FindEvent(FName Id);
	/** True when the host's save has earned this contract clause (Modifiers.csv Unlock) */
	SOULQUOTA_API bool ModifierUnlocked(const FSQModifierDef& Mod, const USQSaveGame* Save);
	/** The colour of the cosmetic this profile has equipped for Kind (owned ones only), else Default */
	SOULQUOTA_API FLinearColor EquippedColor(const USQSaveGame* Save, FName Kind, const FLinearColor& Default);
	/** Equips the next owned cosmetic of Kind (wraps round to "standard"). Returns its display name. */
	SOULQUOTA_API FString CycleCosmetic(USQSaveGame* Save, FName Kind);
	/** The unfinished quest closest to done (highest share of its target). False when everything is done. */
	SOULQUOTA_API bool NearestGoal(const USQSaveGame* Save, const USQData* D, FString& OutName, FString& OutReward, float& OutFrac);
	/** Re-reads Quests.csv / Modifiers.csv / Timeline.csv */
	SOULQUOTA_API void Reload();
	/** Problems found while loading (shown with the HUD's data errors) */
	SOULQUOTA_API const FString& LoadErrors();

	/** True when some quest rewards this Upgrades.csv id (it is locked until that quest is done) */
	SOULQUOTA_API bool IsQuestItem(FName ItemId);
	/** Every quest-rewarded item this profile has not unlocked yet */
	SOULQUOTA_API void GetLockedItems(const USQSaveGame* Save, TArray<FName>& Out);

	/** Current value of a quest's stat: lifetime total, or the best run (RunStats = the run in progress, may be null) */
	SOULQUOTA_API int32 Progress(const FSQQuestDef& Quest, const USQSaveGame* Save, const TMap<FName, int32>* RunStats);
	/** Marks every finished quest done and grants its rewards (coins, reapers, items). Returns the quests just finished. */
	SOULQUOTA_API void CompleteFinished(USQSaveGame* Save, const TMap<FName, int32>* RunStats, TArray<const FSQQuestDef*>& OutDone);

	/** "150 banked gold + hires The Lantern Keeper" (display names from the CSVs when D is given) */
	SOULQUOTA_API FString DescribeRewards(const FSQQuestDef& Quest, const USQData* D);

	/** Banked gold multiplier for a modifier mask: 1 + sum(CoinMult - 1), capped at Cap */
	SOULQUOTA_API float CoinMultiplier(int32 Mask, float Cap);
	SOULQUOTA_API int32 CountModifiers(int32 Mask);

	/** Header-keyed rows of Content/Data/<File> (keys lower-case; quoted cells allowed; # lines skipped). Shared by SQMission / SQAugments. */
	SOULQUOTA_API bool ReadCsvRows(const TCHAR* File, TArray<TMap<FString, FString>>& OutRows, FString& Errors);
	/** Cell by (any-case) header name, empty when missing */
	SOULQUOTA_API FString CsvCell(const TMap<FString, FString>& Row, const TCHAR* Key);
	/** Number cell, Default when empty */
	SOULQUOTA_API float CsvNum(const TMap<FString, FString>& Row, const TCHAR* Key, float Default);
}
