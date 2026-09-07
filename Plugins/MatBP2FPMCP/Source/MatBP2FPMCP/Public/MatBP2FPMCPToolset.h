// MatBP2FPMCPToolset.h - UE 5.8 native MCP tool surface.
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatBP2FPPythonBridge.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "MatBP2FPMCPToolset.generated.h"

/**
 * Read-only material and MatLang operations exposed through UE 5.8 MCP.
 * MCP clients should inspect a material before calling an edit tool.
 */
UCLASS(BlueprintType, Hidden)
class MATBP2FPMCP_API UMatBP2FPInspectToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override
	{
		return TEXT("1.0.0");
	}

	/** Export a project UMaterial to MatLang DSL text without modifying assets. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult ExportMaterialToText(const FString& MaterialPath);

	/** Validate the export/parse round trip for a project UMaterial. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult ValidateMaterialRoundTrip(const FString& MaterialPath);

	/** Return the current Material-to-DSL mapping registry. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult GetMappingTable();

	/** Look up one project Material in the Material-to-DSL mapping registry. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult FindMappingByMaterial(const FString& MaterialPath);

	/** Convert a project Material path to its Saved/BP2DSL file path. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult MaterialPathToDSLPath(const FString& MaterialPath);
};

/**
 * Explicitly mutating material operations exposed through UE 5.8 MCP.
 * save_package is a required argument so persistence is always explicit.
 */
UCLASS(BlueprintType, Hidden)
class MATBP2FPMCP_API UMatBP2FPEditToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override
	{
		return TEXT("1.0.0");
	}

	/** Import DSL text as a new project UMaterial. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Edit")
	static FMatBP2FPPythonResult ImportMaterialFromText(
		const FString& DSLText, const FString& DestinationFolder, bool bSavePackage);

	/** Update a project UMaterial from DSL text using incremental patch when possible. */
	UFUNCTION(meta=(AICallable), Category="MatBP2FP|Edit")
	static FMatBP2FPPythonResult UpdateMaterialFromText(
		const FString& MaterialPath, const FString& DSLText, bool bSavePackage);
};
