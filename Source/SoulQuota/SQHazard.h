// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQHazard.generated.h"

class ASQArena;
class ASQCharacter;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Biomes.csv Hazards column entries */
enum class ESQHazardKind : uint8 { None, Gloom, Graves, Fog, Gusts, Bells };

/**
 * Per-biome navigation hazards (Biomes.csv Hazards / HazardCounts, numbers in Tuning.csv Hazard*). Top-down pass
 * (Nov 9 2026): every hazard is a shape on the ground you can read from above, with a visible warning.
 *   Gloom  - "Gloom rings": a few dark rings roam the map. Always outlined in violet; on a cycle they fill in from the
 *            centre (the warning) and then go dark: inside a dark ring health drains (no soul drop). The lantern's ring is safe.
 *   Graves - open graves: reapers inside wade at HazardGravePlayerSpeed (dash out); enemies get stuck too.
 *   Fog    - drifting fog banks: a low cloud layer, outlined on the ground, that hides whatever is under it.
 *   Gusts  - vents that erupt on a cycle and hop reapers up (harmless; vents at a tier foot aim for its top).
 *   Bells  - a bell tolls on a cycle and sends a glowing ground ring across part of the map: dash through it
 *            (dash i-frames) or take a hit. Its reach is outlined while it winds up.
 * Spawned locally on every machine by ASQArena (not replicated): the layout comes from the biome seed and the timing
 * from the server clock, exactly like the biome gimmicks, so every machine agrees. Damage and launches are server
 * work (launches also on the owning client, like the old jump pads, so the lift is instant); the rest is local visuals.
 */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQHazardField : public AActor
{
	GENERATED_BODY()

public:
	ASQHazardField();
	virtual void Tick(float DeltaSeconds) override;

	static ESQHazardKind ParseKind(const FString& KindName);

	/** Lay out the hazards. Kinds / counts are the Biomes.csv cells ("Gloom|Graves", "4|6"). Call after the arena's props and walls. */
	void Setup(ASQArena* InArena, const FString& HazardCell, const FString& CountCell, int32 Seed, const FLinearColor& InFogColor, const FLinearColor& InLampColor);

	/** Walking speed multiplier at P (graves). Server + owning client (prediction agrees). */
	float PlayerSpeedMult(const FVector2D& P) const;
	/** Horde speed multiplier at P (graves hold enemies too). */
	float EnemySpeedMult(const FVector2D& P) const;
	/** One status line for the HUD (via ASQArena::GimmickStatus), from the local reaper's point of view. Empty = nothing to say. */
	FString StatusText() const;
	/** True when a disc of radius R at (X, Y) overlaps a fixed hazard footprint (graves, vents, bells) - keeps the lantern and shrines off them. */
	bool IsSpotBlocked(float X, float Y, float R) const;
	bool HasKind(ESQHazardKind Kind) const { return Kinds.Contains(Kind); }
	/** Hazards run during the night and the boss fight only (same rule as the biome gimmicks). */
	bool IsHazardActive() const;
	/** True when P is inside a gloom ring that is dark right now (and not inside the lantern's ring). */
	bool IsInDarkGloom(const FVector2D& P) const;

protected:
	float ClockNow() const;
	/** 1 + HazardDamagePerNight x night index */
	float NightDamageMult() const;
	ASQCharacter* LocalReaper() const;

	// --- Gloom ---
	/** True while the rings are dark. bWarning = they are filling in; SecondsLeft = to the next change; Fill = 0..1 warning progress. */
	bool GloomState(bool& bWarning, float& SecondsLeft, float& Fill) const;
	FVector2D GloomPos(int32 Index) const;
	// --- Fog ---
	FVector2D FogPos(int32 Index) const;
	/** 0..1 how deep P is inside the nearest fog bank */
	float FogDepthAt(const FVector2D& P) const;
	// --- Gusts ---
	/** True while vent Index blows; bWarning = it is about to. */
	bool GustState(int32 Index, bool& bWarning) const;
	// --- Bells ---
	/** Current ring radius of bell Index (< 0 = no ring). bWarning = about to toll. OutCycle = toll number (hit once per toll). */
	float BellRing(int32 Index, bool& bWarning, int32& OutCycle) const;

	bool FindSpot(FRandomStream& Rand, float Radius, FVector2D& Out) const;
	void ServerTickHazards(float DeltaSeconds);
	void TickGusts();
	void BuildVisuals();
	void TickVisuals(float DeltaSeconds);
	/** Appends a ring of flat ground segments (radius R round C, band Width) to Out, following the terrain. */
	void AddGroundRing(TArray<FTransform>& Out, const FVector2D& C, float R, float Width, float Lift, int32 Segments) const;
	UStaticMeshComponent* AddPart(FName Shape, const FVector& Loc, const FRotator& Rot, const FVector& Scale, UMaterialInstanceDynamic* InMID);
	UInstancedStaticMeshComponent* AddISM(FName Shape, UMaterialInstanceDynamic* InMID);

	UPROPERTY() TObjectPtr<ASQArena> ArenaRef;
	UPROPERTY() TObjectPtr<USceneComponent> FieldRoot;
	TArray<ESQHazardKind> Kinds;
	float ArenaHalfSize = 5600.f;
	FLinearColor FogTint = FLinearColor(0.1f, 0.1f, 0.15f);
	FLinearColor LampTint = FLinearColor(1.f, 0.6f, 0.2f);

	// Gloom
	int32 NumGloom = 0;
	float GloomPeriod = 40.f, GloomWarn = 4.f, GloomDark = 10.f, GloomDps = 6.f, GloomRadius = 900.f, GloomSpeed = 0.045f, GloomOffset = 0.f;
	float GloomTickTimer = 0.f;
	// Graves
	TArray<FVector2D> GravePos;
	float GraveRadius = 160.f, GravePlayerMult = 0.35f, GraveEnemyMult = 0.45f;
	// Fog
	TArray<FVector2D> FogAnchor;
	float FogRadius = 1100.f, FogDrift = 1200.f;
	// Gusts
	TArray<FVector2D> VentPos;
	TArray<FVector> VentTarget;      // landing spot on a tier top (only when VentHasTarget)
	TArray<bool> VentHasTarget;
	TArray<float> VentPhase;
	float GustPeriod = 7.f, GustWarn = 1.2f, GustActive = 0.8f, GustRadius = 200.f, GustLaunch = 950.f;
	TMap<TWeakObjectPtr<AActor>, float> GustCooldownUntil;
	// Bells
	TArray<FVector2D> BellPos;
	TArray<float> BellPhase;
	float BellPeriod = 22.f, BellWarn = 2.5f, BellSpeed = 700.f, BellMaxRadius = 3200.f, BellBand = 160.f, BellDamage = 18.f, BellClear = 50.f;
	TArray<TMap<TWeakObjectPtr<AActor>, int32>> BellHitCycle;
	float DamagePerNight = 0.3f;

	// --- Visuals (local, not on a dedicated server) ---
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> PartISMs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GloomOrbs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> VentCaps;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> VentMIDs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BellBodies;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> BellMIDs;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GloomDiscISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GloomEdgeISM;
	/** Map remaster: the outline is violet while calm and turns warning red-orange while filling in / dark */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GloomEdgeMID;
	uint8 GloomEdgeTone = 255;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FogISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FogEdgeISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RingISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BellReachISM;
	TArray<FTransform> ScratchGloomDisc;
	TArray<FTransform> ScratchGloomEdge;
	TArray<FTransform> ScratchFog;
	TArray<FTransform> ScratchFogEdge;
	TArray<FTransform> ScratchRing;
	TArray<FTransform> ScratchReach;
	TArray<int32> BellCycleSeen;
	bool bGloomShown = false;
	bool bRingShown = false;
	bool bReachShown = false;
	float GloomRefreshTimer = 0.f;
	float FogRefreshTimer = 0.f;
	float VisualClock = 0.f;
	float FxTimer = 0.f;
	float LocalFxTimer = 0.f;
};
