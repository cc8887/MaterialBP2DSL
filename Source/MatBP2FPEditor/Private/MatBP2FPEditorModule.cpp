// MatBP2FPEditorModule.cpp - Editor Module Implementation
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPEditorModule.h"
#include "MatBPExporter.h"
#include "MatLangRoundTrip.h"
#include "MatBP2FPSettings.h"
#include "MatBP2FPCompilerHook.h"
#include "FMatBP2FPMappingRegistry.h"
#include "MatNodeExporter.h"
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
	
	// Register engine init callback
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(
		this, &FMatBP2FPEditorModule::OnEngineInit
	);
	
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
	
	// Unregister compiler hook first
	if (CompilerHook.IsValid())
	{
		CompilerHook->Unregister();
		CompilerHook.Reset();
	}
	
	// Remove callbacks
	FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
	
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

// ========== Engine Init ==========

void FMatBP2FPEditorModule::OnEngineInit()
{
	// Initialize the Material <-> DSL mapping registry
	InitializeMappingRegistry();

	// Setup auto-sync based on settings
	SetupAutoSync();
}

// ========== Auto Sync Setup ==========

void FMatBP2FPEditorModule::SetupAutoSync()
{
	const UMatBP2FPSettings* Settings = GetDefault<UMatBP2FPSettings>();
	
	if (Settings->AutoSyncMode == EMatBPSyncMode::Mat2FP)
	{
		// Create compiler hook if not already created
		if (!CompilerHook.IsValid())
		{
			CompilerHook = MakeUnique<FMatBP2FPCompilerHook>();
		}
		
		if (!CompilerHook->IsRegistered())
		{
			CompilerHook->Register();
		}
		
		UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Auto-sync Mat2FP enabled"));
	}
	else if (Settings->AutoSyncMode == EMatBPSyncMode::FP2Mat)
	{
		// FP2Mat: File watcher mode (not yet implemented)
		UE_LOG(LogTemp, Warning, TEXT("MatBP2FPEditor: FP2Mat auto-sync mode is not yet implemented"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Auto-sync disabled"));
	}
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
		"ExportMatStub",
		LOCTEXT("ExportStub", "Export MatLang Stub"),
		LOCTEXT("ExportStubTooltip", "Generate stub file with all material expression type signatures"),
		FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FMatBP2FPEditorModule::ExportStub))
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

void FMatBP2FPEditorModule::ExportSelectedMaterials()
{
	// TODO: Implement selected materials export from content browser
	UE_LOG(LogTemp, Warning, TEXT("MatBP2FPEditor: ExportSelectedMaterials not yet implemented"));
}

void FMatBP2FPEditorModule::ExportAllMaterials()
{
	FString OutputDir = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP");
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	
	// Find all materials via AssetRegistry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> MaterialAssets;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
	
	// Filter to exportable content only (not engine/system paths)
	TArray<FAssetData> GameMaterials;
	for (const FAssetData& Asset : MaterialAssets)
	{
		if (FMatBP2FPMappingRegistry::IsExportablePackage(Asset.PackageName.ToString()))
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
		
		// Use unified path convention via registry
		FString PackagePath = Mat->GetPathName();
		FString FileName = FMatBP2FPMappingRegistry::MaterialToDSLPath(PackagePath);
		if (FileName.IsEmpty())
		{
			Failed++;
			continue;
		}
		
		FString Dir = FPaths::GetPath(FileName);
		if (!IFileManager::Get().DirectoryExists(*Dir))
		{
			IFileManager::Get().MakeDirectory(*Dir, true);
		}
		
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
		Exported, GameMaterials.Num(), *OutputDir, Failed);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

void FMatBP2FPEditorModule::RunRoundTripValidation()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> MaterialAssets;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets, true);
	
	// Filter to exportable content (not engine/system paths)
	TArray<FAssetData> GameMaterials;
	for (const FAssetData& Asset : MaterialAssets)
	{
		if (FMatBP2FPMappingRegistry::IsExportablePackage(Asset.PackageName.ToString()))
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
	FString ReportDir = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP");
	FString ReportPath = ReportDir / TEXT("RoundTrip_Report.txt");
	FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	
	UE_LOG(LogTemp, Log, TEXT("\n%s"), *Report);
	
	FString Message = FString::Printf(TEXT("Round-Trip: %d/%d materials PASS\nReport saved to: %s"), Passed, Total, *ReportPath);
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

FString FMatBP2FPEditorModule::GetOutputPath() const
{
	// Unified path convention: {Project}/Saved/BP2DSL/MatBP
	FString OutputPath = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP");
	FPaths::NormalizeDirectoryName(OutputPath);
	return OutputPath;
}

// ========== Mapping Registry ==========

void FMatBP2FPEditorModule::InitializeMappingRegistry()
{
	UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Initializing Material <-> DSL mapping registry..."));
	FMatBP2FPMappingRegistry::Get().Initialize();
}

// ========== Stub Generation ==========

void FMatBP2FPEditorModule::ExportStub()
{
	FString StubPath = GetStubPath();
	UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Exporting material expression stub to %s"), *StubPath);

	bool bOk = FMatNodeExporter::ExportAllExpressions(StubPath);
	if (bOk)
	{
		UE_LOG(LogTemp, Log, TEXT("MatBP2FPEditor: Stub exported successfully."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MatBP2FPEditor: Failed to export stub."));
	}
}

FString FMatBP2FPEditorModule::GetStubPath() const
{
	const UMatBP2FPSettings* Settings = GetDefault<UMatBP2FPSettings>();
	FString Path = Settings->StubOutputPath;
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::ProjectDir() / Path;
	}
	FPaths::NormalizeFilename(Path);
	return Path;
}

#undef LOCTEXT_NAMESPACE
