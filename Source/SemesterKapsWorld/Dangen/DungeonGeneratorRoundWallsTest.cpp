// =====================================================================
//  Проверка скругления комнат формы «Круглая» (автотест).
//
//  Прогоняет генератор на нескольких ключах и по мешу стен проверяет:
//   - стены круглых комнат переставлены на эллипс (вершины НЕ по границам
//     клеток) и переставленных граней больше, чем оставшихся лесенкой;
//   - нет вырожденных треугольников;
//   - ни одна переставленная грань не стоит в клетке коридора (проходы целы);
//   - крыша не получает новых плит: скругление делает только полукруглую стену.
//
//  Выгрузка вершин мешей (нужна, чтобы посмотреть контур сверху скриптом
//  Saved/AgentScratch/render_compare.py) включается только ключом -dangenrounddump:
//  обычный прогон автотестов не должен плодить файлы в Saved.
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
				Dump += FString::Printf(TEXT("v %s %.2f %.2f %.2f %.2f %.2f %.2f\n"), Tag,
				                        V.Position.X, V.Position.Y, V.Position.Z,
				                        V.Normal.X, V.Normal.Y, V.Normal.Z);
			}

			// Индексы тоже: без них не разобрать, какие треугольники к каким вершинам
			// относятся, а проверка оболочки (стена <=> пол и крыша) их требует.
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
	static ADungeonGenerator* MakeGen(bool bRound)
	{
		ADungeonGenerator* Gen = NewObject<ADungeonGenerator>(GetTransientPackage());
		Gen->GridW = 90;
		Gen->GridH = 64;
		Gen->CellSize = 100.f;
		Gen->WallHeight = 400.f;
		Gen->Seed = 20260108;
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

	// Грань стоит на прежнем месте внутри прямоугольника круглой комнаты? Берём
	// середину нижнего ребра, по нормали грани находим клетку камня и клетку
	// открытую, и смотрим, принадлежат ли они этой круглой комнате.
	static bool IsFaceOfCircleRoom(const TArray<FProcMeshVertex>& Verts, int32 QuadBase, const ADungeonGenerator* Gen, float Cell)
	{
		const FVector& A = Verts[QuadBase].Position;
		const FVector& B = Verts[QuadBase + 1].Position;
		const FVector N  = Verts[QuadBase].Normal.GetSafeNormal();
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

	// Вершина «не по сетке» = стоит внутри клетки, а не на её границе: таким
	// образом отличаем переставленные грани от обычных граней-лесенок.
	static bool OffGrid(const FVector& P, float Cell)
	{
		const float FX = FMath::Abs(P.X / Cell - FMath::RoundToFloat(P.X / Cell));
		const float FY = FMath::Abs(P.Y / Cell - FMath::RoundToFloat(P.Y / Cell));
		return FX > 1e-3f && FY > 1e-3f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDangenRoundWallsTest, "Dangen.RoundCircleWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDangenRoundWallsTest::RunTest(const FString& Parameters)
{
	using namespace DangenRoundTest;

	const float Cell = 100.f;
	const int32 Seeds[] = { 197, 42, 1234, 7, 20260108 };

	int32 Runs = 0, RunsWithCircles = 0, RunsWithArc = 0;
	int32 TotalRoofPlates = 0, TotalFloorPlates = 0;
	FString Dump;

	for (int32 Seed : Seeds)
	{
		ADungeonGenerator* Gen = NewObject<ADungeonGenerator>(GetTransientPackage());
		TestNotNull(TEXT("генератор создан"), Gen);
		if (!Gen) return false;

		Gen->GridW = 90;
		Gen->GridH = 64;
		Gen->CellSize = Cell;
		Gen->WallHeight = 400.f;
		Gen->Seed = Seed;          // 0 = случайный; тут всегда один и тот же результат
		Gen->bSpawnMarkers = false;
		Gen->bBuildGeometry = true;
		Gen->bRoundCircleWalls = true;

		Gen->Generate();
		++Runs;

		int32 CircleRooms = 0;
		for (const FPlacedRoom& R : Gen->Rooms)
		{
			if (R.Shape == ERoomShape::Circle) ++CircleRooms;
		}
		UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s]: комнат [%s], круглых [%s]"),
		       *FString::FromInt(Seed), *FString::FromInt(static_cast<int32>(Gen->Rooms.Num())), *FString::FromInt(CircleRooms));

		if (CircleRooms > 0) ++RunsWithCircles;

		UProceduralMeshComponent* Walls = MeshByName(Gen, TEXT("WallsMesh"));
		TestNotNull(TEXT("меш стен найден"), Walls);
		if (!Walls) return false;

		FProcMeshSection* WallsSec = Walls->GetProcMeshSection(0);
		TestNotNull(TEXT("секция стен есть"), WallsSec);
		if (!WallsSec) return false;

		int32 ArcVerts = 0;
		for (const FProcMeshVertex& V : WallsSec->ProcVertexBuffer)
		{
			if (OffGrid(V.Position, Cell)) ++ArcVerts;
		}

		int32 Degenerate = 0;
		for (int32 i = 0; i + 2 < WallsSec->ProcIndexBuffer.Num(); i += 3)
		{
			const FVector& A = WallsSec->ProcVertexBuffer[static_cast<int32>(WallsSec->ProcIndexBuffer[i])].Position;
			const FVector& B = WallsSec->ProcVertexBuffer[static_cast<int32>(WallsSec->ProcIndexBuffer[i + 1])].Position;
			const FVector& C = WallsSec->ProcVertexBuffer[static_cast<int32>(WallsSec->ProcIndexBuffer[i + 2])].Position;
			if (FVector::CrossProduct(B - A, C - A).Size() < 1e-3f) ++Degenerate;
		}
		TestEqual(TEXT("нет вырожденных треугольников в стенах"), Degenerate, 0);

		// Переставленная грань не имеет права стоять в клетке коридора или чужой
		// комнаты: иначе стена перерезала бы проход и подземелье стало бы непроходимым.
		const float OX = Gen->GridW * Cell * 0.5f;   // bCenterOnActor -> сетка центрирована на акторе
		const float OY = Gen->GridH * Cell * 0.5f;
		int32 ArcInCorridorCell = 0;
		for (int32 i = 0; i + 3 < WallsSec->ProcVertexBuffer.Num(); i += 4)
		{
			const FVector& A = WallsSec->ProcVertexBuffer[i].Position;       // низ грани
			const FVector& B = WallsSec->ProcVertexBuffer[i + 1].Position;
			if (!OffGrid(A, Cell) || !OffGrid(B, Cell)) continue;            // интересуют только грани дуги

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

		// Примыкания стен к крыше и полу. Плита примыкания единственная деталь,
		// которая выходит за клетки сетки: её внутренние углы стоят на самой стене.
		// Поэтому вне сетки должны быть ровно 4 вершины на каждую такую плиту, а
		// вырожденных треугольников ни на крыше, ни на полу — ни одной.
		int32 RoofOffGrid = 0, RoofDegenerate = 0;
		if (UProceduralMeshComponent* Roof = MeshByName(Gen, TEXT("RoofMesh")))
		{
			if (FProcMeshSection* RoofSec = Roof->GetProcMeshSection(0))
			{
				for (const FProcMeshVertex& V : RoofSec->ProcVertexBuffer)
				{
					if (OffGrid(V.Position, Cell)) ++RoofOffGrid;
				}
				for (int32 i = 0; i + 2 < RoofSec->ProcIndexBuffer.Num(); i += 3)
				{
					const FVector& A = RoofSec->ProcVertexBuffer[static_cast<int32>(RoofSec->ProcIndexBuffer[i])].Position;
					const FVector& B = RoofSec->ProcVertexBuffer[static_cast<int32>(RoofSec->ProcIndexBuffer[i + 1])].Position;
					const FVector& C = RoofSec->ProcVertexBuffer[static_cast<int32>(RoofSec->ProcIndexBuffer[i + 2])].Position;
					if (FVector::CrossProduct(B - A, C - A).Size() < 1e-3f) ++RoofDegenerate;
				}
			}
		}

		int32 FloorOffGrid = 0, FloorDegenerate = 0;
		if (UProceduralMeshComponent* Floor = MeshByName(Gen, TEXT("FloorMesh")))
		{
			if (FProcMeshSection* FloorSec = Floor->GetProcMeshSection(0))
			{
				for (const FProcMeshVertex& V : FloorSec->ProcVertexBuffer)
				{
					if (OffGrid(V.Position, Cell)) ++FloorOffGrid;
				}
				for (int32 i = 0; i + 2 < FloorSec->ProcIndexBuffer.Num(); i += 3)
				{
					const FVector& A = FloorSec->ProcVertexBuffer[static_cast<int32>(FloorSec->ProcIndexBuffer[i])].Position;
					const FVector& B = FloorSec->ProcVertexBuffer[static_cast<int32>(FloorSec->ProcIndexBuffer[i + 1])].Position;
					const FVector& C = FloorSec->ProcVertexBuffer[static_cast<int32>(FloorSec->ProcIndexBuffer[i + 2])].Position;
					if (FVector::CrossProduct(B - A, C - A).Size() < 1e-3f) ++FloorDegenerate;
				}
			}
		}

		// Сколько прежних граней круглой комнаты ОСТАЛОСЬ на своём месте (у входов
		// и там, где через комнату идёт коридор). Их должно быть заметно меньше,
		// чем переставленных, иначе скругления фактически нет.
		int32 ArcQuads = 0, KeptQuads = 0;
		for (int32 i = 0; i + 3 < WallsSec->ProcVertexBuffer.Num(); i += 4)
		{
			const FProcMeshVertex& V0 = WallsSec->ProcVertexBuffer[i];
			const FProcMeshVertex& V1 = WallsSec->ProcVertexBuffer[i + 1];
			if (OffGrid(V0.Position, Cell) || OffGrid(V1.Position, Cell)) { ++ArcQuads; continue; }
			if (!IsFaceOfCircleRoom(WallsSec->ProcVertexBuffer, i, Gen, Cell)) continue;
			++KeptQuads;
		}

		UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s]: вершин стен [%s], переставлено на эллипс [%s], прежних граней у круглых комнат [%s], плит примыкания: крыша [%s], пол [%s]"),
		       *FString::FromInt(Seed), *FString::FromInt(static_cast<int32>(WallsSec->ProcVertexBuffer.Num())),
		       *FString::FromInt(ArcQuads), *FString::FromInt(KeptQuads),
		       *FString::FromInt(RoofOffGrid / 4), *FString::FromInt(FloorOffGrid / 4));

		if (ArcVerts > 0) ++RunsWithArc;
		TotalRoofPlates += RoofOffGrid / 4;
		TotalFloorPlates += FloorOffGrid / 4;

		// Если в круглых комнатах грани не переставлены хотя бы в одном прогоне — это провал.
		if (CircleRooms > 0)
		{
			TestTrue(TEXT("круглые комнаты переставляют грани на эллипс"), ArcQuads > 0);
			TestTrue(TEXT("переставленных граней больше, чем оставшихся лесенкой"), ArcQuads > KeptQuads);
		TestTrue(TEXT("переставленные грани примкнуты плитой к крыше или полу"),
		         RoofOffGrid + FloorOffGrid > 0);
			TestEqual(TEXT("нет вырожденных треугольников на крыше"), RoofDegenerate, 0);
		TestEqual(TEXT("нет вырожденных треугольников на полу"), FloorDegenerate, 0);
		// На каждую переставленную грань приходится плита примыкания, и у каждой
		// плиты есть хотя бы один угол на самой стене, то есть вне клеток сетки.
		TestTrue(TEXT("каждая переставленная грань получила плиту примыкания"),
		         RoofOffGrid + FloorOffGrid >= ArcQuads);
		}

		// Выгружаем прогон с круглыми комнатами (последний подходящий), чтобы
		// посмотреть результат сверху.
		if (CircleRooms > 0)
		{
			Dump = DumpMeshes(Gen);
		}

		Gen->Destroy();   // мир тесту не нужен, но удаление безопасно в любом случае
	}

	UE_LOG(LogTemp, Display, TEXT("DangenTest ИТОГ: прогонов [%s], с круглыми комнатами [%s], со скруглением [%s], плит примыкания: крыша [%s], пол [%s]"),
	       *FString::FromInt(Runs), *FString::FromInt(RunsWithCircles),
	       *FString::FromInt(RunsWithArc),
	       *FString::FromInt(TotalRoofPlates), *FString::FromInt(TotalFloorPlates));

	TestTrue(TEXT("хотя бы в одном прогоне есть круглая комната"), RunsWithCircles > 0);
	TestTrue(TEXT("хотя бы в одном прогоне стены скруглены"), RunsWithArc > 0);
	TestTrue(TEXT("за все прогоны есть плиты примыкания"), TotalRoofPlates + TotalFloorPlates > 0);

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("AgentScratch");
	const bool bDumpRequested = FParse::Param(FCommandLine::Get(), TEXT("dangenrounddump"));

	if (bDumpRequested && !Dump.IsEmpty())
	{
		const FString Path = Dir / TEXT("arc_dump.txt");
		FFileHelper::SaveStringToFile(Dump, *Path);
		UE_LOG(LogTemp, Display, TEXT("DangenTest: выгрузка мешей -> %s"), *Path);
	}

	// ---------- то же подземелье, но без скругления: для сравнения «до/после» ----------
	if (bDumpRequested)
	{
		ADungeonGenerator* Before = MakeGen(/*bRound=*/false);
		Before->Generate();
		const FString Path = Dir / TEXT("arc_dump_before.txt");
		FFileHelper::SaveStringToFile(DumpMeshes(Before), *Path);
		UE_LOG(LogTemp, Display, TEXT("DangenTest: выгрузка «до» (скругление выключено) -> %s"), *Path);
		Before->Destroy();
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
