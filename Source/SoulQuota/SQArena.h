// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQTypes.h"
#include "SQArena.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class USQData;
class ASQHazardField;

/**
 * A low flat-topped terrace with smoothstep ramps all round (top-down maps, Nov 9 2026: at most MapTierMaxHeight,
 * ~2.5 m, ramps no steeper than MapTierSlope). The name is kept from the old plateau era (quests and chests use it).
 */
struct FSQTerrainPlateau
{
	FVector2D Centre = FVector2D::ZeroVector;
	float Radius = 1000.f;      // flat top radius (before wobble)
	float Height = 180.f;       // top height (cm)
	float Ramp = 900.f;         // horizontal width of the ramp side
	float Wobble = 0.1f;        // radius wobble fraction
	float WobblePhase = 0.f;
	int32 Lobes = 3;

	/** Top radius in the direction of Angle (radians). */
	float RadiusAt(float Angle) const { return Radius * (1.f + Wobble * FMath::Sin(Lobes * Angle + WobblePhase)); }
	/** Outermost reach of the ramp foot. */
	float FootRadius() const { return Radius * (1.f + Wobble) + Ramp; }
};

/**
 * Map revamp (Nov 10 2026): a skate-district terrain feature baked into the height grid, so it is ordinary walkable
 * ground for reapers, bots and the horde (GroundZ follows it). Original shapes; the district plan borrows only the
 * principles of big open skate maps (districts linked by lanes, continuous lines, something to ride everywhere).
 * Heights stay under MapTierMaxHeight so the top-down camera never loses anyone behind terrain.
 */
enum class ESQSkateKind : uint8 { Bowl, Bank, Rollers, Quarter, Spine, Kicker };

struct FSQSkateFeature
{
	FVector2D Centre = FVector2D::ZeroVector;
	FVector2D Dir = FVector2D(1.0, 0.0); // ride direction (unit): ramps rise toward +Dir
	float Length = 900.f;                // along Dir (Bowl: diameter)
	float Width = 700.f;                 // across Dir
	float Height = 150.f;                // rise (Bowl: depth)
	ESQSkateKind Kind = ESQSkateKind::Bank;
	int32 District = INDEX_NONE;
	/** Furthest a non-zero height reaches from Centre (cm). */
	float Reach() const { return Kind == ESQSkateKind::Bowl ? Length * 0.5f + 60.f : 0.5f * FMath::Sqrt(Length * Length + Width * Width) + 420.f; }
	/** Height added at a point (cm; negative inside a bowl). */
	float HeightAt(float X, float Y) const;
};

/** A district (map revamp): a landmark or the boss arena plus the skate ground round it, with its own name and colour. */
struct FSQDistrict
{
	FVector2D Centre = FVector2D::ZeroVector;
	float Radius = 2500.f;       // reach of the district (skate ground), larger than its zone
	int32 Zone = INDEX_NONE;     // MapZones index
	FString Name;
	FLinearColor Color = FLinearColor::White;
	ESQSkateKind Theme = ESQSkateKind::Bank;
};

/** A walking lane from the hub to a district (kept open: no walls, no features, flat, painted, chevrons point outward). */
struct FSQLane
{
	TArray<FVector2D> Pts;
	int32 District = INDEX_NONE;
};

/** A bone skate-park piece (/Game/Art/Props/SM_Park_<Name>) placed in a district: real collision, rideable ramps. */
struct FSQParkSpot
{
	FVector2D At = FVector2D::ZeroVector;
	float Yaw = 0.f;
	int32 Piece = 0;
	float FootR = 300.f;
	int32 District = INDEX_NONE;
};

/** Per-biome look (Biomes.csv colour columns; sRGB hex -> linear). Missing columns fall back to derived colours. */
struct FSQArenaPalette
{
	FLinearColor Grass = FLinearColor(0.08f, 0.19f, 0.04f);
	FLinearColor GrassAlt = FLinearColor(0.05f, 0.19f, 0.12f);
	FLinearColor Path = FLinearColor(0.19f, 0.11f, 0.06f);
	FLinearColor Cliff = FLinearColor(0.16f, 0.15f, 0.17f);
	FLinearColor Top = FLinearColor(0.21f, 0.36f, 0.10f);
	FLinearColor Basin = FLinearColor(0.07f, 0.05f, 0.15f);
	FLinearColor Wall = FLinearColor(0.11f, 0.10f, 0.13f);
	FLinearColor Prop = FLinearColor(0.32f, 0.31f, 0.38f);
	FLinearColor Fog = FLinearColor(0.02f, 0.02f, 0.06f);
	FLinearColor Moon = FLinearColor(0.40f, 0.55f, 1.f);
	FLinearColor Sky = FLinearColor(0.11f, 0.20f, 0.39f);
	FLinearColor Lamp = FLinearColor(1.f, 0.45f, 0.10f);
	FLinearColor Backdrop = FLinearColor(0.007f, 0.008f, 0.02f);
	// Style pass: sky-dome gradient (Biomes.csv SkyHorizonColor / SkyMidColor / SkyZenithColor)
	FLinearColor SkyHorizon = FLinearColor(0.42f, 0.10f, 0.36f);
	FLinearColor SkyMid = FLinearColor(0.13f, 0.06f, 0.32f);
	FLinearColor SkyZenith = FLinearColor(0.02f, 0.02f, 0.10f);
	// Top-down maps (Biomes.csv MapRoadColor / MapCapColor / MapWaterColor)
	FLinearColor Road = FLinearColor(0.26f, 0.17f, 0.09f);
	FLinearColor Cap = FLinearColor(0.60f, 0.58f, 0.52f);
	FLinearColor Water = FLinearColor(0.02f, 0.10f, 0.18f);
	TArray<FName> RimProps;      // stone props along the edge band
	TArray<FName> LampProps;     // lamp post meshes (first one that exists wins)
	TArray<FName> BackdropProps; // silhouettes outside the walls
	TArray<FName> TallProps;     // Biomes.csv MapTallProps: tall landmark dressing (crypts, arches) - always SQFadeable
};

/** Layout obstacle kinds (top-down maps). Everything except Water is drawn; all of them block reapers, the boss and the horde. */
enum class ESQWallKind : uint8 { Stone, GraveRow, Fence, Crates, Water, Crypt, Tent, Stall, Pillar, Gear };

/**
 * One layout obstacle: a straight run A -> B of half width HalfThick (a round obstacle of radius HalfThick when A == B).
 * The horde treats it as a capsule (ASQArena::ResolveWallOverlap); reapers and the boss hit real collision.
 */
struct FSQMapWall
{
	FVector2D A = FVector2D::ZeroVector;
	FVector2D B = FVector2D::ZeroVector;
	float HalfThick = 40.f;
	float Height = 130.f;
	ESQWallKind Kind = ESQWallKind::Stone;
	bool IsRound() const { return FVector2D::DistSquared(A, B) < 1.0; }
};

/** Mission zones of a map. */
enum class ESQZoneKind : uint8 { Hub, Landmark, Boss };

struct FSQMapZone
{
	FVector2D Centre = FVector2D::ZeroVector;
	float Radius = 1000.f;
	float Angle = 0.f;          // polar angle from the arena centre (radians)
	ESQZoneKind Kind = ESQZoneKind::Landmark;
	bool bRaised = false;       // sits on a tier (Clocktower gear platforms)
};

/**
 * The biome arena, built from code at the size in Tuning.csv and the look / layout in Biomes.csv.
 * Top-down remake (Nov 9 2026): a ~112 m square (map revamp Nov 10: ~224 m, Tuning ArenaHalfSize, with skate districts) of mostly flat ground with low tiers, a central hub with an open
 * kiting ring, landmark zones and a boss arena on a ring, an escort road, spawner pads near the edges and per-biome
 * walls (Biomes.csv MapLayout) that block reapers AND the horde.
 * Biome gimmicks are deterministic functions of server time, so every machine agrees on
 * where the tide / spotlights / ice are without sending any extra network data.
 */
UCLASS()
class SOULQUOTA_API ASQArena : public AActor
{
	GENERATED_BODY()

public:
	/** Height of the walkable ground (world Z, cm) at a point. Deterministic from the biome seed, identical on
	 *  every machine. 0 when the arena is not built. The horde, pickups and projectiles use this for their Z. */
	float GroundZ(float X, float Y) const;

	/** Convenience: finds the arena through the game state. Returns 0 when there is none. */
	static float GroundZAt(const UObject* WorldContext, float X, float Y);

	/** Highest / lowest ground point (cm). 0 before the terrain is built. */
	float MaxGroundZ() const { return TerrainMaxZ; }
	float MinGroundZ() const { return TerrainMinZ; }
	/** True once the height grid exists. */
	bool HasTerrain() const { return GridN > 0; }
	/** Up-facing ground normal at a point (UpVector before the terrain is built). */
	FVector GroundNormal(float X, float Y) const;
	/** Ground slope in degrees at a point (0 = flat). */
	float GroundSlopeDeg(float X, float Y) const;
	/** Highest minus lowest ground over a disc (centre + 8 samples at Radius + 8 at Radius/2). Small = flat enough for the lantern. */
	float GroundRoughness(float X, float Y, float Radius) const;
	/** Jump pads are gone (top-down maps have no jumping): always huge. Kept for old callers. */
	float DistToNearestJumpPad(float X, float Y) const;
	/** The low tiers (terraces). */
	const TArray<FSQTerrainPlateau>& GetPlateaus() const { return Plateaus; }

	/** Half the arena side (cm). The invisible walls' inner faces sit at +/- this. */
	float GetArenaHalf() const { return Half; }
	/** Radius of the flat central hub (player starts, first lantern spot). */
	float GetHubRadius() const { return HubRadius; }
	/** Increments every time the arena rebuilds (lets other actors re-validate their spots). */
	int32 GetBuildSerial() const { return BuildSerial; }
	/** True when a lantern ring of Radius at (X, Y) is flat, inside the walls, clear of walls / blocking props / lamps,
	 *  the escort road, spawner pads, ice patches, hazards and shrines. */
	bool IsLanternSpotClear(float X, float Y, float Radius) const;
	/** Nearest clear lantern spot to (X, Y) (searches outwards; falls back to the hub centre). Z = GroundZ. */
	FVector FindLanternSpot(float X, float Y, float Radius) const;
	/** Every blocking prop / lamp / layout wall component (e.g. so the boss can be told to walk through them). */
	void GetPropBlockers(TArray<UPrimitiveComponent*>& Out) const;
	/** True when a footprint of radius R at (X, Y) keeps a capsule-wide gap to every blocking prop and layout wall. */
	bool IsPropFootprintFree(float X, float Y, float R) const;
	/** Lamp post spots on the ground (Z = GroundZ). Same on every machine. */
	const TArray<FVector>& GetLampSpots() const { return LampSpots; }
	/** This biome's navigation hazards (Biomes.csv Hazards), built locally on every machine. May be null. */
	ASQHazardField* GetHazards() const;

	/** 0..1 how much a point is on the escort road. */
	float PathMask(float X, float Y) const;

	// --- Mission layout (top-down maps, Nov 9 2026) ---
	/**
	 * Count spots for a mission objective, deterministic per map seed, on valid ground and clear of walls / blockers.
	 * ObjectiveType: DestroySpawners (pads near the edges, spread out), DefendPoint (landmark centres), Escort (road start,
	 * evenly along the road, road end), CollectItems (scattered, spread out), BankSouls (hub first, then landmarks),
	 * SurviveWave (the most open arenas: hub, then landmarks), KillElite (landmarks in a different order, then open
	 * mid-ring spots), Boss (boss arena centre, then points round its rim). Z = GroundZ.
	 * Returns false (with as many spots as it found in Out) when it can't find Count, or the type is unknown.
	 */
	bool GetObjectiveSpots(FName ObjectiveType, int32 Count, TArray<FVector>& Out) const;
	/** The escort road the cart follows, start to end, a point every ~250 cm (Z = GroundZ). False before the arena is built. */
	bool GetEscortPath(TArray<FVector>& OutPoints) const;
	/** Player start Index (0-3 on an inner ring round the hub centre, then an outer ring), clear of walls. Z = GroundZ. */
	FVector GetPlayerStart(int32 Index) const;
	/** Mission zones (hub, landmarks, boss arena). */
	const TArray<FSQMapZone>& GetZones() const { return MapZones; }
	/** Layout walls (they block the horde too). */
	const TArray<FSQMapWall>& GetWalls() const { return MapWalls; }
	/** Biomes.csv MapLayout of the built map (GraveRows / Docks / Tents / Cloisters / Gears). */
	FName GetLayoutName() const { return LayoutName; }
	// --- Map revamp (Nov 10 2026): districts, lanes, skate features (minimap + HUD read these) ---
	const TArray<FSQDistrict>& GetDistricts() const { return Districts; }
	const TArray<FSQLane>& GetLanes() const { return Lanes; }
	const TArray<FSQSkateFeature>& GetSkateFeatures() const { return SkateFeatures; }
	const TArray<FSQParkSpot>& GetParkSpots() const { return ParkSpots; }
	const TArray<FVector2D>& GetRoadPoints() const { return RoadPts; }
	const TArray<FVector2D>& GetSpawnerPads() const { return SpawnerSpots; }
	/** District index whose ground contains (X, Y) (nearest centre within its radius), or INDEX_NONE (hub / open ground). */
	int32 FindDistrictAt(float X, float Y) const;
	/** Run events (content item 12): a clear, reachable-ground spot for an event site (the shrine placement rules). False = none found. */
	bool FindEventSpot(FRandomStream& Rand, float Radius, FVector& Out) const { return FindInteractSpot(Rand, Radius, false, false, Out); }
	/**
	 * Lantern trail (Nov 12 2026): the run's lantern sites in the order they light. [0] is the hub, then one site per district
	 * (never the boss arena), each a real journey from the one before, ending in the district nearest the boss arena.
	 * Seeded, so every machine and every retry of the same seed agrees. OutNames = the place to print ("Bellstone Banks").
	 * Returns how many sites it found (at most Count, at least 1 on a built map).
	 */
	int32 GetLanternTrail(int32 Count, int32 Seed, TArray<FVector>& OutSites, TArray<FString>& OutNames, TArray<int32>* OutDistricts = nullptr) const;
	/** Water centre-line segments (Docks): A, B, half width. */
	int32 NumWaterSegments() const { return WaterA.Num(); }
	void GetWaterSegment(int32 Index, FVector2D& OutA, FVector2D& OutB, float& OutHalf) const { OutA = WaterA[Index]; OutB = WaterB[Index]; OutHalf = WaterHalf[Index]; }
	/** True when (X, Y) is inside a water band (not a dry bowl). */
	bool IsWaterAt(float X, float Y) const;
	/** True when a disc of Radius at (X, Y) overlaps a layout wall or a blocking prop / lamp footprint (2D). */
	bool IsWallAt(float X, float Y, float Radius) const;
	/** True when a disc touches a mission area that must stay open: hub, landmark cores, boss arena, escort road, spawner pads. */
	bool IsReservedSpot(float X, float Y, float Radius) const;

	// --- Horde blockers (ASQHordeManager; 2D, every machine agrees) ---
	/** Pushes a body of Radius out of every layout wall and blocking prop / lamp footprint. True when it moved. */
	bool ResolveWallOverlap(FVector2D& InOutPos, float Radius) const;
	/** Bends a unit move direction round the wall ahead (probe Look cm ahead). Returns Dir unchanged when the way is clear. */
	FVector2D SteerAroundWalls(const FVector2D& Pos, const FVector2D& Dir, float Radius, float Look) const;

	// --- Camera occlusion (controls agent) ---
	/** Every component tagged "SQFadeable" (taller than MapFadeHeight, each with its own material, safe to hide or swap). */
	void GetFadeables(TArray<UPrimitiveComponent*>& Out) const;

	// --- Old skate-park queries (park removed for top-down; kept so old callers compile) ---
	/** Always false (no park lines). */
	bool IsOnParkLine(float X, float Y, float Radius) const;
	/** Adds nothing (no park pieces). */
	void GetParkColliders(TArray<UPrimitiveComponent*>& Out) const;

	/** Tintable surface: /Game/Materials/M_Ground (vertex colour * Color * subtle world noise, Glow -> emissive).
	 *  Falls back to the clay ColorMID when the material has not been created yet (Scripts/create_ground_material.py). */
	static UMaterialInstanceDynamic* SurfaceMID(UObject* Outer, const FLinearColor& Color, float Glow = 0.f);
	/** Uniform scale that makes a mesh Height cm tall. Works for tiny meshes too (KayKit props imported in metres are ~1 unit tall). */
	static float FitMeshHeight(const UStaticMesh* InMesh, float Height);

	ASQArena();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: switch biome and roll a new random layout (rebuilds on every machine via OnRep).
	 *  InRunSeed < 0 rolls a fresh seed (Tuning RandomMaps 1), 0 = the biome's fixed hand-checked layout. */
	void SetBiome(FName NewBiome, int32 InRunSeed = -1);

	UPROPERTY(ReplicatedUsing = OnRep_Biome) FName BiomeId = TEXT("Graveyard");
	/** Per-run layout seed (Nov 9 2026: a random map every run). Replicated with the biome so every machine builds the same map. */
	UPROPERTY(ReplicatedUsing = OnRep_Biome) int32 RunSeed = 0;
	/** Server: SetBiome has rolled this level's seed (the lobby rolls once per level load, even when the biome is unchanged) */
	bool HasRolledRunSeed() const { return bRunSeedRolled; }
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;

	// --- Map finish (Nov 24 2026) ---
	/** Server: the Trapped Spirit's companion wisps orbit every reaper for Seconds (replicated end time; drawn on every machine). */
	void StartCompanion(float Seconds);
	UPROPERTY(Replicated) float CompanionEndTime = 0.f;
	/** The raised walkway (one per map): deck ends A -> B at DeckZ (false when none fitted this seed). */
	bool GetWalkway(FVector2D& OutA, FVector2D& OutB, float& OutDeckZ) const { OutA = WalkA; OutB = WalkB; OutDeckZ = WalkDeckZ; return bWalkway; }

	// --- Gimmick queries (valid on server and clients) ---
	ESQGimmick GetGimmick() const;
	bool IsGimmickActive() const;
	float PlayerSpeedMult(const FVector2D& P) const;
	bool IsOnIce(const FVector2D& P) const;
	float EnemySpeedMult(const FVector2D& P) const;
	float EnemyDamagePerSecond(const FVector2D& P) const;
	float SoulMult(const FVector2D& P) const;
	bool IsTimeStopped() const;
	FString GimmickStatus() const;
	// --- Map twists (Nov 12 2026 three-map remaster, Docs/Maps.md) ---
	/** Drowned Harbour: the water level now (cm). Ground below it is flooded. Far below the map when there is no tide. */
	float TideLevel() const;
	/** Drowned Harbour: is this spot under the tide right now? */
	bool IsFlooded(const FVector2D& P) const;
	/** Clocktower: number of clock hands sweeping the map (0 on other maps) and each hand's angle (radians) / reach (cm). */
	int32 ClockHandCount() const;
	float ClockHandAngle(int32 Hand) const;
	float ClockHandReach(int32 Hand) const;
	/** Clocktower: is this spot under a hand right now? (jump it: the caller checks height over the ground) */
	bool IsUnderClockHand(const FVector2D& P, float Pad = 0.f) const;
	/** The map's large-scale ground shape (terraces / causeways / the climb) at a point, before skate features. */
	float MacroHeight(float X, float Y) const;
	FName GetMapShape() const { return MapShape; }
	/** Landmark set pieces, gates, walls, trees and lamp posts placed this build: X, Y, footprint radius (minimap). */
	const TArray<FVector>& GetSetMarks() const { return SetMarks; }

protected:
	UFUNCTION() void OnRep_Biome();
	void Rebuild();
	/** 2D mission layout from the seed: hub, landmarks, boss arena, escort road, spawner pads, tiers, water, wall plan. */
	void PlanLayout(const USQData* D, int32 LayoutSeed);
	/** Random-map guard: every open cell that matters (starts, hub, zones, pads, objectives) is reachable on foot from the start. */
	bool ValidateLayout(FString& OutWhy) const;
	void ResetLayoutState();
	/** Personality walls (Biomes.csv MapLayout), clipped against the open mission areas. */
	void PlanWalls(FRandomStream& WallRand);
	/** Adds a straight run A -> B, split where it would cross an open mission area / tier ramp / the edge band. */
	void AddWallRun(const FVector2D& A, const FVector2D& B, float HalfThick, float Height, ESQWallKind Kind);
	/** Adds a round obstacle if its footprint is clear of the open mission areas and the other walls. */
	bool AddWallRound(const FVector2D& C, float Radius, float Height, ESQWallKind Kind, bool bAttached = false);
	/** An arc of wall runs round Centre (angles in radians, a gap of GapDeg every SpanDeg). */
	void AddWallArc(const FVector2D& Centre, float Radius, float FromAng, float ToAng, float SpanDeg, float GapDeg, float HalfThick, float Height, ESQWallKind Kind);
	/** True when a wall footprint point would sit in an area that must stay open (bWater: water may touch the arena edge). */
	bool IsWallBlockedSpot(float X, float Y, float Clear, bool bWater) const;
	/** 2D distance to the escort road centre line (huge when there is no road). */
	float DistToRoad(float X, float Y) const;
	/** Map revamp: districts round the ring zones, lanes from the hub, skate features in the district ground. */
	void PlanDistricts(const USQData* D, FRandomStream& LR);
	/** Map revamp: banks, bowls, rollers, quarters, spines and kickers in every district (before the walls). */
	void PlanSkateFeatures(const USQData* D, FRandomStream& LR);
	/** Map revamp: bone park pieces in the districts (after the walls, so they keep clear of them). */
	void PlanParkSpots(const USQData* D, FRandomStream& LR);
	/** Room a feature / piece at C of footprint R would have (negative = overlaps something that must stay open). */
	float SkateRoom(const FVector2D& C, float R) const;
	/** 2D distance to the nearest hub lane centre line (huge when there are none). */
	float DistToLane(float X, float Y) const;
	/** Sum of the skate features' heights at a point (cm). */
	float SkateHeight(float X, float Y) const;
	/** Map revamp visuals: lane chevrons, district name plates, beacons, park pieces (clients; park collision everywhere). */
	void BuildDistricts(const USQData* D, bool bVisuals);
	/** Deterministic height grid (flat ground, gentle roll, low tiers, water cuts, edge rise). */
	void BuildHeightGrid(const USQData* D, int32 TerrainSeed);
	/** Procedural mesh (render + collision) from the height grid, painted for top-down readability. */
	void BuildTerrainMesh(const FSQArenaPalette& Pal, int32 TerrainSeed);
	/** Collision + visuals of the planned walls (water planes, rows, tents...). */
	void BuildLayoutWalls(const FSQArenaPalette& Pal, float PropScale, bool bArt);
	/** Lamp spots beside the escort road and at the landmarks (deterministic). */
	void PlanLamps(const USQData* D);
	/** Low stone wall + pillars/fence that follow the ground along the edge, plus the ground skirt outside (visual only). */
	void BuildWalls(const FSQArenaPalette& Pal, float PropScale, bool bArt);
	/** Lamps, tier rim stones, grass tufts (lamp blockers on every machine; the rest is visual). */
	void BuildDressing(const USQData* D, const FSQArenaPalette& Pal, int32 Seed, float PropScale, bool bArt);
	/** Silhouettes outside the walls (visual only; one fadeable ISM set per side). */
	/** Map remaster (Nov 10 2026): flagstone plazas on the hub / landmark / boss floors, kerb stones along the road and
	 *  glowing marker posts round each landmark. Visual only (clients), ISMs, deterministic from Seed. */
	void BuildFloorDetail(const USQData* D, const FSQArenaPalette& Pal, int32 Seed);
	void BuildBackdrop(const USQData* D, const FSQArenaPalette& Pal, int32 Seed, float PropScale);
	/** Runtime moon / sky / fog / post-process for the biome, tuned for the top-down camera (not on dedicated servers). */
	void BuildSky(const USQData* D, const FSQArenaPalette& Pal, int32 Seed, float PropScale);
	void ApplyAtmosphere(const USQData* D, const FSQArenaPalette& Pal);
	/** Conforming ice discs drawn on the terrain (Ice biome, not on dedicated servers). */
	void BuildIceOverlay();
	/** Objective spot pools (after the props, lamps and hazards exist). */
	void BuildObjectivePools();
	/** Horde blocker buckets from MapWalls + BlockerSpots. */
	void BuildHordeGrid();
	/** Objective rule: on flat ground, clear of walls / blockers / hazards / ice, inside the walls. */
	bool IsObjectiveSpotClear(float X, float Y, float Radius) const;
	/** Nearest spot to (X, Y) that passes IsObjectiveSpotClear (spiral search, at most MaxDist away). */
	bool NudgeToClear(float X, float Y, float Radius, float MaxDist, FVector2D& Out) const;
	float GridHeight(int32 IX, int32 IY) const { return Heights[IY * (GridN + 1) + IX]; }
	/** Lowers grid vertices until no neighbour step exceeds MaxStep. */
	void ClampTerrainSlopes(float MaxStep);
	/** TerrainMinZ / TerrainMaxZ from the grid. */
	void RefreshTerrainRange();
	UStaticMeshComponent* AddBlock(FName Shape, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color, bool bCollide);
	/** One art mesh as its own component (own material: fadeable / swappable). */
	UStaticMeshComponent* AddArtPart(UStaticMesh* InMesh, const FTransform& Xf, UMaterialInstanceDynamic* InMID, bool bCollide);
	/** Tags a component "SQFadeable" and lists it for GetFadeables. Camera-transparent so a boom never snaps in on it. */
	void MarkFadeable(UPrimitiveComponent* Comp);
	/** One instanced component per imported mesh (tracked in ArtISMs, destroyed on rebuild). */
	UInstancedStaticMeshComponent* MakeArtISM(UStaticMesh* InMesh, UMaterialInstanceDynamic* InMID, bool bCollide, bool bShadow);
	float Now() const;
	float TideX(bool& bActive, bool& bWarning) const;
	FVector2D SpotlightPos(int32 Index) const;
	/** Server: anything outside the walls, under the terrain or below the floor goes back onto the ground. */
	void ServerSafetyNet();
	/** Server: after a rebuild, lift characters the new ground swallowed and push them out of new props / walls. */
	void ServerRePlaceCharacters();
	/** The biome gimmick's own status line (GimmickStatus adds the hazard line to it). */
	FString GimmickStatusOnly() const;

	// --- Map interactables (Shrines.csv) + hazards (Biomes.csv Hazards / HazardCounts / Secrets) ---
	/** Server: destroy the current shrines / pots and scatter a fresh set (deterministic from the biome + Salt = night). */
	void ServerSpawnInteractables(int32 Salt);
	/** Server: respawn the interactables when a night starts or the lobby opens. */
	void ServerWatchPhase();
	/** A free spot for an interactable of footprint Radius (bHigh = on a tier top; bSmall = pot rules, else lantern-spot rules). */
	bool FindInteractSpot(FRandomStream& Rand, float Radius, bool bHigh, bool bSmall, FVector& Out) const;
	UPROPERTY() TObjectPtr<ASQHazardField> Hazards;
	UPROPERTY() TArray<TObjectPtr<AActor>> Interactables;
	/** Interactable footprints: X, Y = centre, Z = radius (server) */
	TArray<FVector> InteractSpots;
	/** Shrines.csv secret Ids this biome hides (Biomes.csv Secrets) */
	TArray<FName> SecretIds;
	uint8 LastPhaseSeen = 255;
	int32 InteractSalt = -1;

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Built;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> ArtISMs;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ZoneISM;
	/** District name plates (text) built by BuildDistricts, destroyed on rebuild. */
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> DistrictParts;
	/** Map twists / set pieces (three-map remaster) */
	FName MapShape;                       // Biomes.csv MapShape: Terraces | Causeways | Climb | (blank = flat)
	float MacroTier = 240.f;              // height of one macro step (Biomes.csv MapShapeTier)
	int32 MacroSteps = 2;                 // Biomes.csv MapShapeSteps
	float MacroNoiseOff = 0.f;
	TArray<FVector> SetMarks;
	TMap<FName, TArray<FName>> SetRoles;  // Biomes.csv SetPieces: Role:Mesh|Role:Mesh...
	float MacroRaw(float X, float Y) const;
	void ReadMapShape(const class USQData* D, int32 Seed);
	int32 BuildSetPieces(const class USQData* D, bool bVisuals);
	UStaticMeshComponent* PlaceSetPiece(UStaticMesh* Mesh, float X, float Y, float YawDeg, float FootR, bool bFade, float Scale = 1.f,
		float ZOff = 0.f, UMaterialInstanceDynamic* InMID = nullptr);
	/** Map quality pass (Nov 16 2026): the district recipe. Every district is composed the same way round the line from
	 *  its gate through its plaza (landmark on a podium opposite the gate, six fronts shoulder to shoulder, a tall second
	 *  row, signature set pieces at the shoulders, stalls in the yard wall, tree line, grave plots). Planned BEFORE the
	 *  terrain is painted, so the ground art can pave an apron and shade the contact under every piece; BuildScenery then
	 *  stands the pieces up. Mirrors blender_work/mapq/recipes.py (the render the look was iterated in). */
	struct FSQScenerySite
	{
		FVector2D At = FVector2D::ZeroVector;
		float Yaw = 0.f;       // deg
		float Foot = 0.f;      // footprint radius (cm)
		float Height = 0.f;    // > 0: scale the mesh to this height (KayKit pieces); 0 = native size
		float Scale = 1.f;     // extra scale (tree variation)
		float ZOff = 0.f;
		float PierOff = 0.f;   // > 0: stands over / beside a lane on two piers this far either side (no footprint between)
		FName Mesh;
		uint8 Kind = 0;        // ESQSiteKind below
		int32 District = INDEX_NONE;
	};
	enum : uint8 { SiteSolid = 0, SiteTree = 1, SitePodium = 2, SitePlot = 3, SiteSpan = 4, SiteOnPodium = 5, SiteTreeLine = 6 };
	TArray<FSQScenerySite> ScenerySites;
	TArray<float> DistrictFrontAng;      // per district: the angle its landmark stands at (rad); -100 = none planned
	void PlanScenery(const class USQData* D);
	float DistrictGateAngle(int32 DistrictIdx) const;
	void BuildGravePlot(const FSQScenerySite& Site, float FrontYaw);
	void ApplySetGlow(UMeshComponent* Comp);
	TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> SetGlowMIDs;
	TMap<FName, UInstancedStaticMeshComponent*> SceneryISMs; // per rebuild (the components live in Built)
	/** Map art pass (Nov 15 2026): dense standing scenery from the CC0 KayKit packs (KitD_ / KitM_): house fronts round
	 *  every plaza, towers behind, cargo against the buildings, wall runs with banners, groves, a forest backdrop. */
	int32 BuildScenery(const class USQData* D);
	void ServerTickTwists(float DeltaSeconds);
	float ClockHitTimer = 0.f;
	TMap<TWeakObjectPtr<AActor>, float> ClockLastHit;
	// Twist numbers cached per build (Tuning Tide* / Clock* keys)
	float TideCycle = 60.f;
	float TideLowZ = -120.f;
	float TideHighZ = 150.f;
	float TideSlow = 0.6f;
	float TideEnemyDps = 10.f;
	int32 ClockHands = 0;
	float ClockPeriod = 50.f;
	float ClockHalfWidth = 60.f;
	float ClockDamage = 22.f;
	float ClockEnemyDps = 600.f;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TideWater;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ClockISM;
	TArray<FTransform> ScratchHands;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WarningISM;

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Terrain;
	/** Warm lamp-post lights (no shadows), at most MaxArenaLights. */
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> MapLights;
	/** Map finish pass: steady soft lights over the lanes, one per lamp pair (no shadows, no flicker). */
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> LaneLampLights;
	/** Blocking prop ISMs / components, invisible prop cylinders, lamp blockers and layout wall colliders (subset of Built / ArtISMs). */
	UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> PropBlockers;
	/** SQFadeable components (subset of Built / ArtISMs). */
	UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> Fadeables;
	/** Blocking prop / lamp footprints: X, Y = centre, Z = radius (cm). Same on every machine. */
	TArray<FVector> BlockerSpots;
	int32 BuildSerial = 0;
	float SafetyNetTimer = 0.f;
	/** Map art pass (Nov 10 2026): lamp flicker + fireflies round the local view (clients; no allocations). */
	void TickAmbience();
	// --- Map finish (Nov 24 2026): stairs + terrace faces, one raised walkway, per-map skies, event skies, companion wisps, scuttler look ---
	/** Stone faces on the terrace banks and stair treads where a lane / the road climbs one (visual; the terrain carries the walking). */
	void BuildTerraceFaces(const USQData* D, const FSQArenaPalette& Pal);
	/** Picks the walkway: a footbridge over one lane, ramps landing clear of walls (footprint goes into BlockerSpots for the horde). */
	void PlanWalkway(const USQData* D, int32 Seed);
	/** Deck, ramps and rails with real collision, piers, themed per map. */
	void BuildWalkway(const USQData* D, const FSQArenaPalette& Pal);
	/** True near the walkway's span (lane lamps and lane spans keep out from under the deck). */
	bool NearWalkwaySpan(float X, float Y, float Pad) const;
	/** Harbour sea horizon + lighthouse beam, Clocktower clock-face skyline (called from BuildSky). */
	void BuildMapSky(const USQData* D, const FSQArenaPalette& Pal, float DomeR, float EdgeZ, FRandomStream& SkyRand);
	/** Event skies (Blood Moon / Soul Storm), lighthouse beam sweep, skyline clock hands, companion wisps, the Gilt Scuttler's look. */
	void TickMapFinish(float T);
	bool bWalkway = false;
	FVector2D WalkA = FVector2D::ZeroVector;
	FVector2D WalkB = FVector2D::ZeroVector;
	float WalkDeckZ = 0.f;
	float WalkHalfWidth = 170.f;
	float WalkRampLen = 900.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SkyDomeMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MoonMID;
	FLinearColor SkyBaseHorizon = FLinearColor::Black;
	FLinearColor SkyBaseMid = FLinearColor::Black;
	FLinearColor SkyBaseZenith = FLinearColor::Black;
	FLinearColor MoonBaseTone = FLinearColor::White;
	float MoonBaseGlow = 2.2f;
	float EventSkyBlend = 0.f;
	FName EventSkyId = NAME_None;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BeamISM;
	FVector BeamOrigin = FVector::ZeroVector;
	float BeamLength = 0.f;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SkyHandISM;
	TArray<FVector> SkyClockCentres;
	TArray<float> SkyClockYaw;
	float SkyClockRadius = 0.f;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StormISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WispCoreISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WispMoteISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ScuttlerISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WispCoinISM;
	float AppliedSkyBlend = 0.f;
	/** District condition marks: a pair of glowing pennant standards at the gate of each conditioned district (built when the
	 *  replicated conditions arrive or change). */
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> CondISMs;
	uint32 CondKey = 0;
	TArray<FTransform> ScratchFinish;
	bool bWispsShown = false;
	bool bScuttlerShown = false;
	bool bStormShown = false;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MoteISM;
	TArray<FTransform> ScratchMotes;
	float LampBaseIntensity = 0.f;
	/** Reused every Tick (no per-frame allocations). */
	TArray<FTransform> ScratchZones;
	TArray<FTransform> ScratchWarnings;
	bool bZonesShown = false;

	TArray<FVector2D> IcePos;
	TArray<float> IceRadius;
	float Half = 5600.f;
	FName BuiltBiome;
	int32 BuiltRunSeed = -1;
	bool bRunSeedRolled = false;
	/** Layout attempt that passed validation (0 = first try; -1 = fell back to the fixed layout) */
	int32 LayoutAttempt = 0;

	// --- Terrain height grid: (GridN + 1)^2 vertex heights, row-major in Y ---
	TArray<float> Heights;
	int32 GridN = 0;
	float CellSize = 100.f;
	float InvCellSize = 0.01f;
	float GridMin = -5600.f;
	float TerrainMinZ = 0.f;
	float TerrainMaxZ = 0.f;
	TArray<FSQTerrainPlateau> Plateaus;
	/** Lamp post spots on the ground (Z = GroundZ). */
	TArray<FVector> LampSpots;

	// --- Mission layout ---
	FName LayoutName;
	int32 MapSeed = 0;
	float HubRadius = 1400.f;
	float KiteRing = 1100.f;
	float RoadHalfWidth = 300.f;
	TArray<FSQMapZone> MapZones;
	int32 BossZone = INDEX_NONE;
	TArray<FSQMapWall> MapWalls;
	/** Escort road centre line (start -> end), a point every ~250 cm. */
	TArray<FVector2D> RoadPts;
	/** Water bands / canals (Docks): centre-line segments and half widths; the ground is cut down under them. */
	TArray<FVector2D> WaterA;
	TArray<FVector2D> WaterB;
	TArray<float> WaterHalf;
	/** Spawner pads near the edges (spread order). */
	TArray<FVector2D> SpawnerSpots;
	/** Scattered collect spots (spread order). */
	TArray<FVector2D> CollectSpots;
	/** Open mid-ring spots for elites (spread order). */
	TArray<FVector2D> EliteSpots;
	/** Map revamp (Nov 10 2026) */
	TArray<FSQDistrict> Districts;
	TArray<FSQLane> Lanes;
	TArray<FSQSkateFeature> SkateFeatures;
	TArray<FSQParkSpot> ParkSpots;
	float LaneHalfWidth = 220.f;
	/** Landmark zone indices in two seeded orders (defend / elite). */
	TArray<int32> LandmarkOrder;
	TArray<int32> EliteOrder;

	// --- Horde blocker buckets (indices into HordeObs) ---
	TArray<FSQMapWall> HordeObs;
	/** Per obstacle: the far ends of the chain of joined runs it belongs to (arcs of chords), and the outward direction there. */
	TArray<FVector2D> HordeChainA;
	TArray<FVector2D> HordeChainB;
	TArray<FVector2D> HordeChainDirA;
	TArray<FVector2D> HordeChainDirB;
	TArray<TArray<int32>> HordeBuckets;
	int32 HordeGridN = 0;
	float HordeCell = 400.f;
};
