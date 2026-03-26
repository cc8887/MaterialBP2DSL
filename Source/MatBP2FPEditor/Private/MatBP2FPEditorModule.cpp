// MatBP2FPEditorModule.cpp - Editor Module Implementation
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPEditorModule.h"
#include "MatBPExporter.h"
#include "MatLangRoundTrip.h"
#include "MatBP2FPSettings.h"
#include "ToolMenus.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/MessageDialog.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/AssetManager.h"

#define LOCTEXT_NAMESPACE "FMatBP2FPEditorModule"

IMPLEMENT_MODULE(FMatBP2FPEditorModule, MatBP2FPEditor);

// ========== Module Lifecycle ==========

void FMatBP2FPEditorModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Module startup"));
	
	// Register editor menus (delayed until engine init)
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FMatBP2FPEditorModule::RegisterMenuExtensions
		)
	);
}

void FMatBP2FPEditorModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Module shutdown"));
	
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

// ========== Menu Registration ==========

void FMatBP2FPEditorModule::RegisterMenuExtensions()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!Menu) return;
	
	FToolMenuSection& Section = Menu->AddSection("MatBP2FP", LOCTEXT("MatBP2FPSection", "MatBP2FP"));
	
	Section.AddMenuEntry(
		"ExportAllMaterials",
		LOCTEXT("ExportAll", "Export All Materials to DSL"),
		LOCTEXT("ExportAllTooltip", "Export all materials in the project to MatLang DSL files"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMatBP2FPEditorModule::ExportAllMaterials))
	);
	
	Section.AddMenuEntry(
		"RunRoundTrip",
		LOCTEXT("RoundTrip", "Run Round-Trip Validation"),
		LOCTEXT("RoundTripTooltip", "Validate export fidelity: Export → Parse → ToString → Compare"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMatBP2FPEditorModule::RunRoundTripValidation))
	);
}

// ========== Menu Actions ==========

void FMatBP2FPEditorModule::ExportAllMaterials()
{
	FString OutputPath = GetOutputPath();
	IFileManager::Get().MakeDirectory(*OutputPath, true);
	
	// Find all materials via AssetRegistry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> MaterialAssets;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
	
	// Filter to game content only
	TArray<FAssetData> GameMaterials;
	for (const FAssetData& Asset : MaterialAssets)
	{
		if (Asset.PackageName.ToString().StartsWith(TEXT("/Game/")))
		{
			GameMaterials.Add(Asset);
		}
	}
	
	int32 Exported = 0;
	int32 Failed = 0;
	
	for (const FAssetData& Asset : GameMaterials)
	{
		UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
		if (!Mat) { Failed++; continue; }
		
		FString DSL = FMatBPExporter::ExportToString(Mat);
		if (DSL.IsEmpty()) { Failed++; continue; }
		
		FString FileName = OutputPath / Mat->GetName() + TEXT(".matlang");
		if (FFileHelper::SaveStringToFile(DSL, *FileName, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Exported++;
			UE_LOG(LogTemp, Log, TEXT("Exported: %s"), *FileName);
		}
		else
		{
			Failed++;
			UE_LOG(LogTemp, Error, TEXT("Failed to write: %s"), *FileName);
		}
	}
	
	FString Message = FString::Printf(TEXT("MatBP2FP Export: %d/%d materials exported to %s\n(%d failed)"),
		Exported, GameMaterials.Num(), *OutputPath, Failed);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

void FMatBP2FPEditorModule::RunRoundTripValidation()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> MaterialAssets;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
	
	// Filter to game content
	TArray<FAssetData> GameMaterials;
	for (const FAssetData& Asset : MaterialAssets)
	{
		if (Asset.PackageName.ToString().StartsWith(TEXT("/Game/")))
		{
			GameMaterials.Add(Asset);
		}
	}
	
	int32 Passed = 0;
	int32 Total = 0;
	FString Report;
	
	for (const FAssetData& Asset : GameMaterials)
	{
		UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
		if (!Mat) continue;
		
		Total++;
		auto Result = FMatLangRoundTrip::Validate(Mat);
		
		if (Result.bPassed)
		{
			Passed++;
			Report += FString::Printf(TEXT("✅ %s: PASS (%d lines)\n"), *Result.MaterialName, Result.TotalLines);
		}
		else
		{
			Report += FString::Printf(TEXT("❌ %s: FAIL (%.1f%% fidelity, %d/%d diffs)\n"),
				*Result.MaterialName, Result.Fidelity * 100.0f, Result.DiffLines, Result.TotalLines);
			for (const FString& Diff : Result.Diffs)
			{
				Report += FString::Printf(TEXT("    %s\n"), *Diff);
			}
		}
	}
	
	Report = FString::Printf(TEXT("=== MatBP2FP Round-Trip Validation ===\n%d/%d PASS\n\n%s"), Passed, Total, *Report);
	
	// Save report
	FString OutputPath = GetOutputPath();
	FString ReportPath = OutputPath / TEXT("RoundTrip_Report.txt");
	FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	
	UE_LOG(LogTemp, Log, TEXT("\n%s"), *Report);
	
	FString Message = FString::Printf(TEXT("Round-Trip: %d/%d materials PASS\nReport saved to: %s"), Passed, Total, *ReportPath);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

FString FMatBP2FPEditorModule::GetOutputPath() const
{
	const UMatBP2FPSettings* Settings = GetDefault<UMatBP2FPSettings>();
	FString OutputPath = FPaths::ProjectDir() / Settings->ExportOutputPath;
	FPaths::NormalizeDirectoryName(OutputPath);
	return OutputPath;
}

#undef LOCTEXT_NAMESPACE
