// MatLangParser.cpp - S-expression Parser for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatLangParser, Log, All);

static const FMatLangToken GEOFToken(EMatLangTokenType::EndOfFile, TEXT(""), 0, 0, 0);

// ========== Construction ==========

FMatLangParser::FMatLangParser(const TArray<FMatLangToken>& InTokens, TArray<FMatLangParseError>& InErrors)
	: Tokens(InTokens), Pos(0), Errors(InErrors)
{
}

// ========== Public API ==========

TSharedPtr<FMaterialGraphAST> FMatLangParser::Parse(const FString& Source, TArray<FMatLangParseError>& OutErrors)
{
	TArray<FMatLangToken> Tokens;
	TArray<FMatLangLexError> LexErrors;
	
	FMatLangTokenizer::Tokenize(Source, Tokens, LexErrors);
	
	for (const auto& Err : LexErrors)
	{
		OutErrors.Add({Err.Message, Err.Line, Err.Column});
	}
	
	if (Tokens.Num() == 0)
	{
		OutErrors.Add({TEXT("Empty source"), 1, 1});
		return nullptr;
	}
	
	return ParseTokens(Tokens, OutErrors);
}

TSharedPtr<FMaterialGraphAST> FMatLangParser::ParseTokens(const TArray<FMatLangToken>& Tokens, TArray<FMatLangParseError>& OutErrors)
{
	FMatLangParser Parser(Tokens, OutErrors);
	return Parser.ParseProgram();
}

// ========== Token Access ==========

const FMatLangToken& FMatLangParser::Current() const
{
	return (Pos < Tokens.Num()) ? Tokens[Pos] : GEOFToken;
}

const FMatLangToken& FMatLangParser::Peek(int32 Ahead) const
{
	int32 Idx = Pos + Ahead;
	return (Idx >= 0 && Idx < Tokens.Num()) ? Tokens[Idx] : GEOFToken;
}

const FMatLangToken& FMatLangParser::Advance()
{
	if (Pos < Tokens.Num())
	{
		return Tokens[Pos++];
	}
	return GEOFToken;
}

bool FMatLangParser::IsAtEnd() const
{
	return Pos >= Tokens.Num() || Current().Type == EMatLangTokenType::EndOfFile;
}

bool FMatLangParser::Check(EMatLangTokenType Type) const
{
	return Current().Type == Type;
}

bool FMatLangParser::CheckValue(EMatLangTokenType Type, const FString& Value) const
{
	return Current().Type == Type && Current().Value == Value;
}

bool FMatLangParser::Match(EMatLangTokenType Type)
{
	if (Check(Type))
	{
		Advance();
		return true;
	}
	return false;
}

bool FMatLangParser::Expect(EMatLangTokenType Type, const FString& Context)
{
	if (Check(Type))
	{
		Advance();
		return true;
	}
	Error(FString::Printf(TEXT("Expected %s in %s, got %s('%s')"),
		*FMatLangToken::TypeToString(Type), *Context,
		*FMatLangToken::TypeToString(Current().Type), *Current().Value));
	return false;
}

// ========== Error Handling ==========

void FMatLangParser::Error(const FString& Message)
{
	ErrorAt(Current(), Message);
}

void FMatLangParser::ErrorAt(const FMatLangToken& Token, const FString& Message)
{
	Errors.Add({Message, Token.Line, Token.Column});
	UE_LOG(LogMatLangParser, Warning, TEXT("Parse error at %d:%d: %s"), Token.Line, Token.Column, *Message);
}

void FMatLangParser::Synchronize()
{
	// Skip to next top-level construct
	int32 Depth = 0;
	while (!IsAtEnd())
	{
		if (Check(EMatLangTokenType::LParen)) Depth++;
		if (Check(EMatLangTokenType::RParen))
		{
			if (Depth <= 0) return;
			Depth--;
		}
		Advance();
	}
}

// ========== Parsing Rules ==========

TSharedPtr<FMaterialGraphAST> FMatLangParser::ParseProgram()
{
	// (material "Name" ...)
	if (!Expect(EMatLangTokenType::LParen, TEXT("program"))) return nullptr;
	
	if (!CheckValue(EMatLangTokenType::Identifier, TEXT("material")))
	{
		Error(TEXT("Expected 'material' keyword"));
		return nullptr;
	}
	Advance();
	
	// Material name
	auto AST = MakeShared<FMaterialGraphAST>();
	if (Check(EMatLangTokenType::String))
	{
		AST->Name = Current().Value;
		Advance();
	}
	else
	{
		Error(TEXT("Expected material name string"));
		return nullptr;
	}
	
	// Parse top-level entries until closing paren
	while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
	{
		ParseTopLevel(AST);
	}
	
	Expect(EMatLangTokenType::RParen, TEXT("material"));
	return AST;
}

void FMatLangParser::ParseTopLevel(TSharedPtr<FMaterialGraphAST> AST)
{
	// Keywords: :domain, :blend-mode, :shading-model, etc.
	if (Check(EMatLangTokenType::Keyword))
	{
		FString Key = Current().Value;
		Advance();
		
		if (Key == TEXT("domain"))
		{
			if (Check(EMatLangTokenType::Identifier))
			{
				AST->Domain = MatLangEnums::StringToDomain(Current().Value);
				Advance();
			}
		}
		else if (Key == TEXT("blend-mode"))
		{
			if (Check(EMatLangTokenType::Identifier))
			{
				AST->BlendMode = MatLangEnums::StringToBlendMode(Current().Value);
				Advance();
			}
		}
		else if (Key == TEXT("shading-model"))
		{
			if (Check(EMatLangTokenType::Identifier))
			{
				AST->ShadingModel = MatLangEnums::StringToShadingModel(Current().Value);
				Advance();
			}
		}
		else if (Key == TEXT("two-sided"))
		{
			if (Check(EMatLangTokenType::Bool))
			{
				AST->bTwoSided = (Current().Value == TEXT("true"));
				Advance();
			}
		}
		else if (Key == TEXT("opacity-mask-clip-value"))
		{
			if (Check(EMatLangTokenType::Float) || Check(EMatLangTokenType::Integer))
			{
				AST->bIsMasked = true;
				AST->OpacityMaskClipValue = FCString::Atof(*Current().Value);
				Advance();
			}
		}
		else if (Key == TEXT("parameters"))
		{
			ParseParameters(AST);
		}
		else
		{
			// Generic extra property
			FString Val = ParseValue();
			AST->ExtraProperties.Add(Key, Val);
		}
		return;
	}
	
	// Parenthesized blocks: (expressions ...) or (outputs ...)
	if (Check(EMatLangTokenType::LParen))
	{
		// Peek at the identifier after '('
		if (Peek(1).Type == EMatLangTokenType::Identifier)
		{
			FString BlockType = Peek(1).Value;
			
			if (BlockType == TEXT("expressions"))
			{
				ParseExpressions(AST);
				return;
			}
			if (BlockType == TEXT("outputs"))
			{
				ParseOutputs(AST);
				return;
			}
		}
		
		// Unknown parenthesized block — skip
		Error(FString::Printf(TEXT("Unknown block at top level")));
		Synchronize();
		return;
	}
	
	// Unexpected token
	Error(FString::Printf(TEXT("Unexpected token '%s' at top level"), *Current().Value));
	Advance();
}

void FMatLangParser::ParseExpressions(TSharedPtr<FMaterialGraphAST> AST)
{
	// (expressions expr1 expr2 ...)
	Expect(EMatLangTokenType::LParen, TEXT("expressions block"));
	
	if (!CheckValue(EMatLangTokenType::Identifier, TEXT("expressions")))
	{
		Error(TEXT("Expected 'expressions' keyword"));
		Synchronize();
		return;
	}
	Advance();
	
	while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
	{
		auto Expr = ParseExprDef();
		if (Expr)
		{
			AST->Expressions.Add(Expr);
		}
		else
		{
			Synchronize();
		}
	}
	
	Expect(EMatLangTokenType::RParen, TEXT("expressions block"));
}

TSharedPtr<FMatExpressionAST> FMatLangParser::ParseExprDef()
{
	// (expr-type $id :prop val :input (connect $other 0) ...)
	if (!Expect(EMatLangTokenType::LParen, TEXT("expression"))) return nullptr;
	
	auto Expr = MakeShared<FMatExpressionAST>();
	
	// Expression type
	if (Check(EMatLangTokenType::Identifier))
	{
		Expr->ExprType = Current().Value;
		Advance();
	}
	else
	{
		Error(TEXT("Expected expression type"));
		return nullptr;
	}
	
	// Expression ID ($xxx)
	if (Check(EMatLangTokenType::Identifier) && Current().IsExprId())
	{
		Expr->Id = Current().Value;
		Advance();
	}
	else
	{
		Error(TEXT("Expected expression ID (e.g. $tex1)"));
		return nullptr;
	}
	
	// Properties and inputs
	while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
	{
		if (Check(EMatLangTokenType::Keyword))
		{
			FString Key = Current().Value;
			Advance();
			
			// Check if the value is a (connect ...) — then it's an input
			if (Check(EMatLangTokenType::LParen) && Peek(1).Type == EMatLangTokenType::Identifier 
				&& Peek(1).Value == TEXT("connect"))
			{
				FMatLangConnection Conn = ParseConnect();
				Expr->AddInput(Key, Conn);
			}
			else
			{
				// It's a property or literal input
				FString Val = ParseValue();
				
				// Heuristic: if key looks like an input name and value is numeric,
				// treat as literal input; otherwise treat as property
				// For simplicity: everything goes into Properties; importer will decide
				Expr->Properties.Add(Key, Val);
			}
		}
		else
		{
			Error(FString::Printf(TEXT("Expected keyword property in expression '%s', got '%s'"), 
				*Expr->Id, *Current().Value));
			Advance();
		}
	}
	
	Expect(EMatLangTokenType::RParen, TEXT("expression"));
	return Expr;
}

void FMatLangParser::ParseOutputs(TSharedPtr<FMaterialGraphAST> AST)
{
	// (outputs :base-color (connect $mul1 0) :metallic 0.5 ...)
	Expect(EMatLangTokenType::LParen, TEXT("outputs block"));
	
	if (!CheckValue(EMatLangTokenType::Identifier, TEXT("outputs")))
	{
		Error(TEXT("Expected 'outputs' keyword"));
		Synchronize();
		return;
	}
	Advance();
	
	while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
	{
		if (Check(EMatLangTokenType::Keyword))
		{
			FString SlotName = Current().Value;
			Advance();
			
			FMatLangInput Input;
			Input.Name = SlotName;
			
			if (Check(EMatLangTokenType::LParen) && Peek(1).Type == EMatLangTokenType::Identifier
				&& Peek(1).Value == TEXT("connect"))
			{
				Input.Connection = ParseConnect();
			}
			else
			{
				Input.LiteralValue = ParseValue();
			}
			
			AST->Outputs.Slots.Add(SlotName, MoveTemp(Input));
		}
		else
		{
			Error(FString::Printf(TEXT("Expected keyword in outputs, got '%s'"), *Current().Value));
			Advance();
		}
	}
	
	Expect(EMatLangTokenType::RParen, TEXT("outputs block"));
}

void FMatLangParser::ParseParameters(TSharedPtr<FMaterialGraphAST> AST)
{
	// :parameters [ (scalar :name "Metallic" :default 0.5) ... ]
	if (!Expect(EMatLangTokenType::LBracket, TEXT("parameters"))) return;
	
	while (!IsAtEnd() && !Check(EMatLangTokenType::RBracket))
	{
		if (Check(EMatLangTokenType::LParen))
		{
			Advance();
			
			FMatParameterDef Param;
			if (Check(EMatLangTokenType::Identifier))
			{
				Param.Type = Current().Value;
				Advance();
			}
			
			// Parse key-value properties
			while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
			{
				if (Check(EMatLangTokenType::Keyword))
				{
					FString Key = Current().Value;
					Advance();
					FString Val = ParseValue();
					
					if (Key == TEXT("name")) Param.Name = Val;
					else if (Key == TEXT("group")) Param.Group = Val;
					else if (Key == TEXT("default")) Param.DefaultValue = Val;
					else if (Key == TEXT("sort-priority"))
					{
						Param.SortPriority = FCString::Atoi(*Val);
					}
				}
				else
				{
					Advance();
				}
			}
			
			Expect(EMatLangTokenType::RParen, TEXT("parameter def"));
			AST->Parameters.Add(MoveTemp(Param));
		}
		else
		{
			Advance();
		}
	}
	
	Expect(EMatLangTokenType::RBracket, TEXT("parameters"));
}

FMatLangConnection FMatLangParser::ParseConnect()
{
	// (connect $target-id output-index?)
	Expect(EMatLangTokenType::LParen, TEXT("connect"));
	
	if (!CheckValue(EMatLangTokenType::Identifier, TEXT("connect")))
	{
		Error(TEXT("Expected 'connect' keyword"));
		Synchronize();
		return FMatLangConnection();
	}
	Advance();
	
	FMatLangConnection Conn;
	
	// Target ID
	if (Check(EMatLangTokenType::Identifier))
	{
		Conn.TargetId = Current().Value;
		Advance();
	}
	else
	{
		Error(TEXT("Expected expression ID in connect"));
	}
	
	// Optional output index (default 0)
	if (Check(EMatLangTokenType::Integer))
	{
		Conn.OutputIndex = FCString::Atoi(*Current().Value);
		Advance();
	}
	
	Expect(EMatLangTokenType::RParen, TEXT("connect"));
	return Conn;
}

FString FMatLangParser::ParseValue()
{
	// String
	if (Check(EMatLangTokenType::String))
	{
		// Current().Value is the already-unescaped content (Tokenizer strips outer quotes
		// and resolves \" -> ").  We must re-escape when serialising back to DSL so that
		// the round-trip is stable: \ -> \\, " -> \"
		FString Raw = Current().Value;
		Raw = Raw.Replace(TEXT("\\"), TEXT("\\\\"));
		Raw = Raw.Replace(TEXT("\""), TEXT("\\\""));
		FString Val = FString::Printf(TEXT("\"%s\""), *Raw);
		Advance();
		return Val;
	}
	
	// Numbers, bools
	if (Check(EMatLangTokenType::Integer) || Check(EMatLangTokenType::Float) || Check(EMatLangTokenType::Bool))
	{
		FString Val = Current().Value;
		Advance();
		return Val;
	}
	
	// Identifier
	if (Check(EMatLangTokenType::Identifier))
	{
		FString Val = Current().Value;
		Advance();
		return Val;
	}
	
	// (asset "path") or (connect ...) or (vector x y z w) etc.
	if (Check(EMatLangTokenType::LParen))
	{
		int32 Depth = 0;
		FString Val;
		bool bNeedSpace = false;
		while (!IsAtEnd())
		{
			if (Check(EMatLangTokenType::LParen))
			{
				Depth++;
				if (bNeedSpace) Val += TEXT(" ");
				Val += TEXT("(");
				bNeedSpace = false;
				Advance();
				continue;
			}
			if (Check(EMatLangTokenType::RParen))
			{
				Depth--;
				if (Depth < 0)
				{
					Error(TEXT("Unmatched closing paren in value"));
					break;
				}
				Val += TEXT(")");
				Advance();
				if (Depth == 0) break;
				bNeedSpace = true;
				continue;
			}
			
			if (bNeedSpace) Val += TEXT(" ");
			bNeedSpace = true;
			
			if (Current().Type == EMatLangTokenType::String)
			{
				// Re-escape the unescaped token value so the round-trip is stable
				FString RawStr = Current().Value;
				RawStr = RawStr.Replace(TEXT("\\"), TEXT("\\\\"));
				RawStr = RawStr.Replace(TEXT("\""), TEXT("\\\""));
				Val += FString::Printf(TEXT("\"%s\""), *RawStr);
			}
			else if (Current().Type == EMatLangTokenType::Keyword)
			{
				Val += FString::Printf(TEXT(":%s"), *Current().Value);
			}
			else
			{
				Val += Current().Value;
			}
			Advance();
		}
		return Val;
	}
	
	// [...] list
	if (Check(EMatLangTokenType::LBracket))
	{
		FString Val = TEXT("[");
		Advance();
		bool bFirst = true;
		while (!IsAtEnd() && !Check(EMatLangTokenType::RBracket))
		{
			if (!bFirst) Val += TEXT(" ");
			Val += ParseValue();
			bFirst = false;
		}
		Expect(EMatLangTokenType::RBracket, TEXT("list value"));
		Val += TEXT("]");
		return Val;
	}
	
	Error(FString::Printf(TEXT("Expected value, got '%s'"), *Current().Value));
	Advance();
	return TEXT("");
}

bool FMatLangParser::IsValueStart() const
{
	return Check(EMatLangTokenType::String)
		|| Check(EMatLangTokenType::Integer)
		|| Check(EMatLangTokenType::Float)
		|| Check(EMatLangTokenType::Bool)
		|| Check(EMatLangTokenType::Identifier)
		|| Check(EMatLangTokenType::LParen)
		|| Check(EMatLangTokenType::LBracket);
}
