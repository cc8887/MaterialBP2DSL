// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.
// Negative and mutation-safety tests for the MatLang lint pipeline.

#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "MatBP2FPVersionCompat.h"
#include "MatBPImporter.h"
#include "MatLangLinter.h"
#include "Misc/AutomationTest.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_DEV_AUTOMATION_TESTS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_LINT_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_LINT_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

#define MBP_LINT_TEST(Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPLint##Name, "MatBP2FP.Lint." #Name, MBP_LINT_TEST_FLAGS)

namespace MatBP2FPLintTests
{
	bool HasRule(const FMatLangLintResult& Result, const FString& RuleId)
	{
		for (const FMatLangDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.RuleId == RuleId)
			{
				return true;
			}
		}
		return false;
	}

	const FMatLangDiagnostic* FindRule(const FMatLangLintResult& Result, const FString& RuleId)
	{
		for (const FMatLangDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.RuleId == RuleId)
			{
				return &Diagnostic;
			}
		}
		return nullptr;
	}
}

MBP_LINT_TEST(SyntaxDiagnostics)
bool FMatBPLintSyntaxDiagnostics::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Source;
		const TCHAR* ExpectedRule;
	};

	static const FCase Cases[] = {
		{TEXT("empty"), TEXT(""), TEXT("ML0002")},
		{TEXT("unexpected character"), TEXT("(material \"M\" @)"), TEXT("ML0001")},
		{TEXT("missing closing paren"), TEXT("(material \"M\""), TEXT("ML0002")},
		{TEXT("malformed exponent"), TEXT("(material \"M\" :custom-value 1e)"), TEXT("ML0001")},
		{TEXT("isolated expression id"), TEXT("(material \"M\" (expressions (constant $ :value 1)))"), TEXT("ML0001")},
		{TEXT("trailing document"), TEXT("(material \"M\") (material \"N\")"), TEXT("ML0002")},
		{TEXT("unknown enum"), TEXT("(material \"M\" :domain surfce)"), TEXT("ML1101")},
		{TEXT("duplicate property"), TEXT("(material \"M\" :domain surface :domain surface)"), TEXT("ML1201")}
	};

	for (const FCase& Case : Cases)
	{
		const FMatLangLintResult Result = FMatLangLinter::Lint(Case.Source, TEXT("case.matlang"));
		TestTrue(FString::Printf(TEXT("%s has errors"), Case.Name), Result.HasErrors());
		TestTrue(FString::Printf(TEXT("%s reports %s"), Case.Name, Case.ExpectedRule),
			MatBP2FPLintTests::HasRule(Result, Case.ExpectedRule));
	}

	const FMatLangLintResult Located = FMatLangLinter::Lint(
		TEXT("(material \"M\"\n  :domain typo)"), TEXT("located.matlang"));
	const FMatLangDiagnostic* EnumDiagnostic = MatBP2FPLintTests::FindRule(Located, TEXT("ML1101"));
	TestNotNull(TEXT("Unknown enum diagnostic exists"), EnumDiagnostic);
	if (EnumDiagnostic)
	{
		TestEqual(TEXT("Unknown enum line"), EnumDiagnostic->Span.StartLine, 2);
		TestEqual(TEXT("Unknown enum column"), EnumDiagnostic->Span.StartColumn, 11);
		TestEqual(TEXT("Diagnostic file path"), EnumDiagnostic->FilePath, FString(TEXT("located.matlang")));
	}
	return true;
}

MBP_LINT_TEST(GraphDiagnostics)
bool FMatBPLintGraphDiagnostics::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Source;
		const TCHAR* ExpectedRule;
	};

	static const FCase Cases[] = {
		{TEXT("duplicate id"),
			TEXT("(material \"M\" (expressions (constant $a :value 1) (constant $a :value 2)))"),
			TEXT("ML1001")},
		{TEXT("dangling reference"),
			TEXT("(material \"M\" (expressions (add $a :a (connect $missing))))"),
			TEXT("ML1002")},
		{TEXT("invalid connect id"),
			TEXT("(material \"M\" (expressions (add $a :a (connect missing))))"),
			TEXT("ML1003")},
		{TEXT("cycle"),
			TEXT("(material \"M\" (expressions (add $a :a (connect $b)) (add $b :a (connect $a))))"),
			TEXT("ML1004")},
		{TEXT("negative output index"),
			TEXT("(material \"M\" (expressions (constant $a :value 1) (add $b :a (connect $a -1))))"),
			TEXT("ML1005")},
		{TEXT("literal output"),
			TEXT("(material \"M\" (outputs :roughness 0.5))"),
			TEXT("ML1102")},
		{TEXT("unknown output"),
			TEXT("(material \"M\" (expressions (constant $a :value 1)) (outputs :rougness (connect $a)))"),
			TEXT("ML1103")},
		{TEXT("duplicate expression key"),
			TEXT("(material \"M\" (expressions (constant $a :value 1 :value 2)))"),
			TEXT("ML1201")}
	};

	for (const FCase& Case : Cases)
	{
		const FMatLangLintResult Result = FMatLangLinter::Lint(Case.Source);
		TestTrue(FString::Printf(TEXT("%s has errors"), Case.Name), Result.HasErrors());
		TestTrue(FString::Printf(TEXT("%s reports %s"), Case.Name, Case.ExpectedRule),
			MatBP2FPLintTests::HasRule(Result, Case.ExpectedRule));
	}
	return true;
}

MBP_LINT_TEST(ValidDocument)
bool FMatBPLintValidDocument::RunTest(const FString& Parameters)
{
	const FString Source = TEXT(
		"(material \"M_Valid\"\n"
		"  :domain surface\n"
		"  :blend-mode opaque\n"
		"  :shading-model default-lit\n"
		"  (expressions\n"
		"    (constant $value :value 0.5))\n"
		"  (outputs\n"
		"    :roughness (connect $value 0)))");
	const FMatLangLintResult Result = FMatLangLinter::Lint(Source);
	TestFalse(TEXT("Valid document has no errors"), Result.HasErrors());
	TestEqual(TEXT("Valid document has no diagnostics"), Result.Diagnostics.Num(), 0);
	return true;
}

MBP_LINT_TEST(InvalidUpdateDoesNotMutate)
bool FMatBPLintInvalidUpdateDoesNotMutate::RunTest(const FString& Parameters)
{
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Material->AddToRoot();
	Material->TwoSided = true;
	UMaterialExpressionConstant* ExistingExpression = NewObject<UMaterialExpressionConstant>(Material);
	ExistingExpression->R = 0.25f;
	MatBP2FPCompat::AddMaterialExpression(Material, ExistingExpression);

	const int32 BeforeCount = MatBP2FPCompat::GetMaterialExpressions(Material).Num();
	const FMatBPImporter::FImportResult Result = FMatBPImporter::UpdateMaterial(
		Material,
		TEXT("(material \"M_Invalid\" (expressions (add $a :a (connect $missing))))"));

	TestFalse(TEXT("Invalid update fails"), Result.bSuccess);
	TestTrue(TEXT("Invalid update reports diagnostics"), Result.Messages.Num() > 0);
	TestEqual(TEXT("Existing expression count is unchanged"),
		MatBP2FPCompat::GetMaterialExpressions(Material).Num(), BeforeCount);
	TestTrue(TEXT("Existing expression is retained"),
		MatBP2FPCompat::GetMaterialExpressions(Material).Contains(ExistingExpression));
	TestTrue(TEXT("Existing material properties are unchanged"), Material->TwoSided);

	Material->RemoveFromRoot();
	return true;
}

MBP_LINT_TEST(MaterialFunctionDocument)
bool FMatBPLintMaterialFunctionDocument::RunTest(const FString& Parameters)
{
	const FString Source =
		TEXT("(material-function \"MF_Test\"\n")
		TEXT("  :asset-path \"/Game/Functions/MF_Test.MF_Test\"\n")
		TEXT("  (function-inputs (input :name \"Value\" :sort-priority 0))\n")
		TEXT("  (function-outputs (output :name \"Result\" :sort-priority 1))\n")
		TEXT("  (expressions (constant $value :value 1.0))\n")
		TEXT("  (outputs))");
	const FMatLangLintResult Result = FMatLangLinter::Lint(Source, TEXT("MF_Test.matlang"));
	TestFalse(TEXT("Material function document should lint without errors"), Result.HasErrors());
	TestTrue(TEXT("Material function AST should be produced"), Result.AST.IsValid());
	if (Result.AST.IsValid())
	{
		TestEqual(TEXT("Graph kind"), Result.AST->Kind, EMatLangGraphKind::MaterialFunction);
		TestEqual(TEXT("Asset path"), Result.AST->AssetPath, FString(TEXT("/Game/Functions/MF_Test.MF_Test")));
		TestEqual(TEXT("Function input count"), Result.AST->FunctionInputs.Num(), 1);
		TestEqual(TEXT("Function output count"), Result.AST->FunctionOutputs.Num(), 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
