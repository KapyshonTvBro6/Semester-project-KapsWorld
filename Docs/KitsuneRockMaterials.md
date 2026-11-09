# Генератор скальных материалов «Кицунэ Юки» в UE5

Веб-генератор ([Reference/kitsune_rock_generator.html](Reference/kitsune_rock_generator.html)) перенесён в движок: карты
BaseColor / Normal / Roughness / AO / Height считаются на CPU и превращаются в
обычные ассеты Unreal — шесть ~UTexture2D~, один master-материал ~M_KitsuneRock~
и ~MaterialInstanceConstant~ на каждую настройку.

**Это отдельный инструмент.** Он ничего не знает про ~ADungeonGenerator~:
у генератора подземелий нет ни одного параметра камня и ни одной кнопки
генерации материалов. Готовый материал назначается стенам вручную — как любой
другой материал из Content Browser.

---

## 1. Файлы

| Файл | Что внутри |
|---|---|
| [KitsuneRockParams.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockParams.h#L1) | ~FKitsuneRockParams~ — все слайдеры веб-генератора, и ~EKitsuneRockPreset~ — восемь пресетов |
| [KitsuneRockParams.cpp](Source/SemesterKapsWorld/Kitsune/KitsuneRockParams.cpp#L1) | Таблица пресетов: значения слайдеров и цвета палитры один в один как в HTML |
| [KitsuneRockField.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockField.h#L1) | ~FKitsuneRockMaps~ — результат: шесть карт в байтах ~FColor~ |
| [KitsuneRockField.cpp](Source/SemesterKapsWorld/Kitsune/KitsuneRockField.cpp#L1) | Расчёт: периодический value-noise, FBM, трещины, слистость, кирпичная кладка, жилы, AO, нормаль, шероховатость. Считается через ~ParallelFor~ |
| [KitsuneRockAssets.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockAssets.h#L1) | ~UKitsuneRockMaterialLibrary~ (Blueprint-функции) и единый список имён параметров материала |
| [KitsuneRockAssets.cpp](Source/SemesterKapsWorld/Kitsune/KitsuneRockAssets.cpp#L1) | Создание и обновление текстур, сборка графа ~M_KitsuneRock~, создание ~MI_...~, сохранение ~.uasset~ |
| [KitsuneRockGeneratorComponent.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockGeneratorComponent.h#L1) | Компонент с кнопками в Details-панели |
| [KitsuneRockCommandlet.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockCommandlet.h#L1) | Пакетный запуск без открытого редактора |
| [KitsuneRockConsoleCommands.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockConsoleCommands.h#L1) | Команды консоли ~Kitsune.Rock.*~ |

---

## 2. Как пользоваться

### А. Компонентом в редакторе (проще всего)

1. Возьмите любой актор (хоть пустой ~Actor~) и добавьте компонент
   **Генератор материалов камня** (~UKitsuneRockGeneratorComponent~).
2. Выберите **Пресет** — настройки заполнятся сами (как выбор «Материал» в веб-версии).
3. Нажмите **«Сгенерировать материал»**.
4. В Content Browser появится папка ~Content/Kitsune/Rock~:
   ~M_KitsuneRock~, ~MI_granite~, ~T_granite_BaseColor~, ~T_granite_Normal~,
   ~T_granite_Roughness~, ~T_granite_AO~, ~T_granite_Height~, ~T_granite_Emissive~
   (имя пресета в нижнем регистре — как ключ в веб-версии).
5. Кнопка **«Назначить материал на меши»** повесит материал на все mesh-компоненты
   актора (и ~UStaticMeshComponent~, и ~UProceduralMeshComponent~).
6. Кнопка **«Случайный сид»** — это кнопка ~🎲~ из веб-версии.

Пресеты можно менять сколько угодно раз: текстуры перезаписываются, инстанс
пересобирается, а material-граф создаётся один раз.

### Б. Командой в консоли редактора (клавиша ~)

~~~
Kitsune.Rock.Master                          # создать master-материал
Kitsune.Rock.Generate Wall01                 # Granite, 512, сид 1337
Kitsune.Rock.Generate Wall01 lavaVeins 1024 42
Kitsune.Rock.GenerateAll                     # все восемь пресетов
Kitsune.Rock.GenerateAll 256                 # все восемь, карты 256
Kitsune.Rock.SyncMaster                      # починить master: карты по умолчанию и типы сэмплеров
Kitsune.Rock.SyncMaster /Game/Kitsune/Rock nosave   # то же, но без сохранения
~~~

### В. Пакетно, без открытого редактора

~~~bash
~/UnrealEngine-5.8.3/Engine/Binaries/Linux/UnrealEditor-Cmd \
  "/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject" \
  -run=KitsuneRock -all -size=512
~~~

Один комплект под свои параметры:

~~~bash
... -run=KitsuneRock -name=Wall01 -preset=mossyCave -size=1024 -seed=7 -relief=0.8 -moss=0.35
~~~

Флаги: ~-all~, ~-name=~, ~-preset=~, ~-size=~, ~-seed=~, ~-folder=~, ~-nosave~,
~-dump=<папка>~, ~-verify~, ~-syncmaster~, ~-antitiling~, ~-params~,
~-scale= ~-octaves= ~-relief= ~-cracks= ~-strata= ~-moisture= ~-moss= ~-roughness=
~-brick= ~-veins= ~-normalstrength= ~-ao= ~-inverty=1~. Полная справка печатается
в начале работы commandlet.

### Г. Из Blueprint

~UKitsuneRockMaterialLibrary~ — Blueprint-библиотека (все функции ~DevelopmentOnly~):

- **Создать master-материал камня** → ~UMaterial~
- **Сгенерировать материал камня** ~(AssetName, Params, AssetFolder, bSaveAssets)~
  → ~FKitsuneRockMaterialResult~: ~bSuccess~, ~Message~, ~MaterialInstance~, ~Textures~
- **Сгенерировать все пресеты камня** ~(Size, bSaveAssets)~ → массив результатов
- **Добавить анти-тайлинг в мастер камня** ~(AssetFolder, bSaveAssets)~ → число добавленных
  узлов (0 = уже встроен). ~DevelopmentOnly~.
- **Случайный тайлинг камня для инстанса** ~(Instance, Seed)~ — работает и в собранной
  игре: сместить UV и искажение UV у динамического инстанса (см. 5.1).

---

## 3. Пресеты

Имя ассета — английское (~MI_Granite~), подпись в интерфейсе — русская, как в веб-версии.
Имя пресета для команд и ~FindKitsuneRockPreset~ — английское, регистр не важен;
русскую подпись тоже принимает ~GetKitsuneRockPresetLabel~.

| Пресет (подпись) | Ключ в коде / имя инстанса | Чем отличается |
|---|---|---|
| Гранит | ~granite~ / ~MI_granite~ | светлый зернистый камень, базовый пресет |
| Базальт | ~basalt~ / ~MI_basalt~ | тёмный, рельеф 0.78, трещины 0.72 |
| Пещерный песчаник | ~sandstone~ / ~MI_sandstone~ | тёплые слои (strata 0.72), шероховатый |
| Мшистая пещера | ~mossyCave~ / ~MI_mossyCave~ | мох 0.85, сырость 0.55 |
| Мокрая пещера | ~wetCave~ / ~MI_wetCave~ | блеск (roughness 0.35), сырость 0.95 |
| Кирпичный данж | ~dungeonBricks~ / ~MI_dungeonBricks~ | кирпичная кладка 0.95, раствор в швах |
| Лавовые жилы | ~lavaVeins~ / ~MI_lavaVeins~ | светящиеся жилы 0.90 |
| Кристальная пещера | ~crystalCave~ / ~MI_crystalCave~ | фиолетовая палитра, жилы 0.75 |

---

## 4. Настройки ~FKitsuneRockParams~ и соответствие веб-слайдерам

| Параметр в UE | Слайдер в браузере | Диапазон | По умолчанию |
|---|---|---|---|
| ~Size~ | Размер карт | 128…2048 (степень двойки) | 512 |
| ~Seed~ | Сид + ~🎲~ | 0…999999 | 1337 |
| ~Scale~ | Масштаб шума | 2…16 | 6.5 |
| ~Octaves~ | Детальность | 1…8 | 7 |
| ~Relief~ | Рельеф | 0…1 | 0.62 |
| ~Cracks~ | Трещины | 0…1 | 0.28 |
| ~Strata~ | Слоистость | 0…1 | 0.12 |
| ~Moisture~ | Сырость | 0…1 | 0.08 |
| ~Moss~ | Мох | 0…1 | 0.05 |
| ~Roughness~ | Шероховатость | 0…1 | 0.72 |
| ~Brick~ | Кирпичи | 0…1 | 0 (0.95 у кирпичного данжа) |
| ~Veins~ | Жилы | 0…1 | 0 (0.90 лава, 0.75 кристаллы) |
| ~NormalStrength~ | Сила нормали | 0…8 | 2.5 |
| ~AOStrength~ | Сила AO | 0…1 | 0.55 |
| ~bInvertNormalY~ | Инвертировать Y | чекбокс | false |
| ~ColorDark / ColorMid / ColorLight / ColorBright / MossColor / VeinColor~ | Палитра | цвета | из пресета |

~Size~, ~Scale~, ~Octaves~ и ~Brick~ описывают сами карты — чтобы изменить их,
нужна перегенерация (кнопка или команда). ~NormalStrength~, ~AOStrength~, цвета и
шероховатость дублируются параметрами материала: их правят прямо в ~MI_*~,
не перегенерируя карты.

---

## 5. Master-материал ~M_KitsuneRock~

Граф строится кодом один раз:

~~~
TexCoord -> Multiply(Tiling) -> Add(UVOffset + Warp U/V) -> BumpOffset(Height = T_Height, HeightRatio = HeightStrength)
                                     ^    |
  WorldPosition -> Multiply(WarpScale) -> Noise(GradientALU) x2
                                          |
                                     Multiply(DetailScale) -> T_BaseColor, T_Normal (детальный слой)

  T_BaseColor  -> Lerp(base -> detail, Alpha = DetailMix) -> Multiply(Tint) --> BaseColor
  T_Normal     -> Custom(Сила нормали: NormalIn, DetailIn, Mix, Strength) -> Normal
  T_Roughness  -> Multiply(RoughnessAdjust) -> Clamp -------------------> Roughness
  T_AO         -> Lerp(1 -> AO, Alpha = AOStrength) --------------------> AmbientOcclusion
  T_Emissive   -> Multiply(EmissiveStrength) -> Multiply(VeinColor) ----> EmissiveColor
  Scalar(Specular) ----------------------------------------------------> Specular
  T_Height     -> BumpOffset (параллакс)
~~~

В веб-версии карт пять: жилы только подкрашивают BaseColor, эмиссивной карты там
нет. В UE добавлена шестая карта ~T_*_Emissive~ — маска жил; она питает
~EmissiveColor~, поэтому ~lavaVeins~ и ~crystalCave~ светятся. Свечение отключается
~EmissiveStrength = 0~ прямо в ~MI_*~, карты при этом не перегенерируются.

Шейдинг ~DefaultLit~, двусторонность выключена. Specular у камня 0.3 вместо
стандартных 0.05 — на матовых породах это заметно.

Параметры инстанса:

| Параметр | Тип | В инстансе | Назначение |
|---|---|---|---|
| ~T_BaseColor ~T_Normal ~T_Roughness ~T_AO ~T_Height ~T_Emissive~ | текстуры | карты пресета | шесть карт генератора |
| ~Tiling~ | скаляр | 1.0 | сколько раз карта повторяется; для стены 4×4 м обычно 0.25…1 |
| ~NormalStrength~ | скаляр | 1.0 | дополнительно усилить нормаль (поверх силы из генератора) |
| ~RoughnessAdjust~ | скаляр | 1.0 | общий множитель шероховатости |
| ~AOStrength~ | скаляр | 1.0 | 0 = AO выключен |
| ~HeightStrength~ | скаляр | 0.02 | параллакс через BumpOffset, 0 = выключен |
| ~Specular~ | скаляр | 0.3 | блик камня |
| ~EmissiveStrength~ | скаляр | 2.5 если есть жилы, иначе 0 | яркость свечения |
| ~UVOffsetX~ ~UVOffsetY~ | скаляр | 0…1 у ~MI_*~ (у каждого своё) | сдвиг раскладки в плитках: разные инстансы берут разные места карты |
| ~WarpAmount~ | скаляр | 0.02 | сила искажения UV в плитках; 0 = выключено |
| ~WarpScale~ | скаляр | 0.0015 | частота волны в 1/см: 0.0015 ≈ волна на 6.7 м |
| ~DetailScale~ | скаляр | 2.0 | во сколько раз мельче детальный слой; держите целым |
| ~DetailMix~ | скаляр | 0.15 | доля детального слоя; 0 = выключен |
| ~Tint~ | вектор | белый | общая подцветка (тёплый оттенок пещеры и т.п.) |
| ~VeinColor~ | вектор | цвет жил пресета | цвет свечения жил |

Компрессия: BaseColor — ~TC_Default~, Normal — ~TC_Normalmap~ (без sRGB),
Roughness / AO / Height / Emissive — ~TC_Grayscale~ (без sRGB). Группы ~World~ и
~WorldNormalMap~, мипы ~SimpleAverage~. Обёртка по UV — ~TA_Wrap~: в 5.8 это
поведение по умолчанию, и карты генератора периодические по построению, шва нет.

---

### 5.1 Анти-тайлинг: почему повтор бросается в глаза и что встроено в мастер

Карты генератора периодичны по построению — это плюс: шва между плитками нет.
Но период равен размеру карты, а геометрия подземелья считает UV в сантиметрах
(~u = координата / TextureTileSize~), поэтому плитка ложится ровно через
~TextureTileSize~ на всех стенах, полах и потолках, и ряд одинаковых стен
получает дословно один и тот же рисунок. При ~TextureTileSize = 0~ плитка равна
~CellSize~ = 100 см, то есть одна карта 512 повторяется каждый метр — это самый
заметный случай. Кирпичный пресет спасает только тем, что кладка читается как
кладка: повторяющийся узор там ожидаем, а у камня глаз ищет совпадения.

Мастер ломает это тремя способами. Все три — параметры: крутятся прямо в ~MI_*~
или в динамическом инстансе, карты не пересобираются.

1. **UVOffsetX / UVOffsetY** — сдвиг всей раскладки в плитках текстуры. Инстансы
   с разными смещениями смотрят в разные места одной карты, и повтор перестаёт
   появляться на уровне «эта стена против той». Рабочий диапазон 0…0.99 (при 1.0
   обёртка ~TA_Wrap~ вернула бы карту ровно на своё место).
2. **WarpAmount / WarpScale** — волновое искажение UV по мировой позиции: два узла
   ~Noise~ («Gradient — Computational», ~GradientALU~: вычислительный Perlin-шум
   без текстур и без собственной периодичности; своего HLSL тут нет). Период волны
   не кратен размеру плитки, поэтому швы между плитками смазываются и глаз не
   находит границу повтора. ~WarpScale~ — частота в 1/см: 0.0015 ≈ волна на 6,7
   метра; ~WarpAmount~ — сила сдвига в долях плитки.
3. **DetailScale / DetailMix** — второй слой тех же карт ~T_BaseColor~ и ~T_Normal~,
   взятый в ~DetailScale~ раз мельче и подмешанный через ~DetailMix~ (~lerp~ для
   BaseColor, а для нормалей — тот же ~lerp~ плюс ~normalize~ внутри уже
   имеющегося Custom-узла). Добавляет частоту, которой у одной карты нет: деталь
   на стене становится разной в разных её местах.

**Меры у приёмов разные.** ~UVOffset~ — чистый перенос: местную частоту выборки
он не меняет и артефактов создать не способен. ~Warp~ и ~Detail~ меняют её, и у
них есть предел. Волна сдвигает UV на ~WarpAmount~ плиток за полпериода длиной
~1 / WarpScale~; местный градиент сдвига сравним с частотой плитки
~1 / TextureTileSize~, когда

```
WarpAmount × WarpScale × TextureTileSize  ≳  0.1
```

За этим пределом выборка идёт по сжимаемой и растягиваемой раскладке, ряды
пикселей складываются в тонкие штрихи вдоль осей мира, и это выглядит как дефект
текстуры, хотя карты целы. Именно так вышло с прежними значениями по умолчанию:
0.10 × 0.004 при плитке 300 см давало 0.12, а дробный ~DetailScale~ 3.17 добавлял
вторую решётку той же карты, несоизмеримую с придонной, — она билась с нею в тот
же муар. Нынешние значения по умолчанию — 0.02 / 0.0015 / 2.0 / 0.15, произведение
0.009, запас в одиннадцать раз. Целый ~DetailScale~ важен: при нём решётка
детального слоя вложена в придонную, второго муара нет вовсе, а расхождение
картинки даёт сама волна.

Сменён и тип шума: ~ValueALU~ движок подписывает как «Low quality, but pure
computation» и «formerly mis-labeled as Gradient noise» — это шум значений, у
которого производная обнуляется в узлах решётки, из-за чего искажение идёт
ступенями вдоль осей. ~GradientALU~ («Gradient — Computational», Perlin Modified
Noise) для искажения UV подходит напрямую. Оба варианта вычислительные, без
текстур, ~Levels~ 1 и без турбулентности: лишние октавы только подняли бы
градиент.

Каскад встраивается в мастер автоматически при любой генерации. Отдельно его
ставит ~Kitsune.Rock.AntiTiling~ в консоли редактора или
~-run=KitsuneRock -antitiling~. Функция идемпотентна и имеет два режима: нет
каскада — вставляет узлы; каскад есть — правит тип шума и те значения по
умолчанию, которые ещё равны прежним (изменённые вручную значения не трогает).
Ни один узел при этом не добавляется повторно. Заодно ~-antitiling~ прописывает
четыре значения во все ~MI_*~ этого каталога, кроме кирпичных: у инстанса с
«brick» в имени остаётся ровно то, что в нём уже стоит. Кирпичные пресеты
генератор и вовсе создаёт с ~WarpAmount~ 0 и ~DetailMix~ 0 — кладке искажение
только вредит, оно рвёт стыковку швов. ~UVOffsetX~ / ~UVOffsetY~ ни у одного
инстанса не меняются: сдвиг безопасен. ~-verify~ требует ~Noise 2/2~ и
~WorldPosition 1/1~ — если каскада нет, проверка это видит.

Отключается всё обратно параметрами, без правки графа:

| Хочу | Значение |
|---|---|
| Выключить искажение UV | ~WarpAmount~ 0 |
| Выключить детальный слой | ~DetailMix~ 0 |
| Вернуть строгую периодичность (сверка карт с веб-версией) | ~UVOffsetX~ 0, ~UVOffsetY~ 0, ~WarpAmount~ 0, ~DetailMix~ 0 |
| Волна крупнее / мельче | ~WarpScale~ 0.001 (≈10 м) … 0.0025 (≈4 м) |
| Детальный слой сильнее | ~DetailMix~ 0.2…0.3 при целом ~DetailScale~ 2 |
| Поднять волну сильнее | ~WarpAmount~ 0.03 при ~WarpScale~ 0.0015: с ~TextureTileSize~ 300 произведение даёт 0.014 — это примерно предел; выше начинают появляться штрихи |
| По стене пошли тонкие штрихи | ~WarpAmount~ 0.01, затем ~DetailMix~ 0: штрихи пропали на ~DetailMix~ 0 — виноват детальный слой; остались — уменьшайте ~WarpScale~ |

Разные инстансы одного пресета. При генерации каждому ~MI_*~ ставится смещение
из хэша его имени, поэтому ~MI_granite~ и ~MI_Granite2~ уже лежат в разных
местах карты. Для комнат внутри уровня удобнее динамический инстанс:
~Make Instance Dynamic~ от ~MI_*~ и Blueprint-функция **Случайный тайлинг камня
для инстанса** ~RandomizeRockInstanceParameters(Instance, Seed)~ — она ставит
~UVOffsetX/Y~, ~WarpAmount~, ~WarpScale~, ~DetailMix~, ~NormalStrength~ и лёгкий
сдвиг ~Tint~ из ~FRandomStream(Seed)~ и работает в собранной игре (в отличие от
остальной библиотеки, эта функция вне ~WITH_EDITOR~). Разный сид на комнату —
разный рисунок.

Искажение привязано к мировой позиции, поэтому на неподвижных стенах и полах оно
стабильно. На движущемся объекте узор поплыл бы по поверхности — для движущихся
камней лучше оставить ~WarpAmount~ 0 и пользоваться только ~UVOffsetX/Y~.

Ещё один рычаг — на стороне генератора подземелья, и генератор камня его не
касается (я его не менял): ~TextureTileSize~ в ~ADungeonGenerator~. При 0 плитка
равна ~CellSize~ = 100 см: одна карта 512 на метр. 200…300 даёт плитку на 2…3
метра — повторов на той же стене в 4…9 раз меньше. Это основной приём,
анти-тайлинг его дополняет; ~-size=1024~ даёт тот же эффект, не трогая
~TextureTileSize~. ~Tiling~ в материале при этом остаётся 1: масштаб плитки
задаёт геометрия, а не материал.

---

## 6. Как назначить материал стенам данжа

Оба пути не трогают генератор подземелий:

1. **Вручную.** Выделить актор стены, Details → Material Elements → слот 0 →
   ~MI_Granite~ (или другой ~MI_*~).
2. **Своим кодом.** ~UKitsuneRockMaterialLibrary::GenerateRockMaterial(...)~ →
   ~MaterialInstance~ → ~SetMaterial(0, MaterialInstance)~ на компоненте стены.
   Это ваш код; ~ADungeonGenerator~ остаётся нетронутым.

---

## 7. Правки и новые материалы

Править можно на трёх уровнях — они разные по цене, и перепутать их легко:

| Что меняем | Где правится | Нужна ли перегенерация карт |
|---|---|---|
| ~Tiling~, ~Tint~, ~NormalStrength~, ~RoughnessAdjust~, ~AOStrength~, ~HeightStrength~, ~Specular~, ~EmissiveStrength~, ~VeinColor~, ~UVOffsetX/Y~, ~WarpAmount~, ~WarpScale~, ~DetailScale~, ~DetailMix~ и сами шесть карт | свойства ~MI_*~ в Details | нет |
| Состав карт: ~Size~, ~Scale~, ~Octaves~, ~Relief~, ~Cracks~, ~Strata~, ~Moisture~, ~Moss~, ~Roughness~, ~Brick~, ~Veins~, сид, палитра | компонент, консольная команда, commandlet или Blueprint | да, карты пересчитываются |
| Сам рецепт: ноды, выходы, новые параметры | Material Editor в ~M_KitsuneRock~ | нет, но это задевает все ~MI_*~ сразу |

Ключевое различие: ~Size ~Scale ~Octaves ~Relief ~Cracks ~Strata ~Moisture ~Moss ~Brick ~Veins~ и палитра **запечены в пиксели** — без пересчёта карт их не изменить. ~Tiling~, ~Tint~, ~Specular~, ~AOStrength~, ~NormalStrength~, ~HeightStrength~, ~EmissiveStrength~, ~VeinColor~ живут в параметрах материала и правятся за секунды.

### 7.1 Быстрая правка готового материала

Открыть ~MI_*~ двойным щелчком → Details → группы «Текстуры камня» и «Камень» (~Group~ у нод мастера именно такие).

| Хочу | Что поставить |
|---|---|
| Мокрый, блестящий камень | ~RoughnessAdjust~ 0.4…0.5, ~Specular~ 0.6…0.8 |
| Сухой и матовый | ~RoughnessAdjust~ 1.2…1.4, ~Specular~ 0.15 |
| Убрать параллакс (плоские стены дешевле) | ~HeightStrength~ 0 |
| Заметнее рельеф | ~NormalStrength~ 1.5…2 (выше 2 обычно «пересолонный» камень) |
| Выключить свечение жил у ~MI_lavaVeins~ | ~EmissiveStrength~ 0 |
| Другой масштаб плитки | ~Tiling~ 2 — карта ляжет дважды на тех же UV |
| Общий оттенок (тёплая пещера, холодный склеп) | ~Tint~ |
| Другой цвет свечения | ~VeinColor~ |
| Меньше бросается повтор плитки | ~WarpAmount~ 0.15, ~DetailMix~ 0.4 (см. 5.1) |
| Вернуть строгую периодичность карт | ~WarpAmount~ 0, ~DetailMix~ 0, ~UVOffsetX~ 0, ~UVOffsetY~ 0 |
| Разные стены из одного и того же ~MI_*~ | ~UVOffsetX~ / ~UVOffsetY~ или динамический инстанс (5.1) |

Повторная генерация **с тем же именем** перезаписывает ~MI_*~ и сбрасывает эти правки на значения пресета. Поэтому либо правьте после финальной генерации, либо защищайте правки дочерним инстансом: правый клик по ~MI_basalt~ → Create Material Instance → ~MI_WallA~. Он наследует карты и скаляры, перекрывает только своё, и генератор его не трогает.

### 7.2 Новый материал из уже готовых карт (без генерации)

Мастер один, инстансов может быть сколько угодно:

1. Правый клик по ~M_KitsuneRock~ → Create Material Instance → ~MI_Granite02~.
2. В Details назначить шесть карт — их можно смешивать от разных пресетов: BaseColor и Normal от ~basalt~, Roughness и AO от ~mossyCave~.
3. Выставить скаляры и векторы по таблице параметров §5.

Так получается «тот же камень, но другой по цвету/масштабу/блику» без единого пересчёта пикселей.

### 7.3 Новый комплект карт

**В редакторе (основной путь).** Добавить на любой актор компонент «Генератор материалов камня» ([KitsuneRockGeneratorComponent.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockGeneratorComponent.h#L14)) и в Details по порядку: «Пресет» → кнопка «Взять значения пресета» → слайдеры (они 1-в-1 как на странице) → «Случайный сид» (это ~🎲~) → «Имя ассета», например ~WallRockA~ → «Сгенерировать материал». Создаются ~T_WallRockA_*~ (шесть карт) и ~MI_WallRockA~. Галочка «Назначить материал на меши владельца» плюс кнопка «Назначить материал на меши» вешают результат на статик-меши актора.

**Консоль редактора** (клавиша ~):

~~~
Kitsune.Rock.Generate WallRockA basalt 512 777
Kitsune.Rock.GenerateAll 256
~~~

**Пакетно, без открытого редактора** — тот же commandlet с индивидуальными настройками:

~~~bash
... -run=KitsuneRock -name=WallRockA -preset=mossyCave -size=1024 -seed=7 \
    -relief=0.8 -cracks=0.5 -moss=0.4 -roughness=0.6 -veins=0.3
~~~

**Из Blueprint**: «Создать мастер-материал камня», «Сгенерировать материал камня», «Сгенерировать все пресеты камня» (раздел 2, «Как пользоваться»). Все они ~DevelopmentOnly~: в shipping-сборке их нет, созданные ассеты работают как обычные материалы.

Имена всегда такие: ~MI_<Имя>~ и ~T_<Имя>_{BaseColor,Normal,Roughness,AO,Height,Emissive}~. Новое имя = новый комплект, существующее имя = перезапись комплекта.

### 7.4 Свой пресет

1. В [KitsuneRockParams.h](Source/SemesterKapsWorld/Kitsune/KitsuneRockParams.h#L13) добавить пункт в ~EKitsuneRockPreset~ (например ~Marmalade UMETA(DisplayName = "Мрамор")~).
2. В [KitsuneRockParams.cpp](Source/SemesterKapsWorld/Kitsune/KitsuneRockParams.cpp#L41) дописать строку в ~GKitsuneRockPresets~: ключ (~"marmalade"~), русскую подпись, десять слайдеров и шесть цветов в sRGB-байтах.
3. Пересобрать редакторскую цель.

После этого пресет доступен везде: в выпадающем списке компонента, в ~Kitsune.Rock.Generate <Имя> marmalade~, в ~-preset=marmalade~ и в Blueprint-енуме. Ничего, кроме этих двух мест, править не нужно — ~ApplyPreset~ читает ту же таблицу.

### 7.5 Правка графа мастера

~M_KitsuneRock~ создаётся кодом один раз и больше не пересобирается: правки графа, сделанные в Material Editor, сохраняются за вами.

- Новая нода-параметр, добавленная в мастер, автоматически появляется в Details всех ~MI_*~.
- Не переименовывайте шесть узлов ~Param2D~ и скаляры: и C++ (~namespace KitsuneRockMaterial~), и ~-verify~, и ~SyncMasterSamplerTypes~ находят их по именам ~T_BaseColor ~T_Normal ~T_Roughness ~T_AO ~T_Height ~T_Emissive ~Tiling~ и так далее. Переименовали — генератор больше не сможет назначить карты и починить сэмплеры, карты придётся вести руками.
- ~SyncMasterSamplerTypes~ лечит только существующие шесть параметров; удалённую ноду он не вернёт.
- Масштабные изменения рецепта (Mask-blended layers, Decal, Terrain-blended) делают либо в другом master-материале, либо через Material Function — не в ~M_KitsuneRock~.

### 7.6 Правка самих картинок

~T_*~ — обычные ~UTexture2D~, их свойства (Compression Settings, ~SRGB~, LOD Group, Mip Gen, wrapping) правятся в Details текстуры. Важно: тип сэмплера узла мастера обязан соответствовать компрессии и ~SRGB~ карты (§ «Красный ERROR!»), поэтому после смены ~CompressionSettings~ или ~SRGB~ запустите ~Kitsune.Rock.SyncMaster~.

Пиксели правятся снаружи: ~-dump=<папка>~ пишет карты в PPM, конвертер (ImageMagick или ffmpeg) делает из них PNG, после правки в GIMP/Substance/Photoshop в Content Browser — правый клик по ~T_*~ → Reimport. Свойства компрессии при импорте стоит перепроверить. Проще лишний раз перегенерировать карты с другим сидом: это секунды.

---

## 8. Пересборка и проверка

~~~bash
~/UnrealEngine-5.8.3/Engine/Build/BatchFiles/Linux/Build.sh \
  SemesterKapsWorldEditor Linux Development \
  -Project="/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject"
~~~

Логи: ~Saved/Logs/kitsune_build.log~ (сборка) и ~Saved/Logs/SemesterKapsWorld.log~ (редактор).

### Проверка целостности ассетов: ~-verify~

~~~bash
~/UnrealEngine-5.8.3/Engine/Binaries/Linux/UnrealEditor-Cmd \
  "/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject" \
  -run=KitsuneRock -all -size=512 -verify -nullrhi -unattended
~~~

Commandlet без открытого редактора проверяет структуру, а не HLSL: что master-материал
существует и в его графе собрано всё необходимое — шесть параметров текстур (у каждого
своя карта по умолчанию и тип сэмплера, совпадающий с этой картой), тринадцать
скалярных (семь прежних плюс ~UVOffsetX~, ~UVOffsetY~, ~WarpAmount~, ~WarpScale~,
~DetailScale~, ~DetailMix~), два векторных, один Custom-узел нормали (~CMOT_Float3~ с
входами ~NormalIn~, ~DetailIn~, ~Mix~ и ~Strength~), один ~BumpOffset~, два узла
~Noise~ и один ~WorldPosition~ — это каскад анти-тайлинга; что каждый ~MI_*~ ссылается
на master; и что у каждого пресета на
диске лежат все шесть текстур одного размера. Размер берётся из ~Source~ текстуры:
~GetSizeX()~ заполняется только после сборки ресурса, а в commandlet он нулевой.

~-verify~ работает после генерации, поэтому для проверки без перезаписи ассетов держите
~-nosave~. Успех — код выхода 0 и строка «Проверка пройдена: граф мастера цел, типы
сэмплеров шести параметров совпадают с их картами, инстансов 8, у всех шесть карт одного
размера»; при нехватке инстанса, карты, параметра или при несовпадении типа сэмплера —
«Проверка не пройдена: замечаний N» и код выхода 1.

Конструктор ~UMaterialExpressionCustom~ всегда заводит один безымянный слот входа
([MaterialExpressions.cpp:12747](</home/german/UnrealEngine-5.8.3/Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressions.cpp#L12747>)),
поэтому в ассете, созданном ранней версией кода, Custom-узел показывает три входа:
~None~, ~NormalIn~, ~Strength~. Пустой слот ни к чему не подключён и кодом не читается —
проверка его допускает; новые master-материалы имеют ровно два входа.

### Встроить анти-тайлинг в уже существующий мастер

~~~bash
~/UnrealEngine-5.8.3/Engine/Binaries/Linux/UnrealEditor-Cmd \
  "/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject" \
  -run=KitsuneRock -antitiling -nullrhi -unattended
~~~

Ставит каскад (~UVOffsetX/Y~, ~WarpAmount/WarpScale~, ~DetailScale/DetailMix~) в
~M_KitsuneRock~; если каскад уже был — меняет тип волны и те значения по умолчанию,
которые ещё равны прежним. Одновременно перечисляет эти значения во всех ~MI_*~
каталога, кроме инстансов с «brick» в имени: у них остаётся то, что в них стоит.
Карты и ~UVOffsetX/Y~ инстансов не трогаются; сохраняются мастер и изменённые
инстансы. Повторный запуск не добавляет ни одного узла. В редакторе та же операция —
~Kitsune.Rock.AntiTiling [папка] [nosave]~ или Blueprint-функция «Добавить
анти-тайлинг в мастер камня». При обычной генерации каскад встраивается сам,
отдельный запуск нужен только старому ассету.

Конструктор ~UMaterialExpressionCustom~ всегда заводит один безымянный слот входа
([MaterialExpressions.cpp:12747](</home/german/UnrealEngine-5.8.3/Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressions.cpp#L12747>)),
и в самом раннем ~M_KitsuneRock~ Custom-узел показывал три входа: ~None~, ~NormalIn~,
~Strength~. Каскад анти-тайлинга пересоздаёт входы этого узла начисто, поэтому в
текущем ассете ровно четыре именованных входа: ~NormalIn~, ~DetailIn~, ~Mix~,
~Strength~ — пустого слота больше нет.

### Красный «ERROR!» под узлами ~Param2D~: тип сэмплера

Движок сам решает, каким сэмплером надо читать текстуру, по её компрессии и ~SRGB~
([MaterialExpressionUtils.cpp:14](</home/german/UnrealEngine-5.8.3/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionUtils.cpp#L14>))
и сверяет это решение со свойством ~SamplerType~ узла
([там же, строка 51](</home/german/UnrealEngine-5.8.3/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionUtils.cpp#L51>)).
Несовпадение — это ошибка компиляции материала:

~~~text
(Node TextureSampleParameter2D) Sampler type is Grayscale, should be Color
    for /Engine/EngineResources/DefaultTexture.DefaultTexture
Failed to compile Material for platform SF_VULKAN_SM6, Default Material will be used in game
~~~

Причины две, и обе были в master-материале:

1. У узла-параметра не было своей текстуры, поэтому движок подставляет
   ~/Engine/EngineResources/DefaultTexture~, а у неё тип ~Color~. Любой узел с ~Normal~
   или ~Grayscale~ оказывался несовместим — отсюда «ERROR!» под узлами ~T_Normal~,
   ~T_Roughness~, ~T_AO~, ~T_Height~, ~T_Emissive~ и некомпилируемый master.
2. Картам ~TC_Grayscale~ без ~SRGB~ (наши Roughness, AO, Height, Emissive) нужен
   ~LinearGrayscale~, а не ~Grayscale~. ~Grayscale~ подходит только grayscale-карте с
   включённым ~SRGB~.

Лечение в коде: генератор вызывает ~SyncMasterSamplerTypes~ — он подставляет каждому
из шести параметров реальную карту (первую найденную из ~T_granite_* ~, при её отсутствии
карту собранного пресета) и вычисляет ~SamplerType~ через
~MaterialExpressionUtils::GetSamplerTypeForTexture~, то есть тип всегда берётся из
фактической текстуры, а не из предположения. Инстансы ~MI_*~ при этом перекрывают все
шесть параметров своими картами, поэтому они и раньше компилировались: ошибка касалась
только самого master-материала.

Готовый мастер можно починить отдельно, не пересобирая ни карт, ни инстансов:

~~~bash
~/UnrealEngine-5.8.3/Engine/Binaries/Linux/UnrealEditor-Cmd \
  "/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject" \
  -run=KitsuneRock -syncmaster -nullrhi -unattended
~~~

Он печатает ««~AO~»: тип сэмплера ~Grayscale~ -> ~LinearGrayscale~ (карта ~T_granite_AO~)» и
сохраняет только ~M_KitsuneRock~. Реальный прогон на этом проекте (мастер был создан
ранней версией кода): четыре узла получили ~Linear Grayscale~, шесть узлов получили карты
по умолчанию — «M_KitsuneRock: исправлено узлов 10», код выхода 0; карты ~T_*~ и все
~MI_*~ остались нетронутыми — дата изменения изменилась только у
~M_KitsuneRock.uasset~. Проверка ~-verify~ сразу после починки пишет «Проверка пройдена: ... типы сэмплеров шести
параметров совпадают с их картами» и не выдаёт ни одного замечания по сэмплерам. С ~-nosave~ тот же прогон ничего не пишет на диск —
им удобно проверять, не закрывая редактор. В редакторе то же самое делается руками:
перетащить ~T_granite_<карта>~ в поле ~Texture~ каждого узла ~Param2D~ — движок тогда сам
поправит ~SamplerType~ (~MaterialExpressions.cpp:2625~, ~AutoSetSampleType~) и ~ERROR!~ исчезнет.

### Числовая сверка с веб-генератором: ~-dump~

~-dump=<папка>~ дописывает шесть карт каждого пресета в PPM (P6), и их сравнивают
с эталонным JS прямо из HTML:

~~~bash
~/UnrealEngine-5.8.3/Engine/Binaries/Linux/UnrealEditor-Cmd \
  "/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject" \
  -run=KitsuneRock -all -size=512 -nosave \
  -dump="/home/german/UnrealProjects/Semester-project-KapsWorld/Saved/KitsuneDump512" \
  -nullrhi -unattended

node Saved/KitsuneDump/compare_reference.mjs 512 Saved/KitsuneDump512
~~~

Скрипт исполняет ~<script>~ из страницы в ~node:vm~ с заглушками DOM и сравнивает
байты карт. Прогон на восьми пресетах (512×512, сид 1337): BaseColor, Normal,
Roughness, AO и Height совпадают с веб-версией, среднее расхождение 0,0000,
максимальное — 1 из 255. Разница только в округлении: JS округняет половину к
чётному, C++ — от нуля, поэтому расходятся лишь значения ровно ~x,5~.

### Про исходный HTML

В веб-версии в каждом пресете ключ ~moss~ объявлен дважды: сначала как количество
мха (число), затем как цвет мха (строка). В объекте JS побеждает последнее
объявление, слайдер «Мох» получает строку, ~+value = NaN~ — мох не подмешивается, а
карта Roughness выходит чёрной, потому что ~NaN~ в ~Uint8ClampedArray~ становится
нулём. В переносе это исправлено: ~Moss~ — количество, ~MossColor~ — цвет. Исправленная
копия страницы — [kitsune_rock_generator_fixed.html](Reference/kitsune_rock_generator_fixed.html),
сверка выше запускает её; оригинал не изменён.

### Компиляция шейдеров

Headless-commandlet шейдеры материалов не компилирует: без рендеринга движок не
создаёт ресурсы материала, поэтому ~IsCompilingOrHadCompileError~ в commandlet всегда
отвечает «нет ресурсов», а ~AppendCompileStateDebugInfo~ пишет «No resources created» —
проверять HLSL оттуда бессмысленно (в этом и причина, по которой ~-verify~ проверяет
структуру ассетов).

Шейдеры ~M_KitsuneRock~ собираются, когда материал открывают в Material Editor, или при
готовке проекта:

~~~bash
~/UnrealEngine-5.8.3/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun \
  -project="/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject" \
  -platform=Linux -clientconfig=Development -build -nocompileeditor -cook -skipstage
~~~

Готовка на этой машине — ~14 минут и ~20 ядер clang'а~. Первая попытка упала на 13-й минуте
не из-за материалов, а из-за моей ошибки: helper, который звал ~ForceRecompileForRendering~
и ~AppendCompileStateDebugInfo~, лежал вне ~#if WITH_EDITOR~, и игровая цель (~WITH_EDITOR=0~)
не собрала ~KitsuneRockCommandlet.cpp~ (~no member named 'ForceRecompileForRendering'~).
Сейчас весь ~-verify~ под ~#if WITH_EDITOR~, и игровая цель пересобрана начисто —
~Result: Succeeded~ за 60 с:

~~~bash
~/UnrealEngine-5.8.3/Engine/Build/BatchFiles/Linux/Build.sh \
  SemesterKapsWorld Linux Development \
  -Project="/home/german/UnrealProjects/Semester-project-KapsWorld/SemesterKapsWorld.uproject"
~~~

То же самое делает обычный ~BuildCookRun -build~: если игровая цель собирается, значит
генератора в shipping-сборке нет. Ошибка в Custom-узле или в параметре видна красной
полосой в Material Editor и останавливает готовку сообщением о компиляции шейдера.

---

## 9. Стоимость и ограничения

- Расчёт карт ~512×512~ ≈ 0,1…0,3 с на 28 ядрах (~ParallelFor~), ~1024×1024~ ≈ 1…2 с.
  Это редакторская операция: в игре ничего не считается.
- Повторная генерация с тем же именем перезаписывает ассеты. Ручные правки
  параметров ~MI_*~ при этом сбрасываются на значения пресета — их лучше делать
  в отдельном инстансе или после финальной генерации.
- ~M_KitsuneRock~ создаётся один раз; существующий материал не пересобирается,
  чтобы не затереть правки, сделанные в редакторе материалов вручную. При этом
  мастер всё равно проходит ~SyncMasterSamplerTypes~: ему подставляются карты по
  умолчанию и правильные типы сэмплеров, поэтому старые ассеты лечатся без удаления —
  ~Kitsune.Rock.SyncMaster~ в консоли редактора или ~-run=KitsuneRock -syncmaster~
  (карты и ~MI_*~ не трогаются, сохраняется только ~M_KitsuneRock~).
  Дополнительно мастер загружается с диска (~FPackageName::DoesPackageExist~ +
  ~LoadObject~): раньше в commandlet он не находился через ~FindObject~ и
  пересоздавался с нуля, что затёрло бы правки графа, сделанные в Material Editor.
  Помимо ~SyncMasterSamplerTypes~ мастер проходит ~UpgradeMasterAntiTiling~: каскад
  анти-тайлинга (5.1) встраивается автоматически, а если он уже встроен — смягчается
  до нынешних значений, не трогая то, что правилось вручную в Material Editor.
  Поэтому старый ~M_KitsuneRock~ лечится без удаления: ~Kitsune.Rock.AntiTiling~ в
  консоли редактора или ~-run=KitsuneRock -antitiling~.
- ~Size~ должен быть степенью двойки 128…2048, другое значение округляется
  (~GetValidSize~), остальные параметры зажимаются в свои диапазоны (~Sanitize~).
- В shipping-сборке генерации нет: весь код под ~WITH_EDITOR~, функции ~DevelopmentOnly~.
  Созданные ассеты работают как обычные материалы.
- Карта высот используется только для параллакса (BumpOffset). Если нужен
  True Virtual Heightmap — включите его в свойствах ~T_*_Height~.

## 10. Что можно добавить позже

- Процедурный вариант без карт на диске: тот же алгоритм в HLSL через Custom-ноду
  (~Shaders/KitsuneRockGenerator.ush~ + ~M_KitsuneRockProcedural~).
- Экспорт карт в PNG для Substance/Blender.
- Mask-карта для Mask-blended layer-материалов (трава в трещинах, разрушаемость).
