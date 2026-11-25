// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQSave.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Guid.h"
#include "Kismet/GameplayStatics.h"
#include "SQInput.h"
#include "SQPerf.h"
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
	// Platform pass (Nov 23 2026; SettingsVersion 3). Fields a profile from before it lacks load at their defaults above.
	StickSensitivity = FMath::IsFinite(StickSensitivity) ? FMath::Clamp(StickSensitivity, 0.f, 1.f) : 0.5f;
	StickDeadZone = FMath::IsFinite(StickDeadZone) ? FMath::Clamp(StickDeadZone, SQSettings::DeadMin, SQSettings::DeadMax) : 0.2f;
	HudScale = FMath::IsFinite(HudScale) ? FMath::Clamp(HudScale, SQSettings::HudScaleMin, SQSettings::HudScaleMax) : 1.f;
	ColourBlindMode = FMath::Min<uint8>(ColourBlindMode, 3);
	ColourBlindStrength = FMath::IsFinite(ColourBlindStrength) ? FMath::Clamp(ColourBlindStrength, 0.f, 1.f) : 1.f;
	MinimapScale = FMath::IsFinite(MinimapScale) ? FMath::Clamp(MinimapScale, SQSettings::MinimapScaleMin, SQSettings::MinimapScaleMax) : 1.f;
	ResScale = FMath::IsFinite(ResScale) ? FMath::Clamp(ResScale, SQSettings::ResScaleMin, 1.f) : 1.f;
	GfxPreset = FMath::Min<uint8>(GfxPreset, 4);
	GfxEffects = FMath::Min<uint8>(GfxEffects, 3);
	GfxHorde = FMath::Min<uint8>(GfxHorde, 3);
	GfxShadows = FMath::Min<uint8>(GfxShadows, 3);
	GfxViewDistance = FMath::Min<uint8>(GfxViewDistance, 3);
	FrameCap = FMath::Clamp(FrameCap, 0, 360);
	if (FrameCap > 0 && FrameCap < 30) FrameCap = 30;
	// binds naming a key that no longer exists are dropped (SQInput::Key falls back to the default anyway)
	for (auto It = KeyBinds.CreateIterator(); It; ++It) { if (!FKey(It.Value()).IsValid()) It.RemoveCurrent(); }
	for (auto It = PadBinds.CreateIterator(); It; ++It) { if (!FKey(It.Value()).IsValid()) It.RemoveCurrent(); }
	if (PlayerId.IsEmpty() || PlayerId.Len() > 40)
	{
		// stable id for reconnecting to a co-op run (never shown, never sent anywhere but the host you join)
		PlayerId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
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
	Save->StickSensitivity = 0.5f;
	Save->StickDeadZone = 0.2f;
	Save->bCameraAssist = true;
	Save->bVibration = true;
	Save->bSlideToggle = false;
	Save->bHoldToConfirm = true;
	Save->HudScale = 1.f;
	Save->bShapeCues = false;
	Save->bReduceFlash = false;
	Save->ColourBlindMode = 0;
	Save->ColourBlindStrength = 1.f;
	Save->MinimapScale = 1.f;
	Save->bGfxGovernor = true;
	Save->bShowFps = false;
	Save->FrameCap = 0;
	Save->bVSync = true;
	SQPerf::SetPreset(Save, 2);
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
	enum class ESQSetRow : uint8
	{
		CamMode, Zoom, MouseSens, StickSens, Smooth, InvertY, CamAssist, Shake,
		DmgNumbers, Tips, AutoPick, Replay,
		GfxPreset, GfxEffects, GfxHorde, GfxShadows, GfxView, GfxOutlines, GfxBloom, GfxGovernor,
		WindowMode, Resolution, ResScale, FrameCap, VSync, ShowFps,
		KeyHints, DeadZone,
		HudScale, ShapeCues, ReduceFlash, HoldConfirm, SlideMode, Vibration,
		ColourBlind, ColourBlindStrength, MinimapSize,
		Master, Music
	};
	enum class ESQSetKind : uint8 { Segment, Slider, Toggle, Button, Cycle };

	struct FSQSetRowDef
	{
		ESQSetRow Id;
		ESQSetKind Kind;
		const TCHAR* Label;
	};

	const FSQSetRowDef SQSetCamera[] = {
		{ ESQSetRow::CamMode, ESQSetKind::Segment, TEXT("Aiming") },
		{ ESQSetRow::Zoom, ESQSetKind::Slider, TEXT("Camera distance") },
		{ ESQSetRow::MouseSens, ESQSetKind::Slider, TEXT("Mouse sensitivity") },
		{ ESQSetRow::StickSens, ESQSetKind::Slider, TEXT("Stick sensitivity") },
		{ ESQSetRow::Smooth, ESQSetKind::Slider, TEXT("Follow smoothing") },
		{ ESQSetRow::InvertY, ESQSetKind::Toggle, TEXT("Invert look up / down") },
		{ ESQSetRow::CamAssist, ESQSetKind::Toggle, TEXT("Controller camera assist") },
		{ ESQSetRow::Shake, ESQSetKind::Segment, TEXT("Screen shake") },
	};
	const FSQSetRowDef SQSetGameplay[] = {
		{ ESQSetRow::DmgNumbers, ESQSetKind::Toggle, TEXT("Damage numbers") },
		{ ESQSetRow::Tips, ESQSetKind::Toggle, TEXT("Tutorial tips") },
		{ ESQSetRow::AutoPick, ESQSetKind::Toggle, TEXT("Auto-pick level-ups") },
		{ ESQSetRow::MinimapSize, ESQSetKind::Slider, TEXT("Minimap size (M = full map)") },
		{ ESQSetRow::Replay, ESQSetKind::Button, TEXT("How-to-play prompts") },
	};
	const FSQSetRowDef SQSetGraphics[] = {
		{ ESQSetRow::GfxPreset, ESQSetKind::Segment, TEXT("Quality preset") },
		{ ESQSetRow::GfxEffects, ESQSetKind::Segment, TEXT("Effects and damage numbers") },
		{ ESQSetRow::GfxHorde, ESQSetKind::Segment, TEXT("Horde detail (animated enemies)") },
		{ ESQSetRow::GfxShadows, ESQSetKind::Segment, TEXT("Shadows") },
		{ ESQSetRow::GfxView, ESQSetKind::Segment, TEXT("View and decor distance") },
		{ ESQSetRow::GfxOutlines, ESQSetKind::Toggle, TEXT("Ink outlines") },
		{ ESQSetRow::GfxBloom, ESQSetKind::Toggle, TEXT("Bloom glow") },
		{ ESQSetRow::GfxGovernor, ESQSetKind::Toggle, TEXT("Auto quality (keeps the frame rate)") },
	};
	const FSQSetRowDef SQSetDisplay[] = {
		{ ESQSetRow::WindowMode, ESQSetKind::Segment, TEXT("Window mode") },
		{ ESQSetRow::Resolution, ESQSetKind::Cycle, TEXT("Resolution") },
		{ ESQSetRow::ResScale, ESQSetKind::Slider, TEXT("Resolution scale") },
		{ ESQSetRow::FrameCap, ESQSetKind::Cycle, TEXT("Frame rate cap") },
		{ ESQSetRow::VSync, ESQSetKind::Toggle, TEXT("V-sync") },
		{ ESQSetRow::ShowFps, ESQSetKind::Toggle, TEXT("Show FPS") },
	};
	const FSQSetRowDef SQSetControls[] = {
		{ ESQSetRow::KeyHints, ESQSetKind::Toggle, TEXT("Show key hints") },
		{ ESQSetRow::DeadZone, ESQSetKind::Slider, TEXT("Stick dead zone") },
	};
	const FSQSetRowDef SQSetAccess[] = {
		{ ESQSetRow::HudScale, ESQSetKind::Slider, TEXT("HUD size") },
		{ ESQSetRow::ColourBlind, ESQSetKind::Segment, TEXT("Colour-blind mode") },
		{ ESQSetRow::ColourBlindStrength, ESQSetKind::Slider, TEXT("Colour-blind filter strength") },
		{ ESQSetRow::ShapeCues, ESQSetKind::Toggle, TEXT("Shapes as well as colours") },
		{ ESQSetRow::ReduceFlash, ESQSetKind::Toggle, TEXT("Reduce flashes") },
		{ ESQSetRow::HoldConfirm, ESQSetKind::Toggle, TEXT("Hold to confirm big choices") },
		{ ESQSetRow::SlideMode, ESQSetKind::Segment, TEXT("Slide") },
		{ ESQSetRow::Vibration, ESQSetKind::Toggle, TEXT("Controller vibration") },
	};
	const FSQSetRowDef SQSetAudio[] = {
		{ ESQSetRow::Master, ESQSetKind::Slider, TEXT("Master volume") },
		{ ESQSetRow::Music, ESQSetKind::Slider, TEXT("Music volume") },
	};
	constexpr int32 SQSetTabControls = 4;

	constexpr float SQSetPad = 28.f;
	constexpr float SQSetRowH = 48.f;
	constexpr float SQSetBodyH = 8.f * SQSetRowH;

	FLinearColor SQSetA(const FLinearColor& Colour, float Alpha)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * Alpha);
	}

	bool SQSetIn(const FSQTutorialPainter& P, float X, float Y, float W, float H)
	{
		return P.bHasMouse && P.MouseX >= X && P.MouseX <= X + W && P.MouseY >= Y && P.MouseY <= Y + H;
	}

	float SQSetRange01(float Value, float Min, float Max)
	{
		return Max > Min ? (Value - Min) / (Max - Min) : 0.f;
	}

	/** Slider value 0..1 of a row */
	float SQSetGet01(const USQSaveGame* Save, ESQSetRow Id)
	{
		switch (Id)
		{
		case ESQSetRow::Zoom: return SQSetRange01(Save->CameraZoom, SQSettings::ZoomMin, SQSettings::ZoomMax);
		case ESQSetRow::MouseSens: return SQSetRange01(Save->MouseSensitivity, SQSettings::SensMin, SQSettings::SensMax);
		case ESQSetRow::StickSens: return Save->StickSensitivity;
		case ESQSetRow::Smooth: return Save->CameraSmoothing;
		case ESQSetRow::Shake: return Save->ScreenShake;
		case ESQSetRow::ResScale: return SQSetRange01(Save->ResScale, SQSettings::ResScaleMin, 1.f);
		case ESQSetRow::DeadZone: return SQSetRange01(Save->StickDeadZone, SQSettings::DeadMin, SQSettings::DeadMax);
		case ESQSetRow::HudScale: return SQSetRange01(Save->HudScale, SQSettings::HudScaleMin, SQSettings::HudScaleMax);
		case ESQSetRow::MinimapSize: return SQSetRange01(Save->MinimapScale, SQSettings::MinimapScaleMin, SQSettings::MinimapScaleMax);
		case ESQSetRow::ColourBlindStrength: return Save->ColourBlindStrength;
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
		case ESQSetRow::MouseSens: Save->MouseSensitivity = FMath::Lerp(SQSettings::SensMin, SQSettings::SensMax, V); break;
		case ESQSetRow::StickSens: Save->StickSensitivity = V; break;
		case ESQSetRow::Smooth: Save->CameraSmoothing = V; break;
		case ESQSetRow::Shake: Save->ScreenShake = V; break;
		case ESQSetRow::ResScale: Save->ResScale = FMath::Lerp(SQSettings::ResScaleMin, 1.f, V); break;
		case ESQSetRow::DeadZone: Save->StickDeadZone = FMath::Lerp(SQSettings::DeadMin, SQSettings::DeadMax, V); break;
		case ESQSetRow::HudScale: Save->HudScale = FMath::Lerp(SQSettings::HudScaleMin, SQSettings::HudScaleMax, V); break;
		case ESQSetRow::MinimapSize: Save->MinimapScale = FMath::Lerp(SQSettings::MinimapScaleMin, SQSettings::MinimapScaleMax, V); break;
		case ESQSetRow::ColourBlindStrength: Save->ColourBlindStrength = V; break;
		case ESQSetRow::Master: Save->MasterVolume = V; break;
		case ESQSetRow::Music: Save->MusicVolume = V; break;
		default: break;
		}
	}

	FString SQSetValueText(const USQSaveGame* Save, ESQSetRow Id)
	{
		switch (Id)
		{
		case ESQSetRow::ResScale: return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Save->ResScale * 100.f));
		case ESQSetRow::HudScale: return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Save->HudScale * 100.f));
		case ESQSetRow::MinimapSize: return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Save->MinimapScale * 100.f));
		case ESQSetRow::DeadZone: return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Save->StickDeadZone * 100.f));
		default: return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(SQSetGet01(Save, Id) * 100.f));
		}
	}

	bool* SQSetBool(USQSaveGame* Save, ESQSetRow Id)
	{
		switch (Id)
		{
		case ESQSetRow::InvertY: return &Save->bInvertLookY;
		case ESQSetRow::CamAssist: return &Save->bCameraAssist;
		case ESQSetRow::DmgNumbers: return &Save->bDamageNumbers;
		case ESQSetRow::Tips: return &Save->bTutorialTips;
		case ESQSetRow::AutoPick: return &Save->bAutoPickLevelUps;
		case ESQSetRow::GfxOutlines: return &Save->bGfxOutlines;
		case ESQSetRow::GfxBloom: return &Save->bGfxBloom;
		case ESQSetRow::GfxGovernor: return &Save->bGfxGovernor;
		case ESQSetRow::VSync: return &Save->bVSync;
		case ESQSetRow::ShowFps: return &Save->bShowFps;
		case ESQSetRow::KeyHints: return &Save->bShowKeyHints;
		case ESQSetRow::ShapeCues: return &Save->bShapeCues;
		case ESQSetRow::ReduceFlash: return &Save->bReduceFlash;
		case ESQSetRow::HoldConfirm: return &Save->bHoldToConfirm;
		case ESQSetRow::Vibration: return &Save->bVibration;
		default: return nullptr;
		}
	}

	bool SQSetIsGraphics(ESQSetRow Id)
	{
		switch (Id)
		{
		case ESQSetRow::GfxPreset: case ESQSetRow::GfxEffects: case ESQSetRow::GfxHorde: case ESQSetRow::GfxShadows: case ESQSetRow::GfxView:
		case ESQSetRow::GfxOutlines: case ESQSetRow::GfxBloom: case ESQSetRow::GfxGovernor: case ESQSetRow::ResScale: case ESQSetRow::FrameCap:
		case ESQSetRow::VSync: case ESQSetRow::ReduceFlash:
			return true;
		default:
			return false;
		}
	}

	/** Options of a segment row and the selected one (-1 = none lit, the CUSTOM preset) */
	int32 SQSetSegment(const USQSaveGame* Save, ESQSetRow Id, const TCHAR* Opts[4], int32& OutSel)
	{
		static const TCHAR* Levels[4] = { TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("ULTRA") };
		auto UseLevels = [&Opts]() { for (int32 LevelIdx = 0; LevelIdx < 4; ++LevelIdx) Opts[LevelIdx] = Levels[LevelIdx]; };
		switch (Id)
		{
		case ESQSetRow::CamMode: Opts[0] = TEXT("AUTO"); Opts[1] = TEXT("MANUAL"); OutSel = Save->AimMode == 1 ? 1 : 0; return 2;
		case ESQSetRow::SlideMode: Opts[0] = TEXT("HOLD"); Opts[1] = TEXT("TOGGLE"); OutSel = Save->bSlideToggle ? 1 : 0; return 2;
		case ESQSetRow::Shake:
			Opts[0] = TEXT("OFF"); Opts[1] = TEXT("LOW"); Opts[2] = TEXT("NORMAL"); Opts[3] = TEXT("STRONG");
			OutSel = Save->ScreenShake <= 0.01f ? 0 : (Save->ScreenShake < 0.375f ? 1 : (Save->ScreenShake < 0.75f ? 2 : 3));
			return 4;
		case ESQSetRow::ColourBlind:
			Opts[0] = TEXT("OFF"); Opts[1] = TEXT("PROTAN"); Opts[2] = TEXT("DEUTAN"); Opts[3] = TEXT("TRITAN");
			OutSel = FMath::Min<int32>(Save->ColourBlindMode, 3);
			return 4;
		case ESQSetRow::WindowMode: Opts[0] = TEXT("WINDOWED"); Opts[1] = TEXT("BORDERLESS"); Opts[2] = TEXT("FULLSCREEN"); OutSel = SQPerf::WindowMode(); return 3;
		case ESQSetRow::GfxPreset: UseLevels(); OutSel = Save->GfxPreset < SQPerf::PresetCustom ? Save->GfxPreset : -1; return 4;
		case ESQSetRow::GfxEffects: UseLevels(); OutSel = FMath::Min<int32>(Save->GfxEffects, 3); return 4;
		case ESQSetRow::GfxHorde: UseLevels(); OutSel = FMath::Min<int32>(Save->GfxHorde, 3); return 4;
		case ESQSetRow::GfxShadows: UseLevels(); OutSel = FMath::Min<int32>(Save->GfxShadows, 3); return 4;
		case ESQSetRow::GfxView: UseLevels(); OutSel = FMath::Min<int32>(Save->GfxViewDistance, 3); return 4;
		default: OutSel = 0; return 0;
		}
	}

	void SQSetSegmentSet(USQSaveGame* Save, const UWorld* World, ESQSetRow Id, int32 Want)
	{
		const uint8 Value = static_cast<uint8>(FMath::Clamp(Want, 0, 3));
		switch (Id)
		{
		case ESQSetRow::CamMode: Save->AimMode = Value > 0 ? 1 : 0; break;
		case ESQSetRow::SlideMode: Save->bSlideToggle = Value > 0; break;
		case ESQSetRow::Shake: { static const float ShakeLevels[4] = { 0.f, 0.25f, 0.5f, 1.f }; Save->ScreenShake = ShakeLevels[Value]; break; }
		case ESQSetRow::ColourBlind: Save->ColourBlindMode = Value; break;
		case ESQSetRow::WindowMode: SQPerf::SetWindowMode(World, Value); break;
		case ESQSetRow::GfxPreset: SQPerf::SetPreset(Save, Value); break;
		case ESQSetRow::GfxEffects: Save->GfxEffects = Value; break;
		case ESQSetRow::GfxHorde: Save->GfxHorde = Value; break;
		case ESQSetRow::GfxShadows: Save->GfxShadows = Value; break;
		case ESQSetRow::GfxView: Save->GfxViewDistance = Value; break;
		default: break;
		}
	}

	FString SQSetCycleText(const USQSaveGame* Save, ESQSetRow Id)
	{
		if (Id == ESQSetRow::FrameCap)
		{
			return Save->FrameCap > 0 ? FString::Printf(TEXT("%d FPS"), Save->FrameCap) : FString(TEXT("UNLIMITED"));
		}
		const FIntPoint Res = SQPerf::CurrentResolution();
		return FString::Printf(TEXT("%d x %d"), Res.X, Res.Y);
	}

	void SQSetCycleStep(USQSaveGame* Save, const UWorld* World, ESQSetRow Id, int32 Dir)
	{
		if (Id == ESQSetRow::FrameCap)
		{
			const TArray<int32>& Caps = SQPerf::FrameCaps();
			if (Caps.Num() == 0) return;
			int32 At = Caps.IndexOfByKey(Save->FrameCap);
			if (At == INDEX_NONE) At = 0;
			At = (At + Dir + Caps.Num()) % Caps.Num();
			Save->FrameCap = Caps[At];
			return;
		}
		TArray<FIntPoint> List;
		SQPerf::Resolutions(List);
		if (List.Num() == 0) return;
		int32 At = List.IndexOfByKey(SQPerf::CurrentResolution());
		if (At == INDEX_NONE) At = List.Num() - 1;
		At = (At + Dir + List.Num()) % List.Num();
		SQPerf::SetResolution(World, List[At]);
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

	// ---- waiting for a key (CONTROLS tab): nothing else reads input this frame
	const bool bCapturing = RebindAct >= 0;
	bool bNavUp = P.bNavUp, bNavDown = P.bNavDown, bNavLeft = P.bNavLeft, bNavRight = P.bNavRight;
	bool bConfirm = P.bConfirm, bTabNext = P.bTabNext, bTabPrev = P.bTabPrev, bBack = P.bBack;
	if (bCapturing)
	{
		bNavUp = bNavDown = bNavLeft = bNavRight = bConfirm = bTabNext = bTabPrev = bBack = false;
		bClick = false;
		const float Waited = P.Now - RebindSince;
		bool bEnd = Waited > 6.f || !P.PC || RebindAct >= SQInput::NumActs;
		if (!bEnd && Waited > 0.2f)
		{
			if (P.PC->WasInputKeyJustPressed(EKeys::Escape))
			{
				bEnd = true;
			}
			else
			{
				const FKey Pressed = SQInput::FirstKeyPressed(P.PC, bRebindPad);
				if (Pressed.IsValid())
				{
					if (SQInput::SetKey(static_cast<SQInput::EAct>(RebindAct), bRebindPad, Pressed))
					{
						Result.bChanged = true;
						Result.bBindsChanged = true;
						Tick();
					}
					bEnd = true;
				}
			}
		}
		if (bEnd) RebindAct = -1;
		Result.bAteClick = P.bClick;
	}

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
		case 2: Rows = SQSetGraphics; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetGraphics)); break;
		case 3: Rows = SQSetDisplay; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetDisplay)); break;
		case 4: Rows = SQSetControls; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetControls)); break;
		case 5: Rows = SQSetAccess; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetAccess)); break;
		case 6: Rows = SQSetAudio; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetAudio)); break;
		default: Rows = SQSetCamera; NumRows = static_cast<int32>(UE_ARRAY_COUNT(SQSetCamera)); break;
		}
	};
	Tab = FMath::Clamp(Tab, 0, NumTabs - 1);
	// Tab / RB = next tab, LB = previous; Up / Down rows (-1 = the tab strip); Left / Right on the strip = tabs
	if (bTabNext || bTabPrev)
	{
		Tab = (Tab + (bTabNext ? 1 : NumTabs - 1)) % NumTabs;
		Focus = FMath::Min(Focus, 0);
		Drag = -1;
		Tick();
	}
	PickRows(Tab);
	// focus order: -1 tab strip, the rows, (CONTROLS) the ten actions, then RESET and DONE
	int32 NumBinds = Tab == SQSetTabControls ? SQInput::NumActs : 0;
	int32 FocusReset = NumRows + NumBinds;
	if (bNavDown && Focus < FocusReset) { Focus++; Tick(); }
	else if (bNavUp && Focus > -1) { Focus = Focus > FocusReset ? FocusReset - 1 : Focus - 1; Tick(); }
	Focus = FMath::Clamp(Focus, -1, FocusReset + 1);
	if (Focus == -1 && (bNavLeft || bNavRight))
	{
		Tab = (Tab + (bNavRight ? 1 : NumTabs - 1)) % NumTabs;
		Drag = -1;
		Tick();
		PickRows(Tab);
		NumBinds = Tab == SQSetTabControls ? SQInput::NumActs : 0;
		FocusReset = NumRows + NumBinds;
	}
	{
		static const TCHAR* TabNames[NumTabs] = { TEXT("CAMERA"), TEXT("GAMEPLAY"), TEXT("GRAPHICS"), TEXT("DISPLAY"), TEXT("CONTROLS"), TEXT("ACCESS"), TEXT("AUDIO") };
		const float TabsX = X + SQSetPad, TabsW = W - 2.f * SQSetPad, TabsH = 42.f;
		if (Focus == -1) P.Round(SQSetA(SQUI::WithAlpha(SQUI::Soul, 0.35f), A), TabsX - 2.f, CurY - 2.f, TabsW + 4.f, TabsH + 4.f, 12.f);
		P.Round(SQSetA(SQUI::PanelDeep, A), TabsX, CurY, TabsW, TabsH, 10.f);
		const float SegW = (TabsW - 8.f) / NumTabs;
		for (int32 TabIdx = 0; TabIdx < NumTabs; ++TabIdx)
		{
			const float SegX = TabsX + 4.f + TabIdx * SegW;
			const bool bSel = TabIdx == Tab;
			const bool bOver = SQSetIn(P, SegX, CurY + 4.f, SegW, TabsH - 8.f);
			TabLift[TabIdx] = FMath::FInterpTo(TabLift[TabIdx], bOver ? 1.f : 0.f, P.Delta, 14.f);
			if (bSel) P.Round(SQSetA(SQUI::Soul, A), SegX + 1.f, CurY + 4.f, SegW - 2.f, TabsH - 8.f, 7.f);
			else if (TabLift[TabIdx] > 0.02f) P.Round(SQSetA(SQUI::WithAlpha(SQUI::PanelHover, TabLift[TabIdx]), A), SegX + 1.f, CurY + 4.f, SegW - 2.f, TabsH - 8.f, 7.f);
			P.Text(TabNames[TabIdx], ESQTutFont::LabelBold, 13.f, SegX + SegW * 0.5f, CurY + TabsH * 0.5f - 0.62f * 13.f,
				SQSetA(bSel ? SQUI::Midnight : FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, TabLift[TabIdx]), A), 1);
			if (bOver && bClick && !bSel)
			{
				EatClick();
				Tab = TabIdx;
				Focus = 0;
				Drag = -1;
				Tick();
				PickRows(Tab);
				NumBinds = Tab == SQSetTabControls ? SQInput::NumActs : 0;
				FocusReset = NumRows + NumBinds;
			}
		}
		CurY += TabsH + 10.f;
	}

	// ---- rows
	if (!P.bMouseHeld && Drag >= 0)
	{
		Drag = -1;
		USQSaveGame::Store(Save); // slider released: write it once
		SQPerf::Apply(World, Save);
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
		if (bFocus) P.Round(SQSetA(SQUI::Soul, A), X + 14.f, RowY + 13.f, 3.f, SQSetRowH - 26.f, 1.5f);
		P.Round(SQSetA(SQUI::Line, A), X + SQSetPad, RowY + SQSetRowH - 1.f, W - 2.f * SQSetPad, 1.f, 0.f);
		P.Text(Row.Label, ESQTutFont::Label, 18.f, X + SQSetPad, MidY - 0.62f * 18.f, SQSetA(SQUI::BoneWhite, A), 0);

		bool bChanged = false;
		switch (Row.Kind)
		{
		case ESQSetKind::Segment:
		{
			const TCHAR* Opts[4] = { TEXT(""), TEXT(""), TEXT(""), TEXT("") };
			int32 Sel = 0;
			const int32 NumOpts = SQSetSegment(Save, Row.Id, Opts, Sel);
			if (NumOpts <= 0) break;
			float OptW[4] = { 0.f, 0.f, 0.f, 0.f };
			float GroupW = 6.f;
			for (int32 OptIdx = 0; OptIdx < NumOpts; ++OptIdx)
			{
				OptW[OptIdx] = P.TextWidth(Opts[OptIdx], ESQTutFont::LabelBold, 13.f) + 24.f;
				GroupW += OptW[OptIdx] + (OptIdx > 0 ? 4.f : 0.f);
			}
			const float GroupH = 32.f, GroupX = RightX - GroupW, GroupY = MidY - GroupH * 0.5f;
			P.Round(SQSetA(SQUI::PanelDeep, A), GroupX, GroupY, GroupW, GroupH, 8.f);
			float OptX = GroupX + 3.f;
			int32 Want = Sel;
			for (int32 OptIdx = 0; OptIdx < NumOpts; ++OptIdx)
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
			if (Sel < 0 && Row.Id == ESQSetRow::GfxPreset)
			{
				P.Text(TEXT("CUSTOM"), ESQTutFont::LabelBold, 12.f, GroupX - 12.f, MidY - 0.62f * 12.f, SQSetA(SQUI::Soul, A), 2);
			}
			if (bFocus)
			{
				// Left / Right step (they stop at the ends); Enter / A cycles
				const int32 From = Sel < 0 ? 2 : Sel;
				if (bNavRight) Want = FMath::Min(NumOpts - 1, Sel < 0 ? From : From + 1);
				else if (bNavLeft) Want = FMath::Max(0, Sel < 0 ? From : From - 1);
				else if (bConfirm) Want = (From + 1) % NumOpts;
			}
			if (Want != Sel)
			{
				SQSetSegmentSet(Save, World, Row.Id, Want);
				bChanged = true;
			}
			break;
		}
		case ESQSetKind::Cycle:
		{
			// <  value  >
			const FString Value = SQSetCycleText(Save, Row.Id);
			const float ArrowW = 30.f, ValueW = 150.f, BoxH = 32.f;
			const float BoxX = RightX - (2.f * ArrowW + ValueW), BoxY = MidY - BoxH * 0.5f;
			P.Round(SQSetA(SQUI::PanelDeep, A), BoxX, BoxY, 2.f * ArrowW + ValueW, BoxH, 8.f);
			const bool bOverPrev = SQSetIn(P, BoxX, BoxY, ArrowW, BoxH);
			const bool bOverNext = SQSetIn(P, BoxX + ArrowW + ValueW, BoxY, ArrowW, BoxH);
			P.Text(TEXT("<"), ESQTutFont::LabelBold, 16.f, BoxX + ArrowW * 0.5f, MidY - 0.62f * 16.f, SQSetA(bOverPrev ? SQUI::BoneWhite : SQUI::Soul, A), 1);
			P.Text(TEXT(">"), ESQTutFont::LabelBold, 16.f, BoxX + ArrowW + ValueW + ArrowW * 0.5f, MidY - 0.62f * 16.f, SQSetA(bOverNext ? SQUI::BoneWhite : SQUI::Soul, A), 1);
			P.Text(Value, ESQTutFont::LabelBold, 14.f, BoxX + ArrowW + ValueW * 0.5f, MidY - 0.62f * 14.f, SQSetA(SQUI::BoneWhite, A), 1);
			int32 Dir = 0;
			if ((bOverPrev || bOverNext) && bClick)
			{
				EatClick();
				Focus = RowIdx;
				Dir = bOverNext ? 1 : -1;
			}
			if (bFocus && (bNavRight || bConfirm)) Dir = 1;
			else if (bFocus && bNavLeft) Dir = -1;
			if (Dir != 0)
			{
				SQSetCycleStep(Save, World, Row.Id, Dir);
				bChanged = true;
			}
			break;
		}
		case ESQSetKind::Slider:
		{
			const float ValueW = 64.f, TrackW = 220.f, TrackH = 7.f;
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
			if (bFocus && (bNavLeft || bNavRight)) Frac = FMath::Clamp(Frac + (bNavRight ? 0.05f : -0.05f), 0.f, 1.f);
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
			// shape as well as colour: ON / OFF in words beside the switch
			P.Text(bOn ? TEXT("ON") : TEXT("OFF"), ESQTutFont::LabelBold, 12.f, TogX - 10.f, MidY - 0.62f * 12.f, SQSetA(bOn ? SQUI::Soul : SQUI::Muted, A), 2);
			// the whole row flips it
			bool bFlip = bFocus && (bNavLeft || bNavRight || bConfirm);
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
			const float ButW = P.TextWidth(Label, ESQTutFont::LabelBold, 13.f) + 30.f, ButH = 32.f;
			const float ButX = RightX - ButW, ButY = MidY - ButH * 0.5f;
			const bool bOverBut = SQSetIn(P, ButX, ButY, ButW, ButH);
			ReplayLift = FMath::FInterpTo(ReplayLift, bOverBut ? 1.f : 0.f, P.Delta, 14.f);
			P.Round(SQSetA(FMath::Lerp(SQUI::Soul, SQUI::BoneWhite, 0.25f * ReplayLift), A), ButX, ButY, ButW, ButH, 8.f);
			P.Text(Label, ESQTutFont::LabelBold, 13.f, ButX + ButW * 0.5f, MidY - 0.62f * 13.f, SQSetA(SQUI::Midnight, A), 1);
			if ((bOverBut && bClick) || (bFocus && bConfirm))
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
			if (SQSetIsGraphics(Row.Id))
			{
				if (Row.Id != ESQSetRow::GfxPreset) SQPerf::NotePresetEdited(Save);
				if (Drag < 0) SQPerf::Apply(World, Save);
			}
			if (Drag < 0) USQSaveGame::Store(Save); // sliders store on release
		}
	}

	// ---- CONTROLS: the rebind list under the two rows (action, keyboard / mouse key, gamepad button), two columns of five
	if (Tab == SQSetTabControls)
	{
		const float ListY = CurY + NumRows * SQSetRowH + 8.f;
		const float ColGap = 20.f;
		const float ColW = (W - 2.f * SQSetPad - ColGap) * 0.5f;
		const float BindH = 50.f;
		const float CapW = 92.f, CapH = 30.f;
		const int32 PerCol = (SQInput::NumActs + 1) / 2;
		P.Text(TEXT("Select a key or button, then press the new one. Esc cancels."), ESQTutFont::Label, 13.f, X + SQSetPad, ListY, SQSetA(SQUI::Muted, A), 0);
		// Left / Right inside the list: keyboard column <-> gamepad column
		const int32 BindFocus = Focus - NumRows;
		if (!bCapturing && BindFocus >= 0 && BindFocus < SQInput::NumActs && (bNavLeft || bNavRight))
		{
			bRebindPad = !bRebindPad;
			Tick();
		}
		for (int32 ActIdx = 0; ActIdx < SQInput::NumActs; ++ActIdx)
		{
			const SQInput::EAct Act = static_cast<SQInput::EAct>(ActIdx);
			const float BX = X + SQSetPad + (ActIdx / PerCol) * (ColW + ColGap);
			const float BY = ListY + 22.f + (ActIdx % PerCol) * BindH;
			const float MidY = BY + BindH * 0.5f;
			const bool bRowFocus = BindFocus == ActIdx;
			if (bRowFocus) P.Round(SQSetA(SQUI::WithAlpha(SQUI::PanelHover, 0.75f), A), BX - 8.f, BY + 3.f, ColW + 16.f, BindH - 6.f, 10.f);
			const float LabelMaxW = ColW - 2.f * CapW - 22.f;
			const FString Action(SQInput::ActName(Act));
			const float FullW = P.TextWidth(Action, ESQTutFont::Label, 15.f);
			const float Px = FullW > LabelMaxW && FullW > 0.f ? FMath::Max(10.f, 15.f * LabelMaxW / FullW) : 15.f;
			P.Text(Action, ESQTutFont::Label, Px, BX, MidY - 0.62f * Px, SQSetA(SQUI::BoneWhite, A), 0);
			for (int32 Device = 0; Device < 2; ++Device)
			{
				const bool bPadCol = Device == 1;
				const float CapX = BX + ColW - (bPadCol ? CapW : 2.f * CapW + 8.f);
				const float CapY = MidY - CapH * 0.5f;
				const bool bWaiting = RebindAct == ActIdx && bRebindPad == bPadCol;
				const bool bCapFocus = bRowFocus && bRebindPad == bPadCol;
				const bool bOverCap = SQSetIn(P, CapX, CapY, CapW, CapH);
				if (bCapFocus || bWaiting) P.Round(SQSetA(SQUI::Soul, A), CapX - 2.f, CapY - 2.f, CapW + 4.f, CapH + 4.f, 9.f);
				P.Round(SQSetA(bWaiting ? SQUI::Soul : (bOverCap ? SQUI::PanelHover : SQUI::PanelDeep), A), CapX, CapY, CapW, CapH, 7.f);
				const FString CapText = bWaiting ? FString(TEXT("PRESS...")) : SQInput::KeyLabel(SQInput::Key(Act, bPadCol));
				const float CapFull = P.TextWidth(CapText, ESQTutFont::LabelBold, 13.f);
				const float CapPx = CapFull > CapW - 10.f && CapFull > 0.f ? FMath::Max(9.f, 13.f * (CapW - 10.f) / CapFull) : 13.f;
				P.Text(CapText, ESQTutFont::LabelBold, CapPx, CapX + CapW * 0.5f, MidY - 0.62f * CapPx, SQSetA(bWaiting ? SQUI::Midnight : SQUI::BoneWhite, A), 1);
				const bool bPick = (bOverCap && bClick) || (bCapFocus && bConfirm);
				if (bPick && !bCapturing && P.PC)
				{
					if (bOverCap && bClick) EatClick();
					Focus = NumRows + ActIdx;
					RebindAct = ActIdx;
					bRebindPad = bPadCol;
					RebindSince = P.Now;
					Tick();
				}
			}
		}
		// column captions
		for (int32 Col = 0; Col < 2; ++Col)
		{
			const float BX = X + SQSetPad + Col * (ColW + ColGap);
			P.Text(TEXT("KEYS"), ESQTutFont::LabelBold, 11.f, BX + ColW - 1.5f * CapW - 8.f, ListY + 18.f, SQSetA(SQUI::Muted, A), 1);
			P.Text(TEXT("PAD"), ESQTutFont::LabelBold, 11.f, BX + ColW - 0.5f * CapW, ListY + 18.f, SQSetA(SQUI::Muted, A), 1);
		}
	}
	CurY += SQSetBodyH + 14.f;

	// ---- note
	P.Text(NoteLine1, ESQTutFont::Label, 14.f, X + SQSetPad, CurY, SQSetA(SQUI::Muted, A), 0);
	{
		// GRAPHICS explains what waits for the next map; DISPLAY shows the live frame rate
		FString Line2 = NoteLine2;
		if (Tab == 2) Line2 = TEXT("Shadows and distance change at once. Effects, horde detail, outlines and bloom change on the next map.");
		else if (Tab == 3) Line2 = FString::Printf(TEXT("Running at %d FPS."), FMath::RoundToInt(SQPerf::SmoothedFps()));
		P.Text(Line2, ESQTutFont::Label, 14.f, X + SQSetPad, CurY + 20.f, SQSetA(SQUI::Muted, A), 0);
	}
	CurY += 40.f + 14.f;

	// ---- RESET DEFAULTS (quiet) and DONE (teal)
	{
		const float ButH = 44.f;
		const float ResetW = 200.f, DoneW = 160.f;
		const float ResetX = X + SQSetPad, DoneX = X + W - SQSetPad - DoneW;
		const bool bOverReset = SQSetIn(P, ResetX, CurY, ResetW, ButH);
		const bool bOverDone = SQSetIn(P, DoneX, CurY, DoneW, ButH);
		const bool bFocusReset = Focus == FocusReset, bFocusDone = Focus == FocusReset + 1;
		if (bFocusReset && bNavRight) { Focus = FocusReset + 1; Tick(); }
		else if (bFocusDone && bNavLeft) { Focus = FocusReset; Tick(); }
		ResetLift = FMath::FInterpTo(ResetLift, bOverReset || bFocusReset ? 1.f : 0.f, P.Delta, 14.f);
		DoneLift = FMath::FInterpTo(DoneLift, bOverDone || bFocusDone ? 1.f : 0.f, P.Delta, 14.f);
		P.Round(SQSetA(bFocusReset ? SQUI::Soul : (ResetLift > 0.5f ? SQUI::Muted : SQUI::Line), A), ResetX - 1.5f, CurY - 1.5f, ResetW + 3.f, ButH + 3.f, 11.5f);
		P.Round(SQSetA(SQUI::Bar, A), ResetX, CurY, ResetW, ButH, 10.f);
		P.Text(TEXT("RESET DEFAULTS"), ESQTutFont::Head, 17.f, ResetX + ResetW * 0.5f, CurY + ButH * 0.5f - 0.62f * 17.f,
			SQSetA(FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, ResetLift), A), 1);
		if (bFocusDone) P.Round(SQSetA(SQUI::BoneWhite, A), DoneX - 2.5f, CurY - 2.5f, DoneW + 5.f, ButH + 5.f, 12.5f);
		P.Round(SQSetA(FMath::Lerp(SQUI::Soul, SQUI::BoneWhite, 0.2f * DoneLift), A), DoneX, CurY, DoneW, ButH, 10.f);
		P.Text(TEXT("DONE"), ESQTutFont::Head, 19.f, DoneX + DoneW * 0.5f, CurY + ButH * 0.5f - 0.62f * 19.f, SQSetA(SQUI::Midnight, A), 1);
		if ((bOverReset && bClick) || (bFocusReset && bConfirm))
		{
			if (bOverReset && bClick) EatClick();
			const bool bWasAutoPick = Save->bAutoPickLevelUps;
			SQSettings::ResetDefaults(Save);
			SQInput::ResetBinds();
			SQSettings::ApplyMasterVolume(World, Save);
			SQPerf::Apply(World, Save);
			USQSaveGame::Store(Save);
			Result.bChanged = true;
			Result.bAudioChanged = true;
			Result.bBindsChanged = true;
			Result.bAutoPickChanged = bWasAutoPick != Save->bAutoPickLevelUps;
			Tick();
		}
		if ((bOverDone && bClick) || (bFocusDone && bConfirm) || bBack)
		{
			if (bOverDone && bClick) EatClick();
			Result.bClose = true;
		}
	}
	return Result;
}
