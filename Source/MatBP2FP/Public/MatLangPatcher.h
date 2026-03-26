// MatLangPatcher.h - Incremental Patch System for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.
//
// Applies diff entries to a live UMaterial without full rebuild.
// Only property-level changes (severity <= Property) can be patched.
// Structural changes (expression add/remove/type-change) require full rebuild.
//
// Pipeline:  Export(OldAST) → Parse(NewDSL) → Diff → Apply → Recompile

#pragma once

#include "CoreMinimal.h"
#include "MatLangDiffer.h"

class UMaterial;
class UMaterialExpression;

/**
 * Result of a patch application
 */
struct MATBP2FP_API FMatLangPatchResult
{
	bool bSuccess;
	int32 NumApplied;
	int32 NumSkipped;
	int32 NumFailed;
	TArray<FString> Messages;

	FMatLangPatchResult() : bSuccess(false), NumApplied(0), NumSkipped(0), NumFailed(0) {}
};

/**
 * Patcher — applies property-level diff entries to a live UMaterial
 */
class MATBP2FP_API FMatLangPatcher
{
public:
	/**
	 * Apply diff entries to material.
	 * Only processes severity <= Property. Structural changes are skipped/flagged.
	 * @param Material     Target UMaterial to patch
	 * @param DiffResult   Diff entries from FMatLangDiffer::Diff()
	 * @param NewAST       New AST (needed for full expression data on property sets)
	 * @return Patch result
	 */
	static FMatLangPatchResult Apply(
		UMaterial* Material,
		const FMatLangDiffResult& DiffResult,
		const TSharedPtr<FMaterialGraphAST>& NewAST);

	/**
	 * Full incremental update pipeline:
	 *   1. Export current material to OldAST
	 *   2. Parse new DSL to NewAST
	 *   3. Diff OldAST vs NewAST
	 *   4. If no structural changes → Apply patch
	 *   5. Recompile material
	 *
	 * @param Material    Target material
	 * @param NewDSL      New DSL source
	 * @param OutDiffResult  Optional: receives the diff result
	 * @return Patch result (bSuccess=false if structural changes detected)
	 */
	static FMatLangPatchResult IncrementalUpdate(
		UMaterial* Material,
		const FString& NewDSL,
		FMatLangDiffResult* OutDiffResult = nullptr);

#if WITH_EDITOR
private:
	UMaterial* Material;
	TSharedPtr<FMaterialGraphAST> NewAST;
	TMap<FString, UMaterialExpression*> IdToExpr;  // $id -> UMaterialExpression*
	FMatLangPatchResult Result;

	FMatLangPatcher(UMaterial* InMaterial, const TSharedPtr<FMaterialGraphAST>& InNewAST);

	// Build the IdToExpr map from current material expressions
	void BuildExpressionMap();

	// Apply a single diff entry
	void ApplyEntry(const FMatLangDiffEntry& Entry);

	// Apply material-level property change
	void ApplyMaterialProperty(const FMatLangDiffEntry& Entry);

	// Apply expression property change
	void ApplyExpressionProperty(const FMatLangDiffEntry& Entry);

	// Apply expression input/connection change
	void ApplyExpressionInput(const FMatLangDiffEntry& Entry);

	// Apply material output slot change
	void ApplyOutputChange(const FMatLangDiffEntry& Entry);

	// Find a UMaterialExpression by $id (using Desc or ExprToId map)
	UMaterialExpression* FindExpressionById(const FString& Id);

	// Set a specific property on a UMaterialExpression from AST data
	void SetExpressionPropertyFromAST(UMaterialExpression* Expr, const FString& Key, const FString& Value);

	// Wire an expression input from AST connection data
	void WireExpressionInput(UMaterialExpression* Expr, const FString& InputName, const FMatLangInput& InputData);

	// Wire a material output slot
	void WireOutputSlot(const FString& SlotName, const FMatLangInput& InputData);

	// Recompile the material after patching
	void RecompileMaterial();

	// Helpers
	static FString KebabToCamel(const FString& KebabCase);
	void Warn(const FString& Message);
	void Info(const FString& Message);
#endif
};
