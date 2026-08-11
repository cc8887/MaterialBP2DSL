// MatLangTokenizer.cpp - S-expression Lexer Implementation
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangTokenizer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatLangTokenizer, Log, All);

// ========== Token Type Names ==========

FString FMatLangToken::TypeToString(EMatLangTokenType T)
{
	switch (T)
	{
		case EMatLangTokenType::LParen:    return TEXT("LParen");
		case EMatLangTokenType::RParen:    return TEXT("RParen");
		case EMatLangTokenType::LBracket:  return TEXT("LBracket");
		case EMatLangTokenType::RBracket:  return TEXT("RBracket");
		case EMatLangTokenType::String:    return TEXT("String");
		case EMatLangTokenType::Integer:   return TEXT("Integer");
		case EMatLangTokenType::Float:     return TEXT("Float");
		case EMatLangTokenType::Bool:      return TEXT("Bool");
		case EMatLangTokenType::Keyword:   return TEXT("Keyword");
		case EMatLangTokenType::Identifier: return TEXT("Identifier");
		case EMatLangTokenType::Arrow:     return TEXT("Arrow");
		case EMatLangTokenType::Comment:   return TEXT("Comment");
		case EMatLangTokenType::EndOfFile: return TEXT("EOF");
		case EMatLangTokenType::Error:     return TEXT("Error");
		default: return TEXT("Unknown");
	}
}

FString FMatLangToken::ToString() const
{
	return FString::Printf(TEXT("%s(%s) at %d:%d"), *TypeToString(Type), *Value, Line, Column);
}

// ========== Tokenizer ==========

FMatLangTokenizer::FMatLangTokenizer(const FString& Source)
	: Src(*Source), Pos(0), Len(Source.Len()), Line(1), Col(1)
{
}

bool FMatLangTokenizer::Tokenize(const FString& Source, TArray<FMatLangToken>& OutTokens,
	TArray<FMatLangLexError>& OutErrors, bool bKeepComments)
{
	FMatLangTokenizer Lexer(Source);
	
	while (!Lexer.IsAtEnd())
	{
		Lexer.SkipWhitespace();
		if (Lexer.IsAtEnd()) break;
		
		FMatLangToken Token = Lexer.ScanToken(OutErrors);
		
		if (Token.Type == EMatLangTokenType::Comment && !bKeepComments)
		{
			continue;
		}
		if (Token.Type == EMatLangTokenType::Error)
		{
			continue; // Error already recorded
		}
		
		OutTokens.Add(Token);
	}
	
	// Add EOF
	OutTokens.Add(FMatLangToken(EMatLangTokenType::EndOfFile, TEXT(""), Lexer.Line, Lexer.Col,
		Lexer.Pos, 0, Lexer.Line, Lexer.Col));
	
	return OutErrors.Num() == 0;
}

TCHAR FMatLangTokenizer::Peek() const
{
	return IsAtEnd() ? 0 : Src[Pos];
}

TCHAR FMatLangTokenizer::PeekAt(int32 Ahead) const
{
	int32 Idx = Pos + Ahead;
	return (Idx >= 0 && Idx < Len) ? Src[Idx] : 0;
}

TCHAR FMatLangTokenizer::Advance()
{
	TCHAR Ch = Src[Pos++];
	if (Ch == '\n') { Line++; Col = 1; }
	else { Col++; }
	return Ch;
}

bool FMatLangTokenizer::IsAtEnd() const
{
	return Pos >= Len;
}

void FMatLangTokenizer::SkipWhitespace()
{
	while (!IsAtEnd())
	{
		TCHAR Ch = Peek();
		if (Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n')
		{
			Advance();
		}
		else
		{
			break;
		}
	}
}

FMatLangToken FMatLangTokenizer::ScanToken(TArray<FMatLangLexError>& OutErrors)
{
	TCHAR Ch = Peek();
	
	// Structural
	if (Ch == '(') { int32 L = Line, C = Col, O = Pos; Advance(); return MakeToken(EMatLangTokenType::LParen, TEXT("("), L, C, O); }
	if (Ch == ')') { int32 L = Line, C = Col, O = Pos; Advance(); return MakeToken(EMatLangTokenType::RParen, TEXT(")"), L, C, O); }
	if (Ch == '[') { int32 L = Line, C = Col, O = Pos; Advance(); return MakeToken(EMatLangTokenType::LBracket, TEXT("["), L, C, O); }
	if (Ch == ']') { int32 L = Line, C = Col, O = Pos; Advance(); return MakeToken(EMatLangTokenType::RBracket, TEXT("]"), L, C, O); }
	
	// String
	if (Ch == '"') return ScanString(OutErrors);
	
	// Comment
	if (Ch == ';' && PeekAt(1) == ';') return ScanComment();
	
	// Keyword (:name)
	if (Ch == ':') return ScanKeyword(OutErrors);
	
	// Arrow (->) or negative number
	if (Ch == '-')
	{
		if (PeekAt(1) == '>')
		{
			int32 L = Line, C = Col, O = Pos;
			Advance(); Advance();
			return MakeToken(EMatLangTokenType::Arrow, TEXT("->"), L, C, O);
		}
		if (IsDigit(PeekAt(1)))
		{
			return ScanNumber(OutErrors);
		}
	}
	
	// Number
	if (IsDigit(Ch)) return ScanNumber(OutErrors);
	
	// Identifier, Bool, or $id
	if (IsIdentChar(Ch) || Ch == '$') return ScanIdentifierOrBool(OutErrors);
	
	// Unknown character — error
	int32 L = Line, C = Col;
	Advance();
	OutErrors.Add({FString::Printf(TEXT("Unexpected character '%c'"), Ch), L, C, Pos - 1, 1});
	return MakeToken(EMatLangTokenType::Error, FString(1, &Ch), L, C, Pos - 1);
}

FMatLangToken FMatLangTokenizer::ScanString(TArray<FMatLangLexError>& OutErrors)
{
	int32 StartLine = Line, StartCol = Col, StartOffset = Pos;
	Advance(); // consume opening "
	
	FString Value;
	while (!IsAtEnd() && Peek() != '"')
	{
		if (Peek() == '\\')
		{
			Advance(); // consume backslash
			if (IsAtEnd())
			{
				OutErrors.Add({TEXT("Unterminated string escape"), Line, Col, Pos, 1});
				break;
			}
			TCHAR Escaped = Advance();
			switch (Escaped)
			{
				case '"':  Value += '"'; break;
				case '\\': Value += '\\'; break;
				case 'n':  Value += '\n'; break;
				case 't':  Value += '\t'; break;
				default:
					OutErrors.Add({FString::Printf(TEXT("Unknown string escape '\\%c'"), Escaped), Line, Col - 2, Pos - 2, 2});
					Value += Escaped;
					break;
			}
		}
		else
		{
			Value += Advance();
		}
	}
	
	if (IsAtEnd())
	{
		OutErrors.Add({TEXT("Unterminated string literal"), StartLine, StartCol, StartOffset, FMath::Max(1, Pos - StartOffset)});
	}
	else
	{
		Advance(); // consume closing "
	}
	
	return MakeToken(EMatLangTokenType::String, Value, StartLine, StartCol, StartOffset);
}

FMatLangToken FMatLangTokenizer::ScanNumber(TArray<FMatLangLexError>& OutErrors)
{
	int32 StartLine = Line, StartCol = Col, StartOffset = Pos;
	FString Value;
	bool bIsFloat = false;
	
	// Optional negative
	if (Peek() == '-')
	{
		Value += Advance();
	}
	
	// Integer part
	while (!IsAtEnd() && IsDigit(Peek()))
	{
		Value += Advance();
	}
	
	// Decimal part
	if (!IsAtEnd() && Peek() == '.' && IsDigit(PeekAt(1)))
	{
		bIsFloat = true;
		Value += Advance(); // .
		while (!IsAtEnd() && IsDigit(Peek()))
		{
			Value += Advance();
		}
	}
	
	// Exponent
	if (!IsAtEnd() && (Peek() == 'e' || Peek() == 'E'))
	{
		bIsFloat = true;
		const int32 ExponentLine = Line;
		const int32 ExponentCol = Col;
		const int32 ExponentOffset = Pos;
		Value += Advance(); // e/E
		if (!IsAtEnd() && (Peek() == '+' || Peek() == '-'))
		{
			Value += Advance();
		}
		const int32 DigitsStart = Pos;
		while (!IsAtEnd() && IsDigit(Peek()))
		{
			Value += Advance();
		}
		if (DigitsStart == Pos)
		{
			OutErrors.Add({TEXT("Exponent requires at least one digit"),
				ExponentLine, ExponentCol, ExponentOffset, FMath::Max(1, Pos - ExponentOffset)});
		}
	}
	
	return MakeToken(bIsFloat ? EMatLangTokenType::Float : EMatLangTokenType::Integer, Value, StartLine, StartCol, StartOffset);
}

FMatLangToken FMatLangTokenizer::ScanKeyword(TArray<FMatLangLexError>& OutErrors)
{
	int32 StartLine = Line, StartCol = Col, StartOffset = Pos;
	Advance(); // consume :

	// Support :"quoted key with spaces" form
	if (!IsAtEnd() && Peek() == '"')
	{
		Advance(); // consume opening "
		FString Value;
		while (!IsAtEnd() && Peek() != '"')
		{
			if (Peek() == '\\' && PeekAt(1) == '"')
			{
				Advance(); // skip backslash
				Value += Advance();
			}
			else
			{
				Value += Advance();
			}
		}
		if (!IsAtEnd())
		{
			Advance(); // consume closing "
		}
		else
		{
			OutErrors.Add({TEXT("Unterminated quoted keyword"), StartLine, StartCol, StartOffset, FMath::Max(1, Pos - StartOffset)});
		}
		return MakeToken(EMatLangTokenType::Keyword, Value, StartLine, StartCol, StartOffset);
	}

	FString Value;
	while (!IsAtEnd() && (IsIdentChar(Peek()) || IsDigit(Peek())))
	{
		Value += Advance();
	}
	if (Value.IsEmpty())
	{
		OutErrors.Add({TEXT("Expected keyword name after ':'"), StartLine, StartCol, StartOffset, 1});
	}
	
	return MakeToken(EMatLangTokenType::Keyword, Value, StartLine, StartCol, StartOffset);
}

FMatLangToken FMatLangTokenizer::ScanIdentifierOrBool(TArray<FMatLangLexError>& OutErrors)
{
	int32 StartLine = Line, StartCol = Col, StartOffset = Pos;
	FString Value;
	
	// Allow $ prefix for expression IDs
	if (Peek() == '$')
	{
		Value += Advance();
	}
	
	while (!IsAtEnd() && (IsIdentChar(Peek()) || IsDigit(Peek())))
	{
		Value += Advance();
	}
	if (Value == TEXT("$"))
	{
		OutErrors.Add({TEXT("Expression ID must contain a name after '$'"), StartLine, StartCol, StartOffset, 1});
	}
	
	// Check for booleans
	if (Value == TEXT("true") || Value == TEXT("false"))
	{
		return MakeToken(EMatLangTokenType::Bool, Value, StartLine, StartCol, StartOffset);
	}
	
	return MakeToken(EMatLangTokenType::Identifier, Value, StartLine, StartCol, StartOffset);
}

FMatLangToken FMatLangTokenizer::ScanComment()
{
	int32 StartLine = Line, StartCol = Col, StartOffset = Pos;
	Advance(); Advance(); // consume ;;
	
	FString Value;
	while (!IsAtEnd() && Peek() != '\n')
	{
		Value += Advance();
	}
	
	return MakeToken(EMatLangTokenType::Comment, Value.TrimStart(), StartLine, StartCol, StartOffset);
}

FMatLangToken FMatLangTokenizer::MakeToken(EMatLangTokenType Type, const FString& Value, int32 StartLine, int32 StartCol, int32 StartOffset) const
{
	return FMatLangToken(Type, Value, StartLine, StartCol, StartOffset, Pos - StartOffset, Line, Col);
}

bool FMatLangTokenizer::IsIdentChar(TCHAR Ch) const
{
	return FChar::IsAlpha(Ch) || Ch == '-' || Ch == '_' || Ch == '.' || Ch == '/';
}

bool FMatLangTokenizer::IsDigit(TCHAR Ch) const
{
	return Ch >= '0' && Ch <= '9';
}
