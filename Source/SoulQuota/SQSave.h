// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SQSave.generated.h"

/**
 * Local (per-PC) progression. Nov 9 2026 missions: ONE meta currency, banked GOLD, kept in OvertimePay (field name kept for old profiles;
 * the old soul coins carry over 1:1). Gold earned in a mission is partly banked at its end (Tuning BankShareWin / BankShareLoss) and buys
 * Death's Office upgrades (MetaRanks) and reaper hires.
 */
UCLASS()
class SOULQUOTA_API USQSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 OvertimePay = 0;
	UPROPERTY() int32 LifetimePay = 0;
	UPROPERTY() TArray<FName> UnlockedReapers;
	UPROPERTY() TArray<FName> Contracts;
	UPROPERTY() int32 BestEndlessNight = 0;
	UPROPERTY() int32 RunsPlayed = 0;
	UPROPERTY() int32 RunsWon = 0;

	// Main menu (SQMenu.cpp): the reaper picked on the REAPERS page, SETTINGS values, last JOIN address.
	// Defaults match Tuning.csv (MasterVolume, MusicVolume, MouseSensitivity).
	UPROPERTY() FName PreferredReaper;
	UPROPERTY() float MasterVolume = 0.8f;
	UPROPERTY() float MusicVolume = 0.45f;
	UPROPERTY() float MouseSensitivity = 0.35f;
	UPROPERTY() FString LastJoinAddress;
	/** Floating damage / heal numbers in a run (toggled in game with N; on by default) */
	UPROPERTY() bool bDamageNumbers = true;
	/** Level-ups pick the rarest card straight away (no pick screen / strip). SETTINGS page; off by default. */
	UPROPERTY() bool bAutoPickLevelUps = false;

	// --- Settings (Nov 9 UI pass; P in a run / SETTINGS in the main menu, both drawn by FSQSettingsPanel; SQSettings has the ranges)
	/** Unused since the Nov 12 chase camera (was 0 = fixed overhead, 1 = free); kept so old profiles load */
	UPROPERTY() uint8 CameraMode = 0;
	/** Chase camera distance behind the reaper, cm (SQSettings::ZoomMin..ZoomMax); the mouse wheel changes it too */
	UPROPERTY() float CameraZoom = 850.f;
	/** Degrees below the horizon the chase camera starts at / Home returns to (the mouse changes it live, SQSettings::TiltMin..TiltMax) */
	UPROPERTY() float CameraTilt = 18.f;
	/** 0 = AUTO aim (nearest enemy), 1 = MANUAL aim (abilities, the reap and the facing follow the camera's crosshair) */
	UPROPERTY() uint8 AimMode = 0;
	/** Mouse up looks down */
	UPROPERTY() bool bInvertLookY = false;
	/** Settings layout version: below SQSettings::Version the camera values are from the old overhead camera and are reset once */
	UPROPERTY() int32 SettingsVersion = 0;
	/** 0 = tight follow .. 1 = very smooth (the spring arm's lag speed) */
	UPROPERTY() float CameraSmoothing = 0.7f;
	/** Unused since the Nov 12 chase camera (the overhead camera leaned toward the cursor) */
	UPROPERTY() bool bCameraLean = true;
	/** Hit / crit camera shake, 0..1 (0.5 = the original strength) */
	UPROPERTY() float ScreenShake = 0.5f;
	/** Key letters on the HUD action bar and the controls line (CONTROLS tab) */
	UPROPERTY() bool bShowKeyHints = true;

	// --- Tutorial (SQTutorial.h) ---
	/** Old Quick Start cards closed once (kept so old profiles load; the Training Shift replaced the cards on Nov 9) */
	UPROPERTY() bool bTutorialSeen = false;
	/** The Training Shift (8 tips, first run) was finished or skipped */
	UPROPERTY() bool bTrainingDone = false;
	/** HOW TO PLAY > PLAY TRAINING SHIFT: the next run plays it again (cleared when it starts) */
	UPROPERTY() bool bTrainingReplay = false;
	/** SETTINGS > TUTORIAL TIPS (one-time tips in a run); "HIDE ALL TIPS" turns it off */
	UPROPERTY() bool bTutorialTips = true;
	/** One-time tips already shown (ESQTip names, e.g. "Chest") */
	UPROPERTY() TArray<FName> TutorialTipsSeen;

	// --- Quests ("Performance Reviews", Quests.csv; SQQuests.h). Local to this PC: in co-op the server credits each reaper's
	// own events (kills, chests...) and team events (nights, bosses, banking) to everyone, and every PC keeps its own profile.
	/** Lifetime totals per quest stat (Mode Total) */
	UPROPERTY() TMap<FName, int32> QuestTotals;
	/** Best single run per quest stat (Mode Best) */
	UPROPERTY() TMap<FName, int32> QuestBests;
	UPROPERTY() TArray<FName> QuestsDone;
	/** Quests finished since the UNLOCKS page was last opened (NEW badges; shown on the end screen) */
	UPROPERTY() TArray<FName> QuestsNew;
	/** Upgrades.csv ids a quest reward has unlocked (quest-locked items stay out of level-up cards and chests until then) */
	UPROPERTY() TArray<FName> UnlockedItems;
	/** Reapers this profile has won a story run with (quest stat ReaperWins) */
	UPROPERTY() TArray<FName> ReapersWonWith;

	// --- Missions (Nov 9 2026; SQMission.h) ---
	/** Death's Office permanent upgrades: MetaUpgrades.csv id -> rank bought */
	UPROPERTY() TMap<FName, int32> MetaRanks;
	/** Missions.csv ids beaten at least once (unlocks the next mission and this one's Hard mode) */
	UPROPERTY() TArray<FName> MissionsCleared;
	/** Missions beaten on Hard */
	UPROPERTY() TArray<FName> HardCleared;
	/** Fastest win per mission (seconds; Hard wins count too) */
	UPROPERTY() TMap<FName, float> MissionBestSeconds;
	// ---- Content pass (Nov 24 2026, Docs/Content.md). ContentVersion 1 = these fields exist; older profiles load them empty.
	UPROPERTY() int32 ContentVersion = 0;
	/** The Long Haunt: most loops begun after the boss, per mission, per reaper, and per mission on Hard */
	UPROPERTY() TMap<FName, int32> LoopBestByMission;
	UPROPERTY() TMap<FName, int32> LoopBestByReaper;
	UPROPERTY() TMap<FName, int32> LoopBestHard;
	/** Death's Ledger: a rank for simply playing (time in missions + wins). Every rank pays a little banked gold; no stats. */
	UPROPERTY() int32 LedgerXP = 0;
	UPROPERTY() int32 LedgerRank = 0;
	/** Daily Haunt (content item 12; ContentVersion 2): the local date (yyyymmdd) of the last daily finished, the best kills on that date, dailies played */
	UPROPERTY() int32 DailyLastKey = 0;
	UPROPERTY() int32 DailyBestKills = 0;
	UPROPERTY() int32 DailyRuns = 0;
	/** Cosmetics.csv ids earned from quests, and the one equipped per Kind (none = the standard look) */
	UPROPERTY() TArray<FName> CosmeticsOwned;
	// ---- Run structure (Nov 25 2026, Docs/RunStructure.md): souls are the second meta currency (skins); endless records
	/** Souls banked across every run, not yet spent (the SKINS shop currency) */
	UPROPERTY() int32 SoulBank = 0;
	/** Souls banked across every run, ever (spent or not) */
	UPROPERTY() int32 SoulsEarnedTotal = 0;
	/** Cosmetics.csv Kind Skin ids bought, and the one worn per reaper (Reapers.csv id -> skin id; none = standard) */
	UPROPERTY() TArray<FName> SkinsOwned;
	UPROPERTY() TMap<FName, FName> SkinsEquipped;
	/** Endless horde records: best survival (s) and kills overall, best kills per reaper, full clears (every stage boss) */
	UPROPERTY() float EndlessBestSeconds = 0.f;
	UPROPERTY() int32 EndlessBestKills = 0;
	UPROPERTY() TMap<FName, int32> EndlessBestKillsByReaper;
	UPROPERTY() int32 RunsFullCleared = 0;
	UPROPERTY() TMap<FName, FName> CosmeticsEquipped;
	int32 LoopBest(FName MissionId) const { const int32* Found = LoopBestByMission.Find(MissionId); return Found ? *Found : 0; }
	/** The mission the next hosted run starts with (USQMissions::SetPendingMission from the menu; none = the first unlocked one) */
	UPROPERTY() FName PendingMission;
	UPROPERTY() bool bPendingHard = false;

	// --- Platform pass (Nov 23 2026): controller / accessibility / graphics. SQInput.h and SQPerf.h read these.
	/** Gamepad right stick: 0..1 (0.5 = Tuning ChaseStickTurnRate; 0 = 0.4x, 1 = 2x) */
	UPROPERTY() float StickSensitivity = 0.5f;
	/** Gamepad stick dead zone (SQSettings::DeadMin..DeadMax) */
	UPROPERTY() float StickDeadZone = 0.2f;
	/** Gamepad: the camera eases round behind the way you run while the right stick rests */
	UPROPERTY() bool bCameraAssist = true;
	UPROPERTY() bool bVibration = true;
	/** Slide: false = hold, true = press to start / press again to stop */
	UPROPERTY() bool bSlideToggle = false;
	/** RETRY / leave on the end screen need a held press (stops a mashed button skipping the results) */
	UPROPERTY() bool bHoldToConfirm = true;
	/** Whole-HUD scale (SQSettings::HudScaleMin..HudScaleMax) */
	UPROPERTY() float HudScale = 1.f;
	/** Colour-blind help: danger zones get a cross-hatch, pings and cues get a shape as well as a colour */
	UPROPERTY() bool bShapeCues = false;
	/** Smaller hit flashes and no full-screen flashes */
	UPROPERTY() bool bReduceFlash = false;
	/** Nov 25 2026: colour-blind filter for the whole game window (Slate colour-vision correction): 0 OFF, 1 PROTAN, 2 DEUTAN, 3 TRITAN */
	UPROPERTY() uint8 ColourBlindMode = 0;
	/** Strength of that filter, 0..1 */
	UPROPERTY() float ColourBlindStrength = 1.f;
	/** Nov 25 2026: corner minimap size multiplier (SQSettings::MinimapScaleMin..Max) */
	UPROPERTY() float MinimapScale = 1.f;
	/** Rebinds: SQInput action id -> key name (only changed ones are stored; missing = the default) */
	UPROPERTY() TMap<FName, FName> KeyBinds;
	UPROPERTY() TMap<FName, FName> PadBinds;
	/** Graphics: 0 LOW 1 MEDIUM 2 HIGH 3 ULTRA 4 CUSTOM (the rows below were changed by hand) */
	UPROPERTY() uint8 GfxPreset = 2;
	/** 0..3 each (LOW..ULTRA) */
	UPROPERTY() uint8 GfxEffects = 2;
	UPROPERTY() uint8 GfxHorde = 2;
	UPROPERTY() uint8 GfxShadows = 2;
	UPROPERTY() uint8 GfxViewDistance = 2;
	UPROPERTY() bool bGfxOutlines = true;
	UPROPERTY() bool bGfxBloom = true;
	/** Auto quality: sheds effect density and horde detail while the frame time stays over budget */
	UPROPERTY() bool bGfxGovernor = true;
	/** Render resolution scale 0.5..1 */
	UPROPERTY() float ResScale = 1.f;
	/** Frames per second cap (0 = none) */
	UPROPERTY() int32 FrameCap = 0;
	UPROPERTY() bool bVSync = true;
	UPROPERTY() bool bShowFps = false;
	/** The first-launch hardware check has run (it picks GfxPreset once) */
	UPROPERTY() bool bGfxDetected = false;
	/** Stable id of this profile for reconnecting to a co-op run (made once; not an account, never shown) */
	UPROPERTY() FString PlayerId;

	/** Settings values back in range (a hand-edited profile can't break the camera) */
	void SanitiseSettings();

	/**
	 * The profile. Every caller shares ONE in-memory copy (loaded from disk once per process and rooted), so the
	 * controller's soul coins, the quest tracker and the menu never overwrite each other's changes with a stale copy.
	 */
	static USQSaveGame* Load();
	static void Store(USQSaveGame* Save);
	bool HasContract(FName Biome) const { return Contracts.Contains(Biome); }
	bool HasReaper(FName Reaper) const { return UnlockedReapers.Contains(Reaper); }
	bool HasItem(FName Item) const { return UnlockedItems.Contains(Item); }
	bool HasQuest(FName Quest) const { return QuestsDone.Contains(Quest); }
	int32 QuestTotal(FName Stat) const { const int32* Found = QuestTotals.Find(Stat); return Found ? *Found : 0; }
	int32 QuestBest(FName Stat) const { const int32* Found = QuestBests.Find(Stat); return Found ? *Found : 0; }
	bool HasClearedMission(FName Mission) const { return MissionsCleared.Contains(Mission); }
	bool HasClearedHard(FName Mission) const { return HardCleared.Contains(Mission); }
	int32 MetaRank(FName Upgrade) const { const int32* Found = MetaRanks.Find(Upgrade); return Found ? FMath::Max(0, *Found) : 0; }
};

struct FSQTutorialPainter;
class UWorld;

/** Settings ranges and the live-apply helpers shared by the run (SQHUD / SQCharacter) and the main menu (SQMenu). */
namespace SQSettings
{
	// Chase camera (Nov 12 2026): distance behind the reaper (cm) and pitch below the horizon (deg; negative looks up)
	constexpr float ZoomMin = 450.f;
	constexpr float ZoomMax = 1500.f;
	constexpr float ZoomDefault = 850.f;
	constexpr float TiltMin = -20.f;
	constexpr float TiltMax = 70.f;
	constexpr float TiltDefault = 18.f;
	constexpr float SensMin = 0.05f;
	constexpr float SensMax = 1.f;
	constexpr float SensDefault = 0.35f;
	constexpr float DeadMin = 0.05f;
	constexpr float DeadMax = 0.5f;
	constexpr float HudScaleMin = 0.8f;
	constexpr float HudScaleMax = 1.35f;
	constexpr float MinimapScaleMin = 0.75f;
	constexpr float MinimapScaleMax = 2.f;
	constexpr float ResScaleMin = 0.5f;
	// 3 = platform pass (Nov 23 2026): graphics / controller / accessibility rows; older profiles get their defaults
	constexpr int32 Version = 3;

	/** Every SETTINGS value back to its default (profile progress untouched) */
	void ResetDefaults(USQSaveGame* Save);
	/** Master volume on the audio device (stays set across level changes) */
	void ApplyMasterVolume(UWorld* World, const USQSaveGame* Save);
	bool IsFullscreen();
	/** Windowed (16:9, 80% of the desktop) or borderless fullscreen; PIE only stores it */
	void ApplyDisplayMode(const UWorld* World, bool bFullscreen);
	/** Spring arm lag speed for the follow smoothing setting */
	float FollowLagSpeed(const USQSaveGame* Save);
	/** Camera shake multiplier (1 = the original strength at the default 0.5) */
	float ShakeScale(const USQSaveGame* Save);
}

/** What the settings panel did this frame (the host reacts: music volume, auto-pick RPC, closing, training replay) */
struct FSQSettingsResult
{
	bool bChanged = false;
	/** A key or button was rebound (or the binds were reset): the controller rebuilds its input mappings */
	bool bBindsChanged = false;
	bool bAudioChanged = false;
	bool bAutoPickChanged = false;
	bool bClose = false;
	bool bReplayTraining = false;
	bool bAteClick = false;
};

/**
 * The SETTINGS panel (Camera preview, Nov 9; platform pass Nov 23): tabs CAMERA / GAMEPLAY / GRAPHICS / DISPLAY / CONTROLS /
 * ACCESS / AUDIO, rows with segmented controls (any number of options; long ones cycle with arrows),
 * sliders and toggles, a note and RESET DEFAULTS / DONE. Drawn through the host's painter (SQHUD in a run, SQMenu on its
 * SETTINGS page) so it matches the screen it sits on. Changes write the shared profile at once and apply live.
 * Keys (host fills the painter): Up / Down rows (the tab strip is the row above the first), Left / Right change, Enter toggles, Tab next tab.
 */
struct FSQSettingsPanel
{
	int32 Tab = 0;
	/** -1 = the tab strip, 0.. = rows of the tab */
	int32 Focus = 0;
	/** Row whose slider is being dragged (-1 none) */
	int32 Drag = -1;
	float OpenedAt = -100.f;
	static constexpr int32 NumTabs = 7;
	float TabLift[NumTabs] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	/** CONTROLS tab: the action being rebound (-1 none) and whether it is the gamepad column */
	int32 RebindAct = -1;
	bool bRebindPad = false;
	float RebindSince = 0.f;
	/** True while the panel waits for a key: the host must not treat Esc / P / B as "close" */
	bool IsCapturingKey() const { return RebindAct >= 0; }
	float ResetLift = 0.f;
	float DoneLift = 0.f;
	float ReplayLift = 0.f;

	/** Design width / height of the panel */
	static constexpr float PanelW = 760.f;
	static float PanelH();
	/** Draws at X, Y (top-left). HeaderHint: "open / close" (run) or "back" (menu); Note: the grey line under the rows. bInRun: REPLAY restarts the training now. */
	FSQSettingsResult Draw(const FSQTutorialPainter& P, UWorld* World, float X, float Y, const FString& HeaderKey, const FString& HeaderHint,
		const FString& NoteLine1, const FString& NoteLine2, bool bInRun);
};
