// Compile-free MatLang performance analysis tests.

#include "CoreMinimal.h"
#include "MatLangLinter.h"
#include "MatLangStaticCostAnalyzer.h"
#include "Misc/AutomationTest.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_DEV_AUTOMATION_TESTS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_PERF_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_PERF_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

#define MBP_PERF_TEST(Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPPerf##Name, "MatBP2FP.Perf." #Name, MBP_PERF_TEST_FLAGS)

namespace MatBP2FPStaticCostTests
{
	TSharedPtr<FMaterialGraphAST> ParseChecked(
		FAutomationTestBase& Test,
		const FString& Source,
		const FString& FileName)
	{
		const FMatLangLintResult Lint = FMatLangLinter::Lint(Source, FileName);
		if (Lint.HasErrors())
		{
			for (const FMatLangDiagnostic& Diagnostic : Lint.Diagnostics)
			{
				Test.AddError(Diagnostic.ToString());
			}
		}
		return Lint.AST;
	}
}

MBP_PERF_TEST(ReachabilityAndStages)
bool FMatBPPerfReachabilityAndStages::RunTest(const FString& Parameters)
{
	const FString Source = TEXT(
		"(material \"M_Cost\"\n"
		"  :asset-path \"/Game/Materials/M_Cost.M_Cost\"\n"
		"  (expressions\n"
		"    (texture-sample $tex :texture (asset \"/Game/T_Tex.T_Tex\"))\n"
		"    (constant $factor :value 0.5)\n"
		"    (multiply $mul :a (connect $tex) :b (connect $factor))\n"
		"    (constant3-vector $wpo :value (0 0 1))\n"
		"    (sine $unused :input (connect $factor)))\n"
		"  (outputs\n"
		"    :base-color (connect $mul)\n"
		"    :world-position-offset (connect $wpo)))");
	const TSharedPtr<FMaterialGraphAST> AST = MatBP2FPStaticCostTests::ParseChecked(*this, Source, TEXT("M_Cost.matlang"));
	TestTrue(TEXT("AST is valid"), AST.IsValid());
	if (!AST.IsValid()) return false;

	const FMatLangStaticCostReport Report = FMatLangStaticCostAnalyzer::Analyze(AST);
	TestEqual(TEXT("Declared nodes"), Report.DeclaredNodes, 5);
	TestEqual(TEXT("One disconnected node"), Report.DeadNodes, 1);
	TestEqual(TEXT("Pixel reachable nodes"), Report.Pixel.ReachableNodes.Default, 3);
	TestEqual(TEXT("Vertex reachable nodes"), Report.Vertex.ReachableNodes.Default, 1);
	TestEqual(TEXT("Pixel texture sites"), Report.Pixel.TextureSampleSites.Default, 1);
	TestEqual(TEXT("Vertex texture sites"), Report.Vertex.TextureSampleSites.Default, 0);
	TestEqual(TEXT("Pixel ALU proxy"), Report.Pixel.ALUProxy.Default, 1);
	TestEqual(TEXT("Default texture asset count"), Report.DefaultTextureAssets.Num(), 1);
	if (Report.DefaultTextureAssets.Num() == 1)
	{
		TestEqual(TEXT("Texture asset path"), Report.DefaultTextureAssets[0], FString(TEXT("/Game/T_Tex.T_Tex")));
	}
	return true;
}

MBP_PERF_TEST(StaticSwitchScenarios)
bool FMatBPPerfStaticSwitchScenarios::RunTest(const FString& Parameters)
{
	const FString Source = TEXT(
		"(material \"M_Switch\"\n"
		"  (expressions\n"
		"    (texture-sample $heavy :texture (asset \"/Game/T_Heavy.T_Heavy\"))\n"
		"    (constant $cheap :value 0)\n"
		"    (static-switch-parameter $switch\n"
		"      :name \"UseHeavy\"\n"
		"      :default false\n"
		"      :a (connect $heavy)\n"
		"      :b (connect $cheap)))\n"
		"  (outputs :base-color (connect $switch)))");
	const TSharedPtr<FMaterialGraphAST> AST = MatBP2FPStaticCostTests::ParseChecked(*this, Source, TEXT("M_Switch.matlang"));
	TestTrue(TEXT("AST is valid"), AST.IsValid());
	if (!AST.IsValid()) return false;

	const FMatLangStaticCostReport Report = FMatLangStaticCostAnalyzer::Analyze(AST);
	TestEqual(TEXT("Default branch has no texture sample"), Report.Pixel.TextureSampleSites.Default, 0);
	TestEqual(TEXT("Conservative branch includes texture sample"), Report.Pixel.TextureSampleSites.Conservative, 1);
	TestEqual(TEXT("One static parameter"), Report.StaticParameters.Num(), 1);
	TestEqual(TEXT("Two theoretical static combinations"), Report.TheoreticalStaticCombinations, uint64(2));
	return true;
}

MBP_PERF_TEST(MaterialFunctionExpansion)
bool FMatBPPerfMaterialFunctionExpansion::RunTest(const FString& Parameters)
{
	const FString FunctionSource = TEXT(
		"(material-function \"MF_Sample\"\n"
		"  :asset-path \"/Game/Functions/MF_Sample.MF_Sample\"\n"
		"  (expressions\n"
		"    (texture-sample $sample :texture (asset \"/Game/T_Function.T_Function\"))\n"
		"    (function-output $out :a (connect $sample)))\n"
		"  (outputs))");
	const FString MaterialSource = TEXT(
		"(material \"M_Function\"\n"
		"  (expressions\n"
		"    (material-function-call $call\n"
		"      :function (asset \"/Game/Functions/MF_Sample.MF_Sample\")))\n"
		"  (outputs :base-color (connect $call)))");

	const TSharedPtr<FMaterialGraphAST> Function = MatBP2FPStaticCostTests::ParseChecked(*this, FunctionSource, TEXT("MF_Sample.matlang"));
	const TSharedPtr<FMaterialGraphAST> Material = MatBP2FPStaticCostTests::ParseChecked(*this, MaterialSource, TEXT("M_Function.matlang"));
	TestTrue(TEXT("Function AST is valid"), Function.IsValid());
	TestTrue(TEXT("Material AST is valid"), Material.IsValid());
	if (!Function.IsValid() || !Material.IsValid()) return false;

	const FMatLangStaticCostAnalyzer::FFunctionResolver Resolver =
		[Function](const FString& AssetPath) -> TSharedPtr<FMaterialGraphAST>
		{
			return AssetPath == Function->AssetPath ? Function : nullptr;
		};
	const FMatLangStaticCostReport Report = FMatLangStaticCostAnalyzer::Analyze(Material, Resolver);
	TestEqual(TEXT("Function call site"), Report.DefaultFunctionCallSites, 1);
	TestEqual(TEXT("Function texture sample is expanded"), Report.Pixel.TextureSampleSites.Default, 1);
	TestEqual(TEXT("Function dependency resolves"), Report.UnresolvedFunctionCalls, 0);
	TestFalse(TEXT("Resolved graph has no unknown cost"), Report.bHasUnknownCost);
	return true;
}

MBP_PERF_TEST(UnknownCostRisk)
bool FMatBPPerfUnknownCostRisk::RunTest(const FString& Parameters)
{
	const FString Source = TEXT(
		"(material \"M_Custom\"\n"
		"  (expressions (custom $code :code \"return 1;\" :output-type 0))\n"
		"  (outputs :emissive-color (connect $code)))");
	const TSharedPtr<FMaterialGraphAST> AST = MatBP2FPStaticCostTests::ParseChecked(*this, Source, TEXT("M_Custom.matlang"));
	TestTrue(TEXT("AST is valid"), AST.IsValid());
	if (!AST.IsValid()) return false;

	const FMatLangStaticCostReport Report = FMatLangStaticCostAnalyzer::Analyze(AST);
	TestTrue(TEXT("Custom HLSL marks unknown cost"), Report.bHasUnknownCost);
	TestTrue(TEXT("Custom HLSL risk is emitted"), Report.Risks.ContainsByPredicate([](const FMatLangStaticCostRisk& Risk)
	{
		return Risk.RuleId == TEXT("MP3001");
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
