// MatLangLinter.cpp - Core syntax and graph validation for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangLinter.h"

namespace
{
	FMatLangSourceSpan WithFallback(const FMatLangSourceSpan& Span)
	{
		if (Span.IsValid())
		{
			return Span;
		}

		FMatLangSourceSpan Result;
		Result.StartLine = Result.EndLine = 1;
		Result.StartColumn = 1;
		Result.EndColumn = 2;
		Result.StartOffset = 0;
		Result.EndOffset = 1;
		return Result;
	}

	void AddError(
		FMatLangLintResult& Result,
		const FString& FilePath,
		const FString& RuleId,
		const FString& Message,
		const FMatLangSourceSpan& Span)
	{
		FMatLangDiagnostic Diagnostic;
		Diagnostic.RuleId = RuleId;
		Diagnostic.Severity = EMatLangDiagnosticSeverity::Error;
		Diagnostic.Message = Message;
		Diagnostic.FilePath = FilePath;
		Diagnostic.Span = WithFallback(Span);
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}

	bool IsKnownMaterialOutput(const FString& Name)
	{
		static const TCHAR* KnownOutputs[] = {
			TEXT("base-color"), TEXT("metallic"), TEXT("specular"), TEXT("roughness"),
			TEXT("anisotropy"), TEXT("emissive-color"), TEXT("opacity"), TEXT("opacity-mask"),
			TEXT("normal"), TEXT("tangent"), TEXT("world-position-offset"), TEXT("subsurface-color"),
			TEXT("ambient-occlusion"), TEXT("refraction"), TEXT("pixel-depth-offset")
		};

		for (const TCHAR* KnownOutput : KnownOutputs)
		{
			if (Name == KnownOutput)
			{
				return true;
			}
		}
		return false;
	}

	void SortDiagnostics(TArray<FMatLangDiagnostic>& Diagnostics)
	{
		Diagnostics.StableSort([](const FMatLangDiagnostic& A, const FMatLangDiagnostic& B)
		{
			if (A.FilePath != B.FilePath) return A.FilePath < B.FilePath;
			if (A.Span.StartLine != B.Span.StartLine) return A.Span.StartLine < B.Span.StartLine;
			if (A.Span.StartColumn != B.Span.StartColumn) return A.Span.StartColumn < B.Span.StartColumn;
			return A.RuleId < B.RuleId;
		});
	}
}

bool FMatLangLintResult::HasErrors() const
{
	return Count(EMatLangDiagnosticSeverity::Error) > 0;
}

int32 FMatLangLintResult::Count(EMatLangDiagnosticSeverity Severity) const
{
	int32 Total = 0;
	for (const FMatLangDiagnostic& Diagnostic : Diagnostics)
	{
		if (Diagnostic.Severity == Severity)
		{
			++Total;
		}
	}
	return Total;
}

FMatLangLintResult FMatLangLinter::Lint(const FString& Source, const FString& FilePath)
{
	FMatLangParseResult ParseResult = FMatLangParser::ParseDocument(Source, FilePath);
	FMatLangLintResult Result;
	Result.AST = ParseResult.AST;
	Result.Diagnostics = MoveTemp(ParseResult.Diagnostics);

	if (!Result.HasErrors() && Result.AST.IsValid())
	{
		FMatLangLintResult SemanticResult = LintAST(Result.AST, FilePath);
		Result.Diagnostics.Append(MoveTemp(SemanticResult.Diagnostics));
	}

	SortDiagnostics(Result.Diagnostics);
	return Result;
}

FMatLangLintResult FMatLangLinter::LintAST(
	const TSharedPtr<FMaterialGraphAST>& AST,
	const FString& FilePath)
{
	FMatLangLintResult Result;
	Result.AST = AST;
	if (!AST.IsValid())
	{
		AddError(Result, FilePath, TEXT("ML0002"), TEXT("Document did not produce an AST"), FMatLangSourceSpan());
		return Result;
	}

	TMap<FString, TSharedPtr<FMatExpressionAST>> ExpressionsById;
	TMap<FString, FMatLangSourceSpan> FirstIdSpans;
	for (const TSharedPtr<FMatExpressionAST>& Expression : AST->Expressions)
	{
		if (!Expression.IsValid())
		{
			continue;
		}

		if (!Expression->Id.StartsWith(TEXT("$")) || Expression->Id.Len() <= 1)
		{
			AddError(Result, FilePath, TEXT("ML1003"),
				FString::Printf(TEXT("Expression ID '%s' must begin with '$' and contain a name"), *Expression->Id),
				Expression->IdSpan);
		}

		if (ExpressionsById.Contains(Expression->Id))
		{
			AddError(Result, FilePath, TEXT("ML1001"),
				FString::Printf(TEXT("Duplicate expression ID '%s'"), *Expression->Id),
				Expression->IdSpan);
			FMatLangDiagnostic& Diagnostic = Result.Diagnostics.Last();
			FMatLangRelatedLocation Related;
			Related.Message = TEXT("First definition is here");
			Related.Span = FirstIdSpans.FindRef(Expression->Id);
			Diagnostic.RelatedLocations.Add(MoveTemp(Related));
			continue;
		}

		ExpressionsById.Add(Expression->Id, Expression);
		FirstIdSpans.Add(Expression->Id, Expression->IdSpan);
	}

	auto ValidateConnection = [&](const FMatLangConnection& Connection, const FMatLangSourceSpan& OwnerSpan)
	{
		const FMatLangSourceSpan DiagnosticSpan = Connection.TargetSpan.IsValid()
			? Connection.TargetSpan : OwnerSpan;
		if (!Connection.TargetId.StartsWith(TEXT("$")) || Connection.TargetId.Len() <= 1)
		{
			AddError(Result, FilePath, TEXT("ML1003"),
				FString::Printf(TEXT("Connection target '%s' is not a valid expression ID"), *Connection.TargetId),
				DiagnosticSpan);
		}
		else if (!ExpressionsById.Contains(Connection.TargetId))
		{
			AddError(Result, FilePath, TEXT("ML1002"),
				FString::Printf(TEXT("Connection references unknown expression '%s'"), *Connection.TargetId),
				DiagnosticSpan);
		}

		if (Connection.OutputIndex < 0)
		{
			AddError(Result, FilePath, TEXT("ML1005"),
				FString::Printf(TEXT("Output index must be non-negative, got %d"), Connection.OutputIndex),
				Connection.SourceSpan.IsValid() ? Connection.SourceSpan : OwnerSpan);
		}
	};

	for (const TSharedPtr<FMatExpressionAST>& Expression : AST->Expressions)
	{
		if (!Expression.IsValid()) continue;
		for (const FMatLangInput& Input : Expression->Inputs)
		{
			if (Input.IsConnected())
			{
				ValidateConnection(*Input.Connection, Input.SourceSpan.IsValid() ? Input.SourceSpan : Expression->SourceSpan);
			}
		}
	}

	TArray<FString> OutputNames;
	AST->Outputs.Slots.GetKeys(OutputNames);
	OutputNames.Sort();
	for (const FString& OutputName : OutputNames)
	{
		const FMatLangInput* Output = AST->Outputs.Slots.Find(OutputName);
		if (!Output) continue;
		const FMatLangSourceSpan OutputSpan = AST->Outputs.SlotSpans.FindRef(OutputName);
		if (AST->Kind == EMatLangGraphKind::Material && !IsKnownMaterialOutput(OutputName))
		{
			AddError(Result, FilePath, TEXT("ML1103"),
				FString::Printf(TEXT("Unknown material output ':%s'"), *OutputName), OutputSpan);
		}
		if (Output->IsConnected())
		{
			ValidateConnection(*Output->Connection, OutputSpan);
		}
		else if (AST->Kind == EMatLangGraphKind::Material && Output->IsLiteral())
		{
			AddError(Result, FilePath, TEXT("ML1102"),
				FString::Printf(TEXT("Literal value for output ':%s' is not supported by the importer"), *OutputName),
				OutputSpan);
		}
	}

	TMap<FString, uint8> VisitState;
	TFunction<void(const FString&)> VisitExpression = [&](const FString& Id)
	{
		VisitState.Add(Id, 1);
		const TSharedPtr<FMatExpressionAST>* ExpressionPtr = ExpressionsById.Find(Id);
		if (ExpressionPtr && ExpressionPtr->IsValid())
		{
			for (const FMatLangInput& Input : (*ExpressionPtr)->Inputs)
			{
				if (!Input.IsConnected() || !ExpressionsById.Contains(Input.Connection->TargetId)) continue;
				const FString& TargetId = Input.Connection->TargetId;
				const uint8* State = VisitState.Find(TargetId);
				if (State && *State == 1)
				{
					AddError(Result, FilePath, TEXT("ML1004"),
						FString::Printf(TEXT("Connection from '%s' to '%s' creates a cycle"), *Id, *TargetId),
						Input.Connection->SourceSpan.IsValid() ? Input.Connection->SourceSpan : Input.SourceSpan);
				}
				else if (!State)
				{
					VisitExpression(TargetId);
				}
			}
		}
		VisitState.Add(Id, 2);
	};

	for (const TSharedPtr<FMatExpressionAST>& Expression : AST->Expressions)
	{
		if (Expression.IsValid() && ExpressionsById.Contains(Expression->Id) && !VisitState.Contains(Expression->Id))
		{
			VisitExpression(Expression->Id);
		}
	}

	SortDiagnostics(Result.Diagnostics);
	return Result;
}
