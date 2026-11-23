// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQPortal.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ASQCharacter;
class USQInteractCue;

/**
 * The Appointment Bell (boss summon). Nov 9 2026 missions: it rises at the map's boss spot (ASQArena::GetObjectiveSpots "Boss") once every
 * objective of the stage is resolved, or when the stage clock runs out (the Final Swarm). Press interact inside its circle and stay there
 * PortalRingSeconds: the stage clock stops and the mission boss rises beside it (failed objectives = Defiance stacks). Beat it = victory.
 */
UCLASS()
class SOULQUOTA_API ASQPortal : public AActor
{
	GENERATED_BODY()

public:
	ASQPortal();

	static ASQPortal* SpawnPortal(UWorld* World, const FVector& Location);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Can be rung now (ASQGameMode: every objective resolved, or overtime) */
	UPROPERTY(Replicated) bool bArmed = false;
	/** Rung: the boss is up (or beaten) */
	UPROPERTY(Replicated) bool bRung = false;
	/** 0..1 while someone holds the ring */
	UPROPERTY(Replicated) float RingProgress = 0.f;
	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> Ringer;

	/** Server: By pressed E in range. False when it isn't armed / already rung. */
	bool StartRing(ASQCharacter* By);
	/** Use radius (Tuning PortalUseRange) */
	float GetUseRange() const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Plinth;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PostLeft;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PostRight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Bell;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Circle;
	/** Shared interactable look (crimson ring + diamond above the frame + label); urgent once armed, dim once rung */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USQInteractCue> Cue;

protected:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BellMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CircleMID;
	float LookTime = 0.f;
	float Swing = 0.f;
	/** Label state last shown (0 waiting, 1 armed, 100+pct ringing): the label string is only rebuilt on a change */
	int32 CueLabelKey = -1;
};
