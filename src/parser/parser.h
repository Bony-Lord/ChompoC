#pragma once

#include "ast.h"
#include "lexer/token.h"

#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parse();

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
    std::size_t function_depth_ = 0;

    StmtPtr declaration();
    StmtPtr var_declaration();
    StmtPtr function_declaration();

    StmtPtr statement();
    StmtPtr expression_statement();
    StmtPtr print_statement();
    StmtPtr block_statement();
    StmtPtr if_statement();
    StmtPtr return_statement();

    std::vector<StmtPtr> block();

    ExprPtr expression();
    ExprPtr parse_precedence(Precedence precedence);

    // Prefix-правила: токен начинает выражение
    ExprPtr literal();
    ExprPtr variable();
    ExprPtr grouping();
    ExprPtr unary();
    ExprPtr array_literal();

    // Infix-правила: токен продолжает левое выражение
    ExprPtr binary(ExprPtr left);
    ExprPtr assignment(ExprPtr left);
    ExprPtr call(ExprPtr callee);

    static const ParseRule &get_rule(TokenType type);
    static Precedence next_precedence(Precedence precedence);

    bool match(std::initializer_list<TokenType> types);
    bool check(TokenType type) const;
    bool is_at_end() const;

    const Token &advance();
    const Token &peek() const;
    const Token &previous() const;

    const Token &consume(TokenType type, std::string_view message);
    [[noreturn]] void error(const Token &token, std::string_view message) const;
};