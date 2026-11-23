// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SQRise.generated.h"

class ASQCharacter;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class USQAnimInstance;

/**
 * Rise from the grave (Rise storyboard, Tuning RiseSeconds = 1.6 s; every beat time scales with it):
 *   0.0 s  glowing teal cracks race out in a ring, camera nudge (owner)
 *   0.3 s  the grave mound bulges and soul light leaks from its seams
 *   0.6 s  a bony hand punches up, dirt clods scatter
 *   0.9 s  the earth bursts, a pillar of soul light, the reaper is dragged up
 *   1.2 s  it climbs out and shakes off the dirt
 *   1.6 s  hero pose + the reaper's flourish (Reapers.csv RiseFlourish), name tag pops, the cracks heal shut
 * One replicated, always-relevant actor per reaper (cosmetic only, no gameplay): the server spawns it, every client plays
 * the sequence from the replicated start time. A client that receives it more than Tuning RiseLateJoinSkip seconds late
 * (late joiner, long hitch) skips straight to the end state: the reaper standing, no cracks. Lives RiseSeconds + 1.6 s.
 * Animation: SQ_Rise_FromGrave from 0.0 s (it rises out of the ground itself, Tuning RiseClipFull 1); otherwise
 * Skeletons_Awaken_Floor / Spawn_Ground_Skeletons / Lie_StandUp from 0.9 s while the skinned mesh is lifted from
 * RiseDepth cm below the ground (hidden by the floor until then). Other one-shots are locked out for the duration.
 */
UCLASS(NotPlaceable)
class SOULQUOTA_API ASQRiseFx : public AActor
{
	GENERATED_BODY()

public:
	ASQRiseFx();

	/**
	 * Server (a client call plays a local-only copy): start the rise for Reaper at its current spot and return its
	 * duration (Tuning RiseSeconds). The caller freezes the reaper for that long. Replaces a rise already running for it.
	 */
	static float PlayRise(ASQCharacter* Reaper);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> Reaper;
	/** Server world time (AGameStateBase::GetServerWorldTimeSeconds) the rise started at */
	UPROPERTY(Replicated) float StartServerTime = 0.f;
	UPROPERTY(Replicated) float Duration = 1.6f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	/** Grave mound: a half-buried sphere that inflates, bursts, then lies flat as a dirt rim */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mound;
	/** The bony hand (fallback clips only: SQ_Rise_FromGrave has its own) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hand;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HandArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HandFingerA;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HandFingerB;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HandFingerC;
	/** Name tag that pops at the end (faces the local camera) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> NameTag;

private:
	/** Starts playback Elapsed seconds into the sequence (or skips to the end state when too late) */
	void BeginSequence(float Elapsed);
	void PlayBeat(int32 Beat);
	void UpdateShapes(float DeltaSeconds);
	USQAnimInstance* GetReaperAnim() const;
	void SetSkinDrop(float DownCm);
	void RestoreSkin();
	void PlayFlourish();
	FVector FeetSpot() const;

	bool bBegun = false;
	bool bSkipped = false;
	float Clock = 0.f;        // local sequence time (s)
	int32 NextBeat = 0;
	float Scale = 1.f;        // Duration / 1.6: every beat time is scaled by this
	bool bClipFull = true;    // the rise clip lifts the body out of the ground itself
	bool bSkinDropped = false;
	FVector SkinBase = FVector(0.f, 0.f, -90.f);
	FName RiseClip;
	float RiseClipLength = 0.f;
	float Depth = 200.f;
	float CrackRadius = 170.f;
	float LeakTimer = 0.f;
	float TagStart = -1.f;
	FLinearColor ReaperColor = FLinearColor::White;
	FName FlourishFx;
	TArray<FName> FlourishAnims;
};
