// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Per-monster effect themes (Nov 10 2026). Every playable monster has its own effect language built from the SQFx effect kit
 * (crescents, flames, puffs, runes, bats, crows, bolts...) in its own colours, layered as anticipation -> impact -> linger.
 * Used by: the character-select entrance (ASQMenuShowroom), the rise flourish at mission start (ASQRiseFx), the signature
 * weapon (ASQCharacter::PlayWeaponFx) and E / R casts (USQAbilityComponent::MulticastCast). All local-only cosmetics.
 */
namespace SQTheme
{
	/** How a monster arrives on the character-select screen (ASQMenuShowroom animates the body; Entrance() plays the effects) */
	enum class EEntrance : uint8 { Rise, Poof, Drop, Materialize, Grow, Jolt };

	struct FEntranceSpec
	{
		EEntrance Mode = EEntrance::Rise;
		float HideFor = 0.f;    // Poof: body hidden this long, then pops in
		float MoveFor = 0.6f;   // Rise / Drop / Materialize / Grow: seconds to arrive
		float DropHeight = 320.f;
	};

	SOULQUOTA_API FEntranceSpec Entrance(FName ReaperId);

	/** The entrance effects at the monster's feet (Fwd = toward the camera / facing). Scale 1 = character select. */
	SOULQUOTA_API void PlayEntrance(UWorld* World, FName ReaperId, const FVector& Feet, const FVector& Fwd, float Scale = 1.f);

	/** Smaller version for the hero pose after rising at the start of a mission */
	SOULQUOTA_API void PlayFlourish(UWorld* World, FName ReaperId, const FVector& Feet, const FVector& Fwd);

	/** Accent layer on top of the signature weapon's own effect. Range = the hit radius / reach; ImpactDelay = seconds until the blow lands. */
	SOULQUOTA_API void PlayAttack(UWorld* World, FName ReaperId, const FVector& From, const FVector& Dir, float Range, bool bCrit, bool bEvolved, float ImpactDelay = 0.f);

	/** Accent layer on top of an E / R cast. From = caster's feet, To = ground target, Radius = the ability's area. */
	SOULQUOTA_API void PlayCast(UWorld* World, FName ReaperId, bool bUlt, const FVector& From, const FVector& To, float Radius);

	/** The monster's effect palette: Role 0 = white-hot core, 1 = its colour, 2 = dark edge, 3 = second accent */
	SOULQUOTA_API FLinearColor Colour(FName ReaperId, int32 Role = 1);

	/** True if this monster has a theme (all 21 roster ids) */
	SOULQUOTA_API bool Has(FName ReaperId);
}
