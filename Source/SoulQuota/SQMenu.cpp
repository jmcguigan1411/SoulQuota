// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQMenu.h"

#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "CanvasItem.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/World.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "SceneView.h"
#include "Sound/SoundBase.h"
#include "SQData.h"
#include "SQMenuShowroom.h"
#include "SQSave.h"
#include "SQUIStyle.h"
#include "SQQuests.h"
#include "SQTutorial.h"
#include "SQMission.h"

class UNetDriver;

namespace
{
	/** A failed join (network or travel failure) leaves a note here; the menu shows it when it opens again. */
	FString MenuPendingNote;
	bool bMenuFailureHooked = false;

	void MenuOnNetworkFailure(UWorld*, UNetDriver*, ENetworkFailure::Type, const FString& ErrorText)
	{
		MenuPendingNote = FString::Printf(TEXT("Couldn't reach that run: %s"), *ErrorText);
	}

	void MenuOnTravelFailure(UWorld*, ETravelFailure::Type, const FString& ErrorText)
	{
		MenuPendingNote = FString::Printf(TEXT("Couldn't start the run: %s"), *ErrorText);
	}

	float MenuEaseOut(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return 1.f - (1.f - C) * (1.f - C) * (1.f - C);
	}

	/** Ease out with a little overshoot. */
	float MenuBackOut(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		const float C1 = 1.70158f;
		const float M = C - 1.f;
		return 1.f + (C1 + 1.f) * M * M * M + C1 * M * M;
	}

	/** 0..1 eased: starts Delay seconds into Elapsed and takes Dur. */
	float MenuAppear(float Elapsed, float Delay, float Dur)
	{
		return MenuEaseOut((Elapsed - Delay) / FMath::Max(0.01f, Dur));
	}

	const FLinearColor MenuWhite(1.f, 1.f, 1.f, 1.f);

	/** One line of the credits: 0 big title, 1 heading, 2 name, 3 small print, 4 space. */
	struct FMenuCreditLine
	{
		uint8 Kind;
		const TCHAR* Text;
	};
}

// ================================================================ game mode, controller

ASQMenuGameMode::ASQMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASQMenuPlayerController::StaticClass();
	HUDClass = ASQMenuHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

ASQMenuPlayerController::ASQMenuPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	bAutoManageActiveCameraTarget = false; // the HUD looks through the showroom camera
}

void ASQMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}

// ================================================================ HUD: setup

ASQMenuHUD::ASQMenuHUD()
{
}

void ASQMenuHUD::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		TimeNow = static_cast<float>(World->GetRealTimeSeconds());
	}
	if (!bMenuFailureHooked && GEngine)
	{
		bMenuFailureHooked = true;
		GEngine->OnNetworkFailure().AddStatic(&MenuOnNetworkFailure);
		GEngine->OnTravelFailure().AddStatic(&MenuOnTravelFailure);
	}
	USQSaveGame* Save = GetSave();
	Address = Save && !Save->LastJoinAddress.IsEmpty() ? Save->LastJoinAddress : FString(TEXT("127.0.0.1"));

	// the music loop (create_audio.py), fading in
	if (USoundBase* Track = SQ::Sound(TEXT("Music")))
	{
		MenuMusic = UGameplayStatics::CreateSound2D(this, Track, 1.f, 1.f, 0.f, nullptr, false, false);
		if (MenuMusic)
		{
			ApplyAudio();
			MenuMusic->FadeIn(2.5f, 1.f);
		}
	}
	ApplyAudio();

	if (!MenuPendingNote.IsEmpty())
	{
		ShowToast(MenuPendingNote, SQUI::Danger);
		MenuPendingNote.Reset();
	}
}

void ASQMenuHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bSettingsDirty)
	{
		StoreSave();
		bSettingsDirty = false;
	}
	if (MenuMusic)
	{
		MenuMusic->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

USQSaveGame* ASQMenuHUD::GetSave()
{
	if (!Profile)
	{
		Profile = USQSaveGame::Load();
	}
	return Profile;
}

void ASQMenuHUD::StoreSave()
{
	if (USQSaveGame* Save = GetSave())
	{
		USQSaveGame::Store(Save);
	}
}

void ASQMenuHUD::ApplyAudio()
{
	const USQSaveGame* Save = GetSave();
	const float MusicVol = Save ? FMath::Clamp(Save->MusicVolume, 0.f, 1.f) : 0.45f;
	// master: the whole game's sound (the audio device's transient primary volume stays set after the level changes)
	SQSettings::ApplyMasterVolume(GetWorld(), Save);
	if (MenuMusic)
	{
		MenuMusic->SetVolumeMultiplier(FMath::Max(0.001f, MusicVol)); // never 0: a silent sound can be stopped for good
	}
}

void ASQMenuHUD::PlayUI(const TCHAR* SoundName, float Volume, float Pitch)
{
	if (USoundBase* Snd = SQ::Sound(SoundName))
	{
		UGameplayStatics::PlaySound2D(this, Snd, Volume, Pitch);
	}
}

void ASQMenuHUD::ShowToast(const FString& Text, const FLinearColor& Colour)
{
	ToastText = Text;
	ToastColour = Colour;
	ToastTime = TimeNow;
}

FName ASQMenuHUD::PreferredReaper()
{
	const USQData* D = USQData::Get(this);
	const USQSaveGame* Save = GetSave();
	if (!D || D->Reapers.Num() == 0)
	{
		return TEXT("Grim");
	}
	if (Save && !Save->PreferredReaper.IsNone() && D->FindReaper(Save->PreferredReaper) && !IsReaperLocked(Save->PreferredReaper))
	{
		return Save->PreferredReaper;
	}
	for (const FSQReaperDef& R : D->Reapers)
	{
		if (!IsReaperLocked(R.Id))
		{
			return R.Id;
		}
	}
	return D->Reapers[0].Id;
}

bool ASQMenuHUD::IsReaperLocked(FName ReaperId)
{
	// same rule as the lobby (ASQPlayerController::Choose)
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* R = D ? D->FindReaper(ReaperId) : nullptr;
	if (!R)
	{
		return true;
	}
	if (D->Tune(TEXT("UnlockAllReapers"), 0.f) > 0.5f)
	{
		return false;
	}
	const USQSaveGame* Save = GetSave();
	return R->UnlockCost > 0 && !(Save && Save->HasReaper(ReaperId));
}

bool ASQMenuHUD::IsBiomeOpen(FName BiomeId)
{
	// same rule as ASQGameMode::IsBiomeUnlocked (the host's profile)
	const USQData* D = USQData::Get(this);
	const FSQBiomeDef* B = D ? D->FindBiome(BiomeId) : nullptr;
	const USQSaveGame* Save = GetSave();
	if (!B || !Save)
	{
		return false;
	}
	if (D->Tune(TEXT("UnlockAllBiomes"), 0.f) > 0.5f)
	{
		return true;
	}
	if (B->bRequiresAllContracts)
	{
		for (const FSQBiomeDef& Other : D->Biomes)
		{
			if (Other.Id != B->Id && !Other.bRequiresAllContracts && !Save->HasContract(Other.Id))
			{
				return false;
			}
		}
		return true;
	}
	return B->RequiresContract.IsNone() || Save->HasContract(B->RequiresContract);
}

void ASQMenuHUD::ContractCount(int32& OutHave, int32& OutTotal)
{
	OutHave = 0;
	OutTotal = 0;
	const USQData* D = USQData::Get(this);
	const USQSaveGame* Save = GetSave();
	if (!D)
	{
		return;
	}
	for (const FSQBiomeDef& B : D->Biomes)
	{
		if (!B.bRequiresAllContracts)
		{
			++OutTotal;
			OutHave += Save && Save->HasContract(B.Id) ? 1 : 0;
		}
	}
}

// ================================================================ drawing helpers

UFont* ASQMenuHUD::MakeFont(UFontFace* Face, int32 Size)
{
	if (!Face)
	{
		return nullptr;
	}
	UFont* F = NewObject<UFont>(this);
	F->FontCacheType = EFontCacheType::Runtime;
	F->LegacyFontSize = Size;
	FTypefaceEntry& Entry = F->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
	Entry.Name = TEXT("Regular");
	Entry.Font = FFontData(Face);
	return F;
}

void ASQMenuHUD::LoadFonts()
{
	if (bFontsLoaded)
	{
		return;
	}
	bFontsLoaded = true;
	auto LoadFace = [](const TCHAR* Path) { return LoadObject<UFontFace>(nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn); };
	CreepsterFont = MakeFont(LoadFace(TEXT("/Game/UI/Fonts/FF_Creepster.FF_Creepster")), 64);
	LilitaFont = MakeFont(LoadFace(TEXT("/Game/UI/Fonts/FF_LilitaOne.FF_LilitaOne")), 48);
	RubikMedium = MakeFont(LoadFace(TEXT("/Game/UI/Fonts/FF_Rubik500.FF_Rubik500")), 40);
	RubikBold = MakeFont(LoadFace(TEXT("/Game/UI/Fonts/FF_Rubik700.FF_Rubik700")), 40);
}

UFont* ASQMenuHUD::HeadFont() const
{
	if (LilitaFont)
	{
		return LilitaFont.Get();
	}
	return GEngine ? GEngine->GetLargeFont() : nullptr;
}

UFont* ASQMenuHUD::DisplayFont() const
{
	return CreepsterFont ? CreepsterFont.Get() : HeadFont();
}

UFont* ASQMenuHUD::LabelFont(bool bBold) const
{
	UFont* F = bBold ? RubikBold.Get() : RubikMedium.Get();
	if (!F)
	{
		F = bBold ? RubikMedium.Get() : RubikBold.Get();
	}
	return F ? F : HeadFont();
}

float ASQMenuHUD::TxtWidth(const FString& Text, UFont* Font, float InPx)
{
	if (!Font || !Canvas)
	{
		return 0.f;
	}
	const float Px = SQUI::ReadablePx(InPx, UnitPx);
	float& LineAtOne = FontLine.FindOrAdd(Font);
	if (LineAtOne <= 0.f)
	{
		float MeasureW = 0.f;
		GetTextSize(TEXT("Hg"), MeasureW, LineAtOne, Font, 1.f);
		LineAtOne = FMath::Max(LineAtOne, 1.f);
	}
	const float TextScale = Px * UnitPx * 1.2f / LineAtOne;
	float OutW = 0.f, OutH = 0.f;
	GetTextSize(Text, OutW, OutH, Font, TextScale);
	return OutW;
}

float ASQMenuHUD::Txt(const FString& Text, UFont* Font, float InPx, float X, float Y, const FLinearColor& Colour, int32 Align, bool bShadow)
{
	if (!Font || Text.IsEmpty())
	{
		return 0.f;
	}
	const float TW = TxtWidth(Text, Font, InPx); // also measures the font
	const float Px = SQUI::ReadablePx(InPx, UnitPx);
	const float TextScale = Px * UnitPx * 1.2f / FMath::Max(FontLine.FindRef(Font), 1.f);
	const float LX = Align == 1 ? X - TW * 0.5f : Align == 2 ? X - TW : X;
	const float A = Colour.A * DrawAlpha;
	if (bShadow)
	{
		DrawText(Text, FLinearColor(0.02f, 0.01f, 0.04f, 0.55f * A), LX - 1.f, Y, Font, TextScale);
		DrawText(Text, FLinearColor(0.02f, 0.01f, 0.04f, 0.55f * A), LX + 1.f, Y, Font, TextScale);
		DrawText(Text, FLinearColor(0.f, 0.f, 0.f, 0.6f * A), LX + 2.f * UnitPx, Y + 3.f * UnitPx, Font, TextScale);
	}
	else if (A > 0.05f)
	{
		// even "flat" menu text gets a faint 1 px drop so thin glyphs don't melt into lighter panels
		DrawText(Text, FLinearColor(0.f, 0.f, 0.f, 0.35f * A), LX + 1.f, Y + 1.f, Font, TextScale);
	}
	DrawText(Text, SQUI::WithAlpha(Colour, A), LX, Y, Font, TextScale);
	return TW;
}

float ASQMenuHUD::FitPx(const FString& Text, UFont* Font, float Px, float MaxW)
{
	const float TW = TxtWidth(Text, Font, Px);
	return TW > MaxW && TW > 0.f ? Px * MaxW / TW : Px;
}

int32 ASQMenuHUD::TxtWrap(const FString& Text, UFont* Font, float Px, float X, float Y, float MaxW, const FLinearColor& Colour, float LineH, int32 MaxLines,
	bool bDraw)
{
	LineH = FMath::Max(LineH, SQUI::ReadablePx(Px, UnitPx) * UnitPx * 1.12f); // lifted small text keeps its lines apart
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), true);
	FString Current;
	int32 Lines = 0;
	for (int32 i = 0; i < Words.Num(); ++i)
	{
		const FString Try = Current.IsEmpty() ? Words[i] : Current + TEXT(" ") + Words[i];
		if (!Current.IsEmpty() && TxtWidth(Try, Font, Px) > MaxW)
		{
			if (Lines == MaxLines - 1)
			{
				// last line allowed: cut it with an ellipsis
				FString Cut = Current + TEXT("...");
				while (Cut.Len() > 4 && TxtWidth(Cut, Font, Px) > MaxW)
				{
					Cut = Cut.LeftChop(4) + TEXT("...");
				}
				if (bDraw)
				{
					Txt(Cut, Font, Px, X, Y + Lines * LineH, Colour);
				}
				return MaxLines;
			}
			if (bDraw)
			{
				Txt(Current, Font, Px, X, Y + Lines * LineH, Colour);
			}
			++Lines;
			Current = Words[i];
		}
		else
		{
			Current = Try;
		}
	}
	if (!Current.IsEmpty())
	{
		if (bDraw)
		{
			Txt(Current, Font, Px, X, Y + Lines * LineH, Colour);
		}
		++Lines;
	}
	return Lines;
}

void ASQMenuHUD::Rect(const FLinearColor& Colour, float X, float Y, float W, float H)
{
	if (W <= 0.f || H <= 0.f)
	{
		return;
	}
	DrawRect(SQUI::WithAlpha(Colour, Colour.A * DrawAlpha), X, Y, W, H);
}

void ASQMenuHUD::Round(const FLinearColor& Colour, float X, float Y, float W, float H, float R)
{
	if (W <= 0.f || H <= 0.f)
	{
		return;
	}
	R = FMath::Min(R, FMath::Min(W, H) * 0.5f);
	if (R < 1.5f)
	{
		Rect(Colour, X, Y, W, H);
		return;
	}
	Rect(Colour, X, Y + R, W, H - 2.f * R);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(R / 1.5f), 2, 16);
	const float RowH = R / Steps;
	for (int32 i = 0; i < Steps; ++i)
	{
		const float DY = R - (i + 0.5f) * RowH;
		const float Inset = R - FMath::Sqrt(FMath::Max(0.f, R * R - DY * DY));
		Rect(Colour, X + Inset, Y + i * RowH, W - 2.f * Inset, RowH);
		Rect(Colour, X + Inset, Y + H - (i + 1) * RowH, W - 2.f * Inset, RowH);
	}
}

void ASQMenuHUD::GlowAround(const FLinearColor& Colour, float X, float Y, float W, float H, float R, float Strength)
{
	if (Strength <= 0.01f)
	{
		return;
	}
	constexpr int32 Rings = 6;
	for (int32 k = Rings; k >= 1; --k)
	{
		const float Grow = k * 3.f * UnitPx;
		const float A = Strength * 0.09f * (1.f - float(k) / (Rings + 1));
		Round(SQUI::WithAlpha(Colour, A), X - Grow, Y - Grow, W + 2.f * Grow, H + 2.f * Grow, R + Grow);
	}
}

void ASQMenuHUD::PanelBox(float X, float Y, float W, float H)
{
	const float U = UnitPx;
	Round(FLinearColor(0.f, 0.f, 0.f, 0.4f), X + 8.f * U, Y + 12.f * U, W, H, 24.f * U);
	Round(SQUI::Line, X - 2.f * U, Y - 2.f * U, W + 4.f * U, H + 4.f * U, 26.f * U);
	Round(SQUI::WithAlpha(SQUI::Panel, 0.97f), X, Y, W, H, 24.f * U);
}

float ASQMenuHUD::KeyCap(const FString& Key, float X, float Y)
{
	const float U = UnitPx;
	const float KW = TxtWidth(Key, LabelFont(true), 13.f) + 18.f * U;
	Round(SQUI::Line, X, Y, KW, 26.f * U, 6.f * U);
	Round(SQUI::PanelHover, X + 2.f * U, Y + 1.5f * U, KW - 4.f * U, 20.f * U, 5.f * U);
	Txt(Key, LabelFont(true), 13.f, X + KW * 0.5f, Y + 3.f * U, SQUI::BoneWhite, 1);
	return KW;
}

void ASQMenuHUD::TiltTile(const FLinearColor& Colour, float X, float Y, float W, float H, float Deg, float PivotX, float PivotY)
{
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), SQUI::WithAlpha(Colour, Colour.A * DrawAlpha));
	Tile.Rotation = FRotator(0.f, Deg, 0.f);
	Tile.PivotPoint = FVector2D(PivotX, PivotY);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ASQMenuHUD::Padlock(float CX, float CY, float Size, const FLinearColor& Colour)
{
	const float S = Size;
	Round(Colour, CX - S * 0.28f, CY - S * 0.48f, S * 0.56f, S * 0.14f, S * 0.07f);   // shackle top
	Rect(Colour, CX - S * 0.28f, CY - S * 0.42f, S * 0.11f, S * 0.4f);                 // shackle sides
	Rect(Colour, CX + S * 0.17f, CY - S * 0.42f, S * 0.11f, S * 0.4f);
	Round(Colour, CX - S * 0.4f, CY - S * 0.06f, S * 0.8f, S * 0.56f, S * 0.1f);       // body
	Round(SQUI::Midnight, CX - S * 0.05f, CY + S * 0.08f, S * 0.1f, S * 0.2f, S * 0.05f); // keyhole
}

void ASQMenuHUD::StatBar(const FString& Label, const FString& Value, float Fill, float X, float Y, float W, const FLinearColor& Colour)
{
	const float U = UnitPx;
	Txt(Label, LabelFont(true), 12.f, X, Y, SQUI::Muted);
	Txt(Value, LabelFont(true), 12.f, X + W, Y, SQUI::BoneWhite, 2);
	Round(SQUI::Midnight, X, Y + 19.f * U, W, 7.f * U, 3.5f * U);
	Round(Colour, X, Y + 19.f * U, FMath::Max(7.f * U, W * FMath::Clamp(Fill, 0.f, 1.f)), 7.f * U, 3.5f * U);
}

bool ASQMenuHUD::MouseIn(float X, float Y, float W, float H) const
{
	return MouseX >= X && MouseX <= X + W && MouseY >= Y && MouseY <= Y + H;
}

void ASQMenuHUD::ShineSweep(float X, float Y, float W, float H, float Period, float Inset)
{
	const float U = UnitPx;
	const float P = FMath::Fmod(TimeNow, Period) / Period;
	const float BandW = 60.f * U;
	const float Slant = 0.5f;
	const float Travel = W + BandW + H * Slant;
	const float BX = X - BandW - H * Slant + P * 1.8f * Travel; // sweeps in the first ~55%, then rests
	constexpr int32 Rows = 12;
	const float RowH = (H - 2.f * Inset) / Rows;
	for (int32 r = 0; r < Rows; ++r)
	{
		const float RY = Y + Inset + r * RowH;
		const float Shift = (H - (RY - Y)) * Slant;
		const float L = FMath::Max(BX + Shift, X + Inset + 8.f * U);
		const float Rt = FMath::Min(BX + Shift + BandW, X + W - Inset - 8.f * U);
		if (Rt > L)
		{
			Rect(FLinearColor(1.f, 1.f, 1.f, 0.3f), L, RY, Rt - L, RowH + 0.5f);
		}
	}
}

// ================================================================ input

bool ASQMenuHUD::NavKey(APlayerController* PC, int32 Dir, const FKey* Keys, int32 NumKeys)
{
	bool bJust = false;
	bool bHeld = false;
	for (int32 i = 0; i < NumKeys; ++i)
	{
		bJust |= PC->WasInputKeyJustPressed(Keys[i]);
		bHeld |= PC->IsInputKeyDown(Keys[i]);
	}
	if (bJust)
	{
		NavHold[Dir] = 0.f;
		return true;
	}
	if (!bHeld)
	{
		NavHold[Dir] = 0.f;
		return false;
	}
	// held: repeats after a moment
	constexpr float RepeatDelay = 0.4f;
	constexpr float RepeatEvery = 0.08f;
	const float Before = NavHold[Dir];
	NavHold[Dir] += DeltaT;
	if (NavHold[Dir] < RepeatDelay)
	{
		return false;
	}
	return Before < RepeatDelay || FMath::FloorToInt((NavHold[Dir] - RepeatDelay) / RepeatEvery) != FMath::FloorToInt((Before - RepeatDelay) / RepeatEvery);
}

void ASQMenuHUD::ReadInput(APlayerController* PC)
{
	float NewX = MouseX, NewY = MouseY;
	if (PC->GetMousePosition(NewX, NewY))
	{
		// Whole-viewport pixels -> this canvas (translated to the view rect: locked-aspect bars / split screen)
		if (Canvas && Canvas->SceneView)
		{
			NewX -= static_cast<float>(Canvas->SceneView->UnscaledViewRect.Min.X);
			NewY -= static_cast<float>(Canvas->SceneView->UnscaledViewRect.Min.Y);
		}
		bMouseMoved = FMath::Abs(NewX - MouseX) + FMath::Abs(NewY - MouseY) > 2.f;
		MouseX = NewX;
		MouseY = NewY;
	}
	else
	{
		bMouseMoved = false;
	}
	bClick = PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	bMouseHeld = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	bWheelUp = PC->WasInputKeyJustPressed(EKeys::MouseScrollUp);
	bWheelDown = PC->WasInputKeyJustPressed(EKeys::MouseScrollDown);

	static const FKey UpKeys[] = { EKeys::Up, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up };
	static const FKey DownKeys[] = { EKeys::Down, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down };
	static const FKey LeftKeys[] = { EKeys::Left, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left };
	static const FKey RightKeys[] = { EKeys::Right, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right };
	bNavUp = NavKey(PC, 0, UpKeys, static_cast<int32>(UE_ARRAY_COUNT(UpKeys)));
	bNavDown = NavKey(PC, 1, DownKeys, static_cast<int32>(UE_ARRAY_COUNT(DownKeys)));
	bNavLeft = NavKey(PC, 2, LeftKeys, static_cast<int32>(UE_ARRAY_COUNT(LeftKeys)));
	bNavRight = NavKey(PC, 3, RightKeys, static_cast<int32>(UE_ARRAY_COUNT(RightKeys)));
	bConfirm = PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
	bBack = PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);

	// which hints to show: the pad's or the keyboard's
	static const FKey PadKeys[] = { EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
		EKeys::Gamepad_LeftStick_Up, EKeys::Gamepad_LeftStick_Down, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right };
	for (const FKey& PadKey : PadKeys)
	{
		if (PC->WasInputKeyJustPressed(PadKey))
		{
			bUsingPad = true;
			break;
		}
	}
	if (bMouseMoved || bClick || PC->WasInputKeyJustPressed(EKeys::Up) || PC->WasInputKeyJustPressed(EKeys::Down)
		|| PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		bUsingPad = false;
	}
}

void ASQMenuHUD::TypeAddress(APlayerController* PC)
{
	static const FKey DigitKeys[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven,
		EKeys::Eight, EKeys::Nine };
	static const FKey NumPadKeys[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
		EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	static const FKey LetterKeys[] = { EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J,
		EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W,
		EKeys::X, EKeys::Y, EKeys::Z };
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	const int32 LenBefore = Address.Len();
	auto Add = [this](TCHAR Ch)
	{
		if (Address.Len() < 64)
		{
			Address.AppendChar(Ch);
		}
	};
	for (int32 i = 0; i < 10; ++i)
	{
		if ((!bShift && PC->WasInputKeyJustPressed(DigitKeys[i])) || PC->WasInputKeyJustPressed(NumPadKeys[i]))
		{
			Add(static_cast<TCHAR>(TEXT('0') + i));
		}
	}
	for (int32 i = 0; i < 26; ++i)
	{
		if (PC->WasInputKeyJustPressed(LetterKeys[i]))
		{
			Add(static_cast<TCHAR>(TEXT('a') + i));
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::Period) || PC->WasInputKeyJustPressed(EKeys::Decimal))
	{
		Add(TEXT('.'));
	}
	if (PC->WasInputKeyJustPressed(EKeys::Hyphen) || PC->WasInputKeyJustPressed(EKeys::Subtract))
	{
		Add(TEXT('-'));
	}
	if (bShift && PC->WasInputKeyJustPressed(EKeys::Semicolon))
	{
		Add(TEXT(':')); // host:port
	}
	if (PC->WasInputKeyJustPressed(EKeys::Delete))
	{
		Address.Reset();
	}
	// Backspace, repeating while held
	const bool bBackspace = PC->IsInputKeyDown(EKeys::BackSpace);
	bool bErase = PC->WasInputKeyJustPressed(EKeys::BackSpace);
	if (bBackspace && !bErase)
	{
		const float Before = BackspaceHold;
		BackspaceHold += DeltaT;
		bErase = BackspaceHold > 0.4f && FMath::FloorToInt((BackspaceHold - 0.4f) / 0.05f) != FMath::FloorToInt((Before - 0.4f) / 0.05f);
	}
	else if (!bBackspace)
	{
		BackspaceHold = 0.f;
	}
	if (bErase && Address.Len() > 0)
	{
		Address.LeftChopInline(1);
	}
	if (Address.Len() != LenBefore)
	{
		PlayUI(TEXT("Soul"), 0.1f, 1.7f);
	}
}

// ================================================================ the frame

void ASQMenuHUD::OpenPage(ESQMenuPage NewPage)
{
	if (NewPage == Page)
	{
		return;
	}
	if (Page == ESQMenuPage::Settings && bSettingsDirty)
	{
		StoreSave();
		bSettingsDirty = false;
	}
	if (Page == ESQMenuPage::Quests)
	{
		// the NEW badges have been seen
		if (USQSaveGame* SeenSave = GetSave())
		{
			if (SeenSave->QuestsNew.Num() > 0)
			{
				SeenSave->QuestsNew.Reset();
				StoreSave();
			}
		}
	}
	PrevPage = Page;
	Page = NewPage;
	PageChangeTime = TimeNow;
	PageFocus = 0;
	if (NewPage == ESQMenuPage::Settings)
	{
		MenuSettings.OpenedAt = -1.f; // its open animation plays
		MenuSettings.Drag = -1;
		MenuSettings.Focus = 0;
	}
	PlayUI(TEXT("Dash"), 0.22f, NewPage == ESQMenuPage::Main ? 0.8f : 1.15f);
	if (NewPage == ESQMenuPage::Reapers)
	{
		ReaperPageShown = -1; // the roster opens on your reaper's page without a slide
		// start on your reaper
		if (const USQData* D = USQData::Get(this))
		{
			const FName Mine = PreferredReaper();
			for (int32 i = 0; i < D->Reapers.Num(); ++i)
			{
				PageFocus = D->Reapers[i].Id == Mine ? i : PageFocus;
			}
		}
	}
	if (NewPage == ESQMenuPage::Credits)
	{
		CreditsScroll = 0.f;
	}
	if (NewPage == ESQMenuPage::Quests)
	{
		QuestRow = 0;
	}
	if (NewPage == ESQMenuPage::Missions)
	{
		// start on the pending mission
		bool bPendingHard = false;
		const FName Pending = USQMissions::GetPendingMission(this, bPendingHard);
		bMissionHard = bPendingHard;
		TArray<FSQMissionView> Missions;
		USQMissions::GetMissionList(USQData::Get(this), GetSave(), Missions);
		for (int32 i = 0; i < Missions.Num(); ++i)
		{
			PageFocus = Missions[i].Id == Pending ? i : PageFocus;
		}
	}
	if (NewPage == ESQMenuPage::Upgrades)
	{
		UpgradeRow = 0;
	}
	if (NewPage == ESQMenuPage::Main && Showroom.IsValid())
	{
		Showroom->ShowReaper(PreferredReaper());
	}
}

void ASQMenuHUD::DrawHUD()
{
	Super::DrawHUD();
	APlayerController* PC = GetOwningPlayerController();
	UWorld* World = GetWorld();
	if (!Canvas || !PC || !World)
	{
		return;
	}
	LoadFonts();
	// Never 0 (a minimised window has a 0-height canvas; several layouts divide by sizes built from this)
	UnitPx = FMath::Max(Canvas->ClipY / 1080.f, 0.05f);
	TimeNow = static_cast<float>(World->GetRealTimeSeconds());
	DeltaT = FMath::Clamp(static_cast<float>(World->GetDeltaSeconds()), 0.f, 0.1f);
	if (MenuOpenedAt < 0.f)
	{
		MenuOpenedAt = TimeNow;
	}
	DrawAlpha = 1.f;
	ReadInput(PC);
	ClockSpin += DeltaT * (25.f + 320.f * MainSlide[0]);

	// the graveyard nook behind the menu
	if (!Showroom.IsValid())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Showroom = World->SpawnActor<ASQMenuShowroom>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (Showroom.IsValid())
		{
			Showroom->ShowReaper(PreferredReaper());
		}
	}
	if (Showroom.IsValid())
	{
		if (PC->GetViewTarget() != Showroom.Get())
		{
			PC->SetViewTarget(Showroom.Get());
		}
		Showroom->SetCloseUp(Page == ESQMenuPage::Reapers && !bTravelling);
	}

	// HOW TO PLAY (the Training Shift board) is up: it takes Esc / Enter / arrows / clicks itself
	const bool bTutOpen = bHowToOpen && !bTravelling;

	// Esc / B: closes the pop-up, goes back a page, or asks to quit on the main page
	if (bBack && !bTravelling && !bTutOpen)
	{
		bBack = false;
		if (bQuitAsk)
		{
			bQuitAsk = false;
			PlayUI(TEXT("Dash"), 0.2f, 0.8f);
		}
		else if (Page != ESQMenuPage::Main)
		{
			OpenPage(ESQMenuPage::Main);
		}
		else
		{
			bQuitAsk = true;
			QuitAskTime = TimeNow;
			QuitFocus = 0;
			PlayUI(TEXT("Soul"), 0.3f, 0.7f);
		}
	}

	DrawBackdrop();

	// while a pop-up is up (or we're clocking in) the page underneath takes no input
	const bool bQuitWasOpen = bQuitAsk; // a pop-up opened this frame doesn't get the click / Enter that opened it
	const float SavedMouseX = MouseX, SavedMouseY = MouseY;
	const bool bSavedClick = bClick, bSavedConfirm = bConfirm, bSavedLeft = bNavLeft, bSavedRight = bNavRight, bSavedUp = bNavUp, bSavedDown = bNavDown;
	if (bQuitAsk || bTravelling || bTutOpen)
	{
		MouseX = MouseY = -10000.f;
		bClick = bConfirm = bNavUp = bNavDown = bNavLeft = bNavRight = bWheelUp = bWheelDown = false;
	}

	switch (Page)
	{
	case ESQMenuPage::Main: DrawMainPage(); break;
	case ESQMenuPage::Join: DrawJoinPage(); break;
	case ESQMenuPage::Reapers: DrawReapersPage(); break;
	case ESQMenuPage::Contracts: DrawContractsPage(); break;
	case ESQMenuPage::Settings: DrawSettingsPage(); break;
	case ESQMenuPage::Credits: DrawCreditsPage(); break;
	case ESQMenuPage::Quests: DrawQuestsPage(); break;
	case ESQMenuPage::Missions: DrawMissionsPage(); break;
	case ESQMenuPage::Upgrades: DrawUpgradesPage(); break;
	default: break;
	}
	DrawAlpha = 1.f;
	DrawBottomBar();
	DrawToast();

	MouseX = SavedMouseX;
	MouseY = SavedMouseY;
	if (bQuitAsk && !bTravelling)
	{
		if (!bQuitWasOpen)
		{
			bClick = bConfirm = bNavUp = bNavDown = bNavLeft = bNavRight = false;
		}
		else
		{
			bClick = bSavedClick;
			bConfirm = bSavedConfirm;
			bNavLeft = bSavedLeft;
			bNavRight = bSavedRight;
			bNavUp = bSavedUp;
			bNavDown = bSavedDown;
		}
		DrawQuitConfirm();
	}
	DrawAlpha = 1.f;
	if (bTutOpen)
	{
		// the board takes this frame's input (the page under it got none)
		bClick = bSavedClick;
		bConfirm = bSavedConfirm;
		bNavLeft = bSavedLeft;
		bNavRight = bSavedRight;
		bNavUp = bSavedUp;
		bNavDown = bSavedDown;
		DrawTutorialLayer();
		DrawAlpha = 1.f;
	}
	DrawInkWipe();
	DrawClockInOverlay();
	DrawAlpha = 1.f;
}

void ASQMenuHUD::FillMenuPainter(FSQTutorialPainter& Painter)
{
	// This menu draws in screen pixels (UnitPx per 1080p pixel); the shared panels work in 1080p design units
	const float U = FMath::Max(UnitPx, 0.01f);
	auto PickFont = [this](ESQTutFont InKind) -> UFont*
	{
		switch (InKind)
		{
		case ESQTutFont::Display: return DisplayFont();
		case ESQTutFont::Head: return HeadFont();
		case ESQTutFont::LabelBold: return LabelFont(true);
		default: return LabelFont(false);
		}
	};
	Painter.Round = [this, U](const FLinearColor& InColour, float InX, float InY, float InW, float InH, float InRadius)
	{
		Round(InColour, InX * U, InY * U, InW * U, InH * U, InRadius * U);
	};
	Painter.Tilt = [this, U](const FLinearColor& InColour, float InX, float InY, float InW, float InH, float InDeg)
	{
		TiltTile(InColour, InX * U, InY * U, InW * U, InH * U, InDeg, 0.5f, 0.5f);
	};
	Painter.Text = [this, U, PickFont](const FString& InStr, ESQTutFont InFont, float InPx, float InX, float InY, const FLinearColor& InColour, int32 InAlign)
	{
		return Txt(InStr, PickFont(InFont), InPx, InX * U, InY * U, InColour, InAlign, true) / U;
	};
	Painter.TextWidth = [this, U, PickFont](const FString& InStr, ESQTutFont InFont, float InPx) { return TxtWidth(InStr, PickFont(InFont), InPx) / U; };
	Painter.Sound = [this](int32 InKind) { PlayUI(TEXT("Soul"), InKind == 1 ? 0.2f : 0.14f, InKind == 1 ? 0.8f : 1.5f); };
	Painter.ViewW = Canvas ? Canvas->ClipX / U : 1920.f;
	Painter.ViewH = Canvas ? Canvas->ClipY / U : 1080.f;
	Painter.Now = TimeNow;
	Painter.Delta = DeltaT;
	Painter.bCursorVisible = true;
	Painter.bHasMouse = MouseX > -1000.f;
	Painter.MouseX = MouseX / U;
	Painter.MouseY = MouseY / U;
	Painter.bClick = bClick;
	Painter.bMouseHeld = bMouseHeld;
	Painter.bNavUp = bNavUp;
	Painter.bNavDown = bNavDown;
	Painter.bNavLeft = bNavLeft;
	Painter.bNavRight = bNavRight;
	Painter.bConfirm = bConfirm;
}

void ASQMenuHUD::DrawTutorialLayer()
{
	USQTutorial* Tut = USQTutorial::Get(this);
	if (!Canvas) return;
	const float U = FMath::Max(UnitPx, 0.01f);
	FSQTutorialPainter Painter;
	FillMenuPainter(Painter);
	const float ViewW = Painter.ViewW, ViewH = Painter.ViewH;
	const float Open = MenuAppear(TimeNow - HowToOpenedAt, 0.f, 0.25f);
	DrawAlpha = 1.f;
	Rect(SQUI::WithAlpha(SQUI::Midnight, 0.9f * Open), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawAlpha = Open;

	// title (Tutorial preview)
	const float CardW = 300.f, Gap = 20.f;
	const float GridW = 4.f * CardW + 3.f * Gap;
	const float X0 = (ViewW - GridW) * 0.5f;
	float CurY = FMath::Max(60.f, ViewH * 0.5f - 330.f) + 16.f * (1.f - Open);
	Painter.Text(TEXT("HOW TO PLAY"), ESQTutFont::LabelBold, 14.f, X0, CurY, SQUI::Soul, 0);
	Painter.Text(TEXT("Three things. That's it."), ESQTutFont::Head, 48.f, X0, CurY + 24.f, SQUI::BoneWhite, 0);
	Painter.Text(TEXT("These three prompts show on your first run. Everything else explains itself with one line the first time you meet it."), ESQTutFont::Label, 18.f,
		X0, CurY + 90.f, SQUI::Muted, 0);
	CurY += 140.f;

	// the 3 prompts, exactly as they show in a run (one per row)
	const float PromptW = FMath::Min(GridW, 700.f);
	for (int32 TipIdx = 0; TipIdx < USQTutorial::NumTrainingTips; ++TipIdx)
	{
		CurY += USQTutorial::DrawTipCard(Painter, TipIdx, X0, CurY, PromptW, false, Open) + Gap;
	}
	CurY += 8.f;
	Painter.Text(TEXT("SPACE jumps, SHIFT dashes, the mouse turns the camera. Your weapons aim themselves."), ESQTutFont::Label, 16.f, X0, CurY, SQUI::BoneWhite, 0);
	Painter.Text(TEXT("Souls, shrines, chests and the bell each get a one-line tip the first time you meet them."), ESQTutFont::Label, 16.f,
		X0, CurY + 24.f, SQUI::Muted, 0);
	CurY += 74.f;

	// PLAY TRAINING SHIFT / CLOSE (Left / Right, Enter; Esc closes)
	const bool bInputOK = TimeNow - HowToOpenedAt > 0.15f;
	if (bInputOK && (bNavLeft || bNavRight))
	{
		HowToFocus = 1 - HowToFocus;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	const float ButH = 64.f, PlayW = 360.f, CloseW = 200.f;
	const bool bSavedClick = bClick;
	const bool bSavedConfirm = bConfirm;
	if (!bInputOK) bClick = bConfirm = false;
	bool bPlay = DrawPageButton(TEXT("SHOW ON MY NEXT RUN"), X0 * U, CurY * U, PlayW * U, ButH * U, 1, true, 0, HowToFocus);
	bool bClose = DrawPageButton(TEXT("CLOSE"), (X0 + PlayW + 16.f) * U, CurY * U, CloseW * U, ButH * U, 0, true, 1, HowToFocus);
	if (bInputOK && bConfirm)
	{
		bPlay = HowToFocus == 0;
		bClose = HowToFocus == 1;
	}
	bClick = bSavedClick;
	bConfirm = bSavedConfirm;
	DrawAlpha = 1.f;
	if (bInputOK && bBack) bClose = true;
	if (bPlay)
	{
		bHowToOpen = false;
		if (Tut) Tut->ArmTrainingReplay();
		ShowToast(TEXT("The 3 how-to-play prompts will show on your next run."), SQUI::Soul);
		StartTravel(false);
	}
	else if (bClose)
	{
		bHowToOpen = false;
		PlayUI(TEXT("Soul"), 0.2f, 0.8f);
	}
}

void ASQMenuHUD::DrawBackdrop()
{
	const float U = UnitPx;
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	if (!Showroom.IsValid())
	{
		Rect(SQUI::Midnight, 0.f, 0.f, CW, CH); // no world to show it in
	}
	// a dark side panel that fades towards the middle, so the reaper stays in view
	auto SideFade = [&](bool bLeft, float Solid, float Fade, float Alpha)
	{
		const FLinearColor Shade = SQUI::WithAlpha(SQUI::Midnight, Alpha);
		Rect(Shade, bLeft ? 0.f : CW - Solid, 0.f, Solid, CH);
		constexpr int32 Bands = 22;
		for (int32 b = 0; b < Bands; ++b)
		{
			const float T = (b + 0.5f) / Bands;
			const float BandX = bLeft ? Solid + Fade * b / Bands : CW - Solid - Fade * (b + 1) / Bands;
			Rect(SQUI::WithAlpha(SQUI::Midnight, Alpha * (1.f - T) * (1.f - T)), BandX, 0.f, Fade / Bands + 1.f, CH);
		}
	};
	if (Page == ESQMenuPage::Main)
	{
		SideFade(true, X0 + 470.f * U, 260.f * U, 0.86f);
		SideFade(false, X0 + 470.f * U, 200.f * U, 0.55f);
	}
	else if (Page == ESQMenuPage::Reapers)
	{
		SideFade(true, X0 + 580.f * U, 160.f * U, 0.86f);
		SideFade(false, X0 + 480.f * U, 160.f * U, 0.86f);
	}
	// a soft band along the top and bottom
	constexpr int32 TopBands = 10;
	for (int32 b = 0; b < TopBands; ++b)
	{
		const float T = (b + 0.5f) / TopBands;
		Rect(SQUI::WithAlpha(SQUI::Midnight, 0.5f * (1.f - T)), 0.f, b * 14.f * U, CW, 14.f * U + 1.f);
		Rect(SQUI::WithAlpha(SQUI::Midnight, 0.5f * (1.f - T)), 0.f, CH - (b + 1) * 14.f * U, CW, 14.f * U + 1.f);
	}
}

// ================================================================ main page

void ASQMenuHUD::DrawTitleBlock(float X, float Y)
{
	const float U = UnitPx;
	// the first time the menu opens it reveals slowly; coming back from a page, quicker
	const bool bFirst = PageChangeTime < MenuOpenedAt;
	const float T = (TimeNow - (bFirst ? MenuOpenedAt : PageChangeTime)) * (bFirst ? 1.f : 2.2f);

	// SOUL: small, spaced, teal, with a soft glow
	{
		const float A = MenuAppear(T, 0.1f, 0.45f);
		const float SoulPx = 42.f;
		float LX = X + 2.f * U;
		const float LY = Y - 14.f * U * (1.f - A);
		const float Pulse = 0.5f + 0.5f * FMath::Sin(TimeNow * 1.8f);
		const FString Word(TEXT("SOUL"));
		for (int32 i = 0; i < Word.Len(); ++i)
		{
			const FString Ch = Word.Mid(i, 1);
			DrawAlpha = A * 0.16f * (0.6f + 0.4f * Pulse);
			for (int32 g = 0; g < 4; ++g)
			{
				const float GX = (g == 0 ? -2.f : g == 1 ? 2.f : 0.f) * U, GY = (g == 2 ? -2.f : g == 3 ? 2.f : 0.f) * U;
				Txt(Ch, HeadFont(), SoulPx, LX + GX, LY + GY, SQUI::Soul);
			}
			DrawAlpha = A;
			LX += Txt(Ch, HeadFont(), SoulPx, LX, LY, SQUI::Soul) + 10.f * U;
		}
		DrawAlpha = 1.f;
	}

	// the time card: unrolls, then QUOTA drips in letter by letter, then OVERTIME is stamped on
	const float QuotaPx = 118.f;
	const FString Quota(TEXT("QUOTA"));
	const float QuotaW = TxtWidth(Quota, DisplayFont(), QuotaPx);
	const FString StampWord(TEXT("DUE BY MIDNIGHT"));
	const float StampPx = 22.f;
	const float StampW = TxtWidth(StampWord, HeadFont(), StampPx) + 30.f * U;
	const float StampH = 40.f * U;
	const float CardX = X - 16.f * U, CardY = Y + 54.f * U, CardH = 124.f * U;
	const float CardW = FMath::Max(470.f * U, QuotaW + StampW + 76.f * U);
	const float Unroll = T < 0.25f ? 0.f : FMath::Min(1.04f, MenuBackOut((T - 0.25f) / 0.55f));
	const float ShownW = CardW * Unroll;
	if (ShownW > 4.f * U)
	{
		Round(FLinearColor(0.f, 0.f, 0.f, 0.45f), CardX + 7.f * U, CardY + 10.f * U, ShownW, CardH, 8.f * U);
		Round(SQUI::Card, CardX, CardY, ShownW, CardH, 8.f * U);
		// ruled lines, punched holes along the top, the clock slot notch on the left
		for (const float RowY : { 50.f, 78.f, 106.f })
		{
			Rect(SQUI::WithAlpha(SQUI::CardInk, 0.1f), CardX + 14.f * U, CardY + RowY * U, ShownW - 28.f * U, 1.5f * U);
		}
		for (float HX = CardX + 20.f * U; HX < CardX + ShownW - 24.f * U; HX += 24.f * U)
		{
			Round(SQUI::WithAlpha(SQUI::Midnight, 0.85f), HX, CardY + 9.f * U, 9.f * U, 9.f * U, 4.5f * U);
		}
		Round(SQUI::Midnight, CardX - 6.f * U, CardY + CardH * 0.5f - 15.f * U, 14.f * U, 30.f * U, 7.f * U);
		if (Unroll > 0.95f)
		{
			DrawAlpha = MenuAppear(T, 0.8f, 0.3f);
			Txt(TEXT("SOUL LEDGER  \u00B7  FORM 13-B  \u00B7  PRIORITY"), LabelFont(true), 11.f, CardX + CardW - 18.f * U, CardY + 24.f * U,
				SQUI::WithAlpha(SQUI::CardInk, 0.55f), 2);
			DrawAlpha = 1.f;
		}
	}
	float LX = X;
	for (int32 i = 0; i < Quota.Len(); ++i)
	{
		const FString Ch = Quota.Mid(i, 1);
		const float A = MenuAppear(T, 0.48f + 0.07f * i, 0.35f);
		if (A > 0.f)
		{
			DrawAlpha = A;
			Txt(Ch, DisplayFont(), QuotaPx, LX, CardY - 12.f * U - 26.f * U * (1.f - A), SQUI::Midnight);
		}
		LX += TxtWidth(Quota.Left(i + 1), DisplayFont(), QuotaPx) - TxtWidth(Quota.Left(i), DisplayFont(), QuotaPx);
	}
	DrawAlpha = 1.f;

	// OVERTIME: a red rubber stamp that thumps on
	const float StampT = (T - 1.05f) / 0.18f;
	if (StampT > 0.f)
	{
		const float P = FMath::Clamp(StampT, 0.f, 1.f);
		const float Grow = 1.6f - 0.6f * MenuEaseOut(P);
		const float SCX = CardX + CardW - StampW * 0.5f - 18.f * U;
		const float SCY = CardY + CardH - StampH * 0.5f - 14.f * U;
		const float SW = StampW * Grow, SH = StampH * Grow, Thick = 3.f * U * Grow;
		DrawAlpha = P * 0.92f;
		const FLinearColor TitleStampInk = SQUI::Stamp;
		Rect(TitleStampInk, SCX - SW * 0.5f, SCY - SH * 0.5f, SW, Thick);
		Rect(TitleStampInk, SCX - SW * 0.5f, SCY + SH * 0.5f - Thick, SW, Thick);
		Rect(TitleStampInk, SCX - SW * 0.5f, SCY - SH * 0.5f, Thick, SH);
		Rect(TitleStampInk, SCX + SW * 0.5f - Thick, SCY - SH * 0.5f, Thick, SH);
		Txt(StampWord, HeadFont(), StampPx * Grow, SCX, SCY - 0.62f * StampPx * Grow * U, TitleStampInk, 1);
		DrawAlpha = 1.f;
		if (P >= 1.f && !bTitleStampSound)
		{
			bTitleStampSound = true;
			PlayUI(TEXT("Hit"), 0.3f, 0.7f);
		}
	}

	// tag line
	DrawAlpha = MenuAppear(T, 0.95f, 0.5f);
	Txt(TEXT("THE DEAD WON'T COLLECT THEMSELVES."), LabelFont(true), 18.f, X + 2.f * U, CardY + CardH + 18.f * U, SQUI::Muted);
	DrawAlpha = 1.f;
}

bool ASQMenuHUD::DrawMainButton(int32 Index, const FString& Label, const FString& Sub, const FLinearColor& SubColour, float X, float Y, float W, float H,
	bool bPrimary, float Appear)
{
	const float U = UnitPx;
	if (Appear <= 0.01f)
	{
		return false;
	}
	DrawAlpha = Appear;
	const float AX = X - 60.f * U * (1.f - Appear);
	const bool bOver = MouseIn(AX, Y, W + 20.f * U, H);
	if (bOver && bMouseMoved && MainFocus != Index)
	{
		MainFocus = Index;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	const bool bFocus = MainFocus == Index;
	float& Slide = MainSlide[FMath::Clamp(Index, 0, 9)];
	Slide = FMath::FInterpTo(Slide, bFocus ? 1.f : 0.f, DeltaT, 14.f);
	const float SX = AX + 16.f * U * Slide;

	if (bPrimary)
	{
		// CLOCK IN: ghost-soul teal, ink text, the shine and a little time clock
		GlowAround(SQUI::Soul, SX, Y, W, H, 16.f * U, 0.35f + 0.65f * Slide * (0.8f + 0.2f * FMath::Sin(TimeNow * 4.f)));
		Round(FLinearColor(0.f, 0.f, 0.f, 0.4f), SX + 6.f * U, Y + 9.f * U, W, H, 16.f * U);
		Round(FMath::Lerp(SQUI::Soul, MenuWhite, 0.18f * Slide), SX, Y, W, H, 16.f * U);
		Round(FLinearColor(1.f, 1.f, 1.f, 0.14f), SX + 6.f * U, Y + 5.f * U, W - 12.f * U, H * 0.36f, 12.f * U);
		ShineSweep(SX, Y, W, H, 3.4f, 5.f * U);

		const float D = H * 0.58f;
		const float CCX = SX + W - 26.f * U - D * 0.5f, CCY = Y + H * 0.5f;
		Round(SQUI::WithAlpha(SQUI::Midnight, 0.9f), CCX - D * 0.5f, CCY - D * 0.5f, D, D, D * 0.5f);
		Round(SQUI::Card, CCX - D * 0.5f + 4.f * U, CCY - D * 0.5f + 4.f * U, D - 8.f * U, D - 8.f * U, D * 0.5f - 4.f * U);
		for (int32 q = 0; q < 4; ++q)
		{
			const float TickDeg = q * 90.f;
			TiltTile(SQUI::CardInk, CCX - 1.5f * U, CCY - D * 0.5f + 6.f * U, 3.f * U, 6.f * U, TickDeg, 0.5f, (D * 0.5f - 6.f * U) / (6.f * U));
		}
		TiltTile(SQUI::Midnight, CCX - 2.f * U, CCY - D * 0.24f, 4.f * U, D * 0.24f, ClockSpin / 12.f, 0.5f, 1.f);
		TiltTile(SQUI::Midnight, CCX - 1.5f * U, CCY - D * 0.36f, 3.f * U, D * 0.36f, ClockSpin, 0.5f, 1.f);
		Round(SQUI::Stamp, CCX - 3.f * U, CCY - 3.f * U, 6.f * U, 6.f * U, 3.f * U);

		// Fit the label between the left edge and the time clock (shrink if the font runs wide)
		const float LabelRoom = (CCX - D * 0.5f - 18.f * U) - (SX + 28.f * U);
		float LabelPx = 46.f;
		while (LabelPx > 24.f && TxtWidth(Label, HeadFont(), LabelPx) > LabelRoom) LabelPx -= 2.f;
		Txt(Label, HeadFont(), LabelPx, SX + 28.f * U, Y + H * 0.42f - 0.62f * LabelPx * U, SQUI::Midnight);
		float SubPx = 16.f;
		while (SubPx > 11.f && TxtWidth(Sub, LabelFont(true), SubPx) > LabelRoom) SubPx -= 1.f;
		Txt(Sub, LabelFont(true), SubPx, SX + 30.f * U, Y + H - 30.f * U, SQUI::WithAlpha(SQUI::Midnight, 0.72f));
	}
	else
	{
		GlowAround(SQUI::Soul, SX, Y, W, H, 12.f * U, 0.4f * Slide);
		Round(FLinearColor(0.f, 0.f, 0.f, 0.3f), SX + 4.f * U, Y + 6.f * U, W, H, 12.f * U);
		Round(SQUI::WithAlpha(FMath::Lerp(SQUI::Panel, SQUI::PanelHover, Slide), 0.97f), SX, Y, W, H, 12.f * U);
		const float BarH = (H - 24.f * U) * (0.35f + 0.65f * Slide);
		Round(FMath::Lerp(SQUI::Line, SQUI::Soul, Slide), SX + 10.f * U, Y + (H - BarH) * 0.5f, 4.f * U, BarH, 2.f * U);
		Txt(Label, HeadFont(), 28.f, SX + 28.f * U, Y + H * 0.5f - 0.62f * 28.f * U, FMath::Lerp(SQUI::BoneWhite, MenuWhite, Slide));
		float RightX = SX + W - 22.f * U;
		if (Slide > 0.02f)
		{
			DrawAlpha = Appear * Slide;
			Txt(TEXT(">"), HeadFont(), 26.f, RightX + 8.f * U * (1.f - Slide), Y + H * 0.5f - 0.62f * 26.f * U, SQUI::Soul, 2);
			DrawAlpha = Appear;
			RightX -= 28.f * U;
		}
		if (!Sub.IsEmpty())
		{
			Txt(Sub, LabelFont(true), 17.f, RightX, Y + H * 0.5f - 0.62f * 17.f * U, SubColour, 2);
		}
	}
	DrawAlpha = 1.f;
	const bool bHit = (bOver && bClick) || (bFocus && bConfirm);
	if (bHit)
	{
		bClick = false;
		bConfirm = false;
		if (!bPrimary)
		{
			PlayUI(TEXT("Soul"), 0.3f, 0.8f);
		}
	}
	return bHit;
}

bool ASQMenuHUD::DrawLinkButton(int32 Index, const FString& Label, float X, float Y, const FLinearColor& Colour, float Appear)
{
	const float U = UnitPx;
	if (Appear <= 0.01f)
	{
		return false;
	}
	DrawAlpha = Appear;
	const float LW = TxtWidth(Label, HeadFont(), 22.f), LH = 36.f * U;
	const bool bOver = MouseIn(X - 8.f * U, Y, LW + 16.f * U, LH);
	if (bOver && bMouseMoved && MainFocus != Index)
	{
		MainFocus = Index;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	const bool bFocus = MainFocus == Index;
	float& Slide = MainSlide[FMath::Clamp(Index, 0, 9)];
	Slide = FMath::FInterpTo(Slide, bFocus ? 1.f : 0.f, DeltaT, 14.f);
	Txt(Label, HeadFont(), 22.f, X, Y + 4.f * U, FMath::Lerp(Colour, SQUI::BoneWhite, Slide));
	if (Slide > 0.02f)
	{
		Round(SQUI::Soul, X, Y + LH - 2.f * U, LW * Slide, 3.f * U, 1.5f * U);
	}
	DrawAlpha = 1.f;
	const bool bHit = (bOver && bClick) || (bFocus && bConfirm);
	if (bHit)
	{
		bClick = false;
		bConfirm = false;
	}
	return bHit;
}

void ASQMenuHUD::DrawMainPage()
{
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	const bool bFirst = PageChangeTime < MenuOpenedAt;
	const float T = (TimeNow - (bFirst ? MenuOpenedAt : PageChangeTime)) * (bFirst ? 1.f : 2.f);
	const float Lead = bFirst ? 0.6f : 0.1f;
	auto ButtonIn = [&](int32 Slot) { return MenuAppear(T, Lead + 0.065f * Slot, 0.4f); };

	DrawTitleBlock(X0, 40.f * U);

	// Up / Down picks a button (HOW TO PLAY, CREDITS and QUIT share the last row: Left / Right between them)
	// 0 START, 1 JOIN, 2 MISSIONS, 3 REAPERS, 4 UPGRADES, 5 QUESTS, 6 SETTINGS, then HOW TO PLAY (7), CREDITS (8) and QUIT (9) on one row
	constexpr int32 NumMain = 10;
	constexpr int32 FirstLink = 7;
	if (bNavDown)
	{
		MainFocus = MainFocus >= FirstLink ? 0 : MainFocus + 1;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (bNavUp)
	{
		MainFocus = MainFocus <= 0 ? FirstLink : (MainFocus >= FirstLink ? FirstLink - 1 : MainFocus - 1);
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (MainFocus >= FirstLink && (bNavRight || bNavLeft))
	{
		MainFocus = bNavRight ? (MainFocus == NumMain - 1 ? FirstLink : MainFocus + 1) : (MainFocus == FirstLink ? NumMain - 1 : MainFocus - 1);
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}

	const USQData* D = USQData::Get(this);
	const FName Mine = PreferredReaper();
	const FSQReaperDef* MyDef = D ? D->FindReaper(Mine) : nullptr;
	const USQSaveGame* MainSave = GetSave();

	// the mission START REAPING hosts
	FString MissionSub = TEXT("SOLO  \u00B7  STRAIGHT INTO THE RUN");
	int32 UpgradesBuyable = 0;
	{
		bool bPendingHard = false;
		const FName Pending = USQMissions::GetPendingMission(this, bPendingHard);
		TArray<FSQMissionView> Missions;
		USQMissions::GetMissionList(D, MainSave, Missions);
		for (const FSQMissionView& Each : Missions)
		{
			if (Each.Id == Pending)
			{
				MissionSub = Each.Name.ToString().ToUpper() + (bPendingHard ? FString(TEXT("  \u00B7  HARD")) : FString()) + TEXT("  \u00B7  SOLO");
			}
		}
		TArray<FSQMetaUpgradeView> Upgrades;
		USQMissions::GetMetaUpgradeList(D, MainSave, Upgrades);
		for (const FSQMetaUpgradeView& Each : Upgrades)
		{
			UpgradesBuyable += (Each.bCanAfford && Each.NextCost >= 0) ? 1 : 0;
		}
	}

	const float BW = 430.f * U, BH = 58.f * U, PH = 108.f * U, Gap = 10.f * U;
	float Y = 290.f * U;
	if (DrawMainButton(0, TEXT("START REAPING"), MissionSub, SQUI::Midnight, X0, Y, BW, PH, true, ButtonIn(0)))
	{
		StartTravel(false);
	}
	Y += PH + Gap;
	if (DrawMainButton(1, TEXT("PLAY CO-OP"), TEXT("HOST OR JOIN"), SQUI::Muted, X0, Y, BW, BH, false, ButtonIn(1)))
	{
		OpenPage(ESQMenuPage::Join);
	}
	Y += BH + Gap;
	{
		const int32 Cleared = MainSave ? MainSave->MissionsCleared.Num() : 0;
		if (DrawMainButton(2, TEXT("MISSIONS"), FString::Printf(TEXT("%d / %d CLEARED"), Cleared, USQMissions::Missions().Num()), SQUI::Soul, X0, Y, BW, BH, false, ButtonIn(2)))
		{
			OpenPage(ESQMenuPage::Missions);
		}
	}
	Y += BH + Gap;
	if (DrawMainButton(3, TEXT("REAPERS"), MyDef ? MyDef->Name.ToUpper() : FString(), MyDef ? MyDef->Color : SQUI::Muted, X0, Y, BW, BH, false, ButtonIn(3)))
	{
		OpenPage(ESQMenuPage::Reapers);
	}
	Y += BH + Gap;
	if (DrawMainButton(4, TEXT("UPGRADES"), UpgradesBuyable > 0 ? FString::Printf(TEXT("%d AFFORDABLE"), UpgradesBuyable)
		: FString::Printf(TEXT("%d GOLD"), MainSave ? MainSave->OvertimePay : 0), SQUI::Amber, X0, Y, BW, BH, false, ButtonIn(4)))
	{
		OpenPage(ESQMenuPage::Upgrades);
	}
	Y += BH + Gap;
	{
		const int32 QuestsDone = MainSave ? MainSave->QuestsDone.Num() : 0;
		const int32 QuestsNew = MainSave ? MainSave->QuestsNew.Num() : 0;
		const FString UnlockSub = QuestsNew > 0 ? FString::Printf(TEXT("%d NEW!"), QuestsNew) : FString::Printf(TEXT("%d / %d"), QuestsDone, SQQuests::Quests().Num());
		if (DrawMainButton(5, TEXT("UNLOCKS"), UnlockSub, QuestsNew > 0 ? SQUI::Gold : SQUI::Amber, X0, Y, BW, BH, false, ButtonIn(5)))
		{
			OpenPage(ESQMenuPage::Quests);
		}
	}
	Y += BH + Gap;
	if (DrawMainButton(6, TEXT("SETTINGS"), FString(), SQUI::Muted, X0, Y, BW, BH, false, ButtonIn(6)))
	{
		OpenPage(ESQMenuPage::Settings);
	}
	Y += BH + 20.f * U;
	float LinkX = X0 + 8.f * U;
	if (DrawLinkButton(7, TEXT("HOW TO PLAY"), LinkX, Y, SQUI::Soul, ButtonIn(7)))
	{
		bHowToOpen = true;
		HowToOpenedAt = TimeNow;
		HowToFocus = 0;
		PlayUI(TEXT("Soul"), 0.3f, 1.f);
	}
	LinkX += TxtWidth(TEXT("HOW TO PLAY"), HeadFont(), 22.f) + 44.f * U;
	if (DrawLinkButton(8, TEXT("CREDITS"), LinkX, Y, SQUI::Muted, ButtonIn(8)))
	{
		OpenPage(ESQMenuPage::Credits);
	}
	LinkX += TxtWidth(TEXT("CREDITS"), HeadFont(), 22.f) + 44.f * U;
	if (DrawLinkButton(9, TEXT("QUIT"), LinkX, Y, SQUI::Danger, ButtonIn(9)))
	{
		bQuitAsk = true;
		QuitAskTime = TimeNow;
		QuitFocus = 0;
		PlayUI(TEXT("Soul"), 0.3f, 0.7f);
	}
	Y += 36.f * U + 28.f * U;

	// a note on the button you're on
	{
		static const TCHAR* Notes[NumMain] = {
			TEXT("Play the selected mission solo, right now. Clear the objectives, then beat the boss."),
			TEXT("Host a lobby for up to 4 reapers, or join a friend's run with their IP address."),
			TEXT("Pick a map. Beat one to open the next, and its Hard mode."),
			TEXT("Pick your reaper and see their kit. Hire new reapers with banked gold."),
			TEXT("Death's Office: spend banked gold on permanent upgrades for every run."),
			TEXT("Unlocks: every goal, what it gives you (gold, reapers, items) and how close you are."),
			TEXT("Volume, mouse sensitivity, display mode and tutorial tips."),
			TEXT("How to play, in 3 short cards."),
			TEXT("Who made this."),
			TEXT("Leave the graveyard and close the game.") };
		if (MainFocus != NoteFocus)
		{
			NoteFocus = MainFocus;
			NoteFocusTime = TimeNow;
		}
		if (NoteFocus >= 0 && NoteFocus < NumMain)
		{
			DrawAlpha = MenuAppear(TimeNow - NoteFocusTime, 0.05f, 0.25f) * ButtonIn(10);
			const float NoteH = 66.f * U;
			Round(SQUI::WithAlpha(SQUI::Panel, 0.85f), X0, Y, BW, NoteH, 12.f * U);
			Round(SQUI::Soul, X0 + 10.f * U, Y + 14.f * U, 4.f * U, NoteH - 28.f * U, 2.f * U);
			TxtWrap(Notes[NoteFocus], LabelFont(), 16.f, X0 + 26.f * U, Y + 11.f * U, BW - 42.f * U, SQUI::Muted, 22.f * U, 2);
			DrawAlpha = 1.f;
		}
	}

	// your reaper's ID card, top right (skipped on very narrow screens)
	const float CardW = FMath::Min(440.f * U, CW - X0 * 2.f - BW - 120.f * U);
	if (CardW >= 300.f * U && DrawReaperCard(CW - X0 - CardW, 48.f * U, CardW, MenuAppear(T, Lead + 0.15f, 0.5f)))
	{
		OpenPage(ESQMenuPage::Reapers);
	}
}

bool ASQMenuHUD::DrawReaperCard(float X, float Y, float W, float Appear)
{
	const float U = UnitPx;
	if (Appear <= 0.01f)
	{
		return false;
	}
	const USQData* D = USQData::Get(this);
	const USQSaveGame* Save = GetSave();
	const FSQReaperDef* Def = D ? D->FindReaper(PreferredReaper()) : nullptr;
	const FLinearColor Colour = Def ? Def->Color : SQUI::BoneWhite;
	const float CardH = 560.f * U;
	const float AX = X + 60.f * U * (1.f - Appear);
	bCardHover = MouseIn(AX, Y - 8.f * U, W, CardH + 8.f * U);
	CardLift = FMath::FInterpTo(CardLift, bCardHover ? 1.f : 0.f, DeltaT, 12.f);
	const float CY = Y - 8.f * U * CardLift;
	DrawAlpha = Appear;

	GlowAround(SQUI::Soul, AX, CY, W, CardH, 22.f * U, 0.35f * CardLift);
	Round(FLinearColor(0.f, 0.f, 0.f, 0.35f + 0.15f * CardLift), AX + 8.f * U, CY + (12.f + 8.f * CardLift) * U, W, CardH, 22.f * U);
	Round(FMath::Lerp(SQUI::Line, SQUI::Soul, CardLift * 0.6f), AX - 2.f * U, CY - 2.f * U, W + 4.f * U, CardH + 4.f * U, 24.f * U);
	Round(SQUI::Panel, AX, CY, W, CardH, 22.f * U);
	Round(SQUI::Midnight, AX + W * 0.5f - 34.f * U, CY + 14.f * U, 68.f * U, 12.f * U, 6.f * U); // lanyard slot
	Round(Colour, AX + 12.f * U, CY + 44.f * U, 6.f * U, CardH - 100.f * U, 3.f * U);           // the reaper's colour stripe

	const float Pad = 34.f * U;
	const float TX = AX + Pad, TW = W - Pad - 26.f * U;
	float TY = CY + 40.f * U;
	Txt(TEXT("TONIGHT'S REAPER"), LabelFont(true), 14.f, TX, TY, SQUI::Muted);
	TY += 22.f * U;
	const FString Name = Def ? Def->Name.ToUpper() : FString(TEXT("GRIM"));
	Txt(Name, HeadFont(), FitPx(Name, HeadFont(), 40.f, TW), TX, TY, SQUI::BoneWhite, 0, true);
	TY += 54.f * U;
	if (Def)
	{
		TxtWrap(Def->PassiveText, LabelFont(), 16.f, TX, TY, TW, SQUI::Muted, 21.f * U, 2);
	}
	TY += 52.f * U;
	Rect(SQUI::Line, TX, TY, TW, 2.f * U);
	TY += 18.f * U;

	// banked gold (meta currency)
	Txt(TEXT("BANKED GOLD"), LabelFont(true), 14.f, TX, TY, SQUI::Amber);
	TY += 22.f * U;
	const float Coin = 44.f * U;
	Round(SQUI::Amber, TX, TY + 10.f * U, Coin, Coin, Coin * 0.5f);
	Round(SQUI::Hex(0xC9, 0x84, 0x2B), TX + 6.f * U, TY + 16.f * U, Coin - 12.f * U, Coin - 12.f * U, Coin * 0.5f - 6.f * U);
	Round(SQUI::Amber, TX + 10.f * U, TY + 20.f * U, Coin - 20.f * U, Coin - 20.f * U, Coin * 0.5f - 10.f * U);
	Txt(FText::AsNumber(Save ? Save->OvertimePay : 0).ToString(), HeadFont(), 58.f, TX + Coin + 14.f * U, TY - 6.f * U, SQUI::Amber, 0, true);
	TY += 74.f * U;
	Rect(SQUI::Line, TX, TY, TW, 2.f * U);
	TY += 18.f * U;

	// runs, wins, endless, contracts
	const float ColW = TW * 0.5f;
	auto Stat = [&](const FString& Key, const FString& Value, float StatX, float StatY, const FLinearColor& ValueColour)
	{
		Txt(Key, LabelFont(true), 13.f, StatX, StatY, SQUI::Muted);
		Txt(Value, HeadFont(), 34.f, StatX, StatY + 18.f * U, ValueColour);
	};
	Stat(TEXT("RUNS PLAYED"), FString::FromInt(Save ? Save->RunsPlayed : 0), TX, TY, SQUI::BoneWhite);
	Stat(TEXT("RUNS WON"), FString::FromInt(Save ? Save->RunsWon : 0), TX + ColW, TY, SQUI::BoneWhite);
	TY += 80.f * U;
	Stat(TEXT("BANKED GOLD"), FString::FromInt(Save ? Save->OvertimePay : 0), TX, TY, SQUI::Amber);
	const int32 Have = Save ? Save->MissionsCleared.Num() : 0;
	const int32 Total = USQMissions::Missions().Num();
	Stat(TEXT("MISSIONS CLEARED"), FString::Printf(TEXT("%d / %d"), Have, Total), TX + ColW, TY, SQUI::Soul);
	for (int32 i = 0; i < Total; ++i)
	{
		Round(i < Have ? SQUI::Soul : SQUI::Line, TX + ColW + i * 18.f * U, TY + 64.f * U, 12.f * U, 12.f * U, 6.f * U);
	}

	Txt(TEXT("VIEW REAPERS  >"), HeadFont(), 20.f, AX + W - Pad, CY + CardH - 46.f * U, FMath::Lerp(SQUI::Muted, SQUI::Soul, CardLift), 2);
	DrawAlpha = 1.f;

	if (bCardHover && bClick)
	{
		bClick = false;
		return true;
	}
	return false;
}

// ================================================================ page frame and buttons

bool ASQMenuHUD::DrawPageFrame(const FString& Title, float VeilAlpha)
{
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	if (VeilAlpha > 0.f)
	{
		Rect(SQUI::WithAlpha(SQUI::Midnight, VeilAlpha), 0.f, 0.f, CW, Canvas->ClipY);
	}
	// tally marks creeping along the top: souls counted
	const float StripH = 30.f * U, Seg = 70.f * U;
	Rect(SQUI::WithAlpha(SQUI::Bar, 0.95f), 0.f, 0.f, CW, StripH);
	const float Shift = FMath::Fmod(TimeNow * 24.f * U, Seg);
	for (float GX = -Seg + Shift; GX < CW; GX += Seg)
	{
		for (int32 m = 0; m < 4; ++m)
		{
			Rect(SQUI::Line, GX + m * 8.f * U, 8.f * U, 3.f * U, 14.f * U);
		}
		TiltTile(SQUI::Line, GX - 4.f * U, 13.5f * U, 34.f * U, 3.f * U, -28.f, 0.5f, 0.5f);
	}
	Rect(SQUI::WithAlpha(SQUI::Soul, 0.7f), 0.f, StripH, CW, 2.f * U);

	// back button, breadcrumb and title slide in
	const float A = MenuAppear(TimeNow - PageChangeTime, 0.08f, 0.35f);
	DrawAlpha = A;
	const float OX = -30.f * U * (1.f - A);
	const float BY = 56.f * U, BS = 64.f * U;
	const bool bOver = MouseIn(X0, BY, BS, BS);
	GlowAround(SQUI::Soul, X0 + OX, BY, BS, BS, 14.f * U, bOver ? 0.5f : 0.f);
	Round(bOver ? SQUI::PanelHover : SQUI::Panel, X0 + OX, BY, BS, BS, 14.f * U);
	Txt(TEXT("<"), HeadFont(), 34.f, X0 + OX + BS * 0.5f, BY + BS * 0.5f - 0.62f * 34.f * U, bOver ? SQUI::Soul : SQUI::BoneWhite, 1);
	Txt(TEXT("DEATH'S OFFICE  /"), LabelFont(true), 15.f, X0 + OX + BS + 24.f * U, BY - 4.f * U, SQUI::Muted);
	Txt(Title, HeadFont(), 54.f, X0 + OX + BS + 24.f * U, BY + 12.f * U, SQUI::BoneWhite, 0, true);
	DrawAlpha = 1.f;
	if (bOver && bClick)
	{
		bClick = false;
		return true;
	}
	return false;
}

bool ASQMenuHUD::DrawPageButton(const FString& Label, float X, float Y, float W, float H, int32 Style, bool bEnabled, int32 FocusIndex, int32& FocusVar)
{
	const float U = UnitPx;
	const bool bOver = MouseIn(X, Y, W, H);
	if (bOver && bMouseMoved && FocusIndex >= 0 && FocusVar != FocusIndex)
	{
		FocusVar = FocusIndex;
		PlayUI(TEXT("Soul"), 0.12f, 1.5f);
	}
	const bool bFocused = FocusIndex >= 0 && FocusVar == FocusIndex;
	const bool bLit = bEnabled && (bOver || bFocused);
	const float R = 16.f * U;
	const FLinearColor Fill = Style == 1 ? SQUI::Soul : Style == 2 ? SQUI::Amber : Style == 3 ? SQUI::Danger : SQUI::Panel;
	if (!bEnabled && Style == 1)
	{
		// a primary that's already done (e.g. ON SHIFT TONIGHT): teal outline
		Round(SQUI::WithAlpha(SQUI::Soul, 0.8f), X, Y, W, H, R);
		Round(SQUI::Panel, X + 3.f * U, Y + 3.f * U, W - 6.f * U, H - 6.f * U, R - 3.f * U);
		Txt(Label, HeadFont(), 26.f, X + W * 0.5f, Y + H * 0.5f - 0.62f * 26.f * U, SQUI::Soul, 1);
		return false;
	}
	if (!bEnabled)
	{
		Round(SQUI::WithAlpha(SQUI::Panel, 0.8f), X, Y, W, H, R);
		Txt(Label, HeadFont(), 24.f, X + W * 0.5f, Y + H * 0.5f - 0.62f * 24.f * U, SQUI::Disabled, 1);
		return false;
	}
	GlowAround(Style == 0 ? SQUI::Soul : Fill, X, Y, W, H, R, bLit ? 0.6f : (Style == 1 ? 0.25f : 0.f));
	Round(FLinearColor(0.f, 0.f, 0.f, 0.35f), X + 4.f * U, Y + 6.f * U, W, H, R);
	if (Style == 0)
	{
		Round(bLit ? SQUI::Soul : SQUI::Line, X - 2.f * U, Y - 2.f * U, W + 4.f * U, H + 4.f * U, R + 2.f * U);
		Round(bLit ? SQUI::PanelHover : SQUI::Panel, X, Y, W, H, R);
		Txt(Label, HeadFont(), 24.f, X + W * 0.5f, Y + H * 0.5f - 0.62f * 24.f * U, SQUI::BoneWhite, 1);
	}
	else
	{
		Round(bLit ? FMath::Lerp(Fill, MenuWhite, 0.2f) : Fill, X, Y, W, H, R);
		Round(FLinearColor(1.f, 1.f, 1.f, 0.14f), X + 6.f * U, Y + 4.f * U, W - 12.f * U, H * 0.36f, R - 4.f * U);
		if (Style == 1)
		{
			ShineSweep(X, Y, W, H, 3.4f, 4.f * U);
		}
		Txt(Label, HeadFont(), 28.f, X + W * 0.5f, Y + H * 0.5f - 0.62f * 28.f * U, SQUI::Midnight, 1);
	}
	const bool bHit = (bOver && bClick) || (bFocused && bConfirm);
	if (bHit)
	{
		bClick = false;
		bConfirm = false;
	}
	return bHit;
}

// ================================================================ JOIN A FRIEND

void ASQMenuHUD::DrawJoinPage()
{
	if (DrawPageFrame(TEXT("PLAY CO-OP"), 0.8f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (!bQuitAsk && !bTravelling)
		{
			TypeAddress(PC);
		}
	}
	const float A = MenuAppear(TimeNow - PageChangeTime, 0.1f, 0.4f);
	const float PW = FMath::Min(820.f * U, Canvas->ClipX - 80.f * U), PH = 520.f * U;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = FMath::Max(170.f * U, (Canvas->ClipY - PH) * 0.5f + 10.f * U) + 30.f * U * (1.f - A);
	DrawAlpha = A;
	PanelBox(PX, PY, PW, PH);

	Txt(TEXT("FRIEND'S ADDRESS"), LabelFont(true), 15.f, PX + 40.f * U, PY + 34.f * U, SQUI::Muted);
	const float FX = PX + 40.f * U, FY = PY + 62.f * U, FW = PW - 80.f * U, FH = 84.f * U;
	Round(SQUI::WithAlpha(SQUI::Soul, 0.5f + 0.5f * (PageFocus == 0 ? 1.f : 0.f)), FX - 3.f * U, FY - 3.f * U, FW + 6.f * U, FH + 6.f * U, 17.f * U);
	Round(SQUI::Midnight, FX, FY, FW, FH, 14.f * U);
	const float AddrPx = FitPx(Address.IsEmpty() ? FString(TEXT("127.0.0.1")) : Address, HeadFont(), 44.f, FW - 60.f * U);
	float CaretX = FX + 24.f * U;
	if (Address.IsEmpty())
	{
		Txt(TEXT("127.0.0.1"), HeadFont(), AddrPx, FX + 24.f * U, FY + FH * 0.5f - 0.62f * AddrPx * U, SQUI::Disabled);
	}
	else
	{
		CaretX += Txt(Address, HeadFont(), AddrPx, FX + 24.f * U, FY + FH * 0.5f - 0.62f * AddrPx * U, SQUI::BoneWhite) + 4.f * U;
	}
	if (FMath::Fmod(TimeNow, 1.f) < 0.55f)
	{
		Rect(SQUI::Soul, CaretX, FY + 20.f * U, 4.f * U, FH - 40.f * U);
	}

	float TY = FY + FH + 26.f * U;
	const float LineH = 25.f * U;
	TY += TxtWrap(TEXT("To host: press HOST A LOBBY and tell your friends your IP address. To join: type the host's IP and press JOIN RUN."), LabelFont(), 18.f, PX + 40.f * U, TY,
		PW - 80.f * U, SQUI::BoneWhite, LineH, 3) * LineH + 10.f * U;
	TY += TxtWrap(TEXT("Same Wi-Fi: their local IP (like 192.168.1.20). Over the internet: their public IP, with port 7777 forwarded on their router."),
		LabelFont(), 16.f, PX + 40.f * U, TY, PW - 80.f * U, SQUI::Muted, 22.f * U, 3) * 22.f * U + 10.f * U;
	Txt(TEXT("TYPE WITH THE KEYBOARD  \u00B7  BACKSPACE DELETES  \u00B7  DELETE CLEARS"), LabelFont(true), 12.f, PX + 40.f * U, TY, SQUI::Disabled);

	// HOST A LOBBY (2), LOCAL TEST (1) and JOIN RUN (focus 0, the default); Left / Right between them
	if (bNavRight || bNavDown)
	{
		PageFocus = PageFocus <= 0 ? 2 : PageFocus - 1;
		PlayUI(TEXT("Soul"), 0.12f, 1.5f);
	}
	if (bNavLeft || bNavUp)
	{
		PageFocus = PageFocus >= 2 ? 0 : PageFocus + 1;
		PlayUI(TEXT("Soul"), 0.12f, 1.5f);
	}
	const float ButGap = 14.f * U;
	const float ButH = 72.f * U, ButY = PY + PH - 36.f * U - ButH, ButW = (PW - 80.f * U - 2.f * ButGap) / 3.f;
	if (DrawPageButton(TEXT("HOST A LOBBY"), PX + 40.f * U, ButY, ButW, ButH, 1, true, 2, PageFocus))
	{
		StartTravel(false, true);
	}
	if (DrawPageButton(TEXT("LOCAL TEST"), PX + 40.f * U + ButW + ButGap, ButY, ButW, ButH, 0, true, 1, PageFocus))
	{
		Address = TEXT("127.0.0.1");
		StartTravel(true);
	}
	if (DrawPageButton(TEXT("JOIN RUN"), PX + 40.f * U + 2.f * (ButW + ButGap), ButY, ButW, ButH, 1, true, 0, PageFocus))
	{
		StartTravel(true);
	}
	DrawAlpha = 1.f;
}

// ================================================================ REAPERS

void ASQMenuHUD::DrawRosterCard(const FSQReaperDef& Def, int32 Index, float X, float Y, float W, float H, float Lift, bool bLocked, bool bMine, bool bAfford)
{
	const float U = UnitPx;
	const float Rad = 14.f * U;
	const FLinearColor Fill = FMath::Lerp(SQUI::Panel, SQUI::PanelHover, Lift);
	GlowAround(SQUI::Soul, X, Y, W, H, Rad, 0.45f * Lift);
	Round(FLinearColor(0.f, 0.f, 0.f, 0.35f), X + 4.f * U, Y + (6.f + 5.f * Lift) * U, W, H, Rad);
	Round(bMine ? SQUI::Soul : FMath::Lerp(SQUI::Line, SQUI::Soul, Lift * 0.6f), X - 2.f * U, Y - 2.f * U, W + 4.f * U, H + 4.f * U, Rad + 2.f * U);
	Round(Fill, X, Y, W, H, Rad);

	// roster number in the corner
	Txt(FString::Printf(TEXT("%02d"), Index + 1), LabelFont(true), 11.f, X + 9.f * U, Y + 7.f * U, bMine ? SQUI::Soul : SQUI::Disabled);

	// portrait: the reaper's colour with their initial (a padlock while they're not hired)
	const float Port = FMath::Clamp(H - 79.f * U, 30.f * U, 60.f * U);
	const float PortX = X + (W - Port) * 0.5f, PortY = Y + 10.f * U;
	const FLinearColor PortColour = bLocked ? FMath::Lerp(Def.Color, SQUI::Disabled, 0.65f) : Def.Color;
	Round(PortColour, PortX, PortY, Port, Port, Port * 0.3f);
	Round(FLinearColor(1.f, 1.f, 1.f, 0.16f), PortX + 4.f * U, PortY + 3.f * U, Port - 8.f * U, Port * 0.32f, Port * 0.16f);
	if (bLocked)
	{
		Padlock(PortX + Port * 0.5f, PortY + Port * 0.52f, Port * 0.5f, SQUI::WithAlpha(SQUI::Midnight, 0.8f));
	}
	else
	{
		const float InitialPx = 0.62f * Port / FMath::Max(0.01f, U);
		Txt(Def.Name.Left(1).ToUpper(), HeadFont(), InitialPx, PortX + Port * 0.5f, PortY + Port * 0.5f - 0.62f * InitialPx * U,
			SQUI::WithAlpha(SQUI::Midnight, 0.85f), 1);
	}

	// name
	const float NameY = PortY + Port + 6.f * U;
	const FString NameUpper = Def.Name.ToUpper();
	Txt(NameUpper, HeadFont(), FitPx(NameUpper, HeadFont(), 17.f, W - 14.f * U), X + W * 0.5f, NameY, bLocked ? SQUI::Muted : SQUI::BoneWhite, 1);

	// role tag
	if (!Def.Role.IsNone())
	{
		const FString RoleUpper = Def.Role.ToString().ToUpper();
		const float RolePx = FitPx(RoleUpper, LabelFont(true), 10.f, W - 30.f * U);
		const float RoleW = TxtWidth(RoleUpper, LabelFont(true), RolePx) + 14.f * U;
		const float RoleY = NameY + 22.f * U;
		Round(SQUI::WithAlpha(Def.Color, 0.22f), X + (W - RoleW) * 0.5f, RoleY, RoleW, 16.f * U, 8.f * U);
		Txt(RoleUpper, LabelFont(true), RolePx, X + W * 0.5f, RoleY + 2.f * U, FMath::Lerp(Def.Color, MenuWhite, 0.45f), 1);
	}

	// status along the bottom: price, YOUR REAPER, or hired
	const float StatusY = Y + H - 21.f * U;
	if (bLocked)
	{
		const FString Price = FText::AsNumber(Def.UnlockCost).ToString();
		const FLinearColor PriceColour = bAfford ? SQUI::Amber : SQUI::Disabled;
		const float CoinS = 12.f * U;
		const float GroupW = CoinS + 5.f * U + TxtWidth(Price, LabelFont(true), 12.f);
		const float GroupX = X + (W - GroupW) * 0.5f;
		Round(PriceColour, GroupX, StatusY + 1.5f * U, CoinS, CoinS, CoinS * 0.5f);
		Round(SQUI::WithAlpha(SQUI::Midnight, 0.35f), GroupX + 3.5f * U, StatusY + 5.f * U, 5.f * U, 5.f * U, 2.5f * U);
		Txt(Price, LabelFont(true), 12.f, GroupX + CoinS + 5.f * U, StatusY, PriceColour);
	}
	else if (bMine)
	{
		const FString Yours(TEXT("YOUR REAPER"));
		const float YoursPx = FitPx(Yours, LabelFont(true), 10.f, W - 24.f * U);
		const float PillW = TxtWidth(Yours, LabelFont(true), YoursPx) + 14.f * U;
		Round(SQUI::Soul, X + (W - PillW) * 0.5f, StatusY - 1.f * U, PillW, 17.f * U, 8.5f * U);
		Txt(Yours, LabelFont(true), YoursPx, X + W * 0.5f, StatusY + 1.5f * U, SQUI::Midnight, 1);
	}
	else
	{
		Txt(TEXT("HIRED"), LabelFont(true), 10.f, X + W * 0.5f, StatusY + 1.5f * U, SQUI::Disabled, 1);
	}
}

int32 ASQMenuHUD::DrawRosterPager(int32 CurrentPage, int32 NumPages, float X, float Y, float W)
{
	const float U = UnitPx;
	const float ButS = 38.f * U;
	int32 Clicked = -1;
	auto Arrow = [&](const TCHAR* Glyph, float ArrowX, bool bEnabled, int32 TargetPage)
	{
		const bool bOver = bEnabled && MouseIn(ArrowX, Y, ButS, ButS);
		GlowAround(SQUI::Soul, ArrowX, Y, ButS, ButS, 12.f * U, bOver ? 0.45f : 0.f);
		Round(bOver ? SQUI::Soul : SQUI::Line, ArrowX - 2.f * U, Y - 2.f * U, ButS + 4.f * U, ButS + 4.f * U, 13.f * U);
		Round(bOver ? SQUI::PanelHover : SQUI::Panel, ArrowX, Y, ButS, ButS, 12.f * U);
		Txt(Glyph, HeadFont(), 24.f, ArrowX + ButS * 0.5f, Y + ButS * 0.5f - 0.62f * 24.f * U, bEnabled ? (bOver ? SQUI::Soul : SQUI::BoneWhite) : SQUI::Disabled, 1);
		if (bOver && bClick)
		{
			bClick = false;
			Clicked = TargetPage;
		}
	};
	Arrow(TEXT("<"), X, CurrentPage > 0, CurrentPage - 1);
	Arrow(TEXT(">"), X + W - ButS, CurrentPage + 1 < NumPages, CurrentPage + 1);

	// "PAGE 2 OF 3" with a dot per page (dots are clickable)
	const float MidX = X + W * 0.5f;
	Txt(FString::Printf(TEXT("PAGE %d OF %d"), CurrentPage + 1, NumPages), LabelFont(true), 13.f, MidX, Y + 1.f * U, SQUI::Muted, 1);
	const float DotS = 10.f * U, DotGap = 8.f * U;
	const int32 Dots = FMath::Min(NumPages, 12);
	const float DotsW = Dots * DotS + (Dots - 1) * DotGap;
	for (int32 p = 0; p < Dots; ++p)
	{
		const float DotX = MidX - DotsW * 0.5f + p * (DotS + DotGap);
		const float DotY = Y + 24.f * U;
		const bool bOver = MouseIn(DotX - 3.f * U, DotY - 4.f * U, DotS + 6.f * U, DotS + 8.f * U);
		Round(p == CurrentPage ? SQUI::Soul : (bOver ? SQUI::Muted : SQUI::Line), DotX, DotY, DotS, DotS, DotS * 0.5f);
		if (bOver && bClick)
		{
			bClick = false;
			Clicked = p;
		}
	}
	return Clicked;
}

void ASQMenuHUD::DrawReapersPage()
{
	if (DrawPageFrame(TEXT("REAPERS"), 0.f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	const USQData* D = USQData::Get(this);
	USQSaveGame* Save = GetSave();
	if (!D || D->Reapers.Num() == 0 || !Save)
	{
		Txt(TEXT("No reapers found (Content/Data/Reapers.csv)."), LabelFont(true), 20.f, X0, 200.f * U, SQUI::Danger);
		return;
	}
	const TArray<FSQReaperDef>& Roster = D->Reapers;
	const int32 Count = Roster.Num();
	PageFocus = FMath::Clamp(PageFocus, 0, Count - 1);
	if (ReaperLift.Num() != Count)
	{
		ReaperLift.SetNumZeroed(Count);
	}
	const FName Mine = PreferredReaper();
	const float Elapsed = TimeNow - PageChangeTime;

	// roster grid: 4 compact portrait cards across (same 540px strip as before, the showroom reaper stays in view),
	// as many rows as fit; a roster too big for one screen turns into pages (any number of reapers)
	const float Top = 170.f * U, Bottom = Canvas->ClipY - 52.f * U - 24.f * U;
	constexpr int32 GridCols = 4;
	const float GridW = 540.f * U, Gap = 12.f * U;
	const float CardW = (GridW - Gap * (GridCols - 1)) / GridCols;
	const float GridTop = Top + 36.f * U;
	const float MinCardH = 112.f * U, MaxCardH = 150.f * U, PagerH = 52.f * U;
	const int32 TotalRows = (Count + GridCols - 1) / GridCols;
	float GridH = Bottom - GridTop;
	int32 RowsPerPage = FMath::Max(1, FMath::FloorToInt((GridH + Gap) / (MinCardH + Gap)));
	bReaperPaged = TotalRows > RowsPerPage;
	if (bReaperPaged)
	{
		GridH -= PagerH;
		RowsPerPage = FMath::Max(1, FMath::FloorToInt((GridH + Gap) / (MinCardH + Gap)));
	}
	const int32 RowsShown = FMath::Clamp(TotalRows, 1, RowsPerPage);
	const float CardH = FMath::Min(MaxCardH, (GridH - Gap * (RowsShown - 1)) / RowsShown);
	const int32 PerPage = GridCols * RowsPerPage;
	const int32 NumPages = FMath::Max(1, (Count + PerPage - 1) / PerPage);

	int32 Hired = 0;
	float MaxHP = 1.f, MaxSpeed = 1.f, MaxCarry = 1.f, MaxMagnet = 1.f, MinDash = 100.f;
	for (int32 i = 0; i < Count; ++i)
	{
		Hired += IsReaperLocked(Roster[i].Id) ? 0 : 1;
		MaxHP = FMath::Max(MaxHP, Roster[i].MaxHealth);
		MaxSpeed = FMath::Max(MaxSpeed, Roster[i].MoveSpeed);
		MaxCarry = FMath::Max(MaxCarry, float(Roster[i].CarryCap));
		MaxMagnet = FMath::Max(MaxMagnet, Roster[i].Magnet);
		MinDash = FMath::Min(MinDash, FMath::Max(0.1f, Roster[i].DashCooldown));
	}
	DrawAlpha = MenuAppear(Elapsed, 0.05f, 0.3f);
	Txt(FString::Printf(TEXT("%d OF %d HIRED"), Hired, Count), LabelFont(true), 16.f, X0, Top, SQUI::Muted);
	Txt(FString::Printf(TEXT("%s GOLD"), *FText::AsNumber(Save->OvertimePay).ToString()), LabelFont(true), 16.f, X0 + GridW, Top, SQUI::Amber, 2);
	DrawAlpha = 1.f;

	// arrows / D-pad move round the grid in 2D; past the left / right edge flips the page
	const int32 Before = PageFocus;
	const int32 FocusCol = PageFocus % GridCols;
	if (bNavRight)
	{
		if (FocusCol < GridCols - 1 && PageFocus + 1 < Count)
		{
			++PageFocus;
		}
		else if (bReaperPaged && PageFocus / PerPage + 1 < NumPages)
		{
			PageFocus = FMath::Min(Count - 1, PageFocus + PerPage - FocusCol); // next page, same row, first column
		}
	}
	if (bNavLeft)
	{
		if (FocusCol > 0)
		{
			--PageFocus;
		}
		else if (bReaperPaged && PageFocus / PerPage > 0)
		{
			PageFocus = PageFocus - PerPage + (GridCols - 1); // previous page, same row, last column
		}
	}
	if (bNavDown)
	{
		if (PageFocus + GridCols < Count)
		{
			PageFocus += GridCols;
		}
		else if (PageFocus / GridCols < (Count - 1) / GridCols)
		{
			PageFocus = Count - 1; // the last row is short: land on its last card
		}
	}
	if (bNavUp && PageFocus - GridCols >= 0)
	{
		PageFocus -= GridCols;
	}

	// pages: mouse wheel, LB / RB, Page Up / Page Down, the pager's arrows and dots
	auto GoToPage = [&](int32 NewPage)
	{
		const int32 Wanted = FMath::Clamp(NewPage, 0, NumPages - 1);
		PageFocus = FMath::Clamp(Wanted * PerPage + PageFocus % PerPage, 0, Count - 1);
	};
	if (bReaperPaged)
	{
		APlayerController* MenuPC = GetOwningPlayerController();
		const bool bKeysLive = MenuPC && !bQuitAsk && !bTravelling;
		const bool bPrev = bWheelUp || (bKeysLive && (MenuPC->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder) || MenuPC->WasInputKeyJustPressed(EKeys::PageUp)));
		const bool bNext = bWheelDown || (bKeysLive && (MenuPC->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) || MenuPC->WasInputKeyJustPressed(EKeys::PageDown)));
		if (bNext && !bPrev)
		{
			GoToPage(PageFocus / PerPage + 1);
		}
		else if (bPrev && !bNext)
		{
			GoToPage(PageFocus / PerPage - 1);
		}
		DrawAlpha = MenuAppear(Elapsed, 0.2f, 0.35f);
		const int32 PagerClick = DrawRosterPager(PageFocus / PerPage, NumPages, X0, GridTop + GridH + 14.f * U, GridW);
		DrawAlpha = 1.f;
		if (PagerClick >= 0)
		{
			GoToPage(PagerClick);
		}
	}
	if (PageFocus != Before)
	{
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}

	// the page on screen follows the focused card (slides in from the side it came from)
	const int32 CurPage = PageFocus / PerPage;
	if (ReaperPageShown < 0 || ReaperPageShown >= NumPages)
	{
		ReaperPageShown = CurPage;
	}
	else if (CurPage != ReaperPageShown)
	{
		ReaperPageDir = CurPage > ReaperPageShown ? 1.f : -1.f;
		ReaperPageShown = CurPage;
		ReaperPageTime = TimeNow;
		PlayUI(TEXT("Dash"), 0.16f, 1.3f);
	}
	const float SlideIn = MenuEaseOut((TimeNow - ReaperPageTime) / 0.3f);
	const float SlideX = ReaperPageDir * 46.f * U * (1.f - SlideIn);

	bool bActivate = bConfirm;
	bConfirm = false;
	const int32 First = CurPage * PerPage;
	const int32 Last = FMath::Min(Count, First + PerPage);
	for (int32 i = First; i < Last; ++i)
	{
		const FSQReaperDef& R = Roster[i];
		const int32 Slot = i - First;
		const float CardX = X0 + (Slot % GridCols) * (CardW + Gap) + SlideX;
		const float BaseY = GridTop + (Slot / GridCols) * (CardH + Gap);
		const float In = MenuAppear(Elapsed, 0.1f + 0.022f * Slot, 0.35f);
		if (In <= 0.01f)
		{
			continue;
		}
		// Hover only lifts the card a little; a click (or arrows / Enter) picks which reaper stands in the showroom
		const bool bOver = MouseIn(CardX, BaseY, CardW, CardH);
		if (bOver && bMouseMoved && ReaperHoverCard != i)
		{
			ReaperHoverCard = i;
			if (PageFocus != i) PlayUI(TEXT("Soul"), 0.08f, 1.6f);
		}
		else if (!bOver && ReaperHoverCard == i)
		{
			ReaperHoverCard = INDEX_NONE;
		}
		if (bOver && bClick)
		{
			bClick = false;
			bActivate |= PageFocus == i; // a second click on the picked card = use it
			PageFocus = i;
		}
		float& Lift = ReaperLift[i];
		Lift = FMath::FInterpTo(Lift, PageFocus == i ? 1.f : (ReaperHoverCard == i ? 0.45f : 0.f), DeltaT, 12.f);
		const float CardY = BaseY + 20.f * U * (1.f - In) - 5.f * U * Lift;
		const bool bLocked = IsReaperLocked(R.Id);
		DrawAlpha = In * SlideIn;
		DrawRosterCard(R, i, CardX, CardY, CardW, CardH, Lift, bLocked, R.Id == Mine, Save->OvertimePay >= R.UnlockCost);
		DrawAlpha = 1.f;
	}

	// the picked reaper stands in the showroom
	const FSQReaperDef& Sel = Roster[PageFocus];
	if (Showroom.IsValid())
	{
		Showroom->ShowReaper(Sel.Id);
	}

	// personnel file, right
	const float GridRight = X0 + GridW;
	const float PW = FMath::Min(440.f * U, CW - X0 - GridRight - 60.f * U);
	if (PW < 280.f * U)
	{
		// too narrow for the file: Enter / a second click still hires / picks
		if (bActivate && !IsReaperLocked(Sel.Id) && Sel.Id != Mine)
		{
			Save->PreferredReaper = Sel.Id;
			StoreSave();
			ShowToast(FString::Printf(TEXT("%s rises tonight."), *Sel.Name), SQUI::Soul);
		}
		return;
	}
	const float PanelIn = MenuAppear(Elapsed, 0.15f, 0.4f);
	const float PX = CW - X0 - PW + 40.f * U * (1.f - PanelIn), PY = Top, PH = Bottom - Top;
	const bool bSelLocked = IsReaperLocked(Sel.Id);
	const bool bSelMine = Sel.Id == Mine;
	DrawAlpha = PanelIn;
	PanelBox(PX, PY, PW, PH);
	Round(Sel.Color, PX, PY, PW, 36.f * U, 24.f * U);
	Rect(SQUI::WithAlpha(SQUI::Panel, 0.97f), PX, PY + 14.f * U, PW, 22.f * U);
	const float IX = PX + 34.f * U, IW = PW - 68.f * U;
	const float ButH = 72.f * U, ButY = PY + PH - 30.f * U - ButH;
	float TY = PY + 40.f * U;
	Txt(FString::Printf(TEXT("PERSONNEL FILE  \u00B7  NO. %02d OF %02d"), PageFocus + 1, Count), LabelFont(true), 14.f, IX, TY, SQUI::Muted);
	TY += 22.f * U;
	const FString SelName = Sel.Name.ToUpper();
	Txt(SelName, HeadFont(), FitPx(SelName, HeadFont(), 42.f, IW), IX, TY, SQUI::BoneWhite, 0, true);
	TY += 54.f * U;
	if (!Sel.Role.IsNone())
	{
		const FString SelRole = Sel.Role.ToString().ToUpper();
		const float SelRolePx = FitPx(SelRole, LabelFont(true), 13.f, IW - 24.f * U);
		const float SelRoleW = TxtWidth(SelRole, LabelFont(true), SelRolePx) + 22.f * U;
		Round(SQUI::WithAlpha(Sel.Color, 0.24f), IX, TY, SelRoleW, 24.f * U, 12.f * U);
		Txt(SelRole, LabelFont(true), SelRolePx, IX + 11.f * U, TY + 4.f * U, FMath::Lerp(Sel.Color, MenuWhite, 0.45f));
		TY += 34.f * U;
	}
	TY += TxtWrap(Sel.PassiveText, LabelFont(), 19.f, IX, TY, IW, SQUI::BoneWhite, 25.f * U, 3) * 25.f * U + 12.f * U;
	if (bSelLocked)
	{
		// how to get them: the CSV's unlock hint, or the price
		const FString HireNote = Sel.UnlockHint.IsEmpty() ? FString::Printf(TEXT("Hire for %s banked gold."), *FText::AsNumber(Sel.UnlockCost).ToString())
			: Sel.UnlockHint;
		Padlock(IX + 8.f * U, TY + 10.f * U, 16.f * U, SQUI::Amber);
		TY += TxtWrap(HireNote, LabelFont(true), 15.f, IX + 24.f * U, TY, IW - 24.f * U, SQUI::Amber, 20.f * U, 2) * 20.f * U + 12.f * U;
	}
	else
	{
		TY += 6.f * U;
	}

	const float BarGap = 38.f * U;
	StatBar(TEXT("HEALTH"), FString::FromInt(FMath::RoundToInt(Sel.MaxHealth)), Sel.MaxHealth / MaxHP, IX, TY, IW, SQUI::Danger);
	TY += BarGap;
	StatBar(TEXT("SPEED"), FString::FromInt(FMath::RoundToInt(Sel.MoveSpeed)), Sel.MoveSpeed / MaxSpeed, IX, TY, IW, SQUI::Soul);
	TY += BarGap;
	StatBar(TEXT("SOUL CARRY"), FString::FromInt(Sel.CarryCap), Sel.CarryCap / MaxCarry, IX, TY, IW, SQUI::Amber);
	TY += BarGap;
	StatBar(TEXT("DASH"), FString::Printf(TEXT("%.1fs"), Sel.DashCooldown), MinDash / FMath::Max(0.1f, Sel.DashCooldown), IX, TY, IW, SQUI::BoneWhite);
	TY += BarGap;
	StatBar(TEXT("SOUL MAGNET"), FString::FromInt(FMath::RoundToInt(Sel.Magnet)), Sel.Magnet / MaxMagnet, IX, TY, IW, SQUI::Muted);
	TY += BarGap + 8.f * U;

	const float ButTop = ButY - 30.f * U;
	if (const FSQUpgradeDef* Weapon = D->FindUpgrade(Sel.StartWeapon))
	{
		if (TY + 70.f * U <= ButTop)
		{
			Rect(SQUI::Line, IX, TY, IW, 2.f * U);
			TY += 14.f * U;
			Txt(TEXT("STARTING WEAPON"), LabelFont(true), 13.f, IX, TY, SQUI::Muted);
			TY += 20.f * U;
			Txt(Weapon->Name.ToUpper(), HeadFont(), FitPx(Weapon->Name.ToUpper(), HeadFont(), 26.f, IW), IX, TY, SQUI::Soul);
			TY += 36.f * U;
			const int32 DescLines = FMath::Clamp(FMath::FloorToInt((ButTop - TY) / (20.f * U)), 0, 3);
			if (DescLines > 0)
			{
				TxtWrap(Weapon->Description, LabelFont(), 15.f, IX, TY, IW, SQUI::Muted, 20.f * U, DescLines);
			}
		}
	}

	// the action button
	const bool bAfford = Save->OvertimePay >= Sel.UnlockCost;
	int32 NoFocus = -1;
	bool bPressed = false;
	if (bSelMine && !bSelLocked)
	{
		DrawPageButton(TEXT("YOUR REAPER"), IX, ButY, IW, ButH, 1, false, -1, NoFocus);
	}
	else if (!bSelLocked)
	{
		bPressed = DrawPageButton(TEXT("SET AS MY REAPER"), IX, ButY, IW, ButH, 1, true, -1, NoFocus);
	}
	else if (bAfford)
	{
		bPressed = DrawPageButton(FString::Printf(TEXT("HIRE FOR %d GOLD"), Sel.UnlockCost), IX, ButY, IW, ButH, 2, true, -1, NoFocus);
	}
	else
	{
		DrawPageButton(FString::Printf(TEXT("NEED %d GOLD"), Sel.UnlockCost), IX, ButY, IW, ButH, 0, false, -1, NoFocus);
		Txt(FString::Printf(TEXT("You have %d. Finish missions to bank gold."), Save->OvertimePay), LabelFont(), 14.f, IX + IW * 0.5f, ButY - 26.f * U,
			SQUI::Muted, 1);
	}
	DrawAlpha = 1.f;

	if (!(bPressed || bActivate))
	{
		return;
	}
	// hire / pick / cheer
	if (bSelLocked && bAfford)
	{
		Save->OvertimePay -= Sel.UnlockCost;
		Save->UnlockedReapers.AddUnique(Sel.Id);
		Save->PreferredReaper = Sel.Id;
		StoreSave();
		PlayUI(TEXT("Bank"), 0.5f);
		ShowToast(FString::Printf(TEXT("Hired %s! They rise tonight."), *Sel.Name), SQUI::Amber);
		if (Showroom.IsValid())
		{
			Showroom->Celebrate();
		}
	}
	else if (bSelLocked)
	{
		PlayUI(TEXT("Hit"), 0.3f, 1.2f);
		ShowToast(FString::Printf(TEXT("%s needs %d banked gold. You have %d."), *Sel.Name, Sel.UnlockCost, Save->OvertimePay), SQUI::Danger);
	}
	else
	{
		if (!bSelMine)
		{
			Save->PreferredReaper = Sel.Id;
			StoreSave();
			ShowToast(FString::Printf(TEXT("%s rises tonight."), *Sel.Name), SQUI::Soul);
		}
		PlayUI(TEXT("LevelUp"), 0.35f);
		if (Showroom.IsValid())
		{
			Showroom->Celebrate();
		}
	}
}

// ================================================================ CONTRACTS

void ASQMenuHUD::DrawContractsPage()
{
	if (DrawPageFrame(TEXT("CONTRACTS"), 0.82f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	const USQData* D = USQData::Get(this);
	const USQSaveGame* Save = GetSave();
	if (!D || D->Biomes.Num() == 0 || !Save)
	{
		Txt(TEXT("No biomes found (Content/Data/Biomes.csv)."), LabelFont(true), 20.f, X0, 200.f * U, SQUI::Danger);
		return;
	}
	const int32 Count = FMath::Min(D->Biomes.Num(), 8);
	const float Elapsed = TimeNow - PageChangeTime;
	const float Top = 170.f * U, Bottom = Canvas->ClipY - 52.f * U - 24.f * U;
	const float RW = FMath::Min(CW - 2.f * X0, 1500.f * U);
	const float RX = (CW - RW) * 0.5f;

	int32 Have = 0, Total = 0;
	ContractCount(Have, Total);
	DrawAlpha = MenuAppear(Elapsed, 0.05f, 0.3f);
	const float HeadW = Txt(FString::Printf(TEXT("CONTRACTS COLLECTED  %d / %d"), Have, Total), HeadFont(), 26.f, RX, Top, SQUI::BoneWhite);
	for (int32 i = 0; i < Total; ++i)
	{
		Round(i < Have ? SQUI::Soul : SQUI::Line, RX + HeadW + 20.f * U + i * 22.f * U, Top + 10.f * U, 14.f * U, 14.f * U, 7.f * U);
	}
	Txt(TEXT("The host picks tonight's biome in the lobby (press B)."), LabelFont(), 16.f, RX + RW, Top + 8.f * U, SQUI::Muted, 2);
	DrawAlpha = 1.f;

	if (bNavDown)
	{
		PageFocus = (PageFocus + 1) % Count;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (bNavUp)
	{
		PageFocus = (PageFocus + Count - 1) % Count;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}

	const float AreaTop = Top + 52.f * U, Gap = 12.f * U;
	const float RowH = FMath::Min(132.f * U, (Bottom - AreaTop - Gap * (Count - 1)) / Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FSQBiomeDef& B = D->Biomes[i];
		const float RowY = AreaTop + i * (RowH + Gap);
		const float In = MenuAppear(Elapsed, 0.1f + 0.06f * i, 0.35f);
		if (In <= 0.01f)
		{
			continue;
		}
		const bool bOver = MouseIn(RX, RowY, RW, RowH);
		if (bOver && bMouseMoved && PageFocus != i)
		{
			PageFocus = i;
			PlayUI(TEXT("Soul"), 0.12f, 1.5f);
		}
		float& Lift = ContractLift[i];
		Lift = FMath::FInterpTo(Lift, PageFocus == i ? 1.f : 0.f, DeltaT, 12.f);
		const float LX = RX - 50.f * U * (1.f - In) + 8.f * U * Lift;
		const bool bCollected = Save->HasContract(B.Id);
		const bool bOpen = IsBiomeOpen(B.Id);
		DrawAlpha = In;

		GlowAround(bCollected ? SQUI::Soul : bOpen ? SQUI::Amber : SQUI::Line, LX, RowY, RW, RowH, 16.f * U, 0.35f * Lift);
		Round(FLinearColor(0.f, 0.f, 0.f, 0.35f), LX + 5.f * U, RowY + 7.f * U, RW, RowH, 16.f * U);
		Round(bCollected ? SQUI::WithAlpha(SQUI::Soul, 0.7f) : SQUI::Line, LX - 2.f * U, RowY - 2.f * U, RW + 4.f * U, RowH + 4.f * U, 18.f * U);
		Round(FMath::Lerp(SQUI::Panel, SQUI::PanelHover, Lift), LX, RowY, RW, RowH, 16.f * U);

		// a little diorama of the biome: fog sky fading to ground, a moon in the prop colour, three graves
		const float SwH = RowH - 16.f * U, SwW = SwH * 1.3f;
		const float SwX = LX + 8.f * U, SwY = RowY + 8.f * U;
		constexpr int32 Bands = 8;
		for (int32 k = 0; k < Bands; ++k)
		{
			const float BandT = float(k) / (Bands - 1);
			Rect(FMath::Lerp(B.FogColor, B.GroundColor, BandT * BandT), SwX, SwY + SwH * k / Bands, SwW, SwH / Bands + 1.f);
		}
		Round(SQUI::WithAlpha(B.PropColor, 0.9f), SwX + SwW * 0.68f, SwY + SwH * 0.14f, SwH * 0.2f, SwH * 0.2f, SwH * 0.1f);
		for (int32 g = 0; g < 3; ++g)
		{
			const float GH = SwH * (0.2f + 0.06f * g);
			Round(SQUI::WithAlpha(SQUI::Midnight, 0.85f), SwX + SwW * (0.14f + 0.27f * g), SwY + SwH - GH, SwW * 0.13f, GH + 4.f * U, SwW * 0.06f);
		}
		if (!bOpen)
		{
			Rect(SQUI::WithAlpha(SQUI::Midnight, 0.55f), SwX, SwY, SwW, SwH);
		}

		// text
		const float StatusW = 250.f * U;
		const float TX = SwX + SwW + 24.f * U;
		const float TW = LX + RW - StatusW - 30.f * U - TX;
		const FSQBossDef* Boss = D->FindBoss(B.BossId);
		const FString Caption = (B.bRequiresAllContracts ? FString(TEXT("FINAL CONTRACT")) : FString::Printf(TEXT("CONTRACT %d"), i + 1))
			+ (Boss ? FString::Printf(TEXT("  \u00B7  BOSS: %s"), *Boss->Name.ToUpper()) : FString());
		Txt(Caption, LabelFont(true), 13.f, TX, RowY + 12.f * U, SQUI::Muted);
		const FString Name = B.Name.ToUpper();
		Txt(Name, HeadFont(), FitPx(Name, HeadFont(), 30.f, TW), TX, RowY + 30.f * U, bOpen ? SQUI::BoneWhite : SQUI::Muted);
		TxtWrap(B.Description, LabelFont(), 16.f, TX, RowY + 70.f * U, TW, SQUI::Muted, 20.f * U, 1);
		if (!B.GimmickText.IsEmpty() && RowH > 108.f * U)
		{
			TxtWrap(B.GimmickText, LabelFont(true), 14.f, TX, RowY + RowH - 30.f * U, TW, bOpen ? SQUI::Soul : SQUI::Disabled, 18.f * U, 1);
		}

		// status stamp, right
		const float SX = LX + RW - StatusW - 18.f * U, SCY = RowY + RowH * 0.5f;
		const FLinearColor StampCol = bCollected ? SQUI::Soul : bOpen ? SQUI::Amber : SQUI::Disabled;
		const FString StampText = bCollected ? FString(TEXT("COLLECTED")) : bOpen ? FString(TEXT("OPEN FOR WORK")) : FString(TEXT("LOCKED"));
		const float BoxH = 46.f * U, Thick = 3.f * U, BoxY = SCY - BoxH * 0.5f - 12.f * U;
		Rect(StampCol, SX, BoxY, StatusW, Thick);
		Rect(StampCol, SX, BoxY + BoxH - Thick, StatusW, Thick);
		Rect(StampCol, SX, BoxY, Thick, BoxH);
		Rect(StampCol, SX + StatusW - Thick, BoxY, Thick, BoxH);
		float StampTextX = SX + StatusW * 0.5f;
		if (!bOpen)
		{
			Padlock(SX + 30.f * U, BoxY + BoxH * 0.5f, 22.f * U, StampCol);
			StampTextX += 14.f * U;
		}
		Txt(StampText, HeadFont(), FitPx(StampText, HeadFont(), 24.f, StatusW - 70.f * U), StampTextX, BoxY + BoxH * 0.5f - 0.62f * 24.f * U, StampCol, 1);
		FString Under;
		if (bCollected)
		{
			Under = TEXT("Boss beaten. Nice work.");
		}
		else if (bOpen)
		{
			Under = TEXT("Beat the boss to collect it.");
		}
		else if (B.bRequiresAllContracts)
		{
			Under = TEXT("Collect every other contract first.");
		}
		else
		{
			const FSQBiomeDef* Need = D->FindBiome(B.RequiresContract);
			Under = FString::Printf(TEXT("Collect %s first."), Need ? *Need->Name : *B.RequiresContract.ToString());
		}
		TxtWrap(Under, LabelFont(), 13.f, SX, BoxY + BoxH + 8.f * U, StatusW, SQUI::Muted, 16.f * U, 2);
		DrawAlpha = 1.f;
	}
}

// ================================================================ QUESTS (Performance Reviews)

void ASQMenuHUD::DrawQuestsPage()
{
	if (DrawPageFrame(TEXT("UNLOCKS"), 0.86f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	const USQData* D = USQData::Get(this);
	const USQSaveGame* Save = GetSave();
	const TArray<FSQQuestDef>& All = SQQuests::Quests();
	if (All.Num() == 0 || !Save)
	{
		Txt(TEXT("No quests found (Content/Data/Quests.csv)."), LabelFont(true), 20.f, X0, 200.f * U, SQUI::Danger);
		return;
	}
	const float Elapsed = TimeNow - PageChangeTime;
	const float Top = 170.f * U, Bottom = Canvas->ClipY - 52.f * U - 24.f * U;
	const float RW = FMath::Min(CW - 2.f * X0, 1500.f * U);
	const float RX = (CW - RW) * 0.5f;

	// Categories in the order they first appear in the CSV (tab 0 = all)
	TArray<FName> Tabs;
	Tabs.Add(NAME_None);
	for (const FSQQuestDef& Quest : All)
	{
		if (!Quest.Category.IsNone()) Tabs.AddUnique(Quest.Category);
	}
	if (bNavRight || bNavLeft)
	{
		QuestTab = (QuestTab + (bNavRight ? 1 : Tabs.Num() - 1)) % Tabs.Num();
		QuestRow = 0;
		PageFocus = 0;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	QuestTab = FMath::Clamp(QuestTab, 0, Tabs.Num() - 1);

	// Header: how many are done, banked gold
	DrawAlpha = MenuAppear(Elapsed, 0.05f, 0.3f);
	const int32 DoneCount = Save->QuestsDone.Num();
	const float HeadW = Txt(FString::Printf(TEXT("UNLOCKED  %d / %d"), DoneCount, All.Num()), HeadFont(), 26.f, RX, Top, SQUI::BoneWhite);
	Round(SQUI::Line, RX + HeadW + 20.f * U, Top + 12.f * U, 220.f * U, 10.f * U, 5.f * U);
	Round(SQUI::Soul, RX + HeadW + 20.f * U, Top + 12.f * U, FMath::Max(10.f * U, 220.f * U * DoneCount / FMath::Max(1, All.Num())), 10.f * U, 5.f * U);
	Txt(FString::Printf(TEXT("BANKED GOLD  %d"), Save->OvertimePay), HeadFont(), 22.f, RX + RW, Top + 2.f * U, SQUI::Amber, 2);

	// Tabs
	float TabX = RX;
	const float TabY = Top + 46.f * U, TabH = 36.f * U;
	for (int32 TabIdx = 0; TabIdx < Tabs.Num(); ++TabIdx)
	{
		const FString Label = TabIdx == 0 ? FString(TEXT("ALL")) : Tabs[TabIdx].ToString().ToUpper();
		const float TabW = TxtWidth(Label, LabelFont(true), 15.f) + 34.f * U;
		const bool bSel = TabIdx == QuestTab;
		const bool bOver = MouseIn(TabX, TabY, TabW, TabH);
		if (bOver && bClick && !bSel)
		{
			QuestTab = TabIdx;
			QuestRow = 0;
			PageFocus = 0;
			PlayUI(TEXT("Soul"), 0.14f, 1.5f);
		}
		Round(bSel ? SQUI::Soul : (bOver ? SQUI::PanelHover : SQUI::Panel), TabX, TabY, TabW, TabH, TabH * 0.5f);
		Txt(Label, LabelFont(true), 15.f, TabX + TabW * 0.5f, TabY + 8.f * U, bSel ? SQUI::Midnight : SQUI::Muted, 1);
		TabX += TabW + 8.f * U;
	}
	DrawAlpha = 1.f;

	// The list: undone first (closest to done on top), then done
	TArray<const FSQQuestDef*> List;
	for (const FSQQuestDef& Quest : All)
	{
		if (QuestTab == 0 || Quest.Category == Tabs[QuestTab]) List.Add(&Quest);
	}
	List.StableSort([Save](const FSQQuestDef& QA, const FSQQuestDef& QB)
	{
		const bool bNewA = Save->QuestsNew.Contains(QA.Id), bNewB = Save->QuestsNew.Contains(QB.Id);
		if (bNewA != bNewB) return bNewA; // just unlocked: on top
		const bool bDoneA = Save->HasQuest(QA.Id), bDoneB = Save->HasQuest(QB.Id);
		if (bDoneA != bDoneB) return !bDoneA;
		const float FracA = static_cast<float>(SQQuests::Progress(QA, Save, nullptr)) / static_cast<float>(FMath::Max(1, QA.Target));
		const float FracB = static_cast<float>(SQQuests::Progress(QB, Save, nullptr)) / static_cast<float>(FMath::Max(1, QB.Target));
		return FracA > FracB;
	});

	const int32 Cols = 2;
	const float Gap = 12.f * U, CardH = 96.f * U;
	const float ListTop = TabY + TabH + 18.f * U;
	const float CardW = (RW - Gap * (Cols - 1)) / Cols;
	const int32 VisibleRows = FMath::Max(1, FMath::FloorToInt((Bottom - ListTop + Gap) / (CardH + Gap)));
	const int32 TotalRows = (List.Num() + Cols - 1) / Cols;
	const int32 MaxRow = FMath::Max(0, TotalRows - VisibleRows);
	if (bWheelDown) QuestRow++;
	if (bWheelUp) QuestRow--;
	if (List.Num() > 0)
	{
		if (bNavDown) PageFocus = FMath::Min(PageFocus + Cols, List.Num() - 1);
		if (bNavUp) PageFocus = FMath::Max(PageFocus - Cols, 0);
		PageFocus = FMath::Clamp(PageFocus, 0, List.Num() - 1);
		if (bNavDown || bNavUp)
		{
			const int32 FocusRow = PageFocus / Cols;
			if (FocusRow < QuestRow) QuestRow = FocusRow;
			if (FocusRow >= QuestRow + VisibleRows) QuestRow = FocusRow - VisibleRows + 1;
			PlayUI(TEXT("Soul"), 0.12f, 1.5f);
		}
	}
	QuestRow = FMath::Clamp(QuestRow, 0, MaxRow);

	for (int32 ListIdx = QuestRow * Cols; ListIdx < List.Num() && ListIdx < (QuestRow + VisibleRows) * Cols; ++ListIdx)
	{
		const FSQQuestDef& Quest = *List[ListIdx];
		const int32 Slot = ListIdx - QuestRow * Cols;
		const float CX = RX + (Slot % Cols) * (CardW + Gap);
		const float CY = ListTop + (Slot / Cols) * (CardH + Gap);
		const float In = MenuAppear(Elapsed, 0.1f + 0.02f * Slot, 0.3f);
		if (In <= 0.01f) continue;
		const bool bDone = Save->HasQuest(Quest.Id);
		const int32 Have = FMath::Min(SQQuests::Progress(Quest, Save, nullptr), Quest.Target);
		const bool bOver = MouseIn(CX, CY, CardW, CardH);
		if (bOver && bMouseMoved) PageFocus = ListIdx;
		const bool bFocus = PageFocus == ListIdx;
		DrawAlpha = In;
		const bool bNewUnlock = Save->QuestsNew.Contains(Quest.Id);
		const FLinearColor Edge = bNewUnlock ? SQUI::Gold : (bDone ? SQUI::Soul : (bFocus ? SQUI::Amber : SQUI::Line));
		Round(FLinearColor(0.f, 0.f, 0.f, 0.3f), CX + 4.f * U, CY + 6.f * U, CardW, CardH, 14.f * U);
		Round(Edge, CX - 2.f * U, CY - 2.f * U, CardW + 4.f * U, CardH + 4.f * U, 16.f * U);
		Round(bFocus ? SQUI::PanelHover : SQUI::Panel, CX, CY, CardW, CardH, 14.f * U);

		const float TX = CX + 18.f * U, TW = CardW - 36.f * U;
		const FString Category = Quest.Category.IsNone() ? FString() : Quest.Category.ToString().ToUpper();
		Txt(Category, LabelFont(true), 11.f, TX, CY + 9.f * U, SQUI::Muted);
		const FString Name = Quest.Name.ToUpper();
		Txt(Name, HeadFont(), FitPx(Name, HeadFont(), 21.f, TW * 0.62f), TX, CY + 24.f * U, bDone ? SQUI::Soul : SQUI::BoneWhite);
		const FString Reward = SQQuests::DescribeRewards(Quest, D);
		Txt(Reward, LabelFont(true), FitPx(Reward, LabelFont(true), 13.f, TW * 0.4f), CX + CardW - 18.f * U, CY + 28.f * U, SQUI::Amber, 2);
		TxtWrap(Quest.Description, LabelFont(), 14.f, TX, CY + 52.f * U, TW * 0.62f, SQUI::Muted, 18.f * U, 1);
		// progress bar (or the DONE stamp)
		const float BarX = CX + CardW * 0.66f, BarW = CardW * 0.34f - 18.f * U, BarY = CY + 62.f * U;
		if (bDone)
		{
			Txt(bNewUnlock ? TEXT("NEW!  UNLOCKED") : TEXT("UNLOCKED"), HeadFont(), 18.f, CX + CardW - 18.f * U, CY + 56.f * U, bNewUnlock ? SQUI::Gold : SQUI::Soul, 2);
		}
		else
		{
			Round(SQUI::Line, BarX, BarY, BarW, 10.f * U, 5.f * U);
			Round(SQUI::Amber, BarX, BarY, FMath::Max(10.f * U, BarW * Have / FMath::Max(1, Quest.Target)), 10.f * U, 5.f * U);
			const FString Count = FString::Printf(TEXT("%s / %s"), *FText::AsNumber(Have).ToString(), *FText::AsNumber(Quest.Target).ToString());
			Txt(Count, LabelFont(true), 12.f, BarX + BarW, BarY - 18.f * U, SQUI::Muted, 2);
			if (Quest.Mode == ESQQuestMode::Best) Txt(TEXT("IN ONE RUN"), LabelFont(true), 10.f, BarX, BarY - 16.f * U, SQUI::Disabled);
		}
		DrawAlpha = 1.f;
	}
	if (MaxRow > 0)
	{
		// a thin scroll track on the right
		const float TrackX = RX + RW + 10.f * U, TrackH = Bottom - ListTop;
		Round(SQUI::Line, TrackX, ListTop, 4.f * U, TrackH, 2.f * U);
		const float ThumbH = TrackH * VisibleRows / FMath::Max(1, TotalRows);
		Round(SQUI::Soul, TrackX, ListTop + (TrackH - ThumbH) * QuestRow / MaxRow, 4.f * U, ThumbH, 2.f * U);
	}
}

// ================================================================ SETTINGS

void ASQMenuHUD::DrawSettingsPage()
{
	if (DrawPageFrame(TEXT("SETTINGS"), 0.82f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!Canvas || !PC)
	{
		return;
	}
	// The shared panel (SQSave.h), same as P in a run: CAMERA / GAMEPLAY / AUDIO / CONTROLS. It writes the profile itself.
	FSQTutorialPainter Painter;
	FillMenuPainter(Painter);
	Painter.bTabNext = PC->WasInputKeyJustPressed(EKeys::Tab);
	const float PanelX = (Painter.ViewW - FSQSettingsPanel::PanelW) * 0.5f;
	const float PanelY = FMath::Max(150.f, (Painter.ViewH - FSQSettingsPanel::PanelH()) * 0.5f + 20.f);
	DrawAlpha = 1.f;
	const FSQSettingsResult Result = MenuSettings.Draw(Painter, GetWorld(), PanelX, PanelY, TEXT("ESC"), TEXT("back"),
		TEXT("Saved at once. The same settings open with P in a run."), TEXT("Camera changes show in your next run (or press P in one)."), false);
	DrawAlpha = 1.f;
	if (Result.bAteClick) bClick = false;
	if (Result.bAudioChanged) ApplyAudio();
	if (Result.bReplayTraining) ShowToast(TEXT("The 3 how-to-play prompts will show on your next run."), SQUI::Soul);
	if (Result.bClose) OpenPage(ESQMenuPage::Main);
}

// ================================================================ CREDITS

void ASQMenuHUD::DrawCreditsPage()
{
	if (DrawPageFrame(TEXT("CREDITS"), 0.82f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	static const FMenuCreditLine Lines[] = {
		{ 0, TEXT("SOUL QUOTA") }, { 3, TEXT("The dead won't collect themselves.") }, { 4, nullptr },
		{ 1, TEXT("A GAME BY") }, { 2, TEXT("Jack McGuigan") }, { 4, nullptr },
		{ 1, TEXT("DESIGN AND CODE") }, { 2, TEXT("Jack McGuigan") }, { 3, TEXT("with help from Claude (Anthropic)") }, { 4, nullptr },
		{ 1, TEXT("CHARACTERS & PROPS") }, { 2, TEXT("KayKit by Kay Lousberg") }, { 3, TEXT("Skeletons Character Pack and Halloween Bits") },
		{ 3, TEXT("CC0 licence (public domain)") }, { 4, nullptr },
		{ 1, TEXT("FONTS") }, { 2, TEXT("Creepster by Font Diner") }, { 3, TEXT("SIL Open Font License 1.1") },
		{ 2, TEXT("Lilita One by Juan Montoreano") }, { 3, TEXT("SIL Open Font License 1.1") },
		{ 2, TEXT("Rubik by The Rubik Project Authors") }, { 3, TEXT("SIL Open Font License 1.1") },
		{ 3, TEXT("The licence texts come with the game.") }, { 4, nullptr },
		{ 1, TEXT("SOUND") }, { 2, TEXT("Sound effects and music made for this game") }, { 4, nullptr },
		{ 1, TEXT("ENGINE") }, { 2, TEXT("Soul Quota uses Unreal\u00AE Engine.") },
		{ 3, TEXT("Unreal\u00AE is a trademark or registered trademark of Epic Games, Inc.") },
		{ 3, TEXT("in the United States of America and elsewhere.") },
		{ 3, TEXT("Unreal\u00AE Engine, Copyright 1998 - 2026, Epic Games, Inc. All rights reserved.") }, { 4, nullptr },
		{ 1, TEXT("PLAYTESTERS") }, { 2, TEXT("Everyone who reaped in the early builds") }, { 4, nullptr }, { 4, nullptr },
		{ 0, TEXT("THANKS FOR PLAYING") }, { 3, TEXT("Now get reaping. Midnight's coming.") },
	};
	auto LineHeight = [U](uint8 Kind) { return (Kind == 0 ? 112.f : Kind == 1 ? 40.f : Kind == 2 ? 48.f : Kind == 3 ? 30.f : 34.f) * U; };
	float Total = 0.f;
	for (const FMenuCreditLine& L : Lines)
	{
		Total += LineHeight(L.Kind);
	}

	const float In = MenuAppear(TimeNow - PageChangeTime, 0.1f, 0.4f);
	const float PW = FMath::Min(1000.f * U, Canvas->ClipX - 80.f * U);
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = 170.f * U + 30.f * U * (1.f - In);
	const float PH = Canvas->ClipY - 52.f * U - 40.f * U - PY;
	DrawAlpha = In;
	PanelBox(PX, PY, PW, PH);

	// rolls up by itself; the wheel or Up / Down scroll faster; starts again at the end
	APlayerController* PC = GetOwningPlayerController();
	const bool bHoldDown = PC && !bQuitAsk && (PC->IsInputKeyDown(EKeys::Down) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down) || PC->IsInputKeyDown(EKeys::Gamepad_LeftStick_Down));
	const bool bHoldUp = PC && !bQuitAsk && (PC->IsInputKeyDown(EKeys::Up) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up) || PC->IsInputKeyDown(EKeys::Gamepad_LeftStick_Up));
	CreditsScroll += DeltaT * 46.f * U;
	CreditsScroll += bHoldDown ? DeltaT * 420.f * U : 0.f;
	CreditsScroll += bWheelDown ? 90.f * U : 0.f;
	CreditsScroll -= bHoldUp ? DeltaT * 420.f * U : 0.f;
	CreditsScroll -= bWheelUp ? 90.f * U : 0.f;
	CreditsScroll = FMath::Max(0.f, CreditsScroll);
	if (CreditsScroll > Total + PH)
	{
		CreditsScroll = 0.f;
	}

	const float AreaTop = PY + 20.f * U, AreaBottom = PY + PH - 20.f * U;
	const float MidX = PX + PW * 0.5f;
	float LY = AreaBottom - CreditsScroll;
	for (const FMenuCreditLine& L : Lines)
	{
		const float LH = LineHeight(L.Kind);
		if (L.Text && LY > AreaTop && LY + LH < AreaBottom)
		{
			const float Edge = 70.f * U;
			DrawAlpha = In * FMath::Clamp(FMath::Min(LY - AreaTop, AreaBottom - (LY + LH)) / Edge, 0.f, 1.f);
			const FString Text(L.Text);
			switch (L.Kind)
			{
			case 0: Txt(Text, DisplayFont(), FitPx(Text, DisplayFont(), 80.f, PW - 60.f * U), MidX, LY, SQUI::Soul, 1, true); break;
			case 1: Txt(Text, LabelFont(true), 16.f, MidX, LY + 8.f * U, SQUI::Amber, 1); break;
			case 2: Txt(Text, HeadFont(), FitPx(Text, HeadFont(), 32.f, PW - 60.f * U), MidX, LY, SQUI::BoneWhite, 1); break;
			default: Txt(Text, LabelFont(), FitPx(Text, LabelFont(), 16.f, PW - 60.f * U), MidX, LY, SQUI::Muted, 1); break;
			}
		}
		LY += LH;
	}
	DrawAlpha = 1.f;
}

// ================================================================ pop-ups, bars, transitions

void ASQMenuHUD::DrawQuitConfirm()
{
	const float U = UnitPx;
	const float A = MenuEaseOut((TimeNow - QuitAskTime) / 0.3f);
	Rect(SQUI::WithAlpha(SQUI::Midnight, 0.78f * A), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const float W = FMath::Min(720.f * U, Canvas->ClipX - 40.f * U), H = 340.f * U;
	const float X = (Canvas->ClipX - W) * 0.5f, Y = (Canvas->ClipY - H) * 0.5f - 40.f * U * (1.f - A);
	DrawAlpha = A;
	// an end-of-shift time card
	Round(FLinearColor(0.f, 0.f, 0.f, 0.45f), X + 10.f * U, Y + 14.f * U, W, H, 10.f * U);
	Round(SQUI::Card, X, Y, W, H, 10.f * U);
	for (float HX = X + 24.f * U; HX < X + W - 24.f * U; HX += 24.f * U)
	{
		Round(SQUI::WithAlpha(SQUI::Midnight, 0.85f), HX, Y + 10.f * U, 9.f * U, 9.f * U, 4.5f * U);
	}
	Txt(TEXT("DEATH'S OFFICE  \u00B7  LEAVING SO SOON"), LabelFont(true), 14.f, X + 44.f * U, Y + 34.f * U, SQUI::WithAlpha(SQUI::CardInk, 0.55f));
	Txt(TEXT("LEAVE?"), HeadFont(), 54.f, X + 44.f * U, Y + 54.f * U, SQUI::Midnight);
	for (float DX = X + 44.f * U; DX < X + W - 44.f * U; DX += 18.f * U)
	{
		Rect(SQUI::WithAlpha(SQUI::CardInk, 0.22f), DX, Y + 130.f * U, 9.f * U, 3.f * U);
	}
	TxtWrap(TEXT("Quit to the desktop? The souls will pile up without you."), LabelFont(), 20.f, X + 44.f * U, Y + 148.f * U, W - 88.f * U,
		SQUI::CardInk, 26.f * U, 2);

	if (bNavLeft || bNavRight || bNavUp || bNavDown)
	{
		QuitFocus = QuitFocus == 0 ? 1 : 0;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	const float ButH = 70.f * U, ButY = Y + H - 40.f * U - ButH, ButW = (W - 88.f * U - 16.f * U) * 0.5f;
	if (DrawPageButton(TEXT("STAY"), X + 44.f * U, ButY, ButW, ButH, 1, true, 0, QuitFocus))
	{
		bQuitAsk = false;
		PlayUI(TEXT("Dash"), 0.2f, 0.8f);
	}
	if (DrawPageButton(TEXT("LEAVE"), X + 44.f * U + ButW + 16.f * U, ButY, ButW, ButH, 3, true, 1, QuitFocus))
	{
		if (bSettingsDirty)
		{
			StoreSave();
			bSettingsDirty = false;
		}
		PlayUI(TEXT("Bell"), 0.4f);
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayerController(), EQuitPreference::Quit, false);
	}
	DrawAlpha = 1.f;
}

void ASQMenuHUD::DrawBottomBar()
{
	const float U = UnitPx;
	const float CW = Canvas->ClipX, BarH = 52.f * U, BarY = Canvas->ClipY - BarH;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	DrawAlpha = MenuAppear(TimeNow - MenuOpenedAt, 0.3f, 0.5f);
	Rect(SQUI::WithAlpha(SQUI::Bar, 0.94f), 0.f, BarY, CW, BarH);
	Rect(SQUI::Line, 0.f, BarY, CW, 1.5f * U);
	float HX = X0;
	auto Hint = [&](const FString& KeyName, const FString& What)
	{
		HX += KeyCap(KeyName, HX, BarY + 13.f * U) + 10.f * U;
		HX += Txt(What, LabelFont(true), 14.f, HX, BarY + 16.f * U, SQUI::Muted) + 26.f * U;
	};
	const bool bPad = bUsingPad;
	if (Page == ESQMenuPage::Settings)
	{
		Hint(bPad ? TEXT("D-PAD") : TEXT("UP / DOWN"), TEXT("SELECT"));
		Hint(bPad ? TEXT("D-PAD") : TEXT("LEFT / RIGHT"), TEXT("ADJUST"));
	}
	else if (Page == ESQMenuPage::Credits)
	{
		Hint(bPad ? TEXT("D-PAD") : TEXT("WHEEL / ARROWS"), TEXT("SCROLL"));
	}
	else if (Page == ESQMenuPage::Join)
	{
		Hint(TEXT("KEYBOARD"), TEXT("TYPE IP"));
	}
	else
	{
		Hint(bPad ? TEXT("D-PAD") : TEXT("ARROWS"), TEXT("SELECT"));
		if (Page == ESQMenuPage::Reapers && bReaperPaged)
		{
			Hint(bPad ? TEXT("LB / RB") : TEXT("WHEEL"), TEXT("PAGE"));
		}
		if (Page == ESQMenuPage::Quests)
		{
			Hint(bPad ? TEXT("D-PAD") : TEXT("LEFT / RIGHT"), TEXT("CATEGORY"));
			Hint(TEXT("WHEEL"), TEXT("SCROLL"));
		}
		if (Page == ESQMenuPage::Missions)
		{
			Hint(bPad ? TEXT("Y") : TEXT("H"), TEXT("HARD MODE"));
		}
		if (Page == ESQMenuPage::Upgrades)
		{
			Hint(TEXT("WHEEL"), TEXT("SCROLL"));
		}
	}
	Hint(bPad ? TEXT("A") : TEXT("ENTER"), Page == ESQMenuPage::Reapers ? TEXT("HIRE / PICK")
		: (Page == ESQMenuPage::Missions ? TEXT("SELECT / START") : (Page == ESQMenuPage::Upgrades ? TEXT("BUY") : TEXT("CONFIRM"))));
	Hint(bPad ? TEXT("B") : TEXT("ESC"), Page == ESQMenuPage::Main ? TEXT("QUIT") : TEXT("BACK"));
	const FString Build(TEXT("SOUL QUOTA  \u00B7  PROTOTYPE BUILD"));
	if (HX + TxtWidth(Build, LabelFont(true), 13.f) < CW - X0)
	{
		Txt(Build, LabelFont(true), 13.f, CW - X0, BarY + 17.f * U, SQUI::Disabled, 2);
	}
	DrawAlpha = 1.f;
}

void ASQMenuHUD::DrawToast()
{
	if (ToastText.IsEmpty())
	{
		return;
	}
	const float Age = TimeNow - ToastTime;
	const float Life = 3.6f;
	if (Age < 0.f || Age > Life)
	{
		return;
	}
	const float U = UnitPx;
	DrawAlpha = FMath::Min(FMath::Clamp(Age / 0.2f, 0.f, 1.f), FMath::Clamp((Life - Age) / 0.4f, 0.f, 1.f));
	const float W = TxtWidth(ToastText, LabelFont(true), 18.f) + 64.f * U, H = 46.f * U;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY - 52.f * U - 72.f * U + 16.f * U * (1.f - MenuEaseOut(Age / 0.25f));
	Round(FLinearColor(0.f, 0.f, 0.f, 0.4f), X + 4.f * U, Y + 6.f * U, W, H, 14.f * U);
	Round(ToastColour, X - 2.f * U, Y - 2.f * U, W + 4.f * U, H + 4.f * U, 16.f * U);
	Round(SQUI::Panel, X, Y, W, H, 14.f * U);
	Round(ToastColour, X + 18.f * U, Y + H * 0.5f - 5.f * U, 10.f * U, 10.f * U, 5.f * U);
	Txt(ToastText, LabelFont(true), 18.f, X + 40.f * U, Y + H * 0.5f - 0.62f * 18.f * U, SQUI::BoneWhite);
	DrawAlpha = 1.f;
}

void ASQMenuHUD::DrawInkWipe()
{
	// an ink wipe with a drippy, soul-lit edge whenever the page changes (not when the menu first opens)
	if (PageChangeTime < MenuOpenedAt)
	{
		return;
	}
	const float T = (TimeNow - PageChangeTime) / 0.4f;
	if (T < 0.f || T >= 1.f)
	{
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	const bool bToRight = Page != ESQMenuPage::Main; // opening a page: the ink rolls off to the right; back to the menu: to the left
	const float E = FMath::InterpEaseInOut(0.f, 1.f, T, 2.2f);
	const float Strip = 22.f * U;
	int32 k = 0;
	for (float SY = 0.f; SY < CH; SY += Strip, ++k)
	{
		const float Drip = (0.5f + 0.5f * FMath::Sin(k * 1.7f + 0.6f)) * (0.6f + 0.4f * FMath::Sin(k * 0.53f));
		const float Bulge = 80.f * U * Drip * (1.f - 0.4f * E);
		if (bToRight)
		{
			const float Left = CW * E * 1.15f - Bulge;
			Rect(SQUI::Midnight, Left, SY, CW - Left, Strip + 1.f);
			Round(SQUI::Midnight, Left - Strip * 0.5f, SY, Strip, Strip, Strip * 0.5f);
			Rect(SQUI::WithAlpha(SQUI::Soul, 0.6f * (1.f - E)), Left - Strip * 0.5f - 3.f * U, SY + 4.f * U, 3.f * U, Strip - 8.f * U);
		}
		else
		{
			const float RightEdge = CW * (1.f - E * 1.15f) + Bulge;
			Rect(SQUI::Midnight, 0.f, SY, RightEdge, Strip + 1.f);
			Round(SQUI::Midnight, RightEdge - Strip * 0.5f, SY, Strip, Strip, Strip * 0.5f);
			Rect(SQUI::WithAlpha(SQUI::Soul, 0.6f * (1.f - E)), RightEdge + Strip * 0.5f, SY + 4.f * U, 3.f * U, Strip - 8.f * U);
		}
	}
}

void ASQMenuHUD::StartTravel(bool bJoin, bool bHostLobby)
{
	if (bTravelling)
	{
		return;
	}
	if (!bJoin && !FPackageName::DoesPackageExist(HostMap))
	{
		ShowToast(TEXT("The Graveyard map is missing. Run Scripts/setup_project.py once."), SQUI::Danger);
		PlayUI(TEXT("Hit"), 0.3f, 1.2f);
		return;
	}
	USQSaveGame* Save = GetSave();
	if (bJoin)
	{
		Address = Address.TrimStartAndEnd();
		if (Address.IsEmpty())
		{
			Address = TEXT("127.0.0.1");
		}
		if (Save)
		{
			Save->LastJoinAddress = Address;
		}
	}
	if (Save)
	{
		Save->PreferredReaper = PreferredReaper();
	}
	StoreSave();
	bSettingsDirty = false;
	bQuitAsk = false;
	bTravelling = true;
	bTravelJoin = bJoin;
	bTravelSolo = !bJoin && !bHostLobby;
	bTravelFired = false;
	bStampSoundPlayed = false;
	TravelStart = TimeNow;
	PlayUI(TEXT("Bank"), 0.55f);
	if (Showroom.IsValid())
	{
		Showroom->Celebrate();
	}
	if (MenuMusic)
	{
		MenuMusic->FadeOut(1.2f, 0.f);
	}
}

void ASQMenuHUD::FireTravel()
{
	bTravelFired = true;
	PlayUI(TEXT("Bell"), 0.35f);
	if (bTravelJoin)
	{
		if (APlayerController* PC = GetOwningPlayerController())
		{
			PC->ClientTravel(Address, ETravelType::TRAVEL_Absolute);
		}
		return;
	}
	// Solo: still a listen server (a friend can join mid-run by IP), with SQSolo so the game mode skips the lobby
	UGameplayStatics::OpenLevel(this, FName(*HostMap), true, bTravelSolo ? TEXT("listen?SQSolo=1") : TEXT("listen"));
}

void ASQMenuHUD::DrawClockInOverlay()
{
	if (!bTravelling)
	{
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	const float T = TimeNow - TravelStart;

	// joining can take a while: Esc / B cancels
	if (bTravelFired && bTravelJoin && bBack)
	{
		bBack = false;
		if (APlayerController* PC = GetOwningPlayerController())
		{
			PC->ConsoleCommand(TEXT("cancel"));
		}
		bTravelling = false;
		if (MenuMusic)
		{
			MenuMusic->FadeIn(1.f, 1.f);
		}
		ShowToast(TEXT("Join cancelled."), SQUI::Muted);
		return;
	}

	Rect(SQUI::WithAlpha(SQUI::Midnight, 0.7f * FMath::Clamp(T / 0.35f, 0.f, 1.f)), 0.f, 0.f, CW, CH);

	// the time card drops in and gets stamped
	const float A = MenuEaseOut(T / 0.35f);
	const float W = FMath::Min(780.f * U, CW - 40.f * U), H = 300.f * U;
	const float X = (CW - W) * 0.5f, Y = (CH - H) * 0.5f - 60.f * U * (1.f - A);
	DrawAlpha = A;
	Round(FLinearColor(0.f, 0.f, 0.f, 0.45f), X + 10.f * U, Y + 14.f * U, W, H, 10.f * U);
	Round(SQUI::Card, X, Y, W, H, 10.f * U);
	for (float HX = X + 24.f * U; HX < X + W - 24.f * U; HX += 24.f * U)
	{
		Round(SQUI::WithAlpha(SQUI::Midnight, 0.85f), HX, Y + 10.f * U, 9.f * U, 9.f * U, 4.5f * U);
	}
	for (const float RowY : { 120.f, 160.f, 200.f })
	{
		Rect(SQUI::WithAlpha(SQUI::CardInk, 0.1f), X + 30.f * U, Y + RowY * U + 32.f * U, W - 60.f * U, 1.5f * U);
	}
	Txt(TEXT("DEATH'S OFFICE  \u00B7  SOUL LEDGER"), LabelFont(true), 14.f, X + 40.f * U, Y + 32.f * U, SQUI::WithAlpha(SQUI::CardInk, 0.55f));
	Txt(bTravelJoin ? TEXT("JOINING A FRIEND'S RUN") : TEXT("RAISING THE DEAD..."), HeadFont(), 40.f, X + 40.f * U, Y + 54.f * U, SQUI::Midnight);
	const USQData* D = USQData::Get(this);
	const FSQReaperDef* Def = D ? D->FindReaper(PreferredReaper()) : nullptr;
	Txt(FString::Printf(TEXT("REAPER:  %s"), Def ? *Def->Name.ToUpper() : TEXT("GRIM")), LabelFont(true), 19.f, X + 40.f * U, Y + 128.f * U, SQUI::CardInk);
	Txt(bTravelJoin ? FString::Printf(TEXT("ADDRESS:  %s"), *Address) : FString(bTravelSolo ? TEXT("SOLO SHIFT  \u00B7  NO LOBBY") : TEXT("CO-OP LOBBY  \u00B7  YOU HOST")), LabelFont(true), 19.f,
		X + 40.f * U, Y + 168.f * U, SQUI::CardInk);
	Txt(TEXT("IN  23:00"), HeadFont(), 30.f, X + 40.f * U, Y + H - 62.f * U, SQUI::Midnight);

	// CLOCKED IN stamp
	const float StampT = (T - 0.5f) / 0.16f;
	if (StampT > 0.f)
	{
		const float P = FMath::Clamp(StampT, 0.f, 1.f);
		const float Grow = 1.7f - 0.7f * MenuEaseOut(P);
		const FString StampWord(TEXT("LET'S REAP"));
		const float SPx = 36.f * Grow;
		const float SW = TxtWidth(StampWord, HeadFont(), SPx) + 40.f * U * Grow, SH = 64.f * U * Grow, Thick = 4.f * U * Grow;
		const float SCX = X + W - 200.f * U, SCY = Y + H - 82.f * U;
		const FLinearColor StampInk = SQUI::WithAlpha(SQUI::Stamp, 0.92f);
		DrawAlpha = A * P;
		Rect(StampInk, SCX - SW * 0.5f, SCY - SH * 0.5f, SW, Thick);
		Rect(StampInk, SCX - SW * 0.5f, SCY + SH * 0.5f - Thick, SW, Thick);
		Rect(StampInk, SCX - SW * 0.5f, SCY - SH * 0.5f, Thick, SH);
		Rect(StampInk, SCX + SW * 0.5f - Thick, SCY - SH * 0.5f, Thick, SH);
		Txt(StampWord, HeadFont(), SPx, SCX, SCY - 0.62f * SPx * U, StampInk, 1);
		if (P >= 1.f && !bStampSoundPlayed)
		{
			bStampSoundPlayed = true;
			PlayUI(TEXT("Hit"), 0.45f, 0.6f);
		}
	}
	DrawAlpha = 1.f;

	// then the ink closes in and we go
	const float Close = MenuEaseOut((T - 0.95f) / 0.45f);
	if (Close > 0.f)
	{
		Rect(SQUI::WithAlpha(SQUI::Midnight, Close), 0.f, 0.f, CW, CH);
		DrawAlpha = Close;
		const FString Going = bTravelJoin ? FString::Printf(TEXT("CONNECTING TO %s..."), *Address) : FString(TEXT("THE GRAVEYARD AWAITS..."));
		Txt(Going, HeadFont(), 30.f, CW * 0.5f, CH * 0.5f - 20.f * U, SQUI::Soul, 1);
		if (bTravelJoin && bTravelFired && T > 2.5f)
		{
			Txt(bUsingPad ? TEXT("B  TO CANCEL") : TEXT("ESC  TO CANCEL"), LabelFont(true), 15.f, CW * 0.5f, CH * 0.5f + 30.f * U, SQUI::Muted, 1);
		}
		DrawAlpha = 1.f;
	}
	if (T >= 1.45f && !bTravelFired)
	{
		FireTravel();
	}
}

// ================================================================ MISSIONS (Nov 9): map cards, hard mode, the pending mission

void ASQMenuHUD::DrawMissionsPage()
{
	if (DrawPageFrame(TEXT("MISSIONS"), 0.84f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	const USQData* D = USQData::Get(this);
	USQSaveGame* Save = GetSave();
	TArray<FSQMissionView> Missions;
	USQMissions::GetMissionList(D, Save, Missions);
	if (Missions.Num() == 0)
	{
		Txt(TEXT("No missions found (Content/Data/Missions.csv)."), LabelFont(true), 20.f, X0, 200.f * U, SQUI::Danger);
		return;
	}
	const int32 Count = FMath::Min(Missions.Num(), 8);
	PageFocus = FMath::Clamp(PageFocus, 0, Count - 1);
	bool bPendingHard = false;
	const FName Pending = USQMissions::GetPendingMission(this, bPendingHard);
	const float Elapsed = TimeNow - PageChangeTime;

	// keys: Left / Right browse, Enter selects (Enter on the selected card starts), H / pad Y toggles hard mode
	if (bNavRight || bNavDown)
	{
		PageFocus = (PageFocus + 1) % Count;
		bMissionHard = Missions[PageFocus].Id == Pending ? bPendingHard : false;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (bNavLeft || bNavUp)
	{
		PageFocus = (PageFocus + Count - 1) % Count;
		bMissionHard = Missions[PageFocus].Id == Pending ? bPendingHard : false;
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	const FSQMissionView& Focus = Missions[PageFocus];
	bool bToggleHard = false;
	if (APlayerController* PC = GetOwningPlayerController())
	{
		bToggleHard = PC->WasInputKeyJustPressed(EKeys::H) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top);
	}

	const float Top = 176.f * U, Bottom = Canvas->ClipY - 52.f * U - 24.f * U;
	const float RW = FMath::Min(CW - 2.f * X0, 1640.f * U);
	const float RX = (CW - RW) * 0.5f;
	DrawAlpha = MenuAppear(Elapsed, 0.05f, 0.3f);
	Txt(TEXT("Pick a map. Each mission is one 15-20 minute stage: objectives, two minibosses, then the boss."), LabelFont(), 17.f, RX, Top, SQUI::Muted);
	DrawAlpha = 1.f;

	// ---- the cards
	const float Gap = 18.f * U;
	const float CardW = FMath::Min(330.f * U, (RW - Gap * (Count - 1)) / Count);
	const float RowW = Count * CardW + (Count - 1) * Gap;
	const float CardX0 = (CW - RowW) * 0.5f;
	const float CardY = Top + 44.f * U;
	const float ActionH = 64.f * U;
	const float CardH = FMath::Max(300.f * U, Bottom - ActionH - 30.f * U - CardY);
	for (int32 i = 0; i < Count; ++i)
	{
		const FSQMissionView& M = Missions[i];
		const float In = MenuAppear(Elapsed, 0.1f + 0.06f * i, 0.4f);
		if (In <= 0.01f)
		{
			continue;
		}
		const float BaseX = CardX0 + i * (CardW + Gap);
		const bool bOver = MouseIn(BaseX, CardY, CardW, CardH);
		if (bOver && bMouseMoved && PageFocus != i)
		{
			PageFocus = i;
			bMissionHard = M.Id == Pending ? bPendingHard : false;
			PlayUI(TEXT("Soul"), 0.12f, 1.5f);
		}
		const bool bFocus = PageFocus == i;
		const bool bSelected = M.Id == Pending;
		float& Lift = MissionLift[i];
		Lift = FMath::FInterpTo(Lift, bFocus ? 1.f : 0.f, DeltaT, 12.f);
		const float Pop = 1.f + 0.03f * Lift;
		const float W = CardW * Pop, H = CardH * Pop;
		const float X = BaseX - (W - CardW) * 0.5f;
		const float Y = CardY - (H - CardH) * 0.5f - 10.f * U * Lift + 40.f * U * (1.f - In);
		DrawAlpha = In;
		const FLinearColor Edge = !M.bUnlocked ? SQUI::Line : (bSelected ? SQUI::Soul : (bFocus ? SQUI::BoneWhite : SQUI::Line));
		const float R = 18.f * U;

		GlowAround(bSelected ? SQUI::Soul : SQUI::BoneWhite, X, Y, W, H, R, (bSelected ? 0.35f : 0.f) + 0.4f * Lift);
		Round(FLinearColor(0.f, 0.f, 0.f, 0.4f), X + 5.f * U, Y + 9.f * U, W, H, R);
		Round(Edge, X - 2.5f * U, Y - 2.5f * U, W + 5.f * U, H + 5.f * U, R + 2.5f * U);
		Round(FMath::Lerp(SQUI::Panel, SQUI::PanelHover, 0.5f * Lift), X, Y, W, H, R);

		// map art: the biome's sky fading to its ground, a moon, graves on the skyline
		const FSQBiomeDef* B = D ? D->FindBiome(M.Biome) : nullptr;
		const FLinearColor Fog = B ? B->FogColor : SQUI::Line;
		const FLinearColor Ground = B ? B->GroundColor : SQUI::Midnight;
		const FLinearColor Prop = B ? B->PropColor : SQUI::BoneWhite;
		const float ArtX = X + 8.f * U, ArtY = Y + 8.f * U, ArtW = W - 16.f * U, ArtH = H * 0.36f;
		constexpr int32 Bands = 10;
		for (int32 k = 0; k < Bands; ++k)
		{
			const float BandT = float(k) / (Bands - 1);
			Rect(FMath::Lerp(Fog, Ground, BandT * BandT), ArtX, ArtY + ArtH * k / Bands, ArtW, ArtH / Bands + 1.f);
		}
		Round(SQUI::WithAlpha(Prop, 0.9f), ArtX + ArtW * 0.7f, ArtY + ArtH * 0.14f, ArtH * 0.22f, ArtH * 0.22f, ArtH * 0.11f);
		for (int32 g = 0; g < 5; ++g)
		{
			const float GH = ArtH * (0.14f + 0.05f * ((g * 7) % 3));
			Round(SQUI::WithAlpha(SQUI::Midnight, 0.85f), ArtX + ArtW * (0.06f + 0.19f * g), ArtY + ArtH - GH, ArtW * 0.09f, GH + 4.f * U, ArtW * 0.04f);
		}
		// skulls of difficulty, over the art
		for (int32 Pip = 0; Pip < FMath::Clamp(M.Difficulty, 1, 5); ++Pip)
		{
			TiltTile(SQUI::Midnight, ArtX + 12.f * U + Pip * 20.f * U, ArtY + 12.f * U, 14.f * U, 14.f * U, 45.f, 0.5f, 0.5f);
			TiltTile(SQUI::Amber, ArtX + 14.f * U + Pip * 20.f * U, ArtY + 14.f * U, 10.f * U, 10.f * U, 45.f, 0.5f, 0.5f);
		}
		if (M.bCleared || M.bHardCleared)
		{
			const FString Stamp = M.bHardCleared ? FString(TEXT("HARD CLEARED")) : FString(TEXT("CLEARED"));
			const float SW = TxtWidth(Stamp, LabelFont(true), 12.f) + 20.f * U;
			Round(M.bHardCleared ? SQUI::Danger : SQUI::Soul, ArtX + ArtW - SW - 10.f * U, ArtY + 10.f * U, SW, 24.f * U, 12.f * U);
			Txt(Stamp, LabelFont(true), 12.f, ArtX + ArtW - SW * 0.5f - 10.f * U, ArtY + 14.f * U, SQUI::Midnight, 1);
		}

		// text
		const float TX = X + 20.f * U, TW = W - 40.f * U;
		float TY = ArtY + ArtH + 14.f * U;
		const FString Name = M.Name.ToString().ToUpper();
		Txt(Name, HeadFont(), FitPx(Name, HeadFont(), 28.f, TW), TX, TY, M.bUnlocked ? SQUI::BoneWhite : SQUI::Muted);
		TY += 40.f * U;
		const FString Meta = FString::Printf(TEXT("%d MIN  -  %d OBJECTIVES"), FMath::Max(1, FMath::RoundToInt(M.DurationSeconds / 60.f)), M.ObjectiveCount)
			+ (M.BossName.IsEmpty() ? FString() : FString::Printf(TEXT("  -  BOSS: %s"), *M.BossName.ToString().ToUpper()));
		Txt(Meta, LabelFont(true), FitPx(Meta, LabelFont(true), 12.f, TW), TX, TY, SQUI::Muted);
		TY += 24.f * U;
		const int32 DescLines = TxtWrap(M.Description.ToString(), LabelFont(), 15.f, TX, TY, TW, SQUI::BoneWhite, 20.f * U, 4);
		TY += DescLines * 20.f * U + 10.f * U;
		if (!M.RewardText.IsEmpty())
		{
			TxtWrap(M.RewardText.ToString(), LabelFont(true), 13.f, TX, TY, TW, SQUI::Amber, 17.f * U, 2);
		}
		// footer: best time, selected tag
		const float FootY = Y + H - 40.f * U;
		Txt(M.BestSeconds > 0.f ? FString::Printf(TEXT("BEST  %d:%02d"), FMath::FloorToInt(M.BestSeconds) / 60, FMath::FloorToInt(M.BestSeconds) % 60) : FString(TEXT("NO CLEAR YET")),
			LabelFont(true), 13.f, TX, FootY + 6.f * U, M.BestSeconds > 0.f ? SQUI::Soul : SQUI::Disabled);
		if (bSelected)
		{
			const FString Tag = bPendingHard ? FString(TEXT("SELECTED  -  HARD")) : FString(TEXT("SELECTED"));
			const float TagW = TxtWidth(Tag, LabelFont(true), 12.f) + 22.f * U;
			Round(bPendingHard ? SQUI::Danger : SQUI::Soul, X + W - 20.f * U - TagW, FootY, TagW, 26.f * U, 13.f * U);
			Txt(Tag, LabelFont(true), 12.f, X + W - 20.f * U - TagW * 0.5f, FootY + 5.f * U, SQUI::Midnight, 1);
		}

		// locked
		if (!M.bUnlocked)
		{
			Round(SQUI::WithAlpha(SQUI::Midnight, 0.62f), X, Y, W, H, R);
			Padlock(X + W * 0.5f, Y + H * 0.42f, 44.f * U, SQUI::BoneWhite);
			TxtWrap(M.UnlockText.IsEmpty() ? FString(TEXT("Locked")) : M.UnlockText.ToString(), LabelFont(true), 15.f, X + 24.f * U, Y + H * 0.42f + 40.f * U, W - 48.f * U,
				SQUI::BoneWhite, 20.f * U, 3);
		}

		// click: select (a second click on the selected card starts the mission)
		if (bOver && bClick)
		{
			bClick = false;
			if (!M.bUnlocked)
			{
				ShowToast(M.UnlockText.IsEmpty() ? FString(TEXT("That mission is locked.")) : M.UnlockText.ToString(), SQUI::Danger);
				PlayUI(TEXT("Hit"), 0.3f, 1.f);
			}
			else if (bSelected && bPendingHard == (bMissionHard && M.bHardUnlocked))
			{
				StartTravel(false);
			}
			else
			{
				USQMissions::SetPendingMission(this, M.Id, bMissionHard && M.bHardUnlocked);
				PlayUI(TEXT("Bank"), 0.4f, 1.f);
			}
		}
	}
	DrawAlpha = 1.f;

	// ---- actions under the cards: HARD MODE switch, SELECT / START
	const float ActY = Bottom - ActionH;
	const float BtnW = 320.f * U;
	int32 NoFocus = -1;
	const FString HardLabel = !Focus.bHardUnlocked ? FString(TEXT("HARD MODE  -  LOCKED")) : (bMissionHard ? FString(TEXT("HARD MODE: ON")) : FString(TEXT("HARD MODE: OFF")));
	if ((DrawPageButton(HardLabel, CW * 0.5f - BtnW - 12.f * U, ActY, BtnW, ActionH, bMissionHard ? 3 : 0, Focus.bHardUnlocked && Focus.bUnlocked, -1, NoFocus) || bToggleHard))
	{
		if (Focus.bHardUnlocked && Focus.bUnlocked)
		{
			bMissionHard = !bMissionHard;
			if (Focus.Id == Pending)
			{
				USQMissions::SetPendingMission(this, Focus.Id, bMissionHard);
			}
			PlayUI(TEXT("Soul"), 0.3f, bMissionHard ? 0.8f : 1.2f);
		}
		else
		{
			ShowToast(TEXT("Beat this mission once to unlock Hard mode."), SQUI::Danger);
		}
	}
	const bool bFocusSelected = Focus.Id == Pending && bPendingHard == (bMissionHard && Focus.bHardUnlocked);
	const FString GoLabel = bFocusSelected ? FString(TEXT("START REAPING")) : FString(TEXT("SELECT MISSION"));
	const bool bGo = DrawPageButton(GoLabel, CW * 0.5f + 12.f * U, ActY, BtnW, ActionH, 1, Focus.bUnlocked, -1, NoFocus);
	if (bGo || bConfirm)
	{
		bConfirm = false;
		if (!Focus.bUnlocked)
		{
			ShowToast(Focus.UnlockText.IsEmpty() ? FString(TEXT("That mission is locked.")) : Focus.UnlockText.ToString(), SQUI::Danger);
		}
		else if (bFocusSelected)
		{
			StartTravel(false);
		}
		else
		{
			USQMissions::SetPendingMission(this, Focus.Id, bMissionHard && Focus.bHardUnlocked);
			PlayUI(TEXT("Bank"), 0.4f, 1.f);
		}
	}
	if (bMissionHard && Focus.bHardUnlocked)
	{
		Txt(TEXT("Hard: tougher enemies and boss, more gold."), LabelFont(true), 13.f, CW * 0.5f - BtnW - 12.f * U, ActY - 22.f * U, SQUI::Danger);
	}
}

// ================================================================ UPGRADES (Nov 9): the permanent meta shop

void ASQMenuHUD::DrawUpgradesPage()
{
	if (DrawPageFrame(TEXT("UPGRADES"), 0.84f))
	{
		OpenPage(ESQMenuPage::Main);
		return;
	}
	const float U = UnitPx;
	const float CW = Canvas->ClipX;
	const float X0 = FMath::Max(72.f * U, CW * 0.04f);
	const USQData* D = USQData::Get(this);
	USQSaveGame* Save = GetSave();
	TArray<FSQMetaUpgradeView> Lines;
	USQMissions::GetMetaUpgradeList(D, Save, Lines);
	if (Lines.Num() == 0 || !Save)
	{
		Txt(TEXT("No upgrades found (Content/Data/MetaUpgrades.csv)."), LabelFont(true), 20.f, X0, 200.f * U, SQUI::Danger);
		return;
	}
	const int32 Count = Lines.Num();
	if (UpgradeLift.Num() != Count)
	{
		UpgradeLift.SetNumZeroed(Count);
	}
	const float Elapsed = TimeNow - PageChangeTime;
	const float Top = 170.f * U, Bottom = Canvas->ClipY - 52.f * U - 24.f * U;
	const float RW = FMath::Min(CW - 2.f * X0, 1500.f * U);
	const float RX = (CW - RW) * 0.5f;

	// header: banked gold
	DrawAlpha = MenuAppear(Elapsed, 0.05f, 0.3f);
	{
		const FString Gold = FString::Printf(TEXT("%d BANKED GOLD"), Save->OvertimePay);
		const float GW = TxtWidth(Gold, HeadFont(), 26.f) + 60.f * U;
		Round(SQUI::WithAlpha(SQUI::Amber, 0.16f), RX, Top - 4.f * U, GW, 44.f * U, 22.f * U);
		Round(SQUI::Hex(0xB8, 0x7A, 0x1E), RX + 12.f * U, Top + 7.f * U, 22.f * U, 22.f * U, 11.f * U);
		Round(SQUI::Amber, RX + 15.f * U, Top + 10.f * U, 16.f * U, 16.f * U, 8.f * U);
		Txt(Gold, HeadFont(), 26.f, RX + 44.f * U, Top + 2.f * U, SQUI::Amber);
		Txt(TEXT("Every mission banks part of the gold you earn. Upgrades last forever and apply to every reaper."), LabelFont(), 15.f, RX + RW, Top + 10.f * U, SQUI::Muted, 2);
	}
	DrawAlpha = 1.f;

	// keys
	constexpr int32 Cols = 2;
	if (bNavDown)
	{
		PageFocus = FMath::Min(Count - 1, PageFocus + Cols);
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (bNavUp)
	{
		PageFocus = FMath::Max(0, PageFocus - Cols);
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (bNavRight)
	{
		PageFocus = FMath::Min(Count - 1, PageFocus + 1);
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	if (bNavLeft)
	{
		PageFocus = FMath::Max(0, PageFocus - 1);
		PlayUI(TEXT("Soul"), 0.14f, 1.5f);
	}
	PageFocus = FMath::Clamp(PageFocus, 0, Count - 1);

	const float AreaTop = Top + 64.f * U, Gap = 14.f * U;
	const float RowH = 118.f * U;
	const float ColW = (RW - Gap) / Cols;
	const int32 TotalRows = (Count + Cols - 1) / Cols;
	const int32 VisibleRows = FMath::Max(1, FMath::FloorToInt((Bottom - AreaTop + Gap) / (RowH + Gap)));
	const int32 MaxRow = FMath::Max(0, TotalRows - VisibleRows);
	if (bWheelDown)
	{
		UpgradeRow = FMath::Min(MaxRow, UpgradeRow + 1);
	}
	if (bWheelUp)
	{
		UpgradeRow = FMath::Max(0, UpgradeRow - 1);
	}
	const int32 FocusRow = PageFocus / Cols;
	if (FocusRow < UpgradeRow)
	{
		UpgradeRow = FocusRow;
	}
	else if (FocusRow >= UpgradeRow + VisibleRows && (bNavDown || bNavRight))
	{
		UpgradeRow = FocusRow - VisibleRows + 1;
	}
	UpgradeRow = FMath::Clamp(UpgradeRow, 0, MaxRow);

	int32 BuyIdx = -1;
	if (bConfirm)
	{
		bConfirm = false;
		BuyIdx = PageFocus;
	}
	for (int32 i = UpgradeRow * Cols; i < FMath::Min(Count, (UpgradeRow + VisibleRows) * Cols); ++i)
	{
		const FSQMetaUpgradeView& L = Lines[i];
		const int32 Slot = i - UpgradeRow * Cols;
		const float X = RX + (Slot % Cols) * (ColW + Gap);
		const float Y = AreaTop + (Slot / Cols) * (RowH + Gap);
		const float In = MenuAppear(Elapsed, 0.1f + 0.03f * Slot, 0.35f);
		if (In <= 0.01f)
		{
			continue;
		}
		const bool bMaxed = L.NextCost < 0 || L.Rank >= L.MaxRank;
		const bool bOver = MouseIn(X, Y, ColW, RowH);
		if (bOver && bMouseMoved && PageFocus != i)
		{
			PageFocus = i;
			PlayUI(TEXT("Soul"), 0.12f, 1.5f);
		}
		const bool bFocus = PageFocus == i;
		float& Lift = UpgradeLift[i];
		Lift = FMath::FInterpTo(Lift, bFocus ? 1.f : 0.f, DeltaT, 12.f);
		const float Flash = UpgradeFlash == i ? FMath::Clamp(1.f - (TimeNow - UpgradeFlashTime) / 0.5f, 0.f, 1.f) : 0.f;
		DrawAlpha = In;
		const float LX = X - 30.f * U * (1.f - In) + 6.f * U * Lift;
		const float R = 16.f * U;
		const FLinearColor Edge = bMaxed ? SQUI::Soul : (L.bCanAfford ? SQUI::Amber : SQUI::Line);
		GlowAround(Edge, LX, Y, ColW, RowH, R, 0.4f * Lift + Flash);
		Round(FLinearColor(0.f, 0.f, 0.f, 0.35f), LX + 4.f * U, Y + 7.f * U, ColW, RowH, R);
		Round(bFocus ? Edge : SQUI::Line, LX - 2.f * U, Y - 2.f * U, ColW + 4.f * U, RowH + 4.f * U, R + 2.f * U);
		Round(FMath::Lerp(SQUI::Panel, SQUI::PanelHover, Lift), LX, Y, ColW, RowH, R);
		if (Flash > 0.f)
		{
			Round(SQUI::WithAlpha(SQUI::Amber, 0.3f * Flash), LX, Y, ColW, RowH, R);
		}

		// buy button on the right
		const float BtnW = 170.f * U, BtnH = 52.f * U;
		const float BtnX = LX + ColW - BtnW - 18.f * U, BtnY = Y + (RowH - BtnH) * 0.5f;
		const bool bOverBtn = MouseIn(BtnX, BtnY, BtnW, BtnH);
		if (bMaxed)
		{
			Round(SQUI::WithAlpha(SQUI::Soul, 0.85f), BtnX, BtnY, BtnW, BtnH, 14.f * U);
			Round(SQUI::Panel, BtnX + 3.f * U, BtnY + 3.f * U, BtnW - 6.f * U, BtnH - 6.f * U, 11.f * U);
			Txt(TEXT("MAXED"), HeadFont(), 22.f, BtnX + BtnW * 0.5f, BtnY + BtnH * 0.5f - 0.62f * 22.f * U, SQUI::Soul, 1);
		}
		else
		{
			const FLinearColor BtnCol = L.bCanAfford ? SQUI::Amber : SQUI::WithAlpha(SQUI::Line, 0.9f);
			Round(bOverBtn && L.bCanAfford ? FMath::Lerp(BtnCol, MenuWhite, 0.2f) : BtnCol, BtnX, BtnY, BtnW, BtnH, 14.f * U);
			if (L.bCanAfford)
			{
				Round(FLinearColor(1.f, 1.f, 1.f, 0.14f), BtnX + 5.f * U, BtnY + 4.f * U, BtnW - 10.f * U, BtnH * 0.36f, 10.f * U);
			}
			Txt(TEXT("BUY"), LabelFont(true), 12.f, BtnX + BtnW * 0.5f, BtnY + 6.f * U, L.bCanAfford ? SQUI::Midnight : SQUI::Muted, 1);
			Txt(FString::Printf(TEXT("%d GOLD"), L.NextCost), HeadFont(), 20.f, BtnX + BtnW * 0.5f, BtnY + 22.f * U, L.bCanAfford ? SQUI::Midnight : SQUI::Disabled, 1);
			if (bOverBtn && bClick)
			{
				bClick = false;
				BuyIdx = i;
			}
		}

		// name, rank pips, description, now -> next
		const float TX = LX + 22.f * U, TW = BtnX - 20.f * U - TX;
		const FString Name = L.Name.ToString().ToUpper();
		const float NameW = Txt(Name, HeadFont(), FitPx(Name, HeadFont(), 24.f, TW * 0.6f), TX, Y + 12.f * U, SQUI::BoneWhite);
		const int32 MaxRank = FMath::Clamp(L.MaxRank, 1, 20);
		const float PipW = FMath::Min(18.f * U, (TW - NameW - 20.f * U) / MaxRank - 4.f * U);
		for (int32 Pip = 0; Pip < MaxRank && PipW > 2.f; ++Pip)
		{
			Round(Pip < L.Rank ? (bMaxed ? SQUI::Soul : SQUI::Amber) : SQUI::Line, TX + NameW + 16.f * U + Pip * (PipW + 4.f * U), Y + 22.f * U, PipW, 8.f * U, 3.f * U);
		}
		TxtWrap(L.Description.ToString(), LabelFont(), 14.f, TX, Y + 48.f * U, TW, SQUI::Muted, 18.f * U, 2);
		const FString NowText = L.CurrentText.IsEmpty() ? FString(TEXT("-")) : L.CurrentText.ToString();
		const FString Change = bMaxed ? FString::Printf(TEXT("RANK %d/%d  -  %s"), L.Rank, L.MaxRank, *NowText)
			: FString::Printf(TEXT("RANK %d/%d  -  %s  >  %s"), L.Rank, L.MaxRank, *NowText, *L.NextText.ToString());
		Txt(Change, LabelFont(true), FitPx(Change, LabelFont(true), 13.f, TW), TX, Y + RowH - 28.f * U, bMaxed ? SQUI::Soul : SQUI::Amber);
		DrawAlpha = 1.f;
	}

	// scroll hint
	if (MaxRow > 0)
	{
		Txt(FString::Printf(TEXT("ROWS %d-%d OF %d  -  WHEEL TO SCROLL"), UpgradeRow + 1, FMath::Min(TotalRows, UpgradeRow + VisibleRows), TotalRows), LabelFont(true), 12.f,
			RX + RW, Bottom + 4.f * U, SQUI::Disabled, 2);
	}

	// purchase
	if (Lines.IsValidIndex(BuyIdx))
	{
		const FSQMetaUpgradeView& L = Lines[BuyIdx];
		if (L.NextCost < 0)
		{
			ShowToast(TEXT("Already maxed."), SQUI::Soul);
		}
		else if (USQMissions::BuyMetaUpgrade(Save, D, L.Id))
		{
			UpgradeFlash = BuyIdx;
			UpgradeFlashTime = TimeNow;
			PlayUI(TEXT("Bank"), 0.7f, 1.f);
			ShowToast(FString::Printf(TEXT("%s upgraded to rank %d"), *L.Name.ToString(), L.Rank + 1), SQUI::Amber);
		}
		else
		{
			PlayUI(TEXT("Hit"), 0.3f, 1.f);
			ShowToast(FString::Printf(TEXT("Needs %d banked gold (you have %d)."), L.NextCost, Save->OvertimePay), SQUI::Danger);
		}
	}
}
