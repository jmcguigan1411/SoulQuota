// Copyright 2026 Jack McGuigan. All rights reserved.
// Tutorial (Nov 9 2026): the "Training Shift" with Grim, plus one-line contextual tips.
// Nov 12 2026 (market audit): the Training Shift is now THREE prompts (move + look / weapons fire themselves, crystals level
// you up / the goal in one sentence); the horde only holds until the first is done. Seven of the 13 tips are retired
// (bRetired in SQTutorial.cpp); the rest are one line each the first time a system is met. Older notes below still
// describe the card look and the mechanics.
//
//   Training Shift  8 tips that follow the action (Tutorial preview, gen5.py). Plays once, on this profile's first run (Grim, map 1),
//                   and again after HOW TO PLAY > PLAY TRAINING SHIFT (USQSaveGame::bTrainingReplay) or SETTINGS > GAMEPLAY > REPLAY.
//                   Each tip waits until the player does the thing (walk, aim, reap, dash, pick...), then fades and the next one
//                   comes. Every tip has SKIP TIP (and its x) and SKIP TUTORIAL. The card is smoked glass with a thin soul-teal
//                   border and glow and the wisp icon; it floats beside the reaper with a dashed pointer to what it is about.
//                   Solo: the horde holds its spawns until tip 4 (the reap) is done (ShouldHoldSpawns, called by the horde).
//                   Co-op: tips show, nothing pauses or holds.
//   Tips            one line each, shown once per profile the first time something happens (chest seen, a teammate down...).
//                   Same card style, bottom-left. "x" closes one, "HIDE ALL TIPS" turns them off (SETTINGS > GAMEPLAY > TUTORIAL TIPS).
//                   They wait while the Training Shift is on screen.
//
// The tutorial has no canvas code of its own: the HUD (and the main menu, for the settings panel's key caps) hand it a painter
// (their own rounded-rect / text helpers), so it looks exactly like the screen it sits on. All coordinates are 1080p design units.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SQTutorial.generated.h"

class APlayerController;

/** Fonts the host screen provides */
enum class ESQTutFont : uint8 { Display, Head, Label, LabelBold };

/** Where the tutorial is being drawn */
enum class ESQTutView : uint8 { Menu, Lobby, Run };

/** One-time tips (shown once per profile; ids are saved by name in USQSaveGame::TutorialTipsSeen) */
enum class ESQTip : uint8
{
	LevelUpSolo, LevelUpCoop, Souls, Gold, Chest, Shrine, Pot, Bell, FinalSwarm, Skull, Ramp, Miniboss, Pinned,
	Ghost, Ping, Combo, // co-op pass, Nov 23 2026
	Event, // content item 12, Nov 24 2026
	Count
};

/** Drawing callbacks + this frame's input, filled in by the host screen (design units, top-left origin, text top at Y). */
struct FSQTutorialPainter
{
	TFunction<void(const FLinearColor& InColour, float InX, float InY, float InW, float InH, float InRadius)> Round;
	/** A rotated plain rectangle (pivot = its centre) */
	TFunction<void(const FLinearColor& InColour, float InX, float InY, float InW, float InH, float InDeg)> Tilt;
	/** Align 0 left, 1 centre, 2 right of InX. Returns the width. */
	TFunction<float(const FString& InStr, ESQTutFont InFont, float InPx, float InX, float InY, const FLinearColor& InColour, int32 InAlign)> Text;
	TFunction<float(const FString& InStr, ESQTutFont InFont, float InPx)> TextWidth;
	/** 0 page turn / tick, 1 close, 2 a tip appears */
	TFunction<void(int32 InKind)> Sound;
	float ViewW = 1920.f;
	float ViewH = 1080.f;
	float Now = 0.f;      // real seconds (keeps going while the level-up pause dilates time)
	float Delta = 0.f;
	bool bHasMouse = false;
	float MouseX = -1.f;
	float MouseY = -1.f;
	/** Left click this frame (only counted while the cursor is visible) */
	bool bClick = false;
	bool bCursorVisible = false;
	/** Left button held (slider drags) */
	bool bMouseHeld = false;
	/** Keyboard / pad navigation this frame (settings panel) */
	bool bNavUp = false;
	bool bNavDown = false;
	bool bNavLeft = false;
	bool bNavRight = false;
	bool bConfirm = false;
	bool bTabNext = false;
	bool bTabPrev = false;
	/** Back / cancel this frame (Esc, gamepad B) */
	bool bBack = false;
	/** The local player (settings panel: key capture for rebinding, device for glyphs). May be null. */
	class APlayerController* PC = nullptr;

	// ---- Training Shift anchors (screen design units; X < 0 = unknown). The HUD fills these every frame.
	float ReaperX = -1.f, ReaperY = -1.f;          // the reaper's chest on screen
	float FoeX = -1.f, FoeY = -1.f;                // nearest enemy (ripe one preferred)
	float CardsX = -1.f, CardsY = -1.f;            // level-up cards / strip
	float PortraitX = -1.f, PortraitY = -1.f;      // ultimate charge ring (portrait)
	float ObjectiveX = -1.f, ObjectiveY = -1.f;    // first objective marker / edge arrow
	/** Top of the bottom action bar (tips stay above it) */
	float BottomReserve = 0.f;
};

/** Local (per machine) tutorial state; lives for the whole game session (menu -> lobby -> run). */
UCLASS()
class SOULQUOTA_API USQTutorial : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static USQTutorial* Get(const UObject* WorldContext);

	/**
	 * Horde hook (server): true in solo while the Training Shift is running and tip 4 (reap) isn't done yet, so the player
	 * learns to move, aim and swing first. Never holds for more than Tuning TrainingHoldMaxSeconds (default 60).
	 */
	static bool ShouldHoldSpawns(const UObject* WorldContext);

	// ---- Training Shift
	/** Starts it now (in a run), from tip 1 */
	void StartTraining(APlayerController* PC);
	/** Ends it and remembers (profile) that it was done or skipped */
	void FinishTraining(bool bSkipped);
	bool IsTrainingActive() const { return bTraining; }
	/** 0-based prompt on screen (3 = finished) */
	int32 GetTrainingStep() const { return TrainStep; }
	/** HOW TO PLAY: the next run plays the Training Shift again */
	void ArmTrainingReplay();
	static constexpr int32 NumTrainingTips = 3;

	// ---- Tips
	void TriggerTip(ESQTip Tip);
	bool AnyTipVisible() const { return ActiveTips.Num() > 0 || bTraining; }
	bool TipsEnabled() const;
	/** "HIDE ALL TIPS": turns the setting off (SETTINGS can turn it back on) */
	void HideAllTips();
	/** SETTINGS / HOW TO PLAY: every tip can show again and the Training Shift plays on the next first-map run */
	void ResetTutorial();

	// ---- Per frame (local player only)
	/** In a run: starts the Training Shift on a first run, checks the current tip's action, queues first-time tips */
	void WatchGame(APlayerController* PC);
	/** Draws the Training Shift card and the tips and handles their clicks. Returns true when it used this frame's click. */
	bool Draw(const FSQTutorialPainter& P, APlayerController* PC, ESQTutView View);
	/** Design rects (X, Y, W, H) of everything clickable drawn last frame (the controller won't reap through them) */
	const TArray<FVector4f>& GetClickZones() const { return ClickZones; }

	// ---- Shared look (the settings panel and HOW TO PLAY use these too)
	/** The preview's key cap: deep face, soul-teal rim and letters. Returns its width. */
	static float DrawSoulKey(const FSQTutorialPainter& P, const FString& KeyName, float X, float Y, float CapH, float Fade);
	static float SoulKeyWidth(const FSQTutorialPainter& P, const FString& KeyName, float CapH);
	/** The little wisp (ghost) icon in soul teal, Size tall, top-left at X, Y */
	static void DrawWisp(const FSQTutorialPainter& P, float X, float Y, float Size, float Fade);
	/** Smoked-glass card: soft teal glow, thin teal rim, deep translucent body */
	static void DrawGlassCard(const FSQTutorialPainter& P, float X, float Y, float W, float H, float Fade);
	/** Text, keys and number of one Training Shift tip (Index 0..7) for HOW TO PLAY */
	static void GetTrainingTip(int32 Index, FString& OutText, TArray<FString>& OutKeys);
	/** One Training Shift card exactly as in a run, without input (HOW TO PLAY board). Returns its height. */
	static float DrawTipCard(const FSQTutorialPainter& P, int32 Index, float X, float Y, float W, bool bBig, float Fade);

private:
	struct FActiveTip
	{
		int32 TipIdx = 0;
		float ShownAt = 0.f;
		float ClosedAt = -1.f;   // >= 0 = fading out
		float CloseLift = 0.f;
	};

	void DrawTraining(const FSQTutorialPainter& P, APlayerController* PC, bool& bAteClick);
	void DrawTips(const FSQTutorialPainter& P, APlayerController* PC, bool& bAteClick);
	/** Checks the current Training Shift tip's action (local, 4 x a second is not enough for clicks: every frame) */
	void WatchTraining(APlayerController* PC);
	void CompleteStep(float Now);
	void AddClickZone(float X, float Y, float W, float H) { ClickZones.Add(FVector4f(X, Y, W, H)); }
	/** Clears per-world state when the map changes (times restart in a new world) */
	void SyncWorld(const UWorld* World);

	TWeakObjectPtr<const UWorld> SeenWorld;

	// Training Shift
	bool bTraining = false;
	bool bTrainingTried = false;
	int32 TrainStep = 0;
	float StepShownAt = -100.f;
	/** >= 0: this step is done and fading out; the next step shows 0.6 s later */
	float StepDoneAt = -1.f;
	bool bStepNeedsStamp = true;
	float TrainingStartedAt = -100.f;
	float StepCloseLift = 0.f;
	float SkipTipLift = 0.f;
	float SkipAllLift = 0.f;
	// action watching (per step)
	float WalkSeconds = 0.f;
	float MouseTravel = 0.f;
	float LastMouseX = -1.f;
	float LastMouseY = -1.f;
	int32 KillsAtStep = 0;
	bool bReapWasCooling = false;
	bool bDashWasCooling = false;
	bool bWasChoosing = false;
	float UltAtStep = 0.f;
	bool bUltWasCooling = false;
	float LastWatchNow = -1.f;

	// Tips
	TArray<FActiveTip> ActiveTips;
	TArray<int32> TipQueue;
	float LastTipAt = -100.f;
	float TipsOffNoteAt = -100.f;
	float HideAllLift = 0.f;
	float NextWatchAt = 0.f;

	TArray<FVector4f> ClickZones;
};
