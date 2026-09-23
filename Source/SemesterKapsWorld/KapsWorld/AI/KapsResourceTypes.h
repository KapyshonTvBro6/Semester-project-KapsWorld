#pragma once

#include "CoreMinimal.h"
#include "KapsResourceTypes.generated.h"

class AKapsResourcePickup;

USTRUCT(BlueprintType)
struct FKapsResourceDrop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	FName ResourceId = "Scrap";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot",
		meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "%"))
	float DropChance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot",
		meta = (ClampMin = "1"))
	int32 MinAmount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot",
		meta = (ClampMin = "1"))
	int32 MaxAmount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	TSubclassOf<AKapsResourcePickup> PickupClass;
};
