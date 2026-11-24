// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SQPlayerState.generated.h"

/** A short local-only HUD message for this player (quest done, chest reward, "not enough gold") */
struct FSQLocalToast
{
	FString Text;
	FString Sub;
	uint8 Kind = 0;      // 0 notice, 1 quest complete, 2 chest reward, 3 coins
	double Time = 0.0;   // world real time
	uint8 Rarity = 0;
};

/** Per-player run data: chosen reaper, pending level-up offers (with rarity), rerolls, kills, gold, quest progress */
UCLASS()
class SOULQUOTA_API ASQPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Owning client: its PlayerState arrived; sends the profile's meta upgrade ranks to the server */
	virtual void ClientInitialize(AController* C) override;

	UPROPERTY(Replicated) FName ReaperId = TEXT("Grim");
	UPROPERTY(Replicated) TArray<FName> Offers;
	/** Rarity of each offer card (0 Common, 1 Rare, 2 Epic, 3 Legendary), same length as Offers */
	UPROPERTY(Replicated) TArray<uint8> OfferRarity;
	/** Bumped every time a new set of offers is rolled: a pick / reroll / skip must name the set it was made on */
	UPROPERTY(Replicated) uint8 OfferSerial = 0;
	UPROPERTY(Replicated) int32 PendingLevelUps = 0;
	UPROPERTY(Replicated) int32 RerollsLeft = 0;
	/** Part of the current team-wide level-up pause (for the "1/3 picked" overlay) */
	UPROPERTY(Replicated) bool bInPickPause = false;
	UPROPERTY(Replicated) bool bShopReady = false;
	UPROPERTY(Replicated) int32 Kills = 0;

	// ---------------- Economy pass (Nov 7): gold + chests. Gold is this reaper's in-run purse (resets every run); it buys chests.
	// Souls stay the team's quota + Death's Office currency. Contract used by the world agent (shrines / pots):
	UPROPERTY(Replicated) int32 Gold = 0;
	/** Paid chests this reaper opened this run (raises their price; free opens never count) */
	UPROPERTY(Replicated) int32 ChestsOpened = 0;
	/** Server: + Amount gold (negative ignored) */
	void AddGold(int32 Amount);
	/** Server: takes Amount if the purse holds it */
	bool SpendGold(int32 Amount);
	/** Server: fractional gold (shared-purse coin splits); whole coins land in Gold */
	void AddGoldExact(float Amount);
	float GoldRemainder = 0.f;

	/** Server: E near a chest / the Appointment Bell (sent by ASQPlayerController::OnInteract) */
	UFUNCTION(Server, Reliable) void ServerUseNearby();
	/** Server, host only, lobby only: toggles Contract Clause Index (Modifiers.csv row) */
	UFUNCTION(Server, Reliable) void ServerToggleModifier(int32 Index);

	// ---------------- Quests (Quests.csv). The server credits events; the owning PC keeps the profile.
	/** Server: count a quest stat for this reaper (sent in batches every QuestFlushSeconds; bots are ignored).
	 *  Other systems call this too, e.g. CreditQuest(TEXT("ShrinesUsed")), CreditQuest(TEXT("SlideKills")). */
	void CreditQuest(FName Stat, int32 Amount = 1);
	/** Server: send pending quest stats to the owning PC now */
	void FlushQuests();
	UFUNCTION(Client, Reliable) void ClientQuestProgress(const TArray<FName>& Stats, const TArray<int32>& Amounts);
	UFUNCTION(Client, Reliable) void ClientQuestRunStart();
	/** End of run: folds this run's bests into the profile, pays the clause bonus (BonusCoins) and saves */
	UFUNCTION(Client, Reliable) void ClientQuestRunEnd(bool bWon, FName PlayedReaper, int32 BonusCoins);
	UFUNCTION(Client, Reliable) void ClientNotice(const FString& Text, const FString& Sub, uint8 Kind, uint8 Rarity);
	/** Owning PC: this run's quest stats so far (Mode Best quests read these) */
	TMap<FName, int32> LocalRunStats;
	/** Owning PC: toasts for the HUD (newest last) */
	TArray<FSQLocalToast> LocalToasts;
	void AddLocalToast(const FString& Text, const FString& Sub, uint8 Kind, uint8 Rarity = 0);

	// ---------------- Quest-locked items (rewards of Quests.csv Item:<Id>)
	/** Server: this reaper's quest-locked upgrades (never offered on level-up cards or chest rolls) */
	TArray<FName> LockedItems;
	UFUNCTION(Server, Reliable) void ServerSetLockedItems(const TArray<FName>& Locked);
	/** Owning PC: sends the profile's locked items (the HUD calls this until it has gone once, and again after an unlock) */
	void SendLockedItems();
	bool bLocksSent = false;
	bool IsItemLocked(FName Id) const;

	// ---------------- Server-side run tracking for quests (not replicated)
	TMap<FName, int32> PendingQuest;
	float TrackLastHealth = -1.f;
	float TrackLastDashCd = 0.f;
	int32 TrackLastCarried = 0;
	int32 TrackRunMaxCarried = 0;
	int32 TrackPlateau = INDEX_NONE;
	bool bTrackHitThisNight = false;
	bool bTrackWasDowned = false;
	float TrackDamageRemainder = 0.f;

	/** Server only (not replicated): the night index this player joined the current run on (0 = from the start) */
	int32 RunJoinNight = 0;
	/** Server only: the profile's stable id (travel option SQId), used to hand a held seat back after a disconnect */
	FString StableId;
	/** Server only: this state stands in for a human who dropped (a bot holds their reaper) */
	bool bHeldSeat = false;
	/** Server only: a mid-run joiner may apply their menu reaper once, until this world REAL time (-1 = closed) */
	double MidRunPickDeadline = -1.0;

	/** True while this player has a set of cards to choose from (level-up cards, or an augment set: bAugmentOffer) */
	bool IsChoosing() const { return Offers.Num() > 0 && (bAugmentOffer ? PendingAugments > 0 : PendingLevelUps > 0); }

	// ---------------- Augments (Nov 9 2026; SQAugments.h). Offered on the level-up card path: while bAugmentOffer is set, Offers /
	// OfferRarity hold the augment ids and their tiers (0 Silver, 1 Gold, 2 Prism) and the card UI draws them in the AUGMENT style.
	// A pick (ChooseOffer / ServerChooseAugment) goes to the augment. Augment sets come before waiting level-up sets; they can be
	// rerolled (one reroll) but not skipped.
	/** The augment set on offer (same ids as Offers while bAugmentOffer) */
	UPROPERTY(Replicated) TArray<FName> AugmentOffers;
	/** Augments this reaper owns this run (in pick order; a stackable one can appear more than once) */
	UPROPERTY(Replicated) TArray<FName> OwnedAugments;
	/** The cards on offer are augments (UI: AUGMENT card style) */
	UPROPERTY(Replicated) bool bAugmentOffer = false;
	/** Augment picks waiting (each one opens a set) */
	UPROPERTY(Replicated) int32 PendingAugments = 0;
	/** Client -> server: take augment Index of AugmentOffers */
	UFUNCTION(Server, Reliable) void ServerChooseAugment(int32 Index);
	/** Server: one more augment pick (Source = ESQAugmentSource as uint8) */
	void GrantAugmentPick(uint8 Source);
	/** Server */
	void ChooseAugment(int32 Index);
	bool IsChoosingAugment() const { return bAugmentOffer && AugmentOffers.Num() > 0 && PendingAugments > 0; }

	// ---------------- Meta upgrades (Death's Office, MetaUpgrades.csv). Each PC owns its ranks; the server keeps a validated copy.
	/** Server copy of this player's ranks (id -> rank; clamped to MetaUpgrades.csv MaxRank) */
	TMap<FName, int32> MetaRanks;
	UFUNCTION(Server, Reliable) void ServerSetMetaRanks(const TArray<FName>& Ids, const TArray<uint8>& Ranks);
	/** Owning PC (or the host): sends the profile's ranks to the server */
	void SendMetaRanks();
	int32 GetMetaRank(FName Id) const { const int32* Found = MetaRanks.Find(Id); return Found ? *Found : 0; }
	/** Sum of USQMissions::MetaValueAt over the lines with this Stat (XPGain, GoldGain, ReviveSeconds, Rerolls, StartLevels, Pardon...) */
	float GetMetaValue(FName Stat) const;

	// ---------------- Mission end (server -> owning PC)
	/** Gold this reaper earned this run (every AddGold, before spending): the banked share is worked out from it */
	int32 RunGoldEarned = 0;
	/** Banks GoldBanked into the profile (plus the first-clear bonus the profile has not had), marks the mission cleared, saves */
	UFUNCTION(Client, Reliable) void ClientMissionEnd(FName MissionId, bool bHard, bool bWon, int32 GoldBanked, float StageSeconds);

	// --- Co-op pick flow (server only; see ASQGameMode::UpdatePickPause) ---
	/** The owner's "Auto-pick level-ups" setting (SQSave), sent by ServerSetAutoPick: picks the rarest card at once */
	bool bAutoPick = false;
	/** The owner's pick strip is expanded (sent by ServerSetPickStripOpen): drives the short protective bubble */
	bool bPickStripOpen = false;
	/** Seconds the current card set has been waiting untouched (co-op auto-pick after Tuning PickAutoSeconds) */
	float OfferIdleSeconds = 0.f;
	/** Index of the rarest card on offer (first one on a tie); INDEX_NONE when there are no cards */
	int32 BestOfferIndex() const;

	/** Server only */
	void GrantLevelUp();
	void GenerateOffers();
	void ChooseOffer(int32 Index);
	void RerollOffers();
	void SkipOffer();
	void ResetForRun();
	/** Rarity roll from the Tuning base odds and Luck (0 Common .. 3 Legendary) */
	uint8 RollRarity(float Luck) const;

private:
	void RollOffers(const TArray<FName>& Avoid);
	void RollAugmentOffers(const TArray<FName>& Avoid);
	void AfterOfferResolved();
	void AfterAugmentResolved();
	/** Gold after the Golden Touch meta bonus; whole coins land in Gold (remainder kept) */
	void AddGoldRaw(int32 Amount);
	/** Server: where each waiting augment pick came from (ESQAugmentSource) */
	TArray<uint8> PendingAugmentSources;
};
