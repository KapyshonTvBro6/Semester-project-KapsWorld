// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SemesterKapsWorld : ModuleRules
{
	public SemesterKapsWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate",

			// ADungeonGenerator: стены из UProceduralMeshComponent
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"SemesterKapsWorld",
			"SemesterKapsWorld/Variant_Strategy",
			"SemesterKapsWorld/Variant_Strategy/UI",
			"SemesterKapsWorld/Variant_TwinStick",
			"SemesterKapsWorld/Variant_TwinStick/AI",
			"SemesterKapsWorld/Variant_TwinStick/Gameplay",
			"SemesterKapsWorld/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Editor-only зависимости: своя Details-панель для ADungeonGenerator
		// (DungeonGeneratorDetails.cpp + регистрация в StartupModule). В игровых
		// сборках этот код выключен, поэтому зависимости добавляются только для редактора.
		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",        // UPropertyEditorModule
				"PropertyEditor",  // IDetailCustomization, IDetailLayoutBuilder
				"SlateCore",

				// Kitsune/KitsuneRockAssets.cpp: FAssetRegistryModule::AssetCreated
				// для созданных текстур, материала и инстансов
				"AssetRegistry"
			});
		}

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
