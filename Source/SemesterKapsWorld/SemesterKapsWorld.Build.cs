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
			"Slate"
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

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
