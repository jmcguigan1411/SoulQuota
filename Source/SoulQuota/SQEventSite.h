// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQEventSite.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USQInteractCue;
struct FSQEventDef;

/**
 * The marked spot of a Site run event (content item 12; Events.csv Kind Site): a prop from the existing prop set (primitive
 * fallback), tinted in the event's colour, inside the shared interact ring (USQInteractCue). Visual only: all the rules
 * (who is standing in the ring, the hold, the reward) run in ASQGameMode::TickEvents. The server destroys it a few seconds
 * after the event ends, so nothing is left on the floor (floor rule).
 */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQEventSite : public AActor
{
	GENERATED_BODY()

public:
	ASQEventSite();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	/** Server: call right after spawning (Events.csv Id, ring radius in cm). */
	void InitSite(FName InEventId, float InRadius);
	/** Server: the event is over (bDone = the crew completed it). Plays the finish and removes the site shortly after. */
	void Finish(bool bDone);

	UPROPERTY(ReplicatedUsing = OnRep_Event) FName EventId;
	UPROPERTY(Replicated) float Radius = 260.f;
	/** 0 = waiting for reapers, 1 = completed, 2 = expired */
	UPROPERTY(ReplicatedUsing = OnRep_State) uint8 State = 0;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SiteRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Cap;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USQInteractCue> Cue;

protected:
	UFUNCTION() void OnRep_Event();
	UFUNCTION() void OnRep_State();
	void BuildVisuals();
	void ShowState();
	bool bVisualsBuilt = false;
	uint8 ShownState = 0;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SiteMID;
};
