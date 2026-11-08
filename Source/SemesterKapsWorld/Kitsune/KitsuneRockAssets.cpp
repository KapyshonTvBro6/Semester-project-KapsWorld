// KitsuneRockAssets.cpp
// Редакторская часть генератора: из карт, посчитанных KitsuneRockField.cpp,
// собираются шесть UTexture2D, master-материал M_KitsuneRock и его инстанс.
//
// Master-материал один на все настройки: он читает шесть текстур-параметров
// и несколько скаляров, поэтому новый камень = новые текстуры + инстанс,
// а не новый материал с нуля.
#include "KitsuneRockAssets.h"
#include "KitsuneRockField.h"
#include "SemesterKapsWorld.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionBumpOffset.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/Char.h"
#include "UObject/Class.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#endif

#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

DEFINE_LOG_CATEGORY_STATIC(LogKitsuneRockAssets, Log, All);

const FString& KitsuneRockMaterial::DefaultAssetFolder()
{
	static const FString Folder(TEXT("/Game/Kitsune/Rock"));
	return Folder;
}

const FString& KitsuneRockMaterial::MasterMaterialName()
{
	static const FString Name(TEXT("M_KitsuneRock"));
	return Name;
}

FString KitsuneRockMaterial::MasterMaterialPath(const FString& AssetFolder)
{
	const FString& Folder = AssetFolder.IsEmpty() ? DefaultAssetFolder() : AssetFolder;
	return Folder / MasterMaterialName();
}

#if WITH_EDITOR
namespace
{
	/** Имя ассета из произвольной строки: только буквы, цифры и подчёркивания. */
	FString MakeSafeAssetName(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());

		for (const TCHAR C : In)
		{
			if (FChar::IsAlnum(C) || C > 127)
			{
				Out.AppendChar(C);
			}
			else if (C == TEXT(' ') || C == TEXT('-') || C == TEXT('.'))
			{
				Out.AppendChar(TEXT('_'));
			}
		}

		if (Out.IsEmpty())
		{
			Out = TEXT("KitsuneRock");
		}
		if (FChar::IsDigit(Out[0]))
		{
			Out = TEXT("Rock") + Out;
		}
		return Out;
	}

	/** Создать пакет (или взять существующий) и загрузить его целиком. */
	UPackage* EnsurePackage(const FString& LongPackageName)
	{
		UPackage* Package = FindPackage(nullptr, *LongPackageName);
		if (!Package)
		{
			Package = CreatePackage(*LongPackageName);
		}
		if (Package)
		{
			Package->FullyLoad();
			Package->Modify();
		}
		return Package;
	}

	/** Сохранить пакет на диск как обычный .uasset. */
	bool SaveAssetToDisk(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}

		UPackage* Package = Asset->GetPackage();
		if (!Package)
		{
			return false;
		}

		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
		{
			UE_LOG(LogKitsuneRockAssets, Error, TEXT("Не удалось построить путь файла для пакета %s"), *Package->GetName());
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		SaveArgs.bWarnOfLongFilename = false;

		const FSavePackageResultStruct Result = UPackage::Save(Package, Asset, *Filename, SaveArgs);
		if (!Result.IsSuccessful())
		{
			UE_LOG(LogKitsuneRockAssets, Error, TEXT("Пакет %s не сохранён (результат %d)"), *Package->GetName(), (int32)Result.Result);
			return false;
		}

		Package->SetDirtyFlag(false);
		return true;
	}

	/** Создать или обновить UTexture2D из байтов FColor (BGRA). */
	UTexture2D* CreateOrRefreshTexture(
		UPackage* Package,
		const FString& AssetName,
		int32 Size,
		const TArray<FColor>& Data,
		TextureCompressionSettings Compression,
		TextureGroup Group,
		bool bSRGB)
	{
		if (!Package || Data.Num() != Size * Size)
		{
			return nullptr;
		}

		FCreateTexture2DParameters Params;
		Params.bUseAlpha = false;
		Params.bDeferCompression = false;
		Params.bSRGB = bSRGB;
		Params.CompressionSettings = Compression;
		Params.TextureGroup = Group;
		Params.MipGenSettings = TMGS_SimpleAverage;

		UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
		if (!Texture)
		{
			Texture = FImageUtils::CreateTexture2D(Size, Size, Data, Package, AssetName, RF_Public | RF_Standalone, Params);
		}
		else
		{
			Texture->Modify();
			Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8);

			uint8* MipData = Texture->Source.LockMip(0);
			if (MipData)
			{
				FMemory::Memcpy(MipData, Data.GetData(), (SIZE_T)(Size * Size) * sizeof(FColor));
			}
			Texture->Source.UnlockMip(0);

			Texture->CompressionSettings = Compression;
			Texture->SRGB = bSRGB ? 1 : 0;
			Texture->LODGroup = Group;
			Texture->MipGenSettings = TMGS_SimpleAverage;
			Texture->UpdateResource();
		}

		if (Texture)
		{
			// Обёртка по умолчанию (TA_Wrap) уже даёт бесшовность: карты генератора
			// считают с периодическим шумом и не имеют шва.
			Texture->PostEditChange();
			FAssetRegistryModule::AssetCreated(Texture);
		}

		return Texture;
	}

	/**
	 * Собрать graph master-материала.
	 * UV -> масштаб Tiling -> BumpOffset (высота) -> шесть семплов-параметров -> выходы.
	 */
	UMaterial* BuildMasterMaterial(UPackage* Package, const FString& AssetName)
	{
		UMaterial* Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone);
		Material->TwoSided = false;
		Material->SetShadingModel(MSM_DefaultLit);

		UMaterialEditorOnlyData* Ed = Material->GetEditorOnlyData();
		if (!Ed)
		{
			return nullptr;
		}

		auto AddExpr = [&](UMaterialExpression* Expr, int32 X, int32 Y) -> UMaterialExpression*
		{
			Expr->MaterialExpressionEditorX = X;
			Expr->MaterialExpressionEditorY = Y;
			Material->GetExpressionCollection().AddExpression(Expr);
			return Expr;
		};

		auto MakeScalar = [&](const FName& Name, float Default, int32 X, int32 Y) -> UMaterialExpressionScalarParameter*
		{
			UMaterialExpressionScalarParameter* Node = NewObject<UMaterialExpressionScalarParameter>(Material);
			Node->ParameterName = Name;
			Node->DefaultValue = Default;
			Node->Group = TEXT("Камень");
			AddExpr(Node, X, Y);
			return Node;
		};

		auto MakeVector = [&](const FName& Name, const FLinearColor& Default, int32 X, int32 Y) -> UMaterialExpressionVectorParameter*
		{
			UMaterialExpressionVectorParameter* Node = NewObject<UMaterialExpressionVectorParameter>(Material);
			Node->ParameterName = Name;
			Node->DefaultValue = Default;
			Node->Group = TEXT("Камень");
			AddExpr(Node, X, Y);
			return Node;
		};

		auto Connect = [](FExpressionInput& Input, UMaterialExpression* Source)
		{
			Input.Expression = Source;
			Input.OutputIndex = 0;
		};

		auto MakeTexParam = [&](const FName& Name, EMaterialSamplerType Sampler, UMaterialExpression* CoordinatesNode, int32 X, int32 Y) -> UMaterialExpressionTextureSampleParameter2D*
		{
			UMaterialExpressionTextureSampleParameter2D* Node = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
			Node->ParameterName = Name;
			Node->SamplerType = Sampler;
			Node->Group = TEXT("Текстуры камня");
			Connect(Node->Coordinates, CoordinatesNode);
			AddExpr(Node, X, Y);
			return Node;
		};

		// --- UV и её масштаб ---
		UMaterialExpression* UV = AddExpr(NewObject<UMaterialExpressionTextureCoordinate>(Material), -1900, 0);
		UMaterialExpressionScalarParameter* TilingParam = MakeScalar(KitsuneRockMaterial::Tiling(), 1.0f, -1900, 260);

		UMaterialExpressionMultiply* UVScale = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Material), -1650, 0);
		Connect(UVScale->A, UV);
		Connect(UVScale->B, TilingParam);

		// --- высота и параллакс ---
		UMaterialExpressionTextureSampleParameter2D* HeightSample = MakeTexParam(
			KitsuneRockMaterial::HeightTexture(), EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale, UVScale, -1650, 700);

		UMaterialExpressionScalarParameter* HeightStrengthParam = MakeScalar(KitsuneRockMaterial::HeightStrength(), 0.0f, -1650, 950);

		UMaterialExpressionBumpOffset* Bump = (UMaterialExpressionBumpOffset*)AddExpr(NewObject<UMaterialExpressionBumpOffset>(Material), -1400, 620);
		Connect(Bump->Coordinate, UVScale);
		Connect(Bump->Height, HeightSample);
		Connect(Bump->HeightRatioInput, HeightStrengthParam);
		Bump->ReferencePlane = 0.5f;
		Bump->HeightRatio = 0.0f;

		// --- шесть текстур-параметров ---
		UMaterialExpressionTextureSampleParameter2D* BaseSample = MakeTexParam(
			KitsuneRockMaterial::BaseColorTexture(), EMaterialSamplerType::SAMPLERTYPE_Color, Bump, -1150, -520);

		UMaterialExpressionTextureSampleParameter2D* NormalSample = MakeTexParam(
			KitsuneRockMaterial::NormalTexture(), EMaterialSamplerType::SAMPLERTYPE_Normal, Bump, -1150, -180);

		UMaterialExpressionTextureSampleParameter2D* RoughSample = MakeTexParam(
			KitsuneRockMaterial::RoughnessTexture(), EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale, Bump, -1150, 160);

		UMaterialExpressionTextureSampleParameter2D* AOSample = MakeTexParam(
			KitsuneRockMaterial::AOTexture(), EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale, UVScale, -1150, 420);

		UMaterialExpressionTextureSampleParameter2D* EmissiveSample = MakeTexParam(
			KitsuneRockMaterial::EmissiveTexture(), EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale, Bump, -1150, 900);

		// --- BaseColor * Tint ---
		UMaterialExpressionVectorParameter* TintParam = MakeVector(KitsuneRockMaterial::Tint(), FLinearColor::White, -900, -360);
		UMaterialExpressionMultiply* BaseMul = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Material), -700, -520);
		Connect(BaseMul->A, BaseSample);
		Connect(BaseMul->B, TintParam);
		Ed->BaseColor.Expression = BaseMul;
		Ed->BaseColor.OutputIndex = 0;

		// --- Normal: сила нормали (Custom вместо ComponentMask, чтобы не плодить узлы) ---
		UMaterialExpressionScalarParameter* NormalStrengthParam = MakeScalar(KitsuneRockMaterial::NormalStrength(), 1.0f, -900, -40);

		UMaterialExpressionCustom* NormalCustom = NewObject<UMaterialExpressionCustom>(Material);
		NormalCustom->Description = TEXT("Сила нормали");

		FCustomInput InputNormal;
		InputNormal.InputName = TEXT("NormalIn");
		Connect(InputNormal.Input, NormalSample);

		FCustomInput InputStrength;
		InputStrength.InputName = TEXT("Strength");
		Connect(InputStrength.Input, NormalStrengthParam);

		NormalCustom->Inputs.SetNum(2, EAllowShrinking::No);
		NormalCustom->Inputs[0] = InputNormal;
		NormalCustom->Inputs[1] = InputStrength;
		NormalCustom->OutputType = ECustomMaterialOutputType::CMOT_Float3;
		NormalCustom->Code =
			TEXT("float3 N = NormalIn;\n")
			TEXT("float3 Scaled = float3(N.xy * Strength, max(N.z, 0.0001));\n")
			TEXT("return normalize(Scaled);\n");

		AddExpr(NormalCustom, -700, -180);
		Ed->Normal.Expression = NormalCustom;
		Ed->Normal.OutputIndex = 0;

		// --- Roughness * RoughnessAdjust, затем Clamp 0..1 ---
		UMaterialExpressionScalarParameter* RoughAdjustParam = MakeScalar(KitsuneRockMaterial::RoughnessAdjust(), 1.0f, -900, 300);
		UMaterialExpressionMultiply* RoughMul = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Material), -700, 160);
		Connect(RoughMul->A, RoughSample);
		Connect(RoughMul->B, RoughAdjustParam);

		UMaterialExpressionClamp* RoughClamp = (UMaterialExpressionClamp*)AddExpr(NewObject<UMaterialExpressionClamp>(Material), -500, 160);
		Connect(RoughClamp->Input, RoughMul);
		RoughClamp->ClampMode = CMODE_Clamp;
		RoughClamp->MinDefault = 0.0f;
		RoughClamp->MaxDefault = 1.0f;

		Ed->Roughness.Expression = RoughClamp;
		Ed->Roughness.OutputIndex = 0;

		// --- Specular (камень: 0.3 вместо стандартных 0.05) ---
		UMaterialExpressionScalarParameter* SpecularParam = MakeScalar(KitsuneRockMaterial::Specular(), 0.3f, -500, 40);
		Ed->Specular.Expression = SpecularParam;
		Ed->Specular.OutputIndex = 0;

		// --- AO: 1 -> AO, вес AOStrength ---
		UMaterialExpressionScalarParameter* AOStrengthParam = MakeScalar(KitsuneRockMaterial::AOStrength(), 1.0f, -900, 560);
		UMaterialExpressionLinearInterpolate* AOLerp = (UMaterialExpressionLinearInterpolate*)AddExpr(NewObject<UMaterialExpressionLinearInterpolate>(Material), -700, 420);
		AOLerp->ConstA = 1.0f;
		Connect(AOLerp->B, AOSample);
		Connect(AOLerp->Alpha, AOStrengthParam);

		Ed->AmbientOcclusion.Expression = AOLerp;
		Ed->AmbientOcclusion.OutputIndex = 0;

		// --- Emissive: цвет вен * маска * сила ---
		UMaterialExpressionScalarParameter* EmissiveStrengthParam = MakeScalar(KitsuneRockMaterial::EmissiveStrength(), 0.0f, -900, 1060);
		UMaterialExpressionVectorParameter* VeinColorParam = MakeVector(KitsuneRockMaterial::VeinColor(), FLinearColor(1.0f, 0.35f, 0.12f), -900, 1220);

		UMaterialExpressionMultiply* EmissiveMaskMul = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Material), -700, 900);
		Connect(EmissiveMaskMul->A, EmissiveSample);
		Connect(EmissiveMaskMul->B, EmissiveStrengthParam);

		UMaterialExpressionMultiply* EmissiveColorMul = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Material), -500, 900);
		Connect(EmissiveColorMul->A, VeinColorParam);
		Connect(EmissiveColorMul->B, EmissiveMaskMul);

		Ed->EmissiveColor.Expression = EmissiveColorMul;
		Ed->EmissiveColor.OutputIndex = 0;

		Material->PostEditChange();
		FAssetRegistryModule::AssetCreated(Material);

		return Material;
	}

	/**
	 * Идемпотентная вставка каскада анти-тайлинга (UVOffset, Warp, Detail).
	 * Если каскад уже встроен — смягчает его настройки, узлы не дублируются.
	 * Реализация ниже, после починки типов сэмплеров: ей нужны карты по умолчанию.
	 */
	int32 UpgradeMasterAntiTiling(UMaterial* Master, const FString& Folder, bool bSaveAssets);

	/** Master-материал: существующий не пересоздаём, чтобы не терять правки вручную. */
	UMaterial* GetOrCreateMasterMaterialInternal(const FString& AssetFolder, bool bSaveAssets)
	{
		const FString MasterPath = KitsuneRockMaterial::MasterMaterialPath(AssetFolder);
		const FString MasterName = KitsuneRockMaterial::MasterMaterialName();

		// Сначала тот, что уже лежит на диске. FindObject в командлете видит только
		// загруженные объекты, поэтому без LoadObject мастер пересоздавался с нуля и
		// правки, сделанные вручную в Material Editor, затёрлись бы. Package при этом не
		// создаём заранее: иначе на месте загруженного ассета появлялась бы пустая пачка.
		UMaterial* Master = nullptr;
		if (FPackageName::DoesPackageExist(MasterPath))
		{
			Master = LoadObject<UMaterial>(nullptr, *MasterPath);
		}

		UPackage* Package = Master ? Master->GetPackage() : EnsurePackage(MasterPath);
		if (!Package)
		{
			return nullptr;
		}

		if (!Master)
		{
			Master = FindObject<UMaterial>(Package, *MasterName);
		}
		if (!Master)
		{
			Master = BuildMasterMaterial(Package, MasterName);
		}

		if (Master)
		{
			// Каскад анти-тайлинга нужен и новому мастеру, и тому, что уже лежит на диске:
			// функция идемпотентна, а сохранение делает сама, если что-то изменила.
			UpgradeMasterAntiTiling(Master, AssetFolder, bSaveAssets);
		}

		return Master;
	}

	/** Пресеты, чьи карты служат значениями по умолчанию в master-материале. */
	const TCHAR* MasterDefaultPresets[] =
	{
		TEXT("granite"), TEXT("basalt"), TEXT("sandstone"), TEXT("mossyCave"),
		TEXT("wetCave"), TEXT("dungeonBricks"), TEXT("lavaVeins"), TEXT("crystalCave")
	};

	/**
	 * Починить шесть узлов параметров текстур в master-материале: тип сэмплера должен
	 * совпадать с фактической текстурой, и у параметра обязана быть своя текстура.
	 *
	 * Движок сам вычисляет правильный тип по компрессии и SRGB карты
	 * (MaterialExpressionUtils.cpp:14) и сверяет его с SamplerType узла
	 * (MaterialExpressionUtils.cpp:51). Если у узла-параметра своей текстуры нет,
	 * используется /Engine/EngineResources/DefaultTexture — у неё тип Color. Поэтому
	 * узел с Normal или Grayscale даёт ошибку вида «Sampler type is Grayscale, should
	 * be Color for /Engine/EngineResources/DefaultTexture», красный ERROR! на узле и
	 * «Failed to compile Material ... Default Material will be used in game» для всего
	 * мастера. Картам TC_Grayscale без SRGB нужен LinearGrayscale, а не Grayscale.
	 */
	int32 SyncMasterSamplerTypes(UMaterial* Master, const FString& Folder, const TMap<FName, UTexture2D*>& FallbackTextures)
	{
		if (!Master)
		{
			return 0;
		}

		const FName* ParamNames[] = {
			&KitsuneRockMaterial::BaseColorTexture(),
			&KitsuneRockMaterial::NormalTexture(),
			&KitsuneRockMaterial::RoughnessTexture(),
			&KitsuneRockMaterial::AOTexture(),
			&KitsuneRockMaterial::HeightTexture(),
			&KitsuneRockMaterial::EmissiveTexture()
		};
		const TCHAR* MapSuffixes[] = {
			TEXT("_BaseColor"), TEXT("_Normal"), TEXT("_Roughness"),
			TEXT("_AO"), TEXT("_Height"), TEXT("_Emissive")
		};

		UEnum* SamplerEnum = StaticEnum<EMaterialSamplerType>();
		int32 Changed = 0;

		for (int32 Index = 0; Index < 6; ++Index)
		{
			const FName ParamName = *ParamNames[Index];

			UTexture2D* DefaultTexture = nullptr;
			for (const TCHAR* PresetKey : MasterDefaultPresets)
			{
				// LoadObject, а не FindObject: в commandlet карты с диска не загружены
				// заранее, а только что созданные текстуры LoadObject находит в памяти.
				DefaultTexture = LoadObject<UTexture2D>(nullptr, *(Folder / (FString(TEXT("T_")) + PresetKey + MapSuffixes[Index])));
				if (DefaultTexture)
				{
					break;
				}
			}

			if (!DefaultTexture)
			{
				const UTexture2D* const* Fallback = FallbackTextures.Find(ParamName);
				if (Fallback && *Fallback)
				{
					DefaultTexture = const_cast<UTexture2D*>(*Fallback);
				}
			}

			if (!DefaultTexture)
			{
				UE_LOG(LogKitsuneRockAssets, Warning, TEXT("В %s нет карты для параметра %s — оставить тип сэмплера не удалось"),
					*Folder, *ParamName.ToString());
				continue;
			}

			const EMaterialSamplerType CorrectSampler = MaterialExpressionUtils::GetSamplerTypeForTexture(DefaultTexture);

			for (UMaterialExpression* Expr : Master->GetExpressions())
			{
				UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr);
				if (!Sample || Sample->ParameterName != ParamName)
				{
					continue;
				}

				if (Sample->Texture.Get() != DefaultTexture)
				{
					Sample->Texture = DefaultTexture;
					++Changed;
				}

				if ((EMaterialSamplerType)Sample->SamplerType != CorrectSampler)
				{
					UE_LOG(LogKitsuneRockAssets, Display, TEXT("«%s»: тип сэмплера %s -> %s (карта %s)"),
						*ParamName.ToString(),
						*SamplerEnum->GetDisplayNameTextByValue(Sample->SamplerType.GetValue()).ToString(),
						*SamplerEnum->GetDisplayNameTextByValue((int64)CorrectSampler).ToString(),
						*DefaultTexture->GetName());
					Sample->SamplerType = CorrectSampler;
					++Changed;
				}
			}
		}

		if (Changed > 0)
		{
			Master->Modify();
			Master->PostEditChange();
		}

		return Changed;
	}

	/**
	 * Анти-тайлинг: вставка в цепочку UV master-материала.
	 *
	 * Почему тайлинг бросается в глаза. Карты генератора периодичны по построению, а
	 * геометрия подземелья считает UV в сантиметрах (u = координата / TextureTileSize),
	 * поэтому плитка ложится ровно через TextureTileSize на всех поверхностях, и ряд
	 * одинаковых стен получает дословно один и тот же рисунок.
	 *
	 * Что добавляется (всё это параметры материала, крутятся прямо в инстансе):
	 *   1. UVOffsetX / UVOffsetY — сдвиг раскладки в плитках: разные инстансы смотрят
	 *      в разные места карты.
	 *   2. WarpAmount / WarpScale — волновое искажение UV по мировой позиции узлами
	 *      Noise (своего HLSL тут нет). Период волны не кратен размеру плитки, поэтому
	 *      границы плиток смазываются и повтор перестаёт читаться.
	 *      WarpScale — частота в 1/см: 0.004 = волна примерно на 2.5 метра.
	 *   3. DetailScale / DetailMix — второй слой тех же карт BaseColor и Normal, взятый
	 *      в DetailScale раз мельче и подмешанный через DetailMix.
	 *
	 * Прежние шесть параметров и их связи не меняются: новые узлы только вставляются
	 * между множителем UV и BumpOffset. Идемпотентно: по наличию параметра DetailMix.
	 */
	// Настройка каскада. Волна обязана быть крупнее плитки и заметно слабее: если её
	// собственный градиент приближается к частоте плитки, пиксели карты начинают
	// складываться и по поверхности идут тонкие штрихи. Проверка на глаз:
	//	WarpAmount * WarpScale * TextureTileSize < 0.1
	// при TextureTileSize = 300 прежние значения (0.10 и 0.004) давали 0.12 —
	// отсюда и артефакты, которые исчезают при WarpAmount = 0.
	// Новый набор: 0.016. Простой сдвиг UVOffset градиента не имеет, он безопасен всегда.
	const float AntiTilingWarpAmount = 0.02f;
	const float AntiTilingWarpScale = 0.0015f;   // период волны около 6.7 м
	// DetailScale целое: решётка детального слоя вложена в решётку придонного,
	// поэтому две выборки одной карты не бьются в муар. Расхождение даёт волна,
	// а не дробный масштаб — она и ломает повтор.
	const float AntiTilingDetailScale = 2.0f;
	const float AntiTilingDetailMix = 0.15f;
	// Прежние значения по ним отличают нетронутые инстансы от правленных вручную.
	const float AntiTilingLegacyWarpAmount = 0.10f;
	const float AntiTilingLegacyWarpScale = 0.004f;
	const float AntiTilingLegacyDetailScale = 3.17f;
	const float AntiTilingLegacyDetailMix = 0.35f;

	/** Правит уже встроенный каскад: тип волны и значения по умолчанию её параметров. */
	int32 TuneExistingAntiTilingNodes(UMaterial* Master)
	{
		int32 Repaired = 0;

		for (UMaterialExpression* Expr : Master->GetExpressions())
		{
			if (UMaterialExpressionNoise* Node = Cast<UMaterialExpressionNoise>(Expr))
			{
				// ValueALU в движке подписан как «Low quality» и «formerly mis-labeled as
				// Gradient noise»: это шум значений, у которого производная обнуляется в узлах
				// решётки. Искажение шло плато вдоль осей мира, и вместо мягкой волны была
				// видна структура. GradientALU («Gradient - Computational», Perlin Modified Noise)
				// непрерывна по первой производной и для искажения UV подходит напрямую.
				// Частоту узла (Scale) не трогаем: двумя узлами она и различается.
				if (Node->NoiseFunction != NOISEFUNCTION_GradientALU
					|| Node->Levels != 1
					|| Node->bTurbulence != 0
					|| !FMath::IsNearlyEqual(Node->OutputMin, -1.0f)
					|| !FMath::IsNearlyEqual(Node->OutputMax, 1.0f))
				{
					Node->NoiseFunction = NOISEFUNCTION_GradientALU;
					Node->Quality = 1;
					Node->Levels = 1;
					Node->LevelScale = 2.0f;
					Node->bTurbulence = 0;
					Node->bTiling = 0;
					Node->OutputMin = -1.0f;
					Node->OutputMax = 1.0f;
					Node->WorldPositionOriginType = EPositionOrigin::Absolute;
					++Repaired;
				}
			}
			else if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				auto SoftenScalar = [&](const FName& Name, float LegacyValue, float NewValue)
				{
					if (Scalar->ParameterName == Name && FMath::IsNearlyEqual(Scalar->DefaultValue, LegacyValue))
					{
						Scalar->DefaultValue = NewValue;
						++Repaired;
					}
				};
				SoftenScalar(KitsuneRockMaterial::WarpAmount(), AntiTilingLegacyWarpAmount, AntiTilingWarpAmount);
				SoftenScalar(KitsuneRockMaterial::WarpScale(), AntiTilingLegacyWarpScale, AntiTilingWarpScale);
				SoftenScalar(KitsuneRockMaterial::DetailScale(), AntiTilingLegacyDetailScale, AntiTilingDetailScale);
				SoftenScalar(KitsuneRockMaterial::DetailMix(), AntiTilingLegacyDetailMix, AntiTilingDetailMix);
			}
		}

		return Repaired;
	}

	/**
	 * Перезаписывает четыре параметра волны и детали в инстансах генератора.
	 * Кирпичные инстансы пропускаются: кладка читается и без каскада, а сдвиг и волна
	 * ломают стыковку швов — они оставлены ровно такими, какими их сделал пользователь.
	 * UVOffset не трогается: сдвиг без градиента артефактов не создаёт.
	 */
	int32 TuneRockInstancesAntiTiling(UMaterial* Master, const FString& Folder, bool bSaveAssets)
	{
		if (!Master)
		{
			return 0;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*Folder), Assets, /*bRecursive=*/false);

		int32 Tuned = 0;
		for (const FAssetData& Asset : Assets)
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
			if (!Instance || Instance->GetMaterial() != Master)
			{
				continue;
			}

			if (Instance->GetName().Find(TEXT("brick"), ESearchCase::IgnoreCase) != INDEX_NONE)
			{
				UE_LOG(LogKitsuneRockAssets, Log, TEXT("%s: кирпич оставлен как есть"), *Instance->GetName());
				continue;
			}

			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::WarpAmount()), AntiTilingWarpAmount);
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::WarpScale()), AntiTilingWarpScale);
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::DetailScale()), AntiTilingDetailScale);
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::DetailMix()), AntiTilingDetailMix);
			Instance->PostEditChange();

			if (bSaveAssets)
			{
				SaveAssetToDisk(Instance);
			}
			++Tuned;
		}

		return Tuned;
	}

	int32 UpgradeMasterAntiTiling(UMaterial* Master, const FString& Folder, bool bSaveAssets)
	{
		if (!Master)
		{
			return 0;
		}

		UMaterialEditorOnlyData* Ed = Master->GetEditorOnlyData();
		if (!Ed)
		{
			return 0;
		}

		bool bAlready = false;
		UMaterialExpressionBumpOffset* Bump = nullptr;
		UMaterialExpressionCustom* NormalCustom = nullptr;
		UMaterialExpressionMultiply* UVScale = nullptr;
		UMaterialExpressionMultiply* BaseMul = nullptr;
		UMaterialExpressionTextureSampleParameter2D* HeightSample = nullptr;
		UMaterialExpressionTextureSampleParameter2D* AOSample = nullptr;
		UMaterialExpressionTextureSampleParameter2D* BaseSample = nullptr;
		UMaterialExpressionTextureSampleParameter2D* NormalSample = nullptr;
		UMaterialExpressionScalarParameter* NormalStrengthParam = nullptr;

		for (UMaterialExpression* Expr : Master->GetExpressions())
		{
			if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				if (Scalar->ParameterName == KitsuneRockMaterial::DetailMix())
				{
					bAlready = true;
				}
				else if (Scalar->ParameterName == KitsuneRockMaterial::NormalStrength() && !NormalStrengthParam)
				{
					NormalStrengthParam = Scalar;
				}
			}
			else if (UMaterialExpressionBumpOffset* Offset = Cast<UMaterialExpressionBumpOffset>(Expr))
			{
				if (!Bump)
				{
					Bump = Offset;
				}
			}
			else if (UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
			{
				if (Sample->ParameterName == KitsuneRockMaterial::HeightTexture() && !HeightSample)
				{
					HeightSample = Sample;
				}
				else if (Sample->ParameterName == KitsuneRockMaterial::AOTexture() && !AOSample)
				{
					AOSample = Sample;
				}
				else if (Sample->ParameterName == KitsuneRockMaterial::BaseColorTexture() && !BaseSample)
				{
					BaseSample = Sample;
				}
				else if (Sample->ParameterName == KitsuneRockMaterial::NormalTexture() && !NormalSample)
				{
					NormalSample = Sample;
				}
			}
		}

		if (bAlready)
		{
			const int32 Repaired = TuneExistingAntiTilingNodes(Master);

			// Сохраняем всегда, когда это разрешено, а не только когда счётчик nonzero.
			// GetOrCreateMasterMaterialInternal вызывает эту же функцию сам: если он сделал
			// это без сохранения, повторный вызов снаружи уже не находит, что править, и
			// поправка остаётся только в памяти — ассет на диске не меняется. Для загруженного
			// с диска мастера повторная запись без изменений безвредна.
			if (bSaveAssets)
			{
				Master->Modify();
				Master->PostEditChange();
				SaveAssetToDisk(Master);
			}
			if (Repaired > 0)
			{
				UE_LOG(LogKitsuneRockAssets, Log, TEXT("%s: каскад уже был — смягчено узлов и параметров %d%s"),
					*Master->GetName(), Repaired, bSaveAssets ? TEXT("") : TEXT(" (без сохранения)"));
			}
			else
			{
				UE_LOG(LogKitsuneRockAssets, Log, TEXT("%s: каскад анти-тайлинга уже встроен и настроен — узлов этого вызова %d%s"),
					*Master->GetName(), Repaired, bSaveAssets ? TEXT(", мастер перезаписан на диск") : TEXT(""));
			}
			return 0;
		}

		// Якоря графа: без них вставляться не во что — значит граф правили вручную.
		if (!Bump || !HeightSample || !BaseSample || !NormalSample || !NormalStrengthParam || !Ed->BaseColor.Expression || !Ed->Normal.Expression)
		{
			UE_LOG(LogKitsuneRockAssets, Warning, TEXT("%s: не найдены узлы BumpOffset, карты BaseColor, Normal или Height либо скаляр NormalStrength — анти-тайлинг не добавлен"), *Master->GetName());
			return 0;
		}

		UVScale = Cast<UMaterialExpressionMultiply>(Bump->Coordinate.Expression);
		BaseMul = Cast<UMaterialExpressionMultiply>(Ed->BaseColor.Expression);
		NormalCustom = Cast<UMaterialExpressionCustom>(Ed->Normal.Expression);
		if (!UVScale || !BaseMul || !NormalCustom)
		{
			UE_LOG(LogKitsuneRockAssets, Warning, TEXT("%s: Coordinate у BumpOffset, BaseColor или Normal заняты другим узлом — анти-тайлинг не добавлен"), *Master->GetName());
			return 0;
		}

		int32 Added = 0;

		auto AddExpr = [&](UMaterialExpression* Expr, int32 X, int32 Y) -> UMaterialExpression*
		{
			Expr->MaterialExpressionEditorX = X;
			Expr->MaterialExpressionEditorY = Y;
			Master->GetExpressionCollection().AddExpression(Expr);
			++Added;
			return Expr;
		};

		auto MakeScalar = [&](const FName& Name, float Default, int32 X, int32 Y) -> UMaterialExpressionScalarParameter*
		{
			UMaterialExpressionScalarParameter* Node = NewObject<UMaterialExpressionScalarParameter>(Master);
			Node->ParameterName = Name;
			Node->DefaultValue = Default;
			Node->Group = TEXT("Анти-тайлинг");
			AddExpr(Node, X, Y);
			return Node;
		};

		auto Connect = [](FExpressionInput& Input, UMaterialExpression* Source)
		{
			Input.Expression = Source;
			Input.OutputIndex = 0;
		};

		auto MakeNoise = [&](UMaterialExpression* PositionNode, float Frequency, int32 X, int32 Y) -> UMaterialExpressionNoise*
		{
			UMaterialExpressionNoise* Node = NewObject<UMaterialExpressionNoise>(Master);
			Connect(Node->Position, PositionNode);
			// Вычислительная волна без текстур и без периодичности: иначе искажение
			// само стало бы повторяющимся и тайлинг вернулся бы.
			// GradientALU, не ValueALU: у шума значений производная обнуляется в узлах
			// решётки, и искажение идёт ступенями вдоль осей мира — это видно на стене.
			Node->NoiseFunction = NOISEFUNCTION_GradientALU;
			Node->Quality = 1;
			Node->Levels = 1;
			Node->Scale = Frequency;
			Node->LevelScale = 2.0f;
			Node->bTurbulence = 0;
			Node->bTiling = 0;
			Node->OutputMin = -1.0f;
			Node->OutputMax = 1.0f;
			Node->WorldPositionOriginType = EPositionOrigin::Absolute;
			AddExpr(Node, X, Y);
			return Node;
		};

		// 1. Смещение всей раскладки, в плитках текстуры.
		UMaterialExpressionScalarParameter* OffsetXParam = MakeScalar(KitsuneRockMaterial::UVOffsetX(), 0.0f, -1900, -320);
		UMaterialExpressionScalarParameter* OffsetYParam = MakeScalar(KitsuneRockMaterial::UVOffsetY(), 0.0f, -1900, -140);

		UMaterialExpressionAdd* OffsetU = (UMaterialExpressionAdd*)AddExpr(NewObject<UMaterialExpressionAdd>(Master), -1500, -120);
		Connect(OffsetU->A, UVScale);
		Connect(OffsetU->B, OffsetXParam);

		UMaterialExpressionAdd* OffsetV = (UMaterialExpressionAdd*)AddExpr(NewObject<UMaterialExpressionAdd>(Master), -1350, -120);
		Connect(OffsetV->A, OffsetU);
		Connect(OffsetV->B, OffsetYParam);

		// 2. Волна по мировой позиции: её период не кратен плитке, швы плиток смазываются.
		UMaterialExpressionWorldPosition* WorldPos = (UMaterialExpressionWorldPosition*)AddExpr(NewObject<UMaterialExpressionWorldPosition>(Master), -1900, -1100);
		WorldPos->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;

		UMaterialExpressionScalarParameter* WarpScaleParam = MakeScalar(KitsuneRockMaterial::WarpScale(), AntiTilingWarpScale, -1750, -1100);
		UMaterialExpressionScalarParameter* WarpAmountParam = MakeScalar(KitsuneRockMaterial::WarpAmount(), AntiTilingWarpAmount, -1750, -920);

		UMaterialExpressionMultiply* WarpPos = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Master), -1550, -1100);
		Connect(WarpPos->A, WorldPos);
		Connect(WarpPos->B, WarpScaleParam);

		UMaterialExpressionNoise* NoiseU = MakeNoise(WarpPos, 1.0f, -1350, -1100);
		// Частота 1.731 вместо 1.0: другое поле шума, иначе X и Y сдвигались бы одинаково.
		UMaterialExpressionNoise* NoiseV = MakeNoise(WarpPos, 1.731f, -1350, -900);

		UMaterialExpressionMultiply* WarpU = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Master), -1150, -1100);
		Connect(WarpU->A, NoiseU);
		Connect(WarpU->B, WarpAmountParam);

		UMaterialExpressionMultiply* WarpV = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Master), -1150, -900);
		Connect(WarpV->A, NoiseV);
		Connect(WarpV->B, WarpAmountParam);

		UMaterialExpressionAdd* WarpAdd = (UMaterialExpressionAdd*)AddExpr(NewObject<UMaterialExpressionAdd>(Master), -1200, -120);
		Connect(WarpAdd->A, OffsetV);
		Connect(WarpAdd->B, WarpU);

		UMaterialExpressionAdd* UVWarped = (UMaterialExpressionAdd*)AddExpr(NewObject<UMaterialExpressionAdd>(Master), -1050, -120);
		Connect(UVWarped->A, WarpAdd);
		Connect(UVWarped->B, WarpV);

		// 3. Далёкие образцы читают искажённые UV; связи самих образцов не тронуты.
		Connect(HeightSample->Coordinates, UVWarped);
		Connect(Bump->Coordinate, UVWarped);
		if (AOSample)
		{
			Connect(AOSample->Coordinates, UVWarped);
		}

		// 4. Детальный слой из тех же карт: тот же параметр -> та же карта в инстансе,
		// только UV в DetailScale раз мельче.
		UMaterialExpressionScalarParameter* DetailScaleParam = MakeScalar(KitsuneRockMaterial::DetailScale(), AntiTilingDetailScale, -1050, 240);
		UMaterialExpressionScalarParameter* DetailMixParam = MakeScalar(KitsuneRockMaterial::DetailMix(), AntiTilingDetailMix, -1050, 420);

		UMaterialExpressionMultiply* UVDetailed = (UMaterialExpressionMultiply*)AddExpr(NewObject<UMaterialExpressionMultiply>(Master), -850, 1150);
		Connect(UVDetailed->A, UVWarped);
		Connect(UVDetailed->B, DetailScaleParam);

		UMaterialExpressionTextureSampleParameter2D* DetailBase = NewObject<UMaterialExpressionTextureSampleParameter2D>(Master);
		DetailBase->ParameterName = KitsuneRockMaterial::BaseColorTexture();
		DetailBase->SamplerType = SAMPLERTYPE_Color;
		DetailBase->Group = TEXT("Текстуры камня");
		DetailBase->Desc = TEXT("Детальный слой BaseColor");
		Connect(DetailBase->Coordinates, UVDetailed);
		AddExpr(DetailBase, -620, 1150);

		UMaterialExpressionTextureSampleParameter2D* DetailNormal = NewObject<UMaterialExpressionTextureSampleParameter2D>(Master);
		DetailNormal->ParameterName = KitsuneRockMaterial::NormalTexture();
		DetailNormal->SamplerType = SAMPLERTYPE_Normal;
		DetailNormal->Group = TEXT("Текстуры камня");
		DetailNormal->Desc = TEXT("Детальный слой Normal");
		Connect(DetailNormal->Coordinates, UVDetailed);
		AddExpr(DetailNormal, -620, 1400);

		UMaterialExpressionLinearInterpolate* BaseLerp = (UMaterialExpressionLinearInterpolate*)AddExpr(NewObject<UMaterialExpressionLinearInterpolate>(Master), -380, 1150);
		Connect(BaseLerp->A, BaseSample);
		Connect(BaseLerp->B, DetailBase);
		Connect(BaseLerp->Alpha, DetailMixParam);
		Connect(BaseMul->A, BaseLerp);

		// 5. Нормали смешиваются в том же Custom-узле: движкового узла смешивания
		// нормалей в 5.8 нет (BlendAngleCorrectedNormals отсутствует), поэтому lerp +
		// normalize здесь же. Входы пересозданы начисто: безымянный слот, который
		// добавляет конструктор Custom-узла, больше не нужен.
		NormalCustom->Description = TEXT("Сила нормали и детальный слой");
		NormalCustom->OutputType = ECustomMaterialOutputType::CMOT_Float3;
		NormalCustom->Inputs.Reset();
		{
			FCustomInput NormalIn;
			NormalIn.InputName = FName(TEXT("NormalIn"));
			Connect(NormalIn.Input, NormalSample);
			NormalCustom->Inputs.Add(NormalIn);

			FCustomInput DetailIn;
			DetailIn.InputName = FName(TEXT("DetailIn"));
			Connect(DetailIn.Input, DetailNormal);
			NormalCustom->Inputs.Add(DetailIn);

			FCustomInput Mix;
			Mix.InputName = FName(TEXT("Mix"));
			Connect(Mix.Input, DetailMixParam);
			NormalCustom->Inputs.Add(Mix);

			FCustomInput Strength;
			Strength.InputName = FName(TEXT("Strength"));
			Connect(Strength.Input, NormalStrengthParam);
			NormalCustom->Inputs.Add(Strength);
		}

		NormalCustom->Code = FString(
			"float3 BaseN = normalize(NormalIn);\n"
			"float3 DetailN = normalize(DetailIn);\n"
			"float3 Mixed = lerp(BaseN, DetailN, saturate(Mix));\n"
			"float3 Scaled = float3(Mixed.xy * Strength, max(Mixed.z, 0.0001));\n"
			"return normalize(Scaled);\n");

		Master->Modify();
		Master->PostEditChange();

		// 6. Новые узлы-параметры обязаны получить свои карты по умолчанию и правильный
		// тип сэмплера, иначе мастер не скомпилируется.
		Added += SyncMasterSamplerTypes(Master, Folder, TMap<FName, UTexture2D*>());

		if (bSaveAssets)
		{
			SaveAssetToDisk(Master);
		}

		UE_LOG(LogKitsuneRockAssets, Log, TEXT("%s: анти-тайлинг встроен, узлов %d (UVOffsetX/Y, WarpAmount/WarpScale, DetailScale/DetailMix)"), *Master->GetName(), Added);
		return Added;
	}
	/** Настроить инстанс: шесть текстур и все скаляры/векторы. */
	void ApplyInstanceParameters(
		UMaterialInstanceConstant* Instance,
		UMaterial* Master,
		const FKitsuneRockParams& P,
		const TMap<FName, UTexture2D*>& Textures)
	{
		if (!Instance || !Master)
		{
			return;
		}

		Instance->ClearParameterValuesEditorOnly();
		Instance->SetParentEditorOnly(Master);

		auto ApplyTexture = [&](const FName& ParamName)
		{
			const UTexture2D* const* Found = Textures.Find(ParamName);
			if (Found && *Found)
			{
				Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParamName), const_cast<UTexture2D*>(*Found));
			}
		};

		ApplyTexture(KitsuneRockMaterial::BaseColorTexture());
		ApplyTexture(KitsuneRockMaterial::NormalTexture());
		ApplyTexture(KitsuneRockMaterial::RoughnessTexture());
		ApplyTexture(KitsuneRockMaterial::AOTexture());
		ApplyTexture(KitsuneRockMaterial::HeightTexture());
		ApplyTexture(KitsuneRockMaterial::EmissiveTexture());

		// Одна карта = один «блок» камня: Tiling подбирается под масштаб стены.
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::Tiling()), 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::NormalStrength()), 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::RoughnessAdjust()), 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::AOStrength()), 1.0f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::HeightStrength()), 0.02f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::Specular()), 0.3f);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::EmissiveStrength()), P.Veins > 0.001f ? 2.5f : 0.0f);

		// Анти-тайлинг: разная раскладка у разных пресетов, чтобы соседние стены не
		// выглядели одной и той же плиткой. Смещение берётся из имени инстанса,
		// поэтому повторный запуск даёт те же значения.
		auto HasScalar = [&](const FName& ParamName)
		{
			for (UMaterialExpression* Expr : Master->GetExpressions())
			{
				if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
				{
					if (Scalar->ParameterName == ParamName)
					{
						return true;
					}
				}
			}
			return false;
		};

		if (HasScalar(KitsuneRockMaterial::WarpAmount()))
		{
			if (P.Brick > KINDA_SMALL_NUMBER)
			{
				// Кладка читается как кладка и без каскада, а сдвиг и волна рвут стыковку
				// швов: для кирпичных пресетов анти-тайлинг выключен значениями по умолчанию.
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::UVOffsetX()), 0.0f);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::UVOffsetY()), 0.0f);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::WarpAmount()), 0.0f);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::WarpScale()), AntiTilingWarpScale);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::DetailScale()), AntiTilingDetailScale);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::DetailMix()), 0.0f);
			}
			else
			{
				const uint32 Hash = (uint32)GetTypeHash(Instance->GetName());
				const float OffsetX = (float)(Hash & 0x3FF) / 1024.0f;
				const float OffsetY = (float)((Hash >> 10) & 0x3FF) / 1024.0f;

				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::UVOffsetX()), OffsetX);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::UVOffsetY()), OffsetY);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::WarpAmount()), AntiTilingWarpAmount);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::WarpScale()), AntiTilingWarpScale);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::DetailScale()), AntiTilingDetailScale);
				Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::DetailMix()), AntiTilingDetailMix);
			}
		}

		Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::Tint()), FLinearColor::White);
		Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(KitsuneRockMaterial::VeinColor()), P.VeinColor);

		Instance->PostEditChange();
	}

	FKitsuneRockMaterialResult GenerateRockMaterialInternal(
		const FString& InAssetName,
		const FKitsuneRockParams& InParams,
		const FString& InAssetFolder,
		bool bSaveAssets)
	{
		FKitsuneRockMaterialResult Result;

		FKitsuneRockParams P = InParams;
		P.Sanitize();

		const FString Folder = InAssetFolder.IsEmpty() ? KitsuneRockMaterial::DefaultAssetFolder() : InAssetFolder;
		const FString Base = MakeSafeAssetName(InAssetName);

		// 1. Карты.
		FKitsuneRockMaps Maps;
		GenerateKitsuneRockMaps(P, Maps);

		// 2. Master-материал.
		UMaterial* Master = GetOrCreateMasterMaterialInternal(Folder, bSaveAssets);
		if (!Master)
		{
			Result.Message = TEXT("Не удалось создать master-материал M_KitsuneRock");
			UE_LOG(LogKitsuneRockAssets, Error, TEXT("%s"), *Result.Message);
			return Result;
		}

		// 3. Шесть текстур.
		struct FTextureJob
		{
			const FName* Param;
			const TCHAR* Suffix;
			const TArray<FColor>* Data;
			TextureCompressionSettings Compression;
			TextureGroup Group;
			bool bSRGB;
		};

		const FTextureJob Jobs[] =
		{
			{ &KitsuneRockMaterial::BaseColorTexture(), TEXT("_BaseColor"), &Maps.BaseColor,    TC_Default,    TEXTUREGROUP_World,             true  },
			{ &KitsuneRockMaterial::NormalTexture(),    TEXT("_Normal"),    &Maps.Normal,       TC_Normalmap,  TEXTUREGROUP_WorldNormalMap,    false },
			{ &KitsuneRockMaterial::RoughnessTexture(), TEXT("_Roughness"), &Maps.Roughness,    TC_Grayscale,  TEXTUREGROUP_World,             false },
			{ &KitsuneRockMaterial::AOTexture(),        TEXT("_AO"),        &Maps.AO,           TC_Grayscale,  TEXTUREGROUP_World,             false },
			{ &KitsuneRockMaterial::HeightTexture(),    TEXT("_Height"),    &Maps.Height,       TC_Grayscale,  TEXTUREGROUP_World,             false },
			{ &KitsuneRockMaterial::EmissiveTexture(),  TEXT("_Emissive"),  &Maps.EmissiveMask, TC_Grayscale,  TEXTUREGROUP_World,             false },
		};

		TMap<FName, UTexture2D*> Textures;
		int32 SavedCount = 0;

		for (const FTextureJob& Job : Jobs)
		{
			const FString TextureName = FString(TEXT("T_")) + Base + Job.Suffix;
			UPackage* TexturePackage = EnsurePackage(Folder / TextureName);

			UTexture2D* Texture = CreateOrRefreshTexture(
				TexturePackage, TextureName, Maps.Size, *Job.Data, Job.Compression, Job.Group, Job.bSRGB);

			if (!Texture)
			{
				Result.Message = FString::Printf(TEXT("Не удалось создать текстуру %s"), *TextureName);
				UE_LOG(LogKitsuneRockAssets, Error, TEXT("%s"), *Result.Message);
				return Result;
			}

			Textures.Add(*Job.Param, Texture);
			Result.Textures.Add(Texture);

			if (bSaveAssets && SaveAssetToDisk(Texture))
			{
				++SavedCount;
			}
		}

		// 3.5. Мастер обязан иметь текстуры по умолчанию и совпадающие типы
		// сэмплеров, иначе он не компилируется сам (ошибка на узлах Param2D).
		const int32 MasterFixes = SyncMasterSamplerTypes(Master, Folder, Textures);
		if (MasterFixes > 0 && bSaveAssets)
		{
			SaveAssetToDisk(Master);
		}

		// 4. Инстанс материала.
		const FString InstanceName = FString(TEXT("MI_")) + Base;
		UPackage* InstancePackage = EnsurePackage(Folder / InstanceName);

		UMaterialInstanceConstant* Instance = FindObject<UMaterialInstanceConstant>(InstancePackage, *InstanceName);
		if (!Instance)
		{
			Instance = NewObject<UMaterialInstanceConstant>(InstancePackage, FName(*InstanceName), RF_Public | RF_Standalone);
		}
		else
		{
			Instance->Modify();
		}

		ApplyInstanceParameters(Instance, Master, P, Textures);
		FAssetRegistryModule::AssetCreated(Instance);

		if (bSaveAssets)
		{
			SaveAssetToDisk(Instance);
		}

		Result.bSuccess = true;
		Result.MaterialInstance = Instance;
		Result.Message = FString::Printf(TEXT("%s: карты %d x %d, текстур: %d, сохранённых пакетов: %d"),
			*InstanceName, Maps.Size, Maps.Size, Result.Textures.Num(), SavedCount + 1);

		UE_LOG(LogKitsuneRockAssets, Log, TEXT("Готово: %s (%s)"), *Instance->GetPathName(), *Result.Message);

		return Result;
	}
}
#endif // WITH_EDITOR

UMaterial* UKitsuneRockMaterialLibrary::GetOrCreateRockMasterMaterial(const FString& AssetFolder, bool bSaveAssets)
{
#if WITH_EDITOR
	return GetOrCreateMasterMaterialInternal(AssetFolder, bSaveAssets);
#else
	UE_LOG(LogKitsuneRockAssets, Warning, TEXT("Master-материал создаётся только в редакторе"));
	return nullptr;
#endif
}

FKitsuneRockMaterialResult UKitsuneRockMaterialLibrary::GenerateRockMaterial(
	const FString& AssetName,
	const FKitsuneRockParams& Params,
	const FString& AssetFolder,
	bool bSaveAssets)
{
#if WITH_EDITOR
	return GenerateRockMaterialInternal(AssetName, Params, AssetFolder, bSaveAssets);
#else
	UE_LOG(LogKitsuneRockAssets, Warning, TEXT("Генерация материалов доступна только в редакторе"));
	return FKitsuneRockMaterialResult();
#endif
}

TArray<FKitsuneRockMaterialResult> UKitsuneRockMaterialLibrary::GenerateAllRockPresets(int32 Size, bool bSaveAssets)
{
	TArray<FKitsuneRockMaterialResult> Results;

#if WITH_EDITOR
	for (EKitsuneRockPreset Preset : GetKitsuneRockPresets())
	{
		FKitsuneRockParams Params = MakeKitsuneRockPreset(Preset);
		if (Size >= 128)
		{
			Params.Size = Size;
		}

		Results.Add(GenerateRockMaterialInternal(GetKitsuneRockPresetName(Preset), Params, KitsuneRockMaterial::DefaultAssetFolder(), bSaveAssets));
	}
#else
	UE_LOG(LogKitsuneRockAssets, Warning, TEXT("Генерация материалов доступна только в редакторе"));
#endif

	return Results;
}

int32 UKitsuneRockMaterialLibrary::SyncRockMasterSamplerTypes(const FString& AssetFolder, bool bSaveAssets)
{
#if WITH_EDITOR
	const FString Folder = AssetFolder.IsEmpty() ? KitsuneRockMaterial::DefaultAssetFolder() : AssetFolder;
	UMaterial* Master = GetOrCreateMasterMaterialInternal(Folder, false);
	if (!Master)
	{
		UE_LOG(LogKitsuneRockAssets, Error, TEXT("Не найден master-материал в %s"), *Folder);
		return -1;
	}

	const int32 Changed = SyncMasterSamplerTypes(Master, Folder, TMap<FName, UTexture2D*>());
	if (Changed > 0 && bSaveAssets)
	{
		SaveAssetToDisk(Master);
	}

	UE_LOG(LogKitsuneRockAssets, Log, TEXT("%s: исправлено узлов %d"), *Master->GetName(), Changed);
	return Changed;
#else
	UE_LOG(LogKitsuneRockAssets, Warning, TEXT("Починка типов сэмплеров доступна только в редакторе"));
	return -1;
#endif
}

int32 UKitsuneRockMaterialLibrary::UpgradeRockMasterAntiTiling(const FString& AssetFolder, bool bSaveAssets)
{
#if WITH_EDITOR
	const FString Folder = AssetFolder.IsEmpty() ? KitsuneRockMaterial::DefaultAssetFolder() : AssetFolder;
	UMaterial* Master = GetOrCreateMasterMaterialInternal(Folder, false);
	if (!Master)
	{
		UE_LOG(LogKitsuneRockAssets, Error, TEXT("Не найден master-материал в %s"), *Folder);
		return -1;
	}

	const int32 Added = UpgradeMasterAntiTiling(Master, Folder, bSaveAssets);
	const int32 Tuned = TuneRockInstancesAntiTiling(Master, Folder, bSaveAssets);

	UE_LOG(LogKitsuneRockAssets, Log, TEXT("%s: узлов анти-тайлинга добавлено %d (0 = каскад уже был), инстансов настроено заново %d"), *Master->GetName(), Added, Tuned);
	return Added;
#else
	UE_LOG(LogKitsuneRockAssets, Warning, TEXT("Правка графа master-материала доступна только в редакторе"));
	return -1;
#endif
}

void UKitsuneRockMaterialLibrary::RandomizeRockInstanceParameters(UMaterialInstanceDynamic* Instance, int32 Seed)
{
	if (!Instance)
	{
		UE_LOG(LogKitsuneRockAssets, Warning, TEXT("RandomizeRockInstanceParameters: инстанс не передан"));
		return;
	}

	// Разные стены и комнаты одного подземелья должны смотреть в разные места карты.
	// Работает и в игре: values идут в динамический инстанс (MID).
	FRandomStream Stream(Seed);

	Instance->SetScalarParameterValue(KitsuneRockMaterial::UVOffsetX(), Stream.GetFraction());
	Instance->SetScalarParameterValue(KitsuneRockMaterial::UVOffsetY(), Stream.GetFraction());
	// Диапазоны заведомо безопасные: при TextureTileSize = 300 максимальные
	// значения дают WarpAmount * WarpScale * TextureTileSize = 0.012 вместо прежних 0.21,
	// при которых по стенам шли штрихи.
	Instance->SetScalarParameterValue(KitsuneRockMaterial::WarpAmount(), 0.012f + Stream.GetFraction() * 0.018f);
	Instance->SetScalarParameterValue(KitsuneRockMaterial::WarpScale(), 0.0010f + Stream.GetFraction() * 0.0012f);
	Instance->SetScalarParameterValue(KitsuneRockMaterial::DetailMix(), 0.10f + Stream.GetFraction() * 0.20f);
	Instance->SetScalarParameterValue(KitsuneRockMaterial::NormalStrength(), 0.90f + Stream.GetFraction() * 0.40f);
	// Лёгкий сдвиг оттенка: H,S,V в байтах (H 0..255 = полный круг), saturation
	// маленькая, чтобы камень не становился цветным.
	Instance->SetVectorParameterValue(KitsuneRockMaterial::Tint(),
		FLinearColor::MakeFromHSV8(
			(uint8)(Stream.GetFraction() * 255.0f),
			(uint8)((0.04f + Stream.GetFraction() * 0.08f) * 255.0f),
			(uint8)((0.88f + Stream.GetFraction() * 0.12f) * 255.0f)));
}
