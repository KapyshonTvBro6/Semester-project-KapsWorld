// KitsuneRockField.h
// Каркас генератора: результат работы — набор PBR-карт того же размера,
// что отдаёт веб-версия (BaseColor / Normal / Roughness / AO / Height),
// плюс маска светящихся вен для Emissive.
#pragma once

#include "CoreMinimal.h"
#include "KitsuneRockParams.h"

/** Пять карт веб-генератора + маска вен (нужна для свечения лавы/кристаллов). */
struct FKitsuneRockMaps
{
	/** Сторона карты в пикселях (степень 2). */
	int32 Size = 0;

	/** Все пиксели row-major: строка 0 — нижняя строка карты (как в UE-текстуре). */
	TArray<FColor> BaseColor;
	TArray<FColor> Normal;        // тангенциальная нормаль, R/G/B = nx/ny/nz
	TArray<FColor> Roughness;     // grayscale
	TArray<FColor> AO;            // grayscale
	TArray<FColor> Height;        // grayscale
	TArray<FColor> EmissiveMask;  // grayscale: 0..1 по весу вен
};

/**
 * Считать карты по настройкам. Порт 1-в-1 алгоритма из <script> веб-страницы:
 * тот же hash2 на целых 32 битах, та же value-noise/fbm с периодическим
 * оборотом (карты стыкуются без шва), те же коэффициенты.
 */
SEMESTERKAPSWORLD_API void GenerateKitsuneRockMaps(const FKitsuneRockParams& Params, FKitsuneRockMaps& OutMaps);
