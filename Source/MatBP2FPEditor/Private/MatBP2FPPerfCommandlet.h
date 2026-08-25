// MatBP2FPPerfCommandlet.h - CI entry point for compile-free MatLang cost analysis
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MatBP2FPPerfCommandlet.generated.h"

/**
 * Analyze one MatLang file or every .matlang file below a directory.
 * Usage: -run=MatBP2FPPerf -path=<file-or-directory> [-output=<json-file>] [budget options]
 * Exit codes: 0 = report generated and policy passed, 1 = invalid DSL or policy failure, 2 = usage/I/O error.
 */
UCLASS()
class UMatBP2FPPerfCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMatBP2FPPerfCommandlet();
	virtual int32 Main(const FString& Params) override;
};
