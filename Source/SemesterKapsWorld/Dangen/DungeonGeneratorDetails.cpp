// DungeonGeneratorDetails.cpp
// Свой вид панели Details для ADungeonGenerator (работает и для BP_DungeonGenerator:
// панель берёт настройки родительского C++-класса).
//
// 1. Кнопки Generate / BuildGeometry / ClearGenerated / ResetRoomTemplates — в самой
//    верхней секции «Действия» (строки с кнопками создаёт сам редактор, мы лишь
//    подтягиваем их категорию наверх; порядок внутри задаёт meta = (DisplayPriority)).
// 2. Типы комнат — компактным меню: у каждого типа карточка, где видно и можно менять
//    название, минимальный и максимальный размер, форму и количество. Там же кнопки
//    «+ тип», «Копия», «Удалить», «Сбросить к встроенным» и живая сводка
//    «сколько комнат запланировано противMaxRooms».
//    Обычные строки массива (узор, цвет) никуда не делись: они видны, когда в Details
//    включён Advanced View (гаечный ключ) — мы перенесли их в Advanced.
// 3. Остальное — понятными группами: лимит комнат и ключ, поле и сетка, дороги,
//    геометрия; результат и компоненты уходят вниз.
//
// Файл целиком редакторский: в игровых сборках он не участвует.
#include "DungeonGeneratorDetails.h"

#if WITH_EDITOR

#include "DungeonGenerator.h"

#include "DetailBuilderTypes.h"      // EPropertyLocation
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"                  // GEditor
#include "PropertyHandle.h"          // IPropertyHandle
#include "ScopedTransaction.h"       // Ctrl+Z для правок из меню

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"       // SVerticalBox / SHorizontalBox
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FDungeonGeneratorDetails"

namespace
{
	// ---------- мелкие типы ----------

	// Что редактирует карточка: комната старта, комната финиша или элемент Templates.
	enum class ECardTarget : uint8
	{
		Start,
		End,
		Template
	};

	// Какое целое поле карточки редактирует спинбокс.
	enum class EIntField : uint8
	{
		WMin,
		WMax,
		HMin,
		HMax,
		Count
	};

	// Границы те же, что у meta = (ClampMin / ClampMax) полей FRoomTemplate.
	static const int32 DimMinLimit = 2;
	static const int32 DimMaxLimit = 40;
	static const int32 CountMinLimit = 0;
	static const int32 CountMaxLimit = 30;

	int32 GetIntField(const FRoomTemplate& Tpl, EIntField Field)
	{
		switch (Field)
		{
		case EIntField::WMin:  return Tpl.WMin;
		case EIntField::WMax:  return Tpl.WMax;
		case EIntField::HMin:  return Tpl.HMin;
		case EIntField::HMax:  return Tpl.HMax;
		case EIntField::Count: return Tpl.Count;
		}
		return 0;
	}

	void SetIntField(FRoomTemplate& Tpl, EIntField Field, int32 Value)
	{
		switch (Field)
		{
		case EIntField::WMin:  Tpl.WMin = Value; break;
		case EIntField::WMax:  Tpl.WMax = Value; break;
		case EIntField::HMin:  Tpl.HMin = Value; break;
		case EIntField::HMax:  Tpl.HMax = Value; break;
		case EIntField::Count: Tpl.Count = Value; break;
		}
	}

	// Пункт выпадающего списка. enum class в TSharedPtr как есть не кладётся,
	// поэтому значение храним int32 и приводим при записи.
	struct FEnumOption
	{
		FEnumOption(int32 InValue, const FText& InLabel) : Value(InValue), Label(InLabel) {}

		int32 Value;
		FText Label;
	};

	// Общая часть всех виджетов меню комнат. Держится shared_ptr'ом самими виджетами,
	// поэтому живёт ровно столько, сколько они. SBox-контейнер меню — только слабой
	// ссылкой: им владеет панель Details (иначе вышел бы цикл из shared_ptr).
	struct FPanelData
	{
		TArray<TWeakObjectPtr<ADungeonGenerator>> Generators;

		TArray<TSharedPtr<FEnumOption>> ShapeOptions;
		TArray<TSharedPtr<FEnumOption>> ConditionOptions;

		TWeakPtr<SBox> Container;
		// Пересобрать всё меню — нужно после добавления/удаления типа комнаты.
		TFunction<void()> Rebuild;

		// Первый валидный генератор — источник значений для отображения.
		ADungeonGenerator* First() const
		{
			for (const TWeakObjectPtr<ADungeonGenerator>& Weak : Generators)
			{
				if (ADungeonGenerator* Generator = Weak.Get())
				{
					return Generator;
				}
			}
			return nullptr;
		}

		int32 TypeCount() const
		{
			const ADungeonGenerator* Generator = First();
			return Generator ? Generator->Templates.Num() : 0;
		}

		FRoomTemplate* GetTemplate(ADungeonGenerator* Generator, ECardTarget Target, int32 Index) const
		{
			if (!Generator)
			{
				return nullptr;
			}
			switch (Target)
			{
			case ECardTarget::Start:
				return &Generator->StartRoom;
			case ECardTarget::End:
				return &Generator->EndRoom;
			case ECardTarget::Template:
				return (Index >= 0 && Index < Generator->Templates.Num()) ? &Generator->Templates[Index] : nullptr;
			}
			return nullptr;
		}

		const FEnumOption* FindOption(const TArray<TSharedPtr<FEnumOption>>& Options, int32 Value) const
		{
			for (const TSharedPtr<FEnumOption>& Option : Options)
			{
				if (Option.IsValid() && Option->Value == Value)
				{
					return Option.Get();
				}
			}
			return nullptr;
		}

		FText OptionLabel(const TArray<TSharedPtr<FEnumOption>>& Options, int32 Value) const
		{
			const FEnumOption* Option = FindOption(Options, Value);
			return Option ? Option->Label : LOCTEXT("UnknownOption", "(нет)");
		}

		// min <= max и всё в разрешённом диапазоне: иначе генератор упирается в свои Clamp.
		static void Normalize(FRoomTemplate& Tpl)
		{
			Tpl.WMin = FMath::Clamp(Tpl.WMin, DimMinLimit, DimMaxLimit);
			Tpl.WMax = FMath::Clamp(Tpl.WMax, Tpl.WMin, DimMaxLimit);
			Tpl.HMin = FMath::Clamp(Tpl.HMin, DimMinLimit, DimMaxLimit);
			Tpl.HMax = FMath::Clamp(Tpl.HMax, Tpl.HMin, DimMaxLimit);
			Tpl.Count = FMath::Clamp(Tpl.Count, CountMinLimit, CountMaxLimit);
		}

		// Правка сразу по всем выделенным генераторам (обычно выделен один): с
		// транзакцией (правка откатывается Ctrl+Z) и отметкой «уровень изменён».
		template <typename MutateType>
		void Apply(const FText& Reason, const MutateType& Mutate) const
		{
			TSharedPtr<FScopedTransaction> Transaction;
			if (GEditor)
			{
				Transaction = MakeShared<FScopedTransaction>(Reason);
			}

			for (const TWeakObjectPtr<ADungeonGenerator>& Weak : Generators)
			{
				if (ADungeonGenerator* Generator = Weak.Get())
				{
					Generator->Modify();
					Mutate(Generator);
					Generator->MarkPackageDirty();
				}
			}
		}
	};

	// ---------- билдеры отдельных полей ----------

	FText CardTitle(ECardTarget Target, int32 Index)
	{
		switch (Target)
		{
		case ECardTarget::Start:
			return LOCTEXT("StartCardTitle", "▶ Старт");
		case ECardTarget::End:
			return LOCTEXT("EndCardTitle", "■ Финиш");
		default:
			return FText::Format(LOCTEXT("TemplateCardTitle", "Тип {0}"), FText::AsNumber(Index + 1));
		}
	}

	TSharedRef<SWidget> MakeLabel(const FText& Text, bool bBold = false)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(bBold ? IDetailLayoutBuilder::GetDetailFontBold() : IDetailLayoutBuilder::GetDetailFont());
	}

	// Поле: подпись сверху, виджет снизу, фиксированная ширина (карточка не «прыгает»).
	TSharedRef<SWidget> MakeFieldCell(const FText& Label, TSharedRef<SWidget> Field, float Width)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 2.f)
			[
				MakeLabel(Label)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(Width)
				[
					Field
				]
			];
	}

	TSharedRef<SWidget> MakeButton(const FText& Label, const FText& ToolTip, TFunction<void()> OnClickedAction)
	{
		TSharedRef<SButton> Button = SNew(SButton)
			.ContentPadding(FMargin(8.f, 3.f))
			.OnClicked_Lambda([OnClickedAction]() -> FReply
			{
				OnClickedAction();
				return FReply::Handled();
			})
			.Content()
			[
				MakeLabel(Label)
			];

		Button->SetToolTipText(ToolTip);
		return Button;
	}

	// Числовое поле: мин./макс. размер в клетках либо количество комнат.
	TSharedRef<SWidget> MakeIntSpinBox(TSharedRef<FPanelData> Data, ECardTarget Target, int32 Index, EIntField Field, const FText& ToolTip)
	{
		const int32 Lo = (Field == EIntField::Count) ? CountMinLimit : DimMinLimit;
		const int32 Hi = (Field == EIntField::Count) ? CountMaxLimit : DimMaxLimit;

		TSharedRef<SSpinBox<int32>> Spin = SNew(SSpinBox<int32>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.MinValue(Lo)
			.MaxValue(Hi)
			.MinSliderValue(Lo)
			.MaxSliderValue(Hi)
			.Delta(1)
			.EnableWheel(true)
			.Value_Lambda([Data, Target, Index, Field, Lo]() -> int32
			{
				const FRoomTemplate* Tpl = Data->GetTemplate(Data->First(), Target, Index);
				return Tpl ? GetIntField(*Tpl, Field) : Lo;
			})
			.OnValueChanged_Lambda([Data, Target, Index, Field](int32 NewValue)
			{
				Data->Apply(LOCTEXT("ChangeRoomSize", "Изменить настройки комнат"),
					[Data, Target, Index, Field, NewValue](ADungeonGenerator* Generator)
					{
						FRoomTemplate* Tpl = Data->GetTemplate(Generator, Target, Index);
						if (!Tpl)
						{
							return;
						}
						SetIntField(*Tpl, Field, NewValue);
						FPanelData::Normalize(*Tpl);
					});
			});

		Spin->SetToolTipText(ToolTip);
		return Spin;
	}

	// Выпадающий список: «Форма» (bShape = true) или «Где ставить» (false).
	TSharedRef<SWidget> MakeEnumCombo(TSharedRef<FPanelData> Data, ECardTarget Target, int32 Index, bool bShape)
	{
		TArray<TSharedPtr<FEnumOption>>& Options = bShape ? Data->ShapeOptions : Data->ConditionOptions;

		const FRoomTemplate* CurrentTpl = Data->GetTemplate(Data->First(), Target, Index);
		const int32 CurrentValue = CurrentTpl ? (bShape ? (int32)CurrentTpl->Shape : (int32)CurrentTpl->Condition) : 0;

		TSharedPtr<FEnumOption> Initial;
		for (const TSharedPtr<FEnumOption>& Option : Options)
		{
			if (Option.IsValid() && Option->Value == CurrentValue)
			{
				Initial = Option;
				break;
			}
		}

		return SNew(SComboBox<TSharedPtr<FEnumOption>>)
			.OptionsSource(&Options)
			.InitiallySelectedItem(Initial)
			.OnGenerateWidget_Lambda([](TSharedPtr<FEnumOption> Item) -> TSharedRef<SWidget>
			{
				return SNew(STextBlock)
					.Text(Item.IsValid() ? Item->Label : FText::GetEmpty())
					.Font(IDetailLayoutBuilder::GetDetailFont());
			})
			.OnSelectionChanged_Lambda([Data, Target, Index, bShape](TSharedPtr<FEnumOption> Item, ESelectInfo::Type)
			{
				if (!Item.IsValid())
				{
					return;
				}
				const int32 NewValue = Item->Value;
				Data->Apply(LOCTEXT("ChangeRoomShape", "Изменить настройки комнат"),
					[Data, Target, Index, bShape, NewValue](ADungeonGenerator* Generator)
					{
						FRoomTemplate* Tpl = Data->GetTemplate(Generator, Target, Index);
						if (!Tpl)
						{
							return;
						}
						if (bShape)
						{
							Tpl->Shape = (ERoomShape)NewValue;
						}
						else
						{
							Tpl->Condition = (ERoomCondition)NewValue;
						}
					});
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([Data, Target, Index, bShape]() -> FText
				{
					const FRoomTemplate* Tpl = Data->GetTemplate(Data->First(), Target, Index);
					if (!Tpl)
					{
						return FText::GetEmpty();
					}
					const int32 Value = bShape ? (int32)Tpl->Shape : (int32)Tpl->Condition;
					return Data->OptionLabel(bShape ? Data->ShapeOptions : Data->ConditionOptions, Value);
				})
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}

	// Название типа. Стартовая и финальная комнаты тоже подписаны — их можно переименовать.
	TSharedRef<SWidget> MakeNameField(TSharedRef<FPanelData> Data, ECardTarget Target, int32 Index)
	{
		return SNew(SEditableTextBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([Data, Target, Index]() -> FText
			{
				const FRoomTemplate* Tpl = Data->GetTemplate(Data->First(), Target, Index);
				return Tpl ? FText::FromString(Tpl->Name) : FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([Data, Target, Index](const FText& NewText, ETextCommit::Type)
			{
				const FString NewName = NewText.ToString();
				Data->Apply(LOCTEXT("RenameRoomType", "Переименовать тип комнаты"),
					[Data, Target, Index, NewName](ADungeonGenerator* Generator)
					{
						if (FRoomTemplate* Tpl = Data->GetTemplate(Generator, Target, Index))
						{
							Tpl->Name = NewName;
						}
					});
			});
	}

	// Строка под карточкой: размеры в клетках и в метрах, форма, количество.
	FText CardSummary(const FPanelData& Data, ECardTarget Target, int32 Index)
	{
		ADungeonGenerator* Generator = Data.First();
		FRoomTemplate* Tpl = Data.GetTemplate(Generator, Target, Index);
		if (!Tpl || !Generator)
		{
			return FText::GetEmpty();
		}

		const float CellMeters = Generator->CellSize / 100.f;
		const FText ShapeText = Data.OptionLabel(Data.ShapeOptions, (int32)Tpl->Shape);
		const int32 Count = (Target == ECardTarget::Template) ? Tpl->Count : 1;

		return FText::FromString(FString::Printf(
			TEXT("%d\u2013%d \u00D7 %d\u2013%d кл. (\u2248 %.1f\u2013%.1f \u00D7 %.1f\u2013%.1f м) \u00B7 %s \u00B7 %d шт."),
			Tpl->WMin, Tpl->WMax, Tpl->HMin, Tpl->HMax,
			Tpl->WMin * CellMeters, Tpl->WMax * CellMeters,
			Tpl->HMin * CellMeters, Tpl->HMax * CellMeters,
			*ShapeText.ToString(), Count));
	}

	// Кнопки карточки: «Копия» и «Удалить». У старта и финиша их нет.
	TSharedRef<SWidget> MakeCardButtons(TSharedRef<FPanelData> Data, ECardTarget Target, int32 Index)
	{
		if (Target != ECardTarget::Template)
		{
			return SNew(SBox);
		}

		const TFunction<void()> Rebuild = Data->Rebuild;

		TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);

		Buttons->AddSlot()
			.AutoWidth()
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("DuplicateType", "Копия"),
					LOCTEXT("DuplicateTypeTip", "Добавить такой же тип комнаты рядом (Ctrl+Z отменяет)"),
					[Data, Index, Rebuild]()
					{
						Data->Apply(LOCTEXT("DuplicateRoomType", "Дублировать тип комнаты"),
							[Index](ADungeonGenerator* Generator)
							{
								if (Index >= 0 && Index < Generator->Templates.Num())
								{
									Generator->Templates.Insert(Generator->Templates[Index], Index + 1);
								}
							});
						Rebuild();
					})
			];

		Buttons->AddSlot()
			.AutoWidth()
			[
				MakeButton(LOCTEXT("DeleteType", "Удалить"),
					LOCTEXT("DeleteTypeTip", "Убрать этот тип комнаты (Ctrl+Z отменяет)"),
					[Data, Index, Rebuild]()
					{
						Data->Apply(LOCTEXT("DeleteRoomType", "Удалить тип комнаты"),
							[Index](ADungeonGenerator* Generator)
							{
								if (Index >= 0 && Index < Generator->Templates.Num())
								{
									Generator->Templates.RemoveAt(Index);
								}
							});
						Rebuild();
					})
			];

		return Buttons;
	}

	// Карточка одного типа комнаты: название + кнопки, размеры, форма/количество/условие, итог.
	TSharedRef<SWidget> MakeCard(TSharedRef<FPanelData> Data, ECardTarget Target, int32 Index)
	{
		const bool bTemplate = (Target == ECardTarget::Template);

		// Строка «Форма / Кол-во / Где ставить» собирается программно: у старта и финиша
		// количества и условия нет (комната всегда одна и ставится по своему правилу).
		TSharedRef<SHorizontalBox> ShapeRow = SNew(SHorizontalBox);

		ShapeRow->AddSlot()
			.AutoWidth()
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeFieldCell(LOCTEXT("ShapeLabel", "Форма"),
					MakeEnumCombo(Data, Target, Index, true), 116.f)
			];

		if (bTemplate)
		{
			ShapeRow->AddSlot()
				.AutoWidth()
				.Padding(0.f, 0.f, 6.f, 0.f)
				[
					MakeFieldCell(LOCTEXT("CountLabel", "Кол-во"),
						MakeIntSpinBox(Data, Target, Index, EIntField::Count,
							LOCTEXT("CountTip", "Сколько комнат этого типа ставить. Общий потолок для всех типов — «Максимум комнат».")), 52.f)
				];

			ShapeRow->AddSlot()
				.FillWidth(1.f)
				[
					MakeFieldCell(LOCTEXT("ConditionLabel", "Где ставить"),
						MakeEnumCombo(Data, Target, Index, false), 150.f)
				];
		}

		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8.f, 6.f))
			[
				SNew(SVerticalBox)

				// --- строка 1: подпись, название типа, кнопки ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 5.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						MakeLabel(CardTitle(Target, Index), true)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						MakeNameField(Data, Target, Index)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						MakeCardButtons(Data, Target, Index)
					]
				]

				// --- строка 2: размеры (мин./макс. ширина и глубина в клетках) ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 5.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 6.f, 0.f)
					[
						MakeFieldCell(LOCTEXT("WMinLabel", "Шир. мин"),
							MakeIntSpinBox(Data, Target, Index, EIntField::WMin,
								LOCTEXT("WMinTip", "Минимальная ширина комнаты в клетках. Сколько сантиметров в клетке — в секции «Поле и сетка».")), 52.f)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 6.f, 0.f)
					[
						MakeFieldCell(LOCTEXT("WMaxLabel", "Шир. макс"),
							MakeIntSpinBox(Data, Target, Index, EIntField::WMax,
								LOCTEXT("WMaxTip", "Максимальная ширина комнаты в клетках. Реальный размер берётся случайно между мин. и макс.")), 52.f)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 6.f, 0.f)
					[
						MakeFieldCell(LOCTEXT("HMinLabel", "Глуб. мин"),
							MakeIntSpinBox(Data, Target, Index, EIntField::HMin,
								LOCTEXT("HMinTip", "Минимальная глубина комнаты в клетках (это же «высота» по сетке).")), 52.f)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						MakeFieldCell(LOCTEXT("HMaxLabel", "Глуб. макс"),
							MakeIntSpinBox(Data, Target, Index, EIntField::HMax,
								LOCTEXT("HMaxTip", "Максимальная глубина комнаты в клетках.")), 52.f)
					]
				]

				// --- строка 3: форма, количество, где ставить (готовая строка) ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 5.f)
				[
					ShapeRow
				]

				// --- строка 4: итог по-человечески, в клетках и в метрах ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([Data, Target, Index]() -> FText
					{
						return CardSummary(*Data, Target, Index);
					})
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f)))
					.AutoWrapText(true)
				]
			];
	}

	// Сколько комнат запланировано по текущим типам: старт + финиш + Count всех типов,
	// отдельно считается то, что ставится уже после дорог (пост-комнаты).
	void CountPlanned(const ADungeonGenerator* Generator, int32& OutMain, int32& OutPost)
	{
		OutMain = 2;   // старт + финиш
		OutPost = 0;

		for (const FRoomTemplate& Tpl : Generator->Templates)
		{
			const bool bPost = (Tpl.Condition == ERoomCondition::NextToRoad || Tpl.Condition == ERoomCondition::NextToRoom);
			if (bPost)
			{
				OutPost += Tpl.Count;
			}
			else
			{
				OutMain += Tpl.Count;
			}
		}
	}

	bool PlannedOverCap(const ADungeonGenerator* Generator)
	{
		if (!Generator)
		{
			return false;
		}
		int32 Main = 0, Post = 0;
		CountPlanned(Generator, Main, Post);
		return (Main > FMath::Max(1, Generator->MaxRooms - 1)) || (Main + Post > Generator->MaxRooms);
	}

	// Верх меню: пояснение + живая сводка (красным, если не все комнаты поместятся).
	TSharedRef<SWidget> MakeHeader(TSharedRef<FPanelData> Data)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MenuHint", "Размеры — в клетках сетки, форма строится внутри прямоугольника ширина × глубина. Кнопки генерации — в секции «Действия» выше."))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f)))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Text_Lambda([Data]() -> FText
				{
					const ADungeonGenerator* Generator = Data->First();
					if (!Generator)
					{
						return FText::GetEmpty();
					}

					int32 Main = 0, Post = 0;
					CountPlanned(Generator, Main, Post);

					return FText::FromString(FString::Printf(
						TEXT("%d тип(ов) · комнат запланировано %d (ещё %d у дорог) · максимум %d · поле %d×%d кл. · 1 кл. = %d см%s"),
						Generator->Templates.Num(), Main, Post, Generator->MaxRooms,
						Generator->GridW, Generator->GridH, FMath::RoundToInt(Generator->CellSize),
						PlannedOverCap(Generator) ? TEXT(" — не всё поместится!") : TEXT("")));
				})
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity_Lambda([Data]() -> FSlateColor
				{
					return PlannedOverCap(Data->First())
						? FSlateColor(FLinearColor(1.f, 0.45f, 0.35f))
						: FSlateColor::UseForeground();
				})
				.AutoWrapText(true)
			];
	}

	// Всё меню: сводка, карточка старта, карточка финиша, карточки типов, общая строка кнопок.
	TSharedRef<SWidget> MakeMenu(TSharedRef<FPanelData> Data)
	{
		TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

		Root->AddSlot()
			.AutoHeight()
			[
				MakeHeader(Data)
			];

		Root->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 6.f)
			[
				MakeCard(Data, ECardTarget::Start, -1)
			];

		Root->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 6.f)
			[
				MakeCard(Data, ECardTarget::End, -1)
			];

		for (int32 Index = 0; Index < Data->TypeCount(); ++Index)
		{
			Root->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 6.f)
				[
					MakeCard(Data, ECardTarget::Template, Index)
				];
		}

		const TFunction<void()> Rebuild = Data->Rebuild;

		TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);

		Toolbar->AddSlot()
			.AutoWidth()
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeButton(LOCTEXT("AddType", "+ Добавить тип"),
					LOCTEXT("AddTypeTip", "Новый тип комнаты (как последний в списке): название, мин./макс. размер, форма и количество настраиваются прямо в карточке."),
					[Data, Rebuild]()
					{
						Data->Apply(LOCTEXT("AddRoomType", "Добавить тип комнаты"), [](ADungeonGenerator* Generator)
						{
							FRoomTemplate NewTpl;
							if (Generator->Templates.Num() > 0)
							{
								NewTpl = Generator->Templates.Last();
							}
							NewTpl.Name = TEXT("Новый тип");
							NewTpl.Count = FMath::Max(1, NewTpl.Count);
							FPanelData::Normalize(NewTpl);
							Generator->Templates.Add(NewTpl);
						});
						Rebuild();
					})
			];

		Toolbar->AddSlot()
			.AutoWidth()
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeButton(LOCTEXT("ResetTypes", "Сбросить к встроенным"),
					LOCTEXT("ResetTypesTip", "Вернуть встроенный набор типов: Большой зал, Комната, Кладовая, Крипта, Пост у дороги. Откатывается Ctrl+Z."),
					[Data, Rebuild]()
					{
						Data->Apply(LOCTEXT("ResetRoomTypes", "Сбросить типы комнат"), [](ADungeonGenerator* Generator)
						{
							Generator->ResetRoomTemplates();
						});
						Rebuild();
					})
			];

		Toolbar->AddSlot()
			.AutoWidth()
			[
				MakeButton(LOCTEXT("RemoveAllTypes", "Убрать все типы"),
					LOCTEXT("RemoveAllTypesTip", "Оставить только комнату старта и комнату финиша. Откатывается Ctrl+Z."),
					[Data, Rebuild]()
					{
						Data->Apply(LOCTEXT("RemoveAllRoomTypes", "Убрать все типы комнат"), [](ADungeonGenerator* Generator)
						{
							Generator->Templates.Reset();
						});
						Rebuild();
					})
			];

		Root->AddSlot()
			.AutoHeight()
			[
				Toolbar
			];

		return Root;
	}
}

TSharedRef<IDetailCustomization> FDungeonGeneratorDetails::MakeInstance()
{
	return MakeShareable(new FDungeonGeneratorDetails);
}

void FDungeonGeneratorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	/* Порядок секций: EditCategory отдаёт приоритет ECategoryPriority (Variable — самый
	   верх, Uncommon — самый низ), а внутри одного приоритета порядок = порядку вызовов.
	   Имена категорий совпадают с Category= у UPROPERTY и UFUNCTION в DungeonGenerator.h.

	   Но priority применяется только когда секцию создаёт этот вызов. Секцию с кнопками
	   (Category = "DungeonActions") создаёт редакторский кастомизатор FObjectDetails, который
	   отрабатывает раньше нашего, поэтому порядок задаём явно через SetSortOrder: 0 — самый
	   верх, дальше по одному на секцию (у движковых секций порядок >= 1000). */

	// 1. Кнопки Generate / BuildGeometry / ClearGenerated / ResetRoomTemplates — наверх.
	//    Строки с кнопками создаёт сам редактор (CallInEditor), порядок внутри задаёт
	//    meta = (DisplayPriority) у функций.
	IDetailCategoryBuilder& Actions = DetailBuilder.EditCategory(TEXT("DungeonActions"),
		LOCTEXT("ActionsCategory", "Действия"), ECategoryPriority::Variable);
	Actions.SetSortOrder(0);
	Actions.InitiallyCollapsed(false);
	Actions.SetToolTip(LOCTEXT("ActionsTooltip",
		"Сгенерировать — убрать старое и построить лабиринт заново. Построить геометрию — сетку не менять, перестроить стены, крышу и пол. Очистить — удалить всё построенное без новой генерации."));

	// 2. Типы комнат: компактное меню (мин./макс. размер, форма, количество).
	IDetailCategoryBuilder& RoomTypes = DetailBuilder.EditCategory(TEXT("RoomTypes"),
		LOCTEXT("RoomTypesCategory", "Типы комнат: размер, форма, количество"),
		ECategoryPriority::Important);
	RoomTypes.SetSortOrder(1);
	RoomTypes.InitiallyCollapsed(false);
	RoomTypes.SetToolTip(LOCTEXT("RoomTypesTooltip",
		"Карточка на тип: минимальный и максимальный размер в клетках, форма, количество и где ставить. "
		"Полные поля (узор, цвет) видны ниже, если включить в Details Advanced View."));

	// Кого редактируем: выделенных генераторов может быть несколько — правим все сразу.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	TSharedRef<FPanelData> Data = MakeShared<FPanelData>();
	for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
	{
		if (ADungeonGenerator* Generator = Cast<ADungeonGenerator>(WeakObject.Get()))
		{
			Data->Generators.Add(Generator);
		}
	}

	if (Data->Generators.Num() > 0)
	{
		Data->ShapeOptions = {
			MakeShared<FEnumOption>((int32)ERoomShape::Rect,   LOCTEXT("ShapeRect",   "Прямоугольник")),
			MakeShared<FEnumOption>((int32)ERoomShape::Cross,  LOCTEXT("ShapeCross",  "Крест")),
			MakeShared<FEnumOption>((int32)ERoomShape::LShape, LOCTEXT("ShapeL",      "Г-образная")),
			MakeShared<FEnumOption>((int32)ERoomShape::Circle, LOCTEXT("ShapeCircle", "Круглая")),
		};

		Data->ConditionOptions = {
			MakeShared<FEnumOption>((int32)ERoomCondition::Any,          LOCTEXT("CondAny",  "Где угодно")),
			MakeShared<FEnumOption>((int32)ERoomCondition::Edge,         LOCTEXT("CondEdge", "У края поля")),
			MakeShared<FEnumOption>((int32)ERoomCondition::NearStart,    LOCTEXT("CondNear", "Ближе к старту")),
			MakeShared<FEnumOption>((int32)ERoomCondition::FarFromStart, LOCTEXT("CondFar",  "Дальше от старта")),
			MakeShared<FEnumOption>((int32)ERoomCondition::NextToRoad,   LOCTEXT("CondRoad", "Рядом с дорогой")),
			MakeShared<FEnumOption>((int32)ERoomCondition::NextToRoom,   LOCTEXT("CondRoom", "Рядом с комнатой")),
		};

		// Стандартные строки массива переносим в Advanced: обычно они не мешают, но
		// узор и цвет по-прежнему можно поменять через Advanced View.
		RoomTypes.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADungeonGenerator, StartRoom)), EPropertyLocation::Advanced);
		RoomTypes.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADungeonGenerator, EndRoom)), EPropertyLocation::Advanced);
		RoomTypes.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADungeonGenerator, Templates)), EPropertyLocation::Advanced);

		// Контейнер меню: его содержимое пересобирается при добавлении/удалении типа.
		TSharedRef<SBox> Container = SNew(SBox).HAlign(HAlign_Fill);
		Data->Container = Container;

		const TWeakPtr<FPanelData> WeakData = Data;
		const TWeakPtr<SBox> WeakContainer = Container;
		Data->Rebuild = [WeakData, WeakContainer]()
		{
			const TSharedPtr<FPanelData> PinnedData = WeakData.Pin();
			const TSharedPtr<SBox> PinnedBox = WeakContainer.Pin();
			if (PinnedData.IsValid() && PinnedBox.IsValid())
			{
				PinnedBox->SetContent(MakeMenu(PinnedData.ToSharedRef()));
			}
		};

		Data->Rebuild();

		RoomTypes.AddCustomRow(LOCTEXT("RoomTypesMenuFilter", "тип комнаты размер форма количество добавить удалить копия"))
			.WholeRowContent()
			.HAlign(HAlign_Fill)
			[
				Container
			];
	}

	// 3. Лимит комнат и ключ генерации.
	IDetailCategoryBuilder& Rooms = DetailBuilder.EditCategory(TEXT("Rooms"),
		LOCTEXT("RoomsCategory", "Комнаты: лимит и ключ"), ECategoryPriority::Important);
	Rooms.SetSortOrder(2);
	Rooms.SetToolTip(LOCTEXT("RoomsTooltip",
		"Максимум комнат — потолок на все типы сразу (старт + финиш + количество всех типов). Ключ = 0 значит каждый раз заново."));

	// 4. Размер поля и цена клетки.
	IDetailCategoryBuilder& Field = DetailBuilder.EditCategory(TEXT("Field"),
		LOCTEXT("FieldCategory", "Поле и сетка"), ECategoryPriority::Important);
	Field.SetSortOrder(3);
	Field.SetToolTip(LOCTEXT("FieldTooltip",
		"Размеры всех комнат — в клетках сетки; сколько сантиметров в одной клетке — «Размер клетки»."));

	// 5. Дороги.
	DetailBuilder.EditCategory(TEXT("Roads"),
		LOCTEXT("RoadsCategory", "Дороги"), ECategoryPriority::Important).SetSortOrder(4);

	// 6. Стены / крыша / пол.
	DetailBuilder.EditCategory(TEXT("Geometry"),
		LOCTEXT("GeometryCategory", "Геометрия: стены, крыша, пол"), ECategoryPriority::Important).SetSortOrder(5);

	// 7. Результат и компоненты — вниз, чтобы не мешались при настройке.
	DetailBuilder.EditCategory(TEXT("Dungeon"),
		LOCTEXT("DungeonCategory", "Результат и компоненты"), ECategoryPriority::Uncommon).SetSortOrder(6);
}

#undef LOCTEXT_NAMESPACE

#endif
