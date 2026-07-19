#pragma once

#include "symbol.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class TokenType {
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    Dot,
    Colon,

    PlusOne,
    MinusOne,

    PlusEq,
    MinusEq,
    MulEq,
    DivideEq,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    Equal,
    EqualEqual,
    NotEqual,
    Not,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,

    Identifier,
    Number,
    String,
    Char,

    Var,
    Print,
    EndOfFile,
    Return,
    Continue,
    Break,
    If,
    Else,
    While,
    For,
    True,
    False,
    Null,
    Fun,
    Class,
    In,
    Array,
    Map,

    Count
};

enum class BindingKind : std::uint8_t {
    Unresolved,
    Global,
    Local,
};

struct SourcePosition {
    std::size_t line, column;
};

struct Token {
    TokenType type;
    std::string lexeme;
    SourcePosition position;
    SymbolId symbol = InvalidSymbol;

    // Filled by Resolver. Local bindings use a direct lexical address
    // (depth, slot); globals stay in the extensible symbol registry.
    BindingKind binding = BindingKind::Unresolved;
    std::uint32_t depth = 0;
    std::uint32_t slot = 0;

    // For function declaration tokens this is the number of slots required
    // by the function frame. It is ignored for ordinary tokens.
    std::uint32_t scope_slots = 0;
};

std::string_view token_type_name(TokenType type);
