// KitsuneRockConsoleCommands.cpp
// Быстрая генерация прямо из редактора:
//   Kitsune.Rock.Generate Wall01 lavaVeins 512 1337
//   Kitsune.Rock.GenerateAll
//   Kitsune.Rock.SyncMaster [папка] [nosave]
#include "KitsuneRockConsoleCommands.h"

#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "KitsuneRockAssets.h"
#include "KitsuneRockField.h"
#include "Misc/Parse.h"

namespace
{
	void ConsoleGenerateRock(const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Использование: Kitsune.Rock.Generate <Имя> [granite|basalt|sandstone|mossyCave|wetCave|dungeonBricks|lavaVeins|crystalCave] [размер] [сид]"));
			return;
		}

		FKitsuneRockParams ParamsStruct;

		if (Args.Num() > 1)
		{
			EKitsuneRockPreset Preset = EKitsuneRockPreset::Granite;
			if (FindKitsuneRockPreset(Args[1], Preset))
			{
				ParamsStruct.ApplyPreset(Preset);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Пресет «%s» не найден — беру значения по умолчанию"), *Args[1]);
				ParamsStruct.Sanitize();
			}
		}
		else
		{
			ParamsStruct.Sanitize();
		}

		if (Args.Num() > 2)
		{
			ParamsStruct.Size = FCString::Atoi(*Args[2]);
		}
		if (Args.Num() > 3)
		{
			ParamsStruct.Seed = FCString::Atoi(*Args[3]);
		}

		ParamsStruct.Sanitize();

		const FKitsuneRockMaterialResult Result = UKitsuneRockMaterialLibrary::GenerateRockMaterial(
			Args[0], ParamsStruct, KitsuneRockMaterial::DefaultAssetFolder(), true);

		if (!Result.bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("Ошибка: %s"), *Result.Message);
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("Создан материал %s"), *Result.Message);
	}

	void ConsoleGenerateAll(const TArray<FString>& Args, UWorld*)
	{
		int32 Size = 0;
		if (Args.Num() > 0)
		{
			Size = FCString::Atoi(*Args[0]);
		}

		const TArray<FKitsuneRockMaterialResult> Results =
			UKitsuneRockMaterialLibrary::GenerateAllRockPresets(Size, true);

		int32 Failures = 0;
		for (const FKitsuneRockMaterialResult& Result : Results)
		{
			if (!Result.bSuccess)
			{
				++Failures;
			}
		}

		UE_LOG(LogTemp, Display, TEXT("Пресетов обработано: %d, ошибок: %d"), Results.Num(), Failures);
	}

	void ConsoleSyncMaster(const TArray<FString>& Args, UWorld*)
	{
		FString Folder = KitsuneRockMaterial::DefaultAssetFolder();
		if (Args.Num() > 0 && !Args[0].IsEmpty())
		{
			Folder = Args[0];
		}

		const bool bSaveAssets = !(Args.Num() > 1 && Args[1] == TEXT("nosave"));
		const int32 Fixed = UKitsuneRockMaterialLibrary::SyncRockMasterSamplerTypes(Folder, bSaveAssets);

		if (Fixed < 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Мастер в %s не найден — сначала Kitsune.Rock.GenerateAll"), *Folder);
			return;
		}

		if (Fixed == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Мастер уже корректен: карты по умолчанию на месте, типы сэмплеров совпадают"));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Исправлено свойств узлов: %d%s"), Fixed, bSaveAssets ? TEXT(" (мастер сохранён)") : TEXT(" (без сохранения)"));
		}
	}

	void ConsoleAntiTiling(const TArray<FString>& Args, UWorld*)
	{
		FString Folder = KitsuneRockMaterial::DefaultAssetFolder();
		if (Args.Num() > 0 && !Args[0].IsEmpty())
		{
			Folder = Args[0];
		}

		const bool bSaveAssets = !(Args.Num() > 1 && Args[1] == TEXT("nosave"));
		const int32 Added = UKitsuneRockMaterialLibrary::UpgradeRockMasterAntiTiling(Folder, bSaveAssets);

		if (Added < 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Мастер в %s не найден — сначала Kitsune.Rock.GenerateAll"), *Folder);
			return;
		}

		if (Added == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Анти-тайлинг уже встроен в мастер из %s: UVOffsetX/Y, WarpAmount/WarpScale, DetailScale/DetailMix"), *Folder);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Анти-тайлинг встроен: узлов %d%s"), Added, bSaveAssets ? TEXT(" (мастер сохранён)") : TEXT(" (без сохранения)"));
		}
	}

	void ConsoleCreateMaster(const TArray<FString>&, UWorld*)
	{
		UMaterial* Master = UKitsuneRockMaterialLibrary::GetOrCreateRockMasterMaterial(
			KitsuneRockMaterial::DefaultAssetFolder(), true);

		UE_LOG(LogTemp, Display, TEXT("Master-материал: %s"),
			Master ? *Master->GetPathName() : TEXT("не создан"));
	}
}

void RegisterKitsuneRockConsoleCommands()
{
	static FAutoConsoleCommandWithWorldAndArgs GenerateRockCommand(
		TEXT("Kitsune.Rock.Generate"),
		TEXT("Собрать комплект текстур и материал MI_<Имя>. Аргументы: <Имя> [пресет] [размер] [сид]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConsoleGenerateRock));

	static FAutoConsoleCommandWithWorldAndArgs GenerateAllCommand(
		TEXT("Kitsune.Rock.GenerateAll"),
		TEXT("Собрать все восемь пресетов. Необязательный аргумент: размер карт (256/512/1024)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConsoleGenerateAll));

	static FAutoConsoleCommandWithWorldAndArgs SyncMasterCommand(
		TEXT("Kitsune.Rock.SyncMaster"),
		TEXT("Починить master-материал: карты по умолчанию у шести параметров текстур и типы сэмплеров. Аргументы: [папка] [nosave]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConsoleSyncMaster));

	static FAutoConsoleCommandWithWorldAndArgs AntiTilingCommand(
		TEXT("Kitsune.Rock.AntiTiling"),
		TEXT("Встроить в master-материал анти-тайлинг: UVOffsetX/UVOffsetY, WarpAmount/WarpScale, DetailScale/DetailMix. Аргументы: [папка] [nosave]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConsoleAntiTiling));

	static FAutoConsoleCommandWithWorldAndArgs CreateMasterCommand(
		TEXT("Kitsune.Rock.Master"),
		TEXT("Создать master-материал M_KitsuneRock, если его ещё нет"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConsoleCreateMaster));
}
#endif // WITH_EDITOR
