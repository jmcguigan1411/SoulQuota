// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "SQQuests.h"

namespace
{
	/** Minimal CSV reader: header row, quoted fields allowed, lines starting with # ignored */
	struct FSQTable
	{
		TMap<FString, int32> Columns;
		TArray<TArray<FString>> Rows;

		bool Load(const FString& FileName, FString& Errors)
		{
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / FileName;
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				Errors += FString::Printf(TEXT("Missing %s. "), *FileName);
				return false;
			}

			TArray<TArray<FString>> All;
			TArray<FString> Row;
			FString Cell;
			bool bQuoted = false;
			for (int32 i = 0; i < Text.Len(); ++i)
			{
				const TCHAR C = Text[i];
				if (bQuoted)
				{
					if (C == TEXT('"'))
					{
						if (i + 1 < Text.Len() && Text[i + 1] == TEXT('"')) { Cell.AppendChar(TEXT('"')); ++i; }
						else { bQuoted = false; }
					}
					else { Cell.AppendChar(C); }
				}
				else if (C == TEXT('"')) { bQuoted = true; }
				else if (C == TEXT(',')) { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); }
				else if (C == TEXT('\n'))
				{
					Row.Add(Cell.TrimStartAndEnd()); Cell.Reset();
					All.Add(Row); Row.Reset();
				}
				else if (C != TEXT('\r')) { Cell.AppendChar(C); }
			}
			if (!Cell.IsEmpty() || Row.Num() > 0) { Row.Add(Cell.TrimStartAndEnd()); All.Add(Row); }

			bool bHeader = true;
			for (const TArray<FString>& R : All)
			{
				if (R.Num() == 0 || R[0].IsEmpty() || R[0].StartsWith(TEXT("#"))) continue;
				if (bHeader)
				{
					// First column wins for duplicate header names; blank header cells are ignored
					for (int32 c = 0; c < R.Num(); ++c)
					{
						const FString HeaderName = R[c].ToLower();
						if (!HeaderName.IsEmpty() && !Columns.Contains(HeaderName)) Columns.Add(HeaderName, c);
					}
					bHeader = false;
				}
				else { Rows.Add(R); }
			}
			return true;
		}

		FString S(int32 Row, const TCHAR* Name) const
		{
			const int32* Col = Columns.Find(FString(Name).ToLower());
			return (Col && Rows.IsValidIndex(Row) && Rows[Row].IsValidIndex(*Col)) ? Rows[Row][*Col] : FString();
		}
		float F(int32 Row, const TCHAR* Name, float Default = 0.f) const
		{
			const FString V = S(Row, Name);
			return V.IsEmpty() ? Default : FCString::Atof(*V);
		}
		int32 I(int32 Row, const TCHAR* Name, int32 Default = 0) const
		{
			const FString V = S(Row, Name);
			return V.IsEmpty() ? Default : FMath::RoundToInt(FCString::Atof(*V));
		}
		FName N(int32 Row, const TCHAR* Name) const
		{
			const FString V = S(Row, Name);
			return V.IsEmpty() ? NAME_None : FName(*V);
		}
		FLinearColor C(int32 Row, const TCHAR* Name) const
		{
			const FString V = S(Row, Name);
			return V.IsEmpty() ? FLinearColor::White : FLinearColor::FromSRGBColor(FColor::FromHex(V));
		}
	};

	/** "SQ_Walk_Swagger|Walking_C" -> { SQ_Walk_Swagger, Walking_C } (empty entries skipped) */
	TArray<FName> SplitAnimList(const FString& V)
	{
		TArray<FName> Out;
		TArray<FString> Parts;
		V.ParseIntoArray(Parts, TEXT("|"), true);
		for (const FString& Part : Parts)
		{
			const FString Clean = Part.TrimStartAndEnd();
			if (!Clean.IsEmpty()) Out.Add(FName(*Clean));
		}
		return Out;
	}

	ESQUpgradeKind ParseKind(const FString& V)
	{
		if (V.Equals(TEXT("Relic"), ESearchCase::IgnoreCase)) return ESQUpgradeKind::Relic;
		if (V.Equals(TEXT("Evolution"), ESearchCase::IgnoreCase)) return ESQUpgradeKind::Evolution;
		return ESQUpgradeKind::Weapon;
	}

	/** Upgrades.csv Category; empty or unknown = derived from Kind (Relic -> Tome) */
	ESQUpgradeCategory ParseCategory(const FString& V, ESQUpgradeKind Kind)
	{
		if (V.Equals(TEXT("Weapon"), ESearchCase::IgnoreCase)) return ESQUpgradeCategory::Weapon;
		if (V.Equals(TEXT("Tome"), ESearchCase::IgnoreCase)) return ESQUpgradeCategory::Tome;
		if (V.Equals(TEXT("Item"), ESearchCase::IgnoreCase)) return ESQUpgradeCategory::Item;
		if (V.Equals(TEXT("Evolution"), ESearchCase::IgnoreCase)) return ESQUpgradeCategory::Evolution;
		if (V.Equals(TEXT("Fallback"), ESearchCase::IgnoreCase)) return ESQUpgradeCategory::Fallback;
		if (Kind == ESQUpgradeKind::Relic) return ESQUpgradeCategory::Tome;
		if (Kind == ESQUpgradeKind::Evolution) return ESQUpgradeCategory::Evolution;
		return ESQUpgradeCategory::Weapon;
	}

	ESQWeaponBehaviour ParseBehaviour(const FString& V)
	{
		if (V.Equals(TEXT("Arc"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Arc;
		if (V.Equals(TEXT("Projectile"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Projectile;
		if (V.Equals(TEXT("Aura"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Aura;
		if (V.Equals(TEXT("Orbit"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Orbit;
		if (V.Equals(TEXT("Nova"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Nova;
		if (V.Equals(TEXT("Drop"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Drop;
		if (V.Equals(TEXT("Turret"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Turret;
		if (V.Equals(TEXT("Chain"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Chain;
		if (V.Equals(TEXT("Beam"), ESearchCase::IgnoreCase)) return ESQWeaponBehaviour::Beam;
		return ESQWeaponBehaviour::None;
	}

	ESQEnemyBehaviour ParseEnemyBehaviour(const FString& V)
	{
		if (V.Equals(TEXT("Wisp"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Wisp;
		if (V.Equals(TEXT("Digger"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Digger;
		if (V.Equals(TEXT("Hound"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Hound;
		if (V.Equals(TEXT("Elite"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Elite;
		if (V.Equals(TEXT("Spitter"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Spitter;
		if (V.Equals(TEXT("Bomber"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Bomber;
		if (V.Equals(TEXT("Splitter"), ESearchCase::IgnoreCase)) return ESQEnemyBehaviour::Splitter;
		return ESQEnemyBehaviour::Chase;
	}

	ESQEnemyGait ParseEnemyGait(const FString& V)
	{
		static const TCHAR* const GaitNames[] = { TEXT("Shamble"), TEXT("Lurch"), TEXT("Scuttle"), TEXT("Hop"), TEXT("ZigZag"), TEXT("Drift"),
			TEXT("Charge"), TEXT("Crawl"), TEXT("Stomp"), TEXT("Circle"), TEXT("Sprint") };
		for (int32 GaitIdx = 0; GaitIdx < static_cast<int32>(UE_ARRAY_COUNT(GaitNames)); ++GaitIdx)
		{
			if (V.Equals(GaitNames[GaitIdx], ESearchCase::IgnoreCase)) return static_cast<ESQEnemyGait>(GaitIdx);
		}
		return ESQEnemyGait::Shamble;
	}

	ESQEnemySpawn ParseEnemySpawn(const FString& V)
	{
		if (V.Equals(TEXT("Crawl"), ESearchCase::IgnoreCase)) return ESQEnemySpawn::Crawl;
		if (V.Equals(TEXT("Drop"), ESearchCase::IgnoreCase)) return ESQEnemySpawn::Drop;
		if (V.Equals(TEXT("Burst"), ESearchCase::IgnoreCase)) return ESQEnemySpawn::Burst;
		if (V.Equals(TEXT("Fade"), ESearchCase::IgnoreCase)) return ESQEnemySpawn::Fade;
		return ESQEnemySpawn::Rise;
	}

	ESQEnemyDeath ParseEnemyDeath(const FString& V)
	{
		if (V.Equals(TEXT("Bones"), ESearchCase::IgnoreCase)) return ESQEnemyDeath::Bones;
		if (V.Equals(TEXT("Launch"), ESearchCase::IgnoreCase)) return ESQEnemyDeath::Launch;
		if (V.Equals(TEXT("Pop"), ESearchCase::IgnoreCase)) return ESQEnemyDeath::Pop;
		if (V.Equals(TEXT("Fade"), ESearchCase::IgnoreCase)) return ESQEnemyDeath::Fade;
		return ESQEnemyDeath::Tumble;
	}

	/** "a|b|c" -> up to 3 floats (missing entries keep Defaults) */
	void ParseFloatTriple(const FString& V, float Out[3])
	{
		TArray<FString> TripleParts;
		V.ParseIntoArray(TripleParts, TEXT("|"), false);
		for (int32 PartIdx = 0; PartIdx < 3 && PartIdx < TripleParts.Num(); ++PartIdx)
		{
			const FString TripleCell = TripleParts[PartIdx].TrimStartAndEnd();
			if (!TripleCell.IsEmpty()) Out[PartIdx] = FCString::Atof(*TripleCell);
		}
	}

	ESQEnemyFly ParseEnemyFly(const FString& V)
	{
		if (V.Equals(TEXT("Swoop"), ESearchCase::IgnoreCase) || V.Equals(TEXT("Bat"), ESearchCase::IgnoreCase)) return ESQEnemyFly::Swoop;
		if (V.Equals(TEXT("Phase"), ESearchCase::IgnoreCase) || V.Equals(TEXT("Ghost"), ESearchCase::IgnoreCase)) return ESQEnemyFly::Phase;
		return ESQEnemyFly::None;
	}

	ESQBossAbility ParseBossAbility(const FString& V)
	{
		static const TCHAR* const AbilityNames[] = { TEXT("None"), TEXT("Slam"), TEXT("Shockwave"), TEXT("Spiral"), TEXT("Fan"), TEXT("Charge"),
			TEXT("Summon"), TEXT("Pools"), TEXT("Vortex"), TEXT("Mark"), TEXT("Pylons"), TEXT("Tether"), TEXT("Ring") };
		for (int32 AbilityIdx = 0; AbilityIdx < static_cast<int32>(UE_ARRAY_COUNT(AbilityNames)); ++AbilityIdx)
		{
			if (V.Equals(AbilityNames[AbilityIdx], ESearchCase::IgnoreCase)) return static_cast<ESQBossAbility>(AbilityIdx);
		}
		return ESQBossAbility::None;
	}

	/** "Slam:3|Spiral:2|Summon" -> picks (weight defaults to 1; unknown names skipped) */
	TArray<FSQBossAbilityPick> ParseBossAbilityList(const FString& V)
	{
		TArray<FSQBossAbilityPick> Picks;
		TArray<FString> AbilityParts;
		V.ParseIntoArray(AbilityParts, TEXT("|"), true);
		for (const FString& AbilityPart : AbilityParts)
		{
			FString AbilityName = AbilityPart.TrimStartAndEnd();
			FString WeightText;
			FString NameLeft;
			if (AbilityName.Split(TEXT(":"), &NameLeft, &WeightText)) AbilityName = NameLeft.TrimStartAndEnd();
			FSQBossAbilityPick Pick;
			Pick.Ability = ParseBossAbility(AbilityName);
			Pick.Weight = WeightText.IsEmpty() ? 1.f : FMath::Max(0.f, FCString::Atof(*WeightText));
			if (Pick.Ability != ESQBossAbility::None && Pick.Weight > 0.f) Picks.Add(Pick);
		}
		return Picks;
	}

	ESQAbilityShape ParseAbilityShape(const FString& V)
	{
		static const TCHAR* const ShapeNames[] = { TEXT("None"), TEXT("Circle"), TEXT("Nova"), TEXT("Cone"), TEXT("Line"), TEXT("Dash"), TEXT("Blink"),
			TEXT("Zone"), TEXT("Volley"), TEXT("Barrage"), TEXT("Chain"), TEXT("Self"), TEXT("Totem"), TEXT("Global") };
		for (int32 ShapeIdx = 0; ShapeIdx < static_cast<int32>(UE_ARRAY_COUNT(ShapeNames)); ++ShapeIdx)
		{
			if (V.Equals(ShapeNames[ShapeIdx], ESearchCase::IgnoreCase)) return static_cast<ESQAbilityShape>(ShapeIdx);
		}
		return ESQAbilityShape::None;
	}

	/** "Slow|Knockback" -> SQAbilityFx bits; unknown names are reported in Errors */
	uint32 ParseAbilityEffects(const FString& V, const FString& Where, FString& Errors)
	{
		struct FEffectName { const TCHAR* Name; uint32 Bit; };
		static const FEffectName EffectNames[] = {
			{ TEXT("Slow"), SQAbilityFx::Slow }, { TEXT("Freeze"), SQAbilityFx::Freeze }, { TEXT("Knockback"), SQAbilityFx::Knockback },
			{ TEXT("Pull"), SQAbilityFx::Pull }, { TEXT("Mark"), SQAbilityFx::Mark }, { TEXT("Heal"), SQAbilityFx::Heal },
			{ TEXT("Shield"), SQAbilityFx::Shield }, { TEXT("Haste"), SQAbilityFx::Haste }, { TEXT("Empower"), SQAbilityFx::Empower },
			{ TEXT("Fortify"), SQAbilityFx::Fortify }, { TEXT("Revive"), SQAbilityFx::Revive }, { TEXT("Lifesteal"), SQAbilityFx::Lifesteal } };
		uint32 Bits = 0;
		TArray<FString> EffectParts;
		V.ParseIntoArray(EffectParts, TEXT("|"), true);
		for (const FString& EffectPart : EffectParts)
		{
			const FString EffectClean = EffectPart.TrimStartAndEnd();
			bool bFound = false;
			for (const FEffectName& Known : EffectNames)
			{
				if (EffectClean.Equals(Known.Name, ESearchCase::IgnoreCase)) { Bits |= Known.Bit; bFound = true; break; }
			}
			if (!bFound && !EffectClean.IsEmpty()) Errors += FString::Printf(TEXT("Kits.csv %s: unknown effect %s. "), *Where, *EffectClean);
		}
		return Bits;
	}

	ESQGimmick ParseGimmick(const FString& V)
	{
		if (V.Equals(TEXT("Tide"), ESearchCase::IgnoreCase)) return ESQGimmick::Tide;
		if (V.Equals(TEXT("Spotlight"), ESearchCase::IgnoreCase)) return ESQGimmick::Spotlight;
		if (V.Equals(TEXT("Ice"), ESearchCase::IgnoreCase)) return ESQGimmick::Ice;
		if (V.Equals(TEXT("TimeStop"), ESearchCase::IgnoreCase)) return ESQGimmick::TimeStop;
		return ESQGimmick::None;
	}
}

void USQData::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Reload();
}

USQData* USQData::Get(const UObject* WorldContext)
{
	if (!WorldContext) return nullptr;
	const UWorld* World = WorldContext->GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	return GI ? GI->GetSubsystem<USQData>() : nullptr;
}

void USQData::Reload()
{
	LoadErrors.Reset();
	SQQuests::Reload(); // Quests.csv / Modifiers.csv / Timeline.csv (economy pass)
	Upgrades.Reset(); Reapers.Reset(); Enemies.Reset(); Bosses.Reset(); Shop.Reset(); Story.Reset(); Tuning.Reset(); Biomes.Reset(); Bonds.Reset(); Kits.Reset();

	FSQTable T;
	if (T.Load(TEXT("Tuning.csv"), LoadErrors))
	{
		for (int32 r = 0; r < T.Rows.Num(); ++r)
		{
			const FName TuneKey = T.N(r, TEXT("Key"));
			if (!TuneKey.IsNone()) Tuning.Add(TuneKey, T.F(r, TEXT("Value")));
		}
	}

	FSQTable R;
	if (R.Load(TEXT("Reapers.csv"), LoadErrors))
	{
		for (int32 r = 0; r < R.Rows.Num(); ++r)
		{
			FSQReaperDef D;
			D.Id = R.N(r, TEXT("Id"));
			D.Name = R.S(r, TEXT("Name"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Reapers.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.MaxHealth = FMath::Max(1.f, R.F(r, TEXT("MaxHealth"), 100.f));
			D.MoveSpeed = FMath::Max(0.f, R.F(r, TEXT("MoveSpeed"), 650.f));
			D.CarryCap = FMath::Max(1, R.I(r, TEXT("CarryCap"), 50));
			D.DashCooldown = FMath::Max(0.1f, R.F(r, TEXT("DashCooldown"), 2.5f));
			D.Magnet = FMath::Max(0.f, R.F(r, TEXT("Magnet"), 300.f));
			D.StartWeapon = R.N(r, TEXT("StartWeapon"));
			D.UnlockCost = FMath::Max(0, R.I(r, TEXT("UnlockCost"), 0));
			// Kits pass: Passive / PassiveText come from Kits.csv below (the Reapers.csv Passive* columns are no longer read)
			D.Role = R.N(r, TEXT("Role"));
			D.UnlockHint = R.S(r, TEXT("UnlockHint"));
			D.Color = R.C(r, TEXT("Color"));
			D.Model = R.N(r, TEXT("Model"));
			D.Weapon = R.N(r, TEXT("Weapon"));
			D.WeaponSize = FMath::Clamp(R.F(r, TEXT("WeaponSize"), 0.f), 0.f, 300.f);
			D.Accessory = R.N(r, TEXT("Accessory"));
			D.AccessorySlot = R.N(r, TEXT("AccessorySlot"));
			D.AccessorySize = FMath::Max(5.f, R.F(r, TEXT("AccessorySize"), 50.f));
			D.ArtScale = FMath::Clamp(R.F(r, TEXT("ArtScale"), 1.f), 0.5f, 2.f);
			D.IdleAnims = SplitAnimList(R.S(r, TEXT("IdleAnim")));
			D.WalkAnims = SplitAnimList(R.S(r, TEXT("WalkAnim")));
			D.RunAnims = SplitAnimList(R.S(r, TEXT("RunAnim")));
			// Gait personality (neutral defaults; clamped so a typo can't fold a reaper in half)
			D.Gait.Bounce = FMath::Clamp(R.F(r, TEXT("GaitBounce"), 0.f), 0.f, 30.f);
			D.Gait.Sway = FMath::Clamp(R.F(r, TEXT("GaitSway"), 0.f), 0.f, 25.f);
			D.Gait.Hunch = FMath::Clamp(R.F(r, TEXT("GaitHunch"), 0.f), -20.f, 45.f);
			D.Gait.Lean = FMath::Clamp(R.F(r, TEXT("GaitLean"), 0.f), -15.f, 35.f);
			D.Gait.ArmSwing = FMath::Clamp(R.F(r, TEXT("GaitArmSwing"), 1.f), 0.f, 2.5f);
			D.Gait.Stride = FMath::Clamp(R.F(r, TEXT("GaitStride"), 1.f), 0.5f, 2.f);
			D.Gait.HeadBob = FMath::Clamp(R.F(r, TEXT("GaitHeadBob"), 0.f), 0.f, 25.f);
			D.Gait.Limp = FMath::Clamp(R.F(r, TEXT("GaitLimp"), 0.f), 0.f, 1.f);
			D.Gait.Hover = FMath::Clamp(R.F(r, TEXT("GaitHover"), 0.f), 0.f, 80.f);
			Reapers.Add(D);
		}
	}

	FSQTable U;
	if (U.Load(TEXT("Upgrades.csv"), LoadErrors))
	{
		for (int32 r = 0; r < U.Rows.Num(); ++r)
		{
			FSQUpgradeDef D;
			D.Id = U.N(r, TEXT("Id"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Upgrades.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.Name = U.S(r, TEXT("Name"));
			D.Kind = ParseKind(U.S(r, TEXT("Kind")));
			D.Behaviour = ParseBehaviour(U.S(r, TEXT("Behaviour")));
			D.MaxLevel = FMath::Max(1, U.I(r, TEXT("MaxLevel"), 1));
			D.Damage = U.F(r, TEXT("Damage"));
			D.DamagePerLevel = U.F(r, TEXT("DamagePerLevel"));
			D.Cooldown = U.F(r, TEXT("Cooldown"), 1.f);
			D.CooldownPerLevel = U.F(r, TEXT("CooldownPerLevel"));
			D.Radius = U.F(r, TEXT("Radius"));
			D.RadiusPerLevel = U.F(r, TEXT("RadiusPerLevel"));
			D.Count = U.F(r, TEXT("Count"), 1.f);
			D.CountPerLevel = U.F(r, TEXT("CountPerLevel"));
			D.Speed = U.F(r, TEXT("Speed"));
			D.Slow = U.F(r, TEXT("Slow"));
			D.Stat = U.N(r, TEXT("Stat"));
			D.StatPerLevel = U.F(r, TEXT("StatPerLevel"));
			D.Stat2 = U.N(r, TEXT("Stat2"));
			D.Stat2PerLevel = U.F(r, TEXT("Stat2PerLevel"));
			D.EvolvesFrom = U.N(r, TEXT("EvolvesFrom"));
			D.RequiresRelic = U.N(r, TEXT("RequiresRelic"));
			D.Weight = FMath::Max(0.f, U.F(r, TEXT("Weight"), 10.f));
			D.Color = U.C(r, TEXT("Color"));
			D.Description = U.S(r, TEXT("Description"));
			D.IconGlyph = U.S(r, TEXT("IconGlyph")).Left(2);
			D.Tag = U.N(r, TEXT("Tag"));
			D.AttackStyle = U.N(r, TEXT("AttackStyle"));
			D.AttackAnims = SplitAnimList(U.S(r, TEXT("AttackAnim")));
			D.FxStyle = U.N(r, TEXT("FxStyle"));
			// Items/tomes pass (appended columns; older CSVs without them still load: Relic rows become tomes)
			D.Category = ParseCategory(U.S(r, TEXT("Category")), D.Kind);
			D.MaxStacks = FMath::Clamp(U.I(r, TEXT("MaxStacks"), 0), 0, 999);
			D.bSignature = U.I(r, TEXT("Signature"), 0) != 0; // kits pass (appended column)
			if (D.Category == ESQUpgradeCategory::Weapon && D.Kind != ESQUpgradeKind::Weapon)
			{
				LoadErrors += FString::Printf(TEXT("Upgrade %s: Category Weapon needs Kind Weapon. "), *D.Id.ToString());
				D.Category = D.Kind == ESQUpgradeKind::Evolution ? ESQUpgradeCategory::Evolution : ESQUpgradeCategory::Tome;
			}
			if ((D.Category == ESQUpgradeCategory::Tome || D.Category == ESQUpgradeCategory::Item || D.Category == ESQUpgradeCategory::Fallback)
				&& D.Kind != ESQUpgradeKind::Relic)
			{
				LoadErrors += FString::Printf(TEXT("Upgrade %s: Tome / Item / Fallback rows need Kind Relic. "), *D.Id.ToString());
				D.Kind = ESQUpgradeKind::Relic;
			}
			{
				// Catch typos: every stat named in the CSV must be one the game applies
				FSQStats Probe;
				if (!D.Stat.IsNone() && !SQ::ApplyStat(Probe, D.Stat, 0.f)) LoadErrors += FString::Printf(TEXT("Upgrade %s: unknown Stat %s. "), *D.Id.ToString(), *D.Stat.ToString());
				if (!D.Stat2.IsNone() && !SQ::ApplyStat(Probe, D.Stat2, 0.f)) LoadErrors += FString::Printf(TEXT("Upgrade %s: unknown Stat2 %s. "), *D.Id.ToString(), *D.Stat2.ToString());
			}
			Upgrades.Add(D);
		}
		// Evolution recipes: a weapon + a TOME (items have no slots, so they can't be a recipe ingredient)
		for (const FSQUpgradeDef& Evo : Upgrades)
		{
			if (Evo.Category != ESQUpgradeCategory::Evolution) continue;
			const FSQUpgradeDef* BaseWeapon = FindUpgrade(Evo.EvolvesFrom);
			const FSQUpgradeDef* PairedTome = FindUpgrade(Evo.RequiresRelic);
			if (!BaseWeapon || BaseWeapon->Category != ESQUpgradeCategory::Weapon) LoadErrors += FString::Printf(TEXT("Evolution %s: EvolvesFrom %s is not a weapon. "), *Evo.Id.ToString(), *Evo.EvolvesFrom.ToString());
			if (!PairedTome || !PairedTome->IsTome()) LoadErrors += FString::Printf(TEXT("Evolution %s: RequiresRelic %s is not a tome. "), *Evo.Id.ToString(), *Evo.RequiresRelic.ToString());
		}
	}

	// Reaper kits (Kits.csv, Nov 9 kits pass; Docs/Kits.md): signature weapon, passive, E and R per reaper
	FSQTable K;
	if (K.Load(TEXT("Kits.csv"), LoadErrors))
	{
		static const TCHAR* const KnownPassives[] = {
			TEXT("TeamDamage"), TEXT("HitAndRun"), TEXT("SlowSouls"), TEXT("Builder"), TEXT("Lamplight"), TEXT("DashStrike"),
			TEXT("OnTheJob"), TEXT("Fetch"), TEXT("Bulwark"), TEXT("Collections"), TEXT("Triage"), TEXT("Scorch"), TEXT("Lookout"),
			TEXT("Spite"), TEXT("Audit"), TEXT("Flock"), TEXT("LastRites"), TEXT("MobileBank"), TEXT("Seance"), TEXT("Stampede"),
			TEXT("Stopwatch") };
		for (int32 r = 0; r < K.Rows.Num(); ++r)
		{
			FSQKitDef Kit;
			Kit.ReaperId = K.N(r, TEXT("ReaperId"));
			if (Kit.ReaperId.IsNone()) continue;
			Kit.SignatureWeapon = K.N(r, TEXT("SignatureWeaponId"));
			Kit.EvoTome = K.N(r, TEXT("EvoTome"));
			Kit.PassiveId = K.N(r, TEXT("PassiveId"));
			Kit.PassiveName = K.S(r, TEXT("PassiveName"));
			Kit.PassiveGlyph = K.S(r, TEXT("PassiveGlyph")).Left(2);
			Kit.PassiveDesc = K.S(r, TEXT("PassiveDesc"));
			Kit.P[0] = K.F(r, TEXT("PassiveP1"));
			Kit.P[1] = K.F(r, TEXT("PassiveP2"));
			Kit.P[2] = K.F(r, TEXT("PassiveP3"));
			Kit.P[3] = K.F(r, TEXT("PassiveP4"));
			Kit.PassiveAnims = SplitAnimList(K.S(r, TEXT("PassiveAnim")));
			const FString KitName = Kit.ReaperId.ToString();
			{
				bool bKnownPassive = false;
				for (const TCHAR* KnownName : KnownPassives)
				{
					if (Kit.PassiveId == KnownName) { bKnownPassive = true; break; }
				}
				if (!bKnownPassive) LoadErrors += FString::Printf(TEXT("Kits.csv %s: unknown PassiveId %s. "), *KitName, *Kit.PassiveId.ToString());
			}
			for (int32 SlotIdx = 0; SlotIdx < 2; ++SlotIdx)
			{
				const TCHAR* Prefix = SlotIdx == 0 ? TEXT("E_") : TEXT("R_");
				auto Col = [Prefix](const TCHAR* Name) { return FString(Prefix) + Name; };
				FSQAbilityDef& A = Kit.Abilities[SlotIdx];
				const FString Where = KitName + TEXT(" ") + Prefix;
				A.Name = K.S(r, *Col(TEXT("Name")));
				A.Glyph = K.S(r, *Col(TEXT("Glyph"))).Left(2);
				A.Shape = ParseAbilityShape(K.S(r, *Col(TEXT("Shape"))));
				if (A.Shape == ESQAbilityShape::None) LoadErrors += FString::Printf(TEXT("Kits.csv %s: unknown Shape. "), *Where);
				A.Effects = ParseAbilityEffects(K.S(r, *Col(TEXT("Effect"))), Where, LoadErrors);
				A.Damage = FMath::Max(0.f, K.F(r, *Col(TEXT("Damage"))));
				A.Radius = FMath::Max(0.f, K.F(r, *Col(TEXT("Radius"))));
				A.Range = FMath::Max(0.f, K.F(r, *Col(TEXT("Range"))));
				A.Width = FMath::Max(0.f, K.F(r, *Col(TEXT("Width"))));
				A.Count = FMath::Clamp(K.I(r, *Col(TEXT("Count"))), 0, 64);
				A.Pierce = FMath::Clamp(K.I(r, *Col(TEXT("Pierce"))), 0, 20);
				A.Cooldown = FMath::Max(1.f, K.F(r, *Col(TEXT("Cooldown")), 10.f));
				A.Duration = FMath::Clamp(K.F(r, *Col(TEXT("Duration"))), 0.f, 30.f);
				A.Tick = FMath::Clamp(K.F(r, *Col(TEXT("Tick")), 0.5f), 0.1f, 5.f);
				A.EffectValue = K.F(r, *Col(TEXT("EffectValue")));
				A.EffectTime = FMath::Clamp(K.F(r, *Col(TEXT("EffectTime"))), 0.f, 15.f);
				A.FinalDamage = FMath::Max(0.f, K.F(r, *Col(TEXT("FinalDamage"))));
				A.Delay = FMath::Clamp(K.F(r, *Col(TEXT("Delay"))), 0.f, 3.f);
				A.EvoDamage = FMath::Max(0.f, K.F(r, *Col(TEXT("EvoDamage")), 1.f));
				A.EvoRadius = FMath::Max(0.1f, K.F(r, *Col(TEXT("EvoRadius")), 1.f));
				A.EvoCount = FMath::Clamp(K.I(r, *Col(TEXT("EvoCount"))), 0, 32);
				A.EvoCooldown = FMath::Max(0.1f, K.F(r, *Col(TEXT("EvoCooldown")), 1.f));
				A.EvoDuration = FMath::Max(0.1f, K.F(r, *Col(TEXT("EvoDuration")), 1.f));
				A.EvoEffects = ParseAbilityEffects(K.S(r, *Col(TEXT("EvoEffect"))), Where, LoadErrors);
				A.EvoValue = K.F(r, *Col(TEXT("EvoValue")));
				A.EvoTime = FMath::Clamp(K.F(r, *Col(TEXT("EvoTime"))), 0.f, 15.f);
				A.EvoText = K.S(r, *Col(TEXT("EvoText")));
				A.Anims = SplitAnimList(K.S(r, *Col(TEXT("Anim"))));
				A.Fx = K.N(r, *Col(TEXT("Fx")));
				A.Color = K.C(r, *Col(TEXT("Color")));
				A.Description = K.S(r, *Col(TEXT("Desc")));
			}
			// The signature weapon (and every evolution of it) belongs to this reaper only
			bool bSignatureOk = false;
			for (FSQUpgradeDef& Sig : Upgrades)
			{
				if (Sig.Id == Kit.SignatureWeapon)
				{
					bSignatureOk = Sig.IsSignature() && Sig.Category == ESQUpgradeCategory::Weapon;
					Sig.SignatureOwner = Kit.ReaperId;
				}
				else if (Sig.Category == ESQUpgradeCategory::Evolution && Sig.EvolvesFrom == Kit.SignatureWeapon)
				{
					Sig.SignatureOwner = Kit.ReaperId;
					if (!Sig.IsSignature()) LoadErrors += FString::Printf(TEXT("Upgrade %s: evolves a signature weapon but has no Signature 1. "), *Sig.Id.ToString());
					if (Sig.RequiresRelic != Kit.EvoTome) LoadErrors += FString::Printf(TEXT("Kits.csv %s: EvoTome %s differs from %s's RequiresRelic. "), *KitName, *Kit.EvoTome.ToString(), *Sig.Id.ToString());
				}
			}
			if (!bSignatureOk) LoadErrors += FString::Printf(TEXT("Kits.csv %s: SignatureWeaponId %s is not a Signature weapon in Upgrades.csv. "), *KitName, *Kit.SignatureWeapon.ToString());
			const FSQUpgradeDef* KitTome = FindUpgrade(Kit.EvoTome);
			if (!KitTome || !KitTome->IsTome()) LoadErrors += FString::Printf(TEXT("Kits.csv %s: EvoTome %s is not a tome. "), *KitName, *Kit.EvoTome.ToString());
			// The reaper starts with the signature and shows the kit's passive
			for (FSQReaperDef& KitReaper : Reapers)
			{
				if (KitReaper.Id != Kit.ReaperId) continue;
				if (!Kit.SignatureWeapon.IsNone()) KitReaper.StartWeapon = Kit.SignatureWeapon;
				KitReaper.Passive = Kit.PassiveId;
				KitReaper.PassiveText = Kit.PassiveDesc;
			}
			Kits.Add(Kit);
		}
		for (const FSQUpgradeDef& Sig : Upgrades)
		{
			if (Sig.IsSignature() && Sig.SignatureOwner.IsNone()) LoadErrors += FString::Printf(TEXT("Upgrade %s: Signature 1 but no kit uses it. "), *Sig.Id.ToString());
		}
	}

	FSQTable E;
	if (E.Load(TEXT("Enemies.csv"), LoadErrors))
	{
		for (int32 r = 0; r < E.Rows.Num() && Enemies.Num() < 255; ++r)
		{
			FSQEnemyDef D;
			D.Id = E.N(r, TEXT("Id"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Enemies.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.Name = E.S(r, TEXT("Name"));
			D.Behaviour = ParseEnemyBehaviour(E.S(r, TEXT("Behaviour")));
			D.Health = FMath::Max(1.f, E.F(r, TEXT("Health"), 10.f));
			D.HealthPerNight = E.F(r, TEXT("HealthPerNight"));
			D.Speed = FMath::Max(0.f, E.F(r, TEXT("Speed"), 200.f));
			D.Damage = FMath::Max(0.f, E.F(r, TEXT("Damage"), 5.f));
			D.Radius = FMath::Max(1.f, E.F(r, TEXT("Radius"), 40.f));
			D.Souls = FMath::Max(0, E.I(r, TEXT("Souls"), 1));
			D.XP = FMath::Max(0, E.I(r, TEXT("XP"), 1));
			D.Weight[0] = FMath::Max(0.f, E.F(r, TEXT("Weight1")));
			D.Weight[1] = FMath::Max(0.f, E.F(r, TEXT("Weight2")));
			D.Weight[2] = FMath::Max(0.f, E.F(r, TEXT("Weight3")));
			D.MinProgress = E.F(r, TEXT("MinProgress"));
			D.Shape = E.N(r, TEXT("Shape"));
			D.Scale = FMath::Max(0.05f, E.F(r, TEXT("Scale"), 1.f));
			D.Color = E.C(r, TEXT("Color"));
			D.SplitInto = E.N(r, TEXT("SplitInto"));
			D.SplitCount = FMath::Clamp(E.I(r, TEXT("SplitCount"), 0), 0, 16); // a typo here would flood the horde
			D.Range = FMath::Max(0.f, E.F(r, TEXT("Range"), 0.f));
			D.Model = E.N(r, TEXT("Model"));
			D.MoveAnims = SplitAnimList(E.S(r, TEXT("Anim")));
			D.Anim = D.MoveAnims.Num() > 0 ? D.MoveAnims[0] : NAME_None;
			D.Height = FMath::Max(1.f, E.F(r, TEXT("Height"), 150.f));
			// Look + animation personality (missing columns keep the defaults)
			D.AnimRate = FMath::Clamp(E.F(r, TEXT("AnimRate"), 1.f), 0.1f, 4.f);
			D.Gait = ParseEnemyGait(E.S(r, TEXT("Gait")));
			D.Spawn = ParseEnemySpawn(E.S(r, TEXT("Spawn")));
			D.Death = ParseEnemyDeath(E.S(r, TEXT("Death")));
			D.Windup = FMath::Clamp(E.F(r, TEXT("Windup"), 0.f), 0.f, 3.f);
			D.AttackAnims = SplitAnimList(E.S(r, TEXT("AttackAnim")));
			D.HitAnims = SplitAnimList(E.S(r, TEXT("HitAnim")));
			D.Prop = E.N(r, TEXT("Prop"));
			D.PropBone = E.N(r, TEXT("PropBone"));
			D.PropSize = FMath::Clamp(E.F(r, TEXT("PropSize"), 40.f), 1.f, 1000.f);
			{
				float PropOff[3] = { 0.f, 0.f, 0.f };
				ParseFloatTriple(E.S(r, TEXT("PropOffset")), PropOff);
				D.PropOffset = FVector(PropOff[0], PropOff[1], PropOff[2]);
				float PropAngles[3] = { 0.f, 0.f, 0.f };
				ParseFloatTriple(E.S(r, TEXT("PropRot")), PropAngles);
				D.PropRot = FRotator(PropAngles[0], PropAngles[1], PropAngles[2]);
				float StretchWH[3] = { 1.f, 1.f, 0.f };
				ParseFloatTriple(E.S(r, TEXT("Stretch")), StretchWH);
				D.Stretch = FVector2D(FMath::Clamp(StretchWH[0], 0.3f, 3.f), FMath::Clamp(StretchWH[1], 0.3f, 3.f));
			}
			D.Accent = E.S(r, TEXT("Accent")).IsEmpty() ? D.Color : E.C(r, TEXT("Accent"));
			D.Glow = FMath::Clamp(E.F(r, TEXT("Glow"), 0.f), 0.f, 20.f);
			D.TintStrength = FMath::Clamp(E.F(r, TEXT("TintStrength"), 0.55f), 0.f, 1.f);
			D.Stack = FMath::Clamp(E.I(r, TEXT("Stack"), 0), 0, 4);
			D.StackAnims = SplitAnimList(E.S(r, TEXT("StackAnim")));
			// Flying (appended columns; a missing or empty cell = a ground enemy)
			D.Fly = ParseEnemyFly(E.S(r, TEXT("Fly")));
			D.Altitude = FMath::Clamp(E.F(r, TEXT("Altitude"), D.IsFlyer() ? 260.f : 0.f), 0.f, 1500.f);
			D.FlyBob = FMath::Clamp(E.F(r, TEXT("FlyBob"), D.IsFlyer() ? 25.f : 0.f), 0.f, 300.f);
			// Collision pass (appended column): reach past touching, cm
			D.Reach = FMath::Clamp(E.F(r, TEXT("Reach"), 12.f), 0.f, 300.f);
			Enemies.Add(D);
		}
	}

	FSQTable B;
	if (B.Load(TEXT("Bosses.csv"), LoadErrors))
	{
		for (int32 r = 0; r < B.Rows.Num(); ++r)
		{
			FSQBossDef D;
			D.Id = B.N(r, TEXT("Id"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Bosses.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.Name = B.S(r, TEXT("Name"));
			D.Title = B.S(r, TEXT("Title"));
			D.Night = B.I(r, TEXT("Night"), 1);
			// Cooldowns floored so a 0 / blank cell can't fire an attack every frame
			D.Health = FMath::Max(1.f, B.F(r, TEXT("Health"), 1000.f));
			D.Speed = FMath::Max(0.f, B.F(r, TEXT("Speed"), 250.f));
			D.SlamDamage = FMath::Max(0.f, B.F(r, TEXT("SlamDamage"), 20.f));
			D.SlamRadius = FMath::Max(0.f, B.F(r, TEXT("SlamRadius"), 350.f));
			D.SlamCooldown = FMath::Max(0.5f, B.F(r, TEXT("SlamCooldown"), 6.f));
			D.RingCount = FMath::Clamp(B.I(r, TEXT("RingCount"), 8), 0, 64);
			D.RingDamage = FMath::Max(0.f, B.F(r, TEXT("RingDamage"), 10.f));
			D.RingCooldown = FMath::Max(0.5f, B.F(r, TEXT("RingCooldown"), 5.f));
			D.SummonId = B.N(r, TEXT("SummonId"));
			D.SummonCount = FMath::Clamp(B.I(r, TEXT("SummonCount"), 8), 0, 200);
			D.SummonCooldown = FMath::Max(0.5f, B.F(r, TEXT("SummonCooldown"), 9.f));
			D.Scale = FMath::Max(0.1f, B.F(r, TEXT("Scale"), 3.f));
			D.Color = B.C(r, TEXT("Color"));
			D.IntroLine = B.S(r, TEXT("IntroLine"));
			D.ContractText = B.S(r, TEXT("ContractText"));
			D.Model = B.N(r, TEXT("Model"));
			D.Weapon = B.N(r, TEXT("Weapon"));
			// Ability rotation + style (missing columns keep the defaults)
			D.Abilities = ParseBossAbilityList(B.S(r, TEXT("Abilities")));
			D.Phase2 = ParseBossAbilityList(B.S(r, TEXT("Phase2")));
			D.Phase3 = ParseBossAbilityList(B.S(r, TEXT("Phase3")));
			D.Elements = SplitAnimList(B.S(r, TEXT("Elements")));
			D.TargetSeconds = FMath::Clamp(B.F(r, TEXT("TargetSeconds"), 180.f), 20.f, 1200.f);
			D.Prop = B.N(r, TEXT("Prop"));
			D.PropBone = B.N(r, TEXT("PropBone"));
			D.PropSize = FMath::Clamp(B.F(r, TEXT("PropSize"), 80.f), 1.f, 2000.f);
			D.Accent = B.S(r, TEXT("Accent")).IsEmpty() ? D.Color : B.C(r, TEXT("Accent"));
			D.Glow = FMath::Clamp(B.F(r, TEXT("Glow"), 0.4f), 0.f, 20.f);
			D.MoveAnims = SplitAnimList(B.S(r, TEXT("MoveAnim")));
			D.RoarAnims = SplitAnimList(B.S(r, TEXT("RoarAnim")));
			D.SummonFlyer = B.N(r, TEXT("SummonFlyer"));
			D.SummonFlyerCount = FMath::Clamp(B.I(r, TEXT("SummonFlyerCount"), 0), 0, 40);
			Bosses.Add(D);
		}
	}

	FSQTable Sh;
	if (Sh.Load(TEXT("Shop.csv"), LoadErrors))
	{
		for (int32 r = 0; r < Sh.Rows.Num(); ++r)
		{
			FSQShopDef D;
			D.Id = Sh.N(r, TEXT("Id"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Shop.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.Name = Sh.S(r, TEXT("Name"));
			D.Cost = FMath::Max(0, Sh.I(r, TEXT("Cost"), 50)); // a negative cost would mint souls
			D.Effect = Sh.N(r, TEXT("Effect"));
			D.Value = Sh.F(r, TEXT("Value"), 1.f);
			D.Description = Sh.S(r, TEXT("Description"));
			Shop.Add(D);
		}
	}

	FSQTable St;
	if (St.Load(TEXT("Story.csv"), LoadErrors))
	{
		for (int32 r = 0; r < St.Rows.Num(); ++r)
		{
			FSQStoryDef D;
			D.Id = St.N(r, TEXT("Id"));
			D.Night = St.I(r, TEXT("Night"));
			D.Speaker = St.S(r, TEXT("Speaker"));
			D.Text = St.S(r, TEXT("Text"));
			Story.Add(D);
		}
	}

	FSQTable Bi;
	if (Bi.Load(TEXT("Biomes.csv"), LoadErrors))
	{
		for (int32 r = 0; r < Bi.Rows.Num(); ++r)
		{
			FSQBiomeDef D;
			D.Id = Bi.N(r, TEXT("Id"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Biomes.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.Name = Bi.S(r, TEXT("Name"));
			D.Description = Bi.S(r, TEXT("Description"));
			D.Gimmick = ParseGimmick(Bi.S(r, TEXT("Gimmick")));
			D.GimmickText = Bi.S(r, TEXT("GimmickText"));
			D.GroundColor = Bi.C(r, TEXT("GroundColor"));
			D.PropColor = Bi.C(r, TEXT("PropColor"));
			D.FogColor = Bi.C(r, TEXT("FogColor"));
			D.PropShape = Bi.N(r, TEXT("PropShape"));
			D.PropCount = FMath::Clamp(Bi.I(r, TEXT("PropCount"), 70), 0, 2000);
			D.DecorCount = FMath::Clamp(Bi.I(r, TEXT("DecorCount"), 120), 0, 5000);
			{
				auto SplitNames = [](const FString& In, TArray<FName>& Out)
				{
					TArray<FString> Parts;
					In.ParseIntoArray(Parts, TEXT("|"), true);
					for (const FString& P : Parts) { const FString Trimmed = P.TrimStartAndEnd(); if (!Trimmed.IsEmpty()) Out.Add(FName(*Trimmed)); }
				};
				SplitNames(Bi.S(r, TEXT("Props")), D.Props);
				SplitNames(Bi.S(r, TEXT("Decor")), D.Decor);
			}
			D.BossId = Bi.N(r, TEXT("BossId"));
			D.RequiresContract = Bi.N(r, TEXT("RequiresContract"));
			D.bRequiresAllContracts = Bi.I(r, TEXT("RequiresAllContracts"), 0) != 0;
			Biomes.Add(D);
		}
	}

	FSQTable Bo;
	if (Bo.Load(TEXT("Bonds.csv"), LoadErrors))
	{
		for (int32 r = 0; r < Bo.Rows.Num(); ++r)
		{
			FSQBondDef D;
			D.Id = Bo.N(r, TEXT("Id"));
			D.Name = Bo.S(r, TEXT("Name"));
			D.RelicA = Bo.N(r, TEXT("RelicA"));
			D.RelicB = Bo.N(r, TEXT("RelicB"));
			D.Stat = Bo.N(r, TEXT("Stat"));
			D.Value = Bo.F(r, TEXT("Value"));
			D.Description = Bo.S(r, TEXT("Description"));
			{
				FSQStats Probe;
				if (!D.Stat.IsNone() && !SQ::ApplyStat(Probe, D.Stat, 0.f)) LoadErrors += FString::Printf(TEXT("Bond %s: unknown Stat %s. "), *D.Id.ToString(), *D.Stat.ToString());
			}
			Bonds.Add(D);
		}
	}

	// Map interactables (SQShrine / SQPot). Reset here so this block stays self-contained.
	Shrines.Reset();
	FSQTable Sr;
	if (Sr.Load(TEXT("Shrines.csv"), LoadErrors))
	{
		for (int32 r = 0; r < Sr.Rows.Num(); ++r)
		{
			FSQShrineDef D;
			D.Id = Sr.N(r, TEXT("Id"));
			if (D.Id.IsNone()) { LoadErrors += FString::Printf(TEXT("Shrines.csv data row %d: no Id (skipped). "), r + 1); continue; }
			D.Name = Sr.S(r, TEXT("Name"));
			D.Type = Sr.N(r, TEXT("Type"));
			D.Biomes = SplitAnimList(Sr.S(r, TEXT("Biomes")));
			D.PerBiome = FMath::Clamp(Sr.I(r, TEXT("PerBiome"), 1), 0, 64);
			D.Chance = FMath::Clamp(Sr.F(r, TEXT("Chance"), 1.f), 0.f, 1.f);
			D.HighGround = FMath::Clamp(Sr.F(r, TEXT("HighGround"), 0.f), 0.f, 1.f);
			D.bSecret = Sr.I(r, TEXT("Secret"), 0) != 0;
			D.bHidden = Sr.I(r, TEXT("Hidden"), 0) != 0;
			D.Radius = FMath::Clamp(Sr.F(r, TEXT("Radius"), 300.f), 50.f, 1500.f);
			D.ChargeSeconds = FMath::Max(0.5f, Sr.F(r, TEXT("ChargeSeconds"), 6.f));
			D.CoopBonus = FMath::Max(0.f, Sr.F(r, TEXT("CoopBonus"), 0.5f));
			D.Choices = FMath::Clamp(Sr.I(r, TEXT("Choices"), 3), 1, 3);
			D.Rarity = FMath::Clamp(Sr.I(r, TEXT("Rarity"), 0), 0, 3);
			D.Value = Sr.F(r, TEXT("Value"));
			D.Value2 = Sr.F(r, TEXT("Value2"));
			D.Enemy = Sr.N(r, TEXT("Enemy"));
			D.Count = FMath::Clamp(Sr.I(r, TEXT("Count"), 0), 0, 200);
			D.Duration = FMath::Max(0.f, Sr.F(r, TEXT("Duration")));
			D.Model = Sr.N(r, TEXT("Model"));
			D.Height = FMath::Clamp(Sr.F(r, TEXT("Height"), 150.f), 10.f, 1000.f);
			D.Color = Sr.C(r, TEXT("Color"));
			D.Glow = Sr.F(r, TEXT("Glow"), 1.f);
			D.Prompt = Sr.S(r, TEXT("Prompt"));
			D.Boosts = Sr.S(r, TEXT("Boosts"));
			D.Description = Sr.S(r, TEXT("Description"));
			Shrines.Add(D);
		}
	}

	if (!LoadErrors.IsEmpty())
	{
		UE_LOG(LogSQ, Error, TEXT("SQData: %s"), *LoadErrors);
	}
	UE_LOG(LogSQ, Log, TEXT("SQData loaded: %d upgrades, %d reapers, %d enemies, %d bosses, %d tuning keys"),
		Upgrades.Num(), Reapers.Num(), Enemies.Num(), Bosses.Num(), Tuning.Num());
}

float USQData::Tune(FName Key, float Default) const
{
	const float* V = Tuning.Find(Key);
	return V ? *V : Default;
}

const FSQUpgradeDef* USQData::FindUpgrade(FName Id) const
{
	return Upgrades.FindByPredicate([Id](const FSQUpgradeDef& D) { return D.Id == Id; });
}

const FSQReaperDef* USQData::FindReaper(FName Id) const
{
	const FSQReaperDef* Found = Reapers.FindByPredicate([Id](const FSQReaperDef& D) { return D.Id == Id; });
	return Found ? Found : (Reapers.Num() > 0 ? &Reapers[0] : nullptr);
}

const FSQKitDef* USQData::FindKit(FName ReaperId) const
{
	return Kits.FindByPredicate([ReaperId](const FSQKitDef& K) { return K.ReaperId == ReaperId; });
}

const FSQBossDef* USQData::FindBoss(FName Id) const
{
	return Bosses.FindByPredicate([Id](const FSQBossDef& D) { return D.Id == Id; });
}

const FSQBossDef* USQData::FindBossForNight(int32 Night) const
{
	const FSQBossDef* Found = Bosses.FindByPredicate([Night](const FSQBossDef& D) { return D.Night == Night; });
	return Found ? Found : (Bosses.Num() > 0 ? &Bosses.Last() : nullptr);
}

const FSQBiomeDef* USQData::FindBiome(FName Id) const
{
	const FSQBiomeDef* Found = Biomes.FindByPredicate([Id](const FSQBiomeDef& D) { return D.Id == Id; });
	return Found ? Found : (Biomes.Num() > 0 ? &Biomes[0] : nullptr);
}

int32 USQData::FindEnemyIndex(FName Id) const
{
	return Enemies.IndexOfByPredicate([Id](const FSQEnemyDef& D) { return D.Id == Id; });
}

const FSQShrineDef* USQData::FindShrine(FName Id) const
{
	return Shrines.FindByPredicate([Id](const FSQShrineDef& D) { return D.Id == Id; });
}

namespace SQ
{
	UStaticMesh* Mesh(FName Shape)
	{
		FString S = Shape.IsNone() ? TEXT("Sphere") : Shape.ToString();
		if (S != TEXT("Sphere") && S != TEXT("Cube") && S != TEXT("Cylinder") && S != TEXT("Cone") && S != TEXT("Plane"))
		{
			S = TEXT("Sphere");
		}
		const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), *S, *S);
		return LoadObject<UStaticMesh>(nullptr, *Path);
	}

	UMaterialInstanceDynamic* ColorMID(UObject* Outer, const FLinearColor& Color)
	{
		// Clay material made by Scripts/create_clay_material.py; falls back to the engine's basic material
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Clay.M_Clay"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Base) return nullptr;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		if (MID) MID->SetVectorParameterValue(TEXT("Color"), Color);
		return MID;
	}

	float UpgradeDamage(const FSQUpgradeDef& Def, int32 Level, float Power)
	{
		return (Def.Damage + Def.DamagePerLevel * (Level - 1)) * FMath::Max(0.1f, Power);
	}

	float UpgradeCooldown(const FSQUpgradeDef& Def, int32 Level)
	{
		return FMath::Max(0.08f, Def.Cooldown + Def.CooldownPerLevel * (Level - 1));
	}

	float UpgradeRadius(const FSQUpgradeDef& Def, int32 Level, float Power)
	{
		return (Def.Radius + Def.RadiusPerLevel * (Level - 1)) * (1.f + 0.25f * FMath::Max(0.f, Power - 1.f));
	}

	int32 UpgradeCount(const FSQUpgradeDef& Def, int32 Level, float Power)
	{
		const int32 Base = FMath::Max(1, FMath::FloorToInt(Def.Count + Def.CountPerLevel * (Level - 1)));
		// Only multi-shot weapons (more than one of something, or count grows with level) get extra count from power
		const bool bMulti = Def.Count >= 2.f || Def.CountPerLevel > 0.f;
		const int32 Extra = bMulti ? FMath::FloorToInt(FMath::Max(0.f, Power - 1.f) * 2.5f + 0.001f) : 0;
		return Base + Extra;
	}

	FString KindName(ESQUpgradeKind Kind)
	{
		switch (Kind)
		{
		case ESQUpgradeKind::Relic: return TEXT("Relic");
		case ESQUpgradeKind::Evolution: return TEXT("EVOLUTION");
		default: return TEXT("Weapon");
		}
	}

	FString CategoryName(const FSQUpgradeDef& Def)
	{
		switch (Def.Category)
		{
		case ESQUpgradeCategory::Tome: return TEXT("TOME");
		case ESQUpgradeCategory::Item: return TEXT("ITEM");
		case ESQUpgradeCategory::Evolution: return TEXT("EVOLUTION");
		case ESQUpgradeCategory::Fallback: return TEXT("BONUS");
		default: return TEXT("WEAPON");
		}
	}

	FString RarityName(uint8 R)
	{
		switch (R)
		{
		case 1: return TEXT("Rare");
		case 2: return TEXT("Epic");
		case 3: return TEXT("Legendary");
		default: return TEXT("Common");
		}
	}

	FLinearColor RarityColor(uint8 R)
	{
		switch (R)
		{
		case 1: return FLinearColor::FromSRGBColor(FColor(0x5C, 0xF2, 0xD6));
		case 2: return FLinearColor::FromSRGBColor(FColor(0xB4, 0x7C, 0xFF));
		case 3: return FLinearColor::FromSRGBColor(FColor(0xFF, 0xB5, 0x47));
		default: return FLinearColor::FromSRGBColor(FColor(0xE8, 0xE2, 0xD0));
		}
	}

	float RarityWeaponPower(uint8 R)
	{
		switch (R)
		{
		case 1: return 0.10f;
		case 2: return 0.22f;
		case 3: return 0.40f;
		default: return 0.f;
		}
	}

	float RarityStatMult(uint8 R)
	{
		switch (R)
		{
		case 1: return 1.4f;
		case 2: return 1.9f;
		case 3: return 2.6f;
		default: return 1.f;
		}
	}

	bool ApplyStat(FSQStats& S, FName Stat, float Value)
	{
		// Whole-number stats round down per source (so a 1.4x Rare pick of "+1 projectile" is still +1)
		const int32 Whole = FMath::FloorToInt(Value + 0.001f);
		if (Stat == TEXT("MaxHealth")) S.MaxHealth += Value;
		else if (Stat == TEXT("MoveSpeed")) S.MoveSpeed += Value;
		else if (Stat == TEXT("Damage") || Stat == TEXT("DamageMult")) S.DamageMult += Value;
		else if (Stat == TEXT("Cooldown") || Stat == TEXT("CooldownMult")) S.CooldownMult -= Value;
		else if (Stat == TEXT("Area") || Stat == TEXT("AreaMult")) S.AreaMult += Value;
		else if (Stat == TEXT("CarryCap")) S.CarryCap += FMath::RoundToInt(Value);
		else if (Stat == TEXT("Magnet")) S.Magnet += Value;
		else if (Stat == TEXT("Armor")) S.Armor += Value;
		else if (Stat == TEXT("Projectiles") || Stat == TEXT("ProjectileBonus")) S.ProjectileBonus += Whole;
		else if (Stat == TEXT("Regen")) S.Regen += Value;
		else if (Stat == TEXT("DashCooldown")) S.DashCooldown -= Value;
		else if (Stat == TEXT("TurretBonus")) S.TurretBonus += Whole;
		else if (Stat == TEXT("SoulBonus")) S.SoulBonus += Value;
		else if (Stat == TEXT("BankSpeed")) S.BankSpeed += Value;
		else if (Stat == TEXT("Thorns")) S.Thorns += Value;
		else if (Stat == TEXT("PickupHeal")) S.PickupHeal += Value;
		else if (Stat == TEXT("Luck")) S.Luck += Value;
		else if (Stat == TEXT("CritChance")) S.CritChance += Value;
		else if (Stat == TEXT("CritDamage")) S.CritDamage += Value;
		else if (Stat == TEXT("Duration")) S.Duration += Value;
		else if (Stat == TEXT("ExtraJumps")) S.ExtraJumps += Whole;
		else if (Stat == TEXT("Evasion")) S.Evasion += Value;
		else if (Stat == TEXT("AllyRegen")) S.AllyRegen += Value;
		else if (Stat == TEXT("ChestKey")) { /* Skeleton Key: read from the owned relics by ASQChest::KeyChance (x / (x + 1)) */ }
		else return false;
		return true;
	}

	void FinalizeStats(FSQStats& S, const USQData* D)
	{
		auto TuneOr = [D](const TCHAR* Key, float Default) { return D ? D->Tune(FName(Key), Default) : Default; };

		// Attack speed: ApplyStat subtracts each "Cooldown" value from CooldownMult, so 1 - CooldownMult = summed attack speed
		const float AttackSpeed = 1.f - S.CooldownMult;
		const float CooldownFloor = FMath::Clamp(TuneOr(TEXT("CooldownFloor"), 0.4f), 0.05f, 1.f);
		S.CooldownMult = AttackSpeed >= 0.f ? 1.f / (1.f + AttackSpeed) : 1.f - AttackSpeed; // negative = slower weapons
		S.CooldownMult = FMath::Clamp(S.CooldownMult, CooldownFloor, 4.f);

		// Crit: one tier, capped (readable numbers); chance past the cap is not wasted, it becomes crit damage
		const float CritCap = FMath::Clamp(TuneOr(TEXT("CritChanceMax"), 1.f), 0.f, 1.f);
		S.CritDamage = FMath::Max(0.f, S.CritDamage);
		if (S.CritChance > CritCap)
		{
			S.CritDamage += (S.CritChance - CritCap) * FMath::Max(0.f, TuneOr(TEXT("CritOverflowToDamage"), 1.f));
			S.CritChance = CritCap;
		}
		S.CritChance = FMath::Max(0.f, S.CritChance);

		S.AreaMult = FMath::Clamp(S.AreaMult, 0.25f, FMath::Max(1.f, TuneOr(TEXT("AreaMax"), 3.f)));
		S.Duration = FMath::Clamp(S.Duration, 0.25f, FMath::Max(1.f, TuneOr(TEXT("DurationMax"), 3.f)));

		// Armor: damage taken x K / (K + armor) - every 0.01 armor = +1% effective health, so it never reaches 100%
		const float ArmorK = FMath::Max(0.05f, TuneOr(TEXT("ArmorK"), 1.f));
		const float ArmorRaw = FMath::Max(0.f, S.Armor);
		S.Armor = FMath::Min(ArmorRaw / (ArmorRaw + ArmorK), FMath::Clamp(TuneOr(TEXT("ArmorMax"), 0.75f), 0.f, 0.95f));

		// Evasion: chance stat, x / (x + K), hard cap
		const float EvasionK = FMath::Max(0.05f, TuneOr(TEXT("EvasionK"), 1.f));
		const float EvasionRaw = FMath::Max(0.f, S.Evasion);
		S.Evasion = FMath::Min(EvasionRaw / (EvasionRaw + EvasionK), FMath::Clamp(TuneOr(TEXT("EvasionMax"), 0.4f), 0.f, 0.9f));

		// Luck: about linear while small, never above LuckSoftCap (rarer cards can't snowball into ever rarer cards)
		const float LuckCap = FMath::Max(0.1f, TuneOr(TEXT("LuckSoftCap"), 2.f));
		const float LuckRaw = FMath::Max(0.f, S.Luck);
		S.Luck = LuckCap * LuckRaw / (LuckRaw + LuckCap);

		S.ProjectileBonus = FMath::Clamp(S.ProjectileBonus, 0, FMath::Max(0, FMath::RoundToInt(TuneOr(TEXT("ProjectileBonusMax"), 4.f))));
		S.TurretBonus = FMath::Clamp(S.TurretBonus, 0, FMath::Max(0, FMath::RoundToInt(TuneOr(TEXT("TurretBonusMax"), 6.f))));
		S.MoveSpeed = FMath::Clamp(S.MoveSpeed, 200.f, FMath::Max(200.f, TuneOr(TEXT("MoveSpeedMax"), 1100.f)));
		S.SoulBonus = FMath::Clamp(S.SoulBonus, 0.f, 1.f);
		S.DashCooldown = FMath::Max(0.4f, S.DashCooldown);
		S.ExtraJumps = FMath::Clamp(S.ExtraJumps, 0, 4);
		S.MaxHealth = FMath::Max(1.f, S.MaxHealth);
		S.CarryCap = FMath::Max(0, S.CarryCap);
		S.Regen = FMath::Max(0.f, S.Regen);
		S.Thorns = FMath::Max(0.f, S.Thorns);
		S.PickupHeal = FMath::Max(0.f, S.PickupHeal);
		S.AllyRegen = FMath::Max(0.f, S.AllyRegen);
		S.BankSpeed = FMath::Max(0.f, S.BankSpeed);
		S.Magnet = FMath::Max(0.f, S.Magnet);
		S.DamageMult = FMath::Max(0.1f, S.DamageMult);
	}

	/** 3 -> "3", 0.25 -> "0.25", 12.5 -> "12.5" (absolute value; callers add the sign) */
	static FString UpgFmt(float Value)
	{
		const float Abs = FMath::Abs(Value);
		if (FMath::IsNearlyEqual(Abs, FMath::RoundToFloat(Abs), 0.01f)) return FString::FromInt(FMath::RoundToInt(Abs));
		FString Out = Abs < 1.f ? FString::Printf(TEXT("%.2f"), Abs) : FString::Printf(TEXT("%.1f"), Abs);
		while (Out.EndsWith(TEXT("0"))) Out.LeftChopInline(1);
		if (Out.EndsWith(TEXT("."))) Out.LeftChopInline(1);
		return Out;
	}

	static FString UpgPct(float Value)
	{
		return UpgFmt(Value * 100.f) + TEXT("%");
	}

	FString StatText(FName Stat, float Value)
	{
		const TCHAR* Plus = Value >= 0.f ? TEXT("+") : TEXT("-");
		const TCHAR* Minus = Value >= 0.f ? TEXT("-") : TEXT("+"); // stats where a positive value is a reduction
		const int32 Whole = FMath::FloorToInt(FMath::Abs(Value) + 0.001f);
		if (Stat == TEXT("MaxHealth")) return FString::Printf(TEXT("%s%s max HP"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("MoveSpeed")) return FString::Printf(TEXT("%s%s move speed"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("Damage") || Stat == TEXT("DamageMult")) return FString::Printf(TEXT("%s%s damage"), Plus, *UpgPct(Value));
		if (Stat == TEXT("Cooldown") || Stat == TEXT("CooldownMult")) return FString::Printf(TEXT("%s%s attack speed"), Plus, *UpgPct(Value));
		if (Stat == TEXT("Area") || Stat == TEXT("AreaMult")) return FString::Printf(TEXT("%s%s area"), Plus, *UpgPct(Value));
		if (Stat == TEXT("CarryCap")) return FString::Printf(TEXT("%s%d soul capacity"), Plus, FMath::RoundToInt(FMath::Abs(Value)));
		if (Stat == TEXT("Magnet")) return FString::Printf(TEXT("%s%s pickup range"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("Armor")) return FString::Printf(TEXT("%s%s armor (%s%s effective HP)"), Plus, *UpgFmt(Value * 100.f), Plus, *UpgPct(Value));
		if (Stat == TEXT("Projectiles") || Stat == TEXT("ProjectileBonus")) return FString::Printf(TEXT("%s%d projectile%s, chain jump%s and beam%s"), Plus, Whole, Whole == 1 ? TEXT("") : TEXT("s"), Whole == 1 ? TEXT("") : TEXT("s"), Whole == 1 ? TEXT("") : TEXT("s"));
		if (Stat == TEXT("Regen")) return FString::Printf(TEXT("%s%s HP/s regen"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("DashCooldown")) return FString::Printf(TEXT("%s%ss dash cooldown"), Minus, *UpgFmt(Value));
		if (Stat == TEXT("TurretBonus")) return FString::Printf(TEXT("%s%d turret%s"), Plus, Whole, Whole == 1 ? TEXT("") : TEXT("s"));
		if (Stat == TEXT("SoulBonus")) return FString::Printf(TEXT("%s%s chance of +1 soul per kill"), Plus, *UpgPct(Value));
		if (Stat == TEXT("BankSpeed")) return FString::Printf(TEXT("%s%s banking speed"), Plus, *UpgPct(Value));
		if (Stat == TEXT("Thorns")) return FString::Printf(TEXT("%s%s thorns damage"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("PickupHeal")) return FString::Printf(TEXT("%s%s HP per soul picked up"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("Luck")) return FString::Printf(TEXT("%s%s luck (diminishing)"), Plus, *UpgPct(Value));
		if (Stat == TEXT("CritChance")) return FString::Printf(TEXT("%s%s crit chance"), Plus, *UpgPct(Value));
		if (Stat == TEXT("CritDamage")) return FString::Printf(TEXT("%s%s crit damage"), Plus, *UpgPct(Value));
		if (Stat == TEXT("Duration")) return FString::Printf(TEXT("%s%s duration"), Plus, *UpgPct(Value));
		if (Stat == TEXT("ExtraJumps")) return FString::Printf(TEXT("%s%d mid-air jump%s"), Plus, Whole, Whole == 1 ? TEXT("") : TEXT("s"));
		if (Stat == TEXT("Evasion")) return FString::Printf(TEXT("%s%s dodge chance (diminishing)"), Plus, *UpgPct(Value));
		if (Stat == TEXT("AllyRegen")) return FString::Printf(TEXT("%s%s HP/s to nearby allies"), Plus, *UpgFmt(Value));
		if (Stat == TEXT("ChestKey")) return FString::Printf(TEXT("%s%s free-chest key (chance x/(x+1))"), Plus, *UpgFmt(Value));
		return FString::Printf(TEXT("%s%s %s"), Plus, *UpgFmt(Value), *Stat.ToString());
	}

	/** What one "count" is called for each behaviour */
	static const TCHAR* UpgCountNoun(ESQWeaponBehaviour B, int32 N)
	{
		const bool bOne = N == 1;
		switch (B)
		{
		case ESQWeaponBehaviour::Projectile: return bOne ? TEXT("projectile") : TEXT("projectiles");
		case ESQWeaponBehaviour::Orbit: return bOne ? TEXT("orbiter") : TEXT("orbiters");
		case ESQWeaponBehaviour::Drop: return bOne ? TEXT("drop") : TEXT("drops");
		case ESQWeaponBehaviour::Turret: return bOne ? TEXT("turret") : TEXT("turrets");
		case ESQWeaponBehaviour::Chain: return bOne ? TEXT("jump") : TEXT("jumps");
		case ESQWeaponBehaviour::Beam: return bOne ? TEXT("beam") : TEXT("beams");
		default: return nullptr; // Arc / Aura / Nova: count is not used
		}
	}

	/** What Radius means for each behaviour */
	static const TCHAR* UpgRadiusNoun(ESQWeaponBehaviour B)
	{
		switch (B)
		{
		case ESQWeaponBehaviour::Chain: return TEXT("jump range");
		case ESQWeaponBehaviour::Beam: return TEXT("beam length");
		case ESQWeaponBehaviour::Orbit: return TEXT("orbit radius");
		case ESQWeaponBehaviour::Projectile:
		case ESQWeaponBehaviour::Turret: return nullptr; // projectile size: not worth a line
		default: return TEXT("radius");
		}
	}

	/** "75 dmg every 0.8s, 450 radius, 2 projectiles, slows 1.5s" */
	static FString UpgSummary(const FSQUpgradeDef& Def, int32 Level, float Power)
	{
		FString Out = FString::Printf(TEXT("%s dmg every %ss"), *UpgFmt(UpgradeDamage(Def, Level, Power)), *UpgFmt(UpgradeCooldown(Def, Level)));
		if (const TCHAR* RadiusNoun = UpgRadiusNoun(Def.Behaviour))
		{
			Out += FString::Printf(TEXT(", %s %s"), *UpgFmt(UpgradeRadius(Def, Level, Power)), RadiusNoun);
		}
		const int32 N = UpgradeCount(Def, Level, Power);
		if (const TCHAR* CountNoun = UpgCountNoun(Def.Behaviour, N))
		{
			Out += FString::Printf(TEXT(", %d %s"), N, CountNoun);
		}
		if (Def.Slow > 0.f) Out += FString::Printf(TEXT(", slows %ss"), *UpgFmt(Def.Slow));
		return Out;
	}

	FString DescribeUpgradeGain(const FSQUpgradeDef& Def, int32 CurrentLevel, uint8 Rarity)
	{
		if (Def.IsFallback())
		{
			// Filler cards (ASQPlayerState::RollOffers / ASQCharacter::ApplyUpgrade); amounts = StatPerLevel x rarity
			const float Mult = RarityStatMult(Rarity);
			if (Def.Id == TEXT("SQ_Gold")) return FString::Printf(TEXT("+%s gold (more each night)"), *UpgFmt(FMath::RoundToFloat(Def.StatPerLevel * Mult)));
			if (Def.Id == TEXT("SQ_XP")) return FString::Printf(TEXT("Team XP: %s of a level (split by crew size)"), *UpgPct(Def.StatPerLevel * Mult));
			return TEXT("Heal to full health");
		}

		if (Def.Kind == ESQUpgradeKind::Relic)
		{
			const float Mult = RarityStatMult(Rarity);
			FString Out = StatText(Def.Stat, Def.StatPerLevel * Mult);
			if (!Def.Stat2.IsNone() && !FMath::IsNearlyZero(Def.Stat2PerLevel))
			{
				Out += TEXT(", ") + StatText(Def.Stat2, Def.Stat2PerLevel * Mult);
			}
			return Out;
		}

		if (Def.Kind == ESQUpgradeKind::Evolution)
		{
			return TEXT("Evolves: ") + UpgSummary(Def, 1, 1.f) + TEXT(" (keeps your power)");
		}

		const float Bonus = RarityWeaponPower(Rarity);
		if (CurrentLevel <= 0)
		{
			FString Out = TEXT("New: ") + UpgSummary(Def, 1, 1.f + Bonus);
			if (Bonus > 0.f) Out += FString::Printf(TEXT(", +%s power"), *UpgPct(Bonus));
			return Out;
		}

		const int32 Next = FMath::Min(CurrentLevel + 1, Def.MaxLevel);
		TArray<FString> Parts;
		const float DmgNow = FMath::Max(0.01f, UpgradeDamage(Def, CurrentLevel, 1.f));
		const float DmgGain = UpgradeDamage(Def, Next, 1.f + Bonus) / DmgNow - 1.f;
		if (DmgGain > 0.005f) Parts.Add(FString::Printf(TEXT("+%s damage"), *UpgPct(DmgGain)));
		const float CdNow = UpgradeCooldown(Def, CurrentLevel);
		const float CdNext = UpgradeCooldown(Def, Next);
		if (CdNext < CdNow - 0.001f) Parts.Add(FString::Printf(TEXT("-%s cooldown"), *UpgPct(1.f - CdNext / FMath::Max(0.01f, CdNow))));
		if (const TCHAR* RadiusNoun = UpgRadiusNoun(Def.Behaviour))
		{
			const float RadiusGain = UpgradeRadius(Def, Next, 1.f + Bonus) - UpgradeRadius(Def, CurrentLevel, 1.f);
			if (RadiusGain >= 1.f) Parts.Add(FString::Printf(TEXT("+%s %s"), *UpgFmt(RadiusGain), RadiusNoun));
		}
		const int32 CountGain = UpgradeCount(Def, Next, 1.f + Bonus) - UpgradeCount(Def, CurrentLevel, 1.f);
		if (CountGain > 0)
		{
			if (const TCHAR* CountNoun = UpgCountNoun(Def.Behaviour, CountGain)) Parts.Add(FString::Printf(TEXT("+%d %s"), CountGain, CountNoun));
		}
		if (Parts.Num() == 0) return TEXT("Max level");
		return FString::Join(Parts, TEXT(", "));
	}

	USoundBase* Sound(const TCHAR* Name)
	{
		static TMap<FString, TWeakObjectPtr<USoundBase>> Cache;
		const FString Key(Name);
		if (const TWeakObjectPtr<USoundBase>* Found = Cache.Find(Key))
		{
			if (Found->IsValid()) return Found->Get();
		}
		const FString Path = FString::Printf(TEXT("/Game/Audio/SFX_%s.SFX_%s"), Name, Name);
		USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		Cache.Add(Key, Loaded);
		return Loaded;
	}
}
