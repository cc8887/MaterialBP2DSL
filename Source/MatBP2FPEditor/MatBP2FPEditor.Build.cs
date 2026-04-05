// MatBP2FPEditor.Build.cs - Editor Module Build Configuration
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

using UnrealBuildTool;

public class MatBP2FPEditor : ModuleRules
{
	public MatBP2FPEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});
		
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"MatBP2FP",            // Runtime module
			"UnrealEd",            // Editor framework (UEditorLoadingAndSavingUtils)
			"MaterialEditor",      // Material editor graph
			"RenderCore",          // Render core types
			"RHI",                 // RHI types
			"Slate",               // UI framework
			"SlateCore",
			"EditorStyle",
			"ToolMenus",           // Editor menus
			"DeveloperSettings",   // Project settings
			"AssetRegistry"        // Asset discovery
		});
	}
}
