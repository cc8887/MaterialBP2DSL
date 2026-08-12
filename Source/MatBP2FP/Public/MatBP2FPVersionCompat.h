// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunctionInterface.h"

class UMaterialExpression;

namespace MatBP2FPCompat
{
	inline const TCHAR* ImportPropertyText(FProperty* Property, const FString& Value, void* ValuePtr, UObject* Owner)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		return Property->ImportText_Direct(*Value, ValuePtr, Owner, PPF_None);
#else
		return Property->ImportText(*Value, ValuePtr, PPF_None, Owner);
#endif
	}

	inline TArray<UMaterialExpression*> GetMaterialExpressions(UMaterial* Material)
	{
		TArray<UMaterialExpression*> Result;
		if (!Material)
		{
			return Result;
		}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			Result.Add(Expression);
		}
#else
		for (UMaterialExpression* Expression : Material->Expressions)
		{
			Result.Add(Expression);
		}
#endif
		return Result;
	}

	inline TArray<UMaterialExpression*> GetFunctionExpressions(UMaterialFunctionInterface* Function)
	{
		TArray<UMaterialExpression*> Result;
		if (!Function)
		{
			return Result;
		}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		for (UMaterialExpression* Expression : Function->GetExpressions())
		{
			Result.Add(Expression);
		}
#elif ENGINE_MAJOR_VERSION == 5
		if (const TArray<TObjectPtr<UMaterialExpression>>* Expressions = Function->GetFunctionExpressions())
		{
			for (UMaterialExpression* Expression : *Expressions)
			{
				Result.Add(Expression);
			}
		}
#else
		if (const TArray<UMaterialExpression*>* Expressions = Function->GetFunctionExpressions())
		{
			Result = *Expressions;
		}
#endif
		return Result;
	}

	inline int32 CountExpressionInputs(UMaterialExpression* Expression)
	{
		if (!Expression)
		{
			return 0;
		}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
		return Expression->CountInputs();
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		return Expression->GetInputsView().Num();
#else
		return Expression->GetInputs().Num();
#endif
	}

	inline void AddMaterialExpression(UMaterial* Material, UMaterialExpression* Expression)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		Material->GetExpressionCollection().AddExpression(Expression);
#else
		Material->Expressions.Add(Expression);
#endif
	}

	inline void ClearMaterialExpressions(UMaterial* Material)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		Material->GetExpressionCollection().Empty();
#else
		Material->Expressions.Empty();
#endif
	}

	inline FExpressionInput* GetMaterialInput(UMaterial* Material, const FString& SlotName)
	{
		if (!Material)
		{
			return nullptr;
		}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		if (!Data)
		{
			return nullptr;
		}
#define MATBP2FP_INPUT(Field) &Data->Field
#else
#define MATBP2FP_INPUT(Field) &Material->Field
#endif

		if (SlotName == TEXT("base-color")) return MATBP2FP_INPUT(BaseColor);
		if (SlotName == TEXT("metallic")) return MATBP2FP_INPUT(Metallic);
		if (SlotName == TEXT("specular")) return MATBP2FP_INPUT(Specular);
		if (SlotName == TEXT("roughness")) return MATBP2FP_INPUT(Roughness);
		if (SlotName == TEXT("anisotropy")) return MATBP2FP_INPUT(Anisotropy);
		if (SlotName == TEXT("emissive-color")) return MATBP2FP_INPUT(EmissiveColor);
		if (SlotName == TEXT("opacity")) return MATBP2FP_INPUT(Opacity);
		if (SlotName == TEXT("opacity-mask")) return MATBP2FP_INPUT(OpacityMask);
		if (SlotName == TEXT("normal")) return MATBP2FP_INPUT(Normal);
		if (SlotName == TEXT("tangent")) return MATBP2FP_INPUT(Tangent);
		if (SlotName == TEXT("world-position-offset")) return MATBP2FP_INPUT(WorldPositionOffset);
		if (SlotName == TEXT("subsurface-color")) return MATBP2FP_INPUT(SubsurfaceColor);
		if (SlotName == TEXT("ambient-occlusion")) return MATBP2FP_INPUT(AmbientOcclusion);
		if (SlotName == TEXT("refraction")) return MATBP2FP_INPUT(Refraction);
		if (SlotName == TEXT("pixel-depth-offset")) return MATBP2FP_INPUT(PixelDepthOffset);

#undef MATBP2FP_INPUT
		return nullptr;
	}

	inline void ClearMaterialInputs(UMaterial* Material)
	{
		if (!Material)
		{
			return;
		}
		for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
		{
			if (FExpressionInput* Input = Material->GetExpressionInputForProperty(
				static_cast<EMaterialProperty>(PropertyIndex)))
			{
				*Input = FExpressionInput();
			}
		}
	}
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
#define MATBP2FP_ASSET_CLASS(ClassType) ClassType::StaticClass()->GetClassPathName()
#else
#define MATBP2FP_ASSET_CLASS(ClassType) ClassType::StaticClass()->GetFName()
#endif

#endif // WITH_EDITOR
