#pragma once

#include "MatBP2FPMCPVersion.h"

#if MATBP2FP_WITH_UE58_MCP
#include "CoreMinimal.h"
#include "MatBP2FPPythonBridge.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#endif

#include "MatBP2FPMCPToolset.generated.h"

#if MATBP2FP_WITH_UE58_MCP

UCLASS(BlueprintType, Hidden)
class MATBP2FPMCP_API UMatBP2FPInspectToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    virtual FString GetToolsetVersion() const override
    {
        return TEXT("1.0.0");
    }

    /** Export one UMaterial to MatLang DSL text without changing the asset. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
    static FMatBP2FPPythonResult ExportMaterialToText(const FString& MaterialPath);

    /** Validate Export -> Parse -> ToString fidelity for one UMaterial. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
    static FMatBP2FPPythonResult ValidateMaterialRoundTrip(const FString& MaterialPath);

    /** Return the Material <-> DSL mapping registry. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
    static FMatBP2FPPythonResult GetMappingTable();

    /** Look up one Material path in the mapping registry. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
    static FMatBP2FPPythonResult FindMappingByMaterial(const FString& MaterialPath);

    /** Convert a /Game Material package path to its Saved/BP2DSL path. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Inspect")
    static FMatBP2FPPythonResult MaterialPathToDSLPath(const FString& MaterialPath);
};

UCLASS(BlueprintType, Hidden)
class MATBP2FPMCP_API UMatBP2FPEditToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    virtual FString GetToolsetVersion() const override
    {
        return TEXT("1.0.0");
    }

    /** Import DSL text as a new UMaterial. Saving is opt-in. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Edit")
    static FMatBP2FPPythonResult ImportMaterialFromText(
        const FString& DSLText,
        const FString& DestinationFolder,
        bool bSavePackage = false);

    /** Update one UMaterial from DSL text. Saving is opt-in. */
    UFUNCTION(meta=(AICallable), Category="MatBP2FP|Edit")
    static FMatBP2FPPythonResult UpdateMaterialFromText(
        const FString& MaterialPath,
        const FString& DSLText,
        bool bSavePackage = false);
};

#endif
