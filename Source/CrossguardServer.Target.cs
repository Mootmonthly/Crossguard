using UnrealBuildTool;

// Dedicated server. Building this target requires a source build of the engine;
// the Epic Games Launcher engine cannot build server targets.
public class CrossguardServerTarget : TargetRules
{
	public CrossguardServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Crossguard");
	}
}
