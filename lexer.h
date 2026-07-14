#pragma once

#include "token.h"

#include <string>
#include <string_view>
#include <vector>

class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> scan_tokens();
private:
    std::string source_;
    std::vector<Token> tokens_;

    std::size_t start_ = 0, current_ = 0;
    SourcePosition start_position_{1, 1};
    SourcePosition current_position_{1, 1};

    bool is_at_end() const;

    char advance();
    char peek() const;
    char peek_next() const;

    void scan_token();
    void number();
    void identifier();
    void string_literal();
    void add_token(TokenType type);

    static bool is_digit(char c);
    static bool is_alpha(char c);
    static bool is_alpha_numeric(char c);
    bool match(char c);

    [[noreturn]] void error(std::string_view message) const;
};