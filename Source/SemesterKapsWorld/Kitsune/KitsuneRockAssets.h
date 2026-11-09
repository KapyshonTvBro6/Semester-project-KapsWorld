// KitsuneRockAssets.h
// Создание ассетов из карт генератора: UTexture2D + master-материал M_KitsuneRock
// + MaterialInstanceConstant на каждую настройку. Код редакторский: в сборке игры
// функции возвращают пустой результат.
//
// К генератору подземелий (ADungeonGenerator) это отношения не имеет:
// готовый материал просто назначается на стены вручную или через Blueprint.
#pragma once

#include "CoreMinimal.h"
#include "KitsuneRockParams.h"
#include "KitsuneRockAssets.generated.h"

class UMaterial;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * Имена параметров master-материала. Список используют и C++, и редактор,
 * и Blueprint — опечатка в имени параметра не потеряет настройку.
 */
namespace KitsuneRockMaterial
{
	// Текстуры
	inline const FName& BaseColorTexture() { static const FName Name(TEXT("T_BaseColor")); return Name; }
	inline const FName& NormalTexture()    { static const FName Name(TEXT("T_Normal")); return Name; }
	inline const FName& RoughnessTexture() { static const FName Name(TEXT("T_Roughness")); return Name; }
	inline const FName& AOTexture()        { static const FName Name(TEXT("T_AO")); return Name; }
	inline const FName& HeightTexture()    { static const FName Name(TEXT("T_Height")); return Name; }
	inline const FName& EmissiveTexture()  { static const FName Name(TEXT("T_Emissive")); return Name; }

	// Скаляры
	inline const FName& Tiling()           { static const FName Name(TEXT("Tiling")); return Name; }
	inline const FName& NormalStrength()   { static const FName Name(TEXT("NormalStrength")); return Name; }
	inline const FName& RoughnessAdjust()  { static const FName Name(TEXT("RoughnessAdjust")); return Name; }
	inline const FName& AOStrength()       { static const FName Name(TEXT("AOStrength")); return Name; }
	inline const FName& HeightStrength()   { static const FName Name(TEXT("HeightStrength")); return Name; }
	inline const FName& Specular()         { static const FName Name(TEXT("Specular")); return Name; }
	inline const FName& EmissiveStrength() { static const FName Name(TEXT("EmissiveStrength")); return Name; }

	// Анти-тайлинг: смещение UV в плитках, волна UV, детальный слой
	inline const FName& UVOffsetX()   { static const FName Name(TEXT("UVOffsetX")); return Name; }
	inline const FName& UVOffsetY()   { static const FName Name(TEXT("UVOffsetY")); return Name; }
	inline const FName& WarpAmount()  { static const FName Name(TEXT("WarpAmount")); return Name; }
	inline const FName& WarpScale()   { static const FName Name(TEXT("WarpScale")); return Name; }
	inline const FName& DetailScale() { static const FName Name(TEXT("DetailScale")); return Name; }
	inline const FName& DetailMix()   { static const FName Name(TEXT("DetailMix")); return Name; }

	// Векторы
	inline const FName& Tint()      { static const FName Name(TEXT("Tint")); return Name; }
	inline const FName& VeinColor() { static const FName Name(TEXT("VeinColor")); return Name; }

	/** Папка, где по умолчанию лежат сгенерированные ассеты. */
	SEMESTERKAPSWORLD_API const FString& DefaultAssetFolder();

	/** Имя master-материала без пути: M_KitsuneRock. */
	SEMESTERKAPSWORLD_API const FString& MasterMaterialName();

	/** Полный путь master-материала: <папка>/M_KitsuneRock. */
	SEMESTERKAPSWORLD_API FString MasterMaterialPath(const FString& AssetFolder);
}

/** Результат генерации комплекта: набор текстур и готовый материал. */
USTRUCT(BlueprintType)
struct FKitsuneRockMaterialResult
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Kitsune")
	bool bSuccess = false;

	/** Короткое сообщение для интерфейса и лога. */
	UPROPERTY(BlueprintReadOnly, Category = "Kitsune")
	FString Message;

	/** Итоговый материал, который можно назначать на меши. */
	UPROPERTY(BlueprintReadOnly, Category = "Kitsune")
	TObjectPtr<UMaterialInstanceConstant> MaterialInstance = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Kitsune")
	TArray<TObjectPtr<UTexture2D>> Textures;
};

/**
 * Библиотека для Blueprint и редактора: собрать карты генератора в текстуры
 * и материал одним вызовом.
 */
UCLASS()
class SEMESTERKAPSWORLD_API UKitsuneRockMaterialLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Создать (или вернуть существующий) master-материал M_KitsuneRock. */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Материалы",
		meta = (DisplayName = "Создать master-материал камня", DevelopmentOnly))
	static UMaterial* GetOrCreateRockMasterMaterial(const FString& AssetFolder, bool bSaveAssets);

	/**
	 * Сгенерировать карты по настройкам, сохранить шесть текстур и создать
	 * MaterialInstanceConstant MI_<AssetName> с parent = M_KitsuneRock.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Материалы",
		meta = (DisplayName = "Сгенерировать материал камня", DevelopmentOnly, Keywords = "rock stone material texture normal generate"))
	static FKitsuneRockMaterialResult GenerateRockMaterial(
		const FString& AssetName,
		const FKitsuneRockParams& Params,
		const FString& AssetFolder,
		bool bSaveAssets);

	/** Прогнать все восемь встроенных пресетов (Size = 0 оставить пресетный размер). */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Материалы",
		meta = (DisplayName = "Сгенерировать все пресеты камня", DevelopmentOnly))
	static TArray<FKitsuneRockMaterialResult> GenerateAllRockPresets(int32 Size, bool bSaveAssets);

	/**
	 * Проверить master-материал: узлам параметров текстур нужны свои карты по умолчанию
	 * и тип сэмплера, который движок вычисляет по компрессии и SRGB этих карт.
	 * Без этого M_KitsuneRock не компилируется (красный ERROR! на узлах Param2D).
	 * Возвращает число исправленных свойств узлов.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Материалы",
		meta = (DisplayName = "Починить текстуры по умолчанию в мастере камня", DevelopmentOnly))
	static int32 SyncRockMasterSamplerTypes(const FString& AssetFolder, bool bSaveAssets);

	/**
	 * Добавить в master-материал узлы анти-тайлинга: UVOffsetX/UVOffsetY, WarpAmount,
	 * WarpScale, DetailScale, DetailMix. Идемпотентно: если узлы уже есть, повторный
	 * вызов ничего не меняет. Существующие MaterialInstanceConstant получают новые
	 * параметры как значения по умолчанию из мастера; их собственные правки не трогаются.
	 * Возвращает число добавленных узлов (0 = уже сделано).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Материалы",
		meta = (DisplayName = "Добавить анти-тайлинг в мастер камня", DevelopmentOnly))
	static int32 UpgradeRockMasterAntiTiling(const FString& AssetFolder, bool bSaveAssets);

	/**
	 * Случайные значения анти-тайлинга для динамического инстанса (MID): разная
	 * раскладка текстуры на разных стенах и комнатах. Работает в игре и в редакторе.
	 * Параметров каскада в инстансе может не быть — тогда они создадутся сами.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kitsune|Материалы",
		meta = (DisplayName = "Случайный тайлинг камня для инстанса", Keywords = "rock tiling random instance"))
	static void RandomizeRockInstanceParameters(UMaterialInstanceDynamic* Instance, int32 Seed);
};
