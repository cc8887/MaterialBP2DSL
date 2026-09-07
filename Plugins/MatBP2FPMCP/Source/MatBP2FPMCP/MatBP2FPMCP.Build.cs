using UnrealBuildTool;

public class MatBP2FPMCP : ModuleRules
{
	public MatBP2FPMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bWithMcp = Target.Version.MajorVersion > 5
			|| (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8);

		// This module is intentionally optional. The plugin is only enabled for
		// UE 5.8+ where ToolsetRegistry and ModelContextProtocol exist.
		if (!bWithMcp)
		{
			throw new BuildException("MatBP2FPMCP requires Unreal Engine 5.8 or newer.");
		}

		PublicDefinitions.Add("MATBP2FP_WITH_MCP=1");
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MatBP2FPEditor",
			"ToolsetRegistry"
		});
	}
}
