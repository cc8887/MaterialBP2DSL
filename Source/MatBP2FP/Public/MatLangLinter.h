// MatLangLinter.h - Core syntax and graph validation for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatLangParser.h"

struct MATBP2FP_API FMatLangLintResult
{
	TSharedPtr<FMaterialGraphAST> AST;
	TArray<FMatLangDiagnostic> Diagnostics;

	bool HasErrors() const;
	int32 Count(EMatLangDiagnosticSeverity Severity) const;
};

class MATBP2FP_API FMatLangLinter
{
public:
	static FMatLangLintResult Lint(const FString& Source, const FString& FilePath = TEXT(""));
	static FMatLangLintResult LintAST(
		const TSharedPtr<FMaterialGraphAST>& AST,
		const FString& FilePath = TEXT(""));
};
