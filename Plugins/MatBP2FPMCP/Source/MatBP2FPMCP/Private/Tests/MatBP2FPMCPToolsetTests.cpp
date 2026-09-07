// UE 5.8 native MCP Toolset registration and schema tests.

#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP && WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "MatBP2FPMCPToolset.h"
#include "Misc/AutomationTest.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#define MBP_MCP_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBP2FPMCPToolsetSchema,
	"MatBP2FP.MCP.ToolsetSchema", MBP_MCP_TEST_FLAGS)

bool FMatBP2FPMCPToolsetSchema::RunTest(const FString& Parameters)
{
	const FString InspectSchema = UToolsetRegistry::GetToolsetJsonSchema(
		UMatBP2FPInspectToolset::StaticClass());
	const FString EditSchema = UToolsetRegistry::GetToolsetJsonSchema(
		UMatBP2FPEditToolset::StaticClass());

	TestFalse(TEXT("Inspect schema is not empty"), InspectSchema.IsEmpty());
	TestFalse(TEXT("Edit schema is not empty"), EditSchema.IsEmpty());
	TestTrue(TEXT("Inspect schema contains export tool"),
		InspectSchema.Contains(TEXT("ExportMaterialToText")));
	TestTrue(TEXT("Inspect schema contains project path tool"),
		InspectSchema.Contains(TEXT("MaterialPathToDSLPath")));
	TestTrue(TEXT("Edit schema contains import tool"),
		EditSchema.Contains(TEXT("ImportMaterialFromText")));
	TestTrue(TEXT("Edit schema contains explicit save argument"),
		EditSchema.Contains(TEXT("bSavePackage")));
	TestFalse(TEXT("MCP surface does not expose file system import"),
		EditSchema.Contains(TEXT("ImportMaterialFromFile")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBP2FPMCPToolsetRegistration,
	"MatBP2FP.MCP.ToolsetRegistration", MBP_MCP_TEST_FLAGS)

bool FMatBP2FPMCPToolsetRegistration::RunTest(const FString& Parameters)
{
	const bool bInspectWasRegistered = UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPInspectToolset::StaticClass());
	const bool bEditWasRegistered = UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPEditToolset::StaticClass());

	if (!bInspectWasRegistered)
	{
		UToolsetRegistry::RegisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
	}
	if (!bEditWasRegistered)
	{
		UToolsetRegistry::RegisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
	}

	TestTrue(TEXT("Inspect toolset is registered"), UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPInspectToolset::StaticClass()));
	TestTrue(TEXT("Edit toolset is registered"), UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPEditToolset::StaticClass()));

	if (!bEditWasRegistered)
	{
		UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
	}
	if (!bInspectWasRegistered)
	{
		UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
	}
	return true;
}

#endif // MATBP2FP_WITH_MCP && WITH_DEV_AUTOMATION_TESTS
