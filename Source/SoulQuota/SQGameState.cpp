// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQGameState.h"
#include "Net/UnrealNetwork.h"
#include "SQPlayerState.h"
#include "SQData.h"
#include "SQBoss.h"
#include "SQLantern.h"
#include "SQHordeManager.h"
#include "SQArena.h"
#include "SQPortal.h"
#include "SQQuests.h"
#include "SQAugments.h"

void ASQGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQGameState, Phase);
	DOREPLIFETIME(ASQGameState, NightIndex);
	DOREPLIFETIME(ASQGameState, PhaseTimeLeft);
	DOREPLIFETIME(ASQGameState, PhaseDuration);
	DOREPLIFETIME(ASQGameState, Quota);
	DOREPLIFETIME(ASQGameState, Banked);
	DOREPLIFETIME(ASQGameState, TrailSites);
	DOREPLIFETIME(ASQGameState, TrailStage);
	DOREPLIFETIME(ASQGameState, TrailMissedMask);
	DOREPLIFETIME(ASQGameState, TrailBanked);
	DOREPLIFETIME(ASQGameState, TrailQuota);
	DOREPLIFETIME(ASQGameState, ShopSouls);
	DOREPLIFETIME(ASQGameState, ShopBuys);
	DOREPLIFETIME(ASQGameState, Defiance);
	DOREPLIFETIME(ASQGameState, bFinalCall);
	DOREPLIFETIME(ASQGameState, TeamLevel);
	DOREPLIFETIME(ASQGameState, TeamXP);
	DOREPLIFETIME(ASQGameState, XPToNext);
	DOREPLIFETIME(ASQGameState, EnemyCount);
	DOREPLIFETIME(ASQGameState, bPickPause);
	DOREPLIFETIME(ASQGameState, PickPauseTimeLeft);
	DOREPLIFETIME(ASQGameState, PickersTotal);
	DOREPLIFETIME(ASQGameState, PickersDone);
	DOREPLIFETIME(ASQGameState, Boss);
	DOREPLIFETIME(ASQGameState, Lantern);
	DOREPLIFETIME(ASQGameState, Horde);
	DOREPLIFETIME(ASQGameState, Arena);
	DOREPLIFETIME(ASQGameState, bEndless);
	DOREPLIFETIME(ASQGameState, LoopIndex);
	DOREPLIFETIME(ASQGameState, EventId);
	DOREPLIFETIME(ASQGameState, EventPos);
	DOREPLIFETIME(ASQGameState, bEventHasPos);
	DOREPLIFETIME(ASQGameState, EventStartTime);
	DOREPLIFETIME(ASQGameState, EventEndTime);
	DOREPLIFETIME(ASQGameState, EventHoldPct);
	DOREPLIFETIME(ASQGameState, EventsDone);
	DOREPLIFETIME(ASQGameState, ClockRate);
	DOREPLIFETIME(ASQGameState, ClockOffset);
	DOREPLIFETIME(ASQGameState, ConditionIds);
	DOREPLIFETIME(ASQGameState, ConditionDistricts);
	DOREPLIFETIME(ASQGameState, bDailyRun);
	DOREPLIFETIME(ASQGameState, LoopChoiceTimeLeft);
	DOREPLIFETIME(ASQGameState, LoopBonusGold);
	DOREPLIFETIME(ASQGameState, LoopBossMod);
	DOREPLIFETIME(ASQGameState, RunStage);
	DOREPLIFETIME(ASQGameState, RunStageCount);
	DOREPLIFETIME(ASQGameState, NextStageName);
	DOREPLIFETIME(ASQGameState, OverflowTimeLeft);
	DOREPLIFETIME(ASQGameState, bSoulRift);
	DOREPLIFETIME(ASQGameState, SoulRiftPos);
	DOREPLIFETIME(ASQGameState, SoulRiftTimeLeft);
	DOREPLIFETIME(ASQGameState, SoulRiftIn);
	DOREPLIFETIME(ASQGameState, SoulRiftNeeded);
	DOREPLIFETIME(ASQGameState, bSoulPassage);
	DOREPLIFETIME(ASQGameState, PassageTimeLeft);
	DOREPLIFETIME(ASQGameState, bEndlessHorde);
	DOREPLIFETIME(ASQGameState, EndlessSeconds);
	DOREPLIFETIME(ASQGameState, EndlessKills);
	DOREPLIFETIME(ASQGameState, RunKills);
	DOREPLIFETIME(ASQGameState, RunSoulsBanked);
	DOREPLIFETIME(ASQGameState, BossTideStart);
	DOREPLIFETIME(ASQGameState, BossTideEnd);
	DOREPLIFETIME(ASQGameState, BossTideRise);
	DOREPLIFETIME(ASQGameState, AlertText);
	DOREPLIFETIME(ASQGameState, AlertServerTime);
	DOREPLIFETIME(ASQGameState, StoryText);
	DOREPLIFETIME(ASQGameState, Difficulty);
	DOREPLIFETIME(ASQGameState, ModifierMask);
	DOREPLIFETIME(ASQGameState, bFinalSwarm);
	DOREPLIFETIME(ASQGameState, FinalSwarmSeconds);
	DOREPLIFETIME(ASQGameState, BossNightProgress);
	DOREPLIFETIME(ASQGameState, Portal);
	DOREPLIFETIME(ASQGameState, BeatText);
	DOREPLIFETIME(ASQGameState, BeatServerTime);
	DOREPLIFETIME(ASQGameState, BeatKind);
	DOREPLIFETIME(ASQGameState, MinibossName);
	DOREPLIFETIME(ASQGameState, MinibossHealthFrac);
	DOREPLIFETIME(ASQGameState, Objectives);
	DOREPLIFETIME(ASQGameState, StageTime);
	DOREPLIFETIME(ASQGameState, StageDuration);
	DOREPLIFETIME(ASQGameState, MissionId);
	DOREPLIFETIME(ASQGameState, bHardMode);
	DOREPLIFETIME(ASQGameState, bSkipLobby);
	DOREPLIFETIME(ASQGameState, RunResult);
	DOREPLIFETIME(ASQGameState, RunStats);
	DOREPLIFETIME(ASQGameState, bBossReady);
}

void ASQGameState::AddTeamXP(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return;
	const USQData* D = USQData::Get(this);
	const float Base = D ? D->Tune(TEXT("XPBase"), 8.f) : 8.f;
	const float Growth = D ? D->Tune(TEXT("XPGrowth"), 15.f) : 15.f;
	// Co-op: the horde (and its XP) grows per extra player, so does each level, keeping every reaper's level close to a solo run
	const float XPPlayerMult = 1.f + FMath::Max(0.f, D ? D->Tune(TEXT("XPPerPlayer"), 0.35f) : 0.35f) * static_cast<float>(FMath::Max(0, PlayerArray.Num() - 1));

	// Probation clause: no level-ups before its share of the STAGE; the XP waits (ASQGameMode pays it out)
	const float HoldUntil = ModMax(&FSQModifierDef::HoldXP);
	if (HoldUntil > 0.f && Phase == ESQPhase::Night && !bFinalSwarm && StageProgress() < HoldUntil)
	{
		HeldXP = FMath::Min(HeldXP + Amount, 100000000);
		return;
	}

	// Night School (meta) and Study Group (augment): fractional bonus XP carried in XPRemainder
	XPRemainder += static_cast<float>(Amount) * (1.f + USQAugments::TeamXPBonus(GetWorld()));
	const int32 WholeXP = FMath::Clamp(FMath::FloorToInt(XPRemainder), 0, 100000000);
	XPRemainder -= static_cast<float>(WholeXP);
	TeamXP = static_cast<int32>(FMath::Min(static_cast<int64>(TeamXP) + WholeXP, static_cast<int64>(1000000000)));
	const int32 AugmentLevelA = FMath::RoundToInt(D ? D->Tune(TEXT("AugmentLevel1"), 10.f) : 10.f);
	const int32 AugmentLevelB = FMath::RoundToInt(D ? D->Tune(TEXT("AugmentLevel2"), 20.f) : 20.f);
	int32 Safety = 0;
	while (TeamXP >= XPToNext && Safety++ < 50)
	{
		TeamXP -= XPToNext;
		TeamLevel++;
		// Quest stat TeamLevel (Mode Best): +1 per level, so the run's sum is the level reached
		for (APlayerState* LevelPS : PlayerArray)
		{
			if (ASQPlayerState* LevelSQ = Cast<ASQPlayerState>(LevelPS)) LevelSQ->CreditQuest(TEXT("TeamLevel"), 1);
		}
		XPToNext = FMath::Max(1, FMath::RoundToInt(FMath::Min((Base + Growth * TeamLevel) * XPPlayerMult, 1.0e9f))); // bad tuning (<= 0) must not level every frame
		for (APlayerState* PS : PlayerArray)
		{
			if (ASQPlayerState* SQPS = Cast<ASQPlayerState>(PS))
			{
				SQPS->GrantLevelUp();
			}
		}
		// Augment offers at the level thresholds (every reaper; the set opens once the current card set is resolved)
		if (TeamLevel == AugmentLevelA || TeamLevel == AugmentLevelB) USQAugments::GrantAugmentPickAll(GetWorld(), ESQAugmentSource::Level);
	}
}

void ASQGameState::PushAlert(const FString& Text)
{
	if (!HasAuthority()) return;
	AlertText = Text;
	AlertServerTime = GetServerWorldTimeSeconds();
}

void ASQGameState::ResetTeamProgress()
{
	const USQData* D = USQData::Get(this);
	TeamLevel = 1;
	TeamXP = 0;
	XPToNext = FMath::Max(1, FMath::RoundToInt((D ? D->Tune(TEXT("XPBase"), 8.f) : 8.f) + (D ? D->Tune(TEXT("XPGrowth"), 15.f) : 15.f)));
	ShopSouls = 0;
	ShopBuys = 0;
	Banked = 0;
	TrailSites.Reset();
	TrailStage = 0;
	TrailMissedMask = 0;
	TrailBanked = 0;
	TrailQuota = 0;
	Defiance = 0;
	NightIndex = 0;
	bFinalCall = false;
	StoryText.Reset();
	Difficulty = 0.f;
	bFinalSwarm = false;
	FinalSwarmSeconds = 0.f;
	BossNightProgress = 1.f;
	BeatText.Reset();
	BeatServerTime = -100.f;
	BeatKind = 0;
	ClearEventState();
	// Run structure (Nov 25 2026)
	RunStage = 0;
	RunStageCount = 1;
	NextStageName.Reset();
	OverflowTimeLeft = 0.f;
	bSoulRift = false;
	SoulRiftTimeLeft = 0.f;
	SoulRiftIn = 0;
	SoulRiftNeeded = 0;
	bSoulPassage = false;
	PassageTimeLeft = 0.f;
	bEndlessHorde = false;
	EndlessSeconds = 0.f;
	EndlessKills = 0;
	RunKills = 0;
	RunSoulsBanked = 0;
	BossTideStart = 0.f;
	BossTideEnd = 0.f;
	BossTideRise = 0.f;
	EventsDone = 0;
	ConditionIds.Reset();
	ConditionDistricts.Reset();
	MinibossName.Reset();
	MinibossHealthFrac = 0.f;
	HeldXP = 0;
	XPRemainder = 0.f;
	Objectives.Reset();
	StageTime = 0.f;
	StageDuration = 0.f;
	RunResult = ESQRunResult::None;
	RunStats = FSQRunStats();
	bBossReady = false;
	Quota = 0;
}

const FSQMissionDef* ASQGameState::GetMission() const
{
	return USQMissions::FindMission(MissionId);
}

float ASQGameState::StageProgress() const
{
	if (Phase != ESQPhase::Night && Phase != ESQPhase::Boss) return 0.f;
	if (bFinalSwarm) return 1.f;
	return FMath::Clamp(StageTime / FMath::Max(1.f, StageDuration), 0.f, 1.f);
}

float ASQGameState::MissionModValue(float FSQModifierDef::* Field) const
{
	const FSQMissionDef* Mission = GetMission();
	if (!Mission) return 1.f;
	if (Field == &FSQModifierDef::SpawnMult) return Mission->Mult(Mission->SpawnMult, Mission->HardSpawnMult, bHardMode);
	if (Field == &FSQModifierDef::EnemyDamageMult) return Mission->Mult(Mission->DamageMult, Mission->HardDamageMult, bHardMode) * LoopDamageMult();
	if (Field == &FSQModifierDef::EliteMult) return LoopEliteMult();
	if (Field == &FSQModifierDef::GoldMult) return Mission->Mult(Mission->GoldMult, Mission->HardGoldMult, bHardMode);
	return 1.f;
}

float ASQGameState::MissionHealthMult(bool bBoss) const
{
	const FSQMissionDef* Mission = GetMission();
	if (!Mission) return 1.f;
	return (bBoss ? Mission->Mult(Mission->BossHealthMult, Mission->HardBossHealthMult, bHardMode) : Mission->Mult(Mission->HealthMult, Mission->HardHealthMult, bHardMode)) * LoopHealthMult();
}

float ASQGameState::LoopHealthMult() const
{
	if (LoopIndex <= 0) return 1.f;
	const USQData* D = USQData::Get(this);
	const float Per = D ? FMath::Clamp(D->Tune(TEXT("EndlessHealthPerLoop"), 0.6f), 0.f, 5.f) : 0.6f;
	return FMath::Min(1.f + Per * static_cast<float>(LoopIndex), D ? FMath::Max(1.f, D->Tune(TEXT("EndlessHealthCap"), 40.f)) : 40.f);
}

float ASQGameState::LoopDamageMult() const
{
	if (LoopIndex <= 0) return 1.f;
	const USQData* D = USQData::Get(this);
	const float Per = D ? FMath::Clamp(D->Tune(TEXT("EndlessDamagePerLoop"), 0.2f), 0.f, 5.f) : 0.2f;
	return FMath::Min(1.f + Per * static_cast<float>(LoopIndex), D ? FMath::Max(1.f, D->Tune(TEXT("EndlessDamageCap"), 6.f)) : 6.f);
}

float ASQGameState::LoopEliteMult() const
{
	if (LoopIndex <= 0) return 1.f;
	const USQData* D = USQData::Get(this);
	const float Per = D ? FMath::Clamp(D->Tune(TEXT("EndlessElitePerLoop"), 0.5f), 0.f, 5.f) : 0.5f;
	return FMath::Min(1.f + Per * static_cast<float>(LoopIndex), 6.f);
}

float ASQGameState::StageHealthSmoothing() const
{
	if ((Phase != ESQPhase::Night && Phase != ESQPhase::Boss) || StageDuration <= 0.f) return 1.f;
	const USQData* D = USQData::Get(this);
	if (!D) return 1.f;
	// Mirror the horde's step: (1 + R x act) x (1 + r x minutes into the act); R includes the test-length scale like HealthPerNight does
	const float Ratio = FMath::Max(0.f, D->Tune(TEXT("StageHpRefRatio"), 3.f)) * NightScale();
	const float PerMinute = FMath::Max(0.f, D->Tune(TEXT("EnemyHealthPerMinute"), 0.1f)) * FMath::Max(0.f, ModProduct(&FSQModifierDef::HealthRampMult));
	const float ActMinutes = FMath::Max(1.f, PhaseDuration) / 60.f;
	const int32 Act = FMath::Clamp(NightIndex, 0, 2);
	const float Into = NightProgress();
	const float HordeNow = (1.f + Ratio * static_cast<float>(Act)) * (1.f + PerMinute * Into * ActMinutes);
	// Anchors at the act starts (the old night starts) and the stage end
	auto Anchor = [Ratio, PerMinute, ActMinutes](int32 Index)
	{
		if (Index >= 3) return (1.f + Ratio * 2.f) * (1.f + PerMinute * ActMinutes);
		return 1.f + Ratio * static_cast<float>(Index);
	};
	const float FromLog = FMath::Loge(FMath::Max(0.01f, Anchor(Act)));
	const float ToLog = FMath::Loge(FMath::Max(0.01f, Anchor(Act + 1)));
	const float Target = FMath::Exp(FMath::Lerp(FromLog, ToLog, FMath::Clamp(Into, 0.f, 1.f)));
	return FMath::Clamp(Target / FMath::Max(0.01f, HordeNow), 0.2f, 5.f);
}

float ASQGameState::NightProgress() const
{
	// Boss fight: the ramps hold where the night was when the bell rang (1 = rung at / after midnight)
	if (Phase == ESQPhase::Boss) return FMath::Clamp(BossNightProgress, 0.f, 1.f);
	if (Phase != ESQPhase::Night) return 0.f;
	if (bFinalSwarm) return 1.f;
	return FMath::Clamp(1.f - PhaseTimeLeft / FMath::Max(1.f, PhaseDuration), 0.f, 1.f);
}

void ASQGameState::AddDifficulty(float Amount)
{
	if (!HasAuthority() || !FMath::IsFinite(Amount)) return;
	Difficulty = FMath::Clamp(Difficulty + Amount, 0.f, 100.f);
}

void ASQGameState::PushBeat(const FString& Text, uint8 Kind)
{
	if (!HasAuthority()) return;
	BeatText = Text;
	BeatKind = Kind;
	BeatServerTime = GetServerWorldTimeSeconds();
}

float ASQGameState::ModProduct(float FSQModifierDef::* Field) const
{
	float Product = 1.f;
	const TArray<FSQModifierDef>& Mods = SQQuests::Modifiers();
	for (int32 ModIdx = 0; ModIdx < Mods.Num(); ++ModIdx)
	{
		if (HasModifier(ModIdx)) Product *= Mods[ModIdx].*Field;
	}
	// The mission (and Hard mode) ride along like an always-on clause: horde size, enemy damage, gold
	return Product * MissionModValue(Field);
}

float ASQGameState::ModMax(float FSQModifierDef::* Field) const
{
	float Best = 0.f;
	const TArray<FSQModifierDef>& Mods = SQQuests::Modifiers();
	for (int32 ModIdx = 0; ModIdx < Mods.Num(); ++ModIdx)
	{
		if (HasModifier(ModIdx)) Best = FMath::Max(Best, Mods[ModIdx].*Field);
	}
	return Best;
}

bool ASQGameState::ModAny(bool FSQModifierDef::* Field) const
{
	const TArray<FSQModifierDef>& Mods = SQQuests::Modifiers();
	for (int32 ModIdx = 0; ModIdx < Mods.Num(); ++ModIdx)
	{
		if (HasModifier(ModIdx) && Mods[ModIdx].*Field) return true;
	}
	return false;
}

float ASQGameState::CoinMultiplier() const
{
	const USQData* D = USQData::Get(this);
	return SQQuests::CoinMultiplier(ModifierMask, D ? D->Tune(TEXT("ModifierCoinCap"), 3.f) : 3.f);
}

float ASQGameState::NightScale() const
{
	const USQData* D = USQData::Get(this);
	if (!D) return 1.f;
	return FMath::Clamp(D->Tune(TEXT("NightSeconds"), 360.f) / FMath::Max(1.f, D->Tune(TEXT("BalanceNightSeconds"), 360.f)), 0.1f, 4.f);
}

float ASQGameState::GoldMultiplier() const
{
	return FMath::Max(0.f, (1.f + FMath::Max(0.f, Difficulty)) * ModProduct(&FSQModifierDef::GoldMult));
}

float ASQGameState::GoldNightGrowth() const
{
	const USQData* D = USQData::Get(this);
	return 1.f + FMath::Max(0.f, D ? D->Tune(TEXT("GoldPerNight"), 1.5f) : 1.5f) * static_cast<float>(FMath::Clamp(NightIndex, 0, 20));
}

float ASQGameState::EconomyHealthMult(bool bBoss) const
{
	const USQData* D = USQData::Get(this);
	if (!D) return 1.f;
	// Act N (missions: the boss uses act 3's value, it is always the stage's last fight)
	const int32 NightNumber = bBoss ? 3 : FMath::Clamp(NightIndex + 1, 1, 3);
	const FString KeyText = bBoss ? FString::Printf(TEXT("EconomyBossHealth%d"), NightNumber) : FString::Printf(TEXT("EconomyEnemyHealth%d"), NightNumber);
	const FName Key(*KeyText);
	const float Base = FMath::Clamp(D->Tune(Key, 1.f), 0.1f, 20.f) * MissionHealthMult(bBoss);
	return FMath::Clamp(bBoss ? Base : Base * StageHealthSmoothing(), 0.05f, 100.f);
}

int32 ASQGameState::ChestPriceFor(const ASQPlayerState* PS) const
{
	const USQData* D = USQData::Get(this);
	if (!D) return 0;
	const float Opened = static_cast<float>(PS ? FMath::Max(0, PS->ChestsOpened) : 0);
	const float Base = FMath::Max(0.f, D->Tune(TEXT("ChestBasePrice"), 30.f));
	const float Grow = FMath::Pow(1.f + FMath::Max(0.f, D->Tune(TEXT("ChestPriceK"), 1.f)) * Opened, FMath::Clamp(D->Tune(TEXT("ChestPriceExp"), 1.4f), 0.f, 4.f));
	const float Capped = FMath::Min(Base * Grow, FMath::Max(1.f, D->Tune(TEXT("ChestPriceCap"), 1500.f)));
	const float Price = Capped * NightScale() * ModProduct(&FSQModifierDef::ChestPriceMult);
	return FMath::Max(1, FMath::RoundToInt(FMath::Min(Price, 1.0e8f)));
}

int32 ASQGameState::ShopPrice(int32 Index) const
{
	const USQData* D = USQData::Get(this);
	if (!D || !D->Shop.IsValidIndex(Index)) return 0;
	// Megabonk-style escalation (Docs/Balance.md): every buy this visit raises all prices; later nights and bigger teams pay more
	const float PerBuy = FMath::Max(0.f, D->Tune(TEXT("ShopPricePerBuy"), 0.75f));
	const float PerNight = FMath::Max(0.f, D->Tune(TEXT("ShopPricePerNight"), 0.5f));
	const float PerPlayer = FMath::Max(0.f, D->Tune(TEXT("ShopPricePerPlayer"), 0.75f));
	const float Mult = (1.f + PerBuy * static_cast<float>(FMath::Max(0, ShopBuys)))
		* (1.f + PerNight * static_cast<float>(FMath::Max(0, NightIndex)))
		* (1.f + PerPlayer * static_cast<float>(FMath::Max(0, PlayerArray.Num() - 1)))
		* FMath::Clamp(D->Tune(TEXT("NightSeconds"), 360.f) / FMath::Max(1.f, D->Tune(TEXT("BalanceNightSeconds"), 360.f)), 0.1f, 4.f); // short test nights earn less
	return FMath::Max(0, FMath::RoundToInt(FMath::Min(static_cast<float>(FMath::Max(0, D->Shop[Index].Cost)) * Mult, 1.0e9f)));
}

// ---------------------------------------------------------------- ping wheel (Nov 23 2026)

void ASQGameState::AddLocalPing(uint8 Kind, const FVector& Where, uint8 Slot, const FString& Who)
{
	const UWorld* PingWorld = GetWorld();
	const double PingNow = PingWorld ? PingWorld->GetTimeSeconds() : 0.0;
	LocalPings.RemoveAll([PingNow, Slot](const FSQPing& Old) { return PingNow - Old.Time > 8.0 || Old.Slot == Slot; }); // one live ping per player
	if (LocalPings.Num() >= 8) LocalPings.RemoveAt(0);
	FSQPing& Ping = LocalPings.AddDefaulted_GetRef();
	Ping.Where = Where;
	Ping.Who = Who.Left(24);
	Ping.Time = PingNow;
	Ping.Kind = Kind;
	Ping.Slot = Slot;
}

FLinearColor ASQGameState::PingColor(uint8 Slot)
{
	// One colour per player, clear of XP green / gold / soul teal
	static const FLinearColor Colors[4] = {
		FLinearColor(0.35f, 0.72f, 1.f), FLinearColor(1.f, 0.45f, 0.75f), FLinearColor(0.78f, 0.55f, 1.f), FLinearColor(1.f, 0.6f, 0.3f) };
	return Colors[Slot % 4];
}

const TCHAR* ASQGameState::PingLabel(uint8 Kind)
{
	switch (Kind)
	{
	case 1: return TEXT("HELP ME");
	case 2: return TEXT("LANTERN");
	case 3: return TEXT("CHEST");
	case 4: return TEXT("DANGER");
	case 5: return TEXT("THANKS");
	default: return TEXT("HERE");
	}
}

// ============================================================ run events (content item 12)

const FSQEventDef* ASQGameState::ActiveEvent() const
{
	return EventId.IsNone() ? nullptr : SQQuests::FindEvent(EventId);
}

float ASQGameState::EventMult(float FSQEventDef::* Field) const
{
	const FSQEventDef* Ev = ActiveEvent();
	return Ev ? Ev->*Field : 1.f;
}

const FSQEventDef* ASQGameState::ConditionOn(int32 District) const
{
	if (District == INDEX_NONE) return nullptr;
	for (int32 CondIdx = 0; CondIdx < ConditionIds.Num() && CondIdx < ConditionDistricts.Num(); ++CondIdx)
	{
		if (ConditionDistricts[CondIdx] == District) return SQQuests::FindEvent(ConditionIds[CondIdx]);
	}
	return nullptr;
}

float ASQGameState::DropMult(const FVector2D& Pos, float FSQEventDef::* Field) const
{
	float Mult = EventMult(Field);
	if (ConditionIds.Num() > 0 && Arena)
	{
		if (const FSQEventDef* Cond = ConditionOn(Arena->FindDistrictAt(static_cast<float>(Pos.X), static_cast<float>(Pos.Y)))) Mult *= Cond->*Field;
	}
	return FMath::Clamp(Mult, 0.1f, 8.f);
}

bool ASQGameState::IsEventSpecial(ESQEventSpecial Special) const
{
	const FSQEventDef* Ev = ActiveEvent();
	return Ev && Ev->Special == Special;
}

int32 ASQGameState::TodayKey()
{
	const FDateTime Today = FDateTime::Now();
	return Today.GetYear() * 10000 + Today.GetMonth() * 100 + Today.GetDay();
}

void ASQGameState::ClearEventState()
{
	// The clock hands keep their rate / offset only through an event: back to real time without a jump is the game mode's job
	EventId = NAME_None;
	bEventHasPos = false;
	EventHoldPct = 0;
	EventStartTime = 0.f;
	EventEndTime = 0.f;
}
