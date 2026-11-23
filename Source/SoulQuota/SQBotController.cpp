// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQBotController.h"
#include "EngineUtils.h"
#include "SQCharacter.h"
#include "SQGameState.h"
#include "SQPlayerState.h"
#include "SQHordeManager.h"
#include "SQLantern.h"
#include "SQData.h"
#include "SQArena.h"

ASQBotController::ASQBotController()
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = true;
}

void ASQBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASQCharacter* C = GetPawn<ASQCharacter>();
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	if (!C || !GS || !GS->Horde) return;

	if (ASQPlayerState* PS = GetPlayerState<ASQPlayerState>())
	{
		if (GS->Phase == ESQPhase::Shop) PS->bShopReady = true;
	}
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) return;
	if (GS->bPickPause) return; // the world is frozen while players pick level-up cards

	InteractCooldown -= DeltaSeconds;
	WanderTimer -= DeltaSeconds;

	const FVector2D Me = C->Pos2D();
	const FVector2D Lantern = GS->Lantern ? FVector2D(GS->Lantern->GetActorLocation().X, GS->Lantern->GetActorLocation().Y) : FVector2D::ZeroVector;
	FVector2D Goal = Me;

	if (C->bDowned)
	{
		Goal = Lantern; // roll towards help
	}
	else if (C->CarriedSkull)
	{
		Goal = Lantern;
	}
	else
	{
		// Rescue a downed teammate first
		ASQCharacter* Downed = nullptr;
		float BestDist = 2000.f;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			if (*It == C || !It->bDowned || It->CarriedBy) continue;
			const float Dist = FVector2D::Distance(It->Pos2D(), Me);
			if (Dist < BestDist) { BestDist = Dist; Downed = *It; }
		}

		const bool bBankTime = GS->Phase == ESQPhase::Night &&
			(C->Carried >= FMath::RoundToInt(C->Stats.CarryCap * 0.75f) || (GS->PhaseTimeLeft < 40.f && C->Carried > 0));

		FVector2D SoulPos;
		if (Downed)
		{
			Goal = Downed->Pos2D();
			if (BestDist < 250.f && InteractCooldown <= 0.f)
			{
				InteractCooldown = 1.f;
				C->ServerInteract();
			}
		}
		else if (bBankTime)
		{
			Goal = Lantern;
		}
		else if (GS->Horde->FindNearestPickup(Me, 1, 2500.f, SoulPos) && C->Carried < C->Stats.CarryCap)
		{
			Goal = SoulPos;
		}
		else
		{
			if (WanderTimer <= 0.f)
			{
				WanderTimer = FMath::FRandRange(2.f, 4.f);
				WanderTarget = Lantern + FMath::RandPointInCircle(2200.f);
				// Top-down maps: never wander into a layout wall (pushed out to its edge instead)
				if (GS->Arena) GS->Arena->ResolveWallOverlap(WanderTarget, 120.f);
			}
			Goal = WanderTarget;
		}
	}

	FVector2D Dir = (Goal - Me).GetSafeNormal();
	if (!C->bDowned)
	{
		const FVector2D Threat = GS->Horde->ComputeThreat(Me, 450.f);
		Dir = (Dir + Threat * 1.2f).GetSafeNormal();
	}
	// Top-down maps: layout walls block reapers, so bend round the wall ahead instead of running into it
	if (!Dir.IsNearlyZero() && GS->Arena && (Goal - Me).SizeSquared() > FMath::Square(150.f))
	{
		Dir = GS->Arena->SteerAroundWalls(Me, Dir, ASQHordeManager::ReaperBodyRadius(C, USQData::Get(C)), 320.f);
	}
	if (!Dir.IsNearlyZero())
	{
		C->AddMovementInput(FVector(Dir.X, Dir.Y, 0.f), 1.f);
	}

	// Top-down (TopDownMode 1): bots need no camera or cursor - they steer with AddMovementInput, face their movement
	// (ASQCharacter::UpdateFacingMode keeps orient-to-movement for non-player controllers) and dash along their move input.
	// Body blocking: a bot pinned by the crowd dashes through it (the dash passes through enemy bodies)
	if (!C->bDowned && C->DashCooldownLeft <= 0.f)
	{
		FSQBodyContacts Pinned;
		if (GS->Horde->GatherBodyContacts(Me, ASQHordeManager::ReaperBodyRadius(C, USQData::Get(C)), 12.f, C->GetFeetAboveGround(), Pinned) >= 3) C->TryDash();
	}
}
