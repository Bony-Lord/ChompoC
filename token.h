#pragma once

#include <string>
#include <string_view>
#include <cstddef>

enum class TokenType {
    LeftParen, // (
    RightParen, // )
    LeftBrace, // {
    RightBrace, // }
    LeftBracket, // [
    RightBracket, // ]
    Semicolon, // ;
    Comma, // ,
    Dot, // .
    Colon, // :

    Plus, // +
    Minus, // -
    Star, // *
    Slash, // /
    Percent, // %

    Equal, // =
    EqualEqual, //==
    NotEqual, // !=
    Not, // !
    Less, // <
    LessEqual, // <=
    Greater, // >
    GreaterEqual, // >=
    AndAnd, // &&
    OrOr, // ||

    // Литералы и имена
    Identifier,
    Number,
    String,

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
    In,



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