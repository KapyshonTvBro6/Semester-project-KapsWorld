// DungeonGeneratorDetails.h
// Свой вид панели Details для ADungeonGenerator (работает и для BP_DungeonGenerator,
// потому что панель берёт настройки родительского C++-класса).
//
// Что меняется:
//   * кнопки Generate / BuildGeometry / ClearGenerated / ResetRoomTemplates собраны
//     в одну секцию «Действия» в самом верху панели;
//   * типы комнат — секцией ниже, удобным меню: карточка на тип (название, мин. и
//     макс. размер, форма, количество, где ставить) + кнопки «+ Добавить тип»,
//     «Копия», «Удалить», «Сбросить к встроенным» и живая сводка по лимиту комнат;
//   * обычные строки массива (узор, цвет) перенесены в Advanced View;
//   * остальные настройки идут понятными группами: лимит комнат, сетка, дороги,
//     геометрия, а результат и компоненты уходят вниз.
//
// Файл целиком редакторский: в игровых сборках он не участвует.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

class FDungeonGeneratorDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

#endif
