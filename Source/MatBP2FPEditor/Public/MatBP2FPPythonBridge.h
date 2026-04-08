// MatBP2FPPythonBridge.h - Python-facing editor bridge for MatBP2FP
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MatBP2FPPythonBridge.generated.h"

/**
 * Structured result returned to Unreal Python / Blueprint callers.
 */
USTRUCT(BlueprintType)
struct MATBP2FPEDITOR_API FMatBP2FPPythonResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	FString FilePath;

	/** The MatLang DSL text (populated by Export operations). */
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	FString DSLText;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	bool bUsedIncrementalPatch = false;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	bool bSavedPackage = false;

	/** Total diff entries applied during an Update operation. */
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	int32 NumChanges = 0;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	int32 NumStructuralChanges = 0;

	/** Number of expressions created (Import operations). */
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	int32 NumExpressionsCreated = 0;

	/** Number of connections wired (Import operations). */
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	int32 NumConnectionsMade = 0;

	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP")
	TArray<FString> Warnings;
};

/**
 * Editor-only bridge exposed to Unreal Python so AI agents can read/write Materials
 * through MatLang DSL without shelling out to commandlets.
 *
 * Python usage:
 *   import unreal
 *   result = unreal.MatBP2FPPythonBridge.export_material_to_text("/Game/Mat/M_Example.M_Example")
 *   print(result.dsl_text)
 */
UCLASS()
class MATBP2FPEDITOR_API UMatBP2FPPythonBridge : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Export a UMaterial asset to MatLang DSL text.
	 * MaterialPath accepts package path or object path, e.g.
	 *   /Game/Materials/M_Skin
	 *   /Game/Materials/M_Skin.M_Skin
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult ExportMaterialToText(const FString& MaterialPath);

	/**
	 * Export a UMaterial asset to a .matlang file.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult ExportMaterialToFile(const FString& MaterialPath, const FString& OutputFilePath);

	/**
	 * Export a UMaterial asset to a .matlang file, including all referenced
	 * Material Functions (recursively). Each function is exported to its own
	 * .matlang file at {OutputDir}/{/Game/ relative path}.matlang.
	 *
	 * Returns the main material result; the full list of exported files
	 * (material + all functions) is returned in Message.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult ExportMaterialWithDependenciesToFile(const FString& MaterialPath, const FString& OutputDir);

	/**
	 * Import MatLang DSL text as a new UMaterial asset.
	 * DestinationFolder should be a content folder such as /Game/Materials/Imported.
	 * The created asset name comes from the :name field in the DSL.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult ImportMaterialFromText(const FString& DSLText, const FString& DestinationFolder, bool bSavePackage = true);

	/**
	 * Import a .matlang file as a new UMaterial asset.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult ImportMaterialFromFile(const FString& InputFilePath, const FString& DestinationFolder, bool bSavePackage = true);

	/**
	 * Smart-update an existing UMaterial from MatLang DSL text.
	 * Uses incremental patch when possible; falls back to full rebuild on structural changes.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult UpdateMaterialFromText(const FString& MaterialPath, const FString& DSLText, bool bSavePackage = true);

	/**
	 * Smart-update an existing UMaterial from a .matlang file.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult UpdateMaterialFromFile(const FString& MaterialPath, const FString& InputFilePath, bool bSavePackage = true);

	/**
	 * Run round-trip validation on a single material:
	 *   Export -> Parse -> ToString -> diff
	 * Returns fidelity percentage and any diff lines in Warnings.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult ValidateMaterialRoundTrip(const FString& MaterialPath);

	// ========== Mapping Registry ==========

	/**
	 * Query the Material <-> DSL mapping table.
	 * Returns all entries as a JSON string: array of objects with keys:
	 *   material_path, dsl_file_path, state, has_material, has_dsl
	 * State values: "Synced", "MatOnly", "DSLOnly", "OutOfSync"
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult GetMappingTable();

	/**
	 * Look up a single mapping entry by Material path.
	 * Returns the entry fields in the result struct.
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult FindMappingByMaterial(const FString& MaterialPath);

	/**
	 * Convert a Material package path to its corresponding DSL file path.
	 * Pure path conversion, no registry lookup needed.
	 *   /Game/Props/M_Wood -> {Project}/Saved/BP2DSL/MatBP/Props/M_Wood.matlang
	 */
	UFUNCTION(BlueprintCallable, Category="MatBP2FP|Python")
	static FMatBP2FPPythonResult MaterialPathToDSLPath(const FString& MaterialPath);
};
