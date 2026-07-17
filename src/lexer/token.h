#pragma once

#include <cstddef>
#include <string>
#include <string_view>

enum class TokenType {
    LeftParen,    // (
    RightParen,   // )
    LeftBrace,    // {
    RightBrace,   // }
    LeftBracket,  // [
    RightBracket, // ]
    Semicolon,    // ;
    Comma,        // ,
    Dot,          // .
    Colon,        // :

    PlusOne,  // ++
    MinusOne, // --

    PlusEq,   // +=
    MinusEq,  // -=
    MulEq,    // *=
    DivideEq, // /=

    Plus,    // +
    Minus,   // -
    Star,    // *
    Slash,   // /
    Percent, // %

    Equal,        // =
    EqualEqual,   //==
    NotEqual,     // !=
    Not,          // !
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=
    AndAnd,       // &&
    OrOr,         // ||

    // Литералы и имена
    Identifier,
    Number,
    String,
    Char,

    // Ключевые слова
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

    Count
};

struct SourcePosition {
    std::size_t line, column;
};

struct Token {
    TokenType type;
    std::string lexeme;
    SourcePosition position;
};

std::string_view token_type_name(TokenType type);