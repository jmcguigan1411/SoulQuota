// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;
class USQSaveGame;

/**
 * Platform pass (Nov 23 2026): graphics options and the automatic quality governor.
 *
 * SETTINGS > GRAPHICS / DISPLAY write the profile; SQPerf::Apply turns it into
 *  - Unreal scalability groups (shadows, view distance, post process, effects, anti-aliasing, textures), resolution scale,
 *    frame cap and V-sync through UGameUserSettings, and
 *  - local Tuning overrides for the game's own cosmetic dials (USQData::SetLocalTune): spark pools, effect counts, area-effect
 *    layers, animated (skinned) enemies near you, impostor shadows, decor distance, fireflies, ink outlines, bloom.
 * Only cosmetic keys are overridden: nothing that changes collision, spawning or damage, so co-op machines can differ.
 * The overrides are read when a map starts, so those rows apply on the next run; the engine groups apply at once.
 *
 * The governor watches the smoothed frame time against a budget and scales two LIVE multipliers down (FxScale, HordeScale)
 * when the game runs slow, then back up when there is headroom. ASQSparks and ASQHordeManager multiply by them every frame.
 */
namespace SQPerf
{
	constexpr int32 NumLevels = 4; // LOW MEDIUM HIGH ULTRA
	constexpr uint8 PresetCustom = 4;

	/** Sets every graphics row of the profile from a preset (0..3) */
	void SetPreset(USQSaveGame* Save, uint8 Preset);
	/** Marks the profile CUSTOM if its rows no longer match its preset */
	void NotePresetEdited(USQSaveGame* Save);
	/** Pushes the profile's graphics rows to the engine and the local Tuning overrides. Safe to call often. */
	void Apply(UWorld* World, const USQSaveGame* Save);
	/** First launch: runs Unreal's hardware benchmark once and picks a preset (Tuning PerfAutoDetect 0 skips it). Returns true if it ran. */
	bool AutoDetectOnce(UWorld* World, USQSaveGame* Save);

	/** Call once a frame on the local machine with the REAL frame time */
	void GovernorTick(const UWorld* World, float RealDelta);
	/** Live multiplier on spark / effect counts: 1 normally, down to the Tuning floor PerfFxFloor under load */
	float FxScale();
	/** Live multiplier on the animated-enemy radius: 1 normally, down to PerfHordeFloor under load */
	float HordeScale();
	/** Smoothed frames per second (for the FPS counter) */
	float SmoothedFps();
	/** 0 = full quality .. 1 = fully shed (debug line) */
	float Pressure();

	/** Frame caps offered by the DISPLAY tab (0 = unlimited) and resolutions (filtered to the desktop size) */
	const TArray<int32>& FrameCaps();
	void Resolutions(TArray<FIntPoint>& Out);
	/** 0 windowed, 1 borderless, 2 fullscreen */
	int32 WindowMode();
	void SetWindowMode(const UWorld* World, int32 Mode);
	FIntPoint CurrentResolution();
	void SetResolution(const UWorld* World, FIntPoint Res);
}
