// MatBP2FPMCPToolset.cpp - UE 5.8 native MCP tool surface.
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPMCPToolset.h"

#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP

namespace MatBP2FPMCP
{
	static FMatBP2FPPythonResult MakeFailure(const FString& Message)
	{
		FMatBP2FPPythonResult Result;
		Result.bSuccess = false;
		Result.Message = Message;
		return Result;
	}

	static bool IsSafeProjectAssetPath(const FString& InPath, FString& OutPath, FString& OutError)
	{
		OutPath = InPath.TrimStartAndEnd();
		if (OutPath.IsEmpty())
		{
			OutError = TEXT("Asset path is empty");
			return false;
		}

		if (!OutPath.StartsWith(TEXT("/Game/")) ||
			OutPath.Contains(TEXT("..")) ||
			OutPath.Contains(TEXT("\\")) ||
			OutPath.Contains(TEXT("'")))
		{
			OutError = FString::Printf(
				TEXT("Only /Game asset paths without traversal are allowed: %s"), *OutPath);
			return false;
		}
		return true;
	}

	static bool IsSafeProjectFolder(const FString& InPath, FString& OutPath, FString& OutError)
	{
		if (!IsSafeProjectAssetPath(InPath, OutPath, OutError))
		{
			return false;
		}

		if (OutPath.Contains(TEXT(".")))
		{
			OutError = FString::Printf(
				TEXT("DestinationFolder must be a content folder, not an object path: %s"), *OutPath);
			return false;
		}
		return true;
	}
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ExportMaterialToText(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::ExportMaterialToText(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ValidateMaterialRoundTrip(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::ValidateMaterialRoundTrip(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::GetMappingTable()
{
	return UMatBP2FPPythonBridge::GetMappingTable();
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::FindMappingByMaterial(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::FindMappingByMaterial(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::MaterialPathToDSLPath(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::MaterialPathToDSLPath(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPEditToolset::ImportMaterialFromText(
	const FString& DSLText, const FString& DestinationFolder, bool bSavePackage)
{
	FString SafeFolder;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectFolder(DestinationFolder, SafeFolder, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::ImportMaterialFromText(DSLText, SafeFolder, bSavePackage);
}

FMatBP2FPPythonResult UMatBP2FPEditToolset::UpdateMaterialFromText(
	const FString& MaterialPath, const FString& DSLText, bool bSavePackage)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::UpdateMaterialFromText(SafePath, DSLText, bSavePackage);
}

#endif // MATBP2FP_WITH_MCP
