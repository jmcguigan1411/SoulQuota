// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SQInteractCue.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

/** What an interactable is: decides its colour so every kind reads the same way across the map. */
UENUM()
enum class ESQCueKind : uint8
{
	Shrine,     // cyan
	Chest,      // warm gold
	FreeChest,  // bright gold, faster pulse
	Pot,        // soft white
	Portal,     // crimson
	Hazard,     // red-orange warning
	Collect     // green
};

/**
 * Map remaster (Nov 10 2026): one shared "you can use this" look for every interactable, so the player learns it once.
 *   - a glowing segmented ring on the ground (hugs tiers and ramps) over a faint tinted pad: the use area
 *   - a floating diamond above it that bobs and spins: visible from across the screen, colour-coded by kind
 *   - a label that appears when the local reaper walks close (Tuning CueLabelRange), facing the camera
 *   - once used it dims and desaturates (ring grey, diamond and label gone), so spent things stop competing for attention
 * Visual only (never on a dedicated server), no collision, no per-frame allocations.
 */
UCLASS(ClassGroup = (SoulQuota))
class SOULQUOTA_API USQInteractCue : public USceneComponent
{
	GENERATED_BODY()

public:
	USQInteractCue();

	static FLinearColor KindColor(ESQCueKind Kind);

	/** Builds (or rebuilds) the cue: use-area Radius (cm), diamond IconHeight above the owner's feet, near-label text. */
	void Setup(ESQCueKind InKind, float InRadius, float InIconHeight, const FString& InLabel);
	void SetLabel(const FString& InLabel);
	/** Spent: dim, grey, no diamond or label. Cheap to call every frame (only acts on a change). */
	void SetUsed(bool bInUsed);
	/** Ready / armed: faster, brighter pulse */
	void SetUrgent(bool bInUrgent);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	void BuildParts();
	void GroundRing();
	void ApplyColors();

	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RingISM;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pad;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Gem;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	UPROPERTY() TObjectPtr<UTextRenderComponent> LabelShadow;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PadMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GemMID;

	ESQCueKind Kind = ESQCueKind::Pot;
	FString LabelText;
	float Radius = 100.f;
	float IconHeight = 150.f;
	float Clock = 0.f;
	float PhaseOffset = 0.f;
	float LabelRange = 750.f;
	float Brightness = 1.f;
	float NearBlend = 0.f;
	float GroundRetry = 0.f;
	bool bBuilt = false;
	bool bGrounded = false;
	bool bUsed = false;
	bool bUrgent = false;
};
