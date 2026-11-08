// KitsuneRockGeneratorComponent.cpp
#include "KitsuneRockGeneratorComponent.h"

#include "KitsuneRockAssets.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogKitsuneRockGen, Log, All);

UKitsuneRockGeneratorComponent::UKitsuneRockGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UKitsuneRockGeneratorComponent::ApplyPreset()
{
	Params.ApplyPreset(Preset);

	UE_LOG(LogKitsuneRockGen, Log, TEXT("Настройки взяты из пресета «%s» (seed %d)"),
		*GetKitsuneRockPresetLabel(Preset), Params.Seed);
}

void UKitsuneRockGeneratorComponent::RandomizeSeed()
{
	Params.Seed = FMath::RandRange(0, 999999);

	UE_LOG(LogKitsuneRockGen, Log, TEXT("Новый сид: %d"), Params.Seed);
}

FString UKitsuneRockGeneratorComponent::ResolveAssetName() const
{
	if (!AssetName.IsEmpty())
	{
		return AssetName;
	}

	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetName() : FString(TEXT("KitsuneRock"));
}

void UKitsuneRockGeneratorComponent::Generate()
{
#if WITH_EDITOR
	Params.Sanitize();

	FKitsuneRockMaterialResult Result = UKitsuneRockMaterialLibrary::GenerateRockMaterial(
		ResolveAssetName(), Params, AssetFolder, bSaveAssets);

	if (!Result.bSuccess || !Result.MaterialInstance)
	{
		UE_LOG(LogKitsuneRockGen, Error, TEXT("Генерация не удалась: %s"), *Result.Message);
		return;
	}

	GeneratedMaterial = Result.MaterialInstance;

	if (bApplyToOwnerMeshes)
	{
		ApplyMaterialToOwner();
	}

	UE_LOG(LogKitsuneRockGen, Log, TEXT("Материал готов: %s"), *Result.MaterialInstance->GetPathName());
#else
	UE_LOG(LogKitsuneRockGen, Error, TEXT("Генерация материалов работает только в редакторе"));
#endif
}

void UKitsuneRockGeneratorComponent::ApplyMaterialToOwner()
{
	if (!GeneratedMaterial)
	{
		UE_LOG(LogKitsuneRockGen, Warning, TEXT("Нет сгенерированного материала: сначала «Сгенерировать материал»"));
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Стены данжа рисуются UProceduralMeshComponent, обычные меши — UStaticMeshComponent:
	// UMeshComponent покрывает и те, и другие.
	TArray<UMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);

	int32 Applied = 0;
	for (UMeshComponent* Mesh : MeshComponents)
	{
		if (!Mesh)
		{
			continue;
		}

		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			Mesh->SetMaterial(Slot, GeneratedMaterial);
			++Applied;
		}
	}

	UE_LOG(LogKitsuneRockGen, Log, TEXT("Материал назначен в слотов: %d (актор %s)"), Applied, *Owner->GetName());
}

#if WITH_EDITOR
void UKitsuneRockGeneratorComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Смена пресета в панели делает то же, что выбор «Материал» в веб-версии:
	// меняются только настройки, ассеты при этом не создаются.
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UKitsuneRockGeneratorComponent, Preset))
	{
		Params.ApplyPreset(Preset);
	}
}
#endif