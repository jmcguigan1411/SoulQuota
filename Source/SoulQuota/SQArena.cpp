// Copyright 2026 Jack McGuigan. All rights reserved.
#include "SQArena.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "SQArt.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQCharacter.h"
#include "SQHazard.h"
#include "SQLantern.h"
#include "SQPot.h"
#include "SQShrine.h"

namespace
{
	const float TidePeriod = 45.f;
	const float TideWarnStart = 30.f;
	const float TideActiveStart = 33.f;
	const float TideHalfWidth = 600.f;
	const float SpotRadius = 650.f;
	const float StopPeriod = 30.f;
	const float StopWarnStart = 23.f;
	const float StopStart = 26.f;
	/** Spawner pads: footprint kept open round each pad (cm). */
	const float ArenaMapPadClear = 450.f;
	/** Biggest body radius the horde grid is padded for (cm). */
	const float ArenaMapHordePad = 220.f;

	/** KayKit props and characters share units: scale so a Rogue would be 170cm tall. */
	float ComputeArtScale()
	{
		// Props were exported in different units from the characters (FBX unit scale 1 vs 100),
		// so size the whole set from a reference prop: a gravestone ~110 cm tall.
		// (FitMeshHeight, not SQArt::FitScale: that returns 1 for meshes under 1 unit tall, i.e. 1 cm specks.)
		static const TCHAR* const Refs[] = { TEXT("gravestone"), TEXT("grave_A"), TEXT("coffin") };
		for (const TCHAR* const RefName : Refs)
		{
			if (const UStaticMesh* RefMesh = SQArt::Prop(FName(RefName))) return ASQArena::FitMeshHeight(RefMesh, 110.f);
		}
		return 1.f;
	}

	/** Transform that sits a mesh's bottom-centre on BaseCentre (works with tilt and non-uniform scale). */
	FTransform ArenaSitTransform(const UStaticMesh* InMesh, const FVector& BaseCentre, const FQuat& Rot, const FVector& Scale3)
	{
		const FBoxSphereBounds Bo = InMesh->GetBounds();
		const FVector LocalBottom(Bo.Origin.X, Bo.Origin.Y, Bo.Origin.Z - Bo.BoxExtent.Z);
		return FTransform(Rot, BaseCentre - Rot.RotateVector(LocalBottom * Scale3), Scale3);
	}

	/** First mesh in the list that exists in /Game/Art/Props. */
	UStaticMesh* ArenaFirstProp(const TArray<FName>& Names)
	{
		for (const FName& PropName : Names)
		{
			if (UStaticMesh* Found = SQArt::Prop(PropName)) return Found;
		}
		return nullptr;
	}

	/** Every mesh in the list that exists. */
	void ArenaLoadProps(const TArray<FName>& Names, TArray<UStaticMesh*>& Out)
	{
		for (const FName& PropName : Names)
		{
			if (UStaticMesh* Found = SQArt::Prop(PropName)) Out.Add(Found);
		}
	}

	/** Brightest channel -> 1 (light colours carry hue only; intensity comes from Tuning.csv). */
	FLinearColor ArenaHue(const FLinearColor& In)
	{
		const float MaxC = FMath::Max3(In.R, In.G, In.B);
		return MaxC > 0.001f ? FLinearColor(In.R / MaxC, In.G / MaxC, In.B / MaxC, 1.f) : FLinearColor::White;
	}

	/**
	 * Reads one Biomes.csv row (lower-case column -> value), or the first row when the id is missing (same rule as
	 * USQData::FindBiome). The extra palette / layout columns are read here so SQData's biome struct stays untouched.
	 */
	void ArenaReadBiomeRow(FName Biome, TMap<FString, FString>& OutCols)
	{
		OutCols.Reset();
		const FString CsvPath = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("Biomes.csv");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *CsvPath)) return;
		const FString Want = Biome.ToString();
		TArray<FString> Header;
		TArray<FString> Fields;
		FString Cell;
		bool bQuoted = false;
		bool bMatched = false;
		auto EndRow = [&]()
		{
			Fields.Add(Cell.TrimStartAndEnd());
			Cell.Reset();
			if (Fields.Num() > 0 && !Fields[0].IsEmpty() && !Fields[0].StartsWith(TEXT("#")))
			{
				if (Header.Num() == 0)
				{
					for (const FString& HeaderCell : Fields) Header.Add(HeaderCell.ToLower());
				}
				else if (!bMatched)
				{
					const bool bThis = Fields[0].Equals(Want, ESearchCase::IgnoreCase);
					if (bThis || OutCols.Num() == 0)
					{
						OutCols.Reset();
						for (int32 c = 0; c < Header.Num() && c < Fields.Num(); ++c) OutCols.Add(Header[c], Fields[c]);
						bMatched = bThis;
					}
				}
			}
			Fields.Reset();
		};
		for (int32 i = 0; i < Text.Len() && !bMatched; ++i)
		{
			const TCHAR Ch = Text[i];
			if (bQuoted)
			{
				if (Ch == TEXT('"'))
				{
					if (i + 1 < Text.Len() && Text[i + 1] == TEXT('"')) { Cell.AppendChar(Ch); ++i; }
					else { bQuoted = false; }
				}
				else { Cell.AppendChar(Ch); }
			}
			else if (Ch == TEXT('"')) { bQuoted = true; }
			else if (Ch == TEXT(',')) { Fields.Add(Cell.TrimStartAndEnd()); Cell.Reset(); }
			else if (Ch == TEXT('\n')) { EndRow(); }
			else if (Ch != TEXT('\r')) { Cell.AppendChar(Ch); }
		}
		if (!bMatched && (!Cell.IsEmpty() || Fields.Num() > 0)) EndRow();
	}

	FSQArenaPalette ArenaLoadPalette(FName Biome, const FSQBiomeDef* B)
	{
		FSQArenaPalette Pal;
		if (B)
		{
			Pal.Grass = B->GroundColor;
			Pal.Prop = B->PropColor;
			Pal.Fog = B->FogColor;
			// Derived defaults for biomes without the extra columns
			Pal.GrassAlt = B->GroundColor * 0.8f + FLinearColor(0.f, 0.03f, 0.04f, 0.f);
			Pal.Top = B->GroundColor * 1.5f + FLinearColor(0.02f, 0.03f, 0.f, 0.f);
			Pal.Basin = B->GroundColor * 0.55f + FLinearColor(0.03f, 0.f, 0.06f, 0.f);
		}
		TMap<FString, FString> Cols;
		ArenaReadBiomeRow(Biome, Cols);
		auto Col = [&Cols](const TCHAR* Key, FLinearColor& Inout)
		{
			const FString* Hex = Cols.Find(FString(Key).ToLower());
			if (Hex && !Hex->IsEmpty()) Inout = FLinearColor::FromSRGBColor(FColor::FromHex(*Hex));
			Inout.A = 1.f;
		};
		auto Names = [&Cols](const TCHAR* Key, TArray<FName>& Out)
		{
			const FString* Raw = Cols.Find(FString(Key).ToLower());
			if (!Raw) return;
			TArray<FString> Parts;
			Raw->ParseIntoArray(Parts, TEXT("|"), true);
			for (const FString& Part : Parts)
			{
				const FString Trimmed = Part.TrimStartAndEnd();
				if (!Trimmed.IsEmpty()) Out.Add(FName(*Trimmed));
			}
		};
		Col(TEXT("GrassAltColor"), Pal.GrassAlt);
		Col(TEXT("PathColor"), Pal.Path);
		Col(TEXT("CliffColor"), Pal.Cliff);
		Col(TEXT("TopColor"), Pal.Top);
		Col(TEXT("BasinColor"), Pal.Basin);
		Col(TEXT("WallColor"), Pal.Wall);
		Col(TEXT("MoonColor"), Pal.Moon);
		Col(TEXT("SkyColor"), Pal.Sky);
		Col(TEXT("LampColor"), Pal.Lamp);
		Col(TEXT("BackdropColor"), Pal.Backdrop);
		// Style pass (Nov 12 2026): sky-dome gradient (derived from the fog / sky colours when the columns are empty)
		Pal.SkyHorizon = FMath::Lerp(ArenaHue(Pal.Fog), ArenaHue(Pal.Lamp), 0.25f) * 0.40f;
		Pal.SkyMid = ArenaHue(Pal.Fog) * 0.20f;
		Pal.SkyZenith = ArenaHue(Pal.Sky) * 0.05f;
		Col(TEXT("SkyHorizonColor"), Pal.SkyHorizon);
		Col(TEXT("SkyMidColor"), Pal.SkyMid);
		Col(TEXT("SkyZenithColor"), Pal.SkyZenith);
		// Top-down maps: road / wall cap / water (derived when the columns are empty)
		Pal.Road = Pal.Path * 1.25f;
		Pal.Cap = FMath::Lerp(Pal.Prop, FLinearColor::White, 0.35f);
		Col(TEXT("MapRoadColor"), Pal.Road);
		Col(TEXT("MapCapColor"), Pal.Cap);
		Col(TEXT("MapWaterColor"), Pal.Water);
		Names(TEXT("RimProps"), Pal.RimProps);
		Names(TEXT("Lamps"), Pal.LampProps);
		Names(TEXT("Backdrop"), Pal.BackdropProps);
		Names(TEXT("MapTallProps"), Pal.TallProps);
		if (Pal.RimProps.Num() == 0) Pal.RimProps = { FName(TEXT("pillar")), FName(TEXT("gravestone")), FName(TEXT("post")) };
		if (Pal.LampProps.Num() == 0) Pal.LampProps = { FName(TEXT("post_lantern")), FName(TEXT("lantern_standing")) };
		if (Pal.BackdropProps.Num() == 0) Pal.BackdropProps = { FName(TEXT("tree_dead_large")), FName(TEXT("tree_dead_medium")) };
		if (Pal.TallProps.Num() == 0) Pal.TallProps = { FName(TEXT("crypt")) };
		return Pal;
	}

	/** Biomes.csv Map* layout columns (negative / none = Tuning.csv default). */
	struct FArenaMapStyle
	{
		FName Layout;
		int32 Landmarks = -1;
		int32 Tiers = -1;
		float TierHeight = -1.f;
		bool bTiersOnLandmarks = false;
		float BossRadius = -1.f;
	};

	FArenaMapStyle ArenaLoadMapStyle(FName Biome)
	{
		FArenaMapStyle Style;
		TMap<FString, FString> StyleCols;
		ArenaReadBiomeRow(Biome, StyleCols);
		auto Cell = [&StyleCols](const TCHAR* Key) -> const FString*
		{
			const FString* Found = StyleCols.Find(FString(Key).ToLower());
			return (Found && !Found->IsEmpty()) ? Found : nullptr;
		};
		if (const FString* LayoutCell = Cell(TEXT("MapLayout"))) Style.Layout = FName(**LayoutCell);
		if (const FString* LandmarkCell = Cell(TEXT("MapLandmarks"))) Style.Landmarks = FCString::Atoi(**LandmarkCell);
		if (const FString* TierCell = Cell(TEXT("MapTiers"))) Style.Tiers = FCString::Atoi(**TierCell);
		if (const FString* TierHCell = Cell(TEXT("MapTierHeight"))) Style.TierHeight = FCString::Atof(**TierHCell);
		if (const FString* TierModeCell = Cell(TEXT("MapTierMode"))) Style.bTiersOnLandmarks = TierModeCell->Equals(TEXT("Landmarks"), ESearchCase::IgnoreCase);
		if (const FString* BossCell = Cell(TEXT("MapBossRadius"))) Style.BossRadius = FCString::Atof(**BossCell);
		return Style;
	}

	bool HasSimpleCollision(const UStaticMesh* InMesh)
	{
		const UBodySetup* BS = InMesh ? InMesh->GetBodySetup() : nullptr;
		return BS && BS->AggGeom.GetElementCount() > 0;
	}

	/** Smoothstep on 0..1 (clamped). Peak gradient is 1.5 at T = 0.5. */
	float Smooth01(float T)
	{
		const float C = FMath::Clamp(T, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	/** Same number on every machine and every run (GetTypeHash(FName) depends on name-table order, so it is not). */
	int32 StableBiomeSeed(FName Biome)
	{
		const FString BiomeKey = Biome.ToString().ToLower();
		return static_cast<int32>(FCrc::StrCrc32(*BiomeKey) & 0x7fffffffu);
	}

	float DistToSegment2D(float PX, float PY, float AX, float AY, float BX, float BY)
	{
		const float SX = BX - AX;
		const float SY = BY - AY;
		const float LenSq = SX * SX + SY * SY;
		const float T = LenSq > 1.f ? FMath::Clamp(((PX - AX) * SX + (PY - AY) * SY) / LenSq, 0.f, 1.f) : 0.f;
		const float QX = AX + SX * T - PX;
		const float QY = AY + SY * T - PY;
		return FMath::Sqrt(QX * QX + QY * QY);
	}

	/** Closest point to P on segment A-B. */
	FVector2D ArenaMapClosest(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D S = B - A;
		const double LenSq = S.SizeSquared();
		const double T = LenSq > 1.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, S) / LenSq, 0.0, 1.0) : 0.0;
		return A + S * T;
	}

	/** 2D distance from (X, Y) to a polyline (huge when it has under two points). */
	float ArenaMapDistToPolyline(const TArray<FVector2D>& Pts, float X, float Y)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			Best = FMath::Min(Best, DistToSegment2D(X, Y, static_cast<float>(Pts[i].X), static_cast<float>(Pts[i].Y),
				static_cast<float>(Pts[i + 1].X), static_cast<float>(Pts[i + 1].Y)));
		}
		return Best;
	}

	/** Distance from the centre to the square edge (inset by Inset) in the direction of Ang. */
	float ArenaMapEdgeDist(float Ang, float Inset, float InHalf)
	{
		const float Axis = FMath::Max(FMath::Abs(FMath::Cos(Ang)), FMath::Abs(FMath::Sin(Ang)));
		return FMath::Max(0.f, InHalf - Inset) / FMath::Max(0.2f, Axis);
	}

	/** Angle wrapped to (-PI, PI]. */
	float ArenaMapWrap(float Ang)
	{
		float Out = FMath::Fmod(Ang + PI, 2.f * PI);
		if (Out < 0.f) Out += 2.f * PI;
		return Out - PI;
	}

	FVector2D ArenaMapDir(float Ang)
	{
		return FVector2D(FMath::Cos(Ang), FMath::Sin(Ang));
	}

	FVector2D ArenaMapCatmull(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, float T)
	{
		const double T1 = T;
		const double T2 = T1 * T1;
		const double T3 = T2 * T1;
		return (P1 * 2.0 + (P2 - P0) * T1 + (P0 * 2.0 - P1 * 5.0 + P2 * 4.0 - P3) * T2 + (P1 * 3.0 - P0 - P2 * 3.0 + P3) * T3) * 0.5;
	}

	/** Reorders Pts greedily so every next point is the farthest from those already taken (starting at First): any prefix is spread out. */
	void ArenaMapSpreadOrder(TArray<FVector2D>& Pts, int32 First)
	{
		const int32 Num = Pts.Num();
		if (Num <= 1) return;
		const TArray<FVector2D> Src = Pts;
		Pts.Reset();
		TArray<double> BestSq;
		BestSq.Init(TNumericLimits<double>::Max(), Num);
		TArray<bool> Used;
		Used.Init(false, Num);
		int32 Pick = FMath::Clamp(First, 0, Num - 1);
		for (int32 Taken = 0; Taken < Num; ++Taken)
		{
			Used[Pick] = true;
			Pts.Add(Src[Pick]);
			int32 Next = INDEX_NONE;
			double NextSq = -1.0;
			for (int32 j = 0; j < Num; ++j)
			{
				if (Used[j]) continue;
				BestSq[j] = FMath::Min(BestSq[j], FVector2D::DistSquared(Src[j], Src[Pick]));
				if (BestSq[j] > NextSq)
				{
					NextSq = BestSq[j];
					Next = j;
				}
			}
			if (Next == INDEX_NONE) break;
			Pick = Next;
		}
	}

	FName ArenaFadeTag()
	{
		return FName(TEXT("SQFadeable"));
	}

	/** One mesh section of the terrain (one material colour). */
	struct FSQTerrainSection
	{
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FVector2D> UV1s;   // ground art: plaza-local emblem coordinates
		TArray<FVector2D> UV2s;   // ground art: world XY in cm (pattern space)
		TArray<FLinearColor> Colors;
	};
}

ASQArena::ASQArena()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void ASQArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASQArena, BiomeId);
	DOREPLIFETIME(ASQArena, RunSeed);
}

void ASQArena::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	ZoneISM = NewObject<UInstancedStaticMeshComponent>(this);
	ZoneISM->SetStaticMesh(SQ::Mesh(TEXT("Cylinder")));
	ZoneISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneISM->SetCastShadow(false);
	ZoneISM->SetupAttachment(Root);
	ZoneISM->RegisterComponent();

	WarningISM = NewObject<UInstancedStaticMeshComponent>(this);
	WarningISM->SetStaticMesh(SQ::Mesh(TEXT("Cube")));
	WarningISM->SetMaterial(0, SQ::ColorMID(this, FLinearColor(0.9f, 0.15f, 0.1f)));
	WarningISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarningISM->SetCastShadow(false);
	WarningISM->SetupAttachment(Root);
	WarningISM->RegisterComponent();

	// An OnRep_Biome that arrived before BeginPlay may already have built the arena without the zone ISMs above
	BuiltBiome = NAME_None;
	Rebuild();
}

void ASQArena::SetBiome(FName NewBiome, int32 InRunSeed)
{
	if (!HasAuthority()) return;
	BiomeId = NewBiome;
	bRunSeedRolled = true;
	if (InRunSeed >= 0)
	{
		RunSeed = InRunSeed;
	}
	else
	{
		// A random map every run (Nov 9 2026). Tuning RandomMaps 0 keeps the fixed per-biome layout.
		const USQData* SeedData = USQData::Get(this);
		const bool bRandom = !SeedData || SeedData->Tune(TEXT("RandomMaps"), 1.f) >= 0.5f;
		RunSeed = bRandom ? FMath::Max(1, FMath::RandRange(1, 0x7ffffffe)) : 0;
	}
	Rebuild();
}

void ASQArena::OnRep_Biome()
{
	Rebuild();
}

UMaterialInstanceDynamic* ASQArena::SurfaceMID(UObject* Outer, const FLinearColor& Color, float Glow)
{
	// M_GroundV2 (map art pass: layered breakup, damp patches) when the setup script has made it, else the first M_Ground
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GroundV2.M_GroundV2"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ground.M_Ground"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Base) return SQ::ColorMID(Outer, Color);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
	return MID;
}

float ASQArena::FitMeshHeight(const UStaticMesh* InMesh, float Height)
{
	if (!InMesh) return 1.f;
	const float MeshH = static_cast<float>(InMesh->GetBounds().BoxExtent.Z) * 2.f;
	return MeshH > UE_KINDA_SMALL_NUMBER ? Height / MeshH : 1.f;
}

UStaticMeshComponent* ASQArena::AddBlock(FName Shape, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color, bool bCollide)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(SQ::Mesh(Shape));
	C->SetMaterial(0, SurfaceMID(this, Color));
	C->SetupAttachment(Root);
	C->SetRelativeLocation(Location);
	C->SetRelativeRotation(Rotation);
	C->SetRelativeScale3D(Scale);
	if (bCollide) C->SetCollisionProfileName(TEXT("BlockAll"));
	else C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->RegisterComponent();
	Built.Add(C);
	return C;
}

UStaticMeshComponent* ASQArena::AddArtPart(UStaticMesh* InMesh, const FTransform& Xf, UMaterialInstanceDynamic* InMID, bool bCollide)
{
	if (!InMesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(InMesh);
	if (InMID) SQArt::SetAllMaterials(C, InMID);
	C->SetupAttachment(Root);
	C->SetRelativeTransform(Xf);
	if (bCollide) C->SetCollisionProfileName(TEXT("BlockAll"));
	else C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->RegisterComponent();
	Built.Add(C);
	return C;
}

void ASQArena::MarkFadeable(UPrimitiveComponent* Comp)
{
	if (!Comp) return;
	Comp->ComponentTags.AddUnique(ArenaFadeTag());
	// The top-down camera fades it instead of a boom snapping in front of it
	Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Fadeables.Add(Comp);
}

UInstancedStaticMeshComponent* ASQArena::MakeArtISM(UStaticMesh* InMesh, UMaterialInstanceDynamic* InMID, bool bCollide, bool bShadow)
{
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetStaticMesh(InMesh);
	if (InMID) SQArt::SetAllMaterials(ISM, InMID);
	ISM->SetupAttachment(Root);
	if (bCollide) ISM->SetCollisionProfileName(TEXT("BlockAll"));
	else ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(bShadow);
	ISM->SetCanEverAffectNavigation(false);
	ISM->RegisterComponent();
	ArtISMs.Add(ISM);
	return ISM;
}

void ASQArena::Rebuild()
{
	const USQData* D = USQData::Get(this);
	if (!D || (BuiltBiome == BiomeId && BuiltRunSeed == RunSeed)) return;
	BuiltBiome = BiomeId;
	BuiltRunSeed = RunSeed;

	// The old biome's hazards go first (their EndPlay hands the moon / fog back before the new look is applied)
	if (Hazards)
	{
		Hazards->Destroy();
		Hazards = nullptr;
	}

	for (UStaticMeshComponent* C : Built)
	{
		if (C) C->DestroyComponent();
	}
	Built.Reset();
	for (UInstancedStaticMeshComponent* ISM : ArtISMs)
	{
		if (ISM) ISM->DestroyComponent();
	}
	ArtISMs.Reset();
	MoteISM = nullptr;
	LampBaseIntensity = 0.f;
	if (Terrain)
	{
		Terrain->DestroyComponent();
		Terrain = nullptr;
	}
	for (UPointLightComponent* MapLight : MapLights)
	{
		if (MapLight) MapLight->DestroyComponent();
	}
	MapLights.Reset();
	PropBlockers.Reset();
	Fadeables.Reset();
	BlockerSpots.Reset();
	IcePos.Reset();
	IceRadius.Reset();
	LampSpots.Reset();
	Plateaus.Reset();
	MapZones.Reset();
	MapWalls.Reset();
	RoadPts.Reset();
	WaterA.Reset();
	WaterB.Reset();
	WaterHalf.Reset();
	SpawnerSpots.Reset();
	CollectSpots.Reset();
	EliteSpots.Reset();
	LandmarkOrder.Reset();
	EliteOrder.Reset();
	HordeObs.Reset();
	HordeBuckets.Reset();
	HordeGridN = 0;
	BossZone = INDEX_NONE;
	Districts.Reset();
	Lanes.Reset();
	SkateFeatures.Reset();
	ParkSpots.Reset();
	SetMarks.Reset();
	TideWater = nullptr;
	ClockISM = nullptr;
	ClockLastHit.Reset();
	for (USceneComponent* Part : DistrictParts)
	{
		if (Part) Part->DestroyComponent();
	}
	DistrictParts.Reset();

	const FSQBiomeDef* B = D->FindBiome(BiomeId);
	const FSQArenaPalette Pal = ArenaLoadPalette(BiomeId, B);
	const FLinearColor PropColor = Pal.Prop;
	const FName PropShape = (B && !B->PropShape.IsNone()) ? B->PropShape : FName(TEXT("Cube"));
	// Map revamp: Biomes.csv counts were authored for the 112 m map; MapPropDensity scales them for the bigger one
	const float PropDensity = FMath::Clamp(D->Tune(TEXT("MapPropDensity"), 1.f), 0.25f, 6.f);
	const int32 PropCount = FMath::RoundToInt(static_cast<float>(B ? B->PropCount : 24) * PropDensity);
	const bool bVisuals = GetNetMode() != NM_DedicatedServer;

	// Top-down maps (Nov 9 2026): ~112 m across (ArenaHalfSize 5600). Map revamp (Nov 10): ~224 m (11200) with skate districts
	Half = FMath::Clamp(D->Tune(TEXT("ArenaHalfSize"), 5600.f), 3000.f, 12000.f);
	const float Size = Half * 2.f / 100.f;

	// Layout first (2D: hub, landmarks, boss arena, road, spawner pads, tiers, walls), then the ground under it.
	// Seeded from the biome name + the replicated RunSeed -> a new map every run, identical on every machine.
	// Random-map guard: each candidate is flood-filled (ValidateLayout); a failed one rolls the next derived seed,
	// and after MaxTries the biome's fixed layout (RunSeed 0, hand-checked) is used. All deterministic from RunSeed.
	const int32 BaseSeed = StableBiomeSeed(BiomeId);
	ReadMapShape(D, BaseSeed);
	int32 Seed = BaseSeed;
	const int32 MaxTries = 8;
	LayoutAttempt = -1;
	for (int32 Try = 0; Try <= MaxTries; ++Try)
	{
		const bool bFallback = RunSeed == 0 || Try == MaxTries;
		Seed = bFallback ? BaseSeed
			: static_cast<int32>((static_cast<uint32>(BaseSeed) ^ (static_cast<uint32>(RunSeed) * 2654435761u) ^ (static_cast<uint32>(Try + 1) * 40503u * 2246822519u)) & 0x7fffffffu);
		if (Try > 0) ResetLayoutState();
		PlanLayout(D, (Seed ^ 0x3C6EF372) & 0x7fffffff);
		BuildHeightGrid(D, (Seed ^ 0x1B873593) & 0x7fffffff);
		FString Why;
		if (bFallback || ValidateLayout(Why))
		{
			LayoutAttempt = bFallback && RunSeed != 0 ? -1 : Try;
			break;
		}
		UE_LOG(LogSQ, Log, TEXT("Arena %s: random layout %d rejected (%s), rolling again"), *BiomeId.ToString(), Try, *Why);
	}
	const int32 TerrainSeed = (Seed ^ 0x1B873593) & 0x7fffffff;
	MapSeed = Seed;
	PlanLamps(D);
	// Lamp blockers (built in BuildDressing) count as footprints from the start, so props keep a gap to them too
	for (const FVector& LampSpot : LampSpots) BlockerSpots.Add(FVector(LampSpot.X, LampSpot.Y, 30.0));

	// Imported props (none loaded -> primitive look)
	TArray<UStaticMesh*> PropMeshes;
	if (B) ArenaLoadProps(B->Props, PropMeshes);
	const bool bArt = PropMeshes.Num() > 0;
	const float PropScale = bArt ? ComputeArtScale() * D->Tune(TEXT("PropScale"), 1.6f) : 1.f;
	UMaterialInstanceDynamic* PropMID = bArt ? SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(PropColor, 0.3f)) : nullptr;
	const float FadeH = D->Tune(TEXT("MapFadeHeight"), 200.f);

	UE_LOG(LogSQ, Log, TEXT("Arena %s: art %s, prop scale %.3f, layout %s, run seed %d (attempt %d)"), *BiomeId.ToString(), bArt ? TEXT("ON") : TEXT("off"), PropScale, *LayoutName.ToString(), RunSeed, LayoutAttempt);
	BuildTerrainMesh(Pal, TerrainSeed);
	// Safety-net floor well below the lowest terrain (catches anything that slips through). Hidden: the terrain is the floor.
	if (UStaticMeshComponent* SafetyFloor = AddBlock(TEXT("Cube"), FVector(0.f, 0.f, TerrainMinZ - 350.f), FRotator::ZeroRotator, FVector(Size, Size, 1.f), Pal.Grass * 0.5f, true))
	{
		SafetyFloor->SetVisibility(false);
		SafetyFloor->SetCastShadow(false);
	}

	// Collision walls: far taller than a gust vent can throw anyone. Invisible (the low stone wall below is the look) and
	// transparent to the camera. ServerSafetyNet catches anything that still escapes.
	const float WallBottom = TerrainMinZ - 500.f;
	const float WallTop = FMath::Max(350.f, TerrainMaxZ + 4000.f);
	const float WallZ = (WallBottom + WallTop) * 0.5f;
	const float WallScaleZ = (WallTop - WallBottom) / 100.f;
	const FVector WallSpots[4] = { FVector(Half + 50.f, 0.f, WallZ), FVector(-Half - 50.f, 0.f, WallZ), FVector(0.f, Half + 50.f, WallZ), FVector(0.f, -Half - 50.f, WallZ) };
	for (int32 w = 0; w < 4; ++w)
	{
		const FVector WallScale = w < 2 ? FVector(1.f, Size + 2.f, WallScaleZ) : FVector(Size + 2.f, 1.f, WallScaleZ);
		if (UStaticMeshComponent* Collider = AddBlock(TEXT("Cube"), WallSpots[w], FRotator::ZeroRotator, WallScale, Pal.Wall, true))
		{
			Collider->SetVisibility(false);
			Collider->SetCastShadow(false);
			Collider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
	}
	if (bVisuals) BuildWalls(Pal, PropScale, bArt);
	// A pawn that falls under KillZ is destroyed (soft-lock). Keep KillZ far below the safety net's floor check.
	if (AWorldSettings* WS = GetWorld() ? GetWorld()->GetWorldSettings() : nullptr)
	{
		WS->KillZ = FMath::Min(WS->KillZ, TerrainMinZ - 20000.f);
	}

	// Layout walls: collision on every machine (they block reapers, the boss and - via the horde grid - the horde)
	BuildLayoutWalls(Pal, PropScale, bArt);
	// Map revamp: bone park pieces (collision + horde footprints everywhere), lane chevrons, beacons, name plates
	BuildDistricts(D, bVisuals);

	// Props: same seed on every machine -> identical layout
	FRandomStream Rand(Seed);
	// Art-only choices use their own stream so Rand (and the ice layout) is identical with or without art
	FRandomStream ArtRand(Seed ^ 0x2A5F17);
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> PropISMs;
	const float PropMaxSlope = D->Tune(TEXT("TerrainPropMaxSlope"), 20.f);

	// Edge props stay off ramps, off the road, out of the mission zones and away from lamps
	auto CanPlaceProp = [&](float CandX, float CandY) -> bool
	{
		if (GroundSlopeDeg(CandX, CandY) > PropMaxSlope) return false;
		if (GroundZ(CandX, CandY) < -15.f) return false;
		if (PathMask(CandX, CandY) > 0.05f) return false;
		if (IsReservedSpot(CandX, CandY, 150.f)) return false;
		for (const FVector& Lamp : LampSpots)
		{
			if (FMath::Square(static_cast<float>(Lamp.X) - CandX) + FMath::Square(static_cast<float>(Lamp.Y) - CandY) < 200.f * 200.f) return false;
		}
		return true;
	};

	auto PlaceProp = [&](float AtX, float AtY, float PropTilt, float PropYaw, float PropShade, float PropHeight, UStaticMesh* ForceMesh = nullptr) -> bool
	{
		const float BaseZ = GroundZ(AtX, AtY) - 6.f;
		if (!bArt)
		{
			const float PrimR = 45.f;
			if (!IsPropFootprintFree(AtX, AtY, PrimR)) return false;
			if (UStaticMeshComponent* PrimProp = AddBlock(PropShape, FVector(AtX, AtY, BaseZ + 50.f * PropHeight), FRotator(PropTilt, PropYaw, 0.f),
				FVector(0.7f, PropShape == TEXT("Cube") ? 0.25f : 0.7f, PropHeight), PropColor * PropShade, true))
			{
				PrimProp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				PropBlockers.Add(PrimProp);
			}
			BlockerSpots.Add(FVector(AtX, AtY, PrimR));
			return true;
		}
		UStaticMesh* PropMesh = ForceMesh ? ForceMesh : PropMeshes[ArtRand.RandRange(0, PropMeshes.Num() - 1)];
		const float PS = PropScale * ArtRand.FRandRange(0.85f, 1.2f);
		// Footprint (same estimate as the fallback blocker below). Every prop keeps a capsule-wide gap to the others and the walls.
		const FVector PropExt = PropMesh->GetBounds().BoxExtent;
		const float FootR = static_cast<float>(FMath::Max(PropExt.X, PropExt.Y)) * PS * 0.8f;
		if (!IsPropFootprintFree(AtX, AtY, FootR)) return false;
		BlockerSpots.Add(FVector(AtX, AtY, FootR));
		const FTransform PropXf = ArenaSitTransform(PropMesh, FVector(AtX, AtY, BaseZ), FQuat(FRotator(0.f, PropYaw, 0.f)), FVector(PS));
		const float PropTall = static_cast<float>(PropExt.Z) * 2.f * PS;
		if (PropTall > FadeH)
		{
			// Tall edge decoration: its own component and material so the camera can fade just this one
			if (UStaticMeshComponent* TallProp = AddArtPart(PropMesh, PropXf, SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(PropColor, 0.3f)), true))
			{
				TallProp->SetCastShadow(true);
				MarkFadeable(TallProp);
				PropBlockers.Add(TallProp);
			}
		}
		else
		{
			UInstancedStaticMeshComponent*& PropISM = PropISMs.FindOrAdd(PropMesh);
			if (!PropISM)
			{
				PropISM = MakeArtISM(PropMesh, PropMID, true, true);
				PropISM->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				PropBlockers.Add(PropISM);
			}
			PropISM->AddInstance(PropXf);
		}

		if (!HasSimpleCollision(PropMesh))
		{
			// No simple collision on the imported mesh: add an invisible blocking cylinder instead
			const float BlockH = FMath::Max(PropTall, 100.f);
			if (UStaticMeshComponent* Blocker = AddBlock(TEXT("Cylinder"), FVector(AtX, AtY, BaseZ + BlockH * 0.5f), FRotator::ZeroRotator,
				FVector(FootR / 50.f, FootR / 50.f, BlockH / 100.f), PropColor, true))
			{
				Blocker->SetCastShadow(false);
				Blocker->SetVisibility(false);
				Blocker->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				PropBlockers.Add(Blocker);
			}
		}
		return true;
	};

	// Graves / crypts hug the walls (Biomes.csv PropCount): tall decoration lives at the edges only, framing the arena
	// Map remaster: grouped into silhouette clusters (MapPropClusterSize per anchor) instead of evenly sprinkled noise,
	// so the edges read as designed vignettes with open ground between them
	const int32 ClusterSize = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapPropClusterSize"), 4.f)), 1, 8);
	int32 EdgeSide = 0;
	float AnchorAlong = 0.f;
	float AnchorInset = 300.f;
	for (int32 i = 0; i < PropCount; ++i)
	{
		// Always consume the same rolls so the stream (and the ice layout after it) never depends on the terrain test
		if (i % ClusterSize == 0)
		{
			EdgeSide = Rand.RandRange(0, 3);
			AnchorAlong = Rand.FRandRange(-Half * 0.9f, Half * 0.9f);
			AnchorInset = Rand.FRandRange(240.f, 520.f);
		}
		const float JitterAlong = Rand.FRandRange(-340.f, 340.f);
		const float JitterInset = Rand.FRandRange(-90.f, 160.f);
		const float Shade = Rand.FRandRange(0.8f, 1.15f);
		// The first of a cluster is its tall centrepiece; the rest step down around it
		const float Height = (i % ClusterSize == 0) ? Rand.FRandRange(1.35f, 1.7f) : Rand.FRandRange(0.8f, 1.25f);
		const float Tilt = Rand.FRandRange(-6.f, 6.f);
		const float Yaw = Rand.FRandRange(0.f, 360.f);
		const float EdgeAlong = FMath::Clamp(AnchorAlong + JitterAlong, -Half * 0.94f, Half * 0.94f);
		const float EdgeOut = Half - FMath::Clamp(AnchorInset + JitterInset, 170.f, 700.f);
		const float PX = EdgeSide == 0 ? EdgeOut : (EdgeSide == 1 ? -EdgeOut : EdgeAlong);
		const float PY = EdgeSide < 2 ? EdgeAlong : (EdgeSide == 2 ? EdgeOut : -EdgeOut);
		if (!CanPlaceProp(PX, PY)) continue;
		PlaceProp(PX, PY, Tilt, Yaw, Shade, Height);
	}

	// --- Composed vignettes (map art pass): the open ground between the districts gets small designed scenes instead
	// of nothing: one tall centrepiece with one or two straight rows of low props in front of it, all facing the same
	// way (a family plot, a row of stalls). Rows keep the usual capsule-wide gaps, so nothing can be sealed off, and
	// they stay out of the hub's kiting ring, the roads, lanes, plazas, spawner pads and the skate ground.
	// Own stream: the main one (ice layout etc.) is untouched.
	TArray<FVector2D> VignetteSpots;
	{
		FRandomStream VigRand(Seed ^ 0x7F4A7C15);
		TArray<UStaticMesh*> TallMeshes;
		TArray<UStaticMesh*> LowMeshes;
		for (UStaticMesh* Candidate : PropMeshes)
		{
			const float Tall = static_cast<float>(Candidate->GetBounds().BoxExtent.Z) * 2.f * PropScale;
			(Tall > FadeH ? TallMeshes : LowMeshes).Add(Candidate);
		}
		const int32 Vignettes = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapVignettes"), 34.f)), 0, 120);
		const float KeepOpen = HubRadius + KiteRing;
		for (int32 VigIdx = 0; VigIdx < Vignettes; ++VigIdx)
		{
			// Fixed rolls per vignette
			const float VX = VigRand.FRandRange(-Half * 0.86f, Half * 0.86f);
			const float VY = VigRand.FRandRange(-Half * 0.86f, Half * 0.86f);
			const float RowYawDeg = static_cast<float>(VigRand.RandRange(0, 7)) * 45.f + VigRand.FRandRange(-8.f, 8.f);
			const int32 Rows = VigRand.RandRange(1, 2);
			const int32 PerRow = VigRand.RandRange(3, 5);
			const int32 TallPick = VigRand.RandRange(0, 1023);
			const int32 LowPick = VigRand.RandRange(0, 1023);
			const float CentreHeight = VigRand.FRandRange(1.35f, 1.7f);
			if (VX * VX + VY * VY < KeepOpen * KeepOpen || !CanPlaceProp(VX, VY) || FMath::Abs(SkateHeight(VX, VY)) > 2.f) continue;
			const FVector2D RowDir = ArenaMapDir(FMath::DegreesToRadians(RowYawDeg));
			const FVector2D Front(-RowDir.Y, RowDir.X);
			UStaticMesh* TallMesh = TallMeshes.Num() > 0 ? TallMeshes[TallPick % TallMeshes.Num()] : nullptr;
			UStaticMesh* LowMesh = LowMeshes.Num() > 0 ? LowMeshes[LowPick % LowMeshes.Num()] : nullptr;
			const float LowR = LowMesh ? static_cast<float>(FMath::Max(LowMesh->GetBounds().BoxExtent.X, LowMesh->GetBounds().BoxExtent.Y)) * PropScale : 45.f;
			const float Spacing = 2.f * LowR + 135.f;
			int32 Placed = 0;
			if (PlaceProp(VX, VY, 0.f, RowYawDeg + 90.f, 1.f, CentreHeight, TallMesh)) ++Placed;
			for (int32 RowIdx = 0; RowIdx < Rows; ++RowIdx)
			{
				const FVector2D RowAt = FVector2D(VX, VY) + Front * (330.f + RowIdx * (2.f * LowR + 190.f));
				for (int32 k = 0; k < PerRow; ++k)
				{
					const FVector2D At = RowAt + RowDir * ((static_cast<float>(k) - 0.5f * static_cast<float>(PerRow - 1)) * Spacing);
					const float AX = static_cast<float>(At.X);
					const float AY = static_cast<float>(At.Y);
					const float Jit = VigRand.FRandRange(-5.f, 5.f);
					const float Gone = VigRand.FRand();
					if (Gone < 0.14f || FMath::Abs(AX) > Half * 0.94f || FMath::Abs(AY) > Half * 0.94f) continue; // a gap in the row: worn, not stamped
					if (!CanPlaceProp(AX, AY) || FMath::Abs(SkateHeight(AX, AY)) > 2.f) continue;
					if (PlaceProp(AX, AY, 0.f, RowYawDeg + 90.f + Jit, 1.f, 1.f, LowMesh)) ++Placed;
				}
			}
			if (Placed > 0) VignetteSpots.Add(FVector2D(VX, VY) + Front * 300.f);
		}
		UE_LOG(LogSQ, Log, TEXT("Map vignettes: %d of %d placed"), VignetteSpots.Num(), Vignettes);
	}

	// Floor rule (Nov 12 2026 map remaster): nothing lies on the floor except pickups. The loose scatter (bones, skulls,
	// candles), tier rim stones, grass tufts, flagstone slabs, kerbs, lane candles and coping stones are all gone;
	// MapFloorClutter 1 brings them back for comparison.
	const bool bFloorClutter = D->Tune(TEXT("MapFloorClutter"), 0.f) > 0.5f;
	if (bArt && B && bVisuals && bFloorClutter)
	{
		// Small non-blocking scatter in little clusters (bones, skulls, candles), tilted to the ground. Mostly along the walls;
		// never in the hub, on the road, in water or on a mission zone floor (objectives read clean from above).
		TArray<UStaticMesh*> DecorMeshes;
		ArenaLoadProps(B->Decor, DecorMeshes);
		if (DecorMeshes.Num() > 0)
		{
			TMap<UStaticMesh*, UInstancedStaticMeshComponent*> DecorISMs;
			const float DecorScale = PropScale * D->Tune(TEXT("DecorScale"), 1.25f);
			const int32 DecorCull = FMath::RoundToInt(D->Tune(TEXT("DecorCullDistance"), 9000.f));
			float ClusterX = 0.f;
			float ClusterY = 0.f;
			int32 ClusterLeft = 0;
			const int32 DecorCount = FMath::RoundToInt(static_cast<float>(B->DecorCount) * PropDensity);
			for (int32 i = 0; i < DecorCount; ++i)
			{
				if (ClusterLeft <= 0)
				{
					const bool bEdgeCluster = ArtRand.FRand() < 0.65f;
					const int32 ClusterSide = ArtRand.RandRange(0, 3);
					const float ClusterAlong = ArtRand.FRandRange(-Half * 0.95f, Half * 0.95f);
					const float ClusterInset = ArtRand.FRandRange(200.f, 1300.f);
					const float FreeX = ArtRand.FRandRange(-Half * 0.95f, Half * 0.95f);
					const float FreeY = ArtRand.FRandRange(-Half * 0.95f, Half * 0.95f);
					if (bEdgeCluster)
					{
						const float ClusterOut = Half - ClusterInset;
						ClusterX = ClusterSide == 0 ? ClusterOut : (ClusterSide == 1 ? -ClusterOut : ClusterAlong);
						ClusterY = ClusterSide < 2 ? ClusterAlong : (ClusterSide == 2 ? ClusterOut : -ClusterOut);
					}
					else if (VignetteSpots.Num() > 0 && (i & 3) != 0)
					{
						// Candles, bones and skulls gather round the vignettes (detail where there is already a scene)
						const FVector2D& Scene = VignetteSpots[ArtRand.RandRange(0, VignetteSpots.Num() - 1)];
						ClusterX = static_cast<float>(Scene.X);
						ClusterY = static_cast<float>(Scene.Y);
					}
					else
					{
						ClusterX = FreeX;
						ClusterY = FreeY;
					}
					ClusterLeft = ArtRand.RandRange(1, 4);
				}
				--ClusterLeft;
				const float DX = FMath::Clamp(ClusterX + ArtRand.FRandRange(-220.f, 220.f), -Half * 0.97f, Half * 0.97f);
				const float DY = FMath::Clamp(ClusterY + ArtRand.FRandRange(-220.f, 220.f), -Half * 0.97f, Half * 0.97f);
				const float DYaw = ArtRand.FRandRange(0.f, 2.f * PI);
				UStaticMesh* DecorMesh = DecorMeshes[ArtRand.RandRange(0, DecorMeshes.Num() - 1)];
				const float DS = DecorScale * ArtRand.FRandRange(0.85f, 1.25f);
				if (IsReservedSpot(DX, DY, 60.f)) continue;
				if (GroundSlopeDeg(DX, DY) > 20.f || GroundZ(DX, DY) < -15.f) continue;
				// A step away from walls, lamps and edge graves
				if (!IsPropFootprintFree(DX, DY, 25.f)) continue;
				UInstancedStaticMeshComponent*& DecorISM = DecorISMs.FindOrAdd(DecorMesh);
				if (!DecorISM)
				{
					DecorISM = MakeArtISM(DecorMesh, PropMID, false, false);
					DecorISM->SetCullDistances(DecorCull / 2, DecorCull);
				}
				const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, GroundNormal(DX, DY));
				DecorISM->AddInstance(ArenaSitTransform(DecorMesh, FVector(DX, DY, GroundZ(DX, DY) - 3.f), Align * FQuat(FVector::UpVector, DYaw), FVector(DS)));
			}
		}
	}

	// Lamps (+ their blockers on every machine), tier rim stones, grass tufts
	BuildDressing(D, Pal, Seed, PropScale, bArt);
	// Map remaster: flagstone plazas, road kerbs and landmark marker posts (visual only, no collision)
	if (bVisuals && bFloorClutter) BuildFloorDetail(D, Pal, Seed);

	// Gimmick setup
	const ESQGimmick Gimmick = B ? B->Gimmick : ESQGimmick::None;
	FLinearColor ZoneColor = FLinearColor(0.3f, 0.6f, 1.f);
	if (Gimmick == ESQGimmick::Ice)
	{
		for (int32 i = 0; i < 12; ++i)
		{
			const float IX = Rand.FRandRange(-Half * 0.8f, Half * 0.8f);
			const float IY = Rand.FRandRange(-Half * 0.8f, Half * 0.8f);
			const float R = Rand.FRandRange(500.f, 900.f);
			// Not on water, and never over a landmark centre (defend points stay grippy)
			if (GroundZ(IX, IY) < -15.f) continue;
			bool bOnLandmark = false;
			for (const FSQMapZone& IceZone : MapZones)
			{
				if (IceZone.Kind == ESQZoneKind::Landmark && FVector2D::Distance(IceZone.Centre, FVector2D(IX, IY)) < IceZone.Radius * 0.5f + R) bOnLandmark = true;
			}
			if (bOnLandmark) continue;
			IcePos.Add(FVector2D(IX, IY));
			IceRadius.Add(R);
		}
		ZoneColor = FLinearColor(0.75f, 0.92f, 1.f);
		BuildIceOverlay();
	}
	else if (Gimmick == ESQGimmick::Spotlight)
	{
		ZoneColor = FLinearColor(1.f, 0.9f, 0.45f);
	}
	else if (Gimmick == ESQGimmick::Tide)
	{
		ZoneColor = FLinearColor(0.15f, 0.45f, 0.75f);
		if (bVisuals)
		{
			// The tide: a sheet of light over the flats (additive, so pickups under it stay visible); solid colour without M_SQGlow
			const FLinearColor WaterColor(0.10f, 0.55f, 0.62f);
			TideWater = AddBlock(TEXT("Cube"), FVector(0.f, 0.f, TerrainMinZ - 450.f), FRotator::ZeroRotator, FVector(Size * 1.3f, Size * 1.3f, 1.f), WaterColor, false);
			if (TideWater)
			{
				UMaterialInstanceDynamic* WaterMID = SQArt::GlowMID(this, WaterColor, FMath::Clamp(D->Tune(TEXT("TideGlow"), 0.55f), 0.05f, 4.f), 0.f);
				if (WaterMID) TideWater->SetMaterial(0, WaterMID);
				TideWater->SetCastShadow(false);
			}
		}
	}
	if (ClockHands > 0 && bVisuals)
	{
		const FLinearColor HandColor(1.f, 0.72f, 0.22f);
		UMaterialInstanceDynamic* HandMID = SQArt::GlowMID(this, HandColor, 2.2f, 0.f);
		ClockISM = MakeArtISM(SQ::Mesh(TEXT("Cube")), HandMID ? HandMID : SurfaceMID(this, HandColor, 3.f), false, false);
	}
	if (ZoneISM)
	{
		ZoneISM->SetMaterial(0, SurfaceMID(this, ZoneColor, Gimmick == ESQGimmick::Spotlight ? 0.6f : 0.15f));
		ZoneISM->ClearInstances();
	}
	if (WarningISM) WarningISM->ClearInstances();

	if (bVisuals)
	{
		BuildBackdrop(D, Pal, Seed, PropScale);
		BuildSky(D, Pal, Seed, PropScale);
		ApplyAtmosphere(D, Pal);
	}

	// Horde blockers: every wall, blocking prop and lamp footprint (the horde walks round them)
	BuildHordeGrid();

	// Navigation hazards + secrets (Biomes.csv Hazards / HazardCounts / Secrets). Built on every machine from the
	// biome seed (after the props and walls, which they avoid).
	{
		TMap<FString, FString> HazardCols;
		ArenaReadBiomeRow(BiomeId, HazardCols);
		SecretIds.Reset();
		if (const FString* SecretCell = HazardCols.Find(TEXT("secrets")))
		{
			TArray<FString> SecretParts;
			SecretCell->ParseIntoArray(SecretParts, TEXT("|"), true);
			for (const FString& SecretPart : SecretParts)
			{
				const FString SecretName = SecretPart.TrimStartAndEnd();
				if (!SecretName.IsEmpty()) SecretIds.Add(FName(*SecretName));
			}
		}
		const FString* HazardCell = HazardCols.Find(TEXT("hazards"));
		const FString* HazardCountCell = HazardCols.Find(TEXT("hazardcounts"));
		if (HazardCell && !HazardCell->IsEmpty() && GetWorld())
		{
			FActorSpawnParameters HazardSpawn;
			HazardSpawn.Owner = this;
			HazardSpawn.ObjectFlags |= RF_Transient;
			HazardSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Hazards = GetWorld()->SpawnActor<ASQHazardField>(ASQHazardField::StaticClass(), FTransform::Identity, HazardSpawn);
			if (Hazards)
			{
				Hazards->Setup(this, *HazardCell, HazardCountCell ? *HazardCountCell : FString(), (Seed ^ 0x48A2D1) & 0x7fffffff, Pal.Fog, Pal.Lamp);
			}
		}
	}

	// Objective spot pools (they avoid the hazards, so after them)
	BuildObjectivePools();

	UE_LOG(LogSQ, Log, TEXT("Map %s (%s): %.0f m across, grid %dx%d, height %.0f..%.0f cm, %d tiers, %d zones, %d walls, road %d pts, %d spawner pads, %d collect spots, %d lamps (%d lit), %d fadeables"),
		*BiomeId.ToString(), *LayoutName.ToString(), Half * 2.f / 100.f, GridN, GridN, TerrainMinZ, TerrainMaxZ, Plateaus.Num(), MapZones.Num(), MapWalls.Num(),
		RoadPts.Num(), SpawnerSpots.Num(), CollectSpots.Num(), LampSpots.Num(), MapLights.Num(), Fadeables.Num());

	++BuildSerial;
	// Biome switch in the lobby: the new ground / walls may have swallowed someone standing on the old ones
	if (HasAuthority()) ServerRePlaceCharacters();

	// Shrines, altars, pots and secrets (server; replicated actors)
	if (HasAuthority())
	{
		const ASQGameState* SpawnGS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
		ServerSpawnInteractables((SpawnGS && SpawnGS->Phase == ESQPhase::Night) ? SpawnGS->NightIndex : 0);
	}
}

// ---------------------------------------------------------------- interactables

ASQHazardField* ASQArena::GetHazards() const
{
	return Hazards.Get();
}

bool ASQArena::FindInteractSpot(FRandomStream& Rand, float Radius, bool bHigh, bool bSmall, FVector& Out) const
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	const ASQLantern* Lantern = GS ? GS->Lantern.Get() : nullptr;
	const FVector LanternAt = Lantern ? Lantern->GetActorLocation() : FVector::ZeroVector;
	const float LanternKeep = Lantern ? Lantern->Radius + Radius + 300.f : 0.f;
	// Shrines use the lantern-spot rules on their inner area (flat, clear of walls / props / lamps / ice / hazards /
	// other shrines); the full ring may run over gentle bumps (it follows the ground)
	const float CheckR = FMath::Min(Radius, 260.f);

	auto SpotOk = [&](float X, float Y) -> bool
	{
		if (FMath::Abs(X) > Half - Radius - 300.f || FMath::Abs(Y) > Half - Radius - 300.f) return false;
		if (Lantern && FMath::Square(X - static_cast<float>(LanternAt.X)) + FMath::Square(Y - static_cast<float>(LanternAt.Y)) < LanternKeep * LanternKeep) return false;
		// Mission areas stay open (hub, landmark floors, boss arena, road, spawner pads)
		if (IsReservedSpot(X, Y, Radius * 0.6f)) return false;
		for (const FVector2D& Collect : CollectSpots)
		{
			if (FVector2D::DistSquared(Collect, FVector2D(X, Y)) < FMath::Square(static_cast<double>(Radius) + 200.0)) return false;
		}
		if (bSmall)
		{
			if (GroundSlopeDeg(X, Y) > 20.f || GroundZ(X, Y) < -15.f) return false;
			if (!IsPropFootprintFree(X, Y, Radius)) return false;
			if (Hazards && Hazards->IsSpotBlocked(X, Y, Radius)) return false;
			for (const FVector& Taken : InteractSpots)
			{
				const float Need = static_cast<float>(Taken.Z) + Radius + 120.f;
				if (FMath::Square(static_cast<float>(Taken.X) - X) + FMath::Square(static_cast<float>(Taken.Y) - Y) < Need * Need) return false;
			}
			return true;
		}
		if (!IsLanternSpotClear(X, Y, CheckR)) return false;
		for (const FVector& Taken : InteractSpots)
		{
			const float Need = static_cast<float>(Taken.Z) + Radius + 250.f;
			if (FMath::Square(static_cast<float>(Taken.X) - X) + FMath::Square(static_cast<float>(Taken.Y) - Y) < Need * Need) return false;
		}
		return true;
	};

	// High ground: somewhere on a tier top (rewards the climb)
	if (bHigh && Plateaus.Num() > 0)
	{
		for (int32 Try = 0; Try < 24; ++Try)
		{
			const FSQTerrainPlateau& Pl = Plateaus[Rand.RandRange(0, Plateaus.Num() - 1)];
			const float Ang = Rand.FRandRange(0.f, 2.f * PI);
			const float MaxR = FMath::Max(0.f, Pl.RadiusAt(Ang) * 0.85f - Radius);
			const float Dist = Rand.FRandRange(0.f, MaxR);
			const float X = static_cast<float>(Pl.Centre.X) + FMath::Cos(Ang) * Dist;
			const float Y = static_cast<float>(Pl.Centre.Y) + FMath::Sin(Ang) * Dist;
			if (!SpotOk(X, Y)) continue;
			Out = FVector(X, Y, GroundZ(X, Y));
			return true;
		}
	}
	for (int32 Try = 0; Try < 80; ++Try)
	{
		const float X = Rand.FRandRange(-Half * 0.9f, Half * 0.9f);
		const float Y = Rand.FRandRange(-Half * 0.9f, Half * 0.9f);
		if (!SpotOk(X, Y)) continue;
		Out = FVector(X, Y, GroundZ(X, Y));
		return true;
	}
	return false;
}

void ASQArena::ServerSpawnInteractables(int32 Salt)
{
	UWorld* World = GetWorld();
	const USQData* D = USQData::Get(this);
	if (!HasAuthority() || !World || !D || GridN <= 0) return;

	for (AActor* Old : Interactables)
	{
		if (IsValid(Old)) Old->Destroy();
	}
	Interactables.Reset();
	InteractSpots.Reset();
	InteractSalt = Salt;

	// Same biome + night = same layout (deterministic), a new night = a fresh map to explore
	FRandomStream Rand(static_cast<int32>((static_cast<uint32>(MapSeed) ^ 0x5A17E5u) + static_cast<uint32>(FMath::Max(0, Salt)) * 7919u) & 0x7fffffff);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 NumShrines = 0;
	int32 NumPots = 0;
	int32 NumMissed = 0;
	// Map revamp: the bigger map gets more of the common ones (Tuning MapInteractScale); one-offs stay one-offs
	const float InteractScale = FMath::Clamp(D->Tune(TEXT("MapInteractScale"), 1.f), 0.25f, 6.f);
	// Shrines first (big footprints), then pots in the gaps
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const FSQShrineDef& Def : D->Shrines)
		{
			const bool bPot = ASQShrine::ParseType(Def.Type) == ESQShrineType::Pot;
			if (bPot != (Pass == 1)) continue;
			if (Def.Biomes.Num() > 0 && !Def.Biomes.Contains(BiomeId)) continue;
			if (Def.bSecret && !SecretIds.Contains(Def.Id)) continue;
			const int32 Copies = (bPot || Def.PerBiome >= 2) ? FMath::Max(1, FMath::RoundToInt(static_cast<float>(Def.PerBiome) * InteractScale)) : Def.PerBiome;
			for (int32 CopyIdx = 0; CopyIdx < Copies; ++CopyIdx)
			{
				// Both rolls every copy, so one copy's luck never shifts the next one's
				const bool bSpawn = Rand.FRand() < Def.Chance;
				const bool bHigh = Rand.FRand() < Def.HighGround;
				if (!bSpawn) continue;
				FVector Spot;
				if (!FindInteractSpot(Rand, Def.Radius, bHigh, bPot, Spot))
				{
					++NumMissed;
					continue;
				}
				AActor* Made = nullptr;
				if (bPot)
				{
					if (ASQPot* Pot = World->SpawnActor<ASQPot>(ASQPot::StaticClass(), Spot, FRotator::ZeroRotator, SpawnParams))
					{
						Pot->InitPot(Def.Id);
						Made = Pot;
						++NumPots;
					}
				}
				else if (ASQShrine* Shrine = World->SpawnActor<ASQShrine>(ASQShrine::StaticClass(), Spot, FRotator::ZeroRotator, SpawnParams))
				{
					Shrine->InitShrine(Def.Id);
					Made = Shrine;
					++NumShrines;
				}
				if (Made)
				{
					Interactables.Add(Made);
					InteractSpots.Add(FVector(Spot.X, Spot.Y, Def.Radius));
				}
			}
		}
	}
	UE_LOG(LogSQ, Log, TEXT("Interactables %s (night salt %d): %d shrines, %d pots, %d could not find a spot"), *BiomeId.ToString(), Salt, NumShrines, NumPots, NumMissed);
}

void ASQArena::ServerWatchPhase()
{
	const UWorld* World = GetWorld();
	const ASQGameState* GS = World ? World->GetGameState<ASQGameState>() : nullptr;
	if (!GS || GridN <= 0) return;
	const uint8 PhaseNow = static_cast<uint8>(GS->Phase);
	if (PhaseNow == LastPhaseSeen) return;
	const uint8 PrevPhase = LastPhaseSeen;
	LastPhaseSeen = PhaseNow;
	if (PrevPhase == 255) return; // first look: Rebuild already placed them
	if (GS->Phase == ESQPhase::Night)
	{
		// Every night gets a fresh layout; the first night keeps the lobby's (nobody could touch it there)
		const bool bFromLobby = PrevPhase == static_cast<uint8>(ESQPhase::Waiting);
		if (!bFromLobby || GS->NightIndex != InteractSalt) ServerSpawnInteractables(GS->NightIndex);
	}
	else if (GS->Phase == ESQPhase::Waiting)
	{
		ServerSpawnInteractables(0);
	}
}

// ---------------------------------------------------------------- gimmick maths

float ASQArena::Now() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.f;
}

ESQGimmick ASQArena::GetGimmick() const
{
	const USQData* D = USQData::Get(this);
	const FSQBiomeDef* B = D ? D->FindBiome(BiomeId) : nullptr;
	return B ? B->Gimmick : ESQGimmick::None;
}

bool ASQArena::IsGimmickActive() const
{
	const ASQGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASQGameState>() : nullptr;
	return GS && (GS->Phase == ESQPhase::Night || GS->Phase == ESQPhase::Boss);
}

// ---------------------------------------------------------------- map twists (three-map remaster, Docs/Maps.md)

void ASQArena::ReadMapShape(const USQData* D, int32 /*Seed*/)
{
	TMap<FString, FString> Cols;
	ArenaReadBiomeRow(BiomeId, Cols);
	MapShape = NAME_None;
	MacroTier = 240.f;
	MacroSteps = 2;
	SetRoles.Reset();
	if (const FString* Shape = Cols.Find(TEXT("mapshape")))
	{
		if (!Shape->IsEmpty() && D->Tune(TEXT("MapShapes"), 1.f) > 0.5f) MapShape = FName(**Shape);
	}
	if (const FString* Tier = Cols.Find(TEXT("mapshapetier")))
	{
		if (!Tier->IsEmpty()) MacroTier = FMath::Clamp(FCString::Atof(**Tier), 0.f, 600.f);
	}
	if (const FString* Steps = Cols.Find(TEXT("mapshapesteps")))
	{
		if (!Steps->IsEmpty()) MacroSteps = FMath::Clamp(FCString::Atoi(**Steps), 1, 8);
	}
	if (const FString* Sets = Cols.Find(TEXT("setpieces")))
	{
		TArray<FString> Pairs;
		Sets->ParseIntoArray(Pairs, TEXT("|"), true);
		for (const FString& Pair : Pairs)
		{
			FString RoleName, MeshName;
			if (Pair.Split(TEXT(":"), &RoleName, &MeshName)) SetRoles.FindOrAdd(FName(*RoleName.TrimStartAndEnd())).Add(FName(*MeshName.TrimStartAndEnd()));
		}
	}
	// Twist numbers
	const ESQGimmick Gimmick = GetGimmick();
	TideCycle = FMath::Clamp(D->Tune(TEXT("TideCycle"), 60.f), 20.f, 300.f);
	TideLowZ = D->Tune(TEXT("TideLow"), -120.f);
	TideHighZ = MacroTier * FMath::Clamp(D->Tune(TEXT("TideHighShare"), 0.62f), 0.1f, 0.95f);
	TideSlow = FMath::Clamp(D->Tune(TEXT("TideSlow"), 0.6f), 0.2f, 1.f);
	TideEnemyDps = FMath::Max(0.f, D->Tune(TEXT("TideEnemyDps"), 10.f));
	ClockHands = Gimmick == ESQGimmick::TimeStop ? FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("ClockHands"), 2.f)), 0, 3) : 0;
	ClockPeriod = FMath::Clamp(D->Tune(TEXT("ClockHandPeriod"), 50.f), 10.f, 600.f);
	ClockHalfWidth = FMath::Clamp(D->Tune(TEXT("ClockHandWidth"), 120.f) * 0.5f, 20.f, 300.f);
	ClockDamage = FMath::Max(0.f, D->Tune(TEXT("ClockHandDamage"), 22.f));
	ClockEnemyDps = FMath::Max(0.f, D->Tune(TEXT("ClockHandEnemyDps"), 600.f));
}

float ASQArena::MacroRaw(float X, float Y) const
{
	static const FName ShapeTerraces(TEXT("Terraces"));
	static const FName ShapeCauseways(TEXT("Causeways"));
	static const FName ShapeClimb(TEXT("Climb"));
	const float Noise = FMath::PerlinNoise2D(FVector2D(X * 0.00011f + MacroNoiseOff, Y * 0.00011f - MacroNoiseOff));
	if (MapShape == ShapeTerraces)
	{
		// Graveyard: two or three broad terraces that the lanes climb (raised burial grounds), never below the base level
		float H = 0.f;
		for (int32 Step = 0; Step < MacroSteps; ++Step) H += MacroTier * Smooth01((Noise - (0.02f + 0.24f * Step)) / 0.15f);
		return H;
	}
	if (MapShape == ShapeCauseways)
	{
		// Harbour: low tidal flats; the hub, every district, the road and the lanes are raised quays and boardwalks,
		// with a few islets of high ground out on the flats
		float Up = 1.f - Smooth01((FMath::Sqrt(X * X + Y * Y) - (HubRadius + 250.f)) / 420.f);
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind == ESQZoneKind::Hub) continue;
			Up = FMath::Max(Up, 1.f - Smooth01((static_cast<float>(FVector2D::Distance(Zone.Centre, FVector2D(X, Y))) - (Zone.Radius + 250.f)) / 420.f));
		}
		Up = FMath::Max(Up, 1.f - Smooth01((DistToRoad(X, Y) - (RoadHalfWidth + 120.f)) / 380.f));
		if (Lanes.Num() > 0) Up = FMath::Max(Up, 1.f - Smooth01((DistToLane(X, Y) - (LaneHalfWidth + 120.f)) / 380.f));
		Up = FMath::Max(Up, Smooth01((Noise - 0.30f) / 0.10f));
		return MacroTier * Up;
	}
	if (MapShape == ShapeClimb)
	{
		// Clocktower: a staircase hillside. The ground steps up from the far side of the map, through the hub, to the
		// boss arena at the top: the lantern trail is a climb.
		const FVector2D Summit = MapZones.IsValidIndex(BossZone) ? MapZones[BossZone].Centre : FVector2D(Half * 0.6f, 0.f);
		const FVector2D Axis = Summit.GetSafeNormal();
		const float U = static_cast<float>(FVector2D::DotProduct(FVector2D(X, Y), Axis)) / FMath::Max(1.f, Half);
		float H = 0.f;
		for (int32 Step = 0; Step < MacroSteps; ++Step)
		{
			const float Edge = MacroSteps > 1 ? FMath::Lerp(-0.42f, 0.44f, static_cast<float>(Step) / static_cast<float>(MacroSteps - 1)) : 0.f;
			H += MacroTier * Smooth01((U - Edge) / 0.05f);
		}
		return H;
	}
	return 0.f;
}

float ASQArena::MacroHeight(float X, float Y) const
{
	if (MapShape.IsNone()) return 0.f;
	const float Raw = MacroRaw(X, Y);
	// Mission floors stay dead level: each takes the height at its own centre
	float BestW = 1.f - Smooth01((FMath::Sqrt(X * X + Y * Y) - HubRadius) / 500.f);
	float BestH = BestW > 0.f ? MacroRaw(0.f, 0.f) : 0.f;
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind == ESQZoneKind::Hub) continue;
		const float W = 1.f - Smooth01((static_cast<float>(FVector2D::Distance(Zone.Centre, FVector2D(X, Y))) - Zone.Radius) / 500.f);
		if (W > BestW)
		{
			BestW = W;
			BestH = MacroRaw(static_cast<float>(Zone.Centre.X), static_cast<float>(Zone.Centre.Y));
		}
	}
	return FMath::Lerp(Raw, BestH, BestW);
}

float ASQArena::TideLevel() const
{
	if (GetGimmick() != ESQGimmick::Tide || !IsGimmickActive()) return -100000.f;
	// Low 40% of the cycle, rising 15%, high 30%, falling 15%
	const float Phase = FMath::Fmod(Now(), TideCycle) / TideCycle;
	float Alpha = 0.f;
	if (Phase >= 0.40f && Phase < 0.55f) Alpha = Smooth01((Phase - 0.40f) / 0.15f);
	else if (Phase >= 0.55f && Phase < 0.85f) Alpha = 1.f;
	else if (Phase >= 0.85f) Alpha = 1.f - Smooth01((Phase - 0.85f) / 0.15f);
	return FMath::Lerp(TideLowZ, TideHighZ, Alpha);
}

bool ASQArena::IsFlooded(const FVector2D& P) const
{
	const float Level = TideLevel();
	return Level > -1000.f && GroundZ(static_cast<float>(P.X), static_cast<float>(P.Y)) < Level - 25.f;
}

int32 ASQArena::ClockHandCount() const
{
	return IsGimmickActive() ? ClockHands : 0;
}

float ASQArena::ClockHandAngle(int32 Hand) const
{
	// Hand 0 (the minute hand) sweeps clockwise once per ClockHandPeriod; the others are slower and run the other way
	const float Speed = 2.f * PI / ClockPeriod;
	if (Hand == 0) return -Now() * Speed;
	return Now() * Speed * (Hand == 1 ? 0.42f : 0.27f) + static_cast<float>(Hand) * 2.1f;
}

float ASQArena::ClockHandReach(int32 Hand) const
{
	return Half * (Hand == 0 ? 1.45f : (Hand == 1 ? 0.72f : 0.5f));
}

bool ASQArena::IsUnderClockHand(const FVector2D& P, float Pad) const
{
	const int32 Hands = ClockHandCount();
	const float Inner = HubRadius * 0.55f; // the hands start outside the lantern square's centre
	for (int32 Hand = 0; Hand < Hands; ++Hand)
	{
		const float Ang = ClockHandAngle(Hand);
		const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
		const float Along = static_cast<float>(FVector2D::DotProduct(P, Dir));
		if (Along < Inner || Along > ClockHandReach(Hand)) continue;
		if (FMath::Abs(static_cast<float>(FVector2D::CrossProduct(Dir, P))) < ClockHalfWidth + Pad) return true;
	}
	return false;
}

void ASQArena::ServerTickTwists(float DeltaSeconds)
{
	if (ClockHandCount() <= 0 || ClockDamage <= 0.f) return;
	ClockHitTimer -= DeltaSeconds;
	if (ClockHitTimer > 0.f) return;
	ClockHitTimer = 0.05f;
	const float T = Now();
	for (TActorIterator<ASQCharacter> It(GetWorld()); It; ++It)
	{
		ASQCharacter* Reaper = *It;
		// Jump the hand: anyone in the air is clear. Bots can't time the jump, so the hands pass through them.
		if (!Reaper || Reaper->bDowned || !Reaper->IsPlayerControlled()) continue;
		const UCharacterMovementComponent* Move = Reaper->GetCharacterMovement();
		if (Move && Move->IsFalling()) continue;
		if (!IsUnderClockHand(Reaper->Pos2D(), 30.f)) continue;
		float& Last = ClockLastHit.FindOrAdd(Reaper);
		if (T - Last < 1.2f) continue;
		Last = T;
		Reaper->ApplyHit(ClockDamage);
	}
}

float ASQArena::TideX(bool& bActive, bool& bWarning) const
{
	const float Cycle = FMath::Fmod(Now(), TidePeriod);
	bWarning = Cycle >= TideWarnStart && Cycle < TideActiveStart;
	bActive = Cycle >= TideActiveStart;
	const float Alpha = bActive ? (Cycle - TideActiveStart) / (TidePeriod - TideActiveStart) : 0.f;
	return FMath::Lerp(-Half - TideHalfWidth, Half + TideHalfWidth, Alpha);
}

FVector2D ASQArena::SpotlightPos(int32 Index) const
{
	const float T = Now();
	return FVector2D(FMath::Sin(T * 0.13f + Index * 2.1f), FMath::Cos(T * 0.17f + Index * 1.3f)) * Half * 0.6f;
}

bool ASQArena::IsOnIce(const FVector2D& P) const
{
	if (GetGimmick() != ESQGimmick::Ice) return false;
	for (int32 i = 0; i < IcePos.Num(); ++i)
	{
		if (FVector2D::DistSquared(P, IcePos[i]) <= IceRadius[i] * IceRadius[i]) return true;
	}
	return false;
}

bool ASQArena::IsTimeStopped() const
{
	return GetGimmick() == ESQGimmick::TimeStop && IsGimmickActive() && FMath::Fmod(Now(), StopPeriod) >= StopStart;
}

float ASQArena::PlayerSpeedMult(const FVector2D& P) const
{
	// Hazards (open graves) stack with the biome gimmick
	const float HazardMult = Hazards ? Hazards->PlayerSpeedMult(P) : 1.f;
	if (!IsGimmickActive()) return HazardMult;
	switch (GetGimmick())
	{
	case ESQGimmick::Tide: return (IsFlooded(P) ? TideSlow : 1.f) * HazardMult;
	case ESQGimmick::Ice: return (IsOnIce(P) ? 1.25f : 1.f) * HazardMult;
	case ESQGimmick::TimeStop: return (IsTimeStopped() ? 1.2f : 1.f) * HazardMult;
	default: return HazardMult;
	}
}

float ASQArena::EnemySpeedMult(const FVector2D& P) const
{
	const float HazardMult = Hazards ? Hazards->EnemySpeedMult(P) : 1.f;
	if (!IsGimmickActive()) return HazardMult;
	switch (GetGimmick())
	{
	case ESQGimmick::Ice: return (IsOnIce(P) ? 0.55f : 1.f) * HazardMult;
	case ESQGimmick::TimeStop: return IsTimeStopped() ? 0.f : HazardMult;
	case ESQGimmick::Tide: return (IsFlooded(P) ? 0.5f : 1.f) * HazardMult;
	default: return HazardMult;
	}
}

float ASQArena::EnemyDamagePerSecond(const FVector2D& P) const
{
	if (!IsGimmickActive()) return 0.f;
	if (GetGimmick() == ESQGimmick::Tide) return IsFlooded(P) ? TideEnemyDps : 0.f;
	// Clocktower: the hands mow the horde
	if (ClockHands > 0 && IsUnderClockHand(P)) return ClockEnemyDps;
	return 0.f;
}

float ASQArena::SoulMult(const FVector2D& P) const
{
	if (!IsGimmickActive() || GetGimmick() != ESQGimmick::Spotlight) return 1.f;
	for (int32 i = 0; i < 3; ++i)
	{
		if (FVector2D::DistSquared(P, SpotlightPos(i)) <= SpotRadius * SpotRadius) return 2.f;
	}
	return 1.f;
}

FString ASQArena::GimmickStatus() const
{
	const FString GimmickLine = GimmickStatusOnly();
	const FString HazardLine = Hazards ? Hazards->StatusText() : FString();
	if (GimmickLine.IsEmpty()) return HazardLine;
	if (HazardLine.IsEmpty()) return GimmickLine;
	return GimmickLine + TEXT("   |   ") + HazardLine;
}

FString ASQArena::GimmickStatusOnly() const
{
	if (!IsGimmickActive()) return FString();
	switch (GetGimmick())
	{
	case ESQGimmick::Tide:
	{
		const float Phase = FMath::Fmod(Now(), TideCycle) / TideCycle;
		if (Phase < 0.28f) return TEXT("LOW TIDE: the flats are open");
		if (Phase < 0.40f) return FString::Printf(TEXT("The tide turns in %.0f... get to the boardwalks"), (0.40f - Phase) * TideCycle);
		if (Phase < 0.55f) return TEXT("TIDE RISING! The flats slow you and drown the horde");
		if (Phase < 0.85f) return TEXT("HIGH TIDE: hold the boardwalks");
		return TEXT("The tide is going out");
	}
	case ESQGimmick::Spotlight: return TEXT("Kills inside the spotlights drop DOUBLE souls");
	case ESQGimmick::Ice: return FString();
	case ESQGimmick::TimeStop:
	{
		const float C = FMath::Fmod(Now(), StopPeriod);
		if (C < StopWarnStart && ClockHands > 0) return TEXT("JUMP the clock hands: they flatten the horde too");
		if (C >= StopStart) return TEXT("TIME STOPPED - the horde is frozen!");
		if (C >= StopWarnStart) return FString::Printf(TEXT("Time stop in %.0f..."), StopStart - C);
		return FString();
	}
	default: return FString();
	}
}


// ---------------------------------------------------------------- visuals

void ASQArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Server (incl. dedicated): out-of-bounds / fell-through safety net, a few times a second
	if (HasAuthority())
	{
		SafetyNetTimer -= DeltaSeconds;
		if (SafetyNetTimer <= 0.f)
		{
			SafetyNetTimer = 0.25f;
			ServerSafetyNet();
		}
		ServerWatchPhase();
		ServerTickTwists(DeltaSeconds);
	}

	if (GetNetMode() != NM_DedicatedServer) TickAmbience();
	if (GetNetMode() == NM_DedicatedServer || !ZoneISM || !WarningISM) return;

	TArray<FTransform>& Zones = ScratchZones;
	TArray<FTransform>& Warnings = ScratchWarnings;
	Zones.Reset();
	Warnings.Reset();
	const ESQGimmick Gimmick = GetGimmick();
	const bool bActive = IsGimmickActive();

	// Ice patches are drawn by the terrain overlay section (BuildIceOverlay), so they follow the ground.
	if (bActive && Gimmick == ESQGimmick::Spotlight)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D P = SpotlightPos(i);
			const float SX = static_cast<float>(P.X);
			const float SY = static_cast<float>(P.Y);
			const float S = SpotRadius / 50.f;
			// Tilt the disc to the ground plane under it and lift it over the bumps
			const float ZC = GroundZ(SX, SY);
			const float ZXP = GroundZ(SX + SpotRadius, SY);
			const float ZXM = GroundZ(SX - SpotRadius, SY);
			const float ZYP = GroundZ(SX, SY + SpotRadius);
			const float ZYM = GroundZ(SX, SY - SpotRadius);
			const FVector SpotN = FVector(-(ZXP - ZXM) / (2.f * SpotRadius), -(ZYP - ZYM) / (2.f * SpotRadius), 1.f).GetSafeNormal();
			const float SZ = FMath::Max(ZC, 0.25f * (ZXP + ZXM + ZYP + ZYM)) + 6.f;
			Zones.Add(FTransform(FRotationMatrix::MakeFromZ(SpotN).Rotator(), FVector(SX, SY, SZ), FVector(S, S, 0.01f)));
		}
	}

	// Drowned Harbour: one sheet of water that rises and falls with the tide (under the ground at low tide)
	if (TideWater)
	{
		const float Level = Gimmick == ESQGimmick::Tide && bActive ? TideLevel() : TerrainMinZ - 400.f;
		const float Ripple = 4.f * FMath::Sin(Now() * 1.7f);
		TideWater->SetRelativeLocation(FVector(0.f, 0.f, FMath::Max(Level, TerrainMinZ - 400.f) - 50.f + Ripple));
	}
	// Clocktower: the hands, laid along the ground in short segments
	if (ClockISM)
	{
		ScratchHands.Reset();
		const int32 Hands = ClockHandCount();
		const float SegLen = 220.f;
		const float Inner = HubRadius * 0.55f;
		for (int32 Hand = 0; Hand < Hands; ++Hand)
		{
			const float Ang = ClockHandAngle(Hand);
			const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
			const float YawDeg = FMath::RadiansToDegrees(Ang);
			const float Reach = ClockHandReach(Hand);
			for (float Along = Inner + SegLen * 0.5f; Along < Reach; Along += SegLen)
			{
				const float SX = static_cast<float>(Dir.X) * Along;
				const float SY = static_cast<float>(Dir.Y) * Along;
				if (FMath::Abs(SX) > Half || FMath::Abs(SY) > Half) break;
				const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, GroundNormal(SX, SY));
				ScratchHands.Add(FTransform(Align * FQuat(FRotator(0.f, YawDeg, 0.f)), FVector(SX, SY, GroundZ(SX, SY) + 28.f),
					FVector(SegLen / 100.f * 1.04f, ClockHalfWidth * 2.f / 100.f, 0.36f)));
			}
		}
		if (ScratchHands.Num() != ClockISM->GetInstanceCount())
		{
			ClockISM->ClearInstances();
			if (ScratchHands.Num() > 0) ClockISM->AddInstances(ScratchHands, false, false);
		}
		else if (ScratchHands.Num() > 0)
		{
			ClockISM->BatchUpdateInstancesTransforms(0, ScratchHands, false, true, false);
		}
	}

	// Nothing to draw two frames running: leave the (already empty) ISMs alone instead of dirtying them every frame
	const bool bAnyZones = Zones.Num() > 0 || Warnings.Num() > 0;
	if (!bAnyZones && !bZonesShown) return;
	bZonesShown = bAnyZones;
	ZoneISM->ClearInstances();
	if (Zones.Num() > 0) ZoneISM->AddInstances(Zones, false, true);
	WarningISM->ClearInstances();
	if (Warnings.Num() > 0) WarningISM->AddInstances(Warnings, false, true);
}

// ---------------------------------------------------------------- safety net / spot validation

void ASQArena::TickAmbience()
{
	const UWorld* World = GetWorld();
	if (!World) return;
	const float T = World->GetTimeSeconds();

	// Lamp flicker: two slow sines per lamp, a few percent (alive, never strobing)
	if (LampBaseIntensity > 0.f)
	{
		for (int32 LampIdx = 0; LampIdx < MapLights.Num(); ++LampIdx)
		{
			if (UPointLightComponent* Lamp = MapLights[LampIdx])
			{
				const float Phase = static_cast<float>(LampIdx) * 1.37f;
				Lamp->SetIntensity(LampBaseIntensity * (0.93f + 0.05f * FMath::Sin(T * 6.1f + Phase) + 0.04f * FMath::Sin(T * 2.3f + Phase * 2.f)));
			}
		}
	}

	// Fireflies: world-anchored spots on a 30 m tile that wraps round the local reaper, so there are always some in view
	if (!MoteISM || ScratchMotes.Num() == 0 || GridN <= 0) return;
	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* ViewPawn = PC ? PC->GetPawn() : nullptr;
	if (!ViewPawn) return;
	const FVector Eye = ViewPawn->GetActorLocation();
	const float Tile = 3000.f;
	for (int32 MoteIdx = 0; MoteIdx < ScratchMotes.Num(); ++MoteIdx)
	{
		const float Seed1 = FMath::Frac(static_cast<float>(MoteIdx) * 0.61803f);
		const float Seed2 = FMath::Frac(static_cast<float>(MoteIdx) * 0.41421f + 0.37f);
		const float HomeX = Seed1 * Tile;
		const float HomeY = Seed2 * Tile;
		float MX = HomeX + Tile * FMath::RoundToFloat((static_cast<float>(Eye.X) - HomeX) / Tile) + 140.f * FMath::Sin(T * 0.31f + MoteIdx * 1.9f);
		float MY = HomeY + Tile * FMath::RoundToFloat((static_cast<float>(Eye.Y) - HomeY) / Tile) + 140.f * FMath::Cos(T * 0.27f + MoteIdx * 2.3f);
		MX = FMath::Clamp(MX, -Half + 50.f, Half - 50.f);
		MY = FMath::Clamp(MY, -Half + 50.f, Half - 50.f);
		const float MZ = GroundZ(MX, MY) + 70.f + 45.f * FMath::Sin(T * 0.7f + MoteIdx * 2.1f);
		// Each one blinks on its own slow beat
		const float Blink = FMath::Max(0.f, FMath::Sin(T * 1.3f + MoteIdx * 0.83f));
		ScratchMotes[MoteIdx] = FTransform(FRotator::ZeroRotator, FVector(MX, MY, MZ), FVector(0.012f + 0.038f * Blink));
	}
	MoteISM->BatchUpdateInstancesTransforms(0, ScratchMotes, true, true, false);
}

void ASQArena::ServerSafetyNet()
{
	UWorld* World = GetWorld();
	if (!World || GridN <= 0) return;
	// The walls' inner faces are at +/-Half (100 cm thick); a capsule centre can never legitimately be past Half - radius,
	// so a centre past Half + 50 is embedded in a wall (e.g. dropped there without a sweep) or already outside
	const float OutLimit = Half + 50.f;
	const float FloorLimit = TerrainMinZ - 500.f;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Char = *It;
		if (!IsValid(Char)) continue;
		UCharacterMovementComponent* CharMove = Char->GetCharacterMovement();
		// Carried skulls (movement disabled) ride on their carrier, who is checked instead
		if (CharMove && CharMove->MovementMode == MOVE_None) continue;
		const FVector Loc = Char->GetActorLocation();
		const float LX = static_cast<float>(Loc.X);
		const float LY = static_cast<float>(Loc.Y);
		const float LZ = static_cast<float>(Loc.Z);
		const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
		const float HalfH = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
		const bool bBadNumber = !FMath::IsFinite(LX) || !FMath::IsFinite(LY) || !FMath::IsFinite(LZ);
		const bool bOutside = bBadNumber || FMath::Abs(LX) > OutLimit || FMath::Abs(LY) > OutLimit || LZ < FloorLimit;
		// Whole capsule well under the walkable surface = fell through a seam (or a rebuild raised the ground)
		const bool bUnder = !bOutside && LZ + HalfH < GroundZ(LX, LY) - 150.f;
		if (!bOutside && !bUnder) continue;

		FVector Dest;
		if (bOutside)
		{
			// Back to the hub (flat, wall-free), spread round the centre so several rescues never stack
			const FVector Start = GetPlayerStart(static_cast<int32>(Char->GetUniqueID() % 8u));
			Dest = FVector(Start.X, Start.Y, Start.Z + HalfH + 30.f);
		}
		else
		{
			Dest = FVector(LX, LY, GroundZ(LX, LY) + HalfH + 30.f);
		}
		UE_LOG(LogSQ, Warning, TEXT("Arena safety net: %s at (%.0f, %.0f, %.0f) was %s - moved to (%.0f, %.0f, %.0f)"),
			*Char->GetName(), LX, LY, LZ, bOutside ? TEXT("out of bounds") : TEXT("under the ground"), Dest.X, Dest.Y, Dest.Z);
		if (CharMove) CharMove->StopMovementImmediately();
		// TeleportTo finds a free spot if Dest overlaps a prop / another character
		if (!Char->TeleportTo(Dest, Char->GetActorRotation(), false, false))
		{
			Char->SetActorLocation(Dest, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}

void ASQArena::ServerRePlaceCharacters()
{
	UWorld* World = GetWorld();
	if (!World || GridN <= 0) return;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Char = *It;
		if (!IsValid(Char)) continue;
		UCharacterMovementComponent* CharMove = Char->GetCharacterMovement();
		if (CharMove && CharMove->MovementMode == MOVE_None) continue;
		const FVector Loc = Char->GetActorLocation();
		const float LX = static_cast<float>(Loc.X);
		const float LY = static_cast<float>(Loc.Y);
		const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
		const float HalfH = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
		const float CapR = Capsule ? Capsule->GetScaledCapsuleRadius() : 40.f;
		const float StandZ = GroundZ(LX, LY) + HalfH + 10.f;
		const bool bMove = static_cast<float>(Loc.Z) < StandZ - 5.f || IsWallAt(LX, LY, CapR);
		if (!bMove) continue;
		const FVector Dest(LX, LY, FMath::Max(static_cast<float>(Loc.Z), StandZ));
		if (CharMove) CharMove->StopMovementImmediately();
		if (IsWallAt(LX, LY, CapR) || !Char->TeleportTo(Dest, Char->GetActorRotation(), false, false))
		{
			// Inside a wall / crypt: a hub start spot is always clear
			const FVector Start = GetPlayerStart(static_cast<int32>(Char->GetUniqueID() % 8u));
			Char->SetActorLocation(FVector(Start.X, Start.Y, Start.Z + HalfH + 30.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}

bool ASQArena::IsPropFootprintFree(float X, float Y, float R) const
{
	// Capsule diameter (80) + margin between footprints, and between a footprint and the wall
	const float Gap = 110.f;
	if (FMath::Abs(X) + R > Half - Gap || FMath::Abs(Y) + R > Half - Gap) return false;
	return !IsWallAt(X, Y, R + Gap);
}

bool ASQArena::IsWallAt(float X, float Y, float Radius) const
{
	for (const FSQMapWall& W : MapWalls)
	{
		const float Dist = DistToSegment2D(X, Y, static_cast<float>(W.A.X), static_cast<float>(W.A.Y), static_cast<float>(W.B.X), static_cast<float>(W.B.Y));
		if (Dist < W.HalfThick + Radius) return true;
	}
	for (const FVector& Spot : BlockerSpots)
	{
		const float Need = static_cast<float>(Spot.Z) + Radius;
		if (FMath::Square(static_cast<float>(Spot.X) - X) + FMath::Square(static_cast<float>(Spot.Y) - Y) < Need * Need) return true;
	}
	return false;
}

float ASQArena::DistToRoad(float X, float Y) const
{
	return ArenaMapDistToPolyline(RoadPts, X, Y);
}

bool ASQArena::IsReservedSpot(float X, float Y, float Radius) const
{
	if (X * X + Y * Y < FMath::Square(HubRadius + Radius)) return true;
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind == ESQZoneKind::Hub) continue;
		const float Need = Zone.Radius + Radius;
		if (FVector2D::DistSquared(Zone.Centre, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return true;
	}
	if (DistToRoad(X, Y) < RoadHalfWidth + Radius) return true;
	if (DistToLane(X, Y) < LaneHalfWidth + Radius) return true;
	for (const FVector2D& Pad : SpawnerSpots)
	{
		const float Need = ArenaMapPadClear + Radius;
		if (FVector2D::DistSquared(Pad, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return true;
	}
	return false;
}

bool ASQArena::IsLanternSpotClear(float X, float Y, float Radius) const
{
	if (GridN <= 0) return true;
	// Well inside the walls (the ring must never touch them)
	if (FMath::Abs(X) > Half - Radius - 400.f || FMath::Abs(Y) > Half - Radius - 400.f) return false;
	// Flat: plain or tier top - never a ramp or a water edge
	if (GroundRoughness(X, Y, Radius + 150.f) > 45.f) return false;
	if (GroundZ(X, Y) < -15.f) return false;
	// Clear of layout walls, blocking props and lamp blockers (the ring and a walking lane round it)
	if (IsWallAt(X, Y, Radius + 100.f)) return false;
	// Off the escort road (the cart runs there) and the spawner pads
	if (DistToRoad(X, Y) < RoadHalfWidth + Radius) return false;
	for (const FVector2D& Pad : SpawnerSpots)
	{
		const float Need = ArenaMapPadClear + Radius;
		if (FVector2D::DistSquared(Pad, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return false;
	}
	// Not on (or overlapping) an ice patch
	if (GetGimmick() == ESQGimmick::Ice)
	{
		for (int32 i = 0; i < IcePos.Num() && i < IceRadius.Num(); ++i)
		{
			const float Need = IceRadius[i] + Radius;
			if (FVector2D::DistSquared(FVector2D(X, Y), IcePos[i]) < static_cast<double>(Need) * Need) return false;
		}
	}
	// Not on a hazard (open grave, gust vent, bell) and clear of the shrines / pots (server; empty on clients)
	if (Hazards && Hazards->IsSpotBlocked(X, Y, Radius)) return false;
	for (const FVector& Taken : InteractSpots)
	{
		const float Need = static_cast<float>(Taken.Z) + Radius + 150.f;
		if (FMath::Square(static_cast<float>(Taken.X) - X) + FMath::Square(static_cast<float>(Taken.Y) - Y) < Need * Need) return false;
	}
	return true;
}

FVector ASQArena::FindLanternSpot(float X, float Y, float Radius) const
{
	if (IsLanternSpotClear(X, Y, Radius)) return FVector(X, Y, GroundZ(X, Y));
	// Rings outwards from the wanted spot, staying in the middle 70% of the arena
	const float Keep = Half * 0.7f;
	for (int32 RingIdx = 1; RingIdx <= 24; ++RingIdx)
	{
		const float RingR = RingIdx * 250.f;
		const int32 Steps = 8 + RingIdx * 2;
		for (int32 k = 0; k < Steps; ++k)
		{
			const float Ang = k * 2.f * PI / Steps;
			const float CX = X + FMath::Cos(Ang) * RingR;
			const float CY = Y + FMath::Sin(Ang) * RingR;
			if (FMath::Abs(CX) > Keep || FMath::Abs(CY) > Keep) continue;
			if (IsLanternSpotClear(CX, CY, Radius)) return FVector(CX, CY, GroundZ(CX, CY));
		}
	}
	// The hub is flat and wall-free by construction
	return FVector(0.f, 0.f, GroundZ(0.f, 0.f));
}

void ASQArena::GetPropBlockers(TArray<UPrimitiveComponent*>& Out) const
{
	for (const TObjectPtr<UPrimitiveComponent>& Comp : PropBlockers)
	{
		if (Comp) Out.Add(Comp.Get());
	}
}

void ASQArena::GetFadeables(TArray<UPrimitiveComponent*>& Out) const
{
	for (const TObjectPtr<UPrimitiveComponent>& Comp : Fadeables)
	{
		if (Comp) Out.Add(Comp.Get());
	}
}

bool ASQArena::IsOnParkLine(float /*X*/, float /*Y*/, float /*Radius*/) const
{
	return false;
}

void ASQArena::GetParkColliders(TArray<UPrimitiveComponent*>& /*Out*/) const
{
}

float ASQArena::DistToNearestJumpPad(float /*X*/, float /*Y*/) const
{
	return TNumericLimits<float>::Max();
}

// ---------------------------------------------------------------- mission API (top-down maps)

bool ASQArena::IsObjectiveSpotClear(float X, float Y, float Radius) const
{
	if (GridN <= 0) return false;
	if (FMath::Abs(X) > Half - Radius - 300.f || FMath::Abs(Y) > Half - Radius - 300.f) return false;
	if (GroundZ(X, Y) < -15.f) return false;
	if (GroundRoughness(X, Y, FMath::Max(100.f, Radius)) > 40.f) return false;
	if (IsWallAt(X, Y, Radius + 60.f)) return false;
	if (Hazards && Hazards->IsSpotBlocked(X, Y, Radius)) return false;
	if (IsOnIce(FVector2D(X, Y))) return false;
	return true;
}

bool ASQArena::NudgeToClear(float X, float Y, float Radius, float MaxDist, FVector2D& Out) const
{
	if (IsObjectiveSpotClear(X, Y, Radius))
	{
		Out = FVector2D(X, Y);
		return true;
	}
	const int32 Rings = FMath::Max(1, FMath::CeilToInt(MaxDist / 150.f));
	for (int32 RingIdx = 1; RingIdx <= Rings; ++RingIdx)
	{
		const float RingR = static_cast<float>(RingIdx) * 150.f;
		const int32 Steps = 10 + RingIdx * 4;
		for (int32 k = 0; k < Steps; ++k)
		{
			const float Ang = static_cast<float>(k) * 2.f * PI / static_cast<float>(Steps);
			const float CX = X + FMath::Cos(Ang) * RingR;
			const float CY = Y + FMath::Sin(Ang) * RingR;
			if (!IsObjectiveSpotClear(CX, CY, Radius)) continue;
			Out = FVector2D(CX, CY);
			return true;
		}
	}
	return false;
}

void ASQArena::BuildObjectivePools()
{
	if (GridN <= 0) return;
	FRandomStream PoolRand(MapSeed ^ 0x51ED270B);

	// Spawner pads: planned near the edges, nudged onto clear ground, then spread out (any first N are far apart)
	TArray<FVector2D> Pads;
	for (const FVector2D& Planned : SpawnerSpots)
	{
		FVector2D Ok;
		if (NudgeToClear(static_cast<float>(Planned.X), static_cast<float>(Planned.Y), 300.f, 900.f, Ok)) Pads.Add(Ok);
	}
	const int32 PadFirst = PoolRand.RandRange(0, FMath::Max(0, Pads.Num() - 1));
	ArenaMapSpreadOrder(Pads, PadFirst);
	SpawnerSpots = Pads;

	// Collect items: a jittered grid over the whole map, off the road / hub / boss floor, spread out
	const USQData* D = USQData::Get(this);
	const int32 WantCollect = D ? FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapCollectSpots"), 24.f)), 4, 64) : 24;
	const float CollectStep = 1300.f;
	const FSQMapZone* BossArena = MapZones.IsValidIndex(BossZone) ? &MapZones[BossZone] : nullptr;
	TArray<FVector2D> Scatter;
	for (float GX = -Half + 900.f; GX <= Half - 900.f; GX += CollectStep)
	{
		for (float GY = -Half + 900.f; GY <= Half - 900.f; GY += CollectStep)
		{
			const float JX = PoolRand.FRandRange(-420.f, 420.f);
			const float JY = PoolRand.FRandRange(-420.f, 420.f);
			const float CX = GX + JX;
			const float CY = GY + JY;
			if (CX * CX + CY * CY < FMath::Square(HubRadius * 0.6f)) continue;
			if (BossArena && FVector2D::Distance(BossArena->Centre, FVector2D(CX, CY)) < BossArena->Radius * 0.8f) continue;
			if (PathMask(CX, CY) > 0.05f) continue;
			if (!IsObjectiveSpotClear(CX, CY, 150.f)) continue;
			Scatter.Add(FVector2D(CX, CY));
		}
	}
	const int32 CollectFirst = PoolRand.RandRange(0, FMath::Max(0, Scatter.Num() - 1));
	ArenaMapSpreadOrder(Scatter, CollectFirst);
	if (Scatter.Num() > WantCollect) Scatter.SetNum(WantCollect);
	CollectSpots = Scatter;

	// Elite spots: open ground on the mid ring, outside the zones
	const float EliteRing = (HubRadius + KiteRing + Half * 0.85f) * 0.5f;
	const float EliteOffset = PoolRand.FRandRange(0.f, 2.f * PI);
	TArray<FVector2D> Elites;
	for (int32 k = 0; k < 16; ++k)
	{
		const float Ang = EliteOffset + static_cast<float>(k) * 2.f * PI / 16.f;
		const FVector2D Want = ArenaMapDir(Ang) * EliteRing;
		FVector2D Ok;
		if (!NudgeToClear(static_cast<float>(Want.X), static_cast<float>(Want.Y), 450.f, 600.f, Ok)) continue;
		bool bInZone = false;
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind != ESQZoneKind::Hub && FVector2D::Distance(Zone.Centre, Ok) < Zone.Radius + 300.f) bInZone = true;
		}
		if (!bInZone) Elites.Add(Ok);
	}
	ArenaMapSpreadOrder(Elites, 0);
	EliteSpots = Elites;
}

bool ASQArena::GetObjectiveSpots(FName ObjectiveType, int32 Count, TArray<FVector>& Out) const
{
	Out.Reset();
	if (Count <= 0) return true;
	if (GridN <= 0) return false;
	auto AddSpot = [this, &Out](const FVector2D& P)
	{
		const float PX = static_cast<float>(P.X);
		const float PY = static_cast<float>(P.Y);
		Out.Add(FVector(PX, PY, GroundZ(PX, PY)));
	};
	// A zone centre, nudged off anything that landed on it (hazards, a prop)
	auto AddZoneCentre = [this, &AddSpot](int32 ZoneIdx, float Radius)
	{
		if (!MapZones.IsValidIndex(ZoneIdx)) return;
		const FSQMapZone& Zone = MapZones[ZoneIdx];
		FVector2D Ok = Zone.Centre;
		NudgeToClear(static_cast<float>(Zone.Centre.X), static_cast<float>(Zone.Centre.Y), Radius, Zone.Radius * 0.5f, Ok);
		AddSpot(Ok);
	};

	if (ObjectiveType == FName(TEXT("DestroySpawners")))
	{
		for (int32 i = 0; i < SpawnerSpots.Num() && Out.Num() < Count; ++i) AddSpot(SpawnerSpots[i]);
	}
	else if (ObjectiveType == FName(TEXT("DefendPoint")))
	{
		for (int32 i = 0; i < LandmarkOrder.Num() && Out.Num() < Count; ++i) AddZoneCentre(LandmarkOrder[i], 300.f);
	}
	else if (ObjectiveType == FName(TEXT("Escort")))
	{
		// Road start, evenly along the road by length, road end
		if (RoadPts.Num() >= 2)
		{
			TArray<float> Arc;
			Arc.Add(0.f);
			for (int32 i = 1; i < RoadPts.Num(); ++i) Arc.Add(Arc.Last() + static_cast<float>(FVector2D::Distance(RoadPts[i - 1], RoadPts[i])));
			const float Total = Arc.Last();
			for (int32 k = 0; k < Count; ++k)
			{
				const float Want = Count > 1 ? Total * static_cast<float>(k) / static_cast<float>(Count - 1) : 0.f;
				int32 Seg = 1;
				while (Seg < Arc.Num() - 1 && Arc[Seg] < Want) ++Seg;
				const float SegLen = FMath::Max(1.f, Arc[Seg] - Arc[Seg - 1]);
				const float T = FMath::Clamp((Want - Arc[Seg - 1]) / SegLen, 0.f, 1.f);
				AddSpot(FMath::Lerp(RoadPts[Seg - 1], RoadPts[Seg], static_cast<double>(T)));
			}
		}
	}
	else if (ObjectiveType == FName(TEXT("CollectItems")))
	{
		for (int32 i = 0; i < CollectSpots.Num() && Out.Num() < Count; ++i) AddSpot(CollectSpots[i]);
	}
	else if (ObjectiveType == FName(TEXT("BankSouls")))
	{
		// The lantern: the hub first, then the landmarks (lantern-spot rules: flat, clear, off the road)
		const float LanternR = 350.f;
		const FVector HubSpot = FindLanternSpot(0.f, 0.f, LanternR);
		Out.Add(HubSpot);
		for (int32 i = 0; i < LandmarkOrder.Num() && Out.Num() < Count; ++i)
		{
			const FSQMapZone& Zone = MapZones[LandmarkOrder[i]];
			const float ZX = static_cast<float>(Zone.Centre.X);
			const float ZY = static_cast<float>(Zone.Centre.Y);
			if (IsLanternSpotClear(ZX, ZY, LanternR)) Out.Add(FVector(ZX, ZY, GroundZ(ZX, ZY)));
		}
	}
	else if (ObjectiveType == FName(TEXT("SurviveWave")))
	{
		// The most open arenas: the hub (with its kiting ring), then the landmarks
		AddSpot(FVector2D::ZeroVector);
		for (int32 i = 0; i < LandmarkOrder.Num() && Out.Num() < Count; ++i) AddZoneCentre(LandmarkOrder[i], 400.f);
	}
	else if (ObjectiveType == FName(TEXT("KillElite")))
	{
		for (int32 i = 0; i < EliteOrder.Num() && Out.Num() < Count; ++i) AddZoneCentre(EliteOrder[i], 400.f);
		for (int32 i = 0; i < EliteSpots.Num() && Out.Num() < Count; ++i) AddSpot(EliteSpots[i]);
	}
	else if (ObjectiveType == FName(TEXT("Boss")))
	{
		if (MapZones.IsValidIndex(BossZone))
		{
			const FSQMapZone& Arena = MapZones[BossZone];
			AddZoneCentre(BossZone, 600.f);
			// Then points round the boss arena (pylons, adds), starting on the side facing the hub
			const float InAng = FMath::Atan2(-static_cast<float>(Arena.Centre.Y), -static_cast<float>(Arena.Centre.X));
			const int32 RimCount = FMath::Max(1, Count - 1);
			for (int32 k = 0; k < RimCount && Out.Num() < Count; ++k)
			{
				const float Ang = InAng + static_cast<float>(k) * 2.f * PI / static_cast<float>(RimCount);
				const FVector2D Want = Arena.Centre + ArenaMapDir(Ang) * (Arena.Radius * 0.65f);
				FVector2D Ok = Want;
				NudgeToClear(static_cast<float>(Want.X), static_cast<float>(Want.Y), 150.f, 300.f, Ok);
				AddSpot(Ok);
			}
		}
	}
	else
	{
		UE_LOG(LogSQ, Warning, TEXT("GetObjectiveSpots: unknown objective type '%s'"), *ObjectiveType.ToString());
		return false;
	}
	if (Out.Num() > Count) Out.SetNum(Count);
	return Out.Num() >= Count;
}

bool ASQArena::GetEscortPath(TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (GridN <= 0 || RoadPts.Num() < 2) return false;
	for (const FVector2D& P : RoadPts)
	{
		const float PX = static_cast<float>(P.X);
		const float PY = static_cast<float>(P.Y);
		OutPoints.Add(FVector(PX, PY, GroundZ(PX, PY)));
	}
	return true;
}

FVector ASQArena::GetPlayerStart(int32 Index) const
{
	const int32 Safe = FMath::Max(0, Index);
	// 0-3 on an inner ring (outside the lantern's ring), then 8 on an outer ring
	const bool bInner = Safe < 4;
	const float StartR = bInner ? 520.f : 800.f;
	const float StartAng = bInner ? (PI * 0.25f + static_cast<float>(Safe) * PI * 0.5f) : (static_cast<float>(Safe - 4) * PI * 0.25f);
	float SX = FMath::Cos(StartAng) * StartR;
	float SY = FMath::Sin(StartAng) * StartR;
	if (GridN > 0 && IsWallAt(SX, SY, 80.f))
	{
		FVector2D Ok;
		if (NudgeToClear(SX, SY, 60.f, 450.f, Ok))
		{
			SX = static_cast<float>(Ok.X);
			SY = static_cast<float>(Ok.Y);
		}
	}
	return FVector(SX, SY, GroundZ(SX, SY));
}

// ---------------------------------------------------------------- horde blockers

void ASQArena::BuildHordeGrid()
{
	HordeObs.Reset();
	HordeBuckets.Reset();
	HordeChainA.Reset();
	HordeChainB.Reset();
	HordeChainDirA.Reset();
	HordeChainDirB.Reset();
	HordeGridN = 0;
	HordeObs.Append(MapWalls);
	for (const FVector& Spot : BlockerSpots)
	{
		FSQMapWall Round;
		Round.A = FVector2D(Spot.X, Spot.Y);
		Round.B = Round.A;
		Round.HalfThick = static_cast<float>(Spot.Z);
		Round.Kind = ESQWallKind::Pillar;
		HordeObs.Add(Round);
	}
	if (HordeObs.Num() == 0) return;
	// Chains: arcs are many short chords joined end to end. Steering heads for the end of the whole chain (the real gap),
	// not the end of one chord (which is just the next chord's start)
	const int32 NumObs = HordeObs.Num();
	HordeChainA.SetNum(NumObs);
	HordeChainB.SetNum(NumObs);
	HordeChainDirA.SetNum(NumObs);
	HordeChainDirB.SetNum(NumObs);
	auto WalkChain = [this, NumObs](int32 StartIdx, bool bFromA, FVector2D& OutEnd, FVector2D& OutDir)
	{
		int32 Cur = StartIdx;
		FVector2D Prev = bFromA ? HordeObs[StartIdx].B : HordeObs[StartIdx].A;
		FVector2D At = bFromA ? HordeObs[StartIdx].A : HordeObs[StartIdx].B;
		for (int32 Guard = 0; Guard < 96; ++Guard)
		{
			int32 Next = INDEX_NONE;
			FVector2D NextFar = At;
			for (int32 j = 0; j < NumObs; ++j)
			{
				if (j == Cur || HordeObs[j].IsRound()) continue;
				if (FVector2D::DistSquared(HordeObs[j].A, At) < 25.0) { Next = j; NextFar = HordeObs[j].B; break; }
				if (FVector2D::DistSquared(HordeObs[j].B, At) < 25.0) { Next = j; NextFar = HordeObs[j].A; break; }
			}
			if (Next == INDEX_NONE || Next == StartIdx) break;
			Prev = At;
			At = NextFar;
			Cur = Next;
		}
		OutEnd = At;
		OutDir = (At - Prev).GetSafeNormal();
	};
	for (int32 ObsIdx = 0; ObsIdx < NumObs; ++ObsIdx)
	{
		const FSQMapWall& W = HordeObs[ObsIdx];
		if (W.IsRound())
		{
			HordeChainA[ObsIdx] = W.A;
			HordeChainB[ObsIdx] = W.B;
			HordeChainDirA[ObsIdx] = FVector2D::ZeroVector;
			HordeChainDirB[ObsIdx] = FVector2D::ZeroVector;
			continue;
		}
		WalkChain(ObsIdx, true, HordeChainA[ObsIdx], HordeChainDirA[ObsIdx]);
		WalkChain(ObsIdx, false, HordeChainB[ObsIdx], HordeChainDirB[ObsIdx]);
	}
	HordeGridN = FMath::Clamp(FMath::CeilToInt(Half * 2.f / HordeCell), 1, 128);
	HordeBuckets.SetNum(HordeGridN * HordeGridN);
	for (int32 ObsIdx = 0; ObsIdx < HordeObs.Num(); ++ObsIdx)
	{
		const FSQMapWall& W = HordeObs[ObsIdx];
		// A query point (radius <= ArenaMapHordePad) only reads its own cell: insert into every cell the padded capsule touches
		const float Pad = W.HalfThick + ArenaMapHordePad;
		const float MinX = static_cast<float>(FMath::Min(W.A.X, W.B.X)) - Pad;
		const float MaxX = static_cast<float>(FMath::Max(W.A.X, W.B.X)) + Pad;
		const float MinY = static_cast<float>(FMath::Min(W.A.Y, W.B.Y)) - Pad;
		const float MaxY = static_cast<float>(FMath::Max(W.A.Y, W.B.Y)) + Pad;
		const int32 CX0 = FMath::Clamp(FMath::FloorToInt((MinX + Half) / HordeCell), 0, HordeGridN - 1);
		const int32 CX1 = FMath::Clamp(FMath::FloorToInt((MaxX + Half) / HordeCell), 0, HordeGridN - 1);
		const int32 CY0 = FMath::Clamp(FMath::FloorToInt((MinY + Half) / HordeCell), 0, HordeGridN - 1);
		const int32 CY1 = FMath::Clamp(FMath::FloorToInt((MaxY + Half) / HordeCell), 0, HordeGridN - 1);
		for (int32 CY = CY0; CY <= CY1; ++CY)
		{
			for (int32 CX = CX0; CX <= CX1; ++CX)
			{
				// Skip cells the capsule's padded reach does not touch (long diagonal walls)
				const float CellX = -Half + (static_cast<float>(CX) + 0.5f) * HordeCell;
				const float CellY = -Half + (static_cast<float>(CY) + 0.5f) * HordeCell;
				const float CellReach = HordeCell * 0.7072f + Pad;
				if (DistToSegment2D(CellX, CellY, static_cast<float>(W.A.X), static_cast<float>(W.A.Y), static_cast<float>(W.B.X), static_cast<float>(W.B.Y)) > CellReach) continue;
				HordeBuckets[CY * HordeGridN + CX].Add(ObsIdx);
			}
		}
	}
}

bool ASQArena::ResolveWallOverlap(FVector2D& InOutPos, float Radius) const
{
	if (HordeGridN <= 0) return false;
	const int32 CX = FMath::Clamp(FMath::FloorToInt((static_cast<float>(InOutPos.X) + Half) / HordeCell), 0, HordeGridN - 1);
	const int32 CY = FMath::Clamp(FMath::FloorToInt((static_cast<float>(InOutPos.Y) + Half) / HordeCell), 0, HordeGridN - 1);
	const TArray<int32>& Bucket = HordeBuckets[CY * HordeGridN + CX];
	const float UseR = FMath::Min(Radius, ArenaMapHordePad);
	bool bMoved = false;
	// A few passes: a push out of one wall can land in a neighbour (corners, joints)
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
	bool bPassMoved = false;
	for (const int32 ObsIdx : Bucket)
	{
		const FSQMapWall& W = HordeObs[ObsIdx];
		const FVector2D Closest = ArenaMapClosest(InOutPos, W.A, W.B);
		const FVector2D Away = InOutPos - Closest;
		const double AwaySq = Away.SizeSquared();
		const float Need = W.HalfThick + UseR;
		if (AwaySq >= static_cast<double>(Need) * Need) continue;
		const float AwayLen = static_cast<float>(FMath::Sqrt(AwaySq));
		FVector2D OutDir;
		if (AwayLen > 0.5f)
		{
			OutDir = Away / AwayLen;
		}
		else
		{
			// Dead on the centre line: out to the side of the wall facing the arena centre
			const FVector2D Along = (W.B - W.A).GetSafeNormal();
			OutDir = Along.IsNearlyZero() ? FVector2D(1.0, 0.0) : FVector2D(-Along.Y, Along.X);
			if (FVector2D::DotProduct(OutDir, -Closest) < 0.0) OutDir = -OutDir;
		}
		InOutPos = Closest + OutDir * Need;
		bMoved = true;
		bPassMoved = true;
	}
	if (!bPassMoved) break;
	}
	return bMoved;
}

FVector2D ASQArena::SteerAroundWalls(const FVector2D& Pos, const FVector2D& Dir, float Radius, float Look) const
{
	if (HordeGridN <= 0 || Dir.IsNearlyZero()) return Dir;
	const float UseR = FMath::Min(Radius, ArenaMapHordePad);
	// Probe ahead (half way first: a wall right in front matters most)
	const FSQMapWall* Hit = nullptr;
	int32 HitIdx = INDEX_NONE;
	for (int32 ProbeIdx = 0; ProbeIdx < 2 && !Hit; ++ProbeIdx)
	{
		const FVector2D Probe = Pos + Dir * (Look * (ProbeIdx == 0 ? 0.5f : 1.f));
		const int32 CX = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Probe.X) + Half) / HordeCell), 0, HordeGridN - 1);
		const int32 CY = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Probe.Y) + Half) / HordeCell), 0, HordeGridN - 1);
		float Deepest = 0.f;
		for (const int32 ObsIdx : HordeBuckets[CY * HordeGridN + CX])
		{
			const FSQMapWall& W = HordeObs[ObsIdx];
			const float Dist = static_cast<float>(FVector2D::Distance(Probe, ArenaMapClosest(Probe, W.A, W.B)));
			const float Depth = W.HalfThick + UseR - Dist;
			if (Depth > Deepest)
			{
				Deepest = Depth;
				Hit = &W;
				HitIdx = ObsIdx;
			}
		}
	}
	if (!Hit) return Dir;

	// Go round the end that makes the shorter trip to a virtual target straight ahead (stable frame to frame)
	const FVector2D Target = Pos + Dir * FMath::Max(1200.0, static_cast<double>(Look) * 4.0);
	const float Clear = Hit->HalfThick + UseR + 40.f;
	FVector2D EndA;
	FVector2D EndB;
	if (Hit->IsRound())
	{
		const FVector2D ToC = (Hit->A - Pos).GetSafeNormal();
		const FVector2D Side(-ToC.Y, ToC.X);
		EndA = Hit->A + Side * Clear;
		EndB = Hit->A - Side * Clear;
	}
	else if (HordeChainA.IsValidIndex(HitIdx))
	{
		// Round the ends of the whole chain this run belongs to
		EndA = HordeChainA[HitIdx] + HordeChainDirA[HitIdx] * Clear;
		EndB = HordeChainB[HitIdx] + HordeChainDirB[HitIdx] * Clear;
	}
	else
	{
		const FVector2D Along = (Hit->B - Hit->A).GetSafeNormal();
		EndA = Hit->A - Along * Clear;
		EndB = Hit->B + Along * Clear;
	}
	const double ViaA = FVector2D::Distance(Pos, EndA) + FVector2D::Distance(EndA, Target);
	const double ViaB = FVector2D::Distance(Pos, EndB) + FVector2D::Distance(EndB, Target);
	const FVector2D ToEnd = ((ViaA <= ViaB ? EndA : EndB) - Pos).GetSafeNormal();
	return ToEnd.IsNearlyZero() ? Dir : ToEnd;
}

// ---------------------------------------------------------------- mission layout (top-down maps, Nov 9 2026)

void ASQArena::ResetLayoutState()
{
	BlockerSpots.Reset();
	IcePos.Reset();
	IceRadius.Reset();
	LampSpots.Reset();
	Plateaus.Reset();
	MapZones.Reset();
	MapWalls.Reset();
	RoadPts.Reset();
	WaterA.Reset();
	WaterB.Reset();
	WaterHalf.Reset();
	SpawnerSpots.Reset();
	CollectSpots.Reset();
	EliteSpots.Reset();
	LandmarkOrder.Reset();
	EliteOrder.Reset();
	BossZone = INDEX_NONE;
	Districts.Reset();
	Lanes.Reset();
	SkateFeatures.Reset();
	ParkSpots.Reset();
}

bool ASQArena::ValidateLayout(FString& OutWhy) const
{
	// Flood fill on a coarse grid from the first player start: a cell is open when a reaper capsule (+ margin) fits.
	const float Cell = 150.f;
	const float Clear = 50.f;
	const int32 N = FMath::Clamp(FMath::CeilToInt(Half * 2.f / Cell), 8, 200);
	auto ToCell = [&](float V) { return FMath::Clamp(FMath::FloorToInt((V + Half) / Cell), 0, N - 1); };
	auto CellX = [&](int32 I) { return -Half + (static_cast<float>(I) + 0.5f) * Cell; };
	TArray<uint8> Open;
	Open.SetNumZeroed(N * N);
	int32 OpenCount = 0;
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const float WX = CellX(X), WY = CellX(Y);
			if (FMath::Abs(WX) > Half - 200.f || FMath::Abs(WY) > Half - 200.f) continue;
			if (IsWallAt(WX, WY, Clear)) continue;
			Open[Y * N + X] = 1;
			++OpenCount;
		}
	}
	if (OpenCount < N * N / 4) { OutWhy = TEXT("too little open ground"); return false; }
	// Start from the hub centre (or the nearest open cell to it)
	int32 Start = INDEX_NONE;
	for (int32 R = 0; R < N / 2 && Start == INDEX_NONE; ++R)
	{
		for (int32 DY = -R; DY <= R && Start == INDEX_NONE; ++DY)
		{
			for (int32 DX = -R; DX <= R; ++DX)
			{
				const int32 X = N / 2 + DX, Y = N / 2 + DY;
				if (X >= 0 && Y >= 0 && X < N && Y < N && Open[Y * N + X]) { Start = Y * N + X; break; }
			}
		}
	}
	if (Start == INDEX_NONE) { OutWhy = TEXT("no open hub"); return false; }
	TArray<uint8> Seen;
	Seen.SetNumZeroed(N * N);
	TArray<int32> Queue;
	Queue.Reserve(OpenCount);
	Queue.Add(Start);
	Seen[Start] = 1;
	int32 Reached = 0;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 C = Queue[Head];
		++Reached;
		const int32 X = C % N, Y = C / N;
		const int32 NX[4] = { X + 1, X - 1, X, X };
		const int32 NY[4] = { Y, Y, Y + 1, Y - 1 };
		for (int32 k = 0; k < 4; ++k)
		{
			if (NX[k] < 0 || NY[k] < 0 || NX[k] >= N || NY[k] >= N) continue;
			const int32 Nb = NY[k] * N + NX[k];
			if (Open[Nb] && !Seen[Nb]) { Seen[Nb] = 1; Queue.Add(Nb); }
		}
	}
	// No sealed pockets: enemies, pickups and chests spawn anywhere open, so nearly all open ground must connect
	if (static_cast<float>(Reached) < 0.85f * static_cast<float>(OpenCount))
	{
		OutWhy = FString::Printf(TEXT("sealed pockets (%d of %d cells reachable)"), Reached, OpenCount);
		return false;
	}
	auto Reachable = [&](const FVector2D& P) -> bool
	{
		const int32 CX = ToCell(static_cast<float>(P.X)), CY = ToCell(static_cast<float>(P.Y));
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				const int32 X = CX + DX, Y = CY + DY;
				if (X >= 0 && Y >= 0 && X < N && Y < N && Seen[Y * N + X]) return true;
			}
		}
		return false;
	};
	for (int32 i = 0; i < 4; ++i)
	{
		const float A = PI * 0.25f + static_cast<float>(i) * PI * 0.5f;
		if (!Reachable(FVector2D(FMath::Cos(A) * 520.f, FMath::Sin(A) * 520.f))) { OutWhy = TEXT("player start blocked"); return false; }
	}
	for (const FSQMapZone& Zone : MapZones)
	{
		if (!Reachable(Zone.Centre)) { OutWhy = TEXT("zone unreachable"); return false; }
	}
	for (const FVector2D& Pad : SpawnerSpots)
	{
		if (!Reachable(Pad)) { OutWhy = TEXT("spawner pad unreachable"); return false; }
	}
	for (const FVector2D& Spot : CollectSpots)
	{
		if (!Reachable(Spot)) { OutWhy = TEXT("objective spot unreachable"); return false; }
	}
	for (const FVector2D& Spot : EliteSpots)
	{
		if (!Reachable(Spot)) { OutWhy = TEXT("elite spot unreachable"); return false; }
	}
	for (const FVector2D& P : RoadPts)
	{
		if (!Reachable(P)) { OutWhy = TEXT("escort road blocked"); return false; }
	}
	if (SpawnerSpots.Num() < 3) { OutWhy = TEXT("too few spawner pads"); return false; }
	return true;
}

void ASQArena::PlanLayout(const USQData* D, int32 LayoutSeed)
{
	FRandomStream LR(LayoutSeed);
	const FArenaMapStyle Style = ArenaLoadMapStyle(BiomeId);
	LayoutName = Style.Layout.IsNone() ? FName(TEXT("GraveRows")) : Style.Layout;
	HubRadius = FMath::Clamp(D->Tune(TEXT("MapHubRadius"), 1400.f), 600.f, Half * 0.4f);
	KiteRing = FMath::Clamp(D->Tune(TEXT("MapKiteRing"), 1100.f), 0.f, Half * 0.3f);
	RoadHalfWidth = FMath::Clamp(D->Tune(TEXT("MapRoadWidth"), 600.f) * 0.5f, 150.f, 600.f);
	const float LandmarkR = FMath::Clamp(D->Tune(TEXT("MapLandmarkRadius"), 1000.f), 500.f, Half * 0.3f);
	const float BossR = FMath::Clamp(Style.BossRadius > 0.f ? Style.BossRadius : D->Tune(TEXT("MapBossRadius"), 1800.f), 900.f, Half * 0.4f);
	const int32 NumLandmarks = FMath::Clamp(Style.Landmarks > 0 ? Style.Landmarks : FMath::RoundToInt(D->Tune(TEXT("MapLandmarks"), 4.f)), 2, 6);
	const float ZoneDist = Half * FMath::Clamp(D->Tune(TEXT("MapZoneRing"), 0.62f), 0.4f, 0.8f);
	const float RoadInset = FMath::Clamp(D->Tune(TEXT("MapRoadInset"), 1200.f), 500.f, Half * 0.5f);
	const float SpawnerInset = FMath::Clamp(D->Tune(TEXT("MapSpawnerInset"), 1000.f), 500.f, Half * 0.5f);

	// --- Hub: flat open centre (player starts, the first lantern) ---
	FSQMapZone HubZone;
	HubZone.Centre = FVector2D::ZeroVector;
	HubZone.Radius = HubRadius;
	HubZone.Kind = ESQZoneKind::Hub;
	MapZones.Add(HubZone);

	// --- Boss arena + landmarks evenly round a ring (zone 1 = boss), in angle order ---
	const float BaseAng = LR.FRandRange(0.f, 2.f * PI);
	const int32 NumRing = NumLandmarks + 1;
	const float RingStep = 2.f * PI / static_cast<float>(NumRing);
	for (int32 k = 0; k < NumRing; ++k)
	{
		const float AngJitter = LR.FRandRange(-0.07f, 0.07f);
		const float DistRoll = LR.FRandRange(0.97f, 1.04f);
		FSQMapZone RingZone;
		RingZone.Angle = BaseAng + static_cast<float>(k) * RingStep + (k == 0 ? 0.f : AngJitter);
		float Dist = 0.f;
		if (k == 0)
		{
			RingZone.Kind = ESQZoneKind::Boss;
			RingZone.Radius = BossR;
			Dist = FMath::Max(HubRadius + BossR * 0.5f, FMath::Min(ZoneDist, Half - BossR - 450.f));
			BossZone = MapZones.Num();
		}
		else
		{
			RingZone.Kind = ESQZoneKind::Landmark;
			RingZone.Radius = LandmarkR;
			Dist = FMath::Max(HubRadius + KiteRing + LandmarkR * 0.5f, FMath::Min(ZoneDist * DistRoll, Half - LandmarkR - 650.f));
		}
		RingZone.Centre = ArenaMapDir(RingZone.Angle) * Dist;
		MapZones.Add(RingZone);
	}
	// Two seeded landmark orders: defend points, and elites (half a turn on, so the two rarely share a zone)
	for (int32 ZoneIdx = 0; ZoneIdx < MapZones.Num(); ++ZoneIdx)
	{
		if (MapZones[ZoneIdx].Kind == ESQZoneKind::Landmark) LandmarkOrder.Add(ZoneIdx);
	}
	for (int32 s = LandmarkOrder.Num() - 1; s > 0; --s)
	{
		const int32 SwapWith = LR.RandRange(0, s);
		LandmarkOrder.Swap(s, SwapWith);
	}
	for (int32 i = 0; i < LandmarkOrder.Num(); ++i) EliteOrder.Add(LandmarkOrder[(i + LandmarkOrder.Num() / 2) % LandmarkOrder.Num()]);

	// --- Escort road: from the edge gap opposite the boss, in to a band through the kiting ring, round past the
	// landmarks' inner edges, and into the boss arena ---
	const float Opp = BaseAng + PI;
	float StartAng = Opp;
	float BestGap = TNumericLimits<float>::Max();
	for (int32 k = 0; k < NumRing; ++k)
	{
		const float MidAng = BaseAng + (static_cast<float>(k) + 0.5f) * RingStep;
		const float GapDiff = FMath::Abs(ArenaMapWrap(MidAng - Opp));
		if (GapDiff < BestGap)
		{
			BestGap = GapDiff;
			StartAng = MidAng;
		}
	}
	const float TurnSign = LR.FRand() < 0.5f ? 1.f : -1.f;
	float Sweep = ArenaMapWrap(BaseAng - StartAng);
	if (TurnSign > 0.f && Sweep < 0.f) Sweep += 2.f * PI;
	if (TurnSign < 0.f && Sweep > 0.f) Sweep -= 2.f * PI;
	float MinInner = TNumericLimits<float>::Max();
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind == ESQZoneKind::Landmark) MinInner = FMath::Min(MinInner, static_cast<float>(Zone.Centre.Size()) - Zone.Radius);
	}
	const float RoadR = FMath::Max(HubRadius + RoadHalfWidth + 150.f, FMath::Min(HubRadius + KiteRing * 0.55f, MinInner - RoadHalfWidth - 120.f));
	const FSQMapZone& BossArena = MapZones[BossZone];
	const FVector2D RoadEnd = BossArena.Centre - BossArena.Centre.GetSafeNormal() * (BossArena.Radius * 0.45f);
	const float EdgeR = ArenaMapEdgeDist(StartAng, RoadInset, Half);
	const float RoadClamp = Half - 700.f;
	const float Fracs[] = { 0.f, 0.02f, 0.07f, 0.17f, 0.32f, 0.47f, 0.62f, 0.77f, 0.90f };
	TArray<FVector2D> Ctrl;
	for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Fracs)); ++i)
	{
		const float Wobble = LR.FRandRange(-80.f, 80.f);
		float CtrlR = RoadR;
		if (i == 0) CtrlR = EdgeR;
		else if (i == 1) CtrlR = FMath::Lerp(RoadR, EdgeR, 0.62f);
		else if (i == 2) CtrlR = FMath::Min(EdgeR, RoadR + 250.f);
		else if (i < 8) CtrlR = RoadR + Wobble;
		const FVector2D CtrlPt = ArenaMapDir(StartAng + Sweep * Fracs[i]) * CtrlR;
		Ctrl.Add(FVector2D(FMath::Clamp(CtrlPt.X, -static_cast<double>(RoadClamp), static_cast<double>(RoadClamp)), FMath::Clamp(CtrlPt.Y, -static_cast<double>(RoadClamp), static_cast<double>(RoadClamp))));
	}
	Ctrl.Add(RoadEnd);
	RoadPts.Add(Ctrl[0]);
	for (int32 Seg = 0; Seg + 1 < Ctrl.Num(); ++Seg)
	{
		const FVector2D& P0 = Ctrl[FMath::Max(Seg - 1, 0)];
		const FVector2D& P1 = Ctrl[Seg];
		const FVector2D& P2 = Ctrl[Seg + 1];
		const FVector2D& P3 = Ctrl[FMath::Min(Seg + 2, Ctrl.Num() - 1)];
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(static_cast<float>(FVector2D::Distance(P1, P2)) / 250.f));
		for (int32 StepIdx = 1; StepIdx <= Steps; ++StepIdx)
		{
			const FVector2D Pt = ArenaMapCatmull(P0, P1, P2, P3, static_cast<float>(StepIdx) / static_cast<float>(Steps));
			RoadPts.Add(FVector2D(FMath::Clamp(Pt.X, -static_cast<double>(RoadClamp), static_cast<double>(RoadClamp)), FMath::Clamp(Pt.Y, -static_cast<double>(RoadClamp), static_cast<double>(RoadClamp))));
		}
	}

	// --- Map revamp: districts round the ring zones and a lane from the hub to each (before tiers / pads / walls) ---
	PlanDistricts(D, LR);

	// --- Water (Docks): the side across the boss axis away from the road is harbour, plus canals reaching in ---
	TArray<FVector2D> PendWaterA;
	TArray<FVector2D> PendWaterB;
	TArray<float> PendWaterHalf;
	if (LayoutName == FName(TEXT("Docks")))
	{
		const float WaterBand = FMath::Clamp(D->Tune(TEXT("MapWaterBand"), 900.f), 400.f, Half * 0.25f);
		const float WaterAng = BaseAng - TurnSign * PI * 0.5f;
		const bool bAlongY = FMath::Abs(FMath::Cos(WaterAng)) >= FMath::Abs(FMath::Sin(WaterAng));
		const float WaterSign = bAlongY ? (FMath::Cos(WaterAng) >= 0.f ? 1.f : -1.f) : (FMath::Sin(WaterAng) >= 0.f ? 1.f : -1.f);
		const float BandLine = WaterSign * (Half - WaterBand * 0.5f);
		const FVector2D Outward = bAlongY ? FVector2D(WaterSign, 0.0) : FVector2D(0.0, WaterSign);
		const FVector2D AlongAxis = bAlongY ? FVector2D(0.0, 1.0) : FVector2D(1.0, 0.0);
		PendWaterA.Add(bAlongY ? FVector2D(BandLine, -Half) : FVector2D(-Half, BandLine));
		PendWaterB.Add(bAlongY ? FVector2D(BandLine, Half) : FVector2D(Half, BandLine));
		PendWaterHalf.Add(WaterBand * 0.5f);
		for (int32 c = 0; c < 2; ++c)
		{
			const float CanalAlong = (c == 0 ? -1.f : 1.f) * Half * 0.42f + LR.FRandRange(-500.f, 500.f);
			const float CanalLen = LR.FRandRange(1800.f, 2600.f);
			const FVector2D CanalFrom = Outward * (Half - WaterBand + 100.f) + AlongAxis * CanalAlong;
			PendWaterA.Add(CanalFrom);
			PendWaterB.Add(CanalFrom - Outward * CanalLen);
			PendWaterHalf.Add(220.f);
		}
	}

	// --- Low tiers (terraces): at most MapTierMaxHeight, ramps no steeper than MapTierSlope ---
	const int32 NumTiers = FMath::Clamp(Style.Tiers >= 0 ? Style.Tiers : FMath::RoundToInt(D->Tune(TEXT("MapTiers"), 2.f)), 0, 10);
	const float TierMaxH = FMath::Clamp(D->Tune(TEXT("MapTierMaxHeight"), 250.f), 50.f, 300.f);
	const float TierH = FMath::Clamp(Style.TierHeight > 0.f ? Style.TierHeight : 180.f, 50.f, TierMaxH);
	const float TierSlope = FMath::Clamp(D->Tune(TEXT("MapTierSlope"), 16.f), 8.f, 24.f);
	auto RampFor = [](float RampH, float SlopeDeg) { return FMath::Max(450.f, 1.5f * RampH / FMath::Tan(FMath::DegreesToRadians(SlopeDeg))); };
	// How far a tier's ramp foot may reach from C before it touches a zone, the road, a pad, water or another tier
	auto TierRoom = [&](const FVector2D& C, int32 SkipZone) -> float
	{
		const float TX = static_cast<float>(C.X);
		const float TY = static_cast<float>(C.Y);
		float Room = FMath::Min(Half - 400.f - FMath::Abs(TX), Half - 400.f - FMath::Abs(TY));
		Room = FMath::Min(Room, static_cast<float>(C.Size()) - HubRadius - KiteRing * 0.5f);
		for (int32 ZoneIdx = 0; ZoneIdx < MapZones.Num(); ++ZoneIdx)
		{
			const FSQMapZone& Other = MapZones[ZoneIdx];
			if (ZoneIdx == SkipZone || Other.Kind == ESQZoneKind::Hub) continue;
			Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, Other.Centre)) - Other.Radius - 150.f);
		}
		Room = FMath::Min(Room, DistToRoad(TX, TY) - RoadHalfWidth - 100.f);
		Room = FMath::Min(Room, DistToLane(TX, TY) - LaneHalfWidth - 100.f);
		for (const FVector2D& Pad : SpawnerSpots) Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, Pad)) - ArenaMapPadClear - 100.f);
		for (int32 w = 0; w < PendWaterA.Num(); ++w)
		{
			Room = FMath::Min(Room, DistToSegment2D(TX, TY, static_cast<float>(PendWaterA[w].X), static_cast<float>(PendWaterA[w].Y),
				static_cast<float>(PendWaterB[w].X), static_cast<float>(PendWaterB[w].Y)) - PendWaterHalf[w] - 150.f);
		}
		for (const FSQTerrainPlateau& Other : Plateaus) Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, Other.Centre)) - Other.FootRadius() - 300.f);
		return Room;
	};
	if (Style.bTiersOnLandmarks)
	{
		// Raised landmarks (Clocktower gear platforms): the defend point sits on the top
		for (int32 i = 0; i < LandmarkOrder.Num() && Plateaus.Num() < NumTiers; ++i)
		{
			FSQMapZone& Zone = MapZones[LandmarkOrder[i]];
			const float PhaseRoll = LR.FRandRange(0.f, 2.f * PI);
			FSQTerrainPlateau Tier;
			Tier.Centre = Zone.Centre;
			Tier.Radius = Zone.Radius * 0.55f;
			Tier.Wobble = 0.04f;
			Tier.Lobes = 3;
			Tier.WobblePhase = PhaseRoll;
			Tier.Height = TierH;
			const float RampAvail = TierRoom(Zone.Centre, LandmarkOrder[i]) - Tier.Radius * (1.f + Tier.Wobble);
			const float RampMin = RampFor(TierH, FMath::Min(TierSlope + 6.f, 24.f));
			if (RampAvail < RampMin) continue;
			Tier.Ramp = FMath::Min(RampFor(TierH, TierSlope + 2.f), RampAvail);
			Plateaus.Add(Tier);
			Zone.bRaised = true;
			Zone.Radius = Tier.Radius;
		}
	}
	// Free tiers: the corners first (the roomiest ground, far from every zone), then anywhere that fits
	const float CornerTurn = LR.FRandRange(0.f, 4.f);
	for (int32 Try = 0; Try < 84 && Plateaus.Num() < NumTiers; ++Try)
	{
		// Same rolls every try (deterministic)
		const float CornerAng = PI * 0.25f + PI * 0.5f * FMath::Fmod(static_cast<float>(FMath::FloorToInt(CornerTurn)) + static_cast<float>(Try), 4.f);
		const bool bCorner = Try < 12;
		const float RandAng = LR.FRandRange(0.f, 2.f * PI);
		const float RandDist = LR.FRandRange(HubRadius + KiteRing + 700.f, Half * 1.2f);
		const float CornerDist = (Half - 1600.f - 400.f * static_cast<float>(Try / 4)) * 1.4142f;
		const float TierAng = bCorner ? CornerAng : RandAng;
		const float TierDist = bCorner ? CornerDist : RandDist;
		const float TopWanted = LR.FRandRange(650.f, 1000.f);
		const float HeightRoll = LR.FRandRange(0.8f, 1.f);
		const float WobbleRoll = LR.FRandRange(0.04f, 0.1f);
		const float PhaseRoll = LR.FRandRange(0.f, 2.f * PI);
		const int32 LobeRoll = LR.RandRange(2, 4);
		const FVector2D TierAt = ArenaMapDir(TierAng) * TierDist;
		if (FMath::Abs(TierAt.X) > Half - 900.0 || FMath::Abs(TierAt.Y) > Half - 900.0) continue;
		FSQTerrainPlateau Tier;
		Tier.Centre = TierAt;
		Tier.Height = TierH * HeightRoll;
		Tier.Ramp = RampFor(Tier.Height, TierSlope);
		Tier.Wobble = WobbleRoll;
		Tier.WobblePhase = PhaseRoll;
		Tier.Lobes = LobeRoll;
		const float TopRoom = (TierRoom(TierAt, INDEX_NONE) - Tier.Ramp) / (1.f + Tier.Wobble);
		if (TopRoom < 380.f) continue;
		Tier.Radius = FMath::Min(TopWanted, TopRoom);
		Plateaus.Add(Tier);
	}

	// --- Spawner pads near the edges: every open gap between ring zones except the road's, and the corners ---
	TArray<float> PadAngs;
	for (int32 k = 0; k < NumRing; ++k)
	{
		const float MidAng = BaseAng + (static_cast<float>(k) + 0.5f) * RingStep;
		if (FMath::Abs(ArenaMapWrap(MidAng - StartAng)) < 0.01f) continue;
		PadAngs.Add(MidAng);
	}
	for (int32 k = 0; k < 4; ++k) PadAngs.Add(PI * 0.25f + static_cast<float>(k) * PI * 0.5f);
	// Fallbacks when tiers / water take some of those: either side of every gap
	for (int32 k = 0; k < NumRing; ++k)
	{
		const float MidAng = BaseAng + (static_cast<float>(k) + 0.5f) * RingStep;
		PadAngs.Add(MidAng + RingStep * 0.3f);
		PadAngs.Add(MidAng - RingStep * 0.3f);
	}
	const int32 WantPads = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapSpawnerCount"), 8.f)), 2, 16);
	for (const float PadAng : PadAngs)
	{
		if (SpawnerSpots.Num() >= WantPads) break;
		FVector2D Pad = ArenaMapDir(PadAng) * ArenaMapEdgeDist(PadAng, SpawnerInset, Half);
		// Pulled in off the water
		for (int32 w = 0; w < PendWaterA.Num(); ++w)
		{
			const float WaterGap = DistToSegment2D(static_cast<float>(Pad.X), static_cast<float>(Pad.Y), static_cast<float>(PendWaterA[w].X), static_cast<float>(PendWaterA[w].Y),
				static_cast<float>(PendWaterB[w].X), static_cast<float>(PendWaterB[w].Y)) - PendWaterHalf[w];
			if (WaterGap < ArenaMapPadClear + 100.f) Pad -= Pad.GetSafeNormal() * (ArenaMapPadClear + 100.f - WaterGap);
		}
		if (FVector2D::Distance(Pad, RoadPts[0]) < 1400.f) continue;
		bool bClear = true;
		for (const FVector2D& Other : SpawnerSpots)
		{
			if (FVector2D::Distance(Other, Pad) < 1300.f) bClear = false;
		}
		for (const FSQMapZone& Zone : MapZones)
		{
			if (FVector2D::Distance(Zone.Centre, Pad) < Zone.Radius + ArenaMapPadClear + 100.f) bClear = false;
		}
		if (DistToRoad(static_cast<float>(Pad.X), static_cast<float>(Pad.Y)) < RoadHalfWidth + ArenaMapPadClear + 100.f) bClear = false;
		for (const FSQTerrainPlateau& Tier : Plateaus)
		{
			if (FVector2D::Distance(Tier.Centre, Pad) < Tier.FootRadius() + ArenaMapPadClear + 150.f) bClear = false;
		}
		if (bClear) SpawnerSpots.Add(Pad);
	}

	// --- Walls: water first (other walls keep off it), then the biome's personality ---
	for (int32 w = 0; w < PendWaterA.Num(); ++w) AddWallRun(PendWaterA[w], PendWaterB[w], PendWaterHalf[w], 300.f, ESQWallKind::Water);
	// Map revamp: skate ground in every district (walls then keep off it)
	PlanSkateFeatures(D, LR);
	PlanWalls(LR);
	PlanParkSpots(D, LR);
	for (const FSQMapWall& W : MapWalls)
	{
		if (W.Kind != ESQWallKind::Water) continue;
		WaterA.Add(W.A);
		WaterB.Add(W.B);
		WaterHalf.Add(W.HalfThick);
	}
}

bool ASQArena::IsWallBlockedSpot(float X, float Y, float Clear, bool bWater) const
{
	// A capsule-wide lane between any wall and the arena edge (water may run right up to it)
	if (!bWater && (FMath::Abs(X) > Half - 350.f - Clear || FMath::Abs(Y) > Half - 350.f - Clear)) return true;
	// The hub and its kiting ring stay open
	if (X * X + Y * Y < FMath::Square(HubRadius + KiteRing + Clear)) return true;
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind == ESQZoneKind::Hub) continue;
		// Landmark cores and the whole boss arena floor stay open; landmark frames go in the band round the core
		const float Core = Zone.Kind == ESQZoneKind::Boss ? Zone.Radius - 60.f : Zone.Radius * (Zone.bRaised ? 0.6f : 0.8f);
		const float Need = Core + Clear;
		if (FVector2D::DistSquared(Zone.Centre, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return true;
	}
	if (DistToRoad(X, Y) < RoadHalfWidth + 180.f + Clear) return true;
	for (const FVector2D& Pad : SpawnerSpots)
	{
		const float Need = ArenaMapPadClear + 100.f + Clear;
		if (FVector2D::DistSquared(Pad, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return true;
	}
	// Map revamp: hub lanes stay open, and nothing stands on the skate ground
	if (DistToLane(X, Y) < LaneHalfWidth + 150.f + Clear) return true;
	for (const FSQSkateFeature& F : SkateFeatures)
	{
		const float Need = F.Reach() + Clear;
		if (FVector2D::DistSquared(F.Centre, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return true;
	}
		// Never on a tier ramp (walls stand on flat ground or on a top)
	for (const FSQTerrainPlateau& Tier : Plateaus)
	{
		const FVector2D Rel = FVector2D(X, Y) - Tier.Centre;
		const float Dist = static_cast<float>(Rel.Size());
		const float TopR = Tier.RadiusAt(FMath::Atan2(static_cast<float>(Rel.Y), static_cast<float>(Rel.X)));
		if (Dist + Clear > TopR && Dist - Clear < Tier.FootRadius()) return true;
	}
	if (!bWater)
	{
		for (const FSQMapWall& W : MapWalls)
		{
			// Off the water, and a capsule-wide lane (or more) round every round obstacle: no pinch a body could wedge in
			if (W.Kind == ESQWallKind::Water)
			{
				if (DistToSegment2D(X, Y, static_cast<float>(W.A.X), static_cast<float>(W.A.Y), static_cast<float>(W.B.X), static_cast<float>(W.B.Y)) < W.HalfThick + Clear + 60.f) return true;
			}
			else if (W.IsRound())
			{
				const float Need = W.HalfThick + Clear + 240.f;
				if (FVector2D::DistSquared(W.A, FVector2D(X, Y)) < static_cast<double>(Need) * Need) return true;
			}
		}
	}
	return false;
}

void ASQArena::AddWallRun(const FVector2D& A, const FVector2D& B, float HalfThick, float Height, ESQWallKind Kind)
{
	const FVector2D Delta = B - A;
	const float Len = static_cast<float>(Delta.Size());
	if (Len < 1.f) return;
	const bool bWater = Kind == ESQWallKind::Water;
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / 50.f));
	const float MinRun = FMath::Max(120.f, HalfThick);
	int32 RunStart = INDEX_NONE;
	for (int32 StepIdx = 0; StepIdx <= Steps; ++StepIdx)
	{
		const FVector2D P = A + Delta * (static_cast<double>(StepIdx) / static_cast<double>(Steps));
		const bool bFree = !IsWallBlockedSpot(static_cast<float>(P.X), static_cast<float>(P.Y), HalfThick, bWater);
		if (bFree && RunStart == INDEX_NONE) RunStart = StepIdx;
		const bool bCloseRun = RunStart != INDEX_NONE && (!bFree || StepIdx == Steps);
		if (!bCloseRun) continue;
		const int32 RunEnd = bFree ? StepIdx : StepIdx - 1;
		if (static_cast<float>(RunEnd - RunStart) * Len / static_cast<float>(Steps) >= MinRun)
		{
			FSQMapWall Piece;
			Piece.A = A + Delta * (static_cast<double>(RunStart) / static_cast<double>(Steps));
			Piece.B = A + Delta * (static_cast<double>(RunEnd) / static_cast<double>(Steps));
			Piece.HalfThick = HalfThick;
			Piece.Height = Height;
			Piece.Kind = Kind;
			MapWalls.Add(Piece);
		}
		RunStart = INDEX_NONE;
	}
}

bool ASQArena::AddWallRound(const FVector2D& C, float Radius, float Height, ESQWallKind Kind, bool bAttached)
{
	const float CX = static_cast<float>(C.X);
	const float CY = static_cast<float>(C.Y);
	if (IsWallBlockedSpot(CX, CY, Radius, false)) return false;
	if (!bAttached)
	{
		// Touching nothing, or a capsule-wide lane to everything (never a pinch a reaper could wedge in)
		for (const FSQMapWall& Other : MapWalls)
		{
			const float Lane = DistToSegment2D(CX, CY, static_cast<float>(Other.A.X), static_cast<float>(Other.A.Y), static_cast<float>(Other.B.X), static_cast<float>(Other.B.Y)) - Other.HalfThick - Radius;
			if (Lane < 240.f) return false;
		}
	}
	FSQMapWall Made;
	Made.A = C;
	Made.B = C;
	Made.HalfThick = Radius;
	Made.Height = Height;
	Made.Kind = Kind;
	MapWalls.Add(Made);
	return true;
}

void ASQArena::AddWallArc(const FVector2D& Centre, float Radius, float FromAng, float ToAng, float SpanDeg, float GapDeg, float HalfThick, float Height, ESQWallKind Kind)
{
	const float Span = FMath::DegreesToRadians(FMath::Max(2.f, SpanDeg));
	const float Gap = FMath::DegreesToRadians(FMath::Max(0.f, GapDeg));
	float A0 = FromAng;
	int32 Guard = 0;
	while (A0 < ToAng - 0.005f && Guard++ < 256)
	{
		const float A1 = FMath::Min(A0 + Span, ToAng);
		// Chords of at most ~4.5 m (the capsule ends overlap, so the run reads as one curve)
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(Radius * (A1 - A0) / 450.f));
		for (int32 PieceIdx = 0; PieceIdx < Pieces; ++PieceIdx)
		{
			const float PA = FMath::Lerp(A0, A1, static_cast<float>(PieceIdx) / static_cast<float>(Pieces));
			const float PB = FMath::Lerp(A0, A1, static_cast<float>(PieceIdx + 1) / static_cast<float>(Pieces));
			AddWallRun(Centre + ArenaMapDir(PA) * Radius, Centre + ArenaMapDir(PB) * Radius, HalfThick, Height, Kind);
		}
		A0 = A1 + Gap;
	}
}

void ASQArena::PlanWalls(FRandomStream& WallRand)
{
	const float RingIn = HubRadius + KiteRing;
	const float Phase = WallRand.FRandRange(0.f, 2.f * PI);
	// Map revamp: scattered obstacles scale with the area (the districts' skate ground keeps a share of it open)
	const float AreaK = FMath::Clamp(FMath::Square(Half / 5600.f), 1.f, 4.f);
	const float ScatterK = FMath::Max(1.f, AreaK * 0.6f);
	const bool bDocks = LayoutName == FName(TEXT("Docks"));
	const bool bTents = LayoutName == FName(TEXT("Tents"));
	const bool bCloisters = LayoutName == FName(TEXT("Cloisters"));
	const bool bGears = LayoutName == FName(TEXT("Gears"));
	const bool bGraves = !bDocks && !bTents && !bCloisters && !bGears;
	if (bGraves && LayoutName != FName(TEXT("GraveRows"))) UE_LOG(LogSQ, Warning, TEXT("Map %s: unknown MapLayout '%s' - using GraveRows"), *BiomeId.ToString(), *LayoutName.ToString());

	// Ring zones (made in angle order) and the open sectors between neighbours
	TArray<int32> RingIdx;
	for (int32 ZoneIdx = 0; ZoneIdx < MapZones.Num(); ++ZoneIdx)
	{
		if (MapZones[ZoneIdx].Kind != ESQZoneKind::Hub) RingIdx.Add(ZoneIdx);
	}
	struct FOpenSector
	{
		float From = 0.f;
		float To = 0.f;
		float Mid = 0.f;
	};
	TArray<FOpenSector> Sectors;
	for (int32 i = 0; i < RingIdx.Num(); ++i)
	{
		const FSQMapZone& ZoneA = MapZones[RingIdx[i]];
		const FSQMapZone& ZoneB = MapZones[RingIdx[(i + 1) % RingIdx.Num()]];
		float AngB = ZoneA.Angle + ArenaMapWrap(ZoneB.Angle - ZoneA.Angle);
		if (AngB <= ZoneA.Angle) AngB += 2.f * PI;
		const float HalfA = FMath::Asin(FMath::Min(0.95f, ZoneA.Radius * 1.3f / FMath::Max(1.f, static_cast<float>(ZoneA.Centre.Size()))));
		const float HalfB = FMath::Asin(FMath::Min(0.95f, ZoneB.Radius * 1.3f / FMath::Max(1.f, static_cast<float>(ZoneB.Centre.Size()))));
		FOpenSector Sec;
		Sec.From = ZoneA.Angle + HalfA;
		Sec.To = AngB - HalfB;
		Sec.Mid = (ZoneA.Angle + AngB) * 0.5f;
		Sectors.Add(Sec);
	}
	auto ForEachLandmark = [this](TFunctionRef<void(const FSQMapZone&, float)> Visit)
	{
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind != ESQZoneKind::Landmark) continue;
			Visit(Zone, FMath::Atan2(static_cast<float>(Zone.Centre.Y), static_cast<float>(Zone.Centre.X)));
		}
	};

	if (bGraves)
	{
		// Rings of grave rows round the hub: one just outside the kiting ring, short rows across every open sector further out
		AddWallArc(FVector2D::ZeroVector, RingIn + 450.f, Phase, Phase + 2.f * PI, 26.f, 9.f, 45.f, 110.f, ESQWallKind::GraveRow);
		for (const FOpenSector& Sec : Sectors)
		{
			if (Sec.To - Sec.From < FMath::DegreesToRadians(6.f)) continue;
			AddWallArc(FVector2D::ZeroVector, RingIn + 1250.f, Sec.From, Sec.To, 12.f, 5.f, 45.f, 110.f, ESQWallKind::GraveRow);
			AddWallArc(FVector2D::ZeroVector, RingIn + 1950.f, Sec.From + 0.03f, Sec.To - 0.03f, 14.f, 6.f, 45.f, 110.f, ESQWallKind::GraveRow);
			// Bigger maps: more short rows further out (split round the skate ground and lanes)
			for (float RowR = RingIn + 2800.f; RowR < Half - 1600.f; RowR += 950.f)
			{
				AddWallArc(FVector2D::ZeroVector, RowR, Sec.From + 0.05f, Sec.To - 0.05f, 10.f, 6.f, 45.f, 110.f, ESQWallKind::GraveRow);
			}
		}
		// Landmarks: an iron fence round the outer side (open toward the hub, a gate in the middle), a crypt either side
		ForEachLandmark([this](const FSQMapZone& Zone, float OutAng)
		{
			AddWallArc(Zone.Centre, Zone.Radius * 1.08f, OutAng - 1.75f, OutAng - 0.38f, 40.f, 0.f, 25.f, 120.f, ESQWallKind::Fence);
			AddWallArc(Zone.Centre, Zone.Radius * 1.08f, OutAng + 0.38f, OutAng + 1.75f, 40.f, 0.f, 25.f, 120.f, ESQWallKind::Fence);
			// Two crypts form the back gate (a ~4.5 m chokepoint), attached to the fence ends
			AddWallRound(Zone.Centre + ArenaMapDir(OutAng - 0.38f) * (Zone.Radius * 1.08f + 60.f), 180.f, 330.f, ESQWallKind::Crypt, true);
			AddWallRound(Zone.Centre + ArenaMapDir(OutAng + 0.38f) * (Zone.Radius * 1.08f + 60.f), 180.f, 330.f, ESQWallKind::Crypt, true);
		});
	}
	else if (bDocks)
	{
		// Crate stacks: short straight walls on the harbour grid (some L-shaped) out in the ring between the zones
		const int32 NumCrates = FMath::RoundToInt(18.f * ScatterK);
		for (int32 i = 0; i < NumCrates; ++i)
		{
			const bool bAlongX = WallRand.FRand() < 0.5f;
			const float CrateLen = WallRand.FRandRange(500.f, 1100.f);
			const float CrateAng = WallRand.FRandRange(0.f, 2.f * PI);
			const float CrateDist = WallRand.FRandRange(RingIn + 300.f, Half - 1000.f);
			const bool bL = WallRand.FRand() < 0.4f;
			const float LSign = WallRand.FRand() < 0.5f ? 1.f : -1.f;
			const FVector2D Axis = bAlongX ? FVector2D(1.0, 0.0) : FVector2D(0.0, 1.0);
			const FVector2D CrateAt = ArenaMapDir(CrateAng) * CrateDist;
			const FVector2D CrateA = CrateAt - Axis * (CrateLen * 0.5f);
			const FVector2D CrateB = CrateAt + Axis * (CrateLen * 0.5f);
			AddWallRun(CrateA, CrateB, 110.f, 170.f, ESQWallKind::Crates);
			if (bL) AddWallRun(CrateB, CrateB + FVector2D(-Axis.Y, Axis.X) * (LSign * 450.f), 110.f, 170.f, ESQWallKind::Crates);
		}
		// Landmarks: a broken ring of low mooring bollards
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind != ESQZoneKind::Landmark) continue;
			for (int32 j = 0; j < 12; ++j)
			{
				const bool bSkip = WallRand.FRand() < 0.35f;
				if (bSkip) continue;
				AddWallRound(Zone.Centre + ArenaMapDir(static_cast<float>(j) * PI / 6.f) * (Zone.Radius * 1.1f), 45.f, 110.f, ESQWallKind::Pillar);
			}
		}
	}
	else if (bTents)
	{
		// Stall avenues: two parallel rows of stalls running out from the kiting ring through every open sector
		for (const FOpenSector& Sec : Sectors)
		{
			const FVector2D Out = ArenaMapDir(Sec.Mid);
			const FVector2D Side(-Out.Y, Out.X);
			for (int32 s = 0; s < 2; ++s)
			{
				const float Lateral = s == 0 ? 430.f : -430.f;
				AddWallRun(Out * (RingIn + 450.f) + Side * Lateral, Out * (RingIn + 1950.f) + Side * Lateral, 110.f, 230.f, ESQWallKind::Stall);
			}
		}
		// Tents: big round tops scattered round the outer ring
		const int32 NumTents = FMath::RoundToInt(40.f * ScatterK);
		for (int32 i = 0; i < NumTents; ++i)
		{
			const float TentAng = WallRand.FRandRange(0.f, 2.f * PI);
			const float TentDist = WallRand.FRandRange(RingIn + 400.f, Half * 1.15f);
			const float TentR = WallRand.FRandRange(300.f, 430.f);
			AddWallRound(ArenaMapDir(TentAng) * TentDist, TentR, 420.f, ESQWallKind::Tent);
		}
		// Landmarks: a show ring of three curved barrier fences with wide gaps
		ForEachLandmark([this](const FSQMapZone& Zone, float OutAng)
		{
			AddWallArc(Zone.Centre, Zone.Radius * 1.1f, OutAng + 0.45f, OutAng + 0.45f + 2.f * PI, 70.f, 50.f, 25.f, 110.f, ESQWallKind::Fence);
		});
	}
	else if (bCloisters)
	{
		// A square cloister: four low walls with an opening in the middle of every side, a pillar on every corner
		auto AddCloister = [this](const FVector2D& C, float S, float Yaw)
		{
			const FVector2D AxisX = ArenaMapDir(Yaw);
			const FVector2D AxisY(-AxisX.Y, AxisX.X);
			const FVector2D Corners[4] = { C + (AxisX + AxisY) * S, C + (AxisY - AxisX) * S, C - (AxisX + AxisY) * S, C + (AxisX - AxisY) * S };
			for (int32 SideIdx = 0; SideIdx < 4; ++SideIdx)
			{
				const FVector2D& QA = Corners[SideIdx];
				const FVector2D& QB = Corners[(SideIdx + 1) % 4];
				const FVector2D SideMid = (QA + QB) * 0.5;
				const FVector2D U = (QB - QA).GetSafeNormal();
				AddWallRun(QA, SideMid - U * 280.f, 50.f, 140.f, ESQWallKind::Stone);
				AddWallRun(SideMid + U * 280.f, QB, 50.f, 140.f, ESQWallKind::Stone);
				AddWallRound(QA, 75.f, 300.f, ESQWallKind::Pillar, true);
			}
		};
		ForEachLandmark([&AddCloister](const FSQMapZone& Zone, float OutAng)
		{
			AddCloister(Zone.Centre, Zone.Radius * 1.05f, OutAng);
		});
		// Two small garden cloisters out in the open
		int32 Gardens = 0;
		const int32 WantGardens = FMath::RoundToInt(2.f * ScatterK);
		for (int32 Try = 0; Try < 40 * WantGardens && Gardens < WantGardens; ++Try)
		{
			const float GardenAng = WallRand.FRandRange(0.f, 2.f * PI);
			const float GardenDist = WallRand.FRandRange(RingIn + 900.f, Half - 1400.f);
			const float GardenYaw = WallRand.FRandRange(0.f, PI * 0.5f);
			const FVector2D GardenAt = ArenaMapDir(GardenAng) * GardenDist;
			const float GardenS = 650.f;
			if (IsWallBlockedSpot(static_cast<float>(GardenAt.X), static_cast<float>(GardenAt.Y), GardenS * 1.45f, false)) continue;
			if (IsWallAt(static_cast<float>(GardenAt.X), static_cast<float>(GardenAt.Y), GardenS * 1.45f + 250.f)) continue;
			AddCloister(GardenAt, GardenS, GardenYaw);
			++Gardens;
		}
		// Colonnades: a line of pillars out through every open sector
		for (const FOpenSector& Sec : Sectors)
		{
			for (float ColDist = RingIn + 300.f; ColDist < Half - 1300.f; ColDist += 420.f)
			{
				AddWallRound(ArenaMapDir(Sec.Mid) * ColDist, 55.f, 260.f, ESQWallKind::Pillar);
			}
		}
	}
	else if (bGears)
	{
		// Gear teeth round every landmark: round the ramp foot of a raised (platform) landmark, round the edge of a flat one
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind != ESQZoneKind::Landmark) continue;
			const float OutAng = FMath::Atan2(static_cast<float>(Zone.Centre.Y), static_cast<float>(Zone.Centre.X));
			float TeethR = Zone.Radius * 1.12f;
			for (const FSQTerrainPlateau& Tier : Plateaus)
			{
				if (FVector2D::DistSquared(Tier.Centre, Zone.Centre) < 1.0) TeethR = Tier.FootRadius() + 140.f;
			}
			for (int32 j = 0; j < 16; ++j)
			{
				if (j % 4 == 3) continue;
				AddWallRound(Zone.Centre + ArenaMapDir(OutAng + static_cast<float>(j) * 2.f * PI / 16.f) * TeethR, 85.f, 150.f, ESQWallKind::Gear);
			}
		}
		// Clock hands: radial brass walls through every open sector, broken in the middle
		for (const FOpenSector& Sec : Sectors)
		{
			const FVector2D Out = ArenaMapDir(Sec.Mid);
			const float Inner = RingIn + 250.f;
			const float OuterR = Half - 1100.f;
			const float MidR = (Inner + OuterR) * 0.5f;
			AddWallRun(Out * Inner, Out * (MidR - 320.f), 55.f, 130.f, ESQWallKind::Gear);
			AddWallRun(Out * (MidR + 320.f), Out * OuterR, 55.f, 130.f, ESQWallKind::Gear);
		}
		// The outer gear ring, with gaps
		AddWallArc(FVector2D::ZeroVector, Half - 1250.f, Phase, Phase + 2.f * PI, 22.f, 9.f, 55.f, 130.f, ESQWallKind::Gear);
	}

	// Boss arena rim: a ring of posts with wide gaps (it frames the fight, it doesn't fence it)
	if (MapZones.IsValidIndex(BossZone))
	{
		const FSQMapZone& BossArena = MapZones[BossZone];
		float PostR = 70.f;
		float PostH = 300.f;
		float StepDeg = 15.f;
		ESQWallKind PostKind = ESQWallKind::Pillar;
		if (bDocks) { PostR = 60.f; PostH = 140.f; }
		else if (bTents) { PostR = 60.f; PostH = 480.f; StepDeg = 18.f; }
		else if (bCloisters) { PostR = 75.f; PostH = 320.f; StepDeg = 14.f; }
		else if (bGears) { PostR = 90.f; PostH = 160.f; StepDeg = 16.f; PostKind = ESQWallKind::Gear; }
		const int32 Posts = FMath::Max(8, FMath::RoundToInt(360.f / StepDeg));
		for (int32 PostIdx = 0; PostIdx < Posts; ++PostIdx)
		{
			const float PostAng = Phase + static_cast<float>(PostIdx) * 2.f * PI / static_cast<float>(Posts);
			AddWallRound(BossArena.Centre + ArenaMapDir(PostAng) * (BossArena.Radius + 60.f + PostR), PostR, PostH, PostKind);
		}
	}
}

void ASQArena::PlanLamps(const USQData* D)
{
	LampSpots.Reset();
	const int32 MaxLamps = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapLamps"), 10.f)), 0, 24);
	if (MaxLamps <= 0 || GridN <= 0) return;
	TArray<FVector2D> Cands;
	// Beside the escort road every ~18 m, alternating sides (they light the cart's way)
	if (RoadPts.Num() >= 2)
	{
		float Acc = 0.f;
		int32 LampIdx = 0;
		for (int32 i = 1; i < RoadPts.Num(); ++i)
		{
			Acc += static_cast<float>(FVector2D::Distance(RoadPts[i - 1], RoadPts[i]));
			if (Acc < 1800.f) continue;
			Acc = 0.f;
			const FVector2D Tangent = (RoadPts[i] - RoadPts[i - 1]).GetSafeNormal();
			const FVector2D Side(-Tangent.Y, Tangent.X);
			Cands.Add(RoadPts[i] + Side * ((LampIdx % 2 == 0 ? 1.f : -1.f) * (RoadHalfWidth + 170.f)));
			++LampIdx;
		}
	}
	// One at every landmark, on the side facing the hub
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind != ESQZoneKind::Landmark) continue;
		const FVector2D In = (-Zone.Centre).GetSafeNormal();
		Cands.Add(Zone.Centre + In * (Zone.Radius * 0.92f) + FVector2D(-In.Y, In.X) * 300.f);
	}
	for (const FVector2D& Cand : Cands)
	{
		if (LampSpots.Num() >= MaxLamps) break;
		const float CX = static_cast<float>(Cand.X);
		const float CY = static_cast<float>(Cand.Y);
		if (FMath::Abs(CX) > Half - 400.f || FMath::Abs(CY) > Half - 400.f) continue;
		if (CX * CX + CY * CY < HubRadius * HubRadius) continue;
		if (GroundSlopeDeg(CX, CY) > 14.f || GroundZ(CX, CY) < -15.f) continue;
		if (IsWallAt(CX, CY, 120.f)) continue;
		if (DistToRoad(CX, CY) < RoadHalfWidth + 60.f) continue;
		bool bClear = true;
		for (const FVector2D& Pad : SpawnerSpots)
		{
			if (FVector2D::Distance(Pad, Cand) < ArenaMapPadClear + 100.f) bClear = false;
		}
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind != ESQZoneKind::Hub && FVector2D::Distance(Zone.Centre, Cand) < Zone.Radius * 0.75f) bClear = false;
		}
		if (!bClear) continue;
		LampSpots.Add(FVector(CX, CY, GroundZ(CX, CY)));
	}
}

void ASQArena::BuildLayoutWalls(const FSQArenaPalette& Pal, float PropScale, bool bArt)
{
	if (MapWalls.Num() == 0) return;
	const bool bVisuals = GetNetMode() != NM_DedicatedServer;
	const USQData* D = USQData::Get(this);
	const float FadeH = D ? D->Tune(TEXT("MapFadeHeight"), 200.f) : 200.f;
	UStaticMesh* CubeMesh = SQ::Mesh(TEXT("Cube"));
	UStaticMesh* CylMesh = SQ::Mesh(TEXT("Cylinder"));
	if (!CubeMesh || !CylMesh) return;

	// Colliders on every machine: hidden boxes / cylinders that match the horde's capsules. Camera-transparent.
	UInstancedStaticMeshComponent* BoxCol = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Wall), true, false);
	UInstancedStaticMeshComponent* CylCol = MakeArtISM(CylMesh, SurfaceMID(this, Pal.Wall), true, false);
	BoxCol->SetVisibility(false);
	CylCol->SetVisibility(false);
	BoxCol->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CylCol->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	PropBlockers.Add(BoxCol);
	PropBlockers.Add(CylCol);

	// Visual sets (low walls are instanced; anything taller than MapFadeHeight is its own SQFadeable component)
	const FLinearColor Brass(0.55f, 0.40f, 0.15f);
	const FLinearColor Wood = FMath::Lerp(FLinearColor(0.30f, 0.19f, 0.10f), Pal.Prop, 0.25f);
	UInstancedStaticMeshComponent* WallVis = nullptr;
	UInstancedStaticMeshComponent* CapVis = nullptr;
	UInstancedStaticMeshComponent* GearVis = nullptr;
	UInstancedStaticMeshComponent* GearCapVis = nullptr;
	UInstancedStaticMeshComponent* PlinthVis = nullptr;
	UInstancedStaticMeshComponent* SlabVis = nullptr;
	UInstancedStaticMeshComponent* GraveArtVis = nullptr;
	UInstancedStaticMeshComponent* FenceArtVis = nullptr;
	UInstancedStaticMeshComponent* FenceBarVis = nullptr;
	UInstancedStaticMeshComponent* CrateVis = nullptr;
	UInstancedStaticMeshComponent* CrateLidVis = nullptr;
	UInstancedStaticMeshComponent* WaterVis = nullptr;
	UInstancedStaticMeshComponent* KerbVis = nullptr;
	UStaticMesh* GraveArt = nullptr;
	UStaticMesh* FenceArt = nullptr;
	UStaticMesh* CryptArt = nullptr;
	UStaticMesh* PillarArt = nullptr;
	float GraveS = 1.f;
	float FenceS = 1.f;
	float FenceArtLen = 0.f;
	bool bFenceAlongX = true;
	if (bVisuals)
	{
		WallVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Wall * 1.15f), false, true);
		CapVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Cap), false, false);
		GearVis = MakeArtISM(CubeMesh, SurfaceMID(this, Brass * 0.8f), false, true);
		GearCapVis = MakeArtISM(CubeMesh, SurfaceMID(this, Brass * 1.5f, 0.1f), false, false);
		PlinthVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Cliff * 1.1f), false, true);
		CrateVis = MakeArtISM(CubeMesh, SurfaceMID(this, Wood), false, true);
		CrateLidVis = MakeArtISM(CubeMesh, SurfaceMID(this, Wood * 1.6f), false, false);
		WaterVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Water, 0.15f), false, false);
		KerbVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Cap * 0.9f), false, true);
		FenceBarVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Wall * 1.4f), false, true);
		if (bArt)
		{
			GraveArt = SQArt::Prop(TEXT("gravestone"));
			if (!GraveArt) GraveArt = SQArt::Prop(TEXT("grave_A"));
			FenceArt = SQArt::Prop(TEXT("fence"));
			CryptArt = ArenaFirstProp(Pal.TallProps);
			PillarArt = SQArt::Prop(TEXT("pillar"));
		}
		if (GraveArt)
		{
			GraveS = FitMeshHeight(GraveArt, 95.f);
			GraveArtVis = MakeArtISM(GraveArt, SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Pal.Prop, 0.3f)), false, true);
		}
		else
		{
			SlabVis = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Prop * 1.1f), false, true);
		}
		if (FenceArt)
		{
			FenceS = FitMeshHeight(FenceArt, 120.f);
			const FVector FenceExt = FenceArt->GetBounds().BoxExtent;
			bFenceAlongX = FenceExt.X >= FenceExt.Y;
			FenceArtLen = static_cast<float>(FMath::Max(FenceExt.X, FenceExt.Y)) * 2.f * FenceS;
			FenceArtVis = MakeArtISM(FenceArt, SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Pal.Wall, 0.35f)), false, true);
		}
	}

	int32 RoundIdx = 0;
	for (const FSQMapWall& W : MapWalls)
	{
		// Ground under the footprint
		float Lo = TNumericLimits<float>::Max();
		float Hi = -TNumericLimits<float>::Max();
		for (int32 s = 0; s <= 4; ++s)
		{
			const FVector2D P = FMath::Lerp(W.A, W.B, static_cast<double>(s) / 4.0);
			const float GZ = GroundZ(static_cast<float>(P.X), static_cast<float>(P.Y));
			Lo = FMath::Min(Lo, GZ);
			Hi = FMath::Max(Hi, GZ);
		}
		const float ColH = W.Kind == ESQWallKind::Water ? 320.f : FMath::Max(W.Height, 150.f);
		const float ColBottom = Lo - 60.f;
		const float ColTop = Hi + ColH;
		const float AX = static_cast<float>(W.A.X);
		const float AY = static_cast<float>(W.A.Y);

		if (W.IsRound())
		{
			CylCol->AddInstance(FTransform(FRotator::ZeroRotator, FVector(AX, AY, (ColBottom + ColTop) * 0.5f),
				FVector(W.HalfThick / 50.f, W.HalfThick / 50.f, (ColTop - ColBottom) / 100.f)));
			if (!bVisuals) continue;
			const bool bTall = W.Height > FadeH;
			const float BaseZ = Lo - 4.f;
			const float HT = W.HalfThick;
			const float PartH = W.Height;
			TArray<UStaticMeshComponent*, TInlineAllocator<3>> RoundParts;
			if (W.Kind == ESQWallKind::Crypt)
			{
				if (CryptArt)
				{
					const FVector CryptExt = CryptArt->GetBounds().BoxExtent;
					const float CryptS = FMath::Min(FitMeshHeight(CryptArt, PartH), HT * 1.15f / FMath::Max(1.f, static_cast<float>(FMath::Max(CryptExt.X, CryptExt.Y))));
					const float CryptYaw = FMath::RadiansToDegrees(FMath::Atan2(-AY, -AX));
					RoundParts.Add(AddArtPart(CryptArt, ArenaSitTransform(CryptArt, FVector(AX, AY, BaseZ), FQuat(FRotator(0.f, CryptYaw, 0.f)), FVector(CryptS)),
						SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Pal.Wall, 0.3f)), false));
				}
				else
				{
					RoundParts.Add(AddBlock(TEXT("Cube"), FVector(AX, AY, BaseZ + PartH * 0.35f), FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(-AY, -AX)), 0.f),
						FVector(HT * 1.8f / 100.f, HT * 1.4f / 100.f, PartH * 0.7f / 100.f), Pal.Wall * 1.2f, false));
					RoundParts.Add(AddBlock(TEXT("Cone"), FVector(AX, AY, BaseZ + PartH * 0.85f), FRotator::ZeroRotator,
						FVector(HT * 1.9f / 100.f, HT * 1.9f / 100.f, PartH * 0.3f / 100.f), Pal.Cap, false));
				}
			}
			else if (W.Kind == ESQWallKind::Tent)
			{
				const bool bStripe = (RoundIdx % 2) == 0;
				RoundParts.Add(AddBlock(TEXT("Cylinder"), FVector(AX, AY, BaseZ + PartH * 0.225f), FRotator::ZeroRotator,
					FVector(HT * 2.f / 100.f, HT * 2.f / 100.f, PartH * 0.45f / 100.f), bStripe ? Pal.Prop : Pal.Cap, false));
				RoundParts.Add(AddBlock(TEXT("Cone"), FVector(AX, AY, BaseZ + PartH * 0.725f), FRotator::ZeroRotator,
					FVector(HT * 2.3f / 100.f, HT * 2.3f / 100.f, PartH * 0.55f / 100.f), bStripe ? Pal.Cap : Pal.Prop, false));
			}
			else if (W.Kind == ESQWallKind::Gear)
			{
				RoundParts.Add(AddBlock(TEXT("Cylinder"), FVector(AX, AY, BaseZ + PartH * 0.5f), FRotator::ZeroRotator,
					FVector(HT * 2.f / 100.f, HT * 2.f / 100.f, PartH / 100.f), Brass * 0.8f, false));
				RoundParts.Add(AddBlock(TEXT("Cylinder"), FVector(AX, AY, BaseZ + PartH + 3.f), FRotator::ZeroRotator,
					FVector(HT * 1.2f / 100.f, HT * 1.2f / 100.f, 0.06f), Brass * 1.6f, false));
			}
			else if (PillarArt)
			{
				const FVector PillarExt = PillarArt->GetBounds().BoxExtent;
				const float PillarS = FMath::Min(FitMeshHeight(PillarArt, PartH), HT * 1.3f / FMath::Max(1.f, static_cast<float>(FMath::Max(PillarExt.X, PillarExt.Y))));
				RoundParts.Add(AddArtPart(PillarArt, ArenaSitTransform(PillarArt, FVector(AX, AY, BaseZ), FQuat::Identity, FVector(PillarS)),
					SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Pal.Wall, 0.3f)), false));
			}
			else
			{
				RoundParts.Add(AddBlock(TEXT("Cylinder"), FVector(AX, AY, BaseZ + PartH * 0.5f), FRotator::ZeroRotator,
					FVector(HT * 1.6f / 100.f, HT * 1.6f / 100.f, PartH / 100.f), Pal.Wall * 1.25f, false));
				RoundParts.Add(AddBlock(TEXT("Cube"), FVector(AX, AY, BaseZ + PartH + 5.f), FRotator::ZeroRotator,
					FVector(HT * 2.2f / 100.f, HT * 2.2f / 100.f, 0.14f), Pal.Cap, false));
			}
			for (UStaticMeshComponent* Part : RoundParts)
			{
				if (!Part) continue;
				Part->SetCastShadow(true);
				if (bTall) MarkFadeable(Part);
			}
			++RoundIdx;
			continue;
		}

		// Straight runs
		const FVector2D Delta = W.B - W.A;
		const float Len = static_cast<float>(Delta.Size());
		const FVector2D U = Delta / static_cast<double>(Len);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(U.Y), static_cast<float>(U.X)));
		const FRotator Rot(0.f, Yaw, 0.f);
		const FVector2D Mid = (W.A + W.B) * 0.5;
		const float MX = static_cast<float>(Mid.X);
		const float MY = static_cast<float>(Mid.Y);
		// The capsule's round ends reach HalfThick past A / B; the box goes half way (players and the horde agree closely)
		const float BoxLen = Len + W.HalfThick;
		BoxCol->AddInstance(FTransform(Rot, FVector(MX, MY, (ColBottom + ColTop) * 0.5f), FVector(BoxLen / 100.f, W.HalfThick * 2.f / 100.f, (ColTop - ColBottom) / 100.f)));
		if (!bVisuals) continue;
		const float WallTopZ = Hi + W.Height;
		switch (W.Kind)
		{
		case ESQWallKind::GraveRow:
		{
			// A low plinth with a row of headstones on it
			PlinthVis->AddInstance(FTransform(Rot, FVector(MX, MY, (Lo + Hi) * 0.5f), FVector(BoxLen / 100.f, (W.HalfThick * 2.f + 10.f) / 100.f, (Hi - Lo + 60.f) / 100.f)));
			const int32 Stones = FMath::Max(1, FMath::FloorToInt(Len / 170.f));
			for (int32 StoneIdx = 0; StoneIdx < Stones; ++StoneIdx)
			{
				const FVector2D P = W.A + U * ((static_cast<float>(StoneIdx) + 0.5f) * Len / static_cast<float>(Stones));
				const float PX = static_cast<float>(P.X);
				const float PY = static_cast<float>(P.Y);
				const float Jit = FMath::Sin((PX + PY) * 0.013f) * 7.f;
				if (GraveArtVis) GraveArtVis->AddInstance(ArenaSitTransform(GraveArt, FVector(PX, PY, Hi + 28.f), FQuat(FRotator(0.f, Yaw + 90.f + Jit, 0.f)), FVector(GraveS)));
				else if (SlabVis) SlabVis->AddInstance(FTransform(FRotator(0.f, Yaw + 90.f + Jit, 0.f), FVector(PX, PY, Hi + 28.f + 45.f), FVector(0.18f, 0.7f, 0.9f)));
			}
			break;
		}
		case ESQWallKind::Fence:
		{
			if (FenceArtVis && FenceArtLen > 10.f)
			{
				const int32 Pieces = FMath::Max(1, FMath::RoundToInt(Len / FenceArtLen));
				const float PieceLen = Len / static_cast<float>(Pieces);
				FVector FenceScale(FenceS);
				if (bFenceAlongX) FenceScale.X *= PieceLen / FenceArtLen;
				else FenceScale.Y *= PieceLen / FenceArtLen;
				const float FenceYaw = bFenceAlongX ? Yaw : Yaw + 90.f;
				for (int32 PieceIdx = 0; PieceIdx < Pieces; ++PieceIdx)
				{
					const FVector2D P = W.A + U * ((static_cast<float>(PieceIdx) + 0.5f) * PieceLen);
					const float PX = static_cast<float>(P.X);
					const float PY = static_cast<float>(P.Y);
					FenceArtVis->AddInstance(ArenaSitTransform(FenceArt, FVector(PX, PY, GroundZ(PX, PY) - 3.f), FQuat(FRotator(0.f, FenceYaw, 0.f)), FenceScale));
				}
			}
			else
			{
				FenceBarVis->AddInstance(FTransform(Rot, FVector(MX, MY, Hi + 85.f), FVector(BoxLen / 100.f, 0.14f, 0.12f)));
				FenceBarVis->AddInstance(FTransform(Rot, FVector(MX, MY, Hi + 40.f), FVector(BoxLen / 100.f, 0.14f, 0.12f)));
				const int32 Posts = FMath::Max(1, FMath::RoundToInt(Len / 220.f));
				for (int32 PostIdx = 0; PostIdx <= Posts; ++PostIdx)
				{
					const FVector2D P = W.A + U * (static_cast<float>(PostIdx) * Len / static_cast<float>(Posts));
					const float PX = static_cast<float>(P.X);
					const float PY = static_cast<float>(P.Y);
					FenceBarVis->AddInstance(FTransform(Rot, FVector(PX, PY, GroundZ(PX, PY) + 55.f), FVector(0.16f, 0.16f, 1.15f)));
				}
			}
			break;
		}
		case ESQWallKind::Crates:
		{
			// Stacks of crates, alternating heights, each with a pale lid (reads as boxes from above)
			const int32 Chunks = FMath::Max(1, FMath::RoundToInt(Len / 260.f));
			const float ChunkLen = BoxLen / static_cast<float>(Chunks);
			for (int32 ChunkIdx = 0; ChunkIdx < Chunks; ++ChunkIdx)
			{
				const float Along = -BoxLen * 0.5f + (static_cast<float>(ChunkIdx) + 0.5f) * ChunkLen;
				const FVector2D P = Mid + U * Along;
				const float PX = static_cast<float>(P.X);
				const float PY = static_cast<float>(P.Y);
				const float CrateH = (ChunkIdx % 2 == 0) ? 150.f : 190.f;
				const float CrateBase = GroundZ(PX, PY) - 10.f;
				const FRotator CrateRot(0.f, Yaw + static_cast<float>((ChunkIdx % 3) - 1) * 4.f, 0.f);
				CrateVis->AddInstance(FTransform(CrateRot, FVector(PX, PY, CrateBase + CrateH * 0.5f), FVector((ChunkLen - 14.f) / 100.f, (W.HalfThick * 2.f - 12.f) / 100.f, CrateH / 100.f)));
				CrateLidVis->AddInstance(FTransform(CrateRot, FVector(PX, PY, CrateBase + CrateH + 4.f), FVector((ChunkLen - 30.f) / 100.f, (W.HalfThick * 2.f - 30.f) / 100.f, 0.08f)));
			}
			break;
		}
		case ESQWallKind::Water:
		{
			// The surface sits below the shore (the ground is cut down under it); crisp pale kerbs mark both shores
			const FVector2D Perp(-U.Y, U.X);
			float ShoreLo = TNumericLimits<float>::Max();
			for (int32 s = 0; s <= 4; ++s)
			{
				const FVector2D OnLine = FMath::Lerp(W.A, W.B, static_cast<double>(s) / 4.0);
				for (int32 ShoreSide = 0; ShoreSide < 2; ++ShoreSide)
				{
					const FVector2D Shore = OnLine + Perp * ((ShoreSide == 0 ? 1.f : -1.f) * (W.HalfThick + 30.f));
					ShoreLo = FMath::Min(ShoreLo, GroundZ(static_cast<float>(Shore.X), static_cast<float>(Shore.Y)));
				}
			}
			WaterVis->AddInstance(FTransform(Rot, FVector(MX, MY, ShoreLo - 32.f), FVector(BoxLen / 100.f, W.HalfThick * 2.f / 100.f, 0.04f)));
			for (int32 ShoreSide = 0; ShoreSide < 2; ++ShoreSide)
			{
				const FVector2D Kerb = Mid + Perp * ((ShoreSide == 0 ? 1.f : -1.f) * (W.HalfThick - 10.f));
				KerbVis->AddInstance(FTransform(Rot, FVector(Kerb.X, Kerb.Y, ShoreLo + 6.f), FVector(BoxLen / 100.f, 0.3f, 0.3f)));
			}
			break;
		}
		case ESQWallKind::Stall:
		{
			// Market stalls: a booth and a striped awning per unit (tall: own components, SQFadeable)
			const int32 Units = FMath::Max(1, FMath::RoundToInt(Len / 380.f));
			const float UnitLen = BoxLen / static_cast<float>(Units);
			for (int32 UnitIdx = 0; UnitIdx < Units; ++UnitIdx)
			{
				const float Along = -BoxLen * 0.5f + (static_cast<float>(UnitIdx) + 0.5f) * UnitLen;
				const FVector2D P = Mid + U * Along;
				const float PX = static_cast<float>(P.X);
				const float PY = static_cast<float>(P.Y);
				const float StallBase = GroundZ(PX, PY) - 6.f;
				const float BoothH = W.Height * 0.72f;
				UStaticMeshComponent* Booth = AddBlock(TEXT("Cube"), FVector(PX, PY, StallBase + BoothH * 0.5f), Rot,
					FVector((UnitLen - 24.f) / 100.f, W.HalfThick * 2.f / 100.f, BoothH / 100.f), Pal.Wall * 1.25f, false);
				UStaticMeshComponent* Awning = AddBlock(TEXT("Cube"), FVector(PX, PY, StallBase + W.Height - 18.f), FRotator(0.f, Yaw, (UnitIdx % 2 == 0) ? 6.f : -6.f),
					FVector((UnitLen - 6.f) / 100.f, (W.HalfThick * 2.f + 50.f) / 100.f, 0.24f), (UnitIdx % 2 == 0) ? Pal.Prop : Pal.Cap, false);
				if (Booth) Booth->SetCastShadow(true);
				if (Awning) Awning->SetCastShadow(true);
				if (W.Height > FadeH)
				{
					MarkFadeable(Booth);
					MarkFadeable(Awning);
				}
			}
			break;
		}
		case ESQWallKind::Gear:
		{
			GearVis->AddInstance(FTransform(Rot, FVector(MX, MY, (Lo - 40.f + WallTopZ) * 0.5f), FVector(BoxLen / 100.f, W.HalfThick * 2.f / 100.f, (WallTopZ - Lo + 40.f) / 100.f)));
			GearCapVis->AddInstance(FTransform(Rot, FVector(MX, MY, WallTopZ + 6.f), FVector((BoxLen + 12.f) / 100.f, (W.HalfThick * 2.f + 16.f) / 100.f, 0.12f)));
			break;
		}
		default:
		{
			// Stone (and anything else straight): a solid wall with a pale cap - a crisp line from above
			WallVis->AddInstance(FTransform(Rot, FVector(MX, MY, (Lo - 40.f + WallTopZ) * 0.5f), FVector(BoxLen / 100.f, W.HalfThick * 2.f / 100.f, (WallTopZ - Lo + 40.f) / 100.f)));
			CapVis->AddInstance(FTransform(Rot, FVector(MX, MY, WallTopZ + 6.f), FVector((BoxLen + 12.f) / 100.f, (W.HalfThick * 2.f + 16.f) / 100.f, 0.12f)));
			break;
		}
		}
	}
	// PropScale is only used by the art fallbacks above through FitMeshHeight; keep the parameter for future dressing
	(void)PropScale;
}

// ---------------------------------------------------------------- terrain

void ASQArena::BuildHeightGrid(const USQData* D, int32 TerrainSeed)
{
	const float CellWanted = FMath::Clamp(D->Tune(TEXT("TerrainCell"), 100.f), 50.f, 400.f);
	GridN = FMath::Clamp(FMath::RoundToInt(Half * 2.f / CellWanted), 16, 256);
	CellSize = Half * 2.f / GridN;
	InvCellSize = 1.f / CellSize;
	GridMin = -Half;

	// Top-down maps: flat ground with a gentle roll (never enough to hide anyone), low tiers, water cuts, a small edge rise
	const float Roll = FMath::Clamp(D->Tune(TEXT("MapRoll"), 25.f), 0.f, 60.f);
	const float EdgeRise = FMath::Clamp(D->Tune(TEXT("MapEdgeRise"), 60.f), 0.f, 150.f);
	const float EdgeBand = 700.f;
	const float WaterDepth = 80.f;
	const float WaterFall = 250.f;
	const float NoiseOff = static_cast<float>(TerrainSeed % 991) * 1.31f;
	MacroNoiseOff = static_cast<float>(TerrainSeed % 977) * 1.73f;
	const bool bMacro = !MapShape.IsNone();
	const int32 Row = GridN + 1;
	Heights.SetNumUninitialized(Row * Row);
	for (int32 IY = 0; IY <= GridN; ++IY)
	{
		const float VY = GridMin + IY * CellSize;
		for (int32 IX = 0; IX <= GridN; ++IX)
		{
			const float VX = GridMin + IX * CellSize;
			float H = Roll * FMath::PerlinNoise2D(FVector2D(VX * 0.00035f + NoiseOff, VY * 0.00035f - NoiseOff));
			// Mission floors (hub, zones, road) are dead level
			float Flat = Smooth01((FMath::Sqrt(VX * VX + VY * VY) - HubRadius) / 600.f);
			for (const FSQMapZone& Zone : MapZones)
			{
				if (Zone.Kind == ESQZoneKind::Hub) continue;
				Flat *= Smooth01((static_cast<float>(FVector2D::Distance(Zone.Centre, FVector2D(VX, VY))) - Zone.Radius) / 500.f);
			}
			Flat *= Smooth01((DistToRoad(VX, VY) - RoadHalfWidth) / 400.f);
			if (Lanes.Num() > 0) Flat *= Smooth01((DistToLane(VX, VY) - LaneHalfWidth) / 300.f);
			H *= Flat;
			// Map revamp: the districts' skate ground (banks, bowls, rollers...) - never on a mission floor, road or lane
			if (SkateFeatures.Num() > 0) H += SkateHeight(VX, VY) * Flat;
			// Tiers: H + max(0, V - max(H, 0)) == max(H, V) on raised ground, continuous where a ramp meets lower ground
			for (const FSQTerrainPlateau& Tier : Plateaus)
			{
				const float PX = VX - static_cast<float>(Tier.Centre.X);
				const float PY = VY - static_cast<float>(Tier.Centre.Y);
				const float PD = FMath::Sqrt(PX * PX + PY * PY);
				const float TopR = Tier.RadiusAt(FMath::Atan2(PY, PX));
				const float PV = Tier.Height * (1.f - Smooth01((PD - TopR) / Tier.Ramp));
				H += FMath::Max(0.f, PV - FMath::Max(H, 0.f));
			}
			// Three-map remaster: the map's large shape (graveyard terraces / harbour causeways / the clocktower climb)
			if (bMacro) H += MacroHeight(VX, VY);
			// Water: cut down inside the water walls only (the shore outside the collider stays level)
			for (int32 w = 0; w < WaterA.Num(); ++w)
			{
				const float WD = DistToSegment2D(VX, VY, static_cast<float>(WaterA[w].X), static_cast<float>(WaterA[w].Y), static_cast<float>(WaterB[w].X), static_cast<float>(WaterB[w].Y));
				if (WD >= WaterHalf[w]) continue;
				H -= WaterDepth * Smooth01((WaterHalf[w] - WD) / WaterFall);
			}
			// The edges rise a little into the walls (frames the arena)
			const float EdgeDist = Half - FMath::Max(FMath::Abs(VX), FMath::Abs(VY));
			H += EdgeRise * (1.f - Smooth01(EdgeDist / EdgeBand));
			Heights[IY * Row + IX] = H;
		}
	}
	// Walkability: limit the step between neighbouring vertices (tiers are designed under it; water banks are inside colliders)
	const float MaxSlopeDeg = FMath::Clamp(D->Tune(TEXT("MapMaxSlope"), 26.f), 10.f, 40.f);
	ClampTerrainSlopes(CellSize * FMath::Tan(FMath::DegreesToRadians(MaxSlopeDeg)));
	RefreshTerrainRange();
}

void ASQArena::ClampTerrainSlopes(float MaxStep)
{
	if (GridN <= 0 || Heights.Num() != (GridN + 1) * (GridN + 1)) return;
	const int32 Row = GridN + 1;
	for (int32 Pass = 0; Pass < 64; ++Pass)
	{
		bool bChanged = false;
		for (int32 IY = 0; IY <= GridN; ++IY)
		{
			for (int32 IX = 0; IX <= GridN; ++IX)
			{
				const int32 Idx = IY * Row + IX;
				float H = Heights[Idx];
				if (IX > 0) H = FMath::Min(H, Heights[Idx - 1] + MaxStep);
				if (IY > 0) H = FMath::Min(H, Heights[Idx - Row] + MaxStep);
				if (H < Heights[Idx])
				{
					if (H < Heights[Idx] - 0.01f) bChanged = true;
					Heights[Idx] = H;
				}
			}
		}
		for (int32 IY = GridN; IY >= 0; --IY)
		{
			for (int32 IX = GridN; IX >= 0; --IX)
			{
				const int32 Idx = IY * Row + IX;
				float H = Heights[Idx];
				if (IX < GridN) H = FMath::Min(H, Heights[Idx + 1] + MaxStep);
				if (IY < GridN) H = FMath::Min(H, Heights[Idx + Row] + MaxStep);
				if (H < Heights[Idx])
				{
					if (H < Heights[Idx] - 0.01f) bChanged = true;
					Heights[Idx] = H;
				}
			}
		}
		if (!bChanged) break;
	}
}

void ASQArena::RefreshTerrainRange()
{
	if (Heights.Num() == 0) return;
	TerrainMinZ = Heights[0];
	TerrainMaxZ = Heights[0];
	for (const float V : Heights)
	{
		TerrainMinZ = FMath::Min(TerrainMinZ, V);
		TerrainMaxZ = FMath::Max(TerrainMaxZ, V);
	}
}

void ASQArena::BuildTerrainMesh(const FSQArenaPalette& Pal, int32 TerrainSeed)
{
	if (GridN <= 0) return;

	Terrain = NewObject<UProceduralMeshComponent>(this);
	Terrain->SetupAttachment(Root);
	Terrain->bUseAsyncCooking = false;              // collision must exist before anyone spawns on it
	Terrain->bUseComplexAsSimpleCollision = true;
	Terrain->SetCollisionProfileName(TEXT("BlockAll"));
	Terrain->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Terrain->SetCastShadow(true);
	Terrain->SetCanEverAffectNavigation(false);    // bots steer directly; no navmesh rebuild for a 112m mesh
	Terrain->RegisterComponent();

	const int32 Row = GridN + 1;
	const USQData* D = USQData::Get(this);
	const float Bright = D ? FMath::Clamp(D->Tune(TEXT("MapGroundBrightness"), 1.2f), 0.5f, 2.5f) : 1.2f;
	const float EdgeDarken = D ? FMath::Clamp(D->Tune(TEXT("MapEdgeDarken"), 0.5f), 0.1f, 1.f) : 0.5f;
	const float EdgeBand = D ? FMath::Clamp(D->Tune(TEXT("MapEdgeBand"), 1500.f), 200.f, 4000.f) : 1500.f;

	// --- Per-vertex normals and painted colours, tuned to read from a fixed top-down camera: soft two-tone ground,
	// pale tier tops with stony ramps, a crisp road with dark edges, paved landmark floors and a boss floor ringed by
	// bright lines, scorched spawner pads, dark water, warm pools under the lamps and a darker band along the walls.
	const float NoiseOff = static_cast<float>(TerrainSeed % 997) * 1.37f;
	const float PoolR = 560.f;
	const float TopRef = FMath::Max(80.f, TerrainMaxZ * 0.5f);
	const FLinearColor Paved = FMath::Lerp(Pal.Path, Pal.Top, 0.45f) * 1.1f;
	const FLinearColor BossFloor = FMath::Lerp(Pal.Basin, Pal.Path, 0.35f) * 1.25f;
	const FLinearColor PadColor = Pal.Basin * 0.55f;
	const FLinearColor PadRing = ArenaHue(Pal.Lamp) * 0.55f;
	// Map art pass (Nov 10 2026): a restrained ground. One narrow value band (so reapers, the horde, pickups and the
	// interactable cues are the brightest things on screen), less chroma, no bright painted rings or stripes: worn dirt
	// paths with ragged edges, bare-earth patches, soft contact shadow along the walls, concrete under the skate ground.
	const float GroundDesat = D ? FMath::Clamp(D->Tune(TEXT("MapGroundDesat"), 0.3f), 0.f, 1.f) : 0.3f;
	const float DistrictTint = D ? FMath::Clamp(D->Tune(TEXT("MapDistrictTint"), 0.06f), 0.f, 0.4f) : 0.06f;
	const float WallShade = D ? FMath::Clamp(D->Tune(TEXT("MapWallShade"), 0.3f), 0.f, 0.8f) : 0.3f;
	// Style pass (Nov 12 2026): StyleGroundVivid pushes the chroma back up (and past the palette) for a bolder floor
	const float GroundVivid = D ? FMath::Clamp(D->Tune(TEXT("StyleGroundVivid"), 0.3f), 0.f, 1.f) : 0.3f;
	auto Muted = [GroundDesat, GroundVivid](const FLinearColor& In) -> FLinearColor
	{
		const float Lum = 0.3f * In.R + 0.59f * In.G + 0.11f * In.B;
		const float Chroma = (1.f - GroundDesat) * (1.f + GroundVivid);
		return FLinearColor(FMath::Max(0.f, Lum + (In.R - Lum) * Chroma), FMath::Max(0.f, Lum + (In.G - Lum) * Chroma),
			FMath::Max(0.f, Lum + (In.B - Lum) * Chroma), 1.f);
	};
	const FLinearColor GrassA = Muted(Pal.Grass);
	const FLinearColor GrassB = Muted(Pal.GrassAlt);
	const FLinearColor BareEarth = Muted(FMath::Lerp(Pal.Path, Pal.Grass, 0.25f)) * 0.82f;
	const FLinearColor RoadTone = Muted(Pal.Road);
	const FLinearColor Concrete = Muted(FMath::Lerp(Pal.Cap, Pal.Cliff, 0.55f)) * 0.8f;
	const FLinearColor BossRim(0.30f, 0.05f, 0.07f);
	// Distance from every vertex to the nearest layout wall (rasterised per wall: cheap), for the contact shade
	TArray<float> WallNear;
	WallNear.Init(1.e9f, Row * Row);
	const float WallReach = 260.f;
	for (const FSQMapWall& W : MapWalls)
	{
		if (W.Kind == ESQWallKind::Water) continue;
		const float Pad = W.HalfThick + WallReach;
		const int32 X0 = FMath::Clamp(FMath::FloorToInt((static_cast<float>(FMath::Min(W.A.X, W.B.X)) - Pad - GridMin) * InvCellSize), 0, GridN);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt((static_cast<float>(FMath::Max(W.A.X, W.B.X)) + Pad - GridMin) * InvCellSize), 0, GridN);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt((static_cast<float>(FMath::Min(W.A.Y, W.B.Y)) - Pad - GridMin) * InvCellSize), 0, GridN);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt((static_cast<float>(FMath::Max(W.A.Y, W.B.Y)) + Pad - GridMin) * InvCellSize), 0, GridN);
		for (int32 WY = Y0; WY <= Y1; ++WY)
		{
			for (int32 WX = X0; WX <= X1; ++WX)
			{
				const float WallD = DistToSegment2D(GridMin + WX * CellSize, GridMin + WY * CellSize, static_cast<float>(W.A.X), static_cast<float>(W.A.Y),
					static_cast<float>(W.B.X), static_cast<float>(W.B.Y)) - W.HalfThick;
				float& Slot = WallNear[WY * Row + WX];
				Slot = FMath::Min(Slot, WallD);
			}
		}
	}
	// Ground art pass (Nov 15 2026): flat art in the surface itself (M_GroundV3 draws it from these masks; older
	// materials ignore them). VMask.X = stone (flagstones / tiles), VMask.Y = wood (planks), VEmblem = plaza-local
	// position / emblem radius (the mosaic), vertex alpha below 1 = wet tidal mud. Plus painted moss along walls and
	// worn, scorched ground where the lanterns stand. Nothing here is a mesh, so nothing can read as a pickup.
	const float GroundArt = D ? FMath::Clamp(D->Tune(TEXT("MapGroundArt"), 1.f), 0.f, 1.f) : 1.f;
	const float MossAmount = D ? FMath::Clamp(D->Tune(TEXT("MapGroundMoss"), 0.35f), 0.f, 1.f) : 0.35f;
	const bool bPlankMap = MapShape == FName(TEXT("Causeways"));
	const bool bTileMap = MapShape == FName(TEXT("Climb"));
	const FLinearColor Moss = Muted(FMath::Lerp(Pal.GrassAlt, Pal.Basin, 0.45f)) * 0.7f;
	const FLinearColor Mud = Muted(FMath::Lerp(Pal.Path, Pal.Basin, 0.5f)) * 0.62f;
	const FLinearColor PlankTone = Muted(FMath::Lerp(Pal.Road, Pal.Path, 0.5f)) * 0.95f;
	TArray<FVector2D> LanternWear;
	LanternWear.Add(FVector2D::ZeroVector);
	for (const FSQDistrict& WearDist : Districts) LanternWear.Add(WearDist.Centre + (-WearDist.Centre).GetSafeNormal() * 750.f);
	TArray<FVector> VNormals;
	TArray<FLinearColor> VColors;
	TArray<FVector2D> VMask;
	TArray<FVector2D> VEmblem;
	VNormals.SetNumUninitialized(Row * Row);
	VColors.SetNumUninitialized(Row * Row);
	VMask.Init(FVector2D::ZeroVector, Row * Row);
	VEmblem.Init(FVector2D(9.0, 9.0), Row * Row);
	for (int32 IY = 0; IY <= GridN; ++IY)
	{
		const float VY = GridMin + IY * CellSize;
		for (int32 IX = 0; IX <= GridN; ++IX)
		{
			const float VX = GridMin + IX * CellSize;
			const int32 XL = FMath::Max(IX - 1, 0);
			const int32 XR = FMath::Min(IX + 1, GridN);
			const int32 YL = FMath::Max(IY - 1, 0);
			const int32 YR = FMath::Min(IY + 1, GridN);
			const float DZX = (GridHeight(XR, IY) - GridHeight(XL, IY)) / ((XR - XL) * CellSize);
			const float DZY = (GridHeight(IX, YR) - GridHeight(IX, YL)) / ((YR - YL) * CellSize);
			const int32 Idx = IY * Row + IX;
			VNormals[Idx] = FVector(-DZX, -DZY, 1.f).GetSafeNormal();

			const float H = Heights[Idx];
			const float Slope = FMath::Sqrt(DZX * DZX + DZY * DZY);
			const float N1 = FMath::PerlinNoise2D(FVector2D(VX * 0.00045f + NoiseOff, VY * 0.00045f - NoiseOff));
			const float N2 = FMath::PerlinNoise2D(FVector2D(VX * 0.0023f - NoiseOff, VY * 0.0023f + NoiseOff * 0.5f));
			const FVector2D VP(VX, VY);

			const float N3 = FMath::PerlinNoise2D(FVector2D(VX * 0.0061f + NoiseOff * 0.7f, VY * 0.0061f - NoiseOff * 1.3f));
			FLinearColor VC = FMath::Lerp(GrassA, GrassB, Smooth01(0.5f + N1 * 1.6f));
			VC *= 1.f + 0.07f * N2;
			// Bare-earth patches where two noise bands overlap (breaks the lawn up without adding contrast)
			VC = FMath::Lerp(VC, BareEarth, 0.6f * Smooth01((N3 + 0.5f * N2 - 0.18f) / 0.22f));
			// Tier tops pale, ramps stony
			VC = FMath::Lerp(VC, Muted(Pal.Top) * (0.92f + 0.06f * N2), 0.7f * Smooth01((H - 40.f) / TopRef));
			VC = FMath::Lerp(VC, Muted(Pal.Cliff) * 1.1f, Smooth01((Slope - 0.12f) / 0.16f));
			// Skate ground: poured concrete, a shade lighter toward the lip
			const float SkateH = SkateFeatures.Num() > 0 ? SkateHeight(VX, VY) : 0.f;
			if (FMath::Abs(SkateH) > 2.f)
			{
				VC = FMath::Lerp(VC, Concrete * (1.f + 0.05f * N2 + 0.12f * Smooth01(FMath::Abs(SkateH) / 180.f)), 0.85f * Smooth01(FMath::Abs(SkateH) / 22.f));
			}
			// Water bed (under the water plane); a dry bowl gets a smooth pale floor and a bright coping ring instead
			const bool bWet = WaterA.Num() > 0 && IsWaterAt(VX, VY);
			if (bWet) VC = FMath::Lerp(VC, Pal.Water * 0.6f, Smooth01((-H - 15.f) / 40.f));
			else if (H < -10.f) VC = FMath::Lerp(VC, FMath::Lerp(Pal.Cap, Pal.Path, 0.4f) * 0.85f, Smooth01((-H - 10.f) / 50.f));
			// Map revamp: each district's ground leans a little toward its colour (the minimap and the eye read the districts)
			for (const FSQDistrict& Dist : Districts)
			{
				const float DistD = static_cast<float>(FVector2D::Distance(Dist.Centre, VP));
				if (DistD > Dist.Radius) continue;
				VC *= FMath::Lerp(FLinearColor::White, Dist.Color, DistrictTint * (1.f - Smooth01((DistD - Dist.Radius * 0.7f) / (Dist.Radius * 0.3f))));
			}
			// Hub: a soft paved disc with a pale edge line
			const float CentreD = FMath::Sqrt(VX * VX + VY * VY);
			// (no bright edge line any more: the flagstones are the plaza; the rim is just trodden a little darker)
			VC = FMath::Lerp(VC, Muted(Paved) * 0.85f, 0.35f * (1.f - Smooth01((CentreD - HubRadius) / 80.f)));
			VC *= 1.f - 0.16f * (1.f - Smooth01(FMath::Abs(CentreD - HubRadius + N2 * 40.f) / 110.f));
			float PavedW = 1.f - Smooth01((CentreD - HubRadius) / 70.f);   // made surfaces: hub, plazas, (planked / tiled) lanes
			const float HubEmblemR = HubRadius * 0.82f;
			if (CentreD < HubEmblemR + 300.f) VEmblem[Idx] = FVector2D(VX, VY) / HubEmblemR;
			// Landmarks: paved floors with a bright edge ring; the boss arena: its own floor and a wider bright ring
			for (const FSQMapZone& Zone : MapZones)
			{
				if (Zone.Kind == ESQZoneKind::Hub) continue;
				const float ZoneD = static_cast<float>(FVector2D::Distance(Zone.Centre, VP));
				if (ZoneD > Zone.Radius + 120.f) continue;
				const bool bBoss = Zone.Kind == ESQZoneKind::Boss;
				VC = FMath::Lerp(VC, Muted(bBoss ? BossFloor : Paved) * 0.85f, (bBoss ? 0.45f : 0.5f) * (1.f - Smooth01((ZoneD - Zone.Radius) / 60.f)));
				// Rim: trodden dark; the boss floor keeps a deep crimson band (a warning you can read, not a neon line)
				const float RimBand = 1.f - Smooth01(FMath::Abs(ZoneD - Zone.Radius + N2 * 35.f) / (bBoss ? 90.f : 100.f));
				if (bBoss) VC = FMath::Lerp(VC, BossRim, 0.55f * RimBand);
				else VC *= 1.f - 0.16f * RimBand;
				PavedW = FMath::Max(PavedW, 1.f - Smooth01((ZoneD - Zone.Radius) / 70.f));
				const float ZoneEmblemR = Zone.Radius * 0.82f;
				if (ZoneD < ZoneEmblemR + 300.f) VEmblem[Idx] = (VP - Zone.Centre) / ZoneEmblemR;
			}
			// Spawner pads: scorched discs ringed in the lamp hue
			for (const FVector2D& Pad : SpawnerSpots)
			{
				const float PadD = static_cast<float>(FVector2D::Distance(Pad, VP));
				if (PadD > 460.f) continue;
				VC = FMath::Lerp(VC, PadColor, 0.8f * (1.f - Smooth01((PadD - 380.f) / 50.f)));
				VC = FMath::Lerp(VC, PadRing * 0.7f, 0.4f * (1.f - Smooth01(FMath::Abs(PadD - 380.f) / 34.f)));
			}
			// The escort road: packed dirt with darker kerb lines
			const float RoadD = DistToRoad(VX, VY);
			// (map art pass: worn in, not painted on. A ragged edge that wanders with the noise, a paler trodden middle
			// and a soft dark verge where the grass meets it)
			if (RoadD < RoadHalfWidth + 200.f)
			{
				const float RoadEdge = RoadD + N2 * 70.f + N3 * 45.f;
				const float RoadMix = 1.f - Smooth01((RoadEdge - (RoadHalfWidth - 110.f)) / 170.f);
				VC = FMath::Lerp(VC, RoadTone * (0.95f + 0.07f * N3 + 0.1f * (1.f - Smooth01(RoadD / (RoadHalfWidth * 0.55f)))), 0.92f * RoadMix);
				VC *= 1.f - 0.14f * (1.f - Smooth01(FMath::Abs(RoadEdge - RoadHalfWidth) / 110.f));
				if (bPlankMap || bTileMap) PavedW = FMath::Max(PavedW, RoadMix);
			}
			// Map revamp: hub lanes, paler packed dirt with dark edges (the chevrons on them point the way)
			if (Lanes.Num() > 0)
			{
				const float LaneD = DistToLane(VX, VY);
				if (LaneD < LaneHalfWidth + 180.f && RoadD > RoadHalfWidth)
				{
					const float LaneEdge = LaneD + N2 * 60.f + N3 * 40.f;
					const float LaneMix = 1.f - Smooth01((LaneEdge - (LaneHalfWidth - 100.f)) / 150.f);
					VC = FMath::Lerp(VC, RoadTone * (1.04f + 0.07f * N3 + 0.1f * (1.f - Smooth01(LaneD / (LaneHalfWidth * 0.55f)))), 0.85f * LaneMix);
					VC *= 1.f - 0.12f * (1.f - Smooth01(FMath::Abs(LaneEdge - LaneHalfWidth) / 100.f));
					if (bPlankMap || bTileMap) PavedW = FMath::Max(PavedW, LaneMix);
				}
			}
			for (const FVector& Lamp : LampSpots)
			{
				const float LD2 = FMath::Square(static_cast<float>(Lamp.X) - VX) + FMath::Square(static_cast<float>(Lamp.Y) - VY);
				if (LD2 >= PoolR * PoolR) continue;
				const float Pool = FMath::Square(1.f - FMath::Sqrt(LD2) / PoolR);
				VC = VC * (1.f + 0.45f * Pool) + Pal.Lamp * (0.07f * Pool);
			}
			// Ground art: moss creeps out from the foot of every wall onto soft ground (never onto paving)
			if (WallNear[Idx] < WallReach && GroundArt > 0.f)
			{
				const float MossBand = Smooth01(WallNear[Idx] / 60.f) * (1.f - Smooth01((WallNear[Idx] - 110.f) / 150.f));
				VC = FMath::Lerp(VC, Moss, GroundArt * MossAmount * MossBand * (1.f - PavedW) * FMath::Clamp(0.75f + N3 * 1.2f, 0.f, 1.f));
			}
			// Ground art: the ground is worn bare and a little scorched where a lantern stands
			for (const FVector2D& Wear : LanternWear)
			{
				const float WearD = static_cast<float>(FVector2D::Distance(Wear, VP)) + N2 * 60.f;
				if (WearD < 520.f) VC *= 1.f - GroundArt * 0.22f * (1.f - Smooth01((WearD - 240.f) / 280.f));
			}
			// Ground art: tidal flats are dark wet mud (vertex alpha below 1 = wet in M_GroundV3); quays are timber
			float Wet = 0.f;
			if (bPlankMap)
			{
				Wet = GroundArt * Smooth01((TideHighZ - 20.f - H) / 70.f);
				VC = FMath::Lerp(VC, Mud * (1.f + 0.08f * N2), 0.75f * Wet);
				VC = FMath::Lerp(VC, PlankTone * (1.f + 0.05f * N3), 0.55f * GroundArt * PavedW * (1.f - Wet));
			}
			VMask[Idx] = bPlankMap ? FVector2D(0.0, GroundArt * PavedW * (1.f - Wet)) : FVector2D(GroundArt * PavedW, 0.0);
			// Contact shade: the ground darkens toward every wall, so walls sit in the ground instead of floating on it
			if (WallNear[Idx] < WallReach) VC *= 1.f - WallShade * FMath::Square(1.f - FMath::Max(0.f, WallNear[Idx]) / WallReach);
			// Edge framing (map remaster): a deep, wide dark band pulls the eye to the playfield and frames the arena
			const float EdgeDist = Half - FMath::Max(FMath::Abs(VX), FMath::Abs(VY));
			VC *= FMath::Lerp(EdgeDarken, 1.f, Smooth01(EdgeDist / EdgeBand));
			VC *= Bright;
			VC.A = 1.f - 0.8f * Wet;
			VColors[Idx] = VC;
		}
	}

	// Split cells into sections. With M_Ground the colour comes from the vertex colours and every section shares one
	// material; without it (clay fallback ignores vertex colour) each section gets a flat colour so the levels still read:
	// 0 = plain ground, 1 = ramps (stony), 2 = tier tops (lighter), 3 = water beds (dark), 4 = road (dirt)
	const float SlopeTan = FMath::Tan(FMath::DegreesToRadians(8.f));
	FSQTerrainSection Sections[5];
	for (int32 IY = 0; IY < GridN; ++IY)
	{
		for (int32 IX = 0; IX < GridN; ++IX)
		{
			const float H00 = GridHeight(IX, IY);
			const float H10 = GridHeight(IX + 1, IY);
			const float H01 = GridHeight(IX, IY + 1);
			const float H11 = GridHeight(IX + 1, IY + 1);
			const float CellRise = FMath::Max(FMath::Max(FMath::Abs(H10 - H00), FMath::Abs(H11 - H01)),
				FMath::Max(FMath::Abs(H01 - H00), FMath::Abs(H11 - H10))) / CellSize;
			const float Avg = 0.25f * (H00 + H10 + H01 + H11);
			int32 Kind = 0;
			if (CellRise > SlopeTan) Kind = 1;
			else if (Avg > 60.f) Kind = 2;
			else if (Avg < -20.f && WaterA.Num() > 0 && IsWaterAt(GridMin + (IX + 0.5f) * CellSize, GridMin + (IY + 0.5f) * CellSize)) Kind = 3;
			else if (PathMask(GridMin + (IX + 0.5f) * CellSize, GridMin + (IY + 0.5f) * CellSize) > 0.5f) Kind = 4;

			FSQTerrainSection& Sec = Sections[Kind];
			const int32 First = Sec.Verts.Num();
			// Corners in order v00, v10, v11, v01
			const int32 CornerX[4] = { IX, IX + 1, IX + 1, IX };
			const int32 CornerY[4] = { IY, IY, IY + 1, IY + 1 };
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const int32 Idx = CornerY[Corner] * Row + CornerX[Corner];
				const float WX = GridMin + CornerX[Corner] * CellSize;
				const float WY = GridMin + CornerY[Corner] * CellSize;
				Sec.Verts.Add(FVector(WX, WY, Heights[Idx]));
				Sec.Normals.Add(VNormals[Idx]);
				Sec.UVs.Add(VMask[Idx]);
				Sec.UV1s.Add(VEmblem[Idx]);
				Sec.UV2s.Add(FVector2D(WX, WY));
				Sec.Colors.Add(VColors[Idx]);
			}
			// Diagonal v00 -> v11, matching GroundZ(). UE's front face for an upward-facing triangle (A, B, C) has
			// ((B - A) ^ (C - A)).Z < 0, i.e. CLOCKWISE when plotted with +X right / +Y up: v00 -> v11 -> v10 and v00 -> v01 -> v11.
			Sec.Tris.Add(First);     Sec.Tris.Add(First + 2); Sec.Tris.Add(First + 1);
			Sec.Tris.Add(First);     Sec.Tris.Add(First + 3); Sec.Tris.Add(First + 2);
		}
	}

	UMaterialInterface* GroundBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GroundV2.M_GroundV2"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!GroundBase) GroundBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ground.M_Ground"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInstanceDynamic* SharedMID = GroundBase ? SurfaceMID(this, FLinearColor::White) : nullptr;
	// Ground art: the terrain alone uses M_GroundV3 (patterns from the masks above) when the setup script has made it
	UMaterialInterface* ArtBase = GroundArt > 0.f
		? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GroundV3.M_GroundV3"), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	if (UMaterialInstanceDynamic* ArtMID = ArtBase ? UMaterialInstanceDynamic::Create(ArtBase, this) : nullptr)
	{
		ArtMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
		ArtMID->SetScalarParameterValue(TEXT("Glow"), 0.f);
		// Graveyard: staggered flagstones, 8-spoke emblem. Harbour: planks, 16-point compass. Clocktower: chequered tiles, 12-hour dial
		ArtMID->SetScalarParameterValue(TEXT("StoneW"), bTileMap ? 115.f : 135.f);
		ArtMID->SetScalarParameterValue(TEXT("StoneH"), bTileMap ? 115.f : 88.f);
		ArtMID->SetScalarParameterValue(TEXT("StoneStagger"), bTileMap ? 0.f : 1.f);
		ArtMID->SetScalarParameterValue(TEXT("StoneChecker"), bTileMap ? 0.16f : 0.f);
		ArtMID->SetScalarParameterValue(TEXT("EmblemSpokes"), bPlankMap ? 16.f : (bTileMap ? 12.f : 8.f));
		ArtMID->SetScalarParameterValue(TEXT("JointDark"), D ? FMath::Clamp(D->Tune(TEXT("MapGroundJoint"), 0.42f), 0.f, 0.9f) : 0.42f);
		SharedMID = ArtMID;
		UE_LOG(LogSQ, Log, TEXT("Arena: ground art on (M_GroundV3, %s)"), bPlankMap ? TEXT("planks") : (bTileMap ? TEXT("tiles") : TEXT("flagstones")));
	}
	else if (GroundArt > 0.f && GroundBase)
	{
		UE_LOG(LogSQ, Log, TEXT("Arena: M_GroundV3 not found - plain ground. Run Scripts/setup_project.py to add the ground art."));
	}
	const FLinearColor SectionColor[5] = { Pal.Grass * Bright, Pal.Cliff * Bright, Pal.Top * Bright, Pal.Water, Pal.Road * Bright };
	const TArray<FProcMeshTangent> NoTangents;
	int32 SectionIndex = 0;
	for (int32 Kind = 0; Kind < 5; ++Kind)
	{
		const FSQTerrainSection& Sec = Sections[Kind];
		if (Sec.Tris.Num() == 0) continue;
		Terrain->CreateMeshSection_LinearColor(SectionIndex, Sec.Verts, Sec.Tris, Sec.Normals, Sec.UVs, Sec.UV1s, Sec.UV2s, TArray<FVector2D>(),
			Sec.Colors, NoTangents, true);
		Terrain->SetMaterial(SectionIndex, SharedMID ? SharedMID : SQ::ColorMID(this, SectionColor[Kind]));
		++SectionIndex;
	}
	if (!GroundBase) UE_LOG(LogSQ, Warning, TEXT("Arena: /Game/Materials/M_Ground missing - flat clay ground. Run Scripts/setup_project.py (create_ground_material.py)."));
}

void ASQArena::BuildIceOverlay()
{
	if (!Terrain || IcePos.Num() == 0 || GetNetMode() == NM_DedicatedServer) return;

	// Polar discs whose vertices sit on GroundZ, so the ice hugs tiers and ramps
	const int32 Rings = 6;
	const int32 Segs = 36;
	FSQTerrainSection Ice;
	auto AddIceVert = [&](float VX, float VY)
	{
		Ice.Verts.Add(FVector(VX, VY, GroundZ(VX, VY) + 5.f));
		Ice.Normals.Add(GroundNormal(VX, VY));
		Ice.UVs.Add(FVector2D(VX / 400.f, VY / 400.f));
		Ice.Colors.Add(FLinearColor::White);
	};
	for (int32 p = 0; p < IcePos.Num(); ++p)
	{
		const float CX = static_cast<float>(IcePos[p].X);
		const float CY = static_cast<float>(IcePos[p].Y);
		const float R = IceRadius[p];
		const int32 Centre = Ice.Verts.Num();
		AddIceVert(CX, CY);
		for (int32 Ring = 1; Ring <= Rings; ++Ring)
		{
			const float RingR = R * Ring / Rings;
			for (int32 s = 0; s < Segs; ++s)
			{
				const float A = s * 2.f * PI / Segs;
				AddIceVert(CX + FMath::Cos(A) * RingR, CY + FMath::Sin(A) * RingR);
			}
		}
		auto RingVert = [Centre, Segs](int32 Ring, int32 s) { return Centre + 1 + (Ring - 1) * Segs + (s % Segs); };
		// Angles grow counter-clockwise (+X right / +Y up), so the upward-facing (clockwise) order is centre -> s+1 -> s
		for (int32 s = 0; s < Segs; ++s)
		{
			Ice.Tris.Add(Centre); Ice.Tris.Add(RingVert(1, s + 1)); Ice.Tris.Add(RingVert(1, s));
		}
		for (int32 Ring = 1; Ring < Rings; ++Ring)
		{
			for (int32 s = 0; s < Segs; ++s)
			{
				const int32 I0 = RingVert(Ring, s);
				const int32 I1 = RingVert(Ring, s + 1);
				const int32 O0 = RingVert(Ring + 1, s);
				const int32 O1 = RingVert(Ring + 1, s + 1);
				Ice.Tris.Add(I0); Ice.Tris.Add(O1); Ice.Tris.Add(O0);
				Ice.Tris.Add(I0); Ice.Tris.Add(I1); Ice.Tris.Add(O1);
			}
		}
	}
	const int32 SectionIndex = Terrain->GetNumSections();
	Terrain->CreateMeshSection_LinearColor(SectionIndex, Ice.Verts, Ice.Tris, Ice.Normals, Ice.UVs, Ice.Colors, TArray<FProcMeshTangent>(), false);
	Terrain->SetMaterial(SectionIndex, SurfaceMID(this, FLinearColor(0.62f, 0.86f, 1.f), 0.12f));
}

// ---------------------------------------------------------------- terrain queries

float ASQArena::GroundZ(float X, float Y) const
{
	if (GridN <= 0) return 0.f;
	const float GX = FMath::Clamp((X - GridMin) * InvCellSize, 0.f, static_cast<float>(GridN));
	const float GY = FMath::Clamp((Y - GridMin) * InvCellSize, 0.f, static_cast<float>(GridN));
	const int32 IX = FMath::Min(static_cast<int32>(GX), GridN - 1);
	const int32 IY = FMath::Min(static_cast<int32>(GY), GridN - 1);
	const float FX = GX - static_cast<float>(IX);
	const float FY = GY - static_cast<float>(IY);
	const int32 Row = GridN + 1;
	const float* Cell = Heights.GetData() + IY * Row + IX;
	const float H00 = Cell[0];
	const float H10 = Cell[1];
	const float H01 = Cell[Row];
	const float H11 = Cell[Row + 1];
	// Same diagonal split (v00 -> v11) as the mesh triangles, so this is exactly the rendered / collision surface
	return FX >= FY
		? H00 + FX * (H10 - H00) + FY * (H11 - H10)
		: H00 + FY * (H01 - H00) + FX * (H11 - H01);
}

float ASQArena::GroundZAt(const UObject* WorldContext, float X, float Y)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	const ASQGameState* GS = W ? W->GetGameState<ASQGameState>() : nullptr;
	return (GS && GS->Arena) ? GS->Arena->GroundZ(X, Y) : 0.f;
}

FVector ASQArena::GroundNormal(float X, float Y) const
{
	if (GridN <= 0) return FVector::UpVector;
	const float E = CellSize * 0.5f;
	const float DZX = (GroundZ(X + E, Y) - GroundZ(X - E, Y)) / (2.f * E);
	const float DZY = (GroundZ(X, Y + E) - GroundZ(X, Y - E)) / (2.f * E);
	return FVector(-DZX, -DZY, 1.f).GetSafeNormal();
}

float ASQArena::GroundSlopeDeg(float X, float Y) const
{
	const FVector N = GroundNormal(X, Y);
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(N.Z), -1.f, 1.f)));
}

float ASQArena::GroundRoughness(float X, float Y, float Radius) const
{
	if (GridN <= 0) return 0.f;
	float Lo = GroundZ(X, Y);
	float Hi = Lo;
	for (int32 k = 0; k < 16; ++k)
	{
		const float A = (k % 8) * 2.f * PI / 8.f;
		const float R = (k < 8) ? Radius : Radius * 0.5f;
		const float Z = GroundZ(X + FMath::Cos(A) * R, Y + FMath::Sin(A) * R);
		Lo = FMath::Min(Lo, Z);
		Hi = FMath::Max(Hi, Z);
	}
	return Hi - Lo;
}

// ---------------------------------------------------------------- map dressing

float ASQArena::PathMask(float X, float Y) const
{
	if (RoadPts.Num() < 2) return 0.f;
	const float Dist = DistToRoad(X, Y);
	if (Dist > RoadHalfWidth + 60.f) return 0.f;
	// A crisp edge (top-down: the road must read as a line, not a smudge)
	return 1.f - Smooth01((Dist - (RoadHalfWidth - 50.f)) / 60.f);
}

void ASQArena::BuildWalls(const FSQArenaPalette& Pal, float PropScale, bool bArt)
{
	UStaticMesh* CubeMesh = SQ::Mesh(TEXT("Cube"));
	if (!CubeMesh) return;
	// Low edge wall: the camera looks down over the near side, so it stays low (fadeable per side anyway)
	const float WallH = 110.f;
	const float Thick = 140.f;
	const float Line = Half + Thick * 0.5f;           // inner face flush with the arena edge
	const int32 Segs = FMath::Max(1, FMath::RoundToInt(Half * 2.f / 600.f));
	const float Step = Half * 2.f / Segs;

	UStaticMesh* FenceMesh = bArt ? SQArt::Prop(TEXT("fence")) : nullptr;
	UStaticMesh* PillarMesh = bArt ? SQArt::Prop(TEXT("fence_pillar")) : nullptr;
	UMaterialInstanceDynamic* FenceMID = (FenceMesh || PillarMesh) ? SQArt::TintMID(this, SQArt::PropTexture(), SQArt::SoftTint(Pal.Wall, 0.35f)) : nullptr;
	float FenceLen = 0.f;
	bool bFenceAlongX = true;
	if (FenceMesh)
	{
		const FVector FE = FenceMesh->GetBounds().BoxExtent;
		bFenceAlongX = FE.X >= FE.Y;
		FenceLen = static_cast<float>(FMath::Max(FE.X, FE.Y)) * 2.f * PropScale;
	}

	// One ISM set per side, so the camera can fade just the side between it and the player
	UInstancedStaticMeshComponent* SkirtISM = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Grass * 0.45f + Pal.Fog * 0.5f), false, false);
	for (int32 Side = 0; Side < 4; ++Side)
	{
		UInstancedStaticMeshComponent* WallISM = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Wall), false, true);
		UInstancedStaticMeshComponent* ColumnISM = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Wall * 1.3f), false, true);
		UInstancedStaticMeshComponent* FenceISM = FenceMesh ? MakeArtISM(FenceMesh, FenceMID, false, true) : nullptr;
		UInstancedStaticMeshComponent* PillarISM = PillarMesh ? MakeArtISM(PillarMesh, FenceMID, false, true) : nullptr;
		MarkFadeable(WallISM);
		MarkFadeable(ColumnISM);
		if (FenceISM) MarkFadeable(FenceISM);
		if (PillarISM) MarkFadeable(PillarISM);

		// Sides 0/1: X = +/-Line (run along Y). Sides 2/3: Y = +/-Line (run along X).
		const bool bRunsAlongY = Side < 2;
		const float Sign = (Side % 2 == 0) ? 1.f : -1.f;
		auto EdgeXY = [bRunsAlongY, Sign](float Along, float Across) { return bRunsAlongY ? FVector2D(Sign * Across, Along) : FVector2D(Along, Sign * Across); };
		auto EdgeGround = [&](float Along) { const FVector2D G = EdgeXY(Along, Half - 20.f); return GroundZ(static_cast<float>(G.X), static_cast<float>(G.Y)); };

		TArray<float> SegTop;
		SegTop.SetNum(Segs);
		float SideMin = TNumericLimits<float>::Max();
		for (int32 k = 0; k < Segs; ++k)
		{
			const float A0 = -Half + k * Step;
			const float Z0 = EdgeGround(A0);
			const float ZM = EdgeGround(A0 + Step * 0.5f);
			const float Z1 = EdgeGround(A0 + Step);
			const float Lo = FMath::Min3(Z0, ZM, Z1) - 90.f;
			const float Top = FMath::Max3(Z0, ZM, Z1) + WallH;
			SegTop[k] = Top;
			SideMin = FMath::Min(SideMin, Lo + 90.f);
			const FVector2D C = EdgeXY(A0 + Step * 0.5f, Line);
			const float AlongS = (Step + 4.f) / 100.f;
			const float AcrossS = Thick / 100.f;
			WallISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(C.X, C.Y, (Lo + Top) * 0.5f),
				bRunsAlongY ? FVector(AcrossS, AlongS, (Top - Lo) / 100.f) : FVector(AlongS, AcrossS, (Top - Lo) / 100.f)));

			// Fence pieces along the top of the wall, stretched a little to fill the segment exactly
			if (FenceISM && FenceLen > 10.f)
			{
				const int32 Pieces = FMath::Max(1, FMath::RoundToInt((Step - 60.f) / FenceLen));
				const float PieceLen = (Step - 60.f) / Pieces;
				FVector FenceScale(PropScale);
				if (bFenceAlongX) FenceScale.X *= PieceLen / FenceLen;
				else FenceScale.Y *= PieceLen / FenceLen;
				const float FenceYaw = (bRunsAlongY == bFenceAlongX) ? 90.f : 0.f;
				for (int32 Piece = 0; Piece < Pieces; ++Piece)
				{
					const FVector2D FP = EdgeXY(A0 + 30.f + (Piece + 0.5f) * PieceLen, Line);
					FenceISM->AddInstance(ArenaSitTransform(FenceMesh, FVector(FP.X, FP.Y, Top - 2.f), FQuat(FRotator(0.f, FenceYaw, 0.f)), FenceScale));
				}
			}
		}

		// Chunky stone columns at every joint (taller than both neighbours), a fence pillar on top when the art exists
		for (int32 k = 0; k <= Segs; ++k)
		{
			const float Along = -Half + k * Step;
			const float ColTop = FMath::Max(SegTop[FMath::Max(k - 1, 0)], SegTop[FMath::Min(k, Segs - 1)]) + 45.f;
			const float ColLo = EdgeGround(Along) - 120.f;
			const FVector2D C = EdgeXY(Along, Line);
			ColumnISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(C.X, C.Y, (ColLo + ColTop) * 0.5f), FVector(1.9f, 1.9f, (ColTop - ColLo) / 100.f)));
			if (PillarISM) PillarISM->AddInstance(ArenaSitTransform(PillarMesh, FVector(C.X, C.Y, ColTop - 2.f), FQuat::Identity, FVector(PropScale * 1.15f)));
		}

		// Ground skirt outside the wall so the arena does not float in a void
		const float SkirtW = 7000.f;
		const FVector2D SC = EdgeXY(0.f, Half + Thick + SkirtW * 0.5f);
		const float SkirtLen = (Half + Thick + SkirtW) * 2.f / 100.f;
		const float SkirtTop = SideMin - 10.f;
		SkirtISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(SC.X, SC.Y, SkirtTop - 50.f),
			bRunsAlongY ? FVector(SkirtW / 100.f, SkirtLen, 1.f) : FVector(SkirtLen, SkirtW / 100.f, 1.f)));
	}
}

void ASQArena::BuildDressing(const USQData* D, const FSQArenaPalette& Pal, int32 Seed, float PropScale, bool bArt)
{
	const bool bVisuals = GetNetMode() != NM_DedicatedServer;

	// --- Lamp posts beside the road and at the landmarks: blockers everywhere; visuals + lights on clients.
	// Each lamp is its own component (taller than MapFadeHeight -> SQFadeable, faded one at a time) ---
	UStaticMesh* LampMesh = bArt ? ArenaFirstProp(Pal.LampProps) : nullptr;
	const float LampHeight = 270.f;
	const float LampScale = LampMesh ? FitMeshHeight(LampMesh, LampHeight) : 1.f;
	const int32 MaxLights = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MaxArenaLights"), 11.f)), 0, 32);
	for (int32 i = 0; i < LampSpots.Num(); ++i)
	{
		const FVector& Spot = LampSpots[i];
		if (UStaticMeshComponent* Blocker = AddBlock(TEXT("Cylinder"), Spot + FVector(0.f, 0.f, 120.f), FRotator::ZeroRotator, FVector(0.6f, 0.6f, 2.4f), Pal.Wall, true))
		{
			Blocker->SetVisibility(false);
			Blocker->SetCastShadow(false);
			Blocker->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			PropBlockers.Add(Blocker);
		}
		if (!bVisuals) continue;
		const float LampYaw = FMath::RadiansToDegrees(FMath::Atan2(-static_cast<float>(Spot.Y), -static_cast<float>(Spot.X)));
		UStaticMeshComponent* Post = nullptr;
		if (LampMesh)
		{
			UMaterialInstanceDynamic* LampMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White);
			if (LampMID) LampMID->SetScalarParameterValue(TEXT("Glow"), 0.25f);
			Post = AddArtPart(LampMesh, ArenaSitTransform(LampMesh, Spot - FVector(0.f, 0.f, 4.f), FQuat(FRotator(0.f, LampYaw, 0.f)), FVector(LampScale)), LampMID, false);
		}
		else
		{
			Post = AddBlock(TEXT("Cylinder"), Spot + FVector(0.f, 0.f, 115.f), FRotator::ZeroRotator, FVector(0.22f, 0.22f, 2.4f), Pal.Wall * 0.8f, false);
		}
		const FVector BulbPos = Spot + FVector(0.f, 0.f, LampHeight * 0.88f);
		UStaticMeshComponent* Bulb = AddBlock(TEXT("Sphere"), BulbPos, FRotator::ZeroRotator, FVector(LampMesh ? 0.32f : 0.5f), ArenaHue(Pal.Lamp), false);
		if (Bulb)
		{
			Bulb->SetMaterial(0, SurfaceMID(this, ArenaHue(Pal.Lamp), 5.f));
			Bulb->SetCastShadow(false);
		}
		if (Post) Post->SetCastShadow(true);
		MarkFadeable(Post);
		MarkFadeable(Bulb);
		if (i < MaxLights)
		{
			UPointLightComponent* LampLight = NewObject<UPointLightComponent>(this);
			LampLight->SetupAttachment(Root);
			LampLight->SetRelativeLocation(BulbPos + FVector(0.f, 0.f, 30.f));
			LampBaseIntensity = D->Tune(TEXT("MapLightIntensity"), 9000.f);
			LampLight->SetIntensity(LampBaseIntensity);
			LampLight->SetAttenuationRadius(D->Tune(TEXT("MapLightRadius"), 1000.f));
			LampLight->SetLightColor(ArenaHue(Pal.Lamp));
			LampLight->SetCastShadows(false);
			LampLight->RegisterComponent();
			MapLights.Add(LampLight);
		}
	}
	if (!bVisuals) return;

	// Everything below is visual only (no collision), from its own stream
	FRandomStream DressRand(Seed ^ 0x2545F491);
	UStaticMesh* CubeMesh = SQ::Mesh(TEXT("Cube"));
	const int32 DecorCull = FMath::RoundToInt(D->Tune(TEXT("DecorCullDistance"), 9000.f));

	const bool bFloorClutter = D->Tune(TEXT("MapFloorClutter"), 0.f) > 0.5f; // floor rule: see Rebuild
	// --- Tier rims: a broken ring of low stones marks every tier edge (the height change reads from above) ---
	if (CubeMesh && bFloorClutter)
	{
		UInstancedStaticMeshComponent* RockISM = MakeArtISM(CubeMesh, SurfaceMID(this, Pal.Cliff * 1.25f), false, true);
		RockISM->SetCullDistances(DecorCull / 2, DecorCull);
		const int32 RimStones = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("RimStones"), 8.f)) * 2, 0, 64);
		for (const FSQTerrainPlateau& Tier : Plateaus)
		{
			for (int32 k = 0; k < RimStones; ++k)
			{
				const float Ang = (k + DressRand.FRandRange(0.2f, 0.8f)) * 2.f * PI / RimStones;
				const float RimR = Tier.RadiusAt(Ang) + DressRand.FRandRange(-20.f, 40.f);
				const float RockSize = DressRand.FRandRange(0.35f, 0.6f);
				const FRotator RockRot(DressRand.FRandRange(-20.f, 20.f), DressRand.FRandRange(0.f, 360.f), DressRand.FRandRange(-20.f, 20.f));
				const bool bSkip = DressRand.FRand() < 0.35f;
				const float RX = static_cast<float>(Tier.Centre.X) + FMath::Cos(Ang) * RimR;
				const float RY = static_cast<float>(Tier.Centre.Y) + FMath::Sin(Ang) * RimR;
				if (bSkip || PathMask(RX, RY) > 0.05f || IsWallAt(RX, RY, 40.f)) continue;
				// Never on the objective floor of a raised landmark
				bool bOnZone = false;
				for (const FSQMapZone& Zone : MapZones)
				{
					if (Zone.Kind != ESQZoneKind::Hub && FVector2D::Distance(Zone.Centre, FVector2D(RX, RY)) < Zone.Radius * 0.85f) bOnZone = true;
				}
				if (bOnZone) continue;
				const FVector RockScale(RockSize, RockSize * 0.9f, RockSize * 0.55f);
				RockISM->AddInstance(FTransform(RockRot, FVector(RX, RY, GroundZ(RX, RY) + RockScale.Z * 15.f), RockScale));
			}
		}
	}

	// --- Grass tufts (map art pass): in patches of a dozen instead of an even sprinkle, toned close to the ground
	// they grow from (detail without contrast). Off the road, lanes, water, ramps, skate ground and mission floors.
	UStaticMesh* ConeMesh = SQ::Mesh(TEXT("Cone"));
	const int32 Tufts = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("GrassTufts"), 400.f)), 0, 6000);
	const float GroundBright = FMath::Clamp(D->Tune(TEXT("MapGroundBrightness"), 1.2f), 0.5f, 2.5f);
	if (ConeMesh && Tufts > 0 && bFloorClutter)
	{
		UInstancedStaticMeshComponent* TuftISM[2] = {
			MakeArtISM(ConeMesh, SurfaceMID(this, FMath::Lerp(Pal.Grass, Pal.Top, 0.3f) * (1.15f * GroundBright)), false, false),
			MakeArtISM(ConeMesh, SurfaceMID(this, Pal.GrassAlt * (1.05f * GroundBright)), false, false) };
		for (int32 t = 0; t < 2; ++t) TuftISM[t]->SetCullDistances(DecorCull / 2, DecorCull);
		const int32 PatchSize = 12;
		float PatchX = 0.f;
		float PatchY = 0.f;
		float PatchR = 300.f;
		int32 PatchShade = 0;
		for (int32 i = 0; i < Tufts; ++i)
		{
			if (i % PatchSize == 0)
			{
				PatchX = DressRand.FRandRange(-Half * 0.95f, Half * 0.95f);
				PatchY = DressRand.FRandRange(-Half * 0.95f, Half * 0.95f);
				PatchR = DressRand.FRandRange(180.f, 460.f);
				PatchShade = DressRand.FRand() < 0.5f ? 0 : 1;
			}
			const FVector2D Off2 = ArenaMapDir(DressRand.FRandRange(0.f, 2.f * PI)) * (PatchR * FMath::Sqrt(DressRand.FRand()));
			const float TX = FMath::Clamp(PatchX + static_cast<float>(Off2.X), -Half * 0.96f, Half * 0.96f);
			const float TY = FMath::Clamp(PatchY + static_cast<float>(Off2.Y), -Half * 0.96f, Half * 0.96f);
			const float TuftYaw = DressRand.FRandRange(0.f, 360.f);
			const float TuftSize = DressRand.FRandRange(0.7f, 1.35f);
			if (GroundSlopeDeg(TX, TY) > 12.f || GroundZ(TX, TY) < -10.f) continue;
			if (IsReservedSpot(TX, TY, 30.f) || FMath::Abs(SkateHeight(TX, TY)) > 2.f) continue;
			const float TZ = GroundZ(TX, TY) - 5.f;
			for (int32 Blade = 0; Blade < 3; ++Blade)
			{
				const float BladeYaw = TuftYaw + Blade * 120.f;
				const FVector Off = FRotator(0.f, BladeYaw, 0.f).Vector() * 14.f * TuftSize;
				const FRotator BladeRot(DressRand.FRandRange(-22.f, -8.f), BladeYaw, 0.f);
				const FVector BladeScale(0.13f * TuftSize, 0.13f * TuftSize, DressRand.FRandRange(0.28f, 0.45f) * TuftSize);
				TuftISM[PatchShade]->AddInstance(ArenaSitTransform(ConeMesh, FVector(TX + Off.X, TY + Off.Y, TZ), FQuat(BladeRot), BladeScale));
			}
		}
	}

	// --- Fireflies: a small pool of glowing motes that Tick keeps drifting round the local view (ambient motion) ---
	const int32 Motes = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapMotes"), 44.f)), 0, 160);
	if (UStaticMesh* MoteMesh = Motes > 0 ? SQ::Mesh(TEXT("Sphere")) : nullptr)
	{
		MoteISM = MakeArtISM(MoteMesh, SurfaceMID(this, FMath::Lerp(ArenaHue(Pal.Lamp), FLinearColor(0.7f, 1.f, 0.6f), 0.45f), 4.f), false, false);
		ScratchMotes.Reset(Motes);
		for (int32 MoteIdx = 0; MoteIdx < Motes; ++MoteIdx)
		{
			ScratchMotes.Add(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -5000.f), FVector(0.01f)));
			MoteISM->AddInstance(ScratchMotes.Last(), false);
		}
	}
}

void ASQArena::BuildFloorDetail(const USQData* D, const FSQArenaPalette& Pal, int32 Seed)
{
	UStaticMesh* CubeMesh = SQ::Mesh(TEXT("Cube"));
	UStaticMesh* CylMesh = SQ::Mesh(TEXT("Cylinder"));
	UStaticMesh* SphereMesh = SQ::Mesh(TEXT("Sphere"));
	if (!CubeMesh || !CylMesh || !SphereMesh) return;
	FRandomStream FloorRand(Seed ^ 0x51ED270B);
	const int32 DecorCull = FMath::RoundToInt(D->Tune(TEXT("DecorCullDistance"), 9000.f));

	// --- Flagstone plazas: concentric rings of slabs on the hub, landmark and boss floors. Three shades, a few
	// missing or cracked, small yaw jitter: from the top-down camera they read as a laid stone plaza with dark grout.
	const float Tile = FMath::Clamp(D->Tune(TEXT("MapFlagstoneSize"), 170.f), 80.f, 400.f);
	const float Grout = FMath::Clamp(D->Tune(TEXT("MapFlagstoneGrout"), 18.f), 4.f, 60.f);
	const float Missing = FMath::Clamp(D->Tune(TEXT("MapFlagstoneMissing"), 0.08f), 0.f, 0.6f);
	const FLinearColor Stone = FMath::Lerp(Pal.Cap, Pal.Cliff, 0.35f);
	UInstancedStaticMeshComponent* SlabISM[3] = {
		MakeArtISM(CubeMesh, SurfaceMID(this, Stone * 0.95f), false, false),
		MakeArtISM(CubeMesh, SurfaceMID(this, Stone * 0.78f), false, false),
		MakeArtISM(CubeMesh, SurfaceMID(this, FMath::Lerp(Stone, Pal.Path, 0.35f) * 0.85f), false, false) };
	UInstancedStaticMeshComponent* BossSlabISM = MakeArtISM(CubeMesh, SurfaceMID(this, FMath::Lerp(Stone, Pal.Basin, 0.45f) * 0.9f), false, false);
	for (UInstancedStaticMeshComponent* SlabSet : SlabISM) SlabSet->SetCullDistances(DecorCull, DecorCull + 2000);
	BossSlabISM->SetCullDistances(DecorCull, DecorCull + 2000);
	const float SlabMax = 4.f; // only on nearly flat floor (ramps keep their stony vertex colour)
	auto LayPlaza = [&](const FVector2D& Centre, float Radius, bool bBoss)
	{
		const int32 Rings = FMath::Max(1, FMath::FloorToInt(Radius / Tile));
		for (int32 RingIdx = 0; RingIdx < Rings; ++RingIdx)
		{
			const float RingR = (RingIdx + 0.5f) * Tile;
			const int32 Count = RingIdx == 0 ? 1 : FMath::Max(6, FMath::RoundToInt(2.f * PI * RingR / Tile));
			const float Offset = FloorRand.FRandRange(0.f, 2.f * PI);
			const float ArcLen = RingIdx == 0 ? Tile : 2.f * PI * RingR / Count;
			for (int32 k = 0; k < Count; ++k)
			{
				// Fixed rolls per slab (layout never depends on the terrain tests below)
				const float Skip = FloorRand.FRand();
				const int32 Shade = FloorRand.RandRange(0, 2);
				const float YawJit = FloorRand.FRandRange(-4.f, 4.f);
				const float SizeJit = FloorRand.FRandRange(0.92f, 1.04f);
				if (Skip < Missing) continue;
				const float Ang = Offset + 2.f * PI * k / Count;
				const float SX = static_cast<float>(Centre.X) + (RingIdx == 0 ? 0.f : FMath::Cos(Ang) * RingR);
				const float SY = static_cast<float>(Centre.Y) + (RingIdx == 0 ? 0.f : FMath::Sin(Ang) * RingR);
				if (GroundSlopeDeg(SX, SY) > SlabMax || GroundZ(SX, SY) < -10.f || IsWallAt(SX, SY, Tile * 0.4f)) continue;
				const FVector Scale((ArcLen - Grout) / 100.f * SizeJit, (Tile - Grout) / 100.f * SizeJit, 0.05f);
				const FRotator Rot(0.f, FMath::RadiansToDegrees(Ang) + 90.f + YawJit, 0.f);
				UInstancedStaticMeshComponent* Target = bBoss ? BossSlabISM : SlabISM[Shade];
				Target->AddInstance(FTransform(Rot, FVector(SX, SY, GroundZ(SX, SY) + 1.5f), Scale), false);
			}
		}
	};
	LayPlaza(FVector2D::ZeroVector, HubRadius * 0.92f, false);
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind == ESQZoneKind::Hub) continue;
		LayPlaza(Zone.Centre, Zone.Radius * 0.9f, Zone.Kind == ESQZoneKind::Boss);
	}

	// --- Road kerbs: a line of low stones on both sides of the escort road (the route reads as a built path) ---
	const float KerbStep = FMath::Clamp(D->Tune(TEXT("MapKerbSpacing"), 150.f), 60.f, 600.f);
	if (RoadPts.Num() >= 2 && KerbStep > 0.f)
	{
		UInstancedStaticMeshComponent* KerbISM = MakeArtISM(CubeMesh, SurfaceMID(this, FMath::Lerp(Pal.Cap, Pal.Cliff, 0.55f)), false, true);
		KerbISM->SetCullDistances(DecorCull, DecorCull + 2000);
		float Carry = 0.f;
		for (int32 Seg = 0; Seg + 1 < RoadPts.Num(); ++Seg)
		{
			const FVector2D A = RoadPts[Seg];
			const FVector2D B = RoadPts[Seg + 1];
			const float SegLen = static_cast<float>(FVector2D::Distance(A, B));
			if (SegLen < 1.f) continue;
			const FVector2D Dir = (B - A) / SegLen;
			const FVector2D Side(-Dir.Y, Dir.X);
			const float SegYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
			float Along = Carry;
			for (; Along < SegLen; Along += KerbStep)
			{
				for (int32 SideSign = -1; SideSign <= 1; SideSign += 2)
				{
					const float Jit = FloorRand.FRandRange(0.85f, 1.15f);
					const FVector2D P = A + Dir * Along + Side * (RoadHalfWidth + 25.f) * static_cast<float>(SideSign);
					const float KX = static_cast<float>(P.X);
					const float KY = static_cast<float>(P.Y);
					// Not where the road opens onto a plaza, another road leg, water or a wall
					if (FMath::Abs(KX) > Half - 120.f || FMath::Abs(KY) > Half - 120.f) continue;
					if (KX * KX + KY * KY < FMath::Square(HubRadius + 60.f)) continue;
					bool bOnPlaza = false;
					for (const FSQMapZone& Zone : MapZones)
					{
						if (FVector2D::DistSquared(Zone.Centre, P) < FMath::Square(Zone.Radius + 60.f)) bOnPlaza = true;
					}
					if (bOnPlaza || DistToRoad(KX, KY) < RoadHalfWidth - 5.f || GroundZ(KX, KY) < -10.f || IsWallAt(KX, KY, 30.f)) continue;
					const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, GroundNormal(KX, KY));
					KerbISM->AddInstance(FTransform(Align * FQuat(FRotator(0.f, SegYaw, 0.f)), FVector(KX, KY, GroundZ(KX, KY) + 6.f),
						FVector(0.62f * Jit, 0.26f, 0.16f * Jit)), false);
				}
			}
			Carry = Along - SegLen; // the spacing carries on round the bend
		}
	}

	// --- Landmark marker posts: short stone posts with a glowing cap round every landmark (boss: crimson), so the
	// objective floors read as places from across the screen. Visual only: never blocks the horde or a reaper.
	const int32 Posts = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapZonePosts"), 6.f)), 0, 16);
	if (Posts > 0)
	{
		// Kit post when it is imported (plain stone cylinder otherwise); the caps are small embers, not light bulbs
		static const TCHAR* const PostNames[] = { TEXT("post"), TEXT("fence_pillar"), TEXT("pillar") };
		UStaticMesh* PostMesh = nullptr;
		for (const TCHAR* const PostName : PostNames)
		{
			PostMesh = SQArt::Prop(FName(PostName));
			if (PostMesh) break;
		}
		const float PostScale = PostMesh ? FitMeshHeight(PostMesh, 95.f) : 1.f;
		UInstancedStaticMeshComponent* PostISM = PostMesh
			? MakeArtISM(PostMesh, SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White), false, true)
			: MakeArtISM(CylMesh, SurfaceMID(this, Pal.Cliff * 1.15f), false, true);
		UInstancedStaticMeshComponent* CapISM = MakeArtISM(SphereMesh, SurfaceMID(this, ArenaHue(Pal.Lamp), 2.6f), false, false);
		UInstancedStaticMeshComponent* BossCapISM = MakeArtISM(SphereMesh, SurfaceMID(this, FLinearColor(1.f, 0.2f, 0.25f), 2.6f), false, false);
		for (const FSQMapZone& Zone : MapZones)
		{
			if (Zone.Kind == ESQZoneKind::Hub) continue;
			const bool bBoss = Zone.Kind == ESQZoneKind::Boss;
			const int32 Count = bBoss ? Posts + 2 : Posts;
			const float Offset = FloorRand.FRandRange(0.f, 2.f * PI);
			for (int32 k = 0; k < Count; ++k)
			{
				const float Ang = Offset + 2.f * PI * k / Count;
				const float PX = static_cast<float>(Zone.Centre.X) + FMath::Cos(Ang) * (Zone.Radius + 55.f);
				const float PY = static_cast<float>(Zone.Centre.Y) + FMath::Sin(Ang) * (Zone.Radius + 55.f);
				if (PathMask(PX, PY) > 0.05f || IsWallAt(PX, PY, 40.f) || GroundZ(PX, PY) < -10.f || GroundSlopeDeg(PX, PY) > 14.f) continue;
				const float BaseZ = GroundZ(PX, PY);
				if (PostMesh) PostISM->AddInstance(ArenaSitTransform(PostMesh, FVector(PX, PY, BaseZ - 3.f), FQuat(FRotator(0.f, FMath::RadiansToDegrees(Ang), 0.f)), FVector(PostScale)), false);
				else PostISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(PX, PY, BaseZ + 45.f), FVector(0.3f, 0.3f, 0.9f)), false);
				(bBoss ? BossCapISM : CapISM)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(PX, PY, BaseZ + 104.f), FVector(0.13f)), false);
			}
		}
	}
}

void ASQArena::BuildBackdrop(const USQData* D, const FSQArenaPalette& Pal, int32 Seed, float PropScale)
{
	// Dark silhouettes in a band outside the walls. Tall: one fadeable ISM set per side, so the near side can fade
	// when the player hugs that wall.
	FRandomStream BackRand(Seed ^ 0x68E31DA4);
	TArray<UStaticMesh*> BackMeshes;
	ArenaLoadProps(Pal.BackdropProps, BackMeshes);
	UStaticMesh* ConeMesh = SQ::Mesh(TEXT("Cone"));
	UMaterialInstanceDynamic* BackMID = SurfaceMID(this, Pal.Backdrop);
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> BackISMs[4];
	const int32 Count = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("BackdropCount"), 80.f)), 0, 400);
	const float Inset = FMath::Clamp(D->Tune(TEXT("MapBackdropInset"), 900.f), 300.f, 3000.f);
	const float ArtScale = BackMeshes.Num() > 0 ? PropScale : 1.f;
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Side = BackRand.RandRange(0, 3);
		const float Along = BackRand.FRandRange(-Half - 2500.f, Half + 2500.f);
		const float Out = Half + BackRand.FRandRange(Inset, Inset + 3200.f);
		const float BX = Side == 0 ? Out : (Side == 1 ? -Out : Along);
		const float BY = Side < 2 ? Along : (Side == 2 ? Out : -Out);
		const float BackYaw = BackRand.FRandRange(0.f, 360.f);
		// Map remaster: no more giants. The near band is life-size to 1.6x (it was 1.4x-2.6x and read as oversized)
		const float BackSize = BackRand.FRandRange(1.f, 1.6f);
		const int32 Pick = BackMeshes.Num() > 0 ? BackRand.RandRange(0, BackMeshes.Num() - 1) : 0;
		UStaticMesh* BackMesh = BackMeshes.Num() > 0 ? BackMeshes[Pick] : ConeMesh;
		if (!BackMesh) continue;
		UInstancedStaticMeshComponent*& BackISM = BackISMs[Side].FindOrAdd(BackMesh);
		if (!BackISM)
		{
			BackISM = MakeArtISM(BackMesh, BackMID, false, false);
			MarkFadeable(BackISM);
		}
		// Outside the grid GroundZ clamps to the edge height, which is where the skirt sits
		const FVector Base(BX, BY, GroundZ(BX, BY) - 30.f);
		const FVector BackScale = BackMeshes.Num() > 0 ? FVector(ArtScale * BackSize) : FVector(2.2f * BackSize, 2.2f * BackSize, 6.f * BackSize);
		BackISM->AddInstance(ArenaSitTransform(BackMesh, Base, FQuat(FRotator(0.f, BackYaw, 0.f)), BackScale));
	}
}

void ASQArena::BuildSky(const USQData* D, const FSQArenaPalette& Pal, int32 Seed, float PropScale)
{
	// Style pass (Nov 12 2026, third-person camera): the horizon is on screen now, so the world needs one. A gradient
	// sky dome (M_SQSky), a big low moon, stars, and two rings of rolling hills with a giant skyline of the biome's
	// silhouettes on top. All far outside the walls, no collision, no shadows. Missing M_SQSky = no dome (the engine
	// sky shows), everything else still builds.
	if (D->Tune(TEXT("StyleSky"), 1.f) < 0.5f) return;
	FRandomStream SkyRand(Seed ^ 0x5C1E77A3);
	UStaticMesh* SphereMesh = SQ::Mesh(TEXT("Sphere"));
	if (!SphereMesh) return;
	const float DomeR = FMath::Clamp(D->Tune(TEXT("StyleSkyRadius"), 90000.f), Half * 3.f, 400000.f);
	const float SkyBright = FMath::Clamp(D->Tune(TEXT("StyleSkyBrightness"), 1.f), 0.f, 6.f);

	if (UMaterialInterface* SkyBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SQSky.M_SQSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		if (UMaterialInstanceDynamic* SkyMID = UMaterialInstanceDynamic::Create(SkyBase, this))
		{
			SkyMID->SetVectorParameterValue(TEXT("Horizon"), Pal.SkyHorizon);
			SkyMID->SetVectorParameterValue(TEXT("Mid"), Pal.SkyMid);
			SkyMID->SetVectorParameterValue(TEXT("Zenith"), Pal.SkyZenith);
			SkyMID->SetScalarParameterValue(TEXT("Brightness"), SkyBright);
			SkyMID->SetScalarParameterValue(TEXT("MidHeight"), FMath::Clamp(D->Tune(TEXT("StyleSkyMidHeight"), 0.22f), 0.05f, 0.8f));
			UInstancedStaticMeshComponent* DomeISM = MakeArtISM(SphereMesh, SkyMID, false, false);
			// Engine sphere = 100 cm across. Centred on the arena origin (the material measures height from there).
			DomeISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(DomeR / 50.f)));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("SQArena: M_SQSky not found (run Scripts/setup_project.py) - no sky dome this run"));
	}

	// Moon: low over the horizon so the chase camera actually sees it. Glow carries it (bloom makes the halo).
	const float MoonYaw = D->Tune(TEXT("StyleMoonYaw"), 35.f);
	const float MoonElev = FMath::Clamp(D->Tune(TEXT("StyleMoonElevation"), 15.f), 4.f, 80.f);
	const float MoonSize = FMath::Clamp(D->Tune(TEXT("StyleMoonSize"), 0.085f), 0.01f, 0.4f) * DomeR;
	const FVector MoonDir = FRotator(MoonElev, MoonYaw, 0.f).Vector();
	const FLinearColor MoonTone = FMath::Lerp(FLinearColor::White, ArenaHue(Pal.Moon), 0.35f);
	UInstancedStaticMeshComponent* MoonISM = MakeArtISM(SphereMesh, SurfaceMID(this, MoonTone, FMath::Clamp(D->Tune(TEXT("StyleMoonGlow"), 2.2f), 0.f, 12.f)), false, false);
	MoonISM->AddInstance(FTransform(FRotator::ZeroRotator, MoonDir * (DomeR * 0.93f), FVector(MoonSize / 50.f)));

	// Stars: two tones, upper sky only, thinner toward the horizon haze
	const int32 Stars = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("StyleStars"), 260.f)), 0, 1200);
	if (Stars > 0)
	{
		UInstancedStaticMeshComponent* StarISMs[2] = {
			MakeArtISM(SphereMesh, SurfaceMID(this, FLinearColor(0.85f, 0.92f, 1.f), 2.6f), false, false),
			MakeArtISM(SphereMesh, SurfaceMID(this, FMath::Lerp(FLinearColor::White, ArenaHue(Pal.Lamp), 0.5f), 2.6f), false, false) };
		for (int32 StarIdx = 0; StarIdx < Stars; ++StarIdx)
		{
			const float Elev = FMath::Lerp(9.f, 85.f, FMath::Sqrt(SkyRand.FRand()));
			const FVector StarDir = FRotator(Elev, SkyRand.FRandRange(0.f, 360.f), 0.f).Vector();
			const float StarSize = DomeR * SkyRand.FRandRange(0.0012f, 0.0034f) * (SkyRand.FRand() < 0.06f ? 2.2f : 1.f);
			StarISMs[SkyRand.FRand() < 0.25f ? 1 : 0]->AddInstance(FTransform(FRotator::ZeroRotator, StarDir * (DomeR * 0.96f), FVector(StarSize / 50.f)));
		}
	}

	// Hills: a near ring in the backdrop colour and a far, paler ring (air between them reads as depth)
	const float EdgeZ = GroundZ(Half, 0.f);
	const int32 HillsPerRing = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("StyleHills"), 26.f)), 0, 80);
	const FLinearColor NearHill = FMath::Lerp(Pal.Backdrop, Pal.Grass * 0.25f, 0.5f);
	const FLinearColor FarHill = FMath::Lerp(NearHill, Pal.SkyHorizon, 0.45f);
	TArray<FVector, TInlineAllocator<80>> HillTops;
	for (int32 Ring = 0; Ring < 2 && HillsPerRing > 0; ++Ring)
	{
		UInstancedStaticMeshComponent* HillISM = MakeArtISM(SphereMesh, SurfaceMID(this, Ring == 0 ? NearHill : FarHill), false, false);
		const float RingR = Half * 1.42f + (Ring == 0 ? 7000.f : 19000.f);
		for (int32 HillIdx = 0; HillIdx < HillsPerRing; ++HillIdx)
		{
			const float Ang = (static_cast<float>(HillIdx) + SkyRand.FRandRange(-0.3f, 0.3f)) / static_cast<float>(HillsPerRing) * 2.f * PI + Ring * 0.4f;
			const float R = RingR + SkyRand.FRandRange(-1500.f, 2500.f);
			const float Wide = SkyRand.FRandRange(5500.f, 9500.f) * (Ring == 0 ? 1.f : 1.8f);
			const float Tall = SkyRand.FRandRange(1100.f, 2600.f) * (Ring == 0 ? 1.f : 2.1f);
			const FVector At(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, EdgeZ - Tall * 0.25f);
			HillISM->AddInstance(FTransform(FRotator(0.f, FMath::RadiansToDegrees(Ang) + 90.f, 0.f), At, FVector(Wide / 50.f, Wide * 0.6f / 50.f, Tall / 50.f)));
			if (Ring == 0) HillTops.Add(FVector(At.X, At.Y, At.Z + Tall * 0.9f));
		}
	}

	// Skyline: giant silhouettes of the biome's backdrop props standing on the near hills
	TArray<UStaticMesh*> SkylineMeshes;
	ArenaLoadProps(Pal.BackdropProps, SkylineMeshes);
	ArenaLoadProps(Pal.TallProps, SkylineMeshes);
	if (SkylineMeshes.Num() > 0 && HillTops.Num() > 0)
	{
		UMaterialInstanceDynamic* SkylineMID = SurfaceMID(this, Pal.Backdrop);
		TMap<UStaticMesh*, UInstancedStaticMeshComponent*> SkylineISMs;
		const int32 PerHill = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("StyleSkylinePerHill"), 2.f)), 0, 6);
		for (const FVector& Top : HillTops)
		{
			for (int32 Piece = 0; Piece < PerHill; ++Piece)
			{
				UStaticMesh* SkylineMesh = SkylineMeshes[SkyRand.RandRange(0, SkylineMeshes.Num() - 1)];
				if (!SkylineMesh) continue;
				UInstancedStaticMeshComponent*& SkylineISM = SkylineISMs.FindOrAdd(SkylineMesh);
				if (!SkylineISM) SkylineISM = MakeArtISM(SkylineMesh, SkylineMID, false, false);
				const FVector Base = Top + FVector(SkyRand.FRandRange(-2200.f, 2200.f), SkyRand.FRandRange(-2200.f, 2200.f), -500.f);
				SkylineISM->AddInstance(ArenaSitTransform(SkylineMesh, Base, FQuat(FRotator(0.f, SkyRand.FRandRange(0.f, 360.f), 0.f)),
					FVector(PropScale * SkyRand.FRandRange(0.6f, 1.f) * FMath::Clamp(D->Tune(TEXT("StyleSkylineScale"), 4.f), 1.f, 13.f))));
			}
		}
	}
}

void ASQArena::ApplyAtmosphere(const USQData* D, const FSQArenaPalette& Pal)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Top-down (Nov 9 2026): the camera looks down at ~55-60 deg, so the sky barely shows and fog matters little;
	// what matters is a bright, readable floor and short, crisp shadows. A high moon, more sky fill, fog only far away.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (ULightComponent* MoonLight = It->GetLightComponent())
		{
			MoonLight->SetIntensity(D->Tune(TEXT("MapMoonIntensity"), 4.f));
			if (UDirectionalLightComponent* MoonDir = Cast<UDirectionalLightComponent>(MoonLight))
			{
				MoonDir->SetLightSourceAngle(FMath::Clamp(D->Tune(TEXT("MapMoonSoftness"), 2.f), 0.f, 8.f)); // soft shadow edges
			}
			MoonLight->SetLightColor(FMath::Lerp(FLinearColor::White, ArenaHue(Pal.Moon), 0.6f));
			// Only a movable light may turn at runtime (the map script makes it movable)
			if (MoonLight->Mobility == EComponentMobility::Movable)
			{
				MoonLight->SetWorldRotation(FRotator(D->Tune(TEXT("MapMoonPitch"), -58.f), D->Tune(TEXT("MapMoonYaw"), 35.f), 0.f));
			}
		}
		break;
	}
	// Sky light: a cool fill so shadows read teal and nothing under a wall goes black
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (USkyLightComponent* SkyComp = It->GetLightComponent())
		{
			SkyComp->SetIntensity(D->Tune(TEXT("MapSkyLight"), 2.2f));
			SkyComp->SetLightColor(FMath::Lerp(FLinearColor::White, ArenaHue(Pal.Sky), 0.5f));
		}
	}
	// Fog starts beyond the far edge of the view: the playfield stays crisp, the backdrop melts into the night
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* Fog = It->GetComponent())
		{
			const float FogBright = D->Tune(TEXT("FogBrightness"), 1.6f);
			// Style pass: the far ground melts into the sky's horizon colour (no hard world edge from the chase camera)
			const float FogSky = FMath::Clamp(D->Tune(TEXT("StyleFogSkyBlend"), 0.6f), 0.f, 1.f);
			const float FogSkyBright = FMath::Clamp(D->Tune(TEXT("StyleSkyBrightness"), 1.f), 0.f, 6.f);
			const FLinearColor FogTone = FMath::Lerp(Pal.Fog * FogBright, Pal.SkyHorizon * FogSkyBright, FogSky);
			Fog->SetFogInscatteringColor(FLinearColor(FogTone.R, FogTone.G, FogTone.B, 1.f));
			Fog->SetFogDensity(D->Tune(TEXT("MapFogDensity"), 0.004f));
			Fog->SetFogHeightFalloff(D->Tune(TEXT("FogHeightFalloff"), 0.25f));
			Fog->SetStartDistance(D->Tune(TEXT("MapFogStart"), 5000.f));
			Fog->SetVolumetricFog(false);
		}
	}
	// Post process: gentle grade, soft bloom, a light vignette (the edges of a top-down view hold enemies)
	APostProcessVolume* PPV = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->bUnbound)
		{
			PPV = *It;
			break;
		}
	}
	if (!PPV)
	{
		FActorSpawnParameters PPSpawn;
		PPSpawn.ObjectFlags |= RF_Transient;
		PPV = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, PPSpawn);
		if (PPV) PPV->bUnbound = true;
	}
	if (PPV)
	{
		FPostProcessSettings& PP = PPV->Settings;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = D->Tune(TEXT("MapBloom"), 0.6f);
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = D->Tune(TEXT("MapVignette"), 0.2f);
		const float Sat = D->Tune(TEXT("MapSaturation"), 1.12f);
		const float Con = D->Tune(TEXT("MapContrast"), 1.08f);
		const float Teal = D->Tune(TEXT("GradeShadowTeal"), 0.06f) * 0.6f;
		const float Warm = D->Tune(TEXT("GradeHighlightWarm"), 0.05f);
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(Con, Con, Con, 1.f);
		PP.bOverride_ColorGainShadows = true;
		PP.ColorGainShadows = FVector4(1.f - Teal * 1.5f, 1.f + Teal * 0.3f, 1.f + Teal, 1.f);
		PP.bOverride_ColorGainHighlights = true;
		PP.ColorGainHighlights = FVector4(1.f + Warm, 1.f + Warm * 0.3f, 1.f - Warm, 1.f);
		// Lift the shadows a touch so the floor under walls and the horde stays readable
		PP.bOverride_ColorOffsetShadows = true;
		const float Lift = D->Tune(TEXT("MapShadowLift"), 0.015f);
		PP.ColorOffsetShadows = FVector4(Lift, Lift, Lift * 1.2f, 0.f);
		// Style pass: ink outlines (M_SQInk, built by Scripts/create_style_materials.py). Optional: no material = no lines.
		PP.WeightedBlendables.Array.Reset();
		const float InkStrength = FMath::Clamp(D->Tune(TEXT("StyleInkStrength"), 0.8f), 0.f, 1.f);
		if (InkStrength > 0.01f)
		{
			if (UMaterialInterface* InkBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SQInk.M_SQInk"), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				if (UMaterialInstanceDynamic* InkMID = UMaterialInstanceDynamic::Create(InkBase, PPV))
				{
					InkMID->SetScalarParameterValue(TEXT("InkStrength"), InkStrength);
					InkMID->SetScalarParameterValue(TEXT("InkThickness"), FMath::Clamp(D->Tune(TEXT("StyleInkThickness"), 1.5f), 0.5f, 4.f));
					InkMID->SetScalarParameterValue(TEXT("InkFadeDistance"), FMath::Clamp(D->Tune(TEXT("StyleInkFadeDistance"), 6500.f), 500.f, 60000.f));
					InkMID->SetScalarParameterValue(TEXT("InkDepthThreshold"), FMath::Clamp(D->Tune(TEXT("StyleInkDepthThreshold"), 0.035f), 0.001f, 1.f));
					InkMID->SetScalarParameterValue(TEXT("InkNormalThreshold"), FMath::Clamp(D->Tune(TEXT("StyleInkNormalThreshold"), 0.9f), 0.05f, 4.f));
					PP.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, InkMID));
				}
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("SQArena: M_SQInk not found (run Scripts/setup_project.py) - no ink outlines this run"));
			}
		}
		// Exposure only where the map already uses manual exposure (an auto-exposure bias of 11 would white the screen out)
		if (PP.bOverride_AutoExposureMethod && PP.AutoExposureMethod == EAutoExposureMethod::AEM_Manual)
		{
			if (const float* Bias = D->Tuning.Find(FName(TEXT("ExposureBias"))))
			{
				PP.bOverride_AutoExposureBias = true;
				PP.AutoExposureBias = *Bias + D->Tune(TEXT("MapExposureAdd"), 0.3f);
			}
		}
	}
}

// ---------------------------------------------------------------- map revamp (Nov 10 2026): districts, lanes, skate ground

namespace
{
	/** Bone park pieces (ArtSource/Props/README_Park.md): name, length along the ride (+X), width (cm). Placed at scale 1. */
	struct FArenaParkPiece
	{
		const TCHAR* Name;
		float Length;
		float Width;
	};
	const FArenaParkPiece ArenaParkPieces[] = {
		{ TEXT("Park_RibKicker"), 330.f, 257.f },
		{ TEXT("Park_FemurRamp"), 618.f, 234.f },
		{ TEXT("Park_SpineWave"), 800.f, 300.f },
		{ TEXT("Park_PelvisQuarter"), 401.f, 561.f },
		{ TEXT("Park_SkullHip"), 420.f, 380.f },
		{ TEXT("Park_CoffinFunbox"), 700.f, 215.f },
		{ TEXT("Park_TombPad"), 360.f, 200.f },
		{ TEXT("Park_JawBank"), 389.f, 805.f },
		{ TEXT("Park_GraveRoller"), 526.f, 300.f },
		{ TEXT("Park_BoneBridge"), 950.f, 341.f },
	};
	const int32 ArenaParkPieceCount = static_cast<int32>(UE_ARRAY_COUNT(ArenaParkPieces));

	/** District looks: one colour per theme slot (kept away from the interactable cue colours where possible). */
	const FLinearColor ArenaDistrictColors[6] = {
		FLinearColor(0.62f, 0.42f, 1.f),   // violet
		FLinearColor(1.f, 0.46f, 0.66f),   // rose
		FLinearColor(0.36f, 0.62f, 1.f),   // azure
		FLinearColor(0.32f, 0.88f, 0.58f), // jade
		FLinearColor(0.96f, 0.84f, 0.58f), // sand
		FLinearColor(1.f, 0.52f, 0.44f),   // coral
	};
	/** Original district names, three per theme (Bowl, Bank, Rollers, Quarter, Spine, Kicker). */
	const TCHAR* const ArenaDistrictNames[6][3] = {
		{ TEXT("Ossuary Bowl"), TEXT("Cauldron Pit"), TEXT("Hollow Basin") },
		{ TEXT("Bellstone Banks"), TEXT("Mourner's Banks"), TEXT("Slate Slopes") },
		{ TEXT("Barrow Hills"), TEXT("Coffin Row"), TEXT("Wraithwood Rollers") },
		{ TEXT("Candlewick Quarter"), TEXT("Lantern Walls"), TEXT("Crypt Curve") },
		{ TEXT("Marrow Ridge"), TEXT("Hollow Spine"), TEXT("Bone Spine") },
		{ TEXT("Sexton's Drop"), TEXT("Gravel Gaps"), TEXT("Jumper's Yard") },
	};
}

float FSQSkateFeature::HeightAt(float X, float Y) const
{
	const float RX = X - static_cast<float>(Centre.X);
	const float RY = Y - static_cast<float>(Centre.Y);
	if (Kind == ESQSkateKind::Bowl)
	{
		const float R = Length * 0.5f;
		const float Dist2 = RX * RX + RY * RY;
		if (Dist2 >= R * R) return 0.f;
		const float Wall = FMath::Clamp(Height * 2.4f, 320.f, R * 0.55f);
		return -Height * (1.f - Smooth01((FMath::Sqrt(Dist2) - (R - Wall)) / Wall));
	}
	const float DX = static_cast<float>(Dir.X);
	const float DY = static_cast<float>(Dir.Y);
	const float U = RX * DX + RY * DY;
	const float V = -RX * DY + RY * DX;
	const float HalfL = Length * 0.5f;
	const float HalfW = Width * 0.5f;
	// Soft side shoulders (the walk clamp keeps them climbable)
	const float Side = 1.f - Smooth01((FMath::Abs(V) - HalfW) / 260.f);
	if (Side <= 0.f) return 0.f;
	const float T = (U + HalfL) / FMath::Max(1.f, Length); // 0 at the entry, 1 at the lip
	float P = 0.f;
	switch (Kind)
	{
	case ESQSkateKind::Bank:
		// Straight wedge up to a deck, then the back falls away (ClampTerrainSlopes turns it into a walkable slope)
		if (T <= 0.f) P = 0.f;
		else if (T < 1.f) P = T;
		else if (U < HalfL + 260.f) P = 1.f;
		else P = 1.f - Smooth01((U - HalfL - 260.f) / 400.f);
		break;
	case ESQSkateKind::Quarter:
		// Curved transition into a wall, a short deck on top
		if (T <= 0.f) P = 0.f;
		else if (T < 1.f) P = T * T;
		else if (U < HalfL + 220.f) P = 1.f;
		else P = 1.f - Smooth01((U - HalfL - 220.f) / 300.f);
		break;
	case ESQSkateKind::Rollers:
		// Three pump humps in a row
		if (T > 0.f && T < 1.f) P = 0.5f - 0.5f * FMath::Cos(2.f * PI * 3.f * T);
		break;
	case ESQSkateKind::Spine:
		// Two banks back to back meeting at a ridge
		if (FMath::Abs(U) < HalfL) P = Smooth01(1.f - FMath::Abs(U) / HalfL);
		break;
	case ESQSkateKind::Kicker:
		// Short steepening launch, tiny table, then the drop
		if (T <= 0.f) P = 0.f;
		else if (T < 1.f) P = FMath::Pow(T, 1.6f);
		else if (U < HalfL + 60.f) P = 1.f;
		else P = 1.f - Smooth01((U - HalfL - 60.f) / 250.f);
		break;
	default:
		break;
	}
	return Height * P * Side;
}

bool ASQArena::IsWaterAt(float X, float Y) const
{
	for (int32 w = 0; w < WaterA.Num(); ++w)
	{
		if (DistToSegment2D(X, Y, static_cast<float>(WaterA[w].X), static_cast<float>(WaterA[w].Y), static_cast<float>(WaterB[w].X), static_cast<float>(WaterB[w].Y)) < WaterHalf[w]) return true;
	}
	return false;
}

int32 ASQArena::GetLanternTrail(int32 Count, int32 Seed, TArray<FVector>& OutSites, TArray<FString>& OutNames, TArray<int32>* OutDistricts) const
{
	OutSites.Reset();
	OutNames.Reset();
	if (OutDistricts) OutDistricts->Reset();
	if (Count <= 0) return 0;
	const float LanternR = 350.f;
	OutSites.Add(GridN > 0 ? FindLanternSpot(0.f, 0.f, LanternR) : FVector::ZeroVector);
	OutNames.Add(TEXT("Lantern Square"));
	if (OutDistricts) OutDistricts->Add(INDEX_NONE);
	if (GridN <= 0 || Count == 1) return OutSites.Num();

	// One candidate per landmark district: beside the landmark on the side facing the hub (the monument holds the centre)
	struct FTrailCand { FVector Site; int32 District; };
	TArray<FTrailCand> Cands;
	const FVector2D BossAt = MapZones.IsValidIndex(BossZone) ? MapZones[BossZone].Centre : FVector2D::ZeroVector;
	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num(); ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		if (Dist.Zone == BossZone) continue;
		const FVector2D In = (-Dist.Centre).GetSafeNormal();
		const FVector2D Want = Dist.Centre + In * 750.f;
		const FVector Site = FindLanternSpot(static_cast<float>(Want.X), static_cast<float>(Want.Y), LanternR);
		// FindLanternSpot falls back to the hub when nothing is clear: such a district has no site
		if (FVector2D(Site.X, Site.Y).Size() < HubRadius + 600.f) continue;
		if (FVector2D::Distance(FVector2D(Site.X, Site.Y), Dist.Centre) > Dist.Radius + 800.f) continue;
		Cands.Add({ Site, DistrictIdx });
	}
	if (Cands.Num() == 0) return OutSites.Num();

	// The last lantern stands in the district nearest the boss arena (the finale is next door, not back at the hub)
	const int32 Want = FMath::Min(Count - 1, Cands.Num());
	int32 FinalIdx = 0;
	for (int32 CandIdx = 1; CandIdx < Cands.Num(); ++CandIdx)
	{
		if (FVector2D::DistSquared(FVector2D(Cands[CandIdx].Site), BossAt) < FVector2D::DistSquared(FVector2D(Cands[FinalIdx].Site), BossAt)) FinalIdx = CandIdx;
	}
	const FTrailCand Final = Cands[FinalIdx];
	Cands.RemoveAt(FinalIdx);

	// The ones in between: each a real journey from the one before (one of the two farthest left, rolled from the seed)
	FRandomStream Roll(Seed * 7907 + 113);
	FVector2D From(OutSites[0].X, OutSites[0].Y);
	TArray<FTrailCand> Route;
	while (Route.Num() < Want - 1 && Cands.Num() > 0)
	{
		Cands.Sort([&From](const FTrailCand& A, const FTrailCand& B)
		{
			return FVector2D::DistSquared(FVector2D(A.Site), From) > FVector2D::DistSquared(FVector2D(B.Site), From);
		});
		// The first hop from the hub may be any district (they all sit on the same ring); later hops take a far one
		const int32 Pool = Route.Num() == 0 ? Cands.Num() : FMath::Min(2, Cands.Num());
		const int32 Pick = Roll.RandRange(0, Pool - 1);
		Route.Add(Cands[Pick]);
		From = FVector2D(Cands[Pick].Site);
		Cands.RemoveAt(Pick);
	}
	// The Clocktower climb: the lanterns in between are walked in order of height (the trail only ever goes up)
	if (MapShape == FName(TEXT("Climb")))
	{
		Route.Sort([](const FTrailCand& A, const FTrailCand& B) { return A.Site.Z < B.Site.Z; });
	}
	Route.Add(Final);
	for (const FTrailCand& Stop : Route)
	{
		OutSites.Add(Stop.Site);
		OutNames.Add(Districts[Stop.District].Name);
		if (OutDistricts) OutDistricts->Add(Stop.District);
	}
	return OutSites.Num();
}

int32 ASQArena::FindDistrictAt(float X, float Y) const
{
	int32 Best = INDEX_NONE;
	double BestD2 = TNumericLimits<double>::Max();
	const FVector2D P(X, Y);
	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num(); ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		const double D2 = FVector2D::DistSquared(Dist.Centre, P);
		if (D2 < static_cast<double>(Dist.Radius) * Dist.Radius && D2 < BestD2)
		{
			BestD2 = D2;
			Best = DistrictIdx;
		}
	}
	return Best;
}

float ASQArena::DistToLane(float X, float Y) const
{
	float Best = TNumericLimits<float>::Max();
	for (const FSQLane& Lane : Lanes) Best = FMath::Min(Best, ArenaMapDistToPolyline(Lane.Pts, X, Y));
	return Best;
}

float ASQArena::SkateHeight(float X, float Y) const
{
	float H = 0.f;
	for (const FSQSkateFeature& F : SkateFeatures)
	{
		const float Reach = F.Reach();
		if (FMath::Square(X - static_cast<float>(F.Centre.X)) + FMath::Square(Y - static_cast<float>(F.Centre.Y)) > Reach * Reach) continue;
		H += F.HeightAt(X, Y);
	}
	return H;
}

float ASQArena::SkateRoom(const FVector2D& C, float R) const
{
	const float CX = static_cast<float>(C.X);
	const float CY = static_cast<float>(C.Y);
	float Room = FMath::Min(Half - 600.f - FMath::Abs(CX), Half - 600.f - FMath::Abs(CY));
	Room = FMath::Min(Room, static_cast<float>(C.Size()) - HubRadius - KiteRing);
	for (const FSQMapZone& Zone : MapZones)
	{
		if (Zone.Kind == ESQZoneKind::Hub) continue;
		Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, Zone.Centre)) - Zone.Radius - 250.f);
	}
	Room = FMath::Min(Room, DistToRoad(CX, CY) - RoadHalfWidth - 250.f);
	Room = FMath::Min(Room, DistToLane(CX, CY) - LaneHalfWidth - 200.f);
	for (const FVector2D& Pad : SpawnerSpots) Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, Pad)) - ArenaMapPadClear - 200.f);
	for (int32 w = 0; w < WaterA.Num(); ++w)
	{
		Room = FMath::Min(Room, DistToSegment2D(CX, CY, static_cast<float>(WaterA[w].X), static_cast<float>(WaterA[w].Y),
			static_cast<float>(WaterB[w].X), static_cast<float>(WaterB[w].Y)) - WaterHalf[w] - 250.f);
	}
	for (const FSQMapWall& W : MapWalls)
	{
		if (W.Kind != ESQWallKind::Water) continue;
		Room = FMath::Min(Room, DistToSegment2D(CX, CY, static_cast<float>(W.A.X), static_cast<float>(W.A.Y), static_cast<float>(W.B.X), static_cast<float>(W.B.Y)) - W.HalfThick - 250.f);
	}
	for (const FSQTerrainPlateau& Tier : Plateaus) Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, Tier.Centre)) - Tier.FootRadius() - 250.f);
	for (const FSQSkateFeature& F : SkateFeatures) Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(C, F.Centre)) - F.Reach() - 150.f);
	return Room - R;
}

void ASQArena::PlanDistricts(const USQData* D, FRandomStream& LR)
{
	Districts.Reset();
	Lanes.Reset();
	LaneHalfWidth = FMath::Clamp(D->Tune(TEXT("MapLaneWidth"), 440.f) * 0.5f, 120.f, 400.f);
	const float Reach = FMath::Clamp(D->Tune(TEXT("MapDistrictReach"), 2600.f), 800.f, Half * 0.5f);
	const float LaneBend = FMath::Clamp(D->Tune(TEXT("MapLaneBend"), 700.f), 0.f, 2000.f);
	static const ESQSkateKind Themes[6] = { ESQSkateKind::Bowl, ESQSkateKind::Bank, ESQSkateKind::Rollers, ESQSkateKind::Quarter, ESQSkateKind::Spine, ESQSkateKind::Kicker };
	// A seeded theme order, so neighbouring districts ride differently from run to run (always five rolls)
	int32 Order[6] = { 0, 1, 2, 3, 4, 5 };
	for (int32 s = 5; s > 0; --s)
	{
		const int32 SwapWith = LR.RandRange(0, s);
		Swap(Order[s], Order[SwapWith]);
	}
	int32 LandmarkCount = 0;
	for (int32 ZoneIdx = 0; ZoneIdx < MapZones.Num(); ++ZoneIdx)
	{
		const FSQMapZone& Zone = MapZones[ZoneIdx];
		if (Zone.Kind == ESQZoneKind::Hub) continue;
		// Same rolls for every zone
		const int32 NameRoll = LR.RandRange(0, 2);
		const float BendRoll = LR.FRandRange(-1.f, 1.f);
		FSQDistrict Dist;
		Dist.Centre = Zone.Centre;
		Dist.Zone = ZoneIdx;
		if (Zone.Kind == ESQZoneKind::Boss)
		{
			Dist.Theme = ESQSkateKind::Bowl;
			Dist.Name = TEXT("The Reckoning Yard");
			Dist.Color = FLinearColor(1.f, 0.18f, 0.22f);
			Dist.Radius = Zone.Radius + Reach * 0.6f;
		}
		else
		{
			const int32 Slot = Order[LandmarkCount % 6];
			++LandmarkCount;
			Dist.Theme = Themes[Slot];
			Dist.Name = ArenaDistrictNames[Slot][NameRoll];
			Dist.Color = ArenaDistrictColors[Slot];
			Dist.Radius = Zone.Radius + Reach;
		}
		const int32 DistrictIdx = Districts.Add(Dist);

		// A lane from the hub edge to the zone edge facing the hub, with a gentle seeded bend
		const FVector2D Out = Zone.Centre.GetSafeNormal();
		const FVector2D Side(-Out.Y, Out.X);
		const float StartR = HubRadius - 100.f;
		const float EndR = static_cast<float>(Zone.Centre.Size()) - Zone.Radius + 120.f;
		if (EndR - StartR < 600.f) continue;
		const float Bend = BendRoll * LaneBend;
		TArray<FVector2D> Ctrl;
		Ctrl.Add(Out * StartR);
		Ctrl.Add(Out * FMath::Lerp(StartR, EndR, 0.35f) + Side * (Bend * 0.6f));
		Ctrl.Add(Out * FMath::Lerp(StartR, EndR, 0.7f) + Side * Bend);
		Ctrl.Add(Out * EndR);
		FSQLane Lane;
		Lane.District = DistrictIdx;
		Lane.Pts.Add(Ctrl[0]);
		for (int32 Seg = 0; Seg + 1 < Ctrl.Num(); ++Seg)
		{
			const FVector2D& P0 = Ctrl[FMath::Max(Seg - 1, 0)];
			const FVector2D& P1 = Ctrl[Seg];
			const FVector2D& P2 = Ctrl[Seg + 1];
			const FVector2D& P3 = Ctrl[FMath::Min(Seg + 2, Ctrl.Num() - 1)];
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(static_cast<float>(FVector2D::Distance(P1, P2)) / 250.f));
			for (int32 StepIdx = 1; StepIdx <= Steps; ++StepIdx)
			{
				Lane.Pts.Add(ArenaMapCatmull(P0, P1, P2, P3, static_cast<float>(StepIdx) / static_cast<float>(Steps)));
			}
		}
		Lanes.Add(MoveTemp(Lane));
	}
}

void ASQArena::PlanSkateFeatures(const USQData* D, FRandomStream& LR)
{
	SkateFeatures.Reset();
	const int32 PerDistrict = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapSkatePerDistrict"), 6.f)), 0, 12);
	const float MaxH = FMath::Clamp(D->Tune(TEXT("MapTierMaxHeight"), 250.f), 50.f, 300.f);
	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num(); ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		if (!MapZones.IsValidIndex(Dist.Zone)) continue;
		const FSQMapZone& Zone = MapZones[Dist.Zone];
		int32 Made = 0;
		for (int32 Try = 0; Try < PerDistrict * 12 && Made < PerDistrict; ++Try)
		{
			// Every roll every try (deterministic whatever gets rejected)
			const float Ang = LR.FRandRange(0.f, 2.f * PI);
			const float DistRoll = LR.FRand();
			const float KindRoll = LR.FRand();
			const float LenRoll = LR.FRand();
			const float WideRoll = LR.FRand();
			const float HeightRoll = LR.FRand();
			const float TurnRoll = LR.FRand();
			FSQSkateFeature F;
			F.District = DistrictIdx;
			// Half the pieces are the district's theme, the rest a mix
			F.Kind = KindRoll < 0.45f ? Dist.Theme : static_cast<ESQSkateKind>(FMath::FloorToInt(KindRoll * 600.f) % 6);
			switch (F.Kind)
			{
			case ESQSkateKind::Bowl:
				F.Length = FMath::Lerp(1400.f, 2200.f, LenRoll);
				F.Width = F.Length;
				F.Height = FMath::Lerp(150.f, 230.f, HeightRoll);
				break;
			case ESQSkateKind::Bank:
				F.Length = FMath::Lerp(700.f, 1100.f, LenRoll);
				F.Width = FMath::Lerp(700.f, 1300.f, WideRoll);
				F.Height = FMath::Lerp(120.f, MaxH * 0.85f, HeightRoll);
				break;
			case ESQSkateKind::Rollers:
				F.Length = FMath::Lerp(1200.f, 2000.f, LenRoll);
				F.Width = FMath::Lerp(500.f, 800.f, WideRoll);
				F.Height = FMath::Lerp(50.f, 90.f, HeightRoll);
				break;
			case ESQSkateKind::Quarter:
				F.Length = FMath::Lerp(500.f, 700.f, LenRoll);
				F.Width = FMath::Lerp(900.f, 1500.f, WideRoll);
				F.Height = FMath::Lerp(150.f, MaxH * 0.9f, HeightRoll);
				break;
			case ESQSkateKind::Spine:
				F.Length = FMath::Lerp(900.f, 1300.f, LenRoll);
				F.Width = FMath::Lerp(800.f, 1300.f, WideRoll);
				F.Height = FMath::Lerp(100.f, 170.f, HeightRoll);
				break;
			default: // Kicker
				F.Length = FMath::Lerp(350.f, 500.f, LenRoll);
				F.Width = FMath::Lerp(300.f, 450.f, WideRoll);
				F.Height = FMath::Lerp(60.f, 100.f, HeightRoll);
				break;
			}
			const float Inner = Zone.Radius + 350.f + F.Reach() * 0.6f;
			if (Inner >= Dist.Radius) continue;
			F.Centre = Zone.Centre + ArenaMapDir(Ang) * FMath::Lerp(Inner, Dist.Radius, DistRoll);
			// Ride into the landmark most of the time, sometimes across the district; quarters face it (ride out and back)
			const FVector2D ToZone = (Zone.Centre - F.Centre).GetSafeNormal();
			if (F.Kind == ESQSkateKind::Quarter) F.Dir = -ToZone;
			else if (TurnRoll < 0.6f) F.Dir = ToZone;
			else F.Dir = FVector2D(-ToZone.Y, ToZone.X) * (TurnRoll < 0.8f ? 1.0 : -1.0);
			if (SkateRoom(F.Centre, F.Reach()) < 0.f) continue;
			SkateFeatures.Add(F);
			++Made;
		}
	}
}

void ASQArena::PlanParkSpots(const USQData* D, FRandomStream& LR)
{
	ParkSpots.Reset();
	const int32 PerDistrict = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapParkPerDistrict"), 4.f)), 0, 10);
	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num(); ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		if (!MapZones.IsValidIndex(Dist.Zone)) continue;
		const FSQMapZone& Zone = MapZones[Dist.Zone];
		int32 Made = 0;
		for (int32 Try = 0; Try < PerDistrict * 14 && Made < PerDistrict; ++Try)
		{
			const float Ang = LR.FRandRange(0.f, 2.f * PI);
			const float DistRoll = LR.FRand();
			const int32 PieceIdx = LR.RandRange(0, ArenaParkPieceCount - 1);
			const float TurnRoll = LR.FRand();
			const FArenaParkPiece& Piece = ArenaParkPieces[PieceIdx];
			const float FootR = 0.5f * FMath::Sqrt(Piece.Length * Piece.Length + Piece.Width * Piece.Width) + 60.f;
			const float Inner = Zone.Radius + 300.f + FootR;
			if (Inner >= Dist.Radius) continue;
			const FVector2D At = Zone.Centre + ArenaMapDir(Ang) * FMath::Lerp(Inner, Dist.Radius, DistRoll);
			// Off the skate ground, lanes, road and zones, with a capsule-wide gap round walls and other pieces
			if (SkateRoom(At, FootR + 120.f) < 0.f) continue;
			if (IsWallAt(static_cast<float>(At.X), static_cast<float>(At.Y), FootR + 250.f)) continue;
			bool bClear = true;
			for (const FSQParkSpot& Other : ParkSpots)
			{
				if (FVector2D::Distance(Other.At, At) < Other.FootR + FootR + 250.f) bClear = false;
			}
			if (!bClear) continue;
			const FVector2D ToZone = (Zone.Centre - At).GetSafeNormal();
			const FVector2D Ride = TurnRoll < 0.55f ? ToZone : FVector2D(-ToZone.Y, ToZone.X) * (TurnRoll < 0.78f ? 1.0 : -1.0);
			FSQParkSpot Spot;
			Spot.At = At;
			Spot.Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Ride.Y), static_cast<float>(Ride.X)));
			Spot.Piece = PieceIdx;
			Spot.FootR = FootR;
			Spot.District = DistrictIdx;
			ParkSpots.Add(Spot);
			++Made;
		}
	}
}

bool ASQArena::PlaceSetPiece(UStaticMesh* Mesh, float X, float Y, float YawDeg, float FootR, bool bFade, float Scale)
{
	if (!Mesh) return false;
	const FTransform Xf(FRotator(0.f, YawDeg, 0.f), FVector(X, Y, GroundZ(X, Y) - 6.f), FVector(Scale));
	UStaticMeshComponent* Part = AddArtPart(Mesh, Xf, nullptr, true); // the mesh carries MI_Set and its UCX hulls
	if (!Part) return false;
	Part->SetCastShadow(true);
	Part->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	if (bFade) MarkFadeable(Part);
	PropBlockers.Add(Part);
	if (FootR > 0.f)
	{
		BlockerSpots.Add(FVector(X, Y, FootR));
		SetMarks.Add(FVector(X, Y, FootR));
	}
	return true;
}

int32 ASQArena::BuildScenery(const USQData* D)
{
	// Map art pass (Nov 15 2026): the map should be full of art. Everything here STANDS (buildings, towers, walls,
	// trees, stacked cargo pushed up against a building): nothing small lies loose on open floor, so nothing can be
	// mistaken for a pickup. Meshes are two more CC0 KayKit packs (same artist and style as the Halloween kit):
	// Dungeon Remastered (KitD_*) and Medieval Hexagon (KitM_*), imported by Scripts/import_art.py with their own
	// textures. Seeded from the map seed, so server and clients build the same scenery; each piece blocks, registers a
	// horde footprint and fades when it sits between the camera and your reaper. Missing meshes are skipped.
	const float Density = FMath::Clamp(D->Tune(TEXT("MapScenery"), 1.f), 0.f, 2.f);
	if (Density <= 0.f || GridN <= 0) return 0;
	const float FrontYaw = D->Tune(TEXT("MapKitFrontYaw"), -90.f); // the kit's front is +Y after import
	const bool bHarbour = MapShape == FName(TEXT("Causeways"));
	const bool bClock = MapShape == FName(TEXT("Climb"));
	struct FKit { const TCHAR* Name; float Height; };
	static const FKit GraveHouses[] = { { TEXT("KitM_church_red"), 1150.f }, { TEXT("KitM_home_A_blue"), 700.f }, { TEXT("KitM_home_B_red"), 820.f },
		{ TEXT("KitM_tavern_red"), 900.f }, { TEXT("KitM_destroyed"), 620.f }, { TEXT("KitM_well_blue"), 380.f } };
	static const FKit GraveTowers[] = { { TEXT("KitM_tower_A_blue"), 1700.f }, { TEXT("KitM_tower_B_red"), 1900.f }, { TEXT("KitM_windmill_red"), 1500.f } };
	static const FKit GraveYard[] = { { TEXT("KitD_barrel_small_stack"), 175.f }, { TEXT("KitD_crates_stacked"), 215.f }, { TEXT("KitM_tent"), 330.f },
		{ TEXT("KitM_weaponrack"), 190.f }, { TEXT("KitM_wheelbarrow"), 120.f } };
	static const FKit HarbourHouses[] = { { TEXT("KitM_lumbermill_red"), 1050.f }, { TEXT("KitM_watermill_blue"), 980.f }, { TEXT("KitM_market_blue"), 660.f },
		{ TEXT("KitM_blacksmith_blue"), 720.f }, { TEXT("KitM_barracks_blue"), 1000.f }, { TEXT("KitM_home_B_red"), 820.f } };
	static const FKit HarbourTowers[] = { { TEXT("KitM_tower_B_red"), 1900.f }, { TEXT("KitM_tower_A_blue"), 1700.f } };
	static const FKit HarbourYard[] = { { TEXT("KitD_box_stacked"), 330.f }, { TEXT("KitD_crates_stacked"), 215.f }, { TEXT("KitD_keg_decorated"), 205.f },
		{ TEXT("KitD_barrel_large"), 200.f }, { TEXT("KitD_barrel_small_stack"), 175.f } };
	static const FKit ClockHouses[] = { { TEXT("KitM_blacksmith_blue"), 720.f }, { TEXT("KitM_scaffolding"), 860.f }, { TEXT("KitM_stage_B"), 430.f },
		{ TEXT("KitM_tavern_red"), 900.f }, { TEXT("KitM_barracks_blue"), 1000.f }, { TEXT("KitM_home_A_blue"), 700.f } };
	static const FKit ClockTowers[] = { { TEXT("KitM_castle_red"), 2600.f }, { TEXT("KitM_tower_B_red"), 2200.f }, { TEXT("KitM_windmill_red"), 1700.f },
		{ TEXT("KitM_tower_A_blue"), 1800.f } };
	static const FKit ClockYard[] = { { TEXT("KitD_crates_stacked"), 215.f }, { TEXT("KitD_keg_decorated"), 205.f }, { TEXT("KitM_weaponrack"), 190.f },
		{ TEXT("KitD_pillar"), 400.f }, { TEXT("KitD_barrel_large"), 200.f } };
	static const FKit WallKit[] = { { TEXT("KitD_wall"), 420.f }, { TEXT("KitD_wall_arched"), 420.f }, { TEXT("KitD_wall_window_open"), 420.f },
		{ TEXT("KitD_wall_broken"), 420.f }, { TEXT("KitD_wall_doorway"), 420.f }, { TEXT("KitD_wall_scaffold"), 420.f },
		{ TEXT("KitD_wall_archedwindow_gated_scaffold"), 420.f } };
	static const FKit BannerKit[] = { { TEXT("KitD_banner_patternA_red"), 0.f }, { TEXT("KitD_banner_patternB_blue"), 0.f }, { TEXT("KitD_banner_shield_red"), 0.f },
		{ TEXT("KitD_banner_triple_blue"), 0.f }, { TEXT("KitD_banner_thin_white"), 0.f } };
	static const FKit TreeKit[] = { { TEXT("KitM_tree_single_A"), 720.f }, { TEXT("KitM_tree_single_B"), 660.f } };
	static const FKit PillarKit[] = { { TEXT("KitD_pillar_decorated"), 480.f }, { TEXT("KitD_pillar"), 440.f } };
	static const FKit BackKit[] = { { TEXT("KitM_trees_A_large"), 1300.f }, { TEXT("KitM_trees_B_large"), 1600.f }, { TEXT("KitM_trees_A_medium"), 1500.f } };
	struct FKitMesh { UStaticMesh* Mesh = nullptr; float Scale = 1.f; float Foot = 100.f; };
	auto LoadKit = [](const FKit* Items, int32 Num, TArray<FKitMesh>& Out)
	{
		for (int32 ItemIdx = 0; ItemIdx < Num; ++ItemIdx)
		{
			UStaticMesh* KitMesh = SQArt::Prop(FName(Items[ItemIdx].Name));
			if (!KitMesh) continue;
			const FVector Ext = KitMesh->GetBounds().BoxExtent;
			FKitMesh Entry;
			Entry.Mesh = KitMesh;
			Entry.Scale = Items[ItemIdx].Height > 0.f ? Items[ItemIdx].Height / FMath::Max(1.f, static_cast<float>(Ext.Z) * 2.f) : 1.f;
			Entry.Foot = static_cast<float>(FMath::Max(Ext.X, Ext.Y)) * Entry.Scale * 0.8f;
			Out.Add(Entry);
		}
	};
	TArray<FKitMesh> Houses, Towers, YardKit, WallMeshes, Banners, Trees, Pillars, Backs;
	if (bHarbour) { LoadKit(HarbourHouses, UE_ARRAY_COUNT(HarbourHouses), Houses); LoadKit(HarbourTowers, UE_ARRAY_COUNT(HarbourTowers), Towers); LoadKit(HarbourYard, UE_ARRAY_COUNT(HarbourYard), YardKit); }
	else if (bClock) { LoadKit(ClockHouses, UE_ARRAY_COUNT(ClockHouses), Houses); LoadKit(ClockTowers, UE_ARRAY_COUNT(ClockTowers), Towers); LoadKit(ClockYard, UE_ARRAY_COUNT(ClockYard), YardKit); }
	else { LoadKit(GraveHouses, UE_ARRAY_COUNT(GraveHouses), Houses); LoadKit(GraveTowers, UE_ARRAY_COUNT(GraveTowers), Towers); LoadKit(GraveYard, UE_ARRAY_COUNT(GraveYard), YardKit); }
	LoadKit(WallKit, UE_ARRAY_COUNT(WallKit), WallMeshes);
	LoadKit(BannerKit, UE_ARRAY_COUNT(BannerKit), Banners);
	LoadKit(TreeKit, UE_ARRAY_COUNT(TreeKit), Trees);
	LoadKit(PillarKit, UE_ARRAY_COUNT(PillarKit), Pillars);
	LoadKit(BackKit, UE_ARRAY_COUNT(BackKit), Backs);
	if (Houses.Num() + Towers.Num() + YardKit.Num() + WallMeshes.Num() + Trees.Num() + Pillars.Num() + Backs.Num() == 0)
	{
		UE_LOG(LogSQ, Log, TEXT("Map scenery: KayKit pieces (KitD_ / KitM_) not imported yet - run Scripts/setup_project.py"));
		return 0;
	}
	auto ClearAt = [this](float X, float Y, float R) -> bool
	{
		if (FMath::Abs(X) > Half - 300.f - R || FMath::Abs(Y) > Half - 300.f - R) return false;
		if (IsReservedSpot(X, Y, R) || !IsPropFootprintFree(X, Y, R)) return false;
		if (PathMask(X, Y) > 0.05f || IsWaterAt(X, Y)) return false;
		if (GroundSlopeDeg(X, Y) > 14.f || FMath::Abs(SkateHeight(X, Y)) > 4.f) return false;
		return true;
	};
	auto Place = [this](const FKitMesh& Kit, float X, float Y, float YawDeg) -> bool
	{
		return PlaceSetPiece(Kit.Mesh, X, Y, YawDeg, Kit.Foot, true, Kit.Scale);
	};
	auto YawTo = [](const FVector2D& From, const FVector2D& To) -> float
	{
		const FVector2D Dir = To - From;
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X)));
	};
	FRandomStream SR(MapSeed ^ 0x0A27F00D);
	int32 HousesPlaced = 0, TowersPlaced = 0, YardPlaced = 0, WallsPlaced = 0, TreesPlaced = 0, PillarsPlaced = 0, BackPlaced = 0;
	const int32 Budget = FMath::RoundToInt(260.f * Density);
	auto Total = [&]() { return HousesPlaced + TowersPlaced + YardPlaced + WallsPlaced + TreesPlaced + PillarsPlaced; };

	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num() && Total() < Budget; ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		if (!MapZones.IsValidIndex(Dist.Zone) || Dist.Zone == BossZone) continue; // the boss arena stays open
		const FSQMapZone& Zone = MapZones[Dist.Zone];
		// --- House fronts round the plaza, facing in, with cargo / stalls pushed up against a side wall
		const int32 WantHouses = Houses.Num() > 0 ? FMath::RoundToInt(5.f * Density) : 0;
		const float StartAng = SR.FRandRange(0.f, 2.f * PI);
		int32 Made = 0;
		for (int32 Try = 0; Try < WantHouses * 5 && Made < WantHouses; ++Try)
		{
			const FKitMesh& House = Houses[SR.RandRange(0, Houses.Num() - 1)];
			const float Ang = StartAng + 2.f * PI * static_cast<float>(Try) / static_cast<float>(WantHouses * 5) + SR.FRandRange(-0.08f, 0.08f);
			const float Ring = Zone.Radius + House.Foot + SR.FRandRange(420.f, 760.f);
			const FVector2D At = Zone.Centre + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Ring;
			const float AX = static_cast<float>(At.X);
			const float AY = static_cast<float>(At.Y);
			if (!ClearAt(AX, AY, House.Foot + 170.f)) continue;
			const float Yaw = YawTo(At, Zone.Centre) + FrontYaw + SR.FRandRange(-7.f, 7.f);
			if (!Place(House, AX, AY, Yaw)) continue;
			++Made;
			++HousesPlaced;
			const int32 WantYard = YardKit.Num() > 0 ? SR.RandRange(1, 2) : 0;
			const FVector2D Side(-FMath::Sin(Ang), FMath::Cos(Ang));
			for (int32 YardIdx = 0; YardIdx < WantYard; ++YardIdx)
			{
				const FKitMesh& Cargo = YardKit[SR.RandRange(0, YardKit.Num() - 1)];
				const float SideSign = YardIdx == 0 ? 1.f : -1.f;
				const FVector2D CargoAt = At + Side * (SideSign * (House.Foot + Cargo.Foot + 30.f));
				const float CX = static_cast<float>(CargoAt.X);
				const float CY = static_cast<float>(CargoAt.Y);
				if (bHarbour && GroundZ(CX, CY) < TideHighZ - 20.f) continue; // cargo stays out of the tide
				if (!ClearAt(CX, CY, Cargo.Foot + 40.f)) continue;
				if (Place(Cargo, CX, CY, Yaw + SR.FRandRange(-20.f, 20.f))) ++YardPlaced;
			}
		}
		// --- Towers behind the district (the skyline you run toward)
		const FVector2D Out = Zone.Centre.GetSafeNormal();
		const int32 WantTowers = Towers.Num() > 0 ? FMath::RoundToInt(2.f * Density) : 0;
		Made = 0;
		for (int32 Try = 0; Try < WantTowers * 8 && Made < WantTowers; ++Try)
		{
			const FKitMesh& Tower = Towers[SR.RandRange(0, Towers.Num() - 1)];
			const float Ang = FMath::Atan2(static_cast<float>(Out.Y), static_cast<float>(Out.X)) + SR.FRandRange(-1.1f, 1.1f);
			const FVector2D At = Zone.Centre + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * SR.FRandRange(Zone.Radius + 1500.f, FMath::Max(Zone.Radius + 1700.f, Dist.Radius));
			const float AX = static_cast<float>(At.X);
			const float AY = static_cast<float>(At.Y);
			if (!ClearAt(AX, AY, Tower.Foot + 200.f)) continue;
			if (Place(Tower, AX, AY, YawTo(At, Zone.Centre) + FrontYaw)) { ++Made; ++TowersPlaced; }
		}
		// --- Wall runs with a banner: three wall pieces end to end, across the line of sight, well off the plaza
		const int32 WantRuns = WallMeshes.Num() > 0 ? FMath::RoundToInt(2.f * Density) : 0;
		Made = 0;
		for (int32 Try = 0; Try < WantRuns * 8 && Made < WantRuns; ++Try)
		{
			const float Ang = SR.FRandRange(0.f, 2.f * PI);
			const FVector2D Radial(FMath::Cos(Ang), FMath::Sin(Ang));
			const FVector2D Tangent(-Radial.Y, Radial.X);
			const FVector2D Mid = Zone.Centre + Radial * SR.FRandRange(Zone.Radius + 1100.f, Zone.Radius + 2100.f);
			const float SegLen = WallMeshes[0].Mesh ? static_cast<float>(WallMeshes[0].Mesh->GetBounds().BoxExtent.X) * 2.f * WallMeshes[0].Scale : 420.f;
			bool bRunClear = true;
			for (int32 Seg = -1; Seg <= 1 && bRunClear; ++Seg)
			{
				const FVector2D SegAt = Mid + Tangent * (SegLen * static_cast<float>(Seg));
				bRunClear = ClearAt(static_cast<float>(SegAt.X), static_cast<float>(SegAt.Y), SegLen * 0.5f + 150.f);
			}
			if (!bRunClear) continue;
			const float WallYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Tangent.Y), static_cast<float>(Tangent.X)));
			for (int32 Seg = -1; Seg <= 1; ++Seg)
			{
				const FKitMesh& WallPiece = WallMeshes[SR.RandRange(0, WallMeshes.Num() - 1)];
				const FVector2D SegAt = Mid + Tangent * (SegLen * static_cast<float>(Seg));
				const float WX = static_cast<float>(SegAt.X);
				const float WY = static_cast<float>(SegAt.Y);
				if (!PlaceSetPiece(WallPiece.Mesh, WX, WY, WallYaw, SegLen * 0.5f, true, WallPiece.Scale)) continue;
				++WallsPlaced;
				// The banner hangs on the wall: same origin, same scale, no collision of its own
				if (Seg == 0 && Banners.Num() > 0)
				{
					const FKitMesh& Banner = Banners[SR.RandRange(0, Banners.Num() - 1)];
					const FTransform BannerXf(FRotator(0.f, WallYaw, 0.f), FVector(WX, WY, GroundZ(WX, WY) - 6.f), FVector(WallPiece.Scale));
					if (UStaticMeshComponent* BannerPart = AddArtPart(Banner.Mesh, BannerXf, nullptr, false)) MarkFadeable(BannerPart);
				}
			}
			++Made;
		}
	}

	// --- Pillar pairs along the lanes (gateposts you run between), every so often
	if (Pillars.Num() > 0)
	{
		const float PillarStep = 1500.f / FMath::Max(0.5f, Density);
		for (const FSQLane& Lane : Lanes)
		{
			float Walked = PillarStep * 0.6f;
			for (int32 PtIdx = 0; PtIdx + 1 < Lane.Pts.Num() && Total() < Budget; ++PtIdx)
			{
				const FVector2D SegA = Lane.Pts[PtIdx];
				const FVector2D SegB = Lane.Pts[PtIdx + 1];
				const float SegLen = static_cast<float>(FVector2D::Distance(SegA, SegB));
				if (SegLen < 1.f) continue;
				const FVector2D Along = (SegB - SegA) / SegLen;
				const FVector2D Across(-Along.Y, Along.X);
				for (; Walked < SegLen; Walked += PillarStep)
				{
					const FVector2D On = SegA + Along * Walked;
					if (On.Size() < HubRadius + 500.f) continue;
					const FKitMesh& Pillar = Pillars[SR.RandRange(0, Pillars.Num() - 1)];
					for (const float SideSign : { -1.f, 1.f })
					{
						const FVector2D At = On + Across * (SideSign * (LaneHalfWidth + 260.f));
						const float AX = static_cast<float>(At.X);
						const float AY = static_cast<float>(At.Y);
						if (!ClearAt(AX, AY, Pillar.Foot + 60.f)) continue;
						if (Place(Pillar, AX, AY, YawTo(At, On) + FrontYaw)) ++PillarsPlaced;
					}
				}
				Walked -= SegLen;
			}
		}
	}

	// --- Groves: clusters of trees filling the open ground between the districts
	if (Trees.Num() > 0)
	{
		const int32 WantGroves = FMath::RoundToInt((bHarbour ? 4.f : (bClock ? 6.f : 10.f)) * Density);
		int32 Groves = 0;
		for (int32 Try = 0; Try < WantGroves * 12 && Groves < WantGroves && Total() < Budget; ++Try)
		{
			const FVector2D Core(SR.FRandRange(-Half + 900.f, Half - 900.f), SR.FRandRange(-Half + 900.f, Half - 900.f));
			if (Core.Size() < HubRadius + 1200.f) continue;
			if (!ClearAt(static_cast<float>(Core.X), static_cast<float>(Core.Y), 380.f)) continue;
			if (bHarbour && GroundZ(static_cast<float>(Core.X), static_cast<float>(Core.Y)) < TideHighZ - 20.f) continue;
			const int32 InGrove = SR.RandRange(4, 6);
			int32 Planted = 0;
			for (int32 TreeTry = 0; TreeTry < InGrove * 4 && Planted < InGrove; ++TreeTry)
			{
				FKitMesh Tree = Trees[SR.RandRange(0, Trees.Num() - 1)];
				Tree.Scale *= SR.FRandRange(0.8f, 1.3f);
				Tree.Foot = 70.f; // the trunk blocks the horde; the canopy is overhead
				const float TreeAng = SR.FRandRange(0.f, 2.f * PI);
				const FVector2D At = Core + FVector2D(FMath::Cos(TreeAng), FMath::Sin(TreeAng)) * SR.FRandRange(0.f, 520.f);
				const float AX = static_cast<float>(At.X);
				const float AY = static_cast<float>(At.Y);
				if (!ClearAt(AX, AY, 150.f)) continue;
				if (Place(Tree, AX, AY, SR.FRandRange(0.f, 360.f))) { ++Planted; ++TreesPlaced; }
			}
			if (Planted > 0) ++Groves;
		}
	}

	// --- Backdrop: a ring of forest beyond the edge of the arena (no collision: nobody gets there)
	if (Backs.Num() > 0)
	{
		const int32 WantBack = FMath::RoundToInt(44.f * Density);
		for (int32 BackIdx = 0; BackIdx < WantBack; ++BackIdx)
		{
			const FKitMesh& Back = Backs[SR.RandRange(0, Backs.Num() - 1)];
			const float Ang = 2.f * PI * (static_cast<float>(BackIdx) + SR.FRandRange(-0.3f, 0.3f)) / static_cast<float>(WantBack);
			const float Ring = Half + Back.Foot + SR.FRandRange(300.f, 1800.f);
			const float BX = FMath::Cos(Ang) * Ring;
			const float BY = FMath::Sin(Ang) * Ring;
			const float BackScale = Back.Scale * SR.FRandRange(0.8f, 1.25f);
			const FTransform BackXf(FRotator(0.f, SR.FRandRange(0.f, 360.f), 0.f), FVector(BX, BY, -40.f), FVector(BackScale));
			if (AddArtPart(Back.Mesh, BackXf, nullptr, false)) ++BackPlaced;
		}
	}
	UE_LOG(LogSQ, Log, TEXT("Map scenery: %d houses, %d towers, %d cargo, %d wall pieces, %d trees, %d pillars, %d backdrop"),
		HousesPlaced, TowersPlaced, YardPlaced, WallsPlaced, TreesPlaced, PillarsPlaced, BackPlaced);
	return Total() + BackPlaced;
}

int32 ASQArena::BuildSetPieces(const USQData* D, bool bVisuals)
{
	// Three-map remaster: districts become places. Each landmark gets a building behind it (chapel, lighthouse, clock
	// tower...), a gate where its lane arrives, a broken yard wall round it, an avenue of trees / pilings and lamp posts
	// along the lane, and statues round the hub. All standing structures with collision and horde footprints: nothing
	// here lies on the floor. Meshes: ArtSource/Props/Set_*.fbx (Biomes.csv SetPieces = Role:Mesh|...).
	if (SetRoles.Num() == 0 || D->Tune(TEXT("MapSetPieces"), 1.f) < 0.5f) return 0;
	auto Load = [this](const TCHAR* RoleName, TArray<UStaticMesh*>& OutMeshes)
	{
		if (const TArray<FName>* Names = SetRoles.Find(FName(RoleName)))
		{
			for (const FName& MeshName : *Names)
			{
				if (UStaticMesh* Mesh = SQArt::Prop(MeshName)) OutMeshes.Add(Mesh);
			}
		}
	};
	TArray<UStaticMesh*> Landmarks, Gates, WallMeshes, Trees, Statues, LampMeshes;
	Load(TEXT("Landmark"), Landmarks);
	Load(TEXT("Gate"), Gates);
	Load(TEXT("Wall"), WallMeshes);
	Load(TEXT("Tree"), Trees);
	Load(TEXT("Statue"), Statues);
	Load(TEXT("Lamp"), LampMeshes);
	if (Landmarks.Num() + Gates.Num() + WallMeshes.Num() + Trees.Num() + Statues.Num() + LampMeshes.Num() == 0)
	{
		UE_LOG(LogSQ, Log, TEXT("Map set pieces: none imported yet for %s (run Scripts/setup_project.py)"), *BiomeId.ToString());
		return 0;
	}
	auto FootOf = [](const UStaticMesh* Mesh, float Share) -> float
	{
		const FVector Ext = Mesh->GetBounds().BoxExtent;
		return static_cast<float>(FMath::Max(Ext.X, Ext.Y)) * Share;
	};
	auto Clear = [this](float X, float Y, float R) -> bool
	{
		if (FMath::Abs(X) > Half - 300.f - R || FMath::Abs(Y) > Half - 300.f - R) return false;
		if (IsReservedSpot(X, Y, R) || !IsPropFootprintFree(X, Y, R)) return false;
		if (PathMask(X, Y) > 0.05f || IsWaterAt(X, Y)) return false;
		if (GroundSlopeDeg(X, Y) > 16.f || FMath::Abs(SkateHeight(X, Y)) > 4.f) return false;
		return true;
	};
	FRandomStream SetRand(MapSeed ^ 0x5E7B1EC5);
	int32 LandmarksPlaced = 0, GatesPlaced = 0, WallsPlaced = 0, TreesPlaced = 0, LampsPlaced = 0, StatuesPlaced = 0;
	const int32 LandmarkShift = Landmarks.Num() > 0 ? SetRand.RandRange(0, Landmarks.Num() - 1) : 0;
	UInstancedStaticMeshComponent* WallISM = nullptr;
	UStaticMesh* WallMesh = WallMeshes.Num() > 0 ? WallMeshes[0] : nullptr;
	const float WallLen = WallMesh ? static_cast<float>(WallMesh->GetBounds().BoxExtent.X) * 2.f : 400.f;

	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num(); ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		if (!MapZones.IsValidIndex(Dist.Zone)) continue;
		const FSQMapZone& Zone = MapZones[Dist.Zone];
		const bool bBoss = Dist.Zone == BossZone;
		const FVector2D Out = Zone.Centre.GetSafeNormal();
		const float OutAng = FMath::Atan2(static_cast<float>(Out.Y), static_cast<float>(Out.X));
		float LandmarkAng = OutAng;
		bool bLandmark = false;

		// --- The landmark building, behind the zone, facing in (the boss arena stays open: no building there)
		if (!bBoss && Landmarks.Num() > 0)
		{
			UStaticMesh* Mesh = Landmarks[(DistrictIdx + LandmarkShift) % Landmarks.Num()];
			const float Foot = FootOf(Mesh, 0.85f);
			static const float TryDeg[] = { 0.f, 32.f, -32.f, 64.f, -64.f, 96.f, -96.f };
			for (const float Deg : TryDeg)
			{
				const float Ang = OutAng + FMath::DegreesToRadians(Deg);
				const FVector2D At = Zone.Centre + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * (Zone.Radius + Foot + 70.f);
				const float AX = static_cast<float>(At.X);
				const float AY = static_cast<float>(At.Y);
				if (!Clear(AX, AY, Foot)) continue;
				if (PlaceSetPiece(Mesh, AX, AY, FMath::RadiansToDegrees(Ang) + 180.f, Foot, true))
				{
					LandmarkAng = Ang;
					bLandmark = true;
					++LandmarksPlaced;
				}
				break;
			}
		}

		// --- The gate: where the district's lane reaches the zone
		float GateAng = OutAng + PI; // towards the hub when there is no lane
		for (const FSQLane& Lane : Lanes)
		{
			if (Lane.District != DistrictIdx || Lane.Pts.Num() < 2) continue;
			for (int32 i = 1; i < Lane.Pts.Num(); ++i)
			{
				if (FVector2D::Distance(Lane.Pts[i], Zone.Centre) > Zone.Radius + 200.f) continue;
				const FVector2D Fwd = (Lane.Pts[i] - Lane.Pts[i - 1]).GetSafeNormal();
				const FVector2D Side(-Fwd.Y, Fwd.X);
				const FVector2D At = Lane.Pts[i];
				GateAng = FMath::Atan2(static_cast<float>(At.Y - Zone.Centre.Y), static_cast<float>(At.X - Zone.Centre.X));
				if (Gates.Num() > 0)
				{
					UStaticMesh* GateMesh = Gates[DistrictIdx % Gates.Num()];
					const float PierOff = FootOf(GateMesh, 1.f) - 85.f; // pier centres sit 85 cm inside the mesh's ends
					const FVector2D PierA = At + Side * PierOff;
					const FVector2D PierB = At - Side * PierOff;
					const bool bFree = IsPropFootprintFree(static_cast<float>(PierA.X), static_cast<float>(PierA.Y), 80.f)
						&& IsPropFootprintFree(static_cast<float>(PierB.X), static_cast<float>(PierB.Y), 80.f)
						&& GroundSlopeDeg(static_cast<float>(At.X), static_cast<float>(At.Y)) < 14.f && PierOff > LaneHalfWidth + 90.f;
					if (bFree && PlaceSetPiece(GateMesh, static_cast<float>(At.X), static_cast<float>(At.Y),
						FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Fwd.Y), static_cast<float>(Fwd.X))), 0.f, true))
					{
						BlockerSpots.Add(FVector(PierA.X, PierA.Y, 80.0));
						BlockerSpots.Add(FVector(PierB.X, PierB.Y, 80.0));
						SetMarks.Add(FVector(PierA.X, PierA.Y, 90.0));
						SetMarks.Add(FVector(PierB.X, PierB.Y, 90.0));
						++GatesPlaced;
					}
				}
				break;
			}
			break;
		}

		// --- The yard wall: a broken ring from both sides of the gate round towards the landmark (gaps every few
		// lengths, so the yard has several ways in and nothing can be sealed)
		if (WallMesh && !bBoss)
		{
			const float RingR = Zone.Radius + 110.f;
			const float StepAng = (WallLen + 26.f) / RingR;
			const float SkipGate = (LaneHalfWidth + 420.f) / RingR;
			struct FWallSeg { FVector2D At; float Yaw; };
			TArray<FWallSeg, TInlineAllocator<48>> Segs;
			for (int32 SideSign = -1; SideSign <= 1; SideSign += 2)
			{
				int32 Run = 0;
				for (float Sweep = SkipGate + StepAng * 0.5f; Sweep < PI * 0.86f; Sweep += StepAng, ++Run)
				{
					if (Run % 4 == 3) continue; // a gap
					const float Ang = GateAng + SideSign * Sweep;
					// Leave the landmark's frontage open
					if (bLandmark && FMath::Abs(FMath::FindDeltaAngleRadians(Ang, LandmarkAng)) < 0.42f) continue;
					const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
					const FVector2D At = Zone.Centre + Dir * RingR;
					const FVector2D Tan(-Dir.Y, Dir.X);
					bool bOk = true;
					for (int32 Probe = -1; Probe <= 1 && bOk; ++Probe)
					{
						const FVector2D ProbeAt = At + Tan * (Probe * WallLen * 0.38f);
						bOk = Clear(static_cast<float>(ProbeAt.X), static_cast<float>(ProbeAt.Y), 45.f);
					}
					if (bOk) Segs.Add({ At, FMath::RadiansToDegrees(Ang) + 90.f });
				}
			}
			for (const FWallSeg& Seg : Segs)
			{
				if (!WallISM)
				{
					WallISM = MakeArtISM(WallMesh, nullptr, true, true);
					WallISM->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
					PropBlockers.Add(WallISM);
				}
				const float SX = static_cast<float>(Seg.At.X);
				const float SY = static_cast<float>(Seg.At.Y);
				WallISM->AddInstance(FTransform(FRotator(0.f, Seg.Yaw, 0.f), FVector(SX, SY, GroundZ(SX, SY) - 8.f)));
				const float YawRad = FMath::DegreesToRadians(Seg.Yaw);
				const FVector2D Along(FMath::Cos(YawRad), FMath::Sin(YawRad));
				for (int32 Probe = -1; Probe <= 1; ++Probe)
				{
					const FVector2D B = Seg.At + Along * (Probe * WallLen * 0.36f);
					BlockerSpots.Add(FVector(B.X, B.Y, 48.0));
				}
				SetMarks.Add(FVector(SX, SY, WallLen * 0.3f));
				++WallsPlaced;
			}
		}
	}

	// --- Avenues: trees (or pilings) down both sides of every lane, lamp posts between them
	const float AvenueGap = FMath::Clamp(D->Tune(TEXT("MapAvenueSpacing"), 820.f), 300.f, 4000.f);
	const int32 AvenueMax = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapAvenueMax"), 72.f)), 0, 300);
	const int32 LampMax = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapLampPostMax"), 64.f)), 0, 300);
	UStaticMesh* LampMesh = LampMeshes.Num() > 0 ? LampMeshes[0] : nullptr;
	UInstancedStaticMeshComponent* LampISM = nullptr;
	UInstancedStaticMeshComponent* LampGlowISM = nullptr;
	for (const FSQLane& Lane : Lanes)
	{
		float Acc = AvenueGap * 0.6f;
		int32 Slot = 0;
		for (int32 i = 1; i < Lane.Pts.Num(); ++i)
		{
			const FVector2D Seg = Lane.Pts[i] - Lane.Pts[i - 1];
			Acc += static_cast<float>(Seg.Size());
			if (Acc < AvenueGap * 0.5f) continue;
			Acc = 0.f;
			const bool bTreeSlot = (Slot++ & 1) == 0;
			const FVector2D Fwd = Seg.GetSafeNormal();
			const FVector2D Side(-Fwd.Y, Fwd.X);
			for (int32 SideSign = -1; SideSign <= 1; SideSign += 2)
			{
				// Fixed rolls per slot
				const float Yaw = SetRand.FRandRange(0.f, 360.f);
				const float Scale = SetRand.FRandRange(0.88f, 1.18f);
				const int32 Pick = SetRand.RandRange(0, 1023);
				if (bTreeSlot && Trees.Num() > 0 && TreesPlaced < AvenueMax)
				{
					UStaticMesh* TreeMesh = Trees[Pick % Trees.Num()];
					const FVector2D At = Lane.Pts[i] + Side * (SideSign * (LaneHalfWidth + 330.f));
					if (Clear(static_cast<float>(At.X), static_cast<float>(At.Y), 60.f)
						&& PlaceSetPiece(TreeMesh, static_cast<float>(At.X), static_cast<float>(At.Y), Yaw, 60.f, true, Scale)) ++TreesPlaced;
				}
				else if (!bTreeSlot && LampMesh && LampsPlaced < LampMax)
				{
					const FVector2D At = Lane.Pts[i] + Side * (SideSign * (LaneHalfWidth + 150.f));
					const float LX = static_cast<float>(At.X);
					const float LY = static_cast<float>(At.Y);
					if (!Clear(LX, LY, 30.f)) continue;
					if (!LampISM)
					{
						LampISM = MakeArtISM(LampMesh, nullptr, true, true);
						LampISM->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
						PropBlockers.Add(LampISM);
					}
					// The lamp's arm (local +Y) reaches over the lane
					const float ArmYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-Side.Y * SideSign), static_cast<float>(-Side.X * SideSign))) - 90.f;
					const float LZ = GroundZ(LX, LY) - 4.f;
					LampISM->AddInstance(FTransform(FRotator(0.f, ArmYaw, 0.f), FVector(LX, LY, LZ)));
					BlockerSpots.Add(FVector(LX, LY, 24.0));
					SetMarks.Add(FVector(LX, LY, 30.0));
					if (bVisuals)
					{
						if (!LampGlowISM)
						{
							const FLinearColor Warm(1.f, 0.66f, 0.28f);
							UMaterialInstanceDynamic* GlowMID = SQArt::GlowMID(this, Warm, 2.4f, 0.f);
							LampGlowISM = MakeArtISM(SQ::Mesh(TEXT("Sphere")), GlowMID ? GlowMID : SurfaceMID(this, Warm, 3.f), false, false);
						}
						const FVector2D Head = At - Side * (SideSign * 32.f);
						LampGlowISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Head.X, Head.Y, LZ + 350.f), FVector(0.55f)));
					}
					++LampsPlaced;
				}
			}
		}
	}

	// --- Statues round the lantern square, between the lanes
	if (Statues.Num() > 0)
	{
		const int32 Want = FMath::Clamp(FMath::RoundToInt(D->Tune(TEXT("MapHubStatues"), 6.f)), 0, 16);
		for (int32 k = 0; k < Want * 2 && StatuesPlaced < Want; ++k)
		{
			const float Ang = 2.f * PI * (static_cast<float>(k) + 0.5f) / static_cast<float>(Want * 2);
			UStaticMesh* Mesh = Statues[k % Statues.Num()];
			const float Foot = FootOf(Mesh, 0.75f);
			const float SX = FMath::Cos(Ang) * (HubRadius + Foot + 60.f);
			const float SY = FMath::Sin(Ang) * (HubRadius + Foot + 60.f);
			if (DistToLane(SX, SY) < LaneHalfWidth + Foot + 200.f || !Clear(SX, SY, Foot)) continue;
			if (PlaceSetPiece(Mesh, SX, SY, FMath::RadiansToDegrees(Ang) + 180.f, Foot, true)) ++StatuesPlaced;
		}
	}
	UE_LOG(LogSQ, Log, TEXT("Map set pieces (%s, shape %s): %d landmarks, %d gates, %d wall lengths, %d avenue trees, %d lamp posts, %d statues"),
		*BiomeId.ToString(), *MapShape.ToString(), LandmarksPlaced, GatesPlaced, WallsPlaced, TreesPlaced, LampsPlaced, StatuesPlaced);
	return LandmarksPlaced;
}

void ASQArena::BuildDistricts(const USQData* D, bool bVisuals)
{
	// --- Bone park pieces: real collision on every machine (reapers ride them), horde footprints so the swarm goes round
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> ParkISMs;
	int32 ParkBuilt = 0;
	for (const FSQParkSpot& Spot : ParkSpots)
	{
		UStaticMesh* PieceMesh = SQArt::Prop(FName(ArenaParkPieces[Spot.Piece].Name));
		const float SX = static_cast<float>(Spot.At.X);
		const float SY = static_cast<float>(Spot.At.Y);
		if (!PieceMesh || !IsPropFootprintFree(SX, SY, Spot.FootR * 0.8f)) continue;
		UInstancedStaticMeshComponent*& ParkISM = ParkISMs.FindOrAdd(PieceMesh);
		if (!ParkISM)
		{
			ParkISM = MakeArtISM(PieceMesh, nullptr, true, true); // the meshes carry MI_Park
			ParkISM->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			PropBlockers.Add(ParkISM);
		}
		ParkISM->AddInstance(FTransform(FRotator(0.f, Spot.Yaw, 0.f), FVector(SX, SY, GroundZ(SX, SY) - 2.f)));
		BlockerSpots.Add(FVector(SX, SY, Spot.FootR * 0.75f));
		++ParkBuilt;
	}
	// --- Landmark set pieces, gates, yard walls, avenues and lamp posts (three-map remaster). When the biome lists
	// SetPieces and the meshes are imported they replace the pillar monuments below.
	const bool bHasSetLandmarks = BuildSetPieces(D, bVisuals) > 0;
	BuildScenery(D);
	// --- District monuments (map art pass): a tall stone marker at the back of every landmark instead of the old
	// neon beacon. Blocks like any prop (footprint on every machine, the horde walks round it); skipped where it won't fit.
	TArray<FVector, TInlineAllocator<8>> Monuments; // X, Y, district index
	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num() && !bHasSetLandmarks; ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		if (!MapZones.IsValidIndex(Dist.Zone)) continue;
		const FSQMapZone& Zone = MapZones[Dist.Zone];
		const FVector2D MonAt = Zone.Centre + Zone.Centre.GetSafeNormal() * (Zone.Radius * 0.92f);
		const float MX = static_cast<float>(MonAt.X);
		const float MY = static_cast<float>(MonAt.Y);
		if (!IsPropFootprintFree(MX, MY, 70.f) || PathMask(MX, MY) > 0.05f || DistToLane(MX, MY) < LaneHalfWidth + 120.f || GroundZ(MX, MY) < -10.f) continue;
		if (UStaticMeshComponent* MonBlock = AddBlock(TEXT("Cylinder"), FVector(MX, MY, GroundZ(MX, MY) + 150.f), FRotator::ZeroRotator, FVector(1.3f, 1.3f, 3.f), FLinearColor::Black, true))
		{
			MonBlock->SetVisibility(false);
			MonBlock->SetCastShadow(false);
			MonBlock->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			PropBlockers.Add(MonBlock);
		}
		BlockerSpots.Add(FVector(MX, MY, 70.0));
		Monuments.Add(FVector(MX, MY, static_cast<double>(DistrictIdx)));
	}
	if (!bVisuals) return;

	UStaticMesh* CubeMesh = SQ::Mesh(TEXT("Cube"));
	UStaticMesh* SphereMesh = SQ::Mesh(TEXT("Sphere"));
	const int32 DecorCull = FMath::RoundToInt(D->Tune(TEXT("DecorCullDistance"), 9000.f));
	const FLinearColor WarmFlame(1.f, 0.62f, 0.25f);

	// --- Monument visuals: a kit pillar (plain stone column without the kit) with a small orb in the district colour
	static const TCHAR* const MonumentNames[] = { TEXT("pillar"), TEXT("post_skull"), TEXT("fence_pillar"), TEXT("post") };
	UStaticMesh* MonMesh = nullptr;
	for (const TCHAR* const MonName : MonumentNames)
	{
		MonMesh = SQArt::Prop(FName(MonName));
		if (MonMesh) break;
	}
	const float MonHeight = FMath::Clamp(D->Tune(TEXT("MapMonumentHeight"), 520.f), 200.f, 900.f);
	for (const FVector& Mon : Monuments)
	{
		const int32 DistrictIdx = FMath::RoundToInt(static_cast<float>(Mon.Z));
		if (!Districts.IsValidIndex(DistrictIdx)) continue;
		const FLinearColor OrbColor = Districts[DistrictIdx].Color;
		const float MX = static_cast<float>(Mon.X);
		const float MY = static_cast<float>(Mon.Y);
		const FVector Base(MX, MY, GroundZ(MX, MY) - 6.f);
		UStaticMeshComponent* Column = nullptr;
		if (MonMesh)
		{
			const float MonScale = FitMeshHeight(MonMesh, MonHeight);
			// Wider than a straight fit: a stocky monument reads from above
			Column = AddArtPart(MonMesh, ArenaSitTransform(MonMesh, Base, FQuat::Identity, FVector(MonScale * 1.5f, MonScale * 1.5f, MonScale)),
				SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White), false);
		}
		else
		{
			Column = AddBlock(TEXT("Cylinder"), Base + FVector(0.f, 0.f, MonHeight * 0.5f), FRotator::ZeroRotator, FVector(1.1f, 1.1f, MonHeight / 100.f), FLinearColor(0.2f, 0.19f, 0.22f), false);
		}
		if (Column)
		{
			Column->SetCastShadow(true);
			MarkFadeable(Column);
		}
		if (UStaticMeshComponent* Orb = AddBlock(TEXT("Sphere"), Base + FVector(0.f, 0.f, MonHeight + 45.f), FRotator::ZeroRotator, FVector(0.5f), OrbColor, false))
		{
			Orb->SetMaterial(0, SurfaceMID(this, OrbColor, 2.2f));
			Orb->SetCastShadow(false);
			MarkFadeable(Orb);
		}
	}

	// --- Lane lights: small candles alternate down both sides of every hub lane with a warm flame that leans toward
	// the district colour. They replace the neon chevrons: the lit path itself says "this way".
	static const TCHAR* const CandleNames[] = { TEXT("candle_triple"), TEXT("skull_candle"), TEXT("candle"), TEXT("candle_melted"), TEXT("lantern_standing") };
	UStaticMesh* CandleMesh = nullptr;
	for (const TCHAR* const CandleName : CandleNames)
	{
		CandleMesh = SQArt::Prop(FName(CandleName));
		if (CandleMesh) break;
	}
	const float LightGap = FMath::Clamp(D->Tune(TEXT("MapLaneLightSpacing"), 520.f), 200.f, 3000.f);
	const float FlameGlow = FMath::Clamp(D->Tune(TEXT("MapLaneLightGlow"), 3.f), 0.f, 8.f);
	const float CandleH = 58.f;
	UInstancedStaticMeshComponent* CandleISM = nullptr;
	const bool bFloorClutter = D->Tune(TEXT("MapFloorClutter"), 0.f) > 0.5f; // floor rule: see Rebuild
	if (CandleMesh && bFloorClutter)
	{
		UMaterialInstanceDynamic* CandleMID = SQArt::TintMID(this, SQArt::PropTexture(), FLinearColor::White);
		if (CandleMID) CandleMID->SetScalarParameterValue(TEXT("Glow"), 0.2f);
		CandleISM = MakeArtISM(CandleMesh, CandleMID, false, false);
		CandleISM->SetCullDistances(DecorCull, DecorCull + 2000);
	}
	int32 LaneLights = 0;
	for (int32 DistrictIdx = 0; DistrictIdx < Districts.Num() && SphereMesh && bFloorClutter; ++DistrictIdx)
	{
		const FSQDistrict& Dist = Districts[DistrictIdx];
		UInstancedStaticMeshComponent* FlameISM = MakeArtISM(SphereMesh, SurfaceMID(this, FMath::Lerp(WarmFlame, Dist.Color, 0.4f), FlameGlow), false, false);
		FlameISM->SetCullDistances(DecorCull, DecorCull + 2000);
		for (const FSQLane& Lane : Lanes)
		{
			if (Lane.District != DistrictIdx || Lane.Pts.Num() < 2) continue;
			float Acc = LightGap * 0.5f;
			int32 SideFlip = 0;
			for (int32 i = 1; i < Lane.Pts.Num(); ++i)
			{
				const FVector2D Seg = Lane.Pts[i] - Lane.Pts[i - 1];
				Acc += static_cast<float>(Seg.Size());
				if (Acc < LightGap) continue;
				Acc = 0.f;
				const FVector2D Fwd = Seg.GetSafeNormal();
				const FVector2D Side(-Fwd.Y, Fwd.X);
				const FVector2D At = Lane.Pts[i] + Side * ((LaneHalfWidth + 45.f) * ((SideFlip++ & 1) ? 1.f : -1.f));
				const float AX = static_cast<float>(At.X);
				const float AY = static_cast<float>(At.Y);
				// Not on a plaza floor, the road, water, a ramp or inside a wall
				if (AX * AX + AY * AY < FMath::Square(HubRadius + 40.f) || PathMask(AX, AY) > 0.05f || GroundZ(AX, AY) < -10.f) continue;
				if (GroundSlopeDeg(AX, AY) > 14.f || IsWallAt(AX, AY, 30.f)) continue;
				bool bOnZone = false;
				for (const FSQMapZone& Zone : MapZones)
				{
					if (Zone.Kind != ESQZoneKind::Hub && FVector2D::DistSquared(Zone.Centre, At) < FMath::Square(Zone.Radius + 40.f)) bOnZone = true;
				}
				if (bOnZone) continue;
				const float AZ = GroundZ(AX, AY);
				float FlameZ = AZ + 22.f;
				if (CandleISM)
				{
					const float CandleScale = FitMeshHeight(CandleMesh, CandleH);
					CandleISM->AddInstance(ArenaSitTransform(CandleMesh, FVector(AX, AY, AZ - 2.f), FQuat(FRotator(0.f, static_cast<float>((LaneLights * 67) % 360), 0.f)), FVector(CandleScale)), false);
					FlameZ = AZ + CandleH + 6.f;
				}
				FlameISM->AddInstance(FTransform(FRotator::ZeroRotator, FVector(AX, AY, FlameZ), FVector(0.09f, 0.09f, 0.13f)), false);
				++LaneLights;
			}
		}
	}

	// --- Skate coping: a pale stone edge along every lip (bank / quarter / kicker), ridge (spine) and bowl rim, laid
	// on the ground. With the concrete tone painted under them the features read as built structures, not lumps.
	if (CubeMesh && bFloorClutter)
	{
		UInstancedStaticMeshComponent* CopingISM = MakeArtISM(CubeMesh, SurfaceMID(this, FLinearColor(0.50f, 0.49f, 0.46f)), false, false);
		CopingISM->SetCullDistances(DecorCull, DecorCull + 2000);
		const float CopeStep = 118.f;
		auto LayCoping = [&](const FVector2D& At, float YawDeg)
		{
			const float CX = static_cast<float>(At.X);
			const float CY = static_cast<float>(At.Y);
			if (FMath::Abs(CX) > Half - 150.f || FMath::Abs(CY) > Half - 150.f || IsWallAt(CX, CY, 20.f)) return;
			const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, GroundNormal(CX, CY));
			CopingISM->AddInstance(FTransform(Align * FQuat(FRotator(0.f, YawDeg, 0.f)), FVector(CX, CY, GroundZ(CX, CY) + 3.f), FVector(1.1f, 0.24f, 0.1f)), false);
		};
		for (const FSQSkateFeature& F : SkateFeatures)
		{
			if (F.Kind == ESQSkateKind::Bowl)
			{
				const float RimR = F.Length * 0.5f;
				const int32 RimCount = FMath::Clamp(FMath::RoundToInt(2.f * PI * RimR / CopeStep), 8, 160);
				for (int32 k = 0; k < RimCount; ++k)
				{
					const float Ang = 2.f * PI * k / RimCount;
					LayCoping(F.Centre + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * RimR, FMath::RadiansToDegrees(Ang) + 90.f);
				}
				continue;
			}
			if (F.Kind == ESQSkateKind::Rollers) continue;
			const FVector2D Across(-F.Dir.Y, F.Dir.X);
			const float AcrossYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Across.Y), static_cast<float>(Across.X)));
			const float LipU = F.Kind == ESQSkateKind::Spine ? 0.f : F.Length * 0.5f;
			const int32 LipCount = FMath::Clamp(FMath::RoundToInt(F.Width / CopeStep), 2, 60);
			for (int32 k = 0; k < LipCount; ++k)
			{
				const float V = (static_cast<float>(k) + 0.5f) / LipCount * F.Width - F.Width * 0.5f;
				LayCoping(F.Centre + F.Dir * LipU + Across * V, AcrossYaw);
			}
		}
	}
	UE_LOG(LogSQ, Log, TEXT("Map districts: %d districts, %d lanes, %d skate features, %d park pieces, %d monuments, %d lane lights"),
		Districts.Num(), Lanes.Num(), SkateFeatures.Num(), ParkBuilt, Monuments.Num(), LaneLights);
}
