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
	// Engine init callback
	void OnEngineInit();
	
	// Menu registration
	void RegisterMenuExtensions();
	
	// Setup auto-sync based on settings
	void SetupAutoSync();
	
	// Menu actions
	void ExportSelectedMaterials();
	void ExportAllMaterials();
	void RunRoundTripValidation();
	
	// Helpers
	FString GetOutputPath() const;

	// 初始化 Material <-> DSL 映射注册表
	void InitializeMappingRegistry();

	// Stub generation
	void ExportStub();
	FString GetStubPath() const;
	
	// Delegate handles
	FDelegateHandle PostEngineInitHandle;
	
	// Compiler hook for auto-sync (owned by module)
	TUniquePtr<class FMatBP2FPCompilerHook> CompilerHook;
};
