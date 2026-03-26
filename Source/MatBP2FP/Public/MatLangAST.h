// MatLangAST.h - Abstract Syntax Tree for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.
//
// Material graphs are DAGs (Directed Acyclic Graphs), not trees.
// Each expression node has a unique ID, and connections reference target IDs.
//
// DSL Format:
//   (material "M_Example"
//     :domain surface
//     :blend-mode opaque
//     :shading-model default-lit
//     :two-sided false
//     (expressions
//       (texture-sample $tex1
//         :texture (asset "/Game/Textures/T_Brick_D")
//         :sampler-type color)
//       (vector-parameter $color1
//         :name "BaseColor"
//         :default (0.5 0.5 0.5 1.0))
//       (multiply $mul1
//         :a (connect $tex1 0)
//         :b (connect $color1 0))
//       (constant $const1
//         :value 0.8))
//     (outputs
//       :base-color (connect $mul1 0)
//       :metallic (connect $const1 0)
//       :roughness 0.5))

#pragma once

#include "CoreMinimal.h"

// ---- Material Domain ----
enum class EMatLangDomain : uint8
{
	Surface,
	DeferredDecal,
	LightFunction,
	Volume,
	PostProcess,
	UserInterface
};

// ---- Blend Mode ----
enum class EMatLangBlendMode : uint8
{
	Opaque,
	Masked,
	Translucent,
	Additive,
	Modulate,
	AlphaComposite,
	AlphaHoldout
};

// ---- Shading Model ----
enum class EMatLangShadingModel : uint8
{
	Unlit,
	DefaultLit,
	Subsurface,
	PreintegratedSkin,
	ClearCoat,
	SubsurfaceProfile,
	TwoSidedFoliage,
	Hair,
	Cloth,
	Eye,
	SingleLayerWater,
	ThinTranslucent,
	Strata
};

/**
 * Connection reference — points to another expression's output
 * DSL: (connect $target-id output-index)
 */
struct MATBP2FP_API FMatLangConnection
{
	FString TargetId;      // Expression ID (e.g. "$tex1")
	int32 OutputIndex;     // Which output of the target (usually 0)
	
	FMatLangConnection() : OutputIndex(0) {}
	FMatLangConnection(const FString& InId, int32 InIdx) : TargetId(InId), OutputIndex(InIdx) {}
	
	bool IsValid() const { return !TargetId.IsEmpty(); }
	FString ToString() const;
};

/**
 * Expression input — either a connection to another expression or a literal value
 */
struct MATBP2FP_API FMatLangInput
{
	FString Name;          // Input pin name (e.g. "a", "b", "uv", "texture")
	
	// One of these is set:
	TOptional<FMatLangConnection> Connection;  // Connected to another expression
	TOptional<FString> LiteralValue;           // Literal (float, vector, etc.)
	
	bool IsConnected() const { return Connection.IsSet() && Connection->IsValid(); }
	bool IsLiteral() const { return LiteralValue.IsSet(); }
	FString ToString(int32 Indent = 0) const;
};

/**
 * Expression node AST — one node in the material DAG
 * DSL: (expression-type $id :input1 value :input2 (connect $other 0) ...)
 */
struct MATBP2FP_API FMatExpressionAST
{
	virtual ~FMatExpressionAST() = default;
	
	FString ExprType;       // "texture-sample", "multiply", "add", "constant", etc.
	FString Id;             // Unique ID within material (e.g. "$tex1", "$mul1")
	TMap<FString, FString> Properties;  // Non-input properties (:name, :texture, :sampler-type, etc.)
	TArray<FMatLangInput> Inputs;       // Named inputs with connections or literals
	
	// Editor metadata
	FVector2D EditorPosition;   // Node position in material editor
	FString Comment;            // Optional node comment
	
	virtual FString ToString(int32 Indent = 0) const;
	
	// Property helpers
	FString GetStringProperty(const FString& Key, const FString& Default = TEXT("")) const;
	float GetFloatProperty(const FString& Key, float Default = 0.0f) const;
	bool GetBoolProperty(const FString& Key, bool Default = false) const;
	
	// Input helpers
	const FMatLangInput* FindInput(const FString& Name) const;
	void AddInput(const FString& Name, const FMatLangConnection& Conn);
	void AddInput(const FString& Name, const FString& LiteralValue);
};

/**
 * Material output connections — which expressions feed into the material result node
 * DSL: (outputs :base-color (connect $mul1 0) :metallic 0.5 :roughness (connect $rough 0) ...)
 */
struct MATBP2FP_API FMatOutputsAST
{
	// Each output slot can be a connection or a literal
	TMap<FString, FMatLangInput> Slots;  // "base-color", "metallic", "roughness", "normal", etc.
	
	FString ToString(int32 Indent = 0) const;
};

/**
 * Parameter definition for material instances
 * Extracted from Parameter expressions (ScalarParameter, VectorParameter, TextureParameter, etc.)
 */
struct MATBP2FP_API FMatParameterDef
{
	FString Name;
	FString Type;           // "scalar", "vector", "texture", "static-switch"
	FString Group;          // Parameter group
	FString DefaultValue;   // Default value as string
	int32 SortPriority;
	
	FString ToString() const;
};

/**
 * Complete Material AST — the full material graph
 * DSL: (material "Name" :domain ... :blend-mode ... (expressions ...) (outputs ...))
 */
struct MATBP2FP_API FMaterialGraphAST
{
	FString Name;
	
	// Material properties
	EMatLangDomain Domain;
	EMatLangBlendMode BlendMode;
	EMatLangShadingModel ShadingModel;
	bool bTwoSided;
	bool bIsMasked;
	float OpacityMaskClipValue;
	
	// Expression nodes (ordered — topological or editor-order)
	TArray<TSharedPtr<FMatExpressionAST>> Expressions;
	
	// Material outputs (connection to material result node)
	FMatOutputsAST Outputs;
	
	// Extracted parameters (convenience, derived from Parameter expressions)
	TArray<FMatParameterDef> Parameters;
	
	// Additional material properties as key-value
	TMap<FString, FString> ExtraProperties;
	
	FMaterialGraphAST();
	
	FString ToString() const;
	
	// Find expression by ID
	TSharedPtr<FMatExpressionAST> FindExpression(const FString& Id) const;
	
	// Topological sort (for serialization order)
	TArray<TSharedPtr<FMatExpressionAST>> GetTopologicalOrder() const;
};

// ---- Enum <-> String conversion ----

namespace MatLangEnums
{
	MATBP2FP_API FString DomainToString(EMatLangDomain Domain);
	MATBP2FP_API EMatLangDomain StringToDomain(const FString& Str);
	
	MATBP2FP_API FString BlendModeToString(EMatLangBlendMode Mode);
	MATBP2FP_API EMatLangBlendMode StringToBlendMode(const FString& Str);
	
	MATBP2FP_API FString ShadingModelToString(EMatLangShadingModel Model);
	MATBP2FP_API EMatLangShadingModel StringToShadingModel(const FString& Str);
}
