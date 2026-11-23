// Copyright 2026 Jack McGuigan. All rights reserved.
// The one palette for the main menu, the lobby and the in-run HUD.
// Midnight ink + violet panels, ghost-soul teal = selected / go, lantern amber = money (gold / banked gold),
// bone white text, a cream card for the title. Creepster for big titles only, Lilita One for buttons
// and headings, Rubik for labels (Scripts/import_fonts.py imports them to /Game/UI/Fonts).
// Nov 9 refinement: a deeper panel shade for layered panels, a brighter gold for AUGMENTS, and one accent colour per
// reaper role (RoleColour) used by the lobby, the ability bar and the crew cards.

#pragma once

#include "CoreMinimal.h"

namespace SQUI
{
	/** sRGB hex (as in the designs) to a linear colour. */
	inline FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A = 1.f)
	{
		FLinearColor C(FColor(R, G, B));
		C.A = A;
		return C;
	}

	/** Same colour, different alpha. */
	inline FLinearColor WithAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, A);
	}

	inline const FLinearColor Midnight = Hex(0x0E, 0x0B, 0x16);        // background
	inline const FLinearColor Bar = Hex(0x09, 0x07, 0x0F);        // bottom bar, top strip
	inline const FLinearColor Panel = Hex(0x1A, 0x15, 0x27);
	inline const FLinearColor PanelHover = Hex(0x26, 0x1E, 0x3A);
	inline const FLinearColor Line = Hex(0x3A, 0x2F, 0x55);
	inline const FLinearColor Soul = Hex(0x5C, 0xF2, 0xD6);       // primary: selected / go
	inline const FLinearColor SoulDeep = Hex(0x2B, 0xB8, 0xA0);   // primary pressed / shade
	inline const FLinearColor Amber = Hex(0xFF, 0xB5, 0x47);      // overtime pay
	inline const FLinearColor BoneWhite = Hex(0xF2, 0xEB, 0xDD);       // text
	inline const FLinearColor Muted = Hex(0xA6, 0x9C, 0xB8);
	inline const FLinearColor Disabled = Hex(0x8A, 0x80, 0xA0);        // disabled text (Nov 10: lifted from 6B6280 for contrast on Panel)
	inline const FLinearColor Danger = Hex(0xFF, 0x6B, 0x6B);
	inline const FLinearColor Card = Hex(0xEF, 0xE6, 0xCF);       // time card paper
	inline const FLinearColor CardInk = Hex(0x2A, 0x22, 0x38);    // printing on the card
	inline const FLinearColor Stamp = Hex(0xD8, 0x43, 0x4B);      // red rubber stamp
	inline const FLinearColor PanelDeep = Hex(0x12, 0x0E, 0x1C);  // lower layer of a layered panel
	inline const FLinearColor Gold = Hex(0xFF, 0xD2, 0x6A);       // augments
	inline const FLinearColor GoldDeep = Hex(0xB9, 0x7A, 0x22);   // augment frame shade
	inline const FLinearColor Shield = Hex(0x7F, 0xA8, 0xFF);     // defender / tank role
	inline const FLinearColor Arcane = Hex(0xB9, 0x8C, 0xFF);     // mobility / caster role

	/**
	 * Readable text size (Nov 10 2026 menu pass), applied inside the HUD / menu text primitives so every screen gets it:
	 * small design sizes are lifted toward 16 px (12 -> 13.2, 10 -> 11.8) and nothing renders under ~10 real pixels at
	 * low resolutions (720p used to shrink 12 px labels to 8 px). ScaleToReal = design px -> screen px.
	 */
	inline float ReadablePx(float Px, float ScaleToReal)
	{
		float Out = Px < 16.f ? Px + (16.f - Px) * 0.3f : Px;
		const float Floor = 10.f / FMath::Max(ScaleToReal, 0.05f);
		if (Out < Floor) Out = FMath::Min(Floor, Out * 1.25f);
		return Out;
	}

	/** Accent colour of a reaper role (Reapers.csv Role). Unknown roles use the reaper's own colour, brightened a little. */
	inline FLinearColor RoleColour(FName Role, const FLinearColor& Fallback)
	{
		const FString R = Role.ToString();
		if (R == TEXT("Healer") || R == TEXT("Reviver") || R == TEXT("Support")) return Soul;
		if (R == TEXT("Defender") || R == TEXT("Control") || R == TEXT("Tank")) return Shield;
		if (R == TEXT("Striker") || R == TEXT("Berserker") || R == TEXT("Damage")) return Danger;
		if (R == TEXT("Economy") || R == TEXT("Banker") || R == TEXT("Hauler") || R == TEXT("Greed") || R == TEXT("Fetcher") || R == TEXT("Mobile bank")) return Amber;
		if (R == TEXT("Mobility") || R == TEXT("Speed") || R == TEXT("Scout") || R == TEXT("Summoner") || R == TEXT("Time")) return Arcane;
		return FMath::Lerp(Fallback, BoneWhite, 0.2f);
	}
}
