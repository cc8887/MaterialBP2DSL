// MatBP2FPExportCommandlet.h
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MatBP2FPExportCommandlet.generated.h"

/**
 * Commandlet to export materials to MatLang DSL
 * Usage: -run=MatBP2FPExport [-material=/Game/Path] [-all]
 */
UCLASS()
class UMatBP2FPExportCommandlet : public UCommandlet
{
	GENERATED_BODY()
	
public:
	UMatBP2FPExportCommandlet();
	virtual int32 Main(const FString& Params) override;
};
