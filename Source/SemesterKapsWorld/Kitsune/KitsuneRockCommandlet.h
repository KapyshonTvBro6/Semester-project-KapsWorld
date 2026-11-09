// KitsuneRockCommandlet.h
// Пакетная генерация без открытия редактора:
//   UnrealEditor-Cmd <uproject> -run=KitsuneRock -all
//   UnrealEditor-Cmd <uproject> -run=KitsuneRock -name=Wall01 -preset=lavaVeins -size=1024 -seed=42
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "KitsuneRockCommandlet.generated.h"

UCLASS()
class SEMESTERKAPSWORLD_API UKitsuneRockCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UKitsuneRockCommandlet();

	virtual int32 Main(const FString& Params) override;
};
