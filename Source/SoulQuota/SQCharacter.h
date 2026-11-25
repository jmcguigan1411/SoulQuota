// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SQTypes.h"
#include "SQCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class USQAnimInstance;
class UMaterialInstanceDynamic;
struct FSQUpgradeDef;
struct FSQReaperDef;

/** ASQCharacter::MulticastFX Kind for a squad combo burst (co-op pass, Nov 23 2026): impact + sparks + the COMBO callout */
inline constexpr uint8 SQFxKindCombo = 77;

/** One floating damage number sent to every machine (cosmetic). Info: bits 0-1 tier (0 normal, 1 crit, 2 huge), bits 2-7 delay in 1/20 s. */
USTRUCT()
struct FSQDamagePop
{
	GENERATED_BODY()

	UPROPERTY() int16 X = 0;
	UPROPERTY() int16 Y = 0;
	UPROPERTY() uint16 Amount = 0;
	UPROPERTY() uint8 Info = 0;
};

/**
 * Reaper movement: CharacterMovement plus Megabonk-style body blocking against the horde.
 * After the normal velocity update, ASQCharacter::ApplyBodyBlock removes the part of the velocity that walks into
 * enemy bodies and adds a capped push out of any overlap. It runs wherever the move is simulated: the server
 * (sim positions) and the owning client (its interpolated positions), so prediction mostly agrees; the server
 * also relaxes its error check while a reaper is in contact (ASQCharacter::ServerUpdateMoveTolerance).
 */
/**
 * Movement tech (Megabonk-style), client-predicted:
 *   - Slide: hold Crouch/Slide on the ground while moving fast -> a speed burst that decays (SlideFriction), a crouched
 *     (lower) capsule, slope gravity that builds speed downhill, slow steering. Stays in MOVE_Walking (PhysWalking keeps
 *     floors, steps and ledges); only the velocity rule changes (CalcSlideVelocity).
 *   - Slide-jump keeps the slide's horizontal speed (air has no lateral friction).
 *   - Bunny hop: for HopWindow seconds after landing the landing speed is kept (up to HopMaxSpeed); a jump inside that
 *     window keeps it and adds HopSpeedGain.
 *   - Fast-fall: slide held in the air after the apex = FastFallGravity x gravity.
 *   - Landing with slide held = a roll: no fall damage, and the slide carries on.
 * Prediction: the held input travels in every saved move as FLAG_Custom_0 (FSQSavedMove::GetCompressedFlags ->
 * UpdateFromCompressedFlags on the server); the slide/hop state is saved per move (SetMoveFor) and restored before each
 * replay (PrepMoveFor), so server corrections replay exactly. Every rule runs inside PerformMovement on the server and the
 * owning client alike. Simulated proxies see the slide through the replicated crouch (bIsCrouched).
 */
UCLASS()
class SOULQUOTA_API USQMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSQSavedMove;

public:
	USQMovementComponent(const FObjectInitializer& ObjectInitializer);

	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual bool CanAttemptJump() const override;
	virtual float GetGravityZ() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;

	/** Re-reads the movement keys from Tuning.csv (every machine; called from ASQCharacter::ApplySpeed). */
	void RefreshMoveTuning();
	/** Local input (owning client / listen host): Crouch/Slide held. Sent with each move as FLAG_Custom_0. */
	void SetSlideHeld(bool bHeld) { bSlideHeld = bHeld; }
	bool IsSlideHeld() const { return bSlideHeld; }
	/** Server and owning client: sliding right now (simulated proxies: use the replicated bIsCrouched). */
	bool IsSliding() const { return bSlideActive; }
	/**
	 * Called from ASQCharacter::Landed (server + owning client, inside the move): opens the bunny-hop window, turns a
	 * slide-held landing into a roll (the slide carries on). OutDrop = fall height in cm; bOutSafe = no fall damage
	 * (roll or jump pad). Returns true when the landing was a roll.
	 */
	bool HandleReaperLanded(float& OutDrop, bool& bOutSafe);
	/** A jump pad launch (big upward launch): the landing that follows deals no fall damage. */
	void MarkFallSafe() { bFallDamageSafe = true; }
	/** Teleports (arena safety net): forget the height fallen so far */
	void ResetFallPeak();

protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;

private:
	/** Slide velocity rule (replaces the walking rule while sliding): slope gravity, slow steering, constant decay. */
	void CalcSlideVelocity(float DeltaTime);
	/** Enters the slide: the speed burst (at most once per SlideBoostCooldown). */
	void StartSlide();
	/** The owning reaper may slide / fast-fall now (not downed, not carried) */
	bool ReaperCanSlide() const;

	// --- Predicted state (saved per move by FSQSavedMove) ---
	bool bSlideHeld = false;
	bool bSlideActive = false;
	float SlideBoostCooldownLeft = 0.f;
	float HopWindowLeft = 0.f;
	float HopKeepSpeed = 0.f;
	// --- Server-authoritative fall tracking (only the server applies fall damage) ---
	float FallPeakZ = 0.f;
	bool bFallDamageSafe = false;

	/** Movement keys from Tuning.csv (code defaults here) */
	struct FSQMoveTune
	{
		float SlideEnterSpeed = 380.f;
		float SlideExitSpeed = 300.f;
		float SlideBoost = 400.f;
		float SlideBoostCooldown = 0.8f;
		float SlideMaxSpeed = 2000.f;
		float SlideFriction = 500.f;
		float SlideSlopeGravity = 2.2f;
		float SlideSteerDegrees = 90.f;
		float HopWindow = 0.15f;
		float HopMaxSpeed = 1500.f;
		float HopSpeedGain = 1.03f;
		float FastFallGravity = 2.5f;
	};
	FSQMoveTune MoveTune;
};

/**
 * A reaper. Auto-fires its weapons (server), carries souls on its back,
 * banks them at the lantern, becomes a rolling skull when downed.
 */
UCLASS()
class SOULQUOTA_API ASQCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASQCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	/** Puts back any prop the camera occlusion hid (local) */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;

	// --- Character pass (Nov 25): text barks over the reaper's head (Reapers.csv Bark*), local visuals on every machine ---
	/** Shows one of this reaper's lines for Event (Level, LowHealth, Revive, Combo, Boss, Victory, Pickup), rate limited */
	void Bark(FName Event);
	FString BarkLine;
	double BarkAt = -100.0;
	double BarkNextAllowed = 0.0;
	float BarkPrevHealthFrac = 1.f;
	bool bBarkPrevDowned = false;
	int32 BarkPrevLevel = -1;
	uint8 BarkPrevPhase = 255;
	void TickBarks();
	/** Bunny-hop window, slide-landing roll and (server) fall damage */
	virtual void Landed(const FHitResult& Hit) override;
	/** Jump pads (big upward launches) make the next landing safe from fall damage */
	virtual void LaunchCharacter(FVector LaunchVelocity, bool bXYOverride, bool bZOverride) override;
	/** The ground-level aura disc follows the lowered capsule while sliding */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// --- Components (placeholder clay shapes until real art) ---
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HeadMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ScytheMesh;
	/** Head of the primitive stand-in weapon (blade, axe head, staff orb) when the weapon art is missing */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> WeaponHeadMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> AuraMesh;
	/** Imported animated reaper (KayKit). Hidden when art is missing; primitives are the fallback. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> SkinMesh;
	/** Per-reaper personality prop (lantern, candle...) held in the left hand or worn on the head. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> AccessoryMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> OrbISM;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> SoulStackISM;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> TurretISM;
	/** Reaper kit: passive, E / R abilities (SQAbility.h, Kits.csv) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USQAbilityComponent> Abilities;
	/** Reap core (Nov 9): left-click scythe, Reap Streak, Reaper's Passage (SQReap.h) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USQReapComponent> Reap;

	// --- Replicated state ---
	UPROPERTY(ReplicatedUsing = OnRep_Reaper) FName ReaperId = TEXT("Grim");
	UPROPERTY(ReplicatedUsing = OnRep_Stats) FSQStats Stats;
	UPROPERTY(ReplicatedUsing = OnRep_Health) float Health = 100.f;

	/** Local-only hit feedback (camera shake + red flash), 0..0.35 */
	float HitFlash = 0.f;
	UPROPERTY(Replicated) int32 Carried = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Downed) bool bDowned = false;
	UPROPERTY(Replicated) int32 Pardons = 0;
	UPROPERTY(Replicated) float BankProgress = 0.f;
	UPROPERTY(Replicated) TArray<FSQOwned> Owned;
	UPROPERTY(Replicated) TArray<FVector_NetQuantize> Turrets;
	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> CarriedSkull;
	UPROPERTY(Replicated) TObjectPtr<ASQCharacter> CarriedBy;
	UPROPERTY(Replicated) bool bGodMode = false;
	UPROPERTY(Replicated) int32 ShopBonusCarry = 0;
	UPROPERTY(Replicated) TArray<FName> ActiveBonds;
	/** Damage absorbed before health (Doctor Beak's overheal). Capped at TriageShieldCap x MaxHealth. */
	UPROPERTY(Replicated) float Shield = 0.f;
	/** 0..1 charge of the passive's next timed trigger (Triage, Lookout, Stopwatch, field revive...) for the HUD; 0 = not timed */
	UPROPERTY(Replicated) float PassiveCharge = 0.f;
	/** Co-op level-up strip open: a short protective bubble (no damage). Everyone sees the shimmer. */
	UPROPERTY(Replicated) bool bPickBubble = false;
	/** Reap core, co-op Harvest Revive: souls the team has reaped near this downed reaper (everyone sees it) */
	UPROPERTY(Replicated) int32 HarvestSouls = 0;
	/** Rise from the grave at run start: server time until which this reaper takes no input and no damage */
	UPROPERTY(Replicated) float RiseLockUntil = -1.f;

	// --- Co-op pass (Nov 23 2026, Docs/CoopFeatures.md) ---
	/** Close Ranks: standing allies within SquadAuraRange (capped at SquadAuraMax; a lone reaper counts SquadAuraSolo) */
	UPROPERTY(Replicated) uint8 SquadNear = 0;
	/** Ghost shift: 0..1 charge of the downed reaper's next Haunt (1 = ready) */
	UPROPERTY(Replicated) float GhostHauntCharge = 0.f;
	/** Server: a squad combo this reaper set off burst at At (called by the horde) */
	void OnSquadCombo(const FVector2D& At, float Radius, float Damage);
	/** Local (owning view): world time of the last squad combo near this reaper, and how many chained */
	float ComboFlashAt = -100.f;
	int32 ComboFlashCount = 0;
	/** Server: this reaper brought Revived back: a heal, R charge and a burst of haste for both */
	void RewardRevive(ASQCharacter* Revived);
	/** Ghost shift: downed and not being carried (floats, gathers XP and gold, haunts) */
	bool IsGhost() const { return bDowned && CarriedBy == nullptr; }
	/** Server: Haunt - marks the toughest enemy near the ghost so the crew hits it harder. False while recharging / nothing near. */
	bool TryGhostHaunt();
	/** Ping wheel. Kind: 0 Here, 1 Help me, 2 Lantern, 3 Chest, 4 Danger, 5 Thanks. Rate-limited on the server. */
	UFUNCTION(Server, Unreliable) void ServerPing(uint8 Kind, FVector_NetQuantize Where);
	/** Slot = the pinging player's index on the server (their ping colour on every machine) */
	UFUNCTION(NetMulticast, Unreliable) void MulticastPing(uint8 Kind, FVector_NetQuantize Where, uint8 Slot);

	// --- Reap core (Nov 9): harvest revive + rise lock ---
	/** Souls reaped so far toward reviving this downed reaper (0 when standing) */
	int32 GetHarvestReviveProgress() const { return HarvestSouls; }
	/** Souls the team must reap within HarvestReviveRange of this downed reaper to revive it (Tuning HarvestReviveSouls) */
	int32 GetHarvestReviveNeed() const;
	/** Server: Count souls were reaped near this downed reaper by By; revives it at ReviveFraction once the need is met */
	void AddHarvestSouls(int32 Count, ASQCharacter* By);
	/** Server: no input, no damage for Seconds (the rise-from-the-grave sequence at run start) */
	void StartRiseLock(float Seconds);
	/** Every machine: still rising from the grave (input and damage blocked) */
	bool IsRising() const;

	// --- Reaper passive (Kits.csv; runs in USQAbilityComponent) ---
	/** The kit passive's main number (Kits.csv PassiveP1). Works on every machine. */
	float GetPassiveValue() const;
	FName GetPassiveId() const;
	/** Soul pickup pull range (Fetch passive makes it huge). Server uses it every frame. */
	float GetSoulMagnet() const;
	/** Server: called by the horde for every kill credited to this reaper (Seance ghosts) */
	void OnPassiveKill(const FVector2D& Where, bool bElite);

	// --- Server API ---
	void InitReaper(FName NewReaper);
	void ApplyHit(float Damage);
	int32 AddCarried(int32 Amount);
	void LoseCarried(int32 Amount, bool bDropAsPickups);
	/** Rarity: 0 Common, 1 Rare, 2 Epic, 3 Legendary (rolled on the level-up card; stronger at higher rarity). */
	void ApplyUpgrade(FName Id, uint8 Rarity = 0);
	/** Luck raises the chance of rarer level-up cards. */
	float GetLuck() const;
	bool CanTakeUpgrade(const FSQUpgradeDef& Def) const;
	void Revive(float HealthFraction);
	void RecomputeStats();
	void ReleaseSkull();
	bool IsPardonPending() const { return PardonTimer > 0.f; }
	/**
	 * Server, co-op picks: bStripOpen = this reaper's strip is open with cards up. A new card set (SerialNow changed)
	 * starts a bubble of BubbleSeconds, at most once per BubbleCooldown; closing the strip ends it.
	 */
	void ServerTickPickBubble(bool bStripOpen, uint8 SerialNow, float DeltaSeconds, float BubbleSeconds, float BubbleCooldown);

	int32 GetUpgradeLevel(FName Id) const;
	/** Weapons + evolutions held (each fills one of Tuning WeaponSlots) */
	int32 CountWeapons() const;
	/** Tomes + items held (every Kind == Relic row) */
	int32 CountRelics() const;
	// --- Items/tomes pass (Docs/ItemsAndTomes.md): weapon + tome slots, items stack without slots ---
	int32 CountTomes() const;
	int32 GetWeaponSlots() const;
	int32 GetTomeSlots() const;
	/** "TOMES 3/4" / "WEAPONS 2/4" for a level-up card of this category (empty for evolutions / items / fallbacks) */
	FString SlotText(const FSQUpgradeDef& Def) const;
	/** Stats before SQ::FinalizeStats (caps and diminishing curves); shrine raises are added to these (USQBoostLedger). Server. */
	FSQStats RawStats;
	FVector2D Pos2D() const { const FVector L = GetActorLocation(); return FVector2D(L.X, L.Y); }

	// --- Body blocking (server + owning client; see USQMovementComponent) ---
	/** Clamps a movement velocity against the horde's bodies: no walking into enemies, a soft push out of overlaps. */
	void ApplyBodyBlock(FVector& InOutVelocity, float DeltaTime) const;
	/** Height of the feet above the terrain under the reaper (0 when walking). Enemies shorter than this are jumped over. */
	float GetFeetAboveGround() const;
	/** True for a moment after a dash: the dash passes through enemy bodies (Tuning DashPassSeconds). */
	bool IsDashPassing() const { return DashPassLeft > 0.f; }

	// --- Input-driven (local) ---
	/** PreferredDir (flat, any length): dash this way (cursor / move direction from the controller). Zero = last move input, else facing. */
	void TryDash(const FVector& PreferredDir = FVector::ZeroVector);
	/** Kit abilities: Slot 2 = E, 3 = R; AimPoint = ground under the cursor. Forwards to USQAbilityComponent::TryCast. */
	void TryCastAbility(uint8 Slot, const FVector& AimPoint);

	// --- Top-down camera, facing and aim (camera / controls; the input side is in SQPlayerController.h) ---
	/** Tuning TopDownMode (default 1; the name is historical): the standard controls. Since Nov 12 2026 that is the third-person
	 *  chase camera (mouse look, camera-relative WASD, auto / manual aim). 0 = the old reference 3rd-person controls. */
	static bool IsTopDownMode(const UObject* WorldContext);
	/** The fixed camera yaw (Tuning CameraYaw, deg). Screen up = this world direction. */
	static float GetCameraYaw(const UObject* WorldContext);
	/** Local (owning player): mouse wheel. Steps > 0 zooms in (Tuning CameraZoomStep cm per step, SQSettings::ZoomMin..ZoomMax; saved). */
	void AddCameraZoom(float Steps);
	/** Local: the camera's yaw right now (free camera: the orbit; fixed: GetCameraYaw). WASD and the stick move along it. */
	float GetViewYaw() const;
	/** SETTINGS > Aiming is MANUAL (USQSaveGame::AimMode 1): abilities, the reap and the facing follow the camera's crosshair */
	static bool IsManualAim();
	/** Local: mouse look / right stick (degrees; pitch > 0 tilts toward looking down, SQSettings::TiltMin..TiltMax) */
	void AddCameraOrbit(float YawDeg, float PitchDeg);
	/** The chase camera's tilt now (deg below the horizon) */
	float GetViewTilt() const { return CameraTiltNow; }
	/** Local (Home): camera behind the reaper at the default tilt and distance */
	void ResetCamera();
	/** Local: the controller keeps the camera live while the game is paused (solo SETTINGS), so changes show at once */
	void UpdateCameraWhilePaused(float RealDeltaSeconds) { UpdateTopDownCamera(RealDeltaSeconds); }
	/** Local (owning player): face the cursor for Seconds (ability casts). The controller feeds the facing through the control rotation. */
	void StartAimLock(float Seconds);
	bool IsAimLocked() const { return AimLockLeft > 0.f; }
	/** Crouch/Slide key held (owning machine only; predicted through USQMovementComponent's saved moves) */
	void SetSlideInput(bool bHeld);
	/** Sliding now, on every machine (owner/server: the movement state; remote views: the replicated crouch) */
	bool IsSliding() const;
	/** Not downed, not a carried skull, not frozen in the lobby: the slide and fast-fall may run */
	bool CanSlideNow() const;
	USQMovementComponent* GetSQMovement() const;

	UFUNCTION(Server, Reliable) void ServerDash(FVector Direction);
	UFUNCTION(Server, Reliable) void ServerInteract();

	/** Kind 0 = expanding pulse, 1 = telegraph warning disc */
	UFUNCTION(NetMulticast, Unreliable) void MulticastFX(uint8 Kind, FVector_NetQuantize Location, float Radius, FColor Color, float Duration);

	/** Chain / beam weapons: a jagged line of sparks through Points on every machine (Style 0 = chain, 1 = beam). */
	UFUNCTION(NetMulticast, Unreliable) void MulticastLineFX(const TArray<FVector_NetQuantize>& Points, FColor Color, uint8 Style);

	/**
	 * A weapon fired: every machine plays the attack (anim, weapon swing, hit-stop) and its effect.
	 * Flags: 1 = crit, 2 = play the attack animation, 4 = draw the effect. Points depend on the behaviour
	 * (Arc/Nova: origin + hit spots; Projectile/Beam: muzzle + end of each shot; Drop: origin + drop spots;
	 * Turret: the new turret; Chain: every link). Sent at most WeaponFxPerSecond / AnimActionRateLimit times a second.
	 */
	UFUNCTION(NetMulticast, Unreliable) void MulticastWeaponFire(FName WeaponId, uint8 Flags, float Radius, const TArray<FVector_NetQuantize>& Points);
	/** Local camera kick for the owning player only (Amount ~ seconds of shake, 0..0.5), Delay seconds from now. No-op elsewhere. */
	void NudgeCamera(float Amount, float Delay = 0.f);
	/**
	 * Reap swing body juice on every machine (SQFx::PlayReapAnim calls it): anticipation squash, smear stretch, then at
	 * ImpactIn the impact squash, weapon pop + glow and the owner's camera nudge (Tuning ReapCamNudge).
	 */
	void PlayReapJuice(float ImpactIn);
	/** Batched floating damage numbers (cosmetic, unreliable) */
	UFUNCTION(NetMulticast, Unreliable) void MulticastDamagePops(const TArray<FSQDamagePop>& Pops);
	/**
	 * The reaper's passive just triggered: play A_SQ_Ability_<PassiveId> (KayKit fallback) + a burst in the reaper's colour
	 * on every machine. Variant 0 = normal, 1 = big moment (bigger burst). Sent by QueueAbilityAnim (server, throttled).
	 */
	UFUNCTION(NetMulticast, Unreliable) void MulticastAbilityAnim(uint8 Variant);
	/** The run begins: everyone rises out of the ground at their start spot. */
	UFUNCTION(NetMulticast, Reliable) void MulticastRunStart();
	/** True in the lobby: nobody moves until every player is ready. */
	bool IsFrozenForLobby() const;   // also true while rising from the grave at run start (IsRising): no input either way

	float DashCooldownLeft = 0.f;

protected:
	/** Slide-jump: the slide crouches the capsule, and stock ACharacter forbids jumping while crouched */
	virtual bool CanJumpInternal_Implementation() const override;
	/** Server: fall damage for a landing DropHeight cm below the highest point of the fall (gentle, never downs) */
	void ServerApplyFallDamage(float DropHeight);
	/** Server: a sliding reaper shoves small ground enemies aside (ASQHordeManager::ApplyKnockback, ~10 Hz, Tuning SlideKnock*) */
	void ServerTickSlideKnock(float DeltaSeconds);
	float SlideKnockTimer = 0.f;
	/** Local: slide dust, roll on a slide landing, camera FOV kick and speed lines */
	void UpdateMovementFx(float DeltaSeconds);
	/** Team level this reaper last drew a level-up burst for (-1 until first seen); cosmetic only */
	int32 FxSeenTeamLevel = -1;
	bool bWasSlidingFx = false;
	float SlideDustTimer = 0.f;
	float SpeedLineTimer = 0.f;
	float CameraFovNow = 75.f;
	float LandRollWindow = 0.f;      // a long fall just landed: a slide starting within this window plays the roll
	float SlideMomentumGrace = 0.f;  // slide speed (and the jumps out of it) is not a dash: no dodge anim on speed bursts

	/** Seconds left of the dash's pass-through (set by DoDash on every machine that runs it) */
	float DashPassLeft = 0.f;

	// --- Top-down camera / facing / reticle / occlusion (local unless noted) ---
	/** Every machine, BeginPlay: sets up the boom for TopDownMode (fixed pitch + yaw, no collision, arm lag = smooth follow) */
	void ApplyCameraMode();
	/** Owning player: zoom, fixed rotation, look-ahead toward the cursor (Boom->TargetOffset, smoothed by the arm's lag) */
	void UpdateTopDownCamera(float DeltaSeconds);
	/** Server + owning client: human reapers in top-down turn toward the control rotation (move dir / cursor); bots orient to movement */
	void UpdateFacingMode();
	/** Owning player: hides arena meshes between the camera and any reaper (see FSQOccluder) */
	void UpdateCameraOcclusion(float DeltaSeconds);
	void RebuildOccluderCache(const class ASQArena* ForArena);
	void RestoreAllOccluders();
	bool bTopDownCamera = false;
	float CameraZoomTarget = 1900.f;
	float CameraZoomNow = 1900.f;
	float CameraArmNow = 850.f;       // the arm after the terrain clamp (UpdateTopDownCamera)
	float DownedCamSeconds = 0.f;     // downed this long (the camera drifts to a living teammate after a moment)
	FVector CameraLookAheadNow = FVector::ZeroVector;
	float AimLockLeft = 0.f;
	// Nov 9 settings: the camera reads USQSaveGame (mode, zoom, tilt, smoothing, lean) every frame, so SETTINGS apply live.
	// The aim reticle is drawn by the HUD now (reap cone + cooldown ring at the cursor), not in the world.
	float CameraOrbitYaw = 0.f;      // chase camera yaw (mouse / right stick)
	float CameraTiltNow = 18.f;      // chase camera tilt (deg below the horizon)
	float CameraZoomSaveAt = -1.f;   // the wheel changed the saved zoom: write the profile a moment later
	/** A tall arena mesh (or one instance of a non-colliding ISM) that may hide a reaper. Box = world bounds. */
	struct FSQOccluder
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		int32 Instance = INDEX_NONE;   // INDEX_NONE = the whole component
		FBox Box = FBox(ForceInit);
	};
	/** An occluder hidden right now: whole components via SetHiddenInGame, ISM instances shrunk to ~0 (non-colliding ISMs only) */
	struct FSQFadedOccluder
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		int32 Instance = INDEX_NONE;
		FTransform Original = FTransform::Identity;
		float HoldLeft = 0.f;
	};
	TArray<FSQOccluder> OccluderCache;
	TArray<FSQFadedOccluder> FadedOccluders;
	TWeakObjectPtr<const class ASQArena> OccluderCacheArena;
	int32 OccluderCacheSerial = -1;
	float OcclusionTimer = 0.f;
	/** Server, remote reapers: seconds the relaxed movement check stays on after the last enemy contact */
	float BodyContactGrace = 0.f;
	bool bBodyTolerance = false;
	/** Server: while a remote reaper touches the horde (or dashes), accept its own position instead of correcting it */
	void ServerUpdateMoveTolerance(float DeltaSeconds);
	UFUNCTION() void OnRep_Reaper();
	UFUNCTION() void OnRep_Stats();
	UFUNCTION() void OnRep_Downed();
	UFUNCTION() void OnRep_Health();
	float LastHealthSeen = 100.f;

	void ApplyVisuals();
	/** Nov 25 2026: the skin last applied (ASQPlayerState::SkinId) */
	FName AppliedSkinId;
public:
	/** Re-applies the look when the owner's skin changed (ASQPlayerState::OnRep_SkinId) */
	void RefreshSkinLook();
protected:
	void ApplySpeed();
	void UpdateLocalVisuals(float DeltaSeconds);
	void DoDash(const FVector& Direction);

	void ServerTickWeapons(float DeltaSeconds);
	void ServerTickBanking(float DeltaSeconds);
	void ServerTickSkull(float DeltaSeconds);
	/** Server: puts a reaper that escaped the arena (over the walls) or fell through the terrain back on the ground inside */
	void ServerKeepInArena();
	/** Server: token bucket for weapon effect multicasts (unreliable RPC storm guard). False = skip this effect. */
	bool AllowWeaponFx();
	float WeaponFxTokens = 24.f;
	float WeaponFxStamp = 0.f;
	void FireWeapon(const FSQUpgradeDef& Def, int32 Level, float Power);
	/** Server: attack animation throttle (AnimActionRateLimit; auras and orbits only gesture every few seconds) */
	bool AllowAttackAnim(const FSQUpgradeDef& Def);
	/** Server: sends MulticastWeaponFire when the effect or the animation is allowed */
	void SendWeaponFire(const FSQUpgradeDef& Def, bool bCrit, bool bAnim, float Radius, const TArray<FVector_NetQuantize>& Points);
	/** Server: queue a floating damage number at a 2D spot (merged and flushed in batches by FlushDamagePops) */
	void AddDamagePop(const FVector2D& Where, float Amount, float CritMult, float Delay = 0.f);
	/** Server: one pop per enemy found within Radius of Center (up to MaxPops) */
	void AddAreaPops(const FVector2D& Center, float Radius, float Amount, float CritMult, int32 MaxPops);
	void FlushDamagePops(float DeltaSeconds);
	TArray<FSQDamagePop> PendingPops;
	float PopFlushTimer = 0.f;
	float GetDamageMult() const;
	/** 1, or 1 + CritDamage when this attack crits (rolled once per volley) */
	float RollCritMult() const;
	/** Heals teammates in range by AllyRegen x Seconds (server) */
	void ServerShareRegen(float Seconds);

	struct FPendingDrop
	{
		FVector Location = FVector::ZeroVector;
		float TimeLeft = 0.f;
		float Damage = 0.f;
		float Radius = 0.f;
	};

	/** Server-only data for each entry of the replicated Turrets array (same order) */
	struct FTurretInfo
	{
		FName Source;            // weapon that placed it (each weapon has its own turret limit)
		float Life = 10.f;
		float ShotCooldown = 0.2f;
		float ShotDamage = 10.f;
		float ShotSpeed = 1500.f;
	};

	TMap<FName, float> WeaponCooldowns;
	TArray<FPendingDrop> PendingDrops;
	TArray<FTurretInfo> TurretInfos;
	void RemoveTurretAt(int32 Index);
	float HitInvuln = 0.f;
	float PardonTimer = -1.f;
	// Co-op pick bubble (server) and its shimmer (local)
	float PickBubbleLeft = 0.f;
	float PickBubbleCooldownLeft = 0.f;
	int32 PickBubbleSerial = -1;
	float PickShimmerTimer = 0.f;
	float RegenAccum = 0.f;
	float BondTimer = 0.f;
	void ServerTickBonds(float DeltaSeconds);
	// Co-op pass (server)
	void ServerTickGhost(float DeltaSeconds);
	float GhostHauntLeft = 0.f;
	int32 GhostTrailStageSeen = -1;
	float PingBudget = 3.f;
	float LastPingTime = -100.f;

	// --- Passives: the kit passive runs in USQAbilityComponent (Kits.csv) ---
	/** Damage multiplier for hits on this reaper (Bulwark reapers nearby, Fortify buffs; 1 = none) */
	float IncomingDamageMult() const;
	void UpdateZoneMovement();
	float VisualTime = 0.f;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HeadMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SkinMID;

	// Skinned-art animation state (local visuals only; the pose itself is built by USQAnimInstance)
	bool bHasSkin = false;
	bool bHasWeaponArt = false;
	FName SkinModel;
	float SkinBaseScale = 1.f;
	float LastVisualHealth = -1.f;
	float SkinGlow = 0.f;
	bool bPlayedSpawn = false;
	float LastDashCooldownSeen = 0.f;
	bool bWasDownedVisual = false;
	uint8 LastMoodVisual = 0;
	float LastYawVisual = 0.f;
	float TurnRateVisual = 0.f;
	bool bWasDashSpeed = false;
	// Extra animation life: idle fidgets, banking, level-up cheers, style per reaper
	float IdleFidgetTime = 0.f;
	float NextFidgetAt = 6.f;
	float BankAnimCooldown = 0.f;
	int32 LastTeamLevelSeen = -1;
	FName StyleAppliedFor;
	void ApplyAnimStyle(class USQAnimInstance* SkinAnim);
	float DashAnimLockout = 0.f;
	uint8 ArcSwingCount = 0;
	// Squash & stretch spring on the skinned mesh (+ = stretched tall, - = squashed)
	float SquashValue = 0.f;
	float SquashVelocity = 0.f;
	bool bWasFallingVisual = false;
	float FallTimeVisual = 0.f;
	float FallPeakVisualZ = 0.f;
	// Server: attack animation throttle
	float LastAttackAnimTime = -100.f;
	float LastAuraGestureTime = -100.f;
	// Server: passive ability animation triggers (QueueAbilityAnim throttles to Tuning AbilityAnimMinInterval)
	float LastAbilityAnimTime = -100.f;
	/** Server: ask every machine to play this reaper's ability animation. False when throttled, downed or not the server. */
	bool QueueAbilityAnim(uint8 Variant = 0);
	/** Local: play the ability clip (or its KayKit fallback). Full body when standing still or for in-place passives. */
	bool PlayAbilityAnim(uint8 Variant);
	FName AccessoryReaper;

	USQAnimInstance* GetSkinAnim() const;
	void UpdateSkinAnimation(float DeltaSeconds);
	void UpdateSquash(float DeltaSeconds);
	void ApplyAccessory(FName ForReaper);
	// --- Weapon presentation (local, every machine) ---
	void PlayAttack(const FSQUpgradeDef& Def, bool bCrit);
	void PlayWeaponFx(const FSQUpgradeDef& Def, bool bCrit, float Radius, const TArray<FVector_NetQuantize>& Points);
	void UpdateWeaponVisuals(float DeltaSeconds);
	void UpdatePassiveWeaponFx(float DeltaSeconds);
	/** Builds / sizes the held weapon (art, or a primitive stand-in) in the right hand */
	void ApplyHeldWeapon(const FSQReaperDef* R);
	/** World position of the held weapon's tip (false when no weapon is shown) */
	bool GetWeaponTip(FVector& OutTip) const;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WeaponMID;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> OrbitISMs;
	TArray<FName> OrbitISMWeapons;
	float HeldWeaponLength = 120.f;
	FVector HeldWeaponBaseScale = FVector::OneVector;
	FVector HeldHeadBaseScale = FVector::OneVector;
	float PendingSwingSquash = 0.f;
	float PendingHitStopSeconds = 0.06f;
	bool bPrimitiveWeapon = false;
	float WeaponTrailTime = 0.f;
	FLinearColor WeaponTrailColor = FLinearColor::White;
	FVector LastTipPos = FVector::ZeroVector;
	bool bLastTipValid = false;
	float WeaponPop = 0.f;
	float WeaponGlow = 0.f;
	float HitStopAt = -1.f;      // seconds until the swing's impact moment (hit-stop + squash)
	float AttackShake = 0.f;     // local camera kick on crits
	float AuraFxTimer = 0.f;
	float OrbTrailTimer = 0.f;
	float HealPopAccum = 0.f;
	float HealPopTimer = 0.f;
	float LastHealthForPops = -1.f;
	FVector VisualVelocity = FVector::ZeroVector;
	FVector LastVisualLocation = FVector::ZeroVector;
	bool bHasLastVisualLocation = false;
	uint8 AttackComboCount = 0;
	bool PlayOneShot(FName AnimName, float Rate = 1.f, bool bFullBody = true, float BlendIn = 0.08f);
	// --- Attack juice (Attack storyboard: anticipation, smear, impact, follow-through, settle), local, every machine ---
	/** Family: 0 Slash, 1 Slam, 2 Thrust, 3 Cast, 4 Throw (from Upgrades.csv AttackStyle). ImpactIn = seconds to the blow. */
	void StartSwingJuice(uint8 Family, float ImpactIn, bool bCrit, bool bReap);
	void TickSwingJuice(float DeltaSeconds);
	float SwingClock = -1.f;     // seconds since the swing started (-1 = no swing)
	float SwingSmearAt = 0.f;
	float SwingImpactAt = 0.f;
	float SwingFollowAt = 0.f;
	float SwingEndAt = 0.f;
	uint8 SwingFamily = 0;
	uint8 SwingBeats = 0;        // bit 0 smear, bit 1 impact, bit 2 follow-through played
	bool bSwingCrit = false;
	bool bSwingReap = false;
	bool bSwingFxDrawn = false;  // the weapon effect (PlayWeaponFx) already drew this swing's smear + impact (Arc weapons)
	float SwingSign = 1.f;       // alternates the slash direction
	float CamNudgeIn = -1.f;
	float CamNudgeAmount = 0.f;
};
