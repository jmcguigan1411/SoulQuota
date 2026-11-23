// Soul Quota - dev screenshots (see SQAutoShot.h)
#include "SQAutoShot.h"
#include "SQData.h"
#include "SQGameState.h"
#include "SQTypes.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "UnrealClient.h"

void USQAutoShot::Tick(float DeltaTime)
{
#if UE_BUILD_SHIPPING
	return;
#else
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || World->GetNetMode() == NM_Client) return;
	const USQData* D = USQData::Get(this);
	const float Every = D ? D->Tune(TEXT("AutoShotEvery"), 5.f) : 5.f;
	const int32 MaxShots = D ? FMath::RoundToInt(D->Tune(TEXT("AutoShotMax"), 60.f)) : 60;
	if (Every <= 0.f || Taken >= MaxShots) return;
	const float RealDelta = static_cast<float>(FApp::GetDeltaTime());
	WorldAge += RealDelta;
	SinceShot += RealDelta;
	// First shot 2 s after the map opens (so menus are caught even on a short visit), then every AutoShotEvery seconds
	const float Wait = Taken == 0 ? 2.f : Every;
	if (SinceShot < Wait || FScreenshotRequest::IsScreenshotRequested()) return;
	SinceShot = 0.f;

	static const FString Session = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	static int32 Serial = 0;
	FString PhaseName = TEXT("Menu");
	if (const ASQGameState* GS = World->GetGameState<ASQGameState>())
	{
		switch (GS->Phase)
		{
		case ESQPhase::Waiting: PhaseName = TEXT("Lobby"); break;
		case ESQPhase::Night: PhaseName = TEXT("Night"); break;
		case ESQPhase::Boss: PhaseName = TEXT("Boss"); break;
		case ESQPhase::Shop: PhaseName = TEXT("Shop"); break;
		case ESQPhase::Victory: PhaseName = TEXT("Victory"); break;
		case ESQPhase::Defeat: PhaseName = TEXT("Defeat"); break;
		default: break;
		}
	}
	FString MapName = World->GetMapName();
	// PIE worlds are named UEDPIE_<n>_<Map>
	if (MapName.StartsWith(TEXT("UEDPIE_")))
	{
		const int32 Cut = MapName.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		if (Cut != INDEX_NONE) MapName = MapName.Mid(Cut + 1);
	}
	const FString File = FPaths::ProjectSavedDir() / TEXT("SQShots") / Session
		/ FString::Printf(TEXT("%03d_%s_%s_%03ds.png"), Serial++, *MapName, *PhaseName, FMath::RoundToInt(WorldAge));
	FScreenshotRequest::RequestScreenshot(File, true, false);
	++Taken;
#endif
}
