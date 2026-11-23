// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQAugments.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "SQQuests.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQGameMode.h"
#include "SQPlayerState.h"
#include "SQCharacter.h"
#include "SQHordeManager.h"
#include "SQChest.h"
#include "SQShrine.h"
#include "SQMission.h"

// Unity build: every file-scope name here carries an SQAug prefix.
namespace SQAugData
{
	struct FSQAugTables
	{
		TArray<FSQAugmentDef> Augments;
		FString Errors;
		bool bLoaded = false;
	};

	FSQAugTables& SQAugTables()
	{
		static FSQAugTables Tables;
		return Tables;
	}

	ESQAugmentEffect SQAugParseEffect(const FString& Text)
	{
		static const TCHAR* Names[] = {
			TEXT("None"), TEXT("Stat"), TEXT("KillBlast"), TEXT("HitSplit"), TEXT("KillRefund"), TEXT("WeaponSlot"), TEXT("TomeSlot"),
			TEXT("Gravestones"), TEXT("QuickRevive"), TEXT("AuraStat"), TEXT("Execute"), TEXT("Overkill"), TEXT("KillHeal"), TEXT("KillGold"),
			TEXT("EliteHunter"), TEXT("LowHPDamage"), TEXT("CastNova"), TEXT("CastHaste"), TEXT("HurtNova"), TEXT("Pardon"), TEXT("LevelUps"),
			TEXT("Rerolls"), TEXT("ChestNow"), TEXT("XPBoost") };
		const FString Clean = Text.TrimStartAndEnd();
		for (int32 NameIdx = 0; NameIdx < static_cast<int32>(UE_ARRAY_COUNT(Names)); ++NameIdx)
		{
			if (Clean.Equals(Names[NameIdx], ESearchCase::IgnoreCase)) return static_cast<ESQAugmentEffect>(NameIdx);
		}
		return ESQAugmentEffect::None;
	}

	void SQAugLoad(FSQAugTables& Tables)
	{
		Tables.Augments.Reset();
		Tables.Errors.Reset();
		Tables.bLoaded = true;
		TArray<TMap<FString, FString>> Rows;
		if (!SQQuests::ReadCsvRows(TEXT("Augments.csv"), Rows, Tables.Errors)) return;
		for (const TMap<FString, FString>& Row : Rows)
		{
			FSQAugmentDef Def;
			Def.Id = FName(*SQQuests::CsvCell(Row, TEXT("Id")));
			if (Def.Id.IsNone()) continue;
			Def.Name = SQQuests::CsvCell(Row, TEXT("Name"));
			Def.Description = SQQuests::CsvCell(Row, TEXT("Description"));
			Def.Tier = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("Tier"), 0.f)), 0, 2));
			Def.Effect = SQAugParseEffect(SQQuests::CsvCell(Row, TEXT("Effect")));
			Def.Value = SQQuests::CsvNum(Row, TEXT("Value"), 0.f);
			Def.Value2 = SQQuests::CsvNum(Row, TEXT("Value2"), 0.f);
			Def.Value3 = SQQuests::CsvNum(Row, TEXT("Value3"), 0.f);
			Def.Stat = FName(*SQQuests::CsvCell(Row, TEXT("Stat")));
			Def.Stat2 = FName(*SQQuests::CsvCell(Row, TEXT("Stat2")));
			Def.bCoopOnly = SQQuests::CsvNum(Row, TEXT("CoopOnly"), 0.f) > 0.5f;
			Def.MaxStacks = FMath::Clamp(FMath::RoundToInt(SQQuests::CsvNum(Row, TEXT("MaxStacks"), 1.f)), 0, 99);
			Def.Weight = FMath::Max(0.f, SQQuests::CsvNum(Row, TEXT("Weight"), 1.f));
			if (Def.Effect == ESQAugmentEffect::None) Tables.Errors += FString::Printf(TEXT("Augment %s: unknown Effect. "), *Def.Id.ToString());
			FSQStats Probe;
			if ((Def.Effect == ESQAugmentEffect::Stat || Def.Effect == ESQAugmentEffect::AuraStat) && !SQ::ApplyStat(Probe, Def.Stat, 0.f))
			{
				Tables.Errors += FString::Printf(TEXT("Augment %s: unknown Stat %s. "), *Def.Id.ToString(), *Def.Stat.ToString());
			}
			Tables.Augments.Add(Def);
		}
	}

	FSQAugTables& SQAugGet()
	{
		FSQAugTables& Tables = SQAugTables();
		if (!Tables.bLoaded) SQAugLoad(Tables);
		return Tables;
	}

	/** Silver / Gold / Prism odds per offer source */
	void SQAugTierWeights(ESQAugmentSource Source, float OutWeights[3])
	{
		switch (Source)
		{
		case ESQAugmentSource::Miniboss1: OutWeights[0] = 35.f; OutWeights[1] = 50.f; OutWeights[2] = 15.f; break;
		case ESQAugmentSource::Miniboss2: OutWeights[0] = 15.f; OutWeights[1] = 50.f; OutWeights[2] = 35.f; break;
		case ESQAugmentSource::Objective: OutWeights[0] = 40.f; OutWeights[1] = 45.f; OutWeights[2] = 15.f; break;
		default:                          OutWeights[0] = 60.f; OutWeights[1] = 35.f; OutWeights[2] = 5.f; break;
		}
	}

	const ASQPlayerState* SQAugPS(const ASQCharacter* C)
	{
		return C ? C->GetPlayerState<ASQPlayerState>() : nullptr;
	}
}

// ================================================================ data

USQAugments* USQAugments::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<USQAugments>() : nullptr;
}

bool USQAugments::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USQAugments::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USQAugments, STATGROUP_Tickables);
}

const TArray<FSQAugmentDef>& USQAugments::Augments() { return SQAugData::SQAugGet().Augments; }
const FString& USQAugments::LoadErrors() { return SQAugData::SQAugGet().Errors; }
void USQAugments::Reload() { SQAugData::SQAugLoad(SQAugData::SQAugTables()); }

const FSQAugmentDef* USQAugments::FindAugment(FName Id)
{
	return Augments().FindByPredicate([Id](const FSQAugmentDef& Each) { return Each.Id == Id; });
}

FString USQAugments::TierName(uint8 Tier)
{
	return Tier >= 2 ? FString(TEXT("PRISM")) : (Tier == 1 ? FString(TEXT("GOLD")) : FString(TEXT("SILVER")));
}

FString USQAugments::Describe(FName Id)
{
	const FSQAugmentDef* Def = FindAugment(Id);
	if (!Def) return Id.ToString();
	return FString::Printf(TEXT("%s - %s: %s"), *TierName(Def->Tier), *Def->Name, *Def->Description);
}

int32 USQAugments::CountOwned(const ASQPlayerState* PS, FName Id)
{
	if (!PS) return 0;
	int32 Count = 0;
	for (const FName& Owned : PS->OwnedAugments) Count += Owned == Id ? 1 : 0;
	return Count;
}

float USQAugments::AugmentDamage(const ASQCharacter* By, float Base)
{
	const USQData* D = By ? USQData::Get(By) : nullptr;
	const ASQGameState* GS = By && By->GetWorld() ? By->GetWorld()->GetGameState<ASQGameState>() : nullptr;
	const float PerLevel = D ? FMath::Max(0.f, D->Tune(TEXT("AugmentDamagePerLevel"), 0.06f)) : 0.06f;
	const float LevelScale = 1.f + PerLevel * static_cast<float>(FMath::Max(0, GS ? GS->TeamLevel - 1 : 0));
	return FMath::Max(0.f, Base) * LevelScale * (By ? FMath::Max(0.1f, By->Stats.DamageMult) : 1.f);
}

// ================================================================ hooks

void USQAugments::OnKill(ASQHordeManager* Horde, ASQCharacter* By, const FVector2D& Pos, int32 EnemyType, uint8 EcoFlags)
{
	UWorld* W = Horde ? Horde->GetWorld() : nullptr;
	if (!W || W->GetNetMode() == NM_Client) return;
	// The mission director counts minibosses (augment offers) and hunt kills
	if (ASQGameMode* GM = W->GetAuthGameMode<ASQGameMode>()) GM->GetDirector().NotifyKill(EnemyType, EcoFlags);

	const ASQPlayerState* PS = SQAugData::SQAugPS(By);
	if (!PS || PS->OwnedAugments.Num() == 0) return;
	USQAugments* Self = Get(W);
	ASQGameState* GS = W->GetGameState<ASQGameState>();
	for (const FName& OwnedId : PS->OwnedAugments)
	{
		const FSQAugmentDef* Def = FindAugment(OwnedId);
		if (!Def) continue;
		switch (Def->Effect)
		{
		case ESQAugmentEffect::KillBlast:
			if (Self && FMath::FRand() < Def->Value) Self->QueueBlast(Pos, Def->Value2, AugmentDamage(By, Def->Value3), By);
			break;
		case ESQAugmentEffect::KillRefund:
		{
			// Capped per second (AugmentRefundPerSecond): at 10+ kills a second an uncapped refund kept the dash (and its
			// i-frames) permanently ready
			float Cut = FMath::Max(0.f, Def->Value);
			if (Self)
			{
				if (FReaperState* RefundState = Self->States.Find(TWeakObjectPtr<ASQCharacter>(By)))
				{
					Cut = FMath::Min(Cut, RefundState->DashRefundBudget);
					RefundState->DashRefundBudget -= Cut;
				}
			}
			By->DashCooldownLeft = FMath::Max(0.f, By->DashCooldownLeft - Cut);
			break;
		}
		case ESQAugmentEffect::KillHeal:
			if (!By->bDowned && FMath::FRand() < Def->Value2 && !(GS && GS->ModAny(&FSQModifierDef::bNoHealing)))
			{
				By->Health = FMath::Min(By->Stats.MaxHealth, By->Health + Def->Value);
			}
			break;
		case ESQAugmentEffect::KillGold:
			if (FMath::FRand() < Def->Value)
			{
				ASQPlayerState* GoldPS = By->GetPlayerState<ASQPlayerState>();
				if (GoldPS) GoldPS->AddGold(FMath::Max(1, FMath::RoundToInt(Def->Value2 * (GS ? GS->GoldNightGrowth() : 1.f))));
			}
			break;
		default:
			break;
		}
	}
}

float USQAugments::OnHit(ASQCharacter* By, float Damage, int32 EnemyType, float EnemyHealth, float EnemyMaxHealth, const FVector2D& Pos, uint8 EcoFlags)
{
	if (!(Damage > 0.f) || !By) return Damage;
	UWorld* W = By->GetWorld();
	if (!W || W->GetNetMode() == NM_Client) return Damage;
	const ASQPlayerState* PS = SQAugData::SQAugPS(By);
	float Out = Damage;
	if (PS && PS->OwnedAugments.Num() > 0)
	{
		const USQData* D = USQData::Get(By);
		const bool bTough = (EcoFlags & (ASQHordeManager::EcoCrowned | ASQHordeManager::EcoMiniboss)) != 0
			|| (D && D->Enemies.IsValidIndex(EnemyType) && D->Enemies[EnemyType].Behaviour == ESQEnemyBehaviour::Elite);
		USQAugments* Self = Get(W);
		// Multipliers first, then the effects that read the final hit
		for (const FName& OwnedId : PS->OwnedAugments)
		{
			const FSQAugmentDef* Def = FindAugment(OwnedId);
			if (!Def) continue;
			if (Def->Effect == ESQAugmentEffect::EliteHunter && bTough) Out *= 1.f + FMath::Max(0.f, Def->Value);
			else if (Def->Effect == ESQAugmentEffect::LowHPDamage && By->Stats.MaxHealth > 0.f && By->Health < By->Stats.MaxHealth * Def->Value2) Out *= 1.f + FMath::Max(0.f, Def->Value);
		}
		for (const FName& OwnedId : PS->OwnedAugments)
		{
			const FSQAugmentDef* Def = FindAugment(OwnedId);
			if (!Def) continue;
			switch (Def->Effect)
			{
			case ESQAugmentEffect::Execute:
			{
				const float Left = EnemyHealth - Out;
				if ((EcoFlags & ASQHordeManager::EcoMiniboss) == 0 && Left > 0.f && Left < EnemyMaxHealth * Def->Value) Out = EnemyHealth + 1.f;
				break;
			}
			case ESQAugmentEffect::Overkill:
				if (Self && Out > EnemyHealth + 1.f) Self->QueueBlast(Pos, Def->Value2, (Out - EnemyHealth) * Def->Value, By);
				break;
			case ESQAugmentEffect::HitSplit:
				if (Self && FMath::FRand() < Def->Value) Self->QueueBolts(Pos, Out * Def->Value2, FMath::Max(1, FMath::RoundToInt(Def->Value3)), By);
				break;
			default:
				break;
			}
		}
	}
	// Spawner nests near the hit take a share of it (mission objective)
	if (ASQGameMode* GM = W->GetAuthGameMode<ASQGameMode>()) GM->GetDirector().NotifyHitAt(Pos, Out);
	return Out;
}

void USQAugments::NotifyCast(ASQCharacter* Caster, uint8 Slot)
{
	if (!Caster || !Caster->HasAuthority()) return;
	const ASQPlayerState* PS = SQAugData::SQAugPS(Caster);
	if (!PS || PS->OwnedAugments.Num() == 0) return;
	USQAugments* Self = Get(Caster);
	for (const FName& OwnedId : PS->OwnedAugments)
	{
		const FSQAugmentDef* Def = FindAugment(OwnedId);
		if (!Def) continue;
		if (Def->Effect == ESQAugmentEffect::CastNova && Self)
		{
			Self->QueueBlast(Caster->Pos2D(), Def->Value2, AugmentDamage(Caster, Def->Value3), Caster);
		}
		else if (Def->Effect == ESQAugmentEffect::CastHaste)
		{
			if (USQBoostLedger* Ledger = USQBoostLedger::Get(Caster)) Ledger->AddBoost(Caster, TEXT("MoveSpeed"), Def->Value, FMath::Max(0.1f, Def->Value2));
		}
	}
}

void USQAugments::ModifyStats(const ASQCharacter* C, FSQStats& Raw)
{
	const ASQPlayerState* PS = SQAugData::SQAugPS(C);
	if (!PS) return;
	// Death's Office meta upgrades (sent by the owner's PC: ASQPlayerState::ServerSetMetaRanks)
	for (const FSQMetaUpgradeDef& Meta : USQMissions::MetaUpgrades())
	{
		const int32 Rank = PS->GetMetaRank(Meta.Id);
		if (Rank > 0) SQ::ApplyStat(Raw, Meta.Stat, Meta.PerRank * static_cast<float>(Rank)); // special lines (XPGain...) are not stats: ignored
	}
	// Stat augments
	for (const FName& OwnedId : PS->OwnedAugments)
	{
		const FSQAugmentDef* Def = FindAugment(OwnedId);
		if (!Def || Def->Effect != ESQAugmentEffect::Stat) continue;
		if (!Def->Stat.IsNone()) SQ::ApplyStat(Raw, Def->Stat, Def->Value);
		if (!Def->Stat2.IsNone()) SQ::ApplyStat(Raw, Def->Stat2, Def->Value2);
	}
	// Team auras this reaper stands in (worked out by Tick every 0.5 s)
	const USQAugments* Self = C ? Get(C) : nullptr;
	if (Self)
	{
		if (const FReaperState* State = Self->States.Find(TWeakObjectPtr<ASQCharacter>(const_cast<ASQCharacter*>(C))))
		{
			for (const FName& AuraId : State->Auras)
			{
				const FSQAugmentDef* Def = FindAugment(AuraId);
				if (Def && !Def->Stat.IsNone()) SQ::ApplyStat(Raw, Def->Stat, Def->Value);
			}
		}
	}
}

int32 USQAugments::ExtraSlots(const ASQCharacter* C, bool bWeapon)
{
	const ASQPlayerState* PS = SQAugData::SQAugPS(C);
	if (!PS) return 0;
	int32 Extra = 0;
	for (const FName& OwnedId : PS->OwnedAugments)
	{
		const FSQAugmentDef* Def = FindAugment(OwnedId);
		if (!Def) continue;
		if ((bWeapon && Def->Effect == ESQAugmentEffect::WeaponSlot) || (!bWeapon && Def->Effect == ESQAugmentEffect::TomeSlot)) Extra += FMath::Max(0, FMath::RoundToInt(Def->Value));
	}
	return FMath::Clamp(Extra, 0, 4);
}

float USQAugments::GetKillCooldownRefund(const ASQCharacter* C)
{
	const ASQPlayerState* PS = SQAugData::SQAugPS(C);
	if (!PS) return 0.f;
	float Refund = 0.f;
	for (const FName& OwnedId : PS->OwnedAugments)
	{
		const FSQAugmentDef* Def = FindAugment(OwnedId);
		if (Def && Def->Effect == ESQAugmentEffect::KillRefund) Refund += FMath::Max(0.f, Def->Value);
	}
	// Remake balance pass: refunds come out of a per-second budget (AugmentRefundPerSecond, refilled by TickReapers),
	// so cooldowns run at most (1 + budget) x as fast however many kills land (no E / R spam loop)
	if (Refund > 0.f)
	{
		if (USQAugments* Self = Get(C))
		{
			if (FReaperState* RefundState = Self->States.Find(TWeakObjectPtr<ASQCharacter>(const_cast<ASQCharacter*>(C))))
			{
				Refund = FMath::Min(Refund, RefundState->AbilityRefundBudget);
				RefundState->AbilityRefundBudget -= Refund;
			}
		}
	}
	return Refund;
}

float USQAugments::TeamXPBonus(const UWorld* World)
{
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!GS) return 0.f;
	float MetaSum = 0.f;
	int32 Humans = 0;
	float AugmentSum = 0.f;
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		const ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS);
		if (!EachSQ) continue;
		if (!EachSQ->IsABot())
		{
			Humans++;
			MetaSum += EachSQ->GetMetaValue(TEXT("XPGain"));
		}
		for (const FName& OwnedId : EachSQ->OwnedAugments)
		{
			const FSQAugmentDef* Def = FindAugment(OwnedId);
			if (Def && Def->Effect == ESQAugmentEffect::XPBoost) AugmentSum += FMath::Max(0.f, Def->Value);
		}
	}
	return FMath::Clamp((Humans > 0 ? MetaSum / static_cast<float>(Humans) : 0.f) + AugmentSum, 0.f, 5.f);
}

// ================================================================ offers

void USQAugments::GrantAugmentPickAll(UWorld* World, ESQAugmentSource Source)
{
	ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!GS || World->GetNetMode() == NM_Client || Augments().Num() == 0) return;
	for (APlayerState* EachPS : GS->PlayerArray)
	{
		if (ASQPlayerState* EachSQ = Cast<ASQPlayerState>(EachPS)) EachSQ->GrantAugmentPick(static_cast<uint8>(Source));
	}
}

void USQAugments::RollOffer(const ASQPlayerState* PS, ESQAugmentSource Source, const TArray<FName>& Avoid, TArray<FName>& OutIds, TArray<uint8>& OutTiers)
{
	OutIds.Reset();
	OutTiers.Reset();
	const UWorld* W = PS ? PS->GetWorld() : nullptr;
	const ASQGameState* GS = W ? W->GetGameState<ASQGameState>() : nullptr;
	const bool bCoop = GS && GS->PlayerArray.Num() >= 2;
	float TierWeights[3];
	SQAugData::SQAugTierWeights(Source, TierWeights);

	TArray<const FSQAugmentDef*> Pool;
	for (const FSQAugmentDef& Def : Augments())
	{
		if (Def.Weight <= 0.f || Def.Effect == ESQAugmentEffect::None) continue;
		if (Def.bCoopOnly && !bCoop) continue;
		if (Def.MaxStacks > 0 && CountOwned(PS, Def.Id) >= Def.MaxStacks) continue;
		Pool.Add(&Def);
	}
	for (int32 Pick = 0; Pick < 3 && Pool.Num() > 0; ++Pick)
	{
		// Tier first (only tiers that still have cards), then a card inside it by Weight (a reroll avoids the last set)
		float TierTotal = 0.f;
		float Live[3] = { 0.f, 0.f, 0.f };
		for (const FSQAugmentDef* Each : Pool) Live[Each->Tier] = TierWeights[Each->Tier];
		for (const float Each : Live) TierTotal += Each;
		uint8 Tier = 0;
		if (TierTotal > 0.f)
		{
			float TierRoll = FMath::FRandRange(0.f, TierTotal);
			for (uint8 TierIdx = 0; TierIdx < 3; ++TierIdx)
			{
				TierRoll -= Live[TierIdx];
				if (TierRoll <= 0.f && Live[TierIdx] > 0.f) { Tier = TierIdx; break; }
				Tier = TierIdx;
			}
		}
		float CardTotal = 0.f;
		for (const FSQAugmentDef* Each : Pool)
		{
			if (Each->Tier == Tier) CardTotal += Each->Weight * (Avoid.Contains(Each->Id) ? 0.15f : 1.f);
		}
		int32 Chosen = INDEX_NONE;
		float CardRoll = FMath::FRandRange(0.f, CardTotal);
		for (int32 PoolIdx = 0; PoolIdx < Pool.Num(); ++PoolIdx)
		{
			if (Pool[PoolIdx]->Tier != Tier) continue;
			Chosen = PoolIdx;
			CardRoll -= Pool[PoolIdx]->Weight * (Avoid.Contains(Pool[PoolIdx]->Id) ? 0.15f : 1.f);
			if (CardRoll <= 0.f) break;
		}
		if (Chosen == INDEX_NONE) Chosen = Pool.Num() - 1;
		OutIds.Add(Pool[Chosen]->Id);
		OutTiers.Add(Pool[Chosen]->Tier);
		Pool.RemoveAt(Chosen);
	}
}

void USQAugments::ApplyPicked(ASQPlayerState* PS, FName Id)
{
	const FSQAugmentDef* Def = FindAugment(Id);
	if (!PS || !Def || !PS->HasAuthority()) return;
	ASQCharacter* C = PS->GetPawn<ASQCharacter>();
	switch (Def->Effect)
	{
	case ESQAugmentEffect::Pardon:
	{
		// Remake balance pass: never more than MaxPardons banked revives (meta + augment), whatever the stacks
		const USQData* PardonData = USQData::Get(PS);
		const int32 PardonCap = FMath::Clamp(FMath::RoundToInt(PardonData ? PardonData->Tune(TEXT("MaxPardons"), 2.f) : 2.f), 1, 9);
		if (C) C->Pardons = FMath::Min(C->Pardons + FMath::Max(1, FMath::RoundToInt(Def->Value)), PardonCap);
		break;
	}
	case ESQAugmentEffect::WeaponSlot:
	case ESQAugmentEffect::TomeSlot:
		// The new slot comes with Value2 free level-up picks to fill it (a bare slot is a trap in a 15-20 minute run)
		for (int32 SlotPickIdx = 0; SlotPickIdx < FMath::Clamp(FMath::RoundToInt(Def->Value2), 0, 5); ++SlotPickIdx) PS->GrantLevelUp();
		break;
	case ESQAugmentEffect::LevelUps:
		for (int32 PickIdx = 0; PickIdx < FMath::Clamp(FMath::RoundToInt(Def->Value), 0, 5); ++PickIdx) PS->GrantLevelUp();
		break;
	case ESQAugmentEffect::Rerolls:
		PS->RerollsLeft = FMath::Min(PS->RerollsLeft + FMath::Max(0, FMath::RoundToInt(Def->Value)), 99);
		break;
	case ESQAugmentEffect::ChestNow:
		for (int32 ChestIdx = 0; ChestIdx < FMath::Clamp(FMath::RoundToInt(Def->Value), 0, 5); ++ChestIdx) ASQChest::GrantChestRoll(PS, true, false);
		break;
	default:
		break;
	}
	if (C)
	{
		C->RecomputeStats();
		C->MulticastFX(0, C->GetActorLocation(), 260.f, Def->Tier >= 2 ? FColor(200, 140, 255) : (Def->Tier == 1 ? FColor(255, 200, 70) : FColor(220, 225, 235)), 0.6f);
	}
}

// ================================================================ tick (server)

void USQAugments::QueueBlast(const FVector2D& Pos, float Radius, float Damage, ASQCharacter* By)
{
	if (!(Damage > 0.f) || Blasts.Num() >= 256) return;
	// Chain cap (remake balance pass): a blast's kills / overkill may queue one more generation at most
	// (AugmentChainMaxDepth); past it the chain stops, so Chain Reaction + Powder Keg can't wipe the map
	const USQData* ChainData = USQData::Get(GetWorld());
	const int32 MaxDepth = FMath::Clamp(FMath::RoundToInt(ChainData ? ChainData->Tune(TEXT("AugmentChainMaxDepth"), 1.f) : 1.f), 0, 8);
	if (ActiveBlastDepth > MaxDepth) return;
	FPendingBlast& Blast = Blasts.AddDefaulted_GetRef();
	Blast.Depth = ActiveBlastDepth;
	Blast.Pos = Pos;
	Blast.Radius = FMath::Clamp(Radius, 40.f, 1500.f);
	Blast.Damage = FMath::Min(Damage, 1.0e7f);
	Blast.By = By;
}

void USQAugments::QueueBolts(const FVector2D& Pos, float Damage, int32 Count, ASQCharacter* By)
{
	for (int32 BoltIdx = 0; BoltIdx < Count && Bolts.Num() < 256; ++BoltIdx)
	{
		FPendingBolt& Bolt = Bolts.AddDefaulted_GetRef();
		Bolt.Pos = Pos;
		Bolt.Damage = FMath::Min(Damage, 1.0e7f);
		Bolt.By = By;
	}
}

void USQAugments::ProcessQueues()
{
	UWorld* W = GetWorld();
	ASQGameState* GS = W ? W->GetGameState<ASQGameState>() : nullptr;
	ASQHordeManager* Horde = GS ? GS->Horde.Get() : nullptr;
	if (!Horde)
	{
		Blasts.Reset();
		Bolts.Reset();
		return;
	}
	const USQData* D = USQData::Get(W);
	const int32 Cap = FMath::Clamp(FMath::RoundToInt(D ? D->Tune(TEXT("AugmentMaxBlastsPerFrame"), 32.f) : 32.f), 1, 256);
	// Take this frame's batch first: blasts that kill queue new ones for next frame (chain reactions spread out)
	TArray<FPendingBlast> BlastBatch;
	const int32 BlastCount = FMath::Min(Cap, Blasts.Num());
	BlastBatch.Append(Blasts.GetData(), BlastCount);
	Blasts.RemoveAt(0, BlastCount);
	for (const FPendingBlast& Blast : BlastBatch)
	{
		ASQCharacter* By = Blast.By.Get();
		ActiveBlastDepth = Blast.Depth + 1;
		Horde->ApplyAreaDamage(Blast.Pos, Blast.Radius, Blast.Damage, By, 0.f);
		ActiveBlastDepth = 0;
		if (By && FxThisFrame < 6)
		{
			FxThisFrame++;
			By->MulticastFX(0, FVector(Blast.Pos.X, Blast.Pos.Y, By->GetActorLocation().Z - 60.0), Blast.Radius, FColor(255, 150, 60), 0.35f);
		}
	}
	TArray<FPendingBolt> BoltBatch;
	const int32 BoltCount = FMath::Min(Cap, Bolts.Num());
	BoltBatch.Append(Bolts.GetData(), BoltCount);
	Bolts.RemoveAt(0, BoltCount);
	for (const FPendingBolt& Bolt : BoltBatch)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const FVector2D Vel(FMath::Cos(Angle) * 1400.f, FMath::Sin(Angle) * 1400.f);
		Horde->SpawnProjectile(Bolt.Pos + Vel * 0.03f, Vel, Bolt.Damage, 28.f, 0.55f, 1, false, Bolt.By.Get());
	}
}

void USQAugments::Tick(float DeltaTime)
{
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_Client) return;
	const ASQGameState* GS = W->GetGameState<ASQGameState>();
	if (!GS || (GS->Phase != ESQPhase::Night && GS->Phase != ESQPhase::Boss))
	{
		// Lobby / end screen: nothing runs, and a new run starts clean
		Blasts.Reset();
		Bolts.Reset();
		if (GS && GS->Phase == ESQPhase::Waiting) States.Reset();
		return;
	}
	FxThisFrame = 0;
	ProcessQueues();
	TickReapers(DeltaTime);
}

void USQAugments::TickReapers(float DeltaTime)
{
	UWorld* W = GetWorld();
	const USQData* D = USQData::Get(W);
	if (!W || !D) return;

	TArray<ASQCharacter*> Reapers;
	for (TActorIterator<ASQCharacter> It(W); It; ++It) Reapers.Add(*It);
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}

	const float OrbitTick = FMath::Max(0.1f, D->Tune(TEXT("AugmentOrbitTick"), 0.4f));
	const float RefundRate = FMath::Max(0.f, D->Tune(TEXT("AugmentRefundPerSecond"), 1.f));
	AuraTimer -= DeltaTime;
	const bool bAuraPass = AuraTimer <= 0.f;
	if (bAuraPass) AuraTimer = 0.5f;

	for (ASQCharacter* C : Reapers)
	{
		FReaperState& State = States.FindOrAdd(TWeakObjectPtr<ASQCharacter>(C));
		const ASQPlayerState* PS = C->GetPlayerState<ASQPlayerState>();
		// Kill refund budgets: refill RefundRate seconds per second, hold at most one second's worth
		State.AbilityRefundBudget = FMath::Min(RefundRate, State.AbilityRefundBudget + RefundRate * DeltaTime);
		State.DashRefundBudget = FMath::Min(RefundRate, State.DashRefundBudget + RefundRate * DeltaTime);

		// ---- Owner effects: orbiting gravestones, hurt nova
		if (PS && !C->bDowned)
		{
			for (const FName& OwnedId : PS->OwnedAugments)
			{
				const FSQAugmentDef* Def = FindAugment(OwnedId);
				if (!Def) continue;
				if (Def->Effect == ESQAugmentEffect::Gravestones)
				{
					State.OrbitAngle = FMath::Fmod(State.OrbitAngle + DeltaTime * 2.4f, 2.f * PI);
					State.OrbitTimer -= DeltaTime;
					State.OrbitFxTimer -= DeltaTime;
					if (State.OrbitTimer <= 0.f)
					{
						State.OrbitTimer = OrbitTick;
						const int32 Stones = FMath::Clamp(FMath::RoundToInt(Def->Value), 1, 12);
						const bool bFx = State.OrbitFxTimer <= 0.f;
						if (bFx) State.OrbitFxTimer = 0.8f;
						for (int32 StoneIdx = 0; StoneIdx < Stones; ++StoneIdx)
						{
							const float Angle = State.OrbitAngle + 2.f * PI * static_cast<float>(StoneIdx) / static_cast<float>(Stones);
							const FVector2D StonePos = C->Pos2D() + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Def->Value2;
							QueueBlast(StonePos, 90.f, AugmentDamage(C, Def->Value3), C);
							if (bFx && FxThisFrame < 6)
							{
								FxThisFrame++;
								C->MulticastFX(0, FVector(StonePos.X, StonePos.Y, C->GetActorLocation().Z - 60.0), 90.f, FColor(170, 170, 185), 0.3f);
							}
						}
					}
				}
				else if (Def->Effect == ESQAugmentEffect::HurtNova)
				{
					State.HurtCooldown = FMath::Max(0.f, State.HurtCooldown - DeltaTime);
					if (State.LastHealth >= 0.f && C->Health < State.LastHealth - 0.5f && State.HurtCooldown <= 0.f)
					{
						State.HurtCooldown = FMath::Max(0.25f, Def->Value2);
						QueueBlast(C->Pos2D(), Def->Value, AugmentDamage(C, Def->Value3), C);
					}
				}
			}
		}
		State.LastHealth = C->bDowned ? -1.f : C->Health;

		// ---- Team auras (owner included), refreshed twice a second; a change re-runs RecomputeStats -> ModifyStats
		if (bAuraPass)
		{
			TArray<FName> AurasNow;
			for (const ASQCharacter* Owner : Reapers)
			{
				const ASQPlayerState* OwnerPS = Owner ? Owner->GetPlayerState<ASQPlayerState>() : nullptr;
				if (!OwnerPS || Owner->bDowned) continue;
				for (const FName& OwnedId : OwnerPS->OwnedAugments)
				{
					const FSQAugmentDef* Def = FindAugment(OwnedId);
					if (!Def || Def->Effect != ESQAugmentEffect::AuraStat) continue;
					if (FVector::Dist2D(Owner->GetActorLocation(), C->GetActorLocation()) <= Def->Value2) AurasNow.Add(OwnedId);
				}
			}
			AurasNow.Sort(FNameLexicalLess());
			if (AurasNow != State.Auras)
			{
				State.Auras = AurasNow;
				C->RecomputeStats();
			}
		}

		// ---- Field revive: a standing teammate with Field Medic (meta) or Swift Last Rites (augment) next to a downed reaper
		if (C->bDowned && !C->IsPardonPending() && C->CarriedBy == nullptr)
		{
			float BestSeconds = 0.f;
			float BestFraction = FMath::Clamp(D->Tune(TEXT("ReviveFraction"), 0.5f), 0.05f, 1.f);
			ASQCharacter* Reviver = nullptr;
			for (ASQCharacter* Helper : Reapers)
			{
				if (Helper == C || Helper->bDowned || FVector::Dist2D(Helper->GetActorLocation(), C->GetActorLocation()) > 260.f) continue;
				const ASQPlayerState* HelperPS = Helper->GetPlayerState<ASQPlayerState>();
				if (!HelperPS) continue;
				float Seconds = HelperPS->GetMetaValue(TEXT("ReviveSeconds"));
				float Fraction = BestFraction;
				for (const FName& OwnedId : HelperPS->OwnedAugments)
				{
					const FSQAugmentDef* Def = FindAugment(OwnedId);
					if (Def && Def->Effect == ESQAugmentEffect::QuickRevive && Def->Value > 0.f && (Seconds <= 0.f || Def->Value < Seconds))
					{
						Seconds = Def->Value;
						Fraction = FMath::Clamp(Def->Value2, 0.05f, 1.f);
					}
				}
				if (Seconds > 0.f && (BestSeconds <= 0.f || Seconds < BestSeconds))
				{
					BestSeconds = Seconds;
					BestFraction = Fraction;
					Reviver = Helper;
				}
			}
			if (Reviver)
			{
				State.ReviveProgress += DeltaTime;
				if (State.ReviveProgress >= BestSeconds)
				{
					State.ReviveProgress = 0.f;
					C->Revive(BestFraction);
					if (ASQPlayerState* ReviverPS = Reviver->GetPlayerState<ASQPlayerState>()) ReviverPS->CreditQuest(TEXT("Revives"));
					Reviver->MulticastFX(0, C->GetActorLocation(), 240.f, FColor(140, 255, 190), 0.6f);
				}
			}
			else
			{
				State.ReviveProgress = 0.f;
			}
		}
		else
		{
			State.ReviveProgress = 0.f;
		}
		State.bWasDowned = C->bDowned;
	}
}
