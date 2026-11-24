// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQFx.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQData.h"
#include "SQArt.h"
#include "SQFxRecipe.h"
#include "SQPerf.h"
#include "SQInput.h"
#include "SQArena.h"
#include "SQCharacter.h"
#include "SQAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	/** Live ASQFx pulses/telegraphs (all worlds). Each is an actor + MID, so a multicast flood is capped here. */
	int32 GSQLiveFxActors = 0;
	constexpr int32 SQMaxLiveFxActors = 96;
}

ASQFx::ASQFx()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
}

void ASQFx::Spawn(UWorld* World, uint8 InKind, const FVector& Location, float InRadius, const FLinearColor& Color, float InDuration)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	// Telegraphs (Kind 1) warn about danger: always shown. Cosmetic pulses are dropped when too many are alive.
	if (InKind == 0 && GSQLiveFxActors >= SQMaxLiveFxActors) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASQFx* Fx = World->SpawnActor<ASQFx>(ASQFx::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Fx) return;
	Fx->bCountedLive = true;
	++GSQLiveFxActors;
	Fx->Kind = InKind;
	Fx->Radius = FMath::Max(10.f, InRadius);
	Fx->Duration = FMath::Max(0.05f, InDuration);
	Fx->Mesh->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	// Nov 12 2026: area effects were "just a circle" (one flat opaque disc). The disc is now a wash of additive light
	// (M_SQGlow; opaque colour if the material is missing) under layered pooled pieces: see the two blocks below.
	Fx->bDanger = InKind == 1 && Color.R > 0.4f && Color.G < 0.45f && Color.B < 0.35f; // reds / oranges warn
	Fx->GlowMat = SQArt::GlowMID(Fx, Color, InKind == 0 ? 0.9f : 0.3f, 0.f);
	if (Fx->GlowMat) Fx->Mesh->SetMaterial(0, Fx->GlowMat);
	else Fx->Mesh->SetMaterial(0, SQ::ColorMID(Fx, Color));
	Fx->SetActorScale3D(FVector(0.01f, 0.01f, 0.02f));
	Fx->SetLifeSpan(Fx->Duration + 0.1f);

	// Detail drops as more area effects are alive at once (aura ticks, boss barrages): 2 full, 1 light, 0 disc + ring only
	const USQData* D = USQData::Get(World);
	const float Dial = D ? FMath::Clamp(D->Tune(TEXT("StyleAoeDetail"), 1.f), 0.f, 2.f) : 1.f;
	int32 Detail = Dial <= 0.f ? -1 : (GSQLiveFxActors < 10 ? 2 : (GSQLiveFxActors < 36 ? 1 : 0));
	if (Detail < 0) return;
	// At most one full-detail impact every 0.08 s (rapid aura ticks keep the shockwave + flash, skip cracks and shards)
	static double LastFullImpact = -1.0;
	const double Now = World->GetTimeSeconds();
	if (InKind == 0 && Detail >= 2)
	{
		if (Now >= LastFullImpact && Now - LastFullImpact < 0.08) Detail = 1;
		else LastFullImpact = Now;
	}
	const float R = Fx->Radius;
	const float Dur = Fx->Duration;
	const FVector Ground = Location + FVector(0.f, 0.f, 4.f);
	const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.45f);
	if (InKind == 0 && Detail >= 1)
	{
		// Nov 19 2026: the layered impact from FxRecipes.csv (*/Impact) when the motion kit is in; the built-in one below otherwise
		SQFxRecipe::FContext ImpactCtx;
		ImpactCtx.Origin = Location;
		ImpactCtx.Target = Location;
		ImpactCtx.bHasTarget = true;
		ImpactCtx.Radius = R;
		ImpactCtx.Detail = Detail >= 2 ? Dial : 0.4f;
		ImpactCtx.SetColor(Color);
		if (SQFxRecipe::Play(World, FName(TEXT("*")), Detail >= 2 ? (R >= 400.f ? FName(TEXT("ImpactHeavy")) : FName(TEXT("Impact"))) : FName(TEXT("ImpactLight")), ImpactCtx)) return;
	}
	if (InKind == 0)
	{
		// Impact: a bright-edged shockwave racing to the rim, a slower echo ring, a flash, cracks that glow then heal,
		// shards thrown outward and motes rising. Counts scale with the radius.
		const float Thick = FMath::Clamp(R * 0.09f, 10.f, 42.f);
		SQFx::Shockwave(World, Ground, R, Hot, FMath::Max(0.12f, Dur * 0.7f), Thick);
		if (Detail >= 1)
		{
			SQFx::Shockwave(World, Ground, R * 0.72f, Color, FMath::Max(0.16f, Dur), Thick * 0.6f, 0.05f);
			SQFx::Flash(World, Location + FVector(0.f, 0.f, 35.f), Hot, FMath::Clamp(R * 0.45f, 40.f, 240.f), 0.12f);
		}
		if (Detail >= 2 && R >= 120.f)
		{
			SQFx::GroundCracks(World, Ground, R * 0.85f, Color, FMath::Clamp(FMath::RoundToInt(R / 70.f * Dial), 4, 9), Dur + 0.55f, 6.f);
			const int32 Shards = FMath::Clamp(FMath::RoundToInt(R / 32.f * Dial), 5, 14);
			const float Start = FMath::FRandRange(0.f, 2.f * PI);
			for (int32 k = 0; k < Shards; ++k)
			{
				const float Ang = Start + 2.f * PI * (k + FMath::FRandRange(-0.3f, 0.3f)) / Shards;
				const FVector Radial(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
				FSQFxParticle P;
				P.Shape = ESQFxShape::Shard;
				P.Pos = Ground + Radial * (R * 0.2f) + FVector(0.f, 0.f, 10.f);
				P.Vel = Radial * (R * FMath::FRandRange(1.6f, 2.6f)) + FVector(0.f, 0.f, FMath::FRandRange(180.f, 420.f));
				P.Color = k % 3 == 0 ? Hot : Color;
				P.Life = FMath::FRandRange(0.32f, 0.5f);
				P.SizeStart = FVector(FMath::Clamp(R * 0.14f, 16.f, 46.f), 7.f, 7.f);
				P.SizeEnd = FVector(FMath::Clamp(R * 0.05f, 6.f, 16.f), 3.f, 3.f);
				P.Gravity = -900.f;
				P.Drag = 1.5f;
				P.FloorZ = Location.Z;
				P.bAlignToVelocity = true;
				SQFx::Emit(World, P);
			}
			SQFx::Burst(World, Location + FVector(0.f, 0.f, 20.f), Hot, FMath::Clamp(FMath::RoundToInt(R / 45.f * Dial), 4, 10), R * 0.5f, 0.75f, 5.f, 260.f, 0.05f);
		}
	}
	else
	{
		// Telegraph / zone: a crisp rim that holds for the whole warning (solid, so it reads at a glance) with a second
		// rim turning the other way just inside it. The disc itself fills toward the rim in Tick.
		for (int32 RimIdx = 0; RimIdx < (Detail >= 1 ? 2 : 1); ++RimIdx)
		{
			FSQFxParticle Rim;
			Rim.Shape = ESQFxShape::Ring;
			Rim.Pos = Ground + FVector(0.f, 0.f, 3.f + 2.f * RimIdx);
			Rim.Color = RimIdx == 0 ? Hot : Color;
			Rim.Life = Dur;
			const float Dia = 2.f * R * (RimIdx == 0 ? 1.f : 0.9f);
			Rim.SizeStart = FVector(Dia, Dia, 14.f);
			Rim.SizeEnd = Rim.SizeStart;
			Rim.SpinAxis = FVector::UpVector;
			Rim.SpinRate = RimIdx == 0 ? 0.5f : -0.8f;
			Rim.bShrinkOut = false;
			SQFx::Emit(World, Rim);
		}
		if (Detail >= 2 && !Fx->bDanger && R >= 150.f)
		{
			// Friendly zone: a slow rune seal on the floor and a few motes lifting off it
			FSQFxParticle Seal;
			Seal.Shape = ESQFxShape::Seal;
			Seal.Pos = Ground + FVector(0.f, 0.f, 2.f);
			Seal.Color = Color;
			Seal.Life = Dur;
			Seal.SizeStart = FVector(R * 1.3f, R * 1.3f, 10.f);
			Seal.SizeEnd = Seal.SizeStart;
			Seal.SpinRate = 0.35f;
			SQFx::Emit(World, Seal);
			SQFx::Burst(World, Location + FVector(0.f, 0.f, 10.f), Hot, FMath::Clamp(FMath::RoundToInt(R / 60.f), 3, 8), R * 0.4f, FMath::Min(1.2f, Dur), 5.f, 220.f);
		}
	}
}

void ASQFx::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bCountedLive)
	{
		bCountedLive = false;
		GSQLiveFxActors = FMath::Max(0, GSQLiveFxActors - 1);
	}
	Super::EndPlay(EndPlayReason);
}

void ASQFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(Elapsed / Duration, 0.f, 1.f);
	const float Base = Radius / 50.f; // engine cylinder is 100 units wide

	if (Kind == 0)
	{
		// Expanding wash of light that thins out and fades (the shockwave / cracks / shards were emitted in Spawn)
		const float S = Base * FMath::InterpEaseOut(0.2f, 1.f, Alpha, 2.f);
		SetActorScale3D(FVector(S, S, FMath::Lerp(0.04f, 0.005f, Alpha)));
		if (GlowMat) GlowMat->SetScalarParameterValue(TEXT("Glow"), 0.9f * FMath::Square(1.f - Alpha));
	}
	else if (GlowMat)
	{
		if (bDanger)
		{
			// Danger: the fill grows from the middle and reaches the rim exactly at the impact; it brightens as it
			// grows and flickers through the last quarter
			// SETTINGS > ACCESS > Shapes as well as colours: danger fills in five ratchet steps (a motion no friendly zone
			// has), so it reads without the red; Reduce flashes calms the final flicker
			const float FillAlpha = SQInput::ShapeCues() ? FMath::CeilToFloat(Alpha * 5.f) / 5.f : Alpha;
			const float Fill = Base * FMath::Lerp(0.12f, 1.f, FillAlpha);
			SetActorScale3D(FVector(Fill, Fill, 0.01f));
			const float Flicker = (Alpha > 0.75f ? 0.25f * FMath::Abs(FMath::Sin(Elapsed * 30.f)) : 0.f) * SQInput::FlashScale();
			GlowMat->SetScalarParameterValue(TEXT("Glow"), 0.35f + 0.5f * Alpha + Flicker);
		}
		else
		{
			// Friendly zone: a steady pool of light that breathes
			SetActorScale3D(FVector(Base, Base, 0.01f));
			const float FadeOut = FMath::Clamp((1.f - Alpha) * 6.f, 0.f, 1.f);
			GlowMat->SetScalarParameterValue(TEXT("Glow"), (0.22f + 0.07f * FMath::Sin(Elapsed * 4.f)) * FadeOut);
		}
	}
	else
	{
		// Telegraph without the glow material: full size, throbbing thickness (as before)
		const float Throb = 0.01f + 0.01f * FMath::Abs(FMath::Sin(Elapsed * 18.f));
		SetActorScale3D(FVector(Base, Base, Throb));
	}
}

// ================================================================ pooled sparks

namespace
{
	constexpr int32 MaxSparkBuckets = 160;  // Nov 10 2026: 64 -> 160 for the effect-kit shapes x theme colours
	constexpr int32 SparkGrowChunk = 32;
	const FVector HiddenSparkLoc(0.0, 0.0, -100000.0);

	const TCHAR* SQFxShapeMesh(ESQFxShape Shape)
	{
		switch (Shape)
		{
		case ESQFxShape::Cube: return TEXT("Cube");
		case ESQFxShape::Cylinder: return TEXT("Cylinder");
		case ESQFxShape::Cone: return TEXT("Cone");
		default: return TEXT("Sphere");
		}
	}

	const TCHAR* SQFxKitName(ESQFxShape Shape)
	{
		switch (Shape)
		{
		case ESQFxShape::Crescent: return TEXT("Crescent");
		case ESQFxShape::Ring: return TEXT("Ring");
		case ESQFxShape::Flame: return TEXT("Flame");
		case ESQFxShape::Puff: return TEXT("Puff");
		case ESQFxShape::Spiral: return TEXT("Spiral");
		case ESQFxShape::Shard: return TEXT("Shard");
		case ESQFxShape::Bolt: return TEXT("Bolt");
		case ESQFxShape::Star: return TEXT("Star");
		case ESQFxShape::Bat: return TEXT("Bat");
		case ESQFxShape::Crow: return TEXT("Crow");
		case ESQFxShape::Moth: return TEXT("Moth");
		case ESQFxShape::Leaf: return TEXT("Leaf");
		case ESQFxShape::Feather: return TEXT("Feather");
		case ESQFxShape::Rune: return TEXT("Rune");
		case ESQFxShape::Seal: return TEXT("Seal");
		case ESQFxShape::Cleaver: return TEXT("Cleaver");
		case ESQFxShape::Claw: return TEXT("Claw");
		case ESQFxShape::Bandage: return TEXT("Bandage");
		case ESQFxShape::Seed: return TEXT("Seed");
		case ESQFxShape::Drop: return TEXT("Drop");
		case ESQFxShape::Swoosh: return TEXT("Swoosh");
		case ESQFxShape::Arc: return TEXT("Arc");
		case ESQFxShape::Streak: return TEXT("Streak");
		case ESQFxShape::Wave: return TEXT("Wave");
		case ESQFxShape::Spikes: return TEXT("Spikes");
		case ESQFxShape::Ember: return TEXT("Ember");
		case ESQFxShape::Petal: return TEXT("Petal");
		case ESQFxShape::Chevron: return TEXT("Chevron");
		case ESQFxShape::Wisp: return TEXT("Wisp");
		case ESQFxShape::Teeth: return TEXT("Teeth");
		case ESQFxShape::Sigil: return TEXT("Sigil");
		case ESQFxShape::Cog: return TEXT("Cog");
		case ESQFxShape::Web: return TEXT("Web");
		case ESQFxShape::Casket: return TEXT("Casket");
		default: return nullptr;
		}
	}

	/** The mesh for a particle shape: an effect-kit mesh if imported (with its 100 cm normalisation), else the basic shape */
	UStaticMesh* SQFxMeshFor(ESQFxShape Shape, float& OutNorm)
	{
		OutNorm = 1.f;
		if (const TCHAR* Kit = SQFxKitName(Shape))
		{
			const FString Path = FString::Printf(TEXT("/Game/Art/FX/SM_FX_%s.SM_FX_%s"), Kit, Kit);
			if (UStaticMesh* KitMesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				const FVector Ext = KitMesh->GetBoundingBox().GetSize();
				OutNorm = 100.f / FMath::Max(1.f, static_cast<float>(Ext.GetMax()));
				return KitMesh;
			}
			// not imported yet: a sphere stands in, kept a little smaller so the shapes don't read as blobs
			OutNorm = 0.6f;
			return SQ::Mesh(TEXT("Sphere"));
		}
		return SQ::Mesh(SQFxShapeMesh(Shape));
	}
}

ASQSparks::ASQSparks()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = false;
	SparkRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SparkRoot;
}

ASQSparks* ASQSparks::Get(UWorld* World)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer) return nullptr;

	static TArray<TWeakObjectPtr<ASQSparks>> Known; // one per world (PIE can run several worlds)
	for (int32 i = Known.Num() - 1; i >= 0; --i)
	{
		ASQSparks* Existing = Known[i].Get();
		if (!Existing) { Known.RemoveAtSwap(i); continue; }
		if (Existing->GetWorld() == World && !Existing->IsActorBeingDestroyed()) return Existing;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASQSparks* Made = World->SpawnActor<ASQSparks>(ASQSparks::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Made) Known.Add(Made);
	return Made;
}

void ASQSparks::InitPool()
{
	if (Sparks.Num() > 0) return;
	const USQData* D = USQData::Get(this);
	// Hit sparks / soul bursts (SparkPool) + weapon trails, tracers, slabs and rings (WeaponFxPool)
	const float SparkCount = D ? D->Tune(TEXT("SparkPool"), 600.f) : 600.f;
	const float WeaponCount = D ? D->Tune(TEXT("WeaponFxPool"), 1000.f) : 1000.f;
	const int32 Pool = FMath::Clamp(FMath::RoundToInt(SparkCount + WeaponCount), 16, 12000);
	Sparks.SetNum(Pool);
	Head = 0;
	NumLive = 0;
	MaxDamageNums = FMath::Clamp(FMath::RoundToInt(D ? D->Tune(TEXT("DamageNumberCap"), 60.f) : 60.f), 4, 200);
	HugeDamage = FMath::Max(1.f, D ? D->Tune(TEXT("DamageNumberHuge"), 250.f) : 250.f);
	// Style pass (Nov 12 2026): one set of dials over every effect in the game (bigger, more of them, hotter, more colour)
	FxSizeMul = FMath::Clamp(D ? D->Tune(TEXT("StyleFxSize"), 1.3f) : 1.3f, 0.5f, 3.f);
	FxCountMul = FMath::Clamp(D ? D->Tune(TEXT("StyleFxCount"), 1.4f) : 1.4f, 0.5f, 3.f);
	FxGlowMul = FMath::Clamp(D ? D->Tune(TEXT("StyleFxGlow"), 1.f) : 1.f, 0.25f, 4.f);
	FxVivid = FMath::Clamp(D ? D->Tune(TEXT("StyleFxVivid"), 0.35f) : 0.35f, 0.f, 1.f);
	FxFlash = FMath::Clamp(D ? D->Tune(TEXT("StyleFxFlash"), 1.f) : 1.f, 0.f, 3.f);
	FxAdditive = FMath::Clamp(D ? D->Tune(TEXT("StyleFxAdditive"), 1.f) : 1.f, 0.f, 3.f);
	// Nov 19 2026: the motion kit. All six must be there, else the swing / shockwave / hit helpers draw their old looks.
	bHasMotionKit = (D ? D->Tune(TEXT("StyleFxMotionKit"), 1.f) : 1.f) > 0.f;
	for (const TCHAR* Need : { TEXT("Swoosh"), TEXT("Arc"), TEXT("Streak"), TEXT("Wave"), TEXT("Spikes"), TEXT("Ember") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Art/FX/SM_FX_%s.SM_FX_%s"), Need, Need);
		if (!LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) bHasMotionKit = false;
	}
	if (!bHasMotionKit) UE_LOG(LogTemp, Display, TEXT("SQFx: motion kit not imported (run Scripts/setup_project.py): drawing the old swing and ring effects"));
}

int32 ASQSparks::BucketFor(const FLinearColor& InColor, ESQFxShape Shape)
{
	// Style pass: push every effect colour away from grey (StyleFxVivid) before it is bucketed
	FLinearColor Color = InColor;
	if (FxVivid > 0.f)
	{
		const float Lum = 0.3f * Color.R + 0.59f * Color.G + 0.11f * Color.B;
		const float Chroma = 1.f + FxVivid;
		Color = FLinearColor(FMath::Clamp(Lum + (Color.R - Lum) * Chroma, 0.f, 1.f), FMath::Clamp(Lum + (Color.G - Lum) * Chroma, 0.f, 1.f),
			FMath::Clamp(Lum + (Color.B - Lum) * Chroma, 0.f, 1.f), 1.f);
	}
	// Quantise so near-identical colours share one instanced mesh
	const FColor C = Color.ToFColor(true);
	const FColor Key((uint8)(C.R & 0xE0), (uint8)(C.G & 0xE0), (uint8)(C.B & 0xE0), (uint8)255);
	const uint8 ShapeKey = static_cast<uint8>(Shape);
	for (int32 i = 0; i < BucketKeys.Num(); ++i)
	{
		if (BucketKeys[i] == Key && BucketShapes[i] == ShapeKey) return i;
	}

	if (BucketKeys.Num() >= MaxSparkBuckets)
	{
		// Full: reuse the closest existing colour (same shape if there is one)
		int32 Best = 0;
		int32 BestDiff = MAX_int32;
		for (int32 i = 0; i < BucketKeys.Num(); ++i)
		{
			const FColor& K = BucketKeys[i];
			const int32 Diff = FMath::Abs((int32)K.R - (int32)Key.R) + FMath::Abs((int32)K.G - (int32)Key.G) + FMath::Abs((int32)K.B - (int32)Key.B)
				+ (BucketShapes[i] == ShapeKey ? 0 : 2000);
			if (Diff < BestDiff) { BestDiff = Diff; Best = i; }
		}
		return Best;
	}

	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	if (!ISM) return INDEX_NONE;
	ISM->SetMobility(EComponentMobility::Movable);
	float Norm = 1.f;
	ISM->SetStaticMesh(SQFxMeshFor(Shape, Norm));
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(false);

	// Glowing material: art tint over a white texture with emissive Glow (falls back to plain clay colour)
	UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	// Nov 12 2026: glowing shapes are drawn as additive light (M_SQGlow) instead of opaque emissive solids: sparks become
	// soft dots, flames / stars / bolts / swing crescents become see-through glow. Dark colours and "object" shapes
	// (shards, clods, bats, leaves, rings that must read as a hard line) stay solid. StyleFxAdditive 0 = all solid.
	const float ColorLum = 0.3f * Color.R + 0.59f * Color.G + 0.11f * Color.B;
	const bool bLightShape = Shape == ESQFxShape::Sphere || Shape == ESQFxShape::Flame || Shape == ESQFxShape::Star || Shape == ESQFxShape::Spiral
		|| Shape == ESQFxShape::Bolt || Shape == ESQFxShape::Rune || Shape == ESQFxShape::Seal || Shape == ESQFxShape::Crescent
		|| Shape == ESQFxShape::Swoosh || Shape == ESQFxShape::Arc || Shape == ESQFxShape::Streak || Shape == ESQFxShape::Wave || Shape == ESQFxShape::Spikes
		|| Shape == ESQFxShape::Ember || Shape == ESQFxShape::Petal || Shape == ESQFxShape::Chevron || Shape == ESQFxShape::Wisp || Shape == ESQFxShape::Teeth
		|| Shape == ESQFxShape::Sigil || Shape == ESQFxShape::Cog || Shape == ESQFxShape::Web || Shape == ESQFxShape::Casket;
	UMaterialInstanceDynamic* MID = nullptr;
	if (FxAdditive > 0.f && bLightShape && ColorLum > 0.14f)
	{
		// Nov 19 2026: glow pulled down (2.6 / 1.8 -> 1.4 / 1.0) so a coloured effect stays that colour on screen instead of
		// blowing out to white; white is now only what a recipe draws white on purpose (thin hot edges, cores).
		bool bFades = false;
		MID = SQArt::GlowMID(this, Color, FxAdditive * FxGlowMul * (Shape == ESQFxShape::Sphere ? 1.4f : 1.f), Shape == ESQFxShape::Sphere ? 0.8f : 0.f, &bFades);
		if (MID)
		{
			if (bFades) ISM->SetNumCustomDataFloats(1); // per-instance brightness (fade in / out, Bright)
			ISM->SetMaterial(0, MID);
			ISM->SetupAttachment(SparkRoot);
			ISM->RegisterComponent();
			BucketKeys.Add(Key);
			BucketShapes.Add(ShapeKey);
			BucketNorm.Add(Norm);
			BucketISMs.Add(ISM);
			BucketXforms.AddDefaulted();
			BucketInstances.Add(0);
			BucketWasLive.Add(false);
			BucketFades.Add(bFades);
			BucketAlpha.AddDefaulted();
			return BucketKeys.Num() - 1;
		}
	}
	MID = SQArt::TintMID(this, White, Color);
	// kit shapes glow a touch hotter than the basic slabs; smoke (Puff) and silhouettes (Bat / Crow) stay matte so they read as shapes
	const bool bMatte = Shape == ESQFxShape::Puff || Shape == ESQFxShape::Bat || Shape == ESQFxShape::Crow;
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Glow"), bMatte ? 0.6f : FxGlowMul * (Shape == ESQFxShape::Sphere ? 4.f : (SQFxKitName(Shape) ? 3.f : 2.5f)));
		MID->SetScalarParameterValue(TEXT("Rim"), 0.f); // effects glow on their own; the art rim light is for bodies and props
	}
	ISM->SetMaterial(0, MID);
	ISM->SetupAttachment(SparkRoot);
	ISM->RegisterComponent();

	BucketKeys.Add(Key);
	BucketShapes.Add(ShapeKey);
	BucketNorm.Add(Norm);
	BucketISMs.Add(ISM);
	BucketXforms.AddDefaulted();
	BucketInstances.Add(0);
	BucketWasLive.Add(false);
	BucketFades.Add(false);
	BucketAlpha.AddDefaulted();
	return BucketKeys.Num() - 1;
}

ASQSparks::FSpark& ASQSparks::Alloc()
{
	FSpark& S = Sparks[Head];
	Head = (Head + 1) % Sparks.Num();
	if (!S.bActive) NumLive++;
	S = FSpark();
	S.bActive = true;
	return S;
}

void ASQSparks::Emit(const FVector& Location, const FLinearColor& Color, int32 Count, float Speed, float Lifetime, float Size, float Gravity, bool bRing, float Delay)
{
	InitPool();
	if (Count <= 0 || Sparks.Num() == 0) return;
	// With the motion kit in, the everyday spark is a faceted diamond mote (a shape) instead of a ball; half again as tall so it reads at the same size
	if (bHasMotionKit) EmitLegacy(Location, BucketFor(Color, ESQFxShape::Ember), Count, Speed, Lifetime, Size * 1.5f, Gravity, bRing, Delay);
	else EmitLegacy(Location, BucketFor(Color, ESQFxShape::Sphere), Count, Speed, Lifetime, Size, Gravity, bRing, Delay);
}

void ASQSparks::EmitLegacy(const FVector& Location, int32 Bucket, int32 Count, float Speed, float Lifetime, float Size, float Gravity, bool bRing, float Delay)
{
	if (Bucket == INDEX_NONE || Count <= 0 || Sparks.Num() == 0) return;
	const int32 N = FMath::Min(FMath::Max(1, FMath::RoundToInt(static_cast<float>(Count) * FxCountMul * SQPerf::FxScale())), Sparks.Num());
	Size *= FxSizeMul;
	const float StartAngle = FMath::FRandRange(0.f, 2.f * PI);
	// Impact pop: a burst of 5+ sparks also gets one fat, very short flash at its centre (reads as a "hit" even in a crowd)
	if (!bRing && Count >= 5 && FxFlash > 0.f)
	{
		FSpark& Pop = Alloc();
		Pop.Age = -FMath::Max(0.f, Delay);
		Pop.Pos = Location;
		Pop.Vel = FVector::ZeroVector;
		Pop.Life = 0.11f;
		const float PopScale = FMath::Clamp(Size * 3.2f * FxFlash, 14.f, 130.f) / 50.f;
		Pop.ScaleA = Pop.ScaleB = FVector(PopScale);
		Pop.Accel = 0.f;
		Pop.Drag = 0.f;
		Pop.Bucket = Bucket;
		Pop.bLegacy = true;
	}
	for (int32 i = 0; i < N; ++i)
	{
		FVector Dir;
		if (bRing)
		{
			const float A = StartAngle + 2.f * PI * (float)i / (float)N + FMath::FRandRange(-0.15f, 0.15f);
			Dir = FVector(FMath::Cos(A), FMath::Sin(A), 0.12f);
		}
		else
		{
			Dir = FMath::VRand();
			Dir.Z = FMath::Abs(Dir.Z) * 0.8f + 0.25f;
			Dir = Dir.GetSafeNormal();
		}

		FSpark& S = Alloc();
		S.Age = -FMath::Max(0.f, Delay); // hidden (and frozen) until the delay is over
		S.Pos = Location;
		S.Vel = Dir * Speed * FMath::FRandRange(0.65f, 1.f);
		S.Life = FMath::Max(0.05f, Lifetime * FMath::FRandRange(0.75f, 1.15f));
		const float SparkScale = FMath::Max(0.5f, Size * FMath::FRandRange(0.7f, 1.2f)) / 50.f; // engine sphere is 100 wide
		S.ScaleA = S.ScaleB = FVector(SparkScale);
		S.Accel = bRing ? 0.f : Gravity;
		S.Drag = bRing ? 3.f : 1.5f;
		S.Bucket = Bucket;
		S.bLegacy = true;
	}
}

void ASQSparks::EmitParticle(const FSQFxParticle& P)
{
	InitPool();
	if (Sparks.Num() == 0) return;
	const int32 Bucket = BucketFor(P.Color, P.Shape);
	if (Bucket == INDEX_NONE) return;
	FSpark& S = Alloc();
	S.Pos = P.Pos;
	S.Vel = P.Vel;
	S.Age = -FMath::Max(0.f, P.Delay);
	S.Life = FMath::Max(0.02f, P.Life);
	S.Accel = P.Gravity;
	S.Drag = FMath::Max(0.f, P.Drag);
	S.Bucket = Bucket;
	S.bLegacy = false;
	S.bAlign = P.bAlignToVelocity;
	S.bShrink = P.bShrinkOut;
	S.bEase = P.bEaseOut;
	S.ScaleA = P.SizeStart * (FxSizeMul / 100.f); // engine basic shapes are 100 units across
	S.ScaleB = P.SizeEnd * (FxSizeMul / 100.f);
	S.Rot = P.Rot;
	S.SpinAxis = P.SpinAxis.IsNearlyZero() ? FVector::UpVector : P.SpinAxis.GetSafeNormal();
	S.SpinRate = P.SpinRate;
	S.FloorZ = P.FloorZ;
	S.TrailEvery = FMath::Max(0.f, P.TrailEvery);
	S.TrailSize = P.TrailSize;
	S.TrailBucket = S.TrailEvery > 0.f ? BucketFor(P.TrailColor, ESQFxShape::Sphere) : INDEX_NONE;
	S.DeathFx = P.DeathFx;
	S.DeathCount = P.DeathCount;
	S.DeathSize = P.DeathSize;
	S.DeathBucket = P.DeathFx > 0 ? BucketFor(P.DeathColor, ESQFxShape::Sphere) : INDEX_NONE;
	S.FadeIn = FMath::Max(0.f, P.FadeIn);
	S.FadeOut = FMath::Clamp(P.FadeOut, 0.f, 1.f);
	S.Bright = FMath::Max(0.f, P.Bright);
	S.SpinDecay = FMath::Max(0.f, P.SpinDecay);
	S.OrbitRate = P.OrbitRate;
	S.OrbitPivot = P.OrbitPivot;
}

// ---------------------------------------------------------------- floating damage numbers

void ASQSparks::AddDamageNumber(const FVector& Location, float Amount, uint8 Tier, float Delay)
{
	InitPool();
	if (Amount < 0.5f) return;
	if (Delay > 0.01f)
	{
		if (PendingNums.Num() >= 128) PendingNums.RemoveAt(0);
		FPendingNum& Pending = PendingNums.AddDefaulted_GetRef();
		Pending.Pos = Location;
		Pending.Amount = Amount;
		Pending.Delay = Delay;
		Pending.Tier = Tier;
		return;
	}
	AddDamageNow(Location, Amount, Tier);
}

void ASQSparks::AddDamageNow(const FVector& Location, float Amount, uint8 Tier)
{
	const bool bHeal = Tier == 3;
	// Merge rapid hits on the same spot (same enemy) into one number that keeps rising
	for (FDamageNum& Num : DamageNums)
	{
		if ((Num.Tier == 3) != bHeal || Num.Age > 0.55f) continue;
		if (FVector::DistSquared2D(Num.Pos, Location) > 75.f * 75.f || FMath::Abs(Num.Pos.Z - Location.Z) > 160.f) continue;
		Num.Amount += Amount;
		if (!bHeal)
		{
			Num.Tier = FMath::Max(Num.Tier, Tier);
			if (Num.Amount >= HugeDamage) Num.Tier = 2;
		}
		Num.Punch = 1.f;
		Num.Age = FMath::Min(Num.Age, 0.2f);
		return;
	}

	if (DamageNums.Num() >= MaxDamageNums)
	{
		// Full: recycle the oldest
		int32 Oldest = 0;
		for (int32 i = 1; i < DamageNums.Num(); ++i)
		{
			if (DamageNums[i].Age > DamageNums[Oldest].Age) Oldest = i;
		}
		DamageNums.RemoveAtSwap(Oldest);
	}
	FDamageNum& Num = DamageNums.AddDefaulted_GetRef();
	Num.Pos = Location;
	Num.Drift = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * 25.f;
	Num.Amount = Amount;
	Num.Tier = (!bHeal && Amount >= HugeDamage) ? 2 : Tier;
	Num.Life = Num.Tier == 2 ? 1.25f : (Num.Tier == 1 ? 1.05f : 0.85f);
	Num.Age = 0.f;
	Num.Punch = 1.f;
}

void ASQSparks::TickDamageNumbers(float DeltaSeconds)
{
	for (int32 i = PendingNums.Num() - 1; i >= 0; --i)
	{
		PendingNums[i].Delay -= DeltaSeconds;
		if (PendingNums[i].Delay <= 0.f)
		{
			const FPendingNum Ready = PendingNums[i];
			PendingNums.RemoveAt(i);
			AddDamageNow(Ready.Pos, Ready.Amount, Ready.Tier);
		}
	}
	for (int32 i = DamageNums.Num() - 1; i >= 0; --i)
	{
		FDamageNum& Num = DamageNums[i];
		Num.Age += DeltaSeconds;
		Num.Punch = FMath::Max(0.f, Num.Punch - DeltaSeconds * 6.f);
		if (Num.Age >= Num.Life) DamageNums.RemoveAtSwap(i);
	}
}

void ASQSparks::GetDamageNumbers(TArray<FSQDamageNumberView>& Out) const
{
	Out.Reset();
	for (const FDamageNum& Num : DamageNums)
	{
		const float Alpha = FMath::Clamp(Num.Age / Num.Life, 0.f, 1.f);
		// Rise fast, then hang (ease out), drifting a little sideways
		const float Rise = 110.f * (1.f - FMath::Square(1.f - Alpha));
		FSQDamageNumberView& View = Out.AddDefaulted_GetRef();
		View.Pos = Num.Pos + FVector(Num.Drift.X * Alpha, Num.Drift.Y * Alpha, Rise);
		View.Amount = Num.Amount;
		View.Age = Num.Age;
		View.Life = Num.Life;
		View.Punch = Num.Punch;
		View.Tier = Num.Tier;
	}
}

// ---------------------------------------------------------------- tick

void ASQSparks::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickDamageNumbers(DeltaSeconds);
	const int32 NumBuckets = FMath::Min(BucketISMs.Num(), BucketXforms.Num());
	if (NumBuckets == 0) return;

	for (int32 b = 0; b < NumBuckets; ++b)
	{
		BucketXforms[b].Reset();
		if (BucketAlpha.IsValidIndex(b)) BucketAlpha[b].Reset();
	}

	struct FSpawnLater { FVector Pos; int32 Bucket; int32 Count; float Size; uint8 Kind; };
	TArray<FSpawnLater, TInlineAllocator<64>> Later;

	if (NumLive > 0)
	{
		int32 Live = 0;
		for (FSpark& S : Sparks)
		{
			if (!S.bActive) continue;
			S.Age += DeltaSeconds;
			if (S.Age < 0.f) { Live++; continue; } // delayed: not born yet
			if (S.Age >= S.Life || !BucketXforms.IsValidIndex(S.Bucket))
			{
				if (S.DeathFx > 0 && S.DeathBucket != INDEX_NONE && Later.Num() < 64)
				{
					Later.Add({ S.Pos, S.DeathBucket, S.DeathCount, S.DeathSize, S.DeathFx });
				}
				S.bActive = false;
				continue;
			}
			S.Vel.Z += S.Accel * DeltaSeconds;
			S.Vel *= FMath::Max(0.f, 1.f - S.Drag * DeltaSeconds);
			S.Pos += S.Vel * DeltaSeconds;
			if (S.OrbitRate != 0.f)
			{
				// Carried round a vertical axis (orbiting crowns, swirls): position, velocity and facing turn together
				const FQuat Turn(FVector::UpVector, S.OrbitRate * DeltaSeconds);
				S.Pos = S.OrbitPivot + Turn.RotateVector(S.Pos - S.OrbitPivot);
				S.Vel = Turn.RotateVector(S.Vel);
				if (!S.bAlign) S.Rot = Turn * S.Rot;
			}
			if (S.Pos.Z < S.FloorZ)
			{
				// Landed: stop dead (slabs, debris); death effects still fire when the life ends
				S.Pos.Z = S.FloorZ;
				S.Vel = FVector::ZeroVector;
				S.Accel = 0.f;
			}
			const float Alpha = S.Age / S.Life;

			FVector Scale;
			if (S.bLegacy)
			{
				// Pop in fast, shrink out (no per-instance opacity on this material)
				const float Grow = FMath::Min(1.f, Alpha * 8.f);
				Scale = S.ScaleA * (Grow * FMath::Sqrt(FMath::Max(0.f, 1.f - Alpha)));
			}
			else
			{
				const float SizeAlpha = S.bEase ? 1.f - FMath::Square(1.f - Alpha) : Alpha;
				Scale = FMath::Lerp(S.ScaleA, S.ScaleB, SizeAlpha);
				const float PopIn = FMath::Min(1.f, S.Age / 0.03f + 0.25f);
				float ShrinkOut = S.bShrink ? FMath::Clamp((1.f - Alpha) / 0.35f, 0.f, 1.f) : 1.f;
				// A piece that asked to fade but sits in a bucket whose material cannot (M_SQGlow2 missing: setup script not run yet) thins away instead of popping off
				if (!S.bShrink && S.FadeOut > 0.f && !(BucketFades.IsValidIndex(S.Bucket) && BucketFades[S.Bucket]))
				{
					ShrinkOut = FMath::Clamp((1.f - Alpha) / S.FadeOut, 0.f, 1.f);
				}
				Scale *= PopIn * ShrinkOut;

				if (S.SpinDecay > 0.f) S.SpinRate *= FMath::Max(0.f, 1.f - S.SpinDecay * DeltaSeconds);
				S.SpinAngle += S.SpinRate * DeltaSeconds;
				if (S.bAlign && !S.Vel.IsNearlyZero())
				{
					S.Rot = FRotationMatrix::MakeFromX(S.Vel).ToQuat() * FQuat(FVector::ForwardVector, S.SpinAngle);
				}
				else if (S.SpinRate != 0.f)
				{
					S.Rot = S.Rot * FQuat(S.SpinAxis, S.SpinRate * DeltaSeconds);
					S.Rot.Normalize();
				}

				if (S.TrailEvery > 0.f && S.TrailBucket != INDEX_NONE)
				{
					S.TrailClock += DeltaSeconds;
					if (S.TrailClock >= S.TrailEvery && Later.Num() < 64)
					{
						S.TrailClock = 0.f;
						Later.Add({ S.Pos, S.TrailBucket, 1, S.TrailSize, 0 });
					}
				}
			}
			Scale = (Scale * (BucketNorm.IsValidIndex(S.Bucket) ? BucketNorm[S.Bucket] : 1.f)).ComponentMax(FVector(0.001f));
			if (BucketFades.IsValidIndex(S.Bucket) && BucketFades[S.Bucket])
			{
				float Fade = S.bLegacy ? 1.f : S.Bright;
				if (!S.bLegacy)
				{
					if (S.FadeIn > 0.f) Fade *= FMath::Min(1.f, S.Age / S.FadeIn);
					if (S.FadeOut > 0.f) Fade *= FMath::Clamp((1.f - Alpha) / S.FadeOut, 0.f, 1.f);
				}
				BucketAlpha[S.Bucket].Add(Fade);
			}
			BucketXforms[S.Bucket].Add(FTransform(S.Rot, S.Pos, Scale));
			Live++;
		}
		NumLive = Live;
	}

	// Trails and death effects (spawned after the loop so the pool isn't rewritten while we walk it)
	for (const FSpawnLater& Spawn : Later)
	{
		if (Spawn.Kind == 0)
		{
			// Trail puff: a small ball that hangs in place and shrinks
			FSpark& S = Alloc();
			S.Pos = Spawn.Pos;
			S.Vel = FVector(0.f, 0.f, 20.f);
			S.Life = 0.28f;
			S.ScaleA = S.ScaleB = FVector(Spawn.Size / 50.f);
			S.Drag = 2.f;
			S.Accel = 0.f;
			S.Bucket = Spawn.Bucket;
			S.bLegacy = true;
		}
		else
		{
			EmitLegacy(Spawn.Pos, Spawn.Bucket, Spawn.Count, 340.f, 0.32f, Spawn.Size, -800.f, false);
			if (Spawn.Kind == 2) EmitLegacy(Spawn.Pos, Spawn.Bucket, FMath::Max(8, Spawn.Count), 420.f, 0.35f, Spawn.Size * 0.9f, 0.f, true);
		}
	}

	for (int32 b = 0; b < NumBuckets; ++b)
	{
		UInstancedStaticMeshComponent* ISM = BucketISMs[b];
		TArray<FTransform>& Xforms = BucketXforms[b];
		const bool bLive = Xforms.Num() > 0;
		if (!ISM || (!bLive && !BucketWasLive[b])) continue; // idle and already hidden
		BucketWasLive[b] = bLive;

		// Grow the instance count in chunks; unused instances are parked out of sight (no re-creation per frame)
		if (Xforms.Num() > BucketInstances[b])
		{
			const int32 NewCount = FMath::Min(Sparks.Num(), FMath::DivideAndRoundUp(Xforms.Num(), SparkGrowChunk) * SparkGrowChunk);
			for (int32 i = BucketInstances[b]; i < NewCount; ++i)
			{
				ISM->AddInstance(FTransform(FQuat::Identity, HiddenSparkLoc, FVector(0.001f)), true);
			}
			BucketInstances[b] = NewCount;
		}
		const FTransform Hidden(FQuat::Identity, HiddenSparkLoc, FVector(0.001f));
		if (Xforms.Num() > BucketInstances[b]) Xforms.SetNum(BucketInstances[b]);
		while (Xforms.Num() < BucketInstances[b]) Xforms.Add(Hidden);
		if (BucketFades.IsValidIndex(b) && BucketFades[b] && BucketAlpha.IsValidIndex(b))
		{
			// Brightness of each live instance (parked ones are out of sight); set before the transform update below, which dirties the render state
			const TArray<float>& Alphas = BucketAlpha[b];
			const int32 NumAlpha = FMath::Min(Alphas.Num(), BucketInstances[b]);
			for (int32 i = 0; i < NumAlpha; ++i) ISM->SetCustomDataValue(i, 0, Alphas[i], false);
		}
		if (Xforms.Num() > 0)
		{
			ISM->BatchUpdateInstancesTransforms(0, Xforms, true, true, true);
		}
	}
}

// ================================================================ helpers

namespace SQFx
{
	void Burst(UWorld* World, const FVector& Location, const FLinearColor& Color, int32 Count, float Speed, float Lifetime, float Size, float Gravity, float Delay)
	{
		if (ASQSparks* Sys = ASQSparks::Get(World))
		{
			Sys->Emit(Location, Color, Count, Speed, Lifetime, Size, Gravity, false, Delay);
		}
	}

	void Ring(UWorld* World, const FVector& Center, const FLinearColor& Color, int32 Count, float Speed, float Lifetime, float Size, float Delay)
	{
		if (ASQSparks* Sys = ASQSparks::Get(World))
		{
			Sys->Emit(Center, Color, Count, Speed, Lifetime, Size, 0.f, true, Delay);
		}
	}

	void Emit(UWorld* World, const FSQFxParticle& Particle)
	{
		if (ASQSparks* Sys = ASQSparks::Get(World))
		{
			Sys->EmitParticle(Particle);
		}
	}

	bool HasMotionKit(UWorld* World)
	{
		ASQSparks* Sys = ASQSparks::Get(World);
		return Sys && Sys->HasMotionKit();
	}

	void Swoosh(UWorld* World, const FVector& Center, float StartYaw, float SweepDeg, float Radius, const FLinearColor& Color, float Thick, float Duration,
		float Delay, float Bright, float LeanDeg, ESQFxShape Shape)
	{
		// The kit mesh spans 120 degrees with its pivot at the arc centre and its head at +60 (counter-clockwise). It is
		// turned to start at StartYaw and spun about Z through the rest of the sweep; clockwise sweeps flip it over.
		const bool bClockwise = SweepDeg < 0.f;
		FQuat Rot = FRotator(0.f, StartYaw + (bClockwise ? -60.f : 60.f), 0.f).Quaternion();
		if (bClockwise) Rot = Rot * FQuat(FVector::ForwardVector, PI);
		if (LeanDeg != 0.f) Rot = Rot * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(LeanDeg));
		const float Size = Radius * 1.7320508f; // kit size (longest side) for this arc radius
		const float Travel = FMath::DegreesToRadians(FMath::Max(0.f, FMath::Abs(SweepDeg) - 120.f));
		const float Decay = 3.2f;
		FSQFxParticle P;
		P.Pos = Center;
		P.Color = Color;
		P.Shape = Shape;
		P.Life = Duration;
		P.Delay = Delay;
		P.SizeStart = FVector(Size * 0.94f, Size * 0.94f, Size * 0.5f * Thick);
		P.SizeEnd = FVector(Size * 1.05f, Size * 1.05f, Size * 0.2f * Thick);
		P.Rot = Rot;
		P.SpinAxis = FVector::UpVector;
		P.SpinRate = Travel * Decay / FMath::Max(0.05f, 1.f - FMath::Exp(-Decay * Duration * 0.8f));
		P.SpinDecay = Decay;
		P.bShrinkOut = false;
		P.bEaseOut = true;
		P.FadeIn = 0.015f;
		P.FadeOut = 0.5f;
		P.Bright = Bright;
		Emit(World, P);
	}

	FVector StreakSize(float Length, float Width)
	{
		// The Streak kit mesh is 9% as wide as it is long, so the side scale that gives Width cm is Width / 0.09
		return FVector(Length, Width / 0.09f, Width / 0.09f);
	}

	void Streak(UWorld* World, const FVector& A, const FVector& B, const FLinearColor& Color, float Width, float Life, float Delay)
	{
		const FVector Span = B - A;
		const float Len = static_cast<float>(Span.Size());
		if (Len < 1.f) return;
		if (HasMotionKit(World))
		{
			// A needle that is fattest near its head and fades, instead of a stretched ball
			FSQFxParticle N;
			N.Pos = (A + B) * 0.5f;
			N.Color = Color;
			N.Shape = ESQFxShape::Streak;
			N.Life = Life;
			N.Delay = Delay;
			N.Rot = FRotationMatrix::MakeFromX(Span).ToQuat();
			N.SizeStart = StreakSize(Len, Width * 1.6f);
			N.SizeEnd = StreakSize(Len, Width * 0.4f);
			N.bShrinkOut = false;
			N.bEaseOut = true;
			N.FadeOut = 0.6f;
			Emit(World, N);
			return;
		}
		FSQFxParticle P;
		P.Pos = (A + B) * 0.5f;
		P.Color = Color;
		P.Life = Life;
		P.Delay = Delay;
		P.Rot = FRotationMatrix::MakeFromX(Span).ToQuat();
		P.SizeStart = FVector(Len, Width, Width);
		P.SizeEnd = FVector(Len, Width * 0.15f, Width * 0.15f);
		P.bShrinkOut = false;
		P.bEaseOut = true;
		Emit(World, P);
	}

	void SwingArc(UWorld* World, const FVector& Center, float StartYaw, float SweepDeg, float Radius, const FLinearColor& Color, float Width, float Duration, float Delay)
	{
		if (HasMotionKit(World))
		{
			// Nov 19 2026: a layered swing instead of a ring of stretched balls: one tapered coloured body, a thin white-hot
			// edge leading it, and speed streaks thrown off the tip
			const float Thick = FMath::Max(0.5f, Width / 58.f);
			const FLinearColor HotEdge = FMath::Lerp(Color, FLinearColor::White, 0.75f);
			Swoosh(World, Center, StartYaw, SweepDeg, Radius, Color, Thick, Duration, Delay, 1.f);
			Swoosh(World, Center + FVector(0.f, 0.f, 4.f), StartYaw + (SweepDeg >= 0.f ? 14.f : -14.f), SweepDeg, Radius * 1.015f, HotEdge, Thick, Duration * 0.75f, Delay + 0.01f, 1.3f, 0.f, ESQFxShape::Arc);
			constexpr int32 Flecks = 5;
			for (int32 i = 0; i < Flecks; ++i)
			{
				const float Rad = FMath::DegreesToRadians(StartYaw + SweepDeg * (0.35f + 0.6f * i / (Flecks - 1)));
				const FVector Radial(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
				const FVector Tangent = FVector(-FMath::Sin(Rad), FMath::Cos(Rad), 0.f) * (SweepDeg >= 0.f ? 1.f : -1.f);
				FSQFxParticle F;
				F.Pos = Center + Radial * Radius;
				F.Vel = (Tangent * 0.8f + Radial * 0.6f) * 520.f + FVector(0.f, 0.f, FMath::FRandRange(20.f, 120.f));
				F.Drag = 3.f;
				F.Color = HotEdge;
				F.Shape = ESQFxShape::Streak;
				F.Life = 0.22f;
				F.Delay = Delay + Duration * 0.3f * i / Flecks;
				F.SizeStart = StreakSize(FMath::FRandRange(40.f, 70.f), 7.f);
				F.SizeEnd = StreakSize(14.f, 2.5f);
				F.bAlignToVelocity = true;
				F.bShrinkOut = false;
				F.FadeOut = 0.6f;
				Emit(World, F);
			}
			return;
		}
		const int32 Segments = FMath::Clamp(FMath::RoundToInt(FMath::Abs(SweepDeg) / 12.f), 5, 32);
		const float StepDeg = SweepDeg / Segments;
		const float SegLen = 2.f * PI * Radius * FMath::Abs(StepDeg) / 360.f * 1.35f;
		const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.45f);
		for (int32 k = 0; k <= Segments; ++k)
		{
			const float Yaw = StartYaw + StepDeg * k;
			const float Rad = FMath::DegreesToRadians(Yaw);
			const FVector Radial(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
			const float Lead = static_cast<float>(k) / Segments; // the leading edge is thickest
			FSQFxParticle P;
			P.Pos = Center + Radial * Radius;
			P.Vel = Radial * 160.f;
			P.Drag = 4.f;
			P.Color = k == Segments ? Hot : Color;
			P.Delay = Delay + Duration * 0.45f * Lead;
			P.Life = Duration * 0.65f;
			P.Rot = FRotator(0.f, Yaw + (SweepDeg >= 0.f ? 90.f : -90.f), 0.f).Quaternion();
			const float W = Width * (0.55f + 0.45f * Lead);
			P.SizeStart = FVector(SegLen, W, W * 0.3f);
			P.SizeEnd = FVector(SegLen * 0.8f, W * 0.2f, W * 0.1f);
			P.bShrinkOut = false;
			Emit(World, P);
		}
	}

	void Shockwave(UWorld* World, const FVector& Center, float Radius, const FLinearColor& Color, float Duration, float Thickness, float Delay)
	{
		if (HasMotionKit(World))
		{
			// Nov 19 2026: one crisp band racing out and fading, with a fainter coloured echo behind it (was a ring of stretched balls)
			const float WaveR = FMath::Max(40.f, Radius);
			const float WaveDur = FMath::Max(0.08f, Duration);
			const float Tall = FMath::Clamp(Thickness / 26.f, 0.5f, 2.5f);
			FSQFxParticle W;
			W.Shape = ESQFxShape::Wave;
			W.Pos = Center;
			W.Color = FMath::Lerp(Color, FLinearColor::White, 0.45f);
			W.Life = WaveDur;
			W.Delay = Delay;
			W.SizeStart = FVector(WaveR * 0.3f, WaveR * 0.3f, WaveR * 0.6f * Tall);
			W.SizeEnd = FVector(WaveR * 2.f, WaveR * 2.f, WaveR * Tall);
			W.bEaseOut = true;
			W.bShrinkOut = false;
			W.FadeOut = 0.6f;
			W.Bright = 1.1f;
			Emit(World, W);
			W.Pos = Center + FVector(0.f, 0.f, 1.f);
			W.Color = Color;
			W.Life = WaveDur * 1.3f;
			W.Delay = Delay + WaveDur * 0.2f;
			W.SizeStart = FVector(WaveR * 0.2f, WaveR * 0.2f, WaveR * 0.4f * Tall);
			W.SizeEnd = FVector(WaveR * 1.6f, WaveR * 1.6f, WaveR * 0.6f * Tall);
			W.FadeOut = 0.8f;
			W.Bright = 0.5f;
			Emit(World, W);
			return;
		}
		const float R = FMath::Max(40.f, Radius);
		const int32 Pieces = FMath::Clamp(FMath::RoundToInt(R / 22.f), 12, 44);
		const float StartR = R * 0.15f;
		const float Dur = FMath::Max(0.08f, Duration);
		const float Speed = (R - StartR) / Dur;
		const float StartAngle = FMath::FRandRange(0.f, 360.f);
		for (int32 k = 0; k < Pieces; ++k)
		{
			const float Yaw = StartAngle + 360.f * k / Pieces;
			const float Rad = FMath::DegreesToRadians(Yaw);
			const FVector Radial(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
			FSQFxParticle P;
			P.Pos = Center + Radial * StartR;
			P.Vel = Radial * Speed;
			P.Color = Color;
			P.Delay = Delay;
			P.Life = Dur;
			P.Rot = FRotator(0.f, Yaw + 90.f, 0.f).Quaternion();
			const float ArcStart = 2.f * PI * StartR / Pieces * 1.25f;
			const float ArcEnd = 2.f * PI * R / Pieces * 1.25f;
			P.SizeStart = FVector(ArcStart, Thickness, Thickness * 0.45f);
			P.SizeEnd = FVector(ArcEnd, Thickness * 0.35f, Thickness * 0.15f);
			P.bShrinkOut = false;
			Emit(World, P);
		}
	}

	void Flash(UWorld* World, const FVector& Location, const FLinearColor& Color, float Size, float Life, float Delay)
	{
		if (HasMotionKit(World))
		{
			// A soft orb that blooms and fades plus a spinning starburst, so a hit has a shape and not just a ball
			FSQFxParticle Orb;
			Orb.Pos = Location;
			Orb.Color = Color;
			Orb.Life = Life;
			Orb.Delay = Delay;
			Orb.SizeStart = FVector(Size * 0.3f);
			Orb.SizeEnd = FVector(Size * 0.8f);
			Orb.Bright = 0.8f;
			Orb.bEaseOut = true;
			Orb.bShrinkOut = false;
			Orb.FadeOut = 0.7f;
			Emit(World, Orb);
			FSQFxParticle Star;
			Star.Pos = Location;
			Star.Color = FMath::Lerp(Color, FLinearColor::White, 0.5f);
			Star.Shape = ESQFxShape::Spikes;
			Star.Life = Life * 1.15f;
			Star.Delay = Delay;
			Star.SizeStart = FVector(Size * 0.7f);
			Star.SizeEnd = FVector(Size * 2.1f);
			Star.bEaseOut = true;
			Star.bShrinkOut = false;
			Star.FadeOut = 0.6f;
			Star.Bright = 1.1f;
			Star.Rot = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion();
			Star.SpinRate = FMath::FRandRange(-3.f, 3.f);
			Emit(World, Star);
			return;
		}
		FSQFxParticle P;
		P.Pos = Location;
		P.Color = Color;
		P.Life = Life;
		P.Delay = Delay;
		P.SizeStart = FVector(Size * 0.6f);
		P.SizeEnd = FVector(Size * 1.4f);
		P.bEaseOut = true;
		Emit(World, P);
	}

	void Impact(UWorld* World, const FVector& Location, const FLinearColor& Color, float Scale, bool bCrit, float Delay)
	{
		const float S = FMath::Clamp(Scale, 0.3f, 4.f);
		if (HasMotionKit(World))
		{
			// Nov 19 2026 hit: starburst flash, fast streak sparks thrown out, a couple of embers that linger (crits add a tilted gold ring)
			const FLinearColor HotHit = FMath::Lerp(Color, FLinearColor::White, 0.65f);
			Flash(World, Location, HotHit, (bCrit ? 58.f : 36.f) * S, 0.11f, Delay);
			const int32 Sparks = FMath::RoundToInt((bCrit ? 9.f : 5.f) * S);
			for (int32 i = 0; i < Sparks; ++i)
			{
				FVector Dir = FMath::VRand();
				Dir.Z = FMath::Abs(Dir.Z) * 0.7f + 0.2f;
				Dir = Dir.GetSafeNormal();
				FSQFxParticle Sp;
				Sp.Pos = Location;
				Sp.Vel = Dir * FMath::FRandRange(380.f, 640.f) * (bCrit ? 1.2f : 1.f);
				Sp.Drag = 4.5f;
				Sp.Gravity = -500.f;
				Sp.Color = (i % 2) ? Color : HotHit;
				Sp.Shape = ESQFxShape::Streak;
				Sp.Life = FMath::FRandRange(0.16f, 0.26f);
				Sp.Delay = Delay;
				Sp.SizeStart = StreakSize(FMath::FRandRange(34.f, 58.f) * S, 6.f * S);
				Sp.SizeEnd = StreakSize(10.f * S, 2.f * S);
				Sp.bAlignToVelocity = true;
				Sp.bShrinkOut = false;
				Sp.FadeOut = 0.5f;
				Sp.Bright = 1.1f;
				Emit(World, Sp);
			}
			for (int32 i = 0; i < (bCrit ? 3 : 2); ++i)
			{
				FVector Dir = FMath::VRand();
				Dir.Z = FMath::Abs(Dir.Z) + 0.4f;
				FSQFxParticle Em;
				Em.Pos = Location;
				Em.Vel = Dir * FMath::FRandRange(60.f, 150.f);
				Em.Drag = 1.5f;
				Em.Gravity = 120.f;
				Em.Color = Color;
				Em.Shape = ESQFxShape::Ember;
				Em.Life = FMath::FRandRange(0.45f, 0.7f);
				Em.Delay = Delay + 0.04f;
				Em.SizeStart = FVector(9.f * S);
				Em.SizeEnd = FVector(4.f * S);
				Em.SpinRate = 6.f;
				Em.FadeOut = 0.6f;
				Em.Bright = 0.9f;
				Emit(World, Em);
			}
			if (bCrit)
			{
				FSQFxParticle Halo;
				Halo.Pos = Location;
				Halo.Color = FLinearColor(1.f, 0.82f, 0.3f);
				Halo.Shape = ESQFxShape::Wave;
				Halo.Life = 0.26f;
				Halo.Delay = Delay;
				Halo.SizeStart = FVector(20.f * S);
				Halo.SizeEnd = FVector(150.f * S, 150.f * S, 110.f * S);
				Halo.bEaseOut = true;
				Halo.bShrinkOut = false;
				Halo.FadeOut = 0.6f;
				Halo.Rot = FRotator(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(-25.f, 25.f)).Quaternion();
				Emit(World, Halo);
			}
			return;
		}
		if (Delay > 0.01f)
		{
			// Delayed impact (a projectile still in flight): an invisible carrier whose death throws the sparks
			FSQFxParticle Carrier;
			Carrier.Pos = Location;
			Carrier.Color = Color;
			Carrier.Delay = Delay;
			Carrier.Life = 0.02f;
			Carrier.SizeStart = Carrier.SizeEnd = FVector(1.f);
			Carrier.DeathFx = bCrit ? 2 : 1;
			Carrier.DeathCount = FMath::RoundToInt((bCrit ? 10.f : 6.f) * S);
			Carrier.DeathSize = (bCrit ? 8.f : 6.f) * S;
			Carrier.DeathColor = Color;
			Emit(World, Carrier);
			Flash(World, Location, FMath::Lerp(Color, FLinearColor::White, 0.6f), (bCrit ? 55.f : 32.f) * S, 0.1f, Delay);
			return;
		}
		Burst(World, Location, Color, FMath::RoundToInt((bCrit ? 10.f : 6.f) * S), 380.f, 0.3f, (bCrit ? 8.f : 6.f) * S, -800.f);
		Flash(World, Location, FMath::Lerp(Color, FLinearColor::White, 0.6f), (bCrit ? 55.f : 32.f) * S, 0.1f);
		if (bCrit) Ring(World, Location, FLinearColor(1.f, 0.82f, 0.3f), 10, 420.f, 0.3f, 6.f * S);
	}

	void DamageNumber(UWorld* World, const FVector& Location, float Amount, uint8 Tier, float Delay)
	{
		if (ASQSparks* Sys = ASQSparks::Get(World))
		{
			Sys->AddDamageNumber(Location, Amount, Tier, Delay);
		}
	}

	// ================================================================ animation pass (Nov 9 storyboards)

	void Glint(UWorld* World, const FVector& Location, const FLinearColor& Color, float Size, float Delay)
	{
		// Four-point star: two crossed thin bars that flare and shrink, plus a hot core
		const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.7f);
		if (HasMotionKit(World))
		{
			// the kit star, spinning as it flares, over a small core (reads from the chase camera; the bars were flat on the ground)
			FSQFxParticle Tw;
			Tw.Pos = Location;
			Tw.Color = Hot;
			Tw.Shape = ESQFxShape::Star;
			Tw.Life = 0.16f;
			Tw.Delay = Delay;
			Tw.SizeStart = FVector(Size * 0.3f);
			Tw.SizeEnd = FVector(Size * 1.1f);
			Tw.bEaseOut = true;
			Tw.Rot = FRotator(0.f, FMath::FRandRange(0.f, 90.f), 0.f).Quaternion();
			Tw.SpinRate = 5.f;
			Tw.FadeOut = 0.5f;
			Emit(World, Tw);
			FSQFxParticle Core;
			Core.Pos = Location;
			Core.Color = Hot;
			Core.Life = 0.1f;
			Core.Delay = Delay;
			Core.SizeStart = FVector(Size * 0.2f);
			Core.SizeEnd = FVector(Size * 0.5f);
			Core.bEaseOut = true;
			Core.FadeOut = 0.6f;
			Emit(World, Core);
			return;
		}
		for (int32 Bar = 0; Bar < 2; ++Bar)
		{
			FSQFxParticle P;
			P.Pos = Location;
			P.Color = Hot;
			P.Shape = ESQFxShape::Cube;
			P.Delay = Delay;
			P.Life = 0.14f;
			P.Rot = FRotator(0.f, Bar == 0 ? 45.f : -45.f, 0.f).Quaternion(); // flat star, reads from the top-down camera
			P.SizeStart = FVector(Size * 0.25f, 2.f, 2.f);
			P.SizeEnd = FVector(Size, 3.f, 3.f);
			P.bEaseOut = true;
			Emit(World, P);
		}
		Flash(World, Location, Hot, Size * 0.35f, 0.1f, Delay);
	}

	void GroundCracks(UWorld* World, const FVector& Center, float Radius, const FLinearColor& Color, int32 Count, float HealAt, float Width, float Delay)
	{
		if (!ASQSparks::Get(World)) return;
		const int32 Lines = FMath::Clamp(Count, 3, 16);
		const float R = FMath::Max(40.f, Radius);
		const float Race = 0.16f; // the cracks reach the rim this long after Delay
		const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.35f);
		const float StartYaw = FMath::FRandRange(0.f, 360.f);
		for (int32 Line = 0; Line < Lines; ++Line)
		{
			float Yaw = StartYaw + 360.f * Line / Lines + FMath::FRandRange(-10.f, 10.f);
			FVector Prev = Center + FVector(0.f, 0.f, 2.f);
			constexpr int32 Kinks = 4;
			for (int32 Kink = 1; Kink <= Kinks; ++Kink)
			{
				const float Frac = static_cast<float>(Kink) / Kinks;
				Yaw += FMath::FRandRange(-22.f, 22.f);
				const float Rad = FMath::DegreesToRadians(Yaw);
				const FVector Next = Center + FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f) * (R * Frac * FMath::FRandRange(0.85f, 1.05f)) + FVector(0.f, 0.f, 2.f);
				const FVector Span = Next - Prev;
				const float Len = static_cast<float>(Span.Size2D());
				if (Len > 2.f)
				{
					FSQFxParticle P;
					P.Pos = (Prev + Next) * 0.5f;
					P.Color = Kink == 1 ? Hot : Color;
					P.Shape = ESQFxShape::Cube;
					P.Delay = Delay + Race * (Frac - 0.25f);
					// bShrinkOut closes the crack over the last 35% of its life: done at HealAt
					P.Life = FMath::Max(0.2f, HealAt - P.Delay);
					P.Rot = FRotationMatrix::MakeFromX(FVector(Span.X, Span.Y, 0.f)).ToQuat();
					const float W = Width * (1.15f - 0.5f * Frac);
					P.SizeStart = FVector(Len * 0.3f, W * 0.4f, 2.f);
					P.SizeEnd = FVector(Len * 1.1f, W, 2.f);
					P.bEaseOut = true;
					Emit(World, P);
				}
				Prev = Next;
			}
		}
	}

	void DirtClods(UWorld* World, const FVector& Center, int32 Count, float Speed, float Size, float Delay)
	{
		const int32 N = FMath::Clamp(Count, 1, 40);
		for (int32 Clod = 0; Clod < N; ++Clod)
		{
			const float Ang = FMath::DegreesToRadians(FMath::FRandRange(0.f, 360.f));
			const float Out = Speed * FMath::FRandRange(0.35f, 0.8f);
			FSQFxParticle P;
			P.Pos = Center + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * FMath::FRandRange(5.f, 35.f);
			P.Vel = FVector(FMath::Cos(Ang) * Out, FMath::Sin(Ang) * Out, Speed * FMath::FRandRange(0.8f, 1.25f));
			P.Gravity = -1900.f;
			P.FloorZ = Center.Z;
			P.Shape = ESQFxShape::Cube;
			P.Color = FMath::Lerp(FLinearColor(0.30f, 0.21f, 0.13f), FLinearColor(0.42f, 0.33f, 0.22f), FMath::FRand());
			P.Delay = Delay;
			P.Life = FMath::FRandRange(0.6f, 0.9f);
			P.SizeStart = P.SizeEnd = FVector(Size * FMath::FRandRange(0.6f, 1.3f));
			P.SpinAxis = FMath::VRand();
			P.SpinRate = FMath::FRandRange(6.f, 14.f);
			Emit(World, P);
		}
	}

	void ReapSwing(UWorld* World, const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, const FLinearColor& Color)
	{
		if (!ASQSparks::Get(World)) return; // dedicated server / editor world: nothing to draw
		const USQData* D = USQData::Get(World);
		const float Detail = FMath::Clamp(D ? D->Tune(TEXT("ReapFxDetail"), 1.f) : 1.f, 0.25f, 2.f);
		const float Lead = FMath::Clamp(D ? D->Tune(TEXT("ReapSweepLead"), 0.05f) : 0.05f, 0.f, 0.4f);
		const float SweepTime = FMath::Clamp(D ? D->Tune(TEXT("ReapSweepSeconds"), 0.18f) : 0.18f, 0.06f, 0.6f);

		FVector Fwd = Dir.GetSafeNormal2D();
		if (Fwd.IsNearlyZero()) Fwd = FVector::ForwardVector;
		const float Yaw0 = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X)));
		const float Half = FMath::Clamp(HalfAngleDeg, 10.f, 179.f);
		const float R = FMath::Max(100.f, Range);
		const float GroundZ = ASQArena::GroundZAt(World, static_cast<float>(Origin.X), static_cast<float>(Origin.Y));
		const FVector Ground(Origin.X, Origin.Y, GroundZ + 6.f);
		const FVector Pivot(Origin.X, Origin.Y, GroundZ + 70.f); // the scythe sweeps at hip height
		const FLinearColor Ghost = FMath::Lerp(Color, FLinearColor::White, 0.3f);
		const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.75f);
		const FLinearColor Deep = Color * 0.75f;
		auto Radial = [](float YawDeg) -> FVector
		{
			const float Rad = FMath::DegreesToRadians(YawDeg);
			return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
		};

		// 1. Anticipation: a glint where the blade is raised behind the shoulder, a soul flare at the reaper
		Glint(World, Pivot + Radial(Yaw0 + Half + 25.f) * 60.f + FVector(0.f, 0.f, 70.f), Hot, 70.f);
		Flash(World, Pivot, Ghost, 60.f, 0.08f);

		// 2. The giant ghost scythe: stepped (stop-motion) frames sweeping from one side of the cone to the other
		const int32 Steps = FMath::Clamp(FMath::RoundToInt(8.f * Detail), 4, 14);
		const float StepTime = SweepTime / (Steps - 1);
		const float BladeLen = R * 0.42f;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float U = static_cast<float>(Step) / (Steps - 1);
			const float Eased = U * U * (3.f - 2.f * U); // slow out of the wind-up, whips through the middle
			const float Yaw = Yaw0 + Half - 2.f * Half * Eased;
			const FVector Out = Radial(Yaw);
			const FVector Trailing = Radial(Yaw + 90.f); // the blade trails the motion (sweep goes clockwise)
			const float StepDelay = Lead + StepTime * Step;
			const bool bLast = Step == Steps - 1;
			const float FrameLife = bLast ? 0.2f : StepTime * 1.8f;
			const FLinearColor FrameCol = bLast ? Hot : (Step % 2 == 0 ? Ghost : Color);

			// Shaft from the reaper out to the blade root
			const FVector Tip = Pivot + Out * (R * 0.9f);
			{
				FSQFxParticle P;
				P.Pos = (Pivot + Out * 30.f + Tip) * 0.5f;
				P.Color = Deep;
				P.Shape = ESQFxShape::Cube;
				P.Delay = StepDelay;
				P.Life = FrameLife;
				P.Rot = FRotationMatrix::MakeFromX(Out).ToQuat();
				P.SizeStart = FVector(R * 0.88f, 9.f, 9.f);
				P.SizeEnd = FVector(R * 0.88f, 4.f, 4.f);
				P.bShrinkOut = false;
				Emit(World, P);
			}
			// Curved blade: trails behind the shaft tip and hooks back toward the reaper
			constexpr int32 BladeSegs = 4;
			FVector Prev = Tip;
			for (int32 Seg = 1; Seg <= BladeSegs; ++Seg)
			{
				const float S = static_cast<float>(Seg) / BladeSegs;
				const FVector Next = Tip + Trailing * (BladeLen * S) - Out * (BladeLen * 0.55f * S * S);
				const FVector Span = Next - Prev;
				FSQFxParticle P;
				P.Pos = (Prev + Next) * 0.5f;
				P.Color = FrameCol;
				P.Shape = ESQFxShape::Cube;
				P.Delay = StepDelay;
				P.Life = FrameLife;
				P.Rot = FRotationMatrix::MakeFromX(Span).ToQuat();
				const float W = FMath::Lerp(30.f, 7.f, S) * (bLast ? 1.2f : 1.f);
				P.SizeStart = FVector(static_cast<float>(Span.Size()) * 1.15f, W, 5.f);
				P.SizeEnd = FVector(static_cast<float>(Span.Size()), W * 0.4f, 3.f);
				P.bShrinkOut = false;
				Emit(World, P);
				Prev = Next;
			}
			// Edge sparks thrown off the blade tip as it passes
			if (Step % 2 == 1 || bLast)
			{
				FSQFxParticle P;
				P.Pos = Tip;
				P.Vel = Out * FMath::FRandRange(380.f, 620.f) + FVector(0.f, 0.f, FMath::FRandRange(80.f, 220.f));
				P.Gravity = -700.f;
				P.Drag = 1.5f;
				P.Color = Hot;
				P.Delay = StepDelay;
				P.Life = 0.35f;
				P.bAlignToVelocity = true;
				P.SizeStart = FVector(22.f, 6.f, 6.f);
				P.SizeEnd = FVector(10.f, 3.f, 3.f);
				Emit(World, P);
			}
		}

		// 3. The smear: a filled ghost crescent over the whole cone (bands from the inside out), white-hot outer edge
		const float SmearDur = SweepTime / 0.45f; // SwingArc's leading edge lands Duration x 0.45 after its delay
		const int32 Bands = FMath::Clamp(FMath::RoundToInt(4.f * Detail), 2, 6);
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			const float F = (Band + 1.f) / Bands;
			const float BandR = R * FMath::Lerp(0.3f, 0.92f, F);
			SwingArc(World, Pivot - FVector(0.f, 0.f, 10.f * F), Yaw0 + Half, -2.f * Half, BandR, Band % 2 == 0 ? Color : Ghost, R * 0.75f / Bands + 14.f, SmearDur, Lead);
		}
		SwingArc(World, Pivot, Yaw0 + Half, -2.f * Half, R * 0.98f, Hot, 16.f, SmearDur * 1.1f, Lead);
		// Ground scars along the cone that linger a moment
		SwingArc(World, Ground, Yaw0 + Half * 0.9f, -1.8f * Half, R * 0.6f, Color, 12.f, 0.9f, Lead + SweepTime * 0.3f);
		SwingArc(World, Ground, Yaw0 + Half * 0.8f, -1.6f * Half, R * 0.85f, Ghost, 9.f, 1.f, Lead + SweepTime * 0.4f);

		// 4. Impact bloom where the blade crosses the cursor line, plus a bright slash line through it
		const float MidT = Lead + SweepTime * 0.5f;
		Flash(World, Pivot + Fwd * (R * 0.7f), Hot, FMath::Clamp(R * 0.4f, 60.f, 220.f), 0.1f, MidT);
		Streak(World, Pivot + Radial(Yaw0 + 70.f) * (R * 0.5f) + Fwd * (R * 0.35f), Pivot + Radial(Yaw0 - 70.f) * (R * 0.5f) + Fwd * (R * 0.35f), FLinearColor::White, 14.f, 0.12f, MidT);

		// 5. Soul suction: wisps pulled from the swept cone back into the reaper
		const int32 Wisps = FMath::Clamp(FMath::RoundToInt(10.f * Detail), 4, 20);
		const float SuckStart = Lead + SweepTime;
		for (int32 Wisp = 0; Wisp < Wisps; ++Wisp)
		{
			const FVector From = Pivot + Radial(Yaw0 + FMath::FRandRange(-Half, Half)) * (R * FMath::FRandRange(0.45f, 1.f)) + FVector(0.f, 0.f, FMath::FRandRange(-20.f, 40.f));
			const float Flight = FMath::FRandRange(0.22f, 0.34f);
			FSQFxParticle P;
			P.Pos = From;
			P.Vel = (Pivot + FVector(0.f, 0.f, 30.f) - From) / Flight;
			P.Color = Ghost;
			P.Delay = SuckStart + 0.015f * Wisp;
			P.Life = Flight;
			P.SizeStart = FVector(14.f);
			P.SizeEnd = FVector(6.f);
			P.bShrinkOut = false;
			P.TrailEvery = 0.035f;
			P.TrailSize = 5.f;
			P.TrailColor = Color;
			Emit(World, P);
		}

		// 6. Close: a ring snaps out at the feet and the reaper flares as the souls arrive
		Shockwave(World, Ground, R * 0.5f, Color, 0.3f, 30.f, SuckStart);
		Flash(World, Pivot + FVector(0.f, 0.f, 20.f), Hot, 120.f, 0.14f, SuckStart + 0.25f);
		Ring(World, Ground + FVector(0.f, 0.f, 4.f), Ghost, FMath::RoundToInt(12.f * Detail), 420.f, 0.3f, 7.f, SuckStart);
	}

	void ReapSoulTrail(UWorld* World, const FVector& From, ASQCharacter* To)
	{
		if (!ASQSparks::Get(World)) return;
		const FLinearColor Soul(0.36f, 0.95f, 0.84f);
		const FLinearColor Hot = FMath::Lerp(Soul, FLinearColor::White, 0.45f);
		Flash(World, From, Soul, 45.f, 0.1f);

		FSQFxParticle P;
		P.Pos = From;
		P.Color = Hot;
		P.SizeStart = FVector(18.f);
		P.SizeEnd = FVector(10.f);
		P.bShrinkOut = false;
		P.TrailEvery = 0.025f;
		P.TrailSize = 7.f;
		P.TrailColor = Soul;
		if (!To)
		{
			// Nobody to fly to: drift up and fade
			P.Vel = FVector(FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-60.f, 60.f), 260.f);
			P.Gravity = 200.f;
			P.Life = 0.7f;
			P.bShrinkOut = true;
			Emit(World, P);
			return;
		}

		// Ballistic arc that lands on the chest where the reaper will be when the soul arrives
		const FVector Chest = To->GetActorLocation() + FVector(0.f, 0.f, 25.f);
		const float Dist = static_cast<float>(FVector::Dist(From, Chest));
		const float Flight = FMath::Clamp(Dist / 1400.f, 0.28f, 0.7f) * FMath::FRandRange(0.9f, 1.1f);
		const FVector Target = Chest + To->GetVelocity() * Flight;
		const float Gravity = -1400.f;
		FVector Vel = (Target - From) / Flight;
		Vel.Z -= 0.5f * Gravity * Flight; // z(t) = z0 + vz t + g t^2 / 2 must hit Target.Z at t = Flight
		// A little sideways wobble so a cone of souls fans out instead of flying in one line
		const FVector Side = FVector::CrossProduct((Target - From).GetSafeNormal2D(), FVector::UpVector);
		Vel += Side * FMath::FRandRange(-120.f, 120.f);
		P.Vel = Vel;
		P.Gravity = Gravity;
		P.Life = Flight;
		P.DeathFx = 1;
		P.DeathCount = 5;
		P.DeathSize = 5.f;
		P.DeathColor = Hot;
		Emit(World, P);
		Flash(World, Target, Soul, 55.f, 0.1f, Flight); // the reaper's chest glows as it lands
	}

	float PlayReapAnim(ASQCharacter* Reaper)
	{
		if (!Reaper || !Reaper->SkinMesh || Reaper->GetNetMode() == NM_DedicatedServer) return 0.f;
		USQAnimInstance* SkinAnim = Cast<USQAnimInstance>(Reaper->SkinMesh->GetAnimInstance());
		const USQData* D = USQData::Get(Reaper);
		const float ImpactFrac = FMath::Clamp(D ? D->Tune(TEXT("ReapImpactFrac"), 0.4f) : 0.4f, 0.05f, 0.95f);
		const float Rate = FMath::Clamp(D ? D->Tune(TEXT("ReapAnimRate"), 1.25f) : 1.25f, 0.3f, 3.f);

		float ClipSeconds = 0.f;
		if (SkinAnim && Reaper->SkinMesh->IsVisible())
		{
			static const TCHAR* const ReapTakes[] = { TEXT("SQ_Reap_Swing"), TEXT("Reaper_Scythe_Swing"), TEXT("SQ_Attack_Slash_R2"), TEXT("2H_Melee_Attack_Slice"), TEXT("1H_Melee_Attack_Slice_Horizontal") };
			for (const TCHAR* Take : ReapTakes)
			{
				const FName TakeName(Take);
				TArray<FName> One;
				One.Add(TakeName);
				const UAnimSequence* Seq = SkinAnim->FindFirstAnim(One);
				if (!Seq) continue;
				// Priority: the reap may cut an auto-attack straight away; upper body so the legs keep running
				if (SkinAnim->PlayAction(TakeName, Rate, false, 0.05f, true))
				{
					ClipSeconds = static_cast<float>(Seq->GetPlayLength()) / Rate;
				}
				break;
			}
		}
		const float ImpactIn = ClipSeconds > 0.f ? FMath::Clamp(ClipSeconds * ImpactFrac, 0.06f, 0.5f) : 0.14f;
		// The impact frame's hit-stop, squash, weapon pop and the owner's camera nudge run on the reaper's swing timeline
		Reaper->PlayReapJuice(ImpactIn);
		return ClipSeconds > 0.f ? ImpactIn : 0.f;
	}
}
