#include "MatBP2FPMCPVersion.h"
#include "Modules/ModuleManager.h"

#if MATBP2FP_WITH_UE58_MCP
#include "MatBP2FPMCPToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#endif

class FMatBP2FPMCPModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
#if MATBP2FP_WITH_UE58_MCP
        UToolsetRegistry::RegisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
#endif
    }

    virtual void ShutdownModule() override
    {
#if MATBP2FP_WITH_UE58_MCP
        UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
        UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
#endif
    }
};

IMPLEMENT_MODULE(FMatBP2FPMCPModule, MatBP2FPMCP)
