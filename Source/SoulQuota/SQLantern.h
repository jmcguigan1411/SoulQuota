// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQLantern.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UInstancedStaticMeshComponent;

/** Death's soul lantern: stand inside the ring to bank carried souls. Moves every night. */
UCLASS()
class SOULQUOTA_API ASQLantern : public AActor
{
	GENERATED_BODY()

public:
	ASQLantern();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(ReplicatedUsing = OnRep_Radius) float Radius = 350.f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Post;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Flame;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ring;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	/** Imported lantern_standing (hidden until the art loads; Post stays as the blocker). */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LanternMesh;
	/** Optional candle ring around the banking zone (art only). */
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Candles;
	// ---- Lantern trail (Nov 12 2026): this actor is the LIT lantern; it also draws the others and the way to itself
	/** Tall light beam over the lit lantern (seen from across the map) */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Beam;
	/** The other sites: dark lanterns still to light / spent ones (world space, rebuilt when the trail moves) */
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> DormantPosts;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SpentPosts;
	/** Glowing footlights from the lantern just banked to the lit one (along the lanes through the hub) */
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Footlights;

protected:
	UFUNCTION() void OnRep_Radius();
	float Time = 0.f;
	float FlameScale = 0.7f;
	float CandleArtScale = 1.f;
	float CandleZ = 0.f;
	/** Server: last spot / arena build that was checked (the game mode moves the lantern every night). */
	FVector2D ValidatedXY = FVector2D(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	int32 ValidatedSerial = -1;
	/** Server: keeps the lantern on a flat spot clear of props, lamps, jump pads and ice. */
	void ServerValidateSpot();
	/** Every machine: rebuilds the dormant / spent posts and the footlights when the trail (or the map) changes */
	void RefreshTrailLook(const class ASQGameState* GS);
	UInstancedStaticMeshComponent* MakeWorldISM(class UStaticMesh* InMesh, class UMaterialInterface* Mat);
	int32 LookStage = -2;
	int32 LookSites = -1;
	int32 LookMissed = -1;
	int32 LookSerial = -1;
	FVector LookAt = FVector(TNumericLimits<float>::Max());
	float PostArtScale = 1.f;
	float PostArtZ = 0.f;
	float BeamHeight = 5000.f;
	float BeamWidth = 0.9f;
	/** Nov 25 2026: the trail moved on; the lantern's flame flies from FlightFrom to the new site once this machine sees it move */
	bool bFlightPending = false;
	/** Nov 25 2026: overflow look (the boss climbing out) */
	float OverflowFx = 0.f;
	bool bWasOverflowing = false;
	float LanternBaseIntensity = 30000.f;
	FVector FlightFrom = FVector::ZeroVector;
	void LaunchFlight(const FVector& From, const FVector& To);
};
