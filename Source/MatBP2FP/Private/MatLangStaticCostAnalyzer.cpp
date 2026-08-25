// MatLangStaticCostAnalyzer.cpp - Compile-free performance analysis for MatLang graphs
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangStaticCostAnalyzer.h"

namespace
{
	enum class ETraversalMode : uint8
	{
		Low,
		Default,
		Conservative
	};

	struct FCostAccumulator
	{
		TSet<FString> VisitedNodes;
		TSet<FString> RootGraphNodes;
		TSet<FString> TextureAssets;
		TSet<FString> StaticParameters;
		int32 TextureSampleSites = 0;
		int32 ALUProxy = 0;
		int32 MaxDepth = 0;
		int32 FunctionCallSites = 0;
	};

	const FMatLangInput* FindInputIgnoreCase(const FMatExpressionAST& Expression, const TCHAR* Name)
	{
		for (const FMatLangInput& Input : Expression.Inputs)
		{
			if (Input.Name.Equals(Name, ESearchCase::IgnoreCase))
			{
				return &Input;
			}
		}
		return nullptr;
	}

	FString Unquote(const FString& Value)
	{
		FString Result = Value.TrimStartAndEnd();
		if (Result.Len() >= 2 && Result.StartsWith(TEXT("\"")) && Result.EndsWith(TEXT("\"")))
		{
			Result = Result.Mid(1, Result.Len() - 2);
		}
		return Result;
	}

	FString ExtractAssetPath(const FString& Value)
	{
		FString Result = Value.TrimStartAndEnd();
		if (Result.StartsWith(TEXT("(asset")))
		{
			const int32 FirstQuote = Result.Find(TEXT("\""));
			const int32 LastQuote = Result.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (FirstQuote != INDEX_NONE && LastQuote > FirstQuote)
			{
				return Result.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
			}
		}
		return Unquote(Result);
	}

	bool IsTextureSample(const FString& Type)
	{
		return Type.Contains(TEXT("texture-sample"), ESearchCase::IgnoreCase)
			&& !Type.Contains(TEXT("texture-object"), ESearchCase::IgnoreCase);
	}

	bool IsStaticSwitch(const FString& Type)
	{
		return Type.Contains(TEXT("static-switch"), ESearchCase::IgnoreCase);
	}

	int32 NodeALUWeight(const FString& Type)
	{
		const FString T = Type.ToLower();
		if (T.Contains(TEXT("constant")) || T.Contains(TEXT("parameter"))
			|| T.Contains(TEXT("texture-coordinate")) || T.Contains(TEXT("texture-object"))
			|| T.Contains(TEXT("texture-sample")) || T.Contains(TEXT("reroute"))
			|| T.Contains(TEXT("function-input")) || T.Contains(TEXT("function-output"))
			|| T.Contains(TEXT("material-function-call")) || T.Contains(TEXT("static-switch")))
		{
			return 0;
		}
		if (T == TEXT("add") || T == TEXT("subtract") || T == TEXT("multiply")
			|| T == TEXT("abs") || T == TEXT("one-minus") || T == TEXT("min")
			|| T == TEXT("max") || T == TEXT("clamp") || T == TEXT("component-mask")
			|| T == TEXT("append-vector") || T == TEXT("saturate"))
		{
			return 1;
		}
		if (T == TEXT("divide") || T == TEXT("dot-product") || T == TEXT("cross-product")
			|| T == TEXT("linear-interpolate") || T == TEXT("floor") || T == TEXT("ceil")
			|| T == TEXT("frac") || T == TEXT("if"))
		{
			return 2;
		}
		if (T.Contains(TEXT("distance")) || T.Contains(TEXT("transform"))
			|| T.Contains(TEXT("desaturation")))
		{
			return 4;
		}
		if (T.Contains(TEXT("normalize")) || T.Contains(TEXT("square-root")) || T == TEXT("sqrt"))
		{
			return 6;
		}
		if (T.Contains(TEXT("power")) || T.Contains(TEXT("sine")) || T.Contains(TEXT("cosine"))
			|| T.Contains(TEXT("fresnel")))
		{
			return 8;
		}
		if (T.Contains(TEXT("noise")))
		{
			return 16;
		}
		return T.Contains(TEXT("custom")) ? 0 : 1;
	}

	bool IsVertexOutput(const FString& OutputName)
	{
		return OutputName.Equals(TEXT("world-position-offset"), ESearchCase::IgnoreCase)
			|| OutputName.Equals(TEXT("displacement"), ESearchCase::IgnoreCase)
			|| OutputName.StartsWith(TEXT("customized-uv"), ESearchCase::IgnoreCase);
	}

	class FAnalyzerWorker
	{
	public:
		FAnalyzerWorker(
			const TSharedPtr<FMaterialGraphAST>& InRoot,
			const FMatLangStaticCostAnalyzer::FFunctionResolver& InResolver,
			const FMatLangStaticCostOptions& InOptions,
			FMatLangStaticCostReport& InReport)
			: Root(InRoot)
			, Resolver(InResolver)
			, Options(InOptions)
			, Report(InReport)
		{
			RootPrefix = Root->AssetPath.IsEmpty() ? Root->Name : Root->AssetPath;
		}

		void Run()
		{
			AddMaterialRisks();

			TArray<FString> OverallRoots;
			TArray<FString> VertexRoots;
			TArray<FString> PixelRoots;
			CollectRoots(*Root, OverallRoots);

			if (Root->Kind == EMatLangGraphKind::Material)
			{
				for (const auto& Pair : Root->Outputs.Slots)
				{
					if (!Pair.Value.IsConnected()) continue;
					(IsVertexOutput(Pair.Key) ? VertexRoots : PixelRoots).AddUnique(Pair.Value.Connection->TargetId);
				}
			}

			FCostAccumulator OverallLow = Traverse(OverallRoots, ETraversalMode::Low, false);
			FCostAccumulator OverallDefault = Traverse(OverallRoots, ETraversalMode::Default, false);
			FCostAccumulator OverallConservative = Traverse(OverallRoots, ETraversalMode::Conservative, true);
			Report.Overall = MakeStageCost(OverallLow, OverallDefault, OverallConservative);
			Report.DeadNodes = FMath::Max(0, Report.DeclaredNodes - OverallConservative.RootGraphNodes.Num());
			Report.DefaultFunctionCallSites = OverallDefault.FunctionCallSites;
			Report.ConservativeFunctionCallSites = OverallConservative.FunctionCallSites;

			if (Root->Kind == EMatLangGraphKind::Material)
			{
				Report.Vertex = AnalyzeStage(VertexRoots);
				Report.Pixel = AnalyzeStage(PixelRoots);
			}

			Report.DefaultTextureAssets = OverallDefault.TextureAssets.Array();
			Report.ConservativeTextureAssets = OverallConservative.TextureAssets.Array();
			Report.StaticParameters = OverallConservative.StaticParameters.Array();
			Report.DefaultTextureAssets.Sort();
			Report.ConservativeTextureAssets.Sort();
			Report.StaticParameters.Sort();

			const int32 NumStaticParameters = Report.StaticParameters.Num();
			if (NumStaticParameters >= 63)
			{
				Report.TheoreticalStaticCombinations = MAX_uint64;
				Report.bStaticCombinationCountCapped = true;
			}
			else
			{
				Report.TheoreticalStaticCombinations = uint64(1) << NumStaticParameters;
			}

			Report.Risks.StableSort([](const FMatLangStaticCostRisk& A, const FMatLangStaticCostRisk& B)
			{
				if (A.SourceSpan.StartLine != B.SourceSpan.StartLine) return A.SourceSpan.StartLine < B.SourceSpan.StartLine;
				if (A.ExpressionId != B.ExpressionId) return A.ExpressionId < B.ExpressionId;
				return A.RuleId < B.RuleId;
			});
		}

	private:
		TSharedPtr<FMaterialGraphAST> Root;
		const FMatLangStaticCostAnalyzer::FFunctionResolver& Resolver;
		const FMatLangStaticCostOptions& Options;
		FMatLangStaticCostReport& Report;
		FString RootPrefix;
		TSet<FString> RiskKeys;

		void AddRisk(
			const FString& RuleId,
			EMatLangStaticRiskSeverity Severity,
			const FString& Message,
			const FMatExpressionAST* Expression = nullptr,
			const FString& QualifiedId = FString())
		{
			const FString ExpressionId = QualifiedId.IsEmpty() && Expression ? Expression->Id : QualifiedId;
			const FString Key = RuleId + TEXT("|") + ExpressionId + TEXT("|") + Message;
			if (RiskKeys.Contains(Key)) return;
			RiskKeys.Add(Key);

			FMatLangStaticCostRisk Risk;
			Risk.RuleId = RuleId;
			Risk.Severity = Severity;
			Risk.Message = Message;
			Risk.ExpressionId = ExpressionId;
			if (Expression) Risk.SourceSpan = Expression->SourceSpan;
			Report.Risks.Add(MoveTemp(Risk));
			if (Severity == EMatLangStaticRiskSeverity::Unknown)
			{
				Report.bHasUnknownCost = true;
			}
		}

		void AddMaterialRisks()
		{
			if (Root->Kind != EMatLangGraphKind::Material) return;

			if (Root->BlendMode != EMatLangBlendMode::Opaque && Root->BlendMode != EMatLangBlendMode::Masked)
			{
				AddRisk(TEXT("MP2001"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Translucent-family blend mode has scene-dependent overdraw and sorting cost"));
			}
			if (Root->bTwoSided)
			{
				AddRisk(TEXT("MP2002"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Two-sided rendering can increase rasterized fragment work"));
			}
			if (Root->bIsMasked || Root->BlendMode == EMatLangBlendMode::Masked)
			{
				AddRisk(TEXT("MP2003"), EMatLangStaticRiskSeverity::Info,
					TEXT("Masked rendering cost depends on alpha-tested coverage"));
			}
			if (Root->Domain == EMatLangDomain::PostProcess)
			{
				AddRisk(TEXT("MP2004"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Post-process materials can execute over a large portion of the screen"));
			}
			else if (Root->Domain == EMatLangDomain::DeferredDecal || Root->Domain == EMatLangDomain::Volume)
			{
				AddRisk(TEXT("MP2005"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Material domain has scene-volume or decal coverage cost"));
			}
			if (Root->ShadingModel != EMatLangShadingModel::Unlit
				&& Root->ShadingModel != EMatLangShadingModel::DefaultLit)
			{
				AddRisk(TEXT("MP2006"), EMatLangStaticRiskSeverity::Info,
					TEXT("Specialized shading model requires platform compilation for a reliable cost"));
			}

			if (Root->Outputs.Slots.Contains(TEXT("world-position-offset")))
			{
				AddRisk(TEXT("MP2101"), EMatLangStaticRiskSeverity::Warning,
					TEXT("World Position Offset adds vertex-stage work and can affect shadows and bounds"));
			}
			if (Root->Outputs.Slots.Contains(TEXT("pixel-depth-offset")))
			{
				AddRisk(TEXT("MP2102"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Pixel Depth Offset can reduce early depth rejection efficiency"));
			}
			if (Root->Outputs.Slots.Contains(TEXT("refraction")))
			{
				AddRisk(TEXT("MP2103"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Refraction introduces scene-dependent sampling cost"));
			}
		}

		void InspectExpressionRisk(
			const FMatExpressionAST& Expression,
			const FString& QualifiedId)
		{
			const FString Type = Expression.ExprType.ToLower();
			if (Type.Contains(TEXT("custom")))
			{
				AddRisk(TEXT("MP3001"), EMatLangStaticRiskSeverity::Unknown,
					TEXT("Custom HLSL cost cannot be determined from graph structure"), &Expression, QualifiedId);
			}
			if (Type.Contains(TEXT("scene-texture")) || Type.Contains(TEXT("scene-depth"))
				|| Type.Contains(TEXT("depth-fade")))
			{
				AddRisk(TEXT("MP3002"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Scene texture or depth access has screen-space bandwidth cost"), &Expression, QualifiedId);
			}
			if (Type.Contains(TEXT("noise")))
			{
				AddRisk(TEXT("MP3003"), EMatLangStaticRiskSeverity::Warning,
					TEXT("Procedural noise is commonly ALU intensive"), &Expression, QualifiedId);
			}
			if (Type.Contains(TEXT("ddx")) || Type.Contains(TEXT("ddy")) || Type.Contains(TEXT("derivative")))
			{
				AddRisk(TEXT("MP3004"), EMatLangStaticRiskSeverity::Info,
					TEXT("Explicit derivatives require pixel-stage evaluation"), &Expression, QualifiedId);
			}
		}

		FMatLangStaticStageCost AnalyzeStage(const TArray<FString>& Roots)
		{
			const FCostAccumulator Low = Traverse(Roots, ETraversalMode::Low, false);
			const FCostAccumulator Default = Traverse(Roots, ETraversalMode::Default, false);
			const FCostAccumulator Conservative = Traverse(Roots, ETraversalMode::Conservative, false);
			return MakeStageCost(Low, Default, Conservative);
		}

		FMatLangStaticStageCost MakeStageCost(
			const FCostAccumulator& Low,
			const FCostAccumulator& Default,
			const FCostAccumulator& Conservative) const
		{
			FMatLangStaticStageCost Cost;
			Cost.ReachableNodes = {Low.VisitedNodes.Num(), Default.VisitedNodes.Num(), Conservative.VisitedNodes.Num()};
			Cost.TextureSampleSites = {Low.TextureSampleSites, Default.TextureSampleSites, Conservative.TextureSampleSites};
			Cost.ALUProxy = {Low.ALUProxy, Default.ALUProxy, Conservative.ALUProxy};
			Cost.DefaultMaxDepth = Default.MaxDepth;
			return Cost;
		}

		FCostAccumulator Traverse(
			const TArray<FString>& Roots,
			ETraversalMode Mode,
			bool bCollectRisks)
		{
			FCostAccumulator Accumulator;
			TSet<FString> FunctionStack;
			if (!Root->AssetPath.IsEmpty()) FunctionStack.Add(Root->AssetPath);
			for (const FString& RootId : Roots)
			{
				VisitNode(*Root, RootId, RootPrefix, Mode, bCollectRisks, 1, FunctionStack, Accumulator);
			}
			return Accumulator;
		}

		void CollectRoots(const FMaterialGraphAST& Graph, TArray<FString>& OutRoots) const
		{
			if (Graph.Kind == EMatLangGraphKind::Material)
			{
				for (const auto& Pair : Graph.Outputs.Slots)
				{
					if (Pair.Value.IsConnected()) OutRoots.AddUnique(Pair.Value.Connection->TargetId);
				}
				return;
			}

			for (const TSharedPtr<FMatExpressionAST>& Expression : Graph.Expressions)
			{
				if (Expression.IsValid() && Expression->ExprType.Contains(TEXT("function-output"), ESearchCase::IgnoreCase))
				{
					OutRoots.AddUnique(Expression->Id);
				}
			}
			if (OutRoots.Num() == 0)
			{
				for (const TSharedPtr<FMatExpressionAST>& Expression : Graph.Expressions)
				{
					if (Expression.IsValid()) OutRoots.Add(Expression->Id);
				}
			}
		}

		int32 EstimateBranchScore(
			const FMaterialGraphAST& Graph,
			const FMatLangInput* Input,
			TSet<FString>& Visiting) const
		{
			if (!Input || !Input->IsConnected()) return 0;
			const FString& Id = Input->Connection->TargetId;
			if (Visiting.Contains(Id)) return 0;
			Visiting.Add(Id);
			const TSharedPtr<FMatExpressionAST> Expression = Graph.FindExpression(Id);
			if (!Expression.IsValid()) return 0;

			int32 Score = NodeALUWeight(Expression->ExprType) + (IsTextureSample(Expression->ExprType) ? 8 : 0);
			for (const FMatLangInput& Child : Expression->Inputs)
			{
				Score += EstimateBranchScore(Graph, &Child, Visiting);
			}
			Visiting.Remove(Id);
			return Score;
		}

		TOptional<bool> ResolveStaticValue(
			const FMaterialGraphAST& Graph,
			const FMatExpressionAST& Expression,
			TSet<FString>& Visiting) const
		{
			if (Expression.ExprType.Contains(TEXT("static-switch-parameter"), ESearchCase::IgnoreCase)
				|| Expression.ExprType.Contains(TEXT("static-bool-parameter"), ESearchCase::IgnoreCase))
			{
				return Expression.GetBoolProperty(TEXT("default"), false);
			}
			if (Expression.ExprType.Contains(TEXT("static-bool"), ESearchCase::IgnoreCase))
			{
				return Expression.GetBoolProperty(TEXT("value"), false);
			}
			const FMatLangInput* ValueInput = FindInputIgnoreCase(Expression, TEXT("value"));
			if (ValueInput && ValueInput->IsConnected() && !Visiting.Contains(ValueInput->Connection->TargetId))
			{
				Visiting.Add(ValueInput->Connection->TargetId);
				const TSharedPtr<FMatExpressionAST> ValueExpression = Graph.FindExpression(ValueInput->Connection->TargetId);
				if (ValueExpression.IsValid()) return ResolveStaticValue(Graph, *ValueExpression, Visiting);
			}
			return TOptional<bool>();
		}

		void VisitNode(
			const FMaterialGraphAST& Graph,
			const FString& Id,
			const FString& Prefix,
			ETraversalMode Mode,
			bool bCollectRisks,
			int32 Depth,
			TSet<FString>& FunctionStack,
			FCostAccumulator& Accumulator)
		{
			Accumulator.MaxDepth = FMath::Max(Accumulator.MaxDepth, Depth);
			const TSharedPtr<FMatExpressionAST> Expression = Graph.FindExpression(Id);
			if (!Expression.IsValid()) return;

			const FString QualifiedId = Prefix + TEXT("::") + Id;
			if (Accumulator.VisitedNodes.Contains(QualifiedId)) return;
			Accumulator.VisitedNodes.Add(QualifiedId);
			if (&Graph == Root.Get() && Prefix == RootPrefix)
			{
				Accumulator.RootGraphNodes.Add(Id);
			}

			Accumulator.ALUProxy += NodeALUWeight(Expression->ExprType);
			if (IsTextureSample(Expression->ExprType))
			{
				++Accumulator.TextureSampleSites;
				if (const FString* Texture = Expression->Properties.Find(TEXT("texture")))
				{
					const FString Path = ExtractAssetPath(*Texture);
					if (!Path.IsEmpty()) Accumulator.TextureAssets.Add(Path);
				}
			}
			if (Expression->ExprType.Contains(TEXT("static-switch-parameter"), ESearchCase::IgnoreCase)
				|| Expression->ExprType.Contains(TEXT("static-bool-parameter"), ESearchCase::IgnoreCase))
			{
				const FString Name = Unquote(Expression->GetStringProperty(TEXT("name")));
				if (!Name.IsEmpty()) Accumulator.StaticParameters.Add(Name);
			}
			if (bCollectRisks) InspectExpressionRisk(*Expression, QualifiedId);

			const bool bFunctionCall = Expression->ExprType.Equals(TEXT("material-function-call"), ESearchCase::IgnoreCase);
			if (bFunctionCall)
			{
				++Accumulator.FunctionCallSites;
				VisitFunction(*Expression, QualifiedId, Mode, bCollectRisks, Depth, FunctionStack, Accumulator);
			}

			if (IsStaticSwitch(Expression->ExprType))
			{
				VisitStaticSwitch(Graph, *Expression, Prefix, Mode, bCollectRisks, Depth, FunctionStack, Accumulator);
				return;
			}

			for (const FMatLangInput& Input : Expression->Inputs)
			{
				if (Input.IsConnected())
				{
					VisitNode(Graph, Input.Connection->TargetId, Prefix, Mode, bCollectRisks,
						Depth + 1, FunctionStack, Accumulator);
				}
			}
		}

		void VisitStaticSwitch(
			const FMaterialGraphAST& Graph,
			const FMatExpressionAST& Expression,
			const FString& Prefix,
			ETraversalMode Mode,
			bool bCollectRisks,
			int32 Depth,
			TSet<FString>& FunctionStack,
			FCostAccumulator& Accumulator)
		{
			const FMatLangInput* A = FindInputIgnoreCase(Expression, TEXT("a"));
			const FMatLangInput* B = FindInputIgnoreCase(Expression, TEXT("b"));

			for (const FMatLangInput& Input : Expression.Inputs)
			{
				if (&Input == A || &Input == B || !Input.IsConnected()) continue;
				VisitNode(Graph, Input.Connection->TargetId, Prefix, Mode, bCollectRisks,
					Depth + 1, FunctionStack, Accumulator);
			}

			if (Mode == ETraversalMode::Conservative)
			{
				if (bCollectRisks)
				{
					TSet<FString> Visiting;
					if (!ResolveStaticValue(Graph, Expression, Visiting).IsSet())
					{
						AddRisk(TEXT("MP3101"), EMatLangStaticRiskSeverity::Unknown,
							TEXT("Static switch value is unresolved; default scenario includes both branches"),
							&Expression, Prefix + TEXT("::") + Expression.Id);
					}
				}
				if (A && A->IsConnected()) VisitNode(Graph, A->Connection->TargetId, Prefix, Mode, bCollectRisks, Depth + 1, FunctionStack, Accumulator);
				if (B && B->IsConnected()) VisitNode(Graph, B->Connection->TargetId, Prefix, Mode, bCollectRisks, Depth + 1, FunctionStack, Accumulator);
				return;
			}

			const FMatLangInput* Selected = nullptr;
			if (Mode == ETraversalMode::Default)
			{
				TSet<FString> Visiting;
				const TOptional<bool> Value = ResolveStaticValue(Graph, Expression, Visiting);
				if (Value.IsSet())
				{
					Selected = Value.GetValue() ? A : B;
				}
				else
				{
					if (bCollectRisks)
					{
						AddRisk(TEXT("MP3101"), EMatLangStaticRiskSeverity::Unknown,
							TEXT("Static switch value is unresolved; default scenario includes both branches"),
							&Expression, Prefix + TEXT("::") + Expression.Id);
					}
					if (A && A->IsConnected()) VisitNode(Graph, A->Connection->TargetId, Prefix, Mode, bCollectRisks, Depth + 1, FunctionStack, Accumulator);
					if (B && B->IsConnected()) VisitNode(Graph, B->Connection->TargetId, Prefix, Mode, bCollectRisks, Depth + 1, FunctionStack, Accumulator);
					return;
				}
			}
			else
			{
				TSet<FString> VisitingA;
				TSet<FString> VisitingB;
				Selected = EstimateBranchScore(Graph, A, VisitingA) <= EstimateBranchScore(Graph, B, VisitingB) ? A : B;
			}

			if (Selected && Selected->IsConnected())
			{
				VisitNode(Graph, Selected->Connection->TargetId, Prefix, Mode, bCollectRisks,
					Depth + 1, FunctionStack, Accumulator);
			}
		}

		void VisitFunction(
			const FMatExpressionAST& Expression,
			const FString& QualifiedId,
			ETraversalMode Mode,
			bool bCollectRisks,
			int32 Depth,
			TSet<FString>& FunctionStack,
			FCostAccumulator& Accumulator)
		{
			if (!Options.bExpandMaterialFunctions) return;
			const FString* FunctionValue = Expression.Properties.Find(TEXT("function"));
			const FString FunctionPath = FunctionValue ? ExtractAssetPath(*FunctionValue) : FString();
			if (FunctionPath.IsEmpty() || !Resolver)
			{
				if (bCollectRisks)
				{
					++Report.UnresolvedFunctionCalls;
					AddRisk(TEXT("MP3201"), EMatLangStaticRiskSeverity::Unknown,
						FString::Printf(TEXT("Material function dependency is unavailable: %s"), *FunctionPath),
						&Expression, QualifiedId);
				}
				return;
			}
			if (FunctionStack.Num() >= Options.MaxFunctionDepth || FunctionStack.Contains(FunctionPath))
			{
				if (bCollectRisks)
				{
					++Report.RecursiveFunctionCalls;
					AddRisk(TEXT("MP3202"), EMatLangStaticRiskSeverity::Unknown,
						FString::Printf(TEXT("Recursive or excessively deep material function dependency: %s"), *FunctionPath),
						&Expression, QualifiedId);
				}
				return;
			}

			const TSharedPtr<FMaterialGraphAST> Function = Resolver(FunctionPath);
			if (!Function.IsValid())
			{
				if (bCollectRisks)
				{
					++Report.UnresolvedFunctionCalls;
					AddRisk(TEXT("MP3201"), EMatLangStaticRiskSeverity::Unknown,
						FString::Printf(TEXT("Material function was not found in the analyzed DSL set: %s"), *FunctionPath),
						&Expression, QualifiedId);
				}
				return;
			}

			FunctionStack.Add(FunctionPath);
			TArray<FString> FunctionRoots;
			CollectRoots(*Function, FunctionRoots);
			const FString FunctionPrefix = QualifiedId + TEXT("->") + FunctionPath;
			for (const FString& RootId : FunctionRoots)
			{
				VisitNode(*Function, RootId, FunctionPrefix, Mode, bCollectRisks,
					Depth + 1, FunctionStack, Accumulator);
			}
			FunctionStack.Remove(FunctionPath);
		}
	};
}

FMatLangStaticCostReport FMatLangStaticCostAnalyzer::Analyze(
	const TSharedPtr<FMaterialGraphAST>& AST,
	const FFunctionResolver& FunctionResolver,
	const FMatLangStaticCostOptions& Options)
{
	FMatLangStaticCostReport Report;
	if (!AST.IsValid())
	{
		Report.bHasUnknownCost = true;
		FMatLangStaticCostRisk Risk;
		Risk.RuleId = TEXT("MP0001");
		Risk.Severity = EMatLangStaticRiskSeverity::Unknown;
		Risk.Message = TEXT("Static cost analysis requires a valid AST");
		Report.Risks.Add(MoveTemp(Risk));
		return Report;
	}

	Report.Name = AST->Name;
	Report.AssetPath = AST->AssetPath;
	Report.Kind = AST->Kind;
	Report.DeclaredNodes = AST->Expressions.Num();

	TMap<FString, int32> FanOut;
	for (const TSharedPtr<FMatExpressionAST>& Expression : AST->Expressions)
	{
		if (!Expression.IsValid()) continue;
		for (const FMatLangInput& Input : Expression->Inputs)
		{
			if (!Input.IsConnected()) continue;
			++Report.DeclaredEdges;
			++FanOut.FindOrAdd(Input.Connection->TargetId);
		}
	}
	for (const auto& Pair : AST->Outputs.Slots)
	{
		if (!Pair.Value.IsConnected()) continue;
		++Report.DeclaredEdges;
		++FanOut.FindOrAdd(Pair.Value.Connection->TargetId);
	}
	for (const auto& Pair : FanOut)
	{
		Report.MaxFanOut = FMath::Max(Report.MaxFanOut, Pair.Value);
	}

	FAnalyzerWorker Worker(AST, FunctionResolver, Options, Report);
	Worker.Run();
	return Report;
}
