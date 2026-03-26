// MatLangTokenizer.h - S-expression Lexer for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Token types — same S-expression syntax as AnimLang
 * Extended with '$' prefix for expression IDs ($tex1, $mul1)
 */
enum class EMatLangTokenType : uint8
{
	// Structural
	LParen,        // (
	RParen,        // )
	LBracket,      // [
	RBracket,      // ]
	
	// Literals
	String,        // "hello"
	Integer,       // 123, -5
	Float,         // 0.5, -3.14
	Bool,          // true, false
	
	// Identifiers & Keywords
	Keyword,       // :name, :loop, :initial
	Identifier,    // texture-sample, multiply, $tex1
	
	// Special
	Arrow,         // ->
	
	// Meta
	Comment,       // ;; ...
	EndOfFile,
	Error          // Lexer error
};

/**
 * Single Token
 */
struct MATBP2FP_API FMatLangToken
{
	EMatLangTokenType Type;
	FString Value;        // Raw token value (without quotes for strings, without : for keywords)
	int32 Line;           // 1-based line number
	int32 Column;         // 1-based column number
	int32 Offset;         // 0-based character offset in source
	
	FMatLangToken()
		: Type(EMatLangTokenType::Error), Line(0), Column(0), Offset(0)
	{
	}
	
	FMatLangToken(EMatLangTokenType InType, const FString& InValue, int32 InLine, int32 InCol, int32 InOffset)
		: Type(InType), Value(InValue), Line(InLine), Column(InCol), Offset(InOffset)
	{
	}
	
	static FString TypeToString(EMatLangTokenType T);
	FString ToString() const;
	
	bool IsLiteral() const
	{
		return Type == EMatLangTokenType::String
			|| Type == EMatLangTokenType::Integer
			|| Type == EMatLangTokenType::Float
			|| Type == EMatLangTokenType::Bool;
	}
	
	/** Check if this identifier is an expression ID (starts with $) */
	bool IsExprId() const
	{
		return Type == EMatLangTokenType::Identifier && Value.StartsWith(TEXT("$"));
	}
};

/**
 * Lexer error
 */
struct FMatLangLexError
{
	FString Message;
	int32 Line;
	int32 Column;
	
	FString ToString() const
	{
		return FString::Printf(TEXT("Lex error at %d:%d: %s"), Line, Column, *Message);
	}
};

/**
 * MatLang S-expression Tokenizer
 * 
 * Same syntax as AnimLang:
 *   ( ) [ ] "string" 123 0.5 true false :keyword identifier ;; comment
 * Extended:
 *   $identifier — expression IDs (treated as regular identifiers)
 */
class MATBP2FP_API FMatLangTokenizer
{
public:
	static bool Tokenize(const FString& Source, TArray<FMatLangToken>& OutTokens, TArray<FMatLangLexError>& OutErrors, bool bKeepComments = false);

private:
	const TCHAR* Src;
	int32 Pos;
	int32 Len;
	int32 Line;
	int32 Col;
	
	FMatLangTokenizer(const FString& Source);
	
	TCHAR Peek() const;
	TCHAR PeekAt(int32 Ahead) const;
	TCHAR Advance();
	bool IsAtEnd() const;
	void SkipWhitespace();
	
	FMatLangToken ScanToken(TArray<FMatLangLexError>& OutErrors);
	FMatLangToken ScanString(TArray<FMatLangLexError>& OutErrors);
	FMatLangToken ScanNumber();
	FMatLangToken ScanKeyword();
	FMatLangToken ScanIdentifierOrBool();
	FMatLangToken ScanComment();
	
	FMatLangToken MakeToken(EMatLangTokenType Type, const FString& Value, int32 StartLine, int32 StartCol, int32 StartOffset) const;
	bool IsIdentChar(TCHAR Ch) const;
	bool IsDigit(TCHAR Ch) const;
};
