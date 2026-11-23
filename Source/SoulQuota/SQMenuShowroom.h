// Copyright 2026 Jack McGuigan. All rights reserved.
// Main menu showroom: a graveyard nook at night behind the menu. Your reaper stands by a big lantern among
// gravestones, a dead tree and a fence, soul wisps bob about, the lantern flickers and the camera sways gently.
// Built in code from the KayKit art (SQArt), with primitive shapes when the art isn't imported.
// ASQMenuHUD spawns it at the world origin and looks through its camera.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQMenuShowroom.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UAnimSequence;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS()
class SOULQUOTA_API ASQMenuShowroom : public AActor
{
	GENERATED_BODY()

public:
	ASQMenuShowroom();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Show this reaper (Reapers.csv Id). A different one rises out of the ground (Spawn_Ground_Skeletons). */
	void ShowReaper(FName ReaperId);
	/** The reaper cheers once (picked on the REAPERS page, or clocking in), then idles again. */
	void Celebrate();
	/** REAPERS page: the camera eases in closer to the reaper. */
	void SetCloseUp(bool bOn) { bCloseUp = bOn; }
	FName GetShownReaper() const { return ShownReaper; }

	// ---- Lobby studio (Nov 9): the in-game lobby spawns this set far above the arena and renders it through a scene capture
	// into a render target the HUD draws (big centre-stage preview, and the roster's portrait cards).
	/** Call between SpawnActorDeferred and FinishSpawning. No moonlight (the arena has its own), no far props, a backdrop wall,
	 *  an accent rim light, and a scene capture that renders only this actor into Target. bPortrait = a tight bust shot, captured
	 *  only on CaptureNow(); otherwise it captures every frame. */
	void SetupStudio(UTextureRenderTarget2D* Target, bool bPortrait);
	/** Rim light / soul glow colour (the focused reaper's role accent). */
	void SetAccent(const FLinearColor& Colour);
	/** Show a reaper at once, already standing and idling (no rise, no pop): for portrait captures. */
	void ShowReaperNow(FName ReaperId);
	/** Studio: render one frame into Target now (portraits). Swaps the target first when NewTarget is set. */
	void CaptureNow(UTextureRenderTarget2D* NewTarget = nullptr);
	/** Studio: pause / resume the every-frame capture (the lobby panel is folded away with TAB). */
	void SetCapturing(bool bOn);
	bool IsStudio() const { return bStudio; }

	/** Camera (actor space, cm): wide shot and the REAPERS page close-up. */
	UPROPERTY(EditAnywhere, Category="Showroom")
	FVector CameraSpot = FVector(860.0, -70.0, 190.0);
	UPROPERTY(EditAnywhere, Category="Showroom")
	FVector CameraAim = FVector(0.0, -10.0, 112.0);
	UPROPERTY(EditAnywhere, Category="Showroom")
	FVector CloseSpot = FVector(520.0, -110.0, 150.0);
	UPROPERTY(EditAnywhere, Category="Showroom")
	FVector CloseAim = FVector(0.0, -15.0, 105.0);
	UPROPERTY(EditAnywhere, Category="Showroom")
	float CameraFov = 42.f;
	/** Lobby portrait captures (SetupStudio bPortrait): a still bust shot. */
	UPROPERTY(EditAnywhere, Category="Showroom")
	FVector PortraitSpot = FVector(170.0, 30.0, 150.0);
	UPROPERTY(EditAnywhere, Category="Showroom")
	FVector PortraitAim = FVector(0.0, 0.0, 138.0);
	UPROPERTY(EditAnywhere, Category="Showroom")
	float PortraitFov = 30.f;
	/** Gentle sway (cm) and how long one sway takes (s). Not an orbit. */
	UPROPERTY(EditAnywhere, Category="Showroom")
	float DriftCm = 22.f;
	UPROPERTY(EditAnywhere, Category="Showroom")
	float DriftSeconds = 14.f;
	/** Manual exposure bias (same idea as the Graveyard map's post process volume). */
	UPROPERTY(EditAnywhere, Category="Showroom")
	float ExposureBias = 11.f;
	/** Reaper height on display (cm) and how far it turns from facing the camera (deg). */
	UPROPERTY(EditAnywhere, Category="Showroom")
	float ReaperHeight = 190.f;
	UPROPERTY(EditAnywhere, Category="Showroom")
	float ReaperTurn = 18.f;
	/** Lantern light brightness (unitless, like ASQLantern) before flicker. */
	UPROPERTY(EditAnywhere, Category="Showroom")
	float LanternBrightness = 26000.f;

	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<USceneComponent> ReaperRoot;
	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<USkeletalMeshComponent> ReaperMesh;
	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<UStaticMeshComponent> ReaperWeapon;
	/** Primitive reaper (robe cone + skull sphere) when the art is missing. */
	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<UStaticMeshComponent> FallbackRobe;
	UPROPERTY(VisibleAnywhere, Category="Showroom")
	TObjectPtr<UStaticMeshComponent> FallbackSkull;
	/** Lobby studio only (SetupStudio), else null. */
	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> StudioCapture;

private:
	void BuildSet();
	void BuildLights();
	void BuildWisps();
	/** A static mesh on the set (actor space). Null mesh = nothing. */
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, UMaterialInterface* Mat);
	/** A KayKit prop standing on the ground at Loc, Height cm tall; the primitive Shape (scaled, coloured) when the art is missing. */
	UStaticMeshComponent* AddProp(FName PropName, const FVector& Loc, float Yaw, float Height, FName Shape, const FVector& ShapeScale,
		const FLinearColor& ShapeColour);
	/** An emissive material (art tint over white with Glow; clay colour if the art material is missing). */
	UMaterialInstanceDynamic* GlowMID(const FLinearColor& Colour, float Glow);
	void PlayIdle();
	/** Plays a one-shot animation, then back to Idle. False if the animation is missing. */
	bool PlayOnce(UAnimSequence* AnimSeq);
	FName ReaperModel() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ReaperMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RobeMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlameMID;
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> LanternLight;
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> SoulLight;
	/** Studio: the accent rim light behind the reaper. */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> RimLight;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> StudioTarget;
	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> MoonLight;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Wisps;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> WispLights;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CandleFlames;

	TArray<FVector> WispHome;
	TArray<float> WispPhase;
	FVector FlameSpot = FVector::ZeroVector;

	FName ShownReaper;
	FName ShownModel;
	bool bHasArt = false;
	bool bCloseUp = false;
	float CloseBlend = 0.f;
	float Clock = 0.f;
	/** Seconds left of a one-shot animation (0 = idling), and when the next idle fidget plays. */
	float OneShotLeft = 0.f;
	float NextFidget = 9.f;
	/** When the reaper last changed or cheered (pop + hop). */
	float PopAt = -100.f;
	float CheerAt = -100.f;
	/** Character-select entrance (Nov 10 2026, SQTheme::Entrance): when it started, its mode (SQTheme::EEntrance) and timings */
	float EntranceAt = -100.f;
	float RootYaw = 0.f;
	uint8 EntranceMode = 0;
	float EntranceHide = 0.f;
	float EntranceMove = 0.6f;
	float EntranceDrop = 320.f;
	bool bEntranceHidden = false;
	void StartEntrance(FName ReaperId);
	/** Scale, offset and spin the entrance applies to the reaper this frame (identity once it has arrived) */
	void EntranceTransform(FVector& OutScale, FVector& OutOffset, float& OutYaw);
	float BaseScale = 1.f;
	bool bStudio = false;
	bool bStudioPortrait = false;
	FLinearColor StudioAccent = FLinearColor(0.36f, 0.95f, 0.84f);
	void BuildStudio();
};
