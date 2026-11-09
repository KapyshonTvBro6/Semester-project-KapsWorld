// KitsuneRockField.cpp
// Точный порт генератора из веб-страницы «Кицунэ Юки».
// Порядок операций, коэффициенты и даже порядок смешивания цветов совпадают
// с JS-версией, поэтому при том же Seed картинка получается та же.
//
// 1) Высотное поле: fbm + ридж-шум (трещины) + слоистость + кирпичи.
// 2) Размытие высот (separable box blur с оборотом) — источник AO.
// 3) Нормали из производных высот, AO из «впадин», влажные/мшистые маски,
//    цвет по шкале из четырёх цветов, шероховатость.
#include "KitsuneRockField.h"

#include "Async/ParallelFor.h"
#include "SemesterKapsWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogKitsuneRock, Log, All);

namespace
{
	// ---------- helpers из веб-версии ----------

	inline double Clamp01(double X)
	{
		return X < 0.0 ? 0.0 : (X > 1.0 ? 1.0 : X);
	}

	inline double Mix(double A, double B, double T)
	{
		return A + (B - A) * T;
	}

	inline int32 ModI(int32 N, int32 M)
	{
		int32 R = N % M;
		return R < 0 ? R + M : R;
	}

	inline double Mod1(double X)
	{
		return X - FMath::FloorToDouble(X);
	}

	inline double SmoothStep(double A, double B, double X)
	{
		if (B == A)
		{
			return X < A ? 0.0 : 1.0;
		}
		const double T = Clamp01((X - A) / (B - A));
		return T * T * (3.0 - 2.0 * T);
	}

	/** Math.imul-хеш (32 бита) -> [0,1). Тот же, что hash2() в веб-версии. */
	inline double Hash2(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = (uint32)X * 0x27d4eb2du ^ (uint32)Y * 0x165667b1u ^ (uint32)Seed * 0x9e3779b9u;
		H = (H ^ (H >> 15)) * 0x85ebca6bu;
		H ^= H >> 13;
		H *= 0xc2b2ae35u;
		H ^= H >> 16;
		return (double)H / 4294967296.0;
	}

	/** Value-noise с периодическим оборотом по period -> бесшовность карты. */
	double ValueNoise(double X, double Y, int32 Period, int32 Seed)
	{
		const int32 Xi = FMath::FloorToDouble(X);
		const int32 Yi = FMath::FloorToDouble(Y);
		const double Xf = X - (double)Xi;
		const double Yf = Y - (double)Yi;

		const double Sx = Xf * Xf * (3.0 - 2.0 * Xf);
		const double Sy = Yf * Yf * (3.0 - 2.0 * Yf);

		const int32 X0 = ModI(Xi, Period);
		const int32 X1 = ModI(Xi + 1, Period);
		const int32 Y0 = ModI(Yi, Period);
		const int32 Y1 = ModI(Yi + 1, Period);

		const double V00 = Hash2(X0, Y0, Seed);
		const double V10 = Hash2(X1, Y0, Seed);
		const double V01 = Hash2(X0, Y1, Seed);
		const double V11 = Hash2(X1, Y1, Seed);

		return Mix(Mix(V00, V10, Sx), Mix(V01, V11, Sx), Sy);
	}

	double FBM(double U, double V, double Cells, int32 Octaves, int32 Seed,
		bool bRidged = false, double Persistence = 0.5, double Lacunarity = 2.0)
	{
		U = Mod1(U);
		V = Mod1(V);

		double Amp = 1.0;
		double Sum = 0.0;
		double Norm = 0.0;
		double Freq = 1.0;

		for (int32 i = 0; i < Octaves; ++i)
		{
			const int32 Period = FMath::Max(2, FMath::RoundToInt(Cells * Freq));
			double n = ValueNoise(U * Period, V * Period, Period, Seed + i * 131);

			if (bRidged)
			{
				n = 1.0 - FMath::Abs(n * 2.0 - 1.0);
				n = n * n;
			}

			Sum += n * Amp;
			Norm += Amp;
			Amp *= Persistence;
			Freq *= Lacunarity;
		}

		return Sum / (Norm > 0.0 ? Norm : 1.0);
	}

	struct FBrickInfo
	{
		double Height = 0.0;
		double Tint = 0.0;
		double Inside = 0.0;
	};

	FBrickInfo BrickInfo(double U, double V, const FKitsuneRockParams& P, int32 Seed)
	{
		const int32 Cols = FMath::Max(3, FMath::RoundToInt(P.Scale));
		const int32 Rows = Cols * 2;

		const int32 Row = FMath::FloorToDouble(V * Rows);
		const double RowFrac = V * Rows - (double)Row;
		const double Offset = (Row % 2) ? 0.5 / (double)Cols : 0.0;

		const double Uu = Mod1(U + Offset);
		const int32 Col = FMath::FloorToDouble(Uu * Cols);
		const double ColFrac = Uu * Cols - (double)Col;

		const double Id = Hash2(Col, Row, Seed + 777);

		const double EdgeX = FMath::Min(ColFrac, 1.0 - ColFrac);
		const double EdgeY = FMath::Min(RowFrac, 1.0 - RowFrac);
		const double Edge = FMath::Min(EdgeX, EdgeY);

		const double MortarW = 0.055 + P.Cracks * 0.035;
		const double Inside = SmoothStep(MortarW, MortarW + 0.04, Edge);
		const double Bevel = SmoothStep(0.0, 0.18, Edge);

		const double H = (0.16 + 0.62 * Id) * (0.32 + 0.68 * Inside) * (0.62 + 0.38 * Bevel);

		FBrickInfo Out;
		Out.Height = H;
		Out.Tint = Id;
		Out.Inside = Inside;
		return Out;
	}

	/** Разворачиваемое box-blur в два прохода (x, затем y). Аналог boxBlur(). */
	void BoxBlur(const TArray<float>& Src, int32 Size, int32 Radius, TArray<float>& OutDst)
	{
		const int32 N = Size * Size;
		OutDst.Init(0.0f, N);

		if (Radius <= 0)
		{
			OutDst = Src;
			return;
		}

		TArray<float> Tmp;
		Tmp.Init(0.0f, N);

		const double Div = (double)(Radius * 2 + 1);

		for (int32 y = 0; y < Size; ++y)
		{
			const int32 Row = y * Size;
			double Sum = 0.0;

			for (int32 x = -Radius; x <= Radius; ++x)
			{
				Sum += (double)Src[Row + ModI(x, Size)];
			}

			for (int32 x = 0; x < Size; ++x)
			{
				Tmp[Row + x] = (float)(Sum / Div);
				const int32 AddX = ModI(x + Radius + 1, Size);
				const int32 RemX = ModI(x - Radius, Size);
				Sum += (double)Src[Row + AddX] - (double)Src[Row + RemX];
			}
		}

		for (int32 x = 0; x < Size; ++x)
		{
			double Sum = 0.0;

			for (int32 y = -Radius; y <= Radius; ++y)
			{
				Sum += (double)Tmp[ModI(y, Size) * Size + x];
			}

			for (int32 y = 0; y < Size; ++y)
			{
				OutDst[y * Size + x] = (float)(Sum / Div);
				const int32 AddY = ModI(y + Radius + 1, Size);
				const int32 RemY = ModI(y - Radius, Size);
				Sum += (double)Tmp[AddY * Size + x] - (double)Tmp[RemY * Size + x];
			}
		}
	}

	/** Округление в байт как у Uint8ClampedArray (clamping + round). */
	inline uint8 QuantByte(double V)
	{
		if (!(V > 0.0))
		{
			return 0;
		}
		if (V > 255.0)
		{
			return 255;
		}
		return (uint8)FMath::Clamp(FMath::RoundToDouble(V), 0.0, 255.0);
	}

	inline FColor MakeRGB(double R, double G, double B, double A = 255.0)
	{
		// FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA) — каналы идут по порядку R, G, B.
	return FColor(QuantByte(R), QuantByte(G), QuantByte(B), QuantByte(A));
	}

	/**
	 * Строка пикселей FColor (row-major) из массива float-значений 0..1,
	 * для grayscale-карт.
	 */
	inline FColor MakeGray(double V)
	{
		const uint8 Q = QuantByte(Clamp01(V) * 255.0);
		return FColor(Q, Q, Q, 255);
	}
}

void GenerateKitsuneRockMaps(const FKitsuneRockParams& InParams, FKitsuneRockMaps& OutMaps)
{
	FKitsuneRockParams P = InParams;
	P.Sanitize();

	const int32 Size = P.GetValidSize();
	const int32 N = Size * Size;

	TArray<float> Height, BaseN, PatchN, CrackMask, Blur;
	TArray<float> BrickTint, BrickMask;

	Height.Init(0.0f, N);
	BaseN.Init(0.0f, N);
	PatchN.Init(0.0f, N);
	CrackMask.Init(0.0f, N);

	const bool bBrick = P.Brick > 0.001;
	if (bBrick)
	{
		BrickTint.Init(0.0f, N);
		BrickMask.Init(0.0f, N);
	}

	// ---------- 1. Высоты ----------
	ParallelFor(Size, [&](int32 y)
	{
		const int32 Row = y * Size;
		const double v = (double)y / (double)Size;

		for (int32 x = 0; x < Size; ++x)
		{
			const int32 i = Row + x;
			const double u = (double)x / (double)Size;

			const double n   = FBM(u, v, P.Scale, P.Octaves, P.Seed, false, 0.5, 2.0);
			const double rid = FBM(u, v, P.Scale * 2.15, FMath::Max(3, P.Octaves - 1), P.Seed + 101, true, 0.55, 2.0);
			const double patch = FBM(u + 0.37, v + 0.71, P.Scale * 3.35, 3, P.Seed + 999, false, 0.5, 2.0);

			double h = n;

			h += (rid - 0.5) * 0.20 * P.Relief;

			if (P.Strata > 0.001)
			{
				const int32 Freq = FMath::Max(1, FMath::RoundToInt(P.Scale * 1.35));
				const double s = 0.5 + 0.5 * FMath::Sin((v * Freq + (n - 0.5) * 2.2) * PI * 2.0);
				h += (FMath::Pow(s, 1.8) - 0.5) * P.Strata * 0.25;
			}

			const double CrackExp = 4.0 - P.Cracks * 3.2;
			double crack = FMath::Pow(Clamp01(rid), CrackExp) * P.Cracks;
			h -= crack * 0.35 * (0.3 + P.Relief);

			const double pit = (1.0 - SmoothStep(0.18, 0.44, n)) * 0.18 * P.Relief;
			h -= pit;

			if (bBrick)
			{
				const double du = (n - 0.5) * 0.02;
				const double dv = (patch - 0.5) * 0.02;
				const FBrickInfo bi = BrickInfo(Mod1(u + du), Mod1(v + dv), P, P.Seed);

				const double bh = Clamp01(bi.Height * 0.72 + n * 0.18 + patch * 0.10);
				h = Mix(h, bh, (double)P.Brick);

				crack = FMath::Max(crack, (1.0 - bi.Inside) * (double)P.Brick * 0.55);

				BrickTint[i] = (float)bi.Tint;
				BrickMask[i] = (float)bi.Inside;
			}

			CrackMask[i] = (float)Clamp01(crack * 1.6);

			h = Clamp01(0.5 + (h - 0.5) * (0.6 + (double)P.Relief * 0.8));

			Height[i] = (float)h;
			BaseN[i]  = (float)n;
			PatchN[i] = (float)patch;
		}
	});

	// ---------- 2. Размытие высот (источник AO и «впадин») ----------
	const int32 BlurRadius = FMath::Max(1, FMath::RoundToInt((float)Size / 256.0f));
	BoxBlur(Height, Size, BlurRadius, Blur);

	// ---------- 3. Нормали и PBR-карты ----------
	FColor Ramp[4];
	P.GetRampSRGB(Ramp);
	const FColor MossRGB = P.MossColor.ToFColor(true);
	const FColor VeinRGB = P.VeinColor.ToFColor(true);

	OutMaps.Size = Size;
	OutMaps.BaseColor.Init(FColor(0, 0, 0, 255), N);
	OutMaps.Normal.Init(FColor(128, 128, 255, 255), N);
	OutMaps.Roughness.Init(FColor(128, 128, 128, 255), N);
	OutMaps.AO.Init(FColor(255, 255, 255, 255), N);
	OutMaps.Height.Init(FColor(0, 0, 0, 255), N);
	OutMaps.EmissiveMask.Init(FColor(0, 0, 0, 255), N);

	ParallelFor(Size, [&](int32 y)
	{
		const int32 Row = y * Size;
		const int32 RowUp   = (y > 0 ? y - 1 : Size - 1) * Size;
		const int32 RowDown = (y < Size - 1 ? y + 1 : 0) * Size;

		for (int32 x = 0; x < Size; ++x)
		{
			const int32 i = Row + x;

			const int32 xLeft  = x > 0 ? x - 1 : Size - 1;
			const int32 xRight = x < Size - 1 ? x + 1 : 0;

			const double Left  = Height[Row + xLeft];
			const double Right = Height[Row + xRight];
			const double Up    = Height[RowUp + x];
			const double Down  = Height[RowDown + x];

			double nx = (Left - Right) * (double)P.NormalStrength;
			double ny = (Down - Up) * (double)P.NormalStrength;

			if (P.bInvertNormalY)
			{
				ny = -ny;
			}

			const double nz = 1.0;
			const double InvLen = 1.0 / FMath::Sqrt(nx * nx + ny * ny + nz * nz);

			nx *= InvLen;
			ny *= InvLen;
			const double nzz = nz * InvLen;

			// Как в canvas: (n * 0.5 + 0.5) * 255.
			OutMaps.Normal[i] = MakeRGB(
				(nx * 0.5 + 0.5) * 255.0,
				(ny * 0.5 + 0.5) * 255.0,
				(nzz * 0.5 + 0.5) * 255.0);

			const double valley = Clamp01(((double)Blur[i] - (double)Height[i]) * (2.0 + (double)P.AOStrength * 10.0));
			const double ao = Clamp01(1.0 - valley * 1.25 - (double)CrackMask[i] * 0.18);
			const double flat = nzz;

			const double wet = Clamp01(
				(double)P.Moisture *
				(0.15 + valley * 1.2 + (double)CrackMask[i] * 0.45) *
				(0.35 + (double)PatchN[i] * 0.65));

			const double moss = Clamp01(
				(double)P.Moss *
				(0.20 + valley * 1.1 + (double)PatchN[i] * 0.55) *
				(0.30 + flat * 0.70) *
				(0.35 + (double)BaseN[i] * 0.65));

			const double veinRaw = (double)P.Veins * (double)CrackMask[i] * (0.40 + (double)PatchN[i] * 0.90);
			const double vein = SmoothStep(0.12, 0.80, veinRaw);

			double t = (double)Height[i] * 0.68 + (double)BaseN[i] * 0.32;

			if (bBrick)
			{
				t = Mix(t, (double)BrickTint[i], (double)P.Brick * (double)BrickMask[i] * 0.82);
			}

			t = Clamp01(t);

			const double PalPos = t * (double)(4 - 1);
			int32 pi = FMath::FloorToDouble(PalPos);
			double pf = PalPos - (double)pi;

			if (pi >= 3)
			{
				pi = 3;
				pf = 0.0;
			}

			const FColor& C0 = Ramp[pi];
			const FColor& C1 = Ramp[FMath::Min(pi + 1, 3)];

			// Цвета шкалы лежат в sRGB-байтах, смешиваем в них же, как в canvas.
			double rr = (double)C0.R + ((double)C1.R - (double)C0.R) * pf;
			double gg = (double)C0.G + ((double)C1.G - (double)C0.G) * pf;
			double bb = (double)C0.B + ((double)C1.B - (double)C0.B) * pf;

			const double speck = ((double)PatchN[i] - 0.5) * 18.0;
			rr += speck;
			gg += speck;
			bb += speck;

			const double shade = 0.35 + 0.65 * ao;
			rr *= shade;
			gg *= shade;
			bb *= shade;

			if (wet > 0.0)
			{
				const double dark = 1.0 - wet * 0.52;
				rr *= dark;
				gg *= dark;
				bb *= dark;
				bb += wet * 12.0;
			}

			if (moss > 0.0)
			{
				rr = Mix(rr, (double)MossRGB.R, moss);
				gg = Mix(gg, (double)MossRGB.G, moss);
				bb = Mix(bb, (double)MossRGB.B, moss);
			}

			if (vein > 0.0)
			{
				rr = Mix(rr, (double)VeinRGB.R, vein);
				gg = Mix(gg, (double)VeinRGB.G, vein);
				bb = Mix(bb, (double)VeinRGB.B, vein);
			}

			OutMaps.BaseColor[i] = MakeRGB(rr, gg, bb);

			double rough = (double)P.Roughness + ((double)BaseN[i] - 0.5) * 0.25 + ((double)Height[i] - 0.5) * 0.15;

			if (bBrick)
			{
				rough = Mix(rough, 0.86, (1.0 - (double)BrickMask[i]) * (double)P.Brick);
			}

			rough = Mix(rough, 0.95, moss);
			rough = Mix(rough, 0.08, wet * 0.90);
			rough = Mix(rough, 0.22, vein);
			rough = Clamp01(rough);

			OutMaps.Roughness[i]    = MakeGray(rough);
			OutMaps.AO[i]           = MakeGray(ao);
			OutMaps.Height[i]       = MakeGray((double)Height[i]);
			OutMaps.EmissiveMask[i] = MakeGray(vein);
		}
	});

	UE_LOG(LogKitsuneRock, Verbose, TEXT("Карты посчитаны: %d x %d, seed=%d"), Size, Size, P.Seed);
}
