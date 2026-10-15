using UnrealBuildTool;

// Engine-free game rules. Only CrossguardRulesModule.cpp touches Unreal headers;
// everything else must stay plain C++ so Tests/CrossguardRules can build it without the engine.
public class CrossguardRules : ModuleRules
{
	public CrossguardRules(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
