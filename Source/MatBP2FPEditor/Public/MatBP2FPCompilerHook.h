// MatBP2FPCompilerHook.h - Material Compilation Hook for Auto-Sync
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Hooks into material compilation events to automatically export
 * Materials to DSL when they are compiled.
 *
 * Uses UMaterial::OnMaterialCompilationFinished() for precise compile-time detection.
 * This event provides the MaterialInterface pointer, making it easy to identify
 * which material just finished compiling.
 *
 * Uses TQueue + FTicker for deferred processing to avoid blocking
 * the shader compile pipeline.
 */
class MATBP2FPEDITOR_API FMatBP2FPCompilerHook
{
public:
	FMatBP2FPCompilerHook();
	~FMatBP2FPCompilerHook();

	/** Register the compilation hook */
	void Register();

	/** Unregister the compilation hook */
	void Unregister();

	/** Check if currently registered */
	bool IsRegistered() const { return bIsRegistered; }

private:
	/** Callback: Called when a material finishes compiling */
	void OnMaterialCompilationFinished(UMaterialInterface* Material);

	/** Ticker callback for deferred processing */
	bool Tick(float DeltaTime);

	/** Process a single material from the queue */
	void ProcessQueuedMaterial(const FString& AssetPath);

	/** Export Material to .matlang file */
	void ExportMaterial(UMaterialInterface* Material);

	// Delegate handle
	FDelegateHandle CompilationFinishedHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	// Registration state
	bool bIsRegistered = false;

	// Deferred processing queue (thread-safe)
	TQueue<FString> PendingAssetPaths;

	// Process at most N per tick to avoid frame stalls
	static constexpr int32 MaxPerTick = 5;
};
