// Shared headless export service and reference-index implementation.

#include "MatBP2FPExportService.h"

#include "MatBP2FPVersionCompat.h"
#include "MatBPExporter.h"
#include "FMatBP2FPMappingRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString KindToString(EMatBP2FPGraphAssetKind Kind)
	{
		return Kind == EMatBP2FPGraphAssetKind::MaterialFunction
			? TEXT("material-function") : TEXT("material");
	}

	EMatBP2FPGraphAssetKind StringToKind(const FString& Kind)
	{
		return Kind == TEXT("material-function")
			? EMatBP2FPGraphAssetKind::MaterialFunction : EMatBP2FPGraphAssetKind::Material;
	}

	bool SaveAtomic(const FString& FilePath, const FString& Contents, FString& OutError)
	{
		const FString Directory = FPaths::GetPath(FilePath);
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			OutError = FString::Printf(TEXT("Failed to create directory: %s"), *Directory);
			return false;
		}

		const FString TemporaryPath = FilePath + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(Contents, *TemporaryPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write temporary file: %s"), *TemporaryPath);
			return false;
		}
		if (!IFileManager::Get().Move(*FilePath, *TemporaryPath, true, true, false, true))
		{
			IFileManager::Get().Delete(*TemporaryPath, false, true);
			OutError = FString::Printf(TEXT("Failed to replace output file: %s"), *FilePath);
			return false;
		}
		return true;
	}

	FString HashContents(const FString& Contents)
	{
		FTCHARToUTF8 Utf8(*Contents);
		uint8 Hash[FSHA1::DigestSize];
		FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
		return BytesToHex(Hash, FSHA1::DigestSize).ToLower();
	}

	FString ExtractAssetPath(const FString& Value)
	{
		FString Trimmed = Value.TrimStartAndEnd();
		const FString Prefix = TEXT("(asset \"");
		const FString Suffix = TEXT("\")");
		if (!Trimmed.StartsWith(Prefix) || !Trimmed.EndsWith(Suffix))
		{
			return FString();
		}
		return Trimmed.Mid(Prefix.Len(), Trimmed.Len() - Prefix.Len() - Suffix.Len());
	}

	int32 FindExpressionLine(const FString& DSL, const FString& ExpressionType, const FString& ExpressionId)
	{
		TArray<FString> Lines;
		DSL.ParseIntoArrayLines(Lines, false);
		const FString Definition = TEXT("(") + ExpressionType + TEXT(" ") + ExpressionId;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Lines[Index].Contains(Definition))
			{
				return Index + 1;
			}
		}
		return 1;
	}

	bool IsSelected(const FAssetData& Asset, const FMatBP2FPExportOptions& Options)
	{
		const FString PackagePath = Asset.PackageName.ToString();
		if (!Options.bIncludeEngine && !FMatBP2FPMappingRegistry::IsExportablePackage(PackagePath))
		{
			return false;
		}
		return Options.AssetFilter.IsEmpty() || PackagePath.StartsWith(Options.AssetFilter);
	}

	void CollectReferences(
		const TSharedPtr<FMaterialGraphAST>& AST,
		const FString& DSL,
		const FString& RelativeFile,
		TArray<FMatBP2FPExportReference>& OutReferences)
	{
		for (const TSharedPtr<FMatExpressionAST>& Expression : AST->Expressions)
		{
			if (!Expression.IsValid() || Expression->ExprType != TEXT("material-function-call"))
			{
				continue;
			}
			const FString* FunctionValue = Expression->Properties.Find(TEXT("function"));
			if (!FunctionValue) continue;
			const FString TargetPath = ExtractAssetPath(*FunctionValue);
			if (TargetPath.IsEmpty()) continue;

			FMatBP2FPExportReference Reference;
			Reference.FromAssetPath = AST->AssetPath;
			Reference.ToAssetPath = TargetPath;
			Reference.ExpressionId = Expression->Id;
			Reference.SourceFile = RelativeFile;
			Reference.SourceLine = FindExpressionLine(DSL, Expression->ExprType, Expression->Id);
			OutReferences.Add(MoveTemp(Reference));
		}
	}

	bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutValue)
	{
		return Object.IsValid() && Object->TryGetStringField(Field, OutValue);
	}
}

FString FMatBP2FPExportService::BuildOutputPath(
	const FString& AssetObjectPath,
	const FString& OutputDirectory)
{
	const FString PackagePath = FPackageName::ObjectPathToPackageName(AssetObjectPath);
	if (!PackagePath.StartsWith(TEXT("/")) || PackagePath.Len() <= 1)
	{
		return FString();
	}
	const FString RelativePath = PackagePath.RightChop(1) + TEXT(".matlang");
	FString Result = FPaths::ConvertRelativePathToFull(OutputDirectory / RelativePath);
	FPaths::NormalizeFilename(Result);
	return Result;
}

FMatBP2FPExportResult FMatBP2FPExportService::ExportAll(const FMatBP2FPExportOptions& Options)
{
	FMatBP2FPExportResult Result;
	FString OutputDirectory = FPaths::ConvertRelativePathToFull(Options.OutputDirectory);
	FPaths::NormalizeDirectoryName(OutputDirectory);
	if (OutputDirectory.IsEmpty())
	{
		Result.Failures.Add(TEXT("Output directory is empty"));
		return Result;
	}

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();
	Registry.SearchAllAssets(true);

	struct FCandidate
	{
		FAssetData Asset;
		EMatBP2FPGraphAssetKind Kind;
	};
	TArray<FCandidate> Candidates;
	if (Options.bIncludeMaterials)
	{
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(MATBP2FP_ASSET_CLASS(UMaterial), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (IsSelected(Asset, Options)) Candidates.Add({Asset, EMatBP2FPGraphAssetKind::Material});
		}
	}
	if (Options.bIncludeFunctions)
	{
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(MATBP2FP_ASSET_CLASS(UMaterialFunction), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (IsSelected(Asset, Options)) Candidates.Add({Asset, EMatBP2FPGraphAssetKind::MaterialFunction});
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.Asset.PackageName.ToString() < B.Asset.PackageName.ToString();
	});

	for (const FCandidate& Candidate : Candidates)
	{
		UObject* Object = Candidate.Asset.GetAsset();
		TSharedPtr<FMaterialGraphAST> AST;
		if (Candidate.Kind == EMatBP2FPGraphAssetKind::Material)
		{
			AST = FMatBPExporter::ExportToAST(Cast<UMaterial>(Object));
		}
		else
		{
			AST = FMatBPExporter::ExportMaterialFunctionToAST(Cast<UMaterialFunction>(Object));
		}

		if (!AST.IsValid())
		{
			Result.Failures.Add(FString::Printf(TEXT("Failed to export asset: %s"),
				*Candidate.Asset.PackageName.ToString()));
			continue;
		}

		const FString DSL = AST->ToString();
		const FString FilePath = BuildOutputPath(AST->AssetPath, OutputDirectory);
		if (FilePath.IsEmpty())
		{
			Result.Failures.Add(FString::Printf(TEXT("Failed to map output path: %s"), *AST->AssetPath));
			continue;
		}

		FString WriteError;
		if (!SaveAtomic(FilePath, DSL, WriteError))
		{
			Result.Failures.Add(WriteError);
			continue;
		}

		FString RelativeFile = FilePath;
		FString RelativeBase = OutputDirectory / TEXT("");
		FPaths::MakePathRelativeTo(RelativeFile, *RelativeBase);
		FPaths::NormalizeFilename(RelativeFile);

		FMatBP2FPExportedAsset Exported;
		Exported.AssetPath = AST->AssetPath;
		Exported.Kind = Candidate.Kind;
		Exported.FilePath = FilePath;
		Exported.RelativeFilePath = RelativeFile;
		Exported.ContentHash = HashContents(DSL);
		Result.Assets.Add(MoveTemp(Exported));
		CollectReferences(AST, DSL, RelativeFile, Result.References);
	}

	Result.References.Sort([](const FMatBP2FPExportReference& A, const FMatBP2FPExportReference& B)
	{
		if (A.FromAssetPath != B.FromAssetPath) return A.FromAssetPath < B.FromAssetPath;
		if (A.ToAssetPath != B.ToAssetPath) return A.ToAssetPath < B.ToAssetPath;
		return A.ExpressionId < B.ExpressionId;
	});

	FString IndexError;
	if (!WriteIndex(Result, OutputDirectory, IndexError))
	{
		Result.Failures.Add(IndexError);
	}
	Result.IndexFilePath = OutputDirectory / TEXT("matlang-index.json");
	FPaths::NormalizeFilename(Result.IndexFilePath);
	return Result;
}

bool FMatBP2FPExportService::WriteIndex(
	const FMatBP2FPExportResult& Result,
	const FString& OutputDirectory,
	FString& OutError)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("format_version"), 1);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> AssetsJson;
	for (const FMatBP2FPExportedAsset& Asset : Result.Assets)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("asset_path"), Asset.AssetPath);
		Item->SetStringField(TEXT("kind"), KindToString(Asset.Kind));
		Item->SetStringField(TEXT("file"), Asset.RelativeFilePath);
		Item->SetStringField(TEXT("content_sha1"), Asset.ContentHash);
		AssetsJson.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("assets"), AssetsJson);

	TArray<TSharedPtr<FJsonValue>> ReferencesJson;
	for (const FMatBP2FPExportReference& Reference : Result.References)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("from"), Reference.FromAssetPath);
		Item->SetStringField(TEXT("to"), Reference.ToAssetPath);
		Item->SetStringField(TEXT("kind"), Reference.Kind);
		Item->SetStringField(TEXT("expression_id"), Reference.ExpressionId);
		Item->SetStringField(TEXT("source_file"), Reference.SourceFile);
		Item->SetNumberField(TEXT("source_line"), Reference.SourceLine);
		ReferencesJson.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("references"), ReferencesJson);

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("Failed to serialize reference index");
		return false;
	}
	return SaveAtomic(OutputDirectory / TEXT("matlang-index.json"), Json, OutError);
}

bool FMatBP2FPReferenceIndex::Load(
	const FString& FilePath,
	FMatBP2FPReferenceIndex& OutIndex,
	FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read index: %s"), *FilePath);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid reference index JSON: %s"), *FilePath);
		return false;
	}

	OutIndex.Assets.Reset();
	OutIndex.References.Reset();
	const TArray<TSharedPtr<FJsonValue>>* AssetValues = nullptr;
	if (Root->TryGetArrayField(TEXT("assets"), AssetValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AssetValues)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			FMatBP2FPExportedAsset Asset;
			FString Kind;
			if (!ReadString(Item, TEXT("asset_path"), Asset.AssetPath) ||
				!ReadString(Item, TEXT("kind"), Kind) ||
				!ReadString(Item, TEXT("file"), Asset.RelativeFilePath))
			{
				OutError = TEXT("Reference index contains an invalid asset entry");
				return false;
			}
			Asset.Kind = StringToKind(Kind);
			Item->TryGetStringField(TEXT("content_sha1"), Asset.ContentHash);
			OutIndex.Assets.Add(MoveTemp(Asset));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ReferenceValues = nullptr;
	if (Root->TryGetArrayField(TEXT("references"), ReferenceValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ReferenceValues)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			FMatBP2FPExportReference Reference;
			if (!ReadString(Item, TEXT("from"), Reference.FromAssetPath) ||
				!ReadString(Item, TEXT("to"), Reference.ToAssetPath))
			{
				OutError = TEXT("Reference index contains an invalid reference entry");
				return false;
			}
			Item->TryGetStringField(TEXT("kind"), Reference.Kind);
			Item->TryGetStringField(TEXT("expression_id"), Reference.ExpressionId);
			Item->TryGetStringField(TEXT("source_file"), Reference.SourceFile);
			double SourceLine = 1.0;
			Item->TryGetNumberField(TEXT("source_line"), SourceLine);
			Reference.SourceLine = FMath::Max(1, static_cast<int32>(SourceLine));
			OutIndex.References.Add(MoveTemp(Reference));
		}
	}
	return true;
}

const FMatBP2FPExportedAsset* FMatBP2FPReferenceIndex::FindAsset(const FString& AssetPath) const
{
	return Assets.FindByPredicate([&](const FMatBP2FPExportedAsset& Asset)
	{
		return Asset.AssetPath == AssetPath ||
			FPackageName::ObjectPathToPackageName(Asset.AssetPath) == AssetPath;
	});
}
