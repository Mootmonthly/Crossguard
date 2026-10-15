using UnrealBuildTool;

public class CrossguardEditorTarget : TargetRules
{
	public CrossguardEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Crossguard");
	}
}
