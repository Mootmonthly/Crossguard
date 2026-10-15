using UnrealBuildTool;

public class CrossguardTarget : TargetRules
{
	public CrossguardTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Crossguard");
	}
}
