// MatBP2FPPerfCommandlet.cpp - CI entry point for compile-free MatLang cost analysis
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPPerfCommandlet.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "MatLangLinter.h"
#include "MatLangStaticCostAnalyzer.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatBPPerf, Log, All);

namespace
{
	struct FLoadedDocument
	{
		FString FilePath;
		TSharedPtr<FMaterialGraphAST> AST;
	};

	struct FPerfPolicy
	{
		TOptional<int32> MaxPixelALU;
		TOptional<int32> MaxVertexALU;
		TOptional<int32> MaxPixelTextureSamples;
		TOptional<int32> MaxVertexTextureSamples;
		TOptional<uint64> MaxStaticCombinations;
		bool bConservative = false;
		bool bFailOnUnknown = false;
		bool bFailOnWarning = false;
	};

	FString RiskSeverityToString(EMatLangStaticRiskSeverity Severity)
	{
		switch (Severity)
		{
			case EMatLangStaticRiskSeverity::Info: return TEXT("info");
			case EMatLangStaticRiskSeverity::Warning: return TEXT("warning");
			default: return TEXT("unknown");
		}
	}

	TSharedPtr<FJsonObject> MetricRangeToJson(const FMatLangStaticMetricRange& Range)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("low"), Range.Low);
		Json->SetNumberField(TEXT("default"), Range.Default);
		Json->SetNumberField(TEXT("conservative"), Range.Conservative);
		return Json;
	}

	TSharedPtr<FJsonObject> StageCostToJson(const FMatLangStaticStageCost& Cost)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("reachable_nodes"), MetricRangeToJson(Cost.ReachableNodes));
		Json->SetObjectField(TEXT("texture_sample_sites"), MetricRangeToJson(Cost.TextureSampleSites));
		Json->SetObjectField(TEXT("alu_proxy"), MetricRangeToJson(Cost.ALUProxy));
		Json->SetNumberField(TEXT("default_max_depth"), Cost.DefaultMaxDepth);
		return Json;
	}

	TArray<TSharedPtr<FJsonValue>> StringArrayToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	void AddViolation(
		TArray<FString>& Violations,
		const TCHAR* Metric,
		int64 Actual,
		int64 Limit)
	{
		if (Actual > Limit)
		{
			Violations.Add(FString::Printf(TEXT("%s is %lld, budget is %lld"), Metric, Actual, Limit));
		}
	}

	TArray<FString> EvaluatePolicy(const FMatLangStaticCostReport& Report, const FPerfPolicy& Policy)
	{
		TArray<FString> Violations;
		auto Select = [&](const FMatLangStaticMetricRange& Range)
		{
			return Policy.bConservative ? Range.Conservative : Range.Default;
		};

		if (Policy.MaxPixelALU.IsSet())
		{
			AddViolation(Violations, TEXT("pixel.alu_proxy"), Select(Report.Pixel.ALUProxy), Policy.MaxPixelALU.GetValue());
		}
		if (Policy.MaxVertexALU.IsSet())
		{
			AddViolation(Violations, TEXT("vertex.alu_proxy"), Select(Report.Vertex.ALUProxy), Policy.MaxVertexALU.GetValue());
		}
		if (Policy.MaxPixelTextureSamples.IsSet())
		{
			AddViolation(Violations, TEXT("pixel.texture_sample_sites"),
				Select(Report.Pixel.TextureSampleSites), Policy.MaxPixelTextureSamples.GetValue());
		}
		if (Policy.MaxVertexTextureSamples.IsSet())
		{
			AddViolation(Violations, TEXT("vertex.texture_sample_sites"),
				Select(Report.Vertex.TextureSampleSites), Policy.MaxVertexTextureSamples.GetValue());
		}
		if (Policy.MaxStaticCombinations.IsSet()
			&& (Report.bStaticCombinationCountCapped
				|| Report.TheoreticalStaticCombinations > Policy.MaxStaticCombinations.GetValue()))
		{
			Violations.Add(FString::Printf(TEXT("theoretical_static_combinations is %s, budget is %llu"),
				Report.bStaticCombinationCountCapped ? TEXT("capped") : *LexToString(Report.TheoreticalStaticCombinations),
				Policy.MaxStaticCombinations.GetValue()));
		}
		if (Policy.bFailOnUnknown && Report.bHasUnknownCost)
		{
			Violations.Add(TEXT("analysis contains unknown cost"));
		}
		if (Policy.bFailOnWarning)
		{
			for (const FMatLangStaticCostRisk& Risk : Report.Risks)
			{
				if (Risk.Severity == EMatLangStaticRiskSeverity::Warning)
				{
					Violations.Add(FString::Printf(TEXT("warning %s: %s"), *Risk.RuleId, *Risk.Message));
				}
			}
		}
		return Violations;
	}

	TSharedPtr<FJsonObject> ReportToJson(
		const FMatLangStaticCostReport& Report,
		const FMaterialGraphAST& AST,
		const FString& SourceFile,
		const TArray<FString>& Violations)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("source_file"), SourceFile);
		Json->SetStringField(TEXT("name"), Report.Name);
		Json->SetStringField(TEXT("asset_path"), Report.AssetPath);
		Json->SetStringField(TEXT("kind"), Report.Kind == EMatLangGraphKind::MaterialFunction
			? TEXT("material-function") : TEXT("material"));
		if (Report.Kind == EMatLangGraphKind::Material)
		{
			Json->SetStringField(TEXT("domain"), MatLangEnums::DomainToString(AST.Domain));
			Json->SetStringField(TEXT("blend_mode"), MatLangEnums::BlendModeToString(AST.BlendMode));
			Json->SetStringField(TEXT("shading_model"), MatLangEnums::ShadingModelToString(AST.ShadingModel));
			Json->SetBoolField(TEXT("two_sided"), AST.bTwoSided);
		}

		TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
		Graph->SetNumberField(TEXT("declared_nodes"), Report.DeclaredNodes);
		Graph->SetNumberField(TEXT("declared_edges"), Report.DeclaredEdges);
		Graph->SetNumberField(TEXT("dead_nodes"), Report.DeadNodes);
		Graph->SetNumberField(TEXT("max_fan_out"), Report.MaxFanOut);
		Json->SetObjectField(TEXT("graph"), Graph);
		Json->SetObjectField(TEXT("overall"), StageCostToJson(Report.Overall));
		Json->SetObjectField(TEXT("vertex"), StageCostToJson(Report.Vertex));
		Json->SetObjectField(TEXT("pixel"), StageCostToJson(Report.Pixel));

		TSharedPtr<FJsonObject> Functions = MakeShared<FJsonObject>();
		Functions->SetNumberField(TEXT("default_call_sites"), Report.DefaultFunctionCallSites);
		Functions->SetNumberField(TEXT("conservative_call_sites"), Report.ConservativeFunctionCallSites);
		Functions->SetNumberField(TEXT("unresolved_calls"), Report.UnresolvedFunctionCalls);
		Functions->SetNumberField(TEXT("recursive_calls"), Report.RecursiveFunctionCalls);
		Json->SetObjectField(TEXT("functions"), Functions);

		TSharedPtr<FJsonObject> StaticSwitches = MakeShared<FJsonObject>();
		StaticSwitches->SetArrayField(TEXT("parameters"), StringArrayToJson(Report.StaticParameters));
		StaticSwitches->SetStringField(TEXT("theoretical_combinations"),
			Report.bStaticCombinationCountCapped ? TEXT("capped") : LexToString(Report.TheoreticalStaticCombinations));
		Json->SetObjectField(TEXT("static_switches"), StaticSwitches);

		TSharedPtr<FJsonObject> Textures = MakeShared<FJsonObject>();
		Textures->SetArrayField(TEXT("default_assets"), StringArrayToJson(Report.DefaultTextureAssets));
		Textures->SetArrayField(TEXT("conservative_assets"), StringArrayToJson(Report.ConservativeTextureAssets));
		Json->SetObjectField(TEXT("textures"), Textures);

		TArray<TSharedPtr<FJsonValue>> RiskValues;
		for (const FMatLangStaticCostRisk& Risk : Report.Risks)
		{
			TSharedPtr<FJsonObject> RiskJson = MakeShared<FJsonObject>();
			RiskJson->SetStringField(TEXT("rule"), Risk.RuleId);
			RiskJson->SetStringField(TEXT("severity"), RiskSeverityToString(Risk.Severity));
			RiskJson->SetStringField(TEXT("message"), Risk.Message);
			RiskJson->SetStringField(TEXT("expression"), Risk.ExpressionId);
			if (Risk.SourceSpan.IsValid())
			{
				RiskJson->SetNumberField(TEXT("line"), Risk.SourceSpan.StartLine);
				RiskJson->SetNumberField(TEXT("column"), Risk.SourceSpan.StartColumn);
			}
			RiskValues.Add(MakeShared<FJsonValueObject>(RiskJson));
		}
		Json->SetArrayField(TEXT("risks"), RiskValues);
		Json->SetBoolField(TEXT("has_unknown_cost"), Report.bHasUnknownCost);
		Json->SetArrayField(TEXT("policy_violations"), StringArrayToJson(Violations));
		Json->SetStringField(TEXT("verdict"), Violations.Num() > 0
			? TEXT("fail") : (Report.bHasUnknownCost ? TEXT("unknown") : (Report.Risks.Num() == 0 ? TEXT("pass") : TEXT("warning"))));
		return Json;
	}

	bool ParseIntBudget(const TMap<FString, FString>& Params, const TCHAR* Name, TOptional<int32>& OutValue, FString& OutError)
	{
		const FString* Value = Params.Find(Name);
		if (!Value) return true;
		if (!Value->IsNumeric() || FCString::Atoi(**Value) < 0)
		{
			OutError = FString::Printf(TEXT("-%s must be a non-negative integer"), Name);
			return false;
		}
		OutValue = FCString::Atoi(**Value);
		return true;
	}

	bool ParseUint64Budget(const TMap<FString, FString>& Params, const TCHAR* Name, TOptional<uint64>& OutValue, FString& OutError)
	{
		const FString* Value = Params.Find(Name);
		if (!Value) return true;
		for (int32 Index = 0; Index < Value->Len(); ++Index)
		{
			if ((*Value)[Index] < '0' || (*Value)[Index] > '9')
			{
				OutError = FString::Printf(TEXT("-%s must be a non-negative integer"), Name);
				return false;
			}
		}
		if (Value->IsEmpty())
		{
			OutError = FString::Printf(TEXT("-%s must not be empty"), Name);
			return false;
		}
		OutValue = FCString::Strtoui64(**Value, nullptr, 10);
		return true;
	}
}

UMatBP2FPPerfCommandlet::UMatBP2FPPerfCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMatBP2FPPerfCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString* PathParam = ParamMap.Find(TEXT("path"));
	if (!PathParam || PathParam->TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogMatBPPerf, Error, TEXT("Usage: -run=MatBP2FPPerf -path=<file-or-directory> [-output=<json-file>] [-conservative] [budget options]"));
		return 2;
	}

	FPerfPolicy Policy;
	Policy.bConservative = Switches.Contains(TEXT("conservative"));
	Policy.bFailOnUnknown = Switches.Contains(TEXT("fail-on-unknown"));
	Policy.bFailOnWarning = Switches.Contains(TEXT("fail-on-warning"));
	FString PolicyError;
	if (!ParseIntBudget(ParamMap, TEXT("max-pixel-alu"), Policy.MaxPixelALU, PolicyError)
		|| !ParseIntBudget(ParamMap, TEXT("max-vertex-alu"), Policy.MaxVertexALU, PolicyError)
		|| !ParseIntBudget(ParamMap, TEXT("max-pixel-texture-samples"), Policy.MaxPixelTextureSamples, PolicyError)
		|| !ParseIntBudget(ParamMap, TEXT("max-vertex-texture-samples"), Policy.MaxVertexTextureSamples, PolicyError)
		|| !ParseUint64Budget(ParamMap, TEXT("max-static-combinations"), Policy.MaxStaticCombinations, PolicyError))
	{
		UE_LOG(LogMatBPPerf, Error, TEXT("%s"), *PolicyError);
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
		UE_LOG(LogMatBPPerf, Error, TEXT("Input path does not exist: %s"), *InputPath);
		return 2;
	}
	Files.Sort();
	if (Files.Num() == 0)
	{
		UE_LOG(LogMatBPPerf, Error, TEXT("No .matlang files found under: %s"), *InputPath);
		return 2;
	}

	TArray<FLoadedDocument> Documents;
	TArray<TSharedPtr<FJsonValue>> ParseErrors;
	bool bInvalidDSL = false;
	for (const FString& File : Files)
	{
		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *File))
		{
			UE_LOG(LogMatBPPerf, Error, TEXT("Failed to read: %s"), *File);
			return 2;
		}

		const FMatLangLintResult Lint = FMatLangLinter::Lint(Source, File);
		if (Lint.HasErrors() || !Lint.AST.IsValid())
		{
			bInvalidDSL = true;
			for (const FMatLangDiagnostic& Diagnostic : Lint.Diagnostics)
			{
				UE_LOG(LogMatBPPerf, Error, TEXT("%s"), *Diagnostic.ToString());
				TSharedPtr<FJsonObject> ErrorJson = MakeShared<FJsonObject>();
				ErrorJson->SetStringField(TEXT("file"), File);
				ErrorJson->SetStringField(TEXT("rule"), Diagnostic.RuleId);
				ErrorJson->SetStringField(TEXT("message"), Diagnostic.Message);
				ErrorJson->SetNumberField(TEXT("line"), Diagnostic.Span.StartLine);
				ErrorJson->SetNumberField(TEXT("column"), Diagnostic.Span.StartColumn);
				ParseErrors.Add(MakeShared<FJsonValueObject>(ErrorJson));
			}
			continue;
		}

		FLoadedDocument& Document = Documents.AddDefaulted_GetRef();
		Document.FilePath = File;
		Document.AST = Lint.AST;
	}

	TMap<FString, TSharedPtr<FMaterialGraphAST>> GraphsByAssetPath;
	for (const FLoadedDocument& Document : Documents)
	{
		if (Document.AST->AssetPath.IsEmpty()) continue;
		if (GraphsByAssetPath.Contains(Document.AST->AssetPath))
		{
			UE_LOG(LogMatBPPerf, Error, TEXT("Duplicate :asset-path in analyzed DSL set: %s"), *Document.AST->AssetPath);
			bInvalidDSL = true;
			continue;
		}
		GraphsByAssetPath.Add(Document.AST->AssetPath, Document.AST);
		const FString PackagePath = FPackageName::ObjectPathToPackageName(Document.AST->AssetPath);
		if (!PackagePath.IsEmpty()) GraphsByAssetPath.FindOrAdd(PackagePath, Document.AST);
	}

	const FMatLangStaticCostAnalyzer::FFunctionResolver Resolver =
		[&GraphsByAssetPath](const FString& AssetPath) -> TSharedPtr<FMaterialGraphAST>
		{
			if (const TSharedPtr<FMaterialGraphAST>* Exact = GraphsByAssetPath.Find(AssetPath)) return *Exact;
			const FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			if (const TSharedPtr<FMaterialGraphAST>* Package = GraphsByAssetPath.Find(PackagePath)) return *Package;
			return nullptr;
		};

	TArray<TSharedPtr<FJsonValue>> ReportValues;
	bool bPolicyFailed = false;
	for (const FLoadedDocument& Document : Documents)
	{
		const FMatLangStaticCostReport Report = FMatLangStaticCostAnalyzer::Analyze(Document.AST, Resolver);
		const TArray<FString> Violations = EvaluatePolicy(Report, Policy);
		bPolicyFailed |= Violations.Num() > 0;
		ReportValues.Add(MakeShared<FJsonValueObject>(ReportToJson(Report, *Document.AST, Document.FilePath, Violations)));

		const int32 PixelALU = Policy.bConservative ? Report.Pixel.ALUProxy.Conservative : Report.Pixel.ALUProxy.Default;
		const int32 PixelSamples = Policy.bConservative
			? Report.Pixel.TextureSampleSites.Conservative : Report.Pixel.TextureSampleSites.Default;
		UE_LOG(LogMatBPPerf, Display,
			TEXT("%s: pixel ALU proxy=%d, pixel texture sites=%d, dead nodes=%d, risks=%d, verdict=%s"),
			*Document.AST->Name, PixelALU, PixelSamples, Report.DeadNodes, Report.Risks.Num(),
			Violations.Num() == 0 ? (Report.bHasUnknownCost ? TEXT("unknown") : TEXT("pass")) : TEXT("fail"));
		for (const FString& Violation : Violations)
		{
			UE_LOG(LogMatBPPerf, Error, TEXT("%s: %s"), *Document.AST->Name, *Violation);
		}
	}

	FString OutputPath = FPaths::ProjectSavedDir() / TEXT("MatBP2FP/Reports/matlang-static-cost.json");
	if (const FString* OutputParam = ParamMap.Find(TEXT("output")))
	{
		OutputPath = FPaths::IsRelative(*OutputParam)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / *OutputParam)
			: FPaths::ConvertRelativePathToFull(*OutputParam);
	}
	FPaths::NormalizeFilename(OutputPath);
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true))
	{
		UE_LOG(LogMatBPPerf, Error, TEXT("Failed to create report directory: %s"), *FPaths::GetPath(OutputPath));
		return 2;
	}

	TSharedPtr<FJsonObject> RootJson = MakeShared<FJsonObject>();
	RootJson->SetNumberField(TEXT("schema_version"), 1);
	RootJson->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
	RootJson->SetStringField(TEXT("analysis"), TEXT("compile-free MatLang static cost"));
	RootJson->SetStringField(TEXT("policy_scenario"), Policy.bConservative ? TEXT("conservative") : TEXT("default"));
	RootJson->SetArrayField(TEXT("parse_errors"), ParseErrors);
	RootJson->SetArrayField(TEXT("reports"), ReportValues);

	FString JsonText;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(RootJson.ToSharedRef(), Writer)
		|| !FFileHelper::SaveStringToFile(JsonText, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogMatBPPerf, Error, TEXT("Failed to write report: %s"), *OutputPath);
		return 2;
	}

	UE_LOG(LogMatBPPerf, Display, TEXT("Wrote %d static cost report(s) to %s"), ReportValues.Num(), *OutputPath);
	return (bInvalidDSL || bPolicyFailed) ? 1 : 0;
}
