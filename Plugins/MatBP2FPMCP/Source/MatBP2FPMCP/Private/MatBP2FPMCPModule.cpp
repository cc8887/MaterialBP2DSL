// MatBP2FPMCPModule.cpp - UE 5.8 MCP Toolset registration.
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "Modules/ModuleManager.h"

#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP
#include "MatBP2FPMCPToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#endif

class FMatBP2FPMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP
		RegisterToolsets();
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
			this, &FMatBP2FPMCPModule::RegisterToolsets);
#endif
	}

	virtual void ShutdownModule() override
	{
#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		UnregisterToolsets();
#endif
	}

private:
#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP
	void RegisterToolsets()
	{
		if (bToolsetsRegistered || !UToolsetRegistry::IsAvailable())
		{
			return;
		}

		UToolsetRegistry::RegisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
		bToolsetsRegistered = true;
	}

	void UnregisterToolsets()
	{
		if (!bToolsetsRegistered)
		{
			return;
		}

		UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
		bToolsetsRegistered = false;
	}

	bool bToolsetsRegistered = false;
	FDelegateHandle PostEngineInitHandle;
#endif
};

IMPLEMENT_MODULE(FMatBP2FPMCPModule, MatBP2FPMCP)
