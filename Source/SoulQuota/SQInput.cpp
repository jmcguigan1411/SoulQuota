// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQInput.h"
#include "SQSave.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/RenderingCommon.h"

namespace
{
	struct FSQActDef
	{
		const TCHAR* Id;
		const TCHAR* Name;
		/** The literal cap text older HUD / tutorial code uses for this action on the default keyboard layout */
		const TCHAR* LegacyCap;
	};

	// Same order as SQInput::EAct
	const FSQActDef SQActDefs[SQInput::NumActs] = {
		{ TEXT("Jump"), TEXT("Jump (hold to hop)"), TEXT("SPACE") },
		{ TEXT("Dash"), TEXT("Dash through crowds"), TEXT("SHIFT") },
		{ TEXT("Slide"), TEXT("Slide"), TEXT("CTRL") },
		{ TEXT("Interact"), TEXT("Interact / chests / bell"), TEXT("F") },
		{ TEXT("AbilityE"), TEXT("Ability (level 3)"), TEXT("E") },
		{ TEXT("AbilityR"), TEXT("Ultimate (level 6)"), TEXT("R") },
		{ TEXT("Reap"), TEXT("Reap (execute ripe foes)"), TEXT("L-CLICK") },
		{ TEXT("Ping"), TEXT("Ping (hold: wheel)"), TEXT("M-CLICK") },
		{ TEXT("Map"), TEXT("Full map"), TEXT("M") },
		{ TEXT("Settings"), TEXT("Settings / pause"), TEXT("P") },
	};

	bool bSQUsingPad = false;
	bool bSQShapeCues = false;
	bool bSQReduceFlash = false;
	bool bSQVibration = true;

	// stick-as-d-pad repeat state (one local player)
	int32 SQNavHeldDir = 0;      // 0 none, 1 up, 2 down, 3 left, 4 right
	float SQNavNextRepeat = 0.f;

	const FKey& SQPadButton(int32 Index)
	{
		static const FKey Buttons[] = {
			EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
			EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger, EKeys::Gamepad_RightTrigger,
			EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick, EKeys::Gamepad_Special_Left, EKeys::Gamepad_Special_Right,
			EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
		};
		return Buttons[FMath::Clamp(Index, 0, static_cast<int32>(UE_ARRAY_COUNT(Buttons)) - 1)];
	}
	constexpr int32 SQNumPadButtons = 16;

	/** Keys a rebind may never take: the menu keys, the card keys and the fixed movement / camera inputs */
	bool SQIsReservedKey(const FKey& InKey)
	{
		static const FKey Reserved[] = {
			EKeys::Escape, EKeys::Enter, EKeys::Tab, EKeys::W, EKeys::A, EKeys::S, EKeys::D,
			EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
			EKeys::LeftAlt, EKeys::Tilde, EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right,
			// fixed second keys and the card / lobby keys (ASQPlayerController::BuildMappings, ASQHUD)
			EKeys::Q, EKeys::X, EKeys::C, EKeys::B, EKeys::O, EKeys::N, EKeys::Home, EKeys::RightMouseButton,
			EKeys::MouseScrollUp, EKeys::MouseScrollDown,
			EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
		};
		for (const FKey& Banned : Reserved)
		{
			if (InKey == Banned) return true;
		}
		return false;
	}

	bool SQStickPast(const APlayerController* PC, const FKey& Axis, float Threshold)
	{
		const float Value = static_cast<float>(PC->GetInputAnalogKeyState(Axis));
		return Threshold >= 0.f ? Value > Threshold : Value < Threshold;
	}
}

FName SQInput::ActId(EAct Act)
{
	return FName(SQActDefs[FMath::Clamp(static_cast<int32>(Act), 0, NumActs - 1)].Id);
}

const TCHAR* SQInput::ActName(EAct Act)
{
	return SQActDefs[FMath::Clamp(static_cast<int32>(Act), 0, NumActs - 1)].Name;
}

FKey SQInput::DefaultKey(EAct Act, bool bPad)
{
	switch (Act)
	{
	case EAct::Jump: return bPad ? EKeys::Gamepad_FaceButton_Bottom : EKeys::SpaceBar;
	case EAct::Dash: return bPad ? EKeys::Gamepad_RightShoulder : EKeys::LeftShift;
	case EAct::Slide: return bPad ? EKeys::Gamepad_RightThumbstick : EKeys::LeftControl;
	case EAct::Interact: return bPad ? EKeys::Gamepad_FaceButton_Left : EKeys::F;
	case EAct::AbilityE: return bPad ? EKeys::Gamepad_LeftTrigger : EKeys::E;
	case EAct::AbilityR: return bPad ? EKeys::Gamepad_RightTrigger : EKeys::R;
	case EAct::Reap: return bPad ? EKeys::Gamepad_LeftShoulder : EKeys::LeftMouseButton;
	case EAct::Ping: return bPad ? EKeys::Gamepad_LeftThumbstick : EKeys::MiddleMouseButton;
	case EAct::Map: return bPad ? EKeys::Gamepad_Special_Left : EKeys::M;
	case EAct::Settings: return bPad ? EKeys::Gamepad_Special_Right : EKeys::P;
	default: return EKeys::Invalid;
	}
}

FKey SQInput::Key(EAct Act, bool bPad)
{
	if (const USQSaveGame* Save = USQSaveGame::Load())
	{
		const TMap<FName, FName>& Binds = bPad ? Save->PadBinds : Save->KeyBinds;
		if (const FName* Found = Binds.Find(ActId(Act)))
		{
			const FKey Saved(*Found);
			if (Saved.IsValid() && Saved.IsGamepadKey() == bPad && !SQIsReservedKey(Saved)) return Saved;
		}
	}
	return DefaultKey(Act, bPad);
}

bool SQInput::SetKey(EAct Act, bool bPad, const FKey& NewKey)
{
	USQSaveGame* Save = USQSaveGame::Load();
	if (!Save || !NewKey.IsValid() || NewKey.IsGamepadKey() != bPad || SQIsReservedKey(NewKey)) return false;
	// analog axes (stick directions, trigger axes, mouse movement) are not buttons
	if (NewKey.IsAxis1D() || NewKey.IsAxis2D() || NewKey.IsAxis3D()) return false;
	const FKey OldKey = Key(Act, bPad);
	if (OldKey == NewKey) return true;
	TMap<FName, FName>& Binds = bPad ? Save->PadBinds : Save->KeyBinds;
	for (int32 OtherIdx = 0; OtherIdx < NumActs; ++OtherIdx)
	{
		const EAct Other = static_cast<EAct>(OtherIdx);
		if (Other != Act && Key(Other, bPad) == NewKey)
		{
			// swap: the other action takes this one's old key
			if (OldKey == DefaultKey(Other, bPad)) Binds.Remove(ActId(Other));
			else Binds.Add(ActId(Other), OldKey.GetFName());
		}
	}
	if (NewKey == DefaultKey(Act, bPad)) Binds.Remove(ActId(Act));
	else Binds.Add(ActId(Act), NewKey.GetFName());
	USQSaveGame::Store(Save);
	return true;
}

void SQInput::ResetBinds()
{
	if (USQSaveGame* Save = USQSaveGame::Load())
	{
		Save->KeyBinds.Reset();
		Save->PadBinds.Reset();
		USQSaveGame::Store(Save);
	}
}

void SQInput::ApplyColourVision(uint8 Mode, float Strength)
{
	// Nov 25 2026: SETTINGS > ACCESS > Colour-blind mode. Slate's colour-vision CORRECTION over the whole game window
	// (world, HUD and menus): shifts the colours a protan / deutan / tritan player confuses into ones they can tell apart.
	// Only re-applied when the setting changes.
	static int32 AppliedKey = -1;
	const int32 Severity = FMath::Clamp(FMath::RoundToInt(Strength * 10.f), 0, 10); // the renderer takes 0..10
	const int32 Key = Mode == 0 ? 0 : (Mode * 100 + Severity);
	if (Key == AppliedKey || !FSlateApplication::IsInitialized()) return;
	FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
	if (!Renderer) return;
	AppliedKey = Key;
	const EColorVisionDeficiency Type = Mode == 1 ? EColorVisionDeficiency::Protanope
		: Mode == 2 ? EColorVisionDeficiency::Deuteranope
		: Mode == 3 ? EColorVisionDeficiency::Tritanope
		: EColorVisionDeficiency::NormalVision;
	Renderer->SetColorVisionDeficiencyType(Type, Mode == 0 ? 0 : Severity, /*bCorrectDeficiency*/ Mode != 0, /*bShowCorrectionWithDeficiency*/ false);
}

void SQInput::Poll(const APlayerController* PC)
{
	if (const USQSaveGame* Save = USQSaveGame::Load())
	{
		bSQShapeCues = Save->bShapeCues;
		bSQReduceFlash = Save->bReduceFlash;
		bSQVibration = Save->bVibration;
		ApplyColourVision(Save->ColourBlindMode, Save->ColourBlindStrength);
	}
	if (!PC || !PC->IsLocalController()) return;
	// Gamepad: any button, or a stick pushed well off centre
	bool bPadNow = false;
	for (int32 ButtonIdx = 0; ButtonIdx < SQNumPadButtons && !bPadNow; ++ButtonIdx)
	{
		bPadNow = PC->WasInputKeyJustPressed(SQPadButton(ButtonIdx));
	}
	if (!bPadNow)
	{
		bPadNow = SQStickPast(PC, EKeys::Gamepad_LeftX, 0.5f) || SQStickPast(PC, EKeys::Gamepad_LeftX, -0.5f)
			|| SQStickPast(PC, EKeys::Gamepad_LeftY, 0.5f) || SQStickPast(PC, EKeys::Gamepad_LeftY, -0.5f)
			|| SQStickPast(PC, EKeys::Gamepad_RightX, 0.5f) || SQStickPast(PC, EKeys::Gamepad_RightX, -0.5f)
			|| SQStickPast(PC, EKeys::Gamepad_RightY, 0.5f) || SQStickPast(PC, EKeys::Gamepad_RightY, -0.5f);
	}
	if (bPadNow)
	{
		bSQUsingPad = true;
		return;
	}
	// Keyboard / mouse: a common key, a mouse button or the mouse moving
	static const FKey Common[] = {
		EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::SpaceBar, EKeys::LeftShift, EKeys::LeftControl, EKeys::E, EKeys::R, EKeys::F, EKeys::Q,
		EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton, EKeys::Enter, EKeys::Escape, EKeys::Tab, EKeys::P, EKeys::M,
		EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right, EKeys::One, EKeys::Two, EKeys::Three,
	};
	bool bKeysNow = false;
	for (const FKey& CommonKey : Common)
	{
		if (PC->WasInputKeyJustPressed(CommonKey))
		{
			bKeysNow = true;
			break;
		}
	}
	if (!bKeysNow)
	{
		double MouseDX = 0.0;
		double MouseDY = 0.0;
		PC->GetInputMouseDelta(MouseDX, MouseDY);
		bKeysNow = FMath::Abs(MouseDX) + FMath::Abs(MouseDY) > 2.0;
	}
	if (bKeysNow) bSQUsingPad = false;
}

bool SQInput::UsingPad()
{
	return bSQUsingPad;
}

bool SQInput::JustPressed(const APlayerController* PC, EAct Act)
{
	return PC && (PC->WasInputKeyJustPressed(Key(Act, false)) || PC->WasInputKeyJustPressed(Key(Act, true)));
}

bool SQInput::IsDown(const APlayerController* PC, EAct Act)
{
	return PC && (PC->IsInputKeyDown(Key(Act, false)) || PC->IsInputKeyDown(Key(Act, true)));
}

FString SQInput::KeyLabel(const FKey& InKey)
{
	if (!InKey.IsValid()) return FString(TEXT("-"));
	struct FSQKeyName
	{
		FKey Key;
		const TCHAR* Text;
	};
	static const FSQKeyName Names[] = {
		{ EKeys::Gamepad_FaceButton_Bottom, TEXT("A") }, { EKeys::Gamepad_FaceButton_Right, TEXT("B") },
		{ EKeys::Gamepad_FaceButton_Left, TEXT("X") }, { EKeys::Gamepad_FaceButton_Top, TEXT("Y") },
		{ EKeys::Gamepad_LeftShoulder, TEXT("LB") }, { EKeys::Gamepad_RightShoulder, TEXT("RB") },
		{ EKeys::Gamepad_LeftTrigger, TEXT("LT") }, { EKeys::Gamepad_RightTrigger, TEXT("RT") },
		{ EKeys::Gamepad_LeftThumbstick, TEXT("LS") }, { EKeys::Gamepad_RightThumbstick, TEXT("RS") },
		{ EKeys::Gamepad_Special_Left, TEXT("VIEW") }, { EKeys::Gamepad_Special_Right, TEXT("MENU") },
		{ EKeys::Gamepad_DPad_Up, TEXT("D-UP") }, { EKeys::Gamepad_DPad_Down, TEXT("D-DOWN") },
		{ EKeys::Gamepad_DPad_Left, TEXT("D-LEFT") }, { EKeys::Gamepad_DPad_Right, TEXT("D-RIGHT") },
		{ EKeys::SpaceBar, TEXT("SPACE") }, { EKeys::LeftShift, TEXT("SHIFT") }, { EKeys::RightShift, TEXT("R-SHIFT") },
		{ EKeys::LeftControl, TEXT("CTRL") }, { EKeys::RightControl, TEXT("R-CTRL") }, { EKeys::LeftAlt, TEXT("ALT") },
		{ EKeys::LeftMouseButton, TEXT("L-CLICK") }, { EKeys::RightMouseButton, TEXT("R-CLICK") }, { EKeys::MiddleMouseButton, TEXT("M-CLICK") },
		{ EKeys::ThumbMouseButton, TEXT("MOUSE 4") }, { EKeys::ThumbMouseButton2, TEXT("MOUSE 5") },
		{ EKeys::Enter, TEXT("ENTER") }, { EKeys::Escape, TEXT("ESC") }, { EKeys::Tab, TEXT("TAB") }, { EKeys::BackSpace, TEXT("BACKSPACE") },
		{ EKeys::CapsLock, TEXT("CAPS") },
	};
	for (const FSQKeyName& Name : Names)
	{
		if (Name.Key == InKey) return FString(Name.Text);
	}
	return InKey.GetDisplayName().ToString().ToUpper();
}

FString SQInput::Label(EAct Act)
{
	return KeyLabel(Key(Act, bSQUsingPad));
}

FString SQInput::TranslateCap(const FString& CapText)
{
	// Compound caps from the controls list
	if (CapText == TEXT("SHIFT / R-CLICK")) return bSQUsingPad ? Label(EAct::Dash) : Label(EAct::Dash) + TEXT(" / R-CLICK");
	if (CapText == TEXT("CTRL / C")) return bSQUsingPad ? Label(EAct::Slide) : Label(EAct::Slide) + TEXT(" / C");
	if (CapText == TEXT("MOUSE")) return bSQUsingPad ? FString(TEXT("R-STICK")) : CapText;
	if (CapText == TEXT("WASD")) return bSQUsingPad ? FString(TEXT("L-STICK")) : CapText;
	if (CapText == TEXT("WHEEL / HOME")) return bSQUsingPad ? FString(TEXT("-")) : CapText;
	if (CapText == TEXT("HOLD ALT")) return bSQUsingPad ? FString(TEXT("-")) : CapText;
	if (CapText == TEXT("1 2 3  Q / X")) return bSQUsingPad ? FString(TEXT("D-PAD  Y / B")) : CapText;
	if (CapText == TEXT("ENTER")) return bSQUsingPad ? FString(TEXT("A")) : CapText;
	if (CapText == TEXT("ESC")) return bSQUsingPad ? FString(TEXT("B")) : CapText;
	for (int32 ActIdx = 0; ActIdx < NumActs; ++ActIdx)
	{
		if (CapText == SQActDefs[ActIdx].LegacyCap) return Label(static_cast<EAct>(ActIdx));
	}
	return CapText;
}

FString SQInput::TranslateInline(const FString& Sentence)
{
	if (!Sentence.Contains(TEXT("["))) return Sentence;
	FString Out = Sentence;
	for (int32 ActIdx = 0; ActIdx < NumActs; ++ActIdx)
	{
		const FString Marker = FString::Printf(TEXT("[%s]"), SQActDefs[ActIdx].LegacyCap);
		if (Out.Contains(Marker))
		{
			Out = Out.Replace(*Marker, *FString::Printf(TEXT("[%s]"), *Label(static_cast<EAct>(ActIdx))));
		}
	}
	return Out;
}

FString SQInput::ControlsLine()
{
	if (bSQUsingPad)
	{
		return FString::Printf(TEXT("L-stick move  -  R-stick look  -  %s jump  -  %s dash  -  %s reap  -  %s ability  -  %s ultimate  -  %s interact  -  %s ping  -  %s map  -  %s settings"),
			*Label(EAct::Jump), *Label(EAct::Dash), *Label(EAct::Reap), *Label(EAct::AbilityE), *Label(EAct::AbilityR), *Label(EAct::Interact),
			*Label(EAct::Ping), *Label(EAct::Map), *Label(EAct::Settings));
	}
	return FString::Printf(TEXT("WASD move  -  Mouse look  -  %s jump  -  %s / R-click dash  -  %s reap  -  %s ability  -  %s ultimate  -  %s interact  -  %s ping  -  %s map  -  Alt cursor  -  %s settings"),
		*Label(EAct::Jump), *Label(EAct::Dash), *Label(EAct::Reap), *Label(EAct::AbilityE), *Label(EAct::AbilityR), *Label(EAct::Interact),
		*Label(EAct::Ping), *Label(EAct::Map), *Label(EAct::Settings));
}

SQInput::FNav SQInput::ReadNav(const APlayerController* PC, float RealNow)
{
	FNav Nav;
	if (!PC) return Nav;
	Nav.bUp = PC->WasInputKeyJustPressed(EKeys::Up) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up);
	Nav.bDown = PC->WasInputKeyJustPressed(EKeys::Down) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down);
	Nav.bLeft = PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left);
	Nav.bRight = PC->WasInputKeyJustPressed(EKeys::Right) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right);
	Nav.bConfirm = PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
	Nav.bBack = PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);
	Nav.bTabNext = PC->WasInputKeyJustPressed(EKeys::Tab) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder);
	Nav.bTabPrev = PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder);

	// Left stick as a d-pad: fires once past 0.6, then repeats (0.4 s, then every 0.12 s) while held
	int32 Dir = 0;
	if (SQStickPast(PC, EKeys::Gamepad_LeftY, 0.6f)) Dir = 1;
	else if (SQStickPast(PC, EKeys::Gamepad_LeftY, -0.6f)) Dir = 2;
	else if (SQStickPast(PC, EKeys::Gamepad_LeftX, -0.6f)) Dir = 3;
	else if (SQStickPast(PC, EKeys::Gamepad_LeftX, 0.6f)) Dir = 4;
	bool bFire = false;
	if (Dir == 0)
	{
		SQNavHeldDir = 0;
	}
	else if (Dir != SQNavHeldDir)
	{
		SQNavHeldDir = Dir;
		SQNavNextRepeat = RealNow + 0.4f;
		bFire = true;
	}
	else if (RealNow >= SQNavNextRepeat)
	{
		SQNavNextRepeat = RealNow + 0.12f;
		bFire = true;
	}
	if (bFire)
	{
		Nav.bUp = Nav.bUp || Dir == 1;
		Nav.bDown = Nav.bDown || Dir == 2;
		Nav.bLeft = Nav.bLeft || Dir == 3;
		Nav.bRight = Nav.bRight || Dir == 4;
	}
	return Nav;
}

FKey SQInput::FirstKeyPressed(const APlayerController* PC, bool bPad)
{
	if (!PC) return EKeys::Invalid;
	if (bPad)
	{
		for (int32 ButtonIdx = 0; ButtonIdx < SQNumPadButtons; ++ButtonIdx)
		{
			if (PC->WasInputKeyJustPressed(SQPadButton(ButtonIdx))) return SQPadButton(ButtonIdx);
		}
		return EKeys::Invalid;
	}
	TArray<FKey> AllKeys;
	EKeys::GetAllKeys(AllKeys);
	for (const FKey& Candidate : AllKeys)
	{
		if (Candidate.IsGamepadKey() || Candidate.IsTouch() || Candidate.IsAxis1D() || Candidate.IsAxis2D() || Candidate.IsAxis3D()) continue;
		if (Candidate == EKeys::AnyKey) continue;
		if (PC->WasInputKeyJustPressed(Candidate)) return Candidate;
	}
	return EKeys::Invalid;
}

void SQInput::Rumble(APlayerController* PC, float Strength, float Seconds)
{
	if (!PC || !bSQUsingPad || !bSQVibration || !PC->IsLocalController()) return;
	const float Power = FMath::Clamp(Strength, 0.f, 1.f);
	if (Power <= 0.01f || Seconds <= 0.f) return;
	PC->PlayDynamicForceFeedback(Power, FMath::Min(Seconds, 1.5f), true, true, true, true);
}

bool SQInput::ShapeCues()
{
	return bSQShapeCues;
}

bool SQInput::ReduceFlash()
{
	return bSQReduceFlash;
}

float SQInput::FlashScale()
{
	return bSQReduceFlash ? 0.3f : 1.f;
}
