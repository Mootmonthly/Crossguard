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
		});
	}
}
