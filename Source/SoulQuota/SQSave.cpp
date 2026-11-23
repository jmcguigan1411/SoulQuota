// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQSave.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "SQTutorial.h"
#include "SQUIStyle.h"

namespace
{
	const TCHAR* SlotName = TEXT("SoulQuotaProfile");
	/** The one shared profile object (rooted for the life of the process; see USQSaveGame::Load) */
	TWeakObjectPtr<USQSaveGame> SQSaveSharedProfile;

	USQSaveGame* SQSaveLoadFromDisk()
	{
		if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
		{
			if (USQSaveGame* FromDisk = Cast<USQSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
			{
				return FromDisk;
			}
		}
		return Cast<USQSaveGame>(UGameplayStatics::CreateSaveGameObject(USQSaveGame::StaticClass()));
	}
}

USQSaveGame* USQSaveGame::Load()
{
	if (USQSaveGame* Shared = SQSaveSharedProfile.Get())
	{
		return Shared;
	}
	USQSaveGame* Existing = SQSaveLoadFromDisk();
	if (Existing)
	{
		{
			// Sanitise a damaged / hand-edited profile
			Existing->OvertimePay = FMath::Max(0, Existing->OvertimePay);
			Existing->LifetimePay = FMath::Max(0, Existing->LifetimePay);
			Existing->BestEndlessNight = FMath::Max(0, Existing->BestEndlessNight);
			Existing->RunsPlayed = FMath::Max(0, Existing->RunsPlayed);
			Existing->RunsWon = FMath::Clamp(Existing->RunsWon, 0, Existing->RunsPlayed);
			for (TPair<FName, int32>& Total : Existing->QuestTotals) Total.Value = FMath::Max(0, Total.Value);
			for (TPair<FName, int32>& Best : Existing->QuestBests) Best.Value = FMath::Max(0, Best.Value);
			for (TPair<FName, int32>& Rank : Existing->MetaRanks) Rank.Value = FMath::Clamp(Rank.Value, 0, 100);
			for (TPair<FName, float>& BestTime : Existing->MissionBestSeconds) BestTime.Value = FMath::Max(0.f, BestTime.Value);
			Existing->SanitiseSettings();
		}
		Existing->AddToRoot(); // shared for the whole process (menu, run, every PIE world)
		SQSaveSharedProfile = Existing;
	}
	return Existing;
}

void USQSaveGame::Store(USQSaveGame* Save)
{
	if (Save) UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}

void USQSaveGame::SanitiseSettings()
{
	CameraMode = CameraMode > 1 ? 0 : CameraMode;
	if (SettingsVersion < SQSettings::Version)
	{
		// profile from the overhead camera: its zoom / tilt mean something else now
		CameraZoom = SQSettings::ZoomDefault;
		CameraTilt = SQSettings::TiltDefault;
		SettingsVersion = SQSettings::Version;
	}
	AimMode = AimMode > 1 ? 0 : AimMode;
	MouseSensitivity = FMath::IsFinite(MouseSensitivity) ? FMath::Clamp(MouseSensitivity, SQSettings::SensMin, SQSettings::SensMax) : SQSettings::SensDefault;
	CameraZoom = FMath::IsFinite(CameraZoom) ? FMath::Clamp(CameraZoom, SQSettings::ZoomMin, SQSettings::ZoomMax) : SQSettings::ZoomDefault;
	CameraTilt = FMath::IsFinite(CameraTilt) ? FMath::Clamp(CameraTilt, SQSettings::TiltMin, SQSettings::TiltMax) : SQSettings::TiltDefault;
	CameraSmoothing = FMath::IsFinite(CameraSmoothing) ? FMath::Clamp(CameraSmoothing, 0.f, 1.f) : 0.7f;
	ScreenShake = FMath::IsFinite(ScreenShake) ? FMath::Clamp(ScreenShake, 0.f, 1.f) : 0.5f;
	MasterVolume = FMath::IsFinite(MasterVolume) ? FMath::Clamp(MasterVolume, 0.f, 1.f) : 0.8f;
	MusicVolume = FMath::IsFinite(MusicVolume) ? FMath::Clamp(MusicVolume, 0.f, 1.f) : 0.45f;
}

// ============================================================ settings: ranges and live apply

void SQSettings::ResetDefaults(USQSaveGame* Save)
{
	if (!Save) return;
	Save->CameraMode = 0;
	Save->CameraZoom = ZoomDefault;
	Save->CameraTilt = TiltDefault;
	Save->CameraSmoothing = 0.7f;
	Save->bCameraLean = true;
	Save->AimMode = 0;
	Save->bInvertLookY = false;
	Save->ScreenShake = 0.5f;
	Save->bDamageNumbers = true;
	Save->bTutorialTips = true;
	Save->bAutoPickLevelUps = false;
	Save->bShowKeyHints = true;
	Save->MasterVolume = 0.8f;
	Save->MusicVolume = 0.45f;
	Save->MouseSensitivity = SensDefault;
}

void SQSettings::ApplyMasterVolume(UWorld* World, const USQSaveGame* Save)
{
	if (!World) return;
	const float MasterVol = Save ? FMath::Clamp(Save->MasterVolume, 0.f, 1.f) : 0.8f;
	FAudioDeviceHandle Device = World->GetAudioDevice();
	if (Device.IsValid())
	{
		Device->SetTransientPrimaryVolume(MasterVol);
	}
}

bool SQSettings::IsFullscreen()
{
	const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return !GUS || GUS->GetFullscreenMode() != EWindowMode::Windowed;
}

void SQSettings::ApplyDisplayMode(const UWorld* World, bool bFullscreen)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS) return;
	const FIntPoint Desk = GUS->GetDesktopResolution();
	if (bFullscreen)
	{
		GUS->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		GUS->SetScreenResolution(Desk);
	}
	else
	{
		// a 16:9 window, 80% of the desktop wide at most
		const int32 WinW = FMath::Max(960, FMath::Min(1600, Desk.X * 4 / 5));
		GUS->SetFullscreenMode(EWindowMode::Windowed);
		GUS->SetScreenResolution(FIntPoint(WinW, WinW * 9 / 16));
	}
	if (!World || World->WorldType != EWorldType::PIE)
	{
		GUS->ApplyResolutionSettings(false);
	}
	GUS->SaveSettings();
}

float SQSettings::FollowLagSpeed(const USQSaveGame* Save)
{
	// 0 = tight (24) .. 1 = floaty (3); the default 0.7 is ~9, the old CameraFollowSpeed
	const float Smooth = Save ? FMath::Clamp(Save->CameraSmoothing, 0.f, 1.f) : 0.7f;
	return FMath::Lerp(24.f, 3.f, Smooth);
}

float SQSettings::ShakeScale(const USQSaveGame* Save)
{
	return Save ? FMath::Clamp(Save->ScreenShake, 0.f, 1.f) * 2.f : 1.f;
}

// ============================================================ the SETTINGS panel

namespace
{
	enum class ESQSetRow : uint8 { CamMode, Zoom, Tilt, Smooth, Lean, Shake, DmgNumbers, Tips, AutoPick, Display, Replay, Master, Music, KeyHints };
	enum class ESQSetKind : uint8 { Segment, Slider, Toggle, Button };

	struct FSQSetRowDef
	{
		ESQSetRow Id;
		ESQSetKind Kind;
		const TCHAR* Label;
	};

	const FSQSetRowDef SQSetCamera[] = {
		{ ESQSetRow::CamMode, ESQSetKind::Segment, TEXT("Aiming") },
		{ ESQSetRow::Zoom, ESQSetKind::Slider, TEXT("Camera distance") },
		{ ESQSetRow::Tilt, ESQSetKind::Slider, TEXT("Mouse sensitivity") },
		{ ESQSetRow::Smooth, ESQSetKind::Slider, TEXT("Follow smoothing") },
		{ ESQSetRow::Lean, ESQSetKind::Toggle, TEXT("Invert look up / down") },
		{ ESQSetRow::Shake, ESQSetKind::Slider, TEXT("Screen shake") },
	};
	const FSQSetRowDef SQSetGameplay[] = {
		{ ESQSetRow::DmgNumbers, ESQSetKind::Toggle, TEXT("Damage numbers") },
		{ ESQSetRow::Tips, ESQSetKind::Toggle, TEXT("Tutorial tips") },
		{ ESQSetRow::AutoPick, ESQSetKind::Toggle, TEXT("Auto-pick level-ups") },
		{ ESQSetRow::Display, ESQSetKind::Segment, TEXT("Display mode") },
		{ ESQSetRow::Replay, ESQSetKind::Button, TEXT("How-to-play prompts") },
	};
	const FSQSetRowDef SQSetAudio[] = {
		{ ESQSetRow::Master, ESQSetKind::Slider, TEXT("Master volume") },
		{ ESQSetRow::Music, ESQSetKind::Slider, TEXT("Music volume") },
	};
	const FSQSetRowDef SQSetControls[] = {
		{ ESQSetRow::KeyHints, ESQSetKind::Toggle, TEXT("Show key hints") },
	};

	struct FSQSetBind
	{
		const TCHAR* Key;
		const TCHAR* Action;
	};
	// CONTROLS tab (display only; checked against ASQPlayerController::SetupInputComponent)
	const FSQSetBind SQSetBinds[] = {
		{ TEXT("WASD"), TEXT("Move (camera-relative)") },
		{ TEXT("MOUSE"), TEXT("Look around") },
		{ TEXT("SPACE"), TEXT("Jump (hold to hop)") },
		{ TEXT("SHIFT / R-CLICK"), TEXT("Dash through crowds") },
		{ TEXT("CTRL / C"), TEXT("Slide") },
		{ TEXT("L-CLICK"), TEXT("Reap (execute ripe foes)") },
		{ TEXT("E"), TEXT("Ability (level 3)") },
		{ TEXT("R"), TEXT("Ultimate (level 6, full souls)") },
		{ TEXT("F"), TEXT("Interact / chests / bell") },
		{ TEXT("M"), TEXT("Full map") },
		{ TEXT("WHEEL / HOME"), TEXT("Camera distance / reset") },
		{ TEXT("HOLD ALT"), TEXT("Free the cursor") },
		{ TEXT("1 2 3  Q / X"), TEXT("Pick a card / reroll / skip") },
		{ TEXT("P"), TEXT("Settings") },
	};

	constexpr float SQSetPad = 28.f;
	constexpr float SQSetRowH = 56.f;
	constexpr float SQSetBodyH = 6.f * SQSetRowH;

	FLinearColor SQSetA(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * Alpha);
	}

	bool SQSetIn(const FSQTutorialPainter& P, float X, float Y, float W, float H)
	{
		return P.bHasMouse && P.MouseX >= X && P.MouseX <= X + W && P.MouseY >= Y && P.MouseY <= Y + H;
	}

	/** Slider value 0..1 of a row, and its label */
	float SQSetGet01(const USQSaveGame* Save, ESQSetRow Id)
	{
		switch (Id)
		{
		case ESQSetRow::Zoom: return (Save->CameraZoom - SQSettings::ZoomMin) / (SQSettings::ZoomMax - SQSettings::ZoomMin);
		case ESQSetRow::Tilt: return (Save->MouseSensitivity - SQSettings::SensMin) / (SQSettings::SensMax - SQSettings::SensMin);
		case ESQSetRow::Smooth: return Save->CameraSmoothing;
		case ESQSetRow::Shake: return Save->ScreenShake;
		case ESQSetRow::Master: return Save->MasterVolume;
		case ESQSetRow::Music: return Save->MusicVolume;
		default: return 0.f;
		}
	}

	void SQSetSet01(USQSaveGame* Save, ESQSetRow Id, float Value01)
	{
		const float V = FMath::Clamp(Value01, 0.f, 1.f);
		switch (Id)
		{
		case ESQSetRow::Zoom: Save->CameraZoom = FMath::Lerp(SQSettings::ZoomMin, SQSettings::ZoomMax, V); break;
		case ESQSetRow::Tilt: Save->MouseSensitivity = FMath::Lerp(SQSettings::SensMin, SQSettings::SensMax, V); break;
		case ESQSetRow::Smooth: Save->CameraSmoothing = V; break;
		case ESQSetRow::Shake: Save->ScreenShake = V; break;
		case ESQSetRow::Master: Save->MasterVolume = V; break;
		case ESQSetRow::Music: Save->MusicVolume = V; break;
		default: break;
		}
	}

	FString SQSetValueText(const USQSaveGame* Save, ESQSetRow Id)
	{
		return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(SQSetGet01(Save, Id) * 100.f));
	}

	bool* SQSetBool(USQSaveGame* Save, ESQSetRow Id)
	{
		switch (Id)
		{
		case ESQSetRow::Lean: return &Save->bInvertLookY;
		case ESQSetRow::DmgNumbers: return &Save->bDamageNumbers;
		case ESQSetRow::Tips: return &Save->bTutorialTips;
		case ESQSetRow::AutoPick: return &Save->bAutoPickLevelUps;
		case ESQSetRow::KeyHints: return &Save->bShowKeyHints;
		default: return nullptr;
		}
	}
}

float FSQSettingsPanel::PanelH()
{
	// pad, header, gap, tabs, gap, rows, gap, two note lines, gap, buttons, pad
	return 24.f + 40.f + 14.f + 42.f + 10.f + SQSetBodyH + 14.f + 40.f + 14.f + 44.f + 24.f;
}

FSQSettingsResult FSQSettingsPanel::Draw(const FSQTutorialPainter& P, UWorld* World, float X, float Y, const FString& HeaderKey, const FString& HeaderHint,
	const FString& NoteLine1, const FString& NoteLine2, bool bInRun)
{
	FSQSettingsResult Result;
	USQSaveGame* Save = USQSaveGame::Load();
	if (!Save || !P.Round || !P.Text || !P.TextWidth) return Result;
	if (OpenedAt < 0.f) OpenedAt = P.Now;
	const float Appear = FMath::Clamp((P.Now - OpenedAt) / 0.2f, 0.f, 1.f);
	const float A = 1.f - (1.f - Appear) * (1.f - Appear);
	const float W = PanelW, H = PanelH();
	Y += 16.f * (1.f - A);
	bool bClick = P.bClick && A > 0.6f;
	auto EatClick = [&bClick, &Result]() { bClick = false; Result.bAteClick = true; };
	auto Tick = [&P]() { if (P.Sound) P.Sound(0); };

	// ---- card: preview Camera settings card (bar fill, soul rim, 14 px corners)
	for (int32 Ring = 3; Ring >= 1; --Ring)
	{
		const float Grow = Ring * 5.f;
		P.Round(SQSetA(FLinearColor(0.f, 0.f, 0.f, 0.14f), A), X - Grow + 2.f, Y - Grow + 8.f, W + 2.f * Grow, H + 2.f * Grow, 14.f + Grow);
	}
	P.Round(SQSetA(SQUI::Soul, A), X - 1.5f, Y - 1.5f, W + 3.f, H + 3.f, 15.5f);
	P.Round(SQSetA(SQUI::WithAlpha(SQUI::Bar, 0.98f), A), X, Y, W, H, 14.f);

	// ---- header: SETTINGS, [P] open / close
	float CurY = Y + 24.f;
	P.Text(TEXT("SETTINGS"), ESQTutFont::Head, 30.f, X + SQSetPad, CurY + 2.f, SQSetA(SQUI::BoneWhite, A), 0);
	{
		const float HintW = P.TextWidth(HeaderHint, ESQTutFont::Label, 15.f);
		const float HintX = X + W - SQSetPad - HintW;
		P.Text(HeaderHint, ESQTutFont::Label, 15.f, HintX, CurY + 11.f, SQSetA(SQUI::Muted, A), 0);
		if (!HeaderKey.IsEmpty())
		{
			const float KeyW = USQTutorial::SoulKeyWidth(P, HeaderKey, 32.f);
			USQTutorial::DrawSoulKey(P, HeaderKey, HintX - 10.f - KeyW, CurY + 2.f, 32.f, A);
		}
	}
	CurY += 40.f + 14.f;

	// ---- tabs (segmented, full width)
	const FSQSetRowDef* Rows = SQSetCamera;
	int32 NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetCamera));
	auto PickRows = [&Rows, &NumRows](int32 InTab)
	{
		switch (InTab)
		{
		case 1: Rows = SQSetGameplay; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetGameplay)); break;
		case 2: Rows = SQSetAudio; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetAudio)); break;
		case 3: Rows = SQSetControls; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetControls)); break;
		default: Rows = SQSetCamera; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetCamera)); break;
		}
	};
	Tab = FMath::Clamp(Tab, 0, 3);
	// keyboard: Tab = next tab; Up / Down rows (-1 = the tab strip); Left / Right on the strip = tabs
	if (P.bTabNext)
	{
		Tab = (Tab + 1) % 4;
		Focus = FMath::Min(Focus, 0);
		Drag = -1;
		Tick();
	}
	PickRows(Tab);
	if (P.bNavDown && Focus < NumRows - 1) { Focus++; Tick(); }
	if (P.bNavUp && Focus > -1) { Focus--; Tick(); }
	Focus = FMath::Clamp(Focus, -1, NumRows - 1);
	if (Focus == -1 && (P.bNavLeft || P.bNavRight))
	{
		Tab = (Tab + (P.bNavRight ? 1 : 3)) % 4;
		Drag = -1;
		Tick();
		PickRows(Tab);
	}
	{
		static const TCHAR* TabNames[4] = { TEXT("CAMERA"), TEXT("GAMEPLAY"), TEXT("AUDIO"), TEXT("CONTROLS") };
		const float TabsX = X + SQSetPad, TabsW = W - 2.f * SQSetPad, TabsH = 42.f;
		P.Round(SQSetA(SQUI::PanelDeep, A), TabsX, CurY, TabsW, TabsH, 10.f);
		if (Focus == -1) P.Round(SQSetA(SQUI::WithAlpha(SQUI::Soul, 0.35f), A), TabsX - 2.f, CurY - 2.f, TabsW + 4.f, TabsH + 4.f, 12.f);
		if (Focus == -1) P.Round(SQSetA(SQUI::PanelDeep, A), TabsX, CurY, TabsW, TabsH, 10.f);
		const float SegW = (TabsW - 8.f) / 4.f;
		for (int32 TabIdx = 0; TabIdx < 4; ++TabIdx)
		{
			const float SegX = TabsX + 4.f + TabIdx * SegW;
			const bool bSel = TabIdx == Tab;
			const bool bOver = SQSetIn(P, SegX, CurY + 4.f, SegW, TabsH - 8.f);
			TabLift[TabIdx] = FMath::FInterpTo(TabLift[TabIdx], bOver ? 1.f : 0.f, P.Delta, 14.f);
			if (bSel) P.Round(SQSetA(SQUI::Soul, A), SegX + 1.f, CurY + 4.f, SegW - 2.f, TabsH - 8.f, 7.f);
			else if (TabLift[TabIdx] > 0.02f) P.Round(SQSetA(SQUI::WithAlpha(SQUI::PanelHover, TabLift[TabIdx]), A), SegX + 1.f, CurY + 4.f, SegW - 2.f, TabsH - 8.f, 7.f);
			P.Text(TabNames[TabIdx], ESQTutFont::LabelBold, 14.f, SegX + SegW * 0.5f, CurY + TabsH * 0.5f - 0.62f * 14.f,
				SQSetA(bSel ? SQUI::Midnight : FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, TabLift[TabIdx]), A), 1);
			if (bOver && bClick && !bSel)
			{
				EatClick();
				Tab = TabIdx;
				Focus = 0;
				Drag = -1;
				Tick();
				PickRows(Tab);
			}
		}
		CurY += TabsH + 10.f;
	}

	// ---- rows
	if (!P.bMouseHeld && Drag >= 0)
	{
		Drag = -1;
		USQSaveGame::Store(Save); // slider released: write it once
	}
	const float RightX = X + W - SQSetPad;
	for (int32 RowIdx = 0; RowIdx < NumRows; ++RowIdx)
	{
		const FSQSetRowDef& Row = Rows[RowIdx];
		const float RowY = CurY + RowIdx * SQSetRowH;
		const float MidY = RowY + SQSetRowH * 0.5f;
		const bool bFocus = Focus == RowIdx;
		const bool bOverRow = SQSetIn(P, X + 12.f, RowY + 2.f, W - 24.f, SQSetRowH - 4.f);
		if (bFocus || bOverRow)
		{
			P.Round(SQSetA(SQUI::WithAlpha(SQUI::PanelHover, bFocus ? 0.75f : 0.4f), A), X + 12.f, RowY + 3.f, W - 24.f, SQSetRowH - 6.f, 10.f);
		}
		if (bFocus) P.Round(SQSetA(SQUI::Soul, A), X + 14.f, RowY + 16.f, 3.f, SQSetRowH - 32.f, 1.5f);
		P.Round(SQSetA(SQUI::Line, A), X + SQSetPad, RowY + SQSetRowH - 1.f, W - 2.f * SQSetPad, 1.f, 0.f);
		P.Text(Row.Label, ESQTutFont::Label, 19.f, X + SQSetPad, MidY - 0.62f * 19.f, SQSetA(SQUI::BoneWhite, A), 0);

		bool bChanged = false;
		switch (Row.Kind)
		{
		case ESQSetKind::Segment:
		{
			// AUTO | MANUAL (aiming)  or  WINDOWED | FULLSCREEN
			const bool bCam = Row.Id == ESQSetRow::CamMode;
			const TCHAR* Opts[2] = { bCam ? TEXT("AUTO") : TEXT("WINDOWED"), bCam ? TEXT("MANUAL") : TEXT("FULLSCREEN") };
			const int32 Sel = bCam ? (Save->AimMode == 1 ? 1 : 0) : (SQSettings::IsFullscreen() ? 1 : 0);
			float OptW[2];
			float GroupW = 6.f;
			for (int32 OptIdx = 0; OptIdx < 2; ++OptIdx)
			{
				OptW[OptIdx] = P.TextWidth(Opts[OptIdx], ESQTutFont::LabelBold, 13.f) + 26.f;
				GroupW += OptW[OptIdx] + (OptIdx == 0 ? 4.f : 0.f);
			}
			const float GroupH = 34.f, GroupX = RightX - GroupW, GroupY = MidY - GroupH * 0.5f;
			P.Round(SQSetA(SQUI::PanelDeep, A), GroupX, GroupY, GroupW, GroupH, 8.f);
			float OptX = GroupX + 3.f;
			int32 Want = Sel;
			for (int32 OptIdx = 0; OptIdx < 2; ++OptIdx)
			{
				const bool bOn = OptIdx == Sel;
				const bool bOverOpt = SQSetIn(P, OptX, GroupY + 3.f, OptW[OptIdx], GroupH - 6.f);
				if (bOn) P.Round(SQSetA(SQUI::Soul, A), OptX, GroupY + 3.f, OptW[OptIdx], GroupH - 6.f, 6.f);
				P.Text(Opts[OptIdx], ESQTutFont::LabelBold, 13.f, OptX + OptW[OptIdx] * 0.5f, MidY - 0.62f * 13.f,
					SQSetA(bOn ? SQUI::Midnight : (bOverOpt ? SQUI::BoneWhite : SQUI::Muted), A), 1);
				if (bOverOpt && bClick)
				{
					EatClick();
					Focus = RowIdx;
					Want = OptIdx;
				}
				OptX += OptW[OptIdx] + 4.f;
			}
			if (bFocus && (P.bNavLeft || P.bNavRight || P.bConfirm)) Want = 1 - Sel;
			if (Want != Sel)
			{
				if (bCam) Save->AimMode = static_cast<uint8>(Want);
				else SQSettings::ApplyDisplayMode(World, Want == 1);
				bChanged = true;
			}
			break;
		}
		case ESQSetKind::Slider:
		{
			const float ValueW = 64.f, TrackW = 200.f, TrackH = 7.f;
			const float TrackX = RightX - ValueW - 12.f - TrackW;
			float Frac = FMath::Clamp(SQSetGet01(Save, Row.Id), 0.f, 1.f);
			const float OldFrac = Frac;
			if (bClick && SQSetIn(P, TrackX - 12.f, MidY - 16.f, TrackW + 24.f, 32.f))
			{
				EatClick();
				Drag = RowIdx;
				Focus = RowIdx;
			}
			if (Drag == RowIdx && P.bMouseHeld && P.bHasMouse) Frac = FMath::Clamp((P.MouseX - TrackX) / TrackW, 0.f, 1.f);
			if (bFocus && (P.bNavLeft || P.bNavRight)) Frac = FMath::Clamp(Frac + (P.bNavRight ? 0.05f : -0.05f), 0.f, 1.f);
			if (!FMath::IsNearlyEqual(Frac, OldFrac, 0.0005f))
			{
				SQSetSet01(Save, Row.Id, Frac);
				bChanged = true;
				if (Row.Id == ESQSetRow::Master || Row.Id == ESQSetRow::Music) Result.bAudioChanged = true;
				if (Row.Id == ESQSetRow::Master) SQSettings::ApplyMasterVolume(World, Save);
			}
			const bool bHot = bFocus || Drag == RowIdx;
			P.Round(SQSetA(SQUI::PanelDeep, A), TrackX, MidY - TrackH * 0.5f, TrackW, TrackH, TrackH * 0.5f);
			P.Round(SQSetA(SQUI::Soul, A), TrackX, MidY - TrackH * 0.5f, FMath::Max(TrackH, TrackW * Frac), TrackH, TrackH * 0.5f);
			const float Knob = bHot ? 18.f : 15.f;
			const float KnobX = TrackX + TrackW * Frac - Knob * 0.5f;
			if (bHot) P.Round(SQSetA(SQUI::WithAlpha(SQUI::Soul, 0.25f), A), KnobX - 4.f, MidY - Knob * 0.5f - 4.f, Knob + 8.f, Knob + 8.f, Knob * 0.5f + 4.f);
			P.Round(SQSetA(SQUI::BoneWhite, A), KnobX, MidY - Knob * 0.5f, Knob, Knob, Knob * 0.5f);
			P.Text(SQSetValueText(Save, Row.Id), ESQTutFont::LabelBold, 14.f, RightX, MidY - 0.62f * 14.f, SQSetA(bHot ? SQUI::Soul : SQUI::Muted, A), 2);
			break;
		}
		case ESQSetKind::Toggle:
		{
			bool* Value = SQSetBool(Save, Row.Id);
			if (!Value) break;
			const float TogW = 48.f, TogH = 26.f, TogX = RightX - TogW, TogY = MidY - TogH * 0.5f;
			const bool bOn = *Value;
			P.Round(SQSetA(bOn ? SQUI::Soul : SQUI::Line, A), TogX, TogY, TogW, TogH, TogH * 0.5f);
			const float KnobS = TogH - 6.f;
			P.Round(SQSetA(SQUI::Midnight, A), bOn ? TogX + TogW - 3.f - KnobS : TogX + 3.f, TogY + 3.f, KnobS, KnobS, KnobS * 0.5f);
			// the whole row flips it
			bool bFlip = bFocus && (P.bNavLeft || P.bNavRight || P.bConfirm);
			if (bOverRow && bClick)
			{
				EatClick();
				Focus = RowIdx;
				bFlip = true;
			}
			if (bFlip)
			{
				*Value = !bOn;
				bChanged = true;
				if (Row.Id == ESQSetRow::AutoPick) Result.bAutoPickChanged = true;
			}
			break;
		}
		case ESQSetKind::Button:
		{
			// Training Shift: REPLAY (run: starts now) / NEXT RUN (menu: the next run plays it)
			const FString Label = bInRun ? FString(TEXT("REPLAY NOW")) : (Save->bTrainingReplay ? FString(TEXT("ON NEXT RUN")) : FString(TEXT("PLAY NEXT RUN")));
			const float ButW = P.TextWidth(Label, ESQTutFont::LabelBold, 13.f) + 30.f, ButH = 34.f;
			const float ButX = RightX - ButW, ButY = MidY - ButH * 0.5f;
			const bool bOverBut = SQSetIn(P, ButX, ButY, ButW, ButH);
			ReplayLift = FMath::FInterpTo(ReplayLift, bOverBut ? 1.f : 0.f, P.Delta, 14.f);
			P.Round(SQSetA(FMath::Lerp(SQUI::Soul, SQUI::BoneWhite, 0.25f * ReplayLift), A), ButX, ButY, ButW, ButH, 8.f);
			P.Text(Label, ESQTutFont::LabelBold, 13.f, ButX + ButW * 0.5f, MidY - 0.62f * 13.f, SQSetA(SQUI::Midnight, A), 1);
			if ((bOverBut && bClick) || (bFocus && P.bConfirm))
			{
				if (bOverBut && bClick) EatClick();
				Focus = RowIdx;
				Result.bReplayTraining = true;
				if (!bInRun && USQTutorial::Get(World))
				{
					USQTutorial::Get(World)->ArmTrainingReplay();
				}
				Tick();
			}
			break;
		}
		}
		if (bChanged)
		{
			Result.bChanged = true;
			Tick();
			if (Drag < 0) USQSaveGame::Store(Save); // sliders store on release
		}
	}

	// CONTROLS: the key list under the toggle, two columns
	if (Tab == 3)
	{
		const float ListY = CurY + NumRows * SQSetRowH + 10.f;
		const float ColW = (W - 2.f * SQSetPad - 20.f) * 0.5f;
		const float BindH = 35.f;
		const int32 NumBinds = static_cast<int32>(UE_ARRAY_COUNT(SQSetBinds));
		const int32 PerCol = (NumBinds + 1) / 2;
		for (int32 BindIdx = 0; BindIdx < NumBinds; ++BindIdx)
		{
			const float BX = X + SQSetPad + (BindIdx / PerCol) * (ColW + 20.f);
			const float BY = ListY + (BindIdx % PerCol) * BindH;
			const float KeyW = USQTutorial::DrawSoulKey(P, SQSetBinds[BindIdx].Key, BX, BY, 26.f, A);
			const FString Action(SQSetBinds[BindIdx].Action);
			const float MaxW = ColW - KeyW - 10.f;
			const float FullW = P.TextWidth(Action, ESQTutFont::Label, 14.f);
			const float Px = FullW > MaxW && FullW > 0.f ? FMath::Max(10.f, 14.f * MaxW / FullW) : 14.f;
			P.Text(Action, ESQTutFont::Label, Px, BX + KeyW + 10.f, BY + 13.f - 0.62f * Px, SQSetA(SQUI::BoneWhite, A), 0);
		}
	}
	CurY += SQSetBodyH + 14.f;

	// ---- note
	P.Text(NoteLine1, ESQTutFont::Label, 14.f, X + SQSetPad, CurY, SQSetA(SQUI::Muted, A), 0);
	P.Text(NoteLine2, ESQTutFont::Label, 14.f, X + SQSetPad, CurY + 20.f, SQSetA(SQUI::Muted, A), 0);
	CurY += 40.f + 14.f;

	// ---- RESET DEFAULTS (quiet) and DONE (teal)
	{
		const float ButH = 44.f;
		const float ResetW = 200.f, DoneW = 160.f;
		const float ResetX = X + SQSetPad, DoneX = X + W - SQSetPad - DoneW;
		const bool bOverReset = SQSetIn(P, ResetX, CurY, ResetW, ButH);
		const bool bOverDone = SQSetIn(P, DoneX, CurY, DoneW, ButH);
		ResetLift = FMath::FInterpTo(ResetLift, bOverReset ? 1.f : 0.f, P.Delta, 14.f);
		DoneLift = FMath::FInterpTo(DoneLift, bOverDone ? 1.f : 0.f, P.Delta, 14.f);
		P.Round(SQSetA(ResetLift > 0.5f ? SQUI::Muted : SQUI::Line, A), ResetX - 1.5f, CurY - 1.5f, ResetW + 3.f, ButH + 3.f, 11.5f);
		P.Round(SQSetA(SQUI::Bar, A), ResetX, CurY, ResetW, ButH, 10.f);
		P.Text(TEXT("RESET DEFAULTS"), ESQTutFont::Head, 17.f, ResetX + ResetW * 0.5f, CurY + ButH * 0.5f - 0.62f * 17.f,
			SQSetA(FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, ResetLift), A), 1);
		P.Round(SQSetA(FMath::Lerp(SQUI::Soul, SQUI::BoneWhite, 0.2f * DoneLift), A), DoneX, CurY, DoneW, ButH, 10.f);
		P.Text(TEXT("DONE"), ESQTutFont::Head, 19.f, DoneX + DoneW * 0.5f, CurY + ButH * 0.5f - 0.62f * 19.f, SQSetA(SQUI::Midnight, A), 1);
		if (bOverReset && bClick)
		{
			EatClick();
			const bool bWasAutoPick = Save->bAutoPickLevelUps;
			SQSettings::ResetDefaults(Save);
			SQSettings::ApplyMasterVolume(World, Save);
			USQSaveGame::Store(Save);
			Result.bChanged = true;
			Result.bAudioChanged = true;
			Result.bAutoPickChanged = bWasAutoPick != Save->bAutoPickLevelUps;
			Tick();
		}
		if (bOverDone && bClick)
		{
			EatClick();
			Result.bClose = true;
		}
	}
	return Result;
}
