using UnrealBuildTool;

public class MedievalSimTarget : TargetRules
{
	public MedievalSimTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MedievalSim");
	}
}
