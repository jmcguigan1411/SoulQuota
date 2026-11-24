// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQPerf.h"
#include "SQData.h"
#include "SQSave.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/App.h"

namespace
{
	// Live governor state (one local machine)
	float SQPerfSmoothedMs = 16.6f;
	float SQPerfPressure = 0.f;
	float SQPerfOverFor = 0.f;
	float SQPerfUnderFor = 0.f;
	float SQPerfFxFloor = 0.45f;
	float SQPerfHordeFloor = 0.6f;
	bool bSQPerfGovernorOn = true;
	int32 SQPerfFrameCap = 0;

	uint8 SQPerfLevel(uint8 Value)
	{
		return static_cast<uint8>(FMath::Clamp(static_cast<int32>(Value), 0, SQPerf::NumLevels - 1));
	}

	/** Base level for the engine groups the panel has no row for (anti-aliasing, textures, shading...) */
	int32 SQPerfBaseLevel(const USQSaveGame* Save)
	{
		if (Save->GfxPreset < SQPerf::PresetCustom) return SQPerfLevel(Save->GfxPreset);
		const int32 Sum = SQPerfLevel(Save->GfxEffects) + SQPerfLevel(Save->GfxHorde) + SQPerfLevel(Save->GfxShadows) + SQPerfLevel(Save->GfxViewDistance);
		return FMath::Clamp((Sum + 2) / 4, 0, SQPerf::NumLevels - 1);
	}

	void SQPerfScaleKey(USQData* Data, const TCHAR* Key, float Fallback, float Mul)
	{
		Data->SetLocalTune(FName(Key), Data->TuneBase(FName(Key), Fallback) * Mul);
	}
}

void SQPerf::SetPreset(USQSaveGame* Save, uint8 Preset)
{
	if (!Save) return;
	const uint8 Level = SQPerfLevel(Preset);
	Save->GfxPreset = Level;
	Save->GfxEffects = Level;
	Save->GfxHorde = Level;
	Save->GfxShadows = Level;
	Save->GfxViewDistance = Level;
	Save->bGfxOutlines = Level >= 1;
	Save->bGfxBloom = Level >= 1;
	Save->ResScale = Level == 0 ? 0.8f : 1.f;
}

void SQPerf::NotePresetEdited(USQSaveGame* Save)
{
	if (!Save || Save->GfxPreset >= PresetCustom) return;
	const uint8 Level = SQPerfLevel(Save->GfxPreset);
	const bool bMatches = Save->GfxEffects == Level && Save->GfxHorde == Level && Save->GfxShadows == Level && Save->GfxViewDistance == Level
		&& Save->bGfxOutlines == (Level >= 1) && Save->bGfxBloom == (Level >= 1);
	if (!bMatches) Save->GfxPreset = PresetCustom;
}

void SQPerf::Apply(UWorld* World, const USQSaveGame* Save)
{
	if (!Save) return;
	const int32 Fx = SQPerfLevel(Save->GfxEffects);
	const int32 Horde = SQPerfLevel(Save->GfxHorde);
	const int32 Shadows = SQPerfLevel(Save->GfxShadows);
	const int32 View = SQPerfLevel(Save->GfxViewDistance);
	const int32 Base = SQPerfBaseLevel(Save);
	bSQPerfGovernorOn = Save->bGfxGovernor;
	SQPerfFrameCap = FMath::Max(0, Save->FrameCap);

	// ---- the game's own cosmetic dials: multipliers on the authored CSV value (HIGH = as authored)
	if (USQData* Data = World ? USQData::Get(World) : nullptr)
	{
		static const float PoolMul[NumLevels] = { 0.45f, 0.75f, 1.f, 1.3f };
		static const float CountMul[NumLevels] = { 0.5f, 0.72f, 1.f, 1.15f };
		static const float AoeMul[NumLevels] = { 0.4f, 0.7f, 1.f, 1.2f };
		static const float MoteMul[NumLevels] = { 0.f, 0.5f, 1.f, 1.6f };
		static const float NumberMul[NumLevels] = { 0.5f, 0.75f, 1.f, 1.3f };
		static const float SkinPoolMul[NumLevels] = { 0.32f, 0.64f, 1.f, 1.45f };
		static const float SkinRadiusMul[NumLevels] = { 0.57f, 0.79f, 1.f, 1.24f };
		static const float DecorMul[NumLevels] = { 0.55f, 0.78f, 1.f, 1.45f };
		Data->ClearLocalTunes();
		SQPerfScaleKey(Data, TEXT("SparkPool"), 600.f, PoolMul[Fx]);
		SQPerfScaleKey(Data, TEXT("WeaponFxPool"), 1000.f, PoolMul[Fx]);
		SQPerfScaleKey(Data, TEXT("StyleFxCount"), 1.4f, CountMul[Fx]);
		SQPerfScaleKey(Data, TEXT("StyleAoeDetail"), 1.f, AoeMul[Fx]);
		SQPerfScaleKey(Data, TEXT("MapMotes"), 44.f, MoteMul[Fx]);
		SQPerfScaleKey(Data, TEXT("DamageNumberCap"), 60.f, NumberMul[Fx]);
		SQPerfScaleKey(Data, TEXT("SkinnedEnemyPool"), 220.f, SkinPoolMul[Horde]);
		SQPerfScaleKey(Data, TEXT("SkinnedEnemyRadius"), 4200.f, SkinRadiusMul[Horde]);
		SQPerfScaleKey(Data, TEXT("SkinnedRiderPool"), 24.f, SkinPoolMul[Horde]);
		if (Horde <= 1 || Shadows == 0) Data->SetLocalTune(TEXT("ImpostorShadows"), 0.f);
		SQPerfScaleKey(Data, TEXT("DecorCullDistance"), 9000.f, DecorMul[View]);
		if (!Save->bGfxOutlines) Data->SetLocalTune(TEXT("StyleInkStrength"), 0.f);
		if (!Save->bGfxBloom) Data->SetLocalTune(TEXT("MapBloom"), FMath::Min(0.15f, Data->TuneBase(TEXT("MapBloom"), 0.6f)));
		if (Save->bReduceFlash) SQPerfScaleKey(Data, TEXT("StyleFxFlash"), 1.f, 0.3f);
		SQPerfFxFloor = FMath::Clamp(Data->TuneBase(TEXT("PerfFxFloor"), 0.45f), 0.2f, 1.f);
		SQPerfHordeFloor = FMath::Clamp(Data->TuneBase(TEXT("PerfHordeFloor"), 0.6f), 0.3f, 1.f);
	}

	// ---- Unreal scalability groups, resolution scale, frame cap, V-sync
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS) return;
	GUS->SetShadowQuality(Shadows);
	GUS->SetViewDistanceQuality(View);
	GUS->SetVisualEffectQuality(Fx);
	GUS->SetPostProcessingQuality(FMath::Max(1, Base)); // the grade, bloom and ink outlines need at least MEDIUM post processing
	GUS->SetAntiAliasingQuality(Base);
	GUS->SetTextureQuality(FMath::Max(1, Base));
	GUS->SetShadingQuality(Base);
	GUS->SetGlobalIlluminationQuality(Base);
	GUS->SetReflectionQuality(Base);
	GUS->SetFoliageQuality(Base);
	GUS->SetResolutionScaleValueEx(FMath::Clamp(Save->ResScale, SQSettings::ResScaleMin, 1.f) * 100.f);
	GUS->SetFrameRateLimit(static_cast<float>(SQPerfFrameCap)); // 0 = no limit
	GUS->SetVSyncEnabled(Save->bVSync);
	// In the editor (PIE) the values are stored but not pushed at the editor viewport
	if (!World || World->WorldType != EWorldType::PIE)
	{
		GUS->ApplyNonResolutionSettings();
	}
	GUS->SaveSettings();
}

bool SQPerf::AutoDetectOnce(UWorld* World, USQSaveGame* Save)
{
	if (!Save || Save->bGfxDetected) return false;
	Save->bGfxDetected = true;
	const USQData* Data = World ? USQData::Get(World) : nullptr;
	const bool bWanted = !Data || Data->TuneBase(TEXT("PerfAutoDetect"), 1.f) >= 0.5f;
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	const bool bEditor = World && World->WorldType == EWorldType::PIE;
	if (bWanted && GUS && !bEditor)
	{
		// Unreal's own CPU / GPU benchmark (about a second), then the overall level it suggests
		GUS->RunHardwareBenchmark();
		GUS->ApplyHardwareBenchmarkResults();
		const int32 Overall = GUS->GetOverallScalabilityLevel(); // -1 = mixed
		const int32 Level = Overall >= 0 ? Overall : FMath::Min(GUS->GetShadowQuality(), GUS->GetVisualEffectQuality());
		SetPreset(Save, static_cast<uint8>(FMath::Clamp(Level, 0, NumLevels - 1)));
	}
	USQSaveGame::Store(Save);
	return bWanted && GUS && !bEditor;
}

void SQPerf::GovernorTick(const UWorld* World, float RealDelta)
{
	const float Dt = FMath::Clamp(RealDelta, 0.f, 0.25f);
	if (Dt <= 0.f) return;
	const float FrameMs = Dt * 1000.f;
	// about half a second of smoothing
	SQPerfSmoothedMs = FMath::Lerp(SQPerfSmoothedMs, FrameMs, FMath::Clamp(Dt * 2.f, 0.f, 1.f));
	const bool bSettled = World && World->GetTimeSeconds() > 5.f && !World->IsPaused();
	if (!bSQPerfGovernorOn || !bSettled)
	{
		// off, loading or paused: ease back to full quality
		SQPerfPressure = FMath::Max(0.f, SQPerfPressure - Dt * 0.25f);
		SQPerfOverFor = 0.f;
		SQPerfUnderFor = 0.f;
		return;
	}
	float TargetFps = 55.f;
	if (const USQData* Data = USQData::Get(World)) TargetFps = FMath::Clamp(Data->TuneBase(TEXT("PerfTargetFps"), 55.f), 20.f, 240.f);
	if (SQPerfFrameCap > 0) TargetFps = FMath::Min(TargetFps, static_cast<float>(SQPerfFrameCap) * 0.92f);
	const float BudgetMs = 1000.f / TargetFps;
	if (SQPerfSmoothedMs > BudgetMs * 1.08f)
	{
		SQPerfOverFor += Dt;
		SQPerfUnderFor = 0.f;
		// slow for 1.5 s: shed over about 4 s
		if (SQPerfOverFor > 1.5f) SQPerfPressure = FMath::Min(1.f, SQPerfPressure + Dt * 0.25f);
	}
	else if (SQPerfSmoothedMs < BudgetMs * 0.85f)
	{
		SQPerfUnderFor += Dt;
		SQPerfOverFor = 0.f;
		// comfortable for 3 s: give quality back slowly (10 s) so it does not flip back and forth
		if (SQPerfUnderFor > 3.f) SQPerfPressure = FMath::Max(0.f, SQPerfPressure - Dt * 0.1f);
	}
	else
	{
		SQPerfOverFor = 0.f;
		SQPerfUnderFor = 0.f;
	}
}

float SQPerf::FxScale()
{
	return FMath::Lerp(1.f, SQPerfFxFloor, SQPerfPressure);
}

float SQPerf::HordeScale()
{
	return FMath::Lerp(1.f, SQPerfHordeFloor, SQPerfPressure);
}

float SQPerf::SmoothedFps()
{
	return SQPerfSmoothedMs > 0.01f ? 1000.f / SQPerfSmoothedMs : 0.f;
}

float SQPerf::Pressure()
{
	return SQPerfPressure;
}

const TArray<int32>& SQPerf::FrameCaps()
{
	static const TArray<int32> Caps = { 0, 30, 40, 60, 90, 120, 144, 165, 240 };
	return Caps;
}

void SQPerf::Resolutions(TArray<FIntPoint>& Out)
{
	Out.Reset();
	const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	const FIntPoint Desk = GUS ? GUS->GetDesktopResolution() : FIntPoint(1920, 1080);
	static const FIntPoint Common[] = {
		FIntPoint(1280, 720), FIntPoint(1280, 800), FIntPoint(1366, 768), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(1920, 1200),
		FIntPoint(2560, 1080), FIntPoint(2560, 1440), FIntPoint(2560, 1600), FIntPoint(3440, 1440), FIntPoint(3840, 2160),
	};
	for (const FIntPoint& Res : Common)
	{
		if (Res.X <= Desk.X && Res.Y <= Desk.Y) Out.Add(Res);
	}
	if (Desk.X > 0 && Desk.Y > 0) Out.AddUnique(Desk);
	if (Out.Num() == 0) Out.Add(FIntPoint(1280, 720));
}

int32 SQPerf::WindowMode()
{
	const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS) return 1;
	switch (GUS->GetFullscreenMode())
	{
	case EWindowMode::Windowed: return 0;
	case EWindowMode::Fullscreen: return 2;
	default: return 1;
	}
}

void SQPerf::SetWindowMode(const UWorld* World, int32 Mode)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS) return;
	const FIntPoint Desk = GUS->GetDesktopResolution();
	if (Mode <= 0)
	{
		// a window no bigger than 80% of the desktop, keeping the chosen resolution if it fits
		FIntPoint Res = GUS->GetScreenResolution();
		if (Res.X <= 0 || Res.X > Desk.X * 9 / 10 || Res.Y > Desk.Y * 9 / 10)
		{
			const int32 WinW = FMath::Max(960, FMath::Min(1600, Desk.X * 4 / 5));
			Res = FIntPoint(WinW, WinW * 9 / 16);
		}
		GUS->SetFullscreenMode(EWindowMode::Windowed);
		GUS->SetScreenResolution(Res);
	}
	else if (Mode == 1)
	{
		GUS->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		GUS->SetScreenResolution(Desk);
	}
	else
	{
		GUS->SetFullscreenMode(EWindowMode::Fullscreen);
	}
	if (!World || World->WorldType != EWorldType::PIE) GUS->ApplyResolutionSettings(false);
	GUS->SaveSettings();
}

FIntPoint SQPerf::CurrentResolution()
{
	const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return GUS ? GUS->GetScreenResolution() : FIntPoint(1920, 1080);
}

void SQPerf::SetResolution(const UWorld* World, FIntPoint Res)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS || Res.X < 640 || Res.Y < 360) return;
	GUS->SetScreenResolution(Res);
	if (!World || World->WorldType != EWorldType::PIE) GUS->ApplyResolutionSettings(false);
	GUS->SaveSettings();
}
