#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "KapsBossAIController.generated.h"

UCLASS()
class SEMESTERKAPSWORLD_API AKapsBossAIController : public AAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	void UpdateAI();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Detection",
		meta = (ClampMin = "100.0", Units = "cm"))
	float DetectionRange = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Detection",
		meta = (ClampMin = "0.0", Units = "s"))
	float LostTargetDelay = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Movement",
		meta = (ClampMin = "0.05", Units = "s"))
	float UpdateInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Movement",
		meta = (ClampMin = "1.0", Units = "cm"))
	float HomeAcceptanceRadius = 80.0f;

private:
	FVector HomeLocation;
	FVector LastKnownPlayerLocation;
	float LastSeenPlayerTime = -1000.0f;
	FTimerHandle AIUpdateTimer;
};
