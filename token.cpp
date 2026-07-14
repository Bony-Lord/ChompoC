#include "token.h"

std::string_view token_type_name(TokenType type) {
    switch (type) {
        case TokenType::LeftParen: return "LeftParen";
        case TokenType::RightParen: return "RightParen";
        case TokenType::Semicolon: return "Semicolon";
        case TokenType::Comma: return "Comma";
        case TokenType::Dot: return "Dot";

        case TokenType::Plus: return "Plus";
        case TokenType::Minus: return "Minus";
        case TokenType::Star: return "Star";
        case TokenType::Slash: return "Slash";

        case TokenType::Equal: return "Equal";

        case TokenType::Identifier: return "Identifier";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";

        case TokenType::Let: return "Let";
        case TokenType::Print: return "Print";
        case TokenType::EndOfFile: return "EndOfFile";
    }
    return "Unknown";
}