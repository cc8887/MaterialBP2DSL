// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.
// Cross-version, asset-free import/export fixtures for UE4.27 through UE5.8.

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "MaterialEditor/PreviewMaterial.h"
#include "MatBP2FPVersionCompat.h"
#include "MatBPExporter.h"
#include "MatBPImporter.h"
#include "MatLangParser.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_DEV_AUTOMATION_TESTS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_COMPAT_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_COMPAT_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

#define MBP_COMPAT_TEST(Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPCompat##Name, "MatBP2FP.Compatibility." #Name, MBP_COMPAT_TEST_FLAGS)

namespace MatBP2FPCompatibilityTests
{
	bool LoadFixture(const TCHAR* FileName, FString& OutSource, FString& OutError)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MatBP2FP"));
		if (!Plugin.IsValid())
		{
			OutError = TEXT("MatBP2FP plugin was not found");
			return false;
		}

		const FString FixturePath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FileName);
		if (!FFileHelper::LoadFileToString(OutSource, *FixturePath))
		{
			OutError = FString::Printf(TEXT("Failed to load fixture: %s"), *FixturePath);
			return false;
		}
		return true;
	}

	UMaterial* CreateTransientMaterial()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->AddToRoot();
		return Material;
	}

	void ReleaseTransientMaterial(UMaterial* Material)
	{
		if (Material)
		{
			Material->RemoveFromRoot();
		}
	}

	UMaterialExpressionMaterialFunctionCall* FindFunctionCall(UMaterial* Material)
	{
		if (!Material)
		{
			return nullptr;
		}
		for (UMaterialExpression* Expression : MatBP2FPCompat::GetMaterialExpressions(Material))
		{
			if (UMaterialExpressionMaterialFunctionCall* FunctionCall =
				Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				return FunctionCall;
			}
		}
		return nullptr;
	}

	template<typename ExpressionType>
	ExpressionType* FindExpression(UMaterial* Material)
	{
		if (!Material)
		{
			return nullptr;
		}
		for (UMaterialExpression* Expression : MatBP2FPCompat::GetMaterialExpressions(Material))
		{
			if (ExpressionType* Match = Cast<ExpressionType>(Expression))
			{
				return Match;
			}
		}
		return nullptr;
	}

	void ClearTransientFunctionPins(UMaterialExpressionMaterialFunctionCall* FunctionCall)
	{
		if (!FunctionCall)
		{
			return;
		}

		for (FFunctionExpressionInput& Input : FunctionCall->FunctionInputs)
		{
			Input.ExpressionInput = nullptr;
		}
		for (FFunctionExpressionOutput& Output : FunctionCall->FunctionOutputs)
		{
			Output.ExpressionOutput = nullptr;
		}
	}

	bool HasResolvedFunctionPins(UMaterialExpressionMaterialFunctionCall* FunctionCall)
	{
		if (!FunctionCall || FunctionCall->FunctionOutputs.Num() == 0)
		{
			return false;
		}
		for (const FFunctionExpressionInput& Input : FunctionCall->FunctionInputs)
		{
			if (!Input.ExpressionInput || !Input.ExpressionInputId.IsValid())
			{
				return false;
			}
		}
		for (const FFunctionExpressionOutput& Output : FunctionCall->FunctionOutputs)
		{
			if (!Output.ExpressionOutput || !Output.ExpressionOutputId.IsValid())
			{
				return false;
			}
		}
		return true;
	}
}

MBP_COMPAT_TEST(Fixtures_ParseAndCanonicalize)
bool FMatBPCompatFixtures_ParseAndCanonicalize::RunTest(const FString& Parameters)
{
	static const TCHAR* FixtureNames[] = {
		TEXT("baseline_pbr.matlang"),
		TEXT("dag_shared_inputs.matlang"),
		TEXT("incremental_before.matlang"),
		TEXT("incremental_after.matlang"),
		TEXT("material_function_call.matlang"),
		TEXT("strata_capability.matlang")
	};

	for (const TCHAR* FixtureName : FixtureNames)
	{
		FString Source;
		FString LoadError;
		if (!TestTrue(FString::Printf(TEXT("Load %s"), FixtureName),
			MatBP2FPCompatibilityTests::LoadFixture(FixtureName, Source, LoadError)))
		{
			AddError(LoadError);
			continue;
		}

		TArray<FMatLangParseError> Errors;
		const TSharedPtr<FMaterialGraphAST> AST = FMatLangParser::Parse(Source, Errors);
		TestTrue(FString::Printf(TEXT("Parse %s"), FixtureName), AST.IsValid());
		TestEqual(FString::Printf(TEXT("No parse errors in %s"), FixtureName), Errors.Num(), 0);
		if (!AST.IsValid())
		{
			continue;
		}

		TArray<FMatLangParseError> CanonicalErrors;
		const TSharedPtr<FMaterialGraphAST> CanonicalAST = FMatLangParser::Parse(AST->ToString(), CanonicalErrors);
		TestTrue(FString::Printf(TEXT("Reparse canonical %s"), FixtureName), CanonicalAST.IsValid());
		TestEqual(FString::Printf(TEXT("No canonical parse errors in %s"), FixtureName), CanonicalErrors.Num(), 0);
	}
	return true;
}

MBP_COMPAT_TEST(MaterialFunctionCall_RefreshAcrossImportAndIncremental)
bool FMatBPCompatMaterialFunctionCall_RefreshAcrossImportAndIncremental::RunTest(const FString& Parameters)
{
	FString Source;
	FString LoadError;
	if (!MatBP2FPCompatibilityTests::LoadFixture(TEXT("material_function_call.matlang"), Source, LoadError))
	{
		AddError(LoadError);
		return false;
	}

	UMaterial* Material = MatBP2FPCompatibilityTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, Source);
	TestTrue(TEXT("Function-call import succeeds"), ImportResult.bSuccess);
	UMaterialExpressionMaterialFunctionCall* FunctionCall =
		MatBP2FPCompatibilityTests::FindFunctionCall(Material);
	UMaterialExpressionNamedRerouteDeclaration* Declaration =
		MatBP2FPCompatibilityTests::FindExpression<UMaterialExpressionNamedRerouteDeclaration>(Material);
	UMaterialExpressionNamedRerouteUsage* Usage =
		MatBP2FPCompatibilityTests::FindExpression<UMaterialExpressionNamedRerouteUsage>(Material);
	TestNotNull(TEXT("Imported function call exists"), FunctionCall);
	TestNotNull(TEXT("Imported named reroute declaration exists"), Declaration);
	TestNotNull(TEXT("Imported named reroute usage exists"), Usage);
	TestTrue(TEXT("Named reroute usage binds to the registered declaration"),
		Usage && Usage->Declaration == Declaration);
	TestTrue(TEXT("Import keeps the native function-call node type"),
		FunctionCall && FunctionCall->GetClass() == UMaterialExpressionMaterialFunctionCall::StaticClass());
	TestTrue(TEXT("Full import resolves transient function pins"),
		MatBP2FPCompatibilityTests::HasResolvedFunctionPins(FunctionCall));

	MatBP2FPCompatibilityTests::ClearTransientFunctionPins(FunctionCall);
	const FMatBPImporter::FUpdateResult NoDiffResult =
		FMatBPImporter::UpdateMaterialDetailed(Material, Source);
	TestTrue(TEXT("No-diff update repairs transient function pins"), NoDiffResult.bSuccess);
	TestTrue(TEXT("No-diff repair resolves every function pin"),
		MatBP2FPCompatibilityTests::HasResolvedFunctionPins(FunctionCall));

	MatBP2FPCompatibilityTests::ClearTransientFunctionPins(FunctionCall);
	const FString IncrementalSource = Source.Replace(TEXT(":two-sided false"), TEXT(":two-sided true"));
	const FMatBPImporter::FUpdateResult IncrementalResult =
		FMatBPImporter::UpdateMaterialDetailed(Material, IncrementalSource);
	TestTrue(TEXT("Incremental update repairs before recompilation"), IncrementalResult.bSuccess);
	TestTrue(TEXT("Function-call update remains incremental"), IncrementalResult.bUsedIncrementalPatch);
	TestTrue(TEXT("Incremental repair resolves every function pin"),
		MatBP2FPCompatibilityTests::HasResolvedFunctionPins(FunctionCall));

	// Simulate an expression object left behind by the pre-fix full-rebuild path.
	// It is owned by the material but intentionally absent from its expression list.
	UMaterialExpressionMaterialFunctionCall* HistoricalOrphan =
		NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
	HistoricalOrphan->Material = Material;
	HistoricalOrphan->MaterialFunction = FunctionCall ? FunctionCall->MaterialFunction : nullptr;
	HistoricalOrphan->UpdateFromFunctionResource(false);
	TestFalse(TEXT("Historical orphan is not registered in the material graph"),
		MatBP2FPCompat::GetMaterialExpressions(Material).Contains(HistoricalOrphan));

	// Legacy exports serialized the declaration UObject path. Rebuilding the same
	// material must ignore that old object and bind the usage by stable GUID.
	const FString OldDeclarationPath = Declaration ? Declaration->GetPathName() : TEXT("None");
	const FString LegacySource = Source.Replace(
		TEXT(":declaration-guid \"11111111222222223333333344444444\""),
		*FString::Printf(TEXT(":declaration \"%s\"\n      :declaration-guid \"11111111222222223333333344444444\""),
			*OldDeclarationPath));
	UMaterialExpressionNamedRerouteDeclaration* OldDeclaration = Declaration;
	const FMatBPImporter::FImportResult RebuildResult =
		FMatBPImporter::UpdateMaterial(Material, LegacySource);
	TestTrue(TEXT("Full rebuild accepts legacy named-reroute object paths"), RebuildResult.bSuccess);
	TestFalse(TEXT("Full rebuild destroys historical orphan expressions"), IsValid(HistoricalOrphan));
	Declaration = MatBP2FPCompatibilityTests::FindExpression<UMaterialExpressionNamedRerouteDeclaration>(Material);
	Usage = MatBP2FPCompatibilityTests::FindExpression<UMaterialExpressionNamedRerouteUsage>(Material);
	FunctionCall = MatBP2FPCompatibilityTests::FindFunctionCall(Material);
	TestTrue(TEXT("Full rebuild creates a new declaration"), Declaration && Declaration != OldDeclaration);
	TestTrue(TEXT("Legacy path is rebound to the new registered declaration"),
		Usage && Usage->Declaration == Declaration);
	TestTrue(TEXT("Rebound declaration belongs to the material expression collection"),
		Declaration && MatBP2FPCompat::GetMaterialExpressions(Material).Contains(Declaration));
	TestTrue(TEXT("Function behind rebound named reroute has resolved pins"),
		MatBP2FPCompatibilityTests::HasResolvedFunctionPins(FunctionCall));

	UMaterial* PreviewMaterial = Cast<UMaterial>(StaticDuplicateObject(
		Material, GetTransientPackage(), NAME_None, ~RF_Standalone, UPreviewMaterial::StaticClass()));
	TestNotNull(TEXT("Editor-style preview duplication succeeds"), PreviewMaterial);
	if (PreviewMaterial)
	{
		PreviewMaterial->AddToRoot();
		UMaterialExpressionMaterialFunctionCall* PreviewFunctionCall =
			MatBP2FPCompatibilityTests::FindFunctionCall(PreviewMaterial);
		UMaterialExpressionNamedRerouteDeclaration* PreviewDeclaration =
			MatBP2FPCompatibilityTests::FindExpression<UMaterialExpressionNamedRerouteDeclaration>(PreviewMaterial);
		UMaterialExpressionNamedRerouteUsage* PreviewUsage =
			MatBP2FPCompatibilityTests::FindExpression<UMaterialExpressionNamedRerouteUsage>(PreviewMaterial);
		TestTrue(TEXT("Editor-style duplication preserves resolved function pins"),
			MatBP2FPCompatibilityTests::HasResolvedFunctionPins(PreviewFunctionCall));
		TestTrue(TEXT("Editor-style duplication keeps named reroutes inside the preview graph"),
			PreviewUsage && PreviewUsage->Declaration == PreviewDeclaration);
		PreviewMaterial->PreEditChange(nullptr);
		PreviewMaterial->PostEditChange();
		PreviewMaterial->RemoveFromRoot();
	}

	MatBP2FPCompatibilityTests::ReleaseTransientMaterial(Material);
	return true;
}

MBP_COMPAT_TEST(Baseline_ImportExport)
bool FMatBPCompatBaseline_ImportExport::RunTest(const FString& Parameters)
{
	FString Source;
	FString LoadError;
	if (!MatBP2FPCompatibilityTests::LoadFixture(TEXT("baseline_pbr.matlang"), Source, LoadError))
	{
		AddError(LoadError);
		return false;
	}

	UMaterial* Material = MatBP2FPCompatibilityTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, Source);
	TestTrue(TEXT("Baseline import succeeds"), ImportResult.bSuccess);
	TestEqual(TEXT("Baseline expression count"), ImportResult.ExpressionsCreated, 4);
	TestEqual(TEXT("Baseline connection count"), ImportResult.ConnectionsMade, 6);
	TestEqual(TEXT("Baseline has no degradation warnings"), ImportResult.Warnings, 0);

	const TSharedPtr<FMaterialGraphAST> Exported = FMatBPExporter::ExportToAST(Material);
	TestTrue(TEXT("Baseline export succeeds"), Exported.IsValid());
	if (Exported.IsValid())
	{
		TestEqual(TEXT("Exported baseline expression count"), Exported->Expressions.Num(), 4);
		TestEqual(TEXT("Exported baseline output count"), Exported->Outputs.Slots.Num(), 4);
	}
	MatBP2FPCompatibilityTests::ReleaseTransientMaterial(Material);
	return true;
}

MBP_COMPAT_TEST(DAG_ImportExport)
bool FMatBPCompatDAG_ImportExport::RunTest(const FString& Parameters)
{
	FString Source;
	FString LoadError;
	if (!MatBP2FPCompatibilityTests::LoadFixture(TEXT("dag_shared_inputs.matlang"), Source, LoadError))
	{
		AddError(LoadError);
		return false;
	}

	UMaterial* Material = MatBP2FPCompatibilityTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, Source);
	TestTrue(TEXT("DAG import succeeds"), ImportResult.bSuccess);
	TestEqual(TEXT("DAG expression count"), ImportResult.ExpressionsCreated, 8);
	TestEqual(TEXT("DAG connection count"), ImportResult.ConnectionsMade, 11);
	TestEqual(TEXT("DAG has no degradation warnings"), ImportResult.Warnings, 0);

	const TSharedPtr<FMaterialGraphAST> Exported = FMatBPExporter::ExportToAST(Material);
	TestTrue(TEXT("DAG export succeeds"), Exported.IsValid());
	if (Exported.IsValid())
	{
		TestEqual(TEXT("Exported DAG expression count"), Exported->Expressions.Num(), 8);
		TestEqual(TEXT("Exported DAG output count"), Exported->Outputs.Slots.Num(), 3);
	}
	MatBP2FPCompatibilityTests::ReleaseTransientMaterial(Material);
	return true;
}

MBP_COMPAT_TEST(Incremental_PropertyOnly)
bool FMatBPCompatIncremental_PropertyOnly::RunTest(const FString& Parameters)
{
	FString BeforeSource;
	FString AfterSource;
	FString LoadError;
	if (!MatBP2FPCompatibilityTests::LoadFixture(TEXT("incremental_before.matlang"), BeforeSource, LoadError) ||
		!MatBP2FPCompatibilityTests::LoadFixture(TEXT("incremental_after.matlang"), AfterSource, LoadError))
	{
		AddError(LoadError);
		return false;
	}

	UMaterial* Material = MatBP2FPCompatibilityTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult InitialImport = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	TestTrue(TEXT("Initial incremental fixture import succeeds"), InitialImport.bSuccess);

	const FMatBPImporter::FUpdateResult UpdateResult = FMatBPImporter::UpdateMaterialDetailed(Material, AfterSource);
	TestTrue(TEXT("Property-only update succeeds"), UpdateResult.bSuccess);
	TestTrue(TEXT("Property-only update uses incremental patch"), UpdateResult.bUsedIncrementalPatch);
	TestEqual(TEXT("Property-only update has no structural changes"), UpdateResult.NumStructuralChanges, 0);
	TestTrue(TEXT("Property-only update detects changed properties"), UpdateResult.NumPropertyChanges > 0);
	TestEqual(TEXT("Property-only update has no patch failures"), UpdateResult.NumFailed, 0);
	MatBP2FPCompatibilityTests::ReleaseTransientMaterial(Material);
	return true;
}

MBP_COMPAT_TEST(Strata_VersionExpectation)
bool FMatBPCompatStrata_VersionExpectation::RunTest(const FString& Parameters)
{
	FString Source;
	FString LoadError;
	if (!MatBP2FPCompatibilityTests::LoadFixture(TEXT("strata_capability.matlang"), Source, LoadError))
	{
		AddError(LoadError);
		return false;
	}

	UMaterial* Material = MatBP2FPCompatibilityTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, Source);
	TestTrue(TEXT("Strata capability fixture imports"), ImportResult.bSuccess);
#if ENGINE_MAJOR_VERSION >= 5
	TestEqual(TEXT("UE5 preserves the Strata shading model"),
		Material->GetShadingModels().GetFirstShadingModel(), MSM_Strata);
#else
	TestEqual(TEXT("UE4.27 degrades Strata to Default Lit"),
		Material->GetShadingModels().GetFirstShadingModel(), MSM_DefaultLit);
#endif
	MatBP2FPCompatibilityTests::ReleaseTransientMaterial(Material);
	return true;
}

MBP_COMPAT_TEST(SpecialInputNames_CanonicalRoundTrip)
bool FMatBPCompatSpecialInputNames_CanonicalRoundTrip::RunTest(const FString& Parameters)
{
	auto SourceExpression = MakeShared<FMatExpressionAST>();
	SourceExpression->ExprType = TEXT("constant");
	SourceExpression->Id = TEXT("$const1");
	SourceExpression->Properties.Add(TEXT("value"), TEXT("1"));

	auto SpecialExpression = MakeShared<FMatExpressionAST>();
	SpecialExpression->ExprType = TEXT("custom");
	SpecialExpression->Id = TEXT("$custom2");
	SpecialExpression->Properties.Add(TEXT("A"), TEXT("\"reflected input text\""));

	const TArray<FString> SpecialNames = {
		TEXT("A"),
		TEXT("UV(0-1)"),
		TEXT("DDX(-UVs)"),
		TEXT("A!=0"),
		TEXT("Use Ramp Tex?"),
		TEXT("[-In] Transparent On"),
		TEXT("Quote\"\\Pin")
	};
	for (const FString& Name : SpecialNames)
	{
		SpecialExpression->AddInput(Name, FMatLangConnection(SourceExpression->Id, 0));
	}

	FMaterialGraphAST Graph;
	Graph.Name = TEXT("SpecialInputNames");
	Graph.Expressions.Add(SourceExpression);
	Graph.Expressions.Add(SpecialExpression);
	FMatLangInput Output;
	Output.Name = TEXT("base-color");
	Output.Connection = FMatLangConnection(SpecialExpression->Id, 0);
	Graph.Outputs.Slots.Add(Output.Name, Output);

	const FString Canonical = Graph.ToString();
	TestTrue(TEXT("Parenthesized pin name is quoted"), Canonical.Contains(TEXT(":\"UV(0-1)\"")));
	TestTrue(TEXT("Operator pin name is quoted"), Canonical.Contains(TEXT(":\"A!=0\"")));
	TestFalse(TEXT("Reflected property/input collision is omitted"), Canonical.Contains(TEXT("reflected input text")));

	TArray<FMatLangParseError> ParseErrors;
	const TSharedPtr<FMaterialGraphAST> Parsed = FMatLangParser::Parse(Canonical, ParseErrors);
	TestTrue(TEXT("Canonical DSL with special pin names parses"), Parsed.IsValid());
	TestEqual(TEXT("Canonical DSL has no parse errors"), ParseErrors.Num(), 0);
	if (!Parsed.IsValid())
	{
		return false;
	}

	const TSharedPtr<FMatExpressionAST> ParsedExpression = Parsed->FindExpression(TEXT("$custom2"));
	TestTrue(TEXT("Special-name expression is present"), ParsedExpression.IsValid());
	if (!ParsedExpression.IsValid())
	{
		return false;
	}
	for (const FString& Name : SpecialNames)
	{
		TestNotNull(*FString::Printf(TEXT("Pin name round-trips: %s"), *Name), ParsedExpression->FindInput(Name));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
