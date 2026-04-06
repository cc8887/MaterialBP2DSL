// MatBP2FPExportCommandlet.cpp
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPExportCommandlet.h"
#include "MatBPExporter.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UMatBP2FPExportCommandlet::UMatBP2FPExportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPExportCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Log, TEXT("=== MatBP2FP Export Commandlet ==="));

	FString OutputDir = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP");
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	
	// Parse params
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);
	
	FString MaterialPath;
	if (const FString* Found = ParamMap.Find(TEXT("material")))
	{
		MaterialPath = *Found;
	}
	bool bAll = Switches.Contains(TEXT("all"));
	
	// Find materials
	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	AR.SearchAllAssets(true);
	
	TArray<FAssetData> MaterialAssets;
	AR.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
	
	int32 Exported = 0;
	int32 Failed = 0;
	
	for (const FAssetData& Asset : MaterialAssets)
	{
		if (!bAll && !MaterialPath.IsEmpty())
		{
			if (!Asset.PackageName.ToString().Contains(MaterialPath))
			{
				continue;
			}
		}
		else if (!bAll)
		{
			// Default: only game content
			if (!Asset.PackageName.ToString().StartsWith(TEXT("/Game/")))
			{
				continue;
			}
		}
		
		UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
		if (!Mat) { Failed++; continue; }
		
		FString DSL = FMatBPExporter::ExportToString(Mat);
		if (DSL.IsEmpty()) { Failed++; continue; }
		
		// Align with CompilerHook path convention: preserve /Game/ directory structure
		FString PackagePath = Mat->GetPathName();
		FString RelativePath = PackagePath.RightChop(FCString::Strlen(TEXT("/Game/")));
		FString FileName = OutputDir / RelativePath + TEXT(".matlang");
		
		FString Dir = FPaths::GetPath(FileName);
		if (!IFileManager::Get().DirectoryExists(*Dir))
		{
			IFileManager::Get().MakeDirectory(*Dir, true);
		}
		if (FFileHelper::SaveStringToFile(DSL, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Exported++;
			UE_LOG(LogTemp, Log, TEXT("  [OK] %s -> %s"), *Mat->GetName(), *FileName);
		}
		else
		{
			Failed++;
			UE_LOG(LogTemp, Error, TEXT("  [FAIL] Failed to write: %s"), *FileName);
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n=== Export Complete: %d exported, %d failed ==="), Exported, Failed);
	return (Failed > 0) ? 1 : 0;
}
