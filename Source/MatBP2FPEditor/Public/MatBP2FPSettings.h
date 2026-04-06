// MatBP2FPSettings.h - Project Settings
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MatBP2FPSettings.generated.h"

/**
 * Auto-sync mode for MatBP2FP
 */
UENUM()
enum class EMatBPSyncMode : uint8
{
	/** No automatic sync */
	None,
	/** Material compile -> auto export to .matlang */
	Mat2FP,
	/** .matlang file change -> auto import to Material (not yet implemented) */
	FP2Mat
};

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
	
	// ========== Auto Sync Settings ==========
	
	/** Auto-sync direction: None disables sync, Mat2FP exports on compile, FP2Mat imports on file change */
	UPROPERTY(Config, EditAnywhere, Category="Auto Sync",
		meta=(DisplayName="Auto Sync Mode"))
	EMatBPSyncMode AutoSyncMode;
	
	// ========== Export Options ==========
	
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
