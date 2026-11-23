// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQFxTheme.h"
#include "SQFxRecipe.h"
#include "SQFx.h"
#include "Engine/World.h"

// Effect language (approved Nov 10 2026): every effect is three beats - a short anticipation glint, a bright impact with a dark
// under-layer (bright core -> coloured body -> dark edge), and a lingering tail (embers, smoke, marks) - built from the stylised
// SQFx kit shapes in the monster's own colours. Shapes read from the top-down camera (flat in XY) or stand up (+Z).

namespace
{
	using SQTheme::EEntrance;
	using Shape = ESQFxShape;

	/** Beat offset added to every particle of the effect being built (PlayAttack lands its accent on the weapon's impact frame) */
	float GBeat = 0.f;

	struct FBeatScope
	{
		explicit FBeatScope(float Offset) { GBeat = FMath::Max(0.f, Offset); }
		~FBeatScope() { GBeat = 0.f; }
	};

	FLinearColor Hx(uint32 Hex)
	{
		return FLinearColor(FColor(static_cast<uint8>((Hex >> 16) & 255u), static_cast<uint8>((Hex >> 8) & 255u), static_cast<uint8>(Hex & 255u)));
	}

	struct FPal
	{
		FLinearColor Core;  // white-hot centre
		FLinearColor Mid;   // the monster's colour
		FLinearColor Dark;  // shadow / edge layer
		FLinearColor Alt;   // a second accent
	};

	float YawOf(const FVector& V)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(V.Y), static_cast<float>(V.X)));
	}

	FVector Flat(const FVector& V)
	{
		FVector F(V.X, V.Y, 0.0);
		return F.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : F.GetSafeNormal();
	}

	FVector OnCircle(const FVector& C, float R, float DegA, float Z = 0.f)
	{
		const float A = FMath::DegreesToRadians(DegA);
		return C + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Z);
	}

	// ---------------------------------------------------------------- building blocks

	/** One kit particle: pops in (ease-out growth), lives, shrinks out */
	void Pop(UWorld* W, Shape S, const FVector& Pos, const FLinearColor& Col, float Size, float Life, float Delay = 0.f, float Yaw = 0.f,
		float Spin = 0.f, const FVector& Vel = FVector::ZeroVector, float Pitch = 0.f, float Gravity = 0.f)
	{
		FSQFxParticle P;
		P.Pos = Pos;
		P.Vel = Vel;
		P.Color = Col;
		P.Shape = S;
		P.Life = Life;
		P.Delay = Delay + GBeat;
		P.SizeStart = FVector(Size * 0.35f);
		P.SizeEnd = FVector(Size);
		P.Rot = FQuat(FRotator(Pitch, Yaw, 0.f));
		P.SpinAxis = FVector::UpVector;
		P.SpinRate = Spin;
		P.Gravity = Gravity;
		P.Drag = Vel.IsNearlyZero() ? 0.f : 2.2f;
		P.bEaseOut = true;
		P.bShrinkOut = true;
		SQFx::Emit(W, P);
	}

	/** A layered slash: dark under-layer, coloured body, hot core, each a beat apart (convex side toward Dir) */
	void Slash(UWorld* W, const FPal& Pal, const FVector& Center, float Yaw, float Size, float Delay = 0.f, float Life = 0.3f)
	{
		Pop(W, Shape::Crescent, Center + FVector(0.0, 0.0, -2.0), Pal.Dark, Size * 1.12f, Life * 1.2f, Delay, Yaw, 0.f);
		Pop(W, Shape::Crescent, Center, Pal.Mid, Size, Life, Delay + 0.02f, Yaw, 0.f);
		Pop(W, Shape::Crescent, Center + FVector(0.0, 0.0, 2.0), Pal.Core, Size * 0.72f, Life * 0.6f, Delay + 0.03f, Yaw, 0.f);
	}

	/** Expanding ring band (shockwave) from R0 to R1 */
	void Wave(UWorld* W, const FVector& C, const FLinearColor& Col, float R0, float R1, float Life, float Delay = 0.f, float Z = 6.f)
	{
		FSQFxParticle P;
		P.Pos = C + FVector(0.0, 0.0, Z);
		P.Color = Col;
		P.Shape = Shape::Ring;
		P.Life = Life;
		P.Delay = Delay + GBeat;
		P.SizeStart = FVector(R0 * 2.f, R0 * 2.f, 60.f);
		P.SizeEnd = FVector(R1 * 2.f, R1 * 2.f, 30.f);
		P.bEaseOut = true;
		P.bShrinkOut = false;
		SQFx::Emit(W, P);
	}

	/** Count shapes thrown out in a cone around Dir (HalfDeg 180 = all round), aligned to their flight */
	void Spray(UWorld* W, Shape S, const FVector& At, const FVector& Dir, float HalfDeg, int32 Count, float Speed, float Size, float Life,
		const FLinearColor& Col, float Gravity = 0.f, float Delay = 0.f, float Up = 0.f, float FloorZ = -1.0e9f)
	{
		const float Base = YawOf(Dir);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Yaw = Base + FMath::FRandRange(-HalfDeg, HalfDeg);
			const FVector D = OnCircle(FVector::ZeroVector, 1.f, Yaw) + FVector(0.0, 0.0, Up + FMath::FRandRange(-0.15f, 0.25f));
			FSQFxParticle P;
			P.Pos = At;
			P.Vel = D * Speed * FMath::FRandRange(0.6f, 1.1f);
			P.Color = Col;
			P.Shape = S;
			P.Life = Life * FMath::FRandRange(0.75f, 1.15f);
			P.Delay = Delay + GBeat + FMath::FRandRange(0.f, 0.06f);
			P.SizeStart = FVector(Size);
			P.SizeEnd = FVector(Size * 0.5f);
			P.Gravity = Gravity;
			P.Drag = 1.6f;
			P.FloorZ = FloorZ;
			P.bAlignToVelocity = true;
			P.SpinRate = FMath::FRandRange(-6.f, 6.f);
			P.SpinAxis = FVector(1.0, 0.0, 0.0);
			SQFx::Emit(W, P);
		}
	}

	/** Count shapes on a circle flying tangentially (Inward > 0 pulls them to the centre: a gathering swirl) */
	void Swirl(UWorld* W, Shape S, const FVector& C, float R, int32 Count, float Speed, float Rise, float Size, float Life, const FLinearColor& Col,
		float Delay = 0.f, float Inward = 0.f, float Z = 40.f)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = 360.f * static_cast<float>(i) / static_cast<float>(FMath::Max(1, Count)) + FMath::FRandRange(-10.f, 10.f);
			const FVector P0 = OnCircle(C, R, A, Z + FMath::FRandRange(0.f, 60.f));
			const FVector Tan = OnCircle(FVector::ZeroVector, 1.f, A + 90.f);
			const FVector In = (C - P0) * FVector(1.0, 1.0, 0.0);
			FSQFxParticle P;
			P.Pos = P0;
			P.Vel = Tan * Speed + FVector(0.0, 0.0, Rise) + In.GetSafeNormal() * Inward;
			P.Color = Col;
			P.Shape = S;
			P.Life = Life;
			P.Delay = Delay + GBeat + 0.015f * static_cast<float>(i % 6);
			P.SizeStart = FVector(Size * 0.6f);
			P.SizeEnd = FVector(Size);
			P.Drag = 0.6f;
			P.bAlignToVelocity = true;
			P.bEaseOut = true;
			SQFx::Emit(W, P);
		}
	}

	/** Shapes falling from Height onto a disc of radius R around C */
	void Rain(UWorld* W, Shape S, const FVector& C, float R, int32 Count, float Height, float Size, float Life, const FLinearColor& Col, float Delay = 0.f, float Spread = 0.6f)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector Land = OnCircle(C, R * FMath::Sqrt(FMath::FRand()), FMath::FRandRange(0.f, 360.f));
			FSQFxParticle P;
			P.Pos = Land + FVector(0.0, 0.0, Height);
			P.Vel = FVector(0.0, 0.0, -Height / FMath::Max(0.1f, Life * 0.55f));
			P.Color = Col;
			P.Shape = S;
			P.Life = Life;
			P.Delay = Delay + GBeat + FMath::FRandRange(0.f, Spread);
			P.SizeStart = FVector(Size);
			P.SizeEnd = FVector(Size * 0.8f);
			P.FloorZ = static_cast<float>(C.Z) + 4.f;
			P.Rot = FQuat(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
			P.DeathFx = 1;
			P.DeathCount = 4;
			P.DeathSize = Size * 0.12f;
			P.DeathColor = Col;
			SQFx::Emit(W, P);
		}
	}

	/** Count shapes laid along A -> B, appearing in sequence (trails, chains, footprints) */
	void Trail(UWorld* W, Shape S, const FVector& A, const FVector& B, int32 Count, float Size, float Life, const FLinearColor& Col, float Delay = 0.f, float Step = 0.025f, float Spin = 0.f)
	{
		const float Yaw = YawOf(B - A);
		for (int32 i = 0; i < Count; ++i)
		{
			const float T = Count > 1 ? static_cast<float>(i) / static_cast<float>(Count - 1) : 0.5f;
			Pop(W, S, FMath::Lerp(A, B, static_cast<double>(T)) + FVector(0.0, 0.0, FMath::FRandRange(-6.f, 6.f)), Col, Size * FMath::FRandRange(0.8f, 1.15f), Life, Delay + Step * static_cast<float>(i),
				Yaw + FMath::FRandRange(-20.f, 20.f), Spin);
		}
	}

	/** Upright shapes (Flame / Spiral / Drop) in a ring, rising */
	void Crown(UWorld* W, Shape S, const FVector& C, float R, int32 Count, float Size, float Life, const FLinearColor& Col, float Delay = 0.f, float Rise = 60.f)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = 360.f * static_cast<float>(i) / static_cast<float>(FMath::Max(1, Count));
			Pop(W, S, OnCircle(C, R, A, Size * 0.4f), Col, Size * FMath::FRandRange(0.8f, 1.2f), Life, Delay + 0.02f * static_cast<float>(i % 4), A, 0.f, FVector(0.0, 0.0, Rise));
		}
	}

	/** Soft cloud: puffs clustered around C that swell and drift up */
	void Cloud(UWorld* W, const FVector& C, float R, int32 Count, float Size, float Life, const FLinearColor& Col, float Delay = 0.f, float Rise = 50.f)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector At = OnCircle(C, R * FMath::Sqrt(FMath::FRand()), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(10.f, Size * 0.8f));
			FSQFxParticle P;
			P.Pos = At;
			P.Vel = (At - C).GetSafeNormal() * 60.f + FVector(0.0, 0.0, Rise);
			P.Color = Col;
			P.Shape = Shape::Puff;
			P.Life = Life * FMath::FRandRange(0.8f, 1.2f);
			P.Delay = Delay + GBeat + FMath::FRandRange(0.f, 0.08f);
			P.SizeStart = FVector(Size * 0.4f);
			P.SizeEnd = FVector(Size * FMath::FRandRange(0.9f, 1.3f));
			P.Rot = FQuat(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
			P.Drag = 1.8f;
			P.bEaseOut = true;
			SQFx::Emit(W, P);
		}
	}

	/** A vertical lightning strike from the sky onto At (Bolt shapes pitched upright, flickering) */
	void Strike(UWorld* W, const FPal& Pal, const FVector& At, float Height, float Delay = 0.f)
	{
		const int32 Segs = 4;
		for (int32 i = 0; i < Segs; ++i)
		{
			const FVector Mid = At + FVector(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), Height * (static_cast<float>(i) + 0.5f) / static_cast<float>(Segs));
			const float Seg = Height / static_cast<float>(Segs) * 1.3f;
			Pop(W, Shape::Bolt, Mid, Pal.Core, Seg, 0.22f, Delay, FMath::FRandRange(0.f, 360.f), 0.f, FVector::ZeroVector, 90.f);
			Pop(W, Shape::Bolt, Mid, Pal.Mid, Seg * 1.15f, 0.3f, Delay + 0.03f, FMath::FRandRange(0.f, 360.f), 0.f, FVector::ZeroVector, 90.f);
		}
		SQFx::Flash(W, At + FVector(0.0, 0.0, 40.0), Pal.Core, 160.f, 0.12f, Delay + GBeat);
		Wave(W, At, Pal.Mid, 20.f, 180.f, 0.35f, Delay);
		Spray(W, Shape::Star, At + FVector(0.0, 0.0, 20.0), FVector(1.0, 0.0, 0.0), 180.f, 10, 420.f, 22.f, 0.4f, Pal.Core, -600.f, Delay, 0.5f);
	}

	// ---------------------------------------------------------------- the 21 palettes
	FPal PalFor(FName Id)
	{
		static const TMap<FName, FPal> Pals = []()
		{
			TMap<FName, FPal> M;
			M.Add(TEXT("Neckless"), FPal{ Hx(0xEAFFB8), Hx(0x7CF0B8), Hx(0x1E5A3A), Hx(0xF08A24) });
			M.Add(TEXT("Tallow"), FPal{ Hx(0xFFF3B0), Hx(0xFFA030), Hx(0x8A3A0A), Hx(0x6AAA3A) });
			M.Add(TEXT("Bonesmith"), FPal{ Hx(0xFFE6C0), Hx(0xD8A85A), Hx(0x5A3A22), Hx(0xFF5A4A) });
			M.Add(TEXT("Pallbearers"), FPal{ Hx(0xE8FFF4), Hx(0x7CF0B8), Hx(0x16141C), Hx(0xC9A24A) });
			M.Add(TEXT("Widow"), FPal{ Hx(0xFFFFFF), Hx(0xC8D8FF), Hx(0x5A4A9A), Hx(0xB98CFF) });
			M.Add(TEXT("Beak"), FPal{ Hx(0xF0FFF4), Hx(0x9A5AE0), Hx(0x2E1C3A), Hx(0x7CF0B8) });
			M.Add(TEXT("Tickwell"), FPal{ Hx(0xFFF8D8), Hx(0xFFD26A), Hx(0x1E2654), Hx(0xB98CFF) });
			M.Add(TEXT("Grim"), FPal{ Hx(0xE8FFFA), Hx(0x5CF2D6), Hx(0x0A0812), Hx(0x3A2F55) });
			M.Add(TEXT("Collector"), FPal{ Hx(0xFFE8B0), Hx(0xD8263A), Hx(0x2A0810), Hx(0xFFD26A) });
			M.Add(TEXT("Monk"), FPal{ Hx(0xFFF6DC), Hx(0xE8C88A), Hx(0x6A5232), Hx(0x5CF2D6) });
			M.Add(TEXT("Femur"), FPal{ Hx(0xFFFFFF), Hx(0xE8E2D0), Hx(0x4A3A2E), Hx(0xFFD26A) });
			M.Add(TEXT("Digby"), FPal{ Hx(0xF0F8FF), Hx(0x7FA8FF), Hx(0x26304A), Hx(0xB8E0B0) });
			M.Add(TEXT("Lookout"), FPal{ Hx(0xFFF3B0), Hx(0xFFC24A), Hx(0x4A5060), Hx(0x9AA0AC) });
			M.Add(TEXT("Keeper"), FPal{ Hx(0xFFF8D8), Hx(0xFFD27A), Hx(0x26321A), Hx(0x9AC05A) });
			M.Add(TEXT("Intern"), FPal{ Hx(0xFFFFFF), Hx(0xFF5A40), Hx(0x5A1A14), Hx(0xFFE08A) });
			M.Add(TEXT("Sweep"), FPal{ Hx(0xFFF0A0), Hx(0xE0C070), Hx(0x111114), Hx(0xB84A3A) });
			M.Add(TEXT("Solace"), FPal{ Hx(0xF4FBFF), Hx(0x7FA8FF), Hx(0x1A2A4A), Hx(0x7CF0B8) });
			M.Add(TEXT("Mort"), FPal{ Hx(0xF2FFF8), Hx(0x5CF2D6), Hx(0x2A3420), Hx(0xF2EBDD) });
			M.Add(TEXT("Actuary"), FPal{ Hx(0xFFFBEA), Hx(0xF2E3C0), Hx(0x5A4A3A), Hx(0xD8434B) });
			M.Add(TEXT("Ectoplasm"), FPal{ Hx(0xF4FFF8), Hx(0x7CF0B8), Hx(0x3A1E48), Hx(0xE0304A) });
			M.Add(TEXT("Vexa"), FPal{ Hx(0xFFFFFF), Hx(0xD8434B), Hx(0x3A1A1E), Hx(0xB9C0CC) });
			return M;
		}();

		const FPal* Found = Pals.Find(Id);
		return Found ? *Found : FPal{ Hx(0xFFFFFF), Hx(0x5CF2D6), Hx(0x16121C), Hx(0xFFD26A) };
	}
}

namespace SQTheme
{
	FLinearColor Colour(FName ReaperId, int32 Role)
	{
		const FPal Pal = PalFor(ReaperId);
		return Role == 0 ? Pal.Core : (Role == 2 ? Pal.Dark : (Role == 3 ? Pal.Alt : Pal.Mid));
	}

	bool Has(FName ReaperId)
	{
		static const TSet<FName> Ids = { TEXT("Neckless"), TEXT("Tallow"), TEXT("Bonesmith"), TEXT("Pallbearers"), TEXT("Widow"), TEXT("Beak"), TEXT("Tickwell"),
			TEXT("Grim"), TEXT("Collector"), TEXT("Monk"), TEXT("Femur"), TEXT("Digby"), TEXT("Lookout"), TEXT("Keeper"), TEXT("Intern"), TEXT("Sweep"),
			TEXT("Solace"), TEXT("Mort"), TEXT("Actuary"), TEXT("Ectoplasm"), TEXT("Vexa") };
		return Ids.Contains(ReaperId);
	}

	FEntranceSpec Entrance(FName Id)
	{
		// Matches the SQ_Intro_<Id> takes (blender_work/sq_anims5.py): Drop lands on the take's landing frame (frame / 30 s)
		FEntranceSpec E;
		const FString S = Id.ToString();
		if (S == TEXT("Bonesmith") || S == TEXT("Mort") || S == TEXT("Monk") || S == TEXT("Keeper") || S == TEXT("Pallbearers") || S == TEXT("Grim"))
		{
			E.Mode = EEntrance::Rise;
			E.MoveFor = S == TEXT("Pallbearers") ? 0.75f : (S == TEXT("Grim") ? 0.8f : 0.9f);
		}
		else if (S == TEXT("Beak") || S == TEXT("Tickwell") || S == TEXT("Collector") || S == TEXT("Intern") || S == TEXT("Sweep") || S == TEXT("Solace"))
		{
			E.Mode = EEntrance::Poof;
			E.HideFor = S == TEXT("Sweep") || S == TEXT("Collector") ? 0.45f : 0.25f;
		}
		else if (S == TEXT("Femur") || S == TEXT("Lookout") || S == TEXT("Vexa") || S == TEXT("Actuary") || S == TEXT("Neckless") || S == TEXT("Ectoplasm"))
		{
			E.Mode = EEntrance::Drop;
			E.MoveFor = S == TEXT("Actuary") ? 0.6f : (S == TEXT("Ectoplasm") ? 0.75f : (S == TEXT("Femur") ? 0.3f : 0.33f));
			E.DropHeight = S == TEXT("Ectoplasm") || S == TEXT("Actuary") ? 260.f : 380.f;
		}
		else if (S == TEXT("Widow"))
		{
			E.Mode = EEntrance::Materialize;
			E.MoveFor = 0.8f;
		}
		else if (S == TEXT("Tallow"))
		{
			E.Mode = EEntrance::Grow;
			E.MoveFor = 0.75f;
		}
		else if (S == TEXT("Digby"))
		{
			E.Mode = EEntrance::Jolt;
			E.MoveFor = 0.45f;   // the strike lands at the take's jolt (frame 14)
		}
		return E;
	}

	void PlayEntrance(UWorld* W, FName Id, const FVector& Feet, const FVector& FwdIn, float K)
	{
		if (!W) return;
		const FPal Pal = PalFor(Id);
		const FVector Fwd = Flat(FwdIn);
		const float Face = YawOf(Fwd);
		const FVector Up(0.0, 0.0, 1.0);
		const FVector Chest = Feet + Up * (110.f * K);
		const FVector Head = Feet + Up * (175.f * K);
		const FString S = Id.ToString();
		const FEntranceSpec E = Entrance(Id);
		const float Land = E.Mode == EEntrance::Drop ? E.MoveFor : (E.Mode == EEntrance::Poof ? E.HideFor : 0.f);

		if (S == TEXT("Bonesmith"))
		{
			// claws out of the grave: earth bursts, rocks, dust; then the war bellow - a gold shield flash and the red eye
			SQFx::DirtClods(W, Feet, FMath::RoundToInt(14 * K), 420.f * K, 14.f * K);
			Spray(W, Shape::Shard, Feet + Up * 10.0, Fwd, 180.f, 10, 380.f * K, 34.f * K, 0.7f, Pal.Dark, -1400.f, 0.f, 0.6f, static_cast<float>(Feet.Z));
			Cloud(W, Feet, 90.f * K, 8, 70.f * K, 1.0f, Pal.Dark * 0.8f, 0.1f);
			Wave(W, Feet, Pal.Mid, 30.f * K, 200.f * K, 0.5f, 1.0f);
			Pop(W, Shape::Star, Head + Fwd * 12.0, Pal.Alt, 50.f * K, 0.35f, 1.0f, 0.f, 6.f);
			Spray(W, Shape::Star, Chest, Fwd, 70.f, 8, 300.f * K, 18.f * K, 0.5f, Pal.Core, -300.f, 1.02f, 0.4f);
		}
		else if (S == TEXT("Mort"))
		{
			SQFx::DirtClods(W, Feet, FMath::RoundToInt(10 * K), 360.f * K, 12.f * K);
			Cloud(W, Feet, 80.f * K, 6, 60.f * K, 0.9f, Pal.Dark, 0.05f);
			// lost souls spill from the satchel and he scoops them back
			Swirl(W, Shape::Star, Chest, 90.f * K, 8, 260.f * K, 40.f, 22.f * K, 0.9f, Pal.Mid, 0.95f, 180.f);
		}
		else if (S == TEXT("Monk"))
		{
			Swirl(W, Shape::Spiral, Feet, 40.f * K, 3, 120.f, 220.f, 140.f * K, 0.9f, Pal.Mid, 0.f, 0.f, 20.f);
			Cloud(W, Feet, 110.f * K, 10, 80.f * K, 1.1f, Pal.Mid * 0.85f, 0.f, 30.f);
			Spray(W, Shape::Bandage, Chest, Fwd, 180.f, 6, 360.f * K, 70.f * K, 0.6f, Pal.Core, 0.f, 0.9f, 0.2f);
			Pop(W, Shape::Star, Head + Fwd * 15.0, Pal.Alt, 40.f * K, 0.4f, 1.0f);
		}
		else if (S == TEXT("Keeper"))
		{
			Spray(W, Shape::Drop, Feet + Up * 10.0, Fwd, 180.f, 12, 300.f * K, 30.f * K, 0.7f, Pal.Alt, -900.f, 0.f, 1.0f, static_cast<float>(Feet.Z));
			Cloud(W, Feet, 120.f * K, 10, 90.f * K, 1.4f, Pal.Dark * 1.4f, 0.1f, 25.f);
			Swirl(W, Shape::Star, Head + Up * 40.0, 70.f * K, 6, 150.f, 60.f, 18.f * K, 1.2f, Pal.Mid, 1.0f, 0.f, 0.f);
			SQFx::Flash(W, Head + Up * 60.0, Pal.Core, 140.f * K, 0.2f, 1.0f);
		}
		else if (S == TEXT("Pallbearers"))
		{
			Cloud(W, Feet, 90.f * K, 8, 70.f * K, 1.2f, Pal.Dark * 3.f, 0.f, 20.f);
			Pop(W, Shape::Seal, Feet + Up * 4.0, Pal.Alt, 200.f * K, 0.8f, 0.05f, Face);
			Spray(W, Shape::Star, Head, Fwd, 50.f, 6, 220.f * K, 16.f * K, 0.6f, Pal.Mid, -200.f, 1.15f, 0.3f);   // the hat-tip glint
		}
		else if (S == TEXT("Grim"))
		{
			Cloud(W, Feet, 110.f * K, 12, 90.f * K, 1.3f, Pal.Dark, 0.f, 10.f);
			Wave(W, Feet, Pal.Alt, 160.f * K, 30.f * K, 0.7f, 0.f);                                     // the shadow pool closing
			for (int32 i = 0; i < 2; ++i)
			{
				Pop(W, Shape::Star, Head + FVector(0.0, (i == 0 ? 10.0 : -10.0), 0.0) + Fwd * 18.0, Pal.Mid, 30.f * K, 0.35f, 1.0f, 0.f, 4.f);
			}
			Slash(W, Pal, Chest + Fwd * (60.f * K), Face, 260.f * K, 1.15f, 0.4f);
		}
		else if (S == TEXT("Beak"))
		{
			// POOF: a purple cloud, a splash of green brew, sparkles, and the cackle
			Cloud(W, Chest, 70.f * K, 14, 110.f * K, 0.9f, Pal.Mid, 0.f, 40.f);
			Cloud(W, Feet, 60.f * K, 6, 70.f * K, 0.8f, Pal.Dark, 0.05f, 30.f);
			Spray(W, Shape::Drop, Chest, Fwd, 180.f, 10, 420.f * K, 26.f * K, 0.7f, Pal.Alt, -1100.f, Land, 0.6f, static_cast<float>(Feet.Z));
			Spray(W, Shape::Star, Chest, Fwd, 180.f, 10, 360.f * K, 20.f * K, 0.8f, Pal.Core, -100.f, Land + 0.05f, 0.5f);
			SQFx::Flash(W, Chest, Pal.Core, 160.f * K, 0.12f, Land - 0.02f);
		}
		else if (S == TEXT("Tickwell"))
		{
			Pop(W, Shape::Rune, Feet + Up * 6.0, Pal.Mid, 300.f * K, 1.1f, 0.f, 0.f, 3.f);
			Pop(W, Shape::Rune, Feet + Up * 8.0, Pal.Alt, 200.f * K, 0.9f, 0.08f, 30.f, -5.f);
			Swirl(W, Shape::Spiral, Feet, 30.f * K, 1, 0.f, 120.f, 220.f * K, 0.7f, Pal.Mid, 0.f, 0.f, 80.f);
			Spray(W, Shape::Star, Chest, Fwd, 180.f, 8, 260.f * K, 22.f * K, 0.7f, Pal.Core, 0.f, Land, 0.3f);
			Wave(W, Feet, Pal.Mid, 20.f * K, 240.f * K, 0.45f, 0.66f);                                  // the staff slam's tick ring
			Pop(W, Shape::Rune, Feet + Up * 6.0, Pal.Core, 240.f * K, 0.5f, 0.66f, 0.f, 8.f);
		}
		else if (S == TEXT("Collector"))
		{
			// a swarm of bats folds into him, then the cape snaps open in a red flash with gold coins
			Swirl(W, Shape::Bat, Chest, 260.f * K, 18, 260.f * K, 0.f, 40.f * K, 0.5f, Pal.Dark, 0.f, 380.f);
			Cloud(W, Chest, 50.f * K, 8, 80.f * K, 0.6f, Pal.Mid * 0.7f, Land - 0.05f, 30.f);
			SQFx::Flash(W, Chest, Pal.Mid, 180.f * K, 0.14f, Land + 0.35f);
			Spray(W, Shape::Seed, Chest, Fwd, 180.f, 10, 380.f * K, 18.f * K, 0.7f, Pal.Alt, -1200.f, Land + 0.35f, 0.6f, static_cast<float>(Feet.Z));
		}
		else if (S == TEXT("Intern"))
		{
			// a firework goes off and he tumbles out of it
			SQFx::Flash(W, Chest, Pal.Core, 180.f * K, 0.12f, Land - 0.02f);
			const FLinearColor Party[4] = { Pal.Mid, Pal.Alt, Hx(0x5CF2D6), Hx(0xB98CFF) };
			for (int32 i = 0; i < 4; ++i)
			{
				Spray(W, Shape::Star, Chest, Fwd, 180.f, 6, 520.f * K, 22.f * K, 0.8f, Party[i], -500.f, Land, 0.8f);
			}
			Cloud(W, Chest, 50.f * K, 8, 70.f * K, 0.7f, Pal.Dark * 2.f, Land, 30.f);
			Spray(W, Shape::Star, Head + Up * 40.0, Fwd, 180.f, 12, 300.f * K, 16.f * K, 0.6f, Pal.Alt, -300.f, 0.8f, 1.0f);   // TA-DA sparkle
		}
		else if (S == TEXT("Sweep"))
		{
			// a murder of crows swoops in and he flops together out of them; straw everywhere
			Swirl(W, Shape::Crow, Chest, 280.f * K, 14, 300.f * K, 0.f, 46.f * K, 0.5f, Pal.Dark, 0.f, 420.f);
			Spray(W, Shape::Feather, Chest, Fwd, 180.f, 16, 340.f * K, 34.f * K, 1.0f, Pal.Mid, -350.f, Land, 0.5f, static_cast<float>(Feet.Z));
			Spray(W, Shape::Crow, Head, Fwd, 120.f, 5, 380.f * K, 44.f * K, 0.8f, Pal.Dark, 0.f, Land + 0.1f, 0.7f);
		}
		else if (S == TEXT("Solace"))
		{
			Strike(W, Pal, Feet, 700.f * K, Land - 0.05f);
			Spray(W, Shape::Bolt, Chest, Fwd, 180.f, 6, 240.f * K, 50.f * K, 0.3f, Pal.Alt, 0.f, Land + 0.35f, 0.2f);
			Swirl(W, Shape::Star, Head + Up * 30.0, 60.f * K, 6, 200.f, 20.f, 16.f * K, 0.7f, Pal.Core, 0.6f);   // "IT'S ALIVE!"
		}
		else if (S == TEXT("Femur"))
		{
			Pop(W, Shape::Claw, Feet + Up * 4.0 + Fwd * 40.0, Pal.Dark, 180.f * K, 1.4f, Land, Face);
			Cloud(W, Feet, 90.f * K, 8, 70.f * K, 0.9f, Pal.Dark * 1.6f, Land, 30.f);
			Wave(W, Feet, Pal.Mid, 20.f * K, 160.f * K, 0.4f, Land);
			// the howl: a pale moon halo above, rings rolling off the muzzle
			Pop(W, Shape::Ring, Head + Up * 90.0, Pal.Alt, 90.f * K, 1.0f, 0.73f, 0.f, 0.5f);
			Pop(W, Shape::Star, Head + Up * 90.0, Pal.Core, 60.f * K, 1.0f, 0.73f);
			for (int32 i = 0; i < 3; ++i)
			{
				Wave(W, Head + Up * 10.0, Pal.Mid, 20.f * K, 140.f * K, 0.5f, 0.75f + 0.12f * static_cast<float>(i), 0.f);
			}
		}
		else if (S == TEXT("Lookout"))
		{
			Spray(W, Shape::Shard, Feet + Up * 10.0, Fwd, 180.f, 12, 420.f * K, 30.f * K, 0.7f, Pal.Dark, -1400.f, Land, 0.6f, static_cast<float>(Feet.Z));
			Cloud(W, Feet, 100.f * K, 8, 80.f * K, 0.9f, Pal.Alt * 0.7f, Land, 25.f);
			Wave(W, Feet, Pal.Alt, 20.f * K, 220.f * K, 0.45f, Land);
			SQFx::Flash(W, Head + Fwd * 20.0, Pal.Mid, 120.f * K, 0.2f, 0.75f);                            // the eye ignites
			Pop(W, Shape::Star, Head + Fwd * 20.0, Pal.Core, 70.f * K, 0.5f, 0.75f, 0.f, 3.f);
		}
		else if (S == TEXT("Vexa"))
		{
			Wave(W, Feet, Pal.Dark, 20.f * K, 260.f * K, 0.5f, Land);
			SQFx::GroundCracks(W, Feet, 170.f * K, Pal.Mid, 7, 1.2f, 8.f, Land);
			Cloud(W, Feet, 90.f * K, 8, 70.f * K, 0.8f, Hx(0xC8C0B8), Land, 40.f);
			// the cleavers clang overhead: steel sparks
			Spray(W, Shape::Star, Head + Up * 40.0, Fwd, 180.f, 14, 380.f * K, 18.f * K, 0.5f, Pal.Alt, -900.f, 0.8f, 0.6f);
			SQFx::Flash(W, Head + Up * 40.0, Pal.Core, 90.f * K, 0.1f, 0.8f);
		}
		else if (S == TEXT("Actuary"))
		{
			Swirl(W, Shape::Moth, Chest, 120.f * K, 6, 160.f, -40.f, 34.f * K, 0.8f, Pal.Mid, 0.f, 60.f);
			Rain(W, Shape::Star, Feet, 80.f * K, 10, 200.f * K, 12.f * K, 0.8f, Pal.Core, 0.f, 0.5f);
			Pop(W, Shape::Seal, Feet + Up * 4.0 + Fwd * 30.0, Pal.Alt, 150.f * K, 1.2f, 1.0f, Face);          // PAID
			Wave(W, Feet + Fwd * 30.0, Pal.Alt, 20.f * K, 120.f * K, 0.35f, 1.0f);
		}
		else if (S == TEXT("Neckless"))
		{
			// vaults off a phantom steed: hoofbeat rings, green fire bursts on landing, the sabre salute flares
			for (int32 i = 0; i < 3; ++i)
			{
				Wave(W, Feet - Fwd * (120.0 - 40.0 * i), Pal.Mid, 10.f * K, 70.f * K, 0.3f, 0.08f * static_cast<float>(i));
			}
			Crown(W, Shape::Flame, Feet, 70.f * K, 8, 90.f * K, 0.7f, Pal.Mid, Land, 80.f);
			Crown(W, Shape::Flame, Feet, 40.f * K, 5, 70.f * K, 0.5f, Pal.Core, Land + 0.04f, 120.f);
			Spray(W, Shape::Star, Chest, Fwd, 180.f, 10, 300.f * K, 16.f * K, 0.9f, Pal.Mid, 200.f, Land, 0.8f);
			Slash(W, Pal, Head + Up * 30.0, Face + 90.f, 180.f * K, 0.8f, 0.35f);
			Pop(W, Shape::Flame, Head + Up * 60.0 + OnCircle(FVector::ZeroVector, 40.f * K, Face + 90.f), Pal.Alt, 70.f * K, 0.6f, 0.8f);   // the lantern head held high
		}
		else if (S == TEXT("Ectoplasm"))
		{
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector Hand = Chest + OnCircle(FVector::ZeroVector, 30.f * K, Face + 90.f + 180.f * static_cast<float>(i % 2), 40.f * static_cast<float>(i / 2));
				SQFx::Streak(W, Hand + Up * (500.f * K), Hand, Pal.Mid, 4.f, 1.0f, 0.f);
			}
			Rain(W, Shape::Star, Feet, 120.f * K, 12, 300.f * K, 14.f * K, 0.9f, Pal.Core, 0.f, 0.6f);
			Cloud(W, Feet, 80.f * K, 6, 70.f * K, 0.8f, Pal.Dark * 2.f, Land, 20.f);
			Wave(W, Feet, Pal.Mid, 20.f * K, 160.f * K, 0.4f, Land + 0.3f);
		}
		else if (S == TEXT("Widow"))
		{
			Swirl(W, Shape::Puff, Chest, 200.f * K, 12, 280.f * K, 60.f, 50.f * K, 0.8f, Pal.Mid, 0.f, 260.f);
			Pop(W, Shape::Spiral, Feet + Up * (100.f * K), Pal.Alt, 240.f * K, 0.8f, 0.f, 0.f, 8.f);
			for (int32 i = 0; i < 3; ++i)
			{
				Wave(W, Chest, Pal.Mid, 30.f * K, 260.f * K, 0.55f, 1.05f + 0.12f * static_cast<float>(i), 0.f);    // the WAIL
			}
			SQFx::Flash(W, Head, Pal.Core, 120.f * K, 0.15f, 1.05f);
		}
		else if (S == TEXT("Tallow"))
		{
			Spray(W, Shape::Leaf, Feet + Up * 20.0, Fwd, 180.f, 14, 300.f * K, 34.f * K, 1.0f, Pal.Alt, -300.f, 0.f, 0.7f, static_cast<float>(Feet.Z));
			SQFx::DirtClods(W, Feet, 8, 280.f * K, 10.f * K);
			// the head tips back and a gout of flame roars skyward
			for (int32 i = 0; i < 6; ++i)
			{
				Pop(W, Shape::Flame, Head + Up * (30.0 + 45.0 * i), i < 2 ? Pal.Core : Pal.Mid, (110.f - 10.f * static_cast<float>(i)) * K, 0.5f, 0.8f + 0.03f * static_cast<float>(i), 0.f, 0.f, FVector(0.0, 0.0, 260.0));
			}
			Cloud(W, Head + Up * 260.0, 40.f * K, 5, 60.f * K, 1.0f, Pal.Dark, 1.0f, 80.f);
		}
		else if (S == TEXT("Digby"))
		{
			// lifeless... then the strike hits the head brace and he jolts alive, sparks crawling over him
			Strike(W, Pal, Head + Up * 40.0, 800.f * K, E.MoveFor);
			for (int32 i = 0; i < 6; ++i)
			{
				Pop(W, Shape::Bolt, Chest + FVector(FMath::FRandRange(-60.f, 60.f) * K, FMath::FRandRange(-40.f, 40.f) * K, FMath::FRandRange(-60.f, 80.f) * K), Pal.Mid, 60.f * K, 0.15f,
					E.MoveFor + 0.05f + 0.06f * static_cast<float>(i), FMath::FRandRange(0.f, 360.f), 0.f, FVector::ZeroVector, FMath::FRandRange(-60.f, 60.f));
			}
			Pop(W, Shape::Star, Head + Fwd * 20.0, Pal.Core, 50.f * K, 0.4f, E.MoveFor + 0.05f);
		}
	}

	void PlayFlourish(UWorld* W, FName ReaperId, const FVector& Feet, const FVector& Fwd)
	{
		PlayEntrance(W, ReaperId, Feet, Fwd, 0.75f);
	}

	void PlayAttack(UWorld* W, FName Id, const FVector& From, const FVector& DirIn, float Range, bool bCrit, bool bEvolved, float ImpactDelay)
	{
		if (!W) return;
		const FPal Pal = PalFor(Id);
		// Nov 19 2026: the data-driven recipe (FxRecipes.csv, <Id>/Attack) replaces the built-in accent below when it exists
		{
			SQFxRecipe::FContext Ctx;
			Ctx.Origin = From;
			Ctx.Dir = Flat(DirIn);
			Ctx.Radius = FMath::Clamp(Range, 120.f, 900.f);
			Ctx.Scale = (bEvolved ? 1.3f : 1.f) * (bCrit ? 1.15f : 1.f);
			Ctx.Beat = FMath::Max(0.f, ImpactDelay);
			Ctx.Core = Pal.Core; Ctx.Mid = Pal.Mid; Ctx.Dark = Pal.Dark; Ctx.Alt = Pal.Alt;
			if (SQFxRecipe::Play(W, Id, FName(TEXT("Attack")), Ctx)) return;
		}
		const FBeatScope Beat(ImpactDelay);
		const FVector Dir = Flat(DirIn);
		const float Yaw = YawOf(Dir);
		const float K = (bEvolved ? 1.3f : 1.f) * (bCrit ? 1.15f : 1.f);
		const FVector Up(0.0, 0.0, 1.0);
		const float R = FMath::Clamp(Range, 120.f, 900.f);
		const FVector Hit = From + Dir * (R * 0.55f) + Up * 40.0;
		const FString S = Id.ToString();

		if (S == TEXT("Neckless"))
		{
			Trail(W, Shape::Flame, From + Up * 40.0, From + Dir * R + Up * 30.0, 6, 70.f * K, 0.4f, Pal.Mid, 0.f, 0.03f);
			Pop(W, Shape::Seed, From + Dir * 40.0 + Up * 110.0, Pal.Alt, 80.f * K, 0.3f, 0.f, Yaw);
		}
		else if (S == TEXT("Tallow"))
		{
			Spray(W, Shape::Seed, From + Up * 150.0 + Dir * 30.0, Dir, 18.f, 3, 900.f, 40.f * K, 0.35f, Pal.Mid, 0.f, 0.f, 0.f);
			Cloud(W, From + Up * 160.0 + Dir * 40.0, 20.f, 2, 40.f * K, 0.5f, Pal.Dark, 0.02f, 90.f);
		}
		else if (S == TEXT("Bonesmith"))
		{
			Spray(W, Shape::Shard, From + Dir * 60.0, Dir, 40.f, 6, 300.f, 50.f * K, 0.6f, Pal.Mid, -800.f, 0.f, 0.9f, static_cast<float>(From.Z));
			Slash(W, Pal, From + Dir * 60.0 + Up * 60.0, Yaw, 200.f * K);
		}
		else if (S == TEXT("Pallbearers"))
		{
			Pop(W, Shape::Seal, From + Dir * 80.0 + Up * 4.0, Pal.Alt, 150.f * K, 0.5f, 0.f, Yaw);
			Cloud(W, From + Dir * 80.0, 50.f, 5, 60.f * K, 0.8f, Pal.Dark * 3.f, 0.f, 30.f);
		}
		else if (S == TEXT("Widow"))
		{
			Wave(W, From, Pal.Mid, 40.f, R, 0.45f, 0.f, 50.f);
			Wave(W, From, Pal.Alt, 30.f, R * 0.7f, 0.4f, 0.1f, 70.f);
		}
		else if (S == TEXT("Beak"))
		{
			Cloud(W, From, R * 0.5f, 4, 70.f * K, 1.1f, Pal.Alt * 0.6f, 0.f, 20.f);
			Spray(W, Shape::Drop, From + Up * 30.0, Dir, 180.f, 3, 200.f, 18.f * K, 0.6f, Pal.Alt, -500.f, 0.f, 0.8f, static_cast<float>(From.Z));
		}
		else if (S == TEXT("Tickwell"))
		{
			Pop(W, Shape::Rune, From + Up * 6.0, Pal.Mid, R * 2.f, 0.5f, 0.f, 0.f, 4.f);
			Wave(W, From, Pal.Alt, R * 0.3f, R, 0.4f, 0.05f);
		}
		else if (S == TEXT("Grim"))
		{
			Slash(W, Pal, From + Up * 50.0 + Dir * (R * 0.35f), Yaw, R * 1.1f * K, 0.f, 0.32f);
		}
		else if (S == TEXT("Collector"))
		{
			Spray(W, Shape::Seed, From + Up * 110.0, Dir, 20.f, 3, 700.f, 26.f * K, 0.4f, Pal.Alt, 0.f, 0.f, 0.f);
			Spray(W, Shape::Drop, Hit, Dir, 60.f, 3, 200.f, 16.f * K, 0.5f, Pal.Mid, -800.f, 0.08f, 0.6f, static_cast<float>(From.Z));
		}
		else if (S == TEXT("Monk"))
		{
			Swirl(W, Shape::Bandage, From, R * 0.6f, 4, 520.f, 0.f, 80.f * K, 0.4f, Pal.Mid, 0.f, 0.f, 60.f);
		}
		else if (S == TEXT("Femur"))
		{
			Pop(W, Shape::Claw, From + Dir * (R * 0.4f) + Up * 4.0, Pal.Dark, R * 1.1f, 0.9f, 0.f, Yaw);
			Pop(W, Shape::Claw, From + Dir * (R * 0.4f) + Up * 8.0, Pal.Mid, R * 0.9f, 0.4f, 0.02f, Yaw);
			Cloud(W, From + Dir * (R * 0.4f), 60.f, 5, 60.f * K, 0.7f, Pal.Dark * 1.5f, 0.f, 40.f);
		}
		else if (S == TEXT("Digby"))
		{
			Slash(W, Pal, From + Dir * 70.0 + Up * 50.0, Yaw, R * 1.2f * K);
			Spray(W, Shape::Bolt, Hit, Dir, 70.f, 3, 160.f, 50.f * K, 0.25f, Pal.Mid, 0.f, 0.05f, 0.f);
		}
		else if (S == TEXT("Lookout"))
		{
			Trail(W, Shape::Star, From + Up * 160.0, From + Dir * R + Up * 40.0, 5, 30.f * K, 0.3f, Pal.Core, 0.f, 0.015f, 4.f);
			Spray(W, Shape::Shard, From + Dir * R, Dir, 60.f, 4, 240.f, 26.f * K, 0.5f, Pal.Alt, -900.f, 0.05f, 0.6f, static_cast<float>(From.Z));
		}
		else if (S == TEXT("Keeper"))
		{
			Wave(W, From, Pal.Mid, 40.f, R, 0.5f, 0.f, 20.f);
			Swirl(W, Shape::Star, From, R * 0.6f, 5, 120.f, 80.f, 16.f * K, 0.7f, Pal.Core, 0.05f);
		}
		else if (S == TEXT("Intern"))
		{
			const FLinearColor Party[3] = { Pal.Mid, Pal.Alt, Hx(0x5CF2D6) };
			Spray(W, Shape::Star, Hit, Dir, 180.f, 5, 260.f, 18.f * K, 0.4f, Party[FMath::RandRange(0, 2)], -400.f, 0.f, 0.6f);
		}
		else if (S == TEXT("Sweep"))
		{
			Spray(W, Shape::Crow, From + Up * 120.0, Dir, 30.f, 2, 700.f, 50.f * K, 0.45f, Pal.Dark, 0.f, 0.f, 0.f);
			Spray(W, Shape::Feather, From + Up * 120.0, Dir, 180.f, 3, 160.f, 22.f * K, 0.7f, Pal.Mid, -250.f, 0.f, 0.3f);
		}
		else if (S == TEXT("Solace"))
		{
			for (int32 i = -2; i <= 2; ++i)
			{
				Pop(W, Shape::Bolt, From + OnCircle(FVector::ZeroVector, R * 0.5f, Yaw + 25.f * static_cast<float>(i)) + Up * 50.0, (i % 2) ? Pal.Mid : Pal.Core, R * 0.45f, 0.2f,
					0.02f * static_cast<float>(i + 2), Yaw + 25.f * static_cast<float>(i));
			}
			Wave(W, From, Pal.Mid, 40.f, R, 0.3f);
		}
		else if (S == TEXT("Mort"))
		{
			Pop(W, Shape::Shard, From + Dir * 60.0 + Up * 80.0, Pal.Alt, 60.f * K, 0.3f, 0.f, Yaw, 20.f);
			Spray(W, Shape::Star, From + Dir * 50.0 + Up * 70.0, Dir, 40.f, 3, 200.f, 14.f * K, 0.4f, Pal.Mid);
		}
		else if (S == TEXT("Actuary"))
		{
			Pop(W, Shape::Seal, Hit - Up * 36.0, Pal.Alt, 160.f * K, 0.7f, 0.f, Yaw);
			Spray(W, Shape::Star, Hit, Dir, 180.f, 5, 220.f, 14.f * K, 0.5f, Pal.Core, -300.f, 0.02f, 0.5f);
		}
		else if (S == TEXT("Ectoplasm"))
		{
			SQFx::Streak(W, From + Up * 200.0, Hit, Pal.Mid, 5.f, 0.35f, GBeat);
			Pop(W, Shape::Star, Hit, Pal.Core, 40.f * K, 0.35f, 0.04f, 0.f, 5.f);
		}
		else if (S == TEXT("Vexa"))
		{
			Swirl(W, Shape::Cleaver, From, R * 0.55f, 4, 700.f, 0.f, 70.f * K, 0.35f, Pal.Alt, 0.f, 0.f, 60.f);
			Pop(W, Shape::Crescent, From + Up * 60.0, Pal.Mid, R * 1.2f * K, 0.3f, 0.f, Yaw, 18.f);
		}
	}

	void PlayCast(UWorld* W, FName Id, bool bUlt, const FVector& From, const FVector& To, float Radius)
	{
		if (!W) return;
		const FPal Pal = PalFor(Id);
		const FVector Dir = Flat(To - From);
		const float Yaw = YawOf(Dir);
		const float Rad = FMath::Clamp(Radius, 80.f, 1600.f);
		const float K = bUlt ? 1.4f : 1.f;
		const FVector Up(0.0, 0.0, 1.0);
		const FString S = Id.ToString();
		const float Gnd = static_cast<float>(To.Z);

		// Nov 19 2026: the data-driven recipe (FxRecipes.csv, <Id>/E or R) replaces the built-in cast below when it exists
		{
			SQFxRecipe::FContext Ctx;
			Ctx.Origin = From;
			Ctx.Dir = Dir;
			Ctx.Target = To;
			Ctx.bHasTarget = true;
			Ctx.Radius = FMath::Clamp(Radius, 140.f, 900.f);
			Ctx.Core = Pal.Core; Ctx.Mid = Pal.Mid; Ctx.Dark = Pal.Dark; Ctx.Alt = Pal.Alt;
			if (SQFxRecipe::Play(W, Id, bUlt ? FName(TEXT("R")) : FName(TEXT("E")), Ctx)) return;
		}

		// anticipation for every cast: a glint and a small ring at the caster's feet
		Pop(W, Shape::Star, From + Up * 120.0, Pal.Core, 40.f * K, 0.2f, 0.f, 0.f, 6.f);
		Wave(W, From, Pal.Dark, 20.f, 90.f * K, 0.25f);

		if (S == TEXT("Neckless"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Flame, From, To, 10, 80.f, 0.6f, Pal.Mid, 0.f, 0.02f);
				Trail(W, Shape::Seed, From + Up * 60.0, To + Up * 60.0, 5, 70.f, 0.3f, Pal.Alt, 0.f, 0.04f, 10.f);
				Crown(W, Shape::Flame, To, 80.f, 6, 90.f, 0.6f, Pal.Mid, 0.15f);
			}
			else
			{
				Crown(W, Shape::Flame, From, Rad * 0.6f, 12, 120.f, 0.9f, Pal.Mid, 0.f, 90.f);
				Crown(W, Shape::Flame, From, Rad * 0.35f, 8, 90.f, 0.7f, Pal.Core, 0.08f, 140.f);
				for (int32 i = 0; i < 4; ++i)
				{
					Wave(W, From, Pal.Mid, 20.f, Rad * 0.8f, 0.4f, 0.12f * static_cast<float>(i));
				}
			}
		}
		else if (S == TEXT("Tallow"))
		{
			if (!bUlt)
			{
				Crown(W, Shape::Flame, To, Rad * 0.5f, 10, 100.f, 0.8f, Pal.Mid, 0.05f);
				Spray(W, Shape::Seed, To + Up * 30.0, Dir, 180.f, 10, 500.f, 34.f, 0.6f, Pal.Alt, -900.f, 0.05f, 0.6f, Gnd);
				Cloud(W, To, Rad * 0.4f, 6, 90.f, 1.0f, Pal.Dark, 0.1f);
			}
			else
			{
				const FVector End = From + Dir * Rad;
				Trail(W, Shape::Flame, From + Up * 150.0, End + Up * 40.0, 16, 130.f, 0.9f, Pal.Mid, 0.f, 0.015f);
				Trail(W, Shape::Flame, From + Up * 150.0, End + Up * 40.0, 10, 80.f, 0.7f, Pal.Core, 0.05f, 0.02f);
				Trail(W, Shape::Star, From + Up * 160.0, End + Up * 50.0, 12, 40.f, 0.5f, Pal.Core, 0.1f, 0.03f, 6.f);
			}
		}
		else if (S == TEXT("Bonesmith"))
		{
			if (!bUlt)
			{
				Wave(W, To, Pal.Alt, 30.f, Rad, 0.45f);
				Wave(W, To, Pal.Mid, 20.f, Rad * 0.8f, 0.4f, 0.08f);
				Spray(W, Shape::Shard, To, Dir, 180.f, 12, 500.f, 50.f, 0.7f, Pal.Dark, -1400.f, 0.f, 0.7f, Gnd);
			}
			else
			{
				Pop(W, Shape::Spiral, To + Up * 120.0, Pal.Mid, 240.f, 1.2f, 0.f, 0.f, 6.f);
				Spray(W, Shape::Shard, To, Dir, 180.f, 16, 500.f, 60.f, 0.8f, Pal.Mid, -1400.f, 0.f, 0.8f, Gnd);
				Crown(W, Shape::Shard, To, 140.f, 8, 110.f, 1.2f, Pal.Dark, 0.1f, 0.f);
			}
		}
		else if (S == TEXT("Pallbearers"))
		{
			Pop(W, Shape::Seal, To + Up * 4.0, Pal.Alt, Rad * 1.6f, 0.8f, 0.f, Yaw);
			Cloud(W, To, Rad * 0.5f, bUlt ? 14 : 8, 90.f * K, 1.2f, Pal.Dark * 3.f, 0.05f, 30.f);
			Spray(W, Shape::Star, To + Up * 30.0, Dir, 180.f, 8, 300.f, 20.f, 0.6f, Pal.Mid, -300.f, 0.05f, 0.5f);
			if (bUlt) Swirl(W, Shape::Leaf, From, Rad * 0.6f, 12, 300.f, 30.f, 50.f, 1.2f, Pal.Mid, 0.f);   // funeral lilies
		}
		else if (S == TEXT("Widow"))
		{
			if (!bUlt)
			{
				for (int32 i = 0; i < 4; ++i)
				{
					Pop(W, Shape::Crescent, From + Dir * (120.0 + 110.0 * i) + Up * 60.0, i % 2 ? Pal.Alt : Pal.Mid, 200.f + 80.f * static_cast<float>(i), 0.35f, 0.04f * static_cast<float>(i), Yaw);
				}
				Spray(W, Shape::Puff, From + Up * 80.0, Dir, 30.f, 8, 600.f, 50.f, 0.6f, Pal.Mid, 0.f, 0.f, 0.f);
			}
			else
			{
				Pop(W, Shape::Spiral, To + Up * 40.0, Pal.Mid, Rad * 1.6f, 1.6f, 0.f, 0.f, -5.f);
				Swirl(W, Shape::Puff, To, Rad * 0.9f, 16, 400.f, 0.f, 70.f, 1.4f, Pal.Alt, 0.f, 220.f);
				Wave(W, To, Pal.Core, Rad, 40.f, 1.2f);
			}
		}
		else if (S == TEXT("Beak"))
		{
			if (!bUlt)
			{
				Spray(W, Shape::Drop, To + Up * 20.0, Dir, 180.f, 14, 380.f, 30.f, 0.8f, Pal.Alt, -1000.f, 0.f, 0.9f, Gnd);
				Cloud(W, To, Rad * 0.6f, 10, 100.f, 1.4f, Pal.Mid, 0.05f, 20.f);
				Swirl(W, Shape::Sphere, To, Rad * 0.5f, 8, 0.f, 120.f, 18.f, 1.0f, Pal.Alt, 0.1f);
			}
			else
			{
				Rain(W, Shape::Star, From, Rad * 0.6f, 20, 400.f, 30.f, 0.9f, Pal.Alt, 0.f, 0.8f);
				Swirl(W, Shape::Feather, From, Rad * 0.5f, 10, 600.f, 0.f, 60.f, 0.8f, Pal.Mid, 0.f);
				Cloud(W, From, Rad * 0.4f, 10, 110.f, 1.2f, Pal.Mid, 0.f, 40.f);
			}
		}
		else if (S == TEXT("Tickwell"))
		{
			if (!bUlt)
			{
				Pop(W, Shape::Rune, From + Up * 6.0, Pal.Mid, 220.f, 0.6f, 0.f, 0.f, -8.f);
				Pop(W, Shape::Rune, To + Up * 6.0, Pal.Mid, 220.f, 0.6f, 0.1f, 0.f, 8.f);
				Trail(W, Shape::Star, From + Up * 80.0, To + Up * 80.0, 8, 30.f, 0.5f, Pal.Core, 0.f, 0.02f, 6.f);
				Pop(W, Shape::Spiral, To + Up * 100.0, Pal.Alt, 180.f, 0.6f, 0.1f, 0.f, -10.f);
			}
			else
			{
				Pop(W, Shape::Rune, From + Up * 8.0, Pal.Mid, Rad * 2.f, 2.0f, 0.f, 0.f, 0.6f);
				Pop(W, Shape::Rune, From + Up * 10.0, Pal.Alt, Rad * 1.3f, 2.0f, 0.1f, 15.f, -0.8f);
				Wave(W, From, Pal.Core, 40.f, Rad, 0.5f);
				Rain(W, Shape::Shard, From, Rad * 0.8f, 18, 30.f, 40.f, 2.0f, Pal.Alt, 0.2f, 0.4f);           // frozen splinters of time
			}
		}
		else if (S == TEXT("Grim"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Puff, From, To, 10, 70.f, 0.8f, Pal.Dark, 0.f, 0.02f);
				Slash(W, Pal, To + Up * 60.0, Yaw, 300.f, 0.12f, 0.35f);
			}
			else
			{
				for (int32 i = 0; i < 3; ++i)
				{
					Slash(W, Pal, From + Up * (60.0 + 20.0 * i), Yaw + 120.f * static_cast<float>(i), Rad * 1.4f, 0.4f + 0.1f * static_cast<float>(i), 0.45f);
				}
				Swirl(W, Shape::Puff, From, Rad * 0.7f, 14, 300.f, 0.f, 80.f, 0.9f, Pal.Dark, 0.f, 260.f);
			}
		}
		else if (S == TEXT("Collector"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Drop, From + Up * 60.0, From + Dir * Rad + Up * 60.0, 12, 30.f, 0.5f, Pal.Mid, 0.f, 0.015f);
				Spray(W, Shape::Bat, From + Up * 100.0, Dir, 25.f, 6, 900.f, 40.f, 0.6f, Pal.Dark, 0.f, 0.f, 0.f);
			}
			else
			{
				Pop(W, Shape::Seal, To + Up * 4.0, Pal.Mid, Rad * 2.f, 1.0f, 0.75f, Yaw);
				Swirl(W, Shape::Bat, To, Rad, 18, 500.f, 0.f, 50.f, 0.9f, Pal.Dark, 0.f, 300.f);
				Spray(W, Shape::Seed, To + Up * 40.0, Dir, 180.f, 14, 500.f, 22.f, 0.8f, Pal.Alt, -1200.f, 0.8f, 0.7f, Gnd);
			}
		}
		else if (S == TEXT("Monk"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Bandage, From + Up * 80.0, To + Up * 60.0, 6, 90.f, 0.4f, Pal.Core, 0.f, 0.02f);
				Cloud(W, To, Rad * 0.5f, 8, 80.f, 1.0f, Pal.Mid, 0.15f, 30.f);
			}
			else
			{
				Pop(W, Shape::Spiral, To + Up * 100.0, Pal.Mid, Rad * 1.2f, 3.0f, 0.f, 0.f, 7.f);
				Swirl(W, Shape::Puff, To, Rad * 0.7f, 16, 500.f, 60.f, 70.f, 2.0f, Pal.Mid, 0.f);
				Rain(W, Shape::Shard, To, Rad * 0.8f, 18, 500.f, 50.f, 0.6f, Pal.Alt, 0.f, 2.6f);
			}
		}
		else if (S == TEXT("Femur"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Puff, From, To, 6, 60.f, 0.7f, Pal.Dark * 1.5f, 0.f, 0.03f);
				Pop(W, Shape::Claw, To + Up * 4.0, Pal.Dark, Rad * 1.6f, 1.2f, 0.15f, Yaw);
				Pop(W, Shape::Claw, To + Up * 8.0, Pal.Mid, Rad * 1.3f, 0.5f, 0.17f, Yaw);
			}
			else
			{
				Pop(W, Shape::Ring, From + Up * 260.0, Pal.Alt, 120.f, 1.2f);
				Pop(W, Shape::Star, From + Up * 260.0, Pal.Core, 90.f, 1.2f);
				for (int32 i = 0; i < 4; ++i)
				{
					Wave(W, From, Pal.Mid, 40.f, Rad, 0.6f, 0.12f * static_cast<float>(i), 30.f);
				}
				Spray(W, Shape::Claw, From + Up * 20.0, Dir, 180.f, 6, 300.f, 120.f, 0.5f, Pal.Mid, 0.f, 0.1f, 0.f);
			}
		}
		else if (S == TEXT("Digby"))
		{
			if (!bUlt)
			{
				Spray(W, Shape::Shard, From + Up * 40.0, Dir, 32.f, 14, 900.f, 40.f, 0.6f, Pal.Dark * 2.f, -900.f, 0.f, 0.2f, Gnd);
				Spray(W, Shape::Puff, From + Up * 40.0, Dir, 32.f, 8, 600.f, 70.f, 0.8f, Hx(0x6A5A44), 0.f, 0.f, 0.f);
			}
			else
			{
				Strike(W, Pal, To, 900.f, 0.f);
				for (int32 i = 0; i < 6; ++i)
				{
					Strike(W, Pal, OnCircle(To, Rad * 0.6f, 60.f * static_cast<float>(i)), 600.f, 0.25f + 0.05f * static_cast<float>(i));
				}
			}
		}
		else if (S == TEXT("Lookout"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Shard, From + Up * 160.0, From + Dir * 1500.0 + Up * 60.0, 14, 40.f, 0.4f, Pal.Alt, 0.f, 0.01f);
				Trail(W, Shape::Star, From + Up * 160.0, From + Dir * 1500.0 + Up * 60.0, 10, 30.f, 0.35f, Pal.Core, 0.f, 0.012f, 6.f);
				Spray(W, Shape::Feather, From + Up * 120.0, Dir, 50.f, 6, 400.f, 40.f, 0.6f, Pal.Alt, 0.f, 0.f, 0.f);
			}
			else
			{
				Spray(W, Shape::Shard, To, Dir, 180.f, 14, 300.f, 50.f, 0.8f, Pal.Alt, -900.f, 0.f, 0.9f, Gnd);
				Pop(W, Shape::Star, To + Up * 180.0, Pal.Core, 120.f, 1.0f, 0.1f, 0.f, 4.f);
				Wave(W, To, Pal.Mid, 40.f, Rad * 0.5f, 0.5f, 0.1f);
			}
		}
		else if (S == TEXT("Keeper"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Star, From + Up * 120.0, To + Up * 60.0, 6, 40.f, 0.5f, Pal.Mid, 0.f, 0.03f, 6.f);
				Swirl(W, Shape::Star, To, Rad * 0.5f, 8, 200.f, 40.f, 22.f, 1.0f, Pal.Core, 0.15f);
				Cloud(W, To, Rad * 0.5f, 6, 90.f, 1.3f, Pal.Dark * 1.6f, 0.15f, 15.f);
			}
			else
			{
				for (int32 i = 0; i < 8; ++i)
				{
					Pop(W, Shape::Star, From + Up * (80.0 + 70.0 * i), i % 2 ? Pal.Mid : Pal.Core, 90.f - 6.f * static_cast<float>(i), 1.0f, 0.03f * static_cast<float>(i), 0.f, 3.f);
				}
				Wave(W, From, Pal.Mid, 40.f, Rad, 0.7f);
				Cloud(W, From, Rad * 0.6f, 14, 110.f, 1.6f, Pal.Alt * 0.6f, 0.1f, 15.f);
			}
		}
		else if (S == TEXT("Intern"))
		{
			const FLinearColor Party[4] = { Pal.Mid, Pal.Alt, Hx(0x5CF2D6), Hx(0xB98CFF) };
			if (!bUlt)
			{
				for (int32 i = 0; i < 5; ++i)
				{
					Spray(W, Shape::Star, From + Up * 80.0, OnCircle(FVector::ZeroVector, 1.f, Yaw - 25.f + 12.5f * static_cast<float>(i)), 4.f, 2, 900.f, 30.f, 0.5f, Party[i % 4], 0.f, 0.f, 0.f);
				}
			}
			else
			{
				for (int32 i = 0; i < 4; ++i)
				{
					Rain(W, Shape::Star, To, Rad * 0.7f, 8, 600.f, 40.f, 0.7f, Party[i], 2.0f + 0.1f * static_cast<float>(i), 0.6f);
				}
				Cloud(W, To, Rad * 0.6f, 10, 120.f, 1.0f, Pal.Dark * 2.f, 2.5f, 40.f);
				SQFx::Flash(W, To + Up * 60.0, Pal.Core, Rad * 0.6f, 0.15f, 2.5f);
			}
		}
		else if (S == TEXT("Sweep"))
		{
			if (!bUlt)
			{
				Spray(W, Shape::Feather, From + Up * 60.0, Dir, 180.f, 14, 400.f, 34.f, 0.9f, Pal.Mid, -300.f, 0.f, 0.5f, Gnd);
				Spray(W, Shape::Feather, To + Up * 60.0, Dir, 180.f, 14, 400.f, 34.f, 0.9f, Pal.Mid, -300.f, 0.12f, 0.5f, Gnd);
				Spray(W, Shape::Crow, To + Up * 80.0, Dir, 180.f, 6, 500.f, 50.f, 0.7f, Pal.Dark, 0.f, 0.12f, 0.5f);
			}
			else
			{
				for (int32 Wv = 0; Wv < 3; ++Wv)
				{
					Spray(W, Shape::Crow, From + Up * 100.0, Dir, 180.f, 10, 900.f, 56.f, 0.8f, Pal.Dark, 0.f, 0.5f * static_cast<float>(Wv), 0.1f);
				}
				Swirl(W, Shape::Feather, From, 120.f, 10, 300.f, 60.f, 40.f, 1.2f, Pal.Mid, 0.f);
			}
		}
		else if (S == TEXT("Solace"))
		{
			if (!bUlt)
			{
				Strike(W, Pal, To, 500.f, 0.f);
				Wave(W, To, Pal.Alt, 40.f, Rad, 0.4f, 0.05f);
			}
			else
			{
				for (int32 i = 0; i < 8; ++i)
				{
					Strike(W, Pal, OnCircle(From, Rad * FMath::FRandRange(0.3f, 0.9f), 45.f * static_cast<float>(i)), 700.f, 0.05f * static_cast<float>(i));
				}
				Swirl(W, Shape::Star, From, 120.f, 10, 200.f, 200.f, 30.f, 1.0f, Pal.Alt, 0.2f);
			}
		}
		else if (S == TEXT("Mort"))
		{
			if (!bUlt)
			{
				Spray(W, Shape::Shard, From + Up * 80.0, Dir, 22.f, 7, 1100.f, 50.f, 0.5f, Pal.Alt, 0.f, 0.f, 0.f);
				Spray(W, Shape::Star, From + Up * 80.0, Dir, 22.f, 6, 900.f, 18.f, 0.4f, Pal.Mid, 0.f, 0.f, 0.f);
			}
			else
			{
				Swirl(W, Shape::Feather, From, 160.f, 12, 900.f, 0.f, 70.f, 0.6f, Pal.Core, 0.f);       // speed lines
				Cloud(W, From, 80.f, 8, 80.f, 0.9f, Pal.Dark * 1.5f, 0.f, 20.f);
				Swirl(W, Shape::Star, From, 100.f, 8, 300.f, 60.f, 22.f, 0.8f, Pal.Mid, 0.1f);
			}
		}
		else if (S == TEXT("Actuary"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Star, From + Up * 120.0, From + Dir * 950.0 + Up * 60.0, 14, 24.f, 0.8f, Pal.Core, 0.f, 0.02f, 3.f);
				Spray(W, Shape::Moth, From + Up * 120.0, Dir, 35.f, 6, 700.f, 40.f, 0.8f, Pal.Mid, 0.f, 0.f, 0.f);
			}
			else
			{
				for (int32 i = 0; i < 12; ++i)
				{
					const FVector At = OnCircle(To, Rad * 0.8f * FMath::Sqrt(FMath::FRand()), FMath::FRandRange(0.f, 360.f));
					Pop(W, Shape::Seal, At + Up * 4.0, Pal.Alt, 220.f, 0.6f, 0.17f * static_cast<float>(i), FMath::FRandRange(-20.f, 20.f));
					Wave(W, At, Pal.Alt, 30.f, 150.f, 0.3f, 0.17f * static_cast<float>(i));
				}
			}
		}
		else if (S == TEXT("Ectoplasm"))
		{
			if (!bUlt)
			{
				SQFx::Streak(W, From + Up * 160.0, To + Up * 40.0, Pal.Mid, 6.f, 0.5f);
				Swirl(W, Shape::Star, To, Rad * 0.6f, 6, 200.f, 0.f, 24.f, 0.6f, Pal.Core, 0.05f);
			}
			else
			{
				Pop(W, Shape::Ring, To + Up * 6.0, Pal.Alt, Rad * 1.4f, 2.5f);
				Pop(W, Shape::Ring, To + Up * 8.0, Pal.Mid, Rad * 1.1f, 2.5f, 0.1f);
				for (int32 i = 0; i < 3; ++i)
				{
					const FVector Doll = OnCircle(To, Rad * 0.45f, 120.f * static_cast<float>(i));
					SQFx::Streak(W, Doll + Up * 600.0, Doll + Up * 80.0, Pal.Mid, 5.f, 2.5f, 0.1f);
					Pop(W, Shape::Star, Doll + Up * 80.0, Pal.Core, 60.f, 0.6f, 0.2f, 0.f, 4.f);
				}
			}
		}
		else if (S == TEXT("Vexa"))
		{
			if (!bUlt)
			{
				Trail(W, Shape::Crescent, From + Up * 60.0, To + Up * 60.0, 5, 180.f, 0.3f, Pal.Mid, 0.f, 0.03f);
				SQFx::Streak(W, From + Up * 80.0, To + Up * 80.0, Pal.Alt, 6.f, 0.3f);              // the hook chain
			}
			else
			{
				for (int32 i = 0; i < 3; ++i)
				{
					Swirl(W, Shape::Cleaver, From, Rad * (0.4f + 0.2f * static_cast<float>(i)), 6, 900.f, 0.f, 70.f, 0.6f, Pal.Alt, 0.15f * static_cast<float>(i), 0.f, 60.f);
				}
				Pop(W, Shape::Crescent, From + Up * 50.0, Pal.Mid, Rad * 2.f, 0.5f, 0.f, Yaw, 14.f);
				Wave(W, From, Pal.Dark, 40.f, Rad, 0.5f);
			}
		}
	}
}
