// FMatBP2FPMapping.h - Material <-> DSL Mapping Types
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Sync state of a single Material <-> DSL pair.
 */
UENUM()
enum class EMatBPSyncState : uint8
{
	/** Both Material and DSL exist and are in sync */
	Synced,
	/** Only Material exists (never exported or DSL was deleted) */
	MatOnly,
	/** Only DSL file exists (Material was deleted or not yet created) */
	DSLOnly,
	/** Both exist but content hash mismatch */
	OutOfSync
};

/**
 * Represents a single Material <-> DSL mapping entry.
 */
struct MATBP2FPEDITOR_API FMatBP2FPMappingEntry
{
	/** Package path of the Material asset, e.g. /Game/Props/M_Wood */
	FString MaterialPath;

	/** Absolute file path of the DSL file on disk */
	FString DSLFilePath;

	/** Category tag for grouping, always "MatBP" for this registry */
	FString CategoryTag;

	/** Last time this entry was exported (Material -> DSL) */
	FDateTime LastExportTime;

	/** Content hash of the last exported DSL, for change detection */
	FString DSLContentHash;

	/** Whether the Material asset currently exists on disk */
	bool bMaterialExists = false;

	/** Whether the DSL file currently exists on disk */
	bool bDSLFileExists = false;

	/** Current sync state */
	EMatBPSyncState State = EMatBPSyncState::MatOnly;

	/** Default constructor */
	FMatBP2FPMappingEntry() = default;

	/** Construct with material path */
	FMatBP2FPMappingEntry(const FString& InMatPath)
		: MaterialPath(InMatPath)
		, CategoryTag(TEXT("MatBP"))
		, bMaterialExists(true)
		, bDSLFileExists(false)
		, State(EMatBPSyncState::MatOnly)
	{}
};
