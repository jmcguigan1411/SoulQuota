// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQFx.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class ASQCharacter;

/** Local-only area effect. Kind 0 = impact (light wash + shockwave, echo ring, flash, cracks, shards, motes); Kind 1 =
 *  telegraph / zone (held rim rings + a fill that grows to the impact for danger colours, a breathing pool + seal for
 *  friendly ones). Layers come from the pooled sparks; the actor only owns the disc. */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQFx : public AActor
{
	GENERATED_BODY()

public:
	ASQFx();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static void Spawn(UWorld* World, uint8 Kind, const FVector& Location, float Radius, const FLinearColor& Color, float Duration);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;

private:
	uint8 Kind = 0;
	float Radius = 100.f;
	float Duration = 0.3f;
	float Elapsed = 0.f;
	bool bCountedLive = false; // counted in the live-effect cap (SQFx.cpp)
	bool bDanger = false;      // Kind 1 in a warning colour (red / orange): the fill grows to the impact moment
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> GlowMat; // additive disc material (null = opaque fallback)
};

/** Shape a pooled particle is drawn with: engine basic shapes, then the stylised effect kit (Nov 10 2026, ArtSource/FX ->
 *  /Game/Art/FX/SM_FX_<Name>; falls back to a sphere until imported). Kit shapes are normalised to 100 cm on their longest side.
 *  Kit conventions: X = travel / length, flat shapes lie in XY (read from the top-down camera), Flame / Spiral / Drop point up +Z. */
enum class ESQFxShape : uint8
{
	Sphere, Cube, Cylinder, Cone,
	Crescent, Ring, Flame, Puff, Spiral, Shard, Bolt, Star, Bat, Crow, Moth, Leaf, Feather, Rune, Seal, Cleaver, Claw, Bandage, Seed, Drop,
	// Nov 19 2026 motion shapes: Swoosh / Arc = 120 degree tapered arcs with the PIVOT AT THE ARC CENTRE (spin about Z to sweep; size = radius x 1.732),
	// Streak = needle along X, Wave = thin shockwave band, Spikes = flat starburst, Ember = diamond mote, Petal = teardrop blade, Chevron = arrowhead, Wisp = little ghost flame
	Swoosh, Arc, Streak, Wave, Spikes, Ember, Petal, Chevron, Wisp, Teeth,
	Sigil, Cog, Web, Casket,
	Count
};

/**
 * One pooled, glowing, local-only particle (weapon trails, tracers, slabs, shards, rings...).
 * Sizes are in cm along the particle's local X / Y / Z (X = length for streaks and tracers).
 */
struct FSQFxParticle
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	ESQFxShape Shape = ESQFxShape::Sphere;
	float Life = 0.3f;
	float Delay = 0.f;                       // hidden (and frozen) for this long first
	FVector SizeStart = FVector(10.f);
	FVector SizeEnd = FVector(10.f);         // lerped over the life
	FQuat Rot = FQuat::Identity;
	FVector SpinAxis = FVector::UpVector;    // local axis
	float SpinRate = 0.f;                    // rad/s
	float Gravity = 0.f;                     // cm/s^2 on Z (negative falls)
	float Drag = 0.f;
	float FloorZ = -1.0e9f;                  // falling particles stop here
	bool bAlignToVelocity = false;           // local X follows the velocity (spin then turns about X)
	bool bShrinkOut = true;                  // shrinks away over the last third of its life
	bool bEaseOut = false;                   // size lerp eases out (fast start)
	float TrailEvery = 0.f;                  // seconds between trail puffs (0 = none)
	float TrailSize = 6.f;
	FLinearColor TrailColor = FLinearColor::White;
	uint8 DeathFx = 0;                       // 0 none, 1 spark burst, 2 burst + ground ring
	int32 DeathCount = 6;
	float DeathSize = 7.f;
	FLinearColor DeathColor = FLinearColor::White;
	// Nov 19 2026 (need M_SQGlow2 for the fades; without it these particles just shrink as before)
	float FadeIn = 0.f;                      // seconds to reach full brightness
	float FadeOut = 0.f;                     // fraction of the life, at the end, over which it fades to nothing (0 = no fade)
	float Bright = 1.f;                      // brightness multiplier (additive shapes only)
	float SpinDecay = 0.f;                   // SpinRate *= 1 - SpinDecay * dt (a swing that whips round and eases out)
	float OrbitRate = 0.f;                   // rad/s about the vertical axis through OrbitPivot (position and velocity turn together)
	FVector OrbitPivot = FVector::ZeroVector;
};

/** A floating damage / heal number for the HUD (world position, already risen) */
struct FSQDamageNumberView
{
	FVector Pos = FVector::ZeroVector;
	float Amount = 0.f;
	float Age = 0.f;
	float Life = 1.f;
	float Punch = 0.f; // 1 right after a hit merged into it, decays to 0
	uint8 Tier = 0;    // 0 normal, 1 crit, 2 huge, 3 heal
};

/**
 * Local-only pooled particles: glowing basic shapes drawn with one instanced mesh per (shape, colour) bucket.
 * One per world, spawned on first use (never on a dedicated server). Pool size: Tuning.csv SparkPool + WeaponFxPool.
 * Also owns the floating damage-number pool the HUD draws. Use through the SQFx helpers below.
 */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQSparks : public AActor
{
	GENERATED_BODY()

public:
	ASQSparks();
	virtual void Tick(float DeltaSeconds) override;

	/** The spark system of this world (spawned on demand). nullptr on dedicated servers / non-game worlds. */
	static ASQSparks* Get(UWorld* World);

	/** bRing: sparks fly outward horizontally in a circle instead of a random upward burst. */
	void Emit(const FVector& Location, const FLinearColor& Color, int32 Count, float Speed, float Lifetime, float Size, float Gravity, bool bRing, float Delay = 0.f);
	void EmitParticle(const FSQFxParticle& P);

	/** Tier: 0 normal, 1 crit, 2 huge, 3 heal. Rapid hits close together merge into one rising number. */
	void AddDamageNumber(const FVector& Location, float Amount, uint8 Tier, float Delay);
	void GetDamageNumbers(TArray<FSQDamageNumberView>& Out) const;

	/** True once the Nov 19 motion kit (SM_FX_Swoosh / Arc / Streak / Wave / Spikes / Ember) is imported: the new swing, shockwave and hit looks need it */
	bool HasMotionKit() { InitPool(); return bHasMotionKit; }

private:
	struct FSpark
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Accel = 0.f;   // Z acceleration (cm/s^2), negative falls, positive rises
		float Drag = 1.f;
		int32 Bucket = INDEX_NONE;
		bool bActive = false;
		bool bLegacy = true; // Burst/Ring look: pop in, shrink out by sqrt
		bool bAlign = false;
		bool bShrink = true;
		bool bEase = false;
		FVector ScaleA = FVector(0.1f);
		FVector ScaleB = FVector(0.1f);
		FQuat Rot = FQuat::Identity;
		FVector SpinAxis = FVector::UpVector;
		float SpinRate = 0.f;
		float SpinAngle = 0.f;
		float FloorZ = -1.0e9f;
		float TrailEvery = 0.f;
		float TrailClock = 0.f;
		float TrailSize = 6.f;
		int32 TrailBucket = INDEX_NONE;
		uint8 DeathFx = 0;
		int32 DeathCount = 0;
		float DeathSize = 0.f;
		int32 DeathBucket = INDEX_NONE;
		float FadeIn = 0.f;
		float FadeOut = 0.f;
		float Bright = 1.f;
		float SpinDecay = 0.f;
		float OrbitRate = 0.f;
		FVector OrbitPivot = FVector::ZeroVector;
	};

	struct FDamageNum
	{
		FVector Pos = FVector::ZeroVector;
		FVector2D Drift = FVector2D::ZeroVector;
		float Amount = 0.f;
		float Age = 0.f;
		float Life = 1.f;
		float Punch = 1.f;
		uint8 Tier = 0;
	};
	struct FPendingNum
	{
		FVector Pos = FVector::ZeroVector;
		float Amount = 0.f;
		float Delay = 0.f;
		uint8 Tier = 0;
	};

	void InitPool();
	int32 BucketFor(const FLinearColor& InColor, ESQFxShape Shape);
	// Style pass dials (Tuning.csv StyleFx*), read once in InitPool
	float FxSizeMul = 1.f;
	float FxCountMul = 1.f;
	float FxGlowMul = 1.f;
	float FxVivid = 0.f;
	float FxFlash = 1.f;
	float FxAdditive = 1.f;
	FSpark& Alloc();
	void EmitLegacy(const FVector& Location, int32 Bucket, int32 Count, float Speed, float Lifetime, float Size, float Gravity, bool bRing, float Delay = 0.f);
	void AddDamageNow(const FVector& Location, float Amount, uint8 Tier);
	void TickDamageNumbers(float DeltaSeconds);

	TArray<FSpark> Sparks;
	int32 Head = 0;
	int32 NumLive = 0;
	TArray<FColor> BucketKeys;
	TArray<uint8> BucketShapes;
	TArray<float> BucketNorm;                  // kit meshes: scale so the longest side is 100 cm (basic shapes: 1)
	TArray<TArray<FTransform>> BucketXforms;   // reused every frame (no per-frame allocation once warm)
	TArray<int32> BucketInstances;             // instance count of each bucket ISM (only grows)
	TArray<bool> BucketWasLive;
	TArray<bool> BucketFades;                  // bucket material reads per-instance custom data 0 as brightness (M_SQGlow2)
	TArray<TArray<float>> BucketAlpha;         // this frame's brightness per instance (fading buckets only), reused
	bool bHasMotionKit = false;                // the Nov 19 kit meshes are imported (else the old segment-built swings and rings are drawn)

	TArray<FDamageNum> DamageNums;
	TArray<FPendingNum> PendingNums;
	int32 MaxDamageNums = 60;
	float HugeDamage = 250.f;

	UPROPERTY() TObjectPtr<USceneComponent> SparkRoot;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> BucketISMs;
};

namespace SQFx
{
	/** Burst of glowing spheres flying out (upper hemisphere). Gravity < 0 falls, > 0 rises (souls). */
	SOULQUOTA_API void Burst(UWorld* World, const FVector& Location, const FLinearColor& Color, int32 Count, float Speed, float Lifetime, float Size, float Gravity = -900.f, float Delay = 0.f);

	/** Flat ring of sparks expanding outward along the ground. */
	SOULQUOTA_API void Ring(UWorld* World, const FVector& Center, const FLinearColor& Color, int32 Count, float Speed, float Lifetime, float Size, float Delay = 0.f);

	/** Any pooled particle (see FSQFxParticle). */
	SOULQUOTA_API void Emit(UWorld* World, const FSQFxParticle& Particle);

	/** A glowing bar from A to B that thins away (beams, lightning, weapon trails). */
	SOULQUOTA_API void Streak(UWorld* World, const FVector& A, const FVector& B, const FLinearColor& Color, float Width, float Life, float Delay = 0.f);

	/** Melee swing: a crescent trail drawn around Center from StartYaw through SweepDeg (deg, + = counter-clockwise seen from above). */
	SOULQUOTA_API void SwingArc(UWorld* World, const FVector& Center, float StartYaw, float SweepDeg, float Radius, const FLinearColor& Color, float Width, float Duration, float Delay = 0.f);

	/** Ground shockwave: a thin ring racing out to Radius. */
	SOULQUOTA_API void Shockwave(UWorld* World, const FVector& Center, float Radius, const FLinearColor& Color, float Duration, float Thickness, float Delay = 0.f);

	/** Short bright ball (muzzle flash, cast flash, impact core). */
	SOULQUOTA_API void Flash(UWorld* World, const FVector& Location, const FLinearColor& Color, float Size, float Life = 0.12f, float Delay = 0.f);

	/** Weapon-coloured hit sparks (bigger with a white core and a ring on crits). */
	SOULQUOTA_API void Impact(UWorld* World, const FVector& Location, const FLinearColor& Color, float Scale, bool bCrit, float Delay = 0.f);

	/** One tapered arc mesh swept from StartYaw through SweepDeg about Center (whips round, eases out, fades). Shape = Swoosh (body) or Arc (thin hot edge). */
	SOULQUOTA_API void Swoosh(UWorld* World, const FVector& Center, float StartYaw, float SweepDeg, float Radius, const FLinearColor& Color, float Thick, float Duration,
		float Delay = 0.f, float Bright = 1.f, float LeanDeg = 0.f, ESQFxShape Shape = ESQFxShape::Swoosh);

	/** True when the Nov 19 motion kit is imported (see ASQSparks::HasMotionKit) */
	SOULQUOTA_API bool HasMotionKit(UWorld* World);
	/** Scale for the Streak kit mesh that gives a needle Length cm long and Width cm wide */
	SOULQUOTA_API FVector StreakSize(float Length, float Width);

	/** Floating number (Tier 0 normal, 1 crit, 2 huge, 3 heal). Cosmetic, merged and capped in ASQSparks. */
	SOULQUOTA_API void DamageNumber(UWorld* World, const FVector& Location, float Amount, uint8 Tier, float Delay = 0.f);

	// --- Animation pass (Nov 9 storyboards: Rise.dc.html, Attack.dc.html). All local-only, call on every machine. ---

	/** Anticipation glint: a four-point star twinkle (blade edge, staff tip). */
	SOULQUOTA_API void Glint(UWorld* World, const FVector& Location, const FLinearColor& Color, float Size, float Delay = 0.f);

	/**
	 * Glowing cracks racing out over the ground from Center to Radius (Count jagged lines), held, then shrinking shut
	 * ("healing") so they are gone at HealAt seconds (counted from now, includes Delay). Rise from the grave, slams.
	 */
	SOULQUOTA_API void GroundCracks(UWorld* World, const FVector& Center, float Radius, const FLinearColor& Color, int32 Count, float HealAt, float Width = 7.f, float Delay = 0.f);

	/** Dirt clods thrown up and out from Center (bounce-stop on the ground at Center.Z). */
	SOULQUOTA_API void DirtClods(UWorld* World, const FVector& Center, int32 Count, float Speed, float Size, float Delay = 0.f);

	/**
	 * Left-click reap (the biggest swing in the game): a giant stepped ghost scythe sweeping the cone (Range, HalfAngleDeg
	 * either side of Dir) toward the cursor, a filled teal smear crescent with a white-hot edge, ground scars, a soul
	 * suction toward the reaper and a closing ring. Starts with a ~0.06 s glint; the blade crosses the cone centre at
	 * about 0.14 s (lines up with SQ_Reap_Swing's impact when PlayReapAnim is called at the same moment).
	 * Respects Tuning ReapFxDetail (particle count scale). Uses the pooled sparks only (no actors).
	 */
	SOULQUOTA_API void ReapSwing(UWorld* World, const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, const FLinearColor& Color);

	/**
	 * A reaped soul flies home: a glowing wisp arcs from From to the reaper (aimed where it will be, from its velocity),
	 * trailing motes, and pops into a small burst + chest flash on arrival. To == nullptr: the soul drifts up and fades.
	 */
	SOULQUOTA_API void ReapSoulTrail(UWorld* World, const FVector& From, ASQCharacter* To);

	/**
	 * Plays the reap swing on the reaper's skinned mesh (SQ_Reap_Swing, then Reaper_Scythe_Swing / SQ_Attack_Slash_R2 /
	 * 2H_Melee_Attack_Slice / 1H_Melee_Attack_Slice_Horizontal), schedules its hit-stop on the impact frame and, for the
	 * owning player, a camera nudge. Returns seconds until the impact frame (Tuning ReapImpactFrac of the clip), or 0 if
	 * nothing played. Local visuals: call on every machine (e.g. from the reap multicast) together with ReapSwing.
	 */
	SOULQUOTA_API float PlayReapAnim(ASQCharacter* Reaper);
}
