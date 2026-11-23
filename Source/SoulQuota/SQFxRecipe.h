// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Data-driven effects (Nov 19 2026): Content/Data/FxRecipes.csv. One row = one layer (a verb, a kit shape, a colour role and its
 * timing / size / motion); an effect is every row with the same Id + Event. Used for each reaper's signature-attack accent and
 * E / R casts (Id = reaper id; SQTheme::PlayAttack / PlayCast try this first) and for the shared events (Id = *): Impact, Death,
 * LevelUp, Collect, ChestOpen, LanternLit, LanternSpent, Bank, Dash, Jump, Land. Edit the CSV and replay: no rebuild.
 * Local-only cosmetics drawn with the pooled sparks. Needs the motion kit meshes (SQFx::HasMotionKit); without them Play returns
 * false and the caller draws its old effect. Preview / authoring mirror: blender_work/fx2 (same CSV, same maths).
 */
namespace SQFxRecipe
{
	struct FContext
	{
		FVector Origin = FVector::ZeroVector;   // O: the feet of whoever makes the effect
		FVector Dir = FVector::ForwardVector;   // flat facing / travel direction
		FVector Target = FVector::ZeroVector;   // T: ground target (defaults to Origin + Dir * Radius when bHasTarget is false)
		bool bHasTarget = false;
		float Radius = 300.f;                   // R: reach / area radius
		float Scale = 1.f;                      // K: size multiplier (evolved, crit, ultimate)
		float Beat = 0.f;                       // seconds added to every Delay (signature attacks: time until the blow lands)
		FLinearColor Core = FLinearColor::White;
		FLinearColor Mid = FLinearColor(0.1f, 0.9f, 0.7f);
		FLinearColor Dark = FLinearColor(0.02f, 0.02f, 0.03f);
		FLinearColor Alt = FLinearColor(1.f, 0.65f, 0.15f);
		float Detail = 1.f;                     // particle count scale (crowded scenes pass less)

		/** One colour for everything: Mid = Color, Core = a hot version of it, Alt = Mid, Dark = a deep version */
		void SetColor(const FLinearColor& Color);
	};

	/** True if FxRecipes.csv has rows for this Id + Event and the motion kit is imported */
	SOULQUOTA_API bool Has(UWorld* World, FName Id, FName Event);

	/** Plays every layer of Id + Event. Returns false (and draws nothing) if there are no rows or the kit is missing. */
	SOULQUOTA_API bool Play(UWorld* World, FName Id, FName Event, const FContext& Ctx);

	/** Shared event (Id = *) in one colour at a place */
	SOULQUOTA_API bool PlayShared(UWorld* World, FName Event, const FVector& Origin, const FLinearColor& Color, float Radius = 100.f, float Scale = 1.f, float Detail = 1.f);

	/** Screen shake asked for by effects (Shake rows): Amount 0..1 at a world position. One handler (the local player's camera) may be set. */
	using FShakeFn = void (*)(UWorld* World, const FVector& Where, float Amount);
	SOULQUOTA_API void SetShakeHandler(FShakeFn Handler);
	SOULQUOTA_API void Shake(UWorld* World, const FVector& Where, float Amount);
}
