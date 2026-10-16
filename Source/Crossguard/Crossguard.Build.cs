using UnrealBuildTool;

public class Crossguard : ModuleRules
{
	public Crossguard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// FUniqueNetIdRepl (player ids for team assignment).
			"CoreOnline",
			"AIModule",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"CrossguardRules",
			// Placeholder UI built in C++ (Source/Crossguard/*/UI, docs/ui.md).
			"UMG",
			"Slate",
			"SlateCore",
		});

		// Editor builds only: the scenario tests (Private/Tests/CrossguardScenario.*) create a map,
		// find the game mode / character Blueprints and run Play-In-Editor. That code is compiled only
		// with WITH_EDITOR, so game and server builds do not need these modules.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"AssetRegistry",
			});
		}
	}
}
