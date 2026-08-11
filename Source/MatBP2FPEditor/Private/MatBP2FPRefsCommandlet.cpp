// Query forward and reverse material-function references from an export index.

#include "MatBP2FPRefsCommandlet.h"

#include "MatBP2FPExportService.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

UMatBP2FPRefsCommandlet::UMatBP2FPRefsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPRefsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString* AssetValue = ParamMap.Find(TEXT("asset"));
	if (!AssetValue || AssetValue->IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=MatBP2FPRefs -asset=/Game/Path/Asset.Asset [-direction=in|out|both] [-index=file]"));
		return 2;
	}

	FString IndexPath = FPaths::ProjectDir() / TEXT("Saved/BP2DSL/MatBP/matlang-index.json");
	if (const FString* Value = ParamMap.Find(TEXT("index")))
	{
		IndexPath = FPaths::IsRelative(*Value) ? FPaths::ProjectDir() / *Value : *Value;
	}
	FString Direction = TEXT("both");
	if (const FString* Value = ParamMap.Find(TEXT("direction")))
	{
		Direction = Value->ToLower();
	}
	if (Direction != TEXT("in") && Direction != TEXT("out") && Direction != TEXT("both"))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid -direction. Expected in, out, or both."));
		return 2;
	}

	FMatBP2FPReferenceIndex Index;
	FString Error;
	if (!FMatBP2FPReferenceIndex::Load(IndexPath, Index, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
		return 2;
	}

	const FString AssetPath = *AssetValue;
	const FString AssetPackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
	auto MatchesAsset = [&](const FString& Candidate)
	{
		return Candidate == AssetPath ||
			FPackageName::ObjectPathToPackageName(Candidate) == AssetPackagePath;
	};
	const FString IndexDirectory = FPaths::GetPath(FPaths::ConvertRelativePathToFull(IndexPath));
	auto AbsoluteFile = [&](const FString& RelativeFile)
	{
		FString File = FPaths::ConvertRelativePathToFull(IndexDirectory / RelativeFile);
		FPaths::NormalizeFilename(File);
		return File;
	};

	bool bFound = false;
	if (const FMatBP2FPExportedAsset* Asset = Index.FindAsset(AssetPath))
	{
		UE_LOG(LogTemp, Display, TEXT("definition: %s:1"), *AbsoluteFile(Asset->RelativeFilePath));
		bFound = true;
	}

	for (const FMatBP2FPExportReference& Reference : Index.References)
	{
		if ((Direction == TEXT("out") || Direction == TEXT("both")) && MatchesAsset(Reference.FromAssetPath))
		{
			const FMatBP2FPExportedAsset* Target = Index.FindAsset(Reference.ToAssetPath);
			const FString TargetLocation = Target
				? AbsoluteFile(Target->RelativeFilePath) + TEXT(":1")
				: Reference.ToAssetPath + TEXT(" (not exported)");
			UE_LOG(LogTemp, Display, TEXT("out: %s:%d %s -> %s"),
				*AbsoluteFile(Reference.SourceFile), Reference.SourceLine,
				*Reference.ExpressionId, *TargetLocation);
			bFound = true;
		}
		if ((Direction == TEXT("in") || Direction == TEXT("both")) && MatchesAsset(Reference.ToAssetPath))
		{
			UE_LOG(LogTemp, Display, TEXT("in: %s:%d %s <- %s"),
				*AbsoluteFile(Reference.SourceFile), Reference.SourceLine,
				*Reference.ExpressionId, *Reference.FromAssetPath);
			bFound = true;
		}
	}

	if (!bFound)
	{
		UE_LOG(LogTemp, Error, TEXT("Asset is not present in the index and has no references: %s"), *AssetPath);
		return 1;
	}
	return 0;
}
