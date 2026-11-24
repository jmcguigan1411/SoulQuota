// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQTypes.h"
#include "SQHordeManager.generated.h"

class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UTexture;
class ASQCharacter;
class ASQArena;
class ASQGameState;
class USQData;
class UStaticMesh;
class UStaticMeshComponent;
class UMeshComponent;
struct FSQEnemyDef;

/** Client animation state of an enemy, replicated in FSQEnemyNet::State (low 3 bits; bit 3 = burst-spawned, bits 4-5 crown / miniboss,
 *  bit 6 = ripe for reaping (reap core, Nov 9); bit 7 free) */
enum class ESQEnemyAnimState : uint8
{
	Move = 0,      // walking / idle
	Windup = 1,    // telegraph: flashing, wind-up anim, ground marker; damage lands when it ends
	Strike = 2,    // the hit just landed (short follow-through)
	Charge = 3,    // charger dash
	Recover = 4,   // dizzy after a charge
	Frozen = 5,    // stopped in time (Stopwatch passive)
	Spawning = 6   // rising / dropping / bursting in (inert)
};

/** One enemy body touching a reaper (body blocking). Normal points from the enemy to the reaper (2D, unit). */
struct FSQBodyContact
{
	FVector2D Normal = FVector2D::ZeroVector;
	float Depth = 0.f;   // > 0 = overlapping by this many cm; <= 0 = within the contact skin only
};
typedef TArray<FSQBodyContact, TInlineAllocator<16>> FSQBodyContacts;

/** What one reap swing or dash passage reaped (server; ASQHordeManager::ApplyReap / ReapAlongPath) */
struct FSQReapHits
{
	int32 Reaped = 0;        // souls reaped: ripe executions + enemies the scythe's own damage killed
	int32 Executed = 0;      // ripe enemies executed
	int32 Hit = 0;           // enemies (and the boss) the swing touched
	bool bHitBoss = false;
	TArray<FVector2D, TInlineAllocator<16>> Spots;   // where each reaped soul fell (harvest revive, FX)
};

/**
 * Simulates the whole horde (enemies, pickups, projectiles) on the server as plain
 * data - no actor per enemy - and replicates compact snapshots at NetRate.
 * Everyone renders with instanced meshes, so 1,000+ enemies stay cheap.
 * The simulation is 2D; every rendered thing sits on ASQArena::GroundZ (never replicated).
 */
UCLASS()
class SOULQUOTA_API ASQHordeManager : public AActor
{
	GENERATED_BODY()

public:
	ASQHordeManager();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Co-op combos (Nov 23 2026, Docs/CoopFeatures.md) ---
	/** Server: while true every DamageEnemy hit PRIMES its target (an E / R is landing: FSQPrimeScope in USQAbilityComponent).
	 *  A primed enemy hit by ANOTHER reaper within ComboWindow bursts for ComboBurst x the priming damage in ComboRadius. */
	bool bPrimeHits = false;

	// --- Server API ---
	int32 ApplyAreaDamage(const FVector2D& Center, float Radius, float Damage, ASQCharacter* By, float Slow = 0.f);
	/** Beam weapons: damages every enemy (and the boss) within HalfWidth of the segment From-To, once each. */
	int32 ApplyLineDamage(const FVector2D& From, const FVector2D& To, float HalfWidth, float Damage, ASQCharacter* By, float Slow = 0.f);
	/**
	 * Chain weapons: hits the nearest enemy within FirstRange of From, then jumps to the nearest
	 * not-yet-hit enemy within JumpRange, up to Hits targets. OutPoints = From + every hit position (for the FX).
	 */
	int32 ApplyChainDamage(const FVector2D& From, int32 Hits, float FirstRange, float JumpRange, float Damage, ASQCharacter* By, float Slow, TArray<FVector2D>& OutPoints);
	void FindNearestTargets(const FVector2D& From, int32 Count, float MaxRange, TArray<FVector2D>& Out) const;
	bool FindNearestPickup(const FVector2D& From, uint8 Kind, float MaxRange, FVector2D& Out) const;
	FVector2D ComputeThreat(const FVector2D& From, float Range) const;
	void SpawnProjectile(const FVector2D& Pos, const FVector2D& Vel, float Damage, float Radius, float Life, int32 Pierce, bool bHostile, ASQCharacter* Shooter);
	/** bBurstSpawn: pops out of something (split) instead of the type's own spawn style */
	void SpawnEnemy(int32 Type, const FVector2D& Pos, bool bBurstSpawn = false);
	/** SpawnEnemy with extra health / damage multipliers and eco flags; returns the index in Enemies (INDEX_NONE when capped) */
	int32 SpawnEnemyEx(int32 Type, const FVector2D& Pos, bool bBurstSpawn, float HealthMult, float DamageMult, uint8 Flags);
	void SpawnEnemyNear(FName EnemyId, const FVector2D& Center, int32 Count, float Radius);
	void DropSouls(const FVector2D& Pos, int32 Amount);
	void DropXP(const FVector2D& Pos, int32 Amount);
	void ClearSoulPickups();
	void ClearAll();

	// --- Economy pass (Nov 7, server): gold coins, crowned elites, timeline beats, Final Swarm, vacuum ---
	/** Enemy flags (FEnemy::EcoFlags; replicated as FSQEnemyNet::State bits 4-5 for the crown / miniboss look) */
	static constexpr uint8 EcoCrowned = 1;
	static constexpr uint8 EcoMiniboss = 2;
	static constexpr uint8 EcoSwarm = 4;     // timeline swarm / Final Swarm spawn: never crowned
	/** Gold coins on the ground (pickup kind 2), split into up to 3 coins */
	void DropGold(const FVector2D& Pos, int32 Amount);
	/** Timeline miniboss: one EnemyId near the reapers with HealthMult / DamageMult, a big crown and a HUD name bar */
	void SpawnMiniboss(FName EnemyId, float HealthMult, float DamageMult, const FString& Name, bool bFreeChest);
	/** Timeline swarm wave: Count x EnemyId (x team size, Difficulty and the clauses' SpawnMult) around the reapers */
	void SpawnSwarm(FName EnemyId, int32 Count);
	/** Run events: is a timeline / event miniboss alive, where is it, and withdraw its free-chest drop (the Hunt got away) */
	bool HasMiniboss() const { return MinibossId != 0; }
	bool GetMinibossPos(FVector2D& Out) const;
	void ForfeitMinibossChest() { bMinibossChest = false; }
	/** The boss fell: every XP gem, gold coin and soul on the map is collected (XP to the team, gold shared, souls to the office) */
	void VacuumPickups();
	/** Gold a reaper picked up (or a vacuum): shared purse in co-op (Tuning GoldShareCoop), else the finder keeps it */
	void GiveGold(ASQCharacter* Finder, float Amount);
	/**
	 * Movement agent API (slide): pushes ground enemies within Radius of Center along Dir (radially out when Dir is zero)
	 * at Strength cm/s, decaying over ~0.3 s. Only enemies whose EnemyRadius <= MaxEnemyRadius; never flyers or ones still spawning in.
	 */
	void ApplyKnockback(const FVector2D& Center, float Radius, const FVector2D& Dir, float Strength, float MaxEnemyRadius);
	/** Kits pass (USQAbilityComponent): ApplyKnockback toward Center (vortex / taunt pulls). Same filters as ApplyKnockback; enemies
	 *  closer than 40 cm to Center are left alone so the crowd bunches instead of jittering. */
	void ApplyPull(const FVector2D& Center, float Radius, float Strength, float MaxEnemyRadius);
	/** Kits pass: damages every enemy (and the boss) whose body is within Range of Origin and inside HalfAngleDeg of Dir, once each. */
	int32 ApplyConeDamage(const FVector2D& Origin, const FVector2D& Dir, float Range, float HalfAngleDeg, float Damage, ASQCharacter* By, float Slow = 0.f);

	// --- Reaper passives (server) ---
	/**
	 * Lookout: marks the enemy with the most health within Range of From. Marked enemies take
	 * (1 + Bonus) x damage from everyone for Seconds and drop LookoutMarkSouls extra souls. OutId 0 = nothing marked.
	 */
	bool MarkToughestEnemy(const FVector2D& From, float Range, float Bonus, float Seconds, uint16& OutId, FVector2D& OutPos);
	/** Position of a living enemy by Id (false when it is gone) */
	bool FindEnemyPos(uint16 EnemyId, FVector2D& OutPos) const;
	/** Stopwatch: enemies within Radius stop moving and attacking for Seconds. Returns how many. */
	int32 FreezeArea(const FVector2D& Center, float Radius, float Seconds);
	void DebugSpawn(int32 Count);
	int32 NumAlive() const;

	// --- Reap core (Nov 9 2026; USQReapComponent in SQReap.h; Docs/Balance.md section 12) ---
	/** Server: alive, below Tuning ReapRipeFraction of its spawn health and not a miniboss (bosses are actors and never ripe).
	 *  Replicated to clients as FSQEnemyNet::State bit 6 (teal ring + skull tag). */
	bool IsEnemyRipe(int32 Index) const;
	/**
	 * Server: the scythe. Every enemy whose body is within Range of Origin and inside HalfAngleDeg of Dir: ripe ones are executed,
	 * the rest take Damage (minibosses Damage x BossBonus and are never executed). The boss in the cone takes Damage x BossBonus.
	 */
	void ApplyReap(const FVector2D& Origin, const FVector2D& Dir, float Range, float HalfAngleDeg, float Damage, float BossBonus, ASQCharacter* By, FSQReapHits& Out);
	/** Server: Reaper's Passage. Ripe enemies whose body touches the segment From-To (widened by Radius) are executed. */
	void ReapAlongPath(const FVector2D& From, const FVector2D& To, float Radius, ASQCharacter* By, FSQReapHits& Out);
	/** Server (bot reaping): nearest ripe enemy within Range of From */
	bool FindNearestRipe(const FVector2D& From, float Range, FVector2D& OutPos) const;
	/** Early-curve pass: the catch-up multiplier on new enemies' HP (1 +- Tuning CatchUpMax; server) */
	float GetCatchUpMult() const { return CatchUpMult; }
	/** Early-curve pass: most living enemies the regular spawn loop allows right now (time curve x crew x spawn multipliers; server) */
	int32 GetOnScreenCap() const;

	// --- Body blocking (every machine: server = sim positions, owning client = its interpolated positions) ---
	/**
	 * Ground enemies whose body touches a reaper circle at At (Radius cm, Skin cm of extra contact margin).
	 * FeetAboveGround: how high the reaper's feet are above the ground there; enemies shorter than that are jumped over.
	 * Flyers, wisps and enemies still spawning in never block. Uses the spatial grid (3x3 cells). Returns the count.
	 */
	int32 GatherBodyContacts(const FVector2D& At, float Radius, float Skin, float FeetAboveGround, FSQBodyContacts& Out) const;
	/** True when any living enemy (flyers included) is within Range of At (server: sim, clients: interpolated). */
	bool AnyEnemyWithin(const FVector2D& At, float Range) const;
	/** Camera / aim agent (gamepad auto-aim): nearest living enemy or the boss within MaxRange on any machine (server: sim, clients: displayed). */
	bool FindNearestEnemyAnyMachine(const FVector2D& From, float MaxRange, FVector2D& OutPos) const;
	/** Does this enemy type block reapers (ground body, not a wisp)? */
	static bool TypeBlocks(const FSQEnemyDef& Def);
	/** Height (cm) a reaper's feet must clear to jump over this type (Tuning BodyBlockHeightMult x art height) */
	float TypeBlockHeight(const FSQEnemyDef& Def) const;

	// --- Collision shapes (Nov 7 collision pass; every machine computes the same numbers from the same CSV + art) ---
	/**
	 * Body circle of an enemy type (cm): Enemies.csv Radius blended toward the width of its art (impostor mesh bounds x FitScale
	 * x Stretch.X) by Tuning CollisionArtFit (1 = art). Body blocking, crowd spacing and every weapon / projectile hit use it.
	 */
	float EnemyRadius(int32 Type) const;
	/** Centre distance at which this type's melee / contact hit lands on a reaper of ReaperRadius: EnemyRadius + ReaperRadius + Enemies.csv Reach */
	float EnemyAttackReach(int32 Type, float ReaperRadius) const;
	/** A reaper's body circle (cm): its capsule radius (Tuning BodyBlockUseCapsule 1, default), else Tuning BodyBlockPlayerRadius. */
	static float ReaperBodyRadius(const ASQCharacter* Reaper, const USQData* Data);
	/** SQShowCollision console command (host, non-Shipping): draws body / reach circles of the horde and the reapers' block circles */
	bool bShowCollision = false;
	int32 NumPickups() const;

	// --- Replicated snapshots ---
	UPROPERTY(Replicated) TArray<FSQEnemyNet> EnemyNet;
	UPROPERTY(Replicated) TArray<FSQPickupNet> PickupNet;
	UPROPERTY(Replicated) TArray<FSQProjNet> ProjNet;
	/**
	 * [0] = snapshot sequence (forces OnRep every snapshot), then Ids of enemies damaged since the last snapshot,
	 * then (optional) a 0 marker followed by Ids killed by a big hit since the last snapshot (their bodies get launched).
	 * Entity Ids are never 0, so the marker is unambiguous.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_HitNet) TArray<uint16> HitNet;

	/** Local count of enemies seen dying (drives kill sounds) */
	int32 LocalDeathCount = 0;

protected:
	struct FEnemy
	{
		uint16 Id = 0;
		uint8 Type = 0;
		bool bAlive = true;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Vel = FVector2D::ZeroVector;
		float Health = 1.f;
		float AttackCd = 0.f;
		float SlowTime = 0.f;
		float FrozenTime = 0.f;   // Stopwatch passive: no movement, no attacks
		float MarkTime = 0.f;     // Lookout passive: takes extra damage while > 0
		float MarkBonus = 0.f;
		int32 Stolen = 0;
		bool bHitNet = false;   // damaged since the last snapshot (goes to HitNet)
		bool bHitView = false;  // damaged since the last local render (listen-server host)
		// --- animation personality (server state machine, replicated as one byte) ---
		float Age = 0.f;        // seconds alive (inert while spawning in)
		float StateTime = 0.f;  // seconds left in the current wind-up / strike / charge / recover
		uint8 AnimState = 0;    // ESQEnemyAnimState
		bool bBurstSpawn = false;
		bool bChargeWindup = false;  // the current wind-up ends in a charge, not a swing (Gravebat: a swoop dive)
		bool bPhaseWindup = false;   // Wraith: the current wind-up is a phase-out that ends in a blink
		float PhaseCd = 0.f;         // Wraith: seconds until it may phase-blink again
		FVector2D ChargeDir = FVector2D::ZeroVector;
		// --- economy pass ---
		float MaxHealth = 1.f;       // spawn health (miniboss bar)
		float DamageMult = 1.f;      // miniboss / Final Swarm tiers hit harder
		uint8 EcoFlags = 0;          // EcoCrowned | EcoMiniboss | EcoSwarm
		FVector2D Knock = FVector2D::ZeroVector; // ApplyKnockback velocity (cm/s), decays
		// --- co-op combos (Nov 23 2026) ---
		float PrimeTime = 0.f;       // seconds left in which another reaper's hit bursts this enemy
		float PrimeDamage = 0.f;     // the ability damage that primed it
		uint32 PrimeBy = 0;          // GetUniqueID() of the reaper who primed it
	};

	struct FPickup
	{
		uint16 Id = 0;
		uint8 Kind = 0; // 0 XP, 1 Soul, 2 Gold
		bool bAlive = true;
		FVector2D Pos = FVector2D::ZeroVector;
		int32 Value = 1;
		float Age = 0.f;
	};

	struct FProjectile
	{
		uint16 Id = 0;
		bool bHostile = false;
		bool bAlive = true;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Vel = FVector2D::ZeroVector;
		float Damage = 0.f;
		float Radius = 30.f;
		float Life = 1.f;
		int32 Pierce = 1;
		bool bHitBoss = false;
		TWeakObjectPtr<ASQCharacter> Owner;
		TArray<uint16, TInlineAllocator<6>> HitIds;
	};

	struct FViewItem
	{
		uint16 Id = 0;
		uint8 Type = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		bool bHit = false;
		uint8 State = 0;        // FSQEnemyNet::State byte
		// Client-derived (carried from the previous frame by CarryViewState; never replicated)
		float Born = 0.f;       // local time first seen (spawn animation)
		float StateAt = 0.f;    // local time the current state started (telegraph progress)
		float Yaw = 0.f;        // smoothed facing (instanced bodies)
		float MoveAmt = 0.f;    // observed speed / type speed (lean, bob)
		// Flyers (client-derived from type + state, never replicated)
		float FlyZ = 0.f;       // smoothed height above the (smoothed) ground
		float FlyGround = 0.f;  // smoothed ground height under it (glides over cliffs instead of snapping)
		// Far impostor <-> skinned hand-off (client-derived, carried like Yaw)
		float WalkPhase = 0.f;  // 0..1 through the walk cycle (impostor flipbook frame; written back from the skinned leader)
		float SlopePitch = 0.f; // smoothed lean with the ground slope (deg), same as the skinned follower's
	};

	void ServerSim(float DeltaSeconds);
	void SimSpawning(float DeltaSeconds, const TArray<ASQCharacter*>& Players);
	void SimEnemies(float DeltaSeconds, const TArray<ASQCharacter*>& Players);
	void SimPickups(float DeltaSeconds, const TArray<ASQCharacter*>& Players);
	void SimProjectiles(float DeltaSeconds, const TArray<ASQCharacter*>& Players);
	void BuildSnapshots();
	void RebuildGrid();
	void DamageEnemy(int32 Index, float Damage, ASQCharacter* By, float Slow, bool bFlash = true);
	void KillEnemy(int32 Index, ASQCharacter* By);
	// --- co-op combos: bursts are queued by DamageEnemy and run at the start of the next sim step (no re-entrancy) ---
	struct FPendingCombo
	{
		FVector2D At = FVector2D::ZeroVector;
		float Damage = 0.f;
		TWeakObjectPtr<ASQCharacter> By;
	};
	TArray<FPendingCombo> PendingCombos;
	TMap<uint32, float> ComboReadyAt; // per detonating reaper: SimClock when their next combo may fire
	bool bInCombo = false;
	bool bComboSolo = false;   // one reaper on the crew: their own weapons burst what their own E / R primed
	float ComboWindow = 4.f;
	float ComboBurst = 0.6f;
	float ComboRadius = 260.f;
	float ComboCooldown = 0.35f;
	void RunCombos(const TArray<ASQCharacter*>& Players);
	void SpawnPickup(uint8 Kind, const FVector2D& Pos, int32 Value);
	int32 PickEnemyType(int32 Night, float Progress) const;
	/** Spawn spot at least MinPlayerDist from every reaper (SpawnSpots) and not on a cliff face (flyers may hover over one) */
	bool SpawnSpotOk(const FVector2D& Spot, float MinPlayerDist, bool bFlyer = false) const;
	/** Next entity Id (never 0: 0 means "none" for marks and Ids wrap after 65535) */
	uint16 TakeNextId();
	// --- Reap core / early curve (server) ---
	/** The replicated state byte of an enemy (anim state, burst, crown, miniboss, ripe) */
	uint8 EnemyNetState(const FEnemy& E) const;
	/** Kills a ripe enemy outright (its body launches); credited to By like any kill */
	void ExecuteEnemy(int32 Index, ASQCharacter* By);
	/** Minutes into the stage at release length (StageTime / NightScale): the early curve runs on these */
	float StageReleaseMinutes(const ASQGameState* GS) const;
	/** Solo spawn rate of the time curve (before crew / mission / Difficulty multipliers) */
	float SpawnCurveRate(const ASQGameState* GS, const USQData* D) const;
	/** New enemies' HP x this in act 1 (EarlyHealthStart easing to 1 at EarlyHealthMinutes) */
	float EarlyHealthMult(const ASQGameState* GS, const USQData* D) const;
	/** Catch-up: compares how long enemies stay near the crew (Little's law: near count / kill rate) with CatchUpTargetSeconds */
	void TickCatchUp(float DeltaSeconds, const TArray<ASQCharacter*>& Players);
	/** Solo Training Shift: while USQTutorial::ShouldHoldSpawns, keep at most TutorialHoldEnemies slow enemies near the reaper */
	void SimTutorialHold(const TArray<ASQCharacter*>& Players, const USQData* D);
	float RipeFraction = 0.25f;          // Tuning ReapRipeFraction (refreshed each tick)
	float CatchUpMult = 1.f;
	float CatchUpKillRate = 0.f;         // kills / s (smoothed)
	float CatchUpNear = 0.f;             // enemies near the crew (smoothed)
	float CatchUpTimer = 0.f;
	int32 CatchUpKills = 0;              // kills since the last catch-up tick
	float TutorialHoldTimer = 0.f;

	/** Best Widow (SlowSouls) passive value on the crew, worked out once per frame instead of per kill */
	float GetWidowBest();
	TArray<FVector2D> SpawnSpots;   // reaper positions for spawn checks (scratch)
	float WidowBestCache = 0.f;
	uint64 WidowBestFrame = TNumericLimits<uint64>::Max();
	FVector2D ClampToArena(const FVector2D& P) const;
	float SimClock = 0.f;                  // server sim time (gait phases)
	TArray<uint16> PendingLaunchIds;       // server: killed by a big hit since the last snapshot
	TSet<uint16> LaunchIds;                // every machine: enemies whose death launches the body (consumed at death)
	FIntPoint Cell(const FVector2D& P) const;

	void RenderAll(float DeltaSeconds);
	/** Fills Born / StateAt / Yaw / MoveAmt of every view enemy from last frame's view (LastEnemyView) */
	void CarryViewState(float Now, float DeltaSeconds);
	/**
	 * Far swarm (everything not skinned): types with impostor art draw as their character (4-frame flipbook ISMs, the same
	 * transform as the skinned follower); the rest as instanced primitives with per-type gait (bob, lean, squash), stacks and props.
	 */
	void RenderEnemyISMs(float Now, float DeltaSeconds);
	/** Ground telegraphs (discs / charge lanes) under every enemy winding up near the view, plus spawn and footfall FX */
	void RenderTelegraphs(float Now, float DeltaSeconds);
	void EmitSpawnFx(const FViewItem& V, float Age, float DeltaSeconds, int32& Budget);
	/** Bones, skulls and props flying off deaths (instanced, pooled) */
	void SpawnDebris(const FVector& At, const FVector& Away, UStaticMesh* Mesh, UMaterialInstanceDynamic* MID, float Size, int32 Count, float Power);
	void TickDebris(float DeltaSeconds);
	UInstancedStaticMeshComponent* DebrisISMFor(UStaticMesh* Mesh, UMaterialInstanceDynamic* MID);
	void UpdateISM(UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xforms);
	void EnsureRenderComponents();

	// --- Skinned horde (imported animated characters near the local player) ---
	void EnsureSkinnedComponents();
	/** Also writes each skinned enemy's facing, slope lean and walk phase back into its view item (seamless hand-off) */
	void RenderSkinned(TArray<FViewItem>& EnemyView, float DeltaSeconds, TArray<bool>& OutSkinned);
	/** Death of a nearby enemy: puppet tumble / launch, burst into bones, pop or fade (by type; bLaunched = big killing hit) */
	void SpawnDeathPuppet(const FViewItem& Dead, bool bLaunched);
	void TickDeathPuppets(float DeltaSeconds);
	FVector2D LocalViewCenter() const;
	int32 FindOrMakeLeader(USkeletalMesh* ArtMesh, FName Model, FName AnimName, UAnimSequence* AnimSeq, int32 Variant, float Rate = 1.f);
	/** Ground height under a point (0 without an arena). FrameArena is refreshed once per render. */
	float GroundAt(const FVector2D& P) const;
	/** Nearest standing player character (positions gathered once per render). False when there is none. */
	bool NearestPlayer(const FVector2D& From, FVector2D& OutPos, float& OutDistSq) const;
	UFUNCTION() void OnRep_HitNet();

	struct FPendingSpawn
	{
		int32 Type = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		bool bBurst = false;
	};
	TArray<FPendingSpawn> PendingSpawns;

	TArray<FEnemy> Enemies;
	TArray<FPickup> Pickups;
	TArray<FProjectile> Projectiles;
	TMap<FIntPoint, TArray<int32>> Grid;

	// Body blocking on non-authority machines: ground enemies near the local pawn at their interpolated
	// positions, bucketed like Grid (rebuilt every render). The server reads Enemies + Grid instead.
	struct FBlocker
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float Radius = 40.f;
		float Height = 120.f;
	};
	TArray<FBlocker> ClientBlockers;
	TMap<FIntPoint, TArray<int32>> ClientBlockGrid;
	void BuildClientBlockers();
	float BlockHeightMult = 0.8f;   // Tuning BodyBlockHeightMult (refreshed each tick)

	uint16 NextId = 1;
	float SpawnAccum = 0.f;
	// Economy pass: tracked timeline miniboss (HUD bar), Final Swarm spawn accumulators (one per Timeline row), quest stat names
	uint16 MinibossId = 0;
	bool bMinibossChest = false;
	TArray<float> FinalSwarmAccum;
	TArray<FName> KillStatByType;
	/** A spot near a random reaper, at least MinGap from every reaper; false when none was found */
	bool FindBeatSpot(float MinR, float MaxR, float MinGap, bool bFlyer, FVector2D& Out);
	void SimFinalSwarm(float DeltaSeconds, const ASQGameState* GS, const USQData* D);
	void UpdateMinibossTracker();
	float NextNetTime = 0.f;
	float CellSize = 250.f;
	float ArenaHalf = 6000.f;

	// Client interpolation state
	TMap<uint16, FVector2D> EnemyDisplay;
	TMap<uint16, FVector2D> PickupDisplay;
	TMap<uint16, FVector2D> ProjDisplay;

	struct FSplat
	{
		uint8 Type = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		float Scale = 1.f;
		float Yaw = 0.f;
		float Z = 0.f;   // ground height when it landed
	};
	void UpdateSplats(const TArray<FViewItem>& EnemyView);
	/** Pickup tier 0..3 from its value (Tuning XpTier1-3 / GoldTier1-3); sent in the high nibble of FSQPickupNet::Kind */
	uint8 PickupTier(const FPickup& P) const;
	uint8 PackedPickupKind(const FPickup& P) const { return static_cast<uint8>((P.Kind & 0x0F) | (PickupTier(P) << 4)); }
	/** Lots of XP / gold on the ground: nearby pickups of a kind fold together into bigger tiers (Tuning PickupMerge*) */
	void MergePickups(float DeltaSeconds, const TArray<ASQCharacter*>& Players);
	float PickupMergeClock = 0.f;
	TArray<FSplat> Splats;
	int32 SplatHead = 0;
	bool bSplatsDirty = false;
	TMap<uint16, FViewItem> LastEnemyView;

	struct FSkinType
	{
		USkeletalMesh* Mesh = nullptr;        // kept alive by SkinAssetRefs
		UAnimSequence* DeathA = nullptr;      // kept alive by SkinAssetRefs
		UAnimSequence* DeathB = nullptr;
		int32 Leaders[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };       // move loop, into SkinLeaders
		int32 AttackLeaders[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE }; // attack / cast loop (optional)
		float AttackRange = 0.f;              // 0 = this type never plays its attack loop
		UTexture* Tex = nullptr;              // palette (T_<Model> or the shared skeleton texture), kept alive by SkinAssetRefs
		float MeshScale = 1.f;
		float ArtHeight = 150.f;
		FLinearColor Tint = FLinearColor::White;
		// one-shots (kept alive by SkinAssetRefs)
		UAnimSequence* AttackSeq = nullptr;   // wind-up + strike, timed so the impact lands when the wind-up ends
		UAnimSequence* HitSeq = nullptr;
		UAnimSequence* SpawnSeq = nullptr;
		float MoveRate = 1.f;
		float MoveLen = 1.f;                  // seconds of the move loop (impostor walk phase -> leader phase)
		float BodyGlow = 0.f;                 // elites glow
		int32 StackLeaders[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE }; // riders on a tower
		// prop accessory on a bone
		UStaticMesh* PropMesh = nullptr;
		FName PropSocket;
		FTransform PropRel = FTransform::Identity;
		// the same prop on a far impostor: mesh component space at the reference pose (hands pulled in from the T-pose)
		FTransform FarPropCS = FTransform::Identity;
		bool bFarProp = false;
	};
	struct FSkinLeaderKey
	{
		FName Model;
		FName Anim;
		int32 Variant = 0;
		float Rate = 1.f;
	};
	struct FSkinSlot
	{
		int32 EnemyId = INDEX_NONE;
		int32 Type = INDEX_NONE;
		int32 LeaderIdx = INDEX_NONE;   // leader currently driving this follower
		FVector2D LastPos = FVector2D::ZeroVector;
		FVector2D JoltDir = FVector2D::ZeroVector;
		float Yaw = 0.f;
		float Pitch = 0.f;
		float MoveHold = 0.f;   // > 0 while the enemy is walking
		float HitAge = 10.f;    // seconds since the last hit (flash / squash / jolt)
		float SpawnAge = 10.f;  // seconds since it rose from the ground
		float CurGlow = 0.f;    // last Glow sent to the slot MID
		float CurWarm = 0.f;    // last wind-up tint amount sent to the slot MID
		float OneShotLeft = 0.f;  // seconds left of a per-follower one-shot (wind-up / hit / spawn)
		bool bOneShot = false;    // playing its own one-shot (not following a leader)
		uint8 OneShotKind = 0;    // 0 none, 1 attack, 2 hit react, 3 spawn
		uint8 LastState = 0;
		bool bAttacking = false;
		bool bHasPos = false;
		bool bShown = false;
	};
	struct FPuppetState
	{
		int32 Type = INDEX_NONE;
		FVector Loc = FVector::ZeroVector;
		float Age = 0.f;
		float SinkDepth = 150.f;
		bool bActive = false;
		uint8 Style = 0;                       // ESQEnemyDeath (Tumble / Launch / Fade use a puppet)
		FVector Vel = FVector::ZeroVector;     // Launch: ballistic flight
		FRotator Rot = FRotator::ZeroRotator;
		FRotator Spin = FRotator::ZeroRotator; // deg/s
		float Scale = 1.f;
		bool bLanded = false;
		FVector BaseScale = FVector::OneVector;  // living size (stretch included); the landing squash wobbles around it
		float RollSign = 1.f;                    // which side a tumble twists to
		float LandAge = -1.f;                    // seconds since the last impact (-1 = not landed yet)
	};
	struct FDebris
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FRotator Spin = FRotator::ZeroRotator;
		float Age = 0.f;
		float Life = 1.6f;
		float Scale = 1.f;
		int32 ISMIdx = INDEX_NONE;
		bool bActive = false;
	};
	TArray<FSkinType> SkinTypes;
	TArray<FSkinLeaderKey> SkinLeaderKeys;
	TArray<FSkinSlot> SkinSlots;
	TMap<uint16, int32> SkinSlotOfId;
	TMap<uint16, float> FreedSkinYaw;   // yaw of enemies whose follower slot was freed this frame
	TArray<FPuppetState> PuppetStates;
	int32 PuppetHead = 0;
	int32 ActiveOneShots = 0;
	int32 OneShotCap = 24;
	TArray<FDebris> Debris;
	int32 DebrisHead = 0;
	TArray<UStaticMesh*> DebrisMeshes;      // parallel to DebrisISMs (kept alive by SkinAssetRefs / the ISMs)
	TArray<TArray<FTransform>> ScratchDebris;
	TArray<FTransform> ScratchTeleOuter;
	TArray<FTransform> ScratchTeleFill;
	TArray<TArray<FTransform>> ScratchPropPerType;
	bool bViewPrimed = false;              // false until the first enemy view (late joiners: no spawn anims for everyone)
	TArray<bool> TypePropIsBody;           // per type: the prop is the whole body (instanced at every distance)
	TArray<float> TypePropFit;             // per type: prop scale for its PropSize x Scale
	TArray<int32> StackLeaderOf;           // per stack follower: leader it copies
	TArray<int32> StackTypeOf;             // per stack follower: enemy type its mesh is set up for
	int32 StackShown = 0;
	bool bSkinInitDone = false;
	bool bAnySkinArt = false;
	float SkinRadius = 4200.f;
	FVector2D SkinViewCenter = FVector2D::ZeroVector;
	float AttackAnimRange = 180.f;
	float HitFlashGlow = 3.f;

	// Hit signal (clients: filled by OnRep_HitNet, consumed by the next render)
	uint16 HitSeq = 0;
	TSet<uint16> PendingHits;

	// Per-render cache (valid only inside RenderAll)
	const ASQArena* FrameArena = nullptr;
	TArray<FVector2D> FramePlayers;

	// Scratch buffers reused every frame (no per-frame allocation once warm)
	TArray<FViewItem> ViewEnemies;
	TArray<FViewItem> ViewPickups;
	TArray<FViewItem> ViewProjs;
	TArray<bool> ScratchSkinnedMask;
	TArray<TArray<FTransform>> ScratchPerType;
	TArray<FTransform> ScratchXP;
	TArray<FTransform> ScratchSoul;
	TArray<FTransform> ScratchSoulFull;
	TArray<FTransform> ScratchSoulTail;
	TArray<FTransform> ScratchSoulMote;
	TArray<FTransform> ScratchSoulPool;
	TArray<FTransform> ScratchSoulHalo;
	/** Soul collection flourish: ids drawn last frame / this frame, and souls flying into a reaper */
	struct FSoulFly { FVector From = FVector::ZeroVector; FVector LastTarget = FVector::ZeroVector; TWeakObjectPtr<class ASQCharacter> To; float Age = 0.f; float Seed = 0.f; };
	TMap<int32, FVector> SoulSeenPrev;
	TMap<int32, FVector> SoulSeenNow;
	TArray<FSoulFly> SoulFlies;
	void RenderSoulAbsorb(float DeltaSeconds);
	TArray<FTransform> ScratchProj;
	TArray<FTransform> ScratchHostile;
	TMap<uint16, FVector2D> ScratchDisplay;
	TArray<TPair<float, int32>> ScratchCandidates;
	TArray<int32> ScratchSlotView;
	TArray<int32> ScratchNewView;
	TMap<uint16, FViewItem> ScratchCurrent;
	TArray<FViewItem> ScratchVanished;
	TArray<TArray<FTransform>> ScratchSplatPerType;

	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> SkinLeaders;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> SkinFollowers;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> DeathPuppets;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SkinMIDs;   // per type (death puppets)
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SlotMIDs;   // per follower slot (hit flash)
	UPROPERTY() TArray<TObjectPtr<UObject>> SkinAssetRefs;

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SlotProps;        // per follower slot (prop accessory)
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> StackFollowers; // tower riders
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PropMIDs;     // per type (prop accessory, glow)
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> PropISMs; // per type, far swarm props (nullptr = none)
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> DebrisISMs;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BoneDebrisMID;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> TeleOuterISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> TeleFillISM;
	/** Ground shadow blobs under flyers (every machine that renders) */
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ShadowISM;
	TArray<FTransform> ScratchShadow;
	/** Flyer height above the ground for a view item: from type + state + age (no extra replicated bytes) */
	float FlyTargetZ(const FSQEnemyDef& Def, const FViewItem& V, float Now) const;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> SplatISMs;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> EnemyISMs;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> XPISM;
	/** Pickup ladders (Nov 10 2026): [0..3] XP crystals (green -> white), [4..7] gold (coin, coin stack, purse, gold bar) */
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> PickupTierISMs;
	TArray<float> PickupTierNorm;
	TArray<TArray<FTransform>> ScratchPickupTier;
	/** Economy pass: gold coins, crowns on crowned elites / minibosses, the miniboss ground ring */
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GoldISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CrownISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MiniRingISM;
	TArray<FTransform> ScratchGold;
	TArray<FTransform> ScratchCrown;
	TArray<FTransform> ScratchMiniRing;
	/** Reap core: soul-teal ring under every ripe enemy (skinned or impostor) and a tiny skull tag over it (pooled ISMs) */
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RipeRingISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RipeSkullISM;
	TArray<FTransform> ScratchRipeRing;
	TArray<FTransform> ScratchRipeSkull;
	int32 RipeRingCapacity = 0;
	int32 RipeSkullCapacity = 0;
	float RipeSkullFit = 0.2f;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SoulISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SoulFullISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SoulTailISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SoulMoteISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SoulPoolISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SoulHaloISM;
	bool bSoulGlow = false; // souls drawn with the additive glow material (M_SQGlow); false = the solid Nov 10 look
	float SoulNorm = 1.f; // SM_FX_Flame longest side -> 100 cm (1 when the sphere fallback is used)
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ProjISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> HostileProjISM;
	UPROPERTY() TObjectPtr<USceneComponent> Root;

	// ---------------- per-type collision + far impostors (Nov 7)
	/**
	 * Perf notes (estimates; KayKit / zombie meshes are 2.1k-5.9k triangles):
	 *  - Skinned followers copy a hidden leader's pose (no anim graph each), but every follower is its own skinned draw (2 material
	 *    slots) + shadow depth: SkinnedEnemyPool 220 ~ 900 draws and ~0.9M triangles at most. That is the cost of the near crowd.
	 *  - Impostors are GPU-instanced static meshes: one ISM per type x frame (at most 18 x 4 = 72; an empty one draws nothing),
	 *    so draw calls stay flat with the horde size. 1,500 far enemies x ~4k triangles ~ 6M triangles (+ shadow depth;
	 *    Tuning ImpostorShadows 0 drops that). Instance counts per frame-ISM change every few frames, so UpdateISMPadded keeps a
	 *    stable capacity (hidden zero-size spares) instead of rebuilding the instance buffer.
	 *  - Everything is pooled: followers, riders, puppets and impostor ISMs are created once and reused.
	 */
	static constexpr int32 ImpostorFrames = 4;
	struct FImpostorSet
	{
		UStaticMesh* Frames[ImpostorFrames] = { nullptr, nullptr, nullptr, nullptr }; // kept alive by ImpostorAssetRefs
		int32 NumValid = 0;      // frames found on disk (missing ones reuse the previous frame)
		float MeshScale = 1.f;   // the skinned mesh's FitScale for this type (same cm scale as SK_<Model>)
		float ArtRadius = 0.f;   // measured body radius (cm), 0 = no art
	};
	/** Loads impostor frames, measures each type's art radius and fills TypeRadius / TypeReach (every machine, dedicated server too) */
	void InitTypeShapes();
	/** Client: one ISM per type x frame with the type's tinted material and glow slots */
	void EnsureImpostorComponents();
	/** Puts the type's glowing MID on every material slot whose name contains "Glow" (eyes, ghost light) */
	void ApplyTypeGlowSlots(UMeshComponent* Comp, int32 Type);
	/** UpdateISM for instance counts that move every frame: pads to a stable capacity with hidden spares (zero size) */
	void UpdateISMPadded(UInstancedStaticMeshComponent* ISM, TArray<FTransform>& Xforms, int32& Capacity);
	/** One far enemy as an impostor instance (+ riders, + far prop): the skinned follower's transform, flipbook frame by walk phase */
	void AddImpostorInstance(FViewItem& V, const FSQEnemyDef& Def, const FImpostorSet& Imp, float Now, float DeltaSeconds, const FQuat& ArtQuat);
	/** SQShowCollision: debug circles (host only, compiled out of Shipping) */
	void DrawCollisionDebug();
	TArray<FImpostorSet> ImpostorSets;
	TArray<float> TypeRadius;
	TArray<float> TypeReach;
	float MaxTypeRadius = 90.f;
	bool bTypeShapesDone = false;
	bool bImpostorInitDone = false;
	float ImpostorPullRate = 4.f;
	TArray<TArray<FTransform>> ScratchImpostor;   // [Type * ImpostorFrames + Frame]
	TArray<int32> ImpostorCapacity;               // [Type * ImpostorFrames + Frame]
	TArray<float> ScratchLeaderPhase;             // [Type * 3 + Variant]: the skinned walk leader's phase this frame (-1 = none)
	UPROPERTY() TArray<TObjectPtr<UObject>> ImpostorAssetRefs;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> ImpostorISMs; // [Type * ImpostorFrames + Frame], nullptr = none
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> TypeGlowMIDs;      // per type (created on first use)
};

/** Marks every horde hit inside its lifetime as an ability hit that primes enemies for a squad combo (server). */
struct FSQPrimeScope
{
	explicit FSQPrimeScope(ASQHordeManager* InHorde) : Horde(InHorde) { if (Horde) Horde->bPrimeHits = true; }
	~FSQPrimeScope() { if (Horde) Horde->bPrimeHits = false; }
	FSQPrimeScope(const FSQPrimeScope&) = delete;
	FSQPrimeScope& operator=(const FSQPrimeScope&) = delete;
private:
	ASQHordeManager* Horde = nullptr;
};
