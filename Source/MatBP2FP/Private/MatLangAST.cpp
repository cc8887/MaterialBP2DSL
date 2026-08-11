// MatLangAST.cpp - AST Implementation for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangAST.h"

namespace
{
	FString EscapeDSLString(const FString& Value)
	{
		return Value.Replace(TEXT("\\"), TEXT("\\\\"))
			.Replace(TEXT("\""), TEXT("\\\""));
	}
}

// ========== FMatLangConnection ==========

FString FMatLangConnection::ToString() const
{
	if (OutputIndex == 0)
	{
		return FString::Printf(TEXT("(connect %s)"), *TargetId);
	}
	return FString::Printf(TEXT("(connect %s %d)"), *TargetId, OutputIndex);
}

// ========== FMatLangInput ==========

FString FMatLangInput::ToString(int32 Indent) const
{
	if (IsConnected())
	{
		return Connection->ToString();
	}
	if (IsLiteral())
	{
		return *LiteralValue;
	}
	return TEXT("");
}

// ========== FMatExpressionAST ==========

FString FMatExpressionAST::ToString(int32 Indent) const
{
	FString Pad;
	for (int32 i = 0; i < Indent; ++i) Pad += TEXT("  ");
	
	FString Result = FString::Printf(TEXT("%s(%s %s"), *Pad, *ExprType, *Id);
	
	// Properties
	for (const auto& Pair : Properties)
	{
		// Values starting with ( or " or [ are already self-delimited DSL values — don't re-quote
		bool bSelfDelimited = Pair.Value.StartsWith(TEXT("(")) 
			|| Pair.Value.StartsWith(TEXT("\""))
			|| Pair.Value.StartsWith(TEXT("["));
		if (bSelfDelimited)
		{
			Result += FString::Printf(TEXT("\n%s  :%s %s"), *Pad, *Pair.Key, *Pair.Value);
		}
		else
		{
			// Quote plain values that contain spaces or slashes
			bool bNeedsQuote = Pair.Value.Contains(TEXT(" ")) || Pair.Value.Contains(TEXT("/"));
			if (bNeedsQuote)
			{
				Result += FString::Printf(TEXT("\n%s  :%s \"%s\""), *Pad, *Pair.Key, *Pair.Value);
			}
			else
			{
				Result += FString::Printf(TEXT("\n%s  :%s %s"), *Pad, *Pair.Key, *Pair.Value);
			}
		}
	}
	
	// Inputs
	for (const FMatLangInput& Input : Inputs)
	{
		FString ValueStr = Input.ToString(Indent + 1);
		// If the input name contains spaces or non-ASCII characters (e.g. Chinese pin names),
		// quote it as :"name" so the tokenizer reads it as a single keyword token.
		bool bNeedsQuote = false;
		for (int32 ci = 0; ci < Input.Name.Len(); ++ci)
		{
			TCHAR Ch = Input.Name[ci];
			if (Ch == ' ' || Ch > 127)
			{
				bNeedsQuote = true;
				break;
			}
		}
		FString KeyStr = bNeedsQuote
			? FString::Printf(TEXT("\"%s\""), *Input.Name)
			: Input.Name;
		Result += FString::Printf(TEXT("\n%s  :%s %s"), *Pad, *KeyStr, *ValueStr);
	}
	
	Result += TEXT(")");
	return Result;
}

FString FMatExpressionAST::GetStringProperty(const FString& Key, const FString& Default) const
{
	const FString* Val = Properties.Find(Key);
	return Val ? *Val : Default;
}

float FMatExpressionAST::GetFloatProperty(const FString& Key, float Default) const
{
	const FString* Val = Properties.Find(Key);
	if (Val)
	{
		float Result = Default;
		LexFromString(Result, **Val);
		return Result;
	}
	return Default;
}

bool FMatExpressionAST::GetBoolProperty(const FString& Key, bool Default) const
{
	const FString* Val = Properties.Find(Key);
	if (Val)
	{
		return Val->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}
	return Default;
}

const FMatLangInput* FMatExpressionAST::FindInput(const FString& Name) const
{
	for (const FMatLangInput& Input : Inputs)
	{
		if (Input.Name == Name)
		{
			return &Input;
		}
	}
	return nullptr;
}

void FMatExpressionAST::AddInput(const FString& Name, const FMatLangConnection& Conn)
{
	FMatLangInput Input;
	Input.Name = Name;
	Input.Connection = Conn;
	Inputs.Add(MoveTemp(Input));
}

void FMatExpressionAST::AddInput(const FString& Name, const FString& LiteralValue)
{
	FMatLangInput Input;
	Input.Name = Name;
	Input.LiteralValue = LiteralValue;
	Inputs.Add(MoveTemp(Input));
}

// ========== FMatOutputsAST ==========

FString FMatOutputsAST::ToString(int32 Indent) const
{
	FString Pad;
	for (int32 i = 0; i < Indent; ++i) Pad += TEXT("  ");
	
	FString Result = FString::Printf(TEXT("%s(outputs"), *Pad);
	
	for (const auto& Pair : Slots)
	{
		FString ValueStr = Pair.Value.ToString(Indent + 1);
		Result += FString::Printf(TEXT("\n%s  :%s %s"), *Pad, *Pair.Key, *ValueStr);
	}
	
	Result += TEXT(")");
	return Result;
}

// ========== FMatParameterDef ==========

FString FMatParameterDef::ToString() const
{
	return FString::Printf(TEXT("(%s :%s :group \"%s\" :default %s)"), *Type, *Name, *Group, *DefaultValue);
}

// ========== FMaterialGraphAST ==========

FMaterialGraphAST::FMaterialGraphAST()
	: Kind(EMatLangGraphKind::Material)
	, Domain(EMatLangDomain::Surface)
	, BlendMode(EMatLangBlendMode::Opaque)
	, ShadingModel(EMatLangShadingModel::DefaultLit)
	, bTwoSided(false)
	, bIsMasked(false)
	, OpacityMaskClipValue(0.333f)
{
}

FString FMaterialGraphAST::ToString() const
{
	if (Kind == EMatLangGraphKind::MaterialFunction)
	{
		// MaterialFunction format: (material-function "Name" (function-inputs ...) (function-outputs ...) (expressions ...) (outputs ...))
		FString Result = FString::Printf(TEXT("(material-function \"%s\"\n"), *EscapeDSLString(Name));
		if (!AssetPath.IsEmpty())
		{
			Result += FString::Printf(TEXT("  :asset-path \"%s\"\n"), *EscapeDSLString(AssetPath));
		}

		if (FunctionInputs.Num() > 0)
		{
			Result += TEXT("  (function-inputs\n");
			for (const FMatParameterDef& Input : FunctionInputs)
			{
				Result += FString::Printf(TEXT("    (input :name \"%s\" :sort-priority %d)\n"),
					*EscapeDSLString(Input.Name), Input.SortPriority);
			}
			Result += TEXT("  )\n");
		}

		if (FunctionOutputs.Num() > 0)
		{
			Result += TEXT("  (function-outputs\n");
			for (const FMatParameterDef& Output : FunctionOutputs)
			{
				Result += FString::Printf(TEXT("    (output :name \"%s\" :sort-priority %d)\n"),
					*EscapeDSLString(Output.Name), Output.SortPriority);
			}
			Result += TEXT("  )\n");
		}

		// Expressions
		Result += TEXT("  (expressions\n");
		for (const auto& Expr : Expressions)
		{
			Result += Expr->ToString(2) + TEXT("\n");
		}
		Result += TEXT("  )\n");

		// Outputs
		Result += Outputs.ToString(1) + TEXT("\n");

		Result += TEXT(")");
		return Result;
	}

	// Material format (original)
	FString Result = FString::Printf(TEXT("(material \"%s\"\n"), *EscapeDSLString(Name));
	if (!AssetPath.IsEmpty())
	{
		Result += FString::Printf(TEXT("  :asset-path \"%s\"\n"), *EscapeDSLString(AssetPath));
	}
	Result += FString::Printf(TEXT("  :domain %s\n"), *MatLangEnums::DomainToString(Domain));
	Result += FString::Printf(TEXT("  :blend-mode %s\n"), *MatLangEnums::BlendModeToString(BlendMode));
	Result += FString::Printf(TEXT("  :shading-model %s\n"), *MatLangEnums::ShadingModelToString(ShadingModel));
	
	if (bTwoSided)
	{
		Result += TEXT("  :two-sided true\n");
	}
	if (bIsMasked)
	{
		Result += FString::Printf(TEXT("  :opacity-mask-clip-value %g\n"), OpacityMaskClipValue);
	}
	
	// Extra properties
	for (const auto& Pair : ExtraProperties)
	{
		Result += FString::Printf(TEXT("  :%s %s\n"), *Pair.Key, *Pair.Value);
	}
	
	// Parameters section (informational, derived from expressions)
	if (Parameters.Num() > 0)
	{
		Result += TEXT("  :parameters [\n");
		for (const FMatParameterDef& Param : Parameters)
		{
			Result += FString::Printf(TEXT("    %s\n"), *Param.ToString());
		}
		Result += TEXT("  ]\n");
	}
	
	// Expressions
	Result += TEXT("  (expressions\n");
	for (const auto& Expr : Expressions)
	{
		Result += Expr->ToString(2) + TEXT("\n");
	}
	Result += TEXT("  )\n");
	
	// Outputs
	Result += Outputs.ToString(1) + TEXT("\n");
	
	Result += TEXT(")");
	return Result;
}

TSharedPtr<FMatExpressionAST> FMaterialGraphAST::FindExpression(const FString& Id) const
{
	for (const auto& Expr : Expressions)
	{
		if (Expr->Id == Id)
		{
			return Expr;
		}
	}
	return nullptr;
}

TArray<TSharedPtr<FMatExpressionAST>> FMaterialGraphAST::GetTopologicalOrder() const
{
	// Kahn's algorithm for topological sort
	// Build adjacency: expression A depends on expression B if A has input connecting to B
	
	TMap<FString, int32> InDegree;
	TMap<FString, TArray<FString>> Dependents; // B -> [A1, A2, ...] (who depends on B)
	TMap<FString, TSharedPtr<FMatExpressionAST>> IdToExpr;
	
	for (const auto& Expr : Expressions)
	{
		IdToExpr.Add(Expr->Id, Expr);
		InDegree.FindOrAdd(Expr->Id) += 0; // Ensure entry exists
		
		for (const FMatLangInput& Input : Expr->Inputs)
		{
			if (Input.IsConnected())
			{
				InDegree.FindOrAdd(Expr->Id) += 1;
				Dependents.FindOrAdd(Input.Connection->TargetId).Add(Expr->Id);
			}
		}
	}
	
	// Start with nodes that have no dependencies
	TArray<FString> Queue;
	for (const auto& Pair : InDegree)
	{
		if (Pair.Value == 0)
		{
			Queue.Add(Pair.Key);
		}
	}
	
	TArray<TSharedPtr<FMatExpressionAST>> Sorted;
	while (Queue.Num() > 0)
	{
		FString Current = Queue[0];
		Queue.RemoveAt(0);
		
		if (auto* Expr = IdToExpr.Find(Current))
		{
			Sorted.Add(*Expr);
		}
		
		if (auto* Deps = Dependents.Find(Current))
		{
			for (const FString& DepId : *Deps)
			{
				int32& Deg = InDegree.FindOrAdd(DepId);
				Deg--;
				if (Deg == 0)
				{
					Queue.Add(DepId);
				}
			}
		}
	}
	
	return Sorted;
}

// ========== Enum Conversions ==========

namespace MatLangEnums
{

FString DomainToString(EMatLangDomain Domain)
{
	switch (Domain)
	{
		case EMatLangDomain::Surface:        return TEXT("surface");
		case EMatLangDomain::DeferredDecal:  return TEXT("deferred-decal");
		case EMatLangDomain::LightFunction:  return TEXT("light-function");
		case EMatLangDomain::Volume:         return TEXT("volume");
		case EMatLangDomain::PostProcess:    return TEXT("post-process");
		case EMatLangDomain::UserInterface:  return TEXT("user-interface");
		default: return TEXT("surface");
	}
}

EMatLangDomain StringToDomain(const FString& Str)
{
	EMatLangDomain Result;
	if (TryStringToDomain(Str, Result)) return Result;
	return EMatLangDomain::Surface;
}

bool TryStringToDomain(const FString& Str, EMatLangDomain& OutDomain)
{
	if (Str == TEXT("surface")) { OutDomain = EMatLangDomain::Surface; return true; }
	if (Str == TEXT("deferred-decal")) { OutDomain = EMatLangDomain::DeferredDecal; return true; }
	if (Str == TEXT("light-function")) { OutDomain = EMatLangDomain::LightFunction; return true; }
	if (Str == TEXT("volume")) { OutDomain = EMatLangDomain::Volume; return true; }
	if (Str == TEXT("post-process")) { OutDomain = EMatLangDomain::PostProcess; return true; }
	if (Str == TEXT("user-interface")) { OutDomain = EMatLangDomain::UserInterface; return true; }
	return false;
}

FString BlendModeToString(EMatLangBlendMode Mode)
{
	switch (Mode)
	{
		case EMatLangBlendMode::Opaque:          return TEXT("opaque");
		case EMatLangBlendMode::Masked:          return TEXT("masked");
		case EMatLangBlendMode::Translucent:     return TEXT("translucent");
		case EMatLangBlendMode::Additive:        return TEXT("additive");
		case EMatLangBlendMode::Modulate:        return TEXT("modulate");
		case EMatLangBlendMode::AlphaComposite:  return TEXT("alpha-composite");
		case EMatLangBlendMode::AlphaHoldout:    return TEXT("alpha-holdout");
		default: return TEXT("opaque");
	}
}

EMatLangBlendMode StringToBlendMode(const FString& Str)
{
	EMatLangBlendMode Result;
	if (TryStringToBlendMode(Str, Result)) return Result;
	return EMatLangBlendMode::Opaque;
}

bool TryStringToBlendMode(const FString& Str, EMatLangBlendMode& OutMode)
{
	if (Str == TEXT("opaque")) { OutMode = EMatLangBlendMode::Opaque; return true; }
	if (Str == TEXT("masked")) { OutMode = EMatLangBlendMode::Masked; return true; }
	if (Str == TEXT("translucent")) { OutMode = EMatLangBlendMode::Translucent; return true; }
	if (Str == TEXT("additive")) { OutMode = EMatLangBlendMode::Additive; return true; }
	if (Str == TEXT("modulate")) { OutMode = EMatLangBlendMode::Modulate; return true; }
	if (Str == TEXT("alpha-composite")) { OutMode = EMatLangBlendMode::AlphaComposite; return true; }
	if (Str == TEXT("alpha-holdout")) { OutMode = EMatLangBlendMode::AlphaHoldout; return true; }
	return false;
}

FString ShadingModelToString(EMatLangShadingModel Model)
{
	switch (Model)
	{
		case EMatLangShadingModel::Unlit:              return TEXT("unlit");
		case EMatLangShadingModel::DefaultLit:         return TEXT("default-lit");
		case EMatLangShadingModel::Subsurface:         return TEXT("subsurface");
		case EMatLangShadingModel::PreintegratedSkin:  return TEXT("preintegrated-skin");
		case EMatLangShadingModel::ClearCoat:          return TEXT("clear-coat");
		case EMatLangShadingModel::SubsurfaceProfile:  return TEXT("subsurface-profile");
		case EMatLangShadingModel::TwoSidedFoliage:    return TEXT("two-sided-foliage");
		case EMatLangShadingModel::Hair:               return TEXT("hair");
		case EMatLangShadingModel::Cloth:              return TEXT("cloth");
		case EMatLangShadingModel::Eye:                return TEXT("eye");
		case EMatLangShadingModel::SingleLayerWater:   return TEXT("single-layer-water");
		case EMatLangShadingModel::ThinTranslucent:    return TEXT("thin-translucent");
		case EMatLangShadingModel::Strata:             return TEXT("strata");
		default: return TEXT("default-lit");
	}
}

EMatLangShadingModel StringToShadingModel(const FString& Str)
{
	EMatLangShadingModel Result;
	if (TryStringToShadingModel(Str, Result)) return Result;
	return EMatLangShadingModel::DefaultLit;
}

bool TryStringToShadingModel(const FString& Str, EMatLangShadingModel& OutModel)
{
	if (Str == TEXT("unlit")) { OutModel = EMatLangShadingModel::Unlit; return true; }
	if (Str == TEXT("default-lit")) { OutModel = EMatLangShadingModel::DefaultLit; return true; }
	if (Str == TEXT("subsurface")) { OutModel = EMatLangShadingModel::Subsurface; return true; }
	if (Str == TEXT("preintegrated-skin")) { OutModel = EMatLangShadingModel::PreintegratedSkin; return true; }
	if (Str == TEXT("clear-coat")) { OutModel = EMatLangShadingModel::ClearCoat; return true; }
	if (Str == TEXT("subsurface-profile")) { OutModel = EMatLangShadingModel::SubsurfaceProfile; return true; }
	if (Str == TEXT("two-sided-foliage")) { OutModel = EMatLangShadingModel::TwoSidedFoliage; return true; }
	if (Str == TEXT("hair")) { OutModel = EMatLangShadingModel::Hair; return true; }
	if (Str == TEXT("cloth")) { OutModel = EMatLangShadingModel::Cloth; return true; }
	if (Str == TEXT("eye")) { OutModel = EMatLangShadingModel::Eye; return true; }
	if (Str == TEXT("single-layer-water")) { OutModel = EMatLangShadingModel::SingleLayerWater; return true; }
	if (Str == TEXT("thin-translucent")) { OutModel = EMatLangShadingModel::ThinTranslucent; return true; }
	if (Str == TEXT("strata")) { OutModel = EMatLangShadingModel::Strata; return true; }
	return false;
}

} // namespace MatLangEnums
