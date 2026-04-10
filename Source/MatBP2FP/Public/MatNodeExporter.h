// MatNodeExporter.h - Export UE Material Expression definitions to MatLang stub
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/**
 * 从 UE 引擎导出材质表达式定义为 MatLang 类型存根
 * 类似于 TypeScript 的 .d.ts 生成
 *
 * 扫描所有 UMaterialExpression 子类，提取引脚签名和属性信息，
 * 输出 S-expression 格式的 stub 文件供 Lint/校验使用。
 */
class MATBP2FP_API FMatNodeExporter
{
public:
	/**
	 * 表达式节点信息
	 */
	struct FExprInfo
	{
		FString ExprName;        // kebab-case DSL 名, e.g. "texture-sample"
		FString ClassName;       // e.g. "UMaterialExpressionTextureSample"
		FString Description;     // 节点描述

		// 输入引脚
		struct FPinInfo
		{
			FString Name;        // e.g. "a", "b", "texture", "uv"
			int32 NumInputs;     // 该引脚接受的输入数量 (Material expression pins can have multiple inputs)
			FString TypeHint;    // 类型提示: "float", "float3", "texture2d", "any"
		};
		TArray<FPinInfo> InputPins;

		// 输出引脚
		struct FOutputInfo
		{
			FString Name;        // 输出名
			int32 OutputIndex;   // 输出索引
			FString TypeHint;    // 类型提示
		};
		TArray<FOutputInfo> OutputPins;

		// 可编辑属性
		TArray<FString> PropertyNames;
	};

	/**
	 * 扫描所有 UMaterialExpression 子类并导出到 stub 文件
	 * @param OutputPath 输出路径
	 * @return 是否成功
	 */
	static bool ExportAllExpressions(const FString& OutputPath);

	/**
	 * 扫描所有材质表达式类
	 */
	static TArray<FExprInfo> ScanAllMaterialExpressions();

private:
	// 命名转换: TextureSample -> texture-sample
	static FString CamelToKebab(const FString& PascalCase);
};

#endif // WITH_EDITOR
