// Tests for mount-safe export paths and reference-index serialization.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MatBP2FPExportService.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_EXPORT_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_EXPORT_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPExportMountSafePaths,
	"MatBP2FP.Export.MountSafePaths", MBP_EXPORT_TEST_FLAGS)

bool FMatBPExportMountSafePaths::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectIntermediateDir() / TEXT("MatBP2FPExportPathTest"));
	const FString Game = FMatBP2FPExportService::BuildOutputPath(
		TEXT("/Game/Common/M_Base.M_Base"), Root);
	const FString Plugin = FMatBP2FPExportService::BuildOutputPath(
		TEXT("/MyPlugin/Common/M_Base.M_Base"), Root);
	TestTrue(TEXT("Game path retains mount point"), Game.EndsWith(TEXT("Game/Common/M_Base.matlang")));
	TestTrue(TEXT("Plugin path retains mount point"), Plugin.EndsWith(TEXT("MyPlugin/Common/M_Base.matlang")));
	TestNotEqual(TEXT("Different mounts must not collide"), Game, Plugin);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPExportReferenceIndexRoundTrip,
	"MatBP2FP.Export.ReferenceIndexRoundTrip", MBP_EXPORT_TEST_FLAGS)

bool FMatBPExportReferenceIndexRoundTrip::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectIntermediateDir() / TEXT("MatBP2FPReferenceIndexTest"));
	FMatBP2FPExportResult Source;
	FMatBP2FPExportedAsset Material;
	Material.AssetPath = TEXT("/Game/Materials/M_Main.M_Main");
	Material.RelativeFilePath = TEXT("Game/Materials/M_Main.matlang");
	Material.ContentHash = TEXT("abc");
	Source.Assets.Add(Material);
	FMatBP2FPExportedAsset Function;
	Function.AssetPath = TEXT("/Game/Functions/MF_Noise.MF_Noise");
	Function.Kind = EMatBP2FPGraphAssetKind::MaterialFunction;
	Function.RelativeFilePath = TEXT("Game/Functions/MF_Noise.matlang");
	Function.ContentHash = TEXT("def");
	Source.Assets.Add(Function);
	FMatBP2FPExportReference Reference;
	Reference.FromAssetPath = Material.AssetPath;
	Reference.ToAssetPath = Function.AssetPath;
	Reference.ExpressionId = TEXT("$fn1");
	Reference.SourceFile = Material.RelativeFilePath;
	Reference.SourceLine = 12;
	Source.References.Add(Reference);

	FString Error;
	TestTrue(TEXT("Index should serialize"), FMatBP2FPExportService::WriteIndex(Source, Root, Error));
	if (!Error.IsEmpty()) AddError(Error);
	FMatBP2FPReferenceIndex Loaded;
	const FString IndexPath = Root / TEXT("matlang-index.json");
	TestTrue(TEXT("Index should deserialize"), FMatBP2FPReferenceIndex::Load(IndexPath, Loaded, Error));
	TestEqual(TEXT("Asset count"), Loaded.Assets.Num(), 2);
	TestEqual(TEXT("Reference count"), Loaded.References.Num(), 1);
	if (Loaded.References.Num() == 1)
	{
		TestEqual(TEXT("Reference target"), Loaded.References[0].ToAssetPath, Function.AssetPath);
		TestEqual(TEXT("Reference line"), Loaded.References[0].SourceLine, 12);
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
