// MatBPExporter.cpp - Material Blueprint to DSL Exporter
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBPExporter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatBPExporter, Log, All);

#if WITH_EDITOR

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionDesaturation.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionCustom.h"
#include "MaterialDomain.h"

// ========== Public API ==========

FString FMatBPExporter::ExportToString(UMaterial* Material)
{
	auto AST = ExportToAST(Material);
	return AST ? AST->ToString() : TEXT("");
}

TSharedPtr<FMaterialGraphAST> FMatBPExporter::ExportToAST(UMaterial* Material)
{
	if (!Material)
	{
		UE_LOG(LogMatBPExporter, Error, TEXT("Null material"));
		return nullptr;
	}
	
	FMatBPExporter Exporter(Material);
	return Exporter.Export();
}

// ========== Internal ==========

FMatBPExporter::FMatBPExporter(UMaterial* InMaterial)
	: Material(InMaterial), IdCounter(0)
{
}

TSharedPtr<FMaterialGraphAST> FMatBPExporter::Export()
{
	auto AST = MakeShared<FMaterialGraphAST>();
	AST->Name = Material->GetName();
	
	// Material properties
	AST->Domain = MapDomain((int32)Material->MaterialDomain);
	AST->BlendMode = MapBlendMode((int32)Material->BlendMode);
	AST->ShadingModel = MapShadingModel((int32)Material->GetShadingModels().GetFirstShadingModel());
	AST->bTwoSided = Material->IsTwoSided();
	
	if (Material->BlendMode == BLEND_Masked)
	{
		AST->bIsMasked = true;
		AST->OpacityMaskClipValue = Material->OpacityMaskClipValue;
	}
	
	// First pass: assign IDs to all expressions
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (Expr && !Cast<UMaterialExpressionComment>(Expr))
		{
			GetOrAssignId(Expr);
		}
	}
	
	// Second pass: export all expressions
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (Expr && !Cast<UMaterialExpressionComment>(Expr))
		{
			auto ExprAST = ExportExpression(Expr);
			if (ExprAST)
			{
				AST->Expressions.Add(ExprAST);
			}
		}
	}
	
	// Export material outputs
	ExportOutputs(AST);
	
	UE_LOG(LogMatBPExporter, Log, TEXT("Exported material '%s': %d expressions, %d output slots"),
		*AST->Name, AST->Expressions.Num(), AST->Outputs.Slots.Num());
	
	return AST;
}

FString FMatBPExporter::GetOrAssignId(UMaterialExpression* Expr)
{
	if (auto* Existing = ExprToId.Find(Expr))
	{
		return *Existing;
	}
	
	// Generate a readable ID based on expression type
	FString TypePrefix = ExprClassToType(Expr);
	
	// Use short prefix: texture-sample -> tex, multiply -> mul, add -> add, etc.
	FString ShortPrefix;
	if (TypePrefix.Contains(TEXT("texture-sample"))) ShortPrefix = TEXT("tex");
	else if (TypePrefix.Contains(TEXT("texture-coordinate"))) ShortPrefix = TEXT("uv");
	else if (TypePrefix.Contains(TEXT("texture-object"))) ShortPrefix = TEXT("texobj");
	else if (TypePrefix.Contains(TEXT("vector-parameter"))) ShortPrefix = TEXT("vparam");
	else if (TypePrefix.Contains(TEXT("scalar-parameter"))) ShortPrefix = TEXT("sparam");
	else if (TypePrefix.Contains(TEXT("static-switch-parameter"))) ShortPrefix = TEXT("sswitch");
	else if (TypePrefix.Contains(TEXT("constant3-vector"))) ShortPrefix = TEXT("vec3");
	else if (TypePrefix.Contains(TEXT("constant4-vector"))) ShortPrefix = TEXT("vec4");
	else if (TypePrefix.Contains(TEXT("constant2-vector"))) ShortPrefix = TEXT("vec2");
	else if (TypePrefix.Contains(TEXT("constant"))) ShortPrefix = TEXT("const");
	else if (TypePrefix.Contains(TEXT("multiply"))) ShortPrefix = TEXT("mul");
	else if (TypePrefix.Contains(TEXT("add"))) ShortPrefix = TEXT("add");
	else if (TypePrefix.Contains(TEXT("subtract"))) ShortPrefix = TEXT("sub");
	else if (TypePrefix.Contains(TEXT("divide"))) ShortPrefix = TEXT("div");
	else if (TypePrefix.Contains(TEXT("lerp")) || TypePrefix.Contains(TEXT("linear-interpolate"))) ShortPrefix = TEXT("lerp");
	else if (TypePrefix.Contains(TEXT("clamp"))) ShortPrefix = TEXT("clamp");
	else if (TypePrefix.Contains(TEXT("power"))) ShortPrefix = TEXT("pow");
	else if (TypePrefix.Contains(TEXT("fresnel"))) ShortPrefix = TEXT("fresnel");
	else if (TypePrefix.Contains(TEXT("panner"))) ShortPrefix = TEXT("pan");
	else if (TypePrefix.Contains(TEXT("normalize"))) ShortPrefix = TEXT("norm");
	else if (TypePrefix.Contains(TEXT("one-minus"))) ShortPrefix = TEXT("oneminus");
	else if (TypePrefix.Contains(TEXT("desaturation"))) ShortPrefix = TEXT("desat");
	else if (TypePrefix.Contains(TEXT("material-function-call"))) ShortPrefix = TEXT("fn");
	else if (TypePrefix.Contains(TEXT("custom"))) ShortPrefix = TEXT("custom");
	else if (TypePrefix.Contains(TEXT("if"))) ShortPrefix = TEXT("if");
	else if (TypePrefix.Contains(TEXT("static-switch"))) ShortPrefix = TEXT("switch");
	else if (TypePrefix.Contains(TEXT("component-mask"))) ShortPrefix = TEXT("mask");
	else if (TypePrefix.Contains(TEXT("append"))) ShortPrefix = TEXT("append");
	else ShortPrefix = TypePrefix.Left(FMath::Min(TypePrefix.Len(), 6));
	
	FString Id = FString::Printf(TEXT("$%s%d"), *ShortPrefix, ++IdCounter);
	ExprToId.Add(Expr, Id);
	return Id;
}

FString FMatBPExporter::ExprClassToType(UMaterialExpression* Expr)
{
	FString ClassName = Expr->GetClass()->GetName();
	
	// Remove "MaterialExpression" prefix
	static const FString Prefix = TEXT("MaterialExpression");
	if (ClassName.StartsWith(Prefix))
	{
		ClassName = ClassName.Mid(Prefix.Len());
	}
	
	return CamelToKebab(ClassName);
}

FString FMatBPExporter::CamelToKebab(const FString& CamelCase)
{
	FString Result;
	for (int32 i = 0; i < CamelCase.Len(); ++i)
	{
		TCHAR Ch = CamelCase[i];
		if (FChar::IsUpper(Ch) && i > 0)
		{
			// Don't insert hyphen between consecutive uppercase letters
			// unless followed by lowercase (e.g. "WSPosition" -> "ws-position")
			bool bPrevUpper = FChar::IsUpper(CamelCase[i - 1]);
			bool bNextLower = (i + 1 < CamelCase.Len()) && FChar::IsLower(CamelCase[i + 1]);
			if (!bPrevUpper || bNextLower)
			{
				Result += TEXT('-');
			}
		}
		Result += FChar::ToLower(Ch);
	}
	return Result;
}

TSharedPtr<FMatExpressionAST> FMatBPExporter::ExportExpression(UMaterialExpression* Expr)
{
	auto Node = MakeShared<FMatExpressionAST>();
	Node->ExprType = ExprClassToType(Expr);
	Node->Id = GetOrAssignId(Expr);
	Node->EditorPosition = FVector2D(Expr->MaterialExpressionEditorX, Expr->MaterialExpressionEditorY);
	
	if (!Expr->Desc.IsEmpty())
	{
		Node->Comment = Expr->Desc;
	}
	
	// Export type-specific properties
	ExportExpressionProperties(Expr, Node);
	
	// Export inputs as connections
	ExportExpressionInputs(Expr, Node);
	
	return Node;
}

// Wrap a raw string value in DSL double-quotes, escaping any internal " and \ characters.
// Every string literal in the DSL must pass through this helper so that ScanString can
// round-trip the value correctly.
static FString EscapeForDSLString(const FString& Raw)
{
	FString Escaped = Raw;
	// Order matters: escape backslashes first, then double-quotes
	Escaped = Escaped.Replace(TEXT("\\"), TEXT("\\\\"));
	Escaped = Escaped.Replace(TEXT("\""), TEXT("\\\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

void FMatBPExporter::ExportExpressionProperties(UMaterialExpression* Expr, TSharedPtr<FMatExpressionAST> Node)
{
	// ---- Constants ----
	if (auto* C = Cast<UMaterialExpressionConstant>(Expr))
	{
		Node->Properties.Add(TEXT("value"), FString::SanitizeFloat(C->R));
		return;
	}
	if (auto* C2 = Cast<UMaterialExpressionConstant2Vector>(Expr))
	{
		Node->Properties.Add(TEXT("value"), FString::Printf(TEXT("(%g %g)"), C2->R, C2->G));
		return;
	}
	if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
	{
		Node->Properties.Add(TEXT("value"), FString::Printf(TEXT("(%g %g %g)"), C3->Constant.R, C3->Constant.G, C3->Constant.B));
		return;
	}
	if (auto* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
	{
		Node->Properties.Add(TEXT("value"), FString::Printf(TEXT("(%g %g %g %g)"), C4->Constant.R, C4->Constant.G, C4->Constant.B, C4->Constant.A));
		return;
	}
	
	// ---- Parameters ----
	if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		Node->Properties.Add(TEXT("name"), EscapeForDSLString(SP->ParameterName.ToString()));
		Node->Properties.Add(TEXT("default"), FString::SanitizeFloat(SP->DefaultValue));
		if (!SP->Group.IsNone())
		{
			Node->Properties.Add(TEXT("group"), EscapeForDSLString(SP->Group.ToString()));
		}
		return;
	}
	if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		Node->Properties.Add(TEXT("name"), EscapeForDSLString(VP->ParameterName.ToString()));
		Node->Properties.Add(TEXT("default"), FString::Printf(TEXT("(%g %g %g %g)"), VP->DefaultValue.R, VP->DefaultValue.G, VP->DefaultValue.B, VP->DefaultValue.A));
		if (!VP->Group.IsNone())
		{
			Node->Properties.Add(TEXT("group"), EscapeForDSLString(VP->Group.ToString()));
		}
		return;
	}
	if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
	{
		Node->Properties.Add(TEXT("name"), EscapeForDSLString(SSP->ParameterName.ToString()));
		Node->Properties.Add(TEXT("default"), SSP->DefaultValue ? TEXT("true") : TEXT("false"));
		return;
	}
	
	// ---- Texture ----
	if (auto* TS = Cast<UMaterialExpressionTextureSample>(Expr))
	{
		if (TS->Texture)
		{
			Node->Properties.Add(TEXT("texture"), FString::Printf(TEXT("(asset \"%s\")"), *TS->Texture->GetPathName()));
		}
		return;
	}
	if (auto* TOP = Cast<UMaterialExpressionTextureObjectParameter>(Expr))
	{
		Node->Properties.Add(TEXT("name"), EscapeForDSLString(TOP->ParameterName.ToString()));
		if (TOP->Texture)
		{
			Node->Properties.Add(TEXT("default-texture"), FString::Printf(TEXT("(asset \"%s\")"), *TOP->Texture->GetPathName()));
		}
		return;
	}
	
	// ---- Texture Coordinate ----
	if (auto* TC = Cast<UMaterialExpressionTextureCoordinate>(Expr))
	{
		Node->Properties.Add(TEXT("coordinate-index"), FString::FromInt(TC->CoordinateIndex));
		if (TC->UTiling != 1.0f) Node->Properties.Add(TEXT("u-tiling"), FString::SanitizeFloat(TC->UTiling));
		if (TC->VTiling != 1.0f) Node->Properties.Add(TEXT("v-tiling"), FString::SanitizeFloat(TC->VTiling));
		return;
	}
	
	// ---- ComponentMask ----
	if (auto* CM = Cast<UMaterialExpressionComponentMask>(Expr))
	{
		FString Mask;
		if (CM->R) Mask += TEXT("r");
		if (CM->G) Mask += TEXT("g");
		if (CM->B) Mask += TEXT("b");
		if (CM->A) Mask += TEXT("a");
		Node->Properties.Add(TEXT("mask"), Mask);
		return;
	}
	
	// ---- Panner ----
	if (auto* Pan = Cast<UMaterialExpressionPanner>(Expr))
	{
		Node->Properties.Add(TEXT("speed-x"), FString::SanitizeFloat(Pan->SpeedX));
		Node->Properties.Add(TEXT("speed-y"), FString::SanitizeFloat(Pan->SpeedY));
		return;
	}
	
	// ---- Fresnel ----
	if (auto* Fresnel = Cast<UMaterialExpressionFresnel>(Expr))
	{
		Node->Properties.Add(TEXT("exponent"), FString::SanitizeFloat(Fresnel->Exponent));
		Node->Properties.Add(TEXT("base-reflect-fraction"), FString::SanitizeFloat(Fresnel->BaseReflectFraction));
		return;
	}
	
	// ---- If ----
	if (auto* IfExpr = Cast<UMaterialExpressionIf>(Expr))
	{
		Node->Properties.Add(TEXT("equality-threshold"), FString::SanitizeFloat(IfExpr->EqualsThreshold));
		return;
	}
	
	// ---- Custom (HLSL) ----
	if (auto* Custom = Cast<UMaterialExpressionCustom>(Expr))
	{
		Node->Properties.Add(TEXT("code"), EscapeForDSLString(Custom->Code));
		Node->Properties.Add(TEXT("output-type"), FString::FromInt((int32)Custom->OutputType));
		if (!Custom->Description.IsEmpty())
		{
			Node->Properties.Add(TEXT("description"), EscapeForDSLString(Custom->Description));
		}
		return;
	}
	
	// ---- MaterialFunctionCall ----
	if (auto* MFC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
	{
		if (MFC->MaterialFunction)
		{
			Node->Properties.Add(TEXT("function"), FString::Printf(TEXT("(asset \"%s\")"), *MFC->MaterialFunction->GetPathName()));
		}
		return;
	}
	
	// ---- Generic: use FProperty reflection for remaining types ----
	// Export non-default UPROPERTY values via reflection
	UClass* ExprClass = Expr->GetClass();
	UMaterialExpression* CDO = Cast<UMaterialExpression>(ExprClass->GetDefaultObject());
	if (!CDO || CDO == Expr) return;
	
	for (TFieldIterator<FProperty> PropIt(ExprClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		
		// Skip known internal properties
		if (Prop->GetName() == TEXT("MaterialExpressionEditorX") || 
			Prop->GetName() == TEXT("MaterialExpressionEditorY") ||
			Prop->GetName() == TEXT("MaterialExpressionGuid") ||
			Prop->GetName() == TEXT("Desc") ||
			Prop->GetName() == TEXT("bCollapsed") ||
			Prop->GetName() == TEXT("Outputs"))
		{
			continue;
		}
		
		// Skip FExpressionInput properties (handled by ExportExpressionInputs)
		if (Prop->IsA<FStructProperty>())
		{
			auto* StructProp = CastField<FStructProperty>(Prop);
			if (StructProp && StructProp->Struct && StructProp->Struct->GetName() == TEXT("ExpressionInput"))
			{
				continue;
			}
		}
		
		// Compare with CDO
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
		const void* DefaultPtr = Prop->ContainerPtrToValuePtr<void>(CDO);
		
		if (Prop->Identical(ValuePtr, DefaultPtr)) continue;
		
		// Export the value
		FString PropValue;
		Prop->ExportText_Direct(PropValue, ValuePtr, DefaultPtr, nullptr, PPF_None);
		
		if (!PropValue.IsEmpty())
		{
			FString PropName = CamelToKebab(Prop->GetName());
			Node->Properties.Add(PropName, EscapeForDSLString(PropValue));
		}
	}
}

void FMatBPExporter::ExportExpressionInputs(UMaterialExpression* Expr, TSharedPtr<FMatExpressionAST> Node)
{
	// Iterate over expression inputs using GetInput(index) API
	const int32 NumInputs = Expr->CountInputs();
	
	for (int32 i = 0; i < NumInputs; ++i)
	{
		FExpressionInput* Input = Expr->GetInput(i);
		if (!Input) continue;
		
		// Get input name
		FName InputName = Expr->GetInputName(i);
		FString InputNameStr = InputName.IsNone() ? FString::Printf(TEXT("input-%d"), i) : CamelToKebab(InputName.ToString());
		
		// Check if connected
		if (Input->Expression)
		{
			FString TargetId = GetOrAssignId(Input->Expression);
			int32 OutputIdx = Input->OutputIndex;
			Node->AddInput(InputNameStr, FMatLangConnection(TargetId, OutputIdx));
		}
	}
}

void FMatBPExporter::ExportOutputs(TSharedPtr<FMaterialGraphAST> AST)
{
	// Material property inputs — these are the "outputs" of the material graph
	// (connections from expressions to the material result node)
	
	struct FMaterialInputInfo
	{
		FString Name;
		FExpressionInput* Input;
	};
	
	TArray<FMaterialInputInfo> MaterialInputs;
	
	// Use GetEditorOnlyData() to access material inputs in UE5
	auto AddInput = [&](const FString& Name, FExpressionInput& Input)
	{
		if (Input.Expression)
		{
			MaterialInputs.Add({Name, &Input});
		}
	};
	
	// Access material inputs through the public accessors
	// In UE5.x, material inputs are stored in EditorOnlyData
	if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
	{
		AddInput(TEXT("base-color"), EditorData->BaseColor);
		AddInput(TEXT("metallic"), EditorData->Metallic);
		AddInput(TEXT("specular"), EditorData->Specular);
		AddInput(TEXT("roughness"), EditorData->Roughness);
		AddInput(TEXT("anisotropy"), EditorData->Anisotropy);
		AddInput(TEXT("emissive-color"), EditorData->EmissiveColor);
		AddInput(TEXT("opacity"), EditorData->Opacity);
		AddInput(TEXT("opacity-mask"), EditorData->OpacityMask);
		AddInput(TEXT("normal"), EditorData->Normal);
		AddInput(TEXT("tangent"), EditorData->Tangent);
		AddInput(TEXT("world-position-offset"), EditorData->WorldPositionOffset);
		AddInput(TEXT("subsurface-color"), EditorData->SubsurfaceColor);
		AddInput(TEXT("ambient-occlusion"), EditorData->AmbientOcclusion);
		AddInput(TEXT("refraction"), EditorData->Refraction);
		AddInput(TEXT("pixel-depth-offset"), EditorData->PixelDepthOffset);
	}
	
	for (const auto& MI : MaterialInputs)
	{
		FMatLangInput Input;
		Input.Name = MI.Name;
		
		FString TargetId = GetOrAssignId(MI.Input->Expression);
		Input.Connection = FMatLangConnection(TargetId, MI.Input->OutputIndex);
		
		AST->Outputs.Slots.Add(MI.Name, MoveTemp(Input));
	}
}

// ========== Enum Mapping ==========

EMatLangDomain FMatBPExporter::MapDomain(int32 UEDomain)
{
	switch ((EMaterialDomain)UEDomain)
	{
		case MD_Surface:       return EMatLangDomain::Surface;
		case MD_DeferredDecal: return EMatLangDomain::DeferredDecal;
		case MD_LightFunction: return EMatLangDomain::LightFunction;
		case MD_Volume:        return EMatLangDomain::Volume;
		case MD_PostProcess:   return EMatLangDomain::PostProcess;
		case MD_UI:            return EMatLangDomain::UserInterface;
		default: return EMatLangDomain::Surface;
	}
}

EMatLangBlendMode FMatBPExporter::MapBlendMode(int32 UEBlendMode)
{
	switch ((EBlendMode)UEBlendMode)
	{
		case BLEND_Opaque:         return EMatLangBlendMode::Opaque;
		case BLEND_Masked:         return EMatLangBlendMode::Masked;
		case BLEND_Translucent:    return EMatLangBlendMode::Translucent;
		case BLEND_Additive:       return EMatLangBlendMode::Additive;
		case BLEND_Modulate:       return EMatLangBlendMode::Modulate;
		case BLEND_AlphaComposite: return EMatLangBlendMode::AlphaComposite;
		case BLEND_AlphaHoldout:   return EMatLangBlendMode::AlphaHoldout;
		default: return EMatLangBlendMode::Opaque;
	}
}

EMatLangShadingModel FMatBPExporter::MapShadingModel(int32 UEShadingModel)
{
	switch ((EMaterialShadingModel)UEShadingModel)
	{
		case MSM_Unlit:              return EMatLangShadingModel::Unlit;
		case MSM_DefaultLit:         return EMatLangShadingModel::DefaultLit;
		case MSM_Subsurface:         return EMatLangShadingModel::Subsurface;
		case MSM_PreintegratedSkin:  return EMatLangShadingModel::PreintegratedSkin;
		case MSM_ClearCoat:          return EMatLangShadingModel::ClearCoat;
		case MSM_SubsurfaceProfile:  return EMatLangShadingModel::SubsurfaceProfile;
		case MSM_TwoSidedFoliage:    return EMatLangShadingModel::TwoSidedFoliage;
		case MSM_Hair:               return EMatLangShadingModel::Hair;
		case MSM_Cloth:              return EMatLangShadingModel::Cloth;
		case MSM_Eye:                return EMatLangShadingModel::Eye;
		case MSM_SingleLayerWater:   return EMatLangShadingModel::SingleLayerWater;
		case MSM_ThinTranslucent:    return EMatLangShadingModel::ThinTranslucent;
		case MSM_Strata:             return EMatLangShadingModel::Strata;
		default: return EMatLangShadingModel::DefaultLit;
	}
}

#else // !WITH_EDITOR

FString FMatBPExporter::ExportToString(UMaterial* Material)
{
	UE_LOG(LogMatBPExporter, Error, TEXT("Material export is only available in editor builds"));
	return TEXT("");
}

TSharedPtr<FMaterialGraphAST> FMatBPExporter::ExportToAST(UMaterial* Material)
{
	UE_LOG(LogMatBPExporter, Error, TEXT("Material export is only available in editor builds"));
	return nullptr;
}

#endif // WITH_EDITOR
