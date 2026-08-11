// MatBP2FPLintCommandlet.cpp - CI entry point for MatLang linting
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPLintCommandlet.h"
#include "MatLangLinter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatBPLint, Log, All);

UMatBP2FPLintCommandlet::UMatBP2FPLintCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPLintCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString* PathParam = ParamMap.Find(TEXT("path"));
	if (!PathParam || PathParam->TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogMatBPLint, Error,
			TEXT("Usage: -run=MatBP2FPLint -path=<file-or-directory> [-fail-on-warning]"));
		return 2;
	}

	FString InputPath = FPaths::IsRelative(*PathParam)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / *PathParam)
		: FPaths::ConvertRelativePathToFull(*PathParam);
	FPaths::NormalizeFilename(InputPath);

	TArray<FString> Files;
	if (IFileManager::Get().FileExists(*InputPath))
	{
		Files.Add(InputPath);
	}
	else if (IFileManager::Get().DirectoryExists(*InputPath))
	{
		IFileManager::Get().FindFilesRecursive(Files, *InputPath, TEXT("*.matlang"), true, false);
	}
	else
	{
		UE_LOG(LogMatBPLint, Error, TEXT("Input path does not exist: %s"), *InputPath);
		return 2;
	}

	Files.Sort();
	if (Files.Num() == 0)
	{
		UE_LOG(LogMatBPLint, Error, TEXT("No .matlang files found under: %s"), *InputPath);
		return 2;
	}

	const bool bFailOnWarning = Switches.Contains(TEXT("fail-on-warning"));
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	for (const FString& File : Files)
	{
		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *File))
		{
			UE_LOG(LogMatBPLint, Error, TEXT("Failed to read: %s"), *File);
			return 2;
		}

		const FMatLangLintResult Result = FMatLangLinter::Lint(Source, File);
		for (const FMatLangDiagnostic& Diagnostic : Result.Diagnostics)
		{
			switch (Diagnostic.Severity)
			{
				case EMatLangDiagnosticSeverity::Error:
					++ErrorCount;
					UE_LOG(LogMatBPLint, Error, TEXT("%s"), *Diagnostic.ToString());
					break;
				case EMatLangDiagnosticSeverity::Warning:
					++WarningCount;
					UE_LOG(LogMatBPLint, Warning, TEXT("%s"), *Diagnostic.ToString());
					break;
				default:
					UE_LOG(LogMatBPLint, Display, TEXT("%s"), *Diagnostic.ToString());
					break;
			}
		}
	}

	UE_LOG(LogMatBPLint, Display, TEXT("Linted %d file(s): %d error(s), %d warning(s)"),
		Files.Num(), ErrorCount, WarningCount);
	return (ErrorCount > 0 || (bFailOnWarning && WarningCount > 0)) ? 1 : 0;
}
