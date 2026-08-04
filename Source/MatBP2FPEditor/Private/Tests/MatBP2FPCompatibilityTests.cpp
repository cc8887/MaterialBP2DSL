// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.
// Cross-version, asset-free import/export fixtures for UE4.27 through UE5.8.

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
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
}

MBP_COMPAT_TEST(Fixtures_ParseAndCanonicalize)
bool FMatBPCompatFixtures_ParseAndCanonicalize::RunTest(const FString& Parameters)
{
	static const TCHAR* FixtureNames[] = {
		TEXT("baseline_pbr.matlang"),
		TEXT("dag_shared_inputs.matlang"),
		TEXT("incremental_before.matlang"),
		TEXT("incremental_after.matlang"),
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

#endif // WITH_DEV_AUTOMATION_TESTS
