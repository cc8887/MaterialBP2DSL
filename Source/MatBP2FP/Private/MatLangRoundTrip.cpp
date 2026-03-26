// MatLangRoundTrip.cpp - Round-trip Validation
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangRoundTrip.h"
#include "MatBPExporter.h"
#include "MatLangParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatLangRoundTrip, Log, All);

FMatLangRoundTrip::FRoundTripResult FMatLangRoundTrip::Validate(UMaterial* Material)
{
	FRoundTripResult Result;
	Result.bPassed = false;
	Result.MaterialName = Material ? Material->GetName() : TEXT("null");
	Result.TotalLines = 0;
	Result.DiffLines = 0;
	Result.Fidelity = 0.0f;
	
	if (!Material)
	{
		Result.Diffs.Add(TEXT("Null material"));
		return Result;
	}
	
	// Step 1: Export to DSL
	FString ExportedDSL = FMatBPExporter::ExportToString(Material);
	if (ExportedDSL.IsEmpty())
	{
		Result.Diffs.Add(TEXT("Export produced empty result"));
		return Result;
	}
	
	// Step 2: Parse back to AST
	return ValidateString(ExportedDSL, Material->GetName());
}

FMatLangRoundTrip::FRoundTripResult FMatLangRoundTrip::ValidateString(const FString& DSLSource, const FString& Name)
{
	FRoundTripResult Result;
	Result.bPassed = false;
	Result.MaterialName = Name;
	Result.TotalLines = 0;
	Result.DiffLines = 0;
	Result.Fidelity = 0.0f;
	
	// Parse
	TArray<FMatLangParseError> ParseErrors;
	auto AST = FMatLangParser::Parse(DSLSource, ParseErrors);
	
	if (!AST)
	{
		Result.Diffs.Add(TEXT("Parse failed"));
		for (const auto& Err : ParseErrors)
		{
			Result.Diffs.Add(Err.ToString());
		}
		return Result;
	}
	
	// ToString
	FString ReconstructedDSL = AST->ToString();
	
	// Compare line-by-line
	TArray<FString> OrigLines = NormalizeAndSplit(DSLSource);
	TArray<FString> ReconLines = NormalizeAndSplit(ReconstructedDSL);
	
	Result.TotalLines = FMath::Max(OrigLines.Num(), ReconLines.Num());
	
	int32 MaxLines = FMath::Max(OrigLines.Num(), ReconLines.Num());
	for (int32 i = 0; i < MaxLines; ++i)
	{
		FString Orig = (i < OrigLines.Num()) ? OrigLines[i] : TEXT("<missing>");
		FString Recon = (i < ReconLines.Num()) ? ReconLines[i] : TEXT("<missing>");
		
		if (Orig != Recon)
		{
			Result.DiffLines++;
			Result.Diffs.Add(FString::Printf(TEXT("Line %d:\n  - %s\n  + %s"), i + 1, *Orig, *Recon));
		}
	}
	
	Result.Fidelity = (Result.TotalLines > 0) ? 
		(float)(Result.TotalLines - Result.DiffLines) / (float)Result.TotalLines : 1.0f;
	Result.bPassed = (Result.DiffLines == 0);
	
	return Result;
}

TArray<FString> FMatLangRoundTrip::NormalizeAndSplit(const FString& Text)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	
	// Trim trailing whitespace, skip empty lines
	TArray<FString> Result;
	for (FString& Line : Lines)
	{
		Line.TrimEndInline();
		if (!Line.IsEmpty())
		{
			Result.Add(MoveTemp(Line));
		}
	}
	return Result;
}
