// MatLangParser.cpp - S-expression Parser for MatLang DSL
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatLangParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogMatLangParser, Log, All);

static const FMatLangToken GEOFToken(EMatLangTokenType::EndOfFile, TEXT(""), 0, 0, 0);

namespace
{
	FMatLangSourceSpan SpanForToken(const FMatLangToken& Token)
	{
		FMatLangSourceSpan Span;
		Span.StartLine = Token.Line;
		Span.StartColumn = Token.Column;
		Span.EndLine = Token.EndLine;
		Span.EndColumn = Token.EndColumn;
		Span.StartOffset = Token.Offset;
		Span.EndOffset = Token.Offset + Token.Length;
		return Span;
	}

	FMatLangSourceSpan SpanForTokens(const FMatLangToken& Start, const FMatLangToken& End)
	{
		FMatLangSourceSpan Span = SpanForToken(Start);
		Span.EndLine = End.EndLine;
		Span.EndColumn = End.EndColumn;
		Span.EndOffset = End.Offset + End.Length;
		return Span;
	}
}

// ========== Construction ==========

FMatLangParser::FMatLangParser(const TArray<FMatLangToken>& InTokens, TArray<FMatLangParseError>& InErrors)
	: Tokens(InTokens), Pos(0), Errors(InErrors)
{
}

// ========== Public API ==========

bool FMatLangParseResult::HasErrors() const
{
	for (const FMatLangDiagnostic& Diagnostic : Diagnostics)
	{
		if (Diagnostic.Severity == EMatLangDiagnosticSeverity::Error)
		{
			return true;
		}
	}
	return false;
}

FMatLangParseResult FMatLangParser::ParseDocument(const FString& Source, const FString& FilePath)
{
	FMatLangParseResult Result;
	TArray<FMatLangParseError> ParseErrors;
	Result.AST = Parse(Source, ParseErrors);

	for (const FMatLangParseError& Error : ParseErrors)
	{
		FMatLangDiagnostic Diagnostic;
		Diagnostic.RuleId = Error.RuleId;
		Diagnostic.Severity = EMatLangDiagnosticSeverity::Error;
		Diagnostic.Message = Error.Message;
		Diagnostic.FilePath = FilePath;
		Diagnostic.Span = Error.Span;
		if (!Diagnostic.Span.IsValid())
		{
			Diagnostic.Span.StartLine = Error.Line;
			Diagnostic.Span.StartColumn = Error.Column;
			Diagnostic.Span.EndLine = Error.Line;
			Diagnostic.Span.EndColumn = Error.Column + 1;
		}
		Result.Diagnostics.Add(MoveTemp(Diagnostic));
	}

	return Result;
}

TSharedPtr<FMaterialGraphAST> FMatLangParser::Parse(const FString& Source, TArray<FMatLangParseError>& OutErrors)
{
	TArray<FMatLangToken> Tokens;
	TArray<FMatLangLexError> LexErrors;
	
	FMatLangTokenizer::Tokenize(Source, Tokens, LexErrors);
	
	for (const auto& Err : LexErrors)
	{
		FMatLangParseError ParseError;
		ParseError.Message = Err.Message;
		ParseError.Line = Err.Line;
		ParseError.Column = Err.Column;
		ParseError.RuleId = TEXT("ML0001");
		ParseError.Span.StartLine = Err.Line;
		ParseError.Span.StartColumn = Err.Column;
		ParseError.Span.EndLine = Err.Line;
		ParseError.Span.EndColumn = Err.Column + FMath::Max(1, Err.Length);
		ParseError.Span.StartOffset = Err.Offset;
		ParseError.Span.EndOffset = Err.Offset == INDEX_NONE ? INDEX_NONE : Err.Offset + FMath::Max(1, Err.Length);
		OutErrors.Add(MoveTemp(ParseError));
	}
	
	if (Tokens.Num() == 0 || Tokens[0].Type == EMatLangTokenType::EndOfFile)
	{
		FMatLangParseError Error;
		Error.Message = TEXT("Empty source");
		Error.Line = 1;
		Error.Column = 1;
		Error.RuleId = TEXT("ML0002");
		Error.Span.StartLine = Error.Span.EndLine = 1;
		Error.Span.StartColumn = Error.Span.EndColumn = 1;
		Error.Span.StartOffset = Error.Span.EndOffset = 0;
		OutErrors.Add(MoveTemp(Error));
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
	ErrorAt(Token, TEXT("ML0002"), Message);
}

void FMatLangParser::ErrorAt(const FMatLangToken& Token, const FString& RuleId, const FString& Message)
{
	FMatLangParseError Error;
	Error.Message = Message;
	Error.Line = Token.Line;
	Error.Column = Token.Column;
	Error.RuleId = RuleId;
	Error.Span = SpanForToken(Token);
	Errors.Add(MoveTemp(Error));
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
	const FMatLangToken StartToken = Current();
	if (!Expect(EMatLangTokenType::LParen, TEXT("program"))) return nullptr;
	
	if (!CheckValue(EMatLangTokenType::Identifier, TEXT("material")) &&
		!CheckValue(EMatLangTokenType::Identifier, TEXT("material-function")))
	{
		Error(TEXT("Expected 'material' or 'material-function' keyword"));
		return nullptr;
	}
	const bool bIsMaterialFunction = Current().Value == TEXT("material-function");
	Advance();
	
	// Material name
	auto AST = MakeShared<FMaterialGraphAST>();
	AST->Kind = bIsMaterialFunction
		? EMatLangGraphKind::MaterialFunction
		: EMatLangGraphKind::Material;
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
	
	const FMatLangToken EndToken = Current();
	if (Expect(EMatLangTokenType::RParen, TEXT("material")))
	{
		AST->SourceSpan = SpanForTokens(StartToken, EndToken);
	}

	if (!IsAtEnd())
	{
		ErrorAt(Current(), TEXT("ML0002"), TEXT("Unexpected tokens after the material definition"));
	}
	return AST;
}

void FMatLangParser::ParseTopLevel(TSharedPtr<FMaterialGraphAST> AST)
{
	// Keywords: :domain, :blend-mode, :shading-model, etc.
	if (Check(EMatLangTokenType::Keyword))
	{
		const FMatLangToken KeyToken = Current();
		FString Key = KeyToken.Value;
		Advance();

		if (SeenTopLevelKeys.Contains(Key))
		{
			ErrorAt(KeyToken, TEXT("ML1201"),
				FString::Printf(TEXT("Duplicate top-level property ':%s'"), *Key));
		}
		SeenTopLevelKeys.Add(Key);
		
		if (Key == TEXT("asset-path"))
		{
			if (Check(EMatLangTokenType::String))
			{
				AST->AssetPath = Current().Value;
				Advance();
			}
			else
			{
				Error(TEXT("Expected string value for :asset-path"));
			}
		}
		else if (Key == TEXT("domain"))
		{
			if (Check(EMatLangTokenType::Identifier))
			{
				EMatLangDomain Domain;
				if (MatLangEnums::TryStringToDomain(Current().Value, Domain))
				{
					AST->Domain = Domain;
				}
				else
				{
					ErrorAt(Current(), TEXT("ML1101"),
						FString::Printf(TEXT("Unknown material domain '%s'"), *Current().Value));
				}
				Advance();
			}
			else
			{
				Error(TEXT("Expected material domain identifier"));
			}
		}
		else if (Key == TEXT("blend-mode"))
		{
			if (Check(EMatLangTokenType::Identifier))
			{
				EMatLangBlendMode BlendMode;
				if (MatLangEnums::TryStringToBlendMode(Current().Value, BlendMode))
				{
					AST->BlendMode = BlendMode;
				}
				else
				{
					ErrorAt(Current(), TEXT("ML1101"),
						FString::Printf(TEXT("Unknown blend mode '%s'"), *Current().Value));
				}
				Advance();
			}
			else
			{
				Error(TEXT("Expected blend mode identifier"));
			}
		}
		else if (Key == TEXT("shading-model"))
		{
			if (Check(EMatLangTokenType::Identifier))
			{
				EMatLangShadingModel ShadingModel;
				if (MatLangEnums::TryStringToShadingModel(Current().Value, ShadingModel))
				{
					AST->ShadingModel = ShadingModel;
				}
				else
				{
					ErrorAt(Current(), TEXT("ML1101"),
						FString::Printf(TEXT("Unknown shading model '%s'"), *Current().Value));
				}
				Advance();
			}
			else
			{
				Error(TEXT("Expected shading model identifier"));
			}
		}
		else if (Key == TEXT("two-sided"))
		{
			if (Check(EMatLangTokenType::Bool))
			{
				AST->bTwoSided = (Current().Value == TEXT("true"));
				Advance();
			}
			else
			{
				Error(TEXT("Expected boolean value for :two-sided"));
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
			else
			{
				Error(TEXT("Expected numeric value for :opacity-mask-clip-value"));
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
			AST->ExtraPropertySpans.Add(Key, SpanForToken(KeyToken));
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
				if (SeenTopLevelBlocks.Contains(BlockType))
				{
					ErrorAt(Peek(1), TEXT("ML1201"), TEXT("Duplicate expressions block"));
				}
				SeenTopLevelBlocks.Add(BlockType);
				ParseExpressions(AST);
				return;
			}
			if (BlockType == TEXT("outputs"))
			{
				if (SeenTopLevelBlocks.Contains(BlockType))
				{
					ErrorAt(Peek(1), TEXT("ML1201"), TEXT("Duplicate outputs block"));
				}
				SeenTopLevelBlocks.Add(BlockType);
				ParseOutputs(AST);
				return;
			}
			if (BlockType == TEXT("function-inputs") || BlockType == TEXT("function-outputs"))
			{
				if (AST->Kind != EMatLangGraphKind::MaterialFunction)
				{
					ErrorAt(Peek(1), TEXT("ML1202"),
						TEXT("Function signature blocks are only valid in material-function documents"));
				}
				if (SeenTopLevelBlocks.Contains(BlockType))
				{
					ErrorAt(Peek(1), TEXT("ML1201"),
						FString::Printf(TEXT("Duplicate %s block"), *BlockType));
				}
				SeenTopLevelBlocks.Add(BlockType);
				ParseFunctionParameters(AST, BlockType == TEXT("function-inputs"));
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
	const FMatLangToken StartToken = Current();
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
		Expr->IdSpan = SpanForToken(Current());
		Advance();
	}
	else
	{
		Error(TEXT("Expected expression ID (e.g. $tex1)"));
		return nullptr;
	}
	
	// Properties and inputs
	TSet<FString> SeenKeys;
	while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
	{
		if (Check(EMatLangTokenType::Keyword))
		{
			const FMatLangToken KeyToken = Current();
			FString Key = KeyToken.Value;
			Advance();

			if (SeenKeys.Contains(Key))
			{
				ErrorAt(KeyToken, TEXT("ML1201"),
					FString::Printf(TEXT("Duplicate key ':%s' in expression '%s'"), *Key, *Expr->Id));
			}
			SeenKeys.Add(Key);
			
			// Check if the value is a (connect ...) — then it's an input
			if (Check(EMatLangTokenType::LParen) && Peek(1).Type == EMatLangTokenType::Identifier 
				&& Peek(1).Value == TEXT("connect"))
			{
				FMatLangConnection Conn = ParseConnect();
				FMatLangInput Input;
				Input.Name = Key;
				Input.Connection = Conn;
				Input.SourceSpan = Conn.SourceSpan;
				Input.SourceSpan.StartLine = KeyToken.Line;
				Input.SourceSpan.StartColumn = KeyToken.Column;
				Input.SourceSpan.StartOffset = KeyToken.Offset;
				Expr->Inputs.Add(MoveTemp(Input));
			}
			else
			{
				// It's a property or literal input
				FString Val = ParseValue();
				
				// Heuristic: if key looks like an input name and value is numeric,
				// treat as literal input; otherwise treat as property
				// For simplicity: everything goes into Properties; importer will decide
				Expr->Properties.Add(Key, Val);
				Expr->PropertySpans.Add(Key, SpanForToken(KeyToken));
			}
		}
		else
		{
			Error(FString::Printf(TEXT("Expected keyword property in expression '%s', got '%s'"), 
				*Expr->Id, *Current().Value));
			Advance();
		}
	}
	
	const FMatLangToken EndToken = Current();
	if (Expect(EMatLangTokenType::RParen, TEXT("expression")))
	{
		Expr->SourceSpan = SpanForTokens(StartToken, EndToken);
	}
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
			const FMatLangToken SlotToken = Current();
			FString SlotName = SlotToken.Value;
			Advance();

			if (AST->Outputs.Slots.Contains(SlotName))
			{
				ErrorAt(SlotToken, TEXT("ML1201"),
					FString::Printf(TEXT("Duplicate material output ':%s'"), *SlotName));
			}
			
			FMatLangInput Input;
			Input.Name = SlotName;
			Input.SourceSpan = SpanForToken(SlotToken);
			
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
			AST->Outputs.SlotSpans.Add(SlotName, SpanForToken(SlotToken));
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
			else
			{
				Error(TEXT("Expected parameter type"));
			}
			
			// Parse key-value properties
			TSet<FString> SeenParamKeys;
			while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
			{
				if (Check(EMatLangTokenType::Keyword))
				{
					const FMatLangToken KeyToken = Current();
					FString Key = KeyToken.Value;
					Advance();
					if (SeenParamKeys.Contains(Key))
					{
						ErrorAt(KeyToken, TEXT("ML1201"),
							FString::Printf(TEXT("Duplicate parameter key ':%s'"), *Key));
					}
					SeenParamKeys.Add(Key);
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
	const FMatLangToken StartToken = Current();
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
		Conn.TargetSpan = SpanForToken(Current());
		if (!Current().IsExprId() || Current().Value.Len() <= 1)
		{
			ErrorAt(Current(), TEXT("ML1003"),
				TEXT("Connection target must be an expression ID beginning with '$'"));
		}
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
	
	const FMatLangToken EndToken = Current();
	if (Expect(EMatLangTokenType::RParen, TEXT("connect")))
	{
		Conn.SourceSpan = SpanForTokens(StartToken, EndToken);
	}
	return Conn;
}

void FMatLangParser::ParseFunctionParameters(TSharedPtr<FMaterialGraphAST> AST, bool bInputs)
{
	const FString BlockName = bInputs ? TEXT("function-inputs") : TEXT("function-outputs");
	Expect(EMatLangTokenType::LParen, BlockName);
	if (!CheckValue(EMatLangTokenType::Identifier, BlockName))
	{
		Error(FString::Printf(TEXT("Expected '%s' keyword"), *BlockName));
		Synchronize();
		return;
	}
	Advance();

	TSet<FString> SeenNames;
	while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
	{
		if (!Expect(EMatLangTokenType::LParen, TEXT("function parameter")))
		{
			Synchronize();
			break;
		}

		FMatParameterDef Parameter;
		const FString ExpectedKind = bInputs ? TEXT("input") : TEXT("output");
		if (!CheckValue(EMatLangTokenType::Identifier, ExpectedKind))
		{
			Error(FString::Printf(TEXT("Expected '%s' function parameter"), *ExpectedKind));
			Synchronize();
			continue;
		}
		Advance();

		TSet<FString> SeenKeys;
		while (!IsAtEnd() && !Check(EMatLangTokenType::RParen))
		{
			if (!Check(EMatLangTokenType::Keyword))
			{
				Error(TEXT("Expected keyword in function parameter"));
				Advance();
				continue;
			}

			const FMatLangToken KeyToken = Current();
			const FString Key = KeyToken.Value;
			Advance();
			if (SeenKeys.Contains(Key))
			{
				ErrorAt(KeyToken, TEXT("ML1201"),
					FString::Printf(TEXT("Duplicate function parameter key ':%s'"), *Key));
			}
			SeenKeys.Add(Key);

			if (Key == TEXT("name") && Check(EMatLangTokenType::String))
			{
				Parameter.Name = Current().Value;
				Advance();
			}
			else if (Key == TEXT("sort-priority") && Check(EMatLangTokenType::Integer))
			{
				Parameter.SortPriority = FCString::Atoi(*Current().Value);
				Advance();
			}
			else if (Key == TEXT("type"))
			{
				Parameter.Type = ParseValue();
			}
			else if (Key == TEXT("default"))
			{
				Parameter.DefaultValue = ParseValue();
			}
			else
			{
				ErrorAt(KeyToken, TEXT("ML0002"),
					FString::Printf(TEXT("Invalid value or unknown key ':%s' in function parameter"), *Key));
				if (IsValueStart()) ParseValue();
			}
		}
		Expect(EMatLangTokenType::RParen, TEXT("function parameter"));

		if (Parameter.Name.IsEmpty())
		{
			Error(TEXT("Function parameter requires a non-empty :name"));
		}
		else
		{
			if (SeenNames.Contains(Parameter.Name))
			{
				ErrorAt(Current(), TEXT("ML1201"),
					FString::Printf(TEXT("Duplicate function parameter name '%s'"), *Parameter.Name));
			}
			SeenNames.Add(Parameter.Name);
			(bInputs ? AST->FunctionInputs : AST->FunctionOutputs).Add(MoveTemp(Parameter));
		}
	}
	Expect(EMatLangTokenType::RParen, BlockName);
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
