#pragma once

#include "ast.h"
#include "token.h"

#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    ExprPtr parse();
private:
    enum class Precedence {
        None,
        Assignment,
        Conditional,
        Or,
        And,
        Equality,
        Comparison,
        Term,
        Factor,
        Unary,
        Call,
        Primary
    };

    using PrefixFunction = ExprPtr (Parser::*)();
    using InfixFunction = ExprPtr (Parser::*)(ExprPtr left);

    struct ParseRule {
        PrefixFunction prefix = nullptr;
        InfixFunction infix = nullptr;
        Precedence precedence = Precedence::None;
    };

    std::vector<Token> tokens_;
    std::size_t current_ = 0;

    ExprPtr expression();
    ExprPtr parse_precedence(Precedence precedence);

    // Prefix-правила: токен начинает выражение
    ExprPtr literal();
    ExprPtr variable();
    ExprPtr grouping();
    ExprPtr unary();

    // Infix-правила: токен продолжает левое выражение
    ExprPtr binary(ExprPtr left);

    static const ParseRule& get_rule(TokenType type);
    static Precedence next_precedence(Precedence precedence);

    bool match(std::initializer_list<TokenType> types);
    bool check(TokenType type) const;
    bool is_at_end() const;

    const Token& advance();
    const Token& peek() const;
    const Token& previous() const;

    const Token& consume(TokenType type, std::string_view message);
    [[noreturn]] void error(const Token& token, std::string_view message) const;
};