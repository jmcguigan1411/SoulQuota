// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SQPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class USQSaveGame;

/**
 * Input is built in code (no input assets needed).
 * TopDownMode 1 (default, Tuning.csv; the name is historical), Nov 12 2026 chase-camera controls (listed in SETTINGS > CONTROLS):
 *   WASD move (camera-relative: W = away from the chase camera), MOUSE look (orbits the camera; cursor hidden and captured in a fight)
 *   SPACE jump (hold to hop), CTRL / C slide (Tuning ChaseJump 0 puts the dash back on Space and drops both)
 *   SHIFT / R-CLICK dash (the way you're moving; standing still: the way the camera looks)
 *   L-CLICK Reap (USQReapComponent::TryReap at the aim point)
 *   E ability (team level 3), R ultimate (team level 6 + a full soul charge); a locked / uncharged press says why (local message)
 *   F interact (chests, bell, skulls), P settings (pauses in solo, opens over the game in co-op; Esc closes it too)
 *   WHEEL camera distance, HOME reset camera, hold LEFT ALT to free the cursor (co-op card strip, tip links)
 *   Aiming (SETTINGS): AUTO (default) = the nearest enemy, else the move / camera direction; MANUAL = the crosshair at the
 *   centre of the screen, and the reaper faces the way the camera looks.
 *   Gamepad: left stick move, right stick look, A jump, RB dash, R3 slide, X interact, LT / RT abilities.
 * TopDownMode 0 (reference): WASD move, mouse look, Space jump, Shift dash, E interact, 1-6 choose, R ready.
 * Lobby (Waiting, nobody can move): the HUD's reaper grid takes arrows / A D / D-pad + Enter / Space / A and clicks;
 * 1-9 quick-pick the first nine reapers (the D-pad browses the grid there instead of picking).
 * Level-up picking, solo (one human; the world freezes): 1/2/3 or D-pad left/up/right or click a card,
 * Q or R / gamepad Y reroll, X / gamepad B skip.
 * Level-up picking, co-op (2+ humans; never freezes): a small strip at the bottom. It starts as a "LEVEL UP (N)" pill
 * in a fight; 1/2/3 or Tab expands it (or it opens by itself with no enemy within PickStripEnemyRange), then 1/2/3 pick,
 * Q or R reroll, X skip. Movement, aim and dash keep working; click a card.
 * Debug console commands start with SQ (press ~).
 */
UCLASS()
class SOULQUOTA_API ASQPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	/** Never leaves the game paused by the settings overlay */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Number keys / clicks: reaper pick in the lobby, buy in the shop, otherwise a level-up card */
	void Choose(int32 Index);
	/** Level-up cards: roll a fresh set (limited per run) / pass on this set for a little health */
	void Reroll();
	void Skip();
	/** The local player has level-up cards up (solo: the big frozen screen; co-op: the strip) */
	bool IsPicking() const;
	/** One human in the game (bots don't count): level-ups freeze the world with the big cards */
	bool IsSoloPick() const;
	/** Co-op strip state (local): expanded = the 3 small cards are up and 1/2/3 pick */
	bool IsPickStripExpanded() const { return bStripExpanded; }
	/** Co-op strip: the cards can be clicked (top-down: whenever the strip is expanded; TopDownMode 0: Left Alt held) */
	bool IsPickCursorHeld() const { return bStripCursor; }
	// Ping wheel (Nov 23 2026): tap middle mouse / left stick click = "Here" where you look; hold = radial (drag or right stick)
	bool IsPingWheelOpen() const { return bPingWheel; }
	/** 0 = Here (no drag yet), 1 Help me, 2 Lantern, 3 Chest, 4 Danger, 5 Thanks */
	uint8 GetPingWheelPick() const;
	UFUNCTION(Server, Reliable) void ServerSetPickStripOpen(bool bOpen);
	UFUNCTION(Server, Reliable) void ServerSetAutoPick(bool bOn);
	/** The run hasn't started (Waiting phase): the reaper grid is up and the cursor is shown */
	bool IsInLobby() const;

	/** Serial = the offer set the choice was made on (stale or doubled clicks are ignored) */
	UFUNCTION(Server, Reliable) void ServerChoose(int32 Index, uint8 Serial);
	UFUNCTION(Server, Reliable) void ServerReroll(uint8 Serial);
	UFUNCTION(Server, Reliable) void ServerSkip(uint8 Serial);
	UFUNCTION(Server, Reliable) void ServerBuy(int32 Index);
	UFUNCTION(Server, Reliable) void ServerReady();
	/** End screen (host only): bRetry = same reapers and mission again right now; otherwise back to the lobby */
	UFUNCTION(Server, Reliable) void ServerEndChoice(bool bRetry);
	/** The Long Haunt: the host's answer after a boss (true = KEEP REAPING). Ignored from anyone but the host. */
	UFUNCTION(Server, Reliable) void ServerLoopChoice(bool bKeepGoing);
	/** True for the player who may press RETRY / CHANGE REAPER on the end screen (the host, or a solo player) */
	bool CanChooseAfterRun() const;
	UFUNCTION(Server, Reliable) void ServerPickReaper(int32 Index);
	UFUNCTION(Server, Reliable) void ServerLobbyCommand(FName Command);
	UFUNCTION(Client, Reliable) void ClientRunEnded(FName BiomeId, bool bWonStory, int32 NightsCleared, int32 Kills, bool bEndless);

	/** Local profile (overtime pay, unlocks, contracts). Loaded on the owning machine only. */
	USQSaveGame* GetProfile();

	/** Local-only message shown by the HUD (unlock feedback, pay earned). Time is world REAL time (the pick pause freezes world time). */
	FString LocalMessage;
	float LocalMessageTime = -100.f;
	void ShowLocalMessage(const FString& Message);
	bool bSackFullHinted = false;
	UFUNCTION(Server, Reliable) void ServerCheat(FName Command, int32 Number, const FString& Text);

	// ---- Debug console commands ----
	UFUNCTION(Exec) void SQSpawn(int32 Count = 200);
	UFUNCTION(Exec) void SQGod();
	UFUNCTION(Exec) void SQSouls(int32 Amount = 50);
	UFUNCTION(Exec) void SQXP(int32 Amount = 50);
	UFUNCTION(Exec) void SQSkip();
	UFUNCTION(Exec) void SQAddBot();
	UFUNCTION(Exec) void SQReaper(const FString& Name);
	UFUNCTION(Exec) void SQReload();
	UFUNCTION(Exec) void SQHost();
	UFUNCTION(Exec) void SQJoin(const FString& Address);
	UFUNCTION(Exec) void SQStats();
	/** Host only: draws enemy body (green / blue) and reach (red) circles and the reapers' body circles (yellow) */
	UFUNCTION(Exec) void SQShowCollision();
	/** Starts the Training Shift now in a run, or arms it for the next run (SQTutorial 1 also resets every tutorial flag first) */
	UFUNCTION(Exec) void SQTutorial(int32 ResetFlags = 0);

	// ---- Tutorial (SQTutorial.h)
	/** Tips / the Training Shift card are on screen in a run and can be clicked (top-down: always, the cursor is shown; TopDownMode 0: Left Alt held) */
	bool IsTipCursorHeld() const;

	// ---- Settings overlay (P; drawn by ASQHUD with FSQSettingsPanel)
	bool IsSettingsOpen() const { return bSettingsOpen; }
	/** Opens / closes it. Solo in a fight: pauses the game while open (engine pause; ASQGameMode's pick-pause dilation is left alone). */
	void SetSettingsOpen(bool bOpen);

	bool bShowStats = true;

	// ---- Mouse aim (shared API: abilities aim here) ----
	/**
	 * Where the player is aiming, on the ground (local controller only; false when there is no pawn).
	 * Mouse: the cursor deprojected onto the terrain (ASQArena height field), falling back to the plane at the pawn's feet.
	 * Gamepad: right stick direction x Tuning AimStickRange. Neither used recently (Tuning AimIdleSeconds / AutoAimMouseIdleSeconds):
	 * the nearest enemy within AutoAimRange (Tuning AutoAimFallback 1), else straight ahead. Not range-clamped.
	 */
	bool GetAimPoint(FVector& OutWorld) const;
	/** GetAimPoint pulled in to at most MaxRange (2D, cm) from the pawn, Z on the ground there. MaxRange <= 0 = no clamp. Zero vector without a pawn. */
	FVector GetAimPointClamped(float MaxRange) const;
	/** SETTINGS > Aiming is MANUAL (and we are in a run): the aim point is the camera's crosshair, not the nearest enemy */
	bool HasManualAim() const;

	/**
	 * Ability slots sent to ASQCharacter::TryCastAbility. Same numbering as the kit views the HUD reads
	 * (USQAbilityComponent::GetSlotViews: 0 signature weapon, 1 passive, 2 = E, 3 = R). Change here if the ability component differs.
	 */
	static constexpr uint8 AbilitySlotE = 2;
	static constexpr uint8 AbilitySlotR = 3;

	/** Platform pass: maps every action again from the profile's binds and dead zone (after SETTINGS > CONTROLS changes) */
	void RebuildBindings();

protected:
	void AddMappingContext();
	void BuildMappings();
	/** Gamepad buttons whose meaning depends on the screen: MENU, LB, VIEW, Y, B, RS click, the pad's reap (polled) */
	void UpdatePadButtons();
	/** Controller rumble on hits / downs / level-ups */
	void UpdatePlatformFeel(float DeltaTime);
	void ClearSlideLatch();
	bool bSlideLatched = false;
	float StickRestFor = 0.f;
	float FeelHealthSeen = -1.f;
	bool bFeelDownedSeen = false;
	int32 FeelLevelSeen = 0;
	double PerfLastRealTime = 0.0;
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJumpStart();
	void OnJumpStop();
	void OnDash();
	void OnSlideStart();
	void OnSlideStop();
	void OnInteract();
	// Top-down
	void OnAbilityE();
	void OnAbilityRPad();
	/** Left mouse: Reap at the cursor */
	void OnReap();
	/** Right mouse: dash toward the cursor */
	void OnDashMouse();
	/** Space / Shift / pad (bTowardCursor false) or right mouse (true) */
	void DoDashInput(bool bForceCursor);
	void OnSettingsKey();
	void OnResetCamera();
	/** Local: the paused-camera update (solo SETTINGS) */
	void UpdateCameraInput(float DeltaTime);
	/** E / R pressed but nothing can happen: says why (locked until level N, R charging), at most every 1.5 s */
	void ExplainCastBlocked(uint8 Slot);
	/** The cursor is over a HUD button / card / tip (last frame's HUD layout) */
	bool IsCursorOverHud() const;
	bool bSettingsOpen = false;
	bool bSettingsPausedGame = false;
	float CastHintAt = -100.f;
	/** E / R / gamepad triggers: aim-lock the facing and hand the cursor point to the ability component */
	void CastAbility(uint8 Slot);
	void OnAimStick(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	/** Local, every tick: the right stick orbits the chase camera */
	void UpdateAimInput(float DeltaTime);
	/** Local, every tick: control rotation yaw = move direction, the cursor while aim-locked, the dash direction just after a dash */
	void UpdateTopDownFacing(float DeltaTime);
	/** The world forward / right of the camera (flat; the free camera's orbit yaw when it is on) */
	void GetCameraAxes(FVector& OutForward, FVector& OutRight) const;
	/** Ground height for aiming (terrain height field, else FallbackZ) */
	float AimGroundZ(float X, float Y, float FallbackZ) const;
	bool bTopDownInput = false;
	FVector MoveInputWorld = FVector::ZeroVector;   // this frame's WASD / stick in world space (top-down facing)
	FVector2D AimStickRaw = FVector2D::ZeroVector;  // right stick, set while held
	float DashFaceLeft = 0.f;
	float DashFaceYaw = 0.f;
	bool bSoloPickCentred = false;
	void OnChoose1();
	void OnChoose2();
	void OnChoose3();
	void OnChoose4();
	void OnChoose5();
	void OnChoose6();
	void OnChoose7();
	void OnChoose8();
	void OnChoose9();
	/** Number-key choose: in the lobby only the digit key itself picks (the D-pad shares actions 1-4 but browses the grid there) */
	void ChooseByKey(int32 Index, const FKey& DigitKey);
	void OnBiome();
	void OnEndless();
	void OnReady();
	void OnReroll();
	void OnSkip();
	/** Shows / hides the cursor and swaps the input mode: on while the pick UI is open and all through the lobby */
	void UpdatePickCursor();
	bool bPickCursorOn = false;
	/** Co-op strip: collapse / expand (Tab, number keys, or calm), tell the server when it opens (bubble) */
	void UpdatePickStrip();
	bool bStripExpanded = false;
	bool bStripOpenSent = false;
	bool bStripCursor = false;
	/** Ping wheel input (polled: no input action needed) */
	void UpdatePing(float DeltaTime);
	bool bPingHeld = false;
	bool bPingWheel = false;
	float PingHeldFor = 0.f;
	FVector2D PingDrag = FVector2D::ZeroVector;

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> JumpAction;
	UPROPERTY() TObjectPtr<UInputAction> DashAction;
	UPROPERTY() TObjectPtr<UInputAction> SlideAction;
	UPROPERTY() TObjectPtr<UInputAction> InteractAction;
	UPROPERTY() TObjectPtr<UInputAction> Choose1Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose2Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose3Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose4Action;
	UPROPERTY() TObjectPtr<UInputAction> ReadyAction;
	UPROPERTY() TObjectPtr<UInputAction> Choose5Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose6Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose7Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose8Action;
	UPROPERTY() TObjectPtr<UInputAction> Choose9Action;
	UPROPERTY() TObjectPtr<UInputAction> BiomeAction;
	UPROPERTY() TObjectPtr<UInputAction> EndlessAction;
	UPROPERTY() TObjectPtr<UInputAction> RerollAction;
	UPROPERTY() TObjectPtr<UInputAction> SkipAction;
	UPROPERTY() TObjectPtr<UInputAction> AbilityEAction;
	UPROPERTY() TObjectPtr<UInputAction> AbilityRPadAction;
	UPROPERTY() TObjectPtr<UInputAction> AimStickAction;
	UPROPERTY() TObjectPtr<UInputAction> ZoomAction;
	UPROPERTY() TObjectPtr<UInputAction> ReapAction;
	UPROPERTY() TObjectPtr<UInputAction> DashMouseAction;
	UPROPERTY() TObjectPtr<UInputAction> SettingsAction;
	UPROPERTY() TObjectPtr<UInputAction> ResetCameraAction;
	UPROPERTY() TObjectPtr<USQSaveGame> Profile;
};
