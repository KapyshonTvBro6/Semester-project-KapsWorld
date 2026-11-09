// KitsuneRockParams.cpp
// Значения пресетов списком с веб-страницы генератора (PRESETS в <script>),
// цвета переводятся из sRGB-хекса в FLinearColor, чтобы их показывал цветовый пикер.
#include "KitsuneRockParams.h"

namespace
{
	// sRGB-хекс веб-версии -> FLinearColor (линейные значения для пикера).
	inline FLinearColor SRGBHex(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}

	// Один пресет: слайдеры + цвета, порядок как на странице.
	struct FKitsuneRockPresetData
	{
		EKitsuneRockPreset Preset;
		const TCHAR* Name;
		const TCHAR* Label;

		float Scale;
		int32 Octaves;
		float Relief;
		float Cracks;
		float Strata;
		float Moisture;
		float Moss;
		float Roughness;
		float Brick;
		float Veins;

		// Цвета шкалы: тёмный -> светлый.
		uint8 C0[3];
		uint8 C1[3];
		uint8 C2[3];
		uint8 C3[3];
		uint8 MossRGB[3];
		uint8 VeinRGB[3];
	};

	static const FKitsuneRockPresetData GKitsuneRockPresets[] =
	{
		{ EKitsuneRockPreset::Granite, TEXT("granite"), TEXT("Гранит"),
		  6.5f, 7, 0.62f, 0.28f, 0.12f, 0.08f, 0.05f, 0.72f, 0.00f, 0.00f,
		  {0x22,0x1f,0x1d}, {0x4c,0x47,0x43}, {0x7c,0x74,0x6c}, {0xb7,0xac,0xa1},
		  {0x4d,0x70,0x38}, {0xd8,0xd8,0xd8} },

		{ EKitsuneRockPreset::Basalt, TEXT("basalt"), TEXT("Базальт"),
		  5.2f, 7, 0.78f, 0.72f, 0.18f, 0.12f, 0.02f, 0.64f, 0.00f, 0.00f,
		  {0x0c,0x0d,0x10}, {0x22,0x25,0x2a}, {0x41,0x46,0x4d}, {0x70,0x77,0x80},
		  {0x31,0x50,0x2b}, {0x2b,0x2f,0x36} },

		{ EKitsuneRockPreset::Sandstone, TEXT("sandstone"), TEXT("Пещерный песчаник"),
		  7.2f, 6, 0.48f, 0.22f, 0.72f, 0.05f, 0.03f, 0.82f, 0.00f, 0.00f,
		  {0x3d,0x28,0x17}, {0x7a,0x4d,0x27}, {0xc0,0x8d,0x52}, {0xec,0xd0,0xa1},
		  {0x61,0x6b,0x2f}, {0xa6,0x7c,0x4f} },

		{ EKitsuneRockPreset::MossyCave, TEXT("mossyCave"), TEXT("Мшистая пещера"),
		  5.8f, 7, 0.70f, 0.45f, 0.20f, 0.55f, 0.85f, 0.83f, 0.00f, 0.00f,
		  {0x15,0x1a,0x12}, {0x2c,0x35,0x24}, {0x55,0x60,0x43}, {0x8d,0x98,0x72},
		  {0x3f,0x70,0x30}, {0x7b,0xa1,0x7d} },

		{ EKitsuneRockPreset::WetCave, TEXT("wetCave"), TEXT("Мокрая пещера"),
		  4.7f, 7, 0.85f, 0.60f, 0.25f, 0.95f, 0.15f, 0.35f, 0.00f, 0.00f,
		  {0x0d,0x14,0x18}, {0x20,0x31,0x3a}, {0x3f,0x56,0x5f}, {0x71,0x88,0x8f},
		  {0x28,0x4a,0x2a}, {0x8f,0xd8,0xff} },

		{ EKitsuneRockPreset::DungeonBricks, TEXT("dungeonBricks"), TEXT("Кирпичный данж"),
		  5.0f, 6, 0.55f, 0.35f, 0.05f, 0.18f, 0.15f, 0.78f, 0.95f, 0.00f,
		  {0x24,0x1f,0x1b}, {0x4c,0x42,0x38}, {0x77,0x68,0x5a}, {0xa5,0x94,0x82},
		  {0x4a,0x6b,0x34}, {0xc8,0xb0,0x8a} },

		{ EKitsuneRockPreset::LavaVeins, TEXT("lavaVeins"), TEXT("Лавовые жилы"),
		  5.5f, 7, 0.80f, 0.85f, 0.18f, 0.00f, 0.00f, 0.58f, 0.00f, 0.90f,
		  {0x09,0x07,0x08}, {0x24,0x1d,0x1c}, {0x4a,0x3a,0x35}, {0x7c,0x65,0x5c},
		  {0x3b,0x2b,0x20}, {0xff,0x5a,0x1f} },

		{ EKitsuneRockPreset::CrystalCave, TEXT("crystalCave"), TEXT("Кристальная пещера"),
		  5.0f, 7, 0.72f, 0.55f, 0.12f, 0.35f, 0.05f, 0.42f, 0.00f, 0.75f,
		  {0x12,0x10,0x1d}, {0x27,0x22,0x3b}, {0x4c,0x41,0x66}, {0x8b,0x7f,0xae},
		  {0x33,0x4c,0x44}, {0x74,0xe6,0xff} }
	};

	const FKitsuneRockPresetData* FindPresetData(EKitsuneRockPreset Preset)
	{
		for (const FKitsuneRockPresetData& Data : GKitsuneRockPresets)
		{
			if (Data.Preset == Preset)
			{
				return &Data;
			}
		}
		return &GKitsuneRockPresets[0];
	}
}

void FKitsuneRockParams::ApplyPreset(EKitsuneRockPreset Preset)
{
	const FKitsuneRockPresetData& D = *FindPresetData(Preset);

	Scale      = D.Scale;
	Octaves    = D.Octaves;
	Relief     = D.Relief;
	Cracks     = D.Cracks;
	Strata     = D.Strata;
	Moisture   = D.Moisture;
	Moss       = D.Moss;
	Roughness  = D.Roughness;
	Brick      = D.Brick;
	Veins      = D.Veins;

	ColorDark   = SRGBHex(D.C0[0], D.C0[1], D.C0[2]);
	ColorMid    = SRGBHex(D.C1[0], D.C1[1], D.C1[2]);
	ColorLight  = SRGBHex(D.C2[0], D.C2[1], D.C2[2]);
	ColorBright = SRGBHex(D.C3[0], D.C3[1], D.C3[2]);
	MossColor   = SRGBHex(D.MossRGB[0], D.MossRGB[1], D.MossRGB[2]);
	VeinColor   = SRGBHex(D.VeinRGB[0], D.VeinRGB[1], D.VeinRGB[2]);

	Sanitize();
}

void FKitsuneRockParams::Sanitize()
{
	Size  = GetValidSize();
	Scale = FMath::Clamp(Scale, 2.0f, 16.0f);
	Octaves = FMath::Clamp(Octaves, 1, 8);
	Relief   = FMath::Clamp(Relief, 0.0f, 1.0f);
	Cracks   = FMath::Clamp(Cracks, 0.0f, 1.0f);
	Strata   = FMath::Clamp(Strata, 0.0f, 1.0f);
	Moisture = FMath::Clamp(Moisture, 0.0f, 1.0f);
	Moss     = FMath::Clamp(Moss, 0.0f, 1.0f);
	Roughness = FMath::Clamp(Roughness, 0.0f, 1.0f);
	Brick    = FMath::Clamp(Brick, 0.0f, 1.0f);
	Veins    = FMath::Clamp(Veins, 0.0f, 1.0f);
	NormalStrength = FMath::Clamp(NormalStrength, 0.0f, 8.0f);
	AOStrength = FMath::Clamp(AOStrength, 0.0f, 1.0f);
}

int32 FKitsuneRockParams::GetValidSize() const
{
	// Ближайшая степень 2 в диапазоне 128..2048.
	int32 S = FMath::Max(128, FMath::Min(Size, 2048));
	int32 Pow = 128;
	while (Pow < S && Pow < 2048)
	{
		Pow *= 2;
	}
	return Pow;
}

void FKitsuneRockParams::GetRampSRGB(FColor OutColors[4]) const
{
	OutColors[0] = ColorDark.ToFColor(true);
	OutColors[1] = ColorMid.ToFColor(true);
	OutColors[2] = ColorLight.ToFColor(true);
	OutColors[3] = ColorBright.ToFColor(true);
}

FKitsuneRockParams MakeKitsuneRockPreset(EKitsuneRockPreset Preset)
{
	FKitsuneRockParams Params;
	Params.ApplyPreset(Preset);
	return Params;
}

FString GetKitsuneRockPresetName(EKitsuneRockPreset Preset)
{
	return FString(FindPresetData(Preset)->Name);
}

FString GetKitsuneRockPresetLabel(EKitsuneRockPreset Preset)
{
	return FString(FindPresetData(Preset)->Label);
}

bool FindKitsuneRockPreset(const FString& Name, EKitsuneRockPreset& OutPreset)
{
	for (const FKitsuneRockPresetData& Data : GKitsuneRockPresets)
	{
		if (Name.Equals(Data.Name, ESearchCase::IgnoreCase))
		{
			OutPreset = Data.Preset;
			return true;
		}
	}
	return false;
}

const TArray<EKitsuneRockPreset>& GetKitsuneRockPresets()
{
	static TArray<EKitsuneRockPreset> Presets;
	if (Presets.Num() == 0)
	{
		for (const FKitsuneRockPresetData& Data : GKitsuneRockPresets)
		{
			Presets.Add(Data.Preset);
		}
	}
	return Presets;
}
