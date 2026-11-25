// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SQTypes.h"
#include "SQQuests.h"
#include "SQMission.h"
#include "SQGameState.generated.h"

class ASQBoss;
class ASQLantern;
class ASQHordeManager;
class ASQArena;
class ASQPortal;
class ASQPlayerState;

/**
 * Shared run state every player can see. Nov 9 2026 missions: a run is ONE mission stage (Missions.csv). Phase Night = the stage is running,
 * Boss = the boss fight, Victory / Defeat = the end screen (Shop is no longer used). NightIndex = the current ACT (0..2, a third of the stage
 * each): the horde's per-night columns, HP / damage steps and gold growth follow it. PhaseTimeLeft / PhaseDuration = the act clock;
 * StageTime / StageDuration = the whole stage. Quota / Banked = the live Bank Souls objective (0 when none). Defiance = failed objectives.
 */
UCLASS()
class SOULQUOTA_API ASQGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated) ESQPhase Phase = ESQPhase::Waiting;
	UPROPERTY(Replicated) int32 NightIndex = 0;
	UPROPERTY(Replicated) float PhaseTimeLeft = 0.f;
	UPROPERTY(Replicated) float PhaseDuration = 1.f;

	UPROPERTY(Replicated) int32 Quota = 0;
	UPROPERTY(Replicated) int32 Banked = 0;
	UPROPERTY(Replicated) int32 ShopSouls = 0;
	/** Purchases in the current Death's Office visit (each one raises every price, see ShopPrice) */
	UPROPERTY(Replicated) int32 ShopBuys = 0;
	UPROPERTY(Replicated) int32 Defiance = 0;
	UPROPERTY(Replicated) bool bFinalCall = false;

	UPROPERTY(Replicated) int32 TeamLevel = 1;
	UPROPERTY(Replicated) int32 TeamXP = 0;
	UPROPERTY(Replicated) int32 XPToNext = 10;

	UPROPERTY(Replicated) int32 EnemyCount = 0;

	/** Megabonk-style level-up pause: the action is frozen (global time dilation) while players pick cards */
	UPROPERTY(Replicated) bool bPickPause = false;
	/** Real seconds left before everyone still choosing gets their first card auto-picked */
	UPROPERTY(Replicated) float PickPauseTimeLeft = 0.f;
	/** Humans in this pause, and how many of them have finished all their picks */
	UPROPERTY(Replicated) int32 PickersTotal = 0;
	UPROPERTY(Replicated) int32 PickersDone = 0;

	UPROPERTY(Replicated) TObjectPtr<ASQBoss> Boss;
	UPROPERTY(Replicated) TObjectPtr<ASQLantern> Lantern;
	UPROPERTY(Replicated) TObjectPtr<ASQHordeManager> Horde;
	UPROPERTY(Replicated) TObjectPtr<ASQArena> Arena;
	UPROPERTY(Replicated) bool bEndless = false;

	UPROPERTY(Replicated) FString AlertText;
	UPROPERTY(Replicated) float AlertServerTime = -100.f;
	UPROPERTY(Replicated) FString StoryText;

	// ---------------- Economy pass (Nov 7): gold, chests, Difficulty, timeline beats, Appointment Bell, Final Swarm, modifiers
	/** Raises enemy count / HP and rewards for the rest of the run: spawn rate and gold x(1 + Difficulty), enemy HP x(1 + DifficultyHealthShare x Difficulty) */
	UPROPERTY(Replicated) float Difficulty = 0.f;
	/** Contract Clauses (Modifiers.csv row i = bit i), picked by the host in the lobby; fixed for the run */
	UPROPERTY(Replicated) int32 ModifierMask = 0;
	/** Midnight passed without the bell: endless escalating spawns until the boss is beaten (or the crew is) */
	UPROPERTY(Replicated) bool bFinalSwarm = false;
	/** Seconds since midnight (drives the swarm's escalation and the HUD) */
	UPROPERTY(Replicated) float FinalSwarmSeconds = 0.f;
	/** Night progress frozen when the bell was rung (enemy HP / damage ramps hold there during the boss fight) */
	UPROPERTY(Replicated) float BossNightProgress = 1.f;
	/** The night's Appointment Bell (boss portal) */
	UPROPERTY(Replicated) TObjectPtr<ASQPortal> Portal;
	/** Last timeline beat announced on the HUD (Kind 1 miniboss, 2 swarm, 3 Final Swarm, 4 bell ready, 5 new objective, 6 objective complete, 7 objective failed) */
	UPROPERTY(Replicated) FString BeatText;
	UPROPERTY(Replicated) float BeatServerTime = -100.f;
	UPROPERTY(Replicated) uint8 BeatKind = 0;
	/** Living timeline miniboss (empty = none) and its health 0..1, for the HUD name bar */
	UPROPERTY(Replicated) FString MinibossName;
	UPROPERTY(Replicated) float MinibossHealthFrac = 0.f;

	// ---------------- Missions (Nov 9 2026; SQMission.h). Read by the HUD / menu (UI agent).
	/** The stage's objectives in order (FSQMissionDirector writes them 4x a second) */
	UPROPERTY(Replicated) TArray<FSQObjectiveView> Objectives;
	/** Seconds since the stage began (frozen during the boss fight) / the stage length. Past StageDuration = the Final Swarm (overtime). */
	UPROPERTY(Replicated) float StageTime = 0.f;
	UPROPERTY(Replicated) float StageDuration = 0.f;
	/** Missions.csv id of this run (the host picks it: menu USQMissions::SetPendingMission, or B in the lobby) */
	UPROPERTY(Replicated) FName MissionId;
	/** Hard mode (O in the lobby, once the mission is beaten) */
	UPROPERTY(Replicated) bool bHardMode = false;
	/** Nov 12 2026: the lobby is being skipped (solo PLAY from the main menu, or RETRY / PLAY AGAIN): the HUD shows a short
	 *  "rising" card in place of the lobby, and everyone counts as ready. Cleared when the stage starts. */
	UPROPERTY(Replicated) bool bSkipLobby = false;
	UPROPERTY(Replicated) ESQRunResult RunResult = ESQRunResult::None;
	/** Victory / defeat screen numbers (filled by ASQGameMode::EndRun) */
	UPROPERTY(Replicated) FSQRunStats RunStats;
	// ---------------- Lantern trail (Nov 12 2026; FSQMissionDirector runs it, ASQLantern and the HUD draw it; Docs/LanternTrail.md)
	/** The run's lantern sites in lighting order ([0] = the hub). Empty = no trail (Tuning LanternTrail 0): one lantern at the hub. */
	UPROPERTY(Replicated) TArray<FVector> TrailSites;
	/** Index of the LIT lantern (the one ASQGameState::Lantern stands on). == TrailSites.Num() once the trail is complete. */
	UPROPERTY(Replicated) int32 TrailStage = 0;
	/** Bit i set = lantern i burnt out before its quota was banked (no reward) */
	UPROPERTY(Replicated) int32 TrailMissedMask = 0;
	/** Souls banked at the lit lantern / the souls it needs */
	UPROPERTY(Replicated) int32 TrailBanked = 0;
	UPROPERTY(Replicated) int32 TrailQuota = 0;

	// ---------------- Ping wheel (Nov 23 2026, Docs/CoopFeatures.md): local, cosmetic, filled by ASQCharacter::MulticastPing
	struct FSQPing
	{
		FVector Where = FVector::ZeroVector;
		FString Who;
		double Time = 0.0;   // world time it arrived
		uint8 Kind = 0;      // 0 Here, 1 Help me, 2 Lantern, 3 Chest, 4 Danger, 5 Thanks
		uint8 Slot = 0;
	};
	TArray<FSQPing> LocalPings;
	void AddLocalPing(uint8 Kind, const FVector& Where, uint8 Slot, const FString& Who);
	static FLinearColor PingColor(uint8 Slot);
	static const TCHAR* PingLabel(uint8 Kind);

	// ---------------- The Long Haunt (Nov 24 2026, Docs/Content.md): optional loops after the mission boss
	/** Loops begun after the first boss fell (0 = the mission itself). Each loop: new lantern route, stronger horde and boss. */
	UPROPERTY(Replicated) int32 LoopIndex = 0;

	// ---- Run events, district conditions, the Daily Haunt (content item 12, Docs/Content.md). Server writes (ASQGameMode::TickEvents).
	/** Events.csv id of the running event (none = no event) */
	UPROPERTY(Replicated) FName EventId;
	/** Where the event is (its site, or the hunted creature); valid when bEventHasPos */
	UPROPERTY(Replicated) FVector EventPos = FVector::ZeroVector;
	UPROPERTY(Replicated) bool bEventHasPos = false;
	/** Server world times the event started / runs out */
	UPROPERTY(Replicated) float EventStartTime = 0.f;
	UPROPERTY(Replicated) float EventEndTime = 0.f;
	/** Site events: how far the hold has got, 0..100 */
	UPROPERTY(Replicated) uint8 EventHoldPct = 0;
	/** Events finished this run (HUD + end screen) */
	UPROPERTY(Replicated) int32 EventsDone = 0;
	/** Clocktower hands: angle time = ServerTime * ClockRate + ClockOffset (the offset keeps the hands continuous when the rate changes) */
	UPROPERTY(Replicated) float ClockRate = 1.f;
	UPROPERTY(Replicated) float ClockOffset = 0.f;
	/** District conditions for this run: Events.csv Condition ids and the ASQArena district each sits on (parallel arrays) */
	UPROPERTY(Replicated) TArray<FName> ConditionIds;
	UPROPERTY(Replicated) TArray<int32> ConditionDistricts;
	/** Daily Haunt: the run seed comes from today's date (same map, events and conditions for everyone that day; local record for now) */
	UPROPERTY(Replicated) bool bDailyRun = false;

	const FSQEventDef* ActiveEvent() const;
	/** The running event's multiplier (1 with no event) */
	float EventMult(float FSQEventDef::* Field) const;
	/** Drop multiplier at a map position: the running event x the condition of the district the position is in */
	float DropMult(const FVector2D& Pos, float FSQEventDef::* Field) const;
	/** The condition on a district (null = none) */
	const FSQEventDef* ConditionOn(int32 District) const;
	bool IsEventSpecial(ESQEventSpecial Special) const;
	/** Clocktower hand time (see ClockRate) */
	float ClockTime() const { return GetServerWorldTimeSeconds() * ClockRate + ClockOffset; }
	/** yyyymmdd of the local date: the Daily Haunt's seed and record key */
	static int32 TodayKey();
	void ClearEventState();
	/** > 0 while the host (or solo player) chooses CASH OUT or KEEP REAPING after a boss; real seconds left (cash out at 0) */
	UPROPERTY(Replicated) float LoopChoiceTimeLeft = 0.f;
	/** Bonus banked gold already locked in by cleared loops (paid to every reaper who was there, win or wipe) */
	UPROPERTY(Replicated) int32 LoopBonusGold = 0;
	/** The current loop boss's twist, for the HUD ("ESCORTED", "SWARMED"...) */
	UPROPERTY(Replicated) FString LoopBossMod;
	/** Horde / boss health, enemy damage and elite weight multipliers of the current loop (1 on the mission itself) */
	float LoopHealthMult() const;
	float LoopDamageMult() const;
	float LoopEliteMult() const;

	// ---------------- Run structure (Nov 25 2026, Docs/RunStructure.md): a chain of stages, soul rift, passage, endless horde
	/** 0-based stage of the run and how many stages it has (Tuning RunStages; 1 = the old single mission) */
	UPROPERTY(Replicated) int32 RunStage = 0;
	UPROPERTY(Replicated) int32 RunStageCount = 1;
	/** The next stage's mission name (shown in the passage) */
	UPROPERTY(Replicated) FString NextStageName;
	/** > 0 while the last lantern overflows (real seconds until the boss climbs out) */
	UPROPERTY(Replicated) float OverflowTimeLeft = 0.f;
	/** The boss's soul rift is open: walk into it */
	UPROPERTY(Replicated) bool bSoulRift = false;
	UPROPERTY(Replicated) FVector SoulRiftPos = FVector::ZeroVector;
	UPROPERTY(Replicated) float SoulRiftTimeLeft = 0.f;
	UPROPERTY(Replicated) int32 SoulRiftIn = 0;
	UPROPERTY(Replicated) int32 SoulRiftNeeded = 0;
	/** In the Soul Passage (falling to the next map) */
	UPROPERTY(Replicated) bool bSoulPassage = false;
	UPROPERTY(Replicated) float PassageTimeLeft = 0.f;
	/** The endless horde after the last boss (until the crew wipes) */
	UPROPERTY(Replicated) bool bEndlessHorde = false;
	UPROPERTY(Replicated) float EndlessSeconds = 0.f;
	UPROPERTY(Replicated) int32 EndlessKills = 0;
	/** Team kills this run (all stages) and souls banked this run (all stages + endless) */
	UPROPERTY(Replicated) int32 RunKills = 0;
	UPROPERTY(Replicated) int32 RunSoulsBanked = 0;
	/** Drowned King Flood / King Tide: the tide is pushed up by BossTideRise between these server times (ASQArena::TideLevel) */
	UPROPERTY(Replicated) float BossTideStart = 0.f;
	UPROPERTY(Replicated) float BossTideEnd = 0.f;
	UPROPERTY(Replicated) float BossTideRise = 0.f;
	/** True between stages (rift / passage): no spawns, no stage clock */
	bool IsBetweenStages() const { return bSoulRift || bSoulPassage; }

	/** True once every objective is resolved: the Appointment Bell is up (or the stage clock ran out) */
	UPROPERTY(Replicated) bool bBossReady = false;

	const FSQMissionDef* GetMission() const;
	/** 0..1 through the whole stage (1 in overtime / the boss fight once the clock ran out) */
	float StageProgress() const;
	/** The mission's (and Hard mode's) multiplier for one modifier number: SpawnMult, EnemyDamageMult, GoldMult (1 for the others) */
	float MissionModValue(float FSQModifierDef::* Field) const;
	/** Mission horde / boss HP multiplier (HealthMult or BossHealthMult, x Hard) */
	float MissionHealthMult(bool bBoss) const;
	/**
	 * Act ramp smoothing (horde HP): the horde computes (Health + HealthPerNight x act) x (1 + per-minute ramp in the act), which jumps at
	 * every act change. This returns the factor that eases it log-linearly between act starts, assuming HealthPerNight = StageHpRefRatio x Health.
	 */
	float StageHealthSmoothing() const;

	/** Server: Difficulty += Amount (shrines, modifiers). Never below 0. */
	void AddDifficulty(float Amount);
	void PushBeat(const FString& Text, uint8 Kind);
	/** Server: XP held back by the Probation clause (paid out at its time) */
	int32 HeldXP = 0;
	/** Server: fractional team XP from the XP bonuses (Night School meta, Study Group augment) */
	float XPRemainder = 0.f;

	bool HasModifier(int32 Index) const { return Index >= 0 && Index < 31 && (ModifierMask & (1 << Index)) != 0; }
	/** Product of one Modifiers.csv number over the active clauses (1 when none), x the mission's own multiplier (MissionModValue) */
	float ModProduct(float FSQModifierDef::* Field) const;
	/** Largest value of one number over the active clauses (0 when none) */
	float ModMax(float FSQModifierDef::* Field) const;
	bool ModAny(bool FSQModifierDef::* Field) const;
	/** Banked gold multiplier of the active clauses (Tuning ModifierCoinCap) */
	float CoinMultiplier() const;
	/** NightSeconds / BalanceNightSeconds (0.1..4): short test nights scale HP steps, boss HP and prices */
	float NightScale() const;
	/** Gold multiplier: (1 + Difficulty) x the clauses' GoldMult */
	float GoldMultiplier() const;
	/** Coin value growth on the current night: 1 + GoldPerNight x night index */
	float GoldNightGrowth() const;
	/** Tuning EconomyBossHealth<N> / EconomyEnemyHealth<N> (act N) x the mission's HP multiplier; the horde's also x StageHealthSmoothing */
	float EconomyHealthMult(bool bBoss) const;
	/**
	 * What a paid chest costs THIS reaper: ChestBasePrice x (1 + ChestPriceK x their paid opens this run) ^ ChestPriceExp,
	 * capped at ChestPriceCap, x NightScale x the clauses' ChestPriceMult. Per reaper (every reaper has a purse, and co-op coins
	 * are shared out), so a 4-player crew pays solo prices and nobody's chest makes a teammate's dearer.
	 */
	int32 ChestPriceFor(const ASQPlayerState* PS) const;

	/** Server only */
	void AddTeamXP(int32 Amount);
	void PushAlert(const FString& Text);
	void ResetTeamProgress();

	/** 0..1 through the current night */
	float NightProgress() const;

	/** Current price of Shop.csv row Index: Cost x (1 + ShopPricePerBuy x buys this visit) x (1 + ShopPricePerNight x night) x (1 + ShopPricePerPlayer x extra players) x NightSeconds / BalanceNightSeconds */
	int32 ShopPrice(int32 Index) const;
};
