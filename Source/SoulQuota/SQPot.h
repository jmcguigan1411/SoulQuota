// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQPot.generated.h"

class ASQCharacter;
class APlayerState;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USQInteractCue;
struct FSQShrineDef;

/**
 * A smashable urn / pumpkin (Shrines.csv Type Pot). Server-authoritative and replicated; scattered by ASQArena.
 * Smashed by walking into it (PotTouchRadius) or by a reaper's auto-attacks reaching it (any standing reaper within
 * PotAttackRange during the night breaks it a moment later). Loot table = Shrines.csv Boosts "Kind:Weight:Amount":
 * Gold (to the smasher), XP (dropped as sparks), Heal (fraction of max HP), Coins (banked gold to the smasher's own profile).
 */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQPot : public AActor
{
	GENERATED_BODY()

public:
	ASQPot();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: call right after spawning (Shrines.csv Id). */
	void InitPot(FName InDefId);
	/** Server: smash it now. By may be null (then the nearest standing reaper gets the gold / heal). */
	void Smash(ASQCharacter* By);
	/** Server hook for weapons: smashes every pot within Radius of Center. Returns how many broke. */
	static int32 SmashInRadius(UWorld* World, const FVector2D& Center, float Radius, ASQCharacter* By);

	UPROPERTY(ReplicatedUsing = OnRep_Def) FName DefId;
	UPROPERTY(ReplicatedUsing = OnRep_Smashed) bool bSmashed = false;
	/** What came out: 0 gold, 1 XP, 2 heal, 3 banked gold (sent with bSmashed for the burst colour) */
	UPROPERTY(Replicated) uint8 LootKind = 0;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> PotRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PotBody;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PotNeck;
	/** Shared interactable look: soft-white ground ring + floating diamond + "SMASH" label (dims once broken) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USQInteractCue> Cue;

	/** The silver urn's banked gold goes to the smasher's local profile (only the machine that owns To saves them). */
	UFUNCTION(NetMulticast, Reliable) void MulticastSoulCoins(APlayerState* To, int32 Coins);

protected:
	UFUNCTION() void OnRep_Def();
	UFUNCTION() void OnRep_Smashed();
	const FSQShrineDef* GetDef() const;
	void ApplyDefVisuals();
	bool bVisualsBuilt = false;
	TWeakObjectPtr<ASQCharacter> PendingBy;
	float PendingSmashTime = -1.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PotMID;
};
