// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SQTypes.h"
#include "SQShrine.generated.h"

class ASQCharacter;
class ASQBoss;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
struct FSQShrineDef;

/** Shrines.csv Type column */
enum class ESQShrineType : uint8 { Charge, Relic, Challenge, Greed, Magnet, Cursed, Key, Crypt, Pot };

/** Replicated as a uint8 (ASQShrine::State) */
enum class ESQShrineState : uint8 { Buried, Idle, Charging, Choosing, Active, Spent, Locked };

/**
 * Server-side ledger of shrine stat boosts. ASQCharacter::RecomputeStats rebuilds Stats from the reaper + relics +
 * bonds and knows nothing about shrines, so after every frame's actor ticks this re-adds each reaper's shrine boosts
 * (permanent raises from charge shrines, the timed pickup range of the Collections Altar) on top of whatever the
 * character last computed (its RawStats, then SQ::FinalizeStats: same caps / curves). Cleared when a new run starts (Waiting phase).
 * HOOK WANTED (SQCharacter owner): an "ExternalBoosts" map applied inside RecomputeStats would make this unnecessary.
 */
UCLASS()
class SOULQUOTA_API USQBoostLedger : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USQBoostLedger* Get(const UObject* WorldContext);

	/** Server: add Value of a SQ::ApplyStat stat to a reaper. Seconds <= 0 = for the rest of the run. */
	void AddBoost(ASQCharacter* Reaper, FName Stat, float Value, float Seconds = 0.f);
	void ClearAll();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FLedgerBoost
	{
		FName Stat;
		float Value = 0.f;
		float TimeLeft = -1.f; // < 0 = permanent
	};
	struct FLedgerEntry
	{
		TArray<FLedgerBoost> Boosts;
		FSQStats BaseStats;     // what the character computed itself (no shrine boosts)
		FSQStats AppliedStats;  // what we last wrote (base + boosts)
		bool bApplied = false;
		bool bDirty = true;
		float LastHealth = -1.f;
	};
	TMap<TWeakObjectPtr<ASQCharacter>, FLedgerEntry> Entries;
};

/**
 * A map interactable from Shrines.csv (one actor class, behaviour by Type). Server-authoritative and replicated;
 * scattered by ASQArena every night. Interaction is proximity / walk-in (the E key belongs to skull carrying):
 *   Charge   - stand in the ring; more reapers charge it faster. Then every reaper who was in range picks 1 of 3 stat
 *              boosts by standing on a pedestal for a moment (bots pick at once; leftovers get a random one after
 *              ShrinePickSeconds). Golden / buried versions offer Legendary boosts only.
 *   Relic    - walk in: 2-3 relic pedestals; the first reaper to stand on one takes that relic (normal level-up rules).
 *   Challenge, Greed, Magnet, Cursed, Key, Crypt - see Shrines.csv header.
 * Visuals: Halloween Bits prop (primitive fallback), glow MIDs, a progress ring, a beam while working, and world-space
 * text (prompt + pedestal labels) that faces the local camera.
 */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQShrine : public AActor
{
	GENERATED_BODY()

public:
	ASQShrine();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Server: call right after spawning (Shrines.csv Id). */
	void InitShrine(FName InDefId);

	static ESQShrineType ParseType(FName TypeName);
	const FSQShrineDef* GetDef() const;
	ESQShrineType GetShrineType() const;
	ESQShrineState GetShrineState() const { return static_cast<ESQShrineState>(State); }

	UPROPERTY(ReplicatedUsing = OnRep_Def) FName DefId;
	/** ESQShrineState */
	UPROPERTY(ReplicatedUsing = OnRep_State) uint8 State = 1;
	/** 0..1: charge / dig / challenge timer / magnet timer / copy health / pick time left (by state) */
	UPROPERTY(Replicated) float Progress = 0.f;
	/** Choosing: stat names (Charge) or relic Ids (Relic), their values (Charge) and rarities */
	UPROPERTY(Replicated) TArray<FName> OfferIds;
	UPROPERTY(Replicated) TArray<float> OfferValues;
	UPROPERTY(Replicated) TArray<uint8> OfferRarity;
	/** Choosing: reapers still to pick (Charge) */
	UPROPERTY(Replicated) uint8 PicksLeft = 0;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> ShrineRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> OrbMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BeamMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> GlowLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> PromptText;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> RingLit;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> RingDim;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> PedestalBases;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> PedestalOrbs;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UTextRenderComponent>> PedestalTexts;

	/** Kind 0 = small pop, 1 = big completion burst, 2 = reward (chest / gold), 3 = fail fizzle */
	UFUNCTION(NetMulticast, Unreliable) void MulticastShrineFx(FVector_NetQuantize At, FColor Color, uint8 Kind);

protected:
	UFUNCTION() void OnRep_Def();
	UFUNCTION() void OnRep_State();

	// --- Server ---
	void ServerTick(float DeltaSeconds);
	void SetShrineState(ESQShrineState NewState);
	void GatherStanding(TArray<ASQCharacter*>& Out) const;
	/** Standing reapers within Radius (2D) whose feet are near this shrine's height (not on a plateau above it) */
	int32 CountNear(const TArray<ASQCharacter*>& Reapers, float Radius, ASQCharacter** OutFirst = nullptr) const;
	/** Pickers[0] is the reaper who opened it (bFirstCome: a relic desk, only one pick in total) */
	void BeginChoosing(const TArray<ASQCharacter*>& Pickers, bool bFirstCome);
	bool RollOffers(const TArray<ASQCharacter*>& Pickers);
	uint8 RollRarity(int32 MinRarity) const;
	void GrantOffer(ASQCharacter* Reaper, int32 Index);
	void TickChoosing(float DeltaSeconds, const TArray<ASQCharacter*>& Reapers);
	void StartChallenge(int32 TeamSize);
	void TickChallenge(float DeltaSeconds, const TArray<ASQCharacter*>& Reapers);
	void SpawnCursedCopy();
	void TickCursed();
	void DismissCopy();
	void PayEveryone(int32 GoldEach) const;
	void SpawnFreeChest(const FVector& Near) const;
	float NightGoldMult() const;
	FVector PedestalLocation(int32 Index) const;
	float PedestalDistance() const;
	void Alert(const FString& Text) const;

	TArray<TWeakObjectPtr<ASQCharacter>> Eligible;
	bool bFirstComeOnly = false;
	float StateTime = 0.f;
	float FailTimer = 0.f;
	bool bSecondWave = false;
	TWeakObjectPtr<ASQCharacter> DwellWho[3];
	float DwellTime[3] = { 0.f, 0.f, 0.f };
	TWeakObjectPtr<ASQBoss> CopyBoss;
	bool bCopyAlive = false;
	bool bWaitingForCopy = false;
	int32 CopiesLeft = 0;
	FVector LastCopyLoc = FVector::ZeroVector;

	// --- Visuals (every machine except a dedicated server) ---
	void ApplyDefVisuals();
	void BuildRing();
	void TickVisuals(float DeltaSeconds);
	FString BuildPromptText(const FSQShrineDef& Def) const;
	bool bVisualsBuilt = false;
	bool bRingGrounded = false;
	/** Map remaster: a spent shrine turns grey so it stops competing with live ones */
	bool bSpentLookShown = false;
	FLinearColor BodyLiveColor = FLinearColor::White;
	int32 RingLitShown = -1;
	float VisualClock = 0.f;
	float FxClock = 0.f;
	float BodySink = 0.f;
	float BodyBaseZ = 0.f;
	FString LastPrompt;
	TArray<FString> LastPedestalText;
	TArray<FTransform> RingSegments;
	TArray<FTransform> ScratchLit;
	TArray<FTransform> ScratchDim;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OrbMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BeamMID;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PedestalMIDs;
};
