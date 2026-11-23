// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQFxRecipe.h"
#include "SQFx.h"
#include "SQData.h"
#include "SQCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace
{
	enum class EVerb : uint8 { Flash, Glint, Wave, Swoosh, Mark, Pop, Pillar, Burst, Dust, Rise, Ring, Fan, Line, Shot, Bolt, Rain, Cracks, Shake, Pillars, Lance, None };
	enum class EAnchor : uint8 { Origin, Hit, Target, Muzzle };
	enum class EColour : uint8 { Core, Mid, Dark, Alt, White, Fixed };

	/** A number that may scale with the context: plain, x Radius ("0.9R") or x distance to the target ("1L") */
	struct FNum
	{
		float Value = 0.f;
		uint8 Unit = 0;     // 0 plain, 1 x R, 2 x L
		bool bSet = false;

		float Get(float R, float L, float Default = 0.f) const
		{
			if (!bSet) return Default;
			return Unit == 1 ? Value * R : (Unit == 2 ? Value * L : Value);
		}
	};

	struct FLayer
	{
		EVerb Verb = EVerb::None;
		ESQFxShape Shape = ESQFxShape::Sphere;
		EColour Colour = EColour::Mid;
		FLinearColor Fixed = FLinearColor::White;
		EAnchor Anchor = EAnchor::Origin;
		FNum Fwd, Up, Delay, Life, Count, Size, Size2, Speed, Radius, Angle, Sweep, Lift, Grav, Spin, Bright;
	};

	struct FBook
	{
		TMap<FName, TArray<FLayer>> Effects;    // key = "<Id>|<Event>"
		bool bLoaded = false;
	};

	FBook& Book()
	{
		static FBook B;
		return B;
	}

	FName KeyOf(FName Id, FName Event)
	{
		return FName(*(Id.ToString() + TEXT("|") + Event.ToString()));
	}

	FNum ParseNum(const FString& In)
	{
		FNum N;
		FString Text = In.TrimStartAndEnd();
		if (Text.IsEmpty()) return N;
		if (Text.EndsWith(TEXT("R"))) { N.Unit = 1; Text.LeftChopInline(1); }
		else if (Text.EndsWith(TEXT("L"))) { N.Unit = 2; Text.LeftChopInline(1); }
		N.Value = Text.IsEmpty() ? 1.f : FCString::Atof(*Text);
		N.bSet = true;
		return N;
	}

	EVerb ParseVerb(const FString& Text)
	{
		static const TCHAR* Names[] = { TEXT("Flash"), TEXT("Glint"), TEXT("Wave"), TEXT("Swoosh"), TEXT("Mark"), TEXT("Pop"), TEXT("Pillar"), TEXT("Burst"), TEXT("Dust"),
			TEXT("Rise"), TEXT("Ring"), TEXT("Fan"), TEXT("Line"), TEXT("Shot"), TEXT("Bolt"), TEXT("Rain"), TEXT("Cracks"), TEXT("Shake"), TEXT("Pillars"), TEXT("Lance") };
		for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Names)); ++i)
		{
			if (Text.Equals(Names[i], ESearchCase::IgnoreCase)) return static_cast<EVerb>(i);
		}
		return EVerb::None;
	}

	ESQFxShape ParseShape(const FString& Text)
	{
		// Same order as ESQFxShape
		static const TCHAR* Names[] = { TEXT("Sphere"), TEXT("Cube"), TEXT("Cylinder"), TEXT("Cone"), TEXT("Crescent"), TEXT("Ring"), TEXT("Flame"), TEXT("Puff"), TEXT("Spiral"),
			TEXT("Shard"), TEXT("Bolt"), TEXT("Star"), TEXT("Bat"), TEXT("Crow"), TEXT("Moth"), TEXT("Leaf"), TEXT("Feather"), TEXT("Rune"), TEXT("Seal"), TEXT("Cleaver"),
			TEXT("Claw"), TEXT("Bandage"), TEXT("Seed"), TEXT("Drop"), TEXT("Swoosh"), TEXT("Arc"), TEXT("Streak"), TEXT("Wave"), TEXT("Spikes"), TEXT("Ember"), TEXT("Petal"),
			TEXT("Chevron"), TEXT("Wisp"), TEXT("Teeth"), TEXT("Sigil"), TEXT("Cog"), TEXT("Web"), TEXT("Casket") };
		static_assert(static_cast<int32>(UE_ARRAY_COUNT(Names)) == static_cast<int32>(ESQFxShape::Count), "shape names out of step with ESQFxShape");
		for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Names)); ++i)
		{
			if (Text.Equals(Names[i], ESearchCase::IgnoreCase)) return static_cast<ESQFxShape>(i);
		}
		return ESQFxShape::Sphere;
	}

	/** Minimal CSV: header row, # comment lines, no quoted fields (the recipes never need them) */
	void Load()
	{
		FBook& B = Book();
		if (B.bLoaded) return;
		B.bLoaded = true;
		TArray<FString> Lines;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("FxRecipes.csv");
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			UE_LOG(LogTemp, Display, TEXT("SQFxRecipe: no %s (the built-in effects are used)"), *Path);
			return;
		}
		TMap<FString, int32> Col;
		int32 Layers = 0;
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT(","), false);
			if (Col.Num() == 0)
			{
				for (int32 c = 0; c < Cells.Num(); ++c) Col.Add(Cells[c].TrimStartAndEnd().ToLower(), c);
				continue;
			}
			auto Cell = [&](const TCHAR* Name) -> FString
			{
				const int32* Index = Col.Find(Name);
				return (Index && Cells.IsValidIndex(*Index)) ? Cells[*Index].TrimStartAndEnd() : FString();
			};
			FLayer L;
			L.Verb = ParseVerb(Cell(TEXT("verb")));
			if (L.Verb == EVerb::None || Cell(TEXT("id")).IsEmpty() || Cell(TEXT("event")).IsEmpty()) continue;
			const FString ShapeText = Cell(TEXT("shape"));
			if (!ShapeText.IsEmpty()) L.Shape = ParseShape(ShapeText);
			const FString ColText = Cell(TEXT("col"));
			if (ColText.StartsWith(TEXT("#"))) { L.Colour = EColour::Fixed; L.Fixed = FLinearColor(FColor::FromHex(ColText)); }
			else if (ColText.Equals(TEXT("Core"), ESearchCase::IgnoreCase)) L.Colour = EColour::Core;
			else if (ColText.Equals(TEXT("Dark"), ESearchCase::IgnoreCase)) L.Colour = EColour::Dark;
			else if (ColText.Equals(TEXT("Alt"), ESearchCase::IgnoreCase)) L.Colour = EColour::Alt;
			else if (ColText.Equals(TEXT("White"), ESearchCase::IgnoreCase)) L.Colour = EColour::White;
			const FString AtText = Cell(TEXT("at"));
			L.Anchor = AtText == TEXT("H") ? EAnchor::Hit : (AtText == TEXT("T") ? EAnchor::Target : (AtText == TEXT("M") ? EAnchor::Muzzle : EAnchor::Origin));
			L.Fwd = ParseNum(Cell(TEXT("fwd")));       L.Up = ParseNum(Cell(TEXT("up")));         L.Delay = ParseNum(Cell(TEXT("delay")));
			L.Life = ParseNum(Cell(TEXT("life")));     L.Count = ParseNum(Cell(TEXT("count")));   L.Size = ParseNum(Cell(TEXT("size")));
			L.Size2 = ParseNum(Cell(TEXT("size2")));   L.Speed = ParseNum(Cell(TEXT("speed")));   L.Radius = ParseNum(Cell(TEXT("radius")));
			L.Angle = ParseNum(Cell(TEXT("angle")));   L.Sweep = ParseNum(Cell(TEXT("sweep")));   L.Lift = ParseNum(Cell(TEXT("lift")));
			L.Grav = ParseNum(Cell(TEXT("grav")));     L.Spin = ParseNum(Cell(TEXT("spin")));     L.Bright = ParseNum(Cell(TEXT("bright")));
			B.Effects.FindOrAdd(KeyOf(FName(*Cell(TEXT("id"))), FName(*Cell(TEXT("event"))))).Add(L);
			++Layers;
		}
		UE_LOG(LogTemp, Display, TEXT("SQFxRecipe: %d effects, %d layers from FxRecipes.csv"), B.Effects.Num(), Layers);
	}

	FVector OnCircle(float Radius, float Deg, float Z = 0.f)
	{
		const float A = FMath::DegreesToRadians(Deg);
		return FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, Z);
	}

	/** Pitch, yaw (deg): the kit's +X raised by Pitch and turned to Yaw */
	FQuat Facing(float Pitch, float Yaw)
	{
		return FRotator(Pitch, Yaw, 0.f).Quaternion();
	}
}

namespace SQFxRecipe
{
	void FContext::SetColor(const FLinearColor& Color)
	{
		Mid = Color;
		Alt = Color;
		Core = FMath::Lerp(Color, FLinearColor::White, 0.7f);
		Dark = FLinearColor(Color.R * 0.25f, Color.G * 0.25f, Color.B * 0.25f, 1.f);
	}

	static FShakeFn GShakeHandler = nullptr;

	void SetShakeHandler(FShakeFn Handler)
	{
		GShakeHandler = Handler;
	}

	void Shake(UWorld* World, const FVector& Where, float Amount)
	{
		if (!World || Amount <= 0.f) return;
		if (GShakeHandler) { GShakeHandler(World, Where, Amount); return; }
		// Built in: nudge the local player's camera, fading with distance (full within 6 m, nothing past 30 m). Tuning FxShake scales it.
		const USQData* D = USQData::Get(World);
		const float Dial = D ? D->Tune(TEXT("FxShake"), 1.f) : 1.f;
		if (Dial <= 0.f) return;
		APlayerController* PC = World->GetFirstPlayerController();
		ASQCharacter* Mine = PC ? Cast<ASQCharacter>(PC->GetPawn()) : nullptr;
		if (!Mine) return;
		const float Dist = static_cast<float>(FVector::Dist(Mine->GetActorLocation(), Where));
		const float Falloff = 1.f - FMath::Clamp((Dist - 600.f) / 2400.f, 0.f, 1.f);
		if (Falloff > 0.f) Mine->NudgeCamera(FMath::Clamp(Amount * Dial * Falloff, 0.f, 1.f) * 0.5f);
	}

	bool Has(UWorld* World, FName Id, FName Event)
	{
		if (!World || !SQFx::HasMotionKit(World)) return false;
		Load();
		return Book().Effects.Contains(KeyOf(Id, Event));
	}

	bool PlayShared(UWorld* World, FName Event, const FVector& Origin, const FLinearColor& Color, float Radius, float Scale, float Detail)
	{
		FContext Ctx;
		Ctx.Origin = Origin;
		Ctx.Target = Origin;
		Ctx.bHasTarget = true;
		Ctx.Radius = Radius;
		Ctx.Scale = Scale;
		Ctx.Detail = Detail;
		Ctx.SetColor(Color);
		return Play(World, FName(TEXT("*")), Event, Ctx);
	}

	bool Play(UWorld* World, FName Id, FName Event, const FContext& C)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer || !SQFx::HasMotionKit(World)) return false;
		Load();
		const TArray<FLayer>* Layers = Book().Effects.Find(KeyOf(Id, Event));
		if (!Layers) return false;

		FVector D(C.Dir.X, C.Dir.Y, 0.0);
		D = D.IsNearlyZero() ? FVector::ForwardVector : D.GetSafeNormal();
		const float YawD = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(D.Y), static_cast<float>(D.X)));
		const float R = FMath::Max(1.f, C.Radius);
		const FVector Target = C.bHasTarget ? C.Target : C.Origin + D * R;
		const float L = FMath::Max(1.f, static_cast<float>((Target - C.Origin).Size()));
		const float K = FMath::Max(0.1f, C.Scale);
		const float GroundZ = static_cast<float>(C.Origin.Z);

		for (const FLayer& Row : *Layers)
		{
			FLinearColor Col = C.Mid;
			switch (Row.Colour)
			{
			case EColour::Core: Col = C.Core; break;
			case EColour::Dark: Col = C.Dark; break;
			case EColour::Alt: Col = C.Alt; break;
			case EColour::White: Col = FLinearColor::White; break;
			case EColour::Fixed: Col = Row.Fixed; break;
			default: break;
			}
			FVector Base = C.Origin;
			if (Row.Anchor == EAnchor::Hit) Base = C.Origin + D * (R * 0.55f);
			else if (Row.Anchor == EAnchor::Target) Base = Target;
			else if (Row.Anchor == EAnchor::Muzzle) Base = C.Origin + D * 45.f + FVector(0.f, 0.f, 105.f);
			const FVector Pos = Base + D * Row.Fwd.Get(R, L) + FVector(0.f, 0.f, Row.Up.Get(R, L));
			const float Delay = FMath::Max(0.f, C.Beat + Row.Delay.Get(R, L));
			const float Life = Row.Life.Get(R, L, 0.3f);
			const int32 Count = FMath::Max(1, FMath::RoundToInt(Row.Count.Get(R, L, 1.f) * (Row.Count.Get(R, L, 1.f) > 2.f ? FMath::Clamp(C.Detail, 0.25f, 2.f) : 1.f)));
			const float Size = Row.Size.Get(R, L, 30.f) * K;
			const float Size2 = Row.Size2.bSet ? Row.Size2.Get(R, L) * K : -1.f;
			const float Speed = Row.Speed.Get(R, L);
			const float Rad = Row.Radius.Get(R, L, R);
			const float Ang = Row.Angle.Get(R, L);
			const float Sweep = Row.Sweep.Get(R, L);
			const float Lift = Row.Lift.Get(R, L);
			const float Grav = Row.Grav.Get(R, L);
			const float Spin = Row.Spin.Get(R, L);
			const float Bright = Row.Bright.Get(R, L, 1.f);

			switch (Row.Verb)
			{
			case EVerb::Flash: SQFx::Flash(World, Pos, Col, Size, Life, Delay); break;
			case EVerb::Glint: SQFx::Glint(World, Pos, Col, Size, Delay); break;
			case EVerb::Cracks: SQFx::GroundCracks(World, Pos, Rad, Col, Count, Delay + Life, 7.f * K, Delay); break;
			case EVerb::Shake:
			{
				const float Amount = Row.Size.Get(R, L, 0.3f);
				if (Delay <= 0.01f) Shake(World, C.Origin, Amount);
				else
				{
					FTimerHandle Handle;
					TWeakObjectPtr<UWorld> WeakWorld(World);
					const FVector Where = C.Origin;
					World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Where, Amount]()
					{
						if (UWorld* LaterWorld = WeakWorld.Get()) Shake(LaterWorld, Where, Amount);
					}), Delay, false);
				}
				break;
			}
			case EVerb::Wave:
			{
				const float Tall = Row.Size2.bSet ? Row.Size2.Get(R, L) : 1.f;
				FSQFxParticle P;
				P.Shape = Row.Shape == ESQFxShape::Teeth ? ESQFxShape::Teeth : ESQFxShape::Wave;
				P.Pos = Pos; P.Color = Col; P.Life = Life; P.Delay = Delay;
				P.SizeStart = FVector(Rad * 0.3f, Rad * 0.3f, Rad * 0.6f * Tall);
				P.SizeEnd = FVector(Rad * 2.f, Rad * 2.f, Rad * Tall);
				P.bEaseOut = true; P.bShrinkOut = false; P.FadeOut = 0.55f; P.Bright = Bright;
				P.Rot = Facing(Ang, YawD);
				P.SpinRate = FMath::DegreesToRadians(Spin);
				SQFx::Emit(World, P);
				break;
			}
			case EVerb::Swoosh:
				SQFx::Swoosh(World, Pos, YawD + Ang, Row.Sweep.bSet ? Sweep : 200.f, Rad, Col, FMath::Max(0.2f, Row.Size.Get(R, L, 1.f)), Life, Delay, Bright, Lift,
					Row.Shape == ESQFxShape::Arc ? ESQFxShape::Arc : ESQFxShape::Swoosh);
				break;
			case EVerb::Mark:
			{
				FSQFxParticle P;
				P.Shape = Row.Shape; P.Pos = Pos + FVector(0.f, 0.f, 3.f); P.Color = Col; P.Life = Life; P.Delay = Delay;
				P.SizeStart = FVector(Size * 0.4f); P.SizeEnd = FVector(Size);
				P.bEaseOut = true; P.bShrinkOut = false; P.FadeIn = 0.04f; P.FadeOut = 0.4f; P.Bright = Bright;
				P.Rot = Facing(0.f, YawD + Ang); P.SpinAxis = FVector::UpVector; P.SpinRate = FMath::DegreesToRadians(Spin);
				SQFx::Emit(World, P);
				break;
			}
			case EVerb::Pop:
			{
				FSQFxParticle P;
				P.Shape = Row.Shape; P.Pos = Pos; P.Vel = D * Speed + FVector(0.f, 0.f, Lift); P.Color = Col; P.Life = Life; P.Delay = Delay;
				P.SizeStart = FVector(Size * 0.35f); P.SizeEnd = FVector(Size2 >= 0.f ? Size2 : Size);
				P.bEaseOut = true; P.FadeOut = 0.45f; P.Bright = Bright;
				P.Rot = Facing(Sweep, YawD + Ang); P.SpinAxis = FVector::UpVector; P.SpinRate = FMath::DegreesToRadians(Spin);
				P.Drag = Speed != 0.f ? 2.2f : 0.f; P.Gravity = Grav;
				SQFx::Emit(World, P);
				break;
			}
			case EVerb::Pillar:
			{
				FSQFxParticle P;
				P.Shape = ESQFxShape::Streak; P.Pos = Pos + FVector(0.f, 0.f, Lift * 0.5f); P.Color = Col; P.Life = Life; P.Delay = Delay;
				P.SizeStart = SQFx::StreakSize(Lift, Size); P.SizeEnd = SQFx::StreakSize(Lift * 1.15f, Size * 0.25f);
				P.bEaseOut = true; P.bShrinkOut = false; P.FadeIn = 0.03f; P.FadeOut = 0.6f; P.Bright = Bright;
				P.Rot = Facing(90.f, 0.f);
				SQFx::Emit(World, P);
				break;
			}
			case EVerb::Burst:
			case EVerb::Dust:
			{
				const bool bStreak = Row.Shape == ESQFxShape::Streak;
				for (int32 i = 0; i < Count; ++i)
				{
					const float Yaw = Ang > 0.f ? YawD + FMath::FRandRange(-Ang, Ang) : YawD;
					const FVector Out = OnCircle(1.f, Yaw) + FVector(0.f, 0.f, Lift + FMath::FRandRange(-0.15f, 0.25f));
					FSQFxParticle P;
					P.Color = Col;
					if (Row.Verb == EVerb::Dust)
					{
						P.Shape = ESQFxShape::Puff;
						P.Pos = Pos + OnCircle(FMath::FRandRange(0.f, Row.Radius.bSet ? Rad : 0.f), FMath::FRandRange(0.f, 360.f));
						P.Vel = Out * Speed * FMath::FRandRange(0.5f, 1.f);
						P.Life = Life * FMath::FRandRange(0.8f, 1.2f); P.Delay = Delay + FMath::FRandRange(0.f, 0.05f);
						P.SizeStart = FVector(Size * 0.6f); P.SizeEnd = FVector(Size * 1.7f);
						P.Drag = 3.f; P.bEaseOut = true;
						P.Rot = Facing(0.f, FMath::FRandRange(0.f, 360.f)); P.SpinRate = FMath::FRandRange(-1.5f, 1.5f);
						SQFx::Emit(World, P);
						continue;
					}
					const float End = Size2 >= 0.f ? Size2 : Size * 0.4f;
					P.Shape = Row.Shape; P.Pos = Pos; P.Vel = Out * Speed * FMath::FRandRange(0.6f, 1.1f);
					P.Life = Life * FMath::FRandRange(0.75f, 1.15f); P.Delay = Delay + FMath::FRandRange(0.f, 0.06f);
					P.SizeStart = bStreak ? SQFx::StreakSize(Size, Size * 0.11f) : FVector(Size);
					P.SizeEnd = bStreak ? SQFx::StreakSize(End, End * 0.11f) : FVector(End);
					P.Gravity = Grav; P.Drag = bStreak ? 4.f : 1.6f; P.FloorZ = Grav < 0.f ? GroundZ : -1.0e9f;
					P.bAlignToVelocity = true; P.SpinRate = FMath::FRandRange(-Spin, Spin); P.SpinAxis = FVector::ForwardVector;
					P.bShrinkOut = !bStreak; P.FadeOut = 0.5f; P.Bright = Bright;
					SQFx::Emit(World, P);
				}
				break;
			}
			case EVerb::Rise:
				for (int32 i = 0; i < Count; ++i)
				{
					FSQFxParticle P;
					P.Shape = Row.Shape; P.Color = Col;
					P.Pos = Pos + OnCircle(Rad * FMath::Sqrt(FMath::FRand()), FMath::FRandRange(0.f, 360.f));
					P.Vel = FVector(FMath::FRandRange(-12.f, 12.f), FMath::FRandRange(-12.f, 12.f), Speed * FMath::FRandRange(0.7f, 1.2f));
					P.Life = Life * FMath::FRandRange(0.75f, 1.15f); P.Delay = Delay + FMath::FRandRange(0.f, Life * 0.4f);
					P.SizeStart = FVector(Size * FMath::FRandRange(0.75f, 1.15f)); P.SizeEnd = FVector(Size2 >= 0.f ? Size2 : Size * 0.5f);
					P.Gravity = Grav; P.Rot = Facing(0.f, FMath::FRandRange(0.f, 360.f)); P.SpinAxis = FVector::UpVector;
					P.SpinRate = FMath::DegreesToRadians(Spin) * FMath::FRandRange(0.6f, 1.2f);
					P.bShrinkOut = false; P.FadeIn = 0.05f; P.FadeOut = 0.5f; P.Bright = Bright;
					SQFx::Emit(World, P);
				}
				break;
			case EVerb::Ring:
			{
				const float Start = Ang == 0.f ? FMath::FRandRange(0.f, 360.f) : YawD + Ang;
				for (int32 i = 0; i < Count; ++i)
				{
					const float A = Start + 360.f * i / Count;
					const FVector Radial = OnCircle(1.f, A);
					FSQFxParticle P;
					P.Shape = Row.Shape; P.Color = Col; P.Pos = Pos + Radial * Rad; P.Vel = Radial * Speed + FVector(0.f, 0.f, Lift);
					P.Life = Life; P.Delay = Delay + i * Sweep;
					P.SizeStart = FVector(Size * 0.35f); P.SizeEnd = FVector(Size2 >= 0.f ? Size2 : Size);
					P.bEaseOut = true; P.Rot = Facing(0.f, A); P.Drag = Speed != 0.f ? 1.2f : 0.f;
					P.OrbitRate = FMath::DegreesToRadians(Spin); P.OrbitPivot = Pos;
					P.FadeIn = 0.03f; P.FadeOut = 0.4f; P.Bright = Bright; P.Gravity = Grav;
					SQFx::Emit(World, P);
				}
				break;
			}
			case EVerb::Fan:
				for (int32 i = 0; i < Count; ++i)
				{
					const float A = YawD + (-Ang + 2.f * Ang * (Count > 1 ? static_cast<float>(i) / (Count - 1) : 0.5f));
					const FVector Radial = OnCircle(1.f, A);
					FSQFxParticle P;
					P.Shape = Row.Shape; P.Color = Col; P.Pos = Pos + Radial * Rad; P.Vel = Radial * Speed;
					P.Life = Life; P.Delay = Delay + i * Sweep;
					P.SizeStart = FVector(Size * 0.35f); P.SizeEnd = FVector(Size2 >= 0.f ? Size2 : Size);
					P.bEaseOut = true; P.Rot = Facing(Lift, A); P.Drag = Speed != 0.f ? 2.5f : 0.f;
					P.FadeIn = 0.02f; P.FadeOut = 0.45f; P.Bright = Bright;
					SQFx::Emit(World, P);
				}
				break;
			case EVerb::Line:
			{
				const FVector End = Pos + D * Rad;
				for (int32 i = 0; i < Count; ++i)
				{
					const float T = (i + 0.5f) / Count;
					FSQFxParticle P;
					P.Shape = Row.Shape; P.Color = Col;
					P.Pos = FMath::Lerp(Pos, End, T) + FVector(0.f, 0.f, Lift * FMath::Sin(PI * T));
					P.Life = Life; P.Delay = Delay + i * Sweep;
					P.SizeStart = FVector(Size * 0.4f); P.SizeEnd = FVector(Size2 >= 0.f ? Size2 : Size);
					P.bEaseOut = true; P.Rot = Facing(0.f, YawD + Ang); P.SpinAxis = FVector::UpVector; P.SpinRate = FMath::DegreesToRadians(Spin);
					P.FadeIn = 0.02f; P.FadeOut = 0.5f; P.Bright = Bright;
					SQFx::Emit(World, P);
				}
				break;
			}
			case EVerb::Pillars:
			{
				// A row of light columns marching along the aim: reads from behind the reaper, where a flat ground line is foreshortened away
				const FVector End = Pos + D * Rad;
				for (int32 i = 0; i < Count; ++i)
				{
					const float T = (i + 0.5f) / Count;
					const float Tall = Lift * (0.7f + 0.5f * FMath::Sin(PI * T));
					FSQFxParticle P;
					P.Shape = ESQFxShape::Streak; P.Color = Col; P.Pos = FMath::Lerp(Pos, End, T) + FVector(0.f, 0.f, Tall * 0.5f);
					P.Life = Life; P.Delay = Delay + i * Sweep;
					P.SizeStart = SQFx::StreakSize(Tall, Size); P.SizeEnd = SQFx::StreakSize(Tall * 1.2f, Size * 0.2f);
					P.bEaseOut = true; P.bShrinkOut = false; P.FadeIn = 0.03f; P.FadeOut = 0.6f; P.Bright = Bright;
					P.Rot = Facing(90.f, 0.f);
					SQFx::Emit(World, P);
				}
				break;
			}
			case EVerb::Lance:
			{
				// One held beam from the anchor along the aim: a coloured body with a thin hot core
				const FVector Span = D * Rad;
				for (int32 Pass = 0; Pass < 2; ++Pass)
				{
					const float W = Pass == 0 ? Size : Size * 0.4f;
					FSQFxParticle P;
					P.Shape = ESQFxShape::Streak; P.Color = Pass == 0 ? Col : C.Core; P.Pos = Pos + Span * 0.5f;
					P.Life = Pass == 0 ? Life : Life * 0.7f; P.Delay = Delay;
					P.Rot = FRotationMatrix::MakeFromX(Span).ToQuat();
					P.SizeStart = SQFx::StreakSize(Rad, W); P.SizeEnd = SQFx::StreakSize(Rad, W * 0.15f);
					P.bShrinkOut = false; P.bEaseOut = true; P.FadeIn = 0.02f; P.FadeOut = 0.6f; P.Bright = Pass == 0 ? Bright : Bright * 1.2f;
					SQFx::Emit(World, P);
				}
				break;
			}
			case EVerb::Shot:
			{
				const FVector Span = D * Rad;
				FSQFxParticle P;
				P.Shape = Row.Shape; P.Color = Col; P.Pos = Pos; P.Vel = Span / FMath::Max(0.03f, Life);
				P.Life = Life; P.Delay = Delay; P.SizeStart = P.SizeEnd = FVector(Size);
				P.bAlignToVelocity = true; P.bShrinkOut = false; P.SpinRate = FMath::DegreesToRadians(Spin);
				P.TrailEvery = 0.018f; P.TrailSize = Size2 >= 0.f ? Size2 : Size * 0.25f; P.TrailColor = C.Mid;
				P.DeathFx = Count > 8 ? 2 : 1; P.DeathCount = Count; P.DeathSize = Size * 0.14f; P.DeathColor = C.Core; P.Bright = Bright;
				SQFx::Emit(World, P);
				break;
			}
			case EVerb::Bolt:
			{
				const FVector Top = Pos + FVector(0.f, 0.f, Lift);
				FVector Prev = Top;
				const int32 Segs = FMath::Max(3, Count);
				for (int32 i = 1; i <= Segs; ++i)
				{
					const float T = static_cast<float>(i) / Segs;
					FVector Next = FMath::Lerp(Top, Pos, T);
					if (i < Segs) Next += FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * (Size * 2.2f);
					const FVector Span = Next - Prev;
					const float Len = static_cast<float>(Span.Size());
					for (int32 Pass = 0; Pass < 2 && Len > 1.f; ++Pass)
					{
						const float W = Pass == 0 ? Size * 0.5f : Size;
						FSQFxParticle P;
						P.Shape = ESQFxShape::Streak; P.Color = Pass == 0 ? C.Core : Col; P.Pos = (Prev + Next) * 0.5f;
						P.Life = Pass == 0 ? Life * 0.7f : Life; P.Delay = Delay + 0.01f * i;
						P.Rot = FRotationMatrix::MakeFromX(Span).ToQuat();
						P.SizeStart = SQFx::StreakSize(Len * 1.15f, W * 1.6f); P.SizeEnd = SQFx::StreakSize(Len * 1.15f, W * 0.3f);
						P.bShrinkOut = false; P.bEaseOut = true; P.FadeOut = 0.6f; P.Bright = Pass == 0 ? Bright * 1.2f : Bright;
						SQFx::Emit(World, P);
					}
					Prev = Next;
				}
				break;
			}
			case EVerb::Rain:
				for (int32 i = 0; i < Count; ++i)
				{
					const FVector Spot = Pos + OnCircle(Rad * FMath::Sqrt(FMath::FRand()), FMath::FRandRange(0.f, 360.f));
					const float When = Delay + FMath::FRandRange(0.f, Sweep);
					const float Fall = FMath::Max(0.08f, Lift / FMath::Max(1.f, Speed));
					FSQFxParticle P;
					P.Shape = Row.Shape; P.Color = Col; P.Pos = Spot + FVector(0.f, 0.f, Lift); P.Vel = FVector(0.f, 0.f, -Speed);
					P.Life = Fall; P.Delay = When; P.SizeStart = P.SizeEnd = FVector(Size);
					P.bAlignToVelocity = true; P.bShrinkOut = false; P.FadeIn = 0.04f;
					P.DeathFx = 1; P.DeathCount = 4; P.DeathSize = Size * 0.12f; P.DeathColor = C.Core; P.Bright = Bright;
					SQFx::Emit(World, P);
					FSQFxParticle W;
					W.Shape = ESQFxShape::Wave; W.Color = Col; W.Pos = Spot + FVector(0.f, 0.f, 3.f);
					W.Life = 0.25f; W.Delay = When + Fall;
					W.SizeStart = FVector(Size * 0.5f); W.SizeEnd = FVector(Size * 2.4f, Size * 2.4f, Size * 1.2f);
					W.bEaseOut = true; W.bShrinkOut = false; W.FadeOut = 0.6f; W.Bright = Bright * 0.8f;
					SQFx::Emit(World, W);
				}
				break;
			default: break;
			}
		}
		return true;
	}
}
