// KapsMeleeAIController.h
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "KapsMeleeAIController.generated.h"

UCLASS()
class SEMESTERKAPSWORLD_API AKapsMeleeAIController : public AAIController
{
	GENERATED_BODY()

public:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	void UpdateAI();

	UPROPERTY(EditDefaultsOnly, Category = "AI",
		meta = (ClampMin = "100.0", Units = "cm"))
	float DetectionRange = 1000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "AI",
		meta = (ClampMin = "0.05", Units = "s"))
	float UpdateInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "AI",
		meta = (ClampMin = "1.0", Units = "cm"))
	float HomeAcceptanceRadius = 25.0f;

private:
	FVector HomeLocation;
	FTimerHandle AIUpdateTimer;
};