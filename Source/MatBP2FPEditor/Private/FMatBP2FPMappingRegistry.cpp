// FMatBP2FPMappingRegistry.cpp - In-memory Material <-> DSL Lookup Table
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "FMatBP2FPMappingRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"

// ========== Singleton ==========

FMatBP2FPMappingRegistry& FMatBP2FPMappingRegistry::Get()
{
	static FMatBP2FPMappingRegistry Instance;
	return Instance;
}

// ========== Lifecycle ==========

void FMatBP2FPMappingRegistry::Initialize()
{
	Reset();

	UE_LOG(LogTemp, Log, TEXT("MatBP2FPMappingRegistry: Initializing..."));

	// Phase 1: Scan materials
	ScanMaterials();

	// Phase 2: Scan DSL files
	ScanDSLFiles();

	// Phase 3: Reconcile
	Reconcile();

	UE_LOG(LogTemp, Log, TEXT("MatBP2FPMappingRegistry: Initialized with %d entries (%d synced, %d mat-only, %d DSL-only, %d out-of-sync)"),
		Entries.Num(),
		Entries.FilterByPredicate([](const FMatBP2FPMappingEntry& E) { return E.State == EMatBPSyncState::Synced; }).Num(),
		Entries.FilterByPredicate([](const FMatBP2FPMappingEntry& E) { return E.State == EMatBPSyncState::MatOnly; }).Num(),
		Entries.FilterByPredicate([](const FMatBP2FPMappingEntry& E) { return E.State == EMatBPSyncState::DSLOnly; }).Num(),
		Entries.FilterByPredicate([](const FMatBP2FPMappingEntry& E) { return E.State == EMatBPSyncState::OutOfSync; }).Num());
}

void FMatBP2FPMappingRegistry::Reset()
{
	Entries.Empty();
	MaterialToIndex.Empty();
	DSLFileToIndex.Empty();
}

// ========== Queries ==========

const FMatBP2FPMappingEntry* FMatBP2FPMappingRegistry::FindByMaterial(const FString& MaterialPath) const
{
	const int32* IndexPtr = MaterialToIndex.Find(MaterialPath);
	if (!IndexPtr) return nullptr;
	if (!Entries.IsValidIndex(*IndexPtr)) return nullptr;
	return &Entries[*IndexPtr];
}

const FMatBP2FPMappingEntry* FMatBP2FPMappingRegistry::FindByDSLFile(const FString& DSLFilePath) const
{
	const int32* IndexPtr = DSLFileToIndex.Find(DSLFilePath);
	if (!IndexPtr) return nullptr;
	if (!Entries.IsValidIndex(*IndexPtr)) return nullptr;
	return &Entries[*IndexPtr];
}

// ========== Path Conversion (Internal Helpers) ==========

namespace
{
	// Strip the content root mount point prefix from a UE package path.
	// e.g., /Game/Props/M_Wood.M_Wood -> Props/M_Wood.M_Wood
	//        /MyPlugin/Props/M_Wood.M_Wood -> Props/M_Wood.M_Wood
	// Returns empty string for system mount points (/Engine/, /Script/, etc.).
	FString StripContentRootPrefix(const FString& PackagePath)
	{
		if (PackagePath.IsEmpty() || !PackagePath.StartsWith(TEXT("/")))
		{
			return FString();
		}

		// Find the second '/' which marks the end of the mount point
		int32 SecondSlash = PackagePath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1);
		if (SecondSlash <= 0)
		{
			return FString();
		}

		// Extract mount point name (between first and second slash)
		FString MountPoint = PackagePath.Mid(1, SecondSlash - 1);

		// Skip system/engine mount points that should not be exported
		if (MountPoint == TEXT("Engine") ||
			MountPoint == TEXT("Script") ||
			MountPoint == TEXT("Temp") ||
			MountPoint == TEXT("Transient"))
		{
			return FString();
		}

		// Return everything after the mount point: "Props/M_Wood.M_Wood"
		return PackagePath.RightChop(SecondSlash + 1);
	}
}

// ========== Path Conversion (Public) ==========

bool FMatBP2FPMappingRegistry::IsExportablePackage(const FString& PackagePath)
{
	return !StripContentRootPrefix(PackagePath).IsEmpty();
}

FString FMatBP2FPMappingRegistry::MaterialToDSLPath(const FString& MaterialPath)
{
	if (MaterialPath.IsEmpty())
	{
		return FString();
	}

	// Dynamically strip any content root prefix (e.g., /Game/, /MyPlugin/)
	// Returns empty for engine/system paths
	FString RelativePath = StripContentRootPrefix(MaterialPath);
	if (RelativePath.IsEmpty())
	{
		return FString();
	}

	// {ProjectDir}/Saved/BP2DSL/MatBP/Path/M_Material.matlang
	FString DSLPath = FPaths::ProjectDir() /
		TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP") / RelativePath;

	// Change extension to .matlang
	FString BaseName = FPaths::GetBaseFilename(DSLPath);
	FString Dir = FPaths::GetPath(DSLPath);
	return Dir / (BaseName + TEXT(".matlang"));
}

FString FMatBP2FPMappingRegistry::DSLToMaterialPath(const FString& DSLFilePath)
{
	// Expected: {ProjectDir}/Saved/BP2DSL/MatBP/Path/M_Material.matlang
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString ExpectedPrefix = ProjectDir / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP") / TEXT("");

	if (!DSLFilePath.StartsWith(ExpectedPrefix))
	{
		return FString();
	}

	// Strip prefix: Path/M_Material.matlang
	FString RelativePath = DSLFilePath.RightChop(ExpectedPrefix.Len());
	FString AssetName = FPaths::GetBaseFilename(RelativePath);

	// Phase 1: Try to resolve via AssetRegistry (supports any mount point)
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AllMaterials;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), AllMaterials);

	TArray<FString> Candidates;
	for (const FAssetData& Asset : AllMaterials)
	{
		if (Asset.AssetName.ToString() == AssetName && IsExportablePackage(Asset.PackageName.ToString()))
		{
			Candidates.Add(Asset.PackageName.ToString());
		}
	}

	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}
	else if (Candidates.Num() > 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("MatBP2FPMappingRegistry: DSLToMaterialPath: ambiguous asset name '%s' (%d candidates), falling back to /Game/"),
			*AssetName, Candidates.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MatBP2FPMappingRegistry: DSLToMaterialPath: no matching material for '%s', falling back to /Game/"),
			*AssetName);
	}

	// Phase 2: Fallback to /Game/ convention
	return TEXT("/Game/") + FPaths::GetPath(RelativePath) / AssetName;
}

// ========== Mutation ==========

FMatBP2FPMappingEntry& FMatBP2FPMappingRegistry::GetOrCreateEntry(const FString& MaterialPath)
{
	const int32* IndexPtr = MaterialToIndex.Find(MaterialPath);
	if (IndexPtr && Entries.IsValidIndex(*IndexPtr))
	{
		return Entries[*IndexPtr];
	}

	// Create new entry
	FMatBP2FPMappingEntry NewEntry(MaterialPath);
	NewEntry.DSLFilePath = MaterialToDSLPath(MaterialPath);

	int32 NewIndex = Entries.Add(MoveTemp(NewEntry));
	MaterialToIndex.Add(MaterialPath, NewIndex);
	if (!NewEntry.DSLFilePath.IsEmpty())
	{
		DSLFileToIndex.Add(NewEntry.DSLFilePath, NewIndex);
	}

	return Entries[NewIndex];
}

void FMatBP2FPMappingRegistry::MarkExported(const FString& MaterialPath, const FString& DSLContent)
{
	const int32* IndexPtr = MaterialToIndex.Find(MaterialPath);
	if (!IndexPtr || !Entries.IsValidIndex(*IndexPtr)) return;

	FMatBP2FPMappingEntry& Entry = Entries[*IndexPtr];
	Entry.bMaterialExists = true;
	Entry.bDSLFileExists = true;
	Entry.State = EMatBPSyncState::Synced;
	Entry.LastExportTime = FDateTime::Now();

	// Update hash: use string hash as lightweight content fingerprint
	Entry.DSLContentHash = LexToString(GetTypeHash(DSLContent));

	// Ensure DSL file index is up to date
	if (!Entry.DSLFilePath.IsEmpty())
	{
		DSLFileToIndex.Add(Entry.DSLFilePath, *IndexPtr);
	}
}

void FMatBP2FPMappingRegistry::MarkImported(const FString& DSLFilePath)
{
	const int32* IndexPtr = DSLFileToIndex.Find(DSLFilePath);
	if (!IndexPtr || !Entries.IsValidIndex(*IndexPtr)) return;

	FMatBP2FPMappingEntry& Entry = Entries[*IndexPtr];
	Entry.bMaterialExists = true;
	Entry.bDSLFileExists = true;
	Entry.State = EMatBPSyncState::Synced;
}

// ========== Internal Scanning ==========

void FMatBP2FPMappingRegistry::ScanMaterials()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Scan Material assets (UMaterial only, not MaterialInstance)
	TArray<FAssetData> MaterialAssets;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MaterialAssets);

	for (const FAssetData& AssetData : MaterialAssets)
	{
		FString PackagePath = AssetData.PackageName.ToString();

		// Only scan exportable content (not engine/system paths)
		if (!IsExportablePackage(PackagePath))
		{
			continue;
		}

		FMatBP2FPMappingEntry Entry(PackagePath);
		Entry.DSLFilePath = MaterialToDSLPath(PackagePath);
		Entry.bMaterialExists = true;
		Entry.bDSLFileExists = FPaths::FileExists(Entry.DSLFilePath);
		Entry.State = Entry.bDSLFileExists ? EMatBPSyncState::OutOfSync : EMatBPSyncState::MatOnly;

		int32 Idx = Entries.Add(MoveTemp(Entry));
		MaterialToIndex.Add(PackagePath, Idx);
		if (!Entries[Idx].DSLFilePath.IsEmpty())
		{
			DSLFileToIndex.Add(Entries[Idx].DSLFilePath, Idx);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("MatBP2FPMappingRegistry: Scanned %d Materials"), MaterialAssets.Num());
}

void FMatBP2FPMappingRegistry::ScanDSLFiles()
{
	FString ScanDir = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP");

	if (!IFileManager::Get().DirectoryExists(*ScanDir))
	{
		return;
	}

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *ScanDir, TEXT("*.matlang"), true, false);

	// Build asset name -> package path lookup from AssetRegistry
	// This is used to resolve orphaned DSL files (not matched in Phase 1)
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TMap<FString, TArray<FString>> MaterialNameToPaths;
	TArray<FAssetData> AllMaterials;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), AllMaterials);
	for (const FAssetData& Asset : AllMaterials)
	{
		FString PkgPath = Asset.PackageName.ToString();
		if (IsExportablePackage(PkgPath))
		{
			MaterialNameToPaths.FindOrAdd(Asset.AssetName.ToString()).Add(PkgPath);
		}
	}

	for (const FString& FilePath : FoundFiles)
	{
		FString AbsPath = FPaths::ConvertRelativePathToFull(FilePath);

		// Skip if already mapped (from Material scan)
		if (DSLFileToIndex.Contains(AbsPath))
		{
			continue;
		}

		// Try to resolve material path by looking up asset name in registry
		FString AssetName = FPaths::GetBaseFilename(AbsPath);
		FString MatPath;
		bool bAmbiguous = false;

		if (const TArray<FString>* Paths = MaterialNameToPaths.Find(AssetName))
		{
			if (Paths->Num() == 1)
			{
				MatPath = (*Paths)[0];
			}
			else if (Paths->Num() > 1)
			{
				bAmbiguous = true;
			}
		}

		// Fallback: guess path using /Game/ convention
		bool bGuessedPath = false;
		if (MatPath.IsEmpty() && !bAmbiguous)
		{
			MatPath = DSLToMaterialPath(AbsPath);
			bGuessedPath = !MatPath.IsEmpty();
		}

		FMatBP2FPMappingEntry Entry;
		Entry.DSLFilePath = AbsPath;
		Entry.CategoryTag = TEXT("MatBP");
		Entry.bDSLFileExists = true;

		if (!MatPath.IsEmpty())
		{
			Entry.MaterialPath = MatPath;
			Entry.bMaterialExists = FPackageName::DoesPackageExist(MatPath);
			Entry.State = EMatBPSyncState::DSLOnly;

			if (bAmbiguous)
			{
				UE_LOG(LogTemp, Warning, TEXT("MatBP2FPMappingRegistry: Ambiguous material name '%s' (%d matches), DSL: %s"),
					*AssetName, MaterialNameToPaths[AssetName].Num(), *FPaths::GetCleanFilename(AbsPath));
			}
			else if (bGuessedPath && !Entry.bMaterialExists)
			{
				UE_LOG(LogTemp, Warning, TEXT("MatBP2FPMappingRegistry: DSL file '%s' has no matching material (guessed: %s)"),
					*FPaths::GetCleanFilename(AbsPath), *MatPath);
			}
		}
		else
		{
			Entry.State = EMatBPSyncState::DSLOnly;

			if (bAmbiguous)
			{
				UE_LOG(LogTemp, Warning, TEXT("MatBP2FPMappingRegistry: DSL file '%s' - ambiguous material name '%s' (%d matches)"),
					*FPaths::GetCleanFilename(AbsPath), *AssetName, MaterialNameToPaths[AssetName].Num());
			}
		}

		int32 Idx = Entries.Add(MoveTemp(Entry));
		DSLFileToIndex.Add(AbsPath, Idx);
		if (!Entries[Idx].MaterialPath.IsEmpty())
		{
			MaterialToIndex.Add(Entries[Idx].MaterialPath, Idx);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("MatBP2FPMappingRegistry: Scanned %d DSL files in MatBP"), FoundFiles.Num());
}

void FMatBP2FPMappingRegistry::Reconcile()
{
	// For entries with both Material and DSL, update state from initial MatOnly
	for (FMatBP2FPMappingEntry& Entry : Entries)
	{
		if (Entry.bMaterialExists && Entry.bDSLFileExists && Entry.State == EMatBPSyncState::MatOnly)
		{
			// Material scan found DSL file exists too - mark as potentially synced
			Entry.State = EMatBPSyncState::OutOfSync; // Will be confirmed on next actual export
		}
	}
}
