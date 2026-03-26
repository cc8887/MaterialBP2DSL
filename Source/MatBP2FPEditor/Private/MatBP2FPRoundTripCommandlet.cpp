// MatBP2FPRoundTripCommandlet.cpp
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPRoundTripCommandlet.h"
#include "MatLangRoundTrip.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"

UMatBP2FPRoundTripCommandlet::UMatBP2FPRoundTripCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPRoundTripCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Log, TEXT("=== MatBP2FP Round-Trip Validation ==="));
	
	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	AR.SearchAllAssets(true);
	
	TArray<FAssetData> MaterialAssets;
	AR.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
	
	int32 Passed = 0;
	int32 Total = 0;
	
	for (const FAssetData& Asset : MaterialAssets)
	{
		if (!Asset.PackageName.ToString().StartsWith(TEXT("/Game/")))
		{
			continue;
		}
		
		UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
		if (!Mat) continue;
		
		Total++;
		auto Result = FMatLangRoundTrip::Validate(Mat);
		
		if (Result.bPassed)
		{
			Passed++;
			UE_LOG(LogTemp, Log, TEXT("  [PASS] %s: %d lines"), *Result.MaterialName, Result.TotalLines);
		}
		else
		{
			FString FidelityStr = FString::Printf(TEXT("%.1f"), Result.Fidelity * 100.0f);
			UE_LOG(LogTemp, Warning, TEXT("  [FAIL] %s: %s pct fidelity (%d/%d lines differ)"),
				*Result.MaterialName, *FidelityStr, Result.DiffLines, Result.TotalLines);
			for (const FString& Diff : Result.Diffs)
			{
				UE_LOG(LogTemp, Warning, TEXT("      %s"), *Diff);
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n=== Results: %d/%d PASS ==="), Passed, Total);
	return (Passed == Total) ? 0 : 1;
}
