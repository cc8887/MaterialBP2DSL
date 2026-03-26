// MatBP2FPSettings.h - Project Settings
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MatBP2FPSettings.generated.h"

/**
 * MatBP2FP Project Settings
 * Location: Edit → Project Settings → Plugins → MatBP2FP
 */
UCLASS(config=Editor, defaultconfig, meta=(DisplayName="MatBP2FP"))
class MATBP2FPEDITOR_API UMatBP2FPSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UMatBP2FPSettings();
	
	// ========== Export Options ==========
	
	/** Output directory for exported .matlang files (relative to project root) */
	UPROPERTY(Config, EditAnywhere, Category="Export Options",
		meta=(DisplayName="Export Output Path"))
	FString ExportOutputPath;
	
	/** Include expression editor positions in DSL output */
	UPROPERTY(Config, EditAnywhere, Category="Export Options",
		meta=(DisplayName="Include Editor Positions"))
	bool bIncludeEditorPositions;
	
	/** Include expression comments/descriptions */
	UPROPERTY(Config, EditAnywhere, Category="Export Options",
		meta=(DisplayName="Include Comments"))
	bool bIncludeComments;
	
	// ========== Import Options ==========
	
	/** Auto-compile material after import */
	UPROPERTY(Config, EditAnywhere, Category="Import Options",
		meta=(DisplayName="Auto Compile After Import"))
	bool bAutoCompileAfterImport;
	
	// ========== UDeveloperSettings Interface ==========
	
	virtual FName GetCategoryName() const override
	{
		return TEXT("Plugins");
	}
	
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("MatBP2FPSettings", "Section", "MatBP2FP");
	}
};
