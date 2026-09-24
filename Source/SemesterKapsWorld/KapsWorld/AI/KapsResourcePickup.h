#pragma once

#include "CoreMinimal.h"
#include "GameFramework\Actor.h"
#include "KapsResourcePickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnResourceCollected,
	FName,
	ResourceId,
	int32,
	Amount
);

UCLASS()
class SEMESTERKAPSWORLD_API AKapsResourcePickup : public AActor
{
	GENERATED_BODY()

public:
	AKapsResourcePickup();

	UFUNCTION(BlueprintCallable, Category = "Resource")
	void InitializePickup(FName InResourceId, int32 InAmount);

	UPROPERTY(BlueprintAssignable, Category = "Resource")
	FOnResourceCollected OnResourceCollected;

protected:
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

	UPROPERTY(VisibleAnywhere, Category = "Resource")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Resource")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resource")
	FName ResourceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resource")
	int32 Amount = 1;
};
