// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQTutorial.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "SQAbility.h"
#include "SQCharacter.h"
#include "SQChest.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQHordeManager.h"
#include "SQMission.h"
#include "SQPlayerController.h"
#include "SQPlayerState.h"
#include "SQPortal.h"
#include "SQPot.h"
#include "SQReap.h"
#include "SQSave.h"
#include "SQShrine.h"
#include "SQUIStyle.h"

// File-scope names carry an SQTut prefix (unity builds merge anonymous namespaces across files).
namespace
{
	/** One Training Shift tip (Tutorial preview gen5.py: text and key caps exactly as there; ASCII only) */
	struct FSQTutTrainTip
	{
		const TCHAR* Text;
		const TCHAR* Keys[7];
		int32 NumKeys;
	};

	// Nov 12 2026 (market audit): three prompts replace the 8-step Training Shift
	const FSQTutTrainTip SQTutTrainTips[] = {
		{ TEXT("Move, look, jump, dash"), { TEXT("W"), TEXT("A"), TEXT("S"), TEXT("D"), TEXT("MOUSE"), TEXT("SPACE"), TEXT("SHIFT") }, 7 },
		{ TEXT("Weapons fire on their own. Grab green crystals to level up"), {}, 0 },
		{ TEXT("Goal: clear the objectives, ring the bell, beat the boss"), {}, 0 },
	};
	static_assert(UE_ARRAY_COUNT(SQTutTrainTips) == USQTutorial::NumTrainingTips, "3 first-run prompts");

	struct FSQTutTip
	{
		const TCHAR* Id;    // saved in the profile
		const TCHAR* Key;   // key cap ("" = none)
		const TCHAR* Text;
		/** Retired Nov 12 2026: never shown (the three prompts and the HUD cover it); kept so ESQTip values stay stable */
		bool bRetired = false;
	};

	// One line each, in ESQTip order
	const FSQTutTip SQTutTips[] = {
		{ TEXT("LevelUpSolo"), TEXT("1-3"), TEXT("Level up! Time is frozen: pick a card with 1, 2 or 3."), true },
		{ TEXT("LevelUpCoop"), TEXT("TAB"), TEXT("Level up! Press TAB (or 1/2/3) to see your cards. No pause!") },
		{ TEXT("Souls"), TEXT(""), TEXT("Souls! Your sack has a limit: carry them to the LIT lantern (follow the beam) to bank them.") },
		{ TEXT("Gold"), TEXT(""), TEXT("Gold! It opens chests. Each chest costs a bit more than the last."), true },
		{ TEXT("Chest"), TEXT("F"), TEXT("A chest! Walk up and press F to spend gold on an item.") },
		{ TEXT("Shrine"), TEXT(""), TEXT("A shrine! Read its sign, then step into the ring.") },
		{ TEXT("Pot"), TEXT(""), TEXT("Pots hold loot. Walk into them or let your weapons smash them."), true },
		{ TEXT("Bell"), TEXT("F"), TEXT("The bell is ready. Press F at its marker to call the boss.") },
		{ TEXT("FinalSwarm"), TEXT(""), TEXT("Midnight! The Final Swarm grows every minute. Ring the bell soon!"), true },
		{ TEXT("Skull"), TEXT("L-CLICK"), TEXT("A teammate is down! Reap foes near them to bring them back.") },
		{ TEXT("Ramp"), TEXT("E"), TEXT("E is unlocked! It aims itself. R needs level 6 and full souls.") },
		{ TEXT("Miniboss"), TEXT(""), TEXT("A miniboss! Take it down for gold for the whole crew."), true },
		{ TEXT("Pinned"), TEXT("SHIFT"), TEXT("Boxed in? SHIFT (or right-click) dashes through the crowd."), true },
		{ TEXT("Ghost"), TEXT("F"), TEXT("Down, not out! Drift to the crew, press F to haunt a big foe, and keep gathering XP.") },
		{ TEXT("Ping"), TEXT("M-CLICK"), TEXT("Tap the mouse wheel to ping a spot. Hold it for Help, Lantern, Chest, Danger, Thanks.") },
		{ TEXT("Combo"), TEXT("Q"), TEXT("COMBO! One reaper's ability primes foes; a teammate's hit makes them burst.") },
		{ TEXT("Event"), TEXT("M"), TEXT("An event! The box under the clock says what to do; the cross on the map shows where.") },
	};
	static_assert(UE_ARRAY_COUNT(SQTutTips) == static_cast<SIZE_T>(ESQTip::Count), "One tip text per ESQTip");

	/** Where a Training Shift card put its clickable bits */
	struct FSQTutCardLayout
	{
		float CloseX = 0.f, CloseY = 0.f, CloseS = 0.f;
		float SkipTipX = 0.f, SkipTipW = 0.f;
		float SkipAllX = 0.f, SkipAllW = 0.f;
		float LinksY = 0.f, LinksH = 0.f;
		float H = 0.f;
	};

	FLinearColor SQTutFade(const FLinearColor& Colour, float Fade)
	{
		return FLinearColor(Colour.R, Colour.G, Colour.B, Colour.A * FMath::Clamp(Fade, 0.f, 1.f));
	}

	float SQTutEaseOut(float T)
	{
		const float Clamped = FMath::Clamp(T, 0.f, 1.f);
		return 1.f - (1.f - Clamped) * (1.f - Clamped) * (1.f - Clamped);
	}

	bool SQTutMouseIn(const FSQTutorialPainter& P, float X, float Y, float W, float H)
	{
		return P.bHasMouse && P.MouseX >= X && P.MouseX <= X + W && P.MouseY >= Y && P.MouseY <= Y + H;
	}

	/** Largest size up to Px that fits MaxW */
	float SQTutFitPx(const FSQTutorialPainter& P, const FString& Str, ESQTutFont Font, float Px, float MaxW, float MinPx)
	{
		const float TW = P.TextWidth ? P.TextWidth(Str, Font, Px) : 0.f;
		return TW > MaxW && TW > 0.f ? FMath::Max(MinPx, Px * MaxW / TW) : Px;
	}

	/** A dashed line (the preview's pointer), Dash on / Gap off */
	void SQTutDashed(const FSQTutorialPainter& P, float X0, float Y0, float X1, float Y1, float Thick, const FLinearColor& Colour)
	{
		if (!P.Tilt) return;
		const float DX = X1 - X0, DY = Y1 - Y0;
		const float Len = FMath::Sqrt(DX * DX + DY * DY);
		if (Len < 2.f) return;
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
		const float Dash = 6.f, Gap = 5.f;
		for (float At = 0.f; At < Len; At += Dash + Gap)
		{
			const float SegLen = FMath::Min(Dash, Len - At);
			const float Mid = (At + SegLen * 0.5f) / Len;
			const float CX = X0 + DX * Mid, CY = Y0 + DY * Mid;
			P.Tilt(Colour, CX - SegLen * 0.5f, CY - Thick * 0.5f, SegLen, Thick, Deg);
		}
	}

	/** The training tip's text for this reaper (tip 2 names the reaper you play; "Grim" for Grim, as in the preview) */
	FString SQTutTrainText(int32 Index, const APlayerController* PC)
	{
		const int32 Safe = FMath::Clamp(Index, 0, USQTutorial::NumTrainingTips - 1);
		FString Text(SQTutTrainTips[Safe].Text);
		static_cast<void>(PC);
		return Text;
	}

	/** One Training Shift card (preview look). Hover lifts colour the links; Out returns the clickable rects. */
	float SQTutDrawTrainCard(const FSQTutorialPainter& P, int32 Index, const FString& Text, float X, float Y, float W, bool bBig, float Fade,
		float CloseLift, float SkipTipLift, float SkipAllLift, FSQTutCardLayout& Out)
	{
		const int32 Safe = FMath::Clamp(Index, 0, USQTutorial::NumTrainingTips - 1);
		const FSQTutTrainTip& Tip = SQTutTrainTips[Safe];
		const float PadX = 16.f, PadTop = 14.f, PadBottom = 12.f;
		const float HeadH = 26.f, BodyH = 34.f, LinksH = 16.f, Gap = 10.f;
		const float H = PadTop + HeadH + Gap + BodyH + Gap + LinksH + PadBottom;
		Out.H = H;
		USQTutorial::DrawGlassCard(P, X, Y, W, H, Fade);

		// header: wisp + "TRAINING SHIFT - n/8", an x on the right
		USQTutorial::DrawWisp(P, X + PadX, Y + PadTop, 24.f, Fade);
		P.Text(FString::Printf(TEXT("HOW TO PLAY - %d/%d"), Safe + 1, USQTutorial::NumTrainingTips), ESQTutFont::LabelBold, 11.f,
			X + PadX + 30.f, Y + PadTop + 6.f, SQTutFade(SQUI::Soul, Fade), 0);
		Out.CloseS = 22.f;
		Out.CloseX = X + W - PadX - Out.CloseS + 4.f;
		Out.CloseY = Y + PadTop + 1.f;
		P.Text(TEXT("x"), ESQTutFont::LabelBold, 16.f, Out.CloseX + Out.CloseS * 0.5f, Out.CloseY + 1.f,
			SQTutFade(FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, CloseLift), Fade), 1);

		// body: key caps then the line (Lilita One, big in a run)
		float BodyX = X + PadX;
		const float BodyY = Y + PadTop + HeadH + Gap;
		for (int32 KeyIdx = 0; KeyIdx < Tip.NumKeys; ++KeyIdx)
		{
			BodyX += USQTutorial::DrawSoulKey(P, Tip.Keys[KeyIdx], BodyX, BodyY, BodyH, Fade) + 6.f;
		}
		if (Tip.NumKeys > 0) BodyX += 4.f;
		const float TextPx = SQTutFitPx(P, Text, ESQTutFont::Head, bBig ? 22.f : 17.f, X + W - PadX - BodyX, 13.f);
		P.Text(Text, ESQTutFont::Head, TextPx, BodyX, BodyY + BodyH * 0.5f - 0.62f * TextPx, SQTutFade(SQUI::BoneWhite, Fade), 0);

		// links: SKIP TIP, SKIP TUTORIAL
		Out.LinksY = BodyY + BodyH + Gap;
		Out.LinksH = LinksH;
		Out.SkipTipX = X + PadX;
		Out.SkipTipW = P.Text(TEXT("SKIP TIP"), ESQTutFont::LabelBold, 12.f, Out.SkipTipX, Out.LinksY, SQTutFade(FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, SkipTipLift), Fade), 0);
		Out.SkipAllX = Out.SkipTipX + Out.SkipTipW + 16.f;
		Out.SkipAllW = P.Text(TEXT("SKIP TUTORIAL"), ESQTutFont::LabelBold, 12.f, Out.SkipAllX, Out.LinksY, SQTutFade(FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, SkipAllLift), Fade), 0);
		return H;
	}

	int32 SQTutHumans(const ASQGameState* GS)
	{
		int32 Humans = 0;
		if (GS)
		{
			for (const APlayerState* Counted : GS->PlayerArray)
			{
				if (Counted && !Counted->IsABot()) Humans++;
			}
		}
		return Humans;
	}
}

// ============================================================ lifetime / state

USQTutorial* USQTutorial::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<USQTutorial>() : nullptr;
}

void USQTutorial::SyncWorld(const UWorld* World)
{
	if (SeenWorld.Get() == World) return;
	// A new map: real time restarts, so every timer here is reset; the Training Shift never follows you into another map
	SeenWorld = World;
	bTraining = false;
	bTrainingTried = false;
	TrainStep = 0;
	StepDoneAt = -1.f;
	ActiveTips.Reset();
	TipQueue.Reset();
	ClickZones.Reset();
	LastTipAt = -100.f;
	TipsOffNoteAt = -100.f;
	NextWatchAt = 0.f;
	LastWatchNow = -1.f;
}

bool USQTutorial::ShouldHoldSpawns(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client) return false;
	const USQTutorial* Tut = Get(World);
	if (!Tut || !Tut->bTraining || Tut->SeenWorld.Get() != World) return false;
	// prompt 1 (move + look) done = its card is fading or a later prompt is up
	const bool bMoveDone = Tut->TrainStep > 0 || Tut->StepDoneAt >= 0.f;
	if (bMoveDone) return false;
	if (SQTutHumans(World->GetGameState<ASQGameState>()) > 1) return false; // co-op: nothing holds
	const USQData* D = USQData::Get(World);
	const float MaxHold = D ? FMath::Clamp(D->Tune(TEXT("TrainingHoldMaxSeconds"), 60.f), 0.f, 600.f) : 60.f;
	return static_cast<float>(World->GetRealTimeSeconds()) - Tut->TrainingStartedAt < MaxHold;
}

void USQTutorial::StartTraining(APlayerController* PC)
{
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return;
	SyncWorld(World);
	const float Now = static_cast<float>(World->GetRealTimeSeconds());
	bTraining = true;
	bTrainingTried = true;
	TrainStep = 0;
	TrainingStartedAt = Now;
	StepShownAt = Now + 1.f; // let the rise-from-the-ground play first
	StepDoneAt = -1.f;
	bStepNeedsStamp = true;
	if (USQSaveGame* Save = USQSaveGame::Load())
	{
		if (Save->bTrainingReplay)
		{
			Save->bTrainingReplay = false;
			USQSaveGame::Store(Save);
		}
	}
}

void USQTutorial::FinishTraining(bool bSkipped)
{
	if (!bTraining) return;
	bTraining = false;
	StepDoneAt = -1.f;
	if (USQSaveGame* Save = USQSaveGame::Load())
	{
		Save->bTrainingDone = true;
		Save->bTrainingReplay = false;
		USQSaveGame::Store(Save);
	}
	UE_LOG(LogTemp, Log, TEXT("SQTutorial: Training Shift %s at tip %d"), bSkipped ? TEXT("skipped") : TEXT("finished"), TrainStep + 1);
}

void USQTutorial::ArmTrainingReplay()
{
	if (USQSaveGame* Save = USQSaveGame::Load())
	{
		Save->bTrainingReplay = true;
		USQSaveGame::Store(Save);
	}
}

bool USQTutorial::TipsEnabled() const
{
	const USQSaveGame* Save = USQSaveGame::Load();
	return !Save || Save->bTutorialTips;
}

void USQTutorial::TriggerTip(ESQTip Tip)
{
	const int32 TipIdx = static_cast<int32>(Tip);
	if (TipIdx < 0 || TipIdx >= static_cast<int32>(ESQTip::Count) || !TipsEnabled()) return;
	// The Training Shift teaches level-ups itself
	if (bTraining && (Tip == ESQTip::LevelUpSolo || Tip == ESQTip::LevelUpCoop)) return;
	if (TipQueue.Contains(TipIdx)) return;
	for (const FActiveTip& Shown : ActiveTips)
	{
		if (Shown.TipIdx == TipIdx) return;
	}
	const USQSaveGame* Save = USQSaveGame::Load();
	if (SQTutTips[TipIdx].bRetired) return;
	if (Save && Save->TutorialTipsSeen.Contains(FName(SQTutTips[TipIdx].Id))) return;
	TipQueue.Add(TipIdx);
}

void USQTutorial::HideAllTips()
{
	if (USQSaveGame* Save = USQSaveGame::Load())
	{
		Save->bTutorialTips = false;
		USQSaveGame::Store(Save);
	}
	ActiveTips.Reset();
	TipQueue.Reset();
}

void USQTutorial::ResetTutorial()
{
	if (USQSaveGame* Save = USQSaveGame::Load())
	{
		Save->bTutorialSeen = false;
		Save->bTrainingDone = false;
		Save->bTutorialTips = true;
		Save->TutorialTipsSeen.Reset();
		USQSaveGame::Store(Save);
	}
	ActiveTips.Reset();
	TipQueue.Reset();
}

// ============================================================ Training Shift: starting and watching the actions

void USQTutorial::CompleteStep(float Now)
{
	if (!bTraining || StepDoneAt >= 0.f) return;
	StepDoneAt = Now;
}

void USQTutorial::WatchTraining(APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!World || !GS) return;
	const float Now = static_cast<float>(World->GetRealTimeSeconds());
	const float Dt = LastWatchNow >= 0.f ? FMath::Clamp(Now - LastWatchNow, 0.f, 0.1f) : 0.f;
	LastWatchNow = Now;
	const bool bFight = GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;

	// ---- start: this profile's first run (the first mission), or a replay armed from HOW TO PLAY
	if (!bTraining && !bTrainingTried && bFight && PC->GetPawn())
	{
		bTrainingTried = true;
		const USQSaveGame* Save = USQSaveGame::Load();
		const TArray<FSQMissionDef>& MissionDefs = USQMissions::Missions();
		const bool bFirstMap = GS->MissionId.IsNone() || MissionDefs.Num() == 0 || MissionDefs[0].Id == GS->MissionId;
		if (Save && (Save->bTrainingReplay || (!Save->bTrainingDone && bFirstMap)))
		{
			StartTraining(PC);
		}
	}
	if (!bTraining) return;

	// ---- the run ended under it: only a training that got past the reap counts as done (else it plays again next time)
	if (!bFight)
	{
		if (GS->Phase == ESQPhase::Victory || GS->Phase == ESQPhase::Defeat || GS->Phase == ESQPhase::Waiting)
		{
			if (TrainStep >= 1) FinishTraining(false);
			else bTraining = false;
		}
		return;
	}

	// ---- next tip after the fade
	if (StepDoneAt >= 0.f)
	{
		if (Now - StepDoneAt < 0.6f) return;
		TrainStep++;
		StepDoneAt = -1.f;
		bStepNeedsStamp = true;
		StepShownAt = Now;
		if (TrainStep >= NumTrainingTips)
		{
			FinishTraining(false);
			if (ASQPlayerController* SQPC = Cast<ASQPlayerController>(PC)) SQPC->ShowLocalMessage(TEXT("That's all you need. Go reap!"));
			return;
		}
	}
	if (Now < StepShownAt) return;

	ASQCharacter* Me = Cast<ASQCharacter>(PC->GetPawn());
	const ASQPlayerState* PS = PC->GetPlayerState<ASQPlayerState>();
	if (!Me || !PS || Me->bDowned) return;
	USQReapComponent* Reap = Me->Reap.Get();
	USQAbilityComponent* Kit = Me->Abilities.Get();

	// snapshot what the step compares against
	if (bStepNeedsStamp)
	{
		bStepNeedsStamp = false;
		WalkSeconds = 0.f;
		MouseTravel = 0.f;
		LastMouseX = LastMouseY = -1000.f;
		KillsAtStep = PS->Kills;
		bReapWasCooling = Reap && Reap->GetCooldownRemaining() > 0.05f;
		bDashWasCooling = Me->DashCooldownLeft > 0.f;
		bWasChoosing = PS->IsChoosing();
		UltAtStep = Kit ? Kit->GetUltCharge01() : 0.f;
		FSQAbilityView RView;
		bUltWasCooling = Kit && Kit->GetAbilityView(3, RView) && RView.Cooldown > 0.05f;
		StepCloseLift = SkipTipLift = SkipAllLift = 0.f;
	}
	const float Age = Now - StepShownAt;

	switch (TrainStep)
	{
	case 0: // move + look: walk a little and turn the chase camera a little (or time to read it)
	{
		if (Me->GetVelocity().Size2D() > 150.f) WalkSeconds += Dt;
		const float ViewX = Me->GetViewYaw();
		const float ViewY = Me->GetViewTilt();
		if (LastMouseX > -900.f) MouseTravel += FMath::Abs(FRotator::NormalizeAxis(ViewX - LastMouseX)) + FMath::Abs(ViewY - LastMouseY);
		LastMouseX = ViewX;
		LastMouseY = ViewY;
		if ((WalkSeconds >= 0.6f && MouseTravel >= 30.f) || Age >= 10.f) CompleteStep(Now);
		break;
	}
	case 1: // weapons fire themselves, crystals level you up: the first level-up pick (or time to read it)
	{
		const bool bChoosing = PS->IsChoosing();
		if (bWasChoosing && !bChoosing) CompleteStep(Now);
		if (GS->TeamLevel >= 2 && !bChoosing && Age >= 4.f) CompleteStep(Now);
		if (Age >= 30.f) CompleteStep(Now);
		bWasChoosing = bChoosing;
		break;
	}
	default: // the goal in one sentence: time to read it
		if (Age >= 8.f) CompleteStep(Now);
		break;
	}
	static_cast<void>(Reap);
	static_cast<void>(Kit);
}

// ============================================================ watching the game for first-time moments (contextual tips)

void USQTutorial::WatchGame(APlayerController* PC)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return;
	SyncWorld(World);
	WatchTraining(PC); // every frame (clicks, dashes)
	if (!TipsEnabled()) return;
	const float WatchNow = static_cast<float>(World->GetRealTimeSeconds());
	if (WatchNow < NextWatchAt) return;
	NextWatchAt = WatchNow + 0.25f;

	const ASQGameState* GS = World->GetGameState<ASQGameState>();
	const ASQPlayerState* PS = PC->GetPlayerState<ASQPlayerState>();
	const ASQCharacter* Me = Cast<ASQCharacter>(PC->GetPawn());
	const USQData* D = USQData::Get(PC);
	if (!GS || !PS) return;

	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) return;

	const int32 Humans = SQTutHumans(GS);
	if (PS->IsChoosing()) TriggerTip(Humans <= 1 ? ESQTip::LevelUpSolo : ESQTip::LevelUpCoop);
	if (PS->Gold > 0) TriggerTip(ESQTip::Gold);
	if (Me && Me->Carried > 0) TriggerTip(ESQTip::Souls);
	if (GS->bFinalSwarm) TriggerTip(ESQTip::FinalSwarm);
	if (Me && Me->bDowned) TriggerTip(ESQTip::Ghost);
	if (Me && !Me->bDowned && Humans > 1 && GS->Phase == ESQPhase::Night && GS->GetServerWorldTimeSeconds() > 45.0) TriggerTip(ESQTip::Ping);
	if (Me && Me->ComboFlashCount > 0) TriggerTip(ESQTip::Combo);
	if (!GS->EventId.IsNone()) TriggerTip(ESQTip::Event);
	if (IsValid(GS->Portal) && GS->Portal->bArmed && !GS->Portal->bRung && GS->Phase == ESQPhase::Night) TriggerTip(ESQTip::Bell);
	if (!GS->MinibossName.IsEmpty() && GS->MinibossHealthFrac > 0.f) TriggerTip(ESQTip::Miniboss);
	for (TActorIterator<ASQCharacter> It(World); It; ++It)
	{
		if (*It != Me && It->bDowned)
		{
			TriggerTip(ESQTip::Skull);
			break;
		}
	}
	if (!Me || Me->bDowned) return;

	// Things seen nearby (only living ones: an opened chest, a spent shrine or a smashed pot doesn't count)
	const FVector MyLoc = Me->GetActorLocation();
	const float SeeRange = D ? FMath::Max(100.f, D->Tune(TEXT("TutorialSeeRange"), 1800.f)) : 1800.f;
	const double SeeSq = static_cast<double>(SeeRange) * SeeRange;
	for (TActorIterator<ASQChest> It(World); It; ++It)
	{
		if (!It->bOpened && FVector::DistSquared2D(It->GetActorLocation(), MyLoc) <= SeeSq)
		{
			TriggerTip(ESQTip::Chest);
			break;
		}
	}
	for (TActorIterator<ASQShrine> It(World); It; ++It)
	{
		if (It->GetShrineState() == ESQShrineState::Idle && It->GetShrineType() != ESQShrineType::Pot
			&& FVector::DistSquared2D(It->GetActorLocation(), MyLoc) <= SeeSq)
		{
			TriggerTip(ESQTip::Shrine);
			break;
		}
	}
	for (TActorIterator<ASQPot> It(World); It; ++It)
	{
		if (!It->bSmashed && FVector::DistSquared2D(It->GetActorLocation(), MyLoc) <= SeeSq)
		{
			TriggerTip(ESQTip::Pot);
			break;
		}
	}

	// (Nov 9, top-down) the old ramp tip slot announces the E ability unlocking at team level 3
	if (GS->TeamLevel >= 3)
	{
		TriggerTip(ESQTip::Ramp);
	}

	// Pinned by the crowd: 3+ bodies touching (the dash slips through them)
	if (GS->Horde)
	{
		FSQBodyContacts Touching;
		const float MyRadius = ASQHordeManager::ReaperBodyRadius(Me, D);
		if (GS->Horde->GatherBodyContacts(Me->Pos2D(), MyRadius, 6.f, Me->GetFeetAboveGround(), Touching) >= 3) TriggerTip(ESQTip::Pinned);
	}
}

// ============================================================ shared look

float USQTutorial::SoulKeyWidth(const FSQTutorialPainter& P, const FString& KeyName, float CapH)
{
	const float Px = CapH * 0.47f;
	return FMath::Max(CapH, (P.TextWidth ? P.TextWidth(KeyName, ESQTutFont::Head, Px) : 0.f) + 20.f);
}

float USQTutorial::DrawSoulKey(const FSQTutorialPainter& P, const FString& KeyName, float X, float Y, float CapH, float Fade)
{
	// Preview key(): 8 px corners, deep face, 1 px soul rim, soul Lilita One letters
	const float Px = CapH * 0.47f;
	const float KW = SoulKeyWidth(P, KeyName, CapH);
	P.Round(SQTutFade(SQUI::Soul, Fade), X, Y, KW, CapH, 8.f);
	P.Round(SQTutFade(SQUI::PanelDeep, Fade), X + 1.5f, Y + 1.5f, KW - 3.f, CapH - 3.f, 6.5f);
	P.Text(KeyName, ESQTutFont::Head, Px, X + KW * 0.5f, Y + CapH * 0.5f - 0.62f * Px, SQTutFade(SQUI::Soul, Fade), 1);
	return KW;
}

void USQTutorial::DrawWisp(const FSQTutorialPainter& P, float X, float Y, float Size, float Fade)
{
	// The preview's outlined ghost: a dome, straight sides, a zig-zag hem, two eyes (drawn as a soul shape with a deep inset)
	const float W = Size * 0.84f;
	const float Stroke = FMath::Max(1.5f, Size * 0.085f);
	const float HemY = Y + Size * 0.86f;
	auto Shape = [&P, X, Y, W, HemY](const FLinearColor& Colour, float Inset)
	{
		P.Round(Colour, X + Inset, Y + Inset, W - 2.f * Inset, W - 2.f * Inset, (W - 2.f * Inset) * 0.5f);         // dome
		P.Round(Colour, X + Inset, Y + W * 0.5f, W - 2.f * Inset, HemY - (Y + W * 0.5f) - Inset * 0.5f, 0.f);       // body
		if (P.Tilt)
		{
			const float Tooth = (W - 2.f * Inset) / 3.f;
			for (int32 ToothIdx = 0; ToothIdx < 3; ++ToothIdx)
			{
				const float TX = X + Inset + Tooth * (ToothIdx + 0.5f);
				const float TS = Tooth * 0.72f;
				P.Tilt(Colour, TX - TS * 0.5f, HemY - TS * 0.5f - Inset * 0.5f, TS, TS, 45.f);
			}
		}
	};
	Shape(SQTutFade(SQUI::Soul, Fade), 0.f);
	Shape(SQTutFade(SQUI::PanelDeep, Fade), Stroke);
	const float EyeR = Size * 0.075f;
	P.Round(SQTutFade(SQUI::Soul, Fade), X + W * 0.33f - EyeR, Y + Size * 0.42f - EyeR, 2.f * EyeR, 2.f * EyeR, EyeR);
	P.Round(SQTutFade(SQUI::Soul, Fade), X + W * 0.67f - EyeR, Y + Size * 0.42f - EyeR, 2.f * EyeR, 2.f * EyeR, EyeR);
}

void USQTutorial::DrawGlassCard(const FSQTutorialPainter& P, float X, float Y, float W, float H, float Fade)
{
	// box-shadow 0 0 24px soul/20%: soft rings
	for (int32 Ring = 4; Ring >= 1; --Ring)
	{
		const float Grow = Ring * 4.f;
		P.Round(SQTutFade(SQUI::WithAlpha(SQUI::Soul, 0.045f * (5 - Ring)), Fade), X - Grow, Y - Grow, W + 2.f * Grow, H + 2.f * Grow, 12.f + Grow);
	}
	P.Round(SQTutFade(SQUI::WithAlpha(SQUI::Soul, 0.5f), Fade), X - 1.f, Y - 1.f, W + 2.f, H + 2.f, 13.f);       // thin rim, soul/50%
	P.Round(SQTutFade(SQUI::WithAlpha(SQUI::PanelDeep, 0.92f), Fade), X, Y, W, H, 12.f);                         // smoked glass
	P.Round(SQTutFade(FLinearColor(1.f, 1.f, 1.f, 0.03f), Fade), X + 3.f, Y + 3.f, W - 6.f, FMath::Min(H * 0.4f, 40.f), 10.f);
}

void USQTutorial::GetTrainingTip(int32 Index, FString& OutText, TArray<FString>& OutKeys)
{
	const FSQTutTrainTip& Tip = SQTutTrainTips[FMath::Clamp(Index, 0, NumTrainingTips - 1)];
	OutText = Tip.Text;
	OutKeys.Reset();
	for (int32 KeyIdx = 0; KeyIdx < Tip.NumKeys; ++KeyIdx) OutKeys.Add(Tip.Keys[KeyIdx]);
}

float USQTutorial::DrawTipCard(const FSQTutorialPainter& P, int32 Index, float X, float Y, float W, bool bBig, float Fade)
{
	FSQTutCardLayout Layout;
	return SQTutDrawTrainCard(P, Index, FString(SQTutTrainTips[FMath::Clamp(Index, 0, NumTrainingTips - 1)].Text), X, Y, W, bBig, Fade, 0.f, 0.f, 0.f, Layout);
}

// ============================================================ drawing

bool USQTutorial::Draw(const FSQTutorialPainter& P, APlayerController* PC, ESQTutView View)
{
	if (!P.Round || !P.Text || !P.TextWidth) return false;
	SyncWorld(PC ? PC->GetWorld() : nullptr);
	ClickZones.Reset();
	bool bAteClick = false;
	if (View != ESQTutView::Run)
	{
		ActiveTips.Reset();
		return false;
	}
	DrawTraining(P, PC, bAteClick);
	DrawTips(P, PC, bAteClick);
	return bAteClick;
}

void USQTutorial::DrawTraining(const FSQTutorialPainter& P, APlayerController* PC, bool& bAteClick)
{
	if (!bTraining || TrainStep >= NumTrainingTips || P.Now < StepShownAt) return;
	const float In = SQTutEaseOut((P.Now - StepShownAt) / 0.3f);
	const float Out = StepDoneAt >= 0.f ? FMath::Clamp(1.f - (P.Now - StepDoneAt) / 0.4f, 0.f, 1.f) : 1.f;
	const float Fade = In * Out;
	if (Fade <= 0.01f) return;

	// ---- where it floats: beside the reaper (above and to the right), kept on screen and above the action bar
	const float W = FMath::Min(700.f, P.ViewW - 48.f);
	const float H = 14.f + 26.f + 10.f + 34.f + 10.f + 16.f + 12.f;
	const bool bHaveReaper = P.ReaperX >= 0.f;
	const float RX = bHaveReaper ? P.ReaperX : P.ViewW * 0.5f;
	const float RY = bHaveReaper ? P.ReaperY : P.ViewH * 0.5f;
	const float TopLimit = 96.f;
	const float BottomLimit = P.ViewH - FMath::Max(24.f, P.BottomReserve + 12.f) - H;
	float X = FMath::Clamp(RX - W * 0.5f, 24.f, P.ViewW - 24.f - W);
	float Y = RY - 120.f - H;
	if (Y < TopLimit) Y = RY + 90.f; // the reaper is near the top: float below instead
	Y = FMath::Clamp(Y, TopLimit, FMath::Max(TopLimit, BottomLimit));
	Y += 14.f * (1.f - In);
	// level-up tip: next to the cards, not under them
	if (TrainStep == 1 && P.CardsX >= 0.f)
	{
		X = FMath::Clamp(P.CardsX - W * 0.5f, 24.f, P.ViewW - 24.f - W);
		Y = FMath::Clamp(P.CardsY - H - 40.f, TopLimit, FMath::Max(TopLimit, BottomLimit));
	}

	// ---- what it points at
	float TX = -1.f, TY = -1.f;
	switch (TrainStep)
	{
	case 1: if (P.CardsX >= 0.f) { TX = P.CardsX; TY = P.CardsY; } break;
	case 2: if (P.ObjectiveX >= 0.f) { TX = P.ObjectiveX; TY = P.ObjectiveY; } break;
	default: break; // move + look: no pointer (the card sits above the reaper)
	}
	if (TX >= 0.f && !(TX >= X && TX <= X + W && TY >= Y && TY <= Y + H))
	{
		// leave the card from the side facing the target
		float SX = FMath::Clamp(TX, X + 20.f, X + W - 20.f);
		float SY = TY > Y + H ? Y + H : (TY < Y ? Y : FMath::Clamp(TY, Y + 12.f, Y + H - 12.f));
		if (TY >= Y && TY <= Y + H) SX = TX < X ? X : X + W;
		const float DX = TX - SX, DY = TY - SY;
		const float Len = FMath::Sqrt(DX * DX + DY * DY);
		if (Len > 24.f)
		{
			const float Stop = (Len - 16.f) / Len; // stop short of the target
			SQTutDashed(P, SX, SY, SX + DX * Stop, SY + DY * Stop, 2.f, SQTutFade(SQUI::WithAlpha(SQUI::Soul, 0.85f), Fade));
			P.Round(SQTutFade(SQUI::WithAlpha(SQUI::Soul, 0.25f + 0.2f * FMath::Sin(P.Now * 5.f)), Fade), TX - 12.f, TY - 12.f, 24.f, 24.f, 12.f);
			P.Round(SQTutFade(SQUI::Soul, Fade), TX - 3.f, TY - 3.f, 6.f, 6.f, 3.f);
		}
	}

	// ---- the card
	FSQTutCardLayout Layout;
	SQTutDrawTrainCard(P, TrainStep, SQTutTrainText(TrainStep, PC), X, Y, W, true, Fade, StepCloseLift, SkipTipLift, SkipAllLift, Layout);
	if (StepDoneAt >= 0.f)
	{
		// done: a teal check flash over the card while it fades
		P.Round(SQTutFade(SQUI::WithAlpha(SQUI::Soul, 0.18f), Out), X, Y, W, H, 12.f);
		return;
	}

	// ---- clicks: x / SKIP TIP / SKIP TUTORIAL
	const bool bLive = Fade > 0.6f && P.bCursorVisible;
	const float LinkPad = 4.f;
	const bool bOverClose = bLive && SQTutMouseIn(P, Layout.CloseX, Layout.CloseY, Layout.CloseS, Layout.CloseS);
	const bool bOverSkipTip = bLive && SQTutMouseIn(P, Layout.SkipTipX - LinkPad, Layout.LinksY - LinkPad, Layout.SkipTipW + 2.f * LinkPad, Layout.LinksH + 2.f * LinkPad);
	const bool bOverSkipAll = bLive && SQTutMouseIn(P, Layout.SkipAllX - LinkPad, Layout.LinksY - LinkPad, Layout.SkipAllW + 2.f * LinkPad, Layout.LinksH + 2.f * LinkPad);
	StepCloseLift = FMath::FInterpTo(StepCloseLift, bOverClose ? 1.f : 0.f, P.Delta, 14.f);
	SkipTipLift = FMath::FInterpTo(SkipTipLift, bOverSkipTip ? 1.f : 0.f, P.Delta, 14.f);
	SkipAllLift = FMath::FInterpTo(SkipAllLift, bOverSkipAll ? 1.f : 0.f, P.Delta, 14.f);
	AddClickZone(Layout.CloseX, Layout.CloseY, Layout.CloseS, Layout.CloseS);
	AddClickZone(Layout.SkipTipX - LinkPad, Layout.LinksY - LinkPad, Layout.SkipTipW + 2.f * LinkPad, Layout.LinksH + 2.f * LinkPad);
	AddClickZone(Layout.SkipAllX - LinkPad, Layout.LinksY - LinkPad, Layout.SkipAllW + 2.f * LinkPad, Layout.LinksH + 2.f * LinkPad);
	if (!P.bClick || bAteClick) return;
	if (bOverClose || bOverSkipTip)
	{
		bAteClick = true;
		CompleteStep(P.Now);
		if (P.Sound) P.Sound(1);
	}
	else if (bOverSkipAll)
	{
		bAteClick = true;
		FinishTraining(true);
		if (P.Sound) P.Sound(1);
	}
}

void USQTutorial::DrawTips(const FSQTutorialPainter& P, APlayerController* PC, bool& bAteClick)
{
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const USQData* D = World ? USQData::Get(World) : nullptr;
	const float Life = D ? FMath::Clamp(D->Tune(TEXT("TutorialTipSeconds"), 10.f), 3.f, 60.f) : 10.f;

	// Promote queued tips: at most 2 on screen, a short gap between new ones, none while the Training Shift runs
	if (!TipsEnabled())
	{
		TipQueue.Reset();
	}
	else if (!bTraining && TipQueue.Num() > 0 && ActiveTips.Num() < 2 && P.Now - LastTipAt > 1.2f)
	{
		FActiveTip NewTip;
		NewTip.TipIdx = TipQueue[0];
		NewTip.ShownAt = P.Now;
		TipQueue.RemoveAt(0);
		ActiveTips.Add(NewTip);
		LastTipAt = P.Now;
		if (USQSaveGame* Save = USQSaveGame::Load())
		{
			Save->TutorialTipsSeen.AddUnique(FName(SQTutTips[NewTip.TipIdx].Id));
			USQSaveGame::Store(Save);
		}
		if (P.Sound) P.Sound(2);
	}

	// Expire: time up, or closed (fade out)
	for (FActiveTip& Each : ActiveTips)
	{
		if (Each.ClosedAt < 0.f && P.Now - Each.ShownAt > Life) Each.ClosedAt = P.Now;
	}
	ActiveTips.RemoveAll([&P](const FActiveTip& Each) { return Each.ClosedAt >= 0.f && P.Now - Each.ClosedAt > 0.3f; });

	// "Tips off" note after HIDE ALL TIPS
	const float Left = 24.f, W = 500.f, H = 56.f, Gap = 10.f;
	float Y = P.ViewH - 24.f - H;
	if (ActiveTips.Num() == 0)
	{
		const float NoteAge = P.Now - TipsOffNoteAt;
		if (NoteAge >= 0.f && NoteAge < 3.f)
		{
			const float NoteFade = FMath::Clamp((3.f - NoteAge) / 0.5f, 0.f, 1.f);
			P.Text(TEXT("Tips off. Turn them back on in SETTINGS (P)."), ESQTutFont::LabelBold, 14.f, Left, P.ViewH - 24.f - 20.f, SQTutFade(SQUI::Muted, NoteFade), 0);
		}
		return;
	}

	// Newest at the bottom, older ones above it. Same smoked-glass card as the Training Shift.
	float TopY = Y;
	for (int32 TipNo = ActiveTips.Num() - 1; TipNo >= 0; --TipNo)
	{
		FActiveTip& Tip = ActiveTips[TipNo];
		const FSQTutTip& Def = SQTutTips[FMath::Clamp(Tip.TipIdx, 0, static_cast<int32>(ESQTip::Count) - 1)];
		const float Age = P.Now - Tip.ShownAt;
		const float In = SQTutEaseOut(Age / 0.3f);
		const float Out = Tip.ClosedAt >= 0.f ? FMath::Clamp(1.f - (P.Now - Tip.ClosedAt) / 0.3f, 0.f, 1.f) : 1.f;
		const float Fade = FMath::Clamp(Age / 0.12f, 0.f, 1.f) * Out;
		const float TX = Left - 40.f * (1.f - In);

		DrawGlassCard(P, TX, Y, W, H, Fade);
		DrawWisp(P, TX + 12.f, Y + 8.f, 18.f, Fade);
		P.Text(TEXT("TIP"), ESQTutFont::LabelBold, 10.f, TX + 36.f, Y + 11.f, SQTutFade(SQUI::Soul, Fade), 0);
		// a thin timer line along the bottom
		const float LeftFrac = FMath::Clamp(1.f - Age / Life, 0.f, 1.f);
		P.Round(SQTutFade(SQUI::WithAlpha(SQUI::Soul, 0.35f), Fade), TX + 12.f, Y + H - 5.f, (W - 24.f) * LeftFrac, 2.f, 1.f);

		float TextX = TX + 12.f;
		const float RowY = Y + 28.f;
		const FString KeyName(Def.Key);
		if (!KeyName.IsEmpty())
		{
			TextX += DrawSoulKey(P, KeyName, TextX, RowY - 2.f, 22.f, Fade) + 8.f;
		}
		const FString Line(Def.Text);
		const float CloseS = 24.f;
		const float LineW = TX + W - CloseS - 18.f - TextX;
		const float LinePx = SQTutFitPx(P, Line, ESQTutFont::LabelBold, 15.f, LineW, 10.f);
		P.Text(Line, ESQTutFont::LabelBold, LinePx, TextX, RowY + 9.f - 0.62f * LinePx, SQTutFade(SQUI::BoneWhite, Fade), 0);

		// "x" dismisses this tip
		const float CloseX = TX + W - CloseS - 8.f, CloseY = Y + 6.f;
		const bool bOver = P.bCursorVisible && Tip.ClosedAt < 0.f && SQTutMouseIn(P, CloseX, CloseY, CloseS, CloseS);
		Tip.CloseLift = FMath::FInterpTo(Tip.CloseLift, bOver ? 1.f : 0.f, P.Delta, 14.f);
		P.Text(TEXT("x"), ESQTutFont::LabelBold, 15.f, CloseX + CloseS * 0.5f, CloseY + 2.f, SQTutFade(FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, Tip.CloseLift), Fade), 1);
		if (Tip.ClosedAt < 0.f) AddClickZone(CloseX, CloseY, CloseS, CloseS);
		if (bOver && P.bClick && !bAteClick)
		{
			bAteClick = true;
			Tip.ClosedAt = P.Now;
			if (P.Sound) P.Sound(1);
		}
		TopY = Y;
		Y -= H + Gap;
	}

	// HIDE ALL TIPS above the stack
	{
		const FString HideLabel(TEXT("HIDE ALL TIPS"));
		const float HideW = P.TextWidth(HideLabel, ESQTutFont::LabelBold, 12.f) + 20.f, HideH = 22.f;
		const float HideX = Left + W - HideW, HideY = TopY - HideH - 6.f;
		const bool bOver = P.bCursorVisible && SQTutMouseIn(P, HideX, HideY, HideW, HideH);
		HideAllLift = FMath::FInterpTo(HideAllLift, bOver ? 1.f : 0.f, P.Delta, 14.f);
		P.Round(SQUI::WithAlpha(bOver ? SQUI::PanelHover : SQUI::PanelDeep, 0.88f), HideX, HideY, HideW, HideH, 11.f);
		P.Text(HideLabel, ESQTutFont::LabelBold, 12.f, HideX + HideW * 0.5f, HideY + 4.f, FMath::Lerp(SQUI::Muted, SQUI::BoneWhite, HideAllLift), 1);
		AddClickZone(HideX, HideY, HideW, HideH);
		if (bOver && P.bClick && !bAteClick)
		{
			bAteClick = true;
			HideAllTips();
			TipsOffNoteAt = P.Now;
			if (P.Sound) P.Sound(1);
		}
	}
}
