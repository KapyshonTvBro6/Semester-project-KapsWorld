// DungeonGenerator.cpp
#include "DungeonGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"	// TActorIterator — нужен, чтобы найти старые маркеры в уровне

// Маркеры старта/финиша помечаются этим тегом. При каждой генерации удаляются ВСЕ
// огни с таким тегом во всём уровне, а не только те, что помнит список Markers:
// список теряется при хот-релоде, компиляции Blueprint, PIE, загрузке сохранённого
// уровня или если актор-генератор удалили и поставили заново.
static const FName DungeonMarkerTag(TEXT("DungeonMarker"));

// Встроенный набор комнат. Используется в двух местах: как значение по умолчанию
// для новых акторов и как подстраховка в GenerateInternal(), если у уже
// сохранённого актора Templates оказался пустым (иначе подземелье = 2 комнаты).
static TArray<FRoomTemplate> MakeDefaultTemplates()
{
	TArray<FRoomTemplate> Out;

	auto Add = [&](const TCHAR* Name, int32 WMin, int32 WMax, int32 HMin, int32 HMax,
	               int32 Count, ERoomCondition Cond, ERoomShape Shape, const FLinearColor& Color)
	{
		FRoomTemplate T;
		T.Name = Name;
		T.WMin = WMin; T.WMax = WMax; T.HMin = HMin; T.HMax = HMax;
		T.Count = Count;
		T.Condition = Cond;
		T.Shape = Shape;
		T.Color = Color;
		Out.Add(T);
	};

	Add(TEXT("Большой зал"),   7, 10, 7, 10, 2, ERoomCondition::Any,          ERoomShape::Rect,   FLinearColor(0.32f, 0.30f, 0.26f));
	Add(TEXT("Комната"),       5,  8, 5,  8, 3, ERoomCondition::Any,          ERoomShape::Rect,   FLinearColor(0.42f, 0.30f, 0.20f));
	Add(TEXT("Кладовая"),      3,  5, 3,  5, 3, ERoomCondition::NearStart,    ERoomShape::Rect,   FLinearColor(0.55f, 0.45f, 0.22f));
	Add(TEXT("Крипта"),        4,  7, 4,  7, 2, ERoomCondition::FarFromStart, ERoomShape::Circle, FLinearColor(0.25f, 0.30f, 0.40f));
	Add(TEXT("Пост у дороги"), 3,  4, 3,  4, 2, ERoomCondition::NextToRoad,   ERoomShape::Rect,   FLinearColor(0.35f, 0.42f, 0.28f));

	return Out;
}

ADungeonGenerator::ADungeonGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Стены: ОДИН процедурный меш, одна секция (создаётся в BuildGeometry)
	WallsMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WallsMesh"));
	WallsMesh->SetupAttachment(Root);
	// Movable (не Static): при каждом Generate() пересоздаются и вершины, и коллизия
	WallsMesh->SetMobility(EComponentMobility::Movable);
	WallsMesh->SetCastShadow(true);
	// коллизия варится синхронно (по умолчанию) -> стены активны сразу после генерации

	// Крыша: ОДИН процедурный меш, одна секция (создаётся в BuildGeometry)
	RoofMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoofMesh"));
	RoofMesh->SetupAttachment(Root);
	RoofMesh->SetMobility(EComponentMobility::Movable);
	RoofMesh->SetCastShadow(true);

	// Пол: ОДИН процедурный меш, одна секция (создаётся в BuildGeometry):
	// плоские прямоугольники над клетками 1 с тем же тайлингом, что у стен и крыши
	FloorMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FloorMesh"));
	FloorMesh->SetupAttachment(Root);
	FloorMesh->SetMobility(EComponentMobility::Movable);
	FloorMesh->SetCastShadow(true);

	// Старый пол: ОДНА плоскость, растянутая под всей сеткой. Один Plane на всё
	// подземелье показывал одну и ту же текстуру, растянутую на десятки метров,
	// поэтому пол теперь собирается во FloorMesh. Компонент оставлен и спрятан:
	// его материал переносится на FloorMesh, сам компонент можно удалить из BP.
	FloorPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorPlane"));
	FloorPlane->SetupAttachment(Root);
	FloorPlane->SetMobility(EComponentMobility::Movable);   // масштаб и положение меняем в рантайме
	FloorPlane->SetVisibility(false);
	FloorPlane->SetHiddenInGame(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		FloorPlane->SetStaticMesh(PlaneMesh.Object);
	}

	// дефолты для старта и финиша (жёлтый / красный)
	StartRoom.Name = TEXT("Комната начала");
	StartRoom.WMin = 4; StartRoom.WMax = 6; StartRoom.HMin = 4; StartRoom.HMax = 6;
	StartRoom.Color = FLinearColor(0.9f, 0.7f, 0.13f);

	EndRoom.Name = TEXT("Комната конца");
	EndRoom.WMin = 4; EndRoom.WMax = 6; EndRoom.HMin = 4; EndRoom.HMax = 6;
	EndRoom.Color = FLinearColor(0.75f, 0.22f, 0.17f);

	// обычные комнаты: 10 штук + старт + финиш + 2 поста = 12-14 комнат
	Templates = MakeDefaultTemplates();
}

uint8 ADungeonGenerator::GetGridValue(int32 X, int32 Y) const
{
	return InBounds(X, Y) ? Grid[Idx(X, Y)] : 0;
}

// =====================================================================
//  ВХОД: генерация
// =====================================================================
void ADungeonGenerator::Generate()
{
	// Сначала убираем всё, что осталось от прошлой генерации (меши, пол, маркеры),
	// иначе новая ложится поверх старой.
	ClearGenerated();

	Seed != 0 ? Stream.Initialize(Seed) : Stream.Initialize(FMath::Rand());

	GenerateInternal();

	if (bBuildGeometry)
	{
		BuildGeometry();
	}
}

void ADungeonGenerator::GenerateInternal()
{
	// --- шаг 1: сетка из нулей ---
	Grid.SetNumZeroed(GridW * GridH);
	RoomMap.Init(-1, GridW * GridH);
	RoadMask.Init(0, GridW * GridH);   // пустая карта коридоров: заполняет CarveRoad
	Rooms.Empty();

	// Если Templates пуст (актор сохранён до появления дефолтов) — берём встроенный
	// набор: иначе подземелье состоит ровно из комнаты старта и комнаты финиша.
	const TArray<FRoomTemplate> Tpls = (Templates.Num() > 0) ? Templates : MakeDefaultTemplates();
	if (Templates.Num() == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("Dungeon: Templates пуст — взят встроенный набор (%d шаблонов)"), Tpls.Num());
	}

	// Одно место под комнату финиша резервируем всегда
	const int32 MainCap = FMath::Max(1, MaxRooms - 1);

	// --- шаг 2: фаза 1 — большие комнаты ---
	// старт: поближе к случайному углу
	const int32 Corner = Rnd(0, 3);
	const int32 CornerX = (Corner % 2) ? GridW - 6 : 6;
	const int32 CornerY = (Corner < 2) ? 6 : GridH - 6;

	TArray<FRoomSpot> Placed;
	FRoomSpot BestStart;
	float BestStartScore = FLT_MAX;
	for (int32 i = 0; i < 300; ++i)
	{
		FRoomSpot S;
		S.W = Rnd(StartRoom.WMin, FMath::Max(StartRoom.WMin, StartRoom.WMax));
		S.H = Rnd(StartRoom.HMin, FMath::Max(StartRoom.HMin, StartRoom.HMax));
		if (S.W + 2 >= GridW || S.H + 2 >= GridH) continue;
		S.X = Rnd(1, GridW - S.W - 1);
		S.Y = Rnd(1, GridH - S.H - 1);
		S.CX = S.X + S.W / 2;
		S.CY = S.Y + S.H / 2;
		const float Score = FVector2D::Distance(FVector2D(S.CX, S.CY), FVector2D(CornerX, CornerY));
		if (Score < BestStartScore) { BestStartScore = Score; BestStart = S; }
	}
	Placed.Add(BestStart);
	CarveRoom(BestStart, StartRoom, ERoomKind::Start);
	const FRoomSpot StartRef = BestStart;

	// обычные шаблоны (кроме пост-условий)
	for (const FRoomTemplate& Tpl : Tpls)
	{
		if (IsPostCondition(Tpl.Condition)) continue;
		for (int32 k = 0; k < Tpl.Count && Rooms.Num() < MainCap; ++k)
		{
			FRoomSpot Spot;
			if (!FindPosition(Tpl, Placed, Tpl.Condition, &StartRef, Spot)) break;
			Placed.Add(Spot);
			CarveRoom(Spot, Tpl, ERoomKind::Normal);
		}
	}

	// финиш: максимально далеко от старта
	FRoomSpot BestEnd;
	float BestD = -1.f;
	for (int32 i = 0; i < 500; ++i)
	{
		FRoomSpot S;
		S.W = Rnd(EndRoom.WMin, FMath::Max(EndRoom.WMin, EndRoom.WMax));
		S.H = Rnd(EndRoom.HMin, FMath::Max(EndRoom.HMin, EndRoom.HMax));
		if (S.W + 2 >= GridW || S.H + 2 >= GridH) continue;
		S.X = Rnd(1, GridW - S.W - 1);
		S.Y = Rnd(1, GridH - S.H - 1);
		S.CX = S.X + S.W / 2;
		S.CY = S.Y + S.H / 2;
		bool Blocked = false;
		for (const FRoomSpot& P : Placed)
		{
			if (RectsOverlap(S, P, 2)) { Blocked = true; break; }
		}
		if (Blocked) continue;
		const float D = FVector2D::Distance(FVector2D(S.CX, S.CY), FVector2D(StartRef.CX, StartRef.CY));
		if (D > BestD) { BestD = D; BestEnd = S; }
	}
	if (BestD >= 0.f)
	{
		CarveRoom(BestEnd, EndRoom, ERoomKind::End);
	}

	// --- шаг 3: дороги (MST + петли, не больше одной на пару) ---
	TArray<TPair<int32, int32>> MST, Extra;
	BuildMST(MST);
	AddExtraEdges(MST, ExtraEdges, Extra);

	TArray<TPair<int32, int32>> All = MST;
	All.Append(Extra);

	// РАНЬШЕ ОШИБКА: MST-рёбра предзаполняли CarvedPairs, а ветка «уже есть» делала
	// continue, поэтому коридоры MST не строились вообще (при 2 комнатах дорог было 0).
	// Теперь строится каждое уникальное ребро, повтор пары пропускается.
	TSet<int64> CarvedPairs;
	int32 Roads = 0;
	for (const TPair<int32, int32>& E : All)
	{
		const int64 Key = (int64)FMath::Min(E.Key, E.Value) * 100000 + FMath::Max(E.Key, E.Value);
		if (CarvedPairs.Contains(Key)) continue; // между парой комнат не больше одной дороги
		CarvedPairs.Add(Key);

		CarveRoad(Rooms[E.Key].CenterX, Rooms[E.Key].CenterY,
		          Rooms[E.Value].CenterX, Rooms[E.Value].CenterY,
		          RoadWidthFor(E.Key, E.Value), E.Key, E.Value);
		++Roads;
	}

	// --- шаг 4: фаза 2 — пост-комнаты у дорог и комнат ---
	for (const FRoomTemplate& Tpl : Tpls)
	{
		if (!IsPostCondition(Tpl.Condition)) continue;
		for (int32 k = 0; k < Tpl.Count && Rooms.Num() < MaxRooms; ++k)
		{
			AddPostRoom(Tpl);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Dungeon: комнат %d, дорог %d (+ проходы к пост-комнатам)"),
	       Rooms.Num(), Roads);
}

// =====================================================================
//  Размещение комнат
// =====================================================================
TArray<FIntPoint> ADungeonGenerator::ShapeCells(int32 W, int32 H, ERoomShape Shape) const
{
	TArray<FIntPoint> Cells;
	const int32 Arm = FMath::Max(2, FMath::Min(W, H) / 3);
	for (int32 DY = 0; DY < H; ++DY)
	{
		for (int32 DX = 0; DX < W; ++DX)
		{
			bool bIn = false;
			switch (Shape)
			{
			case ERoomShape::Rect:
				bIn = true;
				break;
			case ERoomShape::Cross:
			{
				const float CXM = FMath::Abs(DX - (W - 1) / 2.f);
				const float CYM = FMath::Abs(DY - (H - 1) / 2.f);
				bIn = (CYM < Arm / 2.f + 0.01f) || (CXM < Arm / 2.f + 0.01f);
				break;
			}
			case ERoomShape::LShape:
			{
				const int32 CutW = FMath::Max(1, (int32)(W * 0.45f));
				const int32 CutH = FMath::Max(1, (int32)(H * 0.45f));
				bIn = !(DX >= W - CutW && DY < CutH);
				break;
			}
			case ERoomShape::Circle:
			{
				const float NX = (DX + 0.5f) / (W / 2.f) - 1.f;
				const float NY = (DY + 0.5f) / (H / 2.f) - 1.f;
				bIn = NX * NX + NY * NY <= 1.05f;
				break;
			}
			}
			if (bIn) Cells.Add(FIntPoint(DX, DY));
		}
	}
	return Cells;
}

bool ADungeonGenerator::RectsOverlap(const FRoomSpot& A, const FRoomSpot& B, int32 Margin) const
{
	return A.X < B.X + B.W + Margin && A.X + A.W + Margin > B.X &&
	       A.Y < B.Y + B.H + Margin && A.Y + A.H + Margin > B.Y;
}

bool ADungeonGenerator::ConditionOk(ERoomCondition Cond, int32 CX, int32 CY, const FRoomSpot* Start) const
{
	const float Diag = FMath::Sqrt((float)(GridW * GridW + GridH * GridH));
	switch (Cond)
	{
	case ERoomCondition::Edge:
		return CX <= 8 || CY <= 8 || CX >= GridW - 9 || CY >= GridH - 9;
	case ERoomCondition::NearStart:
		return Start && FVector2D::Distance(FVector2D(CX, CY), FVector2D(Start->CX, Start->CY)) <= Diag * 0.32f;
	case ERoomCondition::FarFromStart:
		return Start && FVector2D::Distance(FVector2D(CX, CY), FVector2D(Start->CX, Start->CY)) >= Diag * 0.48f;
	default:
		return true;
	}
}

bool ADungeonGenerator::FindPosition(const FRoomTemplate& Tpl, const TArray<FRoomSpot>& Placed,
                                     ERoomCondition Cond, const FRoomSpot* Start, FRoomSpot& Out)
{
	for (int32 i = 0; i < 400; ++i)
	{
		FRoomSpot S;
		S.W = Rnd(Tpl.WMin, FMath::Max(Tpl.WMin, Tpl.WMax));
		S.H = Rnd(Tpl.HMin, FMath::Max(Tpl.HMin, Tpl.HMax));
		if (S.W + 2 >= GridW || S.H + 2 >= GridH) continue;
		S.X = Rnd(1, GridW - S.W - 1);
		S.Y = Rnd(1, GridH - S.H - 1);
		S.CX = S.X + S.W / 2;
		S.CY = S.Y + S.H / 2;
		if (!ConditionOk(Cond, S.CX, S.CY, Start)) continue;
		bool Blocked = false;
		for (const FRoomSpot& P : Placed)
		{
			if (RectsOverlap(S, P, 2)) { Blocked = true; break; }
		}
		if (!Blocked) { Out = S; return true; }
	}
	return false;
}

void ADungeonGenerator::CarveRoom(const FRoomSpot& Spot, const FRoomTemplate& Tpl, ERoomKind Kind)
{
	FPlacedRoom Room;
	Room.X = Spot.X; Room.Y = Spot.Y; Room.W = Spot.W; Room.H = Spot.H;
	Room.CenterX = Spot.CX; Room.CenterY = Spot.CY;
	Room.Index = Rooms.Num();
	Room.Kind = Kind;
	Room.Color = Tpl.Color;
	Room.Shape = Tpl.Shape;   // нужно BuildGeometry(): по форме видно, что скруглять дугой

	// комната = 1 на сетке
	for (const FIntPoint& C : ShapeCells(Spot.W, Spot.H, Tpl.Shape))
	{
		const int32 GX = Spot.X + C.X;
		const int32 GY = Spot.Y + C.Y;
		if (InBounds(GX, GY))
		{
			Grid[Idx(GX, GY)] = 1;
			RoomMap[Idx(GX, GY)] = Room.Index;
		}
	}
	Rooms.Add(Room);
}

// =====================================================================
//  MST и дороги
// =====================================================================
void ADungeonGenerator::BuildMST(TArray<TPair<int32, int32>>& OutEdges) const
{
	const int32 N = Rooms.Num();
	if (N < 2) return;

	TArray<bool> InTree; InTree.Init(false, N);
	TArray<float> MinE;  MinE.Init(FLT_MAX, N);
	TArray<int32> From;  From.Init(-1, N);

	auto Dist = [&](int32 A, int32 B)
	{
		return FVector2D::Distance(FVector2D(Rooms[A].CenterX, Rooms[A].CenterY),
		                           FVector2D(Rooms[B].CenterX, Rooms[B].CenterY));
	};

	InTree[0] = true;
	for (int32 i = 0; i < N; ++i) { MinE[i] = Dist(0, i); From[i] = 0; }

	for (int32 Step = 1; Step < N; ++Step)
	{
		int32 Best = -1; float BestD = FLT_MAX;
		for (int32 i = 0; i < N; ++i)
		{
			if (!InTree[i] && MinE[i] < BestD) { BestD = MinE[i]; Best = i; }
		}
		if (Best == -1) break;
		InTree[Best] = true;
		OutEdges.Add(TPair<int32, int32>(From[Best], Best));
		for (int32 i = 0; i < N; ++i)
		{
			if (!InTree[i])
			{
				const float D = Dist(Best, i);
				if (D < MinE[i]) { MinE[i] = D; From[i] = Best; }
			}
		}
	}
}

void ADungeonGenerator::AddExtraEdges(const TArray<TPair<int32, int32>>& MST, int32 Count,
                                      TArray<TPair<int32, int32>>& Out) const
{
	TSet<int64> Existing;
	for (const TPair<int32, int32>& E : MST)
	{
		Existing.Add((int64)FMath::Min(E.Key, E.Value) * 100000 + FMath::Max(E.Key, E.Value));
	}

	struct FCand { int32 A, B; float D; };
	TArray<FCand> Cands;
	for (int32 i = 0; i < Rooms.Num(); ++i)
	{
		for (int32 j = i + 1; j < Rooms.Num(); ++j)
		{
			const int64 Key = (int64)i * 100000 + j;
			if (Existing.Contains(Key)) continue;
			const float D = FVector2D::Distance(FVector2D(Rooms[i].CenterX, Rooms[i].CenterY),
			                                    FVector2D(Rooms[j].CenterX, Rooms[j].CenterY));
			Cands.Add({ i, j, D });
		}
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.D < B.D; });
	for (int32 i = 0; i < FMath::Min(Count, Cands.Num()); ++i)
	{
		Out.Add(TPair<int32, int32>(Cands[i].A, Cands[i].B));
	}
}

int32 ADungeonGenerator::RoadWidthFor(int32 A, int32 B) const
{
	const int32 Size = FMath::Min(FMath::Max(Rooms[A].W, Rooms[A].H),
	                              FMath::Max(Rooms[B].W, Rooms[B].H));
	const int32 T1 = FMath::Min(Thr1, Thr2);
	const int32 T2 = FMath::Max(Thr1, Thr2);
	if (Size <= T1) return 1;
	if (Size <= T2) return 2;
	return 3;
}

// =====================================================================
//  Коридор: L-образный, из трёх ног (вход в комнату A, основная полоса, вход в
//  комнату B). Полосу сдвигаем так, чтобы коридор не шёл вдоль стены комнаты:
//  держим RoadRoomGap свободных клеток, а в комнату ведёт перпендикулярный вход.
// =====================================================================
bool ADungeonGenerator::RoadCellAllowed(int32 X, int32 Y, int32 IdxA, int32 IdxB, bool bAlongX) const
{
	if (RoadRoomGap <= 0) return true; // 0 = прежнее поведение: можно вплотную к комнате

	const int32 Self = RoomMap[Idx(X, Y)];
	if (Self >= 0 && Self != IdxA && Self != IdxB) return false; // клетка принадлежит чужой комнате

	for (int32 DY = -RoadRoomGap; DY <= RoadRoomGap; ++DY)
	{
		const int32 Span = RoadRoomGap - FMath::Abs(DY);
		for (int32 DX = -Span; DX <= Span; ++DX)
		{
			if (DX == 0 && DY == 0) continue;
			const int32 XX = X + DX, YY = Y + DY;
			if (!InBounds(XX, YY)) continue;

			const int32 RI = RoomMap[Idx(XX, YY)];
			if (RI < 0) continue;                          // камень или чужой коридор
			if (RI != IdxA && RI != IdxB) return false;    // прижались к чужой комнате

			const FPlacedRoom& R = Rooms[RI];
			const bool bInX = (X >= R.X && X < R.X + R.W);
			const bool bInY = (Y >= R.Y && Y < R.Y + R.H);
			if (bInX && bInY) continue;                    // клетка внутри своей комнаты
			if (bAlongX && !bInX && bInY) continue;        // вход в свою комнату через западную/восточную стену
			if (!bAlongX && bInX && !bInY) continue;       // вход через северную/южную стену
			return false;                                  // коридор едет вдоль стены своей комнаты
		}
	}
	return true;
}

void ADungeonGenerator::BuildRoadSegs(int32 Opt, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 Lane,
                                      TArray<FRoadSeg>& Out) const
{
	Out.Reset();
	if (Opt == 0)
	{
		// вход по строке Y1 -> вертикальная полоса Lane -> вход по строке Y2
		Out.Add({ true,  FMath::Min(X1, Lane), FMath::Max(X1, Lane), Y1 });
		Out.Add({ false, FMath::Min(Y1, Y2),   FMath::Max(Y1, Y2),   Lane });
		Out.Add({ true,  FMath::Min(Lane, X2), FMath::Max(Lane, X2), Y2 });
	}
	else
	{
		// вход по столбцу X1 -> горизонтальная полоса Lane -> вход по столбцу X2
		Out.Add({ false, FMath::Min(Y1, Lane), FMath::Max(Y1, Lane), X1 });
		Out.Add({ true,  FMath::Min(X1, X2),   FMath::Max(X1, X2),   Lane });
		Out.Add({ false, FMath::Min(Lane, Y2), FMath::Max(Lane, Y2), X2 });
	}
}

// Клетка, в которой сходятся обе ноги, допустима, если легальна хотя бы одна из ног:
// в углу коридора направление подхода выбирается поворота, а не координатой клетки.
int32 ADungeonGenerator::ScoreRoadConfig(const TArray<FRoadSeg>& Segs, int32 Width, int32 IdxA, int32 IdxB,
                                         TArray<uint8>& Mask, TArray<int32>& Touched) const
{
	const int32 Half = Width / 2;
	int32 OutsideField = 0;
	Touched.Reset();

	for (const FRoadSeg& Seg : Segs)
	{
		const uint8 Bit = Seg.bAlongX ? 1 : 2;
		for (int32 A = Seg.S; A <= Seg.E; ++A)
		{
			for (int32 k = -Half; k <= Width - 1 - Half; ++k)
			{
				const int32 X = Seg.bAlongX ? A : Seg.Fixed + k;
				const int32 Y = Seg.bAlongX ? Seg.Fixed + k : A;
				if (!InBounds(X, Y)) { ++OutsideField; continue; } // вылезло за край поля — меньший штраф
				const int32 I = Idx(X, Y);
				if (!Mask[I]) Touched.Add(I);
				Mask[I] = (uint8)(Mask[I] | Bit);
			}
		}
	}

	int32 Hugs = 0;
	for (const int32 I : Touched)
	{
		const int32 X = I % GridW;
		const int32 Y = I / GridW;
		const bool bH = (Mask[I] & 1) != 0;
		const bool bV = (Mask[I] & 2) != 0;
		const bool bOkH = bH && RoadCellAllowed(X, Y, IdxA, IdxB, true);
		const bool bOkV = bV && RoadCellAllowed(X, Y, IdxA, IdxB, false);
		if (!bOkH && !bOkV) ++Hugs;
	}

	for (const int32 I : Touched) Mask[I] = 0;

	// касания комнат весят намного больше, чем вылезание за край поля
	return Hugs * 1000 + OutsideField;
}

void ADungeonGenerator::CarveRoad(int32 X1, int32 Y1, int32 X2, int32 Y2, int32 Width, int32 IdxA, int32 IdxB)
{
	const int32 Half = Width / 2;
	const bool  bKeepGap = (RoadRoomGap > 0);
	const int32 Search   = FMath::Max(0, RoadLaneSearch);

	TArray<FRoadSeg> Segs;
	TArray<FRoadSeg> BestSegs;
	TArray<uint8>    Mask;
	TArray<int32>    Touched;
	if (bKeepGap) Mask.Init(0, GridW * GridH);

	int32 BestScore = TNumericLimits<int32>::Max();
	int32 BestShift = TNumericLimits<int32>::Max();

	// Перебираем оба прежних излома (Opt 0 = как старая ветка «else», Opt 1 = как «if»)
	// и сдвиги основной полосы: 0, +1, -1, +2, -2 ... Выбираем наименьший штраф,
	// при равном штрафе — меньший сдвиг, при равном сдвиге — монетка Stream (разнообразие).
	for (int32 Opt = 0; Opt < 2; ++Opt)
	{
		const int32 Nominal = (Opt == 0) ? X2 : Y2; // полоса: столбец X2 или строка Y2
		for (int32 D = 0; D <= Search; ++D)
		{
			for (int32 Sign = 0; Sign < (D == 0 ? 1 : 2); ++Sign)
			{
				const int32 Lane = Nominal + (Sign == 0 ? D : -D);
				if (Lane < 0 || Lane >= (Opt == 0 ? GridW : GridH)) continue;

				BuildRoadSegs(Opt, X1, Y1, X2, Y2, Lane, Segs);
				const int32 Score = bKeepGap ? ScoreRoadConfig(Segs, Width, IdxA, IdxB, Mask, Touched) : 0;

				const bool bTake = (Score < BestScore) ||
				                   (Score == BestScore && D < BestShift) ||
				                   (Score == BestScore && D == BestShift && Stream.FRand() < 0.5f);
				if (bTake)
				{
					BestScore = Score;
					BestShift = D;
					BestSegs  = Segs;
				}
			}
		}
	}

	if (BestSegs.Num() == 0) BuildRoadSegs(0, X1, Y1, X2, Y2, X2, BestSegs);

	for (const FRoadSeg& Seg : BestSegs)
	{
		for (int32 A = Seg.S; A <= Seg.E; ++A)
		{
			for (int32 k = -Half; k <= Width - 1 - Half; ++k)
			{
				const int32 X = Seg.bAlongX ? A : Seg.Fixed + k;
				const int32 Y = Seg.bAlongX ? Seg.Fixed + k : A;
				if (!InBounds(X, Y)) continue;
				Grid[Idx(X, Y)] = 1;   // дороги не ломают комнаты — 1 остаётся 1
				if (RoadMask.Num() == GridW * GridH) RoadMask[Idx(X, Y)] = 1;   // нужно скруглению круглых комнат
			}
		}
	}

	// Если чисто развести не удалось (комнаты стоят слишком плотно), берём вариант с
	// наименьшим числом касаний: подземелье всё равно обязано оставаться связным.
	if (BestScore >= 1000)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Dungeon: коридор %d -> %d не удалось отодвинуть от комнат (касаний: %d)"),
		       IdxA, IdxB, BestScore / 1000);
	}
}

// =====================================================================
//  Пост-комнаты (кладовки у дорог и комнат)
// =====================================================================
void ADungeonGenerator::ShufflePoints(TArray<FIntPoint>& Arr)
{
	for (int32 i = Arr.Num() - 1; i > 0; --i)
	{
		const int32 j = Stream.RandHelper(i + 1);
		Arr.Swap(i, j);
	}
}

bool ADungeonGenerator::FindPostPosition(const FRoomTemplate& Tpl, ERoomCondition Mode, FRoomSpot& Out)
{
	const int32 POST_MARGIN = 2; // пост-комнаты держат дистанцию друг от друга

	TArray<FIntPoint> Anchors;
	for (int32 Y = 1; Y < GridH - 1; ++Y)
	{
		for (int32 X = 1; X < GridW - 1; ++X)
		{
			if (Mode == ERoomCondition::NextToRoad && Grid[Idx(X, Y)] == 1 && RoomMap[Idx(X, Y)] == -1)
			{
				Anchors.Add(FIntPoint(X, Y)); // только клетки дорог, не комнат
			}
			else if (Mode == ERoomCondition::NextToRoom && RoomMap[Idx(X, Y)] >= 0)
			{
				Anchors.Add(FIntPoint(X, Y));
			}
		}
	}
	if (Anchors.Num() == 0) return false;
	ShufflePoints(Anchors);

	// Коробка пост-комнаты не должна стоять вплотную к коридору: иначе проход
	// вольётся в комнату без стены, и комната сольётся с дорогой.
	auto BoxClearOfRoads = [&](int32 X, int32 Y, int32 W, int32 H) -> bool
	{
		const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				for (const FIntPoint& D : Dirs)
				{
					const int32 NX = XX + D.X, NY = YY + D.Y;
					if (!InBounds(NX, NY)) continue;
					if (Grid[Idx(NX, NY)] == 1 && RoomMap[Idx(NX, NY)] < 0) return false; // сосед — клетка дороги
				}
			}
		}
		return true;
	};

	auto IsValidBox = [&](int32 X, int32 Y, int32 W, int32 H, int32 AnchorRoomIdx, bool bKeepRoadClear) -> bool
	{
		if (X < 1 || Y < 1 || X + W >= GridW - 1 || Y + H >= GridH - 1) return false;
		// сама коробка — на чистом камне
		for (int32 YY = Y; YY < Y + H; ++YY)
			for (int32 XX = X; XX < X + W; ++XX)
				if (Grid[Idx(XX, YY)] != 0 || RoomMap[Idx(XX, YY)] != -1) return false;
		// буфер вокруг: никаких чужих комнат (сосед-якорь исключён)
		for (int32 YY = Y - POST_MARGIN; YY < Y + H + POST_MARGIN; ++YY)
		{
			for (int32 XX = X - POST_MARGIN; XX < X + W + POST_MARGIN; ++XX)
			{
				if (!InBounds(XX, YY)) continue;
				const int32 RI = RoomMap[Idx(XX, YY)];
				if (RI >= 0 && RI != AnchorRoomIdx) return false;
			}
		}
		// и от коридоров держим свободную клетку: стена пост-комнаты остаётся целой
		if (bKeepRoadClear && !BoxClearOfRoads(X, Y, W, H)) return false;
		return true;
	};

	const int32 Limit = FMath::Min(Anchors.Num(), 120);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		// Pass 0 — требовательно: пост-комнату ставим так, чтобы до коридора оставалась
		// свободная клетка. Pass 1 — прежние правила, чтобы генерация вообще не осталась
		// без пост-комнат, когда места впритык больше нигде нет.
		const bool bKeepRoadClear = (RoadRoomGap > 0) && (Pass == 0);

		for (int32 A = 0; A < Limit; ++A)
		{
			const FIntPoint Anchor = Anchors[A];
			const int32 AnchorRoom = (Mode == ERoomCondition::NextToRoom) ? RoomMap[Idx(Anchor.X, Anchor.Y)] : -1;
			for (int32 Attempt = 0; Attempt < 10; ++Attempt)
			{
				FRoomSpot S;
				S.W = Rnd(Tpl.WMin, FMath::Max(Tpl.WMin, Tpl.WMax));
				S.H = Rnd(Tpl.HMin, FMath::Max(Tpl.HMin, Tpl.HMax));
				const int32 Side = Rnd(0, 3);
				if (Side == 0)      { S.X = Anchor.X + 2;                       S.Y = Anchor.Y - Rnd(0, FMath::Max(0, S.H - 1)); }
				else if (Side == 1) { S.X = Anchor.X - S.W - 1;                 S.Y = Anchor.Y - Rnd(0, FMath::Max(0, S.H - 1)); }
				else if (Side == 2) { S.X = Anchor.X - Rnd(0, FMath::Max(0, S.W - 1)); S.Y = Anchor.Y + 2; }
				else                { S.X = Anchor.X - Rnd(0, FMath::Max(0, S.W - 1)); S.Y = Anchor.Y - S.H - 1; }
				S.CX = S.X + S.W / 2;
				S.CY = S.Y + S.H / 2;
				S.Anchor = Anchor;
				S.AnchorRoom = AnchorRoom;
				if (IsValidBox(S.X, S.Y, S.W, S.H, AnchorRoom, bKeepRoadClear))
				{
					Out = S;
					return true;
				}
			}
		}
	}
	return false;
}

void ADungeonGenerator::AddPostRoom(const FRoomTemplate& Tpl)
{
	FRoomSpot Spot;
	if (!FindPostPosition(Tpl, Tpl.Condition, Spot)) return;

	CarveRoom(Spot, Tpl, ERoomKind::Post);
	const int32 NewIdx = Rooms.Num() - 1;

	// короткий проход: комната уже стоит рядом, просто соединяем до её центра
	// Проход к пост-комнате: комната стоит на своём месте, но и тут держим отступ
	// от чужих коридоров и комнат — иначе пост-комната сольётся с дорогой.
	CarveRoad(Spot.Anchor.X, Spot.Anchor.Y, Rooms[NewIdx].CenterX, Rooms[NewIdx].CenterY,
	          RoadWidthFor(NewIdx, NewIdx), NewIdx, NewIdx);
}

// =====================================================================
//  Очистка: убирает ВСЁ, что построено предыдущим Generate()
// =====================================================================
void ADungeonGenerator::ClearMarkers()
{
	// 1) маркеры, которые помнит этот актор
	for (APointLight* M : Markers)
	{
		if (IsValid(M)) M->Destroy();
	}
	Markers.Empty();

	UWorld* World = GetWorld();
	if (!World) return;

	// 2) маркеры, которые остались в уровне без нас: после хот-релоада, компиляции
	// Blueprint, загрузки сохранённого уровня или если генератор удалили и поставили
	// заново. Ищем по тегу во всём уровне. Сначала собираем в список — удалять акторов
	// прямо во время прохода TActorIterator нельзя.
	TArray<APointLight*> Leftovers;
	for (TActorIterator<APointLight> It(World); It; ++It)
	{
		APointLight* Light = *It;
		if (IsValid(Light) && Light->ActorHasTag(DungeonMarkerTag))
		{
			Leftovers.Add(Light);
		}
	}
	for (APointLight* Light : Leftovers)
	{
		if (IsValid(Light)) Light->Destroy();
	}

	if (Leftovers.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Dungeon cleanup: удалено маркеров из прошлых генераций: %d"),
		       Leftovers.Num());
	}
}

void ADungeonGenerator::ClearGenerated()
{
	ClearMarkers();

	if (WallsMesh) WallsMesh->ClearAllMeshSections();
	if (RoofMesh) RoofMesh->ClearAllMeshSections();
	if (FloorMesh) FloorMesh->ClearAllMeshSections();

	// Пол из прямоугольников: без этого от прежней сетки остаются висеть его секции.
	// Старая растянутая плоскость всегда спрятана.
	if (FloorPlane)
	{
		FloorPlane->SetVisibility(false);
		FloorPlane->SetHiddenInGame(true);
	}

	Grid.Reset();
	RoomMap.Reset();
	RoadMask.Reset();
	Rooms.Reset();
}

// Кнопка «Сбросить типы комнат»: вернуть встроенный набор типов (5 штук).
// StartRoom и EndRoom не трогаем — их размеры и форма могут быть подобраны вручную.
void ADungeonGenerator::ResetRoomTemplates()
{
	Templates = MakeDefaultTemplates();
}

// =====================================================================
//  СКРУГЛЕНИЕ КОМНАТ ФОРМЫ «КРУГЛАЯ»: грани стены ПЕРЕСТАВЛЯЮТСЯ
//
//  Ничего нового к стене не добавляется. Стену и так составляет одна грань на
//  каждую границу камня и открытой клетки. У комнаты формы «Круглая» те же
//  самые грани ставятся чуть назад: концы грани подтягиваются на эллипс,
//  вписанный в прямоугольник комнаты (тот же эллипс, по которому комната
//  вырезана). Грань остаётся своей — у той же клетки и на том же участке
//  границы, — но лесенка из таких граней превращается в многоугольник,
//  вписанный в ту же клетчатую область, как в макете.
//
//  Переставленная грань при этом не повисает: между ней и прежним ребром клетки
//  остаётся полоса, и её закрывает ТА ЖЕ крышка (или плита пола) клетки, чьё
//  общее с соседом ребро заменено на ломаную по краю стены — крышка каменной
//  клетки дотягивается до верхнего края грани, плита пола клетки комнаты — до
//  нижнего. Отдельных плит примыкания нет: полигонов крыши и пола ровно столько
//  же, сколько клеток они накрывают, а углы крыла — те же точки, что у края
//  стены, поэтому стена соединена с крышей, полом и соседними гранями встык:
//  оболочка без щелей, как у прямоугольных комнат.
//
//  Там, где переставленная грань легла бы поперёк входа или коридора, она
//  остаётся на прежнем месте, а её углы у соседних переставленных граней
//  тоже остаются на сетке — иначе между гранями осталась бы щель.
//
//  WallDirs — тот же порядок направлений, что и обход стен в BuildGeometry:
//  индекс направления это часть ключа маски «грань переставили».
// =====================================================================

struct FWallDir
{
	int32   DX, DY;      // смещение к открытой клетке; оно же — нормаль грани (внутрь подземелья)
	bool    bRunAlongX;  // по какой оси соседствуют одинаковые грани (вдоль неё тянем ряд)
	FVector Normal;
};

const FWallDir WallDirs[4] = {
	// Юг (Y+): открытая клетка по +Y; ряд таких граней идёт вдоль X
	{ 0,  1, true,  FVector(0,  1, 0) },
	// Север (Y-): открытая клетка по -Y
	{ 0, -1, true,  FVector(0, -1, 0) },
	// Восток (X+): открытая клетка по +X; ряд таких граней идёт вдоль Y
	{ 1,  0, false, FVector(1,  0, 0) },
	// Запад (X-): открытая клетка по -X
	{ -1, 0, false, FVector(-1, 0, 0) },
};

static const int32 WallDirCount = 4;

// индекс направления к соседу в том же порядке, что и обход стен
static int32 DirIndexOfNeighbour(int32 DX, int32 DY)
{
	for (int32 i = 0; i < WallDirCount; ++i)
	{
		if (WallDirs[i].DX == DX && WallDirs[i].DY == DY) return i;
	}
	return 0;
}

static float WrapAngle2Pi(float Angle)
{
	const float Two = 2.f * UE_PI;
	Angle = FMath::Fmod(Angle, Two);
	if (Angle < 0.f) Angle += Two;
	return Angle;
}

static float WrapAngleSigned(float Angle)
{
	const float Two = 2.f * UE_PI;
	while (Angle >  UE_PI) Angle -= Two;
	while (Angle < -UE_PI) Angle += Two;
	return Angle;
}

// Угол точки вокруг центра эллипса в ПАРаметрическом смысле: у самой эллипса
// такой угол uniform по обходу, поэтому им удобно сортировать грани по кругу.
static float RoundParamAngle(const FVector2D& Center, const FVector2D& Semi, const FVector2D& P)
{
	return FMath::Atan2((P.Y - Center.Y) / Semi.Y, (P.X - Center.X) / Semi.X);
}

// Точка эллипса на луче из центра через P: луч тянем до самого эллипса.
static FVector2D RoundPointOnEllipse(const FVector2D& Center, const FVector2D& Semi, const FVector2D& P)
{
	const float DX = (P.X - Center.X) / Semi.X;
	const float DY = (P.Y - Center.Y) / Semi.Y;
	const float R2 = DX * DX + DY * DY;
	if (R2 < KINDA_SMALL_NUMBER) return Center;

	const float S = 1.f / FMath::Sqrt(R2);
	return FVector2D(Center.X + (P.X - Center.X) * S, Center.Y + (P.Y - Center.Y) * S);
}

static void RoundEdgeEnds(int32 X, int32 Y, int32 DX, int32 DY, FVector2D& OutA, FVector2D& OutB)
{
	if (DX != 0)
	{
		const float EX = (DX > 0) ? static_cast<float>(X + 1) : static_cast<float>(X);
		OutA = FVector2D(EX, static_cast<float>(Y));
		OutB = FVector2D(EX, static_cast<float>(Y + 1));
	}
	else
	{
		const float EY = (DY > 0) ? static_cast<float>(Y + 1) : static_cast<float>(Y);
		OutA = FVector2D(static_cast<float>(X), EY);
		OutB = FVector2D(static_cast<float>(X + 1), EY);
	}
}

static bool SameGridPoint(const FVector2D& A, const FVector2D& B)
{
	return FMath::Abs(A.X - B.X) < KINDA_SMALL_NUMBER && FMath::Abs(A.Y - B.Y) < KINDA_SMALL_NUMBER;
}

// Клетка сетки открыта и принадлежит именно этой комнате.
bool ADungeonGenerator::RoomCellOwned(int32 X, int32 Y, int32 RoomIndex) const
{
	if (!InBounds(X, Y)) return false;
	const int32 I = Idx(X, Y);
	if (Grid[I] == 0) return false;
	return RoomMap[I] == RoomIndex;
}

// Нельзя переставлять грань, если её хорда пересекает чужую открытую клетку:
// вход в комнату, другую комнату или коридор (дороги не ломают клетки комнат,
// поэтому клетки коридора, идущего сквозь комнату, по-прежнему принадлежат
// комнате — для них важен RoadMask). Такая грань остаётся на прежнем месте.
bool ADungeonGenerator::RoundChordCrossesPassage(float AX, float AY, float BX, float BY, int32 RoomIndex) const
{
	const float DX = BX - AX;
	const float DY = BY - AY;
	const float Len = FMath::Sqrt(DX * DX + DY * DY);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len * 4.f));   // шаг ~0.25 клетки
	const bool bRoadMask = (RoadMask.Num() == GridW * GridH);

	for (int32 s = 1; s < Steps; ++s)   // концы не смотрим: они стоят на самой границе
	{
		const float T = static_cast<float>(s) / static_cast<float>(Steps);
		const int32 CX = FMath::FloorToInt(AX + DX * T);
		const int32 CY = FMath::FloorToInt(AY + DY * T);
		if (!InBounds(CX, CY)) continue;

		const int32 I = Idx(CX, CY);
		if (Grid[I] == 0) continue;                          // камень: грань в камне проходу не мешает
		if (RoomMap[I] == RoomIndex && (!bRoadMask || RoadMask[I] == 0)) continue;   // своя клетка комнаты

		return true;
	}
	return false;
}

// Полоса между прежним ребром клетки и переставленной гранью — это то, чем
// стена теперь НЕ закрыта снаружи. С какой стороны границы она легла, там и
// не хватает плиты: в клетке комнаты пол есть, а крышки нет; в клетке камня
// крышка есть, а пола нет.
// Отсечь многоугольник полупрямой: остаются вершины и точки пересечения со
// стороной Dot(X - P, N) <= 0. Нужно, чтобы крыло клетки не лезло в чужую клетку:
// у одного конца грани полоса может лежать по эту сторону ребра, а у другого —
// по ту, и тогда наружу клетки идёт только часть полосы.
// Углы клетки (X..X+1, Y..Y+1) в порядке обхода против часовой: ребро i идёт от
// Corner[i] к Corner[i+1], соседняя клетка при этом остаётся справа от обхода.
// Пересекаются ли отрезки AB и CD во внутренней точке (совпадение концов не считается).
static bool SegmentsProperlyCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
	auto Cross = [](const FVector2D& O, const FVector2D& U, const FVector2D& V)
	{
		return (U.X - O.X) * (V.Y - O.Y) - (U.Y - O.Y) * (V.X - O.X);
	};

	const float D1 = Cross(C, D, A);
	const float D2 = Cross(C, D, B);
	const float D3 = Cross(A, B, C);
	const float D4 = Cross(A, B, D);

	return ((D1 > 0.f && D2 < 0.f) || (D1 < 0.f && D2 > 0.f)) &&
	       ((D3 > 0.f && D4 < 0.f) || (D3 < 0.f && D4 > 0.f));
}

// Прост ли небольшой многоугольник: его стороны не должны пересекаться, и он не
// должен выродиться в линию (иначе плита пола или крышки станет щелью-треугольником).
static bool PolygonIsSimpleSmall(const TArray<FVector2D>& Poly)
{
	const int32 Num = Poly.Num();
	if (Num < 3) return false;

	double Area2 = 0.0;
	for (int32 i = 0; i < Num; ++i)
	{
		const FVector2D& P = Poly[i];
		const FVector2D& Q = Poly[(i + 1) % Num];
		Area2 += static_cast<double>(P.X) * static_cast<double>(Q.Y)
		       - static_cast<double>(Q.X) * static_cast<double>(P.Y);
	}
	if (FMath::Abs(Area2) < 1e-4) return false;

	for (int32 i = 0; i < Num; ++i)
	{
		for (int32 j = i + 1; j < Num; ++j)
		{
			if (j == i + 1) continue;
			if (i == 0 && j == Num - 1) continue;   // соседние стороны смежны, это не пересечение
			if (SegmentsProperlyCross(Poly[i], Poly[(i + 1) % Num], Poly[j], Poly[(j + 1) % Num]))
				return false;
		}
	}
	return true;
}

static float PolygonSignedArea2(const TArray<FVector2D>& Poly)
{
	float Sum = 0.f;
	const int32 Num = Poly.Num();
	for (int32 i = 0; i < Num; ++i)
	{
		const FVector2D& A = Poly[i];
		const FVector2D& B = Poly[(i + 1) % Num];
		Sum += A.X * B.Y - A.Y * B.X;
	}
	return Sum;   // удвоенная площадь со знаком: > 0 — обход против часовой
}

// Ломаную режем полуплоскостью: остаются участки с Dot(X - P, N) <= 0, на
// границе появляется точка пересечения. Крылом режем саму переставленную грань
// (хорду), а не четырёхугольник полосы: если хорда пересекла ребро клетки,
// такой четырёхугольник самопересекающийся, и резать его нельзя.
static bool PointInTri(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	const float D1 = (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
	const float D2 = (C.X - B.X) * (P.Y - B.Y) - (C.Y - B.Y) * (P.X - B.X);
	const float D3 = (A.X - C.X) * (P.Y - C.Y) - (A.Y - C.Y) * (P.X - C.X);

	// обход против часовой: точка на ребре считается внутри, чтобы такой угол
	// ухом не становился
	const float Eps = -1e-4f;
	return D1 >= Eps && D2 >= Eps && D3 >= Eps;
}

// Сторона точки относительно прямой через A и B: знак косого произведения.
static float LineSide2D(const FVector2D& A, const FVector2D& B, const FVector2D& P)
{
	return (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
}

// Отсечь от многоугольника всё, что лежит по другую сторону ПРЯМОЙ через A и B:
// остаётся та сторона, на которой стоит точка Keep. Точки, лежащие ровно на
// прямой, достаются обеим сторонам, поэтому разрезанная плита и стена встречаются
// в одной и той же точке, без щели и без нахлёста.
static void ClipPolyToSide(const TArray<FVector2D>& In, const FVector2D& A, const FVector2D& B,
                           const FVector2D& Keep, TArray<FVector2D>& Out)
{
	Out.Reset();
	const int32 Num = In.Num();
	if (Num < 3) return;

	const float KeepSide = LineSide2D(A, B, Keep);
	if (FMath::Abs(KeepSide) < 1e-6f) return;

	Out.Reserve(Num + 2);
	for (int32 i = 0; i < Num; ++i)
	{
		const FVector2D& P = In[i];
		const FVector2D& Q = In[(i + 1) % Num];
		const float SP = LineSide2D(A, B, P);
		const float SQ = LineSide2D(A, B, Q);
		const bool bInP = (SP * KeepSide) >= -1e-6f;
		const bool bInQ = (SQ * KeepSide) >= -1e-6f;

		if (bInP) Out.Add(P);
		if (bInP != bInQ)
		{
			const float T = SP / (SP - SQ);
			Out.Add(FVector2D(P.X + (Q.X - P.X) * T, P.Y + (Q.Y - P.Y) * T));
		}
	}
}

// Режет ли ОТРЕЗОК A-B выпуклый многоугольник (а не только прямая через него):
// обе половины остаются кусками многоугольника, а новый край разреза лежит на самом
// отрезке, а не на его продолжении. Положины возвращаются в OutFirst (сторона точки
// FirstRef) и OutSecond; если разреза нет — обе пусты.
static bool SplitConvexBySegment(const FVector2D& A, const FVector2D& B, const TArray<FVector2D>& In,
                                 const FVector2D& FirstRef, const FVector2D& SecondRef,
                                 TArray<FVector2D>& OutFirst, TArray<FVector2D>& OutSecond)
{
	OutFirst.Reset();
	OutSecond.Reset();

	const int32 Num = In.Num();
	if (Num < 3) return false;

	const float DX = B.X - A.X;
	const float DY = B.Y - A.Y;
	const float Len2 = DX * DX + DY * DY;
	if (Len2 < 1e-8f) return false;

	ClipPolyToSide(In, A, B, FirstRef, OutFirst);
	ClipPolyToSide(In, A, B, SecondRef, OutSecond);
	if (OutFirst.Num() < 3 || OutSecond.Num() < 3)
	{
		OutFirst.Reset();
		OutSecond.Reset();
		return false;
	}

	const float AreaIn = FMath::Abs(PolygonSignedArea2(In));
	const float Area1 = FMath::Abs(PolygonSignedArea2(OutFirst));
	const float Area2 = FMath::Abs(PolygonSignedArea2(OutSecond));
	if (Area1 < 1e-4f || Area2 < 1e-4f)
	{
		OutFirst.Reset();
		OutSecond.Reset();
		return false;
	}
	if (Area1 + Area2 < AreaIn - 1e-3f)
	{
		OutFirst.Reset();
		OutSecond.Reset();
		return false;
	}

	// общий край положинок лежит ровно на прямой A-B: по нему и проверяем, что
	// разрез случился на самом отрезке, а не на его продолжении
	float TMin = 1e30f, TMax = -1e30f;
	for (const FVector2D& P : OutFirst)
	{
		if (FMath::Abs(LineSide2D(A, B, P)) > 1e-4f) continue;
		const float T = ((P.X - A.X) * DX + (P.Y - A.Y) * DY) / Len2;   // 0..1 = на отрезке
		TMin = FMath::Min(TMin, T);
		TMax = FMath::Max(TMax, T);
	}
	if (TMax < -1e-3f || TMin > 1.f + 1e-3f)
	{
		OutFirst.Reset();
		OutSecond.Reset();
		return false;
	}
	if (FMath::Min(TMax, 1.f) - FMath::Max(TMin, 0.f) < 1e-3f)
	{
		OutFirst.Reset();
		OutSecond.Reset();
		return false;
	}
	return true;
}

// Разрезание многоугольника «обрезкой ушей» (ear clipping): ухо — выпуклый угол,
// в треугольник которого не попадает ни одна другая вершина. Клетка с крылом
// невыпуклая, а веер от одной вершины в невыпуклом многоугольнике даёт
// треугольники снаружи многоугольника — они и выглядели как чёрные дыры.
// Индексы в OutTris — индексы вершин Poly.
static int32 EarClipTriangulate(const TArray<FVector2D>& Poly, TArray<int32>& OutTris)
{
	const int32 Num = Poly.Num();
	if (Num < 3) return 0;

	TArray<int32> Ring;
	Ring.Reserve(Num);
	for (int32 i = 0; i < Num; ++i) Ring.Add(i);

	int32 Guard = 0;
	while (Ring.Num() > 3 && Guard++ < Num * 4)
	{
		const int32 M = Ring.Num();
		bool bCut = false;

		for (int32 i = 0; i < M; ++i)
		{
			const int32 IA = Ring[(i + M - 1) % M];
			const int32 IB = Ring[i];
			const int32 IC = Ring[(i + 1) % M];

			const FVector2D& A = Poly[IA];
			const FVector2D& B = Poly[IB];
			const FVector2D& C = Poly[IC];

			const float Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
			if (Cross <= KINDA_SMALL_NUMBER) continue;   // рефлексивный или вырожденный угол

			bool bOtherInside = false;
			for (int32 j = 0; j < M; ++j)
			{
				const int32 IJ = Ring[j];
				if (IJ == IA || IJ == IB || IJ == IC) continue;
				if (PointInTri(Poly[IJ], A, B, C)) { bOtherInside = true; break; }
			}
			if (bOtherInside) continue;

			OutTris.Add(IA);
			OutTris.Add(IB);
			OutTris.Add(IC);
			Ring.RemoveAt(i);
			bCut = true;
			break;
		}

		if (!bCut) break;   // многоугольник самопересекающийся: дальше резать нельзя
	}

	if (Ring.Num() == 3)
	{
		const FVector2D& A = Poly[Ring[0]];
		const FVector2D& B = Poly[Ring[1]];
		const FVector2D& C = Poly[Ring[2]];
		const float Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
		if (Cross > KINDA_SMALL_NUMBER)
		{
			OutTris.Add(Ring[0]);
			OutTris.Add(Ring[1]);
			OutTris.Add(Ring[2]);
		}
	}

	return OutTris.Num() / 3;
}

// =====================================================================
//  ПЕРЕСТАВЛЕНИЕ ГРАНИЦ КОМНАТ ФОРМЫ «КРУГЛАЯ»
//  Переставляются НЕ полигоны, а УЗЛЫ СЕТКИ: угол между четырьмя клетками.
//  Каждый такой узел получает ОДНО новое положение — луч из центра комнаты
//  тянется до эллипса комнаты. То же положение используют ВСЕ полигоны,
//  которые сходятся в этом узле: грань стены, крышка клетки камня и плита
//  пола клетки комнаты. Поэтому подземелье остаётся одной сеткой без разрезов:
//  в месте соприкосновения стены с крышей и полом стоит одна и та же точка,
//  и новых полигонов не появляется — их ровно столько, сколько было.
//  Узел, у которого хоть одна грань осталась на прежнем месте (у входа или там,
//  где через комнату идёт коридор), не двигают вовсе: иначе стена разошлась бы
//  со своими соседями и в оболочке возник бы разрез.
// =====================================================================
void ADungeonGenerator::BuildRoundRooms(TArray<FRoundRoom>& OutRooms, TArray<uint8>& OutSkip,
                                        TArray<FVector2D>& OutCornerPos, TArray<uint8>& OutCornerMoved) const
{
	OutRooms.Reset();
	OutSkip.Init(0, GridW * GridH * WallDirCount);

	const int32 CornerNum = (GridW + 1) * (GridH + 1);
	OutCornerMoved.Init(0, CornerNum);
	OutCornerPos.Init(FVector2D(0.f, 0.f), CornerNum);

	if (!bRoundCircleWalls) return;
	if (Grid.Num() != GridW * GridH || RoomMap.Num() != GridW * GridH) return;

	auto CornerId = [&](int32 CX, int32 CY) -> int32 { return CY * (GridW + 1) + CX; };
	auto CornerPoint = [&](int32 CX, int32 CY) -> FVector2D
	{
		return FVector2D(static_cast<float>(CX), static_cast<float>(CY));
	};
	auto CornerX = [&](int32 Id) -> int32 { return Id % (GridW + 1); };
	auto CornerY = [&](int32 Id) -> int32 { return Id / (GridW + 1); };

	// 1) Собираем ВСЕ грани каждой круглой комнаты — ровно те, что обход стен уже
	//    ставит на границе камня и открытой клетки комнаты.
	TArray<FRoundRoom> Cand;
	for (const FPlacedRoom& Room : Rooms)
	{
		if (Room.Shape != ERoomShape::Circle) continue;
		if (Room.W < 4 || Room.H < 4) continue;   // на 3 клетках переставлять нечего

		FRoundRoom R;
		R.RoomIndex = Room.Index;
		R.Center = FVector2D(static_cast<float>(Room.X) + Room.W * 0.5f,
		                     static_cast<float>(Room.Y) + Room.H * 0.5f);
		R.Semi   = FVector2D(Room.W * 0.5f, Room.H * 0.5f);

		for (int32 Y = Room.Y; Y < Room.Y + Room.H; ++Y)
		{
			for (int32 X = Room.X; X < Room.X + Room.W; ++X)
			{
				if (!RoomCellOwned(X, Y, Room.Index)) continue;

				for (int32 Di = 0; Di < WallDirCount; ++Di)
				{
					const FWallDir& Dir = WallDirs[Di];
					const int32 NX = X + Dir.DX;
					const int32 NY = Y + Dir.DY;
					if (!InBounds(NX, NY)) continue;
					if (Grid[Idx(NX, NY)] != 0) continue;   // сосед открыт: это вход, стены тут нет

					FRoundFace F;
					RoundEdgeEnds(X, Y, Dir.DX, Dir.DY, F.A0, F.B0);
					F.A = F.A0;
					F.B = F.B0;
					F.CornerA = CornerId(FMath::RoundToInt(F.A0.X), FMath::RoundToInt(F.A0.Y));
					F.CornerB = CornerId(FMath::RoundToInt(F.B0.X), FMath::RoundToInt(F.B0.Y));
					// у камня грань смотрит в обратную сторону — к открытой клетке
					F.RockCell = FIntPoint(NX, NY);
					F.OpenCell = FIntPoint(X, Y);   // открытая клетка, которой принадлежит эта грань
					F.DirIndex = DirIndexOfNeighbour(-Dir.DX, -Dir.DY);
					R.Faces.Add(F);
				}
			}
		}

		if (R.Faces.Num() < 6) continue;   // граница слишком короткая, ломаная не получится
		Cand.Add(MoveTemp(R));
	}

	// 2) Каждому узлу границы предлагаем ОДНО новое положение (по лучу до эллипса).
	//    Один узел — одна точка на все полигоны, которые в него упираются.
	TArray<FVector2D> Proposal;  Proposal.Init(FVector2D(0.f, 0.f), CornerNum);
	TArray<uint8>     HasMove;   HasMove.Init(0, CornerNum);
	TArray<uint8>     NoMove;    NoMove.Init(0, CornerNum);   // узел трогать нельзя

	for (const FRoundRoom& R : Cand)
	{
		for (const FRoundFace& F : R.Faces)
		{
			const int32 Corners[2] = { F.CornerA, F.CornerB };
			const FVector2D   Grids[2] = { F.A0, F.B0 };
			for (int32 e = 0; e < 2; ++e)
			{
				const int32 C = Corners[e];
				if (C < 0 || C >= CornerNum || NoMove[C]) continue;
				const FVector2D P = RoundPointOnEllipse(R.Center, R.Semi, Grids[e]);
				if (!HasMove[C]) { HasMove[C] = 1; Proposal[C] = P; continue; }
				if (!SameGridPoint(Proposal[C], P)) { NoMove[C] = 1; HasMove[C] = 0; }   // две комнаты спорят из-за одного угла
			}
		}
	}

	// Открытая клетка, которой можно жертвовать ради дуги: она принадлежит комнате
	// формы «Круглая» и не является коридором (через неё не должен идти проход).
	// Возле такой клетки узел сетки двигать можно; возле прохода или чужой комнаты
	// — нельзя: там стена осталась бы прямой и узел обязан стоять на сетке.
	auto CarvedCellRules = [&]() -> TArray<uint8>
	{
		TArray<uint8> Ok;
		Ok.Init(0, GridW * GridH);
		if (RoomMap.Num() != GridW * GridH) return Ok;

		TMap<int32, bool> RoomIsRound;
		for (const FPlacedRoom& Room : Rooms)
		{
			if (Room.Shape != ERoomShape::Circle) continue;
			if (Room.W < 4 || Room.H < 4) continue;
			RoomIsRound.Add(Room.Index, true);
		}

		for (int32 i = 0; i < GridW * GridH; ++i)
		{
			if (Grid[i] != 1) continue;
			if (RoadMask.Num() == GridW * GridH && RoadMask[i] != 0) continue;   // клетка коридора
			const bool* bRound = RoomIsRound.Find(RoomMap[i]);
			if (bRound && *bRound) Ok[i] = 1;
		}
		return Ok;
	};

	const TArray<uint8> CarvedRound = CarvedCellRules();

	// Рёбра, выходящие из узла сетки: каждое задаётся парами клеток по его стороны.
	static const int32 CornerEdgePairs[4][2][2] = {
		{ { -1, -1 }, {  0, -1 } },   // ребро идёт на -X от узла
		{ {  0, -1 }, {  0,  0 } },   // на +X
		{ { -1,  0 }, {  0,  0 } },   // на +Y
		{ { -1, -1 }, { -1,  0 } },   // на -Y
	};

	// Узел сетки двигается только если ВСЕ стены, сходящиеся в нём, стоят на границе
	// камня и клетки круглой комнаты. Иначе в этот узел упиралась бы прямая стена
	// прохода или чужой комнаты: она не переставляется, и узел должен оставаться
	// на своём месте, чтобы такая стена не наклонилась и не перерезала проход.
	auto CornerEdgesAllowed = [&](int32 C) -> bool
	{
		const int32 CX = C % (GridW + 1);
		const int32 CY = C / (GridW + 1);

		for (int32 e = 0; e < 4; ++e)
		{
			const int32 AX = CX + CornerEdgePairs[e][0][0];
			const int32 AY = CY + CornerEdgePairs[e][0][1];
			const int32 BX = CX + CornerEdgePairs[e][1][0];
			const int32 BY = CY + CornerEdgePairs[e][1][1];
			if (!InBounds(AX, AY) || !InBounds(BX, BY)) continue;

			const int32 GA = Grid[Idx(AX, AY)];
			const int32 GB = Grid[Idx(BX, BY)];
			if (GA == GB) continue;   // одна стена тут: общего ребра у стены нет

			const int32 OpenIdx = (GA == 1) ? Idx(AX, AY) : Idx(BX, BY);
			if (CarvedRound[OpenIdx] == 0) return false;   // у узла проход или чужая комната
		}
		return true;
	};

	// Узел двигает все сходящиеся в нём клетки, поэтому проверяем, что каждая из
	// них остаётся нормальным четырёхугольником: иначе крышка или пол самопересекутся.
	auto CellQuadWithMovedCorner = [&](int32 X, int32 Y, int32 MovedCorner, const FVector2D& MovedPos)
	{
		static const int32 Off[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		TArray<FVector2D> Poly;
		Poly.Reserve(4);
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 CX = X + Off[i][0];
			const int32 CY = Y + Off[i][1];
			Poly.Add((CornerId(CX, CY) == MovedCorner) ? MovedPos : CornerPoint(CX, CY));
		}
		return Poly;
	};

	for (int32 C = 0; C < CornerNum; ++C)
	{
		if (!HasMove[C]) continue;
		// смещение меньше сотой клетки перестановкой не считается: из него вышел бы
		// треугольник-щёлка вместо переставленного угла
		if ((Proposal[C] - CornerPoint(CornerX(C), CornerY(C))).Size() < 0.01f) { HasMove[C] = 0; continue; }

		bool bOk = true;
		for (int32 DY = -1; DY <= 0 && bOk; ++DY)
		{
			for (int32 DX = -1; DX <= 0 && bOk; ++DX)
			{
				const int32 X = CornerX(C) + DX;
				const int32 Y = CornerY(C) + DY;
				if (!InBounds(X, Y)) continue;
				if (!PolygonIsSimpleSmall(CellQuadWithMovedCorner(X, Y, C, Proposal[C]))) bOk = false;
			}
		}
		if (!bOk) { HasMove[C] = 0; NoMove[C] = 1; continue; }
		if (!CornerEdgesAllowed(C)) { HasMove[C] = 0; NoMove[C] = 1; }
	}

	// 3) Грань переставляем, только если ОБОИМ её углам это разрешено и если её хорда
	//    не перерезала осевые линии проходов (вход, коридор, проход сквозь комнату). Если грань
	//    остаётся на сетке — её узлы запрещаем всем соседним граням, чтобы стена не
	//    разошлась с соседями. Отмены расходятся по цепочке, поэтому повторяем pass,
	//    пока не останется отмен.

	// Хорда грани не имеет права перерезать осевую линию между двумя проходимыми
	// клетками: именно по ней идёт единственный путь. Срезать угол клетки — можно
	// (так комната и становится круглой), стена может уходить в камень свободно; но
	// если хорда перекрыла середину прохода, проход был бы рассечён стеной. Проверяем
	// по готовым положениям обоих концов грани — тем самым, из которых потом строится
	// и стена, и полигоны пола с крышкой.
	auto SegmentsCrossOrOverlap = [](const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D) -> bool
	{
		auto Orient = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R) -> float
		{
			const float Cross = (Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X);
			if (Cross > 1e-4f) return 1.f;
			if (Cross < -1e-4f) return -1.f;
			return 0.f;
		};
		auto OnSegment = [](const FVector2D& S, const FVector2D& E, const FVector2D& P) -> bool
		{
			const float Eps = 1e-3f;
			return P.X >= FMath::Min(S.X, E.X) - Eps && P.X <= FMath::Max(S.X, E.X) + Eps &&
			       P.Y >= FMath::Min(S.Y, E.Y) - Eps && P.Y <= FMath::Max(S.Y, E.Y) + Eps;
		};

		const float D1 = Orient(A, B, C);
		const float D2 = Orient(A, B, D);
		const float D3 = Orient(C, D, A);
		const float D4 = Orient(C, D, B);

		if (((D1 > 0.f && D2 < 0.f) || (D1 < 0.f && D2 > 0.f)) &&
		    ((D3 > 0.f && D4 < 0.f) || (D3 < 0.f && D4 > 0.f))) return true;

		if (D1 == 0.f && OnSegment(A, B, C)) return true;
		if (D2 == 0.f && OnSegment(A, B, D)) return true;
		if (D3 == 0.f && OnSegment(C, D, A)) return true;
		if (D4 == 0.f && OnSegment(C, D, B)) return true;
		return false;
	};

	auto ChordSealsPassage = [&](const FVector2D& A, const FVector2D& B) -> bool
	{
		const int32 X0 = FMath::FloorToInt(FMath::Min(A.X, B.X)) - 1;
		const int32 X1 = FMath::FloorToInt(FMath::Max(A.X, B.X)) + 1;
		const int32 Y0 = FMath::FloorToInt(FMath::Min(A.Y, B.Y)) - 1;
		const int32 Y1 = FMath::FloorToInt(FMath::Max(A.Y, B.Y)) + 1;

		for (int32 CY = Y0; CY <= Y1; ++CY)
		{
			for (int32 CX = X0; CX <= X1; ++CX)
			{
				for (int32 Dir = 0; Dir < 2; ++Dir)
				{
					const int32 NX = CX + (Dir == 0 ? 1 : 0);
					const int32 NY = CY + (Dir == 1 ? 1 : 0);
					if (!InBounds(NX, NY)) continue;

					const int32 IA = Idx(CX, CY);
					const int32 IB = Idx(NX, NY);
					if (Grid[IA] != 1 || Grid[IB] != 1) continue;   // ребро у границы камня: там стена и обязана стоять

					const FVector2D S(static_cast<float>(CX) + 0.5f, static_cast<float>(CY) + 0.5f);
					const FVector2D E(static_cast<float>(NX) + 0.5f, static_cast<float>(NY) + 0.5f);
					if (SegmentsCrossOrOverlap(A, B, S, E)) return true;   // середина прохода перекрыта
				}
			}
		}
		return false;
	};

	TArray<TArray<uint8>> Reject;
	Reject.SetNum(Cand.Num());
	for (int32 r = 0; r < Cand.Num(); ++r) Reject[r].Init(0, Cand[r].Faces.Num());

	auto FacePass = [&]() -> bool
	{
		bool bChanged = false;
		for (int32 r = 0; r < Cand.Num(); ++r)
		{
			FRoundRoom& R = Cand[r];
			for (int32 f = 0; f < R.Faces.Num(); ++f)
			{
				if (Reject[r][f]) continue;
				FRoundFace& F = R.Faces[f];

				if (F.CornerA < 0 || F.CornerB < 0 || F.CornerA >= CornerNum || F.CornerB >= CornerNum)
				{
					Reject[r][f] = 1;
					continue;
				}
				// как эта грань будет построена стеной на самом деле: её концы берут
				// положение узлов сетки — прежнее или переставленное, поэтому проверять
				// нужно фактическое положение грани, а не только её принятый вариант. Иначе
				// грань, оставленная на сетке из-за соседнего угла, всё равно съезжала бы
				// одним концом за переставленным соседом и перерезала бы проход.
				const FVector2D RealA = HasMove[F.CornerA] ? Proposal[F.CornerA] : F.A0;
				const FVector2D RealB = HasMove[F.CornerB] ? Proposal[F.CornerB] : F.B0;
				if ((RealB - RealA).Size() >= 0.02f &&
				    (RoundChordCrossesPassage(RealA.X, RealA.Y, RealB.X, RealB.Y, R.RoomIndex) ||
				     ChordSealsPassage(RealA, RealB)))
				{
					// в любом виде эта грань перекрыла бы проход — держим на сетке её углы
					F.A = F.A0;
					F.B = F.B0;
					Reject[r][f] = 1;
					NoMove[F.CornerA] = 1;
					NoMove[F.CornerB] = 1;
					HasMove[F.CornerA] = 0;
					HasMove[F.CornerB] = 0;
					bChanged = true;
					continue;
				}

				if (NoMove[F.CornerA] || NoMove[F.CornerB])
				{
					// сосед этого угла стоит по-старему — держим на сетке и свою грань
					F.A = F.A0;
					F.B = F.B0;
					Reject[r][f] = 1;
					bChanged = true;
					continue;
				}

				F.A = HasMove[F.CornerA] ? Proposal[F.CornerA] : CornerPoint(CornerX(F.CornerA), CornerY(F.CornerA));
				F.B = HasMove[F.CornerB] ? Proposal[F.CornerB] : CornerPoint(CornerX(F.CornerB), CornerY(F.CornerB));

				const bool bTooShort = (F.B - F.A).Size() < 0.02f;
				const bool bSeals = !bTooShort && ChordSealsPassage(F.A, F.B);
				const bool bCuts = (!bTooShort && RoundChordCrossesPassage(F.A.X, F.A.Y, F.B.X, F.B.Y, R.RoomIndex)) || bSeals;

				if (bTooShort || bCuts)
				{
					F.A = F.A0;
					F.B = F.B0;
					Reject[r][f] = 1;
					if (bCuts)
					{
						// хорда этой грани перерезала бы проход: держим на сетке и её углы,
						// чтобы соседние грани не ушли вперёд и не оставили щель
						NoMove[F.CornerA] = 1;
						NoMove[F.CornerB] = 1;
						HasMove[F.CornerA] = 0;
						HasMove[F.CornerB] = 0;
						bChanged = true;
					}
				}
			}
		}

		return bChanged;
	};

	for (int32 Pass = 0; Pass < 64; ++Pass) if (!FacePass()) break;

	// 4) Если переставлять почти нечего (вся граница у входов) — комната остаётся
	//    такой, какой была; тогда и её узлы не двигает никто.
	TArray<uint8> RoomDropped;
	RoomDropped.Init(0, Cand.Num());
	for (int32 Outer = 0; Outer < 8; ++Outer)
	{
		bool bAnyDrop = false;
		for (int32 r = 0; r < Cand.Num(); ++r)
		{
			if (RoomDropped[r]) continue;

			int32 Arcs = 0;
			for (int32 f = 0; f < Cand[r].Faces.Num(); ++f)
			{
				if (Reject[r][f]) continue;
				const FRoundFace& F = Cand[r].Faces[f];
				if (SameGridPoint(F.A, F.A0) && SameGridPoint(F.B, F.B0)) continue;
				++Arcs;
			}
			if (Arcs >= 4) continue;

			RoomDropped[r] = 1;
			bAnyDrop = true;
			for (const FRoundFace& F : Cand[r].Faces)
			{
				if (F.CornerA < 0 || F.CornerB < 0) continue;
				NoMove[F.CornerA] = 1;
				NoMove[F.CornerB] = 1;
				HasMove[F.CornerA] = 0;
				HasMove[F.CornerB] = 0;
			}
		}
		if (!bAnyDrop) break;
		for (int32 Pass = 0; Pass < 64; ++Pass) if (!FacePass()) break;
	}

	// 5) Готовые грани и общий список переставленных узлов сетки.
	for (int32 r = 0; r < Cand.Num(); ++r)
	{
		if (RoomDropped[r]) continue;

		FRoundRoom R = MoveTemp(Cand[r]);
		bool bAny = false;
		for (int32 f = 0; f < R.Faces.Num(); ++f)
		{
			FRoundFace& F = R.Faces[f];
			if (Reject[r][f] || (SameGridPoint(F.A, F.A0) && SameGridPoint(F.B, F.B0)))
			{
				F.A = F.A0;
				F.B = F.B0;
				F.bArc = false;
				continue;
			}

			F.bArc = true;
			bAny = true;

			if (!SameGridPoint(F.A, F.A0)) { OutCornerMoved[F.CornerA] = 1; OutCornerPos[F.CornerA] = F.A; }
			if (!SameGridPoint(F.B, F.B0)) { OutCornerMoved[F.CornerB] = 1; OutCornerPos[F.CornerB] = F.B; }
		}
		if (!bAny) continue;

		// 6) Порядок обхода по кругу: нужен, чтобы плитка текстуры шла по круглой
		//    стене непрерывно, и чтобы передняя грань каждой грани смотрела внутрь.
		for (FRoundFace& F : R.Faces)
		{
			const FVector2D Mid((F.A.X + F.B.X) * 0.5f, (F.A.Y + F.B.Y) * 0.5f);
			F.MidAngle = WrapAngle2Pi(RoundParamAngle(R.Center, R.Semi, Mid));

			// начало грани — тот её конец, что идёт раньше по обходу
			const float DA = WrapAngleSigned(RoundParamAngle(R.Center, R.Semi, F.A) - F.MidAngle);
			const float DB = WrapAngleSigned(RoundParamAngle(R.Center, R.Semi, F.B) - F.MidAngle);
			if (DB < DA)
			{
				Swap(F.A, F.B);
			}
		}

		R.Faces.Sort([](const FRoundFace& A, const FRoundFace& B) { return A.MidAngle < B.MidAngle; });

		for (const FRoundFace& F : R.Faces)
		{
			if (!F.bArc) continue;
			OutSkip[Idx(F.RockCell.X, F.RockCell.Y) * WallDirCount + F.DirIndex] = 1;   // прежняя грань тут не нужна
		}

		OutRooms.Add(MoveTemp(R));
	}
}

// Сварка процедурного меша: вершины с ОДИНАКОВЫМ положением, нормалью и UV
// становятся ОДНОЙ вершиной, а треугольники с нулевой площадью после сварки
// убираются. Сеть становится цельной: там, где полигоны соприкасаются, стоит
// одна точка, а не две совпавшие. Возвращает число удалённых совпавших вершин.
static int32 WeldProcMesh(TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Normals, TArray<FVector2D>& UVs)
{
	const int32 Num = Verts.Num();
	if (Num == 0) return 0;

	auto Quant = [](float V) { return FMath::RoundToInt(V * 100.f); };   // положение с точностью до миллиметра
	auto PosHash = [&](const FVector& P)
	{
		return HashCombine(HashCombine(GetTypeHash(Quant(P.X)), GetTypeHash(Quant(P.Y))), GetTypeHash(Quant(P.Z)));
	};

	// вершины группируются по хэшу положения; совпадение проверяется точно,
	// поэтому разные точки в один слот не склеятся
	TMap<uint32, TArray<int32>> Bucket;
	Bucket.Reserve(Num);

	TArray<int32> Map;   // старая вершина -> та же (сваренная) вершина
	Map.Init(-1, Num);

	for (int32 i = 0; i < Num; ++i)
	{
		TArray<int32>* List = Bucket.Find(PosHash(Verts[i]));
		if (List)
		{
			for (int32 Other : *List)
			{
				if (Verts[Other] == Verts[i] && Normals[Other] == Normals[i] && UVs[Other] == UVs[i])
				{
					Map[i] = Other;
					break;
				}
			}
		}
		if (Map[i] >= 0) continue;

		if (List) List->Add(i);
		else
		{
			TArray<int32> NewList;
			NewList.Add(i);
			Bucket.Add(PosHash(Verts[i]), MoveTemp(NewList));
		}
		Map[i] = i;
	}

	TArray<FVector> NewVerts;
	TArray<FVector> NewNormals;
	TArray<FVector2D> NewUVs;
	TArray<int32> NewTris;
	NewVerts.Reserve(Num);
	NewNormals.Reserve(Num);
	NewUVs.Reserve(Num);
	NewTris.Reserve(Tris.Num());

	TArray<int32> NewIdx;   // старая (оставленная) вершина -> её новый номер
	NewIdx.Init(-1, Num);
	for (int32 i = 0; i < Num; ++i)
	{
		if (Map[i] != i) continue;
		NewIdx[i] = NewVerts.Num();
		NewVerts.Add(Verts[i]);
		NewNormals.Add(Normals[i]);
		NewUVs.Add(UVs[i]);
	}

	for (int32 t = 0; t + 2 < Tris.Num(); t += 3)
	{
		const int32 A = (Map[Tris[t]]     >= 0) ? NewIdx[Map[Tris[t]]]     : -1;
		const int32 B = (Map[Tris[t + 1]] >= 0) ? NewIdx[Map[Tris[t + 1]]] : -1;
		const int32 C = (Map[Tris[t + 2]] >= 0) ? NewIdx[Map[Tris[t + 2]]] : -1;

		if (A < 0 || B < 0 || C < 0) continue;
		if (A == B || B == C || A == C) continue;   // треугольник схлопнулся в отрезок

		const FVector Cross = FVector::CrossProduct(NewVerts[B] - NewVerts[A], NewVerts[C] - NewVerts[A]);
		if (Cross.Size() < 1e-4) continue;          // треугольник-щель нулевой площади

		NewTris.Add(A);
		NewTris.Add(B);
		NewTris.Add(C);
	}

	const int32 Removed = Num - NewVerts.Num();
	Verts = MoveTemp(NewVerts);
	Normals = MoveTemp(NewNormals);
	UVs = MoveTemp(NewUVs);
	Tris = MoveTemp(NewTris);
	return Removed;
}

// =====================================================================
//  Геометрия: 0 -> стены (одна секция процедурного меша) + крыша (отдельная секция),
//             1 -> пол (отдельная секция). Соседние куски объединены в прямоугольники,
//             UV всех поверхностей считаются в сантиметрах (текстура не растягивается).
// =====================================================================
void ADungeonGenerator::BuildGeometry()
{
	if (!WallsMesh) return;

	ClearMarkers();

	if (Grid.Num() != GridW * GridH) return;

	WallsMesh->ClearAllMeshSections();
	if (RoofMesh) RoofMesh->ClearAllMeshSections();

	// Стены круглых комнат: их собственные грани, но концы подтянуты на эллипс.
	// Маска говорит обходу стен, на каких местах прежняя грань больше не нужна.
	TArray<FRoundRoom> RoundRooms;
	TArray<uint8> RoundSkipFace;
	TArray<FVector2D> CornerPos;   // новое положение узла сетки (в клетках)
	TArray<uint8> CornerMoved;     // 1 = узел переставлен
	BuildRoundRooms(RoundRooms, RoundSkipFace, CornerPos, CornerMoved);

	const int32 CornerNum = (GridW + 1) * (GridH + 1);

	// Положение узла сетки: переставленное (если узел участвует в круглой стене)
	// или прежнее. Одно и то же значение берут грань стены, крышка клетки камня и
	// плита пола клетки комнаты, поэтому в месте соприкосновения стоит ОДНА точка.
	auto CornerAt = [&](int32 CX, int32 CY) -> FVector2D
	{
		if (CX < 0 || CY < 0 || CX > GridW || CY > GridH)
			return FVector2D(static_cast<float>(CX), static_cast<float>(CY));
		const int32 Id = CY * (GridW + 1) + CX;
		if (Id < CornerNum && CornerMoved[Id] != 0) return CornerPos[Id];
		return FVector2D(static_cast<float>(CX), static_cast<float>(CY));
	};

	auto CornerIsMoved = [&](int32 CX, int32 CY) -> bool
	{
		if (CX < 0 || CY < 0 || CX > GridW || CY > GridH) return false;
		const int32 Id = CY * (GridW + 1) + CX;
		return Id < CornerNum && CornerMoved[Id] != 0;
	};

	const float CS = CellSize;
	const float WH = WallHeight;

	// Размер ОДНОЙ ПЛИТКИ текстуры в сантиметрах. 0 = одна плитка на одну клетку
	// сетки (CellSize). Дальше все UV считаются от мировых размеров в сантиметрах,
	// поэтому одна и та же текстура нигде не растягивается: у стены высотой WallHeight
	// будет WallHeight / Tile плиток вместо одной растянутой, а у ряда из 4 клеток — 4
	// плитки шириной Tile. В материале при этом держи Tiling = 1.
	const float Tile = (TextureTileSize > KINDA_SMALL_NUMBER) ? TextureTileSize : CS;

	// UV в сантиметрах от начала сетки: u = координата / Tile.
	auto UV = [&](float CoordCM) { return CoordCM / Tile; };

	// Все координаты ниже — ЛОКАЛЬНЫЕ относительно актора: компонент сам применит его
	// позицию и поворот (CreateMeshSection и SetRelativeLocation работают в локальном пространстве).
	const FVector Offset = bCenterOnActor
		? FVector(-GridW * CS * 0.5f, -GridH * CS * 0.5f, 0.f)
		: FVector::ZeroVector;

	auto CellLocal = [&](float LX, float LY, float LZ)
	{
		return Offset + FVector(LX, LY, LZ);
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	Vertices.Reserve(GridW * GridH * 2);
	Triangles.Reserve(GridW * GridH * 4);
	Normals.Reserve(GridW * GridH * 2);
	UVs.Reserve(GridW * GridH * 2);

	// ================================================================
	//  Жадная разбивка клеток на прямоугольники (нужна для крыши и пола):
	//  полосу сначала тянем по X насколько можно, потом опускаем её вниз по Y,
	//  пока весь прямоугольник целиком лежит в подходящих клетках.
	//  Так тысячи отдельных квадратов превращаются в несколько больших плит.
	// ================================================================
	TArray<FIntRect> Rects;
	auto CollectRects = [&](uint8 WantedValue, const TArray<uint8>* Skip = nullptr)
	{
		Rects.Reset();
		if (Grid.Num() != GridW * GridH) return;

		// клетка подходит, если она нужного типа и не накрыта своим полигоном с крылом
		auto Fits = [&](int32 X, int32 Y) -> bool
		{
			const int32 I = Idx(X, Y);
			if (Grid[I] != WantedValue) return false;
			if (Skip && (*Skip)[I] != 0) return false;
			return true;
		};

		TArray<uint8> Used;
		Used.Init(0, GridW * GridH);

		for (int32 Y = 0; Y < GridH; ++Y)
		{
			for (int32 X = 0; X < GridW; ++X)
			{
				if (!Fits(X, Y) || Used[Idx(X, Y)]) continue;

				// 1) растягиваем полосу по X
				int32 RectW = 1;
				while (X + RectW < GridW && Fits(X + RectW, Y) && !Used[Idx(X + RectW, Y)])
					++RectW;

				// 2) опускаем полосу по Y, пока весь ряд прямоугольника подходит
				int32 RectH = 1;
				while (Y + RectH < GridH)
				{
					bool bWholeRow = true;
					for (int32 i = 0; i < RectW; ++i)
					{
						if (!Fits(X + i, Y + RectH) || Used[Idx(X + i, Y + RectH)]) { bWholeRow = false; break; }
					}
					if (!bWholeRow) break;
					++RectH;
				}

				for (int32 YY = Y; YY < Y + RectH; ++YY)
					for (int32 XX = X; XX < X + RectW; ++XX)
						Used[Idx(XX, YY)] = 1;

				// FIntRect: Max включительно (клетки X .. Max.X, Y .. Max.Y)
				Rects.Add(FIntRect(X, Y, X + RectW - 1, Y + RectH - 1));
			}
		}
	};

	// Одна горизонтальная плита (крыша или пол) поверх прямоугольника клеток:
	// обход (P00,P01,P11) и (P00,P11,P10) -> передняя грань (C-A)x(B-A) = +Z,
	// то есть плита смотрит ВВЕРХ (как и прежние крышки клеток).
	// UV = мировые X/Y в сантиметрах: швы плиток стоят в одних и тех же местах
	// сетки на всех прямоугольниках, поэтому тайлинг непрерывен через швы.
	auto AddFlatQuadCM = [&](TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms,
		                     TArray<FVector2D>& UVS, float X0, float Y0, float X1, float Y1, float Height)
	{
		const int32 BaseIdx = Verts.Num();

		Verts.Add(CellLocal(X0, Y0, Height));   // P00
		Verts.Add(CellLocal(X0, Y1, Height));   // P01
		Verts.Add(CellLocal(X1, Y1, Height));   // P11
		Verts.Add(CellLocal(X1, Y0, Height));   // P10

		Tris.Add(BaseIdx + 0);
		Tris.Add(BaseIdx + 1);
		Tris.Add(BaseIdx + 2);

		Tris.Add(BaseIdx + 0);
		Tris.Add(BaseIdx + 2);
		Tris.Add(BaseIdx + 3);

		for (int32 i = 0; i < 4; ++i) Norms.Add(FVector(0, 0, 1));

		UVS.Add(FVector2D(UV(X0), UV(Y0)));
		UVS.Add(FVector2D(UV(X0), UV(Y1)));
		UVS.Add(FVector2D(UV(X1), UV(Y1)));
		UVS.Add(FVector2D(UV(X1), UV(Y0)));
	};

	// Та же плита по прямоугольнику клеток (Max включительно). Обёртка держит
	// крышу и пол на клеточной сетке, а крышку уголков дуги — на её собственной,
	// более мелкой: оба пользуются одной раскладкой UV, поэтому швы плиток
	// совпадают на всех поверхностях.
	auto AddFlatRect = [&](TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms,
		                   TArray<FVector2D>& UVS, const FIntRect& Rect, float Height, int32& CoveredCells)
	{
		AddFlatQuadCM(Verts, Tris, Norms, UVS,
		              static_cast<float>(Rect.Min.X) * CS, static_cast<float>(Rect.Min.Y) * CS,
		              static_cast<float>(Rect.Max.X + 1) * CS, static_cast<float>(Rect.Max.Y + 1) * CS,
		              Height);

		CoveredCells += (Rect.Max.X - Rect.Min.X + 1) * (Rect.Max.Y - Rect.Min.Y + 1);
	};

	// Плита примыкания переставленной грани: полоса между прежним ребром клетки
	// (P0-P1) и новой гранью (P3-P2). Порядок обхода подбирается так, чтобы плита
	// смотрела ВВЕРХ (+Z), как крыша и пол. Углы P0 и P1 — ровно те же точки,
	// что уже есть у прежних крышек и плит пола, а P2 и P3 — ровно те же точки,
	// что у верхнего и нижнего края новой грани стены: шов получается общим,
	// поэтому плита не висит отдельно, а вварена в крышу (или в пол) встык.
	// UV берётся из координат, как у плит, поэтому тайлинг продолжается без сдвига.
	// Плита по многоугольнику в клеточных координатах (сантиметры умножаются на CS
	// ровно так же, как у грани стены, поэтому углы плиты и углы стены совпадают
	// дословно). Совпавшие углы убираются, обход разворачивается против часовой
	// (передняя грань +Z), вырожденные и вывернутые треугольники не добавляются.
	// Возвращает число добавленных треугольников: 0 значит, что плиты не получилось.
	auto AddFlatPolyCM = [&](TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms,
		                     TArray<FVector2D>& UVS, TArray<FVector2D> Poly, float Height) -> int32
	{
		for (int32 i = 0; i < Poly.Num(); ++i) Poly[i] *= CS;

		TArray<FVector2D> Clean;
		Clean.Reserve(Poly.Num());
		for (const FVector2D& P : Poly)
		{
			if (Clean.Num() > 0 && (P - Clean[Clean.Num() - 1]).Size() < 1.f) continue;
			Clean.Add(P);
		}
		if (Clean.Num() > 3 && (Clean[0] - Clean[Clean.Num() - 1]).Size() < 1.f) Clean.Pop();
		if (Clean.Num() < 3) return 0;

		// Ориентация — по знаку площади: первый угол клетки с крылом бывает
		// рефлексивным, по нему направление обхода не определить.
		if (PolygonSignedArea2(Clean) < 0.f)   // обход по часовой -> разворачиваем
		{
			for (int32 i = 0, j = Clean.Num() - 1; i < j; ++i, --j) Swap(Clean[i], Clean[j]);
		}

		// Точка пересечения крыла с границей клетки может совпасть с углом клетки
		// или лечь вплотную к соседнему углу: такой угол не нужен, а без его удаления
		// треугольник крыла выходит вырожденным (площадью в доли квадратного
		// миллиметра) и плита начинает мигать.
		for (int32 Pass = 0; Pass < 6; ++Pass)
		{
			const int32 RingNum = Clean.Num();
			if (RingNum < 4) break;

			bool bRemoved = false;
			for (int32 i = 0; i < RingNum; ++i)
			{
				const FVector2D& Prev = Clean[(i + RingNum - 1) % RingNum];
				const FVector2D& Cur = Clean[i];
				const FVector2D& Next = Clean[(i + 1) % RingNum];

				const float Cross = (Cur.X - Prev.X) * (Next.Y - Cur.Y) - (Cur.Y - Prev.Y) * (Next.X - Cur.X);
				if (FMath::Abs(Cross) < 1e-3f)   // угол лежит на соседних рёбрах (в см^2)
				{
					Clean.RemoveAt(i);
					bRemoved = true;
					break;
				}
			}

			if (!bRemoved) break;
		}

		if (Clean.Num() < 3) return 0;

		const int32 BaseIdx = Verts.Num();
		for (const FVector2D& P : Clean)
		{
			Verts.Add(CellLocal(P.X, P.Y, Height));
			Norms.Add(FVector(0, 0, 1));
			UVS.Add(FVector2D(UV(P.X), UV(P.Y)));
		}

		// Обрезка ушей, а не веер: клетка с крылом невыпуклая, а веер от одной
		// вершины дал бы треугольники снаружи многоугольника (чёрные дыры в крышке).
		TArray<int32> LocalTris;
		LocalTris.Reserve((Clean.Num() - 2) * 3);
		const int32 Added = EarClipTriangulate(Clean, LocalTris);

		// Крышка и пол в генераторе обходятся ПО ЧАСОВОЙ (AddFlatQuadCM), и при таком
		// обходе передняя грань каждого треугольника смотрит ВВЕРХ — в ту же сторону,
		// что и хранящаяся нормаль +Z. Срез ушей строит треугольники против часовой,
		// поэтому их порядок разворачиваем: иначе плиты клеток с переставленными
		// углами оказались бы вывернутыми относительно всех остальных плит и
		// показывались бы со своей задней стороны (местами «без текстуры»).
		for (int32 I = 0; I + 2 < LocalTris.Num(); I += 3)
		{
			Tris.Add(BaseIdx + LocalTris[I]);
			Tris.Add(BaseIdx + LocalTris[I + 2]);
			Tris.Add(BaseIdx + LocalTris[I + 1]);
		}

		return Added;
	};

	// =====================================================================
	//  РЁБРА СТЕН, КОТОРЫЕ РЕЗУТ ПЛИТЫ ПОЛА И КРЫШКИ.
	//  Если концы грани стены сошли с линии сетки (их переставили на эллипс
	//  круглой комнаты), грань идёт сквозь клетки и обязана разрезать крышку и
	//  пол ровно по себе. Иначе плита высунется за стену: кусок крыши нависнет
	//  над комнатой, кусок пола уйдёт в камень, а где-то крышка до стены не
	//  дойдёт и останется дыра. Новых полигонов при этом не появляется: прежняя
	//  плита клетки просто разрезается по ребру стены, обе половины остаются
	//  прежними половинами и сходятся со стеной в тех же самых точках.
	//  Координаты — в единицах клетки сетки. RockMid — центр клетки, где камень:
	//  его половину ребра закрывает крышка, противоположную — пол комнаты.
	// =====================================================================
	struct FWallCut
	{
		FVector2D A;
		FVector2D B;
		FVector2D RockMid;
		FVector2D OpenMid;
	};
	TArray<FWallCut> WallCuts;

	auto AddWallCut = [&](const FVector2D& A, const FVector2D& B, const FIntPoint& RockCell, const FIntPoint& OpenCell)
	{
		if ((B - A).Size() < 0.01f) return;

		FWallCut Cut;
		Cut.A = A;
		Cut.B = B;
		Cut.RockMid = FVector2D(static_cast<float>(RockCell.X) + 0.5f, static_cast<float>(RockCell.Y) + 0.5f);
		Cut.OpenMid = FVector2D(static_cast<float>(OpenCell.X) + 0.5f, static_cast<float>(OpenCell.Y) + 0.5f);

		// центр каменной клетки может оказаться ровно на ребре (диагональ сквозь
		// клетку) — тогда сторону задаём точкой чуть ближе к камню
		if (FMath::Abs(LineSide2D(Cut.A, Cut.B, Cut.RockMid)) < 1e-4f)
		{
			Cut.RockMid = Cut.OpenMid + (Cut.RockMid - Cut.OpenMid) * 0.98f;
			if (FMath::Abs(LineSide2D(Cut.A, Cut.B, Cut.RockMid)) < 1e-4f) return;
		}

		WallCuts.Add(Cut);
	};

	// Касается ли ребро стены клетки (её квадрата X..X+1, Y..Y+1).
	auto CutTouchesCell = [&](const FWallCut& Cut, int32 X, int32 Y) -> bool
	{
		const float MinX = FMath::Min(Cut.A.X, Cut.B.X);
		const float MaxX = FMath::Max(Cut.A.X, Cut.B.X);
		const float MinY = FMath::Min(Cut.A.Y, Cut.B.Y);
		const float MaxY = FMath::Max(Cut.A.Y, Cut.B.Y);
		if (MaxX < static_cast<float>(X) - 1e-3f || MinX > static_cast<float>(X + 1) + 1e-3f) return false;
		if (MaxY < static_cast<float>(Y) - 1e-3f || MinY > static_cast<float>(Y + 1) + 1e-3f) return false;

		static const int32 Off[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		const FVector2D C(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D P(static_cast<float>(X + Off[i][0]), static_cast<float>(Y + Off[i][1]));
			const FVector2D Q(static_cast<float>(X + Off[(i + 1) % 4][0]), static_cast<float>(Y + Off[(i + 1) % 4][1]));
			const float SC = LineSide2D(P, Q, C);
			const float SA = LineSide2D(P, Q, Cut.A);
			const float SB = LineSide2D(P, Q, Cut.B);
			if (SA * SC < -1e-6f && SB * SC < -1e-6f) return false;   // оба конца снаружи одной стороны
		}
		return true;
	};

	// Куски клетки, разрезанной рёбрами стен: что из них закрывается крышкой
	// (сторона камня), а что полом (сторона комнаты). Клетку, до которой ни одно
	// ребро не дотягивается, не режем: она целиком достаётся плите своего типа.
	// Куски не новые — это прежняя клетка, делённая ребром стены надвое.
	auto CellCutPieces = [&](int32 X, int32 Y, bool bRockSide, TArray<TArray<FVector2D>>& Out, bool& bOutSplit) -> void
	{
		Out.Reset();
		bOutSplit = false;

		static const int32 Off[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

		TArray<TArray<FVector2D>> Parts;
		TArray<uint8> Side;   // 0 — ещё не разделена, 1 — половина камня, 2 — половина комнаты
		Parts.Add(TArray<FVector2D>());
		Parts[0].Reserve(4);
		for (int32 i = 0; i < 4; ++i)
		{
			Parts[0].Add(FVector2D(static_cast<float>(X + Off[i][0]), static_cast<float>(Y + Off[i][1])));
		}
		Side.Add(0);

		for (const FWallCut& Cut : WallCuts)
		{
			const int32 Num = Parts.Num();
			for (int32 i = 0; i < Num; ++i)
			{
				TArray<FVector2D> RockPart, OpenPart;
				if (!SplitConvexBySegment(Cut.A, Cut.B, Parts[i], Cut.RockMid, Cut.OpenMid, RockPart, OpenPart)) continue;   // клетка этим ребром не делится

				Parts[i] = MoveTemp(RockPart);
				Side[i] = 1;
				Parts.Add(MoveTemp(OpenPart));
				Side.Add(2);
				bOutSplit = true;
			}
		}

		const uint8 WantSide = bRockSide ? 1 : 2;
		const uint8 CellSide = (Grid.Num() == GridW * GridH && Grid[Idx(X, Y)] == 0) ? 1 : 2;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const uint8 S = (Side[i] != 0) ? Side[i] : CellSide;
			if (S == WantSide) Out.Add(MoveTemp(Parts[i]));
		}
	};

	// Клетки, которых касается переставленная стена: у них переставлен угол или
	// сквозь них проходит ребро стены. Такие клетки нельзя забирать общим
	// прямоугольником — общая плита высунулась бы за границу стены. Остальные
	// клетки по-прежнему объединяются в большие плиты, как и было.
	auto BuildRoundCellMarks = [&]() -> TArray<uint8>
	{
		TArray<uint8> Mark;
		Mark.Init(0, GridW * GridH);
		if (Grid.Num() != GridW * GridH) return Mark;

		static const int32 Off[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		for (int32 Y = 0; Y < GridH; ++Y)
		{
			for (int32 X = 0; X < GridW; ++X)
			{
				for (int32 i = 0; i < 4; ++i)
				{
					if (CornerIsMoved(X + Off[i][0], Y + Off[i][1]))
					{
						Mark[Idx(X, Y)] = 1;
						break;
					}
				}
				if (Mark[Idx(X, Y)] != 0) continue;

				// ребро стены может пройти сквозь клетку, не задев её углов
				for (const FWallCut& Cut : WallCuts)
				{
					if (CutTouchesCell(Cut, X, Y))
					{
						Mark[Idx(X, Y)] = 1;
						break;
					}
				}
			}
		}
		return Mark;
	};

	// =====================================================================
	//  СТЕНЫ: соседние грани ОБЪЕДИНЯЮТСЯ.
	//  Грань задаётся камнем (клетка 0) и направлением к открытой клетке (клетка 1).
	//  У двух идущих подряд камней с ОДИНАКОВЫМ направлением их грани лежат в одной
	//  плоскости и стыкуются без шва, поэтому вместо отдельного квадрата на каждую
	//  клетку строим ОДИН прямоугольник на весь ряд: стена из 4 клеток — это один
	//  прямоугольник шириной 4 * CellSize и высотой WallHeight.
	//  Стена всегда идёт на всю высоту, поэтому вертикального объединения тут нет.
	// =====================================================================
	// Направления граней — WallDirs выше по файлу: тот же порядок, что и ключ
	// маски переставленных граней круглых комнат.

	// нужна ли этой клетке грань в этом направлении
	auto WallFaceHere = [&](int32 X, int32 Y, int32 Di)
	{
		const FWallDir& Dir = WallDirs[Di];
		if (Grid[Idx(X, Y)] != 0) return false;                        // не камень
		const int32 NX = X + Dir.DX, NY = Y + Dir.DY;
		if (!InBounds(NX, NY) || Grid[Idx(NX, NY)] != 1) return false; // сосед — комната или дорога
		return RoundSkipFace[Idx(X, Y) * WallDirCount + Di] == 0;       // эту грань переставили на эллипс
	};

	// Ориентация полигонов (требование: грани смотрят ВНУТРЬ дорог и комнат).
	// В UE (левая система координат) передней гранью треугольника (A,B,C) считается сторона,
	// в которую смотрит (C-A) x (B-A) — обход по часовой, если смотреть со стороны нормали
	// (условие «CW = DirectX/UE renderer convention» и Chaos FTriangleMesh::GetFaceNormals,
	// который берёт CrossProduct(p20, p10) = (C-A)x(B-A)). Задняя грань отбраковывается.
	// Обход (V0,V1,V2) и (V1,V3,V2), где V0/V1 — низ прямоугольника, V2/V3 — верх, даёт
	// переднюю грань в сторону Up x L, Up = (0,0,1), L = V1 - V0. Поэтому низ-левый угол
	// ставим с той стороны ряда, чтобы L = Normal x Up: юг -> +X, север -> -X,
	// восток -> -Y, запад -> +Y. Тогда передняя грань совпадает с Normal и смотрит
	// в открытую клетку (в дорогу/комнату), а не в камень.
	auto AddWallRect = [&](const FVector& BottomLeft, const FVector& BottomRight, const FVector& Normal,
	                       float ULeft, float URight)
	{
		const FVector TopLeft  = BottomLeft  + FVector(0.f, 0.f, WH);
		const FVector TopRight = BottomRight + FVector(0.f, 0.f, WH);

		const int32 BaseIdx = Vertices.Num();

		Vertices.Add(BottomLeft);
		Vertices.Add(BottomRight);
		Vertices.Add(TopLeft);
		Vertices.Add(TopRight);

		Triangles.Add(BaseIdx + 0);
		Triangles.Add(BaseIdx + 1);
		Triangles.Add(BaseIdx + 2);

		Triangles.Add(BaseIdx + 1);
		Triangles.Add(BaseIdx + 3);
		Triangles.Add(BaseIdx + 2);

		for (int32 i = 0; i < 4; ++i) Normals.Add(Normal);

		// V считается по высоте стены в сантиметрах: низ стены = 0, верх = WallHeight / Tile.
		// На всех стенах плитки начинаются на одних и тех же высотах, поэтому ряды текстуры
		// идут ровно по всему подземелью и сходятся на смежных гранях одного угла.
		UVs.Add(FVector2D(ULeft,  0.f));
		UVs.Add(FVector2D(URight, 0.f));
		UVs.Add(FVector2D(ULeft,  UV(WH)));
		UVs.Add(FVector2D(URight, UV(WH)));
	};

	// Одна грань гладкой дуги: низ P0-P1, верх на той же высоте WH, что и у всех
	// стен. Нормали ВЕРШИН радиальные (смотрят к центру комнаты), поэтому ломаная
	// из хорд под светом выглядит как гладкая стена, а не как набор пластин.
	// A и B — точки дуги в сантиметрах (уже относительно актора, как и все координаты).
	auto AddArcQuad = [&](const FVector2D& A, const FVector2D& B, const FVector& FaceNormal,
	                      const FVector2D& CenterCM, float ULeft, float URight)
	{
		const FVector BottomLeft  = CellLocal(A.X, A.Y, 0.f);
		const FVector BottomRight = CellLocal(B.X, B.Y, 0.f);
		const FVector TopLeft     = BottomLeft  + FVector(0.f, 0.f, WH);
		const FVector TopRight    = BottomRight + FVector(0.f, 0.f, WH);

		const int32 BaseIdx = Vertices.Num();

		Vertices.Add(BottomLeft);
		Vertices.Add(BottomRight);
		Vertices.Add(TopLeft);
		Vertices.Add(TopRight);

		Triangles.Add(BaseIdx + 0);
		Triangles.Add(BaseIdx + 1);
		Triangles.Add(BaseIdx + 2);

		Triangles.Add(BaseIdx + 1);
		Triangles.Add(BaseIdx + 3);
		Triangles.Add(BaseIdx + 2);

		auto RadialNormal = [&](const FVector2D& P)
		{
			FVector N(CenterCM.X - P.X, CenterCM.Y - P.Y, 0.f);
			return N.SizeSquared() > KINDA_SMALL_NUMBER ? N.GetSafeNormal() : FaceNormal;
		};

		Normals.Add(RadialNormal(A));
		Normals.Add(RadialNormal(B));
		Normals.Add(RadialNormal(A));
		Normals.Add(RadialNormal(B));

		UVs.Add(FVector2D(ULeft,  0.f));
		UVs.Add(FVector2D(URight, 0.f));
		UVs.Add(FVector2D(ULeft,  UV(WH)));
		UVs.Add(FVector2D(URight, UV(WH)));
	};

	int32 WallRects = 0;      // сколько прямоугольников построили
	int32 WallCellFaces = 0;  // сколько отдельных квадратов было бы по-старому

	for (int32 Di = 0; Di < WallDirCount; ++Di)
	{
		const FWallDir& Dir = WallDirs[Di];

		const int32 RunMax   = Dir.bRunAlongX ? GridW : GridH;   // длина ряда вдоль оси грани
		const int32 FixedMax = Dir.bRunAlongX ? GridH : GridW;   // сколько таких рядов

		// Узел ряда: К-й угол сетки вдоль ряда. Грани ряда стоят на ребре между
		// камнем и открытой клеткой, поэтому их концы — узлы сетки, а не произвольные
		// точки: крышка камня и пол комнаты держатся за тот же самый узел.
		auto RunCorner = [&](const FWallDir& RunDir, int32 Fixed, int32 K, int32& CX, int32& CY)
		{
			if (RunDir.bRunAlongX)
			{
				CX = K;
				CY = (RunDir.DY > 0) ? (Fixed + 1) : Fixed;   // ряд идёт по X: Y грани постоянен
			}
			else
			{
				CX = (RunDir.DX > 0) ? (Fixed + 1) : Fixed;   // ряд идёт по Y: X грани постоянен
				CY = K;
			}
		};

		for (int32 F = 0; F < FixedMax; ++F)
		{
			int32 R = 0;
			while (R < RunMax)
			{
				const int32 X = Dir.bRunAlongX ? R : F;
				const int32 Y = Dir.bRunAlongX ? F : R;

				if (!WallFaceHere(X, Y, Di)) { ++R; continue; }

				int32 StartCX = 0, StartCY = 0, NextCX = 0, NextCY = 0;
				RunCorner(Dir, F, R,     StartCX, StartCY);
				RunCorner(Dir, F, R + 1, NextCX,  NextCY);

				// Ряд сливаем только пока все его узлы стоят на сетке. Как только в
				// ряду попадается переставленный узел, ряд обрывается: иначе плитка
				// стены пошла бы по диагонали сквозь клетки и разошлась бы с крышками
				// и полом (стена висела бы над полом, а у прохода оставалась бы дыра).
				bool bAllGrid = !CornerIsMoved(StartCX, StartCY) && !CornerIsMoved(NextCX, NextCY);

				int32 Run = 1;
				while (bAllGrid && R + Run < RunMax)
				{
					const int32 RX = Dir.bRunAlongX ? (R + Run) : F;
					const int32 RY = Dir.bRunAlongX ? F : (R + Run);
					if (!WallFaceHere(RX, RY, Di)) break;

					int32 NX = 0, NY = 0, MX = 0, MY = 0;
					RunCorner(Dir, F, R + Run,     NX, NY);
					RunCorner(Dir, F, R + Run + 1, MX, MY);
					if (CornerIsMoved(NX, NY) || CornerIsMoved(MX, MY)) break;

					++Run;
				}

				int32 EndCX = 0, EndCY = 0;
				RunCorner(Dir, F, R + Run, EndCX, EndCY);

				// края стены = положение узлов сетки: прежнее или переставленное вместе
				// с круглой комнатой — то же самое, из чего сложен край крышки и пола
				const FVector2D EndA = CornerAt(StartCX, StartCY);
				const FVector2D EndB = CornerAt(EndCX, EndCY);

				// если хотя бы один край стены сошёл с линии сетки, эта грань идёт
				// сквозь клетки и режет их крышку и пол ровно по себе
				if (CornerIsMoved(StartCX, StartCY) || CornerIsMoved(EndCX, EndCY))
				{
					AddWallCut(EndA, EndB, FIntPoint(X, Y), FIntPoint(X + Dir.DX, Y + Dir.DY));
				}

				// в какую сторону ряда смотреть как на «левый» угол (чтобы L = Normal x Up)
				const bool bRunGrowing = (Dir.DY != 0) ? (Dir.DY > 0) : (Dir.DX < 0);

				const FVector2D& Left  = bRunGrowing ? EndA : EndB;
				const FVector2D& Right = bRunGrowing ? EndB : EndA;

				AddWallRect(CellLocal(static_cast<float>(Left.X) * CS, static_cast<float>(Left.Y) * CS, 0.f),
				            CellLocal(static_cast<float>(Right.X) * CS, static_cast<float>(Right.Y) * CS, 0.f),
				            Dir.Normal,
				            UV(static_cast<float>(bRunGrowing ? R : (R + Run)) * CS),
				            UV(static_cast<float>(bRunGrowing ? (R + Run) : R) * CS));

				++WallRects;
				WallCellFaces += Run;
				R += Run;
			}
		}
	}

	// ---------------------------------------------------------------------
	//  ПЕРЕСТАВЛЕННЫЕ ГРАНИ комнат формы «Круглая»: те же грани стены, что
	//  генератор ставил на границе камня и открытой клетки, но их концы
	//  подтянуты на эллипс комнаты. Обход идёт по кругу, поэтому счётчик длины
	//  плитки идёт подряд по всей круглой стене; заново он начинается только там,
	//  где грань осталась на прежнем месте (у входа или там, где через комнату
	//  идёт коридор) — такие грани обход стен построил как обычно.
	// ---------------------------------------------------------------------
	int32 ArcFaces = 0;
	int32 KeptFaces = 0;
	for (const FRoundRoom& R : RoundRooms)
	{
		const FVector2D CenterCM(R.Center.X * CS, R.Center.Y * CS);
		float ArcLen = 0.f;

		for (const FRoundFace& Face : R.Faces)
		{
			if (!Face.bArc)
			{
				++KeptFaces;
				ArcLen = 0.f;
				continue;
			}

			FVector2D A = Face.A;
			FVector2D B = Face.B;

			// порядок обхода подобран так, что L = Normal x Up уже даёт переднюю грань
			// внутрь комнаты; если на кривом участке границы вышло наоборот, грань и
			// плитку разворачиваем
			FVector L(B.X - A.X, B.Y - A.Y, 0.f);
			FVector FaceNormal = FVector::CrossProduct(FVector(0, 0, 1), L);
			const FVector2D Mid((A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f);
			const FVector Inward(R.Center.X - Mid.X, R.Center.Y - Mid.Y, 0.f);

			bool bMirror = false;
			if (FVector::DotProduct(FaceNormal, Inward) < 0.f)
			{
				Swap(A, B);
				FaceNormal = -FaceNormal;
				bMirror = true;
			}

			const float SegCM = (B - A).Size() * CS;
			const float U0 = UV(ArcLen);
			ArcLen += SegCM;
			const float U1 = UV(ArcLen);

			// ребро переставленной стены режет плиты крышки и пола ровно по себе
			AddWallCut(A, B, Face.RockCell, Face.OpenCell);

			AddArcQuad(A * CS, B * CS, FaceNormal, CenterCM, bMirror ? U1 : U0, bMirror ? U0 : U1);
			++ArcFaces;
		}
	}

	const int32 WallWelded = WeldProcMesh(Vertices, Triangles, Normals, UVs);

	// одна секция на все стены; последним параметром включаем постройку коллизии
	if (Vertices.Num() > 0)
	{
		WallsMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs,
		                             TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
		WallsMesh->SetMeshSectionVisible(0, true);
	}

	UE_LOG(LogTemp, Log, TEXT("Dungeon walls: %d прямоугольников + %d граней дуги вместо %d отдельных граней (%d vertices, %d triangles, сварено вершин %d)"),
	       WallRects, ArcFaces, WallCellFaces + ArcFaces, Vertices.Num(), Triangles.Num() / 3, WallWelded);

	// Крыша: клетки 0 (пустоту) накрываем плитами на высоте WH, клетки 1 (комнаты и
	// дороги) остаются открытыми — это ямы в каменной плите. Плиты берём по верхнему
	// краю тех же стен (z = WH), поэтому кромка крыши совпадает с верхним краем стен
	// и оболочка получается без щелей. Соседние крышки объединяются прямоугольниками
	// СРАЗУ ПО ДВУМ ОСЯМ (CollectRects): плита тянется и по X, и по Y, поэтому вместо
	// квадрата на каждую клетку по всему полю — несколько больших плит.
	if (bBuildRoof && RoofMesh)
	{
		const TArray<uint8> RoofMark = BuildRoundCellMarks();   // клетки камня с переставленным углом
		CollectRects(0, &RoofMark);   // какие клетки нужно накрыть

		TArray<FVector> RoofVerts;
		TArray<int32> RoofTris;
		TArray<FVector> RoofNormals;
		TArray<FVector2D> RoofUVs;
		RoofVerts.Reserve(Rects.Num() * 4);
		RoofTris.Reserve(Rects.Num() * 6);
		RoofNormals.Reserve(Rects.Num() * 4);
		RoofUVs.Reserve(Rects.Num() * 4);

		int32 RoofCells = 0;
		for (const FIntRect& R : Rects)
			AddFlatRect(RoofVerts, RoofTris, RoofNormals, RoofUVs, R, WH, RoofCells);

		// Крышки клеток, задетых переставленной стеной: такую клетку делит ребро той
		// же стены, и крышке достаётся половина со стороны камня. Граница половины
		// лежит ровно на ребре стены, поэтому кромка крышки совпадает с верхом стены
		// точка в точку, и за стену ничего не высунуто.
		int32 RoofRoundCells = 0;
		int32 RoofQuadFails = 0;
		for (int32 Y = 0; Y < GridH; ++Y)
		{
			for (int32 X = 0; X < GridW; ++X)
			{
				if (RoofMark[Idx(X, Y)] == 0) continue;

				TArray<TArray<FVector2D>> Pieces;
				bool bSplit = false;
				CellCutPieces(X, Y, /*bRockSide=*/true, Pieces, bSplit);

				int32 Added = 0;
				for (const TArray<FVector2D>& Poly : Pieces)
				{
					if (AddFlatPolyCM(RoofVerts, RoofTris, RoofNormals, RoofUVs, Poly, WH) > 0) ++Added;
				}

				if (Added == 0 && !bSplit && Grid[Idx(X, Y)] == 0)
				{
					// такую крышку клетки собрать не удалось — берём обычную крышку,
					// чтобы в покрытии камня не осталось дыры
					AddFlatQuadCM(RoofVerts, RoofTris, RoofNormals, RoofUVs,
					              static_cast<float>(X) * CS, static_cast<float>(Y) * CS,
					              static_cast<float>(X + 1) * CS, static_cast<float>(Y + 1) * CS, WH);
					++Added;
				}

				if (Added == 0)
				{
					++RoofQuadFails;
				}
				else
				{
					++RoofRoundCells;
					++RoofCells;
				}
			}
		}

		const int32 RoofWelded = WeldProcMesh(RoofVerts, RoofTris, RoofNormals, RoofUVs);

		if (RoofVerts.Num() > 0)
		{
			RoofMesh->CreateMeshSection(0, RoofVerts, RoofTris, RoofNormals, RoofUVs,
			                            TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
			RoofMesh->SetMeshSectionVisible(0, true);
		}

		UE_LOG(LogTemp, Log, TEXT("Dungeon roof: %d плит (%d общих + %d с переставленными углами) накрыли %d клеток, %d vertices, %d triangles, сварено вершин %d, отменено узлов %d"),
		       Rects.Num() + RoofRoundCells, Rects.Num(), RoofRoundCells, RoofCells, RoofVerts.Num(), RoofTris.Num() / 3, RoofWelded, RoofQuadFails);

		if (ArcFaces > 0)
			UE_LOG(LogTemp, Log, TEXT("Dungeon round walls: круглых комнат %d, граней переставлено на эллипс %d, у входов и коридоров осталось на месте %d"),
			       RoundRooms.Num(), ArcFaces, KeptFaces);
	}

	// Пол: плоские плиты над клетками 1 (комнаты и дороги) на высоте 0 — те же
	// объединённые прямоугольники, что и у крыши, только наоборот: пол покрывает
	// открытые клетки, а не пустоту. Прежняя ОДНА плоскость Plane, растянутая под всей
	// сеткой, показывала одну текстуру на десятки метров — отсюда и растяжение. Теперь
	// пол тайлируется как стены и крыша: одна плитка = Tile сантиметров, швы плиток
	// стоят на клетках сетки и совпадают со швами стен.
	if (FloorMesh)
	{
		FloorMesh->ClearAllMeshSections();

		TArray<FVector> FloorVerts;
		TArray<int32> FloorTris;
		TArray<FVector> FloorNormals;
		TArray<FVector2D> FloorUVs;

		int32 FloorCells = 0;
		int32 FloorRoundCells = 0;   // заполняется ниже, в блоке пола
		int32 FloorQuadFails = 0;
		if (bBuildFloor)
		{
			const TArray<uint8> FloorMark = BuildRoundCellMarks();   // клетки комнат с переставленным углом
			CollectRects(1, &FloorMark);
			FloorVerts.Reserve(Rects.Num() * 4);
			FloorTris.Reserve(Rects.Num() * 6);
			FloorNormals.Reserve(Rects.Num() * 4);
			FloorUVs.Reserve(Rects.Num() * 4);

			for (const FIntRect& R : Rects)
				AddFlatRect(FloorVerts, FloorTris, FloorNormals, FloorUVs, R, 0.f, FloorCells);

			// Пол клеток, задетых переставленной стеной: ту же клетку делит ребро той
			// же стены, и полу достаётся половина со стороны комнаты. Её граница лежит
			// ровно на ребре стены, то есть стена стоит НА полу, а не висит над ямой,
			// и за стену пол не уходит. Половина комнаты, отрезанная от каменной клетки,
			// тоже достаётся полу — так у круглой комнаты не появляется дыры в углу.
			for (int32 Y = 0; Y < GridH; ++Y)
			{
				for (int32 X = 0; X < GridW; ++X)
				{
					if (FloorMark[Idx(X, Y)] == 0) continue;

					TArray<TArray<FVector2D>> Pieces;
					bool bSplit = false;
					CellCutPieces(X, Y, /*bRockSide=*/false, Pieces, bSplit);

					int32 Added = 0;
					for (const TArray<FVector2D>& Poly : Pieces)
					{
						if (AddFlatPolyCM(FloorVerts, FloorTris, FloorNormals, FloorUVs, Poly, 0.f) > 0) ++Added;
					}

					if (Added == 0 && !bSplit && Grid[Idx(X, Y)] == 1)
					{
						// такой пол собрать не удалось — берём обычный пол клетки,
						// чтобы в покрытии комнаты не осталось дыры
						AddFlatQuadCM(FloorVerts, FloorTris, FloorNormals, FloorUVs,
						              static_cast<float>(X) * CS, static_cast<float>(Y) * CS,
						              static_cast<float>(X + 1) * CS, static_cast<float>(Y + 1) * CS, 0.f);
						++Added;
					}

					if (Added == 0)
					{
						++FloorQuadFails;
					}
					else
					{
						++FloorRoundCells;
						++FloorCells;
					}
				}
			}
		}

		const int32 FloorWelded = WeldProcMesh(FloorVerts, FloorTris, FloorNormals, FloorUVs);

		if (FloorVerts.Num() > 0)
		{
			FloorMesh->CreateMeshSection(0, FloorVerts, FloorTris, FloorNormals, FloorUVs,
			                             TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
			FloorMesh->SetMeshSectionVisible(0, true);
		}

		UE_LOG(LogTemp, Log, TEXT("Dungeon floor: %d плит (%d общих + %d с переставленными углами) на %d клеток, %d vertices, %d triangles, сварено вершин %d, отменено узлов %d"),
		       bBuildFloor ? Rects.Num() + FloorRoundCells : 0, bBuildFloor ? Rects.Num() : 0, FloorRoundCells, FloorCells, FloorVerts.Num(), FloorTris.Num() / 3, FloorWelded, FloorQuadFails);
	}

	// Старая растянутая плоскость пола больше не участвует: прячем её, но материал,
	// который был назначен вручную именно на ней, переносим на новый пол, чтобы пол
	// после обновления не остался вовсе без материала.
	if (FloorPlane)
	{
		if (!FloorMaterial)
		{
			const UStaticMesh* PlaneAsset = FloorPlane->GetStaticMesh();
			UMaterialInterface* PlaneMaterial = FloorPlane->GetMaterial(0);
			UMaterialInterface* PlaneOwnMaterial = PlaneAsset ? PlaneAsset->GetMaterial(0) : nullptr;
			if (PlaneAsset && PlaneMaterial && PlaneMaterial != PlaneOwnMaterial)
				FloorMaterial = PlaneMaterial;
		}
		FloorPlane->SetVisibility(false);
		FloorPlane->SetHiddenInGame(true);
	}

	// Материалы из свойств актора кладём в нулевой слот мешей. Если свойство пустое —
	// ничего не трогаем: тогда работает обычное ручное назначение материала на компоненте.
	auto ApplyMaterial = [&](UProceduralMeshComponent* Mesh, UMaterialInterface* Material)
	{
		if (Mesh && Material) Mesh->SetMaterial(0, Material);
	};
	ApplyMaterial(WallsMesh, WallsMaterial);
	ApplyMaterial(RoofMesh, RoofMaterial);
	ApplyMaterial(FloorMesh, FloorMaterial);

	// маркеры старта и финиша
	if (bSpawnMarkers)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			auto SpawnMarker = [&](ERoomKind Kind, FLinearColor Color)
			{
				for (const FPlacedRoom& R : Rooms)
				{
					if (R.Kind != Kind) continue;
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					APointLight* Light = World->SpawnActor<APointLight>(
						APointLight::StaticClass(), GetActorTransform().TransformPosition(CellLocal((R.CenterX + 0.5f) * CS, (R.CenterY + 0.5f) * CS, 150.f)),
						FRotator::ZeroRotator, Params);
					if (Light)
					{
						// Тег обязателен: по нему следующий Generate() найдёт и удалит этот
						// свет, даже если список Markers окажется пустым.
						Light->Tags.AddUnique(DungeonMarkerTag);
#if WITH_EDITOR
						Light->SetActorLabel(Kind == ERoomKind::Start ? TEXT("DungeonMarker_Start")
						                                              : TEXT("DungeonMarker_End"));
#endif
						UPointLightComponent* LightComponent = Cast<UPointLightComponent>(Light->GetLightComponent());
						if (LightComponent)
						{
							LightComponent->SetLightColor(Color);
							LightComponent->SetIntensity(8000.f);
							LightComponent->SetAttenuationRadius(CellSize * 12.f);
						}
						Markers.Add(Light);
					}
					break;
				}
			};
			SpawnMarker(ERoomKind::Start, FLinearColor(1.f, 0.9f, 0.4f));
			SpawnMarker(ERoomKind::End,   FLinearColor(1.f, 0.25f, 0.2f));
		}
	}
}