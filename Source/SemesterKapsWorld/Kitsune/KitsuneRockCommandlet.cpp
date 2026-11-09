// KitsuneRockCommandlet.cpp
// Собирает комплекты материалов пакетом. Нужен редактор (ассеты — редакторские),
// поэтому запускается только через UnrealEditor-Cmd.
#include "KitsuneRockCommandlet.h"

#include "KitsuneRockAssets.h"
#include "KitsuneRockField.h"
#include "KitsuneRockParams.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionBumpOffset.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "UObject/Class.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

namespace
{
	/** Карта в простом PPM (P6): читают GIMP/ImageMagick/Python — удобно для сверки с веб-версией. */
	bool SaveMapAsPPM(const FString& Path, const TArray<FColor>& Data, int32 Size)
	{
		const FString Header = FString::Printf(TEXT("P6\n%d %d\n255\n"), Size, Size);
		const FTCHARToUTF8 HeaderUTF8(*Header);

		TArray<uint8> Bytes;
		Bytes.Reserve(HeaderUTF8.Length() + Size * Size * 3);
		Bytes.Append(reinterpret_cast<const uint8*>(HeaderUTF8.Get()), HeaderUTF8.Length());

		for (const FColor& Color : Data)
		{
			Bytes.Add(Color.R);
			Bytes.Add(Color.G);
			Bytes.Add(Color.B);
		}

		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}

	void DumpKitsuneMaps(const FString& Dir, const FString& BaseName, const FKitsuneRockMaps& Maps)
	{
		IFileManager::Get().MakeDirectory(*Dir, true);

		static const TCHAR* Suffixes[] = {
			TEXT("BaseColor"), TEXT("Normal"), TEXT("Roughness"),
			TEXT("AO"), TEXT("Height"), TEXT("Emissive")
		};
		static const TArray<FColor>* Sources[] = {
			&Maps.BaseColor, &Maps.Normal, &Maps.Roughness,
			&Maps.AO, &Maps.Height, &Maps.EmissiveMask
		};

		int32 Written = 0;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const FString Path = Dir / (BaseName + TEXT("_") + Suffixes[Index] + TEXT(".ppm"));
			if (SaveMapAsPPM(Path, *Sources[Index], Maps.Size))
			{
				++Written;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Не удалось записать карту %s"), *Path);
			}
		}

		UE_LOG(LogTemp, Display, TEXT("Карты для сверки (%s): %d файлов %d x %d в %s"),
			*BaseName, Written, Maps.Size, Maps.Size, *Dir);
	}
}

#if WITH_EDITOR
	/**
	 * Проверка без рендеринга: граф мастер-материала собран целиком, каждый
	 * MI_* ссылается на мастер, и у пресета на диске лежат все шесть карт одного
	 * размера. Commandlet шейдеры материалов не компилирует — их собирает редактор
	 * или готовка проекта, поэтому здесь проверяется структура, а не HLSL.
	 */
	int32 VerifyKitsuneRockAssets(const FString& Folder, int32 ExpectedSize)
	{
		UMaterial* Master = UKitsuneRockMaterialLibrary::GetOrCreateRockMasterMaterial(Folder, false);

		if (!Master)
		{
			UE_LOG(LogTemp, Error, TEXT("Проверка: мастер-материал не найден в %s"), *Folder);
			return 1;
		}

		static const TCHAR* TextureParams[] = {
			TEXT("T_BaseColor"), TEXT("T_Normal"), TEXT("T_Roughness"),
			TEXT("T_AO"), TEXT("T_Height"), TEXT("T_Emissive")
		};
		static const TCHAR* ScalarParams[] = {
			TEXT("Tiling"), TEXT("NormalStrength"), TEXT("RoughnessAdjust"),
			TEXT("AOStrength"), TEXT("HeightStrength"), TEXT("Specular"), TEXT("EmissiveStrength"),
			TEXT("UVOffsetX"), TEXT("UVOffsetY"), TEXT("WarpAmount"),
			TEXT("WarpScale"), TEXT("DetailScale"), TEXT("DetailMix")
		};
		static const int32 ExpectedScalars = 13;
		static const TCHAR* VectorParams[] = { TEXT("Tint"), TEXT("VeinColor") };
		static const TCHAR* MapSuffixes[] = {
			TEXT("BaseColor"), TEXT("Normal"), TEXT("Roughness"),
			TEXT("AO"), TEXT("Height"), TEXT("Emissive")
		};

		TSet<FName> FoundTextures;
		TSet<FName> FoundScalars;
		TSet<FName> FoundVectors;
		int32 CustomNodes = 0;
		int32 BumpOffsets = 0;
		int32 NoiseNodes = 0;
		int32 WorldPositionNodes = 0;
		bool bNormalCustomOk = false;
		TSet<FName> FoundCustomInputs;
		int32 SamplerProblems = 0;

		// Папку берём из имени пакета мастера: -folder= может быть пустым, а ассеты
		// лежат рядом с мастером (/Game/Kitsune/Rock по умолчанию).
		FString PackageName = Master->GetPackage()->GetName();
		int32 LastSlash = INDEX_NONE;
		if (PackageName.FindLastChar(TEXT('/'), LastSlash))
		{
			PackageName.LeftInline(LastSlash);
		}
		const FString AssetFolder = PackageName;

		for (UMaterialExpression* Expr : Master->GetExpressions())
		{
			if (UMaterialExpressionTextureSampleParameter2D* Tex = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
			{
				FoundTextures.Add(Tex->ParameterName);

				// Движок берёт правильный тип сэмплера из компрессии и SRGB карты
				// (MaterialExpressionUtils.cpp:14) и сверяет с SamplerType узла
				// (MaterialExpressionUtils.cpp:51). Несовпадение = «Sampler type is ..., should
				// be ...» и master не компилируется целиком.
				UTexture* DefaultTexture = Tex->Texture.Get();
				if (!DefaultTexture)
				{
					UE_LOG(LogTemp, Error, TEXT("«%s»: у параметра нет карты по умолчанию — мастер не скомпилируется (берётся DefaultTexture типа Color)"), *Tex->ParameterName.ToString());
					++SamplerProblems;
				}
				else
				{
					const EMaterialSamplerType CorrectSampler = MaterialExpressionUtils::GetSamplerTypeForTexture(DefaultTexture);
					if ((EMaterialSamplerType)Tex->SamplerType != CorrectSampler)
					{
						UEnum* SamplerEnum = StaticEnum<EMaterialSamplerType>();
						UE_LOG(LogTemp, Error, TEXT("«%s»: тип сэмплера %s, а карта %s требует %s"),
							*Tex->ParameterName.ToString(),
							*SamplerEnum->GetDisplayNameTextByValue(Tex->SamplerType.GetValue()).ToString(),
							*DefaultTexture->GetName(),
							*SamplerEnum->GetDisplayNameTextByValue((int64)CorrectSampler).ToString());
						++SamplerProblems;
					}
				}
			}
			else if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				FoundScalars.Add(Scalar->ParameterName);
			}
			else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expr))
			{
				FoundVectors.Add(Vector->ParameterName);
			}
			else if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
			{
				++CustomNodes;

				FString InputNames;
				for (const FCustomInput& Input : Custom->Inputs)
				{
					FoundCustomInputs.Add(Input.InputName);
					InputNames += (InputNames.IsEmpty() ? TEXT("") : TEXT(", ")) + Input.InputName.ToString();
				}

				// Конструктор UMaterialExpressionCustom всегда создаёт один безымянный слот
				// входа (MaterialExpressions.cpp:12747). В старом ассете он мог остаться
				// пустым: код его не читает, на результат это не влияет.
				if (FoundCustomInputs.Contains(FName(NAME_None)))
				{
					UE_LOG(LogTemp, Display, TEXT("Custom «%s»: есть безымянный слот входа — его добавляет движок, код его не использует"), *Custom->GetName());
				}

				UE_LOG(LogTemp, Display, TEXT("Custom «%s»: выход %d, код %d символов, входы (%s)"),
					*Custom->GetName(), (int32)Custom->OutputType.GetValue(), Custom->Code.Len(), *InputNames);

				if (Custom->OutputType == ECustomMaterialOutputType::CMOT_Float3
					&& !Custom->Code.IsEmpty()
					&& FoundCustomInputs.Contains(FName(TEXT("NormalIn")))
					&& FoundCustomInputs.Contains(FName(TEXT("Strength"))))
				{
					bNormalCustomOk = true;
				}
			}
			else if (Cast<UMaterialExpressionBumpOffset>(Expr))
			{
				++BumpOffsets;
			}
			else if (Cast<UMaterialExpressionNoise>(Expr))
			{
				++NoiseNodes;
			}
			else if (Cast<UMaterialExpressionWorldPosition>(Expr))
			{
				++WorldPositionNodes;
			}
		}

		int32 Problems = SamplerProblems;

		for (const TCHAR* ParamName : TextureParams)
		{
			if (!FoundTextures.Contains(FName(ParamName)))
			{
				UE_LOG(LogTemp, Error, TEXT("В графе %s нет параметра текстуры %s"), *Master->GetName(), ParamName);
				++Problems;
			}
		}
		for (const TCHAR* ParamName : ScalarParams)
		{
			if (!FoundScalars.Contains(FName(ParamName)))
			{
				UE_LOG(LogTemp, Error, TEXT("В графе %s нет скалярного параметра %s"), *Master->GetName(), ParamName);
				++Problems;
			}
		}
		for (const TCHAR* ParamName : VectorParams)
		{
			if (!FoundVectors.Contains(FName(ParamName)))
			{
				UE_LOG(LogTemp, Error, TEXT("В графе %s нет векторного параметра %s"), *Master->GetName(), ParamName);
				++Problems;
			}
		}

		if (CustomNodes != 1 || BumpOffsets != 1 || !bNormalCustomOk)
		{
			UE_LOG(LogTemp, Error, TEXT("В графе %s Custom-узлов %d (нужен 1 с выходом float3 и входами NormalIn, DetailIn, Mix, Strength), BumpOffset %d (нужен 1) — граф правили вручную?"),
				*Master->GetName(), CustomNodes, BumpOffsets);
			++Problems;
		}

		if (NoiseNodes < 2 || WorldPositionNodes < 1)
		{
			UE_LOG(LogTemp, Error, TEXT("В графе %s нет каскада анти-тайлинга: узлов Noise %d (нужно 2), WorldPosition %d (нужен 1) — выполни Kitsune.Rock.AntiTiling или -antitiling"),
				*Master->GetName(), NoiseNodes, WorldPositionNodes);
			++Problems;
		}

		UE_LOG(LogTemp, Display, TEXT("Граф %s: параметров — текстур %d/6, скаляров %d/%d, векторов %d/2, Custom %d/1, BumpOffset %d/1, Noise %d/2, WorldPosition %d/1"),
			*Master->GetName(), FoundTextures.Num(), FoundScalars.Num(), ExpectedScalars, FoundVectors.Num(), CustomNodes, BumpOffsets, NoiseNodes, WorldPositionNodes);

		int32 Instances = 0;

		for (EKitsuneRockPreset Preset : GetKitsuneRockPresets())
		{
			const FString Key = GetKitsuneRockPresetName(Preset);

			UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *(AssetFolder / (TEXT("MI_") + Key)));
			if (!Instance)
			{
				UE_LOG(LogTemp, Error, TEXT("Не найден инстанс %s/MI_%s"), *AssetFolder, *Key);
				++Problems;
				continue;
			}
			++Instances;

			UMaterialInterface* Parent = Instance->Parent.Get();
			if (Parent != static_cast<UMaterialInterface*>(Master))
			{
				UE_LOG(LogTemp, Error, TEXT("MI_%s ссылается не на мастер: %s"),
					*Key, Parent ? *Parent->GetName() : TEXT("родителя нет"));
				++Problems;
			}

			int32 MapSize = 0;
			int32 MissingMaps = 0;

			for (const TCHAR* Suffix : MapSuffixes)
			{
				UTexture2D* Map = LoadObject<UTexture2D>(nullptr, *(AssetFolder / (TEXT("T_") + Key + TEXT("_") + Suffix)));
				if (!Map)
				{
					UE_LOG(LogTemp, Error, TEXT("Нет карты %s/T_%s_%s"), *AssetFolder, *Key, Suffix);
					++MissingMaps;
					continue;
				}

				// Источник (Source) — исходный размер карты: он есть и у только что
				// созданной текстуры, и у загруженной с диска. GetSizeX() заполняется
				// только после сборки ресурса, в commandlet он нулевой.
				const int32 MapSourceSizeX = (int32)Map->Source.GetSizeX();
				const int32 MapSourceSizeY = (int32)Map->Source.GetSizeY();

				if (MapSize == 0)
				{
					MapSize = MapSourceSizeX;
				}
				else if (MapSize != MapSourceSizeX || MapSize != MapSourceSizeY)
				{
					UE_LOG(LogTemp, Error, TEXT("MI_%s: карта %s имеет размер %d x %d, остальные %d x %d"),
						*Key, Suffix, MapSourceSizeX, MapSourceSizeY, MapSize, MapSize);
					++Problems;
				}
			}

			Problems += MissingMaps;

			if (ExpectedSize >= 128 && MapSize > 0 && ExpectedSize != MapSize)
			{
				UE_LOG(LogTemp, Warning, TEXT("MI_%s: карты %d x %d, а в командной строке -size=%d"), *Key, MapSize, MapSize, ExpectedSize);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("MI_%s: карты %d x %d, родитель — мастер"), *Key, MapSize, MapSize);
			}
		}

		if (Problems > 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Проверка не пройдена: замечаний %d (инстансов найдено %d из %d)"),
				Problems, Instances, GetKitsuneRockPresets().Num());
			return 1;
		}

		UE_LOG(LogTemp, Display, TEXT("Проверка пройдена: граф мастера цел, типы сэмплеров шести параметров совпадают с их картами, инстансов %d, у всех шесть карт одного размера"), Instances);
		return 0;
	}

	/**
	 * Печатает то, что реально лежит в мастере: значения по умолчанию скаляров и
	 * настройки узлов волны. Нужен, чтобы не гадать, какие значения сейчас в ассете
	 * (их правят и генератор, и Material Editor).
	 */
	int32 DumpMasterParameters(const FString& AssetFolder)
	{
		UMaterial* Master = LoadObject<UMaterial>(nullptr, *(AssetFolder / (TEXT("M_KitsuneRock"))));
		if (!Master)
		{
			UE_LOG(LogTemp, Error, TEXT("Мастер M_KitsuneRock не найден в %s"), *AssetFolder);
			return 1;
		}

		UE_LOG(LogTemp, Display, TEXT("=== %s: значения по умолчанию ==="), *Master->GetName());

		for (UMaterialExpression* Expr : Master->GetExpressions())
		{
			if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				UE_LOG(LogTemp, Display, TEXT("  скаляр %-17s %g"), *Scalar->ParameterName.ToString(), Scalar->DefaultValue);
			}
			else if (UMaterialExpressionNoise* Noise = Cast<UMaterialExpressionNoise>(Expr))
			{
				const TCHAR* TypeName = TEXT("?");
				switch (Noise->NoiseFunction)
				{
				case NOISEFUNCTION_SimplexTex:    TypeName = TEXT("SimplexTex");    break;
				case NOISEFUNCTION_GradientTex:   TypeName = TEXT("GradientTex");   break;
				case NOISEFUNCTION_GradientTex3D: TypeName = TEXT("GradientTex3D"); break;
				case NOISEFUNCTION_GradientALU:   TypeName = TEXT("GradientALU");   break;
				case NOISEFUNCTION_ValueALU:      TypeName = TEXT("ValueALU");      break;
				case NOISEFUNCTION_VoronoiALU:    TypeName = TEXT("VoronoiALU");    break;
				}
				UE_LOG(LogTemp, Display, TEXT("  шум    Scale=%g Levels=%d LevelScale=%g Turbulence=%d Tiling=%d Output [%g..%g] тип %s"),
					Noise->Scale, Noise->Levels, Noise->LevelScale, (int32)Noise->bTurbulence, (int32)Noise->bTiling,
					Noise->OutputMin, Noise->OutputMax, TypeName);
			}
		}

		int32 Instances = 0;
		for (EKitsuneRockPreset Preset : GetKitsuneRockPresets())
		{
			const FString Key = GetKitsuneRockPresetName(Preset);
			UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *(AssetFolder / (TEXT("MI_") + Key)));
			if (!Instance || Instance->Parent.Get() != static_cast<UMaterialInterface*>(Master))
			{
				continue;
			}
			++Instances;
			UE_LOG(LogTemp, Display, TEXT("  инстанс MI_%s%s"), *Key,
				(Key.Find(TEXT("brick"), ESearchCase::IgnoreCase) != INDEX_NONE) ? TEXT(" (кирпич: значения не менялись)") : TEXT(""));
		}
		UE_LOG(LogTemp, Display, TEXT("=== инстансов под мастером: %d"), Instances);
		return 0;
	}
#endif // WITH_EDITOR

UKitsuneRockCommandlet::UKitsuneRockCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UKitsuneRockCommandlet::Main(const FString& InParams)
{
#if !WITH_EDITOR
	UE_LOG(LogTemp, Error, TEXT("Commandlet KitsuneRock требует редакторской сборки"));
	return 1;
#else

	UE_LOG(LogTemp, Display, TEXT("Генератор материалов камня (KitsuneRock)"));
	UE_LOG(LogTemp, Display, TEXT("  -all                       все восемь пресетов (по умолчанию)"));
	UE_LOG(LogTemp, Display, TEXT("  -name=<Имя>              собрать один комплект MI_<Имя>"));
	UE_LOG(LogTemp, Display, TEXT("  -preset=<granite|...>    базовый пресет для -name"));
	UE_LOG(LogTemp, Display, TEXT("  -size=<256|512|1024>     размер карт"));
	UE_LOG(LogTemp, Display, TEXT("  -seed=<int>              сид"));
	UE_LOG(LogTemp, Display, TEXT("  -folder=<путь>           папка ассетов, по умолчанию /Game/Kitsune/Rock"));
	UE_LOG(LogTemp, Display, TEXT("  -nosave                  не сохранять .uasset на диск"));
	UE_LOG(LogTemp, Display, TEXT("  -dump=<папка>            дописать карты в PPM для сверки с веб-версией"));
	UE_LOG(LogTemp, Display, TEXT("  -scale/-octaves/-relief/-cracks/-strata/-moisture/-moss/-roughness/-brick/-veins"));
	UE_LOG(LogTemp, Display, TEXT("  -normalstrength/-ao      переопределить отдельные настройки"));
	UE_LOG(LogTemp, Display, TEXT("  -inverty=1               инвертировать Y нормали"));
	UE_LOG(LogTemp, Display, TEXT("  -verify                  проверить граф мастера, типы сэмплеров, инстансы MI_* и шесть карт у каждого пресета"));
	UE_LOG(LogTemp, Display, TEXT("  -syncmaster              только починить мастер: карты по умолчанию и типы сэмплеров, ничего не пересобираем"));
	UE_LOG(LogTemp, Display, TEXT("  -antitiling              только встроить в мастер анти-тайлинг (UVOffset, Warp, Detail); карты не трогаем, инстансы донастраиваются"));

	const FString Params = InParams;

	FString Name;
	if (!FParse::Value(*Params, TEXT("-name="), Name))
	{
		Name.Reset();
	}

	FString PresetName;
	FParse::Value(*Params, TEXT("-preset="), PresetName);

	FString Folder;
	FParse::Value(*Params, TEXT("-folder="), Folder);

	FString DumpDir;
	FParse::Value(*Params, TEXT("-dump="), DumpDir);

	const bool bSaveAssets = !FParse::Param(*Params, TEXT("nosave"));
	const bool bVerify = FParse::Param(*Params, TEXT("verify"));
	const bool bSyncMasterOnly = FParse::Param(*Params, TEXT("syncmaster"));
	const bool bAntiTilingOnly = FParse::Param(*Params, TEXT("antitiling"));
	const bool bParamsOnly = FParse::Param(*Params, TEXT("params"));

	int32 Size = 0;
	FParse::Value(*Params, TEXT("-size="), Size);

	int32 Seed = 0;
	const bool bSeedGiven = FParse::Value(*Params, TEXT("-seed="), Seed);

	auto ReadFloat = [&](const TCHAR* Key, float& Target)
	{
		FString Value;
		if (FParse::Value(*Params, Key, Value))
		{
			Target = (float)FCString::Atof(*Value);
			return true;
		}
		return false;
	};

	int32 Failures = 0;

#if WITH_EDITOR
	if (bParamsOnly)
	{
		// Только печать того, что лежит в мастере: значения по умолчанию и настройки
		// узлов волны. Ничего не меняется и не сохраняется.
		const FString DumpFolder = Folder.IsEmpty() ? KitsuneRockMaterial::DefaultAssetFolder() : Folder;
		return DumpMasterParameters(DumpFolder);
	}
#else
	if (bParamsOnly)
	{
		// DumpMasterParameters описан внутри #if WITH_EDITOR: без этой проверки
		// игровая (WITH_EDITOR=0) сборка модуля не компилировалась бы.
		UE_LOG(LogTemp, Error, TEXT("-params работает только в редакторной сборке"));
		return 1;
	}
#endif

	if (bAntiTilingOnly)
	{
		// Только каскад анти-тайлинга в мастере: UVOffsetX/Y, WarpAmount/WarpScale,
		// DetailScale/DetailMix. Карты не трогаем, инстансы донастраиваются.
		const int32 Added = UKitsuneRockMaterialLibrary::UpgradeRockMasterAntiTiling(Folder, bSaveAssets);

		if (Added < 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Мастер не найден — сначала соберите пресеты (-all)"));
			return 1;
		}

		if (Added == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Каскад анти-тайлинга уже встроен — ничего не изменено"));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Каскад анти-тайлинга встроен: узлов %d%s"), Added, bSaveAssets ? TEXT("") : TEXT(", без сохранения"));
		}

		return 0;
	}

	if (bSyncMasterOnly)
	{
		// Починка только мастера: карты по умолчанию у шести параметров и типы
		// сэмплеров. Карты и инстансы не трогаем.
		const int32 Fixed = UKitsuneRockMaterialLibrary::SyncRockMasterSamplerTypes(Folder, bSaveAssets);

		if (Fixed < 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Мастер не найден — сначала соберите пресеты (-all)"));
			return 1;
		}

		if (Fixed == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Мастер уже корректен: карты по умолчанию на месте, типы сэмплеров совпадают"));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Исправлено свойств узлов: %d%s"), Fixed, bSaveAssets ? TEXT(" (мастер сохранён)") : TEXT(" (без сохранения)"));
		}

		return 0;
	}

	if (Name.IsEmpty())
	{
		// Без -name собираем все восемь пресетов.
		for (EKitsuneRockPreset Preset : GetKitsuneRockPresets())
		{
			FKitsuneRockParams PresetParams = MakeKitsuneRockPreset(Preset);

			if (Size >= 128)
			{
				PresetParams.Size = Size;
			}
			if (bSeedGiven)
			{
				PresetParams.Seed = Seed;
			}
			PresetParams.Sanitize();

			const FString PresetAssetName = GetKitsuneRockPresetName(Preset);

			const FKitsuneRockMaterialResult Result = UKitsuneRockMaterialLibrary::GenerateRockMaterial(
				PresetAssetName, PresetParams, Folder, bSaveAssets);

			if (!Result.bSuccess)
			{
				++Failures;
				UE_LOG(LogTemp, Error, TEXT("Ошибка: %s"), *Result.Message);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("  %s"), *Result.Message);
			}

			if (!DumpDir.IsEmpty())
			{
				FKitsuneRockMaps Maps;
				GenerateKitsuneRockMaps(PresetParams, Maps);
				DumpKitsuneMaps(DumpDir, PresetAssetName, Maps);
			}
		}

		UE_LOG(LogTemp, Display, TEXT("Пресетов обработано: %d, ошибок: %d"),
			GetKitsuneRockPresets().Num(), Failures);

		if (bVerify)
		{
			Failures += VerifyKitsuneRockAssets(Folder, Size);
		}

		return Failures > 0 ? 1 : 0;
	}

	FKitsuneRockParams ParamsStruct;
	EKitsuneRockPreset Preset = EKitsuneRockPreset::Granite;
	if (!PresetName.IsEmpty() && FindKitsuneRockPreset(PresetName, Preset))
	{
		ParamsStruct.ApplyPreset(Preset);
	}
	else if (!PresetName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Пресет «%s» не найден — беру значения по умолчанию"), *PresetName);
	}

	if (Size >= 128)
	{
		ParamsStruct.Size = Size;
	}
	if (bSeedGiven)
	{
		ParamsStruct.Seed = Seed;
	}

	ReadFloat(TEXT("-scale="), ParamsStruct.Scale);
	ReadFloat(TEXT("-relief="), ParamsStruct.Relief);
	ReadFloat(TEXT("-cracks="), ParamsStruct.Cracks);
	ReadFloat(TEXT("-strata="), ParamsStruct.Strata);
	ReadFloat(TEXT("-moisture="), ParamsStruct.Moisture);
	ReadFloat(TEXT("-moss="), ParamsStruct.Moss);
	ReadFloat(TEXT("-roughness="), ParamsStruct.Roughness);
	ReadFloat(TEXT("-brick="), ParamsStruct.Brick);
	ReadFloat(TEXT("-veins="), ParamsStruct.Veins);
	ReadFloat(TEXT("-normalstrength="), ParamsStruct.NormalStrength);
	ReadFloat(TEXT("-ao="), ParamsStruct.AOStrength);

	int32 Octaves = 0;
	if (FParse::Value(*Params, TEXT("-octaves="), Octaves))
	{
		ParamsStruct.Octaves = Octaves;
	}

	if (FParse::Param(*Params, TEXT("inverty")))
	{
		ParamsStruct.bInvertNormalY = true;
	}

	ParamsStruct.Sanitize();

	const FKitsuneRockMaterialResult Result = UKitsuneRockMaterialLibrary::GenerateRockMaterial(
		Name, ParamsStruct, Folder, bSaveAssets);

	if (!Result.bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("Ошибка: %s"), *Result.Message);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("Создан материал %s"), *Result.Message);

	if (!DumpDir.IsEmpty())
	{
		FKitsuneRockMaps Maps;
		GenerateKitsuneRockMaps(ParamsStruct, Maps);
		DumpKitsuneMaps(DumpDir, Name, Maps);
	}

	if (bVerify)
	{
		return VerifyKitsuneRockAssets(Folder, ParamsStruct.Size) > 0 ? 1 : 0;
	}

	return 0;
#endif
}
