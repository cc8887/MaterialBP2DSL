// MatLangDiagnostic.cpp - Shared diagnostics for MatLang parsing and linting
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangDiagnostic.h"

FString FMatLangDiagnostic::ToString() const
{
	const TCHAR* SeverityText = TEXT("info");
	switch (Severity)
	{
		case EMatLangDiagnosticSeverity::Error: SeverityText = TEXT("error"); break;
		case EMatLangDiagnosticSeverity::Warning: SeverityText = TEXT("warning"); break;
		default: break;
	}

	const FString Location = FilePath.IsEmpty()
		? FString::Printf(TEXT("%d:%d"), Span.StartLine, Span.StartColumn)
		: FString::Printf(TEXT("%s(%d,%d)"), *FilePath, Span.StartLine, Span.StartColumn);

	return FString::Printf(TEXT("%s: %s %s: %s"),
		*Location, SeverityText, *RuleId, *Message);
}
