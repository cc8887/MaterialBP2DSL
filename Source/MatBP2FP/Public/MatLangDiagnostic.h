// MatLangDiagnostic.h - Shared diagnostics for MatLang parsing and linting
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EMatLangDiagnosticSeverity : uint8
{
	Error,
	Warning,
	Info
};

struct MATBP2FP_API FMatLangSourceSpan
{
	int32 StartLine = 0;
	int32 StartColumn = 0;
	int32 EndLine = 0;
	int32 EndColumn = 0;
	int32 StartOffset = INDEX_NONE;
	int32 EndOffset = INDEX_NONE;

	bool IsValid() const
	{
		return StartLine > 0 && StartColumn > 0;
	}
};

struct MATBP2FP_API FMatLangRelatedLocation
{
	FString Message;
	FMatLangSourceSpan Span;
};

struct MATBP2FP_API FMatLangDiagnostic
{
	FString RuleId;
	EMatLangDiagnosticSeverity Severity = EMatLangDiagnosticSeverity::Error;
	FString Message;
	FString FilePath;
	FMatLangSourceSpan Span;
	TArray<FMatLangRelatedLocation> RelatedLocations;

	FString ToString() const;
};
