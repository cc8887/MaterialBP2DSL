// MatLangStaticCostAnalyzer.h - Compile-free performance analysis for MatLang graphs
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"

/** A metric evaluated for a low-cost branch selection, the declared defaults, and all branches. */
struct MATBP2FP_API FMatLangStaticMetricRange
{
	int32 Low = 0;
	int32 Default = 0;
	int32 Conservative = 0;
};

/** Static metrics for one shader stage, or for the whole expanded material graph. */
struct MATBP2FP_API FMatLangStaticStageCost
{
	FMatLangStaticMetricRange ReachableNodes;
	FMatLangStaticMetricRange TextureSampleSites;
	FMatLangStaticMetricRange ALUProxy;
	int32 DefaultMaxDepth = 0;
};

enum class EMatLangStaticRiskSeverity : uint8
{
	Info,
	Warning,
	Unknown
};

/** An actionable static-analysis finding tied back to a DSL expression when possible. */
struct MATBP2FP_API FMatLangStaticCostRisk
{
	FString RuleId;
	EMatLangStaticRiskSeverity Severity = EMatLangStaticRiskSeverity::Warning;
	FString Message;
	FString ExpressionId;
	FMatLangSourceSpan SourceSpan;
};

/** Complete compile-free cost report for one material or material function. */
struct MATBP2FP_API FMatLangStaticCostReport
{
	FString Name;
	FString AssetPath;
	EMatLangGraphKind Kind = EMatLangGraphKind::Material;

	int32 DeclaredNodes = 0;
	int32 DeclaredEdges = 0;
	int32 DeadNodes = 0;
	int32 MaxFanOut = 0;

	FMatLangStaticStageCost Overall;
	FMatLangStaticStageCost Vertex;
	FMatLangStaticStageCost Pixel;

	int32 DefaultFunctionCallSites = 0;
	int32 ConservativeFunctionCallSites = 0;
	int32 UnresolvedFunctionCalls = 0;
	int32 RecursiveFunctionCalls = 0;

	TArray<FString> DefaultTextureAssets;
	TArray<FString> ConservativeTextureAssets;
	TArray<FString> StaticParameters;
	uint64 TheoreticalStaticCombinations = 1;
	bool bStaticCombinationCountCapped = false;
	bool bHasUnknownCost = false;

	TArray<FMatLangStaticCostRisk> Risks;
};

struct MATBP2FP_API FMatLangStaticCostOptions
{
	bool bExpandMaterialFunctions = true;
	int32 MaxFunctionDepth = 32;
};

/**
 * Analyzes a MatLang DAG without loading assets or compiling shaders.
 * ALUProxy is an intentionally platform-neutral relative weight, not a shader instruction count.
 */
class MATBP2FP_API FMatLangStaticCostAnalyzer
{
public:
	using FFunctionResolver = TFunction<TSharedPtr<FMaterialGraphAST>(const FString& AssetPath)>;

	static FMatLangStaticCostReport Analyze(
		const TSharedPtr<FMaterialGraphAST>& AST,
		const FFunctionResolver& FunctionResolver = FFunctionResolver(),
		const FMatLangStaticCostOptions& Options = FMatLangStaticCostOptions());
};
