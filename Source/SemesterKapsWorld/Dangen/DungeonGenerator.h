// DungeonGenerator.h
// Генератор подземелья на сетке (порт веб-генератора в UE5).
//
// Схема работы (как ты и предложил):
//   1. Есть 2D-массив Grid размером GridW x GridH, изначально весь из 0.
//   2. При генерации комнаты и дороги отмечают свои клетки как 1.
//   3. После генерации у клеток со значением 0 строятся только ВИДИМЫЕ грани стен
//      (грани, соседствующие с открытыми клетками) -> один ProceduralMesh; клетки со
//      значением 1 получают пол. Итог: открытый лабиринт внутри каменной оболочки.
//   4. Крыша: те же клетки 0 получают верхние крышки на высоте WallHeight, клетки 1
//      (комнаты и дороги) крышей НЕ закрываются.
//
// СОСЕДНИЕ КУСКИ ОБЪЕДИНЯЮТСЯ (см. BuildGeometry в .cpp):
//   * стена: одинаковые грани идущих подряд камней сливаются в ОДИН прямоугольник на
//     всю длину ряда (стена из 4 клеток = 1 прямоугольник, а не 4 квадрата);
//   * крыша и пол: покрываются прямоугольниками по обеим осям — полоса тянется по X,
//     затем опускается вниз по Y, пока прямоугольник целиком лежит в нужных клетках.
//
// ТАЙЛИНГ ТЕКСТУРЫ (текстура не растягивается): UV всех граней считаются в сантиметрах
// от начала сетки: u = координата / TextureTileSize, v = координата / TextureTileSize.
// Одна плитка текстуры всегда занимает TextureTileSize сантиметров, поэтому и короткая
// стена, и длинный ряд комнат, и весь пол получают одинаковый по масштабу тайлинг,
// а швы плиток совпадают на всех поверхностях (стена/крыша/пол).
//
// Все полигоны стен смотрят ВНУТРЬ подземелья (в сторону дорог и комнат), крыша смотрит вверх.
//
// Каждое Generate() сначала вызывает ClearGenerated(): старая геометрия и старые
// маркеры удаляются целиком, поэтому новая генерация никогда не ложится поверх старой.
//
// Использование: положи актора в уровень, настрой шаблоны комнат и нажми Generate прямо
// в Details (CallInEditor) или вызови Generate() из Blueprint. Материалы:
// WallsMesh / RoofMesh / FloorMesh -> Set Material (0), либо готовые материалы в
// свойствах «Материал стен / крыши / пола» — генератор сам положит их в 0-й слот.
// Тайлинг: UV считаются в сантиметрах, поэтому в материале Tiling = 1, а размер
// плитки задаётся параметром TextureTileSize (0 = одна плитка на одну клетку сетки).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonGenerator.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class APointLight;

UENUM(BlueprintType)
enum class ERoomShape : uint8
{
	Rect    UMETA(DisplayName = "Прямоугольник"),
	Cross   UMETA(DisplayName = "Крест"),
	LShape  UMETA(DisplayName = "Г-образная"),
	Circle  UMETA(DisplayName = "Круглая")
};

UENUM(BlueprintType)
enum class ERoomPattern : uint8
{
	Solid   UMETA(DisplayName = "Однотонная"),
	Checker UMETA(DisplayName = "Шахматная"),
	Bricks  UMETA(DisplayName = "Кирпичи"),
	Noise   UMETA(DisplayName = "Шум")
};

UENUM(BlueprintType)
enum class ERoomCondition : uint8
{
	Any          UMETA(DisplayName = "Где угодно"),
	Edge         UMETA(DisplayName = "У края поля"),
	NearStart    UMETA(DisplayName = "Ближе к старту"),
	FarFromStart UMETA(DisplayName = "Дальше от старта"),
	NextToRoad   UMETA(DisplayName = "Рядом с дорогой (пост-генерация)"),
	NextToRoom   UMETA(DisplayName = "Рядом с комнатой (пост-генерация)")
};

UENUM(BlueprintType)
enum class ERoomKind : uint8
{
	Start,
	Normal,
	End,
	Post
};

// Настраиваемый шаблон комнаты (аналог карточки из веб-версии)
USTRUCT(BlueprintType)
struct FRoomTemplate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Название типа"))
	FString Name = TEXT("Комната");

	// 1. Размеры в клетках: берётся случайный размер между минимальным и максимальным
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2", ClampMax = "40", UIMin = "2", UIMax = "40",
		DisplayName = "Мин. ширина", ToolTip = "Минимальная ширина комнаты в клетках"))
	int32 WMin = 5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2", ClampMax = "40", UIMin = "2", UIMax = "40",
		DisplayName = "Макс. ширина", ToolTip = "Максимальная ширина комнаты в клетках"))
	int32 WMax = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2", ClampMax = "40", UIMin = "2", UIMax = "40",
		DisplayName = "Мин. высота", ToolTip = "Минимальная высота (глубина) комнаты в клетках"))
	int32 HMin = 5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2", ClampMax = "40", UIMin = "2", UIMax = "40",
		DisplayName = "Макс. высота", ToolTip = "Максимальная высота (глубина) комнаты в клетках"))
	int32 HMax = 8;

	// Форма
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Форма",
		ToolTip = "Прямоугольник, крест, Г-образная или круг. Форма строится внутри прямоугольника ширина x высота."))
	ERoomShape Shape = ERoomShape::Rect;

	// 2. Текстура (узор пока хранится для будущего материала / per-instance цвета)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Узор (пока не используется)",
		ToolTip = "Задел на свой материал: Solid, Checker, Bricks или Noise. Сейчас ни на что не влияет."))
	ERoomPattern Pattern = ERoomPattern::Solid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Цвет (пока не используется)",
		ToolTip = "Задел на свой материал. Сейчас ни на что не влияет."))
	FLinearColor Color = FLinearColor(0.42f, 0.3f, 0.2f);

	// Количество
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", ClampMax = "30", UIMin = "0", UIMax = "30",
		DisplayName = "Количество", ToolTip = "Сколько комнат этого типа ставить. Общий потолок для всех типов — MaxRooms."))
	int32 Count = 2;

	// 3. Условие расположения
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Где ставить",
		ToolTip = "Any — где угодно. Edge — у края поля. NearStart / FarFromStart — ближе к стартовой комнате или дальше от неё. NextToRoad / NextToRoom — маленькая пристройка у дороги или у другой комнаты."))
	ERoomCondition Condition = ERoomCondition::Any;
};

// Уже размещённая комната (для Blueprint и геометрии)
USTRUCT(BlueprintType)
struct FPlacedRoom
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 X = 0;
	UPROPERTY(BlueprintReadOnly) int32 Y = 0;
	UPROPERTY(BlueprintReadOnly) int32 W = 0;
	UPROPERTY(BlueprintReadOnly) int32 H = 0;
	UPROPERTY(BlueprintReadOnly) int32 CenterX = 0;
	UPROPERTY(BlueprintReadOnly) int32 CenterY = 0;
	UPROPERTY(BlueprintReadOnly) int32 Index = -1;
	UPROPERTY(BlueprintReadOnly) ERoomKind Kind = ERoomKind::Normal;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
	// Форма этого типа комнат: нужна BuildGeometry(), чтобы понять, какую комнату
	// скруглять дугой (Shape = Circle), а какие оставить прежними гранями клеток.
	UPROPERTY(BlueprintReadOnly) ERoomShape Shape = ERoomShape::Rect;
};

UCLASS()
class ADungeonGenerator : public AActor
{
	GENERATED_BODY()

public:
	ADungeonGenerator();

	// Кнопки в Details-панели. Все в одной категории "DungeonActions" —
	// кастомизация панели (DungeonGeneratorDetails) подтягивает её в самый верх,
	// порядок внутри задаёт DisplayPriority, подпись — DisplayName, подсказка — ToolTip.

	// Главная кнопка: убирает старое, генерирует сетку и сразу строит геометрию
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DungeonActions",
		meta = (DisplayName = "Сгенерировать подземелье", DisplayPriority = "1",
			ToolTip = "Убрать старое (меши, пол, маркеры), построить новую сетку комнат и дорог и сразу перестроить стены, крышу и пол."))
	void Generate();

	// Перестроить геометрию из текущей сетки (0 -> стены, 1 -> пол)
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DungeonActions",
		meta = (DisplayName = "Построить геометрию", DisplayPriority = "2",
			ToolTip = "Сетку не менять, заново построить стены, крышу и пол из текущего Grid."))
	void BuildGeometry();

	// Полностью удалить ВСЕ результаты прошлой генерации: секции меша стен, секцию
	// крыши, пол и маркеры-огни. Маркеры ищем не только в списке Markers, но и по
	// тегу DungeonMarker во всём уровне — поэтому чистятся и те, что остались от
	// прежних генераций после хот-релоада, PIE, загрузки уровня или удаления актора.
	// Generate() вызывает это сам; кнопка нужна, чтобы очистить без новой генерации.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DungeonActions",
		meta = (DisplayName = "Очистить старое", DisplayPriority = "3",
			ToolTip = "Удалить меши стен и крыши, пол и все маркеры прошлых генераций (включая забытые огни в уровне)."))
	void ClearGenerated();

	// Вернуть встроенный набор типов комнат (5 штук): Большой зал, Комната,
	// Кладовая, Крипта, Пост у дороги. Свои настройки при этом теряются, но
	// откатываются обычным Ctrl+Z.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DungeonActions",
		meta = (DisplayName = "Сбросить типы комнат", DisplayPriority = "4",
			ToolTip = "Заполнить список типов комнат встроенным набором. Откатывается Ctrl+Z."))
	void ResetRoomTemplates();

	UFUNCTION(BlueprintPure, Category = "Dungeon")
	uint8 GetGridValue(int32 X, int32 Y) const;

	// ---------- результаты (для Blueprint) ----------
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	TArray<uint8> Grid;          // 0 = камень, 1 = открытое пространство

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	TArray<int32> RoomMap;       // индекс комнаты в клетке (-1 = нет)

	// Дороги не ломают клетки комнат, поэтому клетка коридора внутри комнаты по
	// RoomMap всё ещё принадлежит комнате. Отдельная карта дорог нужна скруглению
	// круглых комнат: ни гладкая стена, ни крышка уголков не должны вставать
	// поперёк коридора — иначе подземелье окажется разрезано пополам.
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	TArray<uint8> RoadMask;      // 1 = клетка затронута коридором

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	TArray<FPlacedRoom> Rooms;

	// ---------- настройки поля ----------
	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "20", ClampMax = "200", UIMin = "20", UIMax = "200",
		DisplayName = "Ширина сетки (клеток)", ToolTip = "Сколько клеток по X. Одна клетка = CellSize."))
	int32 GridW = 90;

	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "20", ClampMax = "200", UIMin = "20", UIMax = "200",
		DisplayName = "Высота сетки (клеток)", ToolTip = "Сколько клеток по Y. Одна клетка = CellSize."))
	int32 GridH = 64;

	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "10", DisplayName = "Размер клетки",
		ToolTip = "Размер клетки в сантиметрах: 100 = 1 метр."))
	float CellSize = 100.f;      // размер клетки в юнитах

	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "100", DisplayName = "Высота стен",
		ToolTip = "Высота стен и крыши в сантиметрах: 200 = 2 метра, 400 = 4 метра."))
	float WallHeight = 400.f;

	// Раньше этот параметр выбирал, рисовать ли сплошную полосу стен вокруг комнат.
	// Оболочка строится по граням: грань выходит только там, где соседняя
	// клетка открыта, поэтому WallBand больше не меняет результат (0 и 1 дают
	// одинаковую сетку). Оставлен, чтобы не ломать уже сохранённые настройки.
	UPROPERTY(EditAnywhere, Category = "Field", meta = (ClampMin = "0", ClampMax = "6",
		DisplayName = "Старая полоса стен (не работает)", ToolTip = "Ничего не меняет: стены строятся по граням клеток."))
	int32 WallBand = 1;

	UPROPERTY(EditAnywhere, Category = "Field", meta = (DisplayName = "Центрировать на акторе",
		ToolTip = "true — подземелья вокруг актора; false — актор в углу подземелья."))
	bool bCenterOnActor = true;

	// ---------- дороги ----------
	// Доп. рёбра (петли): сверх MST добавляются самые короткие коридоры,
	// создавая запасные маршруты. Между парой комнат — не больше одной дороги.
	UPROPERTY(EditAnywhere, Category = "Roads", meta = (ClampMin = "0", ClampMax = "10", UIMin = "0", UIMax = "10",
		DisplayName = "Запасные дороги (петли)", ToolTip = "Сколько дополнительных коридоров сверх минимального дерева дорог."))
	int32 ExtraEdges = 2;

	// Настраиваемые пороги ширины дорог:
	// размер меньшей комнаты <= Thr1 -> 1, <= Thr2 -> 2, иначе 3
	UPROPERTY(EditAnywhere, Category = "Roads", meta = (ClampMin = "2", ClampMax = "15", UIMin = "2", UIMax = "15",
		DisplayName = "Узкие дороги до размера", ToolTip = "Комната меньше или равна этому числу клеток -> дорога шириной 1 клетка."))
	int32 Thr1 = 5;

	UPROPERTY(EditAnywhere, Category = "Roads", meta = (ClampMin = "3", ClampMax = "30", UIMin = "3", UIMax = "30",
		DisplayName = "Средние дороги до размера", ToolTip = "Комната меньше или равна этому числу клеток -> дорога шириной 2 клетки, иначе 3."))
	int32 Thr2 = 10;

	// Зазор между коридором и стеной комнаты, в клетках.
	// 1 -> между дорогой и комнатой всегда остаётся хотя бы одна свободная клетка:
	//      из неё строится стена комнаты, а в комнату ведёт перпендикулярный вход (дверь).
	//      Стена комнаты больше не ломается там, где мимо проходит коридор.
	// 0 -> прежнее поведение: коридор может идти вплотную к стене комнаты.
	UPROPERTY(EditAnywhere, Category = "Roads", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3",
		DisplayName = "Отступ дорог от комнат (клеток)",
		ToolTip = "Сколько свободных клеток держать между коридором и стеной комнаты. 1 = стены комнат целы, 0 = дороги могут идти вплотную."))
	int32 RoadRoomGap = 1;

	// Насколько далеко генератор сдвигает основную ногу коридора, ища положение
	// без касания чужих стен. 0 = только прежние два излома (быстрее, но дороги могут
	// прижиматься к комнатам).
	UPROPERTY(EditAnywhere, Category = "Roads", meta = (ClampMin = "0", ClampMax = "32", UIMin = "0", UIMax = "32",
		DisplayName = "Поиск полосы дороги (клеток)",
		ToolTip = "Радиус сдвига основной ноги коридора при подборе. Больше = чище проходы, но подбор дольше."))
	int32 RoadLaneSearch = 8;

	// ---------- комнаты ----------
	UPROPERTY(EditAnywhere, Category = "RoomTypes", meta = (DisplayName = "Тип: стартовая комната",
		ToolTip = "Комната старта: её размер и форму тоже можно менять."))
	FRoomTemplate StartRoom;

	UPROPERTY(EditAnywhere, Category = "RoomTypes", meta = (DisplayName = "Тип: финальная комната",
		ToolTip = "Комната финиша: ставится максимально далеко от старта."))
	FRoomTemplate EndRoom;

	// Обычные комнаты. Если список пуст, генератор берёт встроенный набор
	// шаблонов (MakeDefaultTemplates в .cpp): без него подземелье состоит
	// ровно из комнаты старта и комнаты финиша.
	UPROPERTY(EditAnywhere, Category = "RoomTypes", meta = (DisplayName = "Типы обычных комнат",
		ShowOnlyInnerProperties,
		ToolTip = "Каждый тип: минимальный и максимальный размер, форма, количество и где ставить. +/- в строке типа."))
	TArray<FRoomTemplate> Templates;

	// Потолок на всё подземелье: старт + финиш + Templates + пост-комнаты.
	// Одно место всегда резервируется под комнату финиша.
	UPROPERTY(EditAnywhere, Category = "Rooms", meta = (ClampMin = "2", ClampMax = "40", UIMin = "2", UIMax = "40",
		DisplayName = "Максимум комнат (всего)", ToolTip = "Потолок на все типы сразу: старт + финиш + Count всех типов + пост-комнаты."))
	int32 MaxRooms = 14;

	// 0 = случайный каждый раз
	UPROPERTY(EditAnywhere, Category = "Rooms", meta = (DisplayName = "Ключ генерации (0 = случайный)",
		ToolTip = "Одинаковый ключ -> одинаковое подземелье. 0 -> каждый раз новое."))
	int32 Seed = 0;

	// ---------- геометрия ----------
	UPROPERTY(EditAnywhere, Category = "Geometry")
	bool bBuildGeometry = true;

	// Крыша сверху клеток 0 (пустоты). Комнаты и дороги (1) крыша не затрагивает.
	UPROPERTY(EditAnywhere, Category = "Geometry")
	bool bBuildRoof = true;

	// Пол над клетками 1 (комнаты и дороги). Собирается прямоугольниками по тем же
	// правилам, что и крыша, — вместо одной растянутой плоскости Plane.
	UPROPERTY(EditAnywhere, Category = "Geometry", meta = (DisplayName = "Строить пол",
		ToolTip = "Пол из прямоугольников над клетками 1. Сними галочку, если пол в уровне сделан вручную."))
	bool bBuildFloor = true;

	// Размер ОДНОЙ ПЛИТКИ текстуры в сантиметрах. UV всех граней считаются от мировых
	// размеров, поэтому текстура нигде не растягивается: 100 = плитка на 1 м,
	// 250 = плитка на 2.5 м. 0 = брать CellSize (одна плитка на одну клетку сетки).
	// В материале при этом держи Tiling = 1: масштаб тайлинга задаёт этот параметр.
	UPROPERTY(EditAnywhere, Category = "Geometry", meta = (ClampMin = "0", ClampMax = "2000", UIMin = "0", UIMax = "1000",
		DisplayName = "Размер плитки текстуры (см, 0 = размер клетки)",
		ToolTip = "Сколько сантиметров занимает одна плитка текстуры на стенах, крыше и полу. 0 = CellSize. В материале Tiling = 1."))
	float TextureTileSize = 0.f;

	// ---------------------------------------------------------------------
	//  Скругление комнат формы «Круглая» (только её; прямоугольник, Г и крест
	//  остаются прежними гранями клеток).
	//
	//  Ничего нового к стене не добавляется: те же грани, что генератор и так
	//  ставит на границе камня и открытой клетки, у круглой комнаты ставятся
	//  чуть назад — их концы подтягиваются на эллипс, вписанный в прямоугольник
	//  комнаты (тот же эллипс, по которому комната вырезана). Лесенка из граней
	//  превращается в многоугольник, вписанный в ту же клетчатую область, как в
	//  макете. Пол и крыша не меняются: уголки за переставленной гранью
	//  остаются за стеной, пройти туда нельзя.
	// ---------------------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Geometry", meta = (DisplayName = "Скруглять круглые комнаты",
		ToolTip = "Грани стен комнат с формой «Круглая» переставляются на эллипс, вписанный в прямоугольник комнаты: лесенка превращается в многоугольник. Сними галочку, чтобы вернуть прежние грани."))
	bool bRoundCircleWalls = true;

	UPROPERTY(EditAnywhere, Category = "Geometry")
	bool bSpawnMarkers = true;   // жёлтый свет на старте, красный на финише

protected:
	// Стены: ОДНА секция процедурного меша — только те грани камня, что соседствуют с открытыми клетками.
	// Полигоны смотрят внутрь подземелья (в сторону открытой клетки).
	UPROPERTY(VisibleAnywhere, Category = "Dungeon")
	TObjectPtr<UProceduralMeshComponent> WallsMesh;

	// Крыша: ОДНА секция процедурного меша — крышки клеток 0 на высоте WallHeight, normals вверх.
	// Соседние клетки крышки объединены в прямоугольники по обеим осям.
	UPROPERTY(VisibleAnywhere, Category = "Dungeon")
	TObjectPtr<UProceduralMeshComponent> RoofMesh;

	// Пол: ОДНА секция процедурного меша — прямоугольники над клетками 1 на высоте 0,
	// normals вверх, тайлинг такой же, как у стен и крыши.
	UPROPERTY(VisibleAnywhere, Category = "Dungeon")
	TObjectPtr<UProceduralMeshComponent> FloorMesh;

	// Старая плоскость пола /Engine/BasicShapes/Plane: пол теперь собирается в FloorMesh,
	// поэтому FloorPlane всегда спрятана. Оставлена, чтобы не ломать уже сохранённые акторы
	// и Blueprint: её материал переносится на FloorMesh, а сам компонент можно удалить из BP.
	UPROPERTY(VisibleAnywhere, Category = "Dungeon", meta = (DisplayName = "Старый плоский пол (спрятан)"))
	TObjectPtr<UStaticMeshComponent> FloorPlane;

	// Материалы поверх всего остального: если здесь что-то назначено, генератор сам
	// поставит материал в 0-й слот нужного меша при BuildGeometry(). Если оставить
	// пустым — работает обычное ручное назначение материала на компоненте.
	UPROPERTY(EditAnywhere, Category = "Geometry", meta = (DisplayName = "Материал стен"))
	TObjectPtr<UMaterialInterface> WallsMaterial;

	UPROPERTY(EditAnywhere, Category = "Geometry", meta = (DisplayName = "Материал крыши"))
	TObjectPtr<UMaterialInterface> RoofMaterial;

	UPROPERTY(EditAnywhere, Category = "Geometry", meta = (DisplayName = "Материал пола"))
	TObjectPtr<UMaterialInterface> FloorMaterial;

private:
	// временная структура для размещения
	struct FRoomSpot
	{
		int32 X = 0, Y = 0, W = 0, H = 0, CX = 0, CY = 0;
		FIntPoint Anchor = FIntPoint::ZeroValue;
		int32 AnchorRoom = -1;
	};

	FRandomStream Stream;

	bool InBounds(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < GridW && Y < GridH; }
	int32 Idx(int32 X, int32 Y) const { return Y * GridW + X; }

	// ---------------- скругление комнат формы «Круглая» ----------------
	// Одна грань стены круглой комнаты. Это НЕ новая грань: это та же грань,
	// что обход стен ставит на границе клетки камня и открытой клетки комнаты,
	// только её концы подтянуты на эллипс комнаты. Координаты — в КЛЕТКАХ
	// сетки (не в сантиметрах): так проще сравнивать грань с клетками.
	struct FRoundFace
	{
		FVector2D A0, B0;      // концы грани, как их даёт сетка
		FVector2D A, B;        // куда эти концы переставлены (на эллипсе)
		FIntPoint RockCell;    // клетка камня, чью грань переставляем
		int32     DirIndex = 0;// направление грани камня (индекс в WallDirs)
		bool      bArc = true; // false = грань осталась на прежнем месте (у входа/коридора)
		float     MidAngle = 0.f; // угол середины грани: по нему идёт обход по кругу
	};

	struct FRoundRoom
	{
		int32 RoomIndex = -1;
		FVector2D Center;          // центр эллипса в клетках сетки
		FVector2D Semi;            // полуоси эллипса: половина прямоугольника комнаты
		TArray<FRoundFace> Faces;  // грани в порядке обхода по кругу
	};

	bool RoomCellOwned(int32 X, int32 Y, int32 RoomIndex) const;
	bool RoundChordCrossesPassage(float AX, float AY, float BX, float BY, int32 RoomIndex) const;
	// Грани круглых комнат + маска GridW*GridH*4: 1 = прежнюю грань этой клетки
	// камня на этом направлении ставить уже не нужно, её заменила переставленная.
	void BuildRoundRooms(TArray<FRoundRoom>& OutRooms, TArray<uint8>& OutSkip) const;

	int32 Rnd(int32 Min, int32 Max) { return Min + Stream.RandHelper(FMath::Max(1, Max - Min + 1)); }
	void ShufflePoints(TArray<FIntPoint>& Arr);

	bool IsPostCondition(ERoomCondition C) const
	{
		return C == ERoomCondition::NextToRoad || C == ERoomCondition::NextToRoom;
	}

	TArray<FIntPoint> ShapeCells(int32 W, int32 H, ERoomShape Shape) const;
	bool ConditionOk(ERoomCondition Cond, int32 CX, int32 CY, const FRoomSpot* Start) const;
	bool RectsOverlap(const FRoomSpot& A, const FRoomSpot& B, int32 Margin) const;
	bool FindPosition(const FRoomTemplate& Tpl, const TArray<FRoomSpot>& Placed,
	                  ERoomCondition Cond, const FRoomSpot* Start, FRoomSpot& Out);
	void CarveRoom(const FRoomSpot& Spot, const FRoomTemplate& Tpl, ERoomKind Kind);
	void BuildMST(TArray<TPair<int32, int32>>& OutEdges) const;
	void AddExtraEdges(const TArray<TPair<int32, int32>>& MST, int32 Count,
	                   TArray<TPair<int32, int32>>& Out) const;
	int32 RoadWidthFor(int32 A, int32 B) const;
	// Нога коридора: bAlongX = true -> идёт вдоль X (Fixed = строка Y),
	// иначе идёт вдоль Y (Fixed = столбец X). S/E — начало и конец вдоль ноги.
	struct FRoadSeg
	{
		bool  bAlongX = true;
		int32 S = 0;
		int32 E = 0;
		int32 Fixed = 0;
	};

	// Можно ли прокладывать коридор в клетке (X, Y) с подходом по оси bAlongX:
	// проверяем, что вокруг нет чужой комнаты и что мы не едем вдоль стены своей.
	bool RoadCellAllowed(int32 X, int32 Y, int32 IdxA, int32 IdxB, bool bAlongX) const;

	// Собирает три ноги варианта Opt: вход в комнату A, основную полосу Lane, вход в комнату B.
	void BuildRoadSegs(int32 Opt, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 Lane, TArray<FRoadSeg>& Out) const;

	// Штраф конфигурации: 0 = коридор нигде не прижимается к стенам комнат.
	// Mask/Touched — рабочие массивы (Mask по клетке: 1 = горизонтальная нога, 2 = вертикальная).
	int32 ScoreRoadConfig(const TArray<FRoadSeg>& Segs, int32 Width, int32 IdxA, int32 IdxB,
	                      TArray<uint8>& Mask, TArray<int32>& Touched) const;

	// IdxA / IdxB — индексы соединяемых комнат: только им разрешено касаться своих
	// стен, потому что дверь в комнату обязана быть в стеновой клетке.
	void CarveRoad(int32 X1, int32 Y1, int32 X2, int32 Y2, int32 Width, int32 IdxA, int32 IdxB);
	bool FindPostPosition(const FRoomTemplate& Tpl, ERoomCondition Mode, FRoomSpot& Out);
	void AddPostRoom(const FRoomTemplate& Tpl);
	void GenerateInternal();

	// Удаляет маркеры старта/финиша: и из списка Markers, и по тегу во всём уровне
	void ClearMarkers();

	UPROPERTY()
	TArray<TObjectPtr<APointLight>> Markers;
};