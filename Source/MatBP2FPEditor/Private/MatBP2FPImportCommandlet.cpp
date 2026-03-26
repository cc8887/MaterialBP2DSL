// MatBP2FPImportCommandlet.cpp
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPImportCommandlet.h"
#include "MatBPImporter.h"
#include "MatBPExporter.h"
#include "MatBP2FPSettings.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

UMatBP2FPImportCommandlet::UMatBP2FPImportCommandlet()
	: bTestMode(false), bUpdateMode(false)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPImportCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Log, TEXT("=== MatBP2FP Import Commandlet ==="));
	
	// Parse params
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);
	
	bTestMode = Switches.Contains(TEXT("test"));
	bUpdateMode = Switches.Contains(TEXT("update"));
	if (const FString* Found = ParamMap.Find(TEXT("file")))
	{
		SpecificFile = *Found;
	}
	
	if (bTestMode)
	{
		UE_LOG(LogTemp, Log, TEXT("  Mode: TEST (import → export → compare)"));
	}
	if (bUpdateMode)
	{
		UE_LOG(LogTemp, Log, TEXT("  Mode: UPDATE (modify existing materials)"));
	}
	
	const UMatBP2FPSettings* Settings = GetDefault<UMatBP2FPSettings>();
	FString InputPath = FPaths::ProjectDir() / Settings->ExportOutputPath;
	FString OutputPackagePath = TEXT("/Game/Materials/Imported/");
	
	if (!SpecificFile.IsEmpty())
	{
		ImportFile(SpecificFile, OutputPackagePath);
		return 0;
	}
	
	// Find all .matlang files in the export directory
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *InputPath, TEXT("*.matlang"), true, false);
	
	if (Files.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No .matlang files found in %s"), *InputPath);
		return 1;
	}
	
	UE_LOG(LogTemp, Log, TEXT("  Found %d .matlang files"), Files.Num());
	
	int32 Succeeded = 0;
	int32 Failed = 0;
	
	for (const FString& File : Files)
	{
		UE_LOG(LogTemp, Log, TEXT("\n--- Importing: %s ---"), *FPaths::GetCleanFilename(File));
		
		FString DSL;
		if (!FFileHelper::LoadFileToString(DSL, *File))
		{
			UE_LOG(LogTemp, Error, TEXT("  [FAIL] Failed to read file"));
			Failed++;
			continue;
		}
		
		auto Result = FMatBPImporter::ImportFromString(DSL, OutputPackagePath);
		
		if (Result.bSuccess)
		{
			Succeeded++;
			UE_LOG(LogTemp, Log, TEXT("  [OK] %s: %d expressions, %d connections, %d warnings"),
				*FPaths::GetBaseFilename(File), Result.ExpressionsCreated, Result.ConnectionsMade, Result.Warnings);
			
			// Test mode: re-export and compare
			if (bTestMode && Result.Material)
			{
				FString ReExported = FMatBPExporter::ExportToString(Result.Material);
				
				// Simple comparison
				TArray<FString> OrigLines, ReLines;
				DSL.ParseIntoArrayLines(OrigLines);
				ReExported.ParseIntoArrayLines(ReLines);
				
				int32 MaxLines = FMath::Max(OrigLines.Num(), ReLines.Num());
				int32 DiffCount = 0;
				
				for (int32 i = 0; i < MaxLines; ++i)
				{
					FString Orig = (i < OrigLines.Num()) ? OrigLines[i].TrimEnd() : TEXT("<missing>");
					FString Recon = (i < ReLines.Num()) ? ReLines[i].TrimEnd() : TEXT("<missing>");
					if (Orig != Recon) DiffCount++;
				}
				
				float Fidelity = MaxLines > 0 ? (float)(MaxLines - DiffCount) / MaxLines * 100.0f : 100.0f;
				UE_LOG(LogTemp, Log, TEXT("    Import RoundTrip: %.1f%% fidelity (%d diffs / %d lines)"),
					Fidelity, DiffCount, MaxLines);
			}
		}
		else
		{
			Failed++;
			UE_LOG(LogTemp, Error, TEXT("  [FAIL] Import failed"));
			for (const FString& Msg : Result.Messages)
			{
				UE_LOG(LogTemp, Error, TEXT("    %s"), *Msg);
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n=== Import Complete: %d succeeded, %d failed ==="), Succeeded, Failed);
	return (Failed > 0) ? 1 : 0;
}

void UMatBP2FPImportCommandlet::ImportFile(const FString& FilePath, const FString& OutputPackagePath)
{
	FString DSL;
	if (!FFileHelper::LoadFileToString(DSL, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to read: %s"), *FilePath);
		return;
	}
	
	if (bUpdateMode)
	{
		// Find existing material by name
		FString MatName = FPaths::GetBaseFilename(FilePath);
		FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARModule.Get();
		AR.SearchAllAssets(true);
		
		TArray<FAssetData> MaterialAssets;
		AR.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
		
		for (const FAssetData& Asset : MaterialAssets)
		{
			if (Asset.AssetName.ToString() == MatName)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
				if (Mat)
				{
					auto Result = FMatBPImporter::UpdateMaterialDetailed(Mat, DSL);
					UE_LOG(LogTemp, Log, TEXT("%s: %s (%s, %d changes, %d structural, %d applied, %d failed)"),
						*MatName,
						Result.bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"),
						Result.bUsedIncrementalPatch ? TEXT("INCREMENTAL") : TEXT("FULL REBUILD"),
						Result.NumChanges, Result.NumStructuralChanges,
						Result.NumApplied, Result.NumFailed);
					for (const FString& Msg : Result.Messages)
					{
						UE_LOG(LogTemp, Log, TEXT("  %s"), *Msg);
					}
					return;
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Material '%s' not found, creating new"), *MatName);
	}
	
	auto Result = FMatBPImporter::ImportFromString(DSL, OutputPackagePath);
	UE_LOG(LogTemp, Log, TEXT("%s: %s (%d expressions, %d connections, %d warnings)"),
		*FPaths::GetBaseFilename(FilePath),
		Result.bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"),
		Result.ExpressionsCreated, Result.ConnectionsMade, Result.Warnings);
}
