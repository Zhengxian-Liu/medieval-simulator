using UnrealBuildTool;

public class MedievalSimEditorTarget : TargetRules
{
	public MedievalSimEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MedievalSim");
	}
}
