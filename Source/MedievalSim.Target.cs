using UnrealBuildTool;

public class MedievalSimTarget : TargetRules
{
	public MedievalSimTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MedievalSim");
	}
}
