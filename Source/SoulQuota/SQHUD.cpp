// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQHUD.h"

#include "CanvasItem.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/World.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "SceneView.h"
#include "Sound/SoundBase.h"
#include "SQArena.h"
#include "SQBoss.h"
#include "SQCharacter.h"
#include "SQData.h"
#include "SQFx.h"
#include "SQGameState.h"
#include "SQHordeManager.h"
#include "SQPlayerController.h"
#include "SQPlayerState.h"
#include "SQSave.h"
#include "SQUIStyle.h"
#include "SQChest.h"
#include "SQPortal.h"
#include "SQQuests.h"
#include "EngineUtils.h"
#include "SQTutorial.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "SQMenuShowroom.h"
#include "SQAbility.h"
#include "SQMission.h"
#include "SQAugments.h"
#include "SQReap.h"
#include "SQShrine.h"
#include "SQPot.h"
#include "Engine/Texture2D.h"
#include <initializer_list>

// Every helper is an ASQHUD member (nothing at file scope), so this file is safe in unity builds.

// ============================================================ easing

float ASQHUD::HudEaseOut(float T)
{
	const float Clamped = FMath::Clamp(T, 0.f, 1.f);
	return 1.f - (1.f - Clamped) * (1.f - Clamped) * (1.f - Clamped);
}

float ASQHUD::HudBackOut(float T)
{
	const float Clamped = FMath::Clamp(T, 0.f, 1.f);
	const float Over = 1.70158f;
	const float Mv = Clamped - 1.f;
	return 1.f + (Over + 1.f) * Mv * Mv * Mv + Over * Mv * Mv;
}

// ============================================================ fonts and text

UFont* ASQHUD::HudMakeFont(UFontFace* Face, int32 Size)
{
	if (!Face)
	{
		return nullptr;
	}
	UFont* NewFont = NewObject<UFont>(this);
	NewFont->FontCacheType = EFontCacheType::Runtime;
	NewFont->LegacyFontSize = Size;
	FTypefaceEntry& Entry = NewFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
	Entry.Name = TEXT("Regular");
	Entry.Font = FFontData(Face);
	return NewFont;
}

void ASQHUD::HudLoadFonts()
{
	if (bHudFontsLoaded)
	{
		return;
	}
	bHudFontsLoaded = true;
	HudCreepster = HudMakeFont(LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/Fonts/FF_Creepster.FF_Creepster"), nullptr, LOAD_Quiet | LOAD_NoWarn), 64);
	HudLilita = HudMakeFont(LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/Fonts/FF_LilitaOne.FF_LilitaOne"), nullptr, LOAD_Quiet | LOAD_NoWarn), 48);
	HudRubikMedium = HudMakeFont(LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/Fonts/FF_Rubik500.FF_Rubik500"), nullptr, LOAD_Quiet | LOAD_NoWarn), 40);
	HudRubikBold = HudMakeFont(LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/Fonts/FF_Rubik700.FF_Rubik700"), nullptr, LOAD_Quiet | LOAD_NoWarn), 40);
}

UFont* ASQHUD::HudHeadFont() const
{
	if (HudLilita)
	{
		return HudLilita.Get();
	}
	return GEngine ? GEngine->GetLargeFont() : nullptr;
}

UFont* ASQHUD::HudDisplayFont() const
{
	return HudCreepster ? HudCreepster.Get() : HudHeadFont();
}

UFont* ASQHUD::HudLabelFont(bool bBold) const
{
	UFont* Picked = bBold ? HudRubikBold.Get() : HudRubikMedium.Get();
	if (!Picked)
	{
		Picked = bBold ? HudRubikMedium.Get() : HudRubikBold.Get();
	}
	return Picked ? Picked : HudHeadFont();
}

bool ASQHUD::HudMouseDesign(float& OutX, float& OutY) const
{
	float RawX = 0.f, RawY = 0.f;
	if (!PlayerOwner || !PlayerOwner->GetMousePosition(RawX, RawY))
	{
		return false;
	}
	// GetMousePosition is in whole-viewport pixels; the canvas is translated to this player's view rect
	if (Canvas && Canvas->SceneView)
	{
		RawX -= static_cast<float>(Canvas->SceneView->UnscaledViewRect.Min.X);
		RawY -= static_cast<float>(Canvas->SceneView->UnscaledViewRect.Min.Y);
	}
	const float MouseScale = FMath::Max(UIScale, 0.01f);
	OutX = RawX / MouseScale;
	OutY = RawY / MouseScale;
	return true;
}

float ASQHUD::HudTxtW(const FString& Str, UFont* InFont, float InPx)
{
	if (!InFont || !Canvas || Str.IsEmpty())
	{
		return 0.f;
	}
	const float Px = SQUI::ReadablePx(InPx, UIScale);
	float& LineAtOne = HudFontLine.FindOrAdd(InFont);
	if (LineAtOne <= 0.f)
	{
		float MeasureW = 0.f;
		GetTextSize(TEXT("Hg"), MeasureW, LineAtOne, InFont, 1.f);
		LineAtOne = FMath::Max(LineAtOne, 1.f);
	}
	const float TextScale = Px * UIScale * 1.2f / LineAtOne;
	float OutW = 0.f, OutH = 0.f;
	GetTextSize(Str, OutW, OutH, InFont, TextScale);
	return OutW / FMath::Max(UIScale, 0.01f);
}

float ASQHUD::HudTxt(const FString& Str, UFont* InFont, float InPx, float X, float Y, const FLinearColor& Colour, int32 Align, bool bShadow)
{
	if (!InFont || !Canvas || Str.IsEmpty())
	{
		return 0.f;
	}
	const float TW = HudTxtW(Str, InFont, InPx); // also measures the font
	const float Px = SQUI::ReadablePx(InPx, UIScale);
	const float TextScale = Px * UIScale * 1.2f / FMath::Max(HudFontLine.FindRef(InFont), 1.f);
	const float LX = Align == 1 ? X - TW * 0.5f : (Align == 2 ? X - TW : X);
	const float A = Colour.A * HudAlpha;
	if (A <= 0.003f)
	{
		return TW;
	}
	if (bShadow)
	{
		// Readability: a 1 px dark outline (real pixels, so it stays crisp at any size) + a soft drop shadow,
		// so text holds up over the bright arena as well as on panels
		const float OX = LX * UIScale, OY = Y * UIScale;
		const FLinearColor Edge(0.02f, 0.01f, 0.04f, 0.6f * A);
		static const FVector2D OutlineDirs[4] = { FVector2D(-1.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, -1.0), FVector2D(0.0, 1.0) };
		for (const FVector2D& Dir : OutlineDirs)
		{
			DrawText(Str, Edge, OX + static_cast<float>(Dir.X), OY + static_cast<float>(Dir.Y), InFont, TextScale);
		}
		DrawText(Str, FLinearColor(0.f, 0.f, 0.f, 0.55f * A), (LX + 1.5f) * UIScale, (Y + 2.f) * UIScale, InFont, TextScale);
	}
	DrawText(Str, SQUI::WithAlpha(Colour, A), LX * UIScale, Y * UIScale, InFont, TextScale);
	return TW;
}

void ASQHUD::HudGlowTxt(const FString& Str, UFont* InFont, float Px, float X, float Y, const FLinearColor& Colour, const FLinearColor& GlowColour, int32 Align)
{
	// a soft halo: the text stamped round itself, faint, at two radii
	for (int32 Ring = 2; Ring >= 1; --Ring)
	{
		const float Off = Px * 0.05f * Ring;
		const FLinearColor Halo = SQUI::WithAlpha(GlowColour, GlowColour.A * (Ring == 2 ? 0.08f : 0.16f));
		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const float Ang = Dir * UE_PI * 0.25f;
			HudTxt(Str, InFont, Px, X + FMath::Cos(Ang) * Off, Y + FMath::Sin(Ang) * Off, Halo, Align, false);
		}
	}
	HudTxt(Str, InFont, Px, X, Y, Colour, Align, true);
}

float ASQHUD::HudFitPx(const FString& Str, UFont* InFont, float Px, float MaxW)
{
	const float TW = HudTxtW(Str, InFont, Px);
	return TW > MaxW && TW > 0.f ? Px * MaxW / TW : Px;
}

int32 ASQHUD::HudWrap(const FString& Str, UFont* InFont, float Px, float X, float Y, float MaxW, const FLinearColor& Colour, float LineStep, int32 MaxLines,
	bool bDraw, int32 Align)
{
	LineStep = FMath::Max(LineStep, SQUI::ReadablePx(Px, UIScale) * 1.12f); // lifted small text keeps its lines apart
	TArray<FString> Paras;
	Str.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Paras, TEXT("\n"), true);
	int32 Lines = 0;
	for (int32 ParaIdx = 0; ParaIdx < Paras.Num(); ++ParaIdx)
	{
		TArray<FString> Words;
		Paras[ParaIdx].ParseIntoArray(Words, TEXT(" "), true);
		FString Current;
		for (int32 WordIdx = 0; WordIdx < Words.Num(); ++WordIdx)
		{
			const FString Try = Current.IsEmpty() ? Words[WordIdx] : Current + TEXT(" ") + Words[WordIdx];
			if (!Current.IsEmpty() && HudTxtW(Try, InFont, Px) > MaxW)
			{
				if (MaxLines > 0 && Lines == MaxLines - 1)
				{
					FString Cut = Current + TEXT("...");
					while (Cut.Len() > 4 && HudTxtW(Cut, InFont, Px) > MaxW)
					{
						Cut = Cut.LeftChop(4) + TEXT("...");
					}
					if (bDraw)
					{
						HudTxt(Cut, InFont, Px, X, Y + Lines * LineStep, Colour, Align, true);
					}
					return MaxLines;
				}
				if (bDraw)
				{
					HudTxt(Current, InFont, Px, X, Y + Lines * LineStep, Colour, Align, true);
				}
				++Lines;
				Current = Words[WordIdx];
			}
			else
			{
				Current = Try;
			}
		}
		if (!Current.IsEmpty())
		{
			const bool bLastAllowed = MaxLines > 0 && Lines == MaxLines - 1 && ParaIdx < Paras.Num() - 1;
			if (bLastAllowed)
			{
				Current += TEXT("...");
			}
			if (bDraw)
			{
				HudTxt(Current, InFont, Px, X, Y + Lines * LineStep, Colour, Align, true);
			}
			++Lines;
			if (bLastAllowed)
			{
				return Lines;
			}
		}
	}
	return Lines;
}

// ============================================================ shapes

void ASQHUD::HudRectPx(const FLinearColor& Colour, float PX, float PY, float PW, float PH)
{
	const float A = Colour.A * HudAlpha;
	if (PW <= 0.f || PH <= 0.f || A <= 0.002f)
	{
		return;
	}
	DrawRect(SQUI::WithAlpha(Colour, A), PX, PY, PW, PH);
}

void ASQHUD::HudRect(const FLinearColor& Colour, float X, float Y, float W, float H)
{
	HudRectPx(Colour, X * UIScale, Y * UIScale, W * UIScale, H * UIScale);
}

void ASQHUD::HudRound(const FLinearColor& Colour, float X, float Y, float W, float H, float Radius)
{
	const float PX = X * UIScale, PY = Y * UIScale, PW = W * UIScale, PH = H * UIScale;
	if (PW <= 0.f || PH <= 0.f)
	{
		return;
	}
	const float PR = FMath::Min(Radius * UIScale, FMath::Min(PW, PH) * 0.5f);
	if (PR < 1.5f)
	{
		HudRectPx(Colour, PX, PY, PW, PH);
		return;
	}
	HudRectPx(Colour, PX, PY + PR, PW, PH - 2.f * PR);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(PR / 1.5f), 2, 16);
	const float StepH = PR / Steps;
	for (int32 i = 0; i < Steps; ++i)
	{
		const float DY = PR - (i + 0.5f) * StepH;
		const float Inset = PR - FMath::Sqrt(FMath::Max(0.f, PR * PR - DY * DY));
		HudRectPx(Colour, PX + Inset, PY + i * StepH, PW - 2.f * Inset, StepH);
		HudRectPx(Colour, PX + Inset, PY + PH - (i + 1) * StepH, PW - 2.f * Inset, StepH);
	}
}

void ASQHUD::HudDisc(const FLinearColor& Colour, float CX, float CY, float Radius)
{
	HudRound(Colour, CX - Radius, CY - Radius, 2.f * Radius, 2.f * Radius, Radius);
}

void ASQHUD::HudGlow(const FLinearColor& Colour, float X, float Y, float W, float H, float Radius, float Strength)
{
	if (Strength <= 0.01f)
	{
		return;
	}
	constexpr int32 Rings = 6;
	for (int32 k = Rings; k >= 1; --k)
	{
		const float Grow = k * 3.f;
		const float A = Strength * 0.09f * (1.f - float(k) / (Rings + 1));
		HudRound(SQUI::WithAlpha(Colour, A), X - Grow, Y - Grow, W + 2.f * Grow, H + 2.f * Grow, Radius + Grow);
	}
}

void ASQHUD::HudPanel(float X, float Y, float W, float H, float Radius, const FLinearColor& Edge, float FillAlpha)
{
	HudRound(FLinearColor(0.f, 0.f, 0.f, 0.35f), X + 5.f, Y + 8.f, W, H, Radius);              // soft drop shadow
	HudRound(Edge, X - 2.f, Y - 2.f, W + 4.f, H + 4.f, Radius + 2.f);                        // outline
	HudRound(SQUI::WithAlpha(SQUI::Panel, FillAlpha), X, Y, W, H, Radius);                  // body
	HudRound(FLinearColor(1.f, 1.f, 1.f, 0.025f), X + 3.f, Y + 3.f, W - 6.f, H * 0.42f, FMath::Max(0.f, Radius - 3.f)); // top sheen
}

void ASQHUD::HudShine(float X, float Y, float W, float H, float Period, float Inset, float Offset)
{
	const float Phase01 = FMath::Fmod(HudNow + Offset, Period) / Period;
	const float BandW = 50.f;
	const float Slant = 0.5f;
	const float Travel = W + BandW + H * Slant;
	const float BandX = X - BandW - H * Slant + Phase01 * 1.8f * Travel; // sweeps in the first ~55%, then rests
	constexpr int32 Rows = 12;
	const float RowH = (H - 2.f * Inset) / Rows;
	for (int32 r = 0; r < Rows; ++r)
	{
		const float RY = Y + Inset + r * RowH;
		const float Shift = (H - (RY - Y)) * Slant;
		const float L = FMath::Max(BandX + Shift, X + Inset + 6.f);
		const float Rt = FMath::Min(BandX + Shift + BandW, X + W - Inset - 6.f);
		if (Rt > L)
		{
			HudRect(FLinearColor(1.f, 1.f, 1.f, 0.22f), L, RY, Rt - L, RowH + 0.5f);
		}
	}
}

void ASQHUD::HudTilt(const FLinearColor& Colour, float X, float Y, float W, float H, float Deg)
{
	const float A = Colour.A * HudAlpha;
	if (!Canvas || A <= 0.002f)
	{
		return;
	}
	FCanvasTileItem Tile(FVector2D(X * UIScale, Y * UIScale), FVector2D(W * UIScale, H * UIScale), SQUI::WithAlpha(Colour, A));
	Tile.Rotation = FRotator(0.f, Deg, 0.f);
	Tile.PivotPoint = FVector2D(0.5f, 0.5f);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ASQHUD::HudBar(float X, float Y, float W, float H, float Fill, float Ghost, const FLinearColor& Colour)
{
	const float Radius = H * 0.5f;
	const float F = FMath::Clamp(Fill, 0.f, 1.f);
	const float G = FMath::Clamp(Ghost, 0.f, 1.f);
	HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.92f), X, Y, W, H, Radius);
	HudRound(FLinearColor(1.f, 1.f, 1.f, 0.04f), X, Y, W, H * 0.5f, Radius);
	if (G > F + 0.002f)
	{
		HudRound(SQUI::WithAlpha(SQUI::BoneWhite, 0.85f), X, Y, FMath::Max(H, W * G), H, Radius); // damage trail
	}
	if (F > 0.002f)
	{
		const float FW = FMath::Max(H, W * F);
		HudRound(Colour, X, Y, FW, H, Radius);
		HudRound(FLinearColor(0.f, 0.f, 0.f, 0.16f), X, Y + H * 0.55f, FW, H * 0.45f, Radius * 0.6f);
		HudRound(FLinearColor(1.f, 1.f, 1.f, 0.25f), X + 3.f, Y + 2.f, FMath::Max(0.f, FW - 6.f), FMath::Max(1.5f, H * 0.28f), Radius * 0.5f);
	}
}

float ASQHUD::HudKeyCap(const FString& KeyName, float X, float Y, float CapH)
{
	const float Px = CapH * 0.5f;
	const float KW = FMath::Max(CapH, HudTxtW(KeyName, HudLabelFont(true), Px) + 16.f);
	HudRound(SQUI::Line, X, Y, KW, CapH, 6.f);
	HudRound(SQUI::PanelHover, X + 2.f, Y + 1.5f, KW - 4.f, CapH - 6.f, 5.f);
	HudTxt(KeyName, HudLabelFont(true), Px, X + KW * 0.5f, Y + (CapH - 4.f) * 0.5f - 0.62f * Px, SQUI::BoneWhite, 1, false);
	return KW;
}

float ASQHUD::HudPill(const FString& Label, float X, float Y, const FLinearColor& Fill, const FLinearColor& Ink, float Px, int32 Align)
{
	const float PH = Px + 14.f;
	const float PW = HudTxtW(Label, HudLabelFont(true), Px) + Px * 1.6f;
	const float LX = Align == 1 ? X - PW * 0.5f : (Align == 2 ? X - PW : X);
	HudRound(Fill, LX, Y, PW, PH, PH * 0.5f);
	HudTxt(Label, HudLabelFont(true), Px, LX + PW * 0.5f, Y + PH * 0.5f - 0.62f * Px, Ink, 1, false);
	return PW;
}

void ASQHUD::HudSoulIcon(float CX, float CY, float IconSize, const FLinearColor& Colour, float GlowAmount)
{
	// a little ghost-soul: round body, tapering wisp on top, two eyes
	if (GlowAmount > 0.01f)
	{
		HudDisc(SQUI::WithAlpha(Colour, 0.10f * GlowAmount), CX, CY, IconSize * 0.75f);
		HudDisc(SQUI::WithAlpha(Colour, 0.16f * GlowAmount), CX, CY, IconSize * 0.58f);
	}
	HudDisc(Colour, CX, CY + IconSize * 0.1f, IconSize * 0.36f);
	HudDisc(Colour, CX + IconSize * 0.04f, CY - IconSize * 0.2f, IconSize * 0.24f);
	HudDisc(Colour, CX + IconSize * 0.12f, CY - IconSize * 0.4f, IconSize * 0.12f);
	HudDisc(SQUI::Midnight, CX - IconSize * 0.12f, CY + IconSize * 0.08f, IconSize * 0.065f);
	HudDisc(SQUI::Midnight, CX + IconSize * 0.12f, CY + IconSize * 0.08f, IconSize * 0.065f);
}

void ASQHUD::HudGlyph(int32 GlyphKind, float CX, float CY, float GlyphSize, const FLinearColor& Colour)
{
	const float Sz = GlyphSize;
	switch (GlyphKind)
	{
	case 0: // weapon: crossed blades with hilts
	{
		const float BladeW = Sz * 0.15f, BladeH = Sz * 0.95f;
		HudTilt(Colour, CX - BladeW * 0.5f, CY - BladeH * 0.5f, BladeW, BladeH, 45.f);
		HudTilt(Colour, CX - BladeW * 0.5f, CY - BladeH * 0.5f, BladeW, BladeH, -45.f);
		const float HiltOff = Sz * 0.27f;
		HudTilt(SQUI::BoneWhite, CX - HiltOff - Sz * 0.15f, CY + HiltOff - Sz * 0.04f, Sz * 0.3f, Sz * 0.08f, 45.f);
		HudTilt(SQUI::BoneWhite, CX + HiltOff - Sz * 0.15f, CY + HiltOff - Sz * 0.04f, Sz * 0.3f, Sz * 0.08f, -45.f);
		HudDisc(SQUI::Midnight, CX, CY, Sz * 0.09f);
		break;
	}
	case 1: // tome / item: a cut gem
	{
		const float Gem = Sz * 0.66f;
		HudTilt(Colour, CX - Gem * 0.5f, CY - Gem * 0.5f, Gem, Gem, 45.f);
		HudTilt(SQUI::WithAlpha(SQUI::Midnight, 0.55f), CX - Gem * 0.3f, CY - Gem * 0.3f, Gem * 0.6f, Gem * 0.6f, 45.f);
		HudTilt(Colour, CX - Gem * 0.18f, CY - Gem * 0.18f, Gem * 0.36f, Gem * 0.36f, 45.f);
		HudDisc(FLinearColor(1.f, 1.f, 1.f, 0.7f), CX - Gem * 0.14f, CY - Gem * 0.2f, Sz * 0.05f);
		break;
	}
	case 2: // evolution: eight-point star
	{
		const float Star = Sz * 0.62f;
		HudTilt(Colour, CX - Star * 0.5f, CY - Star * 0.5f, Star, Star, 0.f);
		HudTilt(Colour, CX - Star * 0.5f, CY - Star * 0.5f, Star, Star, 45.f);
		HudDisc(SQUI::Midnight, CX, CY, Star * 0.3f);
		HudDisc(SQUI::BoneWhite, CX, CY, Star * 0.16f);
		break;
	}
	default: // mend: a plus
	{
		HudRound(Colour, CX - Sz * 0.12f, CY - Sz * 0.42f, Sz * 0.24f, Sz * 0.84f, Sz * 0.06f);
		HudRound(Colour, CX - Sz * 0.42f, CY - Sz * 0.12f, Sz * 0.84f, Sz * 0.24f, Sz * 0.06f);
		break;
	}
	}
}

void ASQHUD::HudPadlock(float CX, float CY, float LockSize, const FLinearColor& Colour)
{
	const float Sz = LockSize;
	HudRound(Colour, CX - Sz * 0.28f, CY - Sz * 0.48f, Sz * 0.56f, Sz * 0.14f, Sz * 0.07f);
	HudRect(Colour, CX - Sz * 0.28f, CY - Sz * 0.42f, Sz * 0.11f, Sz * 0.4f);
	HudRect(Colour, CX + Sz * 0.17f, CY - Sz * 0.42f, Sz * 0.11f, Sz * 0.4f);
	HudRound(Colour, CX - Sz * 0.4f, CY - Sz * 0.06f, Sz * 0.8f, Sz * 0.56f, Sz * 0.1f);
	HudRound(SQUI::Midnight, CX - Sz * 0.05f, CY + Sz * 0.08f, Sz * 0.1f, Sz * 0.2f, Sz * 0.05f);
}

void ASQHUD::HudChevron(float X, float Y, float ChevH, const FLinearColor& Colour)
{
	const float Len = ChevH * 0.62f, Thick = ChevH * 0.2f;
	HudTilt(Colour, X + ChevH * 0.25f - Len * 0.5f, Y + ChevH * 0.3f - Thick * 0.5f, Len, Thick, 45.f);
	HudTilt(Colour, X + ChevH * 0.25f - Len * 0.5f, Y + ChevH * 0.7f - Thick * 0.5f, Len, Thick, -45.f);
}

FLinearColor ASQHUD::HudKindColour(const FSQUpgradeDef* Def) const
{
	if (!Def)
	{
		return SQUI::BoneWhite; // mend
	}
	switch (Def->Kind)
	{
	case ESQUpgradeKind::Relic: return SQUI::Soul;
	case ESQUpgradeKind::Evolution: return SQUI::Amber;
	default: return SQUI::Danger;
	}
}

FString ASQHUD::HudInitials(const FString& Name) const
{
	TArray<FString> Words;
	Name.ParseIntoArray(Words, TEXT(" "), true);
	FString Out;
	if (Words.Num() >= 2)
	{
		Out = Words[0].Left(1) + Words[1].Left(1);
	}
	else if (Words.Num() == 1)
	{
		Out = Words[0].Left(2);
	}
	return Out.ToUpper();
}

FString ASQHUD::ClockText(float Progress) const
{
	const int32 Minutes = FMath::FloorToInt(Progress * 180.f); // 9pm -> midnight
	const int32 Hour = 9 + Minutes / 60;
	const int32 Min = Minutes % 60;
	return Hour >= 12 ? FString(TEXT("12:00 AM")) : FString::Printf(TEXT("%d:%02d PM"), Hour, Min);
}

// ============================================================ Audio director

void ASQHUD::BeginPlay()
{
	Super::BeginPlay();
	const USQData* D = USQData::Get(this);
	// SETTINGS > AUDIO: music from the profile (the menu sets the same values); master on the audio device
	const USQSaveGame* AudioSave = USQSaveGame::Load();
	MusicVolumeSeen = AudioSave ? FMath::Clamp(AudioSave->MusicVolume, 0.f, 1.f) : (D ? D->Tune(TEXT("MusicVolume"), 0.5f) : 0.5f);
	SQSettings::ApplyMasterVolume(GetWorld(), AudioSave);
	if (USoundBase* Track = SQ::Sound(TEXT("Music")))
	{
		Music = UGameplayStatics::SpawnSound2D(this, Track, FMath::Max(0.001f, MusicVolumeSeen)); // never 0: a silent sound can be stopped for good
	}
}

void ASQHUD::Play(const TCHAR* Name, float Volume, float PitchJitter)
{
	const USQData* D = USQData::Get(this);
	if (USoundBase* S = SQ::Sound(Name))
	{
		UGameplayStatics::PlaySound2D(this, S, Volume * (D ? D->Tune(TEXT("MasterVolume"), 0.8f) : 0.8f), 1.f + FMath::FRandRange(-PitchJitter, PitchJitter));
	}
}

void ASQHUD::UpdateAudio()
{
	const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	APlayerController* PC = GetOwningPlayerController();
	const ASQCharacter* C = PC ? Cast<ASQCharacter>(PC->GetPawn()) : nullptr;
	if (!GS) return;
	const float Now = GetWorld()->GetTimeSeconds();

	const uint8 Phase = (uint8)GS->Phase;
	if (Phase != PrevPhase && PrevPhase != 255)
	{
		switch (GS->Phase)
		{
		case ESQPhase::Night: Play(TEXT("NightStart"), 0.8f, 0.f); break;
		case ESQPhase::Boss: Play(TEXT("Bell"), 1.f, 0.f); break;
		case ESQPhase::Victory: Play(TEXT("Victory"), 1.f, 0.f); break;
		case ESQPhase::Defeat: Play(TEXT("Defeat"), 1.f, 0.f); break;
		default: break;
		}
	}
	PrevPhase = Phase;

	if (GS->TeamLevel > PrevLevel) Play(TEXT("LevelUp"), 0.7f, 0.f);
	PrevLevel = GS->TeamLevel;

	if (GS->Banked > PrevBanked) Play(TEXT("Bank"), 0.9f, 0.03f);
	PrevBanked = GS->Banked;

	if (GS->Horde)
	{
		const int32 Deaths = GS->Horde->LocalDeathCount;
		if (Deaths > PrevDeaths && Now - LastKillSound > 0.07f)
		{
			Play(TEXT("Squelch"), FMath::Clamp(0.25f + 0.05f * (Deaths - PrevDeaths), 0.25f, 0.7f), 0.2f);
			LastKillSound = Now;
		}
		PrevDeaths = Deaths;
	}

	if (C)
	{
		if (C->Carried > PrevCarried && Now - LastSoulSound > 0.06f)
		{
			Play(TEXT("Soul"), 0.35f, 0.15f);
			LastSoulSound = Now;
		}
		PrevCarried = C->Carried;
		if (C->HitFlash > PrevHitFlash + 0.1f) Play(TEXT("Hit"), 0.8f, 0.1f);
		PrevHitFlash = C->HitFlash;
		if (C->DashCooldownLeft > PrevDash + 0.2f) Play(TEXT("Dash"), 0.6f, 0.1f);
		PrevDash = C->DashCooldownLeft;
	}
}

// ============================================================ HUD frame

void ASQHUD::DrawHUD()
{
	Super::DrawHUD();
	UpdateAudio();
	UWorld* World = GetWorld();
	if (!Canvas || !World)
	{
		return;
	}
	HudLoadFonts();

	// Authored for 1080p; scale by height, but never narrower than 1440 design units (4:3), so nothing overlaps.
	UIScale = FMath::Clamp(FMath::Min(Canvas->ClipY / 1080.f, Canvas->ClipX / 1440.f), 0.4f, 4.f);
	HudVW = Canvas->ClipX / UIScale;
	HudVH = Canvas->ClipY / UIScale;
	HudNow = static_cast<float>(World->GetRealTimeSeconds());
	// real (undilated) frame time: the level-up pause sets the global time dilation to ~0
	HudDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, 0.1f);
	HudAlpha = 1.f;
	ClickZones.Reset();
	PortraitAnchorX = PortraitAnchorY = ObjectiveAnchorX = ObjectiveAnchorY = StripAnchorX = StripAnchorY = -1.f;

	const ASQGameState* GS = World->GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	APlayerController* PC = GetOwningPlayerController();
	ASQPlayerController* SQPC = Cast<ASQPlayerController>(PC);
	const ASQPlayerState* PS = PC ? PC->GetPlayerState<ASQPlayerState>() : nullptr;
	const ASQCharacter* C = PC ? Cast<ASQCharacter>(PC->GetPawn()) : nullptr;
	if (!GS || !D)
	{
		return;
	}
	// Quest-locked items: tell the server once (and again after a quest unlocks one) which cards this profile can't get yet
	if (ASQPlayerState* LockPS = PC ? PC->GetPlayerState<ASQPlayerState>() : nullptr)
	{
		if (!LockPS->bLocksSent && LockPS->GetOwner() == PC) LockPS->SendLockedItems(); // owner known: the RPC can reach the server
	}

	const float Margin = 24.f;
	const bool bLobby = GS->Phase == ESQPhase::Waiting;
	if (!bLobby)
	{
		LobbyStudioUpdate(false, NAME_None, SQUI::Soul, D); // removes the lobby studios once the run starts
	}
	const bool bShop = GS->Phase == ESQPhase::Shop;
	const bool bEnd = GS->Phase == ESQPhase::Victory || GS->Phase == ESQPhase::Defeat;

	// ---- SETTINGS (P) is up: the panels under it take no clicks or keys (drawn on top at the end)
	const bool bSettingsOpen = SQPC && SQPC->IsSettingsOpen();
	if (const USQSaveGame* MusicSave = USQSaveGame::Load())
	{
		const float MusicNow = FMath::Clamp(MusicSave->MusicVolume, 0.f, 1.f);
		if (Music && !FMath::IsNearlyEqual(MusicNow, MusicVolumeSeen, 0.001f)) Music->SetVolumeMultiplier(FMath::Max(0.001f, MusicNow));
		MusicVolumeSeen = MusicNow;
	}

	// ---- phase changes: banners and entrance animations
	const uint8 PhaseNow = static_cast<uint8>(GS->Phase);
	if (PhaseNow != HudPhaseSeen)
	{
		const bool bFromLobby = HudPhaseSeen == static_cast<uint8>(ESQPhase::Waiting);
		switch (GS->Phase)
		{
		case ESQPhase::Night:
			PhaseBannerKind = 1;
			PhaseBannerAt = HudNow;
			if (bFromLobby)
			{
				ReapAt = HudNow;               // "REAP!" first, then the night title
				PhaseBannerAt = HudNow + 0.8f;
			}
			break;
		case ESQPhase::Boss:
			PhaseBannerKind = 2;
			PhaseBannerAt = HudNow;
			break;
		case ESQPhase::Shop:
			ShopShownAt = HudNow;
			break;
		case ESQPhase::Waiting:
			LobbyShownAt = HudNow;
			bLobbyExpanded = true;
			LobbyFocus = -1;     // the reaper grid starts on your reaper
			LobbyHireArmed = -1;
			break;
		default:
			EndShownAt = HudNow;
			bEndChoiceSent = false;
			EndBtnLift[0] = EndBtnLift[1] = 0.f;
			break;
		}
		HudPhaseSeen = PhaseNow;
	}

	// ---- TAB folds the lobby panel away
	if (bLobby && !bSettingsOpen && PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::Tab))
	{
		bLobbyExpanded = !bLobbyExpanded;
		Play(TEXT("Soul"), 0.2f, 0.f);
	}
	LobbyOpen = FMath::FInterpTo(LobbyOpen, bLobbyExpanded ? 1.f : 0.f, HudDelta, 12.f);

	// ---- smoothed numbers + damage ghosts
	if (C)
	{
		const float MaxHP = FMath::Max(1.f, C->Stats.MaxHealth);
		if (HPShown < 0.f)
		{
			HPShown = HPGhost = HPLast = C->Health;
		}
		if (C->Health < HPLast - 0.01f)
		{
			HPGhostHold = HudNow + 0.45f;
		}
		HPLast = C->Health;
		HPShown = FMath::FInterpTo(HPShown, C->Health, HudDelta, 14.f);
		if (C->Health >= HPGhost)
		{
			HPGhost = C->Health;
		}
		else if (HudNow > HPGhostHold)
		{
			HPGhost = FMath::FInterpConstantTo(HPGhost, C->Health, HudDelta, MaxHP * 0.8f);
		}
		if (C->Carried > SoulsSeen)
		{
			SoulsPulseAt = HudNow;
		}
		SoulsSeen = C->Carried;
		SoulsShown = FMath::FInterpTo(SoulsShown, static_cast<float>(C->Carried), HudDelta, 16.f);
	}
	else
	{
		HPShown = -1.f;
	}

	if (GS->TeamLevel != XPLevelSeen)
	{
		XPShown = 0.f;
		XPLevelSeen = GS->TeamLevel;
	}
	XPShown = FMath::FInterpTo(XPShown, static_cast<float>(GS->TeamXP), HudDelta, 8.f);

	if (const ASQBoss* Boss = GS->Boss)
	{
		if (BossShown < 0.f)
		{
			BossShown = BossGhost = BossLast = Boss->Health;
		}
		if (Boss->Health < BossLast - 0.01f)
		{
			BossGhostHold = HudNow + 0.5f;
		}
		BossLast = Boss->Health;
		BossShown = FMath::FInterpTo(BossShown, Boss->Health, HudDelta, 12.f);
		if (Boss->Health >= BossGhost)
		{
			BossGhost = Boss->Health;
		}
		else if (HudNow > BossGhostHold)
		{
			BossGhost = FMath::FInterpConstantTo(BossGhost, Boss->Health, HudDelta, FMath::Max(1.f, Boss->MaxHealth) * 0.35f);
		}
	}
	else
	{
		BossShown = -1.f;
	}

	// ---- "+N" floaters when souls are banked
	if (FloaterBanked >= 0 && GS->Banked < FloaterBanked)
	{
		QuotaShown = static_cast<float>(GS->Banked); // a new night reset the count: snap, don't drain
	}
	if (FloaterBanked >= 0 && GS->Banked > FloaterBanked && GS->Phase == ESQPhase::Night)
	{
		FSQHudFloater& NewFloater = Floaters.AddDefaulted_GetRef();
		NewFloater.Label = FString::Printf(TEXT("+%d"), GS->Banked - FloaterBanked);
		NewFloater.Colour = SQUI::Amber;
		NewFloater.Born = HudNow;
		NewFloater.Jitter = FMath::FRandRange(-18.f, 18.f);
	}
	FloaterBanked = GS->Banked;
	QuotaShown = FMath::FInterpTo(QuotaShown, static_cast<float>(GS->Banked), HudDelta, 6.f);
	Floaters.RemoveAll([this](const FSQHudFloater& Each) { return HudNow - Each.Born > 1.4f; });

	// ---- floating damage numbers (SETTINGS > GAMEPLAY, or N; saved in the profile), drawn under every panel
	if (const USQSaveGame* NumbersSave = USQSaveGame::Load()) bShowDamageNumbers = NumbersSave->bDamageNumbers;
	if (!bLobby && !bEnd && !bSettingsOpen && PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::N))
	{
		bShowDamageNumbers = !bShowDamageNumbers;
		DamageNumbersToggledAt = HudNow;
		if (USQSaveGame* NumbersSave = USQSaveGame::Load())
		{
			NumbersSave->bDamageNumbers = bShowDamageNumbers;
			USQSaveGame::Store(NumbersSave);
		}
		Play(TEXT("Soul"), 0.2f, 0.f);
	}
	if (!bLobby && !bEnd)
	{
		DrawDamageNumbers();
	}
	if (!bLobby && !bEnd && !bShop)
	{
		DrawWorldMarkers(GS, PS, C, D);
	}
	// ---- the reap reticle at the cursor (under every panel)
	if (!bLobby && !bEnd && !bShop && !bSettingsOpen && C && SQPC && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss))
	{
		DrawReapReticle(GS, C, SQPC);
	}

	// ---- the HUD proper
	if (!bEnd)
	{
		DrawScreenEdges(C);
		// Nov 9 layout: ability bar bottom-centre, gold + teammates top-left, objectives top-right, timer top-centre
		BottomReserve = 0.f;
		TeamPanelBottom = 0.f;
		float StackY = 24.f + 12.f;
		if (!bLobby)
		{
			if (C)
			{
				BottomReserve = DrawAbilityBar(GS, PS, C, D);
			}
			float LeftY = Margin;
			if (PS)
			{
				DrawGoldPanel(GS, PS, C, Margin, LeftY);
				LeftY += 46.f + 12.f;
			}
			DrawTeamCards(GS, PS, D, Margin, LeftY);
			ObjectivesBottom = DrawObjectives(GS, D);
			TeamPanelBottom = ObjectivesBottom;
			if (!bShop)
			{
				DrawObjectiveArrows(GS, C);
			}
			// Map revamp: corner minimap / full map (M) and the district banner
			DrawMinimap(GS, C, D, bShop);
			DrawDistrictBanner(GS, C);
			StackY = DrawTopCentre(GS, D) + 12.f;
			if (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss)
			{
				StackY += DrawMinibossBar(GS, StackY);
			}
		}

		// toasts stack under the top-centre widget
		if (!GS->AlertText.IsEmpty())
		{
			const float AlertAge = static_cast<float>(GS->GetServerWorldTimeSeconds()) - GS->AlertServerTime;
			if (AlertAge > -1.f && AlertAge < 5.f)
			{
				StackY += DrawToast(GS->AlertText, StackY, SQUI::Amber, FMath::Max(0.f, AlertAge), 5.f);
			}
		}
		if (SQPC && !SQPC->LocalMessage.IsEmpty())
		{
			const float MsgAge = HudNow - SQPC->LocalMessageTime; // both world real time
			if (MsgAge >= 0.f && MsgAge < 6.f)
			{
				StackY += DrawToast(SQPC->LocalMessage, StackY, SQUI::Soul, MsgAge, 6.f);
			}
		}
		const FString Gimmick = GS->Arena ? GS->Arena->GimmickStatus() : FString();
		if (Gimmick != GimmickSeen)
		{
			GimmickSeen = Gimmick;
			GimmickSeenAt = HudNow;
		}
		if (!Gimmick.IsEmpty())
		{
			StackY += DrawToast(Gimmick, StackY, SQUI::Soul, HudNow - GimmickSeenAt, -1.f);
		}
		if (C && C->bDowned)
		{
			ASQCharacter* DownMe = Cast<ASQCharacter>(PC->GetPawn());
			const int32 ReviveLeft = DownMe ? FMath::Max(0, FMath::CeilToInt(static_cast<float>(DownMe->GetHarvestReviveNeed()) - static_cast<float>(DownMe->GetHarvestReviveProgress()))) : 0;
			const FString DownText = (SQPC && SQPC->IsSoloPick()) ? FString(TEXT("You're down!"))
				: (ReviveLeft > 0 ? FString::Printf(TEXT("You're down! Teammates bring you back by reaping %d near you."), ReviveLeft)
					: FString(TEXT("You're down! Teammates bring you back by reaping near you.")));
			StackY += DrawToast(DownText, StackY, SQUI::Danger, 1.f, -1.f);
		}
		if (C && C->CarriedSkull)
		{
			StackY += DrawToast(TEXT("Carrying a skull: get to the lantern to revive them (F to drop)"), StackY, SQUI::Soul, 1.f, -1.f);
		}

		DrawFloaters();

		// ---- level-up picking: the server freezes the world while anyone chooses
		const bool bPicking = SQPC ? SQPC->IsPicking() : (PS && PS->IsChoosing() && !bShop && !bLobby);
		// Solo: the big frozen card screen. Co-op: the small strip at the bottom (the game keeps running)
		// an AUGMENT set rides the level-up card path (ASQPlayerState::bAugmentOffer): it gets its own gold cards, solo and co-op
		ASQPlayerState* AugPS = PC ? PC->GetPlayerState<ASQPlayerState>() : nullptr;
		const bool bAugments = AugPS && !bLobby && !bShop && AugPS->bAugmentOffer && AugPS->AugmentOffers.Num() > 0;
		if (bAugments) AddClickZone(0.f, 0.f, HudVW, HudVH); // the augment cards own the mouse: no reaping under them
		const bool bBigCards = bPicking && (!SQPC || SQPC->IsSoloPick()) && !bAugments;
		const bool bWaitingForTeam = !bPicking && GS->bPickPause && !bShop && !bLobby;
		if (!bBigCards)
		{
			DrawPhaseBanner(GS);
			DrawBeatBanner(GS);
			if (C)
			{
				DrawBanking(C, false);
			}
		}
		const uint8 WaitModeNow = static_cast<uint8>(bWaitingForTeam ? 2 : ((bPicking && GS->bPickPause) ? 1 : 0));
		if (WaitModeNow != PickWaitMode)
		{
			PickWaitMode = WaitModeNow;
			WaitShownAt = HudNow;
		}
		PickVeil = FMath::FInterpTo(PickVeil, (bBigCards || bAugments) ? 1.f : (bWaitingForTeam ? 0.75f : 0.f), HudDelta, 10.f);
		if (PickVeil > 0.01f)
		{
			HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.5f * PickVeil), 0.f, 0.f, HudVW, HudVH);
		}
		if (bBigCards && PS && !bSettingsOpen)
		{
			DrawLevelUp(GS, PS, C, D, SQPC);
		}
		else
		{
			OfferSignature = 0;
		}
		if (bPicking && !bBigCards && !bAugments && PS && !bSettingsOpen)
		{
			DrawLevelUpStrip(GS, PS, C, D, SQPC);
		}
		else
		{
			StripSignature = 0;
			bStripWasOpen = false;
		}
		if (bAugments && !bSettingsOpen)
		{
			DrawAugmentOffer(GS, AugPS, D);
		}
		else
		{
			AugmentSignature = 0;
		}
		if (bWaitingForTeam)
		{
			DrawPickStatus(GS, PS, false);
		}
		else if (bPicking && GS->bPickPause && GS->PickersTotal > 1)
		{
			DrawPickStatus(GS, PS, true);
		}
		if (bPicking || bWaitingForTeam)
		{
			DrawPickCountdown(GS);
		}
		if (bShop)
		{
			DrawShop(GS, PS, D);
		}
		if (bLobby && !bSettingsOpen) // the settings overlay owns the screen and the clicks while it is up
		{
			if (GS->bSkipLobby)
			{
				DrawSkipLobby(GS); // solo PLAY / RETRY: no lobby, the run starts in a moment
			}
			else
			{
				DrawLobby(GS, PS, C, D, SQPC); // contract clauses open from its mission panel
				DrawLobbyHints();
				DrawStartCountdown(GS);
			}
		}
	}
	else
	{
		DrawEndScreen(GS, PS);
	}

	// ---- quest / chest / notice cards (this PC only)
	DrawLocalToasts(PS);

	// ---- tutorial: the Training Shift card and one-time tips (run), on top of everything but settings and the data errors
	if (!bEnd && !bLobby && !bSettingsOpen)
	{
		DrawTutorialLayer(bLobby);
	}

	// ---- SETTINGS (P)
	if (SQPC)
	{
		DrawSettingsOverlay(SQPC);
	}

	// ---- data errors (always on top)
	if (!D->LoadErrors.IsEmpty() || !SQQuests::LoadErrors().IsEmpty())
	{
		const FString Err = FString::Printf(TEXT("DATA ERROR: %s%s"), *D->LoadErrors, *SQQuests::LoadErrors());
		const float ErrW = FMath::Min(760.f, HudVW * 0.45f);
		const int32 ErrLines = HudWrap(Err, HudLabelFont(true), 15.f, 0.f, 0.f, ErrW - 36.f, SQUI::Danger, 20.f, 6, false);
		const float ErrY = HudVH * 0.42f;
		HudPanel(Margin, ErrY, ErrW, ErrLines * 20.f + 28.f, 14.f, SQUI::Danger);
		HudWrap(Err, HudLabelFont(true), 15.f, Margin + 18.f, ErrY + 12.f, ErrW - 36.f, SQUI::Danger, 20.f, 6);
	}

	// ---- debug stats + controls (small, bottom-right)
	if (SQPC && SQPC->bShowStats)
	{
		const float Dt = static_cast<float>(FApp::GetDeltaTime());
		const float FPS = Dt > 0.f ? 1.f / Dt : 0.f;
		const int32 Pickups = GS->Horde ? GS->Horde->NumPickups() : 0;
		const int32 NetEnemies = GS->Horde ? GS->Horde->EnemyNet.Num() : 0;
		const float StatsY = bLobby ? HudVH - 48.f - 20.f : HudVH - 44.f;
		HudTxt(FString::Printf(TEXT("FPS %.0f  -  enemies %d (net %d)  -  pickups %d  -  players %d  -  kills %d"),
			FPS, GS->EnemyCount, NetEnemies, Pickups, GS->PlayerArray.Num(), PS ? PS->Kills : 0), HudLabelFont(false), 12.f, HudVW - Margin, StatsY, SQUI::Disabled, 2);
		const USQSaveGame* HintSave = USQSaveGame::Load();
		if (!bLobby && !bEnd && (!HintSave || HintSave->bShowKeyHints))
		{
			HudTxt(TEXT("WASD move  -  Mouse look  -  Space jump  -  Shift / R-click dash  -  L-click reap  -  E ability  -  R ultimate  -  F interact  -  M map  -  Alt cursor  -  P settings"),
				HudLabelFont(false), 12.f, HudVW - Margin, HudVH - 26.f, SQUI::Disabled, 2);
		}
	}
	HudAlpha = 1.f;
}

// ============================================================ toasts, floaters, banners

float ASQHUD::DrawToast(const FString& Msg, float Y, const FLinearColor& Accent, float Age, float Life)
{
	const float In = HudEaseOut(Age / 0.25f);
	const float Out = Life > 0.f ? FMath::Clamp((Life - Age) / 0.5f, 0.f, 1.f) : 1.f;
	const float Vis = FMath::Min(In, Out);
	if (Vis <= 0.01f || Msg.IsEmpty())
	{
		return 0.f;
	}
	const float MaxW = FMath::Clamp(HudVW - 2.f * (24.f + 400.f + 24.f), 320.f, 760.f);
	const float Px = 17.f, Step = 21.f;
	const float TextMax = MaxW - 64.f;
	const int32 Lines = FMath::Max(1, HudWrap(Msg, HudLabelFont(true), Px, 0.f, 0.f, TextMax, SQUI::BoneWhite, Step, 2, false));
	const float TW = FMath::Min(HudTxtW(Msg, HudLabelFont(true), Px), TextMax);
	const float TW2 = TW + 60.f, TH = Lines * Step + 20.f;
	const float TX = (HudVW - TW2) * 0.5f, TY = Y - 10.f * (1.f - In);
	HudAlpha = Vis;
	HudRound(FLinearColor(0.f, 0.f, 0.f, 0.35f), TX + 4.f, TY + 6.f, TW2, TH, 14.f);
	HudRound(Accent, TX - 2.f, TY - 2.f, TW2 + 4.f, TH + 4.f, 16.f);
	HudRound(SQUI::WithAlpha(SQUI::Panel, 0.97f), TX, TY, TW2, TH, 14.f);
	HudDisc(SQUI::WithAlpha(Accent, 0.25f * HudPulse(HudNow, 5.f)), TX + 22.f, TY + 10.f + Step * 0.5f, 9.f);
	HudDisc(Accent, TX + 22.f, TY + 10.f + Step * 0.5f, 5.f);
	HudWrap(Msg, HudLabelFont(true), Px, TX + 40.f, TY + 10.f, TextMax, SQUI::BoneWhite, Step, 2);
	HudAlpha = 1.f;
	return (TH + 8.f) * Vis;
}

void ASQHUD::DrawDamageNumbers()
{
	// Toggle feedback
	const float ToggleAge = HudNow - DamageNumbersToggledAt;
	if (ToggleAge < 1.5f)
	{
		HudAlpha = FMath::Clamp((1.5f - ToggleAge) / 0.4f, 0.f, 1.f);
		HudTxt(bShowDamageNumbers ? FString(TEXT("DAMAGE NUMBERS ON  (N)")) : FString(TEXT("DAMAGE NUMBERS OFF  (N)")), HudLabelFont(true), 16.f,
			HudVW * 0.5f, HudVH - 150.f, SQUI::BoneWhite, 1, true);
		HudAlpha = 1.f;
	}
	if (!bShowDamageNumbers || !Canvas) return;
	ASQSparks* Sparks = ASQSparks::Get(GetWorld());
	if (!Sparks) return;
	TArray<FSQDamageNumberView> Numbers;
	Sparks->GetDamageNumbers(Numbers);
	UFont* NumberFont = HudDisplayFont();
	const FLinearColor TierColours[4] = {
		FLinearColor(1.f, 1.f, 1.f),        // normal: white
		FLinearColor(1.f, 0.85f, 0.18f),    // crit: yellow
		FLinearColor(1.f, 0.36f, 0.12f),    // huge / overcrit: red-orange
		FLinearColor(0.42f, 1.f, 0.48f) };  // heal: green
	const float TierPx[4] = { 22.f, 30.f, 40.f, 24.f };
	for (const FSQDamageNumberView& Num : Numbers)
	{
		const FVector Screen = Canvas->Project(Num.Pos);
		if (Screen.Z <= 0.f) continue; // behind the camera
		if (Screen.X < -80.f || Screen.Y < -80.f || Screen.X > Canvas->ClipX + 80.f || Screen.Y > Canvas->ClipY + 80.f) continue;
		const int32 Tier = FMath::Clamp(static_cast<int32>(Num.Tier), 0, 3);
		const float Alpha = Num.Life > 0.f ? Num.Age / Num.Life : 1.f;
		// Pop in big, settle, punch again when a hit merges in; fade over the last third
		const float PopIn = 1.f + 0.6f * FMath::Clamp(1.f - Num.Age / 0.12f, 0.f, 1.f);
		const float Px = TierPx[Tier] * PopIn * (1.f + 0.3f * Num.Punch);
		HudAlpha = FMath::Clamp((1.f - Alpha) / 0.33f, 0.f, 1.f);
		const int32 Shown = FMath::Max(1, FMath::RoundToInt(Num.Amount));
		FString Label = FString::FromInt(Shown);
		if (Tier == 3) Label = TEXT("+") + Label;
		else if (Tier == 1) Label += TEXT("!");
		else if (Tier == 2) Label += TEXT("!!");
		HudTxt(Label, NumberFont, Px, static_cast<float>(Screen.X) / UIScale, static_cast<float>(Screen.Y) / UIScale - Px * 0.6f, TierColours[Tier], 1, true);
	}
	HudAlpha = 1.f;
}

void ASQHUD::DrawFloaters()
{
	for (const FSQHudFloater& Each : Floaters)
	{
		const float Age = HudNow - Each.Born;
		const float Rise = HudEaseOut(Age / 1.2f);
		HudAlpha = FMath::Clamp(Age / 0.1f, 0.f, 1.f) * FMath::Clamp((1.4f - Age) / 0.5f, 0.f, 1.f);
		const float Pop = 1.f + 0.35f * (1.f - HudEaseOut(Age / 0.2f));
		HudGlowTxt(Each.Label, HudHeadFont(), 24.f * Pop, QuotaAnchorX + Each.Jitter, QuotaAnchorY - 22.f - 46.f * Rise, Each.Colour, Each.Colour, 1);
	}
	HudAlpha = 1.f;
}

void ASQHUD::DrawPhaseBanner(const ASQGameState* GS)
{
	// "REAP!" flash when the lobby turns into the first night
	const float ReapAge = HudNow - ReapAt;
	if (ReapAge >= 0.f && ReapAge < 0.8f)
	{
		const float Punch = HudBackOut(ReapAge / 0.25f);
		HudAlpha = FMath::Clamp((0.8f - ReapAge) / 0.3f, 0.f, 1.f);
		const float ReapPx = 170.f * (1.4f - 0.4f * Punch);
		HudGlowTxt(TEXT("REAP!"), HudDisplayFont(), ReapPx, HudVW * 0.5f, HudVH * 0.42f - 0.62f * ReapPx, SQUI::Soul, SQUI::Soul, 1);
		HudAlpha = 1.f;
	}

	if (PhaseBannerKind == 0)
	{
		return;
	}
	const float Age = HudNow - PhaseBannerAt;
	const float Life = 2.6f;
	if (Age < 0.f || Age > Life)
	{
		return;
	}
	const float In = HudBackOut(Age / 0.45f);
	HudAlpha = FMath::Clamp(Age / 0.15f, 0.f, 1.f) * FMath::Clamp((Life - Age) / 0.6f, 0.f, 1.f);
	FString Title, Sub;
	FLinearColor TitleColour = SQUI::Soul;
	if (PhaseBannerKind == 1)
	{
		Title = GS->MissionId.IsNone() ? FString(TEXT("MISSION START")) : HudPretty(GS->MissionId).ToUpper();
		Sub = FString(TEXT("COMPLETE THE OBJECTIVES, THEN RING THE APPOINTMENT BELL"));
	}
	else
	{
		Title = GS->BossNightProgress >= 0.999f ? FString(TEXT("MIDNIGHT")) : FString(TEXT("THE BELL TOLLS"));
		TitleColour = SQUI::Danger;
		const USQData* D = USQData::Get(this);
		const FSQBossDef* BossDef = (D && GS->Boss) ? D->FindBoss(GS->Boss->BossId) : nullptr;
		Sub = BossDef ? FString::Printf(TEXT("%s  \u00B7  %s"), *BossDef->Name.ToUpper(), *BossDef->Title.ToUpper()) : FString(TEXT("SOMETHING IS COMING"));
	}
	const float TitlePx = 92.f * (1.25f - 0.25f * In);
	const float BannerY = HudVH * 0.3f;
	HudGlowTxt(Title, HudDisplayFont(), TitlePx, HudVW * 0.5f, BannerY - 0.62f * TitlePx, TitleColour, TitleColour, 1);
	HudTxt(Sub, HudLabelFont(true), 18.f, HudVW * 0.5f, BannerY + 0.62f * 92.f + 4.f, SQUI::BoneWhite, 1);
	HudAlpha = 1.f;
}

void ASQHUD::DrawStartCountdown(const ASQGameState* GS)
{
	if (GS->PhaseTimeLeft < 0.f || GS->PhaseTimeLeft > 9.5f)
	{
		return;
	}
	const int32 Count = FMath::Max(1, FMath::CeilToInt(GS->PhaseTimeLeft));
	const float IntoSecond = FMath::Clamp(static_cast<float>(Count) - GS->PhaseTimeLeft, 0.f, 1.f); // 0 when the number appears
	const float Punch = HudBackOut(IntoSecond / 0.3f);
	HudAlpha = FMath::Clamp(IntoSecond / 0.08f, 0.f, 1.f) * FMath::Clamp((1.f - IntoSecond) / 0.35f, 0.f, 1.f);
	const float NumPx = 200.f * (1.5f - 0.5f * Punch);
	const float CY = HudVH * 0.42f;
	const FString Num = FString::FromInt(Count);
	HudDisc(SQUI::WithAlpha(SQUI::Soul, 0.06f), HudVW * 0.5f, CY, 150.f * (1.2f - 0.2f * Punch));
	HudGlowTxt(Num, HudDisplayFont(), NumPx, HudVW * 0.5f, CY - 0.62f * NumPx, SQUI::Soul, SQUI::Soul, 1);
	HudAlpha = 1.f;
	HudTxt(TEXT("GET READY TO REAP"), HudLabelFont(true), 18.f, HudVW * 0.5f, CY + 0.62f * 200.f + 6.f, SQUI::BoneWhite, 1);
}

void ASQHUD::DrawBanking(const ASQCharacter* C, bool bCardsUp)
{
	if (C->BankProgress <= 0.f)
	{
		return;
	}
	const float BW = 340.f, BH = 60.f;
	const float BX = (HudVW - BW) * 0.5f;
	const float BY = bCardsUp ? FMath::Min(HudVH * 0.6f, HudVH - 24.f - 40.f - 260.f - 60.f - BH - 12.f) : HudVH * 0.62f;
	HudGlow(SQUI::Amber, BX, BY, BW, BH, 18.f, 0.35f);
	HudPanel(BX, BY, BW, BH, 18.f, SQUI::WithAlpha(SQUI::Amber, 0.8f));
	HudSoulIcon(BX + 30.f, BY + BH * 0.5f, 26.f, SQUI::Amber, 1.f);
	HudTxt(TEXT("BANKING SOULS"), HudHeadFont(), 17.f, BX + 56.f, BY + 7.f, SQUI::Amber);
	HudTxt(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(C->BankProgress * 100.f)), HudLabelFont(true), 13.f, BX + BW - 18.f, BY + 10.f, SQUI::Muted, 2, false);
	HudBar(BX + 56.f, BY + 34.f, BW - 74.f, 12.f, C->BankProgress, 0.f, SQUI::Amber);
	HudShine(BX + 56.f, BY + 34.f, BW - 74.f, 12.f, 1.2f, 0.f);
}

void ASQHUD::DrawScreenEdges(const ASQCharacter* C)
{
	if (!C)
	{
		return;
	}
	const float VW = HudVW, VH = HudVH;
	// low health: a slow red heartbeat round the edges
	const float HPFrac = C->Health / FMath::Max(1.f, C->Stats.MaxHealth);
	float EdgeA = 0.f;
	if (HPFrac < 0.3f && !C->bDowned)
	{
		EdgeA = (0.3f - HPFrac) / 0.3f * (0.25f + 0.2f * HudPulse(HudNow, 6.f));
	}
	EdgeA = FMath::Max(EdgeA, C->HitFlash * 0.6f);
	if (C->HitFlash > 0.f)
	{
		HudRect(FLinearColor(0.8f, 0.05f, 0.05f, C->HitFlash * 0.18f), 0.f, 0.f, VW, VH);
	}
	if (EdgeA <= 0.01f)
	{
		return;
	}
	constexpr int32 EdgeLayers = 6;
	for (int32 Layer = 0; Layer < EdgeLayers; ++Layer)
	{
		const float Edge = 16.f + Layer * 16.f;
		const FLinearColor EdgeCol(0.55f, 0.02f, 0.04f, EdgeA * 0.22f);
		HudRect(EdgeCol, 0.f, 0.f, VW, Edge);
		HudRect(EdgeCol, 0.f, VH - Edge, VW, Edge);
		HudRect(EdgeCol, 0.f, Edge, Edge, VH - 2.f * Edge);
		HudRect(EdgeCol, VW - Edge, Edge, Edge, VH - 2.f * Edge);
	}
}

// ============================================================ level-up picking (the world is frozen meanwhile)
// Every animation here runs on HudNow / HudDelta (real time): the pick pause sets the global time
// dilation to ~0, so world time barely moves while the cards are up.

float ASQHUD::HudPickButton(const FString& Label, const FString& KeyName, const FString& SubLabel, float X, float Y, float W, float H,
	const FLinearColor& Accent, bool bEnabled, float Lift)
{
	const FLinearColor Edge = bEnabled ? FMath::Lerp(FMath::Lerp(SQUI::Line, Accent, 0.6f), Accent, Lift) : SQUI::Line;
	const float BY = Y - 4.f * Lift;
	if (bEnabled)
	{
		HudGlow(Accent, X, BY, W, H, H * 0.5f, 0.15f + 0.6f * Lift);
	}
	HudRound(FLinearColor(0.f, 0.f, 0.f, 0.35f), X + 4.f, BY + 7.f, W, H, H * 0.5f);
	HudRound(Edge, X - 2.f, BY - 2.f, W + 4.f, H + 4.f, H * 0.5f + 2.f);
	HudRound(FMath::Lerp(SQUI::Panel, SQUI::PanelHover, Lift), X, BY, W, H, H * 0.5f);
	HudRound(FLinearColor(1.f, 1.f, 1.f, 0.03f), X + 4.f, BY + 3.f, W - 8.f, H * 0.42f, H * 0.4f);

	const FLinearColor Ink = bEnabled ? SQUI::BoneWhite : SQUI::Disabled;
	const float CapW = HudKeyCap(KeyName, X + 16.f, BY + (H - 28.f) * 0.5f, 28.f);
	const float TextX = X + 16.f + CapW + 12.f;
	HudTxt(Label, HudHeadFont(), 22.f, TextX, BY + H * 0.5f - 0.62f * 22.f - (SubLabel.IsEmpty() ? 0.f : 7.f), Ink);
	if (!SubLabel.IsEmpty())
	{
		HudTxt(SubLabel, HudLabelFont(true), 12.f, TextX, BY + H * 0.5f + 8.f, bEnabled ? Accent : SQUI::Disabled, 0, false);
	}
	return W;
}

void ASQHUD::HudSparkle(float CX, float CY, float SparkSize, const FLinearColor& Colour)
{
	if (SparkSize <= 0.5f)
	{
		return;
	}
	HudDisc(SQUI::WithAlpha(Colour, Colour.A * 0.25f), CX, CY, SparkSize * 0.9f);
	HudTilt(Colour, CX - SparkSize, CY - SparkSize * 0.12f, SparkSize * 2.f, SparkSize * 0.24f, 0.f);
	HudTilt(Colour, CX - SparkSize, CY - SparkSize * 0.12f, SparkSize * 2.f, SparkSize * 0.24f, 90.f);
	HudDisc(FLinearColor(1.f, 1.f, 1.f, Colour.A), CX, CY, SparkSize * 0.22f);
}

// ============================================================ co-op level-up strip (bottom centre, never freezes)
void ASQHUD::DrawLevelUpStrip(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D, ASQPlayerController* SQPC)
{
	const int32 NumOffers = FMath::Min(PS->Offers.Num(), 3);
	if (NumOffers <= 0 || !SQPC || !GS)
	{
		return;
	}
	// A new card set (pick, reroll, the next banked level) slides in again
	uint32 Sig = static_cast<uint32>(PS->OfferSerial) * 7919u + static_cast<uint32>(NumOffers);
	for (int32 i = 0; i < NumOffers; ++i)
	{
		Sig = Sig * 31u + GetTypeHash(PS->Offers[i]);
	}
	if (Sig != StripSignature)
	{
		StripSignature = Sig;
		StripShownAt = HudNow;
	}
	const bool bOpen = SQPC->IsPickStripExpanded();
	if (bOpen && !bStripWasOpen)
	{
		StripOpenAt = HudNow;
	}
	bStripWasOpen = bOpen;
	const float MidX = HudVW * 0.5f;
	const int32 Banked = FMath::Max(1, PS->PendingLevelUps);
	const float AutoSeconds = FMath::Max(1.f, D->Tune(TEXT("PickAutoSeconds"), 20.f));
	const float AutoLeft = FMath::Max(0.f, AutoSeconds - (HudNow - StripShownAt)); // local estimate of the server's idle timer

	if (!bOpen)
	{
		// Collapsed: a pulsing pill that never covers the fight
		const float PillPulse = HudPulse(HudNow, 6.f);
		const FString PillText = FString::Printf(TEXT("LEVEL UP (%d)  -  press 1/2/3 or TAB"), Banked);
		const float PillPx = 17.f;
		const float PillY = HudVH - FMath::Max(168.f, BottomReserve + 44.f);
		const float PillW = HudTxtW(PillText, HudLabelFont(true), PillPx) + PillPx * 1.6f;
		HudGlow(SQUI::Soul, MidX - PillW * 0.5f, PillY, PillW, PillPx + 14.f, (PillPx + 14.f) * 0.5f, 0.35f + 0.5f * PillPulse);
		HudPill(PillText, MidX, PillY, FMath::Lerp(SQUI::Soul, FLinearColor::White, 0.25f * PillPulse), SQUI::Midnight, PillPx, 1);
		return;
	}

	// ---- expanded: three small cards (icon, name, rarity colour, one-line gain), picked with 1/2/3
	static const FKey StripKeys[] = { EKeys::One, EKeys::Two, EKeys::Three };
	for (int32 KeyIdx = 0; KeyIdx < NumOffers; ++KeyIdx)
	{
		if (PlayerOwner && PlayerOwner->WasInputKeyJustPressed(StripKeys[KeyIdx]))
		{
			StripFlashIndex = KeyIdx;
			StripFlashAt = HudNow;
		}
	}
	float MouseDX = -1.f, MouseDY = -1.f;
	if (SQPC->IsPickCursorHeld())
	{
		float MouseDesignX = 0.f, MouseDesignY = 0.f;
		if (HudMouseDesign(MouseDesignX, MouseDesignY))
		{
			MouseDX = MouseDesignX;
			MouseDY = MouseDesignY;
		}
	}
	const bool bClick = SQPC->IsPickCursorHeld() && PlayerOwner && HudNow >= PickClickLockUntil && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);

	const float CardW = 236.f, CardH = 104.f, Gap = 14.f;
	const float TotalW = NumOffers * CardW + (NumOffers - 1) * Gap;
	const float X0 = MidX - TotalW * 0.5f;
	const float Slide = HudBackOut(FMath::Clamp((HudNow - FMath::Max(StripShownAt, StripOpenAt)) / 0.25f, 0.f, 1.f));
	const float CardY = HudVH - FMath::Max(150.f, BottomReserve + 40.f) - CardH + 40.f * (1.f - Slide);
	AddClickZone(X0 - 8.f, CardY - 48.f, TotalW + 16.f, CardH + 56.f); // the cards and their header: no reaping through them
	StripAnchorX = MidX;
	StripAnchorY = CardY;

	// header: LEVEL UP, +N more banked, the protection bubble, keys
	{
		const float HeadY = CardY - 34.f;
		float HX = X0;
		HX += HudTxt(TEXT("LEVEL UP"), HudHeadFont(), 20.f, HX, HeadY, SQUI::Soul) + 10.f;
		if (Banked > 1)
		{
			HX += HudPill(FString::Printf(TEXT("+%d"), Banked - 1), HX, HeadY - 2.f, SQUI::Amber, SQUI::Midnight, 12.f) + 8.f;
		}
		if (C && C->bPickBubble)
		{
			HudPill(TEXT("PROTECTED"), HX, HeadY - 2.f, FMath::Lerp(SQUI::Soul, FLinearColor::White, 0.3f * HudPulse(HudNow, 14.f)), SQUI::Midnight, 12.f);
		}
		const FString KeysText = FString::Printf(TEXT("R/Q reroll (%d)   X skip   click a card   auto in %ds"), PS->RerollsLeft, FMath::CeilToInt(AutoLeft));
		HudTxt(KeysText, HudLabelFont(false), 12.f, X0 + TotalW, HeadY + 4.f, SQUI::Muted, 2);
	}

	for (int32 i = 0; i < NumOffers; ++i)
	{
		// The host's Choose() clears Offers immediately (mid-loop): stop before reading a removed card
		if (!PS->Offers.IsValidIndex(i))
		{
			break;
		}
		const FName OfferId = PS->Offers[i];
		const FSQUpgradeDef* Def = D->FindUpgrade(OfferId);
		const uint8 Rarity = PS->OfferRarity.IsValidIndex(i) ? FMath::Min<uint8>(PS->OfferRarity[i], 3) : 0;
		const FLinearColor RarCol = SQ::RarityColor(Rarity);
		const int32 Current = (Def && C) ? C->GetUpgradeLevel(OfferId) : 0;
		const float CX = X0 + i * (CardW + Gap);
		const bool bHover = MouseDX >= CX && MouseDX <= CX + CardW && MouseDY >= CardY && MouseDY <= CardY + CardH;
		if (bHover && bClick)
		{
			StripFlashIndex = i;
			StripFlashAt = HudNow;
			PickClickLockUntil = HudNow + 0.3f;
			Play(TEXT("Soul"), 0.5f, 0.f);
			SQPC->Choose(i);
			break; // the offer list may be gone now; redraw next frame
		}
		const float Flash = StripFlashIndex == i ? FMath::Clamp(1.f - (HudNow - StripFlashAt) / 0.35f, 0.f, 1.f) : 0.f;
		const float Lift = (bHover ? 6.f : 0.f) + 8.f * Flash;
		const float CY = CardY - Lift;

		HudGlow(RarCol, CX, CY, CardW, CardH, 12.f, 0.15f + 0.15f * Rarity + Flash);
		HudRound(FLinearColor(0.f, 0.f, 0.f, 0.4f), CX + 4.f, CY + 6.f, CardW, CardH, 12.f);
		HudRound(RarCol, CX - 2.f, CY - 2.f, CardW + 4.f, CardH + 4.f, 14.f);
		HudRound(bHover ? SQUI::PanelHover : SQUI::Panel, CX, CY, CardW, CardH, 12.f);
		HudRound(FMath::Lerp(SQUI::Panel, RarCol, 0.38f), CX, CY, 58.f, CardH, 12.f);

		// key badge + icon in the rarity band on the left
		HudRound(SQUI::BoneWhite, CX + 8.f, CY + 8.f, 22.f, 22.f, 6.f);
		HudTxt(FString::FromInt(i + 1), HudHeadFont(), 15.f, CX + 19.f, CY + 19.f - 0.62f * 15.f, SQUI::Midnight, 1, false);
		const FLinearColor IconCol = Def ? FMath::Lerp(Def->Color, FLinearColor::White, 0.15f) : SQUI::Danger;
		const float IconCX = CX + 29.f, IconCY = CY + 64.f;
		HudDisc(SQUI::Midnight, IconCX, IconCY, 20.f);
		const FString Glyph = Def ? Def->IconGlyph : FString(TEXT("+"));
		if (!Glyph.IsEmpty())
		{
			const float GlyphPx = HudFitPx(Glyph, HudHeadFont(), 20.f, 32.f);
			HudTxt(Glyph, HudHeadFont(), GlyphPx, IconCX, IconCY - 0.62f * GlyphPx, IconCol, 1);
		}
		else
		{
			const int32 GlyphKind = !Def ? 3 : (Def->Kind == ESQUpgradeKind::Weapon ? 0 : (Def->Kind == ESQUpgradeKind::Relic ? 1 : 2));
			HudGlyph(GlyphKind, IconCX, IconCY, 20.f, IconCol);
		}

		// name, rarity + level, one-line gain
		const float TextX = CX + 68.f;
		const float TextW = CardW - 76.f;
		const FString CardName = Def ? Def->Name.ToUpper() : FString(TEXT("MEND"));
		const float NamePx = HudFitPx(CardName, HudHeadFont(), 17.f, TextW);
		HudTxt(CardName, HudHeadFont(), NamePx, TextX, CY + 10.f, SQUI::BoneWhite);
		// items/tomes pass: "TOME LV 2 > 3" / "NEW  TOMES 3/4" (slot use) / EVOLUTION / BONUS
		const FString SlotNote = (Def && C) ? C->SlotText(*Def) : FString();
		const FString LevelText = !Def ? FString(TEXT("HEAL")) : (Def->Kind == ESQUpgradeKind::Evolution ? FString(TEXT("EVOLUTION"))
			: (Def->IsFallback() ? FString(TEXT("BONUS")) : (Current > 0 ? FString::Printf(TEXT("%s LV %d > %d"), *SQ::CategoryName(*Def), Current, Current + 1)
			: FString::Printf(TEXT("NEW  %s"), *SlotNote))));
		const FString RarText = SQ::RarityName(Rarity).ToUpper() + TEXT("  ") + LevelText;
		HudTxt(RarText, HudLabelFont(true), HudFitPx(RarText, HudLabelFont(true), 11.f, TextW), TextX, CY + 38.f, RarCol);
		const FString Gain = Def ? SQ::DescribeUpgradeGain(*Def, Current, Rarity) : FString(TEXT("Heal to full health"));
		const float GainPx = HudFitPx(Gain, HudLabelFont(true), 14.f, TextW);
		HudTxt(Gain, HudLabelFont(true), GainPx, TextX, CY + 62.f, Rarity > 0 ? FMath::Lerp(RarCol, SQUI::BoneWhite, 0.3f) : SQUI::Soul);
	}
}

void ASQHUD::DrawLevelUp(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D, ASQPlayerController* SQPC)
{
	const int32 NumOffers = FMath::Min(PS->Offers.Num(), 6);
	if (NumOffers <= 0)
	{
		return;
	}

	// a new set of offers (new level, reroll, next pending pick) flips in
	uint32 Sig = static_cast<uint32>(PS->OfferSerial) * 7919u + static_cast<uint32>(NumOffers);
	for (int32 i = 0; i < NumOffers; ++i)
	{
		Sig = Sig * 31u + GetTypeHash(PS->Offers[i]);
	}
	if (Sig != OfferSignature)
	{
		OfferSignature = Sig;
		OffersShownAt = HudNow - PickFlashAt < 0.5f ? HudNow + 0.15f : HudNow;
	}
	const float Elapsed = HudNow - OffersShownAt;

	// keyboard / pad presses (the controller sends them; this is only the flash)
	static const FKey PickKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	static const FKey PadKeys[] = { EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down };
	bool bRerollKey = false;
	bool bSkipKey = false;
	if (PlayerOwner)
	{
		for (int32 KeyIdx = 0; KeyIdx < static_cast<int32>(UE_ARRAY_COUNT(PickKeys)) && KeyIdx < NumOffers; ++KeyIdx)
		{
			const bool bPad = KeyIdx < static_cast<int32>(UE_ARRAY_COUNT(PadKeys)) && PlayerOwner->WasInputKeyJustPressed(PadKeys[KeyIdx]);
			if (PlayerOwner->WasInputKeyJustPressed(PickKeys[KeyIdx]) || bPad)
			{
				PickFlashIndex = KeyIdx;
				PickFlashAt = HudNow;
			}
		}
		bRerollKey = PlayerOwner->WasInputKeyJustPressed(EKeys::Q) || PlayerOwner->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top);
		bSkipKey = PlayerOwner->WasInputKeyJustPressed(EKeys::X) || PlayerOwner->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);
	}
	if (bRerollKey && PS->RerollsLeft > 0)
	{
		RerollFlashAt = HudNow;
	}
	if (bSkipKey)
	{
		SkipFlashAt = HudNow;
	}

	// mouse (the controller shows the cursor while picking)
	float MouseDX = -1.f, MouseDY = -1.f;
	if (PlayerOwner && PlayerOwner->bShowMouseCursor)
	{
		float MouseDesignX = 0.f, MouseDesignY = 0.f;
		if (HudMouseDesign(MouseDesignX, MouseDesignY))
		{
			MouseDX = MouseDesignX;
			MouseDY = MouseDesignY;
		}
	}
	const bool bClick = SQPC && PlayerOwner && HudNow >= PickClickLockUntil && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	bool bClickUsed = false;

	// ---- layout: big cards in the middle of the screen (the game is frozen, so the centre is ours)
	const float Gap = 34.f, CardH = 440.f;
	float CardW = 320.f;
	const float Avail = HudVW - 2.f * 24.f;
	if (NumOffers * CardW + (NumOffers - 1) * Gap > Avail)
	{
		CardW = FMath::Max(180.f, (Avail - (NumOffers - 1) * Gap) / NumOffers);
	}
	const float TotalW = NumOffers * CardW + (NumOffers - 1) * Gap;
	const float X0 = (HudVW - TotalW) * 0.5f;
	const float CardY = FMath::Max(170.f, (HudVH - CardH) * 0.5f + 10.f);
	const float MidX = HudVW * 0.5f;

	// ---- title: LEVEL UP!  +  TEAM LEVEL N
	{
		const float TitleIn = HudBackOut(Elapsed / 0.4f);
		HudAlpha = HudEaseOut(Elapsed / 0.25f);
		const float TitlePx = 84.f * (1.18f - 0.18f * TitleIn);
		const float TitleCY = CardY - 100.f;
		HudGlowTxt(TEXT("LEVEL UP!"), HudDisplayFont(), TitlePx, MidX, TitleCY - 0.62f * TitlePx, SQUI::Soul, SQUI::Soul, 1);
		const FString SubText = FString::Printf(TEXT("TEAM LEVEL %d"), GS->TeamLevel);
		const FString MoreText = PS->PendingLevelUps > 1 ? FString::Printf(TEXT("+%d MORE"), PS->PendingLevelUps - 1) : FString();
		const float SubW = HudTxtW(SubText, HudHeadFont(), 22.f);
		const float MoreW = MoreText.IsEmpty() ? 0.f : HudTxtW(MoreText, HudLabelFont(true), 12.f) + 12.f * 1.6f + 12.f;
		const float SubX = MidX - (SubW + MoreW) * 0.5f;
		const float SubCY = CardY - 40.f;
		HudTxt(SubText, HudHeadFont(), 22.f, SubX, SubCY - 0.62f * 22.f, SQUI::BoneWhite);
		if (!MoreText.IsEmpty())
		{
			HudPill(MoreText, SubX + SubW + 12.f, SubCY - 13.f, SQUI::Amber, SQUI::Midnight, 12.f);
		}
		HudAlpha = 1.f;
	}

	// ---- the cards
	for (int32 i = 0; i < NumOffers; ++i)
	{
		const float Entrance = (Elapsed - 0.11f * i) / 0.7f;
		if (Entrance <= 0.f)
		{
			continue;
		}
		const float RiseT = FMath::Clamp(Entrance / 0.45f, 0.f, 1.f);
		const float Rise = HudBackOut(RiseT);
		const float FlipT = FMath::Clamp((Entrance - 0.4f) / 0.5f, 0.f, 1.f);
		const bool bRevealed = FlipT >= 1.f;

		// The host's Choose() clears Offers immediately (mid-loop): stop before reading a removed card
		if (!PS->Offers.IsValidIndex(i))
		{
			break;
		}
		const FName OfferId = PS->Offers[i];
		const FSQUpgradeDef* Def = D->FindUpgrade(OfferId);
		const uint8 Rarity = PS->OfferRarity.IsValidIndex(i) ? FMath::Min<uint8>(PS->OfferRarity[i], 3) : 0;
		const FLinearColor RarCol = SQ::RarityColor(Rarity);
		const bool bEvo = Def && Def->Kind == ESQUpgradeKind::Evolution;
		const int32 Current = (Def && C) ? C->GetUpgradeLevel(OfferId) : 0;

		const float BaseX = X0 + i * (CardW + Gap);
		const bool bHover = bRevealed && MouseDX >= BaseX && MouseDX <= BaseX + CardW && MouseDY >= CardY && MouseDY <= CardY + CardH;
		if (bHover && bClick && !bClickUsed)
		{
			bClickUsed = true;
			PickFlashIndex = i;
			PickFlashAt = HudNow;
			PickClickLockUntil = HudNow + 0.3f;
			Play(TEXT("Soul"), 0.5f, 0.f);
			SQPC->Choose(i);
			break; // the offer list may be gone now; redraw next frame
		}
		const float Flash = PickFlashIndex == i ? FMath::Clamp(1.f - (HudNow - PickFlashAt) / 0.45f, 0.f, 1.f) : 0.f;
		float& Lift = OfferLift[i];
		Lift = FMath::FInterpTo(Lift, (bHover || Flash > 0.f) ? 1.f : 0.f, HudDelta, 14.f);
		const float CX = BaseX;
		const float CY = CardY + 90.f * (1.f - Rise) - 14.f * Lift;
		HudAlpha = HudEaseOut(RiseT);

		// -- still flipping: card back, then the front opening up (horizontal squash)
		if (!bRevealed)
		{
			const float FlipScale = FMath::Abs(FMath::Cos(FlipT * UE_PI));
			const float FaceW = CardW * FMath::Max(0.02f, FlipScale);
			const float FaceX = CX + (CardW - FaceW) * 0.5f;
			HudRound(FLinearColor(0.f, 0.f, 0.f, 0.4f), FaceX + 6.f, CY + 10.f, FaceW, CardH, 18.f);
			HudRound(RarCol, FaceX - 2.5f, CY - 2.5f, FaceW + 5.f, CardH + 5.f, 20.f);
			if (FlipT < 0.5f)
			{
				HudRound(SQUI::Midnight, FaceX, CY, FaceW, CardH, 18.f);
				if (FaceW > 40.f)
				{
					HudRound(SQUI::WithAlpha(RarCol, 0.16f), FaceX + 12.f, CY + 12.f, FaceW - 24.f, CardH - 24.f, 12.f);
					HudRound(SQUI::Midnight, FaceX + 16.f, CY + 16.f, FaceW - 32.f, CardH - 32.f, 10.f);
				}
				if (FlipScale > 0.4f)
				{
					HudSoulIcon(CX + CardW * 0.5f, CY + CardH * 0.5f, 96.f * FlipScale, RarCol, 1.f);
				}
			}
			else
			{
				HudRound(SQUI::Panel, FaceX, CY, FaceW, CardH, 18.f);
				HudRound(FMath::Lerp(SQUI::Panel, RarCol, 0.42f), FaceX, CY, FaceW, 104.f, 18.f);
			}
			continue;
		}

		const float RevealAge = Elapsed - 0.11f * i - 0.9f * 0.7f;
		const float Pulse = HudPulse(HudNow + i * 0.7f, 3.f);

		// -- glow by rarity: epic breathes, legendary blazes
		float GlowAmt = 0.15f + 0.12f * Rarity + 0.45f * Lift + Flash;
		if (Rarity == 2)
		{
			GlowAmt += 0.4f * Pulse;
		}
		else if (Rarity >= 3)
		{
			GlowAmt += 0.3f + 0.35f * HudPulse(HudNow + i, 4.5f);
		}
		HudGlow(RarCol, CX, CY, CardW, CardH, 20.f, GlowAmt);
		HudRound(FLinearColor(0.f, 0.f, 0.f, 0.4f), CX + 6.f, CY + 10.f + 6.f * Lift, CardW, CardH, 18.f);
		const float FrameT = 2.5f + 0.5f * Rarity;
		HudRound(FMath::Lerp(RarCol, FLinearColor::White, 0.35f * Lift), CX - FrameT, CY - FrameT, CardW + 2.f * FrameT, CardH + 2.f * FrameT, 18.f + FrameT);
		HudRound(FMath::Lerp(SQUI::Panel, SQUI::PanelHover, Lift), CX, CY, CardW, CardH, 18.f);
		if (Rarity == 2)
		{
			HudRound(SQUI::WithAlpha(RarCol, 0.05f + 0.06f * Pulse), CX + 4.f, CY + 4.f, CardW - 8.f, CardH - 8.f, 15.f);
		}

		// -- rarity band across the top
		const float BandH = 104.f;
		const FLinearColor BandCol = FMath::Lerp(SQUI::Panel, RarCol, 0.42f);
		HudRound(BandCol, CX, CY, CardW, BandH, 18.f);
		HudRect(BandCol, CX, CY + BandH - 18.f, CardW, 18.f);
		HudRect(SQUI::WithAlpha(RarCol, 0.9f), CX, CY + BandH, CardW, 2.f);
		for (int32 Stripe = 0; Stripe < 6; ++Stripe)
		{
			HudTilt(FLinearColor(1.f, 1.f, 1.f, 0.035f), CX + CardW * 0.4f + Stripe * 28.f, CY + 8.f, 10.f, BandH - 18.f, 25.f);
		}

		// -- number key badge (left) and rarity ribbon (centre)
		const float BadgeS = 34.f;
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.6f), CX + 14.f, CY + 16.f, BadgeS, BadgeS, 9.f);
		HudRound(SQUI::BoneWhite, CX + 14.f, CY + 14.f, BadgeS, BadgeS, 9.f);
		HudTxt(FString::FromInt(i + 1), HudHeadFont(), 22.f, CX + 14.f + BadgeS * 0.5f, CY + 14.f + BadgeS * 0.5f - 0.62f * 22.f, SQUI::Midnight, 1, false);
		const FString RarName = SQ::RarityName(Rarity).ToUpper();
		const float RibbonPx = 13.f;
		const float RibbonW = HudPill(RarName, CX + CardW * 0.5f, CY + 16.f, RarCol, SQUI::Midnight, RibbonPx, 1);
		if (Rarity >= 3)
		{
			HudShine(CX + (CardW - RibbonW) * 0.5f, CY + 16.f, RibbonW, RibbonPx + 14.f, 1.8f, 0.f, i * 0.3f);
		}

		// -- icon disc overlapping the band
		const float IconCX = CX + CardW * 0.5f, IconCY = CY + BandH, IconR = 54.f;
		const FLinearColor IconCol = Def ? FMath::Lerp(Def->Color, FLinearColor::White, 0.15f) : SQUI::Danger;
		HudDisc(SQUI::WithAlpha(RarCol, 0.14f + 0.12f * Lift + (Rarity >= 2 ? 0.08f * Pulse : 0.f)), IconCX, IconCY, IconR + 12.f);
		HudDisc(RarCol, IconCX, IconCY, IconR);
		HudDisc(SQUI::Midnight, IconCX, IconCY, IconR - 4.f);
		HudDisc(SQUI::WithAlpha(IconCol, 0.14f), IconCX, IconCY, IconR - 10.f);
		const FString Glyph = Def ? Def->IconGlyph : FString(TEXT("+"));
		if (!Glyph.IsEmpty())
		{
			const float GlyphPx = HudFitPx(Glyph, HudHeadFont(), 50.f * (1.f + 0.08f * Lift), (IconR - 12.f) * 2.f);
			HudTxt(Glyph, HudHeadFont(), GlyphPx, IconCX, IconCY - 0.62f * GlyphPx, IconCol, 1);
		}
		else
		{
			const int32 GlyphKind = !Def ? 3 : (Def->Kind == ESQUpgradeKind::Weapon ? 0 : (Def->Kind == ESQUpgradeKind::Relic ? 1 : 2));
			HudGlyph(GlyphKind, IconCX, IconCY, 50.f * (1.f + 0.08f * Lift), IconCol);
		}

		// -- level ribbon: NEW / LV a > b / EVOLUTION / FULL HEAL
		float RowY = IconCY + IconR + 10.f;
		if (!Def)
		{
			HudPill(TEXT("FULL HEAL"), IconCX, RowY, SQUI::BoneWhite, SQUI::Midnight, 12.f, 1);
		}
		else if (bEvo)
		{
			const float EvoW = HudPill(TEXT("EVOLUTION"), IconCX, RowY, SQUI::Amber, SQUI::Midnight, 12.f, 1);
			HudShine(IconCX - EvoW * 0.5f, RowY, EvoW, 26.f, 2.2f, 0.f, i * 0.3f);
		}
		else if (Current > 0)
		{
			const FString FromText = FString::Printf(TEXT("LV %d"), Current);
			const FString ToText = FString::FromInt(Current + 1);
			const float FromW = HudTxtW(FromText, HudLabelFont(true), 12.f), ToW = HudTxtW(ToText, HudLabelFont(true), 12.f);
			const float RW = FromW + 6.f + 10.f + 6.f + ToW + 24.f, RH = 26.f;
			const float RX = IconCX - RW * 0.5f;
			HudRound(SQUI::Amber, RX, RowY, RW, RH, RH * 0.5f);
			float RCursor = RX + 12.f;
			RCursor += HudTxt(FromText, HudLabelFont(true), 12.f, RCursor, RowY + RH * 0.5f - 0.62f * 12.f, SQUI::Midnight, 0, false) + 6.f;
			HudChevron(RCursor, RowY + RH * 0.5f - 6.f, 12.f, SQUI::Midnight);
			RCursor += 10.f + 6.f;
			HudTxt(ToText, HudLabelFont(true), 12.f, RCursor, RowY + RH * 0.5f - 0.62f * 12.f, SQUI::Midnight, 0, false);
		}
		else
		{
			HudPill(Def->IsFallback() ? FString(TEXT("BONUS")) : FString(TEXT("NEW")), IconCX, RowY, SQUI::Soul, SQUI::Midnight, 12.f, 1);
		}
		RowY += 34.f;

		// -- name
		const FString CardName = Def ? Def->Name.ToUpper() : FString(TEXT("MEND"));
		const float NamePx = HudFitPx(CardName, HudHeadFont(), 28.f, CardW - 28.f);
		HudTxt(CardName, HudHeadFont(), NamePx, IconCX, RowY, (bEvo || Rarity >= 3) ? FMath::Lerp(RarCol, SQUI::BoneWhite, 0.3f) : SQUI::BoneWhite, 1);
		RowY += 38.f;

		// -- kind / tag chips
		{
			// items/tomes pass: the chip shows the category with its slot use ("TOMES 3/4", "WEAPONS 2/4") or EVOLUTION / BONUS
			const FString KindSlots = (Def && C) ? C->SlotText(*Def) : FString();
			const FString KindText = !Def ? FString(TEXT("HEAL")) : (KindSlots.IsEmpty() ? SQ::CategoryName(*Def) : KindSlots);
			const FString TagText = (Def && !Def->Tag.IsNone()) ? Def->Tag.ToString().ToUpper() : FString();
			const float ChipPx = 11.f;
			const float KindChipW = HudTxtW(KindText, HudLabelFont(true), ChipPx) + ChipPx * 1.6f;
			const float TagChipW = TagText.IsEmpty() ? 0.f : HudTxtW(TagText, HudLabelFont(true), ChipPx) + ChipPx * 1.6f + 6.f;
			const float ChipX = IconCX - (KindChipW + TagChipW) * 0.5f;
			HudPill(KindText, ChipX, RowY, SQUI::WithAlpha(HudKindColour(Def), 0.85f), SQUI::Midnight, ChipPx);
			if (!TagText.IsEmpty())
			{
				HudPill(TagText, ChipX + KindChipW + 6.f, RowY, SQUI::WithAlpha(SQUI::Line, 0.95f), SQUI::BoneWhite, ChipPx);
			}
		}
		RowY += 34.f;

		// -- the concrete gain ("+12 damage", "-8% cooldown"...)
		const FString Gain = Def ? SQ::DescribeUpgradeGain(*Def, Current, Rarity) : FString(TEXT("Heal to full health"));
		int32 GainLines = 0;
		if (!Gain.IsEmpty())
		{
			GainLines = HudWrap(Gain, HudLabelFont(true), 16.f, IconCX, RowY, CardW - 32.f, Rarity > 0 ? FMath::Lerp(RarCol, SQUI::BoneWhite, 0.25f) : SQUI::Soul,
				20.f, 2, true, 1);
		}
		RowY += FMath::Max(1, GainLines) * 20.f + 8.f;
		HudRect(SQUI::WithAlpha(SQUI::Line, 0.9f), CX + 28.f, RowY, CardW - 56.f, 1.5f);
		RowY += 10.f;

		// -- the witty description
		const FString Desc = Def ? Def->Description : FString(TEXT("Nothing left to upgrade: heal to full instead."));
		const int32 MaxLvl = Def ? FMath::Max(1, Def->MaxLevel) : 1;
		const bool bPips = Def && !bEvo && MaxLvl > 1;
		const float DescBottom = CY + CardH - (bPips ? 30.f : 14.f);
		const int32 DescLines = FMath::Max(1, FMath::FloorToInt((DescBottom - RowY) / 18.f));
		HudWrap(Desc, HudLabelFont(false), 14.f, IconCX, RowY, CardW - 32.f, SQUI::Muted, 18.f, DescLines, true, 1);

		// -- level pips (the new level blinks)
		if (bPips)
		{
			const float PipW = FMath::Min(16.f, (CardW - 60.f) / MaxLvl - 4.f);
			const float PipsW = MaxLvl * (PipW + 4.f) - 4.f;
			const float PipX0 = IconCX - PipsW * 0.5f;
			for (int32 Pip = 0; Pip < MaxLvl; ++Pip)
			{
				FLinearColor PipCol = SQUI::Line;
				if (Pip < Current)
				{
					PipCol = SQUI::Amber;
				}
				else if (Pip == Current)
				{
					PipCol = FMath::Lerp(SQUI::Line, RarCol, 0.35f + 0.65f * Pulse);
				}
				HudRound(PipCol, PipX0 + Pip * (PipW + 4.f), CY + CardH - 20.f, PipW, 6.f, 3.f);
			}
		}

		// -- legendary: a shine sweep and twinkling sparkles
		if (Rarity >= 3)
		{
			HudShine(CX, CY, CardW, CardH, 2.8f, 4.f, i * 0.4f);
			for (int32 Spark = 0; Spark < 7; ++Spark)
			{
				const float Cycle = HudNow * 0.8f + Spark * 0.143f + i * 0.31f;
				const float Life01 = FMath::Frac(Cycle);
				const float Seed = FMath::FloorToFloat(Cycle) * 12.9898f + Spark * 78.233f + i * 3.17f;
				const float RandX = FMath::Frac(FMath::Sin(Seed) * 43758.5453f);
				const float RandY = FMath::Frac(FMath::Sin(Seed * 1.37f + 4.1f) * 24634.6345f);
				const float SparkX = CX + 10.f + RandX * (CardW - 20.f);
				const float SparkY = CY + 10.f + RandY * (CardH - 20.f);
				HudSparkle(SparkX, SparkY, 9.f * FMath::Sin(Life01 * UE_PI), SQUI::WithAlpha(FMath::Lerp(RarCol, FLinearColor::White, 0.5f), 0.9f));
			}
		}
		else if (bEvo)
		{
			HudShine(CX, CY, CardW, CardH, 3.4f, 4.f, i * 0.4f);
		}

		// -- reveal pop for epic and legendary, and the pick flash
		if (Rarity >= 2 && RevealAge >= 0.f && RevealAge < 0.4f)
		{
			const float Pop = 1.f - RevealAge / 0.4f;
			HudGlow(RarCol, CX, CY, CardW, CardH, 20.f, 1.6f * Pop);
			HudRound(SQUI::WithAlpha(FLinearColor::White, 0.3f * Pop), CX, CY, CardW, CardH, 18.f);
		}
		if (Flash > 0.f)
		{
			HudRound(FLinearColor(1.f, 1.f, 1.f, 0.35f * Flash), CX, CY, CardW, CardH, 18.f);
		}
	}
	HudAlpha = 1.f;

	// the burst where a card was just picked (the offers may already have changed underneath)
	const float Burst = FMath::Clamp(1.f - (HudNow - PickFlashAt) / 0.45f, 0.f, 1.f);
	if (Burst > 0.f && PickFlashIndex >= 0 && PickFlashIndex < NumOffers && Elapsed < 0.f)
	{
		HudAlpha = Burst;
		const float BurstX = X0 + PickFlashIndex * (CardW + Gap);
		HudGlow(SQUI::Soul, BurstX, CardY, CardW, CardH, 18.f, 1.5f);
		HudRound(SQUI::WithAlpha(SQUI::Soul, 0.25f), BurstX, CardY, CardW, CardH, 18.f);
		HudAlpha = 1.f;
	}

	// ---- REROLL (Q) and SKIP (X) under the cards
	{
		const float ButtonsIn = HudEaseOut((Elapsed - 0.45f) / 0.3f);
		if (ButtonsIn > 0.01f)
		{
			const float BtnW = 250.f, BtnH = 58.f, BtnGap = 26.f;
			const float BtnY = CardY + CardH + 34.f + 16.f * (1.f - ButtonsIn);
			const float RerollX = MidX - BtnGap * 0.5f - BtnW;
			const float SkipX = MidX + BtnGap * 0.5f;
			const bool bCanReroll = PS->RerollsLeft > 0;
			const bool bReady = ButtonsIn >= 0.99f;
			const bool bOverReroll = bReady && MouseDX >= RerollX && MouseDX <= RerollX + BtnW && MouseDY >= BtnY && MouseDY <= BtnY + BtnH;
			const bool bOverSkip = bReady && MouseDX >= SkipX && MouseDX <= SkipX + BtnW && MouseDY >= BtnY && MouseDY <= BtnY + BtnH;
			if (bClick && !bClickUsed && bOverReroll && bCanReroll)
			{
				bClickUsed = true;
				RerollFlashAt = HudNow;
				PickClickLockUntil = HudNow + 0.3f;
				Play(TEXT("Soul"), 0.4f, 0.1f);
				SQPC->Reroll();
			}
			else if (bClick && !bClickUsed && bOverSkip)
			{
				bClickUsed = true;
				SkipFlashAt = HudNow;
				PickClickLockUntil = HudNow + 0.3f;
				Play(TEXT("Soul"), 0.3f, 0.1f);
				SQPC->Skip();
			}
			const float RerollFlash = FMath::Clamp(1.f - (HudNow - RerollFlashAt) / 0.35f, 0.f, 1.f);
			const float SkipFlash = FMath::Clamp(1.f - (HudNow - SkipFlashAt) / 0.35f, 0.f, 1.f);
			RerollLift = FMath::FInterpTo(RerollLift, (bCanReroll && (bOverReroll || RerollFlash > 0.f)) ? 1.f : 0.f, HudDelta, 14.f);
			SkipLift = FMath::FInterpTo(SkipLift, (bOverSkip || SkipFlash > 0.f) ? 1.f : 0.f, HudDelta, 14.f);

			const int32 HealPct = FMath::RoundToInt(100.f * D->Tune(TEXT("SkipHealFraction"), 0.15f));
			HudAlpha = ButtonsIn;
			HudPickButton(TEXT("REROLL"), TEXT("Q"), bCanReroll ? FString::Printf(TEXT("%d LEFT THIS RUN"), PS->RerollsLeft) : FString(TEXT("NONE LEFT")),
				RerollX, BtnY, BtnW, BtnH, SQUI::Soul, bCanReroll, RerollLift);
			HudPickButton(TEXT("SKIP"), TEXT("X"), FString::Printf(TEXT("HEAL %d%% INSTEAD"), HealPct), SkipX, BtnY, BtnW, BtnH, SQUI::Amber, true, SkipLift);
			HudAlpha = 1.f;
			HudTxt(TEXT("CLICK A CARD  \u00B7  1 / 2 / 3  \u00B7  D-PAD"), HudLabelFont(true), 12.f, MidX, BtnY + BtnH + 16.f, SQUI::WithAlpha(SQUI::Muted, ButtonsIn), 1, false);
		}
	}
}

void ASQHUD::DrawPickStatus(const ASQGameState* GS, const ASQPlayerState* PS, bool bCompact)
{
	// who is in this pause, and who is still choosing
	TArray<const ASQPlayerState*> Pickers;
	for (const APlayerState* Each : GS->PlayerArray)
	{
		const ASQPlayerState* SQPS = Cast<ASQPlayerState>(Each);
		if (SQPS && SQPS->bInPickPause)
		{
			Pickers.Add(SQPS);
		}
	}
	const int32 PickTotal = FMath::Max(GS->PickersTotal, Pickers.Num());
	const int32 PickDone = FMath::Min(GS->PickersDone, PickTotal);
	const float WaitIn = HudEaseOut((HudNow - WaitShownAt) / 0.3f);
	const float RowH = bCompact ? 26.f : 34.f;

	if (bCompact)
	{
		// small corner list while you're still choosing yourself
		const float PW = 250.f;
		const float PH = 44.f + Pickers.Num() * RowH + 10.f;
		const float PX = 24.f;
		const float PY = HudVH - 24.f - 120.f - PH;
		HudAlpha = WaitIn;
		HudPanel(PX, PY, PW, PH, 16.f, SQUI::Line);
		HudTxt(FString::Printf(TEXT("TEAM PICKS  %d/%d"), PickDone, PickTotal), HudHeadFont(), 18.f, PX + 16.f, PY + 12.f, SQUI::BoneWhite);
		float RowCY = PY + 44.f + RowH * 0.5f;
		for (const ASQPlayerState* Picker : Pickers)
		{
			const bool bDone = !Picker->IsChoosing();
			const FString PickerName = Picker->GetPlayerName() + (Picker == PS ? FString(TEXT(" (you)")) : FString());
			HudDisc(bDone ? SQUI::Soul : FMath::Lerp(SQUI::Line, SQUI::Amber, HudPulse(HudNow, 6.f)), PX + 22.f, RowCY, 5.f);
			const float NameMax = PW - 44.f - 80.f;
			HudTxt(PickerName, HudLabelFont(true), HudFitPx(PickerName, HudLabelFont(true), 14.f, NameMax), PX + 36.f, RowCY - 0.62f * 14.f, SQUI::BoneWhite, 0, false);
			HudTxt(bDone ? FString(TEXT("DONE")) : FString(TEXT("PICKING")), HudLabelFont(true), 11.f, PX + PW - 16.f, RowCY - 0.62f * 11.f,
				bDone ? SQUI::Soul : SQUI::Amber, 2, false);
			RowCY += RowH;
		}
		HudAlpha = 1.f;
		return;
	}

	// big centred panel: you've picked, the others haven't
	const float PW = 480.f;
	const float PH = 112.f + Pickers.Num() * RowH + 18.f;
	const float PX = (HudVW - PW) * 0.5f;
	const float PY = HudVH * 0.4f - PH * 0.5f + 16.f * (1.f - WaitIn);
	HudAlpha = WaitIn;
	HudGlow(SQUI::Soul, PX, PY, PW, PH, 22.f, 0.25f + 0.2f * HudPulse(HudNow, 3.f));
	HudPanel(PX, PY, PW, PH, 22.f, SQUI::WithAlpha(SQUI::Soul, 0.7f));
	const float WaitPx = HudFitPx(TEXT("WAITING FOR TEAMMATES"), HudHeadFont(), 30.f, PW - 40.f);
	HudTxt(TEXT("WAITING FOR TEAMMATES"), HudHeadFont(), WaitPx, PX + PW * 0.5f, PY + 20.f, SQUI::Soul, 1);
	// three bouncing dots
	for (int32 Dot = 0; Dot < 3; ++Dot)
	{
		const float Bounce = FMath::Max(0.f, FMath::Sin(HudNow * 6.f - Dot * 0.7f));
		HudDisc(SQUI::WithAlpha(SQUI::Soul, 0.5f + 0.5f * Bounce), PX + PW * 0.5f - 16.f + Dot * 16.f, PY + 66.f - 5.f * Bounce, 4.f);
	}
	HudTxt(FString::Printf(TEXT("%d / %d PICKED  \u00B7  THE NIGHT IS PAUSED"), PickDone, PickTotal), HudLabelFont(true), 13.f, PX + PW * 0.5f, PY + 80.f, SQUI::Muted, 1, false);
	float RowY = PY + 112.f;
	for (const ASQPlayerState* Picker : Pickers)
	{
		const bool bDone = !Picker->IsChoosing();
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.55f), PX + 18.f, RowY, PW - 36.f, RowH - 6.f, 10.f);
		const FString PickerName = Picker->GetPlayerName() + (Picker == PS ? FString(TEXT(" (you)")) : FString());
		HudTxt(PickerName, HudLabelFont(true), HudFitPx(PickerName, HudLabelFont(true), 15.f, PW - 200.f), PX + 32.f, RowY + (RowH - 6.f) * 0.5f - 0.62f * 15.f, SQUI::BoneWhite, 0, false);
		if (bDone)
		{
			HudPill(TEXT("DONE"), PX + PW - 28.f, RowY + 2.f, SQUI::Soul, SQUI::Midnight, 11.f, 2);
		}
		else
		{
			HudAlpha = WaitIn * (0.65f + 0.35f * HudPulse(HudNow, 5.f));
			HudPill(Picker->PendingLevelUps > 1 ? FString::Printf(TEXT("PICKING  x%d"), Picker->PendingLevelUps) : FString(TEXT("PICKING")),
				PX + PW - 28.f, RowY + 2.f, SQUI::Amber, SQUI::Midnight, 11.f, 2);
			HudAlpha = WaitIn;
		}
		RowY += RowH;
	}
	HudAlpha = 1.f;
}

void ASQHUD::DrawPickCountdown(const ASQGameState* GS)
{
	if (!GS->bPickPause || GS->PickPauseTimeLeft <= 0.f || GS->PickPauseTimeLeft >= 10.f)
	{
		return;
	}
	const int32 Secs = FMath::Max(1, FMath::CeilToInt(GS->PickPauseTimeLeft));
	const float Beat = HudPulse(HudNow, 8.f);
	const float PW = 250.f, PH = 96.f;
	const float PX = 24.f, PY = HudVH - 24.f - PH;
	HudGlow(SQUI::Danger, PX, PY, PW, PH, 18.f, 0.25f + 0.35f * Beat);
	HudPanel(PX, PY, PW, PH, 18.f, FMath::Lerp(SQUI::Line, SQUI::Danger, Beat));
	const float RingCX = PX + 48.f, RingCY = PY + PH * 0.5f, RingR = 30.f;
	constexpr int32 TickDots = 30;
	const int32 Lit = FMath::RoundToInt(FMath::Clamp(GS->PickPauseTimeLeft / 10.f, 0.f, 1.f) * TickDots);
	for (int32 DotIdx = 0; DotIdx < TickDots; ++DotIdx)
	{
		const float Ang = -UE_HALF_PI + DotIdx * (2.f * UE_PI / TickDots);
		HudDisc(DotIdx < Lit ? SQUI::Danger : SQUI::Line, RingCX + FMath::Cos(Ang) * RingR, RingCY + FMath::Sin(Ang) * RingR, 2.6f);
	}
	const FString SecText = FString::FromInt(Secs);
	HudTxt(SecText, HudHeadFont(), 30.f, RingCX, RingCY - 0.62f * 30.f, SQUI::BoneWhite, 1);
	HudTxt(TEXT("AUTO-PICK IN"), HudLabelFont(true), 12.f, PX + 94.f, PY + 24.f, SQUI::Muted, 0, false);
	HudTxt(FString::Printf(TEXT("%d SECONDS"), Secs), HudHeadFont(), 22.f, PX + 94.f, PY + 44.f, SQUI::Danger);
}

// ============================================================ Death's Office (shop)

void ASQHUD::DrawShop(const ASQGameState* GS, const ASQPlayerState* PS, const USQData* D)
{
	const float Margin = 24.f;
	const float Elapsed = HudNow - ShopShownAt;
	const float In = HudEaseOut(Elapsed / 0.4f);
	const float PW = FMath::Min(1000.f, HudVW - 2.f * Margin);
	const float PX = (HudVW - PW) * 0.5f;
	// stay below whichever top corner panels this panel overlaps
	float MinY = 24.f + 60.f + 16.f;
	if (PX < Margin + 400.f + 16.f)
	{
		MinY = FMath::Max(MinY, Margin + 164.f + 16.f);
	}
	if (PX + PW > HudVW - Margin - 380.f - 16.f)
	{
		MinY = FMath::Max(MinY, TeamPanelBottom + 16.f);
	}
	const float Inner = PW - 56.f;

	const float StoryPx = 16.f, StoryStep = 22.f;
	const float FooterH = 70.f;
	const int32 NumItems = D->Shop.Num();
	const int32 Cols = FMath::Clamp(NumItems, 1, 3);
	const int32 Rows = (NumItems + Cols - 1) / Cols;
	const float ItemGap = 16.f, ItemH = 132.f;
	const float ItemW = (Inner - (Cols - 1) * ItemGap) / Cols;
	const float HeaderH = 92.f;
	const float FixedH = HeaderH + Rows * (ItemH + ItemGap) + FooterH;
	const float MaxStoryH = FMath::Max(0.f, HudVH - MinY - 24.f - FixedH - 28.f);
	const int32 MaxStoryLines = FMath::Clamp(FMath::FloorToInt(MaxStoryH / StoryStep), 0, 5);
	int32 StoryCount = 0;
	if (!GS->StoryText.IsEmpty() && MaxStoryLines > 0)
	{
		StoryCount = HudWrap(GS->StoryText, HudLabelFont(false), StoryPx, 0.f, 0.f, Inner - 36.f, SQUI::BoneWhite, StoryStep, MaxStoryLines, false);
	}
	const float StoryH = StoryCount > 0 ? StoryCount * StoryStep + 28.f + 14.f : 0.f;
	const float PH = FixedH + StoryH;
	const float PY = FMath::Max(MinY, FMath::Min(HudVH * 0.2f, HudVH - 24.f - PH)) + 24.f * (1.f - In);

	// key flashes for buying
	static const FKey BuyKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	if (PlayerOwner)
	{
		for (int32 KeyIdx = 0; KeyIdx < static_cast<int32>(UE_ARRAY_COUNT(BuyKeys)) && KeyIdx < NumItems; ++KeyIdx)
		{
			if (PlayerOwner->WasInputKeyJustPressed(BuyKeys[KeyIdx]))
			{
				ShopFlashIndex = KeyIdx;
				ShopFlashAt = HudNow;
			}
		}
	}

	HudAlpha = In;
	HudPanel(PX, PY, PW, PH, 24.f, SQUI::Line, 0.96f);
	HudRound(SQUI::Amber, PX, PY, PW, 30.f, 24.f); // amber band across the top, like the menu's file cards
	HudRect(SQUI::WithAlpha(SQUI::Panel, 0.96f), PX, PY + 12.f, PW, 20.f);

	const float IX = PX + 28.f;
	HudTxt(TEXT("DEATH'S OFFICE"), HudHeadFont(), 34.f, IX, PY + 34.f, SQUI::BoneWhite);
	// souls to spend + timer, right
	{
		const FString SoulsText = FString::Printf(TEXT("%d SOULS TO SPEND"), GS->ShopSouls);
		const float SoulsW = HudTxtW(SoulsText, HudHeadFont(), 18.f) + 56.f;
		const float SX = PX + PW - 28.f - SoulsW, SY = PY + 34.f;
		HudRound(SQUI::WithAlpha(SQUI::Amber, 0.16f), SX, SY, SoulsW, 36.f, 18.f);
		HudSoulIcon(SX + 20.f, SY + 18.f, 20.f, SQUI::Amber, 0.6f);
		HudTxt(SoulsText, HudHeadFont(), 18.f, SX + 38.f, SY + 18.f - 0.62f * 18.f, SQUI::Amber);
		const FString Timer = FString::Printf(TEXT("NEXT NIGHT IN %ds"), FMath::Max(0, FMath::CeilToInt(GS->PhaseTimeLeft)));
		const bool bHurry = GS->PhaseTimeLeft < 5.f;
		HudTxt(Timer, HudLabelFont(true), 13.f, SX - 18.f, SY + 11.f, bHurry ? FMath::Lerp(SQUI::Muted, SQUI::Danger, HudPulse(HudNow, 8.f)) : SQUI::Muted, 2, false);
	}
	float CurY = PY + HeaderH;

	// story, as a quote with a soul-teal rule
	if (StoryCount > 0)
	{
		const float QH = StoryCount * StoryStep + 28.f;
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.55f), IX, CurY, Inner, QH, 14.f);
		HudRound(SQUI::Soul, IX + 12.f, CurY + 12.f, 4.f, QH - 24.f, 2.f);
		HudWrap(GS->StoryText, HudLabelFont(false), StoryPx, IX + 30.f, CurY + 14.f, Inner - 36.f, SQUI::BoneWhite, StoryStep, MaxStoryLines);
		CurY += QH + 14.f;
	}

	// item cards
	for (int32 i = 0; i < NumItems; ++i)
	{
		const FSQShopDef& Item = D->Shop[i];
		const int32 ItemPrice = GS->ShopPrice(i); // escalating price (Docs/Balance.md)
		const bool bAfford = GS->ShopSouls >= ItemPrice;
		const float ItemIn = HudEaseOut((Elapsed - 0.1f - 0.06f * i) / 0.35f);
		const float CX = IX + (i % Cols) * (ItemW + ItemGap);
		const float CY = CurY + (i / Cols) * (ItemH + ItemGap) + 16.f * (1.f - ItemIn);
		const float Flash = ShopFlashIndex == i ? FMath::Clamp(1.f - (HudNow - ShopFlashAt) / 0.4f, 0.f, 1.f) : 0.f;
		HudAlpha = In * ItemIn;
		if (bAfford)
		{
			HudGlow(SQUI::Amber, CX, CY, ItemW, ItemH, 16.f, 0.15f + Flash);
		}
		HudRound(FLinearColor(0.f, 0.f, 0.f, 0.3f), CX + 4.f, CY + 7.f, ItemW, ItemH, 16.f);
		HudRound(bAfford ? FMath::Lerp(SQUI::Line, SQUI::Amber, 0.5f + 0.5f * Flash) : SQUI::Line, CX - 2.f, CY - 2.f, ItemW + 4.f, ItemH + 4.f, 18.f);
		HudRound(bAfford ? SQUI::PanelHover : SQUI::Panel, CX, CY, ItemW, ItemH, 16.f);
		const float CapW = HudKeyCap(FString::FromInt(i + 1), CX + 14.f, CY + 14.f, 28.f);
		const FString CostText = FString::Printf(TEXT("%d"), ItemPrice);
		const float CostW = HudTxtW(CostText, HudHeadFont(), 16.f) + 40.f;
		const float CostX = CX + ItemW - 14.f - CostW;
		HudRound(bAfford ? SQUI::Amber : SQUI::WithAlpha(SQUI::Line, 0.8f), CostX, CY + 14.f, CostW, 28.f, 14.f);
		HudSoulIcon(CostX + 15.f, CY + 28.f, 15.f, bAfford ? SQUI::Midnight : SQUI::Disabled, 0.f);
		HudTxt(CostText, HudHeadFont(), 16.f, CostX + 28.f, CY + 28.f - 0.62f * 16.f, bAfford ? SQUI::Midnight : SQUI::Disabled, 0, false);
		const float NameX = CX + 14.f + CapW + 10.f;
		const FString ItemName = Item.Name.ToUpper();
		HudTxt(ItemName, HudHeadFont(), HudFitPx(ItemName, HudHeadFont(), 20.f, CostX - NameX - 8.f), NameX, CY + 16.f, bAfford ? SQUI::BoneWhite : SQUI::Muted);
		HudWrap(Item.Description, HudLabelFont(false), 14.f, CX + 16.f, CY + 56.f, ItemW - 32.f, bAfford ? SQUI::Muted : SQUI::Disabled, 18.f, 3);
		if (!bAfford)
		{
			HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.3f), CX, CY, ItemW, ItemH, 16.f);
		}
		if (Flash > 0.f)
		{
			HudRound(FLinearColor(1.f, 1.f, 1.f, 0.25f * Flash), CX, CY, ItemW, ItemH, 16.f);
		}
	}
	HudAlpha = In;
	CurY += Rows * (ItemH + ItemGap);

	// ready footer
	const bool bReady = PS && PS->bShopReady;
	const float FootY = CurY + 6.f;
	if (bReady)
	{
		HudPill(TEXT("READY  \u00B7  WAITING FOR THE CREW"), PX + PW * 0.5f, FootY, SQUI::WithAlpha(SQUI::Soul, 0.18f), SQUI::Soul, 16.f, 1);
	}
	else
	{
		const FString ReadyText = TEXT("WHEN READY");
		const float TW = HudTxtW(ReadyText, HudHeadFont(), 20.f);
		const float BtnW = 36.f + TW + 16.f + 46.f, BtnH = 44.f;
		const float BX = PX + (PW - BtnW) * 0.5f;
		HudGlow(SQUI::Soul, BX, FootY, BtnW, BtnH, 22.f, 0.3f + 0.3f * HudPulse(HudNow, 4.f));
		HudRound(SQUI::Soul, BX, FootY, BtnW, BtnH, 22.f);
		HudShine(BX, FootY, BtnW, BtnH, 3.4f, 4.f);
		const float KW = HudKeyCap(TEXT("R"), BX + 18.f, FootY + 9.f, 26.f);
		HudTxt(ReadyText, HudHeadFont(), 20.f, BX + 18.f + KW + 12.f, FootY + BtnH * 0.5f - 0.62f * 20.f, SQUI::Midnight, 0, false);
	}
	HudAlpha = 1.f;
}

// ============================================================ end screens

// ============================================================ lobby: full-screen reaper select (TAB folds it away)
//
//   +------------------------------------------------------------------------------------------+
//   | CHOOSE YOUR REAPER   0/1 READY   (coins)                                    [TAB] HIDE    |
//   | +-- KIT ----------+          REAPER NAME                         +-- MISSION ---------+   |
//   | | weapon          |          [role] [status]                     | name, desc, HARD   |   |
//   | | passive         |                                              +-- THE CREW --------+   |
//   | | E  (LV 3)       |           3D centre stage                    | 4 rows             |   |
//   | | R  (LV 6)       |                                              |                    |   |
//   | | stat bars       |                                              |                    |   |
//   | +-----------------+   [portrait cards, 2 rows, scrolls]          | [  READY  ]        |   |
//   | keys ...                                                                               |   |
//   +------------------------------------------------------------------------------------------+

void ASQHUD::DrawLobby(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D, ASQPlayerController* SQPC)
{
	USQSaveGame* Save = SQPC ? SQPC->GetProfile() : nullptr;
	const bool bUnlockAll = D->Tune(TEXT("UnlockAllReapers"), 0.f) > 0.5f;
	const int32 NumReapers = D->Reapers.Num();

	int32 ReadyCount = 0, CrewCount = 0;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		if (const ASQPlayerState* OtherSQ = Cast<ASQPlayerState>(Other))
		{
			++CrewCount;
			ReadyCount += (OtherSQ->bShopReady || OtherSQ->IsABot()) ? 1 : 0;
		}
	}
	const bool bMeReady = PS && PS->bShopReady;
	const bool bCountdown = GS->PhaseTimeLeft >= 0.f;

	// ---- focus: follows your reaper (also after a 1-9 quick pick or a server change)
	const FName MyReaper = C ? C->ReaperId : (PS ? PS->ReaperId : FName(NAME_None));
	int32 MyIndex = 0;
	for (int32 i = 0; i < NumReapers; ++i)
	{
		MyIndex = D->Reapers[i].Id == MyReaper ? i : MyIndex;
	}
	bool bSnapRoster = false;
	if (LobbyFocus < 0 || LobbyFocus >= NumReapers || MyReaper != LobbyMineSeen)
	{
		LobbyFocus = MyIndex;
		LobbyMineSeen = MyReaper;
		LobbyHireArmed = -1;
		LobbyFocusAt = HudNow;
		bSnapRoster = true;
	}
	if (LobbyTileLift.Num() != NumReapers)
	{
		LobbyTileLift.SetNumZeroed(NumReapers);
	}
	if (LobbyHireArmed >= 0 && HudNow - LobbyHireArmedAt > 4.f)
	{
		LobbyHireArmed = -1;
	}
	const FSQReaperDef* FocusDef = D->Reapers.IsValidIndex(LobbyFocus) ? &D->Reapers[LobbyFocus] : nullptr;
	const FLinearColor Accent = FocusDef ? SQUI::RoleColour(FocusDef->Role, FocusDef->Color) : SQUI::Soul;
	LobbyStudioUpdate(true, FocusDef ? FocusDef->Id : FName(NAME_None), Accent, D);

	// ---- layout
	const float Open = HudEaseOut(LobbyOpen);
	const float Enter = HudEaseOut((HudNow - LobbyShownAt) / 0.6f);
	const float Vis = Open * Enter;
	const float Margin = 28.f;
	const float TopH = 92.f;
	const float HintH = 48.f;
	const float LeftW = FMath::Clamp(HudVW * 0.235f, 340.f, 450.f);
	const float RightW = FMath::Clamp(HudVW * 0.205f, 320.f, 400.f);
	const float CentreX0 = Margin + LeftW + 24.f;
	const float CentreX1 = HudVW - Margin - RightW - 24.f;
	const float CentreW = FMath::Max(200.f, CentreX1 - CentreX0);
	const float CardW = 84.f, CardH = 104.f, CardGap = 10.f;
	const int32 Cols = FMath::Max(1, FMath::FloorToInt((CentreW + CardGap) / (CardW + CardGap)));
	const int32 TotalRows = FMath::Max(1, (NumReapers + Cols - 1) / Cols);
	const int32 VisibleRows = FMath::Min(2, TotalRows);
	const float RosterH = VisibleRows * CardH + (VisibleRows - 1) * CardGap;
	const float BottomY = HudVH - HintH - 22.f;
	const float RosterY = BottomY - RosterH + (1.f - Vis) * 60.f;
	const float StageY = TopH + 6.f;
	const float StageH = FMath::Max(160.f, RosterY - 22.f - StageY);
	const float SlideL = (1.f - Vis) * (LeftW + 80.f);
	const float SlideR = (1.f - Vis) * (RightW + 80.f);

	// ---- collapsed: a pill on the right edge
	if (LobbyOpen < 0.99f)
	{
		HudAlpha = 1.f - Open;
		const FString PillText = FString::Printf(TEXT("REAPER SELECT  -  %d/%d READY"), ReadyCount, CrewCount);
		const float TextW = HudTxtW(PillText, HudLabelFont(true), 15.f);
		const float CapW = HudTxtW(TEXT("TAB"), HudLabelFont(true), 13.f) + 16.f;
		const float PillW = 18.f + CapW + 12.f + TextW + 40.f, PillH = 48.f;
		const float PillX = HudVW - PillW + 20.f + 40.f * Open, PillY = HudVH * 0.5f - PillH * 0.5f;
		const bool bNudge = !bMeReady;
		HudGlow(bNudge ? SQUI::Soul : SQUI::Line, PillX, PillY, PillW, PillH, 24.f, bNudge ? 0.25f + 0.35f * HudPulse(HudNow, 4.f) : 0.f);
		HudPanel(PillX, PillY, PillW, PillH, 24.f, bNudge ? SQUI::WithAlpha(SQUI::Soul, 0.8f) : SQUI::Line);
		const float KW = HudKeyCap(TEXT("TAB"), PillX + 18.f, PillY + 11.f, 26.f);
		HudTxt(PillText, HudLabelFont(true), 15.f, PillX + 18.f + KW + 12.f, PillY + PillH * 0.5f - 0.62f * 15.f, SQUI::BoneWhite);
		HudAlpha = 1.f;
	}
	if (LobbyOpen <= 0.01f)
	{
		return;
	}

	// ---- input (keyboard / pad / mouse), only while the select is open
	const bool bGridLive = SQPC && bLobbyExpanded && LobbyOpen > 0.5f && NumReapers > 0;
	if (bGridLive)
	{
		auto AnyJust = [SQPC](std::initializer_list<FKey> KeyList)
		{
			for (const FKey& Each : KeyList)
			{
				if (SQPC->WasInputKeyJustPressed(Each))
				{
					return true;
				}
			}
			return false;
		};
		const int32 FocusBefore = LobbyFocus;
		if (AnyJust({ EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left }) && LobbyFocus > 0)
		{
			--LobbyFocus;
		}
		if (AnyJust({ EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right }) && LobbyFocus + 1 < NumReapers)
		{
			++LobbyFocus;
		}
		if (AnyJust({ EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up }) && LobbyFocus - Cols >= 0)
		{
			LobbyFocus -= Cols;
		}
		if (AnyJust({ EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down }))
		{
			if (LobbyFocus + Cols < NumReapers)
			{
				LobbyFocus += Cols;
			}
			else if (LobbyFocus / Cols < (NumReapers - 1) / Cols)
			{
				LobbyFocus = NumReapers - 1; // the last row is short
			}
		}
		if (LobbyFocus != FocusBefore)
		{
			LobbyHireArmed = -1;
			LobbyFocusAt = HudNow;
			bSnapRoster = true;
			Play(TEXT("Soul"), 0.15f, 0.f);
		}
		if (AnyJust({ EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom }))
		{
			LobbyPickReaper(LobbyFocus, D, SQPC);
		}
		if (SQPC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			LobbyRosterRow = FMath::Max(0, LobbyRosterRow - 1);
		}
		if (SQPC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			LobbyRosterRow = FMath::Min(FMath::Max(0, TotalRows - VisibleRows), LobbyRosterRow + 1);
		}
	}
	float LobbyMouseNowX = -1.f, LobbyMouseNowY = -1.f;
	bool bLobbyClick = false;
	if (bGridLive && SQPC->bShowMouseCursor)
	{
		float DesignX = 0.f, DesignY = 0.f;
		if (HudMouseDesign(DesignX, DesignY))
		{
			LobbyMouseNowX = DesignX;
			LobbyMouseNowY = DesignY;
			LobbyMouseX = LobbyMouseNowX;
			LobbyMouseY = LobbyMouseNowY;
		}
		bLobbyClick = SQPC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	}
	// keep the focused card's row on screen (keyboard moves); the wheel scrolls freely
	{
		const int32 FocusRow = FMath::Max(0, LobbyFocus) / Cols;
		if (bSnapRoster)
		{
			if (FocusRow < LobbyRosterRow)
			{
				LobbyRosterRow = FocusRow;
			}
			else if (FocusRow >= LobbyRosterRow + VisibleRows)
			{
				LobbyRosterRow = FocusRow - VisibleRows + 1;
			}
		}
		LobbyRosterRow = FMath::Clamp(LobbyRosterRow, 0, FMath::Max(0, TotalRows - VisibleRows));
	}

	// ---- backdrop: the lobby dims away behind the select
	HudAlpha = 1.f;
	HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.86f * Vis), 0.f, 0.f, HudVW, HudVH);
	HudAlpha = Vis;
	// a soft accent wash behind the stage
	for (int32 Wash = 0; Wash < 4; ++Wash)
	{
		const float Inset = 40.f + Wash * 50.f;
		HudRound(SQUI::WithAlpha(Accent, 0.025f), CentreX0 + Inset, StageY + Inset * 0.6f, CentreW - 2.f * Inset, StageH - Inset * 1.2f, 160.f);
	}

	// ---- top strip: title, ready count, coins, TAB
	{
		const float TitleX = Margin - SlideL;
		HudTxt(TEXT("CHOOSE YOUR REAPER"), HudHeadFont(), 34.f, TitleX, 26.f, SQUI::BoneWhite);
		const float TitleW = HudTxtW(TEXT("CHOOSE YOUR REAPER"), HudHeadFont(), 34.f);
		float ChipX = TitleX + TitleW + 20.f;
		const FString ReadyText = bCountdown ? FString::Printf(TEXT("STARTING IN %d"), FMath::CeilToInt(GS->PhaseTimeLeft))
			: FString::Printf(TEXT("%d / %d READY"), ReadyCount, CrewCount);
		ChipX += HudPill(ReadyText, ChipX, 36.f, bCountdown ? SQUI::Soul : SQUI::WithAlpha(SQUI::Line, 0.95f), bCountdown ? SQUI::Midnight : SQUI::BoneWhite, 13.f) + 10.f;
		const FString Coins = FString::Printf(TEXT("%d BANKED GOLD"), Save ? Save->OvertimePay : 0);
		const float CoinsW = HudTxtW(Coins, HudLabelFont(true), 13.f) + 46.f;
		HudRound(SQUI::WithAlpha(SQUI::Amber, 0.16f), ChipX, 36.f, CoinsW, 27.f, 13.5f);
		HudDisc(SQUI::Hex(0xB8, 0x7A, 0x1E), ChipX + 15.f, 49.5f, 8.f);
		HudDisc(SQUI::Amber, ChipX + 15.f, 49.5f, 6.f);
		HudTxt(Coins, HudLabelFont(true), 13.f, ChipX + 30.f, 49.5f - 0.62f * 13.f, SQUI::Amber, 0, false);

		const float RightEdge = HudVW - Margin + SlideR;
		const float HideW = HudTxtW(TEXT("HIDE TO SEE THE LOBBY"), HudLabelFont(true), 12.f);
		HudTxt(TEXT("HIDE TO SEE THE LOBBY"), HudLabelFont(true), 12.f, RightEdge, 41.f, SQUI::Muted, 2, false);
		const float TabW = HudTxtW(TEXT("TAB"), HudLabelFont(true), 12.f) + 16.f;
		HudKeyCap(TEXT("TAB"), RightEdge - HideW - 10.f - TabW, 35.f, 24.f);
		HudRect(SQUI::WithAlpha(SQUI::Line, 0.6f), Margin, TopH - 8.f, HudVW - 2.f * Margin, 1.f);
	}

	// ---- centre stage + the focused reaper's name
	DrawLobbyStage(FocusDef, CentreX0, StageY, CentreW, StageH, Accent);
	if (FocusDef)
	{
		const bool bFocusMine = FocusDef->Id == MyReaper;
		const bool bFocusLocked = !bUnlockAll && FocusDef->UnlockCost > 0 && (!Save || !Save->HasReaper(FocusDef->Id));
		const bool bFocusAfford = Save && Save->OvertimePay >= FocusDef->UnlockCost;
		const float NameIn = HudBackOut((HudNow - LobbyFocusAt) / 0.35f);
		const float NameFade = HudEaseOut((HudNow - LobbyFocusAt) / 0.2f);
		const float MidX = CentreX0 + CentreW * 0.5f;
		const FString FocusName = FocusDef->Name.ToUpper();
		const float NamePx = HudFitPx(FocusName, HudHeadFont(), 58.f, CentreW - 40.f) * (1.08f - 0.08f * NameIn);
		HudAlpha = Vis * NameFade;
		HudGlowTxt(FocusName, HudHeadFont(), NamePx, MidX, StageY + 6.f + 10.f * (1.f - NameIn), SQUI::BoneWhite, Accent, 1);
		// role + status chips
		const FString RoleText = FocusDef->Role.IsNone() ? FString(TEXT("REAPER")) : FocusDef->Role.ToString().ToUpper();
		FString StatusText;
		FLinearColor StatusFill = SQUI::WithAlpha(SQUI::Line, 0.95f), StatusInk = SQUI::BoneWhite;
		if (bFocusMine)
		{
			StatusText = TEXT("YOUR REAPER");
			StatusFill = SQUI::Soul;
			StatusInk = SQUI::Midnight;
		}
		else if (bFocusLocked)
		{
			StatusText = FString::Printf(TEXT("LOCKED  -  %d GOLD"), FocusDef->UnlockCost);
			StatusFill = bFocusAfford ? SQUI::WithAlpha(SQUI::Amber, 0.9f) : SQUI::WithAlpha(SQUI::Disabled, 0.6f);
			StatusInk = SQUI::Midnight;
		}
		else
		{
			StatusText = TEXT("HIRED");
		}
		const float ChipPx = 13.f;
		const float RoleW = HudTxtW(RoleText, HudLabelFont(true), ChipPx) + ChipPx * 1.6f;
		const float StatusW = HudTxtW(StatusText, HudLabelFont(true), ChipPx) + ChipPx * 1.6f;
		const float ChipsX = MidX - (RoleW + 10.f + StatusW) * 0.5f;
		const float ChipsY = StageY + 6.f + NamePx * 1.2f + 6.f;
		HudPill(RoleText, ChipsX, ChipsY, SQUI::WithAlpha(Accent, 0.9f), SQUI::Midnight, ChipPx);
		HudPill(StatusText, ChipsX + RoleW + 10.f, ChipsY, StatusFill, StatusInk, ChipPx);

		// the hire question / how to unlock, as a banner at the foot of the stage
		FString Banner;
		FLinearColor BannerCol = SQUI::Amber;
		if (LobbyHireArmed == LobbyFocus && bFocusLocked)
		{
			Banner = FString::Printf(TEXT("Hire %s for %d banked gold?  Press ENTER / click again to confirm."), *FocusDef->Name, FocusDef->UnlockCost);
		}
		else if (bFocusLocked && !bFocusAfford)
		{
			Banner = FocusDef->UnlockHint.IsEmpty() ? FString::Printf(TEXT("Needs %d banked gold. Every mission banks some gold."), FocusDef->UnlockCost)
				: FString::Printf(TEXT("%s  -  or %d banked gold"), *FocusDef->UnlockHint, FocusDef->UnlockCost);
			BannerCol = SQUI::Muted;
		}
		else if (bFocusLocked)
		{
			Banner = FString::Printf(TEXT("Press ENTER to hire for %d banked gold"), FocusDef->UnlockCost);
		}
		else if (!bFocusMine)
		{
			Banner = TEXT("Press ENTER or click the card to pick");
			BannerCol = SQUI::Soul;
		}
		if (!Banner.IsEmpty())
		{
			HudAlpha = Vis;
			const float BannerPx = 15.f;
			const float BannerW = FMath::Min(HudTxtW(Banner, HudLabelFont(true), BannerPx) + 40.f, CentreW - 20.f);
			const float BannerY = StageY + StageH - 44.f;
			HudRound(FLinearColor(0.f, 0.f, 0.f, 0.35f), MidX - BannerW * 0.5f + 3.f, BannerY + 5.f, BannerW, 34.f, 17.f);
			HudRound(SQUI::WithAlpha(BannerCol, 0.75f), MidX - BannerW * 0.5f - 1.5f, BannerY - 1.5f, BannerW + 3.f, 37.f, 18.5f);
			HudRound(SQUI::WithAlpha(SQUI::PanelDeep, 0.96f), MidX - BannerW * 0.5f, BannerY, BannerW, 34.f, 17.f);
			HudTxt(Banner, HudLabelFont(true), HudFitPx(Banner, HudLabelFont(true), BannerPx, BannerW - 30.f), MidX, BannerY + 17.f - 0.62f * BannerPx, BannerCol, 1, false);
		}
	}

	// ---- left: kit + stats
	HudAlpha = Vis;
	{
		const float LX = Margin - SlideL;
		const float KitBottom = DrawKitPanel(FocusDef, D, LX, TopH + 4.f, LeftW, Accent);
		DrawStatBars(FocusDef, D, LX, KitBottom + 16.f, LeftW, Accent);
	}

	// ---- right: mission, crew, READY
	const float RX = HudVW - Margin - RightW + SlideR;
	const float ReadyH = 74.f;
	const float ReadyY = BottomY - ReadyH;
	{
		HudAlpha = Vis;
		const float MissionBottom = DrawMissionPanel(GS, D, SQPC, RX, TopH + 4.f, RightW);
		DrawCrewPanel(GS, PS, D, RX, MissionBottom + 16.f, RightW, ReadyY - 18.f - (MissionBottom + 16.f));
	}

	// READY button
	{
		HudAlpha = Vis;
		const bool bOver = LobbyMouseNowX >= RX && LobbyMouseNowX <= RX + RightW && LobbyMouseNowY >= ReadyY && LobbyMouseNowY <= ReadyY + ReadyH;
		if (bOver && bLobbyClick && !bMeReady && SQPC && !SQPC->IsSettingsOpen())
		{
			bLobbyClick = false;
			LobbyReadyFlashAt = HudNow;
			Play(TEXT("Bank"), 0.6f, 0.f);
			SQPC->ServerReady();
		}
		const float Flash = FMath::Clamp(1.f - (HudNow - LobbyReadyFlashAt) / 0.4f, 0.f, 1.f);
		LobbyReadyLift = FMath::FInterpTo(LobbyReadyLift, (bOver && !bMeReady) ? 1.f : 0.f, HudDelta, 14.f);
		const float Pop = 1.f + 0.03f * LobbyReadyLift + 0.05f * Flash;
		const float BW = RightW * Pop, BH = ReadyH * Pop;
		const float BX = RX - (BW - RightW) * 0.5f, BY = ReadyY - (BH - ReadyH) * 0.5f - 3.f * LobbyReadyLift;
		if (!bMeReady)
		{
			HudGlow(SQUI::Soul, BX, BY, BW, BH, 26.f, 0.35f + 0.35f * HudPulse(HudNow, 3.5f) + 0.4f * LobbyReadyLift);
			HudRound(FLinearColor(0.f, 0.f, 0.f, 0.4f), BX + 4.f, BY + 8.f, BW, BH, 26.f);
			HudRound(SQUI::SoulDeep, BX, BY + 4.f, BW, BH, 26.f);
			HudRound(FMath::Lerp(SQUI::Soul, FLinearColor::White, 0.15f * LobbyReadyLift), BX, BY, BW, BH - 2.f, 26.f);
			HudRound(FLinearColor(1.f, 1.f, 1.f, 0.18f), BX + 6.f, BY + 4.f, BW - 12.f, BH * 0.36f, 20.f);
			HudShine(BX, BY, BW, BH, 3.2f, 4.f);
			const float CapW = HudTxtW(TEXT("R"), HudLabelFont(true), 14.f) + 16.f;
			const float LabelW = HudTxtW(TEXT("READY"), HudHeadFont(), 34.f);
			const float GroupX = BX + (BW - (CapW + 14.f + LabelW)) * 0.5f;
			HudKeyCap(TEXT("R"), GroupX, BY + (BH - 28.f) * 0.5f, 28.f);
			HudTxt(TEXT("READY"), HudHeadFont(), 34.f, GroupX + CapW + 14.f, BY + BH * 0.5f - 0.62f * 34.f, SQUI::Midnight, 0, false);
		}
		else
		{
			const int32 Missing = FMath::Max(0, CrewCount - ReadyCount);
			const FString Waiting = bCountdown ? FString::Printf(TEXT("STARTING IN %d"), FMath::CeilToInt(GS->PhaseTimeLeft))
				: (Missing > 0 ? FString::Printf(TEXT("WAITING FOR %d MORE"), Missing) : FString(TEXT("READY!")));
			HudRound(SQUI::Soul, BX - 2.f, BY - 2.f, BW + 4.f, BH + 4.f, 28.f);
			HudRound(SQUI::PanelDeep, BX, BY, BW, BH, 26.f);
			HudRound(SQUI::WithAlpha(SQUI::Soul, 0.08f + 0.06f * HudPulse(HudNow, 3.f)), BX + 4.f, BY + 4.f, BW - 8.f, BH - 8.f, 22.f);
			HudTxt(TEXT("YOU'RE READY"), HudLabelFont(true), 12.f, BX + BW * 0.5f, BY + 12.f, SQUI::Muted, 1, false);
			HudTxt(Waiting, HudHeadFont(), HudFitPx(Waiting, HudHeadFont(), 26.f, BW - 30.f), BX + BW * 0.5f, BY + 30.f, SQUI::Soul, 1);
		}
	}

	// ---- roster: portrait cards, two rows, scrolls when the roster is longer
	{
		HudAlpha = Vis;
		const float TotalW = Cols * CardW + (Cols - 1) * CardGap;
		const float RosterX = CentreX0 + (CentreW - TotalW) * 0.5f;
		const int32 FirstIdx = LobbyRosterRow * Cols;
		const int32 LastIdx = FMath::Min(NumReapers, FirstIdx + VisibleRows * Cols);
		// which teammates picked whom (little dots on the cards)
		TMap<FName, int32> PickedBy;
		for (const APlayerState* Other : GS->PlayerArray)
		{
			const ASQPlayerState* Mate = Cast<ASQPlayerState>(Other);
			if (Mate && Mate != PS)
			{
				PickedBy.FindOrAdd(Mate->ReaperId) += 1;
			}
		}
		// pass 1: hover / click. Hover only lifts the card a little; the showroom (focus) changes on a click or a key
		// (Nov 10 2026: sweeping the mouse across the grid used to swap the model and replay its entrance every card)
		const int32 HoverBefore = LobbyHover;
		LobbyHover = INDEX_NONE;
		for (int32 i = FirstIdx; i < LastIdx; ++i)
		{
			const int32 Slot = i - FirstIdx;
			const float TileX = RosterX + (Slot % Cols) * (CardW + CardGap);
			const float TileY = RosterY + (Slot / Cols) * (CardH + CardGap);
			const bool bOverTile = LobbyMouseNowX >= TileX && LobbyMouseNowX <= TileX + CardW && LobbyMouseNowY >= TileY && LobbyMouseNowY <= TileY + CardH;
			if (bOverTile) LobbyHover = i;
			if (bOverTile && bLobbyClick)
			{
				bLobbyClick = false;
				LobbyFocus = i;
				LobbyPickReaper(i, D, SQPC);
			}
		}
		if (LobbyHover != INDEX_NONE && LobbyHover != HoverBefore && LobbyHover != LobbyFocus) Play(TEXT("Soul"), 0.08f, 0.f);
		// pass 2: draw (unfocused first, the focused card last so its pop sits on top)
		for (int32 PassIdx = 0; PassIdx < 2; ++PassIdx)
		{
			for (int32 i = FirstIdx; i < LastIdx; ++i)
			{
				const bool bFocus = LobbyFocus == i;
				if ((PassIdx == 0) == bFocus)
				{
					continue;
				}
				const FSQReaperDef& RDef = D->Reapers[i];
				const int32 Slot = i - FirstIdx;
				const float TileX = RosterX + (Slot % Cols) * (CardW + CardGap);
				const float TileY = RosterY + (Slot / Cols) * (CardH + CardGap);
				const bool bMine = RDef.Id == MyReaper;
				const bool bLocked = !bUnlockAll && RDef.UnlockCost > 0 && (!Save || !Save->HasReaper(RDef.Id));
				const bool bCanHire = Save && Save->OvertimePay >= RDef.UnlockCost;
				const FLinearColor CardAccent = SQUI::RoleColour(RDef.Role, RDef.Color);
				float& Lift = LobbyTileLift[i];
				Lift = FMath::FInterpTo(Lift, bFocus ? 1.f : (LobbyHover == i ? 0.45f : 0.f), HudDelta, 16.f);
				const float Pop = 1.f + 0.1f * HudBackOut(Lift);
				const float PW = CardW * Pop, PH = CardH * Pop;
				const float PX = TileX - (PW - CardW) * 0.5f;
				const float PY = TileY - (PH - CardH) * 0.5f - 8.f * Lift;

				if (bFocus)
				{
					HudGlow(CardAccent, PX, PY, PW, PH, 14.f, 0.6f + 0.25f * HudPulse(HudNow, 4.f));
				}
				HudRound(FLinearColor(0.f, 0.f, 0.f, 0.38f), PX + 3.f, PY + 7.f + 4.f * Lift, PW, PH, 12.f);
				const FLinearColor Rim = bMine ? SQUI::Soul : (bFocus ? FMath::Lerp(CardAccent, FLinearColor::White, 0.35f) : SQUI::WithAlpha(SQUI::Line, 0.95f));
				const float RimW = (bMine || bFocus) ? 2.5f : 1.5f;
				HudRound(Rim, PX - RimW, PY - RimW, PW + 2.f * RimW, PH + 2.f * RimW, 12.f + RimW);
				HudRound(SQUI::PanelDeep, PX, PY, PW, PH, 12.f);
				const float PortH = PH - 24.f;
				HudPortrait(RDef.Id, D, PX + 4.f, PY + 4.f, PW - 8.f, PortH - 4.f, bLocked ? 1.f : 0.f);
				HudRect(SQUI::WithAlpha(CardAccent, bLocked ? 0.35f : 0.95f), PX + 4.f, PY + PortH, PW - 8.f, 2.5f);
				const FString CardName = RDef.Name.ToUpper();
				HudTxt(CardName, HudLabelFont(true), HudFitPx(CardName, HudLabelFont(true), 11.f, PW - 10.f), PX + PW * 0.5f, PY + PortH + 6.f,
					bLocked ? SQUI::Disabled : (bFocus ? SQUI::BoneWhite : SQUI::Muted), 1, false);
				if (bLocked)
				{
					HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.35f), PX + 4.f, PY + 4.f, PW - 8.f, PortH - 4.f);
					HudPadlock(PX + PW * 0.5f, PY + PortH * 0.42f, 20.f, bCanHire ? SQUI::Amber : SQUI::BoneWhite);
					const FString Price = FString::FromInt(RDef.UnlockCost);
					HudPill(Price, PX + PW * 0.5f, PY + PortH - 24.f, bCanHire ? SQUI::Amber : SQUI::WithAlpha(SQUI::Midnight, 0.85f),
						bCanHire ? SQUI::Midnight : SQUI::Muted, 10.f, 1);
				}
				if (i < 9)
				{
					HudDisc(SQUI::WithAlpha(SQUI::Midnight, 0.7f), PX + 12.f, PY + 12.f, 8.f);
					HudTxt(FString::FromInt(i + 1), HudLabelFont(true), 10.f, PX + 12.f, PY + 12.f - 0.62f * 10.f, SQUI::BoneWhite, 1, false);
				}
				if (bMine)
				{
					HudDisc(SQUI::Midnight, PX + PW - 12.f, PY + 12.f, 8.f);
					HudDisc(SQUI::Soul, PX + PW - 12.f, PY + 12.f, 5.5f);
				}
				if (const int32* Mates = PickedBy.Find(RDef.Id))
				{
					for (int32 Dot = 0; Dot < FMath::Min(*Mates, 3); ++Dot)
					{
						HudDisc(SQUI::Midnight, PX + PW - 12.f - Dot * 11.f, PY + PortH - 10.f, 5.5f);
						HudDisc(SQUI::BoneWhite, PX + PW - 12.f - Dot * 11.f, PY + PortH - 10.f, 3.5f);
					}
				}
				if (LobbyHireArmed == i)
				{
					HudAlpha = Vis * (0.5f + 0.5f * HudPulse(HudNow, 8.f));
					HudRound(SQUI::WithAlpha(SQUI::Amber, 0.25f), PX, PY, PW, PH, 12.f);
					HudAlpha = Vis;
				}
			}
		}
		// scroll hints
		if (TotalRows > VisibleRows)
		{
			const float ArrowX = RosterX + TotalW + 18.f;
			const FLinearColor UpCol = LobbyRosterRow > 0 ? SQUI::BoneWhite : SQUI::Disabled;
			const FLinearColor DownCol = LobbyRosterRow + VisibleRows < TotalRows ? SQUI::BoneWhite : SQUI::Disabled;
			HudArrow(ArrowX, RosterY + 8.f, -90.f, 12.f, UpCol);
			HudArrow(ArrowX, RosterY + RosterH - 8.f, 90.f, 12.f, DownCol);
			HudTxt(FString::Printf(TEXT("%d/%d"), LobbyRosterRow + 1, TotalRows - VisibleRows + 1), HudLabelFont(true), 10.f, ArrowX, RosterY + RosterH * 0.5f - 6.f,
				SQUI::Muted, 1, false);
		}
	}

	// ---- contract clauses pop-over (opened from the mission panel)
	if (bClausesOpen)
	{
		HudAlpha = Vis;
		DrawClausePanel(GS, SQPC ? SQPC->GetPlayerState<ASQPlayerState>() : nullptr, RX - 16.f - 400.f, TopH + 4.f);
	}
	HudAlpha = 1.f;
}

void ASQHUD::DrawLobbyStage(const FSQReaperDef* Def, float X, float Y, float W, float H, const FLinearColor& Accent)
{
	const float MidX = X + W * 0.5f;
	// a pool of light on the floor
	HudRound(SQUI::WithAlpha(Accent, 0.07f), MidX - W * 0.32f, Y + H * 0.8f, W * 0.64f, H * 0.1f, H * 0.05f);
	HudRound(SQUI::WithAlpha(Accent, 0.07f), MidX - W * 0.2f, Y + H * 0.82f, W * 0.4f, H * 0.06f, H * 0.03f);

	UTextureRenderTarget2D* StageTex = StageTarget.Get();
	const bool bLive = StageTex && StageStudio && !StageShown.IsNone();
	if (bLive)
	{
		// fill the stage, cropping the 4:3 capture to its aspect
		const float TexAspect = static_cast<float>(FMath::Max(1, StageTex->SizeX)) / static_cast<float>(FMath::Max(1, StageTex->SizeY));
		const float BoxAspect = W / FMath::Max(1.f, H);
		float U0 = 0.f, U1 = 1.f, V0 = 0.f, V1 = 1.f;
		if (BoxAspect > TexAspect)
		{
			const float Span = TexAspect / BoxAspect;
			V0 = (1.f - Span) * 0.4f;
			V1 = V0 + Span;
		}
		else
		{
			const float Span = BoxAspect / TexAspect;
			U0 = (1.f - Span) * 0.5f;
			U1 = U0 + Span;
		}
		const float SwapIn = HudEaseOut((HudNow - StageSwapAt) / 0.25f);
		HudTexture(StageTex, X, Y, W, H, FLinearColor(1.f, 1.f, 1.f, 0.35f + 0.65f * SwapIn), U0, V0, U1, V1);
		// feather every edge into the backdrop
		constexpr int32 Feather = 12;
		const float Step = 7.f;
		for (int32 Band = 0; Band < Feather; ++Band)
		{
			const float BandA = 0.85f * (1.f - static_cast<float>(Band) / Feather);
			const FLinearColor EdgeCol = SQUI::WithAlpha(SQUI::Midnight, BandA * 0.35f);
			HudRect(EdgeCol, X, Y + Band * Step, W, Step);
			HudRect(EdgeCol, X, Y + H - (Band + 1) * Step, W, Step);
			HudRect(EdgeCol, X + Band * Step, Y, Step, H);
			HudRect(EdgeCol, X + W - (Band + 1) * Step, Y, Step, H);
		}
	}
	else if (Def)
	{
		// no studio (or still starting): a big bobbing soul in the reaper's colour
		const float Bob = 8.f * FMath::Sin(HudNow * 2.f);
		HudSoulIcon(MidX, Y + H * 0.52f + Bob, FMath::Min(W, H) * 0.42f, FMath::Lerp(Def->Color, SQUI::BoneWhite, 0.15f), 1.f);
	}
	// swap flash: an accent ring bursts at the reaper's feet
	const float Burst = FMath::Clamp((HudNow - StageSwapAt) / 0.5f, 0.f, 1.f);
	if (Burst < 1.f)
	{
		const float BurstW = W * (0.2f + 0.5f * HudEaseOut(Burst));
		HudRound(SQUI::WithAlpha(Accent, 0.35f * (1.f - Burst)), MidX - BurstW * 0.5f, Y + H * 0.84f - 6.f, BurstW, 12.f, 6.f);
	}
	// a thin accent line along the stage floor
	HudRound(SQUI::WithAlpha(Accent, 0.55f), MidX - W * 0.3f, Y + H - 2.f, W * 0.6f, 2.f, 1.f);
}

float ASQHUD::DrawKitPanel(const FSQReaperDef* Def, const USQData* D, float X, float Y, float W, const FLinearColor& Accent)
{
	// one row per kit slot: weapon, passive, E, R (USQAbilityComponent::DescribeKit); CSV fallbacks when the kit is missing
	struct FSQHudKitRow
	{
		FString Name;
		FString Glyph;
		FString Desc;
		float Cooldown = 0.f;
		int32 UnlockLevel = 0;
		bool bEvolved = false;
	};
	FSQHudKitRow Rows[4];
	if (Def)
	{
		TArray<FSQAbilityView> Kit;
		USQAbilityComponent::DescribeKit(D, Def->Id, Kit);
		for (int32 SlotIdx = 0; SlotIdx < 4; ++SlotIdx)
		{
			if (Kit.IsValidIndex(SlotIdx))
			{
				Rows[SlotIdx].Name = HudStr(Kit[SlotIdx].Name);
				Rows[SlotIdx].Glyph = HudStr(Kit[SlotIdx].Glyph);
				Rows[SlotIdx].Desc = HudStr(Kit[SlotIdx].Description);
				Rows[SlotIdx].Cooldown = Kit[SlotIdx].CooldownMax;
				Rows[SlotIdx].UnlockLevel = Kit[SlotIdx].UnlockLevel;
				Rows[SlotIdx].bEvolved = Kit[SlotIdx].bEvolved;
			}
		}
		if (Rows[0].Name.IsEmpty())
		{
			if (const FSQUpgradeDef* Start = D->FindUpgrade(Def->StartWeapon))
			{
				Rows[0].Name = Start->Name;
				Rows[0].Glyph = Start->IconGlyph;
				Rows[0].Desc = Start->Description;
				Rows[0].Cooldown = Start->Cooldown;
			}
		}
		if (Rows[1].Name.IsEmpty())
		{
			Rows[1].Name = Def->Passive.IsNone() ? FString(TEXT("Passive")) : HudPretty(Def->Passive);
			Rows[1].Desc = Def->PassiveText;
		}
		if (Rows[2].Name.IsEmpty())
		{
			Rows[2].Name = TEXT("Ability");
			Rows[2].Desc = TEXT("This reaper's E ability.");
		}
		if (Rows[3].Name.IsEmpty())
		{
			Rows[3].Name = TEXT("Ultimate");
			Rows[3].Desc = TEXT("This reaper's R ultimate.");
		}
	}
	if (Rows[2].UnlockLevel <= 0)
	{
		Rows[2].UnlockLevel = 3;
	}
	if (Rows[3].UnlockLevel <= 0)
	{
		Rows[3].UnlockLevel = 6;
	}

	const float RowH = 86.f, HeadH = 46.f, Pad = 16.f;
	const float H = HeadH + 4.f * RowH + Pad;
	HudLayerPanel(X, Y, W, H, 20.f, Accent);
	HudTxt(TEXT("KIT"), HudHeadFont(), 20.f, X + Pad + 2.f, Y + 12.f, SQUI::BoneWhite);
	HudTxt(TEXT("AIM E / R WITH THE MOUSE"), HudLabelFont(true), 11.f, X + W - Pad, Y + 18.f, SQUI::Muted, 2, false);

	static const TCHAR* const SlotTags[4] = { TEXT("SIGNATURE WEAPON"), TEXT("PASSIVE"), TEXT("ABILITY"), TEXT("ULTIMATE") };
	static const TCHAR* const SlotKeys[4] = { TEXT("AUTO"), TEXT("P"), TEXT("E"), TEXT("R") };
	const FLinearColor SlotColours[4] = { SQUI::Amber, SQUI::Soul, Accent, FMath::Lerp(Accent, SQUI::Gold, 0.35f) };
	const float SlideIn = HudEaseOut((HudNow - LobbyFocusAt) / 0.35f);
	const float BaseAlpha = HudAlpha;
	for (int32 SlotIdx = 0; SlotIdx < 4; ++SlotIdx)
	{
		const FSQHudKitRow& Row = Rows[SlotIdx];
		const float RowIn = HudEaseOut((HudNow - LobbyFocusAt - 0.05f * SlotIdx) / 0.3f);
		HudAlpha = BaseAlpha * FMath::Max(0.25f, RowIn);
		const float RY = Y + HeadH + SlotIdx * RowH + 8.f * (1.f - SlideIn);
		const FLinearColor SlotCol = SlotColours[SlotIdx];
		HudRound(SQUI::WithAlpha(SQUI::PanelHover, 0.45f), X + 10.f, RY, W - 20.f, RowH - 8.f, 14.f);
		HudRound(SQUI::WithAlpha(SlotCol, 0.85f), X + 10.f, RY + 12.f, 3.f, RowH - 32.f, 1.5f);

		// icon tile
		const float IconS = 58.f;
		const float IX = X + 22.f, IY = RY + (RowH - 8.f - IconS) * 0.5f;
		HudRound(SQUI::WithAlpha(SlotCol, 0.9f), IX - 2.f, IY - 2.f, IconS + 4.f, IconS + 4.f, 14.f);
		HudRound(SQUI::Midnight, IX, IY, IconS, IconS, 12.f);
		HudRound(SQUI::WithAlpha(SlotCol, 0.16f), IX + 3.f, IY + 3.f, IconS - 6.f, IconS - 6.f, 10.f);
		if (!Row.Glyph.IsEmpty())
		{
			const float GlyphPx = HudFitPx(Row.Glyph, HudHeadFont(), 28.f, IconS - 14.f);
			HudTxt(Row.Glyph, HudHeadFont(), GlyphPx, IX + IconS * 0.5f, IY + IconS * 0.5f - 0.62f * GlyphPx, FMath::Lerp(SlotCol, SQUI::BoneWhite, 0.3f), 1);
		}
		else
		{
			HudGlyph(SlotIdx == 0 ? 0 : (SlotIdx == 1 ? 1 : 2), IX + IconS * 0.5f, IY + IconS * 0.5f, 30.f, FMath::Lerp(SlotCol, SQUI::BoneWhite, 0.2f));
		}
		// key badge
		const FString KeyText(SlotKeys[SlotIdx]);
		const float KeyW = HudTxtW(KeyText, HudLabelFont(true), 10.f) + 10.f;
		HudRound(SQUI::Midnight, IX + IconS - KeyW + 4.f, IY + IconS - 14.f, KeyW, 18.f, 6.f);
		HudRound(SQUI::WithAlpha(SlotCol, 0.95f), IX + IconS - KeyW + 5.f, IY + IconS - 13.f, KeyW - 2.f, 16.f, 5.f);
		HudTxt(KeyText, HudLabelFont(true), 10.f, IX + IconS - KeyW * 0.5f + 4.f, IY + IconS - 5.f - 0.62f * 10.f, SQUI::Midnight, 1, false);

		// text
		const float TX = IX + IconS + 14.f;
		const float TW = X + W - 18.f - TX;
		FString RightText;
		if (Row.Cooldown > 0.05f && SlotIdx != 1)
		{
			RightText = Row.Cooldown >= 10.f ? FString::Printf(TEXT("%.0fs"), Row.Cooldown) : FString::Printf(TEXT("%.1fs"), Row.Cooldown);
		}
		const float RightW = RightText.IsEmpty() ? 0.f : HudPill(RightText, X + W - 20.f, RY + 8.f, SQUI::WithAlpha(SQUI::Midnight, 0.8f), SQUI::Muted, 10.f, 2) + 8.f;
		const FString NameUpper = Row.Name.ToUpper();
		HudTxt(NameUpper, HudHeadFont(), HudFitPx(NameUpper, HudHeadFont(), 18.f, TW - RightW), TX, RY + 7.f, SQUI::BoneWhite);
		FString Tag = SlotTags[SlotIdx];
		if (SlotIdx >= 2)
		{
			Tag += FString::Printf(TEXT("  -  UNLOCKS AT LV %d"), Row.UnlockLevel);
		}
		if (Row.bEvolved)
		{
			Tag += TEXT("  -  EVOLVED");
		}
		HudTxt(Tag, HudLabelFont(true), HudFitPx(Tag, HudLabelFont(true), 10.f, TW), TX, RY + 31.f, SlotCol, 0, false);
		HudWrap(Row.Desc, HudLabelFont(false), 12.f, TX, RY + 46.f, TW, SQUI::Muted, 15.f, 2);
	}
	HudAlpha = BaseAlpha;
	return Y + H;
}

float ASQHUD::DrawStatBars(const FSQReaperDef* Def, const USQData* D, float X, float Y, float W, const FLinearColor& Accent)
{
	if (!Def || D->Reapers.Num() == 0)
	{
		return Y;
	}
	// each stat against the roster's range, as 10 segments
	auto StatOf = [](const FSQReaperDef& R, int32 Which) -> float
	{
		switch (Which)
		{
		case 0: return R.MaxHealth;
		case 1: return R.MoveSpeed;
		case 2: return R.Magnet;
		default: return R.DashCooldown > 0.01f ? 1.f / R.DashCooldown : 0.f;
		}
	};
	static const TCHAR* const StatNames[4] = { TEXT("HEALTH"), TEXT("SPEED"), TEXT("PICKUP"), TEXT("DASH") };
	const float RowH = 26.f, HeadH = 40.f, Pad = 16.f;
	const float H = HeadH + 4.f * RowH + 10.f;
	HudLayerPanel(X, Y, W, H, 18.f, Accent);
	HudTxt(TEXT("STATS"), HudHeadFont(), 18.f, X + Pad + 2.f, Y + 10.f, SQUI::BoneWhite);
	HudTxt(TEXT("VS. THE ROSTER"), HudLabelFont(true), 12.f, X + W - Pad, Y + 15.f, SQUI::Disabled, 2, false);
	for (int32 StatIdx = 0; StatIdx < 4; ++StatIdx)
	{
		float Lo = TNumericLimits<float>::Max(), Hi = -TNumericLimits<float>::Max();
		for (const FSQReaperDef& Each : D->Reapers)
		{
			Lo = FMath::Min(Lo, StatOf(Each, StatIdx));
			Hi = FMath::Max(Hi, StatOf(Each, StatIdx));
		}
		const float Mine = StatOf(*Def, StatIdx);
		const float Frac = Hi - Lo > 0.0001f ? 0.2f + 0.8f * (Mine - Lo) / (Hi - Lo) : 0.6f;
		const float RY = Y + HeadH + StatIdx * RowH;
		HudTxt(StatNames[StatIdx], HudLabelFont(true), 11.f, X + Pad + 2.f, RY + 4.f, SQUI::Muted, 0, false);
		const float BarX = X + 96.f, BarW = W - 96.f - Pad - 52.f;
		constexpr int32 Segs = 10;
		const float SegGap = 4.f;
		const float SegW = (BarW - (Segs - 1) * SegGap) / Segs;
		const float Shown = Frac * HudEaseOut((HudNow - LobbyFocusAt) / 0.45f);
		for (int32 Seg = 0; Seg < Segs; ++Seg)
		{
			const float SegFill = FMath::Clamp(Shown * Segs - Seg, 0.f, 1.f);
			HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.9f), BarX + Seg * (SegW + SegGap), RY + 4.f, SegW, 12.f, 3.f);
			if (SegFill > 0.01f)
			{
				HudRound(SQUI::WithAlpha(FMath::Lerp(Accent, SQUI::BoneWhite, 0.12f), SegFill), BarX + Seg * (SegW + SegGap), RY + 4.f, SegW, 12.f, 3.f);
			}
		}
		FString ValueText;
		switch (StatIdx)
		{
		case 0: ValueText = FString::FromInt(FMath::RoundToInt(Def->MaxHealth)); break;
		case 1: ValueText = FString::FromInt(FMath::RoundToInt(Def->MoveSpeed)); break;
		case 2: ValueText = FString::FromInt(FMath::RoundToInt(Def->Magnet)); break;
		default: ValueText = FString::Printf(TEXT("%.1fs"), Def->DashCooldown); break;
		}
		HudTxt(ValueText, HudLabelFont(true), 12.f, X + W - Pad, RY + 3.f, SQUI::BoneWhite, 2, false);
	}
	return Y + H;
}

float ASQHUD::DrawMissionPanel(const ASQGameState* GS, const USQData* D, ASQPlayerController* SQPC, float X, float Y, float W)
{
	// The host's mission (picked on the main menu MISSIONS page), from the mission list; biome name as a fallback
	FString MissionName, MissionDesc;
	int32 Difficulty = 0;
	float BestTime = -1.f;
	{
		TArray<FSQMissionView> Missions;
		USQMissions::GetMissionList(D, SQPC ? SQPC->GetProfile() : nullptr, Missions);
		for (const FSQMissionView& Each : Missions)
		{
			if (Each.Id == GS->MissionId)
			{
				MissionName = HudStr(Each.Name);
				MissionDesc = HudStr(Each.Description);
				Difficulty = Each.Difficulty;
				BestTime = Each.BestSeconds;
				break;
			}
		}
	}
	if (MissionName.IsEmpty())
	{
		MissionName = GS->MissionId.IsNone() ? FString() : HudPretty(GS->MissionId);
	}
	if (MissionName.IsEmpty())
	{
		const FSQBiomeDef* Biome = GS->Arena ? D->FindBiome(GS->Arena->BiomeId) : nullptr;
		MissionName = Biome ? Biome->Name : FString(TEXT("Mission"));
		MissionDesc = Biome ? Biome->Description : FString();
	}
	const bool bHard = GS->bHardMode;
	const bool bHost = GetNetMode() != NM_Client;
	const FLinearColor Edge = bHard ? SQUI::Danger : SQUI::Soul;

	const float Pad = 18.f;
	const int32 DescLines = MissionDesc.IsEmpty() ? 0 : HudWrap(MissionDesc, HudLabelFont(false), 13.f, 0.f, 0.f, W - 2.f * Pad, SQUI::Muted, 17.f, 2, false);
	const float ClauseRowH = SQQuests::Modifiers().Num() > 0 ? 38.f : 0.f;
	const float H = 44.f + 34.f + DescLines * 17.f + 12.f + 30.f + ClauseRowH + 14.f;
	HudLayerPanel(X, Y, W, H, 20.f, Edge);
	HudTxt(TEXT("MISSION"), HudLabelFont(true), 11.f, X + Pad, Y + 16.f, SQUI::Muted, 0, false);
	HudTxt(bHost ? TEXT("B / CLICK: NEXT MISSION   O: HARD") : TEXT("THE HOST PICKS"), HudLabelFont(true), 12.f, X + W - Pad, Y + 17.f, bHost ? SQUI::Muted : SQUI::Disabled, 2, false);
	// Host: click the mission name = B (next unlocked mission), click the NORMAL / HARD chip = O (same controller lobby commands)
	float HostMX = -1.f, HostMY = -1.f;
	const bool bHostMouse = bHost && SQPC && SQPC->bShowMouseCursor && !SQPC->IsSettingsOpen() && HudMouseDesign(HostMX, HostMY);
	const bool bHostClick = bHostMouse && SQPC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bOverName = bHostMouse && HostMX >= X + 10.f && HostMX <= X + W - 10.f && HostMY >= Y + 32.f && HostMY <= Y + 72.f;
	if (bOverName)
	{
		HudRound(SQUI::WithAlpha(SQUI::PanelHover, 0.7f), X + 10.f, Y + 32.f, W - 20.f, 40.f, 10.f);
		if (bHostClick)
		{
			SQPC->ServerLobbyCommand(TEXT("Biome"));
			Play(TEXT("Soul"), 0.25f, 0.f);
		}
	}
	const FString NameUpper = MissionName.ToUpper();
	HudTxt(NameUpper, HudHeadFont(), HudFitPx(NameUpper, HudHeadFont(), 24.f, W - 2.f * Pad), X + Pad, Y + 36.f, SQUI::BoneWhite);
	float CurY = Y + 44.f + 34.f;
	if (DescLines > 0)
	{
		HudWrap(MissionDesc, HudLabelFont(false), 13.f, X + Pad, CurY, W - 2.f * Pad, SQUI::Muted, 17.f, 2);
		CurY += DescLines * 17.f;
	}
	CurY += 12.f;
	// difficulty pips, HARD MODE, best time
	float ChipX = X + Pad;
	if (Difficulty > 0)
	{
		for (int32 Pip = 0; Pip < FMath::Min(Difficulty, 5); ++Pip)
		{
			HudTilt(bHard ? SQUI::Danger : SQUI::Amber, ChipX + Pip * 16.f, CurY + 7.f, 10.f, 10.f, 45.f);
		}
		ChipX += FMath::Min(Difficulty, 5) * 16.f + 10.f;
	}
	const float ModeChipX = ChipX;
	const float ModeChipW = HudPill(bHard ? FString(TEXT("HARD MODE")) : FString(TEXT("NORMAL")), ChipX, CurY, bHard ? SQUI::Danger : SQUI::WithAlpha(SQUI::Line, 0.95f),
		bHard ? SQUI::Midnight : SQUI::BoneWhite, 11.f);
	ChipX += ModeChipW + 8.f;
	if (bHostClick && HostMX >= ModeChipX && HostMX <= ModeChipX + ModeChipW && HostMY >= CurY - 2.f && HostMY <= CurY + 26.f)
	{
		SQPC->ServerLobbyCommand(TEXT("Endless")); // O: Hard mode on / off (the game mode checks it is unlocked)
		Play(TEXT("Soul"), 0.25f, 0.f);
	}
	if (BestTime > 0.f)
	{
		HudTxt(FString::Printf(TEXT("BEST %s"), *HudTimeText(BestTime)), HudLabelFont(true), 11.f, X + W - Pad, CurY + 6.f, SQUI::Amber, 2, false);
	}
	CurY += 30.f;

	// contract clauses: a row that opens the pop-over
	if (ClauseRowH > 0.f)
	{
		int32 OnCount = 0;
		for (int32 ModIdx = 0; ModIdx < SQQuests::Modifiers().Num(); ++ModIdx)
		{
			OnCount += GS->HasModifier(ModIdx) ? 1 : 0;
		}
		float MX = -1.f, MY = -1.f;
		const bool bMouse = PlayerOwner && PlayerOwner->bShowMouseCursor && HudMouseDesign(MX, MY);
		const float RowX = X + 10.f, RowW = W - 20.f, RowY = CurY;
		const bool bOver = bMouse && MX >= RowX && MX <= RowX + RowW && MY >= RowY && MY <= RowY + 32.f;
		if (bOver && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			bClausesOpen = !bClausesOpen;
			Play(TEXT("Soul"), 0.25f, 0.f);
		}
		ClauseRowLift = FMath::FInterpTo(ClauseRowLift, bOver ? 1.f : 0.f, HudDelta, 14.f);
		HudRound(SQUI::WithAlpha(FMath::Lerp(SQUI::PanelHover, SQUI::Line, ClauseRowLift), 0.7f), RowX, RowY, RowW, 32.f, 10.f);
		HudTxt(TEXT("CONTRACT CLAUSES"), HudLabelFont(true), 12.f, RowX + 12.f, RowY + 9.f, SQUI::BoneWhite, 0, false);
		const FString ClauseState = FString::Printf(TEXT("%d ON  -  GOLD x%.2f"), OnCount, GS->CoinMultiplier());
		HudTxt(ClauseState, HudLabelFont(true), 11.f, RowX + RowW - 30.f, RowY + 10.f, OnCount > 0 ? SQUI::Amber : SQUI::Muted, 2, false);
		HudArrow(RowX + RowW - 12.f, RowY + 16.f, bClausesOpen ? 180.f : 0.f, 9.f, SQUI::Muted);
	}
	return Y + H;
}

float ASQHUD::DrawCrewPanel(const ASQGameState* GS, const ASQPlayerState* PS, const USQData* D, float X, float Y, float W, float MaxH)
{
	TArray<const ASQPlayerState*> Crew;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		if (const ASQPlayerState* Mate = Cast<ASQPlayerState>(Other))
		{
			Crew.Add(Mate);
		}
	}
	int32 ReadyCount = 0;
	for (const ASQPlayerState* Mate : Crew)
	{
		ReadyCount += (Mate->bShopReady || Mate->IsABot()) ? 1 : 0;
	}
	const float Pad = 16.f, HeadH = 42.f;
	const int32 Slots = FMath::Max(4, Crew.Num());
	const float RowH = FMath::Clamp((MaxH - HeadH - 10.f) / Slots, 34.f, 60.f);
	const float H = HeadH + Slots * RowH + 10.f;
	HudLayerPanel(X, Y, W, H, 20.f, SQUI::Line);
	HudTxt(TEXT("THE CREW"), HudHeadFont(), 18.f, X + Pad, Y + 11.f, SQUI::BoneWhite);
	HudTxt(FString::Printf(TEXT("%d/%d READY"), ReadyCount, Crew.Num()), HudLabelFont(true), 11.f, X + W - Pad, Y + 16.f,
		ReadyCount == Crew.Num() && Crew.Num() > 0 ? SQUI::Soul : SQUI::Muted, 2, false);
	for (int32 SlotIdx = 0; SlotIdx < Slots; ++SlotIdx)
	{
		const float RY = Y + HeadH + SlotIdx * RowH;
		const float PortS = RowH - 10.f;
		if (!Crew.IsValidIndex(SlotIdx))
		{
			// an open seat
			HudRound(SQUI::WithAlpha(SQUI::Line, 0.35f), X + 10.f, RY + 2.f, W - 20.f, RowH - 4.f, 12.f);
			HudRound(SQUI::WithAlpha(SQUI::PanelDeep, 0.9f), X + 11.5f, RY + 3.5f, W - 23.f, RowH - 7.f, 11.f);
			HudTxt(TEXT("OPEN SEAT"), HudLabelFont(true), 11.f, X + W * 0.5f, RY + RowH * 0.5f - 0.62f * 11.f, SQUI::Disabled, 1, false);
			continue;
		}
		const ASQPlayerState* Mate = Crew[SlotIdx];
		const FSQReaperDef* MateDef = D->FindReaper(Mate->ReaperId);
		const FLinearColor MateAccent = MateDef ? SQUI::RoleColour(MateDef->Role, MateDef->Color) : SQUI::Muted;
		const bool bMateReady = Mate->bShopReady || Mate->IsABot();
		const bool bMe = Mate == PS;
		HudRound(SQUI::WithAlpha(bMe ? SQUI::PanelHover : SQUI::Panel, 0.8f), X + 10.f, RY + 2.f, W - 20.f, RowH - 4.f, 12.f);
		if (bMateReady)
		{
			HudRound(SQUI::WithAlpha(SQUI::Soul, 0.85f), X + 10.f, RY + 10.f, 3.f, RowH - 20.f, 1.5f);
		}
		HudRound(MateAccent, X + 18.f - 1.5f, RY + 5.f - 1.5f, PortS + 3.f, PortS + 3.f, 9.f);
		HudPortrait(Mate->ReaperId, D, X + 18.f, RY + 5.f, PortS, PortS, 0.f);
		float TagW = 0.f;
		if (bMateReady)
		{
			TagW = HudPill(TEXT("READY"), X + W - 20.f, RY + RowH * 0.5f - 12.f, SQUI::Soul, SQUI::Midnight, 10.f, 2);
		}
		else
		{
			TagW = HudPill(TEXT("PICKING"), X + W - 20.f, RY + RowH * 0.5f - 12.f, SQUI::WithAlpha(SQUI::Line, 0.95f), SQUI::Muted, 10.f, 2);
		}
		const float TX = X + 18.f + PortS + 12.f;
		const float Room = X + W - 20.f - TagW - 10.f - TX;
		const FString MateName = bMe ? FString::Printf(TEXT("%s (YOU)"), *Mate->GetPlayerName()) : Mate->GetPlayerName();
		const float NamePx = HudFitPx(MateName, HudLabelFont(true), 14.f, Room);
		const bool bTwoLines = RowH >= 44.f;
		HudTxt(MateName, HudLabelFont(true), NamePx, TX, bTwoLines ? RY + RowH * 0.5f - 17.f : RY + RowH * 0.5f - 9.f, bMateReady ? SQUI::BoneWhite : SQUI::Muted, 0, false);
		if (bTwoLines)
		{
			const FString ReaperText = (MateDef ? MateDef->Name : Mate->ReaperId.ToString()).ToUpper();
			HudTxt(ReaperText, HudLabelFont(true), HudFitPx(ReaperText, HudLabelFont(true), 11.f, Room), TX, RY + RowH * 0.5f + 2.f, MateAccent, 0, false);
		}
	}
	return Y + H;
}

void ASQHUD::LobbyPickReaper(int32 Index, const USQData* D, ASQPlayerController* SQPC)
{
	if (!SQPC || !D || !D->Reapers.IsValidIndex(Index))
	{
		return;
	}
	const FSQReaperDef& PickDef = D->Reapers[Index];
	const USQSaveGame* Save = SQPC->GetProfile();
	const bool bUnlockAll = D->Tune(TEXT("UnlockAllReapers"), 0.f) > 0.5f;
	const bool bLocked = !bUnlockAll && PickDef.UnlockCost > 0 && (!Save || !Save->HasReaper(PickDef.Id));
	if (bLocked && Save && Save->OvertimePay >= PickDef.UnlockCost && LobbyHireArmed != Index)
	{
		// spending coins: ask once more (the second Enter / click hires through the controller's normal flow)
		LobbyHireArmed = Index;
		LobbyHireArmedAt = HudNow;
		Play(TEXT("Soul"), 0.3f, 0.f);
		return;
	}
	LobbyHireArmed = -1;
	// hired: picks; locked + affordable (confirmed): hires then picks; locked + too dear: the controller shows the "needs N coins" message
	SQPC->Choose(Index);
	Play(bLocked && !(Save && Save->OvertimePay >= PickDef.UnlockCost) ? TEXT("Hit") : TEXT("Soul"), 0.35f, 0.f);
}

void ASQHUD::DrawLobbyHints()
{
	const float BarH = 48.f, BarY = HudVH - BarH;
	const float X0 = 24.f;
	HudRect(SQUI::WithAlpha(SQUI::Bar, 0.9f), 0.f, BarY, HudVW, BarH);
	HudRect(SQUI::Line, 0.f, BarY, HudVW, 1.5f);
	const TCHAR* const Keys[] = { TEXT("ARROWS"), TEXT("ENTER"), TEXT("1-9"), TEXT("WHEEL"), TEXT("R"), TEXT("TAB"), TEXT("~") };
	const TCHAR* const Labels[] = { TEXT("BROWSE"), TEXT("PICK / HIRE"), TEXT("QUICK PICK"), TEXT("SCROLL ROSTER"), TEXT("READY"),
		TEXT("HIDE / SHOW"), TEXT("CONSOLE: SQAddBot ADDS A BOT") };
	// The last hint (console cheat) only for the host, and never in Shipping (cheats are host-only)
	int32 NumHints = static_cast<int32>(UE_ARRAY_COUNT(Keys));
#if UE_BUILD_SHIPPING
	NumHints -= 1;
#else
	if (GetNetMode() == NM_Client)
	{
		NumHints -= 1;
	}
#endif
	float HintX = X0;
	for (int32 i = 0; i < NumHints; ++i)
	{
		const FString KeyStr(Keys[i]);
		const FString LabelStr(Labels[i]);
		const float Need = HudTxtW(KeyStr, HudLabelFont(true), 12.f) + 16.f + 10.f + HudTxtW(LabelStr, HudLabelFont(true), 13.f);
		if (HintX + Need > HudVW - X0)
		{
			break;
		}
		HintX += HudKeyCap(KeyStr, HintX, BarY + 11.f, 24.f) + 10.f;
		HintX += HudTxt(LabelStr, HudLabelFont(true), 13.f, HintX, BarY + 15.f, SQUI::Muted, 0, false) + 24.f;
	}
	const FString Brand = TEXT("SOUL QUOTA");
	if (HintX + HudTxtW(Brand, HudLabelFont(true), 13.f) < HudVW - X0)
	{
		HudTxt(Brand, HudLabelFont(true), 13.f, HudVW - X0, BarY + 15.f, SQUI::Disabled, 2, false);
	}
}

// ============================================================ economy pass: gold, chests, bell, beats, quests, clauses

void ASQHUD::DrawGoldPanel(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, float X, float Y)
{
	const float W = 400.f, H = 46.f;
	if (PS->Gold > GoldSeen) GoldPulseAt = HudNow;
	if (PS->Gold < GoldSeen) GoldShown = static_cast<float>(PS->Gold); // spent: snap down
	GoldSeen = PS->Gold;
	GoldShown = FMath::FInterpTo(GoldShown, static_cast<float>(PS->Gold), HudDelta, 10.f);
	const int32 NextPrice = GS->ChestPriceFor(PS);
	const bool bAfford = PS->Gold >= NextPrice;
	const float Pulse = FMath::Clamp(1.f - (HudNow - GoldPulseAt) / 0.3f, 0.f, 1.f);

	HudPanel(X, Y, W, H, 16.f, bAfford ? SQUI::WithAlpha(SQUI::Amber, 0.7f) : SQUI::Line);
	// a coin: amber disc, darker rim, a shine
	const float CoinX = X + 26.f, CoinY = Y + H * 0.5f, CoinR = 12.f * (1.f + 0.2f * Pulse);
	HudDisc(SQUI::WithAlpha(SQUI::Amber, 0.18f + 0.3f * Pulse), CoinX, CoinY, CoinR + 6.f);
	HudDisc(SQUI::Hex(0xB8, 0x7A, 0x1E), CoinX, CoinY, CoinR);
	HudDisc(SQUI::Amber, CoinX, CoinY, CoinR - 2.5f);
	HudDisc(SQUI::WithAlpha(FLinearColor::White, 0.45f), CoinX - CoinR * 0.35f, CoinY - CoinR * 0.35f, CoinR * 0.25f);
	float TX = X + 48.f;
	TX += HudTxt(TEXT("GOLD"), HudLabelFont(true), 12.f, TX, Y + 16.f, SQUI::Muted, 0, false) + 8.f;
	HudTxt(FString::FromInt(FMath::RoundToInt(GoldShown)), HudHeadFont(), 22.f * (1.f + 0.15f * Pulse), TX, Y + 9.f, SQUI::Amber);
	// right: the next chest's price and the key chance
	const float Key = C ? ASQChest::KeyChance(C) : 0.f;
	FString Right = FString::Printf(TEXT("NEXT CHEST %d"), NextPrice);
	if (Key > 0.005f) Right += FString::Printf(TEXT("  \u00B7  KEY %d%%"), FMath::RoundToInt(Key * 100.f));
	HudTxt(Right, HudLabelFont(true), 13.f, X + W - 16.f, Y + 15.f, bAfford ? SQUI::Amber : SQUI::Muted, 2, false);
}

void ASQHUD::DrawWorldMarkers(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D)
{
	if (!Canvas || !C || !PS) return;
	const FVector Me = C->GetActorLocation();
	const float ChestRange = FMath::Max(50.f, D->Tune(TEXT("ChestUseRange"), 320.f));
	const int32 Price = GS->ChestPriceFor(PS);
	const bool bAction = GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;

	// ---- chest price tags (within 3500 cm) and the nearest one in reach
	const ASQChest* Nearest = nullptr;
	float NearestDist = ChestRange;
	for (TActorIterator<ASQChest> ChestIt(GetWorld()); ChestIt; ++ChestIt)
	{
		const ASQChest* Chest = *ChestIt;
		if (!Chest || Chest->bOpened) continue;
		const float Dist = FVector::Dist2D(Chest->GetActorLocation(), Me);
		if (Dist < NearestDist)
		{
			NearestDist = Dist;
			Nearest = Chest;
		}
		if (Dist > 3500.f) continue;
		const FVector Screen = Canvas->Project(Chest->GetActorLocation() + FVector(0.f, 0.f, 120.f));
		if (Screen.Z <= 0.f || Screen.X < 0.f || Screen.Y < 0.f || Screen.X > Canvas->ClipX || Screen.Y > Canvas->ClipY) continue;
		HudAlpha = FMath::Clamp((3500.f - Dist) / 800.f, 0.f, 1.f);
		const float SX = static_cast<float>(Screen.X) / UIScale, SY = static_cast<float>(Screen.Y) / UIScale;
		if (Chest->bFree)
		{
			HudPill(TEXT("FREE"), SX, SY - 14.f, SQUI::Amber, SQUI::Midnight, 13.f, 1);
		}
		else
		{
			const bool bAfford = PS->Gold >= Price;
			HudPill(FString::Printf(TEXT("%d G"), Price), SX, SY - 14.f, bAfford ? SQUI::Amber : SQUI::Panel, bAfford ? SQUI::Midnight : SQUI::Muted, 13.f, 1);
		}
		HudAlpha = 1.f;
	}

	// ---- the Appointment Bell: a tag over it, or an arrow on the screen edge pointing to it
	const ASQPortal* Bell = GS->Portal.Get();
	const bool bBellLive = Bell && !Bell->bRung && GS->Phase == ESQPhase::Night;
	if (bBellLive)
	{
		const FVector BellTop = Bell->GetActorLocation() + FVector(0.f, 0.f, 450.f);
		const FVector Screen = Canvas->Project(BellTop);
		const float Metres = FVector::Dist2D(Bell->GetActorLocation(), Me) / 100.f;
		const FLinearColor BellCol = Bell->bArmed ? (GS->bFinalSwarm ? SQUI::Danger : SQUI::Soul) : SQUI::Muted;
		const bool bOnScreen = Screen.Z > 0.f && Screen.X > 40.f && Screen.Y > 40.f && Screen.X < Canvas->ClipX - 40.f && Screen.Y < Canvas->ClipY - 40.f;
		if (bOnScreen)
		{
			const float SX = static_cast<float>(Screen.X) / UIScale, SY = static_cast<float>(Screen.Y) / UIScale;
			HudPill(FString::Printf(TEXT("APPOINTMENT BELL  %.0fm"), Metres), SX, SY - 16.f, BellCol, SQUI::Midnight, 12.f, 1);
		}
		else
		{
			// Direction from the screen centre (behind the camera: flipped), clamped to an inset rectangle
			FVector2D Dir(static_cast<float>(Screen.X) - Canvas->ClipX * 0.5f, static_cast<float>(Screen.Y) - Canvas->ClipY * 0.5f);
			if (Screen.Z <= 0.f) Dir = -Dir;
			if (Dir.IsNearlyZero()) Dir = FVector2D(0.f, -1.f);
			Dir.Normalize();
			const float HalfW = HudVW * 0.5f - 70.f, HalfH = HudVH * 0.5f - 90.f;
			const float Scale = FMath::Min(HalfW / FMath::Max(0.001f, FMath::Abs(static_cast<float>(Dir.X))), HalfH / FMath::Max(0.001f, FMath::Abs(static_cast<float>(Dir.Y))));
			const float EX = HudVW * 0.5f + static_cast<float>(Dir.X) * Scale, EY = HudVH * 0.5f + static_cast<float>(Dir.Y) * Scale;
			const float Pulse = Bell->bArmed ? HudPulse(HudNow, 5.f) : 0.f;
			HudDisc(SQUI::WithAlpha(BellCol, 0.2f + 0.25f * Pulse), EX, EY, 22.f + 4.f * Pulse);
			HudDisc(BellCol, EX, EY, 14.f);
			HudDisc(SQUI::Midnight, EX + static_cast<float>(Dir.X) * 6.f, EY + static_cast<float>(Dir.Y) * 6.f, 4.f);
			HudTxt(FString::Printf(TEXT("BELL %.0fm"), Metres), HudLabelFont(true), 12.f, EX, EY + 20.f, BellCol, 1, true);
		}
	}

	// ---- the E prompt (bottom centre) for whatever is in reach
	if (!bAction || C->bDowned) return;
	FString Action, Detail;
	FLinearColor Accent = SQUI::Amber;
	float Progress = -1.f;
	if (Nearest)
	{
		if (Nearest->bFree)
		{
			Action = TEXT("OPEN FREE CHEST");
			Detail = TEXT("an item, on the house");
		}
		else if (PS->Gold >= Price)
		{
			Action = FString::Printf(TEXT("OPEN CHEST  \u00B7  %d GOLD"), Price);
			Detail = TEXT("a random item (luck helps)");
		}
		else
		{
			Action = FString::Printf(TEXT("CHEST  \u00B7  NEED %d GOLD"), Price);
			Detail = FString::Printf(TEXT("you have %d"), PS->Gold);
			Accent = SQUI::Muted;
		}
	}
	else if (bBellLive && FVector::Dist2D(Bell->GetActorLocation(), Me) <= Bell->GetUseRange())
	{
		if (Bell->Ringer)
		{
			Action = TEXT("RINGING THE BELL...");
			Detail = TEXT("stay in the circle");
			Progress = Bell->RingProgress;
			Accent = SQUI::Danger;
		}
		else if (Bell->bArmed)
		{
			Action = TEXT("RING THE APPOINTMENT BELL");
			Detail = GS->bFinalSwarm ? FString(TEXT("ends the swarm's night: the boss rises")) : FString(TEXT("closes the quota now: unbanked souls are lost, the boss rises"));
			Accent = SQUI::Danger;
		}
		else
		{
			Action = TEXT("THE BELL ISN'T READY");
			const float OpenAt = FMath::Clamp(D->Tune(TEXT("PortalMinProgress"), 0.5f), 0.f, 1.f);
			Detail = FString::Printf(TEXT("ready in %d s"), FMath::Max(0, FMath::CeilToInt((OpenAt - GS->NightProgress()) * GS->PhaseDuration)));
			Accent = SQUI::Muted;
		}
	}
	BellPromptLift = FMath::FInterpTo(BellPromptLift, Action.IsEmpty() ? 0.f : 1.f, HudDelta, 14.f);
	if (Action.IsEmpty() || BellPromptLift < 0.02f) return;
	HudAlpha = BellPromptLift;
	const float ActionW = HudTxtW(Action, HudHeadFont(), 18.f);
	const float DetailW = HudTxtW(Detail, HudLabelFont(false), 13.f);
	const float PW = FMath::Max(ActionW + 70.f, DetailW + 70.f) + 20.f, PH = Progress >= 0.f ? 72.f : 60.f;
	const float PX = (HudVW - PW) * 0.5f, PY = HudVH * 0.70f + 12.f * (1.f - BellPromptLift);
	HudPanel(PX, PY, PW, PH, 16.f, SQUI::WithAlpha(Accent, 0.8f));
	if (Progress < 0.f && Accent != SQUI::Muted) HudKeyCap(TEXT("F"), PX + 16.f, PY + 10.f, 26.f);
	HudTxt(Action, HudHeadFont(), 18.f, PX + 56.f, PY + 9.f, Accent == SQUI::Muted ? SQUI::BoneWhite : Accent);
	HudTxt(Detail, HudLabelFont(false), 13.f, PX + 56.f, PY + 34.f, SQUI::Muted, 0, false);
	if (Progress >= 0.f) HudBar(PX + 16.f, PY + PH - 18.f, PW - 32.f, 8.f, Progress, 0.f, SQUI::Danger);
	HudAlpha = 1.f;
}

void ASQHUD::DrawBeatBanner(const ASQGameState* GS)
{
	if (GS->BeatText.IsEmpty() || GS->BeatKind == 0) return;
	const float Age = static_cast<float>(GS->GetServerWorldTimeSeconds()) - GS->BeatServerTime;
	const float Life = GS->BeatKind == 3 ? 4.5f : (GS->BeatKind == 5 ? 3.f : 3.5f);
	if (Age < 0.f || Age > Life) return;
	FString Title;
	FLinearColor Colour = SQUI::Amber;
	switch (GS->BeatKind)
	{
	case 1: Title = TEXT("MINIBOSS"); Colour = SQUI::Danger; break;
	case 2: Title = TEXT("SWARM!"); Colour = SQUI::Amber; break;
	case 3: Title = TEXT("FINAL SWARM"); Colour = SQUI::Danger; break;
	case 5: Title = TEXT("NEW OBJECTIVE"); Colour = SQUI::Gold; break;
	case 6: Title = TEXT("OBJECTIVE COMPLETE"); Colour = SQUI::Soul; break;
	case 7: Title = TEXT("OBJECTIVE FAILED"); Colour = SQUI::Stamp; break;
	default: Title = TEXT("THE BELL IS READY"); Colour = SQUI::Soul; break;
	}
	const float In = HudBackOut(Age / 0.35f);
	HudAlpha = FMath::Clamp(Age / 0.12f, 0.f, 1.f) * FMath::Clamp((Life - Age) / 0.6f, 0.f, 1.f);
	const float TitlePx = (GS->BeatKind == 3 ? 70.f : 54.f) * (1.2f - 0.2f * In);
	const float CY = HudVH * 0.4f;
	HudGlowTxt(Title, HudDisplayFont(), TitlePx, HudVW * 0.5f, CY - 0.62f * TitlePx, Colour, Colour, 1);
	const float SubMax = FMath::Min(HudVW - 200.f, 900.f);
	const float SubPx = HudFitPx(GS->BeatText, HudLabelFont(true), 18.f, SubMax);
	HudTxt(GS->BeatText, HudLabelFont(true), SubPx, HudVW * 0.5f, CY + 0.62f * TitlePx + 2.f, SQUI::BoneWhite, 1);
	HudAlpha = 1.f;
}

float ASQHUD::DrawMinibossBar(const ASQGameState* GS, float Y)
{
	if (GS->MinibossName.IsEmpty() || GS->MinibossHealthFrac <= 0.f) return 0.f;
	const float W = FMath::Clamp(HudVW - 2.f * (24.f + 400.f + 24.f), 320.f, 520.f), H = 52.f;
	const float X = (HudVW - W) * 0.5f;
	HudPanel(X, Y, W, H, 14.f, SQUI::WithAlpha(SQUI::Danger, 0.6f), 0.9f);
	HudPill(TEXT("MINIBOSS"), X + 14.f, Y + 7.f, SQUI::Danger, SQUI::Midnight, 10.f);
	const FString Name = GS->MinibossName.ToUpper();
	HudTxt(Name, HudHeadFont(), HudFitPx(Name, HudHeadFont(), 16.f, W - 140.f), X + 104.f, Y + 6.f, SQUI::BoneWhite);
	HudBar(X + 14.f, Y + 32.f, W - 28.f, 10.f, FMath::Clamp(GS->MinibossHealthFrac, 0.f, 1.f), 0.f, SQUI::Danger);
	return H + 8.f;
}

void ASQHUD::DrawLocalToasts(const ASQPlayerState* PS)
{
	if (!PS) return;
	const double Now = GetWorld()->GetRealTimeSeconds();
	// A sound for each new card (quests get the bank chime, chests the soul pop)
	if (PS->LocalToasts.Num() > 0 && PS->LocalToasts.Last().Time > LastToastTimeSeen)
	{
		const uint8 NewKind = PS->LocalToasts.Last().Kind;
		Play(NewKind == 1 || NewKind == 3 ? TEXT("Bank") : TEXT("Soul"), NewKind == 0 ? 0.3f : 0.8f, 0.f);
		LastToastTimeSeen = PS->LocalToasts.Last().Time;
	}
	const bool bLobby = GetWorld()->GetGameState<ASQGameState>() && GetWorld()->GetGameState<ASQGameState>()->Phase == ESQPhase::Waiting;
	const float W = 380.f, H = 62.f, Life = 6.f;
	const float X = bLobby ? 24.f : HudVW - 24.f - W;
	float Y = bLobby ? HudVH - 48.f - 16.f - H : HudVH - 70.f - H;
	int32 Shown = 0;
	for (int32 ToastIdx = PS->LocalToasts.Num() - 1; ToastIdx >= 0 && Shown < 4; --ToastIdx)
	{
		const FSQLocalToast& Toast = PS->LocalToasts[ToastIdx];
		const float Age = static_cast<float>(Now - Toast.Time);
		if (Age < 0.f || Age > Life) continue;
		FLinearColor Accent = SQUI::Muted;
		FString Tag;
		switch (Toast.Kind)
		{
		case 1: Accent = SQUI::Soul; Tag = TEXT("PERFORMANCE REVIEW"); break;
		case 2: Accent = SQ::RarityColor(Toast.Rarity); Tag = TEXT("CHEST"); break;
		case 3: Accent = SQUI::Amber; Tag = TEXT("BANKED GOLD"); break;
		default: Accent = SQUI::Danger; Tag = TEXT("NOTE"); break;
		}
		const float In = HudEaseOut(Age / 0.3f);
		HudAlpha = FMath::Clamp(Age / 0.1f, 0.f, 1.f) * FMath::Clamp((Life - Age) / 0.6f, 0.f, 1.f);
		const float TX = X + (bLobby ? -40.f : 40.f) * (1.f - In);
		if (Toast.Kind == 1) HudGlow(SQUI::Soul, TX, Y, W, H, 14.f, 0.4f * (1.f - FMath::Clamp(Age / 1.5f, 0.f, 1.f)));
		HudPanel(TX, Y, W, H, 14.f, SQUI::WithAlpha(Accent, 0.85f));
		HudRound(Accent, TX + 10.f, Y + 12.f, 4.f, H - 24.f, 2.f);
		HudTxt(Tag, HudLabelFont(true), 10.f, TX + 24.f, Y + 7.f, Accent, 0, false);
		HudTxt(Toast.Text, HudHeadFont(), HudFitPx(Toast.Text, HudHeadFont(), 15.f, W - 40.f), TX + 24.f, Y + 20.f, SQUI::BoneWhite);
		HudTxt(Toast.Sub, HudLabelFont(false), HudFitPx(Toast.Sub, HudLabelFont(false), 12.f, W - 40.f), TX + 24.f, Y + 41.f, SQUI::Muted, 0, false);
		HudAlpha = 1.f;
		Y -= H + 8.f;
		++Shown;
	}
}

void ASQHUD::DrawClausePanel(const ASQGameState* GS, ASQPlayerState* PS, float X, float Y)
{
	const TArray<FSQModifierDef>& Mods = SQQuests::Modifiers();
	if (Mods.Num() == 0) return;
	const bool bHost = GetNetMode() != NM_Client;
	const float W = 400.f, RowH = 28.f, HeadH = 58.f;
	const float H = HeadH + Mods.Num() * RowH + 44.f;
	if (Y + H > HudVH - 48.f - 12.f) return; // no room (tiny window): the host can still play without clauses
	const float Mult = GS->CoinMultiplier();
	HudPanel(X, Y, W, H, 18.f, GS->ModifierMask != 0 ? SQUI::WithAlpha(SQUI::Amber, 0.7f) : SQUI::Line);
	HudTxt(TEXT("CONTRACT CLAUSES"), HudHeadFont(), 18.f, X + 18.f, Y + 10.f, SQUI::BoneWhite);
	HudTxt(FString::Printf(TEXT("BANKED GOLD x%.2f"), Mult), HudLabelFont(true), 13.f, X + W - 18.f, Y + 14.f, Mult > 1.001f ? SQUI::Amber : SQUI::Muted, 2, false);
	HudTxt(bHost ? TEXT("Harder terms, more pay. Click to toggle.") : TEXT("Harder terms, more pay. The host picks."), HudLabelFont(false), 12.f, X + 18.f, Y + 35.f, SQUI::Muted, 0, false);

	float MX = -1.f, MY = -1.f;
	const bool bMouse = HudMouseDesign(MX, MY);
	const bool bClick = bHost && bMouse && PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	int32 Hovered = INDEX_NONE;
	for (int32 ModIdx = 0; ModIdx < Mods.Num(); ++ModIdx)
	{
		const FSQModifierDef& Mod = Mods[ModIdx];
		const float RY = Y + HeadH + ModIdx * RowH;
		const bool bOn = GS->HasModifier(ModIdx);
		const bool bOver = bMouse && MX >= X + 8.f && MX <= X + W - 8.f && MY >= RY && MY < RY + RowH;
		if (bOver)
		{
			Hovered = ModIdx;
			HudRound(SQUI::WithAlpha(SQUI::PanelHover, 0.9f), X + 8.f, RY, W - 16.f, RowH - 2.f, 8.f);
			if (bClick && PS)
			{
				PS->ServerToggleModifier(ModIdx);
				Play(TEXT("Soul"), 0.3f, 0.f);
			}
		}
		// a checkbox
		HudRound(bOn ? SQUI::Amber : SQUI::Line, X + 18.f, RY + 6.f, 16.f, 16.f, 4.f);
		if (!bOn) HudRound(SQUI::Panel, X + 20.f, RY + 8.f, 12.f, 12.f, 3.f);
		HudTxt(Mod.Name, HudLabelFont(true), 14.f, X + 44.f, RY + 5.f, bOn ? SQUI::BoneWhite : SQUI::Muted, 0, false);
		HudTxt(FString::Printf(TEXT("+%d%%"), FMath::RoundToInt((Mod.CoinMult - 1.f) * 100.f)), HudLabelFont(true), 13.f, X + W - 18.f, RY + 6.f, bOn ? SQUI::Amber : SQUI::Disabled, 2, false);
	}
	// The hovered (else the last switched-on) clause's fine print
	int32 Shown = Hovered;
	if (Shown == INDEX_NONE)
	{
		for (int32 ModIdx = Mods.Num() - 1; ModIdx >= 0 && Shown == INDEX_NONE; --ModIdx) Shown = GS->HasModifier(ModIdx) ? ModIdx : INDEX_NONE;
	}
	if (Mods.IsValidIndex(Shown))
	{
		HudWrap(Mods[Shown].Description, HudLabelFont(false), 12.f, X + 18.f, Y + H - 38.f, W - 36.f, SQUI::BoneWhite, 15.f, 2);
	}
}

// ============================================================ tutorial layer

void ASQHUD::FillPainter(FSQTutorialPainter& Painter)
{
	auto PickFont = [this](ESQTutFont InKind) -> UFont*
	{
		switch (InKind)
		{
		case ESQTutFont::Display: return HudDisplayFont();
		case ESQTutFont::Head: return HudHeadFont();
		case ESQTutFont::LabelBold: return HudLabelFont(true);
		default: return HudLabelFont(false);
		}
	};
	Painter.Round = [this](const FLinearColor& InColour, float InX, float InY, float InW, float InH, float InRadius) { HudRound(InColour, InX, InY, InW, InH, InRadius); };
	Painter.Tilt = [this](const FLinearColor& InColour, float InX, float InY, float InW, float InH, float InDeg) { HudTilt(InColour, InX, InY, InW, InH, InDeg); };
	Painter.Text = [this, PickFont](const FString& InStr, ESQTutFont InFont, float InPx, float InX, float InY, const FLinearColor& InColour, int32 InAlign)
	{
		return HudTxt(InStr, PickFont(InFont), InPx, InX, InY, InColour, InAlign, true);
	};
	Painter.TextWidth = [this, PickFont](const FString& InStr, ESQTutFont InFont, float InPx) { return HudTxtW(InStr, PickFont(InFont), InPx); };
	Painter.Sound = [this](int32 InKind) { Play(TEXT("Soul"), InKind == 2 ? 0.2f : (InKind == 0 ? 0.15f : 0.3f), 0.f); };
	Painter.ViewW = HudVW;
	Painter.ViewH = HudVH;
	Painter.Now = HudNow;
	Painter.Delta = HudDelta;
	Painter.bCursorVisible = PlayerOwner && PlayerOwner->bShowMouseCursor;
	float MouseDesignX = 0.f, MouseDesignY = 0.f;
	Painter.bHasMouse = Painter.bCursorVisible && HudMouseDesign(MouseDesignX, MouseDesignY);
	Painter.MouseX = MouseDesignX;
	Painter.MouseY = MouseDesignY;
	Painter.bClick = Painter.bCursorVisible && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	Painter.bMouseHeld = Painter.bCursorVisible && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
	Painter.BottomReserve = BottomReserve;
}

void ASQHUD::DrawTutorialLayer(bool bLobby)
{
	USQTutorial* Tut = USQTutorial::Get(this);
	if (!Tut || !PlayerOwner || !Canvas) return;
	if (!bLobby) Tut->WatchGame(PlayerOwner);

	FSQTutorialPainter Painter;
	FillPainter(Painter);
	// anchors for the Training Shift's pointer: the reaper, the nearest foe, the cards, the portrait ring, the first objective
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQCharacter* Me = Cast<ASQCharacter>(PlayerOwner->GetPawn());
	const ASQPlayerController* SQPC = Cast<ASQPlayerController>(PlayerOwner);
	if (Me)
	{
		const FVector ReaperScreen = Canvas->Project(Me->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		if (ReaperScreen.Z > 0.f)
		{
			Painter.ReaperX = static_cast<float>(ReaperScreen.X) / UIScale;
			Painter.ReaperY = static_cast<float>(ReaperScreen.Y) / UIScale;
		}
		FVector2D FoePos = FVector2D::ZeroVector;
		if (GS && GS->Horde && GS->Horde->FindNearestEnemyAnyMachine(Me->Pos2D(), 1600.f, FoePos))
		{
			const float FeetZ = static_cast<float>(Me->GetActorLocation().Z) - Me->GetSimpleCollisionHalfHeight();
			const FVector FoeScreen = Canvas->Project(FVector(FoePos.X, FoePos.Y, FeetZ + 50.f));
			if (FoeScreen.Z > 0.f)
			{
				Painter.FoeX = static_cast<float>(FoeScreen.X) / UIScale;
				Painter.FoeY = static_cast<float>(FoeScreen.Y) / UIScale;
			}
		}
	}
	if (SQPC && SQPC->IsPicking())
	{
		if (SQPC->IsSoloPick())
		{
			Painter.CardsX = HudVW * 0.5f;
			Painter.CardsY = HudVH * 0.5f - 60.f;
		}
		else if (StripAnchorX >= 0.f)
		{
			Painter.CardsX = StripAnchorX;
			Painter.CardsY = StripAnchorY;
		}
	}
	Painter.PortraitX = PortraitAnchorX;
	Painter.PortraitY = PortraitAnchorY;
	Painter.ObjectiveX = ObjectiveAnchorX;
	Painter.ObjectiveY = ObjectiveAnchorY;
	HudAlpha = 1.f;
	Tut->Draw(Painter, PlayerOwner, bLobby ? ESQTutView::Lobby : ESQTutView::Run);
	ClickZones.Append(Tut->GetClickZones());
	HudAlpha = 1.f;
}

// ============================================================ Nov 9 UI pass: click zones, reap reticle, settings overlay

bool ASQHUD::IsMouseOverClickable() const
{
	float MouseX = 0.f, MouseY = 0.f;
	if (!PlayerOwner || !PlayerOwner->bShowMouseCursor || !PlayerOwner->GetMousePosition(MouseX, MouseY)) return false;
	// GetMousePosition is whole-viewport pixels; the HUD's design space is this view's pixels / UIScale (single view: same origin)
	const float Scale = FMath::Max(UIScale, 0.01f);
	const float DX = MouseX / Scale, DY = MouseY / Scale;
	for (const FVector4f& Zone : ClickZones)
	{
		if (DX >= Zone.X && DX <= Zone.X + Zone.Z && DY >= Zone.Y && DY <= Zone.Y + Zone.W) return true;
	}
	return false;
}

void ASQHUD::HudLine(float X0, float Y0, float X1, float Y1, float Thick, const FLinearColor& Colour)
{
	const float DX = X1 - X0, DY = Y1 - Y0;
	const float Len = FMath::Sqrt(DX * DX + DY * DY);
	if (Len < 0.5f) return;
	HudTilt(Colour, (X0 + X1) * 0.5f - Len * 0.5f, (Y0 + Y1) * 0.5f - Thick * 0.5f, Len, Thick, FMath::RadiansToDegrees(FMath::Atan2(DY, DX)));
}

void ASQHUD::DrawReapReticle(const ASQGameState* GS, const ASQCharacter* C, ASQPlayerController* SQPC)
{
	// Chase camera (Nov 12 2026): no ground cone and no cursor ring (the reap cooldown is on the action bar). All that is
	// left at the centre of the screen is a small crosshair in MANUAL aim, and the reap streak counter just under it.
	if (!Canvas || !C || !SQPC || C->bDowned) return;
	if (SQPC->IsPicking() && SQPC->IsSoloPick()) return; // the frozen level-up screen
	if (SQPC->bShowMouseCursor) return;                   // cursor up (Alt, cards): no crosshair
	const float MidX = HudVW * 0.5f, MidY = HudVH * 0.5f;
	if (SQPC->HasManualAim())
	{
		HudArc(MidX, MidY, 9.f, 3.5f, 0.f, 1.f, SQUI::WithAlpha(SQUI::Midnight, 0.55f));
		HudArc(MidX, MidY, 9.f, 1.5f, 0.f, 1.f, SQUI::WithAlpha(SQUI::BoneWhite, 0.9f));
		HudDisc(SQUI::WithAlpha(SQUI::BoneWhite, 0.95f), MidX, MidY, 1.6f);
	}
	const USQReapComponent* ReapComp = C->Reap.Get();
	const int32 Streak = ReapComp ? ReapComp->GetStreak() : 0;
	if (Streak >= 2)
	{
		const float StreakFade = FMath::Clamp(ReapComp->GetStreakTimeLeft() / 0.4f, 0.f, 1.f);
		HudAlpha = StreakFade;
		const FString Count = FString::Printf(TEXT("x%d"), Streak);
		const float CountW = HudTxtW(Count, HudHeadFont(), 26.f);
		const float LabelW = HudTxtW(TEXT("REAP STREAK"), HudLabelFont(true), 15.f);
		const float StartX = MidX - (CountW + 6.f + LabelW) * 0.5f;
		const float StreakY = MidY + 120.f;
		HudTxt(Count, HudHeadFont(), 26.f, StartX, StreakY, SQUI::Amber, 0, true);
		HudTxt(TEXT("REAP STREAK"), HudLabelFont(true), 15.f, StartX + CountW + 6.f, StreakY + 10.f, SQUI::Amber, 0, true);
		HudAlpha = 1.f;
	}
	(void)GS;
}

void ASQHUD::DrawSettingsOverlay(ASQPlayerController* SQPC)
{
	if (!SQPC || !SQPC->IsSettingsOpen())
	{
		bSettingsWasOpen = false;
		return;
	}
	if (!bSettingsWasOpen)
	{
		bSettingsWasOpen = true;
		SettingsPanel.OpenedAt = -1.f; // the open animation plays again
		SettingsPanel.Drag = -1;
	}
	HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.62f), 0.f, 0.f, HudVW, HudVH);
	AddClickZone(0.f, 0.f, HudVW, HudVH); // nothing under it reacts to clicks

	FSQTutorialPainter Painter;
	FillPainter(Painter);
	Painter.bNavUp = SQPC->WasInputKeyJustPressed(EKeys::Up);
	Painter.bNavDown = SQPC->WasInputKeyJustPressed(EKeys::Down);
	Painter.bNavLeft = SQPC->WasInputKeyJustPressed(EKeys::Left);
	Painter.bNavRight = SQPC->WasInputKeyJustPressed(EKeys::Right);
	Painter.bConfirm = SQPC->WasInputKeyJustPressed(EKeys::Enter);
	Painter.bTabNext = SQPC->WasInputKeyJustPressed(EKeys::Tab);

	const float PanelX = (HudVW - FSQSettingsPanel::PanelW) * 0.5f;
	const float PanelY = FMath::Max(24.f, (HudVH - FSQSettingsPanel::PanelH()) * 0.5f);
	HudAlpha = 1.f;
	const FSQSettingsResult Result = SettingsPanel.Draw(Painter, GetWorld(), PanelX, PanelY, TEXT("P"), TEXT("open / close"),
		TEXT("Pauses in solo; in co-op it opens over the game."), TEXT("Changes apply live. Hold ALT in a fight to free the cursor."), true);
	HudAlpha = 1.f;
	const USQSaveGame* Save = USQSaveGame::Load();
	if (Result.bAudioChanged && Music && Save)
	{
		Music->SetVolumeMultiplier(FMath::Max(0.001f, FMath::Clamp(Save->MusicVolume, 0.f, 1.f)));
		MusicVolumeSeen = Save->MusicVolume;
	}
	if (Result.bAutoPickChanged && Save) SQPC->ServerSetAutoPick(Save->bAutoPickLevelUps);
	if (Result.bReplayTraining)
	{
		const ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
		USQTutorial* Tut = USQTutorial::Get(this);
		if (Tut && GS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss))
		{
			Tut->StartTraining(SQPC);
			SQPC->SetSettingsOpen(false);
			return;
		}
		if (Tut) Tut->ArmTrainingReplay();
		SQPC->ShowLocalMessage(TEXT("The how-to-play prompts show when the run starts."));
	}
	if (Result.bClose) SQPC->SetSettingsOpen(false);
}

// ============================================================ top-centre: mission / stage timer, boss bar

float ASQHUD::DrawTopCentre(const ASQGameState* GS, const USQData* D)
{
	const float Margin = 24.f;
	const float PillW = 280.f, PillH = 66.f;
	const float PX = (HudVW - PillW) * 0.5f, PY = Margin;
	const bool bNight = GS->Phase == ESQPhase::Night;
	const bool bBossPhase = GS->Phase == ESQPhase::Boss;
	const bool bShopPhase = GS->Phase == ESQPhase::Shop;
	const bool bStage = GS->StageDuration > 0.f && !bShopPhase;
	const float Beat = HudPulse(HudNow, 8.f);

	FString Label = GS->MissionId.IsNone() ? FString(TEXT("MISSION")) : HudPretty(GS->MissionId).ToUpper();
	FString Big;
	FString Small;
	float Frac = -1.f;
	FLinearColor BigCol = SQUI::BoneWhite;
	FLinearColor Edge = GS->bHardMode ? SQUI::WithAlpha(SQUI::Danger, 0.8f) : SQUI::Line;
	if (bShopPhase)
	{
		Label = TEXT("DEATH'S OFFICE");
		Big = FString::Printf(TEXT("%ds"), FMath::Max(0, FMath::CeilToInt(GS->PhaseTimeLeft)));
		Small = FString::Printf(TEXT("%d SOULS TO SPEND"), GS->ShopSouls);
	}
	else if (bStage)
	{
		// StageTime counts up to StageDuration
		const float Elapsed = FMath::Max(0.f, GS->StageTime);
		Frac = FMath::Clamp(Elapsed / GS->StageDuration, 0.f, 1.f);
		Big = HudTimeText(Elapsed);
		Small = FString::Printf(TEXT("OF %s"), *HudTimeText(GS->StageDuration));
		if (Frac > 0.85f)
		{
			BigCol = FMath::Lerp(SQUI::BoneWhite, SQUI::Danger, Beat);
		}
	}
	else if (bNight && GS->bFinalSwarm)
	{
		const int32 SwarmSecs = FMath::FloorToInt(GS->FinalSwarmSeconds);
		Big = FString::Printf(TEXT("+%d:%02d"), SwarmSecs / 60, SwarmSecs % 60);
		Small = TEXT("FINAL SWARM");
		BigCol = FMath::Lerp(SQUI::Danger, SQUI::BoneWhite, 0.3f * Beat);
		Edge = SQUI::Danger;
		Frac = 1.f;
	}
	else if (bNight)
	{
		// old night clock (fallback until the mission timer exists)
		Big = ClockText(GS->NightProgress());
		Small = GS->bEndless ? FString::Printf(TEXT("ENDLESS  -  NIGHT %d"), GS->NightIndex + 1) : FString::Printf(TEXT("NIGHT %d / 3"), GS->NightIndex + 1);
		Frac = FMath::Clamp(GS->NightProgress(), 0.f, 1.f);
		if (GS->bFinalCall)
		{
			BigCol = FMath::Lerp(SQUI::BoneWhite, SQUI::Danger, Beat);
		}
	}
	else if (bBossPhase)
	{
		Big = TEXT("BOSS");
		BigCol = SQUI::Danger;
		Edge = SQUI::Danger;
	}
	else
	{
		Big = FString::Printf(TEXT("%d"), GS->EnemyCount);
		Small = TEXT("FOES");
	}

	if (bBossPhase || GS->bFinalCall)
	{
		HudGlow(SQUI::Danger, PX, PY, PillW, PillH, 24.f, 0.25f + 0.25f * Beat);
	}
	HudLayerPanel(PX, PY, PillW, PillH, 22.f, Edge == SQUI::Line ? SQUI::Soul : Edge, 0.94f);
	HudTxt(Label, HudLabelFont(true), HudFitPx(Label, HudLabelFont(true), 11.f, PillW - 40.f), PX + PillW * 0.5f, PY + 8.f, SQUI::Muted, 1, false);
	const float BigPx = HudFitPx(Big, HudHeadFont(), 30.f, PillW - 40.f);
	HudTxt(Big, HudHeadFont(), BigPx, PX + PillW * 0.5f, PY + 22.f, BigCol, 1);
	if (!Small.IsEmpty())
	{
		const float BigW = HudTxtW(Big, HudHeadFont(), BigPx);
		HudTxt(Small, HudLabelFont(true), 12.f, PX + PillW * 0.5f + BigW * 0.5f + 8.f, PY + 36.f, SQUI::Disabled, 0, false);
	}
	if (Frac >= 0.f)
	{
		const float TrackX = PX + 26.f, TrackW = PillW - 52.f, TrackY = PY + PillH - 9.f;
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.9f), TrackX, TrackY, TrackW, 4.f, 2.f);
		HudRound(Frac > 0.85f ? SQUI::Danger : SQUI::Soul, TrackX, TrackY, FMath::Max(4.f, TrackW * Frac), 4.f, 2.f);
	}
	if (GS->bHardMode)
	{
		HudPill(TEXT("HARD"), PX + PillW + 10.f, PY + PillH * 0.5f - 12.f, SQUI::Danger, SQUI::Midnight, 11.f);
	}
	float Bottom = PY + PillH;
	if (GS->Boss)
	{
		Bottom = DrawBossBar(GS, D, Bottom + 10.f);
	}
	return Bottom;
}

float ASQHUD::DrawBossBar(const ASQGameState* GS, const USQData* D, float Y)
{
	const ASQBoss* Boss = GS->Boss;
	if (!Boss)
	{
		return Y;
	}
	const FSQBossDef* BossDef = D->FindBoss(Boss->BossId);
	const float BW = FMath::Clamp(HudVW * 0.42f, 440.f, 780.f);
	const float BX = (HudVW - BW) * 0.5f, BH = 92.f;
	const bool bShielded = Boss->IsShielded();
	HudGlow(SQUI::Danger, BX, Y, BW, BH, 20.f, 0.25f + 0.15f * HudPulse(HudNow, 2.f));
	HudLayerPanel(BX, Y, BW, BH, 20.f, SQUI::Danger, 0.95f);

	// skull emblem
	const float EmX = BX + 40.f, EmY = Y + BH * 0.5f;
	HudDisc(SQUI::WithAlpha(SQUI::Danger, 0.25f), EmX, EmY, 30.f);
	HudDisc(SQUI::Danger, EmX, EmY, 25.f);
	HudDisc(SQUI::BoneWhite, EmX, EmY - 3.f, 15.f);
	HudRound(SQUI::BoneWhite, EmX - 8.f, EmY + 6.f, 16.f, 9.f, 3.f);
	HudDisc(SQUI::Midnight, EmX - 5.5f, EmY - 3.f, 4.f);
	HudDisc(SQUI::Midnight, EmX + 5.5f, EmY - 3.f, 4.f);

	const float InX = BX + 80.f, InW = BW - 98.f;
	float DefW = 0.f;
	if (Boss->DefianceStacks > 0)
	{
		DefW = HudPill(FString::Printf(TEXT("DEFIANCE x%d"), Boss->DefianceStacks), InX + InW, Y + 10.f, SQUI::Danger, SQUI::Midnight, 11.f, 2) + 10.f;
	}
	const FString BossName = BossDef ? BossDef->Name.ToUpper() : FString(TEXT("THE BOSS"));
	const float NamePx = HudFitPx(BossName, HudHeadFont(), 22.f, InW - DefW);
	const float NameW = HudTxt(BossName, HudHeadFont(), NamePx, InX, Y + 9.f, SQUI::BoneWhite);
	if (BossDef && !BossDef->Title.IsEmpty() && NameW + 30.f < InW - DefW)
	{
		const FString TitleText = BossDef->Title;
		HudTxt(TitleText, HudLabelFont(false), HudFitPx(TitleText, HudLabelFont(false), 13.f, InW - DefW - NameW - 16.f), InX + NameW + 12.f, Y + 16.f, SQUI::Muted);
	}
	const float MaxBossHP = FMath::Max(1.f, Boss->MaxHealth);
	const float BarY = Y + 42.f, BarH = 22.f;
	HudBar(InX, BarY, InW, BarH, FMath::Max(0.f, BossShown) / MaxBossHP, BossGhost / MaxBossHP, bShielded ? SQUI::Shield : SQUI::Danger);
	// phase notches
	for (int32 Notch = 1; Notch < ASQBoss::NumPhases; ++Notch)
	{
		const float NotchFrac = 1.f - static_cast<float>(Notch) / static_cast<float>(ASQBoss::NumPhases);
		HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.9f), InX + InW * NotchFrac - 1.5f, BarY - 2.f, 3.f, BarH + 4.f);
	}
	const FString BossHP = FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(FMath::Max(0.f, BossShown)), FMath::RoundToInt(Boss->MaxHealth));
	HudTxt(BossHP, HudHeadFont(), 15.f, InX + InW * 0.5f, BarY + BarH * 0.5f - 0.62f * 15.f, SQUI::BoneWhite, 1);
	FString PhaseText = FString::Printf(TEXT("PHASE %d/%d"), FMath::Min(Boss->GetPhaseIndex() + 1, ASQBoss::NumPhases), ASQBoss::NumPhases);
	const float PhaseW = HudTxt(PhaseText, HudLabelFont(true), 11.f, InX, BarY + BarH + 6.f, SQUI::Danger, 0, false);
	const FString BossHint = Boss->GetMechanicHint();
	if (!BossHint.IsEmpty())
	{
		HudTxt(BossHint, HudLabelFont(true), HudFitPx(BossHint, HudLabelFont(true), 12.f, InW - PhaseW - 14.f), InX + PhaseW + 14.f, BarY + BarH + 5.f,
			bShielded ? SQUI::BoneWhite : SQUI::Muted, 0, false);
	}
	return Y + BH;
}

// ============================================================ bottom-centre: ability bar

void ASQHUD::DrawLoadoutChip(const FSQOwned* Own, const USQData* D, float X, float Y, float ChipS, bool bSignature)
{
	const float Radius = ChipS * 0.24f;
	if (!Own)
	{
		HudRound(SQUI::WithAlpha(SQUI::Line, 0.5f), X, Y, ChipS, ChipS, Radius);
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.9f), X + 1.5f, Y + 1.5f, ChipS - 3.f, ChipS - 3.f, Radius - 1.f);
		HudDisc(SQUI::WithAlpha(SQUI::Line, 0.7f), X + ChipS * 0.5f, Y + ChipS * 0.5f, 2.5f);
		return;
	}
	const FSQUpgradeDef* Def = D->FindUpgrade(Own->Id);
	const bool bEvo = Def && Def->Kind == ESQUpgradeKind::Evolution;
	const FLinearColor KindCol = bSignature ? SQUI::Amber : HudKindColour(Def);
	const FLinearColor UpColour = Def ? Def->Color : SQUI::Muted;
	const int32 MaxLv = Def ? FMath::Max(1, Def->LevelCap()) : 1;
	const bool bMaxed = Own->Level >= MaxLv && MaxLv > 1;
	if (bEvo || bSignature)
	{
		HudGlow(bEvo ? SQUI::Gold : SQUI::Amber, X, Y, ChipS, ChipS, Radius, bEvo ? 0.35f + 0.25f * HudPulse(HudNow, 3.f) : 0.18f);
	}
	HudRound(bEvo ? SQUI::Gold : FMath::Lerp(SQUI::Line, KindCol, bMaxed || bSignature ? 0.95f : 0.5f), X - 2.f, Y - 2.f, ChipS + 4.f, ChipS + 4.f, Radius + 2.f);
	HudRound(SQUI::PanelHover, X, Y, ChipS, ChipS, Radius);
	HudRound(SQUI::WithAlpha(UpColour, 0.22f), X + 3.f, Y + 3.f, ChipS - 6.f, ChipS * 0.55f, Radius - 2.f);
	const FString Glyph = (Def && !Def->IconGlyph.IsEmpty()) ? Def->IconGlyph : HudInitials(Def ? Def->Name : Own->Id.ToString());
	const float GlyphPx = HudFitPx(Glyph, HudHeadFont(), ChipS * 0.4f, ChipS - 12.f);
	HudTxt(Glyph, HudHeadFont(), GlyphPx, X + ChipS * 0.5f, Y + ChipS * 0.42f - 0.62f * GlyphPx, bEvo ? SQUI::Gold : SQUI::BoneWhite, 1);
	if (bEvo)
	{
		HudTxt(TEXT("EVO"), HudLabelFont(true), 9.f, X + ChipS * 0.5f, Y + ChipS - 14.f, SQUI::Gold, 1, false);
	}
	else if (MaxLv > 1)
	{
		const float PipGap = 1.5f;
		const float PipW = FMath::Min(6.f, (ChipS - 10.f) / MaxLv - PipGap);
		const float PipsW = MaxLv * (PipW + PipGap) - PipGap;
		const float PipX0 = X + (ChipS - PipsW) * 0.5f;
		for (int32 Pip = 0; Pip < MaxLv; ++Pip)
		{
			const bool bOn = Pip < Own->Level;
			HudRound(bOn ? (bMaxed ? SQUI::Soul : SQUI::Amber) : SQUI::Line, PipX0 + Pip * (PipW + PipGap), Y + ChipS - 9.f, PipW, 4.5f, 1.5f);
		}
	}
	if (bSignature)
	{
		HudPill(TEXT("SIG"), X + ChipS * 0.5f, Y - 10.f, SQUI::Amber, SQUI::Midnight, 8.f, 1);
	}
}

void ASQHUD::DrawActionSlot(const FString& KeyName, const FString& IconText, const FString& SubText, float X, float Y, float S, const FLinearColor& Accent,
	bool bReady, bool bLocked, float CooldownLeft01, int32 FlashSlot, bool bEvolved)
{
	const int32 Flash03 = FMath::Clamp(FlashSlot, 0, 3);
	if (bReady && !bAbilityWasReady[Flash03])
	{
		AbilityFlashAt[Flash03] = HudNow;
	}
	bAbilityWasReady[Flash03] = bReady;
	const float Flash = FMath::Clamp(1.f - (HudNow - AbilityFlashAt[Flash03]) / 0.6f, 0.f, 1.f);
	const USQSaveGame* SlotSave = USQSaveGame::Load();
	const bool bKeyHints = !SlotSave || SlotSave->bShowKeyHints;

	if (bReady)
	{
		HudGlow(Accent, X, Y, S, S, 10.f, 0.18f + 0.12f * HudPulse(HudNow, 3.f) + Flash);
	}
	HudRound(FLinearColor(0.f, 0.f, 0.f, 0.4f), X + 2.f, Y + 5.f, S, S, 10.f);
	HudRound(bReady ? Accent : SQUI::Line, X - 2.f, Y - 2.f, S + 4.f, S + 4.f, 12.f);
	HudRound(SQUI::PanelDeep, X, Y, S, S, 10.f);
	HudRound(SQUI::WithAlpha(Accent, bReady ? 0.14f : 0.05f), X + 3.f, Y + 3.f, S - 6.f, S * 0.48f, 8.f);

	// key letter (preview), or the ability icon with key hints off
	const FString Main = bKeyHints ? KeyName : IconText;
	const float MainPx = HudFitPx(Main, HudHeadFont(), S * 0.34f, S - 14.f);
	HudTxt(Main, HudHeadFont(), MainPx, X + S * 0.5f, Y + S * 0.42f - 0.62f * MainPx, bReady ? Accent : SQUI::Muted, 1);

	// cooldown / charge curtain from the top (shrinks as it comes back)
	const float Curtain = FMath::Clamp(CooldownLeft01, 0.f, 1.f);
	if (!bLocked && Curtain > 0.005f)
	{
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.55f), X, Y, S, FMath::Max(4.f, S * Curtain), FMath::Min(10.f, S * Curtain * 0.5f));
	}
	if (bLocked)
	{
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.6f), X, Y, S, S, 10.f);
		HudPadlock(X + S * 0.5f, Y + S * 0.36f, S * 0.34f, SQUI::Disabled);
	}
	if (bEvolved)
	{
		HudRound(SQUI::Gold, X - 4.f, Y - 4.f, S + 8.f, 2.f, 1.f);
	}
	const float SubPx = HudFitPx(SubText, HudLabelFont(true), 10.f, S - 8.f);
	HudTxt(SubText, HudLabelFont(true), SubPx, X + S * 0.5f, Y + S - 15.f, bReady ? SQUI::BoneWhite : SQUI::Muted, 1, false);
	if (Flash > 0.f)
	{
		HudRound(SQUI::WithAlpha(FLinearColor::White, 0.3f * Flash), X, Y, S, S, 10.f);
	}
}

float ASQHUD::DrawAbilityBar(const ASQGameState* GS, const ASQPlayerState* PS, const ASQCharacter* C, const USQData* D)
{
	const FSQReaperDef* RDef = D->FindReaper(C->ReaperId);
	const FLinearColor Accent = SQUI::RoleColour(RDef ? RDef->Role : FName(NAME_None), RDef ? RDef->Color : SQUI::Soul);

	// loadout: the signature weapon (the reaper's start weapon or its evolution) first, other weapons, tomes (items stack, no slots)
	const FSQOwned* Signature = nullptr;
	TArray<const FSQOwned*> WeaponList, TomeList;
	for (const FSQOwned& Own : C->Owned)
	{
		const FSQUpgradeDef* Def = D->FindUpgrade(Own.Id);
		if (Def && Def->IsItem())
		{
			continue;
		}
		if (Def && Def->Kind == ESQUpgradeKind::Relic)
		{
			if (!Def->IsFallback())
			{
				TomeList.Add(&Own);
			}
			continue;
		}
		const bool bIsSignature = (Def && Def->IsSignature()) || (RDef && !RDef->StartWeapon.IsNone() && (Own.Id == RDef->StartWeapon || (Def && Def->EvolvesFrom == RDef->StartWeapon)));
		if (!Signature && bIsSignature)
		{
			Signature = &Own;
			continue;
		}
		WeaponList.Add(&Own);
	}
	if (!Signature && WeaponList.Num() > 0)
	{
		Signature = WeaponList[0];
		WeaponList.RemoveAt(0);
	}

	// the kit (E = slot 2, R = slot 3), live when the ability component is there
	TArray<FSQAbilityView> Kit;
	USQAbilityComponent::DescribeKit(D, C->ReaperId, Kit);
	if (const USQAbilityComponent* Abilities = C->FindComponentByClass<USQAbilityComponent>())
	{
		// live views (cooldowns, unlocks) override the static description slot by slot
		for (uint8 SlotIdx = 0; SlotIdx < 4; ++SlotIdx)
		{
			FSQAbilityView Live;
			if (Abilities->GetAbilityView(SlotIdx, Live))
			{
				if (Kit.Num() <= SlotIdx)
				{
					Kit.SetNum(SlotIdx + 1);
				}
				Kit[SlotIdx] = Live;
			}
		}
	}
	FSQAbilityView FallbackE, FallbackR;
	FallbackE.UnlockLevel = 3;
	FallbackE.bUnlocked = GS->TeamLevel >= 3;
	FallbackR.UnlockLevel = 6;
	FallbackR.bUnlocked = GS->TeamLevel >= 6;
	const FSQAbilityView* ViewE = Kit.IsValidIndex(2) ? &Kit[2] : &FallbackE;
	const FSQAbilityView* ViewR = Kit.IsValidIndex(3) ? &Kit[3] : &FallbackR;

	// ---- layout (HUD preview: portrait ring | weapons + HP + XP | tomes | L E R SPC)
	const float SigS = 64.f, ChipS = 52.f, TomeS = 42.f, Gap = 7.f, SepW = 22.f, ActS = 64.f, ActGap = 8.f, RingR = 40.f;
	const int32 ExtraSlots = FMath::Clamp(FMath::Max(WeaponList.Num(), C->GetWeaponSlots()), 0, 8); // the signature has its own slot (CountWeapons skips it)
	const int32 TomeSlots = FMath::Clamp(FMath::Max(TomeList.Num(), C->GetTomeSlots()), 0, 8);
	const float WeaponsW = SigS + (ExtraSlots > 0 ? Gap + ExtraSlots * (ChipS + Gap) - Gap : 0.f);
	const float TomesW = TomeSlots > 0 ? TomeSlots * (TomeS + Gap) - Gap : 0.f;
	const float RingW = 2.f * RingR + 18.f;
	const float ActW = 4.f * ActS + 3.f * ActGap;
	const float PadX = 22.f;
	const float InnerW = RingW + WeaponsW + SepW + TomesW + SepW + ActW;
	const float W = InnerW + 2.f * PadX;
	const float H = 150.f;
	const float X = (HudVW - W) * 0.5f;
	const float Y = HudVH - 16.f - H;
	const float MaxHP = FMath::Max(1.f, C->Stats.MaxHealth);
	const float HPFrac = C->Health / MaxHP;
	const bool bLow = HPFrac < 0.3f && !C->bDowned;
	const float LowPulse = HudPulse(HudNow, 7.f);
	const USQSaveGame* BarSave = USQSaveGame::Load();
	const bool bKeyHints = !BarSave || BarSave->bShowKeyHints;
	USQAbilityComponent* KitComp = C->Abilities.Get();
	const float UltCharge = KitComp ? FMath::Clamp(KitComp->GetUltCharge01(), 0.f, 1.f) : 0.f;

	if (bLow)
	{
		HudGlow(SQUI::Danger, X, Y, W, H, 24.f, 0.3f + 0.5f * LowPulse);
	}
	HudLayerPanel(X, Y, W, H, 24.f, bLow ? FMath::Lerp(Accent, SQUI::Danger, LowPulse) : Accent, 0.93f);

	// ---- portrait ring: fills gold with the Ultimate charge (souls you reap), pulses when full
	{
		const float RingCX = X + PadX + RingR + 2.f, RingCY = Y + H * 0.5f;
		PortraitAnchorX = RingCX;
		PortraitAnchorY = RingCY;
		const bool bFull = UltCharge >= 0.999f;
		if (bFull && !bUltWasFull) UltFullAt = HudNow;
		bUltWasFull = bFull;
		if (bFull)
		{
			HudGlow(SQUI::Gold, RingCX - RingR, RingCY - RingR, 2.f * RingR, 2.f * RingR, RingR, 0.35f + 0.35f * HudPulse(HudNow, 4.f));
		}
		HudDisc(SQUI::Panel, RingCX, RingCY, RingR + 3.f);
		HudArc(RingCX, RingCY, RingR, 6.f, 0.f, 1.f, SQUI::Line);
		HudArc(RingCX, RingCY, RingR, 6.f, 0.f, UltCharge, SQUI::Gold);
		const float FullFlash = FMath::Clamp(1.f - (HudNow - UltFullAt) / 0.7f, 0.f, 1.f);
		if (FullFlash > 0.f) HudArc(RingCX, RingCY, RingR + 4.f + 14.f * (1.f - FullFlash), 3.f, 0.f, 1.f, SQUI::WithAlpha(SQUI::Gold, FullFlash));
		HudDisc(SQUI::Midnight, RingCX, RingCY, RingR - 5.f);
		const float PortS = 2.f * (RingR - 8.f) * 0.78f;
		HudRound(Accent, RingCX - PortS * 0.5f - 1.5f, RingCY - PortS * 0.5f - 1.5f, PortS + 3.f, PortS + 3.f, PortS * 0.32f);
		HudPortrait(C->ReaperId, D, RingCX - PortS * 0.5f, RingCY - PortS * 0.5f, PortS, PortS, C->bDowned ? 1.f : 0.f);
		// team level badge (bottom-right)
		const FString LevelText = FString::FromInt(GS->TeamLevel);
		const float BadgeX = RingCX + RingR * 0.72f, BadgeY = RingCY + RingR * 0.72f;
		HudDisc(SQUI::Midnight, BadgeX, BadgeY, 13.f);
		HudDisc(SQUI::Soul, BadgeX, BadgeY, 11.f);
		HudTxt(LevelText, HudHeadFont(), HudFitPx(LevelText, HudHeadFont(), 14.f, 18.f), BadgeX, BadgeY - 0.62f * 14.f, SQUI::Midnight, 1, false);
		if (bFull)
		{
			HudPill(TEXT("ULT READY"), RingCX, Y - 10.f, SQUI::Gold, SQUI::Midnight, 9.f, 1);
		}
	}

	// ---- top row: HP, XP + level
	const float ContentX = X + PadX + RingW;
	const float RowY = Y + 14.f;
	const float BarX = ContentX;
	const float BarW = X + W - PadX - BarX;
	const float HPH = 22.f;
	const FLinearColor HPColour = bLow ? FMath::Lerp(SQUI::Danger, SQUI::BoneWhite, 0.25f * LowPulse) : SQUI::Danger;
	HudBar(BarX, RowY, BarW, HPH, FMath::Max(0.f, HPShown) / MaxHP, HPGhost / MaxHP, HPColour);
	const FString HPText = C->bDowned ? FString(TEXT("DOWN")) : FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(FMath::Max(0.f, HPShown)), FMath::RoundToInt(C->Stats.MaxHealth));
	HudTxt(HPText, HudHeadFont(), 15.f, BarX + BarW * 0.5f, RowY + HPH * 0.5f - 0.62f * 15.f, SQUI::BoneWhite, 1);
	if (C->Pardons > 0)
	{
		HudPill(C->Pardons > 1 ? FString::Printf(TEXT("PARDON x%d"), C->Pardons) : FString(TEXT("PARDON")), BarX + BarW - 6.f, RowY - 1.f, SQUI::Soul, SQUI::Midnight, 9.f, 2);
	}
	const float XPY = RowY + HPH + 8.f, XPH = 9.f;
	const float XPFrac = GS->XPToNext > 0 ? FMath::Clamp(XPShown / GS->XPToNext, 0.f, 1.f) : 0.f;
	const bool bPending = PS && PS->PendingLevelUps > 0;
	if (bPending)
	{
		HudGlow(SQUI::Soul, BarX, XPY, BarW, XPH, XPH * 0.5f, 0.3f + 0.3f * HudPulse(HudNow, 5.f));
	}
	HudBar(BarX, XPY, BarW, XPH, XPFrac, 0.f, SQUI::Soul);
	HudTxt(FString::Printf(TEXT("LV %d"), GS->TeamLevel), HudLabelFont(true), 13.f, BarX, XPY + XPH + 2.f, SQUI::Soul, 0, true);
	HudTxt(FString::Printf(TEXT("%d / %d XP"), FMath::RoundToInt(XPShown), GS->XPToNext), HudLabelFont(true), 13.f, BarX + BarW, XPY + XPH + 2.f, SQUI::Muted, 2, true);

	// ---- slots row
	const float SlotsY = Y + 76.f;
	float CurX = ContentX;
	DrawLoadoutChip(Signature, D, CurX, SlotsY, SigS, true);
	CurX += SigS + Gap;
	for (int32 SlotIdx = 0; SlotIdx < ExtraSlots; ++SlotIdx)
	{
		DrawLoadoutChip(WeaponList.IsValidIndex(SlotIdx) ? WeaponList[SlotIdx] : nullptr, D, CurX, SlotsY + (SigS - ChipS) * 0.5f, ChipS, false);
		CurX += ChipS + Gap;
	}
	CurX += SepW - Gap;
	HudRect(SQUI::WithAlpha(SQUI::Line, 0.8f), CurX - SepW * 0.5f, SlotsY + 8.f, 1.5f, SigS - 16.f);
	for (int32 SlotIdx = 0; SlotIdx < TomeSlots; ++SlotIdx)
	{
		DrawLoadoutChip(TomeList.IsValidIndex(SlotIdx) ? TomeList[SlotIdx] : nullptr, D, CurX, SlotsY + (SigS - TomeS) * 0.5f, TomeS, false);
		CurX += TomeS + Gap;
	}
	CurX += SepW - Gap;
	HudRect(SQUI::WithAlpha(SQUI::Line, 0.8f), CurX - SepW * 0.5f, SlotsY + 8.f, 1.5f, SigS - 16.f);

	// ---- actions: L reap, E, R (charge %), SPC / RMB dash
	auto CdText = [](float Seconds) { return Seconds >= 1.f ? FString::Printf(TEXT("%ds"), FMath::CeilToInt(Seconds)) : FString::Printf(TEXT("%.1fs"), Seconds); };
	{
		USQReapComponent* ReapComp = C->Reap.Get();
		const float Cd = ReapComp ? FMath::Max(0.f, ReapComp->GetCooldownRemaining()) : 0.f;
		const float CdMax = ReapComp ? FMath::Max(0.01f, ReapComp->GetCooldownMax()) : 1.f;
		const int32 Streak = ReapComp ? ReapComp->GetStreak() : 0;
		const bool bReady = Cd <= 0.01f;
		const FString Sub = !bReady ? CdText(Cd) : (Streak >= 2 ? FString::Printf(TEXT("x%d"), Streak) : FString(TEXT("REAP")));
		DrawActionSlot(TEXT("L"), TEXT("RP"), Sub, CurX, SlotsY, ActS, SQUI::Soul, bReady, false, bReady ? 0.f : Cd / CdMax, 0, false);
		CurX += ActS + ActGap;
	}
	for (int32 AbilIdx = 0; AbilIdx < 2; ++AbilIdx)
	{
		const FSQAbilityView* View = AbilIdx == 0 ? ViewE : ViewR;
		const bool bIsR = AbilIdx == 1;
		const bool bUnlocked = View && View->bUnlocked;
		const float Cd = View ? FMath::Max(0.f, View->Cooldown) : 0.f;
		const float CdMax = View ? FMath::Max(0.01f, View->CooldownMax) : 1.f;
		const bool bCharging = bIsR && bUnlocked && Cd <= 0.01f && UltCharge < 0.999f;
		const bool bReady = bUnlocked && Cd <= 0.01f && !bCharging;
		FString Sub;
		if (!bUnlocked) Sub = FString::Printf(TEXT("LV %d"), View ? FMath::Max(1, View->UnlockLevel) : (bIsR ? 6 : 3));
		else if (Cd > 0.01f) Sub = CdText(Cd);
		else if (bCharging) Sub = FString::Printf(TEXT("%d%%"), FMath::FloorToInt(UltCharge * 100.f));
		else Sub = TEXT("READY");
		const float Curtain = !bUnlocked ? 0.f : (Cd > 0.01f ? Cd / CdMax : (bCharging ? 1.f - UltCharge : 0.f));
		const FString Glyph = View ? HudStr(View->Glyph) : FString();
		DrawActionSlot(bIsR ? TEXT("R") : TEXT("E"), Glyph.IsEmpty() ? (bIsR ? FString(TEXT("R")) : FString(TEXT("E"))) : Glyph, Sub, CurX, SlotsY, ActS,
			bIsR ? SQUI::Gold : SQUI::BoneWhite, bReady, !bUnlocked, Curtain, 1 + AbilIdx, View && View->bEvolved);
		CurX += ActS + ActGap;
	}
	{
		const bool bDashReady = C->DashCooldownLeft <= 0.f;
		const float DashLeft01 = bDashReady ? 0.f : (C->Stats.DashCooldown > 0.f ? C->DashCooldownLeft / C->Stats.DashCooldown : 1.f);
		DrawActionSlot(TEXT("SHF"), TEXT(">>"), bDashReady ? FString(TEXT("DASH")) : CdText(C->DashCooldownLeft), CurX, SlotsY, ActS, SQUI::Arcane,
			bDashReady, false, DashLeft01, 3, false);
		if (bKeyHints) HudTxt(TEXT("or R-CLICK"), HudLabelFont(true), 11.f, CurX + ActS * 0.5f, SlotsY + ActS + 2.f, SQUI::Disabled, 1, false);
	}

	// ---- owned augments: small gold diamonds above the bar's right end
	float TopUsed = H + 16.f;
	if (PS && PS->OwnedAugments.Num() > 0)
	{
		const float DiaS = 18.f, DiaGap = 30.f;
		const int32 Shown = FMath::Min(PS->OwnedAugments.Num(), 10);
		float DX = X + W - 20.f - (Shown - 1) * DiaGap;
		const float DY = Y - 22.f;
		HudTxt(TEXT("AUGMENTS"), HudLabelFont(true), 12.f, DX - 22.f, DY - 6.f, SQUI::Gold, 2, false);
		for (int32 AugIdx = 0; AugIdx < Shown; ++AugIdx)
		{
			const FSQAugmentDef* OwnedDef = USQAugments::FindAugment(PS->OwnedAugments[AugIdx]);
			const FString AugInit = HudInitials(OwnedDef && !OwnedDef->Name.IsEmpty() ? OwnedDef->Name : HudPretty(PS->OwnedAugments[AugIdx]));
			HudTilt(SQUI::GoldDeep, DX - DiaS * 0.5f - 2.f, DY - DiaS * 0.5f - 2.f, DiaS + 4.f, DiaS + 4.f, 45.f);
			HudTilt(SQUI::Gold, DX - DiaS * 0.5f, DY - DiaS * 0.5f, DiaS, DiaS, 45.f);
			HudTxt(AugInit, HudLabelFont(true), 9.f, DX, DY - 0.62f * 9.f, SQUI::Midnight, 1, false);
			DX += DiaGap;
		}
		TopUsed += 34.f;
	}
	return TopUsed;
}

// ============================================================ top-right: objectives

float ASQHUD::DrawObjectives(const ASQGameState* GS, const USQData* D)
{
	struct FSQHudObjRow
	{
		FString Title;
		FString Detail;
		float Progress = 0.f;
		float TimeLeft = -1.f;
		bool bActive = false;
		bool bDone = false;
		bool bFailed = false;
	};
	TArray<FSQHudObjRow> Rows;
	// The lantern trail row (replicated last) is drawn first: it is the thread through the whole stage
	static const FName TrailType(TEXT("LanternTrail"));
	for (int32 Pass = 0; Pass < 2; ++Pass)
	for (const auto& Obj : GS->Objectives)
	{
		if ((Obj.Type == TrailType) != (Pass == 0)) continue;
		FSQHudObjRow& Row = Rows.AddDefaulted_GetRef();
		Row.Title = HudStr(Obj.Title);
		Row.Detail = HudStr(Obj.Detail);
		Row.Progress = FMath::Clamp(static_cast<float>(Obj.Progress), 0.f, 1.f);
		Row.bActive = Obj.bActive;
		Row.bDone = Obj.bDone;
		Row.bFailed = Obj.bFailed;
		Row.TimeLeft = Obj.TimeLeft;
	}
	const bool bQuotaFallback = Rows.Num() == 0 && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss) && GS->Quota > 0;
	if (bQuotaFallback)
	{
		// no mission objectives yet: the old soul quota as one objective
		FSQHudObjRow& Row = Rows.AddDefaulted_GetRef();
		Row.Title = TEXT("Bank the soul quota");
		Row.Detail = FString::Printf(TEXT("%d / %d souls"), FMath::RoundToInt(QuotaShown), GS->Quota);
		Row.Progress = FMath::Clamp(QuotaShown / static_cast<float>(GS->Quota), 0.f, 1.f);
		Row.bActive = GS->Banked < GS->Quota;
		Row.bDone = GS->Banked >= GS->Quota;
	}
	const float Margin = 24.f, W = 360.f;
	const float X = HudVW - Margin - W, Y = Margin;
	if (Rows.Num() == 0)
	{
		return Y;
	}
	int32 DoneCount = 0;
	for (const FSQHudObjRow& Row : Rows)
	{
		DoneCount += Row.bDone ? 1 : 0;
	}
	if (ObjectivesDoneSeen >= 0 && DoneCount > ObjectivesDoneSeen)
	{
		ObjectiveFlashAt = HudNow;
		Play(TEXT("Bank"), 0.8f, 0.f);
	}
	ObjectivesDoneSeen = DoneCount;
	const float Flash = FMath::Clamp(1.f - (HudNow - ObjectiveFlashAt) / 0.8f, 0.f, 1.f);

	// heights: active rows show a progress bar, done / upcoming rows are one line
	const float HeadH = 44.f;
	float H = HeadH + 8.f;
	for (const FSQHudObjRow& Row : Rows)
	{
		H += (Row.bActive && !Row.bDone && !Row.bFailed) ? 60.f : 30.f;
	}
	if (Flash > 0.f)
	{
		HudGlow(SQUI::Soul, X, Y, W, H, 18.f, Flash);
	}
	HudLayerPanel(X, Y, W, H, 18.f, GS->bHardMode ? SQUI::Danger : SQUI::Soul, 0.9f);
	HudTxt(TEXT("OBJECTIVES"), HudHeadFont(), 18.f, X + 16.f, Y + 11.f, SQUI::BoneWhite);
	HudTxt(FString::Printf(TEXT("%d / %d"), DoneCount, Rows.Num()), HudLabelFont(true), 12.f, X + W - 16.f, Y + 16.f, DoneCount == Rows.Num() ? SQUI::Soul : SQUI::Muted, 2, false);
	float CurY = Y + HeadH;
	for (int32 RowIdx = 0; RowIdx < Rows.Num(); ++RowIdx)
	{
		const FSQHudObjRow& Row = Rows[RowIdx];
		const bool bLive = Row.bActive && !Row.bDone && !Row.bFailed;
		const float RowH = bLive ? 60.f : 30.f;
		const float IconCX = X + 26.f, IconCY = CurY + 14.f;
		if (Row.bFailed)
		{
			HudDisc(SQUI::Danger, IconCX, IconCY, 9.f);
			HudTilt(SQUI::Midnight, IconCX - 6.f, IconCY - 1.25f, 12.f, 2.5f, 45.f);
			HudTilt(SQUI::Midnight, IconCX - 6.f, IconCY - 1.25f, 12.f, 2.5f, -45.f);
			HudTxt(Row.Title, HudLabelFont(true), HudFitPx(Row.Title, HudLabelFont(true), 13.f, W - 120.f), X + 44.f, CurY + 6.f, SQUI::Disabled, 0, false);
			HudTxt(TEXT("FAILED"), HudLabelFont(true), 12.f, X + W - 16.f, CurY + 8.f, SQUI::Danger, 2, false);
		}
		else if (Row.bDone)
		{
			HudDisc(SQUI::Soul, IconCX, IconCY, 9.f);
			HudTilt(SQUI::Midnight, IconCX - 6.f, IconCY + 0.f, 6.f, 2.5f, 45.f);
			HudTilt(SQUI::Midnight, IconCX - 2.f, IconCY - 1.5f, 10.f, 2.5f, -50.f);
			HudTxt(Row.Title, HudLabelFont(true), HudFitPx(Row.Title, HudLabelFont(true), 13.f, W - 60.f), X + 44.f, CurY + 6.f, SQUI::Disabled, 0, false);
		}
		else if (bLive)
		{
			HudRound(SQUI::WithAlpha(SQUI::PanelHover, 0.55f), X + 8.f, CurY - 2.f, W - 16.f, RowH - 4.f, 12.f);
			HudDisc(SQUI::WithAlpha(SQUI::Amber, 0.25f + 0.2f * HudPulse(HudNow + RowIdx, 4.f)), IconCX, IconCY, 11.f);
			HudTilt(SQUI::Amber, IconCX - 5.f, IconCY - 5.f, 10.f, 10.f, 45.f);
			float TimerW = 0.f;
			if (Row.TimeLeft >= 0.f)
			{
				const bool bHurry = Row.TimeLeft < 15.f;
				TimerW = HudPill(HudTimeText(Row.TimeLeft), X + W - 16.f, CurY + 2.f, bHurry ? SQUI::Danger : SQUI::WithAlpha(SQUI::Midnight, 0.85f),
					bHurry ? SQUI::Midnight : SQUI::BoneWhite, 10.f, 2) + 6.f;
			}
			HudTxt(Row.Title, HudHeadFont(), HudFitPx(Row.Title, HudHeadFont(), 16.f, W - 64.f - TimerW), X + 44.f, CurY + 3.f, SQUI::BoneWhite);
			const float PBarX = X + 44.f, PBarW = W - 44.f - 70.f, PBarY = CurY + 30.f;
			HudBar(PBarX, PBarY, PBarW, 8.f, Row.Progress, 0.f, SQUI::Amber);
			const FString Right = Row.Detail.IsEmpty() ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Row.Progress * 100.f)) : Row.Detail;
			HudTxt(Right, HudLabelFont(true), HudFitPx(Right, HudLabelFont(true), 11.f, 60.f), X + W - 16.f, PBarY - 3.f, SQUI::Muted, 2, false);
			if (bQuotaFallback)
			{
				QuotaAnchorX = PBarX + PBarW * Row.Progress;
				QuotaAnchorY = PBarY + 10.f;
			}
		}
		else
		{
			HudDisc(SQUI::Line, IconCX, IconCY, 6.f);
			HudTxt(Row.Title, HudLabelFont(true), HudFitPx(Row.Title, HudLabelFont(true), 13.f, W - 60.f), X + 44.f, CurY + 6.f, SQUI::Muted, 0, false);
		}
		CurY += RowH;
	}
	return Y + H;
}

void ASQHUD::DrawObjectiveArrows(const ASQGameState* GS, const ASQCharacter* C)
{
	if (!Canvas || !C)
	{
		return;
	}
	const FVector Me = C->GetActorLocation();
	const float Left = 60.f, Right = HudVW - 60.f;
	const float Top = 120.f, Bottom = HudVH - FMath::Max(120.f, BottomReserve + 30.f);
	const float CentreX = HudVW * 0.5f, CentreY = (Top + Bottom) * 0.5f;
	int32 ArrowIdx = 0;
	for (const auto& Obj : GS->Objectives)
	{
		if (!Obj.bActive || Obj.bDone)
		{
			continue;
		}
		const FVector Target(Obj.WorldLocation);
		if (Target.IsNearlyZero())
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Target + FVector(0.0, 0.0, 120.0));
		const float SX = static_cast<float>(Screen.X) / UIScale, SY = static_cast<float>(Screen.Y) / UIScale;
		const float Metres = static_cast<float>(FVector::Dist2D(Target, Me)) / 100.f;
		const float Pulse = HudPulse(HudNow + ArrowIdx, 4.f);
		const bool bOnScreen = Screen.Z > 0.f && SX > Left && SX < Right && SY > Top && SY < Bottom;
		if (bOnScreen)
		{
			// a marker over it
			HudDisc(SQUI::WithAlpha(SQUI::Amber, 0.18f + 0.15f * Pulse), SX, SY, 18.f);
			HudTilt(SQUI::GoldDeep, SX - 9.f, SY - 9.f, 18.f, 18.f, 45.f);
			HudTilt(SQUI::Amber, SX - 7.f, SY - 7.f, 14.f, 14.f, 45.f);
			HudTxt(FString::Printf(TEXT("%.0fm"), Metres), HudLabelFont(true), 11.f, SX, SY + 14.f, SQUI::Amber, 1, true);
			if (ArrowIdx == 0)
			{
				ObjectiveAnchorX = SX;
				ObjectiveAnchorY = SY;
			}
		}
		else
		{
			FVector2D Dir(SX - CentreX, SY - CentreY);
			if (Screen.Z <= 0.f)
			{
				Dir = -Dir;
			}
			if (Dir.IsNearlyZero())
			{
				Dir = FVector2D(0.f, -1.f);
			}
			Dir.Normalize();
			const float DirX = static_cast<float>(Dir.X), DirY = static_cast<float>(Dir.Y);
			const float HalfW = (Right - Left) * 0.5f, HalfH = (Bottom - Top) * 0.5f;
			const float Reach = FMath::Min(HalfW / FMath::Max(0.001f, FMath::Abs(DirX)), HalfH / FMath::Max(0.001f, FMath::Abs(DirY)));
			const float EX = CentreX + DirX * Reach, EY = CentreY + DirY * Reach;
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(DirY, DirX));
			HudDisc(FLinearColor(0.f, 0.f, 0.f, 0.35f), EX + 2.f, EY + 4.f, 22.f);
			HudDisc(SQUI::WithAlpha(SQUI::Amber, 0.25f + 0.25f * Pulse), EX, EY, 22.f + 3.f * Pulse);
			HudDisc(SQUI::PanelDeep, EX, EY, 17.f);
			HudTilt(SQUI::Amber, EX - 5.f, EY - 5.f, 10.f, 10.f, 45.f);
			HudArrow(EX + DirX * 30.f, EY + DirY * 30.f, AngleDeg, 14.f, SQUI::Amber);
			HudTxt(FString::Printf(TEXT("%.0fm"), Metres), HudLabelFont(true), 11.f, EX, EY + 24.f, SQUI::Amber, 1, true);
			if (ArrowIdx == 0)
			{
				ObjectiveAnchorX = EX;
				ObjectiveAnchorY = EY;
			}
		}
		++ArrowIdx;
	}
}

// ============================================================ top-left: teammates

float ASQHUD::DrawTeamCards(const ASQGameState* GS, const ASQPlayerState* PS, const USQData* D, float X, float Y)
{
	const float W = 260.f, H = 58.f, Gap = 8.f;
	float CurY = Y;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		const ASQPlayerState* Mate = Cast<ASQPlayerState>(Other);
		if (!Mate || Mate == PS)
		{
			continue;
		}
		ASQCharacter* MateChar = Cast<ASQCharacter>(Mate->GetPawn());
		const FSQReaperDef* MateDef = D->FindReaper(Mate->ReaperId);
		const FLinearColor MateAccent = MateDef ? SQUI::RoleColour(MateDef->Role, MateDef->Color) : SQUI::Muted;
		const bool bDown = MateChar && MateChar->bDowned;
		const float DownPulse = bDown ? HudPulse(HudNow, 5.f) : 0.f;
		if (bDown)
		{
			HudGlow(SQUI::Danger, X, CurY, W, H, 14.f, 0.3f + 0.4f * DownPulse);
		}
		HudLayerPanel(X, CurY, W, H, 14.f, bDown ? SQUI::Danger : MateAccent, 0.9f);
		const float PortS = H - 14.f;
		HudRound(MateAccent, X + 7.f - 1.5f, CurY + 7.f - 1.5f, PortS + 3.f, PortS + 3.f, 9.f);
		HudPortrait(Mate->ReaperId, D, X + 7.f, CurY + 7.f, PortS, PortS, bDown ? 1.f : 0.f);
		const float TX = X + 7.f + PortS + 10.f, TW = X + W - 12.f - TX;
		const FString MateName = Mate->GetPlayerName();
		HudTxt(MateName, HudLabelFont(true), HudFitPx(MateName, HudLabelFont(true), 13.f, TW - 40.f), TX, CurY + 8.f, SQUI::BoneWhite, 0, false);
		HudTxt(FString::FromInt(Mate->Kills), HudLabelFont(true), 12.f, X + W - 12.f, CurY + 10.f, SQUI::Disabled, 2, false);
		if (MateChar && bDown)
		{
			// harvest revive (HUD preview): reap N near them; the bar fills as the crew reaps
			const float Need = static_cast<float>(MateChar->GetHarvestReviveNeed());
			const float Have = FMath::Clamp(static_cast<float>(MateChar->GetHarvestReviveProgress()), 0.f, FMath::Max(Need, 0.f));
			const int32 Left = FMath::Max(0, FMath::CeilToInt(Need - Have));
			const FString DownLine = Need > 0.f ? FString::Printf(TEXT("DOWN - reap %d near them"), Left) : FString(TEXT("DOWN"));
			HudTxt(DownLine, HudLabelFont(true), HudFitPx(DownLine, HudLabelFont(true), 11.f, TW), TX, CurY + 26.f, FMath::Lerp(SQUI::Danger, SQUI::BoneWhite, 0.3f * DownPulse), 0, false);
			HudBar(TX, CurY + 42.f, TW, 8.f, Need > 0.f ? Have / Need : 0.f, 0.f, SQUI::Soul);
		}
		else if (MateChar)
		{
			const float MaxHP = FMath::Max(1.f, MateChar->Stats.MaxHealth);
			HudBar(TX, CurY + 30.f, TW, 12.f, MateChar->Health / MaxHP, 0.f, SQUI::Danger);
			HudTxt(FString::Printf(TEXT("HP %d%%"), FMath::RoundToInt(100.f * MateChar->Health / MaxHP)), HudLabelFont(true), 9.f, TX + TW * 0.5f, CurY + 31.f, SQUI::BoneWhite, 1, false);
		}
		else
		{
			HudTxt(TEXT("NOT SPAWNED"), HudLabelFont(true), 12.f, TX, CurY + 30.f, SQUI::Disabled, 0, false);
		}
		CurY += H + Gap;
	}
	return CurY;
}

// ============================================================ AUGMENT offer (gold cards)

void ASQHUD::DrawAugmentOffer(const ASQGameState* GS, ASQPlayerState* PS, const USQData* D)
{
	const int32 NumOffers = FMath::Min(PS->AugmentOffers.Num(), 4);
	if (NumOffers <= 0)
	{
		AugmentSignature = 0;
		return;
	}
	uint32 Sig = static_cast<uint32>(NumOffers);
	for (int32 i = 0; i < NumOffers; ++i)
	{
		Sig = Sig * 31u + GetTypeHash(PS->AugmentOffers[i]);
	}
	if (Sig != AugmentSignature)
	{
		AugmentSignature = Sig;
		AugmentShownAt = HudNow;
		Play(TEXT("LevelUp"), 0.8f, 0.f);
	}
	const float Elapsed = HudNow - AugmentShownAt;

	// input: 1-3 / D-pad go through the controller (Choose -> ChooseOffer routes to the augment while bAugmentOffer); the HUD only
	// flashes the card. Clicks send ServerChooseAugment here.
	int32 KeyFlash = -1;
	if (PlayerOwner && Elapsed > 0.5f)
	{
		static const FKey AugKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
		static const FKey AugPad[] = { EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down };
		for (int32 KeyIdx = 0; KeyIdx < NumOffers; ++KeyIdx)
		{
			if (PlayerOwner->WasInputKeyJustPressed(AugKeys[KeyIdx]) || PlayerOwner->WasInputKeyJustPressed(AugPad[KeyIdx]))
			{
				KeyFlash = KeyIdx;
				break;
			}
		}
	}
	float MouseDX = -1.f, MouseDY = -1.f;
	if (PlayerOwner && PlayerOwner->bShowMouseCursor)
	{
		float MouseDesignX = 0.f, MouseDesignY = 0.f;
		if (HudMouseDesign(MouseDesignX, MouseDesignY))
		{
			MouseDX = MouseDesignX;
			MouseDY = MouseDesignY;
		}
	}
	const bool bClick = PlayerOwner && HudNow >= AugmentClickLockUntil && Elapsed > 0.5f && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	if (KeyFlash >= 0)
	{
		AugmentFlashIndex = KeyFlash;
		AugmentFlashAt = HudNow;
	}

	const float Gap = 30.f, CardH = 400.f;
	float CardW = 300.f;
	const float Avail = HudVW - 48.f;
	if (NumOffers * CardW + (NumOffers - 1) * Gap > Avail)
	{
		CardW = FMath::Max(200.f, (Avail - (NumOffers - 1) * Gap) / NumOffers);
	}
	const float TotalW = NumOffers * CardW + (NumOffers - 1) * Gap;
	const float X0 = (HudVW - TotalW) * 0.5f;
	const float CardY = FMath::Max(190.f, (HudVH - CardH) * 0.5f);
	const float MidX = HudVW * 0.5f;

	// header
	{
		const float TitleIn = HudBackOut(Elapsed / 0.45f);
		HudAlpha = HudEaseOut(Elapsed / 0.25f);
		const float TitlePx = 88.f * (1.2f - 0.2f * TitleIn);
		HudGlowTxt(TEXT("AUGMENT"), HudDisplayFont(), TitlePx, MidX, CardY - 108.f - 0.62f * TitlePx, SQUI::Gold, SQUI::Gold, 1);
		HudTxt(TEXT("PICK ONE  -  IT LASTS THE WHOLE RUN"), HudHeadFont(), 20.f, MidX, CardY - 52.f, SQUI::BoneWhite, 1);
		HudAlpha = 1.f;
	}

	for (int32 i = 0; i < NumOffers; ++i)
	{
		if (!PS->AugmentOffers.IsValidIndex(i))
		{
			break; // the server may clear the offers mid-loop (host)
		}
		const float Entrance = HudBackOut(FMath::Clamp((Elapsed - 0.1f * i) / 0.5f, 0.f, 1.f));
		const float CX = X0 + i * (CardW + Gap);
		const bool bHover = Elapsed > 0.5f && MouseDX >= CX && MouseDX <= CX + CardW && MouseDY >= CardY && MouseDY <= CardY + CardH;
		if (bHover && bClick)
		{
			AugmentFlashIndex = i;
			AugmentFlashAt = HudNow;
			AugmentClickLockUntil = HudNow + 0.4f;
			Play(TEXT("Bank"), 0.7f, 0.f);
			PS->ServerChooseAugment(i);
			break; // the offers will change: redraw next frame
		}
		float& Lift = AugmentLift[i];
		Lift = FMath::FInterpTo(Lift, bHover ? 1.f : 0.f, HudDelta, 14.f);
		const float CY = CardY + 100.f * (1.f - Entrance) - 14.f * Lift;
		HudAlpha = FMath::Clamp(Entrance, 0.f, 1.f);
		const float Pulse = HudPulse(HudNow + i * 0.6f, 3.f);

		const FName AugId = PS->AugmentOffers[i];
		const FSQAugmentDef* AugDef = USQAugments::FindAugment(AugId);
		const FString AugName = AugDef && !AugDef->Name.IsEmpty() ? AugDef->Name : HudPretty(AugId);
		const uint8 Tier = AugDef ? FMath::Min<uint8>(AugDef->Tier, 2) : 1;
		// tier tints the gem: silver / gold / prism (the frame is always gold: that's the AUGMENT look)
		const FLinearColor TierCol = Tier == 0 ? SQUI::Hex(0xD6, 0xDC, 0xE8) : (Tier == 1 ? SQUI::Gold : FMath::Lerp(SQUI::Arcane, SQUI::Soul, HudPulse(HudNow + i, 1.5f)));
		HudGlow(SQUI::Gold, CX, CY, CardW, CardH, 22.f, 0.35f + 0.25f * Pulse + 0.5f * Lift);
		HudRound(FLinearColor(0.f, 0.f, 0.f, 0.45f), CX + 6.f, CY + 12.f, CardW, CardH, 20.f);
		// double gold frame
		HudRound(SQUI::GoldDeep, CX - 5.f, CY - 5.f, CardW + 10.f, CardH + 10.f, 25.f);
		HudRound(FMath::Lerp(SQUI::Gold, FLinearColor::White, 0.3f * Lift), CX - 3.f, CY - 3.f, CardW + 6.f, CardH + 6.f, 23.f);
		HudRound(SQUI::PanelDeep, CX, CY, CardW, CardH, 20.f);
		HudRound(SQUI::WithAlpha(SQUI::Gold, 0.07f + 0.04f * Pulse), CX + 8.f, CY + 8.f, CardW - 16.f, CardH - 16.f, 14.f);
		// header band
		const float BandH = 112.f;
		HudRound(FMath::Lerp(SQUI::PanelDeep, SQUI::GoldDeep, 0.55f), CX, CY, CardW, BandH, 20.f);
		HudRect(FMath::Lerp(SQUI::PanelDeep, SQUI::GoldDeep, 0.55f), CX, CY + BandH - 20.f, CardW, 20.f);
		HudRect(SQUI::Gold, CX, CY + BandH, CardW, 2.f);
		for (int32 Stripe = 0; Stripe < 6; ++Stripe)
		{
			HudTilt(FLinearColor(1.f, 1.f, 1.f, 0.05f), CX + CardW * 0.35f + Stripe * 30.f, CY + 8.f, 12.f, BandH - 18.f, 25.f);
		}
		HudPill(USQAugments::TierName(Tier), CX + CardW * 0.5f, CY + 16.f, TierCol, SQUI::Midnight, 12.f, 1);
		// key badge
		HudRound(SQUI::Midnight, CX + 14.f, CY + 14.f, 32.f, 32.f, 9.f);
		HudRound(SQUI::Gold, CX + 14.f, CY + 12.f, 32.f, 32.f, 9.f);
		HudTxt(FString::FromInt(i + 1), HudHeadFont(), 20.f, CX + 30.f, CY + 28.f - 0.62f * 20.f, SQUI::Midnight, 1, false);
		// emblem: a gold diamond with the initials
		const float EmCX = CX + CardW * 0.5f, EmCY = CY + BandH;
		const float EmS = 76.f * (1.f + 0.06f * Lift);
		HudDisc(SQUI::WithAlpha(SQUI::Gold, 0.15f + 0.1f * Pulse), EmCX, EmCY, EmS * 0.85f);
		HudTilt(SQUI::GoldDeep, EmCX - EmS * 0.5f - 4.f, EmCY - EmS * 0.5f - 4.f, EmS + 8.f, EmS + 8.f, 45.f);
		HudTilt(SQUI::Gold, EmCX - EmS * 0.5f, EmCY - EmS * 0.5f, EmS, EmS, 45.f);
		HudTilt(SQUI::PanelDeep, EmCX - EmS * 0.4f, EmCY - EmS * 0.4f, EmS * 0.8f, EmS * 0.8f, 45.f);
		HudTilt(SQUI::WithAlpha(TierCol, 0.25f), EmCX - EmS * 0.3f, EmCY - EmS * 0.3f, EmS * 0.6f, EmS * 0.6f, 45.f);
		const FString Init = HudInitials(AugName);
		HudTxt(Init, HudHeadFont(), 30.f, EmCX, EmCY - 0.62f * 30.f, TierCol, 1);
		// name + text
		const FString NameUpper = AugName.ToUpper();
		float RowY = EmCY + EmS * 0.75f + 10.f;
		HudTxt(NameUpper, HudHeadFont(), HudFitPx(NameUpper, HudHeadFont(), 26.f, CardW - 28.f), EmCX, RowY, SQUI::Gold, 1);
		RowY += 40.f;
		HudRect(SQUI::WithAlpha(SQUI::Gold, 0.4f), CX + 30.f, RowY, CardW - 60.f, 1.5f);
		RowY += 12.f;
		const FString AugDesc = AugDef && !AugDef->Description.IsEmpty() ? AugDef->Description : FString(TEXT("Changes how your kit plays for the rest of the run."));
		HudWrap(AugDesc, HudLabelFont(false), 15.f, EmCX, RowY, CardW - 36.f, SQUI::BoneWhite, 19.f, 6, true, 1);
		if (AugDef && AugDef->bCoopOnly)
		{
			HudPill(TEXT("CO-OP"), EmCX, CY + CardH - 36.f, SQUI::WithAlpha(SQUI::Soul, 0.85f), SQUI::Midnight, 10.f, 1);
		}
		// shine + pick flash
		HudShine(CX, CY, CardW, CardH, 3.f, 6.f, i * 0.5f);
		const float Flash = AugmentFlashIndex == i ? FMath::Clamp(1.f - (HudNow - AugmentFlashAt) / 0.45f, 0.f, 1.f) : 0.f;
		if (Flash > 0.f)
		{
			HudRound(SQUI::WithAlpha(SQUI::Gold, 0.4f * Flash), CX, CY, CardW, CardH, 20.f);
		}
	}
	HudAlpha = 1.f;
	const FString Hint = TEXT("CLICK A CARD  -  1 / 2 / 3  -  D-PAD");
	HudTxt(Hint, HudLabelFont(true), 12.f, MidX, CardY + CardH + 28.f, SQUI::Muted, 1, false);
	static_cast<void>(GS);
	static_cast<void>(D);
}

// ============================================================ victory / defeat

void ASQHUD::DrawEndScreen(const ASQGameState* GS, const ASQPlayerState* PS)
{
	const bool bWon = GS->Phase == ESQPhase::Victory;
	const float Elapsed = HudNow - EndShownAt;
	const float Dim = HudEaseOut(Elapsed / 0.6f);
	const FLinearColor Accent = bWon ? SQUI::Soul : SQUI::Danger;
	const float VW = HudVW, VH = HudVH;
	const USQData* D = USQData::Get(this);

	HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.84f * Dim), 0.f, 0.f, VW, VH);

	// title
	const float TitleIn = HudBackOut((Elapsed - 0.15f) / 0.6f);
	HudAlpha = HudEaseOut((Elapsed - 0.15f) / 0.3f);
	const FString Title = bWon ? FString(TEXT("MISSION COMPLETE")) : FString(TEXT("MISSION FAILED"));
	const float TitlePx = HudFitPx(Title, HudDisplayFont(), 110.f, VW - 120.f) * (1.3f - 0.3f * TitleIn);
	const float TitleY = VH * 0.16f;
	HudGlowTxt(Title, HudDisplayFont(), TitlePx, VW * 0.5f, TitleY - 0.62f * TitlePx, Accent, Accent, 1);
	FString Sub = GS->MissionId.IsNone() ? FString() : HudPretty(GS->MissionId).ToUpper();
	if (GS->bHardMode)
	{
		Sub += Sub.IsEmpty() ? TEXT("HARD MODE") : TEXT("  -  HARD MODE");
	}
	if (Sub.IsEmpty())
	{
		Sub = bWon ? FString(TEXT("Death is almost pleased.")) : FString(TEXT("Death is... disappointed."));
	}
	HudTxt(Sub, HudHeadFont(), 22.f, VW * 0.5f, TitleY + 0.62f * 110.f + 4.f, GS->bHardMode ? SQUI::Danger : SQUI::BoneWhite, 1);

	// stats card
	const float CardIn = HudEaseOut((Elapsed - 0.45f) / 0.45f);
	HudAlpha = CardIn;
	const float CW = FMath::Min(900.f, VW - 80.f);
	const float CX = (VW - CW) * 0.5f;
	const float CY = TitleY + 120.f + 20.f * (1.f - CardIn);
	TArray<const ASQPlayerState*> Crew;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		if (const ASQPlayerState* Mate = Cast<ASQPlayerState>(Other))
		{
			Crew.Add(Mate);
		}
	}
	int32 ObjDone = 0;
	for (const auto& Obj : GS->Objectives)
	{
		ObjDone += Obj.bDone ? 1 : 0;
	}
	const FSQRunStats& Stats = GS->RunStats; // filled by the game mode at the end of the run (may be empty: fallbacks below)
	const bool bHaveStats = Stats.Players.Num() > 0 || Stats.ObjectivesTotal > 0 || Stats.StageSeconds > 0.f;
	int32 StoryLines = 0;
	const float StoryStep = 22.f;
	if (bWon && !GS->StoryText.IsEmpty())
	{
		StoryLines = HudWrap(GS->StoryText, HudLabelFont(false), 16.f, 0.f, 0.f, CW - 80.f, SQUI::BoneWhite, StoryStep, 3, false);
	}
	const float TilesH = 104.f, CrewRowH = 46.f;
	const int32 CrewRows = bHaveStats && Stats.Players.Num() > 0 ? Stats.Players.Num() : Crew.Num();
	const float CH = 28.f + TilesH + 24.f + 24.f + CrewRows * CrewRowH + (StoryLines > 0 ? 16.f + StoryLines * StoryStep : 0.f) + 26.f + 40.f;
	HudGlow(Accent, CX, CY, CW, CH, 24.f, 0.3f);
	HudLayerPanel(CX, CY, CW, CH, 24.f, Accent, 0.97f);

	struct FSQHudEndTile
	{
		FString Value;
		const TCHAR* Label;
		FLinearColor Colour;
	};
	TArray<FSQHudEndTile> Tiles;
	if (bHaveStats && Stats.StageSeconds > 0.f)
	{
		Tiles.Add({ HudTimeText(Stats.StageSeconds), TEXT("MISSION TIME"), SQUI::BoneWhite });
	}
	else if (GS->StageDuration > 0.f)
	{
		Tiles.Add({ HudTimeText(GS->StageTime), TEXT("MISSION TIME"), SQUI::BoneWhite });
	}
	else
	{
		Tiles.Add({ GS->bEndless ? FString::FromInt(GS->NightIndex + 1) : FString::Printf(TEXT("%d/3"), FMath::Min(GS->NightIndex + 1, 3)), TEXT("NIGHT REACHED"), SQUI::BoneWhite });
	}
	if (bHaveStats && Stats.ObjectivesTotal > 0)
	{
		Tiles.Add({ FString::Printf(TEXT("%d/%d"), Stats.ObjectivesDone, Stats.ObjectivesTotal), TEXT("OBJECTIVES"), SQUI::Amber });
	}
	else if (GS->Objectives.Num() > 0)
	{
		Tiles.Add({ FString::Printf(TEXT("%d/%d"), ObjDone, GS->Objectives.Num()), TEXT("OBJECTIVES"), SQUI::Amber });
	}
	else
	{
		Tiles.Add({ FString::Printf(TEXT("%d/%d"), GS->Banked, GS->Quota), TEXT("SOULS BANKED"), SQUI::Amber });
	}
	Tiles.Add({ FString::FromInt(GS->TeamLevel), TEXT("TEAM LEVEL"), SQUI::Soul });
	Tiles.Add({ FString::FromInt(PS ? PS->Kills : 0), TEXT("YOUR KILLS"), SQUI::Danger });
	if (bHaveStats && Stats.MinibossesKilled > 0)
	{
		Tiles.Add({ FString::FromInt(Stats.MinibossesKilled), TEXT("MINIBOSSES"), SQUI::Arcane });
	}
	Tiles.Add({ FString::FromInt(PS ? PS->OwnedAugments.Num() : 0), TEXT("AUGMENTS"), SQUI::Gold });
	const int32 NumTiles = Tiles.Num();
	const float TileGap = 12.f;
	const float TileW = (CW - 56.f - (NumTiles - 1) * TileGap) / NumTiles;
	for (int32 TileIdx = 0; TileIdx < NumTiles; ++TileIdx)
	{
		const float TX = CX + 28.f + TileIdx * (TileW + TileGap), TY = CY + 28.f;
		const float TileIn = HudBackOut((Elapsed - 0.6f - 0.08f * TileIdx) / 0.4f);
		HudAlpha = CardIn * FMath::Clamp(TileIn, 0.f, 1.f);
		HudRound(SQUI::WithAlpha(SQUI::Midnight, 0.6f), TX, TY, TileW, TilesH, 16.f);
		HudRound(SQUI::WithAlpha(Tiles[TileIdx].Colour, 0.8f), TX + 16.f, TY, TileW - 32.f, 3.f, 1.5f);
		const float ValPx = HudFitPx(Tiles[TileIdx].Value, HudHeadFont(), 40.f, TileW - 20.f) * (1.2f - 0.2f * TileIn);
		HudTxt(Tiles[TileIdx].Value, HudHeadFont(), ValPx, TX + TileW * 0.5f, TY + 42.f - 0.62f * ValPx, Tiles[TileIdx].Colour, 1);
		HudTxt(Tiles[TileIdx].Label, HudLabelFont(true), 11.f, TX + TileW * 0.5f, TY + TilesH - 28.f, SQUI::Muted, 1, false);
	}
	HudAlpha = CardIn;

	// the crew
	float CurY = CY + 28.f + TilesH + 24.f;
	HudTxt(TEXT("THE CREW"), HudLabelFont(true), 11.f, CX + 30.f, CurY, SQUI::Muted, 0, false);
	HudTxt(bHaveStats && Stats.Players.Num() > 0 ? TEXT("KILLS  -  GOLD BANKED") : TEXT("KILLS"), HudLabelFont(true), 11.f, CX + CW - 30.f, CurY, SQUI::Muted, 2, false);
	CurY += 24.f;
	if (bHaveStats && Stats.Players.Num() > 0)
	{
		for (int32 LineIdx = 0; LineIdx < Stats.Players.Num(); ++LineIdx)
		{
			const FSQPlayerRunStats& Line = Stats.Players[LineIdx];
			const FSQReaperDef* LineDef = D ? D->FindReaper(Line.ReaperId) : nullptr;
			const FLinearColor LineAccent = LineDef ? SQUI::RoleColour(LineDef->Role, LineDef->Color) : SQUI::Muted;
			const float RowIn = HudEaseOut((Elapsed - 0.9f - 0.08f * LineIdx) / 0.3f);
			HudAlpha = CardIn * RowIn;
			const bool bMine = PS && Line.Name == PS->GetPlayerName();
			HudRound(SQUI::WithAlpha(bMine ? SQUI::PanelHover : SQUI::Midnight, 0.6f), CX + 22.f, CurY, CW - 44.f, CrewRowH - 6.f, 10.f);
			HudPortrait(Line.ReaperId, D, CX + 30.f, CurY + 4.f, CrewRowH - 14.f, CrewRowH - 14.f, 0.f);
			HudTxt(Line.bBot ? FString::Printf(TEXT("%s (BOT)"), *Line.Name) : Line.Name, HudLabelFont(true), 15.f, CX + 30.f + CrewRowH, CurY + 5.f, SQUI::BoneWhite, 0, false);
			const FString LineReaper = (LineDef ? LineDef->Name : Line.ReaperId.ToString()).ToUpper();
			const FString LineAugs = Line.Augments > 0 ? FString::Printf(TEXT("  -  %d AUGMENTS"), Line.Augments) : FString();
			HudTxt(LineReaper + LineAugs, HudLabelFont(true), 10.f, CX + 30.f + CrewRowH, CurY + 23.f, LineAccent, 0, false);
			const float GoldW = HudTxt(FString::Printf(TEXT("+%d"), Line.GoldBanked), HudHeadFont(), 18.f, CX + CW - 30.f, CurY + 8.f, SQUI::Amber, 2);
			HudTxt(FString::FromInt(Line.Kills), HudHeadFont(), 20.f, CX + CW - 30.f - GoldW - 40.f, CurY + 7.f, SQUI::BoneWhite, 2);
			CurY += CrewRowH;
		}
	}
	for (int32 MateIdx = 0; MateIdx < Crew.Num() && !(bHaveStats && Stats.Players.Num() > 0); ++MateIdx)
	{
		const ASQPlayerState* Mate = Crew[MateIdx];
		const FSQReaperDef* MateDef = D ? D->FindReaper(Mate->ReaperId) : nullptr;
		const FLinearColor MateAccent = MateDef ? SQUI::RoleColour(MateDef->Role, MateDef->Color) : SQUI::Muted;
		const float RowIn = HudEaseOut((Elapsed - 0.9f - 0.08f * MateIdx) / 0.3f);
		HudAlpha = CardIn * RowIn;
		HudRound(SQUI::WithAlpha(Mate == PS ? SQUI::PanelHover : SQUI::Midnight, 0.6f), CX + 22.f, CurY, CW - 44.f, CrewRowH - 6.f, 10.f);
		HudPortrait(Mate->ReaperId, D, CX + 30.f, CurY + 4.f, CrewRowH - 14.f, CrewRowH - 14.f, 0.f);
		HudTxt(Mate->GetPlayerName(), HudLabelFont(true), 15.f, CX + 30.f + CrewRowH, CurY + 5.f, SQUI::BoneWhite, 0, false);
		const FString ReaperName = (MateDef ? MateDef->Name : Mate->ReaperId.ToString()).ToUpper();
		HudTxt(ReaperName, HudLabelFont(true), 10.f, CX + 30.f + CrewRowH, CurY + 23.f, MateAccent, 0, false);
		HudTxt(FString::FromInt(Mate->Kills), HudHeadFont(), 20.f, CX + CW - 30.f, CurY + 7.f, SQUI::BoneWhite, 2);
		CurY += CrewRowH;
	}
	HudAlpha = CardIn;
	if (StoryLines > 0)
	{
		CurY += 16.f;
		HudWrap(GS->StoryText, HudLabelFont(false), 16.f, VW * 0.5f, CurY, CW - 80.f, SQUI::BoneWhite, StoryStep, 3, true, 1);
		CurY += StoryLines * StoryStep;
	}
	CurY += 22.f;
	if (bHaveStats && Stats.bFirstClear)
	{
		HudPill(TEXT("FIRST CLEAR!"), VW * 0.5f, CurY, SQUI::WithAlpha(Accent, 0.2f), Accent, 13.f, 1);
	}

	// ---- unlock strip (Nov 12 2026): what you just unlocked (NEW) and the closest goals, so every run shows a next toy
	float BelowY = CY + CH + 18.f;
	if (Elapsed > 1.f && EndUnlocksBuiltFor != EndShownAt)
	{
		EndUnlocksBuiltFor = EndShownAt;
		BuildEndUnlocks(D);
	}
	const float BtnH = 64.f, StripH = 78.f;
	const bool bStripFits = EndUnlocks.Num() > 0 && BelowY + StripH + 16.f + BtnH + 44.f <= VH;
	if (bStripFits)
	{
		const float StripIn = HudEaseOut((Elapsed - 1.1f) / 0.4f);
		const int32 NumShown = FMath::Min(3, EndUnlocks.Num());
		const float StripGap = 12.f;
		const float StripW = (CW - (NumShown - 1) * StripGap) / NumShown;
		for (int32 UnlockIdx = 0; UnlockIdx < NumShown; ++UnlockIdx)
		{
			const FEndUnlock& Unlock = EndUnlocks[UnlockIdx];
			const float TX = CX + UnlockIdx * (StripW + StripGap), TY = BelowY + 12.f * (1.f - StripIn);
			HudAlpha = HudEaseOut((Elapsed - 1.1f - 0.08f * UnlockIdx) / 0.35f);
			const FLinearColor Edge = Unlock.bNew ? SQUI::Gold : SQUI::Line;
			HudRound(SQUI::WithAlpha(Edge, 0.9f), TX - 2.f, TY - 2.f, StripW + 4.f, StripH + 4.f, 16.f);
			HudRound(SQUI::WithAlpha(SQUI::PanelDeep, 0.97f), TX, TY, StripW, StripH, 14.f);
			const FString Tag = Unlock.bNew ? FString(TEXT("NEW UNLOCK")) : FString(TEXT("NEXT UP"));
			HudTxt(Tag, HudLabelFont(true), 11.f, TX + 16.f, TY + 8.f, Unlock.bNew ? SQUI::Gold : SQUI::Muted, 0, false);
			const FString UnlockName = Unlock.Name.ToUpper();
			HudTxt(UnlockName, HudHeadFont(), HudFitPx(UnlockName, HudHeadFont(), 18.f, StripW - 32.f), TX + 16.f, TY + 24.f, SQUI::BoneWhite, 0);
			HudTxt(Unlock.Reward, HudLabelFont(true), HudFitPx(Unlock.Reward, HudLabelFont(true), 13.f, StripW - 32.f), TX + 16.f, TY + 48.f, SQUI::Amber, 0, false);
			if (!Unlock.bNew)
			{
				HudRound(SQUI::Line, TX + 16.f, TY + StripH - 10.f, StripW - 32.f, 5.f, 2.5f);
				HudRound(SQUI::Amber, TX + 16.f, TY + StripH - 10.f, FMath::Max(5.f, (StripW - 32.f) * FMath::Clamp(Unlock.Frac, 0.f, 1.f)), 5.f, 2.5f);
			}
		}
		BelowY += StripH + 16.f;
	}

	// ---- RETRY / PLAY AGAIN and CHANGE REAPER (the host or a solo player chooses; everyone else waits)
	ASQPlayerController* EndPC = Cast<ASQPlayerController>(PlayerOwner);
	const bool bCanChoose = EndPC && EndPC->CanChooseAfterRun();
	const float BtnIn = HudEaseOut((Elapsed - 1.2f) / 0.35f);
	HudAlpha = BtnIn;
	const int32 SecondsLeft = FMath::Max(0, FMath::CeilToInt(GS->PhaseTimeLeft));
	if (bCanChoose)
	{
		const bool bInputLive = Elapsed > 1.5f && !bEndChoiceSent && !EndPC->IsSettingsOpen();
		float MouseX = -1.f, MouseY = -1.f;
		const bool bMouse = HudMouseDesign(MouseX, MouseY);
		const bool bClickNow = bInputLive && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);
		const float BtnW = FMath::Min(360.f, (CW - 16.f) * 0.5f), BtnGap = 16.f;
		const float BtnX0 = VW * 0.5f - BtnW - BtnGap * 0.5f;
		const FString Labels[2] = { bWon ? FString(TEXT("PLAY AGAIN")) : FString(TEXT("RETRY")), FString(TEXT("CHANGE REAPER")) };
		const TCHAR* Keys[2] = { TEXT("ENTER"), TEXT("C") };
		int32 Chosen = INDEX_NONE;
		if (bInputLive)
		{
			if (PlayerOwner->WasInputKeyJustPressed(EKeys::Enter) || PlayerOwner->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)) Chosen = 0;
			else if (PlayerOwner->WasInputKeyJustPressed(EKeys::C) || PlayerOwner->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left)) Chosen = 1;
		}
		for (int32 BtnIdx = 0; BtnIdx < 2; ++BtnIdx)
		{
			const float BX = BtnX0 + BtnIdx * (BtnW + BtnGap), BY = BelowY + 14.f * (1.f - BtnIn);
			const bool bOver = bMouse && MouseX >= BX && MouseX <= BX + BtnW && MouseY >= BY && MouseY <= BY + BtnH;
			if (bOver && bClickNow) Chosen = BtnIdx;
			EndBtnLift[BtnIdx] = FMath::FInterpTo(EndBtnLift[BtnIdx], bOver ? 1.f : 0.f, HudDelta, 14.f);
			const float Lift = EndBtnLift[BtnIdx];
			const bool bPrimary = BtnIdx == 0;
			const FLinearColor Face = bPrimary ? SQUI::Soul : SQUI::Panel;
			const FLinearColor Ink = bPrimary ? SQUI::Midnight : SQUI::BoneWhite;
			if (bPrimary) HudGlow(SQUI::Soul, BX, BY, BtnW, BtnH, 20.f, 0.3f + 0.3f * HudPulse(HudNow, 3.5f) + 0.4f * Lift);
			HudRound(FLinearColor(0.f, 0.f, 0.f, 0.4f), BX + 3.f, BY + 6.f, BtnW, BtnH, 20.f);
			HudRound(bPrimary ? SQUI::SoulDeep : SQUI::Line, BX - 2.f, BY - 2.f - 2.f * Lift, BtnW + 4.f, BtnH + 4.f, 22.f);
			HudRound(FMath::Lerp(Face, bPrimary ? SQUI::BoneWhite : SQUI::PanelHover, 0.25f * Lift), BX, BY - 2.f * Lift, BtnW, BtnH, 20.f);
			const float LabelPx = HudFitPx(Labels[BtnIdx], HudHeadFont(), 26.f, BtnW - 120.f);
			HudTxt(Labels[BtnIdx], HudHeadFont(), LabelPx, BX + BtnW * 0.5f - 24.f, BY - 2.f * Lift + BtnH * 0.5f - 0.62f * LabelPx, Ink, 1, false);
			HudPill(Keys[BtnIdx], BX + BtnW - 18.f, BY - 2.f * Lift + BtnH * 0.5f - 13.f, SQUI::WithAlpha(SQUI::Midnight, bPrimary ? 0.25f : 0.6f), Ink, 12.f, 2);
		}
		if (Chosen != INDEX_NONE)
		{
			bEndChoiceSent = true;
			Play(TEXT("Bank"), 0.6f, 0.f);
			EndPC->ServerEndChoice(Chosen == 0);
		}
		HudTxt(FString::Printf(TEXT("BACK TO THE LOBBY IN %ds"), SecondsLeft), HudLabelFont(true), 12.f, VW * 0.5f, BelowY + BtnH + 14.f, SQUI::Muted, 1, false);
	}
	else
	{
		HudPill(FString::Printf(TEXT("THE HOST PICKS: RETRY OR LOBBY  -  %ds"), SecondsLeft), VW * 0.5f, BelowY + 12.f, SQUI::WithAlpha(Accent, 0.2f), Accent, 13.f, 1);
	}
	HudAlpha = 1.f;
}

void ASQHUD::BuildEndUnlocks(const USQData* D)
{
	EndUnlocks.Reset();
	const USQSaveGame* Save = USQSaveGame::Load();
	if (!Save) return;
	// just finished (not yet seen on the UNLOCKS page), newest first
	for (int32 NewIdx = Save->QuestsNew.Num() - 1; NewIdx >= 0 && EndUnlocks.Num() < 3; --NewIdx)
	{
		if (const FSQQuestDef* Quest = SQQuests::FindQuest(Save->QuestsNew[NewIdx]))
		{
			FEndUnlock Unlock;
			Unlock.Name = Quest->Name;
			Unlock.Reward = SQQuests::DescribeRewards(*Quest, D);
			Unlock.Frac = 1.f;
			Unlock.bNew = true;
			EndUnlocks.Add(Unlock);
		}
	}
	// then the closest unfinished goals
	TArray<FEndUnlock> Next;
	for (const FSQQuestDef& Quest : SQQuests::Quests())
	{
		if (Save->HasQuest(Quest.Id)) continue;
		FEndUnlock Unlock;
		Unlock.Name = Quest.Name;
		Unlock.Reward = SQQuests::DescribeRewards(Quest, D);
		Unlock.Frac = static_cast<float>(SQQuests::Progress(Quest, Save, nullptr)) / static_cast<float>(FMath::Max(1, Quest.Target));
		Next.Add(Unlock);
	}
	Next.Sort([](const FEndUnlock& UA, const FEndUnlock& UB) { return UA.Frac > UB.Frac; });
	for (int32 NextIdx = 0; NextIdx < Next.Num() && EndUnlocks.Num() < 3; ++NextIdx) EndUnlocks.Add(Next[NextIdx]);
}

void ASQHUD::DrawSkipLobby(const ASQGameState* GS)
{
	// The lobby would only flash for a moment: hold a calm card over it instead
	HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.92f), 0.f, 0.f, HudVW, HudVH);
	const FString Title(TEXT("RISING..."));
	const float Px = HudFitPx(Title, HudDisplayFont(), 84.f, HudVW - 120.f);
	HudGlowTxt(Title, HudDisplayFont(), Px, HudVW * 0.5f, HudVH * 0.42f - 0.62f * Px, SQUI::Soul, SQUI::Soul, 1);
	const FString Sub = GS && !GS->MissionId.IsNone() ? HudPretty(GS->MissionId).ToUpper() : FString(TEXT("SAME CREW, SAME CONTRACT"));
	HudTxt(Sub, HudHeadFont(), 22.f, HudVW * 0.5f, HudVH * 0.42f + 0.62f * 84.f, SQUI::BoneWhite, 1);
}


// ============================================================ Nov 9 remaster: helpers

void ASQHUD::HudTexture(UTexture* Tex, float X, float Y, float W, float H, const FLinearColor& Tint, float U0, float V0, float U1, float V1)
{
	if (!Canvas || !Tex || W <= 0.f || H <= 0.f)
	{
		return;
	}
	FTextureResource* TexResource = Tex->GetResource();
	if (!TexResource)
	{
		return;
	}
	// Opaque: a scene capture's alpha channel isn't reliable, so a fade darkens toward black instead (panels behind are dark)
	const float Fade = FMath::Clamp(Tint.A * HudAlpha, 0.f, 1.f);
	if (Fade <= 0.003f)
	{
		return;
	}
	FCanvasTileItem Tile(FVector2D(X * UIScale, Y * UIScale), TexResource, FVector2D(W * UIScale, H * UIScale), FVector2D(U0, V0), FVector2D(U1, V1),
		FLinearColor(Tint.R * Fade, Tint.G * Fade, Tint.B * Fade, 1.f));
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);
}

void ASQHUD::HudPortrait(FName ReaperId, const USQData* D, float X, float Y, float W, float H, float Grey)
{
	const FSQReaperDef* PortraitDef = D ? D->FindReaper(ReaperId) : nullptr;
	const FLinearColor Base = PortraitDef ? PortraitDef->Color : SQUI::Muted;
	const TObjectPtr<UTextureRenderTarget2D>* Found = PortraitTargets.Find(ReaperId);
	UTextureRenderTarget2D* PortraitTex = Found ? Found->Get() : nullptr;
	if (PortraitTex && PortraitsReady.Contains(ReaperId) && W > 1.f && H > 1.f)
	{
		// crop the 4:5 capture to this box (keep the head: crop more from the bottom)
		const float TexAspect = static_cast<float>(FMath::Max(1, PortraitTex->SizeX)) / static_cast<float>(FMath::Max(1, PortraitTex->SizeY));
		const float BoxAspect = W / H;
		float U0 = 0.f, U1 = 1.f, V0 = 0.f, V1 = 1.f;
		if (BoxAspect > TexAspect)
		{
			const float Span = TexAspect / BoxAspect;
			V0 = (1.f - Span) * 0.3f;
			V1 = V0 + Span;
		}
		else
		{
			const float Span = BoxAspect / TexAspect;
			U0 = (1.f - Span) * 0.5f;
			U1 = U0 + Span;
		}
		const FLinearColor Tint = FMath::Lerp(FLinearColor::White, FLinearColor(0.34f, 0.32f, 0.4f, 1.f), FMath::Clamp(Grey, 0.f, 1.f));
		HudTexture(PortraitTex, X, Y, W, H, Tint, U0, V0, U1, V1);
		return;
	}
	// no capture yet (or the studio is off): a colour tile with the initial
	const FLinearColor Swatch = FMath::Lerp(Base, SQUI::Disabled, 0.65f * FMath::Clamp(Grey, 0.f, 1.f));
	HudRect(Swatch, X, Y, W, H);
	HudRect(FLinearColor(0.f, 0.f, 0.f, 0.22f), X, Y + H * 0.55f, W, H * 0.45f);
	HudRect(FLinearColor(1.f, 1.f, 1.f, 0.14f), X, Y, W, H * 0.22f);
	const FString Initial = PortraitDef ? PortraitDef->Name.Left(1).ToUpper() : FString(TEXT("?"));
	const float InitialPx = FMath::Min(W, H) * 0.5f;
	HudTxt(Initial, HudHeadFont(), InitialPx, X + W * 0.5f, Y + H * 0.5f - 0.62f * InitialPx, SQUI::WithAlpha(SQUI::Midnight, 0.8f), 1, false);
}

void ASQHUD::HudArc(float CX, float CY, float Radius, float Thick, float From01, float To01, const FLinearColor& Colour)
{
	const float A0 = FMath::Clamp(From01, 0.f, 1.f);
	const float A1 = FMath::Clamp(To01, 0.f, 1.f);
	if (A1 - A0 <= 0.001f || Radius <= 0.f)
	{
		return;
	}
	constexpr int32 Segs = 72;
	const float SegLen = 2.f * UE_PI * Radius / Segs * 1.2f;
	for (int32 Seg = 0; Seg < Segs; ++Seg)
	{
		const float Mid01 = (Seg + 0.5f) / Segs;
		if (Mid01 < A0 || Mid01 > A1)
		{
			continue;
		}
		const float Ang = -UE_HALF_PI + Mid01 * 2.f * UE_PI; // clockwise from 12 o'clock (screen y points down)
		const float SegX = CX + FMath::Cos(Ang) * Radius;
		const float SegY = CY + FMath::Sin(Ang) * Radius;
		HudTilt(Colour, SegX - SegLen * 0.5f, SegY - Thick * 0.5f, SegLen, Thick, FMath::RadiansToDegrees(Ang) + 90.f);
	}
}

void ASQHUD::HudArrow(float TipX, float TipY, float AngleDeg, float ArrowSize, const FLinearColor& Colour)
{
	const float Len = ArrowSize;
	const float Thick = ArrowSize * 0.34f;
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float ArmDeg = AngleDeg + 180.f + Side * 40.f;
		const float ArmRad = FMath::DegreesToRadians(ArmDeg);
		const float MidX = TipX + FMath::Cos(ArmRad) * Len * 0.45f;
		const float MidY = TipY + FMath::Sin(ArmRad) * Len * 0.45f;
		HudTilt(Colour, MidX - Len * 0.5f, MidY - Thick * 0.5f, Len, Thick, ArmDeg);
	}
}

void ASQHUD::HudLayerPanel(float X, float Y, float W, float H, float Radius, const FLinearColor& Accent, float FillAlpha)
{
	// soft shadow in three layers
	for (int32 Layer = 3; Layer >= 1; --Layer)
	{
		const float Grow = Layer * 4.f;
		HudRound(FLinearColor(0.f, 0.f, 0.f, 0.11f), X - Grow + 3.f, Y - Grow + 9.f, W + 2.f * Grow, H + 2.f * Grow, Radius + Grow);
	}
	// rim (a touch of the accent), body, darker lower layers, top sheen, accent strip
	HudRound(SQUI::WithAlpha(FMath::Lerp(SQUI::Line, Accent, 0.3f), 0.95f), X - 1.5f, Y - 1.5f, W + 3.f, H + 3.f, Radius + 1.5f);
	HudRound(SQUI::WithAlpha(SQUI::Panel, FillAlpha), X, Y, W, H, Radius);
	for (int32 Band = 1; Band <= 3; ++Band)
	{
		const float BandH = H * 0.22f * Band;
		HudRound(SQUI::WithAlpha(SQUI::PanelDeep, 0.22f * FillAlpha), X, Y + H - BandH, W, BandH, Radius);
	}
	HudRound(FLinearColor(1.f, 1.f, 1.f, 0.028f), X + 4.f, Y + 3.f, W - 8.f, FMath::Min(H * 0.3f, 44.f), FMath::Max(0.f, Radius - 4.f));
	if (W > 2.f * Radius + 8.f)
	{
		HudRound(SQUI::WithAlpha(Accent, 0.9f), X + Radius, Y - 1.f, W - 2.f * Radius, 3.f, 1.5f);
		HudRound(SQUI::WithAlpha(Accent, 0.18f), X + Radius, Y + 2.f, W - 2.f * Radius, 6.f, 3.f);
	}
}

FString ASQHUD::HudPretty(FName Id)
{
	if (Id.IsNone())
	{
		return FString();
	}
	const FString Raw = Id.ToString();
	FString Out;
	for (int32 CharIdx = 0; CharIdx < Raw.Len(); ++CharIdx)
	{
		const TCHAR Ch = Raw[CharIdx];
		if (Ch == TEXT('_'))
		{
			Out.AppendChar(TEXT(' '));
			continue;
		}
		if (CharIdx > 0 && FChar::IsUpper(Ch) && FChar::IsLower(Raw[CharIdx - 1]))
		{
			Out.AppendChar(TEXT(' '));
		}
		Out.AppendChar(Ch);
	}
	return Out;
}

FString ASQHUD::HudTimeText(float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

UTextureRenderTarget2D* ASQHUD::HudMakeTarget(int32 TargetW, int32 TargetH)
{
	UTextureRenderTarget2D* Made = NewObject<UTextureRenderTarget2D>(this);
	Made->RenderTargetFormat = RTF_RGBA8_SRGB;
	Made->ClearColor = SQUI::Midnight;
	Made->bAutoGenerateMips = false;
	Made->InitAutoFormat(TargetW, TargetH);
	Made->UpdateResourceImmediate(true);
	return Made;
}

void ASQHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LobbyStudioShutdown();
	Super::EndPlay(EndPlayReason);
}

void ASQHUD::LobbyStudioShutdown()
{
	if (StageStudio)
	{
		StageStudio->Destroy();
		StageStudio = nullptr;
	}
	if (PortraitStudio)
	{
		PortraitStudio->Destroy();
		PortraitStudio = nullptr;
	}
	PortraitPending = NAME_None;
	StageShown = NAME_None;
}

void ASQHUD::LobbyStudioUpdate(bool bLobby, FName FocusReaper, const FLinearColor& Accent, const USQData* D)
{
	UWorld* World = GetWorld();
	const bool bWanted = bLobby && World && D && GetNetMode() != NM_DedicatedServer && D->Tune(TEXT("LobbyStudio"), 1.f) > 0.5f;
	if (!bWanted)
	{
		if (StageStudio || PortraitStudio)
		{
			LobbyStudioShutdown();
		}
		return;
	}

	// Far above the arena (the top-down camera never looks up there), one set per local player; the captures render only
	// the studio actor, so the arena, the sky and the fog never show in them
	int32 LocalIdx = 0;
	if (const ULocalPlayer* LP = PlayerOwner ? PlayerOwner->GetLocalPlayer() : nullptr)
	{
		LocalIdx = FMath::Clamp(LP->GetControllerId(), 0, 7);
	}
	const FVector StudioBase(0.0, 30000.0 * LocalIdx, static_cast<double>(D->Tune(TEXT("LobbyStudioHeight"), 60000.f)));
	if (!StageStudio)
	{
		if (!StageTarget)
		{
			StageTarget = HudMakeTarget(1024, 768);
		}
		const FTransform At(StudioBase);
		ASQMenuShowroom* Made = World->SpawnActorDeferred<ASQMenuShowroom>(ASQMenuShowroom::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Made)
		{
			Made->SetupStudio(StageTarget, false);
			Made->FinishSpawning(At);
			Made->SetCloseUp(true);
			StageStudio = Made;
			StageShown = NAME_None;
		}
	}
	if (!bPortraitsDone && !PortraitStudio)
	{
		const FTransform At(StudioBase + FVector(0.0, 9000.0, 0.0));
		ASQMenuShowroom* Made = World->SpawnActorDeferred<ASQMenuShowroom>(ASQMenuShowroom::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Made)
		{
			Made->SetupStudio(nullptr, true);
			Made->FinishSpawning(At);
			PortraitStudio = Made;
			PortraitPending = NAME_None;
		}
	}

	// the centre stage follows the focused reaper (the showroom plays its rise-out-of-the-ground on a change)
	if (StageStudio)
	{
		if (!FocusReaper.IsNone() && FocusReaper != StageShown)
		{
			StageStudio->ShowReaper(FocusReaper);
			StageShown = FocusReaper;
			StageSwapAt = HudNow;
		}
		StageStudio->SetAccent(Accent);
		StageStudio->SetCapturing(bLobbyExpanded);
	}

	// portraits: one reaper at a time - show it, let the idle pose settle for a few frames, capture once into its own target
	if (PortraitStudio && !bPortraitsDone)
	{
		if (PortraitPending.IsNone())
		{
			for (const FSQReaperDef& Each : D->Reapers)
			{
				if (!PortraitsReady.Contains(Each.Id))
				{
					PortraitPending = Each.Id;
					break;
				}
			}
			if (PortraitPending.IsNone())
			{
				bPortraitsDone = true;
				PortraitStudio->Destroy();
				PortraitStudio = nullptr;
			}
			else
			{
				const FSQReaperDef* PendingDef = D->FindReaper(PortraitPending);
				PortraitStudio->ShowReaperNow(PortraitPending);
				PortraitStudio->SetAccent(SQUI::RoleColour(PendingDef ? PendingDef->Role : NAME_None, PendingDef ? PendingDef->Color : SQUI::Soul));
				PortraitWait = 0;
			}
		}
		else if (++PortraitWait >= 4)
		{
			TObjectPtr<UTextureRenderTarget2D>& Slot = PortraitTargets.FindOrAdd(PortraitPending);
			if (!Slot)
			{
				Slot = HudMakeTarget(200, 250);
			}
			PortraitStudio->CaptureNow(Slot.Get());
			PortraitsReady.Add(PortraitPending);
			PortraitPending = NAME_None;
		}
	}
}

// ============================================================ map revamp (Nov 10 2026): minimap + full map

namespace SQMinimap
{
	constexpr int32 Res = 256;      // map image (pixels per side)
	constexpr int32 FogRes = 64;    // fog cells per side (4 px each)
	constexpr int32 FogPx = Res / FogRes;

	FColor Mix(const FColor& A, const FColor& B, float T)
	{
		const float K = FMath::Clamp(T, 0.f, 1.f);
		return FColor(static_cast<uint8>(FMath::Lerp(static_cast<float>(A.R), static_cast<float>(B.R), K)),
			static_cast<uint8>(FMath::Lerp(static_cast<float>(A.G), static_cast<float>(B.G), K)),
			static_cast<uint8>(FMath::Lerp(static_cast<float>(A.B), static_cast<float>(B.B), K)), 255);
	}

	FColor FromLinear(const FLinearColor& C, float Scale = 1.f)
	{
		return FLinearColor(C.R * Scale, C.G * Scale, C.B * Scale, 1.f).ToFColor(true);
	}

	void StampDisc(TArray<FColor>& Img, float CX, float CY, float R, const FColor& Colour, float Alpha = 1.f)
	{
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(CX - R - 1.f));
		const int32 X1 = FMath::Min(Res - 1, FMath::CeilToInt(CX + R + 1.f));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt(CY - R - 1.f));
		const int32 Y1 = FMath::Min(Res - 1, FMath::CeilToInt(CY + R + 1.f));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const float D = FMath::Sqrt(FMath::Square(X + 0.5f - CX) + FMath::Square(Y + 0.5f - CY));
				const float Cover = FMath::Clamp(R + 0.5f - D, 0.f, 1.f) * Alpha;
				if (Cover <= 0.f) continue;
				FColor& Px = Img[Y * Res + X];
				Px = Mix(Px, Colour, Cover);
			}
		}
	}

	void StampLine(TArray<FColor>& Img, const FVector2D& A, const FVector2D& B, float R, const FColor& Colour, float Alpha = 1.f)
	{
		const float Len = static_cast<float>(FVector2D::Distance(A, B));
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / FMath::Max(0.5f, R * 0.6f)));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const FVector2D P = FMath::Lerp(A, B, static_cast<double>(i) / Steps);
			StampDisc(Img, static_cast<float>(P.X), static_cast<float>(P.Y), R, Colour, Alpha);
		}
	}
}

FVector2D ASQHUD::MinimapUV(float X, float Y) const
{
	// North-up = screen-up of the fixed camera (Tuning CameraYaw): rotate the world into camera space first
	const float Rad = FMath::DegreesToRadians(MinimapYaw);
	const float CosY = FMath::Cos(Rad);
	const float SinY = FMath::Sin(Rad);
	const float Fwd = X * CosY + Y * SinY;     // screen up
	const float Right = -X * SinY + Y * CosY;  // screen right
	const float Span = FMath::Max(1.f, MinimapHalf * 2.f);
	return FVector2D((Right + MinimapHalf) / Span, (MinimapHalf - Fwd) / Span);
}

void ASQHUD::MinimapRebuildBase(const ASQArena* Arena, const USQData* D)
{
	using namespace SQMinimap;
	MinimapHalf = Arena->GetArenaHalf();
	MinimapYaw = D ? D->Tune(TEXT("CameraYaw"), 0.f) : 0.f;
	MinimapBase.SetNumUninitialized(Res * Res);
	MinimapFog.Init(0, FogRes * FogRes);
	const float Span = MinimapHalf * 2.f;
	const float Rad = FMath::DegreesToRadians(MinimapYaw);
	const float CosY = FMath::Cos(Rad);
	const float SinY = FMath::Sin(Rad);
	const FColor Outside(9, 8, 15, 255);
	const FColor Ground(40, 52, 46, 255);
	const FColor High(88, 104, 86, 255);
	const FColor Bowl(70, 72, 92, 255);
	const FColor Ramp(26, 32, 30, 255);
	const FColor Water(24, 58, 92, 255);
	const FColor Paved(96, 92, 104, 255);
	const FColor BossFloor(92, 40, 52, 255);
	const TArray<FSQDistrict>& Districts = Arena->GetDistricts();
	const TArray<FSQMapZone>& Zones = Arena->GetZones();
	auto PixelToWorld = [&](float PXf, float PYf, float& OutX, float& OutY)
	{
		const float Fwd = MinimapHalf - PYf / Res * Span;
		const float Right = PXf / Res * Span - MinimapHalf;
		OutX = Fwd * CosY - Right * SinY;
		OutY = Fwd * SinY + Right * CosY;
	};
	auto ToPixel = [&](const FVector2D& W) -> FVector2D
	{
		const FVector2D UV = MinimapUV(static_cast<float>(W.X), static_cast<float>(W.Y));
		return UV * Res;
	};
	const float PxPerCm = Res / Span;

	// --- Per pixel: ground height and slope, water, district tint, zone floors
	for (int32 PY = 0; PY < Res; ++PY)
	{
		for (int32 PX = 0; PX < Res; ++PX)
		{
			float WX, WY;
			PixelToWorld(PX + 0.5f, PY + 0.5f, WX, WY);
			FColor C = Outside;
			if (FMath::Abs(WX) <= MinimapHalf && FMath::Abs(WY) <= MinimapHalf)
			{
				const float H = Arena->GroundZ(WX, WY);
				C = Ground;
				if (Arena->IsWaterAt(WX, WY)) C = Water;
				else
				{
					// Three-map remaster: tall maps (terraces, the Clocktower climb) shade over their whole height range
					if (H > 15.f) C = Mix(Ground, High, FMath::Clamp(H / FMath::Max(220.f, Arena->MaxGroundZ()), 0.f, 1.f));
					else if (H < -15.f) C = Mix(Ground, Bowl, FMath::Clamp(-H / 150.f, 0.f, 1.f));
					const int32 DistrictIdx = Arena->FindDistrictAt(WX, WY);
					if (Districts.IsValidIndex(DistrictIdx)) C = Mix(C, FromLinear(Districts[DistrictIdx].Color, 0.55f), 0.28f);
					// Ramps and banks read as darker strokes (where you can ride)
					C = Mix(C, Ramp, FMath::Clamp((Arena->GroundSlopeDeg(WX, WY) - 7.f) / 14.f, 0.f, 0.75f));
				}
				for (const FSQMapZone& Zone : Zones)
				{
					const float ZD = static_cast<float>(FVector2D::Distance(Zone.Centre, FVector2D(WX, WY)));
					if (ZD > Zone.Radius + 80.f) continue;
					const bool bBoss = Zone.Kind == ESQZoneKind::Boss;
					C = Mix(C, bBoss ? BossFloor : Paved, ZD < Zone.Radius ? 0.75f : 0.f);
					if (FMath::Abs(ZD - Zone.Radius) < 70.f) C = Mix(C, bBoss ? FColor(240, 70, 80, 255) : FColor(220, 214, 200, 255), 0.85f);
				}
				// Edge framing
				const float EdgeDist = MinimapHalf - FMath::Max(FMath::Abs(WX), FMath::Abs(WY));
				if (EdgeDist < 180.f) C = Mix(C, FColor(150, 140, 170, 255), 0.7f);
			}
			MinimapBase[PY * Res + PX] = C;
		}
	}

	// --- Stamped lines: road, lanes, walls, spawner pads, park pieces
	const TArray<FVector2D>& Road = Arena->GetRoadPoints();
	for (int32 i = 1; i < Road.Num(); ++i) StampLine(MinimapBase, ToPixel(Road[i - 1]), ToPixel(Road[i]), FMath::Max(1.2f, 300.f * PxPerCm), FColor(150, 118, 78, 255));
	for (const FSQLane& Lane : Arena->GetLanes())
	{
		const FColor LaneColour = Districts.IsValidIndex(Lane.District) ? Mix(FColor(130, 112, 86, 255), FromLinear(Districts[Lane.District].Color), 0.25f) : FColor(130, 112, 86, 255);
		for (int32 i = 1; i < Lane.Pts.Num(); ++i) StampLine(MinimapBase, ToPixel(Lane.Pts[i - 1]), ToPixel(Lane.Pts[i]), FMath::Max(1.f, 210.f * PxPerCm), LaneColour);
	}
	for (const FSQMapWall& W : Arena->GetWalls())
	{
		if (W.Kind == ESQWallKind::Water) continue;
		StampLine(MinimapBase, ToPixel(W.A), ToPixel(W.B), FMath::Max(0.8f, W.HalfThick * PxPerCm), FColor(14, 13, 20, 255));
	}
	for (const FVector2D& Pad : Arena->GetSpawnerPads())
	{
		const FVector2D P = ToPixel(Pad);
		StampDisc(MinimapBase, static_cast<float>(P.X), static_cast<float>(P.Y), FMath::Max(2.f, 380.f * PxPerCm), FColor(70, 30, 30, 255));
	}
	for (const FSQParkSpot& Spot : Arena->GetParkSpots())
	{
		const FVector2D P = ToPixel(Spot.At);
		const FColor PieceColour = Districts.IsValidIndex(Spot.District) ? FromLinear(Districts[Spot.District].Color, 0.9f) : FColor(200, 200, 200, 255);
		StampDisc(MinimapBase, static_cast<float>(P.X), static_cast<float>(P.Y), FMath::Max(1.5f, Spot.FootR * 0.6f * PxPerCm), PieceColour);
	}
	// Landmark buildings, gate piers, yard walls, avenue trees and lamp posts (ASQArena::BuildSetPieces)
	for (const FVector& Mark : Arena->GetSetMarks())
	{
		const FVector2D P = ToPixel(FVector2D(Mark.X, Mark.Y));
		StampDisc(MinimapBase, static_cast<float>(P.X), static_cast<float>(P.Y), FMath::Max(1.f, static_cast<float>(Mark.Z) * PxPerCm), FColor(20, 18, 30, 255));
	}
	MinimapPixels.SetNumUninitialized(Res * Res);
	bMinimapDirty = true;
}

void ASQHUD::MinimapUpdateFog(const ASQGameState* GS, bool bForce)
{
	using namespace SQMinimap;
	if (MinimapFog.Num() != FogRes * FogRes) return;
	const USQData* D = USQData::Get(this);
	const float RevealCm = D ? FMath::Clamp(D->Tune(TEXT("MinimapRevealRadius"), 3200.f), 500.f, 20000.f) : 3200.f;
	const float RevealCells = RevealCm / (MinimapHalf * 2.f) * FogRes;
	bool bChanged = bForce;
	for (const APlayerState* AnyPS : GS->PlayerArray)
	{
		const APawn* Pawn = AnyPS ? AnyPS->GetPawn() : nullptr;
		if (!Pawn) continue;
		const FVector L = Pawn->GetActorLocation();
		const FVector2D Cell = MinimapUV(static_cast<float>(L.X), static_cast<float>(L.Y)) * FogRes;
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(static_cast<float>(Cell.X) - RevealCells));
		const int32 X1 = FMath::Min(FogRes - 1, FMath::CeilToInt(static_cast<float>(Cell.X) + RevealCells));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt(static_cast<float>(Cell.Y) - RevealCells));
		const int32 Y1 = FMath::Min(FogRes - 1, FMath::CeilToInt(static_cast<float>(Cell.Y) + RevealCells));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const float Dist = FMath::Sqrt(FMath::Square(X + 0.5f - static_cast<float>(Cell.X)) + FMath::Square(Y + 0.5f - static_cast<float>(Cell.Y)));
				const uint8 Want = static_cast<uint8>(FMath::Clamp((RevealCells - Dist) * 255.f, 0.f, 255.f));
				uint8& Fog = MinimapFog[Y * FogRes + X];
				if (Want > Fog)
				{
					Fog = Want;
					bChanged = true;
				}
			}
		}
	}
	if (bChanged) bMinimapDirty = true;
}

void ASQHUD::MinimapCompose()
{
	using namespace SQMinimap;
	if (MinimapBase.Num() != Res * Res || MinimapPixels.Num() != Res * Res) return;
	const FColor Unknown(12, 11, 22, 255);
	for (int32 PY = 0; PY < Res; ++PY)
	{
		// Bilinear fog lookup: soft edges on the revealed ground
		const float FY = (PY + 0.5f) / FogPx - 0.5f;
		const int32 FY0 = FMath::Clamp(FMath::FloorToInt(FY), 0, FogRes - 1);
		const int32 FY1 = FMath::Min(FY0 + 1, FogRes - 1);
		const float TY = FMath::Clamp(FY - FY0, 0.f, 1.f);
		for (int32 PX = 0; PX < Res; ++PX)
		{
			const float FX = (PX + 0.5f) / FogPx - 0.5f;
			const int32 FX0 = FMath::Clamp(FMath::FloorToInt(FX), 0, FogRes - 1);
			const int32 FX1 = FMath::Min(FX0 + 1, FogRes - 1);
			const float TX = FMath::Clamp(FX - FX0, 0.f, 1.f);
			const float Top = FMath::Lerp(static_cast<float>(MinimapFog[FY0 * FogRes + FX0]), static_cast<float>(MinimapFog[FY0 * FogRes + FX1]), TX);
			const float Bot = FMath::Lerp(static_cast<float>(MinimapFog[FY1 * FogRes + FX0]), static_cast<float>(MinimapFog[FY1 * FogRes + FX1]), TX);
			const float Seen = FMath::Lerp(Top, Bot, TY) / 255.f;
			const FColor& Base = MinimapBase[PY * Res + PX];
			// Unexplored ground keeps a faint outline of the layout (you can plan a route), explored ground is full colour
			MinimapPixels[PY * Res + PX] = Mix(Mix(Unknown, Base, 0.22f), Base, Seen);
		}
	}
}

void ASQHUD::MinimapUpload()
{
	using namespace SQMinimap;
	if (MinimapPixels.Num() != Res * Res) return;
	if (!MinimapTex)
	{
		MinimapTex = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8);
		if (!MinimapTex) return;
		MinimapTex->SRGB = true;
		MinimapTex->Filter = TF_Bilinear;
		MinimapTex->AddressX = TA_Clamp;
		MinimapTex->AddressY = TA_Clamp;
		MinimapTex->UpdateResource();
	}
	// The render thread reads the copy later and frees it (only on a change, never per frame)
	const int32 Bytes = Res * Res * static_cast<int32>(sizeof(FColor));
	uint8* Copy = new uint8[Bytes];
	FMemory::Memcpy(Copy, MinimapPixels.GetData(), Bytes);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Res, Res);
	MinimapTex->UpdateTextureRegions(0, 1u, Region, static_cast<uint32>(Res * sizeof(FColor)), static_cast<uint32>(sizeof(FColor)), Copy,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] SrcData;
			delete Regions;
		});
}

void ASQHUD::MinimapRefreshActors(const UWorld* World)
{
	MinimapActors.Reset();
	for (TActorIterator<ASQChest> It(World); It; ++It) MinimapActors.Add(*It);
	for (TActorIterator<ASQShrine> It(World); It; ++It) MinimapActors.Add(*It);
	for (TActorIterator<ASQPot> It(World); It; ++It) MinimapActors.Add(*It);
	for (TActorIterator<ASQObjective> It(World); It; ++It) MinimapActors.Add(*It);
}

void ASQHUD::MinimapDrawIcons(const ASQGameState* GS, const ASQCharacter* C, const USQData* D, float PX, float PY, float PW, float PH,
	const FVector2D& UV0, const FVector2D& UV1, bool bFull)
{
	const FVector2D UVSpan = UV1 - UV0;
	if (UVSpan.X <= 0.0 || UVSpan.Y <= 0.0) return;
	const float IconK = bFull ? 1.25f : 1.f;
	// Panel position of a world point; false when outside (bPin = clamp to the border instead)
	auto ToPanel = [&](const FVector& W, bool bPin, float& OutX, float& OutY) -> bool
	{
		const FVector2D UV = MinimapUV(static_cast<float>(W.X), static_cast<float>(W.Y));
		float NX = static_cast<float>((UV.X - UV0.X) / UVSpan.X);
		float NY = static_cast<float>((UV.Y - UV0.Y) / UVSpan.Y);
		const bool bInside = NX >= 0.f && NX <= 1.f && NY >= 0.f && NY <= 1.f;
		if (!bInside && !bPin) return false;
		NX = FMath::Clamp(NX, 0.04f, 0.96f);
		NY = FMath::Clamp(NY, 0.04f, 0.96f);
		OutX = PX + NX * PW;
		OutY = PY + NY * PH;
		return true;
	};
	const float Pulse = HudPulse(HudNow, 5.f);

	// --- Pots, chests, shrines, objectives
	for (const TWeakObjectPtr<AActor>& Weak : MinimapActors)
	{
		const AActor* A = Weak.Get();
		if (!A) continue;
		float IX = 0.f, IY = 0.f;
		if (const ASQPot* Pot = Cast<ASQPot>(A))
		{
			if (Pot->bSmashed || !ToPanel(A->GetActorLocation(), false, IX, IY)) continue;
			HudDisc(SQUI::WithAlpha(SQUI::BoneWhite, 0.55f), IX, IY, 1.6f * IconK);
		}
		else if (const ASQChest* Chest = Cast<ASQChest>(A))
		{
			if (Chest->bOpened || !ToPanel(A->GetActorLocation(), false, IX, IY)) continue;
			const float S = (Chest->bFree ? 7.f : 6.f) * IconK;
			HudRect(SQUI::Midnight, IX - S * 0.5f - 1.f, IY - S * 0.5f - 1.f, S + 2.f, S + 2.f);
			HudRect(Chest->bFree ? FMath::Lerp(SQUI::Gold, FLinearColor::White, 0.4f * Pulse) : SQUI::Gold, IX - S * 0.5f, IY - S * 0.5f, S, S);
		}
		else if (const ASQShrine* Shrine = Cast<ASQShrine>(A))
		{
			const ESQShrineState St = Shrine->GetShrineState();
			if (St == ESQShrineState::Buried || !ToPanel(A->GetActorLocation(), false, IX, IY)) continue;
			const bool bSpent = St == ESQShrineState::Spent || St == ESQShrineState::Locked;
			const float S = (bSpent ? 4.f : 7.f) * IconK;
			HudTilt(SQUI::Midnight, IX - S * 0.5f - 1.f, IY - S * 0.5f - 1.f, S + 2.f, S + 2.f, 45.f);
			HudTilt(bSpent ? SQUI::Disabled : FLinearColor(0.35f, 0.95f, 1.f), IX - S * 0.5f, IY - S * 0.5f, S, S, 45.f);
		}
		else if (const ASQObjective* Obj = Cast<ASQObjective>(A))
		{
			if (Obj->bDone || !ToPanel(A->GetActorLocation(), true, IX, IY)) continue;
			const bool bNest = Obj->Part == static_cast<uint8>(ESQObjectivePart::Nest);
			const FLinearColor ObjColour = bNest ? FLinearColor(1.f, 0.42f, 0.2f) : FLinearColor(0.4f, 1.f, 0.5f);
			const float R = (5.f + 1.5f * Pulse) * IconK;
			HudDisc(SQUI::Midnight, IX, IY, R + 1.5f);
			HudDisc(ObjColour, IX, IY, R);
			HudDisc(FLinearColor::White, IX, IY, R * 0.38f);
		}
	}

	// --- Lantern trail: spent lanterns (grey), ones still dark (hollow), the lit one (amber, pinned to the border with a pointer)
	float IX = 0.f, IY = 0.f;
	for (int32 SiteIdx = 0; SiteIdx < GS->TrailSites.Num(); ++SiteIdx)
	{
		const bool bLit = SiteIdx == GS->TrailStage;
		if (!ToPanel(GS->TrailSites[SiteIdx], bLit, IX, IY)) continue;
		if (bLit)
		{
			const float R = (6.5f + 2.f * Pulse) * IconK;
			if (C)
			{
				// A line from you towards it: the way to go at a glance
				float MeX = 0.f, MeY = 0.f;
				if (ToPanel(C->GetActorLocation(), true, MeX, MeY)) HudLine(MeX, MeY, IX, IY, 1.5f, SQUI::WithAlpha(SQUI::Amber, 0.55f));
			}
			HudDisc(SQUI::Midnight, IX, IY, R + 2.f);
			HudDisc(SQUI::Amber, IX, IY, R);
			HudRect(SQUI::Midnight, IX - R * 0.28f, IY - R * 0.55f, R * 0.56f, R * 1.1f);
			HudRect(FLinearColor(1.f, 0.95f, 0.7f), IX - R * 0.16f, IY - R * 0.35f, R * 0.32f, R * 0.7f);
		}
		else
		{
			const bool bSpent = SiteIdx < GS->TrailStage;
			const bool bMissed = (GS->TrailMissedMask & (1 << FMath::Clamp(SiteIdx, 0, 30))) != 0;
			const float R = 4.f * IconK;
			HudDisc(SQUI::Midnight, IX, IY, R + 1.5f);
			HudDisc(bSpent ? (bMissed ? SQUI::Disabled : SQUI::WithAlpha(SQUI::Amber, 0.55f)) : SQUI::Muted, IX, IY, R);
			if (!bSpent) HudDisc(SQUI::Midnight, IX, IY, R * 0.55f);
		}
	}

	// --- The bell (portal) and the boss: pinned to the border when off the panel
	if (GS->Portal && ToPanel(GS->Portal->GetActorLocation(), true, IX, IY))
	{
		const float R = (GS->Portal->bArmed ? 7.f + 2.f * Pulse : 6.f) * IconK;
		HudDisc(SQUI::Midnight, IX, IY, R + 2.f);
		HudDisc(FLinearColor(0.95f, 0.15f, 0.25f), IX, IY, R);
		HudDisc(SQUI::Midnight, IX, IY, R * 0.5f);
	}
	if (GS->Boss && IsValid(GS->Boss) && ToPanel(GS->Boss->GetActorLocation(), true, IX, IY))
	{
		const float R = (8.f + 2.f * Pulse) * IconK;
		HudDisc(SQUI::Midnight, IX, IY, R + 2.f);
		HudDisc(FLinearColor(1.f, 0.12f, 0.18f), IX, IY, R);
		HudTxt(TEXT("!"), HudLabelFont(true), R * 1.5f, IX, IY - R * 0.85f, FLinearColor::White, 1, false);
	}

	// --- Squad: teammates as dots in their reaper colour, you as a white arrow showing your facing
	for (const APlayerState* AnyPS : GS->PlayerArray)
	{
		const ASQCharacter* Mate = AnyPS ? Cast<ASQCharacter>(AnyPS->GetPawn()) : nullptr;
		if (!Mate || Mate == C) continue;
		if (!ToPanel(Mate->GetActorLocation(), true, IX, IY)) continue;
		const FSQReaperDef* MateDef = D ? D->FindReaper(Mate->ReaperId) : nullptr;
		const FLinearColor MateColour = Mate->bDowned ? SQUI::Danger : (MateDef ? MateDef->Color : SQUI::Soul);
		HudDisc(SQUI::Midnight, IX, IY, 5.5f * IconK);
		HudDisc(MateColour, IX, IY, 4.f * IconK);
	}
	if (C && ToPanel(C->GetActorLocation(), true, IX, IY))
	{
		const float ScreenDeg = static_cast<float>(C->GetActorRotation().Yaw) - MinimapYaw - 90.f;
		const float Rad = FMath::DegreesToRadians(ScreenDeg);
		// The map is north-up: a faint wedge shows which way the chase camera is looking
		{
			const float ViewDeg = C->GetViewYaw() - MinimapYaw - 90.f;
			for (int32 WedgeIdx = -1; WedgeIdx <= 1; WedgeIdx += 2)
			{
				const float EdgeRad = FMath::DegreesToRadians(ViewDeg + 32.f * WedgeIdx);
				HudLine(IX, IY, IX + FMath::Cos(EdgeRad) * 30.f * IconK, IY + FMath::Sin(EdgeRad) * 30.f * IconK, 1.5f, SQUI::WithAlpha(SQUI::BoneWhite, 0.45f));
			}
		}
		HudDisc(SQUI::Midnight, IX, IY, 6.5f * IconK);
		HudDisc(FLinearColor::White, IX, IY, 4.5f * IconK);
		HudArrow(IX + FMath::Cos(Rad) * 11.f * IconK, IY + FMath::Sin(Rad) * 11.f * IconK, ScreenDeg, 7.f * IconK, FLinearColor::White);
	}
}

void ASQHUD::DrawDistrictBanner(const ASQGameState* GS, const ASQCharacter* C)
{
	const ASQArena* Arena = GS->Arena;
	if (!Arena || !C) return;
	const FVector L = C->GetActorLocation();
	const int32 Now = Arena->FindDistrictAt(static_cast<float>(L.X), static_cast<float>(L.Y));
	if (Now != DistrictSeen)
	{
		DistrictSeen = Now;
		if (Arena->GetDistricts().IsValidIndex(Now))
		{
			DistrictBannerAt = HudNow;
			DistrictBannerName = Arena->GetDistricts()[Now].Name.ToUpper();
			DistrictBannerColour = Arena->GetDistricts()[Now].Color;
		}
	}
	const float Age = HudNow - DistrictBannerAt;
	const float Life = 2.8f;
	if (Age < 0.f || Age > Life || DistrictBannerName.IsEmpty()) return;
	const float In = FMath::Clamp(Age / 0.25f, 0.f, 1.f);
	const float Out = FMath::Clamp((Life - Age) / 0.5f, 0.f, 1.f);
	const float SavedAlpha = HudAlpha;
	HudAlpha = SavedAlpha * In * Out;
	const float CX = HudVW * 0.5f;
	const float Y = HudVH * 0.26f - 10.f * (1.f - HudEaseOut(In));
	const float NamePx = 34.f;
	const float W = HudTxtW(DistrictBannerName, HudHeadFont(), NamePx) + 80.f;
	HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.72f), CX - W * 0.5f, Y - 8.f, W, 62.f);
	HudRect(DistrictBannerColour, CX - W * 0.5f, Y + 52.f, W, 3.f);
	HudTxt(TEXT("ENTERING"), HudLabelFont(true), 13.f, CX, Y - 4.f, SQUI::Muted, 1);
	HudGlowTxt(DistrictBannerName, HudHeadFont(), NamePx, CX, Y + 10.f, DistrictBannerColour, SQUI::WithAlpha(DistrictBannerColour, 0.5f), 1);
	HudAlpha = SavedAlpha;
}

void ASQHUD::DrawMinimap(const ASQGameState* GS, const ASQCharacter* C, const USQData* D, bool bShop)
{
	const ASQArena* Arena = GS->Arena;
	UWorld* World = GetWorld();
	if (!Arena || !World || !Arena->HasTerrain()) return;
	if (MinimapArena.Get() != Arena || MinimapSerial != Arena->GetBuildSerial())
	{
		MinimapArena = Arena;
		MinimapSerial = Arena->GetBuildSerial();
		MinimapRebuildBase(Arena, D);
		MinimapUpdateFog(GS, true);
		MinimapActorTimer = 0.f;
	}
	MinimapFogTimer -= HudDelta;
	if (MinimapFogTimer <= 0.f)
	{
		MinimapFogTimer = 0.25f;
		MinimapUpdateFog(GS, false);
	}
	MinimapActorTimer -= HudDelta;
	if (MinimapActorTimer <= 0.f)
	{
		MinimapActorTimer = 0.5f;
		MinimapRefreshActors(World);
	}
	if (bMinimapDirty)
	{
		bMinimapDirty = false;
		MinimapCompose();
		MinimapUpload();
	}
	if (!MinimapTex) return;

	// M toggles the full map (not while settings own the keys)
	const ASQPlayerController* MapPC = Cast<ASQPlayerController>(GetOwningPlayerController());
	if (MapPC && !MapPC->IsSettingsOpen() && MapPC->WasInputKeyJustPressed(EKeys::M))
	{
		bFullMap = !bFullMap;
		Play(TEXT("Soul"), 0.2f, 0.f);
	}
	FullMapOpen = FMath::FInterpTo(FullMapOpen, bFullMap ? 1.f : 0.f, HudDelta, 14.f);

	// --- Corner minimap: a window round your reaper (Tuning MinimapRange = cm from the centre to an edge)
	const float Margin = 24.f;
	const float S = D ? FMath::Clamp(D->Tune(TEXT("MinimapSize"), 230.f), 120.f, 420.f) : 230.f;
	const float RangeCm = D ? FMath::Clamp(D->Tune(TEXT("MinimapRange"), 5500.f), 1500.f, MinimapHalf) : 5500.f;
	const float MX = HudVW - Margin - S;
	const float MY = HudVH - Margin - S - (bShop ? 0.f : 18.f) - ((MapPC && MapPC->bShowStats) ? 44.f : 0.f); // clear of the debug lines
	if (FullMapOpen < 0.98f)
	{
		const float SavedAlpha = HudAlpha;
		HudAlpha = SavedAlpha * (1.f - FullMapOpen);
		FVector2D Centre(0.5, 0.5);
		if (C)
		{
			const FVector L = C->GetActorLocation();
			Centre = MinimapUV(static_cast<float>(L.X), static_cast<float>(L.Y));
		}
		const double HalfUV = RangeCm / (MinimapHalf * 2.f);
		const FVector2D UV0 = Centre - FVector2D(HalfUV, HalfUV);
		const FVector2D UV1 = Centre + FVector2D(HalfUV, HalfUV);
		HudPanel(MX - 6.f, MY - 6.f, S + 12.f, S + 12.f, 12.f, SQUI::Line, 0.92f);
		HudTexture(MinimapTex, MX, MY, S, S, FLinearColor::White, static_cast<float>(UV0.X), static_cast<float>(UV0.Y), static_cast<float>(UV1.X), static_cast<float>(UV1.Y));
		MinimapDrawIcons(GS, C, D, MX, MY, S, S, UV0, UV1, false);
		// Under it: the district you're in and the key for the big map
		FString Where = TEXT("LANTERN SQUARE");
		FLinearColor WhereColour = SQUI::BoneWhite;
		if (C)
		{
			const FVector L = C->GetActorLocation();
			const int32 DistrictIdx = Arena->FindDistrictAt(static_cast<float>(L.X), static_cast<float>(L.Y));
			if (Arena->GetDistricts().IsValidIndex(DistrictIdx))
			{
				Where = Arena->GetDistricts()[DistrictIdx].Name.ToUpper();
				WhereColour = Arena->GetDistricts()[DistrictIdx].Color;
			}
			else if (FVector2D(L.X, L.Y).Size() > Arena->GetHubRadius() + 400.f)
			{
				Where = TEXT("THE OUTSKIRTS");
				WhereColour = SQUI::Muted;
			}
		}
		HudTxt(Where, HudLabelFont(true), 13.f, MX + 2.f, MY + S + 6.f, WhereColour, 0);
		HudTxt(TEXT("M  MAP"), HudLabelFont(true), 12.f, MX + S - 2.f, MY + S + 6.f, SQUI::Muted, 2);
		HudAlpha = SavedAlpha;
	}

	// --- Full map (M): the whole arena with district names and a legend; the game keeps running underneath
	if (FullMapOpen > 0.02f)
	{
		const float SavedAlpha = HudAlpha;
		HudAlpha = SavedAlpha * FullMapOpen;
		HudRect(SQUI::WithAlpha(SQUI::Midnight, 0.55f), 0.f, 0.f, HudVW, HudVH);
		const float FS = FMath::Min(HudVH * 0.78f, HudVW * 0.62f);
		const float FX = HudVW * 0.5f - FS * 0.5f;
		const float FY = HudVH * 0.5f - FS * 0.5f + 14.f;
		HudPanel(FX - 10.f, FY - 44.f, FS + 20.f, FS + 84.f, 16.f, SQUI::Line, 0.95f);
		HudTxt(TEXT("MAP"), HudHeadFont(), 26.f, FX, FY - 40.f, SQUI::BoneWhite, 0);
		HudTxt(TEXT("M  CLOSE"), HudLabelFont(true), 13.f, FX + FS, FY - 30.f, SQUI::Muted, 2);
		HudTexture(MinimapTex, FX, FY, FS, FS, FLinearColor::White, 0.f, 0.f, 1.f, 1.f);
		const FVector2D UV0(0.0, 0.0);
		const FVector2D UV1(1.0, 1.0);
		// District names at their centres (only once you've seen them)
		for (const FSQDistrict& Dist : Arena->GetDistricts())
		{
			const FVector2D UV = MinimapUV(static_cast<float>(Dist.Centre.X), static_cast<float>(Dist.Centre.Y));
			const int32 FogX = FMath::Clamp(FMath::FloorToInt(static_cast<float>(UV.X) * SQMinimap::FogRes), 0, SQMinimap::FogRes - 1);
			const int32 FogY = FMath::Clamp(FMath::FloorToInt(static_cast<float>(UV.Y) * SQMinimap::FogRes), 0, SQMinimap::FogRes - 1);
			const bool bSeen = MinimapFog.IsValidIndex(FogY * SQMinimap::FogRes + FogX) && MinimapFog[FogY * SQMinimap::FogRes + FogX] > 0;
			const float NX = FX + static_cast<float>(UV.X) * FS;
			const float NY = FY + static_cast<float>(UV.Y) * FS;
			HudTxt(bSeen ? Dist.Name.ToUpper() : FString(TEXT("???")), HudLabelFont(true), 14.f, NX, NY + 14.f, bSeen ? Dist.Color : SQUI::Disabled, 1);
		}
		MinimapDrawIcons(GS, C, D, FX, FY, FS, FS, UV0, UV1, true);
		// Legend
		float LX = FX;
		const float LY = FY + FS + 12.f;
		auto Legend = [&](const FLinearColor& Colour, const TCHAR* Label, int32 Shape)
		{
			if (Shape == 0) HudDisc(Colour, LX + 6.f, LY + 8.f, 5.f);
			else if (Shape == 1) HudRect(Colour, LX + 1.f, LY + 3.f, 10.f, 10.f);
			else HudTilt(Colour, LX + 1.f, LY + 3.f, 10.f, 10.f, 45.f);
			LX += 18.f;
			LX += HudTxt(Label, HudLabelFont(false), 12.f, LX, LY, SQUI::Muted, 0) + 16.f;
		};
		Legend(FLinearColor::White, TEXT("You"), 0);
		Legend(SQUI::Soul, TEXT("Squad"), 0);
		Legend(SQUI::Amber, TEXT("Lit lantern"), 0);
		Legend(FLinearColor(0.4f, 1.f, 0.5f), TEXT("Objective"), 0);
		Legend(FLinearColor(1.f, 0.42f, 0.2f), TEXT("Nest"), 0);
		Legend(SQUI::Gold, TEXT("Chest"), 1);
		Legend(FLinearColor(0.35f, 0.95f, 1.f), TEXT("Shrine"), 2);
		Legend(FLinearColor(0.95f, 0.15f, 0.25f), TEXT("Bell / Boss"), 0);
		HudAlpha = SavedAlpha;
	}
}
