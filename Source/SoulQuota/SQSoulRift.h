// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQSoulRift.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/**
 * Nov 25 2026 run structure (Docs/RunStructure.md). Two looks on one replicated actor:
 *   Mode 0 RIFT: the slain boss's soul torn out as a giant spinning teal wisp; reapers walk into it to leave the map.
 *   Mode 1 PASSAGE: the Soul Passage, a dark tunnel of rising souls high above the next map; an invisible floor holds the crew
 *   while ring after ring of soul light streams past (it reads as falling). Purely cosmetic apart from the floor.
 * The game mode owns the logic (who is inside, timers); this actor only draws it.
 */
UCLASS()
class SOULQUOTA_API ASQSoulRift : public AActor
{
	GENERATED_BODY()
public:
	ASQSoulRift();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: spawn a rift (Mode 0) or a passage (Mode 1) */
	static ASQSoulRift* SpawnRift(UWorld* World, const FVector& Location, uint8 InMode, float InRadius);

	UPROPERTY(ReplicatedUsing = OnRep_Look) uint8 Mode = 0;
	/** Rift: walk-in radius (cm). Passage: floor radius. */
	UPROPERTY(ReplicatedUsing = OnRep_Look) float Radius = 450.f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Core;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Floor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Rings;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Wisps;

protected:
	UFUNCTION() void OnRep_Look();
	void ApplyLook();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GlowMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DarkMat;
	float Time = 0.f;
	float FxTimer = 0.f;
	uint8 LookMode = 255;
};
