#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "KapsDashAIController.generated.h"

UCLASS()
class SEMESTERKAPSWORLD_API AKapsDashAIController : public AAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	void UpdateAI();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Detection",
		meta = (ClampMin = "100.0", Units = "cm"))
	float DetectionRange = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Detection",
		meta = (ClampMin = "0.0", Units = "s"))
	float LostTargetDelay = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Movement",
		meta = (ClampMin = "0.05", Units = "s"))
	float UpdateInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Movement",
		meta = (ClampMin = "1.0", Units = "cm"))
	float HomeAcceptanceRadius = 50.0f;

private:
	FVector HomeLocation;
	FVector LastKnownPlayerLocation;
	float LastSeenPlayerTime = -1000.0f;

	FTimerHandle AIUpdateTimer;
};