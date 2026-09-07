using UnrealBuildTool;

public class MatBP2FPMCP : ModuleRules
{
    public MatBP2FPMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "MatBP2FPEditor"
        });

        // ToolsetRegistry is only available in the UE 5.8+ engine MCP stack.
        bool bWithUE58MCP = Target.Version.MajorVersion > 5 ||
            (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8);
        if (bWithUE58MCP)
        {
            PublicDependencyModuleNames.Add("ToolsetRegistry");
        }
    }
}
