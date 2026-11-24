// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQGameMode.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQPlayerState.h"
#include "SQSession.h"
#include "SQPlayerController.h"
#include "SQCharacter.h"
#include "SQHordeManager.h"
#include "SQLantern.h"
#include "SQBoss.h"
#include "SQArena.h"
#include "SQHUD.h"
#include "SQBotController.h"
#include "SQSave.h"
#include "SQChest.h"
#include "SQPortal.h"
#include "SQQuests.h"
#include "SQMission.h"
#include "SQAugments.h"
#include "SQRise.h"
#include "SQTutorial.h"

ASQGameMode::ASQGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ASQCharacter::StaticClass();
	PlayerControllerClass = ASQPlayerController::StaticClass();
	PlayerStateClass = ASQPlayerState::StaticClass();
	GameStateClass = ASQGameState::StaticClass();
	HUDClass = ASQHUD::StaticClass();
}

void ASQGameMode::BeginPlay()
{
	Super::BeginPlay();
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS) return;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (TActorIterator<ASQArena> It(GetWorld()); It; ++It) { GS->Arena = *It; break; }
	if (!GS->Arena) GS->Arena = GetWorld()->SpawnActor<ASQArena>(ASQArena::StaticClass(), FTransform::Identity, P);

	GS->Horde = GetWorld()->SpawnActor<ASQHordeManager>(ASQHordeManager::StaticClass(), FTransform::Identity, P);
	GS->Lantern = GetWorld()->SpawnActor<ASQLantern>(ASQLantern::StaticClass(), FTransform::Identity, P);
	GS->ResetTeamProgress();
	GS->Phase = ESQPhase::Waiting;
	// The mission the host picked in the menu (or the first unbeaten one); the arena switches to its biome in the lobby tick
	ApplyMissionChoice();

	const USQData* D = USQData::Get(this);
	WaitTimer = D ? D->Tune(TEXT("StartDelay"), 3.f) : 3.f;
	// Solo PLAY from the main menu (travel option SQSolo): no lobby step, the run starts as soon as the reaper is in
	if (OptionsString.Contains(TEXT("SQSolo"), ESearchCase::IgnoreCase) && (!D || D->Tune(TEXT("SoloSkipLobby"), 1.f) > 0.5f))
	{
		GS->bSkipLobby = true;
	}

	// A previous world (or a pick pause interrupted by travel) must never leave the new one frozen
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);

	// Platform pass: a listen server advertises itself (LAN beacon today, Steam lobby after the config switch; SQSession)
	if (GetNetMode() == NM_ListenServer && (!D || D->Tune(TEXT("SessionAdvertise"), 1.f) > 0.5f))
	{
		SQSession::Host(GetWorld(), 4, FString(FPlatformProcess::ComputerName()));
	}
}

void ASQGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Leaving mid pick pause (OpenLevel to the menu, travel, PIE stop): restore normal time
	if (GetWorld() && GetWorld()->GetWorldSettings())
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
	// Objective parts go with the world; nothing to destroy here
	Super::EndPlay(EndPlayReason);
}

FName ASQGameMode::PickReaper() const
{
	const USQData* D = USQData::Get(this);
	const AGameStateBase* GS = GameState;
	if (!D || D->Reapers.Num() == 0) return TEXT("Grim");
	const int32 Index = GS ? FMath::Max(0, GS->PlayerArray.Num() - 1) : 0;
	// Default reaper for a new player: only ever a free (starter) one, never a paid unlock
	TArray<int32> FreeReaperIndices;
	for (int32 ReaperIdx = 0; ReaperIdx < D->Reapers.Num(); ++ReaperIdx)
	{
		if (D->Reapers[ReaperIdx].UnlockCost <= 0) FreeReaperIndices.Add(ReaperIdx);
	}
	if (FreeReaperIndices.Num() == 0) return D->Reapers[0].Id;
	return D->Reapers[FreeReaperIndices[Index % FreeReaperIndices.Num()]].Id;
}

void ASQGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Reaper must be chosen before Super spawns the pawn
	if (NewPlayer)
	{
		if (ASQPlayerState* PS = NewPlayer->GetPlayerState<ASQPlayerState>())
		{
			PS->ReaperId = PickReaper();
			// Late joiner: remember which act they arrived in (a win only counts for them if they joined before the last act),
			// and allow exactly one reaper pick (their menu choice) even though the run is underway
			const ASQGameState* JoinGS = GetGameState<ASQGameState>();
			if (JoinGS && JoinGS->Phase != ESQPhase::Waiting)
			{
				PS->RunJoinNight = JoinGS->NightIndex;
				// Real time: the client's 0.75s pick timer can be held up by a pick pause (world time frozen, max ~25s)
				const bool bRunLive = JoinGS->Phase != ESQPhase::Victory && JoinGS->Phase != ESQPhase::Defeat;
				PS->MidRunPickDeadline = bRunLive ? GetWorld()->GetRealTimeSeconds() + 40.0 : -1.0;
			}
			else
			{
				PS->RunJoinNight = 0;
				PS->MidRunPickDeadline = -1.0;
			}
		}
	}
	// A new mid-run joiner (not someone taking a held seat back) banks catch-up picks and gold
	const bool bTakesSeat = FindHeldSeat(NewPlayer) != INDEX_NONE;
	Super::PostLogin(NewPlayer);
	if (NewPlayer && !bTakesSeat)
	{
		ASQPlayerState* CatchPS = NewPlayer->GetPlayerState<ASQPlayerState>();
		if (CatchPS && CatchPS->MidRunPickDeadline >= 0.0) GrantLateJoinCatchUp(CatchPS);
	}
	// The listen-server host's own Death's Office ranks (remote clients send theirs from ASQPlayerState::ClientInitialize)
	if (NewPlayer && NewPlayer->IsLocalController())
	{
		if (ASQPlayerState* HostPS = NewPlayer->GetPlayerState<ASQPlayerState>()) HostPS->SendMetaRanks();
	}
}

FTransform ASQGameMode::GetSpawnTransform()
{
	const ASQGameState* GS = GetGameState<ASQGameState>();
	const FVector Center = (GS && GS->Lantern) ? GS->Lantern->GetActorLocation() : FVector::ZeroVector;
	const float A = (SpawnCounter++ % 8) * (2.f * PI / 8.f);
	const float SpawnX = static_cast<float>(Center.X) + FMath::Cos(A) * 450.f;
	const float SpawnY = static_cast<float>(Center.Y) + FMath::Sin(A) * 450.f;
	// Stand on the terrain: ground height + capsule half height + a little air
	const UCapsuleComponent* Capsule = GetDefault<ASQCharacter>()->GetCapsuleComponent();
	const float CapsuleHalf = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
	return FTransform(FRotator::ZeroRotator, FVector(SpawnX, SpawnY, ASQArena::GroundZAt(this, SpawnX, SpawnY) + CapsuleHalf + 20.f));
}

FVector ASQGameMode::GetStartSpot(int32 Index) const
{
	const ASQGameState* GS = GetGameState<ASQGameState>();
	const UCapsuleComponent* Capsule = GetDefault<ASQCharacter>()->GetCapsuleComponent();
	const float CapsuleHalf = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
	if (GS && GS->Arena)
	{
		const FVector Start = GS->Arena->GetPlayerStart(Index);
		return FVector(Start.X, Start.Y, Start.Z + CapsuleHalf + 20.f);
	}
	// No arena: a ring round the lantern
	const FVector Center = (GS && GS->Lantern) ? GS->Lantern->GetActorLocation() : FVector::ZeroVector;
	const float Angle = static_cast<float>(Index % 8) * (2.f * PI / 8.f);
	const float SpotX = static_cast<float>(Center.X) + FMath::Cos(Angle) * 450.f;
	const float SpotY = static_cast<float>(Center.Y) + FMath::Sin(Angle) * 450.f;
	return FVector(SpotX, SpotY, ASQArena::GroundZAt(this, SpotX, SpotY) + CapsuleHalf + 20.f);
}

FString ASQGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	// Stable profile id for reconnecting (letters and digits only, bounded: it comes from the client)
	if (NewPlayerController)
	{
		if (ASQPlayerState* IdPS = NewPlayerController->GetPlayerState<ASQPlayerState>())
		{
			FString Id = UGameplayStatics::ParseOption(Options, TEXT("SQId")).Left(40);
			bool bClean = !Id.IsEmpty();
			for (int32 ChIdx = 0; ChIdx < Id.Len(); ++ChIdx) bClean = bClean && FChar::IsAlnum(Id[ChIdx]);
			IdPS->StableId = bClean ? Id : FString();
		}
	}
	return Result;
}

void ASQGameMode::NoticeAll(const FString& Text, const FString& Sub, const ASQPlayerState* Except)
{
	if (!GameState) return;
	for (APlayerState* Each : GameState->PlayerArray)
	{
		ASQPlayerState* EachPS = Cast<ASQPlayerState>(Each);
		if (EachPS && EachPS != Except && !EachPS->IsABot()) EachPS->ClientNotice(Text, Sub, 0, 0);
	}
}

int32 ASQGameMode::FindHeldSeat(const AController* Who) const
{
	const ASQPlayerState* WhoPS = Who ? Who->GetPlayerState<ASQPlayerState>() : nullptr;
	if (!WhoPS || WhoPS->StableId.IsEmpty()) return INDEX_NONE;
	const double RealNow = FPlatformTime::Seconds();
	for (int32 SeatIdx = 0; SeatIdx < HeldSeats.Num(); ++SeatIdx)
	{
		const FHeldSeat& Seat = HeldSeats[SeatIdx];
		const ASQBotController* SeatBot = Seat.Bot.Get();
		if (Seat.StableId == WhoPS->StableId && SeatBot && SeatBot->GetPawn() && RealNow <= Seat.ExpiresReal) return SeatIdx;
	}
	return INDEX_NONE;
}

void ASQGameMode::GrantLateJoinCatchUp(ASQPlayerState* PS)
{
	const ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!PS || !GS || !D || D->Tune(TEXT("LateJoinCatchUp"), 1.f) < 0.5f) return;
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) return;
	// Level-ups: the team level is shared, so a newcomer banks (nearly) as many picks as everyone else has had; the
	// usual pick flow (cards, auto-pick timeout) hands them out. Gold: the squad average.
	const float Share = FMath::Clamp(D->Tune(TEXT("LateJoinLevelShare"), 0.8f), 0.f, 1.f);
	const int32 Picks = FMath::Clamp(FMath::FloorToInt(static_cast<float>(GS->TeamLevel - 1) * Share), 0, 60);
	int32 GoldSum = 0, Others = 0;
	for (APlayerState* Other : GS->PlayerArray)
	{
		const ASQPlayerState* OtherPS = Cast<ASQPlayerState>(Other);
		if (!OtherPS || OtherPS == PS) continue;
		GoldSum += OtherPS->Gold;
		++Others;
	}
	PS->PendingLevelUps += Picks;
	if (Others > 0) PS->Gold = FMath::Max(PS->Gold, GoldSum / Others);
}

void ASQGameMode::Logout(AController* Exiting)
{
	// A human dropping mid-run: a stand-in bot takes over their reaper so the squad is not a body short, and the same
	// profile can take it back (RestartPlayer). In the lobby, at the end screen or without a pawn: the usual clean-up.
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	APlayerController* LeavingPC = Cast<APlayerController>(Exiting);
	ASQPlayerState* LeavingPS = LeavingPC ? LeavingPC->GetPlayerState<ASQPlayerState>() : nullptr;
	ASQCharacter* LeavingPawn = LeavingPC ? Cast<ASQCharacter>(LeavingPC->GetPawn()) : nullptr;
	const bool bRunLive = GS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss);
	const float Grace = D ? D->Tune(TEXT("ReconnectGraceSeconds"), 300.f) : 300.f;
	if (bRunLive && LeavingPS && LeavingPawn && !LeavingPC->IsLocalController() && Grace > 0.f && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		FActorSpawnParameters SeatParams;
		SeatParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASQBotController* SeatBot = GetWorld()->SpawnActor<ASQBotController>(ASQBotController::StaticClass(), FTransform::Identity, SeatParams);
		ASQPlayerState* SeatPS = SeatBot ? SeatBot->GetPlayerState<ASQPlayerState>() : nullptr;
		if (SeatBot && SeatPS)
		{
			SeatPS->ReaperId = LeavingPS->ReaperId;
			SeatPS->Gold = LeavingPS->Gold;
			SeatPS->Kills = LeavingPS->Kills;
			SeatPS->ChestsOpened = LeavingPS->ChestsOpened;
			SeatPS->OwnedAugments = LeavingPS->OwnedAugments;
			SeatPS->PendingLevelUps = LeavingPS->PendingLevelUps;
			SeatPS->PendingAugments = LeavingPS->PendingAugments;
			SeatPS->RerollsLeft = LeavingPS->RerollsLeft;
			SeatPS->RunJoinNight = LeavingPS->RunJoinNight;
			SeatPS->MetaRanks = LeavingPS->MetaRanks;
			SeatPS->StableId = LeavingPS->StableId;
			SeatPS->bHeldSeat = true;
			SeatPS->SetPlayerName(LeavingPS->GetPlayerName() + TEXT(" (away)"));
			SeatPS->SetIsABot(true);
			LeavingPC->UnPossess();
			SeatBot->Possess(LeavingPawn);
			if (!LeavingPS->StableId.IsEmpty())
			{
				HeldSeats.RemoveAll([&](const FHeldSeat& Old) { return Old.StableId == LeavingPS->StableId; });
				FHeldSeat Seat;
				Seat.StableId = LeavingPS->StableId;
				Seat.Bot = SeatBot;
				Seat.ExpiresReal = FPlatformTime::Seconds() + static_cast<double>(Grace);
				HeldSeats.Add(Seat);
			}
			NoticeAll(FString::Printf(TEXT("%s lost connection"), *LeavingPS->GetPlayerName()), TEXT("A stand-in holds their reaper until they are back"), LeavingPS);
		}
		else if (SeatBot)
		{
			SeatBot->Destroy();
		}
	}
	Super::Logout(Exiting);
}

void ASQGameMode::RestartPlayer(AController* NewPlayer)
{
	// No PlayerStart actors needed: spawn around the lantern (the run start moves everyone to the map's player starts)
	if (!NewPlayer || NewPlayer->IsPendingKillPending()) return;
	// Reconnect: take the held seat back (the reaper kept everything; the stand-in's state holds gold, augments, picks)
	const int32 SeatIdx = FindHeldSeat(NewPlayer);
	if (SeatIdx != INDEX_NONE)
	{
		ASQBotController* SeatBot = HeldSeats[SeatIdx].Bot.Get();
		APawn* SeatPawn = SeatBot ? SeatBot->GetPawn() : nullptr;
		ASQPlayerState* SeatPS = SeatBot ? SeatBot->GetPlayerState<ASQPlayerState>() : nullptr;
		ASQPlayerState* BackPS = NewPlayer->GetPlayerState<ASQPlayerState>();
		HeldSeats.RemoveAt(SeatIdx);
		if (SeatBot && SeatPawn && SeatPS && BackPS)
		{
			BackPS->ReaperId = SeatPS->ReaperId;
			BackPS->Gold = SeatPS->Gold;
			BackPS->Kills = SeatPS->Kills;
			BackPS->ChestsOpened = SeatPS->ChestsOpened;
			BackPS->OwnedAugments = SeatPS->OwnedAugments;
			BackPS->PendingLevelUps = SeatPS->PendingLevelUps;
			BackPS->PendingAugments = SeatPS->PendingAugments;
			BackPS->RerollsLeft = SeatPS->RerollsLeft;
			BackPS->RunJoinNight = SeatPS->RunJoinNight;
			BackPS->MidRunPickDeadline = -1.0; // their reaper is already chosen
			SeatBot->UnPossess();
			NewPlayer->Possess(SeatPawn);
			SeatBot->Destroy();
			NoticeAll(FString::Printf(TEXT("%s is back"), *BackPS->GetPlayerName()), TEXT("They took their reaper back from the stand-in"), nullptr);
			return;
		}
	}
	RestartPlayerAtTransform(NewPlayer, GetSpawnTransform());
}

void ASQGameMode::AddBot()
{
	// Debug helper: keep the lobby bounded (each bot is a full character + horde targeting)
	if (GameState && GameState->PlayerArray.Num() >= 8) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASQBotController* Bot = GetWorld()->SpawnActor<ASQBotController>(ASQBotController::StaticClass(), FTransform::Identity, P);
	if (!Bot) return;

	if (ASQPlayerState* PS = Bot->GetPlayerState<ASQPlayerState>())
	{
		PS->ReaperId = PickReaper();
		PS->SetPlayerName(FString::Printf(TEXT("Bot %d"), ++BotCounter));
		PS->SetIsABot(true);
	}

	ASQCharacter* Pawn = GetWorld()->SpawnActor<ASQCharacter>(ASQCharacter::StaticClass(), GetSpawnTransform(), P);
	if (Pawn) Bot->Possess(Pawn);
}

int32 ASQGameMode::CountTeam() const
{
	return GameState ? FMath::Max(1, GameState->PlayerArray.Num()) : 1;
}

// ---------------------------------------------------------------- run flow

void ASQGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;

	if (bBossDefeatedPending)
	{
		bBossDefeatedPending = false;
		HandleBossDefeated();
	}

	switch (GS->Phase)
	{
	case ESQPhase::Waiting:
	{
		// The lobby shows the chosen mission's map
		if (const FSQMissionDef* LobbyMission = GS->GetMission())
		{
			if (GS->Arena && !LobbyMission->Biome.IsNone() && (GS->Arena->BiomeId != LobbyMission->Biome || !GS->Arena->HasRolledRunSeed()) && D->FindBiome(LobbyMission->Biome))
			{
				GS->Arena->SetBiome(LobbyMission->Biome);
				PlaceLantern();
				bLobbyLanternPlaced = true;
			}
		}
		if (!bLobbyLanternPlaced && GS->Arena)
		{
			PlaceLantern();
			bLobbyLanternPlaced = true;
		}
		// Lobby: every human presses R (bots are always ready), then a short countdown
		int32 Humans = 0;
		bool bAllReady = true;
		bool bPawnsIn = true;
		for (APlayerState* PS : GS->PlayerArray)
		{
			ASQPlayerState* SQPS = Cast<ASQPlayerState>(PS);
			if (!SQPS || SQPS->IsABot()) continue;
			Humans++;
			if (GS->bSkipLobby) SQPS->bShopReady = true; // solo PLAY / RETRY: nobody has to press READY
			if (!SQPS->bShopReady) bAllReady = false;
			const APlayerController* ReadyPC = Cast<APlayerController>(SQPS->GetOwner());
			if (!ReadyPC || !ReadyPC->GetPawn()) bPawnsIn = false;
		}
		if (GS->bSkipLobby)
		{
			// A friend joined before the start (or the option was set on a co-op host): fall back to the normal lobby,
			// unless this is a RETRY (the same crew goes again)
			if (Humans > 1 && !bRetryStart)
			{
				GS->bSkipLobby = false;
				bAllReady = false;
				for (APlayerState* PS : GS->PlayerArray)
				{
					if (ASQPlayerState* SQPS = Cast<ASQPlayerState>(PS)) SQPS->bShopReady = SQPS->IsABot();
				}
			}
			else if (bPawnsIn) WaitTimer = FMath::Min(WaitTimer, FMath::Max(0.f, D->Tune(TEXT("SkipLobbyDelay"), 0.4f)));
			else bAllReady = false;
		}
		if (Humans > 0 && bAllReady)
		{
			// Solo: nobody to wait for, so the countdown is short (Nov 12 2026 market audit: quick to start)
			if (Humans == 1) WaitTimer = FMath::Min(WaitTimer, FMath::Max(0.f, D->Tune(TEXT("StartDelaySolo"), 1.f)));
			WaitTimer -= DeltaSeconds;
			GS->PhaseTimeLeft = FMath::Max(0.f, WaitTimer);
			if (WaitTimer <= 0.f)
			{
				// Everyone's ready: drop the whole crew on the map's player starts and go
				PlaceLantern();
				int32 StartIndex = 0;
				const FVector LanternAt = GS->Lantern ? GS->Lantern->GetActorLocation() : FVector::ZeroVector;
				for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
				{
					APlayerController* StartPC = It->Get();
					ASQCharacter* StartChar = StartPC ? StartPC->GetPawn<ASQCharacter>() : nullptr;
					if (!StartChar) continue;
					const FVector StartSpot = GetStartSpot(StartIndex++);
					const FVector FromLantern = (StartSpot - LanternAt).GetSafeNormal2D();
					StartChar->TeleportTo(StartSpot, FromLantern.IsNearlyZero() ? FRotator::ZeroRotator : FromLantern.Rotation(), false, true);
					if (UCharacterMovementComponent* StartMove = StartChar->GetCharacterMovement()) StartMove->StopMovementImmediately();
					StartChar->MulticastRunStart();
				}
				for (TActorIterator<ASQCharacter> BotIt(GetWorld()); BotIt; ++BotIt)
				{
					ASQCharacter* BotChar = *BotIt;
					if (!BotChar || (BotChar->GetController() && BotChar->GetController()->IsPlayerController())) continue;
					BotChar->TeleportTo(GetStartSpot(StartIndex++), BotChar->GetActorRotation(), false, true);
					BotChar->MulticastRunStart();
				}
				StartStage();
				GS->bSkipLobby = false;
				bRetryStart = false;
				// Reap core: every reaper rises from the grave at its start spot (VFX agent: ASQRiseFx), no input / damage meanwhile.
				// After StartStage (it revives / resets reapers), so nothing clears the lock.
				for (TActorIterator<ASQCharacter> RiseIt(GetWorld()); RiseIt; ++RiseIt)
				{
					ASQCharacter* RiseChar = *RiseIt;
					if (!RiseChar) continue;
					const float RiseSeconds = ASQRiseFx::PlayRise(RiseChar);
					RiseChar->StartRiseLock(RiseSeconds);
				}
			}
		}
		else
		{
			WaitTimer = D->Tune(TEXT("StartDelay"), 3.f);
			GS->PhaseTimeLeft = -1.f;
		}
		break;
	}

	case ESQPhase::Night:
	{
		const float ActLength = FMath::Max(1.f, GS->StageDuration / 3.f);
		// Solo Training Shift (UI agent's tutorial): while the horde holds back for the Reap lesson, the stage clock, objectives and
		// timeline beats wait too, so the early curve and the first objective start when the lesson is done
		if (USQTutorial::ShouldHoldSpawns(this))
		{
			CheckWipe();
			break;
		}
		if (!GS->bFinalSwarm)
		{
			GS->StageTime += DeltaSeconds;
			const int32 Act = FMath::Clamp(FMath::FloorToInt(GS->StageTime / ActLength), 0, 2);
			if (Act != GS->NightIndex) SetAct(Act);
			GS->PhaseDuration = ActLength;
			GS->PhaseTimeLeft = FMath::Max(0.f, static_cast<float>(Act + 1) * ActLength - GS->StageTime);
			if (GS->StageTime >= GS->StageDuration) BeginFinalSwarm();
		}
		else
		{
			// Final Swarm: the stage clock stays at zero; the swarm escalates until the bell is rung and the boss beaten
			GS->FinalSwarmSeconds += DeltaSeconds;
			GS->PhaseTimeLeft = 0.f;
		}
		GS->bFinalCall = false;
		Director.Tick(DeltaSeconds, GS->StageTime);
		TickTimeline(GS);
		// Probation clause: the held XP is paid out once its time comes
		if (GS->HeldXP > 0 && (GS->bFinalSwarm || GS->StageProgress() >= GS->ModMax(&FSQModifierDef::HoldXP)))
		{
			const int32 HeldNow = GS->HeldXP;
			GS->HeldXP = 0;
			GS->AddTeamXP(HeldNow);
		}
		// Every objective resolved: the bell rises (the boss is summoned when the crew is ready)
		if (!GS->bBossReady && Director.IsRunning() && Director.AllResolved()) RaiseBell();
		if (IsValid(GS->Portal) && !GS->Portal->bRung) GS->Portal->bArmed = GS->bBossReady;
		if (GS->Phase == ESQPhase::Night) CheckWipe();
		break;
	}

	case ESQPhase::Boss:
		// A boss rung in overtime keeps the Final Swarm going (and growing) through the fight
		if (GS->bFinalSwarm) GS->FinalSwarmSeconds += DeltaSeconds;
		Director.Tick(DeltaSeconds, GS->StageTime);
		// Safety: a boss that failed to spawn or vanished without dying (fell out of the world...) would stall the run forever
		if (!bBossDefeatedPending && !IsValid(GS->Boss)) bBossDefeatedPending = true;
		else CheckWipe();
		break;

	case ESQPhase::Shop:
		// No between-nights office any more: never entered (old saves / cheats): carry on with the stage
		GS->Phase = ESQPhase::Night;
		break;

	case ESQPhase::Victory:
	case ESQPhase::Defeat:
		GS->PhaseTimeLeft -= DeltaSeconds;
		if (GS->PhaseTimeLeft <= 0.f) ResetRun();
		break;
	}

	// Souls banked at the lantern turn into gold for the crew (shared purse); the Bank Souls objective counts them too
	if (GS->Phase == ESQPhase::Night)
	{
		const int32 NewlyBanked = GS->Banked - LastBanked;
		if (NewlyBanked > 0)
		{
			if (GS->Horde) GS->Horde->GiveGold(nullptr, static_cast<float>(NewlyBanked) * FMath::Max(0.f, D->Tune(TEXT("SoulGold"), 0.25f)) * GS->GoldMultiplier());
			CreditAll(GS, TEXT("SoulsBanked"), NewlyBanked);
		}
	}
	LastBanked = GS->Banked;

	if (GS->Horde) GS->EnemyCount = GS->Horde->NumAlive();
	UpdatePickPause(GS, D, DeltaSeconds);

	// Quest events, bots buying chest rolls, and the quest progress batch to every PC
	if (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss)
	{
		TickQuestTracking(GS, D, DeltaSeconds);
		TickBotChests(GS);
	}
	QuestFlushTimer -= DeltaSeconds;
	if (QuestFlushTimer <= 0.f)
	{
		QuestFlushTimer = FMath::Max(0.25f, D->Tune(TEXT("QuestFlushSeconds"), 2.f));
		for (APlayerState* FlushPS : GS->PlayerArray)
		{
			if (ASQPlayerState* FlushSQ = Cast<ASQPlayerState>(FlushPS)) FlushSQ->FlushQuests();
		}
	}
}

void ASQGameMode::UpdatePickPause(ASQGameState* GS, const USQData* D, float DeltaSeconds)
{
	// Solo (one human; bots never count): Megabonk-style, the world freezes while the cards are up. Everything that
	// runs on world DeltaSeconds (phase clock, horde, boss, timers, movement) stops under the dilation; the cap
	// below runs on real time because world time barely moves.
	// Co-op (2+ humans): never freeze. Each reaper banks level-ups and picks from a small strip while playing
	// (HUD DrawLevelUpStrip); here: a short protective bubble while the strip is open, and the idle auto-pick.
	const bool bActionPhase = GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss;
	const double RealNow = FPlatformTime::Seconds();
	const float PauseCap = FMath::Max(1.f, D->Tune(TEXT("PickPauseMaxSeconds"), 25.f));
	int32 Humans = 0;
	for (APlayerState* Counted : GS->PlayerArray)
	{
		if (Counted && !Counted->IsABot()) Humans++;
	}
	const bool bSoloPicks = Humans <= 1;
	const float AutoPickSeconds = FMath::Max(1.f, D->Tune(TEXT("PickAutoSeconds"), 20.f));
	const float BubbleSeconds = FMath::Clamp(D->Tune(TEXT("PickBubbleSeconds"), 1.5f), 0.f, 1.5f); // never more than 1.5 s per pick
	const float BubbleCooldown = FMath::Max(BubbleSeconds, D->Tune(TEXT("PickBubbleCooldown"), 3.f));

	int32 Choosing = 0;
	for (APlayerState* Each : GS->PlayerArray)
	{
		ASQPlayerState* SQPS = Cast<ASQPlayerState>(Each);
		if (!SQPS) continue;
		// A level-up / augment pick that arrived while the pawn was missing never rolled its cards: roll them now
		if (bActionPhase && (SQPS->PendingLevelUps > 0 || SQPS->PendingAugments > 0) && SQPS->Offers.Num() == 0 && SQPS->GetPawn())
		{
			SQPS->GenerateOffers();
		}
		if (!SQPS->IsABot() && SQPS->IsChoosing()) Choosing++;

		// Co-op: a set left untouched for PickAutoSeconds takes its rarest card (keeps chained picks flowing)
		if (!bSoloPicks && bActionPhase && !SQPS->IsABot() && SQPS->IsChoosing())
		{
			SQPS->OfferIdleSeconds += DeltaSeconds;
			if (SQPS->OfferIdleSeconds >= AutoPickSeconds) SQPS->ChooseOffer(FMath::Max(0, SQPS->BestOfferIndex()));
		}
		// Co-op: brief invulnerability while the strip is open (capped per pick, with a cooldown so picks can't chain it)
		if (ASQCharacter* Picker = SQPS->GetPawn<ASQCharacter>())
		{
			const bool bBubbleWanted = !bSoloPicks && bActionPhase && !SQPS->IsABot() && SQPS->bPickStripOpen && SQPS->IsChoosing();
			Picker->ServerTickPickBubble(bBubbleWanted, SQPS->OfferSerial, DeltaSeconds, BubbleSeconds, BubbleCooldown);
		}
	}

	bool bWantPause = bActionPhase && bSoloPicks && Choosing > 0;
	if (bWantPause && !GS->bPickPause)
	{
		PickPauseStartReal = RealNow;
		GS->bPickPause = true;
	}

	if (GS->bPickPause && bWantPause)
	{
		const float Elapsed = static_cast<float>(RealNow - PickPauseStartReal);
		GS->PickPauseTimeLeft = FMath::Max(0.f, PauseCap - Elapsed);
		if (Elapsed >= PauseCap)
		{
			// Time's up: everyone still choosing takes their first card (all their pending picks) and the night resumes
			for (APlayerState* Each : GS->PlayerArray)
			{
				ASQPlayerState* SQPS = Cast<ASQPlayerState>(Each);
				int32 Guard = 0;
				while (SQPS && !SQPS->IsABot() && SQPS->IsChoosing() && Guard++ < 32)
				{
					SQPS->ChooseOffer(0);
				}
			}
			bWantPause = false;
		}
	}

	// Who is in this pause (joins when they start choosing), and how many have finished
	int32 Total = 0;
	int32 Done = 0;
	for (APlayerState* Each : GS->PlayerArray)
	{
		ASQPlayerState* SQPS = Cast<ASQPlayerState>(Each);
		if (!SQPS) continue;
		if (!bWantPause || SQPS->IsABot())
		{
			SQPS->bInPickPause = false;
			continue;
		}
		if (SQPS->IsChoosing()) SQPS->bInPickPause = true;
		if (SQPS->bInPickPause)
		{
			Total++;
			if (!SQPS->IsChoosing()) Done++;
		}
	}
	GS->PickersTotal = Total;
	GS->PickersDone = Done;

	if (!bWantPause)
	{
		GS->bPickPause = false;
		GS->PickPauseTimeLeft = 0.f;
	}

	// Safety: re-asserted every tick, so the freeze can never stick once nobody is choosing
	const float WantDilation = GS->bPickPause ? 0.0001f : 1.f;
	if (!FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(this), WantDilation, 0.00001f))
	{
		UGameplayStatics::SetGlobalTimeDilation(this, WantDilation);
	}
}


// ---------------------------------------------------------------- mission stage

void ASQGameMode::StartStage()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;
	if (!GS->GetMission()) ApplyMissionChoice();
	const FSQMissionDef* Mission = GS->GetMission();

	const int32 Players = CountTeam();
	// Release length x the test knob (NightSeconds / BalanceNightSeconds) x the Overbooked clause
	const float BaseSeconds = Mission ? Mission->Duration : 1080.f;
	GS->StageDuration = FMath::Max(30.f, BaseSeconds * GS->NightScale() * GS->ModProduct(&FSQModifierDef::NightMult));
	GS->StageTime = 0.f;
	GS->NightIndex = 0;
	GS->PhaseDuration = GS->StageDuration / 3.f;
	GS->PhaseTimeLeft = GS->PhaseDuration;
	GS->bFinalSwarm = false;
	GS->FinalSwarmSeconds = 0.f;
	GS->BossNightProgress = 1.f;
	GS->MinibossName.Reset();
	GS->MinibossHealthFrac = 0.f;
	GS->Banked = 0;
	GS->Quota = 0;
	GS->Defiance = 0;
	GS->bFinalCall = false;
	GS->bBossReady = false;
	GS->RunResult = ESQRunResult::None;
	GS->RunStats = FSQRunStats();
	GS->StoryText.Reset();
	LastBanked = 0;
	BossFightStart = 0.f;
	BeatsFired.Init(false, SQQuests::Timeline().Num());

	// A new run: placement seed, run clocks, quest run counters on every PC (TeamLevel starts at 1)
	RunSeed = FMath::Rand() | 1;
	RunStartTime = GetWorld()->GetTimeSeconds();
	bRunAnyoneDowned = false;
	SkullCarriers.Reset();
	const int32 BaseRerolls = FMath::Max(0, FMath::RoundToInt(D->Tune(TEXT("RerollsPerRun"), 2.f)));
	for (APlayerState* RunPS : GS->PlayerArray)
	{
		ASQPlayerState* RunSQ = Cast<ASQPlayerState>(RunPS);
		if (!RunSQ) continue;
		if (!RunSQ->IsABot() && Cast<APlayerController>(RunSQ->GetOwner())) RunSQ->ClientQuestRunStart();
		RunSQ->CreditQuest(TEXT("TeamLevel"), FMath::Max(1, GS->TeamLevel));
		RunSQ->bTrackHitThisNight = false;
		RunSQ->TrackLastHealth = -1.f;
		RunSQ->bShopReady = false;
		// Death's Office: extra rerolls and free starting picks
		RunSQ->RerollsLeft = BaseRerolls + FMath::RoundToInt(RunSQ->GetMetaValue(TEXT("Rerolls")));
		const int32 StartPicks = FMath::Clamp(FMath::RoundToInt(RunSQ->GetMetaValue(TEXT("StartLevels"))), 0, 10);
		for (int32 PickIdx = 0; PickIdx < StartPicks; ++PickIdx) RunSQ->GrantLevelUp();
	}

	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* C = *It;
		if (C->bDowned) C->Revive(D->Tune(TEXT("ReviveFraction"), 0.5f));
		C->ReleaseSkull();
		// Solo: going down is death (Jack, Nov 7) - no free Pardon (Tuning SoloStartPardons 0); Death's Pardon (meta) gives one
		const ASQPlayerState* PardonPS = C->GetPlayerState<ASQPlayerState>();
		const int32 MetaPardons = PardonPS ? FMath::RoundToInt(PardonPS->GetMetaValue(TEXT("Pardon"))) : 0;
		const int32 SoloPardons = (Players <= 1) ? FMath::Max(0, FMath::RoundToInt(D->Tune(TEXT("SoloStartPardons"), 0.f))) : 0;
		C->Pardons = FMath::Clamp(SoloPardons + MetaPardons, 0, 9);
	}

	if (IsValid(GS->Portal)) GS->Portal->Destroy();
	GS->Portal = nullptr;
	GS->Phase = ESQPhase::Night;
	Director.Begin(GetWorld(), Mission, GS->bHardMode, GS->StageDuration);
	PlaceChests(0);
	const FSQBiomeDef* Biome = D->FindBiome(CurrentBiome());
	if (Director.HasTrail())
	{
		GS->PushAlert(FString::Printf(TEXT("%s%s - bank souls at the lit lantern: each one you fill lights the next across the map. %s"), Mission ? *Mission->Name : TEXT("Mission"),
			GS->bHardMode ? TEXT(" (HARD)") : TEXT(""), (Biome && !Biome->GimmickText.IsEmpty()) ? *Biome->GimmickText : TEXT("")));
	}
	else
	{
		GS->PushAlert(FString::Printf(TEXT("%s%s - %d objectives, then the boss. %s"), Mission ? *Mission->Name : TEXT("Mission"), GS->bHardMode ? TEXT(" (HARD)") : TEXT(""),
			Director.NumObjectives(), (Biome && !Biome->GimmickText.IsEmpty()) ? *Biome->GimmickText : TEXT("")));
	}
}

void ASQGameMode::SetAct(int32 Act)
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS) return;
	GS->NightIndex = FMath::Clamp(Act, 0, 2);
	// Act swarms re-arm (their At is a share of the act); Final Swarm tier announcements are kept
	const TArray<FSQTimelineBeat>& Beats = SQQuests::Timeline();
	if (BeatsFired.Num() != Beats.Num()) BeatsFired.Init(false, Beats.Num());
	for (int32 BeatIdx = 0; BeatIdx < Beats.Num(); ++BeatIdx)
	{
		if (Beats[BeatIdx].Kind != ESQBeatKind::FinalSwarm) BeatsFired[BeatIdx] = false;
	}
	PlaceChests(GS->NightIndex);
	GS->PushAlert(FString::Printf(TEXT("ACT %d of 3: the dead grow stronger."), GS->NightIndex + 1));
}

void ASQGameMode::RaiseBell()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D || GS->bBossReady) return;
	GS->bBossReady = true;
	if (!IsValid(GS->Portal))
	{
		ASQArena* Arena = GS->Arena.Get();
		const FVector LanternAt = GS->Lantern ? GS->Lantern->GetActorLocation() : FVector::ZeroVector;
		FVector BellAt = LanternAt + FVector(FMath::Max(600.f, D->Tune(TEXT("PortalMinLanternDist"), 2200.f)), 0.f, 0.f);
		TArray<FVector> BossSpots;
		if (Arena && Arena->GetObjectiveSpots(TEXT("Boss"), 1, BossSpots) && BossSpots.Num() > 0)
		{
			BellAt = BossSpots[0];
		}
		else
		{
			// No boss arena on the map: far from the lantern on clear ground (deterministic per run)
			FRandomStream Stream(RunSeed + 7919 + static_cast<int32>(GetTypeHash(CurrentBiome()) & 0xFFFF));
			const float Half = Arena ? Arena->GetArenaHalf() : D->Tune(TEXT("ArenaHalfSize"), 6000.f);
			const float MinLantern = FMath::Clamp(D->Tune(TEXT("PortalMinLanternDist"), 2200.f), 0.f, Half);
			const float Angle = Stream.FRandRange(0.f, 2.f * PI);
			const float Dist = Stream.FRandRange(MinLantern, FMath::Max(MinLantern + 1.f, Half * 0.8f));
			const float CandX = FMath::Clamp(static_cast<float>(LanternAt.X) + FMath::Cos(Angle) * Dist, -Half * 0.85f, Half * 0.85f);
			const float CandY = FMath::Clamp(static_cast<float>(LanternAt.Y) + FMath::Sin(Angle) * Dist, -Half * 0.85f, Half * 0.85f);
			BellAt = Arena ? Arena->FindLanternSpot(CandX, CandY, 300.f) : FVector(CandX, CandY, ASQArena::GroundZAt(this, CandX, CandY));
		}
		GS->Portal = ASQPortal::SpawnPortal(GetWorld(), BellAt);
	}
	if (IsValid(GS->Portal)) GS->Portal->bArmed = true;
	const int32 Failed = Director.NumFailed();
	GS->PushBeat(Failed > 0 ? FString::Printf(TEXT("The Appointment Bell has risen. Ring it (interact) to face the boss - it carries DEFIANCE x%d."), GS->Defiance)
		: FString(TEXT("Trail walked, objectives done! The Appointment Bell has risen. Ring it (interact) to face the boss.")), 4);
}

void ASQGameMode::RingBell(ASQCharacter* By)
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS || GS->Phase != ESQPhase::Night || !GS->bBossReady) return;
	if (IsValid(GS->Portal) && GS->Portal->bRung) return;
	StartBossFight(By);
}

void ASQGameMode::BeginFinalSwarm()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS || GS->bFinalSwarm) return;
	GS->bFinalSwarm = true;
	GS->FinalSwarmSeconds = 0.f;
	GS->PhaseTimeLeft = 0.f;
	GS->StageTime = GS->StageDuration;
	GS->bFinalCall = false;
	// The timeline is over: what is left fails (Defiance) and the bell rises anyway
	Director.FailRemaining();
	RaiseBell();
	// The tier at minute 0 announces itself (TickTimeline); this alert covers a Timeline.csv without one
	GS->PushAlert(TEXT("TIME'S UP. The Final Swarm is coming - ring the Appointment Bell (interact) to face the boss!"));
}

void ASQGameMode::StartBossFight(ASQCharacter* RungBy)
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D || GS->Phase != ESQPhase::Night) return;

	// The act's ramps (enemy HP / damage, spawn rate) hold where the act was when the bell rang
	GS->BossNightProgress = GS->NightProgress();
	if (!GS->bFinalSwarm && GS->StageDuration - GS->StageTime >= 180.f * GS->NightScale()) CreditAll(GS, TEXT("EarlyBell"));
	GS->bFinalCall = false;

	// The bell banks every soul the crew still carries (as gold): the boss fight has no lantern trips
	const float SoulGold = FMath::Max(0.f, D->Tune(TEXT("SoulGold"), 0.25f));
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		if (It->Carried > 0 && GS->Horde) GS->Horde->GiveGold(*It, static_cast<float>(It->Carried) * SoulGold * GS->GoldMultiplier());
		It->LoseCarried(It->Carried, false);
		It->BankProgress = 0.f;
	}
	if (GS->Horde) GS->Horde->ClearSoulPickups();
	if (IsValid(GS->Portal))
	{
		GS->Portal->bRung = true;
		GS->Portal->bArmed = false;
	}

	const FSQMissionDef* Mission = GS->GetMission();
	const FSQBossDef* Def = (Mission && D->FindBoss(Mission->Boss)) ? D->FindBoss(Mission->Boss) : PickBossForNight(2);
	ASQBoss* Boss = nullptr;
	if (Def)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		// The boss rises beside the bell (or the lantern without one)
		const FVector Center = IsValid(GS->Portal) ? GS->Portal->GetActorLocation() : (GS->Lantern ? GS->Lantern->GetActorLocation() : FVector::ZeroVector);
		const float BossX = static_cast<float>(Center.X) + 900.f;
		const float BossY = static_cast<float>(Center.Y);
		const FVector Loc(BossX, BossY, ASQArena::GroundZAt(this, BossX, BossY) + 300.f);
		Boss = GetWorld()->SpawnActor<ASQBoss>(ASQBoss::StaticClass(), Loc, FRotator::ZeroRotator, P);
	}
	if (!Def || !Boss)
	{
		// No boss = no way to finish the mission: count it as won rather than stall the run
		UE_LOG(LogSQ, Warning, TEXT("Mission boss missing (%s): the mission ends as a win."), Mission ? *Mission->Boss.ToString() : TEXT("no mission"));
		EndRun(true);
		return;
	}
	// Bosses.csv Health x co-op x Defiance (failed objectives) x the test length x EconomyBossHealth3 x the mission's (and Hard mode's) BossHealthMult
	Boss->InitBoss(Def->Id, GS->Defiance, CountTeam(), GS->NightScale() * GS->EconomyHealthMult(true));
	GS->Boss = Boss;
	BossFightStart = GetWorld()->GetTimeSeconds();
	const FString DefianceText = GS->Defiance > 0 ? FString::Printf(TEXT(" Failed objectives: DEFIANCE x%d!"), GS->Defiance) : FString(TEXT(""));
	const FString Who = (RungBy && RungBy->GetPlayerState()) ? RungBy->GetPlayerState()->GetPlayerName() : FString(TEXT("Someone"));
	GS->PushAlert(FString::Printf(TEXT("%s rang the Appointment Bell. %s rises: \"%s\"%s"), *Who, *Def->Name, *Def->IntroLine, *DefianceText));
	GS->Phase = ESQPhase::Boss;
}

void ASQGameMode::OnBossDefeated(ASQBoss* Boss)
{
	// Called from inside the horde simulation: defer to the next GameMode tick
	bBossDefeatedPending = true;
}

void ASQGameMode::HandleBossDefeated()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;
	// Only a live boss fight can be won (a kill landing after a wipe / run end must not start a 2nd EndRun)
	if (GS->Phase != ESQPhase::Boss) return;

	if (IsValid(GS->Boss))
	{
		if (const FSQBossDef* Def = D->FindBoss(GS->Boss->BossId))
		{
			GS->StoryText = Def->ContractText;
		}
	}
	GS->Boss = nullptr;

	// The boss's spoils: every pickup on the map flies to the crew (gold counts towards the bank), every reaper gets the boss purse
	if (GS->Horde) GS->Horde->VacuumPickups();
	const float FightSeconds = GetWorld()->GetTimeSeconds() - BossFightStart;
	const int32 BossGold = FMath::RoundToInt(FMath::Max(0.f, D->Tune(TEXT("BossGold"), 30.f)) * GS->GoldNightGrowth() * GS->GoldMultiplier());
	for (APlayerState* SpoilPS : GS->PlayerArray)
	{
		ASQPlayerState* SpoilSQ = Cast<ASQPlayerState>(SpoilPS);
		if (!SpoilSQ) continue;
		SpoilSQ->AddGold(BossGold);
		const ASQCharacter* SpoilChar = SpoilSQ->GetPawn<ASQCharacter>();
		if (SpoilChar && !SpoilChar->bDowned && SpoilChar->Health < SpoilChar->Stats.MaxHealth * 0.1f) SpoilSQ->CreditQuest(TEXT("LowHPNight"));
	}
	CreditAll(GS, TEXT("BossKills"));
	if (FightSeconds <= D->Tune(TEXT("BossFastSeconds"), 120.f)) CreditAll(GS, TEXT("BossFast"));
	if (FightSeconds <= D->Tune(TEXT("BossBlitzSeconds"), 60.f)) CreditAll(GS, TEXT("BossBlitz"));
	if (GS->Defiance <= 0) CreditAll(GS, TEXT("BossNoDefiance"));
	if (GS->bFinalSwarm)
	{
		CreditAll(GS, TEXT("SwarmBoss"));
		CreditAll(GS, TEXT("SwarmSeconds"), FMath::FloorToInt(GS->FinalSwarmSeconds));
	}
	GS->MinibossName.Reset();
	// Beat the boss = the mission is won
	EndRun(true);
}

bool ASQGameMode::TryBuy(ASQPlayerState* Buyer, int32 Index)
{
	// The between-nights shop is retired (Shop.csv empty): Death's Office sells permanent upgrades from the main menu now
	if (Buyer && !Buyer->IsABot()) Buyer->ClientNotice(TEXT("DEATH'S OFFICE MOVED"), TEXT("Permanent upgrades are bought from the main menu between missions."), 0, 0);
	return false;
}

void ASQGameMode::CheckWipe()
{
	// One rule covers solo and co-op: the run is over the moment no reaper is standing (a pending Pardon counts as
	// standing). Solo (one reaper, no bots): going down = game over at once. Co-op (2+ reapers, humans or bots -
	// a bot crew counts as co-op for testing; bots carry skulls): a downed reaper is a skull until revived, and if
	// everyone is down at the same time the run ends.
	int32 Total = 0;
	int32 Out = 0;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		Total++;
		if (It->bDowned && !It->IsPardonPending()) Out++;
	}
	if (Total > 0 && Out == Total) EndRun(false);
}

void ASQGameMode::EndRun(bool bWon)
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS) return;
	// Once per run: a 2nd call (wipe + boss kill on the same frame, cheat skip) would pay everyone twice
	if (GS->Phase == ESQPhase::Victory || GS->Phase == ESQPhase::Defeat || GS->Phase == ESQPhase::Waiting) return;
	bBossDefeatedPending = false;
	const bool bWasBossFight = GS->Phase == ESQPhase::Boss;

	if (IsValid(GS->Boss))
	{
		GS->Boss->Destroy();
	}
	GS->Boss = nullptr;
	if (GS->Horde) GS->Horde->ClearAll();

	if (bWon && D)
	{
		for (const FSQStoryDef& Memo : D->Story)
		{
			if (Memo.Night == 3) GS->StoryText = GS->StoryText.IsEmpty() ? Memo.Text : GS->StoryText + TEXT("\n\n") + Memo.Text;
		}
	}

	// ---- The stats screen
	const FSQMissionDef* Mission = GS->GetMission();
	const USQSaveGame* HostSave = USQSaveGame::Load();
	FSQRunStats Stats;
	Stats.bWon = bWon;
	Stats.MissionId = GS->MissionId;
	Stats.bHard = GS->bHardMode;
	Stats.StageSeconds = GS->StageTime + GS->FinalSwarmSeconds;
	Stats.TeamLevel = GS->TeamLevel;
	Stats.ObjectivesDone = Director.NumDone();
	Stats.ObjectivesTotal = Director.NumObjectives();
	Stats.MinibossesKilled = Director.GetMinibossKills();
	Stats.BossSeconds = (bWasBossFight || (bWon && BossFightStart > 0.f)) ? FMath::Max(0.f, GetWorld()->GetTimeSeconds() - BossFightStart) : 0.f;
	Stats.Defiance = GS->Defiance;
	Stats.FinalSwarmSeconds = GS->FinalSwarmSeconds;
	Stats.bFirstClear = bWon && HostSave && (GS->bHardMode ? !HostSave->HasClearedHard(GS->MissionId) : !HostSave->HasClearedMission(GS->MissionId));
	const bool bAllObjectives = Stats.ObjectivesTotal > 0 && Stats.ObjectivesDone >= Stats.ObjectivesTotal;
	Director.End();

	// ---- Gold banking (the meta currency): a share of what each reaper EARNED (chest spending never costs the bank), clause bonus, clear bonus
	const float StageShare = FMath::Clamp(GS->StageTime / FMath::Max(1.f, GS->StageDuration), 0.f, 1.f);
	const float Share = bWon ? FMath::Max(0.f, D ? D->Tune(TEXT("BankShareWin"), 0.35f) : 0.35f)
		: FMath::Max(0.f, D ? D->Tune(TEXT("BankShareLoss"), 0.2f) : 0.2f) * FMath::Max(0.5f, StageShare);
	const float HardGold = (Mission && GS->bHardMode) ? Mission->HardGoldMult : 1.f;
	const float RunMinutes = (GetWorld()->GetTimeSeconds() - RunStartTime) / 60.f;
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		ASQPlayerState* QuestPS = Cast<ASQPlayerState>(EachPS);
		if (!QuestPS) continue;
		// Late joiners: a win only counts if they joined before the last act (no hopping into a friend's boss fight for a free unlock)
		const bool bPlayerWon = bWon && QuestPS->RunJoinNight < 2;
		const int32 Banked = FMath::Clamp(FMath::RoundToInt(static_cast<float>(QuestPS->RunGoldEarned) * Share * GS->CoinMultiplier()
			+ (bPlayerWon && Mission ? static_cast<float>(Mission->ClearGold) * HardGold : 0.f)), 0, 10000000);
		FSQPlayerRunStats& Line = Stats.Players.AddDefaulted_GetRef();
		Line.Name = QuestPS->GetPlayerName();
		Line.ReaperId = QuestPS->ReaperId;
		Line.Kills = QuestPS->Kills;
		Line.GoldEarned = QuestPS->RunGoldEarned;
		Line.GoldBanked = QuestPS->IsABot() ? 0 : Banked;
		Line.Augments = QuestPS->OwnedAugments.Num();
		Line.bBot = QuestPS->IsABot();
		if (QuestPS->IsABot() || !Cast<APlayerController>(QuestPS->GetOwner())) continue;

		QuestPS->CreditQuest(TEXT("RunsPlayed"));
		if (bPlayerWon)
		{
			QuestPS->CreditQuest(TEXT("RunsWon"));
			QuestPS->CreditQuest(FName(*FString::Printf(TEXT("Win_%s"), *QuestPS->ReaperId.ToString())));
			if (GS->bHardMode) QuestPS->CreditQuest(TEXT("HardWins"));
			const TArray<FSQModifierDef>& Mods = SQQuests::Modifiers();
			for (int32 ModIdx = 0; ModIdx < Mods.Num(); ++ModIdx)
			{
				if (GS->HasModifier(ModIdx)) QuestPS->CreditQuest(FName(*FString::Printf(TEXT("ModWin_%s"), *Mods[ModIdx].Id.ToString())));
			}
			QuestPS->CreditQuest(TEXT("ModsInWin"), SQQuests::CountModifiers(GS->ModifierMask));
			if (bAllObjectives && !bRunAnyoneDowned) QuestPS->CreditQuest(TEXT("PerfectRun"));
			if (D && RunMinutes <= D->Tune(TEXT("FastWinMinutes"), 14.f) * GS->NightScale()) QuestPS->CreditQuest(TEXT("FastWin"));
		}
		QuestPS->CreditQuest(TEXT("DifficultyPct"), FMath::RoundToInt(FMath::Clamp(GS->Difficulty, 0.f, 100.f) * 100.f));
		QuestPS->FlushQuests();
		// Bank first (marks the mission cleared: derived quest stats), then the quest run end folds the bests and saves
		QuestPS->ClientMissionEnd(GS->MissionId, GS->bHardMode, bPlayerWon, Banked, Stats.StageSeconds);
		QuestPS->ClientQuestRunEnd(bPlayerWon, QuestPS->ReaperId, 0);
	}
	GS->RunStats = Stats;
	GS->RunResult = bWon ? ESQRunResult::Victory : ESQRunResult::Defeat;
	GS->bFinalSwarm = false;
	GS->MinibossName.Reset();
	GS->Quota = 0;
	if (IsValid(GS->Portal))
	{
		GS->Portal->Destroy();
		GS->Portal = nullptr;
	}

	GS->Phase = bWon ? ESQPhase::Victory : ESQPhase::Defeat;
	GS->PhaseDuration = D ? D->Tune(TEXT("EndScreenSeconds"), 20.f) : 20.f;
	GS->PhaseTimeLeft = GS->PhaseDuration;
	int32 CrewSize = 0;
	for (TActorIterator<ASQCharacter> CrewIt(GetWorld()); CrewIt; ++CrewIt) CrewSize++;
	GS->PushAlert(bWon ? FString::Printf(TEXT("MISSION COMPLETE! %s is closed. Death is... mildly impressed."), Mission ? *Mission->Name : TEXT("The contract"))
		: (CrewSize <= 1 ? FString(TEXT("Reaped. Death is filling out the paperwork.")) : FString(TEXT("The whole crew is skulls. Death is filling out the paperwork."))));
}

void ASQGameMode::ResetRun()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS) return;

	// Stand-ins for players who dropped and never came back leave with the run
	HeldSeats.Reset();
	{
		TArray<AController*> SeatBots;
		for (APlayerState* Each : GS->PlayerArray)
		{
			const ASQPlayerState* EachPS = Cast<ASQPlayerState>(Each);
			if (EachPS && EachPS->bHeldSeat) SeatBots.Add(Cast<AController>(EachPS->GetOwner()));
		}
		for (AController* SeatBot : SeatBots)
		{
			if (!SeatBot) continue;
			if (APawn* SeatPawn = SeatBot->GetPawn()) SeatPawn->Destroy();
			SeatBot->Destroy();
		}
	}
	Director.End();
	if (GS->Horde) GS->Horde->ClearAll();
	GS->ResetTeamProgress();
	if (IsValid(GS->Boss)) GS->Boss->Destroy();
	GS->Boss = nullptr;
	bBossDefeatedPending = false;
	// The map's chests, bell and objective parts go with the run
	if (IsValid(GS->Portal)) GS->Portal->Destroy();
	GS->Portal = nullptr;
	for (TActorIterator<ASQChest> ChestIt(GetWorld()); ChestIt; ++ChestIt) ChestIt->Destroy();
	for (TActorIterator<ASQObjective> PartIt(GetWorld()); PartIt; ++PartIt) PartIt->Destroy();
	BeatsFired.Reset();
	// Stale pick pause / clocks from the last run
	GS->bPickPause = false;
	GS->PickPauseTimeLeft = 0.f;
	GS->PickersTotal = 0;
	GS->PickersDone = 0;
	GS->PhaseTimeLeft = -1.f;
	GS->EnemyCount = 0;
	LastBanked = 0;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	// Same mission again unless the host changes it (a first clear may have opened Hard mode / the next map)
	ApplyMissionChoice();
	PlaceLantern();

	for (APlayerState* PS : GS->PlayerArray)
	{
		if (ASQPlayerState* SQPS = Cast<ASQPlayerState>(PS)) SQPS->ResetForRun();
	}
	SpawnCounter = 0;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* C = *It;
		C->InitReaper(C->ReaperId);
		C->SetActorLocation(GetSpawnTransform().GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	}

	GS->Phase = ESQPhase::Waiting;
	GS->bSkipLobby = false;
	bRetryStart = false;
	WaitTimer = D ? D->Tune(TEXT("StartDelay"), 3.f) : 3.f;
}

void ASQGameMode::RetryRun()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS || (GS->Phase != ESQPhase::Victory && GS->Phase != ESQPhase::Defeat)) return;
	ResetRun();
	// ResetRun leaves everyone in the lobby: skip it (the lobby tick marks the crew ready and starts on a short timer)
	GS->bSkipLobby = true;
	bRetryStart = true;
	WaitTimer = 0.f;
}

void ASQGameMode::EndToLobby()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS || (GS->Phase != ESQPhase::Victory && GS->Phase != ESQPhase::Defeat)) return;
	ResetRun();
}

void ASQGameMode::SkipPhase()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS) return;
	switch (GS->Phase)
	{
	case ESQPhase::Waiting:
		WaitTimer = 0.f;
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (ASQPlayerState* SQPS = Cast<ASQPlayerState>(PS)) SQPS->bShopReady = true;
		}
		break;
	case ESQPhase::Night:
		// Cheat skip: bell up -> straight to the boss; otherwise to the end of the stage clock (overtime, the bell rises)
		if (GS->bBossReady) StartBossFight(nullptr);
		else GS->StageTime = GS->StageDuration;
		break;
	case ESQPhase::Boss: if (GS->Boss) GS->Boss->TakeBossDamage(1.e9f, nullptr); break;
	default: GS->PhaseTimeLeft = 0.f; break;
	}
}

// ---------------------------------------------------------------- missions, biomes, bosses

FName ASQGameMode::CurrentBiome() const
{
	const ASQGameState* GS = GetGameState<ASQGameState>();
	return (GS && GS->Arena) ? GS->Arena->BiomeId : FName(TEXT("Graveyard"));
}

void ASQGameMode::ApplyMissionChoice()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS) return;
	bool bHard = false;
	const FName Pending = USQMissions::GetPendingMission(this, bHard);
	GS->MissionId = Pending;
	GS->bHardMode = bHard;
}

void ASQGameMode::PlaceLantern()
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	if (!GS || !GS->Lantern) return;
	TArray<FVector> Hub;
	if (GS->Arena && GS->Arena->GetObjectiveSpots(TEXT("BankSouls"), 1, Hub) && Hub.Num() > 0)
	{
		GS->Lantern->SetActorLocation(Hub[0]);
		return;
	}
	const FVector Fallback = GS->Arena ? GS->Arena->FindLanternSpot(0.f, 0.f, GS->Lantern->Radius + 150.f) : FVector(0.f, 0.f, ASQArena::GroundZAt(this, 0.f, 0.f));
	GS->Lantern->SetActorLocation(Fallback);
}

void ASQGameMode::CycleBiome()
{
	// Lobby key B (host): the next mission the host's profile has unlocked
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	const TArray<FSQMissionDef>& All = USQMissions::Missions();
	if (!GS || !D || GS->Phase != ESQPhase::Waiting || All.Num() == 0) return;
	const USQSaveGame* Save = USQSaveGame::Load();
	const int32 Current = All.IndexOfByPredicate([GS](const FSQMissionDef& Each) { return Each.Id == GS->MissionId; });
	for (int32 Step = 1; Step <= All.Num(); ++Step)
	{
		const FSQMissionDef& Next = All[(FMath::Max(0, Current) + Step) % All.Num()];
		if (!USQMissions::IsMissionUnlocked(D, Save, Next.Id)) continue;
		GS->MissionId = Next.Id;
		GS->bHardMode = false;
		USQMissions::SetPendingMission(this, Next.Id, false);
		if (GS->Arena && D->FindBiome(Next.Biome)) GS->Arena->SetBiome(Next.Biome);
		PlaceLantern();
		GS->PushAlert(FString::Printf(TEXT("Mission: %s (%d/5 skulls). %s"), *Next.Name, Next.Difficulty,
			USQMissions::IsHardUnlocked(D, Save, Next.Id) ? TEXT("O = Hard mode.") : TEXT("Beat it to unlock Hard mode.")));
		return;
	}
}

void ASQGameMode::ToggleEndless()
{
	// Lobby key O (host): Hard mode, once this mission is beaten (Endless mode was retired with the missions rebuild)
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || GS->Phase != ESQPhase::Waiting) return;
	if (!USQMissions::IsHardUnlocked(D, USQSaveGame::Load(), GS->MissionId))
	{
		GS->PushAlert(TEXT("Hard mode unlocks once this mission is beaten."));
		return;
	}
	GS->bHardMode = !GS->bHardMode;
	USQMissions::SetPendingMission(this, GS->MissionId, GS->bHardMode);
	GS->PushAlert(GS->bHardMode ? TEXT("HARD MODE: tougher, hungrier, richer.") : TEXT("Normal mode."));
}

const FSQBossDef* ASQGameMode::PickBossForNight(int32 Night) const
{
	const USQData* D = USQData::Get(this);
	if (!D || D->Bosses.Num() == 0) return nullptr;

	if (Night >= 2)
	{
		// The biome's own legend (fallback when a mission names no valid boss)
		const FSQBiomeDef* B = D->FindBiome(CurrentBiome());
		if (B && !B->BossId.IsNone())
		{
			if (const FSQBossDef* Found = D->FindBoss(B->BossId)) return Found;
		}
		return D->FindBossForNight(3);
	}
	return D->FindBossForNight(Night + 1);
}

// ---------------------------------------------------------------- chests, bell use, timeline, quest tracking

void ASQGameMode::CreditAll(ASQGameState* GS, FName Stat, int32 Amount)
{
	if (!GS || Amount <= 0) return;
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS)) EachSQ->CreditQuest(Stat, Amount);
	}
}

void ASQGameMode::HandleUse(ASQPlayerState* PS)
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	ASQCharacter* User = PS ? PS->GetPawn<ASQCharacter>() : nullptr;
	if (!GS || !D || !User || User->bDowned || User->CarriedSkull) return;
	if (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss) return;
	const FVector UserAt = User->GetActorLocation();
	// E also picks up a downed teammate's skull (ASQCharacter::ServerInteract): that press doesn't open anything
	for (TActorIterator<ASQCharacter> SkullIt(GetWorld()); SkullIt; ++SkullIt)
	{
		const ASQCharacter* Other = *SkullIt;
		if (Other != User && Other->bDowned && !Other->CarriedBy && FVector::Dist2D(Other->GetActorLocation(), UserAt) < 300.f) return;
	}

	// The nearest unopened chest in reach
	const float ChestRange = FMath::Max(50.f, D->Tune(TEXT("ChestUseRange"), 320.f));
	ASQChest* BestChest = nullptr;
	float BestDist = ChestRange;
	for (TActorIterator<ASQChest> ChestIt(GetWorld()); ChestIt; ++ChestIt)
	{
		ASQChest* Chest = *ChestIt;
		if (!Chest || Chest->bOpened) continue;
		const float Dist = static_cast<float>(FVector::Dist2D(Chest->GetActorLocation(), UserAt));
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestChest = Chest;
		}
	}
	if (BestChest)
	{
		BestChest->TryOpen(PS);
		return;
	}

	// The Appointment Bell (only up once the objectives are resolved or the stage clock ran out)
	ASQPortal* Bell = GS->Portal.Get();
	if (IsValid(Bell) && GS->Phase == ESQPhase::Night && !Bell->bRung && FVector::Dist2D(Bell->GetActorLocation(), UserAt) <= Bell->GetUseRange())
	{
		if (!Bell->bArmed)
		{
			const int32 Left = FMath::Max(0, Director.NumObjectives() - Director.NumDone() - Director.NumFailed());
			if (!PS->IsABot()) PS->ClientNotice(TEXT("THE BELL ISN'T READY"), FString::Printf(TEXT("%d objective(s) left. Keep reaping."), Left), 0, 0);
			return;
		}
		Bell->StartRing(User);
	}
}

void ASQGameMode::PlaceChests(int32 Act)
{
	ASQGameState* GS = GetGameState<ASQGameState>();
	const USQData* D = USQData::Get(this);
	if (!GS || !D) return;
	ASQArena* Arena = GS->Arena.Get();
	const float Half = Arena ? Arena->GetArenaHalf() : D->Tune(TEXT("ArenaHalfSize"), 6000.f);
	const FVector LanternAt = GS->Lantern ? GS->Lantern->GetActorLocation() : FVector::ZeroVector;
	// Deterministic: the same run seed and act always give the same layout
	FRandomStream Stream(RunSeed + Act * 7919 + static_cast<int32>(GetTypeHash(CurrentBiome()) & 0xFFFF));

	// Paid chests (unopened ones from earlier acts stay; capped). Per act = the old per-night numbers.
	int32 OnMap = 0;
	for (TActorIterator<ASQChest> ChestIt(GetWorld()); ChestIt; ++ChestIt)
	{
		if (!ChestIt->bOpened) OnMap++;
	}
	const int32 Wanted = FMath::RoundToInt(D->Tune(TEXT("ChestsPerNight"), 6.f) + D->Tune(TEXT("ChestsPerExtraPlayer"), 4.f) * static_cast<float>(FMath::Max(0, CountTeam() - 1)));
	const int32 ToPlace = FMath::Clamp(FMath::Min(Wanted, FMath::RoundToInt(D->Tune(TEXT("ChestsMax"), 24.f)) - OnMap), 0, 40);
	for (int32 ChestIdx = 0; ChestIdx < ToPlace; ++ChestIdx)
	{
		for (int32 ChestTry = 0; ChestTry < 12; ++ChestTry)
		{
			const float CandX = Stream.FRandRange(-Half * 0.88f, Half * 0.88f);
			const float CandY = Stream.FRandRange(-Half * 0.88f, Half * 0.88f);
			const FVector Spot = Arena ? Arena->FindLanternSpot(CandX, CandY, 120.f) : FVector(CandX, CandY, 0.f);
			if (FVector::Dist2D(Spot, LanternAt) < 900.f) continue;
			if (Arena && Arena->IsReservedSpot(static_cast<float>(Spot.X), static_cast<float>(Spot.Y), 120.f)) continue;
			bool bCrowded = false;
			for (TActorIterator<ASQChest> NearIt(GetWorld()); NearIt; ++NearIt)
			{
				if (FVector::Dist2D(NearIt->GetActorLocation(), Spot) < 600.f) { bCrowded = true; break; }
			}
			if (bCrowded) continue;
			ASQChest::SpawnChest(GetWorld(), Spot, false);
			break;
		}
	}
}

void ASQGameMode::TickTimeline(ASQGameState* GS)
{
	const TArray<FSQTimelineBeat>& Beats = SQQuests::Timeline();
	if (BeatsFired.Num() != Beats.Num()) BeatsFired.Init(false, Beats.Num());
	const float Progress = GS->NightProgress();
	const float SwarmMinutes = GS->FinalSwarmSeconds / 60.f;
	for (int32 BeatIdx = 0; BeatIdx < Beats.Num(); ++BeatIdx)
	{
		const FSQTimelineBeat& Beat = Beats[BeatIdx];
		if (BeatsFired[BeatIdx] || !Beat.AppliesToNight(GS->NightIndex) || !Beat.AppliesToMission(GS->MissionId, GS->bHardMode)) continue;
		if (Beat.Kind == ESQBeatKind::FinalSwarm)
		{
			// The horde spawns the tiers itself (ASQHordeManager::SimFinalSwarm); here: announce each tier once as it joins
			if (!GS->bFinalSwarm || SwarmMinutes < Beat.At) continue;
			BeatsFired[BeatIdx] = true;
			GS->PushBeat(Beat.Announce.IsEmpty() ? Beat.Name : Beat.Announce, 3);
			continue;
		}
		if (GS->bFinalSwarm || Progress < Beat.At) continue;
		BeatsFired[BeatIdx] = true;
		if (!GS->Horde) continue;
		if (Beat.Kind == ESQBeatKind::Miniboss)
		{
			GS->Horde->SpawnMiniboss(Beat.Enemy, Beat.HealthMult, Beat.DamageMult, Beat.Name, Beat.bFreeChest);
			GS->PushBeat(Beat.Announce.IsEmpty() ? FString::Printf(TEXT("MINIBOSS: %s"), *Beat.Name) : Beat.Announce, 1);
		}
		else
		{
			GS->Horde->SpawnSwarm(Beat.Enemy, FMath::RoundToInt(Beat.Count));
			GS->PushBeat(Beat.Announce.IsEmpty() ? FString::Printf(TEXT("SWARM: %s"), *Beat.Name) : Beat.Announce, 2);
		}
	}
}

void ASQGameMode::TickBotChests(ASQGameState* GS)
{
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		ASQPlayerState* BotPS = Cast<ASQPlayerState>(EachPS);
		if (!BotPS || !BotPS->IsABot() || !BotPS->GetPawn()) continue;
		const int32 Price = GS->ChestPriceFor(BotPS);
		if (BotPS->Gold >= Price && BotPS->SpendGold(Price))
		{
			BotPS->ChestsOpened++;
			ASQChest::GrantChestRoll(BotPS, false, false);
		}
	}
}

void ASQGameMode::TickQuestTracking(ASQGameState* GS, const USQData* D, float DeltaSeconds)
{
	const bool bNoHealing = GS->ModAny(&FSQModifierDef::bNoHealing);
	const ASQArena* Arena = GS->Arena.Get();

	// Who carries which skull this tick (a skull that stands up next tick was revived by its carrier)
	TMap<TWeakObjectPtr<ASQCharacter>, TWeakObjectPtr<ASQCharacter>> CarriedNow;
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* Reaper = *It;
		if (Reaper->CarriedSkull) CarriedNow.Add(TWeakObjectPtr<ASQCharacter>(Reaper->CarriedSkull.Get()), TWeakObjectPtr<ASQCharacter>(Reaper));
	}

	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* Reaper = *It;
		ASQPlayerState* TrackPS = Reaper->GetPlayerState<ASQPlayerState>();
		if (!TrackPS) continue;

		// Revived: credit whoever carried the skull last tick
		if (TrackPS->bTrackWasDowned && !Reaper->bDowned)
		{
			if (const TWeakObjectPtr<ASQCharacter>* Carrier = SkullCarriers.Find(TWeakObjectPtr<ASQCharacter>(Reaper)))
			{
				if (Carrier->IsValid())
				{
					if (ASQPlayerState* CarrierPS = (*Carrier)->GetPlayerState<ASQPlayerState>()) CarrierPS->CreditQuest(TEXT("Revives"));
				}
			}
			TrackPS->TrackLastHealth = Reaper->Health; // a revive is not a heal (No Sick Days)
		}
		if (Reaper->bDowned) bRunAnyoneDowned = true;
		const bool bJustRevived = TrackPS->bTrackWasDowned && !Reaper->bDowned;
		TrackPS->bTrackWasDowned = Reaper->bDowned;
		if (Reaper->bDowned)
		{
			TrackPS->TrackLastHealth = -1.f;
			continue;
		}

		// Damage taken / no-hit nights; No Sick Days: health never goes back up
		if (TrackPS->TrackLastHealth >= 0.f && !bJustRevived)
		{
			if (Reaper->Health < TrackPS->TrackLastHealth - 0.01f)
			{
				TrackPS->bTrackHitThisNight = true;
				TrackPS->TrackDamageRemainder += TrackPS->TrackLastHealth - Reaper->Health;
				const int32 Whole = FMath::FloorToInt(TrackPS->TrackDamageRemainder);
				if (Whole > 0)
				{
					TrackPS->TrackDamageRemainder -= static_cast<float>(Whole);
					TrackPS->CreditQuest(TEXT("DamageTaken"), Whole);
				}
			}
			else if (bNoHealing && Reaper->Health > TrackPS->TrackLastHealth + 0.01f)
			{
				Reaper->Health = TrackPS->TrackLastHealth;
			}
		}
		TrackPS->TrackLastHealth = Reaper->Health;

		// Dashes (the cooldown jumps up when one starts)
		if (Reaper->DashCooldownLeft > TrackPS->TrackLastDashCd + 0.2f) TrackPS->CreditQuest(TEXT("Dashes"));
		TrackPS->TrackLastDashCd = Reaper->DashCooldownLeft;

		// Most souls carried in this run (Mode Best: credited as the increase)
		if (Reaper->Carried > TrackPS->TrackRunMaxCarried)
		{
			TrackPS->CreditQuest(TEXT("MaxCarried"), Reaper->Carried - TrackPS->TrackRunMaxCarried);
			TrackPS->TrackRunMaxCarried = Reaper->Carried;
		}
		TrackPS->TrackLastCarried = Reaper->Carried;

		// Plateau tops climbed (a new one each time you step onto a different top)
		int32 OnTop = INDEX_NONE;
		if (Arena)
		{
			const TArray<FSQTerrainPlateau>& Plateaus = Arena->GetPlateaus();
			const FVector2D At2D = Reaper->Pos2D();
			for (int32 TopIdx = 0; TopIdx < Plateaus.Num(); ++TopIdx)
			{
				const FVector2D FromCentre = At2D - Plateaus[TopIdx].Centre;
				const float TopAngle = FMath::Atan2(static_cast<float>(FromCentre.Y), static_cast<float>(FromCentre.X));
				if (FromCentre.Size() < Plateaus[TopIdx].RadiusAt(TopAngle) * 0.9f) { OnTop = TopIdx; break; }
			}
		}
		if (OnTop != INDEX_NONE && OnTop != TrackPS->TrackPlateau) TrackPS->CreditQuest(TEXT("PlateausClimbed"));
		TrackPS->TrackPlateau = OnTop;
	}
	SkullCarriers = MoveTemp(CarriedNow);
}
