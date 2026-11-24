// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "SQCharacter.h"
#include "SQGameMode.h"
#include "SQGameState.h"
#include "SQPlayerState.h"
#include "SQHordeManager.h"
#include "SQData.h"
#include "SQSave.h"
#include "TimerManager.h"
#include "SQTutorial.h"
#include "SQArena.h"
#include "SQAbility.h"
#include "SQHUD.h"
#include "SQReap.h"
#include "SQInput.h"
#include "SQPerf.h"
#include "Engine/World.h"

void ASQPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bTopDownInput = ASQCharacter::IsTopDownMode(this);
	if (IsLocalController())
	{
		// The control rotation is the reaper's wanted facing (UpdateTopDownFacing), never the camera (the chase camera orbits
		// through ASQCharacter::AddCameraOrbit). The cursor comes on with the first UpdatePickCursor (lobby, cards, menus).
		SetControlRotation(bTopDownInput ? FRotator(0.f, ASQCharacter::GetCameraYaw(this), 0.f) : FRotator(-45.f, 0.f, 0.f));
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		AddMappingContext();
		// Graphics options: first-launch hardware check (once per profile), then push the profile to the engine and the
		// local cosmetic Tuning overrides (SQPerf)
		if (USQSaveGame* GfxProfile = GetProfile())
		{
			SQPerf::AutoDetectOnce(GetWorld(), GfxProfile);
			SQPerf::Apply(GetWorld(), GfxProfile);
		}

		// Use the reaper picked in the main menu (after the player state has replicated)
		FTimerHandle PreferredTimer;
		GetWorldTimerManager().SetTimer(PreferredTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const USQData* Data = USQData::Get(this);
			const USQSaveGame* PrefProfile = GetProfile();
			if (!Data || !PrefProfile || PrefProfile->PreferredReaper.IsNone()) return;
			for (int32 i = 0; i < Data->Reapers.Num(); ++i)
			{
				if (Data->Reapers[i].Id == PrefProfile->PreferredReaper)
				{
					ServerPickReaper(i);
					break;
				}
			}
		}), 0.75f, false);

		// "Auto-pick level-ups" (SETTINGS): the server picks the rarest card for us at once
		if (const USQSaveGame* PickProfile = GetProfile())
		{
			if (PickProfile->bAutoPickLevelUps) ServerSetAutoPick(true);
		}
	}
}

void ASQPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	bTopDownInput = ASQCharacter::IsTopDownMode(this); // may run before BeginPlay

	auto MakeAction = [this](EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this);
		A->ValueType = Type;
		return A;
	};

	Context = NewObject<UInputMappingContext>(this);
	MoveAction = MakeAction(EInputActionValueType::Axis2D);
	LookAction = MakeAction(EInputActionValueType::Axis2D);
	JumpAction = MakeAction(EInputActionValueType::Boolean);
	DashAction = MakeAction(EInputActionValueType::Boolean);
	SlideAction = MakeAction(EInputActionValueType::Boolean);
	InteractAction = MakeAction(EInputActionValueType::Boolean);
	Choose1Action = MakeAction(EInputActionValueType::Boolean);
	Choose2Action = MakeAction(EInputActionValueType::Boolean);
	Choose3Action = MakeAction(EInputActionValueType::Boolean);
	Choose4Action = MakeAction(EInputActionValueType::Boolean);
	ReadyAction = MakeAction(EInputActionValueType::Boolean);
	Choose5Action = MakeAction(EInputActionValueType::Boolean);
	Choose6Action = MakeAction(EInputActionValueType::Boolean);
	Choose7Action = MakeAction(EInputActionValueType::Boolean);
	Choose8Action = MakeAction(EInputActionValueType::Boolean);
	Choose9Action = MakeAction(EInputActionValueType::Boolean);
	BiomeAction = MakeAction(EInputActionValueType::Boolean);
	EndlessAction = MakeAction(EInputActionValueType::Boolean);
	RerollAction = MakeAction(EInputActionValueType::Boolean);
	SkipAction = MakeAction(EInputActionValueType::Boolean);
	AbilityEAction = MakeAction(EInputActionValueType::Boolean);
	AbilityRPadAction = MakeAction(EInputActionValueType::Boolean);
	AimStickAction = MakeAction(EInputActionValueType::Axis2D);
	ZoomAction = MakeAction(EInputActionValueType::Axis1D);
	ReapAction = MakeAction(EInputActionValueType::Boolean);
	DashMouseAction = MakeAction(EInputActionValueType::Boolean);
	SettingsAction = MakeAction(EInputActionValueType::Boolean);
	ResetCameraAction = MakeAction(EInputActionValueType::Boolean);
	// Mouse buttons stay readable by the HUD (WasInputKeyJustPressed for card / tip / settings clicks)
	ReapAction->bConsumeInput = false;
	DashMouseAction->bConsumeInput = false;
	// P must also close the overlay while the solo game is paused
	SettingsAction->bTriggerWhenPaused = true;

	BuildMappings();
	const USQData* BindData = USQData::Get(this);
	const bool bChaseJump = !BindData || BindData->Tune(TEXT("ChaseJump"), 1.f) >= 0.5f;

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASQPlayerController::OnMove);
		if (bTopDownInput)
		{
			EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASQPlayerController::OnLook);
			EIC->BindAction(AimStickAction, ETriggerEvent::Triggered, this, &ASQPlayerController::OnAimStick);
			EIC->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ASQPlayerController::OnZoom);
			EIC->BindAction(AbilityEAction, ETriggerEvent::Started, this, &ASQPlayerController::OnAbilityE);
			EIC->BindAction(AbilityRPadAction, ETriggerEvent::Started, this, &ASQPlayerController::OnAbilityRPad);
			EIC->BindAction(ReapAction, ETriggerEvent::Started, this, &ASQPlayerController::OnReap);
			EIC->BindAction(DashMouseAction, ETriggerEvent::Started, this, &ASQPlayerController::OnDashMouse);
			EIC->BindAction(ResetCameraAction, ETriggerEvent::Started, this, &ASQPlayerController::OnResetCamera);
			if (bChaseJump)
			{
				EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ASQPlayerController::OnJumpStart);
				EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ASQPlayerController::OnJumpStop);
				EIC->BindAction(SlideAction, ETriggerEvent::Started, this, &ASQPlayerController::OnSlideStart);
				EIC->BindAction(SlideAction, ETriggerEvent::Completed, this, &ASQPlayerController::OnSlideStop);
				EIC->BindAction(SlideAction, ETriggerEvent::Canceled, this, &ASQPlayerController::OnSlideStop);
			}
		}
		else
		{
			EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASQPlayerController::OnLook);
			EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ASQPlayerController::OnJumpStart);
			EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ASQPlayerController::OnJumpStop);
			// Slide is held: Started = down, Completed / Canceled = up (both, so a lost release never leaves it stuck on)
			EIC->BindAction(SlideAction, ETriggerEvent::Started, this, &ASQPlayerController::OnSlideStart);
			EIC->BindAction(SlideAction, ETriggerEvent::Completed, this, &ASQPlayerController::OnSlideStop);
			EIC->BindAction(SlideAction, ETriggerEvent::Canceled, this, &ASQPlayerController::OnSlideStop);
		}
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &ASQPlayerController::OnDash);
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &ASQPlayerController::OnInteract);
		EIC->BindAction(Choose1Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose1);
		EIC->BindAction(Choose2Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose2);
		EIC->BindAction(Choose3Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose3);
		EIC->BindAction(Choose4Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose4);
		EIC->BindAction(ReadyAction, ETriggerEvent::Started, this, &ASQPlayerController::OnReady);
		EIC->BindAction(Choose5Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose5);
		EIC->BindAction(Choose6Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose6);
		EIC->BindAction(Choose7Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose7);
		EIC->BindAction(Choose8Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose8);
		EIC->BindAction(Choose9Action, ETriggerEvent::Started, this, &ASQPlayerController::OnChoose9);
		EIC->BindAction(BiomeAction, ETriggerEvent::Started, this, &ASQPlayerController::OnBiome);
		EIC->BindAction(EndlessAction, ETriggerEvent::Started, this, &ASQPlayerController::OnEndless);
		EIC->BindAction(RerollAction, ETriggerEvent::Started, this, &ASQPlayerController::OnReroll);
		EIC->BindAction(SkipAction, ETriggerEvent::Started, this, &ASQPlayerController::OnSkip);
		EIC->BindAction(SettingsAction, ETriggerEvent::Started, this, &ASQPlayerController::OnSettingsKey);
	}
	else
	{
		UE_LOG(LogSQ, Error, TEXT("InputComponent is not an EnhancedInputComponent - check Config/DefaultInput.ini"));
	}

	AddMappingContext();
}

void ASQPlayerController::BuildMappings()
{
	// Platform pass (Nov 23 2026): every rebindable action maps through SQInput::Key (profile rebinds, else the defaults), so
	// RebuildBindings() makes a rebind live. Movement, the camera, the card keys and the second keys (C slide, right-click dash) are fixed.
	// Gamepad buttons that only mean something outside a fight (LB biome, VIEW endless, MENU ready / settings, Y reroll, B skip)
	// and the pad's reap / map are polled in UpdatePadButtons, so no gamepad button is ever mapped to two actions here.
	if (!Context) return;
	Context->UnmapAll();
	const USQSaveGame* BindSave = GetProfile();
	const float DeadZone = BindSave ? FMath::Clamp(BindSave->StickDeadZone, SQSettings::DeadMin, SQSettings::DeadMax) : 0.2f;

	// WASD -> 2D axis (W/S on Y via swizzle, A/S negated)
	Context->MapKey(MoveAction, EKeys::D);
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(MoveAction, EKeys::A);
		M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(MoveAction, EKeys::W);
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	}
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(MoveAction, EKeys::S);
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}
	{
		FEnhancedActionKeyMapping& MoveStick = Context->MapKey(MoveAction, EKeys::Gamepad_Left2D);
		UInputModifierDeadZone* MoveDeadZone = NewObject<UInputModifierDeadZone>(this);
		MoveDeadZone->LowerThreshold = DeadZone;
		MoveStick.Modifiers.Add(MoveDeadZone);
	}

	const USQData* BindData = USQData::Get(this);
	const bool bChaseJump = !BindData || BindData->Tune(TEXT("ChaseJump"), 1.f) >= 0.5f;
	auto MapBoth = [this](UInputAction* Action, SQInput::EAct Act)
	{
		Context->MapKey(Action, SQInput::Key(Act, false));
		Context->MapKey(Action, SQInput::Key(Act, true));
	};
	if (bTopDownInput)
	{
		// Chase camera (Nov 12 2026): MOUSE / right stick look, SPACE jump, SHIFT / R-click dash, CTRL / C slide, L-click = Reap,
		// F = interact, E / R = abilities (R through ReadyAction, see OnReady), P = settings, wheel = camera distance, Home = reset camera.
		Context->MapKey(LookAction, EKeys::Mouse2D);
		{
			FEnhancedActionKeyMapping& StickMap = Context->MapKey(AimStickAction, EKeys::Gamepad_Right2D);
			UInputModifierDeadZone* StickDeadZone = NewObject<UInputModifierDeadZone>(this);
			StickDeadZone->LowerThreshold = DeadZone;
			StickMap.Modifiers.Add(StickDeadZone);
		}
		Context->MapKey(ZoomAction, EKeys::MouseWheelAxis);
		if (bChaseJump)
		{
			MapBoth(JumpAction, SQInput::EAct::Jump);
			MapBoth(SlideAction, SQInput::EAct::Slide);
			Context->MapKey(SlideAction, EKeys::C);
		}
		else
		{
			Context->MapKey(DashAction, SQInput::Key(SQInput::EAct::Jump, false));
			Context->MapKey(DashAction, SQInput::Key(SQInput::EAct::Jump, true));
		}
		MapBoth(DashAction, SQInput::EAct::Dash);
		MapBoth(InteractAction, SQInput::EAct::Interact);
		MapBoth(AbilityEAction, SQInput::EAct::AbilityE);
		Context->MapKey(AbilityRPadAction, SQInput::Key(SQInput::EAct::AbilityR, true));
		Context->MapKey(ReapAction, SQInput::Key(SQInput::EAct::Reap, false));
		Context->MapKey(DashMouseAction, EKeys::RightMouseButton);
		Context->MapKey(ResetCameraAction, EKeys::Home);
	}
	else
	{
		// TopDownMode 0 (reference): the old 3rd-person controls
		Context->MapKey(LookAction, EKeys::Mouse2D);
		Context->MapKey(LookAction, EKeys::Gamepad_Right2D);
		Context->MapKey(JumpAction, EKeys::SpaceBar);
		Context->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
		Context->MapKey(DashAction, EKeys::LeftShift);
		Context->MapKey(DashAction, EKeys::Gamepad_RightShoulder);
		Context->MapKey(SlideAction, EKeys::C);
		Context->MapKey(SlideAction, EKeys::LeftControl);
		Context->MapKey(SlideAction, EKeys::Gamepad_LeftTrigger);
		Context->MapKey(InteractAction, EKeys::E);
		Context->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
	}
	Context->MapKey(Choose1Action, EKeys::One);
	Context->MapKey(Choose1Action, EKeys::Gamepad_DPad_Left);
	Context->MapKey(Choose2Action, EKeys::Two);
	Context->MapKey(Choose2Action, EKeys::Gamepad_DPad_Up);
	Context->MapKey(Choose3Action, EKeys::Three);
	Context->MapKey(Choose3Action, EKeys::Gamepad_DPad_Right);
	Context->MapKey(Choose4Action, EKeys::Four);
	Context->MapKey(Choose4Action, EKeys::Gamepad_DPad_Down);
	// Ready (lobby / Death's Office) shares the ultimate's key: R by default
	Context->MapKey(ReadyAction, bTopDownInput ? SQInput::Key(SQInput::EAct::AbilityR, false) : FKey(EKeys::R));
	Context->MapKey(Choose5Action, EKeys::Five);
	Context->MapKey(Choose6Action, EKeys::Six);
	Context->MapKey(Choose7Action, EKeys::Seven);
	Context->MapKey(Choose8Action, EKeys::Eight);
	Context->MapKey(Choose9Action, EKeys::Nine);
	Context->MapKey(BiomeAction, EKeys::B);
	Context->MapKey(EndlessAction, EKeys::O);
	Context->MapKey(RerollAction, EKeys::Q);
	Context->MapKey(SkipAction, EKeys::X);
	Context->MapKey(SettingsAction, SQInput::Key(SQInput::EAct::Settings, false));
}

void ASQPlayerController::RebuildBindings()
{
	if (!IsLocalController()) return;
	BuildMappings();
	AddMappingContext();
	FlushPressedKeys(); // nothing stays "held" across the change
	bSlideLatched = false;
}

void ASQPlayerController::UpdatePadButtons()
{
	// Gamepad buttons whose meaning depends on the screen (see BuildMappings). Polled, so they also work while the solo game is paused.
	const ASQHUD* PadHud = Cast<ASQHUD>(GetHUD());
	if (PadHud && PadHud->IsCapturingKey()) return; // SETTINGS > CONTROLS is waiting for a button
	const UWorld* PadWorld = GetWorld();
	const ASQGameState* PadGS = PadWorld ? PadWorld->GetGameState<ASQGameState>() : nullptr;
	if (!PadGS) return;
	const bool bFight = PadGS->Phase == ESQPhase::Night || PadGS->Phase == ESQPhase::Boss;
	const bool bPreRun = PadGS->Phase == ESQPhase::Waiting || PadGS->Phase == ESQPhase::Shop;
	const FKey PadSettings = SQInput::Key(SQInput::EAct::Settings, true);
	if (bSettingsOpen)
	{
		// B closes it from the panel (FSQSettingsPanel, bBack); MENU closes it too
		if (WasInputKeyJustPressed(PadSettings)) SetSettingsOpen(false);
		return;
	}
	if (bPreRun)
	{
		// Lobby / Death's Office: MENU = ready, LB = biome, VIEW = endless (host), RS click = settings
		if (WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)) OnReady();
		if (WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder)) OnBiome();
		if (WasInputKeyJustPressed(EKeys::Gamepad_Special_Left)) OnEndless();
		// LS click = Daily Haunt (host, lobby only; the game mode ignores it elsewhere)
		if (WasInputKeyJustPressed(EKeys::Gamepad_LeftThumbstick) && PadGS->Phase == ESQPhase::Waiting) ServerLobbyCommand(TEXT("Daily"));
		if (WasInputKeyJustPressed(EKeys::Gamepad_RightThumbstick)) SetSettingsOpen(true);
		return;
	}
	if (IsPicking() && (IsSoloPick() || bStripExpanded))
	{
		// level-up cards: Y = reroll, B = skip (d-pad picks, see the Choose actions)
		if (WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top)) OnReroll();
		if (WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right)) OnSkip();
		if (IsSoloPick()) return;
	}
	if (WasInputKeyJustPressed(PadSettings)) SetSettingsOpen(true);
	if (bFight && bTopDownInput && WasInputKeyJustPressed(SQInput::Key(SQInput::EAct::Reap, true))) OnReap();
}

void ASQPlayerController::UpdatePlatformFeel(float DeltaTime)
{
	// Rumble (gamepad only, SETTINGS > Controller vibration): taking a hit, going down, a level-up
	const ASQCharacter* FeelPawn = GetPawn<ASQCharacter>();
	if (FeelPawn)
	{
		const float HealthNow = FeelPawn->Health;
		if (FeelHealthSeen >= 0.f && HealthNow < FeelHealthSeen - 0.5f && !FeelPawn->IsFrozenForLobby())
		{
			const float MaxHealth = FMath::Max(1.f, FeelPawn->Stats.MaxHealth);
			SQInput::Rumble(this, FMath::Clamp(0.25f + 2.5f * (FeelHealthSeen - HealthNow) / MaxHealth, 0.25f, 1.f), 0.18f);
		}
		FeelHealthSeen = HealthNow;
		if (FeelPawn->bDowned && !bFeelDownedSeen) SQInput::Rumble(this, 1.f, 0.6f);
		bFeelDownedSeen = FeelPawn->bDowned;
	}
	else
	{
		FeelHealthSeen = -1.f;
		bFeelDownedSeen = false;
	}
	if (const ASQGameState* FeelGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr)
	{
		if (FeelLevelSeen > 0 && FeelGS->TeamLevel > FeelLevelSeen) SQInput::Rumble(this, 0.5f, 0.3f);
		FeelLevelSeen = FeelGS->TeamLevel;
	}
	(void)DeltaTime;
}

void ASQPlayerController::AddMappingContext()
{
	if (!Context || !IsLocalController()) return;
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Sub->RemoveMappingContext(Context);
		Sub->AddMappingContext(Context, 0);
	}
}

void ASQPlayerController::OnMove(const FInputActionValue& Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	if (const ASQCharacter* Frozen = Cast<ASQCharacter>(P)) { if (Frozen->IsFrozenForLobby()) return; } // lobby: wait for everyone to ready up
	const FVector2D V = Value.Get<FVector2D>();
	if (bTopDownInput)
	{
		// Camera space: W = away from the chase camera (the control rotation is the facing, not the camera)
		FVector CamForward = FVector::ForwardVector;
		FVector CamRight = FVector::RightVector;
		GetCameraAxes(CamForward, CamRight);
		P->AddMovementInput(CamForward, V.Y);
		P->AddMovementInput(CamRight, V.X);
		MoveInputWorld += CamForward * V.Y + CamRight * V.X;
		return;
	}
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	P->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	P->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void ASQPlayerController::OnLook(const FInputActionValue& Value)
{
	// Solo: the cursor is free to click cards and the camera stays put. Co-op: the camera only pauses while Alt shows the cursor.
	if (IsPicking() && (IsSoloPick() || bStripCursor)) return;
	if (IsTipCursorHeld()) return; // clicking a tip's x

	const FVector2D V = Value.Get<FVector2D>();
	if (bPingHeld)
	{
		// The ping wheel reads the look input while its button is held (mouse: pixels; stick: -1..1 a frame)
		PingDrag += V * 4.0;
		const double DragLen = PingDrag.Size();
		if (DragLen > 120.0) PingDrag *= 120.0 / DragLen;
		if (bPingWheel) return;
	}
	if (bTopDownInput)
	{
		// Chase camera: the mouse orbits it whenever the cursor is hidden (the cursor is up for the lobby, cards, shop, SETTINGS, Alt)
		if (bShowMouseCursor || bSettingsOpen) return;
		ASQCharacter* LookPawn = GetPawn<ASQCharacter>();
		if (!LookPawn) return;
		const USQSaveGame* LookSave = GetProfile();
		const float LookSens = LookSave ? FMath::Clamp(LookSave->MouseSensitivity, SQSettings::SensMin, SQSettings::SensMax) : SQSettings::SensDefault;
		const float InvertY = (LookSave && LookSave->bInvertLookY) ? -1.f : 1.f;
		// mouse right = turn right; mouse up = look up (the camera drops: less tilt)
		LookPawn->AddCameraOrbit(static_cast<float>(V.X) * LookSens, -static_cast<float>(V.Y) * LookSens * InvertY);
		return;
	}
	if (IsInLobby() && IsInputKeyDown(EKeys::LeftMouseButton)) return; // clicking the reaper grid doesn't turn the camera (right-drag / stick still look)
	const USQData* D = USQData::Get(this);
	const float Sens = D ? D->Tune(TEXT("MouseSensitivity"), 0.35f) : 0.35f;
	FRotator R = GetControlRotation();
	R.Yaw += V.X * Sens;
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + V.Y * Sens), -75.f, -8.f);
	R.Roll = 0.f;
	SetControlRotation(R);
}

void ASQPlayerController::OnJumpStart()
{
	if (IsPicking() && IsSoloPick()) return;
	ClearSlideLatch();
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) { if (!C->bDowned && !C->IsFrozenForLobby()) C->Jump(); }
}

void ASQPlayerController::OnJumpStop()
{
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->StopJumping();
}

void ASQPlayerController::OnDash()
{
	DoDashInput(false);
}

void ASQPlayerController::OnDashMouse()
{
	if (bSettingsOpen || bShowMouseCursor) return; // the cursor is up: right-click belongs to the HUD
	DoDashInput(true);
}

void ASQPlayerController::DoDashInput(bool bForceCursor)
{
	if (IsPicking() && IsSoloPick()) return;
	ClearSlideLatch();
	ASQCharacter* C = GetPawn<ASQCharacter>();
	if (!C) return;
	if (!bTopDownInput)
	{
		C->TryDash();
		return;
	}
	// Moving: dash the way you're moving. Standing still: straight ahead as the camera looks. Same dash as before:
	// pass-through (DashPassSeconds) and the passives' i-frames live in DoDash.
	(void)bForceCursor;
	FVector DashDir = C->GetLastMovementInputVector();
	DashDir.Z = 0.0;
	if (DashDir.SizeSquared() < 0.01)
	{
		FVector CamForward = FVector::ForwardVector;
		FVector CamRight = FVector::RightVector;
		GetCameraAxes(CamForward, CamRight);
		DashDir = CamForward;
	}
	const bool bWasReady = C->DashCooldownLeft <= 0.f;
	C->TryDash(DashDir);
	if (bWasReady && C->DashCooldownLeft > 0.f && !DashDir.IsNearlyZero())
	{
		// Face the dash for a moment (UpdateTopDownFacing)
		DashFaceYaw = static_cast<float>(DashDir.Rotation().Yaw);
		DashFaceLeft = 0.2f;
	}
}

void ASQPlayerController::OnReap()
{
	// Left mouse: the Reap. Never while the cursor is up (level-up cards, tutorial links, SETTINGS, Alt), picking solo or in the lobby.
	if (!bTopDownInput || bSettingsOpen || IsInLobby() || (IsPicking() && IsSoloPick())) return;
	if (bShowMouseCursor || IsCursorOverHud()) return;
	ASQCharacter* C = GetPawn<ASQCharacter>();
	if (!C || C->bDowned || C->IsFrozenForLobby() || !C->Reap) return;
	FVector AimPoint = FVector::ZeroVector;
	if (!GetAimPoint(AimPoint)) AimPoint = C->GetActorLocation() + C->GetActorForwardVector() * 300.f;
	C->Reap->TryReap(AimPoint);
}

bool ASQPlayerController::IsCursorOverHud() const
{
	const ASQHUD* SQHud = Cast<ASQHUD>(GetHUD());
	return SQHud && SQHud->IsMouseOverClickable();
}

void ASQPlayerController::OnAbilityE()
{
	CastAbility(AbilitySlotE);
}

void ASQPlayerController::OnAbilityRPad()
{
	CastAbility(AbilitySlotR);
}

void ASQPlayerController::CastAbility(uint8 Slot)
{
	if (bSettingsOpen || IsInLobby() || (IsPicking() && IsSoloPick())) return;
	ASQCharacter* C = GetPawn<ASQCharacter>();
	if (!C || C->bDowned || C->IsFrozenForLobby()) return;
	ExplainCastBlocked(Slot); // locked / uncharged: say so (the cast below is still sent; the ability side has the final word)
	FVector AimPoint = FVector::ZeroVector;
	if (!GetAimPoint(AimPoint)) AimPoint = C->GetActorLocation() + C->GetActorForwardVector() * 600.f;
	// Face the cursor for a moment (Tuning AimLockSeconds); UpdateTopDownFacing turns the reaper through the control rotation
	const USQData* CastData = USQData::Get(this);
	C->StartAimLock(CastData ? CastData->Tune(TEXT("AimLockSeconds"), 0.25f) : 0.25f);
	// ---- Ability agent hook: ASQCharacter::TryCastAbility(uint8 Slot, const FVector& AimPoint) is declared in SQCharacter.h and
	// implemented by the ability agent (USQAbilityComponent). Called on the owning machine with the UNCLAMPED ground aim point
	// (GetAimPoint); the ability side clamps to its own range (GetAimPointClamped) and sends the cast to the server.
	// Slot: AbilitySlotE (2) = E / gamepad LT, AbilitySlotR (3) = R / gamepad RT (kit view numbering, see SQPlayerController.h).
	C->TryCastAbility(Slot, AimPoint);
}

void ASQPlayerController::ExplainCastBlocked(uint8 Slot)
{
	const ASQCharacter* C = GetPawn<ASQCharacter>();
	USQAbilityComponent* Kit = C ? C->Abilities.Get() : nullptr;
	const UWorld* World = GetWorld();
	if (!Kit || !World) return;
	FSQAbilityView View;
	if (!Kit->GetAbilityView(Slot, View)) return;
	const float Now = static_cast<float>(World->GetRealTimeSeconds());
	if (Now - CastHintAt < 1.5f) return;
	FString Why;
	if (!View.bUnlocked)
	{
		Why = Slot == AbilitySlotR
			? FString::Printf(TEXT("R (ultimate) unlocks at level %d, then fills from the souls you reap."), FMath::Max(1, View.UnlockLevel))
			: FString::Printf(TEXT("E unlocks at level %d."), FMath::Max(1, View.UnlockLevel));
	}
	else if (Slot == AbilitySlotR && Kit->GetUltCharge01() < 0.999f)
	{
		Why = FString::Printf(TEXT("R is charging: %d%% - reap souls to fill the ring round your portrait."), FMath::FloorToInt(Kit->GetUltCharge01() * 100.f));
	}
	if (Why.IsEmpty()) return;
	CastHintAt = Now;
	ShowLocalMessage(Why);
}

void ASQPlayerController::OnAimStick(const FInputActionValue& Value)
{
	AimStickRaw = Value.Get<FVector2D>(); // read and cleared in UpdateAimInput (Triggered fires every frame while held)
}

void ASQPlayerController::OnZoom(const FInputActionValue& Value)
{
	if (bSettingsOpen || (IsPicking() && IsSoloPick())) return;
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->AddCameraZoom(Value.Get<float>());
}

void ASQPlayerController::OnSlideStart()
{
	if (IsPicking() && IsSoloPick()) return;
	const USQSaveGame* SlideSave = GetProfile();
	if (SlideSave && SlideSave->bSlideToggle)
	{
		// SETTINGS > ACCESS > Slide: TOGGLE. One press starts the slide, the next (or a jump / dash) ends it.
		bSlideLatched = !bSlideLatched;
		if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->SetSlideInput(bSlideLatched);
		return;
	}
	bSlideLatched = false;
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->SetSlideInput(true);
}

void ASQPlayerController::OnSlideStop()
{
	if (bSlideLatched) return; // toggled on: the release means nothing
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->SetSlideInput(false);
}

void ASQPlayerController::ClearSlideLatch()
{
	if (!bSlideLatched) return;
	bSlideLatched = false;
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->SetSlideInput(false);
}

void ASQPlayerController::OnInteract()
{
	if (IsPicking() && IsSoloPick()) return;
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->ServerInteract();
	// Economy pass: interact (F in top-down, E in TopDownMode 0) also opens chests and rings the Appointment Bell (the server ignores it next to a downed teammate)
	if (ASQPlayerState* UsePS = GetPlayerState<ASQPlayerState>()) UsePS->ServerUseNearby();
}

void ASQPlayerController::OnChoose1() { ChooseByKey(0, EKeys::One); }
void ASQPlayerController::OnChoose2() { ChooseByKey(1, EKeys::Two); }
void ASQPlayerController::OnChoose3() { ChooseByKey(2, EKeys::Three); }
void ASQPlayerController::OnChoose4() { ChooseByKey(3, EKeys::Four); }
void ASQPlayerController::OnChoose5() { ChooseByKey(4, EKeys::Five); }
void ASQPlayerController::OnChoose6() { ChooseByKey(5, EKeys::Six); }
// 7-9 only mean something in the lobby (3 level-up cards, the shop's keys are 1-6)
void ASQPlayerController::OnChoose7() { if (IsInLobby()) ChooseByKey(6, EKeys::Seven); }
void ASQPlayerController::OnChoose8() { if (IsInLobby()) ChooseByKey(7, EKeys::Eight); }
void ASQPlayerController::OnChoose9() { if (IsInLobby()) ChooseByKey(8, EKeys::Nine); }

void ASQPlayerController::ChooseByKey(int32 Index, const FKey& DigitKey)
{
	if (bSettingsOpen) return; // the settings overlay is up
	// Actions 1-4 are also on the D-pad; in the lobby the D-pad moves the HUD's reaper grid focus instead
	if (IsInLobby() && !IsInputKeyDown(DigitKey)) return;
	Choose(Index);
}

void ASQPlayerController::OnBiome() { if (!bSettingsOpen) ServerLobbyCommand(TEXT("Biome")); }
void ASQPlayerController::OnEndless() { if (!bSettingsOpen) ServerLobbyCommand(TEXT("Endless")); }

USQSaveGame* ASQPlayerController::GetProfile()
{
	if (!Profile) Profile = USQSaveGame::Load();
	return Profile;
}

void ASQPlayerController::ShowLocalMessage(const FString& Message)
{
	LocalMessage = Message;
	LocalMessageTime = static_cast<float>(GetWorld()->GetRealTimeSeconds());
}

void ASQPlayerController::ServerLobbyCommand_Implementation(FName Command)
{
	// Only the host (the listen-server player) changes biome / mode
	if (!IsLocalController()) return;
	ASQGameMode* GM = GetWorld()->GetAuthGameMode<ASQGameMode>();
	if (!GM) return;
	if (Command == TEXT("Biome")) GM->CycleBiome();
	else if (Command == TEXT("Endless")) GM->ToggleEndless();
	else if (Command == TEXT("Daily")) GM->ToggleDaily();
}

void ASQPlayerController::ClientRunEnded_Implementation(FName BiomeId, bool bWonStory, int32 NightsCleared, int32 Kills, bool bEndless)
{
	const USQData* D = USQData::Get(this);
	USQSaveGame* Save = GetProfile();
	if (!D || !Save) return;

	const int32 SafeNights = FMath::Max(0, NightsCleared);
	const int32 SafeKills = FMath::Max(0, Kills);
	int32 Pay = FMath::RoundToInt(D->Tune(TEXT("PayPerNight"), 40.f) * SafeNights + SafeKills / FMath::Max(1.f, D->Tune(TEXT("KillsPerPay"), 10.f)));
	if (bWonStory) Pay += FMath::RoundToInt(D->Tune(TEXT("PayWinBonus"), 150.f));
	Pay = FMath::Clamp(Pay, 0, 1000000); // never negative (bad tuning), never an overflow
	Save->OvertimePay += Pay;
	Save->LifetimePay += Pay;
	Save->RunsPlayed++;
	FString Extra;
	if (bWonStory)
	{
		Save->RunsWon++;
		if (!Save->Contracts.Contains(BiomeId))
		{
			Save->Contracts.Add(BiomeId);
			const FSQBiomeDef* B = D->FindBiome(BiomeId);
			Extra = FString::Printf(TEXT("  CONTRACT COLLECTED: %s - new biomes may be open (host: press B in the lobby)"), B ? *B->Name : *BiomeId.ToString());
		}
	}
	if (bEndless) Save->BestEndlessNight = FMath::Max(Save->BestEndlessNight, SafeNights);
	USQSaveGame::Store(Save);
	ShowLocalMessage(FString::Printf(TEXT("+%d banked gold (total %d).%s"), Pay, Save->OvertimePay, *Extra));
}

void ASQPlayerController::Choose(int32 Index)
{
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (GS && GS->Phase == ESQPhase::Waiting)
	{
		const USQData* D = USQData::Get(this);
		USQSaveGame* Save = GetProfile();
		if (!D || !D->Reapers.IsValidIndex(Index)) return;
		const FSQReaperDef& R = D->Reapers[Index];
		if (R.UnlockCost > 0 && Save && !Save->HasReaper(R.Id) && D->Tune(TEXT("UnlockAllReapers"), 0.f) < 0.5f)
		{
			if (Save->OvertimePay < R.UnlockCost)
			{
				ShowLocalMessage(FString::Printf(TEXT("%s is locked: needs %d banked gold (you have %d). Finish missions to bank gold."), *R.Name, R.UnlockCost, Save->OvertimePay));
				return;
			}
			Save->OvertimePay -= R.UnlockCost;
			Save->UnlockedReapers.Add(R.Id);
			USQSaveGame::Store(Save);
			ShowLocalMessage(FString::Printf(TEXT("Hired %s!"), *R.Name));
		}
		if (Save)
		{
			Save->PreferredReaper = R.Id;
			USQSaveGame::Store(Save);
		}
		ServerPickReaper(Index);
	}
	else if (GS && GS->Phase == ESQPhase::Shop)
	{
		ServerBuy(Index);
	}
	else if (PS && PS->IsChoosing() && PS->Offers.IsValidIndex(Index))
	{
		// Co-op: the first number key only opens the collapsed strip (no blind picks mid-fight)
		if (!IsSoloPick() && !bStripExpanded)
		{
			bStripExpanded = true;
			return;
		}
		ServerChoose(Index, PS->OfferSerial);
	}
}

bool ASQPlayerController::IsSoloPick() const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!GS) return true;
	int32 Humans = 0;
	for (const APlayerState* Counted : GS->PlayerArray)
	{
		if (Counted && !Counted->IsABot()) Humans++;
	}
	return Humans <= 1;
}

void ASQPlayerController::UpdatePickStrip()
{
	const bool bCoopPicking = IsPicking() && !IsSoloPick();
	if (!bCoopPicking)
	{
		bStripExpanded = false;
		bStripCursor = false;
	}
	else
	{
		// Tab toggles; the strip opens by itself when no enemy is close (Tuning PickStripEnemyRange)
		if (!bSettingsOpen && WasInputKeyJustPressed(EKeys::Tab)) bStripExpanded = !bStripExpanded;
		if (!bStripExpanded)
		{
			const USQData* StripData = USQData::Get(this);
			const ASQGameState* StripGS = GetWorld()->GetGameState<ASQGameState>();
			const APawn* StripPawn = GetPawn();
			const float CalmRange = StripData ? StripData->Tune(TEXT("PickStripEnemyRange"), 600.f) : 600.f;
			if (StripGS && StripGS->Horde && StripPawn)
			{
				const FVector StripLoc = StripPawn->GetActorLocation();
				if (!StripGS->Horde->AnyEnemyWithin(FVector2D(StripLoc.X, StripLoc.Y), CalmRange)) bStripExpanded = true;
			}
		}
		// The cursor is hidden in a fight (mouse look): hold Left Alt to click the expanded strip (1 / 2 / 3 always pick)
		bStripCursor = bStripExpanded && IsInputKeyDown(EKeys::LeftAlt);
	}
	const bool bOpenNow = bCoopPicking && bStripExpanded;
	if (bOpenNow != bStripOpenSent)
	{
		bStripOpenSent = bOpenNow;
		ServerSetPickStripOpen(bOpenNow);
	}
}

void ASQPlayerController::ServerSetPickStripOpen_Implementation(bool bOpen)
{
	if (ASQPlayerState* PS = GetPlayerState<ASQPlayerState>()) PS->bPickStripOpen = bOpen;
}

void ASQPlayerController::ServerSetAutoPick_Implementation(bool bOn)
{
	if (ASQPlayerState* PS = GetPlayerState<ASQPlayerState>()) PS->bAutoPick = bOn;
}

bool ASQPlayerController::IsInLobby() const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	return GS && GS->Phase == ESQPhase::Waiting;
}

bool ASQPlayerController::IsPicking() const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	return GS && PS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss) && PS->IsChoosing();
}

void ASQPlayerController::Reroll()
{
	const ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (!IsSoloPick() && !bStripExpanded) return; // co-op: only from the open strip
	if (PS && IsPicking() && PS->RerollsLeft > 0) ServerReroll(PS->OfferSerial);
}

void ASQPlayerController::Skip()
{
	const ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (!IsSoloPick() && !bStripExpanded) return;
	if (PS && IsPicking()) ServerSkip(PS->OfferSerial);
}

void ASQPlayerController::OnReroll() { if (!bSettingsOpen) Reroll(); }
void ASQPlayerController::OnSkip() { if (!bSettingsOpen) Skip(); }

void ASQPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (IsLocalController())
	{
		UpdatePickStrip();
		UpdatePickCursor();
		UpdatePing(DeltaTime);
		if (bTopDownInput)
		{
			UpdateAimInput(DeltaTime);
			UpdateTopDownFacing(DeltaTime);
			UpdateCameraInput(DeltaTime);
		}
		SQInput::Poll(this);
		UpdatePadButtons();
		UpdatePlatformFeel(DeltaTime);
		if (UWorld* PerfWorld = GetWorld())
		{
			// real frame time for the quality governor (not dilated, still counted while the solo game is paused)
			const double RealNow = PerfWorld->GetRealTimeSeconds();
			if (PerfLastRealTime > 0.0) SQPerf::GovernorTick(PerfWorld, static_cast<float>(RealNow - PerfLastRealTime));
			PerfLastRealTime = RealNow;
		}
		// Settings overlay: Esc closes it (unless CONTROLS is waiting for a key); the solo pause never outlives it
		{
			const ASQHUD* EscHud = Cast<ASQHUD>(GetHUD());
			const bool bCapturing = EscHud && EscHud->IsCapturingKey();
			if (bSettingsOpen && !bCapturing && WasInputKeyJustPressed(EKeys::Escape)) SetSettingsOpen(false);
		}
		if (bSettingsPausedGame)
		{
			const ASQGameState* PauseGS = GetWorld()->GetGameState<ASQGameState>();
			const bool bFight = PauseGS && (PauseGS->Phase == ESQPhase::Night || PauseGS->Phase == ESQPhase::Boss);
			if (!bSettingsOpen || !IsSoloPick() || !bFight)
			{
				bSettingsPausedGame = false;
				SetPause(false);
			}
		}
	}
}

void ASQPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bSettingsPausedGame)
	{
		bSettingsPausedGame = false;
		SetPause(false);
	}
	Super::EndPlay(EndPlayReason);
}

void ASQPlayerController::OnSettingsKey()
{
	const ASQHUD* KeyHud = Cast<ASQHUD>(GetHUD());
	if (KeyHud && KeyHud->IsCapturingKey()) return; // SETTINGS > CONTROLS is waiting for a key (it may be this one)
	SetSettingsOpen(!bSettingsOpen);
}

void ASQPlayerController::SetSettingsOpen(bool bOpen)
{
	if (bOpen == bSettingsOpen) return;
	bSettingsOpen = bOpen;
	if (bOpen)
	{
		// Solo, mid-fight: pause (the engine pause; ASQGameMode re-asserts its own pick-pause time dilation every tick, so that is left
		// alone). Co-op: opens over the game, nothing stops. The lobby / end screens have nothing to pause.
		const ASQGameState* PauseGS = GetWorld()->GetGameState<ASQGameState>();
		const bool bFight = PauseGS && (PauseGS->Phase == ESQPhase::Night || PauseGS->Phase == ESQPhase::Boss);
		if (bFight && IsSoloPick() && HasAuthority() && !GetWorld()->IsPaused())
		{
			bSettingsPausedGame = SetPause(true);
		}
	}
	else if (bSettingsPausedGame)
	{
		bSettingsPausedGame = false;
		SetPause(false);
	}
}

void ASQPlayerController::OnResetCamera()
{
	if (ASQCharacter* C = GetPawn<ASQCharacter>()) C->ResetCamera();
}

void ASQPlayerController::UpdateCameraInput(float DeltaTime)
{
	ASQCharacter* C = GetPawn<ASQCharacter>();
	// The solo settings pause stops the pawn's tick: keep its camera live so SETTINGS changes show at once
	if (C && GetWorld()->IsPaused()) C->UpdateCameraWhilePaused(DeltaTime);
}

bool ASQPlayerController::IsTipCursorHeld() const
{
	if (IsInLobby()) return false;
	if (!IsInputKeyDown(EKeys::LeftAlt)) return false; // in a fight the mouse looks around: hold Left Alt for the cursor
	const USQTutorial* Tut = USQTutorial::Get(this);
	return Tut && Tut->AnyTipVisible();
}

void ASQPlayerController::UpdatePickCursor()
{
	// Chase camera: in a fight the cursor is hidden and captured (the mouse looks around). It shows for the lobby, the shop,
	// the end screens, solo level-up cards, SETTINGS, the full map, and while Left Alt is held (co-op strip, tip links).
	const bool bPickNow = IsPicking() && (IsSoloPick() || bStripCursor);
	const UWorld* CursorWorld = GetWorld();
	const ASQGameState* CursorGS = CursorWorld ? CursorWorld->GetGameState<ASQGameState>() : nullptr;
	const bool bFightNow = CursorGS && (CursorGS->Phase == ESQPhase::Night || CursorGS->Phase == ESQPhase::Boss);
	const bool bAltCursor = bFightNow && IsInputKeyDown(EKeys::LeftAlt);
	const bool bWantCursor = (bTopDownInput && !bFightNow) || bPickNow || IsInLobby() || IsTipCursorHeld() || bSettingsOpen || bAltCursor;
	// Sack full: say why souls stopped lifting (once each time it fills; resets when you bank)
	if (const ASQCharacter* SackPawn = Cast<ASQCharacter>(GetPawn()))
	{
		const bool bSackFull = bFightNow && SackPawn->Stats.CarryCap > 0 && SackPawn->Carried >= SackPawn->Stats.CarryCap;
		if (bSackFull && !bSackFullHinted)
		{
			ShowLocalMessage(TEXT("Soul sack FULL: bank your souls at the lit lantern (follow the beam). Grey wisps can't be picked up until you do."));
		}
		bSackFullHinted = bSackFull;
	}
	// Solo level-up: start the cursor in the middle of the cards, once when they open (the lobby leaves it where it is)
	const bool bSoloPickNow = IsPicking() && IsSoloPick();
	if (bSoloPickNow && !bSoloPickCentred)
	{
		int32 ViewW = 0;
		int32 ViewH = 0;
		GetViewportSize(ViewW, ViewH);
		if (ViewW > 0 && ViewH > 0) SetMouseLocation(ViewW / 2, ViewH / 2);
	}
	bSoloPickCentred = bSoloPickNow;
	if (bWantCursor == bPickCursorOn) return;
	bPickCursorOn = bWantCursor;
	if (bWantCursor)
	{
		FInputModeGameAndUI PickMode;
		// The cursor stays inside a fullscreen window; clicks still reach the HUD (WasInputKeyJustPressed)
		PickMode.SetLockMouseToViewportBehavior(bTopDownInput ? EMouseLockMode::LockInFullscreen : EMouseLockMode::DoNotLock);
		PickMode.SetHideCursorDuringCapture(false);
		SetInputMode(PickMode);
		bShowMouseCursor = true;
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void ASQPlayerController::OnReady()
{
	if (bSettingsOpen) return;
	// R doubles as reroll while level-up cards are up (Q too); otherwise it is the lobby / office ready key.
	// Top-down co-op: a collapsed strip doesn't take R (it stays the ultimate mid-fight; Q still rerolls once the strip is open).
	if (IsPicking() && (!bTopDownInput || IsSoloPick() || bStripExpanded))
	{
		Reroll();
		return;
	}
	// Top-down: in a fight R is ability 2 (the ultimate). Ready only means something in the lobby and Death's Office.
	if (bTopDownInput)
	{
		const ASQGameState* ReadyGS = GetWorld()->GetGameState<ASQGameState>();
		if (ReadyGS && ReadyGS->Phase != ESQPhase::Waiting && ReadyGS->Phase != ESQPhase::Shop)
		{
			// The ultimate's key (R by default; the gamepad's is RT through AbilityRPadAction, and MENU never reaches here in a fight)
			if (IsInputKeyDown(SQInput::Key(SQInput::EAct::AbilityR, false))) CastAbility(AbilitySlotR);
			return;
		}
	}
	ServerReady();
}

void ASQPlayerController::ServerChoose_Implementation(int32 Index, uint8 Serial)
{
	ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (PS && PS->OfferSerial == Serial) PS->ChooseOffer(Index);
}

void ASQPlayerController::ServerReroll_Implementation(uint8 Serial)
{
	ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (PS && PS->OfferSerial == Serial) PS->RerollOffers();
}

void ASQPlayerController::ServerSkip_Implementation(uint8 Serial)
{
	ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (PS && PS->OfferSerial == Serial) PS->SkipOffer();
}

void ASQPlayerController::ServerBuy_Implementation(int32 Index)
{
	if (ASQGameMode* GM = GetWorld()->GetAuthGameMode<ASQGameMode>()) GM->TryBuy(GetPlayerState<ASQPlayerState>(), Index);
}

void ASQPlayerController::ServerReady_Implementation()
{
	// Ready only means something in the lobby and in Death's Office
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (!GS || (GS->Phase != ESQPhase::Waiting && GS->Phase != ESQPhase::Shop)) return;
	if (ASQPlayerState* PS = GetPlayerState<ASQPlayerState>()) PS->bShopReady = true;
}

void ASQPlayerController::ServerEndChoice_Implementation(bool bRetry)
{
	// Only the host's own controller is local on the server
	if (!IsLocalController()) return;
	ASQGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ASQGameMode>() : nullptr;
	if (!GM) return;
	if (bRetry) GM->RetryRun();
	else GM->EndToLobby();
}

void ASQPlayerController::ServerLoopChoice_Implementation(bool bKeepGoing)
{
	if (!IsLocalController()) return;
	if (ASQGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ASQGameMode>() : nullptr) GM->HandleLoopChoice(bKeepGoing);
}

bool ASQPlayerController::CanChooseAfterRun() const
{
	return HasAuthority() && IsLocalController();
}

void ASQPlayerController::ServerPickReaper_Implementation(int32 Index)
{
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();
	if (!GS || !D || !PS) return;

	// Lobby only - except one pick for a player who joined mid-run (their main-menu reaper, sent once from BeginPlay).
	// Swapping mid-run would re-init the character: a free revive / soul reset.
	const bool bLobby = GS->Phase == ESQPhase::Waiting;
	const bool bLateJoinPick = PS->MidRunPickDeadline >= 0.0 && GetWorld()->GetRealTimeSeconds() <= PS->MidRunPickDeadline;
	PS->MidRunPickDeadline = -1.0;
	if (!bLobby && !bLateJoinPick) return;
	if (!D->Reapers.IsValidIndex(Index)) return;

	const FSQReaperDef& PickedDef = D->Reapers[Index];
	if (PickedDef.Id == PS->ReaperId) return; // nothing to change (and no needless re-init spam)

	// Unlock check. Profiles are local to each machine: the host's own picks are checked against the host's save;
	// a remote client's unlock list can't be verified, so it is trusted up to Tuning MaxClientUnlockCost (-1 = trust all).
	if (PickedDef.UnlockCost > 0 && D->Tune(TEXT("UnlockAllReapers"), 0.f) < 0.5f)
	{
		if (IsLocalController())
		{
			const USQSaveGame* HostProfile = GetProfile();
			if (!HostProfile || !HostProfile->HasReaper(PickedDef.Id)) return;
		}
		else
		{
			const float TrustCap = D->Tune(TEXT("MaxClientUnlockCost"), -1.f);
			if (TrustCap >= 0.f && static_cast<float>(PickedDef.UnlockCost) > TrustCap) return;
		}
	}

	ASQCharacter* C = GetPawn<ASQCharacter>();
	if (!bLobby && C && C->bDowned) return; // a late joiner's pick can't be used to stand back up

	PS->ReaperId = PickedDef.Id;
	if (C) C->InitReaper(PS->ReaperId);
}

void ASQPlayerController::ServerCheat_Implementation(FName Command, int32 Number, const FString& Text)
{
#if !UE_BUILD_SHIPPING
	const USQData* D = USQData::Get(this);
	if (!D || D->Tune(TEXT("AllowCheats"), 1.f) < 0.5f) return;
	// Only the host's own console (the listen-server player) may cheat, unless the host opts in with AllowClientCheats=1
	const bool bHostCaller = IsLocalController();
	if (!bHostCaller && D->Tune(TEXT("AllowClientCheats"), 0.f) < 0.5f) return;

	ASQGameMode* GM = GetWorld()->GetAuthGameMode<ASQGameMode>();
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQCharacter* C = GetPawn<ASQCharacter>();
	ASQPlayerState* PS = GetPlayerState<ASQPlayerState>();

	if (Command == TEXT("Spawn") && GS && GS->Horde) GS->Horde->DebugSpawn(FMath::Clamp(Number, 1, 3000));
	else if (Command == TEXT("God") && C) C->bGodMode = !C->bGodMode;
	else if (Command == TEXT("Souls") && C) C->Carried += FMath::Clamp(Number, 0, 100000);
	else if (Command == TEXT("XP") && GS) GS->AddTeamXP(FMath::Clamp(Number, 0, 100000));
	else if (Command == TEXT("Skip") && GM) GM->SkipPhase();
	else if (Command == TEXT("Bot") && GM) GM->AddBot();
	else if (Command == TEXT("Reaper") && C && PS && GS && GS->Phase == ESQPhase::Waiting)
	{
		PS->ReaperId = FName(*Text);
		C->InitReaper(PS->ReaperId);
	}
	else if (Command == TEXT("Reload") && bHostCaller)
	{
		// Host only: Reload rebuilds every def array, so it must never be triggered remotely
		if (USQData* MutableData = USQData::Get(this)) MutableData->Reload();
	}
#endif
}

// Debug console commands: compiled out of Shipping builds (and the server gates them again in ServerCheat)
void ASQPlayerController::SQSpawn(int32 Count)
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("Spawn"), Count, FString());
#endif
}

void ASQPlayerController::SQGod()
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("God"), 0, FString());
#endif
}

void ASQPlayerController::SQSouls(int32 Amount)
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("Souls"), Amount, FString());
#endif
}

void ASQPlayerController::SQXP(int32 Amount)
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("XP"), Amount, FString());
#endif
}

void ASQPlayerController::SQSkip()
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("Skip"), 0, FString());
#endif
}

void ASQPlayerController::SQAddBot()
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("Bot"), 0, FString());
#endif
}

void ASQPlayerController::SQReaper(const FString& Name)
{
#if !UE_BUILD_SHIPPING
	ServerCheat(TEXT("Reaper"), 0, Name);
#endif
}

void ASQPlayerController::SQReload()
{
#if !UE_BUILD_SHIPPING
	// Host only: on a client a local reload would desync its tables from the server's
	if (!HasAuthority()) return;
	if (USQData* D = USQData::Get(this)) D->Reload();
#endif
}

void ASQPlayerController::SQHost()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), true, TEXT("listen"));
}

void ASQPlayerController::SQJoin(const FString& Address)
{
	ClientTravel(Address, ETravelType::TRAVEL_Absolute);
}

void ASQPlayerController::SQStats()
{
	bShowStats = !bShowStats;
}

void ASQPlayerController::SQShowCollision()
{
#if !UE_BUILD_SHIPPING
	// Host only: the circles are drawn from the server's own simulation
	if (!HasAuthority())
	{
		ShowLocalMessage(TEXT("SQShowCollision is host only."));
		return;
	}
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (!GS || !GS->Horde) return;
	GS->Horde->bShowCollision = !GS->Horde->bShowCollision;
	ShowLocalMessage(GS->Horde->bShowCollision
		? TEXT("Collision view ON: green = enemy body, blue = flyer / wisp body, red = enemy reach, yellow = reaper body.")
		: TEXT("Collision view OFF."));
#endif
}

void ASQPlayerController::SQTutorial(int32 ResetFlags)
{
#if !UE_BUILD_SHIPPING
	USQTutorial* Tut = USQTutorial::Get(this);
	if (!Tut) return;
	if (ResetFlags != 0)
	{
		Tut->ResetTutorial();
		ShowLocalMessage(TEXT("Tutorial reset: the how-to-play prompts and every tip will show again."));
	}
	const ASQGameState* TutGS = GetWorld()->GetGameState<ASQGameState>();
	if (TutGS && (TutGS->Phase == ESQPhase::Night || TutGS->Phase == ESQPhase::Boss))
	{
		Tut->StartTraining(this);
	}
	else
	{
		Tut->ArmTrainingReplay();
		ShowLocalMessage(TEXT("The how-to-play prompts show when the run starts."));
	}
#endif
}

// ---------------------------------------------------------------- top-down aim (shared API) and facing

namespace SQAimMath
{
	/**
	 * Where a camera ray meets the ground. With a terrain, the ray is marched over the arena height field (ASQArena::GroundZ
	 * is exactly the rendered / collision terrain surface, the procedural mesh is built from it) between the highest and
	 * lowest ground, then the crossing is bisected. Otherwise (or if it never crosses) the plane at PlaneZ.
	 */
	static FVector GroundHit(const ASQArena* Arena, const FVector& Origin, const FVector& Dir, double PlaneZ)
	{
		const double PlaneT = FMath::Max(0.0, (PlaneZ - Origin.Z) / Dir.Z); // Dir.Z < 0 (checked by the caller)
		const FVector PlaneHit = Origin + Dir * PlaneT;
		if (!Arena || !Arena->HasTerrain()) return PlaneHit;

		const double TopT = FMath::Max(0.0, (static_cast<double>(Arena->MaxGroundZ()) + 5.0 - Origin.Z) / Dir.Z);
		// (capped: a ray skimming the horizon would otherwise march for a kilometre)
		const double BottomT = FMath::Clamp((static_cast<double>(Arena->MinGroundZ()) - 5.0 - Origin.Z) / Dir.Z, TopT, TopT + 30000.0);
		auto Below = [Arena, &Origin, &Dir](double AtT)
		{
			const FVector P = Origin + Dir * AtT;
			return P.Z <= Arena->GroundZ(static_cast<float>(P.X), static_cast<float>(P.Y));
		};
		const double StepT = 40.0;
		double PrevT = TopT;
		for (double RayT = TopT; RayT <= BottomT + StepT; RayT += StepT)
		{
			if (!Below(RayT))
			{
				PrevT = RayT;
				continue;
			}
			double LoT = PrevT;
			double HiT = RayT;
			for (int32 Iter = 0; Iter < 8; ++Iter)
			{
				const double MidT = (LoT + HiT) * 0.5;
				if (Below(MidT)) HiT = MidT;
				else LoT = MidT;
			}
			FVector Hit = Origin + Dir * HiT;
			Hit.Z = Arena->GroundZ(static_cast<float>(Hit.X), static_cast<float>(Hit.Y));
			return Hit;
		}
		return PlaneHit;
	}
}

void ASQPlayerController::GetCameraAxes(FVector& OutForward, FVector& OutRight) const
{
	// The chase camera's own yaw, so W always means "away from the camera"
	const ASQCharacter* AxesPawn = GetPawn<ASQCharacter>();
	const float ViewYaw = AxesPawn ? AxesPawn->GetViewYaw() : ASQCharacter::GetCameraYaw(this);
	const FRotationMatrix CamYawMatrix(FRotator(0.f, ViewYaw, 0.f));
	OutForward = CamYawMatrix.GetUnitAxis(EAxis::X);
	OutRight = CamYawMatrix.GetUnitAxis(EAxis::Y);
}

float ASQPlayerController::AimGroundZ(float X, float Y, float FallbackZ) const
{
	const UWorld* GroundWorld = GetWorld();
	const ASQGameState* GroundGS = GroundWorld ? GroundWorld->GetGameState<ASQGameState>() : nullptr;
	const ASQArena* GroundArena = GroundGS ? GroundGS->Arena.Get() : nullptr;
	return (GroundArena && GroundArena->HasTerrain()) ? GroundArena->GroundZ(X, Y) : FallbackZ;
}

void ASQPlayerController::UpdateAimInput(float DeltaTime)
{
	// Right stick: orbit the chase camera (deg / s at full deflection, Tuning ChaseStickTurnRate); same gates as the mouse
	if (AimStickRaw.SizeSquared() > 0.04 && !bShowMouseCursor && !bSettingsOpen)
	{
		if (ASQCharacter* StickPawn = GetPawn<ASQCharacter>())
		{
			const USQData* StickData = USQData::Get(this);
			const USQSaveGame* StickSave = GetProfile();
			// SETTINGS > Stick sensitivity: 0 = 0.4x .. 0.5 = 1x .. 1 = 1.6x; the response curve is squared so small tilts aim finely
			const float SensMul = FMath::Lerp(0.4f, 1.6f, StickSave ? FMath::Clamp(StickSave->StickSensitivity, 0.f, 1.f) : 0.5f);
			const float TurnRate = (StickData ? FMath::Clamp(StickData->Tune(TEXT("ChaseStickTurnRate"), 200.f), 30.f, 720.f) : 200.f) * SensMul;
			const float InvertY = (StickSave && StickSave->bInvertLookY) ? -1.f : 1.f;
			const float RealDelta = FMath::Clamp(DeltaTime, 0.f, 0.1f);
			const float Tilt = FMath::Clamp(static_cast<float>(AimStickRaw.Size()), 0.f, 1.f);
			const float Curve = FMath::Lerp(Tilt, Tilt * Tilt, StickData ? FMath::Clamp(StickData->Tune(TEXT("StickCurve"), 0.5f), 0.f, 1.f) : 0.5f) / FMath::Max(0.01f, Tilt);
			StickPawn->AddCameraOrbit(static_cast<float>(AimStickRaw.X) * Curve * TurnRate * RealDelta,
				-static_cast<float>(AimStickRaw.Y) * Curve * TurnRate * 0.6f * RealDelta * InvertY);
		}
		StickRestFor = 0.f;
	}
	else
	{
		StickRestFor += FMath::Clamp(DeltaTime, 0.f, 0.1f);
		// Camera assist (gamepad, SETTINGS > Controller camera assist): after the right stick has rested a moment the camera eases
		// round behind the way you run, so one thumb can steer. Never while running toward the camera (that would spin it).
		const USQSaveGame* AssistSave = GetProfile();
		ASQCharacter* AssistPawn = GetPawn<ASQCharacter>();
		if (AssistPawn && AssistSave && AssistSave->bCameraAssist && SQInput::UsingPad() && StickRestFor > 0.6f && !bShowMouseCursor && !bSettingsOpen && !bPingWheel)
		{
			const FVector Vel = AssistPawn->GetVelocity();
			if (FVector(Vel.X, Vel.Y, 0.0).SizeSquared() > FMath::Square(220.0))
			{
				FVector CamForward = FVector::ForwardVector;
				FVector CamRight = FVector::RightVector;
				GetCameraAxes(CamForward, CamRight);
				const float CamYaw = static_cast<float>(CamForward.Rotation().Yaw);
				const float MoveYaw = static_cast<float>(FVector(Vel.X, Vel.Y, 0.0).Rotation().Yaw);
				const float Off = FMath::FindDeltaAngleDegrees(CamYaw, MoveYaw);
				if (FMath::Abs(Off) > 4.f && FMath::Abs(Off) < 130.f)
				{
					const USQData* AssistData = USQData::Get(this);
					const float Rate = AssistData ? FMath::Clamp(AssistData->Tune(TEXT("CameraAssistRate"), 1.1f), 0.f, 6.f) : 1.1f;
					AssistPawn->AddCameraOrbit(Off * FMath::Min(1.f, Rate * FMath::Clamp(DeltaTime, 0.f, 0.1f)), 0.f);
				}
			}
		}
	}
	AimStickRaw = FVector2D::ZeroVector;
}

// ---------------------------------------------------------------- ping wheel (co-op pass, Nov 23 2026)

uint8 ASQPlayerController::GetPingWheelPick() const
{
	if (PingDrag.SizeSquared() < FMath::Square(28.0)) return 0;
	// Clockwise from straight up: Help me, Lantern, Chest, Danger, Thanks (72 degrees each)
	float Angle = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(PingDrag.X), static_cast<float>(PingDrag.Y)));
	if (Angle < -36.f) Angle += 360.f;
	return static_cast<uint8>(1 + FMath::Clamp(FMath::FloorToInt((Angle + 36.f) / 72.f), 0, 4));
}

void ASQPlayerController::UpdatePing(float DeltaTime)
{
	ASQCharacter* PingPawn = GetPawn<ASQCharacter>();
	const UWorld* PingWorld = GetWorld();
	const ASQGameState* PingGS = PingWorld ? PingWorld->GetGameState<ASQGameState>() : nullptr;
	const bool bFight = PingGS && (PingGS->Phase == ESQPhase::Night || PingGS->Phase == ESQPhase::Boss);
	if (!PingPawn || !bFight || bSettingsOpen || bShowMouseCursor)
	{
		bPingHeld = false;
		bPingWheel = false;
		return;
	}
	const bool bDown = IsInputKeyDown(SQInput::Key(SQInput::EAct::Ping, false)) || IsInputKeyDown(SQInput::Key(SQInput::EAct::Ping, true));
	if (bDown)
	{
		if (!bPingHeld)
		{
			bPingHeld = true;
			PingHeldFor = 0.f;
			PingDrag = FVector2D::ZeroVector;
		}
		PingHeldFor += DeltaTime;
		if (PingHeldFor >= 0.22f) bPingWheel = true;
		return;
	}
	if (!bPingHeld) return;
	bPingHeld = false;
	const uint8 Kind = bPingWheel ? GetPingWheelPick() : 0;
	bPingWheel = false;

	// Where you look: the centre of the screen onto the ground ("Help me" and "Thanks" sit on the reaper)
	FVector Where = PingPawn->GetActorLocation();
	if (Kind != 1 && Kind != 5)
	{
		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
		GetPlayerViewPoint(ViewLoc, ViewRot);
		const FVector RayDir = ViewRot.Vector();
		const float FeetZ = static_cast<float>(Where.Z) - PingPawn->GetSimpleCollisionHalfHeight();
		FVector Hit = Where + FVector(RayDir.X, RayDir.Y, 0.0).GetSafeNormal() * 1200.0;
		if (RayDir.Z < -0.02) Hit = SQAimMath::GroundHit(PingGS->Arena.Get(), ViewLoc, RayDir, FeetZ);
		FVector Flat(Hit.X - Where.X, Hit.Y - Where.Y, 0.0);
		if (Flat.SizeSquared() > FMath::Square(6000.0)) Flat = Flat.GetSafeNormal() * 6000.0;
		Where = FVector(Where.X + Flat.X, Where.Y + Flat.Y, 0.0);
		Where.Z = AimGroundZ(static_cast<float>(Where.X), static_cast<float>(Where.Y), FeetZ);
	}
	PingPawn->ServerPing(Kind, Where);
}

bool ASQPlayerController::HasManualAim() const
{
	// SETTINGS > Aiming: MANUAL = everything aims where the camera's crosshair points; AUTO (default) = the nearest enemy
	return bTopDownInput && ASQCharacter::IsManualAim() && !IsInLobby();
}

bool ASQPlayerController::GetAimPoint(FVector& OutWorld) const
{
	const ASQCharacter* AimPawn = GetPawn<ASQCharacter>();
	const UWorld* AimWorld = GetWorld();
	if (!AimPawn || !AimWorld) return false;
	const FVector PawnLoc = AimPawn->GetActorLocation();
	const float FeetZ = static_cast<float>(PawnLoc.Z) - AimPawn->GetSimpleCollisionHalfHeight();
	const USQData* AimData = USQData::Get(this);
	const ASQGameState* AimGS = AimWorld->GetGameState<ASQGameState>();
	const ASQArena* AimArena = AimGS ? AimGS->Arena.Get() : nullptr;
	FVector CamForward = FVector::ForwardVector;
	FVector CamRight = FVector::RightVector;
	GetCameraAxes(CamForward, CamRight);

	// 1. MANUAL: the centre of the screen (the crosshair) onto the ground, kept in front of the reaper and within ManualAimMaxRange
	if (HasManualAim())
	{
		const float MaxRange = AimData ? FMath::Max(300.f, AimData->Tune(TEXT("ManualAimMaxRange"), 2600.f)) : 2600.f;
		FVector Hit = PawnLoc + CamForward * MaxRange;
		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
		GetPlayerViewPoint(ViewLoc, ViewRot);
		const FVector RayDir = ViewRot.Vector();
		if (RayDir.Z < -0.02)
		{
			Hit = SQAimMath::GroundHit(AimArena, ViewLoc, RayDir, FeetZ);
		}
		FVector Flat(Hit.X - PawnLoc.X, Hit.Y - PawnLoc.Y, 0.0);
		// behind the reaper (camera looking steeply down) or too far (looking at the horizon): along the view instead
		if (FVector::DotProduct(Flat, CamForward) < 60.0) Flat = CamForward * 300.0;
		if (Flat.SizeSquared() > FMath::Square(static_cast<double>(MaxRange))) Flat = Flat.GetSafeNormal() * MaxRange;
		OutWorld = FVector(PawnLoc.X + Flat.X, PawnLoc.Y + Flat.Y, 0.0);
		OutWorld.Z = AimGroundZ(static_cast<float>(OutWorld.X), static_cast<float>(OutWorld.Y), FeetZ);
		return true;
	}

	// 2. AUTO: the nearest enemy (or the boss) within AutoAimRange
	if (!AimData || AimData->Tune(TEXT("AutoAimFallback"), 1.f) >= 0.5f)
	{
		const float AutoRange = AimData ? FMath::Max(0.f, AimData->Tune(TEXT("AutoAimRange"), 1500.f)) : 1500.f;
		FVector2D EnemyPos = FVector2D::ZeroVector;
		if (AimGS && AimGS->Horde && AimGS->Horde->FindNearestEnemyAnyMachine(FVector2D(PawnLoc.X, PawnLoc.Y), AutoRange, EnemyPos))
		{
			OutWorld = FVector(EnemyPos.X, EnemyPos.Y, 0.0);
			OutWorld.Z = AimGroundZ(static_cast<float>(EnemyPos.X), static_cast<float>(EnemyPos.Y), FeetZ);
			return true;
		}
	}

	// 3. Nothing to aim at: the way you're moving, else the way the camera looks
	FVector AheadDir = AimPawn->GetLastMovementInputVector().GetSafeNormal2D();
	if (AheadDir.IsNearlyZero()) AheadDir = CamForward;
	OutWorld = PawnLoc + AheadDir * 600.0;
	OutWorld.Z = AimGroundZ(static_cast<float>(OutWorld.X), static_cast<float>(OutWorld.Y), FeetZ);
	return true;
}

FVector ASQPlayerController::GetAimPointClamped(float MaxRange) const
{
	const APawn* AimPawn = GetPawn();
	if (!AimPawn) return FVector::ZeroVector;
	const FVector PawnLoc = AimPawn->GetActorLocation();
	FVector Aim = FVector::ZeroVector;
	if (!GetAimPoint(Aim)) Aim = PawnLoc + AimPawn->GetActorForwardVector() * 600.0;
	if (MaxRange > 0.f)
	{
		FVector Flat(Aim.X - PawnLoc.X, Aim.Y - PawnLoc.Y, 0.0);
		if (Flat.SizeSquared() > FMath::Square(static_cast<double>(MaxRange)))
		{
			Flat = Flat.GetSafeNormal() * MaxRange;
			Aim.X = PawnLoc.X + Flat.X;
			Aim.Y = PawnLoc.Y + Flat.Y;
			const float FeetZ = static_cast<float>(PawnLoc.Z) - AimPawn->GetSimpleCollisionHalfHeight();
			Aim.Z = AimGroundZ(static_cast<float>(Aim.X), static_cast<float>(Aim.Y), FeetZ);
		}
	}
	return Aim;
}

void ASQPlayerController::UpdateTopDownFacing(float DeltaTime)
{
	DashFaceLeft = FMath::Max(0.f, DashFaceLeft - DeltaTime);
	const ASQCharacter* C = GetPawn<ASQCharacter>();
	if (!C)
	{
		MoveInputWorld = FVector::ZeroVector;
		return;
	}
	// The control rotation is the wanted facing; USQMovement turns the reaper toward it (ASQCharacter::UpdateFacingMode)
	float WantYaw = static_cast<float>(GetControlRotation().Yaw);
	const FVector Here = C->GetActorLocation();
	FVector AimAt = FVector::ZeroVector;
	// MANUAL aim: the reaper faces the way the camera looks (strafing). AUTO: the move direction, and the target for a
	// moment after an E / R cast (aim lock).
	const bool bManualFace = HasManualAim();
	const bool bFaceAim = !bManualFace && C->IsAimLocked() && !IsInLobby() && GetAimPoint(AimAt);
	const FVector ToAim(AimAt.X - Here.X, AimAt.Y - Here.Y, 0.0);
	if (DashFaceLeft > 0.f)
	{
		WantYaw = DashFaceYaw;
	}
	else if (bManualFace)
	{
		WantYaw = C->GetViewYaw();
	}
	else if (bFaceAim && ToAim.SizeSquared() > 1600.0)
	{
		WantYaw = static_cast<float>(ToAim.Rotation().Yaw);
	}
	else if (!MoveInputWorld.IsNearlyZero(0.01))
	{
		WantYaw = static_cast<float>(MoveInputWorld.Rotation().Yaw);
	}
	SetControlRotation(FRotator(0.f, WantYaw, 0.f));
	MoveInputWorld = FVector::ZeroVector;
}
