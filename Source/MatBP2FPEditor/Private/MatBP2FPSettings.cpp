// MatBP2FPSettings.cpp - Project Settings Implementation
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPSettings.h"

UMatBP2FPSettings::UMatBP2FPSettings()
	: StubOutputPath(TEXT("Saved/BP2DSL/MatBP/matlang-stub.scm"))
	, AutoSyncMode(EMatBPSyncMode::None)  // Default off, manual enable
	, bIncludeEditorPositions(false)
	, bIncludeComments(true)
	, bAutoCompileAfterImport(true)
{
}
