// MatLangParser.h - S-expression Parser for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"
#include "MatLangTokenizer.h"

/**
 * Parse error with source location
 */
struct MATBP2FP_API FMatLangParseError
{
	FString Message;
	int32 Line;
	int32 Column;
	
	FString ToString() const
	{
		return FString::Printf(TEXT("Parse error at %d:%d: %s"), Line, Column, *Message);
	}
};

/**
 * MatLang S-expression Parser
 * 
 * Grammar (informal):
 *   Program     ::= '(' 'material' STRING TopLevel* ')'
 *   TopLevel    ::= ':domain' IDENT
 *                 | ':blend-mode' IDENT
 *                 | ':shading-model' IDENT
 *                 | ':two-sided' BOOL
 *                 | ':parameters' '[' ParamDef* ']'
 *                 | '(' 'expressions' ExprDef* ')'
 *                 | '(' 'outputs' OutputDef* ')'
 *                 | ':' KEY Value
 *   ExprDef     ::= '(' ExprType '$'ID Property* ')'
 *   Property    ::= ':' KEY Value
 *   Value       ::= STRING | NUMBER | BOOL | '(' 'connect' '$'ID INT? ')' | '(' 'asset' STRING ')' | '[' Value* ']' | IDENT
 *   OutputDef   ::= ':' KEY Value
 *   ParamDef    ::= '(' ParamType ':' NAME ... ')'
 */
class MATBP2FP_API FMatLangParser
{
public:
	static TSharedPtr<FMaterialGraphAST> Parse(const FString& Source, TArray<FMatLangParseError>& OutErrors);
	static TSharedPtr<FMaterialGraphAST> ParseTokens(const TArray<FMatLangToken>& Tokens, TArray<FMatLangParseError>& OutErrors);

private:
	const TArray<FMatLangToken>& Tokens;
	int32 Pos;
	TArray<FMatLangParseError>& Errors;
	
	FMatLangParser(const TArray<FMatLangToken>& InTokens, TArray<FMatLangParseError>& InErrors);
	
	// Token access
	const FMatLangToken& Current() const;
	const FMatLangToken& Peek(int32 Ahead = 0) const;
	const FMatLangToken& Advance();
	bool IsAtEnd() const;
	bool Check(EMatLangTokenType Type) const;
	bool CheckValue(EMatLangTokenType Type, const FString& Value) const;
	bool Match(EMatLangTokenType Type);
	bool Expect(EMatLangTokenType Type, const FString& Context);
	
	// Error handling
	void Error(const FString& Message);
	void ErrorAt(const FMatLangToken& Token, const FString& Message);
	void Synchronize();
	
	// Parsing rules
	TSharedPtr<FMaterialGraphAST> ParseProgram();
	void ParseTopLevel(TSharedPtr<FMaterialGraphAST> AST);
	void ParseExpressions(TSharedPtr<FMaterialGraphAST> AST);
	TSharedPtr<FMatExpressionAST> ParseExprDef();
	void ParseOutputs(TSharedPtr<FMaterialGraphAST> AST);
	void ParseParameters(TSharedPtr<FMaterialGraphAST> AST);
	FString ParseValue();           // Literal, (connect ...), (asset ...), [...]
	FMatLangConnection ParseConnect();  // (connect $id output-index?)
	
	// Helpers
	bool IsValueStart() const;
};
