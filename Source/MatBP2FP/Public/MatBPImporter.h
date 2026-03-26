// MatBPImporter.h - DSL to Material Blueprint Importer
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"

class UMaterial;
class UMaterialExpression;
class UMaterialExpressionComment;

/**
 * Imports a MatLang DSL into a UMaterial
 * Creates material expressions and wires them together
 */
class MATBP2FP_API FMatBPImporter
{
public:
	/** Import result */
	struct FImportResult
	{
		bool bSuccess;
		UMaterial* Material;
		int32 ExpressionsCreated;
		int32 ConnectionsMade;
		int32 Warnings;
		TArray<FString> Messages;
	};

	/** Detailed update result — includes incremental vs full-rebuild info */
	struct FUpdateResult
	{
		bool bSuccess;
		bool bUsedIncrementalPatch;  // true if patched in-place, false if full rebuild
		int32 NumChanges;            // Total diff entries
		int32 NumStructuralChanges;  // Structural diff entries
		int32 NumPropertyChanges;    // Property diff entries
		int32 NumApplied;            // Successfully applied patches
		int32 NumFailed;             // Failed patches
		TArray<FString> Messages;

		FUpdateResult()
			: bSuccess(false), bUsedIncrementalPatch(false)
			, NumChanges(0), NumStructuralChanges(0), NumPropertyChanges(0)
			, NumApplied(0), NumFailed(0) {}
	};
	
	/**
	 * Import DSL string into a new material
	 * @param DSLSource  The MatLang source
	 * @param PackagePath  Package path for the new material (e.g. "/Game/Materials/")
	 * @return Import result
	 */
	static FImportResult ImportFromString(const FString& DSLSource, const FString& PackagePath);
	
	/**
	 * Import AST into a new material
	 */
	static FImportResult ImportFromAST(TSharedPtr<FMaterialGraphAST> AST, const FString& PackagePath);
	
	/**
	 * Update an existing material from DSL (full rebuild — legacy API)
	 * Clears existing expressions and rebuilds
	 */
	static FImportResult UpdateMaterial(UMaterial* ExistingMaterial, const FString& DSLSource);
	
	/**
	 * Update an existing material from AST (full rebuild — legacy API)
	 */
	static FImportResult UpdateMaterialFromAST(UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> AST);

	/**
	 * Smart update: dual-strategy (incremental patch vs full rebuild)
	 *   1. Export current material → OldAST
	 *   2. Parse new DSL → NewAST
	 *   3. Diff OldAST vs NewAST
	 *   4. If only property changes → apply incremental patch
	 *   5. If structural changes or patch fails → fall back to full rebuild
	 *   6. Recompile material
	 *
	 * @param ExistingMaterial  The material to update
	 * @param NewDSL           New DSL source
	 * @return Detailed update result
	 */
	static FUpdateResult UpdateMaterialDetailed(UMaterial* ExistingMaterial, const FString& NewDSL);

	/**
	 * Smart update from AST
	 */
	static FUpdateResult UpdateMaterialDetailedFromAST(UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> NewAST);

#if WITH_EDITOR
private:
	UMaterial* Material;
	TSharedPtr<FMaterialGraphAST> AST;
	TMap<FString, UMaterialExpression*> IdToExpr;  // $id -> created expression
	FImportResult Result;
	
	FMatBPImporter(UMaterial* InMaterial, TSharedPtr<FMaterialGraphAST> InAST);
	
	FImportResult Import();
	
	// Create UMaterial and set properties
	static UMaterial* CreateMaterial(const FString& Name, const FString& PackagePath);
	void SetMaterialProperties();
	
	// Create all expression nodes
	void CreateExpressions();
	UMaterialExpression* CreateExpression(TSharedPtr<FMatExpressionAST> ExprAST);
	
	// Wire all connections
	void WireConnections();
	void WireExpressionInputs(TSharedPtr<FMatExpressionAST> ExprAST, UMaterialExpression* Expr);
	void WireMaterialOutputs();
	
	// Set expression-specific properties
	void SetExpressionProperties(TSharedPtr<FMatExpressionAST> ExprAST, UMaterialExpression* Expr);
	
	// Find UClass for expression type
	static UClass* FindExpressionClass(const FString& ExprType);
	static FString KebabToCamel(const FString& KebabCase);
	
	// Map DSL enum values back to UE enums
	static EMaterialDomain MapDomainToUE(EMatLangDomain Domain);
	static EBlendMode MapBlendModeToUE(EMatLangBlendMode Mode);
	static EMaterialShadingModel MapShadingModelToUE(EMatLangShadingModel Model);
	
	// Clear existing material expressions
	void ClearMaterial();
	
	// Helpers
	void Warn(const FString& Message);
	void Info(const FString& Message);
	
	// Parse asset path from (asset "path") string
	static FString ExtractAssetPath(const FString& Value);
#endif
};
