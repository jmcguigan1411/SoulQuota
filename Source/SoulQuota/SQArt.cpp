// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQArt.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SQData.h"

namespace
{
	template <typename T>
	T* LoadCached(const FString& Path)
	{
		static TMap<FString, TWeakObjectPtr<UObject>> Cache;
		static TSet<FString> Missing;
		if (Missing.Contains(Path)) return nullptr;
		if (const TWeakObjectPtr<UObject>* Found = Cache.Find(Path))
		{
			if (Found->IsValid()) return Cast<T>(Found->Get());
		}
		T* Loaded = LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Loaded) Cache.Add(Path, Loaded);
		else Missing.Add(Path);
		return Loaded;
	}

	FString AssetPath(const FString& Folder, const FString& Name)
	{
		return FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name);
	}
}

namespace SQArt
{
	USkeletalMesh* Character(FName Model)
	{
		if (Model.IsNone()) return nullptr;
		const FString M = Model.ToString();
		return LoadCached<USkeletalMesh>(AssetPath(TEXT("/Game/Art/Characters/") + M, TEXT("SK_") + M));
	}

	UAnimSequence* Anim(FName Model, FName AnimName)
	{
		if (Model.IsNone() || AnimName.IsNone()) return nullptr;
		const FString TakeName = TEXT("A_") + AnimName.ToString();
		if (UAnimSequence* Own = LoadCached<UAnimSequence>(AssetPath(TEXT("/Game/Art/Characters/") + Model.ToString(), TakeName))) return Own;

		// Mesh-only models (Zombie, ZombieBloat, ZombieRunner on the Minion rig, Wraith on the Mage rig) have no takes of
		// their own: look for the take next to the skeleton the mesh uses. Cached per model + take (hits and misses).
		static TMap<FString, TWeakObjectPtr<UAnimSequence>> SkeletonTakeCache;
		static TSet<FString> SkeletonTakeMissing;
		const FString CacheKey = Model.ToString() + TEXT("|") + AnimName.ToString();
		if (SkeletonTakeMissing.Contains(CacheKey)) return nullptr;
		if (const TWeakObjectPtr<UAnimSequence>* CachedTake = SkeletonTakeCache.Find(CacheKey))
		{
			if (CachedTake->IsValid()) return CachedTake->Get();
		}
		UAnimSequence* SkeletonTake = nullptr;
		if (USkeletalMesh* ModelMesh = Character(Model))
		{
			const USkeleton* ModelSkeleton = ModelMesh->GetSkeleton();
			const UPackage* SkeletonPackage = ModelSkeleton ? ModelSkeleton->GetPackage() : nullptr;
			if (SkeletonPackage)
			{
				const FString SkeletonFolder = FPackageName::GetLongPackagePath(SkeletonPackage->GetName());
				const FString OwnFolder = TEXT("/Game/Art/Characters/") + Model.ToString();
				if (!SkeletonFolder.IsEmpty() && SkeletonFolder != OwnFolder)
				{
					SkeletonTake = LoadCached<UAnimSequence>(AssetPath(SkeletonFolder, TakeName));
				}
			}
		}
		// Monster bodies (R_<Id>) share the Reaper rig: their takes live in /Game/Art/Characters/Reaper even if the
		// skeleton lookup above came back empty
		FString TriedFolder = TEXT("(no skeleton)");
		if (USkeletalMesh* ModelMesh = Character(Model))
		{
			const USkeleton* ModelSkeleton = ModelMesh->GetSkeleton();
			TriedFolder = ModelSkeleton ? ModelSkeleton->GetPathName() : FString(TEXT("(mesh has no skeleton)"));
		}
		if (!SkeletonTake && Model.ToString().StartsWith(TEXT("R_")))
		{
			SkeletonTake = LoadCached<UAnimSequence>(AssetPath(TEXT("/Game/Art/Characters/Reaper"), TakeName));
		}
		if (SkeletonTake) SkeletonTakeCache.Add(CacheKey, SkeletonTake);
		else
		{
			SkeletonTakeMissing.Add(CacheKey);
			static TSet<FString> Reported;
			if (!Reported.Contains(Model.ToString()) && (AnimName == FName(TEXT("Walking_A")) || AnimName == FName(TEXT("Idle"))))
			{
				Reported.Add(Model.ToString());
				UE_LOG(LogTemp, Warning, TEXT("SQArt: no take %s for model %s (mesh %s, skeleton %s)"), *TakeName, *Model.ToString(),
					Character(Model) ? TEXT("found") : TEXT("MISSING"), *TriedFolder);
			}
		}
		return SkeletonTake;
	}

	UStaticMesh* Prop(FName Name)
	{
		if (Name.IsNone()) return nullptr;
		return LoadCached<UStaticMesh>(AssetPath(TEXT("/Game/Art/Props"), TEXT("SM_") + Name.ToString()));
	}

	UStaticMesh* Weapon(FName Name)
	{
		if (Name.IsNone()) return nullptr;
		return LoadCached<UStaticMesh>(AssetPath(TEXT("/Game/Art/Weapons"), TEXT("SM_") + Name.ToString()));
	}

	FName ResolveModel(FName Model)
	{
		if (Model.IsNone() || Character(Model)) return Model;
		return Character(FName(TEXT("Mage"))) ? FName(TEXT("Mage")) : Model;
	}

	UTexture* SkinTexture(FName Model, const FString& Tex)
	{
		if (Model.IsNone() || Tex.IsEmpty()) return nullptr;
		const FString TexName = FString::Printf(TEXT("T_%s_%s"), *Model.ToString(), *Tex);
		return LoadCached<UTexture>(AssetPath(TEXT("/Game/Art/Textures"), TexName));
	}

	UTexture* CharacterTexture(FName Model)
	{
		// A model's own palette (T_<Model>, next to its mesh or in Art/Textures: T_Reaper, T_Zombie, T_Gravebat...),
		// else the shared KayKit skeleton texture. Misses are cached by LoadCached, so this stays cheap per call.
		if (!Model.IsNone())
		{
			const FString TexName = TEXT("T_") + Model.ToString();
			if (UTexture* Beside = LoadCached<UTexture>(AssetPath(TEXT("/Game/Art/Characters/") + Model.ToString(), TexName))) return Beside;
			if (UTexture* Own = LoadCached<UTexture>(AssetPath(TEXT("/Game/Art/Textures"), TexName))) return Own;
		}
		return LoadCached<UTexture>(AssetPath(TEXT("/Game/Art/Textures"), TEXT("T_Skeleton")));
	}

	UTexture* PropTexture()
	{
		return LoadCached<UTexture>(AssetPath(TEXT("/Game/Art/Textures"), TEXT("T_HalloweenBits")));
	}

	UMaterialInstanceDynamic* TintMID(UObject* Outer, UTexture* Tex, const FLinearColor& Color)
	{
		// Style pass (Nov 12 2026): M_ArtTintV2 adds a Fresnel rim light (bodies and props pop from the dark ground when
		// seen from behind). Built by Scripts/create_style_materials.py; without it the first M_ArtTint is used.
		UMaterialInterface* Base = LoadCached<UMaterialInterface>(AssetPath(TEXT("/Game/Art/Materials"), TEXT("M_ArtTintV2")));
		const bool bRim = Base != nullptr;
		if (!Base) Base = LoadCached<UMaterialInterface>(AssetPath(TEXT("/Game/Art/Materials"), TEXT("M_ArtTint")));
		if (!Base || !Tex) return SQ::ColorMID(Outer, Color);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		if (MID)
		{
			MID->SetTextureParameterValue(TEXT("Tex"), Tex);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			if (bRim)
			{
				const USQData* D = Outer ? USQData::Get(Outer) : nullptr;
				MID->SetScalarParameterValue(TEXT("Rim"), D ? FMath::Clamp(D->Tune(TEXT("StyleRim"), 0.55f), 0.f, 4.f) : 0.55f);
				MID->SetVectorParameterValue(TEXT("RimColor"), FLinearColor(
					D ? D->Tune(TEXT("StyleRimR"), 0.45f) : 0.45f, D ? D->Tune(TEXT("StyleRimG"), 0.75f) : 0.75f, D ? D->Tune(TEXT("StyleRimB"), 1.f) : 1.f));
			}
		}
		return MID;
	}

	UMaterialInstanceDynamic* GlowMID(UObject* Outer, const FLinearColor& Color, float Glow, float Soft, bool* bOutFades)
	{
		if (bOutFades) *bOutFades = false;
		const USQData* D = Outer ? USQData::Get(Outer) : nullptr;
		const float Dial = D ? D->Tune(TEXT("StyleGlow"), 1.f) : 1.f;
		if (Dial <= 0.f) return nullptr;
		// M_SQGlow2 (Nov 19 2026) = M_SQGlow x per-instance custom data 0 (brightness, default 1): only asked for by instanced
		// effects that set that value, so single meshes keep using M_SQGlow
		UMaterialInterface* Base = bOutFades ? LoadCached<UMaterialInterface>(AssetPath(TEXT("/Game/Materials"), TEXT("M_SQGlow2"))) : nullptr;
		if (Base && bOutFades) *bOutFades = true;
		if (!Base) Base = LoadCached<UMaterialInterface>(AssetPath(TEXT("/Game/Materials"), TEXT("M_SQGlow")));
		if (!Base) return nullptr;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Glow"), Glow * Dial);
			MID->SetScalarParameterValue(TEXT("Soft"), FMath::Clamp(Soft, 0.f, 1.f));
		}
		return MID;
	}

	void SetAllMaterials(UMeshComponent* Comp, UMaterialInstanceDynamic* MID)
	{
		if (!Comp || !MID) return;
		const int32 Num = FMath::Max(1, Comp->GetNumMaterials());
		for (int32 i = 0; i < Num; ++i) Comp->SetMaterial(i, MID);
	}

	void ApplyGlowSlots(UMeshComponent* Comp, UObject* Outer, UTexture* Tex, float Glow)
	{
		if (!Comp) return;
		const TArray<FName> SlotNames = Comp->GetMaterialSlotNames();
		UMaterialInstanceDynamic* GlowMID = nullptr;
		for (const FName& GlowSlot : SlotNames)
		{
			if (!GlowSlot.ToString().Contains(TEXT("Glow"))) continue;
			const int32 SlotIndex = Comp->GetMaterialIndex(GlowSlot);
			if (SlotIndex == INDEX_NONE) continue;
			if (!GlowMID)
			{
				GlowMID = TintMID(Outer, Tex, FLinearColor::White);
				if (GlowMID) GlowMID->SetScalarParameterValue(TEXT("Glow"), Glow);
			}
			if (GlowMID) Comp->SetMaterial(SlotIndex, GlowMID);
		}
	}

	float FitScale(const USkeletalMesh* Mesh, float Height)
	{
		if (!Mesh) return 1.f;
		const float H = Mesh->GetImportedBounds().BoxExtent.Z * 2.f;
		return H > UE_KINDA_SMALL_NUMBER ? Height / H : 1.f;
	}

	float FitScale(const UStaticMesh* Mesh, float Height)
	{
		if (!Mesh) return 1.f;
		const float H = Mesh->GetBounds().BoxExtent.Z * 2.f;
		return H > UE_KINDA_SMALL_NUMBER ? Height / H : 1.f;
	}

	float FitWidth(const UStaticMesh* Mesh, float Width)
	{
		if (!Mesh) return 1.f;
		const FVector E = Mesh->GetBounds().BoxExtent;
		const float W = FMath::Max(E.X, E.Y) * 2.f;
		return W > UE_KINDA_SMALL_NUMBER ? Width / W : 1.f;
	}

	float FitLongest(const UStaticMesh* Mesh, float Length)
	{
		if (!Mesh) return 1.f;
		const FVector E = Mesh->GetBounds().BoxExtent;
		const float L = static_cast<float>(FMath::Max3(E.X, E.Y, E.Z)) * 2.f;
		return L > 0.01f ? Length / L : 1.f;
	}

	float YawOffset(const UObject* WorldContext)
	{
		const USQData* D = USQData::Get(WorldContext);
		return D ? D->Tune(TEXT("ArtYawOffset"), -90.f) : -90.f;
	}

	FLinearColor SoftTint(const FLinearColor& Color, float Strength)
	{
		// Normalise brightness so dark CSV colours still tint rather than blacken the texture
		const float MaxC = FMath::Max3(Color.R, Color.G, Color.B);
		const FLinearColor N = MaxC > 0.01f ? Color / MaxC : FLinearColor::White;
		return FLinearColor(FMath::Lerp(1.f, N.R, Strength), FMath::Lerp(1.f, N.G, Strength), FMath::Lerp(1.f, N.B, Strength), 1.f);
	}

	bool Play(USkeletalMeshComponent* Comp, UAnimSequence* AnimSeq, bool bLoop, float Rate)
	{
		if (!Comp || !AnimSeq) return false;
		if (Comp->GetSingleNodeInstance() && Comp->GetSingleNodeInstance()->GetAnimationAsset() == AnimSeq)
		{
			Comp->SetPlayRate(Rate);
			return true;
		}
		Comp->PlayAnimation(AnimSeq, bLoop);
		Comp->SetPlayRate(Rate);
		return true;
	}

	FName HandSocket(const USkeletalMeshComponent* Comp)
	{
		if (!Comp) return NAME_None;
		static const FName Candidates[] = { FName(TEXT("handslot.r")), FName(TEXT("handslot_r")), FName(TEXT("hand.r")), FName(TEXT("hand_r")) };
		for (const FName& N : Candidates)
		{
			if (Comp->DoesSocketExist(N)) return N;
		}
		return NAME_None;
	}
}
