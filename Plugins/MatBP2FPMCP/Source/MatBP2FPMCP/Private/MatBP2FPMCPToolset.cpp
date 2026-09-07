#include "MatBP2FPMCPToolset.h"

#if MATBP2FP_WITH_UE58_MCP

namespace MatBP2FPMCP
{
    static FMatBP2FPPythonResult MakeInvalidResult(const FString& Message)
    {
        FMatBP2FPPythonResult Result;
        Result.Message = Message;
        return Result;
    }

    static bool ValidateGamePath(
        const FString& Input,
        const TCHAR* FieldName,
        bool bAllowContentRoot,
        FMatBP2FPPythonResult& OutError)
    {
        FString Path = Input;
        Path.TrimStartAndEndInline();

        const bool bIsContentRoot = Path == TEXT("/Game");
        const bool bIsUnderGame = Path.StartsWith(TEXT("/Game/"));
        const bool bHasUnsafeSyntax = Path.Contains(TEXT("\")) ||
            Path.Contains(TEXT("..")) ||
            Path.Contains(TEXT("//"));

        if (Path.IsEmpty() || (!bIsContentRoot && !bIsUnderGame) ||
            (!bAllowContentRoot && bIsContentRoot) || bHasUnsafeSyntax)
        {
            OutError = MakeInvalidResult(FString::Printf(
                TEXT("%s must be a normalized /Game path without traversal: %s"),
                FieldName,
                *Input));
            return false;
        }

        return true;
    }

    static bool ValidateDSLText(
        const FString& DSLText,
        FMatBP2FPPythonResult& OutError)
    {
        FString Trimmed = DSLText;
        Trimmed.TrimStartAndEndInline();
        if (Trimmed.IsEmpty())
        {
            OutError = MakeInvalidResult(TEXT("DSLText must not be empty."));
            return false;
        }
        return true;
    }
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ExportMaterialToText(
    const FString& MaterialPath)
{
    FMatBP2FPPythonResult Error;
    if (!MatBP2FPMCP::ValidateGamePath(MaterialPath, TEXT("MaterialPath"), false, Error))
    {
        return Error;
    }

    return UMatBP2FPPythonBridge::ExportMaterialToText(MaterialPath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ValidateMaterialRoundTrip(
    const FString& MaterialPath)
{
    FMatBP2FPPythonResult Error;
    if (!MatBP2FPMCP::ValidateGamePath(MaterialPath, TEXT("MaterialPath"), false, Error))
    {
        return Error;
    }

    return UMatBP2FPPythonBridge::ValidateMaterialRoundTrip(MaterialPath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::GetMappingTable()
{
    return UMatBP2FPPythonBridge::GetMappingTable();
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::FindMappingByMaterial(
    const FString& MaterialPath)
{
    FMatBP2FPPythonResult Error;
    if (!MatBP2FPMCP::ValidateGamePath(MaterialPath, TEXT("MaterialPath"), false, Error))
    {
        return Error;
    }

    return UMatBP2FPPythonBridge::FindMappingByMaterial(MaterialPath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::MaterialPathToDSLPath(
    const FString& MaterialPath)
{
    FMatBP2FPPythonResult Error;
    if (!MatBP2FPMCP::ValidateGamePath(MaterialPath, TEXT("MaterialPath"), false, Error))
    {
        return Error;
    }

    return UMatBP2FPPythonBridge::MaterialPathToDSLPath(MaterialPath);
}

FMatBP2FPPythonResult UMatBP2FPEditToolset::ImportMaterialFromText(
    const FString& DSLText,
    const FString& DestinationFolder,
    bool bSavePackage)
{
    FMatBP2FPPythonResult Error;
    if (!MatBP2FPMCP::ValidateGamePath(
            DestinationFolder, TEXT("DestinationFolder"), true, Error) ||
        !MatBP2FPMCP::ValidateDSLText(DSLText, Error))
    {
        return Error;
    }

    return UMatBP2FPPythonBridge::ImportMaterialFromText(
        DSLText,
        DestinationFolder,
        bSavePackage);
}

FMatBP2FPPythonResult UMatBP2FPEditToolset::UpdateMaterialFromText(
    const FString& MaterialPath,
    const FString& DSLText,
    bool bSavePackage)
{
    FMatBP2FPPythonResult Error;
    if (!MatBP2FPMCP::ValidateGamePath(MaterialPath, TEXT("MaterialPath"), false, Error) ||
        !MatBP2FPMCP::ValidateDSLText(DSLText, Error))
    {
        return Error;
    }

    return UMatBP2FPPythonBridge::UpdateMaterialFromText(
        MaterialPath,
        DSLText,
        bSavePackage);
}

#endif
