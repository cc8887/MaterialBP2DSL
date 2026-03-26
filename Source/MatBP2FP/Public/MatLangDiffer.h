// MatLangDiffer.h - AST Diff Engine for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.
//
// Compares two FMaterialGraphAST trees (Old vs New) and produces a list
// of diff entries.  Material graphs are DAGs with explicit $id references,
// so expression matching is trivially by $id (stable across exports).
//
// Diff dimensions:
//   1. Material-level properties (domain, blend-mode, shading-model, two-sided, ...)
//   2. Expression list changes  (added / removed / type-changed)
//   3. Expression property changes  (value of :key changed)
//   4. Expression input/connection changes
//   5. Material output slot changes

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"

// ---- Diff Operation Types ----

enum class EMatLangDiffOp : uint8
{
	// Material-level properties
	MaterialPropertyChanged,      // :domain, :blend-mode, :shading-model, :two-sided, etc.

	// Expression list
	ExpressionAdded,              // New $id not present in old
	ExpressionRemoved,            // Old $id not present in new
	ExpressionTypeChanged,        // Same $id but different expression type (structural)

	// Expression properties
	ExprPropertyChanged,          // Same $id, property value changed
	ExprPropertyAdded,            // Same $id, new property key
	ExprPropertyRemoved,          // Same $id, old property key removed

	// Expression inputs/connections
	ExprInputChanged,             // Same $id, same input name, connection/value changed
	ExprInputAdded,               // Same $id, new input name
	ExprInputRemoved,             // Same $id, old input name removed

	// Material output slots
	OutputChanged,                // Same output slot, different connection/value
	OutputAdded,                  // New output slot
	OutputRemoved,                // Old output slot removed
};

/**
 * Severity of a diff entry
 *   0 = Cosmetic  — no functional change (e.g. editor position, comment)
 *   1 = Property  — property/input value change, patchable in-place
 *   2 = Structural — expression added/removed/type-changed, requires rebuild
 */
enum class EMatLangDiffSeverity : uint8
{
	Cosmetic   = 0,
	Property   = 1,
	Structural = 2,
};

/**
 * Single diff entry describing one change between old and new AST
 */
struct MATBP2FP_API FMatLangDiffEntry
{
	EMatLangDiffOp Op;
	EMatLangDiffSeverity Severity;

	FString ExprId;          // Which expression ($id), empty for material-level/output diffs
	FString Key;             // Property/input/output name
	FString OldValue;        // Previous value (empty for Add ops)
	FString NewValue;        // New value (empty for Remove ops)

	FString ToString() const;
};

/**
 * Result of a diff operation
 */
struct MATBP2FP_API FMatLangDiffResult
{
	TArray<FMatLangDiffEntry> Entries;

	int32 NumCosmetic;
	int32 NumProperty;
	int32 NumStructural;

	FMatLangDiffResult() : NumCosmetic(0), NumProperty(0), NumStructural(0) {}

	bool HasStructuralChanges() const { return NumStructural > 0; }
	bool HasPropertyChanges() const { return NumProperty > 0; }
	bool IsEmpty() const { return Entries.Num() == 0; }
	int32 TotalChanges() const { return Entries.Num(); }

	FString GetSummary() const;
};

/**
 * Differ engine — compares two FMaterialGraphAST
 */
class MATBP2FP_API FMatLangDiffer
{
public:
	/**
	 * Compute diff between two ASTs
	 * @param OldAST  Current material state
	 * @param NewAST  Desired material state
	 * @return Diff result with all entries
	 */
	static FMatLangDiffResult Diff(
		const TSharedPtr<FMaterialGraphAST>& OldAST,
		const TSharedPtr<FMaterialGraphAST>& NewAST);

private:
	FMatLangDiffResult Result;

	void DiffMaterialProperties(const FMaterialGraphAST& Old, const FMaterialGraphAST& New);
	void DiffExpressions(const FMaterialGraphAST& Old, const FMaterialGraphAST& New);
	void DiffExpressionProperties(const FString& Id, const FMatExpressionAST& Old, const FMatExpressionAST& New);
	void DiffExpressionInputs(const FString& Id, const FMatExpressionAST& Old, const FMatExpressionAST& New);
	void DiffOutputs(const FMatOutputsAST& Old, const FMatOutputsAST& New);

	void AddEntry(EMatLangDiffOp Op, EMatLangDiffSeverity Severity,
		const FString& ExprId, const FString& Key,
		const FString& OldValue, const FString& NewValue);

	// Serialize an FMatLangInput to a comparable string
	static FString InputToString(const FMatLangInput& Input);
};
