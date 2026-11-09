// KitsuneRockGeneratorComponent.h
// Компонент-инструмент: висит на любом акторе, в Details-панели кнопки
// «Взять пресет», «Случайный сид», «Сгенерировать материал», «Назначить на меши».
// Ничего не знает про генератор подземелий: это отдельный инструмент.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KitsuneRockParams.h"
#include "KitsuneRockGeneratorComponent.generated.h"

class UMaterialInstanceConstant;

UCLASS(ClassGroup = (Kitsune), meta = (BlueprintSpawnableComponent, DisplayName = "Генератор материалов камня"))
class SEMESTERKAPSWORLD_API UKitsuneRockGeneratorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKitsuneRockGeneratorComponent();

	/** Пресет из веб-генератора: при смене пресета настройки перезаписываются. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Пресет",
		meta = (DisplayName = "Пресет", ToolTip = "Выбор пресета только меняет настройки ниже. Ассеты создаются кнопкой «Сгенерировать материал»."))
	EKitsuneRockPreset Preset = EKitsuneRockPreset::Granite;

	/** Те же параметры, что у слайдеров веб-генератора. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Настройки",
		meta = (DisplayName = "Настройки", ShowOnlyInnerProperties))
	FKitsuneRockParams Params;

	/** Имя будущих ассетов: MI_<Имя>, T_<Имя>_BaseColor, ... Пусто = имя актора. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Ассеты",
		meta = (DisplayName = "Имя ассета", ToolTip = "Например Granite01. Пусто — возьмётся имя актора."))
	FString AssetName;

	/** Папка Content Browser для созданных ассетов. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Ассеты",
		meta = (DisplayName = "Папка ассетов", ToolTip = "/Game/Kitsune/Rock по умолчанию."))
	FString AssetFolder = TEXT("/Game/Kitsune/Rock");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Ассеты",
		meta = (DisplayName = "Сохранять ассеты на диск"))
	bool bSaveAssets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Ассеты",
		meta = (DisplayName = "Назначить материал на меши владельца"))
	bool bApplyToOwnerMeshes = true;

	/** Результат последней генерации. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kitsune|Результат",
		meta = (DisplayName = "Готовый материал"))
	TObjectPtr<UMaterialInstanceConstant> GeneratedMaterial;

public:
	/** Перенести значения выбранного пресета в Params. */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Генератор",
		meta = (CallInEditor = "true", DisplayName = "Взять значения пресета"))
	void ApplyPreset();

	/** Новый случайный сид (как кнопка 🎲 в веб-версии). */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Генератор",
		meta = (CallInEditor = "true", DisplayName = "Случайный сид"))
	void RandomizeSeed();

	/** Посчитать карты, создать текстуры и MI_<Имя ассета>. */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Генератор",
		meta = (CallInEditor = "true", DisplayName = "Сгенерировать материал"))
	void Generate();

	/** Назначить полученный материал всем StaticMeshComponent актора-владельца. */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Генератор",
		meta = (CallInEditor = "true", DisplayName = "Назначить материал на меши"))
	void ApplyMaterialToOwner();

	/** Имя ассета, которое будет использовано (с учётом имени актора). */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Генератор")
	FString ResolveAssetName() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
