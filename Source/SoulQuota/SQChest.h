// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQChest.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ASQPlayerState;
class ASQCharacter;
class USQInteractCue;

/**
 * A chest on the map (economy pass, Megabonk gap #3). Walk up and press F (interact).
 *   Paid chest: costs the opener's own price (ASQGameState::ChestPriceFor: rises with every paid chest they open this run).
 *               The Skeleton Key item gives a chance x/(x+1) that it costs nothing (and then the price does not rise).
 *   Free chest: golden; dropped by crowned elites and timeline minibosses. Never costs gold, never raises the price.
 * Opening gives the opener one ITEM (Upgrades.csv Category Item, by Weight; rarity from the Tuning odds + their Luck + ChestLuckBonus).
 * Server-authoritative; clients only animate the lid.
 */
UCLASS()
class SOULQUOTA_API ASQChest : public AActor
{
	GENERATED_BODY()

public:
	ASQChest();

	/** Server: places a chest on the ground under Location (Z snaps to the arena ground). */
	static ASQChest* SpawnChest(UWorld* World, const FVector& Location, bool bInFree);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(ReplicatedUsing = OnRep_Look) bool bFree = false;
	UPROPERTY(ReplicatedUsing = OnRep_Look) bool bOpened = false;

	/** Server: Opener pressed E within range. False when it is gone, or they can't pay (they get a notice). */
	bool TryOpen(ASQPlayerState* Opener);

	/** Server: one chest roll for PS (a relic they can take, else a weapon level, else a heal). Credits quests, tells the player. */
	static void GrantChestRoll(ASQPlayerState* PS, bool bFreeOpen, bool bKeyOpen);
	/** Skeleton Key chance for a reaper: x / (x + 1), x = sum of the ChestKey stat over their relics */
	static float KeyChance(const ASQCharacter* Reaper);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Band;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Lid;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Halo;
	/** Shared interactable look (gold ring, diamond, "CHEST [F]" label); dims once opened */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USQInteractCue> Cue;

protected:
	UFUNCTION() void OnRep_Look();
	void ApplyLook();

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HaloMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BandMID;
	float LidOpen = 0.f;
	/** Seen closed on this machine, so the opening burst plays once when it opens (not for chests that arrive already open) */
	bool bOpenFxArmed = false;
	float LookTime = 0.f;
	bool bLookApplied = false;
	bool bLookFree = false;
	/** Nov 25 2026: the imported bone chest (SM_Chest_Bone + SM_Chest_BoneLid) is in use; false = the primitive stand-in */
	bool bBoneModel = false;
};
