// Soul Quota - dev screenshots. While you play in the editor (or a non-shipping build) this saves a screenshot every few
// seconds to Saved/SQShots/<session>/ so the look of each screen can be reviewed afterwards. Tuning: AutoShotEvery
// (seconds, 0 = off), AutoShotMax (per map load). Never runs in shipping builds.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SQAutoShot.generated.h"

UCLASS()
class SOULQUOTA_API USQAutoShot : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USQAutoShot, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::PIE || WorldType == EWorldType::Game;
	}

private:
	float SinceShot = 0.f;
	float WorldAge = 0.f;
	int32 Taken = 0;
};
