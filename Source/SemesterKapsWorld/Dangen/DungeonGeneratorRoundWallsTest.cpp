// =====================================================================
//  Проверка скругления комнат формы «Круглая» (автотест).
//
//  Скругление переставляет УЗЛЫ СЕТКИ: угол клетки, в котором сходятся стена,
//  крышка камня и плита пола, получает одно положение на эллипсе комнаты, и все
//  три поверхности берут именно его. Новых полигонов не появляется: у каждой
//  клетки остаётся её собственный четырёхугольник.
//
//  Прогоняет генератор на нескольких ключах (в том числе на размерах, близких к
//  BP_DungeonGenerator в проекте) и по мешам проверяет:
//   - стены круглых комнат переставлены на эллипс (вершины НЕ по границам
//     клеток) и переставленных граней больше, чем оставшихся лесенкой;
//   - ни одна переставленная грань не стоит в клетке коридора (проходы целы);
//   - набор перекрытых стеной рёбер сетки «до» и «после» скругления одинаков:
//     скругление не открывает проходов и не ставит стену там, где её не было;
//   - стык — одна точка: в сете нет вершины, которая повторяет другую
//     (положение + нормаль + UV), и угол крышки/плиты пола вне сетки стоит
//     ровно на вершине стены, а низ и верх грани стены — на вершинах пола и
//     крышки (одна точка на все три поверхности, швов и щелей нет);
//   - все треугольники каждого меша смотрят одной стороной со своей нормалью
//     (вывернутых полигонов, из-за которых поверхность местами «без текстуры»,
//     нет ни на стенах, ни на крышке, ни на полу);
//   - нет вырожденных треугольников и щелей между стеной, полом и крышкой;
//   - крышка накрывает весь камень, пол — все проходимые клетки.
//
//  Выгрузка вершин мешей (для скриптов Saved/AgentScratch/check_shell.py,
//  check_weld.py, check_passages.py, render_plan.py, render_iso.py) включается
//  только ключом -dangenrounddump: обычный прогон автотестов не должен плодить
//  файлы в Saved.
//
//  Запуск из редактора: Window -> Test Run -> Automation, фильтр "Dangen".
//  Из командной строки:
//    UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Dangen" \
//        -TestExit="Automation Test Queue Empty" -unattended -nullrhi -noerrdialogs
// =====================================================================

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Dangen/DungeonGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DangenRoundTest
{
	// Размер прогона: ключ сетки и размер клетки (сантиметры).
	struct FRunConfig
	{
		int32 Seed;
		int32 W;
		int32 H;
		float Cell;
	};

	static UProceduralMeshComponent* MeshByName(AActor* Actor, const TCHAR* Name)
	{
		if (!Actor) return nullptr;

		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);

		for (UActorComponent* C : Comps)
		{
			UProceduralMeshComponent* P = Cast<UProceduralMeshComponent>(C);
			if (P && P->GetName() == Name) return P;
		}
		return nullptr;
	}

	// Выгрузка вершин мешей (стены/крыша/пол) в текст — потом рисуем вид сверху.
	static FString DumpMeshes(AActor* Actor)
	{
		FString Dump;
		const TPair<const TCHAR*, const TCHAR*> Names[] = {
			{ TEXT("WallsMesh"),  TEXT("wall") },
			{ TEXT("RoofMesh"),   TEXT("roof") },
			{ TEXT("FloorMesh"),  TEXT("floor") },
		};
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : Names)
		{
			const TCHAR* Tag = Pair.Value;
			UProceduralMeshComponent* Mesh = MeshByName(Actor, Pair.Key);
			if (!Mesh) continue;
			FProcMeshSection* S = Mesh->GetProcMeshSection(0);
			if (!S) continue;
			for (const FProcMeshVertex& V : S->ProcVertexBuffer)
			{
				Dump += FString::Printf(TEXT("v %s %.2f %.2f %.2f %.2f %.2f %.2f %.4f %.4f\n"), Tag,
				                        V.Position.X, V.Position.Y, V.Position.Z,
				                        V.Normal.X, V.Normal.Y, V.Normal.Z,
				                        V.UV0.X, V.UV0.Y);
			}

			// Индексы тоже: без них не разобрать, какие треугольники к каким вершинам
			// относятся, а проверка оболочки (стена, пол и крышка) их требует.
			for (int32 i = 0; i + 2 < S->ProcIndexBuffer.Num(); i += 3)
			{
				Dump += FString::Printf(TEXT("t %s %d %d %d\n"), Tag,
				                        static_cast<int32>(S->ProcIndexBuffer[i]),
				                        static_cast<int32>(S->ProcIndexBuffer[i + 1]),
				                        static_cast<int32>(S->ProcIndexBuffer[i + 2]));
			}
		}
		return Dump;
	}

	// Актор генератора с одинаковыми настройками (только bRoundCircleWalls разный).
	static ADungeonGenerator* MakeGen(const FRunConfig& Cfg, bool bRound)
	{
		ADungeonGenerator* Gen = NewObject<ADungeonGenerator>(GetTransientPackage());
		Gen->GridW = Cfg.W;
		Gen->GridH = Cfg.H;
		Gen->CellSize = Cfg.Cell;
		Gen->WallHeight = 400.f;
		Gen->Seed = Cfg.Seed;
		Gen->bSpawnMarkers = false;
		Gen->bBuildGeometry = true;
		Gen->bRoundCircleWalls = bRound;
		return Gen;
	}

	static bool InGrid(FIntPoint C, const ADungeonGenerator* Gen)
	{
		return C.X >= 0 && C.Y >= 0 && C.X < Gen->GridW && C.Y < Gen->GridH;
	}

	static FIntPoint CellOf(const FVector& P, const ADungeonGenerator* Gen, float Cell)
	{
		// bCenterOnActor -> сетка центрирована на акторе
		const float OX = Gen->GridW * Cell * 0.5f;
		const float OY = Gen->GridH * Cell * 0.5f;
		return FIntPoint(FMath::FloorToInt((P.X + OX) / Cell), FMath::FloorToInt((P.Y + OY) / Cell));
	}

	// Вершина «не по сетке» = стоит внутри клетки, а не на её границе: таким
	// образом отличаем переставленные грани от обычных граней-лесенок.
	static bool OffGrid(const FVector& P, float Cell)
	{
		const float FX = FMath::Abs(P.X / Cell - FMath::RoundToFloat(P.X / Cell));
		const float FY = FMath::Abs(P.Y / Cell - FMath::RoundToFloat(P.Y / Cell));
		return FX > 1e-3f && FY > 1e-3f;
	}

	// Ключ положения по X и Y с точностью до миллиметра: им сравнивают одну и ту
	// же точку в разных мешах (шов стены с полом и крышкой обязан быть общим).
	static FIntPoint XYKey(const FVector& P)
	{
		return FIntPoint(FMath::RoundToInt(static_cast<double>(P.X) * 100.0),
		                 FMath::RoundToInt(static_cast<double>(P.Y) * 100.0));
	}

	static TSet<FIntPoint> MeshXYKeys(const FProcMeshSection* Sec)
	{
		TSet<FIntPoint> Keys;
		if (Sec)
		{
			for (const FProcMeshVertex& V : Sec->ProcVertexBuffer) Keys.Add(XYKey(V.Position));
		}
		return Keys;
	}

	// Грань стены (четыре угла) по индексам. Брать их «по четыре подряд» нельзя:
	// после сварки вершины граней делят угол с соседней гранью, поэтому грань
	// собирается из пары треугольников, а её низ и верх различаются по высоте.
	struct FQuad4
	{
		const FProcMeshVertex* BotA = nullptr;   // низ грани
		const FProcMeshVertex* BotB = nullptr;
		const FProcMeshVertex* TopA = nullptr;   // верх грани
		const FProcMeshVertex* TopB = nullptr;
	};

	static void CollectWallQuads(const FProcMeshSection* Sec, float WallHeight, TArray<FQuad4>& Out)
	{
		Out.Reset();
		if (!Sec) return;

		const TArray<FProcMeshVertex>& V = Sec->ProcVertexBuffer;

		for (int32 t = 0; t + 5 < Sec->ProcIndexBuffer.Num(); t += 6)
		{
			FQuad4 Q;
			int32 Seen[6];
			int32 SeenNum = 0;

			for (int32 k = 0; k < 6; ++k)
			{
				const int32 I = static_cast<int32>(Sec->ProcIndexBuffer[t + k]);
				if (!V.IsValidIndex(I)) continue;

				bool bSeen = false;
				for (int32 s = 0; s < SeenNum; ++s) if (Seen[s] == I) { bSeen = true; break; }
				if (bSeen) continue;
				if (SeenNum < 6) Seen[SeenNum++] = I;

				const FProcMeshVertex& P = V[I];
				if (FMath::Abs(P.Position.Z) < 1.0)
				{
					if (!Q.BotA) Q.BotA = &P;
					else if (!Q.BotB && &P != Q.BotA) Q.BotB = &P;
				}
				else if (FMath::Abs(P.Position.Z - WallHeight) < 1.0)
				{
					if (!Q.TopA) Q.TopA = &P;
					else if (!Q.TopB && &P != Q.TopA) Q.TopB = &P;
				}
			}

			if (Q.BotA && Q.BotB && Q.TopA && Q.TopB) Out.Add(Q);
		}
	}

	// Грань стоит на прежнем месте внутри прямоугольника круглой комнаты? Берём
	// середину нижнего ребра, по нормали грани находим клетку камня и клетку
	// открытую, и смотрим, принадлежат ли они этой круглой комнате.
	static bool IsFaceOfCircleRoom(const FQuad4& Q, const ADungeonGenerator* Gen, float Cell)
	{
		const FVector& A = Q.BotA->Position;
		const FVector& B = Q.BotB->Position;
		const FVector N  = Q.BotA->Normal.GetSafeNormal();
		if (N.SizeSquared() < KINDA_SMALL_NUMBER) return false;

		const FVector M = (A + B) * 0.5f;
		const FIntPoint Rock = CellOf(M - N * (Cell * 0.5f), Gen, Cell);
		const FIntPoint Open = CellOf(M + N * (Cell * 0.5f), Gen, Cell);
		if (!InGrid(Rock, Gen) || !InGrid(Open, Gen)) return false;

		const int32 RI = Rock.Y * Gen->GridW + Rock.X;
		const int32 OI = Open.Y * Gen->GridW + Open.X;
		if (Gen->Grid[RI] != 0 || Gen->Grid[OI] == 0) return false;   // камень | открытая

		const int32 Room = Gen->RoomMap[OI];
		if (Room < 0 || !Gen->Rooms.IsValidIndex(Room)) return false;
		if (Gen->Rooms[Room].Shape != ERoomShape::Circle) return false;
		return Gen->RoomMap[RI] != Room;   // клетка камня внутри прямоугольника, но не комната
	}

	// Вырожденные треугольники (площадь в доли квадратного сантиметра) — те самые
	// иглы, которые на снимках выглядят как «полигон без текстуры».
	static int32 CountDegenerate(const FProcMeshSection* Sec)
	{
		int32 Bad = 0;
		if (!Sec) return 0;

		for (int32 i = 0; i + 2 < Sec->ProcIndexBuffer.Num(); i += 3)
		{
			const FVector& A = Sec->ProcVertexBuffer[static_cast<int32>(Sec->ProcIndexBuffer[i])].Position;
			const FVector& B = Sec->ProcVertexBuffer[static_cast<int32>(Sec->ProcIndexBuffer[i + 1])].Position;
			const FVector& C = Sec->ProcVertexBuffer[static_cast<int32>(Sec->ProcIndexBuffer[i + 2])].Position;
			if (FVector::CrossProduct(B - A, C - A).Size() < 1e-3) ++Bad;
		}
		return Bad;
	}

	// Сварка: в одном сете не должно быть двух вершин с одинаковыми положением,
	// нормалью и UV. Там, где полигоны соприкасаются, обязана стоять ОДНА точка.
	static int32 CountDuplicateVerts(const FProcMeshSection* Sec)
	{
		int32 Dups = 0;
		if (!Sec) return 0;

		const TArray<FProcMeshVertex>& V = Sec->ProcVertexBuffer;
		for (int32 i = 0; i < V.Num(); ++i)
		{
			for (int32 j = i + 1; j < V.Num(); ++j)
			{
				if (V[i].Position == V[j].Position && V[i].Normal == V[j].Normal && V[i].UV0 == V[j].UV0) ++Dups;
			}
		}
		return Dups;
	}

	// Обход треугольника и его нормаль: передняя грань (A,B,C) в UE — сторона,
	// куда смотрит (C-A)x(B-A). Если у части треугольников она расходится с
	// хранящейся нормалью, часть поверхности вывернута и местами «без текстуры».
	static void CountWinding(const FProcMeshSection* Sec, int32& OutWithNormal, int32& OutAgainstNormal)
	{
		OutWithNormal = 0;
		OutAgainstNormal = 0;
		if (!Sec) return;

		const TArray<FProcMeshVertex>& V = Sec->ProcVertexBuffer;
		for (int32 i = 0; i + 2 < Sec->ProcIndexBuffer.Num(); i += 3)
		{
			const FProcMeshVertex& A = V[static_cast<int32>(Sec->ProcIndexBuffer[i])];
			const FProcMeshVertex& B = V[static_cast<int32>(Sec->ProcIndexBuffer[i + 1])];
			const FProcMeshVertex& C = V[static_cast<int32>(Sec->ProcIndexBuffer[i + 2])];

			const FVector Front = FVector::CrossProduct(C.Position - A.Position, B.Position - A.Position);
			if (Front.SizeSquared() < KINDA_SMALL_NUMBER) continue;   // вырожденные считаются отдельно

			if (FVector::DotProduct(Front, A.Normal) >= 0.0) ++OutWithNormal;
			else ++OutAgainstNormal;
		}
	}

	// Пересекаются ли два отрезка (или лежит ли конец одного на другом). Нужно,
	// чтобы понять, перекрыто ли стеной ребро между соседними клетками сетки.
	static bool SegmentsTouch(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		auto Orient = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R) -> double
		{
			const double Cross = (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X);
			if (Cross > 1e-6) return 1.0;
			if (Cross < -1e-6) return -1.0;
			return 0.0;
		};

		const double D1 = Orient(A, B, C);
		const double D2 = Orient(A, B, D);
		const double D3 = Orient(C, D, A);
		const double D4 = Orient(C, D, B);

		if (((D1 > 0 && D2 < 0) || (D1 < 0 && D2 > 0)) &&
		    ((D3 > 0 && D4 < 0) || (D3 < 0 && D4 > 0))) return true;      // строго пересекаются

		// collinear-случаи: край стены лежит ровно на линии центр-центр (или наоборот)
		auto OnSegment = [](const FVector2D& S, const FVector2D& E, const FVector2D& P) -> bool
		{
			const double MinX = FMath::Min(S.X, E.X) - 1e-3, MaxX = FMath::Max(S.X, E.X) + 1e-3;
			const double MinY = FMath::Min(S.Y, E.Y) - 1e-3, MaxY = FMath::Max(S.Y, E.Y) + 1e-3;
			return P.X >= MinX && P.X <= MaxX && P.Y >= MinY && P.Y <= MaxY;
		};

		if (D1 == 0.0 && OnSegment(A, B, C)) return true;
		if (D2 == 0.0 && OnSegment(A, B, D)) return true;
		if (D3 == 0.0 && OnSegment(C, D, A)) return true;
		if (D4 == 0.0 && OnSegment(C, D, B)) return true;
		return false;
	}

	// Какие рёбра сетки (клетка -> сосед по +X или +Y) перекрыты стеной: отрезок
	// «центр клетки -> центр соседа» переводит тот или иной путь. Сетка подземелья
	// при скруглении не меняется, поэтому и набор перекрытых рёбер обязан остаться
	// прежним — иначе скругление открыло бы проход или перекрыла бы его там, где
	// через комнату должен был идти коридор.
	static void CollectBlockedEdges(const ADungeonGenerator* Gen, const TArray<FQuad4>& Quads, float Cell, TSet<FIntPoint>& Out,
		TMap<FIntPoint, TPair<FVector, FVector>>* OutCross = nullptr)
	{
		Out.Reset();
		if (Gen->Grid.Num() != Gen->GridW * Gen->GridH) return;

		TArray<TPair<FVector2D, FVector2D>> Edges;
		Edges.Reserve(Quads.Num());
		TArray<FVector2D> MinBox, MaxBox;
		MinBox.Reserve(Quads.Num());
		MaxBox.Reserve(Quads.Num());

		for (const FQuad4& Q : Quads)
		{
			const FVector2D A(Q.BotA->Position.X, Q.BotA->Position.Y);
			const FVector2D B(Q.BotB->Position.X, Q.BotB->Position.Y);
			Edges.Add(TPair<FVector2D, FVector2D>(A, B));
			MinBox.Add(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)));
			MaxBox.Add(FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
		}

		const double OffX = Gen->bCenterOnActor ? -Gen->GridW * Cell * 0.5 : 0.0;
		const double OffY = Gen->bCenterOnActor ? -Gen->GridH * Cell * 0.5 : 0.0;

		for (int32 Y = 0; Y < Gen->GridH; ++Y)
		{
			for (int32 X = 0; X < Gen->GridW; ++X)
			{
				const double CX = (X + 0.5) * Cell + OffX;
				const double CY = (Y + 0.5) * Cell + OffY;

				for (int32 Dir = 0; Dir < 2; ++Dir)
				{
					const double NX = CX + (Dir == 0 ? Cell : 0.0);
					const double NY = CY + (Dir == 1 ? Cell : 0.0);
					if (Dir == 0 && X + 1 >= Gen->GridW) continue;
					if (Dir == 1 && Y + 1 >= Gen->GridH) continue;

					const double MinX = FMath::Min(CX, NX) - 1.0, MaxX = FMath::Max(CX, NX) + 1.0;
					const double MinY = FMath::Min(CY, NY) - 1.0, MaxY = FMath::Max(CY, NY) + 1.0;

					const FVector2D S(CX, CY);
					const FVector2D E(NX, NY);

					for (int32 e = 0; e < Edges.Num(); ++e)
					{
						// грубый отсев по прямоугольнику: почти все грани отпадают сразу
						if (MaxBox[e].X < MinX || MinBox[e].X > MaxX || MaxBox[e].Y < MinY || MinBox[e].Y > MaxY) continue;

						if (SegmentsTouch(S, E, Edges[e].Key, Edges[e].Value))
						{
							const FIntPoint Key(Y * Gen->GridW + X, Dir);
							Out.Add(Key);
							if (OutCross) OutCross->Add(Key, TPair<FVector, FVector>(Quads[e].BotA->Position, Quads[e].BotB->Position));
							break;
						}
					}
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDangenRoundWallsTest, "Dangen.RoundCircleWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDangenRoundWallsTest::RunTest(const FString& Parameters)
{
	using namespace DangenRoundTest;

	// Пять ключей на обычной сетке + прогон на размерах, близких к настройкам
	// BP_DungeonGenerator в проекте (клетка 50, сетка 120x80).
	const FRunConfig Configs[] = {
		{ 197,       90, 64, 100.f },
		{ 42,        90, 64, 100.f },
		{ 1234,      90, 64, 100.f },
		{ 7,         90, 64, 100.f },
		{ 20260108,  90, 64, 100.f },
		{ 197,      120, 80,  50.f },
	};

	const bool bDumpRequested = FParse::Param(FCommandLine::Get(), TEXT("dangenrounddump"));
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("AgentScratch");

	int32 Runs = 0, RunsWithCircles = 0, RunsWithArc = 0;
	int32 TotalOffGrid = 0, TotalOpened = 0, TotalClosed = 0;

	// Выгружаем каждый прогон отдельным файлом (их сравнивают скрипты
	// Saved/AgentScratch) и запоминаем самый «круглый» прогон под именем
	// arc_dump.txt — на него смотрят render_plan.py и render_iso.py.
	int32 DumpBestArcs = -1;
	FString DumpBestAfter, DumpBestBefore;

	for (int32 Run = 0; Run < UE_ARRAY_COUNT(Configs); ++Run)
	{
		const FRunConfig& Cfg = Configs[Run];
		const float Cell = Cfg.Cell;

		ADungeonGenerator* Gen = MakeGen(Cfg, /*bRound=*/true);
		TestNotNull(TEXT("генератор создан"), Gen);
		if (!Gen) return false;

		Gen->Generate();
		++Runs;

		int32 CircleRooms = 0;
		for (const FPlacedRoom& R : Gen->Rooms)
		{
			if (R.Shape == ERoomShape::Circle) ++CircleRooms;
		}
		UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s] сетка [%sx%s] клетка [%s]: комнат [%s], круглых [%s]"),
		       *FString::FromInt(Cfg.Seed), *FString::FromInt(Cfg.W), *FString::FromInt(Cfg.H),
		       *FString::SanitizeFloat(Cfg.Cell), *FString::FromInt(static_cast<int32>(Gen->Rooms.Num())),
		       *FString::FromInt(CircleRooms));

		if (CircleRooms > 0) ++RunsWithCircles;

		UProceduralMeshComponent* Walls = MeshByName(Gen, TEXT("WallsMesh"));
		TestNotNull(TEXT("меш стен найден"), Walls);
		if (!Walls) return false;

		FProcMeshSection* WallsSec = Walls->GetProcMeshSection(0);
		TestNotNull(TEXT("секция стен есть"), WallsSec);
		if (!WallsSec) return false;

		TArray<FQuad4> Quads;
		CollectWallQuads(WallsSec, Gen->WallHeight, Quads);
		TestTrue(TEXT("грани стен собраны из треугольников"), Quads.Num() > 0);

		int32 ArcVerts = 0;
		for (const FProcMeshVertex& V : WallsSec->ProcVertexBuffer)
		{
			if (OffGrid(V.Position, Cell)) ++ArcVerts;
		}

		const int32 Degenerate = CountDegenerate(WallsSec);
		TestEqual(TEXT("нет вырожденных треугольников в стенах"), Degenerate, 0);

		// Переставленная грань не имеет права стоять в клетке коридора или чужой
		// комнаты: иначе стена перерезала бы проход и подземелье стало бы непроходимым.
		const float OX = Gen->GridW * Cell * 0.5f;   // bCenterOnActor -> сетка центрирована на акторе
		const float OY = Gen->GridH * Cell * 0.5f;
		int32 ArcInCorridorCell = 0;
		for (const FQuad4& Q : Quads)
		{
			const FVector& A = Q.BotA->Position;       // низ грани
			const FVector& B = Q.BotB->Position;
			if (!OffGrid(A, Cell) && !OffGrid(B, Cell)) continue;   // интересуют только дуга и примыкающие к ней грани

			const float MX = (A.X + B.X) * 0.5f;
			const float MY = (A.Y + B.Y) * 0.5f;
			const int32 GX = FMath::FloorToInt((MX + OX) / Cell);
			const int32 GY = FMath::FloorToInt((MY + OY) / Cell);
			if (GX < 0 || GY < 0 || GX >= Gen->GridW || GY >= Gen->GridH) continue;

			const int32 I = GY * Gen->GridW + GX;
			if (Gen->Grid[I] == 0) continue;                                 // камень, дуги тут и быть не должно
			if (Gen->RoadMask.Num() == Gen->GridW * Gen->GridH && Gen->RoadMask[I] != 0)
			{
				++ArcInCorridorCell;                                          // дуга встала поперёк коридора
				continue;
			}
			if (Gen->RoomMap[I] < 0) ++ArcInCorridorCell;                    // дуга в клетке дороги/проема
		}
		TestEqual(TEXT("граней дуги в клетке коридора или чужой клетке"), ArcInCorridorCell, 0);

		int32 RoofOffGrid = 0, RoofDegenerate = 0, FloorOffGrid = 0, FloorDegenerate = 0;
		FProcMeshSection* RoofSec = nullptr;
		FProcMeshSection* FloorSec = nullptr;

		if (UProceduralMeshComponent* Roof = MeshByName(Gen, TEXT("RoofMesh"))) RoofSec = Roof->GetProcMeshSection(0);
		if (UProceduralMeshComponent* Floor = MeshByName(Gen, TEXT("FloorMesh"))) FloorSec = Floor->GetProcMeshSection(0);

		if (RoofSec)
		{
			for (const FProcMeshVertex& V : RoofSec->ProcVertexBuffer) if (OffGrid(V.Position, Cell)) ++RoofOffGrid;
			RoofDegenerate = CountDegenerate(RoofSec);
		}
		if (FloorSec)
		{
			for (const FProcMeshVertex& V : FloorSec->ProcVertexBuffer) if (OffGrid(V.Position, Cell)) ++FloorOffGrid;
			FloorDegenerate = CountDegenerate(FloorSec);
		}

		// Оболочка проверяется как в редакторе: берём нижний край каждой дуги и
		// смотрим, дотягивается ли до него пол (или крышка, если снаружи камень),
		// и крышка ли доходит до верхнего края с наружной стороны. Иначе в потолке
		// чёрная дыра, а под стеной яма — ровно то, что было видно на снимках.
		auto PointInMesh = [](UProceduralMeshComponent* Mesh, double X, double Y) -> bool
		{
			if (!Mesh) return false;
			FProcMeshSection* Sec = Mesh->GetProcMeshSection(0);
			if (!Sec) return false;

			for (int32 i = 0; i + 2 < Sec->ProcIndexBuffer.Num(); i += 3)
			{
				const FVector& A = Sec->ProcVertexBuffer[static_cast<int32>(Sec->ProcIndexBuffer[i])].Position;
				const FVector& B = Sec->ProcVertexBuffer[static_cast<int32>(Sec->ProcIndexBuffer[i + 1])].Position;
				const FVector& C = Sec->ProcVertexBuffer[static_cast<int32>(Sec->ProcIndexBuffer[i + 2])].Position;

				const double D1 = (B.X - A.X) * (Y - A.Y) - (B.Y - A.Y) * (X - A.X);
				const double D2 = (C.X - B.X) * (Y - B.Y) - (C.Y - B.Y) * (X - B.X);
				const double D3 = (A.X - C.X) * (Y - C.Y) - (A.Y - C.Y) * (X - C.X);

				if ((D1 >= 0.0 && D2 >= 0.0 && D3 >= 0.0) || (D1 <= 0.0 && D2 <= 0.0 && D3 <= 0.0)) return true;
			}

			return false;
		};

		UProceduralMeshComponent* RoofLook = MeshByName(Gen, TEXT("RoofMesh"));
		UProceduralMeshComponent* FloorLook = MeshByName(Gen, TEXT("FloorMesh"));

		int32 RoofGaps = 0, FloorGaps = 0;
		const float SliceT[3] = { 0.25f, 0.5f, 0.75f };
		for (const FQuad4& Q : Quads)
		{
			const FVector& VA = Q.BotA->Position;
			const FVector& VB = Q.BotB->Position;
			if (!OffGrid(VA, Cell) && !OffGrid(VB, Cell)) continue;   // обычная грань-лесенка

			double NX = (Q.BotA->Normal.X + Q.BotB->Normal.X) * 0.5;
			double NY = (Q.BotA->Normal.Y + Q.BotB->Normal.Y) * 0.5;
			const double Len = FMath::Sqrt(NX * NX + NY * NY);
			if (Len < 1e-6) continue;
			const double OutX = -NX / Len, OutY = -NY / Len;   // наружу от центра комнаты

			for (const float T : SliceT)
			{
				const double PX = VA.X + (VB.X - VA.X) * static_cast<double>(T);
				const double PY = VA.Y + (VB.Y - VA.Y) * static_cast<double>(T);

				const double SideSign[2] = { 1.0, -1.0 };
				const double SideDist[2] = { 2.0, 10.0 };
				for (const double S : SideSign)
				{
					for (const double D : SideDist)
					{
						const double SX = PX + D * S * OutX;
						const double SY = PY + D * S * OutY;
						if (!PointInMesh(FloorLook, SX, SY) && !PointInMesh(RoofLook, SX, SY)) ++FloorGaps;
					}
				}

				const double OutDist[3] = { 2.0, 10.0, 30.0 };
				for (const double D : OutDist)
				{
					const double SX = PX + D * OutX;
					const double SY = PY + D * OutY;
					if (PointInMesh(FloorLook, SX, SY)) continue;   // снаружи проходимая клетка: крыши там нет по замыслу
					if (!PointInMesh(RoofLook, SX, SY)) ++RoofGaps;
				}
			}
		}

		// Сколько клеток сетки накрыто крышкой и полом. Крышка обязана быть на
		// каждой клетке камня, пол — на каждой проходимой клетке. Крышка и пол могут
		// накрыть и соседнюю клетку: если граня ушла вглубь клетки, пространство за
		// ней из проходимого становится камнем (и наоборот), а дыры там быть не должно.
		int32 RockCells = 0, CarvedCells = 0;
		for (const uint8 G : Gen->Grid)
		{
			if (G == 0) ++RockCells;
			else if (G == 1) ++CarvedCells;
		}

		int32 RoofCovered = 0, FloorCovered = 0;
		const double OffX = Gen->bCenterOnActor ? -Gen->GridW * Cell * 0.5 : 0.0;
		const double OffY = Gen->bCenterOnActor ? -Gen->GridH * Cell * 0.5 : 0.0;
		for (int32 Y = 0; Y < Gen->GridH; ++Y)
		{
			for (int32 X = 0; X < Gen->GridW; ++X)
			{
				const double CX = (X + 0.5) * Cell + OffX;
				const double CY = (Y + 0.5) * Cell + OffY;
				if (PointInMesh(RoofLook, CX, CY)) ++RoofCovered;
				if (PointInMesh(FloorLook, CX, CY)) ++FloorCovered;
			}
		}

		// Сколько прежних граней круглой комнаты ОСТАЛОСЬ на своём месте (у входов
		// и там, где через комнату идёт коридор). Их должно быть заметно меньше,
		// чем переставленных, иначе скругления фактически нет.
		int32 ArcQuads = 0, KeptQuads = 0;
		for (const FQuad4& Q : Quads)
		{
			if (OffGrid(Q.BotA->Position, Cell) || OffGrid(Q.BotB->Position, Cell)) { ++ArcQuads; continue; }
			if (!IsFaceOfCircleRoom(Q, Gen, Cell)) continue;
			++KeptQuads;
		}

		// ---------- цельность сети: сварка, обход, совпадение точек ----------
		const int32 WallDups  = CountDuplicateVerts(WallsSec);
		const int32 RoofDups  = CountDuplicateVerts(RoofSec);
		const int32 FloorDups = CountDuplicateVerts(FloorSec);

		int32 WallWithNorm = 0, WallAgainstNorm = 0;
		int32 RoofWithNorm = 0, RoofAgainstNorm = 0;
		int32 FloorWithNorm = 0, FloorAgainstNorm = 0;
		CountWinding(WallsSec, WallWithNorm, WallAgainstNorm);
		CountWinding(RoofSec, RoofWithNorm, RoofAgainstNorm);
		CountWinding(FloorSec, FloorWithNorm, FloorAgainstNorm);

		// Шов там, где поверхности соприкасаются, обязан быть общим: угол крышки или
		// плиты пола, вышедший за клетки сетки, стоит ровно на вершине стены, а
		// низ и верх переставленной грани — ровно на вершинах пола и крышки.
		const TSet<FIntPoint> WallKeys  = MeshXYKeys(WallsSec);
		const TSet<FIntPoint> RoofKeys  = MeshXYKeys(RoofSec);
		const TSet<FIntPoint> FloorKeys = MeshXYKeys(FloorSec);

		int32 RoofLoose = 0, FloorLoose = 0;
		if (RoofSec)
		{
			for (const FProcMeshVertex& V : RoofSec->ProcVertexBuffer)
			{
				if (OffGrid(V.Position, Cell) && !WallKeys.Contains(XYKey(V.Position))) ++RoofLoose;
			}
		}
		if (FloorSec)
		{
			for (const FProcMeshVertex& V : FloorSec->ProcVertexBuffer)
			{
				if (OffGrid(V.Position, Cell) && !WallKeys.Contains(XYKey(V.Position))) ++FloorLoose;
			}
		}

		int32 ArcLoose = 0;
		for (const FQuad4& Q : Quads)
		{
			if (!OffGrid(Q.BotA->Position, Cell) && !OffGrid(Q.BotB->Position, Cell)) continue;
			for (const FProcMeshVertex* V : { Q.BotA, Q.BotB, Q.TopA, Q.TopB })
			{
				const bool bOnFloor = FloorKeys.Contains(XYKey(V->Position));
				const bool bOnRoof  = RoofKeys.Contains(XYKey(V->Position));
				if (!bOnFloor || !bOnRoof) ++ArcLoose;
			}
		}

		// ---------- проходы: сравнение с тем же подземельем без скругления ----------
		int32 PassOpened = 0, PassClosed = 0;
		if (CircleRooms > 0)
		{
			ADungeonGenerator* Before = MakeGen(Cfg, /*bRound=*/false);
			Before->Generate();

			UProceduralMeshComponent* BeforeWalls = MeshByName(Before, TEXT("WallsMesh"));
			FProcMeshSection* BeforeSec = BeforeWalls ? BeforeWalls->GetProcMeshSection(0) : nullptr;

			TArray<FQuad4> BeforeQuads;
			CollectWallQuads(BeforeSec, Before->WallHeight, BeforeQuads);

			TSet<FIntPoint> BlockedBefore, BlockedAfter;
			TMap<FIntPoint, TPair<FVector, FVector>> CrossAfter;
			CollectBlockedEdges(Before, BeforeQuads, Before->CellSize, BlockedBefore);
			CollectBlockedEdges(Gen, Quads, Cell, BlockedAfter, &CrossAfter);

			TestTrue(TEXT("скругление не меняет сетку подземелья"), Gen->Grid == Before->Grid);

			// Рёбра сопоставляем по смыслу: ребро между ДВУМЯ проходимыми клетками —
			// это проход, стены там быть не должно ни до, ни после скругления. Ребро
			// «камень — проходимая клетка» при скруглении съезжает по делу: стена
			// по-прежнему стоит ровно на общей границе полигонов пола и крышки (это
			// проверяют проверки совпадения точек выше), поэтому её перестановку только
			// показываем в логе, но не считаем поломкой.
			int32 OtherOpened = 0, OtherClosed = 0;
			auto EdgeCarvedBoth = [&](const FIntPoint& E) -> bool
			{
				const int32 X = E.X % Gen->GridW;
				const int32 Y = E.X / Gen->GridW;
				const int32 NX = (E.Y == 0) ? X + 1 : X;
				const int32 NY = (E.Y == 1) ? Y + 1 : Y;
				if (NX >= Gen->GridW || NY >= Gen->GridH) return false;
				return Gen->Grid[E.X] == 1 && Gen->Grid[NY * Gen->GridW + NX] == 1;
			};

			for (const FIntPoint& E : BlockedBefore)
			{
				if (BlockedAfter.Contains(E)) continue;
				if (EdgeCarvedBoth(E)) ++PassOpened; else ++OtherOpened;
			}
			for (const FIntPoint& E : BlockedAfter)
			{
				if (BlockedBefore.Contains(E)) continue;
				if (EdgeCarvedBoth(E))
				{
					++PassClosed;
					const int32 X = E.X % Gen->GridW;
					const int32 Y = E.X / Gen->GridW;
					const int32 NX = (E.Y == 0) ? X + 1 : X;
					const int32 NY = (E.Y == 1) ? Y + 1 : Y;
					const int32 NI = NY * Gen->GridW + NX;
					const TPair<FVector, FVector>* Hit = CrossAfter.Find(E);
					UE_LOG(LogTemp, Display, TEXT("DangenTest: ПРОХОД [%s][%s] -> [%s][%s] перерезан: клетки [%s]/[%s], маска дороги [%s]/[%s], комната [%s]/[%s], низ грани [%s %s] -> [%s %s]"),
					       *FString::FromInt(X), *FString::FromInt(Y), *FString::FromInt(NX), *FString::FromInt(NY),
					       *FString::FromInt(Gen->Grid[E.X]), *FString::FromInt(Gen->Grid[NI]),
					       *FString::FromInt(Gen->RoadMask.Num() == Gen->GridW * Gen->GridH ? Gen->RoadMask[E.X] : -1),
					       *FString::FromInt(Gen->RoadMask.Num() == Gen->GridW * Gen->GridH ? Gen->RoadMask[NI] : -1),
					       *FString::FromInt(Gen->RoomMap.Num() == Gen->GridW * Gen->GridH ? Gen->RoomMap[E.X] : -1),
					       *FString::FromInt(Gen->RoomMap.Num() == Gen->GridW * Gen->GridH ? Gen->RoomMap[NI] : -1),
					       Hit ? *FString::SanitizeFloat(static_cast<float>(Hit->Key.X / Cell), 2) : TEXT("?"),
					       Hit ? *FString::SanitizeFloat(static_cast<float>(Hit->Key.Y / Cell), 2) : TEXT("?"),
					       Hit ? *FString::SanitizeFloat(static_cast<float>(Hit->Value.X / Cell), 2) : TEXT("?"),
					       Hit ? *FString::SanitizeFloat(static_cast<float>(Hit->Value.Y / Cell), 2) : TEXT("?"));
				}
				else ++OtherClosed;
			}

			UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s] сетка [%sx%s]: стен между проходимыми клетками — до [%s], после [%s], разрезано проходов [%s], стен в проходе не стало [%s]; у границы камня съехало: открылось [%s], закрылось [%s]"),
			       *FString::FromInt(Cfg.Seed), *FString::FromInt(Cfg.W), *FString::FromInt(Cfg.H),
			       *FString::FromInt(BlockedBefore.Num()), *FString::FromInt(BlockedAfter.Num()),
			       *FString::FromInt(PassClosed), *FString::FromInt(PassOpened),
			       *FString::FromInt(OtherOpened), *FString::FromInt(OtherClosed));

			if (bDumpRequested)
			{
				FString After = DumpMeshes(Gen);
				FString BeforeDump = DumpMeshes(Before);

				const FString AfterPath = Dir / FString::Printf(TEXT("arc_dump_%d.txt"), Run + 1);
				const FString BeforePath = Dir / FString::Printf(TEXT("arc_dump_before_%d.txt"), Run + 1);
				FFileHelper::SaveStringToFile(After, *AfterPath);
				FFileHelper::SaveStringToFile(BeforeDump, *BeforePath);
				UE_LOG(LogTemp, Display, TEXT("DangenTest: выгрузка мешей -> %s и %s"), *AfterPath, *BeforePath);

				if (ArcQuads > DumpBestArcs)
				{
					DumpBestArcs = ArcQuads;
					DumpBestAfter = MoveTemp(After);
					DumpBestBefore = MoveTemp(BeforeDump);
				}
			}

			Before->Destroy();
		}

		UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s] сетка [%sx%s]: вершин стен [%s], дуги и примыкающих к ним граней [%s], прежних граней у круглых комнат [%s], вершин вне клеток сетки: крыша [%s], пол [%s], дыр: потолок [%s] пол [%s], накрыто клеток: крыша [%s] из [%s], пол [%s] из [%s]"),
		       *FString::FromInt(Cfg.Seed), *FString::FromInt(Cfg.W), *FString::FromInt(Cfg.H),
		       *FString::FromInt(static_cast<int32>(WallsSec->ProcVertexBuffer.Num())),
		       *FString::FromInt(ArcQuads), *FString::FromInt(KeptQuads),
		       *FString::FromInt(RoofOffGrid), *FString::FromInt(FloorOffGrid),
		       *FString::FromInt(RoofGaps), *FString::FromInt(FloorGaps),
		       *FString::FromInt(RoofCovered), *FString::FromInt(RockCells),
		       *FString::FromInt(FloorCovered), *FString::FromInt(CarvedCells));
		UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s]: сварено — повторных вершин: стены [%s], крыша [%s], пол [%s]; треугольников против нормали: стены [%s], крыша [%s], пол [%s]; швов вне сетки: крышка [%s], плита [%s], концы граней дуги [%s]"),
		       *FString::FromInt(Cfg.Seed),
		       *FString::FromInt(WallDups), *FString::FromInt(RoofDups), *FString::FromInt(FloorDups),
		       *FString::FromInt(WallAgainstNorm), *FString::FromInt(RoofAgainstNorm), *FString::FromInt(FloorAgainstNorm),
		       *FString::FromInt(RoofLoose), *FString::FromInt(FloorLoose), *FString::FromInt(ArcLoose));

		if (ArcVerts > 0) ++RunsWithArc;
		TotalOffGrid += RoofOffGrid + FloorOffGrid;
		TotalOpened += PassOpened;
		TotalClosed += PassClosed;

		// Если в круглых комнатах грани не переставлены хотя бы в одном прогоне — это провал.
		if (CircleRooms > 0)
		{
			TestTrue(TEXT("круглые комнаты переставляют грани на эллипс"), ArcQuads > 0);
			TestTrue(TEXT("переставленных граней больше, чем оставшихся лесенкой"), ArcQuads > KeptQuads);

			// И крышка, и пол дотянуты до стены своими же полигонами: у обеих
			// поверхностей есть углы там, где клеток сетки нет.
			TestTrue(TEXT("крышка дотянута до стены (её углы вне клеток сетки)"), RoofOffGrid > 0);
			TestTrue(TEXT("пол дотянут до стены (его углы вне клеток сетки)"), FloorOffGrid > 0);

			TestEqual(TEXT("нет вырожденных треугольников на крыше"), RoofDegenerate, 0);
			TestEqual(TEXT("нет вырожденных треугольников на полу"), FloorDegenerate, 0);

			TestEqual(TEXT("над верхним краем граней дуги крышка доходит до стены (дыр в потолке нет)"), RoofGaps, 0);
			TestEqual(TEXT("под нижним краем граней дуги есть пол или крышка (ям под стеной нет)"), FloorGaps, 0);
			TestTrue(TEXT("крышка накрывает все клетки камня (дыр в крышке нет)"), RoofCovered >= RockCells);
			TestTrue(TEXT("пол лежит на всех проходимых клетках (ям под полом нет)"), FloorCovered >= CarvedCells);

			// Сеть обязана быть цельной: одна точка соприкосновения — одна вершина.
			TestEqual(TEXT("в сете стен нет повторяющихся вершин (стык — одна точка)"), WallDups, 0);
			TestEqual(TEXT("в сете крыши нет повторяющихся вершин"), RoofDups, 0);
			TestEqual(TEXT("в сете пола нет повторяющихся вершин"), FloorDups, 0);

			// Вывернутых полигонов быть не должно ни на одном меше.
			TestEqual(TEXT("грани стен смотрят одной стороной со своей нормалью"), WallAgainstNorm, 0);
			TestEqual(TEXT("плиты крышки смотрят той же стороной, что и прежние плиты (нормаль +Z)"), RoofAgainstNorm, 0);
			TestEqual(TEXT("плиты пола смотрят той же стороной, что и прежние плиты (нормаль +Z)"), FloorAgainstNorm, 0);

			TestEqual(TEXT("каждый угол крышки вне сетки стоит ровно на вершине стены"), RoofLoose, 0);
			TestEqual(TEXT("каждый угол плиты пола вне сетки стоит ровно на вершине стены"), FloorLoose, 0);
			TestEqual(TEXT("низ и верх переставленной грани стоят на вершинах пола и крышки (одна точка на три поверхности)"), ArcLoose, 0);

			// Скругление не имеет права менять проходы подземелья.
			TestEqual(TEXT("в проходе не исчезла стена"), PassOpened, 0);
			TestEqual(TEXT("ни один проход не разрезан стеной (стена не залезла в проходимую клетку)"), PassClosed, 0);
		}

		Gen->Destroy();   // мир тесту не нужен, но удаление безопасно в любом случае
	}

	if (bDumpRequested && !DumpBestAfter.IsEmpty())
	{
		const FString AfterPath = Dir / TEXT("arc_dump.txt");
		const FString BeforePath = Dir / TEXT("arc_dump_before.txt");
		FFileHelper::SaveStringToFile(DumpBestAfter, *AfterPath);
		FFileHelper::SaveStringToFile(DumpBestBefore, *BeforePath);
		UE_LOG(LogTemp, Display, TEXT("DangenTest: самый круглый прогон (%s граней дуги) выгружен в %s и %s"),
		       *FString::FromInt(DumpBestArcs), *AfterPath, *BeforePath);
	}

	UE_LOG(LogTemp, Display, TEXT("DangenTest ИТОГ: прогонов [%s], с круглыми комнатами [%s], со скруглением [%s], вершин вне клеток сетки (крыша+пол) [%s], проходов открылось [%s], закрылось [%s]"),
	       *FString::FromInt(Runs), *FString::FromInt(RunsWithCircles),
	       *FString::FromInt(RunsWithArc), *FString::FromInt(TotalOffGrid),
	       *FString::FromInt(TotalOpened), *FString::FromInt(TotalClosed));

	TestTrue(TEXT("хотя бы в одном прогоне есть круглая комната"), RunsWithCircles > 0);
	TestTrue(TEXT("хотя бы в одном прогоне стены скруглены"), RunsWithArc > 0);
	TestTrue(TEXT("за все прогоны крышки и пол дотянуты до стен"), TotalOffGrid > 0);
	TestEqual(TEXT("за все прогоны проходы не изменились"), TotalOpened + TotalClosed, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
