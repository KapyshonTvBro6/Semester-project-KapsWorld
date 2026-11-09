// Copyright Epic Games, Inc. All Rights Reserved.

#include "SemesterKapsWorld.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Dangen/DungeonGeneratorDetails.h"
#include "Kitsune/KitsuneRockConsoleCommands.h"
#include "PropertyEditorModule.h"
#endif

#if WITH_EDITOR
// Регистрирует удобный вид Details-панели для ADungeonGenerator: кнопки наверху,
// типы комнат отдельной секцией. Работает и для акторов, созданных из
// BP_DungeonGenerator: панель применяет настройки родительского C++-класса.
class FSemesterKapsWorldModule : public FDefaultGameModuleImpl
{
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();

        FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        PropertyModule.RegisterCustomClassLayout(TEXT("DungeonGenerator"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FDungeonGeneratorDetails::MakeInstance));
        PropertyModule.NotifyCustomizationModuleChanged();

        // Генератор материалов камня: команды Kitsune.Rock.*.
        // Отдельный инструмент: к ADungeonGenerator обращений нет.
        RegisterKitsuneRockConsoleCommands();
    }

    virtual void ShutdownModule() override
    {
        if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
        {
            PropertyModule->UnregisterCustomClassLayout(TEXT("DungeonGenerator"));
        }

        FDefaultGameModuleImpl::ShutdownModule();
    }
};
#else
class FSemesterKapsWorldModule : public FDefaultGameModuleImpl
{
};
#endif

IMPLEMENT_PRIMARY_GAME_MODULE( FSemesterKapsWorldModule, SemesterKapsWorld, "SemesterKapsWorld" );

DEFINE_LOG_CATEGORY(LogSemesterKapsWorld)
 