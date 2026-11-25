// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class USQSaveGame;

/**
 * Platform pass (Nov 23 2026): one place for "which key does this action use", "is the player on a gamepad right now",
 * key-cap text for both devices, menu navigation with stick repeat, and controller rumble.
 *
 * Rebinding: the profile stores only the changed binds (USQSaveGame::KeyBinds / PadBinds, action id -> key name).
 * ASQPlayerController::BuildMappings maps every action through SQInput::Key, so a rebind is live after RebuildBindings().
 * Movement (WASD / left stick), the camera (mouse / right stick) and the card keys (1 2 3 / d-pad) are fixed.
 *
 * Glyphs are plain text caps (A B X Y, LB RB LT RT, LS RS, VIEW, MENU): letters and positions only, no console logos.
 */
namespace SQInput
{
	enum class EAct : uint8
	{
		Jump,
		Dash,
		Slide,
		Interact,
		AbilityE,
		AbilityR,
		Reap,
		Ping,
		Map,
		Settings,
		Count
	};

	constexpr int32 NumActs = static_cast<int32>(EAct::Count);

	/** Profile id of an action ("Jump") and its name on the CONTROLS tab ("Jump (hold to hop)") */
	FName ActId(EAct Act);
	const TCHAR* ActName(EAct Act);
	FKey DefaultKey(EAct Act, bool bPad);
	/** The key in use: the profile's rebind if it is a valid key of the right device, else the default */
	FKey Key(EAct Act, bool bPad);
	/**
	 * Rebinds Act (keyboard / mouse or gamepad). If another action already used NewKey the two swap, so no action is left
	 * without a key. Refuses keys of the wrong device and the reserved ones (Esc, the card keys, the sticks). Saves the profile.
	 */
	bool SetKey(EAct Act, bool bPad, const FKey& NewKey);
	void ResetBinds();

	/** Call once a frame for the local player: tracks which device was used last */
	void Poll(const APlayerController* PC);
	bool UsingPad();

	bool JustPressed(const APlayerController* PC, EAct Act);
	bool IsDown(const APlayerController* PC, EAct Act);

	/** Short cap text for a key: "SPACE", "L-CLICK", "A", "RT" */
	FString KeyLabel(const FKey& InKey);
	/** Cap text of an action for the device in use */
	FString Label(EAct Act);
	/**
	 * Key-cap text written for the default keyboard layout ("F", "SPACE", "SHIFT", "L-CLICK", "E", "R", "M", "P") becomes the
	 * cap for the bound key on the device in use. Anything else comes back unchanged.
	 */
	FString TranslateCap(const FString& CapText);
	/** Replaces "[F]" style markers inside a sentence ("CHEST  [F]" -> "CHEST  [X]") */
	FString TranslateInline(const FString& Sentence);
	/** One line listing the controls for the device in use (the small hint line bottom-right) */
	FString ControlsLine();

	/** Menu / panel navigation for this frame: keyboard arrows, d-pad and left stick (with hold-repeat), Enter / A, Esc / B, Tab / RB, LB */
	struct FNav
	{
		bool bUp = false;
		bool bDown = false;
		bool bLeft = false;
		bool bRight = false;
		bool bConfirm = false;
		bool bBack = false;
		bool bTabNext = false;
		bool bTabPrev = false;
	};
	FNav ReadNav(const APlayerController* PC, float RealNow);

	/** Rebinding: the first key that went down this frame (keyboard / mouse when !bPad, gamepad buttons when bPad). Invalid if none. */
	FKey FirstKeyPressed(const APlayerController* PC, bool bPad);

	/** Controller rumble (does nothing on keyboard, with SETTINGS > Vibration off, or for a non-local controller). Strength 0..1. */
	void Rumble(APlayerController* PC, float Strength, float Seconds);

	// ---- accessibility switches read by drawing code (cached from the profile by Poll, so hot paths do not load it)
	bool ShapeCues();
	bool ReduceFlash();
	/** Colour-blind filter (USQSaveGame::ColourBlindMode 0 off .. 3 tritan, strength 0..1); cheap, only applies on change */
	void ApplyColourVision(uint8 Mode, float Strength);
	/** 1, or 0.3 with SETTINGS > Reduce flashes */
	float FlashScale();
}
