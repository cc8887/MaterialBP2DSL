// MatBPImporter.cpp - DSL to Material Blueprint Importer
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBPImporter.h"
#include "MatBP2FPVersionCompat.h"
#include "MatLangParser.h"
#include "MatLangLinter.h"
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
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionDesaturation.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
#include "MaterialDomain.h"
#endif
#include "Factories/MaterialFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "MaterialEditingLibrary.h"

#include "MatBP2FPModule.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/Application/SlateApplication.h"

// ========== Import lifecycle helpers (AutoLayout hook bridge) ==========

namespace
{
	const FName MatBP_AutoLayoutBehaviorName(TEXT("AutoLayout"));

	/**
	 * Ensure the material owns a UMaterialGraph and that its graph nodes are linked
	 * back to the underlying UMaterialExpressions. MatBP2FP authors expressions
	 * directly (UMaterialExpression), but the AutoLayout engine operates on
	 * UEdGraphNode inside a UMaterialGraph, so we bridge here.
	 */
	static UMaterialGraph* MatBP_EnsureMaterialGraph(UMaterial* Material)
	{
		if (!Material)
		{
			return nullptr;
		}

		if (!Material->MaterialGraph)
		{
			Material->MaterialGraph = CastChecked<UMaterialGraph>(FBlueprintEditorUtils::CreateNewGraph(
				Material, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
			Material->MaterialGraph->Material = Material;
		}

		Material->MaterialGraph->RebuildGraph();
		return Material->MaterialGraph;
	}

	/** Build expression -> graph node lookup for the current MaterialGraph. */
	static TMap<UMaterialExpression*, UEdGraphNode*> MatBP_BuildExpressionNodeMap(UMaterialGraph* Graph)
	{
		TMap<UMaterialExpression*, UEdGraphNode*> Map;
		if (!Graph)
		{
			return Map;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node))
			{
				if (MatNode->MaterialExpression)
				{
					Map.Add(MatNode->MaterialExpression, MatNode);
				}
			}
		}
		return Map;
	}

	static MatBP2FPImportLifecycle::FImportLifecycleContext MatBP_MakeLifecycleContext(
		UMaterial* Material,
		UEdGraph* Graph,
		bool bIsFullRebuild,
		bool bIsIncremental)
	{
		MatBP2FPImportLifecycle::FImportLifecycleContext Context;
		Context.ImportSessionId = FGuid::NewGuid();
		Context.TargetAsset = Material;
		Context.TargetGraph = Graph;
		Context.ScopeName = Graph ? FName(*Graph->GetName()) : NAME_None;
		Context.bIsFullRebuild = bIsFullRebuild;
		Context.bIsIncremental = bIsIncremental;
		Context.bIsHeadless = IsRunningCommandlet() || !FSlateApplication::IsInitialized();
		Context.bWillCompile = true; // material import always finalizes via PreEditChange/PostEditChange
		Context.RequestedBehaviors.Add(MatBP_AutoLayoutBehaviorName);
		return Context;
	}

	static void MatBP_BroadcastNodePhase(
		MatBP2FPImportLifecycle::EImportLifecyclePhase Phase,
		const MatBP2FPImportLifecycle::FImportLifecycleContext& Context,
		const TArray<MatBP2FPImportLifecycle::FImportNodeChange>& Changes)
	{
		if (!FMatBP2FPModule::IsAvailable())
		{
			return;
		}

		MatBP2FPImportLifecycle::FImportNodePhaseEvent Event;
		Event.Phase = Phase;
		Event.Context = Context;
		Event.Changes = Changes;
		FMatBP2FPModule::Get().BroadcastNodePhase(Event);
	}

	static void MatBP_BroadcastPropertyPhase(
		MatBP2FPImportLifecycle::EImportLifecyclePhase Phase,
		const MatBP2FPImportLifecycle::FImportLifecycleContext& Context,
		const TArray<MatBP2FPImportLifecycle::FImportPropertyChange>& Changes)
	{
		if (!FMatBP2FPModule::IsAvailable())
		{
			return;
		}

		MatBP2FPImportLifecycle::FImportPropertyPhaseEvent Event;
		Event.Phase = Phase;
		Event.Context = Context;
		Event.Changes = Changes;
		FMatBP2FPModule::Get().BroadcastPropertyPhase(Event);
	}

	static void MatBP_BroadcastFinalizePhase(
		MatBP2FPImportLifecycle::EImportLifecyclePhase Phase,
		const MatBP2FPImportLifecycle::FImportLifecycleContext& Context)
	{
		if (!FMatBP2FPModule::IsAvailable())
		{
			return;
		}

		MatBP2FPImportLifecycle::FImportFinalizePhaseEvent Event;
		Event.Phase = Phase;
		Event.Context = Context;
		FMatBP2FPModule::Get().BroadcastFinalizePhase(Event);
	}

	/**
	 * Map a set of MatLang expression $ids to their corresponding
	 * UMaterialExpression* on a live material. Mirrors the export-by-index
	 * matching used by FMatLangPatcher::BuildExpressionMap so the ids line up
	 * with the diff's ExprId values.
	 */
	static TMap<FString, UMaterialExpression*> MatBP_BuildIdToExpressionMap(UMaterial* Material)
	{
		TMap<FString, UMaterialExpression*> IdToExpr;
		if (!Material)
		{
			return IdToExpr;
		}

		TSharedPtr<FMaterialGraphAST> CurrentAST = FMatBPExporter::ExportToAST(Material);
		if (!CurrentAST)
		{
			return IdToExpr;
		}

		TArray<UMaterialExpression*> AllExprs;
		for (UMaterialExpression* Expr : MatBP2FPCompat::GetMaterialExpressions(Material))
		{
			AllExprs.Add(Expr);
		}

		for (int32 i = 0; i < CurrentAST->Expressions.Num() && i < AllExprs.Num(); ++i)
		{
			IdToExpr.Add(CurrentAST->Expressions[i]->Id, AllExprs[i]);
		}
		return IdToExpr;
	}

	/**
	 * After an incremental patch, collect the UMaterialGraphNodes affected by
	 * the diff and broadcast PostNodeChanges so the AutoLayout hook can run a
	 * selection-only layout (changed nodes re-arranged around their neighbours,
	 * untouched nodes pinned in place).
	 */
	static void MatBP_BroadcastIncrementalNodeChanges(
		UMaterial* Material,
		const FMatLangDiffResult& DiffResult)
	{
		if (!Material || !FMatBP2FPModule::IsAvailable())
		{
			return;
		}

		// Ensure the material owns a graph linked to the underlying expressions.
		UMaterialGraph* MaterialGraph = MatBP_EnsureMaterialGraph(Material);
		if (!MaterialGraph)
		{
			return;
		}

		// Build $id -> UMaterialExpression and UMaterialExpression -> graph node maps.
		const TMap<FString, UMaterialExpression*> IdToExpr = MatBP_BuildIdToExpressionMap(Material);
		const TMap<UMaterialExpression*, UEdGraphNode*> ExprNodeMap =
			MatBP_BuildExpressionNodeMap(MaterialGraph);

		// Collect the distinct affected expression ids from the diff
		// (skip material-level / output diffs which carry no ExprId).
		TSet<FString> AffectedIds;
		for (const FMatLangDiffEntry& Entry : DiffResult.Entries)
		{
			if (!Entry.ExprId.IsEmpty())
			{
				AffectedIds.Add(Entry.ExprId);
			}
		}

		// Translate affected ids to graph nodes.
		TArray<MatBP2FPImportLifecycle::FImportNodeChange> NodeChanges;
		TSet<UEdGraphNode*> SeenNodes;
		for (const FString& Id : AffectedIds)
		{
			if (UMaterialExpression* const* ExprPtr = IdToExpr.Find(Id))
			{
				if (UEdGraphNode* const* NodePtr = ExprNodeMap.Find(*ExprPtr))
				{
					if (*NodePtr && !SeenNodes.Contains(*NodePtr))
					{
						SeenNodes.Add(*NodePtr);
						MatBP2FPImportLifecycle::FImportNodeChange Change;
						Change.Node = *NodePtr;
						Change.ChangeType = MatBP2FPImportLifecycle::EImportNodeChangeType::Modified;
						NodeChanges.Add(Change);
					}
				}
			}
		}

		// If nothing resolved to a node, don't broadcast (avoids a full-graph
		// relayout from an empty change set).
		if (NodeChanges.Num() == 0)
		{
			return;
		}

		MatBP2FPImportLifecycle::FImportLifecycleContext Context =
			MatBP_MakeLifecycleContext(Material, MaterialGraph, /*bIsFullRebuild*/ false, /*bIsIncremental*/ true);

		MatBP_BroadcastNodePhase(
			MatBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges, Context, NodeChanges);

		// Push laid-out graph-node positions back onto the underlying expressions,
		// since the persisted material stores positions on UMaterialExpression.
		MaterialGraph->LinkMaterialExpressionsFromGraph();
	}

	static void MatBP_AppendLintMessages(
		const FMatLangLintResult& LintResult,
		TArray<FString>& OutMessages)
	{
		for (const FMatLangDiagnostic& Diagnostic : LintResult.Diagnostics)
		{
			OutMessages.Add(Diagnostic.ToString());
		}
	}
}

// ========== Public API ==========

FMatBPImporter::FImportResult FMatBPImporter::ImportFromString(const FString& DSLSource, const FString& PackagePath)
{
	FMatLangLintResult LintResult = FMatLangLinter::Lint(DSLSource);
	
	if (LintResult.HasErrors() || !LintResult.AST.IsValid())
	{
		FImportResult Res;
		Res.Warnings = LintResult.Diagnostics.Num();
		MatBP_AppendLintMessages(LintResult, Res.Messages);
		return Res;
	}
	
	return ImportFromAST(LintResult.AST, PackagePath);
}

FMatBPImporter::FImportResult FMatBPImporter::ImportFromAST(TSharedPtr<FMaterialGraphAST> AST, const FString& PackagePath)
{
	const FMatLangLintResult LintResult = FMatLangLinter::LintAST(AST);
	if (LintResult.HasErrors())
	{
		FImportResult Res;
		Res.Warnings = LintResult.Diagnostics.Num();
		MatBP_AppendLintMessages(LintResult, Res.Messages);
		return Res;
	}

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
	FMatLangLintResult LintResult = FMatLangLinter::Lint(DSLSource);
	
	if (LintResult.HasErrors() || !LintResult.AST.IsValid())
	{
		FImportResult Res;
		Res.Material = ExistingMaterial;
		Res.Warnings = LintResult.Diagnostics.Num();
		MatBP_AppendLintMessages(LintResult, Res.Messages);
		return Res;
	}
	
	return UpdateMaterialFromAST(ExistingMaterial, LintResult.AST);
}

FMatBPImporter::FImportResult FMatBPImporter::UpdateMaterialFromAST(UMaterial* ExistingMaterial, TSharedPtr<FMaterialGraphAST> AST)
{
	const FMatLangLintResult LintResult = FMatLangLinter::LintAST(AST);
	if (LintResult.HasErrors())
	{
		FImportResult Res;
		Res.Material = ExistingMaterial;
		Res.Warnings = LintResult.Diagnostics.Num();
		MatBP_AppendLintMessages(LintResult, Res.Messages);
		return Res;
	}

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
	FMatLangLintResult LintResult = FMatLangLinter::Lint(NewDSL);
	if (LintResult.HasErrors() || !LintResult.AST.IsValid())
	{
		MatBP_AppendLintMessages(LintResult, Result.Messages);
		return Result;
	}

	return UpdateMaterialDetailedFromAST(ExistingMaterial, LintResult.AST);
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

	const FMatLangLintResult LintResult = FMatLangLinter::LintAST(NewAST);
	if (LintResult.HasErrors())
	{
		MatBP_AppendLintMessages(LintResult, Result.Messages);
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
		Result.bSuccess = NormalizeAndValidateMaterialGraph(ExistingMaterial, Result.Messages);
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
			TArray<FString> FunctionCallMessages;
			if (!NormalizeAndValidateMaterialGraph(ExistingMaterial, FunctionCallMessages))
			{
				Result.bSuccess = false;
				Result.bUsedIncrementalPatch = true;
				Result.NumApplied = PatchResult.NumApplied;
				Result.NumFailed = PatchResult.NumFailed + FunctionCallMessages.Num();
				Result.Messages.Append(PatchResult.Messages);
				Result.Messages.Append(FunctionCallMessages);
				return Result;
			}

			UE_LOG(LogMatBPImporter, Log, TEXT("UpdateMaterialDetailed: Incremental patch succeeded (%d applied, %d skipped)"),
				PatchResult.NumApplied, PatchResult.NumSkipped);

			Result.bSuccess = true;
			Result.bUsedIncrementalPatch = true;
			Result.NumApplied = PatchResult.NumApplied;
			Result.NumFailed = PatchResult.NumFailed;
			Result.Messages.Append(PatchResult.Messages);

			// Run a selection-only AutoLayout on the patched nodes: changed
			// expressions are re-arranged relative to their neighbours while
			// the rest of the graph stays pinned.
			MatBP_BroadcastIncrementalNodeChanges(ExistingMaterial, DiffResult);

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
	const bool bIsFullRebuild = true;
	MatBP2FPImportLifecycle::FImportLifecycleContext LifecycleContext =
		MatBP_MakeLifecycleContext(Material, nullptr, bIsFullRebuild, false);
	TArray<MatBP2FPImportLifecycle::FImportPropertyChange> PropertyChanges;

	// Before authoring expressions
	MatBP_BroadcastNodePhase(MatBP2FPImportLifecycle::EImportLifecyclePhase::PreNodeChanges, LifecycleContext, {});
	MatBP_BroadcastPropertyPhase(MatBP2FPImportLifecycle::EImportLifecyclePhase::PrePropertyChanges, LifecycleContext, PropertyChanges);

	// Step 1: Set material properties
	SetMaterialProperties();
	
	// Step 2: Create all expression nodes
	CreateExpressions();
	
	// Step 3: Wire all connections
	WireConnections();
	
	// Step 4: Wire material outputs
	WireMaterialOutputs();

	// Resolve non-pin expression references before constructing the editor graph.
	// In particular, Named Reroute usages must point at declarations from this
	// expression collection rather than objects left behind by a previous rebuild.
	if (!NormalizeAndValidateMaterialGraph(Material, Result.Messages))
	{
		Result.Warnings++;
		return Result;
	}

	// Step 5: Bridge to UMaterialGraph so the AutoLayout engine (which operates on
	// UEdGraphNode) can arrange the freshly imported expressions.
	UMaterialGraph* MaterialGraph = MatBP_EnsureMaterialGraph(Material);
	LifecycleContext.TargetGraph = MaterialGraph;
	LifecycleContext.ScopeName = MaterialGraph ? FName(*MaterialGraph->GetName()) : NAME_None;

	TArray<MatBP2FPImportLifecycle::FImportNodeChange> NodeChanges;
	if (MaterialGraph)
	{
		const TMap<UMaterialExpression*, UEdGraphNode*> ExprNodeMap =
			MatBP_BuildExpressionNodeMap(MaterialGraph);
		for (const TPair<FString, UMaterialExpression*>& Pair : IdToExpr)
		{
			if (UEdGraphNode* const* NodePtr = ExprNodeMap.Find(Pair.Value))
			{
				MatBP2FPImportLifecycle::FImportNodeChange Change;
				Change.Node = *NodePtr;
				Change.ChangeType = MatBP2FPImportLifecycle::EImportNodeChangeType::Added;
				NodeChanges.Add(Change);
			}
		}
	}

	// PostNodeChanges drives the AutoLayout hook, which repositions UMaterialGraphNodes.
	MatBP_BroadcastNodePhase(MatBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges, LifecycleContext, NodeChanges);

	// Push the laid-out graph-node positions back onto the underlying expressions,
	// since the persisted material stores positions on UMaterialExpression.
	if (MaterialGraph)
	{
		MaterialGraph->LinkMaterialExpressionsFromGraph();
	}

	if (!NormalizeAndValidateMaterialGraph(Material, Result.Messages))
	{
		Result.Warnings++;
		return Result;
	}

	MatBP_BroadcastPropertyPhase(MatBP2FPImportLifecycle::EImportLifecyclePhase::PostPropertyChanges, LifecycleContext, PropertyChanges);

	// Step 6: Update material
	MatBP_BroadcastFinalizePhase(MatBP2FPImportLifecycle::EImportLifecyclePhase::PreFinalize, LifecycleContext);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	if (!NormalizeAndValidateMaterialGraph(Material, Result.Messages))
	{
		Result.Warnings++;
		return Result;
	}
	Material->MarkPackageDirty();
	MatBP_BroadcastFinalizePhase(MatBP2FPImportLifecycle::EImportLifecyclePhase::PostFinalize, LifecycleContext);
	
	Result.bSuccess = true;
	Info(FString::Printf(TEXT("Import complete: %d expressions, %d connections, %d warnings"),
		Result.ExpressionsCreated, Result.ConnectionsMade, Result.Warnings));
	
	return Result;
}

bool FMatBPImporter::NormalizeAndValidateMaterialGraph(
	UMaterial* InMaterial, TArray<FString>& OutMessages)
{
	if (!InMaterial)
	{
		OutMessages.Add(TEXT("Material function validation failed: null material"));
		return false;
	}

	bool bValid = true;
	bool bRepaired = false;
	const TArray<UMaterialExpression*> MaterialExpressions =
		MatBP2FPCompat::GetMaterialExpressions(InMaterial);
	TSet<UMaterialExpression*> RegisteredExpressions;
	TMap<FGuid, UMaterialExpressionNamedRerouteDeclaration*> NamedDeclarations;

	for (UMaterialExpression* Expression : MaterialExpressions)
	{
		if (!Expression)
		{
			OutMessages.Add(TEXT("Material graph validation failed: expression collection contains null"));
			bValid = false;
			continue;
		}
		if (Expression->GetOuter() != InMaterial)
		{
			OutMessages.Add(FString::Printf(
				TEXT("Material graph validation failed: expression '%s' is not owned by '%s'"),
				*Expression->GetPathName(), *InMaterial->GetPathName()));
			bValid = false;
			continue;
		}

		RegisteredExpressions.Add(Expression);
		Expression->Material = InMaterial;
		Expression->Function = nullptr;
		Expression->SetFlags(RF_Transactional);

		if (UMaterialExpressionNamedRerouteDeclaration* Declaration =
			Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression))
		{
			if (!Declaration->VariableGuid.IsValid())
			{
				OutMessages.Add(FString::Printf(
					TEXT("Material graph validation failed: named reroute declaration '%s' has no GUID"),
					*Declaration->GetPathName()));
				bValid = false;
			}
			else if (NamedDeclarations.Contains(Declaration->VariableGuid))
			{
				OutMessages.Add(FString::Printf(
					TEXT("Material graph validation failed: duplicate named reroute GUID %s"),
					*Declaration->VariableGuid.ToString(EGuidFormats::Digits)));
				bValid = false;
			}
			else
			{
				NamedDeclarations.Add(Declaration->VariableGuid, Declaration);
			}
		}
	}

	// Named reroutes are expression references that are not exposed through GetInput().
	// Rebind them by GUID so legacy DSL UObject paths cannot keep orphan graphs alive.
	for (UMaterialExpression* Expression : MaterialExpressions)
	{
		UMaterialExpressionNamedRerouteUsage* Usage =
			Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
		if (!Usage)
		{
			continue;
		}

		UMaterialExpressionNamedRerouteDeclaration* Resolved =
			NamedDeclarations.FindRef(Usage->DeclarationGuid);
		if (!Resolved && Usage->Declaration
			&& RegisteredExpressions.Contains(Usage->Declaration)
			&& (!Usage->DeclarationGuid.IsValid()
				|| Usage->DeclarationGuid == Usage->Declaration->VariableGuid))
		{
			Resolved = Usage->Declaration;
		}

		if (!Resolved)
		{
			OutMessages.Add(FString::Printf(
				TEXT("Material graph validation failed: named reroute usage '%s' cannot resolve GUID %s"),
				*Usage->GetPathName(), *Usage->DeclarationGuid.ToString(EGuidFormats::Digits)));
			Usage->Declaration = nullptr;
			bValid = false;
			continue;
		}

		if (Usage->Declaration != Resolved)
		{
			Usage->Declaration = Resolved;
			bRepaired = true;
		}
		if (Usage->DeclarationGuid != Resolved->VariableGuid)
		{
			Usage->DeclarationGuid = Resolved->VariableGuid;
			bRepaired = true;
		}
	}

	// Every ordinary expression connection compiled by the material must resolve to
	// an expression registered in the same collection. There is no lossless way to
	// infer a replacement for an arbitrary orphan, so reject it before compilation.
	for (UMaterialExpression* Expression : MaterialExpressions)
	{
		if (!Expression)
		{
			continue;
		}
		const int32 InputCount = MatBP2FPCompat::CountExpressionInputs(Expression);
		for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
		{
			FExpressionInput* Input = Expression->GetInput(InputIndex);
			if (Input && Input->Expression && !RegisteredExpressions.Contains(Input->Expression))
			{
				OutMessages.Add(FString::Printf(
					TEXT("Material graph validation failed: '%s' input %d references unregistered expression '%s'"),
					*Expression->GetPathName(), InputIndex, *Input->Expression->GetPathName()));
				bValid = false;
			}
		}
	}
	for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		FExpressionInput* Input = InMaterial->GetExpressionInputForProperty(
			static_cast<EMaterialProperty>(PropertyIndex));
		if (Input && Input->Expression && !RegisteredExpressions.Contains(Input->Expression))
		{
			OutMessages.Add(FString::Printf(
				TEXT("Material graph validation failed: material property %d references unregistered expression '%s'"),
				PropertyIndex, *Input->Expression->GetPathName()));
			bValid = false;
		}
	}

	if (!bValid)
	{
		return false;
	}

	TSet<UMaterialFunctionInterface*> VisitedFunctions;
	TFunction<void(const TArray<UMaterialExpression*>&)> RefreshAndValidateExpressions;
	RefreshAndValidateExpressions = [&OutMessages, &bValid, &VisitedFunctions, &RefreshAndValidateExpressions](
		const TArray<UMaterialExpression*>& Expressions)
	{
		for (UMaterialExpression* Expression : Expressions)
		{
			UMaterialExpressionMaterialFunctionCall* FunctionCall =
				Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
			if (!FunctionCall)
			{
				continue;
			}

			FunctionCall->UpdateFromFunctionResource(false);
			const FString FunctionPath = FunctionCall->MaterialFunction
				? FunctionCall->MaterialFunction->GetPathName()
				: TEXT("<missing>");

			if (!FunctionCall->MaterialFunction)
			{
				OutMessages.Add(FString::Printf(
					TEXT("Material function validation failed: node '%s' has no function"),
					*FunctionCall->GetPathName()));
				bValid = false;
				continue;
			}

			for (int32 InputIndex = 0; InputIndex < FunctionCall->FunctionInputs.Num(); ++InputIndex)
			{
				const FFunctionExpressionInput& Input = FunctionCall->FunctionInputs[InputIndex];
				if (!Input.ExpressionInput || !Input.ExpressionInputId.IsValid())
				{
					OutMessages.Add(FString::Printf(
						TEXT("Material function validation failed: node '%s', function '%s', input %d is unresolved"),
						*FunctionCall->GetPathName(), *FunctionPath, InputIndex));
					bValid = false;
				}
			}

			for (int32 OutputIndex = 0; OutputIndex < FunctionCall->FunctionOutputs.Num(); ++OutputIndex)
			{
				const FFunctionExpressionOutput& Output = FunctionCall->FunctionOutputs[OutputIndex];
				if (!Output.ExpressionOutput || !Output.ExpressionOutputId.IsValid())
				{
					OutMessages.Add(FString::Printf(
						TEXT("Material function validation failed: node '%s', function '%s', output %d is unresolved"),
						*FunctionCall->GetPathName(), *FunctionPath, OutputIndex));
					bValid = false;
				}
			}

			if (!VisitedFunctions.Contains(FunctionCall->MaterialFunction))
			{
				VisitedFunctions.Add(FunctionCall->MaterialFunction);
				RefreshAndValidateExpressions(
					MatBP2FPCompat::GetFunctionExpressions(FunctionCall->MaterialFunction));
			}
		}
	};

	RefreshAndValidateExpressions(MaterialExpressions);
	if (bRepaired)
	{
		InMaterial->MarkPackageDirty();
	}

	return bValid;
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
	
	UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(
		Material, ExprClass, (int32)ExprAST->EditorPosition.X, (int32)ExprAST->EditorPosition.Y);
	if (!Expr)
	{
		Warn(FString::Printf(TEXT("Failed to create expression '%s'"), *ExprAST->Id));
		return nullptr;
	}
	Expr->Material = Material;
	Expr->Function = nullptr;
	
	// Set editor position
	Expr->MaterialExpressionEditorX = (int32)ExprAST->EditorPosition.X;
	Expr->MaterialExpressionEditorY = (int32)ExprAST->EditorPosition.Y;
	
	// Set description
	if (!ExprAST->Comment.IsEmpty())
	{
		Expr->Desc = ExprAST->Comment;
	}
	
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
	const int32 NumInputs = MatBP2FPCompat::CountExpressionInputs(Expr);
	
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
	struct FOutputMapping
	{
		FString DSLName;
		FExpressionInput* UEInput;
	};
	
	TArray<FOutputMapping> Mappings;
	static const TCHAR* SlotNames[] = {
		TEXT("base-color"), TEXT("metallic"), TEXT("specular"), TEXT("roughness"),
		TEXT("anisotropy"), TEXT("emissive-color"), TEXT("opacity"), TEXT("opacity-mask"),
		TEXT("normal"), TEXT("tangent"), TEXT("world-position-offset"), TEXT("subsurface-color"),
		TEXT("ambient-occlusion"), TEXT("refraction"), TEXT("pixel-depth-offset")
	};
	for (const TCHAR* SlotName : SlotNames)
	{
		Mappings.Add({SlotName, MatBP2FPCompat::GetMaterialInput(Material, SlotName)});
	}
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
		TEXT("declaration"),          // Named reroutes are rebound by DeclarationGuid
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
		MatBP2FPCompat::ImportPropertyText(Prop, Value, ValuePtr, Expr);
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
#if ENGINE_MAJOR_VERSION >= 5
		case EMatLangShadingModel::Strata:             return MSM_Strata;
#endif
		default: return MSM_DefaultLit;
	}
}

void FMatBPImporter::ClearMaterial()
{
	// Older MatBP2FP rebuilds emptied the expression array without destroying its
	// objects. Gather those direct children before deleting the registered graph so
	// stale UObject paths cannot keep historical expression chains in the package.
	const TArray<UMaterialExpression*> RegisteredExpressionSnapshot =
		MatBP2FPCompat::GetMaterialExpressions(Material);
	TSet<UMaterialExpression*> RegisteredExpressions;
	for (UMaterialExpression* Expression : RegisteredExpressionSnapshot)
	{
		RegisteredExpressions.Add(Expression);
	}
	TArray<UObject*> DirectChildren;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	GetObjectsWithOuter(Material, DirectChildren, EGetObjectsFlags::None);
#else
	GetObjectsWithOuter(Material, DirectChildren, false);
#endif
	TArray<UMaterialExpression*> OrphanExpressions;
	for (UObject* Child : DirectChildren)
	{
		UMaterialExpression* Expression = Cast<UMaterialExpression>(Child);
		if (IsValid(Expression)
			&& !RegisteredExpressions.Contains(Expression)
			&& !Expression->IsA<UMaterialExpressionComment>())
		{
			OrphanExpressions.Add(Expression);
		}
	}

	// Use the engine's deletion path so every material property is disconnected,
	// parameter caches are updated, and removed expressions cannot be resolved by
	// an object path during the replacement import.
	// DeleteAllMaterialExpressions mutates the same expression view it iterates in
	// newer engine versions. Iterate our snapshot so no registered node is skipped.
	for (UMaterialExpression* Expression : RegisteredExpressionSnapshot)
	{
		if (IsValid(Expression))
		{
			UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
		}
	}
	for (UMaterialExpression* OrphanExpression : OrphanExpressions)
	{
		UMaterialEditingLibrary::DeleteMaterialExpression(Material, OrphanExpression);
	}
	if (OrphanExpressions.Num() > 0)
	{
		UE_LOG(LogMatBPImporter, Log, TEXT("Removed %d orphan material expressions before rebuild"),
			OrphanExpressions.Num());
	}
	MatBP2FPCompat::ClearMaterialInputs(Material);
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
