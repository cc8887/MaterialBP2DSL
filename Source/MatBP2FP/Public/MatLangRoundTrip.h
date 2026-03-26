// MatLangRoundTrip.h - Round-trip Validation for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterial;

/**
 * Round-trip validation: Export → Parse → ToString → Compare
 * Verifies that the DSL pipeline is lossless for material data
 */
class MATBP2FP_API FMatLangRoundTrip
{
public:
	struct FRoundTripResult
	{
		bool bPassed;
		FString MaterialName;
		int32 TotalLines;
		int32 DiffLines;
		float Fidelity;  // 0.0 - 1.0
		TArray<FString> Diffs;  // Line-by-line differences

		FRoundTripResult()
			: bPassed(false), TotalLines(0), DiffLines(0), Fidelity(0.0f) {}
	};
	
	/**
	 * Run round-trip validation on a material
	 * Export → Parse → ToString → line-by-line compare
	 */
	static FRoundTripResult Validate(UMaterial* Material);
	
	/**
	 * Run round-trip on a DSL string (Parse → ToString → compare)
	 */
	static FRoundTripResult ValidateString(const FString& DSLSource, const FString& Name = TEXT(""));
	
private:
	static TArray<FString> NormalizeAndSplit(const FString& Text);
};
