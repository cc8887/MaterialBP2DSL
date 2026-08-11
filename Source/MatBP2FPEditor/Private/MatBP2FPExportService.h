// Shared headless export service and reference-index model.
#pragma once

#include "CoreMinimal.h"

enum class EMatBP2FPGraphAssetKind : uint8
{
	Material,
	MaterialFunction
};

struct FMatBP2FPExportReference
{
	FString FromAssetPath;
	FString ToAssetPath;
	FString Kind = TEXT("material-function-call");
	FString ExpressionId;
	FString SourceFile;
	int32 SourceLine = 1;
};

struct FMatBP2FPExportedAsset
{
	FString AssetPath;
	EMatBP2FPGraphAssetKind Kind = EMatBP2FPGraphAssetKind::Material;
	FString FilePath;
	FString RelativeFilePath;
	FString ContentHash;
};

struct FMatBP2FPExportOptions
{
	FString OutputDirectory;
	FString AssetFilter;
	bool bIncludeMaterials = true;
	bool bIncludeFunctions = true;
	bool bIncludeEngine = false;
};

struct FMatBP2FPExportResult
{
	TArray<FMatBP2FPExportedAsset> Assets;
	TArray<FMatBP2FPExportReference> References;
	TArray<FString> Failures;
	FString IndexFilePath;

	bool Succeeded() const { return Failures.Num() == 0; }
};

class FMatBP2FPExportService
{
public:
	static FMatBP2FPExportResult ExportAll(const FMatBP2FPExportOptions& Options);
	static FString BuildOutputPath(const FString& AssetObjectPath, const FString& OutputDirectory);
	static bool WriteIndex(const FMatBP2FPExportResult& Result, const FString& OutputDirectory, FString& OutError);
};

class FMatBP2FPReferenceIndex
{
public:
	TArray<FMatBP2FPExportedAsset> Assets;
	TArray<FMatBP2FPExportReference> References;

	static bool Load(const FString& FilePath, FMatBP2FPReferenceIndex& OutIndex, FString& OutError);
	const FMatBP2FPExportedAsset* FindAsset(const FString& AssetPath) const;
};
