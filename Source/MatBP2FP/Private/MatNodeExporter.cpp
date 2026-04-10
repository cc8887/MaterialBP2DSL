// MatNodeExporter.cpp - Export UE Material Expression definitions to MatLang stub
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatNodeExporter.h"

#if WITH_EDITOR
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "UObject/UObjectIterator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ========== 主导出函数 ==========

bool FMatNodeExporter::ExportAllExpressions(const FString& OutputPath)
{
	TArray<FExprInfo> AllExprs = ScanAllMaterialExpressions();

	// Sort by name for stable output
	AllExprs.Sort([](const FExprInfo& A, const FExprInfo& B)
	{
		return A.ExprName < B.ExprName;
	});

	FString Output;
	Output += TEXT(";; Auto-generated MatLang Expression Definitions (Stub)\n");
	Output += TEXT(";; Generated from Unreal Engine\n");
	Output += TEXT(";; Do not edit manually\n\n");
	Output += TEXT(";; Total: ") + FString::FromInt(AllExprs.Num()) + TEXT(" expression types\n\n");

	for (const FExprInfo& Expr : AllExprs)
	{
		// Header comment
		if (!Expr.Description.IsEmpty())
		{
			Output += TEXT(";; ") + Expr.Description + TEXT("\n");
		}
		Output += TEXT(";; UE Class: ") + Expr.ClassName + TEXT("\n");
		Output += TEXT("(define-expr ") + Expr.ExprName + TEXT("\n");

		// Properties
		if (Expr.PropertyNames.Num() > 0)
		{
			Output += TEXT("  (properties");
			for (const FString& Prop : Expr.PropertyNames)
			{
				Output += TEXT("\n    ") + Prop;
			}
			Output += TEXT(")\n");
		}

		Output += TEXT(")\n\n");
	}

	// Ensure directory exists
	FString Dir = FPaths::GetPath(OutputPath);
	IFileManager::Get().MakeDirectory(*Dir, true);

	return FFileHelper::SaveStringToFile(Output, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ========== 扫描所有材质表达式 ==========

TArray<FMatNodeExporter::FExprInfo> FMatNodeExporter::ScanAllMaterialExpressions()
{
	TArray<FExprInfo> Exprs;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;

		if (!Class->IsChildOf(UMaterialExpression::StaticClass()))
			continue;

		// Skip abstract classes
		if (Class->HasAnyClassFlags(CLASS_Abstract))
			continue;

		// Skip deprecated / internal
		if (Class->GetName().StartsWith(TEXT("MaterialExpressionComment")))
			continue;
		if (Class->GetName().StartsWith(TEXT("MaterialExpressionExternal")))
			continue;

		FExprInfo Info;
		Info.ClassName = Class->GetName();

		// Convert to kebab-case: MaterialExpressionTextureSample -> texture-sample
		FString ShortName = Info.ClassName;
		static const int32 PrefixLen = FCString::Strlen(TEXT("MaterialExpression"));
		if (ShortName.Len() > PrefixLen && ShortName.StartsWith(TEXT("MaterialExpression")))
		{
			ShortName = ShortName.Mid(PrefixLen);
		}
		Info.ExprName = CamelToKebab(ShortName);
		Info.Description = Class->GetToolTipText().ToString();

		// Extract editable properties
		for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) &&
				!Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
			{
				// Skip base UObject properties
				if (Property->GetFName() == TEXT("bIsCollapsed") ||
					Property->GetFName() == TEXT("MaterialExpressionEditorX") ||
					Property->GetFName() == TEXT("MaterialExpressionEditorY"))
					continue;

				Info.PropertyNames.Add(Property->GetName());
			}
		}

		Exprs.Add(Info);
	}

	return Exprs;
}

// ========== 命名转换 ==========

FString FMatNodeExporter::CamelToKebab(const FString& PascalCase)
{
	FString Result;
	for (int32 i = 0; i < PascalCase.Len(); ++i)
	{
		TCHAR Ch = PascalCase[i];
		if (FChar::IsUpper(Ch) && i > 0)
		{
			bool bPrevUpper = FChar::IsUpper(PascalCase[i - 1]);
			bool bNextLower = (i + 1 < PascalCase.Len()) && FChar::IsLower(PascalCase[i + 1]);
			if (!bPrevUpper || bNextLower)
			{
				Result += TEXT('-');
			}
		}
		Result += FChar::ToLower(Ch);
	}
	return Result;
}

#endif // WITH_EDITOR
