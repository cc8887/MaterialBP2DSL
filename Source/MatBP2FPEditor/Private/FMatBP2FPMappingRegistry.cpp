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

// ========== Path Conversion ==========

FString FMatBP2FPMappingRegistry::MaterialToDSLPath(const FString& MaterialPath)
{
	if (MaterialPath.IsEmpty() || !MaterialPath.StartsWith(TEXT("/Game/")))
	{
		return FString();
	}

	// /Game/Path/M_Material -> Path/M_Material
	FString RelativePath = MaterialPath.RightChop(FCString::Strlen(TEXT("/Game/")));

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

	// /Game/Path/M_Material
	return TEXT("/Game/") + FPaths::GetPath(RelativePath) / FPaths::GetBaseFilename(RelativePath);
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

		// Only scan /Game/ materials
		if (!PackagePath.StartsWith(TEXT("/Game/")))
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

	for (const FString& FilePath : FoundFiles)
	{
		FString AbsPath = FPaths::ConvertRelativePathToFull(FilePath);

		// Skip if already mapped (from Material scan)
		if (DSLFileToIndex.Contains(AbsPath))
		{
			continue;
		}

		// Try to reverse-map to a Material path
		FString MatPath = DSLToMaterialPath(AbsPath);

		FMatBP2FPMappingEntry Entry;
		Entry.DSLFilePath = AbsPath;
		Entry.CategoryTag = TEXT("MatBP");
		Entry.bDSLFileExists = true;

		if (!MatPath.IsEmpty())
		{
			Entry.MaterialPath = MatPath;
			Entry.bMaterialExists = FPackageName::DoesPackageExist(MatPath);
			Entry.State = EMatBPSyncState::DSLOnly;
		}
		else
		{
			Entry.State = EMatBPSyncState::DSLOnly;
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
