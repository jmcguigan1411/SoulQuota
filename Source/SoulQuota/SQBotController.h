// Copyright 2026 Jack McGuigan. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SQBotController.generated.h"

/** Simple AI teammate so you can test 4-player co-op alone (console: SQAddBot) */
UCLASS()
class SOULQUOTA_API ASQBotController : public AAIController
{
	GENERATED_BODY()

public:
	ASQBotController();
	virtual void Tick(float DeltaSeconds) override;

private:
	FVector2D WanderTarget = FVector2D::ZeroVector;
	float WanderTimer = 0.f;
	float InteractCooldown = 0.f;
};
