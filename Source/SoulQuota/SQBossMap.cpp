// Copyright 2026 Jack McGuigan. All rights reserved.
// Nov 25 2026 run structure (Docs/RunStructure.md): the three map bosses' move sets, each built on its map's mechanic.
//   The Gravekeeper (Graveyard terraces): TombWall, Burrow, Raise; phase 2-3 Cracks (pits across the terraces)
//   The Drowned King (Drowned Harbour tide): Flood, Tentacles, GhostShip; phase 2-3 KingTide (only high ground is safe)
//   The Merchant of Hours (Clocktower): TimeZones, ClockSweep (jump the hands); phase 2-3 Rewind (hit the gear or he heals)
// Every attack is telegraphed with vertical pieces (pillars / standing posts) that read from the chase camera, and spreads
// over the crew (one victim per cast, strikes per standing reaper) so co-op is fair; health already scales with crew size.
#include "SQBoss.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQCharacter.h"
#include "SQHordeManager.h"
#include "SQArena.h"
#include "SQFx.h"

namespace
{
	// MulticastFX kinds added for the map bosses (SQBoss.cpp MulticastFX_Implementation)
	constexpr uint8 FxPillarTelegraph = 10; // disc + a tall warning pillar that lands after Duration
	constexpr uint8 FxPillarImpact = 11;    // a tall burst column
	constexpr uint8 FxShip = 12;            // the ghost ship's hull glow / mast (moving: sent every few frames)
	constexpr uint8 FxLine = 14;            // a straight warning line from Location along Radius (Color alpha unused)


	bool SQBossAirborne(const ASQCharacter* Reaper)
	{
		if (!Reaper) return false;
		const FVector RL = Reaper->GetActorLocation();
		return static_cast<float>(RL.Z) - ASQArena::GroundZAt(Reaper, static_cast<float>(RL.X), static_cast<float>(RL.Y)) > 150.f;
	}
}

float ASQBoss::ZoneSpeedMultAt(const FVector2D& P) const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!GS || ZoneCenters.Num() == 0 || static_cast<float>(GS->GetServerWorldTimeSeconds()) > ZonesEndTime) return 1.f;
	const float R2 = ZoneRadius * ZoneRadius;
	for (int32 ZoneIdx = 0; ZoneIdx < ZoneCenters.Num(); ++ZoneIdx)
	{
		const FVector Center = ZoneCenters[ZoneIdx];
		if (FVector2D::DistSquared(P, FVector2D(Center.X, Center.Y)) > R2) continue;
		const uint8 Kind = ZoneKinds.IsValidIndex(ZoneIdx) ? ZoneKinds[ZoneIdx] : 1;
		return Kind == 0 ? 1.45f : (Kind == 1 ? 0.45f : 0.12f);
	}
	return 1.f;
}

float ASQBoss::SweepYawNow(int32 Hand) const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const float Now = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f;
	return SweepStartYaw + SweepRate * FMath::Max(0.f, Now - SweepStartTime) + (Hand > 0 ? 180.f : 0.f);
}

bool ASQBoss::BeginMapAbility(ESQBossAbility Ability, const FSQBossDef& Def, float Windup)
{
	const FVector L = GetActorLocation();
	const FColor Colour = AbilityColour(Ability);
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	TArray<ASQCharacter*> Standing;
	GatherPlayers(Standing);
	switch (Ability)
	{
	case ESQBossAbility::TombWall:
	{
		// A line of tombstones bursts up across the victim's path, splitting the terrace: route round it
		ASQCharacter* Picked = PickVictim(false);
		Victim = Picked;
		const FVector At = Picked ? Picked->GetActorLocation() : L + GetActorForwardVector() * 800.f;
		const FVector Across = FVector::CrossProduct((At - L).GetSafeNormal2D(), FVector::UpVector).GetSafeNormal2D();
		const int32 Posts = 7 + 2 * Phase;
		for (int32 PostIdx = 0; PostIdx < Posts; ++PostIdx)
		{
			const float Offset = (static_cast<float>(PostIdx) - static_cast<float>(Posts - 1) * 0.5f) * 170.f;
			const FVector P = At + Across * Offset;
			FBossStrike Strike;
			Strike.Center = FVector(P.X, P.Y, GroundAtXY(static_cast<float>(P.X), static_cast<float>(P.Y)) + 5.f);
			Strike.Radius = 115.f;
			Strike.Delay = Windup;
			Strike.Damage = Def.SlamDamage * 0.5f * DamageMult();
			Strike.Colour = Colour;
			Strike.PitSeconds = 6.f + Phase;
			Strike.bWallPost = true;
			Strike.Knock = 600.f;
			Strikes.Add(Strike);
			MulticastFX(FxPillarTelegraph, Strike.Center, Strike.Radius, Colour, Windup);
		}
		return true;
	}
	case ESQBossAbility::Burrow:
	{
		Victim = PickVictim(false);
		AbilityPoint = Victim.IsValid() ? Victim->GetActorLocation() : L;
		MulticastFX(1, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), HitRadius() * 2.f, Colour, Windup);
		return true;
	}
	case ESQBossAbility::Raise:
	{
		// The dead he walked past climb back out: graves open along his path
		TArray<FVector> Spots;
		for (int32 TrailIdx = PathTrail.Num() - 1; TrailIdx >= 0 && Spots.Num() < 3 + Phase; TrailIdx -= 2) Spots.Add(PathTrail[TrailIdx]);
		while (Spots.Num() < 3) Spots.Add(L + FVector(FMath::FRandRange(-700.f, 700.f), FMath::FRandRange(-700.f, 700.f), 0.f));
		Pylons.Reset(); // (not used by Raise; keeps the pylon visuals hidden)
		RaiseSpots = Spots;
		for (const FVector& Spot : Spots)
		{
			MulticastFX(FxPillarTelegraph, FVector(Spot.X, Spot.Y, GroundAtXY(static_cast<float>(Spot.X), static_cast<float>(Spot.Y)) + 5.f), 200.f, Colour, Windup);
		}
		return true;
	}
	case ESQBossAbility::Cracks:
	{
		// The terraces crack: lines of strikes run from the boss toward each reaper and leave pits behind
		const int32 Lines = FMath::Clamp(Standing.Num(), 2, 4);
		for (int32 LineIdx = 0; LineIdx < Lines; ++LineIdx)
		{
			FVector Dir;
			if (Standing.IsValidIndex(LineIdx)) Dir = (Standing[LineIdx]->GetActorLocation() - L).GetSafeNormal2D();
			else Dir = FVector(FMath::Cos(LineIdx * 2.1f), FMath::Sin(LineIdx * 2.1f), 0.f);
			if (Dir.IsNearlyZero()) Dir = GetActorForwardVector();
			for (int32 Step = 1; Step <= 8; ++Step)
			{
				const FVector P = L + Dir * (HitRadius() + 210.f * Step);
				FBossStrike Strike;
				Strike.Center = FVector(P.X, P.Y, GroundAtXY(static_cast<float>(P.X), static_cast<float>(P.Y)) + 5.f);
				Strike.Radius = 190.f;
				Strike.Delay = Windup + 0.11f * Step;
				Strike.Damage = Def.SlamDamage * 0.6f * DamageMult();
				Strike.Colour = Colour;
				Strike.PitSeconds = 4.f + Phase;
				Strikes.Add(Strike);
				MulticastFX(FxPillarTelegraph, Strike.Center, Strike.Radius, Colour, Strike.Delay);
			}
		}
		return true;
	}
	case ESQBossAbility::Flood:
	case ESQBossAbility::KingTide:
	{
		const bool bKing = Ability == ESQBossAbility::KingTide;
		if (GS) GS->PushAlert(bKing ? TEXT("KING TIDE! Get onto high ground or the walkway - the water burns!") : TEXT("The Drowned King calls the tide in! Keep to the causeways."));
		MulticastFX(1, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 1500.f, Colour, Windup);
		return true;
	}
	case ESQBossAbility::Tentacles:
	{
		// Three tentacles burst up in a line where each reaper is heading
		for (ASQCharacter* Reaper : Standing)
		{
			const FVector RL = Reaper->GetActorLocation();
			FVector Ahead = Reaper->GetVelocity().GetSafeNormal2D();
			if (Ahead.IsNearlyZero()) Ahead = (RL - L).GetSafeNormal2D();
			for (int32 Step = 0; Step < 3; ++Step)
			{
				const FVector P = RL + Ahead * (240.f * Step);
				FBossStrike Strike;
				Strike.Center = FVector(P.X, P.Y, GroundAtXY(static_cast<float>(P.X), static_cast<float>(P.Y)) + 5.f);
				Strike.Radius = 210.f;
				Strike.Delay = Windup + 0.3f * Step;
				Strike.Damage = Def.SlamDamage * 0.75f * DamageMult();
				Strike.Colour = Colour;
				Strike.Knock = 900.f;
				Strikes.Add(Strike);
				MulticastFX(FxPillarTelegraph, Strike.Center, Strike.Radius, Colour, Strike.Delay);
			}
		}
		return true;
	}
	case ESQBossAbility::GhostShip:
	{
		// A ghost ship sails a straight lane through the victim, firing broadsides both ways: the lane is marked first
		ASQCharacter* Picked = PickVictim(false);
		Victim = Picked;
		const FVector Through = Picked ? Picked->GetActorLocation() : L;
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		FBossShip Ship;
		Ship.Dir = Dir;
		Ship.Pos = Through - Dir * 2600.f;
		Ship.Left = 5.2f;
		Ships.Add(Ship);
		AbilityPoint = Ship.Pos;
		AbilityDir = Dir;
		for (int32 MarkIdx = 0; MarkIdx <= 8; ++MarkIdx)
		{
			const FVector P = Ship.Pos + Dir * (650.f * MarkIdx);
			MulticastFX(1, FVector(P.X, P.Y, GroundAtXY(static_cast<float>(P.X), static_cast<float>(P.Y)) + 5.f), 260.f, Colour, Windup + 0.6f);
		}
		MulticastFX(FxLine, FVector(Ship.Pos.X, Ship.Pos.Y, GroundAtXY(static_cast<float>(Ship.Pos.X), static_cast<float>(Ship.Pos.Y)) + 40.f), 5200.f, Colour, Windup);
		if (GS) GS->PushAlert(TEXT("A ghost ship! Get out of its lane - it fires both sides."));
		return true;
	}
	case ESQBossAbility::TimeZones:
	{
		MulticastFX(1, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), HitRadius() * 2.f, Colour, Windup);
		return true;
	}
	case ESQBossAbility::ClockSweep:
	{
		// The hand's start line is shown, then it turns: jump it or be outside its reach
		SweepCenter = FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f);
		ASQCharacter* Picked = PickVictim(false);
		Victim = Picked;
		const FVector Toward = Picked ? (Picked->GetActorLocation() - L) : GetActorForwardVector();
		// Start a quarter turn behind the victim so the hand arrives after the warning
		SweepStartYaw = static_cast<float>(Toward.Rotation().Yaw) - 90.f;
		SweepLength = 1900.f + 200.f * Phase;
		for (int32 Hand = 0; Hand < (Phase >= 2 ? 2 : 1); ++Hand)
		{
			const float Yaw = FMath::DegreesToRadians(SweepStartYaw + (Hand > 0 ? 180.f : 0.f));
			MulticastFX(FxLine, SweepCenter + FVector(0.f, 0.f, 35.f), SweepLength, Colour, Windup);
			// The line kind reads its direction from the colour-free Duration sign: send the yaw through a second marker at the tip
			const FVector Tip = SweepCenter + FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f) * SweepLength;
			MulticastFX(FxPillarTelegraph, FVector(Tip.X, Tip.Y, GroundAtXY(static_cast<float>(Tip.X), static_cast<float>(Tip.Y)) + 5.f), 120.f, Colour, Windup);
		}
		if (GS) GS->PushAlert(TEXT("The clock hands turn - JUMP them!"));
		return true;
	}
	case ESQBossAbility::Rewind:
	{
		// A glowing gear appears: stand on it before time runs back, or he heals to where he was 5 s ago
		FVector Away = (L - (Standing.Num() > 0 ? Standing[0]->GetActorLocation() : L + GetActorForwardVector() * 500.f)).GetSafeNormal2D();
		if (Away.IsNearlyZero()) Away = GetActorForwardVector();
		const FVector Side = FVector::CrossProduct(Away, FVector::UpVector);
		const FVector GearAt = L + Side * 1100.f * (FMath::RandBool() ? 1.f : -1.f);
		Pylons.Reset();
		Pylons.Add(FVector(GearAt.X, GearAt.Y, GroundAtXY(static_cast<float>(GearAt.X), static_cast<float>(GearAt.Y))));
		PylonHeldMask = 0;
		PylonLitMask = 0;
		GearHold = 0.f;
		RewindHealth = HealthHistory.Num() > 0 ? HealthHistory[0] : Health;
		MulticastFX(FxPillarTelegraph, FVector(Pylons[0]) + FVector(0.f, 0.f, 5.f), 260.f, Colour, Windup + 5.f);
		if (GS) GS->PushAlert(TEXT("The Merchant winds time back! Stand on the glowing GEAR to stop it!"));
		return true;
	}
	default:
		return false;
	}
}

void ASQBoss::ResolveMapAbility(ESQBossAbility Ability, const FSQBossDef& Def)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQHordeManager* Horde = GS ? GS->Horde.Get() : nullptr;
	const FVector L = GetActorLocation();
	const USQData* D = USQData::Get(this);
	switch (Ability)
	{
	case ESQBossAbility::Burrow:
		bBurrowed = true;
		bInvulnerable = true;
		SetActorEnableCollision(false);
		MulticastFX(FxPillarImpact, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), HitRadius() * 1.6f, AbilityColour(Ability), 0.5f);
		break;
	case ESQBossAbility::Raise:
		if (Horde)
		{
			const FName RaiseId = Def.SummonId.IsNone() ? FName(TEXT("Shambler")) : Def.SummonId;
			for (const FVector& Spot : RaiseSpots)
			{
				Horde->SpawnEnemyNear(RaiseId, FVector2D(Spot.X, Spot.Y), 2 + Phase, 160.f);
				MulticastFX(FxPillarImpact, FVector(Spot.X, Spot.Y, GroundAtXY(static_cast<float>(Spot.X), static_cast<float>(Spot.Y)) + 5.f), 200.f, AbilityColour(Ability), 0.4f);
			}
		}
		RaiseSpots.Reset();
		break;
	case ESQBossAbility::Flood:
	case ESQBossAbility::KingTide:
	{
		const bool bKing = Ability == ESQBossAbility::KingTide;
		const float Hold = bKing ? 10.f : 9.f;
		const bool bTideMap = GS && GS->Arena && GS->Arena->TideLevel() > -1000.f;
		if (GS && bTideMap)
		{
			const float Now = static_cast<float>(GS->GetServerWorldTimeSeconds());
			GS->BossTideStart = Now;
			GS->BossTideEnd = Now + Hold;
			GS->BossTideRise = bKing ? (D ? D->Tune(TEXT("BossKingTideRise"), 260.f) : 260.f) : (D ? D->Tune(TEXT("BossFloodRise"), 120.f) : 120.f);
		}
		else
		{
			// Off the tide map (or no tide): the water still comes - lingering pools across the lanes near the crew
			TArray<ASQCharacter*> Standing;
			GatherPlayers(Standing);
			for (ASQCharacter* Reaper : Standing)
			{
				for (int32 PoolIdx = 0; PoolIdx < 3; ++PoolIdx)
				{
					const FVector P = Reaper->GetActorLocation() + FVector(FMath::FRandRange(-500.f, 500.f), FMath::FRandRange(-500.f, 500.f), 0.f);
					FBossHazard Pool;
					Pool.Center = FVector(P.X, P.Y, GroundAtXY(static_cast<float>(P.X), static_cast<float>(P.Y)) + 5.f);
					Pool.Radius = bKing ? 360.f : 280.f;
					Pool.TimeLeft = Hold;
					Pool.Dps = Def.RingDamage * 0.8f * DamageMult();
					Hazards.Add(Pool);
					MulticastFX(1, Pool.Center, Pool.Radius, AbilityColour(Ability), Pool.TimeLeft);
				}
			}
		}
		KingTideTick = 0.f;
		break;
	}
	case ESQBossAbility::TimeZones:
	{
		// Haste (blue: use it), slow (orange) and freeze (white) circles under and around the crew for 8 s
		TArray<ASQCharacter*> Standing;
		GatherPlayers(Standing);
		ZoneCenters.Reset();
		ZoneKinds.Reset();
		ZoneRadius = D ? D->Tune(TEXT("BossZoneRadius"), 380.f) : 380.f;
		const int32 Want = 3 + Phase + FMath::Max(0, Standing.Num() - 1);
		for (int32 ZoneIdx = 0; ZoneIdx < Want; ++ZoneIdx)
		{
			FVector C;
			if (Standing.IsValidIndex(ZoneIdx)) C = Standing[ZoneIdx]->GetActorLocation();
			else C = L + FVector(FMath::FRandRange(-1600.f, 1600.f), FMath::FRandRange(-1600.f, 1600.f), 0.f);
			C.Z = GroundAtXY(static_cast<float>(C.X), static_cast<float>(C.Y)) + 5.f;
			// Under a reaper: slow or freeze (move!); elsewhere: one haste circle to run for
			const uint8 Kind = Standing.IsValidIndex(ZoneIdx) ? static_cast<uint8>(FMath::RandRange(1, Phase >= 1 ? 2 : 1)) : static_cast<uint8>(ZoneIdx == Want - 1 ? 0 : FMath::RandRange(0, 2));
			ZoneCenters.Add(C);
			ZoneKinds.Add(Kind);
			const FColor KindColour = Kind == 0 ? FColor(80, 180, 255) : (Kind == 1 ? FColor(255, 150, 40) : FColor(235, 245, 255));
			MulticastFX(1, C, ZoneRadius, KindColour, 8.f);
			MulticastFX(FxPillarImpact, C, ZoneRadius * 0.4f, KindColour, 0.4f);
		}
		ZonesEndTime = (GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f) + 8.f;
		break;
	}
	case ESQBossAbility::ClockSweep:
		bSweep = true;
		bSweepTwin = Phase >= 2;
		SweepStartTime = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f;
		SweepRate = (Phase >= 1 ? 150.f : 115.f) * (FMath::RandBool() ? 1.f : -1.f);
		SweepHit.Reset();
		break;
	default:
		break;
	}
}

bool ASQBoss::TickMapAbility(ESQBossAbility Ability, float DeltaSeconds, const FSQBossDef& Def, bool& bHandled)
{
	bHandled = true;
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	const FVector L = GetActorLocation();
	const USQData* D = USQData::Get(this);
	switch (Ability)
	{
	case ESQBossAbility::Burrow:
	{
		// Underground: a marker hunts the victim (slower than a run), then he erupts there
		if (Victim.IsValid() && !Victim->bDowned)
		{
			const FVector To = Victim->GetActorLocation() - AbilityPoint;
			const float Step = (D ? D->Tune(TEXT("BossBurrowSpeed"), 520.f) : 520.f) * DeltaSeconds;
			AbilityPoint += To.GetSafeNormal2D() * FMath::Min(Step, static_cast<float>(To.Size2D()));
		}
		PulseTimer -= DeltaSeconds;
		const float Erupt = 1.9f;
		if (PulseTimer <= 0.f && ActiveTime < Erupt)
		{
			PulseTimer = 0.3f;
			MulticastFX(1, FVector(AbilityPoint.X, AbilityPoint.Y, GroundAtXY(static_cast<float>(AbilityPoint.X), static_cast<float>(AbilityPoint.Y)) + 5.f), 380.f, AbilityColour(Ability), 0.32f);
		}
		if (ActiveTime < Erupt) return false;
		const float GroundZ = GroundAtXY(static_cast<float>(AbilityPoint.X), static_cast<float>(AbilityPoint.Y));
		const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		SetActorEnableCollision(true);
		TeleportTo(FVector(AbilityPoint.X, AbilityPoint.Y, GroundZ + HalfHeight + 20.f), GetActorRotation(), false, false);
		bBurrowed = false;
		bInvulnerable = false;
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Reaper = *It;
			if (Reaper->bDowned) continue;
			const FVector Off = Reaper->GetActorLocation() - AbilityPoint;
			if (Off.Size2D() > 380.f) continue;
			Reaper->ApplyHit(Def.SlamDamage * 1.1f * DamageMult());
			Reaper->LaunchCharacter(Off.GetSafeNormal2D() * 700.f + FVector(0.f, 0.f, 500.f), true, true);
		}
		MulticastFX(FxPillarImpact, FVector(AbilityPoint.X, AbilityPoint.Y, GroundZ + 5.f), 380.f, AbilityColour(Ability), 0.6f);
		return true;
	}
	case ESQBossAbility::GhostShip:
		// The ship runs on its own (TickMapPassives); the boss is free again once it is under way
		return ActiveTime >= 0.6f;
	case ESQBossAbility::Flood:
		return ActiveTime >= 0.6f;
	case ESQBossAbility::KingTide:
	{
		// Planted and channelling while the king tide holds: the water hurts (TickMapPassives)
		return ActiveTime >= 6.f;
	}
	case ESQBossAbility::ClockSweep:
	{
		const float Turn = bSweepTwin ? 180.f : 360.f;
		const float Duration = Turn / FMath::Max(1.f, FMath::Abs(SweepRate));
		// Hit test each hand: reapers on the hand's line, within reach, not in the air (jump it), once per sweep
		for (int32 Hand = 0; Hand < (bSweepTwin ? 2 : 1); ++Hand)
		{
			const float Yaw = FMath::DegreesToRadians(SweepYawNow(Hand));
			const FVector2D HandDir(FMath::Cos(Yaw), FMath::Sin(Yaw));
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				ASQCharacter* Reaper = *It;
				if (Reaper->bDowned || SweepHit.Contains(Reaper)) continue;
				const FVector RL = Reaper->GetActorLocation();
				const FVector2D Off(RL.X - SweepCenter.X, RL.Y - SweepCenter.Y);
				const float Along = static_cast<float>(FVector2D::DotProduct(Off, HandDir));
				if (Along < HitRadius() || Along > SweepLength) continue;
				const float Across = static_cast<float>(FMath::Abs(FVector2D::CrossProduct(HandDir, Off)));
				if (Across > 95.f) continue;
				SweepHit.Add(Reaper);
				if (SQBossAirborne(Reaper)) continue; // jumped it
				Reaper->ApplyHit(Def.SlamDamage * 0.9f * DamageMult());
				Reaper->LaunchCharacter(FVector(-HandDir.Y, HandDir.X, 0.f) * (SweepRate > 0.f ? 600.f : -600.f) + FVector(0.f, 0.f, 300.f), true, true);
				MulticastFX(0, RL, 160.f, AbilityColour(Ability), 0.25f);
			}
		}
		if (ActiveTime < Duration) return false;
		bSweep = false;
		bSweepTwin = false;
		return true;
	}
	case ESQBossAbility::Rewind:
	{
		// Stand on the gear (0.6 s total) before 5 s are up: the rewind breaks and he staggers; else he heals back
		const float HoldRange = D ? D->Tune(TEXT("BossPylonRange"), 260.f) : 260.f;
		uint8 Held = 0;
		if (Pylons.Num() > 0)
		{
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				if (!It->bDowned && FVector::Dist2D(It->GetActorLocation(), Pylons[0]) <= HoldRange) { Held = 1; break; }
			}
		}
		PylonHeldMask = Held;
		if (Held) GearHold += DeltaSeconds;
		if (GearHold >= 0.6f)
		{
			Pylons.Reset();
			PylonHeldMask = 0;
			bStaggered = true;
			StaggerLeft = D ? D->Tune(TEXT("BossStaggerSeconds"), 4.f) : 4.f;
			MulticastAnim(4);
			MulticastFX(FxPillarImpact, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 700.f, FColor(255, 230, 120), 0.5f);
			if (GS) GS->PushAlert(TEXT("GEAR JAMMED! The rewind breaks - hit him!"));
			return true;
		}
		if (ActiveTime < 5.f) return false;
		Pylons.Reset();
		PylonHeldMask = 0;
		if (RewindHealth > Health)
		{
			Health = FMath::Min(MaxHealth, RewindHealth);
			if (GS) GS->PushAlert(TEXT("Time runs back... the Merchant's wounds close."));
		}
		MulticastFX(FxPillarImpact, FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y)) + 5.f), 600.f, AbilityColour(Ability), 0.6f);
		return true;
	}
	case ESQBossAbility::TombWall:
	case ESQBossAbility::Raise:
	case ESQBossAbility::Cracks:
	case ESQBossAbility::Tentacles:
	case ESQBossAbility::TimeZones:
		return ActiveTime >= 0.4f;
	default:
		bHandled = false;
		return true;
	}
}

void ASQBoss::TickMapPassives(float DeltaSeconds, const FSQBossDef& Def)
{
	ASQGameState* GS = GetWorld()->GetGameState<ASQGameState>();
	ASQHordeManager* Horde = GS ? GS->Horde.Get() : nullptr;
	const FVector L = GetActorLocation();

	// Path (Raise) and health history (Rewind)
	PathTimer -= DeltaSeconds;
	if (PathTimer <= 0.f)
	{
		PathTimer = 1.f;
		PathTrail.Add(FVector(L.X, L.Y, GroundAtXY(static_cast<float>(L.X), static_cast<float>(L.Y))));
		if (PathTrail.Num() > 10) PathTrail.RemoveAt(0);
	}
	HistoryTimer -= DeltaSeconds;
	if (HistoryTimer <= 0.f)
	{
		HistoryTimer = 0.5f;
		HealthHistory.Add(Health);
		if (HealthHistory.Num() > 11) HealthHistory.RemoveAt(0);
	}

	// Ghost ships: sail, broadside, ram
	for (int32 ShipIdx = Ships.Num() - 1; ShipIdx >= 0; --ShipIdx)
	{
		FBossShip& Ship = Ships[ShipIdx];
		// The ship waits out the lane warning (wind-up) before it sails
		if (Stage == EStage::Windup && ActiveAbility == static_cast<uint8>(ESQBossAbility::GhostShip)) continue;
		Ship.Pos += Ship.Dir * 1000.f * DeltaSeconds;
		Ship.Left -= DeltaSeconds;
		const FVector Ground(Ship.Pos.X, Ship.Pos.Y, GroundAtXY(static_cast<float>(Ship.Pos.X), static_cast<float>(Ship.Pos.Y)));
		Ship.FxTimer -= DeltaSeconds;
		if (Ship.FxTimer <= 0.f)
		{
			Ship.FxTimer = 0.15f;
			MulticastFX(FxShip, Ground + FVector(0.f, 0.f, 5.f), 260.f, FColor(140, 230, 255), 0.3f);
		}
		for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
		{
			ASQCharacter* Reaper = *It;
			if (Reaper->bDowned || Ship.Hit.Contains(Reaper)) continue;
			if (FVector::Dist2D(Reaper->GetActorLocation(), Ground) > 270.f) continue;
			Ship.Hit.Add(Reaper);
			Reaper->ApplyHit(Def.SlamDamage * 1.0f * DamageMult());
			Reaper->LaunchCharacter(FVector(-Ship.Dir.Y, Ship.Dir.X, 0.f) * 800.f + FVector(0.f, 0.f, 350.f), true, true);
		}
		Ship.FireTimer -= DeltaSeconds;
		if (Horde && Ship.FireTimer <= 0.f)
		{
			Ship.FireTimer = Phase >= 1 ? 0.35f : 0.5f;
			const FVector2D Side(-Ship.Dir.Y, Ship.Dir.X);
			for (int32 SideSign = -1; SideSign <= 1; SideSign += 2)
			{
				for (int32 Spread = -1; Spread <= 1; ++Spread)
				{
					const FVector2D ShotDir = (Side * static_cast<float>(SideSign)).GetRotated(Spread * 12.f);
					Horde->SpawnProjectile(FVector2D(Ship.Pos.X, Ship.Pos.Y) + ShotDir * 200.f, ShotDir * 620.f, Def.RingDamage * DamageMult(), 32.f, 3.f, 1, true, nullptr);
				}
			}
		}
		if (Ship.Left <= 0.f) Ships.RemoveAt(ShipIdx);
	}

	// King Tide: flooded ground burns while it holds (the walkway and high ground are safe)
	if (GS && GS->Arena && static_cast<float>(GS->GetServerWorldTimeSeconds()) < GS->BossTideEnd && ActiveAbility == static_cast<uint8>(ESQBossAbility::KingTide))
	{
		KingTideTick -= DeltaSeconds;
		if (KingTideTick <= 0.f)
		{
			KingTideTick = 0.5f;
			for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
			{
				if (It->bDowned) continue;
				if (GS->Arena->IsFlooded(It->Pos2D())) It->ApplyHit(Def.RingDamage * 0.6f * DamageMult());
			}
		}
	}

	// Time zones end on their own
	if (ZoneCenters.Num() > 0 && GS && static_cast<float>(GS->GetServerWorldTimeSeconds()) > ZonesEndTime)
	{
		ZoneCenters.Reset();
		ZoneKinds.Reset();
	}
}

void ASQBoss::UpdateMapVisuals(float DeltaSeconds)
{
	// Burrowed: the body is under the ground
	const bool bShowBody = !bBurrowed;
	if (Skin && bArtVisuals && Skin->IsVisible() != bShowBody) Skin->SetVisibility(bShowBody, true);
	if (Body && !bArtVisuals && Body->IsVisible() != bShowBody) Body->SetVisibility(bShowBody, true);

	// Entrance: climbs out of the lantern (skin starts a body height down and rises)
	if (Skin && bArtVisuals && bSkinRestKnown)
	{
		const float Total = FMath::Max(0.1f, EntranceTotal);
		const float Down = FMath::Clamp(EntranceLeft / Total, 0.f, 1.f);
		const float Ease = Down * Down;
		const FVector Want = SkinRestLoc - FVector(0.f, 0.f, Ease * 520.f);
		if (!Skin->GetRelativeLocation().Equals(Want, 0.5)) Skin->SetRelativeLocation(Want);
	}

	// Clock hands: long glowing beams turning about the sweep centre
	UStaticMeshComponent* Beams[2] = { HandBeam.Get(), HandBeamTwin.Get() };
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		UStaticMeshComponent* Beam = Beams[Hand];
		if (!Beam) continue;
		const bool bUse = bSweep && (Hand == 0 || bSweepTwin);
		if (Beam->IsVisible() != bUse) Beam->SetVisibility(bUse);
		if (!bUse) continue;
		const float Yaw = SweepYawNow(Hand);
		const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector Mid = FVector(SweepCenter) + Dir * (SweepLength * 0.5f) + FVector(0.f, 0.f, 60.f);
		Beam->SetWorldLocationAndRotation(Mid, FRotationMatrix::MakeFromZ(Dir).Rotator());
		Beam->SetWorldScale3D(FVector(0.5f, 0.5f, SweepLength / 100.f));
	}
}
