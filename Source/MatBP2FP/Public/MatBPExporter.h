// MatBPExporter.h - Material Blueprint to DSL Exporter
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"

class UMaterial;
class UMaterialExpression;
class UMaterialFunction;

/**
 * Exports a UMaterial to MatLang DSL
 * Traverses the material expression graph and builds an AST
 */
class MATBP2FP_API FMatBPExporter
{
public:
	/**
	 * Export a material to DSL string
	 * @param Material The material to export
	 * @return DSL string representation, or empty on failure
	 */
	static FString ExportToString(UMaterial* Material);

	/**
	 * Export a material function to DSL string
	 * @param MaterialFunction The material function to export
	 * @return DSL string representation, or empty on failure
	 */
	static FString ExportMaterialFunctionToString(UMaterialFunction* MaterialFunction);

	/**
	 * Export a material to AST
	 * @param Material The material to export
	 * @return Material AST, or nullptr on failure
	 */
	static TSharedPtr<FMaterialGraphAST> ExportToAST(UMaterial* Material);

	/**
	 * Export a material function to AST
	 * @param MaterialFunction The material function to export
	 * @return Material AST with Kind=MaterialFunction, or nullptr on failure
	 */
	static TSharedPtr<FMaterialGraphAST> ExportMaterialFunctionToAST(UMaterialFunction* MaterialFunction);

#if WITH_EDITOR
private:
	UMaterial* Material;
	TMap<UMaterialExpression*, FString> ExprToId;  // Expression pointer -> $id mapping
	int32 IdCounter;

	FMatBPExporter(UMaterial* InMaterial);

	TSharedPtr<FMaterialGraphAST> Export();

	// MaterialFunction support
	UMaterialFunction* Function;
	FMatBPExporter(UMaterialFunction* InFunction);
	TSharedPtr<FMaterialGraphAST> ExportFunction();
	
	// Get or assign a unique ID for an expression
	FString GetOrAssignId(UMaterialExpression* Expr);
	
	// Convert UE class name to DSL type name (CamelCase -> kebab-case)
	static FString ExprClassToType(UMaterialExpression* Expr);
	static FString CamelToKebab(const FString& CamelCase);
	
	// Export a single expression node
	TSharedPtr<FMatExpressionAST> ExportExpression(UMaterialExpression* Expr);
	
	// Export expression-specific properties
	void ExportExpressionProperties(UMaterialExpression* Expr, TSharedPtr<FMatExpressionAST> Node);
	
	// Export expression inputs as connections
	void ExportExpressionInputs(UMaterialExpression* Expr, TSharedPtr<FMatExpressionAST> Node);
	
	// Export material output connections
	void ExportOutputs(TSharedPtr<FMaterialGraphAST> AST);
	
	// Map UE material property enum values to DSL strings
	static EMatLangDomain MapDomain(int32 UEDomain);
	static EMatLangBlendMode MapBlendMode(int32 UEBlendMode);
	static EMatLangShadingModel MapShadingModel(int32 UEShadingModel);
#endif
};
