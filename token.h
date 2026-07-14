#pragma once

#include <string>
#include <string_view>

enum class TokenType {
    LeftParen, // (
    RightParen, // )
    Semicolon, // ;
    Comma, // ,
    Dot, // .

    Plus, // +
    Minus, // -
    Star, // *
    Slash, // /

    Equal, // =

    // Литералы и имена
    Identifier,
    Number,
    String,

    // Ключевые слова
    Let,
    Print,
    EndOfFile,
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