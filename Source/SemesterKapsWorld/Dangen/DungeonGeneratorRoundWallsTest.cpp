// =====================================================================
//  Проверка скругления комнат формы «Круглая» (автотест).
//
//  Прогоняет генератор на нескольких ключах и по мешу стен проверяет:
//   - есть вершины НЕ по границам клеток (значит дуга построена);
//   - нет вырожденных треугольников;
//   - крышка уголков добавляет плиты крыши.
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
				Dump += FString::Printf(TEXT("%s %.2f %.2f %.2f %.2f %.2f %.2f\n"), Tag,
				                        V.Position.X, V.Position.Y, V.Position.Z,
				                        V.Normal.X, V.Normal.Y, V.Normal.Z);
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
		Gen->WallArcSegments = 16;
		Gen->bCapRoundCorners = true;
		return Gen;
	}

	// Вершина «не по сетке» = стоит внутри клетки, а не на её границе: таким
	// образом отличаем грани дуги и крышки уголков от обычных граней-лесенок.
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

	int32 Runs = 0, RunsWithCircles = 0, RunsWithArc = 0, TotalCapVerts = 0;
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
		Gen->WallArcSegments = 16;
		Gen->bCapRoundCorners = true;

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

		// Дуга не имеет права стоять в клетке коридора или чужой комнаты: иначе
		// гладкая стена перерезала бы проход и подземелье стало бы непроходимым.
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

		int32 CapVerts = 0;
		if (UProceduralMeshComponent* Roof = MeshByName(Gen, TEXT("RoofMesh")))
		{
			if (FProcMeshSection* RoofSec = Roof->GetProcMeshSection(0))
			{
				for (const FProcMeshVertex& V : RoofSec->ProcVertexBuffer)
				{
					const float FX = FMath::Abs(V.Position.X / Cell - FMath::RoundToFloat(V.Position.X / Cell));
					const float FY = FMath::Abs(V.Position.Y / Cell - FMath::RoundToFloat(V.Position.Y / Cell));
					if (FX > 1e-3f || FY > 1e-3f) ++CapVerts;
				}
			}
		}

		UE_LOG(LogTemp, Display, TEXT("DangenTest: ключ [%s]: вершин стен [%s], граней дуги [%s], вершин крышки [%s]"),
		       *FString::FromInt(Seed), *FString::FromInt(static_cast<int32>(WallsSec->ProcVertexBuffer.Num())),
		       *FString::FromInt(ArcVerts), *FString::FromInt(CapVerts));

		if (ArcVerts > 0) ++RunsWithArc;
		TotalCapVerts += CapVerts;

		// Если в круглых комнатах дуга не построилась хотя бы в одном прогоне — это провал.
		if (CircleRooms > 0)
		{
			TestTrue(TEXT("круглые комнаты дают грани дуги"), ArcVerts > 0);
			TestTrue(TEXT("крышка уголков добавляет плиты крыши"), CapVerts > 0);
		}

		// Выгружаем прогон с круглыми комнатами (последний подходящий), чтобы
		// посмотреть результат сверху.
		if (CircleRooms > 0)
		{
			Dump = DumpMeshes(Gen);
		}

		Gen->Destroy();   // мир тесту не нужен, но удаление безопасно в любом случае
	}

	UE_LOG(LogTemp, Display, TEXT("DangenTest ИТОГ: прогонов [%s], с круглыми комнатами [%s], с дугой [%s], вершин крышки [%s]"),
	       *FString::FromInt(Runs), *FString::FromInt(RunsWithCircles),
	       *FString::FromInt(RunsWithArc), *FString::FromInt(TotalCapVerts));

	TestTrue(TEXT("хотя бы в одном прогоне есть круглая комната"), RunsWithCircles > 0);
	TestTrue(TEXT("хотя бы в одном прогоне дуга построена"), RunsWithArc > 0);

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
