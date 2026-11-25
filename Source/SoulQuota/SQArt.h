// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UStaticMesh;
class UAnimSequence;
class UTexture;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UMeshComponent;

/**
 * Imported art (Scripts/import_art.py -> /Game/Art). Every getter returns nullptr when the
 * asset is missing, so callers always keep their primitive fallback.
 *   /Game/Art/Characters/<Model>/SK_<Model>   skeletal mesh
 *   /Game/Art/Characters/<Model>/A_<Anim>     animation (e.g. A_Idle, A_Running_A)
 *   /Game/Art/Props/SM_<name>                 static props (KayKit Halloween Bits)
 *   /Game/Art/Weapons/SM_<name>               hand-held weapons
 *   /Game/Art/Materials/M_ArtTint             Tex * Color, emissive Glow
 */
namespace SQArt
{
	SOULQUOTA_API USkeletalMesh* Character(FName Model);
	/** A_<AnimName> in the model folder; mesh-only models fall back to the folder of the skeleton their mesh uses (cached). */
	SOULQUOTA_API UAnimSequence* Anim(FName Model, FName AnimName);
	SOULQUOTA_API UStaticMesh* Prop(FName Name);
	SOULQUOTA_API UStaticMesh* Weapon(FName Name);
	/** Palette texture for a character model: T_<Model> (next to its mesh or in Art/Textures), else the shared KayKit T_Skeleton. */
	SOULQUOTA_API UTexture* CharacterTexture(FName Model = NAME_None);
	/** Nov 25 2026 skins: T_<Model>_<Tex> (ArtSource/Characters/skins, imported by import_art.py); null when not imported */
	SOULQUOTA_API UTexture* SkinTexture(FName Model, const FString& Tex);
	/** The model actually available: a missing custom model falls back to a KayKit skeleton on the same rig. */
	SOULQUOTA_API FName ResolveModel(FName Model);
	SOULQUOTA_API UTexture* PropTexture();

	/** Tintable art material (falls back to clay when the art material is missing). */
	SOULQUOTA_API UMaterialInstanceDynamic* TintMID(UObject* Outer, UTexture* Tex, const FLinearColor& Color);
	/** Additive glow (Nov 12 2026, /Game/Materials/M_SQGlow from Scripts/create_style_materials.py): unlit light that adds
	 *  to the scene instead of an opaque emissive shape. Soft 1 = bright centre fading to nothing at the silhouette (orbs,
	 *  wisps, halos); Soft 0 = even glow (flat rings / discs seen at a grazing angle). nullptr when the material is missing
	 *  or Tuning StyleGlow is 0: callers keep their old opaque look. */
	SOULQUOTA_API UMaterialInstanceDynamic* GlowMID(UObject* Outer, const FLinearColor& Color, float Glow, float Soft, bool* bOutFades = nullptr);

	/** Apply one material to every slot of a mesh component. */
	SOULQUOTA_API void SetAllMaterials(UMeshComponent* Comp, UMaterialInstanceDynamic* MID);

	/** If the mesh has a material slot whose name contains "Glow" (eyes, lantern), give it a glowing MID. */
	SOULQUOTA_API void ApplyGlowSlots(UMeshComponent* Comp, UObject* Outer, UTexture* Tex, float Glow = 2.5f);

	/** Uniform scale that makes the mesh's bounding-box height equal Height (cm). 1 when unknown. */
	SOULQUOTA_API float FitScale(const USkeletalMesh* Mesh, float Height);
	SOULQUOTA_API float FitScale(const UStaticMesh* Mesh, float Height);
	/** Same idea, but fits the largest horizontal extent. */
	SOULQUOTA_API float FitWidth(const UStaticMesh* Mesh, float Width);

	/** Uniform scale that makes the mesh's longest side equal Length (cm). */
	SOULQUOTA_API float FitLongest(const UStaticMesh* Mesh, float Length);

	/** Yaw (deg) to add so imported characters face +X. Tuning.csv: ArtYawOffset. */
	SOULQUOTA_API float YawOffset(const UObject* WorldContext);

	/** Lerp toward a tint so the texture still reads. */
	SOULQUOTA_API FLinearColor SoftTint(const FLinearColor& Color, float Strength);

	/** Play a looping/one-shot animation if it is not already playing. Returns false if missing. */
	SOULQUOTA_API bool Play(USkeletalMeshComponent* Comp, UAnimSequence* AnimSeq, bool bLoop, float Rate = 1.f);

	/** Find the right-hand socket/bone name on the KayKit rig (dots may be converted on import). */
	SOULQUOTA_API FName HandSocket(const USkeletalMeshComponent* Comp);
}
