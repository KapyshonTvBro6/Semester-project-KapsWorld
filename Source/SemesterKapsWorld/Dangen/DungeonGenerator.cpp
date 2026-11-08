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
//  СКРУГЛЕНИЕ КОМНАТ ФОРМЫ «КРУГЛАЯ»
//
//  Стена такой комнаты собирается не лесенкой по клеткам, а ломаной из
//  хорд окружности, вписанной в прямоугольник комнаты: дуга касается
//  прямых стен ровно там же, где окружность касается сторон прямоугольника
//  (как в макете: грань через каждые 22.5° при 16 гранях на круг).
//
//  Старые грани-лесенки при этом НЕ удаляются: они надёжно держат дороги,
//  двери и навигацию. Дуга строится внутри уже вырезанных клеток комнаты,
//  а всё, что осталось между дугой и прежней стеной, накрывается крышкой
//  крыши — снаружи контур комнаты выглядит ровно по дуге, без уголков.
//
//  Масштаб дуги подбирается автоматически: он чуть меньше единицы, чтобы
//  вся дуга прошла внутри клеток комнаты с запасом RoundArcMargin. Без
//  этого дуга местами вылезала бы за угол клетки и вставала бы впритык к
//  прежней стене — тогда рядом были бы видны и гладкая дуга, и ступеньки.
//
//  Там, где к клетке комнаты примыкает дорога или чужая комната (вход),
//  дуга разрывается: проход должен остаться открытым.
// =====================================================================

// Запас от вершины дуги до границы открытой клетки, в долях клетки.
static const float RoundArcMargin = 0.18f;

static float WrapAngle2Pi(float Angle)
{
	const float Two = 2.f * UE_PI;
	Angle = FMath::Fmod(Angle, Two);
	if (Angle < 0.f) Angle += Two;
	return Angle;
}

int32 ADungeonGenerator::RoundArcSampleCount(const FPlacedRoom& Room) const
{
	const int32 Base = FMath::Clamp(WallArcSegments, 8, 128);
	// Большой комнате нужно больше граней, иначе одна грань растягивается на
	// пару метров и круг снова выглядит гранёным.
	const int32 Bigger = FMath::Max(1, FMath::Max(Room.W, Room.H) / 8);
	return Base * Bigger;
}

// Клетка сетки открыта и принадлежит именно этой комнате.
bool ADungeonGenerator::RoomCellOwned(int32 X, int32 Y, int32 RoomIndex) const
{
	if (!InBounds(X, Y)) return false;
	const int32 I = Idx(X, Y);
	if (Grid[I] == 0) return false;
	return RoomMap[I] == RoomIndex;
}

// Клетка принадлежит комнате и при этом не занята коридором.
bool ADungeonGenerator::RoomCellFree(int32 X, int32 Y, int32 RoomIndex) const
{
	if (!RoomCellOwned(X, Y, RoomIndex)) return false;
	return RoadMask.Num() != GridW * GridH || RoadMask[Idx(X, Y)] == 0;
}

// Точка дуги (в клетках) стоит внутри клетки комнаты и не ближе Margin к её краю.
bool ADungeonGenerator::ArcPointFitsRoom(float GX, float GY, const FPlacedRoom& Room, float Margin) const
{
	const int32 CX = FMath::FloorToInt(GX);
	const int32 CY = FMath::FloorToInt(GY);
	if (!RoomCellOwned(CX, CY, Room.Index)) return false;

	const float FX = GX - static_cast<float>(CX);
	const float FY = GY - static_cast<float>(CY);
	// у границы клетки, за которой чужая клетка, дуга не подходит вплотную
	if (FX < Margin && !RoomCellOwned(CX - 1, CY, Room.Index)) return false;
	if (FX > 1.f - Margin && !RoomCellOwned(CX + 1, CY, Room.Index)) return false;
	if (FY < Margin && !RoomCellOwned(CX, CY - 1, Room.Index)) return false;
	if (FY > 1.f - Margin && !RoomCellOwned(CX, CY + 1, Room.Index)) return false;
	return true;
}

// Нельзя строить грань, если её хорда пересекает клетку коридора: гладкая стена
// встала бы поперёк прохода и разрезала подземелье. Такое возможно, когда коридор
// идёт сквозь комнату (дороги не ломают клетки комнат, поэтому клетки такого
// коридора по-прежнему принадлежат комнате).
bool ADungeonGenerator::ArcFaceCrossesRoad(float AX, float AY, float BX, float BY) const
{
	if (RoadMask.Num() != GridW * GridH) return false;

	const float DX = BX - AX;
	const float DY = BY - AY;
	const float Len = FMath::Sqrt(DX * DX + DY * DY);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len * 2.f));   // шаг ~0.5 клетки

	for (int32 s = 0; s <= Steps; ++s)
	{
		const float T = static_cast<float>(s) / static_cast<float>(Steps);
		const int32 CX = FMath::FloorToInt(AX + DX * T);
		const int32 CY = FMath::FloorToInt(AY + DY * T);
		if (!InBounds(CX, CY)) continue;
		if (RoadMask[Idx(CX, CY)] != 0) return true;
	}
	return false;
}

// Угловые окна, в которых дугу надо разрывать: там клетка комнаты соседствует
// с открытой чужой клеткой (дорога или другая комната) — это вход в комнату.
// Окна заданы в параметрическом угле эллипса: при равномерном уменьшении
// дуги (одинаковый масштаб по обеим полуосям) эти углы не меняются.
void ADungeonGenerator::CollectDoorWindows(const FPlacedRoom& Room, const FVector2D& Center, const FVector2D& Semi,
                                           TArray<TPair<float, float>>& Out) const
{
	Out.Reset();

	const float Two = 2.f * UE_PI;
	const float Step = Two / static_cast<float>(FMath::Max(8, RoundArcSampleCount(Room)));
	const float Pad  = Step * 0.5f + 0.01f;   // немного расширить окно, чтобы грань не легла на вход

	auto ParamAngle = [&](float GX, float GY)
	{
		return FMath::Atan2((GY - Center.Y) / Semi.Y, (GX - Center.X) / Semi.X);
	};

	const FIntPoint Neighbours[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	for (int32 Y = Room.Y; Y < Room.Y + Room.H; ++Y)
	{
		for (int32 X = Room.X; X < Room.X + Room.W; ++X)
		{
			if (!RoomCellOwned(X, Y, Room.Index)) continue;

			for (const FIntPoint& D : Neighbours)
			{
				const int32 NX = X + D.X;
				const int32 NY = Y + D.Y;
				if (!InBounds(NX, NY)) continue;

				const int32 I = Idx(NX, NY);
				if (Grid[I] == 0) continue;              // сосед — камень, прохода тут нет
				if (RoomMap[I] == Room.Index) continue;  // сосед — своя клетка комнаты

				// концы общего ребра двух клеток
				float A0, A1;
				if (D.X != 0)
				{
					const float EX = (D.X > 0) ? static_cast<float>(X + 1) : static_cast<float>(X);
					A0 = ParamAngle(EX, static_cast<float>(Y));
					A1 = ParamAngle(EX, static_cast<float>(Y + 1));
				}
				else
				{
					const float EY = (D.Y > 0) ? static_cast<float>(Y + 1) : static_cast<float>(Y);
					A0 = ParamAngle(static_cast<float>(X), EY);
					A1 = ParamAngle(static_cast<float>(X + 1), EY);
				}

				// приводим окно к [0, 2π), расширяем на Pad и, если оно переходит
				// через ноль, режем на две части
				float Delta = A1 - A0;
				while (Delta >  UE_PI) Delta -= Two;
				while (Delta < -UE_PI) Delta += Two;

				const float Mid  = WrapAngle2Pi(A0 + Delta * 0.5f);
				const float Half = FMath::Abs(Delta) * 0.5f + Pad;
				const float Lo   = Mid - Half;
				const float Hi   = Mid + Half;

				if (Lo < 0.f)
				{
					Out.Add(TPair<float, float>(Lo + Two, Two));
					Out.Add(TPair<float, float>(0.f, WrapAngle2Pi(Hi)));
				}
				else if (Hi >= Two)
				{
					Out.Add(TPair<float, float>(Lo, Two));
					Out.Add(TPair<float, float>(0.f, WrapAngle2Pi(Hi - Two)));
				}
				else
				{
					Out.Add(TPair<float, float>(Lo, Hi));
				}
			}
		}
	}
}

bool ADungeonGenerator::AngleInWindows(float Angle, const TArray<TPair<float, float>>& Windows) const
{
	const float A = WrapAngle2Pi(Angle);
	for (const TPair<float, float>& W : Windows)
	{
		if (A >= W.Key && A <= W.Value) return true;
	}
	return false;
}

// наибольший масштаб дуги (1 = ровно вписанная окружность), при котором вся
// дуга, не считая дверных окон, проходит внутри клеток комнаты с запасом.
// 0 = подобрать не удалось: комната слишком «квадратная», скругляем её прежними гранями.
float ADungeonGenerator::RoundArcScale(const FPlacedRoom& Room, const FVector2D& Center, const FVector2D& Semi,
                                       int32 Samples, const TArray<TPair<float, float>>& Windows) const
{
	const float Two = 2.f * UE_PI;
	const int32 March = 4;   // сколько точек проверяем между соседними вершинами дуги

	for (int32 i = 0; i < 24; ++i)
	{
		const float S = 1.f - 0.03f * static_cast<float>(i);
		if (S < 0.45f) return 0.f;

		bool bFits = true;
		for (int32 k = 0; k < Samples && bFits; ++k)
		{
			const float A0 = Two * static_cast<float>(k) / static_cast<float>(Samples);
			const float A1 = Two * static_cast<float>(k + 1) / static_cast<float>(Samples);

			for (int32 m = 0; m < March; ++m)
			{
				const float A = A0 + (A1 - A0) * (static_cast<float>(m) / static_cast<float>(March));
				if (AngleInWindows(A, Windows)) continue;   // тут дугу разрывает вход

				const float GX = Center.X + Semi.X * S * FMath::Cos(A);
				const float GY = Center.Y + Semi.Y * S * FMath::Sin(A);
				if (!ArcPointFitsRoom(GX, GY, Room, RoundArcMargin)) { bFits = false; break; }
			}
		}

		if (bFits) return S;
	}

	return 0.f;
}

void ADungeonGenerator::BuildRoundRooms(TArray<FRoundRoom>& Out) const
{
	Out.Reset();
	if (!bRoundCircleWalls) return;
	if (Grid.Num() != GridW * GridH || RoomMap.Num() != GridW * GridH) return;

	const float Two = 2.f * UE_PI;

	for (const FPlacedRoom& Room : Rooms)
	{
		if (Room.Shape != ERoomShape::Circle) continue;
		if (Room.W < 4 || Room.H < 4) continue;   // на 3 клетках скруглять нечего

		FRoundRoom R;
		R.RoomIndex = Room.Index;
		R.Center    = FVector2D(static_cast<float>(Room.X) + Room.W * 0.5f,
		                        static_cast<float>(Room.Y) + Room.H * 0.5f);
		R.Semi      = FVector2D(Room.W * 0.5f, Room.H * 0.5f);
		R.Samples   = FMath::Max(8, RoundArcSampleCount(Room));

		TArray<TPair<float, float>> Windows;
		CollectDoorWindows(Room, R.Center, R.Semi, Windows);

		const float S = RoundArcScale(Room, R.Center, R.Semi, R.Samples, Windows);
		if (S <= 0.f) continue;
		R.Semi *= S;

		// вершины дуги по кругу и отметки разрывов (входы в комнату)
		R.Pts.Init(FVector2D::ZeroVector, R.Samples);
		R.Blocked.Init(0, R.Samples);
		for (int32 k = 0; k < R.Samples; ++k)
		{
			const float A  = Two * static_cast<float>(k) / static_cast<float>(R.Samples);
			const float GX = R.Center.X + R.Semi.X * FMath::Cos(A);
			const float GY = R.Center.Y + R.Semi.Y * FMath::Sin(A);
			R.Pts[k] = FVector2D(GX, GY);

			if (AngleInWindows(A, Windows))
			{
				R.Blocked[k] = 1;
				continue;
			}
			if (!RoomCellOwned(FMath::FloorToInt(GX), FMath::FloorToInt(GY), Room.Index))
				R.Blocked[k] = 1;
		}

		int32 Open = 0;
		for (uint8 b : R.Blocked) if (!b) ++Open;
		if (Open < 2) continue;   // почти весь круг под входами — обычная стена лучше

		// Клетки-уголки: клетка комнаты целиком в дугу не помещается -> то, что
		// осталось за дугой, надо накрыть крышкой.
		for (int32 Y = Room.Y; Y < Room.Y + Room.H; ++Y)
		{
			for (int32 X = Room.X; X < Room.X + Room.W; ++X)
			{
				if (!RoomCellFree(X, Y, Room.Index)) continue;   // по клетке идёт коридор — крышу тут не ставим

				bool bWholeInside = true;
				for (int32 i = 0; i < 4 && bWholeInside; ++i)
				{
					const float CornerX = static_cast<float>(X) + ((i & 1) ? 1.f : 0.f);
					const float CornerY = static_cast<float>(Y) + ((i & 2) ? 1.f : 0.f);
					const float NX = (CornerX - R.Center.X) / R.Semi.X;
					const float NY = (CornerY - R.Center.Y) / R.Semi.Y;
					if (NX * NX + NY * NY > 1.f) bWholeInside = false;
				}

				if (!bWholeInside) R.CapCells.Add(FIntPoint(X, Y));
			}
		}

		Out.Add(MoveTemp(R));
	}
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

	// Дуги стен комнат формы «Круглая»: считаем один раз, они нужны и стенам,
	// и крышке уголков пола, оставшихся за дугой.
	TArray<FRoundRoom> RoundRooms;
	BuildRoundRooms(RoundRooms);

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
	auto CollectRects = [&](uint8 WantedValue)
	{
		Rects.Reset();
		if (Grid.Num() != GridW * GridH) return;

		TArray<uint8> Used;
		Used.Init(0, GridW * GridH);

		for (int32 Y = 0; Y < GridH; ++Y)
		{
			for (int32 X = 0; X < GridW; ++X)
			{
				if (Grid[Idx(X, Y)] != WantedValue || Used[Idx(X, Y)]) continue;

				// 1) растягиваем полосу по X
				int32 RectW = 1;
				while (X + RectW < GridW && Grid[Idx(X + RectW, Y)] == WantedValue && !Used[Idx(X + RectW, Y)])
					++RectW;

				// 2) опускаем полосу по Y, пока весь ряд прямоугольника подходит
				int32 RectH = 1;
				while (Y + RectH < GridH)
				{
					bool bWholeRow = true;
					for (int32 i = 0; i < RectW; ++i)
					{
						if (Grid[Idx(X + i, Y + RectH)] != WantedValue || Used[Idx(X + i, Y + RectH)]) { bWholeRow = false; break; }
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

	// =====================================================================
	//  СТЕНЫ: соседние грани ОБЪЕДИНЯЮТСЯ.
	//  Грань задаётся камнем (клетка 0) и направлением к открытой клетке (клетка 1).
	//  У двух идущих подряд камней с ОДИНАКОВЫМ направлением их грани лежат в одной
	//  плоскости и стыкуются без шва, поэтому вместо отдельного квадрата на каждую
	//  клетку строим ОДИН прямоугольник на весь ряд: стена из 4 клеток — это один
	//  прямоугольник шириной 4 * CellSize и высотой WallHeight.
	//  Стена всегда идёт на всю высоту, поэтому вертикального объединения тут нет.
	// =====================================================================
	struct FWallDir
	{
		int32   DX, DY;      // смещение к открытой клетке; оно же — нормаль грани (внутрь подземелья)
		bool    bRunAlongX;  // по какой оси соседствуют одинаковые грани (вдоль неё тянем ряд)
		FVector Normal;
	};

	const FWallDir Dirs[4] = {
		// Юг (Y+): открытая клетка по +Y; ряд таких граней идёт вдоль X
		{ 0,  1, true,  FVector(0,  1, 0) },
		// Север (Y-): открытая клетка по -Y
		{ 0, -1, true,  FVector(0, -1, 0) },
		// Восток (X+): открытая клетка по +X; ряд таких граней идёт вдоль Y
		{ 1,  0, false, FVector(1,  0, 0) },
		// Запад (X-): открытая клетка по -X
		{ -1, 0, false, FVector(-1, 0, 0) },
	};

	// нужна ли этой клетке грань в этом направлении
	auto WallFaceHere = [&](int32 X, int32 Y, const FWallDir& Dir)
	{
		if (Grid[Idx(X, Y)] != 0) return false;                  // не камень
		const int32 NX = X + Dir.DX, NY = Y + Dir.DY;
		return InBounds(NX, NY) && Grid[Idx(NX, NY)] == 1;      // сосед — комната или дорога
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

	for (const FWallDir& Dir : Dirs)
	{
		const int32 RunMax   = Dir.bRunAlongX ? GridW : GridH;   // длина ряда вдоль оси грани
		const int32 FixedMax = Dir.bRunAlongX ? GridH : GridW;   // сколько таких рядов

		for (int32 F = 0; F < FixedMax; ++F)
		{
			int32 R = 0;
			while (R < RunMax)
			{
				const int32 X = Dir.bRunAlongX ? R : F;
				const int32 Y = Dir.bRunAlongX ? F : R;

				if (!WallFaceHere(X, Y, Dir)) { ++R; continue; }

				// тянем ряд вперёд: пока у следующего камня грань той же ориентации,
				// она лежит в той же плоскости и стыкуется с текущей
				int32 Run = 1;
				while (R + Run < RunMax)
				{
					const int32 RX = Dir.bRunAlongX ? (R + Run) : F;
					const int32 RY = Dir.bRunAlongX ? F : (R + Run);
					if (!WallFaceHere(RX, RY, Dir)) break;
					++Run;
				}

				// ряд занимает клетки [R .. R+Run-1] -> вдоль ряда координаты от R*CS до
				// (R+Run)*CS; плоскость самой грани — на границе камня и открытой клетки
				const float S0 = static_cast<float>(R) * CS;
				const float S1 = static_cast<float>(R + Run) * CS;

				const float Plane = (Dir.DX != 0)
					? static_cast<float>(Dir.DX > 0 ? (F + 1) : F) * CS   // восток/запад: X грани
					: static_cast<float>(Dir.DY > 0 ? (F + 1) : F) * CS;  // юг/север:   Y грани

				// в какую сторону ряда смотреть как на «левый» угол (чтобы L = Normal x Up)
				const bool bRunGrowing = (Dir.DY != 0) ? (Dir.DY > 0) : (Dir.DX < 0);

				const float ULeft  = bRunGrowing ? S0 : S1;
				const float URight = bRunGrowing ? S1 : S0;

				FVector BottomLeft, BottomRight;
				if (Dir.bRunAlongX)
				{
					BottomLeft  = CellLocal(ULeft,  Plane, 0.f);
					BottomRight = CellLocal(URight, Plane, 0.f);
				}
				else
				{
					BottomLeft  = CellLocal(Plane, ULeft,  0.f);
					BottomRight = CellLocal(Plane, URight, 0.f);
				}

				AddWallRect(BottomLeft, BottomRight, Dir.Normal, UV(ULeft), UV(URight));

				++WallRects;
				WallCellFaces += Run;
				R += Run;
			}
		}
	}

	// ---------------------------------------------------------------------
	//  ГЛАДКИЕ ДУГИ: стены комнат формы «Круглая».
	//  Обход вершин дуги по кругу; там, где дуга разрывается (вход в комнату),
	// граней нет — проход остаётся открытым, а счётчик длины плитки начинается
	// заново, чтобы текстура на круглой стене была того же размера, что везде.
	// ---------------------------------------------------------------------
	int32 ArcFaces = 0;
	for (const FRoundRoom& R : RoundRooms)
	{
		const FVector2D CenterCM(R.Center.X * CS, R.Center.Y * CS);
		float ArcLen = 0.f;

		for (int32 k = 0; k < R.Samples; ++k)
		{
			const int32 Next = (k + 1) % R.Samples;
			if (R.Blocked[k] || R.Blocked[Next])
			{
				ArcLen = 0.f;
				continue;
			}

			FVector2D A = R.Pts[k];
			FVector2D B = R.Pts[Next];

			// ориентацию подбираем так, чтобы передняя грань смотрела ВНУТРЬ комнаты
			// (тот же порядок обхода, что у прямоугольных граней: L = Normal x Up)
			FVector L(B.X - A.X, B.Y - A.Y, 0.f);
			FVector FaceNormal = FVector::CrossProduct(FVector(0, 0, 1), L);
			const FVector2D Mid((A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f);

			// Грань не ставим, если её хорда пересекает коридор: иначе подземелье
			// оказалось бы разрезано стеной.
			if (ArcFaceCrossesRoad(A.X, A.Y, B.X, B.Y))
			{
				ArcLen = 0.f;
				continue;
			}

			const FVector Inward(R.Center.X - Mid.X, R.Center.Y - Mid.Y, 0.f);
			if (FVector::DotProduct(FaceNormal, Inward) < 0.f)
			{
				Swap(A, B);
				FaceNormal = -FaceNormal;
			}

			const float SegCM = (B - A).Size() * CS;
			const float ULeft  = UV(ArcLen);
			ArcLen += SegCM;
			const float URight = UV(ArcLen);

			AddArcQuad(A * CS, B * CS, FaceNormal, CenterCM, ULeft, URight);
			++ArcFaces;
		}
	}

	// одна секция на все стены; последним параметром включаем постройку коллизии
	if (Vertices.Num() > 0)
	{
		WallsMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs,
		                             TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
		WallsMesh->SetMeshSectionVisible(0, true);
	}

	UE_LOG(LogTemp, Log, TEXT("Dungeon walls: %d прямоугольников вместо %d отдельных граней (%d vertices, %d triangles)"),
	       WallRects, WallCellFaces, Vertices.Num(), Triangles.Num() / 3);

	// Крыша: клетки 0 (пустоту) накрываем плитами на высоте WH, клетки 1 (комнаты и
	// дороги) остаются открытыми — это ямы в каменной плите. Плиты берём по верхнему
	// краю тех же стен (z = WH), поэтому кромка крыши совпадает с верхним краем стен
	// и оболочка получается без щелей. Соседние крышки объединяются прямоугольниками
	// СРАЗУ ПО ДВУМ ОСЯМ (CollectRects): плита тянется и по X, и по Y, поэтому вместо
	// квадрата на каждую клетку по всему полю — несколько больших плит.
	if (bBuildRoof && RoofMesh)
	{
		CollectRects(0);   // какие клетки нужно накрыть

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

		// -----------------------------------------------------------------
		//  КРЫШКА уголков за дугой.
		//  Дуга стоит ВНУТРИ вырезанных клеток (иначе она местами вылезала бы за
		//  угол клетки и вставала бы впритык к прежней стене), поэтому между дугой
		//  и прежней стеной клетки остаются уголки пола. Если их не накрыть, сверху
		//  вокруг круглой стены была бы видна лесенка плитки. Дробим такие клетки на
		//  доли и накрываем только те, что остались ЗА дугой: край крышки идёт почти
		//  по самой дуге, а не ступеньками по клеткам, и снаружи видно ровный круг.
		//  Плиты крышки и крышки камня не перекрываются: крышка занимает только
		//  открытые клетки этой комнаты, камень крышью уже накрыт.
		// -----------------------------------------------------------------
		int32 CapQuads = 0;
		if (bCapRoundCorners)
		{
			const int32 Sub = 8;   // долей клетки на сторону

			for (const FRoundRoom& R : RoundRooms)
			{
				if (R.CapCells.Num() == 0) continue;
				if (!Rooms.IsValidIndex(R.RoomIndex)) continue;
				const FPlacedRoom& Room = Rooms[R.RoomIndex];

				const int32 SW = Room.W * Sub;
				const int32 SH = Room.H * Sub;
				const float  S = static_cast<float>(Sub);

				TArray<uint8> Need;
				Need.Init(0, SW * SH);

				for (const FIntPoint& C : R.CapCells)
				{
					const int32 BX = (C.X - Room.X) * Sub;
					const int32 BY = (C.Y - Room.Y) * Sub;

					for (int32 sy = 0; sy < Sub; ++sy)
					{
						for (int32 sx = 0; sx < Sub; ++sx)
						{
							const float PX = static_cast<float>(C.X) + (static_cast<float>(sx) + 0.5f) / S;
							const float PY = static_cast<float>(C.Y) + (static_cast<float>(sy) + 0.5f) / S;
							const float NX = (PX - R.Center.X) / R.Semi.X;
							const float NY = (PY - R.Center.Y) / R.Semi.Y;
							if (NX * NX + NY * NY > 1.f)
								Need[(BY + sy) * SW + (BX + sx)] = 1;
						}
					}
				}

				// соседние доли объединяем в одну плиту (тот же жадный порядок, что у CollectRects)
				TArray<uint8> Used;
				Used.Init(0, SW * SH);

				for (int32 Y = 0; Y < SH; ++Y)
				{
					for (int32 X = 0; X < SW; ++X)
					{
						const int32 I = Y * SW + X;
						if (!Need[I] || Used[I]) continue;

						int32 RectW = 1;
						while (X + RectW < SW && Need[Y * SW + X + RectW] && !Used[Y * SW + X + RectW])
							++RectW;

						int32 RectH = 1;
						while (Y + RectH < SH)
						{
							bool bWholeRow = true;
							for (int32 i = 0; i < RectW; ++i)
							{
								const int32 J = (Y + RectH) * SW + X + i;
								if (!Need[J] || Used[J]) { bWholeRow = false; break; }
							}
							if (!bWholeRow) break;
							++RectH;
						}

						for (int32 YY = Y; YY < Y + RectH; ++YY)
							for (int32 XX = X; XX < X + RectW; ++XX)
								Used[YY * SW + XX] = 1;

						AddFlatQuadCM(RoofVerts, RoofTris, RoofNormals, RoofUVs,
						              static_cast<float>(Room.X * Sub + X) * CS / S,
						              static_cast<float>(Room.Y * Sub + Y) * CS / S,
						              static_cast<float>(Room.X * Sub + X + RectW) * CS / S,
						              static_cast<float>(Room.Y * Sub + Y + RectH) * CS / S,
						              WH);
						++CapQuads;
					}
				}
			}
		}

		if (RoofVerts.Num() > 0)
		{
			RoofMesh->CreateMeshSection(0, RoofVerts, RoofTris, RoofNormals, RoofUVs,
			                            TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
			RoofMesh->SetMeshSectionVisible(0, true);
		}

		UE_LOG(LogTemp, Log, TEXT("Dungeon roof: %d плит накрыли %d клеток (по-старому было бы %d квадратов), %d vertices, %d triangles"),
		       Rects.Num(), RoofCells, RoofCells, RoofVerts.Num(), RoofTris.Num() / 3);

		if (ArcFaces > 0)
			UE_LOG(LogTemp, Log, TEXT("Dungeon round walls: круглых комнат %d, граней дуги %d, плит крышки уголков %d"),
			       RoundRooms.Num(), ArcFaces, CapQuads);
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
		if (bBuildFloor)
		{
			CollectRects(1);
			FloorVerts.Reserve(Rects.Num() * 4);
			FloorTris.Reserve(Rects.Num() * 6);
			FloorNormals.Reserve(Rects.Num() * 4);
			FloorUVs.Reserve(Rects.Num() * 4);

			for (const FIntRect& R : Rects)
				AddFlatRect(FloorVerts, FloorTris, FloorNormals, FloorUVs, R, 0.f, FloorCells);
		}

		if (FloorVerts.Num() > 0)
		{
			FloorMesh->CreateMeshSection(0, FloorVerts, FloorTris, FloorNormals, FloorUVs,
			                             TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
			FloorMesh->SetMeshSectionVisible(0, true);
		}

		UE_LOG(LogTemp, Log, TEXT("Dungeon floor: %d плит на %d клеток, %d vertices, %d triangles"),
		       bBuildFloor ? Rects.Num() : 0, FloorCells, FloorVerts.Num(), FloorTris.Num() / 3);
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