// MatBP2FPExportCommandlet.cpp
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPExportCommandlet.h"
#include "MatBP2FPExportService.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

UMatBP2FPExportCommandlet::UMatBP2FPExportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPExportCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Log, TEXT("=== MatBP2FP Export Commandlet ==="));

	FMatBP2FPExportOptions Options;
	Options.OutputDirectory = FPaths::ProjectDir() / TEXT("Saved") / TEXT("BP2DSL") / TEXT("MatBP");

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);
	
	if (const FString* Found = ParamMap.Find(TEXT("output")))
	{
		Options.OutputDirectory = FPaths::IsRelative(*Found)
			? FPaths::ProjectDir() / *Found : *Found;
	}
	if (const FString* Found = ParamMap.Find(TEXT("material")))
	{
		Options.AssetFilter = FPackageName::ObjectPathToPackageName(*Found);
	}
	if (const FString* Found = ParamMap.Find(TEXT("path")))
	{
		Options.AssetFilter = FPackageName::ObjectPathToPackageName(*Found);
	}
	Options.bIncludeEngine = FParse::Param(*Params, TEXT("include-engine"));
	Options.bIncludeMaterials = !FParse::Param(*Params, TEXT("functions-only"));
	Options.bIncludeFunctions = !FParse::Param(*Params, TEXT("materials-only"));

	if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
	{
		UE_LOG(LogTemp, Warning, TEXT("-NullRHI was not supplied; CI export should run with -NullRHI"));
	}
	if (!FApp::IsUnattended() && !FParse::Param(FCommandLine::Get(), TEXT("Unattended")))
	{
		UE_LOG(LogTemp, Warning, TEXT("-Unattended was not supplied; CI export should run unattended"));
	}

	const FMatBP2FPExportResult Result = FMatBP2FPExportService::ExportAll(Options);
	for (const FMatBP2FPExportedAsset& Asset : Result.Assets)
	{
		UE_LOG(LogTemp, Log, TEXT("  [OK] %s -> %s"), *Asset.AssetPath, *Asset.FilePath);
	}
	for (const FString& Failure : Result.Failures)
	{
		UE_LOG(LogTemp, Error, TEXT("  [FAIL] %s"), *Failure);
	}

	UE_LOG(LogTemp, Log, TEXT("=== Export Complete: %d asset(s), %d reference(s), %d failure(s) ==="),
		Result.Assets.Num(), Result.References.Num(), Result.Failures.Num());
	UE_LOG(LogTemp, Log, TEXT("Reference index: %s"), *Result.IndexFilePath);
	if (Result.Assets.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No matching material graph assets were exported"));
		return 1;
	}
	return Result.Succeeded() ? 0 : 1;
}
