// MatBP2FPImportCommandlet.h
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MatBP2FPImportCommandlet.generated.h"

/**
 * Commandlet to import MatLang DSL into materials
 * Usage: -run=MatBP2FPImport [-test] [-update] [-file=path.matlang]
 *   -test: import → export → compare (round-trip test)
 *   -update: update existing materials instead of creating new ones
 *   -file: import a specific .matlang file
 */
UCLASS()
class UMatBP2FPImportCommandlet : public UCommandlet
{
	GENERATED_BODY()
	
public:
	UMatBP2FPImportCommandlet();
	virtual int32 Main(const FString& Params) override;
	
private:
	bool bTestMode;
	bool bUpdateMode;
	FString SpecificFile;
	
	void ImportFile(const FString& FilePath, const FString& OutputPackagePath);
};
