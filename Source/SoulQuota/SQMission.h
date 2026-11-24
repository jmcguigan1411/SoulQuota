// Copyright 2026 Jack McGuigan. All rights reserved.
// Missions (Nov 9 2026, missions rebuild). A run is ONE mission: a single stage on one map (Missions.csv row, one per biome) with a
// timeline of OBJECTIVES, two minibosses and a final boss summoned at the Appointment Bell. Gold earned in the run is partly banked at the
// end and buys permanent upgrades (MetaUpgrades.csv) in Death's Office (main menu). Hard mode per map unlocks by beating it.
//   USQMissions        - data tables (Missions.csv, MetaUpgrades.csv), the menu read API, pending mission choice, meta upgrade purchases
//   FSQMissionDirector - server: runs the objectives and minibosses of the live mission (owned by ASQGameMode)
//   ASQObjective       - replicated world part of an objective: spawner nest, defend zone, escort cart, soul urn, beacon
// The UI reads ASQGameState::Objectives (FSQObjectiveView), StageTime / StageDuration, MissionId, bHardMode, RunResult, RunStats.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "SQMission.generated.h"

class USQData;
class USQSaveGame;
class ASQGameState;
class ASQGameMode;
class ASQCharacter;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USQInteractCue;

UENUM(BlueprintType)
enum class ESQRunResult : uint8
{
	None,
	Victory,
	Defeat
};

enum class ESQObjectiveType : uint8 { None, BankSouls, DestroySpawners, DefendPoint, Escort, CollectItems, SurviveWave, KillElite };
enum class ESQObjectiveReward : uint8 { None, Gold, Chest, Augment, XP };

/** One objective of the stage, as the HUD sees it (replicated in ASQGameState::Objectives) */
USTRUCT(BlueprintType)
struct FSQObjectiveView
{
	GENERATED_BODY()

	UPROPERTY() FText Title;
	/** "2 / 3 nests", "Hold the zone", "1:12 left"... */
	UPROPERTY() FText Detail;
	/** 0..1 */
	UPROPERTY() float Progress = 0.f;
	UPROPERTY() bool bActive = false;
	UPROPERTY() bool bDone = false;
	/** Timed out (1 Defiance stack on the boss, no reward) */
	UPROPERTY() bool bFailed = false;
	/** Where to point the HUD arrow (the zone, the cart, the nearest nest / urn, the lantern) */
	UPROPERTY() FVector WorldLocation = FVector::ZeroVector;
	/** BankSouls | DestroySpawners | DefendPoint | Escort | CollectItems | SurviveWave | KillElite */
	UPROPERTY() FName Type;
	/** Gold | Chest | Augment | XP */
	UPROPERTY() FName Reward;
	/** Seconds before it fails (-1 = no limit / not running) */
	UPROPERTY() float TimeLeft = -1.f;
};

/** One reaper's line on the victory / defeat screen */
USTRUCT(BlueprintType)
struct FSQPlayerRunStats
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FName ReaperId;
	UPROPERTY() int32 Kills = 0;
	UPROPERTY() int32 GoldEarned = 0;
	/** Gold this reaper banked into their profile (meta currency) */
	UPROPERTY() int32 GoldBanked = 0;
	UPROPERTY() int32 Augments = 0;
	UPROPERTY() bool bBot = false;
};

/** Victory / defeat screen (ASQGameState::RunStats, filled by ASQGameMode::EndRun) */
USTRUCT(BlueprintType)
struct FSQRunStats
{
	GENERATED_BODY()

	UPROPERTY() bool bWon = false;
	UPROPERTY() FName MissionId;
	UPROPERTY() bool bHard = false;
	UPROPERTY() float StageSeconds = 0.f;
	UPROPERTY() int32 TeamLevel = 1;
	UPROPERTY() int32 ObjectivesDone = 0;
	UPROPERTY() int32 ObjectivesTotal = 0;
	UPROPERTY() int32 MinibossesKilled = 0;
	/** Seconds the boss fight took (0 = never summoned) */
	UPROPERTY() float BossSeconds = 0.f;
	UPROPERTY() int32 Defiance = 0;
	UPROPERTY() float FinalSwarmSeconds = 0.f;
	/** First win of this mission (or of its Hard mode) this run: the menu can make a fuss */
	UPROPERTY() bool bFirstClear = false;
	/** The Long Haunt: loops begun after the mission boss (0 = cashed out at the first boss or never got there) */
	UPROPERTY() int32 Loops = 0;
	/** The crew wiped inside a loop: still a win, with the loop's unlocked gold banked at the loss share */
	UPROPERTY() bool bDiedInLoop = false;
	UPROPERTY() int32 LoopBonusGold = 0;
	UPROPERTY() TArray<FSQPlayerRunStats> Players;
};

/** Mission select card (USQMissions::GetMissionList) */
USTRUCT(BlueprintType)
struct FSQMissionView
{
	GENERATED_BODY()

	UPROPERTY() FName Id;
	UPROPERTY() FText Name;
	UPROPERTY() FText Description;
	UPROPERTY() FName Biome;
	/** Release length in seconds (Missions.csv Duration) */
	UPROPERTY() float DurationSeconds = 0.f;
	UPROPERTY() FText BossName;
	/** 1-5 skulls */
	UPROPERTY() int32 Difficulty = 1;
	UPROPERTY() int32 ObjectiveCount = 0;
	UPROPERTY() bool bUnlocked = false;
	UPROPERTY() bool bCleared = false;
	UPROPERTY() bool bHardUnlocked = false;
	UPROPERTY() bool bHardCleared = false;
	/** "Beat The Overflowing Graveyard to unlock" (empty when unlocked) */
	UPROPERTY() FText UnlockText;
	/** "Clear: 250 gold (first clear +400)" */
	UPROPERTY() FText RewardText;
	/** Best winning time in seconds (0 = none) */
	UPROPERTY() float BestSeconds = 0.f;
};

/** Death's Office line (USQMissions::GetMetaUpgradeList) */
USTRUCT(BlueprintType)
struct FSQMetaUpgradeView
{
	GENERATED_BODY()

	UPROPERTY() FName Id;
	UPROPERTY() FText Name;
	UPROPERTY() FText Description;
	UPROPERTY() FName Stat;
	UPROPERTY() int32 Rank = 0;
	UPROPERTY() int32 MaxRank = 1;
	/** Price of the next rank (-1 = maxed) */
	UPROPERTY() int32 NextCost = -1;
	UPROPERTY() bool bCanAfford = false;
	/** "+12% damage" now / after the next rank ("MAX" when maxed) */
	UPROPERTY() FText CurrentText;
	UPROPERTY() FText NextText;
};

// ---------------------------------------------------------------- data rows

struct FSQObjectiveSpec
{
	ESQObjectiveType Type = ESQObjectiveType::None;
	FName TypeName;
	int32 Count = 1;
	ESQObjectiveReward Reward = ESQObjectiveReward::Gold;
	FName RewardName;
	/** Pressure / nest / elite enemy (Enemies.csv id); none = the type's default */
	FName Enemy;
};

struct FSQMissionMiniboss
{
	FName Enemy;
	float HealthMult = 5.f;
	float DamageMult = 1.3f;
	FString Name;
};

struct FSQMissionDef
{
	FName Id;
	FString Name;
	FString Description;
	FName Biome;
	float Duration = 1080.f;
	TArray<FSQObjectiveSpec> Objectives;
	FName Boss;
	TArray<FSQMissionMiniboss> Minibosses;
	TArray<float> MinibossAt;
	float HealthMult = 1.f;
	float DamageMult = 1.f;
	float SpawnMult = 1.f;
	float BossHealthMult = 1.f;
	float GoldMult = 1.f;
	float HardHealthMult = 1.5f;
	float HardDamageMult = 1.35f;
	float HardSpawnMult = 1.2f;
	float HardBossHealthMult = 1.6f;
	float HardGoldMult = 1.5f;
	/** Mission that must be beaten first (none = open) */
	FName UnlockMission;
	int32 ClearGold = 0;
	int32 FirstClearGold = 0;
	int32 HardFirstClearGold = 0;
	int32 Difficulty = 1;

	float Mult(float Normal, float Hard, bool bHard) const { return Normal * (bHard ? Hard : 1.f); }
};

struct FSQMetaUpgradeDef
{
	FName Id;
	FString Name;
	FString Description;
	FName Stat;
	float PerRank = 0.f;
	int32 MaxRank = 1;
	int32 BaseCost = 100;
	float CostGrowth = 1.5f;
	int32 Order = 0;

	/** Price of rank (CurrentRank + 1); -1 when maxed */
	int32 CostForRank(int32 CurrentRank) const;
	/** "+12% damage" for a rank (empty for rank 0) */
	FString ValueText(int32 Rank) const;
};

/**
 * Mission data + the menu API (all static). Tables load on first use from Content/Data (SQReload re-reads through Reload()).
 * Unlocks follow the local profile; Tuning UnlockAllBiomes 1 opens every mission and Hard mode (testing).
 */
UCLASS()
class SOULQUOTA_API USQMissions : public UObject
{
	GENERATED_BODY()

public:
	static const TArray<FSQMissionDef>& Missions();
	static const FSQMissionDef* FindMission(FName Id);
	static const TArray<FSQMetaUpgradeDef>& MetaUpgrades();
	static const FSQMetaUpgradeDef* FindMetaUpgrade(FName Id);
	static void Reload();
	static const FString& LoadErrors();

	// ---- Menu read API (UI agent: SQMenu mission select + Death's Office pages)
	static void GetMissionList(const USQData* D, const USQSaveGame* Save, TArray<FSQMissionView>& Out);
	static void GetMetaUpgradeList(const USQData* D, const USQSaveGame* Save, TArray<FSQMetaUpgradeView>& Out);
	/** Spends the profile's banked gold on the next rank of Id and saves. False when maxed, unknown or too dear. */
	static bool BuyMetaUpgrade(USQSaveGame* Save, const USQData* D, FName Id);

	static bool IsMissionUnlocked(const USQData* D, const USQSaveGame* Save, FName Id);
	static bool IsHardUnlocked(const USQData* D, const USQSaveGame* Save, FName Id);

	/** Menu: the mission the next hosted run starts with (stored in the profile; ASQGameMode reads it at BeginPlay) */
	static void SetPendingMission(UObject* WorldContext, FName MissionId, bool bHard);
	/** The pending mission (first unlocked mission when none / unknown) */
	static FName GetPendingMission(const UObject* WorldContext, bool& bOutHard);

	/** A profile's rank of a meta upgrade (0 when none) */
	static int32 GetMetaRank(const USQSaveGame* Save, FName Id);
	/** Sum of every rank bought (quest stat MetaRanks) */
	static int32 CountMetaRanks(const USQSaveGame* Save);
	/** Value of a meta line at Rank: PerRank x Rank, ReviveSeconds = seconds (0 = none) */
	static float MetaValueAt(const FSQMetaUpgradeDef& Def, int32 Rank, const USQData* D);

	static ESQObjectiveType ParseObjectiveType(const FString& Text);
	static FName ObjectiveTypeName(ESQObjectiveType Type);
	/** Default pressure / nest / elite enemy for a type */
	static FName DefaultEnemyFor(ESQObjectiveType Type);
};

// ---------------------------------------------------------------- world parts

enum class ESQObjectivePart : uint8 { Beacon, Nest, Zone, Cart, Urn };

/**
 * A replicated piece of an objective in the world (server-authoritative; primitive-shape look until art exists):
 *   Nest   - spawner with HP; spawns enemies every NestSpawnInterval; reapers next to it siege it, weapon hits near it chip it
 *   Zone   - defend ring: the meter (Progress) fills while a reaper stands inside, drains when empty
 *   Cart   - escort hearse: rolls along Path while a reaper is within EscortRadius (Progress = share of the road)
 *   Urn    - soul urn: walk over it to collect it
 *   Beacon - a light pillar marking a spot (lantern for Bank Souls, the hunt / survive area)
 */
UCLASS()
class SOULQUOTA_API ASQObjective : public AActor
{
	GENERATED_BODY()

public:
	ASQObjective();

	/** Server: spawns a part on the ground under Location */
	static ASQObjective* SpawnPart(UWorld* World, ESQObjectivePart InPart, const FVector& Location, float InRadius);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(ReplicatedUsing = OnRep_Look) uint8 Part = 0;
	UPROPERTY(Replicated) float Radius = 300.f;
	UPROPERTY(Replicated) float Health = 0.f;
	UPROPERTY(Replicated) float MaxHealth = 0.f;
	/** Zone meter / cart road share / 0..1 */
	UPROPERTY(Replicated) float Progress = 0.f;
	UPROPERTY(ReplicatedUsing = OnRep_Look) bool bDone = false;
	/** Zone / cart: nobody is in range (the HUD can flash it) */
	UPROPERTY(Replicated) bool bStalled = false;

	// ---- Server settings (set by FSQMissionDirector right after spawning)
	FName SpawnEnemy;
	float FillSeconds = 40.f;
	TArray<FVector> Path;
	float PathLength = 0.f;
	float PathDone = 0.f;
	float PressureTimer = 0.f;
	/** Server: damage to a nest (weapons near it, siege). Marks it done at 0. */
	void DamageNest(float Amount);
	/** Server: true while any standing reaper is within Range (2D) */
	bool AnyReaperWithin(float Range, int32* OutCount = nullptr) const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Top;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ring;
	/** Map revamp (Nov 10 2026): the shared interactable cue (green collect / red-orange nest), so objectives read like everything else */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USQInteractCue> Cue;

protected:
	UFUNCTION() void OnRep_Look();
	void ApplyLook();
	void ServerTickPart(float DeltaSeconds);

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMID;
	float LookTime = 0.f;
	bool bLookApplied = false;
	uint8 LookPart = 255;
	bool bLookDone = false;
};

// ---------------------------------------------------------------- server runtime

/** Server: the live mission's objectives and minibosses (owned by ASQGameMode; plain struct, weak pointers only) */
class SOULQUOTA_API FSQMissionDirector
{
public:
	/** A new stage: lays out the objective schedule (nothing spawns until its time) */
	void Begin(UWorld* InWorld, const FSQMissionDef* InMission, bool bInHard, float InStageDuration);
	/** Every server tick while the stage runs (StageTime = seconds since the stage began; bOvertime = the stage clock ran out) */
	void Tick(float DeltaSeconds, float StageTime);
	/** Destroys every part and clears the views */
	void End();
	/** The stage clock ran out: every unfinished objective fails (Defiance) */
	void FailRemaining();

	/** Horde kill (USQAugments::OnKill forwards it): KillElite progress, miniboss count, augment offers on the 1st / 2nd miniboss */
	void NotifyKill(int32 EnemyType, uint8 EcoFlags);
	/** A weapon hit landed on an enemy at Pos: nests within NestRadius take NestHitShare of it */
	void NotifyHitAt(const FVector2D& Pos, float Damage);

	bool IsRunning() const { return !MissionId.IsNone(); }
	/** Every objective done or failed, and the lantern trail walked to its end */
	bool AllResolved() const;
	/** Lantern trail: on for this stage / every lantern lit or burnt out */
	bool HasTrail() const { return Trail.bOn; }
	/** The Long Haunt: added to the arena's RunSeed so each loop lights a different lantern route on the same map */
	int32 TrailSeedSalt = 0;
	bool IsTrailDone() const { return !Trail.bOn || Trail.bDone; }
	int32 NumObjectives() const { return Runs.Num(); }
	int32 NumDone() const;
	int32 NumFailed() const;
	int32 GetMinibossKills() const { return MinibossKills; }
	/** Looked up by id every time (SQReload may rebuild the tables mid-run) */
	const FSQMissionDef* GetMission() const { return USQMissions::FindMission(MissionId); }

private:
	struct FObjectiveRun
	{
		FSQObjectiveSpec Spec;
		/** 0 pending, 1 active, 2 done, 3 failed */
		uint8 State = 0;
		float ScheduledAt = 0.f;
		float StartedAt = 0.f;
		float TimeLimit = 0.f;
		int32 Target = 1;
		float Value = 0.f;
		float Timer = 0.f;
		FVector Anchor = FVector::ZeroVector;
		TArray<TWeakObjectPtr<ASQObjective>> Parts;
	};

	ASQGameState* GetGS() const;
	void StartObjective(int32 Index, float StageTime);
	void TickObjective(int32 Index, float DeltaSeconds, float StageTime);
	void FinishObjective(int32 Index, bool bSuccess, float StageTime);
	void GrantReward(const FObjectiveRun& Run);
	void TickMinibosses(float StageTime);
	void WriteViews(float StageTime);
	/** Map spots from ASQArena::GetObjectiveSpots, topped up with clear random ground */
	void FindSpots(FName Type, int32 Count, TArray<FVector>& Out) const;
	FVector RandomGround(float MinDistFromCentre) const;
	int32 CrewSize() const;
	float CrewScale() const;

	// ---- Lantern trail (Nov 12 2026; Docs/LanternTrail.md): runs beside the objectives for the whole stage
	struct FTrail
	{
		bool bOn = false;
		bool bDone = false;
		TArray<FVector> Sites;
		TArray<FString> Names;
		int32 Stage = 0;
		int32 Quota = 0;
		int32 Banked = 0;
		int32 Lit = 0;          // lanterns banked in full
		int32 LastGSBanked = 0; // GS->Banked seen last tick (the trail counts what is banked from then on)
		float Deadline = 0.f;   // stage time at which the lit lantern burns out
	};
	FTrail Trail;
	void BeginTrail();
	void TickTrail(float StageTime);
	/** bBanked = the quota was met (reward); false = it burnt out (no reward). Lights the next lantern or ends the trail. */
	void FinishTrailStage(bool bBanked, float StageTime);
	void LightTrailStage(float StageTime);
	int32 TrailQuotaFor(int32 Stage) const;

	TWeakObjectPtr<UWorld> World;
	FName MissionId;
	bool bHard = false;
	float StageDuration = 1080.f;
	TArray<FObjectiveRun> Runs;
	TArray<bool> MinibossFired;
	int32 MinibossKills = 0;
	float ViewTimer = 0.f;
	float LastEndedAt = 0.f;
};
