// MatBP2FPPythonBridge.cpp - Python-facing editor bridge for MatBP2FP
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPPythonBridge.h"

#include "MatBPExporter.h"
#include "MatBPImporter.h"
#include "MatLangRoundTrip.h"
#include "FMatBP2FPMappingRegistry.h"
#include "MatNodeExporter.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

namespace MatBP2FPBridge
{
	static FMatBP2FPPythonResult MakeFailure(const FString& Message)
	{
		FMatBP2FPPythonResult Result;
		Result.bSuccess = false;
		Result.Message = Message;
		return Result;
	}

	/** Normalise /Game/Foo/M_Bar -> /Game/Foo/M_Bar.M_Bar */
	static FString NormalizeMaterialObjectPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (Path.IsEmpty() || Path.Contains(TEXT("'")) || Path.Contains(TEXT(".")))
		{
			return Path;
		}
		const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
		return AssetName.IsEmpty() ? Path : Path + TEXT(".") + AssetName;
	}

	static UMaterial* LoadMaterialByPath(const FString& MaterialPath, FString& OutResolvedPath, FString& OutError)
	{
		if (MaterialPath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("MaterialPath is empty");
			return nullptr;
		}
		OutResolvedPath = NormalizeMaterialObjectPath(MaterialPath);
		UMaterial* Mat = LoadObject<UMaterial>(nullptr, *OutResolvedPath);
		if (!Mat)
		{
			OutError = FString::Printf(TEXT("Failed to load Material: %s"), *MaterialPath);
		}
		return Mat;
	}

	static bool ReadTextFile(const FString& FilePath, FString& OutText, FString& OutError)
	{
		if (FilePath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("FilePath is empty");
			return false;
		}
		if (!FFileHelper::LoadFileToString(OutText, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
			return false;
		}
		return true;
	}

	static bool WriteTextFile(const FString& FilePath, const FString& Text, FString& OutError)
	{
		if (FilePath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("Output file path is empty");
			return false;
		}
		const FString Directory = FPaths::GetPath(FilePath);
		if (!Directory.IsEmpty() && !IFileManager::Get().DirectoryExists(*Directory))
		{
			IFileManager::Get().MakeDirectory(*Directory, true);
		}
		if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write file: %s"), *FilePath);
			return false;
		}
		return true;
	}

	static bool SaveMaterialPackage(UMaterial* Material, FString& OutError)
	{
		if (!Material)
		{
			OutError = TEXT("Material is null");
			return false;
		}
		UPackage* Package = Material->GetPackage();
		if (!Package)
		{
			OutError = TEXT("Material package is null");
			return false;
		}
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Package);
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
		{
			OutError = FString::Printf(TEXT("Failed to save package for %s"), *Material->GetPathName());
			return false;
		}
		return true;
	}
}

// ========== Export ==========

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ExportMaterialToText(const FString& MaterialPath)
{
	FString ResolvedPath;
	FString Error;
	UMaterial* Mat = MatBP2FPBridge::LoadMaterialByPath(MaterialPath, ResolvedPath, Error);
	if (!Mat)
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}

	FMatBP2FPPythonResult Result;
	Result.AssetPath = ResolvedPath;
	Result.DSLText = FMatBPExporter::ExportToString(Mat);
	Result.bSuccess = !Result.DSLText.IsEmpty();
	Result.Message = Result.bSuccess
		? FString::Printf(TEXT("Exported Material to MatLang DSL: %s"), *ResolvedPath)
		: FString::Printf(TEXT("Export produced empty result for: %s"), *ResolvedPath);
	return Result;
}

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ExportMaterialToFile(const FString& MaterialPath, const FString& OutputFilePath)
{
	FMatBP2FPPythonResult Result = ExportMaterialToText(MaterialPath);
	if (!Result.bSuccess)
	{
		return Result;
	}
	FString Error;
	if (!MatBP2FPBridge::WriteTextFile(OutputFilePath, Result.DSLText, Error))
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}
	Result.FilePath = OutputFilePath;
	Result.Message = FString::Printf(TEXT("Exported Material to file: %s"), *OutputFilePath);
	return Result;
}

// ========== Export with Dependencies ==========

namespace
{
	/** Recursively collect all MaterialFunctions referenced by a material or function */
	void CollectMaterialFunctionDependencies(
		const TConstArrayView<TObjectPtr<UMaterialExpression>>& Expressions,
		TSet<TObjectPtr<UMaterialFunctionInterface>>& OutVisited,
		TArray<TObjectPtr<UMaterialFunctionInterface>>& OutOrder)
	{
		for (UMaterialExpression* Expr : Expressions)
		{
			if (auto* MFC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
			{
				if (MFC->MaterialFunction && !OutVisited.Contains(MFC->MaterialFunction))
				{
					OutVisited.Add(MFC->MaterialFunction);
					OutOrder.Add(MFC->MaterialFunction);
					// Recurse into the function's own expressions
					CollectMaterialFunctionDependencies(MFC->MaterialFunction->GetExpressions(), OutVisited, OutOrder);
				}
			}
		}
	}

	/** Convert asset object path to output dir subpath: OutputDir/Path/Name.matlang */
	FString AssetPathToOutputFile(const FString& AssetObjectPath, const FString& OutputDir)
	{
		// Use registry for proper mount point handling
		FString PackagePath = FMatBP2FPMappingRegistry::MaterialToDSLPath(AssetObjectPath);
		if (!PackagePath.IsEmpty())
		{
			return PackagePath;
		}
		// Fallback: strip first mount point
		FString PkgPath = FPackageName::ObjectPathToPackageName(AssetObjectPath);
		FString Relative = PkgPath.RightChop(PkgPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1) + 1);
		return OutputDir / Relative + TEXT(".matlang");
	}
}

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ExportMaterialWithDependenciesToFile(
	const FString& MaterialPath, const FString& OutputDir)
{
	if (OutputDir.TrimStartAndEnd().IsEmpty())
	{
		return MatBP2FPBridge::MakeFailure(TEXT("OutputDir is empty"));
	}

	// Load the main material
	FString ResolvedPath;
	FString Error;
	UMaterial* Mat = MatBP2FPBridge::LoadMaterialByPath(MaterialPath, ResolvedPath, Error);
	if (!Mat)
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}

	TArray<FString> ExportedFiles;
	TArray<FString> FailedFiles;

	// 1. Recursively collect all referenced MaterialFunctions
	TSet<TObjectPtr<UMaterialFunctionInterface>> Visited;
	TArray<TObjectPtr<UMaterialFunctionInterface>> DepFunctions;
	CollectMaterialFunctionDependencies(Mat->GetExpressions(), Visited, DepFunctions);

	// 2. Export each MaterialFunction (dependencies first, topological order)
	for (auto Func : DepFunctions)
	{
		auto MatFunc = Cast<UMaterialFunction>(Func);
		if (!IsValid(MatFunc))
		{
			FailedFiles.Add(FString::Printf(TEXT("[FAIL] Cast UMaterialFunction Failed %s"), *Func->GetPathName()));
			continue;
		}
		FString FuncDSL = FMatBPExporter::ExportMaterialFunctionToString(MatFunc);
		if (FuncDSL.IsEmpty())
		{
			FailedFiles.Add(FString::Printf(TEXT("[FAIL] %s"), *Func->GetPathName()));
			continue;
		}

		FString FuncOutputPath = AssetPathToOutputFile(Func->GetPathName(), OutputDir);
		FString WriteError;
		if (MatBP2FPBridge::WriteTextFile(FuncOutputPath, FuncDSL, WriteError))
		{
			ExportedFiles.Add(FString::Printf(TEXT("[OK] %s -> %s"), *Func->GetName(), *FuncOutputPath));
			// Note: MaterialFunction not tracked in registry (registry only scans UMaterial)
		}
		else
		{
			FailedFiles.Add(FString::Printf(TEXT("[FAIL] %s: %s"), *Func->GetName(), *WriteError));
		}
	}

	// 3. Export the main material
	FString MaterialDSL = FMatBPExporter::ExportToString(Mat);
	if (!MaterialDSL.IsEmpty())
	{
		FString MaterialOutputPath = AssetPathToOutputFile(ResolvedPath, OutputDir);
		FString WriteError;
		if (MatBP2FPBridge::WriteTextFile(MaterialOutputPath, MaterialDSL, WriteError))
		{
			ExportedFiles.Add(FString::Printf(TEXT("[OK] %s -> %s"), *Mat->GetName(), *MaterialOutputPath));
			FMatBP2FPMappingRegistry::Get().MarkExported(ResolvedPath, MaterialDSL);
		}
		else
		{
			FailedFiles.Add(FString::Printf(TEXT("[FAIL] %s: %s"), *Mat->GetName(), *WriteError));
		}
	}
	else
	{
		FailedFiles.Add(FString::Printf(TEXT("[FAIL] %s: export produced empty DSL"), *Mat->GetName()));
	}

	// Build result
	FMatBP2FPPythonResult Result;
	Result.bSuccess = FailedFiles.Num() == 0;
	Result.AssetPath = ResolvedPath;
	Result.DSLText = MaterialDSL;

	// Summary message
	FString Summary = FString::Printf(TEXT("Exported %s with dependencies: %d files exported, %d failed"),
		*Mat->GetName(), ExportedFiles.Num(), FailedFiles.Num());
	if (ExportedFiles.Num() > 0)
	{
		Summary += TEXT("\nExported:\n") + FString::Join(ExportedFiles, TEXT("\n"));
	}
	if (FailedFiles.Num() > 0)
	{
		Summary += TEXT("\nFailed:\n") + FString::Join(FailedFiles, TEXT("\n"));
	}
	Result.Message = Summary;

	return Result;
}

// ========== Import ==========

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ImportMaterialFromText(const FString& DSLText, const FString& DestinationFolder, bool bSavePackage)
{
	if (DSLText.TrimStartAndEnd().IsEmpty())
	{
		return MatBP2FPBridge::MakeFailure(TEXT("DSLText is empty"));
	}
	if (DestinationFolder.TrimStartAndEnd().IsEmpty())
	{
		return MatBP2FPBridge::MakeFailure(TEXT("DestinationFolder is empty"));
	}

	FMatBPImporter::FImportResult ImportResult = FMatBPImporter::ImportFromString(DSLText, DestinationFolder);

	FMatBP2FPPythonResult Result;
	Result.bSuccess = ImportResult.bSuccess;
	Result.DSLText = DSLText;
	Result.NumExpressionsCreated = ImportResult.ExpressionsCreated;
	Result.NumConnectionsMade = ImportResult.ConnectionsMade;
	Result.Warnings = ImportResult.Messages;  // Messages is TArray<FString>

	if (!ImportResult.bSuccess)
	{
		Result.Message = TEXT("Import failed");
		return Result;
	}

	Result.AssetPath = ImportResult.Material ? ImportResult.Material->GetPathName() : TEXT("");
	Result.Message = FString::Printf(
		TEXT("Imported Material: %s (%d expressions, %d connections, %d msg)"),
		*Result.AssetPath, ImportResult.ExpressionsCreated, ImportResult.ConnectionsMade,
		ImportResult.Messages.Num());

	if (bSavePackage && ImportResult.Material)
	{
		FString SaveError;
		Result.bSavedPackage = MatBP2FPBridge::SaveMaterialPackage(ImportResult.Material, SaveError);
		if (!Result.bSavedPackage)
		{
			Result.Warnings.Add(SaveError);
			Result.Message += TEXT(" (package save failed)");
		}
	}
	return Result;
}

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ImportMaterialFromFile(const FString& InputFilePath, const FString& DestinationFolder, bool bSavePackage)
{
	FString DSLText;
	FString Error;
	if (!MatBP2FPBridge::ReadTextFile(InputFilePath, DSLText, Error))
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}
	FMatBP2FPPythonResult Result = ImportMaterialFromText(DSLText, DestinationFolder, bSavePackage);
	Result.FilePath = InputFilePath;
	return Result;
}

// ========== Update ==========

FMatBP2FPPythonResult UMatBP2FPPythonBridge::UpdateMaterialFromText(const FString& MaterialPath, const FString& DSLText, bool bSavePackage)
{
	if (DSLText.TrimStartAndEnd().IsEmpty())
	{
		return MatBP2FPBridge::MakeFailure(TEXT("DSLText is empty"));
	}
	FString ResolvedPath;
	FString Error;
	UMaterial* Mat = MatBP2FPBridge::LoadMaterialByPath(MaterialPath, ResolvedPath, Error);
	if (!Mat)
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}

	FMatBPImporter::FUpdateResult UpdateResult = FMatBPImporter::UpdateMaterialDetailed(Mat, DSLText);

	FMatBP2FPPythonResult Result;
	Result.bSuccess = UpdateResult.bSuccess;
	Result.AssetPath = ResolvedPath;
	Result.DSLText = DSLText;
	Result.bUsedIncrementalPatch = UpdateResult.bUsedIncrementalPatch;
	Result.NumChanges = UpdateResult.NumChanges;
	Result.NumStructuralChanges = UpdateResult.NumStructuralChanges;
	Result.Warnings = UpdateResult.Messages;
	Result.Message = FString::Printf(
		TEXT("Update %s (%s): %d changes (%d structural)"),
		UpdateResult.bSuccess ? TEXT("succeeded") : TEXT("failed"),
		UpdateResult.bUsedIncrementalPatch ? TEXT("incremental") : TEXT("full rebuild"),
		UpdateResult.NumChanges, UpdateResult.NumStructuralChanges);

	if (!Result.bSuccess)
	{
		return Result;
	}

	if (bSavePackage)
	{
		FString SaveError;
		Result.bSavedPackage = MatBP2FPBridge::SaveMaterialPackage(Mat, SaveError);
		if (!Result.bSavedPackage)
		{
			Result.Warnings.Add(SaveError);
			Result.Message += TEXT(" (package save failed)");
		}
	}
	return Result;
}

FMatBP2FPPythonResult UMatBP2FPPythonBridge::UpdateMaterialFromFile(const FString& MaterialPath, const FString& InputFilePath, bool bSavePackage)
{
	FString DSLText;
	FString Error;
	if (!MatBP2FPBridge::ReadTextFile(InputFilePath, DSLText, Error))
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}
	FMatBP2FPPythonResult Result = UpdateMaterialFromText(MaterialPath, DSLText, bSavePackage);
	Result.FilePath = InputFilePath;
	return Result;
}

// ========== Validation ==========

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ValidateMaterialRoundTrip(const FString& MaterialPath)
{
	FString ResolvedPath;
	FString Error;
	UMaterial* Mat = MatBP2FPBridge::LoadMaterialByPath(MaterialPath, ResolvedPath, Error);
	if (!Mat)
	{
		return MatBP2FPBridge::MakeFailure(Error);
	}

	FMatLangRoundTrip::FRoundTripResult RoundTrip = FMatLangRoundTrip::Validate(Mat);

	FMatBP2FPPythonResult Result;
	Result.AssetPath = ResolvedPath;
	Result.bSuccess = RoundTrip.bPassed;
	Result.DSLText = FMatBPExporter::ExportToString(Mat);
	Result.Warnings = RoundTrip.Diffs;
	Result.Message = FString::Printf(
		TEXT("RoundTrip %s: %.1f%% fidelity (%d/%d lines differ) — %s"),
		RoundTrip.bPassed ? TEXT("PASS") : TEXT("FAIL"),
		RoundTrip.Fidelity * 100.0f,
		RoundTrip.DiffLines,
		RoundTrip.TotalLines,
		*RoundTrip.MaterialName);
	return Result;
}

// ========== Mapping Registry ==========

FMatBP2FPPythonResult UMatBP2FPPythonBridge::GetMappingTable()
{
	FMatBP2FPMappingRegistry& Registry = FMatBP2FPMappingRegistry::Get();

	FMatBP2FPPythonResult Result;
	Result.bSuccess = true;

	// Build JSON array of all entries
	TArray<FString> JsonEntries;
	for (const FMatBP2FPMappingEntry& Entry : Registry.GetAllEntries())
	{
		FString StateStr;
		switch (Entry.State)
		{
		case EMatBPSyncState::Synced:    StateStr = TEXT("Synced");    break;
		case EMatBPSyncState::MatOnly:   StateStr = TEXT("MatOnly");   break;
		case EMatBPSyncState::DSLOnly:   StateStr = TEXT("DSLOnly");   break;
		case EMatBPSyncState::OutOfSync: StateStr = TEXT("OutOfSync"); break;
		}

		JsonEntries.Add(FString::Printf(
			TEXT("{\"material_path\":\"%s\",\"dsl_file_path\":\"%s\",\"state\":\"%s\",\"has_material\":%s,\"has_dsl\":%s}"),
			*Entry.MaterialPath,
			*Entry.DSLFilePath,
			*StateStr,
			Entry.bMaterialExists ? TEXT("true") : TEXT("false"),
			Entry.bDSLFileExists ? TEXT("true") : TEXT("false")
		));
	}

	Result.DSLText = TEXT("[") + FString::Join(JsonEntries, TEXT(",")) + TEXT("]");
	Result.Message = FString::Printf(TEXT("Mapping table: %d entries"), Registry.Num());
	return Result;
}

FMatBP2FPPythonResult UMatBP2FPPythonBridge::FindMappingByMaterial(const FString& MaterialPath)
{
	FMatBP2FPMappingRegistry& Registry = FMatBP2FPMappingRegistry::Get();
	const FMatBP2FPMappingEntry* Entry = Registry.FindByMaterial(MaterialPath);

	if (!Entry)
	{
		FMatBP2FPPythonResult Result;
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("No mapping found for: %s"), *MaterialPath);
		return Result;
	}

	FString StateStr;
	switch (Entry->State)
	{
	case EMatBPSyncState::Synced:    StateStr = TEXT("Synced");    break;
	case EMatBPSyncState::MatOnly:   StateStr = TEXT("MatOnly");   break;
	case EMatBPSyncState::DSLOnly:   StateStr = TEXT("DSLOnly");   break;
	case EMatBPSyncState::OutOfSync: StateStr = TEXT("OutOfSync"); break;
	}

	FMatBP2FPPythonResult Result;
	Result.bSuccess = true;
	Result.AssetPath = Entry->MaterialPath;
	Result.FilePath = Entry->DSLFilePath;
	Result.Message = FString::Printf(TEXT("State: %s | Material: %s | DSL: %s"),
		*StateStr, Entry->bMaterialExists ? TEXT("yes") : TEXT("no"), Entry->bDSLFileExists ? TEXT("yes") : TEXT("no"));
	return Result;
}

FMatBP2FPPythonResult UMatBP2FPPythonBridge::MaterialPathToDSLPath(const FString& MaterialPath)
{
	FString DSLPath = FMatBP2FPMappingRegistry::MaterialToDSLPath(MaterialPath);

	if (DSLPath.IsEmpty())
	{
		FMatBP2FPPythonResult Result;
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Invalid material path (engine/system content not exportable): %s"), *MaterialPath);
		return Result;
	}

	FMatBP2FPPythonResult Result;
	Result.bSuccess = true;
	Result.AssetPath = MaterialPath;
	Result.FilePath = DSLPath;
	Result.Message = FString::Printf(TEXT("%s -> %s"), *MaterialPath, *DSLPath);
	return Result;
}

// ========== Stub Export ==========

FMatBP2FPPythonResult UMatBP2FPPythonBridge::ExportStub(const FString& OutputFilePath)
{
	FString StubPath = OutputFilePath;
	if (StubPath.TrimStartAndEnd().IsEmpty())
	{
		StubPath = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP") / TEXT("matlang-stub.scm");
	}
	FPaths::NormalizeFilename(StubPath);

	bool bOk = FMatNodeExporter::ExportAllExpressions(StubPath);

	FMatBP2FPPythonResult Result;
	Result.bSuccess = bOk;
	Result.FilePath = StubPath;
	Result.Message = bOk
		? FString::Printf(TEXT("Material expression stub exported to: %s"), *StubPath)
		: TEXT("Failed to export material expression stub");
	return Result;
}
