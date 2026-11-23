// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BonePose.h"
#include "BoneContainer.h"
#include "ReferenceSkeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "SQArt.h"
#include "SQData.h"

// ============================================================================ pose helpers (any thread)

namespace SQAnimPose
{
	static float SafeLength(const UAnimSequence* Anim)
	{
		return Anim ? FMath::Max(0.f, static_cast<float>(Anim->GetPlayLength())) : 0.f;
	}

	/** Sample one sequence into Out (reference pose when the sequence is missing). */
	static void Sample(FPoseContext& Out, const UAnimSequence* Anim, float Length, float Time, bool bLoop)
	{
		if (!Anim || Length <= UE_KINDA_SMALL_NUMBER)
		{
			Out.Pose.ResetToRefPose();
			return;
		}
		float LocalTime = bLoop ? FMath::Fmod(Time, Length) : FMath::Clamp(Time, 0.f, Length);
		if (LocalTime < 0.f) LocalTime += Length;

		FAnimExtractContext ExtractContext;
		ExtractContext.CurrentTime = LocalTime;
		ExtractContext.bExtractRootMotion = false;
		FAnimationPoseData PoseData(Out);
		Anim->GetAnimationPose(PoseData, ExtractContext);
	}

	/** Out = lerp(Out, Other, Alpha) per bone (local space). */
	static void BlendInto(FCompactPose& Out, const FCompactPose& Other, float Alpha)
	{
		if (Alpha <= 0.001f || Out.GetNumBones() != Other.GetNumBones()) return;
		const bool bCopy = Alpha >= 0.999f;
		for (const FCompactPoseBoneIndex BoneIndex : Out.ForEachBoneIndex())
		{
			if (bCopy) Out[BoneIndex] = Other[BoneIndex];
			else Out[BoneIndex].BlendWith(Other[BoneIndex], Alpha);
		}
	}

	/** Component-space transform of one bone, accumulated up its parent chain. */
	static FTransform ComponentSpaceOf(const FCompactPose& Pose, FCompactPoseBoneIndex BoneIndex)
	{
		FTransform Result = Pose[BoneIndex];
		FCompactPoseBoneIndex ParentIndex = Pose.GetParentBoneIndex(BoneIndex);
		int32 Guard = 0;
		while (ParentIndex.IsValid() && Guard++ < 256)
		{
			Result = Result * Pose[ParentIndex];
			ParentIndex = Pose.GetParentBoneIndex(ParentIndex);
		}
		return Result;
	}

	/** Rotate a bone about its own pivot by a rotation expressed in component space. */
	static void RotateInComponentSpace(FCompactPose& Pose, FCompactPoseBoneIndex BoneIndex, const FQuat& Delta)
	{
		const FCompactPoseBoneIndex ParentIndex = Pose.GetParentBoneIndex(BoneIndex);
		const FQuat ParentRot = ParentIndex.IsValid() ? ComponentSpaceOf(Pose, ParentIndex).GetRotation() : FQuat::Identity;
		FTransform& Local = Pose[BoneIndex];
		FQuat NewRot = ParentRot.Inverse() * Delta * ParentRot * Local.GetRotation();
		NewRot.Normalize();
		Local.SetRotation(NewRot);
	}

	/** Component-space rotation that tips Up toward Dir by Degrees (negative tips away). Same construction as the lean. */
	static FQuat TiltToward(const FVector& Up, const FVector& Dir, float Degrees)
	{
		if (FMath::Abs(Degrees) < 0.01f) return FQuat::Identity;
		const float Radians = FMath::DegreesToRadians(FMath::Clamp(Degrees, -80.f, 80.f));
		const FVector Tipped = (Up + Dir * FMath::Tan(Radians)).GetSafeNormal();
		return Tipped.IsNearlyZero() ? FQuat::Identity : FQuat::FindBetweenNormals(Up, Tipped);
	}

	/** Move a bone by an offset expressed in component space. */
	static void TranslateInComponentSpace(FCompactPose& Pose, FCompactPoseBoneIndex BoneIndex, const FVector& Offset)
	{
		const FCompactPoseBoneIndex ParentIndex = Pose.GetParentBoneIndex(BoneIndex);
		const FVector LocalOffset = ParentIndex.IsValid() ? ComponentSpaceOf(Pose, ParentIndex).InverseTransformVector(Offset) : Offset;
		FTransform& Local = Pose[BoneIndex];
		Local.SetTranslation(Local.GetTranslation() + LocalOffset);
	}
}

// ============================================================================ proxy

enum class ESQAnimBase : uint8 { Locomotion, Downed, Victory, Defeat };

/** Builds the reaper pose in C++. PreUpdate runs on the game thread; Evaluate may run on a worker thread. */
struct FSQAnimInstanceProxy : public FAnimInstanceProxy
{
	FSQAnimInstanceProxy() {}
	explicit FSQAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	struct FActionSlot
	{
		const UAnimSequence* Anim = nullptr;
		float Length = 0.f;
		float Rate = 1.f;
		float Elapsed = 0.f;  // real seconds since start
		float Duration = 0.f; // Length / Rate
		float BlendIn = 0.08f;
		float BlendOut = 0.15f;
		bool bFullBody = false;
		bool bActive = false;
		bool bFading = false; // interrupted: fades out from FadeFrom
		float FadeFrom = 0.f;
		float FadeScale = 1.f;

		float Weight() const
		{
			if (!bActive || !Anim) return 0.f;
			// Eased (smoothstep) in, out and interrupt fades: no linear "pop" at either end of a blend
			if (bFading) return FadeFrom * FMath::SmoothStep(0.f, 1.f, FadeScale);
			const float In = BlendIn > 0.f ? FMath::SmoothStep(0.f, 1.f, Elapsed / BlendIn) : 1.f;
			const float OutW = BlendOut > 0.f ? FMath::SmoothStep(0.f, 1.f, (Duration - Elapsed) / BlendOut) : 1.f;
			return In * OutW;
		}

		void Advance(float Dt)
		{
			if (!bActive) return;
			Elapsed += Dt;
			if (bFading)
			{
				FadeScale -= Dt / 0.16f;
				if (FadeScale <= 0.f) bActive = false;
			}
			if (Elapsed >= Duration) bActive = false;
		}
	};

	void EvaluateBase(ESQAnimBase State, float StateClock, FPoseContext& Out);
	void EvaluateLocomotion(FPoseContext& Out);
	void ApplyAction(const FActionSlot& Slot, FPoseContext& Out);
	void RefreshBoneCache(const FCompactPose& Pose);
	void Interrupt(FActionSlot& Slot);
	// Gait personality (FSQGaitProfile)
	void ApplyGaitArms(FCompactPose& Pose);
	void ApplyGaitBody(FCompactPose& Pose);
	void RefreshArmNeutral();
	void MeasureArmNeutral(const UAnimSequence* Clip, float ClipLength, FQuat& OutLeft, FQuat& OutRight);

	// Sequences (kept alive by USQAnimInstance UPROPERTYs)
	const UAnimSequence* AnimIdle = nullptr;
	const UAnimSequence* AnimWalk = nullptr;
	const UAnimSequence* AnimRun = nullptr;
	const UAnimSequence* AnimDowned = nullptr;
	const UAnimSequence* AnimVictory = nullptr;
	const UAnimSequence* AnimDefeat = nullptr;
	float LenIdle = 0.f, LenWalk = 0.f, LenRun = 0.f, LenDowned = 0.f, LenVictory = 0.f, LenDefeat = 0.f;
	int32 SeenSerial = -1;

	// Slide pose layer (eased in over locomotion while sliding)
	const UAnimSequence* AnimSlide = nullptr;
	float LenSlide = 0.f;
	float SlideSampleTime = 0.f;
	float SlideAlpha = 0.f;       // 0..1, eased in fast, out a little slower
	float SlideLeanDeg = 10.f;    // lean back while sliding

	// Air pose layer (eased in over locomotion while airborne; the strip is sampled by vertical speed)
	const UAnimSequence* AnimAir = nullptr;
	float LenAir = 0.f;
	float AirSampleTime = 0.f;
	float AirAlpha = 0.f;

	// Locomotion
	float SmoothSpeed = 0.f;
	float IdleClock = 0.f;
	float LocoPhase = 0.f; // 0..1 shared by walk and run so the feet stay in step
	float WalkAlpha = 0.f; // idle -> moving
	float RunAlpha = 0.f;  // walk -> run
	float RunRefSpeed = 550.f;
	float SamplePhase = 0.f; // LocoPhase after the limp warp: what the walk/run clips are sampled at
	float LocoDir = 1.f;     // +1 forward .. -1 backpedal (USQAnimInstance::LocoDirection)
	bool bIdleIsFrozenWalk = false; // no idle clip on this model: hold the walk's first frame instead of walking in place
	float HitStopLeft = 0.f;        // seconds the current action stays (almost) frozen
	float WindUpLeft = 0.f;         // seconds the current action still plays slowed (attack anticipation)
	float WindUpRate = 1.f;

	// Base state crossfade
	ESQAnimBase CurState = ESQAnimBase::Locomotion;
	ESQAnimBase PrevState = ESQAnimBase::Locomotion;
	float CurClock = 0.f;
	float PrevClock = 0.f;
	float StateBlend = 1.f; // 1 = fully in CurState

	// One-shot actions (current + the one it interrupted, fading out)
	FActionSlot CurAction;
	FActionSlot PrevAction;

	// Procedural
	FQuat LeanQuat = FQuat::Identity;
	float LeanAlpha = 0.f;
	float BobUnits = 1.f;
	float TurnTwist = 0.f;   // deg, standing turn (chest / head lead)
	float HitWobble = 0.f;   // deg, damped hit wobble
	float HitSide = 1.f;
	float LookYaw = 0.f;     // deg, smoothed look offset (chest + head)
	float LookPitch = 0.f;

	// Gait personality and the character axes in component space (from the game thread)
	FSQGaitProfile GaitProfile;
	FVector GaitFwd = FVector::ForwardVector;
	FVector GaitRight = FVector::RightVector;
	FVector GaitUp = FVector::UpVector;
	// Upper-arm "hanging" rotation of the walk / run clips (average of two opposite phases): ArmSwing scales around it
	const UAnimSequence* ArmNeutralWalkClip = nullptr;
	const UAnimSequence* ArmNeutralRunClip = nullptr;
	bool bArmNeutralValid = false;
	FQuat ArmNeutralWalkL = FQuat::Identity, ArmNeutralWalkR = FQuat::Identity;
	FQuat ArmNeutralRunL = FQuat::Identity, ArmNeutralRunR = FQuat::Identity;

	// Bone cache (built from the mesh reference skeleton)
	const FReferenceSkeleton* CachedRefSkel = nullptr;
	int32 CachedRefNum = -1;
	bool bBoneCacheDirty = true;
	TArray<float> MeshUpperWeight;
	int32 MeshHips = INDEX_NONE;
	int32 MeshSpine = INDEX_NONE;
	TArray<float> CompactUpperWeight;
	int32 CompactHips = INDEX_NONE;
	int32 CompactSpine = INDEX_NONE;
	// Extra bones for the gait layer (INDEX_NONE when the rig has no such bone)
	int32 MeshChestBone = INDEX_NONE, MeshHeadBone = INDEX_NONE, MeshArmL = INDEX_NONE, MeshArmR = INDEX_NONE;
	int32 CompactChest = INDEX_NONE, CompactHead = INDEX_NONE, CompactArmL = INDEX_NONE, CompactArmR = INDEX_NONE;
};

void FSQAnimInstanceProxy::Interrupt(FActionSlot& Slot)
{
	if (!Slot.bActive || Slot.bFading) return;
	Slot.FadeFrom = Slot.Weight();
	Slot.FadeScale = 1.f;
	Slot.bFading = true;
}

void FSQAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	USQAnimInstance* Inst = Cast<USQAnimInstance>(InAnimInstance);
	if (!Inst) return;
	const float Dt = FMath::Clamp(DeltaSeconds, 0.f, 0.1f);

	if (Inst->AnimSetSerial != SeenSerial)
	{
		SeenSerial = Inst->AnimSetSerial;
		CurAction = FActionSlot();
		PrevAction = FActionSlot();
		bBoneCacheDirty = true;
	}

	AnimIdle = Inst->SeqIdle.Get();
	AnimWalk = Inst->SeqWalk.Get();
	AnimRun = Inst->SeqRun.Get();
	AnimDowned = Inst->SeqDowned.Get();
	AnimVictory = Inst->SeqVictory.Get();
	AnimDefeat = Inst->SeqDefeat.Get();
	LenIdle = SQAnimPose::SafeLength(AnimIdle);
	LenWalk = SQAnimPose::SafeLength(AnimWalk);
	LenRun = SQAnimPose::SafeLength(AnimRun);
	LenDowned = SQAnimPose::SafeLength(AnimDowned);
	LenVictory = SQAnimPose::SafeLength(AnimVictory);
	LenDefeat = SQAnimPose::SafeLength(AnimDefeat);
	AnimSlide = Inst->SeqSlide.Get();
	LenSlide = SQAnimPose::SafeLength(AnimSlide);
	SlideSampleTime = FMath::Clamp(Inst->SlidePoseTime, 0.f, LenSlide);
	SlideLeanDeg = FMath::Clamp(Inst->TuneSlideLeanDegrees, -30.f, 45.f);
	AnimAir = Inst->SeqAir.Get();
	LenAir = SQAnimPose::SafeLength(AnimAir);

	// --- Locomotion weights and synced phase
	// Reference speeds scale with the reaper's size: smaller legs need a quicker cycle at the same ground speed
	const float GroundScaleNow = FMath::Clamp(Inst->GroundScale, 0.3f, 3.f);
	const float WalkRef = FMath::Max(50.f, Inst->TuneWalkSpeed * GroundScaleNow);
	RunRefSpeed = FMath::Max(WalkRef + 50.f, Inst->TuneRunSpeed * GroundScaleNow);
	bIdleIsFrozenWalk = Inst->bIdleFrozen;
	const bool bCarrying = Inst->bInputCarrying;
	// Faster to settle when stopping than when starting: the feet stop with the body instead of walking on the spot
	const float WantSpeed = FMath::Max(0.f, Inst->InputSpeed);
	SmoothSpeed = FMath::FInterpTo(SmoothSpeed, WantSpeed, Dt, WantSpeed < SmoothSpeed ? 16.f : 10.f);

	WalkAlpha = FMath::Clamp((SmoothSpeed - 20.f) / (WalkRef * 0.5f - 20.f), 0.f, 1.f);
	RunAlpha = (bCarrying || !AnimRun || AnimRun == AnimWalk) ? 0.f : FMath::Clamp((SmoothSpeed - WalkRef) / (RunRefSpeed - WalkRef), 0.f, 1.f);

	// Playback rate follows ground speed (feet stay planted); clamps only stop extreme speeds looking silly
	const float WalkRate = FMath::Clamp(SmoothSpeed / WalkRef, 0.5f, 1.6f) * (bCarrying ? 0.75f : 1.f);
	const float RunRate = FMath::Clamp(SmoothSpeed / RunRefSpeed, 0.6f, 1.9f);
	const float WalkCycles = LenWalk > UE_KINDA_SMALL_NUMBER ? WalkRate / LenWalk : 0.f;
	const float RunCycles = LenRun > UE_KINDA_SMALL_NUMBER ? RunRate / LenRun : WalkCycles;
	GaitProfile = Inst->GaitInput;
	GaitFwd = Inst->GaitAxisFwd;
	GaitRight = Inst->GaitAxisRight;
	GaitUp = Inst->GaitAxisUp;
	LocoDir = FMath::Clamp(Inst->LocoDirection, -1.f, 1.f);
	// Stride: same ground speed, quicker short steps (>1) or long lazy strides (<1)
	// (softened to 60%: a faster cadence over the same clip stride reads as skating when pushed too far)
	const float StrideRate = FMath::Lerp(1.f, FMath::Clamp(GaitProfile.Stride, 0.5f, 2.f), 0.6f);
	const float Cycles = FMath::Lerp(WalkCycles > 0.f ? WalkCycles : RunCycles, RunCycles, RunAlpha) * StrideRate;
	// LocoDir < 0 = backpedal (top-down cast facing away from the run): the cycle runs backwards (+1 keeps Fmod positive)
	if (WalkAlpha > 0.f) LocoPhase = FMath::Fmod(LocoPhase + Cycles * Dt * LocoDir + 1.f, 1.f);
	// Limp: warp the phase so one half of the cycle (one leg's step) is slow and the other quick. Monotonic for K < 1
	const float LimpK = FMath::Clamp(GaitProfile.Limp, 0.f, 1.f) * 0.35f;
	SamplePhase = LocoPhase;
	if (LimpK > 0.f)
	{
		SamplePhase = LocoPhase - LimpK / (2.f * PI) * FMath::Sin(2.f * PI * LocoPhase);
		SamplePhase = FMath::Fmod(SamplePhase + 1.f, 1.f);
	}
	IdleClock += Dt;

	// --- Base state with crossfade
	ESQAnimBase Want = ESQAnimBase::Locomotion;
	if (Inst->InputMood == 2) Want = ESQAnimBase::Defeat;
	else if (Inst->bInputDowned) Want = ESQAnimBase::Downed;
	else if (Inst->InputMood == 1) Want = ESQAnimBase::Victory;

	if (Want != CurState)
	{
		PrevState = CurState;
		PrevClock = CurClock;
		CurState = Want;
		CurClock = 0.f;
		StateBlend = 0.f;
		// Upper-body layers only make sense over locomotion
		if (Want != ESQAnimBase::Locomotion && !CurAction.bFullBody) Interrupt(CurAction);
	}
	CurClock += Dt;
	PrevClock += Dt;
	StateBlend = FMath::Min(1.f, StateBlend + Dt / 0.25f);

	// --- New actions from the game thread
	for (const USQAnimInstance::FPendingAction& Pending : Inst->PendingActions)
	{
		if (!Pending.Seq) continue;
		if (CurState != ESQAnimBase::Locomotion && !Pending.bFullBody) continue;
		Interrupt(CurAction);
		if (CurAction.bActive) PrevAction = CurAction;

		FActionSlot NewSlot;
		NewSlot.Anim = Pending.Seq;
		NewSlot.Length = SQAnimPose::SafeLength(Pending.Seq);
		NewSlot.Rate = FMath::Max(0.05f, Pending.Rate);
		NewSlot.Duration = NewSlot.Length / NewSlot.Rate;
		NewSlot.BlendIn = FMath::Max(0.f, Pending.BlendIn);
		// Blend out over a quarter of the clip (short attacks snap back, long full-body moves settle softly)
		NewSlot.BlendOut = FMath::Clamp(NewSlot.Length / FMath::Max(0.05f, Pending.Rate) * 0.25f, 0.1f, Pending.bFullBody ? 0.28f : 0.2f);
		NewSlot.bFullBody = Pending.bFullBody;
		NewSlot.bActive = NewSlot.Duration > 0.f;
		CurAction = NewSlot;
	}
	Inst->PendingActions.Reset();

	// Hit-stop: the attack holds its impact pose for a moment (the legs and the fading old action keep going)
	HitStopLeft = FMath::Max(HitStopLeft, Inst->PendingHitStop);
	Inst->PendingHitStop = 0.f;
	// Anticipation: the fresh attack coils slowly for its first frames (USQAnimInstance::WindUp)
	if (Inst->PendingWindUp > 0.f)
	{
		WindUpLeft = Inst->PendingWindUp;
		WindUpRate = Inst->PendingWindRate;
		Inst->PendingWindUp = 0.f;
	}
	const float ActionDt = HitStopLeft > 0.f ? Dt * 0.08f : (WindUpLeft > 0.f ? Dt * WindUpRate : Dt);
	HitStopLeft = FMath::Max(0.f, HitStopLeft - Dt);
	WindUpLeft = FMath::Max(0.f, WindUpLeft - Dt);
	CurAction.Advance(ActionDt);
	PrevAction.Advance(Dt);

	// --- Slide pose weight (locomotion only: downed / victory / defeat own the body)
	const float SlideTarget = (Inst->bInputSliding && AnimSlide && Want == ESQAnimBase::Locomotion) ? 1.f : 0.f;
	SlideAlpha = FMath::FInterpTo(SlideAlpha, SlideTarget, Dt, SlideTarget > SlideAlpha ? 16.f : 9.f);
	if (SlideAlpha < 0.002f && SlideTarget <= 0.f) SlideAlpha = 0.f;

	// --- Air pose weight and where on the strip we are (rising fast = start, apex = middle, falling fast = end)
	const float AirTarget = (Inst->bInputAirborne && AnimAir && Want == ESQAnimBase::Locomotion && SlideTarget <= 0.f) ? 1.f : 0.f;
	AirAlpha = FMath::FInterpTo(AirAlpha, AirTarget, Dt, AirTarget > AirAlpha ? 18.f : 12.f);
	if (AirAlpha < 0.002f && AirTarget <= 0.f) AirAlpha = 0.f;
	if (AnimAir && AirTarget > 0.f)
	{
		const float RiseSpeed = FMath::Max(100.f, Inst->TuneAirRiseSpeed);
		const float FallSpeed = FMath::Max(100.f, Inst->TuneAirFallSpeed);
		const float Vz = Inst->InputVerticalSpeed;
		const float Strip = Vz >= 0.f ? 0.5f * (1.f - FMath::Clamp(Vz / RiseSpeed, 0.f, 1.f)) : 0.5f + 0.5f * FMath::Clamp(-Vz / FallSpeed, 0.f, 1.f);
		const float WantTime = Strip * LenAir;
		AirSampleTime = AirAlpha < 0.05f ? WantTime : FMath::FInterpTo(AirSampleTime, WantTime, Dt, 14.f);
	}

	// --- Procedural
	LeanQuat = Inst->LeanComponent;
	LeanAlpha = FMath::FInterpTo(LeanAlpha, CurState == ESQAnimBase::Locomotion ? 1.f : 0.f, Dt, 8.f);
	BobUnits = Inst->BobScale;
	TurnTwist = Inst->TurnTwistDeg;
	HitWobble = Inst->HitWobbleDeg;
	HitSide = Inst->HitWobbleSide;
	LookYaw = Inst->GetLookYaw();
	LookPitch = Inst->GetLookPitch();
}

void FSQAnimInstanceProxy::RefreshBoneCache(const FCompactPose& Pose)
{
	const FBoneContainer& BoneCont = Pose.GetBoneContainer();
	const FReferenceSkeleton& RefSk = BoneCont.GetReferenceSkeleton();
	const int32 NumMesh = RefSk.GetNum();

	if (bBoneCacheDirty || CachedRefSkel != &RefSk || CachedRefNum != NumMesh)
	{
		bBoneCacheDirty = false;
		CachedRefSkel = &RefSk;
		CachedRefNum = NumMesh;
		MeshHips = INDEX_NONE;
		MeshSpine = INDEX_NONE;
		MeshChestBone = INDEX_NONE;
		MeshHeadBone = INDEX_NONE;
		MeshArmL = INDEX_NONE;
		MeshArmR = INDEX_NONE;
		bArmNeutralValid = false;
		for (int32 i = 0; i < NumMesh; ++i)
		{
			const FString BoneName = RefSk.GetBoneName(i).ToString().ToLower();
			if (MeshHips == INDEX_NONE && (BoneName.Contains(TEXT("hips")) || BoneName.Contains(TEXT("pelvis")))) MeshHips = i;
			if (MeshSpine == INDEX_NONE && BoneName.Contains(TEXT("spine"))) MeshSpine = i;
			if (MeshChestBone == INDEX_NONE && BoneName.Contains(TEXT("chest"))) MeshChestBone = i;
			if (MeshHeadBone == INDEX_NONE && BoneName.Equals(TEXT("head"))) MeshHeadBone = i;
			// KayKit rig: upperarm.l / upperarm.r (dots may become underscores on import)
			const bool bUpperArm = BoneName.Contains(TEXT("upperarm")) || BoneName.Contains(TEXT("upper_arm"));
			if (bUpperArm && !BoneName.Contains(TEXT("twist")))
			{
				if (MeshArmL == INDEX_NONE && (BoneName.EndsWith(TEXT(".l")) || BoneName.EndsWith(TEXT("_l")) || BoneName.Contains(TEXT("left")))) MeshArmL = i;
				if (MeshArmR == INDEX_NONE && (BoneName.EndsWith(TEXT(".r")) || BoneName.EndsWith(TEXT("_r")) || BoneName.Contains(TEXT("right")))) MeshArmR = i;
			}
		}
		if (MeshHeadBone == INDEX_NONE)
		{
			for (int32 i = 0; i < NumMesh; ++i)
			{
				const FString BoneName = RefSk.GetBoneName(i).ToString().ToLower();
				if (BoneName.Contains(TEXT("head")) && !BoneName.Contains(TEXT("top")) && !BoneName.Contains(TEXT("end"))) { MeshHeadBone = i; break; }
			}
		}
		if (MeshSpine == INDEX_NONE) MeshSpine = MeshChestBone;
		if (MeshChestBone == MeshSpine) MeshChestBone = INDEX_NONE;

		// Upper body = the spine (partial) and everything below it in the hierarchy (chest, arms, head)
		MeshUpperWeight.Init(MeshSpine == INDEX_NONE ? 1.f : 0.f, NumMesh);
		if (MeshSpine != INDEX_NONE)
		{
			for (int32 i = 0; i < NumMesh; ++i)
			{
				const int32 Parent = RefSk.GetParentIndex(i);
				if (i == MeshSpine) MeshUpperWeight[i] = 0.6f;
				else if (Parent != INDEX_NONE && Parent < i && MeshUpperWeight[Parent] > 0.f) MeshUpperWeight[i] = 1.f;
			}
		}
	}

	CompactUpperWeight.SetNumZeroed(Pose.GetNumBones());
	CompactHips = INDEX_NONE;
	CompactSpine = INDEX_NONE;
	CompactChest = INDEX_NONE;
	CompactHead = INDEX_NONE;
	CompactArmL = INDEX_NONE;
	CompactArmR = INDEX_NONE;
	for (const FCompactPoseBoneIndex BoneIndex : Pose.ForEachBoneIndex())
	{
		const int32 MeshIndex = BoneCont.MakeMeshPoseIndex(BoneIndex).GetInt();
		const int32 C = BoneIndex.GetInt();
		CompactUpperWeight[C] = MeshUpperWeight.IsValidIndex(MeshIndex) ? MeshUpperWeight[MeshIndex] : 0.f;
		if (MeshIndex == MeshHips) CompactHips = C;
		if (MeshIndex == MeshSpine) CompactSpine = C;
		if (MeshIndex == MeshChestBone) CompactChest = C;
		if (MeshIndex == MeshHeadBone) CompactHead = C;
		if (MeshIndex == MeshArmL) CompactArmL = C;
		if (MeshIndex == MeshArmR) CompactArmR = C;
	}
}

void FSQAnimInstanceProxy::EvaluateLocomotion(FPoseContext& Out)
{
	struct FLayer { const UAnimSequence* Anim; float Length; float Time; float Weight; };
	const FLayer Layers[3] = {
		{ AnimIdle, LenIdle, bIdleIsFrozenWalk ? 0.f : IdleClock, 1.f - WalkAlpha },
		{ AnimWalk, LenWalk, SamplePhase * LenWalk, WalkAlpha * (1.f - RunAlpha) },
		{ AnimRun, LenRun, SamplePhase * LenRun, WalkAlpha * RunAlpha },
	};

	float Accum = 0.f;
	bool bAny = false;
	for (const FLayer& L : Layers)
	{
		if (L.Weight <= 0.001f || !L.Anim) continue;
		if (!bAny)
		{
			SQAnimPose::Sample(Out, L.Anim, L.Length, L.Time, true);
			Accum = L.Weight;
			bAny = true;
			continue;
		}
		FPoseContext Temp(this);
		SQAnimPose::Sample(Temp, L.Anim, L.Length, L.Time, true);
		Accum += L.Weight;
		SQAnimPose::BlendInto(Out.Pose, Temp.Pose, L.Weight / Accum);
	}
	if (!bAny)
	{
		// Nothing for this blend: any locomotion clip beats the T-pose
		const UAnimSequence* Fallback = AnimIdle ? AnimIdle : (AnimWalk ? AnimWalk : AnimRun);
		const float FallbackLen = AnimIdle ? LenIdle : (AnimWalk ? LenWalk : LenRun);
		SQAnimPose::Sample(Out, Fallback, FallbackLen, IdleClock, true);
	}
}

void FSQAnimInstanceProxy::EvaluateBase(ESQAnimBase State, float StateClock, FPoseContext& Out)
{
	switch (State)
	{
	case ESQAnimBase::Downed:  SQAnimPose::Sample(Out, AnimDowned, LenDowned, StateClock, true); break;
	case ESQAnimBase::Victory: SQAnimPose::Sample(Out, AnimVictory, LenVictory, StateClock, true); break;
	case ESQAnimBase::Defeat:  SQAnimPose::Sample(Out, AnimDefeat, LenDefeat, StateClock, true); break;
	default:                   EvaluateLocomotion(Out); break;
	}
}

void FSQAnimInstanceProxy::ApplyAction(const FActionSlot& Slot, FPoseContext& Out)
{
	const float W = Slot.Weight();
	if (W <= 0.001f || !Slot.Anim) return;

	FPoseContext ActionPose(this);
	SQAnimPose::Sample(ActionPose, Slot.Anim, Slot.Length, Slot.Elapsed * Slot.Rate, false);

	FCompactPose& Pose = Out.Pose;
	if (Pose.GetNumBones() != ActionPose.Pose.GetNumBones()) return;
	// Standing still, an upper-body attack may use the legs too (planted stance)
	const float LowerBody = CurState == ESQAnimBase::Locomotion ? 1.f - WalkAlpha : 1.f;
	for (const FCompactPoseBoneIndex BoneIndex : Pose.ForEachBoneIndex())
	{
		const int32 C = BoneIndex.GetInt();
		const float Mask = Slot.bFullBody ? 1.f : FMath::Max(CompactUpperWeight.IsValidIndex(C) ? CompactUpperWeight[C] : 1.f, LowerBody);
		const float BoneWeight = W * Mask;
		if (BoneWeight >= 0.999f) Pose[BoneIndex] = ActionPose.Pose[BoneIndex];
		else if (BoneWeight > 0.001f) Pose[BoneIndex].BlendWith(ActionPose.Pose[BoneIndex], BoneWeight);
	}
}

void FSQAnimInstanceProxy::MeasureArmNeutral(const UAnimSequence* Clip, float ClipLength, FQuat& OutLeft, FQuat& OutRight)
{
	// Two opposite phases of a cycle swing the arm to mirrored extremes: their average is where the arm hangs
	FPoseContext PhaseA(this);
	FPoseContext PhaseB(this);
	SQAnimPose::Sample(PhaseA, Clip, ClipLength, 0.f, true);
	SQAnimPose::Sample(PhaseB, Clip, ClipLength, ClipLength * 0.5f, true);
	const int32 NumPoseBones = PhaseA.Pose.GetNumBones();
	auto Mid = [&PhaseA, &PhaseB, NumPoseBones](int32 Compact) -> FQuat
	{
		if (Compact == INDEX_NONE || Compact >= NumPoseBones) return FQuat::Identity;
		const FCompactPoseBoneIndex ArmIndex(Compact);
		FQuat Avg = FQuat::Slerp(PhaseA.Pose[ArmIndex].GetRotation(), PhaseB.Pose[ArmIndex].GetRotation(), 0.5f);
		Avg.Normalize();
		return Avg;
	};
	OutLeft = Mid(CompactArmL);
	OutRight = Mid(CompactArmR);
}

void FSQAnimInstanceProxy::RefreshArmNeutral()
{
	if (bArmNeutralValid && ArmNeutralWalkClip == AnimWalk && ArmNeutralRunClip == AnimRun) return;
	bArmNeutralValid = true;
	ArmNeutralWalkClip = AnimWalk;
	ArmNeutralRunClip = AnimRun;
	MeasureArmNeutral(AnimWalk, LenWalk, ArmNeutralWalkL, ArmNeutralWalkR);
	if (AnimRun && AnimRun != AnimWalk) MeasureArmNeutral(AnimRun, LenRun, ArmNeutralRunL, ArmNeutralRunR);
	else { ArmNeutralRunL = ArmNeutralWalkL; ArmNeutralRunR = ArmNeutralWalkR; }
}

void FSQAnimInstanceProxy::ApplyGaitArms(FCompactPose& Pose)
{
	// ArmSwing scales how far the upper arms swing away from where they hang (faded to 1 when standing still)
	const float SwingScale = FMath::Lerp(1.f, FMath::Clamp(GaitProfile.ArmSwing, 0.f, 2.5f), WalkAlpha * LeanAlpha * (1.f - SlideAlpha));
	if (FMath::IsNearlyEqual(SwingScale, 1.f, 0.01f) || !AnimWalk) return;
	if (CompactArmL == INDEX_NONE && CompactArmR == INDEX_NONE) return;
	RefreshArmNeutral();

	const int32 NumPoseBones = Pose.GetNumBones();
	const int32 ArmBones[2] = { CompactArmL, CompactArmR };
	const FQuat Neutrals[2] = {
		FQuat::Slerp(ArmNeutralWalkL, ArmNeutralRunL, RunAlpha),
		FQuat::Slerp(ArmNeutralWalkR, ArmNeutralRunR, RunAlpha) };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (ArmBones[Side] == INDEX_NONE || ArmBones[Side] >= NumPoseBones) continue;
		FTransform& ArmLocal = Pose[FCompactPoseBoneIndex(ArmBones[Side])];
		FQuat Neutral = Neutrals[Side];
		Neutral.Normalize();
		FQuat Swing = Neutral.Inverse() * ArmLocal.GetRotation();
		Swing.Normalize();
		if (Swing.W < 0.0) Swing = FQuat(-Swing.X, -Swing.Y, -Swing.Z, -Swing.W); // shortest arc
		FVector SwingAxis = FVector::UpVector;
		double SwingAngle = 0.0;
		Swing.ToAxisAndAngle(SwingAxis, SwingAngle);
		if (SwingAngle < 1e-4) continue;
		const double ScaledAngle = FMath::Clamp(SwingAngle * static_cast<double>(SwingScale), 0.0, static_cast<double>(PI) * 0.9);
		FQuat NewRot = Neutral * FQuat(SwingAxis, ScaledAngle);
		NewRot.Normalize();
		ArmLocal.SetRotation(NewRot);
	}
}

void FSQAnimInstanceProxy::ApplyGaitBody(FCompactPose& Pose)
{
	const FSQGaitProfile& G = GaitProfile;
	if (LeanAlpha <= 0.001f) return;

	// Full-body actions (rolls, spawns, fidgets) own the pose: the personality fades out under them
	float FullBodyAction = 0.f;
	if (CurAction.bFullBody) FullBodyAction = FMath::Max(FullBodyAction, CurAction.Weight());
	if (PrevAction.bFullBody) FullBodyAction = FMath::Max(FullBodyAction, PrevAction.Weight());
	const float GaitW = LeanAlpha * (1.f - FMath::Clamp(FullBodyAction, 0.f, 1.f)) * (1.f - SlideAlpha); // sliding: no stride personality
	const float MoveW = WalkAlpha * GaitW;              // walking or running
	const float RunW = WalkAlpha * RunAlpha * GaitW;    // running only
	const float StillW = (1.f - WalkAlpha) * GaitW;     // standing
	const float Cycle = 2.f * PI * SamplePhase;         // one full stride (two steps)
	const float StepHump = FMath::Abs(FMath::Sin(Cycle)); // peaks mid-step, zero at each footfall
	const float LimpSide = FMath::Max(0.f, FMath::Sin(Cycle)); // only during one leg's step
	const int32 NumPoseBones = Pose.GetNumBones();
	auto Valid = [NumPoseBones](int32 Compact) { return Compact != INDEX_NONE && Compact < NumPoseBones; };

	// Hover: the whole body floats (ghosts), bobbing gently; kept under actions so a ghost never drops to the floor
	if (G.Hover > 0.f)
	{
		const float HoverCm = G.Hover * (1.f + 0.15f * FMath::Sin(IdleClock * 2.2f)) * LeanAlpha;
		SQAnimPose::TranslateInComponentSpace(Pose, FCompactPoseBoneIndex(0), GaitUp * (HoverCm * BobUnits));
	}

	if (Valid(CompactHips))
	{
		const FCompactPoseBoneIndex HipsIndex(CompactHips);
		// Bounce: a hop between footfalls. Limp: the hip drops on the bad leg's step
		const float RiseCm = G.Bounce * StepHump * MoveW - G.Limp * 5.f * LimpSide * MoveW;
		if (FMath::Abs(RiseCm) > 0.01f) SQAnimPose::TranslateInComponentSpace(Pose, HipsIndex, GaitUp * (RiseCm * BobUnits));

		// Sway: hips roll side to side once per stride (a slow idle sway when standing); limp adds a lurch
		const float RollDeg = G.Sway * FMath::Sin(Cycle) * MoveW
			+ G.Sway * 0.25f * FMath::Sin(IdleClock * 1.6f) * StillW
			+ G.Limp * 7.f * LimpSide * MoveW;
		if (FMath::Abs(RollDeg) > 0.01f)
		{
			SQAnimPose::RotateInComponentSpace(Pose, HipsIndex, SQAnimPose::TiltToward(GaitUp, GaitRight, RollDeg));
			// Counter-roll the spine by half so the swing reads in the hips, not as the whole reaper tipping over
			if (Valid(CompactSpine)) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactSpine), SQAnimPose::TiltToward(GaitUp, GaitRight, -0.5f * RollDeg));
		}
	}

	// Hunch (always) + run lean (faded in with running): bend the spine (and chest) forward
	const float BendDeg = G.Hunch * GaitW + G.Lean * RunW;
	if (FMath::Abs(BendDeg) > 0.01f)
	{
		const bool bChest = Valid(CompactChest);
		if (Valid(CompactSpine)) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactSpine), SQAnimPose::TiltToward(GaitUp, GaitFwd, BendDeg * (bChest ? 0.6f : 1.f)));
		if (bChest) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactChest), SQAnimPose::TiltToward(GaitUp, GaitFwd, BendDeg * 0.4f));
	}

	// Head bob: a nod down on every footfall
	const float NodDeg = G.HeadBob * FMath::Cos(2.f * Cycle) * MoveW;
	if (Valid(CompactHead) && FMath::Abs(NodDeg) > 0.01f)
	{
		SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactHead), SQAnimPose::TiltToward(GaitUp, GaitFwd, NodDeg));
	}
}

bool FSQAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	FCompactPose& Pose = Output.Pose;
	if (Pose.GetNumBones() <= 0) return true;
	RefreshBoneCache(Pose);

	// 1+2. Base state, crossfaded (eased) from the previous one
	EvaluateBase(CurState, CurClock, Output);
	if (StateBlend < 1.f)
	{
		FPoseContext PrevPose(this);
		EvaluateBase(PrevState, PrevClock, PrevPose);
		SQAnimPose::BlendInto(Pose, PrevPose.Pose, 1.f - FMath::SmoothStep(0.f, 1.f, StateBlend));
	}

	// 2a. Slide: the legs-forward slide pose over the locomotion (eased)
	if (SlideAlpha > 0.001f && AnimSlide && CurState == ESQAnimBase::Locomotion)
	{
		FPoseContext SlidePose(this);
		SQAnimPose::Sample(SlidePose, AnimSlide, LenSlide, SlideSampleTime, false);
		SQAnimPose::BlendInto(Pose, SlidePose.Pose, FMath::SmoothStep(0.f, 1.f, SlideAlpha));
	}

	// 2a'. Airborne: the jump strip over the locomotion (eased)
	if (AirAlpha > 0.001f && AnimAir && CurState == ESQAnimBase::Locomotion)
	{
		FPoseContext AirPose(this);
		SQAnimPose::Sample(AirPose, AnimAir, LenAir, FMath::Clamp(AirSampleTime, 0.f, LenAir), false);
		SQAnimPose::BlendInto(Pose, AirPose.Pose, FMath::SmoothStep(0.f, 1.f, AirAlpha));
	}

	// 2b. Gait arm swing works on the locomotion arms only, so actions layered next keep their own arms
	ApplyGaitArms(Pose);

	// 3. One-shot actions (interrupted one first so the new one wins)
	ApplyAction(PrevAction, Output);
	ApplyAction(CurAction, Output);

	// 3b. Gait personality (hover, bounce, sway, limp, hunch, run lean, head bob)
	ApplyGaitBody(Pose);

	// 4. Procedural juice
	if (LeanAlpha > 0.001f)
	{
		// Hip bob: two dips per cycle, scaled by speed
		if (CompactHips != INDEX_NONE && WalkAlpha > 0.f)
		{
			const float SpeedAlpha = FMath::Clamp(SmoothSpeed / RunRefSpeed, 0.f, 1.f);
			const float AmpCm = 1.8f * WalkAlpha * (0.4f + 0.6f * SpeedAlpha) * LeanAlpha * (1.f - SlideAlpha);
			const float Dip = 0.5f + 0.5f * FMath::Cos(SamplePhase * 4.f * PI);
			SQAnimPose::TranslateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactHips), FVector(0.f, 0.f, -AmpCm * Dip * BobUnits));
		}

		// Lean the whole body about the feet, plus a little extra bend in the spine
		const FQuat Lean = FQuat::Slerp(FQuat::Identity, LeanQuat, LeanAlpha);
		SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(0), Lean);
		// Sliding: lean back on the slide (the acceleration / turn lean above still tips it into curves)
		if (SlideAlpha > 0.001f)
		{
			SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(0), SQAnimPose::TiltToward(GaitUp, -GaitFwd, SlideLeanDeg * SlideAlpha * LeanAlpha));
		}
		if (CompactSpine != INDEX_NONE && CompactSpine != 0)
		{
			SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactSpine), FQuat::Slerp(FQuat::Identity, LeanQuat, LeanAlpha * 0.4f));
		}

		// Turn in place: standing and turning, the chest and head lead the turn and the body tips into it a little
		const int32 NumPoseBones = Pose.GetNumBones();
		const float TwistDeg = TurnTwist * LeanAlpha;
		if (FMath::Abs(TwistDeg) > 0.2f)
		{
			const float TwistRad = FMath::DegreesToRadians(TwistDeg);
			if (CompactSpine != INDEX_NONE && CompactSpine < NumPoseBones) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactSpine), FQuat(GaitUp, TwistRad * 0.35f));
			if (CompactChest != INDEX_NONE && CompactChest < NumPoseBones) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactChest), FQuat(GaitUp, TwistRad * 0.25f));
			if (CompactHead != INDEX_NONE && CompactHead < NumPoseBones) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactHead), FQuat(GaitUp, TwistRad * 0.4f));
			SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(0), SQAnimPose::TiltToward(GaitUp, GaitRight, TwistDeg * 0.2f));
		}
	}

	// Look offset: the chest and head turn toward where the reaper is looking (fades out with the locomotion state, so
	// downed / victory / defeat poses and their head angles are left alone)
	if ((FMath::Abs(LookYaw) > 0.5f || FMath::Abs(LookPitch) > 0.5f) && LeanAlpha > 0.001f)
	{
		const int32 LookBones = Pose.GetNumBones();
		const float YawRad = FMath::DegreesToRadians(LookYaw) * LeanAlpha;
		const float PitchDeg = LookPitch * LeanAlpha;
		if (CompactChest != INDEX_NONE && CompactChest < LookBones)
		{
			SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactChest), FQuat(GaitUp, YawRad * 0.3f));
		}
		if (CompactHead != INDEX_NONE && CompactHead < LookBones)
		{
			SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactHead), FQuat(GaitUp, YawRad * 0.55f));
			SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactHead), SQAnimPose::TiltToward(GaitUp, -GaitFwd, PitchDeg * 0.6f));
		}
	}

	// Hit wobble: the spine snaps back from the hit and rings out; the head rolls with it (also under actions)
	if (FMath::Abs(HitWobble) > 0.1f && CurState == ESQAnimBase::Locomotion)
	{
		const int32 WobbleBones = Pose.GetNumBones();
		if (CompactSpine != INDEX_NONE && CompactSpine < WobbleBones) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactSpine), SQAnimPose::TiltToward(GaitUp, -GaitFwd, HitWobble));
		if (CompactHead != INDEX_NONE && CompactHead < WobbleBones) SQAnimPose::RotateInComponentSpace(Pose, FCompactPoseBoneIndex(CompactHead), SQAnimPose::TiltToward(GaitUp, GaitRight, HitWobble * 0.6f * HitSide));
	}
	return true;
}

// ============================================================================ instance (game thread)

FAnimInstanceProxy* USQAnimInstance::CreateAnimInstanceProxy()
{
	return new FSQAnimInstanceProxy(this);
}

void USQAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	Super::DestroyAnimInstanceProxy(InProxy);
}

void USQAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (const USQData* Data = USQData::Get(this))
	{
		TuneWalkSpeed = Data->Tune(TEXT("AnimWalkSpeed"), 250.f);
		TuneRunSpeed = Data->Tune(TEXT("AnimRunSpeed"), 550.f);
		TuneLeanDegrees = Data->Tune(TEXT("AnimLeanDegrees"), 12.f);
		TuneSlideLeanDegrees = Data->Tune(TEXT("AnimSlideLeanDegrees"), 10.f);
		TuneAirRiseSpeed = Data->Tune(TEXT("AnimAirRiseSpeed"), 720.f);
		TuneAirFallSpeed = Data->Tune(TEXT("AnimAirFallSpeed"), 900.f);
	}
}

namespace SQAnimNames
{
	// Every locomotion take in the KayKit / Reaper FBX files, most neutral first: the last-resort fallbacks
	static const TCHAR* const Idles[] = { TEXT("Idle"), TEXT("Idle_B"), TEXT("Unarmed_Idle"), TEXT("2H_Melee_Idle"), TEXT("Idle_Combat"),
		TEXT("SQ_Idle_Proud"), TEXT("SQ_Idle_Hunched"), TEXT("SQ_Idle_Nervous"), TEXT("SQ_Idle_Menace"), TEXT("SQ_Idle_Float") };
	static const TCHAR* const Walks[] = { TEXT("Walking_A"), TEXT("Running_A"), TEXT("Walking_B"), TEXT("Walking_C"), TEXT("Walking_D_Skeletons"),
		TEXT("SQ_Walk_March"), TEXT("SQ_Walk_Shuffle"), TEXT("SQ_Walk_Sneak"), TEXT("SQ_Walk_Swagger"), TEXT("SQ_Walk_Prance"),
		TEXT("SQ_Walk_Waddle"), TEXT("SQ_Walk_Limp"), TEXT("Running_B"), TEXT("Running_C") };
	static const TCHAR* const Runs[] = { TEXT("Running_A"), TEXT("Walking_A"), TEXT("Running_B"), TEXT("Running_C"), TEXT("SQ_Run_Sprint"),
		TEXT("SQ_Run_Heavy"), TEXT("SQ_Run_Scurry"), TEXT("SQ_Run_Gallop"), TEXT("SQ_Run_Waddle"), TEXT("SQ_Run_Float"),
		TEXT("Walking_B"), TEXT("Walking_C"), TEXT("Walking_D_Skeletons") };

	/** True the first time Key is seen (one-time warnings) */
	static bool FirstTime(const FString& Key)
	{
		static TSet<FString> Seen;
		if (Seen.Contains(Key)) return false;
		Seen.Add(Key);
		return true;
	}
}

UAnimSequence* USQAnimInstance::FindFirstNamed(const TCHAR* const* Names, int32 NumNames)
{
	for (int32 NameIdx = 0; NameIdx < NumNames; ++NameIdx)
	{
		if (UAnimSequence* Anim = FindAnim(FName(Names[NameIdx]))) return Anim;
	}
	return nullptr;
}

UAnimSequence* USQAnimInstance::FindAnim(FName AnimName)
{
	if (AnimSetModel.IsNone() || AnimName.IsNone()) return nullptr;
	if (const TObjectPtr<UAnimSequence>* Found = ActionCache.Find(AnimName))
	{
		return Found->Get();
	}
	UAnimSequence* Anim = SQArt::Anim(AnimSetModel, AnimName);

	// Only accept clips made for this mesh's skeleton (a mismatched skeleton would sample garbage)
	const USkeletalMeshComponent* Comp = GetSkelMeshComponent();
	const USkeletalMesh* SkelMesh = Comp ? Comp->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* MeshSkeleton = SkelMesh ? SkelMesh->GetSkeleton() : nullptr;
	if (Anim && MeshSkeleton && Anim->GetSkeleton() != MeshSkeleton)
	{
		UE_LOG(LogTemp, Warning, TEXT("SQAnim: %s skipped (skeleton does not match %s)"), *Anim->GetName(), *AnimSetModel.ToString());
		Anim = nullptr;
	}
	ActionCache.Add(AnimName, Anim);
	return Anim;
}

void USQAnimInstance::SetAnimSet(FName Model)
{
	if (Model == AnimSetModel) return;
	AnimSetModel = Model;
	ActionCache.Reset();

	auto First = [this](std::initializer_list<const TCHAR*> Names) -> UAnimSequence*
	{
		for (const TCHAR* N : Names)
		{
			if (UAnimSequence* Anim = FindAnim(FName(N))) return Anim;
		}
		return nullptr;
	};

	SeqIdle = FindFirstNamed(SQAnimNames::Idles, static_cast<int32>(UE_ARRAY_COUNT(SQAnimNames::Idles)));
	SeqWalk = FindFirstNamed(SQAnimNames::Walks, static_cast<int32>(UE_ARRAY_COUNT(SQAnimNames::Walks)));
	SeqRun = FindFirstNamed(SQAnimNames::Runs, static_cast<int32>(UE_ARRAY_COUNT(SQAnimNames::Runs)));
	if (!SeqWalk) SeqWalk = SeqRun;
	if (!SeqRun) SeqRun = SeqWalk;
	bIdleFrozen = !SeqIdle && SeqWalk;
	if (!SeqIdle) SeqIdle = SeqWalk;
	SeqDowned = First({ TEXT("Lie_Idle"), TEXT("Death_A_Pose"), TEXT("Death_B_Pose") });
	if (!SeqDowned) SeqDowned = SeqIdle;
	SeqVictory = First({ TEXT("Cheer"), TEXT("Taunt") });
	if (!SeqVictory) SeqVictory = SeqIdle;
	SeqDefeat = First({ TEXT("Death_A_Pose"), TEXT("Death_B_Pose") });
	if (!SeqDefeat) SeqDefeat = SeqDowned;
	DefaultDowned = SeqDowned;
	DefaultVictory = SeqVictory;
	DefaultDefeat = SeqDefeat;
	// Slide: the seated floor pose reads as a legs-first slide; otherwise a held crouched frame of a roll / sneak
	{
		static const TCHAR* const SlideTakes[] = { TEXT("SQ_Slide"), TEXT("Sit_Floor_Pose"), TEXT("Sit_Floor_Idle"), TEXT("Dodge_Forward"), TEXT("SQ_Walk_Sneak") };
		static const float SlideTakeFraction[] = { 0.f, 0.f, 0.f, 0.32f, 0.25f };
		static_assert(UE_ARRAY_COUNT(SlideTakes) == UE_ARRAY_COUNT(SlideTakeFraction), "one sample point per slide take");
		SeqSlide = nullptr;
		SlidePoseTime = 0.f;
		for (int32 SlideIdx = 0; SlideIdx < static_cast<int32>(UE_ARRAY_COUNT(SlideTakes)); ++SlideIdx)
		{
			UAnimSequence* SlideTake = FindAnim(FName(SlideTakes[SlideIdx]));
			// A zero-length take would sample as the reference (T) pose: try the next one
			if (SlideTake && SlideTake->GetPlayLength() > 0.01)
			{
				SeqSlide = SlideTake;
				SlidePoseTime = SlideTakeFraction[SlideIdx] * static_cast<float>(SlideTake->GetPlayLength());
				break;
			}
		}
	}
	SeqAir = FindAnim(FName(TEXT("SQ_Jump_Air")));
	if (SeqAir && SeqAir->GetPlayLength() <= 0.01) SeqAir = nullptr;
	DefaultIdle = bIdleFrozen ? nullptr : SeqIdle.Get(); // never hand a walk to SetStyle as an idle
	DefaultWalk = SeqWalk;
	DefaultRun = SeqRun;

	PendingActions.Reset();
	BusySeq = nullptr;
	bBusyPriority = false;
	ActionBusyTime = 0.f;
	ActionBusyTotal = 0.f;
	++AnimSetSerial;
}

UAnimSequence* USQAnimInstance::FindFirstAnim(const TArray<FName>& Names)
{
	for (const FName& Candidate : Names)
	{
		if (UAnimSequence* Found = FindAnim(Candidate)) return Found;
	}
	return nullptr;
}

void USQAnimInstance::SetStyle(const TArray<FName>& IdleNames, const TArray<FName>& WalkNames, const TArray<FName>& RunNames, FName DebugOwner)
{
	// Fallback chain per slot: the reaper's style list -> Walking_A / Running_A -> any locomotion clip this model has.
	// Starts from scratch every time, so switching reaper on the same model never keeps the old style.
	UAnimSequence* StyleIdle = FindFirstAnim(IdleNames);
	UAnimSequence* StyleWalk = FindFirstAnim(WalkNames);
	UAnimSequence* StyleRun = FindFirstAnim(RunNames);
	if (!StyleIdle) StyleIdle = DefaultIdle.Get() ? DefaultIdle.Get() : FindFirstNamed(SQAnimNames::Idles, static_cast<int32>(UE_ARRAY_COUNT(SQAnimNames::Idles)));
	if (!StyleWalk) StyleWalk = FindFirstNamed(SQAnimNames::Walks, static_cast<int32>(UE_ARRAY_COUNT(SQAnimNames::Walks)));
	if (!StyleRun) StyleRun = FindFirstNamed(SQAnimNames::Runs, static_cast<int32>(UE_ARRAY_COUNT(SQAnimNames::Runs)));
	if (!StyleWalk) StyleWalk = StyleRun;
	if (!StyleRun) StyleRun = StyleWalk;
	bIdleFrozen = !StyleIdle && StyleWalk;
	if (!StyleIdle) StyleIdle = StyleWalk;
	SeqIdle = StyleIdle;
	SeqWalk = StyleWalk;
	SeqRun = StyleRun;

	// Remaster 7: the reaper's own state loops (SQ_Downed_<Id> on its knees, SQ_Victory_<Id>, SQ_Defeat_<Id>), else the
	// model's shared ones. DebugOwner is the reaper id.
	auto OwnState = [this, &DebugOwner](const TCHAR* Prefix, UAnimSequence* Shared) -> UAnimSequence*
	{
		if (DebugOwner.IsNone()) return Shared;
		UAnimSequence* Own = FindAnim(FName(*(FString(Prefix) + DebugOwner.ToString())));
		return (Own && Own->GetPlayLength() > 0.01) ? Own : Shared;
	};
	SeqDowned = OwnState(TEXT("SQ_Downed_"), DefaultDowned.Get());
	SeqVictory = OwnState(TEXT("SQ_Victory_"), DefaultVictory.Get());
	SeqDefeat = OwnState(TEXT("SQ_Defeat_"), DefaultDefeat.Get());

	const FString OwnerLabel = DebugOwner.IsNone() ? FString(TEXT("?")) : DebugOwner.ToString();
	if (!StyleWalk)
	{
		if (SQAnimNames::FirstTime(OwnerLabel + TEXT("/") + AnimSetModel.ToString()))
		{
			UE_LOG(LogTemp, Warning, TEXT("SQAnim: reaper %s (model %s) has NO walk/run clips: it will slide. Check /Game/Art/Characters/%s/A_* (re-run Scripts/import_art.py)."),
				*OwnerLabel, *AnimSetModel.ToString(), *AnimSetModel.ToString());
		}
	}
	else if ((WalkNames.Num() > 0 && !FindFirstAnim(WalkNames)) || (RunNames.Num() > 0 && !FindFirstAnim(RunNames)) || bIdleFrozen)
	{
		if (SQAnimNames::FirstTime(OwnerLabel + TEXT("/") + AnimSetModel.ToString() + TEXT("/style")))
		{
			UE_LOG(LogTemp, Warning, TEXT("SQAnim: reaper %s (model %s): a styled idle/walk/run clip is missing, using fallback %s / %s / %s."),
				*OwnerLabel, *AnimSetModel.ToString(), *GetNameSafe(SeqIdle.Get()), *GetNameSafe(SeqWalk.Get()), *GetNameSafe(SeqRun.Get()));
		}
	}
}

void USQAnimInstance::SetLocomotion(float InSpeed, const FVector& InVelocity, float InTurnRate, bool bInDowned, bool bInCarrying, uint8 InMood)
{
	InputSpeed = InSpeed;
	InputVelocity = InVelocity;
	InputTurnRate = InTurnRate;
	bInputDowned = bInDowned;
	bInputCarrying = bInCarrying;
	InputMood = InMood;
}

void USQAnimInstance::HitReact(float Strength)
{
	HitWobbleAge = 0.f;
	HitWobbleAmp = 9.f * FMath::Clamp(Strength, 0.f, 1.5f);
	HitWobbleSide = FMath::RandBool() ? 1.f : -1.f;
}

void USQAnimInstance::HitStopIn(float Delay, float Seconds)
{
	if (Delay <= 0.f)
	{
		HitStop(Seconds);
		return;
	}
	HitStopDelayLeft = FMath::Min(Delay, 1.f);
	HitStopDelayedSeconds = Seconds;
}

void USQAnimInstance::WindUp(float Seconds, float RateScale)
{
	PendingWindUp = FMath::Clamp(Seconds, 0.f, 0.4f);
	PendingWindRate = FMath::Clamp(RateScale, 0.2f, 1.f);
}

float USQAnimInstance::TimeToClipFraction(float ClipSeconds, float ClipFraction, float WindSeconds, float WindRate)
{
	// ClipSeconds is the clip's real length at its play rate; the first WindSeconds run at WindRate of that
	const float Target = FMath::Max(0.f, ClipSeconds * ClipFraction);
	const float Rate = FMath::Clamp(WindRate, 0.2f, 1.f);
	const float Wind = FMath::Max(0.f, WindSeconds);
	if (Wind * Rate >= Target) return Target / Rate;
	return Wind + (Target - Wind * Rate);
}

bool USQAnimInstance::PlayAction(FName AnimName, float Rate, bool bFullBody, float BlendIn, bool bPriority)
{
	UAnimSequence* Anim = FindAnim(AnimName);
	if (!Anim) return false;
	const float SafeRate = FMath::Max(0.05f, Rate);
	if (ActionLockLeft > 0.f && !bPriority) return false; // a cinematic (the rise) owns the body

	if (ActionBusyTime > 0.f && BusySeq)
	{
		const float Progress = 1.f - ActionBusyTime / FMath::Max(0.01f, ActionBusyTotal);
		if (bBusyFullBody && !bFullBody) return false;                    // never cut a roll/spawn with an attack
		if (BusySeq == Anim && Progress < 0.5f && !bPriority) return false; // don't restart the same clip constantly
		if (bBusyPriority && !bPriority && !bFullBody && Progress < 0.8f) return false; // attacks never cut an ability moment
		if (!bPriority && !bBusyFullBody && !bFullBody && Progress < 0.35f) return false; // let attacks read before the next one
	}

	if (PendingActions.Num() >= 4) PendingActions.RemoveAt(0);
	FPendingAction Pending;
	Pending.Seq = Anim;
	Pending.Rate = SafeRate;
	Pending.BlendIn = BlendIn;
	Pending.bFullBody = bFullBody;
	PendingActions.Add(Pending);

	BusySeq = Anim;
	bBusyFullBody = bFullBody;
	bBusyPriority = bPriority;
	ActionBusyTotal = static_cast<float>(Anim->GetPlayLength()) / SafeRate;
	ActionBusyTime = ActionBusyTotal;
	return true;
}

void USQAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	ActionBusyTime = FMath::Max(0.f, ActionBusyTime - DeltaSeconds);
	ActionLockLeft = FMath::Max(0.f, ActionLockLeft - DeltaSeconds);
	if (HitStopDelayLeft >= 0.f)
	{
		HitStopDelayLeft -= DeltaSeconds;
		if (HitStopDelayLeft < 0.f)
		{
			HitStopDelayLeft = -1.f;
			HitStop(HitStopDelayedSeconds);
		}
	}
	if (DeltaSeconds <= UE_KINDA_SMALL_NUMBER) return;
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);

	// Acceleration (forward/back lean) and turn rate (sideways lean), smoothed so replicated jitter doesn't twitch
	const FVector Vel2D(InputVelocity.X, InputVelocity.Y, 0.f);
	const FVector Accel = (Vel2D - PrevVelocity) / Dt;
	PrevVelocity = Vel2D;
	SmoothedAccel = FMath::VInterpTo(SmoothedAccel, Accel, Dt, 6.f);
	SmoothedTurnRate = FMath::FInterpTo(SmoothedTurnRate, InputTurnRate, Dt, 8.f);

	const AActor* OwnerActor = GetOwningActor();
	FVector Fwd = OwnerActor ? OwnerActor->GetActorForwardVector() : FVector::ForwardVector;
	Fwd.Z = 0.f;
	Fwd = Fwd.GetSafeNormal();
	if (Fwd.IsNearlyZero()) Fwd = FVector::ForwardVector;
	const FVector RightDir = FVector::CrossProduct(FVector::UpVector, Fwd); // actor right (+yaw turns toward it)

	const float AccelRef = 2200.f;
	const float ForwardAmt = FMath::Clamp(static_cast<float>(FVector::DotProduct(SmoothedAccel, Fwd)) / AccelRef, -1.f, 1.f);
	// Facing snap (a cast toward the cursor): a quick turn that is not a carve, so the sideways lean mostly stays out of it
	FacingSnapAlpha = FMath::FInterpTo(FacingSnapAlpha, InputFacingSnap, Dt, InputFacingSnap > FacingSnapAlpha ? 20.f : 6.f);
	const float SideAmt = FMath::Clamp(FMath::DegreesToRadians(SmoothedTurnRate) * InputSpeed / AccelRef, -1.f, 1.f) * (1.f - 0.85f * FacingSnapAlpha);
	// Backpedal: facing away from the motion while snapped -> the walk / run cycle runs backwards (blended through 0, no pop)
	const float MoveFacingDot = (InputSpeed > 60.f && !Vel2D.IsNearlyZero()) ? static_cast<float>(FVector::DotProduct(Vel2D.GetSafeNormal(), Fwd)) : 1.f;
	const float WantLocoDir = (FacingSnapAlpha > 0.05f && MoveFacingDot < -0.25f) ? -1.f : 1.f;
	LocoDirection = FMath::FInterpTo(LocoDirection, WantLocoDir, Dt, 12.f);
	const FVector LeanDir = Fwd * ForwardAmt + RightDir * SideAmt;
	const float Amount = FMath::Min(1.f, static_cast<float>(LeanDir.Size()));

	FQuat WorldLean = FQuat::Identity;
	if (Amount > 0.001f && !bInputDowned && InputMood == 0)
	{
		const float Angle = FMath::DegreesToRadians(FMath::Clamp(TuneLeanDegrees, 0.f, 45.f)) * Amount;
		const FVector Tilted = (FVector::UpVector + LeanDir.GetSafeNormal() * FMath::Tan(Angle)).GetSafeNormal();
		WorldLean = FQuat::FindBetweenNormals(FVector::UpVector, Tilted);
	}

	const USkeletalMeshComponent* Comp = GetSkelMeshComponent();
	const FQuat CompQuat = Comp ? Comp->GetComponentQuat() : FQuat::Identity;
	LeanComponent = CompQuat.Inverse() * WorldLean * CompQuat;

	// Character axes in mesh component space for the gait layer (the mesh is yawed to face +X)
	const FQuat CompInverse = CompQuat.Inverse();
	GaitAxisFwd = CompInverse.RotateVector(Fwd);
	GaitAxisRight = CompInverse.RotateVector(RightDir);
	GaitAxisUp = CompInverse.RotateVector(FVector::UpVector);
	BobScale = Comp ? 1.f / FMath::Max(0.0001f, static_cast<float>(Comp->GetComponentScale().Z)) : 1.f;

	// Turn in place: only when (nearly) standing; +turn rate = turning right = chest / head yaw toward +Z rotation
	// (a facing snap gets the same lead while running: the short turn blend for casts)
	const float StillK = FMath::Max(1.f - FMath::Clamp(InputSpeed / 150.f, 0.f, 1.f), FacingSnapAlpha);
	const float WantTwist = (bInputDowned || InputMood != 0) ? 0.f : FMath::Clamp(SmoothedTurnRate * 0.05f, -24.f, 24.f) * StillK;
	TurnTwistDeg = FMath::FInterpTo(TurnTwistDeg, WantTwist, Dt, 10.f);

	// Hit wobble: damped spring ringing out over ~0.5 s
	LookYawDeg = FMath::FInterpTo(LookYawDeg, InputLookYaw, Dt, 7.f);
	LookPitchDeg = FMath::FInterpTo(LookPitchDeg, InputLookPitch, Dt, 7.f);
	HitWobbleAge = FMath::Min(HitWobbleAge + Dt, 10.f);
	HitWobbleDeg = HitWobbleAge < 0.7f ? HitWobbleAmp * FMath::Exp(-HitWobbleAge * 7.f) * FMath::Sin(HitWobbleAge * 30.f + 0.6f) : 0.f;
}
