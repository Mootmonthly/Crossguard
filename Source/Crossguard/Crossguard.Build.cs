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

		// Art hooks (Combat/CrossguardCosmetics): trails, impacts and hit sparks are Niagara systems.
		PrivateDependencyModuleNames.Add("Niagara");

		// Nakama REST client (Game/CrossguardNakamaClient, docs/backend-setup.md): HTTP requests, JSON answers.
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTP", "Json" });

		// UGameMapsSettings (the HUD's F10 back to the main menu: GameDefaultMap).
		PrivateDependencyModuleNames.Add("EngineSettings");

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
