// MatLangPatcher.cpp - Incremental Patch System for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangPatcher.h"
#include "MatBPExporter.h"
#include "MatLangParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatLangPatcher, Log, All);

#if WITH_EDITOR

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionIf.h"
#include "MaterialDomain.h"

// ========== Public API ==========

FMatLangPatchResult FMatLangPatcher::Apply(
	UMaterial* Material,
	const FMatLangDiffResult& DiffResult,
	const TSharedPtr<FMaterialGraphAST>& NewAST)
{
	if (!Material || !NewAST)
	{
		FMatLangPatchResult Res;
		Res.bSuccess = false;
		Res.Messages.Add(TEXT("Null material or AST"));
		return Res;
	}

	FMatLangPatcher Patcher(Material, NewAST);
	Patcher.BuildExpressionMap();

	for (const FMatLangDiffEntry& Entry : DiffResult.Entries)
	{
		Patcher.ApplyEntry(Entry);
	}

	Patcher.RecompileMaterial();
	Patcher.Result.bSuccess = (Patcher.Result.NumFailed == 0);

	Patcher.Info(FString::Printf(TEXT("Patch complete: %d applied, %d skipped, %d failed"),
		Patcher.Result.NumApplied, Patcher.Result.NumSkipped, Patcher.Result.NumFailed));

	return Patcher.Result;
}

FMatLangPatchResult FMatLangPatcher::IncrementalUpdate(
	UMaterial* Material,
	const FString& NewDSL,
	FMatLangDiffResult* OutDiffResult)
{
	FMatLangPatchResult PatchResult;

	if (!Material)
	{
		PatchResult.bSuccess = false;
		PatchResult.Messages.Add(TEXT("Null material"));
		return PatchResult;
	}

	// Step 1: Export current material to OldAST
	TSharedPtr<FMaterialGraphAST> OldAST = FMatBPExporter::ExportToAST(Material);
	if (!OldAST)
	{
		PatchResult.bSuccess = false;
		PatchResult.Messages.Add(TEXT("Failed to export current material to AST"));
		return PatchResult;
	}

	// Step 2: Parse new DSL to NewAST
	TArray<FMatLangParseError> ParseErrors;
	TSharedPtr<FMaterialGraphAST> NewAST = FMatLangParser::Parse(NewDSL, ParseErrors);
	if (!NewAST)
	{
		PatchResult.bSuccess = false;
		for (const auto& Err : ParseErrors)
		{
			PatchResult.Messages.Add(Err.ToString());
		}
		return PatchResult;
	}

	// Step 3: Diff
	FMatLangDiffResult DiffResult = FMatLangDiffer::Diff(OldAST, NewAST);

	if (OutDiffResult)
	{
		*OutDiffResult = DiffResult;
	}

	UE_LOG(LogMatLangPatcher, Log, TEXT("IncrementalUpdate: %s"), *DiffResult.GetSummary());

	// Step 4: Check if patchable
	if (DiffResult.HasStructuralChanges())
	{
		PatchResult.bSuccess = false;
		PatchResult.Messages.Add(FString::Printf(TEXT("Structural changes detected (%d), incremental patch not possible"),
			DiffResult.NumStructural));
		return PatchResult;
	}

	if (DiffResult.IsEmpty())
	{
		PatchResult.bSuccess = true;
		PatchResult.Messages.Add(TEXT("No changes detected"));
		return PatchResult;
	}

	// Step 5: Apply patch
	return Apply(Material, DiffResult, NewAST);
}

// ========== Internal ==========

FMatLangPatcher::FMatLangPatcher(UMaterial* InMaterial, const TSharedPtr<FMaterialGraphAST>& InNewAST)
	: Material(InMaterial)
	, NewAST(InNewAST)
{
}

void FMatLangPatcher::BuildExpressionMap()
{
	IdToExpr.Empty();

	// We need to map $id to UMaterialExpression*
	// Strategy: Export current material to get the ExprToId mapping,
	// then invert it. The Exporter's ExportToAST gives us the AST with IDs
	// that should match the current expressions.
	//
	// Simpler approach: use the Desc field or expression index.
	// Best approach: re-export and match by ID.

	TSharedPtr<FMaterialGraphAST> CurrentAST = FMatBPExporter::ExportToAST(Material);
	if (!CurrentAST) return;

	// Build expression array indexed the same way as export
	TArray<UMaterialExpression*> AllExprs;
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		AllExprs.Add(Expr);
	}

	// The exporter assigns IDs in expression iteration order
	// Match by index
	for (int32 i = 0; i < CurrentAST->Expressions.Num() && i < AllExprs.Num(); ++i)
	{
		IdToExpr.Add(CurrentAST->Expressions[i]->Id, AllExprs[i]);
	}

	UE_LOG(LogMatLangPatcher, Log, TEXT("Built expression map: %d entries"), IdToExpr.Num());
}

void FMatLangPatcher::ApplyEntry(const FMatLangDiffEntry& Entry)
{
	// Skip structural changes — they should have been caught upstream
	if (Entry.Severity == EMatLangDiffSeverity::Structural)
	{
		Warn(FString::Printf(TEXT("Skipping structural change: %s"), *Entry.ToString()));
		Result.NumSkipped++;
		return;
	}

	// Skip cosmetic changes (editor position, comments) — optional, apply anyway
	// Cosmetic changes are still applied but won't cause recompile issues

	switch (Entry.Op)
	{
		case EMatLangDiffOp::MaterialPropertyChanged:
			ApplyMaterialProperty(Entry);
			break;

		case EMatLangDiffOp::ExprPropertyChanged:
		case EMatLangDiffOp::ExprPropertyAdded:
		case EMatLangDiffOp::ExprPropertyRemoved:
			ApplyExpressionProperty(Entry);
			break;

		case EMatLangDiffOp::ExprInputChanged:
		case EMatLangDiffOp::ExprInputAdded:
		case EMatLangDiffOp::ExprInputRemoved:
			ApplyExpressionInput(Entry);
			break;

		case EMatLangDiffOp::OutputChanged:
		case EMatLangDiffOp::OutputAdded:
		case EMatLangDiffOp::OutputRemoved:
			ApplyOutputChange(Entry);
			break;

		default:
			Warn(FString::Printf(TEXT("Unhandled diff op: %s"), *Entry.ToString()));
			Result.NumSkipped++;
			break;
	}
}

// ---- Material Property ----

void FMatLangPatcher::ApplyMaterialProperty(const FMatLangDiffEntry& Entry)
{
	if (Entry.Key == TEXT("blend-mode"))
	{
		Material->BlendMode = (EBlendMode)(int32)MatLangEnums::StringToBlendMode(Entry.NewValue);
		Result.NumApplied++;
	}
	else if (Entry.Key == TEXT("shading-model"))
	{
		Material->SetShadingModel(
			(EMaterialShadingModel)(int32)MatLangEnums::StringToShadingModel(Entry.NewValue));
		Result.NumApplied++;
	}
	else if (Entry.Key == TEXT("two-sided"))
	{
		Material->TwoSided = Entry.NewValue == TEXT("true");
		Result.NumApplied++;
	}
	else if (Entry.Key == TEXT("is-masked"))
	{
		// bIsMasked is derived from blend mode, not a direct property
		Result.NumSkipped++;
	}
	else if (Entry.Key == TEXT("opacity-mask-clip-value"))
	{
		Material->OpacityMaskClipValue = FCString::Atof(*Entry.NewValue);
		Result.NumApplied++;
	}
	else if (Entry.Key == TEXT("domain"))
	{
		// Domain change is structural, should not reach here
		Warn(TEXT("Domain change should be structural, skipping"));
		Result.NumSkipped++;
	}
	else
	{
		// Extra properties — try FProperty reflection on UMaterial
		FString PropName = KebabToCamel(Entry.Key);
		FProperty* Prop = Material->GetClass()->FindPropertyByName(*PropName);
		if (Prop)
		{
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Material);
			FString Value = Entry.NewValue;
			Value.RemoveFromStart(TEXT("\"")); Value.RemoveFromEnd(TEXT("\""));
			Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
			Result.NumApplied++;
		}
		else
		{
			Warn(FString::Printf(TEXT("Unknown material property '%s'"), *Entry.Key));
			Result.NumFailed++;
		}
	}
}

// ---- Expression Property ----

void FMatLangPatcher::ApplyExpressionProperty(const FMatLangDiffEntry& Entry)
{
	UMaterialExpression* Expr = FindExpressionById(Entry.ExprId);
	if (!Expr)
	{
		Warn(FString::Printf(TEXT("Expression '%s' not found for property change"), *Entry.ExprId));
		Result.NumFailed++;
		return;
	}

	if (Entry.Key == TEXT("editor-position"))
	{
		// Parse "(x y)" position
		FString Val = Entry.NewValue;
		Val.RemoveFromStart(TEXT("("));
		Val.RemoveFromEnd(TEXT(")"));
		TArray<FString> Parts;
		Val.ParseIntoArray(Parts, TEXT(" "), true);
		if (Parts.Num() >= 2)
		{
			Expr->MaterialExpressionEditorX = (int32)FCString::Atof(*Parts[0]);
			Expr->MaterialExpressionEditorY = (int32)FCString::Atof(*Parts[1]);
		}
		Result.NumApplied++;
		return;
	}

	if (Entry.Key == TEXT("comment"))
	{
		Expr->Desc = Entry.NewValue;
		Result.NumApplied++;
		return;
	}

	// Find expression in NewAST
	TSharedPtr<FMatExpressionAST> NewExpr = NewAST->FindExpression(Entry.ExprId);
	if (NewExpr)
	{
		SetExpressionPropertyFromAST(Expr, Entry.Key, Entry.NewValue);
	}
	else
	{
		// Fallback: direct FProperty reflection
		FString PropName = KebabToCamel(Entry.Key);
		FProperty* Prop = Expr->GetClass()->FindPropertyByName(*PropName);
		if (Prop)
		{
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
			FString Value = Entry.NewValue;
			Value.RemoveFromStart(TEXT("\"")); Value.RemoveFromEnd(TEXT("\""));
			Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
			Result.NumApplied++;
		}
		else
		{
			Warn(FString::Printf(TEXT("Property '%s' not found on expression '%s'"), *Entry.Key, *Entry.ExprId));
			Result.NumFailed++;
		}
	}
}

void FMatLangPatcher::SetExpressionPropertyFromAST(
	UMaterialExpression* Expr, const FString& Key, const FString& Value)
{
	// Type-specific handling (same logic as Importer::SetExpressionProperties but for single property)

	// ---- Constants ----
	if (auto* C = Cast<UMaterialExpressionConstant>(Expr))
	{
		if (Key == TEXT("value")) { C->R = FCString::Atof(*Value); Result.NumApplied++; return; }
	}
	if (auto* C2 = Cast<UMaterialExpressionConstant2Vector>(Expr))
	{
		if (Key == TEXT("value"))
		{
			FString V = Value; V.RemoveFromStart(TEXT("(")); V.RemoveFromEnd(TEXT(")"));
			TArray<FString> P; V.ParseIntoArray(P, TEXT(" "), true);
			if (P.Num() >= 2) { C2->R = FCString::Atof(*P[0]); C2->G = FCString::Atof(*P[1]); }
			Result.NumApplied++; return;
		}
	}
	if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
	{
		if (Key == TEXT("value"))
		{
			FString V = Value; V.RemoveFromStart(TEXT("(")); V.RemoveFromEnd(TEXT(")"));
			TArray<FString> P; V.ParseIntoArray(P, TEXT(" "), true);
			if (P.Num() >= 3) { C3->Constant = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])); }
			Result.NumApplied++; return;
		}
	}
	if (auto* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
	{
		if (Key == TEXT("value"))
		{
			FString V = Value; V.RemoveFromStart(TEXT("(")); V.RemoveFromEnd(TEXT(")"));
			TArray<FString> P; V.ParseIntoArray(P, TEXT(" "), true);
			if (P.Num() >= 4) { C4->Constant = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3])); }
			Result.NumApplied++; return;
		}
	}

	// ---- Parameters ----
	if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		if (Key == TEXT("name")) { FString N = Value; N.RemoveFromStart(TEXT("\"")); N.RemoveFromEnd(TEXT("\"")); SP->ParameterName = *N; Result.NumApplied++; return; }
		if (Key == TEXT("default")) { SP->DefaultValue = FCString::Atof(*Value); Result.NumApplied++; return; }
		if (Key == TEXT("group")) { FString G = Value; G.RemoveFromStart(TEXT("\"")); G.RemoveFromEnd(TEXT("\"")); SP->Group = *G; Result.NumApplied++; return; }
	}
	if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		if (Key == TEXT("name")) { FString N = Value; N.RemoveFromStart(TEXT("\"")); N.RemoveFromEnd(TEXT("\"")); VP->ParameterName = *N; Result.NumApplied++; return; }
		if (Key == TEXT("default"))
		{
			FString V = Value; V.RemoveFromStart(TEXT("(")); V.RemoveFromEnd(TEXT(")"));
			TArray<FString> P; V.ParseIntoArray(P, TEXT(" "), true);
			if (P.Num() >= 4) VP->DefaultValue = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]));
			else if (P.Num() >= 3) VP->DefaultValue = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), 1.0f);
			Result.NumApplied++; return;
		}
	}
	if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
	{
		if (Key == TEXT("name")) { FString N = Value; N.RemoveFromStart(TEXT("\"")); N.RemoveFromEnd(TEXT("\"")); SSP->ParameterName = *N; Result.NumApplied++; return; }
		if (Key == TEXT("default")) { SSP->DefaultValue = Value == TEXT("true"); Result.NumApplied++; return; }
	}

	// ---- Texture ----
	if (auto* TS = Cast<UMaterialExpressionTextureSample>(Expr))
	{
		if (Key == TEXT("texture"))
		{
			FString Path = Value;
			if (Path.StartsWith(TEXT("(asset "))) { Path = Path.Mid(7); Path.RemoveFromEnd(TEXT(")")); Path.TrimStartAndEndInline(); }
			Path.RemoveFromStart(TEXT("\"")); Path.RemoveFromEnd(TEXT("\""));
			UTexture* Tex = LoadObject<UTexture>(nullptr, *Path);
			if (Tex) { TS->Texture = Tex; Result.NumApplied++; } else { Warn(FString::Printf(TEXT("Failed to load texture: %s"), *Path)); Result.NumFailed++; }
			return;
		}
	}

	// ---- TextureCoordinate ----
	if (auto* TC = Cast<UMaterialExpressionTextureCoordinate>(Expr))
	{
		if (Key == TEXT("coordinate-index")) { TC->CoordinateIndex = (int32)FCString::Atof(*Value); Result.NumApplied++; return; }
		if (Key == TEXT("u-tiling")) { TC->UTiling = FCString::Atof(*Value); Result.NumApplied++; return; }
		if (Key == TEXT("v-tiling")) { TC->VTiling = FCString::Atof(*Value); Result.NumApplied++; return; }
	}

	// ---- ComponentMask ----
	if (auto* CM = Cast<UMaterialExpressionComponentMask>(Expr))
	{
		if (Key == TEXT("mask"))
		{
			CM->R = Value.Contains(TEXT("r")); CM->G = Value.Contains(TEXT("g"));
			CM->B = Value.Contains(TEXT("b")); CM->A = Value.Contains(TEXT("a"));
			Result.NumApplied++; return;
		}
	}

	// ---- Panner ----
	if (auto* Pan = Cast<UMaterialExpressionPanner>(Expr))
	{
		if (Key == TEXT("speed-x")) { Pan->SpeedX = FCString::Atof(*Value); Result.NumApplied++; return; }
		if (Key == TEXT("speed-y")) { Pan->SpeedY = FCString::Atof(*Value); Result.NumApplied++; return; }
	}

	// ---- Fresnel ----
	if (auto* F = Cast<UMaterialExpressionFresnel>(Expr))
	{
		if (Key == TEXT("exponent")) { F->Exponent = FCString::Atof(*Value); Result.NumApplied++; return; }
		if (Key == TEXT("base-reflect-fraction")) { F->BaseReflectFraction = FCString::Atof(*Value); Result.NumApplied++; return; }
	}

	// ---- If ----
	if (auto* IfE = Cast<UMaterialExpressionIf>(Expr))
	{
		if (Key == TEXT("equality-threshold")) { IfE->EqualsThreshold = FCString::Atof(*Value); Result.NumApplied++; return; }
	}

	// ---- Custom ----
	if (auto* Cust = Cast<UMaterialExpressionCustom>(Expr))
	{
		if (Key == TEXT("code")) { FString C = Value; C.RemoveFromStart(TEXT("\"")); C.RemoveFromEnd(TEXT("\"")); C = C.Replace(TEXT("\\\""), TEXT("\"")); Cust->Code = C; Result.NumApplied++; return; }
		if (Key == TEXT("output-type")) { Cust->OutputType = (ECustomMaterialOutputType)FCString::Atoi(*Value); Result.NumApplied++; return; }
		if (Key == TEXT("description")) { FString D = Value; D.RemoveFromStart(TEXT("\"")); D.RemoveFromEnd(TEXT("\"")); Cust->Description = D; Result.NumApplied++; return; }
	}

	// ---- Generic FProperty reflection fallback ----
	FString PropName = KebabToCamel(Key);
	FProperty* Prop = Expr->GetClass()->FindPropertyByName(*PropName);
	if (Prop)
	{
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
		FString CleanValue = Value;
		CleanValue.RemoveFromStart(TEXT("\"")); CleanValue.RemoveFromEnd(TEXT("\""));
		Prop->ImportText_Direct(*CleanValue, ValuePtr, nullptr, PPF_None);
		Result.NumApplied++;
	}
	else
	{
		Warn(FString::Printf(TEXT("Property '%s' not found on %s (tried '%s')"),
			*Key, *Expr->GetClass()->GetName(), *PropName));
		Result.NumFailed++;
	}
}

// ---- Expression Input ----

void FMatLangPatcher::ApplyExpressionInput(const FMatLangDiffEntry& Entry)
{
	UMaterialExpression* Expr = FindExpressionById(Entry.ExprId);
	if (!Expr)
	{
		Warn(FString::Printf(TEXT("Expression '%s' not found for input change"), *Entry.ExprId));
		Result.NumFailed++;
		return;
	}

	// Get the new input data from NewAST
	TSharedPtr<FMatExpressionAST> NewExpr = NewAST->FindExpression(Entry.ExprId);
	if (!NewExpr)
	{
		Warn(FString::Printf(TEXT("Expression '%s' not found in new AST"), *Entry.ExprId));
		Result.NumFailed++;
		return;
	}

	if (Entry.Op == EMatLangDiffOp::ExprInputRemoved)
	{
		// Disconnect the input
		FString CamelName = KebabToCamel(Entry.Key);
		const int32 NumInputs = Expr->CountInputs();
		for (int32 i = 0; i < NumInputs; ++i)
		{
			FName InputName = Expr->GetInputName(i);
			if (InputName.ToString().Equals(CamelName, ESearchCase::IgnoreCase))
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (Input)
				{
					Input->Expression = nullptr;
					Input->OutputIndex = 0;
					Result.NumApplied++;
					return;
				}
			}
		}
		Warn(FString::Printf(TEXT("Input '%s' not found on expression '%s'"), *Entry.Key, *Entry.ExprId));
		Result.NumFailed++;
		return;
	}

	// Changed or Added — find the new connection in AST
	const FMatLangInput* NewInput = NewExpr->FindInput(Entry.Key);
	if (!NewInput)
	{
		Warn(FString::Printf(TEXT("Input '%s' not found in new AST for '%s'"), *Entry.Key, *Entry.ExprId));
		Result.NumFailed++;
		return;
	}

	WireExpressionInput(Expr, Entry.Key, *NewInput);
}

void FMatLangPatcher::WireExpressionInput(
	UMaterialExpression* Expr, const FString& InputName, const FMatLangInput& InputData)
{
	FString CamelName = KebabToCamel(InputName);
	const int32 NumInputs = Expr->CountInputs();
	int32 TargetIdx = -1;

	// Find by name
	for (int32 i = 0; i < NumInputs; ++i)
	{
		FName UEName = Expr->GetInputName(i);
		FString NameStr = UEName.ToString();
		if (NameStr.Equals(CamelName, ESearchCase::IgnoreCase) ||
			NameStr.Replace(TEXT(" "), TEXT("")).Equals(CamelName, ESearchCase::IgnoreCase))
		{
			TargetIdx = i;
			break;
		}
	}

	// Fallback by index
	if (TargetIdx < 0)
	{
		if (InputName.StartsWith(TEXT("input-")))
		{
			TargetIdx = FCString::Atoi(*InputName.Mid(6));
		}
		else if (InputName == TEXT("a") && NumInputs > 0) TargetIdx = 0;
		else if (InputName == TEXT("b") && NumInputs > 1) TargetIdx = 1;
	}

	if (TargetIdx < 0 || TargetIdx >= NumInputs)
	{
		Warn(FString::Printf(TEXT("Input '%s' not found on %s"), *InputName, *Expr->GetClass()->GetName()));
		Result.NumFailed++;
		return;
	}

	FExpressionInput* Input = Expr->GetInput(TargetIdx);
	if (!Input)
	{
		Result.NumFailed++;
		return;
	}

	if (InputData.IsConnected())
	{
		UMaterialExpression* TargetExpr = FindExpressionById(InputData.Connection->TargetId);
		if (TargetExpr)
		{
			Input->Expression = TargetExpr;
			Input->OutputIndex = InputData.Connection->OutputIndex;
			Result.NumApplied++;
		}
		else
		{
			Warn(FString::Printf(TEXT("Target expression '%s' not found"), *InputData.Connection->TargetId));
			Result.NumFailed++;
		}
	}
	else
	{
		// Disconnect
		Input->Expression = nullptr;
		Input->OutputIndex = 0;
		Result.NumApplied++;
	}
}

// ---- Output Slot ----

void FMatLangPatcher::ApplyOutputChange(const FMatLangDiffEntry& Entry)
{
	if (Entry.Op == EMatLangDiffOp::OutputRemoved)
	{
		// Clear the output slot
		FMatLangInput EmptyInput;
		EmptyInput.Name = Entry.Key;
		WireOutputSlot(Entry.Key, EmptyInput);
		return;
	}

	// Changed or Added — get new connection from AST
	const FMatLangInput* NewOutput = NewAST->Outputs.Slots.Find(Entry.Key);
	if (!NewOutput)
	{
		Warn(FString::Printf(TEXT("Output '%s' not found in new AST"), *Entry.Key));
		Result.NumFailed++;
		return;
	}

	WireOutputSlot(Entry.Key, *NewOutput);
}

void FMatLangPatcher::WireOutputSlot(const FString& SlotName, const FMatLangInput& InputData)
{
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		Result.NumFailed++;
		return;
	}

	// Map slot name to FExpressionInput*
	FExpressionInput* Target = nullptr;
	if (SlotName == TEXT("base-color")) Target = &EditorData->BaseColor;
	else if (SlotName == TEXT("metallic")) Target = &EditorData->Metallic;
	else if (SlotName == TEXT("specular")) Target = &EditorData->Specular;
	else if (SlotName == TEXT("roughness")) Target = &EditorData->Roughness;
	else if (SlotName == TEXT("anisotropy")) Target = &EditorData->Anisotropy;
	else if (SlotName == TEXT("emissive-color")) Target = &EditorData->EmissiveColor;
	else if (SlotName == TEXT("opacity")) Target = &EditorData->Opacity;
	else if (SlotName == TEXT("opacity-mask")) Target = &EditorData->OpacityMask;
	else if (SlotName == TEXT("normal")) Target = &EditorData->Normal;
	else if (SlotName == TEXT("tangent")) Target = &EditorData->Tangent;
	else if (SlotName == TEXT("world-position-offset")) Target = &EditorData->WorldPositionOffset;
	else if (SlotName == TEXT("subsurface-color")) Target = &EditorData->SubsurfaceColor;
	else if (SlotName == TEXT("ambient-occlusion")) Target = &EditorData->AmbientOcclusion;
	else if (SlotName == TEXT("refraction")) Target = &EditorData->Refraction;
	else if (SlotName == TEXT("pixel-depth-offset")) Target = &EditorData->PixelDepthOffset;

	if (!Target)
	{
		Warn(FString::Printf(TEXT("Unknown output slot '%s'"), *SlotName));
		Result.NumFailed++;
		return;
	}

	if (InputData.IsConnected())
	{
		UMaterialExpression* Expr = FindExpressionById(InputData.Connection->TargetId);
		if (Expr)
		{
			Target->Expression = Expr;
			Target->OutputIndex = InputData.Connection->OutputIndex;
			Result.NumApplied++;
		}
		else
		{
			Warn(FString::Printf(TEXT("Target expression '%s' not found for output '%s'"),
				*InputData.Connection->TargetId, *SlotName));
			Result.NumFailed++;
		}
	}
	else
	{
		// Clear this output slot
		Target->Expression = nullptr;
		Target->OutputIndex = 0;
		Result.NumApplied++;
	}
}

// ---- Helpers ----

UMaterialExpression* FMatLangPatcher::FindExpressionById(const FString& Id)
{
	if (auto* Found = IdToExpr.Find(Id))
	{
		return *Found;
	}
	return nullptr;
}

void FMatLangPatcher::RecompileMaterial()
{
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
}

FString FMatLangPatcher::KebabToCamel(const FString& KebabCase)
{
	FString Result;
	bool bCapNext = true;

	for (TCHAR Ch : KebabCase)
	{
		if (Ch == '-')
		{
			bCapNext = true;
		}
		else
		{
			Result += bCapNext ? FChar::ToUpper(Ch) : Ch;
			bCapNext = false;
		}
	}

	return Result;
}

void FMatLangPatcher::Warn(const FString& Message)
{
	UE_LOG(LogMatLangPatcher, Warning, TEXT("%s"), *Message);
	Result.Messages.Add(Message);
}

void FMatLangPatcher::Info(const FString& Message)
{
	UE_LOG(LogMatLangPatcher, Log, TEXT("%s"), *Message);
	Result.Messages.Add(Message);
}

#else // !WITH_EDITOR

FMatLangPatchResult FMatLangPatcher::Apply(
	UMaterial* Material,
	const FMatLangDiffResult& DiffResult,
	const TSharedPtr<FMaterialGraphAST>& NewAST)
{
	FMatLangPatchResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material patching is only available in editor builds"));
	return Res;
}

FMatLangPatchResult FMatLangPatcher::IncrementalUpdate(
	UMaterial* Material,
	const FString& NewDSL,
	FMatLangDiffResult* OutDiffResult)
{
	FMatLangPatchResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material patching is only available in editor builds"));
	return Res;
}

#endif // WITH_EDITOR
