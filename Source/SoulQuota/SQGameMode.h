// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SQMission.h"
#include "SQGameMode.generated.h"

class ASQBoss;
class ASQCharacter;
class ASQPlayerState;
class ASQGameState;
class USQData;
struct FSQEventDef;

/**
 * Server-only run director (Nov 9 2026 mission-based missions). A run is ONE mission (Missions.csv; the host picks it in the menu or with
 * B in the lobby, O toggles Hard mode once it is beaten):
 *   lobby (ready-up) -> the stage starts at the map's player starts -> objectives in sequence (FSQMissionDirector) with two minibosses
 *   (augment offers) and act swarms (Timeline.csv) -> every objective resolved: the Appointment Bell rises -> ring it -> the mission boss
 *   -> boss beaten = VICTORY (stats screen, gold banked). The stage clock running out first = the FINAL SWARM (overtime, escalating) and the
 *   bell rises anyway (failed objectives = Defiance stacks on the boss). Whole crew down (solo: down = dead) = DEFEAT (a share still banked).
 * The stage runs in 3 acts (a third each): GS NightIndex = the act, so the horde's per-night tuning carries over.
 */
UCLASS()
class SOULQUOTA_API ASQGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASQGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void Logout(AController* Exiting) override;

	void OnBossDefeated(ASQBoss* Boss);
	/** Server: E from PS near a chest (opens it) or the Appointment Bell (starts the ring) */
	void HandleUse(ASQPlayerState* PS);
	/** Server: the bell finished ringing: the stage clock stops and the mission boss rises at the bell */
	void RingBell(ASQCharacter* By);
	/** Death's Office moved to the main menu (permanent upgrades): always false */
	bool TryBuy(ASQPlayerState* Buyer, int32 Index);
	void AddBot();
	void SkipPhase();
	/** End screen, host only: straight into a new run with the same reapers, mission and mode (no lobby) */
	void RetryRun();
	/** End screen, host only: back to the lobby now (change reaper / mission) */
	void EndToLobby();
	/** The Long Haunt: the host's answer after a boss (true = KEEP REAPING, false = CASH OUT). Server. */
	void HandleLoopChoice(bool bKeepGoing);
	/** Lobby, host (B): the next unlocked mission (kept as the pending mission in the host's profile) */
	void CycleBiome();
	/** Lobby, host (O): Hard mode on / off (only once the mission is beaten) */
	void ToggleEndless();
	/** Lobby (host): the Daily Haunt on / off (content item 12): today's seed for the map, events and conditions */
	void ToggleDaily();
	/** The Daily Haunt's seed for today (same on every machine with the same local date) */
	static int32 DailySeed();

	/** The live mission's objectives / minibosses (USQAugments::OnKill / OnHit forward horde events here) */
	FSQMissionDirector& GetDirector() { return Director; }

protected:
	/** Everyone is ready: the mission stage begins */
	void StartStage();
	/** A new act (0..2): act swarms re-arm, the paid chests are topped up */
	void SetAct(int32 Act);
	/** The bell was rung: raise the mission boss at the bell, freeze the stage clock */
	void StartBossFight(ASQCharacter* RungBy);
	/** The stage clock ran out before the boss: the Final Swarm (endless, escalating; Timeline.csv FinalSwarm tiers) */
	void BeginFinalSwarm();
	/** Every objective resolved (or overtime): the Appointment Bell rises at the map's boss spot */
	void RaiseBell();
	/** Act swarms / extra minibosses at their share of the act, Final Swarm tier announcements */
	void TickTimeline(ASQGameState* GS);
	/** Paid chests for an act (unopened ones stay; capped) */
	void PlaceChests(int32 Act);
	/** Server-side quest events that no other system reports (damage taken, dashes, carrying, plateaus, revives...) */
	void TickQuestTracking(ASQGameState* GS, const USQData* D, float DeltaSeconds);
	/** Bots have no E key: they spend their gold on chest rolls straight away */
	void TickBotChests(ASQGameState* GS);
	/** Every reaper gets Stat (team events: objectives, bosses, banking) */
	void CreditAll(ASQGameState* GS, FName Stat, int32 Amount = 1);
	void EndRun(bool bWon);
	void ResetRun();
	void CheckWipe();
	void HandleBossDefeated();
	/** The host's pending mission (menu / profile) onto the game state */
	void ApplyMissionChoice();
	/** Lantern onto the map's hub (ASQArena BankSouls spot 0) */
	void PlaceLantern();
	FTransform GetSpawnTransform();
	/** Run start: the map's player start Index (ASQArena::GetPlayerStart), capsule height added */
	FVector GetStartSpot(int32 Index) const;
	FName PickReaper() const;
	int32 CountTeam() const;
	const struct FSQBossDef* PickBossForNight(int32 Night) const;
	FName CurrentBiome() const;
	/**
	 * Level-up picking. Solo (one human, bots don't count): the world freezes (global time dilation) while the cards are up.
	 * Co-op (2+ humans): never freezes; picks bank per player, the strip's short protective bubble and the idle auto-pick run here.
	 */
	void UpdatePickPause(ASQGameState* GS, const USQData* D, float DeltaSeconds);

	// ---- The Long Haunt (Nov 24 2026): optional loops after the mission boss. Docs/Content.md.
	/** The boss fell and loops are on: lock what was earned, clear the field and ask the host */
	void BeginLoopChoice();
	/** KEEP REAPING: a new stage on the same map (new lantern route, stronger horde, the boss returns with a twist) */
	void StartLoop();
	/** True once the mission boss has fallen this run: from then on the run counts as won however it ends */
	bool bLoopVictoryLocked = false;

	// ---- Run structure (Nov 25 2026, Docs/RunStructure.md): stage chain, overflow, soul rift, passage, endless horde
	/** Missions.csv ids of this run's stages, in order */
	TArray<FName> StageChain;
	/** Tuning BossFromLantern on and the stage has a trail: the boss climbs out of the last lantern (no bell) */
	bool UseLanternBoss() const;
	/** The last lantern is filled (or burnt out): the overflow warning, then the boss */
	void BeginOverflow();
	/** The stage boss fell and another stage follows: its soul rift opens where it died */
	void BeginSoulRift(const FVector& BossAt);
	void TickSoulRift(ASQGameState* GS, const USQData* D, float DeltaSeconds);
	/** A reaper walks into the rift (or is pulled in): it drops into the passage tunnel to wait for the crew */
	void EnterRift(ASQCharacter* Reaper);
	/** Everyone is in: the next map is built below, augment picks, floating chests, the fall */
	void BeginPassage();
	void TickSoulPassage(ASQGameState* GS, const USQData* D, float DeltaSeconds);
	/** The passage ends: land on the next map and start its stage (level, items, gold carry over) */
	void StartNextStage();
	/** After the last stage's boss: the endless horde until the crew wipes */
	void BeginEndless();
	void TickEndless(ASQGameState* GS, const USQData* D, float DeltaSeconds);
	/** Team kills this run (sum of every reaper's kills) */
	int32 SumKills(const ASQGameState* GS) const;
	FVector PassageSpot() const;
	TWeakObjectPtr<class ASQSoulRift> RiftActor;
	TWeakObjectPtr<class ASQSoulRift> PassageActor;
	TArray<TWeakObjectPtr<ASQCharacter>> RiftEntered;
	float RiftBotTimer = 0.f;
	float EndlessLanternTimer = 0.f;
	float EndlessEliteTimer = 0.f;
	float EndlessMinibossTimer = 0.f;
	int32 EndlessKillBase = 0;
	int32 EndlessLanternIdx = 0;

	float WaitTimer = 3.f;
	/** RetryRun: the next start keeps bSkipLobby even with several humans */
	bool bRetryStart = false;
	bool bBossDefeatedPending = false;
	int32 SpawnCounter = 0;
	int32 BotCounter = 0;

	// ---- Platform pass (Nov 2026): reconnect. A human who drops mid-run leaves their reaper on the field under a stand-in
	// bot (it keeps weapons, level-ups, sack, health; the bot's player state holds gold, augments and pending picks). The
	// same profile (stable id sent as the travel option SQId) rejoining takes the seat back. See Docs/Platform.md.
	struct FHeldSeat
	{
		FString StableId;
		TWeakObjectPtr<class ASQBotController> Bot;
		/** Real time after which the seat is no longer handed back (the bot simply plays on) */
		double ExpiresReal = 0.0;
	};
	TArray<FHeldSeat> HeldSeats;
	/** The seat that belongs to this controller's stable id, if it is still held (index into HeldSeats, or INDEX_NONE) */
	int32 FindHeldSeat(const AController* Who) const;
	/** New late joiner (not a reconnect): bank level-ups and gold near the squad's, so they are useful at once */
	void GrantLateJoinCatchUp(class ASQPlayerState* PS);
	/** A notice toast on every human's screen (except one, e.g. the player it is about) */
	void NoticeAll(const FString& Text, const FString& Sub, const class ASQPlayerState* Except);
	/** FPlatformTime::Seconds() when the current pick pause began (world time is frozen during it) */
	double PickPauseStartReal = 0.0;

	// ---- Missions
	FSQMissionDirector Director;
	/** Timeline.csv rows already fired this act (Miniboss / Swarm) or announced (FinalSwarm tiers) */
	TArray<bool> BeatsFired;

	// ---- Run events (content item 12; Events.csv, Docs/Content.md). Server only.
	/** Stage progress (0..1) at which each of this stage's events starts, ascending */
	TArray<float> EventTimes;
	int32 EventsStarted = 0;
	/** Events already used this run (no repeats until the list is exhausted) */
	TArray<FName> EventsUsed;
	float EventHold = 0.f;
	float EventPosTimer = 0.f;
	float EventRetryTimer = 0.f;
	bool bEventHadMiniboss = false;
	TWeakObjectPtr<class ASQEventSite> EventSite;
	void PlanEvents(ASQGameState* GS, const USQData* D, bool bNewRun);
	void TickEvents(ASQGameState* GS, const USQData* D, float DeltaSeconds);
	bool StartEvent(ASQGameState* GS, const USQData* D, const FSQEventDef& Def);
	void EndEvent(ASQGameState* GS, const USQData* D, bool bDone, bool bQuiet);
	void GrantEventReward(ASQGameState* GS, const USQData* D, const FSQEventDef& Def, const FVector& At);
	void SetClockRate(ASQGameState* GS, float NewRate);
	float QuestFlushTimer = 0.f;
	/** World time the boss rose (quest: fast boss kills; stats) and the run began (quest: fast win) */
	float BossFightStart = 0.f;
	float RunStartTime = 0.f;
	/** Seeds the chest placement for the run (deterministic per run and act) */
	int32 RunSeed = 1;
	bool bRunAnyoneDowned = false;
	/** GS->Banked last tick: souls banked at the lantern turn into gold (Tuning SoulGold) */
	int32 LastBanked = 0;
	/** The lobby has put the lantern on the map's hub once the arena exists */
	bool bLobbyLanternPlaced = false;
	/** Skull -> who carried it last tick (a revive at the lantern credits the carrier) */
	TMap<TWeakObjectPtr<ASQCharacter>, TWeakObjectPtr<ASQCharacter>> SkullCarriers;
};
