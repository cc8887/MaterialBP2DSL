// MatBP2FPLintCommandlet.h - CI entry point for MatLang linting
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MatBP2FPLintCommandlet.generated.h"

/**
 * Lint one MatLang file or every .matlang file below a directory.
 * Usage: -run=MatBP2FPLint -path=<file-or-directory> [-fail-on-warning]
 * Exit codes: 0 = pass, 1 = diagnostics exceeded policy, 2 = usage/I/O error.
 */
UCLASS()
class UMatBP2FPLintCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMatBP2FPLintCommandlet();
	virtual int32 Main(const FString& Params) override;
};
