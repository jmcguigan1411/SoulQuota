// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SQData.h"
#include "SQAnimInstance.generated.h"

class UAnimSequence;
struct FAnimInstanceProxy;
struct FSQAnimInstanceProxy;

/**
 * Native reaper animation (no AnimBlueprint asset). The pose is built in C++ by FSQAnimInstanceProxy:
 *   1. Locomotion: Idle / Walking_A / Running_A blended by speed, walk+run share one synced phase (no foot sliding)
 *   2. Base states (downed / victory / defeat) crossfade over 0.2 s
 *   3. One-shot actions: upper-body (spine up, legs keep running) or full-body, with blend in/out
 *   4. Procedural juice: whole-body lean into acceleration and turns, hip bob scaled by speed
 *   5. Gait personality (FSQGaitProfile, per reaper): bounce, sway, hunch, run lean, arm swing, stride rate,
 *      head bob, limp and hover, layered on the clips in component space and faded with speed
 * Everything here is local visuals: call the setters on every machine.
 */
UCLASS(Transient, NotBlueprintable)
class SOULQUOTA_API USQAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Load the animation set for a KayKit model (Minion/Warrior/Rogue/Mage). Safe to call every frame. */
	void SetAnimSet(FName Model);
	FName GetAnimSetModel() const { return AnimSetModel; }

	/** Feed the movement state once per tick. Mood: 0 normal, 1 victory, 2 defeat. TurnRate in deg/s (+ = turning right). */
	void SetLocomotion(float InSpeed, const FVector& InVelocity, float InTurnRate, bool bInDowned, bool bInCarrying, uint8 InMood);

	/**
	 * Per-character locomotion style. Each list is tried in order and the first clip that exists on this model wins
	 * (e.g. { SQ_Walk_Swagger, Walking_C }); an empty list or no match keeps the model's default clip.
	 */
	void SetStyle(const TArray<FName>& IdleNames, const TArray<FName>& WalkNames, const TArray<FName>& RunNames, FName DebugOwner = NAME_None);

	/**
	 * Ground-speed scale of the clips: a reaper drawn 0.8x as tall takes 0.8x shorter strides, so its cycle must run
	 * 1/0.8x faster at the same speed or the feet skate. Pass the reaper's ArtScale.
	 */
	void SetGroundScale(float InScale) { GroundScale = FMath::Clamp(InScale, 0.3f, 3.f); }

	/** Hit-stop: the current one-shot action (attack) nearly freezes for Seconds, then carries on. Legs keep moving. */
	void HitStop(float Seconds) { PendingHitStop = FMath::Max(PendingHitStop, FMath::Clamp(Seconds, 0.f, 0.2f)); }

	/** Hit-stop that lands Delay seconds from now (the reap swing's impact frame). Local visuals. */
	void HitStopIn(float Delay, float Seconds);

	/**
	 * Anticipation: the next / current one-shot action plays at RateScale (0.2..1) for its first Seconds (a held coil
	 * before the blow). Call right after PlayAction. Local visuals.
	 */
	void WindUp(float Seconds, float RateScale);

	/**
	 * Cinematic lock (the rise from the grave): for Seconds only bPriority PlayAction requests are accepted, so the
	 * spawn clip, flinches, fidgets and attacks can't cut the sequence. Local visuals.
	 */
	void LockActions(float Seconds) { ActionLockLeft = FMath::Max(ActionLockLeft, FMath::Clamp(Seconds, 0.f, 10.f)); }
	bool IsActionLocked() const { return ActionLockLeft > 0.f; }
	/** Ends a lock early (LockActions only ever extends one) */
	void ClearActionLock() { ActionLockLeft = 0.f; }

	/** Real-time seconds from the start of the attack clip to ClipFraction of it, counting a WindUp(WindSeconds, WindRate) */
	static float TimeToClipFraction(float ClipSeconds, float ClipFraction, float WindSeconds, float WindRate);

	/** Per-character procedural gait (neutral profile = clips exactly as authored). */
	void SetGait(const FSQGaitProfile& InGait) { GaitInput = InGait; }

	/** Per-character body proportions (character pass; neutral = the mesh as modelled). Local visuals: call on every machine. */
	void SetBody(const FSQBodyProfile& InBody) { BodyInput = InBody; }

	/** First clip of Names that exists for this model (nullptr if none). */
	UAnimSequence* FindFirstAnim(const TArray<FName>& Names);

	/**
	 * Play a one-shot animation (A_<AnimName>) over the locomotion. bFullBody replaces the whole pose,
	 * otherwise it plays from the spine up (and full-body when standing still). Returns false if the anim is missing
	 * or the request was ignored (a more important action is playing).
	 * bPriority (ability moments): may cut an upper-body attack straight away, and attacks then can't cut it early.
	 */
	bool PlayAction(FName AnimName, float Rate = 1.f, bool bFullBody = false, float BlendIn = 0.08f, bool bPriority = false);

	/** True while a one-shot action is still playing. */
	bool IsActionPlaying() const { return ActionBusyTime > 0.f; }

	/** Took a hit: a short damped wobble of the spine and head (Strength 0..1.5, bigger hits wobble more). Local visuals. */
	void HitReact(float Strength);

	/**
	 * Sliding (every machine, from ASQCharacter::IsSliding): the legs-forward slide pose (Sit_Floor_Pose, falling back to
	 * Sit_Floor_Idle / a held frame of Dodge_Forward / SQ_Walk_Sneak) eases in over the locomotion, the body leans back
	 * (Tuning AnimSlideLeanDegrees) and into turns; gait personality and hip bob fade out. Upper-body attacks still play.
	 */
	void SetSliding(bool bInSliding) { bInputSliding = bInSliding; }

	/**
	 * Airborne (every machine, from the movement component): the SQ_Jump_Air pose strip eases in over the locomotion and is
	 * sampled by vertical speed - launch stretch while rising, tuck at the apex, reach for the ground while falling
	 * (Tuning AnimAirRiseSpeed / AnimAirFallSpeed). Without the take nothing changes. Upper-body attacks still play.
	 */
	void SetAirborne(bool bInAirborne, float InVerticalSpeed) { bInputAirborne = bInAirborne; InputVerticalSpeed = InVerticalSpeed; }

	/**
	 * Top-down facing snap (every machine, from ASQCharacter::UpdateSkinAnimation): 1 while the reaper faces the cursor for a
	 * cast instead of its run. The turn is softened (chest and head lead it, no carving side lean) and, when the facing points
	 * away from the motion, the walk / run cycle plays backwards (a backpedal) instead of moonwalking forward.
	 */
	void SetFacingSnap(float InAlpha) { InputFacingSnap = FMath::Clamp(InAlpha, 0.f, 1.f); }

	/**
	 * Look offset (Remaster 7, local visuals): where the reaper is looking relative to its facing, in degrees (yaw + to its
	 * right, pitch + up). The chest and head turn toward it over locomotion and idles (smoothed, clamped), so the reaper
	 * glances where the camera / aim points instead of staring dead ahead. 0,0 = off.
	 */
	void SetLook(float InYawDeg, float InPitchDeg) { InputLookYaw = FMath::Clamp(InYawDeg, -80.f, 80.f); InputLookPitch = FMath::Clamp(InPitchDeg, -30.f, 30.f); }
	float GetLookYaw() const { return LookYawDeg; }
	float GetLookPitch() const { return LookPitchDeg; }

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FSQAnimInstanceProxy;

	UAnimSequence* FindAnim(FName AnimName);
	/** First of the C-string names that loads for this model (nullptr if none) */
	UAnimSequence* FindFirstNamed(const TCHAR* const* Names, int32 NumNames);

	// Assets are held here (UPROPERTY) so they are never garbage collected while the proxy uses raw pointers
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqIdle;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqWalk;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqRun;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqDowned;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqVictory;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqDefeat;
	/** Slide pose (sampled at SlidePoseTime, held) */
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqSlide;
	/** Jump / fall pose strip (sampled by vertical speed) */
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> SeqAir;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UAnimSequence>> ActionCache;
	// The model's default locomotion (SetStyle falls back to these, so a previous reaper's style never sticks)
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DefaultIdle;
	/** The model's shared state loops (SetAnimSet); SetStyle swaps in the reaper's own SQ_Downed_/SQ_Victory_/SQ_Defeat_<Id> */
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DefaultDowned;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DefaultVictory;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DefaultDefeat;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DefaultWalk;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DefaultRun;

	FName AnimSetModel;
	int32 AnimSetSerial = 0;
	/** True when the model has no idle clip at all: the idle is the first frame of the walk, held still */
	bool bIdleFrozen = false;
	float GroundScale = 1.f;
	float PendingHitStop = 0.f;
	// Attack juice (SQAnimInstance.h HitStopIn / WindUp / LockActions), game thread
	float HitStopDelayLeft = -1.f;
	float HitStopDelayedSeconds = 0.f;
	float PendingWindUp = 0.f;
	float PendingWindRate = 1.f;
	float ActionLockLeft = 0.f;

	// Game-thread inputs (copied into the proxy in PreUpdate)
	float InputSpeed = 0.f;
	FVector InputVelocity = FVector::ZeroVector;
	float InputTurnRate = 0.f;
	bool bInputDowned = false;
	bool bInputCarrying = false;
	uint8 InputMood = 0;
	bool bInputSliding = false;
	bool bInputAirborne = false;
	float InputVerticalSpeed = 0.f;
	float TuneAirRiseSpeed = 720.f;
	float TuneAirFallSpeed = 900.f;
	float SlidePoseTime = 0.f;
	float InputFacingSnap = 0.f;
	float InputLookYaw = 0.f;
	float InputLookPitch = 0.f;
	float LookYawDeg = 0.f;   // smoothed
	float LookPitchDeg = 0.f;
	/** Smoothed facing snap (0..1) and locomotion cycle direction (+1 forward .. -1 backpedal), game thread */
	float FacingSnapAlpha = 0.f;
	float LocoDirection = 1.f;

	// Lean (computed on the game thread in NativeUpdateAnimation, in mesh component space)
	FVector PrevVelocity = FVector::ZeroVector;
	FVector SmoothedAccel = FVector::ZeroVector;
	float SmoothedTurnRate = 0.f;
	FQuat LeanComponent = FQuat::Identity;
	float BobScale = 1.f; // world cm -> component units

	// Turn-in-place (standing and turning: the chest and head lead the turn) and the hit wobble, both game thread
	float TurnTwistDeg = 0.f;
	float HitWobbleAge = 10.f;
	float HitWobbleAmp = 0.f;
	float HitWobbleSide = 1.f;
	float HitWobbleDeg = 0.f;

	// Gait personality (copied into the proxy in PreUpdate) and the character's axes in mesh component space
	FSQGaitProfile GaitInput;
	FSQBodyProfile BodyInput;
	FVector GaitAxisFwd = FVector::ForwardVector;
	FVector GaitAxisRight = FVector::RightVector;
	FVector GaitAxisUp = FVector::UpVector;

	// Tuning (Tuning.csv)
	float TuneWalkSpeed = 250.f;
	float TuneRunSpeed = 550.f;
	float TuneLeanDegrees = 12.f;
	float TuneSlideLeanDegrees = 10.f;

	struct FPendingAction
	{
		UAnimSequence* Seq = nullptr;
		float Rate = 1.f;
		float BlendIn = 0.08f;
		bool bFullBody = false;
	};
	TArray<FPendingAction> PendingActions;

	// Game-thread mirror of the action slot, only used to decide whether a new action may interrupt
	UAnimSequence* BusySeq = nullptr;
	bool bBusyFullBody = false;
	bool bBusyPriority = false;
	float ActionBusyTime = 0.f;  // seconds left of the current action
	float ActionBusyTotal = 0.f; // its full duration
};
