// MatBP2FPEditorModule.h - Editor Module Header
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMatBP2FPEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	FDelegateHandle PostEngineInitHandle;
	
	// Menu registration
	void RegisterMenuExtensions();
	
	// Menu actions
	void ExportSelectedMaterials();
	void ExportAllMaterials();
	void RunRoundTripValidation();
	
	// Helpers
	FString GetOutputPath() const;
};
