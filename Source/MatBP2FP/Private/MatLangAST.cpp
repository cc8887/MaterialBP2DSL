// MatLangAST.cpp - AST Implementation for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangAST.h"

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
		Result += FString::Printf(TEXT("\n%s  :%s %s"), *Pad, *Input.Name, *ValueStr);
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
	: Domain(EMatLangDomain::Surface)
	, BlendMode(EMatLangBlendMode::Opaque)
	, ShadingModel(EMatLangShadingModel::DefaultLit)
	, bTwoSided(false)
	, bIsMasked(false)
	, OpacityMaskClipValue(0.333f)
{
}

FString FMaterialGraphAST::ToString() const
{
	FString Result = FString::Printf(TEXT("(material \"%s\"\n"), *Name);
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
	if (Str == TEXT("surface")) return EMatLangDomain::Surface;
	if (Str == TEXT("deferred-decal")) return EMatLangDomain::DeferredDecal;
	if (Str == TEXT("light-function")) return EMatLangDomain::LightFunction;
	if (Str == TEXT("volume")) return EMatLangDomain::Volume;
	if (Str == TEXT("post-process")) return EMatLangDomain::PostProcess;
	if (Str == TEXT("user-interface")) return EMatLangDomain::UserInterface;
	return EMatLangDomain::Surface;
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
	if (Str == TEXT("opaque")) return EMatLangBlendMode::Opaque;
	if (Str == TEXT("masked")) return EMatLangBlendMode::Masked;
	if (Str == TEXT("translucent")) return EMatLangBlendMode::Translucent;
	if (Str == TEXT("additive")) return EMatLangBlendMode::Additive;
	if (Str == TEXT("modulate")) return EMatLangBlendMode::Modulate;
	if (Str == TEXT("alpha-composite")) return EMatLangBlendMode::AlphaComposite;
	if (Str == TEXT("alpha-holdout")) return EMatLangBlendMode::AlphaHoldout;
	return EMatLangBlendMode::Opaque;
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
	if (Str == TEXT("unlit")) return EMatLangShadingModel::Unlit;
	if (Str == TEXT("default-lit")) return EMatLangShadingModel::DefaultLit;
	if (Str == TEXT("subsurface")) return EMatLangShadingModel::Subsurface;
	if (Str == TEXT("preintegrated-skin")) return EMatLangShadingModel::PreintegratedSkin;
	if (Str == TEXT("clear-coat")) return EMatLangShadingModel::ClearCoat;
	if (Str == TEXT("subsurface-profile")) return EMatLangShadingModel::SubsurfaceProfile;
	if (Str == TEXT("two-sided-foliage")) return EMatLangShadingModel::TwoSidedFoliage;
	if (Str == TEXT("hair")) return EMatLangShadingModel::Hair;
	if (Str == TEXT("cloth")) return EMatLangShadingModel::Cloth;
	if (Str == TEXT("eye")) return EMatLangShadingModel::Eye;
	if (Str == TEXT("single-layer-water")) return EMatLangShadingModel::SingleLayerWater;
	if (Str == TEXT("thin-translucent")) return EMatLangShadingModel::ThinTranslucent;
	if (Str == TEXT("strata")) return EMatLangShadingModel::Strata;
	return EMatLangShadingModel::DefaultLit;
}

} // namespace MatLangEnums
