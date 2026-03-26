// MatLangDiffer.cpp - AST Diff Engine for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangDiffer.h"

// ========== FMatLangDiffEntry ==========

FString FMatLangDiffEntry::ToString() const
{
	FString SevStr;
	switch (Severity)
	{
		case EMatLangDiffSeverity::Cosmetic:   SevStr = TEXT("cosmetic"); break;
		case EMatLangDiffSeverity::Property:   SevStr = TEXT("property"); break;
		case EMatLangDiffSeverity::Structural: SevStr = TEXT("STRUCTURAL"); break;
	}

	FString OpStr;
	switch (Op)
	{
		case EMatLangDiffOp::MaterialPropertyChanged: OpStr = TEXT("MatPropChanged"); break;
		case EMatLangDiffOp::ExpressionAdded:         OpStr = TEXT("ExprAdded"); break;
		case EMatLangDiffOp::ExpressionRemoved:       OpStr = TEXT("ExprRemoved"); break;
		case EMatLangDiffOp::ExpressionTypeChanged:   OpStr = TEXT("ExprTypeChanged"); break;
		case EMatLangDiffOp::ExprPropertyChanged:     OpStr = TEXT("ExprPropChanged"); break;
		case EMatLangDiffOp::ExprPropertyAdded:       OpStr = TEXT("ExprPropAdded"); break;
		case EMatLangDiffOp::ExprPropertyRemoved:     OpStr = TEXT("ExprPropRemoved"); break;
		case EMatLangDiffOp::ExprInputChanged:        OpStr = TEXT("ExprInputChanged"); break;
		case EMatLangDiffOp::ExprInputAdded:          OpStr = TEXT("ExprInputAdded"); break;
		case EMatLangDiffOp::ExprInputRemoved:        OpStr = TEXT("ExprInputRemoved"); break;
		case EMatLangDiffOp::OutputChanged:           OpStr = TEXT("OutputChanged"); break;
		case EMatLangDiffOp::OutputAdded:             OpStr = TEXT("OutputAdded"); break;
		case EMatLangDiffOp::OutputRemoved:           OpStr = TEXT("OutputRemoved"); break;
	}

	if (ExprId.IsEmpty())
	{
		return FString::Printf(TEXT("[%s] %s :%s  '%s' -> '%s'"),
			*SevStr, *OpStr, *Key, *OldValue, *NewValue);
	}
	return FString::Printf(TEXT("[%s] %s %s :%s  '%s' -> '%s'"),
		*SevStr, *OpStr, *ExprId, *Key, *OldValue, *NewValue);
}

// ========== FMatLangDiffResult ==========

FString FMatLangDiffResult::GetSummary() const
{
	return FString::Printf(TEXT("Diff: %d total (%d structural, %d property, %d cosmetic)"),
		Entries.Num(), NumStructural, NumProperty, NumCosmetic);
}

// ========== FMatLangDiffer ==========

FMatLangDiffResult FMatLangDiffer::Diff(
	const TSharedPtr<FMaterialGraphAST>& OldAST,
	const TSharedPtr<FMaterialGraphAST>& NewAST)
{
	FMatLangDiffer Differ;

	if (!OldAST || !NewAST)
	{
		if (!OldAST && NewAST)
		{
			// Entire material is new — treat as structural
			Differ.AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
				FString(), TEXT("material"), TEXT("<none>"), NewAST->Name);
		}
		return Differ.Result;
	}

	Differ.DiffMaterialProperties(*OldAST, *NewAST);
	Differ.DiffExpressions(*OldAST, *NewAST);
	Differ.DiffOutputs(OldAST->Outputs, NewAST->Outputs);

	return Differ.Result;
}

void FMatLangDiffer::DiffMaterialProperties(const FMaterialGraphAST& Old, const FMaterialGraphAST& New)
{
	// Domain
	if (Old.Domain != New.Domain)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
			FString(), TEXT("domain"),
			MatLangEnums::DomainToString(Old.Domain),
			MatLangEnums::DomainToString(New.Domain));
	}

	// Blend mode
	if (Old.BlendMode != New.BlendMode)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), TEXT("blend-mode"),
			MatLangEnums::BlendModeToString(Old.BlendMode),
			MatLangEnums::BlendModeToString(New.BlendMode));
	}

	// Shading model
	if (Old.ShadingModel != New.ShadingModel)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), TEXT("shading-model"),
			MatLangEnums::ShadingModelToString(Old.ShadingModel),
			MatLangEnums::ShadingModelToString(New.ShadingModel));
	}

	// Two-sided
	if (Old.bTwoSided != New.bTwoSided)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), TEXT("two-sided"),
			Old.bTwoSided ? TEXT("true") : TEXT("false"),
			New.bTwoSided ? TEXT("true") : TEXT("false"));
	}

	// IsMasked
	if (Old.bIsMasked != New.bIsMasked)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), TEXT("is-masked"),
			Old.bIsMasked ? TEXT("true") : TEXT("false"),
			New.bIsMasked ? TEXT("true") : TEXT("false"));
	}

	// OpacityMaskClipValue
	if (FMath::Abs(Old.OpacityMaskClipValue - New.OpacityMaskClipValue) > KINDA_SMALL_NUMBER)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), TEXT("opacity-mask-clip-value"),
			FString::SanitizeFloat(Old.OpacityMaskClipValue),
			FString::SanitizeFloat(New.OpacityMaskClipValue));
	}

	// Extra properties
	TSet<FString> AllExtraKeys;
	for (const auto& Pair : Old.ExtraProperties) AllExtraKeys.Add(Pair.Key);
	for (const auto& Pair : New.ExtraProperties) AllExtraKeys.Add(Pair.Key);

	for (const FString& Key : AllExtraKeys)
	{
		const FString* OldVal = Old.ExtraProperties.Find(Key);
		const FString* NewVal = New.ExtraProperties.Find(Key);

		if (!OldVal && NewVal)
		{
			AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
				FString(), Key, FString(), *NewVal);
		}
		else if (OldVal && !NewVal)
		{
			AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
				FString(), Key, *OldVal, FString());
		}
		else if (OldVal && NewVal && *OldVal != *NewVal)
		{
			AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
				FString(), Key, *OldVal, *NewVal);
		}
	}
}

void FMatLangDiffer::DiffExpressions(const FMaterialGraphAST& Old, const FMaterialGraphAST& New)
{
	// Build ID maps
	TMap<FString, TSharedPtr<FMatExpressionAST>> OldById;
	TMap<FString, TSharedPtr<FMatExpressionAST>> NewById;

	for (const auto& Expr : Old.Expressions) OldById.Add(Expr->Id, Expr);
	for (const auto& Expr : New.Expressions) NewById.Add(Expr->Id, Expr);

	// Removed expressions (in old but not in new)
	for (const auto& Pair : OldById)
	{
		if (!NewById.Contains(Pair.Key))
		{
			AddEntry(EMatLangDiffOp::ExpressionRemoved, EMatLangDiffSeverity::Structural,
				Pair.Key, TEXT("type"), Pair.Value->ExprType, FString());
		}
	}

	// Added expressions (in new but not in old)
	for (const auto& Pair : NewById)
	{
		if (!OldById.Contains(Pair.Key))
		{
			AddEntry(EMatLangDiffOp::ExpressionAdded, EMatLangDiffSeverity::Structural,
				Pair.Key, TEXT("type"), FString(), Pair.Value->ExprType);
		}
	}

	// Modified expressions (present in both)
	for (const auto& Pair : OldById)
	{
		const TSharedPtr<FMatExpressionAST>* NewExprPtr = NewById.Find(Pair.Key);
		if (!NewExprPtr) continue;

		const FMatExpressionAST& OldExpr = *Pair.Value;
		const FMatExpressionAST& NewExpr = **NewExprPtr;

		// Type changed? — structural
		if (OldExpr.ExprType != NewExpr.ExprType)
		{
			AddEntry(EMatLangDiffOp::ExpressionTypeChanged, EMatLangDiffSeverity::Structural,
				Pair.Key, TEXT("type"), OldExpr.ExprType, NewExpr.ExprType);
			continue; // No point diffing properties if type changed
		}

		// Editor position changed? — cosmetic
		if (FVector2D::DistSquared(OldExpr.EditorPosition, NewExpr.EditorPosition) > 1.0)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyChanged, EMatLangDiffSeverity::Cosmetic,
				Pair.Key, TEXT("editor-position"),
				FString::Printf(TEXT("(%g %g)"), OldExpr.EditorPosition.X, OldExpr.EditorPosition.Y),
				FString::Printf(TEXT("(%g %g)"), NewExpr.EditorPosition.X, NewExpr.EditorPosition.Y));
		}

		// Comment changed? — cosmetic
		if (OldExpr.Comment != NewExpr.Comment)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyChanged, EMatLangDiffSeverity::Cosmetic,
				Pair.Key, TEXT("comment"), OldExpr.Comment, NewExpr.Comment);
		}

		// Diff properties and inputs
		DiffExpressionProperties(Pair.Key, OldExpr, NewExpr);
		DiffExpressionInputs(Pair.Key, OldExpr, NewExpr);
	}
}

void FMatLangDiffer::DiffExpressionProperties(
	const FString& Id, const FMatExpressionAST& Old, const FMatExpressionAST& New)
{
	TSet<FString> AllKeys;
	for (const auto& Pair : Old.Properties) AllKeys.Add(Pair.Key);
	for (const auto& Pair : New.Properties) AllKeys.Add(Pair.Key);

	for (const FString& Key : AllKeys)
	{
		const FString* OldVal = Old.Properties.Find(Key);
		const FString* NewVal = New.Properties.Find(Key);

		if (!OldVal && NewVal)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyAdded, EMatLangDiffSeverity::Property,
				Id, Key, FString(), *NewVal);
		}
		else if (OldVal && !NewVal)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyRemoved, EMatLangDiffSeverity::Property,
				Id, Key, *OldVal, FString());
		}
		else if (OldVal && NewVal && *OldVal != *NewVal)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyChanged, EMatLangDiffSeverity::Property,
				Id, Key, *OldVal, *NewVal);
		}
	}
}

void FMatLangDiffer::DiffExpressionInputs(
	const FString& Id, const FMatExpressionAST& Old, const FMatExpressionAST& New)
{
	// Build input maps by name
	TMap<FString, const FMatLangInput*> OldInputs;
	TMap<FString, const FMatLangInput*> NewInputs;

	for (const FMatLangInput& Input : Old.Inputs) OldInputs.Add(Input.Name, &Input);
	for (const FMatLangInput& Input : New.Inputs) NewInputs.Add(Input.Name, &Input);

	TSet<FString> AllNames;
	for (const auto& Pair : OldInputs) AllNames.Add(Pair.Key);
	for (const auto& Pair : NewInputs) AllNames.Add(Pair.Key);

	for (const FString& Name : AllNames)
	{
		const FMatLangInput** OldInputPtr = OldInputs.Find(Name);
		const FMatLangInput** NewInputPtr = NewInputs.Find(Name);

		if (!OldInputPtr && NewInputPtr)
		{
			AddEntry(EMatLangDiffOp::ExprInputAdded, EMatLangDiffSeverity::Property,
				Id, Name, FString(), InputToString(**NewInputPtr));
		}
		else if (OldInputPtr && !NewInputPtr)
		{
			AddEntry(EMatLangDiffOp::ExprInputRemoved, EMatLangDiffSeverity::Property,
				Id, Name, InputToString(**OldInputPtr), FString());
		}
		else if (OldInputPtr && NewInputPtr)
		{
			FString OldStr = InputToString(**OldInputPtr);
			FString NewStr = InputToString(**NewInputPtr);
			if (OldStr != NewStr)
			{
				AddEntry(EMatLangDiffOp::ExprInputChanged, EMatLangDiffSeverity::Property,
					Id, Name, OldStr, NewStr);
			}
		}
	}
}

void FMatLangDiffer::DiffOutputs(const FMatOutputsAST& Old, const FMatOutputsAST& New)
{
	TSet<FString> AllSlots;
	for (const auto& Pair : Old.Slots) AllSlots.Add(Pair.Key);
	for (const auto& Pair : New.Slots) AllSlots.Add(Pair.Key);

	for (const FString& Slot : AllSlots)
	{
		const FMatLangInput* OldInput = Old.Slots.Find(Slot);
		const FMatLangInput* NewInput = New.Slots.Find(Slot);

		if (!OldInput && NewInput)
		{
			AddEntry(EMatLangDiffOp::OutputAdded, EMatLangDiffSeverity::Property,
				FString(), Slot, FString(), InputToString(*NewInput));
		}
		else if (OldInput && !NewInput)
		{
			AddEntry(EMatLangDiffOp::OutputRemoved, EMatLangDiffSeverity::Property,
				FString(), Slot, InputToString(*OldInput), FString());
		}
		else if (OldInput && NewInput)
		{
			FString OldStr = InputToString(*OldInput);
			FString NewStr = InputToString(*NewInput);
			if (OldStr != NewStr)
			{
				AddEntry(EMatLangDiffOp::OutputChanged, EMatLangDiffSeverity::Property,
					FString(), Slot, OldStr, NewStr);
			}
		}
	}
}

void FMatLangDiffer::AddEntry(EMatLangDiffOp Op, EMatLangDiffSeverity Severity,
	const FString& ExprId, const FString& Key,
	const FString& OldValue, const FString& NewValue)
{
	FMatLangDiffEntry Entry;
	Entry.Op = Op;
	Entry.Severity = Severity;
	Entry.ExprId = ExprId;
	Entry.Key = Key;
	Entry.OldValue = OldValue;
	Entry.NewValue = NewValue;

	Result.Entries.Add(MoveTemp(Entry));

	switch (Severity)
	{
		case EMatLangDiffSeverity::Cosmetic:   Result.NumCosmetic++; break;
		case EMatLangDiffSeverity::Property:   Result.NumProperty++; break;
		case EMatLangDiffSeverity::Structural: Result.NumStructural++; break;
	}
}

FString FMatLangDiffer::InputToString(const FMatLangInput& Input)
{
	if (Input.IsConnected())
	{
		return Input.Connection->ToString(); // e.g. "(connect $tex1 0)"
	}
	if (Input.IsLiteral())
	{
		return *Input.LiteralValue;
	}
	return TEXT("<empty>");
}
