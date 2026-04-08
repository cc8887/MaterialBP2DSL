// FMatBP2FPMappingRegistry.h - In-memory Material <-> DSL Lookup Table
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FMatBP2FPMapping.h"

/**
 * Singleton registry maintaining an in-memory lookup table for
 * Material <-> DSL bidirectional path mapping.
 *
 * Built at engine startup by scanning AssetRegistry (materials)
 * and FileManager (DSL files), then reconciled.
 *
 * Thread safety: NOT thread-safe. All access must be on the game thread.
 */
class MATBP2FPEDITOR_API FMatBP2FPMappingRegistry
{
public:
	/** Get the singleton instance */
	static FMatBP2FPMappingRegistry& Get();

	// ========== Lifecycle ==========

	/**
	 * Initialize the registry.
	 * Scans all materials via AssetRegistry, scans DSL output directory,
	 * then reconciles to establish initial mapping states.
	 */
	void Initialize();

	/** Clear all entries */
	void Reset();

	// ========== Queries ==========

	/** Find a mapping entry by Material package path. Returns nullptr if not found. */
	const FMatBP2FPMappingEntry* FindByMaterial(const FString& MaterialPath) const;

	/** Find a mapping entry by DSL absolute file path. Returns nullptr if not found. */
	const FMatBP2FPMappingEntry* FindByDSLFile(const FString& DSLFilePath) const;

	/** Get all entries */
	const TArray<FMatBP2FPMappingEntry>& GetAllEntries() const { return Entries; }

	/** Number of entries */
	int32 Num() const { return Entries.Num(); }

	// ========== Path Conversion ==========

	/**
	 * Check if a package path belongs to an exportable content root.
	 * Returns true for /Game/, plugin mount points, etc.
	 * Returns false for /Engine/, /Script/, /Temp/, /Transient/.
	 */
	static bool IsExportablePackage(const FString& PackagePath);

	/**
	 * Convert a Material package path to the corresponding DSL file path.
	 * Convention: /Game/Path/M_Material -> {ProjectDir}/Saved/BP2DSL/MatBP/Path/M_Material.matlang
	 * Also works with custom mount points: /MyPlugin/Path/M_Material -> .../MatBP/Path/M_Material.matlang
	 *
	 * @param MaterialPath  Package path, e.g. /Game/Props/M_Wood
	 * @return Absolute DSL file path, or empty string if MaterialPath is invalid
	 */
	static FString MaterialToDSLPath(const FString& MaterialPath);

	/**
	 * Convert a DSL file path back to the Material package path.
	 * Inverse of MaterialToDSLPath().
	 *
	 * Resolution strategy:
	 * 1. Extract asset name from DSL file path, query AssetRegistry for exact match.
	 *    If exactly one matching material is found, returns its real PackagePath
	 *    (preserving the original mount point, e.g. /MyPlugin/Props/M_Wood).
	 * 2. If no match or ambiguous, falls back to /Game/ prefix and logs a Warning.
	 *
	 * @param DSLFilePath  Absolute DSL file path
	 * @return Package path (e.g. /Game/Props/M_Wood), or empty string if not a valid DSL path
	 */
	static FString DSLToMaterialPath(const FString& DSLFilePath);

	// ========== Mutation ==========

	/**
	 * Get or create a mapping entry for the given Material path.
	 * If the entry exists, returns it. Otherwise creates a new MatOnly entry.
	 */
	FMatBP2FPMappingEntry& GetOrCreateEntry(const FString& MaterialPath);

	/**
	 * Mark a mapping as successfully exported (Material -> DSL).
	 * Updates DSLContentHash, LastExportTime, and state.
	 */
	void MarkExported(const FString& MaterialPath, const FString& DSLContent);

	/**
	 * Mark a mapping as successfully imported (DSL -> Material).
	 * Updates state to Synced.
	 */
	void MarkImported(const FString& DSLFilePath);

private:
	// Singleton
	FMatBP2FPMappingRegistry() = default;
	FMatBP2FPMappingRegistry(const FMatBP2FPMappingRegistry&) = delete;
	FMatBP2FPMappingRegistry& operator=(const FMatBP2FPMappingRegistry&) = delete;

	// Scan all Material assets via AssetRegistry
	void ScanMaterials();

	// Scan existing DSL files from disk
	void ScanDSLFiles();

	// Reconcile Material and DSL scans to set correct states
	void Reconcile();

	// ========== Data ==========

	/** All mapping entries, indexed linearly */
	TArray<FMatBP2FPMappingEntry> Entries;

	/** Quick lookup: MaterialPath -> index in Entries array */
	TMap<FString, int32> MaterialToIndex;

	/** Quick lookup: DSLFilePath -> index in Entries array */
	TMap<FString, int32> DSLFileToIndex;
};
