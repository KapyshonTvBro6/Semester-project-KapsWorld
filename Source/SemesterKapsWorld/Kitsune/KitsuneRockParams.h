// KitsuneRockParams.h
// Параметры генератора скальных текстур — порт настроек веб-страницы
// «🦊 Кицунэ Юки — генератор скальных текстур и нормалей».
// Здесь только данные: сами карты считает KitsuneRockField.cpp,
// ассеты создаёт KitsuneRockAssets.cpp (редакторский код).
#pragma once

#include "CoreMinimal.h"
#include "KitsuneRockParams.generated.h"

/** Встроенные пресеты — те же, что в веб-версии. */
UENUM(BlueprintType)
enum class EKitsuneRockPreset : uint8
{
	Granite       UMETA(DisplayName = "Гранит"),
	Basalt        UMETA(DisplayName = "Базальт"),
	Sandstone     UMETA(DisplayName = "Пещерный песчаник"),
	MossyCave     UMETA(DisplayName = "Мшистая пещера"),
	WetCave       UMETA(DisplayName = "Мокрая пещера"),
	DungeonBricks UMETA(DisplayName = "Кирпичный данж"),
	LavaVeins     UMETA(DisplayName = "Лавовые жилы"),
	CrystalCave   UMETA(DisplayName = "Кристальная пещера")
};

/**
 * Одна настройка генератора. 1-в-1 соответствует слайдерам веб-версии,
 * поэтому значения из страницы можно переносить руками без потерь.
 */
USTRUCT(BlueprintType)
struct FKitsuneRockParams
{
	GENERATED_BODY()

public:
	/** Сторона карты в пикселях: 256 / 512 / 1024 (берётся ближайшая степень 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Карта",
		meta = (DisplayName = "Размер карты", ClampMin = "128", ClampMax = "2048", UIMin = "128", UIMax = "2048"))
	int32 Size = 512;

	/** Seed: при одинаковом сиде и настройках карты получаются те же, что в веб-версии. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Карта",
		meta = (DisplayName = "Seed"))
	int32 Seed = 1337;

	/** Масштаб: сколько «каменных» блоков приходится на одну карту. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Рельеф",
		meta = (DisplayName = "Масштаб", ClampMin = "2", ClampMax = "16", UIMin = "2", UIMax = "16"))
	float Scale = 6.5f;

	/** Октавы: количество деталей (1..8). Больше октав = больше мелкого шума. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Рельеф",
		meta = (DisplayName = "Октавы", ClampMin = "1", ClampMax = "8", UIMin = "1", UIMax = "8"))
	int32 Octaves = 7;

	/** Рельеф: насколько сильным делать перепад высот. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Рельеф",
		meta = (DisplayName = "Рельеф", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Relief = 0.62f;

	/** Трещины: количество и глубина трещин (гребневой шум). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Рельеф",
		meta = (DisplayName = "Трещины", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Cracks = 0.28f;

	/** Слоистость: горизонтальные полосы (осадочные породы). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Рельеф",
		meta = (DisplayName = "Слоистость", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Strata = 0.12f;

	/** Влага: затемняет и делает гладкими низкие и трещиноватые места. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Погода",
		meta = (DisplayName = "Влага", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Moisture = 0.08f;

	/** Мох: озеленает выступы и плоские места. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Погода",
		meta = (DisplayName = "Мох", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Moss = 0.05f;

	/** Базовая шероховатость PBR-карты. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|PBR",
		meta = (DisplayName = "Шероховатость", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Roughness = 0.72f;

	/** Кирпичи/данж: подмешивает кирпичную кладку к камню. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Форма",
		meta = (DisplayName = "Кирпичи/данж", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Brick = 0.0f;

	/** Вены/лава/кристаллы: светящиеся жилы по трещинам. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Форма",
		meta = (DisplayName = "Вены/лава/кристаллы", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Veins = 0.0f;

	/** Сила нормалей: амплитуда тангенциальной нормали (0..8). В UE сила нормали
		уже «запечена» в карту, а в материале есть отдельный параметр NormalStrength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|PBR",
		meta = (DisplayName = "Сила нормалей", ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "8"))
	float NormalStrength = 2.5f;

	/** AO: насколько сильно затенять впадины. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|PBR",
		meta = (DisplayName = "AO", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float AOStrength = 0.55f;

	/** Инвертировать Y нормали (нужно, если рельеф подсвечивается «наоборот»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|PBR",
		meta = (DisplayName = "Инвертировать Y нормали"))
	bool bInvertNormalY = false;

	/** Цветная шкала камня: от тёмных пор к светлым (4 опорных цвета, sRGB как на странице). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Цвет",
		meta = (DisplayName = "Цвет 1 (тёмный)"))
	FLinearColor ColorDark;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Цвет",
		meta = (DisplayName = "Цвет 2"))
	FLinearColor ColorMid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Цвет",
		meta = (DisplayName = "Цвет 3"))
	FLinearColor ColorLight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Цвет",
		meta = (DisplayName = "Цвет 4 (светлый)"))
	FLinearColor ColorBright;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Цвет",
		meta = (DisplayName = "Цвет мха"))
	FLinearColor MossColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kitsune|Цвет",
		meta = (DisplayName = "Цвет вен"))
	FLinearColor VeinColor;

public:
	/** Взять настройки встроенного пресета. */
	void ApplyPreset(EKitsuneRockPreset Preset);

	/** Проверить диапазоны и привести размер к степени 2. */
	void Sanitize();

	/** Размер карты, округлённый до допустимой степени 2 (128..2048). */
	int32 GetValidSize() const;

	/** Цвета шкалы в sRGB-байтах — именно в таком виде их рисует веб-версия. */
	void GetRampSRGB(FColor OutColors[4]) const;
};

/** Настройки встроенного пресета (Granite..CrystalCave). */
SEMESTERKAPSWORLD_API FKitsuneRockParams MakeKitsuneRockPreset(EKitsuneRockPreset Preset);

/** Имя пресета для имён ассетов: Granite, Basalt, Sandstone, ... */
SEMESTERKAPSWORLD_API FString GetKitsuneRockPresetName(EKitsuneRockPreset Preset);

/** Русское имя пресета для интерфейса. */
SEMESTERKAPSWORLD_API FString GetKitsuneRockPresetLabel(EKitsuneRockPreset Preset);

/** Пресет по имени (без регистра), false если не найден. */
SEMESTERKAPSWORLD_API bool FindKitsuneRockPreset(const FString& Name, EKitsuneRockPreset& OutPreset);

/** Все пресеты по порядку. */
SEMESTERKAPSWORLD_API const TArray<EKitsuneRockPreset>& GetKitsuneRockPresets();
