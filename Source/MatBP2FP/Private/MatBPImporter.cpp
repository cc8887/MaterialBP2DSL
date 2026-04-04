// MatBPImporter.cpp - DSL to Material Blueprint Importer
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBPImporter.h"
#include "MatLangParser.h"
#include "MatLangDiffer.h"
#include "MatLangPatcher.h"
#include "MatBPExporter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatBPImporter, Log, All);

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
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionDesaturation.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "MaterialDomain.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "MaterialEditingLibrary.h"

// ========== Public API ==========

FMatBPImporter::FImportResult FMatBPImporter::ImportFromString(const FString& DSLSource, const FString& PackagePath)
{
	TArray<FMatLangParseError> ParseErrors;
	auto AST = FMatLangParser::Parse(DSLSource, ParseErrors);
	
	if (!AST)
	{
		FImportResult Res;
		Res.bSuccess = false;
		Res.Material = nullptr;
		Res.ExpressionsCreated = 0;
		Res.ConnectionsMade = 0;
		Res.Warnings = ParseErrors.Num();
		for (const auto& Err : ParseErrors)
		{
			Res.Messages.Add(Err.ToString());
		}
		return Res;
	}
	
	return ImportFromAST(AST, PackagePath);
}

FMatBPImporter::FImportResult FMatBPImporter::ImportFromAST(TSharedPtr<FMaterialGraphAST> AST, const FString& PackagePath)
{
	UMaterial* Mat = CreateMaterial(AST->Name, PackagePath);
	if (!Mat)
	{
		FImportResult Res;
		Res.bSuccess = false;
		Res.Material = nullptr;
		Res.Messages.Add(TEXT("Failed to create material"));
		return Res;
	}
	
	FMatBPImporter Importer(Mat, AST);
	return Importer.Import();
}

FMatBPImporter::FImportResult FMatBPImporter::UpdateMaterial(UMaterial* ExistingMaterial, const FString& DSLSource)
{
	TArray<FMatLangParseError> ParseErrors;
	auto AST = FMatLangParser::Parse(DSLSource, ParseErrors);
	
	if (!AST)
	{
		FImportResult Res;
		Res.bSuccess = false;
		Res.Material = ExistingMaterial;
		for (const auto& Err : ParseErrors)
		{
			Res.Messages.Add(Err.ToString());
		}
		return Res;
	}
	
	return UpdateMaterialFromAST(ExistingMaterial, AST);
}

FMatBPImporter::FImportResult FMatBPImporter::UpdateMaterialFromAST(UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> AST)
{
	FMatBPImporter Importer(ExistingMaterial, AST);
	Importer.ClearMaterial();
	return Importer.Import();
}

FMatBPImporter::FUpdateResult FMatBPImporter::UpdateMaterialDetailed(UMaterial* ExistingMaterial, const FString& NewDSL)
{
	FUpdateResult Result;

	if (!ExistingMaterial)
	{
		Result.bSuccess = false;
		Result.Messages.Add(TEXT("Null material"));
		return Result;
	}

	// Step 1: Parse new DSL
	TArray<FMatLangParseError> ParseErrors;
	auto NewAST = FMatLangParser::Parse(NewDSL, ParseErrors);
	if (!NewAST)
	{
		Result.bSuccess = false;
		for (const auto& Err : ParseErrors)
		{
			Result.Messages.Add(Err.ToString());
		}
		return Result;
	}

	return UpdateMaterialDetailedFromAST(ExistingMaterial, NewAST);
}

FMatBPImporter::FUpdateResult FMatBPImporter::UpdateMaterialDetailedFromAST(
	UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> NewAST)
{
	FUpdateResult Result;

	if (!ExistingMaterial || !NewAST)
	{
		Result.bSuccess = false;
		Result.Messages.Add(TEXT("Null material or AST"));
		return Result;
	}

	// Step 1: Export current material to OldAST
	TSharedPtr<FMaterialGraphAST> OldAST = FMatBPExporter::ExportToAST(ExistingMaterial);
	if (!OldAST)
	{
		// Cannot export — fall back to full rebuild
		UE_LOG(LogMatBPImporter, Warning, TEXT("UpdateMaterialDetailed: Failed to export current material, falling back to full rebuild"));
		Result.Messages.Add(TEXT("Failed to export current state, using full rebuild"));

		FMatBPImporter Importer(ExistingMaterial, NewAST);
		Importer.ClearMaterial();
		FImportResult ImportResult = Importer.Import();

		Result.bSuccess = ImportResult.bSuccess;
		Result.bUsedIncrementalPatch = false;
		Result.Messages.Append(ImportResult.Messages);
		return Result;
	}

	// Step 2: Diff
	FMatLangDiffResult DiffResult = FMatLangDiffer::Diff(OldAST, NewAST);
	Result.NumChanges = DiffResult.TotalChanges();
	Result.NumStructuralChanges = DiffResult.NumStructural;
	Result.NumPropertyChanges = DiffResult.NumProperty;

	UE_LOG(LogMatBPImporter, Log, TEXT("UpdateMaterialDetailed: %s"), *DiffResult.GetSummary());
	for (const auto& Entry : DiffResult.Entries)
	{
		UE_LOG(LogMatBPImporter, Verbose, TEXT("  %s"), *Entry.ToString());
	}

	// Step 3: No changes?
	if (DiffResult.IsEmpty())
	{
		UE_LOG(LogMatBPImporter, Log, TEXT("UpdateMaterialDetailed: No changes detected"));
		Result.bSuccess = true;
		Result.bUsedIncrementalPatch = true;
		Result.Messages.Add(TEXT("No changes detected"));
		return Result;
	}

	// Step 4: Try incremental patch if no structural changes
	if (!DiffResult.HasStructuralChanges())
	{
		UE_LOG(LogMatBPImporter, Log, TEXT("UpdateMaterialDetailed: Attempting incremental patch (%d property changes)"),
			DiffResult.NumProperty);

		FMatLangPatchResult PatchResult = FMatLangPatcher::Apply(ExistingMaterial, DiffResult, NewAST);

		if (PatchResult.bSuccess)
		{
			UE_LOG(LogMatBPImporter, Log, TEXT("UpdateMaterialDetailed: Incremental patch succeeded (%d applied, %d skipped)"),
				PatchResult.NumApplied, PatchResult.NumSkipped);

			Result.bSuccess = true;
			Result.bUsedIncrementalPatch = true;
			Result.NumApplied = PatchResult.NumApplied;
			Result.NumFailed = PatchResult.NumFailed;
			Result.Messages.Append(PatchResult.Messages);
			return Result;
		}

		// Patch failed — fall through to full rebuild
		UE_LOG(LogMatBPImporter, Warning, TEXT("UpdateMaterialDetailed: Incremental patch failed (%d failures), falling back to full rebuild"),
			PatchResult.NumFailed);
		Result.Messages.Add(TEXT("Incremental patch failed, using full rebuild"));
		Result.Messages.Append(PatchResult.Messages);
	}
	else
	{
		UE_LOG(LogMatBPImporter, Log, TEXT("UpdateMaterialDetailed: %d structural changes detected, using full rebuild"),
			DiffResult.NumStructural);
		Result.Messages.Add(FString::Printf(TEXT("Structural changes (%d), using full rebuild"), DiffResult.NumStructural));
	}

	// Step 5: Full rebuild
	FMatBPImporter Importer(ExistingMaterial, NewAST);
	Importer.ClearMaterial();
	FImportResult ImportResult = Importer.Import();

	Result.bSuccess = ImportResult.bSuccess;
	Result.bUsedIncrementalPatch = false;
	Result.Messages.Append(ImportResult.Messages);
	return Result;
}

FMatBPImporter::FMatBPImporter(UMaterial* InMaterial, TSharedPtr<FMaterialGraphAST> InAST)
	: Material(InMaterial), AST(InAST)
{
	Result.bSuccess = false;
	Result.Material = InMaterial;
	Result.ExpressionsCreated = 0;
	Result.ConnectionsMade = 0;
	Result.Warnings = 0;
}

FMatBPImporter::FImportResult FMatBPImporter::Import()
{
	// Step 1: Set material properties
	SetMaterialProperties();
	
	// Step 2: Create all expression nodes
	CreateExpressions();
	
	// Step 3: Wire all connections
	WireConnections();
	
	// Step 4: Wire material outputs
	WireMaterialOutputs();
	
	// Step 5: Update material
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	
	Result.bSuccess = true;
	Info(FString::Printf(TEXT("Import complete: %d expressions, %d connections, %d warnings"),
		Result.ExpressionsCreated, Result.ConnectionsMade, Result.Warnings));
	
	return Result;
}

UMaterial* FMatBPImporter::CreateMaterial(const FString& Name, const FString& PackagePath)
{
	FString FullPath = PackagePath / Name;
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogMatBPImporter, Error, TEXT("Failed to create package: %s"), *FullPath);
		return nullptr;
	}
	
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Mat = Cast<UMaterial>(Factory->FactoryCreateNew(
		UMaterial::StaticClass(), Package, *Name, RF_Public | RF_Standalone, nullptr, GWarn));
	
	if (Mat)
	{
		FAssetRegistryModule::AssetCreated(Mat);
	}
	
	return Mat;
}

void FMatBPImporter::SetMaterialProperties()
{
	Material->MaterialDomain = MapDomainToUE(AST->Domain);
	Material->BlendMode = MapBlendModeToUE(AST->BlendMode);
	Material->SetShadingModel(MapShadingModelToUE(AST->ShadingModel));
	Material->TwoSided = AST->bTwoSided;
	
	if (AST->bIsMasked)
	{
		Material->OpacityMaskClipValue = AST->OpacityMaskClipValue;
	}
}

void FMatBPImporter::CreateExpressions()
{
	for (const auto& ExprAST : AST->Expressions)
	{
		UMaterialExpression* Expr = CreateExpression(ExprAST);
		if (Expr)
		{
			IdToExpr.Add(ExprAST->Id, Expr);
			Result.ExpressionsCreated++;
		}
	}
}

UMaterialExpression* FMatBPImporter::CreateExpression(TSharedPtr<FMatExpressionAST> ExprAST)
{
	UClass* ExprClass = FindExpressionClass(ExprAST->ExprType);
	if (!ExprClass)
	{
		Warn(FString::Printf(TEXT("[DEGRADATION:ExprClass] Unknown expression type '%s' (id: %s)"), 
			*ExprAST->ExprType, *ExprAST->Id));
		return nullptr;
	}
	
	UMaterialExpression* Expr = NewObject<UMaterialExpression>(Material, ExprClass);
	if (!Expr)
	{
		Warn(FString::Printf(TEXT("Failed to create expression '%s'"), *ExprAST->Id));
		return nullptr;
	}
	
	// Set editor position
	Expr->MaterialExpressionEditorX = (int32)ExprAST->EditorPosition.X;
	Expr->MaterialExpressionEditorY = (int32)ExprAST->EditorPosition.Y;
	
	// Set description
	if (!ExprAST->Comment.IsEmpty())
	{
		Expr->Desc = ExprAST->Comment;
	}
	
	// Add to material
	Material->GetExpressionCollection().AddExpression(Expr);
	
	// Set type-specific properties
	SetExpressionProperties(ExprAST, Expr);
	
	return Expr;
}

void FMatBPImporter::WireConnections()
{
	for (const auto& ExprAST : AST->Expressions)
	{
		if (auto* Expr = IdToExpr.Find(ExprAST->Id))
		{
			WireExpressionInputs(ExprAST, *Expr);
		}
	}
}

void FMatBPImporter::WireExpressionInputs(TSharedPtr<FMatExpressionAST> ExprAST, UMaterialExpression* Expr)
{
	const int32 NumInputs = Expr->CountInputs();
	
	for (const FMatLangInput& InputAST : ExprAST->Inputs)
	{
		if (!InputAST.IsConnected()) continue;
		
		const FMatLangConnection& Conn = *InputAST.Connection;
		auto* TargetExpr = IdToExpr.Find(Conn.TargetId);
		if (!TargetExpr || !*TargetExpr)
		{
			Warn(FString::Printf(TEXT("[DEGRADATION:Connection] Expression '%s' input '%s' references unknown '%s'"),
				*ExprAST->Id, *InputAST.Name, *Conn.TargetId));
			continue;
		}
		
		// Find matching input by name.
		// Strategy:
		//   1. Direct match with the raw InputAST.Name (handles Unicode / space names from Exporter)
		//   2. KebabToCamel conversion (handles standard kebab-case names)
		//   3. Strip spaces from UE name and compare (handles "Scale Factor" -> "ScaleFactor")
		FString CamelName = KebabToCamel(InputAST.Name);
		bool bConnected = false;
		
		for (int32 i = 0; i < NumInputs; ++i)
		{
			FName UEInputName = Expr->GetInputName(i);
			FString UENameStr = UEInputName.ToString();
			if (UENameStr.Equals(InputAST.Name, ESearchCase::IgnoreCase) ||    // direct / Unicode
				UENameStr.Equals(CamelName, ESearchCase::IgnoreCase) ||         // kebab-to-camel
				UENameStr.Replace(TEXT(" "), TEXT("")).Equals(CamelName, ESearchCase::IgnoreCase)) // strip spaces
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (Input)
				{
					Input->Expression = *TargetExpr;
					Input->OutputIndex = Conn.OutputIndex;
					bConnected = true;
					Result.ConnectionsMade++;
					break;
				}
			}
		}
		
		if (!bConnected)
		{
			// Try index-based fallback: input-0 -> index 0, a -> index 0, b -> index 1
			int32 FallbackIdx = -1;
			if (InputAST.Name.StartsWith(TEXT("input-")))
			{
				FallbackIdx = FCString::Atoi(*InputAST.Name.Mid(6));
			}
			else if (InputAST.Name == TEXT("a") && NumInputs > 0) FallbackIdx = 0;
			else if (InputAST.Name == TEXT("b") && NumInputs > 1) FallbackIdx = 1;
			
			if (FallbackIdx >= 0 && FallbackIdx < NumInputs)
			{
				FExpressionInput* Input = Expr->GetInput(FallbackIdx);
				if (Input)
				{
					Input->Expression = *TargetExpr;
					Input->OutputIndex = Conn.OutputIndex;
					Result.ConnectionsMade++;
					bConnected = true;
				}
			}
			
			if (!bConnected)
			{
				Warn(FString::Printf(TEXT("[DEGRADATION:InputMatch] Expression '%s': no matching input for '%s'"),
					*ExprAST->Id, *InputAST.Name));
			}
		}
	}
}

void FMatBPImporter::WireMaterialOutputs()
{
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData) return;
	
	struct FOutputMapping
	{
		FString DSLName;
		FExpressionInput* UEInput;
	};
	
	TArray<FOutputMapping> Mappings;
	Mappings.Add({TEXT("base-color"), &EditorData->BaseColor});
	Mappings.Add({TEXT("metallic"), &EditorData->Metallic});
	Mappings.Add({TEXT("specular"), &EditorData->Specular});
	Mappings.Add({TEXT("roughness"), &EditorData->Roughness});
	Mappings.Add({TEXT("anisotropy"), &EditorData->Anisotropy});
	Mappings.Add({TEXT("emissive-color"), &EditorData->EmissiveColor});
	Mappings.Add({TEXT("opacity"), &EditorData->Opacity});
	Mappings.Add({TEXT("opacity-mask"), &EditorData->OpacityMask});
	Mappings.Add({TEXT("normal"), &EditorData->Normal});
	Mappings.Add({TEXT("tangent"), &EditorData->Tangent});
	Mappings.Add({TEXT("world-position-offset"), &EditorData->WorldPositionOffset});
	Mappings.Add({TEXT("subsurface-color"), &EditorData->SubsurfaceColor});
	Mappings.Add({TEXT("ambient-occlusion"), &EditorData->AmbientOcclusion});
	Mappings.Add({TEXT("refraction"), &EditorData->Refraction});
	Mappings.Add({TEXT("pixel-depth-offset"), &EditorData->PixelDepthOffset});
	
	for (const auto& Pair : AST->Outputs.Slots)
	{
		const FMatLangInput& Output = Pair.Value;
		if (!Output.IsConnected()) continue;
		
		// Find matching UE material input
		for (const auto& Mapping : Mappings)
		{
			if (Mapping.DSLName == Pair.Key)
			{
				auto* TargetExpr = IdToExpr.Find(Output.Connection->TargetId);
				if (TargetExpr && *TargetExpr)
				{
					Mapping.UEInput->Expression = *TargetExpr;
					Mapping.UEInput->OutputIndex = Output.Connection->OutputIndex;
					Result.ConnectionsMade++;
				}
				else
				{
					Warn(FString::Printf(TEXT("[DEGRADATION:OutputConnection] Output '%s' references unknown '%s'"),
						*Pair.Key, *Output.Connection->TargetId));
				}
				break;
			}
		}
	}
}

void FMatBPImporter::SetExpressionProperties(TSharedPtr<FMatExpressionAST> ExprAST, UMaterialExpression* Expr)
{
	// ---- Constants ----
	if (auto* C = Cast<UMaterialExpressionConstant>(Expr))
	{
		C->R = ExprAST->GetFloatProperty(TEXT("value"), 0.0f);
		return;
	}
	if (auto* C2 = Cast<UMaterialExpressionConstant2Vector>(Expr))
	{
		FString Val = ExprAST->GetStringProperty(TEXT("value"));
		// Parse (x y)
		Val.RemoveFromStart(TEXT("("));
		Val.RemoveFromEnd(TEXT(")"));
		TArray<FString> Parts;
		Val.ParseIntoArray(Parts, TEXT(" "), true);
		if (Parts.Num() >= 2)
		{
			C2->R = FCString::Atof(*Parts[0]);
			C2->G = FCString::Atof(*Parts[1]);
		}
		return;
	}
	if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
	{
		FString Val = ExprAST->GetStringProperty(TEXT("value"));
		Val.RemoveFromStart(TEXT("("));
		Val.RemoveFromEnd(TEXT(")"));
		TArray<FString> Parts;
		Val.ParseIntoArray(Parts, TEXT(" "), true);
		if (Parts.Num() >= 3)
		{
			C3->Constant = FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
		}
		return;
	}
	if (auto* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
	{
		FString Val = ExprAST->GetStringProperty(TEXT("value"));
		Val.RemoveFromStart(TEXT("("));
		Val.RemoveFromEnd(TEXT(")"));
		TArray<FString> Parts;
		Val.ParseIntoArray(Parts, TEXT(" "), true);
		if (Parts.Num() >= 4)
		{
			C4->Constant = FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
		}
		return;
	}
	
	// ---- Parameters ----
	if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		FString Name = ExprAST->GetStringProperty(TEXT("name"));
		Name.RemoveFromStart(TEXT("\"")); Name.RemoveFromEnd(TEXT("\""));
		SP->ParameterName = *Name;
		SP->DefaultValue = ExprAST->GetFloatProperty(TEXT("default"), 0.0f);
		FString Group = ExprAST->GetStringProperty(TEXT("group"));
		Group.RemoveFromStart(TEXT("\"")); Group.RemoveFromEnd(TEXT("\""));
		if (!Group.IsEmpty()) SP->Group = *Group;
		return;
	}
	if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		FString Name = ExprAST->GetStringProperty(TEXT("name"));
		Name.RemoveFromStart(TEXT("\"")); Name.RemoveFromEnd(TEXT("\""));
		VP->ParameterName = *Name;
		FString DefVal = ExprAST->GetStringProperty(TEXT("default"));
		DefVal.RemoveFromStart(TEXT("(")); DefVal.RemoveFromEnd(TEXT(")"));
		TArray<FString> Parts;
		DefVal.ParseIntoArray(Parts, TEXT(" "), true);
		if (Parts.Num() >= 4)
		{
			VP->DefaultValue = FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
				FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
		}
		else if (Parts.Num() >= 3)
		{
			VP->DefaultValue = FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
				FCString::Atof(*Parts[2]), 1.0f);
		}
		return;
	}
	if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
	{
		FString Name = ExprAST->GetStringProperty(TEXT("name"));
		Name.RemoveFromStart(TEXT("\"")); Name.RemoveFromEnd(TEXT("\""));
		SSP->ParameterName = *Name;
		SSP->DefaultValue = ExprAST->GetBoolProperty(TEXT("default"), false);
		return;
	}
	
	// ---- Texture ----
	if (auto* TS = Cast<UMaterialExpressionTextureSample>(Expr))
	{
		FString TexPath = ExtractAssetPath(ExprAST->GetStringProperty(TEXT("texture")));
		if (!TexPath.IsEmpty())
		{
			UTexture* Tex = LoadObject<UTexture>(nullptr, *TexPath);
			if (Tex)
			{
				TS->Texture = Tex;
			}
			else
			{
				Warn(FString::Printf(TEXT("[DEGRADATION:AssetLoad] Failed to load texture: %s"), *TexPath));
			}
		}
		return;
	}
	if (auto* TOP = Cast<UMaterialExpressionTextureObjectParameter>(Expr))
	{
		FString Name = ExprAST->GetStringProperty(TEXT("name"));
		Name.RemoveFromStart(TEXT("\"")); Name.RemoveFromEnd(TEXT("\""));
		TOP->ParameterName = *Name;
		FString TexPath = ExtractAssetPath(ExprAST->GetStringProperty(TEXT("default-texture")));
		if (!TexPath.IsEmpty())
		{
			UTexture* Tex = LoadObject<UTexture>(nullptr, *TexPath);
			if (Tex) TOP->Texture = Tex;
		}
		return;
	}
	
	// ---- Texture Coordinate ----
	if (auto* TC = Cast<UMaterialExpressionTextureCoordinate>(Expr))
	{
		TC->CoordinateIndex = (int32)ExprAST->GetFloatProperty(TEXT("coordinate-index"), 0.0f);
		TC->UTiling = ExprAST->GetFloatProperty(TEXT("u-tiling"), 1.0f);
		TC->VTiling = ExprAST->GetFloatProperty(TEXT("v-tiling"), 1.0f);
		return;
	}
	
	// ---- ComponentMask ----
	if (auto* CM = Cast<UMaterialExpressionComponentMask>(Expr))
	{
		FString Mask = ExprAST->GetStringProperty(TEXT("mask"));
		CM->R = Mask.Contains(TEXT("r"));
		CM->G = Mask.Contains(TEXT("g"));
		CM->B = Mask.Contains(TEXT("b"));
		CM->A = Mask.Contains(TEXT("a"));
		return;
	}
	
	// ---- Panner ----
	if (auto* Pan = Cast<UMaterialExpressionPanner>(Expr))
	{
		Pan->SpeedX = ExprAST->GetFloatProperty(TEXT("speed-x"), 0.0f);
		Pan->SpeedY = ExprAST->GetFloatProperty(TEXT("speed-y"), 0.0f);
		return;
	}
	
	// ---- Fresnel ----
	if (auto* Fresnel = Cast<UMaterialExpressionFresnel>(Expr))
	{
		Fresnel->Exponent = ExprAST->GetFloatProperty(TEXT("exponent"), 5.0f);
		Fresnel->BaseReflectFraction = ExprAST->GetFloatProperty(TEXT("base-reflect-fraction"), 0.04f);
		return;
	}
	
	// ---- If ----
	if (auto* IfExpr = Cast<UMaterialExpressionIf>(Expr))
	{
		IfExpr->EqualsThreshold = ExprAST->GetFloatProperty(TEXT("equality-threshold"), 0.0f);
		return;
	}
	
	// ---- Custom (HLSL) ----
	if (auto* Custom = Cast<UMaterialExpressionCustom>(Expr))
	{
		FString Code = ExprAST->GetStringProperty(TEXT("code"));
		Code.RemoveFromStart(TEXT("\"")); Code.RemoveFromEnd(TEXT("\""));
		Code = Code.Replace(TEXT("\\\""), TEXT("\""));
		Custom->Code = Code;
		
		FString OutputTypeStr = ExprAST->GetStringProperty(TEXT("output-type"));
		if (!OutputTypeStr.IsEmpty())
		{
			Custom->OutputType = (ECustomMaterialOutputType)FCString::Atoi(*OutputTypeStr);
		}
		
		FString Desc = ExprAST->GetStringProperty(TEXT("description"));
		Desc.RemoveFromStart(TEXT("\"")); Desc.RemoveFromEnd(TEXT("\""));
		if (!Desc.IsEmpty()) Custom->Description = Desc;
		return;
	}
	
	// ---- MaterialFunctionCall ----
	if (auto* MFC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
	{
		FString FuncPath = ExtractAssetPath(ExprAST->GetStringProperty(TEXT("function")));
		if (!FuncPath.IsEmpty())
		{
			UMaterialFunction* Func = LoadObject<UMaterialFunction>(nullptr, *FuncPath);
			if (Func)
			{
				MFC->SetMaterialFunction(Func);
			}
			else
			{
				Warn(FString::Printf(TEXT("[DEGRADATION:AssetLoad] Failed to load material function: %s"), *FuncPath));
			}
		}
		return;
	}
	
	// ---- SetMaterialAttributes ----
	// The 'inputs' and 'attribute-set-types' properties encode the full FExpressionInput
	// array in a complex UObject export format that cannot be safely round-tripped via
	// ImportText_Direct.  The actual pin connections are re-wired by WireExpressionInputs
	// using the stored Inputs list, so we just skip those two properties here.
	if (Expr->GetClass()->GetName() == TEXT("MaterialExpressionSetMaterialAttributes"))
	{
		// No additional setup needed — connections handled by WireExpressionInputs
		return;
	}

	// ---- Generic: use FProperty reflection ----
	UClass* ExprClass = Expr->GetClass();
	// Properties that must not be restored via ImportText_Direct (crash-prone complex types)
	static const TArray<FString> SkipProps = {
		TEXT("inputs"),               // FExpressionInput array (SetMaterialAttributes)
		TEXT("attribute-set-types"),  // FGuid array (SetMaterialAttributes)
	};
	for (const auto& Pair : ExprAST->Properties)
	{
		if (SkipProps.Contains(Pair.Key)) continue;
		FString PropName = KebabToCamel(Pair.Key);
		FProperty* Prop = ExprClass->FindPropertyByName(*PropName);
		if (!Prop) continue;
		
		FString Value = Pair.Value;
		// Strip outer quotes and unescape: \" -> ", \\ -> \  (order matters)
		if (Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
		{
			Value = Value.Mid(1, Value.Len() - 2);
			Value = Value.Replace(TEXT("\\\""), TEXT("\""));
			Value = Value.Replace(TEXT("\\\\"), TEXT("\\"));
		}
		
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);
		Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
	}
}

UClass* FMatBPImporter::FindExpressionClass(const FString& ExprType)
{
	FString ClassName = TEXT("MaterialExpression") + KebabToCamel(ExprType);
	
	// Try direct class lookup
	UClass* Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
	if (Class && Class->IsChildOf(UMaterialExpression::StaticClass()))
	{
		return Class;
	}
	
	// Fallback: iterate all UMaterialExpression subclasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UMaterialExpression::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName() == ClassName)
			{
				return *It;
			}
		}
	}
	
	UE_LOG(LogMatBPImporter, Warning, TEXT("Could not find expression class for type '%s' (tried '%s')"), *ExprType, *ClassName);
	return nullptr;
}

FString FMatBPImporter::KebabToCamel(const FString& KebabCase)
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

EMaterialDomain FMatBPImporter::MapDomainToUE(EMatLangDomain Domain)
{
	switch (Domain)
	{
		case EMatLangDomain::Surface:        return MD_Surface;
		case EMatLangDomain::DeferredDecal:  return MD_DeferredDecal;
		case EMatLangDomain::LightFunction:  return MD_LightFunction;
		case EMatLangDomain::Volume:         return MD_Volume;
		case EMatLangDomain::PostProcess:    return MD_PostProcess;
		case EMatLangDomain::UserInterface:  return MD_UI;
		default: return MD_Surface;
	}
}

EBlendMode FMatBPImporter::MapBlendModeToUE(EMatLangBlendMode Mode)
{
	switch (Mode)
	{
		case EMatLangBlendMode::Opaque:          return BLEND_Opaque;
		case EMatLangBlendMode::Masked:          return BLEND_Masked;
		case EMatLangBlendMode::Translucent:     return BLEND_Translucent;
		case EMatLangBlendMode::Additive:        return BLEND_Additive;
		case EMatLangBlendMode::Modulate:        return BLEND_Modulate;
		case EMatLangBlendMode::AlphaComposite:  return BLEND_AlphaComposite;
		case EMatLangBlendMode::AlphaHoldout:    return BLEND_AlphaHoldout;
		default: return BLEND_Opaque;
	}
}

EMaterialShadingModel FMatBPImporter::MapShadingModelToUE(EMatLangShadingModel Model)
{
	switch (Model)
	{
		case EMatLangShadingModel::Unlit:              return MSM_Unlit;
		case EMatLangShadingModel::DefaultLit:         return MSM_DefaultLit;
		case EMatLangShadingModel::Subsurface:         return MSM_Subsurface;
		case EMatLangShadingModel::PreintegratedSkin:  return MSM_PreintegratedSkin;
		case EMatLangShadingModel::ClearCoat:          return MSM_ClearCoat;
		case EMatLangShadingModel::SubsurfaceProfile:  return MSM_SubsurfaceProfile;
		case EMatLangShadingModel::TwoSidedFoliage:    return MSM_TwoSidedFoliage;
		case EMatLangShadingModel::Hair:               return MSM_Hair;
		case EMatLangShadingModel::Cloth:              return MSM_Cloth;
		case EMatLangShadingModel::Eye:                return MSM_Eye;
		case EMatLangShadingModel::SingleLayerWater:   return MSM_SingleLayerWater;
		case EMatLangShadingModel::ThinTranslucent:    return MSM_ThinTranslucent;
		case EMatLangShadingModel::Strata:             return MSM_Strata;
		default: return MSM_DefaultLit;
	}
}

void FMatBPImporter::ClearMaterial()
{
	// Remove all existing expressions
	Material->GetExpressionCollection().Empty();
	
	// Clear all material input connections
	if (UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
	{
		EditorData->BaseColor = FColorMaterialInput();
		EditorData->Metallic = FScalarMaterialInput();
		EditorData->Specular = FScalarMaterialInput();
		EditorData->Roughness = FScalarMaterialInput();
		EditorData->Anisotropy = FScalarMaterialInput();
		EditorData->EmissiveColor = FColorMaterialInput();
		EditorData->Opacity = FScalarMaterialInput();
		EditorData->OpacityMask = FScalarMaterialInput();
		EditorData->Normal = FVectorMaterialInput();
		EditorData->Tangent = FVectorMaterialInput();
		EditorData->WorldPositionOffset = FVectorMaterialInput();
		EditorData->SubsurfaceColor = FColorMaterialInput();
		EditorData->AmbientOcclusion = FScalarMaterialInput();
		EditorData->Refraction = FScalarMaterialInput();
		EditorData->PixelDepthOffset = FScalarMaterialInput();
	}
}

void FMatBPImporter::Warn(const FString& Message)
{
	UE_LOG(LogMatBPImporter, Warning, TEXT("%s"), *Message);
	Result.Messages.Add(Message);
	Result.Warnings++;
}

void FMatBPImporter::Info(const FString& Message)
{
	UE_LOG(LogMatBPImporter, Log, TEXT("%s"), *Message);
	Result.Messages.Add(Message);
}

FString FMatBPImporter::ExtractAssetPath(const FString& Value)
{
	// (asset "/Game/Path/To/Asset")
	FString Path = Value;
	if (Path.StartsWith(TEXT("(asset ")))
	{
		Path = Path.Mid(7); // Remove "(asset "
		Path.RemoveFromEnd(TEXT(")"));
		Path.TrimStartAndEndInline();
		Path.RemoveFromStart(TEXT("\""));
		Path.RemoveFromEnd(TEXT("\""));
	}
	else
	{
		// Just a quoted path
		Path.RemoveFromStart(TEXT("\""));
		Path.RemoveFromEnd(TEXT("\""));
	}
	return Path;
}

#else // !WITH_EDITOR

FMatBPImporter::FImportResult FMatBPImporter::ImportFromString(const FString& DSLSource, const FString& PackagePath)
{
	FImportResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material import is only available in editor builds"));
	return Res;
}

FMatBPImporter::FImportResult FMatBPImporter::ImportFromAST(TSharedPtr<FMaterialGraphAST> AST, const FString& PackagePath)
{
	FImportResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material import is only available in editor builds"));
	return Res;
}

FMatBPImporter::FImportResult FMatBPImporter::UpdateMaterial(UMaterial* ExistingMaterial, const FString& DSLSource)
{
	FImportResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material import is only available in editor builds"));
	return Res;
}

FMatBPImporter::FImportResult FMatBPImporter::UpdateMaterialFromAST(UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> AST)
{
	FImportResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material import is only available in editor builds"));
	return Res;
}

FMatBPImporter::FUpdateResult FMatBPImporter::UpdateMaterialDetailed(UMaterial* ExistingMaterial, const FString& NewDSL)
{
	FUpdateResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material import is only available in editor builds"));
	return Res;
}

FMatBPImporter::FUpdateResult FMatBPImporter::UpdateMaterialDetailedFromAST(UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> NewAST)
{
	FUpdateResult Res;
	Res.bSuccess = false;
	Res.Messages.Add(TEXT("Material import is only available in editor builds"));
	return Res;
}

#endif // WITH_EDITOR
